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

#define NVC6B5_LAUNCH_DMA_DATA_TRANSFER_TYPE_PIPELINED     0x1
#define NVC6B5_LAUNCH_DMA_DATA_TRANSFER_TYPE_NON_PIPELINED 0x2
#define NVC6B5_LAUNCH_DMA_FLUSH_ENABLE_TRUE                (1u << 2)
#define NVC6B5_LAUNCH_DMA_SRC_MEMORY_LAYOUT_PITCH          (1u << 7)
#define NVC6B5_LAUNCH_DMA_DST_MEMORY_LAYOUT_PITCH          (1u << 8)
#define NVC6B5_LAUNCH_DMA_MULTI_LINE_ENABLE_TRUE           (1u << 9)
#define NVC6B5_LAUNCH_DMA_SRC_TYPE_VIRTUAL                 0
#define NVC6B5_LAUNCH_DMA_DST_TYPE_VIRTUAL                 0

/** SET_OBJECT for copy class on COPY subchannel. */
static inline void
nv_copy_set_object(struct nv_push *p, uint32_t class_copy)
{
   nv_push_set_subch(p, NV_PUSH_SUBCH_COPY);
   nv_push_set_object(p, class_copy);
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

#ifdef __cplusplus
}
#endif

#endif /* NV_COPY_METHODS_H */
