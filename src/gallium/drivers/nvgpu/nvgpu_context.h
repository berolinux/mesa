/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 */
#ifndef NVGPU_CONTEXT_H
#define NVGPU_CONTEXT_H

#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "util/u_blitter.h"
#include "util/u_threaded_context.h"

struct nvgpu_screen;
struct nv_rm_bo;
struct nv_push;
struct nv_channel;

#define NVGPU_PUSH_DWORDS  (64 * 1024)

struct nvgpu_context {
   struct pipe_context base;
   struct nvgpu_screen *screen;

   /* GPFIFO channel (preferred submit path) */
   struct nv_channel *channel;

   /* Legacy direct pushbuffer BO (fallback if channel alloc fails) */
   struct nv_rm_bo *push_bo;
   uint32_t *push_map;
   uint32_t push_dw_size;
   uint32_t push_dw_used;

   struct blitter_context *blitter;
   struct pipe_debug_callback debug;

   /* Bound state (minimal) */
   void *fs;
   void *vs;
   void *blend;
   void *zsa;
   void *rs;
   void *velems;
   struct pipe_vertex_buffer vb[PIPE_MAX_ATTRIBS];
   unsigned num_vb;
   struct pipe_constant_buffer cb[PIPE_SHADER_TYPES][PIPE_MAX_CONSTANT_BUFFERS];
   struct pipe_sampler_view *samplers[PIPE_SHADER_TYPES][PIPE_MAX_SAMPLERS];
   unsigned num_samplers[PIPE_SHADER_TYPES];
   struct pipe_framebuffer_state fb;
   struct pipe_viewport_state viewport;
   struct pipe_scissor_state scissor;
};

static inline struct nvgpu_context *
nvgpu_context(struct pipe_context *pctx)
{
   return (struct nvgpu_context *)pctx;
}

struct pipe_context *nvgpu_context_create(struct pipe_screen *pscreen,
                                          void *priv, unsigned flags);

#endif
