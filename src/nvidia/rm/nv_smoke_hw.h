/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Hardware vertical-slice smoke runners (G1/G2/G3) using nv_channel_g* helpers.
 * Requires live RM/channel; without HAVE_LIBDRM_NVIDIA returns -ENOSYS.
 * Host encode checks remain in nv_smoke_selftest.h (no GPU).
 *
 * ---------------------------------------------------------------------------
 * Environment (bring-up / debug; optional on device/context create)
 * ---------------------------------------------------------------------------
 *
 *   NV_SMOKE_HW=1
 *     When set (any non-empty value except "0"), nvrm_device_create / nvgpu
 *     context init may run G1/G2/G3 HW smoke on the first queue channel.
 *     Failures are logged but typically do not abort device create (bring-up
 *     mode).  Unset or NV_SMOKE_HW=0 disables this path entirely.
 *
 *   NV_SMOKE_HW_SLICES=<mask>
 *     Bitmask of slices to run when NV_SMOKE_HW is enabled (default: all):
 *       1 = G1  CE memcpy + sema release (path A/B sema wait on host)
 *       2 = G2  compute QMD/PCAS + store-imm shader + sema
 *       4 = G3  3D clear/draw + report sema (no MME; path A/B)
 *     Examples:
 *       NV_SMOKE_HW=1 NV_SMOKE_HW_SLICES=1     # G1 only (first silicon gate)
 *       NV_SMOKE_HW=1 NV_SMOKE_HW_SLICES=3     # G1+G2
 *       NV_SMOKE_HW=1 NV_SMOKE_HW_SLICES=7     # all (same as default)
 *
 *   NV_SMOKE_HW_VERBOSE=1
 *     Always print nv_smoke_hw_log_result (g1_submit/payload/sema/class).
 *     Failures also log even without this flag.  Also logs device classes.
 *
 *   NV_RM_LOG_CLASSES=1
 *     Log refined gpfifo/3d/compute/copy classes at nv_rm_device_open time.
 *
 * Kickoff path (G1 hung sema with g1_sema_only also failing):
 *   - Channel schedule (NVA06F/A06C GPFIFO_SCHEDULE) must succeed (preflight -EAGAIN)
 *   - Volta+: usermode doorbell @ +0x90 (NVC361_NOTIFY_CHANNEL_PENDING) + work_submit_token;
 *     else GPPut-only kick (pre_d=1). Matches 610.43.02 / clc361.h binary RE.
 *   - CE/3D/compute/GPFIFO class ladders newest-first (C8B5/C8C0/C86F…; HW_MODEL_FROM_BINARIES_610)
 *   - USERD GPPut published before doorbell (libdrm nvidia_gpfifo_submit_one)
 *   - GP entry wait uses SYNC_WAIT bit 31 (not LEVEL_SUBROUTINE bit 9)
 *   - nv_channel_submit_preflight() before G1 reports schedule/USERD/doorbell state
 *   - g1_host_sema_rc: NVC36F host sema on subch 0 (run BEFORE CE; kickoff gate)
 *   - g1_userd_gp_get/put vs g1_hput: ring consumption after failed G1
 *   - g1_svram/g1_bvram: sema/src-dst BO placement (VRAM vs sysmem)
 *   - Scratch sema prefers VRAM+CPU map; src/dst prefer sysmem for host verify
 *   - Channel buffers remapped via NVOS46 (nv_channel_ensure_buffers_gpu_va)
 *
 * Programmatic entrypoints (same semantics as env):
 *   nv_smoke_hw_env_requested() / nv_smoke_hw_env_slices()
 *   nv_smoke_hw_run_oneshot(rm, ch, slices, g2_shader, timeout_ns, ...)
 *   nv_smoke_hw_run_standalone(drm_fd, gpu_index, slices, ...)  // no app needed
 *   nvrm_device_smoke_hw_run(device, slices, timeout_ns)  // vulkan/nvrm
 *   tools/nvidia_smoke_hw_cli --slices 1   // first silicon gate CLI
 *
 * Host-only (no GPU): build/run nvidia_smoke_host or nv_smoke_selftest_host().
 * Compiler G2 store-imm path: nv_nir_compile_g2_store_imm_smoke() /
 * nv_nir_g2_store_imm_smoke_selftest() (included in nv_smoke_selftest_host).
 *
 * Requirements for HW path:
 *   - Proprietary nvidia.ko (open-gpu-kernel-modules compatible) loaded
 *   - /dev/nvidia* accessible; libdrm_nvidia + mesa nvgpu/nvrm built
 *   - Channel/class alloc succeeds (see nv_channel_g1/g2/g3 helpers)
 *
 * Slices are independent: fix G1 before trusting G2/G3 failures.
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
   /* standalone path only (0 = n/a or ok; set by nv_smoke_hw_run_standalone) */
   int standalone_open_rc;    /* -ENODEV if nv_rm_device_open failed */
   int standalone_channel_rc; /* -EIO if nv_channel_create failed */
   int standalone_buf_va_rc;  /* nv_channel_ensure_buffers_gpu_va */
   int standalone_submit_ready_rc; /* nv_channel_ensure_submit_ready */
   int standalone_engine_rc;  /* nv_channel_ensure_engine_objects */
   int standalone_gpu_tried;  /* last gpu_index tried for open */
   uint32_t slices_run;
   uint32_t slices_ok;
   /* G1 phase detail for bring-up (0 = n/a or success path) */
   int g1_submit_rc;   /* return from nv_channel_g1_ce_copy_sema_submit */
   int g1_payload_rc;  /* 0 ok, -EIO if sema ok but dst != src (256B) */
   int g1_sema_only_rc; /* secondary: sema-only submit if copy path failed */
   int g1_remap_fill_rc; /* tertiary: REMAP fill+sema if copy+sema_only both fail or copy fails */
   int g1_host_sema_rc;  /* quaternary: NVC36F host sema only (kickoff probe) */
   int g1_preflight_rc; /* nv_channel_submit_preflight before G1 (-EAGAIN=unscheduled) */
   int g1_preflight_detail; /* 0 ok, 1=GPPut-only (no doorbell token), else errno */
   int g1_schedule_rc;  /* channel->schedule_rc from last GPFIFO_SCHEDULE attempt */
   int g1_engine_alloc_rc; /* channel engine RmAlloc status (copy/compute/3d children) */
   uint32_t g1_h_obj_copy; /* non-zero if copy engine object allocated under channel */
   uint16_t g1_notifier_status; /* error notifier status word (0xffff = unset) */
   uint32_t g1_notifier_info32;
   uint32_t g1_sema_observed; /* sema_cpu[0] after wait (debug) */
   uint32_t g1_class_copy;    /* class used (0 if unknown) */
   uint32_t g1_gpfifo_class;  /* channel GPFIFO class that RmAlloc succeeded with */
   uint32_t g1_work_submit_token; /* token for usermode+0x90 (0 if missing) */
   uint32_t g1_fill_observed; /* dst_cpu[0] after remap-fill probe (if run) */
   uint32_t g1_userd_gp_get;  /* USERD GPGet after G1 attempts */
   uint32_t g1_userd_gp_put;  /* USERD GPPut after G1 attempts */
   uint32_t g1_host_gpfifo_put; /* ch->gpfifo_put after G1 attempts */
   bool g1_had_doorbell;      /* work_submit_token + usermode map at G1 start */
   bool g1_was_scheduled;     /* channel scheduled at G1 start */
   bool g1_sema_vram;         /* sema BO in VRAM (vs sysmem) */
   bool g1_bufs_vram;         /* src/dst BOs in VRAM */
   bool g1_used_class_try;    /* true if multi-class/pipelined try path was used */
   uint64_t g1_sema_gpu;
   uint64_t g1_src_gpu;
   uint64_t g1_dst_gpu;
   /* G2 phase detail */
   int g2_submit_rc;
   int g2_store_rc;    /* 0 ok, -EIO if sema ok but dst[0] != store_imm */
   int g2_preflight_rc;
   int g2_host_sema_rc; /* secondary: host/GPFIFO sema if compute submit fails */
   int g2_engine_alloc_rc;
   uint32_t g2_h_obj_compute;
   uint32_t g2_store_observed; /* dst_cpu[0] after G2 when mapped */
   uint32_t g2_class_compute;
   uint64_t g2_prog_gpu;
};

