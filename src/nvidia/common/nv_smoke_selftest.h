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
#include "nv_qmd.h"
#include "nv_sph.h"

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
