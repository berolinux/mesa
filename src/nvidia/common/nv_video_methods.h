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
#include <string.h>

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
   /* Optional output surfaces (display/decoded YUV planes, 256B-aligned offsets) */
   uint64_t output_luma_gpu_addr;
   uint64_t output_chroma_gpu_addr;
   uint32_t output_luma_pitch;
   uint32_t output_chroma_pitch;
   uint32_t mb_width;            /* macroblock / CTB width hint for pic_setup */
   uint32_t mb_height;
   uint32_t bit_depth_luma_minus8;
   uint32_t bit_depth_chroma_minus8;
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
#define NV_H264_PS_OUTPUT_LUMA_OFF       16  /* >>8 offset words in some gens */
#define NV_H264_PS_OUTPUT_CHROMA_OFF     17
#define NV_H264_PS_HISTOGRAM_OFF         18
#define NV_H264_PS_COLOC_OFF             19
#define NV_H264_PS_BITSTREAM_LEN         20

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
      if (pic_dwords_cap > NV_HEVC_PS_CURR_IDX)
         pic_dwords[NV_HEVC_PS_CURR_IDX] = fs->picture_index;
      if (fs->output_luma_gpu_addr && pic_dwords_cap > NV_HEVC_PS_OUTPUT_LUMA_OFF)
         pic_dwords[NV_HEVC_PS_OUTPUT_LUMA_OFF] =
            (uint32_t)(fs->output_luma_gpu_addr >> 8);
      if (fs->output_chroma_gpu_addr && pic_dwords_cap > NV_HEVC_PS_OUTPUT_CHROMA_OFF)
         pic_dwords[NV_HEVC_PS_OUTPUT_CHROMA_OFF] =
            (uint32_t)(fs->output_chroma_gpu_addr >> 8);
      break;
   case NV_NVDEC_APP_ID_AV1:
      if (pic_dwords_cap > NV_AV1_PS_FRAME_WH)
         pic_dwords[NV_AV1_PS_FRAME_WH] = (mb_h << 16) | (mb_w & 0xffffu);
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
      if (pic_dwords_cap > NV_H264_PS_CURR_PIC_IDX)
         pic_dwords[NV_H264_PS_CURR_PIC_IDX] = fs->picture_index;
      if (pic_dwords_cap > NV_H264_PS_BITSTREAM_LEN)
         pic_dwords[NV_H264_PS_BITSTREAM_LEN] = fs->bitstream_size;
      if (fs->output_luma_gpu_addr && pic_dwords_cap > NV_H264_PS_OUTPUT_LUMA_OFF)
         pic_dwords[NV_H264_PS_OUTPUT_LUMA_OFF] =
            (uint32_t)(fs->output_luma_gpu_addr >> 8);
      if (fs->output_chroma_gpu_addr && pic_dwords_cap > NV_H264_PS_OUTPUT_CHROMA_OFF)
         pic_dwords[NV_H264_PS_OUTPUT_CHROMA_OFF] =
            (uint32_t)(fs->output_chroma_gpu_addr >> 8);
      if (fs->coloc_gpu_addr && pic_dwords_cap > NV_H264_PS_COLOC_OFF)
         pic_dwords[NV_H264_PS_COLOC_OFF] =
            (uint32_t)(fs->coloc_gpu_addr >> 8);
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
   uint32_t dpb_luma_pitch;
   uint32_t dpb_chroma_pitch;
   uint64_t dpb_luma[8];
   uint64_t dpb_chroma[8];
   uint64_t film_grain_params_addr; /* optional sideband BO */
   uint32_t reserved[24];
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

/** Set HEVC DPB reference slot luma/chroma VA. */
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
         unsigned i, n = (chroma_format_idc != 3) ? 8u : 12u;
         for (i = 0; i < n; i++) {
            if (nv_rbsp_u(&r, 1)) {
               unsigned j, last = 8, next = 8, size = (i < 6) ? 16u : 64u;
               for (j = 0; j < size; j++) {
                  if (next)
                     next = (uint32_t)((int32_t)last + nv_rbsp_se(&r)) & 0xffu;
                  last = next ? next : last;
               }
            }
         }
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
      if (nv_rbsp_u(&r, 1)) { /* pic_scaling_matrix_present */
         unsigned i;
         for (i = 0; i < 8; i++)
            if (nv_rbsp_u(&r, 1))
               ; /* skip scaling list content — approximate */
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

/**
 * HEVC SPS subset: pic size, chroma, bit depths, coding/transform block sizes.
 * Skips scaling lists / ST RPS / VUI. Returns 0 on success.
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
   /* Skip short_term_ref_pic_set structures (complex; host may supply via pipe_desc) */
   if (nv_rbsp_u(&r, 1)) { /* long_term_ref_pics_present_flag */
      num_long_term_ref_pics_sps = nv_rbsp_ue(&r);
      {
         unsigned i;
         for (i = 0; i < num_long_term_ref_pics_sps && i < 32; i++) {
            (void)nv_rbsp_u(&r, log2_max_pic_order_cnt_lsb_minus4 + 4);
            (void)nv_rbsp_u(&r, 1); /* used_by_curr_pic_lt_sps_flag */
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
   uint64_t output_luma_gpu_addr;
   uint64_t output_chroma_gpu_addr;
   uint32_t output_luma_pitch;
   uint32_t output_chroma_pitch;
   struct nv_nvdec_h264_pic_setup h264_ps;
   struct nv_nvdec_hevc_pic_setup hevc_ps;
   bool h264_ps_valid;
   bool hevc_ps_valid;
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

/**
 * Pack session pic_setup structs into the host-mapped pic_setup BO (if any).
 * Call after SPS/PPS load and DPB updates, before emit_frame.
 */
static inline void
nv_nvdec_session_pack_pic_setup(struct nv_nvdec_session *s)
{
   uint32_t dwords;
   if (!s || !s->pic_setup_cpu_map || !s->pic_setup_map_bytes)
      return;
   dwords = s->pic_setup_map_bytes / 4;
   if (s->app_id == NV_NVDEC_APP_ID_HEVC && s->hevc_ps_valid) {
      s->hevc_ps.curr_pic_idx = s->next_picture_index;
      nv_nvdec_hevc_pic_setup_pack(&s->hevc_ps,
                                   (uint32_t *)s->pic_setup_cpu_map, dwords);
   } else if (s->h264_ps_valid) {
      s->h264_ps.curr_pic_idx = s->next_picture_index;
      nv_nvdec_h264_pic_setup_pack(&s->h264_ps,
                                   (uint32_t *)s->pic_setup_cpu_map, dwords);
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


#ifdef __cplusplus
}
#endif

#endif /* NV_VIDEO_METHODS_H */
