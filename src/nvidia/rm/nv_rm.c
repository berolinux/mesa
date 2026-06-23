/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Implementation uses libdrm_nvidia (drm/nvidia.h).  If the library is not
 * linked (build without libdrm_nvidia), compile-time stubs return -ENOSYS.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "nv_rm.h"

#if defined(HAVE_LIBDRM_NVIDIA)
#include "nvidia.h"
#endif

struct nv_rm_device {
#if defined(HAVE_LIBDRM_NVIDIA)
   nvidia_device_handle nvdev;
#endif
   struct nv_device_info info;
   int drm_fd;
   int gpu_index;
   bool valid;
};

struct nv_rm_bo {
   struct nv_rm_device *dev;
#if defined(HAVE_LIBDRM_NVIDIA)
   nvidia_bo_handle nvbo;
#endif
   uint64_t size;
   uint64_t gpu_offset;
   uint32_t rm_handle;
   void *cpu_ptr;
   bool mapped;
};

bool
nv_rm_probe_available(void)
{
#if defined(HAVE_LIBDRM_NVIDIA)
   return nvidia_probe_available();
#else
   return false;
#endif
}

int
nv_rm_probe_gpu_count(void)
{
#if defined(HAVE_LIBDRM_NVIDIA)
   nvidia_device_handle dev = NULL;
   uint32_t maj, min;
   int count;

   if (nvidia_device_initialize(-1, &maj, &min, &dev) != 0)
      return 0;
   count = nvidia_device_get_gpu_count(dev);
   nvidia_device_deinitialize(dev);
   return count;
#else
   return 0;
#endif
}

struct nv_rm_device *
nv_rm_device_open(int drm_fd, int gpu_index)
{
#if defined(HAVE_LIBDRM_NVIDIA)
   struct nv_rm_device *dev;
   struct nvidia_gpu_info gi;
   uint32_t maj, min;
   int ret;

   dev = calloc(1, sizeof(*dev));
   if (!dev)
      return NULL;

   dev->drm_fd = drm_fd;
   dev->gpu_index = gpu_index;

   if (gpu_index >= 0)
      ret = nvidia_device_initialize_gpu(drm_fd, gpu_index, &maj, &min,
                                         &dev->nvdev);
   else
      ret = nvidia_device_initialize(drm_fd, &maj, &min, &dev->nvdev);

   if (ret != 0) {
      free(dev);
      return NULL;
   }

   if (gpu_index < 0 && nvidia_device_get_gpu_count(dev->nvdev) > 0) {
      /* Default to first GPU */
      gpu_index = 0;
      dev->gpu_index = 0;
      nvidia_device_deinitialize(dev->nvdev);
      ret = nvidia_device_initialize_gpu(drm_fd, 0, &maj, &min, &dev->nvdev);
      if (ret != 0) {
         free(dev);
         return NULL;
      }
   }

   memset(&gi, 0, sizeof(gi));
   if (nvidia_query_selected_gpu_info(dev->nvdev, &gi) == 0 ||
       nvidia_query_gpu_info(dev->nvdev, dev->gpu_index, &gi) == 0) {
      dev->info.gpu_id = gi.gpu_id;
      dev->info.pci_vendor_id = gi.pci_vendor_id;
      dev->info.pci_device_id = gi.pci_device_id;
      dev->info.pci_domain = gi.pci_domain;
      dev->info.pci_bus = gi.pci_bus;
      dev->info.pci_dev = gi.pci_device;
      dev->info.pci_func = gi.pci_function;
      dev->info.architecture = gi.architecture;
      dev->info.implementation = gi.implementation;
      dev->info.revision = gi.revision;
      dev->info.sm_version = gi.sm_version;
      dev->info.gpc_count = gi.gpc_count;
      dev->info.tpc_count = gi.tpc_count;
      dev->info.vram_size_bytes = gi.fb_size;
      dev->info.vram_usable_bytes = gi.fb_usable ? gi.fb_usable : gi.fb_size;
      memcpy(dev->info.name, gi.name, sizeof(dev->info.name));
   }

   nv_device_info_select_classes(&dev->info);
   dev->valid = true;
   return dev;
#else
   (void)drm_fd;
   (void)gpu_index;
   return NULL;
#endif
}

