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

/* clock_gettime / CLOCK_MONOTONIC for sema wait helpers */
#if !defined(_POSIX_C_SOURCE) || (_POSIX_C_SOURCE < 200809L)
#undef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <time.h>

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
/* pass9 rodata: C9B0/C8B0/C6B0/C5B0/C3B0 also appear (try via class ladder) */
#define NV_VIDEO_CLASS_NVDEC_C9B0        0x0000C9B0
#define NV_VIDEO_CLASS_NVDEC_C8B0        0x0000C8B0
#define NV_VIDEO_CLASS_NVDEC_C6B0        0x0000C6B0
#define NV_VIDEO_CLASS_NVDEC_C5B0        0x0000C5B0
#define NV_VIDEO_CLASS_NVDEC_C3B0        0x0000C3B0

#define NV_VIDEO_CLASS_NVENC_PASCAL_B4B7 0x0000B4B7
#define NV_VIDEO_CLASS_NVENC_TURING_C0B7 0x0000C0B7
#define NV_VIDEO_CLASS_NVENC_AMPERE_C1B7 0x0000C1B7
/* pass9: C8B7 dominant in gl/egl/vksc; C9B7/C7B7 present */
#define NV_VIDEO_CLASS_NVENC_C9B7        0x0000C9B7
#define NV_VIDEO_CLASS_NVENC_C8B7        0x0000C8B7
#define NV_VIDEO_CLASS_NVENC_C7B7        0x0000C7B7

#define NV_NVDEC_SET_OBJECT              0x0000
#define NV_NVDEC_NOP                     0x0100
#define NV_NVDEC_SET_APPLICATION_ID      0x0200
#define NV_NVDEC_SET_WATCHDOG_TIMER      0x0204
#define NV_NVDEC_SEMAPHORE_A             0x0240
#define NV_NVDEC_SEMAPHORE_B             0x0244
#define NV_NVDEC_SEMAPHORE_C             0x0248
/* tick98: common NVDEC picture/bitstream method offs (class family *B0; provisional) */
#define NV_NVDEC_SET_CONTROL_PARAMS      0x0400
#define NV_NVDEC_SET_DRV_PIC_SETUP_OFFSET 0x0404
#define NV_NVDEC_SET_IN_BUF_BASE_OFFSET  0x0408
#define NV_NVDEC_SET_PICTURE_INDEX       0x040c
#define NV_NVDEC_SET_SLICE_OFFSETS_BUF_OFFSET 0x0410
#define NV_NVDEC_SET_COLOC_DATA_OFFSET   0x0414
#define NV_NVDEC_SET_HISTORY_OFFSET      0x0418
#define NV_NVDEC_SET_DISPLAY_BUF_SIZE    0x041c
#define NV_NVDEC_SET_HISTOGRAM_OFFSET    0x0420
#define NV_NVDEC_EXECUTE                 0x0300

#define NV_NVENC_SET_OBJECT              0x0000
#define NV_NVENC_NOP                     0x0100
#define NV_NVENC_SET_APPLICATION_ID      0x0200
#define NV_NVENC_SET_CONTROL_PARAMS      0x0400
#define NV_NVENC_SET_DRV_PIC_SETUP_OFFSET 0x0404
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
   /* Prefer newest class in pass9 ladder; RmAlloc ladder tries alternates. */
   if (sm_version >= 0x90)
      return NV_VIDEO_CLASS_NVDEC_C9B0;
   if (sm_version >= 0x89)
      return NV_VIDEO_CLASS_NVDEC_C8B0;
   if (sm_version >= 0x87)
      return NV_VIDEO_CLASS_NVDEC_HOPPER_C7;
   if (sm_version >= 0x86)
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
   if (sm_version >= 0x90)
      return NV_VIDEO_CLASS_NVENC_C9B7;
   if (sm_version >= 0x80)
      return NV_VIDEO_CLASS_NVENC_C8B7;
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

/**
 * tick98: minimal NVDEC picture/bitstream buffer setup (offsets are GPU VAs
 * in the video engine address space / mapped via CTXDMA).  Generation-specific
 * control struct at pic_setup_gpu is caller-built; we only emit method sequence.
 */
struct nv_nvdec_pic_setup {
   uint32_t app_id;            /* NV_NVDEC_APP_ID_* */
   uint32_t picture_index;
   uint64_t pic_setup_gpu;     /* driver picture setup struct */
   uint64_t bitstream_gpu;     /* in-buf / bitstream base */
   uint64_t slice_offsets_gpu; /* optional slice offset table */
   uint64_t coloc_gpu;         /* optional coloc / history */
   uint64_t history_gpu;
   uint32_t display_buf_size;
   uint32_t execute_flags;
   uint32_t control_params;    /* SET_CONTROL_PARAMS dword (codec flags) */
};

static inline void
nv_nvdec_emit_pic_setup(struct nv_push *p, const struct nv_nvdec_pic_setup *s)
{
   if (!p || !s)
      return;
   nv_push_method(p, NV_NVDEC_SET_APPLICATION_ID, s->app_id);
   if (s->control_params)
      nv_push_method(p, NV_NVDEC_SET_CONTROL_PARAMS, s->control_params);
   if (s->pic_setup_gpu) {
      nv_push_method(p, NV_NVDEC_SET_DRV_PIC_SETUP_OFFSET,
                     (uint32_t)(s->pic_setup_gpu >> 8)); /* often 256B units; tune on HW */
   }
   if (s->bitstream_gpu) {
      nv_push_method(p, NV_NVDEC_SET_IN_BUF_BASE_OFFSET,
                     (uint32_t)(s->bitstream_gpu >> 8));
   }
   nv_push_method(p, NV_NVDEC_SET_PICTURE_INDEX, s->picture_index);
   if (s->slice_offsets_gpu) {
      nv_push_method(p, NV_NVDEC_SET_SLICE_OFFSETS_BUF_OFFSET,
                     (uint32_t)(s->slice_offsets_gpu >> 8));
   }
   if (s->coloc_gpu) {
      nv_push_method(p, NV_NVDEC_SET_COLOC_DATA_OFFSET,
                     (uint32_t)(s->coloc_gpu >> 8));
   }
   if (s->history_gpu) {
      nv_push_method(p, NV_NVDEC_SET_HISTORY_OFFSET,
                     (uint32_t)(s->history_gpu >> 8));
   }
   if (s->display_buf_size)
      nv_push_method(p, NV_NVDEC_SET_DISPLAY_BUF_SIZE, s->display_buf_size);
}

/** NVDEC completion sema (SEMAPHORE_A/B/C) before EXECUTE — payload = done marker. */
static inline void
nv_nvdec_emit_semaphore_release(struct nv_push *p, uint64_t sema_gpu_addr,
                                uint32_t payload)
{
   if (!p || !sema_gpu_addr)
      return;
   nv_push_method(p, NV_NVDEC_SEMAPHORE_A,
                  (uint32_t)(sema_gpu_addr >> 32) & 0xff);
   nv_push_method(p, NV_NVDEC_SEMAPHORE_B,
                  (uint32_t)(sema_gpu_addr & ~0x3u));
   nv_push_method(p, NV_NVDEC_SEMAPHORE_C, payload);
}

/** Zero host-mapped status/sema before submit (clear stale completion). */
static inline void
nv_nvdec_status_reset_cpu(volatile uint32_t *status_cpu)
{
   if (status_cpu)
      *status_cpu = 0;
}

/**
 * Full vertical-slice NVDEC sema path: reset status, emit sema release method
 * with payload, caller then EXECUTE and polls wait_status_cpu / wait_status_geq.
 */
static inline void
nv_nvdec_emit_semaphore_release_reset(struct nv_push *p,
                                      uint64_t sema_gpu_addr,
                                      uint32_t payload,
                                      volatile uint32_t *status_cpu)
{
   nv_nvdec_status_reset_cpu(status_cpu);
   nv_nvdec_emit_semaphore_release(p, sema_gpu_addr, payload);
}

/** SET_OBJECT + pic setup + sema + EXECUTE — full vertical slice without codec payload. */
static inline void
nv_nvdec_emit_frame_kick(struct nv_push *p, uint32_t class_nvdec,
                         const struct nv_nvdec_pic_setup *s,
                         uint64_t sema_gpu, uint32_t sema_payload,
                         volatile uint32_t *status_cpu)
{
   if (!p)
      return;
   if (class_nvdec)
      nv_nvdec_set_object(p, class_nvdec);
   if (s)
      nv_nvdec_emit_pic_setup(p, s);
   if (sema_gpu)
      nv_nvdec_emit_semaphore_release_reset(p, sema_gpu, sema_payload, status_cpu);
   if (s)
      nv_push_method(p, NV_NVDEC_EXECUTE, s->execute_flags);
   else
      nv_push_method(p, NV_NVDEC_EXECUTE, 0);
}

/**
 * Poll host-mapped status/sema dword until it equals expected (or timeout).
 * NVDEC drivers often write status BO via sema; zero init then wait for payload.
 * Returns 0 on success, -ETIMEDOUT, -EINVAL.
 */
static inline int
nv_nvdec_wait_status_cpu(volatile uint32_t *status_cpu, uint32_t expected,
                         uint64_t timeout_ns)
{
   struct timespec ts;
   uint64_t start_ns = 0, now_ns, deadline_ns;

   if (!status_cpu)
      return -EINVAL;
   if (timeout_ns) {
      if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
         start_ns = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
      deadline_ns = start_ns + timeout_ns;
   } else {
      deadline_ns = 0;
   }
   for (;;) {
      if (*status_cpu == expected)
         return 0;
      if (!timeout_ns)
         return -EAGAIN;
      if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
         return -ETIMEDOUT;
      now_ns = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
      if (now_ns >= deadline_ns)
         return -ETIMEDOUT;
   }
}

/** Wait until status_cpu[0] >= expected (GEQ, for incremental pic_idx payloads). */
static inline int
nv_nvdec_wait_status_geq(volatile uint32_t *status_cpu, uint32_t expected,
                         uint64_t timeout_ns)
{
   struct timespec ts;
   uint64_t start_ns = 0, now_ns, deadline_ns;

   if (!status_cpu)
      return -EINVAL;
   if (!expected)
      return 0;
   if (timeout_ns) {
      if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
         start_ns = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
      deadline_ns = start_ns + timeout_ns;
   } else {
      deadline_ns = 0;
   }
   for (;;) {
      if (*status_cpu >= expected)
         return 0;
      if (!timeout_ns)
         return -EAGAIN;
      if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
         return -ETIMEDOUT;
      now_ns = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
      if (now_ns >= deadline_ns)
         return -ETIMEDOUT;
   }
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

/* Forward: max DPB slots (also defined with pic_setup dword indices below) */
#ifndef NV_H264_PS_MAX_DPB_SLOTS
#define NV_H264_PS_MAX_DPB_SLOTS             16
#endif

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
   /* Optional output surfaces (display/decoded YUV planes, 256B-aligned offsets) */
   uint64_t output_luma_gpu_addr;
   uint64_t output_chroma_gpu_addr;
   uint32_t output_luma_pitch;
   uint32_t output_chroma_pitch;
   uint32_t mb_width;            /* macroblock / CTB width hint for pic_setup */
   uint32_t mb_height;
   uint32_t bit_depth_luma_minus8;
   uint32_t bit_depth_chroma_minus8;
   /* tick101: H.264/HEVC DPB reference surfaces (GPU addrs >>8 in pic_setup) */
   uint32_t num_refl0;
   uint32_t num_refl1;
   uint8_t  refl0_idx[NV_H264_PS_MAX_DPB_SLOTS];
   uint8_t  refl1_idx[NV_H264_PS_MAX_DPB_SLOTS];
   uint64_t dpb_luma_gpu_addr[NV_H264_PS_MAX_DPB_SLOTS];
   uint64_t dpb_chroma_gpu_addr[NV_H264_PS_MAX_DPB_SLOTS];
   uint32_t dpb_slot_count;      /* active DPB entries (0 = none) */
   /* tick103: H.264 SPS/PPS subset for pic_setup dwords 2/3 (refine on silicon) */
   uint32_t h264_profile_idc;    /* e.g. 66/77/100 baseline/main/high */
   uint32_t h264_level_idc;      /* e.g. 41 = 4.1 */
   uint32_t h264_chroma_format_idc; /* 0=mono 1=420 2=422 3=444 */
   uint32_t h264_sps_flags;      /* direct override; 0 = build from fields above */
   uint32_t h264_pps_flags;      /* direct override; 0 = build from cabac/wp fields */
   uint8_t  h264_entropy_coding_mode_flag; /* 1 = CABAC */
   uint8_t  h264_weighted_pred_flag;
   uint8_t  h264_weighted_bipred_idc; /* 0..2 */
   uint8_t  h264_transform_8x8_mode_flag;
   uint8_t  h264_constrained_intra_pred_flag;
   uint8_t  h264_deblocking_filter_control_present_flag;
   int8_t   h264_pic_init_qp_minus26;
   int8_t   h264_chroma_qp_index_offset;
   /* tick104: HEVC / AV1 SPS/PPS subset (refine bit layout on silicon) */
   uint32_t hevc_sps_flags;      /* direct override; 0 = build from fields */
   uint32_t hevc_pps_flags;
   uint8_t  hevc_log2_min_luma_coding_block_size_minus3; /* +3 => min CB size */
   uint8_t  hevc_log2_diff_max_min_luma_coding_block_size;
   uint8_t  hevc_max_transform_hierarchy_depth_inter;
   uint8_t  hevc_max_transform_hierarchy_depth_intra;
   uint8_t  hevc_pcm_enabled_flag;
   uint8_t  hevc_amp_enabled_flag;
   uint8_t  hevc_sao_enabled_flag;
   uint8_t  hevc_temporal_mvp_enabled_flag;
   uint8_t  hevc_strong_intra_smoothing_enabled_flag;
   uint8_t  hevc_dependent_slice_segments_enabled_flag;
   uint8_t  hevc_sign_data_hiding_enabled_flag;
   uint8_t  hevc_cabac_init_present_flag;
   uint8_t  hevc_weighted_pred_flag;
   uint8_t  hevc_weighted_bipred_flag;
   uint8_t  hevc_transquant_bypass_enabled_flag;
   uint8_t  hevc_tiles_enabled_flag;
   uint8_t  hevc_entropy_coding_sync_enabled_flag;
   uint8_t  hevc_loop_filter_across_tiles_enabled_flag;
   uint8_t  hevc_pps_loop_filter_across_slices_enabled_flag;
   uint8_t  hevc_deblocking_filter_override_enabled_flag;
   int8_t   hevc_pps_cb_qp_offset;
   int8_t   hevc_pps_cr_qp_offset;
   /* AV1 frame header subset */
   uint32_t av1_prof_tier_level; /* direct override dword; 0 = build */
   uint32_t av1_frame_type_flags;
   uint8_t  av1_profile;         /* 0=main 1=high 2=professional */
   uint8_t  av1_level;           /* seq_level_idx */
   uint8_t  av1_tier;            /* 0=main 1=high */
   uint8_t  av1_frame_type;      /* 0=key 1=inter 2=intra-only 3=switch */
   uint8_t  av1_show_frame;
   uint8_t  av1_error_resilient_mode;
   uint8_t  av1_disable_cdf_update;
   uint8_t  av1_allow_screen_content_tools;
   uint8_t  av1_force_integer_mv;
   uint8_t  av1_allow_intrabc;
   uint32_t av1_order_hint;
   uint8_t  av1_primary_ref_frame; /* 0..7 or 7=none */
};

/*
 * Codec picture-setup BO layouts (host-written, GPU-read via pic_setup offset).
 * Field offsets refined from binary-driver structures / class expectations;
 * sizes are conservative upper bounds used for BO allocation.
 *
 * Layout convention: little-endian dwords; many fields are 16-bit pairs packed
 * into 32-bit words matching the proprietary driver's pic_setup DMA format.
 */
#define NV_NVDEC_PIC_SETUP_H264_BYTES    1024
#define NV_NVDEC_PIC_SETUP_HEVC_BYTES    2048
#define NV_NVDEC_PIC_SETUP_AV1_BYTES     4096
#define NV_NVDEC_PIC_SETUP_VP9_BYTES     1024
#define NV_NVDEC_PIC_SETUP_MAX_BYTES     NV_NVDEC_PIC_SETUP_AV1_BYTES

/* H.264 pic_setup dword indices (subset; matches common proprietary layout) */
#define NV_H264_PS_MB_WH                 0   /* (mb_height<<16)|mb_width */
#define NV_H264_PS_FRAME_NUM             1
#define NV_H264_PS_SPS_FLAGS             2   /* profile/level/chroma format bits */
#define NV_H264_PS_PPS_FLAGS             3   /* entropy/cabac/weighted pred */
#define NV_H264_PS_NUM_REFL0             4
#define NV_H264_PS_NUM_REFL1             5
#define NV_H264_PS_CURR_PIC_IDX          6
#define NV_H264_PS_FIELD_ORDER_CNT_0     7
#define NV_H264_PS_FIELD_ORDER_CNT_1     8
/* tick101: DPB / ref frame indices (up to 16 slots; proprietary layout subset) */
#define NV_H264_PS_REFL0_BASE            9   /* dwords 9..24 = L0 indices */
#define NV_H264_PS_REFL1_BASE            25  /* dwords 25..40 = L1 indices */
#define NV_H264_PS_DPB_LUMA_BASE         48  /* dwords 48..63 = DPB luma offs>>8 */
#define NV_H264_PS_DPB_CHROMA_BASE       64  /* dwords 64..79 = DPB chroma offs>>8 */
#define NV_H264_PS_OUTPUT_LUMA_OFF       16  /* >>8 offset words in some gens */
#define NV_H264_PS_OUTPUT_CHROMA_OFF     17
#define NV_H264_PS_HISTOGRAM_OFF         18
#define NV_H264_PS_COLOC_OFF             19
#define NV_H264_PS_BITSTREAM_LEN         20
/* NV_H264_PS_MAX_DPB_SLOTS defined above struct nv_nvdec_frame_setup */

/* HEVC pic_setup */
#define NV_HEVC_PS_PIC_WH                0   /* (height<<16)|width in pixels or CTBs */
#define NV_HEVC_PS_LOG2_CTB              1
#define NV_HEVC_PS_SPS_FLAGS             2
#define NV_HEVC_PS_PPS_FLAGS             3
#define NV_HEVC_PS_NUM_REFL0             4
#define NV_HEVC_PS_NUM_REFL1             5
#define NV_HEVC_PS_CURR_IDX              6
#define NV_HEVC_PS_OUTPUT_LUMA_OFF       24
#define NV_HEVC_PS_OUTPUT_CHROMA_OFF     25
#define NV_HEVC_PS_TILE_INFO_OFF         26
#define NV_HEVC_PS_SCALING_LIST_OFF      27

/* AV1 pic_setup (larger; tile/seg maps follow fixed header) */
#define NV_AV1_PS_FRAME_WH               0
#define NV_AV1_PS_PROF_TIER_LEVEL        1
#define NV_AV1_PS_FRAME_TYPE_FLAGS       2
#define NV_AV1_PS_ORDER_HINT             3
#define NV_AV1_PS_PRIMARY_REF            4
#define NV_AV1_PS_OUTPUT_LUMA_OFF        32
#define NV_AV1_PS_OUTPUT_CHROMA_OFF      33
#define NV_AV1_PS_CDF_OFF                34
#define NV_AV1_PS_SEG_MAP_OFF            35
#define NV_AV1_PS_TILE_INFO_OFF          36

/** Return recommended pic_setup BO size for app_id */
static inline uint32_t
nv_nvdec_pic_setup_size(uint32_t app_id)
{
   switch (app_id) {
   case NV_NVDEC_APP_ID_HEVC: return NV_NVDEC_PIC_SETUP_HEVC_BYTES;
   case NV_NVDEC_APP_ID_AV1:  return NV_NVDEC_PIC_SETUP_AV1_BYTES;
   case NV_NVDEC_APP_ID_VP9:  return NV_NVDEC_PIC_SETUP_VP9_BYTES;
   default:                   return NV_NVDEC_PIC_SETUP_H264_BYTES;
   }
}

