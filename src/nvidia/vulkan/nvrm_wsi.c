/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * WSI bring-up for nvrm: uses mesa common WSI with nvidia-drm render/primary
 * fd when available.  Swapchain images are allocated as normal VkImages via
 * the device memory path; PRIME blit/scanout refinements come later.
 */

#include "nvrm_wsi.h"

#include "wsi_common.h"

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
nvrm_wsi_proc_addr(VkPhysicalDevice physicalDevice, const char *pName)
{
   VK_FROM_HANDLE(nvrm_physical_device, pdev, physicalDevice);
   return vk_instance_get_proc_addr_unchecked(&pdev->instance->vk, pName);
}

VkResult
nvrm_init_wsi(struct nvrm_physical_device *pdev)
{
   VkResult result;
   int wsi_fd = pdev->drm_fd;
   struct wsi_device_options wsi_options = {
      .sw_device = false,
   };

   /* wsi_device_init wants a DRM fd for modifiers/scanout queries; -1 is ok
    * for headless / pure RM bring-up (swapchain may still work via prime). */
   result = wsi_device_init(&pdev->wsi_device,
                            nvrm_physical_device_to_handle(pdev),
                            nvrm_wsi_proc_addr,
                            &pdev->instance->vk.alloc,
                            wsi_fd,
                            NULL, /* dri options — none registered yet */
                            &wsi_options);
   if (result != VK_SUCCESS)
      return result;

   pdev->wsi_device.supports_scanout = false;
   pdev->wsi_device.supports_modifiers = false;
   pdev->vk.wsi_device = &pdev->wsi_device;
   return VK_SUCCESS;
}

void
nvrm_finish_wsi(struct nvrm_physical_device *pdev)
{
   if (!pdev)
      return;
   pdev->vk.wsi_device = NULL;
   wsi_device_finish(&pdev->wsi_device, &pdev->instance->vk.alloc);
}
