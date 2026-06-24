/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Standalone HW vertical-slice runner (no Gallium/Vulkan app required).
 * Opens proprietary RM via libdrm_nvidia, creates a GPFIFO channel, runs
 * nv_smoke_hw G1/G2/G3 probes.  First silicon gate:
 *
 *   NV_SMOKE_HW_VERBOSE=1 ./nvidia_smoke_hw_cli --slices 1
 *   NV_SMOKE_HW_VERBOSE=1 ./nvidia_smoke_hw_cli --slices 3   # G1+G2
 *
 * Requires: nvidia.ko, /dev/nvidia*, built libdrm_nvidia + mesa libnvidia_rm.
 * Exit 0 = all requested slices ok; non-zero = open/channel/slice failure.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nv_smoke_hw.h"

/* Probe order for --drm auto: RM ctl first, then per-GPU, then DRM render. */
static const char *const default_drm_paths[] = {
   "/dev/nvidiactl",
   "/dev/nvidia0",
   "/dev/nvidia1",
   "/dev/dri/renderD128",
   "/dev/dri/card0",
   NULL,
};

static int
open_first_drm(const char *explicit_path, const char **opened_path_out)
{
   int fd;
   unsigned i;

   if (explicit_path) {
      fd = open(explicit_path, O_RDWR | O_CLOEXEC);
      if (fd >= 0 && opened_path_out)
         *opened_path_out = explicit_path;
      return fd;
   }

   for (i = 0; default_drm_paths[i]; i++) {
      fd = open(default_drm_paths[i], O_RDWR | O_CLOEXEC);
      if (fd >= 0) {
         if (opened_path_out)
            *opened_path_out = default_drm_paths[i];
         return fd;
      }
   }
   /* fd=-1: nv_rm_device_open may still open /dev/nvidiactl internally */
   if (opened_path_out)
      *opened_path_out = "(none; internal ctl path)";
   return -1;
}

static void
usage(const char *argv0)
{
   fprintf(stderr,
           "Usage: %s [--slices N] [--gpu N] [--drm PATH] [--timeout-ms N] [--no-notifier-check]\n"
           "  Standalone NVIDIA G1/G2/G3 HW smoke (libdrm_nvidia + RM, not nouveau).\n"
           "  --slices N     bitmask: 1=G1 CE, 2=G2 compute, 4=G3 3D (default: env or 1)\n"
           "  --gpu N        GPU index (default 0; -1 = first)\n"
           "  --drm PATH     open this node (default: try nvidiactl, nvidia0, renderD128, or internal)\n"
           "  --timeout-ms N sema/wait timeout (default 2000)\n"
           "  Env: NV_SMOKE_HW_VERBOSE=1  NV_SMOKE_HW_SLICES=N  NV_RM_LOG_CLASSES=1\n"
           "  Example first gate: NV_SMOKE_HW_VERBOSE=1 %s --slices 1\n",
           argv0, argv0);
}

