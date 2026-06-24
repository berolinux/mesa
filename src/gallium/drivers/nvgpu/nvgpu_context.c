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
#include "nv_qmd.h"
#include "nv_shader.h"
#include "nv_fence.h"
#include "nv_tex.h"
#include "nv_sph.h"
#include "nv_nir.h"

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
#include <string.h>

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
nvgpu_set_stencil_ref(struct pipe_context *pctx,
                      const struct pipe_stencil_ref ref)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   ctx->stencil_ref = ref;
}

static void
nvgpu_set_blend_color(struct pipe_context *pctx,
                      const struct pipe_blend_color *color)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   if (color) {
      ctx->blend_color[0] = color->color[0];
      ctx->blend_color[1] = color->color[1];
      ctx->blend_color[2] = color->color[2];
      ctx->blend_color[3] = color->color[3];
   }
}

static void
nvgpu_set_sample_mask(struct pipe_context *pctx, unsigned sample_mask)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   ctx->sample_mask = sample_mask;
}

static void
nvgpu_set_min_samples(struct pipe_context *pctx, unsigned min_samples)
{
   (void)pctx;
   (void)min_samples;
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
                          mesa_shader_stage shader, uint index,
                          const struct pipe_constant_buffer *cb)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   struct pipe_constant_buffer *dst;

   if (shader >= NVGPU_SHADER_STAGES || index >= PIPE_MAX_CONSTANT_BUFFERS)
      return;
   dst = &ctx->cb[shader][index];
   util_copy_constant_buffer(dst, cb);
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
/* vertex_elements: store count + elements for full attribute emit */

static void *
nvgpu_create_vertex_elements_state(struct pipe_context *pctx,
                                   unsigned num_elements,
                                   const struct pipe_vertex_element *elements)
{
   struct nvgpu_velems_state *st;
   unsigned i;
   (void)pctx;

   st = CALLOC_STRUCT(nvgpu_velems_state);
   if (!st)
      return NULL;
   st->num_elements = MIN2(num_elements, PIPE_MAX_ATTRIBS);
   for (i = 0; i < st->num_elements; i++)
      st->ve[i] = elements[i];
   return st;
}

static void
nvgpu_ensure_shader_uploaded(struct nvgpu_context *ctx,
                             struct nvgpu_shader_cso *scso)
{
   if (!scso || !scso->nvsh || scso->nvsh->uploaded)
      return;

   /* NIR->SPH+SASS via nvidia compiler (EXIT stub until full ISel); falls
    * back to trivial SPH if no NIR attached. */
   if (scso->nvsh->nir)
      nv_shader_compile_nir(scso->nvsh, scso->nvsh->nir);
   else
      nv_shader_compile_nir_stub(scso->nvsh);
   if (scso->nvsh->uploaded && !ctx->program_region_base && scso->nvsh->code_gpu_addr)
      ctx->program_region_base = scso->nvsh->code_gpu_addr;
}

static void *
nvgpu_create_shader_state(struct pipe_context *pctx,
                          const struct pipe_shader_state *cso,
                          enum nv_shader_kind kind)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   struct nvgpu_shader_cso *scso;

   scso = CALLOC_STRUCT(nvgpu_shader_cso);
   if (!scso)
      return NULL;

   if (cso)
      scso->base = *cso;

   scso->nvsh = nv_shader_create(ctx->screen->rm, kind);
   if (!scso->nvsh) {
      FREE(scso);
      return NULL;
   }

   if (cso && cso->type == PIPE_SHADER_IR_NIR && cso->ir.nir)
      nv_shader_set_nir(scso->nvsh, cso->ir.nir, false);

   nvgpu_ensure_shader_uploaded(ctx, scso);
   return scso;
}

static void *
nvgpu_create_vs_state(struct pipe_context *pctx,
                      const struct pipe_shader_state *cso)
{
   return nvgpu_create_shader_state(pctx, cso, NV_SHADER_KIND_VERTEX);
}

static void *
nvgpu_create_fs_state(struct pipe_context *pctx,
                      const struct pipe_shader_state *cso)
{
   return nvgpu_create_shader_state(pctx, cso, NV_SHADER_KIND_FRAGMENT);
}

static void *
nvgpu_create_gs_state(struct pipe_context *pctx,
                      const struct pipe_shader_state *cso)
{
   return nvgpu_create_shader_state(pctx, cso, NV_SHADER_KIND_GEOMETRY);
}

static void *
nvgpu_create_tcs_state(struct pipe_context *pctx,
                       const struct pipe_shader_state *cso)
{
   return nvgpu_create_shader_state(pctx, cso, NV_SHADER_KIND_TESS_CTRL);
}

static void *
nvgpu_create_tes_state(struct pipe_context *pctx,
                       const struct pipe_shader_state *cso)
{
   return nvgpu_create_shader_state(pctx, cso, NV_SHADER_KIND_TESS_EVAL);
}

static void
nvgpu_bind_gs_state(struct pipe_context *pctx, void *s)
{
   nvgpu_context(pctx)->gs = s;
}

static void
nvgpu_bind_tcs_state(struct pipe_context *pctx, void *s)
{
   nvgpu_context(pctx)->tcs = s;
}

static void
nvgpu_bind_tes_state(struct pipe_context *pctx, void *s)
{
   nvgpu_context(pctx)->tes = s;
}

