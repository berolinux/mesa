/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Mesa-linked host smoke: encode selftest + compiler G2 store-imm
 * (NV_SMOKE_SELFTEST_HAVE_NV_NIR + idep_nir).  Built by meson as nvidia_smoke_full.
 *
 * Live self-golden (encode twice, compare) is used instead of embedded arrays:
 * mesa compile flags can diverge slightly from standalone gcc encode; both are
 * valid determinism checks.  External traces: traces/nv_smoke_trace_capture.h
 *
 * Exit 0 = pass.  HW: NV_SMOKE_HW=1 NV_SMOKE_HW_SLICES=1 NV_SMOKE_HW_VERBOSE=1
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nv_smoke_selftest.h"

static int
live_self_golden_all(int quiet)
{
   uint32_t g1[128], g2[320], g3[256];
   uint32_t n1 = 0, n2 = 0, n3 = 0;
   int r1, r2, r3;

   r1 = nv_smoke_selftest_g1_ce_sema_push(NULL, 0, g1,
                                          (uint32_t)(sizeof(g1) / 4), &n1);
   if (r1 == 0)
      r1 = nv_smoke_selftest_g1_ce_sema_push(g1, n1, NULL, 0, NULL);

   r2 = nv_smoke_selftest_g2_compute_sema_push(NULL, 0, g2,
                                               (uint32_t)(sizeof(g2) / 4),
                                               &n2, NULL);
   if (r2 == 0)
      r2 = nv_smoke_selftest_g2_compute_sema_push(g2, n2, NULL, 0, NULL, NULL);

   r3 = nv_smoke_selftest_g3_3d_sema_push(NULL, 0, g3,
                                          (uint32_t)(sizeof(g3) / 4), &n3);
   if (r3 == 0)
      r3 = nv_smoke_selftest_g3_3d_sema_push(g3, n3, NULL, 0, NULL);

   if (!quiet)
      fprintf(stderr,
              "live self-golden: g1=%d (n=%u) g2=%d (n=%u) g3=%d (n=%u)\n",
              r1, n1, r2, n2, r3, n3);
   if (r1 != 0 || r2 != 0 || r3 != 0)
      return r1 ? r1 : (r2 ? r2 : r3);
   return 0;
}

int
main(int argc, char **argv)
{
   int quiet = 0;
   int i, r;

   for (i = 1; i < argc; i++) {
      if (!strcmp(argv[i], "--quiet") || !strcmp(argv[i], "-q"))
         quiet = 1;
      else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
         fprintf(stderr,
                 "Usage: %s [--quiet]\n"
                 "  Mesa-linked: host_run_full (encode+compiler G2) + live self-golden.\n"
                 "  Standalone embedded goldens: nvidia_smoke_host --check-g*-golden\n"
                 "  HW: NV_SMOKE_HW=1 NV_SMOKE_HW_SLICES=1 NV_SMOKE_HW_VERBOSE=1 <app>\n",
                 argv[0]);
         return 0;
      }
   }

   r = nv_smoke_selftest_host_run_full();
   if (!quiet) {
      if (r == 0)
         fprintf(stderr, "nv_smoke_selftest_host_run_full: PASS\n");
      else
         fprintf(stderr, "nv_smoke_selftest_host_run_full: FAIL code %d\n", r);
   }
   if (r != 0)
      return 1;

   r = live_self_golden_all(quiet);
   if (!quiet) {
      if (r == 0)
         fprintf(stderr, "live self-golden: PASS\n");
      else
         fprintf(stderr, "live self-golden: FAIL %d\n", r);
   }
   if (r != 0)
      return 1;

   if (!quiet)
      fprintf(stderr, "nvidia_smoke_full: ALL PASS\n");
   return 0;
}
