/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * NVDEC / NVENC class IDs and minimal method helpers derived from
 * open-gpu-kernel-modules class/cl*b0.h (decoder) and cl*b7.h (encoder).
 * Full bitstream/picture setup is generation-specific and implemented
 * incrementally; this header provides class selection and execute kickoffs.
 */
#ifndef NV_VIDEO_METHODS_H
#define NV_VIDEO_METHODS_H

#include <stdint.h>
#include <stdbool.h>

#include "nv_push.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Video class engine IDs (from class/clc*b0.h, clc*b7.h) */
#define NV_VIDEO_CLASS_NVDEC_PASCAL_B0   0x0000B0B0
#define NV_VIDEO_CLASS_NVDEC_VOLTA_B6    0x0000B6B0
#define NV_VIDEO_CLASS_NVDEC_TURING_B8   0x0000B8B0
#define NV_VIDEO_CLASS_NVDEC_AMPERE_C1   0x0000C1B0
#define NV_VIDEO_CLASS_NVDEC_ADA_C4      0x0000C4B0
#define NV_VIDEO_CLASS_NVDEC_HOPPER_C7   0x0000C7B0

#define NV_VIDEO_CLASS_NVENC_PASCAL_B4B7 0x0000B4B7
#define NV_VIDEO_CLASS_NVENC_TURING_C0B7 0x0000C0B7
#define NV_VIDEO_CLASS_NVENC_AMPERE_C1B7 0x0000C1B7

#define NV_NVDEC_SET_OBJECT              0x0000
#define NV_NVDEC_NOP                     0x0100
#define NV_NVDEC_SET_APPLICATION_ID      0x0200
#define NV_NVDEC_SET_WATCHDOG_TIMER      0x0204
#define NV_NVDEC_SEMAPHORE_A             0x0240
#define NV_NVDEC_SEMAPHORE_B             0x0244
#define NV_NVDEC_SEMAPHORE_C             0x0248
#define NV_NVDEC_EXECUTE                 0x0300

#define NV_NVENC_SET_OBJECT              0x0000
#define NV_NVENC_NOP                     0x0100
#define NV_NVENC_SET_APPLICATION_ID      0x0200
#define NV_NVENC_EXECUTE                 0x0300

#define NV_NVDEC_APP_ID_MPEG12           1
#define NV_NVDEC_APP_ID_VC1              2
#define NV_NVDEC_APP_ID_H264             3
#define NV_NVDEC_APP_ID_MPEG4            4
#define NV_NVDEC_APP_ID_VP8              5
#define NV_NVDEC_APP_ID_VP9              7
#define NV_NVDEC_APP_ID_HEVC             8
#define NV_NVDEC_APP_ID_AV1              11

#define NV_NVENC_APP_ID_H264             1
#define NV_NVENC_APP_ID_HEVC             2
#define NV_NVENC_APP_ID_AV1              3

/* Provisional subchannels; real channels allocate dedicated video engines */
#define NV_PUSH_SUBCH_NVDEC              4
#define NV_PUSH_SUBCH_NVENC              5

static inline uint32_t
nv_video_pick_nvdec_class(uint8_t sm_version)
{
   if (sm_version >= 0x90)
      return NV_VIDEO_CLASS_NVDEC_HOPPER_C7;
   if (sm_version >= 0x89)
      return NV_VIDEO_CLASS_NVDEC_ADA_C4;
   if (sm_version >= 0x80)
      return NV_VIDEO_CLASS_NVDEC_AMPERE_C1;
   if (sm_version >= 0x70)
      return NV_VIDEO_CLASS_NVDEC_TURING_B8;
   if (sm_version >= 0x60)
      return NV_VIDEO_CLASS_NVDEC_VOLTA_B6;
   return NV_VIDEO_CLASS_NVDEC_PASCAL_B0;
}

static inline uint32_t
nv_video_pick_nvenc_class(uint8_t sm_version)
{
   if (sm_version >= 0x80)
      return NV_VIDEO_CLASS_NVENC_AMPERE_C1B7;
   if (sm_version >= 0x70)
      return NV_VIDEO_CLASS_NVENC_TURING_C0B7;
   return NV_VIDEO_CLASS_NVENC_PASCAL_B4B7;
}

static inline void
nv_nvdec_set_object(struct nv_push *p, uint32_t class_nvdec)
{
   if (!p || !class_nvdec)
      return;
   nv_push_set_subch(p, NV_PUSH_SUBCH_NVDEC);
   nv_push_set_object(p, class_nvdec);
}

static inline void
nv_nvenc_set_object(struct nv_push *p, uint32_t class_nvenc)
{
   if (!p || !class_nvenc)
      return;
   nv_push_set_subch(p, NV_PUSH_SUBCH_NVENC);
   nv_push_set_object(p, class_nvenc);
}

static inline void
nv_nvdec_emit_execute(struct nv_push *p, uint32_t app_id, uint32_t execute_flags)
{
   if (!p)
      return;
   nv_push_method(p, NV_NVDEC_SET_APPLICATION_ID, app_id);
   nv_push_method(p, NV_NVDEC_EXECUTE, execute_flags);
}

static inline void
nv_nvenc_emit_execute(struct nv_push *p, uint32_t app_id, uint32_t execute_flags)
{
   if (!p)
      return;
   nv_push_method(p, NV_NVENC_SET_APPLICATION_ID, app_id);
   nv_push_method(p, NV_NVENC_EXECUTE, execute_flags);
}

#ifdef __cplusplus
}
#endif

#endif /* NV_VIDEO_METHODS_H */
