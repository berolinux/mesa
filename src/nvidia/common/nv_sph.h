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

#ifdef __cplusplus
}
#endif

#endif /* NV_SPH_H */
