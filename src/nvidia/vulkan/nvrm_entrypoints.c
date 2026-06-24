/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Mesa vk_*_dispatch_table instances for vk_device_init / instance init.
 * Generated nvrm_entrypoints_gen.c (meson) provides weak C entrypoint stubs
 * with a different type (vk_*_entrypoint_table) for ICD/GetProcAddr; we do
 * not include that header here to avoid symbol/type clashes.
 */

#include "nvrm_private.h"
#include "nvrm_wsi.h"

struct vk_instance_dispatch_table nvrm_instance_entrypoints;
struct vk_physical_device_dispatch_table nvrm_physical_device_entrypoints;
struct vk_device_dispatch_table nvrm_device_entrypoints;

static void __attribute__((constructor))
nvrm_init_entrypoints(void)
{
   memset(&nvrm_instance_entrypoints, 0, sizeof(nvrm_instance_entrypoints));
   memset(&nvrm_physical_device_entrypoints, 0,
          sizeof(nvrm_physical_device_entrypoints));
   memset(&nvrm_device_entrypoints, 0, sizeof(nvrm_device_entrypoints));

   nvrm_instance_entrypoints.CreateInstance = nvrm_CreateInstance;
   nvrm_instance_entrypoints.DestroyInstance = nvrm_DestroyInstance;
   nvrm_instance_entrypoints.GetInstanceProcAddr = nvrm_GetInstanceProcAddr;

   nvrm_physical_device_entrypoints.GetPhysicalDeviceProperties2 =
      nvrm_GetPhysicalDeviceProperties2;
   nvrm_physical_device_entrypoints.GetPhysicalDeviceQueueFamilyProperties2 =
      nvrm_GetPhysicalDeviceQueueFamilyProperties2;
   nvrm_physical_device_entrypoints.GetPhysicalDeviceMemoryProperties2 =
      nvrm_GetPhysicalDeviceMemoryProperties2;

   /* Device dispatch table field set is version-dependent; only assign
    * members that exist on current mesa tip (verified by compile). */
#define NVRM_SET(field, fn) \
   do { nvrm_device_entrypoints.field = (fn); } while (0)

   NVRM_SET(AllocateMemory, nvrm_AllocateMemory);
   NVRM_SET(FreeMemory, nvrm_FreeMemory);
   NVRM_SET(MapMemory, nvrm_MapMemory);
   NVRM_SET(UnmapMemory, nvrm_UnmapMemory);
   NVRM_SET(CreateBuffer, nvrm_CreateBuffer);
   NVRM_SET(DestroyBuffer, nvrm_DestroyBuffer);
   NVRM_SET(BindBufferMemory2, nvrm_BindBufferMemory2);
   NVRM_SET(GetBufferMemoryRequirements2, nvrm_GetBufferMemoryRequirements2);
   NVRM_SET(GetImageMemoryRequirements2, nvrm_GetImageMemoryRequirements2);
   NVRM_SET(CreateImage, nvrm_CreateImage);
   NVRM_SET(DestroyImage, nvrm_DestroyImage);
   NVRM_SET(BindImageMemory2, nvrm_BindImageMemory2);
   NVRM_SET(CreateImageView, nvrm_CreateImageView);
   NVRM_SET(DestroyImageView, nvrm_DestroyImageView);
   NVRM_SET(CreateSampler, nvrm_CreateSampler);
   NVRM_SET(DestroySampler, nvrm_DestroySampler);
   NVRM_SET(CreateGraphicsPipelines, nvrm_CreateGraphicsPipelines);
   NVRM_SET(CreateComputePipelines, nvrm_CreateComputePipelines);
   NVRM_SET(DestroyPipeline, nvrm_DestroyPipeline);
   NVRM_SET(CreatePipelineLayout, nvrm_CreatePipelineLayout);
   NVRM_SET(DestroyPipelineLayout, nvrm_DestroyPipelineLayout);
   NVRM_SET(CreateDescriptorSetLayout, nvrm_CreateDescriptorSetLayout);
   NVRM_SET(DestroyDescriptorSetLayout, nvrm_DestroyDescriptorSetLayout);
   NVRM_SET(CreateDescriptorPool, nvrm_CreateDescriptorPool);
   NVRM_SET(DestroyDescriptorPool, nvrm_DestroyDescriptorPool);
   NVRM_SET(AllocateDescriptorSets, nvrm_AllocateDescriptorSets);
   NVRM_SET(FreeDescriptorSets, nvrm_FreeDescriptorSets);
   NVRM_SET(UpdateDescriptorSets, nvrm_UpdateDescriptorSets);
   NVRM_SET(BeginCommandBuffer, nvrm_BeginCommandBuffer);
   NVRM_SET(EndCommandBuffer, nvrm_EndCommandBuffer);
   NVRM_SET(CmdBindPipeline, nvrm_CmdBindPipeline);
   NVRM_SET(CmdBindDescriptorSets, nvrm_CmdBindDescriptorSets);
   NVRM_SET(CmdBeginRendering, nvrm_CmdBeginRendering);
   NVRM_SET(CmdEndRendering, nvrm_CmdEndRendering);
   NVRM_SET(CmdDraw, nvrm_CmdDraw);
   NVRM_SET(CmdDrawIndexed, nvrm_CmdDrawIndexed);
   NVRM_SET(CmdDrawIndirect, nvrm_CmdDrawIndirect);
   NVRM_SET(CmdDrawIndexedIndirect, nvrm_CmdDrawIndexedIndirect);
   NVRM_SET(CmdSetViewport, nvrm_CmdSetViewport);
   NVRM_SET(CmdSetScissor, nvrm_CmdSetScissor);
   NVRM_SET(CmdSetPrimitiveRestartEnable, nvrm_CmdSetPrimitiveRestartEnable);
   NVRM_SET(CmdPushConstants, nvrm_CmdPushConstants);
   NVRM_SET(CmdDispatch, nvrm_CmdDispatch);
   NVRM_SET(CmdDispatchBase, nvrm_CmdDispatchBase);
   NVRM_SET(CmdDispatchIndirect, nvrm_CmdDispatchIndirect);
   NVRM_SET(CmdCopyBuffer2, nvrm_CmdCopyBuffer2);
   NVRM_SET(CmdCopyImage2, nvrm_CmdCopyImage2);
   NVRM_SET(CmdCopyBufferToImage2, nvrm_CmdCopyBufferToImage2);
   NVRM_SET(CmdCopyImageToBuffer2, nvrm_CmdCopyImageToBuffer2);
   NVRM_SET(CmdClearColorImage, nvrm_CmdClearColorImage);
   NVRM_SET(CmdClearAttachments, nvrm_CmdClearAttachments);
   NVRM_SET(CmdSetDepthTestEnable, nvrm_CmdSetDepthTestEnable);
   NVRM_SET(CmdSetDepthWriteEnable, nvrm_CmdSetDepthWriteEnable);
   NVRM_SET(CmdSetDepthCompareOp, nvrm_CmdSetDepthCompareOp);
   NVRM_SET(CmdSetStencilTestEnable, nvrm_CmdSetStencilTestEnable);
   NVRM_SET(CmdSetStencilOp, nvrm_CmdSetStencilOp);
   NVRM_SET(CmdSetStencilCompareMask, nvrm_CmdSetStencilCompareMask);
   NVRM_SET(CmdSetStencilWriteMask, nvrm_CmdSetStencilWriteMask);
   NVRM_SET(CmdSetStencilReference, nvrm_CmdSetStencilReference);
   NVRM_SET(CmdSetCullMode, nvrm_CmdSetCullMode);
   NVRM_SET(CmdSetFrontFace, nvrm_CmdSetFrontFace);
   NVRM_SET(CmdSetDepthBiasEnable, nvrm_CmdSetDepthBiasEnable);
   NVRM_SET(CmdSetDepthBias, nvrm_CmdSetDepthBias);
   NVRM_SET(CmdSetBlendConstants, nvrm_CmdSetBlendConstants);
   NVRM_SET(CmdSetColorWriteEnableEXT, nvrm_CmdSetColorWriteEnableEXT);
   NVRM_SET(CmdSetRasterizerDiscardEnable, nvrm_CmdSetRasterizerDiscardEnable);
   NVRM_SET(CmdSetDepthBoundsTestEnable, nvrm_CmdSetDepthBoundsTestEnable);
   NVRM_SET(CmdSetDepthBounds, nvrm_CmdSetDepthBounds);
   NVRM_SET(CmdSetLogicOpEnableEXT, nvrm_CmdSetLogicOpEnableEXT);
   NVRM_SET(CmdSetLogicOpEXT, nvrm_CmdSetLogicOpEXT);
   NVRM_SET(CmdSetPolygonModeEXT, nvrm_CmdSetPolygonModeEXT);
   NVRM_SET(CmdSetProvokingVertexModeEXT, nvrm_CmdSetProvokingVertexModeEXT);
   NVRM_SET(CmdSetPrimitiveTopology, nvrm_CmdSetPrimitiveTopology);
   NVRM_SET(CmdSetSampleMaskEXT, nvrm_CmdSetSampleMaskEXT);
   NVRM_SET(CmdFillBuffer, nvrm_CmdFillBuffer);
   NVRM_SET(CmdUpdateBuffer, nvrm_CmdUpdateBuffer);
   NVRM_SET(CmdBlitImage2, nvrm_CmdBlitImage2);
   NVRM_SET(CmdResolveImage2, nvrm_CmdResolveImage2);
   NVRM_SET(CmdSetEvent2, nvrm_CmdSetEvent2);
   NVRM_SET(CmdResetEvent2, nvrm_CmdResetEvent2);
   NVRM_SET(CmdWaitEvents2, nvrm_CmdWaitEvents2);
   NVRM_SET(CreateEvent, nvrm_CreateEvent);
   NVRM_SET(DestroyEvent, nvrm_DestroyEvent);
   NVRM_SET(GetEventStatus, nvrm_GetEventStatus);
   NVRM_SET(SetEvent, nvrm_SetEvent);
   NVRM_SET(ResetEvent, nvrm_ResetEvent);
   NVRM_SET(CreateQueryPool, nvrm_CreateQueryPool);
   NVRM_SET(DestroyQueryPool, nvrm_DestroyQueryPool);
   NVRM_SET(GetQueryPoolResults, nvrm_GetQueryPoolResults);
   NVRM_SET(CmdResetQueryPool, nvrm_CmdResetQueryPool);
   NVRM_SET(CmdBeginQuery, nvrm_CmdBeginQuery);
   NVRM_SET(CmdEndQuery, nvrm_CmdEndQuery);
   NVRM_SET(CmdWriteTimestamp2, nvrm_CmdWriteTimestamp2);
   NVRM_SET(CmdSetLineWidth, nvrm_CmdSetLineWidth);
   NVRM_SET(CmdBeginConditionalRenderingEXT, nvrm_CmdBeginConditionalRenderingEXT);
   NVRM_SET(CmdEndConditionalRenderingEXT, nvrm_CmdEndConditionalRenderingEXT);
   NVRM_SET(CmdPipelineBarrier2, nvrm_CmdPipelineBarrier2);
   NVRM_SET(QueueSubmit2, nvrm_QueueSubmit2);
   NVRM_SET(QueuePresentKHR, nvrm_QueuePresent2);
#undef NVRM_SET
}
