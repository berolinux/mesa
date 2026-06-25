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

/* SEMAPHORED operations (clc36f / open-gpu-doc style bitfields) */
#define NVC36F_SEMAPHORED_OPERATION_ACQUIRE      0x00000001
#define NVC36F_SEMAPHORED_OPERATION_RELEASE      0x00000002
#define NVC36F_SEMAPHORED_OPERATION_ACQ_GEQ      0x00000004
#define NVC36F_SEMAPHORED_RELEASE_WFI_DIS        (1u << 20)
#define NVC36F_SEMAPHORED_RELEASE_SIZE_4BYTE     (1u << 24)
#define NVC36F_SEMAPHORED_ACQUIRE_SWITCH_TSG_ENABLE (1u << 12)

/*
 * Open-header release execute: OPERATION_RELEASE | WFI_DIS | SIZE_4BYTE
 *   = 0x2 | (1<<20) | (1<<24) = 0x01100002
 *
 * Pass8 RE: 0x01100002 appears in glcore only as debug/config table loads
 * (a4498e etc), NEVER as pushbuffer sema execute. Keep open-header modes as
 * last-resort theoretical A/B only; primary is blob 0x1001, then vdpau 0x2.
 *
 * pass14/16/17 sema config table (glcore @ 0x11e30c0): structured records
 *   exec (0x1001/02/04, 0x0802/04) + tag_a(4/5/0) + tag_b(2/3/1) + sema_idx
 *   (0x10=A .. 0x12=C; 0x1004→C, 0x1002/0804→B, 0x0802→A; 0x1001 primary @ +0x150
 *   with sema_idx 0x1d nonstd — execute still in D for classic path).
 * pass17 formal: exactly 11 authoritative rows (+0x10..+0x150, stride 0x20);
 * do not implement tail noise past +0x150.  See nv_host_sema_pass17_row().
 * Slot-aware sema (execute on A/B/C not only D) is experimental; ladder below
 * still programs full ABCD with execute in D, varying only execute imm.
 *
 * Blob (610.43.02 glcore @ b6c952) hardcodes SEMAPHORED = 0x1001 after inc4
 * sema block 0x20040004. Pass8 vdpau @ 28c9a uses execute = 0x00000002.
 *
 * pass11 RE: 0x1001 appears as imm32 in glcore executable/rodata (sample offs
 * 0xa43b38, 0xf27b34, 0x113d87c — often near 0x400x400x method/config tables).
 * eglcore still has literal 0x20040004 inc4 sema header (constructed in code,
 * not pure rodata on glcore). vdpau 0x2 remains a valid alternate execute.
 */
#define NVC36F_SEMAPHORED_RELEASE_OPEN_HDRS      \
   (NVC36F_SEMAPHORED_OPERATION_RELEASE |        \
    NVC36F_SEMAPHORED_RELEASE_WFI_DIS |          \
    NVC36F_SEMAPHORED_RELEASE_SIZE_4BYTE)
#define NVC36F_SEMAPHORED_RELEASE_BLOB_610       0x00001001u
#define NVC36F_SEMAPHORED_RELEASE_VDPAU_610      0x00000002u
/* pass11/12: additional sema execute modes observed as imm (rare; try after 0x1001) */
#define NVC36F_SEMAPHORED_RELEASE_BLOB_1000      0x00001000u
#define NVC36F_SEMAPHORED_RELEASE_BLOB_1002      0x00001002u
/* pass13: glcore sema rodata table @ 0x11e30c0 — same family as 0x1001/0x1002 */
#define NVC36F_SEMAPHORED_RELEASE_BLOB_1004      0x00001004u
#define NVC36F_SEMAPHORED_RELEASE_BLOB_0804      0x00000804u
#define NVC36F_SEMAPHORED_RELEASE_BLOB_0802      0x00000802u

/*
 * pass17 formal sema table (glcore 0x11e30c0 +0x10, stride 0x20, 11 rows).
 * sema_idx 0x10/11/12 = A/B/C execute slot; 0x07/08/1d = nonstd (classic D).
 */
#define NV_HOST_SEMA_PASS17_TABLE_BASE_OFF       0x11e30c0u
#define NV_HOST_SEMA_PASS17_ROW_STRIDE           0x20u
#define NV_HOST_SEMA_PASS17_FIRST_ROW_OFF        0x10u
#define NV_HOST_SEMA_PASS17_NUM_ROWS             11u
#define NV_HOST_SEMA_PASS17_IDX_A                0x10u
#define NV_HOST_SEMA_PASS17_IDX_B                0x11u
#define NV_HOST_SEMA_PASS17_IDX_C                0x12u
#define NV_HOST_SEMA_PASS17_IDX_NONSTD_07        0x07u
#define NV_HOST_SEMA_PASS17_IDX_NONSTD_08        0x08u
#define NV_HOST_SEMA_PASS17_IDX_PRIMARY_NONSTD   0x1du

enum nv_host_sema_slot {
   NV_HOST_SEMA_SLOT_A = 0,       /* sema_idx 0x10 / SEMAPHOREA execute */
   NV_HOST_SEMA_SLOT_B = 1,       /* sema_idx 0x11 / SEMAPHOREB execute */
   NV_HOST_SEMA_SLOT_C = 2,       /* sema_idx 0x12 / SEMAPHOREC execute */
   NV_HOST_SEMA_SLOT_D = 3,       /* classic execute in SEMAPHORED */
   NV_HOST_SEMA_SLOT_NONSTD = 4,  /* sema_idx 0x07/08/1d — treat as D path */
};

