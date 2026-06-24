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
   props->limits.lineWidthRange[0] = 1.0f;
   props->limits.lineWidthRange[1] = 64.0f;
   props->limits.lineWidthGranularity = 0.125f;
   props->limits.pointSizeRange[0] = 1.0f;
   props->limits.pointSizeRange[1] = 2047.0f;
   props->limits.pointSizeGranularity = 0.125f;
   props->limits.strictLines = VK_FALSE;
   props->limits.standardSampleLocations = VK_TRUE;
   props->limits.optimalBufferCopyOffsetAlignment = 1;
   props->limits.optimalBufferCopyRowPitchAlignment = 1;
   props->limits.nonCoherentAtomSize = 64;
   props->limits.maxDrawIndexedIndexValue = 0xffffffffu;
   props->limits.maxDrawIndirectCount = 0xffffffffu;
   props->limits.maxSamplerAllocationCount = 4000;
   props->limits.subPixelPrecisionBits = 8;
   props->limits.subTexelPrecisionBits = 8;
   props->limits.mipmapPrecisionBits = 8;
   props->limits.maxTexelBufferElements = 1u << 27;
}

static void
nvrm_fill_physical_device_features(VkPhysicalDeviceFeatures *f)
{
   memset(f, 0, sizeof(*f));
   /* Core 1.0 features supported by nvrm bring-up path (conservative + real). */
   f->robustBufferAccess = VK_TRUE;
   f->fullDrawIndexUint32 = VK_TRUE;
   f->imageCubeArray = VK_TRUE;
   f->independentBlend = VK_TRUE;
   f->geometryShader = VK_FALSE; /* GS path not yet wired in pipeline */
   f->tessellationShader = VK_FALSE;
   f->sampleRateShading = VK_TRUE;
   f->dualSrcBlend = VK_TRUE;
   f->logicOp = VK_TRUE;
   f->multiDrawIndirect = VK_TRUE;
   f->drawIndirectFirstInstance = VK_TRUE;
   f->depthClamp = VK_TRUE;
   f->depthBiasClamp = VK_TRUE;
   f->fillModeNonSolid = VK_TRUE;
   f->depthBounds = VK_TRUE;
   f->wideLines = VK_TRUE;
   f->largePoints = VK_TRUE;
   f->alphaToOne = VK_TRUE;
   f->multiViewport = VK_TRUE;
   f->samplerAnisotropy = VK_TRUE;
   f->textureCompressionETC2 = VK_FALSE;
   f->textureCompressionASTC_LDR = VK_FALSE;
   f->textureCompressionBC = VK_TRUE;
   f->occlusionQueryPrecise = VK_TRUE;
   f->pipelineStatisticsQuery = VK_TRUE;
   f->vertexPipelineStoresAndAtomics = VK_TRUE;
   f->fragmentStoresAndAtomics = VK_TRUE;
   f->shaderTessellationAndGeometryPointSize = VK_FALSE;
   f->shaderImageGatherExtended = VK_TRUE;
   f->shaderStorageImageExtendedFormats = VK_TRUE;
   f->shaderStorageImageMultisample = VK_FALSE;
   f->shaderStorageImageReadWithoutFormat = VK_TRUE;
   f->shaderStorageImageWriteWithoutFormat = VK_TRUE;
   f->shaderUniformBufferArrayDynamicIndexing = VK_TRUE;
   f->shaderSampledImageArrayDynamicIndexing = VK_TRUE;
   f->shaderStorageBufferArrayDynamicIndexing = VK_TRUE;
   f->shaderStorageImageArrayDynamicIndexing = VK_TRUE;
   f->shaderClipDistance = VK_TRUE;
   f->shaderCullDistance = VK_TRUE;
   f->shaderFloat64 = VK_TRUE;
   f->shaderInt64 = VK_TRUE;
   f->shaderInt16 = VK_TRUE;
   f->shaderResourceResidency = VK_FALSE;
   f->shaderResourceMinLod = VK_TRUE;
   f->sparseBinding = VK_FALSE;
   f->sparseResidencyBuffer = VK_FALSE;
   f->sparseResidencyImage2D = VK_FALSE;
   f->sparseResidencyImage3D = VK_FALSE;
   f->sparseResidency2Samples = VK_FALSE;
   f->sparseResidency4Samples = VK_FALSE;
   f->sparseResidency8Samples = VK_FALSE;
   f->sparseResidency16Samples = VK_FALSE;
   f->sparseResidencyAliased = VK_FALSE;
   f->variableMultisampleRate = VK_TRUE;
   f->inheritedQueries = VK_TRUE;
}

