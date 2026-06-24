/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Shader modules, graphics/compute pipelines, descriptor sets (minimal),
 * render pass / dynamic rendering state, and draw/bind command recording.
 * Derived from NVC597 method sequences used by gallium nvgpu + nvidia-3d.
 */

#include "nvrm_private.h"
#include "nv_shader.h"
#include "nv_nir.h"
#include "nv_fence.h"
#include "nv_tex.h"
#include "nv_qmd.h"
#include "nv_device_info.h"
#include "nv_rm.h"

#include "vk_graphics_state.h"
#include "vk_pipeline.h"
#include "vk_shader_module.h"
#include "vk_descriptor_set_layout.h"
#include "vk_nir.h"
#include "nir.h"
#include "spirv/nir_spirv.h"
#include "util/ralloc.h"

/* ---------- shader module ---------- */

struct nvrm_shader_module {
   struct vk_shader_module vk;
   /* SPIR-V / NIR owned by vk_shader_module base when converted */
};

VK_DEFINE_NONDISP_HANDLE_CASTS(nvrm_shader_module, vk.base, VkShaderModule,
                               VK_OBJECT_TYPE_SHADER_MODULE)

/* ---------- pipeline layout / descriptors (minimal) ---------- */

struct nvrm_pipeline_layout {
   struct vk_object_base base;
   uint32_t set_count;
};

struct nvrm_descriptor_set_layout {
   struct vk_descriptor_set_layout vk;
};

struct nvrm_descriptor_pool {
   struct vk_object_base base;
   struct nvrm_device *device;
   uint32_t max_sets;
   uint32_t allocated;
};

