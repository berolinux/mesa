/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 */

#include "nv_channel.h"
#include "nv_3d_methods.h"
#include "nv_copy_methods.h"
#include "nv_push.h"
#include "nv_qmd.h"
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

   /* Error notifier memory + CTXDMA (NV01_CONTEXT_ERROR_TO_MEMORY) */
   req.size = NV_CHANNEL_NOTIFIER_SIZE;
   req.alignment = 4096;
   req.vram = false;
   req.cpu_access = true;
   req.map_gpu_va = true;
   ch->notifier_bo = nv_rm_bo_alloc(rm, &req);
   if (!ch->notifier_bo)
      goto fail;
   ch->h_error_notifier = nv_rm_bo_handle(ch->notifier_bo);
   ch->error_notifier = nv_rm_bo_map(ch->notifier_bo);
   if (ch->error_notifier)
      memset((void *)ch->error_notifier, 0, NV_CHANNEL_NOTIFIER_SIZE);

   /* Error CTXDMA: NV01_CONTEXT_ERROR_TO_MEMORY over notifier memory */
   {
      NV_CONTEXT_DMA_ALLOCATION_PARAMS cdp;
      uint32_t h_cd = 0;
      memset(&cdp, 0, sizeof(cdp));
      cdp.hSubDevice = nv_rm_device_subdevice_handle(rm);
      cdp.flags = 0;
      cdp.hMemory = ch->h_error_notifier;
      cdp.offset = 0;
      cdp.limit = NV_CHANNEL_NOTIFIER_SIZE - 1;
      if (nv_rm_alloc_object(rm, nv_rm_device_device_handle(rm), &h_cd,
                             NV01_CONTEXT_ERROR_TO_MEMORY,
                             &cdp, sizeof(cdp)) == 0)
         ch->h_error_ctxdma = h_cd;
   }

   /* Optional TSG (KEPLER_CHANNEL_GROUP_A) + FERMI_CONTEXT_SHARE_A.
    * Best-effort; fall back to lone channel if RM rejects. */
   {
      uint32_t h_grp = 0, h_cs = 0;
      NV_CHANNEL_GROUP_ALLOCATION_PARAMETERS gp;
      NV_CTXSHARE_ALLOCATION_PARAMETERS csp;
      memset(&gp, 0, sizeof(gp));
      gp.hObjectError = ch->h_error_ctxdma ? ch->h_error_ctxdma
                                           : ch->h_error_notifier;
      gp.hVASpace = ch->h_vaspace;
      gp.engineType = ch->engine_type;
      if (nv_rm_alloc_object(rm, nv_rm_device_device_handle(rm), &h_grp,
                             KEPLER_CHANNEL_GROUP_A, &gp, sizeof(gp)) == 0) {
         ch->h_channel_group = h_grp;
         ch->use_channel_group = true;
         memset(&csp, 0, sizeof(csp));
         csp.hVASpace = ch->h_vaspace;
         csp.flags = NV_CTXSHARE_ALLOCATION_FLAGS_SUBCONTEXT_SYNC;
         if (nv_rm_alloc_object(rm, h_grp, &h_cs,
                                FERMI_CONTEXT_SHARE_A, &csp, sizeof(csp)) == 0)
            ch->h_ctxshare = h_cs;
      }
   }

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

   /* Allocate channel object (parent = TSG if present, else device) */
   memset(&ch_params, 0, sizeof(ch_params));
   ch_params.hObjectError = ch->h_error_ctxdma ? ch->h_error_ctxdma
                                               : ch->h_error_notifier;
   ch_params.gpFifoOffset = ch->gpfifo_gpu_addr;
   ch_params.gpFifoEntries = gpfifo_entries;
   ch_params.flags = 0;
   ch_params.hVASpace = ch->h_vaspace;
   ch_params.hContextShare = ch->h_ctxshare;
   ch_params.hUserdMemory[0] = ch->h_userd_mem;
   ch_params.userdOffset[0] = 0;
   ch_params.engineType = ch->engine_type;

   h_channel = 0;
   {
      uint32_t h = 0;
      uint32_t h_parent = ch->h_channel_group ? ch->h_channel_group
                                              : nv_rm_device_device_handle(rm);
      ret = nv_rm_alloc_object(rm, h_parent, &h,
                               ch->gpfifo_class, &ch_params, sizeof(ch_params));
      if (ret != 0) {
         /* Retry without USERD / without TSG parent */
         memset(ch_params.hUserdMemory, 0, sizeof(ch_params.hUserdMemory));
         memset(ch_params.userdOffset, 0, sizeof(ch_params.userdOffset));
         ch_params.hContextShare = 0;
         h = 0;
         h_parent = nv_rm_device_device_handle(rm);
         ret = nv_rm_alloc_object(rm, h_parent, &h,
                                  ch->gpfifo_class, &ch_params, sizeof(ch_params));
      }
      if (ret != 0)
         goto fail;
      ch->h_channel = h;
   }

   /* Schedule: prefer channel-group schedule when TSG is used, else per-channel */
   if (ch->use_channel_group && ch->h_channel_group) {
      NVA06C_CTRL_GPFIFO_SCHEDULE_PARAMS gsched;
      memset(&gsched, 0, sizeof(gsched));
      gsched.bEnable = NV_TRUE;
      gsched.bSkipSubmit = NV_FALSE;
      ret = nv_rm_control(rm, ch->h_channel_group,
                          NVA06C_CTRL_CMD_GPFIFO_SCHEDULE,
                          &gsched, sizeof(gsched));
      if (ret == 0)
         ch->scheduled = true;
   }
   if (!ch->scheduled) {
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
   if (ch->rm) {
      uint32_t h_dev = nv_rm_device_device_handle(ch->rm);
      if (ch->h_channel) {
         uint32_t h_parent = ch->h_channel_group ? ch->h_channel_group : h_dev;
         nv_rm_free_object(ch->rm, h_parent, ch->h_channel);
      }
      if (ch->h_ctxshare && ch->h_channel_group)
         nv_rm_free_object(ch->rm, ch->h_channel_group, ch->h_ctxshare);
      if (ch->h_channel_group)
         nv_rm_free_object(ch->rm, h_dev, ch->h_channel_group);
      if (ch->h_error_ctxdma)
         nv_rm_free_object(ch->rm, h_dev, ch->h_error_ctxdma);
   }
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
   int r;

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

   /* libdrm helper: write GPFIFO entry, advance put, USERD GPPut, doorbell */
   r = nvidia_gpfifo_submit_one(ch->gpfifo_cpu, ch->gpfifo_entries,
                                &ch->gpfifo_put, ch->userd,
                                pb_addr, seg_dwords,
                                ch->usermode_map, ch->work_submit_token,
                                ch->has_work_submit_token,
                                1000000000ull /* 1s ring-full stall */);
   if (r)
      return r;

   ch->push_dw_base = ch->push_dw_used;
   return 0;
#endif
}

