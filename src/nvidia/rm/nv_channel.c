/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 */

#include "nv_channel.h"
#include "nv_3d_methods.h"
#include "nv_copy_methods.h"
#include "nv_device_info.h"
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

#if defined(HAVE_LIBDRM_NVIDIA)
/*
 * Schedule channel (and/or TSG) so PBDMA can run methods.
 * Pass5 / OGKM ctrla06c: A06C_BIND configures group engine; then GPFIFO_SCHEDULE.
 * Also try A06F_BIND + A06F_GPFIFO_SCHEDULE on the channel object.
 * Returns 0 if scheduled, else last errno; updates ch->schedule_rc/path/bind_rc.
 */
static int
nv_channel_try_schedule(struct nv_channel *ch)
{
   struct nv_rm_device *rm;
   int sret = -EAGAIN;
   int last_err = -EAGAIN;

   if (!ch || !ch->rm || !ch->h_channel)
      return -EINVAL;
   if (ch->scheduled)
      return 0;

   rm = ch->rm;
   ch->schedule_bind_rc = -1;

   /* --- TSG path: BIND (engine) then A06C_GPFIFO_SCHEDULE --- */
   if (ch->h_channel_group) {
      NVA06C_CTRL_BIND_PARAMS bind;
      NVA06C_CTRL_GPFIFO_SCHEDULE_PARAMS gsched;

      memset(&bind, 0, sizeof(bind));
      bind.engineType = ch->engine_type;
      sret = nv_rm_control(rm, ch->h_channel_group, NVA06C_CTRL_CMD_BIND,
                           &bind, sizeof(bind));
      ch->schedule_bind_rc = sret;
      /* BIND may fail if already bound; still try schedule */

      memset(&gsched, 0, sizeof(gsched));
      gsched.bEnable = NV_TRUE;
      gsched.bSkipSubmit = NV_FALSE;
      sret = nv_rm_control(rm, ch->h_channel_group,
                           NVA06C_CTRL_CMD_GPFIFO_SCHEDULE,
                           &gsched, sizeof(gsched));
      if (sret == 0) {
         ch->scheduled = true;
         ch->schedule_rc = 0;
         ch->schedule_path = 1; /* A06C TSG */
         ch->use_channel_group = true;
         return 0;
      }
      last_err = sret;

      /* Retry schedule without assuming BIND succeeded (order variation) */
      memset(&gsched, 0, sizeof(gsched));
      gsched.bEnable = NV_TRUE;
      gsched.bSkipSubmit = NV_FALSE;
      sret = nv_rm_control(rm, ch->h_channel_group,
                           NVA06C_CTRL_CMD_GPFIFO_SCHEDULE,
                           &gsched, sizeof(gsched));
      if (sret == 0) {
         ch->scheduled = true;
         ch->schedule_rc = 0;
         ch->schedule_path = 1;
         ch->use_channel_group = true;
         return 0;
      }
      last_err = sret;
   }

   /* --- Channel path: A06F_BIND then A06F_GPFIFO_SCHEDULE --- */
   {
      NVA06F_CTRL_BIND_PARAMS cbind;
      NVA06F_CTRL_GPFIFO_SCHEDULE_PARAMS sched;

      memset(&cbind, 0, sizeof(cbind));
      cbind.engineType = ch->engine_type;
      sret = nv_rm_control(rm, ch->h_channel, NVA06F_CTRL_CMD_BIND,
                           &cbind, sizeof(cbind));
      if (ch->schedule_bind_rc != 0)
         ch->schedule_bind_rc = sret;

      memset(&sched, 0, sizeof(sched));
      sched.bEnable = NV_TRUE;
      sched.bSkipSubmit = NV_FALSE;
      sret = nv_rm_control(rm, ch->h_channel, NVA06F_CTRL_CMD_GPFIFO_SCHEDULE,
                           &sched, sizeof(sched));
      if (sret == 0) {
         ch->scheduled = true;
         ch->schedule_rc = 0;
         ch->schedule_path = 2; /* A06F channel */
         return 0;
      }
      last_err = sret;

      /* Plain schedule without BIND (legacy path that worked before tick79) */
      memset(&sched, 0, sizeof(sched));
      sched.bEnable = NV_TRUE;
      sched.bSkipSubmit = NV_FALSE;
      sret = nv_rm_control(rm, ch->h_channel, NVA06F_CTRL_CMD_GPFIFO_SCHEDULE,
                           &sched, sizeof(sched));
      if (sret == 0) {
         ch->scheduled = true;
         ch->schedule_rc = 0;
         ch->schedule_path = 2;
         return 0;
      }
      last_err = sret;
   }

   /* Final TSG-only schedule if group exists but channel-first was preferred earlier */
   if (!ch->scheduled && ch->h_channel_group) {
      NVA06C_CTRL_GPFIFO_SCHEDULE_PARAMS gsched;
      memset(&gsched, 0, sizeof(gsched));
      gsched.bEnable = NV_TRUE;
      gsched.bSkipSubmit = NV_FALSE;
      sret = nv_rm_control(rm, ch->h_channel_group,
                           NVA06C_CTRL_CMD_GPFIFO_SCHEDULE,
                           &gsched, sizeof(gsched));
      if (sret == 0) {
         ch->scheduled = true;
         ch->schedule_rc = 0;
         ch->schedule_path = 1;
         ch->use_channel_group = true;
         return 0;
      }
      last_err = sret;
   }

   ch->schedule_rc = last_err;
   ch->schedule_path = 0;
   return last_err;
}
#endif /* HAVE_LIBDRM_NVIDIA */

