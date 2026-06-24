/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 */

#include "nv_shader.h"

#include "nv_rm.h"

#include <stdlib.h>
#include <string.h>

#define NV_SHADER_MIN_CODE_SIZE  256
#define NV_SHADER_CODE_ALIGN     256

void
nv_shader_fill_stage_defaults(struct nv_shader *sh)
{
   if (!sh)
      return;

   switch (sh->kind) {
   case NV_SHADER_KIND_VERTEX:
      sh->pipeline_stage = NV_3D_PIPE_STAGE_VERTEX;
      sh->pipeline_type = NVC597_SET_PIPELINE_SHADER_TYPE_VERTEX;
      sh->bind_group = NV_3D_BIND_GROUP_VERTEX;
      break;
   case NV_SHADER_KIND_FRAGMENT:
      sh->pipeline_stage = NV_3D_PIPE_STAGE_PIXEL;
      sh->pipeline_type = NVC597_SET_PIPELINE_SHADER_TYPE_PIXEL;
      sh->bind_group = NV_3D_BIND_GROUP_PIXEL;
      break;
   case NV_SHADER_KIND_GEOMETRY:
      sh->pipeline_stage = NV_3D_PIPE_STAGE_GEOMETRY;
      sh->pipeline_type = NVC597_SET_PIPELINE_SHADER_TYPE_GEOMETRY;
      sh->bind_group = 3;
      break;
   case NV_SHADER_KIND_TESS_CTRL:
      sh->pipeline_stage = NV_3D_PIPE_STAGE_TESS_INIT;
      sh->pipeline_type = NVC597_SET_PIPELINE_SHADER_TYPE_TESSELLATION_INIT;
      sh->bind_group = 1;
      break;
   case NV_SHADER_KIND_TESS_EVAL:
      sh->pipeline_stage = NV_3D_PIPE_STAGE_TESS;
      sh->pipeline_type = NVC597_SET_PIPELINE_SHADER_TYPE_TESSELLATION;
      sh->bind_group = 2;
      break;
   case NV_SHADER_KIND_COMPUTE:
   default:
      sh->pipeline_stage = 0;
      sh->pipeline_type = 0;
      sh->bind_group = 0;
      break;
   }

   if (!sh->register_count)
      sh->register_count = 16;
}

struct nv_shader *
nv_shader_create(struct nv_rm_device *rm, enum nv_shader_kind kind)
{
   struct nv_shader *sh;

   sh = calloc(1, sizeof(*sh));
   if (!sh)
      return NULL;

   sh->rm = rm;
   sh->kind = kind;
   nv_shader_fill_stage_defaults(sh);
   return sh;
}

void
nv_shader_destroy(struct nv_shader *sh)
{
   if (!sh)
      return;

   if (sh->code_bo) {
      nv_rm_bo_unmap(sh->code_bo);
      nv_rm_bo_free(sh->code_bo);
   }
   if (sh->const_bo) {
      nv_rm_bo_unmap(sh->const_bo);
      nv_rm_bo_free(sh->const_bo);
   }
   free(sh);
}

void
nv_shader_set_nir(struct nv_shader *sh, void *nir, bool take_ownership)
{
   if (!sh)
      return;
   sh->nir = nir;
   sh->owns_nir = take_ownership;
}

static struct nv_rm_bo *
nv_shader_alloc_upload_bo(struct nv_rm_device *rm, const void *data,
                          uint32_t size, uint32_t min_size, uint64_t *gpu_out)
{
   struct nv_rm_bo_req req;
   struct nv_rm_bo *bo;
   void *map;
   uint32_t alloc_size;

   if (!rm || !size)
      return NULL;

   alloc_size = size;
   if (alloc_size < min_size)
      alloc_size = min_size;
   alloc_size = (alloc_size + NV_SHADER_CODE_ALIGN - 1) & ~(NV_SHADER_CODE_ALIGN - 1);

   memset(&req, 0, sizeof(req));
   req.size = alloc_size;
   req.alignment = 4096;
   req.vram = false;
   req.cpu_access = true;
   req.no_scanout = true;
   req.map_gpu_va = true;

   bo = nv_rm_bo_alloc(rm, &req);
   if (!bo)
      return NULL;

   map = nv_rm_bo_map(bo);
   if (!map) {
      nv_rm_bo_free(bo);
      return NULL;
   }

   memset(map, 0, alloc_size);
   if (data && size)
      memcpy(map, data, size);

   if (gpu_out)
      *gpu_out = nv_rm_bo_gpu_offset(bo);
   return bo;
}

int
nv_shader_upload_code(struct nv_shader *sh, const void *code, uint32_t code_size,
                      uint32_t register_count)
{
   struct nv_rm_bo *bo;
   uint64_t gpu = 0;
   static const uint8_t zero_placeholder[NV_SHADER_MIN_CODE_SIZE];

   if (!sh || !sh->rm)
      return -1;

   if (!code || !code_size) {
      code = zero_placeholder;
      code_size = NV_SHADER_MIN_CODE_SIZE;
   }

   if (sh->code_bo) {
      nv_rm_bo_unmap(sh->code_bo);
      nv_rm_bo_free(sh->code_bo);
      sh->code_bo = NULL;
   }

   bo = nv_shader_alloc_upload_bo(sh->rm, code, code_size,
                                  NV_SHADER_MIN_CODE_SIZE, &gpu);
   if (!bo)
      return -1;

   sh->code_bo = bo;
   sh->code_gpu_addr = gpu;
   sh->code_size = code_size;
   if (register_count)
      sh->register_count = register_count;
   sh->uploaded = true;
   return 0;
}

int
nv_shader_upload_constants(struct nv_shader *sh, const void *data, uint32_t size)
{
   struct nv_rm_bo *bo;
   uint64_t gpu = 0;

   if (!sh || !sh->rm || !size)
      return -1;

   if (sh->const_bo) {
      nv_rm_bo_unmap(sh->const_bo);
      nv_rm_bo_free(sh->const_bo);
      sh->const_bo = NULL;
   }

   bo = nv_shader_alloc_upload_bo(sh->rm, data, size, 256, &gpu);
   if (!bo)
      return -1;

   sh->const_bo = bo;
   sh->const_gpu_addr = gpu;
   sh->const_size = size;
   return 0;
}

void
nv_shader_emit_bind(struct nv_push *p, const struct nv_shader *sh,
                    uint64_t program_region_base, int const_shader_slot)
{
   if (!p || !sh || !sh->uploaded || !sh->code_gpu_addr)
      return;

   if (program_region_base)
      nv_3d_set_program_region(p, program_region_base);

   nv_3d_load_pipeline_shader(p, sh->pipeline_stage, sh->pipeline_type,
                              sh->code_gpu_addr, sh->register_count,
                              sh->bind_group);

   if (sh->const_bo && sh->const_gpu_addr && sh->const_size &&
       const_shader_slot >= 0) {
      uint32_t padded = (sh->const_size + 255u) & ~255u;
      nv_3d_set_constant_buffer_selector(p, padded, sh->const_gpu_addr);
      nv_3d_bind_group_constant_buffer(p, sh->bind_group,
                                       (unsigned)const_shader_slot, true);
   }
}
