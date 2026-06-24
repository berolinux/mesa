/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 */
#ifndef NVRM_PRIVATE_H
#define NVRM_PRIVATE_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vk_alloc.h"
#include "vk_buffer.h"
#include "vk_command_buffer.h"
#include "vk_command_pool.h"
#include "vk_device.h"
#include "vk_device_memory.h"
#include "vk_image.h"
#include "vk_instance.h"
#include "vk_log.h"
#include "vk_object.h"
#include "vk_physical_device.h"
#include "vk_queue.h"
#include "vk_sync.h"
#include "wsi_common.h"

#include "nv_rm.h"
#include "nv_channel.h"
#include "nv_device_info.h"
#include "nv_push.h"
#include "nv_3d_methods.h"
#include "nv_copy_methods.h"

#define NVRM_API_VERSION VK_MAKE_VERSION(1, 3, 0)

struct nvrm_instance {
   struct vk_instance vk;
};

struct nvrm_physical_device {
   struct vk_physical_device vk;
   struct nvrm_instance *instance;
   struct nv_rm_device *rm;
   const struct nv_device_info *info;
   int drm_fd;
   int gpu_index;
   struct wsi_device wsi_device;
};

struct nvrm_device {
   struct vk_device vk;
   struct nvrm_physical_device *physical;
   struct nv_rm_device *rm;
   const struct nv_device_info *info;
   struct nvrm_queue *queue;
   struct nv_tex_pool *tex_pool; /* sampler+header pool for descriptors */
};

struct nvrm_queue {
   struct vk_queue vk;
   struct nvrm_device *device;
   struct nv_channel *channel;
   uint32_t h_channel;
   uint32_t h_gpfifo_mem;
   bool channel_ready;
};

struct nvrm_device_memory {
   struct vk_device_memory vk;
   struct nv_rm_bo *bo;
   void *map;
   VkDeviceSize size;
   bool is_vram;
};

struct nvrm_buffer {
   struct vk_buffer vk;
   struct nv_rm_bo *bo;          /* if dedicated / non-sparse */
   VkDeviceAddress addr;
};

struct nvrm_image {
   struct vk_image vk;
   struct nv_rm_bo *bo;
   uint64_t gpu_offset;
   uint32_t row_pitch;       /* bytes; from format Bpp * width, 32B aligned */
   uint32_t level0_size;     /* approximate level-0 plane size */
   uint32_t total_size;      /* allocation estimate including mips/layers */
   uint32_t bpp;             /* bytes per pixel (level 0) */
   uint8_t  gobs_width;      /* NV_TEX_GOBS_* for blocklinear (log2 gobs) */
   uint8_t  gobs_height;
   uint8_t  gobs_depth;
   bool     is_linear;       /* prefer pitch/linear headers when true */
   bool     is_blocklinear;  /* OPTIMAL tiled layout (TEXHEAD_BL / NVC6B5 BL) */
};

struct nvrm_graphics_pipeline; /* defined in nvrm_pipeline.c */

struct nvrm_sampler {
   struct vk_object_base base;
   uint8_t addr_u, addr_v, addr_p;
   uint8_t mag_filt, min_filt, mip_filt;
   float min_lod, max_lod, lod_bias;
   bool unnormalized_coords;
};

/* Forward decls for pipeline types used in cmd buffer state */
struct nvrm_graphics_pipeline;
struct nvrm_compute_pipeline;

struct nvrm_cmd_buffer {
   struct vk_command_buffer vk;
   struct nvrm_device *device;
   struct nv_rm_bo *push_bo;
   struct nv_rm_bo *qmd_bo;   /* scratch for inline QMD GPU address field */
   struct nv_rm_bo *lmem_bo;  /* compute global LMEM backing (spill/scratch) */
   bool lmem_programmed;      /* SET_SHADER_LOCAL_MEMORY* already emitted */
   uint32_t *push_map;
   uint32_t push_dw_cap;
   uint32_t push_dw_used;
   struct nv_push push;
   /* Recording state for graphics / compute */
   struct nvrm_graphics_pipeline *bound_gfx_pipeline;
   struct nvrm_compute_pipeline *bound_compute_pipeline;
   bool channel_init_done;
   bool compute_init_done;
   bool in_render_pass;
   uint32_t render_width;
   uint32_t render_height;
   /* Default local workgroup size when pipeline does not specify */
   uint32_t compute_local_x, compute_local_y, compute_local_z;
};

