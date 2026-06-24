/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * NIR -> SPH + SASS compiler with incremental instruction selection.
 *
 * Walks NIR functions/blocks/instructions and emits SASS via nv_sass_* for
 * common ALU (iadd/fadd/fmul/ffma/mov/ineg/fneg/iand/ior/ixor), load_const,
 * some intrinsics (load/store_global, load_ubo stub, S2R for compute ids),
 * and tex (NOP placeholder).  Unhandled ops are skipped (DCE-style) and the
 * program always ends with EXIT.
 *
 * Register allocation is trivial SSA index -> R(n+1) with RZ=R255 unused.
 * Full RA/scheduling is future work.
 */

#include "nv_nir.h"
#include "nv_sass.h"
#include "nv_ra.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(HAVE_NIR) || defined(NIR_H)
#include "nir.h"
#include "nir_builder.h"
#define NV_HAVE_NIR 1
#else
#define NV_HAVE_NIR 0
struct nir_shader;
#endif

enum nv_compiler_stage
nv_compiler_stage_from_shader_kind(int kind)
{
   switch (kind) {
   case 0: return NV_COMPILER_STAGE_VERTEX;
   case 1: return NV_COMPILER_STAGE_FRAGMENT;
   case 2: return NV_COMPILER_STAGE_GEOMETRY;
   case 3: return NV_COMPILER_STAGE_TESS_CTRL;
   case 4: return NV_COMPILER_STAGE_TESS_EVAL;
   case 5: return NV_COMPILER_STAGE_COMPUTE;
   default: return NV_COMPILER_STAGE_VERTEX;
   }
}

static uint8_t
stage_to_sph_type(enum nv_compiler_stage stage)
{
   switch (stage) {
   case NV_COMPILER_STAGE_VERTEX:     return NV_SPH_TYPE_VERTEX;
   case NV_COMPILER_STAGE_FRAGMENT:   return NV_SPH_TYPE_PIXEL;
   case NV_COMPILER_STAGE_GEOMETRY:   return NV_SPH_TYPE_GEOMETRY;
   case NV_COMPILER_STAGE_TESS_CTRL:  return NV_SPH_TYPE_TESS_INIT;
   case NV_COMPILER_STAGE_TESS_EVAL:  return NV_SPH_TYPE_TESS;
   case NV_COMPILER_STAGE_COMPUTE:    return NV_SPH_TYPE_COMPUTE;
   default: return NV_SPH_TYPE_VERTEX;
   }
}

#if NV_HAVE_NIR

/* Active RA context for the shader being compiled (set in isel_shader). */
static struct nv_ra_context *nv_isel_ra;

/*
 * Spill model: spilled SSA values live in per-thread local memory at byte
 * offset (spill_slot * 4).  Address is built as MOV R254, imm_offset then
 * LDG/STG through that register (global-form load/store as stand-in for LDL/STL
 * until dedicated local-memory opcodes are wired; offset is absolute within
 * the spill region the QMD/SPH local_mem_low reservation covers).
 */
static bool
spill_emit_addr(struct nv_sass_buf *sb, uint16_t slot)
{
   uint32_t off = (uint32_t)slot * 4u;
   return nv_sass_emit_mov_ri(sb, NV_RA_SPILL_TMP_REG, off);
}

static bool
spill_store_reg(struct nv_sass_buf *sb, uint8_t data_reg, uint16_t slot)
{
   if (!spill_emit_addr(sb, slot))
      return false;
   return nv_sass_emit_stg_u32(sb, NV_RA_SPILL_TMP_REG, data_reg);
}

static bool
spill_load_to_tmp(struct nv_sass_buf *sb, uint16_t slot)
{
   if (!spill_emit_addr(sb, slot))
      return false;
   return nv_sass_emit_ldg_u32(sb, NV_RA_SPILL_TMP_REG, NV_RA_SPILL_TMP_REG);
}

/* Destination register for a def: spilled defs write R254 then STG spill slot. */
static uint8_t
ssa_reg_dst(nir_def *def)
{
   if (!def)
      return NV_RA_HW_RZ;
   if (nv_isel_ra && nv_ra_def_spilled(nv_isel_ra, def))
      return NV_RA_SPILL_TMP_REG;
   if (nv_isel_ra)
      return nv_ra_reg_for_def(nv_isel_ra, def);
   {
      unsigned idx = def->index + 1;
      if (idx >= NV_RA_SPILL_TMP_REG)
         idx = NV_RA_SPILL_TMP_REG - 1;
      return (uint8_t)idx;
   }
}

