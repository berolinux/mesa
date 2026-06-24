/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 */

#include "nv_smoke_hw.h"
#include "nv_channel.h"
#include "nv_device_info.h"
#include "nv_rm.h"
#include "nv_shader.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef NV_SMOKE_HW_DEFAULT_TIMEOUT_NS
#define NV_SMOKE_HW_DEFAULT_TIMEOUT_NS  2000000000ull
#endif

static void
scratch_zero(struct nv_smoke_hw_scratch *sc)
{
   if (sc)
      memset(sc, 0, sizeof(*sc));
}

bool
nv_smoke_hw_env_requested(void)
{
   const char *e = getenv("NV_SMOKE_HW");
   if (!e || !e[0] || !strcmp(e, "0"))
      return false;
   return true;
}

bool
nv_smoke_hw_env_verbose(void)
{
   const char *e = getenv("NV_SMOKE_HW_VERBOSE");
   if (!e || !e[0] || !strcmp(e, "0"))
      return false;
   return true;
}

uint32_t
nv_smoke_hw_env_slices(void)
{
   const char *e = getenv("NV_SMOKE_HW_SLICES");
   unsigned long v;
   char *end = NULL;

   if (!e || !e[0])
      return NV_SMOKE_HW_ALL;
   v = strtoul(e, &end, 0);
   if (end == e || !v)
      return NV_SMOKE_HW_ALL;
   return (uint32_t)v & NV_SMOKE_HW_ALL;
}

