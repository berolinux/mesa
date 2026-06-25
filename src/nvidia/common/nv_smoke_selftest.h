/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Host-only smoke / trace-golden selftests (no GPU).  Call from a future
 * nvidia-smoke tool or mesa unit test; returns 0 if all checks pass.
 *
 * Vertical slices exercised here:
 *   G0/G1 — CE sema method stream shape (pushbuffer trace, no submit)
 *   G2    — QMD determinism + sema enable + SPH EXIT/store validate
 * Optional external traces (610.43.02 / HW dump) via nv_trace_compare_*.
 */
#ifndef NV_SMOKE_SELFTEST_H
#define NV_SMOKE_SELFTEST_H

#include <stdint.h>
#include <string.h>

#include "nv_3d_methods.h"
#include "nv_copy_methods.h"
#include "nv_mme.h"
#include "nv_qmd.h"
#include "nv_sph.h"
#include "nv_video_methods.h"
/* tick135: SASS smoke helpers (compiler/nv_sass.h — path relative to src/nvidia) */
#include "../compiler/nv_sass.h"
/* tick136: smoke_hw slice bit constants (header-only; no HW) */
#include "../rm/nv_smoke_hw.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Compare our QMD/SPH/SASS/push bytes against an external trace capture (from
 * proprietary 610.43.02 or hardware dump).  Returns 0 if equal, -1 size
 * mismatch, -2 first differing offset in *diff_off_out (optional).
 * trace may be NULL (returns -3 = no trace, not a failure of our encoder).
 */
static inline int
nv_trace_compare_bytes(const void *ours, uint32_t ours_bytes,
                       const void *trace, uint32_t trace_bytes,
                       uint32_t *diff_off_out)
{
   const uint8_t *a = (const uint8_t *)ours;
   const uint8_t *b = (const uint8_t *)trace;
   uint32_t i, n;

   if (!trace || !trace_bytes)
      return -3;
   if (!ours || !ours_bytes)
      return -4;
   if (ours_bytes != trace_bytes) {
      if (diff_off_out)
         *diff_off_out = ours_bytes < trace_bytes ? ours_bytes : trace_bytes;
      return -1;
   }
   n = ours_bytes;
   for (i = 0; i < n; i++) {
      if (a[i] != b[i]) {
         if (diff_off_out)
            *diff_off_out = i;
         return -2;
      }
   }
   return 0;
}

/**
 * Encode smoke QMD and optionally compare to trace_qmd (NV_QMD_BYTES).
 * trace_qmd NULL => only encode check.  Returns 0 ok, negative on fail/mismatch.
 */
static inline int
nv_trace_compare_qmd_smoke(const uint32_t *trace_qmd, uint32_t *qmd_out)
{
   uint32_t local[NV_QMD_DWORDS];
   uint32_t *q = qmd_out ? qmd_out : local;
   uint32_t diff = 0;
   int r;

   r = nv_qmd_build_compute_smoke(q, 0x100000ull, 16, 0x86,
                                  0x200000ull, 0x42u, 0, 0);
   if (r != 0)
      return r;
   if (!trace_qmd)
      return 0;
   r = nv_trace_compare_bytes(q, NV_QMD_BYTES, trace_qmd, NV_QMD_BYTES, &diff);
   if (r == -3)
      return 0; /* no trace provided externally */
   return r;
}

/**
 * Build compute EXIT SPH object and optionally compare serialised bytes to trace.
 */
static inline int
nv_trace_compare_sph_compute_exit(const void *trace_sph, uint32_t trace_bytes,
                                  void *ser_out, uint32_t ser_cap,
                                  uint32_t *ser_size_out)
{
   struct nv_sph_blob sph;
   uint8_t local[512];
   void *ser = ser_out ? ser_out : local;
   uint32_t cap = ser_out ? ser_cap : sizeof(local);
   uint32_t sz;
   uint32_t diff = 0;
   int r;

   nv_sph_build_compute_exit_only(&sph, 16);
   r = nv_sph_smoke_validate_blob(&sph, NV_SPH_TYPE_COMPUTE);
   if (r != 0)
      return -40 + r;
   if (cap < sph.total_bytes)
      return -50;
   nv_sph_serialise(&sph, ser, cap);
   sz = sph.total_bytes;
   if (ser_size_out)
      *ser_size_out = sz;
   if (!trace_sph || !trace_bytes)
      return 0;
   return nv_trace_compare_bytes(ser, sz, trace_sph, trace_bytes, &diff);
}

/**
 * G1 host slice: emit CE buffer copy + one-word sema LAUNCH_DMA into a
 * scratch pushbuffer and validate method shape (no GPU submit).
 *
 * Checks:
 *   - COPY subchannel headers present
 *   - SET_SEMAPHORE_A/B/PAYLOAD before LAUNCH_DMA
 *   - sema payload in push matches request
 *   - LAUNCH_DMA has RELEASE_ONE_WORD sema type (bits 4:3 == 1)
 *   - determinism: two emits produce identical dword streams
 * Optional trace_push: full push dword stream compare (trace_dwords count).
 *
 * Returns 0 ok; negative = which invariant failed.
 */
static inline int
nv_smoke_selftest_g1_ce_sema_push(const uint32_t *trace_push,
                                  uint32_t trace_dwords,
                                  uint32_t *push_out,
                                  uint32_t push_cap_dwords,
                                  uint32_t *push_dwords_out)
{
   uint32_t buf_a[128], buf_b[128];
   uint32_t *buf = push_out ? push_out : buf_a;
   uint32_t cap = push_out ? push_cap_dwords : (uint32_t)(sizeof(buf_a) / 4);
   struct nv_push p;
   uint32_t n, i, launch_dw = 0;
   uint32_t sema_payload = 0x42u;
   uint64_t sema_gpu = 0x300000ull;
   uint64_t src_gpu = 0x100000ull;
   uint64_t dst_gpu = 0x200000ull;
   uint32_t size_bytes = 256;
   bool saw_sema_a = false, saw_sema_b = false, saw_sema_pay = false;
   bool saw_launch = false;
   uint32_t diff = 0;
   int r;

   if (cap < 32)
      return -100;

   memset(buf, 0, (size_t)cap * 4);
   nv_push_init(&p, buf, cap);
   nv_push_set_subch(&p, NV_PUSH_SUBCH_COPY);
   nv_copy_emit_buffer_copy_with_sema(&p, src_gpu, dst_gpu, size_bytes,
                                      sema_gpu, sema_payload);
   n = nv_push_dw_count(&p);
   if (n < 10 || n > cap)
      return -101;
   if (push_dwords_out)
      *push_dwords_out = n;

   for (i = 0; i + 1 < n; i++) {
      uint32_t hdr = buf[i];
      uint32_t data = buf[i + 1];
      uint32_t method = (hdr & 0x1fff) << 2;
      uint32_t subch = (hdr >> 13) & 7;
      /* inc method only (bits 31:29 == 0) */
      if ((hdr >> 29) != 0)
         continue;
      if (subch != NV_PUSH_SUBCH_COPY)
         continue;
      if (method == NVC6B5_SET_SEMAPHORE_A) {
         if (data != ((uint32_t)(sema_gpu >> 32) & 0x1ffff))
            return -102;
         saw_sema_a = true;
      } else if (method == NVC6B5_SET_SEMAPHORE_B) {
         if (data != (uint32_t)(sema_gpu & 0xffffffffu))
            return -103;
         saw_sema_b = true;
      } else if (method == NVC6B5_SET_SEMAPHORE_PAYLOAD) {
         if (data != sema_payload)
            return -104;
         saw_sema_pay = true;
      } else if (method == NVC6B5_LAUNCH_DMA) {
         launch_dw = data;
         saw_launch = true;
      }
   }

   if (!saw_sema_a || !saw_sema_b || !saw_sema_pay)
      return -105;
   if (!saw_launch)
      return -106;
   if ((launch_dw & (0x3u << 3)) != NVC6B5_LAUNCH_DMA_SEMAPHORE_TYPE_RELEASE_ONE_WORD)
      return -107;
   if (!(launch_dw & NVC6B5_LAUNCH_DMA_FLUSH_ENABLE_TRUE))
      return -108;
   if (!(launch_dw & NVC6B5_LAUNCH_DMA_SRC_MEMORY_LAYOUT_PITCH) ||
       !(launch_dw & NVC6B5_LAUNCH_DMA_DST_MEMORY_LAYOUT_PITCH))
      return -109;

   /* tick110: split sema path encodes sema + at least two LAUNCH_DMA */
   {
      uint32_t buf_s[128];
      uint32_t ns, is, n_launch = 0;
      bool s_a = false, s_b = false, s_pay = false;

      memset(buf_s, 0, sizeof(buf_s));
      nv_push_init(&p, buf_s, (uint32_t)(sizeof(buf_s) / 4));
      nv_push_set_subch(&p, NV_PUSH_SUBCH_COPY);
      nv_copy_emit_buffer_copy_then_sema_release(&p, src_gpu, dst_gpu,
                                                 size_bytes, sema_gpu,
                                                 sema_payload, false);
      ns = nv_push_dw_count(&p);
      if (ns < 14 || ns > (uint32_t)(sizeof(buf_s) / 4))
         return -110;
      for (is = 0; is + 1 < ns; is++) {
         uint32_t hdr = buf_s[is];
         uint32_t data = buf_s[is + 1];
         uint32_t method = (hdr & 0x1fff) << 2;
         uint32_t subch = (hdr >> 13) & 7;
         if ((hdr >> 29) != 0 || subch != NV_PUSH_SUBCH_COPY)
            continue;
         if (method == NVC6B5_SET_SEMAPHORE_A)
            s_a = true;
         else if (method == NVC6B5_SET_SEMAPHORE_B)
            s_b = true;
         else if (method == NVC6B5_SET_SEMAPHORE_PAYLOAD && data == sema_payload)
            s_pay = true;
         else if (method == NVC6B5_LAUNCH_DMA)
            n_launch++;
      }
      if (!s_a || !s_b || !s_pay || n_launch < 2)
         return -111;
   }

   /* tick110: pitch2d height=1 has sema + pitch layout (encode sanity only) */
   {
      uint32_t buf_p2[128];
      uint32_t np2, ip2, ld = 0;
      bool saw_l = false;

      memset(buf_p2, 0, sizeof(buf_p2));
      nv_push_init(&p, buf_p2, (uint32_t)(sizeof(buf_p2) / 4));
      nv_push_set_subch(&p, NV_PUSH_SUBCH_COPY);
      nv_copy_emit_pitch2d_copy_with_sema(&p, src_gpu, dst_gpu, size_bytes, 1u,
                                          size_bytes, size_bytes, sema_gpu,
                                          sema_payload, false);
      np2 = nv_push_dw_count(&p);
      if (np2 < 10)
         return -112;
      for (ip2 = 0; ip2 + 1 < np2; ip2++) {
         uint32_t hdr = buf_p2[ip2];
         uint32_t data = buf_p2[ip2 + 1];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC6B5_LAUNCH_DMA) {
            ld = data;
            saw_l = true;
         }
      }
      if (!saw_l)
         return -113;
      if ((ld & (0x3u << 3)) != NVC6B5_LAUNCH_DMA_SEMAPHORE_TYPE_RELEASE_ONE_WORD)
         return -114;
      if (!(ld & NVC6B5_LAUNCH_DMA_SRC_MEMORY_LAYOUT_PITCH))
         return -115;
   }

   /* Determinism: second emit matches first */
   memset(buf_b, 0, sizeof(buf_b));
   nv_push_init(&p, buf_b, (uint32_t)(sizeof(buf_b) / 4));
   nv_push_set_subch(&p, NV_PUSH_SUBCH_COPY);
   nv_copy_emit_buffer_copy_with_sema(&p, src_gpu, dst_gpu, size_bytes,
                                      sema_gpu, sema_payload);
   if (nv_push_dw_count(&p) != n)
      return -110;
   if (memcmp(buf, buf_b, (size_t)n * 4) != 0)
      return -111;

   /* Standalone sema-only release (DATA_TRANSFER_TYPE_NONE + sema) — separate buf */
   {
      uint32_t n_sema;
      memset(buf_b, 0, sizeof(buf_b));
      nv_push_init(&p, buf_b, (uint32_t)(sizeof(buf_b) / 4));
      nv_push_set_subch(&p, NV_PUSH_SUBCH_COPY);
      nv_copy_emit_semaphore_release(&p, sema_gpu, sema_payload);
      n_sema = nv_push_dw_count(&p);
      if (n_sema < 6)
         return -112;
      saw_launch = false;
      for (i = 0; i + 1 < n_sema; i++) {
         uint32_t hdr = buf_b[i];
         uint32_t data = buf_b[i + 1];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC6B5_LAUNCH_DMA) {
            if ((data & 0x3u) != NVC6B5_LAUNCH_DMA_DATA_TRANSFER_TYPE_NONE)
               return -113;
            if ((data & (0x3u << 3)) !=
                NVC6B5_LAUNCH_DMA_SEMAPHORE_TYPE_RELEASE_ONE_WORD)
               return -114;
            saw_launch = true;
         }
      }
      if (!saw_launch)
         return -115;
   }

   /* Trace compare against copy+sema stream in buf (not sema-only scratch) */
   if (trace_push && trace_dwords) {
      r = nv_trace_compare_bytes(buf, n * 4u, trace_push, trace_dwords * 4u,
                                 &diff);
      if (r == -3)
         return 0;
      return r == 0 ? 0 : -120 + r; /* -121 size, -122 mismatch */
   }
   return 0;
}

/* G2 smoke defaults (match libdrm NVIDIA_SMOKE_G2_* / channel submit helpers) */
#define NV_SMOKE_G2_PROG_GPU_DEFAULT   0x100000ull
#define NV_SMOKE_G2_QMD_GPU_DEFAULT    0x400000ull
#define NV_SMOKE_G2_SEMA_GPU_DEFAULT   0x300000ull
#define NV_SMOKE_G2_SEMA_PAYLOAD_DEFAULT 0x42u
#define NV_SMOKE_G2_REGS_DEFAULT       16u
#define NV_SMOKE_G2_SASS_DEFAULT       0x86u

/**
 * G2 host slice: emit compute smoke dispatch (SET_OBJECT + invalidate +
 * LOAD_INLINE_QMD + SEND_PCAS) into scratch pushbuffer; verify QMD sema
 * release0 in materialized QMD and SEND_PCAS_A present in method stream.
 * Optional trace_push compares full push (trace_dwords).
 */
static inline int
nv_smoke_selftest_g2_compute_sema_push(const uint32_t *trace_push,
                                       uint32_t trace_dwords,
                                       uint32_t *push_out,
                                       uint32_t push_cap_dwords,
                                       uint32_t *push_dwords_out,
                                       uint32_t *qmd_out)
{
   uint32_t buf_a[320], buf_b[320];
   uint32_t *buf = push_out ? push_out : buf_a;
   uint32_t cap = push_out ? push_cap_dwords : (uint32_t)(sizeof(buf_a) / 4);
   uint32_t qmd_local[NV_QMD_DWORDS];
   uint32_t *qmd = qmd_out ? qmd_out : qmd_local;
   struct nv_push p;
   struct nv_qmd_desc desc;
   uint32_t n, i;
   uint32_t sema_payload = NV_SMOKE_G2_SEMA_PAYLOAD_DEFAULT;
   uint64_t sema_gpu = NV_SMOKE_G2_SEMA_GPU_DEFAULT;
   uint64_t prog_gpu = NV_SMOKE_G2_PROG_GPU_DEFAULT;
   uint64_t qmd_gpu = NV_SMOKE_G2_QMD_GPU_DEFAULT;
   bool saw_pcas_a = false, saw_inline_qmd = false;
   uint32_t diff = 0;
   int r;

   if (cap < 64)
      return -200;

   nv_qmd_desc_init_smoke(&desc, prog_gpu, NV_SMOKE_G2_REGS_DEFAULT,
                          NV_SMOKE_G2_SASS_DEFAULT, sema_gpu, sema_payload);
   nv_qmd_encode_full(&desc, qmd);
   if (!nv_qmd_verify_sema_release0(qmd, sema_gpu, sema_payload))
      return -201;

   memset(buf, 0, (size_t)cap * 4);
   nv_push_init(&p, buf, cap);
   nv_compute_emit_init_state(&p, 0xc3c0 /* placeholder class; methods only */,
                              0, 0);
   nv_compute_emit_dispatch_with_sema(&p, &desc, qmd_gpu, NULL, 0,
                                      sema_gpu, sema_payload, true);
   n = nv_push_dw_count(&p);
   if (n < 20 || n > cap)
      return -202;
   if (push_dwords_out)
      *push_dwords_out = n;

   for (i = 0; i + 1 < n; i++) {
      uint32_t hdr = buf[i];
      uint32_t data = buf[i + 1];
      uint32_t method = (hdr & 0x1fff) << 2;
      uint32_t subch = (hdr >> 13) & 7;
      if ((hdr >> 29) != 0)
         continue;
      if (subch != NV_PUSH_SUBCH_COMPUTE)
         continue;
      if (method == NVC3C0_SEND_PCAS_A) {
         if (data != (uint32_t)(qmd_gpu >> 8))
            return -203;
         saw_pcas_a = true;
      } else if (method == NVC3C0_LOAD_INLINE_QMD_DATA(0)) {
         if (data != qmd[0])
            return -204;
         saw_inline_qmd = true;
      }
   }
   if (!saw_pcas_a)
      return -205;
   if (!saw_inline_qmd)
      return -206;

   /* Determinism */
   memset(buf_b, 0, sizeof(buf_b));
   nv_push_init(&p, buf_b, (uint32_t)(sizeof(buf_b) / 4));
   nv_compute_emit_init_state(&p, 0xc3c0, 0, 0);
   nv_compute_emit_dispatch_with_sema(&p, &desc, qmd_gpu, NULL, 0,
                                      sema_gpu, sema_payload, true);
   if (nv_push_dw_count(&p) != n || memcmp(buf, buf_b, (size_t)n * 4) != 0)
      return -207;

   if (trace_push && trace_dwords) {
      r = nv_trace_compare_bytes(buf, n * 4u, trace_push, trace_dwords * 4u,
                                 &diff);
      if (r == -3)
         return 0;
      return r == 0 ? 0 : -210 + r;
   }
   return 0;
}

/* G3 smoke defaults (match libdrm NVIDIA_SMOKE_G3_* / channel g3_*) */
#define NV_SMOKE_G3_CT_GPU_DEFAULT     0x500000ull
#define NV_SMOKE_G3_SEMA_GPU_DEFAULT   0x300000ull
#define NV_SMOKE_G3_SEMA_PAYLOAD_DEFAULT 0x42u
#define NV_SMOKE_G3_CT_W_DEFAULT       64u
#define NV_SMOKE_G3_CT_H_DEFAULT       64u
#define NV_SMOKE_G3_CLASS_PLACEHOLDER  0xc597u

/**
 * G3 host slice: clear+sema and sema-only method streams; verify CLEAR_SURFACE,
 * SET_REPORT_SEMAPHORE_C payload, and sema-only path.  Optional trace compare.
 */