/**
 * tick103: pack H.264 SPS/PPS flag dwords for pic_setup.
 * Layout is a conservative proprietary subset (profile/level/chroma in SPS;
 * entropy/weighted/transform/deblock in PPS).  Exact bit positions may need
 * silicon/trace refinement; non-zero fields are still preferable to zeros.
 *
 * SPS dword (NV_H264_PS_SPS_FLAGS):
 *   [7:0]   profile_idc
 *   [15:8]  level_idc
 *   [17:16] chroma_format_idc
 *   [23:20] bit_depth_luma_minus8
 *   [27:24] bit_depth_chroma_minus8
 *
 * PPS dword (NV_H264_PS_PPS_FLAGS):
 *   [0]     entropy_coding_mode_flag (CABAC)
 *   [1]     weighted_pred_flag
 *   [3:2]   weighted_bipred_idc
 *   [4]     transform_8x8_mode_flag
 *   [5]     constrained_intra_pred_flag
 *   [6]     deblocking_filter_control_present_flag
 *   [15:8]  pic_init_qp_minus26 as uint8 (biased +128 for negative)
 *   [23:16] chroma_qp_index_offset as uint8 (biased +128)
 */
static inline uint32_t
nv_h264_pack_sps_flags(const struct nv_nvdec_frame_setup *fs)
{
   uint32_t profile, level, chroma, bdl, bdc;
   if (!fs)
      return 0;
   if (fs->h264_sps_flags)
      return fs->h264_sps_flags;
   profile = fs->h264_profile_idc ? fs->h264_profile_idc : 100; /* High */
   level = fs->h264_level_idc ? fs->h264_level_idc : 41;
   chroma = fs->h264_chroma_format_idc ? fs->h264_chroma_format_idc : 1; /* 4:2:0 */
   bdl = fs->bit_depth_luma_minus8 & 0xfu;
   bdc = fs->bit_depth_chroma_minus8 & 0xfu;
   return (profile & 0xffu) |
          ((level & 0xffu) << 8) |
          ((chroma & 3u) << 16) |
          (bdl << 20) |
          (bdc << 24);
}

static inline uint32_t
nv_h264_pack_pps_flags(const struct nv_nvdec_frame_setup *fs)
{
   uint32_t f = 0;
   int qp, cqp;
   if (!fs)
      return 0;
   if (fs->h264_pps_flags)
      return fs->h264_pps_flags;
   if (fs->h264_entropy_coding_mode_flag)
      f |= 1u;
   if (fs->h264_weighted_pred_flag)
      f |= 2u;
   f |= ((uint32_t)(fs->h264_weighted_bipred_idc & 3u)) << 2;
   if (fs->h264_transform_8x8_mode_flag)
      f |= 16u;
   if (fs->h264_constrained_intra_pred_flag)
      f |= 32u;
   if (fs->h264_deblocking_filter_control_present_flag)
      f |= 64u;
   qp = (int)fs->h264_pic_init_qp_minus26 + 128;
   if (qp < 0) qp = 0;
   if (qp > 255) qp = 255;
   cqp = (int)fs->h264_chroma_qp_index_offset + 128;
   if (cqp < 0) cqp = 0;
   if (cqp > 255) cqp = 255;
   f |= ((uint32_t)qp) << 8;
   f |= ((uint32_t)cqp) << 16;
   return f;
}

/** Apply SPS/PPS into an existing pic_setup dword buffer (H.264 path only). */
static inline void
nv_nvdec_pic_setup_apply_h264_sps_pps(uint32_t *pic_dwords,
                                      uint32_t pic_dwords_cap,
                                      const struct nv_nvdec_frame_setup *fs)
{
   if (!pic_dwords || !fs)
      return;
   if (pic_dwords_cap > NV_H264_PS_SPS_FLAGS)
      pic_dwords[NV_H264_PS_SPS_FLAGS] = nv_h264_pack_sps_flags(fs);
   if (pic_dwords_cap > NV_H264_PS_PPS_FLAGS)
      pic_dwords[NV_H264_PS_PPS_FLAGS] = nv_h264_pack_pps_flags(fs);
}

/**
 * tick104: HEVC SPS/PPS dwords (conservative proprietary subset).
 *
 * SPS (NV_HEVC_PS_SPS_FLAGS):
 *   [2:0]   log2_min_luma_coding_block_size_minus3
 *   [5:3]   log2_diff_max_min_luma_coding_block_size
 *   [8:6]   max_transform_hierarchy_depth_inter
 *   [11:9]  max_transform_hierarchy_depth_intra
 *   [12]    pcm_enabled_flag
 *   [13]    amp_enabled_flag
 *   [14]    sample_adaptive_offset_enabled_flag
 *   [15]    sps_temporal_mvp_enabled_flag
 *   [16]    strong_intra_smoothing_enabled_flag
 *   [19:17] chroma_format_idc
 *   [23:20] bit_depth_luma_minus8
 *   [27:24] bit_depth_chroma_minus8
 *
 * PPS (NV_HEVC_PS_PPS_FLAGS):
 *   [0] dependent_slice_segments_enabled_flag
 *   [1] sign_data_hiding_enabled_flag
 *   [2] cabac_init_present_flag
 *   [3] weighted_pred_flag
 *   [4] weighted_bipred_flag
 *   [5] transquant_bypass_enabled_flag
 *   [6] tiles_enabled_flag
 *   [7] entropy_coding_sync_enabled_flag
 *   [8] loop_filter_across_tiles_enabled_flag
 *   [9] pps_loop_filter_across_slices_enabled_flag
 *   [10] deblocking_filter_override_enabled_flag
 *   [15:11] (reserved)
 *   [23:16] pps_cb_qp_offset + 128
 *   [31:24] pps_cr_qp_offset + 128
 */
static inline uint32_t
nv_hevc_pack_sps_flags(const struct nv_nvdec_frame_setup *fs)
{
   uint32_t f = 0;
   uint32_t chroma, bdl, bdc;
   if (!fs)
      return 0;
   if (fs->hevc_sps_flags)
      return fs->hevc_sps_flags;
   f |= (fs->hevc_log2_min_luma_coding_block_size_minus3 & 7u);
   f |= ((uint32_t)(fs->hevc_log2_diff_max_min_luma_coding_block_size & 7u)) << 3;
   f |= ((uint32_t)(fs->hevc_max_transform_hierarchy_depth_inter & 7u)) << 6;
   f |= ((uint32_t)(fs->hevc_max_transform_hierarchy_depth_intra & 7u)) << 9;
   if (fs->hevc_pcm_enabled_flag) f |= 1u << 12;
   if (fs->hevc_amp_enabled_flag) f |= 1u << 13;
   if (fs->hevc_sao_enabled_flag) f |= 1u << 14;
   if (fs->hevc_temporal_mvp_enabled_flag) f |= 1u << 15;
   if (fs->hevc_strong_intra_smoothing_enabled_flag) f |= 1u << 16;
   chroma = fs->h264_chroma_format_idc ? fs->h264_chroma_format_idc : 1;
   bdl = fs->bit_depth_luma_minus8 & 0xfu;
   bdc = fs->bit_depth_chroma_minus8 & 0xfu;
   f |= (chroma & 7u) << 17;
   f |= bdl << 20;
   f |= bdc << 24;
   /* default min CB 8 (minus3=0) and diff=3 => max 64 if all zero */
   if (!fs->hevc_log2_min_luma_coding_block_size_minus3 &&
       !fs->hevc_log2_diff_max_min_luma_coding_block_size &&
       !fs->hevc_sps_flags)
      f |= (3u << 3); /* log2_diff = 3 => 8..64 CTB range common default */
   return f;
}

static inline uint32_t
nv_hevc_pack_pps_flags(const struct nv_nvdec_frame_setup *fs)
{
   uint32_t f = 0;
   int cb, cr;
   if (!fs)
      return 0;
   if (fs->hevc_pps_flags)
      return fs->hevc_pps_flags;
   if (fs->hevc_dependent_slice_segments_enabled_flag) f |= 1u;
   if (fs->hevc_sign_data_hiding_enabled_flag) f |= 2u;
   if (fs->hevc_cabac_init_present_flag) f |= 4u;
   if (fs->hevc_weighted_pred_flag) f |= 8u;
   if (fs->hevc_weighted_bipred_flag) f |= 16u;
   if (fs->hevc_transquant_bypass_enabled_flag) f |= 32u;
   if (fs->hevc_tiles_enabled_flag) f |= 64u;
   if (fs->hevc_entropy_coding_sync_enabled_flag) f |= 128u;
   if (fs->hevc_loop_filter_across_tiles_enabled_flag) f |= 256u;
   if (fs->hevc_pps_loop_filter_across_slices_enabled_flag) f |= 512u;
   if (fs->hevc_deblocking_filter_override_enabled_flag) f |= 1024u;
   cb = (int)fs->hevc_pps_cb_qp_offset + 128;
   cr = (int)fs->hevc_pps_cr_qp_offset + 128;
   if (cb < 0) cb = 0;
   if (cb > 255) cb = 255;
   if (cr < 0) cr = 0;
   if (cr > 255) cr = 255;
   f |= ((uint32_t)cb) << 16;
   f |= ((uint32_t)cr) << 24;
   return f;
}

static inline void
nv_nvdec_pic_setup_apply_hevc_sps_pps(uint32_t *pic_dwords,
                                      uint32_t pic_dwords_cap,
                                      const struct nv_nvdec_frame_setup *fs)
{
   if (!pic_dwords || !fs)
      return;
   if (pic_dwords_cap > NV_HEVC_PS_SPS_FLAGS)
      pic_dwords[NV_HEVC_PS_SPS_FLAGS] = nv_hevc_pack_sps_flags(fs);
   if (pic_dwords_cap > NV_HEVC_PS_PPS_FLAGS)
      pic_dwords[NV_HEVC_PS_PPS_FLAGS] = nv_hevc_pack_pps_flags(fs);
   /* log2 CTB size hint: min_cb + diff + 3 (bytes in coding block terms) */
   if (pic_dwords_cap > NV_HEVC_PS_LOG2_CTB) {
      uint32_t min3 = fs->hevc_log2_min_luma_coding_block_size_minus3 & 7u;
      uint32_t diff = fs->hevc_log2_diff_max_min_luma_coding_block_size & 7u;
      if (!diff && !min3)
         diff = 3;
      pic_dwords[NV_HEVC_PS_LOG2_CTB] = min3 + diff + 3u;
   }
}

/**
 * tick104: AV1 profile/tier/level + frame_type flags.
 *
 * PROF_TIER_LEVEL dword:
 *   [2:0]  seq_profile
 *   [7:3]  seq_level_idx
 *   [8]    seq_tier
 *   [12:9] bit_depth_luma_minus8 (reuse frame_setup bit depths)
 *
 * FRAME_TYPE_FLAGS:
 *   [1:0]  frame_type
 *   [2]    show_frame
 *   [3]    error_resilient_mode
 *   [4]    disable_cdf_update
 *   [5]    allow_screen_content_tools
 *   [6]    force_integer_mv
 *   [7]    allow_intrabc
 */
static inline uint32_t
nv_av1_pack_prof_tier_level(const struct nv_nvdec_frame_setup *fs)
{
   uint32_t f;
   if (!fs)
      return 0;
   if (fs->av1_prof_tier_level)
      return fs->av1_prof_tier_level;
   f = (fs->av1_profile & 7u) |
       ((uint32_t)(fs->av1_level & 0x1fu)) << 3 |
       ((uint32_t)(fs->av1_tier & 1u)) << 8 |
       ((fs->bit_depth_luma_minus8 & 0xfu)) << 9;
   return f;
}

static inline uint32_t
nv_av1_pack_frame_type_flags(const struct nv_nvdec_frame_setup *fs)
{
   uint32_t f = 0;
   if (!fs)
      return 0;
   if (fs->av1_frame_type_flags)
      return fs->av1_frame_type_flags;
   f = fs->av1_frame_type & 3u;
   if (fs->av1_show_frame) f |= 4u;
   if (fs->av1_error_resilient_mode) f |= 8u;
   if (fs->av1_disable_cdf_update) f |= 16u;
   if (fs->av1_allow_screen_content_tools) f |= 32u;
   if (fs->av1_force_integer_mv) f |= 64u;
   if (fs->av1_allow_intrabc) f |= 128u;
   return f;
}

static inline void
nv_nvdec_pic_setup_apply_av1_header(uint32_t *pic_dwords,
                                    uint32_t pic_dwords_cap,
                                    const struct nv_nvdec_frame_setup *fs)
{
   if (!pic_dwords || !fs)
      return;
   if (pic_dwords_cap > NV_AV1_PS_PROF_TIER_LEVEL)
      pic_dwords[NV_AV1_PS_PROF_TIER_LEVEL] = nv_av1_pack_prof_tier_level(fs);
   if (pic_dwords_cap > NV_AV1_PS_FRAME_TYPE_FLAGS)
      pic_dwords[NV_AV1_PS_FRAME_TYPE_FLAGS] = nv_av1_pack_frame_type_flags(fs);
   if (pic_dwords_cap > NV_AV1_PS_ORDER_HINT)
      pic_dwords[NV_AV1_PS_ORDER_HINT] = fs->av1_order_hint;
   if (pic_dwords_cap > NV_AV1_PS_PRIMARY_REF)
      pic_dwords[NV_AV1_PS_PRIMARY_REF] = fs->av1_primary_ref_frame;
   if (fs->history_gpu_addr && pic_dwords_cap > NV_AV1_PS_CDF_OFF)
      pic_dwords[NV_AV1_PS_CDF_OFF] = (uint32_t)(fs->history_gpu_addr >> 8);
}

/**
 * Populate a host-mapped pic_setup buffer with essential geometry/output
 * offsets from frame_setup.  Does not fill full reference lists (caller or
 * higher-level VA-API path must extend); provides a valid minimal header so
 * NVDEC execute has non-zero required fields.
 */
static inline void
nv_nvdec_pic_setup_init_minimal(uint32_t *pic_dwords, uint32_t pic_dwords_cap,
                                const struct nv_nvdec_frame_setup *fs)
{
   uint32_t app;
   uint32_t mb_w, mb_h;
   if (!pic_dwords || !pic_dwords_cap || !fs)
      return;
   memset(pic_dwords, 0, (size_t)pic_dwords_cap * 4u);
   app = fs->app_id ? fs->app_id : NV_NVDEC_APP_ID_H264;
   mb_w = fs->mb_width ? fs->mb_width : 1;
   mb_h = fs->mb_height ? fs->mb_height : 1;

   switch (app) {
   case NV_NVDEC_APP_ID_HEVC:
      if (pic_dwords_cap > NV_HEVC_PS_PIC_WH)
         pic_dwords[NV_HEVC_PS_PIC_WH] = (mb_h << 16) | (mb_w & 0xffffu);
      nv_nvdec_pic_setup_apply_hevc_sps_pps(pic_dwords, pic_dwords_cap, fs);
      if (pic_dwords_cap > NV_HEVC_PS_CURR_IDX)
         pic_dwords[NV_HEVC_PS_CURR_IDX] = fs->picture_index;
      if (pic_dwords_cap > NV_HEVC_PS_NUM_REFL0)
         pic_dwords[NV_HEVC_PS_NUM_REFL0] = fs->num_refl0;
      if (pic_dwords_cap > NV_HEVC_PS_NUM_REFL1)
         pic_dwords[NV_HEVC_PS_NUM_REFL1] = fs->num_refl1;
      if (fs->output_luma_gpu_addr && pic_dwords_cap > NV_HEVC_PS_OUTPUT_LUMA_OFF)
         pic_dwords[NV_HEVC_PS_OUTPUT_LUMA_OFF] =
            (uint32_t)(fs->output_luma_gpu_addr >> 8);
      if (fs->output_chroma_gpu_addr && pic_dwords_cap > NV_HEVC_PS_OUTPUT_CHROMA_OFF)
         pic_dwords[NV_HEVC_PS_OUTPUT_CHROMA_OFF] =
            (uint32_t)(fs->output_chroma_gpu_addr >> 8);
      /* DPB planes reuse H.264 slot arrays (same count limits) */
      {
         uint32_t si, n = fs->dpb_slot_count;
         if (n > NV_H264_PS_MAX_DPB_SLOTS)
            n = NV_H264_PS_MAX_DPB_SLOTS;
         for (si = 0; si < n; si++) {
            uint32_t dl = NV_H264_PS_DPB_LUMA_BASE + si;
            uint32_t dc = NV_H264_PS_DPB_CHROMA_BASE + si;
            if (dl < pic_dwords_cap && fs->dpb_luma_gpu_addr[si])
               pic_dwords[dl] = (uint32_t)(fs->dpb_luma_gpu_addr[si] >> 8);
            if (dc < pic_dwords_cap && fs->dpb_chroma_gpu_addr[si])
               pic_dwords[dc] = (uint32_t)(fs->dpb_chroma_gpu_addr[si] >> 8);
         }
      }
      break;
   case NV_NVDEC_APP_ID_AV1:
      if (pic_dwords_cap > NV_AV1_PS_FRAME_WH)
         pic_dwords[NV_AV1_PS_FRAME_WH] = (mb_h << 16) | (mb_w & 0xffffu);
      nv_nvdec_pic_setup_apply_av1_header(pic_dwords, pic_dwords_cap, fs);
      if (fs->output_luma_gpu_addr && pic_dwords_cap > NV_AV1_PS_OUTPUT_LUMA_OFF)
         pic_dwords[NV_AV1_PS_OUTPUT_LUMA_OFF] =
            (uint32_t)(fs->output_luma_gpu_addr >> 8);
      if (fs->output_chroma_gpu_addr && pic_dwords_cap > NV_AV1_PS_OUTPUT_CHROMA_OFF)
         pic_dwords[NV_AV1_PS_OUTPUT_CHROMA_OFF] =
            (uint32_t)(fs->output_chroma_gpu_addr >> 8);
      break;
   case NV_NVDEC_APP_ID_H264:
   default:
      if (pic_dwords_cap > NV_H264_PS_MB_WH)
         pic_dwords[NV_H264_PS_MB_WH] = (mb_h << 16) | (mb_w & 0xffffu);
      /* tick103: SPS/PPS flags (profile/level/chroma + entropy/wp/deblock) */
      nv_nvdec_pic_setup_apply_h264_sps_pps(pic_dwords, pic_dwords_cap, fs);
      if (pic_dwords_cap > NV_H264_PS_CURR_PIC_IDX)
         pic_dwords[NV_H264_PS_CURR_PIC_IDX] = fs->picture_index;
      if (pic_dwords_cap > NV_H264_PS_BITSTREAM_LEN)
         pic_dwords[NV_H264_PS_BITSTREAM_LEN] = fs->bitstream_size;
      if (pic_dwords_cap > NV_H264_PS_NUM_REFL0)
         pic_dwords[NV_H264_PS_NUM_REFL0] = fs->num_refl0;
      if (pic_dwords_cap > NV_H264_PS_NUM_REFL1)
         pic_dwords[NV_H264_PS_NUM_REFL1] = fs->num_refl1;
      if (fs->output_luma_gpu_addr && pic_dwords_cap > NV_H264_PS_OUTPUT_LUMA_OFF)
         pic_dwords[NV_H264_PS_OUTPUT_LUMA_OFF] =
            (uint32_t)(fs->output_luma_gpu_addr >> 8);
      if (fs->output_chroma_gpu_addr && pic_dwords_cap > NV_H264_PS_OUTPUT_CHROMA_OFF)
         pic_dwords[NV_H264_PS_OUTPUT_CHROMA_OFF] =
            (uint32_t)(fs->output_chroma_gpu_addr >> 8);
      if (fs->coloc_gpu_addr && pic_dwords_cap > NV_H264_PS_COLOC_OFF)
         pic_dwords[NV_H264_PS_COLOC_OFF] =
            (uint32_t)(fs->coloc_gpu_addr >> 8);
      /* tick101: pack L0/L1 ref indices and DPB plane offsets when provided */
      {
         uint32_t si, n;
         n = fs->num_refl0;
         if (n > NV_H264_PS_MAX_DPB_SLOTS)
            n = NV_H264_PS_MAX_DPB_SLOTS;
         for (si = 0; si < n; si++) {
            uint32_t di = NV_H264_PS_REFL0_BASE + si;
            if (di < pic_dwords_cap)
               pic_dwords[di] = fs->refl0_idx[si];
         }
         n = fs->num_refl1;
         if (n > NV_H264_PS_MAX_DPB_SLOTS)
            n = NV_H264_PS_MAX_DPB_SLOTS;
         for (si = 0; si < n; si++) {
            uint32_t di = NV_H264_PS_REFL1_BASE + si;
            if (di < pic_dwords_cap)
               pic_dwords[di] = fs->refl1_idx[si];
         }
         n = fs->dpb_slot_count;
         if (n > NV_H264_PS_MAX_DPB_SLOTS)
            n = NV_H264_PS_MAX_DPB_SLOTS;
         for (si = 0; si < n; si++) {
            uint32_t dl = NV_H264_PS_DPB_LUMA_BASE + si;
            uint32_t dc = NV_H264_PS_DPB_CHROMA_BASE + si;
            if (dl < pic_dwords_cap && fs->dpb_luma_gpu_addr[si])
               pic_dwords[dl] = (uint32_t)(fs->dpb_luma_gpu_addr[si] >> 8);
            if (dc < pic_dwords_cap && fs->dpb_chroma_gpu_addr[si])
               pic_dwords[dc] = (uint32_t)(fs->dpb_chroma_gpu_addr[si] >> 8);
         }
      }
      break;
   }
}

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
   /* tick106: encode geometry / rate-control hints for pic_setup BO */
   uint32_t width;
   uint32_t height;
   uint32_t frame_rate_num;      /* e.g. 30 */
   uint32_t frame_rate_den;      /* e.g. 1 */
   uint32_t average_bitrate;     /* bps; 0 = VBR/default */
   uint32_t max_bitrate;
   uint32_t gop_length;          /* 0 = intra-only / driver default */
   uint32_t idr_period;
   uint8_t  profile_idc;         /* H.264/HEVC profile; 0 = default */
   uint8_t  level_idc;
   uint8_t  chroma_format_idc;   /* 1 = 4:2:0 */
   uint8_t  bit_depth_luma_minus8;
   uint32_t nvenc_sps_flags;     /* direct override for pic_setup dword 2 */
   uint32_t nvenc_pps_rc_flags;  /* direct override for pic_setup dword 3 */
};

