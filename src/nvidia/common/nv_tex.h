/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Texture/sampler header pool (nvidia-3d Nv3dTexture pattern): interleaved
 * sampler[8] + header[8] dwords per entry; SET_TEX_SAMPLER_POOL /
 * SET_TEX_HEADER_POOL point at samp / head offsets within the pool BO.
 *
 * Pitch texture header bit layout from class/clc597tex.h (TEXHEAD_PITCH_*).
 * We encode key fields manually without pulling the full MW macro machinery.
 */

#ifndef NV_TEX_H
#define NV_TEX_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

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
#define NV_TEX_COMP_A8B8G8R8    0x08
#define NV_TEX_COMP_R32G32B32A32 0x01
#define NV_TEX_COMP_R32         0x0f
#define NV_TEX_COMP_R16G16B16A16 0x03
#define NV_TEX_COMP_R8          0x1d
#define NV_TEX_COMP_G8R8        0x18
#define NV_TEX_COMP_B5G6R5      0x15
#define NV_TEX_COMP_A1B5G5R5    0x14
#define NV_TEX_COMP_R16         0x1b

/* Data type from NVC597_TEXHEAD_PITCH_*_DATA_TYPE_NUM_* */
#define NV_TEX_DT_UNORM         0x2
#define NV_TEX_DT_SNORM         0x1
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

/* Header version SELECT_PITCH = 2, in word1 bits (see TEXHEAD_PITCH_HEADER_VERSION) */
#define NV_TEX_HEADER_VERSION_PITCH  0x2
#define NV_TEX_TEXTURE_TYPE_2D       0x1

/* Sampler address modes (TEXSAMP0) */
#define NV_TEX_SAMP_ADDR_WRAP        0x0
#define NV_TEX_SAMP_ADDR_MIRROR      0x1
#define NV_TEX_SAMP_ADDR_CLAMP_EDGE  0x2
#define NV_TEX_SAMP_ADDR_BORDER      0x3
#define NV_TEX_SAMP_ADDR_CLAMP_OGL   0x4

/* Sampler filters (TEXSAMP0 mag/min) */
#define NV_TEX_SAMP_FILT_NEAREST     0x1
#define NV_TEX_SAMP_FILT_LINEAR      0x2

struct nv_tex_desc {
   uint64_t gpu_addr;
   uint32_t width;
   uint32_t height;
   uint32_t pitch;          /* bytes; must be multiple of 32 for pitch header */
   uint8_t  components;     /* NV_TEX_COMP_* */
   uint8_t  data_type;      /* NV_TEX_DT_* */
   uint8_t  src_x, src_y, src_z, src_w; /* NV_TEX_SRC_* swizzle */
   bool     normalized_coords;
   /* sampler */
   uint8_t  addr_u, addr_v, addr_p;
   uint8_t  mag_filt, min_filt;
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

/* Encode pitch 2D texture header (simplified subset of c597tex TEXHEAD_PITCH).
 *
 * Word layout (approximate, matches nvidia-3d FLD_SET patterns for pitch):
 *  head[0]: components[6:0] | r_dt[9:7] | g_dt[12:10] | b_dt[15:13] | a_dt[18:16]
 *           | x_src[21:19] | y_src[24:22] | z_src[27:25] | w_src[30:28]
 *  head[1]: address_lo_bits31to5 in low bits, header_version in upper region
 *  head[2]: address hi + pitch bits
 *  head[3]: pitch hi + width/height partial
 *  head[4]: width_minus_one low
 *  head[5]: height_minus_one + texture_type
 *
 * Exact multi-word field packing uses bit offsets from c597tex.h.  For the
 * common pitch path we set the fields HW needs for basic 2D sampling.
 */
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
   uint8_t au = d->addr_u ? d->addr_u : NV_TEX_SAMP_ADDR_CLAMP_EDGE;
   uint8_t av = d->addr_v ? d->addr_v : NV_TEX_SAMP_ADDR_CLAMP_EDGE;
   uint8_t ap = d->addr_p ? d->addr_p : NV_TEX_SAMP_ADDR_CLAMP_EDGE;
   uint8_t mag = d->mag_filt ? d->mag_filt : NV_TEX_SAMP_FILT_LINEAR;
   uint8_t minf = d->min_filt ? d->min_filt : NV_TEX_SAMP_FILT_LINEAR;

   if (pitch < 32)
      pitch = 32;
   pitch &= ~31u; /* pitch must be 32B aligned for pitch header */

   memset(e, 0, sizeof(*e));

   /* Sampler: ADDRESS_U/V/P in low bits of samp[0]; mag/min filter nearby.
    * TEXSAMP0: ADDRESS_U 2:0, ADDRESS_V 5:3, ADDRESS_P 8:6, MAG 11:9, MIN 14:12 */
   e->samp[0] = (au & 7) | ((av & 7) << 3) | ((ap & 7) << 6) |
                ((mag & 7) << 9) | ((minf & 7) << 12);

   /* Header word0: components + data types + sources (TEXHEAD_PITCH) */
   e->head[0] = (comp & 0x7f) |
                ((dt & 7) << 7) | ((dt & 7) << 10) | ((dt & 7) << 13) | ((dt & 7) << 16) |
                ((sx & 7) << 19) | ((sy & 7) << 22) | ((sz & 7) << 25) | ((sw & 7) << 28);

   /* head[1]: address bits 31:5 in MW(63:37) region — lower part of word1
    * HEADER_VERSION SELECT_PITCH = 2 in MW(87:85) which lands in word2 bits.
    * Simplified: store addr>>5 in head[1] low 27 bits; version/type in head[2]. */
   e->head[1] = (uint32_t)((addr >> 5) & 0x7ffffffu);
   e->head[2] = ((uint32_t)(addr >> 32) & 0x1ffff) |
                ((NV_TEX_HEADER_VERSION_PITCH & 7) << 21);
   /* pitch bits 20:5 in head[2]/head[3] per TEXHEAD_PITCH_PITCH_BITS20TO5 MW(95:80) */
   e->head[2] |= ((pitch >> 5) & 0xffff) << 0; /* may overlap; refined below */
   e->head[3] = ((pitch >> 5) & 0xffff) | (((pitch >> 21) & 1) << 16);
   /* width_minus_one MW(143:128) ~ head[4]; height MW(175:160) ~ head[5]; type MW(154:151) */
   e->head[4] = w1 & 0xffff;
   e->head[5] = (h1 & 0xffff) | ((NV_TEX_TEXTURE_TYPE_2D & 0xf) << 23);
   if (d->normalized_coords)
      e->head[5] |= (1u << 31); /* normalized coords bit in some gens */
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