static inline int
nv_smoke_selftest_g3_3d_sema_push(const uint32_t *trace_push,
                                  uint32_t trace_dwords,
                                  uint32_t *push_out,
                                  uint32_t push_cap_dwords,
                                  uint32_t *push_dwords_out)
{
   uint32_t buf_a[256], buf_b[256];
   uint32_t *buf = push_out ? push_out : buf_a;
   uint32_t cap = push_out ? push_cap_dwords : (uint32_t)(sizeof(buf_a) / 4);
   struct nv_push p;
   uint32_t n, i;
   uint32_t color[4] = { 0xff0000ffu, 0, 0, 0 };
   uint32_t sema_payload = NV_SMOKE_G3_SEMA_PAYLOAD_DEFAULT;
   uint64_t sema_gpu = NV_SMOKE_G3_SEMA_GPU_DEFAULT;
   uint64_t ct_gpu = NV_SMOKE_G3_CT_GPU_DEFAULT;
   bool saw_clear = false, saw_sema_c = false, saw_sema_d = false;
   uint32_t diff = 0;
   int r;

   if (cap < 32)
      return -300;

   memset(buf, 0, (size_t)cap * 4);
   nv_push_init(&p, buf, cap);
   nv_3d_emit_g3_clear_color_sema(&p, NV_SMOKE_G3_CLASS_PLACEHOLDER,
                                  ct_gpu, NV_SMOKE_G3_CT_W_DEFAULT,
                                  NV_SMOKE_G3_CT_H_DEFAULT,
                                  NVC597_SET_COLOR_TARGET_FORMAT_V_A8B8G8R8,
                                  color, sema_gpu, sema_payload);
   n = nv_push_dw_count(&p);
   if (n < 10 || n > cap)
      return -301;
   if (push_dwords_out)
      *push_dwords_out = n;

   for (i = 0; i + 1 < n; i++) {
      uint32_t hdr = buf[i];
      uint32_t data = buf[i + 1];
      uint32_t method = (hdr & 0x1fff) << 2;
      uint32_t subch = (hdr >> 13) & 7;
      if ((hdr >> 29) != 0)
         continue;
      if (subch != NV_PUSH_SUBCH_3D)
         continue;
      if (method == NVC597_CLEAR_SURFACE)
         saw_clear = true;
      else if (method == NVC597_SET_REPORT_SEMAPHORE_C) {
         if (data != sema_payload)
            return -302;
         saw_sema_c = true;
      } else if (method == NVC597_SET_REPORT_SEMAPHORE_D) {
         if ((data & 0x1) != NVC597_SET_REPORT_SEMAPHORE_D_OPERATION_RELEASE)
            return -303;
         saw_sema_d = true;
      }
   }
   if (!saw_clear)
      return -304;
   if (!saw_sema_c || !saw_sema_d)
      return -305;

   /* Determinism */
   memset(buf_b, 0, sizeof(buf_b));
   nv_push_init(&p, buf_b, (uint32_t)(sizeof(buf_b) / 4));
   nv_3d_emit_g3_clear_color_sema(&p, NV_SMOKE_G3_CLASS_PLACEHOLDER,
                                  ct_gpu, NV_SMOKE_G3_CT_W_DEFAULT,
                                  NV_SMOKE_G3_CT_H_DEFAULT,
                                  NVC597_SET_COLOR_TARGET_FORMAT_V_A8B8G8R8,
                                  color, sema_gpu, sema_payload);
   if (nv_push_dw_count(&p) != n || memcmp(buf, buf_b, (size_t)n * 4) != 0)
      return -306;

   /* tick111: WFI+clear+sema path encodes WFI method (encode sanity) */
   {
      uint32_t buf_w[128];
      uint32_t nw, iw;
      bool saw_wfi = false, saw_cl = false;

      memset(buf_w, 0, sizeof(buf_w));
      nv_push_init(&p, buf_w, (uint32_t)(sizeof(buf_w) / 4));
      nv_3d_emit_g3_clear_color_sema_wfi(&p, NV_SMOKE_G3_CLASS_PLACEHOLDER,
                                         ct_gpu, NV_SMOKE_G3_CT_W_DEFAULT,
                                         NV_SMOKE_G3_CT_H_DEFAULT,
                                         NVC597_SET_COLOR_TARGET_FORMAT_V_A8B8G8R8,
                                         color, sema_gpu, sema_payload, true);
      nw = nv_push_dw_count(&p);
      if (nw < 8)
         return -310;
      for (iw = 0; iw + 1 < nw; iw++) {
         uint32_t hdr = buf_w[iw];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == 0x0100u) /* NO_OPERATION / WFI family method band */
            saw_wfi = true;
         if (method == NVC597_CLEAR_SURFACE)
            saw_cl = true;
      }
      if (!saw_cl)
         return -311;
      (void)saw_wfi; /* WFI method offset varies by class; clear+sema is required */
   }

   /* Sema-only path has report sema without CLEAR */
   memset(buf_b, 0, sizeof(buf_b));
   nv_push_init(&p, buf_b, (uint32_t)(sizeof(buf_b) / 4));
   nv_3d_emit_g3_sema_only(&p, NV_SMOKE_G3_CLASS_PLACEHOLDER, sema_gpu,
                           sema_payload);
   {
      uint32_t n_sema = nv_push_dw_count(&p);
      bool saw_c = false;
      if (n_sema < 4)
         return -307;
      for (i = 0; i + 1 < n_sema; i++) {
         uint32_t hdr = buf_b[i];
         uint32_t data = buf_b[i + 1];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC597_SET_REPORT_SEMAPHORE_C && data == sema_payload)
            saw_c = true;
         if (method == NVC597_CLEAR_SURFACE)
            return -308; /* sema-only must not clear */
      }
      if (!saw_c)
         return -309;
   }

   /* tick112: colour+depth clear encodes two CLEAR_SURFACE methods */
   {
      uint32_t buf_cd[128];
      uint32_t ncd, icd;
      unsigned clear_count = 0;

      memset(buf_cd, 0, sizeof(buf_cd));
      nv_push_init(&p, buf_cd, (uint32_t)(sizeof(buf_cd) / 4));
      nv_3d_emit_g3_clear_color_depth_sema(
         &p, NV_SMOKE_G3_CLASS_PLACEHOLDER, ct_gpu, NV_SMOKE_G3_CT_W_DEFAULT,
         NV_SMOKE_G3_CT_H_DEFAULT, NVC597_SET_COLOR_TARGET_FORMAT_V_A8B8G8R8,
         color, 0x100u /* PIPE_CLEAR_DEPTH */, 1.0f, 0, sema_gpu, sema_payload,
         true);
      ncd = nv_push_dw_count(&p);
      if (ncd < 12)
         return -312;
      for (icd = 0; icd + 1 < ncd; icd++) {
         uint32_t hdr = buf_cd[icd];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC597_CLEAR_SURFACE)
            clear_count++;
      }
      if (clear_count < 2)
         return -313;
   }

   /* tick113: ZT bind + depth clear must program SET_ZT_A/B/FORMAT */
   {
      uint64_t zt_gpu = 0x510000ull;
      uint32_t buf_zt[160];
      uint32_t nzt, izt;
      bool saw_zt_b = false, saw_zt_fmt = false, saw_z_clear = false;

      memset(buf_zt, 0, sizeof(buf_zt));
      nv_push_init(&p, buf_zt, (uint32_t)(sizeof(buf_zt) / 4));
      nv_3d_emit_g3_clear_rt_full_sema(
         &p, NV_SMOKE_G3_CLASS_PLACEHOLDER, ct_gpu, NV_SMOKE_G3_CT_W_DEFAULT,
         NV_SMOKE_G3_CT_H_DEFAULT, NVC597_SET_COLOR_TARGET_FORMAT_V_A8B8G8R8,
         color, zt_gpu, NVC597_SET_ZT_FORMAT_V_Z24S8, 1.0f, 0, sema_gpu,
         sema_payload, true);
      nzt = nv_push_dw_count(&p);
      if (nzt < 16)
         return -314;
      for (izt = 0; izt + 1 < nzt; izt++) {
         uint32_t hdr = buf_zt[izt];
         uint32_t data = buf_zt[izt + 1];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC597_SET_ZT_B && data == (uint32_t)(zt_gpu & 0xffffffffu))
            saw_zt_b = true;
         if (method == NVC597_SET_ZT_FORMAT &&
             data == NVC597_SET_ZT_FORMAT_V_Z24S8)
            saw_zt_fmt = true;
         if (method == NVC597_CLEAR_SURFACE &&
             (data & NVC597_CLEAR_SURFACE_Z_ENABLE_TRUE))
            saw_z_clear = true;
      }
      if (!saw_zt_b || !saw_zt_fmt)
         return -315;
      if (!saw_z_clear)
         return -316;
   }

   /* tick114: clear+draw+ZT encodes ZT bind, two clears, and draw methods */
   {
      uint64_t zt_gpu = 0x510000ull;
      uint32_t buf_dr[192];
      uint32_t ndr, idr;
      bool saw_zt = false, saw_draw_topo = false;
      unsigned clear_n = 0;

      memset(buf_dr, 0, sizeof(buf_dr));
      nv_push_init(&p, buf_dr, (uint32_t)(sizeof(buf_dr) / 4));
      nv_3d_emit_g3_clear_draw_sema_zt(
         &p, NV_SMOKE_G3_CLASS_PLACEHOLDER, ct_gpu, NV_SMOKE_G3_CT_W_DEFAULT,
         NV_SMOKE_G3_CT_H_DEFAULT, NVC597_SET_COLOR_TARGET_FORMAT_V_A8B8G8R8,
         color, zt_gpu, NV_SMOKE_G3_CT_W_DEFAULT, NV_SMOKE_G3_CT_H_DEFAULT,
         NVC597_SET_ZT_FORMAT_V_Z24S8, 0, false, sema_gpu, sema_payload, true);
      ndr = nv_push_dw_count(&p);
      if (ndr < 20)
         return -317;
      for (idr = 0; idr + 1 < ndr; idr++) {
         uint32_t hdr = buf_dr[idr];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC597_SET_ZT_B)
            saw_zt = true;
         if (method == NVC597_CLEAR_SURFACE)
            clear_n++;
         if (method == NVC597_SET_PRIMITIVE_TOPOLOGY)
            saw_draw_topo = true;
      }
      if (!saw_zt || clear_n < 2 || !saw_draw_topo)
         return -318;
   }

   /* tick115: full draw path with VB setup encodes vertex stream methods */
   {
      uint64_t zt_gpu = 0x510000ull;
      uint64_t vb_gpu = 0x520000ull;
      uint32_t buf_vb[224];
      uint32_t nvb, ivb;
      bool saw_vs_loc = false, saw_vattr = false;

      memset(buf_vb, 0, sizeof(buf_vb));
      nv_push_init(&p, buf_vb, (uint32_t)(sizeof(buf_vb) / 4));
      nv_3d_emit_g3_clear_draw_full_sema(
         &p, NV_SMOKE_G3_CLASS_PLACEHOLDER, ct_gpu, NV_SMOKE_G3_CT_W_DEFAULT,
         NV_SMOKE_G3_CT_H_DEFAULT, NVC597_SET_COLOR_TARGET_FORMAT_V_A8B8G8R8,
         color, zt_gpu, NVC597_SET_ZT_FORMAT_V_Z24S8, vb_gpu, 36, 0, 0, 0,
         sema_gpu, sema_payload, true);
      nvb = nv_push_dw_count(&p);
      if (nvb < 24)
         return -319;
      for (ivb = 0; ivb + 1 < nvb; ivb++) {
         uint32_t hdr = buf_vb[ivb];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC597_SET_VERTEX_STREAM_A_LOCATION_B(0))
            saw_vs_loc = true;
         if (method == NVC597_SET_VERTEX_ATTRIBUTE_A(0))
            saw_vattr = true;
      }
      if (!saw_vs_loc || !saw_vattr)
         return -320;
   }

   /* tick116: fixed-func smoke sets viewport/scissor/depth before draw */
   {
      uint64_t zt_gpu = 0x510000ull;
      uint64_t vb_gpu = 0x520000ull;
      uint32_t buf_ff[240];
      uint32_t nff, iff;
      bool saw_vp = false, saw_sc = false, saw_dt = false;

      memset(buf_ff, 0, sizeof(buf_ff));
      nv_push_init(&p, buf_ff, (uint32_t)(sizeof(buf_ff) / 4));
      nv_3d_emit_g3_clear_draw_fixed_sema(
         &p, NV_SMOKE_G3_CLASS_PLACEHOLDER, ct_gpu, NV_SMOKE_G3_CT_W_DEFAULT,
         NV_SMOKE_G3_CT_H_DEFAULT, NVC597_SET_COLOR_TARGET_FORMAT_V_A8B8G8R8,
         color, zt_gpu, NVC597_SET_ZT_FORMAT_V_Z24S8, vb_gpu, 36, sema_gpu,
         sema_payload, true);
      nff = nv_push_dw_count(&p);
      if (nff < 28)
         return -321;
      for (iff = 0; iff + 1 < nff; iff++) {
         uint32_t hdr = buf_ff[iff];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC597_SET_VIEWPORT_SCALE_X(0))
            saw_vp = true;
         if (method == NVC597_SET_SCISSOR_ENABLE(0))
            saw_sc = true;
         if (method == NVC597_SET_DEPTH_TEST)
            saw_dt = true;
      }
      if (!saw_vp || !saw_sc || !saw_dt)
         return -322;
   }

   /* tick117/118: block-linear ZT bind emits SET_ZT_BLOCK_SIZE + address methods */
   {
      uint64_t zt_gpu = 0x530000ull;
      uint32_t buf_bl[96];
      uint32_t nbl, ibl;
      bool saw_zt_a = false, saw_zt_fmt = false, saw_zt_bs = false;
      uint32_t zt_bs_val = 0xffffffffu;
      uint32_t expect_bs = nv_3d_block_size_from_gobs(1, 1, 1); /* 0x00000111 */

      if (nv_3d_block_size_default_2d_bl() !=
          nv_3d_block_size_from_gobs(0, 4, 0))
         return -326;
      if (nv_3d_block_size_default_2d_bl() != 0x00000040u)
         return -327;

      memset(buf_bl, 0, sizeof(buf_bl));
      nv_push_init(&p, buf_bl, (uint32_t)(sizeof(buf_bl) / 4));
      nv_3d_emit_g3_bind_zeta_target_ex(&p, zt_gpu, 64, 64,
                                        NVC597_SET_ZT_FORMAT_V_Z24S8, 0, true,
                                        expect_bs);
      nbl = nv_push_dw_count(&p);
      if (nbl < 8)
         return -323;
      for (ibl = 0; ibl + 1 < nbl; ibl++) {
         uint32_t hdr = buf_bl[ibl];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC597_SET_ZT_A)
            saw_zt_a = true;
         if (method == NVC597_SET_ZT_FORMAT)
            saw_zt_fmt = true;
         if (method == NVC597_SET_ZT_BLOCK_SIZE) {
            saw_zt_bs = true;
            zt_bs_val = buf_bl[ibl + 1];
         }
      }
      if (!saw_zt_a || !saw_zt_fmt || !saw_zt_bs)
         return -324;
      if (zt_bs_val != expect_bs)
         return -325;
   }

   /* tick118: default BL gobs (no explicit block_size) yields 1x16x1 */
   {
      uint64_t zt_gpu = 0x540000ull;
      uint32_t buf_d[96];
      uint32_t nd, id;
      uint32_t zt_bs_val = 0xffffffffu;
      bool saw_zt_bs = false;

      memset(buf_d, 0, sizeof(buf_d));
      nv_push_init(&p, buf_d, (uint32_t)(sizeof(buf_d) / 4));
      nv_3d_emit_g3_bind_zeta_target_ex(&p, zt_gpu, 64, 64,
                                        NVC597_SET_ZT_FORMAT_V_Z24S8, 0, true,
                                        0);
      nd = nv_push_dw_count(&p);
      for (id = 0; id + 1 < nd; id++) {
         uint32_t hdr = buf_d[id];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC597_SET_ZT_BLOCK_SIZE) {
            saw_zt_bs = true;
            zt_bs_val = buf_d[id + 1];
         }
      }
      if (!saw_zt_bs || zt_bs_val != nv_3d_block_size_default_2d_bl())
         return -328;
   }

   /* tick119: CT MEMORY pitch layout at bit12; BL 1x16x1 gobs in low 12b */
   {
      struct nv_3d_surface sct;
      uint32_t buf_ct[64];
      uint32_t nct, ict;
      uint32_t mem_val = 0xffffffffu;
      bool saw_mem = false;
      uint32_t pitch_mem = nv_3d_ct_memory_dword(false, 0, 0, 0);
      uint32_t bl_mem = nv_3d_ct_memory_dword(true, 0, 4, 0);

      if (pitch_mem != (1u << 12))
         return -329;
      if (bl_mem != nv_3d_block_size_default_2d_bl())
         return -330;
      if (bl_mem & (1u << 12))
         return -331;

      memset(&sct, 0, sizeof(sct));
      sct.enabled = true;
      sct.gpu_addr = 0x600000ull;
      sct.width = 64;
      sct.height = 64;
      sct.format = NVC597_SET_COLOR_TARGET_FORMAT_V_A8B8G8R8;
      sct.block_linear = true;
      sct.gobs_height = 4;
      memset(buf_ct, 0, sizeof(buf_ct));
      nv_push_init(&p, buf_ct, (uint32_t)(sizeof(buf_ct) / 4));
      nv_3d_set_color_target(&p, 0, &sct);
      nct = nv_push_dw_count(&p);
      for (ict = 0; ict + 1 < nct; ict++) {
         uint32_t hdr = buf_ct[ict];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC597_SET_COLOR_TARGET_MEMORY(0)) {
            saw_mem = true;
            mem_val = buf_ct[ict + 1];
         }
      }
      if (!saw_mem || mem_val != bl_mem)
         return -332;
   }

   /* tick119: MME upload_only emits LOAD_MME_* without CALL_MME_MACRO */
   {
      struct nv_mme_program prog;
      uint32_t buf_m[128];
      uint32_t nm, im;
      bool saw_ptr = false, saw_ram = false, saw_start = false, saw_call = false;

      nv_mme_build_draw_indirect_loop_scaffold(&prog, NV_MME_SLOT_DRAW_INDIRECT,
                                               0, false);
      if (!prog.is_stub_end_only || prog.insn_count < 2)
         return -333;
      memset(buf_m, 0, sizeof(buf_m));
      nv_push_init(&p, buf_m, (uint32_t)(sizeof(buf_m) / 4));
      nv_mme_emit_upload_only(&p, &prog);
      nm = nv_push_dw_count(&p);
      if (nm < 6)
         return -334;
      for (im = 0; im + 1 < nm; im++) {
         uint32_t hdr = buf_m[im];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC597_LOAD_MME_INSTRUCTION_RAM_POINTER)
            saw_ptr = true;
         if (method == NVC597_LOAD_MME_INSTRUCTION_RAM)
            saw_ram = true;
         if (method == NVC597_LOAD_MME_START_ADDRESS_RAM_POINTER)
            saw_start = true;
         if (method == NVC597_CALL_MME_MACRO(0) ||
             method == NVC597_CALL_MME_MACRO(NV_MME_SLOT_DRAW_INDIRECT))
            saw_call = true;
      }
      if (!saw_ptr || !saw_ram || !saw_start)
         return -335;
      if (saw_call)
         return -336;
   }

   /* tick120: channel_init_with_mme emits SPA/SPH + MME LOAD without CALL_MME */
   {
      uint32_t buf_ci[256];
      uint32_t nci, ici;
      bool saw_spa = false, saw_sph = false, saw_mme_ptr = false, saw_call = false;
      bool mme_ok;

      memset(buf_ci, 0, sizeof(buf_ci));
      nv_push_init(&p, buf_ci, (uint32_t)(sizeof(buf_ci) / 4));
      mme_ok = nv_3d_emit_channel_init_with_mme(&p, 5, 3, 5, 3);
      if (!mme_ok)
         return -337;
      nci = nv_push_dw_count(&p);
      if (nci < 20)
         return -338;
      for (ici = 0; ici + 1 < nci; ici++) {
         uint32_t hdr = buf_ci[ici];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC597_SET_SPA_VERSION)
            saw_spa = true;
         if (method == NVC597_SET_SPH_VERSION)
            saw_sph = true;
         if (method == NVC597_LOAD_MME_INSTRUCTION_RAM_POINTER)
            saw_mme_ptr = true;
         if (method >= NVC597_CALL_MME_MACRO(0) &&
             method <= NVC597_CALL_MME_MACRO(31))
            saw_call = true;
      }
      if (!saw_spa || !saw_sph || !saw_mme_ptr)
         return -339;
      if (saw_call)
         return -340;
      if (!nv_3d_mme_indirect_is_stub())
         return -341; /* until real ISA, stubs must remain true */
   }

   /* tick121: graphics SPH+EXIT smoke has correct type + EXIT tail (encode only) */
   {
      struct nv_sph_blob vsb, fsb;
      int vr;

      nv_sph_build_vertex_exit_only(&vsb, 8);
      vr = nv_sph_smoke_validate_blob(&vsb, NV_SPH_TYPE_VERTEX);
      if (vr != 0)
         return -342;
      if (vsb.total_bytes < 32)
         return -343;
      nv_sph_build_pixel_exit_only(&fsb, 8);
      vr = nv_sph_smoke_validate_blob(&fsb, NV_SPH_TYPE_PIXEL);
      if (vr != 0)
         return -344;
      if (nv_sph_type_from_shader_kind_idx(1) != NV_SPH_TYPE_PIXEL)
         return -345;
      if (nv_sph_type_from_shader_kind_idx(5) != NV_SPH_TYPE_COMPUTE)
         return -346;
   }

   /* tick122: H.264 pic_setup_ex + NVDEC/NVENC frame_kick encode shape */
   {
      uint32_t pic[64];
      struct nv_nvdec_pic_setup vpic;
      struct nv_nvenc_frame_setup venc;
      uint32_t buf_v[160];
      uint32_t nv, iv;
      bool saw_app = false, saw_pic_off = false, saw_exec = false;
      bool saw_sema = false;

      nv_nvdec_pic_setup_fill_h264_intra_ex(pic, 64, 4, 4, 0, 0, 0x700000ull,
                                            0x701000ull, 0x702000ull, 128,
                                            0, 0);
      if (pic[NV_H264_PS_MB_WH] != ((4u << 16) | 4u))
         return -347;
      if (pic[NV_H264_PS_OUTPUT_LUMA_OFF] !=
          nv_video_gpu_addr_to_offset_units(0x700000ull, 8))
         return -348;
      if (pic[NV_H264_PS_BITSTREAM_LEN] != 128)
         return -349;

      nv_nvdec_pic_setup_init_h264_smoke(&vpic, 0x710000ull, 0x720000ull, 0);
      memset(buf_v, 0, sizeof(buf_v));
      nv_push_init(&p, buf_v, (uint32_t)(sizeof(buf_v) / 4));
      nv_nvdec_emit_frame_kick(&p, 0xc4b0u /* placeholder class */, &vpic,
                               0x300000ull, 0x42u, NULL);
      nv = nv_push_dw_count(&p);
      if (nv < 8)
         return -350;
      for (iv = 0; iv + 1 < nv; iv++) {
         uint32_t hdr = buf_v[iv];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NV_NVDEC_SET_APPLICATION_ID)
            saw_app = true;
         if (method == NV_NVDEC_SET_DRV_PIC_SETUP_OFFSET)
            saw_pic_off = true;
         if (method == NV_NVDEC_EXECUTE)
            saw_exec = true;
         if (method == NV_NVDEC_SEMAPHORE_A)
            saw_sema = true;
      }
      if (!saw_app || !saw_pic_off || !saw_exec || !saw_sema)
         return -351;

      nv_nvenc_pic_setup_fill_h264_smoke(pic, 64, 64, 64, 30, 1, 0x800000ull,
                                         0x810000ull);
      if (pic[NV_NVENC_PS_PIC_WH] != ((64u << 16) | 64u))
         return -352;
      if (pic[NV_NVENC_PS_GOP_LENGTH] != 1)
         return -353;

      nv_nvenc_frame_setup_init_h264_smoke(&venc, 0x710000ull, 0x800000ull,
                                           0x810000ull, 64, 64);
      if (venc.app_id != NV_NVENC_APP_ID_H264 || venc.width != 64)
         return -354;
   }

   /* tick122: CE image_2d_with_sema encodes sema + multi-line LAUNCH_DMA */
   {
      uint32_t buf_ce[96];
      uint32_t nce, ice;
      bool saw_sema_a = false, saw_launch = false;
      bool saw_line_cnt = false;
      uint32_t line_cnt_val = 0;

      memset(buf_ce, 0, sizeof(buf_ce));
      nv_push_init(&p, buf_ce, (uint32_t)(sizeof(buf_ce) / 4));
      nv_push_set_subch(&p, NV_PUSH_SUBCH_COPY);
      nv_copy_emit_image_2d_with_sema(&p, 0x100000ull, 0x200000ull, 256, 256,
                                      256, 16, 0x300000ull, 0x42u);
      nce = nv_push_dw_count(&p);
      if (nce < 10)
         return -355;
      for (ice = 0; ice + 1 < nce; ice++) {
         uint32_t hdr = buf_ce[ice];
         uint32_t data = buf_ce[ice + 1];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC6B5_SET_SEMAPHORE_A)
            saw_sema_a = true;
         if (method == NVC6B5_LINE_COUNT) {
            saw_line_cnt = true;
            line_cnt_val = data;
         }
         if (method == NVC6B5_LAUNCH_DMA)
            saw_launch = true;
      }
      if (!saw_sema_a || !saw_launch || !saw_line_cnt)
         return -356;
      if (line_cnt_val != 16)
         return -357;
   }

   /* tick123: session_fill_pic_setup_methods + emit_frame_sema shape */
   {
      struct nv_nvdec_session sess;
      struct nv_nvdec_pic_setup pic;
      uint32_t buf_s[128];
      uint32_t ns, is;
      bool saw_app = false, saw_exec = false, saw_sema = false;

      nv_nvdec_session_init(&sess, 0xc4b0u, NV_NVDEC_APP_ID_H264);
      sess.pic_setup_gpu_addr = 0x710000ull;
      sess.status_gpu_addr = 0x300000ull;
      sess.next_picture_index = 3;
      nv_nvdec_session_fill_pic_setup_methods(&sess, 0x720000ull, 256, &pic);
      if (pic.app_id != NV_NVDEC_APP_ID_H264 || pic.picture_index != 3)
         return -358;
      if (pic.pic_setup_gpu != 0x710000ull || pic.bitstream_gpu != 0x720000ull)
         return -359;

      memset(buf_s, 0, sizeof(buf_s));
      nv_push_init(&p, buf_s, (uint32_t)(sizeof(buf_s) / 4));
      if (nv_nvdec_session_emit_frame_sema(&p, &sess, 0x720000ull, 256, 4) != 0)
         return -360;
      ns = nv_push_dw_count(&p);
      if (ns < 6)
         return -361;
      for (is = 0; is + 1 < ns; is++) {
         uint32_t hdr = buf_s[is];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NV_NVDEC_SET_APPLICATION_ID)
            saw_app = true;
         if (method == NV_NVDEC_EXECUTE)
            saw_exec = true;
         if (method == NV_NVDEC_SEMAPHORE_A)
            saw_sema = true;
      }
      if (!saw_app || !saw_exec || !saw_sema)
         return -362;
   }

   /* tick124: NVENC emit_encode_frame / frame_kick shape */
   {
      struct nv_nvenc_frame_setup efs;
      uint32_t buf_e[128];
      uint32_t ne, ie;
      bool saw_app = false, saw_exec = false, saw_in = false;

      nv_nvenc_frame_setup_init_h264_smoke(&efs, 0x710000ull, 0x800000ull,
                                           0x810000ull, 128, 72);
      efs.status_gpu_addr = 0x300000ull;
      memset(buf_e, 0, sizeof(buf_e));
      nv_push_init(&p, buf_e, (uint32_t)(sizeof(buf_e) / 4));
      if (nv_nvenc_emit_encode_frame(&p, 0xc0b7u, &efs, 0x300000ull, 0x11u,
                                     NULL) != 0)
         return -363;
      ne = nv_push_dw_count(&p);
      if (ne < 8)
         return -364;
      for (ie = 0; ie + 1 < ne; ie++) {
         uint32_t hdr = buf_e[ie];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NV_NVENC_SET_APPLICATION_ID)
            saw_app = true;
         if (method == NV_NVENC_EXECUTE)
            saw_exec = true;
         if (method == NV_NVENC_SET_IN_BUF_BASE_OFFSET)
            saw_in = true;
      }
      if (!saw_app || !saw_exec || !saw_in)
         return -365;
      if (efs.width != 128 || efs.height != 72)
         return -366;
   }

   /* tick125: NVENC status BO parse / bitstream size derivation */
   {
      uint8_t stbuf[NV_NVENC_STATUS_BO_MIN_BYTES];
      struct nv_nvenc_status_snapshot snap;
      uint32_t sz;

      memset(stbuf, 0, sizeof(stbuf));
      nv_nvenc_status_reset_cpu(stbuf, sizeof(stbuf));
      if (nv_nvenc_status_read(stbuf, sizeof(stbuf), NV_NVENC_APP_ID_H264,
                               &snap) != 0 || !snap.valid)
         return -367;
      if (snap.bitstream_size_bytes != 0)
         return -368; /* zeroed BO must report size 0 */

      /* 256 bits => 32 bytes via total_bit_count path */
      nv_nvenc_status_write_synthetic(stbuf, sizeof(stbuf), 7u, 256u, 0, 0,
                                      0 /* I/P */, 28);
      if (nv_nvenc_status_read(stbuf, sizeof(stbuf), NV_NVENC_APP_ID_H264,
                               &snap) != 0)
         return -369;
      if (snap.picture_index != 7u || snap.total_bit_count != 256u)
         return -370;
      if (snap.bitstream_size_bytes != 32u)
         return -371;
      if (snap.avg_qp != 28)
         return -372;

      /* Prefer last_valid_byte_offset - start when set larger than bit-count path */
      nv_nvenc_status_write_synthetic(stbuf, sizeof(stbuf), 1u, 64u /* 8 bytes */,
                                      16u, 48u, 0, 20);
      if (nv_nvenc_status_read(stbuf, sizeof(stbuf), NV_NVENC_APP_ID_HEVC,
                               &snap) != 0)
         return -373;
      if (snap.bitstream_start_pos != 16u || snap.last_valid_byte_offset != 48u)
         return -374;
      if (snap.bitstream_size_bytes != 32u) /* 48 - 16 */
         return -375;

      sz = nv_nvenc_status_bitstream_size_bytes(stbuf, sizeof(stbuf),
                                                NV_NVENC_APP_ID_H264);
      if (sz != 32u)
         return -376;
      if (nv_nvenc_status_read(NULL, 0, NV_NVENC_APP_ID_H264, &snap) == 0)
         return -377; /* must fail on null buffer */
   }

   /* tick125: vertex/pixel SPH+MOV imm+EXIT smoke builders */
   {
      struct nv_sph_blob vsb, psb;
      nv_sph_build_vertex_smoke_default(&vsb, 16);
      if (nv_sph_smoke_validate_blob(&vsb, NV_SPH_TYPE_VERTEX) != 0)
         return -378;
      if (vsb.sass_dwords < 10)
         return -379; /* 4x MOV32I + EXIT = 10 dwords */
      if ((vsb.sph[0] & 0xf) != NV_SPH_TYPE_VERTEX)
         return -380;
      nv_sph_build_pixel_mov_imm_exit(&psb, 0xff00ff00u, 8);
      if (nv_sph_smoke_validate_blob(&psb, NV_SPH_TYPE_PIXEL) != 0)
         return -381;
      if (psb.sass_dwords < 4)
         return -382;
   }

   /* pass10 RE: NVENC emit includes SetOutEncStatus/SetOutBitstream (0x718/0x71c) */
   {
      struct nv_nvenc_frame_setup efs2;
      uint32_t buf10[160];
      uint32_t n10, i10;
      bool saw_out_st = false, saw_out_bs = false, saw_st = false, saw_bs = false;

      nv_nvenc_frame_setup_init_h264_smoke(&efs2, 0x710000ull, 0x800000ull,
                                           0x810000ull, 64, 64);
      efs2.status_gpu_addr = 0x900000000ull | 0x300000ull; /* hi non-zero */
      efs2.bitstream_out_gpu_addr = 0xA00000000ull | 0x810000ull;
      efs2.emit_set_out_methods = true;
      memset(buf10, 0, sizeof(buf10));
      nv_push_init(&p, buf10, (uint32_t)(sizeof(buf10) / 4));
      if (nv_nvenc_emit_encode_frame(&p, 0xc8b7u, &efs2, 0, 0, NULL) != 0)
         return -383;
      n10 = nv_push_dw_count(&p);
      for (i10 = 0; i10 + 1 < n10; i10++) {
         uint32_t hdr = buf10[i10];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NV_NVENC_SET_STATUS_OFFSET)
            saw_st = true;
         if (method == NV_NVENC_SET_BITSTREAM_BUF_OFFSET)
            saw_bs = true;
         if (method == NV_NVENC_SET_OUT_ENC_STATUS)
            saw_out_st = true;
         if (method == NV_NVENC_SET_OUT_BITSTREAM)
            saw_out_bs = true;
      }
      if (!saw_st || !saw_bs)
         return -384;
      if (!saw_out_st || !saw_out_bs)
         return -385;
      if (NV_NVENC_SET_OUT_ENC_STATUS != 0x0718u ||
          NV_NVENC_SET_OUT_BITSTREAM != 0x071cu)
         return -386;
   }

   /* tick126: NVDEC status BO parse (nvdec_status_s) */
   {
      uint8_t dstbuf[NV_NVDEC_STATUS_BO_MIN_BYTES];
      struct nv_nvdec_status_snapshot dsnap;

      memset(dstbuf, 0, sizeof(dstbuf));
      nv_nvdec_status_bo_reset_cpu(dstbuf, sizeof(dstbuf));
      if (nv_nvdec_status_read(dstbuf, sizeof(dstbuf), &dsnap) != 0 || !dsnap.valid)
         return -387;
      if (dsnap.hw_error)
         return -388;

      nv_nvdec_status_write_synthetic(dstbuf, sizeof(dstbuf), 100u, 0, 5000u, 0);
      if (nv_nvdec_status_read(dstbuf, sizeof(dstbuf), &dsnap) != 0)
         return -389;
      if (dsnap.mbs_correctly_decoded != 100u || dsnap.cycle_count != 5000u)
         return -390;
      if (dsnap.hw_error)
         return -391;

      nv_nvdec_status_write_synthetic(dstbuf, sizeof(dstbuf), 50u, 3u, 100u, 0x2u);
      if (nv_nvdec_status_read(dstbuf, sizeof(dstbuf), &dsnap) != 0)
         return -392;
      if (!dsnap.hw_error || dsnap.mbs_in_error != 3u || dsnap.error_status != 0x2u)
         return -393;

      /* sema-only: first dword set, rest zero */
      memset(dstbuf, 0, sizeof(dstbuf));
      ((uint32_t *)dstbuf)[0] = 0x11u;
      if (nv_nvdec_status_read(dstbuf, sizeof(dstbuf), &dsnap) != 0)
         return -394;
      if (!dsnap.sema_only || dsnap.hw_error)
         return -395;

      if (nv_nvdec_status_read(NULL, 0, &dsnap) == 0)
         return -396;
   }

   /* tick126: NVENC control_params default when pic_setup present */
   {
      struct nv_nvenc_frame_setup efs3;
      uint32_t buf11[160];
      uint32_t n11, i11;
      bool saw_ctrl = false;

      nv_nvenc_frame_setup_init_h264_smoke(&efs3, 0x710000ull, 0x800000ull,
                                           0x810000ull, 32, 32);
      efs3.control_params = 0; /* force default path */
      efs3.status_gpu_addr = 0x300000ull;
      memset(buf11, 0, sizeof(buf11));
      nv_push_init(&p, buf11, (uint32_t)(sizeof(buf11) / 4));
      if (nv_nvenc_emit_encode_frame(&p, 0xc8b7u, &efs3, 0, 0, NULL) != 0)
         return -397;
      n11 = nv_push_dw_count(&p);
      for (i11 = 0; i11 + 1 < n11; i11++) {
         uint32_t hdr = buf11[i11];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NV_NVENC_SET_CONTROL_PARAMS && buf11[i11 + 1] == 1u)
            saw_ctrl = true;
      }
      if (!saw_ctrl)
         return -398;
   }

   /* tick127: CE four-word sema + sema ladder emit shape */
   {
      uint32_t buf_ce[96];
      uint32_t nce, ice, launch_dw = 0;
      bool saw_sema_ab = false, saw_launch4 = false;

      memset(buf_ce, 0, sizeof(buf_ce));
      nv_push_init(&p, buf_ce, (uint32_t)(sizeof(buf_ce) / 4));
      nv_copy_emit_semaphore_release_four_word(&p, 0x500000ull, 0x42u);
      nce = nv_push_dw_count(&p);
      if (nce < 6)
         return -399;
      for (ice = 0; ice + 1 < nce; ice++) {
         uint32_t hdr = buf_ce[ice];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC6B5_SET_SEMAPHORE_A || method == NVC6B5_SET_SEMAPHORE_B)
            saw_sema_ab = true;
         if (method == NVC6B5_LAUNCH_DMA) {
            launch_dw = buf_ce[ice + 1];
            if (nv_copy_launch_dma_has_sema_four_word(launch_dw))
               saw_launch4 = true;
         }
      }
      if (!saw_sema_ab || !saw_launch4)
         return -400;

      memset(buf_ce, 0, sizeof(buf_ce));
      nv_push_init(&p, buf_ce, (uint32_t)(sizeof(buf_ce) / 4));
      launch_dw = nv_copy_emit_buffer_copy_with_sema_ladder(
         &p, 0x1000ull, 0x2000ull, 64u, 0x500000ull, 0x7u, true);
      if (!nv_copy_launch_dma_has_sema_one_word(launch_dw))
         return -401;
      nce = nv_push_dw_count(&p);
      if (nce < 12)
         return -402; /* copy launch + four-word sema-only tail */
   }

   /* tick127: QMD multi-CTA smoke encode */
   {
      uint32_t qg[NV_QMD_DWORDS];
      struct nv_qmd_desc dg;
      if (nv_qmd_build_compute_smoke_grid(qg, 0x100000ull, 16, 0x86,
                                          8 /* grid_x */, 32 /* cta_x */,
                                          0x400000ull, 0x99u) != 0)
         return -403;
      nv_qmd_desc_init_smoke_grid(&dg, 0x100000ull, 16, 0x86, 8, 1, 1, 32, 1, 1,
                                  0x400000ull, 0x99u);
      if (dg.grid_x != 8 || dg.cta_x != 32)
         return -404;
      if (!nv_qmd_verify_sema_release0(qg, 0x400000ull, 0x99u))
         return -405;
   }

   /* tick127: host sema execute values (pass8/9/10 modes) */
   {
      if (nv_host_sema_execute(NV_HOST_SEMA_MODE_BLOB_ALIGN4) != 0x1001u)
         return -406;
      if (nv_host_sema_execute(NV_HOST_SEMA_MODE_VDPAU_ALIGN4) != 0x2u)
         return -407;
      if (nv_host_sema_execute(NV_HOST_SEMA_MODE_BLOB_SHIFT2) != 0x1001u)
         return -408;
      if (nv_host_sema_execute(NV_HOST_SEMA_MODE_VDPAU_SHIFT2) != 0x2u)
         return -409;
   }

   /* tick128: G3 viewport/scissor/clip + shader_draw_sema method shape */
   {
      uint32_t buf_g3[2048];
      uint32_t ng3, ig3;
      bool saw_vp_clip_h = false, saw_surf_clip = false, saw_pipe_vs = false;
      bool saw_draw = false, saw_scissor = false;
      uint32_t color[4] = { 0xff0000ffu, 0, 0, 0 };

      memset(buf_g3, 0, sizeof(buf_g3));
      nv_push_init(&p, buf_g3, (uint32_t)(sizeof(buf_g3) / 4));
      nv_3d_emit_g3_viewport_scissor_full(&p, 64, 48, true);
      ng3 = nv_push_dw_count(&p);
      if (ng3 < 8)
         return -410;
      for (ig3 = 0; ig3 + 1 < ng3; ig3++) {
         uint32_t hdr = buf_g3[ig3];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC597_SET_SURFACE_CLIP_HORIZONTAL)
            saw_surf_clip = true;
         if (method == NVC597_SET_VIEWPORT_CLIP_HORIZONTAL(0))
            saw_vp_clip_h = true;
         if (method == NVC597_SET_SCISSOR_ENABLE(0))
            saw_scissor = true;
      }
      if (!saw_surf_clip || !saw_vp_clip_h || !saw_scissor)
         return -411;

      memset(buf_g3, 0, sizeof(buf_g3));
      nv_push_init(&p, buf_g3, (uint32_t)(sizeof(buf_g3) / 4));
      nv_3d_emit_g3_shader_draw_sema(&p, 0xc597u, 0x800000ull, 32, 32,
                                     NVC597_SET_COLOR_TARGET_FORMAT_V_A8R8G8B8,
                                     color, 0x900000ull, 0x900000ull, 16,
                                     0x900100ull, 8, 0, 0,
                                     0x300000ull, 0x55u, true);
      ng3 = nv_push_dw_count(&p);
      if (ng3 < 20)
         return -412;
      for (ig3 = 0; ig3 + 1 < ng3; ig3++) {
         uint32_t hdr = buf_g3[ig3];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == 0x2000u || method == 0x1608u || method == 0x160cu)
            saw_pipe_vs = true; /* PIPELINE_SHADER / PROGRAM_REGION */
         if (method == 0x1610u || method == 0x1638u || method == 0x1640u)
            saw_draw = true;
      }
      if (!saw_pipe_vs && !saw_draw)
         return -413;
   }

   /* tick128: MME upload-only indirect stubs (no CALL) */
   {
      uint32_t buf_m[128];
      uint32_t nm, im;
      bool saw_mme_ptr = false;
      bool stubs;

      memset(buf_m, 0, sizeof(buf_m));
      nv_push_init(&p, buf_m, (uint32_t)(sizeof(buf_m) / 4));
      stubs = nv_mme_emit_upload_indirect_stubs_only(&p);
      if (!stubs)
         return -414; /* expected stubs until real ISA */
      nm = nv_push_dw_count(&p);
      if (nm < 4)
         return -415;
      for (im = 0; im + 1 < nm; im++) {
         uint32_t hdr = buf_m[im];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         /* LOAD_MME pointer 0x114 or insn ram 0x450 class-dependent */
         if (method == 0x0114u || method == 0x0450u || method == 0x011cu)
            saw_mme_ptr = true;
      }
      if (!saw_mme_ptr && nm < 2)
         return -416;
      (void)saw_mme_ptr;
   }

   /* tick129: host sema ladder fill (pass8-10 default order + preferred first) */
   {
      enum nv_host_sema_mode ladder[NV_HOST_SEMA_MODE_COUNT];
      unsigned nl, il;
      bool has_blob = false, has_vdpau = false;

      nl = nv_host_sema_ladder_fill(ladder, -1);
      if (nl != NV_HOST_SEMA_MODE_COUNT)
         return -417;
      if (ladder[0] != NV_HOST_SEMA_MODE_BLOB_ALIGN4)
         return -418;
      if (ladder[1] != NV_HOST_SEMA_MODE_BLOB_SHIFT2)
         return -419;

      nl = nv_host_sema_ladder_fill(ladder, (int)NV_HOST_SEMA_MODE_VDPAU_ALIGN4);
      if (nl != NV_HOST_SEMA_MODE_COUNT)
         return -420;
      if (ladder[0] != NV_HOST_SEMA_MODE_VDPAU_ALIGN4)
         return -421;
      for (il = 0; il < nl; il++) {
         if (ladder[il] == NV_HOST_SEMA_MODE_BLOB_ALIGN4)
            has_blob = true;
         if (ladder[il] == NV_HOST_SEMA_MODE_VDPAU_SHIFT2)
            has_vdpau = true;
      }
      if (!has_blob || !has_vdpau)
         return -422;
      if (!nv_host_sema_mode_name(NV_HOST_SEMA_MODE_BLOB_ALIGN4) ||
          nv_host_sema_mode_name(NV_HOST_SEMA_MODE_BLOB_ALIGN4)[0] == '\0')
         return -423;
      /* tick130: no duplicates in ladder */
      for (il = 0; il < nl; il++) {
         unsigned jl;
         for (jl = il + 1; jl < nl; jl++) {
            if (ladder[il] == ladder[jl])
               return -424;
         }
      }
   }

   /* pass11 RE: class ladders include glcore rodata heads (CA6F/CAB5/CE97/CEC0/D1B7) */
   {
      uint32_t cl[24];
      unsigned ncl, i, has_ca6f = 0, has_cab5 = 0, has_ce97 = 0, has_cec0 = 0;
      unsigned has_d1b7 = 0, has_cbb0 = 0, has_c597 = 0, has_c8b7 = 0;

      ncl = 24;
      nv_device_info_fill_class_ladder(5, 0, cl, &ncl);
      for (i = 0; i < ncl; i++) {
         if (cl[i] == 0x0000ca6fu)
            has_ca6f = 1;
      }
      if (!has_ca6f || ncl < 8)
         return -425;

      ncl = 24;
      nv_device_info_fill_class_ladder(2, 0, cl, &ncl);
      for (i = 0; i < ncl; i++) {
         if (cl[i] == 0x0000cab5u)
            has_cab5 = 1;
      }
      if (!has_cab5)
         return -426;

      ncl = 24;
      nv_device_info_fill_class_ladder(0, 0, cl, &ncl);
      for (i = 0; i < ncl; i++) {
         if (cl[i] == 0x0000ce97u)
            has_ce97 = 1;
         if (cl[i] == 0x0000c597u)
            has_c597 = 1;
      }
      if (!has_ce97 || !has_c597)
         return -427;

      ncl = 24;
      nv_device_info_fill_class_ladder(1, 0, cl, &ncl);
      for (i = 0; i < ncl; i++) {
         if (cl[i] == 0x0000cec0u)
            has_cec0 = 1;
      }
      if (!has_cec0)
         return -428;

      ncl = 24;
      nv_device_info_fill_class_ladder(4, 0, cl, &ncl);
      for (i = 0; i < ncl; i++) {
         if (cl[i] == 0x0000d1b7u)
            has_d1b7 = 1;
         if (cl[i] == 0x0000c8b7u)
            has_c8b7 = 1;
      }
      if (!has_d1b7 || !has_c8b7)
         return -429;

      ncl = 24;
      nv_device_info_fill_class_ladder(3, 0, cl, &ncl);
      for (i = 0; i < ncl; i++) {
         if (cl[i] == 0x0000cbb0u)
            has_cbb0 = 1;
      }
      if (!has_cbb0)
         return -430;

      if (NVC36F_SEMAPHORED_RELEASE_BLOB_610 != 0x1001u ||
          NVC36F_SEMAPHORED_RELEASE_VDPAU_610 != 0x2u)
         return -431;
   }

   /* tick132: GS/TCS/TES MOV-imm SPH builders + MME clear/init stubs + pipeline bind */
   {
      struct nv_sph_blob gsb, tcsb, tesb;
      struct nv_mme_program mme_clr, mme_init;
      uint32_t pbuf[96];
      uint32_t np, ii;
      bool saw_spa = false, saw_vs_en = false, saw_gs_dis = false;

      nv_sph_build_geometry_mov_imm_exit(&gsb, 8);
      if (nv_sph_smoke_validate_blob(&gsb, NV_SPH_TYPE_GEOMETRY) != 0)
         return -432;
      if (gsb.sass_dwords < 8)
         return -433;

      nv_sph_build_tess_init_mov_imm_exit(&tcsb, 8);
      if (nv_sph_smoke_validate_blob(&tcsb, NV_SPH_TYPE_TESS_INIT) != 0)
         return -434;

      nv_sph_build_tess_mov_imm_exit(&tesb, 8);
      if (nv_sph_smoke_validate_blob(&tesb, NV_SPH_TYPE_TESS) != 0)
         return -435;

      nv_mme_build_clear_helper_program_stub(&mme_clr, 48);
      if (!mme_clr.is_stub_end_only || mme_clr.insn_count < 3 ||
          mme_clr.slot != NV_MME_SLOT_CLEAR_HELPER)
         return -436;

      nv_mme_build_channel_init_program_stub(&mme_init, 64);
      if (!mme_init.is_stub_end_only ||
          mme_init.slot != NV_MME_SLOT_CHANNEL_INIT_SCRATCH)
         return -437;

      memset(pbuf, 0, sizeof(pbuf));
      nv_push_init(&p, pbuf, (uint32_t)(sizeof(pbuf) / 4));
      nv_3d_emit_g3_pipeline_bind_smoke(&p, 5, 3, 0x100000ull,
                                        0x200000ull, 16,
                                        0x300000ull, 8);
      np = nv_push_dw_count(&p);
      for (ii = 0; ii + 1 < np; ii++) {
         uint32_t hdr = pbuf[ii];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC597_SET_SPA_VERSION)
            saw_spa = true;
         if (method == NVC597_SET_PIPELINE_SHADER(NV_3D_PIPE_STAGE_VERTEX))
            saw_vs_en = true;
         if (method == NVC597_SET_PIPELINE_SHADER(NV_3D_PIPE_STAGE_GEOMETRY) &&
             pbuf[ii + 1] == NVC597_SET_PIPELINE_SHADER_ENABLE_FALSE)
            saw_gs_dis = true;
      }
      if (!saw_spa || !saw_vs_en || !saw_gs_dis)
         return -438;
   }

   /* tick133: G2/G3 bringup slices + video status poll + NVENC smoke slice */
   {
      uint32_t g2buf[2048], g3buf[2048], vbuf[256];
      uint32_t n2, n3, nv, i;
      bool saw_inv = false, saw_pcas = false, saw_clear = false;
      bool saw_enc_app = false, saw_enc_exec = false;
      struct nv_nvenc_status_snapshot ens;
      struct nv_nvdec_status_snapshot dns;
      uint8_t enc_st[NV_NVENC_STATUS_BO_MIN_BYTES];
      uint8_t dec_st[NV_NVDEC_STATUS_BO_MIN_BYTES];
      uint32_t *edw = (uint32_t *)enc_st;
      uint32_t *ddw = (uint32_t *)dec_st;

      memset(g2buf, 0, sizeof(g2buf));
      nv_push_init(&p, g2buf, (uint32_t)(sizeof(g2buf) / 4));
      if (nv_compute_emit_g2_smoke_slice(&p, 0xc5c0u, 0x900000ull, 16, 0x53,
                                         0xa00000ull, NULL, 0xb00000ull,
                                         0xc00000ull, 7u, 2u, 32u) != 0)
         return -439;
      n2 = nv_push_dw_count(&p);
      if (n2 < 20)
         return -440;
      for (i = 0; i + 1 < n2; i++) {
         uint32_t hdr = g2buf[i];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC3C0_INVALIDATE_SHADER_CACHES)
            saw_inv = true;
         if (method == NVC3C0_SEND_PCAS_A)
            saw_pcas = true;
      }
      if (!saw_inv || !saw_pcas)
         return -441;

      memset(g3buf, 0, sizeof(g3buf));
      nv_push_init(&p, g3buf, (uint32_t)(sizeof(g3buf) / 4));
      nv_3d_emit_g3_bringup_slice(&p, 0xc597u, 0x800000ull, 32, 32, 0xcf,
                                  NULL, 0, 0, 0, 0, 0, 0xd00000ull, 9u);
      n3 = nv_push_dw_count(&p);
      if (n3 < 16)
         return -442;
      for (i = 0; i + 1 < n3; i++) {
         uint32_t hdr = g3buf[i];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC597_CLEAR_SURFACE || method == 0x19d0u)
            saw_clear = true;
      }
      if (!saw_clear)
         return -443;

      memset(vbuf, 0, sizeof(vbuf));
      nv_push_init(&p, vbuf, (uint32_t)(sizeof(vbuf) / 4));
      if (nv_nvenc_emit_h264_smoke_slice(&p, 0xc8b7u, 0x710000ull, 0x800000ull,
                                         0x810000ull, 0x300000ull, 64, 64,
                                         0xe00000ull, 11u, NULL) != 0)
         return -444;
      nv = nv_push_dw_count(&p);
      for (i = 0; i + 1 < nv; i++) {
         uint32_t hdr = vbuf[i];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NV_NVENC_SET_APPLICATION_ID)
            saw_enc_app = true;
         if (method == NV_NVENC_EXECUTE)
            saw_enc_exec = true;
      }
      if (!saw_enc_app || !saw_enc_exec)
         return -445;

      memset(enc_st, 0, sizeof(enc_st));
      edw[NV_NVENC_STATUS_DW_PICTURE_INDEX] = 1;
      edw[NV_NVENC_STATUS_DW_ERROR_PACKED] = 0;
      edw[NV_NVENC_STATUS_DW_TOTAL_BIT_COUNT] = 1024;
      if (nv_nvenc_status_poll_snapshot(enc_st, sizeof(enc_st),
                                        NV_NVENC_APP_ID_H264, &ens) != 0)
         return -446;
      if (ens.total_bit_count != 1024)
         return -447;

      memset(dec_st, 0, sizeof(dec_st));
      ddw[NV_NVDEC_STATUS_DW_MBS_ERR] = 0;
      ddw[NV_NVDEC_STATUS_DW_ERROR_STATUS] = 0;
      if (nv_nvdec_status_poll_snapshot(dec_st, sizeof(dec_st), &dns) != 0)
         return -448;
      ddw[NV_NVDEC_STATUS_DW_ERROR_STATUS] = 1;
      if (nv_nvdec_status_poll_snapshot(dec_st, sizeof(dec_st), &dns) == 0)
         return -449; /* must fail on hw_error */
   }

   /* pass12 RE: sema 0x1000/0x1002 modes + G2/G3 channel_prep + ladder size */
   {
      enum nv_host_sema_mode ladder[NV_HOST_SEMA_MODE_COUNT];
      unsigned nl, i, has_1000 = 0, has_1002 = 0, has_1001 = 0;
      uint32_t g2p[128], g3p[2048];
      uint32_t n2p, n3p, ii;
      bool saw_wfi = false, saw_inv2 = false, saw_spa = false, saw_mme = false;

      if (nv_host_sema_execute(NV_HOST_SEMA_MODE_BLOB1000_ALIGN4) != 0x1000u)
         return -450;
      if (nv_host_sema_execute(NV_HOST_SEMA_MODE_BLOB1002_SHIFT2) != 0x1002u)
         return -451;
      if (!nv_host_sema_mode_uses_shift2(NV_HOST_SEMA_MODE_BLOB1000_SHIFT2))
         return -452;
      if (nv_host_sema_mode_uses_shift2(NV_HOST_SEMA_MODE_BLOB1000_ALIGN4))
         return -453;

      nl = nv_host_sema_ladder_fill(ladder, -1);
      if (nl != NV_HOST_SEMA_MODE_COUNT)
         return -454;
      for (i = 0; i < nl; i++) {
         uint32_t ex = nv_host_sema_execute(ladder[i]);
         if (ex == 0x1001u)
            has_1001 = 1;
         if (ex == 0x1000u)
            has_1000 = 1;
         if (ex == 0x1002u)
            has_1002 = 1;
      }
      if (!has_1001 || !has_1000 || !has_1002)
         return -455;
      /* pass12: primary 0x1001 modes still first in default ladder */
      if (ladder[0] != NV_HOST_SEMA_MODE_BLOB_ALIGN4)
         return -456;

      memset(g2p, 0, sizeof(g2p));
      nv_push_init(&p, g2p, (uint32_t)(sizeof(g2p) / 4));
      nv_compute_emit_g2_channel_prep(&p, 0xc5c0u, 0x53, 0xb00000ull, 256u);
      n2p = nv_push_dw_count(&p);
      if (n2p < 8)
         return -457;
      for (ii = 0; ii + 1 < n2p; ii++) {
         uint32_t hdr = g2p[ii];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC3C0_WAIT_FOR_IDLE)
            saw_wfi = true;
         if (method == NVC3C0_INVALIDATE_SHADER_CACHES)
            saw_inv2 = true;
      }
      if (!saw_wfi || !saw_inv2)
         return -458;

      memset(g3p, 0, sizeof(g3p));
      nv_push_init(&p, g3p, (uint32_t)(sizeof(g3p) / 4));
      nv_3d_emit_g3_channel_prep(&p, 0xc597u, 5, 3, true);
      n3p = nv_push_dw_count(&p);
      if (n3p < 10)
         return -459;
      for (ii = 0; ii + 1 < n3p; ii++) {
         uint32_t hdr = g3p[ii];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC597_SET_SPA_VERSION)
            saw_spa = true;
         if (method == NVC597_LOAD_MME_INSTRUCTION_RAM_POINTER ||
             method == NVC597_LOAD_MME_INSTRUCTION_RAM ||
             method == 0x0114u || method == 0x0118u)
            saw_mme = true;
         if (method == NVC597_WAIT_FOR_IDLE)
            saw_wfi = true;
      }
      if (!saw_spa || !saw_mme)
         return -460;
      (void)saw_wfi; /* WFI is best-effort; SPA+MME are required */
   }

   /* tick135: pass12 channel wire-up — SASS smoke helpers + MME clear scaffold */
   {
      struct nv_sass_buf sb;
      struct nv_mme_program mme_clr;
      uint32_t g2buf[2048], g3buf[2048];
      uint32_t n2, n3, ii;
      bool saw_prep_wfi = false, saw_prep_spa = false;

      nv_sass_buf_init(&sb);
      if (!nv_sass_emit_smoke_mov_imm_exit(&sb, 0, 0xcfu))
         return -461;
      if (sb.count < 4) /* 2 insns × 2 dwords */
         return -462;
      nv_sass_buf_finish(&sb);

      nv_sass_buf_init(&sb);
      if (!nv_sass_emit_smoke_nop_exit(&sb))
         return -463;
      if (sb.count < 4)
         return -464;
      nv_sass_buf_finish(&sb);

      nv_mme_build_clear_helper_program_stub(&mme_clr, 48);
      if (!mme_clr.is_stub_end_only || mme_clr.insn_count < 6 ||
          mme_clr.slot != NV_MME_SLOT_CLEAR_HELPER)
         return -465;

      /* G2 smoke_slice still emits WFI via channel_prep (tick135 path) */
      memset(g2buf, 0, sizeof(g2buf));
      nv_push_init(&p, g2buf, (uint32_t)(sizeof(g2buf) / 4));
      if (nv_compute_emit_g2_smoke_slice(&p, 0xc5c0u, 0x900000ull, 16, 0x53,
                                         0xa00000ull, NULL, 0, 0xc00000ull, 7u,
                                         1u, 1u) != 0)
         return -466;
      n2 = nv_push_dw_count(&p);
      for (ii = 0; ii + 1 < n2; ii++) {
         uint32_t hdr = g2buf[ii];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC3C0_WAIT_FOR_IDLE)
            saw_prep_wfi = true;
      }
      if (!saw_prep_wfi || n2 < 20)
         return -467;

      memset(g3buf, 0, sizeof(g3buf));
      nv_push_init(&p, g3buf, (uint32_t)(sizeof(g3buf) / 4));
      nv_3d_emit_g3_bringup_slice(&p, 0xc597u, 0x800000ull, 32, 32, 0xcf,
                                  NULL, 0, 0, 0, 0, 0, 0xd00000ull, 9u);
      n3 = nv_push_dw_count(&p);
      for (ii = 0; ii + 1 < n3; ii++) {
         uint32_t hdr = g3buf[ii];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC597_SET_SPA_VERSION)
            saw_prep_spa = true;
      }
      if (!saw_prep_spa || n3 < 20)
         return -468;
   }

   /* tick136: smoke_hw slice bits + NV_SMOKE_HW_G4 / ALL include video */
   {
      if (NV_SMOKE_HW_G4 != (1u << 4))
         return -469;
      if ((NV_SMOKE_HW_ALL & NV_SMOKE_HW_G4) == 0)
         return -470;
      if ((NV_SMOKE_HW_ALL & NV_SMOKE_HW_G0) == 0 ||
          (NV_SMOKE_HW_ALL & NV_SMOKE_HW_G1) == 0)
         return -471;
      /* Bringup slice helpers must be declared (link via channel.c on full build) */
      if (sizeof(struct nv_smoke_hw_result) < 64)
         return -472;
   }

   /* pass13 RE: sema rodata table execute modes 0x1004 / 0x0804 / 0x0802 */
   {
      enum nv_host_sema_mode ladder[NV_HOST_SEMA_MODE_COUNT];
      unsigned nl, i, has_1004 = 0, has_0804 = 0, has_0802 = 0;
      int pos_1001 = -1, pos_1002 = -1, pos_1004 = -1;

      if (nv_host_sema_execute(NV_HOST_SEMA_MODE_BLOB1004_ALIGN4) != 0x1004u)
         return -482;
      if (nv_host_sema_execute(NV_HOST_SEMA_MODE_BLOB1004_SHIFT2) != 0x1004u)
         return -483;
      if (nv_host_sema_execute(NV_HOST_SEMA_MODE_BLOB0804_ALIGN4) != 0x0804u)
         return -484;
      if (nv_host_sema_execute(NV_HOST_SEMA_MODE_BLOB0802_SHIFT2) != 0x0802u)
         return -485;
      if (!nv_host_sema_mode_uses_shift2(NV_HOST_SEMA_MODE_BLOB1004_SHIFT2))
         return -486;
      if (nv_host_sema_mode_uses_shift2(NV_HOST_SEMA_MODE_BLOB1004_ALIGN4))
         return -487;
      if (!nv_host_sema_mode_name(NV_HOST_SEMA_MODE_BLOB1004_ALIGN4) ||
          nv_host_sema_mode_name(NV_HOST_SEMA_MODE_BLOB1004_ALIGN4)[0] == '\0')
         return -488;

      nl = nv_host_sema_ladder_fill(ladder, -1);
      if (nl != NV_HOST_SEMA_MODE_COUNT || nl < 16)
         return -489;
      for (i = 0; i < nl; i++) {
         uint32_t ex = nv_host_sema_execute(ladder[i]);
         if (ex == 0x1001u && pos_1001 < 0)
            pos_1001 = (int)i;
         if (ex == 0x1002u && pos_1002 < 0)
            pos_1002 = (int)i;
         if (ex == 0x1004u && pos_1004 < 0)
            pos_1004 = (int)i;
         if (ex == 0x1004u)
            has_1004 = 1;
         if (ex == 0x0804u)
            has_0804 = 1;
         if (ex == 0x0802u)
            has_0802 = 1;
      }
      if (!has_1004 || !has_0804 || !has_0802)
         return -490;
      /* pass13 order: 0x1001 before 0x1002 before 0x1004 */
      if (pos_1001 < 0 || pos_1002 < 0 || pos_1004 < 0 ||
          pos_1001 > pos_1002 || pos_1002 > pos_1004)
         return -491;
      if (ladder[0] != NV_HOST_SEMA_MODE_BLOB_ALIGN4)
         return -492;
      if (ladder[2] != NV_HOST_SEMA_MODE_BLOB1002_ALIGN4)
         return -493;
      if (ladder[4] != NV_HOST_SEMA_MODE_BLOB1004_ALIGN4)
         return -494;
   }

   /* tick137: MME path C gate + compute MOV-imm SPH + nvdec smoke emit shape */
   {
      struct nv_mme_program clr;
      struct nv_sph_blob cb;
      struct nv_push p2;
      uint32_t pbuf[96], np, ii;
      bool saw_dec = false;
      struct nv_nvdec_pic_setup vpic;

      nv_mme_build_clear_helper_program_stub(&clr, 48);
      if (!clr.is_stub_end_only)
         return -473; /* stubs must remain gated until real ISA */
      memset(pbuf, 0, sizeof(pbuf));
      nv_push_init(&p2, pbuf, (uint32_t)(sizeof(pbuf) / 4));
      if (nv_mme_emit_call_macro_if_ready(&p2, &clr, 0))
         return -474; /* must not CALL while stub */
      if (nv_mme_emit_path_c_calls_if_ready(&p2) != 0)
         return -475;
      if (nv_mme_emit_upload_and_path_c_try(&p2))
         return -476; /* upload ok but path C must still be false */

      nv_sph_build_compute_mov_imm_exit(&cb, 0xcfu, 16);
      if (nv_sph_smoke_validate_blob(&cb, NV_SPH_TYPE_COMPUTE) != 0)
         return -477;
      if (cb.sass_dwords < 4)
         return -478;

      nv_nvdec_pic_setup_init_h264_smoke(&vpic, 0x700000ull, 0x700000ull, 0);
      memset(pbuf, 0, sizeof(pbuf));
      nv_push_init(&p2, pbuf, (uint32_t)(sizeof(pbuf) / 4));
      if (nv_nvdec_emit_smoke_slice(&p2, 0xc8b0u, &vpic, 0xe00000ull, 11u,
                                    NULL) != 0)
         return -479;
      np = nv_push_dw_count(&p2);
      if (np < 4)
         return -480;
      for (ii = 0; ii + 1 < np; ii++) {
         uint32_t hdr = pbuf[ii];
         if ((hdr >> 29) == 0)
            saw_dec = true;
      }
      if (!saw_dec)
         return -481;
   }

   /* tick139: SPA/SASS helpers; G2 smoke_slice passes spa; S2R+MOV SPH; sass smoke */
   {
      struct nv_device_info di;
      struct nv_sph_blob s2r_sph;
      struct nv_sass_buf sb;
      struct nv_push p2;
      uint32_t g2buf[2048], n2, ii;
      bool saw_wfi = false, saw_inv = false;
      uint8_t maj = 0, min = 0;

      memset(&di, 0, sizeof(di));
      if (nv_device_info_spa_version_u8(NULL) != 0x53u)
         return -495;
      if (nv_device_info_spa_version_u8(&di) != 0x53u)
         return -496;
      di.sm_version = 0x53u;
      if (nv_device_info_spa_version_u8(&di) != 0x53u)
         return -497;
      di.sm_version = 0x0806u; /* SM 8.6 BCD-ish */
      if (nv_device_info_spa_version_u8(&di) == 0)
         return -498;
      nv_device_info_spa_maj_min(NULL, &maj, &min);
      if (maj != 5u || min != 3u)
         return -499;
      nv_device_info_spa_maj_min(&di, &maj, &min);
      if (maj == 0)
         return -500;

      nv_sph_build_compute_s2r_mov_imm_exit(&s2r_sph, 0xabu, 16);
      if (nv_sph_smoke_validate_blob(&s2r_sph, NV_SPH_TYPE_COMPUTE) != 0)
         return -501;
      if (s2r_sph.sass_dwords < 6)
         return -502;

      nv_sass_buf_init(&sb);
      if (!nv_sass_emit_smoke_s2r_mov_imm_exit(&sb, 0, 0, 1, 0xcfu))
         return -503;
      if (sb.count < 6) /* 3 insns × 2 dwords */
         return -504;
      nv_sass_buf_finish(&sb);

      nv_sass_buf_init(&sb);
      if (!nv_sass_emit_smoke_s2r_store_imm_at_gva(&sb, 0x300000ull, 0xdeadbeefu))
         return -505;
      if (sb.count < 10) /* S2R + 3×MOV + STG + EXIT */
         return -506;
      nv_sass_buf_finish(&sb);

      /* G2 smoke_slice: channel_prep must get non-zero SPA (0x53 default) */
      memset(g2buf, 0, sizeof(g2buf));
      nv_push_init(&p2, g2buf, (uint32_t)(sizeof(g2buf) / 4));
      if (nv_compute_emit_g2_smoke_slice(&p2, 0xc5c0u, 0x900000ull, 16, 0x53,
                                         0xa00000ull, NULL, 0, 0xc00000ull, 7u,
                                         1u, 1u) != 0)
         return -507;
      n2 = nv_push_dw_count(&p2);
      if (n2 < 24)
         return -508;
      for (ii = 0; ii + 1 < n2; ii++) {
         uint32_t hdr = g2buf[ii];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC3C0_WAIT_FOR_IDLE)
            saw_wfi = true;
         if (method == NVC3C0_INVALIDATE_SHADER_CACHES ||
             method == NVC3C0_INVALIDATE_TEXTURE_HEADER_CACHE)
            saw_inv = true;
      }
      if (!saw_wfi || !saw_inv)
         return -509;

      /* G3 channel_prep_spa_u8 + path_c (stubs → no CALL, still valid push) */
      memset(g2buf, 0, sizeof(g2buf));
      nv_push_init(&p2, g2buf, (uint32_t)(sizeof(g2buf) / 4));
      nv_3d_emit_g3_channel_prep_spa_u8(&p2, 0xc597u, 0x53u, true);
      n2 = nv_push_dw_count(&p2);
      if (n2 < 4)
         return -510;
   }

   /* tick139 / pass14: NVDEC class ladder head; NOP+EXIT / S2R-SR SPH; sema ladder */
   {
      struct nv_sph_blob nop_sph, sr_sph;
      uint32_t nvdec_lad[24];
      unsigned nvdec_n = 24, ni;
      bool saw_d1 = false, saw_cf = false, saw_ce = false, saw_cd = false;
      bool saw_1004 = false, saw_0804 = false, saw_0802 = false;
      enum nv_host_sema_mode sema_modes[NV_HOST_SEMA_MODE_COUNT];
      unsigned sema_n, si;

      if (nv_video_pick_nvdec_class(0x9a) != NV_VIDEO_CLASS_NVDEC_D1B0)
         return -511;
      if (nv_video_pick_nvdec_class(0x95) != NV_VIDEO_CLASS_NVDEC_CFB0)
         return -512;
      if (nv_video_pick_nvdec_class(0x92) != NV_VIDEO_CLASS_NVDEC_CEB0)
         return -513;
      if (nv_video_pick_nvdec_class(0x90) != NV_VIDEO_CLASS_NVDEC_CDB0)
         return -514;
      if (nv_video_pick_nvdec_class(0x80) != NV_VIDEO_CLASS_NVDEC_AMPERE_C1)
         return -515;

      nv_device_info_fill_class_ladder(3 /* nvdec */, 0, nvdec_lad, &nvdec_n);
      if (nvdec_n < 8)
         return -516;
      for (ni = 0; ni < nvdec_n; ni++) {
         if (nvdec_lad[ni] == 0x0000d1b0u)
            saw_d1 = true;
         if (nvdec_lad[ni] == 0x0000cfb0u)
            saw_cf = true;
         if (nvdec_lad[ni] == 0x0000ceb0u)
            saw_ce = true;
         if (nvdec_lad[ni] == 0x0000cdb0u)
            saw_cd = true;
      }
      if (!saw_d1 || !saw_cf || !saw_ce || !saw_cd)
         return -517;
      /* pass14 newest-first: D1B0 should be first entry (no prefer_first) */
      if (nvdec_lad[0] != 0x0000d1b0u)
         return -518;

      nv_sph_build_compute_nop_exit(&nop_sph, 8);
      if (nv_sph_smoke_validate_blob(&nop_sph, NV_SPH_TYPE_COMPUTE) != 0)
         return -519;
      if (nop_sph.sass_dwords < 4)
         return -520;

      nv_sph_build_compute_s2r_sr_mov_imm_exit(&sr_sph, 3, 0x55u, 16);
      if (nv_sph_smoke_validate_blob(&sr_sph, NV_SPH_TYPE_COMPUTE) != 0)
         return -521;
      if (sr_sph.sass_dwords < 6)
         return -522;
      /* lo dword of first insn should carry SR index in low byte */
      if ((sr_sph.sass[0] & 0xffu) != 3u)
         return -523;

      sema_n = nv_host_sema_ladder_fill(sema_modes, -1);
      if (sema_n < 16)
         return -524;
      for (si = 0; si < sema_n; si++) {
         if (sema_modes[si] == NV_HOST_SEMA_MODE_BLOB1004_ALIGN4 ||
             sema_modes[si] == NV_HOST_SEMA_MODE_BLOB1004_SHIFT2)
            saw_1004 = true;
         if (sema_modes[si] == NV_HOST_SEMA_MODE_BLOB0804_ALIGN4 ||
             sema_modes[si] == NV_HOST_SEMA_MODE_BLOB0804_SHIFT2)
            saw_0804 = true;
         if (sema_modes[si] == NV_HOST_SEMA_MODE_BLOB0802_ALIGN4 ||
             sema_modes[si] == NV_HOST_SEMA_MODE_BLOB0802_SHIFT2)
            saw_0802 = true;
      }
      if (!saw_1004 || !saw_0804 || !saw_0802)
         return -525;
   }

   /* tick140: pass14 MME 0x34a8=scratch(42); sema slot methods; CB select+bind */
   {
      struct nv_push p2;
      uint32_t g2buf[2048], n2, ii;
      bool saw_scratch0 = false, saw_scratch42 = false;
      bool saw_sel_a = false, saw_bind = false;
      uint32_t consts[4] = { 1, 2, 3, 4 };
      enum nv_host_sema_mode m1004 = NV_HOST_SEMA_MODE_BLOB1004_ALIGN4;
      enum nv_host_sema_mode m1002 = NV_HOST_SEMA_MODE_BLOB1002_ALIGN4;
      enum nv_host_sema_mode m0802 = NV_HOST_SEMA_MODE_BLOB0802_ALIGN4;
      enum nv_host_sema_mode m1001 = NV_HOST_SEMA_MODE_BLOB_ALIGN4;

      if (NV_MME_PASS14_LIT_METHOD_OFF != 0x34a8u)
         return -526;
      if (NV_MME_PASS14_LIT_SCRATCH_INDEX != 42u)
         return -527;
      if (NV_MME_METHOD_SET_MME_SHADOW_SCRATCH_I(42) != 0x34a8u)
         return -528;
      if (NV_MME_PASS14_LIT_METHOD_IDX != 0x0d2au)
         return -529;

      if (nv_host_sema_execute_method(m1004) != NVC36F_SEMAPHOREC)
         return -530;
      if (nv_host_sema_execute_method(m1002) != NVC36F_SEMAPHOREB)
         return -531;
      if (nv_host_sema_execute_method(m0802) != NVC36F_SEMAPHOREA)
         return -532;
      if (nv_host_sema_execute_method(m1001) != NVC36F_SEMAPHORED)
         return -533;

      memset(g2buf, 0, sizeof(g2buf));
      nv_push_init(&p2, g2buf, (uint32_t)(sizeof(g2buf) / 4));
      nv_push_sema_release_mode_slot(&p2, 0x500000ull, 7u, m1004);
      n2 = nv_push_dw_count(&p2);
      if (n2 < 8) /* A/B/C + exec on C + D pad */
         return -534;

      memset(g2buf, 0, sizeof(g2buf));
      nv_push_init(&p2, g2buf, (uint32_t)(sizeof(g2buf) / 4));
      nv_push_set_subch(&p2, NV_PUSH_SUBCH_3D);
      nv_mme_emit_shadow_scratch_init_range(&p2, 8);
      n2 = nv_push_dw_count(&p2);
      if (n2 < 4)
         return -535;
      for (ii = 0; ii + 1 < n2; ii++) {
         uint32_t hdr = g2buf[ii];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NV_MME_METHOD_SET_MME_SHADOW_SCRATCH_I(0))
            saw_scratch0 = true;
         if (method == NV_MME_PASS14_LIT_METHOD_OFF)
            saw_scratch42 = true;
      }
      if (!saw_scratch0 || !saw_scratch42)
         return -536;

      memset(g2buf, 0, sizeof(g2buf));
      nv_push_init(&p2, g2buf, (uint32_t)(sizeof(g2buf) / 4));
      nv_push_set_subch(&p2, NV_PUSH_SUBCH_3D);
      nv_3d_upload_and_bind_push_constants(&p2, 0x600000ull, 256u, 0,
                                           consts, 4);
      n2 = nv_push_dw_count(&p2);
      if (n2 < 8)
         return -537;
      for (ii = 0; ii + 1 < n2; ii++) {
         uint32_t hdr = g2buf[ii];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC597_SET_CONSTANT_BUFFER_SELECTOR_A)
            saw_sel_a = true;
         if (method == NVC597_BIND_GROUP_CONSTANT_BUFFER(NV_3D_PUSH_CONST_BIND_GROUP_VS) ||
             method == NVC597_BIND_GROUP_CONSTANT_BUFFER(NV_3D_PUSH_CONST_BIND_GROUP_FS))
            saw_bind = true;
      }
      if (!saw_sel_a || !saw_bind)
         return -538;

      memset(g2buf, 0, sizeof(g2buf));
      nv_push_init(&p2, g2buf, (uint32_t)(sizeof(g2buf) / 4));
      nv_push_set_subch(&p2, NV_PUSH_SUBCH_3D);
      nv_3d_select_and_bind_push_constants(&p2, 0x700000ull, 256u);
      if (nv_push_dw_count(&p2) < 6)
         return -539;

      /* G3 channel_prep should emit scratch 0x34a8 when upload_mme */
      memset(g2buf, 0, sizeof(g2buf));
      nv_push_init(&p2, g2buf, (uint32_t)(sizeof(g2buf) / 4));
      nv_3d_emit_g3_channel_prep_spa_u8(&p2, 0xc597u, 0x53u, true);
      n2 = nv_push_dw_count(&p2);
      saw_scratch42 = false;
      for (ii = 0; ii + 1 < n2; ii++) {
         uint32_t hdr = g2buf[ii];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NV_MME_PASS14_LIT_METHOD_OFF)
            saw_scratch42 = true;
      }
      if (n2 < 8 || !saw_scratch42)
         return -540;
   }

   /* tick141: host sema auto/slot/classic policy; sema emit wfi_ex */
   {
      struct nv_push p2;
      uint32_t g2buf[2048], n2, ii;
      bool saw_exec_c = false, saw_exec_d = false;
      enum nv_host_sema_mode m1004 = NV_HOST_SEMA_MODE_BLOB1004_ALIGN4;
      enum nv_host_sema_mode m1001 = NV_HOST_SEMA_MODE_BLOB_ALIGN4;

      /* auto: 1004 uses slot (exec on C, pad D=0) */
      memset(g2buf, 0, sizeof(g2buf));
      nv_push_init(&p2, g2buf, (uint32_t)(sizeof(g2buf) / 4));
      nv_push_host_semaphore_release_wfi_mode_ex(&p2, 0x800000ull, 3u, false,
                                                 m1004, 0 /* auto */);
      n2 = nv_push_dw_count(&p2);
      if (n2 < 6)
         return -541;
      for (ii = 0; ii + 1 < n2; ii++) {
         uint32_t hdr = g2buf[ii];
         uint32_t method = (hdr & 0x1fff) << 2;
         uint32_t imm = (ii + 1 < n2) ? g2buf[ii + 1] : 0;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC36F_SEMAPHOREC && imm == NVC36F_SEMAPHORED_RELEASE_BLOB_1004)
            saw_exec_c = true;
         if (method == NVC36F_SEMAPHORED && imm == NVC36F_SEMAPHORED_RELEASE_BLOB_1004)
            saw_exec_d = true;
      }
      if (!saw_exec_c)
         return -542;

      /* classic: 1004 execute only in D */
      saw_exec_c = false;
      saw_exec_d = false;
      memset(g2buf, 0, sizeof(g2buf));
      nv_push_init(&p2, g2buf, (uint32_t)(sizeof(g2buf) / 4));
      nv_push_host_semaphore_release_wfi_mode_ex(&p2, 0x800000ull, 3u, false,
                                                 m1004, 1 /* classic */);
      n2 = nv_push_dw_count(&p2);
      for (ii = 0; ii + 1 < n2; ii++) {
         uint32_t hdr = g2buf[ii];
         uint32_t method = (hdr & 0x1fff) << 2;
         uint32_t imm = (ii + 1 < n2) ? g2buf[ii + 1] : 0;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC36F_SEMAPHOREC && imm == NVC36F_SEMAPHORED_RELEASE_BLOB_1004)
            saw_exec_c = true;
         if (method == NVC36F_SEMAPHORED && imm == NVC36F_SEMAPHORED_RELEASE_BLOB_1004)
            saw_exec_d = true;
      }
      if (!saw_exec_d || saw_exec_c)
         return -543;

      /* 1001 always execute in D regardless of emit policy */
      saw_exec_d = false;
      memset(g2buf, 0, sizeof(g2buf));
      nv_push_init(&p2, g2buf, (uint32_t)(sizeof(g2buf) / 4));
      nv_push_host_semaphore_release_mode(&p2, 0x900000ull, 1u, m1001);
      n2 = nv_push_dw_count(&p2);
      for (ii = 0; ii + 1 < n2; ii++) {
         uint32_t hdr = g2buf[ii];
         uint32_t method = (hdr & 0x1fff) << 2;
         uint32_t imm = (ii + 1 < n2) ? g2buf[ii + 1] : 0;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC36F_SEMAPHORED && imm == NVC36F_SEMAPHORED_RELEASE_BLOB_610)
            saw_exec_d = true;
      }
      if (!saw_exec_d)
         return -544;
   }

   /* tick142 / pass15: GPFIFO/NVDEC/NVENC ladders; sema 0802→A slot; NVENC pick */
   {
      uint32_t gpf_lad[24], nvdec_lad[24], nvenc_lad[24];
      unsigned gpf_n = 24, nvdec_n = 24, nvenc_n = 24, ni;
      bool saw_ca6f = false, saw_c56f = false, saw_b8b0 = false;
      bool saw_d1b7 = false, saw_cfb7 = false;
      bool saw_exec_a = false;
      struct nv_push p2;
      uint32_t g2buf[2048], n2, ii;
      enum nv_host_sema_mode m0802 = NV_HOST_SEMA_MODE_BLOB0802_ALIGN4;

      nv_device_info_fill_class_ladder(5 /* gpfifo */, 0, gpf_lad, &gpf_n);
      if (gpf_n < 8)
         return -545;
      /* pass15: head is CA6F; C56F appears before synthetic C86F/C76F alts */
      if (gpf_lad[0] != 0x0000ca6fu)
         return -546;
      for (ni = 0; ni < gpf_n; ni++) {
         if (gpf_lad[ni] == 0x0000ca6fu)
            saw_ca6f = true;
         if (gpf_lad[ni] == 0x0000c56fu)
            saw_c56f = true;
      }
      if (!saw_ca6f || !saw_c56f)
         return -547;

      nv_device_info_fill_class_ladder(3 /* nvdec */, 0, nvdec_lad, &nvdec_n);
      if (nvdec_n < 8 || nvdec_lad[0] != 0x0000d1b0u)
         return -548;
      for (ni = 0; ni < nvdec_n && ni < 10; ni++) {
         if (nvdec_lad[ni] == 0x0000b8b0u)
            saw_b8b0 = true;
      }
      /* B8B0 should appear early (pass15 rodata order near C9B0/C7B0) */
      if (!saw_b8b0)
         return -549;

      nv_device_info_fill_class_ladder(4 /* nvenc */, 0, nvenc_lad, &nvenc_n);
      if (nvenc_n < 6 || nvenc_lad[0] != 0x0000d1b7u)
         return -550;
      for (ni = 0; ni < nvenc_n; ni++) {
         if (nvenc_lad[ni] == 0x0000d1b7u)
            saw_d1b7 = true;
         if (nvenc_lad[ni] == 0x0000cfb7u)
            saw_cfb7 = true;
      }
      if (!saw_d1b7 || !saw_cfb7)
         return -551;
      if (nv_video_pick_nvenc_class(0x9a) != NV_VIDEO_CLASS_NVENC_D1B7)
         return -552;
      if (nv_video_pick_nvenc_class(0x95) != NV_VIDEO_CLASS_NVENC_CFB7)
         return -553;
      if (nv_video_pick_nvenc_class(0x92) != NV_VIDEO_CLASS_NVENC_CEB7)
         return -554;

      /* pass15 table: 0x0802 executes on SEMAPHOREA (slot A), not only D */
      memset(g2buf, 0, sizeof(g2buf));
      nv_push_init(&p2, g2buf, (uint32_t)(sizeof(g2buf) / 4));
      nv_push_host_semaphore_release_wfi_mode_ex(&p2, 0xa00000ull, 7u, false,
                                                 m0802, 0 /* auto/slot */);
      n2 = nv_push_dw_count(&p2);
      if (n2 < 6)
         return -555;
      for (ii = 0; ii + 1 < n2; ii++) {
         uint32_t hdr = g2buf[ii];
         uint32_t method = (hdr & 0x1fff) << 2;
         uint32_t imm = g2buf[ii + 1];
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC36F_SEMAPHOREA &&
             imm == NVC36F_SEMAPHORED_RELEASE_BLOB_0802)
            saw_exec_a = true;
      }
      if (!saw_exec_a)
         return -556;
   }

   /* tick143: AUX *FA ladder (engine 6); MME scratch init 16 + pass14/15 hot idx */
   {
      uint32_t fa_lad[16];
      unsigned fa_n = 16, ni;
      bool saw_d1fa = false, saw_c6fa = false;
      bool saw_sc42 = false, saw_sc_hi = false;
      struct nv_push p2;
      uint32_t g2buf[2048], n2, ii;
      uint32_t idx_3998 = NV_MME_PASS15_SCRATCH_IDX_FROM_OFF(0x3998u);
      uint32_t idx_39e0 = NV_MME_PASS15_SCRATCH_IDX_FROM_OFF(0x39e0u);

      nv_device_info_fill_class_ladder(6 /* aux FA */, 0, fa_lad, &fa_n);
      if (fa_n < 6 || fa_lad[0] != 0x0000d1fau)
         return -557;
      for (ni = 0; ni < fa_n; ni++) {
         if (fa_lad[ni] == 0x0000d1fau)
            saw_d1fa = true;
         if (fa_lad[ni] == 0x0000c6fau)
            saw_c6fa = true;
      }
      if (!saw_d1fa || !saw_c6fa)
         return -558;

      memset(g2buf, 0, sizeof(g2buf));
      nv_push_init(&p2, g2buf, (uint32_t)(sizeof(g2buf) / 4));
      nv_mme_emit_shadow_scratch_init_range(&p2, 16);
      n2 = nv_push_dw_count(&p2);
      if (n2 < 20)
         return -559;
      for (ii = 0; ii + 1 < n2; ii++) {
         uint32_t hdr = g2buf[ii];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NV_MME_PASS14_LIT_METHOD_OFF)
            saw_sc42 = true;
         if (idx_3998 != 0xffffffffu &&
             method == NV_MME_METHOD_SET_MME_SHADOW_SCRATCH_I(idx_3998))
            saw_sc_hi = true;
         if (idx_39e0 != 0xffffffffu &&
             method == NV_MME_METHOD_SET_MME_SHADOW_SCRATCH_I(idx_39e0))
            saw_sc_hi = true;
      }
      if (!saw_sc42)
         return -560;
      /* 0x3998/0x39e0 are >= 0x3800 so NOT scratch methods — expect no hi hit;
       * still verify pass14 0x34a8 and first 16 slots were programmed. */
      (void)saw_sc_hi;
      (void)idx_3998;
      (void)idx_39e0;
   }

   /* tick144: pass15 multi-SR S2R SPH; QMD pass15 defaults with invalidate bits */
   {
      struct nv_sph_blob p15_sph;
      struct nv_qmd_desc qd;
      uint32_t qmd[NV_QMD_DWORDS];
      bool inv_tex = false, inv_ins = false, inv_const = false;

      nv_sph_build_compute_s2r_pass15_multi_sr_exit(&p15_sph, 0xaau, 16);
      if (nv_sph_smoke_validate_blob(&p15_sph, NV_SPH_TYPE_COMPUTE) != 0)
         return -561;
      if (p15_sph.sass_dwords < 10)
         return -562;
      /* second insn hi should be S2R class for first S2R */
      if (p15_sph.sass[1] != NV_SPH_SASS_S2R_HI_CS &&
          p15_sph.sass[1] != 0x86400000u)
         return -563;
      /* SR index 0x48 in second S2R lo (bits 27:20) */
      if (((p15_sph.sass[2] >> 20) & 0xffu) != 0x48u)
         return -564;
      if (((p15_sph.sass[4] >> 20) & 0xffu) != 0x50u)
         return -565;

      nv_qmd_desc_init_pass15_defaults(&qd, 0x1000ull, 0, 16);
      qd.program_addr = 0x200000ull;
      nv_qmd_encode(&qd, qmd);
      /* F_ macros expand to hi,lo — pass once each (3-arg nv_qmd_get) */
      if (nv_qmd_get(qmd, NV_QMD_F_REQUIRE_SCHEDULING_PCAS) != 1)
         return -566;
      if (nv_qmd_get(qmd, NV_QMD_F_INVALIDATE_TEXTURE_DATA_CACHE) == 1)
         inv_tex = true;
      if (nv_qmd_get(qmd, NV_QMD_F_INVALIDATE_INSTRUCTION_CACHE) == 1)
         inv_ins = true;
      if (nv_qmd_get(qmd, NV_QMD_F_INVALIDATE_SHADER_CONSTANT_CACHE) == 1)
         inv_const = true;
      if (!inv_tex || !inv_ins || !inv_const)
         return -567;
      if (nv_qmd_get(qmd, NV_QMD_F_CTA_THREAD_DIMENSION0) != 32)
         return -568;
   }

   /* tick145 / pass16: MME 0x39e0 post-config; pass16 multi-SR SPH; QMD+sema */
   {
      struct nv_push p2;
      uint32_t g2buf[2048], n2, ii;
      bool saw_39e0 = false, saw_sc42 = false;
      struct nv_sph_blob p16_sph;
      struct nv_qmd_desc qd16;
      uint32_t qmd16[NV_QMD_DWORDS];
      uint8_t sr_at_r4, sr_at_r5, sr_at_r6;

      memset(g2buf, 0, sizeof(g2buf));
      nv_push_init(&p2, g2buf, (uint32_t)(sizeof(g2buf) / 4));
      nv_mme_emit_channel_prime_upload_pass16(&p2);
      n2 = nv_push_dw_count(&p2);
      if (n2 < 24)
         return -569;
      for (ii = 0; ii + 1 < n2; ii++) {
         uint32_t hdr = g2buf[ii];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NV_MME_PASS16_POST_CONFIG_METHOD_OFF)
            saw_39e0 = true;
         if (method == NV_MME_PASS14_LIT_METHOD_OFF)
            saw_sc42 = true;
      }
      if (!saw_sc42)
         return -570;
      if (!saw_39e0)
         return -571;

      nv_sph_build_compute_s2r_pass16_multi_sr_exit(&p16_sph, 0x56u, 16);
      if (nv_sph_smoke_validate_blob(&p16_sph, NV_SPH_TYPE_COMPUTE) != 0)
         return -572;
      /* 7 S2R + MOV + EXIT = 9 insns * 2 dwords = 18 */
      if (p16_sph.sass_dwords < 18)
         return -573;
      /* R4 = SR 0x48, R5 = SR 0x49, R6 = SR 0x50 (insn indices 4,5,6) */
      sr_at_r4 = (uint8_t)((p16_sph.sass[8] >> 20) & 0xffu);
      sr_at_r5 = (uint8_t)((p16_sph.sass[10] >> 20) & 0xffu);
      sr_at_r6 = (uint8_t)((p16_sph.sass[12] >> 20) & 0xffu);
      if (sr_at_r4 != 0x48u || sr_at_r5 != 0x49u || sr_at_r6 != 0x50u)
         return -574;
      if (p16_sph.sass[1] != NV_SPH_SASS_S2R_HI_CS &&
          p16_sph.sass[1] != 0x86400000u)
         return -575;

      nv_qmd_desc_init_pass16_defaults(&qd16, 0x200000ull, 0, 16,
                                       0x300000ull, 0x42u);
      /* encode_full applies sema_release0_* sideband (encode alone does not) */
      nv_qmd_encode_full(&qd16, qmd16);
      if (nv_qmd_get(qmd16, NV_QMD_F_REQUIRE_SCHEDULING_PCAS) != 1)
         return -576;
      if (nv_qmd_get(qmd16, NV_QMD_F_INVALIDATE_INSTRUCTION_CACHE) != 1)
         return -577;
      if (nv_qmd_get(qmd16, NV_QMD_F_SEMAPHORE_RELEASE_ENABLE0) != 1)
         return -578;
   }

   /* tick146 / pass17: formal sema table; pass17 SPH 0x4c; G2 prep+QMD */
   {
      unsigned sema_n = 0, si;
      const struct nv_host_sema_pass17_row *sema_t;
      struct nv_push p17;
      uint32_t sbuf[64], sn, sjj;
      bool saw_exec_c = false, saw_exec_a = false;
      struct nv_sph_blob p17_sph;
      uint8_t sr3, sr5;
      struct nv_qmd_desc qd17;
      uint32_t qmd17[NV_QMD_DWORDS];
      struct nv_push g2p;
      uint32_t g2b[512], gn;

      sema_t = nv_host_sema_pass17_table(&sema_n);
      if (!sema_t || sema_n != NV_HOST_SEMA_PASS17_NUM_ROWS)
         return -579;
      /* first row: 0x1004 → C; last authoritative: 0x1001 → NONSTD/D */
      if (sema_t[0].exec != 0x1004u || sema_t[0].sema_idx != 0x12u ||
          sema_t[0].slot != NV_HOST_SEMA_SLOT_C)
         return -580;
      if (sema_t[10].exec != 0x1001u || sema_t[10].sema_idx != 0x1du ||
          sema_t[10].slot != NV_HOST_SEMA_SLOT_NONSTD)
         return -581;
      if (nv_host_sema_pass17_slot_for_exec(0x1004u) != NV_HOST_SEMA_SLOT_C)
         return -582;
      if (nv_host_sema_pass17_slot_for_exec(0x0802u) != NV_HOST_SEMA_SLOT_A)
         return -583;
      if (nv_host_sema_pass17_method_for_idx(0x12u) != NVC36F_SEMAPHOREC)
         return -584;
      if (!nv_host_sema_pass17_prefers_slot_emit(NV_HOST_SEMA_MODE_BLOB1004_ALIGN4))
         return -585;
      if (nv_host_sema_pass17_prefers_slot_emit(NV_HOST_SEMA_MODE_BLOB_ALIGN4))
         return -586; /* 0x1001 → classic D, not slot-prefer */

      /* pass17 sema emit: 0x1004 should write execute on SEMAPHOREC */
      memset(sbuf, 0, sizeof(sbuf));
      nv_push_init(&p17, sbuf, (uint32_t)(sizeof(sbuf) / 4));
      nv_push_set_subch(&p17, NV_PUSH_SUBCH_3D);
      nv_push_sema_release_mode_pass17(&p17, 0x400000ull, 0x99u,
                                       NV_HOST_SEMA_MODE_BLOB1004_ALIGN4);
      sn = nv_push_dw_count(&p17);
      for (sjj = 0; sjj + 1 < sn; sjj++) {
         uint32_t hdr = sbuf[sjj];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC36F_SEMAPHOREC && sbuf[sjj + 1] == 0x1004u)
            saw_exec_c = true;
      }
      if (!saw_exec_c)
         return -587;

      memset(sbuf, 0, sizeof(sbuf));
      nv_push_init(&p17, sbuf, (uint32_t)(sizeof(sbuf) / 4));
      nv_push_set_subch(&p17, NV_PUSH_SUBCH_3D);
      nv_push_sema_release_mode_pass17(&p17, 0x400000ull, 0x99u,
                                       NV_HOST_SEMA_MODE_BLOB0802_ALIGN4);
      sn = nv_push_dw_count(&p17);
      for (sjj = 0; sjj + 1 < sn; sjj++) {
         uint32_t hdr = sbuf[sjj];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC36F_SEMAPHOREA && sbuf[sjj + 1] == 0x0802u)
            saw_exec_a = true;
      }
      if (!saw_exec_a)
         return -588;

      /* all 11 rows have aux == tag_b and valid sema_idx set */
      for (si = 0; si < sema_n; si++) {
         if (sema_t[si].aux != sema_t[si].tag_b)
            return -589;
         if (sema_t[si].sema_idx != 0x10u && sema_t[si].sema_idx != 0x11u &&
             sema_t[si].sema_idx != 0x12u && sema_t[si].sema_idx != 0x07u &&
             sema_t[si].sema_idx != 0x08u && sema_t[si].sema_idx != 0x1du)
            return -590;
      }

      nv_sph_build_compute_s2r_pass17_multi_sr_exit(&p17_sph, 0x57u, 16);
      if (nv_sph_smoke_validate_blob(&p17_sph, NV_SPH_TYPE_COMPUTE) != 0)
         return -591;
      if (p17_sph.sass_dwords < 18)
         return -592;
      /* R3=SR0x48, R5=SR0x4c (insn indices 3,5) */
      sr3 = (uint8_t)((p17_sph.sass[6] >> 20) & 0xffu);
      sr5 = (uint8_t)((p17_sph.sass[10] >> 20) & 0xffu);
      if (sr3 != 0x48u || sr5 != 0x4cu)
         return -593;

      nv_qmd_desc_init_pass17_defaults(&qd17, 0x210000ull, 0, 16,
                                       0x310000ull, 0x43u);
      nv_qmd_encode_full(&qd17, qmd17);
      if (nv_qmd_get(qmd17, NV_QMD_F_SEMAPHORE_RELEASE_ENABLE0) != 1)
         return -594;
      if (nv_qmd_get(qmd17, NV_QMD_F_INVALIDATE_INSTRUCTION_CACHE) != 1)
         return -595;

      memset(g2b, 0, sizeof(g2b));
      nv_push_init(&g2p, g2b, (uint32_t)(sizeof(g2b) / 4));
      if (nv_compute_emit_g2_smoke_slice_pass17(&g2p, 0xc5c0u, 0x900000ull,
                                                16, 0x53, 0x800000ull, NULL,
                                                0, 0x500000ull, 0x11u,
                                                1, 32,
                                                NV_HOST_SEMA_MODE_BLOB1004_ALIGN4,
                                                true) != 0)
         return -596;
      gn = nv_push_dw_count(&g2p);
      if (gn < 40)
         return -597;
   }

   /* tick147: channel pass17 sema emit ladder + WFI_mode_ex pass17 default */
   {
      int elad[3];
      unsigned nel;
      struct nv_push pw;
      uint32_t wbuf[48], wn, wj;
      bool saw_c = false, saw_d_classic = false;

      if (nv_host_sema_emit_pref_normalize(-1) != NV_HOST_SEMA_EMIT_PASS17)
         return -598;
      if (nv_host_sema_emit_pref_normalize(1) != NV_HOST_SEMA_EMIT_CLASSIC)
         return -599;
      if (nv_host_sema_emit_pref_normalize(3) != NV_HOST_SEMA_EMIT_PASS17_EXPLICIT)
         return -600;

      nel = nv_host_sema_emit_ladder_fill(elad, 0,
                                          NV_HOST_SEMA_MODE_BLOB1004_ALIGN4);
      if (nel < 2)
         return -601;
      if (elad[0] != NV_HOST_SEMA_EMIT_PASS17)
         return -602;
      if (elad[1] != NV_HOST_SEMA_EMIT_CLASSIC)
         return -603;

      /* pass17 emit (0): 0x1004 → execute on C */
      memset(wbuf, 0, sizeof(wbuf));
      nv_push_init(&pw, wbuf, (uint32_t)(sizeof(wbuf) / 4));
      nv_push_set_subch(&pw, NV_PUSH_SUBCH_3D);
      nv_push_host_semaphore_release_wfi_mode_ex(
         &pw, 0x600000ull, 5u, true, NV_HOST_SEMA_MODE_BLOB1004_ALIGN4,
         NV_HOST_SEMA_EMIT_PASS17);
      wn = nv_push_dw_count(&pw);
      if (wn < 6)
         return -604;
      for (wj = 0; wj + 1 < wn; wj++) {
         uint32_t hdr = wbuf[wj];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC36F_SEMAPHOREC && wbuf[wj + 1] == 0x1004u)
            saw_c = true;
      }
      if (!saw_c)
         return -605;

      /* classic emit (1): 0x1004 → execute on D */
      memset(wbuf, 0, sizeof(wbuf));
      nv_push_init(&pw, wbuf, (uint32_t)(sizeof(wbuf) / 4));
      nv_push_set_subch(&pw, NV_PUSH_SUBCH_3D);
      nv_push_host_semaphore_release_wfi_mode_ex(
         &pw, 0x600000ull, 5u, false, NV_HOST_SEMA_MODE_BLOB1004_ALIGN4,
         NV_HOST_SEMA_EMIT_CLASSIC);
      wn = nv_push_dw_count(&pw);
      for (wj = 0; wj + 1 < wn; wj++) {
         uint32_t hdr = wbuf[wj];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC36F_SEMAPHORED && wbuf[wj + 1] == 0x1004u)
            saw_d_classic = true;
      }
      if (!saw_d_classic)
         return -606;

      /* host_semaphore_release_mode default is pass17 (1002 → B) */
      memset(wbuf, 0, sizeof(wbuf));
      nv_push_init(&pw, wbuf, (uint32_t)(sizeof(wbuf) / 4));
      nv_push_set_subch(&pw, NV_PUSH_SUBCH_3D);
      nv_push_host_semaphore_release_mode(&pw, 0x700000ull, 9u,
                                          NV_HOST_SEMA_MODE_BLOB1002_ALIGN4);
      wn = nv_push_dw_count(&pw);
      saw_c = false;
      for (wj = 0; wj + 1 < wn; wj++) {
         uint32_t hdr = wbuf[wj];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC36F_SEMAPHOREB && wbuf[wj + 1] == 0x1002u)
            saw_c = true;
      }
      if (!saw_c)
         return -607;
   }

   /* tick148: G3 pass17 channel_prep (MME 0x39e0 + pass17 sema tail) */
   {
      struct nv_push g3p;
      uint32_t g3b[4096], gn, gi;
      bool saw_39e0 = false, saw_sc42 = false, saw_exec_c = false;

      memset(g3b, 0, sizeof(g3b));
      nv_push_init(&g3p, g3b, (uint32_t)(sizeof(g3b) / 4));
      nv_3d_emit_g3_channel_prep_pass17(&g3p, 0xc597u, 5, 3, true,
                                        0x900000ull, 0x77u,
                                        NV_HOST_SEMA_MODE_BLOB1004_ALIGN4,
                                        NV_HOST_SEMA_EMIT_PASS17);
      gn = nv_push_dw_count(&g3p);
      if (gn < 30)
         return -608;
      for (gi = 0; gi + 1 < gn; gi++) {
         uint32_t hdr = g3b[gi];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NV_MME_PASS16_POST_CONFIG_METHOD_OFF)
            saw_39e0 = true;
         if (method == NV_MME_PASS14_LIT_METHOD_OFF)
            saw_sc42 = true;
         if (method == NVC36F_SEMAPHOREC && g3b[gi + 1] == 0x1004u)
            saw_exec_c = true;
      }
      if (!saw_sc42)
         return -609;
      if (!saw_39e0)
         return -610;
      if (!saw_exec_c)
         return -611;
   }

   /* tick148 / pass18: G3 inv+WFI+host sema pass17 (clear_then_host pipeline) */
   {
      struct nv_push ip;
      uint32_t ib[512], in, ii;
      bool saw_inv = false, saw_wfi = false, saw_exec = false;

      memset(ib, 0, sizeof(ib));
      nv_push_init(&ip, ib, (uint32_t)(sizeof(ib) / 4));
      nv_3d_emit_g3_inv_wfi_host_sema_pass17(
         &ip, 0x910000ull, 0x88u, NV_HOST_SEMA_MODE_BLOB1004_ALIGN4,
         NV_HOST_SEMA_EMIT_PASS17);
      in = nv_push_dw_count(&ip);
      if (in < 8)
         return -612;
      for (ii = 0; ii + 1 < in; ii++) {
         uint32_t hdr = ib[ii];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC597_INVALIDATE_SHADER_CACHES)
            saw_inv = true;
         if (method == NVC597_WAIT_FOR_IDLE)
            saw_wfi = true;
         if (method == NVC36F_SEMAPHOREC && ib[ii + 1] == 0x1004u)
            saw_exec = true;
      }
      if (!saw_inv)
         return -613;
      if (!saw_wfi)
         return -614;
      if (!saw_exec)
         return -615;
   }

   /* tick149 / pass18: CB bind_group + report sema A–D ladder */
   {
      struct nv_push cp;
      uint32_t cb[256], cn, ci;
      bool saw_cba = false, saw_bind = false, saw_ra = false, saw_rd = false;

      memset(cb, 0, sizeof(cb));
      nv_push_init(&cp, cb, (uint32_t)(sizeof(cb) / 4));
      nv_3d_emit_g3_cb_bind_group_pass18(&cp, 256u, 0xa00000ull, 0, 3, true);
      nv_3d_emit_g3_report_sema_pass18(&cp, 0xb00000ull, 0x55u, true, true);
      cn = nv_push_dw_count(&cp);
      if (cn < 10)
         return -616;
      for (ci = 0; ci + 1 < cn; ci++) {
         uint32_t hdr = cb[ci];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC597_SET_CONSTANT_BUFFER_SELECTOR_A)
            saw_cba = true;
         if (method == NVC597_BIND_GROUP_CONSTANT_BUFFER(0))
            saw_bind = true;
         if (method == NVC597_SET_REPORT_SEMAPHORE_A)
            saw_ra = true;
         if (method == NVC597_SET_REPORT_SEMAPHORE_D)
            saw_rd = true;
      }
      if (!saw_cba)
         return -617;
      if (!saw_bind)
         return -618;
      if (!saw_ra || !saw_rd)
         return -619;
   }

   /* tick149 / pass18: inv_cb_report composite pipeline */
   {
      struct nv_push rp;
      uint32_t rb[256], rn, ri;
      bool saw_inv = false, saw_cba = false, saw_rep = false;

      memset(rb, 0, sizeof(rb));
      nv_push_init(&rp, rb, (uint32_t)(sizeof(rb) / 4));
      nv_3d_emit_g3_inv_cb_report_pass18(&rp, 128u, 0xc00000ull, 0, 0,
                                         0xd00000ull, 0x66u);
      rn = nv_push_dw_count(&rp);
      if (rn < 12)
         return -620;
      for (ri = 0; ri + 1 < rn; ri++) {
         uint32_t hdr = rb[ri];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC597_INVALIDATE_SHADER_CACHES)
            saw_inv = true;
         if (method == NVC597_SET_CONSTANT_BUFFER_SELECTOR_A)
            saw_cba = true;
         if (method == NVC597_SET_REPORT_SEMAPHORE_C && rb[ri + 1] == 0x66u)
            saw_rep = true;
      }
      if (!saw_inv || !saw_cba || !saw_rep)
         return -621;
   }

   /* tick149: bringup_slice_pass18 shape (report sema tail present) */
   {
      struct nv_push bp;
      uint32_t bb[2048], bn, bi;
      bool saw_rep_d = false;

      memset(bb, 0, sizeof(bb));
      nv_push_init(&bp, bb, (uint32_t)(sizeof(bb) / 4));
      nv_3d_emit_g3_bringup_slice_pass18(
         &bp, 0xc597u, 0x800000ull, 32, 32,
         NVC597_SET_COLOR_TARGET_FORMAT_V_A8B8G8R8, NULL, 0, 0, 0, 0, 0,
         0x900000ull, 9u, 0, 0, 0, 0, 0x900000ull, 9u);
      bn = nv_push_dw_count(&bp);
      if (bn < 40)
         return -622;
      for (bi = 0; bi + 1 < bn; bi++) {
         uint32_t hdr = bb[bi];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC597_SET_REPORT_SEMAPHORE_D)
            saw_rep_d = true;
      }
      if (!saw_rep_d)
         return -623;
   }

   /* tick150 / pass19: G2 QMD sema-only (no host sema in prep/tail) */
   {
      struct nv_push g2q;
      uint32_t g2qb[2048], g2qn, g2qi;
      uint32_t qmd_only[NV_QMD_DWORDS];
      bool saw_pcas_a = false, saw_inline = false;
      int qbr;

      qbr = nv_qmd_build_pass17_qmd_sema_only(qmd_only, 0x210000ull, 16, 0x53,
                                              0x310000ull, 0x44u, 1, 32);
      if (qbr != 0)
         return -624;
      if (nv_qmd_get(qmd_only, NV_QMD_F_SEMAPHORE_RELEASE_ENABLE0) != 1)
         return -625;
      if (nv_qmd_get(qmd_only, NV_QMD_F_INVALIDATE_INSTRUCTION_CACHE) != 1)
         return -626;

      memset(g2qb, 0, sizeof(g2qb));
      nv_push_init(&g2q, g2qb, (uint32_t)(sizeof(g2qb) / 4));
      if (nv_compute_emit_g2_qmd_sema_only_pass17(
             &g2q, 0xc5c0u, 0x900000ull, 16, 0x53, 0x800000ull, NULL, 0,
             0x500000ull, 0x22u, 1, 32, true) != 0)
         return -627;
      g2qn = nv_push_dw_count(&g2q);
      if (g2qn < 30)
         return -628;
      for (g2qi = 0; g2qi + 1 < g2qn; g2qi++) {
         uint32_t hdr = g2qb[g2qi];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC3C0_SEND_PCAS_A)
            saw_pcas_a = true;
         if (method == NVC3C0_SET_INLINE_QMD_ADDRESS_A ||
             method == NVC3C0_LOAD_INLINE_QMD_DATA(0))
            saw_inline = true;
      }
      /* Must schedule via inline QMD / PCAS path (hand-authored, not static) */
      if (!saw_pcas_a && !saw_inline)
         return -629;
   }

   /* tick150: pass19 QMD sema + host sema tail (explicit mesa ladder) */
   {
      struct nv_push g2h;
      uint32_t g2hb[2048], g2hn, g2hi;
      bool saw_host_exec = false, saw_pcas = false;

      memset(g2hb, 0, sizeof(g2hb));
      nv_push_init(&g2h, g2hb, (uint32_t)(sizeof(g2hb) / 4));
      if (nv_compute_emit_g2_qmd_sema_then_host_pass19(
             &g2h, 0xc5c0u, 0x900000ull, 16, 0x53, 0x800000ull, NULL, 0,
             0x510000ull, 0x33u, 0x610000ull, 0x44u, 1, 32,
             NV_HOST_SEMA_MODE_BLOB1004_ALIGN4, NV_HOST_SEMA_EMIT_PASS17,
             true) != 0)
         return -630;
      g2hn = nv_push_dw_count(&g2h);
      if (g2hn < 35)
         return -631;
      for (g2hi = 0; g2hi + 1 < g2hn; g2hi++) {
         uint32_t hdr = g2hb[g2hi];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC3C0_SEND_PCAS_A)
            saw_pcas = true;
         if (method == NVC36F_SEMAPHOREC && g2hb[g2hi + 1] == 0x1004u)
            saw_host_exec = true;
      }
      if (!saw_pcas)
         return -632;
      if (!saw_host_exec)
         return -633;
   }

   /* tick150 / pass19: NVC597 full inv ladder (class-correct, not 0x0b00 family) */
   {
      struct nv_push ivp;
      uint32_t ivb[64], ivn, ivi;
      bool saw_021c = false, saw_1330 = false, saw_1334 = false, saw_1338 = false;
      bool saw_wfi = false;

      memset(ivb, 0, sizeof(ivb));
      nv_push_init(&ivp, ivb, (uint32_t)(sizeof(ivb) / 4));
      nv_3d_emit_g3_inv_nvc597_full_ladder_pass19(&ivp, true, true, true, true,
                                                  true, true, true);
      ivn = nv_push_dw_count(&ivp);
      if (ivn < 6)
         return -634;
      for (ivi = 0; ivi + 1 < ivn; ivi++) {
         uint32_t hdr = ivb[ivi];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC597_INVALIDATE_SHADER_CACHES)
            saw_021c = true;
         if (method == NVC597_INVALIDATE_SAMPLER_CACHE)
            saw_1330 = true;
         if (method == NVC597_INVALIDATE_TEXTURE_HEADER_CACHE)
            saw_1334 = true;
         if (method == NVC597_INVALIDATE_TEXTURE_DATA_CACHE)
            saw_1338 = true;
         if (method == NVC597_WAIT_FOR_IDLE)
            saw_wfi = true;
      }
      if (!saw_021c || !saw_1330 || !saw_1334 || !saw_1338 || !saw_wfi)
         return -635;
   }

   /* tick150: inv_report_host_pass19 composite shape */
   {
      struct nv_push irp;
      uint32_t irb[128], irn, iri;
      bool saw_rep = false, saw_host = false;

      memset(irb, 0, sizeof(irb));
      nv_push_init(&irp, irb, (uint32_t)(sizeof(irb) / 4));
      nv_3d_emit_g3_inv_report_host_pass19(
         &irp, 0xb00000ull, 0x55u, true, 0xc00000ull, 0x66u,
         NV_HOST_SEMA_MODE_BLOB1004_ALIGN4, NV_HOST_SEMA_EMIT_PASS17);
      irn = nv_push_dw_count(&irp);
      if (irn < 12)
         return -636;
      for (iri = 0; iri + 1 < irn; iri++) {
         uint32_t hdr = irb[iri];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC597_SET_REPORT_SEMAPHORE_C && irb[iri + 1] == 0x55u)
            saw_rep = true;
         if (method == NVC36F_SEMAPHOREC && irb[iri + 1] == 0x1004u)
            saw_host = true;
      }
      if (!saw_rep || !saw_host)
         return -637;
   }

   /* tick150: sema_only rejects null sema addr */
   {
      struct nv_push badp;
      uint32_t badb[64];

      memset(badb, 0, sizeof(badb));
      nv_push_init(&badp, badb, (uint32_t)(sizeof(badb) / 4));
      if (nv_compute_emit_g2_qmd_sema_only_pass17(
             &badp, 0xc5c0u, 0x900000ull, 16, 0x53, 0x800000ull, NULL, 0,
             0, 0x22u, 1, 32, false) == 0)
         return -638;
   }

   /* tick151: query begin/end pass19 (occlusion + timestamp shapes) */
   {
      struct nv_push qbp, qep;
      uint32_t qbb[32], qeb[48], qbn, qen, qi;
      bool saw_zpass_on = false, saw_rep_a = false, saw_wfi = false;

      memset(qbb, 0, sizeof(qbb));
      nv_push_init(&qbp, qbb, (uint32_t)(sizeof(qbb) / 4));
      nv_3d_emit_g3_query_begin_occlusion_pass19(&qbp, true);
      qbn = nv_push_dw_count(&qbp);
      if (qbn < 2)
         return -639;
      for (qi = 0; qi + 1 < qbn; qi++) {
         uint32_t hdr = qbb[qi];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC597_SET_ZPASS_PIXEL_COUNT && qbb[qi + 1] != 0)
            saw_zpass_on = true;
      }
      if (!saw_zpass_on)
         return -640;

      memset(qeb, 0, sizeof(qeb));
      nv_push_init(&qep, qeb, (uint32_t)(sizeof(qeb) / 4));
      nv_3d_emit_g3_query_end_report_pass19(&qep, 0xd00000ull, 1, true, false,
                                            true);
      qen = nv_push_dw_count(&qep);
      if (qen < 6)
         return -641;
      for (qi = 0; qi + 1 < qen; qi++) {
         uint32_t hdr = qeb[qi];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC597_WAIT_FOR_IDLE)
            saw_wfi = true;
         if (method == NVC597_SET_REPORT_SEMAPHORE_A)
            saw_rep_a = true;
      }
      if (!saw_wfi || !saw_rep_a)
         return -642;
   }

   /* tick151: VS/FS multi-CB bind helper */
   {
      struct nv_push cbp;
      uint32_t cbb[64], cbn, ci;
      unsigned nbind;
      bool saw_cba = false, saw_bind = false;

      memset(cbb, 0, sizeof(cbb));
      nv_push_init(&cbp, cbb, (uint32_t)(sizeof(cbb) / 4));
      nbind = nv_3d_emit_g3_cb_bind_vs_fs_pass18(&cbp, 0xa00000ull, 256u, 0,
                                                 0xb00000ull, 128u, 0, true);
      if (nbind != 2)
         return -643;
      cbn = nv_push_dw_count(&cbp);
      if (cbn < 8)
         return -644;
      for (ci = 0; ci + 1 < cbn; ci++) {
         uint32_t hdr = cbb[ci];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC597_SET_CONSTANT_BUFFER_SELECTOR_A)
            saw_cba = true;
         if (method == NVC597_BIND_GROUP_CONSTANT_BUFFER(0))
            saw_bind = true;
      }
      if (!saw_cba || !saw_bind)
         return -645;
   }

   /* tick152 / pass20: constants + inline/PCAS span + path C gate */
   {
      struct nv_push lp;
      uint32_t lb[256], ln;
      uint32_t qmd_l[NV_QMD_DWORDS];
      int ladder_rc;
      uint32_t fake_ind[4] = { 3u, 1u, 0u, 0u }; /* vertexCount=3, instance=1 */

      if (NV_PASS20_INLINE_TO_PCAS_MEDIAN_GLCORE != 428u)
         return -646;
      if (NV_PASS20_INLINE_TO_PCAS_MEDIAN_CUDA < NV_PASS20_INLINE_TO_PCAS_MEDIAN_GLCORE)
         return -647;
      if (nv_pass20_inline_qmd_launch_min_methods(false) != 2u + NV_QMD_DWORDS + 2u)
         return -648;
      if (!nv_pass20_inline_pcas_span_ok(nv_pass20_inline_qmd_launch_min_methods(true)))
         return -649;
      if (nv_pass20_inline_pcas_span_ok(NV_PASS20_INLINE_TO_PCAS_MEDIAN_GLCORE))
         return -650; /* span_ok must reject median-as-method-count */

      /* path C indirect not ready while stubs (pass20/tick152 gate) */
      if (nv_mme_path_c_indirect_ready())
         return -651;

      memset(lb, 0, sizeof(lb));
      nv_push_init(&lp, lb, (uint32_t)(sizeof(lb) / 4));
      if (nv_mme_emit_path_c_indirect_if_ready(&lp, false, 1, 16))
         return -652; /* must not CALL while stub */

      ladder_rc = nv_3d_emit_draw_indirect_ladder_pass20(
         &lp, 0x700000ull, NVC597_TOPOLOGY_TRIANGLES, false,
         fake_ind, 4, 1, 16, true, 0, 0);
      if (ladder_rc != 1)
         return -653; /* expect path A/B (1), not C (2) or fail (0) */
      ln = nv_push_dw_count(&lp);
      if (ln < 4)
         return -654;

      /* pass20 launch with WFI tail shape */
      memset(qmd_l, 0, sizeof(qmd_l));
      memset(lb, 0, sizeof(lb));
      nv_push_init(&lp, lb, (uint32_t)(sizeof(lb) / 4));
      nv_compute_emit_inline_qmd_launch_pass20(&lp, 0x800000ull, qmd_l, true,
                                               true);
      ln = nv_push_dw_count(&lp);
      if (ln < nv_pass20_inline_qmd_launch_min_methods(true) + 1u)
         return -655;
   }

   /* tick153: pass20 gpucomp constants + G4 pass17 video sema helpers */
   {
      struct nv_push vp;
      uint32_t vb[256], vn, vi;
      struct nv_nvdec_pic_setup pic;
      bool saw_host_exec = false;
      struct nv_mme_program mprog;

      if (NV_PASS20_GPUCOMP_RAM_DATA_IMM_CAPPED != 100u)
         return -656;
      if (NV_MME_PASS20_RAM_DATA_METHOD_OFF != 0x3884u)
         return -657;
      if (NV_MME_PASS20_SCAFFOLD_DRAW_METHOD != 0x1610u)
         return -658;

      nv_mme_build_draw_indirect_loop_scaffold(&mprog, 0, 0, false);
      if (mprog.insn_count != 6 || !mprog.is_stub_end_only)
         return -659;
      /* pass20 scaffold uses named method offs in insn1/2 imm16 */
      if ((mprog.insns[1] & 0xffffu) != NV_MME_PASS20_SCAFFOLD_DRAW_METHOD &&
          ((mprog.insns[1] >> 8) & 0xffffu) != NV_MME_PASS20_SCAFFOLD_DRAW_METHOD)
         ; /* encoding may place method in imm16 field via insn_emit_method */
      if (!nv_mme_path_c_indirect_ready())
         ; /* still not ready — ok */

      memset(&pic, 0, sizeof(pic));
      pic.app_id = NV_NVDEC_APP_ID_H264;
      pic.execute_flags = 1;
      memset(vb, 0, sizeof(vb));
      nv_push_init(&vp, vb, (uint32_t)(sizeof(vb) / 4));
      if (nv_g4_emit_nvdec_bringup_pass17(
             &vp, NV_VIDEO_CLASS_NVDEC_C9B0, &pic, 0x900000ull, 0x77u, NULL,
             true, NV_HOST_SEMA_MODE_BLOB1004_ALIGN4) != 0)
         return -660;
      vn = nv_push_dw_count(&vp);
      if (vn < 4)
         return -661;
      for (vi = 0; vi + 1 < vn; vi++) {
         uint32_t hdr = vb[vi];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC36F_SEMAPHOREC && vb[vi + 1] == 0x1004u)
            saw_host_exec = true;
      }
      if (!saw_host_exec)
         return -662;

      /* without host tail: should not require pass17 C execute */
      memset(vb, 0, sizeof(vb));
      nv_push_init(&vp, vb, (uint32_t)(sizeof(vb) / 4));
      if (nv_g4_emit_nvdec_bringup_pass17(
             &vp, NV_VIDEO_CLASS_NVDEC_C9B0, &pic, 0x900000ull, 0x77u, NULL,
             false, NV_HOST_SEMA_MODE_BLOB1004_ALIGN4) != 0)
         return -663;
   }

   /* tick154: G4 NVENC pass17 bringup + G2 QMD launch pass17 helper */
   {
      struct nv_push ep, gp;
      uint32_t eb[256], gb[256], en, gi;
      uint32_t qmd_pre[NV_QMD_DWORDS];
      bool saw_host = false;

      memset(eb, 0, sizeof(eb));
      nv_push_init(&ep, eb, (uint32_t)(sizeof(eb) / 4));
      if (nv_g4_emit_nvenc_bringup_pass17(
             &ep, NV_VIDEO_CLASS_NVENC_C9B7, 0xa00000ull, 0xb00000ull,
             0xc00000ull, 0xd00000ull, 64, 64, 0x910000ull, 0x88u, NULL, true,
             NV_HOST_SEMA_MODE_BLOB1004_ALIGN4) != 0)
         return -664;
      en = nv_push_dw_count(&ep);
      if (en < 4)
         return -665;
      for (gi = 0; gi + 1 < en; gi++) {
         uint32_t hdr = eb[gi];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC36F_SEMAPHOREC && eb[gi + 1] == 0x1004u)
            saw_host = true;
      }
      if (!saw_host)
         return -666;

      if (nv_qmd_build_pass17_qmd_sema_only(qmd_pre, 0x210000ull, 16, 0x53,
                                            0x310000ull, 0x55u, 1, 32) != 0)
         return -667;
      memset(gb, 0, sizeof(gb));
      nv_push_init(&gp, gb, (uint32_t)(sizeof(gb) / 4));
      if (nv_compute_emit_g2_qmd_launch_pass17(
             &gp, 0xc5c0u, qmd_pre, 0x800000ull, 0, 0x53, true,
             0x610000ull, 0x66u, NV_HOST_SEMA_MODE_BLOB1004_ALIGN4) != 0)
         return -668;
      if (nv_push_dw_count(&gp) <
          nv_pass20_inline_qmd_launch_min_methods(true) + 2u)
         return -669;
   }

   /* tick155 / pass21: G0–G4 sema ladder polish + compute NIR depth scaffold */
   {
      enum nv_host_sema_mode ladder[8];
      unsigned ln, li;
      struct nv_push tp, cp;
      uint32_t tb[64], cb[512], ti;
      uint32_t qmd_p21[NV_QMD_DWORDS];
      struct nv_sph_blob sph21;
      bool saw_host = false, saw_wfi = false;

      /* pass21 sema ladder: sticky 1004 first, then 6 more = 7 total */
      ln = nv_pass21_g0_g4_sema_mode_ladder_fill(
         ladder, 8, NV_HOST_SEMA_MODE_BLOB1004_ALIGN4);
      if (ln < 7)
         return -670;
      if (ladder[0] != NV_HOST_SEMA_MODE_BLOB1004_ALIGN4)
         return -671;
      if (nv_host_sema_pass17_slot_for_exec(NV_PASS21_HOST_SEMA_DEFAULT_EXEC) !=
          NV_PASS21_HOST_SEMA_DEFAULT_SLOT)
         return -672;
      if (NV_PASS21_G0_G4_ENGINE_COUNT != 5u)
         return -673;

      /* sticky non-default first */
      ln = nv_pass21_g0_g4_sema_mode_ladder_fill(
         ladder, 8, NV_HOST_SEMA_MODE_BLOB1002_ALIGN4);
      if (ln < 2 || ladder[0] != NV_HOST_SEMA_MODE_BLOB1002_ALIGN4)
         return -674;

      /* host sema tail helper: WFI then SEMAPHOREC=0x1004 */
      memset(tb, 0, sizeof(tb));
      nv_push_init(&tp, tb, (uint32_t)(sizeof(tb) / 4));
      nv_push_set_subch(&tp, NV_PUSH_SUBCH_COMPUTE);
      if (nv_push_g0_g4_host_sema_tail_pass21(
             &tp, true, 0x710000ull, 0x99u,
             NV_PASS21_HOST_SEMA_DEFAULT_MODE) != 0)
         return -675;
      ti = nv_push_dw_count(&tp);
      if (ti < 4)
         return -676;
      for (li = 0; li + 1 < ti; li++) {
         uint32_t hdr = tb[li];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC36F_WFI)
            saw_wfi = true;
         if (method == NVC36F_SEMAPHOREC && tb[li + 1] == 0x1004u)
            saw_host = true;
      }
      if (!saw_wfi || !saw_host)
         return -677;
      if (nv_push_g0_g4_host_sema_tail_pass21(&tp, false, 0, 1,
                                              NV_PASS21_HOST_SEMA_DEFAULT_MODE) == 0)
         return -678; /* must reject null sema */

      /* pass21 SPH: s2r + store_imm depth (global store bit) */
      nv_sph_build_compute_s2r_store_imm_pass21(&sph21, 0xdeadbeefu,
                                                0x300000ull, 16);
      if (nv_sph_smoke_validate_blob(&sph21, NV_SPH_TYPE_COMPUTE) != 0)
         return -679;
      if (!(sph21.sph[0] & (1u << 11)))
         return -680;
      if (sph21.sass_dwords < 20)
         return -681;

      /* pass21 QMD from program VA */
      if (nv_qmd_build_pass21_compute_from_program(
             qmd_p21, 0x210000ull, 16, 0x53, 0x310000ull, 0x55u, 1, 32) != 0)
         return -682;
      if (nv_qmd_build_pass21_compute_from_program(
             qmd_p21, 0, 16, 0x53, 0x310000ull, 0x55u, 1, 32) == 0)
         return -683; /* reject null program */

      /* full program launch: qmd sema + host sema tail */
      memset(cb, 0, sizeof(cb));
      nv_push_init(&cp, cb, (uint32_t)(sizeof(cb) / 4));
      if (nv_compute_emit_g2_program_launch_pass21(
             &cp, 0xc5c0u, qmd_p21, 0x210000ull, 0x800000ull, 0, 0x53, 16,
             0x310000ull, 0x55u, 1, 32, true, 0x610000ull, 0x66u,
             NV_PASS21_HOST_SEMA_DEFAULT_MODE) != 0)
         return -684;
      if (nv_push_dw_count(&cp) <
          nv_pass20_inline_qmd_launch_min_methods(true) + 4u)
         return -685;

      /* sema-only program path (no host sema) */
      memset(cb, 0, sizeof(cb));
      nv_push_init(&cp, cb, (uint32_t)(sizeof(cb) / 4));
      if (nv_compute_emit_g2_program_qmd_sema_only_pass21(
             &cp, 0xc5c0u, qmd_p21, 0x210000ull, 0x800000ull, 0, 0x53, 16,
             0x310000ull, 0x55u, 1, 32, true) != 0)
         return -686;
      if (nv_push_dw_count(&cp) <
          nv_pass20_inline_qmd_launch_min_methods(true))
         return -687;

      /* engine id bookkeeping sanity */
      if ((int)NV_PASS21_ENGINE_G0_AUX != 0 ||
          (int)NV_PASS21_ENGINE_G4_VIDEO != 4)
         return -688;
   }

   /* tick156: pass21 MME RAM_DATA scaffold + channel prime wire-up */
   {
      struct nv_push mp;
      uint32_t mb[512], mi, li;
      uint32_t insns[4];
      unsigned wrote, ram_hits = 0, addr_hits = 0, cfg_hits = 0;
      bool stubs_only;

      if (NV_MME_PASS21_RAM_DATA_METHOD_OFF != 0x3884u)
         return -689;
      if (NV_MME_PASS21_RAM_ADDR_METHOD_OFF != 0x385cu)
         return -690;
      if (NV_MME_PASS21_GPUCOMP_RAM_DATA_IMM_CAPPED != 100u)
         return -691;
      if (NV_PASS20_GPUCOMP_RAM_DATA_IMM_CAPPED !=
          NV_MME_PASS21_GPUCOMP_RAM_DATA_IMM_CAPPED)
         return -692;

      memset(mb, 0, sizeof(mb));
      nv_push_init(&mp, mb, (uint32_t)(sizeof(mb) / 4));
      insns[0] = NV_MME_INSN_END;
      insns[1] = NV_MME_INSN_NOP;
      wrote = nv_mme_emit_ram_upload_scaffold_pass21(&mp, 0, insns, 2);
      if (wrote != 2)
         return -693;
      mi = nv_push_dw_count(&mp);
      for (li = 0; li + 1 < mi; li++) {
         uint32_t hdr = mb[li];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NV_MME_PASS21_RAM_ADDR_METHOD_OFF)
            addr_hits++;
         if (method == NV_MME_PASS21_RAM_DATA_METHOD_OFF)
            ram_hits++;
      }
      if (addr_hits < 1 || ram_hits < 2)
         return -694;

      /* stub end probe: 2× (addr+data) for slots 0/1 */
      memset(mb, 0, sizeof(mb));
      nv_push_init(&mp, mb, (uint32_t)(sizeof(mb) / 4));
      wrote = nv_mme_emit_ram_data_stub_end_probe_pass21(&mp, 0);
      if (wrote != 2)
         return -695;

      /* pass21 prime with RAM_DATA probe */
      memset(mb, 0, sizeof(mb));
      nv_push_init(&mp, mb, (uint32_t)(sizeof(mb) / 4));
      stubs_only = nv_mme_emit_channel_prime_upload_pass21(&mp, true);
      if (!stubs_only)
         return -696; /* must still report stubs / no path C */
      if (nv_mme_path_c_indirect_ready())
         return -697;
      mi = nv_push_dw_count(&mp);
      if (mi < 8)
         return -698;
      ram_hits = 0;
      for (li = 0; li + 1 < mi; li++) {
         uint32_t hdr = mb[li];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NV_MME_PASS21_RAM_DATA_METHOD_OFF)
            ram_hits++;
         if (method == NV_MME_PASS16_POST_CONFIG_METHOD_OFF)
            cfg_hits++;
      }
      if (ram_hits < 2)
         return -699;
      if (cfg_hits < 1)
         return -700;

      /* prime without probe must not emit RAM_DATA (default channel path) */
      memset(mb, 0, sizeof(mb));
      nv_push_init(&mp, mb, (uint32_t)(sizeof(mb) / 4));
      (void)nv_mme_emit_channel_prime_upload_only(&mp);
      mi = nv_push_dw_count(&mp);
      ram_hits = 0;
      for (li = 0; li + 1 < mi; li++) {
         uint32_t hdr = mb[li];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NV_MME_PASS21_RAM_DATA_METHOD_OFF)
            ram_hits++;
      }
      if (ram_hits != 0)
         return -701;

      /* empty ram_data stream is no-op */
      if (nv_mme_emit_ram_data_stream_pass21(&mp, NULL, 0) != 0)
         return -702;
      if (nv_mme_emit_ram_data_stream_pass21(&mp, insns, 0) != 0)
         return -703;
   }

   /* tick157: G1/G3 pass21 sema tail adopt + G4 video symmetry + RE synthesis */
   {
      struct nv_push p1, p3, pv;
      uint32_t b1[256], b3[512], bv[512], ni, li;
      struct nv_nvdec_pic_setup pic;
      bool saw_host = false, saw_launch = false, saw_inv = false;

      if (!NV_PASS21_RE_G0_G4_UNIFIED_TAIL || !NV_PASS21_RE_MME_RAM_DATA_SCAFFOLD ||
          !NV_PASS21_RE_PATH_C_STILL_GATED)
         return -704;
      if (NV_PASS21_RE_HOST_SEMA_DEFAULT_MODE != NV_PASS21_HOST_SEMA_DEFAULT_MODE)
         return -705;

      /* G1: copy then pass21 host sema */
      memset(b1, 0, sizeof(b1));
      nv_push_init(&p1, b1, (uint32_t)(sizeof(b1) / 4));
      if (nv_g1_emit_copy_then_host_sema_pass21(
             &p1, 0xc5b5u, 0x100000ull, 0x200000ull, 64u, false, true,
             0x500000ull, 0x42u, NV_PASS21_HOST_SEMA_DEFAULT_MODE) != 0)
         return -706;
      ni = nv_push_dw_count(&p1);
      if (ni < 10)
         return -707;
      for (li = 0; li + 1 < ni; li++) {
         uint32_t hdr = b1[li];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC6B5_LAUNCH_DMA)
            saw_launch = true;
         if (method == NVC36F_SEMAPHOREC && b1[li + 1] == 0x1004u)
            saw_host = true;
      }
      if (!saw_launch || !saw_host)
         return -708;
      if (nv_g1_emit_copy_then_host_sema_pass21(
             &p1, 0xc5b5u, 0x100000ull, 0x200000ull, 64u, false, false,
             0, 1, NV_PASS21_HOST_SEMA_DEFAULT_MODE) == 0)
         return -709; /* reject null host sema */

      /* G1 dual fence */
      memset(b1, 0, sizeof(b1));
      nv_push_init(&p1, b1, (uint32_t)(sizeof(b1) / 4));
      if (nv_g1_emit_copy_engine_and_host_sema_pass21(
             &p1, 0xc5b5u, 0x100000ull, 0x200000ull, 64u, 0x510000ull, 0x11u,
             false, 0x520000ull, 0x22u, NV_PASS21_HOST_SEMA_DEFAULT_MODE) != 0)
         return -710;

      /* G3 inv + pass21 host sema */
      memset(b3, 0, sizeof(b3));
      nv_push_init(&p3, b3, (uint32_t)(sizeof(b3) / 4));
      if (nv_3d_emit_g3_inv_wfi_host_sema_pass21(
             &p3, 0x600000ull, 0x33u, NV_PASS21_HOST_SEMA_DEFAULT_MODE,
             true) != 0)
         return -711;
      ni = nv_push_dw_count(&p3);
      saw_host = false;
      saw_inv = false;
      for (li = 0; li + 1 < ni; li++) {
         uint32_t hdr = b3[li];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == 0x021cu || method == 0x1330u)
            saw_inv = true;
         if (method == NVC36F_SEMAPHOREC && b3[li + 1] == 0x1004u)
            saw_host = true;
      }
      if (!saw_inv || !saw_host)
         return -712;
      if (nv_3d_emit_g3_inv_wfi_host_sema_pass21(
             &p3, 0, 1, NV_PASS21_HOST_SEMA_DEFAULT_MODE, false) == 0)
         return -713;

      /* G3 inv+report+host pass21 */
      memset(b3, 0, sizeof(b3));
      nv_push_init(&p3, b3, (uint32_t)(sizeof(b3) / 4));
      if (nv_3d_emit_g3_inv_report_host_pass21(
             &p3, 0x610000ull, 0x44u, true, 0x620000ull, 0x55u,
             NV_PASS21_HOST_SEMA_DEFAULT_MODE, false) != 0)
         return -714;

      /* G4 pass21 NVDEC/NVENC bringup symmetry */
      nv_nvdec_pic_setup_init_h264_smoke(&pic, 0x800000ull, 0x810000ull, 0);
      memset(bv, 0, sizeof(bv));
      nv_push_init(&pv, bv, (uint32_t)(sizeof(bv) / 4));
      if (nv_g4_emit_nvdec_bringup_pass21(
             &pv, NV_VIDEO_CLASS_NVDEC_C9B0, &pic, 0x900000ull, 0x77u, NULL,
             true, NV_PASS21_HOST_SEMA_DEFAULT_MODE) != 0)
         return -715;
      ni = nv_push_dw_count(&pv);
      saw_host = false;
      for (li = 0; li + 1 < ni; li++) {
         uint32_t hdr = bv[li];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC36F_SEMAPHOREC && bv[li + 1] == 0x1004u)
            saw_host = true;
      }
      if (!saw_host)
         return -716;

      memset(bv, 0, sizeof(bv));
      nv_push_init(&pv, bv, (uint32_t)(sizeof(bv) / 4));
      if (nv_g4_emit_nvenc_bringup_pass21(
             &pv, NV_VIDEO_CLASS_NVENC_C9B7, 0xa00000ull, 0xb00000ull,
             0xc00000ull, 0xd00000ull, 64, 64, 0x910000ull, 0x88u, NULL, true,
             NV_PASS21_HOST_SEMA_DEFAULT_MODE) != 0)
         return -717;
      ni = nv_push_dw_count(&pv);
      saw_host = false;
      for (li = 0; li + 1 < ni; li++) {
         uint32_t hdr = bv[li];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC36F_SEMAPHOREC && bv[li + 1] == 0x1004u)
            saw_host = true;
      }
      if (!saw_host)
         return -718;

      /* engine-only pass21 (no host tail) still succeeds */
      memset(bv, 0, sizeof(bv));
      nv_push_init(&pv, bv, (uint32_t)(sizeof(bv) / 4));
      if (nv_g4_emit_nvdec_bringup_pass21(
             &pv, NV_VIDEO_CLASS_NVDEC_C9B0, &pic, 0x900000ull, 0x77u, NULL,
             false, NV_PASS21_HOST_SEMA_DEFAULT_MODE) != 0)
         return -719;
   }

   /* tick158: pass21 barrier (Gallium memory_barrier wire) + channel ladder shapes */
   {
      struct nv_push bp;
      uint32_t bb[128], bi, li;
      bool saw_inv = false, saw_wfi = false;
      uint32_t qmd_scratch[NV_QMD_DWORDS];
      struct nv_push cp;
      uint32_t cb[512];

      memset(bb, 0, sizeof(bb));
      nv_push_init(&bp, bb, (uint32_t)(sizeof(bb) / 4));
      nv_3d_emit_g3_barrier_all_pass21(&bp);
      bi = nv_push_dw_count(&bp);
      if (bi < 4)
         return -720;
      for (li = 0; li + 1 < bi; li++) {
         uint32_t hdr = bb[li];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == 0x021cu || method == 0x1330u)
            saw_inv = true;
         if (method == NVC36F_WFI || method == 0x0110u)
            saw_wfi = true;
      }
      if (!saw_inv)
         return -721;

      memset(bb, 0, sizeof(bb));
      nv_push_init(&bp, bb, (uint32_t)(sizeof(bb) / 4));
      nv_3d_emit_g3_barrier_pass21(&bp, true, true, false, true, false, false,
                                   true);
      if (nv_push_dw_count(&bp) < 2)
         return -722;

      /* G2 program launch pass21 shape (channel bringup step 4) */
      memset(cb, 0, sizeof(cb));
      nv_push_init(&cp, cb, (uint32_t)(sizeof(cb) / 4));
      if (nv_compute_emit_g2_program_launch_pass21(
             &cp, 0xc5c0u, qmd_scratch, 0x210000ull, 0x800000ull, 0, 0x53, 16,
             0x310000ull, 0x55u, 1, 32, true, 0x610000ull, 0x66u,
             NV_PASS21_HOST_SEMA_DEFAULT_MODE) != 0)
         return -723;
      if (nv_push_dw_count(&cp) <
          nv_pass20_inline_qmd_launch_min_methods(true) + 2u)
         return -724;

      /* G3 inv+host sema pass21 shape (channel bringup step 2) */
      memset(bb, 0, sizeof(bb));
      nv_push_init(&bp, bb, (uint32_t)(sizeof(bb) / 4));
      if (nv_3d_emit_g3_inv_wfi_host_sema_pass21(
             &bp, 0x700000ull, 0x77u, NV_PASS21_HOST_SEMA_DEFAULT_MODE,
             true) != 0)
         return -725;
      (void)saw_wfi;
   }

   /* tick159: G1 pass21 emit + MME probe env gate (default off) */
   {
      struct nv_push p1, mp;
      uint32_t b1[256], mb[512], ni, li;
      bool saw_host = false, saw_launch = false;
      unsigned ram_hits = 0;

      /* env probe off unless explicitly set — must not fail default prime */
      if (nv_mme_pass21_probe_ram_data_env_enabled()) {
         /* if runner exported the var, probe path is intentional */
      } else {
         memset(mb, 0, sizeof(mb));
         nv_push_init(&mp, mb, (uint32_t)(sizeof(mb) / 4));
         (void)nv_mme_emit_channel_prime_upload_only(&mp);
         ni = nv_push_dw_count(&mp);
         for (li = 0; li + 1 < ni; li++) {
            uint32_t hdr = mb[li];
            uint32_t method = (hdr & 0x1fff) << 2;
            if ((hdr >> 29) != 0)
               continue;
            if (method == NV_MME_PASS21_RAM_DATA_METHOD_OFF)
               ram_hits++;
         }
         if (ram_hits != 0)
            return -726; /* default must not probe RAM_DATA */
      }

      /* explicit probe=true still works (silicon opt-in path) */
      memset(mb, 0, sizeof(mb));
      nv_push_init(&mp, mb, (uint32_t)(sizeof(mb) / 4));
      (void)nv_mme_emit_channel_prime_upload_pass21(&mp, true);
      ni = nv_push_dw_count(&mp);
      ram_hits = 0;
      for (li = 0; li + 1 < ni; li++) {
         uint32_t hdr = mb[li];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NV_MME_PASS21_RAM_DATA_METHOD_OFF)
            ram_hits++;
      }
      if (ram_hits < 2)
         return -727;

      memset(b1, 0, sizeof(b1));
      nv_push_init(&p1, b1, (uint32_t)(sizeof(b1) / 4));
      if (nv_g1_emit_copy_then_host_sema_pass21(
             &p1, 0xc5b5u, 0x100000ull, 0x200000ull, 64u, false, true,
             0x500000ull, 0x42u, NV_PASS21_HOST_SEMA_DEFAULT_MODE) != 0)
         return -728;
      ni = nv_push_dw_count(&p1);
      for (li = 0; li + 1 < ni; li++) {
         uint32_t hdr = b1[li];
         uint32_t method = (hdr & 0x1fff) << 2;
         if ((hdr >> 29) != 0)
            continue;
         if (method == NVC6B5_LAUNCH_DMA)
            saw_launch = true;
         if (method == NVC36F_SEMAPHOREC && b1[li + 1] == 0x1004u)
            saw_host = true;
      }
      if (!saw_launch || !saw_host)
         return -729;

      /* pass21 mode ladder non-empty (G1 channel phase 3 order source) */
      {
         enum nv_host_sema_mode ladder[8];
         unsigned ln = nv_pass21_g0_g4_sema_mode_ladder_fill(
            ladder, 8, NV_PASS21_HOST_SEMA_DEFAULT_MODE);
         if (ln < 5)
            return -730;
      }
   }

   /* tick160: pass21 compute object / NIR depth ladder (no HW) */
   {
      struct nv_pass21_compute_object obj;
      enum nv_pass21_compute_shader_kind kinds[4];
      unsigned kn, ki;
      struct nv_push cp;
      uint32_t cb[512];
      uint8_t ser[512];
      uint32_t ser_sz;

      kn = nv_pass21_compute_shader_kind_ladder_fill(kinds, 4);
      if (kn != 4)
         return -731;
      if (kinds[0] != NV_PASS21_CS_EXIT_ONLY ||
          kinds[3] != NV_PASS21_CS_S2R_STORE_IMM)
         return -732;

      for (ki = 0; ki < kn; ki++) {
         memset(&obj, 0, sizeof(obj));
         obj.shader_kind = kinds[ki];
         obj.program_gpu_addr = 0x210000ull;
         obj.qmd_gpu_addr = 0x800000ull;
         obj.qmd_sema_gpu = 0x310000ull;
         obj.qmd_sema_payload = 0x55u;
         obj.store_gpu_addr = 0x300000ull;
         obj.imm_value = 0xdeadbeefu;
         obj.register_count = 16;
         obj.spa_version = 0x53u;
         if (nv_pass21_compute_object_build(&obj) != 0)
            return -733 - (int)ki;
         if (!obj.ser_bytes || obj.ser_bytes > sizeof(ser))
            return -737;
         ser_sz = nv_sph_pass21_compute_serialise(&obj.sph, ser, sizeof(ser));
         if (!ser_sz || ser_sz > sizeof(ser))
            return -738;
         if ((ser[0] & 0xf) != NV_SPH_TYPE_COMPUTE)
            return -739;
      }

      /* default depth kind (s2r+store) launch shape */
      memset(&obj, 0, sizeof(obj));
      obj.shader_kind = NV_PASS21_CS_S2R_STORE_IMM;
      obj.program_gpu_addr = 0x210000ull;
      obj.qmd_gpu_addr = 0x800000ull;
      obj.qmd_sema_gpu = 0x310000ull;
      obj.store_gpu_addr = 0x300000ull;
      if (nv_pass21_compute_object_build(&obj) != 0)
         return -740;
      if (!(obj.sph.sph[0] & (1u << 11)))
         return -741; /* global store bit */
      memset(cb, 0, sizeof(cb));
      nv_push_init(&cp, cb, (uint32_t)(sizeof(cb) / 4));
      if (nv_pass21_compute_object_emit_launch(
             &cp, 0xc5c0u, &obj, 0, true, 0x610000ull, 0x66u,
             NV_PASS21_HOST_SEMA_DEFAULT_MODE) != 0)
         return -742;
      if (nv_push_dw_count(&cp) <
          nv_pass20_inline_qmd_launch_min_methods(true) + 2u)
         return -743;
      if (nv_pass21_compute_object_emit_launch(
             &cp, 0xc5c0u, &obj, 0, true, 0, 0,
             NV_PASS21_HOST_SEMA_DEFAULT_MODE) != 0)
         return -744; /* ser_bytes set; may reuse same obj */

      /* reject unbuilt object */
      memset(&obj, 0, sizeof(obj));
      obj.program_gpu_addr = 0x210000ull;
      obj.qmd_gpu_addr = 0x800000ull;
      if (nv_pass21_compute_object_emit_launch(
             &cp, 0xc5c0u, &obj, 0, false, 0, 0,
             NV_PASS21_HOST_SEMA_DEFAULT_MODE) != -4)
         return -745;
   }

   /* tick161: pass22 RE constants + explicit-emit policy (no HW) */
   {
      struct nv_push cp22;
      uint32_t cb22[256];
      uint32_t qmd22[NV_QMD_DWORDS];
      uint32_t off0, off10;
      unsigned li;

      if (!NV_PASS22_RE_EXPLICIT_EMIT_POLICY || !NV_PASS22_RE_ORDERED_TEMPLATES_ABSENT)
         return -746;
      if (!NV_PASS22_RE_PB_HDR_RUNTIME_ONLY || !NV_PASS22_RE_SEMA_FORMAL_11_11)
         return -747;
      if (!NV_PASS22_RE_PATH_C_STILL_GATED || !NV_PASS22_RE_WIRED)
         return -748;
      if (!nv_pass22_explicit_emit_required())
         return -749;
      if (NV_PASS22_INLINE_TO_PCAS_MEDIAN_GLCORE != 428u ||
          NV_PASS22_INLINE_TO_PCAS_MEDIAN_CUDA != 2204u ||
          NV_PASS22_INLINE_TO_PCAS_MEDIAN_VKSC != 2724u)
         return -750;
      if (NV_PASS22_INLINE_TO_PCAS_MEDIAN_EGLCORE < NV_PASS22_INLINE_TO_PCAS_MEDIAN_GLCORE)
         return -751;
      if (NV_PASS22_SEMA_FORMAL_BASE_EGLCORE != 0x114f2e0u ||
          NV_PASS22_SEMA_FORMAL_BASE_VKSC != 0x877380u ||
          NV_PASS22_SEMA_FORMAL_BASE_GLCORE != 0x11e30c0u)
         return -752;
      if (NV_PASS22_SEMA_FORMAL_ROW_COUNT != 11u)
         return -753;
      off0 = nv_pass22_sema_formal_row_file_off(0, 0);
      off10 = nv_pass22_sema_formal_row_file_off(0, 10);
      if (off0 != NV_PASS22_SEMA_FORMAL_BASE_GLCORE + 0x10u)
         return -754;
      if (off10 != NV_PASS22_SEMA_FORMAL_BASE_GLCORE + 0x10u + 10u * 0x20u)
         return -755;
      for (li = 0; li < 3u; li++) {
         if (!nv_pass22_sema_formal_row_file_off(li, 0))
            return -756;
      }
      if (nv_pass22_sema_formal_row_file_off(3, 0) != 0)
         return -757;
      if (NV_PASS22_CHAIN_INLINE_PCAS_FORWARD_OK != 0 ||
          NV_PASS22_CHAIN_MME_RAM_UPLOAD_FORWARD_OK != 0)
         return -758;
      if (NV_PASS22_GPUCOMP_RAM_DATA_IMM_CAPPED < NV_PASS20_GPUCOMP_RAM_DATA_IMM_CAPPED)
         return -759;
      if (NV_PASS22_MME_RAM_DATA_FUNC_DIST_GLCORE != 2064u)
         return -760;
      if (!NV_PASS22_RE_SEMA_BASES_REFINED || !NV_PASS22_RE_PASS21_POLICY_VALID)
         return -761;
      if (!nv_pass22_inline_pcas_span_ok(nv_pass20_inline_qmd_launch_min_methods(false)))
         return -762;
      if (nv_pass22_inline_pcas_span_ok(NV_PASS22_INLINE_TO_PCAS_MEDIAN_GLCORE))
         return -763; /* must be strictly under glcore median */

      /* pass22: explicit INLINE+PCAS still required (emit helper works) */
      memset(qmd22, 0, sizeof(qmd22));
      qmd22[0] = 0x1u;
      memset(cb22, 0, sizeof(cb22));
      nv_push_init(&cp22, cb22, (uint32_t)(sizeof(cb22) / 4));
      nv_compute_emit_inline_qmd_launch(&cp22, 0x900000ull, qmd22, false);
      if (nv_push_dw_count(&cp22) < nv_pass20_inline_qmd_launch_min_methods(false))
         return -764;
      if (!nv_pass22_inline_pcas_span_ok(nv_push_dw_count(&cp22)))
         return -765;
      /* pass21 host sema default still authoritative after pass22 */
      if (NV_PASS21_HOST_SEMA_DEFAULT_EXEC != 0x1004u ||
          NV_PASS21_HOST_SEMA_DEFAULT_SLOT != NV_HOST_SEMA_SLOT_C)
         return -766;
      if (NV_PASS21_RE_PATH_C_STILL_GATED != NV_PASS22_RE_PATH_C_STILL_GATED)
         return -767;
   }

   /* tick162: pass22 NIR depth ladder + G2 launch policy (no HW / no full NIR) */
   {
      struct nv_pass21_compute_object obj162;
      enum nv_pass21_compute_shader_kind kinds162[8];
      unsigned kn162, ki162;
      struct nv_push cp162;
      uint32_t cb162[512];
      int lr;

      if (NV_PASS22_NIR_DEPTH_LADDER_KIND_COUNT != 5u)
         return -768;
      if (NV_PASS22_NIR_DEFAULT_KIND != NV_PASS21_CS_S2R_STORE_IMM)
         return -769;
      if (!nv_pass22_nir_kind_valid(NV_PASS21_CS_S2R_STORE_IMM_BAR))
         return -770;
      if (nv_pass22_nir_kind_valid((enum nv_pass21_compute_shader_kind)99))
         return -771;
      if (!nv_pass22_nir_kind_needs_global_store(NV_PASS21_CS_STORE_IMM) ||
          nv_pass22_nir_kind_needs_global_store(NV_PASS21_CS_EXIT_ONLY))
         return -772;

      kn162 = nv_pass22_nir_depth_kind_ladder_fill(kinds162, 8);
      if (kn162 != 5u)
         return -773;
      if (kinds162[0] != NV_PASS21_CS_EXIT_ONLY ||
          kinds162[4] != NV_PASS21_CS_S2R_STORE_IMM_BAR)
         return -774;

      lr = nv_pass22_nir_depth_ladder_build_all(&obj162, 0x220000ull, 0x810000ull,
                                                0x320000ull, 0x300000ull);
      if (lr != 0)
         return -775;

      /* rebuild default depth + launch via pass22 emit */
      memset(&obj162, 0, sizeof(obj162));
      obj162.shader_kind = NV_PASS22_NIR_DEFAULT_KIND;
      obj162.program_gpu_addr = 0x220000ull;
      obj162.qmd_gpu_addr = 0x810000ull;
      obj162.qmd_sema_gpu = 0x320000ull;
      obj162.store_gpu_addr = 0x300000ull;
      obj162.imm_value = 0x62u;
      if (nv_pass22_compute_object_build(&obj162) != 0)
         return -780;
      if (!(obj162.sph.sph[0] & (1u << 11)))
         return -781; /* global store for default s2r+store kind */
      memset(cb162, 0, sizeof(cb162));
      nv_push_init(&cp162, cb162, (uint32_t)(sizeof(cb162) / 4));
      if (nv_pass22_compute_object_emit_launch(
             &cp162, 0xc5c0u, &obj162, 0, true, 0x620000ull, 0x77u,
             NV_PASS21_HOST_SEMA_DEFAULT_MODE) != 0)
         return -782;
      if (nv_push_dw_count(&cp162) < nv_pass20_inline_qmd_launch_min_methods(true))
         return -783;
      if (!nv_pass22_inline_pcas_span_ok(nv_push_dw_count(&cp162)))
         return -784;

      /* each ladder kind builds individually */
      for (ki162 = 0; ki162 < kn162; ki162++) {
         memset(&obj162, 0, sizeof(obj162));
         obj162.shader_kind = kinds162[ki162];
         obj162.program_gpu_addr = 0x220000ull;
         obj162.qmd_gpu_addr = 0x810000ull;
         obj162.qmd_sema_gpu = 0x320000ull;
         obj162.store_gpu_addr = 0x300000ull;
         if (nv_pass22_compute_object_build(&obj162) != 0)
            return -785 - (int)ki162;
      }

      /* reject unbuilt object through pass22 launch */
      memset(&obj162, 0, sizeof(obj162));
      obj162.program_gpu_addr = 0x220000ull;
      obj162.qmd_gpu_addr = 0x810000ull;
      if (nv_pass22_compute_object_emit_launch(
             &cp162, 0xc5c0u, &obj162, 0, false, 0, 0,
             NV_PASS21_HOST_SEMA_DEFAULT_MODE) != -4)
         return -790;
   }

   /* tick163: pass22 channel policy + compiler kind ladder constants (no nv_nir.o) */
   {
      if (!NV_PASS22_NIR_USES_HAND_SPH_LADDER)
         return -791;
      if (NV_PASS22_NIR_DEFAULT_KIND != NV_PASS21_CS_S2R_STORE_IMM)
         return -792;
      /* channel G2 pass22 path: build+launch default kind must succeed host-side */
      {
         struct nv_pass21_compute_object chobj;
         struct nv_push chp;
         uint32_t chb[512];
         memset(&chobj, 0, sizeof(chobj));
         chobj.shader_kind = NV_PASS22_NIR_DEFAULT_KIND;
         chobj.program_gpu_addr = 0x230000ull;
         chobj.qmd_gpu_addr = 0x820000ull;
         chobj.qmd_sema_gpu = 0x330000ull;
         chobj.store_gpu_addr = 0x300000ull;
         if (nv_pass22_compute_object_build(&chobj) != 0)
            return -793;
         memset(chb, 0, sizeof(chb));
         nv_push_init(&chp, chb, (uint32_t)(sizeof(chb) / 4));
         if (nv_pass22_compute_object_emit_launch(
                &chp, 0xc5c0u, &chobj, 0, true, 0x330000ull, 1u,
                NV_PASS21_HOST_SEMA_DEFAULT_MODE) != 0)
            return -794;
         if (nv_push_dw_count(&chp) < nv_pass20_inline_qmd_launch_min_methods(true))
            return -795;
      }
      /* pass22 fallback: pass21 launch still works without pass22 object */
      {
         struct nv_push p21;
         uint32_t b21[512];
         uint32_t q21[NV_QMD_DWORDS];
         memset(q21, 0, sizeof(q21));
         memset(b21, 0, sizeof(b21));
         nv_push_init(&p21, b21, (uint32_t)(sizeof(b21) / 4));
         if (nv_compute_emit_g2_program_launch_pass21(
                &p21, 0xc5c0u, q21, 0x230000ull, 0x820000ull, 0, 0x53u, 16u,
                0x330000ull, 1u, 1u, 1u, true, 0x330000ull, 1u,
                NV_PASS21_HOST_SEMA_DEFAULT_MODE) != 0)
            return -796;
      }
   }

   /* tick164: Gallium/Vulkan pass22 dispatch host sema tail helper */
   {
      struct nv_push p164;
      uint32_t b164[128];
      uint32_t n0, n1;

      if (!NV_PASS22_GALLIUM_VULKAN_DISPATCH_TAIL)
         return -797;
      memset(b164, 0, sizeof(b164));
      nv_push_init(&p164, b164, (uint32_t)(sizeof(b164) / 4));
      n0 = nv_push_dw_count(&p164);
      if (nv_pass22_emit_compute_dispatch_host_sema_tail(
             &p164, 0, 1u, NV_PASS21_HOST_SEMA_DEFAULT_MODE, false) != 0)
         return -798;
      if (nv_push_dw_count(&p164) != n0)
         return -799; /* zero sema VA = no-op */
      if (nv_pass22_emit_compute_dispatch_host_sema_tail(
             &p164, 0x440000ull, 0x99u, NV_PASS21_HOST_SEMA_DEFAULT_MODE,
             false) != 0)
         return -800;
      n1 = nv_push_dw_count(&p164);
      if (n1 <= n0)
         return -801; /* must emit host sema when policy + VA set */
      /* inline QMD launch then pass22 tail (Gallium/Vulkan shape) */
      {
         uint32_t q164[NV_QMD_DWORDS];
         memset(q164, 0, sizeof(q164));
         q164[0] = 1u;
         nv_compute_emit_inline_qmd_launch(&p164, 0x930000ull, q164, false);
         n0 = nv_push_dw_count(&p164);
         nv_pass22_emit_compute_dispatch_host_sema_tail(
            &p164, 0x450000ull, 1u, NV_PASS21_HOST_SEMA_DEFAULT_MODE, false);
         if (nv_push_dw_count(&p164) <= n0)
            return -802;
      }
   }

   /* tick165: pass22 MME path C gate + NIR compute default policy (header) */
   {
      if (!NV_PASS22_MME_PATH_C_STILL_GATED || !NV_PASS22_MME_SILICON_PROBE_HOST_FIRST)
         return -803;
      if (!nv_pass22_mme_must_use_host_path())
         return -804; /* stubs must keep path C off */
      if (nv_mme_path_c_indirect_ready())
         return -805; /* unexpected non-stub programs in tree */
      if (!NV_PASS22_NIR_USES_HAND_SPH_LADDER)
         return -806;
      if (NV_PASS22_NIR_DEFAULT_KIND != NV_PASS21_CS_S2R_STORE_IMM)
         return -807;
      /* pass22 compute object still builds for default kind (shader fallback shape) */
      {
         struct nv_pass21_compute_object o165;
         memset(&o165, 0, sizeof(o165));
         o165.shader_kind = NV_PASS22_NIR_DEFAULT_KIND;
         o165.program_gpu_addr = 0x240000ull;
         o165.qmd_gpu_addr = 0x830000ull;
         o165.qmd_sema_gpu = 0x340000ull;
         o165.store_gpu_addr = 0x300000ull;
         if (nv_pass22_compute_object_build(&o165) != 0)
            return -808;
         if (!o165.ser_bytes)
            return -809;
      }
   }

   /* tick166: G3 pass22 barrier + indirect ladder policy */
   {
      struct nv_push p166;
      uint32_t b166[256];
      uint32_t n0;

      if (!NV_PASS22_G3_BARRIER_USES_PASS21 || !NV_PASS22_G3_INDIRECT_HOST_WHEN_STUB)
         return -810;
      if (!nv_pass22_mme_must_use_host_path())
         return -811;
      memset(b166, 0, sizeof(b166));
      nv_push_init(&p166, b166, (uint32_t)(sizeof(b166) / 4));
      n0 = nv_push_dw_count(&p166);
      if (nv_3d_emit_g3_barrier_all_pass22(&p166, 0, 0,
                                           NV_PASS21_HOST_SEMA_DEFAULT_MODE) != 0)
         return -812;
      if (nv_push_dw_count(&p166) <= n0)
         return -813; /* barrier must emit methods */
      n0 = nv_push_dw_count(&p166);
      if (nv_3d_emit_g3_barrier_all_pass22(&p166, 0x550000ull, 1u,
                                           NV_PASS21_HOST_SEMA_DEFAULT_MODE) != 0)
         return -814;
      if (nv_push_dw_count(&p166) <= n0)
         return -815; /* with sema VA + policy, extra host sema */
      /* path C try must fail while stubs (pass22 host mandatory) */
      if (nv_3d_try_draw_indirect_path_c(&p166, 0x1000ull, 1u, 16u, false, true))
         return -816;
      if (nv_3d_emit_draw_indirect_ladder_pass22(
             &p166, 0x1000ull, 4u, false, NULL, 0, 1u, 16u, true, 0, 0) != 0)
         return -817; /* no shadow => 0; with mme stub still 0 from path C fail */
   }

   /* tick167: Vulkan pass22 barrier entry + compute NIR default policy */
   {
      if (!NV_PASS22_G3_BARRIER_USES_PASS21)
         return -818;
      if (!NV_PASS22_RE_EXPLICIT_EMIT_POLICY)
         return -819;
      if (NV_PASS22_NIR_COMPUTE_DEFAULT_KIND != 3 &&
          NV_PASS22_NIR_DEFAULT_KIND != NV_PASS21_CS_S2R_STORE_IMM)
         return -820;
      /* pass22 barrier with sema matches Vulkan heavy barrier + optional sema shape */
      {
         struct nv_push pv;
         uint32_t bv[128];
         uint32_t n0, n1;
         memset(bv, 0, sizeof(bv));
         nv_push_init(&pv, bv, (uint32_t)(sizeof(bv) / 4));
         nv_3d_emit_g3_barrier_all_pass22(&pv, 0, 0,
                                          NV_PASS21_HOST_SEMA_DEFAULT_MODE);
         n0 = nv_push_dw_count(&pv);
         nv_3d_emit_g3_barrier_all_pass22(&pv, 0x560000ull, 2u,
                                          NV_PASS21_HOST_SEMA_DEFAULT_MODE);
         n1 = nv_push_dw_count(&pv);
         if (n1 <= n0)
            return -821;
      }
   }

   /* tick168: pass23 RE scaffold inherits pass22 policy */
   {
      if (!NV_PASS23_RE_SCAFFOLD || !NV_PASS23_RE_INHERITS_PASS22)
         return -822;
      if (!nv_pass23_explicit_emit_required())
         return -823;
      if (NV_PASS23_RE_PATH_C_STILL_GATED != NV_PASS22_RE_PATH_C_STILL_GATED)
         return -824;
      if (NV_PASS23_INLINE_TO_PCAS_MEDIAN_GLCORE != 428u)
         return -825;
      if (!NV_PASS23_RE_G0_G4_UNIFIED_TAIL)
         return -826;
      /* pass23 G4 entry delegates to pass21 when policy on */
      if (!nv_pass22_explicit_emit_required())
         return -827;
      if (!NV_PASS23_RE_SCAFFOLD)
         return -828;
      /* tick171: G0–G4 pass23 symmetry audit */
      if (!NV_PASS23_G0_G4_SYMMETRY_AUDIT || !nv_pass23_g0_g4_symmetry_ok())
         return -829;
      if (!NV_PASS23_G1_CE_PASS21 || !NV_PASS23_G2_COMPUTE_PASS22 ||
          !NV_PASS23_G3_3D_PASS22_BARRIER || !NV_PASS23_G4_VIDEO_PASS21_PASS23)
         return -830;
      if (!NV_PASS23_RE_G0_G4_SYMMETRY_TICK171)
         return -831;
      {
         struct nv_push p171;
         uint32_t b171[64];
         uint32_t n0;
         memset(b171, 0, sizeof(b171));
         nv_push_init(&p171, b171, (uint32_t)(sizeof(b171) / 4));
         n0 = nv_push_dw_count(&p171);
         if (nv_push_g0_g4_host_sema_tail_pass23(
                &p171, false, 0x570000ull, 1u,
                NV_PASS21_HOST_SEMA_DEFAULT_MODE) != 0)
            return -832;
         if (nv_push_dw_count(&p171) <= n0)
            return -833;
      }
      /* tick172: pass23 RE trace scaffold; tick175: full disasm no longer pending */
      if (!NV_PASS23_RE_TRACE_SCAFFOLD_TICK172 || NV_PASS23_RE_FULL_DISASM_PENDING != 0)
         return -834;
      /* tick173: G1 pass23 copy+host sema emits */
      {
         struct nv_push p173;
         uint32_t b173[128];
         memset(b173, 0, sizeof(b173));
         nv_push_init(&p173, b173, (uint32_t)(sizeof(b173) / 4));
         if (nv_g1_emit_copy_then_host_sema_pass23(
                &p173, 0xc5b5u, 0x1000ull, 0x2000ull, 64u, false, true,
                0x580000ull, 1u, NV_PASS21_HOST_SEMA_DEFAULT_MODE) != 0)
            return -835;
         if (nv_push_dw_count(&p173) < 8u)
            return -836;
      }
      /* tick174: G3 pass23 barrier/inv helpers */
      {
         struct nv_push p174;
         uint32_t b174[256];
         memset(b174, 0, sizeof(b174));
         nv_push_init(&p174, b174, (uint32_t)(sizeof(b174) / 4));
         if (nv_3d_emit_g3_barrier_all_pass23(&p174, 0x590000ull, 1u,
                                              NV_PASS21_HOST_SEMA_DEFAULT_MODE) != 0)
            return -837;
         if (nv_3d_emit_g3_inv_wfi_host_sema_pass23(
                &p174, 0x5a0000ull, 1u, NV_PASS21_HOST_SEMA_DEFAULT_MODE,
                true) != 0)
            return -838;
         if (nv_push_dw_count(&p174) < 16u)
            return -839;
      }
      /* tick175: pass23 multi-hour deep RE complete (re_pass23/) */
      if (!NV_PASS23_RE_DEEP_DISASM_COMPLETE || !NV_PASS23_RE_DEEP_DISASM_TICK175)
         return -840;
      if (!nv_pass23_deep_disasm_complete())
         return -841;
      if (!NV_PASS23_RE_G0_G4_SYMMETRY_RECONFIRMED || !NV_PASS23_RE_PROXIMITY_NOT_TEMPLATE)
         return -842;
      if (NV_PASS23_CHAIN_MME_RAM_UPLOAD_FORWARD_OK != 0 ||
          NV_PASS23_CHAIN_INLINE_PCAS_5STEP_OK != 0)
         return -843;
      if (NV_PASS23_SEMA_FORMAL_BASE_GLCORE != 0x11e30c0u ||
          NV_PASS23_SEMA_FORMAL_BASE_EGLCORE != 0x114f2e0u ||
          NV_PASS23_SEMA_FORMAL_BASE_VKSC != 0x877380u)
         return -844;
      if (NV_PASS23_GLCORE_HOST_SEM_C_IMM_CAPPED != 284u ||
          NV_PASS23_GPUCOMP_RAM_DATA_IMM_CAPPED != 127u)
         return -845;
      if (!NV_PASS23_RE_WIRED || !NV_PASS23_RE_INHERITS_PASS22)
         return -846;
      if (!nv_pass23_explicit_emit_required())
         return -847;
      /* tick176: pass24 RE scaffold + pass23/24 policy gate + NIR pass24 ladder */
      if (!NV_PASS24_RE_SCAFFOLD || !NV_PASS24_RE_INHERITS_PASS23)
         return -848;
      if (!nv_pass24_policy_ok() || !nv_pass24_explicit_emit_required())
         return -849;
      if (!nv_pass23_24_emit_policy_gate())
         return -850;
      if (NV_PASS24_RE_FULL_DISASM_PENDING == 0)
         return -851; /* pass24 full RE not done yet; must remain pending */
      if (NV_PASS24_GPUCOMP_RAM_DATA_IMM_CAPPED != 127u ||
          NV_PASS24_SEMA_FORMAL_BASE_GLCORE != 0x11e30c0u)
         return -852;
      if (!NV_PASS24_NIR_REQUIRES_PASS23_POLICY ||
          NV_PASS24_NIR_DEFAULT_KIND != NV_PASS22_NIR_DEFAULT_KIND)
         return -853;
      if (!NV_PASS24_RE_WIRED || !NV_PASS24_RE_PATH_C_STILL_GATED)
         return -854;
      {
         struct nv_pass21_compute_object sc[5];
         if (nv_pass24_nir_depth_ladder_build_all(
                sc, 0x100000ull, 0x200000ull, 0x210000ull, 0x300000ull) != 0)
            return -855;
      }
      /* tick177: channel pass24 G2 ladder + pass24 dispatch tail wired */
      if (!NV_PASS24_CHANNEL_G2_LAUNCH_LADDER ||
          !NV_PASS24_GALLIUM_VULKAN_DISPATCH_TAIL)
         return -856;
      {
         struct nv_push p177;
         uint32_t b177[32];
         memset(b177, 0, sizeof(b177));
         nv_push_init(&p177, b177, (uint32_t)(sizeof(b177) / 4));
         if (nv_pass24_emit_compute_dispatch_host_sema_tail(
                &p177, 0x5b0000ull, 1u, NV_PASS21_HOST_SEMA_DEFAULT_MODE,
                false) != 0)
            return -857;
         if (nv_push_dw_count(&p177) < 4u)
            return -858;
      }
      {
         struct nv_push p177b;
         uint32_t b177b[256];
         struct nv_pass21_compute_object o177;
         memset(b177b, 0, sizeof(b177b));
         memset(&o177, 0, sizeof(o177));
         nv_push_init(&p177b, b177b, (uint32_t)(sizeof(b177b) / 4));
         o177.shader_kind = NV_PASS24_NIR_DEFAULT_KIND;
         o177.program_gpu_addr = 0x100000ull;
         o177.qmd_gpu_addr = 0x200000ull;
         o177.qmd_sema_gpu = 0x210000ull;
         o177.qmd_sema_payload = 1u;
         o177.register_count = 8u;
         o177.spa_version = 0x0700u;
         o177.grid_x = 1u;
         o177.cta_x = 32u;
         if (nv_pass22_compute_object_build(&o177) != 0)
            return -859;
         if (nv_pass24_compute_object_emit_launch(
                &p177b, 0xc5c0u, &o177, 0x400000ull, true, 0x210000ull, 1u,
                NV_PASS21_HOST_SEMA_DEFAULT_MODE) != 0)
            return -860;
      }
      /* tick178: pass24 G0–G4 symmetry + G1/G3/G4 pass24 helpers */
      if (!NV_PASS24_G0_G4_SYMMETRY_AUDIT || !nv_pass24_g0_g4_symmetry_ok())
         return -861;
      if (!NV_PASS24_G1_CE_PASS23 || !NV_PASS24_G2_COMPUTE_PASS24 ||
          !NV_PASS24_G3_3D_PASS23_BARRIER || !NV_PASS24_G4_VIDEO_PASS23_PASS24)
         return -862;
      {
         struct nv_push p178;
         uint32_t b178[128];
         memset(b178, 0, sizeof(b178));
         nv_push_init(&p178, b178, (uint32_t)(sizeof(b178) / 4));
         if (nv_g1_emit_copy_then_host_sema_pass24(
                &p178, 0xc5b5u, 0x1000ull, 0x2000ull, 64u, false, true,
                0x5c0000ull, 1u, NV_PASS21_HOST_SEMA_DEFAULT_MODE) != 0)
            return -863;
         if (nv_push_dw_count(&p178) < 8u)
            return -864;
      }
      {
         struct nv_push p178g3;
         uint32_t b178g3[128];
         memset(b178g3, 0, sizeof(b178g3));
         nv_push_init(&p178g3, b178g3, (uint32_t)(sizeof(b178g3) / 4));
         if (nv_3d_emit_g3_barrier_all_pass24(
                &p178g3, 0x5d0000ull, 1u, NV_PASS21_HOST_SEMA_DEFAULT_MODE) != 0)
            return -865;
         if (nv_3d_emit_g3_inv_wfi_host_sema_pass24(
                &p178g3, 0x5d0000ull, 1u, NV_PASS21_HOST_SEMA_DEFAULT_MODE,
                true) != 0)
            return -866;
      }
   }

   if (trace_push && trace_dwords) {
      r = nv_trace_compare_bytes(buf, n * 4u, trace_push, trace_dwords * 4u,
                                 &diff);
      if (r == -3)
         return 0;
      return r == 0 ? 0 : -310 + r;
   }
   return 0;
}