/* NVENC pic_setup BO (host-written, GPU-read); conservative size */
#define NV_NVENC_PIC_SETUP_BYTES         1024
#define NV_NVENC_PS_PIC_WH               0   /* (height<<16)|width */
#define NV_NVENC_PS_FRAME_RATE           1   /* (den<<16)|num */
#define NV_NVENC_PS_SPS_FLAGS            2   /* profile/level/chroma/bitdepth */
#define NV_NVENC_PS_PPS_RC_FLAGS         3   /* RC/gop/idr subset */
#define NV_NVENC_PS_AVG_BITRATE          4
#define NV_NVENC_PS_MAX_BITRATE          5
#define NV_NVENC_PS_GOP_LENGTH           6
#define NV_NVENC_PS_IDR_PERIOD           7
#define NV_NVENC_PS_INPUT_LUMA_OFF       8   /* input Y plane >>8 */
#define NV_NVENC_PS_INPUT_CHROMA_OFF     9
#define NV_NVENC_PS_BITSTREAM_OUT_OFF    10
#define NV_NVENC_PS_RC_STATE_OFF         11
#define NV_NVENC_PS_STATUS_OFF           12

static inline uint32_t
nv_nvenc_pic_setup_size(uint32_t app_id)
{
   (void)app_id;
   return NV_NVENC_PIC_SETUP_BYTES;
}

/**
 * tick106: pack NVENC SPS/profile dword (refine on silicon/traces).
 *   [7:0]   profile_idc
 *   [15:8]  level_idc
 *   [17:16] chroma_format_idc
 *   [23:20] bit_depth_luma_minus8
 *   [31:24] app_id (echo for debug)
 */
static inline uint32_t
nv_nvenc_pack_sps_flags(const struct nv_nvenc_frame_setup *fs)
{
   uint32_t profile, level, chroma, bdl, app;
   if (!fs)
      return 0;
   if (fs->nvenc_sps_flags)
      return fs->nvenc_sps_flags;
   app = fs->app_id ? fs->app_id : NV_NVENC_APP_ID_H264;
   profile = fs->profile_idc ? fs->profile_idc :
             (app == NV_NVENC_APP_ID_HEVC ? 1 : 100);
   level = fs->level_idc ? fs->level_idc : 41;
   chroma = fs->chroma_format_idc ? fs->chroma_format_idc : 1;
   bdl = fs->bit_depth_luma_minus8 & 0xfu;
   return (profile & 0xffu) |
          ((level & 0xffu) << 8) |
          ((chroma & 3u) << 16) |
          (bdl << 20) |
          ((app & 0xffu) << 24);
}

/**
 * tick106: RC/GOP control dword.
 *   [0]     CBR (1) vs VBR/default (0) when average_bitrate set
 *   [1]     max_bitrate present
 *   [2]     gop_length present
 *   [3]     idr_period present
 *   [15:8]  frame_rate_num (clamped)
 *   [23:16] frame_rate_den (clamped)
 */
static inline uint32_t
nv_nvenc_pack_pps_rc_flags(const struct nv_nvenc_frame_setup *fs)
{
   uint32_t f = 0;
   uint32_t fn, fd;
   if (!fs)
      return 0;
   if (fs->nvenc_pps_rc_flags)
      return fs->nvenc_pps_rc_flags;
   if (fs->average_bitrate)
      f |= 1u;
   if (fs->max_bitrate)
      f |= 2u;
   if (fs->gop_length)
      f |= 4u;
   if (fs->idr_period)
      f |= 8u;
   fn = fs->frame_rate_num ? fs->frame_rate_num : 30;
   fd = fs->frame_rate_den ? fs->frame_rate_den : 1;
   if (fn > 255) fn = 255;
   if (fd > 255) fd = 255;
   f |= (fn & 0xffu) << 8;
   f |= (fd & 0xffu) << 16;
   return f;
}

/** Populate NVENC pic_setup dword buffer (minimal encode header). */
static inline void
nv_nvenc_pic_setup_init_minimal(uint32_t *pic_dwords, uint32_t pic_dwords_cap,
                                const struct nv_nvenc_frame_setup *fs)
{
   uint32_t w, h, fn, fd;
   if (!pic_dwords || !pic_dwords_cap || !fs)
      return;
   memset(pic_dwords, 0, (size_t)pic_dwords_cap * 4u);
   w = fs->width ? fs->width : 1;
   h = fs->height ? fs->height : 1;
   fn = fs->frame_rate_num ? fs->frame_rate_num : 30;
   fd = fs->frame_rate_den ? fs->frame_rate_den : 1;
   if (pic_dwords_cap > NV_NVENC_PS_PIC_WH)
      pic_dwords[NV_NVENC_PS_PIC_WH] = (h << 16) | (w & 0xffffu);
   if (pic_dwords_cap > NV_NVENC_PS_FRAME_RATE)
      pic_dwords[NV_NVENC_PS_FRAME_RATE] = (fd << 16) | (fn & 0xffffu);
   if (pic_dwords_cap > NV_NVENC_PS_SPS_FLAGS)
      pic_dwords[NV_NVENC_PS_SPS_FLAGS] = nv_nvenc_pack_sps_flags(fs);
   if (pic_dwords_cap > NV_NVENC_PS_PPS_RC_FLAGS)
      pic_dwords[NV_NVENC_PS_PPS_RC_FLAGS] = nv_nvenc_pack_pps_rc_flags(fs);
   if (pic_dwords_cap > NV_NVENC_PS_AVG_BITRATE)
      pic_dwords[NV_NVENC_PS_AVG_BITRATE] = fs->average_bitrate;
   if (pic_dwords_cap > NV_NVENC_PS_MAX_BITRATE)
      pic_dwords[NV_NVENC_PS_MAX_BITRATE] = fs->max_bitrate;
   if (pic_dwords_cap > NV_NVENC_PS_GOP_LENGTH)
      pic_dwords[NV_NVENC_PS_GOP_LENGTH] = fs->gop_length;
   if (pic_dwords_cap > NV_NVENC_PS_IDR_PERIOD)
      pic_dwords[NV_NVENC_PS_IDR_PERIOD] = fs->idr_period;
   if (fs->input_yuv_gpu_addr && pic_dwords_cap > NV_NVENC_PS_INPUT_LUMA_OFF)
      pic_dwords[NV_NVENC_PS_INPUT_LUMA_OFF] =
         (uint32_t)(fs->input_yuv_gpu_addr >> 8);
   if (fs->bitstream_out_gpu_addr && pic_dwords_cap > NV_NVENC_PS_BITSTREAM_OUT_OFF)
      pic_dwords[NV_NVENC_PS_BITSTREAM_OUT_OFF] =
         (uint32_t)(fs->bitstream_out_gpu_addr >> 8);
   if (fs->rc_gpu_addr && pic_dwords_cap > NV_NVENC_PS_RC_STATE_OFF)
      pic_dwords[NV_NVENC_PS_RC_STATE_OFF] =
         (uint32_t)(fs->rc_gpu_addr >> 8);
   if (fs->status_gpu_addr && pic_dwords_cap > NV_NVENC_PS_STATUS_OFF)
      pic_dwords[NV_NVENC_PS_STATUS_OFF] =
         (uint32_t)(fs->status_gpu_addr >> 8);
}

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

/**
 * tick106: NVENC vertical slice — SET_OBJECT + frame methods + sema + EXECUTE.
 * pic_setup BO must be pre-filled (nv_nvenc_pic_setup_init_minimal) if used.
 */
static inline void
nv_nvenc_emit_frame_kick(struct nv_push *p, uint32_t class_nvenc,
                         const struct nv_nvenc_frame_setup *fs,
                         uint64_t sema_gpu, uint32_t sema_payload,
                         volatile uint32_t *status_cpu)
{
   if (!p)
      return;
   if (class_nvenc)
      nv_nvenc_set_object(p, class_nvenc);
   if (sema_gpu)
      nv_nvdec_emit_semaphore_release_reset(p, sema_gpu, sema_payload,
                                            status_cpu);
   if (fs)
      nv_nvenc_emit_frame_setup(p, fs);
   else
      nv_push_method(p, NV_NVENC_EXECUTE, 1u);
}




/* ---- Codec picture-setup BO layouts (CPU-filled, GPU-read by NVDEC) ----
 * Layouts mirror fields the proprietary driver writes before EXECUTE; sizes
 * and offsets refined incrementally from class traces / binary driver RE.
 * All multi-byte fields are little-endian; GPU addresses are absolute VA.
 */

#define NV_NVDEC_H264_PIC_SETUP_DWORDS   64
#define NV_NVDEC_HEVC_PIC_SETUP_DWORDS   96
#define NV_NVDEC_AV1_PIC_SETUP_DWORDS    128
#define NV_NVDEC_MAX_PIC_SETUP_BYTES     (NV_NVDEC_AV1_PIC_SETUP_DWORDS * 4)

/* H.264 picture parameter block (subset; first 64 dwords) */
struct nv_nvdec_h264_pic_setup {
   uint32_t width_mb;            /* pic width in macroblocks */
   uint32_t height_mb;
   uint32_t frame_num;
   uint32_t field_pic_flag;      /* 0=frame, 1=field */
   uint32_t bottom_field_flag;
   uint32_t mbaff_frame_flag;
   uint32_t ref_pic_flag;
   uint32_t constrained_intra_pred_flag;
   uint32_t weighted_pred_flag;
   uint32_t weighted_bipred_idc;
   uint32_t frame_mbs_only_flag;
   uint32_t transform_8x8_mode_flag;
   uint32_t chroma_format_idc;
   uint32_t l0_ref_count;        /* active L0 refs */
   uint32_t l1_ref_count;
   uint32_t log2_max_frame_num_minus4;
   uint32_t pic_order_cnt_type;
   uint32_t log2_max_pic_order_cnt_lsb_minus4;
   uint32_t delta_pic_order_always_zero_flag;
   uint32_t direct_8x8_inference_flag;
   uint32_t entropy_coding_mode_flag;
   uint32_t pic_order_present_flag;
   uint32_t deblocking_filter_control_present_flag;
   uint32_t redundant_pic_cnt_present_flag;
   uint32_t num_slice_groups_minus1;
   uint32_t slice_group_map_type;
   uint32_t num_ref_idx_l0_active_minus1;
   uint32_t num_ref_idx_l1_active_minus1;
   uint32_t pic_init_qp_minus26;
   uint32_t chroma_qp_index_offset;
   uint32_t second_chroma_qp_index_offset;
   uint32_t curr_pic_idx;        /* DPB slot for current picture */
   uint32_t dpb_luma_pitch;
   uint32_t dpb_chroma_pitch;
   uint64_t dpb_luma_top[16];    /* reference frame VAs (top field) */
   uint64_t dpb_luma_bot[16];
   uint64_t dpb_chroma_top[16];
   uint64_t dpb_chroma_bot[16];
   uint32_t reserved[8];
};

/* HEVC picture parameter block (subset) */
struct nv_nvdec_hevc_pic_setup {
   uint32_t pic_width_in_luma_samples;
   uint32_t pic_height_in_luma_samples;
   uint32_t log2_min_luma_coding_block_size_minus3;
   uint32_t log2_diff_max_min_luma_coding_block_size;
   uint32_t log2_min_transform_block_size_minus2;
   uint32_t log2_diff_max_min_transform_block_size;
   uint32_t max_transform_hierarchy_depth_intra;
   uint32_t max_transform_hierarchy_depth_inter;
   uint32_t pcm_enabled_flag;
   uint32_t separate_colour_plane_flag;
   uint32_t chroma_format_idc;
   uint32_t bit_depth_luma_minus8;
   uint32_t bit_depth_chroma_minus8;
   uint32_t log2_max_pic_order_cnt_lsb_minus4;
   uint32_t num_short_term_ref_pic_sets;
   uint32_t num_long_term_ref_pics_sps;
   uint32_t num_ref_idx_l0_default_active_minus1;
   uint32_t num_ref_idx_l1_default_active_minus1;
   uint32_t init_qp_minus26;
   uint32_t dependent_slice_segments_enabled_flag;
   uint32_t sign_data_hiding_enabled_flag;
   uint32_t amp_enabled_flag;
   uint32_t sample_adaptive_offset_enabled_flag;
   uint32_t pcm_loop_filter_disabled_flag;
   uint32_t strong_intra_smoothing_enabled_flag;
   uint32_t temporal_mvp_enabled_flag;
   uint32_t curr_pic_idx;
   uint32_t dpb_luma_pitch;
   uint32_t dpb_chroma_pitch;
   uint64_t dpb_luma[16];
   uint64_t dpb_chroma[16];
   uint32_t reserved[16];
};

/* VP9 picture parameter block (subset; NVDEC app_id 7) */
struct nv_nvdec_vp9_pic_setup {
   uint32_t width;
   uint32_t height;
   uint32_t prev_width;
   uint32_t prev_height;
   uint32_t profile;
   uint32_t bit_depth;
   uint32_t subsampling_x;
   uint32_t subsampling_y;
   uint32_t frame_type;          /* 0=key, 1=inter */
   uint32_t show_frame;
   uint32_t error_resilient_mode;
   uint32_t intra_only;
   uint32_t allow_high_precision_mv;
   uint32_t mcomp_filter_type;
   uint32_t refresh_frame_context;
   uint32_t frame_context_idx;
   uint32_t reset_frame_context;
   uint32_t segmentation_enabled;
   uint32_t segmentation_update_map;
   uint32_t segmentation_temporal_update;
   uint32_t segmentation_update_data;
   uint32_t last_ref_frame;
   uint32_t golden_ref_frame;
   uint32_t alt_ref_frame;
   uint32_t last_ref_sign_bias;
   uint32_t golden_ref_sign_bias;
   uint32_t alt_ref_sign_bias;
   uint32_t lossless_flag;
   uint32_t use_prev_frame_mvs;
   uint32_t filter_level;
   uint32_t sharpness_level;
   uint32_t log2_tile_rows;
   uint32_t log2_tile_columns;
   uint32_t base_qindex;
   int32_t y_dc_delta_q;
   int32_t uv_dc_delta_q;
   int32_t uv_ac_delta_q;
   uint32_t first_partition_size;
   uint32_t frame_header_length_in_bytes;
   uint32_t curr_pic_idx;
   uint32_t dpb_luma_pitch;
   uint32_t dpb_chroma_pitch;
   uint64_t dpb_luma[8];
   uint64_t dpb_chroma[8];
   uint64_t output_luma_gpu_addr;
   uint64_t output_chroma_gpu_addr;
   uint64_t history_gpu_addr;
   uint32_t reserved[16];
};

#define NV_NVDEC_VP9_PIC_SETUP_DWORDS  96

/* AV1 picture parameter block (subset) */
struct nv_nvdec_av1_pic_setup {
   uint32_t width;
   uint32_t height;
   uint32_t superres_denom;
   uint32_t use_128x128_superblock;
   uint32_t intra_only;
   uint32_t allow_high_precision_mv;
   uint32_t allow_warped_motion;
   uint32_t enable_cdef;
   uint32_t enable_restoration;
   uint32_t enable_superres;
   uint32_t bit_depth_minus8;
   uint32_t mono_chrome;
   uint32_t subsampling_x;
   uint32_t subsampling_y;
   uint32_t frame_type;
   uint32_t show_frame;
   uint32_t error_resilient_mode;
   uint32_t disable_cdf_update;
   uint32_t allow_screen_content_tools;
   uint32_t force_integer_mv;
   uint32_t coded_lossless;
   uint32_t use_superres;
   uint32_t refresh_frame_flags;
   uint32_t curr_pic_idx;
   uint32_t primary_ref_frame;
   uint32_t order_hint;
   uint32_t profile;
   uint32_t order_hint_bits_minus_1;
   uint32_t enable_order_hint;
   uint32_t enable_jnt_comp;
   uint32_t ref_frame_mvs;
   uint32_t film_grain_params_present;
   uint32_t apply_grain;
   uint32_t base_qindex;
   uint32_t tile_cols;
   uint32_t tile_rows;
   uint8_t ref_frame_idx[8];
   uint32_t dpb_luma_pitch;
   uint32_t dpb_chroma_pitch;
   uint64_t dpb_luma[8];
   uint64_t dpb_chroma[8];
   uint64_t output_luma_gpu_addr;
   uint64_t output_chroma_gpu_addr;
   uint64_t film_grain_params_addr; /* optional sideband BO */
   uint32_t reserved[16];
};

/* Serialise H.264 setup into a dword array (host endian) for CPU map/upload */
static inline void
nv_nvdec_h264_pic_setup_pack(const struct nv_nvdec_h264_pic_setup *ps,
                             uint32_t *dst, uint32_t dst_dwords)
{
   uint32_t i, n = NV_NVDEC_H264_PIC_SETUP_DWORDS;
   const uint32_t *src;
   if (!ps || !dst || !dst_dwords)
      return;
   if (n > dst_dwords)
      n = dst_dwords;
   src = (const uint32_t *)ps;
   for (i = 0; i < n; i++)
      dst[i] = src[i];
}

