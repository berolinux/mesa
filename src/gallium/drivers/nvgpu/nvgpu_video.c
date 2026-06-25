/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * NVDEC bitstream video decode via shared nv_nvdec_session helpers.
 * Uses the graphics GPFIFO channel (NVDEC subchannel) on the same RM
 * device as 3D/compute — matches proprietary driver multi-engine channel
 * model at a high level (engine object programmed via SET_OBJECT).
 */

#include "nvgpu_video.h"
#include "nvgpu_context.h"
#include "nvgpu_screen.h"
#include "nvgpu_resource.h"

#include "nv_rm.h"
#include "nv_push.h"
#include "nv_channel.h"
#include "nv_video_methods.h"
#include "nv_device_info.h"

#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_video.h"
#include "vl/vl_video_buffer.h"
#include "pipe/p_defines.h"
#include "pipe/p_video_state.h"

#include <stdio.h>
#include <string.h>

#define NVGPU_VID_BITSTREAM_BO_SIZE   (4u * 1024u * 1024u)
#define NVGPU_VID_PIC_SETUP_BO_SIZE   4096u
#define NVGPU_VID_STATUS_BO_SIZE      256u

struct nvgpu_video_codec {
   struct pipe_video_codec base;
   struct nvgpu_context *ctx;
   struct nv_nvdec_session session;
   struct nv_rm_bo *bitstream_bo;
   void *bitstream_map;
   uint32_t bitstream_bo_size;
   struct nv_rm_bo *pic_setup_bo;
   void *pic_setup_map;
   struct nv_rm_bo *status_bo;
   void *status_map;
   uint32_t class_nvdec;
   uint32_t app_id;
   uint32_t last_frame_sema_payload;
   uint8_t *frame_bs;
   uint32_t frame_bs_used;
   uint32_t frame_bs_cap;
   struct pipe_video_buffer *cur_target;
   struct pipe_picture_desc *cur_picture;
   bool frame_active;
};

static uint32_t
nvgpu_profile_to_app_id(enum pipe_video_profile profile)
{
   switch (u_reduce_video_profile(profile)) {
   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      return NV_NVDEC_APP_ID_H264;
   case PIPE_VIDEO_FORMAT_HEVC:
      return NV_NVDEC_APP_ID_HEVC;
   case PIPE_VIDEO_FORMAT_AV1:
      return NV_NVDEC_APP_ID_AV1;
   case PIPE_VIDEO_FORMAT_VP9:
      return NV_NVDEC_APP_ID_VP9;
   case PIPE_VIDEO_FORMAT_MPEG12:
      return NV_NVDEC_APP_ID_MPEG12;
   default:
      return 0;
   }
}

static bool
nvgpu_video_ensure_bos(struct nvgpu_video_codec *dec)
{
   struct nv_rm_bo_req req;
   struct nvgpu_screen *screen;

   if (!dec || !dec->ctx || !dec->ctx->screen || !dec->ctx->screen->rm)
      return false;
   screen = dec->ctx->screen;

   if (!dec->bitstream_bo) {
      memset(&req, 0, sizeof(req));
      req.size = NVGPU_VID_BITSTREAM_BO_SIZE;
      req.alignment = 256;
      req.vram = false;
      req.cpu_access = true;
      req.no_scanout = true;
      req.map_gpu_va = true;
      dec->bitstream_bo = nv_rm_bo_alloc(screen->rm, &req);
      if (!dec->bitstream_bo)
         return false;
      dec->bitstream_bo_size = NVGPU_VID_BITSTREAM_BO_SIZE;
      dec->bitstream_map = nv_rm_bo_map(dec->bitstream_bo);
   }
   if (!dec->pic_setup_bo) {
      memset(&req, 0, sizeof(req));
      req.size = NVGPU_VID_PIC_SETUP_BO_SIZE;
      req.alignment = 256;
      req.vram = false;
      req.cpu_access = true;
      req.no_scanout = true;
      req.map_gpu_va = true;
      dec->pic_setup_bo = nv_rm_bo_alloc(screen->rm, &req);
      if (!dec->pic_setup_bo)
         return false;
      dec->pic_setup_map = nv_rm_bo_map(dec->pic_setup_bo);
      if (dec->pic_setup_map)
         memset(dec->pic_setup_map, 0, NVGPU_VID_PIC_SETUP_BO_SIZE);
   }
   if (!dec->status_bo) {
      memset(&req, 0, sizeof(req));
      req.size = NVGPU_VID_STATUS_BO_SIZE;
      req.alignment = 256;
      req.vram = false;
      req.cpu_access = true;
      req.no_scanout = true;
      req.map_gpu_va = true;
      dec->status_bo = nv_rm_bo_alloc(screen->rm, &req);
      if (dec->status_bo) {
         dec->status_map = nv_rm_bo_map(dec->status_bo);
         if (dec->status_map)
            memset(dec->status_map, 0, NVGPU_VID_STATUS_BO_SIZE);
      }
   } else if (dec->status_bo && !dec->status_map) {
      dec->status_map = nv_rm_bo_map(dec->status_bo);
   }
   return dec->bitstream_bo && dec->pic_setup_bo && dec->status_bo &&
          dec->bitstream_map;
}

static void
nvgpu_video_destroy(struct pipe_video_codec *codec)
{
   struct nvgpu_video_codec *dec = (struct nvgpu_video_codec *)codec;
   if (!dec)
      return;
   if (dec->frame_bs)
      FREE(dec->frame_bs);
   if (dec->bitstream_bo)
      nv_rm_bo_free(dec->bitstream_bo);
   if (dec->pic_setup_bo)
      nv_rm_bo_free(dec->pic_setup_bo);
   if (dec->status_bo)
      nv_rm_bo_free(dec->status_bo);
   FREE(dec);
}

static void
nvgpu_video_begin_frame(struct pipe_video_codec *codec,
                        struct pipe_video_buffer *target,
                        struct pipe_picture_desc *picture)
{
   struct nvgpu_video_codec *dec = (struct nvgpu_video_codec *)codec;
   if (!dec)
      return;
   dec->cur_target = target;
   dec->cur_picture = picture;
   dec->frame_bs_used = 0;
   dec->frame_active = true;
}

