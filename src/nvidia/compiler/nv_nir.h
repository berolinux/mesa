/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * NIR -> NVIDIA shader object (SPH + SASS) compiler entry points.
 * Full instruction selection is incremental; early path lowers trivial
 * NIR (empty/const/move/return) to SPH + SASS EXIT (and later real ops).
 */

#ifndef NV_NIR_H
#define NV_NIR_H

#include <stdbool.h>
#include <stdint.h>

#include "nv_sph.h"

#ifdef __cplusplus
extern "C" {
#endif

struct nir_shader;
struct nv_shader;

enum nv_compiler_stage {
   NV_COMPILER_STAGE_VERTEX = 0,
   NV_COMPILER_STAGE_FRAGMENT,
   NV_COMPILER_STAGE_GEOMETRY,
   NV_COMPILER_STAGE_TESS_CTRL,
   NV_COMPILER_STAGE_TESS_EVAL,
   NV_COMPILER_STAGE_COMPUTE,
};

struct nv_compiler_options {
   enum nv_compiler_stage stage;
   uint8_t sph_version;     /* 0 = default VOLTA_PLUS */
   uint16_t min_registers;
   bool dump_ir;            /* stderr debug */
};

struct nv_compiler_result {
   struct nv_sph_blob blob;
   uint8_t *code;           /* owned; SPH+SASS serialised; free with free() */
   uint32_t code_size;
   uint32_t register_count;
   uint32_t local_mem_size;
   bool success;
   char error[256];
};

/** Compile NIR to SPH+SASS object bytes.  Does not upload to GPU. */
bool nv_nir_compile(const struct nir_shader *nir,
                    const struct nv_compiler_options *opts,
                    struct nv_compiler_result *out);

void nv_compiler_result_finish(struct nv_compiler_result *res);

/** Map nv_shader_kind / mesa stage to compiler stage. */
enum nv_compiler_stage nv_compiler_stage_from_shader_kind(int kind);

/** Compile NIR into an nv_shader (upload BO); returns 0 on success. */
int nv_shader_compile_nir(struct nv_shader *sh, const struct nir_shader *nir);

#ifdef __cplusplus
}
#endif

#endif /* NV_NIR_H */
