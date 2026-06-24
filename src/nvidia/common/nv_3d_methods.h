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

/* Stream output (transform feedback) — offsets stable Maxwell..Ada (clce97/clc597) */
#define NVC597_SET_STREAM_OUT_BUFFER_ENABLE(j)              (0x0380 + (j) * 32)
#define NVC597_SET_STREAM_OUT_BUFFER_ENABLE_V_FALSE         0x0
#define NVC597_SET_STREAM_OUT_BUFFER_ENABLE_V_TRUE          0x1
#define NVC597_SET_STREAM_OUT_BUFFER_ADDRESS_A(j)           (0x0384 + (j) * 32)
#define NVC597_SET_STREAM_OUT_BUFFER_ADDRESS_B(j)           (0x0388 + (j) * 32)
#define NVC597_SET_STREAM_OUT_BUFFER_SIZE(j)                (0x038c + (j) * 32)
#define NVC597_SET_STREAM_OUT_BUFFER_LOAD_WRITE_POINTER(j)  (0x0390 + (j) * 32)
#define NVC597_SET_STREAM_OUT_CONTROL_STREAM(j)             (0x0700 + (j) * 16)
#define NVC597_SET_STREAM_OUT_CONTROL_COMPONENT_COUNT(j)    (0x0704 + (j) * 16)
#define NVC597_SET_STREAM_OUT_CONTROL_STRIDE(j)             (0x0708 + (j) * 16)
#define NVC597_SET_STREAM_OUTPUT                            0x0744
#define NVC597_SET_STREAM_OUTPUT_ENABLE_FALSE               0x0
#define NVC597_SET_STREAM_OUTPUT_ENABLE_TRUE                0x1
/* layout select: stream i, dword j packs 4 attribute numbers (byte each) */
#define NVC597_SET_STREAM_OUT_LAYOUT_SELECT(i, j)           (0x2800 + (i) * 128 + (j) * 4)
#define NVC597_SET_MAX_STREAM_OUTPUT_GS_INSTANCES_PER_TASK  0x0d60

/* Tessellation domain/spacing/output (clce97; offsets stable through Ada) */
#define NVC597_SET_TESSELLATION_PARAMETERS              0x0320
#define NVC597_SET_TESSELLATION_PARAMETERS_DOMAIN_ISOLINE       0x0
#define NVC597_SET_TESSELLATION_PARAMETERS_DOMAIN_TRIANGLE      0x1
#define NVC597_SET_TESSELLATION_PARAMETERS_DOMAIN_QUAD          0x2
#define NVC597_SET_TESSELLATION_PARAMETERS_SPACING_INTEGER      0x0
#define NVC597_SET_TESSELLATION_PARAMETERS_SPACING_FRAC_ODD     0x1
#define NVC597_SET_TESSELLATION_PARAMETERS_SPACING_FRAC_EVEN    0x2
#define NVC597_SET_TESSELLATION_PARAMETERS_OUTPUT_POINTS        0x0
#define NVC597_SET_TESSELLATION_PARAMETERS_OUTPUT_LINES         0x1
#define NVC597_SET_TESSELLATION_PARAMETERS_OUTPUT_TRI_CW        0x2
#define NVC597_SET_TESSELLATION_PARAMETERS_OUTPUT_TRI_CCW       0x3
#define NVC597_SET_TESSELLATION_LOD_U0_OR_DENSITY        0x0324
#define NVC597_SET_TESSELLATION_LOD_V0_OR_DETAIL         0x0328
#define NVC597_SET_TESSELLATION_LOD_U1_OR_W0             0x032c
#define NVC597_SET_TESSELLATION_LOD_V1                   0x0330
#define NVC597_SET_TESSELLATION_CUT_HEIGHT               0x1008

/* Geometry shader output topology / max vertices (subset) */
#define NVC597_SET_GS_OUTPUT_PRIMITIVE_TOPOLOGY         0x0a0c
#define NVC597_SET_GS_MAX_OUTPUT_VERTEX_COUNT           0x0a10

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
#define NVC597_SET_COLOR_TARGET_FORMAT_V_R8          0xF8
#define NVC597_SET_COLOR_TARGET_FORMAT_V_R16         0xC2
#define NVC597_SET_COLOR_TARGET_FORMAT_V_R16_G16     0xC3
#define NVC597_SET_COLOR_TARGET_FORMAT_V_RF32_GF32   0xC4
#define NVC597_SET_COLOR_TARGET_FORMAT_V_A2B10G10R10 0xDF
#define NVC597_SET_COLOR_TARGET_FORMAT_V_RF11_GF11_BF10 0xE6
#define NVC597_SET_COLOR_TARGET_FORMAT_V_R8_G8       0xF5
#define NVC597_SET_COLOR_TARGET_FORMAT_V_R8_G8_B8_A8 0xF7
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

