/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Shader / program object: owns a GPU BO with uploaded machine code (or
 * placeholder code until NIR->ISA exists) and emits SET_PIPELINE_SHADER /
 * SET_PIPELINE_PROGRAM_ADDRESS per nvidia-3d nv3dLoadProgram.
 */

#ifndef NV_SHADER_H
#define NV_SHADER_H

#include <stdbool.h>
#include <stdint.h>

#include "nv_3d_methods.h"
#include "nv_push.h"

#ifdef __cplusplus
extern "C" {
#endif

struct nv_rm_device;
struct nv_rm_bo;

/* Gallium / Vulkan shader stage we care about for 3D */
enum nv_shader_kind {
   NV_SHADER_KIND_VERTEX = 0,
   NV_SHADER_KIND_FRAGMENT,
   NV_SHADER_KIND_GEOMETRY,
   NV_SHADER_KIND_TESS_CTRL,
   NV_SHADER_KIND_TESS_EVAL,
   NV_SHADER_KIND_COMPUTE,
};

struct nv_shader {
   struct nv_rm_device *rm;
   struct nv_rm_bo *code_bo;      /* shader binary / header+code */
   struct nv_rm_bo *const_bo;     /* optional compiler constants */
   uint64_t code_gpu_addr;
   uint64_t const_gpu_addr;
   uint32_t code_size;
   uint32_t const_size;
   uint32_t register_count;       /* SET_PIPELINE_REGISTER_COUNT */
   uint32_t bind_group;           /* SET_PIPELINE_BINDING group */
   uint32_t pipeline_stage;       /* NV_3D_PIPE_STAGE_* */
   uint32_t pipeline_type;        /* NVC597_SET_PIPELINE_SHADER_TYPE_* */
   enum nv_shader_kind kind;
   bool program_region_set;
   bool uploaded;
   void *nir;
   bool owns_nir;
};

struct nv_shader *
nv_shader_create(struct nv_rm_device *rm, enum nv_shader_kind kind);

void
nv_shader_destroy(struct nv_shader *sh);

int
nv_shader_upload_code(struct nv_shader *sh, const void *code, uint32_t code_size,
                      uint32_t register_count);

int
nv_shader_upload_constants(struct nv_shader *sh, const void *data, uint32_t size);

void
nv_shader_set_nir(struct nv_shader *sh, void *nir, bool take_ownership);

void
nv_shader_emit_bind(struct nv_push *p, const struct nv_shader *sh,
                    uint64_t program_region_base, int const_shader_slot);

void
nv_shader_fill_stage_defaults(struct nv_shader *sh);

/** Build SPH+EXIT (or later NIR->SASS) and upload; no-op if already uploaded. */
int
nv_shader_compile_nir_stub(struct nv_shader *sh);

/** Compile NIR via idep_nvidia_compiler (SPH+SASS object upload). */
struct nir_shader;
int
nv_shader_compile_nir(struct nv_shader *sh, const struct nir_shader *nir);

#ifdef __cplusplus
}
#endif

#endif /* NV_SHADER_H */
