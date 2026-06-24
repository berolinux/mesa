/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 */

#include "nvrm_private.h"

VKAPI_ATTR VkResult VKAPI_CALL
nvrm_AllocateMemory(VkDevice _device,
                    const VkMemoryAllocateInfo *pAllocateInfo,
                    const VkAllocationCallbacks *pAllocator,
                    VkDeviceMemory *pMem)
{
   VK_FROM_HANDLE(nvrm_device, device, _device);
   struct nvrm_device_memory *mem;
   struct nv_rm_bo_req req;
   bool is_vram = true;

   if (pAllocateInfo->memoryTypeIndex == 1)
      is_vram = false;

   mem = vk_device_memory_create(&device->vk, pAllocateInfo, pAllocator,
                                 sizeof(*mem));
   if (!mem)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   memset(&req, 0, sizeof(req));
   req.size = pAllocateInfo->allocationSize;
   req.alignment = 4096;
   req.vram = is_vram;
   req.cpu_access = !is_vram;
   req.no_scanout = true;

   mem->bo = nv_rm_bo_alloc(device->rm, &req);
   if (!mem->bo && is_vram) {
      req.vram = false;
      req.cpu_access = true;
      mem->bo = nv_rm_bo_alloc(device->rm, &req);
      is_vram = false;
   }
   if (!mem->bo) {
      vk_device_memory_destroy(&device->vk, pAllocator, &mem->vk);
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   }

   mem->size = pAllocateInfo->allocationSize;
   mem->is_vram = is_vram;
   mem->map = NULL;
   *pMem = nvrm_device_memory_to_handle(mem);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
nvrm_FreeMemory(VkDevice _device, VkDeviceMemory _mem,
                const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(nvrm_device, device, _device);
   VK_FROM_HANDLE(nvrm_device_memory, mem, _mem);
   if (!mem)
      return;
   if (mem->map) {
      nv_rm_bo_unmap(mem->bo);
      mem->map = NULL;
   }
   if (mem->bo)
      nv_rm_bo_free(mem->bo);
   vk_device_memory_destroy(&device->vk, pAllocator, &mem->vk);
}

VKAPI_ATTR VkResult VKAPI_CALL
nvrm_MapMemory(VkDevice _device, VkDeviceMemory _memory, VkDeviceSize offset,
               VkDeviceSize size, VkMemoryMapFlags flags, void **ppData)
{
   VK_FROM_HANDLE(nvrm_device_memory, mem, _memory);
   void *map;
   (void)_device;
   (void)size;
   (void)flags;

   if (!mem->map) {
      mem->map = nv_rm_bo_map(mem->bo);
      if (!mem->map)
         return VK_ERROR_MEMORY_MAP_FAILED;
   }
   map = (uint8_t *)mem->map + offset;
   *ppData = map;
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
nvrm_UnmapMemory(VkDevice _device, VkDeviceMemory _memory)
{
   VK_FROM_HANDLE(nvrm_device_memory, mem, _memory);
   (void)_device;
   if (mem && mem->map) {
      nv_rm_bo_unmap(mem->bo);
      mem->map = NULL;
   }
}


static uint32_t
nvrm_format_bpp(VkFormat fmt)
{
   switch (fmt) {
   case VK_FORMAT_R8_UNORM:
   case VK_FORMAT_R8_UINT:
   case VK_FORMAT_S8_UINT:
      return 1;
   case VK_FORMAT_R8G8_UNORM:
   case VK_FORMAT_R16_SFLOAT:
   case VK_FORMAT_R16_UNORM:
   case VK_FORMAT_D16_UNORM:
      return 2;
   case VK_FORMAT_R8G8B8A8_UNORM:
   case VK_FORMAT_R8G8B8A8_SRGB:
   case VK_FORMAT_B8G8R8A8_UNORM:
   case VK_FORMAT_B8G8R8A8_SRGB:
   case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
   case VK_FORMAT_R32_SFLOAT:
   case VK_FORMAT_D24_UNORM_S8_UINT:
   case VK_FORMAT_D32_SFLOAT:
      return 4;
   case VK_FORMAT_R16G16B16A16_SFLOAT:
      return 8;
   case VK_FORMAT_R32G32B32A32_SFLOAT:
      return 16;
   default:
      return 4;
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
nvrm_CreateImage(VkDevice _device,
                 const VkImageCreateInfo *pCreateInfo,
                 const VkAllocationCallbacks *pAllocator,
                 VkImage *pImage)
{
   VK_FROM_HANDLE(nvrm_device, device, _device);
   struct nvrm_image *img;
   uint32_t w, h, bpp, pitch, size;

   img = vk_image_create(&device->vk, pCreateInfo, pAllocator, sizeof(*img));
   if (!img)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   w = pCreateInfo->extent.width ? pCreateInfo->extent.width : 1;
   h = pCreateInfo->extent.height ? pCreateInfo->extent.height : 1;
   bpp = nvrm_format_bpp(pCreateInfo->format);
   pitch = (w * bpp + 31u) & ~31u;
   size = pitch * h;
   if (pCreateInfo->extent.depth > 1)
      size *= pCreateInfo->extent.depth;
   if (pCreateInfo->arrayLayers > 1)
      size *= pCreateInfo->arrayLayers;
   if (pCreateInfo->mipLevels > 1)
      size += size / 2; /* coarse mip tail estimate */

   img->bo = NULL;
   img->gpu_offset = 0;
   img->row_pitch = pitch;
   img->level0_size = pitch * h;
   img->is_linear = !(pCreateInfo->tiling == VK_IMAGE_TILING_OPTIMAL &&
                      (pCreateInfo->usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                             VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)));
   /* OPTIMAL attachments often blocklinear on NVIDIA; sample-only may be linear */
   if (pCreateInfo->tiling == VK_IMAGE_TILING_LINEAR)
      img->is_linear = true;

   *pImage = nvrm_image_to_handle(img);
   (void)size;
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
nvrm_DestroyImage(VkDevice _device, VkImage _image,
                  const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(nvrm_device, device, _device);
   VK_FROM_HANDLE(nvrm_image, img, _image);
   if (!img)
      return;
   /* BO is owned by bound memory path; image may share BO */
   vk_image_destroy(&device->vk, pAllocator, &img->vk);
}

VKAPI_ATTR VkResult VKAPI_CALL
nvrm_BindImageMemory2(VkDevice _device, uint32_t bindInfoCount,
                      const VkBindImageMemoryInfo *pBindInfos)
{
   VK_FROM_HANDLE(nvrm_device, device, _device);
   uint32_t i;
   (void)device;
   for (i = 0; i < bindInfoCount; i++) {
      VK_FROM_HANDLE(nvrm_image, img, pBindInfos[i].image);
      VK_FROM_HANDLE(nvrm_device_memory, mem, pBindInfos[i].memory);
      if (!img || !mem || !mem->bo)
         continue;
      img->bo = mem->bo;
      img->gpu_offset = nv_rm_bo_gpu_offset(mem->bo) + pBindInfos[i].memoryOffset;
   }
   return VK_SUCCESS;
}
