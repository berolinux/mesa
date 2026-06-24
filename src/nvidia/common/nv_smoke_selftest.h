/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Host-only smoke / trace-golden selftests (no GPU).  Call from a future
 * nvidia-smoke tool or mesa unit test; returns 0 if all checks pass.
 */
#ifndef NV_SMOKE_SELFTEST_H
#define NV_SMOKE_SELFTEST_H

#include <stdint.h>

#include "nv_qmd.h"
#include "nv_sph.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Run QMD encode determinism + sema enable bit + compute SPH EXIT validate.
 * Returns 0 on success; negative code indicates which check failed.
 */
static inline int
nv_smoke_selftest_host(void)
{
   uint32_t qmd_a[NV_QMD_DWORDS], qmd_b[NV_QMD_DWORDS];
   struct nv_sph_blob sph;
   uint8_t ser[512];
   uint32_t ser_sz;
   int r;

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

   nv_sph_build_compute_exit_only(&sph, 16);
   r = nv_sph_smoke_validate_blob(&sph, NV_SPH_TYPE_COMPUTE);
   if (r != 0)
      return -40 + r;

   ser_sz = nv_sph_smoke_serialise_compute_exit(ser, sizeof(ser), 16);
   if (!ser_sz || ser_sz > sizeof(ser))
      return -50;
   if ((ser[0] & 0xf) != NV_SPH_TYPE_COMPUTE)
      return -51;

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
