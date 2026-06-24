/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * NVC3C0 compute QMD (Queue Meta Data) v02.02 encoder and method emitters.
 *
 * Field layout from class/clc3c0qmd.h (NVC3C0_QMDV02_02_* MW positions).
 * Launch methods from class/clc3c0.h: SET_INLINE_QMD_ADDRESS_{A,B},
 * LOAD_INLINE_QMD_DATA(i), SEND_PCAS_{A,B}, SEND_SIGNALING_PCAS_B.
 *
 * QMD is 256 bytes (64 dwords / 2048 bits).  Inline path uploads via
 * LOAD_INLINE_QMD_DATA; SEND_PCAS then schedules from QMD GPU address
 * (address >> 8 in SEND_PCAS_A).
 */

#ifndef NV_QMD_H
#define NV_QMD_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "nv_push.h"

#ifdef __cplusplus
extern "C" {
#endif

/* QMD v02.02 size: 2048 bits = 64 dwords = 256 bytes */
#define NV_QMD_DWORDS           64
#define NV_QMD_BYTES            (NV_QMD_DWORDS * 4)
#define NV_QMD_VERSION          2
#define NV_QMD_MAJOR_VERSION    2

/* NVC3C0 method offsets (class/clc3c0.h) — used on NV_PUSH_SUBCH_COMPUTE */
#define NVC3C0_SET_OBJECT                    0x0000
#define NVC3C0_NO_OPERATION                  0x0100
#define NVC3C0_SET_SPA_VERSION               0x0310
#define NVC3C0_SET_CWD_CONTROL               0x0258
#define NVC3C0_SET_CWD_SLOT_COUNT            0x02b0
#define NVC3C0_SEND_PCAS_A                   0x02b4
#define NVC3C0_SEND_PCAS_B                   0x02b8
#define NVC3C0_SEND_SIGNALING_PCAS_B         0x02bc
#define NVC3C0_SET_INLINE_QMD_ADDRESS_A      0x0318
#define NVC3C0_SET_INLINE_QMD_ADDRESS_B      0x031c
#define NVC3C0_LOAD_INLINE_QMD_DATA(i)       (0x0320 + (i) * 4)
/* Non-throttled local mem size (legacy method block used by some paths) */
#define NVC3C0_SET_SHADER_LOCAL_MEMORY_NON_THROTTLED_A  0x02e4
#define NVC3C0_SET_SHADER_LOCAL_MEMORY_NON_THROTTLED_B  0x02e8
#define NVC3C0_SET_SHADER_LOCAL_MEMORY_NON_THROTTLED_C  0x02ec
/* Global LMEM backing address + window (clc3c0.h) */
#define NVC3C0_SET_SHADER_LOCAL_MEMORY_A     0x0790
#define NVC3C0_SET_SHADER_LOCAL_MEMORY_B     0x0794
#define NVC3C0_SET_SHADER_LOCAL_MEMORY_WINDOW_A  0x07b0
#define NVC3C0_SET_SHADER_LOCAL_MEMORY_WINDOW_B  0x07b4
/* Keep aliases for older code that used 0x02e4 block as "local memory" */
#define NVC3C0_SET_SHADER_LOCAL_MEMORY_SIZE_A  NVC3C0_SET_SHADER_LOCAL_MEMORY_NON_THROTTLED_A
#define NVC3C0_SET_SHADER_LOCAL_MEMORY_SIZE_B  NVC3C0_SET_SHADER_LOCAL_MEMORY_NON_THROTTLED_B
#define NVC3C0_SET_SHADER_LOCAL_MEMORY_SIZE_C  NVC3C0_SET_SHADER_LOCAL_MEMORY_NON_THROTTLED_C
#define NVC3C0_INVALIDATE_SHADER_CACHES      0x021c
#define NVC3C0_INVALIDATE_TEXTURE_HEADER_CACHE 0x0228
#define NVC3C0_INVALIDATE_TEXTURE_SAMPLER_CACHE 0x022c
#define NVC3C0_INVALIDATE_TEXTURE_DATA_CACHE 0x0230

/* SEND_SIGNALING_PCAS_B bits */
#define NVC3C0_SEND_SIGNALING_PCAS_B_INVALIDATE_TRUE  (1u << 0)
#define NVC3C0_SEND_SIGNALING_PCAS_B_SCHEDULE_TRUE    (1u << 1)

/* QMD v02.02 MW field positions (inclusive hi:lo) from clc3c0qmd.h */
#define NV_QMD_F_SM_GLOBAL_CACHING_ENABLE              134, 134
#define NV_QMD_F_RUN_CTA_IN_ONE_SM_PARTITION           135, 135
#define NV_QMD_F_IS_QUEUE                              136, 136
#define NV_QMD_F_SEMAPHORE_RELEASE_ENABLE0             138, 138
#define NV_QMD_F_SEMAPHORE_RELEASE_ENABLE1             139, 139
#define NV_QMD_F_REQUIRE_SCHEDULING_PCAS               140, 140
#define NV_QMD_F_DEPENDENT_QMD_SCHEDULE_ENABLE         141, 141
#define NV_QMD_F_INVALIDATE_TEXTURE_HEADER_CACHE       186, 186
#define NV_QMD_F_INVALIDATE_TEXTURE_SAMPLER_CACHE      187, 187
#define NV_QMD_F_INVALIDATE_TEXTURE_DATA_CACHE         188, 188
#define NV_QMD_F_INVALIDATE_SHADER_DATA_CACHE          189, 189
#define NV_QMD_F_INVALIDATE_INSTRUCTION_CACHE          190, 190
#define NV_QMD_F_INVALIDATE_SHADER_CONSTANT_CACHE      191, 191
#define NV_QMD_F_PROGRAM_OFFSET                        287, 256
#define NV_QMD_F_CWD_REFERENCE_COUNT_ID                357, 352
#define NV_QMD_F_CWD_REFERENCE_COUNT_DELTA_MINUS_ONE   365, 358
#define NV_QMD_F_CWD_REFERENCE_COUNT_INCR_ENABLE       367, 367
#define NV_QMD_F_CWD_MEMBAR_TYPE                       369, 368
#define NV_QMD_F_CWD_REFERENCE_COUNT_DECR_ENABLE       371, 371
#define NV_QMD_F_API_VISIBLE_CALL_LIMIT                378, 378
#define NV_QMD_F_SAMPLER_INDEX                         382, 382
#define NV_QMD_F_CTA_RASTER_WIDTH                      415, 384
#define NV_QMD_F_CTA_RASTER_HEIGHT                     431, 416
#define NV_QMD_F_CTA_RASTER_DEPTH                      463, 448
#define NV_QMD_F_SHARED_MEMORY_SIZE                    561, 544
#define NV_QMD_F_MIN_SM_CONFIG_SHARED_MEM_SIZE         568, 562
#define NV_QMD_F_MAX_SM_CONFIG_SHARED_MEM_SIZE         575, 569
#define NV_QMD_F_QMD_VERSION                           579, 576
#define NV_QMD_F_QMD_MAJOR_VERSION                     583, 580
#define NV_QMD_F_CTA_THREAD_DIMENSION0                 607, 592
#define NV_QMD_F_CTA_THREAD_DIMENSION1                 623, 608
#define NV_QMD_F_CTA_THREAD_DIMENSION2                 639, 624
#define NV_QMD_F_CONSTANT_BUFFER_VALID_BASE            640
#define NV_QMD_F_REGISTER_COUNT_V                      656, 648
#define NV_QMD_F_TARGET_SM_CONFIG_SHARED_MEM_SIZE      663, 657
#define NV_QMD_F_BARRIER_COUNT                         959, 955
#define NV_QMD_F_SHADER_LOCAL_MEMORY_LOW_SIZE          951, 928
#define NV_QMD_F_SHADER_LOCAL_MEMORY_HIGH_SIZE         983, 960
#define NV_QMD_F_REGISTER_COUNT                        991, 984
#define NV_QMD_F_SHADER_LOCAL_MEMORY_CRS_SIZE          1015, 992
#define NV_QMD_F_SASS_VERSION                          1023, 1016
#define NV_QMD_F_PROGRAM_ADDRESS_LOWER                 1567, 1536
#define NV_QMD_F_PROGRAM_ADDRESS_UPPER                 1584, 1568

/* Per-CB fields start at bit offset; slot i uses base + i*64 */
#define NV_QMD_CB_ADDR_LOWER_BASE   1024
#define NV_QMD_CB_ADDR_UPPER_BASE   1056
#define NV_QMD_CB_INVALIDATE_BASE   1074
#define NV_QMD_CB_SIZE_SHIFTED4_BASE 1075
#define NV_QMD_CB_SLOT_STRIDE       64
#define NV_QMD_MAX_CBS              8

struct nv_qmd_desc {
   uint64_t program_addr;       /* GPU VA of shader code (SPH+SASS object) */
   uint32_t program_offset;     /* usually 0 for uploaded object start */
   uint32_t grid_x, grid_y, grid_z;  /* CTA raster dimensions (dispatch size) */
   uint32_t cta_x, cta_y, cta_z;     /* local workgroup size */
   uint32_t register_count;     /* SM registers per thread */
   uint32_t shared_mem_size;    /* bytes; aligned by caller */
   uint32_t local_mem_low;      /* per-thread local mem low partition */
   uint32_t local_mem_high;
   uint32_t local_mem_crs;
   uint32_t barrier_count;
   uint8_t  sass_version;       /* SM SASS version, e.g. 0x50 for SM 5.0 */
   bool     sm_global_caching;
   bool     invalidate_caches;  /* texture/shader/instruction caches */
   /* Optional constant buffers (slot 0..7) */
   uint64_t cb_addr[NV_QMD_MAX_CBS];
   uint32_t cb_size[NV_QMD_MAX_CBS];  /* bytes; encoded as size>>4 in QMD */
   uint8_t  cb_valid_mask;            /* bit i => CB i valid */
};

/*
 * Set bits [hi:lo] inclusive in QMD multi-word bit array (little-endian
 * dwords), matching NVIDIA MW(hi:lo) convention in class headers.
 */
static inline void
nv_qmd_mw_set(uint32_t *words, unsigned nwords, unsigned hi, unsigned lo,
              uint64_t value)
{
   unsigned bit, w, b;
   uint64_t mask;
   unsigned width = hi - lo + 1;
   if (width >= 64)
      mask = ~0ull;
   else
      mask = (1ull << width) - 1ull;
   value &= mask;
   for (bit = lo; bit <= hi; bit++) {
      if (!(value & (1ull << (bit - lo))))
         continue;
      w = bit / 32;
      b = bit % 32;
      if (w < nwords)
         words[w] |= (1u << b);
   }
}

static inline void
nv_qmd_set(uint32_t *qmd, unsigned hi, unsigned lo, uint64_t value)
{
   nv_qmd_mw_set(qmd, NV_QMD_DWORDS, hi, lo, value);
}

/** Encode a QMD v02.02 descriptor into a 64-dword buffer. */
static inline void
nv_qmd_encode(const struct nv_qmd_desc *d, uint32_t qmd[NV_QMD_DWORDS])
{
   unsigned i;
   uint32_t regs, shared;
   uint32_t cta_x, cta_y, cta_z;
   uint32_t gx, gy, gz;

   memset(qmd, 0, NV_QMD_BYTES);

   if (!d)
      return;

   regs = d->register_count ? d->register_count : 16;
   if (regs > 255)
      regs = 255;
   shared = d->shared_mem_size;
   if (shared > 0xffffu)
      shared = 0xffffu;

   cta_x = d->cta_x ? d->cta_x : 1;
   cta_y = d->cta_y ? d->cta_y : 1;
   cta_z = d->cta_z ? d->cta_z : 1;
   if (cta_x > 0xffffu) cta_x = 0xffffu;
   if (cta_y > 0xffffu) cta_y = 0xffffu;
   if (cta_z > 0xffffu) cta_z = 0xffffu;

   gx = d->grid_x ? d->grid_x : 1;
   gy = d->grid_y ? d->grid_y : 1;
   gz = d->grid_z ? d->grid_z : 1;

   /* Version */
   nv_qmd_set(qmd, NV_QMD_F_QMD_VERSION, NV_QMD_VERSION);
   nv_qmd_set(qmd, NV_QMD_F_QMD_MAJOR_VERSION, NV_QMD_MAJOR_VERSION);

   /* Caching / scheduling defaults suitable for direct PCAS launch */
   if (d->sm_global_caching)
      nv_qmd_set(qmd, NV_QMD_F_SM_GLOBAL_CACHING_ENABLE, 1);
   nv_qmd_set(qmd, NV_QMD_F_IS_QUEUE, 0); /* grid, not queue */
   nv_qmd_set(qmd, NV_QMD_F_REQUIRE_SCHEDULING_PCAS, 1);
   nv_qmd_set(qmd, NV_QMD_F_API_VISIBLE_CALL_LIMIT, 1); /* NO_CHECK */
   nv_qmd_set(qmd, NV_QMD_F_SAMPLER_INDEX, 0); /* independently */

   if (d->invalidate_caches) {
      nv_qmd_set(qmd, NV_QMD_F_INVALIDATE_TEXTURE_HEADER_CACHE, 1);
      nv_qmd_set(qmd, NV_QMD_F_INVALIDATE_TEXTURE_SAMPLER_CACHE, 1);
      nv_qmd_set(qmd, NV_QMD_F_INVALIDATE_TEXTURE_DATA_CACHE, 1);
      nv_qmd_set(qmd, NV_QMD_F_INVALIDATE_SHADER_DATA_CACHE, 1);
      nv_qmd_set(qmd, NV_QMD_F_INVALIDATE_INSTRUCTION_CACHE, 1);
      nv_qmd_set(qmd, NV_QMD_F_INVALIDATE_SHADER_CONSTANT_CACHE, 1);
   }

   /* Program */
   nv_qmd_set(qmd, NV_QMD_F_PROGRAM_OFFSET, d->program_offset);
   nv_qmd_set(qmd, NV_QMD_F_PROGRAM_ADDRESS_LOWER,
              (uint32_t)(d->program_addr & 0xffffffffu));
   nv_qmd_set(qmd, NV_QMD_F_PROGRAM_ADDRESS_UPPER,
              (uint32_t)((d->program_addr >> 32) & 0x1ffffu));

   /* Grid / CTA dimensions */
   nv_qmd_set(qmd, NV_QMD_F_CTA_RASTER_WIDTH, gx);
   nv_qmd_set(qmd, NV_QMD_F_CTA_RASTER_HEIGHT, gy & 0xffffu);
   nv_qmd_set(qmd, NV_QMD_F_CTA_RASTER_DEPTH, gz & 0xffffu);
   nv_qmd_set(qmd, NV_QMD_F_CTA_THREAD_DIMENSION0, cta_x);
   nv_qmd_set(qmd, NV_QMD_F_CTA_THREAD_DIMENSION1, cta_y);
   nv_qmd_set(qmd, NV_QMD_F_CTA_THREAD_DIMENSION2, cta_z);

   /* Shared / local / registers */
   nv_qmd_set(qmd, NV_QMD_F_SHARED_MEMORY_SIZE, shared);
   /* SM config shared mem size fields are in units of 256B chunks (7 bits) */
   {
      uint32_t sm_cfg = (shared + 255u) / 256u;
      if (sm_cfg > 0x7f) sm_cfg = 0x7f;
      nv_qmd_set(qmd, NV_QMD_F_MIN_SM_CONFIG_SHARED_MEM_SIZE, sm_cfg);
      nv_qmd_set(qmd, NV_QMD_F_MAX_SM_CONFIG_SHARED_MEM_SIZE, sm_cfg);
      nv_qmd_set(qmd, NV_QMD_F_TARGET_SM_CONFIG_SHARED_MEM_SIZE, sm_cfg);
   }
   nv_qmd_set(qmd, NV_QMD_F_REGISTER_COUNT_V, regs & 0x1ffu);
   nv_qmd_set(qmd, NV_QMD_F_REGISTER_COUNT, regs & 0xffu);
   nv_qmd_set(qmd, NV_QMD_F_SHADER_LOCAL_MEMORY_LOW_SIZE,
              d->local_mem_low & 0xffffffu);
   nv_qmd_set(qmd, NV_QMD_F_SHADER_LOCAL_MEMORY_HIGH_SIZE,
              d->local_mem_high & 0xffffffu);
   nv_qmd_set(qmd, NV_QMD_F_SHADER_LOCAL_MEMORY_CRS_SIZE,
              d->local_mem_crs & 0xffffffu);
   if (d->barrier_count)
      nv_qmd_set(qmd, NV_QMD_F_BARRIER_COUNT, d->barrier_count & 0x1fu);
   if (d->sass_version)
      nv_qmd_set(qmd, NV_QMD_F_SASS_VERSION, d->sass_version);

   /* Constant buffers */
   for (i = 0; i < NV_QMD_MAX_CBS; i++) {
      unsigned base;
      if (!(d->cb_valid_mask & (1u << i)))
         continue;
      nv_qmd_set(qmd, NV_QMD_F_CONSTANT_BUFFER_VALID_BASE + i,
                 NV_QMD_F_CONSTANT_BUFFER_VALID_BASE + i, 1);
      base = i * NV_QMD_CB_SLOT_STRIDE;
      nv_qmd_set(qmd,
                 NV_QMD_CB_ADDR_LOWER_BASE + base + 31,
                 NV_QMD_CB_ADDR_LOWER_BASE + base,
                 (uint32_t)(d->cb_addr[i] & 0xffffffffu));
      nv_qmd_set(qmd,
                 NV_QMD_CB_ADDR_UPPER_BASE + base + 16,
                 NV_QMD_CB_ADDR_UPPER_BASE + base,
                 (uint32_t)((d->cb_addr[i] >> 32) & 0x1ffffu));
      if (d->cb_size[i]) {
         uint32_t sz4 = (d->cb_size[i] + 15u) >> 4;
         if (sz4 > 0x1fffu)
            sz4 = 0x1fffu;
         nv_qmd_set(qmd,
                    NV_QMD_CB_SIZE_SHIFTED4_BASE + base + 12,
                    NV_QMD_CB_SIZE_SHIFTED4_BASE + base,
                    sz4);
      }
   }
}

/** Bind NVC3C0 compute object on the compute subchannel. */
static inline void
nv_compute_set_object(struct nv_push *p, uint32_t class_id)
{
   if (!p || !class_id)
      return;
   nv_push_set_subch(p, NV_PUSH_SUBCH_COMPUTE);
   nv_push_method(p, NVC3C0_SET_OBJECT, class_id);
}

/**
 * Emit inline QMD upload + SEND_PCAS schedule on the compute subchannel.
 *
 * @param p            push buffer (subchannel switched to COMPUTE)
 * @param qmd_gpu_addr GPU VA of a 256-byte-aligned QMD scratch buffer
 *                     (used by SET_INLINE_QMD_ADDRESS and SEND_PCAS_A)
 * @param qmd          64 dwords of encoded QMD
 * @param signaling    if true, use SEND_SIGNALING_PCAS_B with invalidate+schedule
 */
static inline void
nv_compute_emit_inline_qmd_launch(struct nv_push *p, uint64_t qmd_gpu_addr,
                                  const uint32_t qmd[NV_QMD_DWORDS],
                                  bool signaling)
{
   unsigned i;
   uint32_t addr_shift8;

   if (!p || !qmd)
      return;

   nv_push_set_subch(p, NV_PUSH_SUBCH_COMPUTE);

   /* SET_INLINE_QMD_ADDRESS: QMD address >> 8, split upper/lower */
   addr_shift8 = (uint32_t)(qmd_gpu_addr >> 8);
   nv_push_method(p, NVC3C0_SET_INLINE_QMD_ADDRESS_A,
                  (uint32_t)((qmd_gpu_addr >> 40) & 0xffffffffu));
   nv_push_method(p, NVC3C0_SET_INLINE_QMD_ADDRESS_B, addr_shift8);

   /* LOAD_INLINE_QMD_DATA(0..63) */
   for (i = 0; i < NV_QMD_DWORDS; i++)
      nv_push_method(p, NVC3C0_LOAD_INLINE_QMD_DATA(i), qmd[i]);

   /* SEND_PCAS_A: QMD address shifted by 8 (same as inline address lower) */
   nv_push_method(p, NVC3C0_SEND_PCAS_A, addr_shift8);
   /* SEND_PCAS_B: from=0, delta=1 (one QMD) */
   nv_push_method(p, NVC3C0_SEND_PCAS_B, (1u << 24) | 0u);

   if (signaling) {
      nv_push_method(p, NVC3C0_SEND_SIGNALING_PCAS_B,
                     NVC3C0_SEND_SIGNALING_PCAS_B_INVALIDATE_TRUE |
                     NVC3C0_SEND_SIGNALING_PCAS_B_SCHEDULE_TRUE);
   }
}

/**
 * High-level: encode QMD from desc and emit compute launch methods.
 * Does not allocate QMD GPU memory — caller provides qmd_gpu_addr for the
 * address fields; for record-only paths pass 0 (inline data still loaded).
 */
static inline void
nv_compute_emit_dispatch(struct nv_push *p, const struct nv_qmd_desc *desc,
                         uint64_t qmd_gpu_addr, uint32_t class_compute)
{
   uint32_t qmd[NV_QMD_DWORDS];

   if (!p || !desc)
      return;

   nv_qmd_encode(desc, qmd);

   if (class_compute)
      nv_compute_set_object(p, class_compute);
   else
      nv_push_set_subch(p, NV_PUSH_SUBCH_COMPUTE);

   nv_compute_emit_inline_qmd_launch(p, qmd_gpu_addr, qmd, true);
}

/**
 * Encode QMD into caller buffer and optionally mirror into host-mapped QMD BO.
 * When qmd_host is non-NULL, the 256-byte QMD is written there so the GPU VA
 * at qmd_gpu_addr contains the same contents as the inline LOAD path — this
 * matches the proprietary driver's "materialize QMD then SEND_PCAS" model and
 * is the prerequisite for GPU-side indirect grid patching (copy indirect
 * {x,y,z} into CTA_RASTER_* MW fields before PCAS).
 */
static inline void
nv_qmd_materialize(const struct nv_qmd_desc *desc, uint32_t qmd_out[NV_QMD_DWORDS],
                   void *qmd_host)
{
   if (!desc || !qmd_out)
      return;
   nv_qmd_encode(desc, qmd_out);
   if (qmd_host)
      memcpy(qmd_host, qmd_out, NV_QMD_BYTES);
}

/**
 * Dispatch using materialised QMD: encode, optionally host-mirror, then launch.
 * Preferred over nv_compute_emit_dispatch when a dedicated QMD scratch BO
 * exists and may be reused across launches in the same command buffer.
 */
static inline void
nv_compute_emit_dispatch_materialized(struct nv_push *p,
                                      const struct nv_qmd_desc *desc,
                                      uint64_t qmd_gpu_addr,
                                      void *qmd_host,
                                      uint32_t class_compute)
{
   uint32_t qmd[NV_QMD_DWORDS];

   if (!p || !desc)
      return;

   nv_qmd_materialize(desc, qmd, qmd_host);

   if (class_compute)
      nv_compute_set_object(p, class_compute);
   else
      nv_push_set_subch(p, NV_PUSH_SUBCH_COMPUTE);

   nv_compute_emit_inline_qmd_launch(p, qmd_gpu_addr, qmd, true);
}

/**
 * Patch CTA raster (grid) dimensions into an already-encoded QMD dword array.
 * Used when indirect dispatch reads {x,y,z} at record time (or later, when
 * a copy engine path writes these fields from a GPU indirect buffer).
 */
static inline void
nv_qmd_patch_grid(uint32_t qmd[NV_QMD_DWORDS],
                  uint32_t gx, uint32_t gy, uint32_t gz)
{
   if (!qmd)
      return;
   if (!gx) gx = 1;
   if (!gy) gy = 1;
   if (!gz) gz = 1;
   /* CTA_RASTER_WIDTH 415:384 spans dwords 12..12 (bits 0..31 of dw12) */
   /* CTA_RASTER_HEIGHT 431:416 spans dwords 13 (bits 0..15) */
   /* CTA_RASTER_DEPTH 463:448 spans dwords 14 (bits 0..15) */
   qmd[12] = gx; /* bits 384..415 fully within dword 12 */
   qmd[13] = (qmd[13] & 0xffff0000u) | (gy & 0xffffu);
   qmd[14] = (qmd[14] & 0xffff0000u) | (gz & 0xffffu);
}

/** Byte offsets of CTA raster dwords within a 256-byte QMD (for CE copies). */
#define NV_QMD_OFF_CTA_RASTER_WIDTH_DW   (12u * 4u)  /* bits 415:384 */
#define NV_QMD_OFF_CTA_RASTER_HEIGHT_DW  (13u * 4u)  /* low 16 of bits 431:416 */
#define NV_QMD_OFF_CTA_RASTER_DEPTH_DW   (14u * 4u)  /* low 16 of bits 463:448 */

/**
 * Compute required global LMEM BO size for a given per-thread local requirement.
 *
 * NVIDIA programs SET_SHADER_LOCAL_MEMORY_NON_THROTTLED_* with a per-SM size
 * and SM count; the backing BO must hold max_sm_count * per_sm_bytes (plus
 * alignment headroom).  We use:
 *   per_thread = align(local_mem_low_bytes, 16)  (SPH/QMD local field)
 *   threads_per_sm = 2048 (conservative Ampere/Turing occupancy ceiling)
 *   per_sm = align(per_thread * threads_per_sm, 0x10000)  (64KB granule)
 *   total  = per_sm * max_sm_count
 *
 * Spill-only shaders with local_mem_low=0 still get a minimum 64KB/SM window
 * so STL/LDL of RA spill slots is valid.
 */
#define NV_LMEM_THREADS_PER_SM_CONSERVATIVE  2048u
#define NV_LMEM_SM_GRANULE_BYTES             0x10000u  /* 64 KiB */
#define NV_LMEM_MIN_PER_SM_BYTES             0x10000u

static inline uint32_t
nv_lmem_align_u32(uint32_t v, uint32_t align)
{
   return (v + align - 1u) & ~(align - 1u);
}

static inline uint32_t
nv_lmem_per_sm_bytes(uint32_t local_mem_low_bytes)
{
   uint32_t per_thread = local_mem_low_bytes ?
      nv_lmem_align_u32(local_mem_low_bytes, 16u) : 16u; /* at least one spill slot */
   uint64_t per_sm = (uint64_t)per_thread * (uint64_t)NV_LMEM_THREADS_PER_SM_CONSERVATIVE;
   if (per_sm < NV_LMEM_MIN_PER_SM_BYTES)
      per_sm = NV_LMEM_MIN_PER_SM_BYTES;
   if (per_sm > 0xffffffffu)
      per_sm = 0xffffffffu;
   return nv_lmem_align_u32((uint32_t)per_sm, NV_LMEM_SM_GRANULE_BYTES);
}

static inline uint64_t
nv_lmem_total_bo_bytes(uint32_t local_mem_low_bytes, uint32_t sm_count)
{
   uint32_t per_sm = nv_lmem_per_sm_bytes(local_mem_low_bytes);
   uint32_t nsm = sm_count ? sm_count : 1u;
   return (uint64_t)per_sm * (uint64_t)nsm;
}

/**
 * Program compute-class global LMEM backing store (address + size window).
 * lmem_gpu_addr should be a device BO sized via nv_lmem_total_bo_bytes().
 * size_bytes is the per-SM non-throttled window (not the full BO size).
 */
static inline void
nv_compute_set_shader_local_memory(struct nv_push *p, uint64_t lmem_gpu_addr,
                                   uint32_t size_bytes, uint32_t max_sm_count)
{
   uint32_t sz = size_bytes ? size_bytes : NV_LMEM_MIN_PER_SM_BYTES;
   uint32_t sz_hi = 0;

   if (!p)
      return;
   /* Size can exceed 4GB in theory; only low 32b is programmed in B, high in A */
   if (sz == 0)
      sz = NV_LMEM_MIN_PER_SM_BYTES;

   nv_push_set_subch(p, NV_PUSH_SUBCH_COMPUTE);
   /* Global LMEM region base */
   nv_push_method(p, NVC3C0_SET_SHADER_LOCAL_MEMORY_A,
                  (uint32_t)((lmem_gpu_addr >> 32) & 0x1ffffu));
   nv_push_method(p, NVC3C0_SET_SHADER_LOCAL_MEMORY_B,
                  (uint32_t)(lmem_gpu_addr & 0xffffffffu));
   /* Window base (often same as region start for simple drivers) */
   nv_push_method(p, NVC3C0_SET_SHADER_LOCAL_MEMORY_WINDOW_A,
                  (uint32_t)((lmem_gpu_addr >> 32) & 0x1ffffu));
   nv_push_method(p, NVC3C0_SET_SHADER_LOCAL_MEMORY_WINDOW_B,
                  (uint32_t)(lmem_gpu_addr & 0xffffffffu));
   /* Non-throttled size: SIZE_UPPER in A (bits 7:0 of high part), lower in B */
   nv_push_method(p, NVC3C0_SET_SHADER_LOCAL_MEMORY_NON_THROTTLED_A, sz_hi);
   nv_push_method(p, NVC3C0_SET_SHADER_LOCAL_MEMORY_NON_THROTTLED_B, sz);
   nv_push_method(p, NVC3C0_SET_SHADER_LOCAL_MEMORY_NON_THROTTLED_C,
                  max_sm_count ? (max_sm_count & 0x1ffu) : 1u);
}

/**
 * Convenience: program LMEM using per-thread requirement and SM count.
 * Computes per-SM window size internally; caller still allocates the BO.
 */
static inline void
nv_compute_set_shader_local_memory_for_shader(struct nv_push *p,
                                              uint64_t lmem_gpu_addr,
                                              uint32_t local_mem_low_bytes,
                                              uint32_t sm_count)
{
   uint32_t per_sm = nv_lmem_per_sm_bytes(local_mem_low_bytes);
   nv_compute_set_shader_local_memory(p, lmem_gpu_addr, per_sm, sm_count);
}

#ifdef __cplusplus
}
#endif

#endif /* NV_QMD_H */
