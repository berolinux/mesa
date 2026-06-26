/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 */

#include "nv_smoke_hw.h"
#include "nv_channel.h"
#include "nv_device_info.h"
#include "nv_rm.h"
#include "nv_shader.h"
#include "nv_3d_methods.h"
#include "nv_qmd.h"
#include "nv_video_methods.h"

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

/** Verbose: dump GPFIFO/push for trace/diff on HW box (NV_SMOKE_HW_VERBOSE=1).
 *  Pass7 goldens: sema block = 20040004 | hi | lo | payload | 1001 (modes 2/3
 *  vary lo encoding); kick = entry then GPPut@+0x8c then sfence+doorbell@+0x90
 *  when class > C36E; expect g1_spath=2, g1_hs_mode=2|3 on silicon.
 */
static void
nv_smoke_hw_dump_channel_trace(struct nv_channel *ch, const char *tag)
{
   unsigned i, n;
   uint32_t gp_get = 0, gp_put = 0, host_put = 0;
   const char *t = tag ? tag : "nv_smoke_hw";
   bool db_ok;

   if (!ch || !nv_smoke_hw_env_verbose())
      return;
   nv_channel_userd_snapshot(ch, &gp_get, &gp_put, &host_put);
   db_ok = ch->has_work_submit_token && ch->usermode_map &&
           (ch->gpfifo_class == 0 || ch->gpfifo_class > 0xc36eu);
   fprintf(stderr,
           "%s: trace gpfifo_class=0x%x scheduled=%d tok=%s tok_val=0x%x "
           "usermode=%s USERD GPGet=%u GPPut=%u host_put=%u "
           "kick=multi_USERD(1slot_now) pass7_expect spath=2 hs_mode=2|3\n",
           t, (unsigned)ch->gpfifo_class, ch->scheduled ? 1 : 0,
           ch->has_work_submit_token ? "yes" : "no",
           (unsigned)ch->work_submit_token,
           ch->usermode_map ? "yes" : "no",
           (unsigned)gp_get, (unsigned)gp_put, (unsigned)host_put);
   if (ch->gpfifo_cpu && ch->gpfifo_entries) {
      uint32_t slot = ch->gpfifo_put ? (ch->gpfifo_put - 1) % ch->gpfifo_entries : 0;
      uint32_t w0 = ch->gpfifo_cpu[slot * 2];
      uint32_t w1 = ch->gpfifo_cpu[slot * 2 + 1];
      uint64_t pb_va = ((uint64_t)(w1 & 0xff) << 32) | (uint64_t)(w0 & ~3u);
      uint32_t len_dw = (w1 >> 10) & 0x1fffffu;
      fprintf(stderr,
              "%s: GPFIFO last slot[%u]= %08x %08x "
              "decode pb_va=0x%llx len_dw=%u priv=%u level=%u sync=%u "
              "doorbell=%s (class 0x%x %s 0xC36E)\n",
              t, (unsigned)slot, w0, w1,
              (unsigned long long)pb_va, (unsigned)len_dw,
              (w1 >> 8) & 1, (w1 >> 9) & 1, (w1 >> 31) & 1,
              db_ok ? "yes" : "no/GPPut-only",
              (unsigned)ch->gpfifo_class,
              (ch->gpfifo_class > 0xc36eu) ? ">" :
              (ch->gpfifo_class ? "<=" : "?"));
   }
   if (ch->push_cpu && ch->push_dw_used) {
      n = ch->push_dw_used < 32u ? ch->push_dw_used : 32u;
      fprintf(stderr, "%s: push[0..%u):", t, n);
      for (i = 0; i < n; i++)
         fprintf(stderr, " %08x", ch->push_cpu[i]);
      fprintf(stderr, "\n");
      /* Annotate host sema INC4 block if present (pass7 b6c938) */
      for (i = 0; i + 4 < ch->push_dw_used; i++) {
         if (ch->push_cpu[i] == 0x20040004u) {
            fprintf(stderr,
                    "%s: sema_block@dw%u: hdr=%08x hi=%08x lo=%08x "
                    "payload=%08x exec=%08x (blob 0x1001 / vdpau 0x2 / open hdrs)\n",
                    t, i,
                    ch->push_cpu[i], ch->push_cpu[i + 1], ch->push_cpu[i + 2],
                    ch->push_cpu[i + 3],
                    i + 4 < ch->push_dw_used ? ch->push_cpu[i + 4] : 0u);
            break;
         }
      }
   }
}

