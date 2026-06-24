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
};

struct nvrm_cmd_buffer {
   struct vk_command_buffer vk;
   struct nvrm_device *device;
   struct nv_rm_bo *push_bo;
   uint32_t *push_map;
   uint32_t push_dw_cap;
   uint32_t push_dw_used;
   struct nv_push push;
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
extern struct vk_command_buffer_dispatch_table nvrm_cmd_buffer_entrypoints;
extern struct vk_queue_dispatch_table nvrm_queue_entrypoints;

VkResult nvrm_enumerate_physical_devices(struct vk_instance *vk_instance);
void nvrm_physical_device_destroy(struct vk_physical_device *vk_pdev);
VkResult nvrm_queue_init(struct nvrm_device *device, struct nvrm_queue *queue);
void nvrm_queue_finish(struct nvrm_queue *queue);

#endif