static void
nvgpu_delete_shader_state(struct pipe_context *pctx, void *s)
{
   struct nvgpu_shader_cso *scso = s;
   (void)pctx;
   if (!scso)
      return;
   nv_shader_destroy(scso->nvsh);
   FREE(scso);
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
      if (fb->cbufs[i].texture) {
         const struct pipe_surface *psurf = &fb->cbufs[i];
         struct nvgpu_resource *res = nvgpu_resource(psurf->texture);
         unsigned tw = psurf->texture->width0;
         unsigned th = psurf->texture->height0;
         unsigned stride = util_format_get_stride(psurf->format, tw);
         stride = align(stride, 128);
         if (res && res->row_pitch)
            stride = res->row_pitch;
         s.enabled = true;
         s.gpu_addr = res ? res->gpu_offset : 0;
         s.width = tw;
         s.height = th;
         s.format = nv_3d_color_format_from_pipe((unsigned)psurf->format);
         s.block_linear = res ? res->blocklinear : false;
         s.array_pitch = stride * th;
         n_cbufs++;
      }
      nv_3d_set_color_target(push, i, &s);
   }

   if (n_cbufs) {
      targets[0] = 0;
      nv_3d_set_ct_select(push, n_cbufs, targets);
   }

   if (fb->zsbuf.texture) {
      const struct pipe_surface *zs = &fb->zsbuf;
      struct nvgpu_resource *res = nvgpu_resource(zs->texture);
      struct nv_3d_surface s;
      memset(&s, 0, sizeof(s));
      s.enabled = true;
      s.gpu_addr = res ? res->gpu_offset : 0;
      s.width = zs->texture->width0;
      s.height = zs->texture->height0;
      s.format = nv_3d_zt_format_from_pipe((unsigned)zs->format);
      s.block_linear = res ? res->blocklinear : false;
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

/* Emit vertex buffers + attributes from pipe_vertex_element CSO. */
static void
nvgpu_emit_vertex_state(struct nvgpu_context *ctx, struct nv_push *push)
{
   const struct nvgpu_velems_state *vel = ctx->velems;
   unsigned i, max_stream = 0;
   bool stream_emitted[PIPE_MAX_ATTRIBS];

   memset(stream_emitted, 0, sizeof(stream_emitted));

   /* First pass: vertex streams for each unique buffer index in velems / vb */
   for (i = 0; i < ctx->num_vb && i < PIPE_MAX_ATTRIBS; i++) {
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
      stride = 12; /* default; refined from velems when available */
      nv_3d_set_vertex_stream(push, i, addr, size, stride);
      stream_emitted[i] = true;
      if (i > max_stream)
         max_stream = i;
   }

   if (vel && vel->num_elements) {
      for (i = 0; i < vel->num_elements && i < 32; i++) {
         const struct pipe_vertex_element *e = &vel->ve[i];
         unsigned stream = e->vertex_buffer_index;
         unsigned comp = nv_3d_vertex_comp_from_pipe((unsigned)e->src_format);
         bool active = true;

         /* Ensure stream programmed even if only referenced via velem */
         if (stream < PIPE_MAX_ATTRIBS && !stream_emitted[stream] &&
             stream < ctx->num_vb && ctx->vb[stream].buffer.resource) {
            struct pipe_vertex_buffer *vb = &ctx->vb[stream];
            struct nvgpu_resource *res = nvgpu_resource(vb->buffer.resource);
            uint64_t addr = (res ? res->gpu_offset : 0) + vb->buffer_offset;
            uint32_t size = vb->buffer.resource->width0 > vb->buffer_offset
               ? (uint32_t)(vb->buffer.resource->width0 - vb->buffer_offset) : 0;
            uint32_t stride = 12;
            nv_3d_set_vertex_stream(push, stream, addr, size, stride);
            stream_emitted[stream] = true;
         }

         nv_3d_set_vertex_attribute(push, i, stream, e->src_offset, comp, active);
      }
      /* Deactivate remaining attribute slots */
      for (; i < 16; i++)
         nv_3d_set_vertex_attribute(push, i, 0, 0,
            NVC597_SET_VERTEX_ATTRIBUTE_A_COMPONENT_BIT_WIDTHS_R32_G32_B32,
            false);
   } else {
      /* Fallback when no velems CSO: attribute i = stream i as R32G32B32 */
      for (i = 0; i < ctx->num_vb && i < 16; i++) {
         if (!ctx->vb[i].buffer.resource)
            continue;
         nv_3d_set_vertex_attribute(push, i, i, 0,
            NVC597_SET_VERTEX_ATTRIBUTE_A_COMPONENT_BIT_WIDTHS_R32_G32_B32,
            true);
      }
   }
}

/* One-time 3D channel defaults + texture pool bind. */
static void
nvgpu_ensure_3d_init(struct nvgpu_context *ctx, struct nv_push *push)
{
   const struct nv_device_info *di = ctx->screen->info;

   if (!ctx->channel_init_emitted) {
      uint32_t spa_maj = 5, spa_min = 3; /* Ampere-class default; refined by arch later */
      if (di) {
         if (di->architecture >= 0x170) { spa_maj = 7; spa_min = 0; }
         else if (di->architecture >= 0x160) { spa_maj = 6; spa_min = 0; }
      }
      nv_3d_emit_channel_init_defaults(push, spa_maj, spa_min, 5, 3);
      ctx->channel_init_emitted = true;
   }

   if (!ctx->tex_pool && ctx->screen->rm)
      ctx->tex_pool = nv_tex_pool_create(ctx->screen->rm, 64);

   if (ctx->tex_pool && !ctx->tex_pool_bound) {
      nv_tex_pool_emit_bind(push, ctx->tex_pool);
      ctx->tex_pool_bound = true;
   }
}

/* Emit blend / depth-stencil / rasterizer from bound CSO pointers. */
static void
nvgpu_emit_fixed_func(struct nvgpu_context *ctx, struct nv_push *push)
{
   const struct pipe_blend_state *blend = ctx->blend;
   const struct pipe_depth_stencil_alpha_state *zsa = ctx->zsa;
   const struct pipe_rasterizer_state *rs = ctx->rs;
   bool blend_en = false;
   unsigned rgb_func = 0, rgb_src = 1, rgb_dst = 0;
   unsigned a_func = 0, a_src = 1, a_dst = 0;
   unsigned cm = 0xf;
   bool depth_en = false, depth_wr = true;
   unsigned depth_fn = 1; /* LESS */
   bool stencil_en = false;
   unsigned cull = 0;
   bool front_ccw = false;
   unsigned fill = 0;
   bool smooth = true;

   if (blend) {
      blend_en = blend->rt[0].blend_enable;
      rgb_func = blend->rt[0].rgb_func;
      rgb_src = blend->rt[0].rgb_src_factor;
      rgb_dst = blend->rt[0].rgb_dst_factor;
      a_func = blend->rt[0].alpha_func;
      a_src = blend->rt[0].alpha_src_factor;
      a_dst = blend->rt[0].alpha_dst_factor;
      cm = blend->rt[0].colormask;
   }
   if (zsa) {
      depth_en = zsa->depth_enabled;
      depth_wr = zsa->depth_writemask;
      depth_fn = zsa->depth_func;
      stencil_en = zsa->stencil[0].enabled;
   }
   if (rs) {
      cull = rs->cull_face;
      front_ccw = rs->front_ccw;
      fill = rs->fill_front; /* 0 fill, 1 line, 2 point in pipe */
      smooth = !rs->flatshade;
   }

   nv_3d_emit_blend_zsa_raster(push, blend_en, rgb_func, rgb_src, rgb_dst,
                               a_func, a_src, a_dst, cm,
                               depth_en, depth_wr, depth_fn, stencil_en,
                               cull, front_ccw, fill, smooth);
   if (zsa && stencil_en) {
      const struct pipe_stencil_state *sf = &zsa->stencil[0];
      const struct pipe_stencil_state *sb = &zsa->stencil[1];
      bool two_sided = sb->enabled;
      unsigned fref = ctx->stencil_ref.ref_value[0];
      unsigned bref = ctx->stencil_ref.ref_value[1];
      nv_3d_emit_stencil_state_full(push, true, two_sided,
         sf->func, sf->valuemask, sf->writemask, fref,
         sf->fail_op, sf->zfail_op, sf->zpass_op,
         two_sided ? sb->func : sf->func,
         two_sided ? sb->valuemask : sf->valuemask,
         two_sided ? sb->writemask : sf->writemask,
         two_sided ? bref : fref,
         two_sided ? sb->fail_op : sf->fail_op,
         two_sided ? sb->zfail_op : sf->zfail_op,
         two_sided ? sb->zpass_op : sf->zpass_op);
   }
   if (rs) {
      if (rs->offset_tri)
         nv_3d_emit_depth_bias(push, true, rs->offset_units, rs->offset_clamp,
                               rs->offset_scale);
      if (rs->rasterizer_discard)
         nv_3d_emit_rasterizer_discard(push, true);
      if (rs->point_size > 0.0f)
         nv_3d_emit_point_size(push, rs->point_size);
      if (rs->flatshade_first)
         nv_3d_emit_provoking_vertex(push, false); /* first vertex */
      else if (rs->flatshade)
         nv_3d_emit_provoking_vertex(push, true);  /* last vertex typical */
      if (blend && blend->logicop_enable)
         nv_3d_emit_logic_op(push, true, blend->logicop_func);
      /* depth_clamp: pipe sets depth_clip_near/far false when clamping */
      nv_3d_emit_depth_clamp_enable(push,
         rs->depth_clamp || (!rs->depth_clip_near && !rs->depth_clip_far));
      nv_3d_emit_viewport_z_clip_range(push, rs->clip_halfz);
      if (rs->line_width > 0.0f)
         nv_3d_emit_line_width(push, rs->line_width);
   }
   /* Dynamic blend constants from set_blend_color */
   nv_3d_emit_blend_constants(push, ctx->blend_color[0], ctx->blend_color[1],
                              ctx->blend_color[2], ctx->blend_color[3]);
   if (ctx->fb.samples > 1)
      nv_3d_emit_msaa(push, ctx->fb.samples, false);
   if (ctx->sample_mask && ctx->sample_mask != ~0u)
      nv_3d_emit_sample_mask(push, ctx->sample_mask);
}

/* Upload FS sampler views into tex pool as pitch 2D headers (slot = view index). */
static void
nvgpu_emit_textures(struct nvgpu_context *ctx, struct nv_push *push)
{
   unsigned i, n;
   (void)push;

   if (!ctx->tex_pool)
      return;

   n = ctx->num_samplers[MESA_SHADER_FRAGMENT];
   for (i = 0; i < n && i < PIPE_MAX_SAMPLERS; i++) {
      struct pipe_sampler_view *sv = ctx->samplers[MESA_SHADER_FRAGMENT][i];
      struct nvgpu_resource *res;
      struct nv_tex_desc desc;
      struct nv_tex_entry ent;
      struct pipe_sampler_state *ss = NULL;
      unsigned pitch, w, h;

      if (!sv || !sv->texture)
         continue;
      res = nvgpu_resource(sv->texture);
      if (!res)
         continue;

      memset(&desc, 0, sizeof(desc));
      w = sv->texture->width0;
      h = sv->texture->height0;
      pitch = align(util_format_get_stride(sv->format, w), 128);
      desc.gpu_addr = res->gpu_offset;
      desc.width = w;
      desc.height = h;
      desc.pitch = pitch;
      nv_tex_format_from_pipe((unsigned)sv->format, &desc.components, &desc.data_type);
      desc.src_x = NV_TEX_SRC_R;
      desc.src_y = NV_TEX_SRC_G;
      desc.src_z = NV_TEX_SRC_B;
      desc.src_w = NV_TEX_SRC_A;
      desc.normalized_coords = true;
      desc.addr_u = NV_TEX_SAMP_ADDR_CLAMP_EDGE;
      desc.addr_v = NV_TEX_SAMP_ADDR_CLAMP_EDGE;
      desc.addr_p = NV_TEX_SAMP_ADDR_CLAMP_EDGE;
      desc.mag_filt = NV_TEX_SAMP_FILT_LINEAR;
      desc.min_filt = NV_TEX_SAMP_FILT_LINEAR;

      if (i < ctx->num_sampler_cso && ctx->sampler_cso[i])
         ss = ctx->sampler_cso[i];
      if (ss) {
         if (ss->wrap_s == PIPE_TEX_WRAP_REPEAT)
            desc.addr_u = NV_TEX_SAMP_ADDR_WRAP;
         else if (ss->wrap_s == PIPE_TEX_WRAP_MIRROR_REPEAT)
            desc.addr_u = NV_TEX_SAMP_ADDR_MIRROR;
         if (ss->wrap_t == PIPE_TEX_WRAP_REPEAT)
            desc.addr_v = NV_TEX_SAMP_ADDR_WRAP;
         if (ss->min_img_filter == PIPE_TEX_FILTER_NEAREST)
            desc.min_filt = NV_TEX_SAMP_FILT_NEAREST;
         if (ss->mag_img_filter == PIPE_TEX_FILTER_NEAREST)
            desc.mag_filt = NV_TEX_SAMP_FILT_NEAREST;
      }

      /* Blocklinear textures use TEXHEAD_BL; linear/pitch use TEXHEAD_PITCH */
      if (res && res->blocklinear && !res->linear) {
         desc.blocklinear = true;
         desc.gobs_width = res->gobs_width;
         desc.gobs_height = res->gobs_height ? res->gobs_height : NV_TEX_GOBS_SIXTEEN;
         desc.gobs_depth = res->gobs_depth;
         desc.gpu_addr &= ~0x1ffull;
         if (res->row_pitch)
            desc.pitch = res->row_pitch;
      }
      nv_tex_encode_2d(&desc, &ent);
      nv_tex_pool_set_entry(ctx->tex_pool, (int)i, &ent);
   }

   if (n)
      nv_tex_invalidate_caches(push);
}

/* Bind CB0 for a shader stage if present. */
static void
nvgpu_emit_cb0(struct nvgpu_context *ctx, struct nv_push *push,
               mesa_shader_stage stage, unsigned bind_group)
{
   if (!ctx->cb[stage][0].buffer)
      return;
   struct nvgpu_resource *res = nvgpu_resource(ctx->cb[stage][0].buffer);
   uint64_t addr = (res ? res->gpu_offset : 0) + ctx->cb[stage][0].buffer_offset;
   uint32_t sz = ctx->cb[stage][0].buffer_size;
   if (!sz && res)
      sz = (uint32_t)res->b.b.width0;
   if (sz) {
      nv_3d_set_constant_buffer_selector(push, (sz + 255u) & ~255u, addr);
      nv_3d_bind_group_constant_buffer(push, bind_group, 0, true);
   }
}

/* Ensure indirect shadow BO exists for GPU-only indirect buffers (path B). */
static void
nvgpu_ensure_indirect_shadow(struct nvgpu_context *ctx)
{
   struct nv_rm_bo_req req;
   if (!ctx || ctx->indirect_shadow_bo || !ctx->screen || !ctx->screen->rm)
      return;
   memset(&req, 0, sizeof(req));
   req.size = 4096;
   req.alignment = 256;
   req.vram = false;
   req.cpu_access = true;
   req.no_scanout = true;
   req.map_gpu_va = true;
   ctx->indirect_shadow_bo = nv_rm_bo_alloc(ctx->screen->rm, &req);
   if (ctx->indirect_shadow_bo) {
      ctx->indirect_shadow_bo_size = 4096;
      ctx->indirect_shadow_map = nv_rm_bo_map(ctx->indirect_shadow_bo);
   }
}

/* Emit bound VS/TCS/TES/GS/FS via SET_PIPELINE_SHADER. */
static void
nvgpu_emit_shaders(struct nvgpu_context *ctx, struct nv_push *push)
{
   struct nvgpu_shader_cso *vs = ctx->vs;
   struct nvgpu_shader_cso *tcs = ctx->tcs;
   struct nvgpu_shader_cso *tes = ctx->tes;
   struct nvgpu_shader_cso *gs = ctx->gs;
   struct nvgpu_shader_cso *fs = ctx->fs;
   uint64_t region = 0;
   bool region_once = false;
   bool tess_active, gs_active;

   if (vs)
      nvgpu_ensure_shader_uploaded(ctx, vs);
   if (tcs)
      nvgpu_ensure_shader_uploaded(ctx, tcs);
   if (tes)
      nvgpu_ensure_shader_uploaded(ctx, tes);
   if (gs)
      nvgpu_ensure_shader_uploaded(ctx, gs);
   if (fs)
      nvgpu_ensure_shader_uploaded(ctx, fs);

   if (!ctx->program_region_emitted && ctx->program_region_base) {
      region = ctx->program_region_base;
      ctx->program_region_emitted = true;
      region_once = true;
   }

   tess_active = tcs && tcs->nvsh && tcs->nvsh->uploaded &&
                 tes && tes->nvsh && tes->nvsh->uploaded;
   gs_active = gs && gs->nvsh && gs->nvsh->uploaded;

   /* Tess/geom: enable only when CSO present, else disable stage.
    * nv_shader_emit_bind emits NIR-derived tess/GS meta when valid. */
   if (tcs && tcs->nvsh && tcs->nvsh->uploaded) {
      nv_shader_emit_bind(push, tcs->nvsh, region_once ? region : 0, -1);
      region_once = false;
   } else {
      nv_3d_disable_pipeline_shader(push, NV_3D_PIPE_STAGE_TESS_INIT);
   }
   if (tes && tes->nvsh && tes->nvsh->uploaded) {
      nv_shader_emit_bind(push, tes->nvsh, 0, -1);
   } else {
      nv_3d_disable_pipeline_shader(push, NV_3D_PIPE_STAGE_TESS);
   }
   /* If tess active but TES had no meta, apply conservative defaults once */
   if (tess_active && tes && tes->nvsh && !tes->nvsh->tess_meta_valid &&
       (!tcs || !tcs->nvsh || !tcs->nvsh->tess_meta_valid))
      nv_3d_tess_enable_defaults(push);

   if (gs_active) {
      nv_shader_emit_bind(push, gs->nvsh, 0, -1);
      if (!gs->nvsh->gs_meta_valid)
         nv_3d_set_geometry_shader_output(push, NVC597_TOPOLOGY_TRIANGLES, 256);
   } else {
      nv_3d_disable_pipeline_shader(push, NV_3D_PIPE_STAGE_GEOMETRY);
   }

   if (vs && vs->nvsh && vs->nvsh->uploaded) {
      nv_shader_emit_bind(push, vs->nvsh, region_once ? region : 0, -1);
      region_once = false;
   }
   if (fs && fs->nvsh && fs->nvsh->uploaded)
      nv_shader_emit_bind(push, fs->nvsh, region_once ? region : 0, -1);

   /* Application constant buffers: CB0 per active stage */
   nvgpu_emit_cb0(ctx, push, MESA_SHADER_VERTEX, NV_3D_BIND_GROUP_VERTEX);
   if (tcs)
      nvgpu_emit_cb0(ctx, push, MESA_SHADER_TESS_CTRL, NV_3D_BIND_GROUP_VERTEX);
   if (tes)
      nvgpu_emit_cb0(ctx, push, MESA_SHADER_TESS_EVAL, NV_3D_BIND_GROUP_VERTEX);
   if (gs)
      nvgpu_emit_cb0(ctx, push, MESA_SHADER_GEOMETRY, NV_3D_BIND_GROUP_VERTEX);
   nvgpu_emit_cb0(ctx, push, MESA_SHADER_FRAGMENT, NV_3D_BIND_GROUP_PIXEL);
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

   nvgpu_ensure_3d_init(ctx, &push);
   /* Program RTs/ZETA before CLEAR_SURFACE (required for HW to have a target) */
   nvgpu_emit_framebuffer(ctx, &push);
   nvgpu_emit_fixed_func(ctx, &push);
   nv_3d_emit_clear_surface(&push, buffers, color_ui, (float)depth, stencil);
   nv_push_wfi(&push);
   nvgpu_push_finish(ctx, &push, true);
}

static void
nvgpu_clear(struct pipe_context *pctx, unsigned buffers,
            uint32_t color_clear_mask, uint8_t stencil_clear_mask,
            const struct pipe_scissor_state *scissor_state,
            const union pipe_color_union *color,
            double depth, unsigned stencil)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   (void)scissor_state;
   (void)color_clear_mask;
   (void)stencil_clear_mask;
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

   for (i = 0; i < num_draws; i++) {
      uint32_t topo = NVC597_TOPOLOGY_TRIANGLES;
      uint32_t instance_count = info->instance_count ? info->instance_count : 1;
      uint32_t start_instance = info->start_instance;

      if (!nvgpu_push_start(ctx, &push, 512))
         return;

      if (class_3d)
         nv_3d_set_object(&push, class_3d);
      else
         nv_push_set_subch(&push, NV_PUSH_SUBCH_3D);

      nvgpu_ensure_3d_init(ctx, &push);
      nvgpu_emit_framebuffer(ctx, &push);
      nvgpu_emit_fixed_func(ctx, &push);
      nvgpu_emit_textures(ctx, &push);
      nvgpu_emit_shaders(ctx, &push);
      nvgpu_emit_vertex_state(ctx, &push);

      /* Map pipe primitive mode to NVC597 topology (subset) */
      switch (info->mode) {
      case MESA_PRIM_POINTS: topo = NVC597_TOPOLOGY_POINTS; break;
      case MESA_PRIM_LINES: topo = NVC597_TOPOLOGY_LINES; break;
      case MESA_PRIM_LINE_STRIP: topo = NVC597_TOPOLOGY_LINE_STRIP; break;
      case MESA_PRIM_TRIANGLES: topo = NVC597_TOPOLOGY_TRIANGLES; break;
      case MESA_PRIM_TRIANGLE_STRIP: topo = NVC597_TOPOLOGY_TRIANGLE_STRIP; break;
      case MESA_PRIM_TRIANGLE_FAN: topo = NVC597_TOPOLOGY_TRIANGLE_FAN; break;
      default: topo = NVC597_TOPOLOGY_TRIANGLES; break;
      }
      nv_3d_set_primitive_topology(&push, topo);
      if (info->primitive_restart)
         nv_3d_set_primitive_restart(&push, true, info->restart_index);
      else
         nv_3d_set_primitive_restart(&push, false, 0);

      if (indirect && indirect->buffer) {
         /* Indirect path A: host-map indirect resource; path B: CE-copy to
          * temporary host-mappable shadow (same pattern as nvrm indirect). */
         struct nvgpu_resource *ibuf = nvgpu_resource(indirect->buffer);
         uint32_t draw_count = indirect->draw_count ? indirect->draw_count : 1;
         uint32_t ind_stride = indirect->stride ? indirect->stride :
            (info->index_size ? 20u : 16u);
         const uint32_t *ind_base = NULL;
         void *ind_map = NULL;
         uint32_t ind_bytes;
         uint32_t stack_shadow[64]; /* up to 4 non-indexed or 3 indexed draws */
         uint32_t *heap_shadow = NULL;
         const struct nv_device_info *di = ctx->screen ? ctx->screen->info : NULL;
         uint32_t class_copy = di ? di->class_copy : 0;

         if (ibuf && indirect->buffer->width0) {
            ind_map = ibuf->cpu_ptr;
            if (ind_map)
               ind_base = (const uint32_t *)((const uint8_t *)ind_map +
                                             indirect->offset);
         }

         /* Path B: GPU-only indirect BO — CE linear copy into stack/heap, then
          * materialize normal draws (requires prior GPU writes flushed). */
         ind_bytes = draw_count * ind_stride;
         /* Path B: CE-copy into host-mappable indirect_shadow_bo, then read */
         if (!ind_base && ibuf && ind_bytes > 0) {
            nvgpu_ensure_indirect_shadow(ctx);
            if (ctx->indirect_shadow_map && ctx->indirect_shadow_bo &&
                ind_bytes <= ctx->indirect_shadow_bo_size && class_copy) {
               uint64_t src_addr = ibuf->gpu_offset + indirect->offset;
               uint64_t dst_addr = nv_rm_bo_gpu_offset(ctx->indirect_shadow_bo);
               nv_copy_indirect_to_shadow(&push, class_copy, src_addr, dst_addr,
                                          ind_bytes);
               ind_base = (const uint32_t *)ctx->indirect_shadow_map;
            } else if (ibuf->bo) {
               void *try_map = nv_rm_bo_map(ibuf->bo);
               if (try_map) {
                  ind_base = (const uint32_t *)((const uint8_t *)try_map +
                                                indirect->offset);
                  ind_map = try_map;
               }
            }
            (void)stack_shadow;
            (void)heap_shadow;
         }

         if (info->index_size && info->has_user_indices == false &&
             info->index.resource) {
            struct nvgpu_resource *ib = nvgpu_resource(info->index.resource);
            uint64_t ib_addr = ib ? ib->gpu_offset : 0;
            uint64_t ib_size = info->index.resource->width0;
            nv_3d_set_index_buffer(&push, ib_addr, ib_size, info->index_size);
         }

         if (ind_base) {
            if (info->index_size)
               nv_3d_emit_draw_indexed_indirect_multi(&push, topo, ind_base,
                                                      draw_count, ind_stride);
            else
               nv_3d_emit_draw_indirect_multi(&push, topo, ind_base,
                                              draw_count, ind_stride);
         }
         if (ind_map && ibuf && ind_map != ibuf->cpu_ptr)
            nv_rm_bo_unmap(ibuf->bo);
      } else if (info->index_size && info->has_user_indices == false &&
          info->index.resource) {
         struct nvgpu_resource *ib = nvgpu_resource(info->index.resource);
         uint64_t ib_addr = ib ? ib->gpu_offset : 0;
         uint64_t ib_size = info->index.resource->width0;
         int32_t bias = draws[i].index_bias;
         nv_3d_set_index_buffer(&push, ib_addr, ib_size, info->index_size);
         nv_3d_emit_draw_index_buffer_instanced(&push, topo, draws[i].start,
                                                draws[i].count, bias,
                                                instance_count, start_instance);
      } else {
         nv_3d_emit_draw_vertex_array_instanced(&push, topo, draws[i].start,
                                                draws[i].count, instance_count,
                                                start_instance);
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
   uint32_t class_copy = info ? info->class_copy : 0;
   (void)dst_level; (void)src_level; (void)dstz;

   if (!dres || !sres || !src_box)
      return;

   /* Buffer-to-buffer: linear 1D copy engine */
   if (dst->target == PIPE_BUFFER && src->target == PIPE_BUFFER) {
      saddr = sres->gpu_offset + (uint64_t)src_box->x;
      daddr = dres->gpu_offset + (uint64_t)dstx;
      size = (uint32_t)src_box->width;
      if (!size)
         return;
      if (!nvgpu_push_start(ctx, &push, 64))
         return;
      nv_copy_push_buffer_copy(&push, class_copy, saddr, daddr, size);
      nvgpu_push_finish(ctx, &push, true);
      return;
   }

   /* 2D textures: pitch multi-line or blocklinear NVC6B5 */
   if ((dst->target == PIPE_TEXTURE_2D || dst->target == PIPE_TEXTURE_RECT ||
        dst->target == PIPE_TEXTURE_2D_ARRAY) &&
       (src->target == PIPE_TEXTURE_2D || src->target == PIPE_TEXTURE_RECT ||
        src->target == PIPE_TEXTURE_2D_ARRAY) &&
       src_box->depth <= 1 && dst_level == 0 && src_level == 0) {
      unsigned bpp = util_format_get_blocksize(src->format);
      unsigned src_stride = align(util_format_get_stride(src->format, src->width0), 128);
      unsigned dst_stride = align(util_format_get_stride(dst->format, dst->width0), 128);
      uint32_t w = (uint32_t)src_box->width;
      uint32_t h = (uint32_t)src_box->height;
      bool src_bl = sres->blocklinear && !sres->linear;
      bool dst_bl = dres->blocklinear && !dres->linear;
      unsigned layer = src_box->z;
      uint32_t bl_h = sres->gobs_height ? sres->gobs_height : 4;

      if (!w || !h)
         return;

      if (sres->row_pitch)
         src_stride = sres->row_pitch;
      if (dres->row_pitch)
         dst_stride = dres->row_pitch;
      if (sres->bpp)
         bpp = sres->bpp;

      saddr = sres->gpu_offset;
      daddr = dres->gpu_offset;
      if (!src_bl) {
         saddr += (uint64_t)layer * src_stride * src->height0 +
                  (uint64_t)src_box->y * src_stride +
                  (uint64_t)src_box->x * bpp;
      }
      if (!dst_bl) {
         daddr += (uint64_t)dstz * dst_stride * dst->height0 +
                  (uint64_t)dsty * dst_stride +
                  (uint64_t)dstx * bpp;
      }

      if (!nvgpu_push_start(ctx, &push, 128))
         return;
      if (class_copy)
         nv_copy_set_object(&push, class_copy);
      else
         nv_push_set_subch(&push, NV_PUSH_SUBCH_COPY);
      if (src_bl || dst_bl) {
         nv_copy_emit_image_2d_bl(&push, saddr, daddr,
                                  w, h, bpp, src_stride, dst_stride,
                                  src_bl ? (uint32_t)src_box->x : 0,
                                  src_bl ? (uint32_t)src_box->y : 0,
                                  dst_bl ? (uint32_t)dstx : 0,
                                  dst_bl ? (uint32_t)dsty : 0,
                                  src_bl, dst_bl,
                                  sres->gobs_width, bl_h,
                                  dres->gobs_width,
                                  dres->gobs_height ? dres->gobs_height : 4);
         nv_push_wfi(&push);
      } else {
         nv_copy_emit_image_2d(&push, saddr, daddr,
                               w * bpp, src_stride, dst_stride, h);
         nv_push_wfi(&push);
      }
      nvgpu_push_finish(ctx, &push, true);
      return;
   }

   /* Fallback: software / blitter path */
   if (ctx->blitter) {
      util_resource_copy_region(pctx, dst, dst_level, dstx, dsty, dstz,
                                src, src_level, src_box);
   }
}

static void
nvgpu_flush(struct pipe_context *pctx, struct pipe_fence_handle **fence,
            unsigned flags)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   struct nv_push push;
   uint32_t seq = 0;

   if (!ctx->fence && ctx->screen->rm)
      ctx->fence = nv_fence_create(ctx->screen->rm);

   /* Emit sema signal so CPU can wait without only GPGet polling */
   if (ctx->fence && nvgpu_push_start(ctx, &push, 32)) {
      const struct nv_device_info *di = ctx->screen->info;
      if (di && di->class_3d)
         nv_3d_set_object(&push, di->class_3d);
      else
         nv_push_set_subch(&push, NV_PUSH_SUBCH_3D);
      nv_push_wfi(&push);
      seq = nv_fence_emit_3d_signal(ctx->fence, &push);
      /* Also host sema as belt-and-suspenders for engines that skip 3D report */
      if (!seq)
         seq = nv_fence_emit_host_signal(ctx->fence, &push);
      ctx->last_fence_seq = seq ? seq : ctx->fence->seq;
      nvgpu_push_finish(ctx, &push, true);
   } else if (ctx->channel) {
      if (ctx->channel->push_dw_used > ctx->channel->push_dw_base)
         nv_channel_kickoff(ctx->channel);
   }

   if (!(flags & PIPE_FLUSH_DEFERRED) && ctx->fence && ctx->last_fence_seq) {
      nv_fence_wait(ctx->fence, ctx->last_fence_seq, 100000000ull);
   } else if (!(flags & PIPE_FLUSH_DEFERRED) && ctx->channel) {
      nv_channel_wait_idle(ctx->channel, 100000000ull);
   }

   if (fence)
      *fence = (struct pipe_fence_handle *)(ctx->fence ? ctx->fence : NULL);
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

   for (s = 0; s < NVGPU_SHADER_STAGES; s++) {
      unsigned c;
      for (c = 0; c < PIPE_MAX_CONSTANT_BUFFERS; c++)
         pipe_resource_reference(&ctx->cb[s][c].buffer, NULL);
   }

   util_unreference_framebuffer_state(&ctx->fb);

   if (ctx->tex_pool)
      nv_tex_pool_destroy(ctx->tex_pool);
   if (ctx->lmem_bo)
      nv_rm_bo_free(ctx->lmem_bo);
   if (ctx->indirect_shadow_bo) {
      if (ctx->indirect_shadow_map)
         nv_rm_bo_unmap(ctx->indirect_shadow_bo);
      nv_rm_bo_free(ctx->indirect_shadow_bo);
   }
   if (ctx->fence)
      nv_fence_destroy(ctx->fence);
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
                        mesa_shader_stage shader,
                        unsigned start, unsigned num,
                        unsigned unbind_num_trailing_slots,
                        struct pipe_sampler_view **views)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   unsigned i;
   (void)unbind_num_trailing_slots;

   if (shader >= NVGPU_SHADER_STAGES)
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
                          mesa_shader_stage shader,
                          unsigned start, unsigned num, void **states)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   unsigned i;
   if (shader != MESA_SHADER_FRAGMENT)
      return;
   for (i = 0; i < num; i++) {
      unsigned slot = start + i;
      if (slot >= PIPE_MAX_SAMPLERS)
         break;
      ctx->sampler_cso[slot] = states ? states[i] : NULL;
   }
   if (start + num > ctx->num_sampler_cso)
      ctx->num_sampler_cso = start + num;
}

