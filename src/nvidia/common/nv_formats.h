/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Comprehensive PIPE_FORMAT <-> NVC597 hardware format mapping tables.
 * Derived from open-gpu-kernel-modules clc597.h format enums and
 * NIL (NVIDIA Image Library) nil_formats.csv.
 *
 * Tables cover: render target (color), depth/stencil, and vertex attribute
 * formats for Turing (NVC597) and compatible generations.
 */

#ifndef NV_FORMATS_H
#define NV_FORMATS_H

#include <stdint.h>
#include <stdbool.h>
#include "util/format/u_formats.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===================================================================
 * NVC597 Render Target (Color) Format Values
 * From clc597.h SET_COLOR_TARGET_FORMAT_V_*
 * =================================================================== */

#define NV_RT_FORMAT_DISABLED                     0x00
#define NV_RT_FORMAT_RF32_GF32_BF32_AF32          0xC0
#define NV_RT_FORMAT_RS32_GS32_BS32_AS32          0xC1
#define NV_RT_FORMAT_RU32_GU32_BU32_AU32          0xC2
#define NV_RT_FORMAT_RF32_GF32_BF32_X32           0xC3
#define NV_RT_FORMAT_RS32_GS32_BS32_X32           0xC4
#define NV_RT_FORMAT_RU32_GU32_BU32_X32           0xC5
#define NV_RT_FORMAT_R16_G16_B16_A16              0xC6
#define NV_RT_FORMAT_RN16_GN16_BN16_AN16          0xC7
#define NV_RT_FORMAT_RS16_GS16_BS16_AS16          0xC8
#define NV_RT_FORMAT_RU16_GU16_BU16_AU16          0xC9
#define NV_RT_FORMAT_RF16_GF16_BF16_AF16          0xCA
#define NV_RT_FORMAT_RF32_GF32                    0xCB
#define NV_RT_FORMAT_RS32_GS32                    0xCC
#define NV_RT_FORMAT_RU32_GU32                    0xCD
#define NV_RT_FORMAT_RF16_GF16_BF16_X16           0xCE
#define NV_RT_FORMAT_A8R8G8B8                     0xCF
#define NV_RT_FORMAT_A8RL8GL8BL8                  0xD0
#define NV_RT_FORMAT_A2B10G10R10                  0xD1
#define NV_RT_FORMAT_AU2BU10GU10RU10              0xD2
#define NV_RT_FORMAT_A8B8G8R8                     0xD5
#define NV_RT_FORMAT_A8BL8GL8RL8                  0xD6
#define NV_RT_FORMAT_AN8BN8GN8RN8                 0xD7
#define NV_RT_FORMAT_AS8BS8GS8RS8                 0xD8
#define NV_RT_FORMAT_AU8BU8GU8RU8                 0xD9
#define NV_RT_FORMAT_R16_G16                      0xDA
#define NV_RT_FORMAT_RN16_GN16                    0xDB
#define NV_RT_FORMAT_RS16_GS16                    0xDC
#define NV_RT_FORMAT_RU16_GU16                    0xDD
#define NV_RT_FORMAT_RF16_GF16                    0xDE
#define NV_RT_FORMAT_A2R10G10B10                  0xDF
#define NV_RT_FORMAT_BF10GF11RF11                 0xE0
#define NV_RT_FORMAT_RS32                         0xE3
#define NV_RT_FORMAT_RU32                         0xE4
#define NV_RT_FORMAT_RF32                         0xE5
#define NV_RT_FORMAT_X8R8G8B8                     0xE6
#define NV_RT_FORMAT_X8RL8GL8BL8                  0xE7
#define NV_RT_FORMAT_R5G6B5                       0xE8
#define NV_RT_FORMAT_A1R5G5B5                     0xE9
#define NV_RT_FORMAT_G8R8                         0xEA
#define NV_RT_FORMAT_GN8RN8                       0xEB
#define NV_RT_FORMAT_GS8RS8                       0xEC
#define NV_RT_FORMAT_GU8RU8                       0xED
#define NV_RT_FORMAT_R16                          0xEE
#define NV_RT_FORMAT_RN16                         0xEF
#define NV_RT_FORMAT_RS16                         0xF0
#define NV_RT_FORMAT_RU16                         0xF1
#define NV_RT_FORMAT_RF16                         0xF2
#define NV_RT_FORMAT_R8                           0xF3
#define NV_RT_FORMAT_RN8                          0xF4
#define NV_RT_FORMAT_RS8                          0xF5
#define NV_RT_FORMAT_RU8                          0xF6
#define NV_RT_FORMAT_A8                           0xF7
#define NV_RT_FORMAT_X1R5G5B5                     0xF8
#define NV_RT_FORMAT_X8B8G8R8                     0xF9
#define NV_RT_FORMAT_X8BL8GL8RL8                  0xFA
#define NV_RT_FORMAT_R32                          0xFF
#define NV_RT_FORMAT_A16                          0x40
#define NV_RT_FORMAT_AF16                         0x41
#define NV_RT_FORMAT_AF32                         0x42
#define NV_RT_FORMAT_A8R8                         0x43
#define NV_RT_FORMAT_R16_A16                      0x44
#define NV_RT_FORMAT_RF16_AF16                    0x45
#define NV_RT_FORMAT_RF32_AF32                    0x46
#define NV_RT_FORMAT_B8G8R8A8                     0x47

