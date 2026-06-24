/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * SASS instruction encoder scaffold for Maxwell+ (SM 5.0–8.x).
 *
 * Instruction encodings are reverse-engineered from patterns observed in
 * proprietary compiler output and public SM documentation.  Each instruction
 * is 8 bytes (2 dwords) on Maxwell/Pascal; Volta+ uses a different 16-byte
 * form in some cases but EXIT/MOV/IADD/FADD/IMAD encodings below are chosen
 * for compatibility with the SPH sass_level_2=false path (8-byte instructions).
 *
 * This is incrementally refined; unhandled NIR maps to NOP + EXIT.
 */

#ifndef NV_SASS_H
#define NV_SASS_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Max instructions in early encoder (SPH blob has limited sass[] array). */
#define NV_SASS_MAX_INSNS       64
#define NV_SASS_DWORDS_PER_INSN 2

/* Predicate always-true (PT) in predicate field */
#define NV_SASS_PRED_PT         7

/* Common opcode families (hi dword primary opcode bits vary by generation;
 * we use Maxwell/Pascal-style encodings widely documented for EXIT/MOV/ALU). */

/* EXIT: terminate warp (lo=0, hi has EXIT class) */
#define NV_SASS_EXIT_LO         0x00000000u
#define NV_SASS_EXIT_HI         0x50b00000u

/* NOP (scheduling pad) */
#define NV_SASS_NOP_LO          0x00000000u
#define NV_SASS_NOP_HI          0x50b00000u /* same class; refined later */

/* MOV Rd, Ra  (register move, Maxwell/Pascal approximate) */
/* Encoding skeleton: Rd in bits of lo, Ra elsewhere; hi carries MOV class. */
#define NV_SASS_MOV_HI_BASE     0x5c980780u

/* IADD Rd, Ra, Rb  (integer add, no carry) */
#define NV_SASS_IADD_HI_BASE    0x5c100000u

/* IADD32I Rd, Ra, imm20 */
#define NV_SASS_IADD32I_HI_BASE 0x1c000000u

/* FADD Rd, Ra, Rb  (FP32 add) */
#define NV_SASS_FADD_HI_BASE    0x5c580000u

/* FFMA Rd, Ra, Rb, Rc */
#define NV_SASS_FFMA_HI_BASE    0x59800000u

/* FMUL Rd, Ra, Rb */
#define NV_SASS_FMUL_HI_BASE    0x5c680000u

/* IMAD Rd, Ra, Rb, Rc (integer multiply-add) */
#define NV_SASS_IMAD_HI_BASE    0x5c280000u

/* LOP3.LUT logical (generic; used for AND/OR/XOR approximations) */
#define NV_SASS_LOP3_HI_BASE    0x5c470000u

/* S2R Rd, SR#  (special register read — thread/block IDs etc.) */
#define NV_SASS_S2R_HI_BASE     0x86400000u

/* LDG / STG global load/store (very approximate class bits) */
#define NV_SASS_LDG_HI_BASE     0xeed00000u
#define NV_SASS_STG_HI_BASE     0xeed80000u

/* LDL / STL — local memory load/store (per-thread LMEM; spill/frame) */
#define NV_SASS_LDL_HI_BASE     0xef400000u
#define NV_SASS_STL_HI_BASE     0xef480000u

/* LDS / STS — shared memory (CTA/shared) approximations */
#define NV_SASS_LDS_HI_BASE     0xef480800u
#define NV_SASS_STS_HI_BASE     0xef580000u

/* BRA / relative branch (lo has offset, hi has BRA class) */
#define NV_SASS_BRA_HI_BASE     0xe2400000u

/* SHF.L / SHF.R — shift left/right (Maxwell SHF class approx) */
#define NV_SASS_SHF_L_HI_BASE   0x5cf80000u
#define NV_SASS_SHF_R_HI_BASE   0x5cf80200u  /* direction bit in hi */

/* IMNMX / FMNMX — integer/float min/max */
#define NV_SASS_IMNMX_HI_BASE   0x5c600000u
#define NV_SASS_FMNMX_HI_BASE   0x5c600800u  /* FP variant bit in hi */

