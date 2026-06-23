/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Pushbuffer / GPFIFO command stream builder.
 * Method header format follows NVIDIA engine method encoding used by the
 * proprietary driver and documented in class headers (NV906F / NVC36F etc.).
 */

#ifndef NV_PUSH_H
#define NV_PUSH_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Method header: [subchannel:3 | count:13 | method:13 | inc:1?] varies by
 * GPU generation.  We use the NVC36F / Turing+ "new" format when possible:
 *   bits 31-29: subchannel
 *   bits 28-16: method count (N methods)
 *   bits 15-0 : method address >> 2  (or inc/non-inc opcode in low bits)
 *
 * For initial bring-up we emit the classic Fermi+ incrementing method header
 * which is still accepted on modern GPUs for the engines we target:
 *   (subch << 13) | (count << 16) | (method >> 2)   [varies]
 *
 * The exact encoding will be refined from binary driver disassembly; for now
 * we provide helpers that match the layout used by nvkms / RM samples.
 */

#define NV_PUSH_SUBCH_3D       0
#define NV_PUSH_SUBCH_COMPUTE  1
#define NV_PUSH_SUBCH_2D       2
#define NV_PUSH_SUBCH_M2MF     3
#define NV_PUSH_SUBCH_COPY     4

struct nv_push {
   uint32_t *start;
   uint32_t *end;
   uint32_t *cur;
   uint32_t  subch;
};

static inline void
nv_push_init(struct nv_push *p, uint32_t *map, uint32_t dw_count)
{
   p->start = map;
   p->cur = map;
   p->end = map + dw_count;
   p->subch = NV_PUSH_SUBCH_3D;
}

static inline void
nv_push_set_subch(struct nv_push *p, uint32_t subch)
{
   p->subch = subch;
}

static inline uint32_t
nv_push_dw_count(const struct nv_push *p)
{
   return (uint32_t)(p->cur - p->start);
}

static inline bool
nv_push_space(const struct nv_push *p, uint32_t dwords)
{
   return (p->cur + dwords) <= p->end;
}

/* Classic incrementing methods header (Fermi+ engines) */
static inline uint32_t
nv_push_hdr_inc(uint32_t subch, uint32_t method, uint32_t count)
{
   /* method is byte offset; store as dword index in low 12 bits, count in 28:16, subch in 15:13 */
   return ((count) << 16) | ((subch) << 13) | ((method) >> 2);
}

/* Non-incrementing methods (same method, multiple data) */
static inline uint32_t
nv_push_hdr_noninc(uint32_t subch, uint32_t method, uint32_t count)
{
   return (1u << 30) | ((count) << 16) | ((subch) << 13) | ((method) >> 2);
}

/* Immediate data method (1 dword inline) */
static inline uint32_t
nv_push_hdr_imm(uint32_t subch, uint32_t method, uint32_t imm)
{
   return (1u << 29) | ((imm & 0x1fff) << 16) | ((subch) << 13) | ((method) >> 2);
}

static inline void
nv_push_dword(struct nv_push *p, uint32_t dw)
{
   assert(p->cur < p->end);
   *p->cur++ = dw;
}

static inline void
nv_push_method(struct nv_push *p, uint32_t method, uint32_t data)
{
   assert(nv_push_space(p, 2));
   nv_push_dword(p, nv_push_hdr_inc(p->subch, method, 1));
   nv_push_dword(p, data);
}

static inline void
nv_push_methodN(struct nv_push *p, uint32_t method, const uint32_t *data,
                uint32_t count)
{
   uint32_t i;
   assert(nv_push_space(p, 1 + count));
   nv_push_dword(p, nv_push_hdr_inc(p->subch, method, count));
   for (i = 0; i < count; i++)
      nv_push_dword(p, data[i]);
}

static inline void
nv_push_1inc(struct nv_push *p, uint32_t method, uint32_t a)
{
   nv_push_method(p, method, a);
}

static inline void
nv_push_2inc(struct nv_push *p, uint32_t method, uint32_t a, uint32_t b)
{
   uint32_t d[2] = { a, b };
   nv_push_methodN(p, method, d, 2);
}

/* GPFIFO entry: 8-byte { offset_lo, {offset_hi:8, length:21, wait:1, ...} } */
struct nv_gp_entry {
   uint32_t offset_lo;
   uint32_t offset_hi_length;
};

#define NV_GP_ENTRY_LENGTH_SHIFT 10
#define NV_GP_ENTRY_PRIV         (1u << 2)
#define NV_GP_ENTRY_LEVEL        (1u << 3)
#define NV_GP_ENTRY_SYNC         (1u << 4)
#define NV_GP_ENTRY_OPCODE_NOP   (0u << 0)
#define NV_GP_ENTRY_OPCODE_ILLEGAL (1u << 0)
#define NV_GP_ENTRY_OPCODE_GP_CRC  (2u << 0)
#define NV_GP_ENTRY_OPCODE_PB_CRC  (3u << 0)

static inline void
nv_gp_entry_init(struct nv_gp_entry *e, uint64_t gpu_addr, uint32_t length_dwords,
                 bool wait)
{
   e->offset_lo = (uint32_t)(gpu_addr & 0xffffffffu);
   e->offset_hi_length = ((uint32_t)(gpu_addr >> 32) & 0xff) |
                         ((length_dwords & 0x1fffff) << NV_GP_ENTRY_LENGTH_SHIFT);
   if (wait)
      e->offset_hi_length |= NV_GP_ENTRY_SYNC;
}

#ifdef __cplusplus
}
#endif

#endif /* NV_PUSH_H */
