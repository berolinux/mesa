/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 */

#include "nv_sass.h"

#include <stdlib.h>

void
nv_sass_buf_finish(struct nv_sass_buf *b)
{
   if (!b)
      return;
   free(b->dwords);
   b->dwords = NULL;
   b->capacity = 0;
   b->count = 0;
}

bool
nv_sass_buf_reserve(struct nv_sass_buf *b, uint32_t extra_insns)
{
   uint32_t need_dwords;
   uint32_t *nd;
   uint32_t new_cap;

   if (!b)
      return false;
   need_dwords = b->count + extra_insns * NV_SASS_DWORDS_PER_INSN;
   if (need_dwords <= b->capacity)
      return true;
   new_cap = b->capacity ? b->capacity : 32;
   while (new_cap < need_dwords)
      new_cap *= 2;
   if (new_cap > NV_SASS_MAX_INSNS * NV_SASS_DWORDS_PER_INSN * 4)
      new_cap = NV_SASS_MAX_INSNS * NV_SASS_DWORDS_PER_INSN * 4;
   if (new_cap < need_dwords)
      return false;
   nd = realloc(b->dwords, new_cap * sizeof(uint32_t));
   if (!nd)
      return false;
   b->dwords = nd;
   b->capacity = new_cap;
   return true;
}

bool
nv_sass_emit_raw(struct nv_sass_buf *b, uint32_t lo, uint32_t hi)
{
   if (!nv_sass_buf_reserve(b, 1))
      return false;
   b->dwords[b->count++] = lo;
   b->dwords[b->count++] = hi;
   return true;
}

bool
nv_sass_emit_exit(struct nv_sass_buf *b)
{
   return nv_sass_emit_raw(b, NV_SASS_EXIT_LO, NV_SASS_EXIT_HI);
}

bool
nv_sass_emit_nop(struct nv_sass_buf *b)
{
   /* Use a no-op scheduling slot; same EXIT class with different pred in future */
   return nv_sass_emit_raw(b, 0x00000000u, 0x50b00000u);
}

/*
 * Register field placement for Maxwell/Pascal-style 64-bit instructions
 * (approximate; refined as more encodings are validated against proprietary output):
 *   lo[7:0]   = Rd
 *   lo[15:8]  = Ra  (or part of imm)
 *   lo[23:16] = Rb
 *   lo[31:24] = Rc / imm high / unused
 *   hi        = opcode class + modifiers
 */
static uint32_t
pack_lo_rrr(uint8_t rd, uint8_t ra, uint8_t rb)
{
   return (uint32_t)rd | ((uint32_t)ra << 8) | ((uint32_t)rb << 16);
}

static uint32_t
pack_lo_rrrr(uint8_t rd, uint8_t ra, uint8_t rb, uint8_t rc)
{
   return (uint32_t)rd | ((uint32_t)ra << 8) | ((uint32_t)rb << 16) |
          ((uint32_t)rc << 24);
}

bool
nv_sass_emit_mov_rr(struct nv_sass_buf *b, uint8_t rd, uint8_t ra)
{
   nv_sass_note_reg(b, rd);
   nv_sass_note_reg(b, ra);
   /* MOV Rd, Ra — Ra in source field, Rb=RZ (255) often used for unused */
   return nv_sass_emit_raw(b,
                           pack_lo_rrr(rd, ra, 0xff),
                           NV_SASS_MOV_HI_BASE);
}

bool
nv_sass_emit_mov_ri(struct nv_sass_buf *b, uint8_t rd, uint32_t imm)
{
   nv_sass_note_reg(b, rd);
   /* Approximate MOV32I: imm in lo, Rd in hi/lo combo */
   return nv_sass_emit_raw(b, imm,
                           0x01000000u | ((uint32_t)rd << 0));
}

bool
nv_sass_emit_iadd_rrr(struct nv_sass_buf *b, uint8_t rd, uint8_t ra, uint8_t rb)
{
   nv_sass_note_reg(b, rd);
   nv_sass_note_reg(b, ra);
   nv_sass_note_reg(b, rb);
   return nv_sass_emit_raw(b, pack_lo_rrr(rd, ra, rb), NV_SASS_IADD_HI_BASE);
}