struct nvrm_descriptor_set {
   struct vk_object_base base;
   struct nvrm_descriptor_set_layout *layout;
   struct nvrm_device *device;
   struct {
      struct nv_rm_bo *bo;
      uint64_t offset;
      uint64_t range;
   } ubo[16];
   uint32_t ubo_count;
   /* Combined image/sampler bindings: pool slot for TEX header index */
   struct {
      int tex_slot;          /* index in device tex pool; -1 if unset */
      struct nvrm_image_view *view;
      VkFormat format;
      uint32_t width, height, pitch;
      uint64_t gpu_addr;
      bool has_sampler;
      uint8_t addr_u, addr_v, mag_filt, min_filt;
   } img[16];
   uint32_t img_count;
   struct {
      struct nv_rm_bo *bo;
      uint64_t offset;
      uint64_t range;
   } ssbo[16];
   uint32_t ssbo_count;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(nvrm_pipeline_layout, base, VkPipelineLayout,
                               VK_OBJECT_TYPE_PIPELINE_LAYOUT)
VK_DEFINE_NONDISP_HANDLE_CASTS(nvrm_descriptor_set_layout, vk.base,
                               VkDescriptorSetLayout,
                               VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT)
VK_DEFINE_NONDISP_HANDLE_CASTS(nvrm_descriptor_pool, base, VkDescriptorPool,
                               VK_OBJECT_TYPE_DESCRIPTOR_POOL)
VK_DEFINE_NONDISP_HANDLE_CASTS(nvrm_descriptor_set, base, VkDescriptorSet,
                               VK_OBJECT_TYPE_DESCRIPTOR_SET)

/* ---------- graphics / compute pipeline ---------- */

struct nvrm_graphics_pipeline {
   struct vk_object_base base;
   struct nvrm_device *device;
   struct nv_shader *vs;
   struct nv_shader *fs;
   uint64_t program_region_base;
   /* Raster / depth / blend cached from create info (simplified) */
   bool depth_test_enable;
   bool depth_write_enable;
   uint32_t depth_compare_op;
   bool blend_enable;
   uint32_t cull_mode;
   bool front_ccw;
   VkPrimitiveTopology topology;
   uint32_t topology_nv; /* NVC597 topology */
};

struct nvrm_compute_pipeline {
   struct vk_object_base base;
   struct nvrm_device *device;
   struct nv_shader *cs;
   uint32_t local_size_x;
   uint32_t local_size_y;
   uint32_t local_size_z;
   uint32_t shared_mem_bytes;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(nvrm_graphics_pipeline, base, VkPipeline,
                               VK_OBJECT_TYPE_PIPELINE)
VK_DEFINE_NONDISP_HANDLE_CASTS(nvrm_compute_pipeline, base, VkPipeline,
                               VK_OBJECT_TYPE_PIPELINE)

/* ---------- render pass / framebuffer (minimal, for compatibility) ---------- */

struct nvrm_render_pass {
   struct vk_object_base base;
   uint32_t attachment_count;
   uint32_t subpass_count;
};

struct nvrm_framebuffer {
   struct vk_object_base base;
   uint32_t width, height, layers;
   uint32_t attachment_count;
};

struct nvrm_image_view {
   struct vk_object_base base;
   struct nvrm_image *image;
   VkFormat format;
   uint32_t level, layer, layer_count;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(nvrm_render_pass, base, VkRenderPass,
                               VK_OBJECT_TYPE_RENDER_PASS)
VK_DEFINE_NONDISP_HANDLE_CASTS(nvrm_framebuffer, base, VkFramebuffer,
                               VK_OBJECT_TYPE_FRAMEBUFFER)
VK_DEFINE_NONDISP_HANDLE_CASTS(nvrm_image_view, base, VkImageView,
                               VK_OBJECT_TYPE_IMAGE_VIEW)

/* Extend cmd buffer with bound pipeline / render state */
/* Stored in cmd via push only; track in cmd struct extensions in private.h
 * — we add fields via careful access: use vk_command_buffer for now and
 * local static not possible.  Add to nvrm_cmd_buffer in private.h separately.
 */

static uint32_t
vk_topology_to_nv(VkPrimitiveTopology topo)
{
   switch (topo) {
   case VK_PRIMITIVE_TOPOLOGY_POINT_LIST: return NVC597_TOPOLOGY_POINTS;
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST: return NVC597_TOPOLOGY_LINES;
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP: return NVC597_TOPOLOGY_LINE_STRIP;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST: return NVC597_TOPOLOGY_TRIANGLES;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: return NVC597_TOPOLOGY_TRIANGLE_STRIP;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN: return NVC597_TOPOLOGY_TRIANGLE_FAN;
   default: return NVC597_TOPOLOGY_TRIANGLES;
   }
}

static enum nv_shader_kind
vk_stage_to_kind(VkShaderStageFlagBits stage)
{
   switch (stage) {
   case VK_SHADER_STAGE_VERTEX_BIT: return NV_SHADER_KIND_VERTEX;
   case VK_SHADER_STAGE_FRAGMENT_BIT: return NV_SHADER_KIND_FRAGMENT;
   case VK_SHADER_STAGE_GEOMETRY_BIT: return NV_SHADER_KIND_GEOMETRY;
   case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT: return NV_SHADER_KIND_TESS_CTRL;
   case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: return NV_SHADER_KIND_TESS_EVAL;
   case VK_SHADER_STAGE_COMPUTE_BIT: return NV_SHADER_KIND_COMPUTE;
   default: return NV_SHADER_KIND_VERTEX;
   }
}

static mesa_shader_stage
vk_stage_to_mesa(VkShaderStageFlagBits stage)
{
   switch (stage) {
   case VK_SHADER_STAGE_VERTEX_BIT: return MESA_SHADER_VERTEX;
   case VK_SHADER_STAGE_FRAGMENT_BIT: return MESA_SHADER_FRAGMENT;
   case VK_SHADER_STAGE_GEOMETRY_BIT: return MESA_SHADER_GEOMETRY;
   case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT: return MESA_SHADER_TESS_CTRL;
   case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: return MESA_SHADER_TESS_EVAL;
   case VK_SHADER_STAGE_COMPUTE_BIT: return MESA_SHADER_COMPUTE;
   default: return MESA_SHADER_VERTEX;
   }
}

/* NIR options filled conservatively; backend lowers most ops in isel/stubs. */
static const nir_shader_compiler_options nvrm_nir_options = { 0 };

static struct spirv_to_nir_options
nvrm_spirv_options(void)
{
   return (struct spirv_to_nir_options){
      .ubo_addr_format = nir_address_format_32bit_index_offset,
      .ssbo_addr_format = nir_address_format_64bit_global,
      .phys_ssbo_addr_format = nir_address_format_64bit_global,
      .push_const_addr_format = nir_address_format_32bit_offset,
      .shared_addr_format = nir_address_format_32bit_offset,
      .constant_addr_format = nir_address_format_64bit_global,
   };
}

static nir_shader *
nvrm_shader_stage_to_nir(struct nvrm_device *dev,
                         const VkPipelineShaderStageCreateInfo *stage)
{
   VK_FROM_HANDLE(vk_shader_module, module, stage->module);
   mesa_shader_stage mesa_stage = vk_stage_to_mesa(stage->stage);
   const char *entrypoint = stage->pName ? stage->pName : "main";
   struct spirv_to_nir_options spirv_opts;
   nir_shader *nir = NULL;
   void *mem_ctx;

   if (!module)
      return NULL;

   /* Already-converted NIR (internal / test path) */
   if (module->nir)
      return nir_shader_clone(NULL, module->nir);

   if (!module->size)
      return NULL;

   spirv_opts = nvrm_spirv_options();
   mem_ctx = ralloc_context(NULL);
   if (!mem_ctx)
      return NULL;

   nir = vk_spirv_to_nir(&dev->vk,
                         (const uint32_t *)module->data,
                         module->size,
                         mesa_stage,
                         entrypoint,
                         stage->pSpecializationInfo,
                         &spirv_opts,
                         &nvrm_nir_options,
                         false,
                         mem_ctx);
   if (!nir) {
      ralloc_free(mem_ctx);
      return NULL;
   }

   /* Steal NIR off the temp context so it outlives this function; caller
    * takes ownership via nv_shader_set_nir(..., take_ownership=true) and
    * must ralloc_free when done.  We ralloc_steal onto a new ctx. */
   {
      void *keep = ralloc_context(NULL);
      if (!keep) {
         ralloc_free(mem_ctx);
         return NULL;
      }
      ralloc_steal(keep, nir);
      ralloc_free(mem_ctx);
      /* Stash keep pointer in nir->options user data? Store via gc_ctx field
       * is not portable; attach as parent of nir via ralloc — freeing nir's
       * parent frees everything.  Caller calls ralloc_free(ralloc_parent(nir)).
       * nv_shader_destroy will need to free it; for now set_nir takes ownership
       * and we document that owns_nir means ralloc_free(nir). */
      (void)keep;
   }

   return nir;
}

static struct nv_shader *
nvrm_compile_stage(struct nvrm_device *dev,
                   const VkPipelineShaderStageCreateInfo *stage)
{
   struct nv_shader *sh;
   nir_shader *nir = NULL;

   sh = nv_shader_create(dev->rm, vk_stage_to_kind(stage->stage));
   if (!sh)
      return NULL;

   nir = nvrm_shader_stage_to_nir(dev, stage);
   if (nir) {
      nv_shader_set_nir(sh, nir, true);
      if (nv_shader_compile_nir(sh, nir) != 0)
         nv_shader_compile_nir_stub(sh);
   } else {
      nv_shader_compile_nir_stub(sh);
   }
   return sh;
}

/* ---- CreateGraphicsPipelines ---- */

VKAPI_ATTR VkResult VKAPI_CALL
nvrm_CreateGraphicsPipelines(VkDevice _device, VkPipelineCache pipelineCache,
                             uint32_t createInfoCount,
                             const VkGraphicsPipelineCreateInfo *pCreateInfos,
                             const VkAllocationCallbacks *pAllocator,
                             VkPipeline *pPipelines)
{
   VK_FROM_HANDLE(nvrm_device, device, _device);
   uint32_t i, s;
   (void)pipelineCache;
   (void)pAllocator;

   for (i = 0; i < createInfoCount; i++) {
      const VkGraphicsPipelineCreateInfo *ci = &pCreateInfos[i];
      struct nvrm_graphics_pipeline *pipe;

      pipe = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*pipe), 8,
                        VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (!pipe) {
         pPipelines[i] = VK_NULL_HANDLE;
         continue;
      }

      vk_object_base_init(&device->vk, &pipe->base, VK_OBJECT_TYPE_PIPELINE);
      pipe->device = device;
      pipe->topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      pipe->topology_nv = NVC597_TOPOLOGY_TRIANGLES;
      pipe->depth_write_enable = true;
      pipe->front_ccw = false;

      if (ci->pInputAssemblyState)
         pipe->topology = ci->pInputAssemblyState->topology;
      pipe->topology_nv = vk_topology_to_nv(pipe->topology);

      if (ci->pRasterizationState) {
         pipe->cull_mode = ci->pRasterizationState->cullMode;
         pipe->front_ccw = ci->pRasterizationState->frontFace ==
                           VK_FRONT_FACE_COUNTER_CLOCKWISE;
      }
      if (ci->pDepthStencilState) {
         pipe->depth_test_enable = ci->pDepthStencilState->depthTestEnable;
         pipe->depth_write_enable = ci->pDepthStencilState->depthWriteEnable;
         pipe->depth_compare_op = ci->pDepthStencilState->depthCompareOp;
      }
      if (ci->pColorBlendState && ci->pColorBlendState->attachmentCount)
         pipe->blend_enable = ci->pColorBlendState->pAttachments[0].blendEnable;

      for (s = 0; s < ci->stageCount; s++) {
         const VkPipelineShaderStageCreateInfo *st = &ci->pStages[s];
         struct nv_shader *sh = nvrm_compile_stage(device, st);
         if (!sh)
            continue;
         if (st->stage == VK_SHADER_STAGE_VERTEX_BIT)
            pipe->vs = sh;
         else if (st->stage == VK_SHADER_STAGE_FRAGMENT_BIT)
            pipe->fs = sh;
         else
            nv_shader_destroy(sh);
         if (sh && sh->code_gpu_addr && !pipe->program_region_base)
            pipe->program_region_base = sh->code_gpu_addr;
      }

      /* Ensure at least stub shaders exist for bind */
      if (!pipe->vs) {
         pipe->vs = nv_shader_create(device->rm, NV_SHADER_KIND_VERTEX);
         if (pipe->vs)
            nv_shader_compile_nir_stub(pipe->vs);
      }
      if (!pipe->fs) {
         pipe->fs = nv_shader_create(device->rm, NV_SHADER_KIND_FRAGMENT);
         if (pipe->fs)
            nv_shader_compile_nir_stub(pipe->fs);
      }
      if (pipe->vs && pipe->vs->code_gpu_addr)
         pipe->program_region_base = pipe->vs->code_gpu_addr;

      pPipelines[i] = nvrm_graphics_pipeline_to_handle(pipe);
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvrm_CreateComputePipelines(VkDevice _device, VkPipelineCache pipelineCache,
                            uint32_t createInfoCount,
                            const VkComputePipelineCreateInfo *pCreateInfos,
                            const VkAllocationCallbacks *pAllocator,
                            VkPipeline *pPipelines)
{
   VK_FROM_HANDLE(nvrm_device, device, _device);
   uint32_t i;
   (void)pipelineCache;

   for (i = 0; i < createInfoCount; i++) {
      struct nvrm_compute_pipeline *pipe;
      pipe = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*pipe), 8,
                        VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (!pipe) {
         pPipelines[i] = VK_NULL_HANDLE;
         continue;
      }
      vk_object_base_init(&device->vk, &pipe->base, VK_OBJECT_TYPE_PIPELINE);
      pipe->device = device;
      pipe->local_size_x = 1;
      pipe->local_size_y = 1;
      pipe->local_size_z = 1;
      pipe->shared_mem_bytes = 0;
      pipe->cs = nvrm_compile_stage(device, &pCreateInfos[i].stage);
      if (!pipe->cs) {
         pipe->cs = nv_shader_create(device->rm, NV_SHADER_KIND_COMPUTE);
         if (pipe->cs)
            nv_shader_compile_nir_stub(pipe->cs);
      }
      /* Pull local size / shared mem from compiled NIR when available */
      if (pipe->cs && pipe->cs->nir) {
         const struct nir_shader *ns = (const struct nir_shader *)pipe->cs->nir;
         if (ns->info.workgroup_size[0])
            pipe->local_size_x = ns->info.workgroup_size[0];
         if (ns->info.workgroup_size[1])
            pipe->local_size_y = ns->info.workgroup_size[1];
         if (ns->info.workgroup_size[2])
            pipe->local_size_z = ns->info.workgroup_size[2];
         pipe->shared_mem_bytes = (uint32_t)ns->info.shared_size;
      }
      pPipelines[i] = nvrm_compute_pipeline_to_handle(pipe);
   }
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
nvrm_DestroyPipeline(VkDevice _device, VkPipeline _pipeline,
                     const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(nvrm_device, device, _device);
   struct nvrm_graphics_pipeline *gp;

   if (_pipeline == VK_NULL_HANDLE)
      return;

   /* All pipelines are created as graphics or compute structs with vk_object_base
    * first; graphics has vs/fs, compute has cs.  We tag compute by putting a
    * non-null cs and null vs in a union-less approach: destroy tries graphics
    * layout (vs/fs).  Compute pipelines use the same allocation pattern with
    * only cs set — detect via magic: if first shader pointer area looks like
    * only cs, handled below.
    */
   gp = nvrm_graphics_pipeline_from_handle(_pipeline);
   if (!gp)
      return;

   /* Heuristic: compute pipeline has cs at offset of vs in a parallel struct;
    * both structs share device pointer at same offset after base.  Always
    * destroy vs/fs if non-null; also try cs via compute cast if vs is null. */
   if (gp->vs || gp->fs || gp->device == device) {
      struct nvrm_compute_pipeline *cp = (struct nvrm_compute_pipeline *)(void *)gp;
      if (!gp->vs && !gp->fs && cp->cs) {
         nv_shader_destroy(cp->cs);
         vk_object_base_finish(&cp->base);
         vk_free2(&device->vk.alloc, pAllocator, cp);
         return;
      }
      nv_shader_destroy(gp->vs);
      nv_shader_destroy(gp->fs);
      vk_object_base_finish(&gp->base);
      vk_free2(&device->vk.alloc, pAllocator, gp);
   }
}

/* ---- Pipeline layout / descriptors ---- */

VKAPI_ATTR VkResult VKAPI_CALL
nvrm_CreatePipelineLayout(VkDevice _device,
                          const VkPipelineLayoutCreateInfo *pCreateInfo,
                          const VkAllocationCallbacks *pAllocator,
                          VkPipelineLayout *pPipelineLayout)
{
   VK_FROM_HANDLE(nvrm_device, device, _device);
   struct nvrm_pipeline_layout *layout;

   layout = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*layout), 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!layout)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   vk_object_base_init(&device->vk, &layout->base, VK_OBJECT_TYPE_PIPELINE_LAYOUT);
   layout->set_count = pCreateInfo->setLayoutCount;
   *pPipelineLayout = nvrm_pipeline_layout_to_handle(layout);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
nvrm_DestroyPipelineLayout(VkDevice _device, VkPipelineLayout _layout,
                           const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(nvrm_device, device, _device);
   VK_FROM_HANDLE(nvrm_pipeline_layout, layout, _layout);
   if (!layout)
      return;
   vk_object_base_finish(&layout->base);
   vk_free2(&device->vk.alloc, pAllocator, layout);
}

VKAPI_ATTR VkResult VKAPI_CALL
nvrm_CreateDescriptorSetLayout(VkDevice _device,
                               const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
                               const VkAllocationCallbacks *pAllocator,
                               VkDescriptorSetLayout *pSetLayout)
{
   VK_FROM_HANDLE(nvrm_device, device, _device);
   struct nvrm_descriptor_set_layout *layout;

   layout = vk_descriptor_set_layout_zalloc(&device->vk, sizeof(*layout),
                                            pCreateInfo);
   if (!layout)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   *pSetLayout = nvrm_descriptor_set_layout_to_handle(layout);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
nvrm_DestroyDescriptorSetLayout(VkDevice _device, VkDescriptorSetLayout _layout,
                                const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(nvrm_device, device, _device);
   VK_FROM_HANDLE(nvrm_descriptor_set_layout, layout, _layout);
   if (!layout)
      return;
   vk_descriptor_set_layout_destroy(&device->vk, &layout->vk);
   (void)pAllocator;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvrm_CreateDescriptorPool(VkDevice _device,
                          const VkDescriptorPoolCreateInfo *pCreateInfo,
                          const VkAllocationCallbacks *pAllocator,
                          VkDescriptorPool *pDescriptorPool)
{
   VK_FROM_HANDLE(nvrm_device, device, _device);
   struct nvrm_descriptor_pool *pool;

   pool = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*pool), 8,
                     VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!pool)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   vk_object_base_init(&device->vk, &pool->base, VK_OBJECT_TYPE_DESCRIPTOR_POOL);
   pool->device = device;
   pool->max_sets = pCreateInfo->maxSets;
   *pDescriptorPool = nvrm_descriptor_pool_to_handle(pool);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
nvrm_DestroyDescriptorPool(VkDevice _device, VkDescriptorPool _pool,
                           const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(nvrm_device, device, _device);
   VK_FROM_HANDLE(nvrm_descriptor_pool, pool, _pool);
   if (!pool)
      return;
   vk_object_base_finish(&pool->base);
   vk_free2(&device->vk.alloc, pAllocator, pool);
}

VKAPI_ATTR VkResult VKAPI_CALL
nvrm_AllocateDescriptorSets(VkDevice _device,
                            const VkDescriptorSetAllocateInfo *pAllocateInfo,
                            VkDescriptorSet *pDescriptorSets)
{
   VK_FROM_HANDLE(nvrm_device, device, _device);
   VK_FROM_HANDLE(nvrm_descriptor_pool, pool, pAllocateInfo->descriptorPool);
   uint32_t i;

   for (i = 0; i < pAllocateInfo->descriptorSetCount; i++) {
      struct nvrm_descriptor_set *set;
      VK_FROM_HANDLE(nvrm_descriptor_set_layout, layout,
                     pAllocateInfo->pSetLayouts[i]);

      set = vk_zalloc2(&device->vk.alloc, NULL, sizeof(*set), 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (!set)
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      vk_object_base_init(&device->vk, &set->base, VK_OBJECT_TYPE_DESCRIPTOR_SET);
      set->layout = layout;
      set->device = device;
      if (pool)
         pool->allocated++;
      pDescriptorSets[i] = nvrm_descriptor_set_to_handle(set);
   }
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvrm_FreeDescriptorSets(VkDevice _device, VkDescriptorPool descriptorPool,
                        uint32_t descriptorSetCount,
                        const VkDescriptorSet *pDescriptorSets)
{
   VK_FROM_HANDLE(nvrm_device, device, _device);
   uint32_t i;
   (void)descriptorPool;
   for (i = 0; i < descriptorSetCount; i++) {
      VK_FROM_HANDLE(nvrm_descriptor_set, set, pDescriptorSets[i]);
      if (!set)
         continue;
      vk_object_base_finish(&set->base);
      vk_free(&device->vk.alloc, set);
   }
   return VK_SUCCESS;
}


/* ---------- texture descriptor helpers (clc597tex pitch headers) ---------- */

static void
nvrm_vk_format_to_tex(VkFormat fmt, uint8_t *comp, uint8_t *dt)
{
   uint8_t c = NV_TEX_COMP_A8B8G8R8;
   uint8_t d = NV_TEX_DT_UNORM;
   switch (fmt) {
   case VK_FORMAT_R8G8B8A8_UNORM:
   case VK_FORMAT_B8G8R8A8_UNORM:
   case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
      c = NV_TEX_COMP_A8B8G8R8; d = NV_TEX_DT_UNORM; break;
   case VK_FORMAT_R8G8B8A8_SRGB:
   case VK_FORMAT_B8G8R8A8_SRGB:
      c = NV_TEX_COMP_A8B8G8R8; d = NV_TEX_DT_UNORM; break;
   case VK_FORMAT_R32_SFLOAT:
      c = NV_TEX_COMP_R32; d = NV_TEX_DT_FLOAT; break;
   case VK_FORMAT_R16G16B16A16_SFLOAT:
      c = NV_TEX_COMP_R16G16B16A16; d = NV_TEX_DT_FLOAT; break;
   case VK_FORMAT_R32G32B32A32_SFLOAT:
      c = NV_TEX_COMP_R32G32B32A32; d = NV_TEX_DT_FLOAT; break;
   case VK_FORMAT_R8_UNORM:
      c = NV_TEX_COMP_R8; d = NV_TEX_DT_UNORM; break;
   case VK_FORMAT_R8G8_UNORM:
      c = NV_TEX_COMP_G8R8; d = NV_TEX_DT_UNORM; break;
   case VK_FORMAT_D24_UNORM_S8_UINT:
      c = NV_TEX_COMP_Z24S8; d = NV_TEX_DT_UINT; break;
   case VK_FORMAT_D32_SFLOAT:
      c = NV_TEX_COMP_ZF32; d = NV_TEX_DT_FLOAT; break;
   case VK_FORMAT_D16_UNORM:
      c = NV_TEX_COMP_Z16; d = NV_TEX_DT_UNORM; break;
   default:
      break;
   }
   if (comp) *comp = c;
   if (dt) *dt = d;
}

static uint8_t
nvrm_vk_addr_mode(VkSamplerAddressMode m)
{
   switch (m) {
   case VK_SAMPLER_ADDRESS_MODE_REPEAT: return NV_TEX_SAMP_ADDR_WRAP;
   case VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT: return NV_TEX_SAMP_ADDR_MIRROR;
   case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE: return NV_TEX_SAMP_ADDR_CLAMP_EDGE;
   case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER: return NV_TEX_SAMP_ADDR_BORDER;
   case VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE: return NV_TEX_SAMP_ADDR_MIRROR;
   default: return NV_TEX_SAMP_ADDR_CLAMP_EDGE;
   }
}

static uint8_t
nvrm_vk_filt(VkFilter f)
{
   return (f == VK_FILTER_NEAREST) ? NV_TEX_SAMP_FILT_NEAREST
                                   : NV_TEX_SAMP_FILT_LINEAR;
}

VK_DEFINE_NONDISP_HANDLE_CASTS(nvrm_sampler, base, VkSampler, VK_OBJECT_TYPE_SAMPLER)

VKAPI_ATTR VkResult VKAPI_CALL
nvrm_CreateSampler(VkDevice _device,
                   const VkSamplerCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator,
                   VkSampler *pSampler)
{
   VK_FROM_HANDLE(nvrm_device, device, _device);
   struct nvrm_sampler *s;

   s = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*s), 8,
                  VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!s)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   vk_object_base_init(&device->vk, &s->base, VK_OBJECT_TYPE_SAMPLER);
   s->addr_u = nvrm_vk_addr_mode(pCreateInfo->addressModeU);
   s->addr_v = nvrm_vk_addr_mode(pCreateInfo->addressModeV);
   s->addr_p = nvrm_vk_addr_mode(pCreateInfo->addressModeW);
   s->mag_filt = nvrm_vk_filt(pCreateInfo->magFilter);
   s->min_filt = nvrm_vk_filt(pCreateInfo->minFilter);
   s->mip_filt = (pCreateInfo->mipmapMode == VK_SAMPLER_MIPMAP_MODE_NEAREST)
                    ? NV_TEX_SAMP_FILT_NEAREST : NV_TEX_SAMP_FILT_LINEAR;
   s->min_lod = pCreateInfo->minLod;
   s->max_lod = pCreateInfo->maxLod;
   s->lod_bias = pCreateInfo->mipLodBias;
   s->unnormalized_coords = pCreateInfo->unnormalizedCoordinates;
   *pSampler = nvrm_sampler_to_handle(s);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
nvrm_DestroySampler(VkDevice _device, VkSampler _sampler,
                    const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(nvrm_device, device, _device);
   VK_FROM_HANDLE(nvrm_sampler, s, _sampler);
   if (!s)
      return;
   vk_object_base_finish(&s->base);
   vk_free2(&device->vk.alloc, pAllocator, s);
}

/* Lazy per-device texture header pool stored via first descriptor set's device */
static struct nv_tex_pool *
nvrm_device_ensure_tex_pool(struct nvrm_device *dev)
{
   if (!dev)
      return NULL;
   if (!dev->tex_pool)
      dev->tex_pool = nv_tex_pool_create(dev->rm, 256);
   return dev->tex_pool;
}

static int
nvrm_write_combined_image_sampler(struct nvrm_device *dev,
                                  struct nvrm_image_view *view,
                                  const VkSamplerCreateInfo *sci_fallback,
                                  VkSampler vk_samp,
                                  struct nv_tex_entry *out_entry)
{
   struct nv_tex_desc desc;
   struct nvrm_image *img;
   struct nvrm_sampler *samp = NULL;
   uint32_t w, h, pitch, level;
   uint8_t comp, dt;
   (void)sci_fallback;

   if (!dev || !view || !view->image || !out_entry)
      return -1;
   img = view->image;
   if (vk_samp != VK_NULL_HANDLE)
      samp = nvrm_sampler_from_handle(vk_samp);

   memset(&desc, 0, sizeof(desc));
   level = view->level;
   w = img->vk.extent.width ? img->vk.extent.width : 1;
   h = img->vk.extent.height ? img->vk.extent.height : 1;
   if (level > 0) {
      w = w > (1u << level) ? (w >> level) : 1;
      h = h > (1u << level) ? (h >> level) : 1;
   }
   /* Prefer stored row_pitch from CreateImage; scale for mips coarsely */
   if (img->row_pitch)
      pitch = level ? ((img->row_pitch >> level) + 31u) & ~31u : img->row_pitch;
   else
      pitch = (w * 4 + 31u) & ~31u;
   if (pitch < 32)
      pitch = 32;

   nvrm_vk_format_to_tex(view->format ? view->format : img->vk.format, &comp, &dt);
   desc.gpu_addr = img->gpu_offset ? img->gpu_offset :
                   (img->bo ? nv_rm_bo_gpu_offset(img->bo) : 0);
   /* Mip offset estimate: sum prior levels */
   if (level > 0 && img->row_pitch && img->vk.extent.height) {
      uint32_t lv, off = 0, lw = img->vk.extent.width, lh = img->vk.extent.height;
      uint32_t lp = img->row_pitch;
      for (lv = 0; lv < level; lv++) {
         off += lp * (lh ? lh : 1);
         lw = lw > 1 ? lw / 2 : 1;
         lh = lh > 1 ? lh / 2 : 1;
         lp = (lp > 32 ? lp / 2 : 32);
         lp = (lp + 31u) & ~31u;
      }
      desc.gpu_addr += off;
   }
   desc.width = w;
   desc.height = h;
   desc.pitch = pitch;
   desc.components = comp;
   desc.data_type = dt;
   desc.src_x = NV_TEX_SRC_R;
   desc.src_y = NV_TEX_SRC_G;
   desc.src_z = NV_TEX_SRC_B;
   desc.src_w = NV_TEX_SRC_A;
   desc.texture_type = NV_TEX_TEXTURE_TYPE_2D;
   if (img->vk.image_type == VK_IMAGE_TYPE_1D)
      desc.texture_type = NV_TEX_TEXTURE_TYPE_1D;
   else if (img->vk.image_type == VK_IMAGE_TYPE_3D)
      desc.texture_type = NV_TEX_TEXTURE_TYPE_3D;
   desc.normalized_coords = !(samp && samp->unnormalized_coords);
   if (view->format == VK_FORMAT_R8G8B8A8_SRGB ||
       view->format == VK_FORMAT_B8G8R8A8_SRGB)
      desc.s_r_g_b_conversion = true;
   /* Blocklinear images use TEXHEAD_BL (512B-aligned address, gobs layout) */
   if (img->is_blocklinear && !img->is_linear) {
      desc.blocklinear = true;
      desc.gobs_width = img->gobs_width;
      desc.gobs_height = img->gobs_height ? img->gobs_height : NV_TEX_GOBS_SIXTEEN;
      desc.gobs_depth = img->gobs_depth;
      /* Align sample address to 512B for BL header ADDRESS_BITS31TO9 */
      desc.gpu_addr &= ~0x1ffull;
   }
   if (samp) {
      desc.addr_u = samp->addr_u;
      desc.addr_v = samp->addr_v;
      desc.addr_p = samp->addr_p;
      desc.mag_filt = samp->mag_filt;
      desc.min_filt = samp->min_filt;
      desc.mip_filt = samp->mip_filt;
      desc.min_lod = samp->min_lod;
      desc.max_lod = samp->max_lod;
      desc.lod_bias = samp->lod_bias;
   } else {
      desc.addr_u = NV_TEX_SAMP_ADDR_CLAMP_EDGE;
      desc.addr_v = NV_TEX_SAMP_ADDR_CLAMP_EDGE;
      desc.addr_p = NV_TEX_SAMP_ADDR_CLAMP_EDGE;
      desc.mag_filt = NV_TEX_SAMP_FILT_LINEAR;
      desc.min_filt = NV_TEX_SAMP_FILT_LINEAR;
      desc.mip_filt = NV_TEX_SAMP_FILT_NEAREST;
   }
   nv_tex_encode_2d(&desc, out_entry);
   return 0;
}

VKAPI_ATTR void VKAPI_CALL
nvrm_UpdateDescriptorSets(VkDevice _device,
                          uint32_t descriptorWriteCount,
                          const VkWriteDescriptorSet *pDescriptorWrites,
                          uint32_t descriptorCopyCount,
                          const VkCopyDescriptorSet *pDescriptorCopies)
{
   VK_FROM_HANDLE(nvrm_device, device, _device);
   uint32_t i;
   (void)descriptorCopyCount;
   (void)pDescriptorCopies;

   for (i = 0; i < descriptorWriteCount; i++) {
      const VkWriteDescriptorSet *w = &pDescriptorWrites[i];
      VK_FROM_HANDLE(nvrm_descriptor_set, set, w->dstSet);
      if (!set)
         continue;
      if (w->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
          w->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) {
         uint32_t j;
         for (j = 0; j < w->descriptorCount && set->ubo_count < 16; j++) {
            VK_FROM_HANDLE(nvrm_buffer, buf, w->pBufferInfo[j].buffer);
            set->ubo[set->ubo_count].bo = buf ? buf->bo : NULL;
            set->ubo[set->ubo_count].offset = w->pBufferInfo[j].offset;
            set->ubo[set->ubo_count].range = w->pBufferInfo[j].range;
            set->ubo_count++;
         }
      } else if (w->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
                 w->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) {
         uint32_t j;
         for (j = 0; j < w->descriptorCount && set->ssbo_count < 16; j++) {
            VK_FROM_HANDLE(nvrm_buffer, buf, w->pBufferInfo[j].buffer);
            set->ssbo[set->ssbo_count].bo = buf ? buf->bo : NULL;
            set->ssbo[set->ssbo_count].offset = w->pBufferInfo[j].offset;
            set->ssbo[set->ssbo_count].range = w->pBufferInfo[j].range;
            set->ssbo_count++;
         }
      } else if (w->descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
                 w->descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
                 w->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
         uint32_t j;
         struct nv_tex_pool *pool = nvrm_device_ensure_tex_pool(device);
         for (j = 0; j < w->descriptorCount && set->img_count < 16; j++) {
            VK_FROM_HANDLE(nvrm_image_view, view, w->pImageInfo[j].imageView);
            struct nv_tex_entry entry;
            int slot = -1;
            if (view && pool &&
                nvrm_write_combined_image_sampler(device, view, NULL,
                                                  w->pImageInfo[j].sampler,
                                                  &entry) == 0) {
               slot = nv_tex_pool_set_entry(pool, -1, &entry);
            }
            set->img[set->img_count].tex_slot = slot;
            set->img[set->img_count].view = view;
            set->img[set->img_count].format = view ? view->format : VK_FORMAT_UNDEFINED;
            if (view && view->image) {
               set->img[set->img_count].width = view->image->vk.extent.width;
               set->img[set->img_count].height = view->image->vk.extent.height;
               set->img[set->img_count].gpu_addr =
                  view->image->gpu_offset ? view->image->gpu_offset :
                  (view->image->bo ? nv_rm_bo_gpu_offset(view->image->bo) : 0);
            }
            set->img[set->img_count].has_sampler =
               (w->descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            set->img_count++;
         }
      }
   }
}

/* ---- Render pass / image view / framebuffer ---- */

VKAPI_ATTR VkResult VKAPI_CALL
nvrm_CreateRenderPass2(VkDevice _device,
                       const VkRenderPassCreateInfo2 *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator,
                       VkRenderPass *pRenderPass)
{
   VK_FROM_HANDLE(nvrm_device, device, _device);
   struct nvrm_render_pass *rp;

   rp = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*rp), 8,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!rp)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   vk_object_base_init(&device->vk, &rp->base, VK_OBJECT_TYPE_RENDER_PASS);
   rp->attachment_count = pCreateInfo->attachmentCount;
   rp->subpass_count = pCreateInfo->subpassCount;
   *pRenderPass = nvrm_render_pass_to_handle(rp);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvrm_CreateRenderPass(VkDevice device,
                      const VkRenderPassCreateInfo *pCreateInfo,
                      const VkAllocationCallbacks *pAllocator,
                      VkRenderPass *pRenderPass)
{
   /* Convert-lite: only counts matter for stub */
   VkRenderPassCreateInfo2 ci2 = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2,
      .attachmentCount = pCreateInfo->attachmentCount,
      .subpassCount = pCreateInfo->subpassCount,
   };
   return nvrm_CreateRenderPass2(device, &ci2, pAllocator, pRenderPass);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_DestroyRenderPass(VkDevice _device, VkRenderPass _rp,
                       const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(nvrm_device, device, _device);
   VK_FROM_HANDLE(nvrm_render_pass, rp, _rp);
   if (!rp)
      return;
   vk_object_base_finish(&rp->base);
   vk_free2(&device->vk.alloc, pAllocator, rp);
}

VKAPI_ATTR VkResult VKAPI_CALL
nvrm_CreateImageView(VkDevice _device,
                     const VkImageViewCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator,
                     VkImageView *pView)
{
   VK_FROM_HANDLE(nvrm_device, device, _device);
   VK_FROM_HANDLE(nvrm_image, image, pCreateInfo->image);
   struct nvrm_image_view *view;

   view = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*view), 8,
                     VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!view)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   vk_object_base_init(&device->vk, &view->base, VK_OBJECT_TYPE_IMAGE_VIEW);
   view->image = image;
   view->format = pCreateInfo->format;
   view->level = pCreateInfo->subresourceRange.baseMipLevel;
   view->layer = pCreateInfo->subresourceRange.baseArrayLayer;
   view->layer_count = pCreateInfo->subresourceRange.layerCount;
   *pView = nvrm_image_view_to_handle(view);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
nvrm_DestroyImageView(VkDevice _device, VkImageView _view,
                      const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(nvrm_device, device, _device);
   VK_FROM_HANDLE(nvrm_image_view, view, _view);
   if (!view)
      return;
   vk_object_base_finish(&view->base);
   vk_free2(&device->vk.alloc, pAllocator, view);
}

VKAPI_ATTR VkResult VKAPI_CALL
nvrm_CreateFramebuffer(VkDevice _device,
                       const VkFramebufferCreateInfo *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator,
                       VkFramebuffer *pFramebuffer)
{
   VK_FROM_HANDLE(nvrm_device, device, _device);
   struct nvrm_framebuffer *fb;

   fb = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*fb), 8,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!fb)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   vk_object_base_init(&device->vk, &fb->base, VK_OBJECT_TYPE_FRAMEBUFFER);
   fb->width = pCreateInfo->width;
   fb->height = pCreateInfo->height;
   fb->layers = pCreateInfo->layers;
   fb->attachment_count = pCreateInfo->attachmentCount;
   *pFramebuffer = nvrm_framebuffer_to_handle(fb);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
nvrm_DestroyFramebuffer(VkDevice _device, VkFramebuffer _fb,
                        const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(nvrm_device, device, _device);
   VK_FROM_HANDLE(nvrm_framebuffer, fb, _fb);
   if (!fb)
      return;
   vk_object_base_finish(&fb->base);
   vk_free2(&device->vk.alloc, pAllocator, fb);
}

/* ---- Command buffer: bind pipeline, begin rendering, draw ---- */

static void
nvrm_cmd_emit_pipeline_state(struct nvrm_cmd_buffer *cmd,
                             struct nvrm_graphics_pipeline *pipe)
{
   const struct nv_device_info *info = cmd->device->info;
   uint32_t class_3d = info ? info->class_3d : 0;
   uint64_t region = 0;
   unsigned cull = 0;

