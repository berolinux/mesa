/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Maxwell+ Shader Program Header (SPH) builder + minimal SASS stubs.
 * SPH is the first 20 dwords (80 bytes) of a shader object on Maxwell+;
 * program address points at the SPH; code follows after sph_size.
 *
 * Field layout derived from open-gpu-kernel-modules class headers and
 * known public SPH documentation (type/version/register/local/crs/ots
 * fields used by nvidia-3d channel init + proprietary compiler output).
 * Full NIR->SASS is incremental; this provides a valid bindable object.
 */

#ifndef NV_SPH_H
#define NV_SPH_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NV_SPH_DWORDS              20
#define NV_SPH_BYTES               (NV_SPH_DWORDS * 4)
#define NV_SPH_CODE_ALIGN          32
#define NV_SPH_TOTAL_MIN_BYTES     256

/* SPH type field (word0 bits 3:0) — matches hardware shader type encoding */
#define NV_SPH_TYPE_VERTEX         0x1
#define NV_SPH_TYPE_TESS_INIT      0x2
#define NV_SPH_TYPE_TESS           0x3
#define NV_SPH_TYPE_GEOMETRY       0x4
#define NV_SPH_TYPE_PIXEL          0x5
#define NV_SPH_TYPE_COMPUTE        0x6

/* SPH version (word0 bits 7:4): Maxwell=3, Pascal=4, Volta/Turing/Ampere=5+ */
#define NV_SPH_VERSION_MAXWELL     3
#define NV_SPH_VERSION_PASCAL      4
#define NV_SPH_VERSION_VOLTA_PLUS  5

/* Minimal SASS: EXIT instruction (common encoding across SM 5.0–8.x for EXIT) */
#define NV_SASS_EXIT_LO            0x00000000u
#define NV_SASS_EXIT_HI            0x50b00000u  /* EXIT with default predicates */

struct nv_sph_info {
   uint8_t  type;              /* NV_SPH_TYPE_* */
   uint8_t  sph_version;       /* NV_SPH_VERSION_* */
   uint16_t register_count;    /* total registers (R0..Rn-1), min 4 */
   uint16_t barrier_count;
   uint32_t local_mem_low_size;
   uint32_t local_mem_high_size;
   uint32_t local_mem_crs_size;
   uint32_t ots_offset;        /* offset table start, typically SPH_BYTES */
   uint16_t store_req_start;
   uint16_t store_req_end;
   bool     does_global_store;
   bool     sass_level_2;
   /* Per-IO map bits — left zero for stub shaders */
   uint32_t vmap_lo;
   uint32_t vmap_hi;
};

struct nv_sph_blob {
   uint32_t sph[NV_SPH_DWORDS];
   uint32_t sass[16];          /* up to 8 instructions (16 dwords) */
   uint32_t sass_dwords;
   uint32_t total_bytes;       /* SPH + pad + sass, aligned */
};

static inline void
nv_sph_info_defaults(struct nv_sph_info *info, uint8_t type)
{
   memset(info, 0, sizeof(*info));
   info->type = type;
   info->sph_version = NV_SPH_VERSION_VOLTA_PLUS;
   info->register_count = 8;
   info->ots_offset = NV_SPH_BYTES;
   info->store_req_start = 0xff;
   info->store_req_end = 0xff;
}

/**
 * Encode SPH words.  Bit positions follow the Maxwell+ SPH layout used by
 * the proprietary compiler and mirrored in open research / nouveau/nvk
 * (cross-checked against SPH_VERSION method requirement on the 3D class).
 *
 * Word 0: type[3:0] | version[7:4] | sass_level_2[10] | does_global_store[11]
 * Word 1: register_count[5:0] | barrier_count[12:8]
 * Word 2: shader_local_memory_low_size
 * Word 3: shader_local_memory_high_size
 * Word 4: shader_local_memory_crs_size
 * Word 5: ots_offset / program entry relative offset in some gens
 * Word 6: store_req_start[7:0] | store_req_end[15:8]
 * Words 7-19: I/O maps, per-stage fields (zeroed for minimal stub)
 */
static inline void
nv_sph_encode(const struct nv_sph_info *info, uint32_t sph_out[NV_SPH_DWORDS])
{
   uint32_t regs = info->register_count ? info->register_count : 4;
   if (regs < 4)
      regs = 4;
   if (regs > 255)
      regs = 255;

   memset(sph_out, 0, NV_SPH_BYTES);

   sph_out[0] = (info->type & 0xf) |
                ((info->sph_version & 0xf) << 4) |
                (info->sass_level_2 ? (1u << 10) : 0) |
                (info->does_global_store ? (1u << 11) : 0);

   sph_out[1] = (regs & 0x3f) |
                ((info->barrier_count & 0x1f) << 8);

   sph_out[2] = info->local_mem_low_size;
   sph_out[3] = info->local_mem_high_size;
   sph_out[4] = info->local_mem_crs_size;
   sph_out[5] = info->ots_offset ? info->ots_offset : NV_SPH_BYTES;
   sph_out[6] = (info->store_req_start & 0xff) |
                ((info->store_req_end & 0xff) << 8);
   sph_out[7] = info->vmap_lo;
   sph_out[8] = info->vmap_hi;
}

/** Append EXIT; total_bytes = SPH + aligned sass tail. */
static inline void
nv_sph_build_trivial(struct nv_sph_blob *blob, uint8_t type, uint16_t regs)
{
   struct nv_sph_info info;
   uint32_t code_off, total;

   nv_sph_info_defaults(&info, type);
   info.register_count = regs ? regs : 8;
   nv_sph_encode(&info, blob->sph);

   /* Code starts at SPH_BYTES (ots_offset).  Pad with NOP-like zeros then EXIT. */
   memset(blob->sass, 0, sizeof(blob->sass));
   blob->sass[0] = NV_SASS_EXIT_LO;
   blob->sass[1] = NV_SASS_EXIT_HI;
   blob->sass_dwords = 2;

   code_off = NV_SPH_BYTES;
   total = code_off + blob->sass_dwords * 4;
   if (total < NV_SPH_TOTAL_MIN_BYTES)
      total = NV_SPH_TOTAL_MIN_BYTES;
   total = (total + NV_SPH_CODE_ALIGN - 1) & ~(NV_SPH_CODE_ALIGN - 1);
   blob->total_bytes = total;
}

/** Serialise SPH+sass into a contiguous buffer (caller provides >= total_bytes). */
static inline void
nv_sph_serialise(const struct nv_sph_blob *blob, void *dst, uint32_t dst_size)
{
   uint8_t *d = (uint8_t *)dst;
   if (!d || dst_size < NV_SPH_BYTES)
      return;
   memset(d, 0, dst_size);
   memcpy(d, blob->sph, NV_SPH_BYTES);
   if (dst_size >= NV_SPH_BYTES + blob->sass_dwords * 4)
      memcpy(d + NV_SPH_BYTES, blob->sass, blob->sass_dwords * 4);
}

static inline uint8_t
nv_sph_type_from_shader_kind(int kind)
{
   /* mirrors enum nv_shader_kind */
   switch (kind) {
   case 0: return NV_SPH_TYPE_VERTEX;
   case 1: return NV_SPH_TYPE_PIXEL;
   case 2: return NV_SPH_TYPE_GEOMETRY;
   case 3: return NV_SPH_TYPE_TESS_INIT;
   case 4: return NV_SPH_TYPE_TESS;
   case 5: return NV_SPH_TYPE_COMPUTE;
   default: return NV_SPH_TYPE_VERTEX;
   }
}

#ifdef __cplusplus
}
#endif

#endif /* NV_SPH_H */
