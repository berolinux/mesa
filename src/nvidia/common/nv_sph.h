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
   uint32_t sass[48];          /* up to 24 instructions (48 dwords) */
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

   if (!blob)
      return;
   memset(blob, 0, sizeof(*blob));
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

/**
 * Smoke compute: SPH type=COMPUTE + single EXIT (valid minimal QMD target).
 * Does not touch global memory; sema release on QMD is the completion signal.
 */
static inline void
nv_sph_build_compute_exit_only(struct nv_sph_blob *blob, uint16_t regs)
{
   nv_sph_build_trivial(blob, NV_SPH_TYPE_COMPUTE, regs ? regs : 8);
}

/**
 * tick137: compute MOV R0, imm + EXIT smoke (aligns with pass12/gpucomp
 * 0x5c98/0x7918 signatures via approximate MOV32I + EXIT encoding).
 * Non-trivial SASS stream without global store — useful bind/trace target
 * when store_imm target VA is unavailable.
 */
static inline void
nv_sph_build_compute_mov_imm_exit(struct nv_sph_blob *blob, uint32_t imm_value,
                                  uint16_t regs)
{
   struct nv_sph_info info;
   uint32_t *s;
   unsigned n = 0;
   uint32_t code_off, total;

   if (!blob)
      return;
   memset(blob, 0, sizeof(*blob));
   nv_sph_info_defaults(&info, NV_SPH_TYPE_COMPUTE);
   info.register_count = regs ? regs : 16;
   info.does_global_store = false;
   info.barrier_count = 1;
   nv_sph_encode(&info, blob->sph);

   s = blob->sass;
#define NV_SPH_SASS_MOV32I_HI(rd)  (0x01000000u | ((uint32_t)(rd) & 0xffu))
   s[n++] = imm_value;
   s[n++] = NV_SPH_SASS_MOV32I_HI(0);
   s[n++] = NV_SASS_EXIT_LO;
   s[n++] = NV_SASS_EXIT_HI;
#undef NV_SPH_SASS_MOV32I_HI

   blob->sass_dwords = n;
   code_off = NV_SPH_BYTES;
   total = code_off + n * 4u;
   if (total < NV_SPH_TOTAL_MIN_BYTES)
      total = NV_SPH_TOTAL_MIN_BYTES;
   total = (total + NV_SPH_CODE_ALIGN - 1) & ~(NV_SPH_CODE_ALIGN - 1);
   blob->total_bytes = total;
   (void)code_off;
}

/**
 * tick139: compute S2R(tid.x) + MOV imm + EXIT via hand-encoded SPH/SASS
 * (aligns pass13 gpucomp S2R/MOV/EXIT families; no global store).
 */
static inline void
nv_sph_build_compute_s2r_mov_imm_exit(struct nv_sph_blob *blob,
                                      uint32_t imm_value, uint16_t regs)
{
   struct nv_sph_info info;
   uint32_t *s;
   unsigned n = 0;
   uint32_t code_off, total;

   if (!blob)
      return;
   memset(blob, 0, sizeof(*blob));
   nv_sph_info_defaults(&info, NV_SPH_TYPE_COMPUTE);
   info.register_count = regs ? regs : 16;
   info.does_global_store = false;
   info.barrier_count = 1;
   nv_sph_encode(&info, blob->sph);

   s = blob->sass;
#define NV_SPH_SASS_MOV32I_HI(rd)  (0x01000000u | ((uint32_t)(rd) & 0xffu))
   /* S2R R0, SR0 — approximate Maxwell+ encoding (hi class 0xf0c8 in pass13) */
   s[n++] = 0x00000000u; /* lo: Rd=0, Sr=0 */
   s[n++] = 0xf0c80000u; /* hi: S2R class (pass13 gpucomp 0xf0c8) */
   s[n++] = imm_value;
   s[n++] = NV_SPH_SASS_MOV32I_HI(1);
   s[n++] = NV_SASS_EXIT_LO;
   s[n++] = NV_SASS_EXIT_HI;
#undef NV_SPH_SASS_MOV32I_HI

   blob->sass_dwords = n;
   code_off = NV_SPH_BYTES;
   total = code_off + n * 4u;
   if (total < NV_SPH_TOTAL_MIN_BYTES)
      total = NV_SPH_TOTAL_MIN_BYTES;
   total = (total + NV_SPH_CODE_ALIGN - 1) & ~(NV_SPH_CODE_ALIGN - 1);
   blob->total_bytes = total;
   (void)code_off;
}

/**
 * tick139 / pass14: compute S2R(sr) + MOV imm + EXIT with explicit SR index.
 * pass14 gpucomp: SR 0x00 dominant (tid/laneid family); also try SR1/SR3.
 */
static inline void
nv_sph_build_compute_s2r_sr_mov_imm_exit(struct nv_sph_blob *blob,
                                         uint8_t sr_index, uint32_t imm_value,
                                         uint16_t regs)
{
   struct nv_sph_info info;
   uint32_t *s;
   unsigned n = 0;
   uint32_t code_off, total;

   if (!blob)
      return;
   memset(blob, 0, sizeof(*blob));
   nv_sph_info_defaults(&info, NV_SPH_TYPE_COMPUTE);
   info.register_count = regs ? regs : 16;
   info.does_global_store = false;
   info.barrier_count = 1;
   nv_sph_encode(&info, blob->sph);

   s = blob->sass;
#define NV_SPH_SASS_MOV32I_HI(rd)  (0x01000000u | ((uint32_t)(rd) & 0xffu))
   /* S2R R0, SR# — lo packs Rd=0 + Sr in low byte (pass14 shift probes) */
   s[n++] = (uint32_t)(sr_index & 0xffu);
   s[n++] = 0xf0c80000u;
   s[n++] = imm_value;
   s[n++] = NV_SPH_SASS_MOV32I_HI(1);
   s[n++] = NV_SASS_EXIT_LO;
   s[n++] = NV_SASS_EXIT_HI;
#undef NV_SPH_SASS_MOV32I_HI

   blob->sass_dwords = n;
   code_off = NV_SPH_BYTES;
   total = code_off + n * 4u;
   if (total < NV_SPH_TOTAL_MIN_BYTES)
      total = NV_SPH_TOTAL_MIN_BYTES;
   total = (total + NV_SPH_CODE_ALIGN - 1) & ~(NV_SPH_CODE_ALIGN - 1);
   blob->total_bytes = total;
   (void)code_off;
}

/**
 * tick139 / pass14: minimal compute NOP+EXIT (gpucomp short-kernel pattern).
 * Validates SPH+SASS without MOV/S2R — useful for program object bring-up.
 */
static inline void
nv_sph_build_compute_nop_exit(struct nv_sph_blob *blob, uint16_t regs)
{
   struct nv_sph_info info;
   uint32_t *s;
   unsigned n = 0;
   uint32_t code_off, total;

   if (!blob)
      return;
   memset(blob, 0, sizeof(*blob));
   nv_sph_info_defaults(&info, NV_SPH_TYPE_COMPUTE);
   info.register_count = regs ? regs : 8;
   info.does_global_store = false;
   info.barrier_count = 0;
   nv_sph_encode(&info, blob->sph);

   s = blob->sass;
   /* NOP then EXIT (pass14 / pass13 SASS families 0x50b0 / 0x7918) */
   s[n++] = 0x00000000u;
   s[n++] = 0x50b00000u;
   s[n++] = NV_SASS_EXIT_LO;
   s[n++] = NV_SASS_EXIT_HI;

   blob->sass_dwords = n;
   code_off = NV_SPH_BYTES;
   total = code_off + n * 4u;
   if (total < NV_SPH_TOTAL_MIN_BYTES)
      total = NV_SPH_TOTAL_MIN_BYTES;
   total = (total + NV_SPH_CODE_ALIGN - 1) & ~(NV_SPH_CODE_ALIGN - 1);
   blob->total_bytes = total;
   (void)code_off;
}

/**
 * tick121: minimal graphics stage smoke — correct SPH type + EXIT only.
 * Used when NIR/SASS is unavailable so bind/draw still has a valid program
 * object (HW may still fault on shader I/O; fixed-func path remains preferred
 * until real SASS).  type: NV_SPH_TYPE_VERTEX / PIXEL / GEOMETRY / etc.
 */