/* F2I / I2F conversions */
#define NV_SASS_F2I_HI_BASE     0x5cb00000u
#define NV_SASS_I2F_HI_BASE     0x5cb80000u

/* MUFU — multi-function (RCP/RSQ/SQRT/LG2/EX2/SIN/COS approximations) */
#define NV_SASS_MUFU_HI_BASE    0x50800000u
#define NV_SASS_MUFU_RCP        0
#define NV_SASS_MUFU_RSQ        1
#define NV_SASS_MUFU_LG2        2
#define NV_SASS_MUFU_EX2        3
#define NV_SASS_MUFU_SIN        4
#define NV_SASS_MUFU_COS        5
#define NV_SASS_MUFU_SQRT       8

/* LDC — load from constant bank c[bank][offset] (Maxwell/Pascal style) */
#define NV_SASS_LDC_HI_BASE     0xef900000u

/* TEX / TLD — texture fetch / texture load (binding index in instr) */
#define NV_SASS_TEX_HI_BASE     0x86180000u
#define NV_SASS_TLD_HI_BASE     0x86100000u

/* FSET / ISET — compare producing predicate/register result (approx) */
#define NV_SASS_FSET_HI_BASE    0x58080000u
#define NV_SASS_ISET_HI_BASE    0x5b6c0000u

/* ISETP — integer compare to predicate (Maxwell/Pascal approx) */
#define NV_SASS_ISETP_HI_BASE   0x5b6c0000u
#define NV_SASS_FSETP_HI_BASE   0x5bb00000u

/* SELP — select predicated (Rd = cond ? Ra : Rb) */
#define NV_SASS_SELP_HI_BASE    0x5c980000u

/* SHFL — warp shuffle (idx/up/down/bfly modes in hi bits) */
#define NV_SASS_SHFL_HI_BASE    0xef100000u
#define NV_SASS_SHFL_MODE_IDX   0
#define NV_SASS_SHFL_MODE_UP    1
#define NV_SASS_SHFL_MODE_DOWN  2
#define NV_SASS_SHFL_MODE_BFLY  3

/* BAR.SYNC — CTA barrier (compute workgroup barrier) */
#define NV_SASS_BAR_HI_BASE     0xf0a80000u

/* ATOM / ATOMS — global/shared atomics (add/exch/cas approximations) */
#define NV_SASS_ATOM_HI_BASE    0xed000000u
#define NV_SASS_ATOM_OP_ADD     0
#define NV_SASS_ATOM_OP_MIN     1
#define NV_SASS_ATOM_OP_MAX     2
#define NV_SASS_ATOM_OP_INC     3
#define NV_SASS_ATOM_OP_DEC     4
#define NV_SASS_ATOM_OP_AND     5
#define NV_SASS_ATOM_OP_OR      6
#define NV_SASS_ATOM_OP_XOR     7
#define NV_SASS_ATOM_OP_EXCH    8
#define NV_SASS_ATOM_OP_CAS     9

/* MEMBAR — memory barrier */
#define NV_SASS_MEMBAR_HI_BASE  0xef980000u

/* IMNMX max bit */
#define NV_SASS_MINMAX_MAX_BIT  (1u << 23)

/* Special register indices (subset) */
#define NV_SASS_SR_LANEID       0
#define NV_SASS_SR_VIRTCFG      2
#define NV_SASS_SR_VIRTID       3
#define NV_SASS_SR_PM0          4
#define NV_SASS_SR_SMEMSZ       5
#define NV_SASS_SR_CLOCKLO      80
#define NV_SASS_SR_CLOCKHI      81
#define NV_SASS_SR_AFFINITY     92
#define NV_SASS_SR_ORDERING_TICKET 93
#define NV_SASS_SR_THREAD_KILL  94
#define NV_SASS_SR_SHADER_TYPE  95
#define NV_SASS_SR_TID_X        32
#define NV_SASS_SR_TID_Y        33
#define NV_SASS_SR_TID_Z        34
#define NV_SASS_SR_CTAID_X      37
#define NV_SASS_SR_CTAID_Y      38
#define NV_SASS_SR_CTAID_Z      39
#define NV_SASS_SR_NTID_X       40
#define NV_SASS_SR_NTID_Y       41
#define NV_SASS_SR_NTID_Z       42
#define NV_SASS_SR_NCTAID_X     43
#define NV_SASS_SR_NCTAID_Y     44
#define NV_SASS_SR_NCTAID_Z     45
/* Graphics / fragment specials (Maxwell+ S2R indices; refined from
 * public SM docs + binary compiler output patterns) */