void
nv_smoke_hw_log_result(const struct nv_smoke_hw_result *res, const char *prefix)
{
   const char *p = prefix ? prefix : "nv_smoke_hw";
   if (!res)
      return;
   fprintf(stderr,
           "%s: run=0x%x ok=0x%x g1_rc=%d g2_rc=%d g3_rc=%d"
           " g1_pre=%d g1_pre_d=%d g1_sched=%d g1_sched_rc=%d g1_eng_rc=%d g1_h_copy=0x%x g1_db=%d"
           " g1_submit=%d g1_payload=%d g1_sema_only=%d g1_remap=%d g1_host_sema=%d"
           " g1_sema=0x%x g1_fill=0x%x g1_class=0x%x g1_notif=0x%x/0x%x"
           " g1_svram=%d g1_bvram=%d g1_sgpu=0x%llx g1_dgpu=0x%llx"
           " g1_gp_get=%u g1_gp_put=%u g1_hput=%u"
           " g2_pre=%d g2_eng_rc=%d g2_h_comp=0x%x g2_submit=%d g2_store=%d"
           " g2_host_sema=%d g2_obs=0x%x g2_class=0x%x g2_prog=0x%llx\n",
           p,
           (unsigned)res->slices_run, (unsigned)res->slices_ok,
           res->g1_rc, res->g2_rc, res->g3_rc,
           res->g1_preflight_rc, res->g1_preflight_detail,
           res->g1_was_scheduled ? 1 : 0, res->g1_schedule_rc,
           res->g1_engine_alloc_rc, (unsigned)res->g1_h_obj_copy,
           res->g1_had_doorbell ? 1 : 0,
           res->g1_submit_rc, res->g1_payload_rc, res->g1_sema_only_rc,
           res->g1_remap_fill_rc, res->g1_host_sema_rc,
           (unsigned)res->g1_sema_observed, (unsigned)res->g1_fill_observed,
           (unsigned)res->g1_class_copy,
           (unsigned)res->g1_notifier_status, (unsigned)res->g1_notifier_info32,
           res->g1_sema_vram ? 1 : 0, res->g1_bufs_vram ? 1 : 0,
           (unsigned long long)res->g1_sema_gpu,
           (unsigned long long)res->g1_dst_gpu,
           (unsigned)res->g1_userd_gp_get, (unsigned)res->g1_userd_gp_put,
           (unsigned)res->g1_host_gpfifo_put,
           res->g2_preflight_rc, res->g2_engine_alloc_rc,
           (unsigned)res->g2_h_obj_compute,
           res->g2_submit_rc, res->g2_store_rc, res->g2_host_sema_rc,
           (unsigned)res->g2_store_observed,
           (unsigned)res->g2_class_compute,
           (unsigned long long)res->g2_prog_gpu);
   if (res->g1_rc < 0 && (res->slices_run & NV_SMOKE_HW_G1)) {
      fprintf(stderr,
              "%s: G1 bring-up hints: class_copy=0x%x preflight=%d (detail=%d) "
              "sched=%d schedule_rc=%d doorbell=%d submit=%d sema_only=%d remap=%d host_sema=%d\n"
              "  USERD GPGet=%u GPPut=%u host_gpfifo_put=%u (GPGet!=GPPut => ring not consumed)\n"
              "  host_sema ok, CE fails => kickoff works; fix CE class/methods or h_obj_copy alloc\n"
              "  host_sema fail => schedule/doorbell/GPPut (not CE)\n"
              "  g1_eng_rc!=0 / h_copy=0 => RmAlloc copy under channel failed (engine context)\n"
              "  g1_notif non-zero => channel error notifier (method/engine fault)\n"
              "  sema_only ok, copy fail, remap ok = pitch/src OFFSET_IN issue\n"
              "  sema_only ok, copy+remap fail = CE class/methods\n"
              "  payload_rc=-EIO = sema ok but 256B dst!=src (wrong offsets/VAS)\n",
              p, (unsigned)res->g1_class_copy, res->g1_preflight_rc,
              res->g1_preflight_detail, res->g1_was_scheduled ? 1 : 0,
              res->g1_schedule_rc, res->g1_had_doorbell ? 1 : 0,
              res->g1_submit_rc, res->g1_sema_only_rc, res->g1_remap_fill_rc,
              res->g1_host_sema_rc,
              (unsigned)res->g1_userd_gp_get, (unsigned)res->g1_userd_gp_put,
              (unsigned)res->g1_host_gpfifo_put);
   }
   if (res->g2_rc < 0 && (res->slices_run & NV_SMOKE_HW_G2)) {
      fprintf(stderr,
              "%s: G2 bring-up hints: class_compute=0x%x h_comp=0x%x eng_rc=%d "
              "prog=0x%llx submit=%d store_rc=%d host_sema=%d obs=0x%x\n"
              "  host_sema ok, submit fail => QMD/compute class/SPA (not kickoff)\n"
              "  host_sema fail => same as G1 kickoff issues\n"
              "  store_rc=-EIO => sema ok but shader store wrong; prog=0 => no store-imm shader\n",
              p, (unsigned)res->g2_class_compute, (unsigned)res->g2_h_obj_compute,
              res->g2_engine_alloc_rc,
              (unsigned long long)res->g2_prog_gpu,
              res->g2_submit_rc, res->g2_store_rc, res->g2_host_sema_rc,
              (unsigned)res->g2_store_observed);
   }
}

/*
 * Alloc scratch BO: try VRAM (GPU sema/DMA often prefers FB) then sysmem.
 * Requires CPU map + GPU VA for host verify / sema poll.
 */
static struct nv_rm_bo *
smoke_alloc_mapped_bo(struct nv_rm_device *rm, uint64_t size, uint64_t align,
                      bool prefer_vram, bool *vram_out)
{
   struct nv_rm_bo_req req;
   struct nv_rm_bo *bo = NULL;
   int pass;

   if (vram_out)
      *vram_out = false;
   if (!rm || !size)
      return NULL;

   (void)nv_rm_device_ensure_vaspace(rm);

   for (pass = 0; pass < 2; pass++) {
      bool vram = (pass == 0) ? prefer_vram : !prefer_vram;
      memset(&req, 0, sizeof(req));
      req.size = size;
      req.alignment = align ? align : 256;
      req.vram = vram;
      req.cpu_access = true;
      req.no_scanout = true;
      req.map_gpu_va = true;
      bo = nv_rm_bo_alloc(rm, &req);
      if (bo) {
         if (vram_out)
            *vram_out = vram;
         return bo;
      }
   }
   return NULL;
}