int
nv_channel_flush(struct nv_channel *ch)
{
   if (!ch)
      return -EINVAL;
   if (ch->push_dw_used <= ch->push_dw_base)
      return 0;
   return nv_channel_kickoff(ch);
}

int
nv_channel_submit_and_wait(struct nv_channel *ch, uint64_t wait_timeout_ns)
{
   int r;
   if (!ch)
      return -EINVAL;
   r = nv_channel_flush(ch);
   if (r)
      return r;
   if (!wait_timeout_ns)
      return 0;
   return nv_channel_wait_idle(ch, wait_timeout_ns);
}

int
nv_channel_wait_idle(struct nv_channel *ch, uint64_t timeout_ns)
{
#if !defined(HAVE_LIBDRM_NVIDIA)
   (void)ch; (void)timeout_ns;
   return -ENOSYS;
#else
   if (!ch || !ch->userd)
      return -EINVAL;

   return nvidia_userd_wait_gpfifo_idle(ch->userd, ch->gpfifo_put, timeout_ns);
#endif
}

int
nv_channel_notifier_status(struct nv_channel *ch, uint16_t *status_out,
                           uint32_t *info32_out)
{
#if !defined(HAVE_LIBDRM_NVIDIA)
   (void)ch; (void)status_out; (void)info32_out;
   return -ENOSYS;
#else
   if (!ch || !ch->error_notifier)
      return -EINVAL;
   return nvidia_notifier_status(ch->error_notifier, status_out, info32_out);
#endif
}

