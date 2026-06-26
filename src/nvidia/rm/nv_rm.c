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
#include <unistd.h>
#include <sys/eventfd.h>
#endif

#include "nv_rm.h"

/* NVOS32 memory type hints from open-gpu-kernel-modules nvos.h */
#ifndef NVOS32_TYPE_IMAGE
#define NVOS32_TYPE_IMAGE          0
#define NVOS32_TYPE_DEPTH          1
#define NVOS32_TYPE_TEXTURE        2
#define NVOS32_TYPE_VIDEO          3
#define NVOS32_TYPE_DMA            6
#define NVOS32_TYPE_SHADER_PROGRAM 11
#define NVOS32_TYPE_NOTIFIER       13
#define NVOS32_TYPE_STENCIL        16
#endif

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
   /* tick101: 2D surface metadata when allocated via memory_alloc_ex */
   uint32_t width;
   uint32_t height;
   int32_t pitch;
   bool surface_2d;
   bool direct_rm_alloc;     /* true if rm_handle from memory_alloc_ex (no nvbo) */
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
      /* tick102/104: GR topology + QMD limits */
      dev->info.max_warps_per_sm = gi.max_warps_per_sm;
      dev->info.thread_stack_scaling = gi.thread_stack_scaling;
      dev->info.max_threads_per_warp = gi.max_threads_per_warp;
      dev->info.max_sp_per_sm = gi.max_sp_per_sm;
      dev->info.gpu_core_count = gi.gpu_core_count;
      dev->info.subdevice_instance = gi.subdevice_instance;
      dev->info.subdevice_count = gi.subdevice_count ? gi.subdevice_count : 1;
      /* tick107: virtualization / GRID policy (affects alloc + doorbell assumptions) */
      dev->info.virtualization_mode = gi.virtualization_mode;
      dev->info.is_grid_build = gi.is_grid_build;
      dev->info.vram_size_bytes = gi.fb_size;
      dev->info.vram_usable_bytes = gi.fb_usable ? gi.fb_usable : gi.fb_size;
      /* tick101: BAR1/heap/ECC for mapping policy */
      dev->info.fb_heap_size = gi.fb_heap_size;
      dev->info.fb_heap_start = gi.fb_heap_start;
      dev->info.bar1_size = gi.bar1_size;
      dev->info.bar1_avail_size = gi.bar1_avail_size;
      dev->info.fbpa_ecc_enabled = gi.fbpa_ecc_enabled;
      if (!dev->info.sysmem_visible_bytes && gi.bar1_size)
         dev->info.sysmem_visible_bytes = gi.bar1_avail_size ?
            gi.bar1_avail_size : gi.bar1_size;
      /* tick96: refined FB/PCI/page-size probe from libdrm refresh */
      dev->info.fb_region_count = gi.fb_region_count;
      dev->info.fb_region0_base = gi.fb_region0_base;
      dev->info.fb_region0_limit = gi.fb_region0_limit;
      dev->info.max_page_size = gi.max_page_size;
      dev->info.rm_pci_device_id = gi.rm_pci_device_id;
      dev->info.rm_pci_subsystem_id = gi.rm_pci_subsystem_id;
      dev->info.rm_pci_revision_id = gi.rm_pci_revision_id;
      /* tick98: RM build/version from NV0000 system probe */
      dev->info.rm_changelist = gi.rm_changelist;
      dev->info.rm_official_cl = gi.rm_official_cl;
      dev->info.rm_platform_type = gi.rm_platform_type;
      memcpy(dev->info.rm_driver_version, gi.rm_driver_version,
             sizeof(dev->info.rm_driver_version));
      memcpy(dev->info.rm_build_branch, gi.rm_build_branch,
             sizeof(dev->info.rm_build_branch));
      /* tick99: GPU UUID/GID */
      memcpy(dev->info.gpu_uuid, gi.gpu_uuid, sizeof(dev->info.gpu_uuid));
      memcpy(dev->info.gpu_gid_binary, gi.gpu_gid_binary,
             sizeof(dev->info.gpu_gid_binary));
      dev->info.gpu_gid_binary_len = gi.gpu_gid_binary_len;
      if (!dev->info.pci_device_id && gi.rm_pci_device_id)
         dev->info.pci_device_id = gi.rm_pci_device_id & 0xffffu;
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
   uint32_t big_page = 0;
   uint32_t flags = NV_VASPACE_ALLOCATION_FLAGS_NONE;
   uint64_t va_size = 0, va_base = 0;

   if (!dev || !dev->nvdev)
      return -EINVAL;
   if (dev->vas_ready && dev->h_vaspace)
      return 0;

   /* tick97: wire probe max_page_size into FERMI_VASPACE_A bigPageSize */
   big_page = nvidia_rm_vaspace_normalize_big_page(dev->info.max_page_size);
   /*
    * Private VAS: allow PTE fallback into sysmem if FB PTE heap is tight;
    * on Turing+ with UVM-style faulting, enable page faulting when we have
    * a non-default big page (implies real GPU probe succeeded).
    */
   flags = NV_VASPACE_ALLOCATION_FLAGS_RETRY_PTE_ALLOC_IN_SYS;
   if (big_page != 0 && dev->info.architecture >= 0x160 /* Turing-ish */)
      flags |= NV_VASPACE_ALLOCATION_FLAGS_ENABLE_PAGE_FAULTING;

   /* Default: let RM pick size/base (pass 0); index = create new private VAS */
   ret = nvidia_rm_vaspace_alloc(dev->nvdev, &h_vas,
                                 NV_VASPACE_ALLOCATION_INDEX_GPU_NEW,
                                 flags, 0, 0, big_page, &va_size, &va_base);
   if (ret != 0 && (flags & NV_VASPACE_ALLOCATION_FLAGS_ENABLE_PAGE_FAULTING)) {
      /* Retry without page-faulting if RM rejects the flag combination */
      flags &= ~NV_VASPACE_ALLOCATION_FLAGS_ENABLE_PAGE_FAULTING;
      h_vas = 0;
      ret = nvidia_rm_vaspace_alloc(dev->nvdev, &h_vas,
                                    NV_VASPACE_ALLOCATION_INDEX_GPU_NEW,
                                    flags, 0, 0, big_page, &va_size, &va_base);
   }
   if (ret != 0 && big_page != 0) {
      /* Retry with RM-default big page if normalized size was rejected */
      h_vas = 0;
      ret = nvidia_rm_vaspace_alloc(dev->nvdev, &h_vas,
                                    NV_VASPACE_ALLOCATION_INDEX_GPU_NEW,
                                    NV_VASPACE_ALLOCATION_FLAGS_RETRY_PTE_ALLOC_IN_SYS,
                                    0, 0, 0, &va_size, &va_base);
   }
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
nv_rm_bo_map_gpu_va_flags(struct nv_rm_bo *bo, uint32_t os46_flags)
{
#if defined(HAVE_LIBDRM_NVIDIA)
   struct nv_rm_device *dev;
   uint64_t dma_off = 0;
   uint32_t h_dev;
   int ret;
   uint32_t flags_used = 0;

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
   if (!dev->h_vaspace)
      return -ENODEV;

   h_dev = nvidia_device_get_device_handle(dev->nvdev);
   if (!h_dev)
      return -ENODEV;

   if (os46_flags != 0) {
      ret = nvidia_rm_map_memory_dma(dev->nvdev, h_dev, dev->h_vaspace,
                                     bo->rm_handle, 0, bo->size,
                                     os46_flags, &dma_off);
      flags_used = os46_flags;
      /* tick113: explicit flags failed — fall back to auto BAR1 ladder */
      if (ret != 0) {
         ret = nvidia_rm_map_memory_dma_auto_bar1(
            dev->nvdev, h_dev, dev->h_vaspace, bo->rm_handle, 0, bo->size,
            dev->info.max_page_size, dev->info.bar1_avail_size, &dma_off,
            &flags_used);
      }
   } else {
      /* tick103: BAR1-aware auto page-size ladder from probe */
      ret = nvidia_rm_map_memory_dma_auto_bar1(dev->nvdev, h_dev, dev->h_vaspace,
                                               bo->rm_handle, 0, bo->size,
                                               dev->info.max_page_size,
                                               dev->info.bar1_avail_size,
                                               &dma_off, &flags_used);
   }
   /* tick113: last resort — 4K page size only (guest/vGPU sometimes rejects big pages) */
   if (ret != 0) {
      uint32_t fl4k = NVOS46_FLAGS_PAGE_SIZE_4KB_SHL;
      if (fl4k != os46_flags) {
         ret = nvidia_rm_map_memory_dma(dev->nvdev, h_dev, dev->h_vaspace,
                                        bo->rm_handle, 0, bo->size, fl4k,
                                        &dma_off);
         if (ret == 0)
            flags_used = fl4k;
      }
   }
   if (ret != 0)
      return ret;

   (void)flags_used;
   bo->dma_offset = dma_off;
   bo->gpu_offset = dma_off;
   bo->gpu_va_mapped = true;
   return 0;
#else
   (void)bo;
   (void)os46_flags;
   return -ENOSYS;
#endif
}

