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

/* tick105: additional macro slots observed / reserved (not indirect path) */
#define NV_MME_SLOT_CHANNEL_INIT_SCRATCH     2  /* tentative channel-init macro */
#define NV_MME_SLOT_CLEAR_HELPER             3  /* tentative clear/blit helper */
#define NV_MME_SLOT_EXTENDED_COUNT           4

/* Method offsets for CALL_MME_DATA / SET_MME_SHADOW (NVC597 family subset) */
#define NV_MME_METHOD_CALL_DATA_BASE         0x3880u  /* CALL_MME_DATA(0) approx */
#define NV_MME_METHOD_SET_MME_SHADOW_SCRATCH 0x3400u  /* SET_MME_SHADOW_SCRATCH(i) base */

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

/*
 * tick100: provisional MME instruction bitfields (Maxwell/Pascal+ class family).
 * Not validated against silicon; used only to document RE progress.  Production
 * paths must keep is_stub_end_only=true until opcode table is proven.
 *
 * Common pattern in dumps: low bits select op; high bits carry immediates /
 * register indices / method merge targets.  END is reliably 0x1 in observed
 * terminator slots.
 */
#define NV_MME_OP_MASK                       0x0000000fu
#define NV_MME_OP_END                        0x1u
#define NV_MME_OP_NOP                        0x0u
#define NV_MME_OP_MERGE_METHOD               0x2u /* tentative */
#define NV_MME_OP_ALU                        0x4u /* tentative */
#define NV_MME_OP_BRANCH                     0x5u /* tentative */
#define NV_MME_OP_STATE_LOAD                 0x6u /* tentative */
#define NV_MME_OP_EMIT_METHOD                0x8u /* tentative method emit */

#define NV_MME_INSN_OP(op)                   ((uint32_t)(op) & NV_MME_OP_MASK)
#define NV_MME_INSN_IMM16(v)                 (((uint32_t)(v) & 0xffffu) << 8)
#define NV_MME_INSN_REG(a, b) \
   ((((uint32_t)(a) & 0x1fu) << 24) | (((uint32_t)(b) & 0x1fu) << 16))

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
   /*
    * tick100: structured stub using provisional op/imm/reg fields (still not
    * silicon-valid ISA).  Host indirect path remains authoritative.
    */
   prog->insns[0] = NV_MME_INSN_OP(indexed ? NV_MME_OP_STATE_LOAD : NV_MME_OP_NOP) |
                    NV_MME_INSN_IMM16(0) | NV_MME_INSN_STATE_LOAD_CLASS;
   prog->insns[1] = NV_MME_INSN_OP(NV_MME_OP_ALU) | NV_MME_INSN_REG(0, 1) |
                    NV_MME_INSN_ALU_CLASS;
   prog->insns[2] = NV_MME_INSN_OP(NV_MME_OP_BRANCH) | NV_MME_INSN_IMM16(4) |
                    NV_MME_INSN_BRANCH_CLASS;
   prog->insns[3] = NV_MME_INSN_OP(NV_MME_OP_MERGE_METHOD) |
                    NV_MME_INSN_IMM16(0x3800) | NV_MME_INSN_MERGE_CLASS;
   prog->insns[4] = NV_MME_INSN_OP(NV_MME_OP_END); /* == NV_MME_INSN_END when op=1 */
   prog->insn_count = 5;
   prog->is_stub_end_only = true; /* not real microcode; do not enable in prod paths */
   (void)indexed;
}

/**
 * tick102: program scaffold for pass5 hottest observed macro index (slot 28 /
 * method 0x39e0).  Still END-terminated stub — documents RAM region layout for
 * future RE of proprietary channel-init macros, not production indirect path.
 */
static inline void
nv_mme_build_pass5_hot_program_stub(struct nv_mme_program *prog,
                                    uint32_t ram_offset)
{
   if (!prog)
      return;
   memset(prog, 0, sizeof(*prog));
   prog->slot = NV_MME_PASS5_HOT_MACRO_INDEX;
   prog->ram_offset = ram_offset;
   /* Method-merge toward hottest CALL_MME_MACRO offset, then END */
   prog->insns[0] = NV_MME_INSN_OP(NV_MME_OP_MERGE_METHOD) |
                    NV_MME_INSN_IMM16(NV_MME_PASS5_HOT_METHOD_OFF) |
                    NV_MME_INSN_MERGE_CLASS;
   prog->insns[1] = NV_MME_INSN_OP(NV_MME_OP_STATE_LOAD) |
                    NV_MME_INSN_IMM16(0) | NV_MME_INSN_STATE_LOAD_CLASS;
   prog->insns[2] = NV_MME_INSN_OP(NV_MME_OP_END);
   prog->insn_count = 3;
   prog->is_stub_end_only = true;
}