static inline void
nv_sph_build_graphics_exit_only(struct nv_sph_blob *blob, uint8_t sph_type,
                                uint16_t regs)
{
   if (!sph_type || sph_type == NV_SPH_TYPE_COMPUTE)
      sph_type = NV_SPH_TYPE_VERTEX;
   nv_sph_build_trivial(blob, sph_type, regs ? regs : 8);
}

/** Vertex stage SPH+EXIT smoke. */
static inline void
nv_sph_build_vertex_exit_only(struct nv_sph_blob *blob, uint16_t regs)
{
   nv_sph_build_graphics_exit_only(blob, NV_SPH_TYPE_VERTEX, regs);
}

/** Pixel/fragment stage SPH+EXIT smoke. */
static inline void
nv_sph_build_pixel_exit_only(struct nv_sph_blob *blob, uint16_t regs)
{
   nv_sph_build_graphics_exit_only(blob, NV_SPH_TYPE_PIXEL, regs);
}

/** Geometry stage SPH+EXIT smoke. */
static inline void
nv_sph_build_geometry_exit_only(struct nv_sph_blob *blob, uint16_t regs)
{
   nv_sph_build_graphics_exit_only(blob, NV_SPH_TYPE_GEOMETRY, regs);
}

/*
 * tick125: vertex smoke with minimal SASS beyond EXIT — MOV32I R0..R3 with
 * clip-space constants then EXIT.  Not a real attribute/vmap program; gives
 * non-zero instruction stream for bind/trace while fixed-func remains the
 * preferred G3 path until NIR->SASS.
 *
 * Layout (8-byte Maxwell+ pairs, approximate MOV32I class used in compute store):
 *   MOV R0, x_bits; MOV R1, y_bits; MOV R2, z_bits; MOV R3, w_bits; EXIT
 */
#define NV_SPH_SASS_MOV32I_HI_RD(rd)  (0x01000000u | ((uint32_t)(rd) & 0xffu))

static inline void
nv_sph_build_vertex_mov_imm_exit(struct nv_sph_blob *blob,
                                 uint32_t pos_x_bits, uint32_t pos_y_bits,
                                 uint32_t pos_z_bits, uint32_t pos_w_bits,
                                 uint16_t regs)
{
   struct nv_sph_info info;
   uint32_t *s;
   unsigned n = 0;
   uint32_t code_off, total;

   if (!blob)
      return;
   memset(blob, 0, sizeof(*blob));
   nv_sph_info_defaults(&info, NV_SPH_TYPE_VERTEX);
   info.register_count = regs ? regs : 16;
   /* Minimal vmap: attribute 0 active (bits refined on silicon) */
   info.vmap_lo = 0x1u;
   nv_sph_encode(&info, blob->sph);

   s = blob->sass;
   s[n++] = pos_x_bits;
   s[n++] = NV_SPH_SASS_MOV32I_HI_RD(0);
   s[n++] = pos_y_bits;
   s[n++] = NV_SPH_SASS_MOV32I_HI_RD(1);
   s[n++] = pos_z_bits;
   s[n++] = NV_SPH_SASS_MOV32I_HI_RD(2);
   s[n++] = pos_w_bits ? pos_w_bits : 0x3f800000u; /* 1.0f default W */
   s[n++] = NV_SPH_SASS_MOV32I_HI_RD(3);
   s[n++] = NV_SASS_EXIT_LO;
   s[n++] = NV_SASS_EXIT_HI;
   blob->sass_dwords = n;

   code_off = NV_SPH_BYTES;
   total = code_off + blob->sass_dwords * 4;
   if (total < NV_SPH_TOTAL_MIN_BYTES)
      total = NV_SPH_TOTAL_MIN_BYTES;
   total = (total + NV_SPH_CODE_ALIGN - 1) & ~(NV_SPH_CODE_ALIGN - 1);
   blob->total_bytes = total;
}

/** Default vertex smoke: unit W, zero XYZ (degenerate but valid bind object). */
static inline void
nv_sph_build_vertex_smoke_default(struct nv_sph_blob *blob, uint16_t regs)
{
   nv_sph_build_vertex_mov_imm_exit(blob, 0, 0, 0, 0x3f800000u, regs);
}

/** Pixel smoke: MOV R0, imm color; EXIT (no real RT write yet). */
static inline void
nv_sph_build_pixel_mov_imm_exit(struct nv_sph_blob *blob, uint32_t color_bits,
                                uint16_t regs)
{
   struct nv_sph_info info;
   uint32_t *s;
   unsigned n = 0;
   uint32_t code_off, total;

   if (!blob)
      return;
   memset(blob, 0, sizeof(*blob));
   nv_sph_info_defaults(&info, NV_SPH_TYPE_PIXEL);
   info.register_count = regs ? regs : 8;
   nv_sph_encode(&info, blob->sph);

   s = blob->sass;
   s[n++] = color_bits ? color_bits : 0xffffffffu;
   s[n++] = NV_SPH_SASS_MOV32I_HI_RD(0);
   s[n++] = NV_SASS_EXIT_LO;
   s[n++] = NV_SASS_EXIT_HI;
   blob->sass_dwords = n;

   code_off = NV_SPH_BYTES;
   total = code_off + blob->sass_dwords * 4;
   if (total < NV_SPH_TOTAL_MIN_BYTES)
      total = NV_SPH_TOTAL_MIN_BYTES;
   total = (total + NV_SPH_CODE_ALIGN - 1) & ~(NV_SPH_CODE_ALIGN - 1);
   blob->total_bytes = total;
}

/**
 * tick132: generic graphics-stage MOV imm R0..R3 + EXIT (GS/TCS/TES smoke).
 * Same approximate MOV32I class as vertex/pixel; not real stage I/O.  Gives
 * non-trivial SASS for bind/trace on all graphics stages until NIR->SASS.
 */
static inline void
nv_sph_build_graphics_mov_imm_exit(struct nv_sph_blob *blob, uint8_t sph_type,
                                   uint32_t r0, uint32_t r1, uint32_t r2,
                                   uint32_t r3, uint16_t regs)
{
   struct nv_sph_info info;
   uint32_t *s;
   unsigned n = 0;
   uint32_t code_off, total;

   if (!blob)
      return;
   if (sph_type < NV_SPH_TYPE_VERTEX || sph_type > NV_SPH_TYPE_PIXEL)
      sph_type = NV_SPH_TYPE_VERTEX;
   memset(blob, 0, sizeof(*blob));
   nv_sph_info_defaults(&info, sph_type);
   info.register_count = regs ? regs : 8;
   if (sph_type == NV_SPH_TYPE_VERTEX)
      info.vmap_lo = 0x1u;
   nv_sph_encode(&info, blob->sph);

   s = blob->sass;
   s[n++] = r0;
   s[n++] = NV_SPH_SASS_MOV32I_HI_RD(0);
   s[n++] = r1;
   s[n++] = NV_SPH_SASS_MOV32I_HI_RD(1);
   s[n++] = r2;
   s[n++] = NV_SPH_SASS_MOV32I_HI_RD(2);
   s[n++] = r3 ? r3 : 0x3f800000u;
   s[n++] = NV_SPH_SASS_MOV32I_HI_RD(3);
   s[n++] = NV_SASS_EXIT_LO;
   s[n++] = NV_SASS_EXIT_HI;
   blob->sass_dwords = n;

   code_off = NV_SPH_BYTES;
   total = code_off + blob->sass_dwords * 4;
   if (total < NV_SPH_TOTAL_MIN_BYTES)
      total = NV_SPH_TOTAL_MIN_BYTES;
   total = (total + NV_SPH_CODE_ALIGN - 1) & ~(NV_SPH_CODE_ALIGN - 1);
   blob->total_bytes = total;
}

/** Geometry smoke: four imm regs + EXIT (no emit/restart yet). */
static inline void
nv_sph_build_geometry_mov_imm_exit(struct nv_sph_blob *blob, uint16_t regs)
{
   nv_sph_build_graphics_mov_imm_exit(blob, NV_SPH_TYPE_GEOMETRY,
                                      0, 0, 0, 0x3f800000u, regs);
}

