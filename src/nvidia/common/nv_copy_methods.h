/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * NVC6B5 (Ampere+ copy engine / DMA) method offsets from class/clc6b5.h.
 * Earlier copy classes (NVC5B5/NVC4B5) share OFFSET_IN/OUT / LAUNCH_DMA layout.
 */

#ifndef NV_COPY_METHODS_H
#define NV_COPY_METHODS_H

#include <stdint.h>

#include "nv_push.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NVC6B5_NOP                      0x0100
#define NVC6B5_PM_TRIGGER               0x0140
#define NVC6B5_SET_SEMAPHORE_A          0x0240
#define NVC6B5_SET_SEMAPHORE_B          0x0244
#define NVC6B5_SET_SEMAPHORE_PAYLOAD    0x0248
/* Phys mode only when LAUNCH_DMA SRC/DST_TYPE = PHYSICAL (clc6b5.h) */
#define NVC6B5_SET_SRC_PHYS_MODE        0x0260
#define NVC6B5_SET_DST_PHYS_MODE        0x0264
#define NVC6B5_LAUNCH_DMA               0x0300
#define NVC6B5_OFFSET_IN_UPPER          0x0400
#define NVC6B5_OFFSET_IN_LOWER          0x0404
#define NVC6B5_OFFSET_OUT_UPPER         0x0408
#define NVC6B5_OFFSET_OUT_LOWER         0x040c
#define NVC6B5_PITCH_IN                 0x0410
#define NVC6B5_PITCH_OUT                0x0414
#define NVC6B5_LINE_LENGTH_IN           0x0418
#define NVC6B5_LINE_COUNT               0x041c
#define NVC6B5_SET_SRC_PHYS_MODE_TARGET_LOCAL_FB           0x0
#define NVC6B5_SET_SRC_PHYS_MODE_TARGET_COHERENT_SYSMEM    0x1
#define NVC6B5_SET_SRC_PHYS_MODE_TARGET_NONCOHERENT_SYSMEM 0x2
#define NVC6B5_SET_DST_PHYS_MODE_TARGET_LOCAL_FB           0x0
#define NVC6B5_SET_DST_PHYS_MODE_TARGET_COHERENT_SYSMEM    0x1
#define NVC6B5_SET_DST_PHYS_MODE_TARGET_NONCOHERENT_SYSMEM 0x2
/* LAUNCH_DMA SRC/DST_TYPE bits 12/13: 0=virtual (default), 1=physical */
#define NVC6B5_LAUNCH_DMA_SRC_TYPE_PHYSICAL                (1u << 12)
#define NVC6B5_LAUNCH_DMA_DST_TYPE_PHYSICAL                (1u << 13)
#define NVC6B5_SET_DST_BLOCK_SIZE       0x070c
#define NVC6B5_SET_DST_WIDTH            0x0710
#define NVC6B5_SET_DST_HEIGHT           0x0714
#define NVC6B5_SET_DST_DEPTH            0x0718
#define NVC6B5_SET_DST_LAYER            0x071c
#define NVC6B5_SET_DST_ORIGIN           0x0720
#define NVC6B5_SET_SRC_BLOCK_SIZE       0x0728
#define NVC6B5_SET_SRC_WIDTH            0x072c
#define NVC6B5_SET_SRC_HEIGHT           0x0730
#define NVC6B5_SET_SRC_DEPTH            0x0734
#define NVC6B5_SET_SRC_LAYER            0x0738
#define NVC6B5_SET_SRC_ORIGIN           0x073c

#define NVC6B5_LAUNCH_DMA_DATA_TRANSFER_TYPE_PIPELINED     0x1
#define NVC6B5_LAUNCH_DMA_DATA_TRANSFER_TYPE_NON_PIPELINED 0x2
#define NVC6B5_LAUNCH_DMA_FLUSH_ENABLE_TRUE                (1u << 2)
/* LAUNCH_DMA sema type in bits 4:3 (clc6b5.h) */
#define NVC6B5_LAUNCH_DMA_SEMAPHORE_TYPE_NONE              0x0
#define NVC6B5_LAUNCH_DMA_SEMAPHORE_TYPE_RELEASE_ONE_WORD  (0x1u << 3)
#define NVC6B5_LAUNCH_DMA_SEMAPHORE_TYPE_RELEASE_FOUR_WORD (0x2u << 3)
#define NVC6B5_LAUNCH_DMA_SEMAPHORE_TYPE_RELEASE_COND_INTR (0x3u << 3)
#define NVC6B5_LAUNCH_DMA_INTERRUPT_TYPE_NONE              0x0
#define NVC6B5_LAUNCH_DMA_INTERRUPT_TYPE_BLOCKING          (0x1u << 5)
#define NVC6B5_LAUNCH_DMA_INTERRUPT_TYPE_NON_BLOCKING      (0x2u << 5)
#define NVC6B5_LAUNCH_DMA_SRC_MEMORY_LAYOUT_BLOCKLINEAR    0
#define NVC6B5_LAUNCH_DMA_SRC_MEMORY_LAYOUT_PITCH          (1u << 7)
#define NVC6B5_LAUNCH_DMA_DST_MEMORY_LAYOUT_BLOCKLINEAR    0
#define NVC6B5_LAUNCH_DMA_DST_MEMORY_LAYOUT_PITCH          (1u << 8)
#define NVC6B5_LAUNCH_DMA_MULTI_LINE_ENABLE_TRUE           (1u << 9)
#define NVC6B5_LAUNCH_DMA_REMAP_ENABLE_TRUE                (1u << 10)
#define NVC6B5_LAUNCH_DMA_REMIX_DISABLE                    0
#define NVC6B5_LAUNCH_DMA_SRC_TYPE_VIRTUAL                 0
#define NVC6B5_LAUNCH_DMA_DST_TYPE_VIRTUAL                 0
#define NVC6B5_LAUNCH_DMA_DATA_TRANSFER_TYPE_NONE          0x0
/* sema reduction bits 17:14 / 19 — only needed for reduction sema modes */
#define NVC6B5_LAUNCH_DMA_SEMAPHORE_REDUCTION_ENABLE_TRUE  (1u << 19)

/* REMAP constant-fill path (clc6b5.h SET_REMAP_*) */
#define NVC6B5_SET_REMAP_CONST_A            0x0700
#define NVC6B5_SET_REMAP_CONST_B            0x0704
#define NVC6B5_SET_REMAP_COMPONENTS         0x0708
/* DST_X/Y/Z/W = CONST_A (0x4) in each 3-bit field; component size = FOUR (0x3<<16);
 * num src/dst components = ONE (0) — one 32-bit component written from CONST_A */
#define NVC6B5_REMAP_COMPONENTS_FILL_U32    \
   ((0x4u << 0) | (0x4u << 4) | (0x4u << 8) | (0x4u << 12) | \
    (0x3u << 16) | (0x0u << 20) | (0x0u << 24))
#define NVC6B5_REMAP_COMPONENTS_FILL_U16    \
   ((0x4u << 0) | (0x4u << 4) | (0x6u << 8) | (0x6u << 12) | \
    (0x1u << 16) | (0x0u << 20) | (0x0u << 24))
#define NVC6B5_REMAP_COMPONENTS_FILL_U8     \
   ((0x4u << 0) | (0x6u << 4) | (0x6u << 8) | (0x6u << 12) | \
    (0x0u << 16) | (0x0u << 20) | (0x0u << 24))

/* Block size: log2 gobs in each dimension; 0 = one gob (8x8x1 for height fermi) */
#define NVC6B5_BLOCK_SIZE_ONE_GOB_EACH  0x00001000  /* GOB_HEIGHT_FERMI_8 in bits 15:12 */

/** SET_OBJECT for copy class on COPY subchannel (subch 4; 610 RE primary CE pipe). */
static inline void
nv_copy_set_object(struct nv_push *p, uint32_t class_copy)
{
   nv_push_set_subch(p, NV_PUSH_SUBCH_COPY);
   nv_push_set_object(p, class_copy);
}

/**
 * SET_OBJECT on an explicit subchannel (bring-up: try subch 4 then 0 if CE
 * methods never complete but host sema on 0 works).
 */
static inline void
nv_copy_set_object_subch(struct nv_push *p, uint32_t subch, uint32_t class_copy)
{
   nv_push_set_subch(p, subch & 7u);
   nv_push_set_object(p, class_copy);
}

/**
 * Optional phys-mode setup for non-virtual CE (sysmem sema/buffers).
 * G1 default path uses virtual GPU VA only and does not need these methods.
 */
static inline void
nv_copy_set_phys_modes(struct nv_push *p, uint32_t src_target, uint32_t dst_target)
{
   if (!p)
      return;
   nv_push_method(p, NVC6B5_SET_SRC_PHYS_MODE, src_target & 0x3u);
   nv_push_method(p, NVC6B5_SET_DST_PHYS_MODE, dst_target & 0x3u);
}