   if (!cmd->push_map || !pipe)
      return;

   if (class_3d)
      nv_3d_set_object(&cmd->push, class_3d);
   else
      nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_3D);

   if (!cmd->channel_init_done) {
      nv_3d_emit_channel_init_defaults(&cmd->push, 5, 3, 5, 3);
      cmd->channel_init_done = true;
   }

   if (pipe->cull_mode & VK_CULL_MODE_FRONT_AND_BACK)
      cull = 3;
   else if (pipe->cull_mode & VK_CULL_MODE_FRONT_BIT)
      cull = 1;
   else if (pipe->cull_mode & VK_CULL_MODE_BACK_BIT)
      cull = 2;

   nv_3d_emit_blend_zsa_raster(&cmd->push, pipe->blend_enable,
                               0, 1, 0, 0, 1, 0, 0xf,
                               pipe->depth_test_enable, pipe->depth_write_enable,
                               (unsigned)pipe->depth_compare_op, false,
                               cull, pipe->front_ccw, 0, true);

   nv_3d_disable_pipeline_shader(&cmd->push, NV_3D_PIPE_STAGE_TESS_INIT);
   nv_3d_disable_pipeline_shader(&cmd->push, NV_3D_PIPE_STAGE_TESS);
   nv_3d_disable_pipeline_shader(&cmd->push, NV_3D_PIPE_STAGE_GEOMETRY);

