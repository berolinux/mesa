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



/* Common NVDEC method offsets (stable across Pascal+ class/cl*b0 families;
 * values from proprietary/class headers patterns used by the binary driver). */
#define NV_NVDEC_SET_CONTROL_PARAMS          0x0400
#define NV_NVDEC_SET_DRV_PIC_SETUP_OFFSET_NV 0x0404
#define NV_NVDEC_SET_IN_BUF_BASE_OFFSET      0x0408
#define NV_NVDEC_SET_PICTURE_INDEX           0x040c
#define NV_NVDEC_SET_SLICE_OFFSETS_BUF_OFFSET 0x0410
#define NV_NVDEC_SET_COLOC_DATA_OFFSET       0x0414
#define NV_NVDEC_SET_HISTORY_OFFSET          0x0418
#define NV_NVDEC_SET_DISPLAY_BUF_SIZE        0x041c
#define NV_NVDEC_SET_HISTOGRAM_OFFSET        0x0420
#define NV_NVDEC_SET_NVDEC_STATUS_OFFSET     0x0424
#define NV_NVDEC_EXECUTE_AWAKEN              (1u << 8)

/* H.264/HEVC/AV1 picture setup is a driver-private structure written to a GPU
 * BO; the class only receives its GPU offset via SET_DRV_PIC_SETUP_OFFSET_NV.
 * We expose a minimal descriptor for userspace to populate before execute. */
struct nv_nvdec_frame_setup {
   uint32_t app_id;              /* NV_NVDEC_APP_ID_* */
   uint32_t picture_index;
   uint32_t control_params;      /* codec-specific control dword */
   uint64_t bitstream_gpu_addr;  /* compressed slice/NAL buffer */
   uint32_t bitstream_size;
   uint64_t pic_setup_gpu_addr;  /* driver picture setup BO (codec struct) */
   uint64_t slice_offsets_gpu_addr;
   uint64_t coloc_gpu_addr;      /* H.264 colocated MV data (optional) */
   uint64_t history_gpu_addr;    /* VP8/VP9/AV1 history (optional) */
   uint64_t status_gpu_addr;     /* NVDEC status/report BO */
   uint32_t display_buf_size;
   uint32_t execute_flags;
};

/* Map Vulkan/VA-API style codec tags to NVDEC application ID */
static inline uint32_t
nv_nvdec_app_id_from_codec(uint32_t fourcc_or_tag)
{
   switch (fourcc_or_tag) {
   case 0x34363248: /* 'H264' LE as H264 */
   case 3:  return NV_NVDEC_APP_ID_H264;
   case 0x43564548: /* 'HEVC' */
   case 8:  return NV_NVDEC_APP_ID_HEVC;
   case 0x20315641: /* 'AV1 ' */
   case 11: return NV_NVDEC_APP_ID_AV1;
   case 0x00395056: /* 'VP9\0' */
   case 7:  return NV_NVDEC_APP_ID_VP9;
   case 0x00385056: /* 'VP8\0' */
   case 5:  return NV_NVDEC_APP_ID_VP8;
   case 0x3332504d: /* 'MP23' mpeg2 */
   case 1:  return NV_NVDEC_APP_ID_MPEG12;
   default: return NV_NVDEC_APP_ID_H264;
   }
}

/**
 * Program NVDEC frame offsets + execute.  pic_setup BO must already contain
 * the codec-specific picture parameter block (binary-driver layout TBD per
 * codec; structure size/offsets refined incrementally from class traces).
 */
