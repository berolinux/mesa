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

static void
usage(const char *argv0)
{
   fprintf(stderr,
           "Usage: %s [--slices N] [--gpu N] [--drm PATH] [--timeout-ms N] [--no-notifier-check]\n"
           "  Standalone NVIDIA G1/G2/G3 HW smoke (libdrm_nvidia + RM, not nouveau).\n"
           "  --slices N     bitmask: 1=G1 CE, 2=G2 compute, 4=G3 3D (default: env or 1)\n"
           "  --gpu N        GPU index (default 0; -1 = first)\n"
           "  --drm PATH     open this DRM node (default: try -1 fd = ctl-only path)\n"
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

   if (drm_path) {
      drm_fd = open(drm_path, O_RDWR | O_CLOEXEC);
      if (drm_fd < 0) {
         perror(drm_path);
         return 1;
      }
   }

   setenv("NV_SMOKE_HW_VERBOSE", "1", 0); /* default verbose for CLI unless already set */

   r = nv_smoke_hw_run_standalone(drm_fd, gpu_index, slices, timeout_ns,
                                  check_notifier, &res);
   if (drm_fd >= 0)
      close(drm_fd);

   if (r == 0) {
      fprintf(stderr, "nvidia_smoke_hw_cli: PASS slices=0x%x ok=0x%x\n",
              (unsigned)res.slices_run, (unsigned)res.slices_ok);
      return 0;
   }
   fprintf(stderr, "nvidia_smoke_hw_cli: FAIL rc=%d slices_run=0x%x ok=0x%x "
                   "(see nv_smoke_hw_standalone lines above)\n",
           r, (unsigned)res.slices_run, (unsigned)res.slices_ok);
   return 1;
}
