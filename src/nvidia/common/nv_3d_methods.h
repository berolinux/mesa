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

/* Instancing / draw control (clc597.h) */
#define NVC597_SET_INSTANCE_COUNT               0x0220
#define NVC597_SET_DRAW_CONTROL_A               0x0260
#define NVC597_SET_DRAW_CONTROL_B               0x0264
#define NVC597_SET_DRAW_CONTROL_A_TOPOLOGY_SHIFT              0
#define NVC597_SET_DRAW_CONTROL_A_INSTANCE_ID_FIRST           (0u << 5)
#define NVC597_SET_DRAW_CONTROL_A_INSTANCE_ID_SUBSEQUENT      (1u << 5)
#define NVC597_SET_DRAW_CONTROL_A_INSTANCE_ID_UNCHANGED       (2u << 5)
#define NVC597_SET_DRAW_CONTROL_A_INSTANCE_ITERATE_ENABLE     (1u << 9)
#define NVC597_SET_DRAW_CONTROL_A_IGNORE_GLOBAL_BASE_VERTEX   (1u << 10)
#define NVC597_SET_DRAW_CONTROL_A_IGNORE_GLOBAL_BASE_INSTANCE (1u << 11)
#define NVC597_SET_GLOBAL_BASE_VERTEX_INDEX     0x1434
#define NVC597_SET_GLOBAL_BASE_INSTANCE_INDEX   0x1438
#define NVC597_SET_VERTEX_ID_BASE               0x1118
#define NVC597_SET_PRIMITIVE_TOPOLOGY           0x1970
#define NVC597_SET_PRIMITIVE_TOPOLOGY_CONTROL   0x1948
#define NVC597_SET_PRIMITIVE_TOPOLOGY_CONTROL_USE_SEPARATE    0x1
#define NVC597_SET_DA_PRIMITIVE_RESTART         0x1644
#define NVC597_SET_DA_PRIMITIVE_RESTART_INDEX   0x1648
#define NVC597_SET_DRAW_AUTO_START              0x13a4
#define NVC597_SET_DRAW_AUTO_STRIDE             0x1318

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

/* SPA / program region / pipeline shader (nvidia-3d nv3dLoadProgram pattern) */
#define NVC597_SET_SPA_VERSION                  0x0310
#define NVC597_SET_PROGRAM_REGION_A             0x1608  /* stable since NV9097/NVC097 */
#define NVC597_SET_PROGRAM_REGION_B             0x160c

#define NVC597_SET_PIPELINE_SHADER(j)           (0x2000 + (j) * 64)
#define NVC597_SET_PIPELINE_SHADER_ENABLE_TRUE  0x1
#define NVC597_SET_PIPELINE_SHADER_ENABLE_FALSE 0x0
#define NVC597_SET_PIPELINE_SHADER_TYPE_SHIFT   4
#define NVC597_SET_PIPELINE_SHADER_TYPE_VERTEX_CULL_BEFORE_FETCH 0x0
#define NVC597_SET_PIPELINE_SHADER_TYPE_VERTEX  0x1
#define NVC597_SET_PIPELINE_SHADER_TYPE_TESSELLATION_INIT 0x2
#define NVC597_SET_PIPELINE_SHADER_TYPE_TESSELLATION 0x3
#define NVC597_SET_PIPELINE_SHADER_TYPE_GEOMETRY 0x4
#define NVC597_SET_PIPELINE_SHADER_TYPE_PIXEL   0x5

#define NVC597_SET_PIPELINE_REGISTER_COUNT(j)   (0x200c + (j) * 64)
#define NVC597_SET_PIPELINE_BINDING(j)          (0x2010 + (j) * 64)
#define NVC597_SET_PIPELINE_PROGRAM_ADDRESS_A(j) (0x2014 + (j) * 64)
#define NVC597_SET_PIPELINE_PROGRAM_ADDRESS_B(j) (0x2018 + (j) * 64)

#define NVC597_SET_CONSTANT_BUFFER_SELECTOR_A   0x2380
#define NVC597_SET_CONSTANT_BUFFER_SELECTOR_B   0x2384
#define NVC597_SET_CONSTANT_BUFFER_SELECTOR_C   0x2388
#define NVC597_BIND_GROUP_CONSTANT_BUFFER(j)    (0x2410 + (j) * 32)
#define NVC597_BIND_GROUP_CONSTANT_BUFFER_VALID_TRUE  0x1
#define NVC597_BIND_GROUP_CONSTANT_BUFFER_VALID_FALSE 0x0
#define NVC597_BIND_GROUP_CONSTANT_BUFFER_SHADER_SLOT_SHIFT 4