static inline void
nv_nvdec_emit_frame_setup(struct nv_push *p, const struct nv_nvdec_frame_setup *fs)
{
   uint32_t app;
   if (!p || !fs)
      return;
   app = fs->app_id ? fs->app_id : NV_NVDEC_APP_ID_H264;
   nv_push_method(p, NV_NVDEC_SET_APPLICATION_ID, app);
   if (fs->control_params)
      nv_push_method(p, NV_NVDEC_SET_CONTROL_PARAMS, fs->control_params);
   if (fs->pic_setup_gpu_addr) {
      nv_push_method(p, NV_NVDEC_SET_DRV_PIC_SETUP_OFFSET_NV,
                     (uint32_t)(fs->pic_setup_gpu_addr >> 8));
   }
   if (fs->bitstream_gpu_addr) {
      nv_push_method(p, NV_NVDEC_SET_IN_BUF_BASE_OFFSET,
                     (uint32_t)(fs->bitstream_gpu_addr >> 8));
   }
   nv_push_method(p, NV_NVDEC_SET_PICTURE_INDEX, fs->picture_index);
   if (fs->slice_offsets_gpu_addr) {
      nv_push_method(p, NV_NVDEC_SET_SLICE_OFFSETS_BUF_OFFSET,
                     (uint32_t)(fs->slice_offsets_gpu_addr >> 8));
   }
   if (fs->coloc_gpu_addr) {
      nv_push_method(p, NV_NVDEC_SET_COLOC_DATA_OFFSET,
                     (uint32_t)(fs->coloc_gpu_addr >> 8));
   }
   if (fs->history_gpu_addr) {
      nv_push_method(p, NV_NVDEC_SET_HISTORY_OFFSET,
                     (uint32_t)(fs->history_gpu_addr >> 8));
   }
   if (fs->display_buf_size)
      nv_push_method(p, NV_NVDEC_SET_DISPLAY_BUF_SIZE, fs->display_buf_size);
   if (fs->status_gpu_addr) {
      nv_push_method(p, NV_NVDEC_SET_NVDEC_STATUS_OFFSET,
                     (uint32_t)(fs->status_gpu_addr >> 8));
   }
   nv_push_method(p, NV_NVDEC_EXECUTE,
                  fs->execute_flags ? fs->execute_flags : 1u);
   nv_push_wfi(p);
}

/* NVENC common offsets (class/cl*b7 families) */
#define NV_NVENC_SET_CONTROL_PARAMS          0x0400
#define NV_NVENC_SET_PIC_SETUP_OFFSET        0x0404
#define NV_NVENC_SET_IN_BUF_BASE_OFFSET      0x0408
#define NV_NVENC_SET_BITSTREAM_BUF_OFFSET    0x040c
#define NV_NVENC_SET_RC_OFFSET               0x0410
#define NV_NVENC_SET_STATUS_OFFSET           0x0414

struct nv_nvenc_frame_setup {
   uint32_t app_id;
   uint32_t control_params;
   uint64_t pic_setup_gpu_addr;
   uint64_t input_yuv_gpu_addr;
   uint64_t bitstream_out_gpu_addr;
   uint64_t rc_gpu_addr;
   uint64_t status_gpu_addr;
   uint32_t execute_flags;
};

static inline void
nv_nvenc_emit_frame_setup(struct nv_push *p, const struct nv_nvenc_frame_setup *fs)
{
   uint32_t app;
   if (!p || !fs)
      return;
   app = fs->app_id ? fs->app_id : NV_NVENC_APP_ID_H264;
   nv_push_method(p, NV_NVENC_SET_APPLICATION_ID, app);
   if (fs->control_params)
      nv_push_method(p, NV_NVENC_SET_CONTROL_PARAMS, fs->control_params);
   if (fs->pic_setup_gpu_addr)
      nv_push_method(p, NV_NVENC_SET_PIC_SETUP_OFFSET,
                     (uint32_t)(fs->pic_setup_gpu_addr >> 8));
   if (fs->input_yuv_gpu_addr)
      nv_push_method(p, NV_NVENC_SET_IN_BUF_BASE_OFFSET,
                     (uint32_t)(fs->input_yuv_gpu_addr >> 8));
   if (fs->bitstream_out_gpu_addr)
      nv_push_method(p, NV_NVENC_SET_BITSTREAM_BUF_OFFSET,
                     (uint32_t)(fs->bitstream_out_gpu_addr >> 8));
   if (fs->rc_gpu_addr)
      nv_push_method(p, NV_NVENC_SET_RC_OFFSET,
                     (uint32_t)(fs->rc_gpu_addr >> 8));
   if (fs->status_gpu_addr)
      nv_push_method(p, NV_NVENC_SET_STATUS_OFFSET,
                     (uint32_t)(fs->status_gpu_addr >> 8));
   nv_push_method(p, NV_NVENC_EXECUTE,
                  fs->execute_flags ? fs->execute_flags : 1u);
   nv_push_wfi(p);
}


#ifdef __cplusplus
}
#endif

#endif /* NV_VIDEO_METHODS_H */
