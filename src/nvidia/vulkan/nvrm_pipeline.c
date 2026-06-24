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
#include "nv_copy_methods.h"
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

/* Per-binding metadata captured from VkDescriptorSetLayoutCreateInfo. */
#define NVRM_MAX_LAYOUT_BINDINGS  32
#define NVRM_MAX_DESC_SLOTS       16

enum nvrm_desc_kind {
   NVRM_DESC_KIND_NONE = 0,
   NVRM_DESC_KIND_UBO,
   NVRM_DESC_KIND_SSBO,
   NVRM_DESC_KIND_IMAGE,
   NVRM_DESC_KIND_SAMPLER,
   NVRM_DESC_KIND_TEXEL,
};

struct nvrm_layout_binding {
   uint32_t binding;
   VkDescriptorType type;
   enum nvrm_desc_kind kind;
   uint32_t descriptor_count;
   VkShaderStageFlags stage_flags;
   bool is_dynamic;
   /* Flat index into set->ubo/ssbo/img arrays for this binding's first element */
   uint16_t flat_base;
   uint16_t flat_count;
};

struct nvrm_descriptor_set_layout {
   struct vk_descriptor_set_layout vk;
   struct nvrm_layout_binding bindings[NVRM_MAX_LAYOUT_BINDINGS];
   uint32_t binding_count;
   uint32_t ubo_slots;   /* total UBO descriptors across all bindings */
   uint32_t ssbo_slots;
   uint32_t img_slots;
   uint32_t dynamic_ubo_count;
   uint32_t dynamic_ssbo_count;
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
      bool is_dynamic;
   } ubo[NVRM_MAX_DESC_SLOTS];
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
   } img[NVRM_MAX_DESC_SLOTS];
   uint32_t img_count;
   struct {
      struct nv_rm_bo *bo;
      uint64_t offset;
      uint64_t range;
      bool is_dynamic;
   } ssbo[NVRM_MAX_DESC_SLOTS];
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

/* Vertex input attribute/binding cached at pipeline create (emitted on bind). */
struct nvrm_vtx_attrib {
   uint32_t location;      /* shader location / attribute index */
   uint32_t binding;
   uint32_t offset;
   uint32_t component_fmt; /* NVC597 component bit-width code */
   bool active;
};

struct nvrm_vtx_binding_info {
   uint32_t binding;
   uint32_t stride;
   uint32_t input_rate; /* 0=vertex, 1=instance */
   bool used;
};

struct nvrm_graphics_pipeline {
   struct vk_object_base base;
   struct nvrm_device *device;
   struct nv_shader *vs;
   struct nv_shader *fs;
   struct nv_shader *gs;   /* optional geometry */
   struct nv_shader *tcs;  /* optional tess control */
   struct nv_shader *tes;  /* optional tess eval */
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
   bool prim_restart_enable;
   /* Vertex input */
   struct nvrm_vtx_attrib attribs[NVRM_MAX_VTX_ATTRIBS];
   uint32_t attrib_count;
   struct nvrm_vtx_binding_info vtx_bindings[NVRM_MAX_VTX_BINDINGS];
   uint32_t vtx_binding_count;
   /* Multisample / line width (minimal) */
   uint32_t sample_count;
   float line_width;
};

struct nvrm_compute_pipeline {
   struct vk_object_base base;
   struct nvrm_device *device;
   struct nv_shader *cs;
   uint32_t local_size_x;
   uint32_t local_size_y;
   uint32_t local_size_z;
   uint32_t shared_mem_bytes;
   uint32_t local_mem_bytes; /* per-thread LMEM from compiler/SPH */
};

VK_DEFINE_NONDISP_HANDLE_CASTS(nvrm_graphics_pipeline, base, VkPipeline,
                               VK_OBJECT_TYPE_PIPELINE)
VK_DEFINE_NONDISP_HANDLE_CASTS(nvrm_compute_pipeline, base, VkPipeline,
                               VK_OBJECT_TYPE_PIPELINE)

/* ---------- render pass / framebuffer (minimal, for compatibility) ---------- */

#define NVRM_MAX_RP_ATTACHMENTS  8
#define NVRM_MAX_RP_SUBPASSES    4

struct nvrm_rp_attachment {
   VkFormat format;
   VkAttachmentLoadOp load_op;
   VkAttachmentStoreOp store_op;
   VkAttachmentLoadOp stencil_load_op;
   VkImageLayout initial_layout;
   VkImageLayout final_layout;
   bool is_depth_stencil;
};

struct nvrm_rp_subpass {
   uint32_t color_count;
   uint32_t color_refs[NVRM_MAX_RP_ATTACHMENTS];
   int32_t depth_stencil_ref; /* -1 if none */
};

struct nvrm_render_pass {
   struct vk_object_base base;
   uint32_t attachment_count;
   uint32_t subpass_count;
   struct nvrm_rp_attachment atts[NVRM_MAX_RP_ATTACHMENTS];
   struct nvrm_rp_subpass subpasses[NVRM_MAX_RP_SUBPASSES];
};

struct nvrm_framebuffer {
   struct vk_object_base base;
   uint32_t width, height, layers;
   uint32_t attachment_count;
   struct nvrm_image_view *views[NVRM_MAX_RP_ATTACHMENTS];
};

struct nvrm_image_view {
   struct vk_object_base base;
   struct nvrm_image *image;
   VkFormat format;
   uint32_t level, layer, layer_count;
   VkImageAspectFlags aspect_mask;
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
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY: return 0xA; /* LINELIST_ADJCY */
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY: return 0xB;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY: return 0xC;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY: return 0xD;
   case VK_PRIMITIVE_TOPOLOGY_PATCH_LIST: return 0xE;
   default: return NVC597_TOPOLOGY_TRIANGLES;
   }
}

/* Map VkFormat to NVC597 vertex component bit-width code (subset). */
static uint32_t
vk_format_to_vtx_comp(VkFormat fmt)
{
   switch (fmt) {
   case VK_FORMAT_R32G32B32A32_SFLOAT:
   case VK_FORMAT_R32G32B32A32_SINT:
   case VK_FORMAT_R32G32B32A32_UINT:
      return NVC597_SET_VERTEX_ATTRIBUTE_A_COMPONENT_BIT_WIDTHS_R32_G32_B32_A32;
   case VK_FORMAT_R32G32B32_SFLOAT:
   case VK_FORMAT_R32G32B32_SINT:
   case VK_FORMAT_R32G32B32_UINT:
      return NVC597_SET_VERTEX_ATTRIBUTE_A_COMPONENT_BIT_WIDTHS_R32_G32_B32;
   case VK_FORMAT_R32G32_SFLOAT:
   case VK_FORMAT_R32G32_SINT:
   case VK_FORMAT_R32G32_UINT:
      return NVC597_SET_VERTEX_ATTRIBUTE_A_COMPONENT_BIT_WIDTHS_R32_G32;
   case VK_FORMAT_R32_SFLOAT:
   case VK_FORMAT_R32_SINT:
   case VK_FORMAT_R32_UINT:
      return NVC597_SET_VERTEX_ATTRIBUTE_A_COMPONENT_BIT_WIDTHS_R32;
   case VK_FORMAT_R16G16B16A16_SFLOAT:
   case VK_FORMAT_R16G16B16A16_SINT:
   case VK_FORMAT_R16G16B16A16_UINT:
   case VK_FORMAT_R16G16B16A16_SNORM:
   case VK_FORMAT_R16G16B16A16_UNORM:
      return NVC597_SET_VERTEX_ATTRIBUTE_A_COMPONENT_BIT_WIDTHS_R16_G16_B16_A16;
   case VK_FORMAT_R16G16_SFLOAT:
   case VK_FORMAT_R16G16_SINT:
   case VK_FORMAT_R16G16_UINT:
   case VK_FORMAT_R16G16_SNORM:
   case VK_FORMAT_R16G16_UNORM:
      return NVC597_SET_VERTEX_ATTRIBUTE_A_COMPONENT_BIT_WIDTHS_R16_G16;
   case VK_FORMAT_R8G8B8A8_UNORM:
   case VK_FORMAT_R8G8B8A8_SNORM:
   case VK_FORMAT_R8G8B8A8_UINT:
   case VK_FORMAT_R8G8B8A8_SINT:
   case VK_FORMAT_B8G8R8A8_UNORM:
   case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
      return NVC597_SET_VERTEX_ATTRIBUTE_A_COMPONENT_BIT_WIDTHS_R8_G8_B8_A8;
   case VK_FORMAT_R8G8_UNORM:
   case VK_FORMAT_R8G8_SNORM:
      return NVC597_SET_VERTEX_ATTRIBUTE_A_COMPONENT_BIT_WIDTHS_R8_G8;
   default:
      return NVC597_SET_VERTEX_ATTRIBUTE_A_COMPONENT_BIT_WIDTHS_R32_G32_B32_A32;
   }
}