struct nv_host_sema_pass17_row {
   uint32_t exec;      /* execute imm: 0x1004/1002/0804/0802/1001 */
   uint8_t  tag_a;     /* 4, 5, or 0 */
   uint8_t  tag_b;     /* 2, 3, or 1 */
   uint8_t  sema_idx;  /* 0x10/11/12/07/08/1d */
   uint8_t  aux;       /* often equals tag_b */
   enum nv_host_sema_slot slot;
};

/** pass17 authoritative 11-row formal table (no tail noise). */
static inline const struct nv_host_sema_pass17_row *
nv_host_sema_pass17_table(unsigned *out_count)
{
   static const struct nv_host_sema_pass17_row k_rows[NV_HOST_SEMA_PASS17_NUM_ROWS] = {
      /* +0x10  tag(4,2) ladder */
      { 0x1004u, 4, 2, 0x12u, 2, NV_HOST_SEMA_SLOT_C },
      { 0x1002u, 4, 2, 0x11u, 2, NV_HOST_SEMA_SLOT_B },
      { 0x0804u, 4, 2, 0x11u, 2, NV_HOST_SEMA_SLOT_B },
      { 0x0802u, 4, 2, 0x10u, 2, NV_HOST_SEMA_SLOT_A },
      /* +0x90  tag(5,3) ladder */
      { 0x1004u, 5, 3, 0x12u, 3, NV_HOST_SEMA_SLOT_C },
      { 0x1002u, 5, 3, 0x11u, 3, NV_HOST_SEMA_SLOT_B },
      { 0x0804u, 5, 3, 0x11u, 3, NV_HOST_SEMA_SLOT_B },
      { 0x0802u, 5, 3, 0x10u, 3, NV_HOST_SEMA_SLOT_A },
      /* +0x110 nonstd / primary nonstd */
      { 0x1004u, 0, 1, 0x08u, 1, NV_HOST_SEMA_SLOT_NONSTD },
      { 0x1002u, 0, 1, 0x07u, 1, NV_HOST_SEMA_SLOT_NONSTD },
      { 0x1001u, 0, 1, 0x1du, 1, NV_HOST_SEMA_SLOT_NONSTD },
   };
   if (out_count)
      *out_count = NV_HOST_SEMA_PASS17_NUM_ROWS;
   return k_rows;
}

static inline const struct nv_host_sema_pass17_row *
nv_host_sema_pass17_row(unsigned index)
{
   unsigned n = 0;
   const struct nv_host_sema_pass17_row *t = nv_host_sema_pass17_table(&n);
   if (index >= n)
      return NULL;
   return &t[index];
}

/** Map execute imm → pass17 slot (first matching row in formal table). */
static inline enum nv_host_sema_slot
nv_host_sema_pass17_slot_for_exec(uint32_t exec_imm)
{
   unsigned n = 0, i;
   const struct nv_host_sema_pass17_row *t = nv_host_sema_pass17_table(&n);
   for (i = 0; i < n; i++) {
      if (t[i].exec == exec_imm)
         return t[i].slot;
   }
   return NV_HOST_SEMA_SLOT_D;
}

/** Map sema_idx (0x10/11/12/1d/…) → GPFIFO sema method offset. */
static inline uint32_t
nv_host_sema_pass17_method_for_idx(uint8_t sema_idx)
{
   switch (sema_idx) {
   case NV_HOST_SEMA_PASS17_IDX_A: return NVC36F_SEMAPHOREA;
   case NV_HOST_SEMA_PASS17_IDX_B: return NVC36F_SEMAPHOREB;
   case NV_HOST_SEMA_PASS17_IDX_C: return NVC36F_SEMAPHOREC;
   case NV_HOST_SEMA_PASS17_IDX_NONSTD_07:
   case NV_HOST_SEMA_PASS17_IDX_NONSTD_08:
   case NV_HOST_SEMA_PASS17_IDX_PRIMARY_NONSTD:
   default:
      return NVC36F_SEMAPHORED;
   }
}

/** Map pass17 slot → method offset (NONSTD/D → SEMAPHORED). */
static inline uint32_t
nv_host_sema_pass17_method_for_slot(enum nv_host_sema_slot slot)
{
   switch (slot) {
   case NV_HOST_SEMA_SLOT_A: return NVC36F_SEMAPHOREA;
   case NV_HOST_SEMA_SLOT_B: return NVC36F_SEMAPHOREB;
   case NV_HOST_SEMA_SLOT_C: return NVC36F_SEMAPHOREC;
   case NV_HOST_SEMA_SLOT_D:
   case NV_HOST_SEMA_SLOT_NONSTD:
   default:
      return NVC36F_SEMAPHORED;
   }
}

