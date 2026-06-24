/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Gallium pipe_screen for NVIDIA GPUs via the proprietary kernel module RM.
 */

#include "nvgpu_screen.h"
#include "nvgpu_context.h"
#include "nvgpu_resource.h"

#include "nv_rm.h"
#include "nv_device_info.h"
#include "nv_fence.h"

#include "util/format/u_format.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_screen.h"
#include "util/u_string.h"
#include "frontend/drm_driver.h"

#include <stdio.h>
#include <xf86drm.h>

static const char *
nvgpu_get_vendor(struct pipe_screen *pscreen)
{
   return "NVIDIA (open Mesa)";
}

static const char *
nvgpu_get_device_vendor(struct pipe_screen *pscreen)
{
   return "NVIDIA Corporation";
}

static const char *
nvgpu_get_name(struct pipe_screen *pscreen)
{
   struct nvgpu_screen *screen = nvgpu_screen(pscreen);
   if (screen->info && screen->info->name[0])
      return screen->info->name;
   return "NVIDIA GPU";
}

static int
nvgpu_get_param(struct pipe_screen *pscreen, enum pipe_cap param)
{
   struct nvgpu_screen *screen = nvgpu_screen(pscreen);
   const struct nv_device_info *info = screen->info;

   switch (param) {
   case PIPE_CAP_NPOT_TEXTURES:
   case PIPE_CAP_MIXED_FRAMEBUFFER_SIZES:
   case PIPE_CAP_MIXED_COLOR_DEPTH_BITS:
   case PIPE_CAP_ANISOTROPIC_FILTER:
   case PIPE_CAP_OCCLUSION_QUERY:
   case PIPE_CAP_TEXTURE_MULTISAMPLE:
   case PIPE_CAP_TEXTURE_SWIZZLE:
   case PIPE_CAP_DEPTH_CLIP_DISABLE:
   case PIPE_CAP_FRAGMENT_SHADER_TEXTURE_LOD:
   case PIPE_CAP_FRAGMENT_SHADER_DERIVATIVES:
   case PIPE_CAP_VERTEX_COLOR_UNCLAMPED:
   case PIPE_CAP_TGSI_TEXCOORD:
   case PIPE_CAP_TEXTURE_BARRIER:
   case PIPE_CAP_COMPUTE:
   case PIPE_CAP_SEAMLESS_CUBE_MAP:
   case PIPE_CAP_CUBE_MAP_ARRAY:
   case PIPE_CAP_TEXTURE_BUFFER_OBJECTS:
   case PIPE_CAP_QUERY_TIMESTAMP:
   case PIPE_CAP_QUERY_TIME_ELAPSED:
   case PIPE_CAP_CONDITIONAL_RENDER:
   case PIPE_CAP_TEXTURE_QUERY_LOD:
   case PIPE_CAP_SAMPLE_SHADING:
   case PIPE_CAP_DRAW_INDIRECT:
   case PIPE_CAP_MULTI_DRAW_INDIRECT:
   case PIPE_CAP_TIMER_QUERY:
   case PIPE_CAP_STREAM_OUTPUT_PAUSE_RESUME:
   case PIPE_CAP_TEXTURE_MIRROR_CLAMP_TO_EDGE:
   case PIPE_CAP_TGSI_VS_LAYER_VIEWPORT:
   case PIPE_CAP_TGSI_TES_LAYER_VIEWPORT:
   case PIPE_CAP_QUERY_PIPELINE_STATISTICS:
   case PIPE_CAP_TEXTURE_BORDER_COLOR_QUIRK:
   case PIPE_CAP_PREFER_IMM_ARRAYS_AS_CONSTBUF:
      return 1;

   case PIPE_CAP_MAX_RENDER_TARGETS:
      return 8;
   case PIPE_CAP_MAX_DUAL_SOURCE_RENDER_TARGETS:
      return 1;
   case PIPE_CAP_MAX_TEXTURE_2D_SIZE:
      return 16384;
   case PIPE_CAP_MAX_TEXTURE_3D_LEVELS:
      return 12; /* 2048 */
   case PIPE_CAP_MAX_TEXTURE_CUBE_LEVELS:
      return 15;
   case PIPE_CAP_MAX_TEXTURE_ARRAY_LAYERS:
      return 2048;
   case PIPE_CAP_MAX_STREAM_OUTPUT_BUFFERS:
      return 4;
   case PIPE_CAP_MAX_STREAM_OUTPUT_SEPARATE_COMPONENTS:
   case PIPE_CAP_MAX_STREAM_OUTPUT_INTERLEAVED_COMPONENTS:
      return 128;
   case PIPE_CAP_MAX_GEOMETRY_OUTPUT_VERTICES:
      return 1024;
   case PIPE_CAP_MAX_GEOMETRY_TOTAL_OUTPUT_COMPONENTS:
      return 1024;
   case PIPE_CAP_MAX_VERTEX_ATTRIB_STRIDE:
      return 2048;
   case PIPE_CAP_MAX_VIEWPORTS:
      return 16;
   case PIPE_CAP_ENDIANNESS:
      return PIPE_ENDIAN_LITTLE;
   case PIPE_CAP_MIN_TEXTURE_GATHER_OFFSET:
      return -32;
   case PIPE_CAP_MAX_TEXTURE_GATHER_OFFSET:
      return 31;
   case PIPE_CAP_VENDOR_ID:
      return 0x10de;
   case PIPE_CAP_DEVICE_ID:
      return info ? (int)info->pci_device_id : 0xffff;
   case PIPE_CAP_VIDEO_MEMORY:
      if (info)
         return (int)(info->vram_size_bytes / (1024 * 1024));
      return 0;
   case PIPE_CAP_UMA:
      return 0;
   case PIPE_CAP_ACCELERATED:
      return 1;
   case PIPE_CAP_GRAPHICS:
      return info ? info->has_graphics : 1;
   case PIPE_CAP_MIN_MAP_BUFFER_ALIGNMENT:
      return 64;
   case PIPE_CAP_MAX_TEXEL_BUFFER_ELEMENTS_UINT:
      return 134217728;
   case PIPE_CAP_MAX_CONSTANT_BUFFER_SIZE_UINT:
      return 65536;
   case PIPE_CAP_MAX_TEXTURE_BUFFER_SIZE_UINT:
      return 134217728;
   case PIPE_CAP_MAX_SHADER_BUFFER_SIZE_UINT:
      return 1 << 27;
   case PIPE_CAP_MAX_VARYINGS:
      return 31;
   case PIPE_CAP_PREFERRED_FOR_TEXTURE_UPLOAD:
      return 1;
   case PIPE_CAP_SHAREABLE_SHADERS:
      return 1;
   case PIPE_CAP_PCI_GROUP:
      return info ? (int)info->pci_domain : 0;
   case PIPE_CAP_PCI_BUS:
      return info ? (int)info->pci_bus : 0;
   case PIPE_CAP_PCI_DEVICE:
      return info ? (int)info->pci_dev : 0;
   case PIPE_CAP_PCI_FUNCTION:
      return info ? (int)info->pci_func : 0;
   default:
      return u_pipe_screen_get_param_defaults(pscreen, param);
   }
}

