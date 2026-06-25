/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 */

#include "nv_device_info.h"
#include "nv_video_methods.h"

#include <stdio.h>

/* Channel / engine class IDs from open-gpu-kernel-modules class headers.
 * 610.43.02 glcore/cuda embed descending rodata ladders (pass11 RE @ glcore
 * 0x11bb600 / 0x1238b70).  Try newest first via refine_class_from_list /
 * nv_device_info_fill_class_ladder / channel engine alts.
 *
 * pass11 glcore primary ladders (newest→oldest, contiguous rodata arrays):
 *   DMA:    CAB5 C9B5 C8B5 C7B5 C6B5 C5B5 C3B5 C1B5 C0B5 B0B5
 *   COMP:   CEC0 CDC0 CBC0 C9C0 C7C0 C6C0 C5C0 C3C0 C1C0 C0C0 B1C0 B0C0
 *   3D:     CE97 CD97 CB97 C997 C797 C697 C597 C397 C197 C097 B197 B097
 *   GPFIFO: CA6F C96F C56F C46F C36F C06F B06F A06F A26F 906F 506F
 *   NVENC:  D1B7 CFB7 CEB7 C9B7 C8B7 C7B7 (B6B7 B4B7 C5B7 C4B7 side table)
 *   NVDEC:  C9B0..C4B0 (+ B8B0 in NVENC side table; CBB0/CAB0 in global hit map)
 */
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
/* pass11 glcore rodata: CEC0/CDC0 head of compute ladder (post-Blackwell stubs) */
#define BLACKWELL_COMPUTE_A         0x0000cdc0
#define POST_BLACKWELL_COMPUTE_A    0x0000cec0

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
#define BLACKWELL_3D_A              0x0000cd97
#define POST_BLACKWELL_3D_A         0x0000ce97