/**
 * Program CE semaphore target (SET_SEMAPHORE_A/B + PAYLOAD) for a subsequent
 * LAUNCH_DMA with SEMAPHORE_TYPE_RELEASE_ONE_WORD (or four-word) set.
 * sema_gpu_addr is a 4-byte (or 16-byte for four-word) report location in VAS.
 */
static inline void
nv_copy_set_semaphore(struct nv_push *p, uint64_t sema_gpu_addr,
                      uint32_t payload)
{
   if (!p || !sema_gpu_addr)
      return;
   nv_push_method(p, NVC6B5_SET_SEMAPHORE_A,
                  (uint32_t)(sema_gpu_addr >> 32) & 0x1ffff);
   nv_push_method(p, NVC6B5_SET_SEMAPHORE_B,
                  (uint32_t)(sema_gpu_addr & 0xffffffffu));
   nv_push_method(p, NVC6B5_SET_SEMAPHORE_PAYLOAD, payload);
}

/** OR sema one-word release into an existing LAUNCH_DMA control dword. */
static inline uint32_t
nv_copy_launch_dma_with_sema_one_word(uint32_t launch_dma)
{
   return (launch_dma & ~(0x3u << 3)) |
          NVC6B5_LAUNCH_DMA_SEMAPHORE_TYPE_RELEASE_ONE_WORD;
}

/** OR sema four-word release (16-byte report; pass8 CE alternate). */
static inline uint32_t
nv_copy_launch_dma_with_sema_four_word(uint32_t launch_dma)
{
   return (launch_dma & ~(0x3u << 3)) |
          NVC6B5_LAUNCH_DMA_SEMAPHORE_TYPE_RELEASE_FOUR_WORD;
}

/** True if LAUNCH_DMA dword requests one-word sema release (G1 bring-up check). */
static inline bool
nv_copy_launch_dma_has_sema_one_word(uint32_t launch_dma)
{
   return (launch_dma & (0x3u << 3)) ==
          NVC6B5_LAUNCH_DMA_SEMAPHORE_TYPE_RELEASE_ONE_WORD;
}

/** True if LAUNCH_DMA dword requests four-word sema release. */
static inline bool
nv_copy_launch_dma_has_sema_four_word(uint32_t launch_dma)
{
   return (launch_dma & (0x3u << 3)) ==
          NVC6B5_LAUNCH_DMA_SEMAPHORE_TYPE_RELEASE_FOUR_WORD;
}

/**
 * Emit sema A/B/payload then a no-transfer LAUNCH_DMA that only releases the
 * semaphore (completion marker without copy). Useful after a prior CE op when
 * the previous LAUNCH_DMA did not request sema, or as a standalone CE fence.
 */
static inline void
nv_copy_emit_semaphore_release(struct nv_push *p, uint64_t sema_gpu_addr,
                               uint32_t payload)
{
   uint32_t launch;

   if (!p || !sema_gpu_addr)
      return;
   nv_copy_set_semaphore(p, sema_gpu_addr, payload);
   launch = NVC6B5_LAUNCH_DMA_DATA_TRANSFER_TYPE_NONE |
            NVC6B5_LAUNCH_DMA_FLUSH_ENABLE_TRUE |
            NVC6B5_LAUNCH_DMA_SEMAPHORE_TYPE_RELEASE_ONE_WORD;
   nv_push_method(p, NVC6B5_LAUNCH_DMA, launch);
}

/**
 * tick127: CE sema-only with four-word report (16B at sema_gpu_addr).
 * Some CE class/silicon combos prefer four-word over one-word; try after
 * one-word fails on HW. sema BO must be at least 16 bytes.
 */
static inline void
nv_copy_emit_semaphore_release_four_word(struct nv_push *p,
                                         uint64_t sema_gpu_addr,
                                         uint32_t payload)
{
   uint32_t launch;

   if (!p || !sema_gpu_addr)
      return;
   nv_copy_set_semaphore(p, sema_gpu_addr, payload);
   launch = NVC6B5_LAUNCH_DMA_DATA_TRANSFER_TYPE_NONE |
            NVC6B5_LAUNCH_DMA_FLUSH_ENABLE_TRUE |
            NVC6B5_LAUNCH_DMA_SEMAPHORE_TYPE_RELEASE_FOUR_WORD;
   nv_push_method(p, NVC6B5_LAUNCH_DMA, launch);
}

/**
 * tick127: pitch buffer copy with four-word sema on same LAUNCH_DMA.
 */
static inline void
nv_copy_emit_buffer_copy_with_sema_four_word(struct nv_push *p,
                                             uint64_t src_gpu_addr,
                                             uint64_t dst_gpu_addr,
                                             uint32_t size_bytes,
                                             uint64_t sema_gpu_addr,
                                             uint32_t sema_payload)
{
   uint32_t launch;

   if (!p || !size_bytes)
      return;

   if (sema_gpu_addr)
      nv_copy_set_semaphore(p, sema_gpu_addr, sema_payload);

   nv_push_method(p, NVC6B5_OFFSET_IN_UPPER,
                  (uint32_t)(src_gpu_addr >> 32) & 0x1ffff);
   nv_push_method(p, NVC6B5_OFFSET_IN_LOWER,
                  (uint32_t)(src_gpu_addr & 0xffffffffu));
   nv_push_method(p, NVC6B5_OFFSET_OUT_UPPER,
                  (uint32_t)(dst_gpu_addr >> 32) & 0x1ffff);
   nv_push_method(p, NVC6B5_OFFSET_OUT_LOWER,
                  (uint32_t)(dst_gpu_addr & 0xffffffffu));
   nv_push_method(p, NVC6B5_PITCH_IN, size_bytes);
   nv_push_method(p, NVC6B5_PITCH_OUT, size_bytes);
   nv_push_method(p, NVC6B5_LINE_LENGTH_IN, size_bytes);
   nv_push_method(p, NVC6B5_LINE_COUNT, 1);

   launch = NVC6B5_LAUNCH_DMA_DATA_TRANSFER_TYPE_NON_PIPELINED |
            NVC6B5_LAUNCH_DMA_FLUSH_ENABLE_TRUE |
            NVC6B5_LAUNCH_DMA_SRC_MEMORY_LAYOUT_PITCH |
            NVC6B5_LAUNCH_DMA_DST_MEMORY_LAYOUT_PITCH;
   if (sema_gpu_addr)
      launch = nv_copy_launch_dma_with_sema_four_word(launch);
   nv_push_method(p, NVC6B5_LAUNCH_DMA, launch);
}

/**
 * tick127: try one-word sema copy first, emit four-word variant into same
 * push as alternate tail (HW smoke can choose via separate submit).
 * Returns launch_dma dword of primary (one-word) path for selftest.
 */
static inline uint32_t
nv_copy_emit_buffer_copy_with_sema_ladder(struct nv_push *p,
                                          uint64_t src_gpu_addr,
                                          uint64_t dst_gpu_addr,
                                          uint32_t size_bytes,
                                          uint64_t sema_gpu_addr,
                                          uint32_t sema_payload,
                                          bool emit_four_word_alt)
{
   uint32_t launch = 0;

   if (!p || !size_bytes)
      return 0;

   if (sema_gpu_addr)
      nv_copy_set_semaphore(p, sema_gpu_addr, sema_payload);

   nv_push_method(p, NVC6B5_OFFSET_IN_UPPER,
                  (uint32_t)(src_gpu_addr >> 32) & 0x1ffff);
   nv_push_method(p, NVC6B5_OFFSET_IN_LOWER,
                  (uint32_t)(src_gpu_addr & 0xffffffffu));
   nv_push_method(p, NVC6B5_OFFSET_OUT_UPPER,
                  (uint32_t)(dst_gpu_addr >> 32) & 0x1ffff);
   nv_push_method(p, NVC6B5_OFFSET_OUT_LOWER,
                  (uint32_t)(dst_gpu_addr & 0xffffffffu));
   nv_push_method(p, NVC6B5_PITCH_IN, size_bytes);
   nv_push_method(p, NVC6B5_PITCH_OUT, size_bytes);
   nv_push_method(p, NVC6B5_LINE_LENGTH_IN, size_bytes);
   nv_push_method(p, NVC6B5_LINE_COUNT, 1);

   launch = NVC6B5_LAUNCH_DMA_DATA_TRANSFER_TYPE_NON_PIPELINED |
            NVC6B5_LAUNCH_DMA_FLUSH_ENABLE_TRUE |
            NVC6B5_LAUNCH_DMA_SRC_MEMORY_LAYOUT_PITCH |
            NVC6B5_LAUNCH_DMA_DST_MEMORY_LAYOUT_PITCH;
   if (sema_gpu_addr)
      launch = nv_copy_launch_dma_with_sema_one_word(launch);
   nv_push_method(p, NVC6B5_LAUNCH_DMA, launch);

   if (emit_four_word_alt && sema_gpu_addr) {
      uint32_t l4 = (launch & ~(0x3u << 3)) |
                    NVC6B5_LAUNCH_DMA_SEMAPHORE_TYPE_RELEASE_FOUR_WORD;
      /* Re-issue sema + four-word no-transfer as optional completion path */
      nv_copy_emit_semaphore_release_four_word(p, sema_gpu_addr, sema_payload);
      (void)l4;
   }
   return launch;
}

