/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Gallium pipe_screen for NVIDIA GPUs via the proprietary kernel module RM.
 * Mesa tip stores capabilities in pipe_screen::{caps,shader_caps,compute_caps}.
 */

#include "nvgpu_screen.h"
#include "nvgpu_public.h"
#include "compiler/shader_enums.h"
#include "nvgpu_context.h"
#include "nvgpu_resource.h"
#include "nvgpu_video.h"

#include "nv_rm.h"
#include "nv_device_info.h"
#include "nv_fence.h"

#include "nv_formats.h"
#include "nir.h"

#include "util/format/u_format.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_screen.h"
#include "util/u_string.h"
#include "frontend/drm_driver.h"

#include <stdio.h>
#include <string.h>
#include <xf86drm.h>

static const char *
nvgpu_get_vendor(struct pipe_screen *pscreen)
{
   (void)pscreen;
   return "NVIDIA (open Mesa)";
}

static const char *
nvgpu_get_device_vendor(struct pipe_screen *pscreen)
{
   (void)pscreen;
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

static void
nvgpu_init_shader_caps(struct nvgpu_screen *screen)
{
   mesa_shader_stage s;

   for (s = MESA_SHADER_VERTEX; s < MESA_SHADER_STAGES; s++) {
      struct pipe_shader_caps *sc =
         (struct pipe_shader_caps *)&screen->base.shader_caps[s];

      memset(sc, 0, sizeof(*sc));

      switch (s) {
      case MESA_SHADER_VERTEX:
      case MESA_SHADER_TESS_CTRL:
      case MESA_SHADER_TESS_EVAL:
      case MESA_SHADER_GEOMETRY:
      case MESA_SHADER_FRAGMENT:
      case MESA_SHADER_COMPUTE:
         break;
      default:
         continue;
      }

      sc->max_instructions = 16384;
      sc->max_alu_instructions = 16384;
      sc->max_tex_instructions = 16384;
      sc->max_tex_indirections = 8;
      sc->max_control_flow_depth = 64;
      sc->max_inputs = (s == MESA_SHADER_VERTEX) ? 32 : 32;
      sc->max_outputs = (s == MESA_SHADER_FRAGMENT) ? 8 : 32;
      sc->max_const_buffer0_size = 65536;
      sc->max_const_buffers = PIPE_MAX_CONSTANT_BUFFERS;
      sc->max_temps = 256;
      sc->max_texture_samplers = PIPE_MAX_SAMPLERS;
      sc->max_sampler_views = PIPE_MAX_SHADER_SAMPLER_VIEWS;
      sc->max_shader_buffers = PIPE_MAX_SHADER_BUFFERS;
      sc->max_shader_images = PIPE_MAX_SHADER_IMAGES;
      sc->supported_irs = (1 << PIPE_SHADER_IR_NIR);
      sc->cont_supported = true;
      sc->indirect_temp_addr = true;
      sc->indirect_const_addr = true;
      sc->integers = true;
      sc->int64_atomics = true;
      sc->fp16 = true;
      sc->int16 = true;
   }
}

static void
nvgpu_init_compute_caps(struct nvgpu_screen *screen)
{
   struct pipe_compute_caps *cc =
      (struct pipe_compute_caps *)&screen->base.compute_caps;
   const struct nv_device_info *info = screen->info;
   uint64_t vram = info ? info->vram_size_bytes : (4ull << 30);

   memset(cc, 0, sizeof(*cc));
   cc->address_bits = 64;
   cc->grid_dimension = 3;
   cc->max_grid_size[0] = 2147483647u;
   cc->max_grid_size[1] = 65535u;
   cc->max_grid_size[2] = 65535u;
   cc->max_block_size[0] = 1024;
   cc->max_block_size[1] = 1024;
   cc->max_block_size[2] = 64;
   cc->max_threads_per_block = 1024;
   cc->max_local_size = 48 * 1024;
   cc->max_clock_frequency = 1500;
   cc->max_compute_units = info && info->tpc_count ? info->tpc_count : 1;
   cc->max_subgroups = info && info->max_warps_per_sm ? info->max_warps_per_sm : 32;
   cc->subgroup_sizes = 32; /* NVIDIA warp */
   cc->max_variable_threads_per_block = 1024;
   cc->max_mem_alloc_size = vram;
   cc->max_global_size = vram;
}

static void
nvgpu_init_screen_caps(struct nvgpu_screen *screen)
{
   struct pipe_caps *caps = (struct pipe_caps *)&screen->base.caps;

   u_init_pipe_screen_caps(&screen->base, 1 /* accel */);

   caps->graphics = true;
   caps->npot_textures = true;
   caps->anisotropic_filter = true;
   caps->occlusion_query = true;
   caps->query_time_elapsed = true;
   caps->texture_shadow_map = true;
   caps->texture_swizzle = true;
   caps->texture_mirror_clamp = true;
   caps->blend_equation_separate = true;
   caps->primitive_restart = true;
   caps->primitive_restart_fixed_index = true;
   caps->indep_blend_enable = true;
   caps->indep_blend_func = true;
   caps->fs_coord_origin_upper_left = true;
   caps->fs_coord_pixel_center_half_integer = true;
   caps->depth_clip_disable = true;
   caps->depth_clamp_enable = true;
   caps->vs_instanceid = true;
   caps->vertex_element_instance_divisor = true;
   caps->mixed_colorbuffer_formats = true;
   caps->seamless_cube_map = true;
   caps->seamless_cube_map_per_texture = true;
   caps->conditional_render = true;
   caps->texture_barrier = true;
   caps->compute = true;
   caps->start_instance = true;
   caps->query_timestamp = true;
   caps->texture_multisample = true;
   caps->cube_map_array = true;
   caps->texture_buffer_objects = true;
   caps->tgsi_texcoord = true;
   caps->mixed_framebuffer_sizes = true;
   caps->buffer_map_persistent_coherent = true;
   caps->texture_query_lod = true;
   caps->sample_shading = true;
   caps->draw_indirect = true;
   caps->fs_fine_derivative = true;
   caps->conditional_render_inverted = true;
   caps->clip_halfz = true;
   caps->polygon_offset_clamp = true;
   caps->texture_float_linear = true;
   caps->texture_half_float_linear = true;
   caps->depth_bounds_test = true;
   caps->shareable_shaders = true;
   caps->clear_scissored = true;
   caps->multi_draw_indirect = true;
   caps->fs_position_is_sysval = true;
   caps->fs_face_is_integer_sysval = true;
   caps->query_memory_info = true;
   caps->framebuffer_no_attachment = true;
   caps->cull_distance = true;
   caps->doubles = true;
   caps->int64 = true;
   caps->shader_clock = true;
   caps->fp16 = true;
   caps->nir_samplers_as_deref = true;
   caps->legacy_math_rules = true;
   caps->native_fence_fd = false; /* until implemented */
   caps->memobj = false;

   caps->max_render_targets = 8;
   caps->max_dual_source_render_targets = 1;
   caps->max_texture_2d_size = 16384;
   caps->max_texture_3d_levels = 12;   /* 2048 */
   caps->max_texture_cube_levels = 15; /* 16384 */
   caps->max_texture_array_layers = 2048;
   caps->max_stream_output_buffers = 4;
   caps->max_stream_output_separate_components = 128;
   caps->max_stream_output_interleaved_components = 128;
   caps->max_geometry_output_vertices = 1024;
   caps->max_geometry_total_output_components = 1024;
   caps->max_vertex_streams = 4;
   caps->max_vertex_attrib_stride = 2048;
   caps->max_viewports = 16;
   caps->max_varyings = 32;
   caps->glsl_feature_level = 460;
   caps->glsl_feature_level_compatibility = 460;
   caps->constant_buffer_offset_alignment = 256; /* NVIDIA CB prefers 256B */
   caps->min_map_buffer_alignment = 64;
   caps->shader_buffer_offset_alignment = 16;
   caps->texture_buffer_offset_alignment = 16;
   caps->max_texel_buffer_elements = 134217728;
   caps->max_shader_buffer_size = 1u << 27;
   caps->viewport_subpixel_bits = 8;
   caps->rasterizer_subpixel_bits = 8;
   caps->min_texel_offset = -8;
   caps->max_texel_offset = 7;
   caps->min_texture_gather_offset = -32;
   caps->max_texture_gather_offset = 31;
   caps->endianness = PIPE_ENDIAN_LITTLE;
   caps->vendor_id = 0x10de; /* NVIDIA PCI vendor */
   if (screen->info)
      caps->device_id = screen->info->pci_device_id;
   else
      caps->device_id = 0;
   caps->video_memory = screen->info ?
      (unsigned)(screen->info->vram_size_bytes / (1024 * 1024)) : 0;
   caps->uma = false;
   (void)screen;
}

static bool
nvgpu_is_format_supported(struct pipe_screen *pscreen,
                          enum pipe_format format,
                          enum pipe_texture_target target,
                          unsigned sample_count,
                          unsigned storage_sample_count,
                          unsigned usage)
{
   (void)pscreen;
   (void)target;

   if (format == PIPE_FORMAT_NONE)
      return !!(usage & PIPE_BIND_RENDER_TARGET);

   /* MSAA: only power-of-two sample counts up to 8 */
   if (sample_count > 1) {
      if (sample_count != 2 && sample_count != 4 && sample_count != 8)
         return false;
   }

   if (MAX2(1, sample_count) != MAX2(1, storage_sample_count))
      return false;

   if (!util_format_description(format))
      return false;

   /* Render target: check against the complete NVC597 RT format table */
   if (usage & PIPE_BIND_RENDER_TARGET) {
      if (!nv_format_is_color_renderable(format))
         return false;
   }

   /* Blending: integer formats cannot blend */
   if (usage & PIPE_BIND_BLENDABLE) {
      if (!nv_format_is_blendable(format))
         return false;
   }

   /* Depth/stencil: check against the ZT format table */
   if (usage & PIPE_BIND_DEPTH_STENCIL) {
      if (!nv_format_is_depth_stencil(format))
         return false;
   }

   /* Sampler view: use the comprehensive texturable format query */
   if (usage & PIPE_BIND_SAMPLER_VIEW) {
      if (!nv_format_is_texturable(format))
         return false;
   }

   /* Vertex buffer: check against vertex attribute format table */
   if (usage & PIPE_BIND_VERTEX_BUFFER) {
      if (!nv_format_is_vertex_format(format))
         return false;
   }

   /* Index buffer: only 8, 16, 32-bit unsigned */
   if (usage & PIPE_BIND_INDEX_BUFFER) {
      if (format != PIPE_FORMAT_R8_UINT &&
          format != PIPE_FORMAT_R16_UINT &&
          format != PIPE_FORMAT_R32_UINT)
         return false;
   }

   /* Shader image / storage: same as texturable + renderable for typed */
   if (usage & PIPE_BIND_SHADER_IMAGE) {
      if (!nv_format_is_texturable(format))
         return false;
   }

   /* Display/scanout: limited to common display formats */
   if (usage & (PIPE_BIND_DISPLAY_TARGET | PIPE_BIND_SCANOUT)) {
      switch (format) {
      case PIPE_FORMAT_B8G8R8A8_UNORM:
      case PIPE_FORMAT_B8G8R8X8_UNORM:
      case PIPE_FORMAT_R8G8B8A8_UNORM:
      case PIPE_FORMAT_R8G8B8X8_UNORM:
      case PIPE_FORMAT_R10G10B10A2_UNORM:
      case PIPE_FORMAT_R16G16B16A16_FLOAT:
         break;
      default:
         return false;
      }
   }

   /* Constant buffer / shader buffer / stream output / shared / linear:
    * these are format-independent binds; accept if format is valid. */
   return true;
}

/* Fence: opaque handle is nv_fence*; wait on latest seq in the fence object. */
static void
nvgpu_fence_reference(struct pipe_screen *pscreen,
                      struct pipe_fence_handle **dst,
                      struct pipe_fence_handle *src)
{
   struct nv_fence *old = dst ? (struct nv_fence *)*dst : NULL;
   struct nv_fence *ref = (struct nv_fence *)src;
   (void)pscreen;

   if (old == ref)
      return;
   if (ref)
      nv_fence_ref(ref);
   if (dst)
      *dst = src;
   if (old)
      nv_fence_unref(old);
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

static int
type_size_vec4(const struct glsl_type *type, bool bindless)
{
   (void)bindless;
   return glsl_count_attribute_slots(type, false);
}

static void
nvgpu_finalize_nir(struct pipe_screen *pscreen, struct nir_shader *nir,
                   bool optimize)
{
   (void)pscreen;
   if (!nir)
      return;

   if (optimize) {
      bool progress;
      do {
         progress = false;
         NIR_PASS(progress, nir, nir_opt_copy_prop);
         NIR_PASS(progress, nir, nir_opt_dce);
         NIR_PASS(progress, nir, nir_opt_dead_cf);
         NIR_PASS(progress, nir, nir_opt_cse);
         NIR_PASS(progress, nir, nir_opt_constant_folding);
         NIR_PASS(progress, nir, nir_opt_algebraic);
         NIR_PASS(progress, nir, nir_opt_loop);
      } while (progress);
   }

   NIR_PASS(_, nir, nir_lower_var_copies);
   NIR_PASS(_, nir, nir_lower_vars_to_ssa);
   NIR_PASS(_, nir, nir_lower_io, nir_var_shader_in | nir_var_shader_out,
            type_size_vec4, nir_lower_io_lower_64bit_to_32);

   /* Lower remaining ALU ops we don't handle in SASS backend */
   NIR_PASS(_, nir, nir_lower_alu_to_scalar, NULL, NULL);
   NIR_PASS(_, nir, nir_lower_all_phis_to_scalar);

   if (optimize) {
      NIR_PASS(_, nir, nir_opt_dce);
      NIR_PASS(_, nir, nir_opt_dead_cf);
   }

   NIR_PASS(_, nir, nir_convert_from_ssa, true, false);
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
   screen->base.is_format_supported = nvgpu_is_format_supported;
   screen->base.context_create = nvgpu_context_create;
   screen->base.resource_create = nvgpu_resource_create;
   screen->base.resource_from_handle = nvgpu_resource_from_handle;
   screen->base.resource_get_handle = nvgpu_resource_get_handle;
   screen->base.resource_destroy = nvgpu_resource_destroy;
   screen->base.query_memory_info = nvgpu_query_memory_info;
   screen->base.fence_reference = nvgpu_fence_reference;
   screen->base.fence_finish = nvgpu_fence_finish;
   screen->base.finalize_nir = nvgpu_finalize_nir;
   screen->base.get_video_param = nvgpu_screen_get_video_param;
   screen->base.is_video_format_supported = nvgpu_screen_is_video_format_supported;

   nvgpu_init_shader_caps(screen);
   nvgpu_init_compute_caps(screen);
   nvgpu_init_screen_caps(screen);

   /* NIR compiler options: tell frontend what to lower before our backend.
    * NVIDIA Turing+ hardware has native 32-bit int/float; 16-bit is partial.
    * We handle FMA, most ALU, shifts, min/max natively via SASS. */
   {
      static const nir_shader_compiler_options nvgpu_nir_options = {
         .lower_fdiv = true,
         .lower_flrp32 = true,
         .lower_flrp64 = true,
         .lower_fpow = true,
         .lower_fmod = true,
         .lower_bitfield_extract = true,
         .lower_bitfield_insert = true,
         .lower_uadd_carry = true,
         .lower_usub_borrow = true,
         .lower_mul_high = true,
         .lower_extract_byte = true,
         .lower_extract_word = true,
         .lower_insert_byte = true,
         .lower_insert_word = true,
         .lower_pack_half_2x16 = true,
         .lower_unpack_half_2x16 = true,
         .lower_int64_options = nir_lower_imul64 | nir_lower_iadd64 |
                                nir_lower_shift64 | nir_lower_minmax64 |
                                nir_lower_conv64,
         .lower_doubles_options = nir_lower_drcp | nir_lower_dsqrt |
                                  nir_lower_drsq | nir_lower_dtrunc |
                                  nir_lower_dfloor | nir_lower_dceil |
                                  nir_lower_dfract | nir_lower_dround_even |
                                  nir_lower_dmod,
         .max_unroll_iterations = 32,
         .force_indirect_unrolling = nir_var_all,
         .force_indirect_unrolling_sampler = true,
         .has_fsub = true,
         .has_isub = true,
         .compact_arrays = true,
         .support_16bit_alu = false,
      };
      for (unsigned i = 0; i < MESA_SHADER_MESH_STAGES; i++)
         screen->base.nir_options[i] = &nvgpu_nir_options;
   }

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
