/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 */

#include "nv_ra.h"

#include <string.h>

#if defined(HAVE_NIR) || defined(NIR_H)
#include "nir.h"
#define NV_RA_HAVE_NIR 1
#else
#define NV_RA_HAVE_NIR 0
struct nir_shader;
struct nir_def;
#endif

void
nv_ra_context_init(struct nv_ra_context *ra)
{
   memset(ra, 0, sizeof(*ra));
   ra->ok = true;
}

void
nv_ra_context_finish(struct nv_ra_context *ra)
{
   if (!ra)
      return;
   memset(ra, 0, sizeof(*ra));
}

uint8_t
nv_ra_reg_for_def(const struct nv_ra_context *ra, const struct nir_def *def)
{
   unsigned idx;
   if (!ra || !def)
      return NV_RA_HW_RZ;
   idx = def->index;
   if (idx >= NV_RA_MAX_SSA || !ra->live[idx].active)
      return (uint8_t)((idx % NV_RA_HW_LAST) + NV_RA_HW_FIRST);
   return ra->live[idx].hw_reg ? ra->live[idx].hw_reg
                               : (uint8_t)((idx % NV_RA_HW_LAST) + NV_RA_HW_FIRST);
}

uint16_t
nv_ra_register_count(const struct nv_ra_context *ra, uint16_t min_regs)
{
   uint16_t n = min_regs ? min_regs : 8;
   if (ra && ra->max_hw_reg > n)
      n = ra->max_hw_reg;
   /* Reserve R254 as spill temp when any spill occurred */
   if (ra && ra->had_spill && n < NV_RA_SPILL_TMP_REG + 1)
      n = NV_RA_SPILL_TMP_REG + 1;
   if (n < 4)
      n = 4;
   if (n > 255)
      n = 255;
   return n;
}

bool
nv_ra_def_spilled(const struct nv_ra_context *ra, const struct nir_def *def)
{
   unsigned idx;
   if (!ra || !def)
      return false;
   idx = def->index;
   if (idx >= NV_RA_MAX_SSA || !ra->live[idx].active)
      return false;
   return ra->live[idx].spilled;
}

uint16_t
nv_ra_def_spill_slot(const struct nv_ra_context *ra, const struct nir_def *def)
{
   unsigned idx;
   if (!ra || !def)
      return 0xffff;
   idx = def->index;
   if (idx >= NV_RA_MAX_SSA || !ra->live[idx].active || !ra->live[idx].spilled)
      return 0xffff;
   return ra->live[idx].spill_slot;
}

#if NV_RA_HAVE_NIR

static void
note_def(struct nv_ra_context *ra, nir_def *def, uint32_t pt)
{
   unsigned idx;
   if (!def)
      return;
   idx = def->index;
   if (idx >= NV_RA_MAX_SSA)
      return;
   if (!ra->live[idx].active) {
      ra->live[idx].active = true;
      ra->live[idx].start = pt;
      ra->live[idx].end = pt;
      if (idx + 1 > ra->num_ssa)
         ra->num_ssa = (uint16_t)(idx + 1);
   } else {
      if (pt < ra->live[idx].start)
         ra->live[idx].start = pt;
      if (pt > ra->live[idx].end)
         ra->live[idx].end = pt;
   }
}

static void
note_src(struct nv_ra_context *ra, nir_src *src, uint32_t pt)
{
   if (!src || !src->ssa)
      return;
   note_def(ra, src->ssa, pt);
}

static void
scan_instr(struct nv_ra_context *ra, nir_instr *instr, uint32_t pt)
{
   switch (instr->type) {
   case nir_instr_type_alu: {
      nir_alu_instr *alu = nir_instr_as_alu(instr);
      unsigned i, n = nir_op_infos[alu->op].num_inputs;
      for (i = 0; i < n; i++)
         note_src(ra, &alu->src[i].src, pt);
      note_def(ra, &alu->def, pt);
      break;
   }
   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
      unsigned i, n = nir_intrinsic_infos[intr->intrinsic].num_srcs;
      for (i = 0; i < n; i++)
         note_src(ra, &intr->src[i], pt);
      if (nir_intrinsic_infos[intr->intrinsic].has_dest)
         note_def(ra, &intr->def, pt);
      break;
   }
   case nir_instr_type_tex: {
      nir_tex_instr *tex = nir_instr_as_tex(instr);
      unsigned i;
      for (i = 0; i < tex->num_srcs; i++)
         note_src(ra, &tex->src[i].src, pt);
      note_def(ra, &tex->def, pt);
      break;
   }
   case nir_instr_type_load_const:
      note_def(ra, &nir_instr_as_load_const(instr)->def, pt);
      break;
   case nir_instr_type_undef:
      note_def(ra, &nir_instr_as_undef(instr)->def, pt);
      break;
   case nir_instr_type_phi: {
      nir_phi_instr *phi = nir_instr_as_phi(instr);
      nir_foreach_phi_src (ps, phi)
         note_src(ra, &ps->src, pt);
      note_def(ra, &phi->def, pt);
      break;
   }
   default:
      break;
   }
}

