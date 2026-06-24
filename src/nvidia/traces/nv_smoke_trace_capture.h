/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Notes + helpers for comparing our host encode against external captures
 * (proprietary 610.43.02 userspace traces or live HW pushbuffer dumps).
 *
 * ---------------------------------------------------------------------------
 * What to capture (minimal vertical slices; align with nv_smoke_selftest_g*)
 * ---------------------------------------------------------------------------
 *
 * G1 (CE + sema) — first silicon / first binary compare:
 *   - SET_OBJECT copy class (e.g. AMPERE_DMA_COPY_A / NVC6B5)
 *   - OffsetIn/Out upper/lower, PitchIn/Out, LineLengthIn, LineCount
 *   - LaunchDMA with sema release (one-word sema addr + payload)
 *   - Do NOT include unrelated channel setup if possible (isolate CE methods)
 *   Expected size in our self-generated golden: ~24 dwords (see
 *   nv_smoke_trace_goldens.h nv_smoke_golden_g1_push).
 *
 * G2 (compute QMD/PCAS):
 *   - SET_OBJECT compute (e.g. NVC3C0 / AMPERE_COMPUTE_A)
 *   - Invalidates / SPA / CWD as needed for dispatch
 *   - LOAD_INLINE_QMD_DATA(0..) or QMD in memory + SEND_PCAS_A/B
 *   - QMD sema release0 fields (addr + payload) must match our nv_qmd_encode
 *   Our golden: ~150 dwords push + 256 B QMD (nv_smoke_golden_g2_push).
 *
 * G3 (3D clear + report sema, no MME):
 *   - SET_OBJECT 3D, optional CT bind, colour clear, REPORT_SEMAPHORE
 *   Our golden: ~34 dwords (nv_smoke_golden_g3_push).
 *
 * ---------------------------------------------------------------------------
 * How to obtain a capture (outside mesa/libdrm; temp/helper only)
 * ---------------------------------------------------------------------------
 *
 * 1) nvidia_smoke_host --dump-g1 /tmp/g1_ours.bin  (our encoder baseline)
 * 2) Capture proprietary/HW push as little-endian uint32_t stream (same layout).
 * 3) Compare:
 *      nv_smoke_selftest_g1_against_trace(trace_dwords, trace_count)
 *    or nv_trace_compare_bytes(ours, ours_bytes, trace, trace_bytes, &diff)
 * 4) First mismatch offset * 4 = byte offset to investigate in class methods.
 *
 * Do not commit proprietary binaries into mesa; store captures under
 * /tmp/nvidia-reveng-pp-v2/traces/ or a private berolinux/nvidia-* helper repo.
 * Replace embedded goldens only after deliberate alignment with a verified trace.
 *
 * HW bring-up (no trace needed first):
 *   NV_SMOKE_HW=1 NV_SMOKE_HW_SLICES=1 NV_SMOKE_HW_VERBOSE=1 <app>
 *   Read stderr nvrm_smoke_hw: g1_submit / g1_payload / g1_sema / g1_class.
 *
 * ---------------------------------------------------------------------------
 * Optional: load a binary capture from disk (caller owns buffer; returns count)
 * ---------------------------------------------------------------------------
 */
#ifndef NV_SMOKE_TRACE_CAPTURE_H
#define NV_SMOKE_TRACE_CAPTURE_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "nv_smoke_selftest.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Read little-endian dword trace from path into *out_dwords (malloc'd).
 * *out_count set to dword count.  Returns 0 ok, -1 open/read fail, -2 empty.
 * Caller must free(*out_dwords).
 */
static inline int
nv_smoke_trace_load_file(const char *path, uint32_t **out_dwords,
                         uint32_t *out_count)
{
   FILE *f;
   long sz;
   uint32_t *buf;
   size_t nread;

   if (!path || !out_dwords || !out_count)
      return -1;
   *out_dwords = NULL;
   *out_count = 0;

   f = fopen(path, "rb");
   if (!f)
      return -1;
   if (fseek(f, 0, SEEK_END) != 0) {
      fclose(f);
      return -1;
   }
   sz = ftell(f);
   if (sz < 4) {
      fclose(f);
      return -2;
   }
   rewind(f);
   sz &= ~3L; /* dword align */
   buf = (uint32_t *)malloc((size_t)sz);
   if (!buf) {
      fclose(f);
      return -1;
   }
   nread = fread(buf, 1, (size_t)sz, f);
   fclose(f);
   if (nread < 4) {
      free(buf);
      return -2;
   }
   nread &= ~3u;
   *out_dwords = buf;
   *out_count = (uint32_t)(nread / 4);
   return 0;
}

/** Compare file trace to live G1 encode; 0 match, negative selftest/trace codes. */
static inline int
nv_smoke_trace_compare_g1_file(const char *path)
{
   uint32_t *trace = NULL;
   uint32_t n = 0;
   int r = nv_smoke_trace_load_file(path, &trace, &n);
   if (r != 0)
      return r == -2 ? -3 : -1; /* align with nv_trace_compare "no trace" style */
   r = nv_smoke_selftest_g1_against_trace(trace, n);
   free(trace);
   return r;
}

static inline int
nv_smoke_trace_compare_g2_file(const char *path)
{
   uint32_t *trace = NULL;
   uint32_t n = 0;
   uint32_t scratch[320];
   int r = nv_smoke_trace_load_file(path, &trace, &n);
   if (r != 0)
      return r == -2 ? -3 : -1;
   r = nv_smoke_selftest_g2_compute_sema_push(trace, n, scratch,
                                              (uint32_t)(sizeof(scratch) / 4),
                                              NULL, NULL);
   free(trace);
   return r;
}

static inline int
nv_smoke_trace_compare_g3_file(const char *path)
{
   uint32_t *trace = NULL;
   uint32_t n = 0;
   uint32_t scratch[256];
   int r = nv_smoke_trace_load_file(path, &trace, &n);
   if (r != 0)
      return r == -2 ? -3 : -1;
   r = nv_smoke_selftest_g3_3d_sema_push(trace, n, scratch,
                                         (uint32_t)(sizeof(scratch) / 4), NULL);
   free(trace);
   return r;
}

#ifdef __cplusplus
}
#endif

#endif /* NV_SMOKE_TRACE_CAPTURE_H */
