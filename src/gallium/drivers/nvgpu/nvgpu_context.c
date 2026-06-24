/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Pipe context: state tracking + pushbuffer emit skeleton.
 * Draw/clear currently allocate push space and record methods; channel
 * kickoff will submit via GPFIFO once channel setup is complete.
 */

#include "nvgpu_context.h"
#include "nvgpu_screen.h"
#include "nvgpu_resource.h"

#include "nv_rm.h"
#include "nv_channel.h"
#include "nv_push.h"
#include "nv_3d_methods.h"
#include "nv_copy_methods.h"

#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_upload_mgr.h"
#include "util/u_helpers.h"
#include "util/u_framebuffer.h"
#include "util/u_surface.h"
#include "util/u_transfer.h"
#include "util/u_blitter.h"
#include "util/format/u_format.h"
#include "pipe/p_defines.h"

#include <stdio.h>

/* ---- transfer ---- */

static void *
nvgpu_transfer_map(struct pipe_context *pctx,
                   struct pipe_resource *pres,
                   unsigned level,
                   unsigned usage,
                   const struct pipe_box *box,
                   struct pipe_transfer **transfer)
{
   struct nvgpu_resource *res = nvgpu_resource(pres);
   struct pipe_transfer *xfr;
   uint8_t *map;
   unsigned stride, layer_stride;

   xfr = CALLOC_STRUCT(pipe_transfer);
   if (!xfr)
      return NULL;

   pipe_resource_reference(&xfr->resource, pres);
   xfr->level = level;
   xfr->usage = usage;
   xfr->box = *box;

   map = nv_rm_bo_map(res->bo);
   if (!map) {
      pipe_resource_reference(&xfr->resource, NULL);
      FREE(xfr);
      return NULL;
   }

   if (pres->target == PIPE_BUFFER) {
      stride = 0;
      layer_stride = 0;
      map += box->x;
   } else {
      stride = util_format_get_stride(pres->format, pres->width0);
      stride = align(stride, 128);
      layer_stride = util_format_get_2d_size(pres->format, stride, pres->height0);
      map += box->z * layer_stride +
             box->y * stride +
             box->x * util_format_get_blocksize(pres->format);
   }

   xfr->stride = stride;
   xfr->layer_stride = layer_stride;
   *transfer = xfr;
   return map;
}

static void
nvgpu_transfer_unmap(struct pipe_context *pctx, struct pipe_transfer *xfr)
{
   struct nvgpu_resource *res;

   if (!xfr)
      return;
   res = nvgpu_resource(xfr->resource);
   /* Keep BO mapped for reuse; unmap on resource destroy */
   (void)res;
   (void)pctx;
   pipe_resource_reference(&xfr->resource, NULL);
   FREE(xfr);
}

/* ---- state setters (store only; emit at draw) ---- */

#define NVGPU_SIMPLE_SET(name, field, type) \
static void nvgpu_set_##name(struct pipe_context *pctx, const type *state) \
{ \
   struct nvgpu_context *ctx = nvgpu_context(pctx); \
   if (state) ctx->field = *state; \
   else memset(&ctx->field, 0, sizeof(ctx->field)); \
}

NVGPU_SIMPLE_SET(framebuffer_state, fb, struct pipe_framebuffer_state)

static void
nvgpu_set_viewport_states(struct pipe_context *pctx, unsigned start_slot,
                          unsigned num_viewports,
                          const struct pipe_viewport_state *state)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   (void)start_slot;
   (void)num_viewports;
   if (state)
      ctx->viewport = state[0];
}

static void
nvgpu_set_scissor_states(struct pipe_context *pctx, unsigned start_slot,
                         unsigned num_scissors,
                         const struct pipe_scissor_state *state)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   (void)start_slot;
   (void)num_scissors;
   if (state)
      ctx->scissor = state[0];
}

static void
nvgpu_set_vertex_buffers(struct pipe_context *pctx, unsigned num_buffers,
                         const struct pipe_vertex_buffer *buffers)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   unsigned i;

   for (i = 0; i < ctx->num_vb; i++)
      pipe_vertex_buffer_unreference(&ctx->vb[i]);

   ctx->num_vb = num_buffers;
   for (i = 0; i < num_buffers; i++) {
      if (buffers)
         pipe_vertex_buffer_reference(&ctx->vb[i], &buffers[i]);
      else
         memset(&ctx->vb[i], 0, sizeof(ctx->vb[i]));
   }
}

