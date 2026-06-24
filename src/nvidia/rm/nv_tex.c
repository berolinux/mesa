/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 */

#include "nv_tex.h"
#include "nv_rm.h"

#include <stdlib.h>
#include <string.h>

struct nv_tex_pool *
nv_tex_pool_create(struct nv_rm_device *rm, uint32_t num_entries)
{
   struct nv_tex_pool *pool;
   struct nv_rm_bo_req req;
   uint32_t n = num_entries ? num_entries : NV_TEX_POOL_DEFAULT_N;
   uint64_t bytes;

   if (!rm)
      return NULL;

   pool = calloc(1, sizeof(*pool));
   if (!pool)
      return NULL;

   pool->rm = rm;
   pool->num_entries = n;
   bytes = (uint64_t)n * NV_TEX_ENTRY_BYTES;
   if (bytes < 4096)
      bytes = 4096;

   memset(&req, 0, sizeof(req));
   req.size = bytes;
   req.alignment = 4096;
   req.vram = false;
   req.cpu_access = true;
   req.no_scanout = true;
   req.map_gpu_va = true;

   pool->bo = nv_rm_bo_alloc(rm, &req);
   if (!pool->bo) {
      free(pool);
      return NULL;
   }

   pool->cpu_map = nv_rm_bo_map(pool->bo);
   if (!pool->cpu_map) {
      nv_rm_bo_free(pool->bo);
      free(pool);
      return NULL;
   }

   memset(pool->cpu_map, 0, (size_t)bytes);
   pool->gpu_addr = nv_rm_bo_gpu_offset(pool->bo);
   pool->next_slot = 0;
   return pool;
}

void
nv_tex_pool_destroy(struct nv_tex_pool *pool)
{
   if (!pool)
      return;
   if (pool->bo) {
      nv_rm_bo_unmap(pool->bo);
      nv_rm_bo_free(pool->bo);
   }
   free(pool);
}

int
nv_tex_pool_set_entry(struct nv_tex_pool *pool, int slot,
                      const struct nv_tex_entry *entry)
{
   struct nv_tex_entry *dst;
   uint32_t s;

   if (!pool || !pool->cpu_map || !entry)
      return -1;

   if (slot < 0) {
      s = pool->next_slot;
      if (s >= pool->num_entries)
         s = 0; /* ring overwrite; real driver would grow/refcnt */
      pool->next_slot = s + 1;
   } else {
      s = (uint32_t)slot;
      if (s >= pool->num_entries)
         return -1;
   }

   dst = (struct nv_tex_entry *)((uint8_t *)pool->cpu_map + s * NV_TEX_ENTRY_BYTES);
   memcpy(dst, entry, sizeof(*dst));
   return (int)s;
}

void
nv_tex_pool_emit_bind(struct nv_push *p, struct nv_tex_pool *pool)
{
   uint64_t samp_addr, head_addr;
   uint32_t max_idx;

   if (!p || !pool || !pool->gpu_addr)
      return;

   /* nvidia-3d: sampler pool at offset of samp within entry 0; header at head.
    * Entry layout: samp[8] then head[8], so head_addr = gpu + 32. */
   samp_addr = pool->gpu_addr;
   head_addr = pool->gpu_addr + NV_TEX_SAMPLER_DWORDS * 4;
   max_idx = pool->num_entries ? (pool->num_entries * 2 - 1) : 0;

   /* VIA_HEADER_BINDING: sampler index comes from texture header */
   nv_push_method(p, NVC597_SET_SAMPLER_BINDING,
                  NVC597_SET_SAMPLER_BINDING_V_VIA_HEADER_BINDING);

   nv_push_method(p, NVC597_SET_TEX_SAMPLER_POOL_A,
                  (uint32_t)(samp_addr >> 32) & 0xff);
   nv_push_method(p, NVC597_SET_TEX_SAMPLER_POOL_B,
                  (uint32_t)(samp_addr & 0xffffffffu));
   nv_push_method(p, NVC597_SET_TEX_SAMPLER_POOL_C, 0); /* max index 0 in via-header */

   nv_push_method(p, NVC597_SET_TEX_HEADER_POOL_A,
                  (uint32_t)(head_addr >> 32) & 0xff);
   nv_push_method(p, NVC597_SET_TEX_HEADER_POOL_B,
                  (uint32_t)(head_addr & 0xffffffffu));
   nv_push_method(p, NVC597_SET_TEX_HEADER_POOL_C, max_idx & 0x3fffff);

   nv_tex_invalidate_caches(p);
   pool->pools_emitted = true;
}