int
nv_smoke_hw_scratch_create(struct nv_rm_device *rm,
                           struct nv_smoke_hw_scratch *out)
{
   struct nv_rm_bo_req req;
   struct nv_smoke_hw_scratch sc;
   bool sema_v = false, buf_v = false;

   if (!rm || !out)
      return -EINVAL;

   scratch_zero(&sc);
   sc.rm = rm;
   sc.sema_payload = 0x42u;
   sc.g2_store_imm = 0xdeadbeefu;
   sc.owned = true;

   /* Sema: prefer VRAM (CE/host sema writes target GPU VA; host polls CPU map) */
   sc.sema_bo = smoke_alloc_mapped_bo(rm, 4096, 256, true, &sema_v);
   if (!sc.sema_bo)
      goto fail;
   sc.sema_vram = sema_v;
   sc.sema_cpu = (volatile uint32_t *)nv_rm_bo_map(sc.sema_bo);
   sc.sema_gpu = nv_rm_bo_gpu_offset(sc.sema_bo);
   if (!sc.sema_cpu || !sc.sema_gpu)
      goto fail;
   sc.sema_cpu[0] = 0;

   /* G1 src/dst: prefer sysmem for easy CPU verify; if only VRAM works, ok */
   sc.src_bo = smoke_alloc_mapped_bo(rm, 4096, 256, false, &buf_v);
   sc.dst_bo = smoke_alloc_mapped_bo(rm, 4096, 256, false, &buf_v);
   if (!sc.src_bo || !sc.dst_bo)
      goto fail;
   sc.bufs_vram = buf_v;
   sc.src_cpu = nv_rm_bo_map(sc.src_bo);
   sc.dst_cpu = nv_rm_bo_map(sc.dst_bo);
   sc.src_gpu = nv_rm_bo_gpu_offset(sc.src_bo);
   sc.dst_gpu = nv_rm_bo_gpu_offset(sc.dst_bo);
   if (!sc.src_cpu || !sc.dst_cpu || !sc.src_gpu || !sc.dst_gpu)
      goto fail;
   memset(sc.src_cpu, 0xa5, 256);
   memset(sc.dst_cpu, 0, 256);

   sc.qmd_bo = smoke_alloc_mapped_bo(rm, 4096, 256, false, NULL);
   if (!sc.qmd_bo)
      goto fail;
   sc.qmd_cpu = nv_rm_bo_map(sc.qmd_bo);
   sc.qmd_gpu = nv_rm_bo_gpu_offset(sc.qmd_bo);
   if (!sc.qmd_cpu || !sc.qmd_gpu)
      goto fail;
   memset(sc.qmd_cpu, 0, 256);

   memset(&req, 0, sizeof(req));
   req.size = 64 * 64 * 4; /* 64x64 A8B8G8R8 pitch */
   req.alignment = 256;
   req.vram = false;
   req.cpu_access = true;
   req.no_scanout = true;
   req.map_gpu_va = true;
   sc.ct_bo = nv_rm_bo_alloc(rm, &req);
   if (!sc.ct_bo) {
      req.vram = true;
      sc.ct_bo = nv_rm_bo_alloc(rm, &req);
   }
   if (sc.ct_bo) {
      sc.ct_gpu = nv_rm_bo_gpu_offset(sc.ct_bo);
      /* CT optional; G3 clear sema works without mapped CT for encode path */
   }

   *out = sc;
   return 0;

fail:
   sc.owned = true;
   nv_smoke_hw_scratch_destroy(&sc);
   scratch_zero(out);
   return -ENOMEM;
}