static inline void
nv_nvdec_hevc_pic_setup_pack(const struct nv_nvdec_hevc_pic_setup *ps,
                             uint32_t *dst, uint32_t dst_dwords)
{
   uint32_t i, n = NV_NVDEC_HEVC_PIC_SETUP_DWORDS;
   const uint32_t *src;
   if (!ps || !dst || !dst_dwords)
      return;
   if (n > dst_dwords)
      n = dst_dwords;
   src = (const uint32_t *)ps;
   for (i = 0; i < n; i++)
      dst[i] = src[i];
}

static inline void
nv_nvdec_av1_pic_setup_pack(const struct nv_nvdec_av1_pic_setup *ps,
                            uint32_t *dst, uint32_t dst_dwords)
{
   uint32_t i, n = NV_NVDEC_AV1_PIC_SETUP_DWORDS;
   const uint32_t *src;
   if (!ps || !dst || !dst_dwords)
      return;
   if (n > dst_dwords)
      n = dst_dwords;
   src = (const uint32_t *)ps;
   for (i = 0; i < n; i++)
      dst[i] = src[i];
}

static inline void
nv_nvdec_vp9_pic_setup_pack(const struct nv_nvdec_vp9_pic_setup *ps,
                            uint32_t *dst, uint32_t dst_dwords)
{
   uint32_t i, n = NV_NVDEC_VP9_PIC_SETUP_DWORDS;
   const uint32_t *src;
   if (!ps || !dst || !dst_dwords)
      return;
   if (n > dst_dwords)
      n = dst_dwords;
   src = (const uint32_t *)ps;
   for (i = 0; i < n; i++)
      dst[i] = src[i];
}

/**
 * Apply VP9 frame params (pipe_vp9_picture_desc / VAAPI subset) into pic_setup.
 */
static inline void
nv_nvdec_vp9_apply_frame(struct nv_nvdec_vp9_pic_setup *ps,
                         uint32_t width, uint32_t height,
                         uint32_t prev_width, uint32_t prev_height,
                         uint32_t profile, uint32_t bit_depth,
                         uint32_t subsampling_x, uint32_t subsampling_y,
                         uint32_t frame_type, uint32_t show_frame,
                         uint32_t error_resilient_mode, uint32_t intra_only,
                         uint32_t allow_high_precision_mv,
                         uint32_t mcomp_filter_type,
                         uint32_t refresh_frame_context,
                         uint32_t frame_context_idx,
                         uint32_t reset_frame_context,
                         uint32_t segmentation_enabled,
                         uint32_t segmentation_update_map,
                         uint32_t segmentation_temporal_update,
                         uint32_t segmentation_update_data,
                         uint32_t last_ref_frame, uint32_t golden_ref_frame,
                         uint32_t alt_ref_frame,
                         uint32_t last_ref_sign_bias,
                         uint32_t golden_ref_sign_bias,
                         uint32_t alt_ref_sign_bias,
                         uint32_t lossless_flag, uint32_t use_prev_frame_mvs,
                         uint32_t filter_level, uint32_t sharpness_level,
                         uint32_t log2_tile_rows, uint32_t log2_tile_columns,
                         uint32_t base_qindex,
                         int32_t y_dc_delta_q, int32_t uv_dc_delta_q,
                         int32_t uv_ac_delta_q,
                         uint32_t first_partition_size,
                         uint32_t frame_header_length_in_bytes)
{
   if (!ps)
      return;
   if (!ps->width && !ps->height)
      memset(ps, 0, sizeof(*ps));
   ps->width = width ? width : ps->width;
   ps->height = height ? height : ps->height;
   ps->prev_width = prev_width ? prev_width : ps->prev_width;
   ps->prev_height = prev_height ? prev_height : ps->prev_height;
   ps->profile = profile;
   ps->bit_depth = bit_depth ? bit_depth : 8;
   ps->subsampling_x = subsampling_x ? 1 : 0;
   ps->subsampling_y = subsampling_y ? 1 : 0;
   ps->frame_type = frame_type & 1u;
   ps->show_frame = show_frame ? 1 : 0;
   ps->error_resilient_mode = error_resilient_mode ? 1 : 0;
   ps->intra_only = intra_only ? 1 : 0;
   ps->allow_high_precision_mv = allow_high_precision_mv ? 1 : 0;
   ps->mcomp_filter_type = mcomp_filter_type & 7u;
   ps->refresh_frame_context = refresh_frame_context ? 1 : 0;
   ps->frame_context_idx = frame_context_idx & 3u;
   ps->reset_frame_context = reset_frame_context & 3u;
   ps->segmentation_enabled = segmentation_enabled ? 1 : 0;
   ps->segmentation_update_map = segmentation_update_map ? 1 : 0;
   ps->segmentation_temporal_update = segmentation_temporal_update ? 1 : 0;
   ps->segmentation_update_data = segmentation_update_data ? 1 : 0;
   ps->last_ref_frame = last_ref_frame & 7u;
   ps->golden_ref_frame = golden_ref_frame & 7u;
   ps->alt_ref_frame = alt_ref_frame & 7u;
   ps->last_ref_sign_bias = last_ref_sign_bias ? 1 : 0;
   ps->golden_ref_sign_bias = golden_ref_sign_bias ? 1 : 0;
   ps->alt_ref_sign_bias = alt_ref_sign_bias ? 1 : 0;
   ps->lossless_flag = lossless_flag ? 1 : 0;
   ps->use_prev_frame_mvs = use_prev_frame_mvs ? 1 : 0;
   ps->filter_level = filter_level & 0x3fu;
   ps->sharpness_level = sharpness_level & 7u;
   ps->log2_tile_rows = log2_tile_rows & 3u;
   ps->log2_tile_columns = log2_tile_columns & 3u;
   ps->base_qindex = base_qindex & 0xffu;
   ps->y_dc_delta_q = y_dc_delta_q;
   ps->uv_dc_delta_q = uv_dc_delta_q;
   ps->uv_ac_delta_q = uv_ac_delta_q;
   ps->first_partition_size = first_partition_size;
   ps->frame_header_length_in_bytes = frame_header_length_in_bytes;
}

static inline void
nv_nvdec_vp9_set_dpb_ref(struct nv_nvdec_vp9_pic_setup *ps, unsigned slot,
                         uint64_t luma_va, uint64_t chroma_va)
{
   if (!ps || slot >= 8)
      return;
   ps->dpb_luma[slot] = luma_va;
   ps->dpb_chroma[slot] = chroma_va;
}

/* ---- SPS/PPS bitstream-derived helpers (host-side; fill pic_setup fields) ---- */

/**
 * Apply H.264 SPS/PPS subset into pic_setup (fields commonly needed by NVDEC).
 * sps/pps are logical bitstream values (not NAL bytes); driver copies into
 * proprietary pic_setup dword layout via existing pack path.
 */
static inline void
nv_nvdec_h264_apply_sps_pps(struct nv_nvdec_h264_pic_setup *ps,
                            uint32_t pic_width_in_mbs,
                            uint32_t pic_height_in_map_units,
                            uint32_t frame_mbs_only_flag,
                            uint32_t mb_adaptive_frame_field_flag,
                            uint32_t direct_8x8_inference_flag,
                            uint32_t chroma_format_idc,
                            uint32_t log2_max_frame_num_minus4,
                            uint32_t pic_order_cnt_type,
                            uint32_t log2_max_pic_order_cnt_lsb_minus4,
                            uint32_t delta_pic_order_always_zero_flag,
                            uint32_t entropy_coding_mode_flag,
                            uint32_t pic_order_present_flag,
                            uint32_t num_ref_idx_l0_default_active_minus1,
                            uint32_t num_ref_idx_l1_default_active_minus1,
                            uint32_t weighted_pred_flag,
                            uint32_t weighted_bipred_idc,
                            uint32_t pic_init_qp_minus26,
                            uint32_t chroma_qp_index_offset,
                            uint32_t second_chroma_qp_index_offset,
                            uint32_t deblocking_filter_control_present_flag,
                            uint32_t constrained_intra_pred_flag,
                            uint32_t redundant_pic_cnt_present_flag,
                            uint32_t transform_8x8_mode_flag)
{
   if (!ps)
      return;
   ps->width_mb = pic_width_in_mbs;
   ps->height_mb = pic_height_in_map_units;
   ps->frame_mbs_only_flag = frame_mbs_only_flag;
   ps->mbaff_frame_flag = mb_adaptive_frame_field_flag;
   ps->direct_8x8_inference_flag = direct_8x8_inference_flag;
   ps->chroma_format_idc = chroma_format_idc;
   ps->log2_max_frame_num_minus4 = log2_max_frame_num_minus4;
   ps->pic_order_cnt_type = pic_order_cnt_type;
   ps->log2_max_pic_order_cnt_lsb_minus4 = log2_max_pic_order_cnt_lsb_minus4;
   ps->delta_pic_order_always_zero_flag = delta_pic_order_always_zero_flag;
   ps->entropy_coding_mode_flag = entropy_coding_mode_flag;
   ps->pic_order_present_flag = pic_order_present_flag;
   ps->num_ref_idx_l0_active_minus1 = num_ref_idx_l0_default_active_minus1;
   ps->num_ref_idx_l1_active_minus1 = num_ref_idx_l1_default_active_minus1;
   ps->weighted_pred_flag = weighted_pred_flag;
   ps->weighted_bipred_idc = weighted_bipred_idc;
   ps->pic_init_qp_minus26 = pic_init_qp_minus26;
   ps->chroma_qp_index_offset = chroma_qp_index_offset;
   ps->second_chroma_qp_index_offset = second_chroma_qp_index_offset;
   ps->deblocking_filter_control_present_flag =
      deblocking_filter_control_present_flag;
   ps->constrained_intra_pred_flag = constrained_intra_pred_flag;
   ps->redundant_pic_cnt_present_flag = redundant_pic_cnt_present_flag;
   ps->transform_8x8_mode_flag = transform_8x8_mode_flag;
   ps->l0_ref_count = num_ref_idx_l0_default_active_minus1 + 1;
   ps->l1_ref_count = num_ref_idx_l1_default_active_minus1 + 1;
}

/** Set H.264 DPB reference VA for slot (luma/chroma top/bot; 0 clears). */
static inline void
nv_nvdec_h264_set_dpb_ref(struct nv_nvdec_h264_pic_setup *ps, unsigned slot,
                          uint64_t luma_top, uint64_t luma_bot,
                          uint64_t chroma_top, uint64_t chroma_bot)
{
   if (!ps || slot >= 16)
      return;
   ps->dpb_luma_top[slot] = luma_top;
   ps->dpb_luma_bot[slot] = luma_bot;
   ps->dpb_chroma_top[slot] = chroma_top;
   ps->dpb_chroma_bot[slot] = chroma_bot;
}

/**
 * Apply HEVC SPS/PPS subset (VPS/SPS/PPS logical fields) into pic_setup.
 */
static inline void
nv_nvdec_hevc_apply_sps_pps(struct nv_nvdec_hevc_pic_setup *ps,
                            uint32_t pic_width_in_luma_samples,
                            uint32_t pic_height_in_luma_samples,
                            uint32_t chroma_format_idc,
                            uint32_t bit_depth_luma_minus8,
                            uint32_t bit_depth_chroma_minus8,
                            uint32_t log2_min_luma_coding_block_size_minus3,
                            uint32_t log2_diff_max_min_luma_coding_block_size,
                            uint32_t log2_min_transform_block_size_minus2,
                            uint32_t log2_diff_max_min_transform_block_size,
                            uint32_t max_transform_hierarchy_depth_intra,
                            uint32_t max_transform_hierarchy_depth_inter,
                            uint32_t amp_enabled_flag,
                            uint32_t sample_adaptive_offset_enabled_flag,
                            uint32_t pcm_enabled_flag,
                            uint32_t pcm_loop_filter_disabled_flag,
                            uint32_t strong_intra_smoothing_enabled_flag,
                            uint32_t temporal_mvp_enabled_flag,
                            uint32_t log2_max_pic_order_cnt_lsb_minus4,
                            uint32_t num_short_term_ref_pic_sets,
                            uint32_t num_long_term_ref_pics_sps,
                            uint32_t num_ref_idx_l0_default_active_minus1,
                            uint32_t num_ref_idx_l1_default_active_minus1,
                            uint32_t init_qp_minus26,
                            uint32_t dependent_slice_segments_enabled_flag,
                            uint32_t sign_data_hiding_enabled_flag)
{
   if (!ps)
      return;
   ps->pic_width_in_luma_samples = pic_width_in_luma_samples;
   ps->pic_height_in_luma_samples = pic_height_in_luma_samples;
   ps->chroma_format_idc = chroma_format_idc;
   ps->bit_depth_luma_minus8 = bit_depth_luma_minus8;
   ps->bit_depth_chroma_minus8 = bit_depth_chroma_minus8;
   ps->log2_min_luma_coding_block_size_minus3 =
      log2_min_luma_coding_block_size_minus3;
   ps->log2_diff_max_min_luma_coding_block_size =
      log2_diff_max_min_luma_coding_block_size;
   ps->log2_min_transform_block_size_minus2 =
      log2_min_transform_block_size_minus2;
   ps->log2_diff_max_min_transform_block_size =
      log2_diff_max_min_transform_block_size;
   ps->max_transform_hierarchy_depth_intra = max_transform_hierarchy_depth_intra;
   ps->max_transform_hierarchy_depth_inter = max_transform_hierarchy_depth_inter;
   ps->amp_enabled_flag = amp_enabled_flag;
   ps->sample_adaptive_offset_enabled_flag = sample_adaptive_offset_enabled_flag;
   ps->pcm_enabled_flag = pcm_enabled_flag;
   ps->pcm_loop_filter_disabled_flag = pcm_loop_filter_disabled_flag;
   ps->strong_intra_smoothing_enabled_flag = strong_intra_smoothing_enabled_flag;
   ps->temporal_mvp_enabled_flag = temporal_mvp_enabled_flag;
   ps->log2_max_pic_order_cnt_lsb_minus4 = log2_max_pic_order_cnt_lsb_minus4;
   ps->num_short_term_ref_pic_sets = num_short_term_ref_pic_sets;
   ps->num_long_term_ref_pics_sps = num_long_term_ref_pics_sps;
   ps->num_ref_idx_l0_default_active_minus1 =
      num_ref_idx_l0_default_active_minus1;
   ps->num_ref_idx_l1_default_active_minus1 =
      num_ref_idx_l1_default_active_minus1;
   ps->init_qp_minus26 = init_qp_minus26;
   ps->dependent_slice_segments_enabled_flag =
      dependent_slice_segments_enabled_flag;
   ps->sign_data_hiding_enabled_flag = sign_data_hiding_enabled_flag;
}

/**
 * Apply AV1 sequence/frame subset into pic_setup (VAAPI/pipe_av1_picture_desc
 * fields).  NVDEC proprietary layout is serialised via av1_pic_setup_pack;
 * dword indices mirror NV_AV1_PS_* offsets in the minimal init path.
 */
static inline void
nv_nvdec_av1_apply_seq_frame(struct nv_nvdec_av1_pic_setup *ps,
                             uint32_t width, uint32_t height,
                             uint32_t profile, uint32_t bit_depth_idx,
                             uint32_t use_128x128_superblock,
                             uint32_t enable_cdef, uint32_t mono_chrome,
                             uint32_t subsampling_x, uint32_t subsampling_y,
                             uint32_t enable_order_hint,
                             uint32_t order_hint_bits_minus_1,
                             uint32_t enable_jnt_comp, uint32_t ref_frame_mvs,
                             uint32_t film_grain_params_present,
                             uint32_t frame_type, uint32_t show_frame,
                             uint32_t error_resilient_mode,
                             uint32_t disable_cdf_update,
                             uint32_t allow_screen_content_tools,
                             uint32_t force_integer_mv,
                             uint32_t use_superres, uint32_t superres_denom,
                             uint32_t allow_high_precision_mv,
                             uint32_t allow_warped_motion,
                             uint32_t primary_ref_frame, uint32_t order_hint,
                             uint32_t base_qindex,
                             uint32_t tile_cols, uint32_t tile_rows,
                             uint32_t apply_grain)
{
   if (!ps)
      return;
   if (!ps->width && !ps->height)
      memset(ps, 0, sizeof(*ps));
   ps->width = width ? width : ps->width;
   ps->height = height ? height : ps->height;
   ps->profile = profile;
   /* bit_depth_idx 0=8, 1=10, 2=12 → minus8 */
   ps->bit_depth_minus8 = bit_depth_idx ? (bit_depth_idx == 1 ? 2 : 4) : 0;
   ps->use_128x128_superblock = use_128x128_superblock ? 1 : 0;
   ps->enable_cdef = enable_cdef ? 1 : 0;
   ps->mono_chrome = mono_chrome ? 1 : 0;
   ps->subsampling_x = subsampling_x ? 1 : 0;
   ps->subsampling_y = subsampling_y ? 1 : 0;
   ps->enable_order_hint = enable_order_hint ? 1 : 0;
   ps->order_hint_bits_minus_1 = order_hint_bits_minus_1 & 7u;
   ps->enable_jnt_comp = enable_jnt_comp ? 1 : 0;
   ps->ref_frame_mvs = ref_frame_mvs ? 1 : 0;
   ps->film_grain_params_present = film_grain_params_present ? 1 : 0;
   ps->frame_type = frame_type & 3u;
   ps->intra_only = (frame_type == 2 /* INTRA_ONLY */) ? 1 : 0;
   ps->show_frame = show_frame ? 1 : 0;
   ps->error_resilient_mode = error_resilient_mode ? 1 : 0;
   ps->disable_cdf_update = disable_cdf_update ? 1 : 0;
   ps->allow_screen_content_tools = allow_screen_content_tools ? 1 : 0;
   ps->force_integer_mv = force_integer_mv ? 1 : 0;
   ps->use_superres = use_superres ? 1 : 0;
   ps->enable_superres = use_superres ? 1 : 0;
   ps->superres_denom = superres_denom ? superres_denom : 8;
   ps->allow_high_precision_mv = allow_high_precision_mv ? 1 : 0;
   ps->allow_warped_motion = allow_warped_motion ? 1 : 0;
   ps->primary_ref_frame = primary_ref_frame & 7u;
   ps->order_hint = order_hint;
   ps->base_qindex = base_qindex & 0xffu;
   ps->tile_cols = tile_cols;
   ps->tile_rows = tile_rows;
   ps->apply_grain = apply_grain ? 1 : 0;
}

static inline void
nv_nvdec_av1_set_dpb_ref(struct nv_nvdec_av1_pic_setup *ps, unsigned slot,
                         uint64_t luma_va, uint64_t chroma_va)
{
   if (!ps || slot >= 8)
      return;
   ps->dpb_luma[slot] = luma_va;
   ps->dpb_chroma[slot] = chroma_va;
}

static inline void
nv_nvdec_av1_set_ref_frame_idx(struct nv_nvdec_av1_pic_setup *ps,
                               const uint8_t ref_frame_idx[7])
{
   unsigned i;
   if (!ps)
      return;
   for (i = 0; i < 7; i++)
      ps->ref_frame_idx[i] = ref_frame_idx ? ref_frame_idx[i] : 0;
   ps->ref_frame_idx[7] = 0;
}

static inline void
nv_nvdec_hevc_set_dpb_ref(struct nv_nvdec_hevc_pic_setup *ps, unsigned slot,
                          uint64_t luma_va, uint64_t chroma_va)
{
   if (!ps || slot >= 16)
      return;
   ps->dpb_luma[slot] = luma_va;
   ps->dpb_chroma[slot] = chroma_va;
}

/* ---- Minimal Annex-B / AVCC NAL unit walker (host-side bitstream prep) ---- */