static bool
nvgpu_video_append_bs(struct nvgpu_video_codec *dec,
                      const void *data, unsigned size)
{
   uint32_t need;
   if (!dec || !data || !size)
      return true;
   need = dec->frame_bs_used + size;
   if (need > dec->frame_bs_cap) {
      uint32_t ncap = dec->frame_bs_cap ? dec->frame_bs_cap * 2 : 65536u;
      uint8_t *nbuf;
      while (ncap < need)
         ncap *= 2;
      if (ncap > NVGPU_VID_BITSTREAM_BO_SIZE)
         ncap = NVGPU_VID_BITSTREAM_BO_SIZE;
      if (need > ncap)
         return false;
      nbuf = REALLOC(dec->frame_bs, dec->frame_bs_cap, ncap);
      if (!nbuf)
         return false;
      dec->frame_bs = nbuf;
      dec->frame_bs_cap = ncap;
   }
   memcpy(dec->frame_bs + dec->frame_bs_used, data, size);
   dec->frame_bs_used += size;
   return true;
}

static void
nvgpu_video_decode_bitstream(struct pipe_video_codec *codec,
                             struct pipe_video_buffer *target,
                             struct pipe_picture_desc *picture,
                             unsigned num_buffers,
                             const void *const *buffers,
                             const unsigned *sizes)
{
   struct nvgpu_video_codec *dec = (struct nvgpu_video_codec *)codec;
   unsigned i;
   (void)target;
   (void)picture;
   if (!dec || !dec->frame_active)
      return;
   for (i = 0; i < num_buffers; i++) {
      if (!buffers[i] || !sizes[i])
         continue;
      if (!nvgpu_video_append_bs(dec, buffers[i], sizes[i]))
         break;
   }
}


/* Extract luma/chroma GPU VA + pitch from a pipe_video_buffer target. */
static void
nvgpu_video_buffer_planes(struct pipe_video_buffer *vb,
                          uint64_t *luma_va, uint64_t *chroma_va,
                          uint32_t *luma_pitch, uint32_t *chroma_pitch)
{
   struct pipe_sampler_view **views;
   struct pipe_resource *luma_res = NULL, *chroma_res = NULL;
   struct nvgpu_resource *luma_nr, *chroma_nr;

   if (luma_va) *luma_va = 0;
   if (chroma_va) *chroma_va = 0;
   if (luma_pitch) *luma_pitch = 0;
   if (chroma_pitch) *chroma_pitch = 0;
   if (!vb)
      return;

   if (vb->get_sampler_view_planes) {
      views = vb->get_sampler_view_planes(vb);
      if (views && views[0] && views[0]->texture)
         luma_res = views[0]->texture;
      if (views && views[1] && views[1]->texture)
         chroma_res = views[1]->texture;
   }
   if (luma_res) {
      luma_nr = nvgpu_resource(luma_res);
      if (luma_nr) {
         if (luma_va) *luma_va = luma_nr->gpu_offset;
         if (luma_pitch)
            *luma_pitch = luma_nr->row_pitch ? luma_nr->row_pitch
                                            : luma_res->width0;
      }
   }
   if (chroma_res) {
      chroma_nr = nvgpu_resource(chroma_res);
      if (chroma_nr) {
         if (chroma_va) *chroma_va = chroma_nr->gpu_offset;
         if (chroma_pitch)
            *chroma_pitch = chroma_nr->row_pitch ? chroma_nr->row_pitch
                                                : chroma_res->width0;
      }
   } else if (luma_res && luma_va && *luma_va && luma_pitch && *luma_pitch) {
      if (chroma_va)
         *chroma_va = *luma_va + (uint64_t)(*luma_pitch) * luma_res->height0;
      if (chroma_pitch)
         *chroma_pitch = *luma_pitch;
   }
}

/**
 * Apply pipe_h264_picture_desc SPS/PPS + DPB ref[] into session pic_setup.
 * Returns true if picture_desc contributed fields (overrides annex-B parse).
 */
static bool
nvgpu_video_apply_h264_picture_desc(struct nvgpu_video_codec *dec,
                                    struct pipe_picture_desc *picture)
{
   const struct pipe_h264_picture_desc *h264;
   const struct pipe_h264_pps *pps;
   const struct pipe_h264_sps *sps;
   unsigned i;

   if (!dec || !picture)
      return false;
   if (u_reduce_video_profile(dec->base.profile) != PIPE_VIDEO_FORMAT_MPEG4_AVC)
      return false;

   h264 = (const struct pipe_h264_picture_desc *)picture;
   pps = h264->pps;
   sps = pps ? pps->sps : NULL;
   if (!sps)
      return false;

   nv_nvdec_h264_apply_sps_pps(&dec->session.h264_ps,
      sps->pic_width_in_mbs_minus1 + 1,
      sps->pic_height_in_mbs_minus1 + 1,
      sps->frame_mbs_only_flag,
      sps->mb_adaptive_frame_field_flag,
      sps->direct_8x8_inference_flag,
      sps->chroma_format_idc,
      sps->log2_max_frame_num_minus4,
      sps->pic_order_cnt_type,
      sps->log2_max_pic_order_cnt_lsb_minus4,
      sps->delta_pic_order_always_zero_flag,
      pps->entropy_coding_mode_flag,
      pps->bottom_field_pic_order_in_frame_present_flag,
      pps->num_ref_idx_l0_default_active_minus1,
      pps->num_ref_idx_l1_default_active_minus1,
      pps->weighted_pred_flag,
      pps->weighted_bipred_idc,
      (uint32_t)(int32_t)pps->pic_init_qp_minus26,
      (uint32_t)(int32_t)pps->chroma_qp_index_offset,
      (uint32_t)(int32_t)pps->second_chroma_qp_index_offset,
      pps->deblocking_filter_control_present_flag,
      pps->constrained_intra_pred_flag,
      pps->redundant_pic_cnt_present_flag,
      pps->transform_8x8_mode_flag);