static bool
ssa_commit_dst(struct nv_sass_buf *sb, nir_def *def, uint8_t rd_used)
{
   uint16_t slot;
   if (!def || !nv_isel_ra || !nv_ra_def_spilled(nv_isel_ra, def))
      return true;
   slot = nv_ra_def_spill_slot(nv_isel_ra, def);
   if (slot == 0xffff)
      return true;
   /* rd_used should be R254; store it to spill slot */
   (void)rd_used;
   return spill_store_reg(sb, NV_RA_SPILL_TMP_REG, slot);
}

/* Source register: spilled defs reload into R254 before use. */
static uint8_t
ssa_reg(nir_def *def)
{
   if (!def)
      return NV_RA_HW_RZ;
   if (nv_isel_ra && nv_ra_def_spilled(nv_isel_ra, def))
      return NV_RA_SPILL_TMP_REG; /* caller must emit reload first via src_reg */
   if (nv_isel_ra)
      return nv_ra_reg_for_def(nv_isel_ra, def);
   {
      unsigned idx = def->index + 1;
      if (idx >= NV_RA_SPILL_TMP_REG)
         idx = NV_RA_SPILL_TMP_REG - 1;
      return (uint8_t)idx;
   }
}

static uint8_t
src_reg_reload(struct nv_sass_buf *sb, nir_src *src)
{
   nir_def *def;
   uint16_t slot;
   if (!src || !src->ssa)
      return NV_RA_HW_RZ;
   def = src->ssa;
   if (nv_isel_ra && nv_ra_def_spilled(nv_isel_ra, def)) {
      slot = nv_ra_def_spill_slot(nv_isel_ra, def);
      if (slot != 0xffff)
         spill_load_to_tmp(sb, slot);
      return NV_RA_SPILL_TMP_REG;
   }
   return ssa_reg(def);
}

static uint8_t
src_reg(nir_src *src)
{
   /* Without sb context in simple paths; prefer src_reg_reload in isel_alu */
   if (!src || !src->ssa)
      return NV_RA_HW_RZ;
   return ssa_reg(src->ssa);
}

static uint32_t
const_u32_from_load(nir_load_const_instr *lc, unsigned comp)
{
   if (!lc || comp >= lc->def.num_components)
      return 0;
   if (lc->def.bit_size == 64)
      return (uint32_t)lc->value[comp].u64;
   if (lc->def.bit_size == 16)
      return (uint32_t)lc->value[comp].u16;
   if (lc->def.bit_size == 8)
      return (uint32_t)lc->value[comp].u8;
   return lc->value[comp].u32;
}

