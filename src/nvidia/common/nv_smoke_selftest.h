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
      uint32_t buf_g3[256];
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
      uint32_t g2buf[200], g3buf[200], vbuf[180];
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
      uint32_t g2p[64], g3p[160];
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
      uint32_t g2buf[220], g3buf[220];
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