/** H.264/HEVC NAL unit types (low 5 bits for H.264; HEVC uses 6-bit type). */
#define NV_NAL_H264_TYPE_NON_IDR     1
#define NV_NAL_H264_TYPE_IDR         5
#define NV_NAL_H264_TYPE_SEI         6
#define NV_NAL_H264_TYPE_SPS         7
#define NV_NAL_H264_TYPE_PPS         8
#define NV_NAL_H264_TYPE_AUD         9
#define NV_NAL_HEVC_TYPE_VPS         32
#define NV_NAL_HEVC_TYPE_SPS         33
#define NV_NAL_HEVC_TYPE_PPS         34
#define NV_NAL_HEVC_TYPE_IDR_W_RADL  19
#define NV_NAL_HEVC_TYPE_IDR_N_LP    20
#define NV_NAL_HEVC_TYPE_CRA         21
#define NV_NAL_HEVC_TYPE_TRAIL_R     1

struct nv_nal_unit {
   const uint8_t *data;   /* points at NAL header byte (after start code) */
   uint32_t size;         /* bytes including header, excluding start code */
   uint8_t nal_type;      /* H.264: low 5 bits; HEVC: (byte0>>1)&0x3f */
   uint8_t nal_ref_idc;   /* H.264 only: (byte0>>5)&3 */
   bool is_hevc;
};

/**
 * Find next Annex-B start code (0x000001 or 0x00000001) at or after *off.
 * Returns start-code byte length (3 or 4), or 0 if none. Updates *off to
 * first byte of start code.
 */
static inline uint32_t
nv_nal_find_start_code(const uint8_t *buf, uint32_t buf_size, uint32_t *off)
{
   uint32_t i;
   if (!buf || !off || *off >= buf_size)
      return 0;
   for (i = *off; i + 3 < buf_size; i++) {
      if (buf[i] == 0 && buf[i + 1] == 0) {
         if (buf[i + 2] == 1) {
            *off = i;
            return 3;
         }
         if (i + 4 <= buf_size && buf[i + 2] == 0 && buf[i + 3] == 1) {
            *off = i;
            return 4;
         }
      }
   }
   return 0;
}

/**
 * Parse next Annex-B NAL from bitstream at *cursor. On success advances
 * *cursor past this NAL (to next start code or end). is_hevc selects type
 * extraction. Returns true if a NAL was found.
 */
static inline bool
nv_nal_next_annexb(const uint8_t *buf, uint32_t buf_size, uint32_t *cursor,
                   bool is_hevc, struct nv_nal_unit *out)
{
   uint32_t sc_off, sc_len, nal_start, next_sc, next_len, nal_end;
   if (!buf || !cursor || !out || *cursor >= buf_size)
      return false;
   sc_off = *cursor;
   sc_len = nv_nal_find_start_code(buf, buf_size, &sc_off);
   if (!sc_len)
      return false;
   nal_start = sc_off + sc_len;
   if (nal_start >= buf_size)
      return false;
   next_sc = nal_start;
   next_len = nv_nal_find_start_code(buf, buf_size, &next_sc);
   nal_end = next_len ? next_sc : buf_size;
   /* trim trailing zeros that precede next start code */
   while (nal_end > nal_start && buf[nal_end - 1] == 0)
      nal_end--;
   if (nal_end <= nal_start)
      return false;
   memset(out, 0, sizeof(*out));
   out->data = buf + nal_start;
   out->size = nal_end - nal_start;
   out->is_hevc = is_hevc;
   if (is_hevc)
      out->nal_type = (uint8_t)((out->data[0] >> 1) & 0x3f);
   else {
      out->nal_type = (uint8_t)(out->data[0] & 0x1f);
      out->nal_ref_idc = (uint8_t)((out->data[0] >> 5) & 0x3);
   }
   *cursor = next_len ? next_sc : buf_size;
   return true;
}

/**
 * Scan Annex-B buffer for first SPS/PPS (H.264) or VPS/SPS/PPS (HEVC).
 * Writes pointer/size of first matching NAL payload (header included) into
 * out_*; returns number of parameter sets found (0..3).
 */
static inline unsigned
nv_nal_find_param_sets_annexb(const uint8_t *buf, uint32_t buf_size, bool is_hevc,
                              const uint8_t **sps_out, uint32_t *sps_size,
                              const uint8_t **pps_out, uint32_t *pps_size,
                              const uint8_t **vps_out, uint32_t *vps_size)
{
   uint32_t cur = 0;
   struct nv_nal_unit nal;
   unsigned found = 0;
   if (sps_out) *sps_out = NULL;
   if (sps_size) *sps_size = 0;
   if (pps_out) *pps_out = NULL;
   if (pps_size) *pps_size = 0;
   if (vps_out) *vps_out = NULL;
   if (vps_size) *vps_size = 0;
   if (!buf || !buf_size)
      return 0;
   while (nv_nal_next_annexb(buf, buf_size, &cur, is_hevc, &nal)) {
      if (is_hevc) {
         if (nal.nal_type == NV_NAL_HEVC_TYPE_VPS && vps_out && !*vps_out) {
            *vps_out = nal.data;
            if (vps_size) *vps_size = nal.size;
            found++;
         } else if (nal.nal_type == NV_NAL_HEVC_TYPE_SPS && sps_out && !*sps_out) {
            *sps_out = nal.data;
            if (sps_size) *sps_size = nal.size;
            found++;
         } else if (nal.nal_type == NV_NAL_HEVC_TYPE_PPS && pps_out && !*pps_out) {
            *pps_out = nal.data;
            if (pps_size) *pps_size = nal.size;
            found++;
         }
      } else {
         if (nal.nal_type == NV_NAL_H264_TYPE_SPS && sps_out && !*sps_out) {
            *sps_out = nal.data;
            if (sps_size) *sps_size = nal.size;
            found++;
         } else if (nal.nal_type == NV_NAL_H264_TYPE_PPS && pps_out && !*pps_out) {
            *pps_out = nal.data;
            if (pps_size) *pps_size = nal.size;
            found++;
         }
      }
   }
   return found;
}

/**
 * True if NAL is a coded slice that NVDEC execute would consume (non-PS).
 */
static inline bool
nv_nal_is_slice(const struct nv_nal_unit *nal)
{
   if (!nal || !nal->data || !nal->size)
      return false;
   if (nal->is_hevc) {
      uint8_t t = nal->nal_type;
      return t <= 9 || t == NV_NAL_HEVC_TYPE_IDR_W_RADL ||
             t == NV_NAL_HEVC_TYPE_IDR_N_LP || t == NV_NAL_HEVC_TYPE_CRA;
   }
   return nal->nal_type >= 1 && nal->nal_type <= 5;
}

/* ---- Minimal RBSP / Exp-Golomb reader for SPS/PPS subset parse ---- */

struct nv_rbsp_reader {
   const uint8_t *data;
   uint32_t size;
   uint32_t bit_pos; /* absolute bit index into data */
};

static inline void
nv_rbsp_init(struct nv_rbsp_reader *r, const uint8_t *data, uint32_t size)
{
   if (!r)
      return;
   r->data = data;
   r->size = size;
   r->bit_pos = 0;
}

/** Read up to 32 bits (big-endian within stream). Returns 0 on underrun. */
static inline uint32_t
nv_rbsp_u(struct nv_rbsp_reader *r, unsigned nbits)
{
   uint32_t v = 0;
   unsigned i;
   if (!r || !r->data || !nbits || nbits > 32)
      return 0;
   for (i = 0; i < nbits; i++) {
      uint32_t byte_i = r->bit_pos / 8;
      uint32_t bit_i = 7 - (r->bit_pos % 8);
      if (byte_i >= r->size)
         return v;
      v = (v << 1) | ((r->data[byte_i] >> bit_i) & 1u);
      r->bit_pos++;
   }
   return v;
}

static inline uint32_t
nv_rbsp_ue(struct nv_rbsp_reader *r)
{
   unsigned lz = 0;
   if (!r)
      return 0;
   while (lz < 32) {
      uint32_t byte_i = r->bit_pos / 8;
      uint32_t bit_i = 7 - (r->bit_pos % 8);
      uint32_t bit;
      if (byte_i >= r->size)
         break;
      bit = (r->data[byte_i] >> bit_i) & 1u;
      r->bit_pos++;
      if (bit)
         break;
      lz++;
   }
   if (lz == 0)
      return 0;
   if (lz >= 32)
      return 0xffffffffu;
   return ((1u << lz) - 1u) + nv_rbsp_u(r, lz);
}

static inline int32_t
nv_rbsp_se(struct nv_rbsp_reader *r)
{
   uint32_t ue = nv_rbsp_ue(r);
   if (ue & 1u)
      return (int32_t)((ue + 1u) / 2u);
   return -(int32_t)(ue / 2u);
}

/**
 * Parse H.264 SPS NAL (payload after NAL header byte) into pic_setup fields.
 * Handles baseline/main/high profile_idc paths conservatively; skips VUI/HRD.
 * Returns 0 on success, -1 on failure.
 */
/**
 * H.264 scaling_list(sizeOfScalingList): SE deltas until 16 or 64 coefficients
 * (ITU-T H.264 7.3.2.1.1.1).  Used by SPS/PPS scaling matrix present flags.
 */
static inline void
nv_h264_rbsp_skip_scaling_list(struct nv_rbsp_reader *r, unsigned size_of_list)
{
   unsigned j, last = 8, next = 8;
   if (!r)
      return;
   if (size_of_list > 64)
      size_of_list = 64;
   for (j = 0; j < size_of_list; j++) {
      if (next)
         next = (uint32_t)((int32_t)last + nv_rbsp_se(r)) & 0xffu;
      last = next ? next : last;
   }
}

/**
 * Skip seq/pic scaling matrix tables: for each list index, if present flag,
 * consume scaling_list(16) for i<6 else scaling_list(64).
 * n_lists: 8 (4:2:0) or 12 (4:4:4) for SPS; PPS uses 6 or 2+6*transform_8x8.
 */
static inline void
nv_h264_rbsp_skip_scaling_matrices(struct nv_rbsp_reader *r, unsigned n_lists)
{
   unsigned i;
   if (!r)
      return;
   if (n_lists > 12)
      n_lists = 12;
   for (i = 0; i < n_lists; i++) {
      if (!nv_rbsp_u(r, 1))
         continue; /* scaling_list_present_flag[i] == 0 */
      nv_h264_rbsp_skip_scaling_list(r, (i < 6) ? 16u : 64u);
   }
}

static inline int
nv_h264_parse_sps_nal(const uint8_t *nal, uint32_t nal_size,
                      struct nv_nvdec_h264_pic_setup *ps)
{
   struct nv_rbsp_reader r;
   uint8_t profile_idc, level_idc, chroma_format_idc = 1;
   uint32_t seq_parameter_set_id;
   uint32_t log2_max_frame_num_minus4 = 0;
   uint32_t pic_order_cnt_type = 0;
   uint32_t log2_max_pic_order_cnt_lsb_minus4 = 0;
   uint32_t delta_pic_order_always_zero_flag = 0;
   uint32_t max_num_ref_frames;
   uint32_t pic_width_in_mbs_minus1, pic_height_in_map_units_minus1;
   uint32_t frame_mbs_only_flag = 1, mb_adaptive_frame_field_flag = 0;
   uint32_t direct_8x8_inference_flag = 0;
   if (!nal || nal_size < 4 || !ps)
      return -1;
   /* Skip NAL header (1 byte); RBSP may contain emulation prevention — ignore
    * 0x03 for this minimal path (works for most SPS without EPB in early fields). */
   nv_rbsp_init(&r, nal + 1, nal_size - 1);
   profile_idc = (uint8_t)nv_rbsp_u(&r, 8);
   (void)nv_rbsp_u(&r, 8); /* constraint_set flags + reserved_zero_2bits */
   level_idc = (uint8_t)nv_rbsp_u(&r, 8);
   (void)level_idc;
   seq_parameter_set_id = nv_rbsp_ue(&r);
   (void)seq_parameter_set_id;
   if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 ||
       profile_idc == 244 || profile_idc == 44 || profile_idc == 83 ||
       profile_idc == 86 || profile_idc == 118 || profile_idc == 128 ||
       profile_idc == 138 || profile_idc == 139 || profile_idc == 134) {
      chroma_format_idc = (uint8_t)nv_rbsp_ue(&r);
      if (chroma_format_idc == 3)
         (void)nv_rbsp_u(&r, 1); /* separate_colour_plane_flag */
      (void)nv_rbsp_ue(&r); /* bit_depth_luma_minus8 */
      (void)nv_rbsp_ue(&r); /* bit_depth_chroma_minus8 */
      (void)nv_rbsp_u(&r, 1); /* qpprime_y_zero_transform_bypass_flag */
      if (nv_rbsp_u(&r, 1)) { /* seq_scaling_matrix_present_flag */
         unsigned n = (chroma_format_idc != 3) ? 8u : 12u;
         nv_h264_rbsp_skip_scaling_matrices(&r, n);
      }
   }
   log2_max_frame_num_minus4 = nv_rbsp_ue(&r);
   pic_order_cnt_type = nv_rbsp_ue(&r);
   if (pic_order_cnt_type == 0)
      log2_max_pic_order_cnt_lsb_minus4 = nv_rbsp_ue(&r);
   else if (pic_order_cnt_type == 1) {
      delta_pic_order_always_zero_flag = nv_rbsp_u(&r, 1);
      (void)nv_rbsp_se(&r); /* offset_for_non_ref_pic */
      (void)nv_rbsp_se(&r); /* offset_for_top_to_bottom_field */
      {
         uint32_t n = nv_rbsp_ue(&r), k;
         for (k = 0; k < n && k < 256; k++)
            (void)nv_rbsp_se(&r);
      }
   }
   max_num_ref_frames = nv_rbsp_ue(&r);
   (void)nv_rbsp_u(&r, 1); /* gaps_in_frame_num_value_allowed_flag */
   pic_width_in_mbs_minus1 = nv_rbsp_ue(&r);
   pic_height_in_map_units_minus1 = nv_rbsp_ue(&r);
   frame_mbs_only_flag = nv_rbsp_u(&r, 1);
   if (!frame_mbs_only_flag)
      mb_adaptive_frame_field_flag = nv_rbsp_u(&r, 1);
   direct_8x8_inference_flag = nv_rbsp_u(&r, 1);
   /* frame_cropping / vui ignored */

   nv_nvdec_h264_apply_sps_pps(ps,
      pic_width_in_mbs_minus1 + 1,
      pic_height_in_map_units_minus1 + 1,
      frame_mbs_only_flag,
      mb_adaptive_frame_field_flag,
      direct_8x8_inference_flag,
      chroma_format_idc,
      log2_max_frame_num_minus4,
      pic_order_cnt_type,
      log2_max_pic_order_cnt_lsb_minus4,
      delta_pic_order_always_zero_flag,
      0, 0, /* entropy/pic_order from PPS */
      max_num_ref_frames ? max_num_ref_frames - 1 : 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
   return 0;
}

/**
 * Parse H.264 PPS NAL (after header) and merge entropy/ref/qp fields into ps.
 */
static inline int
nv_h264_parse_pps_nal(const uint8_t *nal, uint32_t nal_size,
                      struct nv_nvdec_h264_pic_setup *ps)
{
   struct nv_rbsp_reader r;
   uint32_t entropy_coding_mode_flag, pic_order_present_flag;
   uint32_t num_slice_groups_minus1;
   uint32_t num_ref_idx_l0, num_ref_idx_l1;
   uint32_t weighted_pred_flag, weighted_bipred_idc;
   int32_t pic_init_qp_minus26, chroma_qp_index_offset;
   uint32_t deblocking_filter_control_present_flag;
   uint32_t constrained_intra_pred_flag, redundant_pic_cnt_present_flag;
   uint32_t transform_8x8_mode_flag = 0;
   int32_t second_chroma_qp_index_offset = 0;
   if (!nal || nal_size < 2 || !ps)
      return -1;
   nv_rbsp_init(&r, nal + 1, nal_size - 1);
   (void)nv_rbsp_ue(&r); /* pic_parameter_set_id */
   (void)nv_rbsp_ue(&r); /* seq_parameter_set_id */
   entropy_coding_mode_flag = nv_rbsp_u(&r, 1);
   pic_order_present_flag = nv_rbsp_u(&r, 1);
   num_slice_groups_minus1 = nv_rbsp_ue(&r);
   if (num_slice_groups_minus1 > 0) {
      uint32_t sgmt = nv_rbsp_ue(&r);
      if (sgmt == 0) {
         uint32_t i;
         for (i = 0; i <= num_slice_groups_minus1 && i < 8; i++)
            (void)nv_rbsp_ue(&r);
      } else if (sgmt == 2) {
         uint32_t i;
         for (i = 0; i < num_slice_groups_minus1 && i < 8; i++) {
            (void)nv_rbsp_ue(&r);
            (void)nv_rbsp_ue(&r);
         }
      } else if (sgmt == 3 || sgmt == 4 || sgmt == 5) {
         (void)nv_rbsp_u(&r, 1);
         (void)nv_rbsp_ue(&r);
      } else if (sgmt == 6) {
         uint32_t pic_size = nv_rbsp_ue(&r), i;
         unsigned bits = 0, t = num_slice_groups_minus1;
         while (t) { bits++; t >>= 1; }
         if (!bits) bits = 1;
         for (i = 0; i <= pic_size && i < 8192; i++)
            (void)nv_rbsp_u(&r, bits);
      }
   }
   num_ref_idx_l0 = nv_rbsp_ue(&r);
   num_ref_idx_l1 = nv_rbsp_ue(&r);
   weighted_pred_flag = nv_rbsp_u(&r, 1);
   weighted_bipred_idc = nv_rbsp_u(&r, 2);
   pic_init_qp_minus26 = nv_rbsp_se(&r);
   (void)nv_rbsp_se(&r); /* pic_init_qs_minus26 */
   chroma_qp_index_offset = nv_rbsp_se(&r);
   deblocking_filter_control_present_flag = nv_rbsp_u(&r, 1);
   constrained_intra_pred_flag = nv_rbsp_u(&r, 1);
   redundant_pic_cnt_present_flag = nv_rbsp_u(&r, 1);
   /* More RBSP data: transform_8x8 + second chroma offset (High profile) */
   if (r.bit_pos / 8 < r.size) {
      transform_8x8_mode_flag = nv_rbsp_u(&r, 1);
      if (nv_rbsp_u(&r, 1)) { /* pic_scaling_matrix_present_flag */
         /* 6 lists always; +6 more (8x8) when transform_8x8_mode_flag */
         unsigned n = transform_8x8_mode_flag ? 12u : 6u;
         nv_h264_rbsp_skip_scaling_matrices(&r, n);
      }
      second_chroma_qp_index_offset = nv_rbsp_se(&r);
   }
   ps->entropy_coding_mode_flag = entropy_coding_mode_flag;
   ps->pic_order_present_flag = pic_order_present_flag;
   ps->num_slice_groups_minus1 = num_slice_groups_minus1;
   ps->num_ref_idx_l0_active_minus1 = num_ref_idx_l0;
   ps->num_ref_idx_l1_active_minus1 = num_ref_idx_l1;
   ps->l0_ref_count = num_ref_idx_l0 + 1;
   ps->l1_ref_count = num_ref_idx_l1 + 1;
   ps->weighted_pred_flag = weighted_pred_flag;
   ps->weighted_bipred_idc = weighted_bipred_idc;
   ps->pic_init_qp_minus26 = (uint32_t)pic_init_qp_minus26;
   ps->chroma_qp_index_offset = (uint32_t)chroma_qp_index_offset;
   ps->second_chroma_qp_index_offset = (uint32_t)second_chroma_qp_index_offset;
   ps->deblocking_filter_control_present_flag =
      deblocking_filter_control_present_flag;
   ps->constrained_intra_pred_flag = constrained_intra_pred_flag;
   ps->redundant_pic_cnt_present_flag = redundant_pic_cnt_present_flag;
   ps->transform_8x8_mode_flag = transform_8x8_mode_flag;
   return 0;
}