/* Fallback class IDs (OGKM + 610.43.02 binary ladders; prefer refined/bound over these) */
#ifndef NV_CH_FALLBACK_COPY
#define NV_CH_FALLBACK_COPY     0x0000c8b5u  /* HOPPER_DMA_COPY_A — common in 610 RE */
#endif
#ifndef NV_CH_FALLBACK_COMPUTE
#define NV_CH_FALLBACK_COMPUTE  0x0000c7c0u  /* AMPERE_COMPUTE_B / Hopper-line methods */
#endif
#ifndef NV_CH_FALLBACK_3D
#define NV_CH_FALLBACK_3D       0x0000c797u  /* AMPERE_B_3D_B — common in 610 RE */
#endif

uint32_t
nv_channel_resolve_class_copy(const struct nv_channel *ch, uint32_t explicit_class)
{
   if (explicit_class)
      return explicit_class;
   /* Prefer class that successfully RmAlloc'd under this channel */
   if (ch && ch->class_copy_bound)
      return ch->class_copy_bound;
   if (ch && ch->info && ch->info->class_copy)
      return ch->info->class_copy;
   return NV_CH_FALLBACK_COPY;
}

uint32_t
nv_channel_resolve_class_compute(const struct nv_channel *ch,
                                 uint32_t explicit_class)
{
   if (explicit_class)
      return explicit_class;
   if (ch && ch->class_compute_bound)
      return ch->class_compute_bound;
   if (ch && ch->info && ch->info->class_compute)
      return ch->info->class_compute;
   return NV_CH_FALLBACK_COMPUTE;
}

uint32_t
nv_channel_resolve_class_3d(const struct nv_channel *ch, uint32_t explicit_class)
{
   if (explicit_class)
      return explicit_class;
   if (ch && ch->class_3d_bound)
      return ch->class_3d_bound;
   if (ch && ch->info && ch->info->class_3d)
      return ch->info->class_3d;
   return NV_CH_FALLBACK_3D;
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
   /* GPGet/GPPut/Put/Get must start at 0 or first submit races with garbage */
   nvidia_userd_init_host(ch->userd, NV_CHANNEL_USERD_SIZE);
   ch->gpfifo_put = 0;

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
      /*
       * Channel alloc retries (RM is picky about TSG/USERD/VASpace/error ctx).
       * Outer loop: GPFIFO class ladder (C86F→C76F→…; 610.43.02 binary RE).
       * Inner loop: progressively strip optional fields; keep gpFifoOffset/entries.
       */
      struct {
         bool use_tsg_parent;
         bool use_userd;
         bool use_ctxshare;
         bool use_vaspace;
         bool use_error_ctxdma; /* false => notifier mem handle only */
      } attempts[] = {
         { true,  true,  true,  true,  true  },
         { true,  true,  false, true,  true  },
         { false, true,  false, true,  true  },
         { false, false, false, true,  true  },
         { false, false, false, true,  false },
         { false, false, false, false, false },
      };
      uint32_t gpfifo_classes[12];
      unsigned n_gpf = sizeof(gpfifo_classes) / sizeof(gpfifo_classes[0]);
      unsigned gi, ai;
      uint32_t h_dev = nv_rm_device_device_handle(rm);
      int last_ret = -1;

      nv_device_info_fill_class_ladder(5, ch->gpfifo_class, gpfifo_classes,
                                       &n_gpf);

      for (gi = 0; gi < n_gpf && !ch->h_channel; gi++) {
         uint32_t gpf_class = gpfifo_classes[gi];
         if (!gpf_class)
            continue;

         for (ai = 0; ai < sizeof(attempts) / sizeof(attempts[0]); ai++) {
            uint32_t h = 0;
            uint32_t h_parent = h_dev;

            memset(&ch_params, 0, sizeof(ch_params));
            if (attempts[ai].use_error_ctxdma && ch->h_error_ctxdma)
               ch_params.hObjectError = ch->h_error_ctxdma;
            else
               ch_params.hObjectError = ch->h_error_notifier;
            ch_params.gpFifoOffset = ch->gpfifo_gpu_addr;
            ch_params.gpFifoEntries = gpfifo_entries;
            ch_params.flags = 0;
            ch_params.engineType = ch->engine_type;
            if (attempts[ai].use_vaspace)
               ch_params.hVASpace = ch->h_vaspace;
            if (attempts[ai].use_ctxshare && ch->h_ctxshare)
               ch_params.hContextShare = ch->h_ctxshare;
            if (attempts[ai].use_userd && ch->h_userd_mem) {
               ch_params.hUserdMemory[0] = ch->h_userd_mem;
               ch_params.userdOffset[0] = 0;
            }
            if (attempts[ai].use_tsg_parent && ch->h_channel_group)
               h_parent = ch->h_channel_group;

            ret = nv_rm_alloc_object(rm, h_parent, &h,
                                     gpf_class, &ch_params,
                                     sizeof(ch_params));
            last_ret = ret;
            if (ret == 0) {
               ch->h_channel = h;
               ch->gpfifo_class = gpf_class;
               if (!attempts[ai].use_tsg_parent)
                  ch->use_channel_group = false;
               break;
            }
         }
      }
      if (!ch->h_channel) {
         (void)last_ret;
         goto fail;
      }
   }

   /* Schedule: A06C BIND+SCHEDULE and/or A06F BIND+SCHEDULE (tick79 / pass5). */
   ch->schedule_rc = -EAGAIN;
   ch->schedule_path = 0;
   ch->schedule_bind_rc = -1;
   (void)nv_channel_try_schedule(ch);

   /* Work submit token (Turing+ / class > C36E): NOTIF_INDEX then GET_TOKEN.
    * 610.43.02 glcore RE (a53229 then a53269); try channel then TSG parent. */
   if (ch->gpfifo_class == 0 || ch->gpfifo_class > 0xc36eu) {
      NVC36F_CTRL_CMD_GPFIFO_GET_WORK_SUBMIT_TOKEN_PARAMS tok;
      NVC36F_CTRL_GPFIFO_SET_WORK_SUBMIT_TOKEN_NOTIF_INDEX_PARAMS nip;
      uint32_t token_parents[2];
      unsigned ti, ntp = 0;

      token_parents[ntp++] = ch->h_channel;
      if (ch->h_channel_group)
         token_parents[ntp++] = ch->h_channel_group;
      for (ti = 0; ti < ntp && !ch->has_work_submit_token; ti++) {
         memset(&nip, 0, sizeof(nip));
         nip.index = NV_CHANNELGPFIFO_NOTIFICATION_TYPE_WORK_SUBMIT_TOKEN;
         (void)nv_rm_control(rm, token_parents[ti],
                             NVC36F_CTRL_CMD_GPFIFO_SET_WORK_SUBMIT_TOKEN_NOTIF_INDEX,
                             &nip, sizeof(nip));
         memset(&tok, 0, sizeof(tok));
         if (nv_rm_control(rm, token_parents[ti],
                           NVC36F_CTRL_CMD_GPFIFO_GET_WORK_SUBMIT_TOKEN,
                           &tok, sizeof(tok)) == 0) {
            ch->work_submit_token = tok.workSubmitToken;
            ch->has_work_submit_token = true;
         }
      }
   }

   /* Ensure VAS mappings for ring/push/userd (may refine gpu_addr after alloc) */
   (void)nv_channel_ensure_buffers_gpu_va(ch);

   /* Engine objects under channel (copy/compute/3d) — best-effort before first methods */
   (void)nv_channel_ensure_engine_objects(ch);

   /* Refresh usermode map if device got it after channel start */
   if (!ch->usermode_map)
      ch->usermode_map = nv_rm_device_usermode_map(rm);

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
      /* Free engine objects before channel (use recorded alloc parent) */
      if (ch->h_obj_copy) {
         uint32_t hp = ch->h_obj_copy_parent ? ch->h_obj_copy_parent
                                             : ch->h_channel;
         if (hp)
            nv_rm_free_object(ch->rm, hp, ch->h_obj_copy);
      }
      if (ch->h_obj_compute) {
         uint32_t hp = ch->h_obj_compute_parent ? ch->h_obj_compute_parent
                                                : ch->h_channel;
         if (hp)
            nv_rm_free_object(ch->rm, hp, ch->h_obj_compute);
      }
      if (ch->h_obj_3d) {
         uint32_t hp = ch->h_obj_3d_parent ? ch->h_obj_3d_parent : ch->h_channel;
         if (hp)
            nv_rm_free_object(ch->rm, hp, ch->h_obj_3d);
      }
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

