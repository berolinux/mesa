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
#include "nv_device_info.h"

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
#define NV_QMD_F_DEPENDENT_QMD_TYPE                    142, 142
#define NV_QMD_F_DEPENDENT_QMD_FIELD_COPY              143, 143
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
/* DEPENDENT_QMD_POINTER: QMD VA >> 8 (32-bit field) — NVC3C0_QMDV02_02 */
#define NV_QMD_F_DEPENDENT_QMD_POINTER                 511, 480
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
/* RELEASE0 sema: address + payload (clc3c0qmd.h NVC3C0_QMDV02_02_RELEASE0_*) */
#define NV_QMD_F_RELEASE0_ADDRESS_LOWER                767, 736
#define NV_QMD_F_RELEASE0_ADDRESS_UPPER                775, 768
#define NV_QMD_F_RELEASE0_REDUCTION_OP                 790, 788
#define NV_QMD_F_RELEASE0_REDUCTION_FORMAT             793, 792
#define NV_QMD_F_RELEASE0_REDUCTION_ENABLE             794, 794
#define NV_QMD_F_RELEASE0_STRUCTURE_SIZE               799, 799
#define NV_QMD_F_RELEASE0_PAYLOAD                      831, 800
#define NV_QMD_F_RELEASE1_ADDRESS_LOWER                863, 832
#define NV_QMD_F_RELEASE1_ADDRESS_UPPER                871, 864
#define NV_QMD_F_RELEASE1_REDUCTION_OP                 886, 884
#define NV_QMD_F_RELEASE1_REDUCTION_FORMAT             889, 888
#define NV_QMD_F_RELEASE1_REDUCTION_ENABLE             890, 890
#define NV_QMD_F_RELEASE1_STRUCTURE_SIZE               895, 895
#define NV_QMD_F_RELEASE1_PAYLOAD                      927, 896
#define NV_QMD_F_BARRIER_COUNT                         959, 955
#define NV_QMD_F_SHADER_LOCAL_MEMORY_LOW_SIZE          951, 928
#define NV_QMD_F_SHADER_LOCAL_MEMORY_HIGH_SIZE         983, 960
#define NV_QMD_F_REGISTER_COUNT                        991, 984
#define NV_QMD_F_SHADER_LOCAL_MEMORY_CRS_SIZE          1015, 992
#define NV_QMD_F_SASS_VERSION                          1023, 1016
#define NV_QMD_F_PROGRAM_ADDRESS_LOWER                 1567, 1536
#define NV_QMD_F_PROGRAM_ADDRESS_UPPER                 1584, 1568

/* RELEASE0_STRUCTURE_SIZE values */
#define NV_QMD_RELEASE_STRUCT_FOUR_WORDS               0u
#define NV_QMD_RELEASE_STRUCT_ONE_WORD                 1u
/* DEPENDENT_QMD_TYPE */
#define NV_QMD_DEPENDENT_TYPE_QUEUE                    0u
#define NV_QMD_DEPENDENT_TYPE_GRID                     1u

/* Per-CB fields start at bit offset; slot i uses base + i*64 */
#define NV_QMD_CB_ADDR_LOWER_BASE   1024
#define NV_QMD_CB_ADDR_UPPER_BASE   1056
#define NV_QMD_CB_INVALIDATE_BASE   1074
#define NV_QMD_CB_SIZE_SHIFTED4_BASE 1075
#define NV_QMD_CB_SLOT_STRIDE       64
#define NV_QMD_MAX_CBS              8


/** Align shared memory to 256B (QMD SM config chunk size). */
static inline uint32_t
nv_qmd_align_shared_mem(uint32_t bytes)
{
   if (!bytes)
      return 0;
   return (bytes + 255u) & ~255u;
}

/** Estimate barrier count from workgroup size (conservative; NIR barrier uses BAR.SYNC 0). */
static inline uint32_t
nv_qmd_default_barrier_count(uint32_t cta_x, uint32_t cta_y, uint32_t cta_z)
{
   uint32_t threads = cta_x * cta_y * cta_z;
   if (!threads)
      return 1;
   /* At least 1 barrier slot; more for large workgroups (warp count / 2, min 1 max 16) */
   {
      uint32_t warps = (threads + 31u) / 32u;
      uint32_t bc = warps > 1 ? (warps / 2) : 1;
      if (bc < 1) bc = 1;
      if (bc > 16) bc = 16;
      return bc;
   }
}

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
   /* Completion / chaining (applied after encode if set) */
   uint64_t sema_release0_addr;       /* 0 = disabled */
   uint32_t sema_release0_value;
   uint64_t sema_release1_addr;
   uint32_t sema_release1_value;
   uint64_t dependent_qmd_addr;       /* 0 = no dependent QMD schedule */
   bool     dependent_qmd_copy;       /* field-copy vs pointer-only */
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

/** Read bits [hi:lo] inclusive from QMD (inverse of nv_qmd_mw_set). */
static inline uint64_t
nv_qmd_mw_get(const uint32_t *words, unsigned nwords, unsigned hi, unsigned lo)
{
   unsigned bit, w, b;
   uint64_t value = 0;
   for (bit = lo; bit <= hi; bit++) {
      w = bit / 32;
      b = bit % 32;
      if (w < nwords && (words[w] & (1u << b)))
         value |= (1ull << (bit - lo));
   }
   return value;
}

