/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Texture/sampler header pool (nvidia-3d Nv3dTexture pattern): interleaved
 * sampler[8] + header[8] dwords per entry; SET_TEX_SAMPLER_POOL /
 * SET_TEX_HEADER_POOL point at samp / head offsets within the pool BO.
 *
 * Pitch texture header bit layout from class/clc597tex.h (TEXHEAD_PITCH_*).
 * MW(hi:lo) means inclusive bit range in the 256-bit (8-dword) header word
 * array; we implement NV_TEX_MW_SET to place fields exactly.
 */

#ifndef NV_TEX_H
#define NV_TEX_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "nv_3d_methods.h"
#include "nv_push.h"

#ifdef __cplusplus
extern "C" {
#endif

struct nv_rm_device;
struct nv_rm_bo;

#define NV_TEX_SAMPLER_DWORDS   8
#define NV_TEX_HEADER_DWORDS    8
#define NV_TEX_ENTRY_DWORDS     (NV_TEX_SAMPLER_DWORDS + NV_TEX_HEADER_DWORDS)
#define NV_TEX_ENTRY_BYTES      (NV_TEX_ENTRY_DWORDS * 4)
#define NV_TEX_POOL_DEFAULT_N   64

/* Component sizes from NVC597_TEXHEAD_PITCH_COMPONENTS_SIZES_* */
#define NV_TEX_COMP_R32G32B32A32 0x01
#define NV_TEX_COMP_R16G16B16A16 0x03
#define NV_TEX_COMP_R32G32       0x04
#define NV_TEX_COMP_X8B8G8R8     0x07
#define NV_TEX_COMP_A8B8G8R8     0x08
#define NV_TEX_COMP_A2B10G10R10  0x09
#define NV_TEX_COMP_R16G16       0x0c
#define NV_TEX_COMP_R32          0x0f
#define NV_TEX_COMP_A1B5G5R5     0x14
#define NV_TEX_COMP_B5G6R5       0x15
#define NV_TEX_COMP_G8R8         0x18
#define NV_TEX_COMP_R16          0x1b
#define NV_TEX_COMP_R8           0x1d
#define NV_TEX_COMP_BF10GF11RF11 0x21
#define NV_TEX_COMP_DXT1         0x24
#define NV_TEX_COMP_DXT23        0x25
#define NV_TEX_COMP_DXT45        0x26
#define NV_TEX_COMP_BC7U         0x17
#define NV_TEX_COMP_Z24S8        0x29
#define NV_TEX_COMP_ZF32         0x2f
#define NV_TEX_COMP_Z16          0x3a

/* Data type from NVC597_TEXHEAD_PITCH_*_DATA_TYPE_NUM_* */
#define NV_TEX_DT_SNORM         0x1
#define NV_TEX_DT_UNORM         0x2
#define NV_TEX_DT_SINT          0x3
#define NV_TEX_DT_UINT          0x4
#define NV_TEX_DT_FLOAT         0x7

/* Source channel from NVC597_TEXHEAD_PITCH_*_SOURCE_IN_* */
#define NV_TEX_SRC_ZERO         0x0
#define NV_TEX_SRC_R            0x2
#define NV_TEX_SRC_G            0x3
#define NV_TEX_SRC_B            0x4
#define NV_TEX_SRC_A            0x5
#define NV_TEX_SRC_ONE_FLOAT    0x6
#define NV_TEX_SRC_ONE_INT      0x7

/* Header version SELECT_PITCH = 2; TEXTURE_TYPE TWO_D = 1 */
#define NV_TEX_HEADER_VERSION_PITCH  0x2
#define NV_TEX_HEADER_VERSION_BL     0x3
#define NV_TEX_TEXTURE_TYPE_1D       0x0
#define NV_TEX_TEXTURE_TYPE_2D       0x1
#define NV_TEX_TEXTURE_TYPE_3D       0x2
#define NV_TEX_TEXTURE_TYPE_CUBE     0x3
#define NV_TEX_TEXTURE_TYPE_1D_ARRAY 0x4
#define NV_TEX_TEXTURE_TYPE_2D_ARRAY 0x5
#define NV_TEX_TEXTURE_TYPE_1D_BUF   0x6
#define NV_TEX_TEXTURE_TYPE_2D_NO_MIP 0x7

/* Sampler address modes (TEXSAMP0) */
#define NV_TEX_SAMP_ADDR_WRAP        0x0
#define NV_TEX_SAMP_ADDR_MIRROR      0x1
#define NV_TEX_SAMP_ADDR_CLAMP_EDGE  0x2
#define NV_TEX_SAMP_ADDR_BORDER      0x3
#define NV_TEX_SAMP_ADDR_CLAMP_OGL   0x4

/* Sampler filters (TEXSAMP1 mag/min — see clc597tex TEXSAMP1) */
#define NV_TEX_SAMP_FILT_NEAREST     0x1
#define NV_TEX_SAMP_FILT_LINEAR      0x2

struct nv_tex_desc {
   uint64_t gpu_addr;
   uint32_t width;
   uint32_t height;
   uint32_t depth;          /* 1 for 2D */
   uint32_t pitch;          /* bytes; must be multiple of 32 for pitch header */
   uint8_t  components;     /* NV_TEX_COMP_* */
   uint8_t  data_type;      /* NV_TEX_DT_* — applied to R/G/B/A channels */
   uint8_t  src_x, src_y, src_z, src_w; /* NV_TEX_SRC_* swizzle */
   uint8_t  texture_type;   /* NV_TEX_TEXTURE_TYPE_*; default 2D */
   bool     normalized_coords;
   bool     s_r_g_b_conversion; /* sRGB */
   /* sampler */
   uint8_t  addr_u, addr_v, addr_p;
   uint8_t  mag_filt, min_filt;
   uint8_t  mip_filt;       /* 1=none/nearest, 2=linear */
   float    max_aniso;      /* 1.0 .. 16.0; encoded coarsely */
   float    min_lod, max_lod;
   float    lod_bias;
};

struct nv_tex_entry {
   uint32_t samp[NV_TEX_SAMPLER_DWORDS];
   uint32_t head[NV_TEX_HEADER_DWORDS];
};

struct nv_tex_pool {
   struct nv_rm_device *rm;
   struct nv_rm_bo *bo;
   void *cpu_map;
   uint64_t gpu_addr;
   uint32_t num_entries;
   uint32_t next_slot;
   bool pools_emitted;
};

/*
 * Set bits [hi:lo] inclusive in a multi-word bit array (little-endian dwords),
 * matching NVIDIA class header MW(hi:lo) convention.
 */
static inline void
nv_tex_mw_set(uint32_t *words, unsigned nwords, unsigned hi, unsigned lo,
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
nv_tex_mw_set_u32(uint32_t *words, unsigned hi, unsigned lo, uint32_t value)
{
   nv_tex_mw_set(words, NV_TEX_HEADER_DWORDS, hi, lo, value);
}

/* Encode pitch 2D texture header using exact clc597tex MW field positions. */
static inline void
nv_tex_encode_pitch_2d(const struct nv_tex_desc *d, struct nv_tex_entry *e)
{
   uint32_t w1 = d->width ? (d->width - 1) : 0;
   uint32_t h1 = d->height ? (d->height - 1) : 0;
   uint32_t pitch = d->pitch;
   uint64_t addr = d->gpu_addr;
   uint8_t dt = d->data_type ? d->data_type : NV_TEX_DT_UNORM;
   uint8_t sx = d->src_x ? d->src_x : NV_TEX_SRC_R;
   uint8_t sy = d->src_y ? d->src_y : NV_TEX_SRC_G;
   uint8_t sz = d->src_z ? d->src_z : NV_TEX_SRC_B;
   uint8_t sw = d->src_w ? d->src_w : NV_TEX_SRC_A;
   uint8_t comp = d->components ? d->components : NV_TEX_COMP_A8B8G8R8;
   uint8_t ttype = d->texture_type ? d->texture_type : NV_TEX_TEXTURE_TYPE_2D;
   uint8_t au = d->addr_u ? d->addr_u : NV_TEX_SAMP_ADDR_CLAMP_EDGE;
   uint8_t av = d->addr_v ? d->addr_v : NV_TEX_SAMP_ADDR_CLAMP_EDGE;
   uint8_t ap = d->addr_p ? d->addr_p : NV_TEX_SAMP_ADDR_CLAMP_EDGE;
   uint8_t mag = d->mag_filt ? d->mag_filt : NV_TEX_SAMP_FILT_LINEAR;
   uint8_t minf = d->min_filt ? d->min_filt : NV_TEX_SAMP_FILT_LINEAR;
   uint8_t mip = d->mip_filt ? d->mip_filt : NV_TEX_SAMP_FILT_NEAREST;
   uint32_t addr31_5;
   uint32_t addr48_32;
   uint32_t pitch_bits20_5;
   uint32_t pitch_bit21;

   if (pitch < 32)
      pitch = 32;
   pitch &= ~31u;

   memset(e, 0, sizeof(*e));

   /* TEXSAMP0: ADDRESS_U 2:0, V 5:3, P 8:6
    * TEXSAMP1: MAG 2:0, MIN 5:3, MIP 8:6 (offsets from clc597tex TEXSAMP1) */
   e->samp[0] = (au & 7) | ((av & 7) << 3) | ((ap & 7) << 6);
   e->samp[1] = (mag & 7) | ((minf & 7) << 3) | ((mip & 7) << 6);
   /* LOD bias / min / max — coarse fixed-point in samp[2]/samp[3] later */

   /* --- TEXHEAD_PITCH via MW positions from clc597tex.h --- */
   /* COMPONENTS MW(6:0) */
   nv_tex_mw_set_u32(e->head, 6, 0, comp);
   /* R/G/B/A_DATA_TYPE MW(9:7)(12:10)(15:13)(18:16) */
   nv_tex_mw_set_u32(e->head, 9, 7, dt);
   nv_tex_mw_set_u32(e->head, 12, 10, dt);
   nv_tex_mw_set_u32(e->head, 15, 13, dt);
   nv_tex_mw_set_u32(e->head, 18, 16, dt);
   /* X/Y/Z/W_SOURCE MW(21:19)(24:22)(27:25)(30:28) */
   nv_tex_mw_set_u32(e->head, 21, 19, sx);
   nv_tex_mw_set_u32(e->head, 24, 22, sy);
   nv_tex_mw_set_u32(e->head, 27, 25, sz);
   nv_tex_mw_set_u32(e->head, 30, 28, sw);

   /* ADDRESS_BITS31TO5 MW(63:37) — 27 bits of (addr >> 5) */
   addr31_5 = (uint32_t)((addr >> 5) & 0x7ffffffu);
   nv_tex_mw_set_u32(e->head, 63, 37, addr31_5);
   /* ADDRESS_BITS48TO32 MW(80:64) — 17 bits */
   addr48_32 = (uint32_t)((addr >> 32) & 0x1ffffu);
   nv_tex_mw_set_u32(e->head, 80, 64, addr48_32);
   /* HEADER_VERSION MW(87:85) = SELECT_PITCH (2) */
   nv_tex_mw_set_u32(e->head, 87, 85, NV_TEX_HEADER_VERSION_PITCH);

   /* PITCH_BITS20TO5 MW(111:96) */
   pitch_bits20_5 = (pitch >> 5) & 0xffffu;
   nv_tex_mw_set_u32(e->head, 111, 96, pitch_bits20_5);
   /* PITCH_BIT21 MW(145:145) */
   pitch_bit21 = (pitch >> 21) & 1u;
   nv_tex_mw_set_u32(e->head, 145, 145, pitch_bit21);

   /* WIDTH_MINUS_ONE MW(144:128) — 17 bits */
   nv_tex_mw_set_u32(e->head, 144, 128, w1 & 0x1ffffu);
   /* HEIGHT_MINUS_ONE_BIT16 MW(146:146) if height > 65536 */
   if (h1 & 0x10000u)
      nv_tex_mw_set_u32(e->head, 146, 146, 1);
   /* TEXTURE_TYPE MW(154:151) */
   nv_tex_mw_set_u32(e->head, 154, 151, ttype & 0xfu);
   /* HEIGHT_MINUS_ONE MW(175:160) — low 16 bits */
   nv_tex_mw_set_u32(e->head, 175, 160, h1 & 0xffffu);
   /* NORMALIZED_COORDS MW(191:191) */
   if (d->normalized_coords)
      nv_tex_mw_set_u32(e->head, 191, 191, 1);
   /* S_R_G_B_CONVERSION MW(123:123) if present in pitch header — set when sRGB */
   if (d->s_r_g_b_conversion)
      nv_tex_mw_set_u32(e->head, 123, 123, 1);
}

/** Map common pipe_format numeric id to tex component size + data type. */
static inline void
nv_tex_format_from_pipe(unsigned pipe_fmt, uint8_t *comp_out, uint8_t *dt_out)
{
   uint8_t comp = NV_TEX_COMP_A8B8G8R8;
   uint8_t dt = NV_TEX_DT_UNORM;
   switch (pipe_fmt) {
   case 1: case 9: case 3: case 4: case 10:
      comp = NV_TEX_COMP_A8B8G8R8; dt = NV_TEX_DT_UNORM; break;
   case 5:
      comp = NV_TEX_COMP_B5G6R5; dt = NV_TEX_DT_UNORM; break;
   case 31:
      comp = NV_TEX_COMP_R32; dt = NV_TEX_DT_FLOAT; break;
   case 38:
      comp = NV_TEX_COMP_R16G16B16A16; dt = NV_TEX_DT_FLOAT; break;
   case 39:
      comp = NV_TEX_COMP_R32G32B32A32; dt = NV_TEX_DT_FLOAT; break;
   case 19:
      comp = NV_TEX_COMP_R8; dt = NV_TEX_DT_UNORM; break;
   case 16:
      comp = NV_TEX_COMP_G8R8; dt = NV_TEX_DT_UNORM; break;
   case 55:
      comp = NV_TEX_COMP_Z24S8; dt = NV_TEX_DT_UINT; break;
   case 56:
      comp = NV_TEX_COMP_ZF32; dt = NV_TEX_DT_FLOAT; break;
   case 57:
      comp = NV_TEX_COMP_Z16; dt = NV_TEX_DT_UNORM; break;
   default:
      break;
   }
   if (comp_out) *comp_out = comp;
   if (dt_out) *dt_out = dt;
}

struct nv_tex_pool *nv_tex_pool_create(struct nv_rm_device *rm, uint32_t num_entries);
void nv_tex_pool_destroy(struct nv_tex_pool *pool);

/** Write entry at slot; returns slot index or -1.  slot=-1 allocates next. */
int nv_tex_pool_set_entry(struct nv_tex_pool *pool, int slot,
                          const struct nv_tex_entry *entry);

/** Emit SET_TEX_SAMPLER_POOL + SET_TEX_HEADER_POOL + sampler binding VIA_HEADER. */
void nv_tex_pool_emit_bind(struct nv_push *p, struct nv_tex_pool *pool);

/** Invalidate sampler + texture header caches (all lines). */
static inline void
nv_tex_invalidate_caches(struct nv_push *p)
{
   nv_push_method(p, NVC597_INVALIDATE_SAMPLER_CACHE, 0);
   nv_push_method(p, NVC597_INVALIDATE_TEXTURE_HEADER_CACHE, 0);
}

#ifdef __cplusplus
}
#endif

#endif /* NV_TEX_H */