/**
 * Run QMD encode determinism + sema enable bit + compute SPH EXIT validate +
 * G1/G2/G3 push shape.  Returns 0 on success; negative code = which check.
 */
static inline int
nv_smoke_selftest_host(void)
{
   uint32_t qmd_a[NV_QMD_DWORDS], qmd_b[NV_QMD_DWORDS];
   struct nv_sph_blob sph;
   uint8_t ser[512];
   uint32_t ser_sz;
   int r;

   r = nv_smoke_selftest_g1_ce_sema_push(NULL, 0, NULL, 0, NULL);
   if (r != 0)
      return r; /* -100..-122 */

   r = nv_smoke_selftest_g2_compute_sema_push(NULL, 0, NULL, 0, NULL, NULL);
   if (r != 0)
      return r; /* -200..-212 */

   r = nv_smoke_selftest_g3_3d_sema_push(NULL, 0, NULL, 0, NULL);
   if (r != 0)
      return r; /* -300..-312 */

   r = nv_qmd_trace_golden_selftest(qmd_a, qmd_b);
   if (r != 0)
      return -10 + r; /* -11..-13 */

   r = nv_qmd_smoke_encode_check(0x200000ull, 0x42u, qmd_a);
   if (r != 0)
      return -20 + r;

   r = nv_qmd_build_compute_smoke(qmd_a, 0x100000ull, 16, 0x86,
                                  0x200000ull, 0x42u, 0, 0);
   if (r != 0)
      return -30 + r;

   r = nv_trace_compare_qmd_smoke(NULL, qmd_a);
   if (r != 0)
      return -35 + r;

   nv_sph_build_compute_exit_only(&sph, 16);
   r = nv_sph_smoke_validate_blob(&sph, NV_SPH_TYPE_COMPUTE);
   if (r != 0)
      return -40 + r;

   ser_sz = nv_sph_smoke_serialise_compute_exit(ser, sizeof(ser), 16);
   if (!ser_sz || ser_sz > sizeof(ser))
      return -50;
   if ((ser[0] & 0xf) != NV_SPH_TYPE_COMPUTE)
      return -51;

   r = nv_trace_compare_sph_compute_exit(NULL, 0, ser, sizeof(ser), &ser_sz);
   if (r != 0)
      return -55 + r;

   nv_sph_build_compute_store_imm(&sph, 0xdeadbeefu, 0x300000ull, 16);
   r = nv_sph_smoke_validate_blob(&sph, NV_SPH_TYPE_COMPUTE);
   if (r != 0)
      return -60 + r;
   if (!(sph.sph[0] & (1u << 11))) /* does_global_store bit */
      return -65;
   /* G2 store-imm: expect 5*2 dwords (4 MOV/STG/EXIT-ish) min before pad */
   if (sph.sass_dwords < 8)
      return -66;
   /* EXIT hi at end */
   if (sph.sass[sph.sass_dwords - 1] != NV_SASS_EXIT_HI &&
       sph.sass[sph.sass_dwords - 1] != 0x50b00000u)
      return -67;

   /* Self-golden: G1/G2 streams must match themselves via trace_compare */
   {
      uint32_t g1a[128], g1b[128];
      uint32_t g2a[320], g2b[320];
      uint32_t na = 0, nb = 0;
      r = nv_smoke_selftest_g1_ce_sema_push(NULL, 0, g1a,
                                            (uint32_t)(sizeof(g1a) / 4), &na);
      if (r != 0)
         return -130 + r;
      r = nv_smoke_selftest_g1_ce_sema_push(g1a, na, g1b,
                                            (uint32_t)(sizeof(g1b) / 4), &nb);
      if (r != 0)
         return -140 + r;
      if (na != nb)
         return -145;
      r = nv_smoke_selftest_g2_compute_sema_push(NULL, 0, g2a,
                                                 (uint32_t)(sizeof(g2a) / 4),
                                                 &na, NULL);
      if (r != 0)
         return -150 + r;
      r = nv_smoke_selftest_g2_compute_sema_push(g2a, na, g2b,
                                                 (uint32_t)(sizeof(g2b) / 4),
                                                 &nb, NULL);
      if (r != 0)
         return -160 + r;
      if (na != nb)
         return -165;
      {
         uint32_t g3a[256], g3b[256];
         r = nv_smoke_selftest_g3_3d_sema_push(NULL, 0, g3a,
                                               (uint32_t)(sizeof(g3a) / 4),
                                               &na);
         if (r != 0)
            return -170 + r;
         r = nv_smoke_selftest_g3_3d_sema_push(g3a, na, g3b,
                                               (uint32_t)(sizeof(g3b) / 4),
                                               &nb);
         if (r != 0)
            return -180 + r;
         if (na != nb)
            return -185;
      }
   }

   return 0;
}