bool
nv_sass_emit_iadd_rri(struct nv_sass_buf *b, uint8_t rd, uint8_t ra, int32_t imm)
{
   nv_sass_note_reg(b, rd);
   nv_sass_note_reg(b, ra);
   /* IADD32I: imm20 in instruction; truncated */
   uint32_t imm20 = (uint32_t)imm & 0xfffffu;
   return nv_sass_emit_raw(b,
                           (uint32_t)rd | ((uint32_t)ra << 8) | (imm20 << 20),
                           NV_SASS_IADD32I_HI_BASE | ((imm20 >> 12) & 0xff));
}

bool
nv_sass_emit_fadd_rrr(struct nv_sass_buf *b, uint8_t rd, uint8_t ra, uint8_t rb)
{
   nv_sass_note_reg(b, rd);
   nv_sass_note_reg(b, ra);
   nv_sass_note_reg(b, rb);
   return nv_sass_emit_raw(b, pack_lo_rrr(rd, ra, rb), NV_SASS_FADD_HI_BASE);
}

bool
nv_sass_emit_fmul_rrr(struct nv_sass_buf *b, uint8_t rd, uint8_t ra, uint8_t rb)
{
   nv_sass_note_reg(b, rd);
   nv_sass_note_reg(b, ra);
   nv_sass_note_reg(b, rb);
   return nv_sass_emit_raw(b, pack_lo_rrr(rd, ra, rb), NV_SASS_FMUL_HI_BASE);
}

bool
nv_sass_emit_ffma_rrrr(struct nv_sass_buf *b, uint8_t rd, uint8_t ra,
                       uint8_t rb, uint8_t rc)
{
   nv_sass_note_reg(b, rd);
   nv_sass_note_reg(b, ra);
   nv_sass_note_reg(b, rb);
   nv_sass_note_reg(b, rc);
   return nv_sass_emit_raw(b, pack_lo_rrrr(rd, ra, rb, rc), NV_SASS_FFMA_HI_BASE);
}

bool
nv_sass_emit_imad_rrrr(struct nv_sass_buf *b, uint8_t rd, uint8_t ra,
                       uint8_t rb, uint8_t rc)
{
   nv_sass_note_reg(b, rd);
   nv_sass_note_reg(b, ra);
   nv_sass_note_reg(b, rb);
   nv_sass_note_reg(b, rc);
   return nv_sass_emit_raw(b, pack_lo_rrrr(rd, ra, rb, rc), NV_SASS_IMAD_HI_BASE);
}

bool
nv_sass_emit_lop3(struct nv_sass_buf *b, uint8_t rd, uint8_t ra,
                  uint8_t rb, uint8_t rc, uint8_t lut)
{
   nv_sass_note_reg(b, rd);
   nv_sass_note_reg(b, ra);
   nv_sass_note_reg(b, rb);
   nv_sass_note_reg(b, rc);
   return nv_sass_emit_raw(b,
                           pack_lo_rrrr(rd, ra, rb, rc),
                           NV_SASS_LOP3_HI_BASE | ((uint32_t)lut << 0));
}

bool
nv_sass_emit_s2r(struct nv_sass_buf *b, uint8_t rd, uint8_t sr)
{
   nv_sass_note_reg(b, rd);
   return nv_sass_emit_raw(b,
                           (uint32_t)rd | ((uint32_t)sr << 8),
                           NV_SASS_S2R_HI_BASE);
}

/* R2P class hi nibble approximates Maxwell R2P.P0 (pred write from reg bit). */
#define NV_SASS_R2P_HI_BASE   0x50c00000u
#define NV_SASS_P2R_HI_BASE   0x50c80000u
#define NV_SASS_KIL_HI_BASE   0x50b00000u  /* KIL / BPT.KILL family stand-in */