void
nv_smoke_hw_scratch_destroy(struct nv_smoke_hw_scratch *sc)
{
   if (!sc)
      return;
   if (!sc->owned) {
      scratch_zero(sc);
      return;
   }
   if (sc->ct_bo)
      nv_rm_bo_free(sc->ct_bo);
   if (sc->qmd_bo) {
      if (sc->qmd_cpu)
         nv_rm_bo_unmap(sc->qmd_bo);
      nv_rm_bo_free(sc->qmd_bo);
   }
   if (sc->dst_bo) {
      if (sc->dst_cpu)
         nv_rm_bo_unmap(sc->dst_bo);
      nv_rm_bo_free(sc->dst_bo);
   }
   if (sc->src_bo) {
      if (sc->src_cpu)
         nv_rm_bo_unmap(sc->src_bo);
      nv_rm_bo_free(sc->src_bo);
   }
   if (sc->sema_bo) {
      if (sc->sema_cpu)
         nv_rm_bo_unmap(sc->sema_bo);
      nv_rm_bo_free(sc->sema_bo);
   }
   scratch_zero(sc);
}

int
nv_smoke_hw_run_on_channel(struct nv_channel *ch,
                           struct nv_smoke_hw_scratch *sc,
                           uint32_t slices,
                           struct nv_shader *g2_shader,
                           uint64_t timeout_ns,
                           bool check_notifier,
                           struct nv_smoke_hw_result *result_out)
{
   struct nv_smoke_hw_result res;
   int r = 0;
   uint64_t to = timeout_ns ? timeout_ns : NV_SMOKE_HW_DEFAULT_TIMEOUT_NS;
   uint64_t prog = 0;
   uint32_t regs = 16;
   uint8_t sass = 0x86;

   memset(&res, 0, sizeof(res));
   res.g1_rc = 1;
   res.g2_rc = 1;
   res.g3_rc = 1;

   if (!ch || !sc || !sc->sema_gpu || !sc->sema_cpu)
      return -EINVAL;
   if (!slices)
      slices = NV_SMOKE_HW_ALL;

   /* Bring-up: log resolved classes once when verbose */
   if (nv_smoke_hw_env_verbose() && ch->info)
      nv_device_info_log_classes(ch->info, "nv_smoke_hw");