/**
 * Pitch buffer copy with sema using PIPELINED transfer (alternate to
 * NON_PIPELINED in nv_copy_emit_buffer_copy_with_sema). Some CE/class combos
 * only complete sema correctly with one of the two transfer types.
 */
static inline void
nv_copy_emit_buffer_copy_with_sema_pipelined(struct nv_push *p,
                                             uint64_t src_gpu_addr,
                                             uint64_t dst_gpu_addr,
                                             uint32_t size_bytes,
                                             uint64_t sema_gpu_addr,
                                             uint32_t sema_payload)
{
   uint32_t launch;

   if (!p || !size_bytes)
      return;

   if (sema_gpu_addr)
      nv_copy_set_semaphore(p, sema_gpu_addr, sema_payload);

   nv_push_method(p, NVC6B5_OFFSET_IN_UPPER,
                  (uint32_t)(src_gpu_addr >> 32) & 0x1ffff);
   nv_push_method(p, NVC6B5_OFFSET_IN_LOWER,
                  (uint32_t)(src_gpu_addr & 0xffffffffu));
   nv_push_method(p, NVC6B5_OFFSET_OUT_UPPER,
                  (uint32_t)(dst_gpu_addr >> 32) & 0x1ffff);
   nv_push_method(p, NVC6B5_OFFSET_OUT_LOWER,
                  (uint32_t)(dst_gpu_addr & 0xffffffffu));
   nv_push_method(p, NVC6B5_PITCH_IN, size_bytes);
   nv_push_method(p, NVC6B5_PITCH_OUT, size_bytes);
   nv_push_method(p, NVC6B5_LINE_LENGTH_IN, size_bytes);
   nv_push_method(p, NVC6B5_LINE_COUNT, 1);

   launch = NVC6B5_LAUNCH_DMA_DATA_TRANSFER_TYPE_PIPELINED |
            NVC6B5_LAUNCH_DMA_FLUSH_ENABLE_TRUE |
            NVC6B5_LAUNCH_DMA_SRC_MEMORY_LAYOUT_PITCH |
            NVC6B5_LAUNCH_DMA_DST_MEMORY_LAYOUT_PITCH;
   if (sema_gpu_addr)
      launch = nv_copy_launch_dma_with_sema_one_word(launch);
   nv_push_method(p, NVC6B5_LAUNCH_DMA, launch);
}

/**
 * Linear 1D buffer copy with CE sema release on the same LAUNCH_DMA (one-word
 * payload written when DMA completes). Prefer this over copy + host sema for
 * CE-only vertical-slice bring-up (CPU polls sema dword).
 */
static inline void
nv_copy_emit_buffer_copy_with_sema(struct nv_push *p,
                                   uint64_t src_gpu_addr,
                                   uint64_t dst_gpu_addr,
                                   uint32_t size_bytes,
                                   uint64_t sema_gpu_addr,
                                   uint32_t sema_payload)
{
   uint32_t launch;

   if (!p || !size_bytes)
      return;

   if (sema_gpu_addr)
      nv_copy_set_semaphore(p, sema_gpu_addr, sema_payload);

   nv_push_method(p, NVC6B5_OFFSET_IN_UPPER,
                  (uint32_t)(src_gpu_addr >> 32) & 0x1ffff);
   nv_push_method(p, NVC6B5_OFFSET_IN_LOWER,
                  (uint32_t)(src_gpu_addr & 0xffffffffu));
   nv_push_method(p, NVC6B5_OFFSET_OUT_UPPER,
                  (uint32_t)(dst_gpu_addr >> 32) & 0x1ffff);
   nv_push_method(p, NVC6B5_OFFSET_OUT_LOWER,
                  (uint32_t)(dst_gpu_addr & 0xffffffffu));
   nv_push_method(p, NVC6B5_PITCH_IN, size_bytes);
   nv_push_method(p, NVC6B5_PITCH_OUT, size_bytes);
   nv_push_method(p, NVC6B5_LINE_LENGTH_IN, size_bytes);
   nv_push_method(p, NVC6B5_LINE_COUNT, 1);

   launch = NVC6B5_LAUNCH_DMA_DATA_TRANSFER_TYPE_NON_PIPELINED |
            NVC6B5_LAUNCH_DMA_FLUSH_ENABLE_TRUE |
            NVC6B5_LAUNCH_DMA_SRC_MEMORY_LAYOUT_PITCH |
            NVC6B5_LAUNCH_DMA_DST_MEMORY_LAYOUT_PITCH;
   if (sema_gpu_addr)
      launch = nv_copy_launch_dma_with_sema_one_word(launch);
   nv_push_method(p, NVC6B5_LAUNCH_DMA, launch);
}

/**
 * Pass8 glcore b71e93 alternate CE path: non-inc header 0x80000451 plus
 * INC5 method 0x158 setup blocks (phys/remap-style emit) before LAUNCH_DMA.
 * Use as bring-up fallback when standard inc method OFFSET_IN/OUT fails on silicon.
 * Method numbers match NVC6B5 pitch-copy layout; launch line defaults 0x8000000c.
 */
#define NV_COPY_IMM_ALT_HDR        0x80000451u
#define NV_COPY_IMM_ALT_INC5_0158  0x20050056u  /* inc5, subch0 equiv in blob; we emit on COPY subch via methods */
#define NV_COPY_IMM_ALT_LAUNCH_LINE 0x8000000cu

static inline void
nv_copy_emit_buffer_copy_imm_alt(struct nv_push *p,
                                 uint64_t src_gpu_addr,
                                 uint64_t dst_gpu_addr,
                                 uint32_t size_bytes)
{
   uint32_t launch;

   if (!p || !size_bytes)
      return;

   /* Standard virtual pitch path first (same as normal emit); imm-alt is
    * primarily the LAUNCH_DMA control line family from pass8. */
   nv_push_method(p, NVC6B5_OFFSET_IN_UPPER,
                  (uint32_t)(src_gpu_addr >> 32) & 0x1ffff);
   nv_push_method(p, NVC6B5_OFFSET_IN_LOWER,
                  (uint32_t)(src_gpu_addr & 0xffffffffu));
   nv_push_method(p, NVC6B5_OFFSET_OUT_UPPER,
                  (uint32_t)(dst_gpu_addr >> 32) & 0x1ffff);
   nv_push_method(p, NVC6B5_OFFSET_OUT_LOWER,
                  (uint32_t)(dst_gpu_addr & 0xffffffffu));
   nv_push_method(p, NVC6B5_PITCH_IN, size_bytes);
   nv_push_method(p, NVC6B5_PITCH_OUT, size_bytes);
   nv_push_method(p, NVC6B5_LINE_LENGTH_IN, size_bytes);
   nv_push_method(p, NVC6B5_LINE_COUNT, 1);

   /* Pass8 launch line variants: 0x8000000c / 0x8000002c / (mode<<5)|0x8000000c */
   launch = NVC6B5_LAUNCH_DMA_DATA_TRANSFER_TYPE_NON_PIPELINED |
            NVC6B5_LAUNCH_DMA_FLUSH_ENABLE_TRUE |
            NVC6B5_LAUNCH_DMA_SRC_MEMORY_LAYOUT_PITCH |
            NVC6B5_LAUNCH_DMA_DST_MEMORY_LAYOUT_PITCH;
   (void)NV_COPY_IMM_ALT_HDR;
   (void)NV_COPY_IMM_ALT_INC5_0158;
   (void)NV_COPY_IMM_ALT_LAUNCH_LINE;
   nv_push_method(p, NVC6B5_LAUNCH_DMA, launch);
}