   region = pipe->program_region_base;
   if (pipe->vs && pipe->vs->uploaded)
      nv_shader_emit_bind(&cmd->push, pipe->vs, region, -1);
   if (pipe->fs && pipe->fs->uploaded)
      nv_shader_emit_bind(&cmd->push, pipe->fs, 0, -1);

   cmd->bound_gfx_pipeline = pipe;
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdBindPipeline(VkCommandBuffer commandBuffer,
                     VkPipelineBindPoint pipelineBindPoint,
                     VkPipeline _pipeline)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   if (pipelineBindPoint == VK_PIPELINE_BIND_POINT_COMPUTE) {
      struct nvrm_compute_pipeline *cp =
         nvrm_compute_pipeline_from_handle(_pipeline);
      cmd->bound_compute_pipeline = cp;
      if (cp) {
         cmd->compute_local_x = cp->local_size_x ? cp->local_size_x : 1;
         cmd->compute_local_y = cp->local_size_y ? cp->local_size_y : 1;
         cmd->compute_local_z = cp->local_size_z ? cp->local_size_z : 1;
      }
      return;
   }
   if (pipelineBindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS)
      return;
   struct nvrm_graphics_pipeline *pipe =
      nvrm_graphics_pipeline_from_handle(_pipeline);
   if (pipe)
      nvrm_cmd_emit_pipeline_state(cmd, pipe);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdBeginRendering(VkCommandBuffer commandBuffer,
                       const VkRenderingInfo *pRenderingInfo)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   const struct nv_device_info *info = cmd->device->info;
   uint32_t class_3d = info ? info->class_3d : 0;
   uint32_t w, h;
   uint8_t targets[8] = {0};
   unsigned i, n_color = 0;