/* Map VkFormat to NVC597 color target / ZT format codes (subset). */
static uint32_t
vk_format_to_ct_format(VkFormat fmt)
{
   switch (fmt) {
   case VK_FORMAT_B8G8R8A8_UNORM:
   case VK_FORMAT_B8G8R8A8_SRGB:
      return NVC597_SET_COLOR_TARGET_FORMAT_V_B8G8R8A8;
   case VK_FORMAT_R8G8B8A8_UNORM:
   case VK_FORMAT_R8G8B8A8_SRGB:
   case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
      return NVC597_SET_COLOR_TARGET_FORMAT_V_A8B8G8R8;
   case VK_FORMAT_R16G16B16A16_SFLOAT:
      return NVC597_SET_COLOR_TARGET_FORMAT_V_RF16_GF16_BF16_AF16;
   case VK_FORMAT_R32G32B32A32_SFLOAT:
      return NVC597_SET_COLOR_TARGET_FORMAT_V_RF32_GF32_BF32_AF32;
   case VK_FORMAT_R16G16B16A16_UNORM:
      return NVC597_SET_COLOR_TARGET_FORMAT_V_R16_G16_B16_A16;
   case VK_FORMAT_R5G6B5_UNORM_PACK16:
      return NVC597_SET_COLOR_TARGET_FORMAT_V_R5G6B5;
   case VK_FORMAT_R32_SFLOAT:
   case VK_FORMAT_R32_UINT:
   case VK_FORMAT_R32_SINT:
      return NVC597_SET_COLOR_TARGET_FORMAT_V_R32;
   case VK_FORMAT_R8_UNORM:
   case VK_FORMAT_R8_UINT:
   case VK_FORMAT_R8_SINT:
      return NVC597_SET_COLOR_TARGET_FORMAT_V_R8;
   case VK_FORMAT_R16_UNORM:
   case VK_FORMAT_R16_SFLOAT:
   case VK_FORMAT_R16_UINT:
   case VK_FORMAT_R16_SINT:
      return NVC597_SET_COLOR_TARGET_FORMAT_V_R16;
   case VK_FORMAT_R16G16_UNORM:
   case VK_FORMAT_R16G16_SFLOAT:
   case VK_FORMAT_R16G16_UINT:
   case VK_FORMAT_R16G16_SINT:
      return NVC597_SET_COLOR_TARGET_FORMAT_V_R16_G16;
   case VK_FORMAT_R32G32_SFLOAT:
   case VK_FORMAT_R32G32_UINT:
   case VK_FORMAT_R32G32_SINT:
      return NVC597_SET_COLOR_TARGET_FORMAT_V_RF32_GF32;
   case VK_FORMAT_R8G8_UNORM:
   case VK_FORMAT_R8G8_UINT:
   case VK_FORMAT_R8G8_SINT:
      return NVC597_SET_COLOR_TARGET_FORMAT_V_R8_G8;
   case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
   case VK_FORMAT_A2B10G10R10_UINT_PACK32:
      return NVC597_SET_COLOR_TARGET_FORMAT_V_A2B10G10R10;
   case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
      return NVC597_SET_COLOR_TARGET_FORMAT_V_RF11_GF11_BF10;
   case VK_FORMAT_R8G8B8A8_UINT:
   case VK_FORMAT_R8G8B8A8_SINT:
      return NVC597_SET_COLOR_TARGET_FORMAT_V_R8_G8_B8_A8;
   default:
      return NVC597_SET_COLOR_TARGET_FORMAT_V_A8B8G8R8;
   }
}

static uint32_t
vk_format_to_zt_format(VkFormat fmt)
{
   switch (fmt) {
   case VK_FORMAT_D16_UNORM:
      return NVC597_SET_ZT_FORMAT_V_Z16;
   case VK_FORMAT_D32_SFLOAT:
      return NVC597_SET_ZT_FORMAT_V_ZF32;
   case VK_FORMAT_D24_UNORM_S8_UINT:
      return NVC597_SET_ZT_FORMAT_V_Z24S8;
   case VK_FORMAT_D32_SFLOAT_S8_UINT:
      return NVC597_SET_ZT_FORMAT_V_ZF32_X24S8;
   case VK_FORMAT_X8_D24_UNORM_PACK32:
      return NVC597_SET_ZT_FORMAT_V_X8Z24;
   case VK_FORMAT_S8_UINT:
      return NVC597_SET_ZT_FORMAT_V_S8;
   default:
      return NVC597_SET_ZT_FORMAT_V_Z24S8;
   }
}

static bool
vk_format_is_depth_stencil(VkFormat fmt)
{
   switch (fmt) {
   case VK_FORMAT_D16_UNORM:
   case VK_FORMAT_D32_SFLOAT:
   case VK_FORMAT_D24_UNORM_S8_UINT:
   case VK_FORMAT_D32_SFLOAT_S8_UINT:
   case VK_FORMAT_X8_D24_UNORM_PACK32:
   case VK_FORMAT_S8_UINT:
      return true;
   default:
      return false;
   }
}