static void
nvgpu_delete_sampler_state(struct pipe_context *pctx, void *state)
{
   (void)pctx;
   FREE(state);
}


/* ---- blit: prefer HW copy; otherwise util_blitter if available ---- */

static void
nvgpu_blit(struct pipe_context *pctx, const struct pipe_blit_info *info)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   struct pipe_box box;

   if (!info || !info->src.resource || !info->dst.resource)
      return;

   /* Nearest, same-format, no scaling: use CE path via resource_copy_region */
   if (info->src.resource->format == info->dst.resource->format &&
       info->filter == PIPE_TEX_FILTER_NEAREST &&
       info->src.box.width == info->dst.box.width &&
       info->src.box.height == info->dst.box.height &&
       info->src.box.depth == info->dst.box.depth &&
       !(info->mask & PIPE_MASK_S)) {
      u_box_3d(info->src.box.x, info->src.box.y, info->src.box.z,
               info->src.box.width, info->src.box.height, info->src.box.depth,
               &box);
      nvgpu_resource_copy_region(pctx, info->dst.resource, info->dst.level,
                                 info->dst.box.x, info->dst.box.y, info->dst.box.z,
                                 info->src.resource, info->src.level, &box);
      return;
   }

   if (ctx->blitter && util_blitter_is_blit_supported(ctx->blitter, info)) {
      util_blitter_save_vertex_buffers(ctx->blitter, ctx->vb, ctx->num_vb);
      util_blitter_save_vertex_elements(ctx->blitter, ctx->velems);
      util_blitter_save_vertex_shader(ctx->blitter, ctx->vs);
      util_blitter_save_rasterizer(ctx->blitter, ctx->rs);
      util_blitter_save_viewport(ctx->blitter, &ctx->viewport);
      util_blitter_save_scissor(ctx->blitter, &ctx->scissor);
      util_blitter_save_fragment_shader(ctx->blitter, ctx->fs);
      util_blitter_save_blend(ctx->blitter, ctx->blend);
      util_blitter_save_depth_stencil_alpha(ctx->blitter, ctx->zsa);
      util_blitter_save_framebuffer(ctx->blitter, &ctx->fb);
      util_blitter_save_fragment_sampler_states(ctx->blitter,
         ctx->num_sampler_cso, (void **)ctx->sampler_cso);
      util_blitter_save_fragment_sampler_views(ctx->blitter,
         ctx->num_samplers[MESA_SHADER_FRAGMENT],
         ctx->samplers[MESA_SHADER_FRAGMENT]);
      util_blitter_blit(ctx->blitter, info, NULL);
   }
}

