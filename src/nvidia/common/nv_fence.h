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
/* nv_3d_methods.h provides nv_3d_report_semaphore_acquire /
 * nv_push_host_semaphore_acquire used by nv_fence_emit_wait_other. */
#include "nv_3d_methods.h"

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

/**
 * Timeline-style payload: increment seq and emit 3D report sema release.
 * Returns the new seq (payload written to sema memory).
 */
static inline uint32_t
nv_fence_signal_next_3d(struct nv_fence *f, struct nv_push *p)
{
   if (!f || !p)
      return 0;
   return nv_fence_emit_3d_signal(f, p);
}

/** Host semaphore path (GPFIFO/NVC36F) for submits without active 3D class. */
static inline uint32_t
nv_fence_signal_next_host(struct nv_fence *f, struct nv_push *p)
{
   if (!f || !p)
      return 0;
   return nv_fence_emit_host_signal(f, p);
}

/**
 * Wait on another fence's sema (acquire GEQ) before continuing push stream.
 * Used for VkSemaphore wait on queue submit / cmd buffer boundaries.
 */
static inline void
nv_fence_emit_wait_other(struct nv_push *p, const struct nv_fence *other,
                         uint32_t wait_seq, bool use_3d_report)
{
   if (!p || !other || !other->sema_gpu_addr)
      return;
   if (use_3d_report)
      nv_3d_report_semaphore_acquire(p, other->sema_gpu_addr, wait_seq,
                                     true /* one_word */);
   else
      nv_push_host_semaphore_acquire(p, other->sema_gpu_addr, wait_seq);
}

#ifdef __cplusplus
}
#endif

#endif /* NV_FENCE_H */