/* ===================================================================
 * NVC597 Depth/Stencil Format Values
 * From clc597.h SET_ZT_FORMAT_V_*
 * =================================================================== */

#define NV_ZT_FORMAT_Z16                          0x13
#define NV_ZT_FORMAT_Z24S8                        0x14
#define NV_ZT_FORMAT_X8Z24                        0x15
#define NV_ZT_FORMAT_S8Z24                        0x16
#define NV_ZT_FORMAT_S8                           0x17
#define NV_ZT_FORMAT_V8Z24                        0x18
#define NV_ZT_FORMAT_ZF32                         0x0A
#define NV_ZT_FORMAT_ZF32_X24S8                   0x19
#define NV_ZT_FORMAT_X8Z24_X16V8S8                0x1D
#define NV_ZT_FORMAT_ZF32_X16V8X8                 0x1E
#define NV_ZT_FORMAT_ZF32_X16V8S8                 0x1F

/* ===================================================================
 * NVC597 Vertex Attribute Component Bit Widths
 * From clc597.h SET_VERTEX_ATTRIBUTE_A_COMPONENT_BIT_WIDTHS_*
 * =================================================================== */

#define NV_VTX_FMT_R32_G32_B32_A32               0x01
#define NV_VTX_FMT_R32_G32_B32                   0x02
#define NV_VTX_FMT_R16_G16_B16_A16               0x03
#define NV_VTX_FMT_R32_G32                        0x04
#define NV_VTX_FMT_R16_G16_B16                   0x05
#define NV_VTX_FMT_A8B8G8R8                      0x2F
#define NV_VTX_FMT_R8_G8_B8_A8                   0x0A
#define NV_VTX_FMT_X8B8G8R8                      0x33
#define NV_VTX_FMT_A2B10G10R10                   0x30
#define NV_VTX_FMT_B10G11R11                     0x31
#define NV_VTX_FMT_R16_G16                        0x0F
#define NV_VTX_FMT_R32                            0x12
#define NV_VTX_FMT_R8_G8_B8                      0x13
#define NV_VTX_FMT_G8R8                          0x32
#define NV_VTX_FMT_R8_G8                          0x18
#define NV_VTX_FMT_R16                            0x1B
#define NV_VTX_FMT_R8                             0x1D
#define NV_VTX_FMT_A8                             0x34

/* Vertex attribute numerical types */
#define NV_VTX_TYPE_SNORM                         0x01
#define NV_VTX_TYPE_UNORM                         0x02
#define NV_VTX_TYPE_SINT                          0x03
#define NV_VTX_TYPE_UINT                          0x04
#define NV_VTX_TYPE_USCALED                       0x05
#define NV_VTX_TYPE_SSCALED                       0x06
#define NV_VTX_TYPE_FLOAT                         0x07

/* ===================================================================
 * Format info structure for the mapping table
 * =================================================================== */

/** Capabilities bit flags */
#define NV_FMT_CAP_COLOR_TARGET  (1u << 0)  /* C: renderable as color target */
#define NV_FMT_CAP_TEXTURE       (1u << 1)  /* T: usable as texture/sampled */
#define NV_FMT_CAP_DEPTH_STENCIL (1u << 2)  /* Z: depth/stencil target */
#define NV_FMT_CAP_BUFFER        (1u << 3)  /* B: usable in buffer views */
#define NV_FMT_CAP_ALPHA_BLEND   (1u << 4)  /* A: alpha-blendable */
#define NV_FMT_CAP_STORAGE       (1u << 5)  /* S: storage image/buffer */
#define NV_FMT_CAP_SCANOUT       (1u << 6)  /* D: scanout / display */

struct nv_format_info {
   uint16_t rt_format;    /* NV_RT_FORMAT_* or 0 for non-renderable */
   uint8_t  zt_format;    /* NV_ZT_FORMAT_* or 0 for non-depth */
   uint8_t  vtx_bits;     /* NV_VTX_FMT_* component bit widths */
   uint8_t  vtx_type;     /* NV_VTX_TYPE_* numerical type */
   uint8_t  caps;         /* NV_FMT_CAP_* bitfield */
};

/* ===================================================================
 * PIPE_FORMAT → NVC597 render target format conversion
 *
 * Complete mapping derived from NIL nil_formats.csv "czt" column
 * cross-referenced with clc597.h SET_COLOR_TARGET_FORMAT_V_*.
 * =================================================================== */

