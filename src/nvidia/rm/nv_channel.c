/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 */

#include "nv_channel.h"
#include "nv_rm.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(HAVE_LIBDRM_NVIDIA)
#include "nvidia.h"
#include "nvidia_rm.h"
#endif

static uint32_t
align_u32(uint32_t v, uint32_t a)
{
   return (v + a - 1) & ~(a - 1);
}

static uint64_t
now_ns(void)
{
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

struct nv_channel *
nv_channel_create(struct nv_rm_device *rm, uint32_t engine_type,
                  uint32_t gpfifo_entries, uint32_t push_dwords)
{
#if !defined(HAVE_LIBDRM_NVIDIA)
   (void)rm; (void)engine_type; (void)gpfifo_entries; (void)push_dwords;
   return NULL;
#else
   struct nv_channel *ch;
   const struct nv_device_info *info;
   struct nv_rm_bo_req req;
   NV_CHANNEL_ALLOC_PARAMS ch_params;
   uint32_t h_channel;
   int ret;
   uint32_t gpfifo_bytes, push_bytes;

   if (!rm)
      return NULL;

   info = nv_rm_device_info(rm);
   if (!info || !info->class_gpfifo)
      return NULL;

   if (gpfifo_entries < 32)
      gpfifo_entries = NV_CHANNEL_DEFAULT_GPFIFO_ENTRIES;
   if (push_dwords < 4096)
      push_dwords = NV_CHANNEL_DEFAULT_PUSH_DWORDS;

   ch = calloc(1, sizeof(*ch));
   if (!ch)
      return NULL;

   ch->rm = rm;
   ch->info = info;
   ch->engine_type = engine_type ? engine_type : NV2080_ENGINE_TYPE_GRAPHICS;
   ch->gpfifo_class = info->class_gpfifo;
   ch->gpfifo_entries = gpfifo_entries;
   ch->push_dw_size = push_dwords;

   gpfifo_bytes = gpfifo_entries * NV_GP_ENTRY_SIZE;
   push_bytes = push_dwords * 4;

   /* Ensure device VAS + usermode doorbell before channel alloc */
   (void)nv_rm_device_ensure_vaspace(rm);
   (void)nv_rm_device_ensure_usermode(rm);
   ch->h_vaspace = nv_rm_device_vaspace_handle(rm);
   ch->usermode_map = nv_rm_device_usermode_map(rm);

   /* USERD - prefer VRAM uncached; fall back to sysmem; map into VAS */
   memset(&req, 0, sizeof(req));
   req.size = NV_CHANNEL_USERD_SIZE;
   req.alignment = NV_CHANNEL_USERD_SIZE;
   req.vram = info->vram_size_bytes > 0;
   req.cpu_access = true;
   req.no_scanout = true;
   req.map_gpu_va = true;
   ch->userd_bo = nv_rm_bo_alloc(rm, &req);
   if (!ch->userd_bo && req.vram) {
      req.vram = false;
      ch->userd_bo = nv_rm_bo_alloc(rm, &req);
   }
   if (!ch->userd_bo)
      goto fail;
   ch->h_userd_mem = nv_rm_bo_handle(ch->userd_bo);
   ch->userd = nv_rm_bo_map(ch->userd_bo);
   if (!ch->userd)
      goto fail;
   memset((void *)ch->userd, 0, NV_CHANNEL_USERD_SIZE);

   /* Error notifier */
   req.size = NV_CHANNEL_NOTIFIER_SIZE;
   req.alignment = 4096;
   req.vram = false;
   req.cpu_access = true;
   req.map_gpu_va = true;
   ch->notifier_bo = nv_rm_bo_alloc(rm, &req);
   if (!ch->notifier_bo)
      goto fail;
   ch->h_error_notifier = nv_rm_bo_handle(ch->notifier_bo);

   /* GPFIFO ring (must have GPU VA for gpFifoOffset) */
   req.size = gpfifo_bytes;
   req.alignment = 4096;
   req.vram = false;
   req.cpu_access = true;
   req.map_gpu_va = true;
   ch->gpfifo_bo = nv_rm_bo_alloc(rm, &req);
   if (!ch->gpfifo_bo)
      goto fail;
   ch->h_gpfifo_mem = nv_rm_bo_handle(ch->gpfifo_bo);
   ch->gpfifo_cpu = nv_rm_bo_map(ch->gpfifo_bo);
   ch->gpfifo_gpu_addr = nv_rm_bo_gpu_offset(ch->gpfifo_bo);
   if (!ch->gpfifo_cpu)
      goto fail;
   memset(ch->gpfifo_cpu, 0, gpfifo_bytes);

   /* Pushbuffer */
   req.size = push_bytes;
   req.alignment = 4096;
   req.map_gpu_va = true;
   ch->push_bo = nv_rm_bo_alloc(rm, &req);
   if (!ch->push_bo)
      goto fail;
   ch->h_push_mem = nv_rm_bo_handle(ch->push_bo);
   ch->push_cpu = nv_rm_bo_map(ch->push_bo);
   ch->push_gpu_addr = nv_rm_bo_gpu_offset(ch->push_bo);
   if (!ch->push_cpu)
      goto fail;
   memset(ch->push_cpu, 0, push_bytes);

   /* Allocate channel object */
   memset(&ch_params, 0, sizeof(ch_params));
   ch_params.hObjectError = ch->h_error_notifier;
   /* gpFifoOffset must be a GPU VA in the channel's VASpace */
   ch_params.gpFifoOffset = ch->gpfifo_gpu_addr;
   ch_params.gpFifoEntries = gpfifo_entries;
   ch_params.flags = 0;
   ch_params.hVASpace = ch->h_vaspace;
   ch_params.hUserdMemory[0] = ch->h_userd_mem;
   ch_params.userdOffset[0] = 0;
   ch_params.engineType = ch->engine_type;

   h_channel = 0; /* let helper assign */
   /* Need a requested handle: use rm alloc object API */
   {
      uint32_t h = 0;
      /* Pick a non-zero handle via a throwaway BO's handle range is not ideal;
       * nv_rm_alloc_object accepts 0 and kernel may assign.  Request explicit. */
      ret = nv_rm_alloc_object(rm, nv_rm_device_device_handle(rm), &h,
                               ch->gpfifo_class, &ch_params, sizeof(ch_params));
      if (ret != 0) {
         /* Retry without client-provided USERD (older GPUs: USERD inside channel) */
         memset(ch_params.hUserdMemory, 0, sizeof(ch_params.hUserdMemory));
         memset(ch_params.userdOffset, 0, sizeof(ch_params.userdOffset));
         h = 0;
         ret = nv_rm_alloc_object(rm, nv_rm_device_device_handle(rm), &h,
                                  ch->gpfifo_class, &ch_params, sizeof(ch_params));
      }
      if (ret != 0)
         goto fail;
      ch->h_channel = h;
   }

   /* Schedule channel via RmControl */
   {
      NVA06F_CTRL_GPFIFO_SCHEDULE_PARAMS sched;
      memset(&sched, 0, sizeof(sched));
      sched.bEnable = NV_TRUE;
      sched.bSkipSubmit = NV_FALSE;
      ret = nv_rm_control(rm, ch->h_channel, NVA06F_CTRL_CMD_GPFIFO_SCHEDULE,
                          &sched, sizeof(sched));
      if (ret == 0)
         ch->scheduled = true;
   }

   /* Work submit token (Volta+) */
   {
      NVC36F_CTRL_CMD_GPFIFO_GET_WORK_SUBMIT_TOKEN_PARAMS tok;
      memset(&tok, 0, sizeof(tok));
      if (nv_rm_control(rm, ch->h_channel,
                        NVC36F_CTRL_CMD_GPFIFO_GET_WORK_SUBMIT_TOKEN,
                        &tok, sizeof(tok)) == 0) {
         ch->work_submit_token = tok.workSubmitToken;
         ch->has_work_submit_token = true;
      }
   }

   return ch;

fail:
   nv_channel_destroy(ch);
   return NULL;
#endif
}

void
nv_channel_destroy(struct nv_channel *ch)
{
   if (!ch)
      return;

#if defined(HAVE_LIBDRM_NVIDIA)
   if (ch->h_channel && ch->rm)
      nv_rm_free_object(ch->rm, nv_rm_device_device_handle(ch->rm),
                        ch->h_channel);
   if (ch->push_bo)
      nv_rm_bo_free(ch->push_bo);
   if (ch->gpfifo_bo)
      nv_rm_bo_free(ch->gpfifo_bo);
   if (ch->notifier_bo)
      nv_rm_bo_free(ch->notifier_bo);
   if (ch->userd_bo)
      nv_rm_bo_free(ch->userd_bo);
#endif
   free(ch);
}

uint32_t *
nv_channel_push_begin(struct nv_channel *ch, uint32_t need_dwords)
{
   if (!ch || !ch->push_cpu)
      return NULL;

   if (ch->push_dw_used + need_dwords + 16 >= ch->push_dw_size) {
      /* Ring wrap: kick off what we have, then reset */
      if (ch->push_dw_used > ch->push_dw_base)
         nv_channel_kickoff(ch);
      ch->push_dw_used = 0;
      ch->push_dw_base = 0;
   }

   ch->push_dw_base = ch->push_dw_used;
   return ch->push_cpu + ch->push_dw_used;
}

uint32_t
nv_channel_push_used(struct nv_channel *ch)
{
   return ch ? (ch->push_dw_used - ch->push_dw_base) : 0;
}

int
nv_channel_kickoff(struct nv_channel *ch)
{
#if !defined(HAVE_LIBDRM_NVIDIA)
   (void)ch;
   return -ENOSYS;
#else
   uint32_t seg_dwords;
   uint64_t pb_addr;
   uint32_t entry[2];
   uint32_t put_idx, next_put;
   volatile nvidia_userd_control_t *ud;

   if (!ch || !ch->gpfifo_cpu || !ch->userd || !ch->push_cpu)
      return -EINVAL;

   seg_dwords = ch->push_dw_used - ch->push_dw_base;
   if (seg_dwords == 0)
      return 0;

   /* Align segment end for some engines that want 4-dword alignment */
   while (seg_dwords & 3) {
      ch->push_cpu[ch->push_dw_used++] = 0; /* NOP padding if needed */
      seg_dwords++;
   }

   pb_addr = ch->push_gpu_addr + (uint64_t)ch->push_dw_base * 4;
   nvidia_gp_entry_pack(entry, pb_addr, seg_dwords, false, false);

   put_idx = ch->gpfifo_put;
   next_put = (put_idx + 1) % ch->gpfifo_entries;

   /* Stall if ring full (GPGet == next_put) */
   ud = (volatile nvidia_userd_control_t *)ch->userd;
   {
      uint64_t deadline = now_ns() + 1000000000ull; /* 1s */
      while (ud->GPGet == next_put) {
         if (now_ns() > deadline)
            return -ETIMEDOUT;
      }
   }

   ch->gpfifo_cpu[put_idx * 2 + 0] = entry[0];
   ch->gpfifo_cpu[put_idx * 2 + 1] = entry[1];

   /* Memory barrier before publishing put */
   __sync_synchronize();

   ch->gpfifo_put = next_put;
   ud->GPPut = next_put;
   __sync_synchronize();

   /* Volta+: ring usermode doorbell with work_submit_token (nvidia-push path).
    * Pre-Volta / missing usermode: GPPut alone is sufficient. */
   if (ch->has_work_submit_token && ch->usermode_map)
      nvidia_rm_doorbell_ring(ch->usermode_map, ch->work_submit_token);

   ch->push_dw_base = ch->push_dw_used;
   return 0;
#endif
}

int
nv_channel_wait_idle(struct nv_channel *ch, uint64_t timeout_ns)
{
#if !defined(HAVE_LIBDRM_NVIDIA)
   (void)ch; (void)timeout_ns;
   return -ENOSYS;
#else
   volatile nvidia_userd_control_t *ud;
   uint64_t deadline;
   uint32_t target;

   if (!ch || !ch->userd)
      return -EINVAL;

   ud = (volatile nvidia_userd_control_t *)ch->userd;
   target = ch->gpfifo_put;
   deadline = timeout_ns ? now_ns() + timeout_ns : 0;

   for (;;) {
      if (ud->GPGet == target)
         return 0;
      if (!timeout_ns)
         return -EAGAIN;
      if (now_ns() > deadline)
         return -ETIMEDOUT;
   }
#endif
}