   if (slices & NV_SMOKE_HW_G1) {
      res.slices_run |= NV_SMOKE_HW_G1;
      (void)nv_channel_ensure_engine_objects(ch);
      res.g1_class_copy = nv_channel_resolve_class_copy(ch, 0);
      res.g1_preflight_rc = nv_channel_submit_preflight(ch, &res.g1_preflight_detail);
      res.g1_was_scheduled = ch->scheduled;
      res.g1_schedule_rc = ch->schedule_rc;
      res.g1_engine_alloc_rc = ch->engine_alloc_rc;
      res.g1_h_obj_copy = ch->h_obj_copy;
      res.g1_notifier_status = 0xffff;
      res.g1_had_doorbell = ch->has_work_submit_token && ch->usermode_map != NULL;
      res.g1_sema_vram = sc->sema_vram;
      res.g1_bufs_vram = sc->bufs_vram;
      res.g1_sema_gpu = sc->sema_gpu;
      res.g1_src_gpu = sc->src_gpu;
      res.g1_dst_gpu = sc->dst_gpu;

      if (res.g1_preflight_rc != 0 && res.g1_preflight_rc != -EAGAIN) {
         /* Hard failure: missing channel objects — skip submit noise */
         res.g1_rc = res.g1_preflight_rc;
         res.g1_submit_rc = res.g1_preflight_rc;
         r = res.g1_rc;
      } else if (!sc->src_gpu || !sc->dst_gpu) {
         res.g1_rc = -EINVAL;
         res.g1_submit_rc = -EINVAL;
         r = res.g1_rc;
      } else {
         if (sc->src_cpu)
            memset(sc->src_cpu, 0xa5, 256);
         if (sc->dst_cpu)
            memset(sc->dst_cpu, 0, 256);
         if (sc->sema_cpu)
            sc->sema_cpu[0] = 0;
         /*
          * Multi-class + optional PIPELINED LAUNCH_DMA (try_pipelined=true).
          * First success updates class_copy_bound for later sema_only/remap probes.
          */
         res.g1_used_class_try = true;
         res.g1_submit_rc = nv_channel_g1_ce_copy_sema_submit_try_classes(
            ch, sc->src_gpu, sc->dst_gpu, 256, sc->sema_gpu, sc->sema_cpu,
            sc->sema_payload, true, to, check_notifier, true /* try_pipelined */,
            &res.g1_class_copy);
         if (!res.g1_class_copy)
            res.g1_class_copy = nv_channel_resolve_class_copy(ch, 0);
         res.g1_rc = res.g1_submit_rc;
         if (sc->sema_cpu)
            res.g1_sema_observed = sc->sema_cpu[0];
         if (res.g1_submit_rc == 0) {
            /* Full 256B payload check when host maps exist */
            if (sc->src_cpu && sc->dst_cpu &&
                memcmp(sc->src_cpu, sc->dst_cpu, 256) != 0) {
               res.g1_payload_rc = -EIO;
               res.g1_rc = -EIO;
            } else {
               res.g1_payload_rc = 0;
               res.slices_ok |= NV_SMOKE_HW_G1;
            }
         } else if (res.g1_submit_rc != 0 && res.g1_submit_rc != -EAGAIN) {
            /*
             * Secondary: sema-only CE fence.  Tertiary: REMAP fill+sema (no src).
             * sema_only ok + remap ok + copy fail => pitch/src issue
             * sema_only ok + remap fail => CE class/REMAP methods
             * all fail => schedule/doorbell/GPPut not executing methods
             * Reset notifier between probes so a stale channel error does not
             * poison later wait_check results.
             */
            nv_channel_notifier_reset(ch);
            if (sc->sema_cpu)
               sc->sema_cpu[0] = 0;
            res.g1_sema_only_rc = nv_channel_g1_ce_sema_only_submit(ch, 0,
                                                                    sc->sema_gpu,
                                                                    sc->sema_cpu,
                                                                    sc->sema_payload,
                                                                    true, to,
                                                                    check_notifier);
            if (sc->sema_cpu)
               res.g1_sema_observed = sc->sema_cpu[0];

            nv_channel_notifier_reset(ch);
            if (sc->dst_cpu)
               memset(sc->dst_cpu, 0, 256);
            if (sc->sema_cpu)
               sc->sema_cpu[0] = 0;
            res.g1_remap_fill_rc = nv_channel_g1_ce_remap_fill_sema_submit(
               ch, 0, sc->dst_gpu, 256, 0xcafebabeu, sc->sema_gpu,
               sc->sema_cpu, sc->sema_payload, true, to, check_notifier);
            if (sc->sema_cpu && res.g1_remap_fill_rc == 0)
               res.g1_sema_observed = sc->sema_cpu[0];
            if (sc->dst_cpu)
               res.g1_fill_observed = ((const uint32_t *)sc->dst_cpu)[0];

            /* Quaternary: host/GPFIFO sema (no CE) — isolates kickoff vs CE */
            nv_channel_notifier_reset(ch);
            if (sc->sema_cpu)
               sc->sema_cpu[0] = 0;
            res.g1_host_sema_rc = nv_channel_gpfifo_host_sema_submit(
               ch, sc->sema_gpu, sc->sema_cpu, sc->sema_payload, true, to,
               check_notifier);
            if (sc->sema_cpu && res.g1_host_sema_rc == 0)
               res.g1_sema_observed = sc->sema_cpu[0];
         } else if (res.g1_submit_rc == -EAGAIN) {
            res.g1_sema_only_rc = -EAGAIN;
            res.g1_remap_fill_rc = -EAGAIN;
            res.g1_host_sema_rc = -EAGAIN;
         }
         nv_channel_userd_snapshot(ch, &res.g1_userd_gp_get, &res.g1_userd_gp_put,
                                   &res.g1_host_gpfifo_put);
         {
            uint16_t nst = 0xffff;
            uint32_t ninfo = 0;
            if (nv_channel_notifier_status(ch, &nst, &ninfo) == 0) {
               res.g1_notifier_status = nst;
               res.g1_notifier_info32 = ninfo;
            }
         }
         if (res.g1_rc && !r)
            r = res.g1_rc;
      }
   }