   dec->session.h264_ps.frame_num = h264->frame_num;
   dec->session.h264_ps.field_pic_flag = h264->field_pic_flag;
   dec->session.h264_ps.bottom_field_flag = h264->bottom_field_flag;
   dec->session.h264_ps.ref_pic_flag = h264->is_reference ? 1 : 0;
   if (h264->num_ref_idx_l0_active_minus1 || h264->num_ref_idx_l1_active_minus1) {
      dec->session.h264_ps.num_ref_idx_l0_active_minus1 =
         h264->num_ref_idx_l0_active_minus1;
      dec->session.h264_ps.num_ref_idx_l1_active_minus1 =
         h264->num_ref_idx_l1_active_minus1;
      dec->session.h264_ps.l0_ref_count = h264->num_ref_idx_l0_active_minus1 + 1;
      dec->session.h264_ps.l1_ref_count = h264->num_ref_idx_l1_active_minus1 + 1;
   }

   for (i = 0; i < 16; i++) {
      uint64_t luma = 0, chroma = 0;
      uint32_t lp = 0, cp = 0;
      if (h264->ref[i]) {
         nvgpu_video_buffer_planes(h264->ref[i], &luma, &chroma, &lp, &cp);
         if (lp)
            dec->session.h264_ps.dpb_luma_pitch = lp;
         if (cp)
            dec->session.h264_ps.dpb_chroma_pitch = cp;
      }
      nv_nvdec_session_set_h264_dpb(&dec->session, i,
         luma,
         h264->bottom_is_reference[i] ? luma : 0,
         chroma,
         h264->bottom_is_reference[i] ? chroma : 0);
   }
   dec->session.h264_ps_valid = true;
   return true;
}

static bool
nvgpu_video_apply_h265_picture_desc(struct nvgpu_video_codec *dec,
                                    struct pipe_picture_desc *picture)
{
   const struct pipe_h265_picture_desc *h265;
   const struct pipe_h265_pps *pps;
   const struct pipe_h265_sps *sps;
   unsigned i;

   if (!dec || !picture)
      return false;
   if (u_reduce_video_profile(dec->base.profile) != PIPE_VIDEO_FORMAT_HEVC)
      return false;

   h265 = (const struct pipe_h265_picture_desc *)picture;
   pps = h265->pps;
   sps = pps ? pps->sps : NULL;
   if (!sps)
      return false;

   nv_nvdec_hevc_apply_sps_pps(&dec->session.hevc_ps,
      sps->pic_width_in_luma_samples,
      sps->pic_height_in_luma_samples,
      sps->chroma_format_idc,
      sps->bit_depth_luma_minus8,
      sps->bit_depth_chroma_minus8,
      sps->log2_min_luma_coding_block_size_minus3,
      sps->log2_diff_max_min_luma_coding_block_size,
      sps->log2_min_transform_block_size_minus2,
      sps->log2_diff_max_min_transform_block_size,
      sps->max_transform_hierarchy_depth_intra,
      sps->max_transform_hierarchy_depth_inter,
      sps->amp_enabled_flag,
      sps->sample_adaptive_offset_enabled_flag,
      sps->pcm_enabled_flag,
      sps->pcm_loop_filter_disabled_flag,
      sps->strong_intra_smoothing_enabled_flag,
      sps->sps_temporal_mvp_enabled_flag,
      sps->log2_max_pic_order_cnt_lsb_minus4,
      sps->num_short_term_ref_pic_sets,
      sps->num_long_term_ref_pics_sps,
      pps->num_ref_idx_l0_default_active_minus1,
      pps->num_ref_idx_l1_default_active_minus1,
      (uint32_t)(int32_t)pps->init_qp_minus26,
      pps->dependent_slice_segments_enabled_flag,
      pps->sign_data_hiding_enabled_flag);

   for (i = 0; i < 16; i++) {
      uint64_t luma = 0, chroma = 0;
      uint32_t lp = 0, cp = 0;
      if (h265->ref[i]) {
         nvgpu_video_buffer_planes(h265->ref[i], &luma, &chroma, &lp, &cp);
         if (lp)
            dec->session.hevc_ps.dpb_luma_pitch = lp;
         if (cp)
            dec->session.hevc_ps.dpb_chroma_pitch = cp;
      }
      nv_nvdec_session_set_hevc_dpb(&dec->session, i, luma, chroma);
   }
   dec->session.hevc_ps_valid = true;
   return true;
}

/**
 * Apply pipe_av1_picture_desc sequence/frame + ref[16] DPB into session av1_ps.
 * Maps first 8 refs to NVDEC 8-frame DPB; film_grain_target is recorded via
 * output override when present.
 */
static bool
nvgpu_video_apply_av1_picture_desc(struct nvgpu_video_codec *dec,
                                   struct pipe_picture_desc *picture)
{
   const struct pipe_av1_picture_desc *av1;
   unsigned i;

   if (!dec || !picture)
      return false;
   if (u_reduce_video_profile(dec->base.profile) != PIPE_VIDEO_FORMAT_AV1)
      return false;

   av1 = (const struct pipe_av1_picture_desc *)picture;