/* Viewport clip volume (clc597/cl9097): per-viewport XY clip + Z range */
#define NVC597_SET_VIEWPORT_CLIP_HORIZONTAL(j)  (0x0c00 + (j) * 16)
#define NVC597_SET_VIEWPORT_CLIP_VERTICAL(j)    (0x0c04 + (j) * 16)
#define NVC597_SET_VIEWPORT_CLIP_MIN_Z(j)       (0x0c08 + (j) * 16)
#define NVC597_SET_VIEWPORT_CLIP_MAX_Z(j)       (0x0c0c + (j) * 16)
#define NVC597_SET_VIEWPORT_Z_CLIP              0x0d7c
#define NVC597_SET_VIEWPORT_Z_CLIP_RANGE_NEGATIVE_W_TO_POSITIVE_W 0x0
#define NVC597_SET_VIEWPORT_Z_CLIP_RANGE_ZERO_TO_POSITIVE_W       0x1
/* SET_VIEWPORT_CLIP_CONTROL @ 0x193c — depth clamp vs clip at pixel Z */
#define NVC597_SET_VIEWPORT_CLIP_CONTROL        0x193c
#define NVC597_SET_VIEWPORT_CLIP_CONTROL_MIN_Z_ZERO_MAX_Z_ONE_TRUE  (1u << 0)
#define NVC597_SET_VIEWPORT_CLIP_CONTROL_PIXEL_MIN_Z_CLAMP          (1u << 3)
#define NVC597_SET_VIEWPORT_CLIP_CONTROL_PIXEL_MAX_Z_CLAMP          (1u << 4)
#define NVC597_SET_VIEWPORT_CLIP_CONTROL_Z_CLIP_RANGE_SHIFT         16
#define NVC597_SET_VIEWPORT_CLIP_CONTROL_Z_CLIP_RANGE_ZERO_ONE      (2u << 16)
#define NVC597_SET_VIEWPORT_CLIP_CONTROL_Z_CLIP_RANGE_MINUS_INF_PLUS_INF (3u << 16)

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
#define NVC597_CLEAR_SURFACE_MRT_SELECT_SHIFT       6
#define NVC597_CLEAR_SURFACE_MRT_SELECT_MASK        (7u << 6)

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
#define NVC597_LOAD_CONSTANT_BUFFER_OFFSET      0x238c
#define NVC597_LOAD_CONSTANT_BUFFER(i)          (0x2390 + (i) * 4)
#define NVC597_BIND_GROUP_CONSTANT_BUFFER(j)    (0x2410 + (j) * 32)
#define NVC597_BIND_GROUP_CONSTANT_BUFFER_VALID_TRUE  0x1
#define NVC597_BIND_GROUP_CONSTANT_BUFFER_VALID_FALSE 0x0
#define NVC597_BIND_GROUP_CONSTANT_BUFFER_SHADER_SLOT_SHIFT 4
/* Push constants conventionally use bind group vertex/pixel slot 0 (c[0] / bank 1) */
#define NV_3D_PUSH_CONST_SHADER_SLOT            0
#define NV_3D_PUSH_CONST_BIND_GROUP_VS          NV_3D_BIND_GROUP_VERTEX
#define NV_3D_PUSH_CONST_BIND_GROUP_FS          NV_3D_BIND_GROUP_PIXEL

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
/* ZPASS / query counter report modes (subset) */
#define NVC597_SET_REPORT_SEMAPHORE_D_REPORT_NONE           0
#define NVC597_SET_REPORT_SEMAPHORE_D_REPORT_ZPASS_PIXEL_CNT (1u << 5)
#define NVC597_SET_RENDER_ENABLE_A              0x1550
#define NVC597_SET_RENDER_ENABLE_B              0x1554
#define NVC597_SET_RENDER_ENABLE_C              0x1558
#define NVC597_SET_RENDER_ENABLE_C_MODE_FALSE              0x0
#define NVC597_SET_RENDER_ENABLE_C_MODE_TRUE               0x1
#define NVC597_SET_RENDER_ENABLE_C_MODE_CONDITIONAL        0x2 /* render if mem != 0 */
#define NVC597_SET_RENDER_ENABLE_C_MODE_RENDER_IF_EQUAL    0x3
#define NVC597_SET_RENDER_ENABLE_C_MODE_RENDER_IF_NOT_EQUAL 0x4
#define NVC597_SET_RENDER_ENABLE_OVERRIDE       0x1944
#define NVC597_SET_RENDER_ENABLE_OVERRIDE_MODE_ALWAYS_RENDER  0x0
#define NVC597_SET_RENDER_ENABLE_OVERRIDE_MODE_USE_RENDER_ENABLE 0x1
#define NVC597_SET_RENDER_ENABLE_OVERRIDE_MODE_ALWAYS_FALSE  0x2
#define NVC597_SET_ZPASS_PIXEL_COUNT             0x0d70
#define NVC597_SET_ZPASS_PIXEL_COUNT_ENABLE_TRUE  1
#define NVC597_SET_ZPASS_PIXEL_COUNT_ENABLE_FALSE 0
#define NVC597_SET_REPORT_SEMAPHORE_D_FLUSH_DISABLE_TRUE     (1u << 2)
/* Two-sided / back stencil (clc597.h) */
#define NVC597_SET_BACK_STENCIL_FUNC_REF        0x0f54
#define NVC597_SET_BACK_STENCIL_MASK            0x0f58
#define NVC597_SET_BACK_STENCIL_FUNC_MASK       0x0f5c
#define NVC597_SET_DEPTH_BOUNDS_MIN             0x0f9c
#define NVC597_SET_DEPTH_BOUNDS_MAX             0x0fa0
#define NVC597_SET_TWO_SIDED_STENCIL_TEST       0x1594
#define NVC597_SET_BACK_STENCIL_OP_FAIL         0x1598
#define NVC597_SET_BACK_STENCIL_OP_ZFAIL        0x159c
#define NVC597_SET_BACK_STENCIL_OP_ZPASS        0x15a0
#define NVC597_SET_BACK_STENCIL_FUNC            0x15a4
#define NVC597_SET_STENCIL_FUNC_MASK            0x1398
#define NVC597_SET_DEPTH_BOUNDS_TEST            0x19bc
/* Logic op enable is bit 0 of SET_LOGIC_OP method word; op value in full dword */
#define NVC597_SET_LOGIC_OP_ENABLE_TRUE         1u
#define NVC597_SET_LOGIC_OP_ENABLE_FALSE        0u

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
#define NVC597_SET_POLY_OFFSET_POINT            0x0db0
#define NVC597_SET_POLY_OFFSET_LINE             0x0db4
#define NVC597_SET_POLY_OFFSET_FILL             0x0db8
#define NVC597_SET_DEPTH_BIAS                   0x0dbc
#define NVC597_SET_SLOPE_SCALE_DEPTH_BIAS       0x0dc0
#define NVC597_SET_DEPTH_BIAS_CLAMP             0x0dc4
#define NVC597_SET_ANTI_ALIAS_ENABLE            0x0d58
#define NVC597_SET_ANTI_ALIAS_ALPHA_CONTROL     0x0d5c
#define NVC597_SET_SAMPLE_MASK_X0_Y0            0x0d60
#define NVC597_SET_SAMPLE_MASK_X1_Y0            0x0d64
#define NVC597_SET_SAMPLE_MASK_X0_Y1            0x0d68
#define NVC597_SET_SAMPLE_MASK_X1_Y1            0x0d6c
#define NVC597_SET_ANTI_ALIAS_SAMPLES           0x0d54
#define NVC597_SET_BLEND_CONST_RED              0x0d20
#define NVC597_SET_BLEND_CONST_GREEN            0x0d24
#define NVC597_SET_BLEND_CONST_BLUE             0x0d28
#define NVC597_SET_BLEND_CONST_ALPHA            0x0d2c
#define NVC597_SET_BLEND_ENABLE_COMMON          0x0d1c
/* NVC597_SET_LOGIC_OP / LOGIC_OP_ENABLE already defined above (0x19c4/0x19c8) */
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

/**
 * VkDrawIndirectCommand / pipe indirect non-indexed record (16 bytes):
 *   [0] vertexCount  [1] instanceCount  [2] firstVertex  [3] firstInstance
 * Emit one draw from a CPU-visible record pointer.
 */
static inline void
nv_3d_emit_draw_indirect_record(struct nv_push *p, uint32_t topology_nv,
                                const uint32_t rec[4])
{
   uint32_t vertex_count, instance_count, first_vertex, first_instance;
   if (!rec)
      return;
   vertex_count = rec[0];
   instance_count = rec[1] ? rec[1] : 1;
   first_vertex = rec[2];
   first_instance = rec[3];
   if (!vertex_count)
      return;
   nv_3d_emit_draw_vertex_array_instanced(p, topology_nv, first_vertex,
                                          vertex_count, instance_count,
                                          first_instance);
}

/**
 * VkDrawIndexedIndirectCommand (20 bytes):
 *   [0] indexCount  [1] instanceCount  [2] firstIndex
 *   [3] vertexOffset (int32)  [4] firstInstance
 */
static inline void
nv_3d_emit_draw_indexed_indirect_record(struct nv_push *p, uint32_t topology_nv,
                                        const uint32_t rec[5])
{
   uint32_t index_count, instance_count, first_index, first_instance;
   int32_t vertex_offset;
   if (!rec)
      return;
   index_count = rec[0];
   instance_count = rec[1] ? rec[1] : 1;
   first_index = rec[2];
   vertex_offset = (int32_t)rec[3];
   first_instance = rec[4];
   if (!index_count)
      return;
   nv_3d_set_global_base_vertex_instance(p, (uint32_t)vertex_offset,
                                         first_instance);
   nv_3d_set_draw_control(p, topology_nv, instance_count, true);
   nv_push_method(p, NVC597_DRAW_INDEX_BUFFER_BEGIN_END_A, first_index);
   nv_push_method(p, NVC597_DRAW_INDEX_BUFFER_BEGIN_END_B, index_count);
}

/** Emit up to draw_count indirect non-indexed draws from tightly/strided records */
static inline void
nv_3d_emit_draw_indirect_multi(struct nv_push *p, uint32_t topology_nv,
                               const uint32_t *base, uint32_t draw_count,
                               uint32_t stride_bytes)
{
   uint32_t d;
   uint32_t stride = stride_bytes ? stride_bytes : 16;
   if (!base || !draw_count)
      return;
   for (d = 0; d < draw_count; d++) {
      const uint32_t *rec = (const uint32_t *)((const uint8_t *)base +
                                               (size_t)d * stride);
      nv_3d_emit_draw_indirect_record(p, topology_nv, rec);
   }
}