/**
 * Scan Annex-B H.264 bitstream and fill pic_setup from first SPS+PPS pair.
 * Returns number of parameter sets applied (0, 1, or 2).
 */
static inline unsigned
nv_h264_pic_setup_from_annexb(const uint8_t *buf, uint32_t buf_size,
                              struct nv_nvdec_h264_pic_setup *ps)
{
   const uint8_t *sps = NULL, *pps = NULL;
   uint32_t sps_sz = 0, pps_sz = 0;
   unsigned n = 0;
   if (!ps)
      return 0;
   memset(ps, 0, sizeof(*ps));
   nv_nal_find_param_sets_annexb(buf, buf_size, false,
                                 &sps, &sps_sz, &pps, &pps_sz, NULL, NULL);
   if (sps && sps_sz && nv_h264_parse_sps_nal(sps, sps_sz, ps) == 0)
      n++;
   if (pps && pps_sz && nv_h264_parse_pps_nal(pps, pps_sz, ps) == 0)
      n++;
   return n;
}


/**
 * Skip HEVC profile_tier_level() RBSP syntax (H.265 7.3.3).
 * max_sub_layers_minus1 controls sub-layer PTL arrays.
 * Returns 0 on success (best-effort; stops if RBSP runs short).
 */
static inline int
nv_hevc_rbsp_skip_profile_tier_level(struct nv_rbsp_reader *r,
                                     uint32_t max_sub_layers_minus1)
{
   uint32_t i, j;
   uint8_t sub_layer_profile_present_flag[8];
   uint8_t sub_layer_level_present_flag[8];
   if (!r)
      return -1;
   /* general_profile_space(2) tier_flag(1) profile_idc(5) */
   (void)nv_rbsp_u(r, 2);
   (void)nv_rbsp_u(r, 1);
   (void)nv_rbsp_u(r, 5);
   /* general_profile_compatibility_flag[32] */
   (void)nv_rbsp_u(r, 32);
   /* progressive/interlaced/non_packed/frame_only source flags */
   (void)nv_rbsp_u(r, 1);
   (void)nv_rbsp_u(r, 1);
   (void)nv_rbsp_u(r, 1);
   (void)nv_rbsp_u(r, 1);
   /* general_reserved_zero_44bits */
   (void)nv_rbsp_u(r, 44);
   (void)nv_rbsp_u(r, 8); /* general_level_idc */
   for (i = 0; i < max_sub_layers_minus1 && i < 8; i++) {
      sub_layer_profile_present_flag[i] = (uint8_t)nv_rbsp_u(r, 1);
      sub_layer_level_present_flag[i] = (uint8_t)nv_rbsp_u(r, 1);
   }
   if (max_sub_layers_minus1 > 0) {
      for (i = max_sub_layers_minus1; i < 8; i++)
         (void)nv_rbsp_u(r, 2); /* reserved_zero_2bits */
   }
   for (i = 0; i < max_sub_layers_minus1 && i < 8; i++) {
      if (sub_layer_profile_present_flag[i]) {
         (void)nv_rbsp_u(r, 2);
         (void)nv_rbsp_u(r, 1);
         (void)nv_rbsp_u(r, 5);
         (void)nv_rbsp_u(r, 32);
         (void)nv_rbsp_u(r, 1);
         (void)nv_rbsp_u(r, 1);
         (void)nv_rbsp_u(r, 1);
         (void)nv_rbsp_u(r, 1);
         (void)nv_rbsp_u(r, 44);
      }
      if (sub_layer_level_present_flag[i])
         (void)nv_rbsp_u(r, 8);
   }
   (void)j;
   return 0;
}

/**
 * Skip HEVC scaling_list_data() (7.3.4) when scaling_list_enabled && present.
 * Best-effort: reads scaling list present flags and SE deltas.
 */
static inline void
nv_hevc_rbsp_skip_scaling_list_data(struct nv_rbsp_reader *r)
{
   unsigned sizeId, matrixId;
   if (!r)
      return;
   for (sizeId = 0; sizeId < 4; sizeId++) {
      unsigned max_mid = (sizeId == 3) ? 2u : 6u;
      for (matrixId = 0; matrixId < max_mid; matrixId++) {
         if (!nv_rbsp_u(r, 1)) { /* scaling_list_pred_mode_flag == 0 => pred */
            (void)nv_rbsp_ue(r); /* scaling_list_pred_matrix_id_delta */
         } else {
            unsigned coef_num = (sizeId == 0) ? 16u : 64u;
            unsigned k;
            if (sizeId > 1)
               (void)nv_rbsp_se(r); /* scaling_list_dc_coef_minus8 */
            for (k = 0; k < coef_num; k++)
               (void)nv_rbsp_se(r); /* scaling_list_delta_coef */
         }
      }
   }
}

/* Host-side short_term_ref_pic_set table for correct inter-RPS RBSP walks */
#define NV_HEVC_MAX_ST_RPS   64
#define NV_HEVC_MAX_RPS_PICS 16

struct nv_hevc_st_rps_entry {
   uint8_t num_negative_pics;
   uint8_t num_positive_pics;
   uint8_t used_by_curr_s0[NV_HEVC_MAX_RPS_PICS];
   uint8_t used_by_curr_s1[NV_HEVC_MAX_RPS_PICS];
   /* Absolute POC deltas relative to the current picture (H.265 8.3.2).
    * s0 are negative (or zero) POC offsets; s1 positive.  Used to rebuild
    * predicted RPS via inter_ref_pic_set_prediction. */
   int32_t delta_poc_s0[NV_HEVC_MAX_RPS_PICS];
   int32_t delta_poc_s1[NV_HEVC_MAX_RPS_PICS];
};

struct nv_hevc_st_rps_table {
   struct nv_hevc_st_rps_entry e[NV_HEVC_MAX_ST_RPS];
   unsigned count;
};

static inline unsigned
nv_hevc_st_rps_num_delta_pocs(const struct nv_hevc_st_rps_table *t, unsigned idx)
{
   if (!t || idx >= t->count || idx >= NV_HEVC_MAX_ST_RPS)
      return 0;
   return (unsigned)t->e[idx].num_negative_pics +
          (unsigned)t->e[idx].num_positive_pics;
}

/** Sort predicted delta_poc list ascending (H.265 8.3.2 step 5/6 style). */
static inline void
nv_hevc_st_rps_sort_delta_asc(int32_t *d, uint8_t *used, unsigned n)
{
   unsigned i, j;
   for (i = 1; i < n; i++) {
      int32_t kd = d[i];
      uint8_t ku = used[i];
      j = i;
      while (j > 0 && d[j - 1] > kd) {
         d[j] = d[j - 1];
         used[j] = used[j - 1];
         j--;
      }
      d[j] = kd;
      used[j] = ku;
   }
}

/**
 * Rebuild predicted ST-RPS from reference set per H.265 8.3.2 (inter RPS pred).
 * Writes result into out; on failure leaves out zeroed.
 */
static inline void
nv_hevc_st_rps_predict(const struct nv_hevc_st_rps_entry *ref,
                       int32_t delta_rps,
                       const uint8_t *used_by_curr_flag,
                       const uint8_t *use_delta_flag,
                       unsigned num_delta_pocs_ref,
                       struct nv_hevc_st_rps_entry *out)
{
   int32_t ref_pocs[NV_HEVC_MAX_RPS_PICS * 2];
   uint8_t ref_used[NV_HEVC_MAX_RPS_PICS * 2];
   int32_t pred_pocs[NV_HEVC_MAX_RPS_PICS * 2 + 1];
   uint8_t pred_used[NV_HEVC_MAX_RPS_PICS * 2 + 1];
   unsigned n_ref = 0, n_pred = 0, i, j;
   unsigned n_neg = 0, n_pos = 0;

   if (!out)
      return;
   memset(out, 0, sizeof(*out));
   if (!ref || num_delta_pocs_ref > 32)
      return;

   /* Build RefPicSetStCurrBefore/After style list: s0 then s1 deltas */
   for (i = 0; i < ref->num_negative_pics && i < NV_HEVC_MAX_RPS_PICS; i++) {
      ref_pocs[n_ref] = ref->delta_poc_s0[i];
      ref_used[n_ref] = ref->used_by_curr_s0[i];
      n_ref++;
   }
   for (i = 0; i < ref->num_positive_pics && i < NV_HEVC_MAX_RPS_PICS; i++) {
      ref_pocs[n_ref] = ref->delta_poc_s1[i];
      ref_used[n_ref] = ref->used_by_curr_s1[i];
      n_ref++;
   }

   /* Predicted candidates: for each ref entry j, if use_delta_flag[j] keep
    * (poc + delta_rps); plus optional current-picture candidate at j==NumDeltaPocs. */
   for (j = 0; j <= num_delta_pocs_ref && j < 33; j++) {
      int32_t d;
      uint8_t use_d;
      if (j < num_delta_pocs_ref) {
         d = ref_pocs[j] + delta_rps;
         use_d = use_delta_flag ? use_delta_flag[j] : 1;
      } else {
         d = delta_rps; /* current picture in reference set */
         use_d = use_delta_flag ? use_delta_flag[j] : 1;
      }
      if (!use_d)
         continue;
      if (n_pred >= NV_HEVC_MAX_RPS_PICS * 2)
         break;
      pred_pocs[n_pred] = d;
      pred_used[n_pred] = used_by_curr_flag ? used_by_curr_flag[j] : 0;
      n_pred++;
   }

   /* Split into negative (s0) and non-negative (s1) lists, sort each */
   for (i = 0; i < n_pred; i++) {
      if (pred_pocs[i] < 0) {
         if (n_neg < NV_HEVC_MAX_RPS_PICS) {
            out->delta_poc_s0[n_neg] = pred_pocs[i];
            out->used_by_curr_s0[n_neg] = pred_used[i];
            n_neg++;
         }
      } else if (pred_pocs[i] > 0) {
         if (n_pos < NV_HEVC_MAX_RPS_PICS) {
            out->delta_poc_s1[n_pos] = pred_pocs[i];
            out->used_by_curr_s1[n_pos] = pred_used[i];
            n_pos++;
         }
      }
      /* zero POC relative to current is not placed in ST lists */
   }

   /* s0 ascending by magnitude (most negative first = ascending signed) */
   nv_hevc_st_rps_sort_delta_asc(out->delta_poc_s0, out->used_by_curr_s0, n_neg);
   nv_hevc_st_rps_sort_delta_asc(out->delta_poc_s1, out->used_by_curr_s1, n_pos);
   out->num_negative_pics = (uint8_t)n_neg;
   out->num_positive_pics = (uint8_t)n_pos;
   (void)ref_used;
}

/**
 * Parse one short_term_ref_pic_set(stRpsIdx) per H.265 7.3.7 into table slot
 * stRpsIdx (when table non-NULL) so inter_ref_pic_set_prediction reads the
 * exact NumDeltaPocs(RefRpsIdx) flag pairs and rebuilds predicted POC deltas.
 * NVDEC still gets refs via DPB; this keeps RBSP alignment and host-side RPS.
 *
 * stRpsIdx: index in SPS loop (0 .. num_short_term_ref_pic_sets-1)
 * num_st_rps_total: total SPS short-term sets (for delta_idx in slice RPS only)
 */
static inline void
nv_hevc_rbsp_parse_short_term_ref_pic_set(struct nv_rbsp_reader *r,
                                          unsigned stRpsIdx,
                                          unsigned num_st_rps_total,
                                          struct nv_hevc_st_rps_table *table)
{
   uint32_t inter_ref_pic_set_prediction_flag = 0;
   uint32_t num_negative_pics = 0, num_positive_pics = 0;
   unsigned i;
   struct nv_hevc_st_rps_entry *slot = NULL;

   if (!r)
      return;
   if (table && stRpsIdx < NV_HEVC_MAX_ST_RPS) {
      slot = &table->e[stRpsIdx];
      memset(slot, 0, sizeof(*slot));
      if (stRpsIdx + 1 > table->count)
         table->count = stRpsIdx + 1;
   }

   if (stRpsIdx != 0)
      inter_ref_pic_set_prediction_flag = nv_rbsp_u(r, 1);

   if (inter_ref_pic_set_prediction_flag) {
      uint32_t delta_idx_minus1 = 0;
      uint32_t delta_rps_sign = 0;
      uint32_t abs_delta_rps_minus1 = 0;
      int32_t delta_rps = 0;
      unsigned RefRpsIdx;
      unsigned num_delta_pocs;
      uint8_t used_flags[33];
      uint8_t use_delta_flags[33];
      memset(used_flags, 0, sizeof(used_flags));
      memset(use_delta_flags, 1, sizeof(use_delta_flags)); /* default keep */

      if (stRpsIdx == num_st_rps_total)
         delta_idx_minus1 = nv_rbsp_ue(r);
      delta_rps_sign = nv_rbsp_u(r, 1);
      abs_delta_rps_minus1 = nv_rbsp_ue(r);
      delta_rps = (1 - 2 * (int32_t)delta_rps_sign) *
                  ((int32_t)abs_delta_rps_minus1 + 1);

      RefRpsIdx = stRpsIdx - (delta_idx_minus1 + 1);
      if (RefRpsIdx >= NV_HEVC_MAX_ST_RPS)
         RefRpsIdx = 0;
      num_delta_pocs = table ? nv_hevc_st_rps_num_delta_pocs(table, RefRpsIdx)
                             : 0;
      /* Fallback if table missing/empty: still drain a modest number of flags */
      if (!num_delta_pocs)
         num_delta_pocs = 16;
      if (num_delta_pocs > 32)
         num_delta_pocs = 32;

      /* used_by_curr_pic_flag[j] + optional use_delta_flag[j] for j=0..NumDeltaPocs */
      for (i = 0; i <= num_delta_pocs && i < 33; i++) {
         uint32_t used = nv_rbsp_u(r, 1);
         used_flags[i] = (uint8_t)used;
         if (!used) {
            uint32_t ud = nv_rbsp_u(r, 1);
            use_delta_flags[i] = (uint8_t)ud;
         } else {
            use_delta_flags[i] = 1;
         }
         if (r->bit_pos / 8 >= r->size)
            break;
      }

      /* Predicted set: full POC delta rebuild from reference entry */
      if (slot && table && RefRpsIdx < table->count) {
         nv_hevc_st_rps_predict(&table->e[RefRpsIdx], delta_rps,
                                used_flags, use_delta_flags, num_delta_pocs,
                                slot);
      }
   } else {
      int32_t poc = 0;
      num_negative_pics = nv_rbsp_ue(r);
      num_positive_pics = nv_rbsp_ue(r);
      if (num_negative_pics > NV_HEVC_MAX_RPS_PICS)
         num_negative_pics = NV_HEVC_MAX_RPS_PICS;
      if (num_positive_pics > NV_HEVC_MAX_RPS_PICS)
         num_positive_pics = NV_HEVC_MAX_RPS_PICS;
      if (slot) {
         slot->num_negative_pics = (uint8_t)num_negative_pics;
         slot->num_positive_pics = (uint8_t)num_positive_pics;
      }
      poc = 0;
      for (i = 0; i < num_negative_pics; i++) {
         uint32_t dpm1 = nv_rbsp_ue(r); /* delta_poc_s0_minus1 */
         poc -= (int32_t)(dpm1 + 1);
         {
            uint32_t u = nv_rbsp_u(r, 1);
            if (slot && i < NV_HEVC_MAX_RPS_PICS) {
               slot->delta_poc_s0[i] = poc;
               slot->used_by_curr_s0[i] = (uint8_t)u;
            }
         }
      }
      poc = 0;
      for (i = 0; i < num_positive_pics; i++) {
         uint32_t dpm1 = nv_rbsp_ue(r); /* delta_poc_s1_minus1 */
         poc += (int32_t)(dpm1 + 1);
         {
            uint32_t u = nv_rbsp_u(r, 1);
            if (slot && i < NV_HEVC_MAX_RPS_PICS) {
               slot->delta_poc_s1[i] = poc;
               slot->used_by_curr_s1[i] = (uint8_t)u;
            }
         }
      }
   }
}

/** Walk one ST-RPS without storing (legacy name; uses NULL table). */
static inline void
nv_hevc_rbsp_skip_short_term_ref_pic_set(struct nv_rbsp_reader *r,
                                         unsigned stRpsIdx,
                                         unsigned num_st_rps_total)
{
   nv_hevc_rbsp_parse_short_term_ref_pic_set(r, stRpsIdx, num_st_rps_total,
                                             NULL);
}

/**
 * HEVC SPS subset: pic size, chroma, bit depths, coding/transform block sizes.
 * Walks scaling lists + short_term_ref_pic_set; skips VUI. Returns 0 on success.
 */
