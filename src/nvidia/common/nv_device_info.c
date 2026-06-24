/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 */

#include "nv_device_info.h"
#include "nv_video_methods.h"

/* Channel / engine class IDs from open-gpu-kernel-modules class headers */
#define KEPLER_A_COMPUTE_A          0x0000a0c0
#define MAXWELL_COMPUTE_A           0x0000b0c0
#define MAXWELL_COMPUTE_B           0x0000b1c0
#define PASCAL_COMPUTE_A            0x0000c0c0
#define PASCAL_COMPUTE_B            0x0000c1c0
#define VOLTA_COMPUTE_A             0x0000c3c0
#define TURING_COMPUTE_A            0x0000c5c0
#define AMPERE_COMPUTE_A            0x0000c6c0
#define AMPERE_COMPUTE_B            0x0000c7c0
#define ADA_COMPUTE_A               0x0000c9c0
#define HOPPER_COMPUTE_A            0x0000cbc0

#define KEPLER_A_3D_A               0x0000a097
#define MAXWELL_A_3D_A              0x0000b097
#define MAXWELL_B_3D_B              0x0000b197
#define PASCAL_A_3D_A               0x0000c097
#define PASCAL_B_3D_B               0x0000c197
#define VOLTA_A_3D_A                0x0000c397
#define TURING_A_3D_A               0x0000c597
#define AMPERE_A_3D_A               0x0000c697
#define AMPERE_B_3D_B               0x0000c797
#define ADA_A_3D_A                  0x0000c997
#define HOPPER_A_3D_A               0x0000cb97

#define KEPLER_CHANNEL_GPFIFO_A     0x0000a06f
#define MAXWELL_CHANNEL_GPFIFO_A    0x0000b06f
#define PASCAL_CHANNEL_GPFIFO_A     0x0000c06f
#define VOLTA_CHANNEL_GPFIFO_A      0x0000c36f
#define TURING_CHANNEL_GPFIFO_A     0x0000c46f
#define AMPERE_CHANNEL_GPFIFO_A     0x0000c56f
#define HOPPER_CHANNEL_GPFIFO_A     0x0000c76f

#define FERMI_TWOD_A                0x0000902d
#define KEPLER_INLINE_TO_MEMORY_A   0x0000a040
#define KEPLER_INLINE_TO_MEMORY_B   0x0000a140
#define MAXWELL_DMA_COPY_A          0x0000b0b5
#define PASCAL_DMA_COPY_A           0x0000c0b5
#define PASCAL_DMA_COPY_B           0x0000c1b5
#define VOLTA_DMA_COPY_A            0x0000c3b5
#define TURING_DMA_COPY_A           0x0000c5b5
#define AMPERE_DMA_COPY_A           0x0000c6b5
#define AMPERE_DMA_COPY_B           0x0000c7b5
#define HOPPER_DMA_COPY_A           0x0000c8b5

enum nv_gpu_family
nv_device_info_family_from_arch(uint32_t architecture)
{
   switch (architecture) {
   case NV_ARCH_KEPLER:    return NV_GPU_FAMILY_KEPLER;
   case NV_ARCH_MAXWELL:   return NV_GPU_FAMILY_MAXWELL;
   case NV_ARCH_PASCAL:    return NV_GPU_FAMILY_PASCAL;
   case NV_ARCH_VOLTA:     return NV_GPU_FAMILY_VOLTA;
   case NV_ARCH_TURING:    return NV_GPU_FAMILY_TURING;
   case NV_ARCH_AMPERE:    return NV_GPU_FAMILY_AMPERE;
   case NV_ARCH_ADA:       return NV_GPU_FAMILY_ADA;
   case NV_ARCH_HOPPER:    return NV_GPU_FAMILY_HOPPER;
   case NV_ARCH_BLACKWELL: return NV_GPU_FAMILY_BLACKWELL;
   default:
      if (architecture >= NV_ARCH_BLACKWELL)
         return NV_GPU_FAMILY_BLACKWELL;
      if (architecture >= NV_ARCH_ADA)
         return NV_GPU_FAMILY_ADA;
      if (architecture >= NV_ARCH_HOPPER)
         return NV_GPU_FAMILY_HOPPER;
      if (architecture >= NV_ARCH_AMPERE)
         return NV_GPU_FAMILY_AMPERE;
      if (architecture >= NV_ARCH_TURING)
         return NV_GPU_FAMILY_TURING;
      if (architecture >= NV_ARCH_VOLTA)
         return NV_GPU_FAMILY_VOLTA;
      if (architecture >= NV_ARCH_PASCAL)
         return NV_GPU_FAMILY_PASCAL;
      if (architecture >= NV_ARCH_MAXWELL)
         return NV_GPU_FAMILY_MAXWELL;
      if (architecture >= NV_ARCH_KEPLER)
         return NV_GPU_FAMILY_KEPLER;
      return NV_GPU_FAMILY_UNKNOWN;
   }
}