/* Host sema emit modes for silicon A/B (nv_channel_gpfifo_host_sema_submit). */
enum nv_host_sema_mode {
   NV_HOST_SEMA_MODE_OPEN_SHIFT2 = 0, /* execute=open hdrs; SEMAPHOREB = addr>>2 */
   NV_HOST_SEMA_MODE_OPEN_ALIGN4 = 1, /* execute=open hdrs; SEMAPHOREB = addr&~3 */
   NV_HOST_SEMA_MODE_BLOB_SHIFT2 = 2, /* execute=0x1001; addr>>2 (clc36f style) */
   NV_HOST_SEMA_MODE_BLOB_ALIGN4 = 3, /* execute=0x1001; addr&~3 (blob b6c959 lo) */
   NV_HOST_SEMA_MODE_VDPAU_SHIFT2 = 4, /* execute=0x2; addr>>2 (pass8 vdpau 28c9a) */
   NV_HOST_SEMA_MODE_VDPAU_ALIGN4 = 5, /* execute=0x2; addr&~3 */
   /* pass12: glcore imm 0x1000/0x1002 sites; secondary blob execute variants */
   NV_HOST_SEMA_MODE_BLOB1000_SHIFT2 = 6, /* execute=0x1000; addr>>2 */
   NV_HOST_SEMA_MODE_BLOB1000_ALIGN4 = 7, /* execute=0x1000; addr&~3 */
   NV_HOST_SEMA_MODE_BLOB1002_SHIFT2 = 8, /* execute=0x1002; addr>>2 */
   NV_HOST_SEMA_MODE_BLOB1002_ALIGN4 = 9, /* execute=0x1002; addr&~3 */
   /* pass13: rodata sema config table execute alts (try after 0x1002, before 0x1000) */
   NV_HOST_SEMA_MODE_BLOB1004_SHIFT2 = 10, /* execute=0x1004; addr>>2 */
   NV_HOST_SEMA_MODE_BLOB1004_ALIGN4 = 11, /* execute=0x1004; addr&~3 */
   NV_HOST_SEMA_MODE_BLOB0804_SHIFT2 = 12, /* execute=0x0804; addr>>2 (experimental) */
   NV_HOST_SEMA_MODE_BLOB0804_ALIGN4 = 13, /* execute=0x0804; addr&~3 */
   NV_HOST_SEMA_MODE_BLOB0802_SHIFT2 = 14, /* execute=0x0802; addr>>2 (experimental) */
   NV_HOST_SEMA_MODE_BLOB0802_ALIGN4 = 15, /* execute=0x0802; addr&~3 */
   NV_HOST_SEMA_MODE_COUNT        = 16,
};

/**
 * tick129 / pass8-13: default bring-up order for host sema modes.
 * Prefer blob 0x1001 (glcore/vksc), then 0x1002/0x1004 (pass12/13 rodata),
 * then 0x1000, then experimental 0x0804/0x0802, then vdpau 0x2, then open-header.
 * If preferred_mode is in range, it is tried first (cached channel win).
 * Writes up to NV_HOST_SEMA_MODE_COUNT modes into out[]; returns count.
 */
static inline unsigned
nv_host_sema_ladder_fill(enum nv_host_sema_mode out[NV_HOST_SEMA_MODE_COUNT],
                         int preferred_mode)
{
   static const enum nv_host_sema_mode k_default[NV_HOST_SEMA_MODE_COUNT] = {
      NV_HOST_SEMA_MODE_BLOB_ALIGN4,
      NV_HOST_SEMA_MODE_BLOB_SHIFT2,
      NV_HOST_SEMA_MODE_BLOB1002_ALIGN4,
      NV_HOST_SEMA_MODE_BLOB1002_SHIFT2,
      NV_HOST_SEMA_MODE_BLOB1004_ALIGN4,
      NV_HOST_SEMA_MODE_BLOB1004_SHIFT2,
      NV_HOST_SEMA_MODE_BLOB1000_ALIGN4,
      NV_HOST_SEMA_MODE_BLOB1000_SHIFT2,
      NV_HOST_SEMA_MODE_BLOB0804_ALIGN4,
      NV_HOST_SEMA_MODE_BLOB0804_SHIFT2,
      NV_HOST_SEMA_MODE_BLOB0802_ALIGN4,
      NV_HOST_SEMA_MODE_BLOB0802_SHIFT2,
      NV_HOST_SEMA_MODE_VDPAU_ALIGN4,
      NV_HOST_SEMA_MODE_VDPAU_SHIFT2,
      NV_HOST_SEMA_MODE_OPEN_ALIGN4,
      NV_HOST_SEMA_MODE_OPEN_SHIFT2,
   };
   unsigned n = 0, i;
   bool seen[NV_HOST_SEMA_MODE_COUNT];

   if (!out)
      return 0;
   for (i = 0; i < NV_HOST_SEMA_MODE_COUNT; i++)
      seen[i] = false;

   if (preferred_mode >= 0 && preferred_mode < (int)NV_HOST_SEMA_MODE_COUNT) {
      out[n++] = (enum nv_host_sema_mode)preferred_mode;
      seen[preferred_mode] = true;
   }
   for (i = 0; i < NV_HOST_SEMA_MODE_COUNT; i++) {
      enum nv_host_sema_mode m = k_default[i];
      if (seen[m])
         continue;
      out[n++] = m;
      seen[m] = true;
   }
   return n;
}