/* ---- compute launch_grid via QMD (mirrors Vulkan nvrm compute path) ---- */

static void
nvgpu_launch_grid(struct pipe_context *pctx,
                  const struct pipe_grid_info *info)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   const struct nv_device_info *di = ctx->screen->info;
   struct nv_qmd_desc desc;
   struct nv_push push;
   struct nvgpu_shader_cso *cs = ctx->cs;
   uint32_t class_compute = di ? di->class_compute : 0;
   uint64_t prog = 0;
   uint32_t regs = 16;
   uint32_t gx, gy, gz;
   uint32_t cta_x = 1, cta_y = 1, cta_z = 1;
   uint8_t sass_ver = 0x50;

   if (!info)
      return;
   if (!nvgpu_push_start(ctx, &push, 256))
      return;

   if (cs && cs->nvsh) {
      nvgpu_ensure_shader_uploaded(ctx, cs);
      prog = cs->nvsh->code_gpu_addr;
      if (cs->nvsh->register_count)
         regs = cs->nvsh->register_count;
   }

   gx = info->grid[0] ? info->grid[0] : 1;
   gy = info->grid[1] ? info->grid[1] : 1;
   gz = info->grid[2] ? info->grid[2] : 1;
   cta_x = info->block[0] ? info->block[0] : 1;
   cta_y = info->block[1] ? info->block[1] : 1;
   cta_z = info->block[2] ? info->block[2] : 1;

   /* Indirect compute: host-read indirect resource if mapped (path A) */
   if (info->indirect) {
      struct nvgpu_resource *ib = nvgpu_resource(info->indirect);
      const uint32_t *ind = NULL;
      if (ib && ib->cpu_ptr)
         ind = (const uint32_t *)((const uint8_t *)ib->cpu_ptr +
                                 info->indirect_offset);
      if (ind) {
         gx = ind[0] ? ind[0] : 1;
         gy = ind[1] ? ind[1] : 1;
         gz = ind[2] ? ind[2] : 1;
      }
   }

   if (di && di->sm_version)
      sass_ver = (uint8_t)(di->sm_version & 0xff);

   memset(&desc, 0, sizeof(desc));
   desc.program_addr = prog;
   desc.grid_x = gx;
   desc.grid_y = gy;
   desc.grid_z = gz;
   desc.cta_x = cta_x;
   desc.cta_y = cta_y;
   desc.cta_z = cta_z;
   desc.register_count = regs;
   desc.shared_mem_size = nv_qmd_align_shared_mem(
      cs && cs->nvsh ? 0 : 0); /* shared from NIR when tracked */
   desc.barrier_count = nv_qmd_default_barrier_count(cta_x, cta_y, cta_z);
   desc.sass_version = sass_ver;
   desc.sm_global_caching = true;
   desc.invalidate_caches = true;
   if (cs && cs->nvsh && cs->nvsh->local_mem_size)
      desc.local_mem_low = cs->nvsh->local_mem_size;

   /* Ensure compute LMEM BO exists and program SET_SHADER_LOCAL_MEMORY* once */
   if (!ctx->lmem_bo && ctx->screen && ctx->screen->rm) {
      struct nv_rm_bo_req req;
      uint32_t sm_count = 2;
      uint64_t lmem_need;
      if (di && di->tpc_count)
         sm_count = di->tpc_count;
      lmem_need = nv_lmem_total_bo_bytes(desc.local_mem_low, sm_count);
      if (lmem_need > 0x10000000ull)
         lmem_need = 0x10000000ull;
      if (lmem_need < 0x10000ull)
         lmem_need = 0x10000ull;
      memset(&req, 0, sizeof(req));
      req.size = (uint32_t)lmem_need;
      req.alignment = 4096;
      req.vram = true;
      req.cpu_access = false;
      req.no_scanout = true;
      req.map_gpu_va = true;
      ctx->lmem_bo = nv_rm_bo_alloc(ctx->screen->rm, &req);
      if (ctx->lmem_bo)
         ctx->lmem_bo_size = (uint32_t)lmem_need;
   }
   if (ctx->lmem_bo && !ctx->lmem_programmed) {
      uint64_t lmem_addr = nv_rm_bo_gpu_offset(ctx->lmem_bo);
      uint32_t sm_count = (di && di->tpc_count) ? di->tpc_count : 1;
      if (lmem_addr) {
         nv_compute_set_shader_local_memory_for_shader(&push, lmem_addr,
                                                      desc.local_mem_low,
                                                      sm_count);
         ctx->lmem_programmed = true;
      }
   }

   nv_compute_emit_dispatch(&push, &desc, 0, class_compute);
   nv_push_wfi(&push);
   nvgpu_push_finish(ctx, &push, true);
}