static inline int
nv_hevc_parse_sps_nal(const uint8_t *nal, uint32_t nal_size,
                      struct nv_nvdec_hevc_pic_setup *ps)
{
   struct nv_rbsp_reader r;
   uint32_t chroma_format_idc = 1;
   uint32_t pic_width, pic_height;
   uint32_t bit_depth_luma_minus8 = 0, bit_depth_chroma_minus8 = 0;
   uint32_t log2_min_luma_coding_block_size_minus3;
   uint32_t log2_diff_max_min_luma_coding_block_size;
   uint32_t log2_min_transform_block_size_minus2;
   uint32_t log2_diff_max_min_transform_block_size;
   uint32_t max_transform_hierarchy_depth_inter;
   uint32_t max_transform_hierarchy_depth_intra;
   if (!nal || nal_size < 4 || !ps)
      return -1;
   uint32_t sps_max_sub_layers_minus1 = 0;
   uint32_t log2_max_pic_order_cnt_lsb_minus4 = 0;
   uint32_t amp_enabled_flag = 0, sao_enabled = 0, pcm_enabled = 0;
   uint32_t pcm_loop_filter_disabled = 0, temporal_mvp = 0, strong_intra = 0;
   uint32_t num_short_term_ref_pic_sets = 0, num_long_term_ref_pics_sps = 0;
   /* HEVC NAL header is 2 bytes */
   nv_rbsp_init(&r, nal + 2, nal_size > 2 ? nal_size - 2 : 0);
   (void)nv_rbsp_u(&r, 4); /* sps_video_parameter_set_id */
   sps_max_sub_layers_minus1 = nv_rbsp_u(&r, 3);
   (void)nv_rbsp_u(&r, 1); /* sps_temporal_id_nesting_flag */
   nv_hevc_rbsp_skip_profile_tier_level(&r, sps_max_sub_layers_minus1);
   (void)nv_rbsp_ue(&r); /* sps_seq_parameter_set_id */
   chroma_format_idc = nv_rbsp_ue(&r);
   if (chroma_format_idc == 3)
      (void)nv_rbsp_u(&r, 1);
   pic_width = nv_rbsp_ue(&r);
   pic_height = nv_rbsp_ue(&r);
   if (nv_rbsp_u(&r, 1)) { /* conformance_window_flag */
      (void)nv_rbsp_ue(&r);
      (void)nv_rbsp_ue(&r);
      (void)nv_rbsp_ue(&r);
      (void)nv_rbsp_ue(&r);
   }
   bit_depth_luma_minus8 = nv_rbsp_ue(&r);
   bit_depth_chroma_minus8 = nv_rbsp_ue(&r);
   log2_max_pic_order_cnt_lsb_minus4 = nv_rbsp_ue(&r);
   if (nv_rbsp_u(&r, 1)) { /* sps_sub_layer_ordering_info_present_flag */
      unsigned i;
      for (i = 0; i <= sps_max_sub_layers_minus1 && i < 8; i++) {
         (void)nv_rbsp_ue(&r); /* max_dec_pic_buffering_minus1 */
         (void)nv_rbsp_ue(&r); /* max_num_reorder_pics */
         (void)nv_rbsp_ue(&r); /* max_latency_increase_plus1 */
      }
   } else {
      (void)nv_rbsp_ue(&r);
      (void)nv_rbsp_ue(&r);
      (void)nv_rbsp_ue(&r);
   }
   log2_min_luma_coding_block_size_minus3 = nv_rbsp_ue(&r);
   log2_diff_max_min_luma_coding_block_size = nv_rbsp_ue(&r);
   log2_min_transform_block_size_minus2 = nv_rbsp_ue(&r);
   log2_diff_max_min_transform_block_size = nv_rbsp_ue(&r);
   max_transform_hierarchy_depth_inter = nv_rbsp_ue(&r);
   max_transform_hierarchy_depth_intra = nv_rbsp_ue(&r);
   if (nv_rbsp_u(&r, 1)) { /* scaling_list_enabled_flag */
      if (nv_rbsp_u(&r, 1)) /* sps_scaling_list_data_present_flag */
         nv_hevc_rbsp_skip_scaling_list_data(&r);
   }
   amp_enabled_flag = nv_rbsp_u(&r, 1);
   sao_enabled = nv_rbsp_u(&r, 1);
   pcm_enabled = nv_rbsp_u(&r, 1);
   if (pcm_enabled) {
      (void)nv_rbsp_u(&r, 4); /* pcm_sample_bit_depth_luma_minus1 */
      (void)nv_rbsp_u(&r, 4);
      (void)nv_rbsp_ue(&r); /* log2_min_pcm_luma_coding_block_size_minus3 */
      (void)nv_rbsp_ue(&r); /* log2_diff_max_min_pcm_luma_coding_block_size */
      pcm_loop_filter_disabled = nv_rbsp_u(&r, 1);
   }
   num_short_term_ref_pic_sets = nv_rbsp_ue(&r);
   /* Parse short_term_ref_pic_set[i] with host RPS table for inter-RPS walks */
   {
      struct nv_hevc_st_rps_table st_rps;
      unsigned st_i;
      memset(&st_rps, 0, sizeof(st_rps));
      for (st_i = 0; st_i < num_short_term_ref_pic_sets && st_i < NV_HEVC_MAX_ST_RPS;
           st_i++)
         nv_hevc_rbsp_parse_short_term_ref_pic_set(&r, st_i,
                                                   num_short_term_ref_pic_sets,
                                                   &st_rps);
   }
   if (nv_rbsp_u(&r, 1)) { /* long_term_ref_pics_present_flag */
      num_long_term_ref_pics_sps = nv_rbsp_ue(&r);
      {
         unsigned i;
         uint32_t lt_poc_lsb_bits = log2_max_pic_order_cnt_lsb_minus4 + 4;
         /* Walk SPS LT list fully so RBSP position stays aligned for
          * temporal_mvp / strong_intra; store counts in pic_setup for NVDEC. */
         if (num_long_term_ref_pics_sps > 32)
            num_long_term_ref_pics_sps = 32;
         for (i = 0; i < num_long_term_ref_pics_sps; i++) {
            (void)nv_rbsp_u(&r, lt_poc_lsb_bits); /* lt_ref_pic_poc_lsb_sps */
            (void)nv_rbsp_u(&r, 1);               /* used_by_curr_pic_lt_sps_flag */
         }
      }
   }
   temporal_mvp = nv_rbsp_u(&r, 1);
   strong_intra = nv_rbsp_u(&r, 1);
   nv_nvdec_hevc_apply_sps_pps(ps, pic_width, pic_height, chroma_format_idc,
      bit_depth_luma_minus8, bit_depth_chroma_minus8,
      log2_min_luma_coding_block_size_minus3,
      log2_diff_max_min_luma_coding_block_size,
      log2_min_transform_block_size_minus2,
      log2_diff_max_min_transform_block_size,
      max_transform_hierarchy_depth_intra,
      max_transform_hierarchy_depth_inter,
      amp_enabled_flag, sao_enabled, pcm_enabled, pcm_loop_filter_disabled,
      strong_intra, temporal_mvp, log2_max_pic_order_cnt_lsb_minus4,
      num_short_term_ref_pic_sets, num_long_term_ref_pics_sps,
      0, 0, 0, 0, 0); /* l0/l1/init_qp/dependent_slice/sign_hiding from PPS */
   return 0;
}

/**
 * Walk slice-segment LT RPS tail after ST RPS (H.265 7.3.6.1 subset).
 * Only drains RBSP; NVDEC gets LT refs via DPB picture_desc when provided.
 */
static inline void
nv_hevc_rbsp_skip_slice_lt_rps(struct nv_rbsp_reader *r,
                               uint32_t num_long_term_sps,
                               uint32_t log2_max_pic_order_cnt_lsb_minus4,
                               bool long_term_ref_pics_present)
{
   uint32_t num_long_term_pics = 0;
   uint32_t num_long_term_sps_active = 0;
   unsigned i;
   uint32_t lsb_bits;

   if (!r || !long_term_ref_pics_present)
      return;
   if (num_long_term_sps > 0)
      num_long_term_sps_active = nv_rbsp_ue(r); /* num_long_term_sps */
   num_long_term_pics = nv_rbsp_ue(r);         /* num_long_term_pics */
   if (num_long_term_sps_active > 32)
      num_long_term_sps_active = 32;
   if (num_long_term_pics > 32)
      num_long_term_pics = 32;
   lsb_bits = log2_max_pic_order_cnt_lsb_minus4 + 4;
   for (i = 0; i < num_long_term_sps_active; i++) {
      if (num_long_term_sps > 1)
         (void)nv_rbsp_u(r, 1); /* lt_idx_sps may be coded as ue; drain 1+ bits conservatively via ue if large */
      /* used_by_curr_pic_lt_flag */
      (void)nv_rbsp_u(r, 1);
   }
   for (i = 0; i < num_long_term_pics; i++) {
      (void)nv_rbsp_u(r, lsb_bits); /* poc_lsb_lt */
      (void)nv_rbsp_u(r, 1);        /* used_by_curr_pic_lt_flag */
      if (nv_rbsp_u(r, 1))          /* delta_poc_msb_present_flag */
         (void)nv_rbsp_ue(r);       /* delta_poc_msb_cycle_lt */
   }
   (void)num_long_term_sps;
}

static inline unsigned
nv_hevc_pic_setup_from_annexb(const uint8_t *buf, uint32_t buf_size,
                              struct nv_nvdec_hevc_pic_setup *ps)
{
   const uint8_t *sps = NULL;
   uint32_t sps_sz = 0;
   if (!ps)
      return 0;
   memset(ps, 0, sizeof(*ps));
   nv_nal_find_param_sets_annexb(buf, buf_size, true,
                                 &sps, &sps_sz, NULL, NULL, NULL, NULL);
   if (sps && sps_sz && nv_hevc_parse_sps_nal(sps, sps_sz, ps) == 0)
      return 1;
   return 0;
}

/* Build frame_setup + write pic_setup BO from codec struct; returns 0 on success */
static inline int
nv_nvdec_fill_frame_from_h264(struct nv_nvdec_frame_setup *fs,
                              const struct nv_nvdec_h264_pic_setup *ps,
                              uint64_t pic_setup_gpu_addr,
                              void *pic_setup_cpu_map,
                              uint32_t pic_setup_map_bytes,
                              uint64_t bitstream_gpu_addr,
                              uint32_t bitstream_size,
                              uint32_t picture_index)
{
   if (!fs || !ps)
      return -1;
   memset(fs, 0, sizeof(*fs));
   fs->app_id = NV_NVDEC_APP_ID_H264;
   fs->picture_index = picture_index;
   fs->pic_setup_gpu_addr = pic_setup_gpu_addr;
   fs->bitstream_gpu_addr = bitstream_gpu_addr;
   fs->bitstream_size = bitstream_size;
   fs->execute_flags = 1;
   if (pic_setup_cpu_map && pic_setup_map_bytes >= NV_NVDEC_H264_PIC_SETUP_DWORDS * 4)
      nv_nvdec_h264_pic_setup_pack(ps, (uint32_t *)pic_setup_cpu_map,
                                   pic_setup_map_bytes / 4);
   return 0;
}

/**
 * Host-side NVDEC decode session: owns pic_setup/status scratch layout hints
 * and sequences frame executes.  BOs are provided by the caller (Vulkan video
 * / gallium VA path); this struct is the method-level session state.
 */
struct nv_nvdec_session {
   uint32_t class_nvdec;       /* from nv_device_info.class_nvdec */
   uint32_t app_id;            /* NV_NVDEC_APP_ID_* */
   uint32_t next_picture_index;
   uint32_t pic_setup_bytes;
   uint64_t pic_setup_gpu_addr;
   void *pic_setup_cpu_map;    /* optional host map of pic_setup BO */
   uint32_t pic_setup_map_bytes;
   uint64_t status_gpu_addr;
   volatile uint32_t *status_cpu_map; /* optional host map for sema wait */
   uint64_t output_luma_gpu_addr;
   uint64_t output_chroma_gpu_addr;
   uint32_t output_luma_pitch;
   uint32_t output_chroma_pitch;
   struct nv_nvdec_h264_pic_setup h264_ps;
   struct nv_nvdec_hevc_pic_setup hevc_ps;
   struct nv_nvdec_av1_pic_setup av1_ps;
   struct nv_nvdec_vp9_pic_setup vp9_ps;
   bool h264_ps_valid;
   bool hevc_ps_valid;
   bool av1_ps_valid;
   bool vp9_ps_valid;
   bool object_set;            /* SET_OBJECT emitted on channel */
};

static inline void
nv_nvdec_session_init(struct nv_nvdec_session *s, uint32_t class_nvdec,
                      uint32_t app_id)
{
   if (!s)
      return;
   memset(s, 0, sizeof(*s));
   s->class_nvdec = class_nvdec;
   s->app_id = app_id ? app_id : NV_NVDEC_APP_ID_H264;
   s->pic_setup_bytes = nv_nvdec_pic_setup_size(s->app_id);
}

static inline void
nv_nvdec_session_set_pic_setup_bo(struct nv_nvdec_session *s,
                                  uint64_t gpu_addr, void *cpu_map,
                                  uint32_t map_bytes)
{
   if (!s)
      return;
   s->pic_setup_gpu_addr = gpu_addr;
   s->pic_setup_cpu_map = cpu_map;
   s->pic_setup_map_bytes = map_bytes;
}

static inline void
nv_nvdec_session_set_output(struct nv_nvdec_session *s,
                            uint64_t luma_gpu, uint64_t chroma_gpu,
                            uint32_t luma_pitch, uint32_t chroma_pitch)
{
   if (!s)
      return;
   s->output_luma_gpu_addr = luma_gpu;
   s->output_chroma_gpu_addr = chroma_gpu;
   s->output_luma_pitch = luma_pitch;
   s->output_chroma_pitch = chroma_pitch;
}

/** Optional status BO (NVDEC writes decode completion / error dword). */
static inline void
nv_nvdec_session_set_status_bo(struct nv_nvdec_session *s, uint64_t gpu_addr)
{
   if (!s)
      return;
   s->status_gpu_addr = gpu_addr;
}

/** Host-mapped status dword for sema/status wait (may be NULL). */
static inline void
nv_nvdec_session_set_status_cpu_map(struct nv_nvdec_session *s,
                                    volatile uint32_t *cpu_map)
{
   if (!s)
      return;
   s->status_cpu_map = cpu_map;
}

/** Wait for last emit_frame sema payload (pic_idx+1) with GEQ. */
static inline int
nv_nvdec_session_wait_last_frame(struct nv_nvdec_session *s,
                                 uint64_t timeout_ns)
{
   if (!s || !s->status_cpu_map || !s->next_picture_index)
      return -EINVAL;
   return nv_nvdec_wait_status_geq(s->status_cpu_map, s->next_picture_index,
                                   timeout_ns);
}

/**
 * Program H.264 DPB reference for slot (mirrors nv_nvdec_h264_set_dpb_ref).
 * Also updates curr_pic_idx when slot matches next_picture_index.
 */
static inline void
nv_nvdec_session_set_h264_dpb(struct nv_nvdec_session *s, unsigned slot,
                              uint64_t luma_top, uint64_t luma_bot,
                              uint64_t chroma_top, uint64_t chroma_bot)
{
   if (!s || slot >= 16)
      return;
   nv_nvdec_h264_set_dpb_ref(&s->h264_ps, slot, luma_top, luma_bot,
                             chroma_top, chroma_bot);
   s->h264_ps.dpb_luma_pitch = s->output_luma_pitch;
   s->h264_ps.dpb_chroma_pitch = s->output_chroma_pitch;
   s->h264_ps_valid = true;
}

/** Program HEVC DPB reference for slot. */
static inline void
nv_nvdec_session_set_hevc_dpb(struct nv_nvdec_session *s, unsigned slot,
                              uint64_t luma_va, uint64_t chroma_va)
{
   if (!s || slot >= 16)
      return;
   nv_nvdec_hevc_set_dpb_ref(&s->hevc_ps, slot, luma_va, chroma_va);
   s->hevc_ps.dpb_luma_pitch = s->output_luma_pitch;
   s->hevc_ps.dpb_chroma_pitch = s->output_chroma_pitch;
   s->hevc_ps_valid = true;
}

/** Program AV1 reference frame slot (8-frame DPB). */
static inline void
nv_nvdec_session_set_av1_dpb(struct nv_nvdec_session *s, unsigned slot,
                             uint64_t luma_va, uint64_t chroma_va)
{
   if (!s || slot >= 8)
      return;
   nv_nvdec_av1_set_dpb_ref(&s->av1_ps, slot, luma_va, chroma_va);
   s->av1_ps.dpb_luma_pitch = s->output_luma_pitch;
   s->av1_ps.dpb_chroma_pitch = s->output_chroma_pitch;
   s->av1_ps.output_luma_gpu_addr = s->output_luma_gpu_addr;
   s->av1_ps.output_chroma_gpu_addr = s->output_chroma_gpu_addr;
   s->av1_ps_valid = true;
}

/** Program VP9 reference frame slot (8-frame DPB; last/golden/alt via indices). */
static inline void
nv_nvdec_session_set_vp9_dpb(struct nv_nvdec_session *s, unsigned slot,
                             uint64_t luma_va, uint64_t chroma_va)
{
   if (!s || slot >= 8)
      return;
   nv_nvdec_vp9_set_dpb_ref(&s->vp9_ps, slot, luma_va, chroma_va);
   s->vp9_ps.dpb_luma_pitch = s->output_luma_pitch;
   s->vp9_ps.dpb_chroma_pitch = s->output_chroma_pitch;
   s->vp9_ps.output_luma_gpu_addr = s->output_luma_gpu_addr;
   s->vp9_ps.output_chroma_gpu_addr = s->output_chroma_gpu_addr;
   s->vp9_ps_valid = true;
}

/**
 * Pack session pic_setup structs into the host-mapped pic_setup BO (if any).
 * Call after SPS/PPS load and DPB updates, before emit_frame.
 */
static inline void
nv_nvdec_session_pack_pic_setup(struct nv_nvdec_session *s)
{
   uint32_t dwords;
   uint32_t *pic;
   if (!s || !s->pic_setup_cpu_map || !s->pic_setup_map_bytes)
      return;
   dwords = s->pic_setup_map_bytes / 4;
   pic = (uint32_t *)s->pic_setup_cpu_map;
   if (s->app_id == NV_NVDEC_APP_ID_VP9 && s->vp9_ps_valid) {
      s->vp9_ps.curr_pic_idx = s->next_picture_index;
      s->vp9_ps.output_luma_gpu_addr = s->output_luma_gpu_addr;
      s->vp9_ps.output_chroma_gpu_addr = s->output_chroma_gpu_addr;
      nv_nvdec_vp9_pic_setup_pack(&s->vp9_ps, pic, dwords);
      /* Minimal dword header: width/height in dwords 0/1 style */
      if (dwords > 0)
         pic[0] = (s->vp9_ps.height << 16) | (s->vp9_ps.width & 0xffffu);
      if (dwords > 1)
         pic[1] = (s->vp9_ps.profile & 7u) |
                  ((s->vp9_ps.bit_depth & 0x1fu) << 8) |
                  (s->vp9_ps.frame_type ? (1u << 16) : 0) |
                  (s->vp9_ps.show_frame ? (1u << 17) : 0) |
                  (s->vp9_ps.intra_only ? (1u << 18) : 0) |
                  (s->vp9_ps.error_resilient_mode ? (1u << 19) : 0);
      if (dwords > 2)
         pic[2] = (s->vp9_ps.last_ref_frame & 7u) |
                  ((s->vp9_ps.golden_ref_frame & 7u) << 4) |
                  ((s->vp9_ps.alt_ref_frame & 7u) << 8) |
                  ((s->vp9_ps.base_qindex & 0xffu) << 16);
      if (s->output_luma_gpu_addr && dwords > 32)
         pic[32] = (uint32_t)(s->output_luma_gpu_addr >> 8);
      if (s->output_chroma_gpu_addr && dwords > 33)
         pic[33] = (uint32_t)(s->output_chroma_gpu_addr >> 8);
   } else if (s->app_id == NV_NVDEC_APP_ID_AV1 && s->av1_ps_valid) {
      s->av1_ps.curr_pic_idx = s->next_picture_index;
      s->av1_ps.output_luma_gpu_addr = s->output_luma_gpu_addr;
      s->av1_ps.output_chroma_gpu_addr = s->output_chroma_gpu_addr;
      nv_nvdec_av1_pic_setup_pack(&s->av1_ps, pic, dwords);
      /* Overlay NV_AV1_PS_* dword slots expected by minimal init / firmware */
      if (dwords > NV_AV1_PS_FRAME_WH)
         pic[NV_AV1_PS_FRAME_WH] =
            ((s->av1_ps.height & 0xffffu) << 16) | (s->av1_ps.width & 0xffffu);
      if (dwords > NV_AV1_PS_PROF_TIER_LEVEL)
         pic[NV_AV1_PS_PROF_TIER_LEVEL] =
            (s->av1_ps.profile & 7u) |
            ((s->av1_ps.bit_depth_minus8 & 0xfu) << 8);
      if (dwords > NV_AV1_PS_FRAME_TYPE_FLAGS)
         pic[NV_AV1_PS_FRAME_TYPE_FLAGS] =
            (s->av1_ps.frame_type & 3u) |
            (s->av1_ps.show_frame ? (1u << 2) : 0) |
            (s->av1_ps.error_resilient_mode ? (1u << 3) : 0) |
            (s->av1_ps.use_128x128_superblock ? (1u << 8) : 0) |
            (s->av1_ps.mono_chrome ? (1u << 9) : 0) |
            (s->av1_ps.enable_cdef ? (1u << 10) : 0) |
            (s->av1_ps.apply_grain ? (1u << 11) : 0);
      if (dwords > NV_AV1_PS_ORDER_HINT)
         pic[NV_AV1_PS_ORDER_HINT] = s->av1_ps.order_hint;
      if (dwords > NV_AV1_PS_PRIMARY_REF)
         pic[NV_AV1_PS_PRIMARY_REF] = s->av1_ps.primary_ref_frame & 7u;
      if (s->output_luma_gpu_addr && dwords > NV_AV1_PS_OUTPUT_LUMA_OFF)
         pic[NV_AV1_PS_OUTPUT_LUMA_OFF] =
            (uint32_t)(s->output_luma_gpu_addr >> 8);
      if (s->output_chroma_gpu_addr && dwords > NV_AV1_PS_OUTPUT_CHROMA_OFF)
         pic[NV_AV1_PS_OUTPUT_CHROMA_OFF] =
            (uint32_t)(s->output_chroma_gpu_addr >> 8);
   } else if (s->app_id == NV_NVDEC_APP_ID_HEVC && s->hevc_ps_valid) {
      s->hevc_ps.curr_pic_idx = s->next_picture_index;
      nv_nvdec_hevc_pic_setup_pack(&s->hevc_ps, pic, dwords);
   } else if (s->h264_ps_valid) {
      s->h264_ps.curr_pic_idx = s->next_picture_index;
      nv_nvdec_h264_pic_setup_pack(&s->h264_ps, pic, dwords);
   }
}