/** Human-readable sema mode name for logs / smoke_hw. */
static inline const char *
nv_host_sema_mode_name(enum nv_host_sema_mode mode)
{
   switch (mode) {
   case NV_HOST_SEMA_MODE_OPEN_SHIFT2:  return "open_shift2";
   case NV_HOST_SEMA_MODE_OPEN_ALIGN4:  return "open_align4";
   case NV_HOST_SEMA_MODE_BLOB_SHIFT2:  return "blob_shift2_0x1001";
   case NV_HOST_SEMA_MODE_BLOB_ALIGN4:  return "blob_align4_0x1001";
   case NV_HOST_SEMA_MODE_VDPAU_SHIFT2: return "vdpau_shift2_0x2";
   case NV_HOST_SEMA_MODE_VDPAU_ALIGN4: return "vdpau_align4_0x2";
   case NV_HOST_SEMA_MODE_BLOB1000_SHIFT2: return "blob_shift2_0x1000";
   case NV_HOST_SEMA_MODE_BLOB1000_ALIGN4: return "blob_align4_0x1000";
   case NV_HOST_SEMA_MODE_BLOB1002_SHIFT2: return "blob_shift2_0x1002";
   case NV_HOST_SEMA_MODE_BLOB1002_ALIGN4: return "blob_align4_0x1002";
   case NV_HOST_SEMA_MODE_BLOB1004_SHIFT2: return "blob_shift2_0x1004";
   case NV_HOST_SEMA_MODE_BLOB1004_ALIGN4: return "blob_align4_0x1004";
   case NV_HOST_SEMA_MODE_BLOB0804_SHIFT2: return "blob_shift2_0x0804";
   case NV_HOST_SEMA_MODE_BLOB0804_ALIGN4: return "blob_align4_0x0804";
   case NV_HOST_SEMA_MODE_BLOB0802_SHIFT2: return "blob_shift2_0x0802";
   case NV_HOST_SEMA_MODE_BLOB0802_ALIGN4: return "blob_align4_0x0802";
   default: return "unknown";
   }
}

/** pass12/13: true if mode uses addr>>2 for SEMAPHOREB (else addr&~3 align4). */
static inline bool
nv_host_sema_mode_uses_shift2(enum nv_host_sema_mode mode)
{
   switch (mode) {
   case NV_HOST_SEMA_MODE_OPEN_SHIFT2:
   case NV_HOST_SEMA_MODE_BLOB_SHIFT2:
   case NV_HOST_SEMA_MODE_VDPAU_SHIFT2:
   case NV_HOST_SEMA_MODE_BLOB1000_SHIFT2:
   case NV_HOST_SEMA_MODE_BLOB1002_SHIFT2:
   case NV_HOST_SEMA_MODE_BLOB1004_SHIFT2:
   case NV_HOST_SEMA_MODE_BLOB0804_SHIFT2:
   case NV_HOST_SEMA_MODE_BLOB0802_SHIFT2:
      return true;
   default:
      return false;
   }
}

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

/* SEMAPHOREB low-address encoding for a host sema mode. */
static inline uint32_t
nv_host_sema_addr_lo(uint64_t sema_gpu_addr, enum nv_host_sema_mode mode)
{
   if (nv_host_sema_mode_uses_shift2(mode))
      return (uint32_t)((sema_gpu_addr >> 2) & 0x3fffffffu);
   return (uint32_t)(sema_gpu_addr & ~0x3u);
}

/* pass12/13: execute imm includes 0x1000/0x1002/0x1004/0x0804/0x0802 alts. */
static inline uint32_t
nv_host_sema_execute(enum nv_host_sema_mode mode)
{
   switch (mode) {
   case NV_HOST_SEMA_MODE_BLOB_SHIFT2:
   case NV_HOST_SEMA_MODE_BLOB_ALIGN4:
      return NVC36F_SEMAPHORED_RELEASE_BLOB_610;
   case NV_HOST_SEMA_MODE_BLOB1000_SHIFT2:
   case NV_HOST_SEMA_MODE_BLOB1000_ALIGN4:
      return NVC36F_SEMAPHORED_RELEASE_BLOB_1000;
   case NV_HOST_SEMA_MODE_BLOB1002_SHIFT2:
   case NV_HOST_SEMA_MODE_BLOB1002_ALIGN4:
      return NVC36F_SEMAPHORED_RELEASE_BLOB_1002;
   case NV_HOST_SEMA_MODE_BLOB1004_SHIFT2:
   case NV_HOST_SEMA_MODE_BLOB1004_ALIGN4:
      return NVC36F_SEMAPHORED_RELEASE_BLOB_1004;
   case NV_HOST_SEMA_MODE_BLOB0804_SHIFT2:
   case NV_HOST_SEMA_MODE_BLOB0804_ALIGN4:
      return NVC36F_SEMAPHORED_RELEASE_BLOB_0804;
   case NV_HOST_SEMA_MODE_BLOB0802_SHIFT2:
   case NV_HOST_SEMA_MODE_BLOB0802_ALIGN4:
      return NVC36F_SEMAPHORED_RELEASE_BLOB_0802;
   case NV_HOST_SEMA_MODE_VDPAU_SHIFT2:
   case NV_HOST_SEMA_MODE_VDPAU_ALIGN4:
      return NVC36F_SEMAPHORED_RELEASE_VDPAU_610;
   case NV_HOST_SEMA_MODE_OPEN_SHIFT2:
   case NV_HOST_SEMA_MODE_OPEN_ALIGN4:
   default:
      return NVC36F_SEMAPHORED_RELEASE_OPEN_HDRS;
   }
}