bool
nv_sass_emit_r2p(struct nv_sass_buf *b, uint8_t ra, uint8_t pred_idx, uint8_t bit)
{
   nv_sass_note_reg(b, ra);
   /* lo: Ra in [15:8], pred index + bit select in high byte of lo / low of hi */
   return nv_sass_emit_raw(b,
                           ((uint32_t)ra << 8) | ((uint32_t)bit << 16) |
                           ((uint32_t)(pred_idx & 7) << 24),
                           NV_SASS_R2P_HI_BASE);
}

bool
nv_sass_emit_p2r(struct nv_sass_buf *b, uint8_t rd, uint8_t pred_idx)
{
   nv_sass_note_reg(b, rd);
   return nv_sass_emit_raw(b,
                           (uint32_t)rd | ((uint32_t)(pred_idx & 7) << 8),
                           NV_SASS_P2R_HI_BASE);
}

bool
nv_sass_emit_kill_thread(struct nv_sass_buf *b)
{
   /* Unconditional pixel/helper kill:
    *  1) MOV R0, 1  — non-zero marks kill intent
    *  2) R2P P0, R0.b0 — drive kill predicate (compiler/HW path maps to THREAD_KILL)
    *  3) KIL-class instruction — mark lane as helper / discard fragment writes
    * Does NOT use EXIT (would terminate whole warp incorrectly for demote). */
   if (!nv_sass_emit_mov_ri(b, 0, 1))
      return false;
   if (!nv_sass_emit_r2p(b, 0, 0, 0))
      return false;
   return nv_sass_emit_raw(b, 0x00000000u, NV_SASS_KIL_HI_BASE);
}

bool
nv_sass_emit_kill_thread_if(struct nv_sass_buf *b, uint8_t cond_reg)
{
   /* Conditional kill: R2P from cond_reg bit0, then KIL (predicated in full RA).
    * Simplified: always emit R2P from cond_reg then KIL — hardware/predication
    * refinement tracks cond_reg through predicate allocator later. */
   nv_sass_note_reg(b, cond_reg);
   if (!nv_sass_emit_r2p(b, cond_reg, 0, 0))
      return false;
   return nv_sass_emit_raw(b, 0x00000000u, NV_SASS_KIL_HI_BASE);
}

bool
nv_sass_emit_ldg_u32(struct nv_sass_buf *b, uint8_t rd, uint8_t ra_addr)
{
   nv_sass_note_reg(b, rd);
   nv_sass_note_reg(b, ra_addr);
   return nv_sass_emit_raw(b, pack_lo_rrr(rd, ra_addr, 0), NV_SASS_LDG_HI_BASE);
}

bool
nv_sass_emit_stg_u32(struct nv_sass_buf *b, uint8_t ra_addr, uint8_t rb_data)
{
   nv_sass_note_reg(b, ra_addr);
   nv_sass_note_reg(b, rb_data);
   b->has_global_store = true;
   return nv_sass_emit_raw(b, pack_lo_rrr(0, ra_addr, rb_data), NV_SASS_STG_HI_BASE);
}

bool
nv_sass_emit_ldl_u32(struct nv_sass_buf *b, uint8_t rd, uint8_t ra_addr)
{
   /* Local mem load: Rd = LMEM[Ra + imm0]; address reg holds byte offset */
   nv_sass_note_reg(b, rd);
   nv_sass_note_reg(b, ra_addr);
   return nv_sass_emit_raw(b, pack_lo_rrr(rd, ra_addr, 0), NV_SASS_LDL_HI_BASE);
}

bool
nv_sass_emit_stl_u32(struct nv_sass_buf *b, uint8_t ra_addr, uint8_t rb_data)
{
   /* Local mem store: LMEM[Ra] = Rb */
   nv_sass_note_reg(b, ra_addr);
   nv_sass_note_reg(b, rb_data);
   return nv_sass_emit_raw(b, pack_lo_rrr(0, ra_addr, rb_data), NV_SASS_STL_HI_BASE);
}

