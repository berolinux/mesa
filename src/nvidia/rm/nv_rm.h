/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Mesa-side RM client. Uses libdrm_nvidia when available, with a thin
 * abstraction so the Gallium and Vulkan drivers share one code path.
 */

#ifndef NV_RM_H
#define NV_RM_H

#include <stdbool.h>
#include <stdint.h>

#include "nv_device_info.h"

#ifdef __cplusplus
extern "C" {
#endif

struct nv_rm_device;
struct nv_rm_bo;

struct nv_rm_bo_req {
   uint64_t size;
   uint64_t alignment;
   bool vram;
   bool cpu_access;
   bool no_scanout;
   /** If true, map BO into device VASpace via NVOS46 after alloc (needs VAS). */
   bool map_gpu_va;
   /**
    * tick101: optional 2D/surface alloc via NV_MEMORY_ALLOCATION_PARAMS
    * (nvidia_rm_memory_alloc_ex).  When width & height are non-zero, mesa
    * allocates as pitch surface (IMAGE/DEPTH/VIDEO type); size is still
    * used as minimum byte size (max(size, pitch*height) if pitch set).
    */
   uint32_t width;
   uint32_t height;
   int32_t pitch;       /* 0 = RM computes; returned via nv_rm_bo_pitch() */
   uint32_t rm_type;    /* NVOS32_TYPE_*; 0 = DMA or IMAGE when 2D */
   uint32_t format;     /* surface format dword when applicable */
   /** tick103: request block-linear layout (NVOS32_ATTR_FORMAT_BLOCK_LINEAR) */
   bool blocklinear;
   uint8_t gobs_height; /* log2 gobs in height for BL (0 = default 4 => 16 gobs) */
};

struct nv_rm_device *
nv_rm_device_open(int drm_fd, int gpu_index);

void
nv_rm_device_close(struct nv_rm_device *dev);

/** FERMI_VASPACE_A handle (0 if not allocated / using default device VAS) */
uint32_t
nv_rm_device_vaspace_handle(struct nv_rm_device *dev);

/** Usermode doorbell CPU mapping (VOLTA_USERMODE_A); NULL if unavailable */
volatile void *
nv_rm_device_usermode_map(struct nv_rm_device *dev);

/** Ensure device has an explicit VASpace; returns 0 on success or if already ok */
int
nv_rm_device_ensure_vaspace(struct nv_rm_device *dev);

/** Ensure usermode doorbell object is allocated and mapped */
int
nv_rm_device_ensure_usermode(struct nv_rm_device *dev);

/** Map an existing BO into the device VASpace; updates BO gpu_offset on success */
int
nv_rm_bo_map_gpu_va(struct nv_rm_bo *bo);

/**
 * tick102: NVOS46 map with explicit flags (or 0 = use auto page-size ladder
 * from device max_page_size).  Unmaps prior VA mapping if present.
 */
int
nv_rm_bo_map_gpu_va_flags(struct nv_rm_bo *bo, uint32_t os46_flags);

/**
 * tick102: preferred unshifted NVOS46 PAGE_SIZE selector for a mapping of
 * @map_length using device probe max_page_size (via libdrm helper when built).
 */
uint32_t
nv_rm_device_os46_page_size_sel(struct nv_rm_device *dev, uint64_t map_length);

int
nv_rm_device_fd_ctl(struct nv_rm_device *dev);

int
nv_rm_device_fd_gpu(struct nv_rm_device *dev);

int
nv_rm_device_fd_drm(struct nv_rm_device *dev);

const struct nv_device_info *
nv_rm_device_info(struct nv_rm_device *dev);

uint32_t
nv_rm_device_client_handle(struct nv_rm_device *dev);

uint32_t
nv_rm_device_device_handle(struct nv_rm_device *dev);

uint32_t
nv_rm_device_subdevice_handle(struct nv_rm_device *dev);

int
nv_rm_control(struct nv_rm_device *dev, uint32_t h_object, uint32_t cmd,
              void *params, uint32_t params_size);

int
nv_rm_alloc_object(struct nv_rm_device *dev, uint32_t h_parent,
                   uint32_t *h_object, uint32_t h_class,
                   void *alloc_params, uint32_t alloc_params_size);

int
nv_rm_free_object(struct nv_rm_device *dev, uint32_t h_parent,
                  uint32_t h_object);

/**
 * tick93: Dup object into this client (NVOS55).  h_client_src may equal
 * nv_rm_device_client_handle(dev) for self-dup.
 */
int nv_rm_dup_object(struct nv_rm_device *dev, uint32_t h_parent_dst,
                     uint32_t *h_object_dst, uint32_t h_client_src,
                     uint32_t h_object_src, uint32_t flags);

/** Poll one RM event (NVOS41).  Returns 0 on success, negative errno/NV status. */
int nv_rm_get_event_data(struct nv_rm_device *dev, uint32_t *h_object_out,
                         uint32_t *notify_index_out, uint32_t *info32_out,
                         uint16_t *info16_out, uint32_t *more_events_out);

/**
 * Allocate NV01_EVENT_OS_EVENT under h_parent for h_src_resource + event_fd.
 * notify_index: NV2080_NOTIFIERS_* (optionally OR NV01_EVENT_* flags).
 */
int nv_rm_alloc_event_os(struct nv_rm_device *dev, uint32_t h_parent,
                         uint32_t h_src_resource, int os_event_fd,
                         uint32_t notify_index, uint32_t *h_event_out);

/** NV2080_CTRL_CMD_EVENT_SET_NOTIFICATION on h_subdevice (or device subdevice). */
int nv_rm_event_set_notification(struct nv_rm_device *dev, uint32_t h_subdevice,
                                 uint32_t event, uint32_t action,
                                 bool notify_state, uint32_t info32,
                                 uint16_t info16);

/** tick95: GPU PTIMER nanoseconds (subdevice TIMER_GET_TIME). */
int nv_rm_timer_get_time(struct nv_rm_device *dev, uint64_t *time_nsec_out);

/** Export/import RM object via Unix FD (cross-process/share). */
int nv_rm_export_object_to_fd(struct nv_rm_device *dev, uint32_t h_parent,
                              uint32_t h_object, int *fd_inout,
                              uint32_t flags);
int nv_rm_import_object_from_fd(struct nv_rm_device *dev, uint32_t h_parent,
                                int import_fd, uint32_t *h_object_out);

/**
 * tick97: NVOS57 share policy on an RM object (requires libdrm_nvidia).
 * share_all_dup: convenience RS_SHARE_TYPE_ALL + DUP_OBJECT compose.
 */
int nv_rm_share_object_all_dup(struct nv_rm_device *dev, uint32_t h_object);

/**
 * tick98: non-destructive bring-up probe for event/share/export/timer paths.
 * Does not affect normal driver operation; safe to call from smoke on HW box.
 *
 * eventfd_rc: eventfd(2) + NV01_EVENT_OS_EVENT + EVENT_SET_NOTIFICATION (disable cleanup)
 * share_rc: NVOS57 share_all_dup on device handle
 * export_rc: export device object to FD then close (or best-effort error)
 * timer_rc: TIMER_GET_TIME on subdevice
 * time_nsec_out: optional PTIMER sample when timer_rc == 0
 *
 * Returns 0 if all attempted steps succeeded (or skipped missing pieces);
 * negative = first hard failure. Individual *rc fields always set.
 */
struct nv_rm_aux_probe_result {
   int eventfd_rc;
   int share_rc;
   int export_rc;
   int timer_rc;
   int eventfd_fd;          /* eventfd fd if created (>=0), else -1; caller may close if still open */
   int export_fd;           /* export FD if non-negative; caller should close */
   uint32_t h_event;        /* NV01_EVENT handle if allocated (caller frees via nv_rm_free_object) */
   uint64_t time_nsec;
};

int nv_rm_probe_aux_paths(struct nv_rm_device *dev,
                          struct nv_rm_aux_probe_result *out);

struct nv_rm_bo *
nv_rm_bo_alloc(struct nv_rm_device *dev, const struct nv_rm_bo_req *req);

/**
 * tick101: allocate a 2D pitch surface (color/depth/video) via memory_alloc_ex
 * when libdrm supports it; falls back to linear nv_rm_bo_alloc(size=pitch*h).
 * pitch_inout: in 0 = RM/default; out = allocated pitch on success.
 */
struct nv_rm_bo *
nv_rm_bo_alloc_2d(struct nv_rm_device *dev, uint32_t width, uint32_t height,
                  int32_t *pitch_inout, bool vram, bool cpu_access,
                  bool map_gpu_va, uint32_t rm_type, uint32_t format);

/**
 * tick103: allocate block-linear 2D surface via memory_alloc_ex (BL ATTR).
 * size_bytes: minimum allocation size (GOB-aligned layer); pitch_inout unused
 * for BL (pitch field often 0 / RM-internal). gobs_h: log2 height gobs (4=16).
 */
struct nv_rm_bo *
nv_rm_bo_alloc_bl(struct nv_rm_device *dev, uint32_t width, uint32_t height,
                  uint64_t size_bytes, uint8_t gobs_h, bool vram,
                  bool map_gpu_va, uint32_t rm_type, uint32_t format);

void
nv_rm_bo_free(struct nv_rm_bo *bo);

/** Allocated pitch for 2D surfaces (0 if linear/unknown). */
int32_t nv_rm_bo_pitch(const struct nv_rm_bo *bo);
uint32_t nv_rm_bo_width(const struct nv_rm_bo *bo);
uint32_t nv_rm_bo_height(const struct nv_rm_bo *bo);

void *
nv_rm_bo_map(struct nv_rm_bo *bo);

void
nv_rm_bo_unmap(struct nv_rm_bo *bo);

uint32_t
nv_rm_bo_handle(struct nv_rm_bo *bo);

uint64_t
nv_rm_bo_size(struct nv_rm_bo *bo);

uint64_t
nv_rm_bo_gpu_offset(struct nv_rm_bo *bo);

int
nv_rm_bo_export_dmabuf(struct nv_rm_bo *bo, int *fd_out);

/* Probe helpers */
bool nv_rm_probe_available(void);
int nv_rm_probe_gpu_count(void);

#ifdef __cplusplus
}
#endif

#endif /* NV_RM_H */
