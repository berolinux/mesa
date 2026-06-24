/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Standalone / meson-runnable host smoke selftest (G1 CE sema push + G2 QMD/SPH
 * + G3 3D sema + embedded trace goldens + compiler G2 store-imm selftest).
 * Build (standalone, no full mesa link):
 *   gcc -std=c11 -I../common -I../compiler -o nvidia_smoke_host nvidia_smoke_host.c
 *     (header-only path: include nv_smoke_selftest.h only; G2 compiler check
 *      needs nv_nir.c/nv_sass.c linked or run via meson test nvidia_smoke_host)
 * Or: meson test -C <build> nvidia_smoke_host
 *
 * Exit 0 = pass, 1 = fail (stderr prints code).
 *
 * This tool is HOST ONLY (no GPU).  For live hardware vertical slices see
 * nv_smoke_hw.h environment:
 *   NV_SMOKE_HW=1              run G1/G2/G3 on device create (if wired)
 *   NV_SMOKE_HW_SLICES=1|2|4   bitmask: 1=G1, 2=G2, 4=G3 (default all=7)
 * Example first silicon gate: NV_SMOKE_HW=1 NV_SMOKE_HW_SLICES=1 <app>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nv_smoke_selftest.h"
/* Traces dir is sibling of tools/ via -I../common; goldens live under traces/ */
#include "../traces/nv_smoke_trace_goldens.h"

static int
dump_dwords(const char *path, const uint32_t *dw, uint32_t n, int quiet)
{
   FILE *f;
   if (!path || !dw || !n)
      return 0;
   f = fopen(path, "wb");
   if (!f) {
      perror(path);
      return -1;
   }
   if (fwrite(dw, 4, n, f) != n) {
      perror("fwrite");
      fclose(f);
      return -1;
   }
   fclose(f);
   if (!quiet)
      fprintf(stderr, "wrote %u dwords to %s\n", n, path);
   return 0;
}

static void
usage(const char *argv0)
{
   fprintf(stderr,
           "Usage: %s [--quiet] [--dump-g1|g2|g3 PATH] [--dump-qmd PATH]\n"
           "         [--check-g1-golden] [--check-g2-golden] [--check-g3-golden]\n"
           "  Host-only NVIDIA vertical-slice selftest (no GPU).\n"
           "  G1=CE sema; G2=compute QMD/PCAS + store-imm SPH; G3=3D sema.\n"
           "  HW (separate): NV_SMOKE_HW=1 NV_SMOKE_HW_SLICES=1|2|4|7 with nvgpu/nvrm.\n",
           argv0);
}

