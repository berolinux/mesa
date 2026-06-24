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
   NVRM_SET(CmdDispatch, nvrm_CmdDispatch);
   NVRM_SET(CmdDispatchBase, nvrm_CmdDispatchBase);
   NVRM_SET(CmdCopyBuffer2, nvrm_CmdCopyBuffer2);
   NVRM_SET(CmdCopyImage2, nvrm_CmdCopyImage2);
   NVRM_SET(CmdCopyBufferToImage2, nvrm_CmdCopyBufferToImage2);
   NVRM_SET(CmdCopyImageToBuffer2, nvrm_CmdCopyImageToBuffer2);
   NVRM_SET(CmdClearColorImage, nvrm_CmdClearColorImage);
   NVRM_SET(CmdPipelineBarrier2, nvrm_CmdPipelineBarrier2);
   NVRM_SET(QueueSubmit2, nvrm_QueueSubmit2);
   NVRM_SET(QueuePresentKHR, nvrm_QueuePresent2);
#undef NVRM_SET
}