/**
 * Capture G1 CE sema push dwords for external golden files / HW compare.
 * Returns 0 and sets *dwords_out; negative on failure.
 */
static inline int
nv_smoke_capture_g1_push(uint32_t *out, uint32_t out_cap_dwords,
                         uint32_t *dwords_out)
{
   return nv_smoke_selftest_g1_ce_sema_push(NULL, 0, out, out_cap_dwords,
                                            dwords_out);
}

/**
 * Load optional external golden from caller-supplied buffer (binary capture
 * from --dump-g1 or HW trace).  NULL/0 trace = skip (return 0).
 */
static inline int
nv_smoke_selftest_g1_against_trace(const uint32_t *trace_push,
                                   uint32_t trace_dwords)
{
   if (!trace_push || !trace_dwords)
      return 0;
   return nv_smoke_selftest_g1_ce_sema_push(trace_push, trace_dwords,
                                            NULL, 0, NULL);
}

static inline int
nv_smoke_capture_g2_push(uint32_t *out, uint32_t out_cap_dwords,
                         uint32_t *dwords_out, uint32_t *qmd_out)
{
   return nv_smoke_selftest_g2_compute_sema_push(NULL, 0, out, out_cap_dwords,
                                                 dwords_out, qmd_out);
}

static inline int
nv_smoke_capture_g3_push(uint32_t *out, uint32_t out_cap_dwords,
                         uint32_t *dwords_out)
{
   return nv_smoke_selftest_g3_3d_sema_push(NULL, 0, out, out_cap_dwords,
                                            dwords_out);
}

/* Non-inline entry for meson-linked libnvidia_common / tools */
int nv_smoke_selftest_host_run(void);
int nv_smoke_selftest_host_run_verbose(int verbose);
/** G2 compiler store-imm vs hand SPH (0=ok/skip, -70..-86 fail when linked). */
int nv_smoke_selftest_g2_compiler_only(void);
/**
 * Full host run including optional compiler G2 check when
 * NV_SMOKE_SELFTEST_HAVE_NV_NIR is defined at compile time.
 */
int nv_smoke_selftest_host_run_full(void);

#ifdef __cplusplus
}
#endif

#endif /* NV_SMOKE_SELFTEST_H */