int
main(int argc, char **argv)
{
   int quiet = 0;
   int check_g1 = 0, check_g2 = 0, check_g3 = 0;
   const char *dump_g1 = NULL, *dump_g2 = NULL, *dump_g3 = NULL;
   const char *dump_qmd = NULL;
   int i, r;
   uint32_t push_a[320], push_b[320];
   uint32_t qmd_a[NV_QMD_DWORDS], qmd_b[NV_QMD_DWORDS];
   uint32_t na = 0, nb = 0;

   for (i = 1; i < argc; i++) {
      if (!strcmp(argv[i], "--quiet") || !strcmp(argv[i], "-q"))
         quiet = 1;
      else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
         usage(argv[0]);
         return 0;
      } else if (!strcmp(argv[i], "--check-g1-golden"))
         check_g1 = 1;
      else if (!strcmp(argv[i], "--check-g2-golden"))
         check_g2 = 1;
      else if (!strcmp(argv[i], "--check-g3-golden"))
         check_g3 = 1;
      else if (!strcmp(argv[i], "--dump-g1") && i + 1 < argc)
         dump_g1 = argv[++i];
      else if (!strcmp(argv[i], "--dump-g2") && i + 1 < argc)
         dump_g2 = argv[++i];
      else if (!strcmp(argv[i], "--dump-g3") && i + 1 < argc)
         dump_g3 = argv[++i];
      else if (!strcmp(argv[i], "--dump-qmd") && i + 1 < argc)
         dump_qmd = argv[++i];
      else {
         usage(argv[0]);
         return 2;
      }
   }

   r = nv_smoke_selftest_host();
   if (!quiet) {
      if (r == 0)
         fprintf(stderr, "nv_smoke_selftest_host: PASS\n");
      else
         fprintf(stderr, "nv_smoke_selftest_host: FAIL code %d\n", r);
   }
   if (r != 0)
      return 1;

   r = nv_smoke_selftest_against_embedded_goldens();
   if (!quiet) {
      if (r == 0)
         fprintf(stderr, "nv_smoke embedded goldens: PASS\n");
      else
         fprintf(stderr, "nv_smoke embedded goldens: FAIL code %d\n", r);
   }
   if (r != 0)
      return 1;

   if (check_g1 || dump_g1) {
      r = nv_smoke_selftest_g1_ce_sema_push(NULL, 0, push_a,
                                            (uint32_t)(sizeof(push_a) / 4),
                                            &na);
      if (r != 0) {
         if (!quiet)
            fprintf(stderr, "g1 capture: FAIL %d\n", r);
         return 1;
      }
      r = nv_smoke_selftest_g1_ce_sema_push(NULL, 0, push_b,
                                            (uint32_t)(sizeof(push_b) / 4),
                                            &nb);
      if (r != 0 || na != nb || memcmp(push_a, push_b, (size_t)na * 4) != 0) {
         if (!quiet)
            fprintf(stderr, "g1 golden/determinism: FAIL r=%d na=%u nb=%u\n",
                    r, na, nb);
         return 1;
      }
      if (!quiet)
         fprintf(stderr, "g1 golden/determinism: PASS (%u dwords)\n", na);
      if (dump_g1 && dump_dwords(dump_g1, push_a, na, quiet))
         return 1;
      r = nv_smoke_selftest_g1_ce_sema_push(push_a, na, push_b,
                                            (uint32_t)(sizeof(push_b) / 4),
                                            &nb);
      if (r != 0) {
         if (!quiet)
            fprintf(stderr, "g1 trace_compare self-golden: FAIL %d\n", r);
         return 1;
      }
      if (!quiet)
         fprintf(stderr, "g1 trace_compare self-golden: PASS\n");
   }

   if (check_g2 || dump_g2 || dump_qmd) {
      r = nv_smoke_selftest_g2_compute_sema_push(NULL, 0, push_a,
                                                 (uint32_t)(sizeof(push_a) / 4),
                                                 &na, qmd_a);
      if (r != 0) {
         if (!quiet)
            fprintf(stderr, "g2 capture: FAIL %d\n", r);
         return 1;
      }
      r = nv_smoke_selftest_g2_compute_sema_push(NULL, 0, push_b,
                                                 (uint32_t)(sizeof(push_b) / 4),
                                                 &nb, qmd_b);
      if (r != 0 || na != nb || memcmp(push_a, push_b, (size_t)na * 4) != 0 ||
          memcmp(qmd_a, qmd_b, NV_QMD_BYTES) != 0) {
         if (!quiet)
            fprintf(stderr, "g2 golden/determinism: FAIL r=%d na=%u nb=%u\n",
                    r, na, nb);
         return 1;
      }
      if (!quiet)
         fprintf(stderr, "g2 golden/determinism: PASS (%u dwords, QMD %u B)\n",
                 na, (unsigned)NV_QMD_BYTES);
      if (dump_g2 && dump_dwords(dump_g2, push_a, na, quiet))
         return 1;
      if (dump_qmd && dump_dwords(dump_qmd, qmd_a, NV_QMD_DWORDS, quiet))
         return 1;
      r = nv_smoke_selftest_g2_compute_sema_push(push_a, na, push_b,
                                                 (uint32_t)(sizeof(push_b) / 4),
                                                 &nb, qmd_b);
      if (r != 0) {
         if (!quiet)
            fprintf(stderr, "g2 trace_compare self-golden: FAIL %d\n", r);
         return 1;
      }
      if (!quiet)
         fprintf(stderr, "g2 trace_compare self-golden: PASS\n");
   }

   if (check_g3 || dump_g3) {
      r = nv_smoke_selftest_g3_3d_sema_push(NULL, 0, push_a,
                                            (uint32_t)(sizeof(push_a) / 4),
                                            &na);
      if (r != 0) {
         if (!quiet)
            fprintf(stderr, "g3 capture: FAIL %d\n", r);
         return 1;
      }
      r = nv_smoke_selftest_g3_3d_sema_push(NULL, 0, push_b,
                                            (uint32_t)(sizeof(push_b) / 4),
                                            &nb);
      if (r != 0 || na != nb || memcmp(push_a, push_b, (size_t)na * 4) != 0) {
         if (!quiet)
            fprintf(stderr, "g3 golden/determinism: FAIL r=%d na=%u nb=%u\n",
                    r, na, nb);
         return 1;
      }
      if (!quiet)
         fprintf(stderr, "g3 golden/determinism: PASS (%u dwords)\n", na);
      if (dump_g3 && dump_dwords(dump_g3, push_a, na, quiet))
         return 1;
      r = nv_smoke_selftest_g3_3d_sema_push(push_a, na, push_b,
                                            (uint32_t)(sizeof(push_b) / 4),
                                            &nb);
      if (r != 0) {
         if (!quiet)
            fprintf(stderr, "g3 trace_compare self-golden: FAIL %d\n", r);
         return 1;
      }
      if (!quiet)
         fprintf(stderr, "g3 trace_compare self-golden: PASS\n");
   }

   return 0;
}