static float
nvgpu_get_paramf(struct pipe_screen *pscreen, enum pipe_capf param)
{
   switch (param) {
   case PIPE_CAPF_MIN_LINE_WIDTH:
   case PIPE_CAPF_MIN_LINE_WIDTH_AA:
   case PIPE_CAPF_MIN_POINT_SIZE:
   case PIPE_CAPF_MIN_POINT_SIZE_AA:
      return 1.0f;
   case PIPE_CAPF_MAX_LINE_WIDTH:
   case PIPE_CAPF_MAX_LINE_WIDTH_AA:
      return 10.0f;
   case PIPE_CAPF_MAX_POINT_SIZE:
   case PIPE_CAPF_MAX_POINT_SIZE_AA:
      return 2048.0f;
   case PIPE_CAPF_MAX_TEXTURE_ANISOTROPY:
      return 16.0f;
   case PIPE_CAPF_MAX_TEXTURE_LOD_BIAS:
      return 15.0f;
   case PIPE_CAPF_MIN_CONSERVATIVE_RASTER_DILATE:
   case PIPE_CAPF_MAX_CONSERVATIVE_RASTER_DILATE:
   case PIPE_CAPF_CONSERVATIVE_RASTER_DILATE_GRANULARITY:
      return 0.0f;
   }
   return 0.0f;
}

