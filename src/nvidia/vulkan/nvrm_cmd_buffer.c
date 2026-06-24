/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Command buffer records into a CPU pushbuffer; submit path kicks GPFIFO.
 */

#include "nvrm_private.h"
#include "nv_3d_methods.h"
#include "nv_copy_methods.h"
#include "nv_shader.h"
#include "nv_qmd.h"
#include "nv_rm.h"

#define NVRM_CMD_PUSH_DWORDS (32 * 1024)
#define NVRM_QMD_SCRATCH_SIZE  4096  /* 256B QMD + padding, 256B aligned */
/* Fallback LMEM BO when device SM count unknown (covers ~2 SMs at min granule) */
#define NVRM_LMEM_SCRATCH_SIZE (2 * NV_LMEM_MIN_PER_SM_BYTES)
/* Push constants CB: 256B min selector size; hold 64 dwords = 256B exactly */
#define NVRM_PUSH_CONST_BO_SIZE  256

VKAPI_ATTR VkResult VKAPI_CALL
nvrm_BeginCommandBuffer(VkCommandBuffer commandBuffer,
                        const VkCommandBufferBeginInfo *pBeginInfo)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   struct nv_rm_bo_req req;
   const struct nv_device_info *info;
   uint32_t sm_count = 2;
   uint64_t lmem_need;
   (void)pBeginInfo;

   if (!cmd->push_bo) {
      memset(&req, 0, sizeof(req));
      req.size = NVRM_CMD_PUSH_DWORDS * 4;
      req.alignment = 4096;
      req.vram = false;
      req.cpu_access = true;
      req.no_scanout = true;
      req.map_gpu_va = true;
      cmd->push_bo = nv_rm_bo_alloc(cmd->device->rm, &req);
      if (!cmd->push_bo)
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;
      cmd->push_map = nv_rm_bo_map(cmd->push_bo);
      cmd->push_dw_cap = NVRM_CMD_PUSH_DWORDS;
   }
   /* Dedicated 256B-aligned QMD scratch for SEND_PCAS address field */
   if (!cmd->qmd_bo && cmd->device && cmd->device->rm) {
      memset(&req, 0, sizeof(req));
      req.size = NVRM_QMD_SCRATCH_SIZE;
      req.alignment = 256;
      req.vram = false;
      req.cpu_access = true;
      req.no_scanout = true;
      req.map_gpu_va = true;
      cmd->qmd_bo = nv_rm_bo_alloc(cmd->device->rm, &req);
   }
   /* Global LMEM backing sized for SM count * per-SM window (conservative spill) */
   info = cmd->device ? cmd->device->info : NULL;
   if (info && info->tpc_count)
      sm_count = info->tpc_count;
   lmem_need = nv_lmem_total_bo_bytes(0 /* min spill */, sm_count);
   if (lmem_need < NVRM_LMEM_SCRATCH_SIZE)
      lmem_need = NVRM_LMEM_SCRATCH_SIZE;
   if (lmem_need > 0x10000000ull) /* 256 MiB hard cap for cmd-buffer alloc */
      lmem_need = 0x10000000ull;
   if (!cmd->lmem_bo && cmd->device && cmd->device->rm) {
      memset(&req, 0, sizeof(req));
      req.size = (uint32_t)lmem_need;
      req.alignment = 4096;
      req.vram = true;
      req.cpu_access = false;
      req.no_scanout = true;
      req.map_gpu_va = true;
      cmd->lmem_bo = nv_rm_bo_alloc(cmd->device->rm, &req);
      if (cmd->lmem_bo)
         cmd->lmem_bo_size = (uint32_t)lmem_need;
   } else if (cmd->lmem_bo && cmd->lmem_bo_size < (uint32_t)lmem_need) {
      /* Re-alloc larger if previous begin used smaller size (rare) */
      nv_rm_bo_free(cmd->lmem_bo);
      cmd->lmem_bo = NULL;
      memset(&req, 0, sizeof(req));
      req.size = (uint32_t)lmem_need;
      req.alignment = 4096;
      req.vram = true;
      req.cpu_access = false;
      req.no_scanout = true;
      req.map_gpu_va = true;
      cmd->lmem_bo = nv_rm_bo_alloc(cmd->device->rm, &req);
      if (cmd->lmem_bo)
         cmd->lmem_bo_size = (uint32_t)lmem_need;
   }
   /* Push-constants CB: CPU-writable + GPU-mapped for LOAD_CONSTANT_BUFFER */
   if (!cmd->push_const_bo && cmd->device && cmd->device->rm) {
      memset(&req, 0, sizeof(req));
      req.size = NVRM_PUSH_CONST_BO_SIZE;
      req.alignment = 256;
      req.vram = false;
      req.cpu_access = true;
      req.no_scanout = true;
      req.map_gpu_va = true;
      cmd->push_const_bo = nv_rm_bo_alloc(cmd->device->rm, &req);
      if (cmd->push_const_bo) {
         cmd->push_const_bo_size = NVRM_PUSH_CONST_BO_SIZE;
         cmd->push_const_map = nv_rm_bo_map(cmd->push_const_bo);
         if (cmd->push_const_map)
            memset(cmd->push_const_map, 0, NVRM_PUSH_CONST_BO_SIZE);
      }
   }

   /* Host-mappable shadow for indirect draw path B (CE copies GPU-only IB) */
   if (!cmd->indirect_shadow_bo && cmd->device && cmd->device->rm) {
      memset(&req, 0, sizeof(req));
      req.size = 4096; /* many indirect draw records (16/20 B each) */
      req.alignment = 256;
      req.vram = false;
      req.cpu_access = true;
      req.no_scanout = true;
      req.map_gpu_va = true;
      cmd->indirect_shadow_bo = nv_rm_bo_alloc(cmd->device->rm, &req);
      if (cmd->indirect_shadow_bo) {
         cmd->indirect_shadow_bo_size = 4096;
         cmd->indirect_shadow_map = nv_rm_bo_map(cmd->indirect_shadow_bo);
      }
   }

   cmd->push_dw_used = 0;
   cmd->compute_init_done = false;
   cmd->lmem_programmed = false;
   cmd->lmem_local_req = 0;
   cmd->bound_compute_pipeline = NULL;
   cmd->bound_gfx_pipeline = NULL;
   cmd->bound_set_count = 0;
   cmd->dynamic_offset_count = 0;
   cmd->index_valid = false;
   cmd->dyn_depth_valid = false;
   cmd->dyn_stencil_valid = false;
   cmd->dyn_cull_mode_valid = false;
   cmd->dyn_front_face_valid = false;
   cmd->dyn_depth_test_enable = false;
   cmd->dyn_depth_write_enable = true;
   cmd->dyn_depth_compare_op = VK_COMPARE_OP_LESS;
   cmd->dyn_stencil_test_enable = false;
   cmd->dyn_stencil_front_compare_op = VK_COMPARE_OP_ALWAYS;
   cmd->dyn_stencil_front_fail_op = VK_STENCIL_OP_KEEP;
   cmd->dyn_stencil_front_zfail_op = VK_STENCIL_OP_KEEP;
   cmd->dyn_stencil_front_zpass_op = VK_STENCIL_OP_KEEP;
   cmd->dyn_stencil_front_compare_mask = 0xff;
   cmd->dyn_stencil_front_write_mask = 0xff;
   cmd->dyn_stencil_front_reference = 0;
   cmd->dyn_blend_valid = false;
   cmd->dyn_blend_enable = false;
   cmd->dyn_color_write_valid = false;
   cmd->dyn_color_write_mask = 0xf;
   cmd->dyn_depth_bias_valid = false;
   cmd->dyn_depth_bias_enable = false;
   cmd->dyn_sample_mask_valid = false;
   cmd->dyn_sample_mask = 0xffffffffu;
   cmd->dyn_blend_const_valid = false;
   cmd->dyn_line_width_valid = false;
   cmd->dyn_line_width = 1.0f;
   cmd->cond_render_active = false;
   cmd->cond_render_gpu_addr = 0;
   cmd->active_query_pool = NULL;
   cmd->push_const_dwords = 0;
   cmd->push_const_dirty = false;
   cmd->prim_restart_enable = false;
   memset(cmd->bound_sets, 0, sizeof(cmd->bound_sets));
   memset(cmd->vtx_binding, 0, sizeof(cmd->vtx_binding));
   if (cmd->push_map)
      nv_push_init(&cmd->push, cmd->push_map, cmd->push_dw_cap);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvrm_EndCommandBuffer(VkCommandBuffer commandBuffer)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   if (cmd->push_map)
      cmd->push_dw_used = nv_push_dw_count(&cmd->push);
   return VK_SUCCESS;
}