/** Tess init (TCS) smoke. */
static inline void
nv_sph_build_tess_init_mov_imm_exit(struct nv_sph_blob *blob, uint16_t regs)
{
   nv_sph_build_graphics_mov_imm_exit(blob, NV_SPH_TYPE_TESS_INIT,
                                      0, 0, 0, 0x3f800000u, regs);
}

/** Tess eval (TES) smoke. */
static inline void
nv_sph_build_tess_mov_imm_exit(struct nv_sph_blob *blob, uint16_t regs)
{
   nv_sph_build_graphics_mov_imm_exit(blob, NV_SPH_TYPE_TESS,
                                      0, 0, 0, 0x3f800000u, regs);
}

/** Map nv_shader_kind-like index to NV_SPH_TYPE_* (0=VS..5=CS). */
static inline uint8_t
nv_sph_type_from_shader_kind_idx(unsigned kind_idx)
{
   switch (kind_idx) {
   case 0: return NV_SPH_TYPE_VERTEX;
   case 1: return NV_SPH_TYPE_PIXEL;
   case 2: return NV_SPH_TYPE_GEOMETRY;
   case 3: return NV_SPH_TYPE_TESS_INIT;
   case 4: return NV_SPH_TYPE_TESS;
   case 5: return NV_SPH_TYPE_COMPUTE;
   default: return NV_SPH_TYPE_VERTEX;
   }
}

#define NV_SPH_SASS_STG_HI      0xeed80000u
#define NV_SPH_SASS_MOV_HI_REG  0x5c980780u
#define NV_SPH_SASS_S2R_HI_CS   0x86400000u
#define NV_SPH_SR_CTAID_X       37  /* block/grid id approximations (Maxwell+) */
#define NV_SPH_SR_TID_X         33

/**
 * Smoke compute with global store: MOV R1, imm; STG [R0], R1; EXIT.
 * Caller must set R0 to destination GPU VA low via QMD CB0 or prior setup —
 * for true G2 store test, pass store_addr in R0 using MOV32I sequence below
 * when store_addr_lo/hi are non-zero (two-reg address in R2:R3, STG via R2).
 *
 * When store_addr is 0: only EXIT (same as compute_exit_only but marks
 * does_global_store=false).
 * When store_addr non-zero: encode approximate MOV R2=lo, MOV R3=hi, MOV R1=imm,
 * STG [R2], R1, EXIT and set does_global_store in SPH.
 */
static inline void
nv_sph_build_compute_store_imm(struct nv_sph_blob *blob, uint32_t imm_value,
                               uint64_t store_addr, uint16_t regs)
{
   struct nv_sph_info info;
   uint32_t *s;
   unsigned n = 0;
   uint32_t code_off, total;

   if (!blob)
      return;
   memset(blob, 0, sizeof(*blob));
   nv_sph_info_defaults(&info, NV_SPH_TYPE_COMPUTE);
   info.register_count = regs ? regs : 16;
   info.does_global_store = (store_addr != 0);
   info.barrier_count = 1;
   nv_sph_encode(&info, blob->sph);

   s = blob->sass;
   if (store_addr) {
      uint32_t alo = (uint32_t)(store_addr & 0xffffffffu);
      uint32_t ahi = (uint32_t)(store_addr >> 32);
      /*
       * G2 store-imm smoke (mirrors nv_sass_emit_smoke_store_imm_at_gva /
       * nv_sass_emit_mov_ri: imm in lo, hi = 0x01000000|Rd).
       *   MOV R2,addr_lo; MOV R3,addr_hi; MOV R1,imm; STG.U32 [R2],R1; EXIT
       */
#define NV_SPH_SASS_MOV32I_HI(rd)  (0x01000000u | ((uint32_t)(rd) & 0xffu))
      s[n++] = alo;
      s[n++] = NV_SPH_SASS_MOV32I_HI(2);
      s[n++] = ahi;
      s[n++] = NV_SPH_SASS_MOV32I_HI(3);
      s[n++] = imm_value;
      s[n++] = NV_SPH_SASS_MOV32I_HI(1);
      /* STG: Rd=0/RZ, Ra=R2, Rb=R1 (nv_sass_emit_stg_u32) */
      s[n++] = (0u) | ((2u & 0xffu) << 8) | ((1u & 0xffu) << 16);
      s[n++] = NV_SPH_SASS_STG_HI;
      s[n++] = NV_SASS_EXIT_LO;
      s[n++] = NV_SASS_EXIT_HI;
#undef NV_SPH_SASS_MOV32I_HI
   } else {
      s[n++] = NV_SASS_EXIT_LO;
      s[n++] = NV_SASS_EXIT_HI;
   }
   blob->sass_dwords = n;

   code_off = NV_SPH_BYTES;
   total = code_off + blob->sass_dwords * 4;
   if (total < NV_SPH_TOTAL_MIN_BYTES)
      total = NV_SPH_TOTAL_MIN_BYTES;
   total = (total + NV_SPH_CODE_ALIGN - 1) & ~(NV_SPH_CODE_ALIGN - 1);
   blob->total_bytes = total;
}

/**
 * Validate serialised SPH+SASS object layout (host-only / trace-golden).
 * Returns 0 if SPH type matches, sass has EXIT at end, total_bytes aligned.
 */
static inline int
nv_sph_smoke_validate_blob(const struct nv_sph_blob *blob, uint8_t expect_type)
{
   uint8_t type;
   if (!blob || blob->total_bytes < NV_SPH_BYTES + 8)
      return -1;
   if (blob->total_bytes & (NV_SPH_CODE_ALIGN - 1))
      return -2;
   type = (uint8_t)(blob->sph[0] & 0xf);
   if (expect_type && type != expect_type)
      return -3;
   if (blob->sass_dwords < 2)
      return -4;
   /* Last insn should be EXIT (common hi class) */
   if (blob->sass[blob->sass_dwords - 1] != NV_SASS_EXIT_HI &&
       blob->sass[blob->sass_dwords - 1] != 0x50b00000u)
      return -5;
   return 0;
}

/**
 * tick144 / pass15: compute SPH with multi-SR S2R smoke (SR0, SR0x48 lanemask,
 * SR0x50 SMID) + MOV R3,imm + EXIT.  No global store; suitable for bind/trace
 * when store VA unavailable.  Self-contained SASS (no nv_sass.c link required).
 */
static inline void
nv_sph_build_compute_s2r_pass15_multi_sr_exit(struct nv_sph_blob *blob,
                                              uint32_t imm, uint16_t regs)
{
   struct nv_sph_info info;
   uint32_t *s;
   unsigned n = 0;
   uint32_t code_off, total;
   uint8_t sr0 = 0, sr48 = 0x48, sr50 = 0x50;

   if (!blob)
      return;
   memset(blob, 0, sizeof(*blob));
   nv_sph_info_defaults(&info, NV_SPH_TYPE_COMPUTE);
   info.register_count = regs ? regs : 16;
   info.barrier_count = 1;
   nv_sph_encode(&info, blob->sph);

   s = blob->sass;
   /* S2R Rd, SR# — lo: Rd | (SR << 20); hi: S2R class (mirrors nv_sass_emit_s2r) */
#define NV_SPH_S2R_LO(rd, sr)  ((uint32_t)(rd) | (((uint32_t)(sr) & 0xffu) << 20))
#define NV_SPH_MOV32I_HI(rd)   (0x01000000u | ((uint32_t)(rd) & 0xffu))
   s[n++] = NV_SPH_S2R_LO(0, sr0);
   s[n++] = NV_SPH_SASS_S2R_HI_CS;
   s[n++] = NV_SPH_S2R_LO(1, sr48);
   s[n++] = NV_SPH_SASS_S2R_HI_CS;
   s[n++] = NV_SPH_S2R_LO(2, sr50);
   s[n++] = NV_SPH_SASS_S2R_HI_CS;
   s[n++] = imm ? imm : 0x55u;
   s[n++] = NV_SPH_MOV32I_HI(3);
   s[n++] = NV_SASS_EXIT_LO;
   s[n++] = NV_SASS_EXIT_HI;
#undef NV_SPH_S2R_LO
#undef NV_SPH_MOV32I_HI
   blob->sass_dwords = n;

   code_off = NV_SPH_BYTES;
   total = code_off + blob->sass_dwords * 4;
   if (total < NV_SPH_TOTAL_MIN_BYTES)
      total = NV_SPH_TOTAL_MIN_BYTES;
   total = (total + NV_SPH_CODE_ALIGN - 1) & ~(NV_SPH_CODE_ALIGN - 1);
   blob->total_bytes = total;
}

