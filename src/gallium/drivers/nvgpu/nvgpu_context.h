/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 */
#ifndef NVGPU_CONTEXT_H
#define NVGPU_CONTEXT_H

#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "pipe/p_defines.h"
#include "compiler/shader_enums.h"
#include "util/u_blitter.h"
#include "util/u_threaded_context.h"

/* Mesa tip uses MESA_SHADER_STAGES; keep local alias for CB/sampler arrays. */
#ifndef NVGPU_SHADER_STAGES
#define NVGPU_SHADER_STAGES MESA_SHADER_STAGES
#endif

struct nvgpu_screen;
struct nv_rm_bo;
struct nv_push;
struct nv_channel;
struct nv_shader;
struct nv_fence;
struct nv_tex_pool;

#define NVGPU_PUSH_DWORDS  (64 * 1024)

/* Gallium CSO for vertex elements: array + count (pipe only passes array) */
struct nvgpu_velems_state {
   unsigned num_elements;
   struct pipe_vertex_element ve[PIPE_MAX_ATTRIBS];
};

/* Gallium shader CSO: pipe_shader_state + compiled/uploaded nv_shader */
struct nvgpu_shader_cso {
   struct pipe_shader_state base;
   struct nv_shader *nvsh;
};

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

   /* Fence sema (shared by flush / pipe_fence_handle as opaque pointer) */
   struct nv_fence *fence;
   uint32_t last_fence_seq;

   struct blitter_context *blitter;

   /* Program region base for SET_PROGRAM_REGION (first uploaded shader BO) */
   uint64_t program_region_base;
   bool program_region_emitted;
   bool channel_init_emitted;

   /* Texture/sampler header pool (shared across draws) */
   struct nv_tex_pool *tex_pool;
   bool tex_pool_bound;
   /* Cached sampler state pointers for FS (slot 0..N) */
   void *sampler_cso[PIPE_MAX_SAMPLERS];
   unsigned num_sampler_cso;

   /* Compute global LMEM backing (SET_SHADER_LOCAL_MEMORY*) */
   struct nv_rm_bo *lmem_bo;
   uint32_t lmem_bo_size;
   bool lmem_programmed;

   /* Bound state (minimal) */
   void *fs;
   void *vs;
   void *gs;   /* geometry shader CSO (optional) */
   void *tcs;  /* tess control */
   void *tes;  /* tess eval */
   void *cs;  /* compute CSO */
   void *blend;
   void *zsa;
   void *rs;
   void *velems;
   struct pipe_vertex_buffer vb[PIPE_MAX_ATTRIBS];
   unsigned num_vb;
   struct pipe_constant_buffer cb[NVGPU_SHADER_STAGES][PIPE_MAX_CONSTANT_BUFFERS];
   struct pipe_sampler_view *samplers[NVGPU_SHADER_STAGES][PIPE_MAX_SAMPLERS];
   unsigned num_samplers[NVGPU_SHADER_STAGES];
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