static void *
nvgpu_create_compute_state(struct pipe_context *pctx,
                           const struct pipe_compute_state *cso)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   struct nvgpu_shader_cso *scso;
   struct pipe_shader_state sh;

   if (!cso)
      return NULL;
   scso = CALLOC_STRUCT(nvgpu_shader_cso);
   if (!scso)
      return NULL;
   memset(&sh, 0, sizeof(sh));
   sh.type = cso->ir_type;
   if (cso->ir_type == PIPE_SHADER_IR_NIR)
      sh.ir.nir = (void *)cso->prog;
   scso->base = sh;
   scso->nvsh = nv_shader_create(ctx->screen->rm, NV_SHADER_KIND_COMPUTE);
   if (!scso->nvsh) {
      FREE(scso);
      return NULL;
   }
   if (cso->ir_type == PIPE_SHADER_IR_NIR && cso->prog)
      nv_shader_set_nir(scso->nvsh, (struct nir_shader *)cso->prog, false);
   nvgpu_ensure_shader_uploaded(ctx, scso);
   return scso;
}

static void
nvgpu_bind_compute_state(struct pipe_context *pctx, void *state)
{
   nvgpu_context(pctx)->cs = state;
}

static void
nvgpu_set_shader_buffers(struct pipe_context *pctx,
                         mesa_shader_stage shader,
                         unsigned start_slot, unsigned count,
                         const struct pipe_shader_buffer *buffers,
                         unsigned unbind_num_trailing_slots)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   unsigned i, total;
   (void)shader; /* single shared SSBO table; compute/FS share for now */
   if (start_slot >= PIPE_MAX_SHADER_BUFFERS)
      return;
   total = count + unbind_num_trailing_slots;
   if (start_slot + total > PIPE_MAX_SHADER_BUFFERS)
      total = PIPE_MAX_SHADER_BUFFERS - start_slot;
   for (i = 0; i < total; i++) {
      unsigned s = start_slot + i;
      if (i < count && buffers && buffers[i].buffer) {
         pipe_resource_reference(&ctx->ssbo[s].buffer, buffers[i].buffer);
         ctx->ssbo[s].buffer_offset = buffers[i].buffer_offset;
         ctx->ssbo[s].buffer_size = buffers[i].buffer_size;
      } else {
         pipe_resource_reference(&ctx->ssbo[s].buffer, NULL);
         ctx->ssbo[s].buffer_offset = 0;
         ctx->ssbo[s].buffer_size = 0;
      }
   }
   if (start_slot + total > ctx->num_ssbo)
      ctx->num_ssbo = start_slot + total;
}

