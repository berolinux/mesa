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
#define NVC6B5_SET_SEMAPHORE_A          0x0240
#define NVC6B5_SET_SEMAPHORE_B          0x0244
#define NVC6B5_SET_SEMAPHORE_PAYLOAD    0x0248
#define NVC6B5_LAUNCH_DMA               0x0300
#define NVC6B5_OFFSET_IN_UPPER          0x0400
#define NVC6B5_OFFSET_IN_LOWER          0x0404
#define NVC6B5_OFFSET_OUT_UPPER         0x0408
#define NVC6B5_OFFSET_OUT_LOWER         0x040c
#define NVC6B5_PITCH_IN                 0x0410
#define NVC6B5_PITCH_OUT                0x0414
#define NVC6B5_LINE_LENGTH_IN           0x0418
#define NVC6B5_LINE_COUNT               0x041c
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
#define NVC6B5_LAUNCH_DMA_SRC_MEMORY_LAYOUT_BLOCKLINEAR    0
#define NVC6B5_LAUNCH_DMA_SRC_MEMORY_LAYOUT_PITCH          (1u << 7)
#define NVC6B5_LAUNCH_DMA_DST_MEMORY_LAYOUT_BLOCKLINEAR    0
#define NVC6B5_LAUNCH_DMA_DST_MEMORY_LAYOUT_PITCH          (1u << 8)
#define NVC6B5_LAUNCH_DMA_MULTI_LINE_ENABLE_TRUE           (1u << 9)
#define NVC6B5_LAUNCH_DMA_REMIX_DISABLE                    0
#define NVC6B5_LAUNCH_DMA_SRC_TYPE_VIRTUAL                 0
#define NVC6B5_LAUNCH_DMA_DST_TYPE_VIRTUAL                 0

/* Block size: log2 gobs in each dimension; 0 = one gob (8x8x1 for height fermi) */
#define NVC6B5_BLOCK_SIZE_ONE_GOB_EACH  0x00001000  /* GOB_HEIGHT_FERMI_8 in bits 15:12 */

/** SET_OBJECT for copy class on COPY subchannel. */
static inline void
nv_copy_set_object(struct nv_push *p, uint32_t class_copy)
{
   nv_push_set_subch(p, NV_PUSH_SUBCH_COPY);
   nv_push_set_object(p, class_copy);
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

#ifdef __cplusplus
}
#endif

#endif /* NV_COPY_METHODS_H */
