/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 */
#ifndef NVGPU_SCREEN_H
#define NVGPU_SCREEN_H

#include "pipe/p_screen.h"
#include "util/u_dynarray.h"

struct nv_rm_device;
struct nv_device_info;

struct nvgpu_screen {
   struct pipe_screen base;
   int fd;                          /* drm fd (may be -1 if pure RM) */
   struct nv_rm_device *rm;
   const struct nv_device_info *info;
   struct slab_parent_pool transfer_pool;
};

static inline struct nvgpu_screen *
nvgpu_screen(struct pipe_screen *pscreen)
{
   return (struct nvgpu_screen *)pscreen;
}

#endif