int
nv_channel_check_notifier(struct nv_channel *ch, bool clear_on_ok,
                          uint64_t timeout_ns)
{
#if !defined(HAVE_LIBDRM_NVIDIA)
   (void)ch; (void)clear_on_ok; (void)timeout_ns;
   return -ENOSYS;
#else
   if (!ch || !ch->error_notifier)
      return -EINVAL;
   return nvidia_notifier_wait(ch->error_notifier, clear_on_ok, timeout_ns);
#endif
}

void
nv_channel_notifier_reset(struct nv_channel *ch)
{
#if !defined(HAVE_LIBDRM_NVIDIA)
   (void)ch;
#else
   if (!ch || !ch->error_notifier)
      return;
   nvidia_notifier_reset(ch->error_notifier);
#endif
}

void
nv_channel_push_advance(struct nv_channel *ch, uint32_t dwords_written)
{
   if (!ch || !dwords_written)
      return;
   if (ch->push_dw_used + dwords_written > ch->push_dw_size)
      dwords_written = ch->push_dw_size - ch->push_dw_used;
   ch->push_dw_used += dwords_written;
}

int
nv_channel_submit_wait_check(struct nv_channel *ch, uint64_t wait_timeout_ns,
                             bool check_notifier, bool clear_notifier_on_ok)
{
   int r;

   if (!ch)
      return -EINVAL;

   r = nv_channel_flush(ch);
   if (r)
      return r;

   if (wait_timeout_ns) {
      r = nv_channel_wait_idle(ch, wait_timeout_ns);
      if (r)
         return r;
   }

   if (check_notifier) {
      if (wait_timeout_ns)
         r = nv_channel_check_notifier(ch, clear_notifier_on_ok,
                                       wait_timeout_ns);
      else
         r = nv_channel_notifier_status(ch, NULL, NULL);
      if (r == -EAGAIN && !wait_timeout_ns)
         return 0; /* kick-only: in-progress is ok */
      if (r)
         return r;
   }
   return 0;
}

int
nv_channel_wait_sema_cpu(volatile uint32_t *sema_cpu, uint32_t payload,
                         uint64_t timeout_ns)
{
#if !defined(HAVE_LIBDRM_NVIDIA)
   (void)sema_cpu; (void)payload; (void)timeout_ns;
   return -ENOSYS;
#else
   if (!sema_cpu || !payload)
      return 0;
   return nvidia_sema_wait_geq(sema_cpu, payload, timeout_ns);
#endif
}

int
nv_channel_submit_wait_sema(struct nv_channel *ch,
                            volatile uint32_t *sema_cpu, uint32_t sema_payload,
                            uint64_t wait_timeout_ns, bool check_notifier)
{
   int r;

   if (!ch)
      return -EINVAL;

   nv_channel_notifier_reset(ch);

   r = nv_channel_flush(ch);
   if (r)
      return r;

#if defined(HAVE_LIBDRM_NVIDIA)
   r = nvidia_submit_wait_complete(ch->userd, ch->gpfifo_put,
                                   sema_cpu, sema_payload,
                                   check_notifier ? ch->error_notifier : NULL,
                                   wait_timeout_ns);
   return r;
#else
   (void)sema_cpu; (void)sema_payload; (void)wait_timeout_ns;
   (void)check_notifier;
   return -ENOSYS;
#endif
}

int
nv_channel_g1_ce_copy_sema_submit(struct nv_channel *ch,
                                  uint32_t class_copy,
                                  uint64_t src_gpu_addr,
                                  uint64_t dst_gpu_addr,
                                  uint32_t size_bytes,
                                  uint64_t sema_gpu_addr,
                                  volatile uint32_t *sema_cpu,
                                  uint32_t sema_payload,
                                  bool sema_reset,
                                  uint64_t wait_timeout_ns,
                                  bool check_notifier)
{
   struct nv_push push;
   uint32_t *map;
   uint32_t need = 64;
   uint32_t cc;

   if (!ch || !src_gpu_addr || !dst_gpu_addr || !size_bytes || !sema_gpu_addr)
      return -EINVAL;
   if (!sema_payload)
      sema_payload = 0x42u;

   cc = class_copy;
   if (!cc && ch->info)
      cc = ch->info->class_copy;
   if (!cc)
      return -EINVAL;

   if (sema_reset && sema_cpu)
      sema_cpu[0] = 0;

   map = nv_channel_push_begin(ch, need);
   if (!map)
      return -ENOMEM;

   nv_push_init(&push, map, need);
   nv_copy_set_object(&push, cc);
   nv_copy_emit_buffer_copy_with_sema(&push, src_gpu_addr, dst_gpu_addr,
                                      size_bytes, sema_gpu_addr, sema_payload);
   nv_channel_push_advance(ch, nv_push_dw_count(&push));

   return nv_channel_submit_wait_sema(ch, sema_cpu, sema_payload,
                                      wait_timeout_ns, check_notifier);
}

