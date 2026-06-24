/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * NVC597 (Ampere 3D) method offsets and helpers from open-gpu-kernel-modules
 * class/clc597.h and nvidia-3d-color-targets.h patterns.  Method numbers are
 * stable through NVC697/NVC797 for the subset used here.
 */

#ifndef NV_3D_METHODS_H
#define NV_3D_METHODS_H

#include <stdint.h>
#include <string.h>

#include "nv_push.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Method addresses (byte offsets) --- */
#define NVC597_SET_OBJECT                       0x0000
#define NVC597_NO_OPERATION                     0x0100
#define NVC597_DRAW_INDEX_BUFFER_BEGIN_END_A    0x0268
#define NVC597_DRAW_INDEX_BUFFER_BEGIN_END_B    0x026c
#define NVC597_DRAW_VERTEX_ARRAY_BEGIN_END_A    0x0270
#define NVC597_DRAW_VERTEX_ARRAY_BEGIN_END_B    0x0274

/* Vertex stream j: stride/enable (A), location hi/lo (B/C), frequency (D) */
#define NVC597_SET_VERTEX_STREAM_A_FORMAT(j)    (0x1c00 + (j) * 16)
#define NVC597_SET_VERTEX_STREAM_A_LOCATION_A(j) (0x1c04 + (j) * 16)
#define NVC597_SET_VERTEX_STREAM_A_LOCATION_B(j) (0x1c08 + (j) * 16)
#define NVC597_SET_VERTEX_STREAM_A_FREQUENCY(j) (0x1c0c + (j) * 16)
#define NVC597_SET_VERTEX_STREAM_A_FORMAT_ENABLE_TRUE (1u << 12)

#define NVC597_SET_VERTEX_STREAM_SIZE_A(j)      (0x0600 + (j) * 8)
#define NVC597_SET_VERTEX_STREAM_SIZE_B(j)      (0x0604 + (j) * 8)

#define NVC597_SET_VERTEX_ATTRIBUTE_A(i)        (0x1160 + (i) * 4)
#define NVC597_SET_VERTEX_ATTRIBUTE_A_STREAM_SHIFT            0
#define NVC597_SET_VERTEX_ATTRIBUTE_A_SOURCE_INACTIVE         (1u << 6)
#define NVC597_SET_VERTEX_ATTRIBUTE_A_OFFSET_SHIFT            7
#define NVC597_SET_VERTEX_ATTRIBUTE_A_COMPONENT_BIT_WIDTHS_SHIFT 21
#define NVC597_SET_VERTEX_ATTRIBUTE_A_COMPONENT_BIT_WIDTHS_R32_G32_B32_A32  0x01
#define NVC597_SET_VERTEX_ATTRIBUTE_A_COMPONENT_BIT_WIDTHS_R32_G32_B32      0x02
#define NVC597_SET_VERTEX_ATTRIBUTE_A_COMPONENT_BIT_WIDTHS_R16_G16_B16_A16  0x03
#define NVC597_SET_VERTEX_ATTRIBUTE_A_COMPONENT_BIT_WIDTHS_R32_G32          0x04
#define NVC597_SET_VERTEX_ATTRIBUTE_A_COMPONENT_BIT_WIDTHS_R8_G8_B8_A8      0x0A
#define NVC597_SET_VERTEX_ATTRIBUTE_A_COMPONENT_BIT_WIDTHS_R32              0x0F
#define NVC597_SET_VERTEX_ATTRIBUTE_A_COMPONENT_BIT_WIDTHS_R16_G16          0x0C
#define NVC597_SET_VERTEX_ATTRIBUTE_A_COMPONENT_BIT_WIDTHS_R8_G8            0x18

#define NVC597_SET_INDEX_BUFFER_A               0x17c8
#define NVC597_SET_INDEX_BUFFER_B               0x17cc
#define NVC597_SET_INDEX_BUFFER_C               0x17d0
#define NVC597_SET_INDEX_BUFFER_D               0x17d4
#define NVC597_SET_INDEX_BUFFER_E               0x17d8
#define NVC597_SET_INDEX_BUFFER_F               0x17dc
#define NVC597_SET_INDEX_BUFFER_SIZE_A          0x0238
#define NVC597_SET_INDEX_BUFFER_SIZE_B          0x023c

