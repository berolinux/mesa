/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Maxwell/Pascal/Turing+ MME (method macro engine) instruction scaffolding.
 *
 * MME executes short programs that emit 3D/CE method writes, driven by
 * LOAD_MME_INSTRUCTION_RAM + CALL_MME_MACRO / CALL_MME_DATA on the 3D class
 * (NVC597_* mirrors NV902D/NV9097 in open-gpu-doc).
 *
 * Full ISA is not fully public; encodings here are derived incrementally from:
 *   - open-gpu-doc class method offsets (LOAD_MME_*, CALL_MME_*, SET_MME_*)
 *   - open-gpu-doc PRI MME ESR / exception bits (illegal opcode, missing data)
 *   - observed proprietary compiler / channel-init patterns (END, method emit)
 *
 * Until binary RE completes real indirect-draw loops, macros either:
 *   (a) END immediately (safe no-op), or
 *   (b) emit a fixed method sequence via host-side "shadow program" that
 *       duplicates what the macro *should* emit (path C' correctness).
 *
 * Pass5/8/9 (610.43.02 glcore) shows real CALL_MME traffic at method offs
 * 0x3800 (hdr 0x20010e00 ×4), 0x3998, 0x39e0 hottest — confirms HW uses MME.
 * Pass9: 24× 0x3800 imm in glcore rodata; cuda has 2110× (compute noise).
 * ISA still unvalidated → keep END stubs; host path A/B/C' for indirect draws.
 * Tick88: emit uses full POINTER+RAM+START_ADDRESS+CALL sequence (clc597.h).
 *
 * Host-side shadow programs live in nv_3d_methods.h / callers; this header
 * owns instruction constants and multi-insn program tables.
 */

#ifndef NV_MME_H
#define NV_MME_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Instruction word conventions (32-bit MME insn, Maxwell+ family) ---
 *
 * Public research + class dumps agree on at least:
 *   END           — terminate macro (opcode low bits = 1 in common dumps)
 * Method-emit and ALU/branch encodings vary by generation; we treat unknown
 * multi-insn programs conservatively (END-only or host shadow).
 */
#define NV_MME_INSN_END              0x00000001u

/* Placeholder class bits for future RE (documented so stubs are not magic).
 * Do not ship non-END programs in production paths until validated. */
#define NV_MME_INSN_NOP              0x00000000u
#define NV_MME_INSN_MERGE_CLASS      0x08000000u /* tentative method-merge slot */
#define NV_MME_INSN_STATE_LOAD_CLASS 0x10000000u /* tentative state/mem load */
#define NV_MME_INSN_BRANCH_CLASS     0x20000000u /* tentative branch */
#define NV_MME_INSN_ALU_CLASS        0x40000000u /* tentative ALU/add */

/* Macro slot indices (must match nv_3d_methods.h NV_MME_MACRO_*) */
#define NV_MME_SLOT_DRAW_INDIRECT            0
#define NV_MME_SLOT_DRAW_INDEXED_INDIRECT    1
#define NV_MME_SLOT_COUNT                    2

/*
 * Pass5 glcore method-off frequency (inc1 s0 CALL_MME-ish headers):
 *   0x3800 base (hdr 0x20010e00) — 5 hits
 *   0x3998 (hdr 0x20010e66) — 21 hits
 *   0x39e0 (hdr 0x20010e78) — 45 hits (hottest observed)
 * Macro index i is at byte off 0x3800 + i*8 (CALL_MME_MACRO(i)).
 * Hottest slot index ≈ (0x39e0 - 0x3800) / 8 = 28 — not our indirect slots 0/1.
 */
#define NV_MME_PASS5_HOT_METHOD_OFF          0x39e0u
#define NV_MME_PASS5_HOT_MACRO_INDEX         28u

/* Instruction RAM layout: each macro gets a dedicated region */
#define NV_MME_RAM_SLOT_STRIDE               16  /* dwords per macro region */
#define NV_MME_MAX_INSNS_PER_MACRO           16

struct nv_mme_program {
   uint32_t slot;                         /* macro index / start-address slot */
   uint32_t ram_offset;                   /* instruction RAM dword index */
   uint32_t insns[NV_MME_MAX_INSNS_PER_MACRO];
   uint32_t insn_count;
   bool     is_stub_end_only;             /* true = not yet real microcode */
};

static inline void
nv_mme_program_init_end_only(struct nv_mme_program *prog, uint32_t slot,
                             uint32_t ram_offset)
{
   if (!prog)
      return;
   memset(prog, 0, sizeof(*prog));
   prog->slot = slot;
   prog->ram_offset = ram_offset;
   prog->insns[0] = NV_MME_INSN_END;
   prog->insn_count = 1;
   prog->is_stub_end_only = true;
}

/**
 * Build indirect-draw macro program.
 *
 * Intended behaviour (when fully RE'd):
 *   - Read CALL_MME_DATA: data0 = draw_count | (stride << 16)
 *   - Read CALL_MME_DATA: indirect addr lo/hi (or use SET_MME_MEM_ADDRESS)
 *   - Loop draw_count times:
 *       load VkDrawIndirectCommand / VkDrawIndexedIndirectCommand from mem
 *       emit SET_VERTEX_ID_BASE / instanced begin-end methods
 *   - END
 *
 * Current: END-only stub.  Host path C' emits equivalent methods from CPU
 * shadow/map (path A/B) while this macro only primes MME state.
 */
/**
 * tick98: multi-insn scaffold (still stub — not validated ISA).
 * Layout: NOP padding then END, so LOAD_MME_INSTRUCTION_RAM writes >1 dword
 * and exercises POINTER/RAM/START/CALL with a non-trivial RAM span.  Host
 * indirect path remains authoritative until binary RE fills real opcodes.
 */
static inline void
nv_mme_build_draw_indirect_program(struct nv_mme_program *prog, uint32_t slot,
                                   uint32_t ram_offset, bool indexed)
{
   if (!prog)
      return;
   memset(prog, 0, sizeof(*prog));
   prog->slot = slot;
   prog->ram_offset = ram_offset;
   /* Reserved class bits document intended roles; all low bits still END-safe. */
   prog->insns[0] = NV_MME_INSN_NOP | (indexed ? NV_MME_INSN_STATE_LOAD_CLASS : 0);
   prog->insns[1] = NV_MME_INSN_NOP | NV_MME_INSN_ALU_CLASS;
   prog->insns[2] = NV_MME_INSN_NOP | NV_MME_INSN_BRANCH_CLASS;
   prog->insns[3] = NV_MME_INSN_NOP | NV_MME_INSN_MERGE_CLASS;
   prog->insns[4] = NV_MME_INSN_END;
   prog->insn_count = 5;
   prog->is_stub_end_only = true; /* not real microcode; do not enable in prod paths */
   (void)indexed;
}

/**
 * Build both indirect macros into caller-provided array (size >= SLOT_COUNT).
 * Returns number of programs filled (2).
 */
static inline unsigned
nv_mme_build_indirect_draw_programs(struct nv_mme_program progs[NV_MME_SLOT_COUNT])
{
   if (!progs)
      return 0;
   nv_mme_build_draw_indirect_program(&progs[NV_MME_SLOT_DRAW_INDIRECT],
                                      NV_MME_SLOT_DRAW_INDIRECT,
                                      0, false);
   nv_mme_build_draw_indirect_program(&progs[NV_MME_SLOT_DRAW_INDEXED_INDIRECT],
                                      NV_MME_SLOT_DRAW_INDEXED_INDIRECT,
                                      NV_MME_RAM_SLOT_STRIDE, true);
   return NV_MME_SLOT_COUNT;
}

/** True if any program is still END-only (caller must use path A/B for draws). */
static inline bool
nv_mme_programs_are_stubs(const struct nv_mme_program progs[NV_MME_SLOT_COUNT])
{
   unsigned i;
   if (!progs)
      return true;
   for (i = 0; i < NV_MME_SLOT_COUNT; i++) {
      if (progs[i].is_stub_end_only || progs[i].insn_count == 0)
         return true;
   }
   return false;
}

/**
 * Pass8: emit LOAD_MME_INSTRUCTION_RAM for one program then CALL_MME_MACRO(slot).
 * Requires 3D class already SET_OBJECT on push. Safe with END-only stubs (no-op macro).
 * Method offsets: CALL_MME_MACRO(i) @ 0x3800 + i*8 (NVC597 family).
 */
#ifndef NVC597_LOAD_MME_INSTRUCTION_RAM
/* Fallback if class header not included; matches Maxwell+ inc method pattern */
#define NV_MME_METHOD_LOAD_INSN_RAM   0x0450u
#define NV_MME_METHOD_CALL_MACRO_BASE 0x3800u
#else
#define NV_MME_METHOD_LOAD_INSN_RAM   NVC597_LOAD_MME_INSTRUCTION_RAM
#define NV_MME_METHOD_CALL_MACRO_BASE NVC597_CALL_MME_MACRO(0)
#endif

/**
 * Upload program via canonical NVC597 sequence (pass9/tick88):
 *   LOAD_MME_INSTRUCTION_RAM_POINTER(ram_offset)
 *   LOAD_MME_INSTRUCTION_RAM(insn) × N
 *   LOAD_MME_START_ADDRESS_RAM_POINTER(slot)
 *   LOAD_MME_START_ADDRESS_RAM(ram_offset)
 *   CALL_MME_MACRO(slot) with data0=0
 * END-only programs are safe no-ops on HW (macro terminates immediately).
 */
static inline void
nv_mme_emit_load_and_call_end_stub(struct nv_push *p,
                                   const struct nv_mme_program *prog)
{
   uint32_t i;
   uint32_t call_off;
   uint32_t ram_ptr;

   if (!p || !prog || prog->insn_count == 0)
      return;

   ram_ptr = prog->ram_offset;
#ifdef NVC597_LOAD_MME_INSTRUCTION_RAM_POINTER
   nv_push_method(p, NVC597_LOAD_MME_INSTRUCTION_RAM_POINTER, ram_ptr);
   for (i = 0; i < prog->insn_count && i < NV_MME_MAX_INSNS_PER_MACRO; i++)
      nv_push_method(p, NVC597_LOAD_MME_INSTRUCTION_RAM, prog->insns[i]);
   nv_push_method(p, NVC597_LOAD_MME_START_ADDRESS_RAM_POINTER, prog->slot);
   nv_push_method(p, NVC597_LOAD_MME_START_ADDRESS_RAM, ram_ptr);
   nv_push_method(p, NVC597_CALL_MME_MACRO(prog->slot), 0);
#else
   /* Fallback when class header not pulled in (still sets pointer/data/call) */
   nv_push_method(p, 0x0114u, ram_ptr);
   for (i = 0; i < prog->insn_count && i < NV_MME_MAX_INSNS_PER_MACRO; i++)
      nv_push_method(p, NV_MME_METHOD_LOAD_INSN_RAM, prog->insns[i]);
   nv_push_method(p, 0x011cu, prog->slot);
   nv_push_method(p, 0x0120u, ram_ptr);
   call_off = NV_MME_METHOD_CALL_MACRO_BASE + prog->slot * 8u;
   nv_push_method(p, call_off, 0);
#endif
   (void)call_off;
}

/** Prime both indirect slots with END-only macros (state init; draws still path A/B). */
static inline void
nv_mme_emit_prime_indirect_stubs(struct nv_push *p)
{
   struct nv_mme_program progs[NV_MME_SLOT_COUNT];
   unsigned i;

   if (!p)
      return;
   nv_mme_build_indirect_draw_programs(progs);
   for (i = 0; i < NV_MME_SLOT_COUNT; i++)
      nv_mme_emit_load_and_call_end_stub(p, &progs[i]);
}

#ifdef __cplusplus
}
#endif

#endif /* NV_MME_H */