   nv_nvdec_av1_apply_seq_frame(&dec->session.av1_ps,
      av1->picture_parameter.frame_width
         ? av1->picture_parameter.frame_width
         : av1->picture_parameter.max_width,
      av1->picture_parameter.frame_height
         ? av1->picture_parameter.frame_height
         : av1->picture_parameter.max_height,
      av1->picture_parameter.profile,
      av1->picture_parameter.bit_depth_idx,
      av1->picture_parameter.seq_info_fields.use_128x128_superblock,
      av1->picture_parameter.seq_info_fields.enable_cdef,
      av1->picture_parameter.seq_info_fields.mono_chrome,
      av1->picture_parameter.seq_info_fields.subsampling_x,
      av1->picture_parameter.seq_info_fields.subsampling_y,
      av1->picture_parameter.seq_info_fields.enable_order_hint,
      av1->picture_parameter.order_hint_bits_minus_1,
      av1->picture_parameter.seq_info_fields.enable_jnt_comp,
      av1->picture_parameter.seq_info_fields.ref_frame_mvs,
      av1->picture_parameter.seq_info_fields.film_grain_params_present,
      av1->picture_parameter.pic_info_fields.frame_type,
      av1->picture_parameter.pic_info_fields.show_frame,
      av1->picture_parameter.pic_info_fields.error_resilient_mode,
      av1->picture_parameter.pic_info_fields.disable_cdf_update,
      av1->picture_parameter.pic_info_fields.allow_screen_content_tools,
      av1->picture_parameter.pic_info_fields.force_integer_mv,
      av1->picture_parameter.pic_info_fields.use_superres,
      av1->picture_parameter.superres_scale_denominator
         ? av1->picture_parameter.superres_scale_denominator : 8,
      av1->picture_parameter.pic_info_fields.allow_high_precision_mv,
      av1->picture_parameter.pic_info_fields.allow_warped_motion,
      av1->picture_parameter.primary_ref_frame,
      av1->picture_parameter.order_hint,
      av1->picture_parameter.base_qindex,
      av1->picture_parameter.tile_cols,
      av1->picture_parameter.tile_rows,
      av1->picture_parameter.film_grain_info.film_grain_info_fields.apply_grain);

   nv_nvdec_av1_set_ref_frame_idx(&dec->session.av1_ps,
                                  av1->picture_parameter.ref_frame_idx);

   for (i = 0; i < 8; i++) {
      uint64_t luma = 0, chroma = 0;
      uint32_t lp = 0, cp = 0;
      /* pipe has 16 ref slots; NVDEC AV1 DPB is 8 — use first 8 non-null */
      if (av1->ref[i]) {
         nvgpu_video_buffer_planes(av1->ref[i], &luma, &chroma, &lp, &cp);
         if (lp)
            dec->session.av1_ps.dpb_luma_pitch = lp;
         if (cp)
            dec->session.av1_ps.dpb_chroma_pitch = cp;
      }
      nv_nvdec_session_set_av1_dpb(&dec->session, i, luma, chroma);
   }

   /* Optional film-grain output target overrides normal output when set */
   if (av1->film_grain_target) {
      uint64_t luma = 0, chroma = 0;
      uint32_t lp = 0, cp = 0;
      nvgpu_video_buffer_planes(av1->film_grain_target, &luma, &chroma,
                                &lp, &cp);
      if (luma)
         nv_nvdec_session_set_output(&dec->session, luma, chroma,
                                     lp ? lp : dec->session.output_luma_pitch,
                                     cp ? cp : dec->session.output_chroma_pitch);
   }

   dec->session.av1_ps_valid = true;
   return true;
}

/**
 * Apply pipe_vp9_picture_desc frame params + ref[8] DPB into session vp9_ps.
 */
static bool
nvgpu_video_apply_vp9_picture_desc(struct nvgpu_video_codec *dec,
                                   struct pipe_picture_desc *picture)
{
   const struct pipe_vp9_picture_desc *vp9;
   unsigned i;

   if (!dec || !picture)
      return false;
   if (u_reduce_video_profile(dec->base.profile) != PIPE_VIDEO_FORMAT_VP9)
      return false;

   vp9 = (const struct pipe_vp9_picture_desc *)picture;

   nv_nvdec_vp9_apply_frame(&dec->session.vp9_ps,
      vp9->picture_parameter.frame_width,
      vp9->picture_parameter.frame_height,
      vp9->picture_parameter.prev_frame_width,
      vp9->picture_parameter.prev_frame_height,
      vp9->picture_parameter.profile,
      vp9->picture_parameter.bit_depth
         ? vp9->picture_parameter.bit_depth : 8,
      vp9->picture_parameter.pic_fields.subsampling_x,
      vp9->picture_parameter.pic_fields.subsampling_y,
      vp9->picture_parameter.pic_fields.frame_type,
      vp9->picture_parameter.pic_fields.show_frame,
      vp9->picture_parameter.pic_fields.error_resilient_mode,
      vp9->picture_parameter.pic_fields.intra_only,
      vp9->picture_parameter.pic_fields.allow_high_precision_mv,
      vp9->picture_parameter.pic_fields.mcomp_filter_type,
      vp9->picture_parameter.pic_fields.refresh_frame_context,
      vp9->picture_parameter.pic_fields.frame_context_idx,
      vp9->picture_parameter.pic_fields.reset_frame_context,
      vp9->picture_parameter.pic_fields.segmentation_enabled,
      vp9->picture_parameter.pic_fields.segmentation_update_map,
      vp9->picture_parameter.pic_fields.segmentation_temporal_update,
      vp9->picture_parameter.pic_fields.segmentation_update_data,
      vp9->picture_parameter.pic_fields.last_ref_frame,
      vp9->picture_parameter.pic_fields.golden_ref_frame,
      vp9->picture_parameter.pic_fields.alt_ref_frame,
      vp9->picture_parameter.pic_fields.last_ref_frame_sign_bias,
      vp9->picture_parameter.pic_fields.golden_ref_frame_sign_bias,
      vp9->picture_parameter.pic_fields.alt_ref_frame_sign_bias,
      vp9->picture_parameter.pic_fields.lossless_flag,
      vp9->picture_parameter.pic_fields.use_prev_frame_mvs,
      vp9->picture_parameter.filter_level,
      vp9->picture_parameter.sharpness_level,
      vp9->picture_parameter.log2_tile_rows,
      vp9->picture_parameter.log2_tile_columns,
      vp9->picture_parameter.base_qindex,
      vp9->picture_parameter.y_dc_delta_q,
      vp9->picture_parameter.uv_dc_delta_q,
      vp9->picture_parameter.uv_ac_delta_q,
      vp9->picture_parameter.first_partition_size,
      vp9->picture_parameter.frame_header_length_in_bytes);

   for (i = 0; i < 8; i++) {
      uint64_t luma = 0, chroma = 0;
      uint32_t lp = 0, cp = 0;
      if (vp9->ref[i]) {
         nvgpu_video_buffer_planes(vp9->ref[i], &luma, &chroma, &lp, &cp);
         if (lp)
            dec->session.vp9_ps.dpb_luma_pitch = lp;
         if (cp)
            dec->session.vp9_ps.dpb_chroma_pitch = cp;
      }
      nv_nvdec_session_set_vp9_dpb(&dec->session, i, luma, chroma);
   }
   dec->session.vp9_ps_valid = true;
   return true;
}