   if (!cmd->push_map || !pRenderingInfo)
      return;

   if (class_3d)
      nv_3d_set_object(&cmd->push, class_3d);
   else
      nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_3D);

   if (!cmd->channel_init_done) {
      nv_3d_emit_channel_init_defaults(&cmd->push, 5, 3, 5, 3);
      cmd->channel_init_done = true;
   }

   w = pRenderingInfo->renderArea.extent.width;
   h = pRenderingInfo->renderArea.extent.height;
   if (!w) w = 1;
   if (!h) h = 1;

   for (i = 0; i < pRenderingInfo->colorAttachmentCount && i < 8; i++) {
      const VkRenderingAttachmentInfo *att = &pRenderingInfo->pColorAttachments[i];
      struct nv_3d_surface s;
      memset(&s, 0, sizeof(s));
      if (att->imageView != VK_NULL_HANDLE) {
         VK_FROM_HANDLE(nvrm_image_view, view, att->imageView);
         if (view && view->image) {
            s.enabled = true;
            s.gpu_addr = view->image->gpu_offset;
            s.width = w;
            s.height = h;
            s.format = NVC597_SET_COLOR_TARGET_FORMAT_V_A8B8G8R8;
            s.block_linear = false;
            n_color++;
         }
      }
      nv_3d_set_color_target(&cmd->push, i, &s);
   }
   if (n_color)
      nv_3d_set_ct_select(&cmd->push, n_color, targets);