/* Insertion sort on order[] by live range start (then SSA index). */
static void
sort_ssa_by_start(struct nv_ra_context *ra, unsigned *order, unsigned n)
{
   unsigned i, j;
   for (i = 1; i < n; i++) {
      unsigned key = order[i];
      j = i;
      while (j > 0) {
         unsigned prev = order[j - 1];
         if (ra->live[prev].start < ra->live[key].start ||
             (ra->live[prev].start == ra->live[key].start && prev < key))
            break;
         order[j] = prev;
         j--;
      }
      order[j] = key;
   }
}

static void
assign_registers(struct nv_ra_context *ra)
{
   /* Linear scan in live-range start order: at each interval, expire ended
    * ranges (free their regs) then assign lowest free hw reg; if pressure
    * would touch R254 (spill temp), mark spill slot instead. */
   unsigned i, k;
   unsigned order[NV_RA_MAX_SSA];
   unsigned n_active = 0;
   bool used[256];
   unsigned active_list[256];
   unsigned n_active_list = 0;

   n_active = 0;
   for (i = 0; i < ra->num_ssa && i < NV_RA_MAX_SSA; i++) {
      if (!ra->live[i].active)
         continue;
      order[n_active++] = i;
   }
   if (!n_active)
      return;
   sort_ssa_by_start(ra, order, n_active);

   memset(used, 0, sizeof(used));
   n_active_list = 0;

   for (k = 0; k < n_active; k++) {
      unsigned idx = order[k];
      uint32_t cur_start = ra->live[idx].start;
      uint8_t pick;
      unsigned a;

      /* Expire intervals that ended before cur_start */
      for (a = 0; a < n_active_list; ) {
         unsigned aid = active_list[a];
         if (ra->live[aid].end < cur_start && ra->live[aid].hw_reg &&
             !ra->live[aid].spilled) {
            used[ra->live[aid].hw_reg] = false;
            active_list[a] = active_list[--n_active_list];
         } else {
            a++;
         }
      }

      /* Find lowest free register strictly below spill temp */
      for (pick = NV_RA_HW_FIRST; pick < NV_RA_SPILL_TMP_REG; pick++) {
         if (!used[pick])
            break;
      }

      if (pick >= NV_RA_SPILL_TMP_REG) {
         ra->live[idx].spilled = true;
         ra->live[idx].spill_slot = ra->spill_count++;
         ra->live[idx].hw_reg = NV_RA_HW_RZ;
         ra->had_spill = true;
         if (n_active_list < 256)
            active_list[n_active_list++] = idx;
         continue;
      }

      ra->live[idx].hw_reg = pick;
      ra->live[idx].spilled = false;
      used[pick] = true;
      if (pick + 1 > ra->max_hw_reg)
         ra->max_hw_reg = (uint16_t)(pick + 1);
      if (n_active_list < 256)
         active_list[n_active_list++] = idx;
   }
   if (ra->had_spill && ra->max_hw_reg < NV_RA_SPILL_TMP_REG + 1)
      ra->max_hw_reg = NV_RA_SPILL_TMP_REG + 1;
}

bool
nv_ra_allocate(struct nv_ra_context *ra, const struct nir_shader *nir)
{
   uint32_t pt = 0;

   if (!ra)
      return false;
   nv_ra_context_init(ra);
   ra->spill_count = 0;
   ra->had_spill = false;
   if (!nir) {
      ra->ok = true;
      return true;
   }

   nir_foreach_function (func, nir) {
      if (!func->impl)
         continue;
      nir_index_ssa_defs(func->impl);
      nir_foreach_block (block, func->impl) {
         nir_foreach_instr (instr, block) {
            scan_instr(ra, instr, pt);
            pt++;
         }
      }
   }

   assign_registers(ra);
   ra->ok = true;
   return true;
}

#else /* !NV_RA_HAVE_NIR */

bool
nv_ra_allocate(struct nv_ra_context *ra, const struct nir_shader *nir)
{
   (void)nir;
   if (!ra)
      return false;
   nv_ra_context_init(ra);
   ra->ok = true;
   return true;
}

#endif
