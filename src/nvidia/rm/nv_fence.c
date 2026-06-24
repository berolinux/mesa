/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include "nv_fence.h"

#include "nv_3d_methods.h"
#include "nv_rm.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t
nv_fence_now_ns(void)
{
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

struct nv_fence *
nv_fence_create(struct nv_rm_device *rm)
{
   struct nv_fence *f;
   struct nv_rm_bo_req req;
   void *map;

   if (!rm)
      return NULL;

   f = calloc(1, sizeof(*f));
   if (!f)
      return NULL;

   f->rm = rm;

   memset(&req, 0, sizeof(req));
   req.size = 4096;
   req.alignment = 4096;
   req.vram = false;
   req.cpu_access = true;
   req.no_scanout = true;
   req.map_gpu_va = true;

   f->sema_bo = nv_rm_bo_alloc(rm, &req);
   if (!f->sema_bo) {
      free(f);
      return NULL;
   }

   map = nv_rm_bo_map(f->sema_bo);
   if (!map) {
      nv_rm_bo_free(f->sema_bo);
      free(f);
      return NULL;
   }

   f->sema_cpu = (volatile uint32_t *)map;
   f->sema_gpu_addr = nv_rm_bo_gpu_offset(f->sema_bo);
   f->sema_cpu[0] = 0;
   f->seq = 0;
   f->signaled = true;
   return f;
}

void
nv_fence_destroy(struct nv_fence *f)
{
   if (!f)
      return;
   if (f->sema_bo) {
      nv_rm_bo_unmap(f->sema_bo);
      nv_rm_bo_free(f->sema_bo);
   }
   free(f);
}

uint32_t
nv_fence_emit_3d_signal(struct nv_fence *f, struct nv_push *p)
{
   if (!f || !p || !f->sema_gpu_addr)
      return 0;

   f->seq++;
   f->signaled = false;
   nv_3d_report_semaphore_release(p, f->sema_gpu_addr, f->seq, true);
   return f->seq;
}

uint32_t
nv_fence_emit_host_signal(struct nv_fence *f, struct nv_push *p)
{
   if (!f || !p || !f->sema_gpu_addr)
      return 0;

   f->seq++;
   f->signaled = false;
   nv_push_host_semaphore_release(p, f->sema_gpu_addr, f->seq);
   return f->seq;
}

bool
nv_fence_signaled(struct nv_fence *f, uint32_t seq)
{
   uint32_t cur;

   if (!f)
      return true;
   if (!seq)
      return true;
   if (!f->sema_cpu)
      return f->signaled && f->seq >= seq;

   cur = f->sema_cpu[0];
   if (cur >= seq) {
      f->signaled = (cur >= f->seq);
      return true;
   }
   return false;
}

int
nv_fence_wait(struct nv_fence *f, uint32_t seq, uint64_t timeout_ns)
{
   uint64_t start, now;

   if (!f || !seq)
      return 0;
   if (nv_fence_signaled(f, seq))
      return 0;
   if (!f->sema_cpu)
      return -1;

   start = nv_fence_now_ns();
   for (;;) {
      if (f->sema_cpu[0] >= seq) {
         f->signaled = (f->sema_cpu[0] >= f->seq);
         return 0;
      }
      if (timeout_ns == 0)
         return -1;
      now = nv_fence_now_ns();
      if (now - start >= timeout_ns)
         return -1;
   }
}