static void
nvgpu_set_constant_buffer(struct pipe_context *pctx,
                          enum pipe_shader_type shader, uint index,
                          bool take_ownership,
                          const struct pipe_constant_buffer *cb)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   struct pipe_constant_buffer *dst;

   if (shader >= PIPE_SHADER_TYPES || index >= PIPE_MAX_CONSTANT_BUFFERS)
      return;
   dst = &ctx->cb[shader][index];
   if (take_ownership) {
      pipe_resource_reference(&dst->buffer, NULL);
      if (cb) *dst = *cb;
      else memset(dst, 0, sizeof(*dst));
   } else {
      util_copy_constant_buffer(dst, cb, false);
   }
}

#define NVGPU_BIND_STATE(name, field) \
static void nvgpu_bind_##name##_state(struct pipe_context *pctx, void *state) \
{ \
   nvgpu_context(pctx)->field = state; \
}

NVGPU_BIND_STATE(blend, blend)
NVGPU_BIND_STATE(rasterizer, rs)
NVGPU_BIND_STATE(depth_stencil_alpha, zsa)
NVGPU_BIND_STATE(vs, vs)
NVGPU_BIND_STATE(fs, fs)
NVGPU_BIND_STATE(vertex_elements, velems)

/* State create/delete: store CSO by value in heap block */
#define NVGPU_CSO_CREATE(name, type) \
static void *nvgpu_create_##name##_state(struct pipe_context *pctx, const type *cso) \
{ \
   type *s = mem_dup(cso, sizeof(*cso)); \
   (void)pctx; \
   return s; \
}

#define NVGPU_CSO_DELETE(name) \
static void nvgpu_delete_##name##_state(struct pipe_context *pctx, void *s) \
{ \
   (void)pctx; \
   FREE(s); \
}

NVGPU_CSO_CREATE(blend, struct pipe_blend_state)
NVGPU_CSO_DELETE(blend)
NVGPU_CSO_CREATE(rasterizer, struct pipe_rasterizer_state)
NVGPU_CSO_DELETE(rasterizer)
NVGPU_CSO_CREATE(depth_stencil_alpha, struct pipe_depth_stencil_alpha_state)
NVGPU_CSO_DELETE(depth_stencil_alpha)
/* vertex_elements is an array - handle specially */

static void *
nvgpu_create_vertex_elements_state(struct pipe_context *pctx,
                                   unsigned num_elements,
                                   const struct pipe_vertex_element *elements)
{
   struct pipe_vertex_element *ve;
   (void)pctx;
   ve = mem_dup(elements, sizeof(*elements) * num_elements);
   return ve;
}

static void *
nvgpu_create_vs_state(struct pipe_context *pctx,
                      const struct pipe_shader_state *cso)
{
   struct pipe_shader_state *s = CALLOC_STRUCT(pipe_shader_state);
   (void)pctx;
   if (!s) return NULL;
   *s = *cso;
   /* NIR ownership: take reference if present; real compiler later */
   if (cso->type == PIPE_SHADER_IR_NIR && cso->ir.nir)
      s->ir.nir = cso->ir.nir; /* screen/context will compile on first use */
   return s;
}

static void *
nvgpu_create_fs_state(struct pipe_context *pctx,
                      const struct pipe_shader_state *cso)
{
   return nvgpu_create_vs_state(pctx, cso);
}

static void
nvgpu_delete_shader_state(struct pipe_context *pctx, void *s)
{
   (void)pctx;
   FREE(s);
}

static void
nvgpu_delete_vertex_elements_state(struct pipe_context *pctx, void *s)
{
   (void)pctx;
   FREE(s);
}

/* ---- draw / clear ---- */

static void
nvgpu_ensure_channel(struct nvgpu_context *ctx)
{
   struct nv_rm_bo_req req;

   if (ctx->channel || ctx->push_bo)
      return;

   /* Prefer full GPFIFO channel (nvidia-push style) */
   ctx->channel = nv_channel_create(ctx->screen->rm, 0 /* GRAPHICS */,
                                    0, NVGPU_PUSH_DWORDS);
   if (ctx->channel)
      return;

   /* Fallback: direct push BO without channel (record only, no submit) */
   memset(&req, 0, sizeof(req));
   req.size = NVGPU_PUSH_DWORDS * 4;
   req.alignment = 4096;
   req.vram = false;
   req.cpu_access = true;
   req.no_scanout = true;

   ctx->push_bo = nv_rm_bo_alloc(ctx->screen->rm, &req);
   if (!ctx->push_bo)
      return;
   ctx->push_map = nv_rm_bo_map(ctx->push_bo);
   ctx->push_dw_size = NVGPU_PUSH_DWORDS;
   ctx->push_dw_used = 0;
}