static bool
isel_alu(struct nv_sass_buf *sb, nir_alu_instr *alu)
{
   uint8_t rd = ssa_reg_dst(&alu->def);
   uint8_t ra, rb, rc;
   nir_op op = alu->op;
   bool ok = false;

   switch (op) {
   case nir_op_mov:
   case nir_op_vec2:
   case nir_op_vec3:
   case nir_op_vec4:
   case nir_op_f2f32:
   case nir_op_i2i32:
   case nir_op_u2u32:
      ra = src_reg_reload(sb, &alu->src[0].src);
      ok = nv_sass_emit_mov_rr(sb, rd, ra);
      break;

   case nir_op_ineg:
      ra = src_reg_reload(sb, &alu->src[0].src);
      ok = nv_sass_emit_iadd_neg_rb(sb, rd, 0xff, ra);
      break;

   case nir_op_fneg:
      ra = src_reg_reload(sb, &alu->src[0].src);
      ok = nv_sass_emit_fadd_rrr(sb, rd, ra, 0xff);
      break;

   case nir_op_iabs:
      ra = src_reg_reload(sb, &alu->src[0].src);
      ok = nv_sass_emit_imnmx(sb, rd, ra, ra, true, true);
      break;

   case nir_op_fabs:
      ra = src_reg_reload(sb, &alu->src[0].src);
      ok = nv_sass_emit_fmnmx(sb, rd, ra, ra, true);
      break;

   case nir_op_iadd:
      ra = src_reg_reload(sb, &alu->src[0].src);
      rb = src_reg_reload(sb, &alu->src[1].src);
      ok = nv_sass_emit_iadd_rrr(sb, rd, ra, rb);
      break;

   case nir_op_isub:
      ra = src_reg_reload(sb, &alu->src[0].src);
      rb = src_reg_reload(sb, &alu->src[1].src);
      ok = nv_sass_emit_iadd_neg_rb(sb, rd, ra, rb);
      break;

   case nir_op_imul:
      ra = src_reg_reload(sb, &alu->src[0].src);
      rb = src_reg_reload(sb, &alu->src[1].src);
      ok = nv_sass_emit_imad_rrrr(sb, rd, ra, rb, 0xff);
      break;

   case nir_op_umad24:
   case nir_op_imad24_ir3:
      ra = src_reg_reload(sb, &alu->src[0].src);
      rb = src_reg_reload(sb, &alu->src[1].src);
      rc = src_reg_reload(sb, &alu->src[2].src);
      ok = nv_sass_emit_imad_rrrr(sb, rd, ra, rb, rc);
      break;

   case nir_op_fadd:
      ra = src_reg_reload(sb, &alu->src[0].src);
      rb = src_reg_reload(sb, &alu->src[1].src);
      ok = nv_sass_emit_fadd_rrr(sb, rd, ra, rb);
      break;

   case nir_op_fmul:
      ra = src_reg_reload(sb, &alu->src[0].src);
      rb = src_reg_reload(sb, &alu->src[1].src);
      ok = nv_sass_emit_fmul_rrr(sb, rd, ra, rb);
      break;

   case nir_op_ffma:
   case nir_op_ffmaz:
      ra = src_reg_reload(sb, &alu->src[0].src);
      rb = src_reg_reload(sb, &alu->src[1].src);
      rc = src_reg_reload(sb, &alu->src[2].src);
      ok = nv_sass_emit_ffma_rrrr(sb, rd, ra, rb, rc);
      break;

   case nir_op_iand:
      ra = src_reg_reload(sb, &alu->src[0].src);
      rb = src_reg_reload(sb, &alu->src[1].src);
      ok = nv_sass_emit_lop3(sb, rd, ra, rb, 0xff, 0xC0);
      break;

   case nir_op_ior:
      ra = src_reg_reload(sb, &alu->src[0].src);
      rb = src_reg_reload(sb, &alu->src[1].src);
      ok = nv_sass_emit_lop3(sb, rd, ra, rb, 0xff, 0xFC);
      break;

   case nir_op_ixor:
      ra = src_reg_reload(sb, &alu->src[0].src);
      rb = src_reg_reload(sb, &alu->src[1].src);
      ok = nv_sass_emit_lop3(sb, rd, ra, rb, 0xff, 0x3C);
      break;

   case nir_op_inot:
      ra = src_reg_reload(sb, &alu->src[0].src);
      ok = nv_sass_emit_lop3(sb, rd, ra, 0xff, 0xff, 0x33);
      break;

   case nir_op_ishl:
      ra = src_reg_reload(sb, &alu->src[0].src);
      rb = src_reg_reload(sb, &alu->src[1].src);
      ok = nv_sass_emit_shf_l(sb, rd, ra, rb);
      break;

   case nir_op_ishr:
      ra = src_reg_reload(sb, &alu->src[0].src);
      rb = src_reg_reload(sb, &alu->src[1].src);
      ok = nv_sass_emit_shf_r(sb, rd, ra, rb, true);
      break;

   case nir_op_ushr:
      ra = src_reg_reload(sb, &alu->src[0].src);
      rb = src_reg_reload(sb, &alu->src[1].src);
      ok = nv_sass_emit_shf_r(sb, rd, ra, rb, false);
      break;

   case nir_op_imin:
      ra = src_reg_reload(sb, &alu->src[0].src);
      rb = src_reg_reload(sb, &alu->src[1].src);
      ok = nv_sass_emit_imnmx(sb, rd, ra, rb, false, true);
      break;

   case nir_op_imax:
      ra = src_reg_reload(sb, &alu->src[0].src);
      rb = src_reg_reload(sb, &alu->src[1].src);
      ok = nv_sass_emit_imnmx(sb, rd, ra, rb, true, true);
      break;

   case nir_op_umin:
      ra = src_reg_reload(sb, &alu->src[0].src);
      rb = src_reg_reload(sb, &alu->src[1].src);
      ok = nv_sass_emit_imnmx(sb, rd, ra, rb, false, false);
      break;

   case nir_op_umax:
      ra = src_reg_reload(sb, &alu->src[0].src);
      rb = src_reg_reload(sb, &alu->src[1].src);
      ok = nv_sass_emit_imnmx(sb, rd, ra, rb, true, false);
      break;

   case nir_op_fmin:
      ra = src_reg_reload(sb, &alu->src[0].src);
      rb = src_reg_reload(sb, &alu->src[1].src);
      ok = nv_sass_emit_fmnmx(sb, rd, ra, rb, false);
      break;

   case nir_op_fmax:
      ra = src_reg_reload(sb, &alu->src[0].src);
      rb = src_reg_reload(sb, &alu->src[1].src);
      ok = nv_sass_emit_fmnmx(sb, rd, ra, rb, true);
      break;

   case nir_op_f2i32:
      ra = src_reg_reload(sb, &alu->src[0].src);
      ok = nv_sass_emit_f2i(sb, rd, ra, true);
      break;

   case nir_op_f2u32:
      ra = src_reg_reload(sb, &alu->src[0].src);
      ok = nv_sass_emit_f2i(sb, rd, ra, false);
      break;

   case nir_op_i2f32:
      ra = src_reg_reload(sb, &alu->src[0].src);
      ok = nv_sass_emit_i2f(sb, rd, ra, true);
      break;

   case nir_op_u2f32:
      ra = src_reg_reload(sb, &alu->src[0].src);
      ok = nv_sass_emit_i2f(sb, rd, ra, false);
      break;

   case nir_op_frcp:
      ra = src_reg_reload(sb, &alu->src[0].src);
      ok = nv_sass_emit_mufu(sb, rd, ra, NV_SASS_MUFU_RCP);
      break;

   case nir_op_frsq:
      ra = src_reg_reload(sb, &alu->src[0].src);
      ok = nv_sass_emit_mufu(sb, rd, ra, NV_SASS_MUFU_RSQ);
      break;

   case nir_op_fsqrt:
      ra = src_reg_reload(sb, &alu->src[0].src);
      ok = nv_sass_emit_mufu(sb, rd, ra, NV_SASS_MUFU_SQRT);
      break;

   case nir_op_flog2:
      ra = src_reg_reload(sb, &alu->src[0].src);
      ok = nv_sass_emit_mufu(sb, rd, ra, NV_SASS_MUFU_LG2);
      break;

   case nir_op_fexp2:
      ra = src_reg_reload(sb, &alu->src[0].src);
      ok = nv_sass_emit_mufu(sb, rd, ra, NV_SASS_MUFU_EX2);
      break;

   case nir_op_ffloor:
   case nir_op_fceil:
   case nir_op_ftrunc:
   case nir_op_fround_even:
      ra = src_reg_reload(sb, &alu->src[0].src);
      ok = nv_sass_emit_mov_rr(sb, rd, ra);
      break;

   case nir_op_bcsel: {
      uint8_t cond = src_reg_reload(sb, &alu->src[0].src);
      uint8_t tval = src_reg_reload(sb, &alu->src[1].src);
      uint8_t fval = src_reg_reload(sb, &alu->src[2].src);
      if (!nv_sass_emit_isetp(sb, 0, cond, 0xff, false, false))
         return false;
      ok = nv_sass_emit_selp(sb, rd, tval, fval, 0);
      break;
   }

   case nir_op_ieq:
   case nir_op_ine:
   case nir_op_ilt:
   case nir_op_ige:
   case nir_op_ult:
   case nir_op_uge: {
      bool is_signed = (op == nir_op_ilt || op == nir_op_ige);
      bool is_eq = (op == nir_op_ieq || op == nir_op_ine);
      ra = src_reg_reload(sb, &alu->src[0].src);
      rb = src_reg_reload(sb, &alu->src[1].src);
      if (!nv_sass_emit_isetp(sb, 0, ra, rb, is_signed, is_eq))
         return false;
      if (!nv_sass_emit_mov_ri(sb, rd, 1))
         return false;
      ok = nv_sass_emit_selp(sb, rd, rd, 0xff, 0);
      break;
   }

   case nir_op_feq:
   case nir_op_fneu:
   case nir_op_flt:
   case nir_op_fge:
      ra = src_reg_reload(sb, &alu->src[0].src);
      rb = src_reg_reload(sb, &alu->src[1].src);
      if (!nv_sass_emit_fsetp(sb, 0, ra, rb,
                             op == nir_op_feq || op == nir_op_fneu))
         return false;
      ok = nv_sass_emit_selp(sb, rd, ra, 0xff, 0);
      break;

   default:
      if (nir_op_infos[op].num_inputs >= 1) {
         ra = src_reg_reload(sb, &alu->src[0].src);
         ok = nv_sass_emit_mov_rr(sb, rd, ra);
      } else {
         ok = nv_sass_emit_nop(sb);
      }
      break;
   }

   if (!ok)
      return false;
   return ssa_commit_dst(sb, &alu->def, rd);
}