/* Report semaphore (3D engine completion signal; nvidia-3d / nvkms-headsurface) */
#define NVC597_SET_REPORT_SEMAPHORE_A           0x1b00
#define NVC597_SET_REPORT_SEMAPHORE_B           0x1b04
#define NVC597_SET_REPORT_SEMAPHORE_C           0x1b08
#define NVC597_SET_REPORT_SEMAPHORE_D           0x1b0c
#define NVC597_SET_REPORT_SEMAPHORE_D_OPERATION_RELEASE      0x0
#define NVC597_SET_REPORT_SEMAPHORE_D_OPERATION_ACQUIRE      0x1
#define NVC597_SET_REPORT_SEMAPHORE_D_RELEASE_AFTER_WRITES   (1u << 4)
#define NVC597_SET_REPORT_SEMAPHORE_D_PIPELINE_LOCATION_ALL  (0xfu << 12)
#define NVC597_SET_REPORT_SEMAPHORE_D_STRUCTURE_SIZE_ONE_WORD (1u << 28)
#define NVC597_SET_REPORT_SEMAPHORE_D_STRUCTURE_SIZE_FOUR_WORDS 0
#define NVC597_SET_REPORT_SEMAPHORE_D_FLUSH_DISABLE_TRUE     (1u << 2)

