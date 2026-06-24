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
   uint32_t class_nvdec;
   uint32_t app_id;
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

static void
nvgpu_video_apply_picture_desc(struct nvgpu_video_codec *dec,
                               struct pipe_picture_desc *picture)
{
   if (!dec || !picture)
      return;
   if (nvgpu_video_apply_h264_picture_desc(dec, picture))
      return;
   (void)nvgpu_video_apply_h265_picture_desc(dec, picture);
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
   nv_nvdec_session_pack_pic_setup(&dec->session);

   if (!nvgpu_push_start(ctx, &push, 256))
      goto out_clear;

   if (nv_nvdec_session_emit_frame(&push, &dec->session, bs_gpu, bs_size) == 0)
      r = 0;
   nvgpu_push_finish(ctx, &push, true);

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
   if (templ->entrypoint != PIPE_VIDEO_ENTRYPOINT_BITSTREAM)
      return NULL;

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

   if (entrypoint != PIPE_VIDEO_ENTRYPOINT_BITSTREAM)
      return 0;

   app_id = nvgpu_profile_to_app_id(profile);
   if (!app_id)
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