#define NVC597_SET_INDEX_BUFFER_E_INDEX_SIZE_ONE_BYTE   0x0
#define NVC597_SET_INDEX_BUFFER_E_INDEX_SIZE_TWO_BYTES  0x1
#define NVC597_SET_INDEX_BUFFER_E_INDEX_SIZE_FOUR_BYTES 0x2

#define NVC597_SET_COLOR_TARGET_A(j)            (0x0800 + (j) * 64)
#define NVC597_SET_COLOR_TARGET_B(j)            (0x0804 + (j) * 64)
#define NVC597_SET_COLOR_TARGET_WIDTH(j)        (0x0808 + (j) * 64)
#define NVC597_SET_COLOR_TARGET_HEIGHT(j)       (0x080c + (j) * 64)
#define NVC597_SET_COLOR_TARGET_FORMAT(j)       (0x0810 + (j) * 64)
#define NVC597_SET_COLOR_TARGET_MEMORY(j)       (0x0814 + (j) * 64)
#define NVC597_SET_COLOR_TARGET_THIRD_DIMENSION(j) (0x0818 + (j) * 64)
#define NVC597_SET_COLOR_TARGET_FORMAT_V_DISABLED   0x00
#define NVC597_SET_COLOR_TARGET_FORMAT_V_A8R8G8B8   0xCF
#define NVC597_SET_COLOR_TARGET_FORMAT_V_A8B8G8R8   0xD5
#define NVC597_SET_COLOR_TARGET_FORMAT_V_X8B8G8R8   0xF9
#define NVC597_SET_COLOR_TARGET_FORMAT_V_B8G8R8A8   0x47
#define NVC597_SET_COLOR_TARGET_FORMAT_V_R5G6B5     0xE8
#define NVC597_SET_COLOR_TARGET_FORMAT_V_A1R5G5B5   0xE9
#define NVC597_SET_COLOR_TARGET_FORMAT_V_R32        0xFF
#define NVC597_SET_COLOR_TARGET_FORMAT_V_RF32_GF32_BF32_AF32 0xC0
#define NVC597_SET_COLOR_TARGET_FORMAT_V_R16_G16_B16_A16     0xC6
#define NVC597_SET_COLOR_TARGET_FORMAT_V_RF16_GF16_BF16_AF16 0xCA
#define NVC597_SET_COLOR_TARGET_MEMORY_LAYOUT_BLOCKLINEAR 0x00000000
#define NVC597_SET_COLOR_TARGET_MEMORY_LAYOUT_PITCH       (1u << 12)

#define NVC597_SET_CT_SELECT                    0x121c
#define NVC597_SET_CT_SELECT_TARGET_COUNT_SHIFT 0
#define NVC597_SET_CT_SELECT_TARGET0_SHIFT      4
#define NVC597_SET_CT_SELECT_TARGET1_SHIFT      8
#define NVC597_SET_CT_SELECT_TARGET2_SHIFT      12
#define NVC597_SET_CT_SELECT_TARGET3_SHIFT      16
#define NVC597_SET_CT_SELECT_TARGET4_SHIFT      20
#define NVC597_SET_CT_SELECT_TARGET5_SHIFT      24
#define NVC597_SET_CT_SELECT_TARGET6_SHIFT      28

#define NVC597_SET_ZT_A                         0x0fe0
#define NVC597_SET_ZT_B                         0x0fe4
#define NVC597_SET_ZT_FORMAT                    0x0fe8
#define NVC597_SET_ZT_BLOCK_SIZE                0x0fec
#define NVC597_SET_ZT_ARRAY_PITCH               0x0ff0
#define NVC597_SET_ZT_FORMAT_V_ZF32             0x0A
#define NVC597_SET_ZT_FORMAT_V_Z16              0x13
#define NVC597_SET_ZT_FORMAT_V_Z24S8            0x14
#define NVC597_SET_ZT_FORMAT_V_X8Z24            0x15
#define NVC597_SET_ZT_FORMAT_V_S8Z24            0x16
#define NVC597_SET_ZT_FORMAT_V_S8               0x17
#define NVC597_SET_ZT_FORMAT_V_ZF32_X24S8       0x19

