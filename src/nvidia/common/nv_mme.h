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
 * pass20 (tick153): gpucomp has 100× 0x3884 RAM_DATA imm (compiler/SASS path)
 * and 19× 0x34a8 / 35× 0x39e0 — confirms MME method vocabulary in compiler
 * libs, not ordered microcode programs in static binary.  glcore RAM_DATA imm
 * sits ~2064B from nearest func prologue (runtime fill, not rodata template).
 * Indirect path C remains gated (is_stub_end_only) until live MME ISA capture.
 *
 * Host-side shadow programs live in nv_3d_methods.h / callers; this header
 * owns instruction constants and multi-insn program tables.
 */

/* pass20/tick153: provisional insn classes refined with gpucomp density notes */
#define NV_MME_PASS20_RAM_DATA_METHOD_OFF        0x3884u
#define NV_MME_PASS20_RAM_ADDR_METHOD_OFF        0x385cu
#define NV_MME_PASS20_CFG_END_METHOD_OFF         0x3880u
/* pass20: indirect scaffold method offs (draw vs indexed) — host shadow uses
 * real 3D methods; these are only MME pseudo-emit targets in scaffolds */
#define NV_MME_PASS20_SCAFFOLD_DRAW_METHOD       0x1610u
#define NV_MME_PASS20_SCAFFOLD_DRAW_IDX_METHOD   0x1620u
#define NV_MME_PASS20_SCAFFOLD_MERGE_DRAW        0x1618u
#define NV_MME_PASS20_SCAFFOLD_MERGE_DRAW_IDX    0x1630u

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
/* SET_MME_SHADOW_SCRATCH(i) at 0x3400 + i*4 (open-gpu-doc compute/3d classes) */
#define NV_MME_METHOD_SET_MME_SHADOW_SCRATCH_I(i) \
   (NV_MME_METHOD_SET_MME_SHADOW_SCRATCH + (((uint32_t)(i) & 0xffu) * 4u))

/*
 * pass14 glcore literal 0x2001xxxx census hotspot: method idx 0xd2a → moff 0x34a8.
 * 0x34a8 - 0x3400 = 0xa8; 0xa8/4 = 42 → SET_MME_SHADOW_SCRATCH(42).
 * tick140: treat as MME scratch init (zero/clear) during channel prime, not a
 * mystery 3D method.  Call_MME_MACRO base 0x3800 is separate (i*8 stride).
 */
#define NV_MME_PASS14_LIT_METHOD_OFF         0x34a8u
#define NV_MME_PASS14_LIT_SCRATCH_INDEX      42u
#define NV_MME_PASS14_LIT_METHOD_IDX         0x0d2au /* 0x34a8 >> 2 */

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
 * pass16 RE: 0x39e0 is MME-adjacent post-config (follows 0x3800 in glcore imm
 * ngrams), not SET_MME_SHADOW_SCRATCH(i) — off >= 0x3800 is outside the
 * 0x3400..0x37fc scratch window.  Emit as a direct 3D method imm=0 after
 * shadow-scratch init / RAM prime to probe channel completeness on silicon;
 * never required for smoke success and never substitutes for RAM_DATA.
 */
