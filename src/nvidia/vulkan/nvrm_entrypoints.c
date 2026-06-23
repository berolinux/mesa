/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Minimal dispatch tables.  Will be replaced by vk_entrypoints_gen output
 * covering the full Vulkan 1.3 + extensions set.
 */

#include "nvrm_private.h"
#include "vk_common_entrypoints.h"
#include "vk_device_entrypoint_stubs.h"
#include "vk_instance_entrypoint_stubs.h"
#include "vk_physical_device_entrypoint_stubs.h"
#include "vk_queue_entrypoint_stubs.h"
#include "vk_cmd_queue_entrypoints.h"

/* Weak/empty tables - vk_runtime fills gaps via common entrypoints.
 * Hand-implemented entrypoints are declared in the .c files with nvrm_ prefix
 * and must be registered here as we add them.
 *
 * For bring-up we rely on vk_instance_init / vk_device_init accepting our
 * partial tables; unimplemented entrypoints return NULL / stub errors.
 */

struct vk_instance_dispatch_table nvrm_instance_entrypoints = { 0 };
struct vk_physical_device_dispatch_table nvrm_physical_device_entrypoints = { 0 };
struct vk_device_dispatch_table nvrm_device_entrypoints = { 0 };
struct vk_command_buffer_dispatch_table nvrm_cmd_buffer_entrypoints = { 0 };
struct vk_queue_dispatch_table nvrm_queue_entrypoints = { 0 };

/* Constructor to patch in the entrypoints we implement by hand */
static void __attribute__((constructor))
nvrm_init_entrypoints(void)
{
   /* Instance */
   nvrm_instance_entrypoints.CreateInstance = nvrm_CreateInstance;
   nvrm_instance_entrypoints.DestroyInstance = nvrm_DestroyInstance;
   nvrm_instance_entrypoints.GetInstanceProcAddr = nvrm_GetInstanceProcAddr;

   /* Physical device */
   nvrm_physical_device_entrypoints.GetPhysicalDeviceProperties2 =
      nvrm_GetPhysicalDeviceProperties2;
   nvrm_physical_device_entrypoints.GetPhysicalDeviceQueueFamilyProperties2 =
      nvrm_GetPhysicalDeviceQueueFamilyProperties2;
   nvrm_physical_device_entrypoints.GetPhysicalDeviceMemoryProperties2 =
      nvrm_GetPhysicalDeviceMemoryProperties2;

   /* Device */
   nvrm_device_entrypoints.CreateDevice = nvrm_CreateDevice;
   nvrm_device_entrypoints.DestroyDevice = nvrm_DestroyDevice;
   nvrm_device_entrypoints.GetDeviceQueue2 = nvrm_GetDeviceQueue2;
   nvrm_device_entrypoints.AllocateMemory = nvrm_AllocateMemory;
   nvrm_device_entrypoints.FreeMemory = nvrm_FreeMemory;
   nvrm_device_entrypoints.MapMemory = nvrm_MapMemory;
   nvrm_device_entrypoints.UnmapMemory = nvrm_UnmapMemory;

   /* Command buffer */
   nvrm_cmd_buffer_entrypoints.BeginCommandBuffer = nvrm_BeginCommandBuffer;
   nvrm_cmd_buffer_entrypoints.EndCommandBuffer = nvrm_EndCommandBuffer;
   nvrm_cmd_buffer_entrypoints.CmdPipelineBarrier2 = nvrm_CmdPipelineBarrier2;
   nvrm_cmd_buffer_entrypoints.CmdClearColorImage = nvrm_CmdClearColorImage;
   nvrm_cmd_buffer_entrypoints.CmdCopyBuffer2 = nvrm_CmdCopyBuffer2;
}