#define NV_SASS_SR_INVID        29   /* invocation / instance-related */
#define NV_SASS_SR_VTXID        30   /* vertex index within draw */
#define NV_SASS_SR_PRIMID       31   /* primitive ID */
#define NV_SASS_SR_YDIR         48   /* fragment Y direction / front face proxy */
#define NV_SASS_SR_THREADKILL   94   /* same as THREAD_KILL */
#define NV_SASS_SR_WARPERROR    96
#define NV_SASS_SR_PM1          5
#define NV_SASS_SR_PM2          6
#define NV_SASS_SR_PM3          7
#define NV_SASS_SR_PM4          8
#define NV_SASS_SR_PM5          9
#define NV_SASS_SR_PM6          10
#define NV_SASS_SR_PM7          11

struct nv_sass_buf {
   uint32_t *dwords;     /* owned array; 2 dwords per instruction */
   uint32_t  capacity;   /* in dwords */
   uint32_t  count;      /* dwords written */
   uint16_t  max_reg;    /* highest Rd/Ra/Rb index seen + 1 */
   bool      has_global_store;
   bool      has_tex;
};

static inline void
nv_sass_buf_init(struct nv_sass_buf *b)
{
   memset(b, 0, sizeof(*b));
}

void nv_sass_buf_finish(struct nv_sass_buf *b);

/* Ensure capacity for at least n more instructions (2*n dwords). */
bool nv_sass_buf_reserve(struct nv_sass_buf *b, uint32_t extra_insns);

/* Append one 8-byte instruction; returns false on OOM. */
bool nv_sass_emit_raw(struct nv_sass_buf *b, uint32_t lo, uint32_t hi);

bool nv_sass_emit_exit(struct nv_sass_buf *b);
bool nv_sass_emit_nop(struct nv_sass_buf *b);
bool nv_sass_emit_mov_rr(struct nv_sass_buf *b, uint8_t rd, uint8_t ra);
bool nv_sass_emit_mov_ri(struct nv_sass_buf *b, uint8_t rd, uint32_t imm);
bool nv_sass_emit_iadd_rrr(struct nv_sass_buf *b, uint8_t rd, uint8_t ra, uint8_t rb);
bool nv_sass_emit_iadd_rri(struct nv_sass_buf *b, uint8_t rd, uint8_t ra, int32_t imm);
bool nv_sass_emit_fadd_rrr(struct nv_sass_buf *b, uint8_t rd, uint8_t ra, uint8_t rb);
bool nv_sass_emit_fmul_rrr(struct nv_sass_buf *b, uint8_t rd, uint8_t ra, uint8_t rb);
bool nv_sass_emit_ffma_rrrr(struct nv_sass_buf *b, uint8_t rd, uint8_t ra,
                            uint8_t rb, uint8_t rc);
bool nv_sass_emit_imad_rrrr(struct nv_sass_buf *b, uint8_t rd, uint8_t ra,
                            uint8_t rb, uint8_t rc);
bool nv_sass_emit_lop3(struct nv_sass_buf *b, uint8_t rd, uint8_t ra,
                       uint8_t rb, uint8_t rc, uint8_t lut);