static void
nvgpu_set_shader_images(struct pipe_context *pctx,
                        mesa_shader_stage shader,
                        unsigned start_slot, unsigned count,
                        unsigned unbind_num_trailing_slots,
                        const struct pipe_image_view *images)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   unsigned i, total;
   (void)shader;
   if (start_slot >= PIPE_MAX_SHADER_IMAGES)
      return;
   total = count + unbind_num_trailing_slots;
   if (start_slot + total > PIPE_MAX_SHADER_IMAGES)
      total = PIPE_MAX_SHADER_IMAGES - start_slot;
   for (i = 0; i < total; i++) {
      unsigned s = start_slot + i;
      if (i < count && images && images[i].resource) {
         ctx->images[s] = images[i];
         pipe_resource_reference(&ctx->images[s].resource, images[i].resource);
      } else {
         pipe_resource_reference(&ctx->images[s].resource, NULL);
         memset(&ctx->images[s], 0, sizeof(ctx->images[s]));
      }
   }
   if (start_slot + total > ctx->num_images)
      ctx->num_images = start_slot + total;
}

static void
nvgpu_set_global_binding(struct pipe_context *pctx,
                         unsigned first, unsigned count,
                         struct pipe_resource **resources,
                         uint32_t **handles)
{
   struct pipe_shader_buffer tmp[PIPE_MAX_SHADER_BUFFERS];
   unsigned i, n = count;
   (void)handles;
   if (first >= PIPE_MAX_SHADER_BUFFERS)
      return;
   if (first + n > PIPE_MAX_SHADER_BUFFERS)
      n = PIPE_MAX_SHADER_BUFFERS - first;
   memset(tmp, 0, sizeof(tmp));
   if (resources) {
      for (i = 0; i < n; i++) {
         if (!resources[i])
            continue;
         tmp[i].buffer = resources[i];
         tmp[i].buffer_offset = 0;
         tmp[i].buffer_size = resources[i]->width0;
      }
   }
   nvgpu_set_shader_buffers(pctx, MESA_SHADER_COMPUTE, first, n,
                            resources ? tmp : NULL, 0);
}

static void
nvgpu_memory_barrier(struct pipe_context *pctx, unsigned flags)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   struct nv_push push;
   bool sh_d = false, sh_c = false, sh_i = false;
   bool tx_s = false, tx_h = false, tx_d = false;

   if (!nvgpu_push_start(ctx, &push, 32))
      return;
   if (flags & (PIPE_BARRIER_SHADER_BUFFER | PIPE_BARRIER_CONSTANT_BUFFER |
                PIPE_BARRIER_INDIRECT_BUFFER | PIPE_BARRIER_VERTEX_BUFFER |
                PIPE_BARRIER_INDEX_BUFFER | PIPE_BARRIER_STREAMOUT_BUFFER |
                PIPE_BARRIER_GLOBAL_BUFFER | PIPE_BARRIER_UPDATE_BUFFER)) {
      sh_d = sh_c = true;
   }
   if (flags & (PIPE_BARRIER_TEXTURE | PIPE_BARRIER_FRAMEBUFFER |
                PIPE_BARRIER_IMAGE | PIPE_BARRIER_UPDATE_BUFFER |
                PIPE_BARRIER_UPDATE_TEXTURE)) {
      tx_s = tx_h = tx_d = true;
   }
   if (flags & PIPE_BARRIER_MAPPED_BUFFER)
      sh_d = true;
   nv_3d_emit_memory_barrier(&push, sh_i, sh_d, sh_c, tx_s, tx_h, tx_d);
   nvgpu_push_finish(ctx, &push, true);
}

static void
nvgpu_texture_barrier(struct pipe_context *pctx, unsigned flags)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   struct nv_push push;
   (void)flags;
   if (!nvgpu_push_start(ctx, &push, 16))
      return;
   nv_3d_emit_texture_barrier(&push);
   nvgpu_push_finish(ctx, &push, true);
}

/* ---- queries (occlusion / timestamp via REPORT_SEMAPHORE) ---- */

static struct pipe_query *
nvgpu_create_query(struct pipe_context *pctx, unsigned query_type, unsigned index)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   struct nvgpu_query *q;
   struct nv_rm_bo_req req;
   (void)index;

   if (query_type != PIPE_QUERY_OCCLUSION_COUNTER &&
       query_type != PIPE_QUERY_OCCLUSION_PREDICATE &&
       query_type != PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE &&
       query_type != PIPE_QUERY_TIMESTAMP &&
       query_type != PIPE_QUERY_TIME_ELAPSED &&
       query_type != PIPE_QUERY_GPU_FINISHED)
      return NULL;
   if (!ctx->screen || !ctx->screen->rm)
      return NULL;

   q = CALLOC_STRUCT(nvgpu_query);
   if (!q)
      return NULL;
   q->type = query_type;
   memset(&req, 0, sizeof(req));
   req.size = 64; /* 4-word report + padding */
   req.alignment = 256;
   req.vram = false;
   req.cpu_access = true;
   req.no_scanout = true;
   req.map_gpu_va = true;
   q->bo = nv_rm_bo_alloc(ctx->screen->rm, &req);
   if (!q->bo) {
      FREE(q);
      return NULL;
   }
   q->map = nv_rm_bo_map(q->bo);
   q->gpu_addr = nv_rm_bo_gpu_offset(q->bo);
   if (q->map)
      memset(q->map, 0, 64);
   return (struct pipe_query *)q;
}

static void
nvgpu_destroy_query(struct pipe_context *pctx, struct pipe_query *pq)
{
   struct nvgpu_query *q = (struct nvgpu_query *)pq;
   (void)pctx;
   if (!q)
      return;
   if (q->bo) {
      if (q->map)
         nv_rm_bo_unmap(q->bo);
      nv_rm_bo_free(q->bo);
   }
   FREE(q);
}

static bool
nvgpu_begin_query(struct pipe_context *pctx, struct pipe_query *pq)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   struct nvgpu_query *q = (struct nvgpu_query *)pq;
   struct nv_push push;

   if (!q)
      return false;
   if (q->map)
      memset(q->map, 0, 64);
   q->result = 0;
   q->result_ready = false;
   q->active = true;

   if (q->type == PIPE_QUERY_OCCLUSION_COUNTER ||
       q->type == PIPE_QUERY_OCCLUSION_PREDICATE ||
       q->type == PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE) {
      if (!nvgpu_push_start(ctx, &push, 16))
         return false;
      nv_3d_set_zpass_pixel_count(&push, true);
      nvgpu_push_finish(ctx, &push, false);
      ctx->active_occlusion = pq;
   }
   return true;
}

static bool
nvgpu_end_query(struct pipe_context *pctx, struct pipe_query *pq)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   struct nvgpu_query *q = (struct nvgpu_query *)pq;
   struct nv_push push;
   bool zpass;

   if (!q || !q->gpu_addr)
      return false;
   q->active = false;
   zpass = (q->type == PIPE_QUERY_OCCLUSION_COUNTER ||
            q->type == PIPE_QUERY_OCCLUSION_PREDICATE ||
            q->type == PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE);

   if (!nvgpu_push_start(ctx, &push, 32))
      return false;
   if (zpass) {
      nv_3d_report_query_release(&push, q->gpu_addr, 1, true, false);
      nv_3d_set_zpass_pixel_count(&push, false);
      if (ctx->active_occlusion == pq)
         ctx->active_occlusion = NULL;
   } else {
      /* timestamp / gpu_finished: non-zpass report release */
      nv_3d_report_query_release(&push, q->gpu_addr, 1, false, false);
   }
   nvgpu_push_finish(ctx, &push, true);

   /* Host snapshot when mapped (path A); GPU may still be writing — poll in get */
   if (q->map) {
      volatile uint32_t *dw = q->map;
      q->result = dw[0];
      if (dw[0] || dw[1] || dw[2] || dw[3])
         q->result_ready = true;
   }
   return true;
}