static bool
isel_intrinsic(struct nv_sass_buf *sb, nir_intrinsic_instr *intr)
{
   uint8_t rd, ra, rb;
   nir_intrinsic_op op = intr->intrinsic;
   bool ok = true;

   switch (op) {
   case nir_intrinsic_load_global:
   case nir_intrinsic_load_global_constant:
      rd = ssa_reg_dst(&intr->def);
      ra = src_reg_reload(sb, &intr->src[0]);
      ok = nv_sass_emit_ldg_u32(sb, rd, ra);
      break;

   case nir_intrinsic_store_global:
   case nir_intrinsic_store_ssbo:
      ra = src_reg_reload(sb, &intr->src[1]);
      rb = src_reg_reload(sb, &intr->src[0]);
      return nv_sass_emit_stg_u32(sb, ra, rb);

   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_uniform: {
      uint8_t bank = 0;
      uint16_t off_dw = 0;
      rd = ssa_reg_dst(&intr->def);
      if (nir_src_is_const(intr->src[0]))
         bank = (uint8_t)(nir_src_as_uint(intr->src[0]) & 0x1f);
      if (intr->intrinsic == nir_intrinsic_load_ubo &&
          nir_src_is_const(intr->src[1]))
         off_dw = (uint16_t)((nir_src_as_uint(intr->src[1]) / 4) & 0xffff);
      ok = nv_sass_emit_ldc(sb, rd, bank, off_dw);
      break;
   }

   case nir_intrinsic_load_push_constant:
      rd = ssa_reg_dst(&intr->def);
      {
         uint16_t off_dw = 0;
         if (nir_src_is_const(intr->src[0]))
            off_dw = (uint16_t)((nir_src_as_uint(intr->src[0]) / 4) & 0xffff);
         ok = nv_sass_emit_ldc(sb, rd, 1, off_dw);
      }
      break;

   case nir_intrinsic_load_invocation_id:
   case nir_intrinsic_load_local_invocation_id:
      rd = ssa_reg_dst(&intr->def);
      ok = nv_sass_emit_s2r(sb, rd, NV_SASS_SR_TID_X);
      break;

   case nir_intrinsic_load_workgroup_id:
      rd = ssa_reg_dst(&intr->def);
      ok = nv_sass_emit_s2r(sb, rd, NV_SASS_SR_CTAID_X);
      break;

   case nir_intrinsic_load_num_workgroups:
      rd = ssa_reg_dst(&intr->def);
      ok = nv_sass_emit_s2r(sb, rd, NV_SASS_SR_NCTAID_X);
      break;

   case nir_intrinsic_load_workgroup_size:
      rd = ssa_reg_dst(&intr->def);
      ok = nv_sass_emit_s2r(sb, rd, NV_SASS_SR_NTID_X);
      break;

   case nir_intrinsic_load_subgroup_invocation:
   case nir_intrinsic_load_helper_invocation:
      rd = ssa_reg_dst(&intr->def);
      ok = nv_sass_emit_s2r(sb, rd, NV_SASS_SR_LANEID);
      break;

   case nir_intrinsic_barrier:
      if (!nv_sass_emit_membar(sb))
         return false;
      return nv_sass_emit_bar_sync(sb, 0);

   case nir_intrinsic_global_atomic:
   case nir_intrinsic_global_atomic_swap:
   case nir_intrinsic_ssbo_atomic:
   case nir_intrinsic_ssbo_atomic_swap: {
      uint8_t atom_op = NV_SASS_ATOM_OP_ADD;
      rd = ssa_reg_dst(&intr->def);
      ra = src_reg_reload(sb, &intr->src[0]);
      rb = src_reg_reload(sb, &intr->src[1]);
      if (op == nir_intrinsic_global_atomic_swap ||
          op == nir_intrinsic_ssbo_atomic_swap)
         atom_op = NV_SASS_ATOM_OP_CAS;
      ok = nv_sass_emit_atom(sb, rd, ra, rb, atom_op);
      break;
   }

   case nir_intrinsic_shuffle:
   case nir_intrinsic_shuffle_xor:
   case nir_intrinsic_shuffle_up:
   case nir_intrinsic_shuffle_down: {
      uint8_t mode = NV_SASS_SHFL_MODE_IDX;
      rd = ssa_reg_dst(&intr->def);
      ra = src_reg_reload(sb, &intr->src[0]);
      rb = src_reg_reload(sb, &intr->src[1]);
      if (op == nir_intrinsic_shuffle_xor)
         mode = NV_SASS_SHFL_MODE_BFLY;
      else if (op == nir_intrinsic_shuffle_up)
         mode = NV_SASS_SHFL_MODE_UP;
      else if (op == nir_intrinsic_shuffle_down)
         mode = NV_SASS_SHFL_MODE_DOWN;
      ok = nv_sass_emit_shfl(sb, rd, ra, rb, mode);
      break;
   }

   case nir_intrinsic_load_frag_coord:
   case nir_intrinsic_load_front_face:
   case nir_intrinsic_load_vertex_id:
   case nir_intrinsic_load_instance_id:
   case nir_intrinsic_load_base_instance:
   case nir_intrinsic_load_base_vertex:
   case nir_intrinsic_load_draw_id:
   case nir_intrinsic_load_primitive_id:
   case nir_intrinsic_load_sample_id:
   case nir_intrinsic_load_sample_pos:
   case nir_intrinsic_load_sample_mask_in:
   case nir_intrinsic_load_view_index:
   case nir_intrinsic_load_input:
   case nir_intrinsic_load_interpolated_input:
   case nir_intrinsic_load_per_vertex_input:
   case nir_intrinsic_store_output:
   case nir_intrinsic_store_per_vertex_output:
   case nir_intrinsic_store_deref:
   case nir_intrinsic_load_deref:
   case nir_intrinsic_copy_deref:
   case nir_intrinsic_decl_reg:
   case nir_intrinsic_load_reg:
   case nir_intrinsic_store_reg:
      if (nir_intrinsic_infos[op].has_dest) {
         rd = ssa_reg_dst(&intr->def);
         ok = nv_sass_emit_mov_ri(sb, rd, 0);
         break;
      }
      return nv_sass_emit_nop(sb);

   default:
      if (nir_intrinsic_infos[op].has_dest) {
         rd = ssa_reg_dst(&intr->def);
         ok = nv_sass_emit_mov_ri(sb, rd, 0);
         break;
      }
      return true;
   }

   if (!ok)
      return false;
   if (nir_intrinsic_infos[op].has_dest)
      return ssa_commit_dst(sb, &intr->def, rd);
   return true;
}