static int
nvgpu_get_shader_param(struct pipe_screen *pscreen,
                       enum pipe_shader_type shader,
                       enum pipe_shader_cap param)
{
   switch (shader) {
   case PIPE_SHADER_VERTEX:
   case PIPE_SHADER_FRAGMENT:
   case PIPE_SHADER_COMPUTE:
      break;
   case PIPE_SHADER_GEOMETRY:
   case PIPE_SHADER_TESS_CTRL:
   case PIPE_SHADER_TESS_EVAL:
      /* Advertise with conservative limits; compiler will refine later */
      break;
   default:
      return 0;
   }

   switch (param) {
   case PIPE_SHADER_CAP_MAX_INSTRUCTIONS:
   case PIPE_SHADER_CAP_MAX_ALU_INSTRUCTIONS:
   case PIPE_SHADER_CAP_MAX_TEX_INSTRUCTIONS:
   case PIPE_SHADER_CAP_MAX_TEX_INDIRECTIONS:
      return 16384;
   case PIPE_SHADER_CAP_MAX_CONTROL_FLOW_DEPTH:
      return 8;
   case PIPE_SHADER_CAP_MAX_INPUTS:
      return shader == PIPE_SHADER_VERTEX ? 32 : 32;
   case PIPE_SHADER_CAP_MAX_OUTPUTS:
      return 32;
   case PIPE_SHADER_CAP_MAX_CONST_BUFFER0_SIZE:
      return 65536;
   case PIPE_SHADER_CAP_MAX_CONST_BUFFERS:
      return 16;
   case PIPE_SHADER_CAP_MAX_TEMPS:
      return 256;
   case PIPE_SHADER_CAP_INDIRECT_INPUT_ADDR:
   case PIPE_SHADER_CAP_INDIRECT_OUTPUT_ADDR:
   case PIPE_SHADER_CAP_INDIRECT_TEMP_ADDR:
   case PIPE_SHADER_CAP_INDIRECT_CONST_ADDR:
      return 1;
   case PIPE_SHADER_CAP_INTEGERS:
      return 1;
   case PIPE_SHADER_CAP_MAX_TEXTURE_SAMPLERS:
   case PIPE_SHADER_CAP_MAX_SAMPLER_VIEWS:
      return 32;
   case PIPE_SHADER_CAP_MAX_SHADER_BUFFERS:
      return 16;
   case PIPE_SHADER_CAP_MAX_SHADER_IMAGES:
      return 16;
   case PIPE_SHADER_CAP_SUPPORTED_IRS:
      return (1 << PIPE_SHADER_IR_NIR);
   default:
      return 0;
   }
}