   if (pRenderingInfo->pDepthAttachment &&
       pRenderingInfo->pDepthAttachment->imageView != VK_NULL_HANDLE) {
      VK_FROM_HANDLE(nvrm_image_view, view,
                     pRenderingInfo->pDepthAttachment->imageView);
      struct nv_3d_surface s;
      memset(&s, 0, sizeof(s));
      if (view && view->image) {
         s.enabled = true;
         s.gpu_addr = view->image->gpu_offset;
         s.width = w;
         s.height = h;
         s.format = NVC597_SET_ZT_FORMAT_V_Z24S8;
         nv_3d_set_zeta_target(&cmd->push, &s);
      }
   }

   nv_3d_set_surface_clip(&cmd->push,
                          pRenderingInfo->renderArea.offset.x,
                          pRenderingInfo->renderArea.offset.y, w, h);
   nv_3d_set_viewport0(&cmd->push,
                       (float)w * 0.5f, (float)h * -0.5f, 0.5f,
                       (float)w * 0.5f, (float)h * 0.5f, 0.5f);

   /* Load ops: clear colour if requested */
   for (i = 0; i < pRenderingInfo->colorAttachmentCount && i < 1; i++) {
      const VkRenderingAttachmentInfo *att = &pRenderingInfo->pColorAttachments[i];
      if (att->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR) {
         const uint32_t *c = att->clearValue.color.uint32;
         nv_3d_emit_clear_surface(&cmd->push, 0x10, c, 0.0f, 0);
      }
   }
   if (pRenderingInfo->pDepthAttachment &&
       pRenderingInfo->pDepthAttachment->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR) {
      float d = pRenderingInfo->pDepthAttachment->clearValue.depthStencil.depth;
      nv_3d_emit_clear_surface(&cmd->push, 0x100, NULL, d, 0);
   }