const char *
nv_device_info_family_name(enum nv_gpu_family family)
{
   switch (family) {
   case NV_GPU_FAMILY_KEPLER:    return "Kepler";
   case NV_GPU_FAMILY_MAXWELL:   return "Maxwell";
   case NV_GPU_FAMILY_PASCAL:    return "Pascal";
   case NV_GPU_FAMILY_VOLTA:     return "Volta";
   case NV_GPU_FAMILY_TURING:    return "Turing";
   case NV_GPU_FAMILY_AMPERE:    return "Ampere";
   case NV_GPU_FAMILY_ADA:       return "Ada";
   case NV_GPU_FAMILY_HOPPER:    return "Hopper";
   case NV_GPU_FAMILY_BLACKWELL: return "Blackwell";
   default:                     return "Unknown";
   }
}

void
nv_device_info_select_classes(struct nv_device_info *info)
{
   if (!info)
      return;

   info->family = nv_device_info_family_from_arch(info->architecture);
   info->has_graphics = true;
   info->has_compute = true;
   info->class_2d = FERMI_TWOD_A;
   info->class_sw = 0x0000507f; /* NV50_MEMORY_TO_MEMORY_FORMAT-ish SW */

   switch (info->family) {
   case NV_GPU_FAMILY_KEPLER:
      info->class_compute = KEPLER_A_COMPUTE_A;
      info->class_3d = KEPLER_A_3D_A;
      info->class_gpfifo = KEPLER_CHANNEL_GPFIFO_A;
      info->class_copy = KEPLER_INLINE_TO_MEMORY_B;
      info->class_m2mf = KEPLER_INLINE_TO_MEMORY_A;
      break;
   case NV_GPU_FAMILY_MAXWELL:
      info->class_compute = MAXWELL_COMPUTE_B;
      info->class_3d = MAXWELL_B_3D_B;
      info->class_gpfifo = MAXWELL_CHANNEL_GPFIFO_A;
      info->class_copy = MAXWELL_DMA_COPY_A;
      info->class_m2mf = MAXWELL_DMA_COPY_A;
      break;
   case NV_GPU_FAMILY_PASCAL:
      info->class_compute = PASCAL_COMPUTE_B;
      info->class_3d = PASCAL_B_3D_B;
      info->class_gpfifo = PASCAL_CHANNEL_GPFIFO_A;
      info->class_copy = PASCAL_DMA_COPY_B;
      info->class_m2mf = PASCAL_DMA_COPY_B;
      break;
   case NV_GPU_FAMILY_VOLTA:
      info->class_compute = VOLTA_COMPUTE_A;
      info->class_3d = VOLTA_A_3D_A;
      info->class_gpfifo = VOLTA_CHANNEL_GPFIFO_A;
      info->class_copy = VOLTA_DMA_COPY_A;
      info->class_m2mf = VOLTA_DMA_COPY_A;
      break;
   case NV_GPU_FAMILY_TURING:
      info->class_compute = TURING_COMPUTE_A;
      info->class_3d = TURING_A_3D_A;
      info->class_gpfifo = TURING_CHANNEL_GPFIFO_A;
      info->class_copy = TURING_DMA_COPY_A;
      info->class_m2mf = TURING_DMA_COPY_A;
      break;
   case NV_GPU_FAMILY_AMPERE:
      info->class_compute = AMPERE_COMPUTE_B;
      info->class_3d = AMPERE_B_3D_B;
      info->class_gpfifo = AMPERE_CHANNEL_GPFIFO_A;
      info->class_copy = AMPERE_DMA_COPY_B;
      info->class_m2mf = AMPERE_DMA_COPY_B;
      break;
   case NV_GPU_FAMILY_ADA:
      info->class_compute = ADA_COMPUTE_A;
      info->class_3d = ADA_A_3D_A;
      info->class_gpfifo = AMPERE_CHANNEL_GPFIFO_A; /* Ada uses Ampere GPFIFO class family */
      info->class_copy = AMPERE_DMA_COPY_B;
      info->class_m2mf = AMPERE_DMA_COPY_B;
      break;
   case NV_GPU_FAMILY_HOPPER:
   case NV_GPU_FAMILY_BLACKWELL:
      info->class_compute = HOPPER_COMPUTE_A;
      info->class_3d = HOPPER_A_3D_A;
      info->class_gpfifo = HOPPER_CHANNEL_GPFIFO_A;
      info->class_copy = HOPPER_DMA_COPY_A;
      info->class_m2mf = HOPPER_DMA_COPY_A;
      info->gsp_mode = true;
      break;
   default:
      info->has_graphics = false;
      info->has_compute = false;
      break;
   }

   /* Video engines (NVDEC/NVENC) — class IDs from open-gpu-kernel-modules
    * class/cl*b0.h / cl*b7.h; selected by SM version when known. */
   if (info->has_graphics || info->has_compute) {
      uint8_t sm = (uint8_t)(info->sm_version ? info->sm_version : 0x75);
      info->class_nvdec = nv_video_pick_nvdec_class(sm);
      info->class_nvenc = nv_video_pick_nvenc_class(sm);
      info->has_video_decode = (info->class_nvdec != 0);
      info->has_video_encode = (info->class_nvenc != 0);
   }
}

