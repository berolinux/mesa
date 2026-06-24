/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Runnable host selftest entry (link with mesa or compile standalone via
 * tools/nvidia_smoke_host.c).  No GPU required.
 */

#include "nv_smoke_selftest.h"

#include <stdio.h>

int
nv_smoke_selftest_host_run(void)
{
   return nv_smoke_selftest_host();
}

int
nv_smoke_selftest_host_run_verbose(int verbose)
{
   int r = nv_smoke_selftest_host();
   if (verbose) {
      if (r == 0)
         fprintf(stderr, "nv_smoke_selftest_host: PASS\n");
      else
         fprintf(stderr, "nv_smoke_selftest_host: FAIL code %d\n", r);
   }
   return r;
}