static void
nvrm_cmd_emit_full_invalidate(struct nvrm_cmd_buffer *cmd)
{
   if (!cmd || !cmd->push_map)
      return;
   nv_push_wfi(&cmd->push);
   nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_3D);
   nv_push_method(&cmd->push, NVC597_INVALIDATE_SAMPLER_CACHE, 0);
   nv_push_method(&cmd->push, NVC597_INVALIDATE_TEXTURE_HEADER_CACHE, 0);
   nv_push_method(&cmd->push, NVC597_INVALIDATE_TEXTURE_DATA_CACHE, 0);
   nv_push_method(&cmd->push, NVC597_INVALIDATE_SHADER_CACHES,
                  NVC597_INVALIDATE_SHADER_CACHES_INSTRUCTION_TRUE |
                  NVC597_INVALIDATE_SHADER_CACHES_DATA_TRUE |
                  NVC597_INVALIDATE_SHADER_CACHES_CONSTANT_TRUE);
   /* Copy engine WFI for transfer hazards */
   nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_COPY);
   nv_push_wfi(&cmd->push);
   nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_3D);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdPipelineBarrier2(VkCommandBuffer commandBuffer,
                         const VkDependencyInfo *pDependencyInfo)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   (void)pDependencyInfo;
   /* Host WFI + texture/shader/copy invalidates.  Memory dependency scopes
    * are not yet split by stage; full invalidates match the conservative
    * path used by the proprietary driver after transfers. */
   nvrm_cmd_emit_full_invalidate(cmd);
}

/* ---- VkEvent / VkQueryPool object management + cmd recording ---- */

static struct nv_rm_bo *
nvrm_alloc_sema_bo(struct nvrm_device *dev, uint32_t size, void **map_out,
                   uint64_t *gpu_out)
{
   struct nv_rm_bo_req req;
   struct nv_rm_bo *bo;
   if (!dev || !dev->rm)
      return NULL;
   memset(&req, 0, sizeof(req));
   req.size = size < 16 ? 16 : size;
   req.alignment = 256;
   req.vram = false;
   req.cpu_access = true;
   req.no_scanout = true;
   req.map_gpu_va = true;
   bo = nv_rm_bo_alloc(dev->rm, &req);
   if (!bo)
      return NULL;
   if (map_out)
      *map_out = nv_rm_bo_map(bo);
   if (gpu_out)
      *gpu_out = nv_rm_bo_gpu_offset(bo);
   return bo;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvrm_CreateEvent(VkDevice _device, const VkEventCreateInfo *pCreateInfo,
                 const VkAllocationCallbacks *pAllocator, VkEvent *pEvent)
{
   VK_FROM_HANDLE(nvrm_device, device, _device);
   struct nvrm_event *ev;
   (void)pCreateInfo;
   (void)pAllocator;
   ev = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*ev), 8,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!ev)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   vk_object_base_init(&device->vk, &ev->base, VK_OBJECT_TYPE_EVENT);
   ev->device = device;
   ev->bo = nvrm_alloc_sema_bo(device, 16, &ev->map, &ev->sema_gpu_addr);
   if (!ev->bo) {
      vk_object_base_finish(&ev->base);
      vk_free2(&device->vk.alloc, pAllocator, ev);
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   }
   if (ev->map)
      memset(ev->map, 0, 16);
   ev->sema_value = 0;
   *pEvent = nvrm_event_to_handle(ev);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
nvrm_DestroyEvent(VkDevice _device, VkEvent _event,
                  const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(nvrm_device, device, _device);
   VK_FROM_HANDLE(nvrm_event, ev, _event);
   if (!ev)
      return;
   if (ev->bo) {
      if (ev->map)
         nv_rm_bo_unmap(ev->bo);
      nv_rm_bo_free(ev->bo);
   }
   vk_object_base_finish(&ev->base);
   vk_free2(&device->vk.alloc, pAllocator, ev);
}