int
nv_channel_g1_ce_sema_only_submit(struct nv_channel *ch,
                                  uint32_t class_copy,
                                  uint64_t sema_gpu_addr,
                                  volatile uint32_t *sema_cpu,
                                  uint32_t sema_payload,
                                  bool sema_reset,
                                  uint64_t wait_timeout_ns,
                                  bool check_notifier)
{
   struct nv_push push;
   uint32_t *map;
   uint32_t need = 32;
   uint32_t cc;

   if (!ch || !sema_gpu_addr)
      return -EINVAL;
   if (!sema_payload)
      sema_payload = 0x42u;

   cc = class_copy;
   if (!cc && ch->info)
      cc = ch->info->class_copy;
   if (!cc)
      return -EINVAL;

   if (sema_reset && sema_cpu)
      sema_cpu[0] = 0;

   map = nv_channel_push_begin(ch, need);
   if (!map)
      return -ENOMEM;

   nv_push_init(&push, map, need);
   nv_copy_set_object(&push, cc);
   nv_copy_emit_semaphore_release(&push, sema_gpu_addr, sema_payload);
   nv_channel_push_advance(ch, nv_push_dw_count(&push));

   return nv_channel_submit_wait_sema(ch, sema_cpu, sema_payload,
                                      wait_timeout_ns, check_notifier);
}

int
nv_channel_g2_compute_dispatch_sema_submit(struct nv_channel *ch,
                                           uint32_t class_compute,
                                           const struct nv_qmd_desc *desc,
                                           uint64_t qmd_gpu_addr,
                                           void *qmd_host,
                                           uint64_t sema_gpu_addr,
                                           volatile uint32_t *sema_cpu,
                                           uint32_t sema_payload,
                                           bool sema_reset,
                                           bool emit_init_state,
                                           bool method_invalidate,
                                           uint64_t wait_timeout_ns,
                                           bool check_notifier)
{
   struct nv_push push;
   struct nv_qmd_desc local;
   uint32_t *map;
   /* SET_OBJECT + SPA/CWD + invalidate + 64x LOAD_INLINE_QMD + PCAS ~ 200 dwords */
   uint32_t need = 256;
   uint32_t cc;

   if (!ch || !desc || !qmd_gpu_addr)
      return -EINVAL;
   if (!sema_payload && sema_gpu_addr)
      sema_payload = 0x42u;

   cc = class_compute;
   if (!cc && ch->info)
      cc = ch->info->class_compute;
   if (!cc)
      return -EINVAL;

   if (sema_reset && sema_cpu)
      sema_cpu[0] = 0;

   local = *desc;
   if (sema_gpu_addr && sema_payload)
      nv_qmd_desc_set_sema_release0(&local, sema_gpu_addr, sema_payload);

   map = nv_channel_push_begin(ch, need);
   if (!map)
      return -ENOMEM;

   nv_push_init(&push, map, need);
   if (emit_init_state)
      nv_compute_emit_init_state(&push, cc, 0 /* spa */, 0 /* cwd slots */);
   else
      nv_compute_set_object(&push, cc);

   /* class_compute 0: object/subch already set above */
   nv_compute_emit_dispatch_with_sema(&push, &local, qmd_gpu_addr, qmd_host,
                                      0, sema_gpu_addr, sema_payload,
                                      method_invalidate);
   nv_channel_push_advance(ch, nv_push_dw_count(&push));

   return nv_channel_submit_wait_sema(ch, sema_cpu, sema_payload,
                                      wait_timeout_ns, check_notifier);
}