   if (slices & NV_SMOKE_HW_G2) {
      res.slices_run |= NV_SMOKE_HW_G2;
      (void)nv_channel_ensure_engine_objects(ch);
      res.g2_class_compute = nv_channel_resolve_class_compute(ch, 0);
      res.g2_engine_alloc_rc = ch->engine_alloc_rc;
      res.g2_h_obj_compute = ch->h_obj_compute;
      res.g2_preflight_rc = nv_channel_submit_preflight(ch, NULL);
      if (!sc->qmd_gpu || !sc->qmd_cpu) {
         res.g2_rc = -EINVAL;
         res.g2_submit_rc = -EINVAL;
         if (!r)
            r = res.g2_rc;
      } else if (res.g2_preflight_rc != 0 && res.g2_preflight_rc != -EAGAIN) {
         res.g2_rc = res.g2_preflight_rc;
         res.g2_submit_rc = res.g2_preflight_rc;
         if (!r)
            r = res.g2_rc;
      } else {
         uint32_t store_imm = sc->g2_store_imm ? sc->g2_store_imm : 0xdeadbeefu;
         uint64_t store_gpu = sc->dst_gpu; /* reuse G1 dst BO as G2 store target */
         bool expect_store = false;

         /* Reset sema between slices (G1 may have left payload set) */
         if (sc->sema_cpu)
            sc->sema_cpu[0] = 0;

         /* Prefer store-imm smoke shader targeting dst_bo[0] */
         if (g2_shader && sc->rm && store_gpu) {
            if (sc->dst_cpu)
               memset(sc->dst_cpu, 0, 256);
            if (nv_shader_upload_compute_smoke(g2_shader, 1 /* store_imm */,
                                               store_imm, store_gpu, 16) == 0 &&
                g2_shader->uploaded) {
               prog = g2_shader->code_gpu_addr;
               if (g2_shader->register_count)
                  regs = g2_shader->register_count;
               expect_store = true;
            }
         }
         if (!prog && g2_shader && g2_shader->uploaded) {
            prog = g2_shader->code_gpu_addr;
            if (g2_shader->register_count)
               regs = g2_shader->register_count;
         }
         res.g2_prog_gpu = prog;
         nv_channel_notifier_reset(ch);
         res.g2_submit_rc = nv_channel_g2_compute_smoke_sema_submit_try_classes(
            ch, prog, regs, sass, sc->qmd_gpu, sc->qmd_cpu, sc->sema_gpu,
            sc->sema_cpu, sc->sema_payload, true, true, true, to,
            check_notifier, &res.g2_class_compute);
         if (!res.g2_class_compute)
            res.g2_class_compute = nv_channel_resolve_class_compute(ch, 0);
         res.g2_rc = res.g2_submit_rc;
         if (sc->dst_cpu)
            res.g2_store_observed = ((volatile uint32_t *)sc->dst_cpu)[0];
         if (res.g2_submit_rc == 0) {
            /* If store-imm path and mapped, verify first dword */
            if (expect_store && store_gpu && sc->dst_cpu &&
                ((volatile uint32_t *)sc->dst_cpu)[0] != store_imm) {
               res.g2_store_rc = -EIO;
               res.g2_rc = -EIO;
            } else {
               res.g2_store_rc = 0;
               res.slices_ok |= NV_SMOKE_HW_G2;
            }
         } else if (res.g2_submit_rc != 0 && res.g2_submit_rc != -EAGAIN) {
            /* Secondary: host sema — kickoff ok vs QMD/compute class issue */
            if (sc->sema_cpu)
               sc->sema_cpu[0] = 0;
            res.g2_host_sema_rc = nv_channel_gpfifo_host_sema_submit(
               ch, sc->sema_gpu, sc->sema_cpu, sc->sema_payload, true, to,
               check_notifier);
         }
         if (res.g2_rc && !r)
            r = res.g2_rc;
      }
   }