static bool
nvgpu_push_start(struct nvgpu_context *ctx, struct nv_push *push, uint32_t need)
{
   uint32_t *base;

   nvgpu_ensure_channel(ctx);

   if (ctx->channel) {
      base = nv_channel_push_begin(ctx->channel, need);
      if (!base)
         return false;
      nv_push_init(push, base, need);
      return true;
   }

   if (!ctx->push_map)
      return false;
   if (ctx->push_dw_used + need >= ctx->push_dw_size)
      ctx->push_dw_used = 0;
   nv_push_init(push, ctx->push_map + ctx->push_dw_used,
                ctx->push_dw_size - ctx->push_dw_used);
   return true;
}

static void
nvgpu_push_finish(struct nvgpu_context *ctx, struct nv_push *push, bool kick)
{
   uint32_t used = nv_push_dw_count(push);

   if (ctx->channel) {
      ctx->channel->push_dw_used = ctx->channel->push_dw_base + used;
      if (kick)
         nv_channel_kickoff(ctx->channel);
   } else {
      ctx->push_dw_used += used;
   }
}

/* Emit framebuffer colour/depth targets + surface clip from bound pipe FB. */
static void
nvgpu_emit_framebuffer(struct nvgpu_context *ctx, struct nv_push *push)
{
   const struct pipe_framebuffer_state *fb = &ctx->fb;
   uint8_t targets[8] = {0, 1, 2, 3, 4, 5, 6, 7};
   unsigned i, n_cbufs = 0;
   uint32_t clip_w = fb->width ? fb->width : 1;
   uint32_t clip_h = fb->height ? fb->height : 1;

   for (i = 0; i < PIPE_MAX_COLOR_BUFS && i < 8; i++) {
      struct nv_3d_surface s;
      memset(&s, 0, sizeof(s));
      if (fb->cbufs[i]) {
         struct pipe_surface *psurf = fb->cbufs[i];
         struct nvgpu_resource *res = nvgpu_resource(psurf->texture);
         unsigned stride = util_format_get_stride(psurf->format, psurf->width);
         stride = align(stride, 128);
         s.enabled = true;
         s.gpu_addr = res ? res->gpu_offset : 0;
         s.width = psurf->width;
         s.height = psurf->height;
         s.format = nv_3d_color_format_from_pipe((unsigned)psurf->format);
         s.block_linear = res ? !res->linear : false;
         s.array_pitch = stride * psurf->height;
         n_cbufs++;
      }
      nv_3d_set_color_target(push, i, &s);
   }

   if (n_cbufs) {
      targets[0] = 0;
      nv_3d_set_ct_select(push, n_cbufs, targets);
   }

   if (fb->zsbuf) {
      struct pipe_surface *zs = fb->zsbuf;
      struct nvgpu_resource *res = nvgpu_resource(zs->texture);
      struct nv_3d_surface s;
      memset(&s, 0, sizeof(s));
      s.enabled = true;
      s.gpu_addr = res ? res->gpu_offset : 0;
      s.width = zs->width;
      s.height = zs->height;
      s.format = nv_3d_zt_format_from_pipe((unsigned)zs->format);
      s.block_linear = res ? !res->linear : false;
      nv_3d_set_zeta_target(push, &s);
   }

   nv_3d_set_surface_clip(push, 0, 0, clip_w, clip_h);

   /* Viewport from pipe state (scale/offset form) */
   {
      const struct pipe_viewport_state *vp = &ctx->viewport;
      float sx = vp->scale[0];
      float sy = vp->scale[1];
      float sz = vp->scale[2];
      float ox = vp->translate[0];
      float oy = vp->translate[1];
      float oz = vp->translate[2];
      if (sx == 0.0f && sy == 0.0f) {
         sx = (float)clip_w * 0.5f;
         sy = (float)clip_h * -0.5f;
         ox = (float)clip_w * 0.5f;
         oy = (float)clip_h * 0.5f;
         sz = 0.5f;
         oz = 0.5f;
      }
      nv_3d_set_viewport0(push, sx, sy, sz, ox, oy, oz);
   }

   if (ctx->scissor.maxx > ctx->scissor.minx) {
      nv_3d_set_scissor0(push, true,
                         ctx->scissor.minx, ctx->scissor.miny,
                         ctx->scissor.maxx, ctx->scissor.maxy);
   }
}