static enum nvrm_desc_kind
vk_desc_type_to_kind(VkDescriptorType t, bool *is_dynamic_out)
{
   *is_dynamic_out = false;
   if (t == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) {
      *is_dynamic_out = true;
      return NVRM_DESC_KIND_UBO;
   }
   if (t == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
      return NVRM_DESC_KIND_UBO;
   if (t == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) {
      *is_dynamic_out = true;
      return NVRM_DESC_KIND_SSBO;
   }
   if (t == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
      return NVRM_DESC_KIND_SSBO;
   if (t == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
       t == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
       t == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ||
       t == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT)
      return NVRM_DESC_KIND_IMAGE;
   if (t == VK_DESCRIPTOR_TYPE_SAMPLER)
      return NVRM_DESC_KIND_SAMPLER;
   if (t == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER ||
       t == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)
      return NVRM_DESC_KIND_TEXEL;
   return NVRM_DESC_KIND_NONE;
}

/* Look up layout binding by Vulkan binding number. */
static const struct nvrm_layout_binding *
nvrm_layout_find_binding(const struct nvrm_descriptor_set_layout *layout,
                         uint32_t binding)
{
   uint32_t i;
   if (!layout)
      return NULL;
   for (i = 0; i < layout->binding_count; i++) {
      if (layout->bindings[i].binding == binding)
         return &layout->bindings[i];
   }
   return NULL;
}

/* Flat slot index for (binding, arrayElement) within its kind array. */
static int
nvrm_layout_flat_slot(const struct nvrm_descriptor_set_layout *layout,
                      uint32_t binding, uint32_t array_element,
                      enum nvrm_desc_kind *kind_out)
{
   const struct nvrm_layout_binding *lb = nvrm_layout_find_binding(layout, binding);
   if (!lb)
      return -1;
   if (kind_out)
      *kind_out = lb->kind;
   if (array_element >= lb->flat_count)
      return -1;
   return (int)lb->flat_base + (int)array_element;
}

static void
nvrm_pipeline_capture_vertex_input(struct nvrm_graphics_pipeline *pipe,
                                   const VkPipelineVertexInputStateCreateInfo *vi)
{
   uint32_t i;
   if (!pipe || !vi)
      return;
   pipe->attrib_count = 0;
   pipe->vtx_binding_count = 0;
   for (i = 0; i < vi->vertexBindingDescriptionCount &&
        pipe->vtx_binding_count < NVRM_MAX_VTX_BINDINGS; i++) {
      const VkVertexInputBindingDescription *b = &vi->pVertexBindingDescriptions[i];
      struct nvrm_vtx_binding_info *dst = &pipe->vtx_bindings[pipe->vtx_binding_count++];
      dst->binding = b->binding;
      dst->stride = b->stride;
      dst->input_rate = (b->inputRate == VK_VERTEX_INPUT_RATE_INSTANCE) ? 1 : 0;
      dst->used = true;
   }
   for (i = 0; i < vi->vertexAttributeDescriptionCount &&
        pipe->attrib_count < NVRM_MAX_VTX_ATTRIBS; i++) {
      const VkVertexInputAttributeDescription *a = &vi->pVertexAttributeDescriptions[i];
      struct nvrm_vtx_attrib *dst = &pipe->attribs[pipe->attrib_count++];
      dst->location = a->location;
      dst->binding = a->binding;
      dst->offset = a->offset;
      dst->component_fmt = vk_format_to_vtx_comp(a->format);
      dst->active = true;
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

      if (ci->pInputAssemblyState) {
         pipe->topology = ci->pInputAssemblyState->topology;
         pipe->prim_restart_enable = ci->pInputAssemblyState->primitiveRestartEnable;
      }
      pipe->topology_nv = vk_topology_to_nv(pipe->topology);

      if (ci->pRasterizationState) {
         pipe->cull_mode = ci->pRasterizationState->cullMode;
         pipe->front_ccw = ci->pRasterizationState->frontFace ==
                           VK_FRONT_FACE_COUNTER_CLOCKWISE;
         pipe->line_width = ci->pRasterizationState->lineWidth;
      }
      if (ci->pDepthStencilState) {
         pipe->depth_test_enable = ci->pDepthStencilState->depthTestEnable;
         pipe->depth_write_enable = ci->pDepthStencilState->depthWriteEnable;
         pipe->depth_compare_op = ci->pDepthStencilState->depthCompareOp;
      }
      if (ci->pColorBlendState && ci->pColorBlendState->attachmentCount)
         pipe->blend_enable = ci->pColorBlendState->pAttachments[0].blendEnable;
      if (ci->pMultisampleState)
         pipe->sample_count = ci->pMultisampleState->rasterizationSamples;
      if (ci->pVertexInputState)
         nvrm_pipeline_capture_vertex_input(pipe, ci->pVertexInputState);

      for (s = 0; s < ci->stageCount; s++) {
         const VkPipelineShaderStageCreateInfo *st = &ci->pStages[s];
         struct nv_shader *sh = nvrm_compile_stage(device, st);
         if (!sh)
            continue;
         if (st->stage == VK_SHADER_STAGE_VERTEX_BIT)
            pipe->vs = sh;
         else if (st->stage == VK_SHADER_STAGE_FRAGMENT_BIT)
            pipe->fs = sh;
         else if (st->stage == VK_SHADER_STAGE_GEOMETRY_BIT)
            pipe->gs = sh;
         else if (st->stage == VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT)
            pipe->tcs = sh;
         else if (st->stage == VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)
            pipe->tes = sh;
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
         /* scratch_size is per-thread spill/temp; align to 16 like SPH local_low */
         if (ns->scratch_size)
            pipe->local_mem_bytes = (uint32_t)((ns->scratch_size + 15) & ~15);
      }
      /* Compiler may have recorded spill requirement on the shader object */
      if (pipe->cs && pipe->cs->local_mem_size > pipe->local_mem_bytes)
         pipe->local_mem_bytes = pipe->cs->local_mem_size;
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
   if (gp->vs || gp->fs || gp->gs || gp->tcs || gp->tes || gp->device == device) {
      struct nvrm_compute_pipeline *cp = (struct nvrm_compute_pipeline *)(void *)gp;
      if (!gp->vs && !gp->fs && !gp->gs && !gp->tcs && !gp->tes && cp->cs) {
         nv_shader_destroy(cp->cs);
         vk_object_base_finish(&cp->base);
         vk_free2(&device->vk.alloc, pAllocator, cp);
         return;
      }
      nv_shader_destroy(gp->vs);
      nv_shader_destroy(gp->fs);
      nv_shader_destroy(gp->gs);
      nv_shader_destroy(gp->tcs);
      nv_shader_destroy(gp->tes);
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
   uint32_t i;
   uint16_t ubo_next = 0, ssbo_next = 0, img_next = 0;

   layout = vk_descriptor_set_layout_zalloc(&device->vk, sizeof(*layout),
                                            pCreateInfo);
   if (!layout)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   layout->binding_count = 0;
   layout->ubo_slots = layout->ssbo_slots = layout->img_slots = 0;
   layout->dynamic_ubo_count = layout->dynamic_ssbo_count = 0;

   for (i = 0; i < pCreateInfo->bindingCount &&
        layout->binding_count < NVRM_MAX_LAYOUT_BINDINGS; i++) {
      const VkDescriptorSetLayoutBinding *b = &pCreateInfo->pBindings[i];
      struct nvrm_layout_binding *lb = &layout->bindings[layout->binding_count++];
      bool is_dyn = false;
      uint32_t count = b->descriptorCount ? b->descriptorCount : 1;

      lb->binding = b->binding;
      lb->type = b->descriptorType;
      lb->kind = vk_desc_type_to_kind(b->descriptorType, &is_dyn);
      lb->descriptor_count = count;
      lb->stage_flags = b->stageFlags;
      lb->is_dynamic = is_dyn;
      lb->flat_base = 0;
      lb->flat_count = 0;

      if (lb->kind == NVRM_DESC_KIND_UBO) {
         lb->flat_base = ubo_next;
         lb->flat_count = (uint16_t)MIN2(count, NVRM_MAX_DESC_SLOTS - ubo_next);
         ubo_next = (uint16_t)(ubo_next + lb->flat_count);
         if (is_dyn)
            layout->dynamic_ubo_count += lb->flat_count;
      } else if (lb->kind == NVRM_DESC_KIND_SSBO ||
                 lb->kind == NVRM_DESC_KIND_TEXEL) {
         lb->flat_base = ssbo_next;
         lb->flat_count = (uint16_t)MIN2(count, NVRM_MAX_DESC_SLOTS - ssbo_next);
         ssbo_next = (uint16_t)(ssbo_next + lb->flat_count);
         if (is_dyn)
            layout->dynamic_ssbo_count += lb->flat_count;
      } else if (lb->kind == NVRM_DESC_KIND_IMAGE ||
                 lb->kind == NVRM_DESC_KIND_SAMPLER) {
         lb->flat_base = img_next;
         lb->flat_count = (uint16_t)MIN2(count, NVRM_MAX_DESC_SLOTS - img_next);
         img_next = (uint16_t)(img_next + lb->flat_count);
      }
   }
   layout->ubo_slots = ubo_next;
   layout->ssbo_slots = ssbo_next;
   layout->img_slots = img_next;

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
      /* Pre-size slot counts from layout so writes by binding index fit */
      if (layout) {
         set->ubo_count = layout->ubo_slots;
         set->ssbo_count = layout->ssbo_slots;
         set->img_count = layout->img_slots;
      }
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

static void
nvrm_desc_write_ubo_slot(struct nvrm_descriptor_set *set, uint32_t slot,
                         const VkDescriptorBufferInfo *info, bool is_dynamic)
{
   VK_FROM_HANDLE(nvrm_buffer, buf, info->buffer);
   if (slot >= NVRM_MAX_DESC_SLOTS)
      return;
   set->ubo[slot].bo = buf ? buf->bo : NULL;
   set->ubo[slot].offset = info->offset;
   set->ubo[slot].range = info->range;
   set->ubo[slot].is_dynamic = is_dynamic;
   if (slot >= set->ubo_count)
      set->ubo_count = slot + 1;
}

static void
nvrm_desc_write_ssbo_slot(struct nvrm_descriptor_set *set, uint32_t slot,
                          const VkDescriptorBufferInfo *info, bool is_dynamic)
{
   VK_FROM_HANDLE(nvrm_buffer, buf, info->buffer);
   if (slot >= NVRM_MAX_DESC_SLOTS)
      return;
   set->ssbo[slot].bo = buf ? buf->bo : NULL;
   set->ssbo[slot].offset = info->offset;
   set->ssbo[slot].range = info->range;
   set->ssbo[slot].is_dynamic = is_dynamic;
   if (slot >= set->ssbo_count)
      set->ssbo_count = slot + 1;
}

static void
nvrm_desc_write_image_slot(struct nvrm_device *device,
                           struct nvrm_descriptor_set *set, uint32_t slot,
                           const VkDescriptorImageInfo *info,
                           VkDescriptorType dtype)
{
   VK_FROM_HANDLE(nvrm_image_view, view, info->imageView);
   struct nv_tex_pool *pool = nvrm_device_ensure_tex_pool(device);
   struct nv_tex_entry entry;
   int tex_slot = -1;
   if (slot >= NVRM_MAX_DESC_SLOTS)
      return;
   if (view && pool &&
       nvrm_write_combined_image_sampler(device, view, NULL, info->sampler,
                                         &entry) == 0) {
      tex_slot = nv_tex_pool_set_entry(pool, -1, &entry);
   }
   set->img[slot].tex_slot = tex_slot;
   set->img[slot].view = view;
   set->img[slot].format = view ? view->format : VK_FORMAT_UNDEFINED;
   if (view && view->image) {
      set->img[slot].width = view->image->vk.extent.width;
      set->img[slot].height = view->image->vk.extent.height;
      set->img[slot].pitch = view->image->row_pitch;
      set->img[slot].gpu_addr =
         view->image->gpu_offset ? view->image->gpu_offset :
         (view->image->bo ? nv_rm_bo_gpu_offset(view->image->bo) : 0);
   }
   set->img[slot].has_sampler =
      (dtype == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
   if (slot >= set->img_count)
      set->img_count = slot + 1;
}

VKAPI_ATTR void VKAPI_CALL
nvrm_UpdateDescriptorSets(VkDevice _device,
                          uint32_t descriptorWriteCount,
                          const VkWriteDescriptorSet *pDescriptorWrites,
                          uint32_t descriptorCopyCount,
                          const VkCopyDescriptorSet *pDescriptorCopies)
{
   VK_FROM_HANDLE(nvrm_device, device, _device);
   uint32_t i, j;

   for (i = 0; i < descriptorWriteCount; i++) {
      const VkWriteDescriptorSet *w = &pDescriptorWrites[i];
      VK_FROM_HANDLE(nvrm_descriptor_set, set, w->dstSet);
      enum nvrm_desc_kind kind = NVRM_DESC_KIND_NONE;
      bool is_dyn = false;
      int flat;
      if (!set)
         continue;

      /* Resolve through layout when available; else fall back to binding index */
      flat = nvrm_layout_flat_slot(set->layout, w->dstBinding,
                                   w->dstArrayElement, &kind);
      if (flat < 0) {
         kind = vk_desc_type_to_kind(w->descriptorType, &is_dyn);
         flat = (int)(w->dstBinding + w->dstArrayElement);
      } else {
         const struct nvrm_layout_binding *lb =
            nvrm_layout_find_binding(set->layout, w->dstBinding);
         is_dyn = lb ? lb->is_dynamic : false;
      }

      if (kind == NVRM_DESC_KIND_UBO ||
          w->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
          w->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) {
         for (j = 0; j < w->descriptorCount; j++) {
            int s = flat + (int)j;
            if (s >= 0 && s < (int)NVRM_MAX_DESC_SLOTS)
               nvrm_desc_write_ubo_slot(set, (uint32_t)s, &w->pBufferInfo[j],
                                        is_dyn ||
                                        w->descriptorType ==
                                        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
         }
      } else if (kind == NVRM_DESC_KIND_SSBO || kind == NVRM_DESC_KIND_TEXEL ||
                 w->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
                 w->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) {
         for (j = 0; j < w->descriptorCount; j++) {
            int s = flat + (int)j;
            if (s >= 0 && s < (int)NVRM_MAX_DESC_SLOTS && w->pBufferInfo)
               nvrm_desc_write_ssbo_slot(set, (uint32_t)s, &w->pBufferInfo[j],
                                         is_dyn ||
                                         w->descriptorType ==
                                         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC);
         }
      } else if (kind == NVRM_DESC_KIND_IMAGE || kind == NVRM_DESC_KIND_SAMPLER ||
                 w->descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
                 w->descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
                 w->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ||
                 w->descriptorType == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT) {
         for (j = 0; j < w->descriptorCount; j++) {
            int s = flat + (int)j;
            if (s >= 0 && s < (int)NVRM_MAX_DESC_SLOTS && w->pImageInfo)
               nvrm_desc_write_image_slot(device, set, (uint32_t)s,
                                          &w->pImageInfo[j], w->descriptorType);
         }
      }
   }

   /* Descriptor copies: copy by layout-resolved flat slots when possible */
   for (i = 0; i < descriptorCopyCount; i++) {
      const VkCopyDescriptorSet *c = &pDescriptorCopies[i];
      VK_FROM_HANDLE(nvrm_descriptor_set, src, c->srcSet);
      VK_FROM_HANDLE(nvrm_descriptor_set, dst, c->dstSet);
      uint32_t k;
      if (!src || !dst)
         continue;
      for (k = 0; k < c->descriptorCount; k++) {
         int sslot = nvrm_layout_flat_slot(src->layout, c->srcBinding,
                                           c->srcArrayElement + k, NULL);
         int dslot = nvrm_layout_flat_slot(dst->layout, c->dstBinding,
                                           c->dstArrayElement + k, NULL);
         if (sslot < 0)
            sslot = (int)(c->srcBinding + c->srcArrayElement + k);
         if (dslot < 0)
            dslot = (int)(c->dstBinding + c->dstArrayElement + k);
         if (sslot >= 0 && sslot < (int)NVRM_MAX_DESC_SLOTS &&
             dslot >= 0 && dslot < (int)NVRM_MAX_DESC_SLOTS) {
            dst->ubo[dslot] = src->ubo[sslot];
            dst->ssbo[dslot] = src->ssbo[sslot];
            dst->img[dslot] = src->img[sslot];
            if ((uint32_t)dslot >= dst->ubo_count)
               dst->ubo_count = (uint32_t)dslot + 1;
            if ((uint32_t)dslot >= dst->ssbo_count)
               dst->ssbo_count = (uint32_t)dslot + 1;
            if ((uint32_t)dslot >= dst->img_count)
               dst->img_count = (uint32_t)dslot + 1;
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
   uint32_t i, s;

   rp = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*rp), 8,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!rp)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   vk_object_base_init(&device->vk, &rp->base, VK_OBJECT_TYPE_RENDER_PASS);
   rp->attachment_count = MIN2(pCreateInfo->attachmentCount, NVRM_MAX_RP_ATTACHMENTS);
   rp->subpass_count = MIN2(pCreateInfo->subpassCount, NVRM_MAX_RP_SUBPASSES);

   for (i = 0; i < rp->attachment_count; i++) {
      const VkAttachmentDescription2 *a = &pCreateInfo->pAttachments[i];
      rp->atts[i].format = a->format;
      rp->atts[i].load_op = a->loadOp;
      rp->atts[i].store_op = a->storeOp;
      rp->atts[i].stencil_load_op = a->stencilLoadOp;
      rp->atts[i].initial_layout = a->initialLayout;
      rp->atts[i].final_layout = a->finalLayout;
      rp->atts[i].is_depth_stencil = vk_format_is_depth_stencil(a->format);
   }
   for (s = 0; s < rp->subpass_count; s++) {
      const VkSubpassDescription2 *sp = &pCreateInfo->pSubpasses[s];
      struct nvrm_rp_subpass *out = &rp->subpasses[s];
      uint32_t c;
      out->color_count = MIN2(sp->colorAttachmentCount, NVRM_MAX_RP_ATTACHMENTS);
      out->depth_stencil_ref = -1;
      for (c = 0; c < out->color_count; c++) {
         if (sp->pColorAttachments)
            out->color_refs[c] = sp->pColorAttachments[c].attachment;
         else
            out->color_refs[c] = VK_ATTACHMENT_UNUSED;
      }
      if (sp->pDepthStencilAttachment &&
          sp->pDepthStencilAttachment->attachment != VK_ATTACHMENT_UNUSED)
         out->depth_stencil_ref = (int32_t)sp->pDepthStencilAttachment->attachment;
   }

   *pRenderPass = nvrm_render_pass_to_handle(rp);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvrm_CreateRenderPass(VkDevice device,
                      const VkRenderPassCreateInfo *pCreateInfo,
                      const VkAllocationCallbacks *pAllocator,
                      VkRenderPass *pRenderPass)
{
   /* Promote v1 create info to v2 (attachments + subpasses only) */
   VkRenderPassCreateInfo2 ci2 = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2,
      .attachmentCount = pCreateInfo->attachmentCount,
      .pAttachments = NULL,
      .subpassCount = pCreateInfo->subpassCount,
      .pSubpasses = NULL,
   };
   /* Stack-allocate promoted arrays for common small sizes */
   VkAttachmentDescription2 atts[NVRM_MAX_RP_ATTACHMENTS];
   VkSubpassDescription2 subs[NVRM_MAX_RP_SUBPASSES];
   VkAttachmentReference2 color_refs[NVRM_MAX_RP_SUBPASSES][NVRM_MAX_RP_ATTACHMENTS];
   VkAttachmentReference2 ds_refs[NVRM_MAX_RP_SUBPASSES];
   uint32_t i, s;

   memset(atts, 0, sizeof(atts));
   memset(subs, 0, sizeof(subs));
   for (i = 0; i < pCreateInfo->attachmentCount && i < NVRM_MAX_RP_ATTACHMENTS; i++) {
      const VkAttachmentDescription *a = &pCreateInfo->pAttachments[i];
      atts[i].sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
      atts[i].format = a->format;
      atts[i].samples = a->samples;
      atts[i].loadOp = a->loadOp;
      atts[i].storeOp = a->storeOp;
      atts[i].stencilLoadOp = a->stencilLoadOp;
      atts[i].stencilStoreOp = a->stencilStoreOp;
      atts[i].initialLayout = a->initialLayout;
      atts[i].finalLayout = a->finalLayout;
   }
   ci2.pAttachments = atts;
   for (s = 0; s < pCreateInfo->subpassCount && s < NVRM_MAX_RP_SUBPASSES; s++) {
      const VkSubpassDescription *sp = &pCreateInfo->pSubpasses[s];
      uint32_t c;
      subs[s].sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;
      subs[s].pipelineBindPoint = sp->pipelineBindPoint;
      subs[s].colorAttachmentCount = sp->colorAttachmentCount;
      for (c = 0; c < sp->colorAttachmentCount && c < NVRM_MAX_RP_ATTACHMENTS; c++) {
         color_refs[s][c].sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
         color_refs[s][c].attachment = sp->pColorAttachments ?
            sp->pColorAttachments[c].attachment : VK_ATTACHMENT_UNUSED;
         color_refs[s][c].layout = sp->pColorAttachments ?
            sp->pColorAttachments[c].layout : VK_IMAGE_LAYOUT_UNDEFINED;
      }
      subs[s].pColorAttachments = color_refs[s];
      if (sp->pDepthStencilAttachment) {
         ds_refs[s].sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
         ds_refs[s].attachment = sp->pDepthStencilAttachment->attachment;
         ds_refs[s].layout = sp->pDepthStencilAttachment->layout;
         subs[s].pDepthStencilAttachment = &ds_refs[s];
      }
   }
   ci2.pSubpasses = subs;
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
   view->aspect_mask = pCreateInfo->subresourceRange.aspectMask;
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
   uint32_t i;

   fb = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*fb), 8,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!fb)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   vk_object_base_init(&device->vk, &fb->base, VK_OBJECT_TYPE_FRAMEBUFFER);
   fb->width = pCreateInfo->width;
   fb->height = pCreateInfo->height;
   fb->layers = pCreateInfo->layers;
   fb->attachment_count = MIN2(pCreateInfo->attachmentCount, NVRM_MAX_RP_ATTACHMENTS);
   for (i = 0; i < fb->attachment_count; i++) {
      if (pCreateInfo->pAttachments)
         fb->views[i] = nvrm_image_view_from_handle(pCreateInfo->pAttachments[i]);
   }
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
nvrm_cmd_emit_vertex_attribs(struct nvrm_cmd_buffer *cmd,
                             struct nvrm_graphics_pipeline *pipe)
{
   uint32_t i, a;
   if (!pipe)
      return;
   /* Disable all attribs first, then enable active ones from pipeline */
   for (i = 0; i < NVRM_MAX_VTX_ATTRIBS; i++)
      nv_3d_set_vertex_attribute(&cmd->push, i, 0, 0, 0, false);
   for (a = 0; a < pipe->attrib_count; a++) {
      const struct nvrm_vtx_attrib *at = &pipe->attribs[a];
      uint32_t loc = at->location < NVRM_MAX_VTX_ATTRIBS ? at->location : a;
      if (!at->active)
         continue;
      nv_3d_set_vertex_attribute(&cmd->push, loc, at->binding, at->offset,
                                 at->component_fmt, true);
   }
   /* Instance frequency for bindings with instance rate */
   for (i = 0; i < pipe->vtx_binding_count; i++) {
      const struct nvrm_vtx_binding_info *bi = &pipe->vtx_bindings[i];
      if (!bi->used || bi->binding >= NVRM_MAX_VTX_BINDINGS)
         continue;
      if (bi->input_rate) {
         /* Frequency divisor = 1 instance per element (advance every instance) */
         nv_push_method(&cmd->push,
                        NVC597_SET_VERTEX_STREAM_A_FREQUENCY(bi->binding), 1);
      }
   }
}

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

   nv_3d_set_primitive_topology(&cmd->push, pipe->topology_nv);
   nv_3d_set_primitive_restart(&cmd->push, pipe->prim_restart_enable ||
                               cmd->prim_restart_enable,
                               cmd->prim_restart_index);

   /* Optional tess/geom: enable only if shaders present, else disable */
   if (pipe->tcs && pipe->tcs->uploaded)
      nv_shader_emit_bind(&cmd->push, pipe->tcs, 0, -1);
   else
      nv_3d_disable_pipeline_shader(&cmd->push, NV_3D_PIPE_STAGE_TESS_INIT);
   if (pipe->tes && pipe->tes->uploaded)
      nv_shader_emit_bind(&cmd->push, pipe->tes, 0, -1);
   else
      nv_3d_disable_pipeline_shader(&cmd->push, NV_3D_PIPE_STAGE_TESS);
   if (pipe->gs && pipe->gs->uploaded)
      nv_shader_emit_bind(&cmd->push, pipe->gs, 0, -1);
   else
      nv_3d_disable_pipeline_shader(&cmd->push, NV_3D_PIPE_STAGE_GEOMETRY);

   region = pipe->program_region_base;
   if (pipe->vs && pipe->vs->uploaded)
      nv_shader_emit_bind(&cmd->push, pipe->vs, region, -1);
   if (pipe->fs && pipe->fs->uploaded)
      nv_shader_emit_bind(&cmd->push, pipe->fs, 0, -1);

   nvrm_cmd_emit_vertex_attribs(cmd, pipe);

   cmd->bound_gfx_pipeline = pipe;
}

/* Upload dirty push constants into GPU CB and bind to VS/FS slot 0. */
static void
nvrm_cmd_emit_push_constants(struct nvrm_cmd_buffer *cmd)
{
   uint64_t addr;
   uint32_t n_dw;
   if (!cmd || !cmd->push_map)
      return;
   if (!cmd->push_const_dirty && !cmd->push_const_dwords)
      return;
   if (!cmd->push_const_bo)
      return;
   addr = nv_rm_bo_gpu_offset(cmd->push_const_bo);
   if (!addr)
      return;
   n_dw = cmd->push_const_dwords ? cmd->push_const_dwords : 1;
   if (n_dw > NVRM_MAX_PUSH_CONST_DWORDS)
      n_dw = NVRM_MAX_PUSH_CONST_DWORDS;
   /* Keep CPU mirror in sync for debugging / host-side reads */
   if (cmd->push_const_map)
      memcpy(cmd->push_const_map, cmd->push_const, n_dw * 4u);
   nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_3D);
   nv_3d_upload_and_bind_push_constants(&cmd->push, addr,
                                        cmd->push_const_bo_size ?
                                        cmd->push_const_bo_size : 256u,
                                        0, cmd->push_const, n_dw);
   cmd->push_const_dirty = false;
}

/* Emit all currently bound descriptor sets (UBOs + tex pools). */
static void
nvrm_cmd_emit_bound_descriptors(struct nvrm_cmd_buffer *cmd,
                                VkPipelineBindPoint bind_point)
{
   uint32_t i, u, dyn_idx = 0;
   (void)bind_point;
   if (!cmd->push_map)
      return;
   nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_3D);
   /* Push constants first so descriptor CBs don't clobber the selector */
   nvrm_cmd_emit_push_constants(cmd);
   for (i = 0; i < cmd->bound_set_count && i < NVRM_MAX_DESC_SETS; i++) {
      struct nvrm_descriptor_set *set = cmd->bound_sets[i];
      if (!set)
         continue;
      for (u = 0; u < set->ubo_count && u < 16; u++) {
         uint64_t addr = 0;
         uint32_t sz = (uint32_t)set->ubo[u].range;
         uint64_t dyn_off = 0;
         if (dyn_idx < cmd->dynamic_offset_count)
            dyn_off = cmd->dynamic_offsets[dyn_idx++];
         if (set->ubo[u].bo)
            addr = nv_rm_bo_gpu_offset(set->ubo[u].bo) + set->ubo[u].offset + dyn_off;
         if (!sz && set->ubo[u].bo)
            sz = (uint32_t)nv_rm_bo_size(set->ubo[u].bo);
         if (sz) {
            nv_3d_set_constant_buffer_selector(&cmd->push, (sz + 255u) & ~255u, addr);
            /* set index i -> bind group; slot u within set */
            nv_3d_bind_group_constant_buffer(&cmd->push, i, u, true);
         }
      }
      for (u = 0; u < set->ssbo_count && u < 16; u++) {
         /* SSBO addresses are consumed by shaders via global loads; record only */
         (void)set->ssbo[u];
      }
      if (set->img_count > 0 && cmd->device && cmd->device->tex_pool)
         nv_tex_pool_emit_bind(&cmd->push, cmd->device->tex_pool);
   }
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

/* Fill nv_3d_surface from image view + render extent / format. */
static void
nvrm_surface_from_view(struct nv_3d_surface *s, struct nvrm_image_view *view,
                       uint32_t w, uint32_t h, bool is_zt)
{
   struct nvrm_image *img;
   memset(s, 0, sizeof(*s));
   if (!view || !view->image)
      return;
   img = view->image;
   s->enabled = true;
   s->gpu_addr = img->gpu_offset ? img->gpu_offset :
                 (img->bo ? nv_rm_bo_gpu_offset(img->bo) : 0);
   s->width = w ? w : img->vk.extent.width;
   s->height = h ? h : img->vk.extent.height;
   s->block_linear = img->is_blocklinear;
   if (is_zt)
      s->format = vk_format_to_zt_format(view->format);
   else
      s->format = vk_format_to_ct_format(view->format);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdBeginRendering(VkCommandBuffer commandBuffer,
                       const VkRenderingInfo *pRenderingInfo)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   const struct nv_device_info *info = cmd->device->info;
   uint32_t class_3d = info ? info->class_3d : 0;
   uint32_t w, h;
   uint8_t targets[8] = {0, 1, 2, 3, 4, 5, 6, 7};
   unsigned i, n_color = 0;
   bool have_zt = false;

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
            nvrm_surface_from_view(&s, view, w, h, false);
            n_color++;
         }
      }
      nv_3d_set_color_target(&cmd->push, i, &s);
   }
   /* Disable unused colour targets */
   for (; i < 8; i++) {
      struct nv_3d_surface s;
      memset(&s, 0, sizeof(s));
      nv_3d_set_color_target(&cmd->push, i, &s);
   }
   if (n_color)
      nv_3d_set_ct_select(&cmd->push, n_color, targets);

   /* Depth and/or stencil attachment (may share image view) */
   if (pRenderingInfo->pDepthAttachment &&
       pRenderingInfo->pDepthAttachment->imageView != VK_NULL_HANDLE) {
      VK_FROM_HANDLE(nvrm_image_view, view,
                     pRenderingInfo->pDepthAttachment->imageView);
      struct nv_3d_surface s;
      nvrm_surface_from_view(&s, view, w, h, true);
      if (s.enabled) {
         nv_3d_set_zeta_target(&cmd->push, &s);
         have_zt = true;
      }
   } else if (pRenderingInfo->pStencilAttachment &&
              pRenderingInfo->pStencilAttachment->imageView != VK_NULL_HANDLE) {
      VK_FROM_HANDLE(nvrm_image_view, view,
                     pRenderingInfo->pStencilAttachment->imageView);
      struct nv_3d_surface s;
      nvrm_surface_from_view(&s, view, w, h, true);
      if (s.enabled) {
         nv_3d_set_zeta_target(&cmd->push, &s);
         have_zt = true;
      }
   }

   nv_3d_set_surface_clip(&cmd->push,
                          pRenderingInfo->renderArea.offset.x,
                          pRenderingInfo->renderArea.offset.y, w, h);
   /* Viewport: scale half-extent, offset centre; Y inverted for GL/Vulkan clip */
   nv_3d_set_viewport0(&cmd->push,
                       (float)w * 0.5f, (float)h * -0.5f, 1.0f,
                       (float)w * 0.5f, (float)h * 0.5f, 0.0f);

   /* Apply scissor from render area */
   nv_3d_set_scissor0(&cmd->push, true,
                      (uint32_t)pRenderingInfo->renderArea.offset.x,
                      (uint32_t)pRenderingInfo->renderArea.offset.y,
                      (uint32_t)(pRenderingInfo->renderArea.offset.x + (int32_t)w),
                      (uint32_t)(pRenderingInfo->renderArea.offset.y + (int32_t)h));

   /* Load ops: clear colour targets */
   for (i = 0; i < pRenderingInfo->colorAttachmentCount && i < 8; i++) {
      const VkRenderingAttachmentInfo *att = &pRenderingInfo->pColorAttachments[i];
      if (att->imageView != VK_NULL_HANDLE &&
          att->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR) {
         const uint32_t *c = att->clearValue.color.uint32;
         /* Multi-target clear: hardware clears all enabled CTs; set values then clear once */
         if (i == 0)
            nv_3d_emit_clear_surface(&cmd->push, 0x10, c, 0.0f, 0);
      }
   }
   if (have_zt) {
      float d = 1.0f;
      uint32_t st = 0;
      uint32_t flags = 0;
      if (pRenderingInfo->pDepthAttachment &&
          pRenderingInfo->pDepthAttachment->imageView != VK_NULL_HANDLE &&
          pRenderingInfo->pDepthAttachment->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR) {
         d = pRenderingInfo->pDepthAttachment->clearValue.depthStencil.depth;
         flags |= 0x100;
      }
      if (pRenderingInfo->pStencilAttachment &&
          pRenderingInfo->pStencilAttachment->imageView != VK_NULL_HANDLE &&
          pRenderingInfo->pStencilAttachment->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR) {
         st = pRenderingInfo->pStencilAttachment->clearValue.depthStencil.stencil;
         flags |= 0x200;
      } else if (pRenderingInfo->pDepthAttachment &&
                 pRenderingInfo->pDepthAttachment->imageView != VK_NULL_HANDLE &&
                 pRenderingInfo->pDepthAttachment->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR) {
         /* Depth-only clear may include stencil in combined formats */
         st = pRenderingInfo->pDepthAttachment->clearValue.depthStencil.stencil;
      }
      if (flags)
         nv_3d_emit_clear_surface(&cmd->push, flags, NULL, d, st);
   }

   cmd->render_width = w;
   cmd->render_height = h;
   cmd->in_render_pass = true;
   cmd->render_color_count = pRenderingInfo->colorAttachmentCount;
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
   VK_FROM_HANDLE(nvrm_render_pass, rp, pRenderPassBegin->renderPass);
   VK_FROM_HANDLE(nvrm_framebuffer, fb, pRenderPassBegin->framebuffer);
   VkRenderingInfo ri;
   VkRenderingAttachmentInfo colors[NVRM_MAX_RP_ATTACHMENTS];
   VkRenderingAttachmentInfo depth_att, stencil_att;
   const struct nvrm_rp_subpass *sp;
   uint32_t i, n_color = 0;
   (void)pSubpassBeginInfo;

   memset(&ri, 0, sizeof(ri));
   ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
   ri.renderArea = pRenderPassBegin->renderArea;
   ri.layerCount = fb ? fb->layers : 1;
   if (!ri.layerCount)
      ri.layerCount = 1;

   memset(colors, 0, sizeof(colors));
   memset(&depth_att, 0, sizeof(depth_att));
   memset(&stencil_att, 0, sizeof(stencil_att));

   sp = (rp && rp->subpass_count) ? &rp->subpasses[0] : NULL;
   if (sp && fb) {
      for (i = 0; i < sp->color_count && n_color < NVRM_MAX_RP_ATTACHMENTS; i++) {
         uint32_t ref = sp->color_refs[i];
         if (ref == VK_ATTACHMENT_UNUSED || ref >= fb->attachment_count)
            continue;
         colors[n_color].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
         colors[n_color].imageView = nvrm_image_view_to_handle(fb->views[ref]);
         if (rp && ref < rp->attachment_count)
            colors[n_color].loadOp = rp->atts[ref].load_op;
         else
            colors[n_color].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
         if (pRenderPassBegin->clearValueCount > ref &&
             pRenderPassBegin->pClearValues &&
             colors[n_color].loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR)
            colors[n_color].clearValue = pRenderPassBegin->pClearValues[ref];
         n_color++;
      }
      if (sp->depth_stencil_ref >= 0 &&
          (uint32_t)sp->depth_stencil_ref < fb->attachment_count) {
         uint32_t ref = (uint32_t)sp->depth_stencil_ref;
         depth_att.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
         depth_att.imageView = nvrm_image_view_to_handle(fb->views[ref]);
         if (rp && ref < rp->attachment_count) {
            depth_att.loadOp = rp->atts[ref].load_op;
            stencil_att.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            stencil_att.imageView = depth_att.imageView;
            stencil_att.loadOp = rp->atts[ref].stencil_load_op;
            if (pRenderPassBegin->clearValueCount > ref &&
                pRenderPassBegin->pClearValues) {
               if (depth_att.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR)
                  depth_att.clearValue = pRenderPassBegin->pClearValues[ref];
               if (stencil_att.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR)
                  stencil_att.clearValue = pRenderPassBegin->pClearValues[ref];
            }
            ri.pStencilAttachment = &stencil_att;
         }
         ri.pDepthAttachment = &depth_att;
      }
   } else if (pRenderPassBegin->clearValueCount && pRenderPassBegin->pClearValues) {
      /* Minimal fallback: single colour clear without FB views */
      colors[0].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
      colors[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      colors[0].clearValue = pRenderPassBegin->pClearValues[0];
      n_color = 1;
   }

   ri.colorAttachmentCount = n_color;
   ri.pColorAttachments = n_color ? colors : NULL;
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
   uint32_t topo = NVC597_TOPOLOGY_TRIANGLES;
   if (!cmd->push_map)
      return;
   if (cmd->bound_gfx_pipeline) {
      nvrm_cmd_emit_pipeline_state(cmd, cmd->bound_gfx_pipeline);
      topo = cmd->bound_gfx_pipeline->topology_nv;
   }
   nvrm_cmd_emit_bound_descriptors(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS);
   nvrm_cmd_emit_push_constants(cmd);
   if (!instanceCount)
      instanceCount = 1;
   nv_3d_emit_draw_vertex_array_instanced(&cmd->push, topo, firstVertex,
                                          vertexCount, instanceCount,
                                          firstInstance);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount,
                    uint32_t instanceCount, uint32_t firstIndex,
                    int32_t vertexOffset, uint32_t firstInstance)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   uint32_t topo = NVC597_TOPOLOGY_TRIANGLES;
   if (!cmd->push_map)
      return;
   if (cmd->bound_gfx_pipeline) {
      nvrm_cmd_emit_pipeline_state(cmd, cmd->bound_gfx_pipeline);
      topo = cmd->bound_gfx_pipeline->topology_nv;
   }
   if (cmd->index_valid) {
      nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_3D);
      nv_3d_set_index_buffer(&cmd->push, cmd->index_addr, cmd->index_size,
                             cmd->index_type_size);
   }
   nvrm_cmd_emit_bound_descriptors(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS);
   nvrm_cmd_emit_push_constants(cmd);
   if (!instanceCount)
      instanceCount = 1;
   nv_3d_emit_draw_index_buffer_instanced(&cmd->push, topo, firstIndex,
                                          indexCount, vertexOffset,
                                          instanceCount, firstInstance);
}

/*
 * Indirect draws (VkDrawIndirectCommand / VkDrawIndexedIndirectCommand).
 *
 * Path A: indirect BO is host-mappable — read each command at record time and
 * emit normal instanced draws (correct and matches compute indirect path A).
 *
 * Path B: GPU-only BO — CE-copy indirect records into cmd->indirect_shadow_bo
 * (host-mappable), then read counts from the shadow.  Requires the shadow to
 * have been populated by a prior submit when the indirect BO was written on
 * the GPU; at pure record time without GPU execution the shadow may be stale
 * (zero counts).  Still better than always emitting zero when map fails.
 */

/* Path B: if indirect BO is not host-mappable, CE-copy into indirect_shadow_bo
 * and return a CPU pointer into the shadow map (or NULL if shadow unavailable).
 * Emits CE methods into the command buffer; caller must not unmap shadow BO. */
static const uint32_t *
nvrm_indirect_path_b_shadow(struct nvrm_cmd_buffer *cmd,
                            struct nvrm_buffer *buf,
                            VkDeviceSize offset,
                            uint32_t need_bytes,
                            void **unmap_cookie)
{
   const struct nv_device_info *info;
   uint64_t src_addr, dst_addr;
   uint32_t class_copy;
   uint32_t copy_size;

   *unmap_cookie = NULL;
   if (!cmd || !buf || !buf->bo || !cmd->indirect_shadow_bo ||
       !cmd->indirect_shadow_map || !need_bytes)
      return NULL;

   copy_size = need_bytes;
   if (copy_size > cmd->indirect_shadow_bo_size)
      copy_size = cmd->indirect_shadow_bo_size;

   info = cmd->device ? cmd->device->info : NULL;
   class_copy = info ? info->class_copy : 0;
   src_addr = nv_rm_bo_gpu_offset(buf->bo) + (uint64_t)offset;
   dst_addr = nv_rm_bo_gpu_offset(cmd->indirect_shadow_bo);

   nv_copy_indirect_to_shadow(&cmd->push, class_copy, src_addr, dst_addr,
                              copy_size);
   /* Shadow is persistently mapped; no unmap on caller side */
   return (const uint32_t *)cmd->indirect_shadow_map;
}

static const uint32_t *
nvrm_try_map_indirect_u32(struct nvrm_buffer *buf, VkDeviceSize offset,
                          uint32_t need_bytes, void **unmap_cookie)
{
   void *map;
   *unmap_cookie = NULL;
   if (!buf || !buf->bo)
      return NULL;
   map = nv_rm_bo_map(buf->bo);
   if (!map)
      return NULL;
   *unmap_cookie = buf->bo;
   return (const uint32_t *)((const uint8_t *)map + offset);
}

static void
nvrm_unmap_indirect(void *unmap_cookie)
{
   if (unmap_cookie)
      nv_rm_bo_unmap((struct nv_rm_bo *)unmap_cookie);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer,
                     VkDeviceSize offset, uint32_t drawCount, uint32_t stride)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvrm_buffer, buf, buffer);
   uint32_t d;
   uint32_t topo = NVC597_TOPOLOGY_TRIANGLES;
   uint32_t rec_stride = stride ? stride : NV_VK_DRAW_INDIRECT_STRIDE_DEFAULT;
   void *unmap = NULL;
   const uint32_t *base = NULL;

   if (!cmd->push_map || !buf || !drawCount)
      return;
   if (cmd->bound_gfx_pipeline) {
      nvrm_cmd_emit_pipeline_state(cmd, cmd->bound_gfx_pipeline);
      topo = cmd->bound_gfx_pipeline->topology_nv;
   }
   nvrm_cmd_emit_push_constants(cmd);
   nvrm_cmd_emit_bound_descriptors(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS);

   base = nvrm_try_map_indirect_u32(buf, offset,
                                    rec_stride * drawCount, &unmap);
   if (!base && buf)
      base = nvrm_indirect_path_b_shadow(cmd, buf, offset,
                                         rec_stride * drawCount, &unmap);
   for (d = 0; d < drawCount; d++) {
      uint32_t vertex_count = 0, instance_count = 1;
      uint32_t first_vertex = 0, first_instance = 0;
      if (base) {
         const uint32_t *rec = (const uint32_t *)((const uint8_t *)base +
                                                  (size_t)d * rec_stride);
         /* VkDrawIndirectCommand: vertexCount, instanceCount, firstVertex, firstInstance */
         vertex_count = rec[0];
         instance_count = rec[1] ? rec[1] : 1;
         first_vertex = rec[2];
         first_instance = rec[3];
      }
      nv_3d_emit_draw_vertex_array_instanced(&cmd->push, topo, first_vertex,
                                             vertex_count, instance_count,
                                             first_instance);
   }
   nvrm_unmap_indirect(unmap);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer,
                            VkDeviceSize offset, uint32_t drawCount,
                            uint32_t stride)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvrm_buffer, buf, buffer);
   uint32_t d;
   uint32_t topo = NVC597_TOPOLOGY_TRIANGLES;
   uint32_t rec_stride = stride ? stride : NV_VK_DRAWINDEXED_INDIRECT_STRIDE_DEFAULT;
   void *unmap = NULL;
   const uint32_t *base = NULL;

   if (!cmd->push_map || !drawCount)
      return;
   if (cmd->bound_gfx_pipeline) {
      nvrm_cmd_emit_pipeline_state(cmd, cmd->bound_gfx_pipeline);
      topo = cmd->bound_gfx_pipeline->topology_nv;
   }
   if (cmd->index_valid) {
      nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_3D);
      nv_3d_set_index_buffer(&cmd->push, cmd->index_addr, cmd->index_size,
                             cmd->index_type_size);
   }
   nvrm_cmd_emit_push_constants(cmd);
   nvrm_cmd_emit_bound_descriptors(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS);

   if (buf)
      base = nvrm_try_map_indirect_u32(buf, offset,
                                       rec_stride * drawCount, &unmap);
   if (!base && buf)
      base = nvrm_indirect_path_b_shadow(cmd, buf, offset,
                                         rec_stride * drawCount, &unmap);
   for (d = 0; d < drawCount; d++) {
      uint32_t index_count = 0, instance_count = 1;
      uint32_t first_index = 0, first_instance = 0;
      int32_t vertex_offset = 0;
      if (base) {
         const uint32_t *rec = (const uint32_t *)((const uint8_t *)base +
                                                  (size_t)d * rec_stride);
         /* VkDrawIndexedIndirectCommand */
         index_count = rec[0];
         instance_count = rec[1] ? rec[1] : 1;
         first_index = rec[2];
         vertex_offset = (int32_t)rec[3];
         first_instance = rec[4];
      }
      nv_3d_emit_draw_index_buffer_instanced(&cmd->push, topo, first_index,
                                             index_count, vertex_offset,
                                             instance_count, first_instance);
   }
   nvrm_unmap_indirect(unmap);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdSetPrimitiveRestartEnable(VkCommandBuffer commandBuffer,
                                  VkBool32 primitiveRestartEnable)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   cmd->prim_restart_enable = primitiveRestartEnable;
   if (cmd->push_map)
      nv_3d_set_primitive_restart(&cmd->push, primitiveRestartEnable,
                                  cmd->prim_restart_index);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport,
                    uint32_t viewportCount, const VkViewport *pViewports)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   if (!viewportCount || !pViewports || firstViewport > 0)
      return;
   cmd->vp_x = pViewports[0].x;
   cmd->vp_y = pViewports[0].y;
   cmd->vp_w = pViewports[0].width;
   cmd->vp_h = pViewports[0].height;
   cmd->vp_min_z = pViewports[0].minDepth;
   cmd->vp_max_z = pViewports[0].maxDepth;
   cmd->vp_valid = true;
   if (cmd->push_map) {
      /* Vulkan viewport -> NV scale/offset: scale = half extent, offset = centre */
      float sx = cmd->vp_w * 0.5f;
      float sy = cmd->vp_h * 0.5f;
      float sz = cmd->vp_max_z - cmd->vp_min_z;
      float ox = cmd->vp_x + sx;
      float oy = cmd->vp_y + sy;
      float oz = cmd->vp_min_z;
      nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_3D);
      nv_3d_set_viewport0(&cmd->push, sx, sy, sz, ox, oy, oz);
   }
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor,
                   uint32_t scissorCount, const VkRect2D *pScissors)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   if (!scissorCount || !pScissors || firstScissor > 0)
      return;
   cmd->sc_x = pScissors[0].offset.x;
   cmd->sc_y = pScissors[0].offset.y;
   cmd->sc_w = pScissors[0].extent.width;
   cmd->sc_h = pScissors[0].extent.height;
   cmd->sc_valid = true;
   if (cmd->push_map) {
      nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_3D);
      nv_3d_set_scissor0(&cmd->push, true,
                         (uint32_t)cmd->sc_x,
                         (uint32_t)(cmd->sc_x + (int32_t)cmd->sc_w),
                         (uint32_t)cmd->sc_y,
                         (uint32_t)(cmd->sc_y + (int32_t)cmd->sc_h));
   }
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout,
                      VkShaderStageFlags stageFlags, uint32_t offset,
                      uint32_t size, const void *pValues)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   uint32_t start_dw, n_dw, i;
   const uint32_t *src = pValues;
   (void)layout;
   (void)stageFlags;
   if (!pValues || !size)
      return;
   start_dw = offset / 4;
   n_dw = (size + 3) / 4;
   for (i = 0; i < n_dw && (start_dw + i) < NVRM_MAX_PUSH_CONST_DWORDS; i++)
      cmd->push_const[start_dw + i] = src[i];
   if (start_dw + n_dw > cmd->push_const_dwords)
      cmd->push_const_dwords = start_dw + n_dw;
   if (cmd->push_const_dwords > NVRM_MAX_PUSH_CONST_DWORDS)
      cmd->push_const_dwords = NVRM_MAX_PUSH_CONST_DWORDS;
   cmd->push_const_dirty = true;
   /* Eager upload so subsequent draws/dispatches see constants without
    * requiring another descriptor bind. */
   if (cmd->push_map)
      nvrm_cmd_emit_push_constants(cmd);
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
   struct nvrm_graphics_pipeline *pipe = cmd->bound_gfx_pipeline;
   if (!cmd->push_map)
      return;
   nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_3D);
   for (i = 0; i < bindingCount; i++) {
      VK_FROM_HANDLE(nvrm_buffer, buf, pBuffers[i]);
      uint64_t addr = 0;
      uint32_t size = 0;
      uint32_t stride = pStrides ? (uint32_t)pStrides[i] : 0;
      uint32_t slot = firstBinding + i;
      /* Fall back to pipeline-captured stride when dynamic stride omitted */
      if (!stride && pipe) {
         uint32_t b;
         for (b = 0; b < pipe->vtx_binding_count; b++) {
            if (pipe->vtx_bindings[b].binding == slot) {
               stride = pipe->vtx_bindings[b].stride;
               break;
            }
         }
      }
      if (!stride)
         stride = 12;
      if (buf) {
         addr = (buf->addr ? buf->addr : (buf->bo ? nv_rm_bo_gpu_offset(buf->bo) : 0))
                + pOffsets[i];
         if (pSizes && pSizes[i] != VK_WHOLE_SIZE)
            size = (uint32_t)pSizes[i];
         else if (buf->bo)
            size = (uint32_t)nv_rm_bo_size(buf->bo);
      }
      if (slot < NVRM_MAX_VTX_BINDINGS) {
         cmd->vtx_binding[slot].addr = addr;
         cmd->vtx_binding[slot].size = size;
         cmd->vtx_binding[slot].stride = stride;
         cmd->vtx_binding[slot].valid = (buf != NULL);
         nv_3d_set_vertex_stream(&cmd->push, slot, addr, size, stride);
      }
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
   cmd->index_addr = addr;
   cmd->index_size = sz;
   cmd->index_type_size = (uint8_t)isz;
   cmd->index_valid = (buf != NULL);
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
   uint32_t i;
   (void)layout;
   if (!cmd->push_map)
      return;

   /* Cache dynamic offsets for later emit (graphics draw / compute dispatch) */
   cmd->dynamic_offset_count = 0;
   if (pDynamicOffsets && dynamicOffsetCount) {
      uint32_t n = dynamicOffsetCount < 32 ? dynamicOffsetCount : 32;
      memcpy(cmd->dynamic_offsets, pDynamicOffsets, n * sizeof(uint32_t));
      cmd->dynamic_offset_count = n;
   }

   for (i = 0; i < descriptorSetCount; i++) {
      VK_FROM_HANDLE(nvrm_descriptor_set, set, pDescriptorSets[i]);
      uint32_t slot = firstSet + i;
      if (slot < NVRM_MAX_DESC_SETS) {
         cmd->bound_sets[slot] = set;
         if (slot + 1 > cmd->bound_set_count)
            cmd->bound_set_count = slot + 1;
      }
   }

   nvrm_cmd_emit_bound_descriptors(cmd, pipelineBindPoint);
}




