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

   /* Standalone sema-only release (DATA_TRANSFER_TYPE_NONE + sema) */
   memset(buf_b, 0, sizeof(buf_b));
   nv_push_init(&p, buf_b, (uint32_t)(sizeof(buf_b) / 4));
   nv_push_set_subch(&p, NV_PUSH_SUBCH_COPY);
   nv_copy_emit_semaphore_release(&p, sema_gpu, sema_payload);
   n = nv_push_dw_count(&p);
   if (n < 6)
      return -112;
   saw_launch = false;
   for (i = 0; i + 1 < n; i++) {
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

   if (trace_push && trace_dwords) {
      r = nv_trace_compare_bytes(buf, n * 4u, trace_push, trace_dwords * 4u,
                                 &diff);
      if (r == -3)
         return 0;
      return r == 0 ? 0 : -120 + r; /* -121 size, -122 mismatch */
   }
   return 0;
}

/**
 * Run QMD encode determinism + sema enable bit + compute SPH EXIT validate +
 * G1 CE sema push shape.  Returns 0 on success; negative code = which check.
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

   return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* NV_SMOKE_SELFTEST_H */