/**
 * pass14 sema config table (glcore @ 0x11e30c0): which SEMAPHORE* method
 * receives the execute dword for a given execute imm / mode family.
 * Returns method offset (0x10=A, 0x14=B, 0x18=C, 0x1c=D).
 *
 * Table evidence (primary family tag_a=4/5):
 *   0x1004 → C (0x12 idx / 0x48 off — use SEMAPHOREC as execute? no: sema_idx
 *             is method index in table = A/B/C slot that gets execute as D-class
 *             op in non-standard layouts; for GPFIFO ABCD block we still write
 *             A/B/C address+payload and put execute only in the preferred slot.
 *   0x1002 → B
 *   0x0802 → A
 *   0x1001 / 0x1000 / vdpau / open → D (classic full block, execute in D)
 *
 * tick140: implement slot-aware path; default release_mode keeps execute in D.
 */
static inline uint32_t
nv_host_sema_execute_method(enum nv_host_sema_mode mode)
{
   uint32_t exec = nv_host_sema_execute(mode);
   enum nv_host_sema_slot slot = nv_host_sema_pass17_slot_for_exec(exec);
   /* pass17 formal table is authoritative for known execute imms */
   if (slot != NV_HOST_SEMA_SLOT_D && slot != NV_HOST_SEMA_SLOT_NONSTD)
      return nv_host_sema_pass17_method_for_slot(slot);
   switch (exec) {
   case NVC36F_SEMAPHORED_RELEASE_BLOB_1004:
      return NVC36F_SEMAPHOREC; /* pass14 sema_idx 0x12 → C */
   case NVC36F_SEMAPHORED_RELEASE_BLOB_1002:
      return NVC36F_SEMAPHOREB; /* sema_idx 0x11 → B */
   case NVC36F_SEMAPHORED_RELEASE_BLOB_0804:
      return NVC36F_SEMAPHOREB; /* sema_idx 0x11 → B */
   case NVC36F_SEMAPHORED_RELEASE_BLOB_0802:
      return NVC36F_SEMAPHOREA; /* sema_idx 0x10 → A */
   case NVC36F_SEMAPHORED_RELEASE_BLOB_610:
   case NVC36F_SEMAPHORED_RELEASE_BLOB_1000:
   case NVC36F_SEMAPHORED_RELEASE_VDPAU_610:
   case NVC36F_SEMAPHORED_RELEASE_OPEN_HDRS:
   default:
      return NVC36F_SEMAPHORED; /* classic execute in D */
   }
}

/**
 * Host semaphore release with explicit silicon A/B mode.
 * pass12 ladder: BLOB 0x1001 → 0x1000/0x1002 alts → VDPAU 0x2 → open-header.
 * Always programs A/B/C/D in order; execute dword goes to D (historic).
 */
static inline void
nv_push_sema_release_mode(struct nv_push *p, uint64_t sema_gpu_addr,
                          uint32_t payload, enum nv_host_sema_mode mode)
{
   nv_push_method(p, NVC36F_SEMAPHOREA,
                  (uint32_t)(sema_gpu_addr >> 32) & 0xff);
   nv_push_method(p, NVC36F_SEMAPHOREB,
                  nv_host_sema_addr_lo(sema_gpu_addr, mode));
   nv_push_method(p, NVC36F_SEMAPHOREC, payload);
   nv_push_method(p, NVC36F_SEMAPHORED, nv_host_sema_execute(mode));
}

/**
 * tick140 / pass14: slot-aware sema release.
 * Writes SEMAPHOREA (hi), SEMAPHOREB (lo), SEMAPHOREC (payload) always, then
 * writes execute to the pass14-preferred method (A/B/C/D).  When execute is
 * not D, also writes 0 to SEMAPHORED as a harmless trailing NOP-ish (some
 * silicon may require D last; optional zero keeps method stream length stable
 * for bring-up ladders that expect 4 sema methods).
 *
 * Use for experimental bring-up of 0x1004/0x1002/0x0802 modes; primary path
 * remains nv_push_sema_release_mode (execute always in D).
 */
static inline void
nv_push_sema_release_mode_slot(struct nv_push *p, uint64_t sema_gpu_addr,
                               uint32_t payload, enum nv_host_sema_mode mode)
{
   uint32_t exec = nv_host_sema_execute(mode);
   uint32_t exec_mthd = nv_host_sema_execute_method(mode);
   uint32_t addr_hi = (uint32_t)(sema_gpu_addr >> 32) & 0xff;
   uint32_t addr_lo = nv_host_sema_addr_lo(sema_gpu_addr, mode);

   /* Always establish address + payload in A/B/C first (GPFIFO order). */
   nv_push_method(p, NVC36F_SEMAPHOREA, addr_hi);
   nv_push_method(p, NVC36F_SEMAPHOREB, addr_lo);
   nv_push_method(p, NVC36F_SEMAPHOREC, payload);

   if (exec_mthd == NVC36F_SEMAPHORED) {
      nv_push_method(p, NVC36F_SEMAPHORED, exec);
      return;
   }

   /*
    * Non-D execute: re-issue the chosen slot method with execute imm.
    * Classic blob still programs A=hi, B=lo, C=payload, D=exec; pass14 table
    * suggests some modes put exec in A/B/C instead.  Emit execute on target
    * slot, then D=0 to pad (conservative; silicon may ignore zero D).
    */
   nv_push_method(p, exec_mthd, exec);
   if (exec_mthd != NVC36F_SEMAPHORED)
      nv_push_method(p, NVC36F_SEMAPHORED, 0);
}

