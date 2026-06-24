/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 */

#include "nvgpu_resource.h"
#include "nvgpu_screen.h"

#include "nv_rm.h"
#include "nv_tex.h"

#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_resource.h"
#include "util/format/u_format.h"

#include <xf86drm.h>

static bool
resource_use_vram(const struct pipe_resource *templ)
{
   if (templ->target == PIPE_BUFFER)
      return false; /* prefer GART for buffers by default */
   if (templ->bind & (PIPE_BIND_RENDER_TARGET | PIPE_BIND_DEPTH_STENCIL |
                      PIPE_BIND_DISPLAY_TARGET | PIPE_BIND_SCANOUT |
                      PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_SHARED))
      return true;
   return false;
}

/* Prefer blocklinear for render/depth/sampled 2D textures (matches proprietary). */
static bool
resource_use_blocklinear(const struct pipe_resource *templ)
{
   if (templ->target == PIPE_BUFFER || templ->target == PIPE_TEXTURE_1D ||
       templ->target == PIPE_TEXTURE_1D_ARRAY)
      return false;
   if (templ->bind & PIPE_BIND_LINEAR)
      return false;
   if (templ->usage == PIPE_USAGE_STAGING || templ->usage == PIPE_USAGE_STREAM)
      return false;
   if (templ->bind & (PIPE_BIND_RENDER_TARGET | PIPE_BIND_DEPTH_STENCIL |
                      PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_DISPLAY_TARGET |
                      PIPE_BIND_SCANOUT | PIPE_BIND_SHARED))
      return true;
   /* Default 2D textures: blocklinear on NVIDIA */
   if (templ->target == PIPE_TEXTURE_2D || templ->target == PIPE_TEXTURE_2D_ARRAY ||
       templ->target == PIPE_TEXTURE_RECT || templ->target == PIPE_TEXTURE_CUBE ||
       templ->target == PIPE_TEXTURE_CUBE_ARRAY || templ->target == PIPE_TEXTURE_3D)
      return true;
   return false;
}

static uint32_t
resource_bl_level0_size(uint32_t width, uint32_t height, uint32_t bpp,
                        uint8_t gobs_h)
{
   uint32_t gob_w_px = bpp ? (64u / bpp) : 16u;
   uint32_t gob_h_px = 8;
   uint32_t block_h_gobs = 1u << (gobs_h ? gobs_h : 4);
   uint32_t block_h_px = gob_h_px * block_h_gobs;
   uint32_t aw, ah;
   if (!gob_w_px)
      gob_w_px = 16;
   aw = (width + gob_w_px - 1) / gob_w_px * gob_w_px;
   ah = (height + block_h_px - 1) / block_h_px * block_h_px;
   if (!aw) aw = gob_w_px;
   if (!ah) ah = block_h_px;
   return aw * ah * (bpp ? bpp : 4);
}

static uint64_t
resource_size_bytes(const struct pipe_resource *templ, bool blocklinear,
                    uint32_t *row_pitch_out, uint32_t *level0_out,
                    uint32_t *bpp_out)
{
   uint64_t stride, layer_stride, size;
   unsigned samples = MAX2(1, templ->nr_samples);
   uint32_t bpp = util_format_get_blocksize(templ->format);
   if (!bpp)
      bpp = 4;
   if (bpp_out)
      *bpp_out = bpp;

   if (templ->target == PIPE_BUFFER) {
      size = align64(templ->width0, 256);
      if (row_pitch_out)
         *row_pitch_out = (uint32_t)size;
      if (level0_out)
         *level0_out = (uint32_t)size;
      return size;
   }

   stride = util_format_get_stride(templ->format, templ->width0);
   stride = align64(stride, 32); /* method pitch wants 32B; store logical pitch */
   if (row_pitch_out)
      *row_pitch_out = (uint32_t)stride;

   if (blocklinear) {
      layer_stride = resource_bl_level0_size(templ->width0, templ->height0, bpp,
                                             NV_TEX_GOBS_SIXTEEN);
      if (level0_out)
         *level0_out = (uint32_t)layer_stride;
   } else {
      layer_stride = util_format_get_2d_size(templ->format, stride, templ->height0);
      if (level0_out)
         *level0_out = (uint32_t)layer_stride;
   }

   size = layer_stride * MAX2(1, templ->depth0) * MAX2(1, templ->array_size) * samples;
   if (templ->last_level > 0)
      size = size + size / 2;
   if (blocklinear)
      size = align64(size, 512); /* BL base address 512B */
   return align64(size, 4096);
}

