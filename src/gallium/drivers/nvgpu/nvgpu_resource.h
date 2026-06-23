/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 */
#ifndef NVGPU_RESOURCE_H
#define NVGPU_RESOURCE_H

#include "pipe/p_state.h"
#include "util/u_range.h"
#include "util/u_threaded_context.h"

struct nv_rm_bo;
struct nvgpu_screen;

struct nvgpu_resource {
   struct threaded_resource b;
   struct nv_rm_bo *bo;
   uint64_t gpu_offset;
   uint32_t rm_handle;
   enum pipe_format internal_format;
   bool linear;
};

static inline struct nvgpu_resource *
nvgpu_resource(struct pipe_resource *pr)
{
   return (struct nvgpu_resource *)pr;
}

struct pipe_resource *nvgpu_resource_create(struct pipe_screen *pscreen,
                                            const struct pipe_resource *templ);
struct pipe_resource *nvgpu_resource_from_handle(struct pipe_screen *pscreen,
                                                 const struct pipe_resource *templ,
                                                 struct winsys_handle *whandle,
                                                 unsigned usage);
void nvgpu_resource_destroy(struct pipe_screen *pscreen,
                            struct pipe_resource *pres);

#endif
