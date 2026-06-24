/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Bridge: compile NIR and upload into nv_shader (links compiler + rm).
 */

#include "nv_nir.h"
#include "nv_shader.h"

#include <string.h>

int
nv_shader_compile_nir(struct nv_shader *sh, const struct nir_shader *nir)
{
   struct nv_compiler_options opts;
   struct nv_compiler_result res;
   int r;

   if (!sh || !sh->rm)
      return -1;
   if (sh->uploaded)
      return 0;

   memset(&opts, 0, sizeof(opts));
   opts.stage = nv_compiler_stage_from_shader_kind((int)sh->kind);
   opts.min_registers = sh->register_count ? sh->register_count : 8;

   if (!nv_nir_compile(nir, &opts, &res) || !res.success) {
      nv_compiler_result_finish(&res);
      return nv_shader_upload_code(sh, NULL, 0, opts.min_registers);
   }

   r = nv_shader_upload_code(sh, res.code, res.code_size, res.register_count);
   nv_compiler_result_finish(&res);
   return r;
}