/**
 * tick105: encode a single "emit method" pseudo-instruction (RE scaffold).
 * Intended shape once validated: low nibble EMIT_METHOD, imm16 = method offset
 * (byte offset into 3D class method space), high reg fields = imm/data source.
 * Still stub — do not ship in production paths without silicon validation.
 */
static inline uint32_t
nv_mme_insn_emit_method(uint16_t method_off, uint8_t reg_src)
{
   return NV_MME_INSN_OP(NV_MME_OP_EMIT_METHOD) |
          NV_MME_INSN_IMM16(method_off) |
          NV_MME_INSN_REG(reg_src, 0) |
          NV_MME_INSN_MERGE_CLASS;
}

/**
 * tick105: channel-init macro scaffold (slot 2) — mirrors proprietary pattern
 * of a short macro that may prime shadow scratch / method state then END.
 * Host path remains authoritative; this only exercises LOAD/START/CALL plumbing.
 */
static inline void
nv_mme_build_channel_init_program_stub(struct nv_mme_program *prog,
                                       uint32_t ram_offset)
{
   if (!prog)
      return;
   memset(prog, 0, sizeof(*prog));
   prog->slot = NV_MME_SLOT_CHANNEL_INIT_SCRATCH;
   prog->ram_offset = ram_offset;
   /* Pseudo: shadow/scratch touch, tentative method emit, branch skip, END */
   prog->insns[0] = NV_MME_INSN_OP(NV_MME_OP_STATE_LOAD) |
                    NV_MME_INSN_IMM16(0) | NV_MME_INSN_STATE_LOAD_CLASS;
   prog->insns[1] = nv_mme_insn_emit_method(0x0200u /* SET_OBJECT-ish */, 0);
   prog->insns[2] = NV_MME_INSN_OP(NV_MME_OP_ALU) | NV_MME_INSN_REG(0, 0) |
                    NV_MME_INSN_ALU_CLASS;
   prog->insns[3] = NV_MME_INSN_OP(NV_MME_OP_BRANCH) | NV_MME_INSN_IMM16(5) |
                    NV_MME_INSN_BRANCH_CLASS;
   prog->insns[4] = NV_MME_INSN_OP(NV_MME_OP_END);
   prog->insn_count = 5;
   prog->is_stub_end_only = true;
}

/**
 * tick105/112: clear/blit helper macro scaffold (slot 3).
 * Intended (unvalidated) sequence mirrors host G3: set colour clear values,
 * CLEAR_SURFACE (0x19d0), optional report sema — all pseudo-ops until RE.
 * Host nv_3d_emit_g3_* remains authoritative; this only primes MME RAM/CALL.
 */
static inline void
nv_mme_build_clear_helper_program_stub(struct nv_mme_program *prog,
                                       uint32_t ram_offset)
{
   if (!prog)
      return;
   memset(prog, 0, sizeof(*prog));
   prog->slot = NV_MME_SLOT_CLEAR_HELPER;
   prog->ram_offset = ram_offset;
   /* Pseudo: load clear state, emit SET_COLOR_CLEAR_VALUE band, CLEAR_SURFACE, END */
   prog->insns[0] = NV_MME_INSN_OP(NV_MME_OP_STATE_LOAD) |
                    NV_MME_INSN_IMM16(0) | NV_MME_INSN_STATE_LOAD_CLASS;
   prog->insns[1] = nv_mme_insn_emit_method(0x0d80u /* SET_COLOR_CLEAR_VALUE(0) */,
                                            0);
   prog->insns[2] = nv_mme_insn_emit_method(0x19d0u /* CLEAR_SURFACE */, 1);
   prog->insns[3] = NV_MME_INSN_OP(NV_MME_OP_MERGE_METHOD) |
                    NV_MME_INSN_IMM16(0x1b00u /* report sema band approx */) |
                    NV_MME_INSN_MERGE_CLASS;
   prog->insns[4] = NV_MME_INSN_OP(NV_MME_OP_END);
   prog->insn_count = 5;
   prog->is_stub_end_only = true;
}