void
nv_rm_device_close(struct nv_rm_device *dev)
{
   if (!dev)
      return;
#if defined(HAVE_LIBDRM_NVIDIA)
   if (dev->nvdev)
      nvidia_device_deinitialize(dev->nvdev);
#endif
   free(dev);
}

int
nv_rm_device_fd_ctl(struct nv_rm_device *dev)
{
#if defined(HAVE_LIBDRM_NVIDIA)
   return dev && dev->nvdev ? nvidia_device_get_fd(dev->nvdev) : -1;
#else
   (void)dev;
   return -1;
#endif
}

int
nv_rm_device_fd_gpu(struct nv_rm_device *dev)
{
#if defined(HAVE_LIBDRM_NVIDIA)
   return dev && dev->nvdev ? nvidia_device_get_gpu_fd(dev->nvdev) : -1;
#else
   (void)dev;
   return -1;
#endif
}

int
nv_rm_device_fd_drm(struct nv_rm_device *dev)
{
   return dev ? dev->drm_fd : -1;
}

const struct nv_device_info *
nv_rm_device_info(struct nv_rm_device *dev)
{
   return dev ? &dev->info : NULL;
}

uint32_t
nv_rm_device_client_handle(struct nv_rm_device *dev)
{
#if defined(HAVE_LIBDRM_NVIDIA)
   return dev && dev->nvdev ? nvidia_device_get_client_handle(dev->nvdev) : 0;
#else
   (void)dev;
   return 0;
#endif
}

uint32_t
nv_rm_device_device_handle(struct nv_rm_device *dev)
{
#if defined(HAVE_LIBDRM_NVIDIA)
   return dev && dev->nvdev ? nvidia_device_get_device_handle(dev->nvdev) : 0;
#else
   (void)dev;
   return 0;
#endif
}

uint32_t
nv_rm_device_subdevice_handle(struct nv_rm_device *dev)
{
#if defined(HAVE_LIBDRM_NVIDIA)
   return dev && dev->nvdev ? nvidia_device_get_subdevice_handle(dev->nvdev) : 0;
#else
   (void)dev;
   return 0;
#endif
}

int
nv_rm_control(struct nv_rm_device *dev, uint32_t h_object, uint32_t cmd,
              void *params, uint32_t params_size)
{
#if defined(HAVE_LIBDRM_NVIDIA)
   if (!dev || !dev->nvdev)
      return -EINVAL;
   return nvidia_rm_control(dev->nvdev, h_object, cmd, params, params_size);
#else
   (void)dev; (void)h_object; (void)cmd; (void)params; (void)params_size;
   return -ENOSYS;
#endif
}

int
nv_rm_alloc_object(struct nv_rm_device *dev, uint32_t h_parent,
                   uint32_t *h_object, uint32_t h_class,
                   void *alloc_params, uint32_t alloc_params_size)
{
#if defined(HAVE_LIBDRM_NVIDIA)
   if (!dev || !dev->nvdev || !h_object)
      return -EINVAL;
   return nvidia_rm_alloc(dev->nvdev, h_parent, h_object, h_class,
                          alloc_params, alloc_params_size);
#else
   (void)dev; (void)h_parent; (void)h_object; (void)h_class;
   (void)alloc_params; (void)alloc_params_size;
   return -ENOSYS;
#endif
}

int
nv_rm_free_object(struct nv_rm_device *dev, uint32_t h_parent,
                  uint32_t h_object)
{
#if defined(HAVE_LIBDRM_NVIDIA)
   if (!dev || !dev->nvdev)
      return -EINVAL;
   return nvidia_rm_free(dev->nvdev, h_parent, h_object);
#else
   (void)dev; (void)h_parent; (void)h_object;
   return -ENOSYS;
#endif
}