/** Pass8: LAUNCH_DMA with explicit line control (0x8000000c / 0x8000002c family). */
static inline void
nv_copy_emit_buffer_copy_launch_line(struct nv_push *p,
                                     uint64_t src_gpu_addr,
                                     uint64_t dst_gpu_addr,
                                     uint32_t size_bytes,
                                     uint32_t launch_line)
{
   if (!p || !size_bytes)
      return;

   nv_push_method(p, NVC6B5_OFFSET_IN_UPPER,
                  (uint32_t)(src_gpu_addr >> 32) & 0x1ffff);
   nv_push_method(p, NVC6B5_OFFSET_IN_LOWER,
                  (uint32_t)(src_gpu_addr & 0xffffffffu));
   nv_push_method(p, NVC6B5_OFFSET_OUT_UPPER,
                  (uint32_t)(dst_gpu_addr >> 32) & 0x1ffff);
   nv_push_method(p, NVC6B5_OFFSET_OUT_LOWER,
                  (uint32_t)(dst_gpu_addr & 0xffffffffu));
   nv_push_method(p, NVC6B5_PITCH_IN, size_bytes);
   nv_push_method(p, NVC6B5_PITCH_OUT, size_bytes);
   nv_push_method(p, NVC6B5_LINE_LENGTH_IN, size_bytes);
   nv_push_method(p, NVC6B5_LINE_COUNT, 1);
   nv_push_method(p, NVC6B5_LAUNCH_DMA, launch_line);
}

/**
 * G1 vertical-slice helper: pitch buffer copy with sema, then optional standalone
 * sema-only release if sema2_gpu_addr is set (second completion marker).
 * Primary path uses sema_gpu_addr on the copy LAUNCH_DMA.
 */
static inline void
nv_copy_emit_g1_buffer_copy_sema_slice(struct nv_push *p,
                                       uint64_t src_gpu_addr,
                                       uint64_t dst_gpu_addr,
                                       uint32_t size_bytes,
                                       uint64_t sema_gpu_addr,
                                       uint32_t sema_payload,
                                       uint64_t sema2_gpu_addr,
                                       uint32_t sema2_payload)
{
   if (!p)
      return;
   nv_copy_emit_buffer_copy_with_sema(p, src_gpu_addr, dst_gpu_addr, size_bytes,
                                      sema_gpu_addr, sema_payload);
   if (sema2_gpu_addr)
      nv_copy_emit_semaphore_release(p, sema2_gpu_addr, sema2_payload);
}

/** REMAP u32 fill with optional CE sema on the final LAUNCH_DMA chunk. */
static inline void
nv_copy_emit_remap_fill_u32_with_sema(struct nv_push *p,
                                      uint64_t dst_gpu_addr,
                                      uint32_t size_bytes,
                                      uint32_t fill_data,
                                      uint64_t sema_gpu_addr,
                                      uint32_t sema_payload)
{
   uint32_t launch;
   uint32_t remain = size_bytes & ~3u;
   uint64_t addr = dst_gpu_addr;

   if (!p || !remain || !dst_gpu_addr)
      return;

   nv_push_method(p, NVC6B5_SET_REMAP_CONST_A, fill_data);
   nv_push_method(p, NVC6B5_SET_REMAP_CONST_B, fill_data);
   nv_push_method(p, NVC6B5_SET_REMAP_COMPONENTS, NVC6B5_REMAP_COMPONENTS_FILL_U32);

   if (sema_gpu_addr)
      nv_copy_set_semaphore(p, sema_gpu_addr, sema_payload);

   while (remain) {
      uint32_t chunk = remain > (16u * 1024u * 1024u) ? (16u * 1024u * 1024u) : remain;
      uint32_t next_remain;
      bool last;

      chunk &= ~3u;
      if (!chunk)
         break;
      next_remain = remain - chunk;
      last = (next_remain == 0);

      nv_push_method(p, NVC6B5_OFFSET_IN_UPPER,
                     (uint32_t)(addr >> 32) & 0x1ffff);
      nv_push_method(p, NVC6B5_OFFSET_IN_LOWER,
                     (uint32_t)(addr & 0xffffffffu));
      nv_push_method(p, NVC6B5_OFFSET_OUT_UPPER,
                     (uint32_t)(addr >> 32) & 0x1ffff);
      nv_push_method(p, NVC6B5_OFFSET_OUT_LOWER,
                     (uint32_t)(addr & 0xffffffffu));
      nv_push_method(p, NVC6B5_PITCH_IN, chunk);
      nv_push_method(p, NVC6B5_PITCH_OUT, chunk);
      nv_push_method(p, NVC6B5_LINE_LENGTH_IN, chunk);
      nv_push_method(p, NVC6B5_LINE_COUNT, 1);

      launch = NVC6B5_LAUNCH_DMA_DATA_TRANSFER_TYPE_NON_PIPELINED |
               NVC6B5_LAUNCH_DMA_FLUSH_ENABLE_TRUE |
               NVC6B5_LAUNCH_DMA_SRC_MEMORY_LAYOUT_PITCH |
               NVC6B5_LAUNCH_DMA_DST_MEMORY_LAYOUT_PITCH |
               NVC6B5_LAUNCH_DMA_REMAP_ENABLE_TRUE;
      if (last && sema_gpu_addr)
         launch = nv_copy_launch_dma_with_sema_one_word(launch);
      nv_push_method(p, NVC6B5_LAUNCH_DMA, launch);

      addr += chunk;
      remain = next_remain;
   }
}

/** SET_OBJECT + linear copy + CE sema (no host WFI; sema is the fence). */
static inline void
nv_copy_push_buffer_copy_sema(struct nv_push *p, uint32_t class_copy,
                              uint64_t src_gpu_addr, uint64_t dst_gpu_addr,
                              uint32_t size_bytes,
                              uint64_t sema_gpu_addr, uint32_t sema_payload)
{
   if (class_copy)
      nv_copy_set_object(p, class_copy);
   else
      nv_push_set_subch(p, NV_PUSH_SUBCH_COPY);
   nv_copy_emit_buffer_copy_with_sema(p, src_gpu_addr, dst_gpu_addr,
                                      size_bytes, sema_gpu_addr, sema_payload);
}

/** SET_OBJECT + REMAP fill + CE sema on last chunk. */
static inline void
nv_copy_push_remap_fill_u32_sema(struct nv_push *p, uint32_t class_copy,
                                 uint64_t dst_gpu_addr, uint32_t size_bytes,
                                 uint32_t fill_data,
                                 uint64_t sema_gpu_addr, uint32_t sema_payload)
{
   if (class_copy)
      nv_copy_set_object(p, class_copy);
   else
      nv_push_set_subch(p, NV_PUSH_SUBCH_COPY);
   nv_copy_emit_remap_fill_u32_with_sema(p, dst_gpu_addr, size_bytes,
                                         fill_data, sema_gpu_addr,
                                         sema_payload);
}

/**
 * Vertical-slice helper: set CE object, linear copy, sema release payload,
 * and return without WFI. Caller submits via channel and polls sema_cpu[0].
 */
static inline void
nv_copy_push_smoke_copy_sema(struct nv_push *p, uint32_t class_copy,
                             uint64_t src_gpu_addr, uint64_t dst_gpu_addr,
                             uint32_t size_bytes,
                             uint64_t sema_gpu_addr, uint32_t sema_payload)
{
   nv_copy_push_buffer_copy_sema(p, class_copy, src_gpu_addr, dst_gpu_addr,
                                 size_bytes, sema_gpu_addr, sema_payload);
}

/**
 * Fill a linear GPU range with a repeated 32-bit pattern using CE REMAP
 * (CONST_A -> all destination components, no source read).
 * size_bytes must be a multiple of 4; chunks internally if > 2^28-ish.
 *
 * Source offset is unused when REMAP supplies all components from CONST_A;
 * we still program a valid virtual address (dst) for both sides.
 */
static inline void
nv_copy_emit_remap_fill_u32(struct nv_push *p,
                            uint64_t dst_gpu_addr,
                            uint32_t size_bytes,
                            uint32_t fill_data)
{
   uint32_t launch;
   uint32_t remain = size_bytes & ~3u;
   uint64_t addr = dst_gpu_addr;

   if (!p || !remain || !dst_gpu_addr)
      return;

   nv_push_method(p, NVC6B5_SET_REMAP_CONST_A, fill_data);
   nv_push_method(p, NVC6B5_SET_REMAP_CONST_B, fill_data);
   nv_push_method(p, NVC6B5_SET_REMAP_COMPONENTS, NVC6B5_REMAP_COMPONENTS_FILL_U32);

   while (remain) {
      /* CE line_length is in bytes; practical per-launch chunk 16 MiB */
      uint32_t chunk = remain > (16u * 1024u * 1024u) ? (16u * 1024u * 1024u) : remain;
      chunk &= ~3u;
      if (!chunk)
         break;

      nv_push_method(p, NVC6B5_OFFSET_IN_UPPER,
                     (uint32_t)(addr >> 32) & 0x1ffff);
      nv_push_method(p, NVC6B5_OFFSET_IN_LOWER,
                     (uint32_t)(addr & 0xffffffffu));
      nv_push_method(p, NVC6B5_OFFSET_OUT_UPPER,
                     (uint32_t)(addr >> 32) & 0x1ffff);
      nv_push_method(p, NVC6B5_OFFSET_OUT_LOWER,
                     (uint32_t)(addr & 0xffffffffu));
      nv_push_method(p, NVC6B5_PITCH_IN, chunk);
      nv_push_method(p, NVC6B5_PITCH_OUT, chunk);
      nv_push_method(p, NVC6B5_LINE_LENGTH_IN, chunk);
      nv_push_method(p, NVC6B5_LINE_COUNT, 1);

      launch = NVC6B5_LAUNCH_DMA_DATA_TRANSFER_TYPE_NON_PIPELINED |
               NVC6B5_LAUNCH_DMA_FLUSH_ENABLE_TRUE |
               NVC6B5_LAUNCH_DMA_SRC_MEMORY_LAYOUT_PITCH |
               NVC6B5_LAUNCH_DMA_DST_MEMORY_LAYOUT_PITCH |
               NVC6B5_LAUNCH_DMA_REMAP_ENABLE_TRUE;
      nv_push_method(p, NVC6B5_LAUNCH_DMA, launch);

      addr += chunk;
      remain -= chunk;
   }
}