   cmd->render_width = w;
   cmd->render_height = h;
   cmd->in_render_pass = true;
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdEndRendering(VkCommandBuffer commandBuffer)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   if (cmd->push_map)
      nv_push_wfi(&cmd->push);
   cmd->in_render_pass = false;
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdBeginRenderPass2(VkCommandBuffer commandBuffer,
                         const VkRenderPassBeginInfo *pRenderPassBegin,
                         const VkSubpassBeginInfo *pSubpassBeginInfo)
{
   VkRenderingInfo ri;
   VkRenderingAttachmentInfo color;
   (void)pSubpassBeginInfo;
   memset(&ri, 0, sizeof(ri));
   ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
   ri.renderArea = pRenderPassBegin->renderArea;
   ri.layerCount = 1;
   /* Without attachment formats, only set area; clears from pClearValues if any */
   if (pRenderPassBegin->clearValueCount && pRenderPassBegin->pClearValues) {
      memset(&color, 0, sizeof(color));
      color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
      color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      color.clearValue = pRenderPassBegin->pClearValues[0];
      ri.colorAttachmentCount = 1;
      ri.pColorAttachments = &color;
   }
   nvrm_CmdBeginRendering(commandBuffer, &ri);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdEndRenderPass2(VkCommandBuffer commandBuffer,
                       const VkSubpassEndInfo *pSubpassEndInfo)
{
   (void)pSubpassEndInfo;
   nvrm_CmdEndRendering(commandBuffer);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount,
             uint32_t instanceCount, uint32_t firstVertex,
             uint32_t firstInstance)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   (void)instanceCount;
   (void)firstInstance;
   if (!cmd->push_map)
      return;
   if (cmd->bound_gfx_pipeline)
      nvrm_cmd_emit_pipeline_state(cmd, cmd->bound_gfx_pipeline);
   nv_3d_emit_draw_vertex_array(&cmd->push, firstVertex, vertexCount);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount,
                    uint32_t instanceCount, uint32_t firstIndex,
                    int32_t vertexOffset, uint32_t firstInstance)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   (void)instanceCount;
   (void)vertexOffset;
   (void)firstInstance;
   if (!cmd->push_map)
      return;
   if (cmd->bound_gfx_pipeline)
      nvrm_cmd_emit_pipeline_state(cmd, cmd->bound_gfx_pipeline);
   /* Index buffer must have been bound; emit draw with first/count */
   nv_3d_emit_draw_index_buffer(&cmd->push, firstIndex, indexCount);
}

static VKAPI_ATTR void VKAPI_CALL
nvrm_CmdBindVertexBuffers2(VkCommandBuffer commandBuffer, uint32_t firstBinding,
                           uint32_t bindingCount, const VkBuffer *pBuffers,
                           const VkDeviceSize *pOffsets,
                           const VkDeviceSize *pSizes,
                           const VkDeviceSize *pStrides)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   uint32_t i;
   if (!cmd->push_map)
      return;
   nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_3D);
   for (i = 0; i < bindingCount; i++) {
      VK_FROM_HANDLE(nvrm_buffer, buf, pBuffers[i]);
      uint64_t addr = 0;
      uint32_t size = 0;
      uint32_t stride = pStrides ? (uint32_t)pStrides[i] : 12;
      uint32_t slot = firstBinding + i;
      if (buf) {
         addr = (buf->addr ? buf->addr : (buf->bo ? nv_rm_bo_gpu_offset(buf->bo) : 0))
                + pOffsets[i];
         if (pSizes && pSizes[i] != VK_WHOLE_SIZE)
            size = (uint32_t)pSizes[i];
         else if (buf->bo)
            size = (uint32_t)nv_rm_bo_size(buf->bo);
      }
      if (slot < 16)
         nv_3d_set_vertex_stream(&cmd->push, slot, addr, size, stride ? stride : 12);
   }
}

static VKAPI_ATTR void VKAPI_CALL
nvrm_CmdBindIndexBuffer2KHR(VkCommandBuffer commandBuffer, VkBuffer buffer,
                            VkDeviceSize offset, VkDeviceSize size,
                            VkIndexType indexType)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvrm_buffer, buf, buffer);
   uint64_t addr = 0;
   uint64_t sz = size;
   unsigned isz = 4;
   if (!cmd->push_map)
      return;
   if (buf)
      addr = (buf->addr ? buf->addr : (buf->bo ? nv_rm_bo_gpu_offset(buf->bo) : 0)) + offset;
   if (sz == VK_WHOLE_SIZE && buf && buf->bo)
      sz = nv_rm_bo_size(buf->bo) > offset ? nv_rm_bo_size(buf->bo) - offset : 0;
   if (indexType == VK_INDEX_TYPE_UINT16)
      isz = 2;
   else if (indexType == VK_INDEX_TYPE_UINT8_EXT)
      isz = 1;
   nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_3D);
   nv_3d_set_index_buffer(&cmd->push, addr, sz, isz);
}