#define NV_MME_PASS16_POST_CONFIG_METHOD_OFF NV_MME_PASS5_HOT_METHOD_OFF /* 0x39e0 */
#define NV_MME_PASS16_NEAR_MME_METHOD_OFF    0x3998u /* pass15 gap; lower priority */

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
 * tick105/112/135: clear/blit helper macro scaffold (slot 3).
 * pass12 RE: glcore programs MME via LOAD_MME_INSTRUCTION_RAM (0x114/0x118)
 * not SET_MME_INSTRUCTION_RAM_PTR (0x3880) in the hot path we observed; host
 * still uses LOAD_* helpers.  Pseudo insn sequence documents intended G3
 * clear path; is_stub_end_only remains true until opcode table is proven.
 * Host nv_3d_emit_g3_* remains authoritative.
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
   /* Pseudo: state load → colour clear values ×4 → CLEAR_SURFACE → sema band → END */
   prog->insns[0] = NV_MME_INSN_OP(NV_MME_OP_STATE_LOAD) |
                    NV_MME_INSN_IMM16(0) | NV_MME_INSN_STATE_LOAD_CLASS;
   prog->insns[1] = nv_mme_insn_emit_method(0x0d80u /* SET_COLOR_CLEAR_VALUE(0) */,
                                            0);
   prog->insns[2] = nv_mme_insn_emit_method(0x0d84u /* SET_COLOR_CLEAR_VALUE(1) */,
                                            0);
   prog->insns[3] = nv_mme_insn_emit_method(0x0d88u /* SET_COLOR_CLEAR_VALUE(2) */,
                                            0);
   prog->insns[4] = nv_mme_insn_emit_method(0x0d8cu /* SET_COLOR_CLEAR_VALUE(3) */,
                                            0);
   prog->insns[5] = nv_mme_insn_emit_method(0x19d0u /* CLEAR_SURFACE */, 1);
   prog->insns[6] = NV_MME_INSN_OP(NV_MME_OP_MERGE_METHOD) |
                    NV_MME_INSN_IMM16(0x1b00u /* report sema band approx */) |
                    NV_MME_INSN_MERGE_CLASS;
   prog->insns[7] = NV_MME_INSN_OP(NV_MME_OP_END);
   prog->insn_count = 8;
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
   /* 1: loop body — method emit band (draw vs indexed draw; pass20 names) */
   prog->insns[1] = nv_mme_insn_emit_method(
      indexed ? NV_MME_PASS20_SCAFFOLD_DRAW_IDX_METHOD
              : NV_MME_PASS20_SCAFFOLD_DRAW_METHOD, 0);
   /* 2: merge/call helper for secondary method burst */
   prog->insns[2] = NV_MME_INSN_OP(NV_MME_OP_MERGE_METHOD) |
                    NV_MME_INSN_IMM16(indexed ? NV_MME_PASS20_SCAFFOLD_MERGE_DRAW_IDX
                                             : NV_MME_PASS20_SCAFFOLD_MERGE_DRAW) |
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
 * tick119: upload program to instruction RAM + bind start address, no CALL.
 * Lets channel-init stage macros without executing stub bodies (avoids tentative
 * method-emit opcodes until ISA is silicon-validated).
 */
static inline void
nv_mme_emit_upload_only(struct nv_push *p, const struct nv_mme_program *prog)
{
   uint32_t i;
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
#else
   nv_push_method(p, 0x0114u, ram_ptr);
   for (i = 0; i < prog->insn_count && i < NV_MME_MAX_INSNS_PER_MACRO; i++)
      nv_push_method(p, NV_MME_METHOD_LOAD_INSN_RAM, prog->insns[i]);
   nv_push_method(p, 0x011cu, prog->slot);
   nv_push_method(p, 0x0120u, ram_ptr);
#endif
}

/** tick119: upload indirect + extended slots without CALL (prime RAM only). */
static inline void
nv_mme_emit_upload_full_table_only(struct nv_push *p)
{
   struct nv_mme_program progs[NV_MME_SLOT_EXTENDED_COUNT];
   struct nv_mme_program pass5;
   unsigned i, n;

   if (!p)
      return;
   n = nv_mme_build_extended_program_table(progs, &pass5);
   for (i = 0; i < n && i < NV_MME_SLOT_EXTENDED_COUNT; i++)
      nv_mme_emit_upload_only(p, &progs[i]);
   nv_mme_emit_upload_only(p, &pass5);
}

/**
 * tick119: CALL_MME_MACRO(slot) with optional data0 — use only after upload_only
 * or when program is END-only (safe no-op).  Still not production indirect.
 */
