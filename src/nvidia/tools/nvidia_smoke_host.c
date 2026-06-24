/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Standalone / meson-runnable host smoke selftest (G1 CE sema push + G2 QMD/SPH).
 * Build (standalone, no full mesa link):
 *   gcc -std=c11 -I../common -o nvidia_smoke_host nvidia_smoke_host.c
 *     (header-only path: include nv_smoke_selftest.h only)
 * Or link nv_smoke_selftest.c from mesa build.
 *
 * Exit 0 = pass, 1 = fail (stderr prints code).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nv_smoke_selftest.h"

static void
usage(const char *argv0)
{
   fprintf(stderr,
           "Usage: %s [--quiet] [--dump-g1 PATH] [--check-g1-golden]\n"
           "  Host-only NVIDIA vertical-slice selftest (no GPU).\n"
           "  --dump-g1 PATH   write G1 CE sema push dwords as binary\n"
           "  --check-g1-golden  run G1 twice and require byte-identical streams\n",
           argv0);
}

int
main(int argc, char **argv)
{
   int quiet = 0;
   int check_golden = 0;
   const char *dump_g1 = NULL;
   int i, r;
   uint32_t push_a[128], push_b[128];
   uint32_t na = 0, nb = 0;

   for (i = 1; i < argc; i++) {
      if (!strcmp(argv[i], "--quiet") || !strcmp(argv[i], "-q"))
         quiet = 1;
      else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
         usage(argv[0]);
         return 0;
      } else if (!strcmp(argv[i], "--check-g1-golden"))
         check_golden = 1;
      else if (!strcmp(argv[i], "--dump-g1") && i + 1 < argc)
         dump_g1 = argv[++i];
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

   if (check_golden || dump_g1) {
      r = nv_smoke_selftest_g1_ce_sema_push(NULL, 0, push_a,
                                            (uint32_t)(sizeof(push_a) / 4),
                                            &na);
      if (r != 0) {
         if (!quiet)
            fprintf(stderr, "g1 capture A: FAIL %d\n", r);
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

      if (dump_g1) {
         FILE *f = fopen(dump_g1, "wb");
         if (!f) {
            perror(dump_g1);
            return 1;
         }
         if (fwrite(push_a, 4, na, f) != na) {
            perror("fwrite");
            fclose(f);
            return 1;
         }
         fclose(f);
         if (!quiet)
            fprintf(stderr, "wrote %u dwords to %s\n", na, dump_g1);
      }

      /* Self-golden: compare second emit against first as trace */
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

   return 0;
}
