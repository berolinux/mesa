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