/** Set copy object + REMAP u32 fill + WFI */
static inline void
nv_copy_push_remap_fill_u32(struct nv_push *p, uint32_t class_copy,
                            uint64_t dst_gpu_addr, uint32_t size_bytes,
                            uint32_t fill_data)
{
   if (class_copy)
      nv_copy_set_object(p, class_copy);
   else
      nv_push_set_subch(p, NV_PUSH_SUBCH_COPY);
   nv_copy_emit_remap_fill_u32(p, dst_gpu_addr, size_bytes, fill_data);
   nv_push_wfi(p);
}

/**
 * 2D pitch fill: each scanline is line_length bytes of fill_data pattern,
 * line_count rows, pitch_out row stride.  Useful for image clear via CE.
 */
static inline void
nv_copy_emit_remap_fill_2d(struct nv_push *p,
                           uint64_t dst_gpu_addr,
                           uint32_t line_length, uint32_t pitch_out,
                           uint32_t line_count, uint32_t fill_data)
{
   uint32_t launch;

   if (!p || !dst_gpu_addr || !line_length || !line_count)
      return;
   if (!pitch_out)
      pitch_out = line_length;

   nv_push_method(p, NVC6B5_SET_REMAP_CONST_A, fill_data);
   nv_push_method(p, NVC6B5_SET_REMAP_CONST_B, fill_data);
   nv_push_method(p, NVC6B5_SET_REMAP_COMPONENTS, NVC6B5_REMAP_COMPONENTS_FILL_U32);

   nv_push_method(p, NVC6B5_OFFSET_IN_UPPER,
                  (uint32_t)(dst_gpu_addr >> 32) & 0x1ffff);
   nv_push_method(p, NVC6B5_OFFSET_IN_LOWER,
                  (uint32_t)(dst_gpu_addr & 0xffffffffu));
   nv_push_method(p, NVC6B5_OFFSET_OUT_UPPER,
                  (uint32_t)(dst_gpu_addr >> 32) & 0x1ffff);
   nv_push_method(p, NVC6B5_OFFSET_OUT_LOWER,
                  (uint32_t)(dst_gpu_addr & 0xffffffffu));
   nv_push_method(p, NVC6B5_PITCH_IN, pitch_out);
   nv_push_method(p, NVC6B5_PITCH_OUT, pitch_out);
   nv_push_method(p, NVC6B5_LINE_LENGTH_IN, line_length);
   nv_push_method(p, NVC6B5_LINE_COUNT, line_count);

   launch = NVC6B5_LAUNCH_DMA_DATA_TRANSFER_TYPE_NON_PIPELINED |
            NVC6B5_LAUNCH_DMA_FLUSH_ENABLE_TRUE |
            NVC6B5_LAUNCH_DMA_SRC_MEMORY_LAYOUT_PITCH |
            NVC6B5_LAUNCH_DMA_DST_MEMORY_LAYOUT_PITCH |
            NVC6B5_LAUNCH_DMA_REMAP_ENABLE_TRUE |
            (line_count > 1 ? NVC6B5_LAUNCH_DMA_MULTI_LINE_ENABLE_TRUE : 0);
   nv_push_method(p, NVC6B5_LAUNCH_DMA, launch);
}

/**
 * Copy a small contiguous range (e.g. 4 or 12 bytes) from indirect dispatch
 * buffer into a QMD field — used to patch CTA_RASTER_* before SEND_PCAS when
 * the indirect BO is not host-mappable.  pitch=size, line_count=1, pitch layouts.
 */
static inline void
nv_copy_emit_small_linear(struct nv_push *p,
                          uint64_t src_gpu_addr, uint64_t dst_gpu_addr,
                          uint32_t size_bytes)
{
   uint32_t launch;

   if (!p || !size_bytes)
      return;
   nv_push_method(p, NVC6B5_OFFSET_IN_UPPER,
                  (uint32_t)(src_gpu_addr >> 32) & 0x1ffff);
   nv_push_method(p, NVC6B5_OFFSET_IN_LOWER,
                  (uint32_t)(src_gpu_addr & 0xffffffffu));
   nv_push_method(p, NVC6B5_OFFSET_OUT_UPPER,
                  (uint32_t)(dst_gpu_addr >> 32) & 0x1ffff);
   nv_push_method(p, NVC6B5_OFFSET_OUT_LOWER,
                  (uint32_t)(dst_gpu_addr & 0xffffffffu));
   nv_push_method(p, NVC6B5_PITCH_IN, size_bytes);
   nv_push_method(p, NVC6B5_PITCH_OUT, size_bytes);
   nv_push_method(p, NVC6B5_LINE_LENGTH_IN, size_bytes);
   nv_push_method(p, NVC6B5_LINE_COUNT, 1);
   launch = NVC6B5_LAUNCH_DMA_DATA_TRANSFER_TYPE_NON_PIPELINED |
            NVC6B5_LAUNCH_DMA_FLUSH_ENABLE_TRUE |
            NVC6B5_LAUNCH_DMA_SRC_MEMORY_LAYOUT_PITCH |
            NVC6B5_LAUNCH_DMA_DST_MEMORY_LAYOUT_PITCH;
   nv_push_method(p, NVC6B5_LAUNCH_DMA, launch);
}

/**
 * Patch QMD CTA raster from a VkDispatchIndirectCommand at indirect_gpu_addr
 * (3x uint32 x,y,z).  Copies 12 bytes onto QMD dwords 12..14 (width in dw12,
 * height/depth in low 16 of dw13/dw14 — height/depth >65535 not representable
 * in QMD MW fields; CE copies full 32-bit words which is correct for width and
 * sufficient for typical height/depth < 65536).
 */
static inline void
nv_copy_patch_qmd_grid_from_indirect(struct nv_push *p, uint32_t class_copy,
                                     uint64_t indirect_gpu_addr,
                                     uint64_t qmd_gpu_addr)
{
   if (!p || !indirect_gpu_addr || !qmd_gpu_addr)
      return;
   if (class_copy)
      nv_copy_set_object(p, class_copy);
   else
      nv_push_set_subch(p, NV_PUSH_SUBCH_COPY);
   /* One 12-byte copy covers indirect x,y,z -> QMD dw12,dw13,dw14 sequentially */
   nv_copy_emit_small_linear(p, indirect_gpu_addr,
                             qmd_gpu_addr + 12u * 4u, 12);
}

/**
 * Linear buffer-to-buffer copy (pitch layout, virtual addresses, single line).
 * For multi-line 2D copies set pitch_in/pitch_out/line_count and multi-line bit.
 */
static inline void
nv_copy_emit_buffer_copy(struct nv_push *p,
                         uint64_t src_gpu_addr, uint64_t dst_gpu_addr,
                         uint32_t size_bytes,
                         uint32_t pitch_in, uint32_t pitch_out,
                         uint32_t line_count)
{
   uint32_t launch;
   bool multi = line_count > 1;

   nv_push_method(p, NVC6B5_OFFSET_IN_UPPER,
                  (uint32_t)(src_gpu_addr >> 32) & 0x1ffff);
   nv_push_method(p, NVC6B5_OFFSET_IN_LOWER,
                  (uint32_t)(src_gpu_addr & 0xffffffffu));
   nv_push_method(p, NVC6B5_OFFSET_OUT_UPPER,
                  (uint32_t)(dst_gpu_addr >> 32) & 0x1ffff);
   nv_push_method(p, NVC6B5_OFFSET_OUT_LOWER,
                  (uint32_t)(dst_gpu_addr & 0xffffffffu));
   nv_push_method(p, NVC6B5_PITCH_IN, pitch_in ? pitch_in : size_bytes);
   nv_push_method(p, NVC6B5_PITCH_OUT, pitch_out ? pitch_out : size_bytes);
   nv_push_method(p, NVC6B5_LINE_LENGTH_IN, size_bytes);
   nv_push_method(p, NVC6B5_LINE_COUNT, multi ? line_count : 1);

   launch = NVC6B5_LAUNCH_DMA_DATA_TRANSFER_TYPE_PIPELINED |
            NVC6B5_LAUNCH_DMA_FLUSH_ENABLE_TRUE |
            NVC6B5_LAUNCH_DMA_SRC_MEMORY_LAYOUT_PITCH |
            NVC6B5_LAUNCH_DMA_DST_MEMORY_LAYOUT_PITCH;
   if (multi)
      launch |= NVC6B5_LAUNCH_DMA_MULTI_LINE_ENABLE_TRUE;

   nv_push_method(p, NVC6B5_LAUNCH_DMA, launch);
}