#define NVC597_SET_SURFACE_CLIP_HORIZONTAL      0x0ff4
#define NVC597_SET_SURFACE_CLIP_VERTICAL        0x0ff8
#define NVC597_SET_ZT_READ_ONLY                 0x07f8

#define NVC597_SET_VIEWPORT_SCALE_X(j)          (0x0a00 + (j) * 32)
#define NVC597_SET_VIEWPORT_SCALE_Y(j)          (0x0a04 + (j) * 32)
#define NVC597_SET_VIEWPORT_SCALE_Z(j)          (0x0a08 + (j) * 32)
#define NVC597_SET_VIEWPORT_OFFSET_X(j)         (0x0a0c + (j) * 32)
#define NVC597_SET_VIEWPORT_OFFSET_Y(j)         (0x0a10 + (j) * 32)
#define NVC597_SET_VIEWPORT_OFFSET_Z(j)         (0x0a14 + (j) * 32)

#define NVC597_SET_SCISSOR_ENABLE(j)            (0x0e00 + (j) * 16)
#define NVC597_SET_SCISSOR_HORIZONTAL(j)        (0x0e04 + (j) * 16)
#define NVC597_SET_SCISSOR_VERTICAL(j)          (0x0e08 + (j) * 16)

#define NVC597_SET_COLOR_CLEAR_VALUE(i)         (0x0d80 + (i) * 4)
#define NVC597_SET_Z_CLEAR_VALUE                0x0d90
#define NVC597_SET_STENCIL_CLEAR_VALUE          0x0da0
#define NVC597_CLEAR_SURFACE                    0x19d0

#define NVC597_CLEAR_SURFACE_Z_ENABLE_TRUE      (1u << 0)
#define NVC597_CLEAR_SURFACE_STENCIL_ENABLE_TRUE (1u << 1)
#define NVC597_CLEAR_SURFACE_R_ENABLE_TRUE      (1u << 2)
#define NVC597_CLEAR_SURFACE_G_ENABLE_TRUE      (1u << 3)
#define NVC597_CLEAR_SURFACE_B_ENABLE_TRUE      (1u << 4)
#define NVC597_CLEAR_SURFACE_A_ENABLE_TRUE      (1u << 5)

#define NVC597_TOPOLOGY_POINTS                  0x0
#define NVC597_TOPOLOGY_LINES                   0x1
#define NVC597_TOPOLOGY_LINE_LOOP               0x2
#define NVC597_TOPOLOGY_LINE_STRIP              0x3
#define NVC597_TOPOLOGY_TRIANGLES               0x4
#define NVC597_TOPOLOGY_TRIANGLE_STRIP          0x5
#define NVC597_TOPOLOGY_TRIANGLE_FAN            0x6
#define NVC597_TOPOLOGY_QUADS                   0x7

/* Surface descriptor for RT/ZETA setup */
struct nv_3d_surface {
   uint64_t gpu_addr;
   uint32_t width;
   uint32_t height;
   uint32_t array_pitch;   /* ZT array pitch or third dimension */
   uint32_t format;        /* NVC597_SET_*_FORMAT_V_* value */
   bool block_linear;
   bool enabled;
};

static inline uint32_t
nv_3d_topology_from_pipe_prim(unsigned pipe_prim)
{
   switch (pipe_prim) {
   case 0:  return NVC597_TOPOLOGY_POINTS;
   case 1:  return NVC597_TOPOLOGY_LINES;
   case 2:  return NVC597_TOPOLOGY_LINE_LOOP;
   case 3:  return NVC597_TOPOLOGY_LINE_STRIP;
   case 4:  return NVC597_TOPOLOGY_TRIANGLES;
   case 5:  return NVC597_TOPOLOGY_TRIANGLE_STRIP;
   case 6:  return NVC597_TOPOLOGY_TRIANGLE_FAN;
   case 7:  return NVC597_TOPOLOGY_QUADS;
   default: return NVC597_TOPOLOGY_TRIANGLES;
   }
}

/* Map common pipe_format enum values to NVC597 colour target formats.
 * pipe_format numbers match Mesa's pipe/p_format.h for the common cases. */
