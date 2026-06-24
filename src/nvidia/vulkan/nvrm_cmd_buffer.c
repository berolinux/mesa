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

#define NVRM_CMD_PUSH_DWORDS (32 * 1024)

VKAPI_ATTR VkResult VKAPI_CALL
nvrm_BeginCommandBuffer(VkCommandBuffer commandBuffer,
                        const VkCommandBufferBeginInfo *pBeginInfo)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   struct nv_rm_bo_req req;
   (void)pBeginInfo;

   if (!cmd->push_bo) {
      memset(&req, 0, sizeof(req));
      req.size = NVRM_CMD_PUSH_DWORDS * 4;
      req.alignment = 4096;
      req.vram = false;
      req.cpu_access = true;
      req.no_scanout = true;
      cmd->push_bo = nv_rm_bo_alloc(cmd->device->rm, &req);
      if (!cmd->push_bo)
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;
      cmd->push_map = nv_rm_bo_map(cmd->push_bo);
      cmd->push_dw_cap = NVRM_CMD_PUSH_DWORDS;
   }
   cmd->push_dw_used = 0;
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
   (void)commandBuffer;
   (void)pDependencyInfo;
   /* GPU cache/L2 invalidate methods will go here */
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

   /* Colour-only CLEAR_SURFACE via NVC597 methods (render targets must be
    * programmed separately; this records the clear values + CLEAR_SURFACE). */
   nv_3d_push_clear(&cmd->push, class_3d, 0x10 /* PIPE_CLEAR_COLOR0 */, c, 0.0f, 0);
}

/* Bind a pre-uploaded nv_shader to the graphics pipeline (helper for pipeline
 * bind once shader modules compile to machine code). */
void
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
void
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