VK_DEFINE_HANDLE_CASTS(nvrm_instance, vk.base, VkInstance, VK_OBJECT_TYPE_INSTANCE)
VK_DEFINE_HANDLE_CASTS(nvrm_physical_device, vk.base, VkPhysicalDevice,
                       VK_OBJECT_TYPE_PHYSICAL_DEVICE)
VK_DEFINE_HANDLE_CASTS(nvrm_device, vk.base, VkDevice, VK_OBJECT_TYPE_DEVICE)
VK_DEFINE_HANDLE_CASTS(nvrm_queue, vk.base, VkQueue, VK_OBJECT_TYPE_QUEUE)
VK_DEFINE_HANDLE_CASTS(nvrm_device_memory, vk.base, VkDeviceMemory,
                       VK_OBJECT_TYPE_DEVICE_MEMORY)
VK_DEFINE_HANDLE_CASTS(nvrm_buffer, vk.base, VkBuffer, VK_OBJECT_TYPE_BUFFER)
VK_DEFINE_HANDLE_CASTS(nvrm_image, vk.base, VkImage, VK_OBJECT_TYPE_IMAGE)
VK_DEFINE_HANDLE_CASTS(nvrm_cmd_buffer, vk.base, VkCommandBuffer,
                       VK_OBJECT_TYPE_COMMAND_BUFFER)

extern struct vk_physical_device_dispatch_table nvrm_physical_device_entrypoints;
extern struct vk_device_dispatch_table nvrm_device_entrypoints;
extern struct vk_instance_dispatch_table nvrm_instance_entrypoints;

VkResult nvrm_enumerate_physical_devices(struct vk_instance *vk_instance);
void nvrm_physical_device_destroy(struct vk_physical_device *vk_pdev);
VkResult nvrm_queue_init(struct nvrm_device *device, struct nvrm_queue *queue);
void nvrm_queue_finish(struct nvrm_queue *queue);

/* Hand-implemented entrypoints (prototypes for -Wmissing-prototypes).
 * Full tables come from vk_entrypoints_gen in a later integration step. */
VKAPI_ATTR VkResult VKAPI_CALL nvrm_CreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                                                   const VkAllocationCallbacks *pAllocator,
                                                   VkInstance *pInstance);
VKAPI_ATTR void VKAPI_CALL nvrm_DestroyInstance(VkInstance instance,
                                                const VkAllocationCallbacks *pAllocator);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL nvrm_GetInstanceProcAddr(VkInstance instance,
                                                                  const char *pName);
VKAPI_ATTR void VKAPI_CALL nvrm_GetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice,
                                                             VkPhysicalDeviceProperties2 *pProperties);
VKAPI_ATTR void VKAPI_CALL nvrm_GetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice physicalDevice,
                                                                        uint32_t *pQueueFamilyPropertyCount,
                                                                        VkQueueFamilyProperties2 *pQueueFamilyProperties);
VKAPI_ATTR void VKAPI_CALL nvrm_GetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physicalDevice,
                                                                   VkPhysicalDeviceMemoryProperties2 *pMemoryProperties);
VKAPI_ATTR VkResult VKAPI_CALL nvrm_CreateDevice(VkPhysicalDevice physicalDevice,
                                                 const VkDeviceCreateInfo *pCreateInfo,
                                                 const VkAllocationCallbacks *pAllocator,
                                                 VkDevice *pDevice);
VKAPI_ATTR void VKAPI_CALL nvrm_DestroyDevice(VkDevice device,
                                              const VkAllocationCallbacks *pAllocator);
VKAPI_ATTR void VKAPI_CALL nvrm_GetDeviceQueue2(VkDevice device,
                                                const VkDeviceQueueInfo2 *pQueueInfo,
                                                VkQueue *pQueue);
VKAPI_ATTR VkResult VKAPI_CALL nvrm_AllocateMemory(VkDevice device,
                                                   const VkMemoryAllocateInfo *pAllocateInfo,
                                                   const VkAllocationCallbacks *pAllocator,
                                                   VkDeviceMemory *pMemory);