/*
 * Try RmAlloc(h_class) under channel, then device, then subdevice.
 * Some RM builds only accept engine objects under one of these parents.
 */
static int
nv_channel_try_alloc_engine(struct nv_channel *ch, uint32_t h_class,
                            uint32_t *h_out, uint32_t *h_parent_out)
{
#if !defined(HAVE_LIBDRM_NVIDIA)
   (void)ch; (void)h_class; (void)h_out; (void)h_parent_out;
   return -ENOSYS;
#else
   uint32_t parents[4];
   unsigned n = 0, i;
   int last = -ENOENT;

   if (!ch || !ch->rm || !h_class || !h_out)
      return -EINVAL;
   if (h_parent_out)
      *h_parent_out = 0;

   parents[n++] = ch->h_channel;
   if (ch->h_channel_group)
      parents[n++] = ch->h_channel_group;
   parents[n++] = nv_rm_device_device_handle(ch->rm);
   {
      uint32_t h_sub = nv_rm_device_subdevice_handle(ch->rm);
      if (h_sub)
         parents[n++] = h_sub;
   }

   for (i = 0; i < n; i++) {
      uint32_t h = 0;
      int r;
      if (!parents[i])
         continue;
      r = nv_rm_alloc_object(ch->rm, parents[i], &h, h_class, NULL, 0);
      if (r == 0 && h) {
         *h_out = h;
         if (h_parent_out)
            *h_parent_out = parents[i];
         return 0;
      }
      last = r ? r : -EIO;
   }
   return last;
#endif
}