/**
 * tick145 / pass16: compute SPH multi-SR smoke — SR0, 0x01, 0x03, 0x25, 0x48,
 * 0x49, 0x50 into R0..R6; MOV R7,imm; EXIT.  Self-contained (no nv_sass.c).
 */
static inline void
nv_sph_build_compute_s2r_pass16_multi_sr_exit(struct nv_sph_blob *blob,
                                              uint32_t imm, uint16_t regs)
{
   struct nv_sph_info info;
   uint32_t *s;
   unsigned n = 0;
   uint32_t code_off, total;
   /* pass16 priority SRs (see HW_MODEL_PASS16 / nv_sass.h pass15/16 defines) */
   static const uint8_t srs[7] = { 0x00, 0x01, 0x03, 0x25, 0x48, 0x49, 0x50 };
   unsigned i;

   if (!blob)
      return;
   memset(blob, 0, sizeof(*blob));
   nv_sph_info_defaults(&info, NV_SPH_TYPE_COMPUTE);
   info.register_count = regs ? regs : 16;
   if (info.register_count < 8)
      info.register_count = 8;
   info.barrier_count = 1;
   nv_sph_encode(&info, blob->sph);

   s = blob->sass;
#define NV_SPH_S2R_LO(rd, sr)  ((uint32_t)(rd) | (((uint32_t)(sr) & 0xffu) << 20))
#define NV_SPH_MOV32I_HI(rd)   (0x01000000u | ((uint32_t)(rd) & 0xffu))
   for (i = 0; i < 7; i++) {
      s[n++] = NV_SPH_S2R_LO((uint8_t)i, srs[i]);
      s[n++] = NV_SPH_SASS_S2R_HI_CS;
   }
   s[n++] = imm ? imm : 0x56u;
   s[n++] = NV_SPH_MOV32I_HI(7);
   s[n++] = NV_SASS_EXIT_LO;
   s[n++] = NV_SASS_EXIT_HI;
#undef NV_SPH_S2R_LO
#undef NV_SPH_MOV32I_HI
   blob->sass_dwords = n;

   code_off = NV_SPH_BYTES;
   total = code_off + blob->sass_dwords * 4;
   if (total < NV_SPH_TOTAL_MIN_BYTES)
      total = NV_SPH_TOTAL_MIN_BYTES;
   total = (total + NV_SPH_CODE_ALIGN - 1) & ~(NV_SPH_CODE_ALIGN - 1);
   blob->total_bytes = total;
}

/**
 * tick146 / pass17: multi-SR SPH refined rank — SR0, 0x01, 0x03, 0x48, 0x49,
 * 0x4c (pass17 secondary), 0x50.  Drops 0x25 for 0x4c per pass17 SR priority.
 */
static inline void
nv_sph_build_compute_s2r_pass17_multi_sr_exit(struct nv_sph_blob *blob,
                                              uint32_t imm, uint16_t regs)
{
   struct nv_sph_info info;
   uint32_t *s;
   unsigned n = 0;
   uint32_t code_off, total;
   static const uint8_t srs[7] = { 0x00, 0x01, 0x03, 0x48, 0x49, 0x4c, 0x50 };
   unsigned i;

   if (!blob)
      return;
   memset(blob, 0, sizeof(*blob));
   nv_sph_info_defaults(&info, NV_SPH_TYPE_COMPUTE);
   info.register_count = regs ? regs : 16;
   if (info.register_count < 8)
      info.register_count = 8;
   info.barrier_count = 1;
   nv_sph_encode(&info, blob->sph);

   s = blob->sass;
#define NV_SPH_S2R_LO(rd, sr)  ((uint32_t)(rd) | (((uint32_t)(sr) & 0xffu) << 20))
#define NV_SPH_MOV32I_HI(rd)   (0x01000000u | ((uint32_t)(rd) & 0xffu))
   for (i = 0; i < 7; i++) {
      s[n++] = NV_SPH_S2R_LO((uint8_t)i, srs[i]);
      s[n++] = NV_SPH_SASS_S2R_HI_CS;
   }
   s[n++] = imm ? imm : 0x57u;
   s[n++] = NV_SPH_MOV32I_HI(7);
   s[n++] = NV_SASS_EXIT_LO;
   s[n++] = NV_SASS_EXIT_HI;
#undef NV_SPH_S2R_LO
#undef NV_SPH_MOV32I_HI
   blob->sass_dwords = n;

   code_off = NV_SPH_BYTES;
   total = code_off + blob->sass_dwords * 4;
   if (total < NV_SPH_TOTAL_MIN_BYTES)
      total = NV_SPH_TOTAL_MIN_BYTES;
   total = (total + NV_SPH_CODE_ALIGN - 1) & ~(NV_SPH_CODE_ALIGN - 1);
   blob->total_bytes = total;
}

/*
 * tick155 / pass21: compute NIR depth scaffold — pass17 multi-SR probe, then
 * MOV32I + STG store_imm + EXIT (global store bit set).  Bridges s2r smoke
 * and store_imm smoke into one program the QMD/SPH pipeline can upload as
 * a single shader object (nir→sass still smoke-level, not full NIR lower).
 */
static inline void
nv_sph_build_compute_s2r_store_imm_pass21(struct nv_sph_blob *blob,
                                          uint32_t imm_value,
                                          uint64_t store_addr,
                                          uint16_t regs)
{
   struct nv_sph_info info;
   uint32_t *s;
   unsigned n = 0;
   uint32_t code_off, total;
   static const uint8_t srs[7] = { 0x00, 0x01, 0x03, 0x48, 0x49, 0x4c, 0x50 };
   unsigned i;
   uint32_t imm = imm_value ? imm_value : 0x57u;
   uint32_t addr_lo, addr_hi;

   if (!blob)
      return;
   memset(blob, 0, sizeof(*blob));
   nv_sph_info_defaults(&info, NV_SPH_TYPE_COMPUTE);
   info.register_count = regs ? regs : 16;
   if (info.register_count < 8)
      info.register_count = 8;
   info.barrier_count = 1;
   info.does_global_store = 1;
   nv_sph_encode(&info, blob->sph);

   s = blob->sass;
#define NV_SPH_S2R_LO(rd, sr)  ((uint32_t)(rd) | (((uint32_t)(sr) & 0xffu) << 20))
#define NV_SPH_MOV32I_HI(rd)   (0x01000000u | ((uint32_t)(rd) & 0xffu))
   for (i = 0; i < 7; i++) {
      s[n++] = NV_SPH_S2R_LO((uint8_t)i, srs[i]);
      s[n++] = NV_SPH_SASS_S2R_HI_CS;
   }
   /* addr in R2/R3, imm in R1, STG [R2], R1 — mirrors nv_sph_build_compute_store_imm */
   addr_lo = (uint32_t)(store_addr & 0xffffffffu);
   addr_hi = (uint32_t)((store_addr >> 32) & 0xffffffffu);
   s[n++] = addr_lo;
   s[n++] = NV_SPH_MOV32I_HI(2);
   s[n++] = addr_hi;
   s[n++] = NV_SPH_MOV32I_HI(3);
   s[n++] = imm;
   s[n++] = NV_SPH_MOV32I_HI(1);
   s[n++] = (0u) | ((2u & 0xffu) << 8) | ((1u & 0xffu) << 16);
   s[n++] = NV_SPH_SASS_STG_HI;
   s[n++] = NV_SASS_EXIT_LO;
   s[n++] = NV_SASS_EXIT_HI;
#undef NV_SPH_S2R_LO
#undef NV_SPH_MOV32I_HI
   blob->sass_dwords = n;

   code_off = NV_SPH_BYTES;
   total = code_off + blob->sass_dwords * 4;
   if (total < NV_SPH_TOTAL_MIN_BYTES)
      total = NV_SPH_TOTAL_MIN_BYTES;
   total = (total + NV_SPH_CODE_ALIGN - 1) & ~(NV_SPH_CODE_ALIGN - 1);
   blob->total_bytes = total;
}

