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

static void
nvgpu_emit_clear_methods(struct nvgpu_context *ctx, unsigned buffers,
                         const union pipe_color_union *color,
                         double depth, unsigned stencil)
{
   struct nv_push push;
   const struct nv_device_info *info = ctx->screen->info;
   uint32_t class_3d = info ? info->class_3d : 0;
   const uint32_t *color_ui = color ? color->ui : NULL;

   if (!nvgpu_push_start(ctx, &push, 64))
      return;

   /* NVC597 CLEAR_SURFACE sequence (compatible with NVC697/NVC797 class numbers) */
   nv_3d_push_clear(&push, class_3d, buffers, color_ui, (float)depth, stencil);
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
   (void)info; /* index_size/mode: need full vertex setup before indexed draws */

   for (i = 0; i < num_draws; i++) {
      if (!nvgpu_push_start(ctx, &push, 128))
         return;

      /* NVC597 non-indexed draw: DRAW_VERTEX_ARRAY_BEGIN_END_{A,B} */
      nv_3d_push_draw_arrays(&push, class_3d, draws[i].start, draws[i].count);
      nvgpu_push_finish(ctx, &push, i + 1 == num_draws);
   }
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

   ctx->base.resource_copy_region = util_resource_copy_region;
   ctx->base.blit = NULL; /* use blitter once shaders work */

   nvgpu_ensure_channel(ctx);
   return &ctx->base;
}