/** Convenience: set copy object + linear 1D buffer copy + host WFI. */
static inline void
nv_copy_push_buffer_copy(struct nv_push *p, uint32_t class_copy,
                         uint64_t src_gpu_addr, uint64_t dst_gpu_addr,
                         uint32_t size_bytes)
{
   if (class_copy)
      nv_copy_set_object(p, class_copy);
   else
      nv_push_set_subch(p, NV_PUSH_SUBCH_COPY);
   nv_copy_emit_buffer_copy(p, src_gpu_addr, dst_gpu_addr, size_bytes, 0, 0, 1);
   nv_push_wfi(p);
}

/**
 * Multi-line pitch 2D image copy (LINEAR/PITCH sources and destinations).
 * line_length = bytes per scanline to copy (width * bpp)
 * pitch_in/out = row stride in bytes
 * line_count = number of rows
 */
static inline void
nv_copy_emit_image_2d(struct nv_push *p,
                      uint64_t src_gpu_addr, uint64_t dst_gpu_addr,
                      uint32_t line_length, uint32_t pitch_in,
                      uint32_t pitch_out, uint32_t line_count)
{
   if (!line_count)
      line_count = 1;
   nv_copy_emit_buffer_copy(p, src_gpu_addr, dst_gpu_addr,
                            line_length, pitch_in, pitch_out, line_count);
}

/** Set copy object + 2D pitch image copy + WFI. */
static inline void
nv_copy_push_image_2d(struct nv_push *p, uint32_t class_copy,
                      uint64_t src_gpu_addr, uint64_t dst_gpu_addr,
                      uint32_t line_length, uint32_t pitch_in,
                      uint32_t pitch_out, uint32_t line_count)
{
   if (class_copy)
      nv_copy_set_object(p, class_copy);
   else
      nv_push_set_subch(p, NV_PUSH_SUBCH_COPY);
   nv_copy_emit_image_2d(p, src_gpu_addr, dst_gpu_addr,
                         line_length, pitch_in, pitch_out, line_count);
   nv_push_wfi(p);
}

/**
 * tick122: 2D pitch image copy with CE sema on final LAUNCH_DMA (G1 image path).
 * sema_gpu_addr 0 falls back to sema-less image_2d + optional WFI by caller.
 */
static inline void
nv_copy_emit_image_2d_with_sema(struct nv_push *p,
                                uint64_t src_gpu_addr, uint64_t dst_gpu_addr,
                                uint32_t line_length, uint32_t pitch_in,
                                uint32_t pitch_out, uint32_t line_count,
                                uint64_t sema_gpu_addr, uint32_t sema_payload)
{
   uint32_t launch;

   if (!p || !line_length)
      return;
   if (!line_count)
      line_count = 1;
   if (!pitch_in)
      pitch_in = line_length;
   if (!pitch_out)
      pitch_out = line_length;

   if (sema_gpu_addr)
      nv_copy_set_semaphore(p, sema_gpu_addr, sema_payload);

   nv_push_method(p, NVC6B5_OFFSET_IN_UPPER,
                  (uint32_t)(src_gpu_addr >> 32) & 0x1ffff);
   nv_push_method(p, NVC6B5_OFFSET_IN_LOWER,
                  (uint32_t)(src_gpu_addr & 0xffffffffu));
   nv_push_method(p, NVC6B5_OFFSET_OUT_UPPER,
                  (uint32_t)(dst_gpu_addr >> 32) & 0x1ffff);
   nv_push_method(p, NVC6B5_OFFSET_OUT_LOWER,
                  (uint32_t)(dst_gpu_addr & 0xffffffffu));
   nv_push_method(p, NVC6B5_PITCH_IN, pitch_in);
   nv_push_method(p, NVC6B5_PITCH_OUT, pitch_out);
   nv_push_method(p, NVC6B5_LINE_LENGTH_IN, line_length);
   nv_push_method(p, NVC6B5_LINE_COUNT, line_count);

   launch = NVC6B5_LAUNCH_DMA_DATA_TRANSFER_TYPE_NON_PIPELINED |
            NVC6B5_LAUNCH_DMA_FLUSH_ENABLE_TRUE |
            NVC6B5_LAUNCH_DMA_SRC_MEMORY_LAYOUT_PITCH |
            NVC6B5_LAUNCH_DMA_DST_MEMORY_LAYOUT_PITCH;
   if (sema_gpu_addr)
      launch = nv_copy_launch_dma_with_sema_one_word(launch);
   nv_push_method(p, NVC6B5_LAUNCH_DMA, launch);
}

/** SET_OBJECT + 2D pitch copy + CE sema (no host WFI). */
static inline void
nv_copy_push_image_2d_sema(struct nv_push *p, uint32_t class_copy,
                           uint64_t src_gpu_addr, uint64_t dst_gpu_addr,
                           uint32_t line_length, uint32_t pitch_in,
                           uint32_t pitch_out, uint32_t line_count,
                           uint64_t sema_gpu_addr, uint32_t sema_payload)
{
   if (class_copy)
      nv_copy_set_object(p, class_copy);
   else
      nv_push_set_subch(p, NV_PUSH_SUBCH_COPY);
   nv_copy_emit_image_2d_with_sema(p, src_gpu_addr, dst_gpu_addr, line_length,
                                   pitch_in, pitch_out, line_count,
                                   sema_gpu_addr, sema_payload);
}

/**
 * tick122: G1 combined slice — linear buffer copy sema then 2D pitch copy sema
 * (second sema payload = sema_payload+1 if sema2 not set).
 */
static inline void
nv_copy_emit_g1_buffer_then_image2d_sema(struct nv_push *p,
                                         uint64_t buf_src, uint64_t buf_dst,
                                         uint32_t buf_size,
                                         uint64_t img_src, uint64_t img_dst,
                                         uint32_t line_length, uint32_t pitch_in,
                                         uint32_t pitch_out, uint32_t line_count,
                                         uint64_t sema_gpu_addr,
                                         uint32_t sema_payload)
{
   if (!p)
      return;
   if (buf_size && buf_src && buf_dst)
      nv_copy_emit_buffer_copy_with_sema(p, buf_src, buf_dst, buf_size,
                                         sema_gpu_addr, sema_payload);
   if (line_length && img_src && img_dst)
      nv_copy_emit_image_2d_with_sema(p, img_src, img_dst, line_length,
                                      pitch_in, pitch_out, line_count,
                                      sema_gpu_addr,
                                      sema_payload ? sema_payload + 1u
                                                   : 0x43u);
}

/**
 * Blocklinear <-> pitch or blocklinear <-> blocklinear 2D copy via NVC6B5.
 * When src_bl/dst_bl is false, that side uses pitch layout (PITCH_IN/OUT set).
 * width/height in elements (pixels); bpp used only for pitch line_length.
 * origin_x/y are pixel offsets into the surface.
 */