static inline void
nv_mme_emit_call_macro_only(struct nv_push *p, uint32_t slot, uint32_t data0)
{
   if (!p)
      return;
#ifdef NVC597_CALL_MME_MACRO
   nv_push_method(p, NVC597_CALL_MME_MACRO(slot), data0);
#else
   nv_push_method(p, NV_MME_METHOD_CALL_MACRO_BASE + (slot & 0x7fu) * 8u, data0);
#endif
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

/**
 * tick128: upload indirect-draw loop scaffolds only (no CALL_MME).
 * Returns true if programs are still stubs (always until real MME ISA).
 */
static inline bool
nv_mme_emit_upload_indirect_stubs_only(struct nv_push *p)
{
   struct nv_mme_program progs[NV_MME_SLOT_COUNT];
   unsigned i;

   if (!p)
      return true;
   nv_mme_build_indirect_draw_programs(progs);
   for (i = 0; i < NV_MME_SLOT_COUNT; i++)
      nv_mme_emit_upload_only(p, &progs[i]);
   return nv_mme_programs_are_stubs(progs);
}

/**
 * tick140 / pass14: SET_MME_SHADOW_SCRATCH(i) = value.
 * open-gpu-doc: (0x3400 + i*4).  Used to clear/init MME shadow state before
 * RAM upload; pass14 lit 0x34a8 is scratch index 42.
 */
static inline void
nv_mme_emit_set_shadow_scratch(struct nv_push *p, unsigned scratch_i,
                               uint32_t value)
{
   if (!p)
      return;
   nv_push_set_subch(p, NV_PUSH_SUBCH_3D);
   nv_push_method(p, NV_MME_METHOD_SET_MME_SHADOW_SCRATCH_I(scratch_i), value);
}

/** tick140: zero pass14 hotspot scratch (index 42 / method 0x34a8). */
static inline void
nv_mme_emit_pass14_hot_scratch_zero(struct nv_push *p)
{
   nv_mme_emit_set_shadow_scratch(p, NV_MME_PASS14_LIT_SCRATCH_INDEX, 0);
}

/**
 * tick140/143: zero first N scratch slots + pass14/15 hot indices.
 * pass15 unknown lit methods 0x3998 / 0x39e0 sit in MME/AA region — also zero
 * scratch indices derived from those method offs (relative to 0x3400 base)
 * when they fall in the shadow-scratch method space.
 */
#define NV_MME_PASS15_LIT_METHOD_OFF_3998    0x3998u
#define NV_MME_PASS15_LIT_METHOD_OFF_39E0    0x39e0u
/* If interpreted as SET_MME_SHADOW_SCRATCH(i): i = (off - 0x3400) / 4 */
#define NV_MME_PASS15_SCRATCH_IDX_FROM_OFF(off) \
   ((((uint32_t)(off) >= 0x3400u) && ((uint32_t)(off) < 0x3800u)) \
    ? (((uint32_t)(off) - 0x3400u) / 4u) : 0xffffffffu)

static inline void
nv_mme_emit_shadow_scratch_init_range(struct nv_push *p, unsigned count)
{
   unsigned i, n = count ? count : 16u;
   uint32_t idx_3998, idx_39e0;
   if (!p)
      return;
   if (n > 64u)
      n = 64u;
   for (i = 0; i < n; i++)
      nv_mme_emit_set_shadow_scratch(p, i, 0);
   if (NV_MME_PASS14_LIT_SCRATCH_INDEX >= n)
      nv_mme_emit_pass14_hot_scratch_zero(p);
   /* tick143: pass15 gap methods 0x3998/0x39e0 — zero as scratch if in range */
   idx_3998 = NV_MME_PASS15_SCRATCH_IDX_FROM_OFF(NV_MME_PASS15_LIT_METHOD_OFF_3998);
   idx_39e0 = NV_MME_PASS15_SCRATCH_IDX_FROM_OFF(NV_MME_PASS15_LIT_METHOD_OFF_39E0);
   if (idx_3998 != 0xffffffffu && idx_3998 >= n)
      nv_mme_emit_set_shadow_scratch(p, idx_3998, 0);
   if (idx_39e0 != 0xffffffffu && idx_39e0 >= n && idx_39e0 != idx_3998)
      nv_mme_emit_set_shadow_scratch(p, idx_39e0, 0);
}

/**
 * tick145 / pass16: emit method 0x39e0 with imm 0 after MME scratch/RAM setup.
 * Documents RE-observed post-MME config probe; safe no-op imm on unknown state.
 */
static inline void
nv_mme_emit_pass16_post_config_probe(struct nv_push *p)
{
   if (!p)
      return;
   nv_push_set_subch(p, NV_PUSH_SUBCH_3D);
   nv_push_method(p, NV_MME_PASS16_POST_CONFIG_METHOD_OFF, 0);
}

/**
 * tick145: full MME channel prime — scratch init, RAM/table upload, pass16
 * post-config probe (0x39e0).  Still no CALL_MME while stubs remain.
 */
static inline bool
nv_mme_emit_channel_prime_upload_pass16(struct nv_push *p)
{
   if (!p)
      return true;
   nv_mme_emit_shadow_scratch_init_range(p, 16);
   nv_mme_emit_upload_full_table_only(p);
   nv_mme_emit_pass16_post_config_probe(p);
   return true;
}

/**
 * tick146 / pass17: channel prime — pass16 path (scratch + RAM + 0x39e0 probe).
 * pass17 RE reconfirmed RAM_DATA imm=0 in primary libs; no new static upload
 * sequence; keep pass16 as production prime until live trace.
 */
static inline bool
nv_mme_emit_channel_prime_upload_pass17(struct nv_push *p)
{
   return nv_mme_emit_channel_prime_upload_pass16(p);
}

/*
 * tick156 / pass21: RAM_DATA / RAM_ADDR method scaffold (NVC597-class offsets).
 * pass20 RE: gpucomp 100× 0x3884 imm, glcore ~2064B from func prologue (runtime
 * fill).  Static binaries lack ordered microcode; these helpers emit the method
 * vocabulary for channel prime / silicon capture without enabling path C CALL
 * on stubs.  insn_count=0 is a no-op (addr only optional); non-zero writes
 * successive RAM_DATA dwords starting at ram_addr.
 */
#define NV_MME_PASS21_RAM_DATA_METHOD_OFF   NV_MME_PASS20_RAM_DATA_METHOD_OFF
#define NV_MME_PASS21_RAM_ADDR_METHOD_OFF   NV_MME_PASS20_RAM_ADDR_METHOD_OFF
#define NV_MME_PASS21_CFG_END_METHOD_OFF    NV_MME_PASS20_CFG_END_METHOD_OFF
#define NV_MME_PASS21_MAX_RAM_DATA_DWORDS   64u
#define NV_MME_PASS21_GPUCOMP_RAM_DATA_IMM_CAPPED  100u /* pass20 measurement */

/**
 * tick156: set MME instruction RAM address pointer (method 0x385c).
 * ram_addr is HW RAM word index / offset as programmed by the binary driver
 * (typically start of macro slot region; 0 = base).
 */
static inline void
nv_mme_emit_ram_addr_pass21(struct nv_push *p, uint32_t ram_addr)
{
   if (!p)
      return;
   nv_push_set_subch(p, NV_PUSH_SUBCH_3D);
   nv_push_method(p, NV_MME_PASS21_RAM_ADDR_METHOD_OFF, ram_addr);
}

/**
 * tick156: stream MME instruction words via method 0x3884 (RAM_DATA).
 * Does not CALL_MME; safe for capture/scaffold.  Caps at
 * NV_MME_PASS21_MAX_RAM_DATA_DWORDS.
 */
static inline unsigned
nv_mme_emit_ram_data_stream_pass21(struct nv_push *p,
                                   const uint32_t *insns,
                                   unsigned insn_count)
{
   unsigned i, n;

   if (!p || !insns || !insn_count)
      return 0;
   n = insn_count;
   if (n > NV_MME_PASS21_MAX_RAM_DATA_DWORDS)
      n = NV_MME_PASS21_MAX_RAM_DATA_DWORDS;
   nv_push_set_subch(p, NV_PUSH_SUBCH_3D);
   for (i = 0; i < n; i++)
      nv_push_method(p, NV_MME_PASS21_RAM_DATA_METHOD_OFF, insns[i]);
   return n;
}

/**
 * tick156: RAM_ADDR + RAM_DATA stream for one program (upload scaffold only).
 * Returns dwords written via RAM_DATA (0 if no insns).
 */
static inline unsigned
nv_mme_emit_ram_upload_scaffold_pass21(struct nv_push *p,
                                       uint32_t ram_addr,
                                       const uint32_t *insns,
                                       unsigned insn_count)
{
   if (!p)
      return 0;
   nv_mme_emit_ram_addr_pass21(p, ram_addr);
   return nv_mme_emit_ram_data_stream_pass21(p, insns, insn_count);
}

/**
 * tick156: upload stub END programs through RAM_DATA path (vocab probe).
 * Writes END (0x1) for indirect slots 0/1 at ram_addr and ram_addr+1.
 * Still does not CALL — path C remains gated.
 */
static inline unsigned
nv_mme_emit_ram_data_stub_end_probe_pass21(struct nv_push *p,
                                           uint32_t ram_addr_base)
{
   uint32_t end_insn = NV_MME_INSN_END;
   unsigned n = 0;

   if (!p)
      return 0;
   n += nv_mme_emit_ram_upload_scaffold_pass21(p, ram_addr_base, &end_insn, 1);
   n += nv_mme_emit_ram_upload_scaffold_pass21(p, ram_addr_base + 1u,
                                               &end_insn, 1);
   return n;
}

/**
 * tick156 / pass21: channel prime = pass17 path + optional RAM_DATA stub probe.
 * probe_ram_data=true emits END via 0x3884 for silicon capture; false = pass17
 * only.  Always returns true (stubs still active; path C not enabled).
 */
static inline bool
nv_mme_emit_channel_prime_upload_pass21(struct nv_push *p, bool probe_ram_data)
{
   if (!p)
      return true;
   nv_mme_emit_channel_prime_upload_pass17(p);
   if (probe_ram_data)
      (void)nv_mme_emit_ram_data_stub_end_probe_pass21(p, 0);
   return true;
}

/** tick128: channel prime — full MME table upload without CALL (stubs only). */
static inline bool
nv_mme_emit_channel_prime_upload_only(struct nv_push *p)
{
   if (!p)
      return true;
   /* tick143/145/146/156: scratch + RAM; pass16/17 0x39e0; pass21 no probe by default */
   return nv_mme_emit_channel_prime_upload_pass21(p, false);
}

/**
 * tick137: path C gate — CALL_MME_MACRO only when program is *not* stub_end_only.
 * Returns true if CALL was emitted (real microcode path); false if skipped
 * (caller must use host path A/B/C' method emission instead).
 * Until RE validates MME opcodes, all builders set is_stub_end_only=true so
 * this always returns false — production draws/clears stay on host methods.
 */
static inline bool
nv_mme_emit_call_macro_if_ready(struct nv_push *p,
                                const struct nv_mme_program *prog,
                                uint32_t data0)
{
   if (!p || !prog || prog->insn_count == 0)
      return false;
   if (prog->is_stub_end_only)
      return false;
   nv_mme_emit_call_macro_only(p, prog->slot, data0);
   return true;
}

/**
 * tick152 / pass20: path C readiness probe — true only when indirect (or
 * clear/init) programs are non-stub.  Always false until RE fills real ISA;
 * documents gate for silicon try without enabling unsafe CALL on stubs.
 */
static inline bool
nv_mme_path_c_indirect_ready(void)
{
   struct nv_mme_program progs[NV_MME_SLOT_COUNT];
   nv_mme_build_indirect_draw_programs(progs);
   return !nv_mme_programs_are_stubs(progs);
}

/**
 * tick152: path C indirect try — CALL indirect slot only if non-stub.
 * data0 = draw_count | (stride << 16) (same as pass5 / mme_kick convention).
 * Returns true if CALL emitted; false = caller must use host path A/B/C'.
 */
static inline bool
nv_mme_emit_path_c_indirect_if_ready(struct nv_push *p, bool indexed,
                                     uint32_t draw_count, uint32_t stride_bytes)
{
   struct nv_mme_program prog;
   uint32_t data0;

   if (!p || !draw_count)
      return false;
   if (indexed)
      nv_mme_build_draw_indirect_loop_scaffold(
         &prog, NV_MME_SLOT_DRAW_INDEXED_INDIRECT, 16, true);
   else
      nv_mme_build_draw_indirect_loop_scaffold(
         &prog, NV_MME_SLOT_DRAW_INDIRECT, 0, false);
   data0 = (draw_count & 0xffffu) | ((stride_bytes & 0xffffu) << 16);
   return nv_mme_emit_call_macro_if_ready(p, &prog, data0);
}

/**
 * tick137: after RAM prime, optionally CALL clear/channel-init slots only if
 * non-stub.  Returns count of CALLs emitted (0 while stubs remain).
 * Host clear/draw remains authoritative when 0.
 */
static inline unsigned
nv_mme_emit_path_c_calls_if_ready(struct nv_push *p)
{
   struct nv_mme_program clr, init;
   unsigned n = 0;

   if (!p)
      return 0;
   nv_mme_build_clear_helper_program_stub(&clr, 48);
   nv_mme_build_channel_init_program_stub(&init, 64);
   if (nv_mme_emit_call_macro_if_ready(p, &clr, 0))
      n++;
   if (nv_mme_emit_call_macro_if_ready(p, &init, 0))
      n++;
   return n;
}

/**
 * tick152: path C full try — clear/init CALLs + indirect CALLs if ready.
 * Returns total CALLs (0 while all stubs — normal production state).
 */
static inline unsigned
nv_mme_emit_path_c_all_if_ready(struct nv_push *p, bool try_indirect,
                                bool indexed, uint32_t draw_count,
                                uint32_t stride_bytes)
{
   unsigned n = nv_mme_emit_path_c_calls_if_ready(p);
   if (try_indirect &&
       nv_mme_emit_path_c_indirect_if_ready(p, indexed, draw_count,
                                            stride_bytes))
      n++;
   return n;
}

/**
 * tick137: upload full MME table then attempt path C CALLs (no-op while stubs).
 * Returns true if any CALL was emitted (path C active); false = host-only.
 */
static inline bool
nv_mme_emit_upload_and_path_c_try(struct nv_push *p)
{
   if (!p)
      return false;
   nv_mme_emit_upload_full_table_only(p);
   return nv_mme_emit_path_c_calls_if_ready(p) > 0;
}

/**
 * tick152: upload indirect stubs + try path C indirect (false until ISA ready).
 */
static inline bool
nv_mme_emit_upload_and_path_c_indirect_try(struct nv_push *p, bool indexed,
                                           uint32_t draw_count,
                                           uint32_t stride_bytes)
{
   if (!p)
      return false;
   nv_mme_emit_upload_indirect_stubs_only(p);
   return nv_mme_emit_path_c_indirect_if_ready(p, indexed, draw_count,
                                               stride_bytes);
}

#ifdef __cplusplus
}
#endif

#endif /* NV_MME_H */