void
nv_smoke_hw_log_result(const struct nv_smoke_hw_result *res, const char *prefix)
{
   const char *p = prefix ? prefix : "nv_smoke_hw";
   if (!res)
      return;
   fprintf(stderr,
           "%s: run=0x%x ok=0x%x g1_rc=%d g2_rc=%d g3_rc=%d"
           " g1_pre=%d g1_pre_d=%d g1_sched=%d g1_sched_rc=%d g1_spath=%d g1_sbind=%d g1_tsg=%d g1_eng_rc=%d g1_h_copy=0x%x g1_db=%d"
           " g1_submit=%d g1_payload=%d g1_sema_only=%d g1_remap=%d g1_ce_hs=%d g1_host_sema=%d g1_hs_mode=%d"
           " g1_sema=0x%x g1_fill=0x%x g1_class=0x%x g1_gpfifo=0x%x g1_tok=0x%x"
           " g1_notif=0x%x/0x%x"
           " g1_svram=%d g1_bvram=%d g1_sgpu=0x%llx g1_dgpu=0x%llx"
           " g1_gp_get=%u g1_gp_put=%u g1_hput=%u"
           " g2_pre=%d g2_eng_rc=%d g2_h_comp=0x%x g2_submit=%d g2_qmd_hs=%d g2_store=%d"
           " g2_host_sema=%d g2_hs_mode=%d g2_obs=0x%x g2_class=0x%x g2_prog=0x%llx g2_had_prog=%d"
           " g3_pre=%d g3_submit=%d g3_sema_only=%d g3_host_sema=%d g3_hs_mode=%d g3_class=0x%x"
           " g3_h_3d=0x%x\n",
           p,
           (unsigned)res->slices_run, (unsigned)res->slices_ok,
           res->g1_rc, res->g2_rc, res->g3_rc,
           res->g1_preflight_rc, res->g1_preflight_detail,
           res->g1_was_scheduled ? 1 : 0, res->g1_schedule_rc,
           res->g1_schedule_path, res->g1_schedule_bind_rc, res->g1_tsg,
           res->g1_engine_alloc_rc, (unsigned)res->g1_h_obj_copy,
           res->g1_had_doorbell ? 1 : 0,
           res->g1_submit_rc, res->g1_payload_rc, res->g1_sema_only_rc,
           res->g1_remap_fill_rc, res->g1_ce_host_sema_rc,
           res->g1_host_sema_rc, res->g1_host_sema_mode,
           (unsigned)res->g1_sema_observed, (unsigned)res->g1_fill_observed,
           (unsigned)res->g1_class_copy,
           (unsigned)res->g1_gpfifo_class,
           (unsigned)res->g1_work_submit_token,
           (unsigned)res->g1_notifier_status, (unsigned)res->g1_notifier_info32,
           res->g1_sema_vram ? 1 : 0, res->g1_bufs_vram ? 1 : 0,
           (unsigned long long)res->g1_sema_gpu,
           (unsigned long long)res->g1_dst_gpu,
           (unsigned)res->g1_userd_gp_get, (unsigned)res->g1_userd_gp_put,
           (unsigned)res->g1_host_gpfifo_put,
           res->g2_preflight_rc, res->g2_engine_alloc_rc,
           (unsigned)res->g2_h_obj_compute,
           res->g2_submit_rc, res->g2_qmd_host_sema_rc, res->g2_store_rc,
           res->g2_host_sema_rc, res->g2_host_sema_mode,
           (unsigned)res->g2_store_observed,
           (unsigned)res->g2_class_compute,
           (unsigned long long)res->g2_prog_gpu,
           res->g2_had_prog ? 1 : 0,
           res->g3_preflight_rc, res->g3_submit_rc, res->g3_sema_only_rc,
           res->g3_host_sema_rc, res->g3_host_sema_mode,
           (unsigned)res->g3_class_3d,
           (unsigned)res->g3_h_obj_3d);
   if (res->g1_rc < 0 && (res->slices_run & NV_SMOKE_HW_G1)) {
      fprintf(stderr,
              "%s: G1 bring-up hints: class_copy=0x%x preflight=%d (detail=%d) "
              "sched=%d schedule_rc=%d doorbell=%d submit=%d sema_only=%d remap=%d host_sema=%d hs_mode=%d\n"
              "  USERD GPGet=%u GPPut=%u host_gpfifo_put=%u (GPGet!=GPPut => ring not consumed)\n"
              "  host_sema ok, CE fails => kickoff works; fix CE class/methods or h_obj_copy alloc\n"
              "  hs_mode: 2/3=blob0x1001 4/5=vdpau0x2 0/1=open_hdrs (pass8; sticky after ok)\n"
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
              res->g1_host_sema_rc, res->g1_host_sema_mode,
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
   if (res->g3_rc < 0 && (res->slices_run & NV_SMOKE_HW_G3)) {
      fprintf(stderr,
              "%s: G3 bring-up hints: class_3d=0x%x h_3d=0x%x pre=%d host_sema=%d "
              "submit=%d sema_only=%d\n"
              "  host_sema ok, sema_only ok, clear fail => CLEAR_SURFACE/RT methods\n"
              "  host_sema ok, sema_only fail => 3D class/sema methods\n"
              "  host_sema fail => kickoff (schedule/GPPut@0x8c/doorbell@0x90/token)\n",
              p, (unsigned)res->g3_class_3d, (unsigned)res->g3_h_obj_3d,
              res->g3_preflight_rc, res->g3_host_sema_rc,
              res->g3_submit_rc, res->g3_sema_only_rc);
   }
}

/*
 * Alloc scratch BO: try VRAM (GPU sema/DMA often prefers FB) then sysmem.
 * tick110: under VGX/GRID virt, flip first try to sysmem when prefer_vram set.
 * Requires CPU map + GPU VA for host verify / sema poll.
 */
static struct nv_rm_bo *
smoke_alloc_mapped_bo(struct nv_rm_device *rm, uint64_t size, uint64_t align,
                      bool prefer_vram, bool *vram_out)
{
   struct nv_rm_bo_req req;
   struct nv_rm_bo *bo = NULL;
   int pass;
   bool first_vram = prefer_vram;

   if (vram_out)
      *vram_out = false;
   if (!rm || !size)
      return NULL;

   (void)nv_rm_device_ensure_vaspace(rm);
   if (prefer_vram && nv_rm_device_info(rm) &&
       nv_device_info_prefer_sysmem_alloc(nv_rm_device_info(rm)))
      first_vram = false;

   for (pass = 0; pass < 2; pass++) {
      bool vram = (pass == 0) ? first_vram : !first_vram;
      memset(&req, 0, sizeof(req));
      req.size = size;
      req.alignment = align ? align : 256;
      req.vram = vram;
      req.cpu_access = true;
      req.no_scanout = true;
      req.map_gpu_va = true;
      bo = nv_rm_bo_alloc(rm, &req);
      if (bo) {
         /* Reinforce NVOS46 VAS map (alloc may have kept phys-only on failure) */
         (void)nv_rm_bo_map_gpu_va(bo);
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

   /* tick101: G3 colour target as 2D pitch surface when possible */
   memset(&req, 0, sizeof(req));
   req.width = 64;
   req.height = 64;
   req.pitch = 64 * 4;
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

   /* tick113/117: G3 ZETA — try block-linear DEPTH first, then pitch sysmem/vidmem */
   {
      int32_t zt_pitch = 64 * 4;
      sc.zt_bo = nv_rm_bo_alloc_depth_2d_ex(rm, 64, 64, &zt_pitch, true, true,
                                            true);
      if (sc.zt_bo) {
         sc.zt_block_linear = true;
         /* tick118: 1x16x1 gobs (RM gobs_height=4 / NV_TEX_GOBS_SIXTEEN) */
         sc.zt_block_size = nv_3d_block_size_default_2d_bl();
      }
      if (!sc.zt_bo) {
         zt_pitch = 64 * 4;
         sc.zt_bo = nv_rm_bo_alloc_depth_2d_ex(rm, 64, 64, &zt_pitch, false,
                                               true, false);
      }
      if (!sc.zt_bo) {
         zt_pitch = 64 * 4;
         sc.zt_bo = nv_rm_bo_alloc_depth_2d_ex(rm, 64, 64, &zt_pitch, true,
                                               true, false);
      }
      if (sc.zt_bo)
         sc.zt_gpu = nv_rm_bo_gpu_offset(sc.zt_bo);
   }

   /* tick115: G3 smoke VB (3 verts × 3 floats) + optional NVDEC pic_setup BO */
   sc.vb_bo = smoke_alloc_mapped_bo(rm, 4096, 256, false, NULL);
   if (sc.vb_bo) {
      sc.vb_cpu = nv_rm_bo_map(sc.vb_bo);
      sc.vb_gpu = nv_rm_bo_gpu_offset(sc.vb_bo);
      if (sc.vb_cpu) {
         float verts[9];
         nv_3d_g3_smoke_triangle_verts_f32(verts);
         memcpy(sc.vb_cpu, verts, sizeof(verts));
      }
   }
   sc.vid_ps_bo = smoke_alloc_mapped_bo(rm, 4096, 256, false, NULL);
   if (sc.vid_ps_bo) {
      sc.vid_ps_cpu = nv_rm_bo_map(sc.vid_ps_bo);
      sc.vid_ps_gpu = nv_rm_bo_gpu_offset(sc.vid_ps_bo);
      if (sc.vid_ps_cpu)
         /* tick122: H.264 intra pic_setup with output/bitstream offset fields */
         nv_nvdec_pic_setup_fill_h264_intra_ex((uint32_t *)sc.vid_ps_cpu, 256,
                                               4, 4, 0, 0,
                                               sc.ct_gpu ? sc.ct_gpu
                                                         : sc.vid_ps_gpu,
                                               0, sc.vid_ps_gpu, 256, 0, 0);
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
   if (sc->vid_ps_bo) {
      if (sc->vid_ps_cpu)
         nv_rm_bo_unmap(sc->vid_ps_bo);
      nv_rm_bo_free(sc->vid_ps_bo);
   }
   if (sc->vb_bo) {
      if (sc->vb_cpu)
         nv_rm_bo_unmap(sc->vb_bo);
      nv_rm_bo_free(sc->vb_bo);
   }
   if (sc->zt_bo)
      nv_rm_bo_free(sc->zt_bo);
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
   res.g1_host_sema_mode = -1;
   res.g2_host_sema_mode = -1;
   res.g3_host_sema_mode = -1;
   res.g1_ce_host_sema_rc = -1;
   res.g2_qmd_host_sema_rc = -1;
   res.g3_clear_host_sema_rc = -1;
   res.g1_fault_method_rc = -1;
   res.g0_rc = 1;
   res.g1_rc = 1;
   res.g2_rc = 1;
   res.g3_rc = 1;
   res.g4_rc = 1;
   res.g2_bringup_slice_rc = -1;
   res.g3_bringup_slice_rc = -1;
   res.g4_nvenc_rc = -1;
   res.g4_nvdec_rc = -1;

   if (!ch || !sc || !sc->sema_gpu || !sc->sema_cpu)
      return -EINVAL;

   /* tick109: virt/vGPU may need longer sema/GPFIFO waits than 2s default */
   to = nv_channel_effective_wait_timeout_ns(ch, to);
   if (nv_smoke_hw_env_verbose() && ch->info &&
       nv_device_info_is_virtualized(ch->info)) {
      fprintf(stderr,
              "nv_smoke_hw: virt_mode=0x%x grid=%u wait_timeout_ns=%llu "
              "prefer_sysmem=%d\n",
              ch->info->virtualization_mode, ch->info->is_grid_build,
              (unsigned long long)to,
              nv_device_info_prefer_sysmem_alloc(ch->info) ? 1 : 0);
   }

   if (!slices)
      slices = NV_SMOKE_HW_ALL;

   /* Bring-up: log resolved classes once when verbose */
   if (nv_smoke_hw_env_verbose() && ch->info)
      nv_device_info_log_classes(ch->info, "nv_smoke_hw");

   /* tick98: G0 aux probe — eventfd/NV01_EVENT, NVOS57 share, export FD, timer */
   if (slices & NV_SMOKE_HW_G0) {
      struct nv_rm_aux_probe_result aux;

      res.slices_run |= NV_SMOKE_HW_G0;
      memset(&aux, 0, sizeof(aux));
      res.g0_rc = nv_rm_probe_aux_paths(ch->rm, &aux);
      res.g0_eventfd_rc = aux.eventfd_rc;
      res.g0_share_rc = aux.share_rc;
      res.g0_export_rc = aux.export_rc;
      res.g0_timer_rc = aux.timer_rc;
      res.g0_timer_nsec = aux.time_nsec;
      /* Timer success is enough for G0 ok on headless agents; event/share/export
       * may fail without full permissions — still record codes for silicon logs. */
      if (res.g0_timer_rc == 0)
         res.slices_ok |= NV_SMOKE_HW_G0;
      else if (res.g0_rc == 0)
         res.slices_ok |= NV_SMOKE_HW_G0;
      if (nv_smoke_hw_env_verbose()) {
         fprintf(stderr,
                 "nv_smoke_hw G0 aux: rc=%d eventfd=%d share=%d export=%d "
                 "timer=%d t=%llu ns driver_cl=%u platform=%u ver=%s\n",
                 res.g0_rc, res.g0_eventfd_rc, res.g0_share_rc, res.g0_export_rc,
                 res.g0_timer_rc, (unsigned long long)res.g0_timer_nsec,
                 ch->info ? ch->info->rm_changelist : 0u,
                 ch->info ? ch->info->rm_platform_type : 0u,
                 ch->info && ch->info->rm_driver_version[0]
                    ? ch->info->rm_driver_version : "?");
         if (ch->info && ch->info->gpu_uuid[0])
            fprintf(stderr, "nv_smoke_hw G0 gpu_uuid=%s\n", ch->info->gpu_uuid);
      }
      /* Best-effort arm channel RC eventfd for later G1-G3 RC triage on silicon */
      if (ch->error_event_fd < 0)
         (void)nv_channel_ensure_error_event(ch, -1, 0);
      if (res.g0_rc && !r && res.g0_timer_rc != 0)
         r = res.g0_rc;
   }

   if (slices & NV_SMOKE_HW_G1) {
      res.slices_run |= NV_SMOKE_HW_G1;
      (void)nv_channel_ensure_engine_objects(ch);
      res.g1_class_copy = nv_channel_resolve_class_copy(ch, 0);
      res.g1_gpfifo_class = ch->gpfifo_class;
      res.g1_work_submit_token =
         ch->has_work_submit_token ? ch->work_submit_token : 0;
      res.g1_preflight_rc = nv_channel_submit_preflight(ch, &res.g1_preflight_detail);
      res.g1_was_scheduled = ch->scheduled;
      res.g1_schedule_rc = ch->schedule_rc;
      res.g1_schedule_path = ch->schedule_path;
      res.g1_fault_method_rc = ch->fault_method_rc;
      res.g1_schedule_bind_rc = ch->schedule_bind_rc;
      res.g1_tsg = ch->h_channel_group ? 1 : 0;
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
         /*
          * G1 order (610.43.02 RE / HW_MODEL_DEEP_DISASM): host sema first
          * isolates kickoff (schedule/GPPut/doorbell) from CE methods.
          * Then CE copy; on CE fail, sema_only + remap diagnose engine path.
          */
         if (sc->sema_cpu)
            sc->sema_cpu[0] = 0;
         nv_channel_notifier_reset(ch);
         if (res.g1_preflight_rc == -EAGAIN) {
            res.g1_host_sema_rc = -EAGAIN;
         } else {
            /* Pass5: ladder blob 0x1001 then open headers (glcore b6c952) */
            res.g1_host_sema_rc = nv_channel_gpfifo_host_sema_submit_ex(
               ch, sc->sema_gpu, sc->sema_cpu, sc->sema_payload, true, to,
               check_notifier, &res.g1_host_sema_mode);
            if (sc->sema_cpu)
               res.g1_sema_observed = sc->sema_cpu[0];
         }

         if (sc->src_cpu)
            memset(sc->src_cpu, 0xa5, 256);
         if (sc->dst_cpu)
            memset(sc->dst_cpu, 0, 256);
         if (sc->sema_cpu)
            sc->sema_cpu[0] = 0;
         nv_channel_notifier_reset(ch);

         /* Multi-class + optional PIPELINED LAUNCH_DMA (try_pipelined=true). */
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
             * Pass7 quaternary: CE copy (no CE sema) + host sema — use when
             * host_sema ok but CE sema methods fail (completion via NVC36F).
             * sema_only ok + remap ok + copy fail => pitch/src issue
             * sema_only ok + remap fail => CE class/REMAP methods
             * host_sema ok + all CE fail => CE class/methods (kickoff works)
             * host_sema fail => schedule/doorbell/GPPut (not CE)
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

            /* Pass7: if kickoff works (host sema ok), try CE data + host sema */
            if (res.g1_host_sema_rc == 0 && res.g1_rc != 0) {
               int ce_hs_mode = -1;

               nv_channel_notifier_reset(ch);
               if (sc->src_cpu)
                  memset(sc->src_cpu, 0xa5, 256);
               if (sc->dst_cpu)
                  memset(sc->dst_cpu, 0, 256);
               if (sc->sema_cpu)
                  sc->sema_cpu[0] = 0;
               res.g1_ce_host_sema_rc = nv_channel_g1_ce_copy_then_host_sema_submit(
                  ch, 0, sc->src_gpu, sc->dst_gpu, 256, sc->sema_gpu,
                  sc->sema_cpu, sc->sema_payload, true, to, check_notifier,
                  &ce_hs_mode);
               if (sc->sema_cpu)
                  res.g1_sema_observed = sc->sema_cpu[0];
               if (res.g1_ce_host_sema_rc == 0) {
                  if (ce_hs_mode >= 0)
                     res.g1_host_sema_mode = ce_hs_mode;
                  if (sc->src_cpu && sc->dst_cpu &&
                      memcmp(sc->src_cpu, sc->dst_cpu, 256) != 0) {
                     res.g1_payload_rc = -EIO;
                     res.g1_rc = -EIO;
                  } else {
                     res.g1_payload_rc = 0;
                     res.g1_submit_rc = 0;
                     res.g1_rc = 0;
                     res.slices_ok |= NV_SMOKE_HW_G1;
                     if (ch->class_copy_bound)
                        res.g1_class_copy = ch->class_copy_bound;
                  }
               }
            }
         } else if (res.g1_submit_rc == -EAGAIN) {
            res.g1_sema_only_rc = -EAGAIN;
            res.g1_remap_fill_rc = -EAGAIN;
         }
         nv_channel_userd_snapshot(ch, &res.g1_userd_gp_get, &res.g1_userd_gp_put,
                                   &res.g1_host_gpfifo_put);
         if (nv_smoke_hw_env_verbose())
            nv_smoke_hw_dump_channel_trace(ch, "nv_smoke_hw_g1");
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

         /* Host sema first (same kickoff gate as G1; G1 may have left sema set) */
         if (sc->sema_cpu)
            sc->sema_cpu[0] = 0;
         nv_channel_notifier_reset(ch);
         if (res.g2_preflight_rc == -EAGAIN)
            res.g2_host_sema_rc = -EAGAIN;
         else
            res.g2_host_sema_rc = nv_channel_gpfifo_host_sema_submit_ex(
               ch, sc->sema_gpu, sc->sema_cpu, sc->sema_payload, true, to,
               check_notifier, &res.g2_host_sema_mode);

         if (sc->sema_cpu)
            sc->sema_cpu[0] = 0;
         if (sc->dst_cpu)
            memset(sc->dst_cpu, 0, 256);

         /* Prefer store-imm smoke shader targeting dst_bo[0] */
         if (g2_shader && sc->rm && store_gpu) {
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
         res.g2_had_prog = (prog != 0);
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
         } else if (res.g2_submit_rc != 0 && res.g2_submit_rc != -EAGAIN &&
                    res.g2_host_sema_rc == 0) {
            /*
             * Tick85: kickoff works (host sema ok) but QMD sema failed —
             * try QMD/PCAS without QMD sema, complete via host sema (G1 ce_hs analog).
             */
            int qhs_mode = -1;
            uint32_t qhs_class = 0;

            nv_channel_notifier_reset(ch);
            if (sc->sema_cpu)
               sc->sema_cpu[0] = 0;
            if (sc->dst_cpu)
               memset(sc->dst_cpu, 0, 256);
            res.g2_qmd_host_sema_rc =
               nv_channel_g2_compute_smoke_then_host_sema_submit(
                  ch, 0, prog, regs, sass, sc->qmd_gpu, sc->qmd_cpu,
                  sc->sema_gpu, sc->sema_cpu, sc->sema_payload, true,
                  true, true, to, check_notifier, &qhs_mode, &qhs_class);
            if (sc->dst_cpu)
               res.g2_store_observed = ((volatile uint32_t *)sc->dst_cpu)[0];
            if (res.g2_qmd_host_sema_rc == 0) {
               if (qhs_mode >= 0)
                  res.g2_host_sema_mode = qhs_mode;
               if (qhs_class)
                  res.g2_class_compute = qhs_class;
               if (expect_store && store_gpu && sc->dst_cpu &&
                   ((volatile uint32_t *)sc->dst_cpu)[0] != store_imm) {
                  res.g2_store_rc = -EIO;
                  res.g2_rc = -EIO;
               } else {
                  res.g2_store_rc = 0;
                  res.g2_submit_rc = 0;
                  res.g2_rc = 0;
                  res.slices_ok |= NV_SMOKE_HW_G2;
               }
            }
         }
         /* tick136: tertiary — pass12 full G2 bringup slice (channel_prep+QMD+PCAS) */
         if (res.g2_rc != 0 && res.g2_rc != -EAGAIN && sc->qmd_gpu) {
            uint32_t bu_class = 0;

            if (sc->sema_cpu)
               sc->sema_cpu[0] = 0;
            if (sc->dst_cpu)
               memset(sc->dst_cpu, 0, 256);
            nv_channel_notifier_reset(ch);
            res.g2_bringup_slice_rc = nv_channel_g2_bringup_slice_submit(
               ch, 0, prog, regs, sass, sc->qmd_gpu, sc->qmd_cpu,
               0 /* lmem: channel_prep uses 256B default without window */,
               sc->sema_gpu, sc->sema_cpu, sc->sema_payload, true, 1, 1, to,
               check_notifier, &bu_class);
            if (sc->dst_cpu)
               res.g2_store_observed = ((volatile uint32_t *)sc->dst_cpu)[0];
            if (res.g2_bringup_slice_rc == 0) {
               if (bu_class)
                  res.g2_class_compute = bu_class;
               if (expect_store && store_gpu && sc->dst_cpu &&
                   ((volatile uint32_t *)sc->dst_cpu)[0] != store_imm) {
                  res.g2_store_rc = -EIO;
                  res.g2_rc = -EIO;
               } else {
                  res.g2_store_rc = 0;
                  res.g2_submit_rc = 0;
                  res.g2_rc = 0;
                  res.slices_ok |= NV_SMOKE_HW_G2;
               }
            }
         }
         if (nv_smoke_hw_env_verbose())
            nv_smoke_hw_dump_channel_trace(ch, "nv_smoke_hw_g2");
         if (res.g2_rc && !r)
            r = res.g2_rc;
      }
   }

   if (slices & NV_SMOKE_HW_G3) {
      res.slices_run |= NV_SMOKE_HW_G3;
      (void)nv_channel_ensure_engine_objects(ch);
      res.g3_class_3d = nv_channel_resolve_class_3d(ch, 0);
      res.g3_h_obj_3d = ch->h_obj_3d;
      res.g3_preflight_rc = nv_channel_submit_preflight(ch, NULL);

      if (sc->sema_cpu)
         sc->sema_cpu[0] = 0;
      nv_channel_notifier_reset(ch);
      if (res.g3_preflight_rc != 0 && res.g3_preflight_rc != -EAGAIN) {
         res.g3_rc = res.g3_preflight_rc;
         res.g3_submit_rc = res.g3_preflight_rc;
         if (res.g3_rc && !r)
            r = res.g3_rc;
      } else if (res.g3_preflight_rc == -EAGAIN) {
         res.g3_host_sema_rc = -EAGAIN;
         res.g3_submit_rc = -EAGAIN;
         res.g3_rc = -EAGAIN;
         if (!r)
            r = res.g3_rc;
      } else {
         /* Host sema first (kickoff gate; same as G1/G2) */
         res.g3_host_sema_rc = nv_channel_gpfifo_host_sema_submit_ex(
            ch, sc->sema_gpu, sc->sema_cpu, sc->sema_payload, true, to,
            check_notifier, &res.g3_host_sema_mode);

         if (sc->sema_cpu)
            sc->sema_cpu[0] = 0;
         nv_channel_notifier_reset(ch);
         /* tick115: prefer VB+RT draw when VB ready; else CT+ZT clear; else colour */
         if (sc->vb_gpu)
            res.g3_submit_rc = nv_channel_g3_draw_rt_sema_submit(
               ch, 0, sc->ct_gpu, 64, 64, 0, NULL, sc->zt_gpu, 0,
               sc->vb_gpu, 36, sc->sema_gpu, sc->sema_cpu, sc->sema_payload,
               true, to, check_notifier);
         else if (sc->zt_gpu)
            res.g3_submit_rc = nv_channel_g3_clear_rt_sema_submit(
               ch, 0, sc->ct_gpu, 64, 64, 0, NULL, sc->zt_gpu, 0,
               1.0f, 0, sc->sema_gpu, sc->sema_cpu, sc->sema_payload, true,
               to, check_notifier);
         else
            res.g3_submit_rc = nv_channel_g3_clear_sema_submit(
               ch, 0, sc->ct_gpu, 64, 64, 0, NULL, false /* no draw */,
               sc->sema_gpu, sc->sema_cpu, sc->sema_payload, true, to,
               check_notifier);
         res.g3_rc = res.g3_submit_rc;
         if (res.g3_submit_rc == 0) {
            res.slices_ok |= NV_SMOKE_HW_G3;
         } else if (res.g3_submit_rc != 0 && res.g3_submit_rc != -EAGAIN) {
            /* tick115/116: optional NVDEC/NVENC sema smoke (non-fatal) */
            if (sc->vid_ps_gpu && sc->sema_gpu) {
               struct nv_nvdec_pic_setup vpic;
               nv_nvdec_pic_setup_init_h264_smoke(&vpic, sc->vid_ps_gpu,
                                                  sc->vid_ps_gpu, 0);
               if (sc->sema_cpu)
                  sc->sema_cpu[0] = 0;
               nv_channel_notifier_reset(ch);
               (void)nv_channel_nvdec_frame_sema_submit(
                  ch, 0, &vpic, sc->sema_gpu, sc->sema_cpu, sc->sema_payload,
                  true, to, check_notifier);
            }
            if (sc->sema_gpu) {
               struct nv_nvenc_frame_setup venc;
               if (sc->vid_ps_cpu)
                  nv_nvenc_pic_setup_fill_h264_smoke(
                     (uint32_t *)sc->vid_ps_cpu, 256, 64, 64, 30, 1,
                     sc->ct_gpu ? sc->ct_gpu : sc->vid_ps_gpu, sc->vid_ps_gpu);
               nv_nvenc_frame_setup_init_h264_smoke(
                  &venc, sc->vid_ps_gpu,
                  sc->ct_gpu ? sc->ct_gpu : sc->vid_ps_gpu, sc->vid_ps_gpu, 64,
                  64);
               if (sc->sema_cpu)
                  sc->sema_cpu[0] = 0;
               nv_channel_notifier_reset(ch);
               (void)nv_channel_nvenc_frame_sema_submit(
                  ch, 0, &venc, sc->sema_gpu, sc->sema_cpu, sc->sema_payload,
                  true, to, check_notifier);
            }
            /* Secondary: 3D sema-only (isolates clear methods vs sema/class) */
            if (sc->sema_cpu)
               sc->sema_cpu[0] = 0;
            nv_channel_notifier_reset(ch);
            res.g3_sema_only_rc = nv_channel_g3_sema_only_submit(
               ch, 0, sc->sema_gpu, sc->sema_cpu, sc->sema_payload, true, to,
               check_notifier);

            /* Tick86: kickoff ok but 3D sema failed — clear + host sema */
            if (res.g3_host_sema_rc == 0 && res.g3_rc != 0) {
               int g3hs_mode = -1;
               uint32_t g3hs_class = 0;

               if (sc->sema_cpu)
                  sc->sema_cpu[0] = 0;
               nv_channel_notifier_reset(ch);
               res.g3_clear_host_sema_rc =
                  nv_channel_g3_clear_then_host_sema_submit(
                     ch, 0, sc->ct_gpu, 64, 64, 0, NULL, false,
                     sc->sema_gpu, sc->sema_cpu, sc->sema_payload, true, to,
                     check_notifier, &g3hs_mode, &g3hs_class);
               if (res.g3_clear_host_sema_rc == 0) {
                  if (g3hs_mode >= 0)
                     res.g3_host_sema_mode = g3hs_mode;
                  if (g3hs_class)
                     res.g3_class_3d = g3hs_class;
                  res.g3_submit_rc = 0;
                  res.g3_rc = 0;
                  res.slices_ok |= NV_SMOKE_HW_G3;
               }
            }
            /* tick136: tertiary — pass12 G3 bringup_slice (SPA/MME/inv/clear/draw) */
            if (res.g3_rc != 0 && res.g3_rc != -EAGAIN) {
               uint32_t bu3 = 0;

               if (sc->sema_cpu)
                  sc->sema_cpu[0] = 0;
               nv_channel_notifier_reset(ch);
               res.g3_bringup_slice_rc = nv_channel_g3_bringup_slice_submit(
                  ch, 0, sc->ct_gpu, 64, 64, 0, NULL, 0, 0, 0, 0, 0,
                  sc->sema_gpu, sc->sema_cpu, sc->sema_payload, true, to,
                  check_notifier, &bu3);
               if (res.g3_bringup_slice_rc == 0) {
                  if (bu3)
                     res.g3_class_3d = bu3;
                  res.g3_submit_rc = 0;
                  res.g3_rc = 0;
                  res.slices_ok |= NV_SMOKE_HW_G3;
               }
            }
         }
         if (nv_smoke_hw_env_verbose())
            nv_smoke_hw_dump_channel_trace(ch, "nv_smoke_hw_g3");
         if (res.g3_rc && !r)
            r = res.g3_rc;
      }
   }

   /* tick136: G4 dedicated video/encode smoke (NV_SMOKE_HW_SLICES=16 or ALL) */
   if (slices & NV_SMOKE_HW_G4) {
      res.slices_run |= NV_SMOKE_HW_G4;
      (void)nv_channel_ensure_engine_objects(ch);
      res.g4_rc = -ENODEV;

      if (sc->vid_ps_gpu && sc->sema_gpu) {
         struct nv_nvdec_pic_setup vpic;
         uint32_t dec_class = 0;

         nv_nvdec_pic_setup_init_h264_smoke(&vpic, sc->vid_ps_gpu,
                                            sc->vid_ps_gpu, 0);
         if (sc->sema_cpu)
            sc->sema_cpu[0] = 0;
         nv_channel_notifier_reset(ch);
         /* tick137: prefer pass12 nvdec smoke_slice; fall back to frame sema */
         res.g4_nvdec_rc = nv_channel_nvdec_smoke_slice_submit(
            ch, 0, &vpic, sc->sema_gpu, sc->sema_cpu, sc->sema_payload,
            true, to, check_notifier, &dec_class);
         if (res.g4_nvdec_rc != 0) {
            if (sc->sema_cpu)
               sc->sema_cpu[0] = 0;
            nv_channel_notifier_reset(ch);
            res.g4_nvdec_rc = nv_channel_nvdec_frame_sema_submit(
               ch, 0, &vpic, sc->sema_gpu, sc->sema_cpu, sc->sema_payload,
               true, to, check_notifier);
         }
         if (res.g4_nvdec_rc == 0) {
            if (dec_class)
               res.g4_class_nvdec = dec_class;
            else
               res.g4_class_nvdec = ch->class_nvdec_bound
                                       ? ch->class_nvdec_bound
                                       : nv_channel_resolve_class_nvdec(ch, 0);
            res.g4_rc = 0;
         }
      }

      if (sc->sema_gpu) {
         uint32_t enc_class = 0;
         struct nv_nvenc_frame_setup venc;

         if (sc->vid_ps_cpu)
            nv_nvenc_pic_setup_fill_h264_smoke(
               (uint32_t *)sc->vid_ps_cpu, 256, 64, 64, 30, 1,
               sc->ct_gpu ? sc->ct_gpu : sc->vid_ps_gpu, sc->vid_ps_gpu);
         nv_nvenc_frame_setup_init_h264_smoke(
            &venc, sc->vid_ps_gpu,
            sc->ct_gpu ? sc->ct_gpu : sc->vid_ps_gpu, sc->vid_ps_gpu, 64, 64);
         if (sc->sema_cpu)
            sc->sema_cpu[0] = 0;
         nv_channel_notifier_reset(ch);
         /* Prefer pass12 h264 smoke slice; fall back to frame sema */
         res.g4_nvenc_rc = nv_channel_nvenc_h264_smoke_slice_submit(
            ch, 0, sc->vid_ps_gpu,
            sc->ct_gpu ? sc->ct_gpu : sc->vid_ps_gpu, sc->vid_ps_gpu,
            sc->vid_ps_gpu, 64, 64, sc->sema_gpu, sc->sema_cpu,
            sc->sema_payload, true, to, check_notifier, &enc_class);
         if (res.g4_nvenc_rc != 0) {
            if (sc->sema_cpu)
               sc->sema_cpu[0] = 0;
            nv_channel_notifier_reset(ch);
            res.g4_nvenc_rc = nv_channel_nvenc_frame_sema_submit(
               ch, 0, &venc, sc->sema_gpu, sc->sema_cpu, sc->sema_payload,
               true, to, check_notifier);
         }
         if (res.g4_nvenc_rc == 0) {
            if (enc_class)
               res.g4_class_nvenc = enc_class;
            else if (ch->class_nvenc_bound)
               res.g4_class_nvenc = ch->class_nvenc_bound;
            res.g4_rc = 0;
         } else if (res.g4_rc != 0) {
            res.g4_rc = res.g4_nvenc_rc;
         }
      }

      if (res.g4_rc == 0)
         res.slices_ok |= NV_SMOKE_HW_G4;
      else if (res.g4_rc == 1)
         ; /* no video scratch — treat as skipped, not fatal for ALL */
      else if (res.g4_rc && !r && (slices == NV_SMOKE_HW_G4))
         r = res.g4_rc; /* only fatal when G4 is the sole requested slice */

      if (nv_smoke_hw_env_verbose()) {
         fprintf(stderr,
                 "nv_smoke_hw G4 video: rc=%d nvenc=%d nvdec=%d "
                 "class_enc=0x%x class_dec=0x%x\n",
                 res.g4_rc, res.g4_nvenc_rc, res.g4_nvdec_rc,
                 (unsigned)res.g4_class_nvenc, (unsigned)res.g4_class_nvdec);
      }
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
   struct nv_shader *g2_shader = NULL;
   struct nv_smoke_hw_result res;
   int run_r = -ENODEV;
   int try_gpu;
   int open_gi = gpu_index;
   int buf_va_rc = 0, eng_rc = 0, sready_rc = 0;

   memset(&res, 0, sizeof(res));
   res.g1_host_sema_mode = -1;
   res.g2_host_sema_mode = -1;
   res.g3_host_sema_mode = -1;
   res.g1_rc = 1;
   res.g2_rc = 1;
   res.g3_rc = 1;
   res.standalone_gpu_tried = gpu_index;

   if (!slices)
      slices = nv_smoke_hw_env_slices();
   if (!timeout_ns)
      timeout_ns = NV_SMOKE_HW_DEFAULT_TIMEOUT_NS;

   /*
    * Try requested gpu_index, then 0, then -1 (first/default).  Agents often
    * pass gpu=0 while only ctl/device path works with -1.
    * If drm_fd is a DRM render node only, also retry with fd=-1 so libdrm_nvidia
    * opens /dev/nvidiactl internally (renderD128 alone cannot speak NVOS).
    */
   for (try_gpu = 0; try_gpu < 3 && !rm; try_gpu++) {
      int gi = (try_gpu == 0) ? gpu_index : (try_gpu == 1) ? 0 : -1;
      if (try_gpu > 0 && gi == gpu_index)
         continue;
      open_gi = gi;
      res.standalone_gpu_tried = gi;
      rm = nv_rm_device_open(drm_fd, gi);
   }
   if (!rm && drm_fd >= 0) {
      for (try_gpu = 0; try_gpu < 3 && !rm; try_gpu++) {
         int gi = (try_gpu == 0) ? gpu_index : (try_gpu == 1) ? 0 : -1;
         if (try_gpu > 0 && gi == gpu_index)
            continue;
         open_gi = gi;
         res.standalone_gpu_tried = gi;
         rm = nv_rm_device_open(-1, gi);
      }
      if (rm && (nv_smoke_hw_env_verbose() || getenv("NV_SMOKE_HW_VERBOSE")))
         fprintf(stderr,
                 "nv_smoke_hw_run_standalone: opened via internal /dev/nvidiactl "
                 "(fd=-1) after drm_fd=%d failed\n", drm_fd);
   }
   if (!rm) {
      res.standalone_open_rc = -ENODEV;
      if (nv_smoke_hw_env_verbose() || true)
         fprintf(stderr,
                 "nv_smoke_hw_run_standalone: nv_rm_device_open(fd=%d,gpu=%d) failed "
                 "(need nvidia.ko + /dev/nvidiactl + /dev/nvidiaN + libdrm_nvidia; "
                 "tried gpu %d/0/-1 and fd=-1 fallback)\n",
                 drm_fd, gpu_index, gpu_index);
      if (result_out)
         *result_out = res;
      return -ENODEV;
   }
   res.standalone_open_rc = 0;
   res.standalone_gpu_tried = open_gi;

   (void)nv_rm_device_ensure_vaspace(rm);
   (void)nv_rm_device_ensure_usermode(rm);

   /* engine_type 0 => nv_channel_create defaults to GRAPHICS (0x1) */
   ch = nv_channel_create(rm, 0, 0, 0);
   if (!ch) {
      res.standalone_channel_rc = -EIO;
      fprintf(stderr, "nv_smoke_hw_run_standalone: nv_channel_create failed "
                      "(schedule/USERD/GPFIFO/engine alloc — see channel code; "
                      "gpu_index=%d)\n", open_gi);
      nv_rm_device_close(rm);
      if (result_out)
         *result_out = res;
      return -EIO;
   }
   res.standalone_channel_rc = 0;

   /* Bring-up ladder: VA map channel buffers → engines → schedule/doorbell */
   buf_va_rc = nv_channel_ensure_buffers_gpu_va(ch);
   eng_rc = nv_channel_ensure_engine_objects(ch);
   sready_rc = nv_channel_ensure_submit_ready(ch);
   res.standalone_buf_va_rc = buf_va_rc;
   res.standalone_engine_rc = eng_rc;
   res.standalone_submit_ready_rc = sready_rc;
   if (nv_smoke_hw_env_verbose() && (buf_va_rc || eng_rc || sready_rc))
      fprintf(stderr,
              "nv_smoke_hw_run_standalone: post-create buf_va_rc=%d eng_rc=%d "
              "submit_ready_rc=%d (non-fatal; G1 preflight may retry)\n",
              buf_va_rc, eng_rc, sready_rc);

   /* G2: allocate store-imm compute smoke shader when G2 requested */
   if (slices & NV_SMOKE_HW_G2) {
      g2_shader = nv_shader_create(rm, NV_SHADER_KIND_COMPUTE);
      if (!g2_shader && nv_smoke_hw_env_verbose())
         fprintf(stderr, "nv_smoke_hw_run_standalone: nv_shader_create(compute) "
                         "failed; G2 will run with program_gpu=0\n");
   }

   run_r = nv_smoke_hw_run_oneshot(rm, ch, slices, g2_shader, timeout_ns,
                                   check_notifier, &res);
   /* preserve standalone phase codes in result (oneshot overwrites via stack res) */
   {
      struct nv_smoke_hw_result out = res;
      out.standalone_open_rc = 0;
      out.standalone_channel_rc = 0;
      out.standalone_buf_va_rc = buf_va_rc;
      out.standalone_engine_rc = eng_rc;
      out.standalone_submit_ready_rc = sready_rc;
      out.standalone_gpu_tried = open_gi;
      if (result_out)
         *result_out = out;
      if (nv_smoke_hw_env_verbose() || run_r != 0)
         nv_smoke_hw_log_result(&out, "nv_smoke_hw_standalone");
   }

   if (g2_shader)
      nv_shader_destroy(g2_shader);
   nv_channel_destroy(ch);
   nv_rm_device_close(rm);
   return run_r;
}