int
nv_channel_ensure_engine_objects(struct nv_channel *ch)
{
#if !defined(HAVE_LIBDRM_NVIDIA)
   (void)ch;
   return -ENOSYS;
#else
   uint32_t cc, ccomp, c3;
   /* Newest-first ladders (610.43.02 binary RE + OGKM); prefer refined/bound first */
   uint32_t copy_alts[12];
   uint32_t compute_alts[12];
   uint32_t t3d_alts[12];
   unsigned n_copy = 12, n_comp = 12, n_3d = 12;
   int any_ok = 0;
   int last_fail = 0;
   unsigned ai;

   if (!ch || !ch->rm || !ch->h_channel)
      return -EINVAL;

   /*
    * Use info/fallback only (not class_*_bound yet) to avoid skipping alternates
    * when a prior partial bind exists.
    */
   cc = 0;
   ccomp = 0;
   c3 = 0;
   if (ch->info) {
      cc = ch->info->class_copy;
      ccomp = ch->info->class_compute;
      c3 = ch->info->class_3d;
   }
   if (!cc)
      cc = NV_CH_FALLBACK_COPY;
   if (!ccomp)
      ccomp = NV_CH_FALLBACK_COMPUTE;
   if (!c3)
      c3 = NV_CH_FALLBACK_3D;

   n_copy = sizeof(copy_alts) / sizeof(copy_alts[0]);
   nv_device_info_fill_class_ladder(2, cc, copy_alts, &n_copy);
   n_comp = sizeof(compute_alts) / sizeof(compute_alts[0]);
   nv_device_info_fill_class_ladder(1, ccomp, compute_alts, &n_comp);
   n_3d = sizeof(t3d_alts) / sizeof(t3d_alts[0]);
   nv_device_info_fill_class_ladder(0, c3, t3d_alts, &n_3d);

   if (!ch->h_obj_copy) {
      uint32_t tried[16];
      unsigned nt = 0;
      for (ai = 0; ai < n_copy; ai++) {
         uint32_t cl = copy_alts[ai];
         uint32_t h = 0;
         unsigned t;
         int r;
         if (!cl)
            continue;
         for (t = 0; t < nt; t++)
            if (tried[t] == cl)
               break;
         if (t < nt)
            continue;
         if (nt < 16)
            tried[nt++] = cl;
         r = nv_channel_try_alloc_engine(ch, cl, &h, &ch->h_obj_copy_parent);
         if (r == 0 && h) {
            ch->h_obj_copy = h;
            ch->class_copy_bound = cl;
            any_ok = 1;
            break;
         }
         last_fail = r ? r : -EIO;
      }
   } else {
      any_ok = 1;
   }

   if (!ch->h_obj_compute) {
      uint32_t tried[16];
      unsigned nt = 0;
      for (ai = 0; ai < n_comp; ai++) {
         uint32_t cl = compute_alts[ai];
         uint32_t h = 0;
         unsigned t;
         int r;
         if (!cl)
            continue;
         for (t = 0; t < nt; t++)
            if (tried[t] == cl)
               break;
         if (t < nt)
            continue;
         if (nt < 16)
            tried[nt++] = cl;
         r = nv_channel_try_alloc_engine(ch, cl, &h, &ch->h_obj_compute_parent);
         if (r == 0 && h) {
            ch->h_obj_compute = h;
            ch->class_compute_bound = cl;
            any_ok = 1;
            break;
         }
         if (!last_fail)
            last_fail = r ? r : -EIO;
      }
   } else {
      any_ok = 1;
   }

   if (!ch->h_obj_3d) {
      uint32_t tried[16];
      unsigned nt = 0;
      for (ai = 0; ai < n_3d; ai++) {
         uint32_t cl = t3d_alts[ai];
         uint32_t h = 0;
         unsigned t;
         int r;
         if (!cl)
            continue;
         for (t = 0; t < nt; t++)
            if (tried[t] == cl)
               break;
         if (t < nt)
            continue;
         if (nt < 16)
            tried[nt++] = cl;
         r = nv_channel_try_alloc_engine(ch, cl, &h, &ch->h_obj_3d_parent);
         if (r == 0 && h) {
            ch->h_obj_3d = h;
            ch->class_3d_bound = cl;
            any_ok = 1;
            break;
         }
         if (!last_fail)
            last_fail = r ? r : -EIO;
      }
   } else {
      any_ok = 1;
   }

   /*
    * If RM bound a different class than classlist refine, prefer the bound
    * class for SET_OBJECT / resolve helpers on this channel (stored in
    * class_*_bound; callers can read via channel fields).
    */
   ch->engine_alloc_rc = any_ok ? 0 : (last_fail ? last_fail : -ENOENT);
   return ch->engine_alloc_rc;
#endif
}

int
nv_channel_ensure_buffers_gpu_va(struct nv_channel *ch)
{
#if !defined(HAVE_LIBDRM_NVIDIA)
   (void)ch;
   return -ENOSYS;
#else
   int r = 0, one;

   if (!ch || !ch->rm)
      return -EINVAL;

   (void)nv_rm_device_ensure_vaspace(ch->rm);

   if (ch->userd_bo) {
      one = nv_rm_bo_map_gpu_va(ch->userd_bo);
      if (one == 0)
         /* refresh CPU/GPU views if needed; userd pointer is CPU map */
         ;
      else if (!r)
         r = one;
   }
   if (ch->gpfifo_bo) {
      one = nv_rm_bo_map_gpu_va(ch->gpfifo_bo);
      if (one == 0)
         ch->gpfifo_gpu_addr = nv_rm_bo_gpu_offset(ch->gpfifo_bo);
      else if (!r)
         r = one;
   }
   if (ch->push_bo) {
      one = nv_rm_bo_map_gpu_va(ch->push_bo);
      if (one == 0)
         ch->push_gpu_addr = nv_rm_bo_gpu_offset(ch->push_bo);
      else if (!r)
         r = one;
   }
   if (ch->notifier_bo)
      (void)nv_rm_bo_map_gpu_va(ch->notifier_bo);

   return r;
#endif
}