static bool
nvgpu_get_query_result(struct pipe_context *pctx, struct pipe_query *pq,
                       bool wait, union pipe_query_result *result)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   struct nvgpu_query *q = (struct nvgpu_query *)pq;
   volatile uint32_t *dw;
   unsigned spins;

   if (!q || !result)
      return false;

   if (q->map) {
      dw = q->map;
      /* Fast path: already written by GPU */
      if (dw[0] || dw[1] || dw[2] || dw[3]) {
         q->result = ((uint64_t)dw[1] << 32) | dw[0];
         q->result_ready = true;
      } else if (wait && q->gpu_addr) {
         /* GPU-side acquire on report sema (payload 1 written by end_query),
          * then WFI so subsequent host read sees coherent data. */
         struct nv_push push;
         if (nvgpu_push_start(ctx, &push, 48)) {
            const struct nv_device_info *di = ctx->screen ? ctx->screen->info : NULL;
            if (di && di->class_3d)
               nv_3d_set_object(&push, di->class_3d);
            else
               nv_push_set_subch(&push, NV_PUSH_SUBCH_3D);
            nv_3d_report_semaphore_acquire(&push, q->gpu_addr, 1, false);
            nv_push_wfi(&push);
            nvgpu_push_finish(ctx, &push, true);
         }
         /* Host poll with fence wait as belt-and-suspenders */
         if (ctx->fence && ctx->last_fence_seq)
            nv_fence_wait(ctx->fence, ctx->last_fence_seq, 2000000000ull);
         else if (ctx->channel)
            nv_channel_wait_idle(ctx->channel, 2000000000ull);
         for (spins = 0; spins < 1000000 && !dw[0] && !dw[1]; spins++)
            ;
         q->result = ((uint64_t)dw[1] << 32) | dw[0];
         q->result_ready = (dw[0] || dw[1] || dw[2] || dw[3]);
      } else if (!wait) {
         return false; /* not ready */
      }
   }

   switch (q->type) {
   case PIPE_QUERY_OCCLUSION_COUNTER:
      result->u64 = q->result;
      break;
   case PIPE_QUERY_OCCLUSION_PREDICATE:
   case PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE:
      result->b = q->result != 0;
      break;
   case PIPE_QUERY_TIMESTAMP:
   case PIPE_QUERY_TIME_ELAPSED:
      result->u64 = q->result;
      break;
   case PIPE_QUERY_GPU_FINISHED:
      result->b = q->result_ready || q->result != 0;
      break;
   default:
      result->u64 = q->result;
      break;
   }
   return true;
}

static void
nvgpu_render_condition(struct pipe_context *pctx, struct pipe_query *pq,
                       bool condition, enum pipe_render_cond_flag mode)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   struct nvgpu_query *q = (struct nvgpu_query *)pq;
   struct nv_push push;
   (void)mode;
   if (!pq) {
      ctx->cond_render_active = false;
      ctx->cond_render_inverted = false;
      ctx->cond_render_gpu_addr = 0;
      if (nvgpu_push_start(ctx, &push, 16)) {
         nv_3d_clear_conditional_render(&push);
         nvgpu_push_finish(ctx, &push, true);
      }
      return;
   }
   ctx->cond_render_active = true;
   ctx->cond_render_inverted = condition;
   if (q && q->gpu_addr)
      ctx->cond_render_gpu_addr = q->gpu_addr;
   if (ctx->cond_render_gpu_addr && nvgpu_push_start(ctx, &push, 32)) {
      nv_3d_set_conditional_render(&push, ctx->cond_render_gpu_addr,
                                   ctx->cond_render_inverted);
      nvgpu_push_finish(ctx, &push, true);
   }
}

static void
nvgpu_clear_buffer(struct pipe_context *pctx, struct pipe_resource *pres,
                   unsigned offset, unsigned size, const void *clear_value,
                   int clear_value_size)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   struct nvgpu_resource *res = nvgpu_resource(pres);
   const struct nv_device_info *di;
   struct nv_push push;
   uint64_t addr;
   uint32_t fill_data = 0;
   uint32_t class_copy = 0;
   unsigned i;

   if (!pres || !res || size == 0)
      return;
   addr = res->gpu_offset + offset;
   di = ctx->screen ? ctx->screen->info : NULL;
   class_copy = di ? di->class_copy : 0;
   if (clear_value && clear_value_size > 0) {
      const uint8_t *cv = clear_value;
      for (i = 0; i < 4 && i < (unsigned)clear_value_size; i++)
         fill_data |= ((uint32_t)cv[i]) << (8 * i);
      if (clear_value_size == 2)
         fill_data = (fill_data & 0xffffu) | ((fill_data & 0xffffu) << 16);
      else if (clear_value_size == 1) {
         fill_data &= 0xffu;
         fill_data |= fill_data << 8;
         fill_data |= fill_data << 16;
      }
   }
   if (!nvgpu_push_start(ctx, &push, 64))
      return;
   nv_copy_push_remap_fill_u32(&push, class_copy, addr, size & ~3u, fill_data);
   nvgpu_push_finish(ctx, &push, true);
}

/* ---- stream output (transform feedback) ---- */

static void
nvgpu_set_stream_output_targets(struct pipe_context *pctx,
                                unsigned num_targets,
                                struct pipe_stream_output_target **targets,
                                const unsigned *offsets,
                                enum mesa_prim output_prim)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   struct nv_push push;
   const struct nv_device_info *di;
   unsigned i;
   (void)output_prim;

   /* Release previous targets */
   for (i = 0; i < ctx->num_so_targets; i++) {
      if (ctx->so_targets[i])
         pipe_so_target_reference(&ctx->so_targets[i], NULL);
   }
   ctx->num_so_targets = 0;
   ctx->so_append_bitmask = 0;
   ctx->so_enabled = false;

   if (!num_targets || !targets) {
      if (nvgpu_push_start(ctx, &push, 32)) {
         di = ctx->screen ? ctx->screen->info : NULL;
         if (di && di->class_3d)
            nv_3d_set_object(&push, di->class_3d);
         else
            nv_push_set_subch(&push, NV_PUSH_SUBCH_3D);
         nv_3d_stream_out_disable_all(&push);
         nvgpu_push_finish(ctx, &push, true);
      }
      return;
   }

   if (!nvgpu_push_start(ctx, &push, 256))
      return;
   di = ctx->screen ? ctx->screen->info : NULL;
   if (di && di->class_3d)
      nv_3d_set_object(&push, di->class_3d);
   else
      nv_push_set_subch(&push, NV_PUSH_SUBCH_3D);

   /* Collect buffer addresses; build layout from last active VS/GS stream_output */
   {
      struct nvgpu_shader_cso *so_src = ctx->gs ? ctx->gs :
                                       (ctx->tes ? ctx->tes : ctx->vs);
      const struct pipe_stream_output_info *soi = NULL;
      uint64_t addrs[4] = { 0, 0, 0, 0 };
      uint32_t sizes[4] = { 0, 0, 0, 0 };
      unsigned strides[4] = { 0, 0, 0, 0 };
      uint8_t oreg[PIPE_MAX_SO_OUTPUTS];
      uint8_t obuf[PIPE_MAX_SO_OUTPUTS];
      uint8_t odst[PIPE_MAX_SO_OUTPUTS];
      uint8_t oncomp[PIPE_MAX_SO_OUTPUTS];
      uint8_t ostream[PIPE_MAX_SO_OUTPUTS];
      unsigned nout = 0;
      unsigned append_mask = 0;
      unsigned j;

      if (so_src)
         soi = &so_src->base.stream_output;

      for (i = 0; i < 4; i++) {
         if (i >= num_targets || !targets[i])
            continue;
         struct pipe_stream_output_target *t = targets[i];
         struct nvgpu_resource *res = nvgpu_resource(t->buffer);
         pipe_so_target_reference(&ctx->so_targets[i], t);
         if (res)
            addrs[i] = res->gpu_offset + t->buffer_offset;
         sizes[i] = t->buffer_size;
         if (offsets && offsets[i] == (unsigned)-1)
            append_mask |= (1u << i);
         if (soi && i < 4)
            strides[i] = soi->stride[i] * 4u; /* stride is in dwords */
         ctx->num_so_targets = i + 1;
         ctx->so_enabled = true;
      }
      ctx->so_append_bitmask = append_mask;

      if (soi && soi->num_outputs > 0) {
         nout = soi->num_outputs;
         if (nout > PIPE_MAX_SO_OUTPUTS)
            nout = PIPE_MAX_SO_OUTPUTS;
         for (j = 0; j < nout; j++) {
            oreg[j] = (uint8_t)soi->output[j].register_index;
            obuf[j] = (uint8_t)soi->output[j].output_buffer;
            odst[j] = (uint8_t)soi->output[j].dst_offset;
            oncomp[j] = (uint8_t)soi->output[j].num_components;
            ostream[j] = (uint8_t)soi->output[j].stream;
         }
         nv_3d_stream_out_emit_from_pipe_info(&push, oreg, obuf, odst, oncomp,
                                              ostream, nout, strides, 4,
                                              addrs, sizes, append_mask);
      } else {
         /* No shader xfb info: simple sequential layout per active target */
         for (i = 0; i < 4; i++) {
            if (!addrs[i] || !sizes[i]) {
               nv_3d_set_stream_out_buffer(&push, i, 0, 0);
               continue;
            }
            nv_3d_stream_out_setup_simple(&push, i, addrs[i], sizes[i],
                                          0, 4, strides[i] ? strides[i] : 16);
            if (!(append_mask & (1u << i)))
               nv_3d_set_stream_out_buffer_write_pointer(&push, i, 0);
         }
      }
   }

   nv_3d_set_stream_output_enable(&push, ctx->so_enabled);
   nvgpu_push_finish(ctx, &push, true);
}

