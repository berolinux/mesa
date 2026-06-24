/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 */

#include "nvrm_private.h"

#include "vk_common_entrypoints.h"
#include "vk_log.h"

static const struct vk_instance_extension_table instance_extensions = {
   .KHR_get_physical_device_properties2 = true,
   .KHR_external_memory_capabilities = true,
   .KHR_external_semaphore_capabilities = true,
   .KHR_external_fence_capabilities = true,
   .EXT_debug_report = true,
   .EXT_debug_utils = true,
#ifdef VK_USE_PLATFORM_XCB_KHR
   .KHR_xcb_surface = true,
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
   .KHR_wayland_surface = true,
#endif
   .KHR_surface = true,
};

VKAPI_ATTR VkResult VKAPI_CALL
nvrm_CreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator,
                    VkInstance *pInstance)
{
   struct nvrm_instance *instance;
   VkResult result;
   const VkAllocationCallbacks *alloc = pAllocator ? pAllocator
                                                   : vk_default_allocator();

   if (!nv_rm_probe_available())
      return VK_ERROR_INITIALIZATION_FAILED;

   instance = vk_zalloc2(alloc, NULL, sizeof(*instance), 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!instance)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   result = vk_instance_init(&instance->vk, &instance_extensions,
                             &nvrm_instance_entrypoints, pCreateInfo,
                             pAllocator);
   if (result != VK_SUCCESS) {
      vk_free2(alloc, NULL, instance);
      return result;
   }

   instance->vk.physical_devices.enumerate = nvrm_enumerate_physical_devices;
   instance->vk.physical_devices.destroy = nvrm_physical_device_destroy;

   *pInstance = nvrm_instance_to_handle(instance);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
nvrm_DestroyInstance(VkInstance _instance,
                     const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(nvrm_instance, instance, _instance);
   (void)pAllocator;
   if (!instance)
      return;
   vk_instance_finish(&instance->vk);
   vk_free2(&instance->vk.alloc, NULL, instance);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
nvrm_GetInstanceProcAddr(VkInstance _instance, const char *pName)
{
   VK_FROM_HANDLE(nvrm_instance, instance, _instance);
   /* Runtime uses entrypoint tables; without full codegen we fall back to
    * the instance dispatch table / common helpers via unchecked lookup. */
   if (!instance)
      return vk_instance_get_proc_addr_unchecked(NULL, pName);
   return vk_instance_get_proc_addr_unchecked(&instance->vk, pName);
}

PUBLIC VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char *pName)
{
   return nvrm_GetInstanceProcAddr(instance, pName);
}

PUBLIC VKAPI_ATTR VkResult VKAPI_CALL
vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t *pSupportedVersion)
{
   *pSupportedVersion = MIN2(*pSupportedVersion, 6u);
   return VK_SUCCESS;
}