bool nv_sass_emit_s2r(struct nv_sass_buf *b, uint8_t rd, uint8_t sr);
bool nv_sass_emit_ldg_u32(struct nv_sass_buf *b, uint8_t rd, uint8_t ra_addr);
bool nv_sass_emit_stg_u32(struct nv_sass_buf *b, uint8_t ra_addr, uint8_t rb_data);
bool nv_sass_emit_ldl_u32(struct nv_sass_buf *b, uint8_t rd, uint8_t ra_addr);
bool nv_sass_emit_stl_u32(struct nv_sass_buf *b, uint8_t ra_addr, uint8_t rb_data);
bool nv_sass_emit_lds_u32(struct nv_sass_buf *b, uint8_t rd, uint8_t ra_addr);
bool nv_sass_emit_sts_u32(struct nv_sass_buf *b, uint8_t ra_addr, uint8_t rb_data);

bool nv_sass_emit_shf_l(struct nv_sass_buf *b, uint8_t rd, uint8_t ra, uint8_t rb);
bool nv_sass_emit_shf_r(struct nv_sass_buf *b, uint8_t rd, uint8_t ra, uint8_t rb, bool arithmetic);
bool nv_sass_emit_imnmx(struct nv_sass_buf *b, uint8_t rd, uint8_t ra, uint8_t rb, bool is_max, bool is_signed);
bool nv_sass_emit_fmnmx(struct nv_sass_buf *b, uint8_t rd, uint8_t ra, uint8_t rb, bool is_max);
bool nv_sass_emit_f2i(struct nv_sass_buf *b, uint8_t rd, uint8_t ra, bool is_signed);
bool nv_sass_emit_i2f(struct nv_sass_buf *b, uint8_t rd, uint8_t ra, bool is_signed);
bool nv_sass_emit_mufu(struct nv_sass_buf *b, uint8_t rd, uint8_t ra, uint8_t mufu_op);
bool nv_sass_emit_ldc(struct nv_sass_buf *b, uint8_t rd, uint8_t bank, uint16_t offset_dwords);
bool nv_sass_emit_tex(struct nv_sass_buf *b, uint8_t rd, uint8_t ra_coord, uint8_t tex_idx);
bool nv_sass_emit_tld(struct nv_sass_buf *b, uint8_t rd, uint8_t ra_coord, uint8_t tex_idx);
bool nv_sass_emit_iadd_neg_rb(struct nv_sass_buf *b, uint8_t rd, uint8_t ra, uint8_t rb);

/* Predicated select / compare / control / atomics / barriers */
bool nv_sass_emit_selp(struct nv_sass_buf *b, uint8_t rd, uint8_t ra, uint8_t rb,
                       uint8_t pred);
bool nv_sass_emit_isetp(struct nv_sass_buf *b, uint8_t pred_dst, uint8_t ra,
                        uint8_t rb, bool is_signed, bool is_eq);
bool nv_sass_emit_fsetp(struct nv_sass_buf *b, uint8_t pred_dst, uint8_t ra,
                        uint8_t rb, bool is_eq);
bool nv_sass_emit_shfl(struct nv_sass_buf *b, uint8_t rd, uint8_t ra,
                       uint8_t rb_idx, uint8_t mode);
bool nv_sass_emit_bar_sync(struct nv_sass_buf *b, uint8_t barrier_id);
bool nv_sass_emit_membar(struct nv_sass_buf *b);
bool nv_sass_emit_atom(struct nv_sass_buf *b, uint8_t rd, uint8_t ra_addr,
                       uint8_t rb_data, uint8_t atom_op);
bool nv_sass_emit_bra(struct nv_sass_buf *b, int32_t rel_insn_offset);
bool nv_sass_emit_bra_pred(struct nv_sass_buf *b, int32_t rel_insn_offset,
                           uint8_t pred, bool not_pred);

/* Copy first min(count, dst_cap_dwords) into dst; returns dwords copied. */
uint32_t nv_sass_buf_copy_out(const struct nv_sass_buf *b,
                              uint32_t *dst, uint32_t dst_cap_dwords);

/* Track register pressure helper */
static inline void
nv_sass_note_reg(struct nv_sass_buf *b, uint8_t r)
{
   if (r + 1 > b->max_reg)
      b->max_reg = (uint16_t)(r + 1);
}

#ifdef __cplusplus
}
#endif

#endif /* NV_SASS_H */