static inline uint32_t
nv_pipe_to_rt_format(enum pipe_format fmt)
{
   switch (fmt) {
   /* 32-bit RGBA */
   case PIPE_FORMAT_B8G8R8A8_UNORM:     return NV_RT_FORMAT_A8R8G8B8;
   case PIPE_FORMAT_B8G8R8X8_UNORM:     return NV_RT_FORMAT_X8R8G8B8;
   case PIPE_FORMAT_B8G8R8A8_SRGB:      return NV_RT_FORMAT_A8RL8GL8BL8;
   case PIPE_FORMAT_B8G8R8X8_SRGB:      return NV_RT_FORMAT_X8RL8GL8BL8;
   case PIPE_FORMAT_R8G8B8A8_UNORM:     return NV_RT_FORMAT_A8B8G8R8;
   case PIPE_FORMAT_R8G8B8X8_UNORM:     return NV_RT_FORMAT_X8B8G8R8;
   case PIPE_FORMAT_R8G8B8A8_SRGB:      return NV_RT_FORMAT_A8BL8GL8RL8;
   case PIPE_FORMAT_R8G8B8X8_SRGB:      return NV_RT_FORMAT_X8BL8GL8RL8;
   case PIPE_FORMAT_R8G8B8A8_SNORM:     return NV_RT_FORMAT_AN8BN8GN8RN8;
   case PIPE_FORMAT_R8G8B8A8_SINT:      return NV_RT_FORMAT_AS8BS8GS8RS8;
   case PIPE_FORMAT_R8G8B8A8_UINT:      return NV_RT_FORMAT_AU8BU8GU8RU8;
   case PIPE_FORMAT_R8G8B8X8_SNORM:     return NV_RT_FORMAT_AN8BN8GN8RN8;
   case PIPE_FORMAT_R8G8B8X8_SINT:      return NV_RT_FORMAT_AS8BS8GS8RS8;
   case PIPE_FORMAT_R8G8B8X8_UINT:      return NV_RT_FORMAT_AU8BU8GU8RU8;

   /* 10/10/10/2 */
   case PIPE_FORMAT_R10G10B10A2_UNORM:  return NV_RT_FORMAT_A2B10G10R10;
   case PIPE_FORMAT_R10G10B10X2_UNORM:  return NV_RT_FORMAT_A2B10G10R10;
   case PIPE_FORMAT_B10G10R10A2_UNORM:  return NV_RT_FORMAT_A2R10G10B10;
   case PIPE_FORMAT_B10G10R10X2_UNORM:  return NV_RT_FORMAT_A2R10G10B10;
   case PIPE_FORMAT_R10G10B10A2_UINT:   return NV_RT_FORMAT_AU2BU10GU10RU10;

   /* 11/11/10 float */
   case PIPE_FORMAT_R11G11B10_FLOAT:    return NV_RT_FORMAT_BF10GF11RF11;

   /* 16-bit per channel */
   case PIPE_FORMAT_R16_FLOAT:          return NV_RT_FORMAT_RF16;
   case PIPE_FORMAT_R16_UNORM:          return NV_RT_FORMAT_R16;
   case PIPE_FORMAT_R16_SNORM:          return NV_RT_FORMAT_RN16;
   case PIPE_FORMAT_R16_SINT:           return NV_RT_FORMAT_RS16;
   case PIPE_FORMAT_R16_UINT:           return NV_RT_FORMAT_RU16;
   case PIPE_FORMAT_R16G16_FLOAT:       return NV_RT_FORMAT_RF16_GF16;
   case PIPE_FORMAT_R16G16_UNORM:       return NV_RT_FORMAT_R16_G16;
   case PIPE_FORMAT_R16G16_SNORM:       return NV_RT_FORMAT_RN16_GN16;
   case PIPE_FORMAT_R16G16_SINT:        return NV_RT_FORMAT_RS16_GS16;
   case PIPE_FORMAT_R16G16_UINT:        return NV_RT_FORMAT_RU16_GU16;
   case PIPE_FORMAT_R16G16B16A16_FLOAT: return NV_RT_FORMAT_RF16_GF16_BF16_AF16;
   case PIPE_FORMAT_R16G16B16A16_UNORM: return NV_RT_FORMAT_R16_G16_B16_A16;
   case PIPE_FORMAT_R16G16B16A16_SNORM: return NV_RT_FORMAT_RN16_GN16_BN16_AN16;
   case PIPE_FORMAT_R16G16B16A16_SINT:  return NV_RT_FORMAT_RS16_GS16_BS16_AS16;
   case PIPE_FORMAT_R16G16B16A16_UINT:  return NV_RT_FORMAT_RU16_GU16_BU16_AU16;
   case PIPE_FORMAT_R16G16B16X16_FLOAT: return NV_RT_FORMAT_RF16_GF16_BF16_X16;
   case PIPE_FORMAT_R16G16B16X16_UNORM: return NV_RT_FORMAT_R16_G16_B16_A16;
   case PIPE_FORMAT_R16G16B16X16_SNORM: return NV_RT_FORMAT_RN16_GN16_BN16_AN16;
   case PIPE_FORMAT_R16G16B16X16_SINT:  return NV_RT_FORMAT_RS16_GS16_BS16_AS16;
   case PIPE_FORMAT_R16G16B16X16_UINT:  return NV_RT_FORMAT_RU16_GU16_BU16_AU16;

   /* 32-bit per channel */
   case PIPE_FORMAT_R32_FLOAT:          return NV_RT_FORMAT_RF32;
   case PIPE_FORMAT_R32_SINT:           return NV_RT_FORMAT_RS32;
   case PIPE_FORMAT_R32_UINT:           return NV_RT_FORMAT_RU32;
   case PIPE_FORMAT_R32G32_FLOAT:       return NV_RT_FORMAT_RF32_GF32;
   case PIPE_FORMAT_R32G32_SINT:        return NV_RT_FORMAT_RS32_GS32;
   case PIPE_FORMAT_R32G32_UINT:        return NV_RT_FORMAT_RU32_GU32;
   case PIPE_FORMAT_R32G32B32A32_FLOAT: return NV_RT_FORMAT_RF32_GF32_BF32_AF32;
   case PIPE_FORMAT_R32G32B32A32_SINT:  return NV_RT_FORMAT_RS32_GS32_BS32_AS32;
   case PIPE_FORMAT_R32G32B32A32_UINT:  return NV_RT_FORMAT_RU32_GU32_BU32_AU32;
   case PIPE_FORMAT_R32G32B32X32_FLOAT: return NV_RT_FORMAT_RF32_GF32_BF32_X32;
   case PIPE_FORMAT_R32G32B32X32_SINT:  return NV_RT_FORMAT_RS32_GS32_BS32_X32;
   case PIPE_FORMAT_R32G32B32X32_UINT:  return NV_RT_FORMAT_RU32_GU32_BU32_X32;

   /* 8-bit per channel */
   case PIPE_FORMAT_R8_UNORM:           return NV_RT_FORMAT_R8;
   case PIPE_FORMAT_R8_SNORM:           return NV_RT_FORMAT_RN8;
   case PIPE_FORMAT_R8_SINT:            return NV_RT_FORMAT_RS8;
   case PIPE_FORMAT_R8_UINT:            return NV_RT_FORMAT_RU8;
   case PIPE_FORMAT_R8G8_UNORM:         return NV_RT_FORMAT_G8R8;
   case PIPE_FORMAT_R8G8_SNORM:         return NV_RT_FORMAT_GN8RN8;
   case PIPE_FORMAT_R8G8_SINT:          return NV_RT_FORMAT_GS8RS8;
   case PIPE_FORMAT_R8G8_UINT:          return NV_RT_FORMAT_GU8RU8;

   /* 5/6/5 */
   case PIPE_FORMAT_B5G6R5_UNORM:       return NV_RT_FORMAT_R5G6B5;
   case PIPE_FORMAT_B5G5R5A1_UNORM:     return NV_RT_FORMAT_A1R5G5B5;
   case PIPE_FORMAT_B5G5R5X1_UNORM:     return NV_RT_FORMAT_X1R5G5B5;

   /* Alpha only */
   case PIPE_FORMAT_A8_UNORM:           return NV_RT_FORMAT_A8;

   /* Luminance (map to R channel — swizzle applied via TIC) */
   case PIPE_FORMAT_L8_UNORM:           return NV_RT_FORMAT_R8;
   case PIPE_FORMAT_L8_SNORM:           return NV_RT_FORMAT_RN8;
   case PIPE_FORMAT_L8_SINT:            return NV_RT_FORMAT_RS8;
   case PIPE_FORMAT_L8_UINT:            return NV_RT_FORMAT_RU8;
   case PIPE_FORMAT_L16_UNORM:          return NV_RT_FORMAT_R16;
   case PIPE_FORMAT_L16_SNORM:          return NV_RT_FORMAT_RN16;
   case PIPE_FORMAT_L16_FLOAT:          return NV_RT_FORMAT_RF16;
   case PIPE_FORMAT_L16_SINT:           return NV_RT_FORMAT_RS16;
   case PIPE_FORMAT_L16_UINT:           return NV_RT_FORMAT_RU16;
   case PIPE_FORMAT_L32_FLOAT:          return NV_RT_FORMAT_RF32;
   case PIPE_FORMAT_L32_SINT:           return NV_RT_FORMAT_RS32;
   case PIPE_FORMAT_L32_UINT:           return NV_RT_FORMAT_RU32;

   /* Intensity (map to R channel — swizzle RRRR via TIC) */
   case PIPE_FORMAT_I8_UNORM:           return NV_RT_FORMAT_R8;
   case PIPE_FORMAT_I8_SNORM:           return NV_RT_FORMAT_RN8;
   case PIPE_FORMAT_I8_SINT:            return NV_RT_FORMAT_RS8;
   case PIPE_FORMAT_I8_UINT:            return NV_RT_FORMAT_RU8;
   case PIPE_FORMAT_I16_UNORM:          return NV_RT_FORMAT_R16;
   case PIPE_FORMAT_I16_SNORM:          return NV_RT_FORMAT_RN16;
   case PIPE_FORMAT_I16_FLOAT:          return NV_RT_FORMAT_RF16;
   case PIPE_FORMAT_I16_SINT:           return NV_RT_FORMAT_RS16;
   case PIPE_FORMAT_I16_UINT:           return NV_RT_FORMAT_RU16;
   case PIPE_FORMAT_I32_FLOAT:          return NV_RT_FORMAT_RF32;
   case PIPE_FORMAT_I32_SINT:           return NV_RT_FORMAT_RS32;
   case PIPE_FORMAT_I32_UINT:           return NV_RT_FORMAT_RU32;

   /* A+F16/32 (alpha + one float channel) */
   case PIPE_FORMAT_A16_FLOAT:          return NV_RT_FORMAT_AF16;
   case PIPE_FORMAT_A32_FLOAT:          return NV_RT_FORMAT_AF32;

   default: return NV_RT_FORMAT_DISABLED;
   }
}