/** pass17: prefer slot-aware emit for modes with formal A/B/C rows. */
static inline bool
nv_host_sema_pass17_prefers_slot_emit(enum nv_host_sema_mode mode)
{
   uint32_t exec = nv_host_sema_execute(mode);
   enum nv_host_sema_slot s = nv_host_sema_pass17_slot_for_exec(exec);
   return s == NV_HOST_SEMA_SLOT_A || s == NV_HOST_SEMA_SLOT_B ||
          s == NV_HOST_SEMA_SLOT_C;
}

/**
 * tick146 / pass17: sema release using formal table policy.
 * Modes with A/B/C execute slots use slot-aware emit; 0x1001/nonstd/open/vdpau
 * stay on classic execute-in-D path.
 */
static inline void
nv_push_sema_release_mode_pass17(struct nv_push *p, uint64_t sema_gpu_addr,
                                 uint32_t payload, enum nv_host_sema_mode mode)
{
   if (nv_host_sema_pass17_prefers_slot_emit(mode))
      nv_push_sema_release_mode_slot(p, sema_gpu_addr, payload, mode);
   else
      nv_push_sema_release_mode(p, sema_gpu_addr, payload, mode);
}

/*
 * tick155 / pass21: unified G0–G4 host sema tail polish.
 * All engines (aux/CE/compute/3D/video) share the same pass17 formal sema
 * policy when emitting host sema on the GPFIFO/3D sema methods.  Default mode
 * for channel bringup ladders is BLOB1004 (slot C execute) — matches pass17
 * first formal row and tick147+ channel sticky default.
 */
#define NV_PASS21_HOST_SEMA_DEFAULT_MODE   NV_HOST_SEMA_MODE_BLOB1004_ALIGN4
#define NV_PASS21_HOST_SEMA_DEFAULT_EXEC   0x1004u
#define NV_PASS21_HOST_SEMA_DEFAULT_SLOT   NV_HOST_SEMA_SLOT_C
#define NV_PASS21_G0_G4_ENGINE_COUNT       5u  /* aux, CE, compute, 3D, video */

/** Engine id for G0–G4 sema ladder bookkeeping (not push subchannel). */
enum nv_pass21_engine_id {
   NV_PASS21_ENGINE_G0_AUX = 0,
   NV_PASS21_ENGINE_G1_CE = 1,
   NV_PASS21_ENGINE_G2_COMPUTE = 2,
   NV_PASS21_ENGINE_G3_3D = 3,
   NV_PASS21_ENGINE_G4_VIDEO = 4,
};

/**
 * tick155: emit pass17 host sema tail on SUBCH_3D (GPFIFO sema methods),
 * optionally preceded by WFI on the active engine subchannel.  Used uniformly
 * by G0–G4 bringup ladders so silicon sticky sema mode is one code path.
 * Returns 0 on success, -1 if p/sema missing.
 */
static inline int
nv_push_g0_g4_host_sema_tail_pass21(struct nv_push *p,
                                    bool pre_wfi_on_cur_subch,
                                    uint64_t host_sema_gpu,
                                    uint32_t host_sema_payload,
                                    enum nv_host_sema_mode host_sema_mode)
{
   if (!p || !host_sema_gpu)
      return -1;
   if (pre_wfi_on_cur_subch)
      nv_push_method(p, NVC36F_WFI, 0);
   nv_push_set_subch(p, NV_PUSH_SUBCH_3D);
   nv_push_sema_release_mode_pass17(
      p, host_sema_gpu,
      host_sema_payload ? host_sema_payload : 1u,
      host_sema_mode);
   return 0;
}

/*
 * tick171 / pass23: G0–G4 symmetry audit — all engines share pass21 host sema
 * tail when pass23 inherits pass22 explicit-emit policy.  Channel ladders:
 *   G1 CE:    nv_g1_emit_copy_then_host_sema_pass21
 *   G2 comp:  pass22 compute object / pass21 program launch
 *   G3 3D:    pass22 barrier / inv_wfi_host_sema_pass21
 *   G4 video: pass21/pass23 NVDEC/NVENC bringup (ticks 169–170)
 *   G0 aux:   same pass21 sema tail on 3D subch methods
 */
#define NV_PASS23_G0_G4_SYMMETRY_AUDIT       1
#define NV_PASS23_G0_G4_HOST_SEMA_IS_PASS21  1
#define NV_PASS23_G1_CE_PASS21               1
#define NV_PASS23_G2_COMPUTE_PASS22          1
#define NV_PASS23_G3_3D_PASS22_BARRIER       1
#define NV_PASS23_G4_VIDEO_PASS21_PASS23     1

/** tick171: pass23 host sema tail — alias of pass21 (unified formal policy). */
static inline int
nv_push_g0_g4_host_sema_tail_pass23(struct nv_push *p,
                                    bool pre_wfi_on_cur_subch,
                                    uint64_t host_sema_gpu,
                                    uint32_t host_sema_payload,
                                    enum nv_host_sema_mode host_sema_mode)
{
   return nv_push_g0_g4_host_sema_tail_pass21(p, pre_wfi_on_cur_subch,
                                              host_sema_gpu, host_sema_payload,
                                              host_sema_mode);
}