/** True if NV_SMOKE_HW_VERBOSE is set (non-empty, not "0"). */
bool nv_smoke_hw_env_verbose(void);

/** Print result to stderr (always if verbose or any slice failed). */
void nv_smoke_hw_log_result(const struct nv_smoke_hw_result *res,
                            const char *prefix);

/**
 * Scratch BOs for smoke: sema (4B), CE src/dst (G1), QMD (G2), CT (G3).
 * Caller owns via nv_smoke_hw_scratch_destroy; or pass NULL and use one-shot
 * alloc inside nv_smoke_hw_run_on_channel (alloc/free per call).
 */
struct nv_smoke_hw_scratch {
   struct nv_rm_device *rm;
   struct nv_rm_bo *sema_bo;  /* sema: prefer VRAM+CPU map, else sysmem */
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
   uint32_t g2_store_imm;   /* value written by G2 store-imm smoke (default 0xdeadbeef) */
   bool sema_vram;
   bool bufs_vram;          /* last successful src/dst placement (sysmem preferred) */
   bool owned; /* scratch alloc'd by create; destroy frees */
};

/** True if getenv("NV_SMOKE_HW") is set and non-zero/non-empty (not "0"). */
bool nv_smoke_hw_env_requested(void);

/** Parse NV_SMOKE_HW_SLICES (default ALL). Bits: 1=G1, 2=G2, 4=G3. */
uint32_t nv_smoke_hw_env_slices(void);

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

/**
 * Standalone bring-up: open RM (drm_fd, often -1 or /dev/dri/cardN fd), create
 * a graphics GPFIFO channel, run slices, destroy.  No Gallium/Vulkan required.
 * Returns 0 if all requested slices ok, negative on open/channel/slice failure.
 * result_out optional.  Logs via nv_smoke_hw_log_result when verbose or fail.
 */
int nv_smoke_hw_run_standalone(int drm_fd, int gpu_index, uint32_t slices,
                               uint64_t timeout_ns, bool check_notifier,
                               struct nv_smoke_hw_result *result_out);

#ifdef __cplusplus
}
#endif

#endif /* NV_SMOKE_HW_H */