void
nv_device_info_refine_class_from_list(struct nv_device_info *info,
                                     int engine_kind,
                                     const uint32_t *class_list,
                                     uint32_t count)
{
   uint32_t i, best = 0;
   uint32_t lo = 0, hi = 0xffffffffu;

   if (!info || !class_list || !count)
      return;

   /* Class ID bands (open-gpu-doc / class/cl* headers): 3D ~0x9000-0xC9A0,
    * compute ~0x90C0-0xC9C0, DMA copy ~0x90B5-0xC8B5, NVDEC ~0xB0B0-0xC7B0,
    * NVENC ~0x90B7-0xC4B7 — refine within band, prefer highest. */
   switch (engine_kind) {
   case 0: /* graphics / 3D */
      lo = 0x00009097u;
      hi = 0x0000c9a0u;
      break;
   case 1: /* compute */
      lo = 0x000090c0u;
      hi = 0x0000c9c0u;
      break;
   case 2: /* copy / dma */
      lo = 0x000090b5u;
      hi = 0x0000c8b5u;
      break;
   case 3: /* nvdec */
      lo = 0x0000b0b0u;
      hi = 0x0000c7b0u;
      break;
   case 4: /* nvenc */
      lo = 0x000090b7u;
      hi = 0x0000c4b7u;
      break;
   default:
      return;
   }

   for (i = 0; i < count; i++) {
      uint32_t c = class_list[i];
      if (c < lo || c > hi)
         continue;
      if (c > best)
         best = c;
   }
   if (!best)
      return;

   switch (engine_kind) {
   case 0:
      info->class_3d = best;
      info->has_graphics = true;
      break;
   case 1:
      info->class_compute = best;
      info->has_compute = true;
      break;
   case 2:
      info->class_copy = best;
      info->class_m2mf = best;
      break;
   case 3:
      info->class_nvdec = best;
      info->has_video_decode = true;
      break;
   case 4:
      info->class_nvenc = best;
      info->has_video_encode = true;
      break;
   }
   info->classes_from_rm = true;
}