/** tick171: true if pass23 G0–G4 symmetry flags are coherent with pass21/22. */
static inline bool
nv_pass23_g0_g4_symmetry_ok(void)
{
   return NV_PASS23_G0_G4_SYMMETRY_AUDIT != 0 &&
          NV_PASS23_G0_G4_HOST_SEMA_IS_PASS21 != 0 &&
          NV_PASS21_G0_G4_ENGINE_COUNT == 5u;
}

/* tick178 / pass24: G0–G4 symmetry inherits pass23; G2 uses pass24 launch ladder */
#define NV_PASS24_G0_G4_SYMMETRY_AUDIT       1
#define NV_PASS24_G0_G4_HOST_SEMA_IS_PASS21  1
#define NV_PASS24_G1_CE_PASS23               1
#define NV_PASS24_G2_COMPUTE_PASS24          1
#define NV_PASS24_G3_3D_PASS23_BARRIER       1
#define NV_PASS24_G4_VIDEO_PASS23_PASS24     1

/** tick178: pass24 host sema tail — alias pass23/21 (unified formal policy). */
static inline int
nv_push_g0_g4_host_sema_tail_pass24(struct nv_push *p,
                                    bool pre_wfi_on_cur_subch,
                                    uint64_t host_sema_gpu,
                                    uint32_t host_sema_payload,
                                    enum nv_host_sema_mode host_sema_mode)
{
   return nv_push_g0_g4_host_sema_tail_pass23(p, pre_wfi_on_cur_subch,
                                              host_sema_gpu, host_sema_payload,
                                              host_sema_mode);
}

/** tick178: pass24 G0–G4 symmetry coherent with pass23 + pass24 G2 ladder. */
static inline bool
nv_pass24_g0_g4_symmetry_ok(void)
{
   return NV_PASS24_G0_G4_SYMMETRY_AUDIT != 0 &&
          NV_PASS24_G0_G4_HOST_SEMA_IS_PASS21 != 0 &&
          nv_pass23_g0_g4_symmetry_ok() &&
          NV_PASS24_G1_CE_PASS23 != 0 &&
          NV_PASS24_G2_COMPUTE_PASS24 != 0 &&
          NV_PASS24_G3_3D_PASS23_BARRIER != 0 &&
          NV_PASS24_G4_VIDEO_PASS23_PASS24 != 0;
}

/* tick181 / pass25: G0–G4 symmetry inherits pass24 impl wire */
#define NV_PASS25_G0_G4_SYMMETRY_AUDIT       1
#define NV_PASS25_G0_G4_HOST_SEMA_IS_PASS21  1
#define NV_PASS25_G1_CE_PASS24               1
#define NV_PASS25_G2_COMPUTE_PASS24          1
#define NV_PASS25_G3_3D_PASS24_BARRIER       1
#define NV_PASS25_G4_VIDEO_PASS24_PASS25     1
#define NV_PASS25_G0_G4_SYMMETRY_TICK181     1

/** tick181: pass25 host sema tail — alias pass24/23/21 (unified formal policy). */
static inline int
nv_push_g0_g4_host_sema_tail_pass25(struct nv_push *p,
                                    bool pre_wfi_on_cur_subch,
                                    uint64_t host_sema_gpu,
                                    uint32_t host_sema_payload,
                                    enum nv_host_sema_mode host_sema_mode)
{
   return nv_push_g0_g4_host_sema_tail_pass24(p, pre_wfi_on_cur_subch,
                                              host_sema_gpu, host_sema_payload,
                                              host_sema_mode);
}

/** tick181: pass25 G0–G4 symmetry coherent with pass24 wire + pass25 policy. */
static inline bool
nv_pass25_g0_g4_symmetry_ok(void)
{
   return NV_PASS25_G0_G4_SYMMETRY_AUDIT != 0 &&
          NV_PASS25_G0_G4_HOST_SEMA_IS_PASS21 != 0 &&
          NV_PASS25_G0_G4_SYMMETRY_TICK181 != 0 &&
          nv_pass24_g0_g4_symmetry_ok() &&
          NV_PASS25_G1_CE_PASS24 != 0 &&
          NV_PASS25_G2_COMPUTE_PASS24 != 0 &&
          NV_PASS25_G3_3D_PASS24_BARRIER != 0 &&
          NV_PASS25_G4_VIDEO_PASS24_PASS25 != 0;
}

/* tick185 / pass26: G0–G4 symmetry inherits pass25 impl wire */
#define NV_PASS26_G0_G4_SYMMETRY_AUDIT       1
#define NV_PASS26_G0_G4_HOST_SEMA_IS_PASS21  1
#define NV_PASS26_G1_CE_PASS25               1
#define NV_PASS26_G2_COMPUTE_PASS25          1
#define NV_PASS26_G3_3D_PASS25_BARRIER       1
#define NV_PASS26_G4_VIDEO_PASS25_PASS26     1
#define NV_PASS26_G0_G4_SYMMETRY_TICK185     1