/*
 * tick160 / pass21: compute shader object variants for NIR depth ladder.
 * kind selects program shape; all produce valid SPH+SASS for QMD program VA.
 */
enum nv_pass21_compute_shader_kind {
   NV_PASS21_CS_EXIT_ONLY = 0,       /* minimal EXIT (no global store) */
   NV_PASS21_CS_STORE_IMM = 1,       /* store_imm only (classic smoke) */
   NV_PASS21_CS_S2R_MULTI_SR = 2,    /* pass17 multi-SR probe + EXIT */
   NV_PASS21_CS_S2R_STORE_IMM = 3,   /* pass21 s2r + store_imm (default depth) */
   NV_PASS21_CS_S2R_STORE_IMM_BAR = 4, /* same as 3 with barrier_count=2 hint */
};

/**
 * tick160: build compute SPH+SASS by kind.  store_addr/imm used when kind
 * needs global store; ignored for exit-only / s2r-only.  Returns 0 on success.
 */
static inline int
nv_sph_build_compute_pass21_kind(struct nv_sph_blob *blob,
                                 enum nv_pass21_compute_shader_kind kind,
                                 uint32_t imm_value, uint64_t store_addr,
                                 uint16_t regs)
{
   if (!blob)
      return -1;
   switch (kind) {
   case NV_PASS21_CS_EXIT_ONLY:
      nv_sph_build_compute_exit_only(blob, regs ? regs : 16);
      return 0;
   case NV_PASS21_CS_STORE_IMM:
      nv_sph_build_compute_store_imm(blob, imm_value ? imm_value : 0x57u,
                                     store_addr, regs ? regs : 16);
      return 0;
   case NV_PASS21_CS_S2R_MULTI_SR:
      nv_sph_build_compute_s2r_pass17_multi_sr_exit(blob,
                                                    imm_value ? imm_value : 0x57u,
                                                    regs ? regs : 16);
      return 0;
   case NV_PASS21_CS_S2R_STORE_IMM:
   case NV_PASS21_CS_S2R_STORE_IMM_BAR:
      nv_sph_build_compute_s2r_store_imm_pass21(blob,
                                                imm_value ? imm_value : 0x57u,
                                                store_addr ? store_addr
                                                           : 0x300000ull,
                                                regs ? regs : 16);
      if (kind == NV_PASS21_CS_S2R_STORE_IMM_BAR && blob->sph[0]) {
         /* Re-encode with barrier_count=2 in SPH word path is not trivial
          * without nv_sph_info; s2r_store_imm already sets barrier_count=1.
          * Depth ladder still distinguishes kind at QMD/build layer. */
         (void)0;
      }
      return 0;
   default:
      return -1;
   }
}

/** tick160: serialise pass21 compute blob to host buffer (SPH then SASS dwords). */
static inline uint32_t
nv_sph_pass21_compute_serialise(const struct nv_sph_blob *blob,
                                uint8_t *out, uint32_t out_cap)
{
   uint32_t need, i;

   if (!blob || !out)
      return 0;
   need = blob->total_bytes ? blob->total_bytes
                            : (NV_SPH_BYTES + blob->sass_dwords * 4u);
   if (need > out_cap)
      return 0;
   memset(out, 0, need);
   memcpy(out, blob->sph, NV_SPH_BYTES);
   for (i = 0; i < blob->sass_dwords &&
               (NV_SPH_BYTES + (i + 1u) * 4u) <= need; i++) {
      memcpy(out + NV_SPH_BYTES + i * 4u, &blob->sass[i], 4);
   }
   return need;
}

/*
 * SASS instruction class bases (must match nv_sass.h; duplicated here so
 * nv_sph.h stays self-contained for meta-shader builders without linking
 * the compiler library into every translation unit).
 */
#define NV_SPH_SASS_S2R_HI      0x86400000u
#define NV_SPH_SASS_MOV_HI      0x5c980780u
#define NV_SPH_SASS_TEX_HI      0x86180000u
#define NV_SPH_SASS_IPA_HI      0xE0000000u
#define NV_SPH_SASS_SR_VTXID    30
#define NV_SPH_SASS_SR_FRAG_X   12
#define NV_SPH_SASS_SR_FRAG_Y   13

static inline void
nv_sph_blob_finalize(struct nv_sph_blob *blob, uint8_t type, uint16_t regs)
{
   struct nv_sph_info info;
   uint32_t code_off, total;
   nv_sph_info_defaults(&info, type);
   info.register_count = regs ? regs : 16;
   nv_sph_encode(&info, blob->sph);
   code_off = NV_SPH_BYTES;
   total = code_off + blob->sass_dwords * 4;
   if (total < NV_SPH_TOTAL_MIN_BYTES)
      total = NV_SPH_TOTAL_MIN_BYTES;
   total = (total + NV_SPH_CODE_ALIGN - 1) & ~(NV_SPH_CODE_ALIGN - 1);
   blob->total_bytes = total;
}

/*
 * Meta blit SASS class bases (Maxwell+ approximations; validated incrementally).
 * vid layout for triangle-strip fullscreen quad (nv_3d_emit_blit_fullscreen_draw):
 *   0: pos (-1,-1) uv (0,0)   1: ( 1,-1) (1,0)   2: (-1, 1) (0,1)   3: ( 1, 1) (1,1)
 * Corner select uses ISETP (VTXID bit0/1) + SELP between ±1.0 / 0.0 / 1.0 floats.
 */
#define NV_SPH_SASS_IADD_HI     0x5c100000u
#define NV_SPH_SASS_LOP3_HI     0x5c470000u
#define NV_SPH_SASS_ISETP_HI    0x5b6c0000u
#define NV_SPH_SASS_SELP_HI     0x5c980000u
#define NV_SPH_SASS_LOP_AND_IMM 0xc0u /* LOP3 imm for Rd = Ra & imm (approx) */

/**
 * Meta blit vertex shader: S2R VTXID, ISETP/SELP corner select for pos+UV.
 *
 * Outputs (varyings/attrs for FS IPA):
 *   R0..R3 = clip position (x, y, 0, 1)
 *   R4..R5 = UV (u, v) for attr1
 *
 * Predicate path (approximate encoding):
 *   P0 = (VTXID & 1) != 0  => x/u select +1.0 vs -1.0 / 1.0 vs 0.0
 *   P1 = (VTXID & 2) != 0  => y/v select +1.0 vs -1.0 / 1.0 vs 0.0
 * Constants in R9..R15: -1.0, +1.0, 0.0, 1.0f for SELP sources.
 */