/**
 * tick112: full table prime helper — builds all extended slots with distinct
 * RAM offsets so channel-init can LOAD each region without overlap.
 * Returns number of programs written (0..NV_MME_SLOT_EXTENDED_COUNT).
 */
static inline unsigned
nv_mme_prime_all_extended_stubs(struct nv_mme_program *progs,
                                unsigned max_progs, uint32_t ram_base)
{
   unsigned n = 0;

   if (!progs || max_progs < 1)
      return 0;
   if (n < max_progs) {
      nv_mme_build_draw_indirect_program(&progs[n], NV_MME_SLOT_DRAW_INDIRECT,
                                         ram_base + n * NV_MME_RAM_SLOT_STRIDE,
                                         false);
      n++;
   }
   if (n < max_progs) {
      nv_mme_build_draw_indirect_program(&progs[n],
                                         NV_MME_SLOT_DRAW_INDEXED_INDIRECT,
                                         ram_base + n * NV_MME_RAM_SLOT_STRIDE,
                                         true);
      n++;
   }
   if (n < max_progs) {
      nv_mme_build_channel_init_program_stub(
         &progs[n], ram_base + n * NV_MME_RAM_SLOT_STRIDE);
      n++;
   }
   if (n < max_progs) {
      nv_mme_build_clear_helper_program_stub(
         &progs[n], ram_base + n * NV_MME_RAM_SLOT_STRIDE);
      n++;
   }
   return n;
}

/**
 * tick106: pseudo-instruction for "branch if reg_src != 0, imm16 = target insn idx".
 * Documents intended control flow for indirect draw_count loops; not silicon-valid.
 */
static inline uint32_t
nv_mme_insn_branch_nz(uint16_t target_insn_idx, uint8_t reg_src)
{
   return NV_MME_INSN_OP(NV_MME_OP_BRANCH) |
          NV_MME_INSN_IMM16(target_insn_idx) |
          NV_MME_INSN_REG(reg_src, 0) |
          NV_MME_INSN_BRANCH_CLASS;
}

/**
 * tick106: pseudo-instruction for ALU inc/dec on reg_dst (imm16 = ±delta).
 * Loop counters in indirect macros would use this once ISA is proven.
 */
static inline uint32_t
nv_mme_insn_alu_add_imm(uint8_t reg_dst, int16_t imm)
{
   return NV_MME_INSN_OP(NV_MME_OP_ALU) |
          NV_MME_INSN_IMM16((uint16_t)imm) |
          NV_MME_INSN_REG(reg_dst, reg_dst) |
          NV_MME_INSN_ALU_CLASS;
}

/**
 * tick106: richer indirect-draw scaffold — documents intended loop shape:
 *   R0 = draw_count from CALL_MME_DATA; loop: load indirect cmd, emit methods, dec, branch.
 * Still is_stub_end_only; host path A/B/C' remains authoritative.
 */
static inline void
nv_mme_build_draw_indirect_loop_scaffold(struct nv_mme_program *prog,
                                         uint32_t slot, uint32_t ram_offset,
                                         bool indexed)
{
   if (!prog)
      return;
   memset(prog, 0, sizeof(*prog));
   prog->slot = slot;
   prog->ram_offset = ram_offset;
   /* 0: state load (indirect buffer / CALL_MME_DATA draw_count into R0) */
   prog->insns[0] = NV_MME_INSN_OP(NV_MME_OP_STATE_LOAD) |
                    NV_MME_INSN_IMM16(0) | NV_MME_INSN_STATE_LOAD_CLASS;
   /* 1: loop body — method emit band (draw vs indexed draw) */
   prog->insns[1] = nv_mme_insn_emit_method(indexed ? 0x1620u : 0x1610u, 0);
   /* 2: merge/call helper for secondary method burst */
   prog->insns[2] = NV_MME_INSN_OP(NV_MME_OP_MERGE_METHOD) |
                    NV_MME_INSN_IMM16(indexed ? 0x1630u : 0x1618u) |
                    NV_MME_INSN_MERGE_CLASS;
   /* 3: decrement loop counter R0 */
   prog->insns[3] = nv_mme_insn_alu_add_imm(0, -1);
   /* 4: branch back to insn 1 while R0 != 0 */
   prog->insns[4] = nv_mme_insn_branch_nz(1, 0);
   /* 5: END */
   prog->insns[5] = NV_MME_INSN_OP(NV_MME_OP_END);
   prog->insn_count = 6;
   prog->is_stub_end_only = true;
   (void)indexed;
}