/* ===================================================================
 * PIPE_FORMAT → NVC597 depth/stencil format
 * =================================================================== */

static inline uint32_t
nv_pipe_to_zt_format(enum pipe_format fmt)
{
   switch (fmt) {
   case PIPE_FORMAT_Z16_UNORM:           return NV_ZT_FORMAT_Z16;
   case PIPE_FORMAT_Z32_FLOAT:           return NV_ZT_FORMAT_ZF32;
   case PIPE_FORMAT_Z24X8_UNORM:         return NV_ZT_FORMAT_X8Z24;
   case PIPE_FORMAT_X8Z24_UNORM:         return NV_ZT_FORMAT_Z24S8;
   case PIPE_FORMAT_Z24_UNORM_S8_UINT:   return NV_ZT_FORMAT_S8Z24;
   case PIPE_FORMAT_S8_UINT_Z24_UNORM:   return NV_ZT_FORMAT_Z24S8;
   case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:return NV_ZT_FORMAT_ZF32_X24S8;
   case PIPE_FORMAT_S8_UINT:             return NV_ZT_FORMAT_S8;
   default: return 0;
   }
}

/* ===================================================================
 * PIPE_FORMAT → NVC597 vertex attribute format
 *
 * Returns component bit widths + numerical type packed as:
 *   bits [5:0]  = NV_VTX_FMT_* component bit widths
 *   bits [10:8] = NV_VTX_TYPE_* numerical type
 *   0 = unsupported vertex format
 * =================================================================== */

