/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * WSI for nvrm: mesa common WSI on nvidia-drm render node when available.
 * Swapchain images are normal VkImages (OPTIMAL/blocklinear).  Present goes
 * through common WSI (PRIME / wl_drm / x11); scanout via nvidia-drm modifiers
 * is enabled when we have a real DRM fd.
 */

#include "nvrm_wsi.h"
#include "nv_channel.h"

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

   /* With a real nvidia-drm fd, allow modifier/scanout queries in common WSI.
    * Without fd (pure RM), keep PRIME-only path. */
   if (wsi_fd >= 0) {
      pdev->wsi_device.supports_scanout = true;
      pdev->wsi_device.supports_modifiers = true;
   } else {
      pdev->wsi_device.supports_scanout = false;
      pdev->wsi_device.supports_modifiers = false;
   }
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

/* QueuePresent2: delegate to common WSI when swapchain is WSI-backed. */
VKAPI_ATTR VkResult VKAPI_CALL
nvrm_QueuePresent2(VkQueue _queue, const VkPresentInfoKHR *pPresentInfo)
{
   VK_FROM_HANDLE(nvrm_queue, queue, _queue);
   struct nvrm_device *device;

   if (!queue || !pPresentInfo)
      return VK_ERROR_INITIALIZATION_FAILED;

   device = queue->device;
   if (!device || !device->physical || !device->physical->vk.wsi_device)
      return VK_ERROR_SURFACE_LOST_KHR;

   /* Ensure prior submits are flushed before present (semaphore waits are
    * handled by common WSI / application; we only need channel progress). */
   if (queue->channel_ready && queue->channel)
      nv_channel_kickoff(queue->channel);

   return wsi_common_queue_present(device->physical->vk.wsi_device,
                                   &queue->vk, pPresentInfo);
}
