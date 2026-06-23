/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 */

#include "nvgpu_resource.h"
#include "nvgpu_screen.h"

#include "nv_rm.h"

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

static uint64_t
resource_size_bytes(const struct pipe_resource *templ)
{
   uint64_t stride, layer_stride, size;
   unsigned samples = MAX2(1, templ->nr_samples);

   if (templ->target == PIPE_BUFFER)
      return align64(templ->width0, 256);

   stride = util_format_get_stride(templ->format, templ->width0);
   stride = align64(stride, 128);
   layer_stride = util_format_get_2d_size(templ->format, stride, templ->height0);
   size = layer_stride * MAX2(1, templ->depth0) * MAX2(1, templ->array_size) * samples;

   /* Mip levels rough estimate */
   if (templ->last_level > 0)
      size = size + size / 2;

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
   res->linear = (templ->bind & PIPE_BIND_LINEAR) != 0 ||
                 templ->target == PIPE_BUFFER;

   size = resource_size_bytes(templ);

   memset(&req, 0, sizeof(req));
   req.size = size;
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