VKAPI_ATTR VkResult VKAPI_CALL
nvrm_GetEventStatus(VkDevice _device, VkEvent _event)
{
   VK_FROM_HANDLE(nvrm_event, ev, _event);
   (void)_device;
   if (!ev)
      return VK_EVENT_RESET;
   if (ev->map) {
      uint32_t v = *(volatile uint32_t *)ev->map;
      ev->sema_value = v;
      return v ? VK_EVENT_SET : VK_EVENT_RESET;
   }
   return ev->sema_value ? VK_EVENT_SET : VK_EVENT_RESET;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvrm_SetEvent(VkDevice _device, VkEvent _event)
{
   VK_FROM_HANDLE(nvrm_event, ev, _event);
   (void)_device;
   if (!ev)
      return VK_ERROR_UNKNOWN;
   if (ev->map)
      *(volatile uint32_t *)ev->map = 1;
   ev->sema_value = 1;
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvrm_ResetEvent(VkDevice _device, VkEvent _event)
{
   VK_FROM_HANDLE(nvrm_event, ev, _event);
   (void)_device;
   if (!ev)
      return VK_ERROR_UNKNOWN;
   if (ev->map)
      *(volatile uint32_t *)ev->map = 0;
   ev->sema_value = 0;
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdSetEvent2(VkCommandBuffer commandBuffer, VkEvent event,
                  const VkDependencyInfo *pDependencyInfo)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvrm_event, ev, event);
   (void)pDependencyInfo;
   if (!cmd || !cmd->push_map || !ev || !ev->sema_gpu_addr)
      return;
   nv_push_wfi(&cmd->push);
   nv_push_host_semaphore_release(&cmd->push, ev->sema_gpu_addr, 1);
   ev->sema_value = 1;
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdResetEvent2(VkCommandBuffer commandBuffer, VkEvent event,
                    VkPipelineStageFlags2 stageMask)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvrm_event, ev, event);
   (void)stageMask;
   if (!cmd || !cmd->push_map || !ev || !ev->sema_gpu_addr)
      return;
   nv_push_wfi(&cmd->push);
   nv_push_host_semaphore_release(&cmd->push, ev->sema_gpu_addr, 0);
   ev->sema_value = 0;
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdWaitEvents2(VkCommandBuffer commandBuffer, uint32_t eventCount,
                    const VkEvent *pEvents,
                    const VkDependencyInfo *pDependencyInfos)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   uint32_t i;
   (void)pDependencyInfos;
   if (!cmd || !cmd->push_map)
      return;
   for (i = 0; i < eventCount && pEvents; i++) {
      VK_FROM_HANDLE(nvrm_event, ev, pEvents[i]);
      if (ev && ev->sema_gpu_addr)
         nv_push_host_semaphore_acquire(&cmd->push, ev->sema_gpu_addr, 1);
   }
   nvrm_cmd_emit_full_invalidate(cmd);
}

static uint64_t
nvrm_query_slot_addr(struct nvrm_query_pool *qp, uint32_t query)
{
   if (!qp || query >= qp->query_count)
      return 0;
   return qp->base_gpu_addr + (uint64_t)query * NVRM_QUERY_SLOT_BYTES;
}

static void *
nvrm_query_slot_map(struct nvrm_query_pool *qp, uint32_t query)
{
   if (!qp || !qp->map || query >= qp->query_count)
      return NULL;
   return (uint8_t *)qp->map + (size_t)query * NVRM_QUERY_SLOT_BYTES;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvrm_CreateQueryPool(VkDevice _device, const VkQueryPoolCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator,
                     VkQueryPool *pQueryPool)
{
   VK_FROM_HANDLE(nvrm_device, device, _device);
   struct nvrm_query_pool *qp;
   uint32_t bytes;
   if (!pCreateInfo || !pCreateInfo->queryCount)
      return VK_ERROR_INITIALIZATION_FAILED;
   qp = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*qp), 8,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!qp)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   vk_object_base_init(&device->vk, &qp->base, VK_OBJECT_TYPE_QUERY_POOL);
   qp->device = device;
   qp->query_count = pCreateInfo->queryCount;
   qp->vk_type = pCreateInfo->queryType;
   if (pCreateInfo->queryType == VK_QUERY_TYPE_OCCLUSION)
      qp->kind = NVRM_QUERY_OCCLUSION;
   else if (pCreateInfo->queryType == VK_QUERY_TYPE_TIMESTAMP)
      qp->kind = NVRM_QUERY_TIMESTAMP;
   else
      qp->kind = NVRM_QUERY_PIPELINE_STATS;
   bytes = qp->query_count * NVRM_QUERY_SLOT_BYTES;
   if (bytes < 256)
      bytes = 256;
   qp->bo = nvrm_alloc_sema_bo(device, bytes, &qp->map, &qp->base_gpu_addr);
   if (!qp->bo) {
      vk_object_base_finish(&qp->base);
      vk_free2(&device->vk.alloc, pAllocator, qp);
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   }
   if (qp->map)
      memset(qp->map, 0, bytes);
   *pQueryPool = nvrm_query_pool_to_handle(qp);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
nvrm_DestroyQueryPool(VkDevice _device, VkQueryPool _pool,
                      const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(nvrm_device, device, _device);
   VK_FROM_HANDLE(nvrm_query_pool, qp, _pool);
   if (!qp)
      return;
   if (qp->bo) {
      if (qp->map)
         nv_rm_bo_unmap(qp->bo);
      nv_rm_bo_free(qp->bo);
   }
   vk_object_base_finish(&qp->base);
   vk_free2(&device->vk.alloc, pAllocator, qp);
}

/*
 * NVC597 report semaphore 4-word structure (approx from class/report layout):
 *   dw0: payload / zpass counter low
 *   dw1: timestamp low or counter high
 *   dw2: timestamp high / report marker
 *   dw3: availability / completion (non-zero when release completed)
 * One-word reports only write dw0; availability inferred from non-zero dw0
 * or explicit dw3 when 4-word structure was used.
 */
static bool
nvrm_query_slot_available(const uint32_t *slot, enum nvrm_query_kind kind)
{
   if (!slot)
      return false;
   if (slot[3])
      return true; /* 4-word report completion marker */
   if (kind == NVRM_QUERY_TIMESTAMP)
      return slot[0] || slot[1] || slot[2];
   /* Occlusion: zero is a valid result; availability uses dw3 or non-zero any */
   return slot[0] || slot[1] || slot[2] || slot[3];
}

static uint64_t
nvrm_query_slot_value64(const uint32_t *slot, enum nvrm_query_kind kind)
{
   if (!slot)
      return 0;
   if (kind == NVRM_QUERY_TIMESTAMP) {
      /* 64-bit timestamp: typically dw0|dw1 or dw1|dw2 depending on report mode */
      if (slot[1] || slot[2])
         return ((uint64_t)slot[2] << 32) | slot[1];
      return ((uint64_t)slot[1] << 32) | slot[0];
   }
   if (kind == NVRM_QUERY_OCCLUSION)
      return ((uint64_t)slot[1] << 32) | slot[0];
   /* pipeline stats: sum first two dwords as coarse proxy */
   return ((uint64_t)slot[1] << 32) | slot[0];
}