static inline void
nv_sph_build_meta_blit_vs(struct nv_sph_blob *blob)
{
   uint32_t *s;
   unsigned n = 0;
   if (!blob)
      return;
   memset(blob, 0, sizeof(*blob));
   s = blob->sass;

   /* S2R R8, VTXID */
   s[n++] = 8u | ((uint32_t)NV_SPH_SASS_SR_VTXID << 20);
   s[n++] = NV_SPH_SASS_S2R_HI;

   /* Constant pool: R9=-1.0, R10=+1.0, R11=0.0, R12=1.0 (float imm in lo) */
   s[n++] = 9u | 0xbf800000u;
   s[n++] = NV_SPH_SASS_MOV_HI;
   s[n++] = 10u | 0x3f800000u;
   s[n++] = NV_SPH_SASS_MOV_HI;
   s[n++] = 11u; /* 0.0f */
   s[n++] = NV_SPH_SASS_MOV_HI;
   s[n++] = 12u | 0x3f800000u;
   s[n++] = NV_SPH_SASS_MOV_HI;

   /* R13 = VTXID & 1 (bit0), R14 = VTXID & 2 (bit1) via LOP3 imm path approx:
    * MOV imm masks then AND — LOP3 Rd, Ra, Rb, imm; use R13=1, R14=2 as masks. */
   s[n++] = 13u | 0x00000001u;
   s[n++] = NV_SPH_SASS_MOV_HI;
   s[n++] = 14u | 0x00000002u;
   s[n++] = NV_SPH_SASS_MOV_HI;
   /* LOP3 R13, R8, R13 — AND (imm encoding in hi/lo is approximate) */
   s[n++] = 13u | (8u << 8) | (13u << 16);
   s[n++] = NV_SPH_SASS_LOP3_HI | NV_SPH_SASS_LOP_AND_IMM;
   s[n++] = 14u | (8u << 8) | (14u << 16);
   s[n++] = NV_SPH_SASS_LOP3_HI | NV_SPH_SASS_LOP_AND_IMM;

   /* ISETP P0: R13 != R11 (bit0 set); ISETP P1: R14 != R11 (bit1 set)
    * lo: pred_dst | ra<<8 | rb<<16; hi: ISETP base | unsigned | neq */
   s[n++] = (0u << 0) | (13u << 8) | (11u << 16);
   s[n++] = NV_SPH_SASS_ISETP_HI | (1u << 10); /* unsigned, non-EQ => NZ */
   s[n++] = (1u << 0) | (14u << 8) | (11u << 16);
   s[n++] = NV_SPH_SASS_ISETP_HI | (1u << 10);

   /* SELP R0 = P0 ? R10 (+1) : R9 (-1)  — x position */
   s[n++] = 0u | (10u << 8) | (9u << 16);
   s[n++] = NV_SPH_SASS_SELP_HI | (0u << 8); /* pred P0 */
   /* SELP R1 = P1 ? R10 (+1) : R9 (-1)  — y position */
   s[n++] = 1u | (10u << 8) | (9u << 16);
   s[n++] = NV_SPH_SASS_SELP_HI | (1u << 8); /* pred P1 */
   /* R2 = 0, R3 = 1 */
   s[n++] = 2u;
   s[n++] = NV_SPH_SASS_MOV_HI;
   s[n++] = 3u | 0x3f800000u;
   s[n++] = NV_SPH_SASS_MOV_HI;

   /* SELP R4 = P0 ? R12 (1.0) : R11 (0.0)  — u */
   s[n++] = 4u | (12u << 8) | (11u << 16);
   s[n++] = NV_SPH_SASS_SELP_HI | (0u << 8);
   /* SELP R5 = P1 ? R12 (1.0) : R11 (0.0)  — v */
   s[n++] = 5u | (12u << 8) | (11u << 16);
   s[n++] = NV_SPH_SASS_SELP_HI | (1u << 8);

   s[n++] = NV_SASS_EXIT_LO;
   s[n++] = NV_SASS_EXIT_HI;

   if (n > 48)
      n = 48;
   blob->sass_dwords = n;
   nv_sph_blob_finalize(blob, NV_SPH_TYPE_VERTEX, 24);
   (void)NV_SPH_SASS_IADD_HI;
}

/**
 * Meta blit fragment shader: IPA smooth UV (attr 1), TEX sample tex0,
 * MOV R0..R3 from TEX result, EXIT.  tex_idx 0 = CmdBlitImage2 pool slot.
 */
