/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Pushbuffer method encoding from class headers NV906F/NVC36F (SET_OBJECT,
 * semaphores, NOP) and the classic Fermi+ incrementing method format used
 * by engine classes (NVC597 3D, NVC6B5 copy, etc.).
 *
 * GPFIFO entry format: NV506F_GP_ENTRY / NVC36F_GP_ENTRY (8 bytes).
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

/* Subchannel assignments used by the proprietary driver / nvidia-push */
#define NV_PUSH_SUBCH_3D       0
#define NV_PUSH_SUBCH_COMPUTE  1
#define NV_PUSH_SUBCH_2D       2
#define NV_PUSH_SUBCH_M2MF     3  /* or COPY on modern */
#define NV_PUSH_SUBCH_COPY     4
#define NV_PUSH_SUBCH_SW       7

/* Host/GPFIFO methods (NVC36F / NV906F) - on subchannel via SET_OBJECT first */
#define NVC36F_SET_OBJECT              0x00000000
#define NVC36F_ILLEGAL                 0x00000004
#define NVC36F_NOP                     0x00000008
#define NVC36F_SEMAPHOREA              0x00000010
#define NVC36F_SEMAPHOREB              0x00000014
#define NVC36F_SEMAPHOREC              0x00000018
#define NVC36F_SEMAPHORED              0x0000001C
#define NVC36F_NON_STALL_INTERRUPT     0x00000020
#define NVC36F_FB_FLUSH                0x00000024
#define NVC36F_MEM_OP_A                0x00000028
#define NVC36F_MEM_OP_B                0x0000002C
#define NVC36F_MEM_OP_C                0x00000030
#define NVC36F_MEM_OP_D                0x00000034
#define NVC36F_WFI                     0x00000078
#define NVC36F_CRC_CHECK               0x0000007C
#define NVC36F_YIELD                   0x00000080

/* SEMAPHORED operations */
#define NVC36F_SEMAPHORED_OPERATION_ACQUIRE      0x00000001
#define NVC36F_SEMAPHORED_OPERATION_RELEASE      0x00000002
#define NVC36F_SEMAPHORED_OPERATION_ACQ_GEQ      0x00000004
#define NVC36F_SEMAPHORED_RELEASE_WFI_DIS        (1u << 20)
#define NVC36F_SEMAPHORED_RELEASE_SIZE_4BYTE     (1u << 24)

/* GPFIFO entry (NV506F/NVC36F) */
#define NV_GP_ENTRY_SIZE               8
#define NV_GP_ENTRY0_GET_SHIFT         2
#define NV_GP_ENTRY1_GET_HI_MASK       0xff
#define NV_GP_ENTRY1_PRIV_SHIFT        8
#define NV_GP_ENTRY1_LEVEL_SHIFT       9
#define NV_GP_ENTRY1_LENGTH_SHIFT      10
#define NV_GP_ENTRY1_LENGTH_MASK       0x1fffff

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
nv_push_init_at(struct nv_push *p, uint32_t *map, uint32_t *cur, uint32_t dw_cap)
{
   p->start = map;
   p->cur = cur;
   p->end = map + dw_cap;
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

/*
 * Fermi+ incrementing method header (used by engine method streams):
 *   bits 31:29 = 000 (inc method)
 *   bits 28:16 = method count
 *   bits 15:13 = subchannel
 *   bits 12:0  = method address >> 2
 *
 * Non-incrementing: bit 30 set.
 * Immediates: bit 29 set, imm in 28:16.
 */
static inline uint32_t
nv_push_hdr_inc(uint32_t subch, uint32_t method, uint32_t count)
{
   return ((count & 0x1fff) << 16) | ((subch & 7) << 13) | ((method >> 2) & 0x1fff);
}

static inline uint32_t
nv_push_hdr_noninc(uint32_t subch, uint32_t method, uint32_t count)
{
   return (1u << 30) | ((count & 0x1fff) << 16) | ((subch & 7) << 13) |
          ((method >> 2) & 0x1fff);
}

static inline uint32_t
nv_push_hdr_imm(uint32_t subch, uint32_t method, uint32_t imm)
{
   return (1u << 29) | ((imm & 0x1fff) << 16) | ((subch & 7) << 13) |
          ((method >> 2) & 0x1fff);
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

static inline void
nv_push_set_object(struct nv_push *p, uint32_t engine_class)
{
   nv_push_method(p, NVC36F_SET_OBJECT, engine_class & 0xffff);
}

static inline void
nv_push_nop(struct nv_push *p)
{
   nv_push_method(p, NVC36F_NOP, 0);
}

/* Host semaphore release (4-byte payload) at GPU sema address */
static inline void
nv_push_sema_release(struct nv_push *p, uint64_t sema_gpu_addr, uint32_t payload)
{
   nv_push_method(p, NVC36F_SEMAPHOREA, (uint32_t)(sema_gpu_addr >> 32) & 0xff);
   nv_push_method(p, NVC36F_SEMAPHOREB, (uint32_t)(sema_gpu_addr & ~0x3u));
   nv_push_method(p, NVC36F_SEMAPHOREC, payload);
   nv_push_method(p, NVC36F_SEMAPHORED,
                  NVC36F_SEMAPHORED_OPERATION_RELEASE |
                  NVC36F_SEMAPHORED_RELEASE_WFI_DIS |
                  NVC36F_SEMAPHORED_RELEASE_SIZE_4BYTE);
}

static inline void
nv_push_wfi(struct nv_push *p)
{
   nv_push_method(p, NVC36F_WFI, 0);
}

static inline void
nv_gp_entry_pack(uint32_t entry[2], uint64_t gpu_addr, uint32_t length_dwords)
{
   entry[0] = (uint32_t)((gpu_addr >> NV_GP_ENTRY0_GET_SHIFT) << NV_GP_ENTRY0_GET_SHIFT);
   entry[1] = ((uint32_t)(gpu_addr >> 32) & NV_GP_ENTRY1_GET_HI_MASK) |
              ((length_dwords & NV_GP_ENTRY1_LENGTH_MASK) << NV_GP_ENTRY1_LENGTH_SHIFT);
}

#ifdef __cplusplus
}
#endif

#endif /* NV_PUSH_H */