int
nv_rm_bo_map_gpu_va(struct nv_rm_bo *bo)
{
   return nv_rm_bo_map_gpu_va_flags(bo, 0); /* 0 => auto ladder */
}

uint32_t
nv_rm_device_os46_page_size_sel(struct nv_rm_device *dev, uint64_t map_length)
{
#if defined(HAVE_LIBDRM_NVIDIA)
   uint64_t max_ps = 0;
   uint64_t bar1 = 0;
   if (dev) {
      max_ps = dev->info.max_page_size;
      bar1 = dev->info.bar1_avail_size;
   }
   return nvidia_rm_os46_pick_page_size_bar1(max_ps, map_length, bar1);
#else
   (void)dev;
   (void)map_length;
   return 0; /* NVOS46_FLAGS_PAGE_SIZE_DEFAULT */
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

int
nv_rm_share_object_all_dup(struct nv_rm_device *dev, uint32_t h_object)
{
#if defined(HAVE_LIBDRM_NVIDIA)
   if (!dev || !dev->nvdev || !h_object)
      return -EINVAL;
   return nvidia_rm_share_object_all_dup(dev->nvdev, h_object);
#else
   (void)dev;
   (void)h_object;
   return -ENOSYS;
#endif
}

int
nv_rm_probe_aux_paths(struct nv_rm_device *dev,
                      struct nv_rm_aux_probe_result *out)
{
#if defined(HAVE_LIBDRM_NVIDIA)
   struct nv_rm_aux_probe_result r;
   uint32_t h_dev, h_sub;
   int efd = -1;
   int xfd = -1;
   uint32_t h_ev = 0;
   int first_err = 0;

   memset(&r, 0, sizeof(r));
   r.eventfd_rc = -ENOSYS;
   r.share_rc = -ENOSYS;
   r.export_rc = -ENOSYS;
   r.timer_rc = -ENOSYS;
   r.eventfd_fd = -1;
   r.export_fd = -1;
   r.h_event = 0;

   if (!dev || !dev->nvdev)
      return -EINVAL;
   if (!out)
      return -EINVAL;

   h_dev = nv_rm_device_device_handle(dev);
   h_sub = nv_rm_device_subdevice_handle(dev);

   /* Timer: always cheap when subdevice exists */
   r.timer_rc = nv_rm_timer_get_time(dev, &r.time_nsec);
   if (r.timer_rc != 0 && !first_err)
      first_err = r.timer_rc;

   /* NVOS57 share on device object (best-effort; may fail without policy support) */
   if (h_dev) {
      r.share_rc = nv_rm_share_object_all_dup(dev, h_dev);
      if (r.share_rc != 0 && !first_err)
         first_err = r.share_rc;
   } else {
      r.share_rc = -ENODEV;
   }

   /* Export device object to Unix FD (may require prior share on some RM builds) */
   if (h_dev) {
      xfd = -1;
      r.export_rc = nv_rm_export_object_to_fd(dev, 0, h_dev, &xfd, 0);
      if (r.export_rc == 0 && xfd >= 0) {
         r.export_fd = xfd;
         close(xfd);
         r.export_fd = -1;
      } else if (r.export_rc != 0 && !first_err) {
         first_err = r.export_rc;
      }
   } else {
      r.export_rc = -ENODEV;
   }

   /* eventfd + NV01_EVENT_OS_EVENT on subdevice (RC notifier index) */
   efd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
   if (efd < 0) {
      r.eventfd_rc = -errno;
      if (!first_err)
         first_err = r.eventfd_rc;
   } else if (!h_sub) {
      r.eventfd_rc = -ENODEV;
      close(efd);
      efd = -1;
   } else {
      r.eventfd_fd = efd;
      r.eventfd_rc = nv_rm_alloc_event_os(dev, h_sub, h_dev ? h_dev : h_sub,
                                          efd, NV2080_NOTIFIERS_RC, &h_ev);
      if (r.eventfd_rc == 0 && h_ev) {
         r.h_event = h_ev;
         /* Arm then disable notification so we leave RM quiet */
         (void)nv_rm_event_set_notification(
            dev, h_sub, NV2080_NOTIFIERS_RC,
            NV2080_CTRL_EVENT_SET_NOTIFICATION_ACTION_SINGLE, true, 0, 0);
         (void)nv_rm_event_set_notification(
            dev, h_sub, NV2080_NOTIFIERS_RC,
            NV2080_CTRL_EVENT_SET_NOTIFICATION_ACTION_DISABLE, false, 0, 0);
         (void)nv_rm_free_object(dev, h_sub, h_ev);
         r.h_event = 0;
      } else if (r.eventfd_rc != 0 && !first_err) {
         first_err = r.eventfd_rc;
      }
      close(efd);
      r.eventfd_fd = -1;
   }

   *out = r;
   return first_err;
#else
   (void)dev;
   if (out) {
      memset(out, 0, sizeof(*out));
      out->eventfd_rc = out->share_rc = out->export_rc = out->timer_rc = -ENOSYS;
      out->eventfd_fd = out->export_fd = -1;
   }
   return -ENOSYS;
#endif
}

#if defined(HAVE_LIBDRM_NVIDIA)
static struct nv_rm_bo *
nv_rm_bo_alloc_via_memory_ex(struct nv_rm_device *dev,
                             const struct nv_rm_bo_req *req)
{
   struct nv_rm_bo *bo;
   uint32_t h_mem = 0;
   uint32_t h_class;
   uint32_t type;
   uint32_t flags;
   uint32_t attr, attr2;
   uint32_t h_vas = 0;
   uint64_t size, align, off = 0, lim = 0;
   int32_t pitch_in, pitch_out = 0;
   int ret;

   if (!dev || !dev->nvdev || !req)
      return NULL;

   size = req->size;
   if (req->width && req->height) {
      int32_t p = req->pitch;
      if (p <= 0)
         p = (int32_t)((req->width * 4u + 255u) & ~255u); /* A8R8G8B8 default pitch */
      if (size < (uint64_t)p * (uint64_t)req->height)
         size = (uint64_t)p * (uint64_t)req->height;
      pitch_in = req->pitch; /* 0 lets RM refine */
   } else {
      pitch_in = 0;
   }
   if (size == 0)
      return NULL;

   align = req->alignment ? req->alignment : 256;
   type = req->rm_type;
   if (!type)
      type = (req->width && req->height) ? NVOS32_TYPE_IMAGE : NVOS32_TYPE_DMA;

   flags = NVOS32_ALLOC_FLAGS_ALIGNMENT_FORCE |
           NVOS32_ALLOC_FLAGS_MAP_NOT_REQUIRED;
   if (req->vram)
      flags |= NVOS32_ALLOC_FLAGS_PERSISTENT_VIDMEM;
   if (req->no_scanout)
      flags |= NVOS32_ALLOC_FLAGS_NO_SCANOUT;

   if (req->vram) {
      h_class = NV01_MEMORY_LOCAL_USER;
      {
         /* tick105: prefer BIG/HUGE ATTR page size when GPU max_page_size allows */
         uint32_t pgsz = nvidia_rm_os32_pick_attr_page_size(dev->info.max_page_size,
                                                            size);
         if (req->blocklinear && req->width && req->height) {
            /* block-linear IMAGE/DEPTH; GPU-cacheable preferred for RT/tex */
            attr = NV_OS32_ATTR_VIDMEM_UNCACHED_BL_NONCONTIG_PGSZ(pgsz);
            attr2 = NV_OS32_ATTR2_GPU_CACHEABLE_YES_VAL;
            align = req->alignment ? req->alignment : 512;
            pitch_in = 0; /* BL: pitch is RM/gob internal, not linear row stride */
         } else {
            attr = NV_OS32_ATTR_VIDMEM_UNCACHED_NONCONTIG_PGSZ(pgsz);
            attr2 = NV_OS32_ATTR2_GPU_CACHEABLE_NO_VAL;
         }
      }
   } else {
      /* tick112: PCI WC/uncached non-contig ATTR with GPU-appropriate page size
       * (guest/vGPU sema/rings/push often need non-contig + WC CPU maps). */
      uint32_t pgsz = nvidia_rm_os32_pick_attr_page_size(dev->info.max_page_size,
                                                         size);
      h_class = NV01_MEMORY_SYSTEM;
      attr = nvidia_rm_os32_attr_sysmem_mappable(pgsz, req->cpu_access);
      attr2 = NV_OS32_ATTR2_GPU_CACHEABLE_DEFAULT_VAL;
      /* sysmem BL is unusual; fall through pitch unless caller insists */
      if (req->blocklinear)
         attr |= NV_OS32_DRF_SHL(16, 17, NVOS32_ATTR_FORMAT_BLOCK_LINEAR);
   }

   if (req->map_gpu_va || dev->vas_ready) {
      (void)nv_rm_device_ensure_vaspace(dev);
      h_vas = dev->h_vaspace;
   }

   bo = calloc(1, sizeof(*bo));
   if (!bo)
      return NULL;
   bo->dev = dev;
   bo->direct_rm_alloc = true;

   ret = nvidia_rm_memory_alloc_ex(dev->nvdev, 0, &h_mem, h_class, type, flags,
                                   attr, attr2, req->format,
                                   req->width, req->height, pitch_in,
                                   size, align, h_vas, &off, &lim, &pitch_out);
   if (ret != 0 && req->vram && req->blocklinear) {
      /* retry contig BL then non-BL noncontig */
      attr = NV_OS32_ATTR_VIDMEM_4K_UNCACHED_BL;
      ret = nvidia_rm_memory_alloc_ex(dev->nvdev, 0, &h_mem, h_class, type,
                                      flags, attr, attr2, req->format,
                                      req->width, req->height, pitch_in,
                                      size, align, h_vas, &off, &lim,
                                      &pitch_out);
   }
   if (ret != 0 && req->vram) {
      /* retry strict contig / default cache (pitch or last resort) */
      attr = req->blocklinear ? NV_OS32_ATTR_VIDMEM_4K_UNCACHED_BL
                              : NV_OS32_ATTR_VIDMEM_4K_UNCACHED;
      ret = nvidia_rm_memory_alloc_ex(dev->nvdev, 0, &h_mem, h_class, type,
                                      flags, attr, attr2, req->format,
                                      req->width, req->height, pitch_in,
                                      size, align, h_vas, &off, &lim,
                                      &pitch_out);
   }
   /* tick112: sysmem retry ladder — 4K WC/uncached then contig defaults */
   if (ret != 0 && !req->vram) {
      attr = req->cpu_access ? NV_OS32_ATTR_PCI_4K_WRITECOMBINE
                             : NV_OS32_ATTR_PCI_4K_UNCACHED;
      if (req->blocklinear)
         attr |= NV_OS32_DRF_SHL(16, 17, NVOS32_ATTR_FORMAT_BLOCK_LINEAR);
      ret = nvidia_rm_memory_alloc_ex(dev->nvdev, 0, &h_mem, h_class, type,
                                      flags, attr, attr2, req->format,
                                      req->width, req->height, pitch_in,
                                      size, align, h_vas, &off, &lim,
                                      &pitch_out);
   }
   if (ret != 0 && !req->vram && req->cpu_access) {
      /* WC failed: uncached non-contig 4K often works for sema/notifier BOs */
      attr = NV_OS32_ATTR_PCI_4K_UNCACHED;
      ret = nvidia_rm_memory_alloc_ex(dev->nvdev, 0, &h_mem, h_class, type,
                                      flags, attr, attr2, req->format,
                                      req->width, req->height, pitch_in,
                                      size, align, h_vas, &off, &lim,
                                      &pitch_out);
   }
   if (ret != 0) {
      free(bo);
      return NULL;
   }

   bo->rm_handle = h_mem;
   bo->size = lim ? (lim + 1) : size;
   if (lim && lim + 1 > size)
      bo->size = lim + 1;
   bo->gpu_offset = off;
   bo->width = req->width;
   bo->height = req->height;
   bo->pitch = pitch_out ? pitch_out : pitch_in;
   bo->surface_2d = (req->width && req->height);

   if (req->map_gpu_va || dev->vas_ready)
      (void)nv_rm_bo_map_gpu_va(bo);
   return bo;
}
#endif

struct nv_rm_bo *
nv_rm_bo_alloc(struct nv_rm_device *dev, const struct nv_rm_bo_req *req)
{
#if defined(HAVE_LIBDRM_NVIDIA)
   struct nv_rm_bo *bo;
   struct nvidia_bo_alloc_request areq;
   struct nvidia_bo_metadata meta;
   int ret;

   if (!dev || !dev->nvdev || !req || (req->size == 0 && !(req->width && req->height)))
      return NULL;

   /* tick101: 2D/surface path prefers memory_alloc_ex (correct ATTR + pitch) */
   if (req->width && req->height) {
      bo = nv_rm_bo_alloc_via_memory_ex(dev, req);
      if (bo)
         return bo;
      /* fall through to linear size estimate */
   }

   bo = calloc(1, sizeof(*bo));
   if (!bo)
      return NULL;
   bo->dev = dev;

   memset(&areq, 0, sizeof(areq));
   areq.size = req->size;
   if (!areq.size && req->width && req->height) {
      int32_t p = req->pitch > 0 ? req->pitch
                                 : (int32_t)((req->width * 4u + 255u) & ~255u);
      areq.size = (uint64_t)p * (uint64_t)req->height;
   }
   areq.alignment = req->alignment;
   areq.domain = req->vram ? NVIDIA_BO_DOMAIN_VRAM : NVIDIA_BO_DOMAIN_GART;
   areq.flags = 0;
   if (req->cpu_access)
      areq.flags |= NVIDIA_BO_FLAGS_CPU_ACCESS;
   if (req->no_scanout)
      areq.flags |= NVIDIA_BO_FLAGS_NO_SCANOUT;
   if (req->rm_type)
      areq.rm_type = req->rm_type;

   ret = nvidia_bo_alloc(dev->nvdev, &areq, &bo->nvbo);
   if (ret != 0) {
      free(bo);
      return NULL;
   }

   memset(&meta, 0, sizeof(meta));
   nvidia_bo_query_metadata(bo->nvbo, &meta);
   bo->size = meta.aligned_size ? meta.aligned_size : areq.size;
   bo->gpu_offset = meta.gpu_offset;
   bo->rm_handle = meta.rm_handle;
   bo->width = req->width;
   bo->height = req->height;
   bo->pitch = req->pitch;
   bo->surface_2d = (req->width && req->height);

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

struct nv_rm_bo *
nv_rm_bo_alloc_2d(struct nv_rm_device *dev, uint32_t width, uint32_t height,
                  int32_t *pitch_inout, bool vram, bool cpu_access,
                  bool map_gpu_va, uint32_t rm_type, uint32_t format)
{
   struct nv_rm_bo_req req;
   struct nv_rm_bo *bo;
   int32_t pitch;

   if (!dev || !width || !height)
      return NULL;
   pitch = pitch_inout ? *pitch_inout : 0;
   memset(&req, 0, sizeof(req));
   req.width = width;
   req.height = height;
   req.pitch = pitch;
   req.vram = vram;
   req.cpu_access = cpu_access;
   req.map_gpu_va = map_gpu_va;
   req.no_scanout = true;
   req.rm_type = rm_type;
   req.format = format;
   if (pitch > 0)
      req.size = (uint64_t)pitch * (uint64_t)height;
   else
      req.size = (uint64_t)((width * 4u + 255u) & ~255u) * (uint64_t)height;
   req.alignment = 256;
   bo = nv_rm_bo_alloc(dev, &req);
   if (bo && pitch_inout)
      *pitch_inout = bo->pitch;
   return bo;
}

struct nv_rm_bo *
nv_rm_bo_alloc_bl(struct nv_rm_device *dev, uint32_t width, uint32_t height,
                  uint64_t size_bytes, uint8_t gobs_h, bool vram,
                  bool map_gpu_va, uint32_t rm_type, uint32_t format)
{
   struct nv_rm_bo_req req;
   struct nv_rm_bo *bo;

   if (!dev || !width || !height || !size_bytes)
      return NULL;
   memset(&req, 0, sizeof(req));
   req.width = width;
   req.height = height;
   req.pitch = 0;
   req.size = size_bytes;
   req.alignment = 512;
   req.vram = vram;
   req.cpu_access = false; /* BL typically GPU-only; staging is linear */
   req.map_gpu_va = map_gpu_va;
   req.no_scanout = true;
   req.rm_type = rm_type ? rm_type : NVOS32_TYPE_IMAGE;
   req.format = format;
   req.blocklinear = true;
   req.gobs_height = gobs_h ? gobs_h : 4;
   bo = nv_rm_bo_alloc(dev, &req);
   if (!bo && vram) {
      req.vram = false;
      req.cpu_access = true;
      bo = nv_rm_bo_alloc(dev, &req);
   }
   return bo;
}

struct nv_rm_bo *
nv_rm_bo_alloc_typed(struct nv_rm_device *dev, uint64_t size_bytes,
                     uint32_t rm_type, bool vram, bool cpu_access,
                     bool map_gpu_va)
{
   struct nv_rm_bo_req req;

   if (!dev || !size_bytes)
      return NULL;
   memset(&req, 0, sizeof(req));
   req.size = size_bytes;
   req.alignment = 4096;
   req.vram = vram;
   req.cpu_access = cpu_access;
   req.map_gpu_va = map_gpu_va;
   req.no_scanout = true;
   req.rm_type = rm_type ? rm_type : NVOS32_TYPE_DMA;
   return nv_rm_bo_alloc(dev, &req);
}

struct nv_rm_bo *
nv_rm_bo_alloc_typed_auto(struct nv_rm_device *dev, uint64_t size_bytes,
                          uint32_t rm_type, bool prefer_vram, bool map_gpu_va)
{
   struct nv_rm_bo *bo;
   bool try_vram = prefer_vram;

   if (!dev || !size_bytes)
      return NULL;
   /* Under guest/GRID virt, prefer sysmem for host-mappable work buffers first */
   if (prefer_vram && nv_device_info_prefer_sysmem_alloc(&dev->info))
      try_vram = false;

   if (try_vram) {
      bo = nv_rm_bo_alloc_typed(dev, size_bytes, rm_type, true, false, map_gpu_va);
      if (!bo)
         bo = nv_rm_bo_alloc_typed(dev, size_bytes, rm_type, false, true,
                                   map_gpu_va);
      return bo;
   }
   bo = nv_rm_bo_alloc_typed(dev, size_bytes, rm_type, false, true, map_gpu_va);
   if (!bo && prefer_vram)
      bo = nv_rm_bo_alloc_typed(dev, size_bytes, rm_type, true, false, map_gpu_va);
   return bo;
}

struct nv_rm_bo *
nv_rm_bo_alloc_notifier(struct nv_rm_device *dev, uint64_t size_bytes)
{
   if (!size_bytes)
      size_bytes = 4096;
   return nv_rm_bo_alloc_typed(dev, size_bytes, NVOS32_TYPE_NOTIFIER,
                               false, true, true);
}

struct nv_rm_bo *
nv_rm_bo_alloc_shader(struct nv_rm_device *dev, uint64_t size_bytes,
                      bool map_gpu_va)
{
   struct nv_rm_bo *bo;
   if (!size_bytes)
      return NULL;
   /* tick111: virt-aware domain order via alloc_typed_auto (prefer vidmem) */
   bo = nv_rm_bo_alloc_typed_auto(dev, size_bytes, NVOS32_TYPE_SHADER_PROGRAM,
                                  true, map_gpu_va);
   if (!bo)
      bo = nv_rm_bo_alloc_typed(dev, size_bytes, NVOS32_TYPE_SHADER_PROGRAM,
                                false, true, map_gpu_va);
   return bo;
}

struct nv_rm_bo *
nv_rm_bo_alloc_video_2d(struct nv_rm_device *dev, uint32_t width,
                        uint32_t height, int32_t *pitch_inout, bool vram,
                        bool map_gpu_va)
{
   return nv_rm_bo_alloc_2d(dev, width, height, pitch_inout, vram,
                            !vram /* cpu_access for sysmem */, map_gpu_va,
                            NVOS32_TYPE_VIDEO, 0);
}

struct nv_rm_bo *
nv_rm_bo_alloc_depth_2d(struct nv_rm_device *dev, uint32_t width,
                        uint32_t height, int32_t *pitch_inout, bool vram,
                        bool map_gpu_va)
{
   return nv_rm_bo_alloc_depth_2d_ex(dev, width, height, pitch_inout, vram,
                                     map_gpu_va, false);
}

struct nv_rm_bo *
nv_rm_bo_alloc_depth_2d_ex(struct nv_rm_device *dev, uint32_t width,
                           uint32_t height, int32_t *pitch_inout, bool vram,
                           bool map_gpu_va, bool blocklinear)
{
   struct nv_rm_bo *bo = NULL;
   struct nv_rm_bo_req req;
   int32_t pitch = pitch_inout && *pitch_inout > 0 ? *pitch_inout
                                                   : (int32_t)(width * 4u);
   bool cpu = !vram;
   bool try_vram = vram;

   if (!dev || !width || !height)
      return NULL;

   if (blocklinear) {
      /* BL depth prefers vidmem; try DEPTH then IMAGE with blocklinear flag */
      memset(&req, 0, sizeof(req));
      req.width = width;
      req.height = height;
      req.pitch = 0; /* BL: RM/gob internal */
      req.size = (uint64_t)width * (uint64_t)height * 4u;
      req.alignment = 512;
      req.vram = true;
      req.cpu_access = false;
      req.no_scanout = true;
      req.map_gpu_va = map_gpu_va;
      req.rm_type = NVOS32_TYPE_DEPTH;
      req.blocklinear = true;
      req.gobs_height = 4; /* 16 gobs height default (tick103 convention) */
      bo = nv_rm_bo_alloc(dev, &req);
      if (!bo) {
         req.rm_type = NVOS32_TYPE_IMAGE;
         bo = nv_rm_bo_alloc(dev, &req);
      }
      if (!bo && !vram) {
         /* guest may only have sysmem — fall through to pitch */
         blocklinear = false;
      } else if (bo) {
         if (pitch_inout)
            *pitch_inout = nv_rm_bo_pitch(bo);
         return bo;
      }
   }

   /* Prefer DEPTH type; STENCIL for S8-only paths; IMAGE as last resort */
   bo = nv_rm_bo_alloc_2d(dev, width, height, &pitch, try_vram, cpu, map_gpu_va,
                          NVOS32_TYPE_DEPTH, 0);
   if (!bo)
      bo = nv_rm_bo_alloc_2d(dev, width, height, &pitch, try_vram, cpu,
                             map_gpu_va, NVOS32_TYPE_STENCIL, 0);
   if (!bo)
      bo = nv_rm_bo_alloc_2d(dev, width, height, &pitch, try_vram, cpu,
                             map_gpu_va, NVOS32_TYPE_IMAGE, 0);
   if (!bo && try_vram) {
      pitch = pitch_inout && *pitch_inout > 0 ? *pitch_inout
                                              : (int32_t)(width * 4u);
      bo = nv_rm_bo_alloc_2d(dev, width, height, &pitch, false, true,
                             map_gpu_va, NVOS32_TYPE_DEPTH, 0);
      if (!bo)
         bo = nv_rm_bo_alloc_2d(dev, width, height, &pitch, false, true,
                                map_gpu_va, NVOS32_TYPE_IMAGE, 0);
   }
   if (bo && pitch_inout)
      *pitch_inout = nv_rm_bo_pitch(bo) ? nv_rm_bo_pitch(bo) : pitch;
   return bo;
}

int32_t
nv_rm_bo_pitch(const struct nv_rm_bo *bo)
{
   return bo ? bo->pitch : 0;
}

uint32_t
nv_rm_bo_width(const struct nv_rm_bo *bo)
{
   return bo ? bo->width : 0;
}

uint32_t
nv_rm_bo_height(const struct nv_rm_bo *bo)
{
   return bo ? bo->height : 0;
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
   if (bo->mapped && bo->nvbo)
      nvidia_bo_cpu_unmap(bo->nvbo);
   if (bo->nvbo)
      nvidia_bo_free(bo->nvbo);
   else if (bo->direct_rm_alloc && bo->dev && bo->dev->nvdev && bo->rm_handle) {
      /* Direct RmAlloc path: free RM object under device */
      uint32_t h_dev = nvidia_device_get_device_handle(bo->dev->nvdev);
      if (h_dev)
         (void)nvidia_rm_free(bo->dev->nvdev, h_dev, bo->rm_handle);
   }
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