struct pipe_resource *
nvgpu_resource_create(struct pipe_screen *pscreen,
                      const struct pipe_resource *templ)
{
   struct nvgpu_screen *screen = nvgpu_screen(pscreen);
   struct nvgpu_resource *res;
   struct nv_rm_bo_req req;
   uint64_t size;

   res = CALLOC_STRUCT(nvgpu_resource);
   if (!res)
      return NULL;

   res->b.b = *templ;
   res->b.b.screen = pscreen;
   pipe_reference_init(&res->b.b.reference, 1);
   res->internal_format = templ->format;
   res->blocklinear = resource_use_blocklinear(templ);
   res->linear = !res->blocklinear;
   res->gobs_width = NV_TEX_GOBS_ONE;
   res->gobs_height = NV_TEX_GOBS_SIXTEEN;
   res->gobs_depth = NV_TEX_GOBS_ONE;

   size = resource_size_bytes(templ, res->blocklinear, &res->row_pitch,
                              &res->level0_size, &res->bpp);

   memset(&req, 0, sizeof(req));
   req.size = size;
   req.alignment = res->blocklinear ? 512 : 4096;
   if (req.alignment < 4096)
      req.alignment = 4096;
   req.vram = resource_use_vram(templ);
   req.cpu_access = (templ->usage == PIPE_USAGE_STAGING) ||
                    (templ->usage == PIPE_USAGE_STREAM) ||
                    (templ->bind & PIPE_BIND_LINEAR) ||
                    templ->target == PIPE_BUFFER;
   req.no_scanout = !(templ->bind & (PIPE_BIND_SCANOUT | PIPE_BIND_DISPLAY_TARGET));

   res->bo = nv_rm_bo_alloc(screen->rm, &req);
   if (!res->bo) {
      /* Retry in GART if VRAM alloc failed */
      if (req.vram) {
         req.vram = false;
         req.cpu_access = true;
         res->bo = nv_rm_bo_alloc(screen->rm, &req);
      }
   }
   if (!res->bo) {
      FREE(res);
      return NULL;
   }

   res->gpu_offset = nv_rm_bo_gpu_offset(res->bo);
   /* Host mirror for indirect draw path A (may be NULL if BO not CPU-mapped) */
   res->cpu_ptr = res->bo ? nv_rm_bo_map(res->bo) : NULL;
   res->rm_handle = nv_rm_bo_handle(res->bo);
   return &res->b.b;
}

struct pipe_resource *
nvgpu_resource_from_handle(struct pipe_screen *pscreen,
                           const struct pipe_resource *templ,
                           struct winsys_handle *whandle,
                           unsigned usage)
{
   /* DMA-BUF import path: real implementation maps fd via RM import.
    * For now allocate a placeholder resource with the given size; full
    * import via NV_ESC_RM_IMPORT_OBJECT_FROM_FD comes next tick. */
   struct pipe_resource tmpl = *templ;
   (void)whandle;
   (void)usage;
   if (!tmpl.width0)
      tmpl.width0 = 1;
   return nvgpu_resource_create(pscreen, &tmpl);
}

void
nvgpu_resource_destroy(struct pipe_screen *pscreen, struct pipe_resource *pres)
{
   struct nvgpu_resource *res = nvgpu_resource(pres);
   (void)pscreen;

   if (res->bo)
      nv_rm_bo_free(res->bo);
   FREE(res);
}