static bool
isel_tex(struct nv_sass_buf *sb, nir_tex_instr *tex)
{
   uint8_t rd = ssa_reg_dst(&tex->def);
   uint8_t ra = 0;
   uint8_t tex_idx = 0;
   unsigned i;
   bool ok;

   sb->has_tex = true;
   for (i = 0; i < tex->num_srcs; i++) {
      if (tex->src[i].src_type == nir_tex_src_coord ||
          tex->src[i].src_type == nir_tex_src_backend1)
         ra = src_reg_reload(sb, &tex->src[i].src);
      if (tex->src[i].src_type == nir_tex_src_texture_handle ||
          tex->src[i].src_type == nir_tex_src_sampler_handle)
         tex_idx = (uint8_t)(tex->texture_index & 0xff);
   }
   if (!tex_idx)
      tex_idx = (uint8_t)(tex->texture_index & 0xff);

   switch (tex->op) {
   case nir_texop_txf:
   case nir_texop_txf_ms:
      ok = nv_sass_emit_tld(sb, rd, ra, tex_idx);
      break;
   case nir_texop_tex:
   case nir_texop_txb:
   case nir_texop_txl:
   case nir_texop_txd:
   default:
      ok = nv_sass_emit_tex(sb, rd, ra, tex_idx);
      break;
   }
   if (!ok)
      return false;
   return ssa_commit_dst(sb, &tex->def, rd);
}