/* NVC3C0 QMD-based compute dispatch (clc3c0 / clc3c0qmd v02.02). */
/* Fallback fixed LMEM BO when SM count / local req unknown at begin time */
#ifndef NVRM_LMEM_SCRATCH_SIZE
#define NVRM_LMEM_SCRATCH_SIZE (256 * 1024)
#endif

static void
nvrm_ensure_compute_lmem(struct nvrm_cmd_buffer *cmd)
{
   const struct nv_device_info *info;
   uint64_t lmem_addr;
   uint32_t sm_count = 1;
   uint32_t local_req = 0;
   struct nvrm_compute_pipeline *cp;

   if (!cmd || cmd->lmem_programmed || !cmd->lmem_bo)
      return;
   info = cmd->device ? cmd->device->info : NULL;
   if (info && info->tpc_count)
      sm_count = info->tpc_count;
   /* Prefer per-thread requirement from bound compute pipeline / shader */
   cp = cmd->bound_compute_pipeline;
   if (cp && cp->local_mem_bytes)
      local_req = cp->local_mem_bytes;
   else if (cp && cp->cs && cp->cs->local_mem_size)
      local_req = cp->cs->local_mem_size;
   else if (cmd->lmem_local_req)
      local_req = cmd->lmem_local_req;
   cmd->lmem_local_req = local_req;

   lmem_addr = nv_rm_bo_gpu_offset(cmd->lmem_bo);
   if (!lmem_addr)
      return;
   /* Program per-SM window sized from local_req; BO must cover sm_count * per_sm */
   nv_compute_set_shader_local_memory_for_shader(&cmd->push, lmem_addr,
                                                 local_req, sm_count);
   cmd->lmem_programmed = true;
}

