/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Command buffer records into a CPU pushbuffer; submit path kicks GPFIFO.
 */

#include "nvrm_private.h"
#include "nv_3d_methods.h"
#include "nv_copy_methods.h"
#include "nv_shader.h"
#include "nv_qmd.h"
#include "nv_rm.h"

#define NVRM_CMD_PUSH_DWORDS (32 * 1024)
#define NVRM_QMD_SCRATCH_SIZE  4096  /* 256B QMD + padding, 256B aligned */
/* Fallback LMEM BO when device SM count unknown (covers ~2 SMs at min granule) */
#define NVRM_LMEM_SCRATCH_SIZE (2 * NV_LMEM_MIN_PER_SM_BYTES)
/* Push constants CB: 256B min selector size; hold 64 dwords = 256B exactly */
#define NVRM_PUSH_CONST_BO_SIZE  256

VKAPI_ATTR VkResult VKAPI_CALL
nvrm_BeginCommandBuffer(VkCommandBuffer commandBuffer,
                        const VkCommandBufferBeginInfo *pBeginInfo)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   struct nv_rm_bo_req req;
   const struct nv_device_info *info;
   uint32_t sm_count = 2;
   uint64_t lmem_need;
   (void)pBeginInfo;

   if (!cmd->push_bo) {
      memset(&req, 0, sizeof(req));
      req.size = NVRM_CMD_PUSH_DWORDS * 4;
      req.alignment = 4096;
      req.vram = false;
      req.cpu_access = true;
      req.no_scanout = true;
      req.map_gpu_va = true;
      cmd->push_bo = nv_rm_bo_alloc(cmd->device->rm, &req);
      if (!cmd->push_bo)
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;
      cmd->push_map = nv_rm_bo_map(cmd->push_bo);
      cmd->push_dw_cap = NVRM_CMD_PUSH_DWORDS;
   }
   /* Dedicated 256B-aligned QMD scratch for SEND_PCAS address field */
   if (!cmd->qmd_bo && cmd->device && cmd->device->rm) {
      memset(&req, 0, sizeof(req));
      req.size = NVRM_QMD_SCRATCH_SIZE;
      req.alignment = 256;
      req.vram = false;
      req.cpu_access = true;
      req.no_scanout = true;
      req.map_gpu_va = true;
      cmd->qmd_bo = nv_rm_bo_alloc(cmd->device->rm, &req);
   }
   /* Global LMEM backing sized for SM count * per-SM window (conservative spill) */
   info = cmd->device ? cmd->device->info : NULL;
   if (info && info->tpc_count)
      sm_count = info->tpc_count;
   lmem_need = nv_lmem_total_bo_bytes(0 /* min spill */, sm_count);
   if (lmem_need < NVRM_LMEM_SCRATCH_SIZE)
      lmem_need = NVRM_LMEM_SCRATCH_SIZE;
   if (lmem_need > 0x10000000ull) /* 256 MiB hard cap for cmd-buffer alloc */
      lmem_need = 0x10000000ull;
   if (!cmd->lmem_bo && cmd->device && cmd->device->rm) {
      memset(&req, 0, sizeof(req));
      req.size = (uint32_t)lmem_need;
      req.alignment = 4096;
      req.vram = true;
      req.cpu_access = false;
      req.no_scanout = true;
      req.map_gpu_va = true;
      cmd->lmem_bo = nv_rm_bo_alloc(cmd->device->rm, &req);
      if (cmd->lmem_bo)
         cmd->lmem_bo_size = (uint32_t)lmem_need;
   } else if (cmd->lmem_bo && cmd->lmem_bo_size < (uint32_t)lmem_need) {
      /* Re-alloc larger if previous begin used smaller size (rare) */
      nv_rm_bo_free(cmd->lmem_bo);
      cmd->lmem_bo = NULL;
      memset(&req, 0, sizeof(req));
      req.size = (uint32_t)lmem_need;
      req.alignment = 4096;
      req.vram = true;
      req.cpu_access = false;
      req.no_scanout = true;
      req.map_gpu_va = true;
      cmd->lmem_bo = nv_rm_bo_alloc(cmd->device->rm, &req);
      if (cmd->lmem_bo)
         cmd->lmem_bo_size = (uint32_t)lmem_need;
   }
   /* Push-constants CB: CPU-writable + GPU-mapped for LOAD_CONSTANT_BUFFER */
   if (!cmd->push_const_bo && cmd->device && cmd->device->rm) {
      memset(&req, 0, sizeof(req));
      req.size = NVRM_PUSH_CONST_BO_SIZE;
      req.alignment = 256;
      req.vram = false;
      req.cpu_access = true;
      req.no_scanout = true;
      req.map_gpu_va = true;
      cmd->push_const_bo = nv_rm_bo_alloc(cmd->device->rm, &req);
      if (cmd->push_const_bo) {
         cmd->push_const_bo_size = NVRM_PUSH_CONST_BO_SIZE;
         cmd->push_const_map = nv_rm_bo_map(cmd->push_const_bo);
         if (cmd->push_const_map)
            memset(cmd->push_const_map, 0, NVRM_PUSH_CONST_BO_SIZE);
      }
   }

   /* Host-mappable shadow for indirect draw path B (CE copies GPU-only IB) */
   if (!cmd->indirect_shadow_bo && cmd->device && cmd->device->rm) {
      memset(&req, 0, sizeof(req));
      req.size = 4096; /* many indirect draw records (16/20 B each) */
      req.alignment = 256;
      req.vram = false;
      req.cpu_access = true;
      req.no_scanout = true;
      req.map_gpu_va = true;
      cmd->indirect_shadow_bo = nv_rm_bo_alloc(cmd->device->rm, &req);
      if (cmd->indirect_shadow_bo) {
         cmd->indirect_shadow_bo_size = 4096;
         cmd->indirect_shadow_map = nv_rm_bo_map(cmd->indirect_shadow_bo);
      }
   }

   cmd->push_dw_used = 0;
   cmd->compute_init_done = false;
   cmd->lmem_programmed = false;
   cmd->lmem_local_req = 0;
   cmd->bound_compute_pipeline = NULL;
   cmd->bound_gfx_pipeline = NULL;
   cmd->bound_set_count = 0;
   cmd->dynamic_offset_count = 0;
   cmd->index_valid = false;
   cmd->dyn_depth_valid = false;
   cmd->dyn_stencil_valid = false;
   cmd->dyn_cull_mode_valid = false;
   cmd->dyn_front_face_valid = false;
   cmd->dyn_depth_test_enable = false;
   cmd->dyn_depth_write_enable = true;
   cmd->dyn_depth_compare_op = VK_COMPARE_OP_LESS;
   cmd->dyn_stencil_test_enable = false;
   cmd->dyn_stencil_front_compare_op = VK_COMPARE_OP_ALWAYS;
   cmd->dyn_stencil_front_fail_op = VK_STENCIL_OP_KEEP;
   cmd->dyn_stencil_front_zfail_op = VK_STENCIL_OP_KEEP;
   cmd->dyn_stencil_front_zpass_op = VK_STENCIL_OP_KEEP;
   cmd->dyn_stencil_front_compare_mask = 0xff;
   cmd->dyn_stencil_front_write_mask = 0xff;
   cmd->dyn_stencil_front_reference = 0;
   cmd->push_const_dwords = 0;
   cmd->push_const_dirty = false;
   cmd->prim_restart_enable = false;
   memset(cmd->bound_sets, 0, sizeof(cmd->bound_sets));
   memset(cmd->vtx_binding, 0, sizeof(cmd->vtx_binding));
   if (cmd->push_map)
      nv_push_init(&cmd->push, cmd->push_map, cmd->push_dw_cap);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvrm_EndCommandBuffer(VkCommandBuffer commandBuffer)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   if (cmd->push_map)
      cmd->push_dw_used = nv_push_dw_count(&cmd->push);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdPipelineBarrier2(VkCommandBuffer commandBuffer,
                         const VkDependencyInfo *pDependencyInfo)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   (void)pDependencyInfo;

   if (!cmd || !cmd->push_map)
      return;

   /* Host WFI + texture/shader cache invalidates on 3D subchannel.
    * Memory dependency scopes are not yet split by stage; full invalidates
    * match the conservative path used by proprietary driver after transfers. */
   nv_push_wfi(&cmd->push);
   nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_3D);
   nv_push_method(&cmd->push, NVC597_INVALIDATE_SAMPLER_CACHE, 0);
   nv_push_method(&cmd->push, NVC597_INVALIDATE_TEXTURE_HEADER_CACHE, 0);
   nv_push_method(&cmd->push, NVC597_INVALIDATE_TEXTURE_DATA_CACHE, 0);
   nv_push_method(&cmd->push, NVC597_INVALIDATE_SHADER_CACHES,
                  NVC597_INVALIDATE_SHADER_CACHES_INSTRUCTION_TRUE |
                  NVC597_INVALIDATE_SHADER_CACHES_DATA_TRUE |
                  NVC597_INVALIDATE_SHADER_CACHES_CONSTANT_TRUE);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image,
                        VkImageLayout imageLayout,
                        const VkClearColorValue *pColor,
                        uint32_t rangeCount,
                        const VkImageSubresourceRange *pRanges)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   const struct nv_device_info *info = cmd->device->info;
   uint32_t class_3d = info ? info->class_3d : 0;
   const uint32_t *c = pColor ? pColor->uint32 : NULL;
   (void)image;
   (void)imageLayout;
   (void)rangeCount;
   (void)pRanges;

   if (!cmd->push_map)
      return;

   /* Colour clear: if inside a multi-RT render pass, clear all active MRTs
    * with the same colour; otherwise target 0 only. */
   {
      unsigned buffers = 0x10;
      unsigned mrt = cmd->render_color_count ? cmd->render_color_count : 1;
      unsigned i;
      for (i = 0; i < mrt && i < 8; i++)
         buffers |= (0x10u << i);
      if (mrt > 1)
         nv_3d_push_clear_multi(&cmd->push, class_3d, buffers, c, 0.0f, 0, mrt);
      else
         nv_3d_push_clear(&cmd->push, class_3d, 0x10, c, 0.0f, 0);
   }
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdClearAttachments(VkCommandBuffer commandBuffer,
                         uint32_t attachmentCount,
                         const VkClearAttachment *pAttachments,
                         uint32_t rectCount,
                         const VkClearRect *pRects)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   const struct nv_device_info *info = cmd->device ? cmd->device->info : NULL;
   uint32_t class_3d = info ? info->class_3d : 0;
   uint32_t a;

   (void)rectCount;
   (void)pRects;

   if (!cmd->push_map || !pAttachments || !attachmentCount)
      return;

   if (class_3d)
      nv_3d_set_object(&cmd->push, class_3d);
   else
      nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_3D);

   for (a = 0; a < attachmentCount; a++) {
      const VkClearAttachment *att = &pAttachments[a];
      if (att->aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
         const uint32_t *c = att->clearValue.color.uint32;
         unsigned ti = att->colorAttachment & 7u;
         nv_3d_emit_clear_surface_mrt(&cmd->push, ti, c, true, true, true, true);
      }
      if (att->aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
         unsigned buffers = 0;
         if (att->aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT)
            buffers |= 0x100;
         if (att->aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT)
            buffers |= 0x200;
         nv_3d_emit_clear_surface(&cmd->push, buffers, NULL,
                                  att->clearValue.depthStencil.depth,
                                  att->clearValue.depthStencil.stencil);
      }
   }
   nv_push_wfi(&cmd->push);
}

