/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Subset of NVC597 (Ampere 3D) method offsets and bitfields from
 * open-gpu-kernel-modules class/clc597.h.  Later generations (NVC697/NVC797)
 * preserve these method numbers for CLEAR_SURFACE / colour clear / basic draw.
 * Use nv_push_* helpers with these constants for engine method streams.
 */

#ifndef NV_3D_METHODS_H
#define NV_3D_METHODS_H

#include <stdint.h>
#include <string.h>

#include "nv_push.h"

#ifdef __cplusplus
extern "C" {
#endif

/* NVC597 method addresses (byte offsets) */
#define NVC597_SET_OBJECT                       0x0000
#define NVC597_NO_OPERATION                     0x0100
#define NVC597_DRAW_VERTEX_ARRAY_BEGIN_END_A    0x0270
#define NVC597_DRAW_VERTEX_ARRAY_BEGIN_END_B    0x0274
#define NVC597_SET_COLOR_CLEAR_VALUE(i)         (0x0d80 + (i) * 4)
#define NVC597_SET_Z_CLEAR_VALUE                0x0d90
#define NVC597_SET_STENCIL_CLEAR_VALUE          0x0da0
#define NVC597_CLEAR_SURFACE                    0x19d0

/* NVC597_CLEAR_SURFACE bitfields (enable flags as set-bit values) */
#define NVC597_CLEAR_SURFACE_Z_ENABLE_TRUE      (1u << 0)
#define NVC597_CLEAR_SURFACE_STENCIL_ENABLE_TRUE (1u << 1)
#define NVC597_CLEAR_SURFACE_R_ENABLE_TRUE      (1u << 2)
#define NVC597_CLEAR_SURFACE_G_ENABLE_TRUE      (1u << 3)
#define NVC597_CLEAR_SURFACE_B_ENABLE_TRUE      (1u << 4)
#define NVC597_CLEAR_SURFACE_A_ENABLE_TRUE      (1u << 5)
#define NVC597_CLEAR_SURFACE_MRT_SELECT_SHIFT   6
#define NVC597_CLEAR_SURFACE_RT_ARRAY_INDEX_SHIFT 10

/* Primitive topologies for DRAW_VERTEX_ARRAY_BEGIN_END_INSTANCE_* (also used
 * as a guide when selecting begin/end style draw). */
#define NVC597_TOPOLOGY_POINTS                  0x0
#define NVC597_TOPOLOGY_LINES                   0x1
#define NVC597_TOPOLOGY_LINE_LOOP               0x2
#define NVC597_TOPOLOGY_LINE_STRIP              0x3
#define NVC597_TOPOLOGY_TRIANGLES               0x4
#define NVC597_TOPOLOGY_TRIANGLE_STRIP          0x5
#define NVC597_TOPOLOGY_TRIANGLE_FAN            0x6
#define NVC597_TOPOLOGY_QUADS                   0x7
#define NVC597_TOPOLOGY_QUAD_STRIP              0x8
#define NVC597_TOPOLOGY_POLYGON                 0x9

/* Gallium / GL prim modes -> NVC597 topology (subset) */
static inline uint32_t
nv_3d_topology_from_pipe_prim(unsigned pipe_prim)
{
   switch (pipe_prim) {
   case 0:  return NVC597_TOPOLOGY_POINTS;         /* PIPE_PRIM_POINTS */
   case 1:  return NVC597_TOPOLOGY_LINES;          /* PIPE_PRIM_LINES */
   case 2:  return NVC597_TOPOLOGY_LINE_LOOP;
   case 3:  return NVC597_TOPOLOGY_LINE_STRIP;
   case 4:  return NVC597_TOPOLOGY_TRIANGLES;
   case 5:  return NVC597_TOPOLOGY_TRIANGLE_STRIP;
   case 6:  return NVC597_TOPOLOGY_TRIANGLE_FAN;
   case 7:  return NVC597_TOPOLOGY_QUADS;
   case 8:  return NVC597_TOPOLOGY_QUAD_STRIP;
   case 9:  return NVC597_TOPOLOGY_POLYGON;
   default: return NVC597_TOPOLOGY_TRIANGLES;
   }
}

/** Emit SET_OBJECT for the 3D class on the 3D subchannel. */
static inline void
nv_3d_set_object(struct nv_push *p, uint32_t class_3d)
{
   nv_push_set_subch(p, NV_PUSH_SUBCH_3D);
   nv_push_set_object(p, class_3d);
}

/**
 * Emit colour/depth/stencil clear values + CLEAR_SURFACE.
 * colour may be NULL (treated as zero); depth/stencil only if buffers flags set.
 * buffers: bit0=depth, bit1=stencil, bit2=color (matches pipe clear masks loosely;
 * pass pipe_clear buffers bits directly).
 */
static inline void
nv_3d_emit_clear_surface(struct nv_push *p, unsigned buffers,
                         const uint32_t color_ui[4],
                         float depth, uint32_t stencil)
{
   uint32_t clear_flags = 0;
   uint32_t c[4];
   union { float f; uint32_t u; } d;

   if (buffers & 0x10 /* PIPE_CLEAR_COLOR0 / COLOR */) {
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

   if (buffers & 0x100 /* PIPE_CLEAR_DEPTH */) {
      d.f = depth;
      nv_push_method(p, NVC597_SET_Z_CLEAR_VALUE, d.u);
      clear_flags |= NVC597_CLEAR_SURFACE_Z_ENABLE_TRUE;
   }

   if (buffers & 0x200 /* PIPE_CLEAR_STENCIL */) {
      nv_push_method(p, NVC597_SET_STENCIL_CLEAR_VALUE, stencil & 0xff);
      clear_flags |= NVC597_CLEAR_SURFACE_STENCIL_ENABLE_TRUE;
   }

   /* Also accept combined depth+stencil single bit if caller ORs them */
   if ((buffers & 0x300) == 0x300) {
      clear_flags |= NVC597_CLEAR_SURFACE_Z_ENABLE_TRUE |
                     NVC597_CLEAR_SURFACE_STENCIL_ENABLE_TRUE;
   }

   if (clear_flags)
      nv_push_method(p, NVC597_CLEAR_SURFACE, clear_flags);
}

/**
 * Emit non-indexed vertex array draw: BEGIN_END_A (start index) + BEGIN_END_B (count).
 * Requires vertex arrays / shaders programmed separately.
 */
static inline void
nv_3d_emit_draw_vertex_array(struct nv_push *p, uint32_t start, uint32_t count)
{
   nv_push_method(p, NVC597_DRAW_VERTEX_ARRAY_BEGIN_END_A, start);
   nv_push_method(p, NVC597_DRAW_VERTEX_ARRAY_BEGIN_END_B, count);
}

/**
 * Convenience: set 3D object, clear, WFI.  class_3d may be 0 to skip SET_OBJECT.
 */
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

/**
 * Convenience: set 3D object + draw vertex array range + WFI.
 */
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