   if (slices & NV_SMOKE_HW_G3) {
      res.slices_run |= NV_SMOKE_HW_G3;
      /* emit_draw=false: no shaders; clear+sema only (safer first HW bring-up) */
      res.g3_rc = nv_channel_g3_clear_sema_submit(ch, 0, sc->ct_gpu,
                                                  64, 64, 0, NULL,
                                                  false /* no draw */,
                                                  sc->sema_gpu, sc->sema_cpu,
                                                  sc->sema_payload, true, to,
                                                  check_notifier);
      if (res.g3_rc == 0)
         res.slices_ok |= NV_SMOKE_HW_G3;
      if (res.g3_rc && !r)
         r = res.g3_rc;
   }

   if (result_out)
      *result_out = res;
   if (nv_smoke_hw_env_verbose() || r != 0)
      nv_smoke_hw_log_result(&res, "nv_smoke_hw");
   return r;
}

int
nv_smoke_hw_run_oneshot(struct nv_rm_device *rm, struct nv_channel *ch,
                        uint32_t slices, struct nv_shader *g2_shader,
                        uint64_t timeout_ns, bool check_notifier,
                        struct nv_smoke_hw_result *result_out)
{
   struct nv_smoke_hw_scratch sc;
   int r, run_r;

   if (!rm || !ch)
      return -EINVAL;
   r = nv_smoke_hw_scratch_create(rm, &sc);
   if (r)
      return r;
   run_r = nv_smoke_hw_run_on_channel(ch, &sc, slices, g2_shader, timeout_ns,
                                      check_notifier, result_out);
   nv_smoke_hw_scratch_destroy(&sc);
   return run_r;
}

int
nv_smoke_hw_run_standalone(int drm_fd, int gpu_index, uint32_t slices,
                           uint64_t timeout_ns, bool check_notifier,
                           struct nv_smoke_hw_result *result_out)
{
   struct nv_rm_device *rm = NULL;
   struct nv_channel *ch = NULL;
   struct nv_smoke_hw_result res;
   int r, run_r = -ENODEV;

   memset(&res, 0, sizeof(res));
   res.g1_rc = 1;
   res.g2_rc = 1;
   res.g3_rc = 1;

   if (!slices)
      slices = nv_smoke_hw_env_slices();
   if (!timeout_ns)
      timeout_ns = NV_SMOKE_HW_DEFAULT_TIMEOUT_NS;

   rm = nv_rm_device_open(drm_fd, gpu_index);
   if (!rm) {
      if (nv_smoke_hw_env_verbose())
         fprintf(stderr,
                 "nv_smoke_hw_run_standalone: nv_rm_device_open(fd=%d,gpu=%d) failed "
                 "(need nvidia.ko + /dev/nvidia* / libdrm_nvidia)\n",
                 drm_fd, gpu_index);
      if (result_out)
         *result_out = res;
      return -ENODEV;
   }

   /* engine_type 0 => nv_channel_create defaults to GRAPHICS (0x1) */
   ch = nv_channel_create(rm, 0, 0, 0);
   if (!ch) {
      if (nv_smoke_hw_env_verbose())
         fprintf(stderr, "nv_smoke_hw_run_standalone: nv_channel_create failed\n");
      nv_rm_device_close(rm);
      if (result_out)
         *result_out = res;
      return -EIO;
   }

   run_r = nv_smoke_hw_run_oneshot(rm, ch, slices, NULL, timeout_ns,
                                   check_notifier, &res);
   if (result_out)
      *result_out = res;
   if (nv_smoke_hw_env_verbose() || run_r != 0)
      nv_smoke_hw_log_result(&res, "nv_smoke_hw_standalone");

   nv_channel_destroy(ch);
   nv_rm_device_close(rm);
   (void)r;
   return run_r;
}
