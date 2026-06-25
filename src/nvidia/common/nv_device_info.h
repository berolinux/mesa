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
   /* tick101: FB_GET_INFO_V2 BAR1/heap/ECC (from libdrm nvidia_gpu_info) */
   uint64_t fb_heap_size;
   uint64_t fb_heap_start;
   uint64_t bar1_size;
   uint64_t bar1_avail_size;
   uint32_t fbpa_ecc_enabled;

   /* tick96: from nvidia_gpu_info refined probe */
   uint32_t fb_region_count;
   uint64_t fb_region0_base;
   uint64_t fb_region0_limit;
   uint64_t max_page_size;
   uint32_t rm_pci_device_id;
   uint32_t rm_pci_subsystem_id;
   uint32_t rm_pci_revision_id;
   /* tick98: NV0000 SYSTEM_GET_BUILD_VERSION / PLATFORM_TYPE */
   uint32_t rm_changelist;
   uint32_t rm_official_cl;
   uint32_t rm_platform_type;
   char rm_driver_version[64];
   char rm_build_branch[64];
   /* tick99: GPU_GET_GID_INFO UUID */
   char gpu_uuid[48];
   uint8_t gpu_gid_binary[16];
   uint32_t gpu_gid_binary_len;

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
   uint32_t class_nvdec;        /* NVDEC video decode engine */
   uint32_t class_nvenc;        /* NVENC video encode engine */

   bool has_graphics;
   bool has_compute;
   bool has_video_decode;
   bool has_video_encode;
   bool is_tegra;
   bool gsp_mode;             /* GSP-RM firmware offload active */
   bool classes_from_rm;      /* class IDs refined via GET_ENGINE_CLASSLIST */
};

enum nv_gpu_family nv_device_info_family_from_arch(uint32_t architecture);
const char *nv_device_info_family_name(enum nv_gpu_family family);
void nv_device_info_select_classes(struct nv_device_info *info);

/**
 * Refine class_3d / class_compute / class_copy from RM classlist entries.
 * class_list/count: results of GET_ENGINE_CLASSLIST for one engine.
 * engine_kind: 0=graphics/3d, 1=compute, 2=copy, 3=nvdec, 4=nvenc.
 * Picks highest class in the family band; does not clear family defaults if
 * no match.
 */
void nv_device_info_refine_class_from_list(struct nv_device_info *info,
                                          int engine_kind,
                                          const uint32_t *class_list,
                                          uint32_t count);

/**
 * Refine GPFIFO / channel class (0x*06f band) from a classlist (often GR or
 * first engine's list).  Highest matching ID wins; keeps family default if none.
 */
void nv_device_info_refine_gpfifo_from_list(struct nv_device_info *info,
                                            const uint32_t *class_list,
                                            uint32_t count);

/** One-line stderr summary of engine classes (bring-up / NV_SMOKE_HW_VERBOSE). */
void nv_device_info_log_classes(const struct nv_device_info *info,
                                const char *prefix);

/**
 * Newest-first class ladders from 610.43.02 binary RE + OGKM headers.
 * Writes up to *inout_n entries into out[]; sets *inout_n to count written.
 * engine_kind: 0=3d, 1=compute, 2=copy, 5=gpfifo (same as refine_* kinds + 5).
 * prefer_first: optional class to put at index 0 (e.g. refined device class).
 */
void nv_device_info_fill_class_ladder(int engine_kind, uint32_t prefer_first,
                                      uint32_t *out, unsigned *inout_n);

#ifdef __cplusplus
}
#endif

#endif /* NV_DEVICE_INFO_H */
