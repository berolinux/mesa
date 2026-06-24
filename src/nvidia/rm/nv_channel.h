/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * GPFIFO channel management.  Allocation sequence follows nvidia-push
 * (open-gpu-kernel-modules/src/common/unix/nvidia-push/src/nvidia-push-init.c):
 *   1. Allocate USERD memory (NV01_MEMORY_LOCAL_USER or SYSTEM)
 *   2. Allocate error notifier memory
 *   3. Allocate pushbuffer + GPFIFO ring in mappable memory
 *   4. RmAlloc(GPFIFO class, NV_CHANNEL_ALLOC_PARAMS)
 *   5. Map USERD, schedule channel, optionally get work-submit token
 *   6. Kickoff: write GPFIFO entries, update GPPut in USERD / doorbell
 */

#ifndef NV_CHANNEL_H
#define NV_CHANNEL_H

#include <stdbool.h>
#include <stdint.h>

#include "nv_device_info.h"

#ifdef __cplusplus
extern "C" {
#endif

struct nv_rm_device;
struct nv_rm_bo;

#define NV_CHANNEL_DEFAULT_GPFIFO_ENTRIES  512
#define NV_CHANNEL_DEFAULT_PUSH_DWORDS     (64 * 1024)
#define NV_CHANNEL_USERD_SIZE              4096
#define NV_CHANNEL_NOTIFIER_SIZE           4096

struct nv_channel {
   struct nv_rm_device *rm;
   const struct nv_device_info *info;

   uint32_t h_channel;
   uint32_t h_userd_mem;
   uint32_t h_error_notifier;      /* memory handle for notifier BO */
   uint32_t h_error_ctxdma;        /* NV01_CONTEXT_ERROR_TO_MEMORY over notifier */
   uint32_t h_push_mem;
   uint32_t h_gpfifo_mem;
   uint32_t h_vaspace;             /* FERMI_VASPACE_A passed at channel alloc */
   uint32_t h_channel_group;       /* KEPLER_CHANNEL_GROUP_A if used */
   uint32_t h_ctxshare;            /* FERMI_CONTEXT_SHARE_A if used */
   uint32_t engine_type;
   uint32_t gpfifo_class;
   uint32_t work_submit_token;
   bool has_work_submit_token;
   bool scheduled;
   bool use_channel_group;

   /* CPU mappings */
   volatile uint32_t *userd;       /* USERD control block (GPPut/GPGet/...) */
   volatile void *error_notifier;  /* mapped notifier memory (nvidia_notification_t) */
   uint32_t *gpfifo_cpu;           /* GPFIFO ring (pairs of dwords) */
   uint32_t *push_cpu;             /* pushbuffer backing */
   uint64_t gpfifo_gpu_addr;       /* GPU VA/offset of GPFIFO ring start */
   uint64_t push_gpu_addr;         /* GPU VA/offset of pushbuffer start */
   uint32_t gpfifo_entries;        /* ring capacity in entries */
   uint32_t gpfifo_put;            /* next entry index to write */
   uint32_t push_dw_size;
   uint32_t push_dw_used;
   uint32_t push_dw_base;          /* start of current kickoff segment */

   /* Doorbell: pointer into device usermode region (not owned by channel) */
   volatile void *usermode_map;

   struct nv_rm_bo *userd_bo;
   struct nv_rm_bo *notifier_bo;
   struct nv_rm_bo *gpfifo_bo;
   struct nv_rm_bo *push_bo;
};

struct nv_channel *
nv_channel_create(struct nv_rm_device *rm, uint32_t engine_type,
                  uint32_t gpfifo_entries, uint32_t push_dwords);

void
nv_channel_destroy(struct nv_channel *ch);

/** Begin recording methods into the pushbuffer; returns pointer to write at */
uint32_t *
nv_channel_push_begin(struct nv_channel *ch, uint32_t need_dwords);

/** Commit pushbuffer segment and submit via GPFIFO + GPPut update */
int
nv_channel_kickoff(struct nv_channel *ch);

/** Current push dwords written since last kickoff begin */
uint32_t
nv_channel_push_used(struct nv_channel *ch);

/** Wait for GPU to consume GPFIFO (poll GPGet) with timeout in ns; 0 = try once */
int
nv_channel_wait_idle(struct nv_channel *ch, uint64_t timeout_ns);

#ifdef __cplusplus
}
#endif

#endif /* NV_CHANNEL_H */
