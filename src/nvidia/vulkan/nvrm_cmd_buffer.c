/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Command buffer records into a CPU pushbuffer; submit path kicks GPFIFO.
 */

#include "nvrm_private.h"

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
   (void)image;
   (void)imageLayout;
   (void)rangeCount;
   (void)pRanges;

   if (!cmd->push_map || !info)
      return;

   nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_3D);
   nv_push_method(&cmd->push, 0x0000, info->class_3d);
   if (pColor)
      nv_push_methodN(&cmd->push, 0x0d80, pColor->uint32, 4);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdCopyBuffer2(VkCommandBuffer commandBuffer,
                    const VkCopyBufferInfo2 *pCopyBufferInfo)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   const struct nv_device_info *info = cmd->device->info;
   uint32_t i;

   if (!cmd->push_map || !info)
      return;

   nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_COPY);
   nv_push_method(&cmd->push, 0x0000, info->class_copy);

   for (i = 0; i < pCopyBufferInfo->regionCount; i++) {
      const VkBufferCopy2 *r = &pCopyBufferInfo->pRegions[i];
      /* Real DMA_COPY launch methods TBD from class header NVC5B5/NVC6B5 */
      nv_push_method(&cmd->push, 0x0400, (uint32_t)r->srcOffset);
      nv_push_method(&cmd->push, 0x0404, (uint32_t)(r->srcOffset >> 32));
      nv_push_method(&cmd->push, 0x0408, (uint32_t)r->dstOffset);
      nv_push_method(&cmd->push, 0x040c, (uint32_t)(r->dstOffset >> 32));
      nv_push_method(&cmd->push, 0x0418, (uint32_t)r->size);
   }
}