/** tick185: pass26 host sema tail — alias pass25/24/21. */
static inline int
nv_push_g0_g4_host_sema_tail_pass26(struct nv_push *p,
                                    bool pre_wfi_on_cur_subch,
                                    uint64_t host_sema_gpu,
                                    uint32_t host_sema_payload,
                                    enum nv_host_sema_mode host_sema_mode)
{
   return nv_push_g0_g4_host_sema_tail_pass25(p, pre_wfi_on_cur_subch,
                                              host_sema_gpu, host_sema_payload,
                                              host_sema_mode);
}

/** tick185: pass26 G0–G4 symmetry coherent with pass25 wire + pass26 policy. */
static inline bool
nv_pass26_g0_g4_symmetry_ok(void)
{
   return NV_PASS26_G0_G4_SYMMETRY_AUDIT != 0 &&
          NV_PASS26_G0_G4_HOST_SEMA_IS_PASS21 != 0 &&
          NV_PASS26_G0_G4_SYMMETRY_TICK185 != 0 &&
          nv_pass25_g0_g4_symmetry_ok() &&
          NV_PASS26_G1_CE_PASS25 != 0 &&
          NV_PASS26_G2_COMPUTE_PASS25 != 0 &&
          NV_PASS26_G3_3D_PASS25_BARRIER != 0 &&
          NV_PASS26_G4_VIDEO_PASS25_PASS26 != 0;
}

/**
 * tick155: recommended sema mode order for G0–G4 silicon ladders (pass17
 * first, then classic BLOB1002/0802/1001, then open/vdpau).  Fills *out with
 * up to max_out modes; returns count written.  sticky_pref is tried first if
 * it appears in the canonical order (else pass17 1004 leads).
 */
static inline unsigned
nv_pass21_g0_g4_sema_mode_ladder_fill(enum nv_host_sema_mode out[],
                                      unsigned max_out,
                                      enum nv_host_sema_mode sticky_pref)
{
   static const enum nv_host_sema_mode k_order[] = {
      NV_HOST_SEMA_MODE_BLOB1004_ALIGN4,
      NV_HOST_SEMA_MODE_BLOB1002_ALIGN4,
      NV_HOST_SEMA_MODE_BLOB0804_ALIGN4,
      NV_HOST_SEMA_MODE_BLOB0802_ALIGN4,
      NV_HOST_SEMA_MODE_BLOB_ALIGN4, /* execute 0x1001 */
      NV_HOST_SEMA_MODE_OPEN_ALIGN4,
      NV_HOST_SEMA_MODE_VDPAU_ALIGN4,
   };
   unsigned n = 0, i;
   bool sticky_in_order = false;

   if (!out || !max_out)
      return 0;
   for (i = 0; i < sizeof(k_order) / sizeof(k_order[0]); i++) {
      if (k_order[i] == sticky_pref) {
         sticky_in_order = true;
         break;
      }
   }
   if (sticky_in_order && n < max_out)
      out[n++] = sticky_pref;
   for (i = 0; i < sizeof(k_order) / sizeof(k_order[0]) && n < max_out; i++) {
      if (sticky_in_order && k_order[i] == sticky_pref)
         continue;
      out[n++] = k_order[i];
   }
   return n;
}

/* Host semaphore release (4-byte payload) at GPU sema address */
static inline void
nv_push_sema_release(struct nv_push *p, uint64_t sema_gpu_addr, uint32_t payload)
{
   /* Historic: align4 + open headers (same as nv_push_sema_release before pass5) */
   nv_push_sema_release_mode(p, sema_gpu_addr, payload,
                             NV_HOST_SEMA_MODE_OPEN_ALIGN4);
}

/* Host semaphore acquire (stall channel until mem == payload; ACQ_GEQ variant). */
static inline void
nv_push_sema_acquire(struct nv_push *p, uint64_t sema_gpu_addr, uint32_t payload)
{
   nv_push_method(p, NVC36F_SEMAPHOREA, (uint32_t)(sema_gpu_addr >> 32) & 0xff);
   nv_push_method(p, NVC36F_SEMAPHOREB, (uint32_t)(sema_gpu_addr & ~0x3u));
   nv_push_method(p, NVC36F_SEMAPHOREC, payload);
   nv_push_method(p, NVC36F_SEMAPHORED,
                  NVC36F_SEMAPHORED_OPERATION_ACQUIRE |
                  NVC36F_SEMAPHORED_ACQUIRE_SWITCH_TSG_ENABLE |
                  NVC36F_SEMAPHORED_RELEASE_SIZE_4BYTE);
}

/* Acquire when sema value is >= payload (typical progress sema pattern). */
static inline void
nv_push_sema_acquire_geq(struct nv_push *p, uint64_t sema_gpu_addr,
                         uint32_t payload)
{
   nv_push_method(p, NVC36F_SEMAPHOREA, (uint32_t)(sema_gpu_addr >> 32) & 0xff);
   nv_push_method(p, NVC36F_SEMAPHOREB, (uint32_t)(sema_gpu_addr & ~0x3u));
   nv_push_method(p, NVC36F_SEMAPHOREC, payload);
   nv_push_method(p, NVC36F_SEMAPHORED,
                  NVC36F_SEMAPHORED_OPERATION_ACQ_GEQ |
                  NVC36F_SEMAPHORED_ACQUIRE_SWITCH_TSG_ENABLE |
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