VKAPI_ATTR VkResult VKAPI_CALL
nvrm_GetQueryPoolResults(VkDevice _device, VkQueryPool _pool,
                         uint32_t firstQuery, uint32_t queryCount,
                         size_t dataSize, void *pData, VkDeviceSize stride,
                         VkQueryResultFlags flags)
{
   VK_FROM_HANDLE(nvrm_query_pool, qp, _pool);
   uint32_t i;
   bool any_unavailable = false;
   (void)_device;
   if (!qp || !pData || !queryCount)
      return VK_ERROR_UNKNOWN;
   for (i = 0; i < queryCount; i++) {
      uint32_t qi = firstQuery + i;
      uint32_t *slot = (uint32_t *)nvrm_query_slot_map(qp, qi);
      uint8_t *dst = (uint8_t *)pData + (size_t)i * (size_t)stride;
      uint64_t val;
      bool avail;
      size_t need = (flags & VK_QUERY_RESULT_64_BIT) ? 8 : 4;
      if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)
         need *= 2;
      if (!slot || (size_t)(i + 1) * (size_t)stride > dataSize ||
          (size_t)(dst - (uint8_t *)pData) + need > dataSize)
         continue;
      avail = nvrm_query_slot_available(slot, qp->kind);
      if (!avail) {
         any_unavailable = true;
         if (flags & VK_QUERY_RESULT_WAIT_BIT) {
            /* Host-mappable sema BO: spin briefly on availability dword */
            unsigned spin;
            for (spin = 0; spin < 100000 && !avail; spin++)
               avail = nvrm_query_slot_available(slot, qp->kind);
            if (!avail)
               any_unavailable = true;
            else
               any_unavailable = false;
         }
      }
      val = nvrm_query_slot_value64(slot, qp->kind);
      if (flags & VK_QUERY_RESULT_64_BIT) {
         *(uint64_t *)dst = val;
         if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)
            *(uint64_t *)(dst + 8) = avail ? 1 : 0;
      } else {
         *(uint32_t *)dst = (uint32_t)val;
         if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)
            *(uint32_t *)(dst + 4) = avail ? 1 : 0;
      }
      if (!avail && (flags & VK_QUERY_RESULT_PARTIAL_BIT) == 0 &&
          (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) == 0)
         any_unavailable = true;
   }
   if (any_unavailable && !(flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) &&
       !(flags & VK_QUERY_RESULT_PARTIAL_BIT))
      return VK_NOT_READY;
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdCopyQueryPoolResults(VkCommandBuffer commandBuffer,
                             VkQueryPool queryPool,
                             uint32_t firstQuery, uint32_t queryCount,
                             VkBuffer dstBuffer, VkDeviceSize dstOffset,
                             VkDeviceSize stride, VkQueryResultFlags flags)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvrm_query_pool, qp, queryPool);
   VK_FROM_HANDLE(nvrm_buffer, dst, dstBuffer);
   const struct nv_device_info *info;
   uint32_t class_copy, i;
   uint64_t daddr;
   uint32_t elem_size = (flags & VK_QUERY_RESULT_64_BIT) ? 8 : 4;

   if (!cmd || !cmd->push_map || !qp || !dst)
      return;
   if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)
      elem_size *= 2;
   info = cmd->device ? cmd->device->info : NULL;
   class_copy = info ? info->class_copy : 0;
   daddr = (dst->addr ? dst->addr : (dst->bo ? nv_rm_bo_gpu_offset(dst->bo) : 0))
           + dstOffset;
   if (!daddr || !qp->base_gpu_addr)
      return;
   if (class_copy)
      nv_copy_set_object(&cmd->push, class_copy);
   else
      nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_COPY);
   /* Copy each query slot (or first elem_size bytes) into tightly/strided dst */
   for (i = 0; i < queryCount; i++) {
      uint64_t saddr = qp->base_gpu_addr +
                       (uint64_t)(firstQuery + i) * NVRM_QUERY_SLOT_BYTES;
      uint64_t out = daddr + (uint64_t)i * (stride ? stride : elem_size);
      uint32_t copy_bytes = elem_size <= NVRM_QUERY_SLOT_BYTES
                            ? elem_size : NVRM_QUERY_SLOT_BYTES;
      nv_copy_emit_buffer_copy(&cmd->push, saddr, out, copy_bytes, 0, 0, 1);
   }
   nv_push_wfi(&cmd->push);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
                       uint32_t firstQuery, uint32_t queryCount)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvrm_query_pool, qp, queryPool);
   uint32_t i;
   (void)cmd;
   if (!qp || !qp->map)
      return;
   for (i = 0; i < queryCount; i++) {
      void *slot = nvrm_query_slot_map(qp, firstQuery + i);
      if (slot)
         memset(slot, 0, NVRM_QUERY_SLOT_BYTES);
   }
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
                   uint32_t query, VkQueryControlFlags flags)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvrm_query_pool, qp, queryPool);
   (void)flags;
   if (!cmd || !cmd->push_map || !qp)
      return;
   cmd->active_query_pool = qp;
   cmd->active_query_index = query;
   cmd->active_query_is_occlusion = (qp->kind == NVRM_QUERY_OCCLUSION);
   nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_3D);
   if (cmd->active_query_is_occlusion)
      nv_3d_set_zpass_pixel_count(&cmd->push, true);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
                 uint32_t query)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvrm_query_pool, qp, queryPool);
   uint64_t addr;
   if (!cmd || !cmd->push_map || !qp)
      return;
   addr = nvrm_query_slot_addr(qp, query);
   if (!addr)
      return;
   nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_3D);
   if (qp->kind == NVRM_QUERY_OCCLUSION) {
      nv_3d_report_query_release(&cmd->push, addr, 0, true, true);
      nv_3d_set_zpass_pixel_count(&cmd->push, false);
   } else {
      /* Timestamp / stats: pipeline semaphore release with 4-word structure */
      nv_3d_report_query_release(&cmd->push, addr, 0, false, false);
   }
   if (cmd->active_query_pool == qp)
      cmd->active_query_pool = NULL;
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdWriteTimestamp2(VkCommandBuffer commandBuffer,
                        VkPipelineStageFlags2 stage,
                        VkQueryPool queryPool, uint32_t query)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvrm_query_pool, qp, queryPool);
   uint64_t addr;
   (void)stage;
   if (!cmd || !cmd->push_map || !qp)
      return;
   addr = nvrm_query_slot_addr(qp, query);
   if (!addr)
      return;
   nv_push_wfi(&cmd->push);
   nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_3D);
   nv_3d_report_query_release(&cmd->push, addr, 0, false, false);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   cmd->dyn_line_width = lineWidth;
   cmd->dyn_line_width_valid = true;
   if (cmd->push_map)
      nv_3d_emit_line_width(&cmd->push, lineWidth);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdBeginConditionalRenderingEXT(
   VkCommandBuffer commandBuffer,
   const VkConditionalRenderingBeginInfoEXT *pInfo)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvrm_buffer, buf, pInfo ? pInfo->buffer : VK_NULL_HANDLE);
   uint64_t addr = 0;
   bool inverted = false;
   if (!cmd || !cmd->push_map || !pInfo)
      return;
   if (buf)
      addr = (buf->addr ? buf->addr : (buf->bo ? nv_rm_bo_gpu_offset(buf->bo) : 0))
             + pInfo->offset;
   inverted = !!(pInfo->flags & VK_CONDITIONAL_RENDERING_INVERTED_BIT_EXT);
   cmd->cond_render_active = true;
   cmd->cond_render_gpu_addr = addr;
   cmd->cond_render_inverted = inverted;
   nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_3D);
   /* Normal: render if *addr != 0 (CONDITIONAL mode).
    * Inverted: render if *addr == 0 (RENDER_IF_EQUAL with zero compare). */
   nv_3d_set_conditional_render(&cmd->push, addr, inverted);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdEndConditionalRenderingEXT(VkCommandBuffer commandBuffer)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   if (!cmd || !cmd->push_map)
      return;
   cmd->cond_render_active = false;
   cmd->cond_render_gpu_addr = 0;
   cmd->cond_render_inverted = false;
   nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_3D);
   nv_3d_set_conditional_render(&cmd->push, 0, false);
}