static bool
isel_load_const(struct nv_sass_buf *sb, nir_load_const_instr *lc)
{
   uint8_t rd = ssa_reg_dst(&lc->def);
   uint32_t imm = const_u32_from_load(lc, 0);
   if (!nv_sass_emit_mov_ri(sb, rd, imm))
      return false;
   return ssa_commit_dst(sb, &lc->def, rd);
}

static bool
isel_instr(struct nv_sass_buf *sb, nir_instr *instr)
{
   switch (instr->type) {
   case nir_instr_type_alu:
      return isel_alu(sb, nir_instr_as_alu(instr));
   case nir_instr_type_intrinsic:
      return isel_intrinsic(sb, nir_instr_as_intrinsic(instr));
   case nir_instr_type_tex:
      return isel_tex(sb, nir_instr_as_tex(instr));
   case nir_instr_type_load_const:
      return isel_load_const(sb, nir_instr_as_load_const(instr));
   case nir_instr_type_undef:
   case nir_instr_type_phi:
   case nir_instr_type_jump:
   case nir_instr_type_call:
   case nir_instr_type_deref:
      return true;
   default:
      return true;
   }
}

static bool
isel_shader(struct nv_sass_buf *sb, const nir_shader *nir,
            struct nv_ra_context *ra)
{
   struct nv_ra_context *prev_ra = nv_isel_ra;