static void
nvgpu_video_apply_picture_desc(struct nvgpu_video_codec *dec,
                               struct pipe_picture_desc *picture)
{
   if (!dec || !picture)
      return;
   if (nvgpu_video_apply_h264_picture_desc(dec, picture))
      return;
   if (nvgpu_video_apply_h265_picture_desc(dec, picture))
      return;
   if (nvgpu_video_apply_av1_picture_desc(dec, picture))
      return;
   (void)nvgpu_video_apply_vp9_picture_desc(dec, picture);
}

static void
nvgpu_video_set_output_from_target(struct nvgpu_video_codec *dec,
                                   struct pipe_video_buffer *target)
{
   uint64_t luma_va = 0, chroma_va = 0;
   uint32_t luma_pitch = 0, chroma_pitch = 0;

   if (!dec || !target)
      return;
   nvgpu_video_buffer_planes(target, &luma_va, &chroma_va,
                             &luma_pitch, &chroma_pitch);
   nv_nvdec_session_set_output(&dec->session, luma_va, chroma_va,
                               luma_pitch, chroma_pitch ? chroma_pitch
                                                        : luma_pitch);
}

static int
nvgpu_video_end_frame(struct pipe_video_codec *codec,
                      struct pipe_video_buffer *target,
                      struct pipe_picture_desc *picture)
{
   struct nvgpu_video_codec *dec = (struct nvgpu_video_codec *)codec;
   struct nvgpu_context *ctx;
   struct nv_push push;
   uint64_t bs_gpu, pic_gpu, st_gpu;
   uint32_t bs_size;
   int r = -1;

   if (!dec || !dec->frame_active)
      return 0;
   ctx = dec->ctx;
   if (!ctx || !nvgpu_video_ensure_bos(dec))
      goto out_clear;

   if (!dec->frame_bs_used) {
      r = 0;
      goto out_clear;
   }

   bs_size = dec->frame_bs_used;
   if (bs_size > dec->bitstream_bo_size)
      bs_size = dec->bitstream_bo_size;
   memcpy(dec->bitstream_map, dec->frame_bs, bs_size);

   /* Annex-B SPS/PPS first; pipe_picture_desc SPS/PPS/DPB overrides when present */
   (void)nv_nvdec_session_load_annexb_ps(&dec->session, dec->frame_bs,
                                         dec->frame_bs_used);
   nvgpu_video_apply_picture_desc(dec, picture ? picture : dec->cur_picture);
   nvgpu_video_set_output_from_target(dec, target ? target : dec->cur_target);

   pic_gpu = nv_rm_bo_gpu_offset(dec->pic_setup_bo);
   st_gpu = nv_rm_bo_gpu_offset(dec->status_bo);
   bs_gpu = nv_rm_bo_gpu_offset(dec->bitstream_bo);

   nv_nvdec_session_set_pic_setup_bo(&dec->session, pic_gpu,
                                     dec->pic_setup_map,
                                     NVGPU_VID_PIC_SETUP_BO_SIZE);
   nv_nvdec_session_set_status_bo(&dec->session, st_gpu);
   if (dec->status_map)
      nv_nvdec_session_set_status_cpu_map(
         &dec->session, (volatile uint32_t *)dec->status_map);
   nv_nvdec_session_pack_pic_setup(&dec->session);

   /* Pre-arm sema payload (picture_index+1 written by NVDEC sema release) */
   dec->last_frame_sema_payload = (dec->session.next_picture_index + 1);
   if (dec->status_map)
      *(volatile uint32_t *)dec->status_map = 0;

   /*
    * tick123: prefer channel sema submit (class ladder + wait) when channel
    * exists; fall back to push-only session_emit_frame_sema / emit_frame.
    */
   if (ctx->channel) {
      struct nv_nvdec_pic_setup vpic;
      nv_nvdec_session_fill_pic_setup_methods(&dec->session, bs_gpu, bs_size,
                                              &vpic);
      r = nv_channel_nvdec_frame_sema_submit(
         ctx->channel, dec->class_nvdec, &vpic, st_gpu,
         dec->status_map ? (volatile uint32_t *)dec->status_map : NULL,
         dec->last_frame_sema_payload, true, 2000000000ull, true);
      if (r == 0 || r == -EAGAIN)
         r = (r == -EAGAIN) ? -1 : 0;
   }

   if (r != 0) {
      if (!nvgpu_push_start(ctx, &push, 256))
         goto out_clear;
      /* sema path first (matches channel submit encoding); then full frame_setup */
      if (nv_nvdec_session_emit_frame_sema(&push, &dec->session, bs_gpu,
                                           bs_size,
                                           dec->last_frame_sema_payload) == 0)
         r = 0;
      else if (nv_nvdec_session_emit_frame(&push, &dec->session, bs_gpu,
                                           bs_size) == 0)
         r = 0;
      nvgpu_push_finish(ctx, &push, true);
   }

   /* Wait for NVDEC sema on status BO, else channel GPFIFO idle as fallback */
   if (r == 0 && dec->status_map) {
      if (nv_nvdec_wait_status_cpu((volatile uint32_t *)dec->status_map,
                                   dec->last_frame_sema_payload,
                                   2000000000ull) != 0) {
         if (ctx->channel)
            (void)nv_channel_wait_idle(ctx->channel, 2000000000ull);
      }
   } else if (r == 0 && ctx->channel) {
      (void)nv_channel_wait_idle(ctx->channel, 2000000000ull);
   }
   if (ctx->channel)
      (void)nv_channel_check_notifier(ctx->channel, true, 0);

   dec->session.next_picture_index =
      (dec->session.next_picture_index + 1) & 15u;

out_clear:
   dec->frame_bs_used = 0;
   dec->frame_active = false;
   dec->cur_target = NULL;
   dec->cur_picture = NULL;
   return r;
}