bool
nv_sass_emit_lds_u32(struct nv_sass_buf *b, uint8_t rd, uint8_t ra_addr)
{
   nv_sass_note_reg(b, rd);
   nv_sass_note_reg(b, ra_addr);
   return nv_sass_emit_raw(b, pack_lo_rrr(rd, ra_addr, 0), NV_SASS_LDS_HI_BASE);
}

bool
nv_sass_emit_sts_u32(struct nv_sass_buf *b, uint8_t ra_addr, uint8_t rb_data)
{
   nv_sass_note_reg(b, ra_addr);
   nv_sass_note_reg(b, rb_data);
   return nv_sass_emit_raw(b, pack_lo_rrr(0, ra_addr, rb_data), NV_SASS_STS_HI_BASE);
}

uint32_t
nv_sass_buf_copy_out(const struct nv_sass_buf *b,
                     uint32_t *dst, uint32_t dst_cap_dwords)
{
   uint32_t n;
   if (!b || !dst || !b->dwords)
      return 0;
   n = b->count;
   if (n > dst_cap_dwords)
      n = dst_cap_dwords;
   memcpy(dst, b->dwords, n * sizeof(uint32_t));
   return n;
}


bool
nv_sass_emit_shf_l(struct nv_sass_buf *b, uint8_t rd, uint8_t ra, uint8_t rb)
{
   nv_sass_note_reg(b, rd);
   nv_sass_note_reg(b, ra);
   nv_sass_note_reg(b, rb);
   return nv_sass_emit_raw(b, pack_lo_rrr(rd, ra, rb), NV_SASS_SHF_L_HI_BASE);
}

bool
nv_sass_emit_shf_r(struct nv_sass_buf *b, uint8_t rd, uint8_t ra, uint8_t rb,
                   bool arithmetic)
{
   nv_sass_note_reg(b, rd);
   nv_sass_note_reg(b, ra);
   nv_sass_note_reg(b, rb);
   uint32_t hi = NV_SASS_SHF_R_HI_BASE;
   if (arithmetic)
      hi |= (1u << 12); /* arithmetic shift flag approx */
   return nv_sass_emit_raw(b, pack_lo_rrr(rd, ra, rb), hi);
}

bool
nv_sass_emit_imnmx(struct nv_sass_buf *b, uint8_t rd, uint8_t ra, uint8_t rb,
                   bool is_max, bool is_signed)
{
   nv_sass_note_reg(b, rd);
   nv_sass_note_reg(b, ra);
   nv_sass_note_reg(b, rb);
   uint32_t hi = NV_SASS_IMNMX_HI_BASE;
   if (is_max)
      hi |= NV_SASS_MINMAX_MAX_BIT;
   if (!is_signed)
      hi |= (1u << 10); /* unsigned variant bit approx */
   return nv_sass_emit_raw(b, pack_lo_rrr(rd, ra, rb), hi);
}

bool
nv_sass_emit_fmnmx(struct nv_sass_buf *b, uint8_t rd, uint8_t ra, uint8_t rb,
                   bool is_max)
{
   nv_sass_note_reg(b, rd);
   nv_sass_note_reg(b, ra);
   nv_sass_note_reg(b, rb);
   uint32_t hi = NV_SASS_FMNMX_HI_BASE;
   if (is_max)
      hi |= NV_SASS_MINMAX_MAX_BIT;
   return nv_sass_emit_raw(b, pack_lo_rrr(rd, ra, rb), hi);
}

bool
nv_sass_emit_f2i(struct nv_sass_buf *b, uint8_t rd, uint8_t ra, bool is_signed)
{
   nv_sass_note_reg(b, rd);
   nv_sass_note_reg(b, ra);
   uint32_t hi = NV_SASS_F2I_HI_BASE;
   if (!is_signed)
      hi |= (1u << 13);
   return nv_sass_emit_raw(b, pack_lo_rrr(rd, ra, 0xff), hi);
}