static int
nvgpu_get_compute_param(struct pipe_screen *pscreen,
                        enum pipe_shader_ir ir_type,
                        enum pipe_compute_cap param,
                        void *ret)
{
   struct nvgpu_screen *screen = nvgpu_screen(pscreen);
   const struct nv_device_info *info = screen->info;

   switch (param) {
   case PIPE_COMPUTE_CAP_ADDRESS_BITS:
      if (ret) *(uint32_t *)ret = 64;
      return sizeof(uint32_t);
   case PIPE_COMPUTE_CAP_IR_TARGET:
      if (ret) snprintf((char *)ret, 8, "nv");
      return 3;
   case PIPE_COMPUTE_CAP_GRID_DIMENSION:
      if (ret) *(uint64_t *)ret = 3;
      return sizeof(uint64_t);
   case PIPE_COMPUTE_CAP_MAX_GRID_SIZE:
      if (ret) {
         uint64_t *v = ret;
         v[0] = 2147483647; v[1] = 65535; v[2] = 65535;
      }
      return 3 * sizeof(uint64_t);
   case PIPE_COMPUTE_CAP_MAX_BLOCK_SIZE:
      if (ret) {
         uint64_t *v = ret;
         v[0] = 1024; v[1] = 1024; v[2] = 64;
      }
      return 3 * sizeof(uint64_t);
   case PIPE_COMPUTE_CAP_MAX_THREADS_PER_BLOCK:
      if (ret) *(uint64_t *)ret = 1024;
      return sizeof(uint64_t);
   case PIPE_COMPUTE_CAP_MAX_GLOBAL_SIZE:
      if (ret) *(uint64_t *)ret = info ? info->vram_size_bytes : (4ull << 30);
      return sizeof(uint64_t);
   case PIPE_COMPUTE_CAP_MAX_LOCAL_SIZE:
      if (ret) *(uint64_t *)ret = 48 * 1024;
      return sizeof(uint64_t);
   case PIPE_COMPUTE_CAP_MAX_PRIVATE_SIZE:
      if (ret) *(uint64_t *)ret = 512 * 1024;
      return sizeof(uint64_t);
   case PIPE_COMPUTE_CAP_MAX_INPUT_SIZE:
      if (ret) *(uint64_t *)ret = 4096;
      return sizeof(uint64_t);
   case PIPE_COMPUTE_CAP_MAX_MEM_ALLOC_SIZE:
      if (ret) *(uint64_t *)ret = info ? info->vram_size_bytes : (4ull << 30);
      return sizeof(uint64_t);
   case PIPE_COMPUTE_CAP_MAX_CLOCK_FREQUENCY:
      if (ret) *(uint32_t *)ret = 1500;
      return sizeof(uint32_t);
   case PIPE_COMPUTE_CAP_MAX_COMPUTE_UNITS:
      if (ret) *(uint32_t *)ret = info && info->tpc_count ? info->tpc_count : 1;
      return sizeof(uint32_t);
   case PIPE_COMPUTE_CAP_IMAGES_SUPPORTED:
      if (ret) *(uint32_t *)ret = 1;
      return sizeof(uint32_t);
   case PIPE_COMPUTE_CAP_SUBGROUP_SIZES:
      if (ret) *(uint32_t *)ret = 32; /* NVIDIA warp */
      return sizeof(uint32_t);
   case PIPE_COMPUTE_CAP_MAX_SUBGROUPS:
      if (ret) *(uint32_t *)ret = info && info->max_warps_per_sm ?
                                   info->max_warps_per_sm : 32;
      return sizeof(uint32_t);
   default:
      return 0;
   }
}

static bool
nvgpu_is_format_supported(struct pipe_screen *pscreen,
                          enum pipe_format format,
                          enum pipe_texture_target target,
                          unsigned sample_count,
                          unsigned storage_sample_count,
                          unsigned usage)
{
   if (sample_count > 1) {
      if (sample_count != 2 && sample_count != 4 && sample_count != 8)
         return false;
   }

   if (MAX2(1, sample_count) != MAX2(1, storage_sample_count))
      return false;

   /* Conservative: accept all formats the util layer considers valid for the usage */
   if (usage & PIPE_BIND_RENDER_TARGET) {
      if (!util_format_is_rgba8_variant(format) &&
          !util_format_is_rgbx8_variant(format) &&
          format != PIPE_FORMAT_B8G8R8A8_UNORM &&
          format != PIPE_FORMAT_B8G8R8X8_UNORM &&
          format != PIPE_FORMAT_R16G16B16A16_FLOAT &&
          format != PIPE_FORMAT_R32G32B32A32_FLOAT &&
          format != PIPE_FORMAT_R10G10B10A2_UNORM &&
          format != PIPE_FORMAT_R11G11B10_FLOAT &&
          format != PIPE_FORMAT_R16_UNORM &&
          format != PIPE_FORMAT_R16G16_UNORM &&
          format != PIPE_FORMAT_R16G16B16A16_UNORM &&
          format != PIPE_FORMAT_R32_FLOAT &&
          format != PIPE_FORMAT_R32G32_FLOAT &&
          !util_format_is_srgb(format))
         return false;
   }

   if (usage & PIPE_BIND_DEPTH_STENCIL) {
      if (format != PIPE_FORMAT_Z24_UNORM_S8_UINT &&
          format != PIPE_FORMAT_Z24X8_UNORM &&
          format != PIPE_FORMAT_Z32_FLOAT &&
          format != PIPE_FORMAT_Z32_FLOAT_S8X24_UINT &&
          format != PIPE_FORMAT_Z16_UNORM &&
          format != PIPE_FORMAT_S8_UINT)
         return false;
   }

   return true;
}