#define KEPLER_CHANNEL_GPFIFO_A     0x0000a06f
#define MAXWELL_CHANNEL_GPFIFO_A    0x0000b06f
#define PASCAL_CHANNEL_GPFIFO_A     0x0000c06f
#define VOLTA_CHANNEL_GPFIFO_A      0x0000c36f
#define TURING_CHANNEL_GPFIFO_A     0x0000c46f
#define AMPERE_CHANNEL_GPFIFO_A     0x0000c56f
#define HOPPER_CHANNEL_GPFIFO_A     0x0000c76f
/* pass11 glcore GPFIFO ladder head: CA6F C96F (cuda also has C86F in alt table) */
#define BLACKWELL_CHANNEL_GPFIFO_A  0x0000c86f
#define POST_BLACKWELL_CHANNEL_GPFIFO_A 0x0000c96f
#define POST_BLACKWELL_CHANNEL_GPFIFO_B 0x0000ca6f

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
/* pass11 glcore DMA ladder head: CAB5 C9B5 C8B5 … (contiguous @ 0x11bb600) */
#define BLACKWELL_DMA_COPY_A        0x0000c9b5
#define POST_BLACKWELL_DMA_COPY_A   0x0000cab5

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
      info->class_compute = HOPPER_COMPUTE_A;
      info->class_3d = HOPPER_A_3D_A;
      info->class_gpfifo = HOPPER_CHANNEL_GPFIFO_A;
      info->class_copy = HOPPER_DMA_COPY_A;
      info->class_m2mf = HOPPER_DMA_COPY_A;
      info->gsp_mode = true;
      break;
   case NV_GPU_FAMILY_BLACKWELL:
      /* Prefer newest IDs seen in 610 ladders; RM classlist refine upgrades further */
      info->class_compute = BLACKWELL_COMPUTE_A;
      info->class_3d = BLACKWELL_3D_A;
      info->class_gpfifo = BLACKWELL_CHANNEL_GPFIFO_A;
      info->class_copy = HOPPER_DMA_COPY_A; /* C8B5 solid in 610; refine may pick C9B5+ */
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

   /* Class ID bands (open-gpu-doc / class/cl* headers + 610.43.02 binary ladders):
    * 3D ~0x9097-0xCFA0, compute ~0x90C0-0xCFC0, DMA copy ~0x90B5-0xCFB5,
    * NVDEC ~0xB0B0-0xCFB0, NVENC ~0x90B7-0xCFB7 — refine within band, prefer highest. */
   switch (engine_kind) {
   case 0: /* graphics / 3D */
      lo = 0x00009097u;
      hi = 0x0000cfa0u;
      break;
   case 1: /* compute */
      lo = 0x000090c0u;
      hi = 0x0000cfc0u;
      break;
   case 2: /* copy / dma */
      lo = 0x000090b5u;
      hi = 0x0000cfb5u;
      break;
   case 3: /* nvdec */
      lo = 0x0000b0b0u;
      hi = 0x0000cfb0u;
      break;
   case 4: /* nvenc */
      lo = 0x000090b7u;
      hi = 0x0000cfb7u;
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

void
nv_device_info_refine_gpfifo_from_list(struct nv_device_info *info,
                                       const uint32_t *class_list,
                                       uint32_t count)
{
   uint32_t i, best = 0;
   /* Kepler..Hopper GPFIFO: 0xA06F .. 0xC76F (and later in same low-byte 0x6f) */
   const uint32_t lo = 0x0000a06fu;
   const uint32_t hi = 0x0000cf6fu;

   if (!info || !class_list || !count)
      return;

   for (i = 0; i < count; i++) {
      uint32_t c = class_list[i];
      if (c < lo || c > hi)
         continue;
      /* GPFIFO classes end with 0x6f in class ID (open-gpu class headers) */
      if ((c & 0xffu) != 0x6fu)
         continue;
      if (c > best)
         best = c;
   }
   if (best) {
      info->class_gpfifo = best;
      info->classes_from_rm = true;
   }
}

void
nv_device_info_log_classes(const struct nv_device_info *info, const char *prefix)
{
   const char *p = prefix ? prefix : "nv_device_info";
   if (!info)
      return;
   fprintf(stderr,
           "%s: arch=0x%x sm=%u classes_from_rm=%d "
           "gpfifo=0x%x 3d=0x%x compute=0x%x copy=0x%x nvdec=0x%x nvenc=0x%x "
           "gfx=%d compute_ok=%d\n",
           p,
           (unsigned)info->architecture,
           (unsigned)info->sm_version,
           (int)info->classes_from_rm,
           (unsigned)info->class_gpfifo,
           (unsigned)info->class_3d,
           (unsigned)info->class_compute,
           (unsigned)info->class_copy,
           (unsigned)info->class_nvdec,
           (unsigned)info->class_nvenc,
           (int)info->has_graphics,
           (int)info->has_compute);
}

void
nv_device_info_fill_class_ladder(int engine_kind, uint32_t prefer_first,
                                 uint32_t *out, unsigned *inout_n)
{
   /*
    * Newest-first ladders.  pass11/12 (610.43.02 glcore rodata @ 0x11bb600 /
    * 0x1238b70) is authoritative for contiguous engine arrays; pass9 counts
    * and pass5–8 imm scans remain as secondary sources for intermediate IDs
    * not present in the primary glcore arrays (e.g. C4B5, C8C0, C66F).
    *
    * pass12: primary GPFIFO has no C86F/C76F/C66F/506F (alts only); NVENC
    * rodata is oldest-first C5B7..D1B7 (mesa still prefers newest-first).
    * NVDEC has no single clean contiguous ladder; side table C4B0..C9B0 + alts.
    */
   /* pass11 glcore @ 0x11bb600: CAB5..B0B5 (no C4B5 in primary; keep as alt) */
   static const uint32_t ladder_copy[] = {
      0x0000cab5u, 0x0000c9b5u, 0x0000c8b5u, 0x0000c7b5u, 0x0000c6b5u,
      0x0000c5b5u, 0x0000c4b5u, 0x0000c3b5u, 0x0000c1b5u, 0x0000c0b5u,
      0x0000b0b5u, 0x0000a0b5u,
   };
   /* pass11 glcore @ 0x11bb640: CEC0..B0C0; pass9 cuda intermediates C8C0/CAC0 */
   static const uint32_t ladder_compute[] = {
      0x0000cec0u, 0x0000cdc0u, 0x0000ccc0u, 0x0000cbc0u, 0x0000cac0u,
      0x0000c9c0u, 0x0000c8c0u, 0x0000c7c0u, 0x0000c6c0u, 0x0000c5c0u,
      0x0000c4c0u, 0x0000c3c0u, 0x0000c1c0u, 0x0000c0c0u, 0x0000b1c0u,
      0x0000b0c0u,
   };
   /* pass11 glcore @ 0x11bb680: CE97..B097; pass9 also CC97/CA97/C897/C497 */
   static const uint32_t ladder_3d[] = {
      0x0000ce97u, 0x0000cd97u, 0x0000cc97u, 0x0000cb97u, 0x0000ca97u,
      0x0000c997u, 0x0000c897u, 0x0000c797u, 0x0000c697u, 0x0000c597u,
      0x0000c497u, 0x0000c397u, 0x0000c197u, 0x0000c097u, 0x0000b197u,
      0x0000b097u,
   };
   /* pass11 glcore @ 0x1238b70: CA6F C96F C56F..506F; cuda alt C86F; pass9 C66F/C36E */
   static const uint32_t ladder_gpfifo[] = {
      0x0000ca6fu, 0x0000c96fu, 0x0000c86fu, 0x0000c76fu, 0x0000c66fu,
      0x0000c56fu, 0x0000c46fu, 0x0000c36fu, 0x0000c36eu, 0x0000c06fu,
      0x0000b06fu, 0x0000a26fu, 0x0000a16fu, 0x0000a06fu, 0x0000906fu,
   };
   /* pass14 glcore @ 0x1238bf0: D1B0 CFB0 CEB0 CDB0 C9B0..; pass11 alts CBB0/CAB0/B8B0 */
   static const uint32_t ladder_nvdec[] = {
      0x0000d1b0u, 0x0000cfb0u, 0x0000ceb0u, 0x0000cdb0u, 0x0000cbb0u,
      0x0000cab0u, 0x0000c9b0u, 0x0000c8b0u, 0x0000c7b0u, 0x0000c6b0u,
      0x0000c5b0u, 0x0000c4b0u, 0x0000c3b0u, 0x0000c2b0u, 0x0000c1b0u,
      0x0000c0b0u, 0x0000b8b0u, 0x0000b6b0u, 0x0000b0b0u, 0x0000a0b0u,
   };
   /* pass11 glcore @ 0x1238bb4: D1B7 CFB7 CEB7 C9B7 C8B7 C7B7 + C5B7 C4B7 B6B7 B4B7 */
   static const uint32_t ladder_nvenc[] = {
      0x0000d1b7u, 0x0000cfb7u, 0x0000ceb7u, 0x0000c9b7u, 0x0000c8b7u,
      0x0000c7b7u, 0x0000c6b7u, 0x0000c5b7u, 0x0000c4b7u, 0x0000c1b7u,
      0x0000c0b7u, 0x0000b6b7u, 0x0000b4b7u,
   };
   const uint32_t *lad = NULL;
   unsigned lad_n = 0, max_n, i, n = 0;

   if (!out || !inout_n || !*inout_n)
      return;
   max_n = *inout_n;

   switch (engine_kind) {
   case 0:
      lad = ladder_3d;
      lad_n = sizeof(ladder_3d) / sizeof(ladder_3d[0]);
      break;
   case 1:
      lad = ladder_compute;
      lad_n = sizeof(ladder_compute) / sizeof(ladder_compute[0]);
      break;
   case 2:
      lad = ladder_copy;
      lad_n = sizeof(ladder_copy) / sizeof(ladder_copy[0]);
      break;
   case 3: /* nvdec — tick89 / pass9 video ladder */
      lad = ladder_nvdec;
      lad_n = sizeof(ladder_nvdec) / sizeof(ladder_nvdec[0]);
      break;
   case 4: /* nvenc */
      lad = ladder_nvenc;
      lad_n = sizeof(ladder_nvenc) / sizeof(ladder_nvenc[0]);
      break;
   case 5:
      lad = ladder_gpfifo;
      lad_n = sizeof(ladder_gpfifo) / sizeof(ladder_gpfifo[0]);
      break;
   default:
      *inout_n = 0;
      return;
   }

   if (prefer_first && n < max_n)
      out[n++] = prefer_first;
   for (i = 0; i < lad_n && n < max_n; i++) {
      unsigned t;
      uint32_t c = lad[i];
      if (!c)
         continue;
      for (t = 0; t < n; t++)
         if (out[t] == c)
            break;
      if (t < n)
         continue;
      out[n++] = c;
   }
   *inout_n = n;
}
