/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 */

#include "nv_smoke_hw.h"
#include "nv_channel.h"
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
           " g1_submit=%d g1_payload=%d g1_sema=0x%x g1_class=0x%x\n",
           p,
           (unsigned)res->slices_run, (unsigned)res->slices_ok,
           res->g1_rc, res->g2_rc, res->g3_rc,
           res->g1_submit_rc, res->g1_payload_rc,
           (unsigned)res->g1_sema_observed,
           (unsigned)res->g1_class_copy);
   if (res->g1_rc < 0 && (res->slices_run & NV_SMOKE_HW_G1)) {
      fprintf(stderr,
              "%s: G1 bring-up hints: class_copy=0x%x submit_rc=%d "
              "(EINVAL=bad class/args, ETIMEDOUT/EIO=sema wait, "
              "payload_rc=-EIO means sema ok but CE copy wrong)\n",
              p, (unsigned)res->g1_class_copy, res->g1_submit_rc);
   }
}

int
nv_smoke_hw_scratch_create(struct nv_rm_device *rm,
                           struct nv_smoke_hw_scratch *out)
{
   struct nv_rm_bo_req req;
   struct nv_smoke_hw_scratch sc;

   if (!rm || !out)
      return -EINVAL;

   scratch_zero(&sc);
   sc.rm = rm;
   sc.sema_payload = 0x42u;
   sc.g2_store_imm = 0xdeadbeefu;
   sc.owned = true;

   memset(&req, 0, sizeof(req));
   req.size = 4096;
   req.alignment = 256;
   req.vram = false;
   req.cpu_access = true;
   req.map_gpu_va = true;

   sc.sema_bo = nv_rm_bo_alloc(rm, &req);
   if (!sc.sema_bo)
      goto fail;
   sc.sema_cpu = (volatile uint32_t *)nv_rm_bo_map(sc.sema_bo);
   sc.sema_gpu = nv_rm_bo_gpu_offset(sc.sema_bo);
   if (!sc.sema_cpu || !sc.sema_gpu)
      goto fail;
   sc.sema_cpu[0] = 0;

   req.size = 4096;
   sc.src_bo = nv_rm_bo_alloc(rm, &req);
   sc.dst_bo = nv_rm_bo_alloc(rm, &req);
   if (!sc.src_bo || !sc.dst_bo)
      goto fail;
   sc.src_cpu = nv_rm_bo_map(sc.src_bo);
   sc.dst_cpu = nv_rm_bo_map(sc.dst_bo);
   sc.src_gpu = nv_rm_bo_gpu_offset(sc.src_bo);
   sc.dst_gpu = nv_rm_bo_gpu_offset(sc.dst_bo);
   if (!sc.src_cpu || !sc.dst_cpu || !sc.src_gpu || !sc.dst_gpu)
      goto fail;
   memset(sc.src_cpu, 0xa5, 256);
   memset(sc.dst_cpu, 0, 256);

   req.size = 4096;
   sc.qmd_bo = nv_rm_bo_alloc(rm, &req);
   if (!sc.qmd_bo)
      goto fail;
   sc.qmd_cpu = nv_rm_bo_map(sc.qmd_bo);
   sc.qmd_gpu = nv_rm_bo_gpu_offset(sc.qmd_bo);
   if (!sc.qmd_cpu || !sc.qmd_gpu)
      goto fail;
   memset(sc.qmd_cpu, 0, 256);

   req.size = 64 * 64 * 4; /* 64x64 A8B8G8R8 pitch */
   sc.ct_bo = nv_rm_bo_alloc(rm, &req);
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

   if (slices & NV_SMOKE_HW_G1) {
      res.slices_run |= NV_SMOKE_HW_G1;
      if (ch->info)
         res.g1_class_copy = ch->info->class_copy;
      if (!sc->src_gpu || !sc->dst_gpu) {
         res.g1_rc = -EINVAL;
         res.g1_submit_rc = -EINVAL;
         r = res.g1_rc;
      } else if (!res.g1_class_copy && !ch->info) {
         /* No class_copy on channel — submit will also fail; record early */
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
         res.g1_submit_rc = nv_channel_g1_ce_copy_sema_submit(ch, 0,
                                                              sc->src_gpu,
                                                              sc->dst_gpu,
                                                              256, sc->sema_gpu,
                                                              sc->sema_cpu,
                                                              sc->sema_payload,
                                                              true, to,
                                                              check_notifier);
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
         }
         if (res.g1_rc && !r)
            r = res.g1_rc;
      }
   }

   if (slices & NV_SMOKE_HW_G2) {
      res.slices_run |= NV_SMOKE_HW_G2;
      if (!sc->qmd_gpu || !sc->qmd_cpu) {
         res.g2_rc = -EINVAL;
         if (!r)
            r = res.g2_rc;
      } else {
         uint32_t store_imm = sc->g2_store_imm ? sc->g2_store_imm : 0xdeadbeefu;
         uint64_t store_gpu = sc->dst_gpu; /* reuse G1 dst BO as G2 store target */

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
            }
         }
         if (!prog && g2_shader && g2_shader->uploaded) {
            prog = g2_shader->code_gpu_addr;
            if (g2_shader->register_count)
               regs = g2_shader->register_count;
         }
         res.g2_rc = nv_channel_g2_compute_smoke_sema_submit(ch, 0, prog, regs,
                                                             sass, sc->qmd_gpu,
                                                             sc->qmd_cpu,
                                                             sc->sema_gpu,
                                                             sc->sema_cpu,
                                                             sc->sema_payload,
                                                             true, true, true,
                                                             to, check_notifier);
         if (res.g2_rc == 0) {
            /* If store-imm path and mapped, verify first dword */
            if (store_gpu && sc->dst_cpu &&
                ((volatile uint32_t *)sc->dst_cpu)[0] != store_imm)
               res.g2_rc = -EIO;
            else
               res.slices_ok |= NV_SMOKE_HW_G2;
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