int
nv_channel_ensure_submit_ready(struct nv_channel *ch)
{
#if !defined(HAVE_LIBDRM_NVIDIA)
   (void)ch;
   return -ENOSYS;
#else
   if (!ch || !ch->rm || !ch->h_channel)
      return -EINVAL;

   /* GPU VAs for GPFIFO/push must be valid before kickoff (NVOS46 remap if needed) */
   (void)nv_channel_ensure_buffers_gpu_va(ch);

   /* Engine objects (idempotent) — needed before CE/compute/3D methods on some RM builds */
   (void)nv_channel_ensure_engine_objects(ch);

   /* Schedule if create-time schedule failed (channel won't run methods) */
   if (!ch->scheduled)
      (void)nv_channel_try_schedule(ch);

   /* Doorbell prerequisites (Volta+ / class > C36E): usermode + work_submit_token.
    * 610.43.02 glcore RE (ac5557): doorbell path only when gpfifo_class > 0xC36E.
    * Blob channel setup (a53229/a53269): SET_NOTIF_INDEX then GET_WORK_SUBMIT_TOKEN.
    */
   if (!ch->usermode_map && ch->rm) {
      (void)nv_rm_device_ensure_usermode(ch->rm);
      ch->usermode_map = nv_rm_device_usermode_map(ch->rm);
   }
   if (!ch->has_work_submit_token && ch->rm &&
       (ch->gpfifo_class == 0 || ch->gpfifo_class > 0xc36eu)) {
      NVC36F_CTRL_CMD_GPFIFO_GET_WORK_SUBMIT_TOKEN_PARAMS tok;
      NVC36F_CTRL_GPFIFO_SET_WORK_SUBMIT_TOKEN_NOTIF_INDEX_PARAMS nip;
      uint32_t token_parents[2];
      unsigned ti, ntp = 0;

      token_parents[ntp++] = ch->h_channel;
      if (ch->h_channel_group)
         token_parents[ntp++] = ch->h_channel_group;
      for (ti = 0; ti < ntp; ti++) {
         /* Best-effort: bind error-context notifier slot before token fetch */
         memset(&nip, 0, sizeof(nip));
         nip.index = 0;
         (void)nv_rm_control(ch->rm, token_parents[ti],
                             NVC36F_CTRL_CMD_GPFIFO_SET_WORK_SUBMIT_TOKEN_NOTIF_INDEX,
                             &nip, sizeof(nip));
         memset(&tok, 0, sizeof(tok));
         if (nv_rm_control(ch->rm, token_parents[ti],
                           NVC36F_CTRL_CMD_GPFIFO_GET_WORK_SUBMIT_TOKEN,
                           &tok, sizeof(tok)) == 0) {
            ch->work_submit_token = tok.workSubmitToken;
            ch->has_work_submit_token = true;
            break;
         }
      }
      /* Pre-Turing (class <= C36E): token ctrl may fail; GPPut-only ok */
   }

   return 0;
#endif
}

