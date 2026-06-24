/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * NIR lowering pass (incremental):
 *  1. Inspect NIR metadata (stage, num_inputs/outputs, temps) for SPH fields
 *  2. Walk functions/blocks/instrs; for now emit SASS EXIT only
 *  3. Future: map nir_alu/load/store/tex to SM 5+/7+ instruction encodings
 *
 * We deliberately do NOT include full NIR headers in the common path when
 * building outside mesa (nv_shader.c links this via mesa idep_nir).  When
 * compiled inside mesa, HAVE_NIR is defined and we read real NIR stats.
 */

#include "nv_nir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Mesa NIR (optional at compile time) */
#if defined(HAVE_NIR) || defined(NIR_H)
#include "nir.h"
#include "nir_builder.h"
#define NV_HAVE_NIR 1
#else
#define NV_HAVE_NIR 0
/* Forward decl so nv_shader_compile_nir still compiles if headers missing */
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
static uint16_t
estimate_registers_from_nir(const nir_shader *nir, uint16_t min_regs)
{
   uint16_t regs = min_regs ? min_regs : 8;
   unsigned temps = 0;

   if (!nir)
      return regs;

   /* Count SSA defs / temps as a crude register pressure estimate */
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

   /* Rough: 1 temp per few SSA values, clamp */
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
#endif

/*
 * Emit minimal SASS program.  Real encoder will append instructions before EXIT.
 * SM 5.0–8.x EXIT: hi word 0x50b00000 is widely observed for predicate-true EXIT.
 */
static void
emit_sass_exit_only(struct nv_sph_blob *blob)
{
   memset(blob->sass, 0, sizeof(blob->sass));
   blob->sass[0] = NV_SASS_EXIT_LO;
   blob->sass[1] = NV_SASS_EXIT_HI;
   blob->sass_dwords = 2;
}

/* Placeholder for future: encode MOV R0, c[0][0] etc. before EXIT */
static void
emit_sass_prologue_stub(struct nv_sph_blob *blob, bool has_work)
{
   (void)has_work;
   emit_sass_exit_only(blob);
}

bool
nv_nir_compile(const struct nir_shader *nir,
               const struct nv_compiler_options *opts,
               struct nv_compiler_result *out)
{
   struct nv_sph_info info;
   struct nv_compiler_options def_opts;
   uint16_t regs = 8;
   uint8_t sph_type;
   unsigned num_instr = 0, num_blocks = 0;
   bool has_tex = false, has_store = false;
   uint8_t *code;
   uint32_t total;

   if (!out)
      return false;
   memset(out, 0, sizeof(*out));

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
   } else
#endif
   {
      regs = opts->min_registers ? opts->min_registers : 8;
      (void)num_instr;
      (void)num_blocks;
   }

   nv_sph_info_defaults(&info, sph_type);
   if (opts->sph_version)
      info.sph_version = opts->sph_version;
   info.register_count = regs;
   info.does_global_store = has_store;
   info.local_mem_low_size = 0;
   info.local_mem_high_size = 0;
   info.local_mem_crs_size = 0;

   nv_sph_encode(&info, out->blob.sph);
   emit_sass_prologue_stub(&out->blob, num_instr > 0);

   total = NV_SPH_BYTES + out->blob.sass_dwords * 4;
   if (total < NV_SPH_TOTAL_MIN_BYTES)
      total = NV_SPH_TOTAL_MIN_BYTES;
   total = (total + NV_SPH_CODE_ALIGN - 1) & ~(NV_SPH_CODE_ALIGN - 1);
   out->blob.total_bytes = total;

   code = calloc(1, total);
   if (!code) {
      snprintf(out->error, sizeof(out->error), "OOM");
      return false;
   }
   nv_sph_serialise(&out->blob, code, total);
   out->code = code;
   out->code_size = total;
   out->register_count = regs;
   out->local_mem_size = 0;
   out->success = true;
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

/* Implemented in rm/nv_shader.c when linking; weak stub here avoids dup if only compiler lib built */
#if 0
int nv_shader_compile_nir(struct nv_shader *sh, const struct nir_shader *nir);
#endif