VKAPI_ATTR void VKAPI_CALL
nvrm_GetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice,
                                VkPhysicalDeviceFeatures2 *pFeatures)
{
   (void)physicalDevice;
   if (!pFeatures)
      return;
   nvrm_fill_physical_device_features(&pFeatures->features);

   /* Walk pNext for common feature structs we can advertise. */
   void *pnext = pFeatures->pNext;
   while (pnext) {
      VkBaseOutStructure *base = pnext;
      switch (base->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES: {
         VkPhysicalDeviceVulkan11Features *f11 =
            (VkPhysicalDeviceVulkan11Features *)base;
         f11->storageBuffer16BitAccess = VK_TRUE;
         f11->uniformAndStorageBuffer16BitAccess = VK_TRUE;
         f11->storagePushConstant16 = VK_TRUE;
         f11->multiview = VK_FALSE;
         f11->variablePointersStorageBuffer = VK_TRUE;
         f11->variablePointers = VK_TRUE;
         f11->shaderDrawParameters = VK_TRUE;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES: {
         VkPhysicalDeviceVulkan12Features *f12 =
            (VkPhysicalDeviceVulkan12Features *)base;
         f12->samplerMirrorClampToEdge = VK_TRUE;
         f12->drawIndirectCount = VK_TRUE;
         f12->storageBuffer8BitAccess = VK_TRUE;
         f12->uniformAndStorageBuffer8BitAccess = VK_TRUE;
         f12->shaderInt8 = VK_TRUE;
         f12->shaderFloat16 = VK_TRUE;
         f12->descriptorIndexing = VK_FALSE;
         f12->timelineSemaphore = VK_TRUE;
         f12->bufferDeviceAddress = VK_FALSE;
         f12->vulkanMemoryModel = VK_TRUE;
         f12->hostQueryReset = VK_TRUE;
         f12->scalarBlockLayout = VK_TRUE;
         f12->separateDepthStencilLayouts = VK_TRUE;
         f12->hostQueryReset = VK_TRUE;
         f12->subgroupBroadcastDynamicId = VK_TRUE;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES: {
         VkPhysicalDeviceVulkan13Features *f13 =
            (VkPhysicalDeviceVulkan13Features *)base;
         f13->dynamicRendering = VK_TRUE;
         f13->shaderDemoteToHelperInvocation = VK_TRUE;
         f13->shaderTerminateInvocation = VK_TRUE;
         f13->subgroupSizeControl = VK_TRUE;
         f13->computeFullSubgroups = VK_TRUE;
         f13->synchronization2 = VK_TRUE;
         f13->maintenance4 = VK_TRUE;
         f13->pipelineCreationCacheControl = VK_TRUE;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT: {
         /* optional; leave defaults if type not in headers at compile */
         break;
      }
      default:
         break;
      }
      pnext = base->pNext;
   }
}

VKAPI_ATTR void VKAPI_CALL
nvrm_GetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice,
                               VkPhysicalDeviceFeatures *pFeatures)
{
   (void)physicalDevice;
   if (pFeatures)
      nvrm_fill_physical_device_features(pFeatures);
}

static VkFormatFeatureFlags2
nvrm_format_feature_flags(VkFormat format, bool optimal)
{
   VkFormatFeatureFlags2 f = 0;
   (void)optimal;

   switch (format) {
   case VK_FORMAT_R8G8B8A8_UNORM:
   case VK_FORMAT_R8G8B8A8_SRGB:
   case VK_FORMAT_B8G8R8A8_UNORM:
   case VK_FORMAT_B8G8R8A8_SRGB:
   case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
   case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
   case VK_FORMAT_R8_UNORM:
   case VK_FORMAT_R8G8_UNORM:
   case VK_FORMAT_R16_UNORM:
   case VK_FORMAT_R16G16_UNORM:
   case VK_FORMAT_R16G16B16A16_UNORM:
   case VK_FORMAT_R16G16B16A16_SFLOAT:
   case VK_FORMAT_R32_SFLOAT:
   case VK_FORMAT_R32G32_SFLOAT:
   case VK_FORMAT_R32G32B32A32_SFLOAT:
   case VK_FORMAT_R32_UINT:
   case VK_FORMAT_R32_SINT:
   case VK_FORMAT_R32G32B32A32_UINT:
   case VK_FORMAT_R16_SFLOAT:
   case VK_FORMAT_R16G16_SFLOAT:
   case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
   case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
   case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
      f = VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT |
          VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT |
          VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT |
          VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BLEND_BIT |
          VK_FORMAT_FEATURE_2_BLIT_SRC_BIT |
          VK_FORMAT_FEATURE_2_BLIT_DST_BIT |
          VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT |
          VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT |
          VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
      break;
   case VK_FORMAT_D16_UNORM:
   case VK_FORMAT_D32_SFLOAT:
   case VK_FORMAT_D24_UNORM_S8_UINT:
   case VK_FORMAT_D32_SFLOAT_S8_UINT:
   case VK_FORMAT_S8_UINT:
      f = VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT |
          VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT |
          VK_FORMAT_FEATURE_2_BLIT_SRC_BIT |
          VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT |
          VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT;
      break;
   case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
   case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
   case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
   case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
   case VK_FORMAT_BC2_UNORM_BLOCK:
   case VK_FORMAT_BC2_SRGB_BLOCK:
   case VK_FORMAT_BC3_UNORM_BLOCK:
   case VK_FORMAT_BC3_SRGB_BLOCK:
   case VK_FORMAT_BC4_UNORM_BLOCK:
   case VK_FORMAT_BC5_UNORM_BLOCK:
   case VK_FORMAT_BC6H_UFLOAT_BLOCK:
   case VK_FORMAT_BC6H_SFLOAT_BLOCK:
   case VK_FORMAT_BC7_UNORM_BLOCK:
   case VK_FORMAT_BC7_SRGB_BLOCK:
      f = VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT |
          VK_FORMAT_FEATURE_2_BLIT_SRC_BIT |
          VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT |
          VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT |
          VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
      break;
   case VK_FORMAT_R8_UINT:
   case VK_FORMAT_R8G8_UINT:
   case VK_FORMAT_R8G8B8A8_UINT:
   case VK_FORMAT_R16_UINT:
   case VK_FORMAT_R16G16B16A16_UINT:
   case VK_FORMAT_R8_SINT:
   case VK_FORMAT_R8G8B8A8_SINT:
      f = VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT |
          VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT |
          VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT |
          VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT |
          VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT |
          VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_BIT |
          VK_FORMAT_FEATURE_2_UNIFORM_TEXEL_BUFFER_BIT;
      break;
   default:
      /* Unknown: still allow transfer so copies/validation paths work. */
      f = VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT |
          VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT;
      break;
   }
   return f;
}

VKAPI_ATTR void VKAPI_CALL
nvrm_GetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice,
                                        VkFormat format,
                                        VkFormatProperties2 *pFormatProperties)
{
   (void)physicalDevice;
   VkFormatFeatureFlags2 lin = nvrm_format_feature_flags(format, false);
   VkFormatFeatureFlags2 opt = nvrm_format_feature_flags(format, true);
   VkFormatFeatureFlags2 buf = 0;

   if (lin & VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT)
      buf |= VK_FORMAT_FEATURE_2_UNIFORM_TEXEL_BUFFER_BIT |
             VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_BIT |
             VK_FORMAT_FEATURE_2_VERTEX_BUFFER_BIT;

   if (!pFormatProperties)
      return;

   pFormatProperties->formatProperties.linearTilingFeatures =
      (VkFormatFeatureFlags)lin;
   pFormatProperties->formatProperties.optimalTilingFeatures =
      (VkFormatFeatureFlags)opt;
   pFormatProperties->formatProperties.bufferFeatures =
      (VkFormatFeatureFlags)buf;

   void *pnext = pFormatProperties->pNext;
   while (pnext) {
      VkBaseOutStructure *base = pnext;
      if (base->sType == VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3) {
         VkFormatProperties3 *fp3 = (VkFormatProperties3 *)base;
         fp3->linearTilingFeatures = lin;
         fp3->optimalTilingFeatures = opt;
         fp3->bufferFeatures = buf;
      }
      pnext = base->pNext;
   }
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