/* Emit vertex buffers / attributes currently bound (minimal stream 0..n). */
static void
nvgpu_emit_vertex_state(struct nvgpu_context *ctx, struct nv_push *push)
{
   unsigned i;
   for (i = 0; i < ctx->num_vb && i < 16; i++) {
      struct pipe_vertex_buffer *vb = &ctx->vb[i];
      struct nvgpu_resource *res;
      uint64_t addr;
      uint32_t size, stride;
      if (!vb->buffer.resource)
         continue;
      res = nvgpu_resource(vb->buffer.resource);
      addr = (res ? res->gpu_offset : 0) + vb->buffer_offset;
      size = vb->buffer.resource->width0 > vb->buffer_offset
             ? (uint32_t)(vb->buffer.resource->width0 - vb->buffer_offset) : 0;
      stride = vb->stride ? vb->stride : 12;
      nv_3d_set_vertex_stream(push, i, addr, size, stride);
      /* Default attribute i reads stream i as R32G32B32 float at offset 0 */
      nv_3d_set_vertex_attribute(push, i, i, 0,
         NVC597_SET_VERTEX_ATTRIBUTE_A_COMPONENT_BIT_WIDTHS_R32_G32_B32, true);
   }
}

static void
nvgpu_emit_clear_methods(struct nvgpu_context *ctx, unsigned buffers,
                         const union pipe_color_union *color,
                         double depth, unsigned stencil)
{
   struct nv_push push;
   const struct nv_device_info *info = ctx->screen->info;
   uint32_t class_3d = info ? info->class_3d : 0;
   const uint32_t *color_ui = color ? color->ui : NULL;

   if (!nvgpu_push_start(ctx, &push, 256))
      return;

   if (class_3d)
      nv_3d_set_object(&push, class_3d);
   else
      nv_push_set_subch(&push, NV_PUSH_SUBCH_3D);

   /* Program RTs/ZETA before CLEAR_SURFACE (required for HW to have a target) */
   nvgpu_emit_framebuffer(ctx, &push);
   nv_3d_emit_clear_surface(&push, buffers, color_ui, (float)depth, stencil);
   nv_push_wfi(&push);
   nvgpu_push_finish(ctx, &push, true);
}

static void
nvgpu_clear(struct pipe_context *pctx, unsigned buffers,
            const struct pipe_scissor_state *scissor_state,
            const union pipe_color_union *color,
            double depth, unsigned stencil)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   (void)scissor_state;
   nvgpu_emit_clear_methods(ctx, buffers, color, depth, stencil);
}

static void
nvgpu_draw_vbo(struct pipe_context *pctx, const struct pipe_draw_info *info,
               unsigned drawid_offset,
               const struct pipe_draw_indirect_info *indirect,
               const struct pipe_draw_start_count_bias *draws,
               unsigned num_draws)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   struct nv_push push;
   const struct nv_device_info *di = ctx->screen->info;
   uint32_t class_3d = di ? di->class_3d : 0;
   unsigned i;

   (void)drawid_offset;
   (void)indirect;

   for (i = 0; i < num_draws; i++) {
      if (!nvgpu_push_start(ctx, &push, 512))
         return;

      if (class_3d)
         nv_3d_set_object(&push, class_3d);
      else
         nv_push_set_subch(&push, NV_PUSH_SUBCH_3D);

      nvgpu_emit_framebuffer(ctx, &push);
      nvgpu_emit_vertex_state(ctx, &push);

      if (info->index_size && info->has_user_indices == false &&
          info->index.resource) {
         struct nvgpu_resource *ib = nvgpu_resource(info->index.resource);
         uint64_t ib_addr = ib ? ib->gpu_offset : 0;
         uint64_t ib_size = info->index.resource->width0;
         nv_3d_set_index_buffer(&push, ib_addr, ib_size, info->index_size);
         nv_3d_emit_draw_index_buffer(&push, draws[i].start, draws[i].count);
      } else {
         nv_3d_emit_draw_vertex_array(&push, draws[i].start, draws[i].count);
      }

      nv_push_wfi(&push);
      nvgpu_push_finish(ctx, &push, i + 1 == num_draws);
   }
}