bool
nv_sass_emit_i2f(struct nv_sass_buf *b, uint8_t rd, uint8_t ra, bool is_signed)
{
   nv_sass_note_reg(b, rd);
   nv_sass_note_reg(b, ra);
   uint32_t hi = NV_SASS_I2F_HI_BASE;
   if (!is_signed)
      hi |= (1u << 13);
   return nv_sass_emit_raw(b, pack_lo_rrr(rd, ra, 0xff), hi);
}

bool
nv_sass_emit_mufu(struct nv_sass_buf *b, uint8_t rd, uint8_t ra, uint8_t mufu_op)
{
   nv_sass_note_reg(b, rd);
   nv_sass_note_reg(b, ra);
   return nv_sass_emit_raw(b,
                           (uint32_t)rd | ((uint32_t)ra << 8) | ((uint32_t)mufu_op << 20),
                           NV_SASS_MUFU_HI_BASE);
}

bool
nv_sass_emit_selp(struct nv_sass_buf *b, uint8_t rd, uint8_t ra, uint8_t rb,
                  uint8_t pred)
{
   nv_sass_note_reg(b, rd);
   nv_sass_note_reg(b, ra);
   nv_sass_note_reg(b, rb);
   /* SELP Rd, Ra, Rb, Pu — predicate in hi low bits (approx) */
   return nv_sass_emit_raw(b, pack_lo_rrr(rd, ra, rb),
                           NV_SASS_SELP_HI_BASE | ((uint32_t)(pred & 7) << 8));
}

bool
nv_sass_emit_isetp(struct nv_sass_buf *b, uint8_t pred_dst, uint8_t ra,
                   uint8_t rb, bool is_signed, bool is_eq)
{
   nv_sass_note_reg(b, ra);
   nv_sass_note_reg(b, rb);
   uint32_t hi = NV_SASS_ISETP_HI_BASE;
   if (!is_signed)
      hi |= (1u << 10);
   if (is_eq)
      hi |= (1u << 14); /* EQ vs LT/LE etc; refined later */
   return nv_sass_emit_raw(b,
                           ((uint32_t)(pred_dst & 7) << 0) |
                           ((uint32_t)ra << 8) | ((uint32_t)rb << 16),
                           hi);
}

bool
nv_sass_emit_fsetp(struct nv_sass_buf *b, uint8_t pred_dst, uint8_t ra,
                   uint8_t rb, bool is_eq)
{
   nv_sass_note_reg(b, ra);
   nv_sass_note_reg(b, rb);
   uint32_t hi = NV_SASS_FSETP_HI_BASE;
   if (is_eq)
      hi |= (1u << 14);
   return nv_sass_emit_raw(b,
                           ((uint32_t)(pred_dst & 7) << 0) |
                           ((uint32_t)ra << 8) | ((uint32_t)rb << 16),
                           hi);
}

bool
nv_sass_emit_shfl(struct nv_sass_buf *b, uint8_t rd, uint8_t ra,
                  uint8_t rb_idx, uint8_t mode)
{
   nv_sass_note_reg(b, rd);
   nv_sass_note_reg(b, ra);
   nv_sass_note_reg(b, rb_idx);
   return nv_sass_emit_raw(b, pack_lo_rrr(rd, ra, rb_idx),
                           NV_SASS_SHFL_HI_BASE | ((uint32_t)(mode & 3) << 12));
}

bool
nv_sass_emit_bar_sync(struct nv_sass_buf *b, uint8_t barrier_id)
{
   /* BAR.SYNC barrier_id — CTA-wide barrier for compute */
   return nv_sass_emit_raw(b, (uint32_t)(barrier_id & 0x1f), NV_SASS_BAR_HI_BASE);
}

bool
nv_sass_emit_membar(struct nv_sass_buf *b)
{
   return nv_sass_emit_raw(b, 0, NV_SASS_MEMBAR_HI_BASE);
}

