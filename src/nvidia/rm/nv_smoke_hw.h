/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Hardware vertical-slice smoke runners (G1/G2/G3) using nv_channel_g* helpers.
 * Requires live RM/channel; without HAVE_LIBDRM_NVIDIA returns -ENOSYS.
 * Host encode checks remain in nv_smoke_selftest.h (no GPU).
 */
#ifndef NV_SMOKE_HW_H
#define NV_SMOKE_HW_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct nv_channel;
struct nv_rm_device;
struct nv_shader;

/* Which slices to run (bitmask) */
#define NV_SMOKE_HW_G1   (1u << 0)
#define NV_SMOKE_HW_G2   (1u << 1)
#define NV_SMOKE_HW_G3   (1u << 2)
#define NV_SMOKE_HW_ALL  (NV_SMOKE_HW_G1 | NV_SMOKE_HW_G2 | NV_SMOKE_HW_G3)

struct nv_smoke_hw_result {
   int g1_rc;   /* 0 ok, negative errno/fail; 1 = skipped */
   int g2_rc;
   int g3_rc;
   uint32_t slices_run;
   uint32_t slices_ok;
};

/**
 * Scratch BOs for smoke: sema (4B), CE src/dst (G1), QMD (G2), CT (G3).
 * Caller owns via nv_smoke_hw_scratch_destroy; or pass NULL and use one-shot
 * alloc inside nv_smoke_hw_run_on_channel (alloc/free per call).
 */
struct nv_smoke_hw_scratch {
   struct nv_rm_device *rm;
   struct nv_rm_bo *sema_bo;
   struct nv_rm_bo *src_bo;   /* G1 source */
   struct nv_rm_bo *dst_bo;   /* G1 dest */
   struct nv_rm_bo *qmd_bo;   /* G2 QMD */
   struct nv_rm_bo *ct_bo;    /* G3 colour target (optional) */
   volatile uint32_t *sema_cpu;
   void *qmd_cpu;
   void *src_cpu;
   void *dst_cpu;
   uint64_t sema_gpu;
   uint64_t src_gpu;
   uint64_t dst_gpu;
   uint64_t qmd_gpu;
   uint64_t ct_gpu;
   uint32_t sema_payload;
   bool owned; /* scratch alloc'd by create; destroy frees */
};

/** Allocate/map host-mappable scratch BOs on rm. Returns 0 or negative errno. */
int nv_smoke_hw_scratch_create(struct nv_rm_device *rm,
                               struct nv_smoke_hw_scratch *out);

void nv_smoke_hw_scratch_destroy(struct nv_smoke_hw_scratch *sc);

/**
 * Run selected slices on an existing channel + scratch.
 * g2_shader: optional uploaded compute smoke shader (NULL => program VA 0).
 * timeout_ns: sema/GPFIFO wait budget (e.g. 2e9).
 * Returns 0 if all requested slices succeeded; else first failing slice rc.
 * Fills result_out if non-NULL.
 */
int nv_smoke_hw_run_on_channel(struct nv_channel *ch,
                               struct nv_smoke_hw_scratch *sc,
                               uint32_t slices,
                               struct nv_shader *g2_shader,
                               uint64_t timeout_ns,
                               bool check_notifier,
                               struct nv_smoke_hw_result *result_out);

/**
 * One-shot: create scratch, run slices, destroy scratch.
 * ch must be valid and ready.
 */
int nv_smoke_hw_run_oneshot(struct nv_rm_device *rm, struct nv_channel *ch,
                            uint32_t slices, struct nv_shader *g2_shader,
                            uint64_t timeout_ns, bool check_notifier,
                            struct nv_smoke_hw_result *result_out);

#ifdef __cplusplus
}
#endif

#endif /* NV_SMOKE_HW_H */
