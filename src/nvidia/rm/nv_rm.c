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

#if defined(HAVE_LIBDRM_NVIDIA)
#include <stdio.h>
#endif

#include "nv_rm.h"

#if defined(HAVE_LIBDRM_NVIDIA)
#include "nvidia.h"
#include "nvidia_rm.h"
#endif

struct nv_rm_device {
#if defined(HAVE_LIBDRM_NVIDIA)
   nvidia_device_handle nvdev;
#endif
   struct nv_device_info info;
   int drm_fd;
   int gpu_index;
   bool valid;

   /* Explicit VASpace (FERMI_VASPACE_A) for channel/BO GPU VAs */
   uint32_t h_vaspace;
   uint64_t vas_size;
   uint64_t vas_base;
   bool vas_ready;

   /* VOLTA_USERMODE_A / HOPPER_USERMODE_A doorbell BAR mapping */
   uint32_t h_usermode;
   uint32_t usermode_class;
   volatile void *usermode_map;
   bool usermode_ready;
};

struct nv_rm_bo {
   struct nv_rm_device *dev;
#if defined(HAVE_LIBDRM_NVIDIA)
   nvidia_bo_handle nvbo;
#endif
   uint64_t size;
   uint64_t gpu_offset;      /* GPU VA if mapped into VAS; else RM phys offset */
   uint64_t dma_offset;      /* NVOS46 returned VA */
   uint32_t rm_handle;
   void *cpu_ptr;
   bool mapped;
   bool gpu_va_mapped;
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

   /* Best-effort refine classes via RM GET_ENGINE_CLASSLIST (+ engine walk) */
   {
      uint32_t clist[128];
      uint32_t engines[84];
      uint32_t n, ne, ei;
      static const uint32_t refine_engines[] = {
         NV2080_ENGINE_TYPE_GRAPHICS,
         NV2080_ENGINE_TYPE_COPY0,
         NV2080_ENGINE_TYPE_COPY1,
         NV2080_ENGINE_TYPE_COPY2,
         NV2080_ENGINE_TYPE_NVDEC0,
         NV2080_ENGINE_TYPE_NVENC0,
         NV2080_ENGINE_TYPE_NVENC1,
      };
      unsigned ri;

      /* For each engine's classlist, refine by class ID bands (not only et==).
       * Covers alternate COPY/NVDEC indices returned by GET_ENGINES_V2. */
#define NV_RM_REFINE_CLIST(clist_, n_) do { \
         nv_device_info_refine_gpfifo_from_list(&dev->info, (clist_), (n_)); \
         nv_device_info_refine_class_from_list(&dev->info, 0, (clist_), (n_)); \
         nv_device_info_refine_class_from_list(&dev->info, 1, (clist_), (n_)); \
         nv_device_info_refine_class_from_list(&dev->info, 2, (clist_), (n_)); \
         nv_device_info_refine_class_from_list(&dev->info, 3, (clist_), (n_)); \
         nv_device_info_refine_class_from_list(&dev->info, 4, (clist_), (n_)); \
      } while (0)

      ne = (uint32_t)(sizeof(engines) / sizeof(engines[0]));
      if (nvidia_rm_gpu_get_engines(dev->nvdev, engines, &ne) == 0 && ne) {
         for (ei = 0; ei < ne; ei++) {
            n = 128;
            if (nvidia_rm_gpu_get_engine_classlist(dev->nvdev, engines[ei],
                                                   clist, &n) != 0 || !n)
               continue;
            NV_RM_REFINE_CLIST(clist, n);
         }
      }

      /* Always hit canonical engines even if GET_ENGINES failed/partial */
      for (ri = 0; ri < sizeof(refine_engines) / sizeof(refine_engines[0]);
           ri++) {
         n = 128;
         if (nvidia_rm_gpu_get_engine_classlist(dev->nvdev, refine_engines[ri],
                                                clist, &n) != 0 || !n)
            continue;
         NV_RM_REFINE_CLIST(clist, n);
      }
#undef NV_RM_REFINE_CLIST

      if (getenv("NV_SMOKE_HW_VERBOSE") || getenv("NV_RM_LOG_CLASSES"))
         nv_device_info_log_classes(&dev->info, "nv_rm_device");
   }

   dev->valid = true;

   /* Best-effort: allocate private VASpace + usermode doorbell now so
    * channels and BO GPU VAs are ready before first submit.  Failures are
    * non-fatal; channels fall back to default device VAS / GPPut-only kick. */
   (void)nv_rm_device_ensure_vaspace(dev);
   (void)nv_rm_device_ensure_usermode(dev);

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
   if (dev->nvdev) {
      if (dev->usermode_map && dev->h_usermode) {
         nvidia_rm_unmap_memory(dev->nvdev,
                                nvidia_device_get_subdevice_handle(dev->nvdev),
                                dev->h_usermode,
                                (void *)dev->usermode_map, 0);
         nvidia_rm_free(dev->nvdev,
                        nvidia_device_get_subdevice_handle(dev->nvdev),
                        dev->h_usermode);
      }
      if (dev->h_vaspace) {
         nvidia_rm_free(dev->nvdev,
                        nvidia_device_get_device_handle(dev->nvdev),
                        dev->h_vaspace);
      }
      nvidia_device_deinitialize(dev->nvdev);
   }