/**
 * Load parameter sets from Annex-B SPS/PPS into session pic_setup structs.
 * Returns number of PS applied (H.264: 0..2, HEVC: 0..1 SPS).
 */
static inline unsigned
nv_nvdec_session_load_annexb_ps(struct nv_nvdec_session *s,
                                const uint8_t *annexb, uint32_t annexb_size)
{
   unsigned n = 0;
   if (!s || !annexb || !annexb_size)
      return 0;
   if (s->app_id == NV_NVDEC_APP_ID_HEVC) {
      n = nv_hevc_pic_setup_from_annexb(annexb, annexb_size, &s->hevc_ps);
      s->hevc_ps_valid = (n > 0);
   } else {
      n = nv_h264_pic_setup_from_annexb(annexb, annexb_size, &s->h264_ps);
      s->h264_ps_valid = (n > 0);
   }
   return n;
}

/**
 * Build frame_setup for one picture and emit NVDEC methods + EXECUTE.
 * bitstream_gpu/size point at slice/NAL data for this frame (may include PS).
 * Returns 0 on success.
 */
static inline int
nv_nvdec_session_emit_frame(struct nv_push *p, struct nv_nvdec_session *s,
                            uint64_t bitstream_gpu_addr, uint32_t bitstream_size)
{
   struct nv_nvdec_frame_setup fs;
   uint32_t pic_idx;
   if (!p || !s || !bitstream_gpu_addr || !bitstream_size)
      return -1;
   if (!s->object_set && s->class_nvdec) {
      nv_nvdec_set_object(p, s->class_nvdec);
      s->object_set = true;
   } else if (!s->object_set) {
      nv_push_set_subch(p, NV_PUSH_SUBCH_NVDEC);
   }
   pic_idx = s->next_picture_index++;
   memset(&fs, 0, sizeof(fs));
   fs.app_id = s->app_id;
   fs.picture_index = pic_idx;
   fs.bitstream_gpu_addr = bitstream_gpu_addr;
   fs.bitstream_size = bitstream_size;
   fs.pic_setup_gpu_addr = s->pic_setup_gpu_addr;
   fs.status_gpu_addr = s->status_gpu_addr;
   fs.output_luma_gpu_addr = s->output_luma_gpu_addr;
   fs.output_chroma_gpu_addr = s->output_chroma_gpu_addr;
   fs.output_luma_pitch = s->output_luma_pitch;
   fs.output_chroma_pitch = s->output_chroma_pitch;
   fs.execute_flags = 1;
   /* Arm status sema (payload = picture_index+1); reset host map before submit */
   if (s->status_gpu_addr)
      nv_nvdec_emit_semaphore_release_reset(p, s->status_gpu_addr, pic_idx + 1,
                                            s->status_cpu_map);
   if (s->app_id == NV_NVDEC_APP_ID_HEVC && s->hevc_ps_valid) {
      fs.mb_width = s->hevc_ps.pic_width_in_luma_samples;
      fs.mb_height = s->hevc_ps.pic_height_in_luma_samples;
      fs.bit_depth_luma_minus8 = s->hevc_ps.bit_depth_luma_minus8;
      fs.bit_depth_chroma_minus8 = s->hevc_ps.bit_depth_chroma_minus8;
      if (s->pic_setup_cpu_map &&
          s->pic_setup_map_bytes >= NV_NVDEC_HEVC_PIC_SETUP_DWORDS * 4)
         nv_nvdec_hevc_pic_setup_pack(&s->hevc_ps,
                                      (uint32_t *)s->pic_setup_cpu_map,
                                      s->pic_setup_map_bytes / 4);
   } else if (s->h264_ps_valid) {
      fs.mb_width = s->h264_ps.width_mb;
      fs.mb_height = s->h264_ps.height_mb;
      if (s->pic_setup_cpu_map &&
          s->pic_setup_map_bytes >= NV_NVDEC_H264_PIC_SETUP_DWORDS * 4)
         nv_nvdec_h264_pic_setup_pack(&s->h264_ps,
                                      (uint32_t *)s->pic_setup_cpu_map,
                                      s->pic_setup_map_bytes / 4);
   }
   nv_nvdec_emit_frame_setup(p, &fs);
   nv_nvdec_emit_execute(p, s->app_id, fs.execute_flags);
   nv_push_wfi(p);
   return 0;
}

/**
 * tick123: build nv_nvdec_pic_setup method args from session + bitstream BO.
 * Used by Gallium/channel sema submit path (nv_channel_nvdec_frame_sema_submit).
 */
static inline void
nv_nvdec_session_fill_pic_setup_methods(const struct nv_nvdec_session *s,
                                        uint64_t bitstream_gpu_addr,
                                        uint32_t bitstream_size,
                                        struct nv_nvdec_pic_setup *out)
{
   if (!out)
      return;
   memset(out, 0, sizeof(*out));
   if (!s)
      return;
   out->app_id = s->app_id;
   out->picture_index = s->next_picture_index;
   out->pic_setup_gpu = s->pic_setup_gpu_addr;
   out->bitstream_gpu = bitstream_gpu_addr;
   out->execute_flags = 1u;
   out->display_buf_size = bitstream_size;
   if (s->status_gpu_addr)
      out->history_gpu = 0; /* status is sema path, not history */
   (void)bitstream_size;
}

/**
 * tick123: emit frame via pic_setup methods + sema + EXECUTE (no WFI).
 * Prefer when caller will wait on status sema (Gallium end_frame / channel path).
 * Does not increment next_picture_index (caller does after successful submit).
 */
static inline int
nv_nvdec_session_emit_frame_sema(struct nv_push *p, struct nv_nvdec_session *s,
                                 uint64_t bitstream_gpu_addr,
                                 uint32_t bitstream_size,
                                 uint32_t sema_payload)
{
   struct nv_nvdec_pic_setup pic;
   uint32_t payload;

   if (!p || !s || !bitstream_gpu_addr || !bitstream_size)
      return -1;

   if (s->pic_setup_cpu_map && s->pic_setup_map_bytes >= 32)
      nv_nvdec_session_pack_pic_setup(s);

   nv_nvdec_session_fill_pic_setup_methods(s, bitstream_gpu_addr,
                                           bitstream_size, &pic);
   payload = sema_payload ? sema_payload : (s->next_picture_index + 1u);

   if (!s->object_set && s->class_nvdec) {
      nv_nvdec_set_object(p, s->class_nvdec);
      s->object_set = true;
   } else if (!s->object_set) {
      nv_push_set_subch(p, NV_PUSH_SUBCH_NVDEC);
   }

   nv_nvdec_emit_pic_setup(p, &pic);
   if (s->status_gpu_addr)
      nv_nvdec_emit_semaphore_release_reset(p, s->status_gpu_addr, payload,
                                            s->status_cpu_map);
   nv_push_method(p, NV_NVDEC_EXECUTE, pic.execute_flags);
   return 0;
}

/**
 * One-shot decode: init session, load PS from annexb if present in same buffer,
 * emit frame.  Useful for gallium/Vulkan video entrypoints.
 */
static inline int
nv_nvdec_decode_annexb_frame(struct nv_push *p, uint32_t class_nvdec,
                             uint32_t app_id,
                             const uint8_t *annexb_cpu, uint32_t annexb_size,
                             uint64_t bitstream_gpu_addr,
                             uint64_t pic_setup_gpu_addr, void *pic_setup_cpu,
                             uint32_t pic_setup_map_bytes,
                             uint64_t output_luma, uint64_t output_chroma,
                             uint32_t luma_pitch, uint32_t chroma_pitch)
{
   struct nv_nvdec_session s;
   if (!p || !bitstream_gpu_addr)
      return -1;
   nv_nvdec_session_init(&s, class_nvdec, app_id);
   nv_nvdec_session_set_pic_setup_bo(&s, pic_setup_gpu_addr, pic_setup_cpu,
                                     pic_setup_map_bytes);
   nv_nvdec_session_set_output(&s, output_luma, output_chroma,
                               luma_pitch, chroma_pitch);
   if (annexb_cpu && annexb_size)
      nv_nvdec_session_load_annexb_ps(&s, annexb_cpu, annexb_size);
   return nv_nvdec_session_emit_frame(p, &s, bitstream_gpu_addr, annexb_size);
}

static inline int
nv_nvdec_fill_frame_from_hevc(struct nv_nvdec_frame_setup *fs,
                              const struct nv_nvdec_hevc_pic_setup *ps,
                              uint64_t pic_setup_gpu_addr,
                              void *pic_setup_cpu_map,
                              uint32_t pic_setup_map_bytes,
                              uint64_t bitstream_gpu_addr,
                              uint32_t bitstream_size,
                              uint32_t picture_index)
{
   if (!fs || !ps)
      return -1;
   memset(fs, 0, sizeof(*fs));
   fs->app_id = NV_NVDEC_APP_ID_HEVC;
   fs->picture_index = picture_index;
   fs->pic_setup_gpu_addr = pic_setup_gpu_addr;
   fs->bitstream_gpu_addr = bitstream_gpu_addr;
   fs->bitstream_size = bitstream_size;
   fs->execute_flags = 1;
   if (pic_setup_cpu_map && pic_setup_map_bytes >= NV_NVDEC_HEVC_PIC_SETUP_DWORDS * 4)
      nv_nvdec_hevc_pic_setup_pack(ps, (uint32_t *)pic_setup_cpu_map,
                                   pic_setup_map_bytes / 4);
   return 0;
}

static inline int
nv_nvdec_fill_frame_from_av1(struct nv_nvdec_frame_setup *fs,
                             const struct nv_nvdec_av1_pic_setup *ps,
                             uint64_t pic_setup_gpu_addr,
                             void *pic_setup_cpu_map,
                             uint32_t pic_setup_map_bytes,
                             uint64_t bitstream_gpu_addr,
                             uint32_t bitstream_size,
                             uint32_t picture_index)
{
   if (!fs || !ps)
      return -1;
   memset(fs, 0, sizeof(*fs));
   fs->app_id = NV_NVDEC_APP_ID_AV1;
   fs->picture_index = picture_index;
   fs->pic_setup_gpu_addr = pic_setup_gpu_addr;
   fs->bitstream_gpu_addr = bitstream_gpu_addr;
   fs->bitstream_size = bitstream_size;
   fs->execute_flags = 1;
   if (pic_setup_cpu_map && pic_setup_map_bytes >= NV_NVDEC_AV1_PIC_SETUP_DWORDS * 4)
      nv_nvdec_av1_pic_setup_pack(ps, (uint32_t *)pic_setup_cpu_map,
                                  pic_setup_map_bytes / 4);
   return 0;
}

/**
 * tick114: NVDEC offset encoding helper — most *B0 classes use 256-byte units
 * (VA >> 8).  Some newer classes may want 4K (>>12); pass unit_shift=8 or 12.
 */
static inline uint32_t
nv_video_gpu_addr_to_offset_units(uint64_t gpu_addr, unsigned unit_shift)
{
   if (!unit_shift)
      unit_shift = 8;
   return (uint32_t)(gpu_addr >> unit_shift);
}

/** tick114: fill minimal H.264 pic_setup dwords for IDR/I smoke (no DPB). */
static inline void
nv_nvdec_pic_setup_fill_h264_intra_smoke(uint32_t *pic_dwords,
                                         uint32_t pic_dwords_cap,
                                         uint32_t mb_w, uint32_t mb_h,
                                         uint32_t frame_num,
                                         uint32_t curr_pic_idx)
{
   if (!pic_dwords || pic_dwords_cap < 8)
      return;
   memset(pic_dwords, 0, (size_t)pic_dwords_cap * sizeof(uint32_t));
   if (!mb_w)
      mb_w = 1;
   if (!mb_h)
      mb_h = 1;
   pic_dwords[NV_H264_PS_MB_WH] = (mb_h << 16) | (mb_w & 0xffffu);
   pic_dwords[NV_H264_PS_FRAME_NUM] = frame_num;
   pic_dwords[NV_H264_PS_SPS_FLAGS] =
      (100u /* High */) | (41u << 8) | (1u << 16); /* 4:2:0 */
   pic_dwords[NV_H264_PS_PPS_FLAGS] = 1u; /* CABAC */
   pic_dwords[NV_H264_PS_NUM_REFL0] = 0;
   pic_dwords[NV_H264_PS_NUM_REFL1] = 0;
   pic_dwords[NV_H264_PS_CURR_PIC_IDX] = curr_pic_idx;
}

/**
 * tick122: H.264 intra smoke with output/bitstream/coloc GPU offsets (>>8 units).
 * pic_dwords_cap should be >= NV_NVDEC_H264_PIC_SETUP_DWORDS (64) when possible.
 * output/bitstream/coloc/history addresses 0 = leave field zero.
 */
static inline void
nv_nvdec_pic_setup_fill_h264_intra_ex(uint32_t *pic_dwords,
                                      uint32_t pic_dwords_cap,
                                      uint32_t mb_w, uint32_t mb_h,
                                      uint32_t frame_num,
                                      uint32_t curr_pic_idx,
                                      uint64_t output_luma_gpu,
                                      uint64_t output_chroma_gpu,
                                      uint64_t bitstream_gpu,
                                      uint32_t bitstream_len_bytes,
                                      uint64_t coloc_gpu,
                                      uint64_t history_gpu)
{
   nv_nvdec_pic_setup_fill_h264_intra_smoke(pic_dwords, pic_dwords_cap, mb_w,
                                            mb_h, frame_num, curr_pic_idx);
   if (!pic_dwords || pic_dwords_cap < 21)
      return;
   if (output_luma_gpu)
      pic_dwords[NV_H264_PS_OUTPUT_LUMA_OFF] =
         nv_video_gpu_addr_to_offset_units(output_luma_gpu, 8);
   if (output_chroma_gpu)
      pic_dwords[NV_H264_PS_OUTPUT_CHROMA_OFF] =
         nv_video_gpu_addr_to_offset_units(output_chroma_gpu, 8);
   if (history_gpu)
      pic_dwords[NV_H264_PS_HISTOGRAM_OFF] =
         nv_video_gpu_addr_to_offset_units(history_gpu, 8);
   if (coloc_gpu)
      pic_dwords[NV_H264_PS_COLOC_OFF] =
         nv_video_gpu_addr_to_offset_units(coloc_gpu, 8);
   if (bitstream_len_bytes)
      pic_dwords[NV_H264_PS_BITSTREAM_LEN] = bitstream_len_bytes;
   (void)bitstream_gpu; /* bitstream base is SET_IN_BUF_BASE_OFFSET in methods */
}

/**
 * tick122: populate nv_nvdec_pic_setup method args from smoke buffers.
 * pic_setup_gpu / bitstream_gpu are absolute GPU VAs; control_params 0 = default.
 */
static inline void
nv_nvdec_pic_setup_init_h264_smoke(struct nv_nvdec_pic_setup *s,
                                   uint64_t pic_setup_gpu,
                                   uint64_t bitstream_gpu,
                                   uint32_t picture_index)
{
   if (!s)
      return;
   memset(s, 0, sizeof(*s));
   s->app_id = NV_NVDEC_APP_ID_H264;
   s->picture_index = picture_index;
   s->pic_setup_gpu = pic_setup_gpu;
   s->bitstream_gpu = bitstream_gpu;
   s->execute_flags = 1u;
   s->control_params = 0;
}

/** tick122: minimal NVENC pic_setup BO for H.264 intra / low-res smoke. */
static inline void
nv_nvenc_pic_setup_fill_h264_smoke(uint32_t *pic_dwords,
                                   uint32_t pic_dwords_cap,
                                   uint32_t width, uint32_t height,
                                   uint32_t fps_num, uint32_t fps_den,
                                   uint64_t input_luma_gpu,
                                   uint64_t bitstream_out_gpu)
{
   if (!pic_dwords || pic_dwords_cap < 13)
      return;
   if (!width)
      width = 64;
   if (!height)
      height = 64;
   if (!fps_num)
      fps_num = 30;
   if (!fps_den)
      fps_den = 1;
   memset(pic_dwords, 0, (size_t)pic_dwords_cap * sizeof(uint32_t));
   pic_dwords[NV_NVENC_PS_PIC_WH] = (height << 16) | (width & 0xffffu);
   pic_dwords[NV_NVENC_PS_FRAME_RATE] = (fps_den << 16) | (fps_num & 0xffffu);
   pic_dwords[NV_NVENC_PS_SPS_FLAGS] =
      (100u) | (41u << 8) | (1u << 16); /* High / level 4.1 / 4:2:0 */
   pic_dwords[NV_NVENC_PS_PPS_RC_FLAGS] = 1u; /* intra / default RC subset */
   pic_dwords[NV_NVENC_PS_GOP_LENGTH] = 1u;   /* intra-only */
   pic_dwords[NV_NVENC_PS_IDR_PERIOD] = 1u;
   if (input_luma_gpu)
      pic_dwords[NV_NVENC_PS_INPUT_LUMA_OFF] =
         nv_video_gpu_addr_to_offset_units(input_luma_gpu, 8);
   if (bitstream_out_gpu)
      pic_dwords[NV_NVENC_PS_BITSTREAM_OUT_OFF] =
         nv_video_gpu_addr_to_offset_units(bitstream_out_gpu, 8);
}

/** tick122: init nv_nvenc_frame_setup for smoke (optionally with pic_setup BO). */
static inline void
nv_nvenc_frame_setup_init_h264_smoke(struct nv_nvenc_frame_setup *fs,
                                     uint64_t pic_setup_gpu,
                                     uint64_t input_yuv_gpu,
                                     uint64_t bitstream_out_gpu,
                                     uint32_t width, uint32_t height)
{
   if (!fs)
      return;
   memset(fs, 0, sizeof(*fs));
   fs->app_id = NV_NVENC_APP_ID_H264;
   fs->execute_flags = 1u;
   fs->pic_setup_gpu_addr = pic_setup_gpu;
   fs->input_yuv_gpu_addr = input_yuv_gpu;
   fs->bitstream_out_gpu_addr = bitstream_out_gpu;
   fs->width = width ? width : 64;
   fs->height = height ? height : 64;
   fs->frame_rate_num = 30;
   fs->frame_rate_den = 1;
   fs->gop_length = 1;
   fs->idr_period = 1;
   fs->profile_idc = 100;
   fs->level_idc = 41;
   fs->chroma_format_idc = 1;
}

/**
 * tick124: Gallium/NVENC session-like emit — sema + frame_setup without requiring
 * full nv_nvenc_session object.  class_nvenc 0 uses subch only.
 */
static inline int
nv_nvenc_emit_encode_frame(struct nv_push *p, uint32_t class_nvenc,
                           const struct nv_nvenc_frame_setup *fs,
                           uint64_t sema_gpu, uint32_t sema_payload,
                           volatile uint32_t *status_cpu)
{
   if (!p || !fs)
      return -1;
   nv_nvenc_emit_frame_kick(p, class_nvenc, fs, sema_gpu, sema_payload,
                            status_cpu);
   return 0;
}

/** tick114: NVENC pic_setup-only methods (no sema; use existing frame_kick for full slice). */
static inline void
nv_nvenc_emit_pic_setup_simple(struct nv_push *p, uint32_t app_id,
                               uint32_t control_params, uint64_t pic_setup_gpu)
{
   if (!p)
      return;
   if (app_id)
      nv_push_method(p, NV_NVENC_SET_APPLICATION_ID, app_id);
   if (control_params)
      nv_push_method(p, NV_NVENC_SET_CONTROL_PARAMS, control_params);
   if (pic_setup_gpu) {
      nv_push_method(p, NV_NVENC_SET_DRV_PIC_SETUP_OFFSET,
                     nv_video_gpu_addr_to_offset_units(pic_setup_gpu, 8));
   }
}

#ifdef __cplusplus
}
#endif

#endif /* NV_VIDEO_METHODS_H */