/* Bind a pre-uploaded nv_shader to the graphics pipeline (helper for pipeline
 * bind once shader modules compile to machine code). */
static void
nvrm_cmd_bind_graphics_shader(struct nvrm_cmd_buffer *cmd,
                              const struct nv_shader *sh,
                              uint64_t program_region_base)
{
   if (!cmd || !cmd->push_map || !sh)
      return;
   nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_3D);
   nv_shader_emit_bind(&cmd->push, sh, program_region_base, -1);
}

/* Record a host semaphore release at the end of the command buffer segment
 * (queue submit path can wait via nv_fence_wait on the same sema BO). */
static void
nvrm_cmd_emit_host_sema_release(struct nvrm_cmd_buffer *cmd,
                                uint64_t sema_gpu_addr, uint32_t payload)
{
   if (!cmd || !cmd->push_map || !sema_gpu_addr)
      return;
   nv_push_wfi(&cmd->push);
   nv_push_host_semaphore_release(&cmd->push, sema_gpu_addr, payload);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdCopyBuffer2(VkCommandBuffer commandBuffer,
                    const VkCopyBufferInfo2 *pCopyBufferInfo)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   const struct nv_device_info *info = cmd->device->info;
   VK_FROM_HANDLE(nvrm_buffer, src_buf, pCopyBufferInfo->srcBuffer);
   VK_FROM_HANDLE(nvrm_buffer, dst_buf, pCopyBufferInfo->dstBuffer);
   uint64_t src_base = 0, dst_base = 0;
   uint32_t i;

   if (!cmd->push_map)
      return;

   if (src_buf && src_buf->bo)
      src_base = nv_rm_bo_gpu_offset(src_buf->bo);
   if (dst_buf && dst_buf->bo)
      dst_base = nv_rm_bo_gpu_offset(dst_buf->bo);

   if (info && info->class_copy)
      nv_copy_set_object(&cmd->push, info->class_copy);
   else
      nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_COPY);

   for (i = 0; i < pCopyBufferInfo->regionCount; i++) {
      const VkBufferCopy2 *r = &pCopyBufferInfo->pRegions[i];
      nv_copy_emit_buffer_copy(&cmd->push,
                               src_base + r->srcOffset,
                               dst_base + r->dstOffset,
                               (uint32_t)r->size, 0, 0, 1);
   }
   nv_push_wfi(&cmd->push);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdCopyImage2(VkCommandBuffer commandBuffer,
                   const VkCopyImageInfo2 *pCopyImageInfo)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   const struct nv_device_info *info = cmd->device->info;
   VK_FROM_HANDLE(nvrm_image, src_img, pCopyImageInfo->srcImage);
   VK_FROM_HANDLE(nvrm_image, dst_img, pCopyImageInfo->dstImage);
   uint64_t src_base = 0, dst_base = 0;
   uint32_t class_copy = info ? info->class_copy : 0;
   uint32_t i;

   if (!cmd->push_map || !src_img || !dst_img)
      return;

   src_base = src_img->gpu_offset ? src_img->gpu_offset :
              (src_img->bo ? nv_rm_bo_gpu_offset(src_img->bo) : 0);
   dst_base = dst_img->gpu_offset ? dst_img->gpu_offset :
              (dst_img->bo ? nv_rm_bo_gpu_offset(dst_img->bo) : 0);

   if (class_copy)
      nv_copy_set_object(&cmd->push, class_copy);
   else
      nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_COPY);

   for (i = 0; i < pCopyImageInfo->regionCount; i++) {
      const VkImageCopy2 *r = &pCopyImageInfo->pRegions[i];
      uint32_t w = r->extent.width ? r->extent.width : 1;
      uint32_t h = r->extent.height ? r->extent.height : 1;
      uint32_t bpp = src_img->bpp ? src_img->bpp : 4;
      uint32_t pitch_in = src_img->row_pitch ? src_img->row_pitch : (w * bpp + 31u) & ~31u;
      uint32_t pitch_out = dst_img->row_pitch ? dst_img->row_pitch : (w * bpp + 31u) & ~31u;
      bool src_bl = src_img->is_blocklinear && !src_img->is_linear;
      bool dst_bl = dst_img->is_blocklinear && !dst_img->is_linear;
      uint32_t bl_w = 0, bl_h = 0;

      /* Blocklinear block size encoding: log2 gobs (see NVC6B5_SET_*_BLOCK_SIZE) */
      if (src_bl || dst_bl) {
         bl_w = src_img->gobs_width;
         bl_h = src_img->gobs_height ? src_img->gobs_height : 4; /* SIXTEEN gobs */
         nv_copy_emit_image_2d_bl(&cmd->push, src_base, dst_base,
                                  w, h, bpp, pitch_in, pitch_out,
                                  r->srcOffset.x, r->srcOffset.y,
                                  r->dstOffset.x, r->dstOffset.y,
                                  src_bl, dst_bl,
                                  bl_w, bl_h, bl_w, bl_h);
      } else {
         uint32_t line_len = w * bpp;
         uint64_t s = src_base + (uint64_t)r->srcOffset.y * pitch_in +
                      (uint64_t)r->srcOffset.x * bpp;
         uint64_t d = dst_base + (uint64_t)r->dstOffset.y * pitch_out +
                      (uint64_t)r->dstOffset.x * bpp;
         nv_copy_emit_image_2d(&cmd->push, s, d, line_len, pitch_in, pitch_out, h);
      }
   }
   nv_push_wfi(&cmd->push);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdCopyBufferToImage2(VkCommandBuffer commandBuffer,
                           const VkCopyBufferToImageInfo2 *pCopyBufferToImageInfo)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   const struct nv_device_info *info = cmd->device->info;
   VK_FROM_HANDLE(nvrm_buffer, src_buf, pCopyBufferToImageInfo->srcBuffer);
   VK_FROM_HANDLE(nvrm_image, dst_img, pCopyBufferToImageInfo->dstImage);
   uint64_t src_base = 0, dst_base = 0;
   uint32_t class_copy = info ? info->class_copy : 0;
   uint32_t i;

   if (!cmd->push_map || !dst_img)
      return;

   if (src_buf && src_buf->bo)
      src_base = nv_rm_bo_gpu_offset(src_buf->bo);
   dst_base = dst_img->gpu_offset ? dst_img->gpu_offset :
              (dst_img->bo ? nv_rm_bo_gpu_offset(dst_img->bo) : 0);

   if (class_copy)
      nv_copy_set_object(&cmd->push, class_copy);
   else
      nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_COPY);

   for (i = 0; i < pCopyBufferToImageInfo->regionCount; i++) {
      const VkBufferImageCopy2 *r = &pCopyBufferToImageInfo->pRegions[i];
      uint32_t w = r->imageExtent.width ? r->imageExtent.width : 1;
      uint32_t h = r->imageExtent.height ? r->imageExtent.height : 1;
      uint32_t bpp = dst_img->bpp ? dst_img->bpp : 4;
      uint32_t pitch_out = dst_img->row_pitch ? dst_img->row_pitch : (w * bpp + 31u) & ~31u;
      uint32_t buf_row = r->bufferRowLength ? r->bufferRowLength * bpp : (w * bpp);
      uint64_t s = src_base + r->bufferOffset;
      uint64_t d = dst_base + (uint64_t)r->imageOffset.y * pitch_out +
                   (uint64_t)r->imageOffset.x * bpp;
      bool dst_bl = dst_img->is_blocklinear && !dst_img->is_linear;

      if (dst_bl) {
         nv_copy_emit_image_2d_bl(&cmd->push, s, d, w, h, bpp, buf_row, pitch_out,
                                  0, 0, r->imageOffset.x, r->imageOffset.y,
                                  false, true,
                                  0, 0,
                                  dst_img->gobs_width,
                                  dst_img->gobs_height ? dst_img->gobs_height : 4);
      } else {
         nv_copy_emit_image_2d(&cmd->push, s, d, w * bpp, buf_row, pitch_out, h);
      }
   }
   nv_push_wfi(&cmd->push);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdCopyImageToBuffer2(VkCommandBuffer commandBuffer,
                           const VkCopyImageToBufferInfo2 *pCopyImageToBufferInfo)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   const struct nv_device_info *info = cmd->device->info;
   VK_FROM_HANDLE(nvrm_image, src_img, pCopyImageToBufferInfo->srcImage);
   VK_FROM_HANDLE(nvrm_buffer, dst_buf, pCopyImageToBufferInfo->dstBuffer);
   uint64_t src_base = 0, dst_base = 0;
   uint32_t class_copy = info ? info->class_copy : 0;
   uint32_t i;

   if (!cmd->push_map || !src_img)
      return;

   src_base = src_img->gpu_offset ? src_img->gpu_offset :
              (src_img->bo ? nv_rm_bo_gpu_offset(src_img->bo) : 0);
   if (dst_buf && dst_buf->bo)
      dst_base = nv_rm_bo_gpu_offset(dst_buf->bo);

   if (class_copy)
      nv_copy_set_object(&cmd->push, class_copy);
   else
      nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_COPY);

   for (i = 0; i < pCopyImageToBufferInfo->regionCount; i++) {
      const VkBufferImageCopy2 *r = &pCopyImageToBufferInfo->pRegions[i];
      uint32_t w = r->imageExtent.width ? r->imageExtent.width : 1;
      uint32_t h = r->imageExtent.height ? r->imageExtent.height : 1;
      uint32_t bpp = src_img->bpp ? src_img->bpp : 4;
      uint32_t pitch_in = src_img->row_pitch ? src_img->row_pitch : (w * bpp + 31u) & ~31u;
      uint32_t buf_row = r->bufferRowLength ? r->bufferRowLength * bpp : (w * bpp);
      uint64_t s = src_base + (uint64_t)r->imageOffset.y * pitch_in +
                   (uint64_t)r->imageOffset.x * bpp;
      uint64_t d = dst_base + r->bufferOffset;
      bool src_bl = src_img->is_blocklinear && !src_img->is_linear;

      if (src_bl) {
         nv_copy_emit_image_2d_bl(&cmd->push, s, d, w, h, bpp, pitch_in, buf_row,
                                  r->imageOffset.x, r->imageOffset.y, 0, 0,
                                  true, false,
                                  src_img->gobs_width,
                                  src_img->gobs_height ? src_img->gobs_height : 4,
                                  0, 0);
      } else {
         nv_copy_emit_image_2d(&cmd->push, s, d, w * bpp, pitch_in, buf_row, h);
      }
   }
   nv_push_wfi(&cmd->push);
}