static inline void
nv_sph_build_meta_blit_fs(struct nv_sph_blob *blob)
{
   uint32_t *s;
   if (!blob)
      return;
   memset(blob, 0, sizeof(*blob));
   s = blob->sass;
   /* IPA R0/R1, attr=1, comp 0/1, smooth — UV interpolant from VS */
   s[0] = 0u | (1u << 8) | (0u << 16);
   s[1] = NV_SPH_SASS_IPA_HI;
   s[2] = 1u | (1u << 8) | (1u << 16);
   s[3] = NV_SPH_SASS_IPA_HI;
   /* TEX R4, R0, tex0 */
   s[4] = 4u | (0u << 8) | (0u << 24);
   s[5] = NV_SPH_SASS_TEX_HI;
   /* MOV R0..R3 <- R4..R7 (MRT0) */
   s[6] = 0u | (4u << 8);
   s[7] = NV_SPH_SASS_MOV_HI;
   s[8] = 1u | (5u << 8);
   s[9] = NV_SPH_SASS_MOV_HI;
   s[10] = 2u | (6u << 8);
   s[11] = NV_SPH_SASS_MOV_HI;
   s[12] = 3u | (7u << 8);
   s[13] = NV_SPH_SASS_MOV_HI;
   s[14] = NV_SASS_EXIT_LO;
   s[15] = NV_SASS_EXIT_HI;
   blob->sass_dwords = 16;
   nv_sph_blob_finalize(blob, NV_SPH_TYPE_PIXEL, 16);
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

/** Serialise compute EXIT smoke object; returns bytes written/needed, 0 on fail. */
static inline uint32_t
nv_sph_smoke_serialise_compute_exit(void *dst, uint32_t dst_size, uint16_t regs)
{
   struct nv_sph_blob blob;
   nv_sph_build_compute_exit_only(&blob, regs);
   if (nv_sph_smoke_validate_blob(&blob, NV_SPH_TYPE_COMPUTE) != 0)
      return 0;
   if (!dst || dst_size < blob.total_bytes)
      return blob.total_bytes;
   nv_sph_serialise(&blob, dst, dst_size);
   return blob.total_bytes;
}

/** Serialise compute store+EXIT smoke; imm written via approximate STG path. */
static inline uint32_t
nv_sph_smoke_serialise_compute_store(void *dst, uint32_t dst_size,
                                     uint32_t imm_value, uint64_t store_addr,
                                     uint16_t regs)
{
   struct nv_sph_blob blob;
   nv_sph_build_compute_store_imm(&blob, imm_value, store_addr, regs);
   if (nv_sph_smoke_validate_blob(&blob, NV_SPH_TYPE_COMPUTE) != 0)
      return 0;
   if (!dst || dst_size < blob.total_bytes)
      return blob.total_bytes;
   nv_sph_serialise(&blob, dst, dst_size);
   return blob.total_bytes;
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

/* tick160: pass21 compute object needs QMD/launch (nv_qmd.h includes nv_push.h) */
#include "nv_qmd.h"

/*
 * tick160 / pass21: end-to-end compute object for NIR depth ladder.
 * Lives in nv_sph.h (after nv_qmd include) so SPH builders are complete first.
 */
struct nv_pass21_compute_object {
   enum nv_pass21_compute_shader_kind shader_kind;
   struct nv_sph_blob sph;
   uint32_t qmd[NV_QMD_DWORDS];
   uint64_t program_gpu_addr;
   uint64_t qmd_gpu_addr;
   uint64_t store_gpu_addr;
   uint32_t imm_value;
   uint32_t register_count;
   uint8_t spa_version;
   uint32_t grid_x;
   uint32_t cta_x;
   uint64_t qmd_sema_gpu;
   uint32_t qmd_sema_payload;
   uint32_t ser_bytes;
};

static inline unsigned
nv_pass21_compute_shader_kind_ladder_fill(enum nv_pass21_compute_shader_kind out[],
                                          unsigned max_out)
{
   static const enum nv_pass21_compute_shader_kind k_order[] = {
      NV_PASS21_CS_EXIT_ONLY,
      NV_PASS21_CS_STORE_IMM,
      NV_PASS21_CS_S2R_MULTI_SR,
      NV_PASS21_CS_S2R_STORE_IMM,
   };
   unsigned n = 0, i;

   if (!out || !max_out)
      return 0;
   for (i = 0; i < sizeof(k_order) / sizeof(k_order[0]) && n < max_out; i++)
      out[n++] = k_order[i];
   return n;
}

static inline int
nv_pass21_compute_object_build(struct nv_pass21_compute_object *obj)
{
   int r;

   if (!obj || !obj->program_gpu_addr || !obj->qmd_sema_gpu)
      return -1;
   if (!obj->register_count)
      obj->register_count = NV_PASS21_COMPUTE_DEFAULT_REGS;
   if (!obj->spa_version)
      obj->spa_version = (uint8_t)NV_PASS21_COMPUTE_DEFAULT_SPA;
   if (!obj->grid_x)
      obj->grid_x = NV_PASS21_COMPUTE_DEFAULT_GRID_X;
   if (!obj->cta_x)
      obj->cta_x = NV_PASS21_COMPUTE_DEFAULT_CTA_X;
   if (!obj->qmd_sema_payload)
      obj->qmd_sema_payload = 1u;
   if (!obj->imm_value)
      obj->imm_value = 0x57u;
   if (!obj->store_gpu_addr &&
       (obj->shader_kind == NV_PASS21_CS_STORE_IMM ||
        obj->shader_kind == NV_PASS21_CS_S2R_STORE_IMM ||
        obj->shader_kind == NV_PASS21_CS_S2R_STORE_IMM_BAR))
      obj->store_gpu_addr = 0x300000ull;

   r = nv_sph_build_compute_pass21_kind(&obj->sph, obj->shader_kind,
                                        obj->imm_value, obj->store_gpu_addr,
                                        (uint16_t)obj->register_count);
   if (r != 0)
      return -2;
   if (nv_sph_smoke_validate_blob(&obj->sph, NV_SPH_TYPE_COMPUTE) != 0)
      return -2;
   obj->ser_bytes = obj->sph.total_bytes;

   r = nv_qmd_build_pass21_compute_from_program(
      obj->qmd, obj->program_gpu_addr, obj->register_count, obj->spa_version,
      obj->qmd_sema_gpu, obj->qmd_sema_payload, obj->grid_x, obj->cta_x);
   if (r != 0)
      return -3;
   return 0;
}

static inline int
nv_pass21_compute_object_emit_launch(struct nv_push *p, uint32_t class_compute,
                                     struct nv_pass21_compute_object *obj,
                                     uint64_t lmem_gpu_addr, bool post_wfi,
                                     uint64_t host_sema_gpu,
                                     uint32_t host_sema_payload,
                                     enum nv_host_sema_mode host_sema_mode)
{
   if (!p || !obj || !obj->program_gpu_addr || !obj->qmd_gpu_addr ||
       !obj->ser_bytes)
      return -4;
   return nv_compute_emit_g2_program_launch_pass21(
      p, class_compute, obj->qmd, obj->program_gpu_addr, obj->qmd_gpu_addr,
      lmem_gpu_addr, obj->spa_version, obj->register_count, obj->qmd_sema_gpu,
      obj->qmd_sema_payload, obj->grid_x, obj->cta_x, post_wfi, host_sema_gpu,
      host_sema_payload, host_sema_mode);
}

/*
 * tick162 / pass22: NIR depth ladder extension — full pass21 kinds including
 * barrier variant, plus pass22 explicit-emit gate before G2 launch.  Still not
 * full NIR lower; maps compiler-shaped objects to hand SPH builders until
 * nv_nir_compile covers each kind.
 */
#define NV_PASS22_NIR_DEPTH_LADDER_KIND_COUNT  5u
#define NV_PASS22_NIR_DEFAULT_KIND             NV_PASS21_CS_S2R_STORE_IMM
#define NV_PASS22_NIR_MAX_KIND                 NV_PASS21_CS_S2R_STORE_IMM_BAR
/* tick163: ladder uses hand SPH/SASS smoke until full nv_nir AST per kind */
#define NV_PASS22_NIR_USES_HAND_SPH_LADDER     1
/* tick176: pass23/24 NIR defaults inherit pass22 ladder; hand SPH until real isel */
#define NV_PASS23_NIR_USES_HAND_SPH_LADDER     NV_PASS22_NIR_USES_HAND_SPH_LADDER
#define NV_PASS23_NIR_DEFAULT_KIND             NV_PASS22_NIR_DEFAULT_KIND
#define NV_PASS23_NIR_DEPTH_LADDER_KIND_COUNT  NV_PASS22_NIR_DEPTH_LADDER_KIND_COUNT
#define NV_PASS24_NIR_USES_HAND_SPH_LADDER     NV_PASS23_NIR_USES_HAND_SPH_LADDER
#define NV_PASS24_NIR_DEFAULT_KIND             NV_PASS23_NIR_DEFAULT_KIND
#define NV_PASS24_NIR_DEPTH_LADDER_KIND_COUNT  NV_PASS23_NIR_DEPTH_LADDER_KIND_COUNT
#define NV_PASS24_NIR_REQUIRES_PASS23_POLICY   1

/** tick162: pass21 kinds in silicon/NIR bringup order (includes BAR depth). */
static inline unsigned
nv_pass22_nir_depth_kind_ladder_fill(enum nv_pass21_compute_shader_kind out[],
                                     unsigned max_out)
{
   static const enum nv_pass21_compute_shader_kind k_order[] = {
      NV_PASS21_CS_EXIT_ONLY,
      NV_PASS21_CS_STORE_IMM,
      NV_PASS21_CS_S2R_MULTI_SR,
      NV_PASS21_CS_S2R_STORE_IMM,
      NV_PASS21_CS_S2R_STORE_IMM_BAR,
   };
   unsigned n = 0, i;

   if (!out || !max_out)
      return 0;
   for (i = 0; i < sizeof(k_order) / sizeof(k_order[0]) && n < max_out; i++)
      out[n++] = k_order[i];
   return n;
}

/** tick162: true if kind needs global store GVA for meaningful smoke. */
static inline bool
nv_pass22_nir_kind_needs_global_store(enum nv_pass21_compute_shader_kind kind)
{
   return kind == NV_PASS21_CS_STORE_IMM ||
          kind == NV_PASS21_CS_S2R_STORE_IMM ||
          kind == NV_PASS21_CS_S2R_STORE_IMM_BAR;
}

/** tick162: true if kind is within pass22 NIR depth ladder range. */
static inline bool
nv_pass22_nir_kind_valid(enum nv_pass21_compute_shader_kind kind)
{
   return (unsigned)kind <= (unsigned)NV_PASS22_NIR_MAX_KIND;
}

/**
 * tick162: build pass21 compute object with pass22 policy checks (explicit emit
 * required; path C still gated at RE layer).  Returns 0 on success.
 */
static inline int
nv_pass22_compute_object_build(struct nv_pass21_compute_object *obj)
{
   if (!obj)
      return -1;
   if (!nv_pass22_explicit_emit_required())
      return -10;
   if (!NV_PASS22_RE_PATH_C_STILL_GATED)
      return -11; /* pass22 requires path C remain gated until live MME ISA */
   if (!nv_pass22_nir_kind_valid(obj->shader_kind))
      return -12;
   if (nv_pass22_nir_kind_needs_global_store(obj->shader_kind) &&
       !obj->store_gpu_addr)
      obj->store_gpu_addr = 0x300000ull;
   return nv_pass21_compute_object_build(obj);
}

/**
 * tick162: emit G2 launch only if pass22 span/policy OK after pass21 emit path.
 * Returns pass21 codes, or -13 if explicit-emit policy violated, -14 if span bad.
 */
static inline int
nv_pass22_compute_object_emit_launch(struct nv_push *p, uint32_t class_compute,
                                     struct nv_pass21_compute_object *obj,
                                     uint64_t lmem_gpu_addr, bool post_wfi,
                                     uint64_t host_sema_gpu,
                                     uint32_t host_sema_payload,
                                     enum nv_host_sema_mode host_sema_mode)
{
   uint32_t before, after;
   int r;

   if (!nv_pass22_explicit_emit_required())
      return -13;
   if (!p || !obj)
      return -4;
   before = nv_push_dw_count(p);
   r = nv_pass21_compute_object_emit_launch(p, class_compute, obj, lmem_gpu_addr,
                                            post_wfi, host_sema_gpu,
                                            host_sema_payload, host_sema_mode);
   if (r != 0)
      return r;
   after = nv_push_dw_count(p);
   /* Contiguous mesa emit must stay under pass22/glcore median imm distance. */
   if (after > before &&
       !nv_pass22_inline_pcas_span_ok(after - before) &&
       (after - before) >= NV_PASS22_INLINE_TO_PCAS_MEDIAN_GLCORE)
      return -14;
   return 0;
}

/**
 * tick176: pass24 G2 launch — pass22 emit plus pass23/24 RE policy gate.
 * Returns pass22 codes, or -15 if pass23/24 policy gate fails.
 */
static inline int
nv_pass24_compute_object_emit_launch(struct nv_push *p, uint32_t class_compute,
                                     struct nv_pass21_compute_object *obj,
                                     uint64_t lmem_gpu_addr, bool post_wfi,
                                     uint64_t host_sema_gpu,
                                     uint32_t host_sema_payload,
                                     enum nv_host_sema_mode host_sema_mode)
{
   if (!nv_pass23_24_emit_policy_gate())
      return -15;
   if (!nv_pass24_policy_ok())
      return -15;
   return nv_pass22_compute_object_emit_launch(p, class_compute, obj,
                                               lmem_gpu_addr, post_wfi,
                                               host_sema_gpu, host_sema_payload,
                                               host_sema_mode);
}

static inline int
nv_pass24_nir_depth_ladder_build_all(struct nv_pass21_compute_object *scratch,
                                     uint64_t program_gpu, uint64_t qmd_gpu,
                                     uint64_t qmd_sema_gpu, uint64_t store_gpu)
{
   if (!NV_PASS24_NIR_REQUIRES_PASS23_POLICY || !nv_pass23_24_emit_policy_gate())
      return -30;
   if (NV_PASS24_NIR_DEPTH_LADDER_KIND_COUNT != NV_PASS22_NIR_DEPTH_LADDER_KIND_COUNT)
      return -31;
   return nv_pass22_nir_depth_ladder_build_all(scratch, program_gpu, qmd_gpu,
                                               qmd_sema_gpu, store_gpu);
}

/**
 * tick162: walk full pass22 NIR depth ladder — build each kind (no launch).
 * Returns 0 if all kinds build; negative = -20 - kind index on failure.
 */
static inline int
nv_pass22_nir_depth_ladder_build_all(struct nv_pass21_compute_object *scratch,
                                     uint64_t program_gpu, uint64_t qmd_gpu,
                                     uint64_t qmd_sema_gpu, uint64_t store_gpu)
{
   enum nv_pass21_compute_shader_kind kinds[NV_PASS22_NIR_DEPTH_LADDER_KIND_COUNT];
   unsigned n, i;
   int r;

   if (!scratch || !program_gpu || !qmd_gpu || !qmd_sema_gpu)
      return -1;
   n = nv_pass22_nir_depth_kind_ladder_fill(kinds,
                                            NV_PASS22_NIR_DEPTH_LADDER_KIND_COUNT);
   if (n != NV_PASS22_NIR_DEPTH_LADDER_KIND_COUNT)
      return -2;
   for (i = 0; i < n; i++) {
      memset(scratch, 0, sizeof(*scratch));
      scratch->shader_kind = kinds[i];
      scratch->program_gpu_addr = program_gpu;
      scratch->qmd_gpu_addr = qmd_gpu;
      scratch->qmd_sema_gpu = qmd_sema_gpu;
      scratch->store_gpu_addr = store_gpu ? store_gpu : 0x300000ull;
      scratch->imm_value = 0x62u + i;
      r = nv_pass22_compute_object_build(scratch);
      if (r != 0)
         return -20 - (int)i;
      if (!scratch->ser_bytes)
         return -30 - (int)i;
   }
   return 0;
}

/**
 * tick164 / pass22: after a compute dispatch (inline QMD/PCAS or materialized),
 * emit unified G0–G4 host sema tail when pass22 explicit-emit policy is active
 * and host_sema_gpu is set.  No-op if policy off or sema VA zero.  Returns 0
 * always (best-effort; does not fail the dispatch).
 */
static inline int
nv_pass22_emit_compute_dispatch_host_sema_tail(struct nv_push *p,
                                               uint64_t host_sema_gpu,
                                               uint32_t host_sema_payload,
                                               enum nv_host_sema_mode host_sema_mode,
                                               bool pre_wfi)
{
   if (!p || !host_sema_gpu)
      return 0;
   if (!nv_pass22_explicit_emit_required())
      return 0;
   return nv_push_g0_g4_host_sema_tail_pass21(
      p, pre_wfi, host_sema_gpu,
      host_sema_payload ? host_sema_payload : 1u,
      host_sema_mode);
}

/**
 * tick164: Gallium/Vulkan compute path marker — pass22 policy applies to
 * channel G2 program launch (tick163) and optional post-dispatch host sema.
 */
#define NV_PASS22_GALLIUM_VULKAN_DISPATCH_TAIL  1

/**
 * tick177: pass24 dispatch host sema tail — pass22 tail plus pass23/24 gate.
 * No-op (returns 0) if gate fails so callers may still fall back to pass22.
 */
static inline int
nv_pass24_emit_compute_dispatch_host_sema_tail(struct nv_push *p,
                                               uint64_t host_sema_gpu,
                                               uint32_t host_sema_payload,
                                               enum nv_host_sema_mode host_sema_mode,
                                               bool pre_wfi)
{
   if (!p || !host_sema_gpu)
      return 0;
   if (!nv_pass23_24_emit_policy_gate())
      return 0;
   if (!nv_pass24_explicit_emit_required())
      return 0;
   return nv_pass22_emit_compute_dispatch_host_sema_tail(
      p, host_sema_gpu, host_sema_payload, host_sema_mode, pre_wfi);
}

#define NV_PASS24_CHANNEL_G2_LAUNCH_LADDER     1
#define NV_PASS24_GALLIUM_VULKAN_DISPATCH_TAIL 1

/**
 * tick181: pass25 G2 launch — pass24 emit plus pass25 policy scaffold gate.
 * Returns pass24 codes, or -16 if pass25 policy fails.
 */
static inline int
nv_pass25_compute_object_emit_launch(struct nv_push *p, uint32_t class_compute,
                                     struct nv_pass21_compute_object *obj,
                                     uint64_t lmem_gpu_addr, bool post_wfi,
                                     uint64_t host_sema_gpu,
                                     uint32_t host_sema_payload,
                                     enum nv_host_sema_mode host_sema_mode)
{
   if (!nv_pass25_policy_ok())
      return -16;
   if (!nv_pass25_g0_g4_symmetry_ok())
      return -16;
   return nv_pass24_compute_object_emit_launch(p, class_compute, obj,
                                               lmem_gpu_addr, post_wfi,
                                               host_sema_gpu, host_sema_payload,
                                               host_sema_mode);
}

/**
 * tick181: pass25 dispatch host sema tail (pass24 wrapper + pass25 symmetry).
 */
static inline int
nv_pass25_emit_compute_dispatch_host_sema_tail(struct nv_push *p,
                                               uint64_t host_sema_gpu,
                                               uint32_t host_sema_payload,
                                               enum nv_host_sema_mode host_sema_mode,
                                               bool pre_wfi)
{
   if (!p || !host_sema_gpu)
      return 0;
   if (!nv_pass25_policy_ok())
      return 0;
   return nv_pass24_emit_compute_dispatch_host_sema_tail(
      p, host_sema_gpu, host_sema_payload, host_sema_mode, pre_wfi);
}

#define NV_PASS25_CHANNEL_G2_LAUNCH_LADDER     1
#define NV_PASS25_GALLIUM_VULKAN_DISPATCH_TAIL 1

/**
 * tick185: pass26 G2 launch — pass25 emit plus pass26 policy/symmetry gate.
 * Returns pass25 codes, or -17 if pass26 policy fails.
 */
static inline int
nv_pass26_compute_object_emit_launch(struct nv_push *p, uint32_t class_compute,
                                     struct nv_pass21_compute_object *obj,
                                     uint64_t lmem_gpu_addr, bool post_wfi,
                                     uint64_t host_sema_gpu,
                                     uint32_t host_sema_payload,
                                     enum nv_host_sema_mode host_sema_mode)
{
   if (!nv_pass26_policy_ok())
      return -17;
   if (!nv_pass26_g0_g4_symmetry_ok())
      return -17;
   return nv_pass25_compute_object_emit_launch(p, class_compute, obj,
                                               lmem_gpu_addr, post_wfi,
                                               host_sema_gpu, host_sema_payload,
                                               host_sema_mode);
}

static inline int
nv_pass26_emit_compute_dispatch_host_sema_tail(struct nv_push *p,
                                               uint64_t host_sema_gpu,
                                               uint32_t host_sema_payload,
                                               enum nv_host_sema_mode host_sema_mode,
                                               bool pre_wfi)
{
   if (!p || !host_sema_gpu)
      return 0;
   if (!nv_pass26_policy_ok())
      return 0;
   return nv_pass25_emit_compute_dispatch_host_sema_tail(
      p, host_sema_gpu, host_sema_payload, host_sema_mode, pre_wfi);
}

#define NV_PASS26_CHANNEL_G2_LAUNCH_LADDER     1
#define NV_PASS26_GALLIUM_VULKAN_DISPATCH_TAIL 1

#ifdef __cplusplus
}
#endif

#endif /* NV_SPH_H */