static inline uint32_t
nv_3d_color_format_from_pipe(unsigned pipe_fmt)
{
   /* Use numeric values for the most common formats without requiring pipe headers
    * in all translation units; callers can also pass explicit NVC597 format values. */
   switch ((unsigned)pipe_fmt) {
   case 1:  /* PIPE_FORMAT_B8G8R8A8_UNORM */ return NVC597_SET_COLOR_TARGET_FORMAT_V_B8G8R8A8;
   case 2:  /* PIPE_FORMAT_B8G8R8X8_UNORM */ return NVC597_SET_COLOR_TARGET_FORMAT_V_X8B8G8R8;
   case 3:  /* PIPE_FORMAT_A8R8G8B8_UNORM */ return NVC597_SET_COLOR_TARGET_FORMAT_V_A8R8G8B8;
   case 4:  /* PIPE_FORMAT_X8R8G8B8_UNORM */ return NVC597_SET_COLOR_TARGET_FORMAT_V_A8R8G8B8;
   case 5:  /* PIPE_FORMAT_B5G6R5_UNORM */   return NVC597_SET_COLOR_TARGET_FORMAT_V_R5G6B5;
   case 9:  /* PIPE_FORMAT_R8G8B8A8_UNORM */ return NVC597_SET_COLOR_TARGET_FORMAT_V_A8B8G8R8;
   case 10: /* PIPE_FORMAT_R8G8B8X8_UNORM */ return NVC597_SET_COLOR_TARGET_FORMAT_V_X8B8G8R8;
   case 31: /* PIPE_FORMAT_R32_FLOAT */      return NVC597_SET_COLOR_TARGET_FORMAT_V_R32;
   case 38: /* PIPE_FORMAT_R16G16B16A16_FLOAT */ return NVC597_SET_COLOR_TARGET_FORMAT_V_RF16_GF16_BF16_AF16;
   case 39: /* PIPE_FORMAT_R32G32B32A32_FLOAT */ return NVC597_SET_COLOR_TARGET_FORMAT_V_RF32_GF32_BF32_AF32;
   default: return NVC597_SET_COLOR_TARGET_FORMAT_V_A8B8G8R8;
   }
}

static inline uint32_t
nv_3d_zt_format_from_pipe(unsigned pipe_fmt)
{
   switch ((unsigned)pipe_fmt) {
   case 42: /* PIPE_FORMAT_Z16_UNORM */           return NVC597_SET_ZT_FORMAT_V_Z16;
   case 43: /* PIPE_FORMAT_Z32_FLOAT */           return NVC597_SET_ZT_FORMAT_V_ZF32;
   case 44: /* PIPE_FORMAT_Z24_UNORM_S8_UINT */   return NVC597_SET_ZT_FORMAT_V_Z24S8;
   case 45: /* PIPE_FORMAT_Z24X8_UNORM */         return NVC597_SET_ZT_FORMAT_V_X8Z24;
   case 46: /* PIPE_FORMAT_S8_UINT_Z24_UNORM */   return NVC597_SET_ZT_FORMAT_V_S8Z24;
   case 47: /* PIPE_FORMAT_S8_UINT */             return NVC597_SET_ZT_FORMAT_V_S8;
   case 48: /* PIPE_FORMAT_Z32_FLOAT_S8X24_UINT */ return NVC597_SET_ZT_FORMAT_V_ZF32_X24S8;
   default: return NVC597_SET_ZT_FORMAT_V_Z24S8;
   }
}

static inline void
nv_3d_set_object(struct nv_push *p, uint32_t class_3d)
{
   nv_push_set_subch(p, NV_PUSH_SUBCH_3D);
   nv_push_set_object(p, class_3d);
}

/** Select colour target indices (nvidia-3d nv3dSelectColorTarget pattern). */
static inline void
nv_3d_set_ct_select(struct nv_push *p, uint32_t target_count,
                    const uint8_t targets[8])
{
   uint32_t v = (target_count & 0xf) << NVC597_SET_CT_SELECT_TARGET_COUNT_SHIFT;
   unsigned i;
   for (i = 0; i < 8; i++) {
      uint8_t t = targets ? targets[i] : 0;
      v |= ((uint32_t)(t & 0x7)) << (NVC597_SET_CT_SELECT_TARGET0_SHIFT + i * 4);
   }
   nv_push_method(p, NVC597_SET_CT_SELECT, v);
}