VKAPI_ATTR void VKAPI_CALL nvrm_FreeMemory(VkDevice device, VkDeviceMemory memory,
                                           const VkAllocationCallbacks *pAllocator);
VKAPI_ATTR VkResult VKAPI_CALL nvrm_MapMemory(VkDevice device, VkDeviceMemory memory,
                                              VkDeviceSize offset, VkDeviceSize size,
                                              VkMemoryMapFlags flags, void **ppData);
VKAPI_ATTR void VKAPI_CALL nvrm_UnmapMemory(VkDevice device, VkDeviceMemory memory);
VKAPI_ATTR VkResult VKAPI_CALL nvrm_BeginCommandBuffer(VkCommandBuffer commandBuffer,
                                                       const VkCommandBufferBeginInfo *pBeginInfo);
VKAPI_ATTR VkResult VKAPI_CALL nvrm_EndCommandBuffer(VkCommandBuffer commandBuffer);
VKAPI_ATTR void VKAPI_CALL nvrm_CmdPipelineBarrier2(VkCommandBuffer commandBuffer,
                                                    const VkDependencyInfo *pDependencyInfo);
VKAPI_ATTR void VKAPI_CALL nvrm_CmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image,
                                                   VkImageLayout imageLayout,
                                                   const VkClearColorValue *pColor,
                                                   uint32_t rangeCount,
                                                   const VkImageSubresourceRange *pRanges);
VKAPI_ATTR void VKAPI_CALL nvrm_CmdCopyBuffer2(VkCommandBuffer commandBuffer,
                                               const VkCopyBufferInfo2 *pCopyBufferInfo);
VKAPI_ATTR void VKAPI_CALL nvrm_CmdCopyImage2(VkCommandBuffer commandBuffer,
                                              const VkCopyImageInfo2 *pCopyImageInfo);
VKAPI_ATTR void VKAPI_CALL nvrm_CmdCopyBufferToImage2(VkCommandBuffer commandBuffer,
                                                      const VkCopyBufferToImageInfo2 *pCopyBufferToImageInfo);
VKAPI_ATTR void VKAPI_CALL nvrm_CmdCopyImageToBuffer2(VkCommandBuffer commandBuffer,
                                                      const VkCopyImageToBufferInfo2 *pCopyImageToBufferInfo);
VKAPI_ATTR VkResult VKAPI_CALL nvrm_CreateGraphicsPipelines(VkDevice device,
                                                            VkPipelineCache pipelineCache,
                                                            uint32_t createInfoCount,
                                                            const VkGraphicsPipelineCreateInfo *pCreateInfos,
                                                            const VkAllocationCallbacks *pAllocator,
                                                            VkPipeline *pPipelines);
VKAPI_ATTR VkResult VKAPI_CALL nvrm_CreateComputePipelines(VkDevice device,
                                                           VkPipelineCache pipelineCache,
                                                           uint32_t createInfoCount,
                                                           const VkComputePipelineCreateInfo *pCreateInfos,
                                                           const VkAllocationCallbacks *pAllocator,
                                                           VkPipeline *pPipelines);
VKAPI_ATTR void VKAPI_CALL nvrm_DestroyPipeline(VkDevice device, VkPipeline pipeline,
                                                const VkAllocationCallbacks *pAllocator);
VKAPI_ATTR VkResult VKAPI_CALL nvrm_CreatePipelineLayout(VkDevice device,
                                                         const VkPipelineLayoutCreateInfo *pCreateInfo,
                                                         const VkAllocationCallbacks *pAllocator,
                                                         VkPipelineLayout *pPipelineLayout);
VKAPI_ATTR void VKAPI_CALL nvrm_DestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout,
                                                      const VkAllocationCallbacks *pAllocator);
VKAPI_ATTR VkResult VKAPI_CALL nvrm_CreateDescriptorSetLayout(VkDevice device,
                                                              const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
                                                              const VkAllocationCallbacks *pAllocator,
                                                              VkDescriptorSetLayout *pSetLayout);
VKAPI_ATTR void VKAPI_CALL nvrm_DestroyDescriptorSetLayout(VkDevice device,
                                                           VkDescriptorSetLayout descriptorSetLayout,
                                                           const VkAllocationCallbacks *pAllocator);