int
nv_channel_g2_compute_smoke_sema_submit(struct nv_channel *ch,
                                        uint32_t class_compute,
                                        uint64_t program_gpu_addr,
                                        uint32_t register_count,
                                        uint8_t sass_version,
                                        uint64_t qmd_gpu_addr,
                                        void *qmd_host,
                                        uint64_t sema_gpu_addr,
                                        volatile uint32_t *sema_cpu,
                                        uint32_t sema_payload,
                                        bool sema_reset,
                                        bool emit_init_state,
                                        bool method_invalidate,
                                        uint64_t wait_timeout_ns,
                                        bool check_notifier)
{
   struct nv_qmd_desc desc;

   if (!sema_payload)
      sema_payload = 0x42u;

   nv_qmd_desc_init_smoke(&desc, program_gpu_addr, register_count,
                          sass_version, sema_gpu_addr, sema_payload);
   return nv_channel_g2_compute_dispatch_sema_submit(ch, class_compute, &desc,
                                                     qmd_gpu_addr, qmd_host,
                                                     sema_gpu_addr, sema_cpu,
                                                     sema_payload, sema_reset,
                                                     emit_init_state,
                                                     method_invalidate,
                                                     wait_timeout_ns,
                                                     check_notifier);
}

int
nv_channel_g3_clear_sema_submit(struct nv_channel *ch,
                                uint32_t class_3d,
                                uint64_t ct_gpu_addr,
                                uint32_t ct_w, uint32_t ct_h,
                                uint32_t ct_format,
                                const uint32_t color_ui[4],
                                bool emit_draw,
                                uint64_t sema_gpu_addr,
                                volatile uint32_t *sema_cpu,
                                uint32_t sema_payload,
                                bool sema_reset,
                                uint64_t wait_timeout_ns,
                                bool check_notifier)
{
   struct nv_push push;
   uint32_t *map;
   uint32_t need = 128;
   uint32_t c3;
   uint32_t c[4];

   if (!ch || !sema_gpu_addr)
      return -EINVAL;
   if (!sema_payload)
      sema_payload = 0x42u;

   c3 = class_3d;
   if (!c3 && ch->info)
      c3 = ch->info->class_3d;
   if (!c3)
      return -EINVAL;

   if (!ct_format)
      ct_format = NVC597_SET_COLOR_TARGET_FORMAT_V_A8B8G8R8;
   if (color_ui)
      memcpy(c, color_ui, sizeof(c));
   else
      memset(c, 0, sizeof(c));

   if (sema_reset && sema_cpu)
      sema_cpu[0] = 0;

   map = nv_channel_push_begin(ch, need);
   if (!map)
      return -ENOMEM;

   nv_push_init(&push, map, need);
   if (emit_draw)
      nv_3d_emit_g3_clear_draw_sema(&push, c3, ct_gpu_addr, ct_w, ct_h,
                                    ct_format, c, false /* sema after draw */,
                                    sema_gpu_addr, sema_payload);
   else
      nv_3d_emit_g3_clear_color_sema(&push, c3, ct_gpu_addr, ct_w, ct_h,
                                     ct_format, c, sema_gpu_addr, sema_payload);
   nv_channel_push_advance(ch, nv_push_dw_count(&push));

   return nv_channel_submit_wait_sema(ch, sema_cpu, sema_payload,
                                      wait_timeout_ns, check_notifier);
}

int
nv_channel_g3_sema_only_submit(struct nv_channel *ch,
                               uint32_t class_3d,
                               uint64_t sema_gpu_addr,
                               volatile uint32_t *sema_cpu,
                               uint32_t sema_payload,
                               bool sema_reset,
                               uint64_t wait_timeout_ns,
                               bool check_notifier)
{
   struct nv_push push;
   uint32_t *map;
   uint32_t need = 32;
   uint32_t c3;

   if (!ch || !sema_gpu_addr)
      return -EINVAL;
   if (!sema_payload)
      sema_payload = 0x42u;

   c3 = class_3d;
   if (!c3 && ch->info)
      c3 = ch->info->class_3d;
   if (!c3)
      return -EINVAL;

   if (sema_reset && sema_cpu)
      sema_cpu[0] = 0;

   map = nv_channel_push_begin(ch, need);
   if (!map)
      return -ENOMEM;

   nv_push_init(&push, map, need);
   nv_3d_emit_g3_sema_only(&push, c3, sema_gpu_addr, sema_payload);
   nv_channel_push_advance(ch, nv_push_dw_count(&push));

   return nv_channel_submit_wait_sema(ch, sema_cpu, sema_payload,
                                      wait_timeout_ns, check_notifier);
}