   if (!nir) {
      nv_isel_ra = prev_ra;
      return nv_sass_emit_exit(sb);
   }

   if (ra) {
      nv_ra_allocate(ra, nir);
      nv_isel_ra = ra;
   } else {
      nv_isel_ra = NULL;
   }

   nir_foreach_function (func, nir) {
      if (!func->impl)
         continue;
      nir_index_ssa_defs(func->impl);
      nir_foreach_block (block, func->impl) {
         nir_foreach_instr (instr, block) {
            if (!isel_instr(sb, instr)) {
               nv_isel_ra = prev_ra;
               return false;
            }
         }
      }
   }

   nv_isel_ra = prev_ra;
   return nv_sass_emit_exit(sb);
}

static uint16_t
estimate_registers_from_nir(const nir_shader *nir, uint16_t min_regs)
{
   uint16_t regs = min_regs ? min_regs : 8;
   unsigned temps = 0;

   if (!nir)
      return regs;

   nir_foreach_function (func, nir) {
      if (!func->impl)
         continue;
      nir_foreach_block (block, func->impl) {
         nir_foreach_instr (instr, block) {
            if (instr->type == nir_instr_type_alu ||
                instr->type == nir_instr_type_intrinsic ||
                instr->type == nir_instr_type_tex ||
                instr->type == nir_instr_type_load_const)
               temps++;
         }
      }
   }

   if (temps / 4 + 4 > regs)
      regs = (uint16_t)(temps / 4 + 4);
   if (regs < 4)
      regs = 4;
   if (regs > 255)
      regs = 255;
   return regs;
}

static void
nir_stats(const nir_shader *nir, unsigned *num_instr, unsigned *num_blocks,
          bool *has_tex, bool *has_store_global)
{
   *num_instr = 0;
   *num_blocks = 0;
   *has_tex = false;
   *has_store_global = false;
   if (!nir)
      return;
   nir_foreach_function (func, nir) {
      if (!func->impl)
         continue;
      nir_foreach_block (block, func->impl) {
         (*num_blocks)++;
         nir_foreach_instr (instr, block) {
            (*num_instr)++;
            if (instr->type == nir_instr_type_tex)
               *has_tex = true;
            if (instr->type == nir_instr_type_intrinsic) {
               nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
               if (intr->intrinsic == nir_intrinsic_store_global ||
                   intr->intrinsic == nir_intrinsic_store_ssbo)
                  *has_store_global = true;
            }
         }
      }
   }
}
#endif /* NV_HAVE_NIR */

/*
 * Copy SASS into sph_blob limited buffer; if more dwords than sass[16],
 * allocate via result->code only (SPH serialise uses blob sass first 16).
 */
static void
fill_sph_blob_sass(struct nv_sph_blob *blob, const struct nv_sass_buf *sb)
{
   uint32_t n;
   memset(blob->sass, 0, sizeof(blob->sass));
   n = nv_sass_buf_copy_out(sb, blob->sass,
                            (uint32_t)(sizeof(blob->sass) / sizeof(blob->sass[0])));
   if (n == 0) {
      blob->sass[0] = NV_SASS_EXIT_LO;
      blob->sass[1] = NV_SASS_EXIT_HI;
      blob->sass_dwords = 2;
   } else {
      blob->sass_dwords = n;
   }
}