VKAPI_ATTR VkResult VKAPI_CALL nvrm_CreateDescriptorPool(VkDevice device,
                                                         const VkDescriptorPoolCreateInfo *pCreateInfo,
                                                         const VkAllocationCallbacks *pAllocator,
                                                         VkDescriptorPool *pDescriptorPool);
VKAPI_ATTR void VKAPI_CALL nvrm_DestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool,
                                                      const VkAllocationCallbacks *pAllocator);
VKAPI_ATTR VkResult VKAPI_CALL nvrm_AllocateDescriptorSets(VkDevice device,
                                                           const VkDescriptorSetAllocateInfo *pAllocateInfo,
                                                           VkDescriptorSet *pDescriptorSets);
VKAPI_ATTR VkResult VKAPI_CALL nvrm_FreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool,
                                                       uint32_t descriptorSetCount,
                                                       const VkDescriptorSet *pDescriptorSets);
VKAPI_ATTR void VKAPI_CALL nvrm_UpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount,
                                                     const VkWriteDescriptorSet *pDescriptorWrites,
                                                     uint32_t descriptorCopyCount,
                                                     const VkCopyDescriptorSet *pDescriptorCopies);
VKAPI_ATTR VkResult VKAPI_CALL nvrm_CreateRenderPass2(VkDevice device,
                                                      const VkRenderPassCreateInfo2 *pCreateInfo,
                                                      const VkAllocationCallbacks *pAllocator,
                                                      VkRenderPass *pRenderPass);
VKAPI_ATTR VkResult VKAPI_CALL nvrm_CreateRenderPass(VkDevice device,
                                                     const VkRenderPassCreateInfo *pCreateInfo,
                                                     const VkAllocationCallbacks *pAllocator,
                                                     VkRenderPass *pRenderPass);
VKAPI_ATTR void VKAPI_CALL nvrm_DestroyRenderPass(VkDevice device, VkRenderPass renderPass,
                                                  const VkAllocationCallbacks *pAllocator);
VKAPI_ATTR VkResult VKAPI_CALL nvrm_CreateImageView(VkDevice device,
                                                    const VkImageViewCreateInfo *pCreateInfo,
                                                    const VkAllocationCallbacks *pAllocator,
                                                    VkImageView *pView);
VKAPI_ATTR void VKAPI_CALL nvrm_DestroyImageView(VkDevice device, VkImageView imageView,
                                                 const VkAllocationCallbacks *pAllocator);
VKAPI_ATTR VkResult VKAPI_CALL nvrm_CreateFramebuffer(VkDevice device,
                                                      const VkFramebufferCreateInfo *pCreateInfo,
                                                      const VkAllocationCallbacks *pAllocator,
                                                      VkFramebuffer *pFramebuffer);
VKAPI_ATTR void VKAPI_CALL nvrm_DestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer,
                                                   const VkAllocationCallbacks *pAllocator);
VKAPI_ATTR void VKAPI_CALL nvrm_CmdBindPipeline(VkCommandBuffer commandBuffer,
                                                VkPipelineBindPoint pipelineBindPoint,
                                                VkPipeline pipeline);
VKAPI_ATTR void VKAPI_CALL nvrm_CmdBindDescriptorSets(VkCommandBuffer commandBuffer,
                                                      VkPipelineBindPoint pipelineBindPoint,
                                                      VkPipelineLayout layout, uint32_t firstSet,
                                                      uint32_t descriptorSetCount,
                                                      const VkDescriptorSet *pDescriptorSets,
                                                      uint32_t dynamicOffsetCount,
                                                      const uint32_t *pDynamicOffsets);
VKAPI_ATTR void VKAPI_CALL nvrm_CmdBeginRenderPass2(VkCommandBuffer commandBuffer,
                                                    const VkRenderPassBeginInfo *pRenderPassBegin,
                                                    const VkSubpassBeginInfo *pSubpassBeginInfo);
VKAPI_ATTR void VKAPI_CALL nvrm_CmdEndRenderPass2(VkCommandBuffer commandBuffer,
                                                  const VkSubpassEndInfo *pSubpassEndInfo);
VKAPI_ATTR void VKAPI_CALL nvrm_CmdBeginRendering(VkCommandBuffer commandBuffer,
                                                  const VkRenderingInfo *pRenderingInfo);