static void
nvgpu_clear_texture(struct pipe_context *pctx, struct pipe_resource *pres,
                    unsigned level, const struct pipe_box *box,
                    const void *data)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   struct nvgpu_resource *res = nvgpu_resource(pres);
   const struct nv_device_info *di;
   struct nv_push push;
   uint64_t addr;
   uint32_t fill_data = 0;
   uint32_t class_copy = 0;
   unsigned bpp, layer_stride, size;

   if (!pres || !res || !box)
      return;
   /* Linear/BO clear via CE REMAP for level 0 full/subrect when possible */
   bpp = util_format_get_blocksize(pres->format);
   if (!bpp)
      bpp = 4;
   if (data && bpp <= 4) {
      memcpy(&fill_data, data, bpp);
      if (bpp == 1) {
         fill_data &= 0xffu;
         fill_data |= fill_data << 8;
         fill_data |= fill_data << 16;
      } else if (bpp == 2)
         fill_data = (fill_data & 0xffffu) | ((fill_data & 0xffffu) << 16);
   }
   layer_stride = (unsigned)pres->width0 * bpp;
   if (pres->target != PIPE_BUFFER && level == 0 && box->x == 0 && box->y == 0 &&
       box->width == (int)pres->width0 && box->height == (int)pres->height0) {
      size = layer_stride * (unsigned)box->height * (unsigned)MAX2(box->depth, 1);
      addr = res->gpu_offset;
      di = ctx->screen ? ctx->screen->info : NULL;
      class_copy = di ? di->class_copy : 0;
      if (nvgpu_push_start(ctx, &push, 64)) {
         nv_copy_push_remap_fill_u32(&push, class_copy, addr, size & ~3u,
                                     fill_data);
         nvgpu_push_finish(ctx, &push, true);
         return;
      }
   }
   /* Fallback: software clear via transfer (slow path) */
   util_clear_texture_sw(pctx, pres, level, box, data);
}

static void
nvgpu_get_sample_position(struct pipe_context *pctx, unsigned sample_count,
                          unsigned sample_index, float *out_value)
{
   (void)pctx;
   /* Standard MSAA positions (subset; matches common GL defaults) */
   static const float pos1[1][2] = { { 0.5f, 0.5f } };
   static const float pos2[2][2] = { { 0.25f, 0.25f }, { 0.75f, 0.75f } };
   static const float pos4[4][2] = {
      { 0.375f, 0.125f }, { 0.875f, 0.375f },
      { 0.125f, 0.625f }, { 0.625f, 0.875f }
   };
   const float (*tbl)[2] = pos1;
   unsigned n = 1;
   if (sample_count >= 4) {
      tbl = pos4;
      n = 4;
   } else if (sample_count >= 2) {
      tbl = pos2;
      n = 2;
   }
   if (sample_index >= n)
      sample_index = 0;
   out_value[0] = tbl[sample_index][0];
   out_value[1] = tbl[sample_index][1];
}

static uint64_t
nvgpu_get_timestamp(struct pipe_context *pctx)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   struct pipe_query *pq;
   union pipe_query_result qr;
   uint64_t ts = 0;

   pq = nvgpu_create_query(pctx, PIPE_QUERY_TIMESTAMP, 0);
   if (!pq)
      return 0;
   if (nvgpu_begin_query(pctx, pq))
      nvgpu_end_query(pctx, pq);
   if (nvgpu_get_query_result(pctx, pq, true, &qr))
      ts = qr.u64;
   nvgpu_destroy_query(pctx, pq);
   (void)ctx;
   return ts;
}

static void
nvgpu_flush_resource(struct pipe_context *pctx, struct pipe_resource *pres)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   struct nv_push push;
   (void)pres;
   /* Ensure GPU writes to resource are ordered: WFI + texture/memory barrier */
   if (!nvgpu_push_start(ctx, &push, 32))
      return;
   nv_push_wfi(&push);
   nv_3d_emit_memory_barrier(&push, true, true, true, false, false, false);
   nv_3d_emit_texture_barrier(&push);
   nvgpu_push_finish(ctx, &push, true);
}

static void
nvgpu_invalidate_resource(struct pipe_context *pctx, struct pipe_resource *pres)
{
   struct nvgpu_context *ctx = nvgpu_context(pctx);
   struct nv_push push;
   (void)pres;
   if (!nvgpu_push_start(ctx, &push, 16))
      return;
   nv_3d_emit_texture_barrier(&push);
   nvgpu_push_finish(ctx, &push, false);
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
   ctx->base.buffer_unmap = nvgpu_transfer_unmap;
   ctx->base.texture_unmap = nvgpu_transfer_unmap;
   ctx->base.buffer_subdata = u_default_buffer_subdata;
   ctx->base.texture_subdata = u_default_texture_subdata;

   ctx->base.set_framebuffer_state = nvgpu_set_framebuffer_state;
   ctx->base.set_viewport_states = nvgpu_set_viewport_states;
   ctx->base.set_scissor_states = nvgpu_set_scissor_states;
   ctx->base.set_stencil_ref = nvgpu_set_stencil_ref;
   ctx->base.set_blend_color = nvgpu_set_blend_color;
   ctx->base.set_sample_mask = nvgpu_set_sample_mask;
   ctx->base.set_min_samples = nvgpu_set_min_samples;
   ctx->base.set_vertex_buffers = nvgpu_set_vertex_buffers;
   ctx->base.set_constant_buffer = nvgpu_set_constant_buffer;
   ctx->base.set_sampler_views = nvgpu_set_sampler_views;
   ctx->sample_mask = ~0u;

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
   ctx->base.create_gs_state = nvgpu_create_gs_state;
   ctx->base.bind_gs_state = nvgpu_bind_gs_state;
   ctx->base.delete_gs_state = nvgpu_delete_shader_state;
   ctx->base.create_tcs_state = nvgpu_create_tcs_state;
   ctx->base.bind_tcs_state = nvgpu_bind_tcs_state;
   ctx->base.delete_tcs_state = nvgpu_delete_shader_state;
   ctx->base.create_tes_state = nvgpu_create_tes_state;
   ctx->base.bind_tes_state = nvgpu_bind_tes_state;
   ctx->base.delete_tes_state = nvgpu_delete_shader_state;
   ctx->base.create_vertex_elements_state = nvgpu_create_vertex_elements_state;
   ctx->base.bind_vertex_elements_state = nvgpu_bind_vertex_elements_state;
   ctx->base.delete_vertex_elements_state = nvgpu_delete_vertex_elements_state;
   ctx->base.create_sampler_state = nvgpu_create_sampler_state;
   ctx->base.bind_sampler_states = nvgpu_bind_sampler_states;
   ctx->base.delete_sampler_state = nvgpu_delete_sampler_state;
   ctx->base.create_sampler_view = nvgpu_create_sampler_view;
   ctx->base.sampler_view_destroy = nvgpu_sampler_view_destroy;

   ctx->base.resource_copy_region = nvgpu_resource_copy_region;
   ctx->base.blit = nvgpu_blit;
   ctx->base.create_compute_state = nvgpu_create_compute_state;
   ctx->base.bind_compute_state = nvgpu_bind_compute_state;
   ctx->base.delete_compute_state = nvgpu_delete_shader_state;
   ctx->base.set_global_binding = nvgpu_set_global_binding;
   ctx->base.set_shader_buffers = nvgpu_set_shader_buffers;
   ctx->base.set_shader_images = nvgpu_set_shader_images;
   ctx->base.memory_barrier = nvgpu_memory_barrier;
   ctx->base.texture_barrier = nvgpu_texture_barrier;
   ctx->base.render_condition = nvgpu_render_condition;
   ctx->base.create_query = nvgpu_create_query;
   ctx->base.destroy_query = nvgpu_destroy_query;
   ctx->base.begin_query = nvgpu_begin_query;
   ctx->base.end_query = nvgpu_end_query;
   ctx->base.get_query_result = nvgpu_get_query_result;
   ctx->base.clear_buffer = nvgpu_clear_buffer;
   ctx->base.clear_texture = nvgpu_clear_texture;
   ctx->base.set_stream_output_targets = nvgpu_set_stream_output_targets;
   ctx->base.get_sample_position = nvgpu_get_sample_position;
   ctx->base.get_timestamp = nvgpu_get_timestamp;
   ctx->base.flush_resource = nvgpu_flush_resource;
   ctx->base.invalidate_resource = nvgpu_invalidate_resource;
   ctx->base.launch_grid = nvgpu_launch_grid;

   nvgpu_ensure_channel(ctx);
   if (screen->rm)
      ctx->fence = nv_fence_create(screen->rm);
   /* Software blitter for scaled/filtered blits (uses driver shaders/CSOs) */
   ctx->blitter = util_blitter_create(&ctx->base);
   return &ctx->base;
}