struct nv_rm_bo *
nv_rm_bo_alloc(struct nv_rm_device *dev, const struct nv_rm_bo_req *req)
{
#if defined(HAVE_LIBDRM_NVIDIA)
   struct nv_rm_bo *bo;
   struct nvidia_bo_alloc_request areq;
   struct nvidia_bo_metadata meta;
   int ret;

   if (!dev || !dev->nvdev || !req || req->size == 0)
      return NULL;

   bo = calloc(1, sizeof(*bo));
   if (!bo)
      return NULL;
   bo->dev = dev;

   memset(&areq, 0, sizeof(areq));
   areq.size = req->size;
   areq.alignment = req->alignment;
   areq.domain = req->vram ? NVIDIA_BO_DOMAIN_VRAM : NVIDIA_BO_DOMAIN_GART;
   areq.flags = 0;
   if (req->cpu_access)
      areq.flags |= NVIDIA_BO_FLAGS_CPU_ACCESS;
   if (req->no_scanout)
      areq.flags |= NVIDIA_BO_FLAGS_NO_SCANOUT;

   ret = nvidia_bo_alloc(dev->nvdev, &areq, &bo->nvbo);
   if (ret != 0) {
      free(bo);
      return NULL;
   }

   memset(&meta, 0, sizeof(meta));
   nvidia_bo_query_metadata(bo->nvbo, &meta);
   bo->size = meta.aligned_size ? meta.aligned_size : req->size;
   bo->gpu_offset = meta.gpu_offset;
   bo->rm_handle = meta.rm_handle;
   return bo;
#else
   (void)dev; (void)req;
   return NULL;
#endif
}

void
nv_rm_bo_free(struct nv_rm_bo *bo)
{
   if (!bo)
      return;
#if defined(HAVE_LIBDRM_NVIDIA)
   if (bo->mapped)
      nvidia_bo_cpu_unmap(bo->nvbo);
   if (bo->nvbo)
      nvidia_bo_free(bo->nvbo);
#endif
   free(bo);
}

void *
nv_rm_bo_map(struct nv_rm_bo *bo)
{
#if defined(HAVE_LIBDRM_NVIDIA)
   void *ptr = NULL;
   if (!bo || !bo->nvbo)
      return NULL;
   if (bo->mapped)
      return bo->cpu_ptr;
   if (nvidia_bo_cpu_map(bo->nvbo, &ptr) != 0)
      return NULL;
   bo->cpu_ptr = ptr;
   bo->mapped = true;
   return ptr;
#else
   (void)bo;
   return NULL;
#endif
}

void
nv_rm_bo_unmap(struct nv_rm_bo *bo)
{
#if defined(HAVE_LIBDRM_NVIDIA)
   if (!bo || !bo->mapped)
      return;
   nvidia_bo_cpu_unmap(bo->nvbo);
   bo->mapped = false;
   bo->cpu_ptr = NULL;
#else
   (void)bo;
#endif
}

uint32_t
nv_rm_bo_handle(struct nv_rm_bo *bo)
{
   return bo ? bo->rm_handle : 0;
}

uint64_t
nv_rm_bo_size(struct nv_rm_bo *bo)
{
   return bo ? bo->size : 0;
}

uint64_t
nv_rm_bo_gpu_offset(struct nv_rm_bo *bo)
{
   return bo ? bo->gpu_offset : 0;
}

int
nv_rm_bo_export_dmabuf(struct nv_rm_bo *bo, int *fd_out)
{
#if defined(HAVE_LIBDRM_NVIDIA)
   if (!bo || !bo->nvbo || !fd_out)
      return -EINVAL;
   return nvidia_bo_export_dmabuf(bo->nvbo, fd_out);
#else
   (void)bo; (void)fd_out;
   return -ENOSYS;
#endif
}