/* Fixed-function / texture pool / channel init (nvidia-3d fermi init) */
#define NVC597_SET_RASTER_ENABLE                0x037c
#define NVC597_SET_SAMPLER_BINDING              0x1234
#define NVC597_SET_SAMPLER_BINDING_V_INDEPENDENTLY      0x0
#define NVC597_SET_SAMPLER_BINDING_V_VIA_HEADER_BINDING 0x1
#define NVC597_SET_DEPTH_TEST                   0x12cc
#define NVC597_SET_FILL_MODE                    0x12d0
#define NVC597_SET_FILL_MODE_V_POINT            0x1
#define NVC597_SET_FILL_MODE_V_WIREFRAME        0x2
#define NVC597_SET_FILL_MODE_V_SOLID            0x3
#define NVC597_SET_SHADE_MODE                   0x12d4
#define NVC597_SET_SHADE_MODE_V_OGL_SMOOTH      0x1d01
#define NVC597_SET_SHADE_MODE_V_OGL_FLAT        0x1d00
#define NVC597_SET_DEPTH_WRITE                  0x12e8
#define NVC597_SET_DEPTH_FUNC                   0x130c
#define NVC597_SET_DEPTH_FUNC_V_OGL_NEVER       0x200
#define NVC597_SET_DEPTH_FUNC_V_OGL_LESS        0x201
#define NVC597_SET_DEPTH_FUNC_V_OGL_EQUAL       0x202
#define NVC597_SET_DEPTH_FUNC_V_OGL_LEQUAL      0x203
#define NVC597_SET_DEPTH_FUNC_V_OGL_GREATER     0x204
#define NVC597_SET_DEPTH_FUNC_V_OGL_NOTEQUAL    0x205
#define NVC597_SET_DEPTH_FUNC_V_OGL_GEQUAL      0x206
#define NVC597_SET_DEPTH_FUNC_V_OGL_ALWAYS      0x207
#define NVC597_INVALIDATE_SHADER_CACHES         0x021c
#define NVC597_INVALIDATE_SHADER_CACHES_INSTRUCTION_TRUE  (1u << 0)
#define NVC597_INVALIDATE_SHADER_CACHES_DATA_TRUE         (1u << 4)
#define NVC597_INVALIDATE_SHADER_CACHES_CONSTANT_TRUE     (1u << 12)
#define NVC597_INVALIDATE_SAMPLER_CACHE         0x1330
#define NVC597_INVALIDATE_TEXTURE_HEADER_CACHE  0x1334
#define NVC597_INVALIDATE_TEXTURE_DATA_CACHE    0x1338
#define NVC597_SET_BLEND_SEPARATE_FOR_ALPHA     0x133c
#define NVC597_SET_BLEND_COLOR_OP               0x1340
#define NVC597_SET_BLEND_COLOR_OP_V_OGL_FUNC_ADD 0x8006
#define NVC597_SET_BLEND_COLOR_SOURCE_COEFF     0x1344
#define NVC597_SET_BLEND_COLOR_DEST_COEFF       0x1348
#define NVC597_SET_BLEND_ALPHA_OP               0x134c
#define NVC597_SET_BLEND_ALPHA_SOURCE_COEFF     0x1350
#define NVC597_SET_BLEND_ALPHA_DEST_COEFF       0x1354
#define NVC597_SET_BLEND_COLOR_SOURCE_COEFF_V_OGL_ONE  0x4001
#define NVC597_SET_BLEND_COLOR_SOURCE_COEFF_V_OGL_ZERO 0x4000
#define NVC597_SET_BLEND_COLOR_SOURCE_COEFF_V_OGL_SRC_ALPHA 0x4302
#define NVC597_SET_BLEND_COLOR_SOURCE_COEFF_V_OGL_ONE_MINUS_SRC_ALPHA 0x4303
#define NVC597_SET_BLEND_COLOR_DEST_COEFF_V_OGL_ZERO   0x4000
#define NVC597_SET_BLEND_COLOR_DEST_COEFF_V_OGL_ONE    0x4001
#define NVC597_SET_BLEND_COLOR_DEST_COEFF_V_OGL_ONE_MINUS_SRC_ALPHA 0x4303
#define NVC597_SET_BLEND_COLOR_DEST_COEFF_V_OGL_SRC_ALPHA 0x4302
#define NVC597_SET_BLEND(i)                     (0x1360 + (i) * 4)
#define NVC597_SET_STENCIL_TEST                 0x1380
#define NVC597_SET_STENCIL_OP_FAIL              0x1384
#define NVC597_SET_STENCIL_OP_ZFAIL             0x1388
#define NVC597_SET_STENCIL_OP_ZPASS             0x138c
#define NVC597_SET_STENCIL_FUNC                 0x1390
#define NVC597_SET_STENCIL_FUNC_REF             0x1394
#define NVC597_SET_STENCIL_MASK                 0x139c
#define NVC597_SET_WINDOW_ORIGIN                0x13ac
#define NVC597_SET_WINDOW_ORIGIN_MODE_UPPER_LEFT 0x0
#define NVC597_SET_WINDOW_ORIGIN_FLIP_Y_TRUE    (1u << 4)
#define NVC597_SET_POINT_SIZE                   0x1518
#define NVC597_SET_TEX_SAMPLER_POOL_A           0x155c
#define NVC597_SET_TEX_SAMPLER_POOL_B           0x1560
#define NVC597_SET_TEX_SAMPLER_POOL_C           0x1564
#define NVC597_SET_TEX_HEADER_POOL_A            0x1574
#define NVC597_SET_TEX_HEADER_POOL_B            0x1578
#define NVC597_SET_TEX_HEADER_POOL_C            0x157c
#define NVC597_SET_SPH_VERSION                  0x16a4
#define NVC597_SET_ZCULL_BOUNDS                 0x196c
#define NVC597_SET_LOGIC_OP                     0x19c4
#define NVC597_OGL_SET_CULL                     0x1918
#define NVC597_OGL_SET_FRONT_FACE               0x191c
#define NVC597_OGL_SET_FRONT_FACE_V_CW          0x900
#define NVC597_OGL_SET_FRONT_FACE_V_CCW         0x901
#define NVC597_OGL_SET_CULL_FACE                0x1920
#define NVC597_OGL_SET_CULL_FACE_V_FRONT        0x404
#define NVC597_OGL_SET_CULL_FACE_V_BACK         0x405
#define NVC597_OGL_SET_CULL_FACE_V_FRONT_AND_BACK 0x408
#define NVC597_SET_CT_WRITE(i)                  (0x1a00 + (i) * 4)
#define NVC597_SET_LINE_WIDTH_FLOAT             0x0d40
#define NVC597_SET_PROVOKING_VERTEX             0x0d68
#define NVC597_SET_PROVOKING_VERTEX_V_FIRST     0x0
#define NVC597_SET_PROVOKING_VERTEX_V_LAST      0x1

/* Pipeline stage index (j in SET_PIPELINE_SHADER(j)) */
#define NV_3D_PIPE_STAGE_VERTEX                 0
#define NV_3D_PIPE_STAGE_TESS_INIT              1
#define NV_3D_PIPE_STAGE_TESS                   2
#define NV_3D_PIPE_STAGE_GEOMETRY               3
#define NV_3D_PIPE_STAGE_PIXEL                  4

/* Bind group indices used by nvidia-3d (vertex=0, pixel=4 typically) */
#define NV_3D_BIND_GROUP_VERTEX                 0
#define NV_3D_BIND_GROUP_PIXEL                  4

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