static void
nvgpu_video_flush(struct pipe_video_codec *codec)
{
   struct nvgpu_video_codec *dec = (struct nvgpu_video_codec *)codec;
   if (!dec || !dec->ctx)
      return;
   if (dec->ctx->channel)
      (void)nv_channel_wait_idle(dec->ctx->channel, 1000000000ull);
}

/* ---- tick124: NVENC encode (pipe_video_codec ENCODE entrypoint) ---- */

struct nvgpu_video_encoder {
   struct pipe_video_codec base;
   struct nvgpu_context *ctx;
   struct nv_rm_bo *pic_setup_bo;
   void *pic_setup_map;
   struct nv_rm_bo *bitstream_out_bo;
   void *bitstream_out_map;
   uint32_t bitstream_out_size;
   struct nv_rm_bo *status_bo;
   void *status_map;
   uint32_t class_nvenc;
   uint32_t app_id;
   uint32_t width;
   uint32_t height;
   uint32_t frame_num;
   uint32_t last_sema_payload;
   struct pipe_video_buffer *cur_source;
   struct pipe_picture_desc *cur_picture;
   struct pipe_resource *pending_dest;
   void *pending_feedback;
   bool frame_active;
};

static uint32_t
nvgpu_profile_to_nvenc_app_id(enum pipe_video_profile profile)
{
   switch (u_reduce_video_profile(profile)) {
   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      return NV_NVENC_APP_ID_H264;
   case PIPE_VIDEO_FORMAT_HEVC:
      return NV_NVENC_APP_ID_HEVC;
   default:
      return 0;
   }
}

static bool
nvgpu_enc_ensure_bos(struct nvgpu_video_encoder *enc)
{
   struct nv_rm_bo_req req;
   struct nvgpu_screen *screen;

   if (!enc || !enc->ctx || !enc->ctx->screen || !enc->ctx->screen->rm)
      return false;
   screen = enc->ctx->screen;

   if (!enc->pic_setup_bo) {
      memset(&req, 0, sizeof(req));
      req.size = NVGPU_VID_PIC_SETUP_BO_SIZE;
      req.alignment = 256;
      req.vram = false;
      req.cpu_access = true;
      req.no_scanout = true;
      req.map_gpu_va = true;
      enc->pic_setup_bo = nv_rm_bo_alloc(screen->rm, &req);
      if (!enc->pic_setup_bo)
         return false;
      enc->pic_setup_map = nv_rm_bo_map(enc->pic_setup_bo);
      if (enc->pic_setup_map)
         memset(enc->pic_setup_map, 0, NVGPU_VID_PIC_SETUP_BO_SIZE);
   }
   if (!enc->bitstream_out_bo) {
      memset(&req, 0, sizeof(req));
      req.size = NVGPU_VID_BITSTREAM_BO_SIZE;
      req.alignment = 256;
      req.vram = false;
      req.cpu_access = true;
      req.no_scanout = true;
      req.map_gpu_va = true;
      enc->bitstream_out_bo = nv_rm_bo_alloc(screen->rm, &req);
      if (!enc->bitstream_out_bo)
         return false;
      enc->bitstream_out_size = NVGPU_VID_BITSTREAM_BO_SIZE;
      enc->bitstream_out_map = nv_rm_bo_map(enc->bitstream_out_bo);
   }
   if (!enc->status_bo) {
      memset(&req, 0, sizeof(req));
      req.size = NVGPU_VID_STATUS_BO_SIZE;
      req.alignment = 256;
      req.vram = false;
      req.cpu_access = true;
      req.no_scanout = true;
      req.map_gpu_va = true;
      enc->status_bo = nv_rm_bo_alloc(screen->rm, &req);
      if (!enc->status_bo)
         return false;
      enc->status_map = nv_rm_bo_map(enc->status_bo);
      if (enc->status_map)
         memset(enc->status_map, 0, NVGPU_VID_STATUS_BO_SIZE);
   }
   return enc->pic_setup_bo && enc->bitstream_out_bo && enc->status_bo;
}

static void
nvgpu_enc_destroy(struct pipe_video_codec *codec)
{
   struct nvgpu_video_encoder *enc = (struct nvgpu_video_encoder *)codec;
   if (!enc)
      return;
   if (enc->status_bo)
      nv_rm_bo_free(enc->status_bo);
   if (enc->bitstream_out_bo)
      nv_rm_bo_free(enc->bitstream_out_bo);
   if (enc->pic_setup_bo)
      nv_rm_bo_free(enc->pic_setup_bo);
   FREE(enc);
}

static void
nvgpu_enc_begin_frame(struct pipe_video_codec *codec,
                      struct pipe_video_buffer *target,
                      struct pipe_picture_desc *picture)
{
   struct nvgpu_video_encoder *enc = (struct nvgpu_video_encoder *)codec;
   if (!enc)
      return;
   enc->frame_active = true;
   enc->cur_source = target;
   enc->cur_picture = picture;
   enc->pending_dest = NULL;
   enc->pending_feedback = NULL;
}

static void
nvgpu_enc_encode_bitstream(struct pipe_video_codec *codec,
                           struct pipe_video_buffer *source,
                           struct pipe_resource *destination,
                           void **feedback)
{
   struct nvgpu_video_encoder *enc = (struct nvgpu_video_encoder *)codec;
   if (!enc)
      return;
   if (source)
      enc->cur_source = source;
   enc->pending_dest = destination;
   if (feedback)
      *feedback = (void *)(uintptr_t)(enc->frame_num + 1u);
   enc->pending_feedback = feedback ? *feedback : NULL;
}

static int
nvgpu_enc_end_frame(struct pipe_video_codec *codec,
                    struct pipe_video_buffer *target,
                    struct pipe_picture_desc *picture)
{
   struct nvgpu_video_encoder *enc = (struct nvgpu_video_encoder *)codec;
   struct nvgpu_context *ctx;
   struct nv_nvenc_frame_setup fs;
   struct nv_push push;
   uint64_t pic_gpu, bs_gpu, st_gpu, in_gpu = 0;
   uint32_t luma_pitch = 0, chroma_pitch = 0;
   uint64_t luma_va = 0, chroma_va = 0;
   uint32_t w, h;
   int r = -1;
   (void)target;
   (void)picture;

