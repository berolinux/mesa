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
   /* tick102/104: NV2080 THREAD_STACK_SCALING_FACTOR (QMD stack / shmem policy) */
   uint32_t thread_stack_scaling;
   uint32_t max_threads_per_warp; /* GR probe; usually 32 */
   uint32_t max_sp_per_sm;
   uint32_t gpu_core_count;       /* total SM/core count when reported */
   /* tick106: NV2080 subdevice instance / count (MIG multi-USERD planning) */
   uint32_t subdevice_instance;
   uint32_t subdevice_count;
   /* tick107: NV0080 virtualization mode (NONE/NMOS/VGX/HOST_VGPU/HOST_VSGA) */
   uint32_t virtualization_mode;
   uint32_t is_grid_build;

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

/* NV0080_CTRL_GPU_VIRTUALIZATION_MODE_* (ctrl0080gpu.h) — mirrored for policy */
#define NV_VIRT_MODE_NONE       0x00000000u
#define NV_VIRT_MODE_NMOS       0x00000001u
#define NV_VIRT_MODE_VGX        0x00000002u  /* guest / passthrough-style */
#define NV_VIRT_MODE_HOST_VGPU  0x00000003u  /* host vGPU (HOST alias) */
#define NV_VIRT_MODE_HOST_VSGA  0x00000004u

/** tick108: true when RM reports guest/vGPU/GRID virtualization (not baremetal). */
static inline bool
nv_device_info_is_virtualized(const struct nv_device_info *info)
{
   if (!info)
      return false;
   return info->virtualization_mode != NV_VIRT_MODE_NONE || info->is_grid_build != 0;
}

/**
 * tick139 / pass13: map device SM / sass_version probe to SPA_VERSION byte.
 * sm_version is often 0xMMNN (major.minor BCD-ish, e.g. 0x0806 = SM 8.6) or a
 * single-byte sass/SPA code already (0x50..0x90).  Smoke default remains 0x53
 * (Ampere-era 5.3) when probe is missing — matches G3 channel_prep 5,3.
 *
 * Returns a single-byte SPA/SASS code suitable for SET_SPA_VERSION low byte
 * and QMD sass_version fields.
 */
static inline uint8_t
nv_device_info_spa_version_u8(const struct nv_device_info *info)
{
   uint32_t sm;
   uint8_t maj, min;

   if (!info || !info->sm_version)
      return 0x53u; /* pass12/13 smoke default (maj=5, min=3) */

   sm = info->sm_version;
   /* Already a compact code in typical SPA range (0x40..0xA0). */
   if (sm >= 0x40u && sm <= 0xA0u)
      return (uint8_t)(sm & 0xffu);
   /* BCD / nibble major.minor in low 16 (e.g. 0x0806, 0x0705, 0x0089). */
   if (sm <= 0xffffu) {
      maj = (uint8_t)((sm >> 8) & 0xffu);
      min = (uint8_t)(sm & 0xffu);
      if (maj == 0 && min != 0)
         return min; /* low byte only */
      if (maj >= 5u && maj <= 12u && min <= 15u)
         return (uint8_t)(((maj & 0xfu) << 4) | (min & 0xfu));
      /* decimal-style 0xMMNN where NN is two-digit minor (8.6 → 0x0806) */
      if (maj >= 5u && maj <= 12u && min <= 99u)
         return (uint8_t)(((maj & 0xfu) << 4) | ((min / 10u) & 0xfu));
   }
   return (uint8_t)(sm & 0xffu);
}

/** tick139: SPA major/minor for NVC597_SET_SPA_VERSION ((maj<<8)|min). */
static inline void
nv_device_info_spa_maj_min(const struct nv_device_info *info,
                           uint8_t *maj_out, uint8_t *min_out)
{
   uint8_t spa = nv_device_info_spa_version_u8(info);
   uint8_t maj = (uint8_t)((spa >> 4) & 0xfu);
   uint8_t min = (uint8_t)(spa & 0xfu);

   /* 0x53 → maj=5 min=3; 0x50 with zero minor nibbles still maj=5 min=0 */
   if (maj == 0 && spa >= 0x40u) {
      maj = (uint8_t)((spa >> 4) & 0xfu);
      if (maj == 0)
         maj = 5u;
   }
   if (maj == 0)
      maj = 5u;
   if (!min && spa == 0x53u)
      min = 3u;
   if (maj_out)
      *maj_out = maj;
   if (min_out)
      *min_out = min;
}

/** Alias: QMD / compute sass_version field uses same SPA-ish byte. */
static inline uint8_t
nv_device_info_sass_version_u8(const struct nv_device_info *info)
{
   return nv_device_info_spa_version_u8(info);
}

/**
 * tick108: prefer conservative sysmem/CPU-access paths when virtualized.
 * Guest/vGPU often has restricted BAR1 / limited GPU-direct allocs; host sema
 * and notifier rings should still work in sysmem WC.
 */
static inline bool
nv_device_info_prefer_sysmem_alloc(const struct nv_device_info *info)
{
   if (!info)
      return false;
   if (info->virtualization_mode == NV_VIRT_MODE_VGX)
      return true;
   if (info->virtualization_mode == NV_VIRT_MODE_HOST_VGPU ||
       info->virtualization_mode == NV_VIRT_MODE_HOST_VSGA)
      return info->is_grid_build != 0; /* GRID builds: favor sysmem for smoke/sema */
   return false;
}

/**
 * tick108: doorbell/token still required on Volta+; virtualization does not
 * disable it but may need longer ring-full stalls (caller multiplies timeout).
 */
static inline uint64_t
nv_device_info_gpfifo_stall_ns(const struct nv_device_info *info,
                              uint64_t default_ns)
{
   uint64_t d = default_ns ? default_ns : 1000000000ull;
   if (info && nv_device_info_is_virtualized(info)) {
      if (d < 2000000000ull)
         d = 2000000000ull; /* 2s minimum under virt */
   }
   return d;
}

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