/**
 * tick106: fill EXTENDED_COUNT macro table (indirect + channel-init + clear + pass5 hot).
 * ram regions stride by NV_MME_RAM_SLOT_STRIDE; pass5 hot uses slot 28 / separate region.
 * Returns number of programs written into progs[] (caller provides >= EXTENDED_COUNT+1
 * if including pass5 hot at progs[EXTENDED_COUNT]).
 */
static inline unsigned
nv_mme_build_extended_program_table(struct nv_mme_program progs[NV_MME_SLOT_EXTENDED_COUNT],
                                    struct nv_mme_program *pass5_hot_out)
{
   uint32_t ram = 0;
   if (!progs)
      return 0;
   nv_mme_build_draw_indirect_loop_scaffold(&progs[NV_MME_SLOT_DRAW_INDIRECT],
                                            NV_MME_SLOT_DRAW_INDIRECT, ram, false);
   ram += NV_MME_RAM_SLOT_STRIDE;
   nv_mme_build_draw_indirect_loop_scaffold(&progs[NV_MME_SLOT_DRAW_INDEXED_INDIRECT],
                                            NV_MME_SLOT_DRAW_INDEXED_INDIRECT, ram, true);
   ram += NV_MME_RAM_SLOT_STRIDE;
   nv_mme_build_channel_init_program_stub(&progs[NV_MME_SLOT_CHANNEL_INIT_SCRATCH], ram);
   ram += NV_MME_RAM_SLOT_STRIDE;
   nv_mme_build_clear_helper_program_stub(&progs[NV_MME_SLOT_CLEAR_HELPER], ram);
   if (pass5_hot_out) {
      ram += NV_MME_RAM_SLOT_STRIDE;
      nv_mme_build_pass5_hot_program_stub(pass5_hot_out, ram);
   }
   return NV_MME_SLOT_EXTENDED_COUNT;
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
   nv_mme_build_draw_indirect_loop_scaffold(&progs[NV_MME_SLOT_DRAW_INDIRECT],
                                            NV_MME_SLOT_DRAW_INDIRECT,
                                            0, false);
   nv_mme_build_draw_indirect_loop_scaffold(&progs[NV_MME_SLOT_DRAW_INDEXED_INDIRECT],
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

/**
 * tick105: upload/call one program, optionally emit CALL_MME_DATA(0)=data0 first
 * (indirect draw data0 = draw_count | (stride<<16) pattern from pass5 traces).
 */
static inline void
nv_mme_emit_load_call_with_data0(struct nv_push *p,
                                 const struct nv_mme_program *prog,
                                 uint32_t data0)
{
   if (!p || !prog)
      return;
   if (data0) {
#ifdef NVC597_CALL_MME_DATA
      nv_push_method(p, NVC597_CALL_MME_DATA(0), data0);
#else
      nv_push_method(p, NV_MME_METHOD_CALL_DATA_BASE, data0);
#endif
   }
   nv_mme_emit_load_and_call_end_stub(p, prog);
}

/** Prime channel-init + clear helper stubs (in addition to indirect slots). */
static inline void
nv_mme_emit_prime_extended_stubs(struct nv_push *p)
{
   struct nv_mme_program init_prog, clear_prog;
   if (!p)
      return;
   nv_mme_build_channel_init_program_stub(&init_prog,
                                          2u * NV_MME_RAM_SLOT_STRIDE);
   nv_mme_build_clear_helper_program_stub(&clear_prog,
                                          3u * NV_MME_RAM_SLOT_STRIDE);
   nv_mme_emit_load_and_call_end_stub(p, &init_prog);
   nv_mme_emit_load_and_call_end_stub(p, &clear_prog);
}

/**
 * tick109: upload full extended macro table (indirect loop + init/clear + pass5 hot).
 * Still END-terminated stubs — exercises LOAD/START/CALL plumbing for G3 channel-init
 * experiments without enabling production indirect draws (host path remains authoritative).
 */
static inline void
nv_mme_emit_prime_full_table_stubs(struct nv_push *p)
{
   struct nv_mme_program progs[NV_MME_SLOT_EXTENDED_COUNT];
   struct nv_mme_program pass5;
   unsigned i, n;

   if (!p)
      return;
   n = nv_mme_build_extended_program_table(progs, &pass5);
   for (i = 0; i < n && i < NV_MME_SLOT_EXTENDED_COUNT; i++)
      nv_mme_emit_load_and_call_end_stub(p, &progs[i]);
   nv_mme_emit_load_and_call_end_stub(p, &pass5);
}

/**
 * tick113: clear-helper macro only (slot 3) — lighter G3 channel-init path than
 * full table prime.  Still is_stub_end_only; host G3 clear/draw remains authoritative.
 */
static inline void
nv_mme_emit_prime_clear_helper_only(struct nv_push *p, uint32_t ram_offset)
{
   struct nv_mme_program clear_prog;

   if (!p)
      return;
   nv_mme_build_clear_helper_program_stub(&clear_prog, ram_offset);
   nv_mme_emit_load_and_call_end_stub(p, &clear_prog);
}

/**
 * tick113: pseudo-instruction for method emit targeting CLEAR_SURFACE (0x19d0) +
 * report sema band — documents intended clear macro body once ISA is proven.
 */
static inline void
nv_mme_build_clear_surface_loop_scaffold(struct nv_mme_program *prog,
                                         uint32_t ram_offset)
{
   if (!prog)
      return;
   memset(prog, 0, sizeof(*prog));
   prog->slot = NV_MME_SLOT_CLEAR_HELPER;
   prog->ram_offset = ram_offset;
   /* 0: load clear/color/depth values from CALL_MME_DATA / shadow */
   prog->insns[0] = NV_MME_INSN_OP(NV_MME_OP_STATE_LOAD) |
                    NV_MME_INSN_IMM16(0) | NV_MME_INSN_STATE_LOAD_CLASS;
   /* 1-2: SET_COLOR_CLEAR_VALUE(0..1) pseudo emits */
   prog->insns[1] = nv_mme_insn_emit_method(0x0d80u, 0);
   prog->insns[2] = nv_mme_insn_emit_method(0x0d84u, 1);
   /* 3: CLEAR_SURFACE */
   prog->insns[3] = nv_mme_insn_emit_method(0x19d0u, 2);
   /* 4: optional second clear (Z/S) */
   prog->insns[4] = nv_mme_insn_emit_method(0x19d0u, 3);
   /* 5: report sema release band */
   prog->insns[5] = NV_MME_INSN_OP(NV_MME_OP_MERGE_METHOD) |
                    NV_MME_INSN_IMM16(0x1b00u) | NV_MME_INSN_MERGE_CLASS;
   /* 6: END */
   prog->insns[6] = NV_MME_INSN_OP(NV_MME_OP_END);
   prog->insn_count = 7;
   prog->is_stub_end_only = true;
}

/**
 * tick114: draw-indirect loop scaffold using richer ALU/branch pseudo-ops plus
 * tentative method emits for BEGIN/END (still is_stub_end_only).
 * Host path C' remains authoritative until binary RE validates opcodes.
 */
static inline void
nv_mme_build_draw_indirect_with_clear_scaffold(struct nv_mme_program *prog,
                                               uint32_t slot,
                                               uint32_t ram_offset,
                                               bool indexed)
{
   if (!prog)
      return;
   memset(prog, 0, sizeof(*prog));
   prog->slot = slot;
   prog->ram_offset = ram_offset;
   prog->insns[0] = NV_MME_INSN_OP(NV_MME_OP_STATE_LOAD) |
                    NV_MME_INSN_IMM16(0) | NV_MME_INSN_STATE_LOAD_CLASS;
   /* optional pre-draw clear via CLEAR_SURFACE pseudo */
   prog->insns[1] = nv_mme_insn_emit_method(0x19d0u, 0);
   prog->insns[2] = nv_mme_insn_emit_method(indexed ? 0x1620u : 0x1610u, 1);
   prog->insns[3] = nv_mme_insn_alu_add_imm(0, -1);
   prog->insns[4] = nv_mme_insn_branch_nz(2, 0);
   prog->insns[5] = NV_MME_INSN_OP(NV_MME_OP_END);
   prog->insn_count = 6;
   prog->is_stub_end_only = true;
   (void)indexed;
}

#ifdef __cplusplus
}
#endif

#endif /* NV_MME_H */