/**
 * Emit colour target j setup (nvidia-3d nv3dSetColorTarget): A/B/width/height/
 * format/memory as incrementing method burst.
 */
static inline void
nv_3d_set_color_target(struct nv_push *p, unsigned j,
                       const struct nv_3d_surface *s)
{
   uint32_t mem_info;

   if (!s || !s->enabled) {
      nv_push_method(p, NVC597_SET_COLOR_TARGET_FORMAT(j),
                     NVC597_SET_COLOR_TARGET_FORMAT_V_DISABLED);
      return;
   }

   mem_info = s->block_linear ? NVC597_SET_COLOR_TARGET_MEMORY_LAYOUT_BLOCKLINEAR
                              : NVC597_SET_COLOR_TARGET_MEMORY_LAYOUT_PITCH;

   /* Burst: SET_COLOR_TARGET_A..MEMORY (6 dwords after header, matching nvidia-3d) */
   nv_push_method(p, NVC597_SET_COLOR_TARGET_A(j),
                  (uint32_t)(s->gpu_addr >> 32) & 0xff);
   nv_push_method(p, NVC597_SET_COLOR_TARGET_B(j),
                  (uint32_t)(s->gpu_addr & 0xffffffffu));
   nv_push_method(p, NVC597_SET_COLOR_TARGET_WIDTH(j), s->width);
   nv_push_method(p, NVC597_SET_COLOR_TARGET_HEIGHT(j), s->height);
   nv_push_method(p, NVC597_SET_COLOR_TARGET_FORMAT(j), s->format);
   nv_push_method(p, NVC597_SET_COLOR_TARGET_MEMORY(j), mem_info);
   if (s->array_pitch)
      nv_push_method(p, NVC597_SET_COLOR_TARGET_THIRD_DIMENSION(j), s->array_pitch);
}

/** Emit ZETA (depth/stencil) target setup. */
static inline void
nv_3d_set_zeta_target(struct nv_push *p, const struct nv_3d_surface *s)
{
   if (!s || !s->enabled) {
      /* Disable by writing invalid/zero format is not standard; leave unset.
       * Callers that unbind should pass enabled=false and we zero the address. */
      nv_push_method(p, NVC597_SET_ZT_A, 0);
      nv_push_method(p, NVC597_SET_ZT_B, 0);
      return;
   }

   nv_push_method(p, NVC597_SET_ZT_A, (uint32_t)(s->gpu_addr >> 32) & 0xff);
   nv_push_method(p, NVC597_SET_ZT_B, (uint32_t)(s->gpu_addr & 0xffffffffu));
   nv_push_method(p, NVC597_SET_ZT_FORMAT, s->format);
   nv_push_method(p, NVC597_SET_ZT_BLOCK_SIZE, 0); /* one gob each dim for pitch/linear */
   if (s->array_pitch)
      nv_push_method(p, NVC597_SET_ZT_ARRAY_PITCH, s->array_pitch);
}

/** Surface clip (render area) in pixels. */
static inline void
nv_3d_set_surface_clip(struct nv_push *p, uint32_t x, uint32_t y,
                       uint32_t w, uint32_t h)
{
   nv_push_method(p, NVC597_SET_SURFACE_CLIP_HORIZONTAL,
                  (x & 0xffff) | ((w & 0xffff) << 16));
   nv_push_method(p, NVC597_SET_SURFACE_CLIP_VERTICAL,
                  (y & 0xffff) | ((h & 0xffff) << 16));
}

/** Viewport 0 scale/offset (float bits). */
static inline void
nv_3d_set_viewport0(struct nv_push *p,
                    float scale_x, float scale_y, float scale_z,
                    float offset_x, float offset_y, float offset_z)
{
   union { float f; uint32_t u; } sx, sy, sz, ox, oy, oz;
   sx.f = scale_x; sy.f = scale_y; sz.f = scale_z;
   ox.f = offset_x; oy.f = offset_y; oz.f = offset_z;
   nv_push_method(p, NVC597_SET_VIEWPORT_SCALE_X(0), sx.u);
   nv_push_method(p, NVC597_SET_VIEWPORT_SCALE_Y(0), sy.u);
   nv_push_method(p, NVC597_SET_VIEWPORT_SCALE_Z(0), sz.u);
   nv_push_method(p, NVC597_SET_VIEWPORT_OFFSET_X(0), ox.u);
   nv_push_method(p, NVC597_SET_VIEWPORT_OFFSET_Y(0), oy.u);
   nv_push_method(p, NVC597_SET_VIEWPORT_OFFSET_Z(0), oz.u);
}