/**
 * Program draw control: topology in bits 3:0, instance iterate when
 * instance_count > 1, and optionally ignore global base vertex/instance
 * (Vulkan draws pass base via SET_GLOBAL_BASE_* instead).
 */
static inline void
nv_3d_set_draw_control(struct nv_push *p, uint32_t topology_nv,
                       uint32_t instance_count, bool use_global_bases)
{
   uint32_t a = (topology_nv & 0xfu) | NVC597_SET_DRAW_CONTROL_A_INSTANCE_ID_FIRST;
   if (instance_count > 1)
      a |= NVC597_SET_DRAW_CONTROL_A_INSTANCE_ITERATE_ENABLE;
   if (use_global_bases)
      a |= NVC597_SET_DRAW_CONTROL_A_IGNORE_GLOBAL_BASE_VERTEX |
           NVC597_SET_DRAW_CONTROL_A_IGNORE_GLOBAL_BASE_INSTANCE;
   nv_push_method(p, NVC597_SET_DRAW_CONTROL_A, a);
   nv_push_method(p, NVC597_SET_DRAW_CONTROL_B,
                  instance_count ? instance_count : 1u);
   nv_push_method(p, NVC597_SET_INSTANCE_COUNT,
                  instance_count ? instance_count : 1u);
}

static inline void
nv_3d_set_global_base_vertex_instance(struct nv_push *p,
                                      int32_t base_vertex,
                                      uint32_t base_instance)
{
   nv_push_method(p, NVC597_SET_GLOBAL_BASE_VERTEX_INDEX, (uint32_t)base_vertex);
   nv_push_method(p, NVC597_SET_GLOBAL_BASE_INSTANCE_INDEX, base_instance);
   nv_push_method(p, NVC597_SET_VERTEX_ID_BASE, (uint32_t)base_vertex);
}

static inline void
nv_3d_set_primitive_topology(struct nv_push *p, uint32_t topology_nv)
{
   /* Separate topology state (override bit = use this method, not begin/end) */
   nv_push_method(p, NVC597_SET_PRIMITIVE_TOPOLOGY_CONTROL,
                  NVC597_SET_PRIMITIVE_TOPOLOGY_CONTROL_USE_SEPARATE);
   /* Vulkan/GL style values are 1-based in some modes; NVC597 topo 0-6 maps
    * directly for the common list/strip/fan set when stored in bits 3:0 of
    * draw control; primitive topology method uses 1=points..5=tristrip. */
   static const uint32_t topo_v[] = { 1, 2, 3, 4, 5, 6, 7 };
   uint32_t v = (topology_nv < 7) ? topo_v[topology_nv] : 4;
   nv_push_method(p, NVC597_SET_PRIMITIVE_TOPOLOGY, v);
}

static inline void
nv_3d_set_primitive_restart(struct nv_push *p, bool enable, uint32_t index)
{
   nv_push_method(p, NVC597_SET_DA_PRIMITIVE_RESTART, enable ? 1u : 0u);
   if (enable)
      nv_push_method(p, NVC597_SET_DA_PRIMITIVE_RESTART_INDEX, index);
}

static inline void
nv_3d_emit_draw_vertex_array(struct nv_push *p, uint32_t start, uint32_t count)
{
   nv_push_method(p, NVC597_DRAW_VERTEX_ARRAY_BEGIN_END_A, start);
   nv_push_method(p, NVC597_DRAW_VERTEX_ARRAY_BEGIN_END_B, count);
}

/** Non-indexed draw with full instancing / base instance support. */
static inline void
nv_3d_emit_draw_vertex_array_instanced(struct nv_push *p,
                                       uint32_t topology_nv,
                                       uint32_t first_vertex,
                                       uint32_t vertex_count,
                                       uint32_t instance_count,
                                       uint32_t first_instance)
{
   nv_3d_set_global_base_vertex_instance(p, 0, first_instance);
   nv_3d_set_draw_control(p, topology_nv, instance_count, true);
   nv_3d_emit_draw_vertex_array(p, first_vertex, vertex_count);
}

static inline void
nv_3d_emit_draw_index_buffer(struct nv_push *p, uint32_t first, uint32_t count)
{
   nv_push_method(p, NVC597_DRAW_INDEX_BUFFER_BEGIN_END_A, first);
   nv_push_method(p, NVC597_DRAW_INDEX_BUFFER_BEGIN_END_B, count);
}