   if (!enc || !enc->frame_active)
      return 0;
   ctx = enc->ctx;
   if (!ctx || !nvgpu_enc_ensure_bos(enc))
      goto out_clear;

   w = enc->width ? enc->width : (enc->base.width ? enc->base.width : 64);
   h = enc->height ? enc->height : (enc->base.height ? enc->base.height : 64);

   if (enc->cur_source)
      nvgpu_video_buffer_planes(enc->cur_source, &luma_va, &chroma_va,
                                &luma_pitch, &chroma_pitch);
   in_gpu = luma_va ? luma_va : 0;

   pic_gpu = nv_rm_bo_gpu_offset(enc->pic_setup_bo);
   bs_gpu = nv_rm_bo_gpu_offset(enc->bitstream_out_bo);
   st_gpu = nv_rm_bo_gpu_offset(enc->status_bo);

   if (enc->pic_setup_map) {
      if (enc->app_id == NV_NVENC_APP_ID_H264 ||
          enc->app_id == NV_NVENC_APP_ID_HEVC) {
         nv_nvenc_pic_setup_fill_h264_smoke(
            (uint32_t *)enc->pic_setup_map,
            NVGPU_VID_PIC_SETUP_BO_SIZE / 4, w, h, 30, 1, in_gpu, bs_gpu);
         if (enc->app_id == NV_NVENC_APP_ID_HEVC) {
            /* HEVC uses same smoke layout subset until dedicated packer exists */
            ((uint32_t *)enc->pic_setup_map)[NV_NVENC_PS_SPS_FLAGS] =
               (1u /* Main */) | (120u << 8) | (1u << 16);
         }
      }
   }

   nv_nvenc_frame_setup_init_h264_smoke(&fs, pic_gpu, in_gpu, bs_gpu, w, h);
   fs.app_id = enc->app_id ? enc->app_id : NV_NVENC_APP_ID_H264;
   fs.status_gpu_addr = st_gpu;
   if (enc->app_id == NV_NVENC_APP_ID_HEVC)
      fs.app_id = NV_NVENC_APP_ID_HEVC;

   enc->last_sema_payload = enc->frame_num + 1u;
   if (enc->status_map)
      *(volatile uint32_t *)enc->status_map = 0;

   if (ctx->channel) {
      r = nv_channel_nvenc_frame_sema_submit(
         ctx->channel, enc->class_nvenc, &fs, st_gpu,
         enc->status_map ? (volatile uint32_t *)enc->status_map : NULL,
         enc->last_sema_payload, true, 2000000000ull, true);
      if (r == 0 || r == -EAGAIN)
         r = (r == -EAGAIN) ? -1 : 0;
   }

   if (r != 0) {
      if (!nvgpu_push_start(ctx, &push, 128))
         goto out_clear;
      nv_nvenc_emit_frame_kick(&push, enc->class_nvenc, &fs, st_gpu,
                               enc->last_sema_payload,
                               enc->status_map
                                  ? (volatile uint32_t *)enc->status_map
                                  : NULL);
      nvgpu_push_finish(ctx, &push, true);
      r = 0;
   }

   if (r == 0 && enc->status_map) {
      if (nv_nvdec_wait_status_cpu((volatile uint32_t *)enc->status_map,
                                   enc->last_sema_payload,
                                   2000000000ull) != 0) {
         if (ctx->channel)
            (void)nv_channel_wait_idle(ctx->channel, 2000000000ull);
      }
   } else if (r == 0 && ctx->channel) {
      (void)nv_channel_wait_idle(ctx->channel, 2000000000ull);
   }
   if (ctx->channel)
      (void)nv_channel_check_notifier(ctx->channel, true, 0);

   /*
    * Feedback / destination: no size written by HW yet — report 0 bytes so
    * clients do not read garbage; refine when status BO layout is silicon-proven.
    */
   (void)enc->pending_dest;
   (void)enc->pending_feedback;

   enc->frame_num++;

out_clear:
   enc->frame_active = false;
   enc->cur_source = NULL;
   enc->cur_picture = NULL;
   enc->pending_dest = NULL;
   enc->pending_feedback = NULL;
   return r;
}

static void
nvgpu_enc_flush(struct pipe_video_codec *codec)
{
   struct nvgpu_video_encoder *enc = (struct nvgpu_video_encoder *)codec;
   if (!enc || !enc->ctx)
      return;
   if (enc->ctx->channel)
      (void)nv_channel_wait_idle(enc->ctx->channel, 1000000000ull);
}

static void
nvgpu_enc_get_feedback(struct pipe_video_codec *codec,
                       void *feedback, unsigned *size)
{
   struct nvgpu_video_encoder *enc = (struct nvgpu_video_encoder *)codec;
   (void)feedback;
   if (size)
      *size = 0; /* unknown until status/bitstream size ring is reverse-engineered */
   (void)enc;
}

static struct pipe_video_codec *
nvgpu_create_video_encoder(struct pipe_context *context,
                           const struct pipe_video_codec *templ)
{
   struct nvgpu_context *ctx = (struct nvgpu_context *)context;
   struct nvgpu_video_encoder *enc;
   struct nvgpu_screen *screen;
   uint32_t app_id;
   uint32_t class_nvenc = 0;

   if (!context || !templ || !ctx)
      return NULL;

   app_id = nvgpu_profile_to_nvenc_app_id(templ->profile);
   if (!app_id)
      return NULL;

   screen = ctx->screen;
   if (!screen || !screen->rm)
      return NULL;
   if (screen->info)
      class_nvenc = screen->info->class_nvenc;
   if (!class_nvenc)
      class_nvenc = NV_VIDEO_CLASS_NVENC_TURING_C0B7;

   enc = CALLOC_STRUCT(nvgpu_video_encoder);
   if (!enc)
      return NULL;

   enc->base = *templ;
   enc->base.context = context;
   enc->ctx = ctx;
   enc->app_id = app_id;
   enc->class_nvenc = class_nvenc;
   enc->width = templ->width;
   enc->height = templ->height;

   enc->base.destroy = nvgpu_enc_destroy;
   enc->base.begin_frame = nvgpu_enc_begin_frame;
   enc->base.encode_bitstream = nvgpu_enc_encode_bitstream;
   enc->base.end_frame = nvgpu_enc_end_frame;
   enc->base.flush = nvgpu_enc_flush;
   enc->base.get_feedback = nvgpu_enc_get_feedback;

   if (!nvgpu_enc_ensure_bos(enc)) {
      nvgpu_enc_destroy(&enc->base);
      return NULL;
   }

   return &enc->base;
}