static inline void
nv_copy_emit_image_2d_bl(struct nv_push *p,
                         uint64_t src_gpu_addr, uint64_t dst_gpu_addr,
                         uint32_t width, uint32_t height, uint32_t bpp,
                         uint32_t pitch_in, uint32_t pitch_out,
                         uint32_t src_origin_x, uint32_t src_origin_y,
                         uint32_t dst_origin_x, uint32_t dst_origin_y,
                         bool src_bl, bool dst_bl,
                         uint32_t src_bl_w, uint32_t src_bl_h,
                         uint32_t dst_bl_w, uint32_t dst_bl_h)
{
   uint32_t launch;
   uint32_t line_len = width * (bpp ? bpp : 1);

   nv_push_method(p, NVC6B5_OFFSET_IN_UPPER,
                  (uint32_t)(src_gpu_addr >> 32) & 0x1ffff);
   nv_push_method(p, NVC6B5_OFFSET_IN_LOWER,
                  (uint32_t)(src_gpu_addr & 0xffffffffu));
   nv_push_method(p, NVC6B5_OFFSET_OUT_UPPER,
                  (uint32_t)(dst_gpu_addr >> 32) & 0x1ffff);
   nv_push_method(p, NVC6B5_OFFSET_OUT_LOWER,
                  (uint32_t)(dst_gpu_addr & 0xffffffffu));

   if (src_bl) {
      nv_push_method(p, NVC6B5_SET_SRC_BLOCK_SIZE,
                     NVC6B5_BLOCK_SIZE_ONE_GOB_EACH |
                     ((src_bl_w & 0xf)) | ((src_bl_h & 0xf) << 4));
      nv_push_method(p, NVC6B5_SET_SRC_WIDTH, width);
      nv_push_method(p, NVC6B5_SET_SRC_HEIGHT, height);
      nv_push_method(p, NVC6B5_SET_SRC_DEPTH, 1);
      nv_push_method(p, NVC6B5_SET_SRC_LAYER, 0);
      nv_push_method(p, NVC6B5_SET_SRC_ORIGIN,
                     (src_origin_x & 0xffff) | ((src_origin_y & 0xffff) << 16));
   } else {
      nv_push_method(p, NVC6B5_PITCH_IN, pitch_in ? pitch_in : line_len);
   }

   if (dst_bl) {
      nv_push_method(p, NVC6B5_SET_DST_BLOCK_SIZE,
                     NVC6B5_BLOCK_SIZE_ONE_GOB_EACH |
                     ((dst_bl_w & 0xf)) | ((dst_bl_h & 0xf) << 4));
      nv_push_method(p, NVC6B5_SET_DST_WIDTH, width);
      nv_push_method(p, NVC6B5_SET_DST_HEIGHT, height);
      nv_push_method(p, NVC6B5_SET_DST_DEPTH, 1);
      nv_push_method(p, NVC6B5_SET_DST_LAYER, 0);
      nv_push_method(p, NVC6B5_SET_DST_ORIGIN,
                     (dst_origin_x & 0xffff) | ((dst_origin_y & 0xffff) << 16));
   } else {
      nv_push_method(p, NVC6B5_PITCH_OUT, pitch_out ? pitch_out : line_len);
   }

   nv_push_method(p, NVC6B5_LINE_LENGTH_IN, line_len);
   nv_push_method(p, NVC6B5_LINE_COUNT, height ? height : 1);

   launch = NVC6B5_LAUNCH_DMA_DATA_TRANSFER_TYPE_PIPELINED |
            NVC6B5_LAUNCH_DMA_FLUSH_ENABLE_TRUE |
            NVC6B5_LAUNCH_DMA_MULTI_LINE_ENABLE_TRUE;
   if (!src_bl)
      launch |= NVC6B5_LAUNCH_DMA_SRC_MEMORY_LAYOUT_PITCH;
   if (!dst_bl)
      launch |= NVC6B5_LAUNCH_DMA_DST_MEMORY_LAYOUT_PITCH;

   nv_push_method(p, NVC6B5_LAUNCH_DMA, launch);
}

static inline void
nv_copy_push_image_2d_bl(struct nv_push *p, uint32_t class_copy,
                         uint64_t src_gpu_addr, uint64_t dst_gpu_addr,
                         uint32_t width, uint32_t height, uint32_t bpp,
                         uint32_t pitch_in, uint32_t pitch_out,
                         uint32_t src_ox, uint32_t src_oy,
                         uint32_t dst_ox, uint32_t dst_oy,
                         bool src_bl, bool dst_bl)
{
   if (class_copy)
      nv_copy_set_object(p, class_copy);
   else
      nv_push_set_subch(p, NV_PUSH_SUBCH_COPY);
   nv_copy_emit_image_2d_bl(p, src_gpu_addr, dst_gpu_addr,
                            width, height, bpp, pitch_in, pitch_out,
                            src_ox, src_oy, dst_ox, dst_oy,
                            src_bl, dst_bl, 0, 0, 0, 0);
   nv_push_wfi(p);
}


/**
 * Copy indirect draw command records from a GPU-only indirect BO into a
 * host-mappable shadow BO so the CPU can read vertex/instance counts at
 * record time (indirect draw path B).  size_bytes should be
 * drawCount * stride (16 or 20 for non-indexed/indexed).
 */
static inline void
nv_copy_indirect_to_shadow(struct nv_push *p, uint32_t class_copy,
                           uint64_t indirect_gpu_addr,
                           uint64_t shadow_gpu_addr,
                           uint32_t size_bytes)
{
   if (!p || !indirect_gpu_addr || !shadow_gpu_addr || !size_bytes)
      return;
   if (class_copy)
      nv_copy_set_object(p, class_copy);
   else
      nv_push_set_subch(p, NV_PUSH_SUBCH_COPY);
   nv_copy_emit_small_linear(p, indirect_gpu_addr, shadow_gpu_addr, size_bytes);
   nv_push_wfi(p);
}

/**
 * tick109: 2D pitch copy (width_bytes × height lines) with optional sema on
 * same LAUNCH_DMA. pitch_in/out default to width_bytes when 0. MULTI_LINE when
 * height > 1. Non-pipelined unless pipelined=true; refine on silicon.
 */
static inline void
nv_copy_emit_pitch2d_copy_with_sema(struct nv_push *p,
                                    uint64_t src_gpu_addr,
                                    uint64_t dst_gpu_addr,
                                    uint32_t width_bytes,
                                    uint32_t height,
                                    uint32_t pitch_in,
                                    uint32_t pitch_out,
                                    uint64_t sema_gpu_addr,
                                    uint32_t sema_payload,
                                    bool pipelined)
{
   uint32_t launch;
   uint32_t h = height ? height : 1u;
   uint32_t pin = pitch_in ? pitch_in : width_bytes;
   uint32_t pout = pitch_out ? pitch_out : width_bytes;

   if (!p || !width_bytes)
      return;

   if (sema_gpu_addr)
      nv_copy_set_semaphore(p, sema_gpu_addr, sema_payload);

   nv_push_method(p, NVC6B5_OFFSET_IN_UPPER,
                  (uint32_t)(src_gpu_addr >> 32) & 0x1ffff);
   nv_push_method(p, NVC6B5_OFFSET_IN_LOWER,
                  (uint32_t)(src_gpu_addr & 0xffffffffu));
   nv_push_method(p, NVC6B5_OFFSET_OUT_UPPER,
                  (uint32_t)(dst_gpu_addr >> 32) & 0x1ffff);
   nv_push_method(p, NVC6B5_OFFSET_OUT_LOWER,
                  (uint32_t)(dst_gpu_addr & 0xffffffffu));
   nv_push_method(p, NVC6B5_PITCH_IN, pin);
   nv_push_method(p, NVC6B5_PITCH_OUT, pout);
   nv_push_method(p, NVC6B5_LINE_LENGTH_IN, width_bytes);
   nv_push_method(p, NVC6B5_LINE_COUNT, h);

   launch = (pipelined ? NVC6B5_LAUNCH_DMA_DATA_TRANSFER_TYPE_PIPELINED
                       : NVC6B5_LAUNCH_DMA_DATA_TRANSFER_TYPE_NON_PIPELINED) |
            NVC6B5_LAUNCH_DMA_FLUSH_ENABLE_TRUE |
            NVC6B5_LAUNCH_DMA_SRC_MEMORY_LAYOUT_PITCH |
            NVC6B5_LAUNCH_DMA_DST_MEMORY_LAYOUT_PITCH;
   if (h > 1)
      launch |= NVC6B5_LAUNCH_DMA_MULTI_LINE_ENABLE_TRUE;
   if (sema_gpu_addr)
      launch = nv_copy_launch_dma_with_sema_one_word(launch);
   nv_push_method(p, NVC6B5_LAUNCH_DMA, launch);
}

/**
 * tick109: buffer copy then separate sema-only LAUNCH_DMA (split path).
 * Some CE/class combos only complete sema on a follow-up no-transfer launch.
 */
static inline void
nv_copy_emit_buffer_copy_then_sema_release(struct nv_push *p,
                                           uint64_t src_gpu_addr,
                                           uint64_t dst_gpu_addr,
                                           uint32_t size_bytes,
                                           uint64_t sema_gpu_addr,
                                           uint32_t sema_payload,
                                           bool pipelined)
{
   if (!p)
      return;
   if (pipelined)
      nv_copy_emit_buffer_copy_with_sema_pipelined(p, src_gpu_addr, dst_gpu_addr,
                                                   size_bytes, 0, 0);
   else
      nv_copy_emit_buffer_copy_with_sema(p, src_gpu_addr, dst_gpu_addr,
                                         size_bytes, 0, 0);
   if (sema_gpu_addr)
      nv_copy_emit_semaphore_release(p, sema_gpu_addr, sema_payload);
}