/** Indexed draw with vertexOffset / firstInstance / instanceCount. */
static inline void
nv_3d_emit_draw_index_buffer_instanced(struct nv_push *p,
                                       uint32_t topology_nv,
                                       uint32_t first_index,
                                       uint32_t index_count,
                                       int32_t vertex_offset,
                                       uint32_t instance_count,
                                       uint32_t first_instance)
{
   nv_3d_set_global_base_vertex_instance(p, vertex_offset, first_instance);
   nv_3d_set_draw_control(p, topology_nv, instance_count, true);
   nv_3d_emit_draw_index_buffer(p, first_index, index_count);
}

/** DrawAuto: vertex count derived from buffer byte size / stride (XFB-style). */
static inline void
nv_3d_emit_draw_auto(struct nv_push *p, uint32_t topology_nv,
                     uint32_t byte_count, uint32_t stride,
                     uint32_t instance_count)
{
   nv_3d_set_draw_control(p, topology_nv, instance_count, false);
   nv_push_method(p, NVC597_SET_DRAW_AUTO_STRIDE, stride & 0xfffu);
   nv_push_method(p, NVC597_SET_DRAW_AUTO_START, byte_count);
}

/** Map pipe_format (numeric, see p_format.h) to vertex component bit-width code. */
static inline uint32_t
nv_3d_vertex_comp_from_pipe(unsigned pipe_fmt)
{
   switch (pipe_fmt) {
   case 1:  /* B8G8R8A8_UNORM */
   case 9:  /* R8G8B8A8_UNORM */
   case 3:  /* A8R8G8B8_UNORM */
      return NVC597_SET_VERTEX_ATTRIBUTE_A_COMPONENT_BIT_WIDTHS_R8_G8_B8_A8;
   case 31: /* R32_FLOAT */
   case 32: /* R32_UINT */
   case 33: /* R32_SINT */
      return NVC597_SET_VERTEX_ATTRIBUTE_A_COMPONENT_BIT_WIDTHS_R32;
   case 34: /* R32G32_FLOAT */
   case 35: /* R32G32_UINT */
      return NVC597_SET_VERTEX_ATTRIBUTE_A_COMPONENT_BIT_WIDTHS_R32_G32;
   case 36: /* R32G32B32_FLOAT */
      return NVC597_SET_VERTEX_ATTRIBUTE_A_COMPONENT_BIT_WIDTHS_R32_G32_B32;
   case 39: /* R32G32B32A32_FLOAT */
   case 40: /* R32G32B32A32_UINT */
      return NVC597_SET_VERTEX_ATTRIBUTE_A_COMPONENT_BIT_WIDTHS_R32_G32_B32_A32;
   case 12: /* R16G16_UNORM / similar */
   case 13:
      return NVC597_SET_VERTEX_ATTRIBUTE_A_COMPONENT_BIT_WIDTHS_R16_G16;
   case 14: /* R16G16B16A16 */
   case 15:
      return NVC597_SET_VERTEX_ATTRIBUTE_A_COMPONENT_BIT_WIDTHS_R16_G16_B16_A16;
   case 6:  /* R8G8_UNORM */
   case 7:
      return NVC597_SET_VERTEX_ATTRIBUTE_A_COMPONENT_BIT_WIDTHS_R8_G8;
   default:
      return NVC597_SET_VERTEX_ATTRIBUTE_A_COMPONENT_BIT_WIDTHS_R32_G32_B32;
   }
}

/** SPA version (affects simulators; required by nvidia-3d channel init). */
static inline void
nv_3d_set_spa_version(struct nv_push *p, uint32_t major, uint32_t minor)
{
   nv_push_method(p, NVC597_SET_SPA_VERSION,
                  (minor & 0xff) | ((major & 0xff) << 8));
}

/**
 * Program region base address.  On modern HW this may be optional when
 * SET_PIPELINE_PROGRAM_ADDRESS carries absolute VAs; still emitted for
 * compatibility with Fermi-era channel init patterns.
 */
static inline void
nv_3d_set_program_region(struct nv_push *p, uint64_t gpu_addr)
{
   nv_push_method(p, NVC597_SET_PROGRAM_REGION_A,
                  (uint32_t)(gpu_addr >> 32) & 0xff);
   nv_push_method(p, NVC597_SET_PROGRAM_REGION_B,
                  (uint32_t)(gpu_addr & 0xffffffffu));
}

/**
 * Load a pipeline shader stage (nv3dLoadProgram): enable+type, program address,
 * register count, bind group.
 *
 * stage: NV_3D_PIPE_STAGE_*
 * type:  NVC597_SET_PIPELINE_SHADER_TYPE_*
 * program_addr: absolute GPU VA of shader header/code entry
 */
