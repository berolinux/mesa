/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Hand-maintained dispatch tables until vk_entrypoints_gen is wired for nvrm.
 * Mesa runtime uses vk_*_dispatch_table; entrypoint_table is a separate type
 * used only by GetInstanceProcAddr with full codegen.
 */

#include "nvrm_private.h"

/* Instance / physical / device dispatch — field names match runtime tables.
 * Command buffer / queue tables are owned by vk_runtime generated headers;
 * we keep zero-initialized placeholders via weak symbols only if types exist.
 * For bring-up we register only instance/physical/device fields we implement.
 */

struct vk_instance_dispatch_table nvrm_instance_entrypoints;
struct vk_physical_device_dispatch_table nvrm_physical_device_entrypoints;
struct vk_device_dispatch_table nvrm_device_entrypoints;

/* cmd/queue entrypoints are stored on command buffers / queues via
 * vk_command_buffer_init / vk_queue_init using device dispatch; expose empty
 * tables only if the type is complete (included via private.h chain). */
#if defined(__has_include)
/* Always define as zeroed objects; if type incomplete, this file won't compile
 * and we fall back to omitting — include vk_command_buffer.h already in private.
 */
#endif

/* Minimal: only tables that compile without generated cmd_queue entrypoints */
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

   /* Device-level methods may live only in entrypoint tables from codegen;
    * assign through device dispatch when fields exist (Mesa tip varies). */
#ifdef NVRM_HAS_DEVICE_DISPATCH_CREATE
   nvrm_device_entrypoints.CreateDevice = nvrm_CreateDevice;
#endif
}