VKAPI_ATTR void VKAPI_CALL nvrm_CmdEndRendering(VkCommandBuffer commandBuffer);
VKAPI_ATTR void VKAPI_CALL nvrm_CmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount,
                                        uint32_t instanceCount, uint32_t firstVertex,
                                        uint32_t firstInstance);
VKAPI_ATTR void VKAPI_CALL nvrm_CmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount,
                                               uint32_t instanceCount, uint32_t firstIndex,
                                               int32_t vertexOffset, uint32_t firstInstance);
VKAPI_ATTR VkResult VKAPI_CALL nvrm_QueueSubmit2(VkQueue queue, uint32_t submitCount,
                                                 const VkSubmitInfo2 *pSubmits, VkFence fence);
VKAPI_ATTR VkResult VKAPI_CALL nvrm_QueueSubmit(VkQueue queue, uint32_t submitCount,
                                                const VkSubmitInfo *pSubmits, VkFence fence);
VKAPI_ATTR VkResult VKAPI_CALL nvrm_CreateSampler(VkDevice device,
                                                  const VkSamplerCreateInfo *pCreateInfo,
                                                  const VkAllocationCallbacks *pAllocator,
                                                  VkSampler *pSampler);
VKAPI_ATTR void VKAPI_CALL nvrm_DestroySampler(VkDevice device, VkSampler sampler,
                                               const VkAllocationCallbacks *pAllocator);
VKAPI_ATTR VkResult VKAPI_CALL nvrm_CreateImage(VkDevice device,
                                                const VkImageCreateInfo *pCreateInfo,
                                                const VkAllocationCallbacks *pAllocator,
                                                VkImage *pImage);
VKAPI_ATTR void VKAPI_CALL nvrm_DestroyImage(VkDevice device, VkImage image,
                                             const VkAllocationCallbacks *pAllocator);
VKAPI_ATTR void VKAPI_CALL nvrm_CmdDispatchBase(VkCommandBuffer commandBuffer,
                                                uint32_t baseGroupX,
                                                uint32_t baseGroupY,
                                                uint32_t baseGroupZ,
                                                uint32_t groupCountX,
                                                uint32_t groupCountY,
                                                uint32_t groupCountZ);
VKAPI_ATTR void VKAPI_CALL nvrm_CmdDispatchIndirect(VkCommandBuffer commandBuffer,
                                                    VkBuffer buffer,
                                                    VkDeviceSize offset);
VKAPI_ATTR void VKAPI_CALL nvrm_CmdDispatch(VkCommandBuffer commandBuffer,
                                            uint32_t groupCountX,
                                            uint32_t groupCountY,
                                            uint32_t groupCountZ);
VKAPI_ATTR VkResult VKAPI_CALL nvrm_CreateBuffer(VkDevice device,
                                                 const VkBufferCreateInfo *pCreateInfo,
                                                 const VkAllocationCallbacks *pAllocator,
                                                 VkBuffer *pBuffer);
VKAPI_ATTR void VKAPI_CALL nvrm_DestroyBuffer(VkDevice device, VkBuffer buffer,
                                              const VkAllocationCallbacks *pAllocator);
VKAPI_ATTR VkResult VKAPI_CALL nvrm_BindBufferMemory2(VkDevice device,
                                                      uint32_t bindInfoCount,
                                                      const VkBindBufferMemoryInfo *pBindInfos);
VKAPI_ATTR void VKAPI_CALL nvrm_GetBufferMemoryRequirements2(VkDevice device,
                                                             const VkBufferMemoryRequirementsInfo2 *pInfo,
                                                             VkMemoryRequirements2 *pMemoryRequirements);
VKAPI_ATTR void VKAPI_CALL nvrm_GetImageMemoryRequirements2(VkDevice device,
                                                            const VkImageMemoryRequirementsInfo2 *pInfo,
                                                            VkMemoryRequirements2 *pMemoryRequirements);
VKAPI_ATTR VkResult VKAPI_CALL nvrm_BindImageMemory2(VkDevice device,
                                                     uint32_t bindInfoCount,
                                                     const VkBindImageMemoryInfo *pBindInfos);

#endif