static inline uint64_t
nv_qmd_get(const uint32_t *qmd, unsigned hi, unsigned lo)
{
   return nv_qmd_mw_get(qmd, NV_QMD_DWORDS, hi, lo);
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
   shared = nv_qmd_align_shared_mem(d->shared_mem_size);
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
   {
      uint32_t bc = d->barrier_count ? d->barrier_count
                     : nv_qmd_default_barrier_count(cta_x, cta_y, cta_z);
      nv_qmd_set(qmd, NV_QMD_F_BARRIER_COUNT, bc & 0x1fu);
   }
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

/* nv_compute_emit_dispatch / nv_qmd_materialize / nv_compute_emit_dispatch_materialized
 * follow nv_qmd_encode_full (below) so sema/dependent sideband is always applied. */

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
 * QMD completion semaphore release (v02.02 SEMAPHORE_RELEASE_ENABLE0 + RELEASE0_*).
 * Field layout from NVC3C0_QMDV02_02_RELEASE0_* in class/clc3c0qmd.h (also in
 * mesa/src/nouveau/headers/nvidia/classes/clc3c0qmd.h — NVIDIA class headers).
 *
 * sema_gpu_addr: semaphore structure VA (one-word or four-word report sema).
 * sema_value: payload written on completion (non-reduction release = set payload).
 * one_word: true => RELEASE0_STRUCTURE_SIZE_ONE_WORD (single dword sema).
 */
static inline void
nv_qmd_set_semaphore_release0_ex(uint32_t qmd[NV_QMD_DWORDS],
                                 uint64_t sema_gpu_addr, uint32_t sema_value,
                                 bool one_word, bool reduction_enable)
{
   if (!qmd || !sema_gpu_addr)
      return;
   nv_qmd_set(qmd, NV_QMD_F_SEMAPHORE_RELEASE_ENABLE0, 1);
   nv_qmd_set(qmd, NV_QMD_F_RELEASE0_ADDRESS_LOWER,
              (uint32_t)(sema_gpu_addr & 0xffffffffu));
   nv_qmd_set(qmd, NV_QMD_F_RELEASE0_ADDRESS_UPPER,
              (uint32_t)((sema_gpu_addr >> 32) & 0xffu));
   nv_qmd_set(qmd, NV_QMD_F_RELEASE0_PAYLOAD, sema_value);
   nv_qmd_set(qmd, NV_QMD_F_RELEASE0_STRUCTURE_SIZE,
              one_word ? NV_QMD_RELEASE_STRUCT_ONE_WORD
                       : NV_QMD_RELEASE_STRUCT_FOUR_WORDS);
   if (reduction_enable) {
      nv_qmd_set(qmd, NV_QMD_F_RELEASE0_REDUCTION_ENABLE, 1);
      nv_qmd_set(qmd, NV_QMD_F_RELEASE0_REDUCTION_FORMAT, 0); /* U32 */
      nv_qmd_set(qmd, NV_QMD_F_RELEASE0_REDUCTION_OP, 0);     /* RED_ADD */
   }
}

static inline void
nv_qmd_set_semaphore_release0(uint32_t qmd[NV_QMD_DWORDS],
                              uint64_t sema_gpu_addr, uint32_t sema_value)
{
   if (!qmd)
      return;
   nv_qmd_set_semaphore_release0_ex(qmd, sema_gpu_addr, sema_value,
                                    true /* one_word */, false /* reduction */);
}

/** Second sema release slot (SEMAPHORE_RELEASE_ENABLE1 / RELEASE1_*). */
static inline void
nv_qmd_set_semaphore_release1(uint32_t qmd[NV_QMD_DWORDS],
                              uint64_t sema_gpu_addr, uint32_t sema_value,
                              bool one_word)
{
   if (!qmd || !sema_gpu_addr)
      return;
   nv_qmd_set(qmd, NV_QMD_F_SEMAPHORE_RELEASE_ENABLE1, 1);
   nv_qmd_set(qmd, NV_QMD_F_RELEASE1_ADDRESS_LOWER,
              (uint32_t)(sema_gpu_addr & 0xffffffffu));
   nv_qmd_set(qmd, NV_QMD_F_RELEASE1_ADDRESS_UPPER,
              (uint32_t)((sema_gpu_addr >> 32) & 0xffu));
   nv_qmd_set(qmd, NV_QMD_F_RELEASE1_PAYLOAD, sema_value);
   nv_qmd_set(qmd, NV_QMD_F_RELEASE1_STRUCTURE_SIZE,
              one_word ? NV_QMD_RELEASE_STRUCT_ONE_WORD
                       : NV_QMD_RELEASE_STRUCT_FOUR_WORDS);
}

/**
 * Enable dependent QMD schedule: DEPENDENT_QMD_POINTER = next_qmd_gpu_addr >> 8
 * (MW 511:480).  type_grid selects DEPENDENT_QMD_TYPE_GRID vs QUEUE.
 */
static inline void
nv_qmd_set_dependent_qmd_ex(uint32_t qmd[NV_QMD_DWORDS],
                            uint64_t next_qmd_gpu_addr,
                            uint32_t type_grid_or_queue,
                            bool field_copy)
{
   if (!qmd || !next_qmd_gpu_addr)
      return;
   nv_qmd_set(qmd, NV_QMD_F_DEPENDENT_QMD_SCHEDULE_ENABLE, 1);
   nv_qmd_set(qmd, NV_QMD_F_DEPENDENT_QMD_TYPE, type_grid_or_queue ? 1 : 0);
   if (field_copy)
      nv_qmd_set(qmd, NV_QMD_F_DEPENDENT_QMD_FIELD_COPY, 1);
   nv_qmd_set(qmd, NV_QMD_F_DEPENDENT_QMD_POINTER,
              (uint32_t)(next_qmd_gpu_addr >> 8));
}

static inline void
nv_qmd_set_dependent_qmd(uint32_t qmd[NV_QMD_DWORDS], uint64_t next_qmd_gpu_addr)
{
   nv_qmd_set_dependent_qmd_ex(qmd, next_qmd_gpu_addr,
                               NV_QMD_DEPENDENT_TYPE_GRID, false);
}

/** Apply sema/dependent fields from desc after nv_qmd_encode(). */
static inline void
nv_qmd_apply_desc_sideband(const struct nv_qmd_desc *d,
                           uint32_t qmd[NV_QMD_DWORDS])
{
   if (!d || !qmd)
      return;
   if (d->sema_release0_addr)
      nv_qmd_set_semaphore_release0(qmd, d->sema_release0_addr,
                                    d->sema_release0_value);
   if (d->sema_release1_addr)
      nv_qmd_set_semaphore_release1(qmd, d->sema_release1_addr,
                                    d->sema_release1_value, true /* one_word */);
   if (d->dependent_qmd_addr)
      nv_qmd_set_dependent_qmd_ex(qmd, d->dependent_qmd_addr,
                                  NV_QMD_DEPENDENT_TYPE_GRID,
                                  d->dependent_qmd_copy);
}

/** Encode QMD then apply sema/dependent sideband from the same desc. */
static inline void
nv_qmd_encode_full(const struct nv_qmd_desc *d, uint32_t qmd[NV_QMD_DWORDS])
{
   nv_qmd_encode(d, qmd);
   nv_qmd_apply_desc_sideband(d, qmd);
}

/**
 * High-level: encode QMD (with sema/dependent sideband) and emit compute launch.
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

   nv_qmd_encode_full(desc, qmd);

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
   nv_qmd_encode_full(desc, qmd_out);
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
 * Invalidate caches on QMD schedule (texture/shader) — set bits in QMD so
 * SEND_PCAS with invalidate is not strictly required for every launch.
 */
static inline void
nv_qmd_set_cache_invalidate_all(uint32_t qmd[NV_QMD_DWORDS])
{
   if (!qmd)
      return;
   nv_qmd_set(qmd, NV_QMD_F_INVALIDATE_TEXTURE_HEADER_CACHE, 1);
   nv_qmd_set(qmd, NV_QMD_F_INVALIDATE_TEXTURE_SAMPLER_CACHE, 1);
   nv_qmd_set(qmd, NV_QMD_F_INVALIDATE_TEXTURE_DATA_CACHE, 1);
}

/**
 * Prepare QMD for indirect dispatch path B: placeholder grid 1x1x1,
 * cache invalidates on, optional sema release for CPU wait.
 */
static inline void
nv_qmd_prepare_indirect_placeholder(uint32_t qmd[NV_QMD_DWORDS],
                                    const struct nv_qmd_desc *desc,
                                    uint64_t sema_gpu_addr,
                                    uint32_t sema_value)
{
   struct nv_qmd_desc d;
   if (!qmd || !desc)
      return;
   d = *desc;
   d.grid_x = 1;
   d.grid_y = 1;
   d.grid_z = 1;
   if (sema_gpu_addr) {
      d.sema_release0_addr = sema_gpu_addr;
      d.sema_release0_value = sema_value;
   }
   nv_qmd_encode_full(&d, qmd);
   nv_qmd_set_cache_invalidate_all(qmd);
}

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
#define NV_LMEM_SM_COUNT_FALLBACK            1u
#define NV_LMEM_SM_COUNT_CAP                 256u      /* sanity for alloc sizing */

static inline uint32_t
nv_lmem_align_u32(uint32_t v, uint32_t align)
{
   return (v + align - 1u) & ~(align - 1u);
}

/**
 * tick105: derive concurrent threads/SM from GR probe when available.
 * max_warps_per_sm * max_threads_per_warp (default 32) is occupancy ceiling;
 * fall back to 2048 conservative if probe missing.
 */
static inline uint32_t
nv_lmem_threads_per_sm_from_gr(uint32_t max_warps_per_sm,
                               uint32_t max_threads_per_warp)
{
   uint32_t tw = max_threads_per_warp ? max_threads_per_warp : 32u;
   uint64_t thr;

   if (!max_warps_per_sm)
      return NV_LMEM_THREADS_PER_SM_CONSERVATIVE;
   thr = (uint64_t)max_warps_per_sm * (uint64_t)tw;
   if (thr < 32u)
      thr = 32u;
   if (thr > NV_LMEM_THREADS_PER_SM_CONSERVATIVE)
      thr = NV_LMEM_THREADS_PER_SM_CONSERVATIVE; /* keep conservative upper bound */
   if (thr > 0xffffffffu)
      thr = 0xffffffffu;
   return (uint32_t)thr;
}

/** tick105: SM/core count for LMEM window — prefer gpu_core_count, then tpc_count. */
static inline uint32_t
nv_lmem_sm_count_from_info(const struct nv_device_info *info)
{
   uint32_t n = NV_LMEM_SM_COUNT_FALLBACK;
   if (!info)
      return n;
   if (info->gpu_core_count)
      n = info->gpu_core_count;
   else if (info->tpc_count)
      n = info->tpc_count;
   else if (info->gpc_count)
      n = info->gpc_count;
   if (n > NV_LMEM_SM_COUNT_CAP)
      n = NV_LMEM_SM_COUNT_CAP;
   if (!n)
      n = NV_LMEM_SM_COUNT_FALLBACK;
   return n;
}

/**
 * Effective local_mem_low including thread_stack_scaling floor (tick102/105).
 * Callers pass shader spill size; probe scale ensures minimum stack reservation.
 */
static inline uint32_t
nv_lmem_effective_local_bytes(uint32_t local_mem_low_bytes,
                              uint32_t thread_stack_scaling)
{
   uint32_t floor_lmem = 0;
   if (thread_stack_scaling) {
      floor_lmem = thread_stack_scaling * 16u;
      if (floor_lmem < 16u)
         floor_lmem = 16u;
   }
   return local_mem_low_bytes > floor_lmem ? local_mem_low_bytes : floor_lmem;
}

static inline uint32_t
nv_lmem_per_sm_bytes_ex(uint32_t local_mem_low_bytes,
                        uint32_t max_warps_per_sm,
                        uint32_t max_threads_per_warp)
{
   uint32_t per_thread = local_mem_low_bytes ?
      nv_lmem_align_u32(local_mem_low_bytes, 16u) : 16u; /* at least one spill slot */
   uint32_t thr_sm = nv_lmem_threads_per_sm_from_gr(max_warps_per_sm,
                                                    max_threads_per_warp);
   uint64_t per_sm = (uint64_t)per_thread * (uint64_t)thr_sm;
   if (per_sm < NV_LMEM_MIN_PER_SM_BYTES)
      per_sm = NV_LMEM_MIN_PER_SM_BYTES;
   if (per_sm > 0xffffffffu)
      per_sm = 0xffffffffu;
   return nv_lmem_align_u32((uint32_t)per_sm, NV_LMEM_SM_GRANULE_BYTES);
}

static inline uint32_t
nv_lmem_per_sm_bytes(uint32_t local_mem_low_bytes)
{
   return nv_lmem_per_sm_bytes_ex(local_mem_low_bytes, 0, 0);
}

static inline uint64_t
nv_lmem_total_bo_bytes_ex(uint32_t local_mem_low_bytes, uint32_t sm_count,
                          uint32_t max_warps_per_sm,
                          uint32_t max_threads_per_warp)
{
   uint32_t per_sm = nv_lmem_per_sm_bytes_ex(local_mem_low_bytes,
                                             max_warps_per_sm,
                                             max_threads_per_warp);
   uint32_t nsm = sm_count ? sm_count : NV_LMEM_SM_COUNT_FALLBACK;
   if (nsm > NV_LMEM_SM_COUNT_CAP)
      nsm = NV_LMEM_SM_COUNT_CAP;
   return (uint64_t)per_sm * (uint64_t)nsm;
}

static inline uint64_t
nv_lmem_total_bo_bytes(uint32_t local_mem_low_bytes, uint32_t sm_count)
{
   return nv_lmem_total_bo_bytes_ex(local_mem_low_bytes, sm_count, 0, 0);
}

/** tick105: size LMEM BO from full nv_device_info + shader local requirement */
static inline uint64_t
nv_lmem_total_bo_bytes_from_info(const struct nv_device_info *info,
                                 uint32_t local_mem_low_bytes)
{
   uint32_t eff, sm;
   if (!info)
      return nv_lmem_total_bo_bytes(local_mem_low_bytes, NV_LMEM_SM_COUNT_FALLBACK);
   eff = nv_lmem_effective_local_bytes(local_mem_low_bytes,
                                       info->thread_stack_scaling);
   sm = nv_lmem_sm_count_from_info(info);
   return nv_lmem_total_bo_bytes_ex(eff, sm, info->max_warps_per_sm,
                                    info->max_threads_per_warp);
}

/**
 * tick107: CRS (call/return stack) local mem size from thread_stack_scaling.
 * Proprietary paths often reserve a small CRS window separate from spill (low).
 * Heuristic: scale * 32B, min 64, max 4096; refine on silicon traces.
 */
static inline uint32_t
nv_qmd_default_crs_bytes(uint32_t thread_stack_scaling)
{
   uint32_t crs;
   if (!thread_stack_scaling)
      return 64u;
   crs = thread_stack_scaling * 32u;
   if (crs < 64u)
      crs = 64u;
   if (crs > 4096u)
      crs = 4096u;
   return crs;
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

/** tick105: program LMEM using GR probe (threads/SM, core count, stack scale) */
static inline void
nv_compute_set_shader_local_memory_from_info(struct nv_push *p,
                                             uint64_t lmem_gpu_addr,
                                             uint32_t local_mem_low_bytes,
                                             const struct nv_device_info *info)
{
   uint32_t eff, sm, per_sm;
   if (!p || !lmem_gpu_addr)
      return;
   if (!info) {
      nv_compute_set_shader_local_memory_for_shader(p, lmem_gpu_addr,
                                                    local_mem_low_bytes, 1);
      return;
   }
   eff = nv_lmem_effective_local_bytes(local_mem_low_bytes,
                                       info->thread_stack_scaling);
   sm = nv_lmem_sm_count_from_info(info);
   per_sm = nv_lmem_per_sm_bytes_ex(eff, info->max_warps_per_sm,
                                    info->max_threads_per_warp);
   nv_compute_set_shader_local_memory(p, lmem_gpu_addr, per_sm, sm);
}

/**
 * tick107: recommended global LMEM BO size (spill + fractional CRS in pool).
 * CRS is primarily per-thread in QMD; still inflate eff slightly for BO window.
 */
static inline uint64_t
nv_compute_lmem_bo_bytes_from_info(uint32_t local_mem_low_bytes,
                                   const struct nv_device_info *info)
{
   uint32_t scale = info ? info->thread_stack_scaling : 0;
   uint32_t eff = nv_lmem_effective_local_bytes(local_mem_low_bytes, scale);
   uint32_t crs = nv_qmd_default_crs_bytes(scale);
   if (crs > 0 && eff < local_mem_low_bytes + crs)
      eff = local_mem_low_bytes + (crs / 4u);
   if (info)
      return nv_lmem_total_bo_bytes_from_info(info, eff);
   return nv_lmem_total_bo_bytes(eff, NV_LMEM_SM_COUNT_FALLBACK);
}

/**
 * Program SPA version + CWD control before first QMD in a compute channel.
 * spa_version: SM SASS version byte (e.g. 0x86 Ampere, 0x89 Ada, 0x50 Maxwell).
 * cwd_slot_count: cooperative work distributor slots (0 = skip / leave default).
 */
static inline void
nv_compute_emit_init_state(struct nv_push *p, uint32_t class_compute,
                           uint8_t spa_version, uint32_t cwd_slot_count)
{
   if (!p)
      return;
   if (class_compute)
      nv_compute_set_object(p, class_compute);
   else
      nv_push_set_subch(p, NV_PUSH_SUBCH_COMPUTE);
   if (spa_version)
      nv_push_method(p, NVC3C0_SET_SPA_VERSION, (uint32_t)spa_version);
   /* CWD_CONTROL: use SM / SM_DISABLE as 0 (enable all); refinements later */
   nv_push_method(p, NVC3C0_SET_CWD_CONTROL, 0);
   if (cwd_slot_count)
      nv_push_method(p, NVC3C0_SET_CWD_SLOT_COUNT, cwd_slot_count);
}

/** Method-level shader/texture cache invalidates (in addition to QMD bits). */
static inline void
nv_compute_emit_invalidate_caches(struct nv_push *p)
{
   if (!p)
      return;
   nv_push_set_subch(p, NV_PUSH_SUBCH_COMPUTE);
   nv_push_method(p, NVC3C0_INVALIDATE_SHADER_CACHES, 0x1); /* instruction */
   nv_push_method(p, NVC3C0_INVALIDATE_TEXTURE_HEADER_CACHE, 0);
   nv_push_method(p, NVC3C0_INVALIDATE_TEXTURE_SAMPLER_CACHE, 0);
   nv_push_method(p, NVC3C0_INVALIDATE_TEXTURE_DATA_CACHE, 0);
}

/**
 * tick133: compute channel bring-up — SET_OBJECT + SPA/CWD + method invalidates
 * + optional LMEM window.  Call before first QMD/PCAS launch on a fresh channel.
 *
 * spa_version: 0 uses 0x53 (Ampere-era default, same as G3 pipeline bind).
 * lmem_gpu_addr: 0 skips SET_SHADER_LOCAL_MEMORY_*.
 * local_mem_low_bytes: per-thread low partition (0 = 256 B smoke default).
 */
static inline void
nv_compute_emit_g2_bringup_init(struct nv_push *p, uint32_t class_compute,
                                uint8_t spa_version, uint32_t cwd_slot_count,
                                uint64_t lmem_gpu_addr,
                                uint32_t local_mem_low_bytes)
{
   uint8_t spa = spa_version ? spa_version : (uint8_t)0x53u;
   uint32_t low = local_mem_low_bytes ? local_mem_low_bytes : 256u;

   if (!p)
      return;
   nv_compute_emit_init_state(p, class_compute, spa, cwd_slot_count);
   nv_compute_emit_invalidate_caches(p);
   if (lmem_gpu_addr) {
      nv_push_set_subch(p, NV_PUSH_SUBCH_COMPUTE);
      nv_push_method(p, NVC3C0_SET_SHADER_LOCAL_MEMORY_A,
                     (uint32_t)((lmem_gpu_addr >> 32) & 0xffu));
      nv_push_method(p, NVC3C0_SET_SHADER_LOCAL_MEMORY_B,
                     (uint32_t)(lmem_gpu_addr & 0xffffffffu));
      nv_push_method(p, NVC3C0_SET_SHADER_LOCAL_MEMORY_NON_THROTTLED_A, low);
      nv_push_method(p, NVC3C0_SET_SHADER_LOCAL_MEMORY_NON_THROTTLED_B, 0);
      nv_push_method(p, NVC3C0_SET_SHADER_LOCAL_MEMORY_NON_THROTTLED_C, 0);
   }
}

/**
 * Attach QMD sema release0 to a desc (one-word payload when grid completes).
 * Returns sema_payload for CPU wait via nvidia_sema_wait_geq / nv_fence_wait.
 */
static inline void
nv_qmd_desc_set_sema_release0(struct nv_qmd_desc *d,
                              uint64_t sema_gpu_addr, uint32_t sema_payload)
{
   if (!d || !sema_gpu_addr || !sema_payload)
      return;
   d->sema_release0_addr = sema_gpu_addr;
   d->sema_release0_value = sema_payload;
}

/**
 * tick102: clamp CTA dimensions / local mem using GR probe (max_warps_per_sm,
 * thread_stack_scaling).  Safe no-op when probe fields are zero.
 *
 * warps_needed ≈ ceil(cta_x*cta_y*cta_z / 32); must not exceed max_warps_per_sm
 * when that is known.  thread_stack_scaling (when non-zero) is an RM-reported
 * multiplier for per-thread stack/local reservation — applied as a minimum
 * local_mem_low floor of 16*scale bytes (aligned).
 */
static inline void
nv_qmd_desc_apply_gr_limits(struct nv_qmd_desc *d, uint32_t max_warps_per_sm,
                            uint32_t thread_stack_scaling)
{
   uint32_t threads, warps_need, max_threads;

   if (!d)
      return;

   threads = d->cta_x * d->cta_y * d->cta_z;
   if (!threads)
      threads = 1;

   if (max_warps_per_sm) {
      max_threads = max_warps_per_sm * 32u;
      if (threads > max_threads) {
         /* Prefer shrinking X then Y/Z; keep at least 1×1×1 */
         d->cta_x = max_threads >= 32 ? 32 : (max_threads ? max_threads : 1);
         d->cta_y = 1;
         d->cta_z = 1;
         threads = d->cta_x;
      }
      warps_need = (threads + 31u) / 32u;
      if (warps_need > max_warps_per_sm && d->cta_x > 1) {
         d->cta_x = max_warps_per_sm * 32u;
         if (!d->cta_x)
            d->cta_x = 1;
      }
   }

   if (thread_stack_scaling) {
      uint32_t floor_lmem = thread_stack_scaling * 16u;
      if (floor_lmem < 16u)
         floor_lmem = 16u;
      if (d->local_mem_low < floor_lmem)
         d->local_mem_low = floor_lmem;
   }
}

/**
 * Build a minimal compute QMD desc for vertical-slice / smoke testing:
 * 1x1x1 grid, 1x1x1 CTA, given shader GPU VA, sema on completion.
 * program_addr may be 0 for encode-only tests (hardware will fault if launched).
 */
static inline void
nv_qmd_desc_init_smoke(struct nv_qmd_desc *d, uint64_t program_gpu_addr,
                       uint32_t register_count, uint8_t sass_version,
                       uint64_t sema_gpu_addr, uint32_t sema_payload)
{
   if (!d)
      return;
   memset(d, 0, sizeof(*d));
   d->program_addr = program_gpu_addr;
   d->program_offset = 0;
   d->grid_x = 1;
   d->grid_y = 1;
   d->grid_z = 1;
   d->cta_x = 1;
   d->cta_y = 1;
   d->cta_z = 1;
   d->register_count = register_count ? register_count : 16;
   d->shared_mem_size = 0;
   d->local_mem_low = 0;
   d->barrier_count = 1;
   d->sass_version = sass_version ? sass_version : 0x50;
   d->sm_global_caching = true;
   d->invalidate_caches = true;
   if (sema_gpu_addr && sema_payload)
      nv_qmd_desc_set_sema_release0(d, sema_gpu_addr, sema_payload);
}

/**
 * tick132: apply conservative per-thread local-mem defaults when unset.
 * low/high/crs partitions are generation-dependent; 0 keeps encoder zeros
 * which works for trivial EXIT/store smoke but fails larger NIR kernels.
 */
static inline void
nv_qmd_desc_apply_local_mem_defaults(struct nv_qmd_desc *d,
                                     uint32_t low_bytes, uint32_t high_bytes,
                                     uint32_t crs_bytes)
{
   if (!d)
      return;
   if (!d->local_mem_low && low_bytes)
      d->local_mem_low = low_bytes;
   if (!d->local_mem_high && high_bytes)
      d->local_mem_high = high_bytes;
   if (!d->local_mem_crs && crs_bytes)
      d->local_mem_crs = crs_bytes;
}

/** tick132: enable sema release0 + invalidate caches for G2 bring-up QMD. */
static inline void
nv_qmd_desc_apply_g2_bringup_defaults(struct nv_qmd_desc *d,
                                      uint64_t sema_gpu_addr,
                                      uint32_t sema_payload)
{
   if (!d)
      return;
   d->sm_global_caching = true;
   d->invalidate_caches = true;
   if (!d->barrier_count)
      d->barrier_count = nv_qmd_default_barrier_count(d->cta_x, d->cta_y,
                                                       d->cta_z);
   if (sema_gpu_addr)
      nv_qmd_desc_set_sema_release0(d, sema_gpu_addr, sema_payload);
   /* Trivial smoke: 256 B low local is enough for EXIT/store; refine on HW */
   nv_qmd_desc_apply_local_mem_defaults(d, 256u, 0u, 0u);
}

/**
 * tick127: multi-CTA smoke QMD (grid/cta non-1) for G2 ladder stress.
 * sema_payload 0 skips sema release0 (caller may add later).
 */
static inline void
nv_qmd_desc_init_smoke_grid(struct nv_qmd_desc *d, uint64_t program_gpu_addr,
                            uint32_t register_count, uint8_t sass_version,
                            uint32_t grid_x, uint32_t grid_y, uint32_t grid_z,
                            uint32_t cta_x, uint32_t cta_y, uint32_t cta_z,
                            uint64_t sema_gpu_addr, uint32_t sema_payload)
{
   nv_qmd_desc_init_smoke(d, program_gpu_addr, register_count, sass_version,
                          sema_gpu_addr, sema_payload);
   if (!d)
      return;
   d->grid_x = grid_x ? grid_x : 1;
   d->grid_y = grid_y ? grid_y : 1;
   d->grid_z = grid_z ? grid_z : 1;
   d->cta_x = cta_x ? cta_x : 1;
   d->cta_y = cta_y ? cta_y : 1;
   d->cta_z = cta_z ? cta_z : 1;
   if (d->cta_x > 1024)
      d->cta_x = 1024;
   if (d->cta_y > 1024)
      d->cta_y = 1024;
   if (d->cta_z > 64)
      d->cta_z = 64;
}

/**
 * tick133: full G2 smoke slice — bringup init + QMD encode (G2 defaults) +
 * materialize + PCAS launch.  program_gpu_addr may be 0 for encode-only tests.
 * Must follow nv_qmd_desc_init_smoke_grid / apply_g2_bringup_defaults in this header.
 * Returns 0 on success, -1 on bad args.
 */
static inline int
nv_compute_emit_g2_smoke_slice(struct nv_push *p, uint32_t class_compute,
                               uint64_t program_gpu_addr,
                               uint32_t register_count, uint8_t sass_version,
                               uint64_t qmd_gpu_addr, void *qmd_host,
                               uint64_t lmem_gpu_addr,
                               uint64_t sema_gpu_addr, uint32_t sema_payload,
                               uint32_t grid_x, uint32_t cta_x)
{
   struct nv_qmd_desc d;
   uint32_t qmd[NV_QMD_DWORDS];

   if (!p)
      return -1;

   nv_compute_emit_g2_bringup_init(p, class_compute, 0, 0, lmem_gpu_addr, 256u);

   nv_qmd_desc_init_smoke_grid(&d, program_gpu_addr, register_count,
                               sass_version,
                               grid_x ? grid_x : 1, 1, 1,
                               cta_x ? cta_x : 1, 1, 1,
                               0, 0);
   nv_qmd_desc_apply_g2_bringup_defaults(&d, sema_gpu_addr, sema_payload);
   nv_qmd_materialize(&d, qmd, qmd_host);

   if (class_compute)
      nv_compute_set_object(p, class_compute);
   else
      nv_push_set_subch(p, NV_PUSH_SUBCH_COMPUTE);
   nv_compute_emit_inline_qmd_launch(p, qmd_gpu_addr, qmd, true);
   return 0;
}

/** Encode smoke QMD with optional non-1x1x1 grid; returns 0 on success. */
static inline int
nv_qmd_build_compute_smoke_grid(uint32_t qmd_out[NV_QMD_DWORDS],
                                uint64_t program_gpu_addr,
                                uint32_t register_count, uint8_t sass_version,
                                uint32_t grid_x, uint32_t cta_x,
                                uint64_t sema_gpu_addr, uint32_t sema_payload)
{
   struct nv_qmd_desc d;
   if (!qmd_out)
      return -1;
   nv_qmd_desc_init_smoke_grid(&d, program_gpu_addr, register_count,
                               sass_version, grid_x, 1, 1, cta_x, 1, 1,
                               sema_gpu_addr, sema_payload);
   nv_qmd_encode_full(&d, qmd_out);
   return 0;
}

/** tick102: smoke QMD + apply GR limits from nv_device_info probe fields */
static inline void
nv_qmd_desc_init_smoke_gr(struct nv_qmd_desc *d, uint64_t program_gpu_addr,
                          uint32_t register_count, uint8_t sass_version,
                          uint64_t sema_gpu_addr, uint32_t sema_payload,
                          uint32_t max_warps_per_sm,
                          uint32_t thread_stack_scaling)
{
   nv_qmd_desc_init_smoke(d, program_gpu_addr, register_count, sass_version,
                          sema_gpu_addr, sema_payload);
   nv_qmd_desc_apply_gr_limits(d, max_warps_per_sm, thread_stack_scaling);
}

/** tick104: apply GR limits from nv_device_info (NULL-safe). */
static inline void
nv_qmd_desc_apply_device_gr_limits(struct nv_qmd_desc *d,
                                   const struct nv_device_info *info)
{
   if (!d || !info)
      return;
   nv_qmd_desc_apply_gr_limits(d, info->max_warps_per_sm,
                               info->thread_stack_scaling);
}

/**
 * tick107: apply LMEM low/high/crs from shader spill + GR probe into QMD desc.
 * local_mem_low_bytes is shader spill; high partition usually 0 for simple kernels.
 */
static inline void
nv_qmd_desc_apply_lmem_from_info(struct nv_qmd_desc *d,
                                 uint32_t local_mem_low_bytes,
                                 const struct nv_device_info *info)
{
   uint32_t scale = info ? info->thread_stack_scaling : 0;
   if (!d)
      return;
   d->local_mem_low = nv_lmem_effective_local_bytes(local_mem_low_bytes, scale);
   if (!d->local_mem_high)
      d->local_mem_high = 0;
   if (!d->local_mem_crs)
      d->local_mem_crs = nv_qmd_default_crs_bytes(scale);
}

/** tick107: set dependent QMD chain on desc (schedule enable applied at encode_full). */
static inline void
nv_qmd_desc_set_dependent(struct nv_qmd_desc *d, uint64_t next_qmd_gpu_addr,
                          bool field_copy)
{
   if (!d)
      return;
   d->dependent_qmd_addr = next_qmd_gpu_addr;
   d->dependent_qmd_copy = field_copy;
}

/**
 * tick107: smoke QMD fully parameterized from nv_device_info (GR + optional LMEM).
 * sass_version_override 0 => derive coarse default from sm_version.
 */
static inline void
nv_qmd_desc_init_smoke_device(struct nv_qmd_desc *d, uint64_t program_gpu_addr,
                             uint32_t register_count, uint8_t sass_version_override,
                             uint64_t sema_gpu_addr, uint32_t sema_payload,
                             uint32_t local_mem_low_bytes,
                             const struct nv_device_info *info)
{
   uint8_t sass = sass_version_override;
   if (!d)
      return;
   if (!sass && info && info->sm_version) {
      uint32_t sm = info->sm_version;
      if (sm >= 0x50 && sm <= 0xff)
         sass = (uint8_t)sm;
      else if ((sm >> 8) && (sm >> 8) <= 0xff)
         sass = (uint8_t)((sm >> 8) & 0xffu);
   }
   nv_qmd_desc_init_smoke(d, program_gpu_addr, register_count, sass,
                          sema_gpu_addr, sema_payload);
   if (info)
      nv_qmd_desc_apply_device_gr_limits(d, info);
   nv_qmd_desc_apply_lmem_from_info(d, local_mem_low_bytes, info);
}

/**
 * tick107: two-QMD chain — primary completes sema; optional dependent QMD addr.
 * Useful for smoke G2 extensions (prep + main) without second SEND_PCAS.
 */
static inline void
nv_qmd_desc_init_smoke_chain(struct nv_qmd_desc *primary,
                             struct nv_qmd_desc *dependent_opt,
                             uint64_t program_gpu_addr,
                             uint32_t register_count, uint8_t sass_version,
                             uint64_t sema_gpu_addr, uint32_t sema_payload,
                             uint64_t dependent_qmd_gpu_addr,
                             const struct nv_device_info *info)
{
   nv_qmd_desc_init_smoke_device(primary, program_gpu_addr, register_count,
                                sass_version, sema_gpu_addr, sema_payload,
                                0, info);
   if (dependent_qmd_gpu_addr)
      nv_qmd_desc_set_dependent(primary, dependent_qmd_gpu_addr, false);
   if (dependent_opt) {
      nv_qmd_desc_init_smoke_device(dependent_opt, program_gpu_addr,
                                    register_count, sass_version,
                                    0, 0, 0, info);
   }
}

/**
 * tick107: program LMEM methods + SPA/CWD init in one vertical helper.
 * class_compute 0 keeps current subchannel object; spa from info sm_version heuristic.
 */
static inline void
nv_compute_emit_lmem_and_init_from_info(struct nv_push *p,
                                        uint32_t class_compute,
                                        uint64_t lmem_gpu_addr,
                                        uint32_t local_mem_low_bytes,
                                        const struct nv_device_info *info,
                                        uint32_t cwd_slot_count)
{
   uint8_t spa = 0x50;
   if (!p)
      return;
   if (info && info->sm_version) {
      uint32_t sm = info->sm_version;
      if (sm >= 0x50 && sm <= 0xff)
         spa = (uint8_t)sm;
      else if ((sm >> 8) && (sm >> 8) <= 0xff)
         spa = (uint8_t)((sm >> 8) & 0xffu);
   }
   nv_compute_emit_init_state(p, class_compute, spa, cwd_slot_count);
   if (lmem_gpu_addr)
      nv_compute_set_shader_local_memory_from_info(p, lmem_gpu_addr,
                                                   local_mem_low_bytes, info);
}

/**
 * Full compute launch with optional method-level invalidates + sema on QMD.
 * When sema_gpu_addr is set, RELEASE_ENABLE0 is applied via encode_full.
 * Call nv_compute_emit_init_state() once per channel before first dispatch.
 */
static inline void
nv_compute_emit_dispatch_with_sema(struct nv_push *p,
                                   const struct nv_qmd_desc *desc_in,
                                   uint64_t qmd_gpu_addr,
                                   void *qmd_host,
                                   uint32_t class_compute,
                                   uint64_t sema_gpu_addr,
                                   uint32_t sema_payload,
                                   bool method_invalidate)
{
   struct nv_qmd_desc desc;
   uint32_t qmd[NV_QMD_DWORDS];

   if (!p || !desc_in)
      return;

   desc = *desc_in;
   if (sema_gpu_addr && sema_payload)
      nv_qmd_desc_set_sema_release0(&desc, sema_gpu_addr, sema_payload);

   if (class_compute)
      nv_compute_set_object(p, class_compute);
   else
      nv_push_set_subch(p, NV_PUSH_SUBCH_COMPUTE);

   if (method_invalidate || desc.invalidate_caches)
      nv_compute_emit_invalidate_caches(p);

   nv_qmd_materialize(&desc, qmd, qmd_host);
   nv_compute_emit_inline_qmd_launch(p, qmd_gpu_addr, qmd, true);
}

/**
 * Smoke-path dispatch: init SPA/CWD (optional), invalidate, materialize QMD
 * with sema, SEND_PCAS.  Returns sema_payload (pass-through) for CPU wait.
 * Does not submit GPFIFO — caller uses nv_channel_submit_wait_sema().
 */
/**
 * tick104: smoke dispatch with optional device GR limits (warps / stack scale).
 */
static inline uint32_t
nv_compute_push_smoke_dispatch_gr(struct nv_push *p, uint32_t class_compute,
                                  uint8_t spa_version,
                                  uint64_t program_gpu_addr,
                                  uint32_t register_count,
                                  uint64_t qmd_gpu_addr, void *qmd_host,
                                  uint64_t sema_gpu_addr, uint32_t sema_payload,
                                  bool emit_init,
                                  const struct nv_device_info *info)
{
   struct nv_qmd_desc desc;

   if (!p)
      return 0;

   /* tick108: SPA/CWD from probe when available; else spa_version arg */
   if (emit_init && info)
      nv_compute_emit_lmem_and_init_from_info(p, class_compute, 0, 0, info, 0);
   else if (emit_init)
      nv_compute_emit_init_state(p, class_compute, spa_version, 0);

   if (info) {
      nv_qmd_desc_init_smoke_device(&desc, program_gpu_addr, register_count,
                                    spa_version, sema_gpu_addr, sema_payload,
                                    0, info);
   } else {
      nv_qmd_desc_init_smoke(&desc, program_gpu_addr, register_count,
                             spa_version, sema_gpu_addr, sema_payload);
   }
   nv_compute_emit_dispatch_with_sema(p, &desc, qmd_gpu_addr, qmd_host,
                                      class_compute, sema_gpu_addr,
                                      sema_payload, true);
   return sema_payload;
}

static inline uint32_t
nv_compute_push_smoke_dispatch(struct nv_push *p, uint32_t class_compute,
                               uint8_t spa_version,
                               uint64_t program_gpu_addr,
                               uint32_t register_count,
                               uint64_t qmd_gpu_addr, void *qmd_host,
                               uint64_t sema_gpu_addr, uint32_t sema_payload,
                               bool emit_init)
{
   return nv_compute_push_smoke_dispatch_gr(p, class_compute, spa_version,
                                            program_gpu_addr, register_count,
                                            qmd_gpu_addr, qmd_host,
                                            sema_gpu_addr, sema_payload,
                                            emit_init, NULL);
}

/**
 * Verify sema fields are present in an encoded QMD (trace/golden self-check).
 * Returns true if RELEASE_ENABLE0 is set and address/payload match.
 */
static inline bool
nv_qmd_verify_sema_release0(const uint32_t qmd[NV_QMD_DWORDS],
                            uint64_t expect_addr, uint32_t expect_payload)
{
   uint32_t lo, hi, payload, en;
   uint64_t addr;
   if (!qmd || !expect_addr || !expect_payload)
      return false;
   en = nv_qmd_get(qmd, NV_QMD_F_SEMAPHORE_RELEASE_ENABLE0);
   if (!en)
      return false;
   lo = nv_qmd_get(qmd, NV_QMD_F_RELEASE0_ADDRESS_LOWER);
   hi = nv_qmd_get(qmd, NV_QMD_F_RELEASE0_ADDRESS_UPPER);
   payload = nv_qmd_get(qmd, NV_QMD_F_RELEASE0_PAYLOAD);
   addr = ((uint64_t)(hi & 0xff) << 32) | (uint64_t)lo;
   if (addr != expect_addr)
      return false;
   if (payload != expect_payload)
      return false;
   return true;
}

/**
 * Encode QMD with sema and return 0 if sema sideband applied (enable0 set).
 * Lightweight sanity for unit/smoke without hardware.
 */
static inline int
nv_qmd_smoke_encode_check(uint64_t sema_gpu_addr, uint32_t sema_payload,
                          uint32_t qmd_out[NV_QMD_DWORDS])
{
   struct nv_qmd_desc d;
   uint32_t qmd_local[NV_QMD_DWORDS];
   uint32_t *q = qmd_out ? qmd_out : qmd_local;

   if (!sema_gpu_addr || !sema_payload)
      return -1;
   nv_qmd_desc_init_smoke(&d, 0x10000ull, 16, 0x86, sema_gpu_addr, sema_payload);
   nv_qmd_encode_full(&d, q);
   /* SEMAPHORE_RELEASE_ENABLE0 is MW bit 138 => dword 4, bit 10 */
   if (!(q[4] & (1u << 10)))
      return -2;
   return 0;
}

/**
 * tick108: encode check via smoke_device (GR/LMEM/CRS when info set).
 * Returns 0 ok; -1..-2 same as smoke_encode_check; -3 sema verify fail; -4 CRS policy.
 */
static inline int
nv_qmd_smoke_encode_check_device(uint64_t sema_gpu_addr, uint32_t sema_payload,
                                 const struct nv_device_info *info,
                                 uint32_t qmd_out[NV_QMD_DWORDS])
{
   struct nv_qmd_desc d;
   uint32_t qmd_local[NV_QMD_DWORDS];
   uint32_t *q = qmd_out ? qmd_out : qmd_local;

   if (!sema_gpu_addr || !sema_payload)
      return -1;
   if (!info)
      return nv_qmd_smoke_encode_check(sema_gpu_addr, sema_payload, qmd_out);

   nv_qmd_desc_init_smoke_device(&d, 0x10000ull, 16, 0, sema_gpu_addr,
                                 sema_payload, 0, info);
   nv_qmd_encode_full(&d, q);
   if (!(q[4] & (1u << 10)))
      return -2;
   if (!nv_qmd_verify_sema_release0(q, sema_gpu_addr, sema_payload))
      return -3;
   if (info->thread_stack_scaling && !d.local_mem_crs)
      return -4;
   return 0;
}

/**
 * Trace-golden: encode two QMDs with fixed inputs and require identical bytes
 * (determinism check).  Optionally compare sema enable + program addr fields.
 * program_addr/sema_addr/sema_payload are fixed for golden reproducibility.
 * Returns 0 if both encodes match and sema bit set; negative on mismatch.
 */
static inline int
nv_qmd_trace_golden_selftest(uint32_t qmd_a[NV_QMD_DWORDS],
                             uint32_t qmd_b[NV_QMD_DWORDS])
{
   struct nv_qmd_desc d;
   const uint64_t prog = 0x0000000000100000ull; /* 1 MiB aligned example */
   const uint64_t sema = 0x0000000000200000ull;
   const uint32_t payload = 0x42u;
   uint32_t local_a[NV_QMD_DWORDS], local_b[NV_QMD_DWORDS];
   uint32_t *a = qmd_a ? qmd_a : local_a;
   uint32_t *b = qmd_b ? qmd_b : local_b;

   nv_qmd_desc_init_smoke(&d, prog, 16, 0x86, sema, payload);
   nv_qmd_encode_full(&d, a);
   nv_qmd_encode_full(&d, b);
   if (memcmp(a, b, NV_QMD_BYTES) != 0)
      return -1;
   if (!(a[4] & (1u << 10)))
      return -2;
   /* Grid 1x1x1 should set CTA raster width=1 in encoded QMD (non-zero body) */
   {
      unsigned i, nonzero = 0;
      for (i = 0; i < NV_QMD_DWORDS; i++) {
         if (a[i])
            nonzero++;
      }
      if (nonzero < 4)
         return -3;
   }
   return 0;
}

/**
 * Build full G2 smoke QMD: program at program_gpu_addr, sema completion,
 * optional CB0 at cb0_addr for constants.  Writes qmd_out and returns 0.
 */
static inline int
nv_qmd_build_compute_smoke(uint32_t qmd_out[NV_QMD_DWORDS],
                           uint64_t program_gpu_addr,
                           uint32_t register_count, uint8_t sass_version,
                           uint64_t sema_gpu_addr, uint32_t sema_payload,
                           uint64_t cb0_addr, uint32_t cb0_size)
{
   struct nv_qmd_desc d;
   if (!qmd_out || !program_gpu_addr)
      return -1;
   nv_qmd_desc_init_smoke(&d, program_gpu_addr, register_count, sass_version,
                          sema_gpu_addr, sema_payload);
   if (cb0_addr && cb0_size) {
      d.cb_addr[0] = cb0_addr;
      d.cb_size[0] = cb0_size;
      d.cb_valid_mask |= 0x1;
   }
   nv_qmd_encode_full(&d, qmd_out);
   if (sema_gpu_addr && sema_payload && !(qmd_out[4] & (1u << 10)))
      return -2;
   return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* NV_QMD_H */
