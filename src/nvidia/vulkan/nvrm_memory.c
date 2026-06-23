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