bool
nv_sass_emit_atom(struct nv_sass_buf *b, uint8_t rd, uint8_t ra_addr,
                  uint8_t rb_data, uint8_t atom_op)
{
   nv_sass_note_reg(b, rd);
   nv_sass_note_reg(b, ra_addr);
   nv_sass_note_reg(b, rb_data);
   b->has_global_store = true;
   return nv_sass_emit_raw(b, pack_lo_rrr(rd, ra_addr, rb_data),
                           NV_SASS_ATOM_HI_BASE | ((uint32_t)(atom_op & 0xf) << 9));
}

bool
nv_sass_emit_bra(struct nv_sass_buf *b, int32_t rel_insn_offset)
{
   /* BRA offset — offset in instructions (approx; byte scale refined later) */
   uint32_t imm = (uint32_t)rel_insn_offset & 0xffffffu;
   return nv_sass_emit_raw(b, imm, NV_SASS_BRA_HI_BASE);
}

bool
nv_sass_emit_bra_pred(struct nv_sass_buf *b, int32_t rel_insn_offset,
                      uint8_t pred, bool not_pred)
{
   uint32_t imm = (uint32_t)rel_insn_offset & 0xffffffu;
   uint32_t hi = NV_SASS_BRA_HI_BASE | ((uint32_t)(pred & 7) << 8);
   if (not_pred)
      hi |= (1u << 12);
   return nv_sass_emit_raw(b, imm, hi);
}

bool
nv_sass_emit_ldc(struct nv_sass_buf *b, uint8_t rd, uint8_t bank,
                 uint16_t offset_dwords)
{
   nv_sass_note_reg(b, rd);
   /* LDC Rd, c[bank][offset]; bank in bits, offset in dwords * 4 for byte addr */
   uint32_t lo = (uint32_t)rd | ((uint32_t)(offset_dwords & 0xffff) << 8);
   uint32_t hi = NV_SASS_LDC_HI_BASE | ((uint32_t)(bank & 0x1f) << 0);
   return nv_sass_emit_raw(b, lo, hi);
}

bool
nv_sass_emit_tex(struct nv_sass_buf *b, uint8_t rd, uint8_t ra_coord,
                 uint8_t tex_idx)
{
   nv_sass_note_reg(b, rd);
   nv_sass_note_reg(b, ra_coord);
   b->has_tex = true;
   return nv_sass_emit_raw(b,
                           pack_lo_rrr(rd, ra_coord, 0) | ((uint32_t)tex_idx << 24),
                           NV_SASS_TEX_HI_BASE);
}

bool
nv_sass_emit_tld(struct nv_sass_buf *b, uint8_t rd, uint8_t ra_coord,
                 uint8_t tex_idx)
{
   nv_sass_note_reg(b, rd);
   nv_sass_note_reg(b, ra_coord);
   b->has_tex = true;
   return nv_sass_emit_raw(b,
                           pack_lo_rrr(rd, ra_coord, 0) | ((uint32_t)tex_idx << 24),
                           NV_SASS_TLD_HI_BASE);
}

bool
nv_sass_emit_txq(struct nv_sass_buf *b, uint8_t rd, uint8_t tex_idx,
                 uint8_t query_type)
{
   nv_sass_note_reg(b, rd);
   b->has_tex = true;
   /* TXQ Rd, tex#, query — returns width/height/depth/levels depending on type */
   return nv_sass_emit_raw(b,
                           (uint32_t)rd | ((uint32_t)(query_type & 0xf) << 8) |
                           ((uint32_t)tex_idx << 24),
                           NV_SASS_TXQ_HI_BASE);
}

bool
nv_sass_emit_iadd_neg_rb(struct nv_sass_buf *b, uint8_t rd, uint8_t ra,
                         uint8_t rb)
{
   /* IADD Rd, Ra, -Rb: set negate-B modifier in hi (bit varies by SM; bit 9 common) */
   nv_sass_note_reg(b, rd);
   nv_sass_note_reg(b, ra);
   nv_sass_note_reg(b, rb);
   return nv_sass_emit_raw(b, pack_lo_rrr(rd, ra, rb),
                           NV_SASS_IADD_HI_BASE | (1u << 9));
}