/** Scissor 0 enable + rect. */
static inline void
nv_3d_set_scissor0(struct nv_push *p, bool enable,
                   uint32_t xmin, uint32_t ymin, uint32_t xmax, uint32_t ymax)
{
   nv_push_method(p, NVC597_SET_SCISSOR_ENABLE(0), enable ? 1 : 0);
   if (enable) {
      nv_push_method(p, NVC597_SET_SCISSOR_HORIZONTAL(0),
                     (xmin & 0xffff) | ((xmax & 0xffff) << 16));
      nv_push_method(p, NVC597_SET_SCISSOR_VERTICAL(0),
                     (ymin & 0xffff) | ((ymax & 0xffff) << 16));
   }
}

/**
 * Program vertex stream j: format (stride|enable), location, frequency=0,
 * plus stream size (limit) for bounds.
 */
static inline void
nv_3d_set_vertex_stream(struct nv_push *p, unsigned j,
                        uint64_t gpu_addr, uint32_t size_bytes, uint32_t stride)
{
   uint32_t fmt = (stride & 0xfff) | NVC597_SET_VERTEX_STREAM_A_FORMAT_ENABLE_TRUE;
   nv_push_method(p, NVC597_SET_VERTEX_STREAM_A_FORMAT(j), fmt);
   nv_push_method(p, NVC597_SET_VERTEX_STREAM_A_LOCATION_A(j),
                  (uint32_t)(gpu_addr >> 32) & 0xff);
   nv_push_method(p, NVC597_SET_VERTEX_STREAM_A_LOCATION_B(j),
                  (uint32_t)(gpu_addr & 0xffffffffu));
   nv_push_method(p, NVC597_SET_VERTEX_STREAM_A_FREQUENCY(j), 0);
   nv_push_method(p, NVC597_SET_VERTEX_STREAM_SIZE_A(j),
                  (uint32_t)((uint64_t)size_bytes >> 32) & 0xff);
   nv_push_method(p, NVC597_SET_VERTEX_STREAM_SIZE_B(j), size_bytes);
}

/** Vertex attribute i: stream, byte offset, component format code. */
static inline void
nv_3d_set_vertex_attribute(struct nv_push *p, unsigned i,
                           unsigned stream, unsigned offset,
                           unsigned component_format, bool active)
{
   uint32_t v = (stream & 0x1f) |
                (active ? 0 : NVC597_SET_VERTEX_ATTRIBUTE_A_SOURCE_INACTIVE) |
                ((offset & 0x3fff) << NVC597_SET_VERTEX_ATTRIBUTE_A_OFFSET_SHIFT) |
                ((component_format & 0x3f) << NVC597_SET_VERTEX_ATTRIBUTE_A_COMPONENT_BIT_WIDTHS_SHIFT);
   nv_push_method(p, NVC597_SET_VERTEX_ATTRIBUTE_A(i), v);
}

/** Index buffer setup: address, size, index element size (1/2/4). */
static inline void
nv_3d_set_index_buffer(struct nv_push *p, uint64_t gpu_addr,
                       uint64_t size_bytes, unsigned index_size)
{
   uint32_t isz;
   switch (index_size) {
   case 1:  isz = NVC597_SET_INDEX_BUFFER_E_INDEX_SIZE_ONE_BYTE; break;
   case 2:  isz = NVC597_SET_INDEX_BUFFER_E_INDEX_SIZE_TWO_BYTES; break;
   default: isz = NVC597_SET_INDEX_BUFFER_E_INDEX_SIZE_FOUR_BYTES; break;
   }
   nv_push_method(p, NVC597_SET_INDEX_BUFFER_A,
                  (uint32_t)(gpu_addr >> 32) & 0xff);
   nv_push_method(p, NVC597_SET_INDEX_BUFFER_B,
                  (uint32_t)(gpu_addr & 0xffffffffu));
   nv_push_method(p, NVC597_SET_INDEX_BUFFER_C, 0); /* start offset low extra */
   nv_push_method(p, NVC597_SET_INDEX_BUFFER_D, 0);
   nv_push_method(p, NVC597_SET_INDEX_BUFFER_E, isz);
   nv_push_method(p, NVC597_SET_INDEX_BUFFER_F, 0);
   nv_push_method(p, NVC597_SET_INDEX_BUFFER_SIZE_A,
                  (uint32_t)(size_bytes >> 32) & 0xff);
   nv_push_method(p, NVC597_SET_INDEX_BUFFER_SIZE_B,
                  (uint32_t)(size_bytes & 0xffffffffu));
}

