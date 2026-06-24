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

struct nv_rm_bo *
nv_rm_bo_alloc(struct nv_rm_device *dev, const struct nv_rm_bo_req *req);

void
nv_rm_bo_free(struct nv_rm_bo *bo);

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