#define NV_VTX_PACK(bits, type) \
   (((uint32_t)(bits) & 0x3f) | (((uint32_t)(type) & 0x7) << 8))
#define NV_VTX_UNPACK_BITS(v)  ((v) & 0x3f)
#define NV_VTX_UNPACK_TYPE(v)  (((v) >> 8) & 0x7)

static inline uint32_t
nv_pipe_to_vtx_format(enum pipe_format fmt)
{
   switch (fmt) {
   /* R32G32B32A32 */
   case PIPE_FORMAT_R32G32B32A32_FLOAT:  return NV_VTX_PACK(NV_VTX_FMT_R32_G32_B32_A32, NV_VTX_TYPE_FLOAT);
   case PIPE_FORMAT_R32G32B32A32_SINT:   return NV_VTX_PACK(NV_VTX_FMT_R32_G32_B32_A32, NV_VTX_TYPE_SINT);
   case PIPE_FORMAT_R32G32B32A32_UINT:   return NV_VTX_PACK(NV_VTX_FMT_R32_G32_B32_A32, NV_VTX_TYPE_UINT);
   case PIPE_FORMAT_R32G32B32A32_UNORM:  return NV_VTX_PACK(NV_VTX_FMT_R32_G32_B32_A32, NV_VTX_TYPE_UNORM);
   case PIPE_FORMAT_R32G32B32A32_SNORM:  return NV_VTX_PACK(NV_VTX_FMT_R32_G32_B32_A32, NV_VTX_TYPE_SNORM);
   case PIPE_FORMAT_R32G32B32A32_USCALED:return NV_VTX_PACK(NV_VTX_FMT_R32_G32_B32_A32, NV_VTX_TYPE_USCALED);
   case PIPE_FORMAT_R32G32B32A32_SSCALED:return NV_VTX_PACK(NV_VTX_FMT_R32_G32_B32_A32, NV_VTX_TYPE_SSCALED);

   /* R32G32B32 */
   case PIPE_FORMAT_R32G32B32_FLOAT:     return NV_VTX_PACK(NV_VTX_FMT_R32_G32_B32, NV_VTX_TYPE_FLOAT);
   case PIPE_FORMAT_R32G32B32_SINT:      return NV_VTX_PACK(NV_VTX_FMT_R32_G32_B32, NV_VTX_TYPE_SINT);
   case PIPE_FORMAT_R32G32B32_UINT:      return NV_VTX_PACK(NV_VTX_FMT_R32_G32_B32, NV_VTX_TYPE_UINT);
   case PIPE_FORMAT_R32G32B32_UNORM:     return NV_VTX_PACK(NV_VTX_FMT_R32_G32_B32, NV_VTX_TYPE_UNORM);
   case PIPE_FORMAT_R32G32B32_SNORM:     return NV_VTX_PACK(NV_VTX_FMT_R32_G32_B32, NV_VTX_TYPE_SNORM);
   case PIPE_FORMAT_R32G32B32_USCALED:   return NV_VTX_PACK(NV_VTX_FMT_R32_G32_B32, NV_VTX_TYPE_USCALED);
   case PIPE_FORMAT_R32G32B32_SSCALED:   return NV_VTX_PACK(NV_VTX_FMT_R32_G32_B32, NV_VTX_TYPE_SSCALED);

   /* R32G32 */
   case PIPE_FORMAT_R32G32_FLOAT:        return NV_VTX_PACK(NV_VTX_FMT_R32_G32, NV_VTX_TYPE_FLOAT);
   case PIPE_FORMAT_R32G32_SINT:         return NV_VTX_PACK(NV_VTX_FMT_R32_G32, NV_VTX_TYPE_SINT);
   case PIPE_FORMAT_R32G32_UINT:         return NV_VTX_PACK(NV_VTX_FMT_R32_G32, NV_VTX_TYPE_UINT);
   case PIPE_FORMAT_R32G32_UNORM:        return NV_VTX_PACK(NV_VTX_FMT_R32_G32, NV_VTX_TYPE_UNORM);
   case PIPE_FORMAT_R32G32_SNORM:        return NV_VTX_PACK(NV_VTX_FMT_R32_G32, NV_VTX_TYPE_SNORM);
   case PIPE_FORMAT_R32G32_USCALED:      return NV_VTX_PACK(NV_VTX_FMT_R32_G32, NV_VTX_TYPE_USCALED);
   case PIPE_FORMAT_R32G32_SSCALED:      return NV_VTX_PACK(NV_VTX_FMT_R32_G32, NV_VTX_TYPE_SSCALED);

   /* R32 */
   case PIPE_FORMAT_R32_FLOAT:           return NV_VTX_PACK(NV_VTX_FMT_R32, NV_VTX_TYPE_FLOAT);
   case PIPE_FORMAT_R32_SINT:            return NV_VTX_PACK(NV_VTX_FMT_R32, NV_VTX_TYPE_SINT);
   case PIPE_FORMAT_R32_UINT:            return NV_VTX_PACK(NV_VTX_FMT_R32, NV_VTX_TYPE_UINT);
   case PIPE_FORMAT_R32_UNORM:           return NV_VTX_PACK(NV_VTX_FMT_R32, NV_VTX_TYPE_UNORM);
   case PIPE_FORMAT_R32_SNORM:           return NV_VTX_PACK(NV_VTX_FMT_R32, NV_VTX_TYPE_SNORM);
   case PIPE_FORMAT_R32_USCALED:         return NV_VTX_PACK(NV_VTX_FMT_R32, NV_VTX_TYPE_USCALED);
   case PIPE_FORMAT_R32_SSCALED:         return NV_VTX_PACK(NV_VTX_FMT_R32, NV_VTX_TYPE_SSCALED);

   /* R16G16B16A16 */
   case PIPE_FORMAT_R16G16B16A16_FLOAT:  return NV_VTX_PACK(NV_VTX_FMT_R16_G16_B16_A16, NV_VTX_TYPE_FLOAT);
   case PIPE_FORMAT_R16G16B16A16_SINT:   return NV_VTX_PACK(NV_VTX_FMT_R16_G16_B16_A16, NV_VTX_TYPE_SINT);
   case PIPE_FORMAT_R16G16B16A16_UINT:   return NV_VTX_PACK(NV_VTX_FMT_R16_G16_B16_A16, NV_VTX_TYPE_UINT);
   case PIPE_FORMAT_R16G16B16A16_UNORM:  return NV_VTX_PACK(NV_VTX_FMT_R16_G16_B16_A16, NV_VTX_TYPE_UNORM);
   case PIPE_FORMAT_R16G16B16A16_SNORM:  return NV_VTX_PACK(NV_VTX_FMT_R16_G16_B16_A16, NV_VTX_TYPE_SNORM);
   case PIPE_FORMAT_R16G16B16A16_USCALED:return NV_VTX_PACK(NV_VTX_FMT_R16_G16_B16_A16, NV_VTX_TYPE_USCALED);
   case PIPE_FORMAT_R16G16B16A16_SSCALED:return NV_VTX_PACK(NV_VTX_FMT_R16_G16_B16_A16, NV_VTX_TYPE_SSCALED);

   /* R16G16B16 */
   case PIPE_FORMAT_R16G16B16_FLOAT:     return NV_VTX_PACK(NV_VTX_FMT_R16_G16_B16, NV_VTX_TYPE_FLOAT);
   case PIPE_FORMAT_R16G16B16_SINT:      return NV_VTX_PACK(NV_VTX_FMT_R16_G16_B16, NV_VTX_TYPE_SINT);
   case PIPE_FORMAT_R16G16B16_UINT:      return NV_VTX_PACK(NV_VTX_FMT_R16_G16_B16, NV_VTX_TYPE_UINT);
   case PIPE_FORMAT_R16G16B16_UNORM:     return NV_VTX_PACK(NV_VTX_FMT_R16_G16_B16, NV_VTX_TYPE_UNORM);
   case PIPE_FORMAT_R16G16B16_SNORM:     return NV_VTX_PACK(NV_VTX_FMT_R16_G16_B16, NV_VTX_TYPE_SNORM);
   case PIPE_FORMAT_R16G16B16_USCALED:   return NV_VTX_PACK(NV_VTX_FMT_R16_G16_B16, NV_VTX_TYPE_USCALED);
   case PIPE_FORMAT_R16G16B16_SSCALED:   return NV_VTX_PACK(NV_VTX_FMT_R16_G16_B16, NV_VTX_TYPE_SSCALED);

   /* R16G16 */
   case PIPE_FORMAT_R16G16_FLOAT:        return NV_VTX_PACK(NV_VTX_FMT_R16_G16, NV_VTX_TYPE_FLOAT);
   case PIPE_FORMAT_R16G16_SINT:         return NV_VTX_PACK(NV_VTX_FMT_R16_G16, NV_VTX_TYPE_SINT);
   case PIPE_FORMAT_R16G16_UINT:         return NV_VTX_PACK(NV_VTX_FMT_R16_G16, NV_VTX_TYPE_UINT);
   case PIPE_FORMAT_R16G16_UNORM:        return NV_VTX_PACK(NV_VTX_FMT_R16_G16, NV_VTX_TYPE_UNORM);
   case PIPE_FORMAT_R16G16_SNORM:        return NV_VTX_PACK(NV_VTX_FMT_R16_G16, NV_VTX_TYPE_SNORM);
   case PIPE_FORMAT_R16G16_USCALED:      return NV_VTX_PACK(NV_VTX_FMT_R16_G16, NV_VTX_TYPE_USCALED);
   case PIPE_FORMAT_R16G16_SSCALED:      return NV_VTX_PACK(NV_VTX_FMT_R16_G16, NV_VTX_TYPE_SSCALED);

   /* R16 */
   case PIPE_FORMAT_R16_FLOAT:           return NV_VTX_PACK(NV_VTX_FMT_R16, NV_VTX_TYPE_FLOAT);
   case PIPE_FORMAT_R16_SINT:            return NV_VTX_PACK(NV_VTX_FMT_R16, NV_VTX_TYPE_SINT);
   case PIPE_FORMAT_R16_UINT:            return NV_VTX_PACK(NV_VTX_FMT_R16, NV_VTX_TYPE_UINT);
   case PIPE_FORMAT_R16_UNORM:           return NV_VTX_PACK(NV_VTX_FMT_R16, NV_VTX_TYPE_UNORM);
   case PIPE_FORMAT_R16_SNORM:           return NV_VTX_PACK(NV_VTX_FMT_R16, NV_VTX_TYPE_SNORM);
   case PIPE_FORMAT_R16_USCALED:         return NV_VTX_PACK(NV_VTX_FMT_R16, NV_VTX_TYPE_USCALED);
   case PIPE_FORMAT_R16_SSCALED:         return NV_VTX_PACK(NV_VTX_FMT_R16, NV_VTX_TYPE_SSCALED);

   /* R8G8B8A8 */
   case PIPE_FORMAT_R8G8B8A8_UNORM:      return NV_VTX_PACK(NV_VTX_FMT_R8_G8_B8_A8, NV_VTX_TYPE_UNORM);
   case PIPE_FORMAT_R8G8B8A8_SNORM:      return NV_VTX_PACK(NV_VTX_FMT_R8_G8_B8_A8, NV_VTX_TYPE_SNORM);
   case PIPE_FORMAT_R8G8B8A8_SINT:       return NV_VTX_PACK(NV_VTX_FMT_R8_G8_B8_A8, NV_VTX_TYPE_SINT);
   case PIPE_FORMAT_R8G8B8A8_UINT:       return NV_VTX_PACK(NV_VTX_FMT_R8_G8_B8_A8, NV_VTX_TYPE_UINT);
   case PIPE_FORMAT_R8G8B8A8_USCALED:    return NV_VTX_PACK(NV_VTX_FMT_R8_G8_B8_A8, NV_VTX_TYPE_USCALED);
   case PIPE_FORMAT_R8G8B8A8_SSCALED:    return NV_VTX_PACK(NV_VTX_FMT_R8_G8_B8_A8, NV_VTX_TYPE_SSCALED);

   /* R8G8B8 */
   case PIPE_FORMAT_R8G8B8_UNORM:        return NV_VTX_PACK(NV_VTX_FMT_R8_G8_B8, NV_VTX_TYPE_UNORM);
   case PIPE_FORMAT_R8G8B8_SNORM:        return NV_VTX_PACK(NV_VTX_FMT_R8_G8_B8, NV_VTX_TYPE_SNORM);
   case PIPE_FORMAT_R8G8B8_SINT:         return NV_VTX_PACK(NV_VTX_FMT_R8_G8_B8, NV_VTX_TYPE_SINT);
   case PIPE_FORMAT_R8G8B8_UINT:         return NV_VTX_PACK(NV_VTX_FMT_R8_G8_B8, NV_VTX_TYPE_UINT);
   case PIPE_FORMAT_R8G8B8_USCALED:      return NV_VTX_PACK(NV_VTX_FMT_R8_G8_B8, NV_VTX_TYPE_USCALED);
   case PIPE_FORMAT_R8G8B8_SSCALED:      return NV_VTX_PACK(NV_VTX_FMT_R8_G8_B8, NV_VTX_TYPE_SSCALED);

   /* R8G8 */
   case PIPE_FORMAT_R8G8_UNORM:          return NV_VTX_PACK(NV_VTX_FMT_R8_G8, NV_VTX_TYPE_UNORM);
   case PIPE_FORMAT_R8G8_SNORM:          return NV_VTX_PACK(NV_VTX_FMT_R8_G8, NV_VTX_TYPE_SNORM);
   case PIPE_FORMAT_R8G8_SINT:           return NV_VTX_PACK(NV_VTX_FMT_R8_G8, NV_VTX_TYPE_SINT);
   case PIPE_FORMAT_R8G8_UINT:           return NV_VTX_PACK(NV_VTX_FMT_R8_G8, NV_VTX_TYPE_UINT);
   case PIPE_FORMAT_R8G8_USCALED:        return NV_VTX_PACK(NV_VTX_FMT_R8_G8, NV_VTX_TYPE_USCALED);
   case PIPE_FORMAT_R8G8_SSCALED:        return NV_VTX_PACK(NV_VTX_FMT_R8_G8, NV_VTX_TYPE_SSCALED);

   /* R8 */
   case PIPE_FORMAT_R8_UNORM:            return NV_VTX_PACK(NV_VTX_FMT_R8, NV_VTX_TYPE_UNORM);
   case PIPE_FORMAT_R8_SNORM:            return NV_VTX_PACK(NV_VTX_FMT_R8, NV_VTX_TYPE_SNORM);
   case PIPE_FORMAT_R8_SINT:             return NV_VTX_PACK(NV_VTX_FMT_R8, NV_VTX_TYPE_SINT);
   case PIPE_FORMAT_R8_UINT:             return NV_VTX_PACK(NV_VTX_FMT_R8, NV_VTX_TYPE_UINT);
   case PIPE_FORMAT_R8_USCALED:          return NV_VTX_PACK(NV_VTX_FMT_R8, NV_VTX_TYPE_USCALED);
   case PIPE_FORMAT_R8_SSCALED:          return NV_VTX_PACK(NV_VTX_FMT_R8, NV_VTX_TYPE_SSCALED);

   /* 10/10/10/2 */
   case PIPE_FORMAT_R10G10B10A2_UNORM:   return NV_VTX_PACK(NV_VTX_FMT_A2B10G10R10, NV_VTX_TYPE_UNORM);
   case PIPE_FORMAT_R10G10B10A2_SNORM:   return NV_VTX_PACK(NV_VTX_FMT_A2B10G10R10, NV_VTX_TYPE_SNORM);
   case PIPE_FORMAT_R10G10B10A2_UINT:    return NV_VTX_PACK(NV_VTX_FMT_A2B10G10R10, NV_VTX_TYPE_UINT);
   case PIPE_FORMAT_R10G10B10A2_USCALED: return NV_VTX_PACK(NV_VTX_FMT_A2B10G10R10, NV_VTX_TYPE_USCALED);
   case PIPE_FORMAT_R10G10B10A2_SSCALED: return NV_VTX_PACK(NV_VTX_FMT_A2B10G10R10, NV_VTX_TYPE_SSCALED);

   /* 11/11/10 float */
   case PIPE_FORMAT_R11G11B10_FLOAT:     return NV_VTX_PACK(NV_VTX_FMT_B10G11R11, NV_VTX_TYPE_FLOAT);

   /* B8G8R8A8 (BGRA vertex input, uses A8B8G8R8 with swap) */
   case PIPE_FORMAT_B8G8R8A8_UNORM:      return NV_VTX_PACK(NV_VTX_FMT_A8B8G8R8, NV_VTX_TYPE_UNORM);
   case PIPE_FORMAT_B8G8R8A8_SINT:       return NV_VTX_PACK(NV_VTX_FMT_A8B8G8R8, NV_VTX_TYPE_SINT);
   case PIPE_FORMAT_B8G8R8A8_UINT:       return NV_VTX_PACK(NV_VTX_FMT_A8B8G8R8, NV_VTX_TYPE_UINT);

   /* 64-bit (mapped as R32G32 in HW) */
   case PIPE_FORMAT_R64_FLOAT:           return NV_VTX_PACK(NV_VTX_FMT_R32_G32, NV_VTX_TYPE_FLOAT);
   case PIPE_FORMAT_R64_SINT:            return NV_VTX_PACK(NV_VTX_FMT_R32_G32, NV_VTX_TYPE_SINT);
   case PIPE_FORMAT_R64_UINT:            return NV_VTX_PACK(NV_VTX_FMT_R32_G32, NV_VTX_TYPE_UINT);

   default: return 0;
   }
}

