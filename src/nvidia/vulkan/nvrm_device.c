/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 */

#include "nvrm_private.h"
#include "nvrm_wsi.h"
#include "nv_tex.h"

#include "vk_common_entrypoints.h"
#include "vk_util.h"

#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>

VkResult
nvrm_enumerate_physical_devices(struct vk_instance *vk_instance)
{
   struct nvrm_instance *instance =
      container_of(vk_instance, struct nvrm_instance, vk);
   int gpu_count;
   int i;

   gpu_count = nv_rm_probe_gpu_count();
   if (gpu_count <= 0)
      return VK_SUCCESS; /* no devices, not an error */

   for (i = 0; i < gpu_count; i++) {
      struct nvrm_physical_device *pdev;
      struct nv_rm_device *rm;
      int drm_fd = -1;

      /* Try to open nvidia-drm render node; fall back to pure RM */
      drm_fd = drmOpenWithType("nvidia-drm", NULL, DRM_NODE_RENDER);
      if (drm_fd < 0)
         drm_fd = -1;

      rm = nv_rm_device_open(drm_fd, i);
      if (!rm) {
         if (drm_fd >= 0)
            close(drm_fd);
         continue;
      }

      pdev = vk_zalloc(&instance->vk.alloc, sizeof(*pdev), 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
      if (!pdev) {
         nv_rm_device_close(rm);
         if (drm_fd >= 0)
            close(drm_fd);
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }

      pdev->instance = instance;
      pdev->rm = rm;
      pdev->info = nv_rm_device_info(rm);
      pdev->drm_fd = drm_fd;
      pdev->gpu_index = i;

      vk_physical_device_init(&pdev->vk, &instance->vk,
                             NULL, NULL, NULL,
                             &nvrm_physical_device_entrypoints);

      /* WSI is optional at bring-up; failure leaves a non-WSI physical device */
      if (nvrm_init_wsi(pdev) != VK_SUCCESS)
         pdev->vk.wsi_device = NULL;

      list_addtail(&pdev->vk.link, &instance->vk.physical_devices.list);
   }

   return VK_SUCCESS;
}

void
nvrm_physical_device_destroy(struct vk_physical_device *vk_pdev)
{
   struct nvrm_physical_device *pdev =
      container_of(vk_pdev, struct nvrm_physical_device, vk);

   nvrm_finish_wsi(pdev);
   if (pdev->rm)
      nv_rm_device_close(pdev->rm);
   if (pdev->drm_fd >= 0)
      close(pdev->drm_fd);
   vk_physical_device_finish(&pdev->vk);
   vk_free(&pdev->instance->vk.alloc, pdev);
}

static void
nvrm_get_physical_device_properties2(struct vk_physical_device *vk_pdev,
                                     VkPhysicalDeviceProperties2 *pProperties)
{
   struct nvrm_physical_device *pdev =
      container_of(vk_pdev, struct nvrm_physical_device, vk);
   const struct nv_device_info *info = pdev->info;
   VkPhysicalDeviceProperties *props = &pProperties->properties;

   memset(props, 0, sizeof(*props));
   props->apiVersion = NVRM_API_VERSION;
   props->driverVersion = vk_get_driver_version();
   props->vendorID = 0x10de;
   props->deviceID = info ? info->pci_device_id : 0;
   props->deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;

   if (info && info->name[0])
      snprintf(props->deviceName, sizeof(props->deviceName), "%s", info->name);
   else
      snprintf(props->deviceName, sizeof(props->deviceName), "NVIDIA GPU (nvrm)");

   props->limits.maxImageDimension1D = 16384;
   props->limits.maxImageDimension2D = 16384;
   props->limits.maxImageDimension3D = 2048;
   props->limits.maxImageDimensionCube = 16384;
   props->limits.maxImageArrayLayers = 2048;
   props->limits.maxBoundDescriptorSets = 8;
   props->limits.maxPerStageDescriptorSamplers = 32;
   props->limits.maxPerStageDescriptorUniformBuffers = 16;
   props->limits.maxPerStageDescriptorStorageBuffers = 16;
   props->limits.maxPerStageDescriptorSampledImages = 128;
   props->limits.maxPerStageDescriptorStorageImages = 16;
   props->limits.maxPerStageResources = 256;
   props->limits.maxDescriptorSetSamplers = 96;
   props->limits.maxDescriptorSetUniformBuffers = 96;
   props->limits.maxDescriptorSetStorageBuffers = 96;
   props->limits.maxDescriptorSetSampledImages = 256;
   props->limits.maxDescriptorSetStorageImages = 32;
   props->limits.maxVertexInputAttributes = 32;
   props->limits.maxVertexInputBindings = 32;
   props->limits.maxVertexInputAttributeOffset = 2047;
   props->limits.maxVertexInputBindingStride = 2048;
   props->limits.maxVertexOutputComponents = 128;
   props->limits.maxFragmentInputComponents = 128;
   props->limits.maxFragmentOutputAttachments = 8;
   props->limits.maxComputeSharedMemorySize = 48 * 1024;
   props->limits.maxComputeWorkGroupCount[0] = 2147483647;
   props->limits.maxComputeWorkGroupCount[1] = 65535;
   props->limits.maxComputeWorkGroupCount[2] = 65535;
   props->limits.maxComputeWorkGroupInvocations = 1024;
   props->limits.maxComputeWorkGroupSize[0] = 1024;
   props->limits.maxComputeWorkGroupSize[1] = 1024;
   props->limits.maxComputeWorkGroupSize[2] = 64;
   props->limits.timestampComputeAndGraphics = VK_TRUE;
   props->limits.timestampPeriod = 1.0f;
   props->limits.nonCoherentAtomSize = 64;
   props->limits.minMemoryMapAlignment = 64;
   props->limits.bufferImageGranularity = 64;
   props->limits.sparseAddressSpaceSize = 0;
   props->limits.maxPushConstantsSize = 256;
   props->limits.maxMemoryAllocationCount = 4096;
   props->limits.maxSamplerAllocationCount = 4000;
   props->limits.maxUniformBufferRange = 65536;
   props->limits.maxStorageBufferRange = 1u << 27;
   props->limits.minUniformBufferOffsetAlignment = 64;
   props->limits.minStorageBufferOffsetAlignment = 16;
   props->limits.minTexelBufferOffsetAlignment = 16;
   props->limits.maxSamplerLodBias = 15.0f;
   props->limits.maxSamplerAnisotropy = 16.0f;
   props->limits.maxViewports = 16;
   props->limits.maxViewportDimensions[0] = 16384;
   props->limits.maxViewportDimensions[1] = 16384;
   props->limits.viewportBoundsRange[0] = -32768.0f;
   props->limits.viewportBoundsRange[1] = 32767.0f;
   props->limits.viewportSubPixelBits = 8;
   props->limits.maxFramebufferWidth = 16384;
   props->limits.maxFramebufferHeight = 16384;
   props->limits.maxFramebufferLayers = 2048;
   props->limits.framebufferColorSampleCounts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;
   props->limits.framebufferDepthSampleCounts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;
   props->limits.framebufferStencilSampleCounts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;
   props->limits.maxColorAttachments = 8;
   props->limits.sampledImageColorSampleCounts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;
   props->limits.storageImageSampleCounts = VK_SAMPLE_COUNT_1_BIT;
   props->limits.maxSampleMaskWords = 1;
   props->limits.maxClipDistances = 8;
   props->limits.maxCullDistances = 8;
   props->limits.maxCombinedClipAndCullDistances = 8;
}

VKAPI_ATTR void VKAPI_CALL
nvrm_GetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice,
                                  VkPhysicalDeviceProperties2 *pProperties)
{
   VK_FROM_HANDLE(nvrm_physical_device, pdev, physicalDevice);
   nvrm_get_physical_device_properties2(&pdev->vk, pProperties);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_GetPhysicalDeviceQueueFamilyProperties2(
   VkPhysicalDevice physicalDevice,
   uint32_t *pCount,
   VkQueueFamilyProperties2 *pQueueFamilyProperties)
{
   VK_OUTARRAY_MAKE_TYPED(VkQueueFamilyProperties2, out,
                          pQueueFamilyProperties, pCount);
   vk_outarray_append_typed(VkQueueFamilyProperties2, &out, p) {
      p->queueFamilyProperties.queueFlags =
         VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
      p->queueFamilyProperties.queueCount = 1;
      p->queueFamilyProperties.timestampValidBits = 64;
      p->queueFamilyProperties.minImageTransferGranularity = (VkExtent3D){1, 1, 1};
   }
}

VKAPI_ATTR void VKAPI_CALL
nvrm_GetPhysicalDeviceMemoryProperties2(
   VkPhysicalDevice physicalDevice,
   VkPhysicalDeviceMemoryProperties2 *pMemoryProperties)
{
   VK_FROM_HANDLE(nvrm_physical_device, pdev, physicalDevice);
   const struct nv_device_info *info = pdev->info;
   VkPhysicalDeviceMemoryProperties *mem = &pMemoryProperties->memoryProperties;
   VkDeviceSize vram = info ? info->vram_size_bytes : (1ull << 30);

   memset(mem, 0, sizeof(*mem));
   mem->memoryHeapCount = 2;
   mem->memoryHeaps[0].size = vram;
   mem->memoryHeaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
   mem->memoryHeaps[1].size = 4ull << 30; /* report 4GB host-visible window */
   mem->memoryHeaps[1].flags = 0;

   mem->memoryTypeCount = 2;
   mem->memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
   mem->memoryTypes[0].heapIndex = 0;
   mem->memoryTypes[1].propertyFlags =
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
   mem->memoryTypes[1].heapIndex = 1;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvrm_CreateDevice(VkPhysicalDevice physicalDevice,
                  const VkDeviceCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator,
                  VkDevice *pDevice)
{
   VK_FROM_HANDLE(nvrm_physical_device, pdev, physicalDevice);
   struct nvrm_device *device;
   VkResult result;

   device = vk_zalloc2(&pdev->vk.instance->alloc, pAllocator,
                       sizeof(*device), 8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!device)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   device->physical = pdev;
   device->rm = pdev->rm;
   device->info = pdev->info;

   result = vk_device_init(&device->vk, &pdev->vk, NULL,
                          pCreateInfo, pAllocator);
   if (result == VK_SUCCESS)
      device->vk.dispatch_table = nvrm_device_entrypoints;
   if (result != VK_SUCCESS) {
      vk_free2(&pdev->vk.instance->alloc, pAllocator, device);
      return result;
   }

   device->queue = vk_zalloc(&device->vk.alloc, sizeof(*device->queue), 8,
                             VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!device->queue) {
      vk_device_finish(&device->vk);
      vk_free2(&pdev->vk.instance->alloc, pAllocator, device);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   result = nvrm_queue_init(device, device->queue);
   if (result != VK_SUCCESS) {
      vk_free(&device->vk.alloc, device->queue);
      vk_device_finish(&device->vk);
      vk_free2(&pdev->vk.instance->alloc, pAllocator, device);
      return result;
   }

   *pDevice = nvrm_device_to_handle(device);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
nvrm_DestroyDevice(VkDevice _device, const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(nvrm_device, device, _device);
   if (!device)
      return;
   if (device->queue) {
      if (device->tex_pool) {
      nv_tex_pool_destroy(device->tex_pool);
      device->tex_pool = NULL;
   }
   nvrm_queue_finish(device->queue);
      vk_free(&device->vk.alloc, device->queue);
   }
   vk_device_finish(&device->vk);
   vk_free2(&device->vk.alloc, pAllocator, device);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_GetDeviceQueue2(VkDevice _device, const VkDeviceQueueInfo2 *pQueueInfo,
                     VkQueue *pQueue)
{
   VK_FROM_HANDLE(nvrm_device, device, _device);
   (void)pQueueInfo;
   *pQueue = nvrm_queue_to_handle(device->queue);
}

static VkResult
nvrm_queue_submit(struct vk_queue *vk_queue,
                  struct vk_queue_submit *submit)
{
   struct nvrm_queue *queue =
      container_of(vk_queue, struct nvrm_queue, vk);
   struct nv_channel *ch = queue->channel;
   uint32_t i, j;

   if (!ch || !ch->push_cpu)
      return VK_ERROR_DEVICE_LOST;

   /* Replay each command buffer's recorded push dwords into the channel
    * pushbuffer and kick GPFIFO.  Command buffers store a linear method stream
    * in push_map; we append and kick once per submit (or when space is tight). */
   for (i = 0; i < submit->command_buffer_count; i++) {
      struct nvrm_cmd_buffer *cmd =
         container_of(submit->command_buffers[i], struct nvrm_cmd_buffer, vk);
      uint32_t *dst;
      uint32_t need;

      if (!cmd || !cmd->push_map || cmd->push_dw_used == 0)
         continue;

      need = cmd->push_dw_used;
      dst = nv_channel_push_begin(ch, need + 8);
      if (!dst)
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;

      for (j = 0; j < need; j++)
         dst[j] = cmd->push_map[j];
      ch->push_dw_used = ch->push_dw_base + need;

      if (nv_channel_kickoff(ch) != 0)
         return VK_ERROR_DEVICE_LOST;
   }

   /* Wait for GPU to drain GPFIFO when not deferred (simple host sync). */
   if (!(submit->wait_count == 0 && submit->signal_count == 0))
      (void)nv_channel_wait_idle(ch, 500000000ull); /* 500ms */

   return VK_SUCCESS;
}

VkResult
nvrm_queue_init(struct nvrm_device *device, struct nvrm_queue *queue)
{
   VkResult result;
   struct nv_channel *ch;

   queue->device = device;
   queue->channel = NULL;
   queue->h_channel = 0;
   queue->h_gpfifo_mem = 0;
   queue->channel_ready = false;

   result = vk_queue_init(&queue->vk, &device->vk, NULL, 0 /* queue_family_index */);
   if (result != VK_SUCCESS)
      return result;

   /* Graphics GPFIFO channel via shared nv_channel (VASpace + doorbell included) */
   if (device->rm) {
      ch = nv_channel_create(device->rm, 0 /* default GRAPHICS engine */,
                             NV_CHANNEL_DEFAULT_GPFIFO_ENTRIES,
                             NV_CHANNEL_DEFAULT_PUSH_DWORDS);
      if (ch) {
         queue->channel = ch;
         queue->h_channel = ch->h_channel;
         queue->h_gpfifo_mem = ch->h_gpfifo_mem;
         queue->channel_ready = true;
         queue->vk.driver_submit = nvrm_queue_submit;
      }
   }

   /* Channel failure is non-fatal at device create time: API still loads;
    * submits will fail with DEVICE_LOST until channel works. */
   return VK_SUCCESS;
}

void
nvrm_queue_finish(struct nvrm_queue *queue)
{
   if (queue->channel) {
      nv_channel_destroy(queue->channel);
      queue->channel = NULL;
      queue->h_channel = 0;
      queue->channel_ready = false;
   }
   vk_queue_finish(&queue->vk);
}