static inline void
nv_3d_emit_clear_surface(struct nv_push *p, unsigned buffers,
                         const uint32_t color_ui[4],
                         float depth, uint32_t stencil)
{
   uint32_t clear_flags = 0;
   uint32_t c[4];
   union { float f; uint32_t u; } d;

   if (buffers & 0x10) {
      if (color_ui)
         memcpy(c, color_ui, sizeof(c));
      else
         memset(c, 0, sizeof(c));
      nv_push_method(p, NVC597_SET_COLOR_CLEAR_VALUE(0), c[0]);
      nv_push_method(p, NVC597_SET_COLOR_CLEAR_VALUE(1), c[1]);
      nv_push_method(p, NVC597_SET_COLOR_CLEAR_VALUE(2), c[2]);
      nv_push_method(p, NVC597_SET_COLOR_CLEAR_VALUE(3), c[3]);
      clear_flags |= NVC597_CLEAR_SURFACE_R_ENABLE_TRUE |
                     NVC597_CLEAR_SURFACE_G_ENABLE_TRUE |
                     NVC597_CLEAR_SURFACE_B_ENABLE_TRUE |
                     NVC597_CLEAR_SURFACE_A_ENABLE_TRUE;
   }
   if (buffers & 0x100) {
      d.f = depth;
      nv_push_method(p, NVC597_SET_Z_CLEAR_VALUE, d.u);
      clear_flags |= NVC597_CLEAR_SURFACE_Z_ENABLE_TRUE;
   }
   if (buffers & 0x200) {
      nv_push_method(p, NVC597_SET_STENCIL_CLEAR_VALUE, stencil & 0xff);
      clear_flags |= NVC597_CLEAR_SURFACE_STENCIL_ENABLE_TRUE;
   }
   if (clear_flags)
      nv_push_method(p, NVC597_CLEAR_SURFACE, clear_flags);
}

static inline void
nv_3d_emit_draw_vertex_array(struct nv_push *p, uint32_t start, uint32_t count)
{
   nv_push_method(p, NVC597_DRAW_VERTEX_ARRAY_BEGIN_END_A, start);
   nv_push_method(p, NVC597_DRAW_VERTEX_ARRAY_BEGIN_END_B, count);
}

static inline void
nv_3d_emit_draw_index_buffer(struct nv_push *p, uint32_t first, uint32_t count)
{
   nv_push_method(p, NVC597_DRAW_INDEX_BUFFER_BEGIN_END_A, first);
   nv_push_method(p, NVC597_DRAW_INDEX_BUFFER_BEGIN_END_B, count);
}

static inline void
nv_3d_push_clear(struct nv_push *p, uint32_t class_3d, unsigned buffers,
                 const uint32_t color_ui[4], float depth, uint32_t stencil)
{
   if (class_3d)
      nv_3d_set_object(p, class_3d);
   else
      nv_push_set_subch(p, NV_PUSH_SUBCH_3D);
   nv_3d_emit_clear_surface(p, buffers, color_ui, depth, stencil);
   nv_push_wfi(p);
}

static inline void
nv_3d_push_draw_arrays(struct nv_push *p, uint32_t class_3d,
                       uint32_t start, uint32_t count)
{
   if (class_3d)
      nv_3d_set_object(p, class_3d);
   else
      nv_push_set_subch(p, NV_PUSH_SUBCH_3D);
   nv_3d_emit_draw_vertex_array(p, start, count);
   nv_push_wfi(p);
}

#ifdef __cplusplus
}
#endif

#endif /* NV_3D_METHODS_H */