/* ---- Buffer fill / update / blit / resolve via copy engine ---- */

/* Fallback when REMAP unavailable: host-side dword pattern via shadow BO. */
static void
nvrm_cmd_fill_via_shadow(struct nvrm_cmd_buffer *cmd, uint64_t dst_addr,
                         VkDeviceSize size_bytes, uint32_t data)
{
   const struct nv_device_info *info = cmd->device ? cmd->device->info : NULL;
   uint32_t class_copy = info ? info->class_copy : 0;
   VkDeviceSize remain = size_bytes & ~3ull;
   uint64_t addr = dst_addr;
   uint32_t *shadow;
   uint32_t chunk_dwords, i;
   uint64_t shadow_gpu;

   if (!cmd->indirect_shadow_map || !cmd->indirect_shadow_bo || remain < 4)
      return;
   shadow = (uint32_t *)cmd->indirect_shadow_map;
   shadow_gpu = nv_rm_bo_gpu_offset(cmd->indirect_shadow_bo);
   chunk_dwords = cmd->indirect_shadow_bo_size / 4;
   if (!chunk_dwords)
      return;
   if (class_copy)
      nv_copy_set_object(&cmd->push, class_copy);
   else
      nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_COPY);
   while (remain >= 4) {
      uint32_t n = (uint32_t)(remain / 4);
      if (n > chunk_dwords)
         n = chunk_dwords;
      for (i = 0; i < n; i++)
         shadow[i] = data;
      nv_copy_emit_buffer_copy(&cmd->push, shadow_gpu, addr, n * 4, 0, 0, 1);
      addr += n * 4ull;
      remain -= n * 4ull;
   }
   nv_push_wfi(&cmd->push);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer,
                   VkDeviceSize dstOffset, VkDeviceSize size, uint32_t data)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvrm_buffer, dst, dstBuffer);
   const struct nv_device_info *info;
   uint32_t class_copy;
   uint64_t addr;
   uint32_t fill_bytes;

   if (!cmd || !cmd->push_map || !dst)
      return;
   info = cmd->device ? cmd->device->info : NULL;
   class_copy = info ? info->class_copy : 0;
   addr = (dst->addr ? dst->addr : (dst->bo ? nv_rm_bo_gpu_offset(dst->bo) : 0))
          + dstOffset;
   if (!addr)
      return;
   if (size == VK_WHOLE_SIZE)
      size = dst->vk.size > dstOffset ? dst->vk.size - dstOffset : 4;
   fill_bytes = (uint32_t)(size & ~3ull);
   if (!fill_bytes)
      return;
   /* Preferred: CE REMAP constant fill (no host staging) */
   if (class_copy || cmd->push_map) {
      nv_copy_push_remap_fill_u32(&cmd->push, class_copy, addr, fill_bytes, data);
      return;
   }
   nvrm_cmd_fill_via_shadow(cmd, addr, size, data);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image,
                               VkImageLayout imageLayout,
                               const VkClearDepthStencilValue *pDepthStencil,
                               uint32_t rangeCount,
                               const VkImageSubresourceRange *pRanges)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvrm_image, img, image);
   const struct nv_device_info *info;
   uint32_t class_3d, r;
   float depth = pDepthStencil ? pDepthStencil->depth : 1.0f;
   uint32_t stencil = pDepthStencil ? pDepthStencil->stencil : 0;
   uint64_t addr = 0;
   uint32_t w = 1, h = 1, pitch = 0, zt_fmt = NVC597_SET_ZT_FORMAT_V_Z24S8;

   (void)imageLayout;
   if (!cmd || !cmd->push_map)
      return;
   info = cmd->device ? cmd->device->info : NULL;
   class_3d = info ? info->class_3d : 0;
   if (class_3d)
      nv_3d_set_object(&cmd->push, class_3d);
   else
      nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_3D);

   if (img) {
      addr = (img->bo ? nv_rm_bo_gpu_offset(img->bo) : 0) + img->gpu_offset;
      w = img->vk.extent.width ? img->vk.extent.width : 1;
      h = img->vk.extent.height ? img->vk.extent.height : 1;
      pitch = img->row_pitch ? img->row_pitch : (w * (img->bpp ? img->bpp : 4));
      /* Map common depth formats via pipe-like numbers from vk_format bits */
      if (img->bpp == 2)
         zt_fmt = NVC597_SET_ZT_FORMAT_V_Z16;
      else if (img->bpp == 4)
         zt_fmt = NVC597_SET_ZT_FORMAT_V_Z24S8;
      else if (img->bpp == 8)
         zt_fmt = NVC597_SET_ZT_FORMAT_V_ZF32_X24S8;
   }

   for (r = 0; r < (rangeCount ? rangeCount : 1); r++) {
      unsigned buffers = 0x1 | 0x2; /* depth + stencil */
      VkImageAspectFlags aspects = VK_IMAGE_ASPECT_DEPTH_BIT |
                                   VK_IMAGE_ASPECT_STENCIL_BIT;
      if (pRanges && r < rangeCount)
         aspects = pRanges[r].aspectMask;
      buffers = 0;
      if (aspects & VK_IMAGE_ASPECT_DEPTH_BIT)
         buffers |= 0x1;
      if (aspects & VK_IMAGE_ASPECT_STENCIL_BIT)
         buffers |= 0x2;
      if (!buffers)
         buffers = 0x1 | 0x2;

      if (addr && !cmd->in_render_pass) {
         /* Bind zeta from image then clear (standalone CmdClearDepthStencilImage) */
         nv_3d_bind_and_clear_zeta(&cmd->push, addr, w, h, pitch, zt_fmt,
                                   buffers, depth, stencil);
      } else {
         /* Already in render pass with zeta bound: clear only */
         nv_3d_emit_clear_surface(&cmd->push, buffers, NULL, depth, stencil);
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer,
                     VkDeviceSize dstOffset, VkDeviceSize dataSize,
                     const void *pData)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvrm_buffer, dst, dstBuffer);
   uint64_t addr;
   const struct nv_device_info *info;
   uint32_t class_copy;

   if (!cmd || !cmd->push_map || !dst || !pData || !dataSize)
      return;
   info = cmd->device ? cmd->device->info : NULL;
   class_copy = info ? info->class_copy : 0;
   addr = (dst->addr ? dst->addr : (dst->bo ? nv_rm_bo_gpu_offset(dst->bo) : 0))
          + dstOffset;
   if (!addr)
      return;
   /* Stage through host-mappable shadow then CE copy (max 64 KiB per Vulkan) */
   if (cmd->indirect_shadow_map && cmd->indirect_shadow_bo &&
       dataSize <= cmd->indirect_shadow_bo_size) {
      uint64_t shadow = nv_rm_bo_gpu_offset(cmd->indirect_shadow_bo);
      memcpy(cmd->indirect_shadow_map, pData, (size_t)dataSize);
      if (class_copy)
         nv_copy_set_object(&cmd->push, class_copy);
      else
         nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_COPY);
      nv_copy_emit_buffer_copy(&cmd->push, shadow, addr, (uint32_t)dataSize,
                               0, 0, 1);
      nv_push_wfi(&cmd->push);
   } else if (cmd->push_const_map && cmd->push_const_bo &&
              dataSize <= cmd->push_const_bo_size) {
      uint64_t shadow = nv_rm_bo_gpu_offset(cmd->push_const_bo);
      memcpy(cmd->push_const_map, pData, (size_t)dataSize);
      if (class_copy)
         nv_copy_set_object(&cmd->push, class_copy);
      else
         nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_COPY);
      nv_copy_emit_buffer_copy(&cmd->push, shadow, addr, (uint32_t)dataSize,
                               0, 0, 1);
      nv_push_wfi(&cmd->push);
   }
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdBlitImage2(VkCommandBuffer commandBuffer,
                   const VkBlitImageInfo2 *pBlitImageInfo)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvrm_image, src, pBlitImageInfo ? pBlitImageInfo->srcImage : VK_NULL_HANDLE);
   VK_FROM_HANDLE(nvrm_image, dst, pBlitImageInfo ? pBlitImageInfo->dstImage : VK_NULL_HANDLE);
   const struct nv_device_info *info;
   uint32_t class_copy;
   uint32_t r;

   if (!cmd || !cmd->push_map || !pBlitImageInfo || !src || !dst)
      return;
   info = cmd->device ? cmd->device->info : NULL;
   class_copy = info ? info->class_copy : 0;
   if (class_copy)
      nv_copy_set_object(&cmd->push, class_copy);
   else
      nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_COPY);

   for (r = 0; r < pBlitImageInfo->regionCount; r++) {
      const VkImageBlit2 *b = &pBlitImageInfo->pRegions[r];
      int32_t sx0 = b->srcOffsets[0].x, sy0 = b->srcOffsets[0].y;
      int32_t sx1 = b->srcOffsets[1].x, sy1 = b->srcOffsets[1].y;
      int32_t dx0 = b->dstOffsets[0].x, dy0 = b->dstOffsets[0].y;
      int32_t dx1 = b->dstOffsets[1].x, dy1 = b->dstOffsets[1].y;
      uint32_t sw = (uint32_t)(sx1 > sx0 ? sx1 - sx0 : sx0 - sx1);
      uint32_t sh = (uint32_t)(sy1 > sy0 ? sy1 - sy0 : sy0 - sy1);
      uint32_t dw = (uint32_t)(dx1 > dx0 ? dx1 - dx0 : dx0 - dx1);
      uint32_t dh = (uint32_t)(dy1 > dy0 ? dy1 - dy0 : dy0 - dy1);
      uint64_t saddr = (src->bo ? nv_rm_bo_gpu_offset(src->bo) : 0) + src->gpu_offset;
      uint64_t daddr = (dst->bo ? nv_rm_bo_gpu_offset(dst->bo) : 0) + dst->gpu_offset;
      uint32_t spitch = src->row_pitch ? src->row_pitch
         : (src->bpp * (src->vk.extent.width ? src->vk.extent.width : 1));
      uint32_t dpitch = dst->row_pitch ? dst->row_pitch
         : (dst->bpp * (dst->vk.extent.width ? dst->vk.extent.width : 1));
      uint32_t sbpp = src->bpp ? src->bpp : 4;
      uint32_t dbpp = dst->bpp ? dst->bpp : 4;
      uint32_t w, h, line_len;
      uint64_t soff, doff;

      if (!saddr || !daddr)
         continue;
      if (sw == 0) sw = 1;
      if (sh == 0) sh = 1;
      if (dw == 0) dw = 1;
      if (dh == 0) dh = 1;
      /* Unscaled CE pitch blit (scaled/filter needs 3D path later) */
      w = sw < dw ? sw : dw;
      h = sh < dh ? sh : dh;
      soff = saddr + (uint64_t)sy0 * spitch + (uint64_t)sx0 * sbpp;
      doff = daddr + (uint64_t)dy0 * dpitch + (uint64_t)dx0 * dbpp;
      line_len = w * (sbpp < dbpp ? sbpp : dbpp);
      if (src->is_blocklinear || dst->is_blocklinear)
         nv_copy_emit_image_2d_bl(&cmd->push, soff, doff, w, h, sbpp,
                                  spitch, dpitch, 0, 0, 0, 0,
                                  src->is_blocklinear, dst->is_blocklinear,
                                  src->gobs_width, src->gobs_height,
                                  dst->gobs_width, dst->gobs_height);
      else
         nv_copy_emit_image_2d(&cmd->push, soff, doff, line_len,
                               spitch, dpitch, h);
   }
   nv_push_wfi(&cmd->push);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdResolveImage2(VkCommandBuffer commandBuffer,
                      const VkResolveImageInfo2 *pResolveImageInfo)
{
   /* MSAA resolve: 1:1 CE copy of first sample (full resolve needs 3D). */
   VkBlitImageInfo2 bi;
   VkImageBlit2 stack_regions[4];
   VkImageBlit2 *regions = stack_regions;
   VkImageBlit2 *heap = NULL;
   uint32_t i, n;

   if (!pResolveImageInfo || !pResolveImageInfo->regionCount)
      return;
   n = pResolveImageInfo->regionCount;
   if (n > 4) {
      heap = (VkImageBlit2 *)calloc(n, sizeof(*heap));
      if (!heap)
         return;
      regions = heap;
   } else {
      memset(stack_regions, 0, sizeof(stack_regions));
   }
   for (i = 0; i < n; i++) {
      const VkImageResolve2 *r = &pResolveImageInfo->pRegions[i];
      regions[i].sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
      regions[i].srcSubresource = r->srcSubresource;
      regions[i].dstSubresource = r->dstSubresource;
      regions[i].srcOffsets[0] = r->srcOffset;
      regions[i].srcOffsets[1].x = r->srcOffset.x + (int32_t)r->extent.width;
      regions[i].srcOffsets[1].y = r->srcOffset.y + (int32_t)r->extent.height;
      regions[i].srcOffsets[1].z = r->srcOffset.z + (int32_t)r->extent.depth;
      regions[i].dstOffsets[0] = r->dstOffset;
      regions[i].dstOffsets[1].x = r->dstOffset.x + (int32_t)r->extent.width;
      regions[i].dstOffsets[1].y = r->dstOffset.y + (int32_t)r->extent.height;
      regions[i].dstOffsets[1].z = r->dstOffset.z + (int32_t)r->extent.depth;
   }
   memset(&bi, 0, sizeof(bi));
   bi.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
   bi.srcImage = pResolveImageInfo->srcImage;
   bi.srcImageLayout = pResolveImageInfo->srcImageLayout;
   bi.dstImage = pResolveImageInfo->dstImage;
   bi.dstImageLayout = pResolveImageInfo->dstImageLayout;
   bi.regionCount = n;
   bi.pRegions = regions;
   bi.filter = VK_FILTER_NEAREST;
   nvrm_CmdBlitImage2(commandBuffer, &bi);
   free(heap);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image,
                        VkImageLayout imageLayout,
                        const VkClearColorValue *pColor,
                        uint32_t rangeCount,
                        const VkImageSubresourceRange *pRanges)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   const struct nv_device_info *info = cmd->device->info;
   uint32_t class_3d = info ? info->class_3d : 0;
   const uint32_t *c = pColor ? pColor->uint32 : NULL;
   (void)image;
   (void)imageLayout;
   (void)rangeCount;
   (void)pRanges;

   if (!cmd->push_map)
      return;

   /* Colour clear: if inside a multi-RT render pass, clear all active MRTs
    * with the same colour; otherwise target 0 only. */
   {
      unsigned buffers = 0x10;
      unsigned mrt = cmd->render_color_count ? cmd->render_color_count : 1;
      unsigned i;
      for (i = 0; i < mrt && i < 8; i++)
         buffers |= (0x10u << i);
      if (mrt > 1)
         nv_3d_push_clear_multi(&cmd->push, class_3d, buffers, c, 0.0f, 0, mrt);
      else
         nv_3d_push_clear(&cmd->push, class_3d, 0x10, c, 0.0f, 0);
   }
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdClearAttachments(VkCommandBuffer commandBuffer,
                         uint32_t attachmentCount,
                         const VkClearAttachment *pAttachments,
                         uint32_t rectCount,
                         const VkClearRect *pRects)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   const struct nv_device_info *info = cmd->device ? cmd->device->info : NULL;
   uint32_t class_3d = info ? info->class_3d : 0;
   uint32_t a;

   (void)rectCount;
   (void)pRects;

   if (!cmd->push_map || !pAttachments || !attachmentCount)
      return;

   if (class_3d)
      nv_3d_set_object(&cmd->push, class_3d);
   else
      nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_3D);

   for (a = 0; a < attachmentCount; a++) {
      const VkClearAttachment *att = &pAttachments[a];
      if (att->aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
         const uint32_t *c = att->clearValue.color.uint32;
         unsigned ti = att->colorAttachment & 7u;
         nv_3d_emit_clear_surface_mrt(&cmd->push, ti, c, true, true, true, true);
      }
      if (att->aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
         unsigned buffers = 0;
         if (att->aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT)
            buffers |= 0x100;
         if (att->aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT)
            buffers |= 0x200;
         nv_3d_emit_clear_surface(&cmd->push, buffers, NULL,
                                  att->clearValue.depthStencil.depth,
                                  att->clearValue.depthStencil.stencil);
      }
   }
   nv_push_wfi(&cmd->push);
}

