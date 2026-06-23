/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * GPU identification and capability tracking. Values originate from RM
 * NV2080_CTRL_MC_GET_ARCH_INFO / GR_GET_INFO / FB_GET_INFO queries.
 */

#ifndef NV_DEVICE_INFO_H
#define NV_DEVICE_INFO_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum nv_gpu_family {
   NV_GPU_FAMILY_UNKNOWN = 0,
   NV_GPU_FAMILY_KEPLER,
   NV_GPU_FAMILY_MAXWELL,
   NV_GPU_FAMILY_PASCAL,
   NV_GPU_FAMILY_VOLTA,
   NV_GPU_FAMILY_TURING,
   NV_GPU_FAMILY_AMPERE,
   NV_GPU_FAMILY_ADA,
   NV_GPU_FAMILY_HOPPER,
   NV_GPU_FAMILY_BLACKWELL,
};

/* RM architecture IDs from NV2080_CTRL_MC_GET_ARCH_INFO */
#define NV_ARCH_KEPLER    0x000000e0
#define NV_ARCH_MAXWELL   0x00000110
#define NV_ARCH_PASCAL    0x00000130
#define NV_ARCH_VOLTA     0x00000140
#define NV_ARCH_TURING    0x00000160
#define NV_ARCH_AMPERE    0x00000170
#define NV_ARCH_HOPPER    0x00000180
#define NV_ARCH_ADA       0x00000190
#define NV_ARCH_BLACKWELL 0x000001a0

struct nv_device_info {
   uint32_t gpu_id;
   uint32_t pci_vendor_id;
   uint32_t pci_device_id;
   uint32_t pci_domain;
   uint32_t pci_bus;
   uint32_t pci_dev;
   uint32_t pci_func;

   uint32_t architecture;     /* NV2080 MC architecture */
   uint32_t implementation;
   uint32_t revision;
   enum nv_gpu_family family;

   uint32_t sm_version;       /* e.g. 0x0705 for SM 7.5 (Turing) */
   uint32_t gpc_count;
   uint32_t tpc_count;
   uint32_t max_warps_per_sm;

   uint64_t vram_size_bytes;
   uint64_t vram_usable_bytes;
   uint64_t sysmem_visible_bytes;

   char name[64];
   char chipset_name[32];

   /* Class handles for channels / engines (filled during init) */
   uint32_t class_compute;
   uint32_t class_3d;
   uint32_t class_2d;
   uint32_t class_m2mf;
   uint32_t class_copy;
   uint32_t class_gpfifo;
   uint32_t class_sw;

   bool has_graphics;
   bool has_compute;
   bool has_video_decode;
   bool has_video_encode;
   bool is_tegra;
   bool gsp_mode;             /* GSP-RM firmware offload active */
};

enum nv_gpu_family nv_device_info_family_from_arch(uint32_t architecture);
const char *nv_device_info_family_name(enum nv_gpu_family family);
void nv_device_info_select_classes(struct nv_device_info *info);

#ifdef __cplusplus
}
#endif

#endif /* NV_DEVICE_INFO_H */