static void
/* Fence: opaque handle is nv_fence*; wait on latest seq in the fence object. */
static void
nvgpu_fence_reference(struct pipe_screen *pscreen,
                      struct pipe_fence_handle **dst,
                      struct pipe_fence_handle *src)
{
   (void)pscreen;
   if (dst)
      *dst = src;
}

static bool
nvgpu_fence_finish(struct pipe_screen *pscreen,
                   struct pipe_context *pctx,
                   struct pipe_fence_handle *fence,
                   uint64_t timeout)
{
   struct nv_fence *f = (struct nv_fence *)fence;
   uint32_t seq;
   (void)pscreen;
   (void)pctx;

   if (!f)
      return true;
   seq = f->seq;
   if (!seq)
      return true;
   if (timeout == 0)
      return nv_fence_signaled(f, seq);
   return nv_fence_wait(f, seq, timeout) == 0;
}

static void
nvgpu_destroy_screen(struct pipe_screen *pscreen)
{
   struct nvgpu_screen *screen = nvgpu_screen(pscreen);

   slab_destroy_parent(&screen->transfer_pool);
   if (screen->rm)
      nv_rm_device_close(screen->rm);
   FREE(screen);
}

static void
nvgpu_query_memory_info(struct pipe_screen *pscreen,
                        struct pipe_memory_info *info)
{
   struct nvgpu_screen *screen = nvgpu_screen(pscreen);
   const struct nv_device_info *di = screen->info;

   memset(info, 0, sizeof(*info));
   if (!di)
      return;

   info->total_device_memory = (unsigned)(di->vram_size_bytes / 1024);
   info->avail_device_memory = (unsigned)(di->vram_usable_bytes / 1024);
   info->total_staging_memory = 0;
   info->avail_staging_memory = 0;
   info->device_memory_evicted = 0;
   info->nr_device_memory_evictions = 0;
}

struct pipe_screen *
nvgpu_screen_create(int fd, const struct pipe_screen_config *config,
                    struct sw_winsys *winsys)
{
   struct nvgpu_screen *screen;
   int gpu_index = 0;

   (void)config;
   (void)winsys;

   if (!nv_rm_probe_available()) {
      fprintf(stderr, "nvgpu: NVIDIA kernel module not available "
                      "(/dev/nvidiactl missing)\n");
      return NULL;
   }

   screen = CALLOC_STRUCT(nvgpu_screen);
   if (!screen)
      return NULL;

   screen->fd = fd;
   screen->rm = nv_rm_device_open(fd, gpu_index);
   if (!screen->rm) {
      fprintf(stderr, "nvgpu: failed to open RM device (gpu_index=%d)\n",
              gpu_index);
      FREE(screen);
      return NULL;
   }
   screen->info = nv_rm_device_info(screen->rm);

   screen->base.destroy = nvgpu_destroy_screen;
   screen->base.get_name = nvgpu_get_name;
   screen->base.get_vendor = nvgpu_get_vendor;
   screen->base.get_device_vendor = nvgpu_get_device_vendor;
   screen->base.get_param = nvgpu_get_param;
   screen->base.get_paramf = nvgpu_get_paramf;
   screen->base.get_shader_param = nvgpu_get_shader_param;
   screen->base.get_compute_param = nvgpu_get_compute_param;
   screen->base.is_format_supported = nvgpu_is_format_supported;
   screen->base.context_create = nvgpu_context_create;
   screen->base.resource_create = nvgpu_resource_create;
   screen->base.resource_from_handle = nvgpu_resource_from_handle;
   screen->base.resource_destroy = nvgpu_resource_destroy;
   screen->base.query_memory_info = nvgpu_query_memory_info;
   screen->base.fence_reference = nvgpu_fence_reference;
   screen->base.fence_finish = nvgpu_fence_finish;

   slab_create_parent(&screen->transfer_pool, sizeof(struct pipe_transfer), 16);

   if (screen->info) {
      fprintf(stderr, "nvgpu: initialized %s (%s, arch=0x%x sm=0x%x vram=%u MB)\n",
              screen->info->name[0] ? screen->info->name : "GPU",
              nv_device_info_family_name(screen->info->family),
              screen->info->architecture,
              screen->info->sm_version,
              (unsigned)(screen->info->vram_size_bytes / (1024 * 1024)));
   }

   return &screen->base;
}
