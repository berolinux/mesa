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

/* BRA / relative branch (lo has offset, hi has BRA class) */
#define NV_SASS_BRA_HI_BASE     0xe2400000u

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