static inline void
nv_3d_load_pipeline_shader(struct nv_push *p, unsigned stage, unsigned type,
                           uint64_t program_addr, uint32_t register_count,
                           uint32_t bind_group)
{
   uint32_t shader_word = NVC597_SET_PIPELINE_SHADER_ENABLE_TRUE |
                          ((type & 0xf) << NVC597_SET_PIPELINE_SHADER_TYPE_SHIFT);

   nv_push_method(p, NVC597_SET_PIPELINE_SHADER(stage), shader_word);
   nv_push_method(p, NVC597_SET_PIPELINE_PROGRAM_ADDRESS_A(stage),
                  (uint32_t)(program_addr >> 32) & 0xff);
   nv_push_method(p, NVC597_SET_PIPELINE_PROGRAM_ADDRESS_B(stage),
                  (uint32_t)(program_addr & 0xffffffffu));
   nv_push_method(p, NVC597_SET_PIPELINE_REGISTER_COUNT(stage),
                  register_count & 0x1ff);
   nv_push_method(p, NVC597_SET_PIPELINE_BINDING(stage),
                  bind_group & 0x7);
}

/** Disable a pipeline stage (e.g. unused geometry). */
static inline void
nv_3d_disable_pipeline_shader(struct nv_push *p, unsigned stage)
{
   nv_push_method(p, NVC597_SET_PIPELINE_SHADER(stage),
                  NVC597_SET_PIPELINE_SHADER_ENABLE_FALSE);
}

/** Select constant buffer for subsequent BIND_GROUP_CONSTANT_BUFFER. */
static inline void
nv_3d_set_constant_buffer_selector(struct nv_push *p, uint32_t size_bytes,
                                   uint64_t gpu_addr)
{
   nv_push_method(p, NVC597_SET_CONSTANT_BUFFER_SELECTOR_A, size_bytes & 0x1ffff);
   nv_push_method(p, NVC597_SET_CONSTANT_BUFFER_SELECTOR_B,
                  (uint32_t)(gpu_addr >> 32) & 0xff);
   nv_push_method(p, NVC597_SET_CONSTANT_BUFFER_SELECTOR_C,
                  (uint32_t)(gpu_addr & 0xffffffffu));
}

/** Bind selected CB to bind_group / shader_slot (nvidia-3d nv3dBindCb). */
static inline void
nv_3d_bind_group_constant_buffer(struct nv_push *p, unsigned bind_group,
                                 unsigned shader_slot, bool valid)
{
   uint32_t v = (valid ? NVC597_BIND_GROUP_CONSTANT_BUFFER_VALID_TRUE
                       : NVC597_BIND_GROUP_CONSTANT_BUFFER_VALID_FALSE) |
                ((shader_slot & 0x1f) << NVC597_BIND_GROUP_CONSTANT_BUFFER_SHADER_SLOT_SHIFT);
   nv_push_method(p, NVC597_BIND_GROUP_CONSTANT_BUFFER(bind_group), v);
}

/**
 * 3D report semaphore release (writes payload when pipeline reaches location).
 * nvidia-3d / nvkms uses PIPELINE_LOCATION_ALL + RELEASE + ONE_WORD/FOUR_WORDS.
 */
static inline void
nv_3d_report_semaphore_release(struct nv_push *p, uint64_t sema_gpu_addr,
                               uint32_t payload, bool one_word)
{
   uint32_t d = NVC597_SET_REPORT_SEMAPHORE_D_OPERATION_RELEASE |
                NVC597_SET_REPORT_SEMAPHORE_D_RELEASE_AFTER_WRITES |
                NVC597_SET_REPORT_SEMAPHORE_D_PIPELINE_LOCATION_ALL |
                NVC597_SET_REPORT_SEMAPHORE_D_FLUSH_DISABLE_TRUE |
                (one_word ? NVC597_SET_REPORT_SEMAPHORE_D_STRUCTURE_SIZE_ONE_WORD
                          : NVC597_SET_REPORT_SEMAPHORE_D_STRUCTURE_SIZE_FOUR_WORDS);

   nv_push_method(p, NVC597_SET_REPORT_SEMAPHORE_A,
                  (uint32_t)(sema_gpu_addr >> 32) & 0xff);
   nv_push_method(p, NVC597_SET_REPORT_SEMAPHORE_B,
                  (uint32_t)(sema_gpu_addr & 0xffffffffu));
   nv_push_method(p, NVC597_SET_REPORT_SEMAPHORE_C, payload);
   nv_push_method(p, NVC597_SET_REPORT_SEMAPHORE_D, d);
}

