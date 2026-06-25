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
   uint32_t shared_mem_size;
   bool success;
   char error[256];
};

/** Compile NIR to SPH+SASS object bytes.  Does not upload to GPU. */
bool nv_nir_compile(const struct nir_shader *nir,
                    const struct nv_compiler_options *opts,
                    struct nv_compiler_result *out);

/**
 * G2 smoke without NIR: MOV32I addr/imm + STG.U32 + EXIT through compiler SPH
 * path (load_const/store_global equivalent).  Matches hand nv_sph_build_compute_store_imm.
 */
bool nv_nir_compile_g2_store_imm_smoke(uint32_t imm_value, uint64_t store_gpu_addr,
                                       uint16_t min_registers,
                                       struct nv_compiler_result *out);

/** Compare compiler G2 store-imm vs hand SPH builder; 0 = ok, negative = fail. */
int nv_nir_g2_store_imm_smoke_selftest(uint32_t imm_value, uint64_t store_gpu_addr,
                                       uint16_t regs);

/*
 * tick162 / pass22: NIR depth ladder via hand SPH builders (header path in
 * nv_sph.h: nv_pass22_nir_depth_*).  Full nv_nir_compile per-kind is incremental;
 * mesa channel/G2 should use nv_pass22_compute_object_* until compiler covers
 * EXIT / S2R / S2R+store / BAR variants uniformly.
 */
#define NV_PASS22_NIR_USES_HAND_SPH_LADDER  1

/**
 * tick163: compile one pass21/pass22 NIR depth kind through nv_sass smoke
 * emitters + SPH encode (compiler pipeline, no full NIR AST yet).  kind uses
 * nv_pass21_compute_shader_kind values from nv_sph.h.
 */
bool nv_nir_compile_g2_pass22_kind_smoke(int pass21_kind, uint32_t imm_value,
                                         uint64_t store_gpu_addr,
                                         uint16_t min_registers,
                                         struct nv_compiler_result *out);

/** tick163: host check — kind smoke builds valid compute SPH for all ladder kinds. */
int nv_nir_g2_pass22_kind_ladder_selftest(uint16_t regs);

/**
 * tick165: compute compile without full NIR AST — pass22 default depth kind
 * (S2R+store) via compiler SASS/SPH pipeline.  Used when nv_nir_compile has no
 * NIR or isel is incomplete; matches pass22 channel/Gallium policy.
 */
bool nv_nir_compile_compute_pass22_default(uint32_t imm_value,
                                           uint64_t store_gpu_addr,
                                           uint16_t min_registers,
                                           struct nv_compiler_result *out);

#define NV_PASS22_NIR_COMPUTE_DEFAULT_KIND  3 /* NV_PASS21_CS_S2R_STORE_IMM */

void nv_compiler_result_finish(struct nv_compiler_result *res);

/** Map nv_shader_kind / mesa stage to compiler stage. */
enum nv_compiler_stage nv_compiler_stage_from_shader_kind(int kind);

/** Compile NIR into an nv_shader (upload BO); returns 0 on success. */
int nv_shader_compile_nir(struct nv_shader *sh, const struct nir_shader *nir);

#ifdef __cplusplus
}
#endif

#endif /* NV_NIR_H */