/* Bind a pre-uploaded nv_shader to the graphics pipeline (helper for pipeline
 * bind once shader modules compile to machine code). */
static void
nvrm_cmd_bind_graphics_shader(struct nvrm_cmd_buffer *cmd,
                              const struct nv_shader *sh,
                              uint64_t program_region_base)
{
   if (!cmd || !cmd->push_map || !sh)
      return;
   nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_3D);
   nv_shader_emit_bind(&cmd->push, sh, program_region_base, -1);
}

/* Record a host semaphore release at the end of the command buffer segment
 * (queue submit path can wait via nv_fence_wait on the same sema BO). */
static void
nvrm_cmd_emit_host_sema_release(struct nvrm_cmd_buffer *cmd,
                                uint64_t sema_gpu_addr, uint32_t payload)
{
   if (!cmd || !cmd->push_map || !sema_gpu_addr)
      return;
   nv_push_wfi(&cmd->push);
   nv_push_host_semaphore_release(&cmd->push, sema_gpu_addr, payload);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdCopyBuffer2(VkCommandBuffer commandBuffer,
                    const VkCopyBufferInfo2 *pCopyBufferInfo)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   const struct nv_device_info *info = cmd->device->info;
   VK_FROM_HANDLE(nvrm_buffer, src_buf, pCopyBufferInfo->srcBuffer);
   VK_FROM_HANDLE(nvrm_buffer, dst_buf, pCopyBufferInfo->dstBuffer);
   uint64_t src_base = 0, dst_base = 0;
   uint32_t i;

   if (!cmd->push_map)
      return;

   if (src_buf && src_buf->bo)
      src_base = nv_rm_bo_gpu_offset(src_buf->bo);
   if (dst_buf && dst_buf->bo)
      dst_base = nv_rm_bo_gpu_offset(dst_buf->bo);

   if (info && info->class_copy)
      nv_copy_set_object(&cmd->push, info->class_copy);
   else
      nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_COPY);

   for (i = 0; i < pCopyBufferInfo->regionCount; i++) {
      const VkBufferCopy2 *r = &pCopyBufferInfo->pRegions[i];
      nv_copy_emit_buffer_copy(&cmd->push,
                               src_base + r->srcOffset,
                               dst_base + r->dstOffset,
                               (uint32_t)r->size, 0, 0, 1);
   }
   nv_push_wfi(&cmd->push);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdCopyImage2(VkCommandBuffer commandBuffer,
                   const VkCopyImageInfo2 *pCopyImageInfo)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   const struct nv_device_info *info = cmd->device->info;
   VK_FROM_HANDLE(nvrm_image, src_img, pCopyImageInfo->srcImage);
   VK_FROM_HANDLE(nvrm_image, dst_img, pCopyImageInfo->dstImage);
   uint64_t src_base = 0, dst_base = 0;
   uint32_t class_copy = info ? info->class_copy : 0;
   uint32_t i;

   if (!cmd->push_map || !src_img || !dst_img)
      return;

   src_base = src_img->gpu_offset ? src_img->gpu_offset :
              (src_img->bo ? nv_rm_bo_gpu_offset(src_img->bo) : 0);
   dst_base = dst_img->gpu_offset ? dst_img->gpu_offset :
              (dst_img->bo ? nv_rm_bo_gpu_offset(dst_img->bo) : 0);

   if (class_copy)
      nv_copy_set_object(&cmd->push, class_copy);
   else
      nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_COPY);

   for (i = 0; i < pCopyImageInfo->regionCount; i++) {
      const VkImageCopy2 *r = &pCopyImageInfo->pRegions[i];
      uint32_t w = r->extent.width ? r->extent.width : 1;
      uint32_t h = r->extent.height ? r->extent.height : 1;
      uint32_t bpp = src_img->bpp ? src_img->bpp : 4;
      uint32_t pitch_in = src_img->row_pitch ? src_img->row_pitch : (w * bpp + 31u) & ~31u;
      uint32_t pitch_out = dst_img->row_pitch ? dst_img->row_pitch : (w * bpp + 31u) & ~31u;
      bool src_bl = src_img->is_blocklinear && !src_img->is_linear;
      bool dst_bl = dst_img->is_blocklinear && !dst_img->is_linear;
      uint32_t bl_w = 0, bl_h = 0;

      /* Blocklinear block size encoding: log2 gobs (see NVC6B5_SET_*_BLOCK_SIZE) */
      if (src_bl || dst_bl) {
         bl_w = src_img->gobs_width;
         bl_h = src_img->gobs_height ? src_img->gobs_height : 4; /* SIXTEEN gobs */
         nv_copy_emit_image_2d_bl(&cmd->push, src_base, dst_base,
                                  w, h, bpp, pitch_in, pitch_out,
                                  r->srcOffset.x, r->srcOffset.y,
                                  r->dstOffset.x, r->dstOffset.y,
                                  src_bl, dst_bl,
                                  bl_w, bl_h, bl_w, bl_h);
      } else {
         uint32_t line_len = w * bpp;
         uint64_t s = src_base + (uint64_t)r->srcOffset.y * pitch_in +
                      (uint64_t)r->srcOffset.x * bpp;
         uint64_t d = dst_base + (uint64_t)r->dstOffset.y * pitch_out +
                      (uint64_t)r->dstOffset.x * bpp;
         nv_copy_emit_image_2d(&cmd->push, s, d, line_len, pitch_in, pitch_out, h);
      }
   }
   nv_push_wfi(&cmd->push);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdCopyBufferToImage2(VkCommandBuffer commandBuffer,
                           const VkCopyBufferToImageInfo2 *pCopyBufferToImageInfo)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   const struct nv_device_info *info = cmd->device->info;
   VK_FROM_HANDLE(nvrm_buffer, src_buf, pCopyBufferToImageInfo->srcBuffer);
   VK_FROM_HANDLE(nvrm_image, dst_img, pCopyBufferToImageInfo->dstImage);
   uint64_t src_base = 0, dst_base = 0;
   uint32_t class_copy = info ? info->class_copy : 0;
   uint32_t i;

   if (!cmd->push_map || !dst_img)
      return;

   if (src_buf && src_buf->bo)
      src_base = nv_rm_bo_gpu_offset(src_buf->bo);
   dst_base = dst_img->gpu_offset ? dst_img->gpu_offset :
              (dst_img->bo ? nv_rm_bo_gpu_offset(dst_img->bo) : 0);

   if (class_copy)
      nv_copy_set_object(&cmd->push, class_copy);
   else
      nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_COPY);

   for (i = 0; i < pCopyBufferToImageInfo->regionCount; i++) {
      const VkBufferImageCopy2 *r = &pCopyBufferToImageInfo->pRegions[i];
      uint32_t w = r->imageExtent.width ? r->imageExtent.width : 1;
      uint32_t h = r->imageExtent.height ? r->imageExtent.height : 1;
      uint32_t bpp = dst_img->bpp ? dst_img->bpp : 4;
      uint32_t pitch_out = dst_img->row_pitch ? dst_img->row_pitch : (w * bpp + 31u) & ~31u;
      uint32_t buf_row = r->bufferRowLength ? r->bufferRowLength * bpp : (w * bpp);
      uint64_t s = src_base + r->bufferOffset;
      uint64_t d = dst_base + (uint64_t)r->imageOffset.y * pitch_out +
                   (uint64_t)r->imageOffset.x * bpp;
      bool dst_bl = dst_img->is_blocklinear && !dst_img->is_linear;

      if (dst_bl) {
         nv_copy_emit_image_2d_bl(&cmd->push, s, d, w, h, bpp, buf_row, pitch_out,
                                  0, 0, r->imageOffset.x, r->imageOffset.y,
                                  false, true,
                                  0, 0,
                                  dst_img->gobs_width,
                                  dst_img->gobs_height ? dst_img->gobs_height : 4);
      } else {
         nv_copy_emit_image_2d(&cmd->push, s, d, w * bpp, buf_row, pitch_out, h);
      }
   }
   nv_push_wfi(&cmd->push);
}