#endif
   free(dev);
}

uint32_t
nv_rm_device_vaspace_handle(struct nv_rm_device *dev)
{
   return dev ? dev->h_vaspace : 0;
}

volatile void *
nv_rm_device_usermode_map(struct nv_rm_device *dev)
{
   return dev ? dev->usermode_map : NULL;
}

int
nv_rm_device_ensure_vaspace(struct nv_rm_device *dev)
{
#if defined(HAVE_LIBDRM_NVIDIA)
   int ret;
   uint32_t h_vas = 0;
   uint64_t va_size = 0, va_base = 0;

   if (!dev || !dev->nvdev)
      return -EINVAL;
   if (dev->vas_ready && dev->h_vaspace)
      return 0;

   /* Default: let RM pick size/base (pass 0); index = create new private VAS */
   ret = nvidia_rm_vaspace_alloc(dev->nvdev, &h_vas,
                                 NV_VASPACE_ALLOCATION_INDEX_GPU_NEW,
                                 NV_VASPACE_ALLOCATION_FLAGS_NONE,
                                 0, 0, 0, &va_size, &va_base);
   if (ret != 0) {
      /* Fall back to device-global VAS reference if private alloc fails */
      h_vas = 0;
      ret = nvidia_rm_vaspace_alloc(dev->nvdev, &h_vas,
                                    NV_VASPACE_ALLOCATION_INDEX_GPU_DEVICE,
                                    NV_VASPACE_ALLOCATION_FLAGS_NONE,
                                    0, 0, 0, &va_size, &va_base);
   }
   if (ret != 0)
      return ret;

   dev->h_vaspace = h_vas;
   dev->vas_size = va_size;
   dev->vas_base = va_base;
   dev->vas_ready = true;
   return 0;
#else
   (void)dev;
   return -ENOSYS;
#endif
}

int
nv_rm_device_ensure_usermode(struct nv_rm_device *dev)
{
#if defined(HAVE_LIBDRM_NVIDIA)
   int ret;
   uint32_t h_um = 0, h_class = 0;
   void *map = NULL;

   if (!dev || !dev->nvdev)
      return -EINVAL;
   if (dev->usermode_ready && dev->usermode_map)
      return 0;

   ret = nvidia_rm_usermode_alloc_map(dev->nvdev, &h_um, &h_class, &map);
   if (ret != 0)
      return ret;

   dev->h_usermode = h_um;
   dev->usermode_class = h_class;
   dev->usermode_map = (volatile void *)map;
   dev->usermode_ready = true;
   return 0;
#else
   (void)dev;
   return -ENOSYS;
#endif
}