static inline void
nv_3d_emit_draw_indexed_indirect_multi(struct nv_push *p, uint32_t topology_nv,
                                       const uint32_t *base, uint32_t draw_count,
                                       uint32_t stride_bytes)
{
   uint32_t d;
   uint32_t stride = stride_bytes ? stride_bytes : 20;
   if (!base || !draw_count)
      return;
   for (d = 0; d < draw_count; d++) {
      const uint32_t *rec = (const uint32_t *)((const uint8_t *)base +
                                               (size_t)d * stride);
      nv_3d_emit_draw_indexed_indirect_record(p, topology_nv, rec);
   }
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
 * Upload dwords into the currently selected constant buffer via
 * LOAD_CONSTANT_BUFFER_OFFSET + LOAD_CONSTANT_BUFFER(i) (clc597.h).
 * offset_bytes must be dword-aligned; count is number of uint32_t words.
 * Hardware accepts bursts; we emit in chunks of 16 dwords per method range.
 */
static inline void
nv_3d_load_constant_buffer_dwords(struct nv_push *p, uint32_t offset_bytes,
                                  const uint32_t *dwords, uint32_t count)
{
   uint32_t off = offset_bytes & 0xffffu;
   uint32_t i = 0;
   if (!p || !dwords || !count)
      return;
   while (i < count) {
      uint32_t chunk = count - i;
      uint32_t j;
      if (chunk > 16)
         chunk = 16;
      nv_push_method(p, NVC597_LOAD_CONSTANT_BUFFER_OFFSET, (off + i * 4u) & 0xffffu);
      for (j = 0; j < chunk; j++)
         nv_push_method(p, NVC597_LOAD_CONSTANT_BUFFER(j), dwords[i + j]);
      i += chunk;
   }
}

/**
 * Select CB at gpu_addr, upload dwords, bind to VS+FS push-const slots.
 * size_bytes is the CB allocation size (must be >= offset + count*4, 256-aligned preferred).
 */
static inline void
nv_3d_upload_and_bind_push_constants(struct nv_push *p, uint64_t cb_gpu_addr,
                                     uint32_t cb_size_bytes,
                                     uint32_t offset_bytes,
                                     const uint32_t *dwords, uint32_t count)
{
   uint32_t sel_size = cb_size_bytes ? cb_size_bytes : 256u;
   if (!p || !cb_gpu_addr)
      return;
   if (sel_size < 256u)
      sel_size = 256u;
   sel_size = (sel_size + 255u) & ~255u;
   nv_3d_set_constant_buffer_selector(p, sel_size, cb_gpu_addr);
   if (dwords && count)
      nv_3d_load_constant_buffer_dwords(p, offset_bytes, dwords, count);
   nv_3d_bind_group_constant_buffer(p, NV_3D_PUSH_CONST_BIND_GROUP_VS,
                                    NV_3D_PUSH_CONST_SHADER_SLOT, true);
   nv_3d_bind_group_constant_buffer(p, NV_3D_PUSH_CONST_BIND_GROUP_FS,
                                    NV_3D_PUSH_CONST_SHADER_SLOT, true);
}

/* VkDrawIndirectCommand / VkDrawIndexedIndirectCommand field offsets (bytes) */
#define NV_VK_DRAW_INDIRECT_VERTEX_COUNT_OFF      0
#define NV_VK_DRAW_INDIRECT_INSTANCE_COUNT_OFF    4
#define NV_VK_DRAW_INDIRECT_FIRST_VERTEX_OFF      8
#define NV_VK_DRAW_INDIRECT_FIRST_INSTANCE_OFF   12
#define NV_VK_DRAW_INDIRECT_STRIDE_DEFAULT       16

#define NV_VK_DRAWINDEXED_INDIRECT_INDEX_COUNT_OFF     0
#define NV_VK_DRAWINDEXED_INDIRECT_INSTANCE_COUNT_OFF  4
#define NV_VK_DRAWINDEXED_INDIRECT_FIRST_INDEX_OFF     8
#define NV_VK_DRAWINDEXED_INDIRECT_VERTEX_OFFSET_OFF  12
#define NV_VK_DRAWINDEXED_INDIRECT_FIRST_INSTANCE_OFF 16
#define NV_VK_DRAWINDEXED_INDIRECT_STRIDE_DEFAULT     20

/**
 * 3D report semaphore release (writes payload when pipeline reaches location).
 * nvidia-3d / nvkms uses PIPELINE_LOCATION_ALL + RELEASE + ONE_WORD/FOUR_WORDS.
 */

static inline void
nv_3d_emit_line_width(struct nv_push *p, float width)
{
   union { float f; uint32_t u; } w;
   w.f = width > 0.0f ? width : 1.0f;
   nv_push_method(p, NVC597_SET_LINE_WIDTH_FLOAT, w.u);
}

/** Viewport slot j (0..15): scale/offset from VkViewport-style params */
static inline void
nv_3d_set_viewport_n(struct nv_push *p, unsigned j,
                     float x, float y, float w, float h,
                     float min_z, float max_z)
{
   union { float f; uint32_t u; } sx, sy, sz, ox, oy, oz;
   unsigned slot = j & 15u;
   sx.f = w * 0.5f;
   sy.f = h * 0.5f;
   sz.f = (max_z - min_z) * 0.5f;
   ox.f = x + w * 0.5f;
   oy.f = y + h * 0.5f;
   oz.f = (max_z + min_z) * 0.5f;
   nv_push_method(p, NVC597_SET_VIEWPORT_SCALE_X(slot), sx.u);
   nv_push_method(p, NVC597_SET_VIEWPORT_SCALE_Y(slot), sy.u);
   nv_push_method(p, NVC597_SET_VIEWPORT_SCALE_Z(slot), sz.u);
   nv_push_method(p, NVC597_SET_VIEWPORT_OFFSET_X(slot), ox.u);
   nv_push_method(p, NVC597_SET_VIEWPORT_OFFSET_Y(slot), oy.u);
   nv_push_method(p, NVC597_SET_VIEWPORT_OFFSET_Z(slot), oz.u);
}

static inline void
nv_3d_set_scissor_n(struct nv_push *p, unsigned j,
                    int32_t x, int32_t y, uint32_t w, uint32_t h)
{
   unsigned slot = j & 15u;
   uint32_t xmin = (uint32_t)(x < 0 ? 0 : x);
   uint32_t ymin = (uint32_t)(y < 0 ? 0 : y);
   uint32_t xmax = xmin + (w ? w : 1);
   uint32_t ymax = ymin + (h ? h : 1);
   nv_push_method(p, NVC597_SET_SCISSOR_ENABLE(slot), 1);
   nv_push_method(p, NVC597_SET_SCISSOR_HORIZONTAL(slot),
                  (xmax << 16) | (xmin & 0xffffu));
   nv_push_method(p, NVC597_SET_SCISSOR_VERTICAL(slot),
                  (ymax << 16) | (ymin & 0xffffu));
}

/**
 * Conditional render / predication: point RENDER_ENABLE at a sema/report
 * memory word; render_c_mode is NVC597_SET_RENDER_ENABLE_C_MODE_*;
 * mode_override selects always/use/never render via OVERRIDE register.
 *
 * For normal cond-render (render when *addr != 0): CONDITIONAL + USE_RENDER_ENABLE.
 * For inverted (render when *addr == 0): RENDER_IF_EQUAL + USE_RENDER_ENABLE
 * with compare value 0 (hardware compares memory to 0 internally for IF_EQUAL).
 */
static inline void
nv_3d_set_render_enable_memory_ex(struct nv_push *p, uint64_t cond_gpu_addr,
                                  uint32_t render_c_mode,
                                  uint32_t mode_override)
{
   if (cond_gpu_addr) {
      nv_push_method(p, NVC597_SET_RENDER_ENABLE_A,
                     (uint32_t)(cond_gpu_addr >> 32) & 0xff);
      nv_push_method(p, NVC597_SET_RENDER_ENABLE_B,
                     (uint32_t)(cond_gpu_addr & 0xffffffffu));
      nv_push_method(p, NVC597_SET_RENDER_ENABLE_C, render_c_mode);
   }
   nv_push_method(p, NVC597_SET_RENDER_ENABLE_OVERRIDE, mode_override);
}

static inline void
nv_3d_set_render_enable_memory(struct nv_push *p, uint64_t cond_gpu_addr,
                               uint32_t mode_override)
{
   nv_3d_set_render_enable_memory_ex(p, cond_gpu_addr,
      cond_gpu_addr ? NVC597_SET_RENDER_ENABLE_C_MODE_CONDITIONAL : 0,
      mode_override);
}

/** Conditional rendering: normal = render if *addr != 0; inverted = if *addr == 0 */
static inline void
nv_3d_set_conditional_render(struct nv_push *p, uint64_t cond_gpu_addr,
                             bool inverted)
{
   if (!cond_gpu_addr) {
      nv_push_method(p, NVC597_SET_RENDER_ENABLE_OVERRIDE,
                     NVC597_SET_RENDER_ENABLE_OVERRIDE_MODE_ALWAYS_RENDER);
      return;
   }
   nv_3d_set_render_enable_memory_ex(p, cond_gpu_addr,
      inverted ? NVC597_SET_RENDER_ENABLE_C_MODE_RENDER_IF_EQUAL
               : NVC597_SET_RENDER_ENABLE_C_MODE_CONDITIONAL,
      NVC597_SET_RENDER_ENABLE_OVERRIDE_MODE_USE_RENDER_ENABLE);
}

static inline void
nv_3d_set_zpass_pixel_count(struct nv_push *p, bool enable)
{
   nv_push_method(p, NVC597_SET_ZPASS_PIXEL_COUNT,
                  enable ? NVC597_SET_ZPASS_PIXEL_COUNT_ENABLE_TRUE
                         : NVC597_SET_ZPASS_PIXEL_COUNT_ENABLE_FALSE);
}

/* ---- Cache / pipeline barriers (3D subchannel; mirrors host WFI + inv) ---- */

/** Invalidate shader I/D/C caches (post-shader-upload or SSBO/image writes). */
static inline void
nv_3d_invalidate_shader_caches(struct nv_push *p, bool instr, bool data, bool constant)
{
   uint32_t v = 0;
   if (instr)
      v |= NVC597_INVALIDATE_SHADER_CACHES_INSTRUCTION_TRUE;
   if (data)
      v |= NVC597_INVALIDATE_SHADER_CACHES_DATA_TRUE;
   if (constant)
      v |= NVC597_INVALIDATE_SHADER_CACHES_CONSTANT_TRUE;
   if (v)
      nv_push_method(p, NVC597_INVALIDATE_SHADER_CACHES, v);
}

/** Invalidate texture sampler/header/data caches (post-image write or layout transition). */
static inline void
nv_3d_invalidate_texture_caches(struct nv_push *p, bool sampler, bool header, bool data)
{
   if (sampler)
      nv_push_method(p, NVC597_INVALIDATE_SAMPLER_CACHE, 0);
   if (header)
      nv_push_method(p, NVC597_INVALIDATE_TEXTURE_HEADER_CACHE, 0);
   if (data)
      nv_push_method(p, NVC597_INVALIDATE_TEXTURE_DATA_CACHE, 0);
}

/**
 * Full conservative barrier: WFI (via caller or nv_push_wfi) then all invs.
 * Stage bits are coarse; proprietary driver often does similar full inv after
 * transfer/compute before sampling.
 */
static inline void
nv_3d_emit_memory_barrier(struct nv_push *p,
                          bool shader_instr, bool shader_data, bool shader_const,
                          bool tex_sampler, bool tex_header, bool tex_data)
{
   nv_3d_invalidate_shader_caches(p, shader_instr, shader_data, shader_const);
   nv_3d_invalidate_texture_caches(p, tex_sampler, tex_header, tex_data);
}

/**
 * Barrier from coarse access categories (closer to proprietary stage matrix).
 * Call after optional WFI; sets only the cache invs required by the transition.
 *
 * @param after_shader_write   src wrote shader storage / SSBO / image
 * @param after_transfer       src wrote via CE/copy/blit/clear
 * @param after_color_write    src wrote color attachment
 * @param after_depth_write    src wrote depth/stencil
 * @param after_xfb_write      src wrote transform feedback
 * @param before_shader_read   dst reads shader storage / UBO / indirect
 * @param before_tex_sample    dst samples textures / input attachments
 * @param before_shader_code   dst executes newly uploaded shader code
 */
static inline void
nv_3d_emit_barrier_from_access(struct nv_push *p,
                               bool after_shader_write,
                               bool after_transfer,
                               bool after_color_write,
                               bool after_depth_write,
                               bool after_xfb_write,
                               bool before_shader_read,
                               bool before_tex_sample,
                               bool before_shader_code)
{
   bool si = before_shader_code;
   bool sd = after_shader_write || after_xfb_write || before_shader_read ||
             after_transfer;
   bool sc = before_shader_read || before_shader_code;
   bool ts = before_tex_sample || after_color_write || after_depth_write ||
             after_transfer;
   bool th = ts;
   bool td = ts || after_color_write || after_depth_write || after_transfer;
   if (!si && !sd && !sc && !ts && !th && !td) {
      /* Execution-only: light data inv keeps later indirect/reads coherent */
      sd = sc = true;
   }
   nv_3d_emit_memory_barrier(p, si, sd, sc, ts, th, td);
}

/** Texture barrier only (GL textureBarrier / VK fragment-feedback loop). */
static inline void
nv_3d_emit_texture_barrier(struct nv_push *p)
{
   nv_3d_invalidate_texture_caches(p, true, true, true);
}

/** Program tessellation domain/spacing/output prim (domain 0=iso 1=tri 2=quad). */
static inline void
nv_3d_set_tessellation_parameters(struct nv_push *p, unsigned domain,
                                  unsigned spacing, unsigned output_prim)
{
   uint32_t v = (domain & 0x3) |
                ((spacing & 0x3) << 4) |
                ((output_prim & 0x3) << 8);
   nv_push_method(p, NVC597_SET_TESSELLATION_PARAMETERS, v);
}

/** Default outer/inner tess levels (float bits as uint32). */
static inline void
nv_3d_set_tessellation_lod(struct nv_push *p,
                           uint32_t lod_u0, uint32_t lod_v0,
                           uint32_t lod_u1, uint32_t lod_v1)
{
   nv_push_method(p, NVC597_SET_TESSELLATION_LOD_U0_OR_DENSITY, lod_u0);
   nv_push_method(p, NVC597_SET_TESSELLATION_LOD_V0_OR_DETAIL, lod_v0);
   nv_push_method(p, NVC597_SET_TESSELLATION_LOD_U1_OR_W0, lod_u1);
   nv_push_method(p, NVC597_SET_TESSELLATION_LOD_V1, lod_v1);
}

/** Enable tess with triangle domain, integer spacing, CCW tri output (common GL). */
static inline void
nv_3d_tess_enable_defaults(struct nv_push *p)
{
   const uint32_t one_f = 0x3f800000u; /* 1.0f */
   nv_3d_set_tessellation_parameters(p,
      NVC597_SET_TESSELLATION_PARAMETERS_DOMAIN_TRIANGLE,
      NVC597_SET_TESSELLATION_PARAMETERS_SPACING_INTEGER,
      NVC597_SET_TESSELLATION_PARAMETERS_OUTPUT_TRI_CCW);
   nv_3d_set_tessellation_lod(p, one_f, one_f, one_f, one_f);
   nv_push_method(p, NVC597_SET_TESSELLATION_CUT_HEIGHT, 0);
}

/** Geometry shader output topology (reuse NVC597_TOPOLOGY_* values) + max verts. */
static inline void
nv_3d_set_geometry_shader_output(struct nv_push *p, uint32_t topology_nv,
                                 uint32_t max_output_vertices)
{
   nv_push_method(p, NVC597_SET_GS_OUTPUT_PRIMITIVE_TOPOLOGY, topology_nv);
   if (max_output_vertices)
      nv_push_method(p, NVC597_SET_GS_MAX_OUTPUT_VERTEX_COUNT,
                     max_output_vertices & 0x3ff);
}

/** Disable conditional rendering (always render). */
static inline void
nv_3d_clear_conditional_render(struct nv_push *p)
{
   nv_push_method(p, NVC597_SET_RENDER_ENABLE_OVERRIDE,
                  NVC597_SET_RENDER_ENABLE_OVERRIDE_MODE_ALWAYS_RENDER);
}

/** Write occlusion (ZPASS) or timestamp-style report to sema addr (4 or 16 bytes). */
static inline void
nv_3d_report_query_release(struct nv_push *p, uint64_t sema_gpu_addr,
                           uint32_t payload, bool zpass_counter, bool one_word)
{
   uint32_t d = NVC597_SET_REPORT_SEMAPHORE_D_OPERATION_RELEASE |
                NVC597_SET_REPORT_SEMAPHORE_D_RELEASE_AFTER_WRITES |
                NVC597_SET_REPORT_SEMAPHORE_D_PIPELINE_LOCATION_ALL |
                NVC597_SET_REPORT_SEMAPHORE_D_FLUSH_DISABLE_TRUE |
                (one_word ? NVC597_SET_REPORT_SEMAPHORE_D_STRUCTURE_SIZE_ONE_WORD
                          : NVC597_SET_REPORT_SEMAPHORE_D_STRUCTURE_SIZE_FOUR_WORDS);
   if (zpass_counter)
      d |= NVC597_SET_REPORT_SEMAPHORE_D_REPORT_ZPASS_PIXEL_CNT;
   nv_push_method(p, NVC597_SET_REPORT_SEMAPHORE_A,
                  (uint32_t)(sema_gpu_addr >> 32) & 0xff);
   nv_push_method(p, NVC597_SET_REPORT_SEMAPHORE_B,
                  (uint32_t)(sema_gpu_addr & 0xffffffffu));
   nv_push_method(p, NVC597_SET_REPORT_SEMAPHORE_C, payload);
   nv_push_method(p, NVC597_SET_REPORT_SEMAPHORE_D, d);
}

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

/** 3D report semaphore acquire: stall until memory at sema_gpu_addr == payload. */
static inline void
nv_3d_report_semaphore_acquire(struct nv_push *p, uint64_t sema_gpu_addr,
                               uint32_t payload, bool one_word)
{
   uint32_t d = NVC597_SET_REPORT_SEMAPHORE_D_OPERATION_ACQUIRE |
                NVC597_SET_REPORT_SEMAPHORE_D_PIPELINE_LOCATION_ALL |
                (one_word ? NVC597_SET_REPORT_SEMAPHORE_D_STRUCTURE_SIZE_ONE_WORD
                          : NVC597_SET_REPORT_SEMAPHORE_D_STRUCTURE_SIZE_FOUR_WORDS);
   nv_push_method(p, NVC597_SET_REPORT_SEMAPHORE_A,
                  (uint32_t)(sema_gpu_addr >> 32) & 0xff);
   nv_push_method(p, NVC597_SET_REPORT_SEMAPHORE_B,
                  (uint32_t)(sema_gpu_addr & 0xffffffffu));
   nv_push_method(p, NVC597_SET_REPORT_SEMAPHORE_C, payload);
   nv_push_method(p, NVC597_SET_REPORT_SEMAPHORE_D, d);
}

/* ---- Stream output (transform feedback) ---- */

/** Bind or unbind SO buffer slot j (0..3). size_bytes 0 disables. */
static inline void
nv_3d_set_stream_out_buffer(struct nv_push *p, unsigned j, uint64_t gpu_addr,
                            uint32_t size_bytes)
{
   if (j >= 4)
      return;
   if (!size_bytes || !gpu_addr) {
      nv_push_method(p, NVC597_SET_STREAM_OUT_BUFFER_ENABLE(j),
                     NVC597_SET_STREAM_OUT_BUFFER_ENABLE_V_FALSE);
      return;
   }
   nv_push_method(p, NVC597_SET_STREAM_OUT_BUFFER_ADDRESS_A(j),
                  (uint32_t)(gpu_addr >> 32) & 0xff);
   nv_push_method(p, NVC597_SET_STREAM_OUT_BUFFER_ADDRESS_B(j),
                  (uint32_t)(gpu_addr & 0xffffffffu));
   nv_push_method(p, NVC597_SET_STREAM_OUT_BUFFER_SIZE(j), size_bytes);
   nv_push_method(p, NVC597_SET_STREAM_OUT_BUFFER_ENABLE(j),
                  NVC597_SET_STREAM_OUT_BUFFER_ENABLE_V_TRUE);
}

/** Reset write pointer for SO buffer j (byte offset into the buffer). */
static inline void
nv_3d_set_stream_out_buffer_write_pointer(struct nv_push *p, unsigned j,
                                          uint32_t start_offset_bytes)
{
   if (j >= 4)
      return;
   nv_push_method(p, NVC597_SET_STREAM_OUT_BUFFER_LOAD_WRITE_POINTER(j),
                  start_offset_bytes);
}

/**
 * Program SO control for buffer slot b: which GS/VS stream, how many components,
 * and output stride in bytes.
 */
static inline void
nv_3d_set_stream_out_control(struct nv_push *p, unsigned b, unsigned stream_select,
                             unsigned component_count, uint32_t stride_bytes)
{
   if (b >= 4)
      return;
   nv_push_method(p, NVC597_SET_STREAM_OUT_CONTROL_STREAM(b),
                  stream_select & 0x3);
   nv_push_method(p, NVC597_SET_STREAM_OUT_CONTROL_COMPONENT_COUNT(b),
                  component_count & 0xff);
   nv_push_method(p, NVC597_SET_STREAM_OUT_CONTROL_STRIDE(b), stride_bytes);
}

/**
 * Emit one layout-select dword: four attribute numbers (8-bit each).
 * dword_index is 0..31 per stream buffer; attrs[4] may include 0xff for holes.
 */
static inline void
nv_3d_set_stream_out_layout_dword(struct nv_push *p, unsigned stream_buf,
                                  unsigned dword_index, const uint8_t attrs[4])
{
   uint32_t v;
   if (stream_buf >= 4 || dword_index >= 32)
      return;
   v = (uint32_t)attrs[0] |
       ((uint32_t)attrs[1] << 8) |
       ((uint32_t)attrs[2] << 16) |
       ((uint32_t)attrs[3] << 24);
   nv_push_method(p, NVC597_SET_STREAM_OUT_LAYOUT_SELECT(stream_buf, dword_index), v);
}

/** Global SO enable/disable (after buffers/control/layout programmed). */
static inline void
nv_3d_set_stream_output_enable(struct nv_push *p, bool enable)
{
   nv_push_method(p, NVC597_SET_STREAM_OUTPUT,
                  enable ? NVC597_SET_STREAM_OUTPUT_ENABLE_TRUE
                         : NVC597_SET_STREAM_OUTPUT_ENABLE_FALSE);
}

/** Disable all four SO buffers and turn off stream output. */
static inline void
nv_3d_stream_out_disable_all(struct nv_push *p)
{
   unsigned j;
   nv_3d_set_stream_output_enable(p, false);
   for (j = 0; j < 4; j++)
      nv_push_method(p, NVC597_SET_STREAM_OUT_BUFFER_ENABLE(j),
                     NVC597_SET_STREAM_OUT_BUFFER_ENABLE_V_FALSE);
}

/**
 * Program a simple interleaved SO layout for buffer 0: sequential attributes
 * 0..attr_count-1, stride = attr_count * 4 (float/dword components).
 * Suitable for Gallium set_stream_output_targets when shader xfb is minimal.
 */
static inline void
nv_3d_stream_out_setup_simple(struct nv_push *p, unsigned buf_idx,
                              uint64_t gpu_addr, uint32_t size_bytes,
                              unsigned stream_select, unsigned attr_count,
                              uint32_t stride_bytes)
{
   unsigned dwords, di, ai;
   uint8_t attrs[4];

   if (buf_idx >= 4)
      return;
   if (!gpu_addr || !size_bytes || !attr_count) {
      nv_3d_set_stream_out_buffer(p, buf_idx, 0, 0);
      return;
   }
   if (!stride_bytes)
      stride_bytes = attr_count * 4u;
   nv_3d_set_stream_out_buffer(p, buf_idx, gpu_addr, size_bytes);
   nv_3d_set_stream_out_buffer_write_pointer(p, buf_idx, 0);
   nv_3d_set_stream_out_control(p, buf_idx, stream_select, attr_count, stride_bytes);

   dwords = (attr_count + 3u) / 4u;
   if (dwords > 32)
      dwords = 32;
   ai = 0;
   for (di = 0; di < dwords; di++) {
      unsigned k;
      for (k = 0; k < 4; k++) {
         if (ai < attr_count)
            attrs[k] = (uint8_t)(ai++);
         else
            attrs[k] = 0xff;
      }
      nv_3d_set_stream_out_layout_dword(p, buf_idx, di, attrs);
   }
}

/**
 * Emit full SO layout from Gallium pipe_stream_output_info (per-buffer).
 * strides[] are in bytes; attrs are register_index (8-bit) per component.
 */
static inline void
nv_3d_stream_out_emit_from_pipe_info(struct nv_push *p,
                                     const uint8_t *output_register,
                                     const uint8_t *output_buffer,
                                     const uint8_t *output_dst_offset,
                                     const uint8_t *output_num_components,
                                     const uint8_t *output_stream,
                                     unsigned num_outputs,
                                     const unsigned *strides,
                                     unsigned num_buffers,
                                     const uint64_t *buf_addrs,
                                     const uint32_t *buf_sizes,
                                     unsigned append_mask)
{
   unsigned b, o, k;
   unsigned comp_count[4] = { 0, 0, 0, 0 };
   uint8_t layout[4][128];
   unsigned layout_len[4] = { 0, 0, 0, 0 };

   memset(layout, 0xff, sizeof(layout));

   for (o = 0; o < num_outputs && o < 128; o++) {
      unsigned buf = output_buffer ? output_buffer[o] : 0;
      unsigned ncomp = output_num_components ? output_num_components[o] : 1;
      unsigned reg = output_register ? output_register[o] : o;
      unsigned stream = output_stream ? output_stream[o] : 0;
      unsigned dst_off = output_dst_offset ? output_dst_offset[o] : 0;
      unsigned slot_base;
      (void)stream;
      if (buf >= 4 || ncomp == 0)
         continue;
      slot_base = dst_off;
      for (k = 0; k < ncomp && k < 4; k++) {
         unsigned slot = slot_base + k;
         if (slot < 128)
            layout[buf][slot] = (uint8_t)(reg + k);
      }
      if (slot_base + ncomp > comp_count[buf])
         comp_count[buf] = slot_base + ncomp;
      if (slot_base + ncomp > layout_len[buf])
         layout_len[buf] = slot_base + ncomp;
   }

   for (b = 0; b < 4 && b < num_buffers; b++) {
      uint32_t stride = strides ? (uint32_t)strides[b] : 0;
      uint64_t addr = buf_addrs ? buf_addrs[b] : 0;
      uint32_t sz = buf_sizes ? buf_sizes[b] : 0;
      unsigned dwords, di2;
      uint8_t attrs[4];

      if (!addr || !sz) {
         nv_3d_set_stream_out_buffer(p, b, 0, 0);
         continue;
      }
      if (!stride && comp_count[b])
         stride = comp_count[b] * 4u;
      if (!stride)
         stride = 16;
      if (!comp_count[b])
         comp_count[b] = stride / 4u;
      if (!comp_count[b])
         comp_count[b] = 4;

      nv_3d_set_stream_out_buffer(p, b, addr, sz);
      if (!(append_mask & (1u << b)))
         nv_3d_set_stream_out_buffer_write_pointer(p, b, 0);
      nv_3d_set_stream_out_control(p, b, 0, comp_count[b] & 0xff, stride);

      dwords = (layout_len[b] + 3u) / 4u;
      if (!dwords)
         dwords = (comp_count[b] + 3u) / 4u;
      if (dwords > 32)
         dwords = 32;
      for (di2 = 0; di2 < dwords; di2++) {
         for (k = 0; k < 4; k++) {
            unsigned idx = di2 * 4 + k;
            if (idx < layout_len[b] && layout[b][idx] != 0xff)
               attrs[k] = layout[b][idx];
            else if (idx < comp_count[b])
               attrs[k] = (uint8_t)idx;
            else
               attrs[k] = 0xff;
         }
         nv_3d_set_stream_out_layout_dword(p, b, di2, attrs);
      }
   }
   for (; b < 4; b++)
      nv_3d_set_stream_out_buffer(p, b, 0, 0);
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

/** Stencil op: map Vulkan/GL style 0..7 to NVC597 D3D values (KEEP=1, ZERO=2, REPLACE=3, INCR=4, DECR=5, INVERT=6, INCR_WRAP=7, DECR_WRAP=8) */
static inline uint32_t
nv_3d_ogl_stencil_op(unsigned op)
{
   static const uint32_t tbl[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
   return op < 8 ? tbl[op] : 1;
}

/** Vulkan stencil op (KEEP=0..DECR_WRAP=7) to hardware D3D encoding */
static inline uint32_t
nv_3d_vk_stencil_op(unsigned vk_op)
{
   return nv_3d_ogl_stencil_op(vk_op);
}

static inline void
nv_3d_emit_depth_state(struct nv_push *p, bool test_enable, bool write_enable,
                       unsigned compare_op)
{
   nv_push_method(p, NVC597_SET_DEPTH_TEST, test_enable ? 1 : 0);
   nv_push_method(p, NVC597_SET_DEPTH_WRITE, write_enable ? 1 : 0);
   nv_push_method(p, NVC597_SET_DEPTH_FUNC, nv_3d_ogl_cmp_func(compare_op));
}

/** Front-face (default) stencil plane: func, masks, ref, ops */
static inline void
nv_3d_emit_stencil_front(struct nv_push *p,
                         unsigned compare_op, unsigned compare_mask,
                         unsigned write_mask, unsigned reference,
                         unsigned fail_op, unsigned zfail_op, unsigned zpass_op)
{
   nv_push_method(p, NVC597_SET_STENCIL_FUNC, nv_3d_ogl_cmp_func(compare_op));
   nv_push_method(p, NVC597_SET_STENCIL_FUNC_REF, reference & 0xff);
   nv_push_method(p, NVC597_SET_STENCIL_FUNC_MASK, compare_mask & 0xff);
   nv_push_method(p, NVC597_SET_STENCIL_MASK, write_mask & 0xff);
   nv_push_method(p, NVC597_SET_STENCIL_OP_FAIL, nv_3d_ogl_stencil_op(fail_op));
   nv_push_method(p, NVC597_SET_STENCIL_OP_ZFAIL, nv_3d_ogl_stencil_op(zfail_op));
   nv_push_method(p, NVC597_SET_STENCIL_OP_ZPASS, nv_3d_ogl_stencil_op(zpass_op));
}

/** Back-face stencil plane (requires SET_TWO_SIDED_STENCIL_TEST=1) */
static inline void
nv_3d_emit_stencil_back(struct nv_push *p,
                        unsigned compare_op, unsigned compare_mask,
                        unsigned write_mask, unsigned reference,
                        unsigned fail_op, unsigned zfail_op, unsigned zpass_op)
{
   nv_push_method(p, NVC597_SET_BACK_STENCIL_FUNC, nv_3d_ogl_cmp_func(compare_op));
   nv_push_method(p, NVC597_SET_BACK_STENCIL_FUNC_REF, reference & 0xff);
   nv_push_method(p, NVC597_SET_BACK_STENCIL_FUNC_MASK, compare_mask & 0xff);
   nv_push_method(p, NVC597_SET_BACK_STENCIL_MASK, write_mask & 0xff);
   nv_push_method(p, NVC597_SET_BACK_STENCIL_OP_FAIL, nv_3d_ogl_stencil_op(fail_op));
   nv_push_method(p, NVC597_SET_BACK_STENCIL_OP_ZFAIL, nv_3d_ogl_stencil_op(zfail_op));
   nv_push_method(p, NVC597_SET_BACK_STENCIL_OP_ZPASS, nv_3d_ogl_stencil_op(zpass_op));
}

/**
 * Full stencil state: enable, optional two-sided, front (+ optional back).
 * If two_sided is false, only front plane is programmed (back mirrors front in HW default).
 */
static inline void
nv_3d_emit_stencil_state_full(struct nv_push *p, bool enable, bool two_sided,
                              unsigned f_cmp, unsigned f_cmp_mask,
                              unsigned f_wrt_mask, unsigned f_ref,
                              unsigned f_fail, unsigned f_zfail, unsigned f_zpass,
                              unsigned b_cmp, unsigned b_cmp_mask,
                              unsigned b_wrt_mask, unsigned b_ref,
                              unsigned b_fail, unsigned b_zfail, unsigned b_zpass)
{
   nv_push_method(p, NVC597_SET_STENCIL_TEST, enable ? 1 : 0);
   if (!enable)
      return;
   nv_push_method(p, NVC597_SET_TWO_SIDED_STENCIL_TEST, two_sided ? 1 : 0);
   nv_3d_emit_stencil_front(p, f_cmp, f_cmp_mask, f_wrt_mask, f_ref,
                            f_fail, f_zfail, f_zpass);
   if (two_sided)
      nv_3d_emit_stencil_back(p, b_cmp, b_cmp_mask, b_wrt_mask, b_ref,
                              b_fail, b_zfail, b_zpass);
}

static inline void
nv_3d_emit_stencil_state(struct nv_push *p, bool enable,
                         unsigned compare_op, unsigned compare_mask,
                         unsigned write_mask, unsigned reference,
                         unsigned fail_op, unsigned zfail_op, unsigned zpass_op)
{
   nv_3d_emit_stencil_state_full(p, enable, false,
      compare_op, compare_mask, write_mask, reference,
      fail_op, zfail_op, zpass_op,
      compare_op, compare_mask, write_mask, reference,
      fail_op, zfail_op, zpass_op);
}

/** OGL-style cull/front-face; cull_face: 0=none, 1=front, 2=back, 3=front+back */
static inline void
nv_3d_emit_cull_front_face(struct nv_push *p, unsigned cull_face, bool front_ccw)
{
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
}

/** Map VkCullModeFlags to internal cull_face encoding */
static inline unsigned
nv_3d_cull_face_from_vk(uint32_t vk_cull_mode)
{
   /* VK_CULL_MODE_NONE=0, FRONT=1, BACK=2, FRONT_AND_BACK=3 */
   return (unsigned)(vk_cull_mode & 3u);
}

static inline void
nv_3d_emit_fill_mode(struct nv_push *p, unsigned fill_mode)
{
   uint32_t fill = NVC597_SET_FILL_MODE_V_SOLID;
   if (fill_mode == 1)
      fill = NVC597_SET_FILL_MODE_V_WIREFRAME;
   else if (fill_mode == 2)
      fill = NVC597_SET_FILL_MODE_V_POINT;
   nv_push_method(p, NVC597_SET_FILL_MODE, fill);
}

static inline void
nv_3d_emit_point_size(struct nv_push *p, float size)
{
   union { float f; uint32_t u; } v;
   v.f = size;
   nv_push_method(p, NVC597_SET_POINT_SIZE, v.u);
}

static inline void
nv_3d_emit_rasterizer_discard(struct nv_push *p, bool discard)
{
   /* raster enable = !discard */
   nv_push_method(p, NVC597_SET_RASTER_ENABLE, discard ? 0 : 1);
}

static inline void
nv_3d_emit_provoking_vertex(struct nv_push *p, bool last_vertex)
{
   nv_push_method(p, NVC597_SET_PROVOKING_VERTEX,
                  last_vertex ? NVC597_SET_PROVOKING_VERTEX_V_LAST
                              : NVC597_SET_PROVOKING_VERTEX_V_FIRST);
}

static inline void
nv_3d_emit_depth_bounds(struct nv_push *p, bool enable, float min_d, float max_d)
{
   union { float f; uint32_t u; } lo, hi;
   lo.f = min_d;
   hi.f = max_d;
   nv_push_method(p, NVC597_SET_DEPTH_BOUNDS_TEST, enable ? 1 : 0);
   if (enable) {
      nv_push_method(p, NVC597_SET_DEPTH_BOUNDS_MIN, lo.u);
      nv_push_method(p, NVC597_SET_DEPTH_BOUNDS_MAX, hi.u);
   }
}

/** Logic op: enable + Vulkan/GL op code (COPY=0x1503 etc. via OGL codes in method) */
static inline void
nv_3d_emit_logic_op(struct nv_push *p, bool enable, unsigned vk_logic_op)
{
   /* Vulkan VkLogicOp 0..15 maps to GL 0x1500+op; enable is separate method at 0x19c8
    * on some gens — on C597 enable is programmed via SET_LOGIC_OP with op when on,
    * and a follow-up clear.  Use dedicated enable method if present (0x19c8) via
    * LOGIC_OP register with enable bit patterns from class header. */
   uint32_t gl_op = 0x1500u + (vk_logic_op & 0xfu);
   if (!enable) {
      nv_push_method(p, NVC597_SET_LOGIC_OP, 0); /* disable path: zeroed op */
      return;
   }
   nv_push_method(p, NVC597_SET_LOGIC_OP, gl_op);
}

static inline void
nv_3d_emit_depth_bias(struct nv_push *p, bool enable,
                      float constant_factor, float clamp, float slope_factor)
{
   union { float f; uint32_t u; } cf, cl, sf;
   cf.f = constant_factor;
   cl.f = clamp;
   sf.f = slope_factor;
   /* Polygon offset enable: fill (triangles); line/point optional */
   nv_push_method(p, NVC597_SET_POLY_OFFSET_FILL, enable ? 1 : 0);
   nv_push_method(p, NVC597_SET_POLY_OFFSET_LINE, enable ? 1 : 0);
   nv_push_method(p, NVC597_SET_POLY_OFFSET_POINT, 0);
   nv_push_method(p, NVC597_SET_DEPTH_BIAS, cf.u);
   nv_push_method(p, NVC597_SET_SLOPE_SCALE_DEPTH_BIAS, sf.u);
   nv_push_method(p, NVC597_SET_DEPTH_BIAS_CLAMP, cl.u);
}

static inline void
nv_3d_emit_sample_mask(struct nv_push *p, uint32_t sample_mask)
{
   /* Replicate 16-bit/32-bit sample mask across 2x2 pixel sample mask regs */
   nv_push_method(p, NVC597_SET_SAMPLE_MASK_X0_Y0, sample_mask);
   nv_push_method(p, NVC597_SET_SAMPLE_MASK_X1_Y0, sample_mask);
   nv_push_method(p, NVC597_SET_SAMPLE_MASK_X0_Y1, sample_mask);
   nv_push_method(p, NVC597_SET_SAMPLE_MASK_X1_Y1, sample_mask);
}

static inline void
nv_3d_emit_msaa(struct nv_push *p, uint32_t sample_count, bool alpha_to_coverage)
{
   uint32_t samples_log2 = 0;
   if (sample_count >= 16)
      samples_log2 = 4;
   else if (sample_count >= 8)
      samples_log2 = 3;
   else if (sample_count >= 4)
      samples_log2 = 2;
   else if (sample_count >= 2)
      samples_log2 = 1;
   nv_push_method(p, NVC597_SET_ANTI_ALIAS_ENABLE, sample_count > 1 ? 1 : 0);
   nv_push_method(p, NVC597_SET_ANTI_ALIAS_SAMPLES, samples_log2);
   nv_push_method(p, NVC597_SET_ANTI_ALIAS_ALPHA_CONTROL,
                  alpha_to_coverage ? 1 : 0);
}

static inline void
nv_3d_emit_alpha_to_coverage(struct nv_push *p, bool enable)
{
   nv_push_method(p, NVC597_SET_ANTI_ALIAS_ALPHA_CONTROL, enable ? 1 : 0);
}

/**
 * Depth clamp via SET_VIEWPORT_CLIP_CONTROL (clc597 0x193c).
 * When clamp_enable: PIXEL_MIN/MAX_Z use CLAMP (bits 3/4) so fragments outside
 * [minZ,maxZ] are clamped instead of discarded.  When disabled: CLIP (bits clear).
 * Also program viewport 0 clip Z range to full [0,1] so clamp has a valid range.
 */
static inline void
nv_3d_emit_depth_clamp_enable(struct nv_push *p, bool clamp_enable)
{
   union { float f; uint32_t u; } z0, z1;
   uint32_t ctrl;

   z0.f = 0.0f;
   z1.f = 1.0f;
   nv_push_method(p, NVC597_SET_VIEWPORT_CLIP_MIN_Z(0), z0.u);
   nv_push_method(p, NVC597_SET_VIEWPORT_CLIP_MAX_Z(0), z1.u);

   ctrl = NVC597_SET_VIEWPORT_CLIP_CONTROL_MIN_Z_ZERO_MAX_Z_ONE_TRUE |
          NVC597_SET_VIEWPORT_CLIP_CONTROL_Z_CLIP_RANGE_ZERO_ONE;
   if (clamp_enable)
      ctrl |= NVC597_SET_VIEWPORT_CLIP_CONTROL_PIXEL_MIN_Z_CLAMP |
              NVC597_SET_VIEWPORT_CLIP_CONTROL_PIXEL_MAX_Z_CLAMP;
   nv_push_method(p, NVC597_SET_VIEWPORT_CLIP_CONTROL, ctrl);
   /* ZCULL: leave bounds neutral; clip control owns pixel Z behavior */
   nv_push_method(p, NVC597_SET_ZCULL_BOUNDS, 0);
}

/** Program viewport clip rectangle + Z range for slot (used with multi-viewport). */
static inline void
nv_3d_set_viewport_clip(struct nv_push *p, unsigned slot,
                        uint16_t x0, uint16_t width,
                        uint16_t y0, uint16_t height,
                        float min_z, float max_z)
{
   union { float f; uint32_t u; } zmin, zmax;
   if (slot >= 16)
      slot = 0;
   zmin.f = min_z;
   zmax.f = max_z;
   nv_push_method(p, NVC597_SET_VIEWPORT_CLIP_HORIZONTAL(slot),
                  (uint32_t)x0 | ((uint32_t)width << 16));
   nv_push_method(p, NVC597_SET_VIEWPORT_CLIP_VERTICAL(slot),
                  (uint32_t)y0 | ((uint32_t)height << 16));
   nv_push_method(p, NVC597_SET_VIEWPORT_CLIP_MIN_Z(slot), zmin.u);
   nv_push_method(p, NVC597_SET_VIEWPORT_CLIP_MAX_Z(slot), zmax.u);
}

/** Vulkan/GL depth clip range: 0=[-W,+W] (GL), 1=[0,+W] (D3D/Vulkan default). */
static inline void
nv_3d_emit_viewport_z_clip_range(struct nv_push *p, bool zero_to_one)
{
   nv_push_method(p, NVC597_SET_VIEWPORT_Z_CLIP,
                  zero_to_one
                     ? NVC597_SET_VIEWPORT_Z_CLIP_RANGE_ZERO_TO_POSITIVE_W
                     : NVC597_SET_VIEWPORT_Z_CLIP_RANGE_NEGATIVE_W_TO_POSITIVE_W);
}

/**
 * Bind a depth/stencil (ZETA) target from image parameters and optionally clear.
 * Used by CmdClearDepthStencilImage when not inside an active render pass.
 */
static inline void
nv_3d_bind_and_clear_zeta(struct nv_push *p, uint64_t gpu_addr,
                          uint32_t width, uint32_t height, uint32_t pitch,
                          uint32_t zt_format, unsigned clear_buffers,
                          float depth, uint32_t stencil)
{
   struct nv_3d_surface s;
   memset(&s, 0, sizeof(s));
   s.gpu_addr = gpu_addr;
   s.width = width ? width : 1;
   s.height = height ? height : 1;
   s.array_pitch = pitch ? pitch : (s.width * 4);
   s.format = zt_format ? zt_format : NVC597_SET_ZT_FORMAT_V_Z24S8;
   s.block_linear = false;
   s.enabled = true;
   nv_3d_set_zeta_target(p, &s);
   nv_3d_set_surface_clip(p, 0, 0, s.width, s.height);
   if (clear_buffers)
      nv_3d_emit_clear_surface(p, clear_buffers, NULL, depth, stencil);
}

static inline void
nv_3d_emit_blend_constants(struct nv_push *p,
                           float r, float g, float b, float a)
{
   union { float f; uint32_t u; } c;
   c.f = r; nv_push_method(p, NVC597_SET_BLEND_CONST_RED, c.u);
   c.f = g; nv_push_method(p, NVC597_SET_BLEND_CONST_GREEN, c.u);
   c.f = b; nv_push_method(p, NVC597_SET_BLEND_CONST_BLUE, c.u);
   c.f = a; nv_push_method(p, NVC597_SET_BLEND_CONST_ALPHA, c.u);
}

static inline void
nv_3d_emit_color_write_mask(struct nv_push *p, unsigned target_index,
                            unsigned rgba_mask)
{
   unsigned ti = target_index & 7u;
   nv_push_method(p, NVC597_SET_CT_WRITE(ti), rgba_mask & 0xf);
}

static inline void
nv_3d_emit_blend_enable_target(struct nv_push *p, unsigned target_index,
                               bool enable)
{
   /* Per-target blend enable; index 0 uses SET_BLEND(0) */
   unsigned ti = target_index & 7u;
   nv_push_method(p, NVC597_SET_BLEND(ti), enable ? 1 : 0);
}

/** Host semaphore acquire (wait until sema memory >= payload or == payload) */
static inline void
nv_push_host_semaphore_acquire(struct nv_push *p, uint64_t sema_gpu_addr,
                               uint32_t payload)
{
   nv_push_method(p, NVC36F_SEMAPHOREA,
                  (uint32_t)(sema_gpu_addr >> 32) & 0xff);
   nv_push_method(p, NVC36F_SEMAPHOREB,
                  (uint32_t)((sema_gpu_addr >> 2) & 0x3fffffffu));
   nv_push_method(p, NVC36F_SEMAPHOREC, payload);
   nv_push_method(p, NVC36F_SEMAPHORED,
                  NVC36F_SEMAPHORED_OPERATION_ACQ_GEQ |
                  NVC36F_SEMAPHORED_ACQUIRE_SWITCH_TSG_ENABLE |
                  NVC36F_SEMAPHORED_RELEASE_SIZE_4BYTE);
}

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


/**
 * Clear one MRT slot (target_index 0..7) with the given colour.  NVC597
 * CLEAR_SURFACE selects the MRT via bits 6..8; colour values are set once
 * then applied per-target.  buffers mask uses bit 4<<i for COLORi (PIPE style)
 * or pass target_index explicitly via target_index parameter.
 */
static inline void
nv_3d_emit_clear_surface_mrt(struct nv_push *p, unsigned target_index,
                             const uint32_t color_ui[4],
                             bool clear_r, bool clear_g, bool clear_b, bool clear_a)
{
   uint32_t clear_flags = 0;
   uint32_t c[4];
   unsigned ti = target_index & 7u;

   if (color_ui)
      memcpy(c, color_ui, sizeof(c));
   else
      memset(c, 0, sizeof(c));
   nv_push_method(p, NVC597_SET_COLOR_CLEAR_VALUE(0), c[0]);
   nv_push_method(p, NVC597_SET_COLOR_CLEAR_VALUE(1), c[1]);
   nv_push_method(p, NVC597_SET_COLOR_CLEAR_VALUE(2), c[2]);
   nv_push_method(p, NVC597_SET_COLOR_CLEAR_VALUE(3), c[3]);
   if (clear_r)
      clear_flags |= NVC597_CLEAR_SURFACE_R_ENABLE_TRUE;
   if (clear_g)
      clear_flags |= NVC597_CLEAR_SURFACE_G_ENABLE_TRUE;
   if (clear_b)
      clear_flags |= NVC597_CLEAR_SURFACE_B_ENABLE_TRUE;
   if (clear_a)
      clear_flags |= NVC597_CLEAR_SURFACE_A_ENABLE_TRUE;
   if (!clear_flags)
      clear_flags = NVC597_CLEAR_SURFACE_R_ENABLE_TRUE |
                    NVC597_CLEAR_SURFACE_G_ENABLE_TRUE |
                    NVC597_CLEAR_SURFACE_B_ENABLE_TRUE |
                    NVC597_CLEAR_SURFACE_A_ENABLE_TRUE;
   clear_flags |= (ti << NVC597_CLEAR_SURFACE_MRT_SELECT_SHIFT);
   nv_push_method(p, NVC597_CLEAR_SURFACE, clear_flags);
}

/**
 * Clear all active colour targets (count 1..8) plus optional Z/S.
 * buffers: bit 4<<i for colour target i, bit 8 for depth, bit 9 for stencil
 * (PIPE_CLEAR_COLOR0 = 0x10, COLOR1 = 0x20, ... DEPTH = 0x100, STENCIL = 0x200).
 */
static inline void
nv_3d_emit_clear_surface_multi(struct nv_push *p, unsigned buffers,
                               const uint32_t color_ui[4],
                               float depth, uint32_t stencil,
                               unsigned max_color_targets)
{
   unsigned i, n = max_color_targets ? max_color_targets : 8;
   union { float f; uint32_t u; } d;
   uint32_t zs_flags = 0;
   unsigned color_bits = buffers & 0x1f0u;

   if (n > 8)
      n = 8;

   /* PIPE_CLEAR_COLORi = bit (4 + i) = 0x10 << i */
   for (i = 0; i < n; i++) {
      if (color_bits & (0x10u << i))
         nv_3d_emit_clear_surface_mrt(p, i, color_ui, true, true, true, true);
   }

   if (buffers & 0x100) {
      d.f = depth;
      nv_push_method(p, NVC597_SET_Z_CLEAR_VALUE, d.u);
      zs_flags |= NVC597_CLEAR_SURFACE_Z_ENABLE_TRUE;
   }
   if (buffers & 0x200) {
      nv_push_method(p, NVC597_SET_STENCIL_CLEAR_VALUE, stencil & 0xff);
      zs_flags |= NVC597_CLEAR_SURFACE_STENCIL_ENABLE_TRUE;
   }
   if (zs_flags)
      nv_push_method(p, NVC597_CLEAR_SURFACE, zs_flags);
}

static inline void
nv_3d_push_clear_multi(struct nv_push *p, uint32_t class_3d, unsigned buffers,
                       const uint32_t color_ui[4], float depth, uint32_t stencil,
                       unsigned max_color_targets)
{
   if (class_3d)
      nv_3d_set_object(p, class_3d);
   else
      nv_push_set_subch(p, NV_PUSH_SUBCH_3D);
   /* If only single-target COLOR0 or no multi bits, use classic path */
   if ((buffers & 0x1f0) == 0x10 || (buffers & 0x1f0) == 0) {
      nv_3d_emit_clear_surface(p, buffers, color_ui, depth, stencil);
   } else {
      nv_3d_emit_clear_surface_multi(p, buffers, color_ui, depth, stencil,
                                     max_color_targets);
   }
   nv_push_wfi(p);
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