VKAPI_ATTR void VKAPI_CALL
nvrm_CmdCopyImageToBuffer2(VkCommandBuffer commandBuffer,
                           const VkCopyImageToBufferInfo2 *pCopyImageToBufferInfo)
{
   VK_FROM_HANDLE(nvrm_cmd_buffer, cmd, commandBuffer);
   const struct nv_device_info *info = cmd->device->info;
   VK_FROM_HANDLE(nvrm_image, src_img, pCopyImageToBufferInfo->srcImage);
   VK_FROM_HANDLE(nvrm_buffer, dst_buf, pCopyImageToBufferInfo->dstBuffer);
   uint64_t src_base = 0, dst_base = 0;
   uint32_t class_copy = info ? info->class_copy : 0;
   uint32_t i;

   if (!cmd->push_map || !src_img)
      return;

   src_base = src_img->gpu_offset ? src_img->gpu_offset :
              (src_img->bo ? nv_rm_bo_gpu_offset(src_img->bo) : 0);
   if (dst_buf && dst_buf->bo)
      dst_base = nv_rm_bo_gpu_offset(dst_buf->bo);

   if (class_copy)
      nv_copy_set_object(&cmd->push, class_copy);
   else
      nv_push_set_subch(&cmd->push, NV_PUSH_SUBCH_COPY);

   for (i = 0; i < pCopyImageToBufferInfo->regionCount; i++) {
      const VkBufferImageCopy2 *r = &pCopyImageToBufferInfo->pRegions[i];
      uint32_t w = r->imageExtent.width ? r->imageExtent.width : 1;
      uint32_t h = r->imageExtent.height ? r->imageExtent.height : 1;
      uint32_t bpp = src_img->bpp ? src_img->bpp : 4;
      uint32_t pitch_in = src_img->row_pitch ? src_img->row_pitch : (w * bpp + 31u) & ~31u;
      uint32_t buf_row = r->bufferRowLength ? r->bufferRowLength * bpp : (w * bpp);
      uint64_t s = src_base + (uint64_t)r->imageOffset.y * pitch_in +
                   (uint64_t)r->imageOffset.x * bpp;
      uint64_t d = dst_base + r->bufferOffset;
      bool src_bl = src_img->is_blocklinear && !src_img->is_linear;

      if (src_bl) {
         nv_copy_emit_image_2d_bl(&cmd->push, s, d, w, h, bpp, pitch_in, buf_row,
                                  r->imageOffset.x, r->imageOffset.y, 0, 0,
                                  true, false,
                                  src_img->gobs_width,
                                  src_img->gobs_height ? src_img->gobs_height : 4,
                                  0, 0);
      } else {
         nv_copy_emit_image_2d(&cmd->push, s, d, w * bpp, pitch_in, buf_row, h);
      }
   }
   nv_push_wfi(&cmd->push);
}