/* ===================================================================
 * Helper: Build SET_VERTEX_ATTRIBUTE_A dword from pipe format
 *
 * Returns the fully-packed attribute word for nv_push_method() with
 * SET_VERTEX_ATTRIBUTE_A(i), or 0 if the format is unsupported.
 * =================================================================== */

static inline uint32_t
nv_vtx_attrib_from_pipe(enum pipe_format fmt, unsigned stream,
                        unsigned byte_offset, bool active)
{
   uint32_t packed = nv_pipe_to_vtx_format(fmt);
   uint32_t bits, type;

   if (!packed)
      return 0;

   bits = NV_VTX_UNPACK_BITS(packed);
   type = NV_VTX_UNPACK_TYPE(packed);

   return (stream & 0x1f) |
          (active ? 0 : (1u << 6)) |
          ((byte_offset & 0x3fff) << 7) |
          ((bits & 0x3f) << 21) |
          ((type & 0x7) << 27);
}

/* ===================================================================
 * Format capability queries
 * =================================================================== */

/** Check if a pipe format is renderable as a color target on NVC597. */
static inline bool
nv_format_is_color_renderable(enum pipe_format fmt)
{
   return nv_pipe_to_rt_format(fmt) != NV_RT_FORMAT_DISABLED;
}

/** Check if a pipe format is usable as a depth/stencil target on NVC597. */
static inline bool
nv_format_is_depth_stencil(enum pipe_format fmt)
{
   return nv_pipe_to_zt_format(fmt) != 0;
}

/** Check if a pipe format has vertex input support on NVC597. */
static inline bool
nv_format_is_vertex_format(enum pipe_format fmt)
{
   return nv_pipe_to_vtx_format(fmt) != 0;
}

#ifdef __cplusplus
}
#endif

#endif /* NV_FORMATS_H */