bool
nv_nir_compile(const struct nir_shader *nir,
               const struct nv_compiler_options *opts,
               struct nv_compiler_result *out)
{
   struct nv_sph_info info;
   struct nv_compiler_options def_opts;
   struct nv_sass_buf sass;
   struct nv_ra_context ra;
   uint16_t regs = 8;
   uint32_t spill_local = 0;
   uint8_t sph_type;
   unsigned num_instr = 0, num_blocks = 0;
   bool has_tex = false, has_store = false;
   uint8_t *code;
   uint32_t total;
   uint32_t sass_bytes;

   if (!out)
      return false;
   memset(out, 0, sizeof(*out));
   nv_sass_buf_init(&sass);
   nv_ra_context_init(&ra);

   if (!opts) {
      memset(&def_opts, 0, sizeof(def_opts));
      def_opts.stage = NV_COMPILER_STAGE_VERTEX;
      def_opts.min_registers = 8;
      opts = &def_opts;
   }

   sph_type = stage_to_sph_type(opts->stage);

#if NV_HAVE_NIR
   if (nir) {
      regs = estimate_registers_from_nir(nir, opts->min_registers);
      nir_stats(nir, &num_instr, &num_blocks, &has_tex, &has_store);
      if (opts->dump_ir)
         fprintf(stderr, "nv_nir: stage=%u instr=%u blocks=%u tex=%d store=%d regs=%u\n",
                 (unsigned)opts->stage, num_instr, num_blocks,
                 (int)has_tex, (int)has_store, (unsigned)regs);

      if (!isel_shader(&sass, nir, &ra)) {
         snprintf(out->error, sizeof(out->error), "SASS isel OOM");
         nv_sass_buf_finish(&sass);
         nv_ra_context_finish(&ra);
         return false;
      }
      regs = nv_ra_register_count(&ra, opts->min_registers ? opts->min_registers : 8);
      spill_local = nv_ra_spill_local_bytes(&ra);
      if (sass.max_reg > regs)
         regs = sass.max_reg;
      if (sass.has_global_store)
         has_store = true;
      if (sass.has_tex)
         has_tex = true;
   } else
#endif
   {
      regs = opts->min_registers ? opts->min_registers : 8;
      (void)num_instr;
      (void)num_blocks;
      (void)has_tex;
      nv_sass_emit_exit(&sass);
   }

   if (regs < 4)
      regs = 4;
   if (regs > 255)
      regs = 255;

   nv_sph_info_defaults(&info, sph_type);
   if (opts->sph_version)
      info.sph_version = opts->sph_version;
   info.register_count = regs;
   info.does_global_store = has_store;
   /* RA spill slots map into local mem low partition (byte offsets 0..N) */
   info.local_mem_low_size = spill_local;
   info.local_mem_high_size = 0;
   info.local_mem_crs_size = 0;

   nv_sph_encode(&info, out->blob.sph);
   fill_sph_blob_sass(&out->blob, &sass);

   /* Prefer full sass stream in result code if larger than embedded sass[] */
   sass_bytes = sass.count * 4;
   if (sass_bytes < out->blob.sass_dwords * 4)
      sass_bytes = out->blob.sass_dwords * 4;

   total = NV_SPH_BYTES + sass_bytes;
   if (total < NV_SPH_TOTAL_MIN_BYTES)
      total = NV_SPH_TOTAL_MIN_BYTES;
   total = (total + NV_SPH_CODE_ALIGN - 1) & ~(NV_SPH_CODE_ALIGN - 1);
   out->blob.total_bytes = total;

   code = calloc(1, total);
   if (!code) {
      snprintf(out->error, sizeof(out->error), "OOM");
      nv_sass_buf_finish(&sass);
      nv_ra_context_finish(&ra);
      return false;
   }
   memcpy(code, out->blob.sph, NV_SPH_BYTES);
   if (sass.dwords && sass.count)
      memcpy(code + NV_SPH_BYTES, sass.dwords, sass.count * 4);
   else
      memcpy(code + NV_SPH_BYTES, out->blob.sass, out->blob.sass_dwords * 4);

   out->code = code;
   out->code_size = total;
   out->register_count = regs;
   out->local_mem_size = spill_local;
   out->success = true;
   nv_sass_buf_finish(&sass);
   nv_ra_context_finish(&ra);
   return true;
}

void
nv_compiler_result_finish(struct nv_compiler_result *res)
{
   if (!res)
      return;
   free(res->code);
   res->code = NULL;
   res->code_size = 0;
   res->success = false;
}