int
nv_channel_submit_preflight(struct nv_channel *ch, int *detail_out)
{
#if !defined(HAVE_LIBDRM_NVIDIA)
   (void)ch;
   if (detail_out)
      *detail_out = -ENOSYS;
   return -ENOSYS;
#else
   int r;

   if (detail_out)
      *detail_out = 0;
   if (!ch || !ch->rm || !ch->h_channel) {
      if (detail_out)
         *detail_out = -EINVAL;
      return -EINVAL;
   }
   if (!ch->gpfifo_cpu || !ch->userd || !ch->push_cpu || !ch->push_gpu_addr) {
      if (detail_out)
         *detail_out = -EINVAL;
      return -EINVAL;
   }

   r = nv_channel_ensure_submit_ready(ch);
   if (r) {
      if (detail_out)
         *detail_out = r;
      return r;
   }

   if (!ch->scheduled) {
      if (detail_out)
         *detail_out = -EAGAIN; /* schedule failed; methods won't run */
      return -EAGAIN;
   }

   /* Token+doorbell optional (pre-Volta GPPut-only); report via detail 1 if missing */
   if (detail_out && !(ch->has_work_submit_token && ch->usermode_map))
      *detail_out = 1; /* non-fatal: GPPut-only kick path */

   return 0;
#endif
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
   bool ring_doorbell;

   if (!ch || !ch->gpfifo_cpu || !ch->userd || !ch->push_cpu)
      return -EINVAL;

   seg_dwords = ch->push_dw_used - ch->push_dw_base;
   if (seg_dwords == 0)
      return 0;

   (void)nv_channel_ensure_submit_ready(ch);

   /* Align segment end for some engines that want 4-dword alignment */
   while (seg_dwords & 3) {
      ch->push_cpu[ch->push_dw_used++] = 0; /* NOP padding if needed */
      seg_dwords++;
   }

   pb_addr = ch->push_gpu_addr + (uint64_t)ch->push_dw_base * 4;

   /*
    * libdrm submit_one_ex: entry → USERD.GPPut@0x8c → (if class>C36E) sfence +
    * doorbell@usermode+0x90 with work_submit_token.  Matches 610.43.02 glcore@ac5540.
    * Pass usermode/token whenever available; class gate is applied inside submit_one_ex.
    */
   ring_doorbell = ch->has_work_submit_token && ch->usermode_map != NULL &&
                   nvidia_gpfifo_class_needs_doorbell(ch->gpfifo_class);

   r = nvidia_gpfifo_submit_one_ex(ch->gpfifo_cpu, ch->gpfifo_entries,
                                   &ch->gpfifo_put, ch->userd,
                                   pb_addr, seg_dwords,
                                   ch->usermode_map,
                                   ch->work_submit_token,
                                   ch->has_work_submit_token,
                                   ch->gpfifo_class,
                                   1000000000ull /* 1s ring-full stall */);
   if (r)
      return r;

   /*
    * If class gate or missing token prevented doorbell inside submit_one_ex but
    * we now have token+map and class needs doorbell, ring once (no double-ring
    * when submit_one_ex already rang).
    */
   if (!ring_doorbell && ch->usermode_map && ch->has_work_submit_token &&
       nvidia_gpfifo_class_needs_doorbell(ch->gpfifo_class))
      nvidia_rm_doorbell_ring(ch->usermode_map, ch->work_submit_token);

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
   int pre;

   if (!ch || !src_gpu_addr || !dst_gpu_addr || !size_bytes || !sema_gpu_addr)
      return -EINVAL;
   if (!sema_payload)
      sema_payload = 0x42u;

   /* Fail fast if channel cannot run methods (schedule/USERD/push missing) */
   pre = nv_channel_submit_preflight(ch, NULL);
   if (pre)
      return pre;

   cc = nv_channel_resolve_class_copy(ch, class_copy);

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

static int
g1_copy_sema_one_subch(struct nv_channel *ch, uint32_t cc, uint32_t subch,
                       bool pipelined,
                       uint64_t src_gpu_addr, uint64_t dst_gpu_addr,
                       uint32_t size_bytes, uint64_t sema_gpu_addr,
                       volatile uint32_t *sema_cpu, uint32_t sema_payload,
                       bool sema_reset, uint64_t wait_timeout_ns,
                       bool check_notifier)
{
   struct nv_push push;
   uint32_t *map;
   uint32_t need = 64;
   int pre;

   pre = nv_channel_submit_preflight(ch, NULL);
   if (pre)
      return pre;

   if (sema_reset && sema_cpu)
      sema_cpu[0] = 0;

   map = nv_channel_push_begin(ch, need);
   if (!map)
      return -ENOMEM;

   nv_push_init(&push, map, need);
   /* subch 4 = NV_PUSH_SUBCH_COPY (610 RE primary); subch 0 = fallback */
   nv_copy_set_object_subch(&push, subch, cc);
   if (pipelined)
      nv_copy_emit_buffer_copy_with_sema_pipelined(&push, src_gpu_addr,
                                                   dst_gpu_addr, size_bytes,
                                                   sema_gpu_addr, sema_payload);
   else
      nv_copy_emit_buffer_copy_with_sema(&push, src_gpu_addr, dst_gpu_addr,
                                         size_bytes, sema_gpu_addr, sema_payload);
   nv_channel_push_advance(ch, nv_push_dw_count(&push));

   return nv_channel_submit_wait_sema(ch, sema_cpu, sema_payload,
                                      wait_timeout_ns, check_notifier);
}

static int
g1_copy_sema_one(struct nv_channel *ch, uint32_t cc, bool pipelined,
                 uint64_t src_gpu_addr, uint64_t dst_gpu_addr,
                 uint32_t size_bytes, uint64_t sema_gpu_addr,
                 volatile uint32_t *sema_cpu, uint32_t sema_payload,
                 bool sema_reset, uint64_t wait_timeout_ns,
                 bool check_notifier)
{
   int r;

   /* Prefer COPY subch 4 (binary RE); fall back to subch 0 if CE never completes */
   r = g1_copy_sema_one_subch(ch, cc, NV_PUSH_SUBCH_COPY, pipelined,
                              src_gpu_addr, dst_gpu_addr, size_bytes,
                              sema_gpu_addr, sema_cpu, sema_payload,
                              sema_reset, wait_timeout_ns, check_notifier);
   if (r == 0 || r == -EAGAIN || r == -EINVAL || r == -ENOSYS)
      return r;
   return g1_copy_sema_one_subch(ch, cc, NV_PUSH_SUBCH_3D, pipelined,
                                 src_gpu_addr, dst_gpu_addr, size_bytes,
                                 sema_gpu_addr, sema_cpu, sema_payload,
                                 sema_reset, wait_timeout_ns, check_notifier);
}

int
nv_channel_g1_ce_copy_sema_submit_try_classes(struct nv_channel *ch,
                                              uint64_t src_gpu_addr,
                                              uint64_t dst_gpu_addr,
                                              uint32_t size_bytes,
                                              uint64_t sema_gpu_addr,
                                              volatile uint32_t *sema_cpu,
                                              uint32_t sema_payload,
                                              bool sema_reset,
                                              uint64_t wait_timeout_ns,
                                              bool check_notifier,
                                              bool try_pipelined,
                                              uint32_t *class_used_out)
{
   uint32_t classes[16];
   unsigned n = 16, i, pipe_pass;
   int last = -EINVAL;
   uint32_t tried[16];
   unsigned nt = 0;
   uint32_t prefer = 0;

   if (!ch || !src_gpu_addr || !dst_gpu_addr || !size_bytes || !sema_gpu_addr)
      return -EINVAL;
   if (!sema_payload)
      sema_payload = 0x42u;
   if (class_used_out)
      *class_used_out = 0;

   /* Prefer RmAlloc'd class, then device refine, then 610 RE newest-first ladder */
   if (ch->class_copy_bound)
      prefer = ch->class_copy_bound;
   else if (ch->info && ch->info->class_copy)
      prefer = ch->info->class_copy;
   else
      prefer = nv_channel_resolve_class_copy(ch, 0);
   nv_device_info_fill_class_ladder(2, prefer, classes, &n);

   for (pipe_pass = 0; pipe_pass < (try_pipelined ? 2u : 1u); pipe_pass++) {
      bool pipelined = (pipe_pass == 1);
      nt = 0;
      for (i = 0; i < n; i++) {
         uint32_t cc = classes[i];
         unsigned t;
         int r;
         if (!cc)
            continue;
         for (t = 0; t < nt; t++)
            if (tried[t] == cc)
               break;
         if (t < nt)
            continue;
         if (nt < 16)
            tried[nt++] = cc;

         r = g1_copy_sema_one(ch, cc, pipelined, src_gpu_addr, dst_gpu_addr,
                              size_bytes, sema_gpu_addr, sema_cpu, sema_payload,
                              sema_reset, wait_timeout_ns, check_notifier);
         if (r == 0) {
            if (class_used_out)
               *class_used_out = cc;
            /* Remember working class for future resolves on this channel */
            if (!ch->class_copy_bound)
               ch->class_copy_bound = cc;
            return 0;
         }
         last = r;
         /* Hard channel issues: don't burn timeout budget on more classes */
         if (r == -EAGAIN || r == -EINVAL || r == -ENOSYS)
            return r;
      }
   }
   return last;
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
   int pre;

   if (!ch || !sema_gpu_addr)
      return -EINVAL;
   if (!sema_payload)
      sema_payload = 0x42u;

   pre = nv_channel_submit_preflight(ch, NULL);
   if (pre)
      return pre;

   cc = nv_channel_resolve_class_copy(ch, class_copy);

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
nv_channel_g1_ce_remap_fill_sema_submit(struct nv_channel *ch,
                                        uint32_t class_copy,
                                        uint64_t dst_gpu_addr,
                                        uint32_t size_bytes,
                                        uint32_t fill_data,
                                        uint64_t sema_gpu_addr,
                                        volatile uint32_t *sema_cpu,
                                        uint32_t sema_payload,
                                        bool sema_reset,
                                        uint64_t wait_timeout_ns,
                                        bool check_notifier)
{
   struct nv_push push;
   uint32_t *map;
   uint32_t need = 96;
   uint32_t cc;
   int pre;

   if (!ch || !dst_gpu_addr || !size_bytes || !sema_gpu_addr)
      return -EINVAL;
   if (!sema_payload)
      sema_payload = 0x42u;

   pre = nv_channel_submit_preflight(ch, NULL);
   if (pre)
      return pre;

   cc = nv_channel_resolve_class_copy(ch, class_copy);

   if (sema_reset && sema_cpu)
      sema_cpu[0] = 0;

   map = nv_channel_push_begin(ch, need);
   if (!map)
      return -ENOMEM;

   nv_push_init(&push, map, need);
   nv_copy_set_object(&push, cc);
   nv_copy_emit_remap_fill_u32_with_sema(&push, dst_gpu_addr, size_bytes,
                                         fill_data, sema_gpu_addr, sema_payload);
   nv_channel_push_advance(ch, nv_push_dw_count(&push));

   return nv_channel_submit_wait_sema(ch, sema_cpu, sema_payload,
                                      wait_timeout_ns, check_notifier);
}

/*
 * Host sema mode ladder (pass5 RE / glcore 610.43.02 @ b6c938-b6c959):
 *   blob execute 0x1001 first (matches proprietary emit), then open bitfields.
 *   For each execute, try addr>>2 (clc36f) then addr&~3 (blob stores full lo).
 * Order prioritizes silicon bring-up fidelity over theoretical header purity.
 */
static const enum nv_host_sema_mode nv_host_sema_try_order[NV_HOST_SEMA_MODE_COUNT] = {
   NV_HOST_SEMA_MODE_BLOB_SHIFT2,
   NV_HOST_SEMA_MODE_BLOB_ALIGN4,
   NV_HOST_SEMA_MODE_OPEN_SHIFT2,
   NV_HOST_SEMA_MODE_OPEN_ALIGN4,
};

int
nv_channel_gpfifo_host_sema_submit_ex(struct nv_channel *ch,
                                      uint64_t sema_gpu_addr,
                                      volatile uint32_t *sema_cpu,
                                      uint32_t sema_payload,
                                      bool sema_reset,
                                      uint64_t wait_timeout_ns,
                                      bool check_notifier,
                                      int *mode_used_out)
{
   struct nv_push push;
   uint32_t *map;
   uint32_t need = 16;
   int pre;
   int last_rc = -EIO;
   unsigned i;

   if (mode_used_out)
      *mode_used_out = (int)NV_HOST_SEMA_MODE_BLOB_SHIFT2;

   if (!ch || !sema_gpu_addr)
      return -EINVAL;
   if (!sema_payload)
      sema_payload = 0x42u;

   pre = nv_channel_submit_preflight(ch, NULL);
   if (pre)
      return pre;

   for (i = 0; i < NV_HOST_SEMA_MODE_COUNT; i++) {
      enum nv_host_sema_mode mode = nv_host_sema_try_order[i];

      if (sema_reset && sema_cpu)
         sema_cpu[0] = 0;

      map = nv_channel_push_begin(ch, need);
      if (!map)
         return -ENOMEM;

      /* Host sema on subch 0; no engine SET_OBJECT — only GPFIFO/channel executes */
      nv_push_init(&push, map, need);
      nv_push_set_subch(&push, NV_PUSH_SUBCH_3D);
      /* WFI then sema: ensures prior segment methods complete before sema write */
      nv_push_host_semaphore_release_wfi_mode(&push, sema_gpu_addr, sema_payload,
                                              true, mode);
      nv_channel_push_advance(ch, nv_push_dw_count(&push));

      last_rc = nv_channel_submit_wait_sema(ch, sema_cpu, sema_payload,
                                            wait_timeout_ns, check_notifier);
      if (mode_used_out)
         *mode_used_out = (int)mode;
      if (last_rc == 0)
         return 0;
      /* -ETIMEDOUT / -EIO: sema did not complete — try next encoding */
      if (last_rc != -ETIMEDOUT && last_rc != -EIO && last_rc != -EAGAIN)
         return last_rc;
   }

   return last_rc;
}

int
nv_channel_gpfifo_host_sema_submit(struct nv_channel *ch,
                                   uint64_t sema_gpu_addr,
                                   volatile uint32_t *sema_cpu,
                                   uint32_t sema_payload,
                                   bool sema_reset,
                                   uint64_t wait_timeout_ns,
                                   bool check_notifier)
{
   return nv_channel_gpfifo_host_sema_submit_ex(ch, sema_gpu_addr, sema_cpu,
                                                sema_payload, sema_reset,
                                                wait_timeout_ns, check_notifier,
                                                NULL);
}

void
nv_channel_userd_snapshot(struct nv_channel *ch,
                          uint32_t *gp_get_out, uint32_t *gp_put_out,
                          uint32_t *host_put_out)
{
   if (gp_get_out)
      *gp_get_out = 0;
   if (gp_put_out)
      *gp_put_out = 0;
   if (host_put_out)
      *host_put_out = 0;
   if (!ch)
      return;
   if (host_put_out)
      *host_put_out = ch->gpfifo_put;
#if defined(HAVE_LIBDRM_NVIDIA)
   if (ch->userd)
      (void)nvidia_userd_snapshot(ch->userd, gp_get_out, gp_put_out, NULL, NULL);
#else
   (void)gp_get_out;
   (void)gp_put_out;
#endif
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
   int pre;

   if (!ch || !desc || !qmd_gpu_addr)
      return -EINVAL;
   if (!sema_payload && sema_gpu_addr)
      sema_payload = 0x42u;

   pre = nv_channel_submit_preflight(ch, NULL);
   if (pre)
      return pre;

   cc = nv_channel_resolve_class_compute(ch, class_compute);

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
nv_channel_g2_compute_smoke_sema_submit_try_classes(struct nv_channel *ch,
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
                                                    bool check_notifier,
                                                    uint32_t *class_used_out)
{
   uint32_t classes[6];
   unsigned n = 0, i;
   int last = -EINVAL;
   uint32_t tried[6];
   unsigned nt = 0;

   if (!ch || !qmd_gpu_addr)
      return -EINVAL;
   if (!sema_payload)
      sema_payload = 0x42u;
   if (class_used_out)
      *class_used_out = 0;

   if (ch->class_compute_bound)
      classes[n++] = ch->class_compute_bound;
   if (ch->info && ch->info->class_compute)
      classes[n++] = ch->info->class_compute;
   classes[n++] = nv_channel_resolve_class_compute(ch, 0);
   classes[n++] = 0x0000c7c0u;
   classes[n++] = 0x0000c6c0u;
   classes[n++] = 0x0000c5c0u;
   classes[n++] = 0x0000c3c0u;

   for (i = 0; i < n; i++) {
      uint32_t cc = classes[i];
      unsigned t;
      int r;
      if (!cc)
         continue;
      for (t = 0; t < nt; t++)
         if (tried[t] == cc)
            break;
      if (t < nt)
         continue;
      if (nt < 6)
         tried[nt++] = cc;

      r = nv_channel_g2_compute_smoke_sema_submit(ch, cc, program_gpu_addr,
                                                  register_count, sass_version,
                                                  qmd_gpu_addr, qmd_host,
                                                  sema_gpu_addr, sema_cpu,
                                                  sema_payload, sema_reset,
                                                  emit_init_state,
                                                  method_invalidate,
                                                  wait_timeout_ns,
                                                  check_notifier);
      if (r == 0) {
         if (class_used_out)
            *class_used_out = cc;
         if (!ch->class_compute_bound)
            ch->class_compute_bound = cc;
         return 0;
      }
      last = r;
      if (r == -EAGAIN || r == -EINVAL || r == -ENOSYS)
         return r;
   }
   return last;
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

   c3 = nv_channel_resolve_class_3d(ch, class_3d);

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

   c3 = nv_channel_resolve_class_3d(ch, class_3d);

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