int
main(int argc, char **argv)
{
   int drm_fd = -1;
   int gpu_index = 0;
   uint32_t slices = 0;
   uint64_t timeout_ns = 2000000000ull;
   bool check_notifier = true;
   const char *drm_path = NULL;
   const char *opened_path = NULL;
   struct nv_smoke_hw_result res;
   int i, r;

   for (i = 1; i < argc; i++) {
      if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
         usage(argv[0]);
         return 0;
      } else if (!strcmp(argv[i], "--slices") && i + 1 < argc) {
         slices = (uint32_t)strtoul(argv[++i], NULL, 0);
      } else if (!strcmp(argv[i], "--gpu") && i + 1 < argc) {
         gpu_index = (int)strtol(argv[++i], NULL, 0);
      } else if (!strcmp(argv[i], "--drm") && i + 1 < argc) {
         drm_path = argv[++i];
      } else if (!strcmp(argv[i], "--timeout-ms") && i + 1 < argc) {
         timeout_ns = (uint64_t)strtoul(argv[++i], NULL, 0) * 1000000ull;
      } else if (!strcmp(argv[i], "--no-notifier-check")) {
         check_notifier = false;
      } else {
         fprintf(stderr, "unknown arg: %s\n", argv[i]);
         usage(argv[0]);
         return 2;
      }
   }

   if (!slices)
      slices = nv_smoke_hw_env_slices();
   if (!slices)
      slices = NV_SMOKE_HW_G1; /* default first silicon gate */

   setenv("NV_SMOKE_HW_VERBOSE", "1", 0); /* default verbose for CLI unless already set */

   drm_fd = open_first_drm(drm_path, &opened_path);
   if (drm_path && drm_fd < 0) {
      perror(drm_path);
      return 1;
   }
   if (drm_fd >= 0)
      fprintf(stderr, "nvidia_smoke_hw_cli: using fd path %s (fd=%d) gpu=%d slices=0x%x\n",
              opened_path ? opened_path : "?", drm_fd, gpu_index, (unsigned)slices);
   else
      fprintf(stderr, "nvidia_smoke_hw_cli: no /dev/nvidia* or dri node; trying internal open "
                      "gpu=%d slices=0x%x\n", gpu_index, (unsigned)slices);

   memset(&res, 0, sizeof(res));
   r = nv_smoke_hw_run_standalone(drm_fd, gpu_index, slices, timeout_ns,
                                  check_notifier, &res);
   if (drm_fd >= 0)
      close(drm_fd);

   if (r == 0) {
      fprintf(stderr, "nvidia_smoke_hw_cli: PASS slices=0x%x ok=0x%x gpu_tried=%d "
              "g1_class=0x%x g1_gpfifo=0x%x g1_host_sema=%d g1_hs_mode=%d\n",
              (unsigned)res.slices_run, (unsigned)res.slices_ok,
              res.standalone_gpu_tried, (unsigned)res.g1_class_copy,
              (unsigned)res.g1_gpfifo_class, res.g1_host_sema_rc,
              res.g1_host_sema_mode);
      return 0;
   }
   fprintf(stderr,
           "nvidia_smoke_hw_cli: FAIL rc=%d open_rc=%d ch_rc=%d gpu_tried=%d "
           "buf_va_rc=%d eng_rc=%d sready_rc=%d\n"
           "  slices_run=0x%x ok=0x%x g1_rc=%d g2_rc=%d g3_rc=%d\n"
           "  g1_pre=%d g1_sched=%d g1_eng_alloc=%d g1_host_sema=%d g1_hs_mode=%d g1_sema_obs=0x%x\n"
           "  g1_class=0x%x g1_gpfifo=0x%x g1_tok=0x%x g1_h_copy=0x%x\n"
           "  g1_submit=%d sema_only=%d remap=%d\n"
           "  g1_userd_get/put=%u/%u host_gpfifo_put=%u doorbell=%d scheduled=%d\n"
           "  TRIAGE (run with NV_SMOKE_HW_VERBOSE=1; fix in order):\n"
           "    1) open_rc=-19: nvidia.ko + /dev/nvidiactl + /dev/nvidiaN (not only renderD128)\n"
           "    2) ch_rc=-5: GPFIFO class ladder (C86F..C36F) / TSG / USERD / VASpace / error ctx\n"
           "    3) sready/g1_pre/g1_sched/doorbell: schedule + USERD GPPut + token@usermode+0x90\n"
           "    4) g1_host_sema!=0: kickoff/channel (not CE); fix step 3 before CE methods\n"
           "       hs_mode tries 2=blob0x1001>>2, 3=blob&~3, 0=open>>2, 1=open&~3 (pass5)\n"
           "    5) host_sema ok, g1_rc fail: CE class ladder (C8B5..) / SET_OBJECT subch4 / VA\n"
           "    6) g1_userd_get unchanged: GPPut/doorbell not running on silicon\n"
           "    (RE: HW_MODEL_PASS5_DEEP_DISASM_610.43.02.md + PASS4)\n",
           r, res.standalone_open_rc, res.standalone_channel_rc,
           res.standalone_gpu_tried,
           res.standalone_buf_va_rc, res.standalone_engine_rc,
           res.standalone_submit_ready_rc,
           (unsigned)res.slices_run, (unsigned)res.slices_ok,
           res.g1_rc, res.g2_rc, res.g3_rc,
           res.g1_preflight_rc, res.g1_schedule_rc, res.g1_engine_alloc_rc,
           res.g1_host_sema_rc, res.g1_host_sema_mode,
           (unsigned)res.g1_sema_observed,
           (unsigned)res.g1_class_copy, (unsigned)res.g1_gpfifo_class,
           (unsigned)res.g1_work_submit_token, (unsigned)res.g1_h_obj_copy,
           res.g1_submit_rc, res.g1_sema_only_rc, res.g1_remap_fill_rc,
           (unsigned)res.g1_userd_gp_get, (unsigned)res.g1_userd_gp_put,
           (unsigned)res.g1_host_gpfifo_put,
           res.g1_had_doorbell ? 1 : 0, res.g1_was_scheduled ? 1 : 0);
   return 1;
}
