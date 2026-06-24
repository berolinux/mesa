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