/** Host channel semaphore release (NVC36F_SEMAPHORE*, any subchannel object). */
static inline void
nv_push_host_semaphore_release(struct nv_push *p, uint64_t sema_gpu_addr,
                               uint32_t payload)
{
   /* Offset must be 4-byte aligned; SEMAPHOREB stores bits 31:2 */
   nv_push_method(p, NVC36F_SEMAPHOREA,
                  (uint32_t)(sema_gpu_addr >> 32) & 0xff);
   nv_push_method(p, NVC36F_SEMAPHOREB,
                  (uint32_t)((sema_gpu_addr >> 2) & 0x3fffffffu));
   nv_push_method(p, NVC36F_SEMAPHOREC, payload);
   nv_push_method(p, NVC36F_SEMAPHORED,
                  NVC36F_SEMAPHORED_OPERATION_RELEASE |
                  NVC36F_SEMAPHORED_RELEASE_WFI_DIS |
                  NVC36F_SEMAPHORED_RELEASE_SIZE_4BYTE);
}

/** One-time channel defaults (subset of nvidia-3d fermi initChannel). */
static inline void
nv_3d_emit_channel_init_defaults(struct nv_push *p, uint32_t spa_major,
                                 uint32_t spa_minor, uint32_t sph_cur,
                                 uint32_t sph_oldest)
{
   nv_3d_set_spa_version(p, spa_major, spa_minor);
   nv_push_method(p, NVC597_SET_SPH_VERSION,
                  (sph_cur & 0xffff) | ((sph_oldest & 0xffff) << 16));
   nv_push_method(p, NVC597_SET_WINDOW_ORIGIN,
                  NVC597_SET_WINDOW_ORIGIN_MODE_UPPER_LEFT |
                  NVC597_SET_WINDOW_ORIGIN_FLIP_Y_TRUE);
   nv_push_method(p, NVC597_SET_ZCULL_BOUNDS, 0);
   nv_push_method(p, NVC597_SET_RASTER_ENABLE, 1);
   nv_push_method(p, NVC597_SET_SHADE_MODE, NVC597_SET_SHADE_MODE_V_OGL_SMOOTH);
   nv_push_method(p, NVC597_SET_FILL_MODE, NVC597_SET_FILL_MODE_V_SOLID);
}

/** Map pipe_compare_func (0..7) to OGL depth/stencil func value. */
static inline uint32_t
nv_3d_ogl_cmp_func(unsigned pipe_func)
{
   static const uint32_t tbl[8] = {
      NVC597_SET_DEPTH_FUNC_V_OGL_NEVER,
      NVC597_SET_DEPTH_FUNC_V_OGL_LESS,
      NVC597_SET_DEPTH_FUNC_V_OGL_EQUAL,
      NVC597_SET_DEPTH_FUNC_V_OGL_LEQUAL,
      NVC597_SET_DEPTH_FUNC_V_OGL_GREATER,
      NVC597_SET_DEPTH_FUNC_V_OGL_NOTEQUAL,
      NVC597_SET_DEPTH_FUNC_V_OGL_GEQUAL,
      NVC597_SET_DEPTH_FUNC_V_OGL_ALWAYS,
   };
   return pipe_func < 8 ? tbl[pipe_func] : NVC597_SET_DEPTH_FUNC_V_OGL_LESS;
}

#define NVC597_SET_BLEND_COLOR_DEST_COEFF_V_OGL_DST_COLOR  0x4306

/** Map pipe_blendfactor (common subset) to OGL blend coeff. */
static inline uint32_t
nv_3d_ogl_blend_factor(unsigned pipe_factor, bool is_alpha)
{
   (void)is_alpha;
   switch (pipe_factor) {
   case 1:  return NVC597_SET_BLEND_COLOR_SOURCE_COEFF_V_OGL_ONE;   /* ONE */
   case 0:  return NVC597_SET_BLEND_COLOR_SOURCE_COEFF_V_OGL_ZERO;  /* ZERO */
   case 2:  return NVC597_SET_BLEND_COLOR_SOURCE_COEFF_V_OGL_SRC_ALPHA;
   case 3:  return NVC597_SET_BLEND_COLOR_SOURCE_COEFF_V_OGL_ONE_MINUS_SRC_ALPHA;
   case 4:  return NVC597_SET_BLEND_COLOR_DEST_COEFF_V_OGL_DST_COLOR;
   default: return NVC597_SET_BLEND_COLOR_SOURCE_COEFF_V_OGL_ONE;
   }
}