int
nv_rm_bo_map_gpu_va(struct nv_rm_bo *bo)
{
#if defined(HAVE_LIBDRM_NVIDIA)
   struct nv_rm_device *dev;
   uint64_t dma_off = 0;
   int ret;

   if (!bo || !bo->dev || !bo->rm_handle)
      return -EINVAL;
   if (bo->gpu_va_mapped)
      return 0;

   dev = bo->dev;
   if (!dev->vas_ready || !dev->h_vaspace) {
      ret = nv_rm_device_ensure_vaspace(dev);
      if (ret != 0)
         return ret;
   }

   ret = nvidia_rm_map_memory_dma(dev->nvdev,
                                  nvidia_device_get_device_handle(dev->nvdev),
                                  dev->h_vaspace,
                                  bo->rm_handle,
                                  0, bo->size,
                                  NVOS46_FLAGS_ACCESS_READ_WRITE,
                                  &dma_off);
   if (ret != 0)
      return ret;

   bo->dma_offset = dma_off;
   bo->gpu_offset = dma_off;
   bo->gpu_va_mapped = true;
   return 0;
#else
   (void)bo;
   return -ENOSYS;
#endif
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

int
nv_rm_dup_object(struct nv_rm_device *dev, uint32_t h_parent_dst,
                 uint32_t *h_object_dst, uint32_t h_client_src,
                 uint32_t h_object_src, uint32_t flags)
{
#if defined(HAVE_LIBDRM_NVIDIA)
   if (!dev || !dev->nvdev || !h_object_dst)
      return -EINVAL;
   return nvidia_rm_dup_object(dev->nvdev, h_parent_dst, h_object_dst,
                               h_client_src, h_object_src, flags);
#else
   (void)dev; (void)h_parent_dst; (void)h_object_dst; (void)h_client_src;
   (void)h_object_src; (void)flags;
   return -ENOSYS;
#endif
}

int
nv_rm_get_event_data(struct nv_rm_device *dev, uint32_t *h_object_out,
                     uint32_t *notify_index_out, uint32_t *info32_out,
                     uint16_t *info16_out, uint32_t *more_events_out)
{
#if defined(HAVE_LIBDRM_NVIDIA)
   if (!dev || !dev->nvdev)
      return -EINVAL;
   return nvidia_rm_get_event_data(dev->nvdev, h_object_out, notify_index_out,
                                   info32_out, info16_out, more_events_out);
#else
   (void)dev; (void)h_object_out; (void)notify_index_out; (void)info32_out;
   (void)info16_out; (void)more_events_out;
   return -ENOSYS;
#endif
}

int
nv_rm_alloc_event_os(struct nv_rm_device *dev, uint32_t h_parent,
                     uint32_t h_src_resource, int os_event_fd,
                     uint32_t notify_index, uint32_t *h_event_out)
{
#if defined(HAVE_LIBDRM_NVIDIA)
   if (!dev || !dev->nvdev || !h_event_out)
      return -EINVAL;
   return nvidia_rm_alloc_event_os(dev->nvdev, h_parent, h_src_resource,
                                   os_event_fd, notify_index, h_event_out);
#else
   (void)dev; (void)h_parent; (void)h_src_resource; (void)os_event_fd;
   (void)notify_index; (void)h_event_out;
   return -ENOSYS;
#endif
}

int
nv_rm_event_set_notification(struct nv_rm_device *dev, uint32_t h_subdevice,
                             uint32_t event, uint32_t action,
                             bool notify_state, uint32_t info32,
                             uint16_t info16)
{
#if defined(HAVE_LIBDRM_NVIDIA)
   if (!dev || !dev->nvdev)
      return -EINVAL;
   if (!h_subdevice)
      h_subdevice = nv_rm_device_subdevice_handle(dev);
   if (!h_subdevice)
      return -ENODEV;
   return nvidia_rm_event_set_notification(dev->nvdev, h_subdevice, event,
                                           action, notify_state, info32,
                                           info16);
#else
   (void)dev; (void)h_subdevice; (void)event; (void)action; (void)notify_state;
   (void)info32; (void)info16;
   return -ENOSYS;
#endif
}

int
nv_rm_timer_get_time(struct nv_rm_device *dev, uint64_t *time_nsec_out)
{
#if defined(HAVE_LIBDRM_NVIDIA)
   if (!dev || !dev->nvdev)
      return -EINVAL;
   return nvidia_rm_timer_get_time(dev->nvdev, time_nsec_out);
#else
   (void)dev; (void)time_nsec_out;
   return -ENOSYS;
#endif
}

int
nv_rm_export_object_to_fd(struct nv_rm_device *dev, uint32_t h_parent,
                          uint32_t h_object, int *fd_inout, uint32_t flags)
{
#if defined(HAVE_LIBDRM_NVIDIA)
   if (!dev || !dev->nvdev || !h_object || !fd_inout)
      return -EINVAL;
   return nvidia_rm_export_object_to_fd(dev->nvdev, h_parent, h_object,
                                        fd_inout, flags);
#else
   (void)dev; (void)h_parent; (void)h_object; (void)fd_inout; (void)flags;
   return -ENOSYS;
#endif
}

int
nv_rm_import_object_from_fd(struct nv_rm_device *dev, uint32_t h_parent,
                            int import_fd, uint32_t *h_object_out)
{
#if defined(HAVE_LIBDRM_NVIDIA)
   if (!dev || !dev->nvdev || !h_object_out)
      return -EINVAL;
   return nvidia_rm_import_object_from_fd(dev->nvdev, h_parent, import_fd,
                                          h_object_out);
#else
   (void)dev; (void)h_parent; (void)import_fd; (void)h_object_out;
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

   /* Map into device VASpace when requested or when VAS is ready (default path
    * for channel push/GPFIFO memory needs real GPU VAs). */
   if (req->map_gpu_va || dev->vas_ready) {
      if (nv_rm_bo_map_gpu_va(bo) != 0) {
         /* Keep BO; gpu_offset may still be usable as RM offset on some paths */
      }
   }
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
   if (bo->gpu_va_mapped && bo->dev && bo->dev->nvdev && bo->dev->h_vaspace) {
      nvidia_rm_unmap_memory_dma(bo->dev->nvdev,
                                 nvidia_device_get_device_handle(bo->dev->nvdev),
                                 bo->dev->h_vaspace,
                                 bo->rm_handle,
                                 bo->dma_offset,
                                 bo->size,
                                 0);
      bo->gpu_va_mapped = false;
   }
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