static VKAPI_ATTR void VKAPI_CALL
nvrm_CmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer,
                        VkDeviceSize offset, VkIndexType indexType)
{
   nvrm_CmdBindIndexBuffer2KHR(commandBuffer, buffer, offset, VK_WHOLE_SIZE,
                               indexType);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdBindDescriptorSets(VkCommandBuffer commandBuffer,
                           VkPipelineBindPoint pipelineBindPoint,
                           VkPipelineLayout layout, uint32_t firstSet,
                           uint32_t descriptorSetCount,
                           const VkDescriptorSet *pDescriptorSets,
                           uint32_t dynamicOffsetCount,
                           const uint32_t *pDynamicOffsets)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   uint32_t i, u;
   (void)pipelineBindPoint;
   (void)layout;
   (void)dynamicOffsetCount;
   (void)pDynamicOffsets;
   if (!cmd->push_map)
      return;
   nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_3D);
   for (i = 0; i < descriptorSetCount; i++) {
      VK_FROM_HANDLE(nvrm_descriptor_set, set, pDescriptorSets[i]);
      if (!set)
         continue;
      for (u = 0; u < set->ubo_count; u++) {
         uint64_t addr = 0;
         uint32_t sz = (uint32_t)set->ubo[u].range;
         if (set->ubo[u].bo)
            addr = nv_rm_bo_gpu_offset(set->ubo[u].bo) + set->ubo[u].offset;
         if (!sz && set->ubo[u].bo)
            sz = (uint32_t)nv_rm_bo_size(set->ubo[u].bo);
         if (sz) {
            nv_3d_set_constant_buffer_selector(&cmd->push, (sz + 255u) & ~255u, addr);
            /* firstSet+i as bind group approximation; slot u */
            nv_3d_bind_group_constant_buffer(&cmd->push, firstSet + i, u, true);
         }
      }
      /* Bind texture header/sampler pools when set has image descriptors */
      if (set->img_count > 0 && cmd->device && cmd->device->tex_pool) {
         nv_tex_pool_emit_bind(&cmd->push, cmd->device->tex_pool);
      }
   }
}




/* NVC3C0 QMD-based compute dispatch (clc3c0 / clc3c0qmd v02.02). */

static void
nvrm_emit_compute_dispatch(struct nvrm_cmd_buffer *cmd,
                           uint32_t gx, uint32_t gy, uint32_t gz)
{
   struct nv_qmd_desc desc;
   struct nvrm_compute_pipeline *cp;
   struct nv_shader *cs;
   const struct nv_device_info *info;
   uint32_t class_compute = 0;
   uint64_t qmd_addr = 0;
   uint64_t prog = 0;
   uint32_t regs = 16;
   uint32_t cta_x = 1, cta_y = 1, cta_z = 1;
   uint32_t shared = 0;
   uint8_t sass_ver = 0x50;

   if (!cmd || !cmd->push_map)
      return;

   cp = cmd->bound_compute_pipeline;
   cs = cp ? cp->cs : NULL;
   info = cmd->device ? cmd->device->info : NULL;
   if (info) {
      class_compute = info->class_compute;
      if (info->sm_version)
         sass_ver = (uint8_t)(info->sm_version & 0xff);
   }

   if (cs) {
      prog = cs->code_gpu_addr;
      if (cs->register_count)
         regs = cs->register_count;
      if (cs->const_gpu_addr && cs->const_size) {
         /* filled into desc below */
      }
   }

   if (cp) {
      cta_x = cp->local_size_x ? cp->local_size_x : 1;
      cta_y = cp->local_size_y ? cp->local_size_y : 1;
      cta_z = cp->local_size_z ? cp->local_size_z : 1;
      shared = cp->shared_mem_bytes;
   } else if (cmd->compute_local_x) {
      cta_x = cmd->compute_local_x;
      cta_y = cmd->compute_local_y ? cmd->compute_local_y : 1;
      cta_z = cmd->compute_local_z ? cmd->compute_local_z : 1;
   }

   if (!gx) gx = 1;
   if (!gy) gy = 1;
   if (!gz) gz = 1;

   memset(&desc, 0, sizeof(desc));
   desc.program_addr = prog;
   desc.program_offset = 0;
   desc.grid_x = gx;
   desc.grid_y = gy;
   desc.grid_z = gz;
   desc.cta_x = cta_x;
   desc.cta_y = cta_y;
   desc.cta_z = cta_z;
   desc.register_count = regs;
   desc.shared_mem_size = shared;
   desc.sass_version = sass_ver;
   desc.sm_global_caching = true;
   desc.invalidate_caches = !cmd->compute_init_done;

   if (cs && cs->const_gpu_addr && cs->const_size) {
      desc.cb_addr[0] = cs->const_gpu_addr;
      desc.cb_size[0] = cs->const_size;
      desc.cb_valid_mask = 0x1;
   }

   if (cmd->qmd_bo)
      qmd_addr = nv_rm_bo_gpu_offset(cmd->qmd_bo);
   else if (cmd->push_bo)
      /* Fallback: use push BO GPU offset as QMD address field only;
       * inline LOAD_INLINE_QMD_DATA carries the actual QMD contents. */
      qmd_addr = nv_rm_bo_gpu_offset(cmd->push_bo);

   nv_compute_emit_dispatch(&cmd->push, &desc, qmd_addr, class_compute);
   cmd->compute_init_done = true;
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdDispatch(VkCommandBuffer commandBuffer,
                 uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   if (!cmd || !cmd->push_map)
      return;
   nvrm_emit_compute_dispatch(cmd, groupCountX, groupCountY, groupCountZ);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdDispatchBase(VkCommandBuffer commandBuffer,
                     uint32_t baseGroupX, uint32_t baseGroupY, uint32_t baseGroupZ,
                     uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
   /* QMD CTA raster has no base offset field in the simple path; base groups
    * would need shader-side workgroup ID remapping.  For now launch the same
    * grid size (base is ignored until full indirect/base support lands). */
   (void)baseGroupX; (void)baseGroupY; (void)baseGroupZ;
   nvrm_CmdDispatch(commandBuffer, groupCountX, groupCountY, groupCountZ);
}

/* Queue submit: kick channel with cmd buffer push contents */
VKAPI_ATTR VkResult VKAPI_CALL
nvrm_QueueSubmit2(VkQueue _queue, uint32_t submitCount,
                  const VkSubmitInfo2 *pSubmits, VkFence fence)
{
   VK_FROM_HANDLE(nvrm_queue, queue, _queue);
   uint32_t s, c;
   (void)fence;

   if (!queue->channel_ready || !queue->channel)
      return VK_SUCCESS; /* record-only without channel */

   for (s = 0; s < submitCount; s++) {
      const VkSubmitInfo2 *sub = &pSubmits[s];
      for (c = 0; c < sub->commandBufferInfoCount; c++) {
         VK_FROM_HANDLE(nvrm_cmd_buffer, cmd,
                        sub->pCommandBufferInfos[c].commandBuffer);
         if (!cmd || !cmd->push_map || !cmd->push_dw_used)
            continue;
         /* Copy cmd push into channel pushbuffer and kick — simplified:
          * re-init channel push from cmd buffer content if sizes allow */
         uint32_t *dst = nv_channel_push_begin(queue->channel, cmd->push_dw_used);
         if (dst) {
            memcpy(dst, cmd->push_map, cmd->push_dw_used * 4);
            queue->channel->push_dw_used =
               queue->channel->push_dw_base + cmd->push_dw_used;
            nv_channel_kickoff(queue->channel);
         }
      }
   }
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvrm_QueueSubmit(VkQueue queue, uint32_t submitCount,
                 const VkSubmitInfo *pSubmits, VkFence fence)
{
   /* Thin wrapper: only support command buffers from first submit */
   VkSubmitInfo2 s2;
   VkCommandBufferSubmitInfo cbs[8];
   uint32_t i, n;
   if (submitCount == 0)
      return VK_SUCCESS;
   n = pSubmits[0].commandBufferCount;
   if (n > 8)
      n = 8;
   memset(&s2, 0, sizeof(s2));
   s2.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
   s2.commandBufferInfoCount = n;
   s2.pCommandBufferInfos = cbs;
   for (i = 0; i < n; i++) {
      memset(&cbs[i], 0, sizeof(cbs[i]));
      cbs[i].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
      cbs[i].commandBuffer = pSubmits[0].pCommandBuffers[i];
   }
   return nvrm_QueueSubmit2(queue, 1, &s2, fence);
}