struct pipe_video_codec *
nvgpu_create_video_codec(struct pipe_context *context,
                         const struct pipe_video_codec *templ)
{
   struct nvgpu_context *ctx = (struct nvgpu_context *)context;
   struct nvgpu_video_codec *dec;
   struct nvgpu_screen *screen;
   uint32_t app_id;
   uint32_t class_nvdec = 0;

   if (!context || !templ || !ctx)
      return NULL;
   /* tick123/124: bitstream decode (NVDEC) or encode (NVENC) */
   if (templ->entrypoint != PIPE_VIDEO_ENTRYPOINT_BITSTREAM &&
       templ->entrypoint != PIPE_VIDEO_ENTRYPOINT_ENCODE)
      return NULL;
   if (templ->entrypoint == PIPE_VIDEO_ENTRYPOINT_ENCODE)
      return nvgpu_create_video_encoder(context, templ);

   app_id = nvgpu_profile_to_app_id(templ->profile);
   if (!app_id)
      return NULL;

   screen = ctx->screen;
   if (!screen || !screen->rm)
      return NULL;
   if (screen->info)
      class_nvdec = screen->info->class_nvdec;
   if (!class_nvdec)
      class_nvdec = NV_VIDEO_CLASS_NVDEC_PASCAL_B0;

   dec = CALLOC_STRUCT(nvgpu_video_codec);
   if (!dec)
      return NULL;

   dec->base = *templ;
   dec->base.context = context;
   dec->ctx = ctx;
   dec->app_id = app_id;
   dec->class_nvdec = class_nvdec;

   nv_nvdec_session_init(&dec->session, class_nvdec, app_id);

   dec->base.destroy = nvgpu_video_destroy;
   dec->base.begin_frame = nvgpu_video_begin_frame;
   dec->base.decode_bitstream = nvgpu_video_decode_bitstream;
   dec->base.end_frame = nvgpu_video_end_frame;
   dec->base.flush = nvgpu_video_flush;

   if (!nvgpu_video_ensure_bos(dec)) {
      nvgpu_video_destroy(&dec->base);
      return NULL;
   }

   nv_nvdec_session_set_pic_setup_bo(&dec->session,
      nv_rm_bo_gpu_offset(dec->pic_setup_bo), dec->pic_setup_map,
      NVGPU_VID_PIC_SETUP_BO_SIZE);
   nv_nvdec_session_set_status_bo(&dec->session,
      nv_rm_bo_gpu_offset(dec->status_bo));
   if (dec->status_map)
      nv_nvdec_session_set_status_cpu_map(
         &dec->session, (volatile uint32_t *)dec->status_map);

   return &dec->base;
}

struct pipe_video_buffer *
nvgpu_create_video_buffer(struct pipe_context *context,
                          const struct pipe_video_buffer *tmpl)
{
   return vl_video_buffer_create(context, tmpl);
}

int
nvgpu_screen_get_video_param(struct pipe_screen *pscreen,
                             enum pipe_video_profile profile,
                             enum pipe_video_entrypoint entrypoint,
                             enum pipe_video_cap param)
{
   uint32_t app_id;
   (void)pscreen;

   if (entrypoint != PIPE_VIDEO_ENTRYPOINT_BITSTREAM &&
       entrypoint != PIPE_VIDEO_ENTRYPOINT_ENCODE)
      return 0;

   app_id = nvgpu_profile_to_app_id(profile);
   if (!app_id)
      return 0;
   /* tick124: NVENC H.264/HEVC encode via pipe_video_codec */
   if (entrypoint == PIPE_VIDEO_ENTRYPOINT_ENCODE &&
       app_id != NV_NVDEC_APP_ID_H264 && app_id != NV_NVDEC_APP_ID_HEVC)
      return 0;

   switch (param) {
   case PIPE_VIDEO_CAP_SUPPORTED:
      return 1;
   case PIPE_VIDEO_CAP_MAX_WIDTH:
   case PIPE_VIDEO_CAP_MAX_HEIGHT:
      if (app_id == NV_NVDEC_APP_ID_HEVC || app_id == NV_NVDEC_APP_ID_AV1)
         return 8192;
      return 4096;
   case PIPE_VIDEO_CAP_MIN_WIDTH:
   case PIPE_VIDEO_CAP_MIN_HEIGHT:
      return 48;
   case PIPE_VIDEO_CAP_SUPPORTS_PROGRESSIVE:
      return 1;
   case PIPE_VIDEO_CAP_MAX_MACROBLOCKS:
      return (4096 / 16) * (4096 / 16);
   case PIPE_VIDEO_CAP_SUPPORTS_CONTIGUOUS_PLANES_MAP:
      return 1;
   case PIPE_VIDEO_CAP_REQUIRES_FLUSH_ON_END_FRAME:
      return 1;
   default:
      return 0;
   }
}

bool
nvgpu_screen_is_video_format_supported(struct pipe_screen *pscreen,
                                       enum pipe_format format,
                                       enum pipe_video_profile profile,
                                       enum pipe_video_entrypoint entrypoint)
{
   (void)pscreen;
   (void)profile;
   if (entrypoint != PIPE_VIDEO_ENTRYPOINT_BITSTREAM &&
       entrypoint != PIPE_VIDEO_ENTRYPOINT_PROCESSING)
      return false;
   return format == PIPE_FORMAT_NV12 || format == PIPE_FORMAT_P010 ||
          format == PIPE_FORMAT_P016;
}