/**
 * Emit blend/ZSA/rasterizer from Gallium-like state (passed as opaque fields
 * to avoid requiring pipe headers in every TU).
 *
 * blend_enable: rt0 blend enable
 * rgb/alpha func & factors: pipe enum values (see p_defines.h)
 * colormask: 4-bit RGBA write mask for CT0
 * depth_enabled/writemask/func: depth state
 * stencil_enabled: front stencil only (simplified)
 * cull_face: 0=none, 1=front, 2=back, 3=front_and_back
 * front_ccw: non-zero => CCW front face
 * fill_mode: 0=fill, 1=line, 2=point
 * smooth_shade: non-zero => smooth, else flat
 */
static inline void
nv_3d_emit_blend_zsa_raster(struct nv_push *p,
                            bool blend_enable,
                            unsigned rgb_func, unsigned rgb_src, unsigned rgb_dst,
                            unsigned alpha_func, unsigned alpha_src, unsigned alpha_dst,
                            unsigned colormask,
                            bool depth_enabled, bool depth_writemask, unsigned depth_func,
                            bool stencil_enabled,
                            unsigned cull_face, bool front_ccw,
                            unsigned fill_mode, bool smooth_shade)
{
   uint32_t fill = NVC597_SET_FILL_MODE_V_SOLID;
   if (fill_mode == 1)
      fill = NVC597_SET_FILL_MODE_V_WIREFRAME;
   else if (fill_mode == 2)
      fill = NVC597_SET_FILL_MODE_V_POINT;

   nv_push_method(p, NVC597_SET_FILL_MODE, fill);
   nv_push_method(p, NVC597_SET_SHADE_MODE,
                  smooth_shade ? NVC597_SET_SHADE_MODE_V_OGL_SMOOTH
                               : NVC597_SET_SHADE_MODE_V_OGL_FLAT);
   nv_push_method(p, NVC597_OGL_SET_FRONT_FACE,
                  front_ccw ? NVC597_OGL_SET_FRONT_FACE_V_CCW
                            : NVC597_OGL_SET_FRONT_FACE_V_CW);
   if (cull_face) {
      uint32_t dir = NVC597_OGL_SET_CULL_FACE_V_BACK;
      if (cull_face == 1)
         dir = NVC597_OGL_SET_CULL_FACE_V_FRONT;
      else if (cull_face == 3)
         dir = NVC597_OGL_SET_CULL_FACE_V_FRONT_AND_BACK;
      nv_push_method(p, NVC597_OGL_SET_CULL, 1);
      nv_push_method(p, NVC597_OGL_SET_CULL_FACE, dir);
   } else {
      nv_push_method(p, NVC597_OGL_SET_CULL, 0);
   }

   nv_push_method(p, NVC597_SET_DEPTH_TEST, depth_enabled ? 1 : 0);
   nv_push_method(p, NVC597_SET_DEPTH_WRITE, depth_writemask ? 1 : 0);
   nv_push_method(p, NVC597_SET_DEPTH_FUNC, nv_3d_ogl_cmp_func(depth_func));

   nv_push_method(p, NVC597_SET_STENCIL_TEST, stencil_enabled ? 1 : 0);

   nv_push_method(p, NVC597_SET_BLEND(0), blend_enable ? 1 : 0);
   if (blend_enable) {
      nv_push_method(p, NVC597_SET_BLEND_SEPARATE_FOR_ALPHA, 1);
      nv_push_method(p, NVC597_SET_BLEND_COLOR_OP, NVC597_SET_BLEND_COLOR_OP_V_OGL_FUNC_ADD);
      nv_push_method(p, NVC597_SET_BLEND_COLOR_SOURCE_COEFF,
                     nv_3d_ogl_blend_factor(rgb_src, false));
      nv_push_method(p, NVC597_SET_BLEND_COLOR_DEST_COEFF,
                     nv_3d_ogl_blend_factor(rgb_dst, false));
      nv_push_method(p, NVC597_SET_BLEND_ALPHA_OP, NVC597_SET_BLEND_COLOR_OP_V_OGL_FUNC_ADD);
      nv_push_method(p, NVC597_SET_BLEND_ALPHA_SOURCE_COEFF,
                     nv_3d_ogl_blend_factor(alpha_src, true));
      nv_push_method(p, NVC597_SET_BLEND_ALPHA_DEST_COEFF,
                     nv_3d_ogl_blend_factor(alpha_dst, true));
      (void)rgb_func;
      (void)alpha_func;
   }

   /* CT write mask: low 4 bits RGBA */
   nv_push_method(p, NVC597_SET_CT_WRITE(0), colormask & 0xf);
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
