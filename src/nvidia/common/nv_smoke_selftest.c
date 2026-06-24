/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Runnable host selftest entry (link with mesa or compile standalone via
 * tools/nvidia_smoke_host.c).  No GPU required.
 *
 * Mesa build defines NV_SMOKE_SELFTEST_HAVE_NV_NIR and links nv_nir/nv_sass
 * so nv_smoke_selftest_host() runs compiler G2 store-imm checks (-70..-86).
 * Standalone gcc of nvidia_smoke_host.c (header-only) skips those checks.
 */

#include "nv_smoke_selftest.h"

#include <stdio.h>

/* Mesa meson: compile with -DNV_SMOKE_SELFTEST_HAVE_NV_NIR and link compiler */
#ifdef NV_SMOKE_SELFTEST_HAVE_NV_NIR
#include "nv_nir.h"
#endif

int
nv_smoke_selftest_host_run(void)
{
   return nv_smoke_selftest_host();
}

int
nv_smoke_selftest_g2_compiler_only(void)
{
#ifdef NV_SMOKE_SELFTEST_HAVE_NV_NIR
   int r = nv_nir_g2_store_imm_smoke_selftest(0xdeadbeefu, 0x300000ull, 16);
   if (r != 0)
      return r;
   r = nv_nir_g2_store_imm_smoke_selftest(0x11111111u, 0x400000ull, 16);
   if (r != 0)
      return r - 10; /* -80..-86 secondary imm/addr pair */
   return 0;
#else
   return 0; /* skipped without compiler link */
#endif
}

int
nv_smoke_selftest_host_run_full(void)
{
   int r = nv_smoke_selftest_host();
   if (r != 0)
      return r;
   return nv_smoke_selftest_g2_compiler_only();
}

int
nv_smoke_selftest_host_run_verbose(int verbose)
{
   int r = nv_smoke_selftest_host_run_full();
   if (verbose) {
      if (r == 0)
         fprintf(stderr, "nv_smoke_selftest_host: PASS\n");
      else
         fprintf(stderr, "nv_smoke_selftest_host: FAIL code %d\n", r);
   }
   return r;
}