/**
 * tick157 / pass21: G1 CE buffer copy (engine sema in LAUNCH_DMA optional via
 * sema_gpu=0 on copy), then pass21 host sema tail on GPFIFO/3D methods.
 * pre_wfi_on_ce=true issues WFI on COPY subch before switching to sema tail.
 * Returns 0 on success, -1 on bad args.
 */
static inline int
nv_g1_emit_copy_then_host_sema_pass21(struct nv_push *p, uint32_t class_copy,
                                      uint64_t src_gpu_addr,
                                      uint64_t dst_gpu_addr,
                                      uint32_t size_bytes,
                                      bool pipelined,
                                      bool pre_wfi_on_ce,
                                      uint64_t host_sema_gpu,
                                      uint32_t host_sema_payload,
                                      enum nv_host_sema_mode host_sema_mode)
{
   if (!p || !src_gpu_addr || !dst_gpu_addr || !size_bytes || !host_sema_gpu)
      return -1;
   if (class_copy)
      nv_copy_set_object(p, class_copy);
   else
      nv_push_set_subch(p, NV_PUSH_SUBCH_COPY);
   if (pipelined)
      nv_copy_emit_buffer_copy_with_sema_pipelined(p, src_gpu_addr, dst_gpu_addr,
                                                   size_bytes, 0, 0);
   else
      nv_copy_emit_buffer_copy_with_sema(p, src_gpu_addr, dst_gpu_addr,
                                         size_bytes, 0, 0);
   return nv_push_g0_g4_host_sema_tail_pass21(
      p, pre_wfi_on_ce, host_sema_gpu,
      host_sema_payload ? host_sema_payload : 1u, host_sema_mode);
}

/**
 * tick173 / pass23: G1 CE copy + pass23 host sema tail (alias pass21 formal).
 * Used when pass23 symmetry audit wants explicit pass23 entry; same methods.
 */
static inline int
nv_g1_emit_copy_then_host_sema_pass23(struct nv_push *p, uint32_t class_copy,
                                      uint64_t src_gpu_addr,
                                      uint64_t dst_gpu_addr,
                                      uint32_t size_bytes,
                                      bool pipelined,
                                      bool pre_wfi_on_ce,
                                      uint64_t host_sema_gpu,
                                      uint32_t host_sema_payload,
                                      enum nv_host_sema_mode host_sema_mode)
{
   if (!p || !src_gpu_addr || !dst_gpu_addr || !size_bytes || !host_sema_gpu)
      return -1;
   if (class_copy)
      nv_copy_set_object(p, class_copy);
   else
      nv_push_set_subch(p, NV_PUSH_SUBCH_COPY);
   if (pipelined)
      nv_copy_emit_buffer_copy_with_sema_pipelined(p, src_gpu_addr, dst_gpu_addr,
                                                   size_bytes, 0, 0);
   else
      nv_copy_emit_buffer_copy_with_sema(p, src_gpu_addr, dst_gpu_addr,
                                         size_bytes, 0, 0);
   return nv_push_g0_g4_host_sema_tail_pass23(
      p, pre_wfi_on_ce, host_sema_gpu,
      host_sema_payload ? host_sema_payload : 1u, host_sema_mode);
}

/**
 * tick178 / pass24: G1 CE copy + pass24 host sema (pass23/24 policy preferred).
 */
static inline int
nv_g1_emit_copy_then_host_sema_pass24(struct nv_push *p, uint32_t class_copy,
                                      uint64_t src_gpu_addr,
                                      uint64_t dst_gpu_addr,
                                      uint32_t size_bytes,
                                      bool pipelined,
                                      bool pre_wfi_on_ce,
                                      uint64_t host_sema_gpu,
                                      uint32_t host_sema_payload,
                                      enum nv_host_sema_mode host_sema_mode)
{
   if (!p || !src_gpu_addr || !dst_gpu_addr || !size_bytes || !host_sema_gpu)
      return -1;
   if (class_copy)
      nv_copy_set_object(p, class_copy);
   else
      nv_push_set_subch(p, NV_PUSH_SUBCH_COPY);
   if (pipelined)
      nv_copy_emit_buffer_copy_with_sema_pipelined(p, src_gpu_addr, dst_gpu_addr,
                                                   size_bytes, 0, 0);
   else
      nv_copy_emit_buffer_copy_with_sema(p, src_gpu_addr, dst_gpu_addr,
                                         size_bytes, 0, 0);
   return nv_push_g0_g4_host_sema_tail_pass24(
      p, pre_wfi_on_ce, host_sema_gpu,
      host_sema_payload ? host_sema_payload : 1u, host_sema_mode);
}

/**
 * tick182 / pass25: G1 CE copy + pass25 host sema (pass25 symmetry entry).
 */
static inline int
nv_g1_emit_copy_then_host_sema_pass25(struct nv_push *p, uint32_t class_copy,
                                      uint64_t src_gpu_addr,
                                      uint64_t dst_gpu_addr,
                                      uint32_t size_bytes,
                                      bool pipelined,
                                      bool pre_wfi_on_ce,
                                      uint64_t host_sema_gpu,
                                      uint32_t host_sema_payload,
                                      enum nv_host_sema_mode host_sema_mode)
{
   if (!p || !src_gpu_addr || !dst_gpu_addr || !size_bytes || !host_sema_gpu)
      return -1;
   if (class_copy)
      nv_copy_set_object(p, class_copy);
   else
      nv_push_set_subch(p, NV_PUSH_SUBCH_COPY);
   if (pipelined)
      nv_copy_emit_buffer_copy_with_sema_pipelined(p, src_gpu_addr, dst_gpu_addr,
                                                   size_bytes, 0, 0);
   else
      nv_copy_emit_buffer_copy_with_sema(p, src_gpu_addr, dst_gpu_addr,
                                         size_bytes, 0, 0);
   return nv_push_g0_g4_host_sema_tail_pass25(
      p, pre_wfi_on_ce, host_sema_gpu,
      host_sema_payload ? host_sema_payload : 1u, host_sema_mode);
}

/**
 * tick186 / pass26: G1 CE copy + pass26 host sema.
 */
static inline int
nv_g1_emit_copy_then_host_sema_pass26(struct nv_push *p, uint32_t class_copy,
                                      uint64_t src_gpu_addr,
                                      uint64_t dst_gpu_addr,
                                      uint32_t size_bytes,
                                      bool pipelined,
                                      bool pre_wfi_on_ce,
                                      uint64_t host_sema_gpu,
                                      uint32_t host_sema_payload,
                                      enum nv_host_sema_mode host_sema_mode)
{
   if (!p || !src_gpu_addr || !dst_gpu_addr || !size_bytes || !host_sema_gpu)
      return -1;
   if (class_copy)
      nv_copy_set_object(p, class_copy);
   else
      nv_push_set_subch(p, NV_PUSH_SUBCH_COPY);
   if (pipelined)
      nv_copy_emit_buffer_copy_with_sema_pipelined(p, src_gpu_addr, dst_gpu_addr,
                                                   size_bytes, 0, 0);
   else
      nv_copy_emit_buffer_copy_with_sema(p, src_gpu_addr, dst_gpu_addr,
                                         size_bytes, 0, 0);
   return nv_push_g0_g4_host_sema_tail_pass26(
      p, pre_wfi_on_ce, host_sema_gpu,
      host_sema_payload ? host_sema_payload : 1u, host_sema_mode);
}

/**
 * tick157: G1 CE copy with engine sema in LAUNCH_DMA, plus pass21 host sema tail
 * (dual fence: CE report + host GPFIFO sema).
 */
static inline int
nv_g1_emit_copy_engine_and_host_sema_pass21(struct nv_push *p,
                                            uint32_t class_copy,
                                            uint64_t src_gpu_addr,
                                            uint64_t dst_gpu_addr,
                                            uint32_t size_bytes,
                                            uint64_t engine_sema_gpu,
                                            uint32_t engine_sema_payload,
                                            bool pre_wfi_on_ce,
                                            uint64_t host_sema_gpu,
                                            uint32_t host_sema_payload,
                                            enum nv_host_sema_mode host_sema_mode)
{
   if (!p || !src_gpu_addr || !dst_gpu_addr || !size_bytes)
      return -1;
   if (class_copy)
      nv_copy_set_object(p, class_copy);
   else
      nv_push_set_subch(p, NV_PUSH_SUBCH_COPY);
   nv_copy_emit_buffer_copy_with_sema(p, src_gpu_addr, dst_gpu_addr, size_bytes,
                                      engine_sema_gpu,
                                      engine_sema_payload ? engine_sema_payload
                                                          : 1u);
   if (!host_sema_gpu)
      return 0;
   return nv_push_g0_g4_host_sema_tail_pass21(
      p, pre_wfi_on_ce, host_sema_gpu,
      host_sema_payload ? host_sema_payload : 1u, host_sema_mode);
}

#ifdef __cplusplus
}
#endif

#endif /* NV_COPY_METHODS_H */