static void
nvgpu_resource_copy_region(struct pipe_context *pctx,
                           struct pipe_resource *dst,
                           unsigned dst_level,
                           unsigned dstx, unsigned dsty, unsigned dstz,
                           struct pipe_resource *src,
                           unsigned src_level,
                           const struct pipe_box *src_box)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   struct nvgpu_resource *dres = nvgpu_resource(dst);
   struct nvgpu_resource *sres = nvgpu_resource(src);
   const struct nv_device_info *info = ctx->screen->info;
   struct nv_push push;
   uint64_t saddr, daddr;
   uint32_t size;
   (void)dst_level; (void)src_level; (void)dsty; (void)dstz;

   if (!dres || !sres || !src_box)
      return;

   /* Linear 1D/buffer path via copy engine; 2D/3D falls through to util_blitter later */
   if (dst->target != PIPE_BUFFER || src->target != PIPE_BUFFER) {
      if (ctx->blitter) {
         util_resource_copy_region(pctx, dst, dst_level, dstx, dsty, dstz,
                                   src, src_level, src_box);
      }
      return;
   }

   saddr = sres->gpu_offset + (uint64_t)src_box->x;
   daddr = dres->gpu_offset + (uint64_t)dstx;
   size = (uint32_t)src_box->width;
   if (!size)
      return;

   if (!nvgpu_push_start(ctx, &push, 64))
      return;
   nv_copy_push_buffer_copy(&push, info ? info->class_copy : 0,
                            saddr, daddr, size);
   nvgpu_push_finish(ctx, &push, true);
}

static void
nvgpu_flush(struct pipe_context *pctx, struct pipe_fence_handle **fence,
            unsigned flags)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   (void)flags;

   if (ctx->channel) {
      /* Kick any pending push; wait briefly for GPU to catch up */
      if (ctx->channel->push_dw_used > ctx->channel->push_dw_base)
         nv_channel_kickoff(ctx->channel);
      if (!(flags & PIPE_FLUSH_DEFERRED))
         nv_channel_wait_idle(ctx->channel, 100000000ull); /* 100ms */
   }
   if (fence)
      *fence = NULL;
}

static void
nvgpu_destroy_context(struct pipe_context *pctx)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   unsigned i, s;

   if (ctx->blitter)
      util_blitter_destroy(ctx->blitter);

   for (i = 0; i < ctx->num_vb; i++)
      pipe_vertex_buffer_unreference(&ctx->vb[i]);

   for (s = 0; s < PIPE_SHADER_TYPES; s++) {
      unsigned c;
      for (c = 0; c < PIPE_MAX_CONSTANT_BUFFERS; c++)
         pipe_resource_reference(&ctx->cb[s][c].buffer, NULL);
   }

   util_unreference_framebuffer_state(&ctx->fb);

   if (ctx->channel)
      nv_channel_destroy(ctx->channel);
   if (ctx->push_bo) {
      nv_rm_bo_unmap(ctx->push_bo);
      nv_rm_bo_free(ctx->push_bo);
   }

   FREE(ctx);
}

static void
nvgpu_set_sampler_views(struct pipe_context *pctx,
                        enum pipe_shader_type shader,
                        unsigned start, unsigned num,
                        unsigned unbind_num_trailing_slots,
                        bool take_ownership,
                        struct pipe_sampler_view **views)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   unsigned i;
   (void)unbind_num_trailing_slots;
   (void)take_ownership;

   if (shader >= PIPE_SHADER_TYPES)
      return;
   for (i = 0; i < num; i++) {
      unsigned slot = start + i;
      if (slot >= PIPE_MAX_SAMPLERS)
         break;
      pipe_sampler_view_reference(&ctx->samplers[shader][slot],
                                  views ? views[i] : NULL);
   }
   ctx->num_samplers[shader] = start + num;
}

static struct pipe_sampler_view *
nvgpu_create_sampler_view(struct pipe_context *pctx,
                          struct pipe_resource *texture,
                          const struct pipe_sampler_view *templ)
{
   struct pipe_sampler_view *view = CALLOC_STRUCT(pipe_sampler_view);
   if (!view)
      return NULL;
   *view = *templ;
   view->texture = NULL;
   pipe_resource_reference(&view->texture, texture);
   view->reference.count = 1;
   view->context = pctx;
   return view;
}

static void
nvgpu_sampler_view_destroy(struct pipe_context *pctx,
                           struct pipe_sampler_view *view)
{
   (void)pctx;
   pipe_resource_reference(&view->texture, NULL);
   FREE(view);
}

