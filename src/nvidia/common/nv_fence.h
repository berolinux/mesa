/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * GPU/CPU fence via semaphore memory:
 *   - 3D path: SET_REPORT_SEMAPHORE release (pipeline location ALL)
 *   - Host path: NVC36F SEMAPHORE* release
 * CPU waits by polling the mapped semaphore dword (GEQ payload).
 */

#ifndef NV_FENCE_H
#define NV_FENCE_H

#include <stdbool.h>
#include <stdint.h>

#include "nv_push.h"

#ifdef __cplusplus
extern "C" {
#endif

struct nv_rm_device;
struct nv_rm_bo;
struct nv_channel;

struct nv_fence {
   struct nv_rm_device *rm;
   struct nv_rm_bo *sema_bo;
   volatile uint32_t *sema_cpu;
   uint64_t sema_gpu_addr;
   uint32_t seq;
   bool signaled;
};

struct nv_fence *
nv_fence_create(struct nv_rm_device *rm);

void
nv_fence_destroy(struct nv_fence *f);

uint32_t
nv_fence_emit_3d_signal(struct nv_fence *f, struct nv_push *p);

uint32_t
nv_fence_emit_host_signal(struct nv_fence *f, struct nv_push *p);

int
nv_fence_wait(struct nv_fence *f, uint32_t seq, uint64_t timeout_ns);

bool
nv_fence_signaled(struct nv_fence *f, uint32_t seq);

static inline uint32_t
nv_fence_seq(const struct nv_fence *f)
{
   return f ? f->seq : 0;
}

#ifdef __cplusplus
}
#endif

#endif /* NV_FENCE_H */