static void
nvrm_fill_compute_desc(struct nvrm_cmd_buffer *cmd, struct nv_qmd_desc *desc,
                       uint32_t gx, uint32_t gy, uint32_t gz,
                       uint32_t *class_compute_out)
{
   struct nvrm_compute_pipeline *cp;
   struct nv_shader *cs;
   const struct nv_device_info *info;
   uint32_t class_compute = 0;
   uint64_t prog = 0;
   uint32_t regs = 16;
   uint32_t cta_x = 1, cta_y = 1, cta_z = 1;
   uint32_t shared = 0;
   uint32_t local_spill = 0;
   uint8_t sass_ver = 0x50;

   memset(desc, 0, sizeof(*desc));
   if (!cmd)
      return;

   cp = cmd->bound_compute_pipeline;
   cs = cp ? cp->cs : NULL;
   info = cmd->device ? cmd->device->info : NULL;
   if (info) {
      class_compute = info->class_compute;
      if (info->sm_version)
         sass_ver = (uint8_t)(info->sm_version & 0xff);
   }
   if (class_compute_out)
      *class_compute_out = class_compute;

   if (cs) {
      prog = cs->code_gpu_addr;
      if (cs->register_count)
         regs = cs->register_count;
      if (cs->local_mem_size)
         local_spill = cs->local_mem_size;
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

   desc->program_addr = prog;
   desc->program_offset = 0;
   desc->grid_x = gx;
   desc->grid_y = gy;
   desc->grid_z = gz;
   desc->cta_x = cta_x;
   desc->cta_y = cta_y;
   desc->cta_z = cta_z;
   desc->register_count = regs;
   desc->shared_mem_size = shared;
   desc->local_mem_low = local_spill;
   desc->sass_version = sass_ver;
   desc->sm_global_caching = true;
   desc->invalidate_caches = !cmd->compute_init_done;

   if (cs && cs->const_gpu_addr && cs->const_size) {
      desc->cb_addr[0] = cs->const_gpu_addr;
      desc->cb_size[0] = cs->const_size;
      desc->cb_valid_mask = 0x1;
   }
}

static void
nvrm_emit_compute_dispatch(struct nvrm_cmd_buffer *cmd,
                           uint32_t gx, uint32_t gy, uint32_t gz)
{
   struct nv_qmd_desc desc;
   uint32_t class_compute = 0;
   uint64_t qmd_addr = 0;
   void *qmd_host = NULL;

   if (!cmd || !cmd->push_map)
      return;

   nvrm_ensure_compute_lmem(cmd);
   nvrm_fill_compute_desc(cmd, &desc, gx, gy, gz, &class_compute);

   if (cmd->qmd_bo) {
      qmd_addr = nv_rm_bo_gpu_offset(cmd->qmd_bo);
      qmd_host = nv_rm_bo_map(cmd->qmd_bo);
   } else if (cmd->push_bo) {
      qmd_addr = nv_rm_bo_gpu_offset(cmd->push_bo);
   }

   /* Materialize QMD into scratch BO (host mirror) + inline load + SEND_PCAS */
   nv_compute_emit_dispatch_materialized(&cmd->push, &desc, qmd_addr,
                                         qmd_host, class_compute);
   if (qmd_host && cmd->qmd_bo)
      nv_rm_bo_unmap(cmd->qmd_bo);

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

/*
 * CmdDispatchIndirect: VkDispatchIndirectCommand is {x,y,z} uint32 at offset
 * in buffer.
 *
 * Path A (host-readable indirect BO): read grid at record time, materialize
 * full QMD into cmd->qmd_bo with correct CTA_RASTER_*, then SEND_PCAS.
 *
 * Path B (GPU-only indirect BO): materialize QMD with placeholder 1x1x1 grid
 * into qmd_bo (host mirror + inline load), then CE-copy 12 bytes from the
 * indirect buffer into QMD dwords 12..14 (CTA_RASTER width/height/depth),
 * then SEND_PCAS.  Requires qmd_bo + indirect buffer GPU VAs.
 */
VKAPI_ATTR void VKAPI_CALL
nvrm_CmdDispatchIndirect(VkCommandBuffer commandBuffer,
                         VkBuffer buffer, VkDeviceSize offset)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvrm_buffer, buf, buffer);
   struct nv_qmd_desc desc;
   const struct nv_device_info *info;
   uint32_t class_compute = 0;
   uint32_t class_copy = 0;
   uint32_t gx = 1, gy = 1, gz = 1;
   uint64_t qmd_addr = 0;
   uint64_t indirect_addr = 0;
   void *qmd_host = NULL;
   bool have_grid = false;
   uint32_t qmd_tmp[NV_QMD_DWORDS];

   if (!cmd || !cmd->push_map)
      return;

   nvrm_ensure_compute_lmem(cmd);
   info = cmd->device ? cmd->device->info : NULL;
   if (info)
      class_copy = info->class_copy;

   if (buf && buf->bo) {
      indirect_addr = nv_rm_bo_gpu_offset(buf->bo) + (uint64_t)offset;
      void *map = nv_rm_bo_map(buf->bo);
      if (map) {
         const uint32_t *icmd =
            (const uint32_t *)((const uint8_t *)map + (size_t)offset);
         gx = icmd[0] ? icmd[0] : 1;
         gy = icmd[1] ? icmd[1] : 1;
         gz = icmd[2] ? icmd[2] : 1;
         have_grid = true;
         nv_rm_bo_unmap(buf->bo);
      }
   }

   nvrm_fill_compute_desc(cmd, &desc, gx, gy, gz, &class_compute);

   if (cmd->qmd_bo) {
      qmd_addr = nv_rm_bo_gpu_offset(cmd->qmd_bo);
      qmd_host = nv_rm_bo_map(cmd->qmd_bo);
   } else if (cmd->push_bo) {
      qmd_addr = nv_rm_bo_gpu_offset(cmd->push_bo);
   }

   if (have_grid) {
      /* Path A: grid known on CPU */
      nv_compute_emit_dispatch_materialized(&cmd->push, &desc, qmd_addr,
                                            qmd_host, class_compute);
   } else if (qmd_addr && indirect_addr) {
      /* Path B: materialize placeholder QMD, CE-patch grid from indirect BO */
      nv_qmd_materialize(&desc, qmd_tmp, qmd_host);
      nv_copy_patch_qmd_grid_from_indirect(&cmd->push, class_copy,
                                           indirect_addr, qmd_addr);
      if (class_compute)
         nv_compute_set_object(&cmd->push, class_compute);
      else
         nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_COMPUTE);
      nv_compute_emit_inline_qmd_launch(&cmd->push, qmd_addr, qmd_tmp, true);
   } else {
      nv_compute_emit_dispatch(&cmd->push, &desc, qmd_addr, class_compute);
   }

   if (qmd_host && cmd->qmd_bo)
      nv_rm_bo_unmap(cmd->qmd_bo);

   cmd->compute_init_done = true;
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