static void *
nvgpu_create_sampler_state(struct pipe_context *pctx,
                           const struct pipe_sampler_state *state)
{
   (void)pctx;
   return mem_dup(state, sizeof(*state));
}

static void
nvgpu_bind_sampler_states(struct pipe_context *pctx,
                          enum pipe_shader_type shader,
                          unsigned start, unsigned num, void **states)
{
   (void)pctx; (void)shader; (void)start; (void)num; (void)states;
}

static void
nvgpu_delete_sampler_state(struct pipe_context *pctx, void *state)
{
   (void)pctx;
   FREE(state);
}

struct pipe_context *
nvgpu_context_create(struct pipe_screen *pscreen, void *priv, unsigned flags)
{
   struct nvgpu_screen *screen = nvgpu_screen(pscreen);
   struct nvgpu_context *ctx;

   (void)flags;
   ctx = CALLOC_STRUCT(nvgpu_context);
   if (!ctx)
      return NULL;

   ctx->screen = screen;
   ctx->base.screen = pscreen;
   ctx->base.priv = priv;
   ctx->base.destroy = nvgpu_destroy_context;

   ctx->base.draw_vbo = nvgpu_draw_vbo;
   ctx->base.clear = nvgpu_clear;
   ctx->base.flush = nvgpu_flush;

   ctx->base.buffer_map = nvgpu_transfer_map;
   ctx->base.texture_map = nvgpu_transfer_map;
   ctx->base.transfer_map = nvgpu_transfer_map;
   ctx->base.transfer_unmap = nvgpu_transfer_unmap;
   ctx->base.buffer_subdata = u_default_buffer_subdata;
   ctx->base.texture_subdata = u_default_texture_subdata;

   ctx->base.set_framebuffer_state = nvgpu_set_framebuffer_state;
   ctx->base.set_viewport_states = nvgpu_set_viewport_states;
   ctx->base.set_scissor_states = nvgpu_set_scissor_states;
   ctx->base.set_vertex_buffers = nvgpu_set_vertex_buffers;
   ctx->base.set_constant_buffer = nvgpu_set_constant_buffer;
   ctx->base.set_sampler_views = nvgpu_set_sampler_views;

   ctx->base.create_blend_state = nvgpu_create_blend_state;
   ctx->base.bind_blend_state = nvgpu_bind_blend_state;
   ctx->base.delete_blend_state = nvgpu_delete_blend_state;
   ctx->base.create_rasterizer_state = nvgpu_create_rasterizer_state;
   ctx->base.bind_rasterizer_state = nvgpu_bind_rasterizer_state;
   ctx->base.delete_rasterizer_state = nvgpu_delete_rasterizer_state;
   ctx->base.create_depth_stencil_alpha_state = nvgpu_create_depth_stencil_alpha_state;
   ctx->base.bind_depth_stencil_alpha_state = nvgpu_bind_depth_stencil_alpha_state;
   ctx->base.delete_depth_stencil_alpha_state = nvgpu_delete_depth_stencil_alpha_state;
   ctx->base.create_vs_state = nvgpu_create_vs_state;
   ctx->base.bind_vs_state = nvgpu_bind_vs_state;
   ctx->base.delete_vs_state = nvgpu_delete_shader_state;
   ctx->base.create_fs_state = nvgpu_create_fs_state;
   ctx->base.bind_fs_state = nvgpu_bind_fs_state;
   ctx->base.delete_fs_state = nvgpu_delete_shader_state;
   ctx->base.create_vertex_elements_state = nvgpu_create_vertex_elements_state;
   ctx->base.bind_vertex_elements_state = nvgpu_bind_vertex_elements_state;
   ctx->base.delete_vertex_elements_state = nvgpu_delete_vertex_elements_state;
   ctx->base.create_sampler_state = nvgpu_create_sampler_state;
   ctx->base.bind_sampler_states = nvgpu_bind_sampler_states;
   ctx->base.delete_sampler_state = nvgpu_delete_sampler_state;
   ctx->base.create_sampler_view = nvgpu_create_sampler_view;
   ctx->base.sampler_view_destroy = nvgpu_sampler_view_destroy;

   ctx->base.resource_copy_region = nvgpu_resource_copy_region;
   ctx->base.blit = NULL; /* use blitter once shaders work */

   nvgpu_ensure_channel(ctx);
   return &ctx->base;
}
