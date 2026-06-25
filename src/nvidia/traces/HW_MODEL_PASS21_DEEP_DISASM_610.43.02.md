# HW Model Pass 21 — G0–G4 Unified Host Sema, MME RAM_DATA Scaffold, Compute Program Depth
# (NVIDIA-Linux-x86_64-610.43.02 synthesis; ticks 155–158)

**Date:** 2026-06-25  
**Scope:** Implement pass17/19/20 RE into a single engine-symmetric host sema policy,
MME RAM_DATA method vocabulary (not live ISA), and compute program→QMD→launch path.
Pass21 multi-hour static RE directory: `/tmp/nvidia-reveng-pp-v2/re_pass21/` (scaffold;
builds on pass18–20 artifacts).

## RE synthesis (what is authoritative)

| Topic | Pass21 position |
|-------|-----------------|
| Host sema formal table | glcore `0x11e30c0` 11 rows; egl/vksc byte-identical (pass17–19) |
| Default host sema mode | `NV_HOST_SEMA_MODE_BLOB1004_ALIGN4` → execute `0x1004` on SEMAPHOREC (slot C) |
| Engine symmetry | G0–G4 all use `nv_push_g0_g4_host_sema_tail_pass21` for host tail |
| Sema mode silicon ladder | `nv_pass21_g0_g4_sema_mode_ladder_fill` (1004→1002→0804→0802→0x1001→open→vdpau) |
| MME RAM_DATA (`0x3884`) | Runtime-only in glcore (~2064B from func prologue); gpucomp 100× imm (pass20) |
| MME RAM_ADDR (`0x385c`) | Method vocab present; no ordered static microcode |
| MME path C indirect | Still `stub_end_only`; CALL gated; host A/B/C' authoritative |
| INLINE→PCAS | pass20 medians glcore 428 / cuda 2204 / vksc 2724 (imm distance, not order proof) |
| Pushbuffer headers | Runtime-constructed (`0x2000/0x2001` exact = 0 in static libs) |
| NVC597 inv/CB/report | Class-correct methods; imm-family `0x0b00`/`0x1280` not interchangeable |

## Mesa wire-up (ticks 155–158)

| Tick | Deliverable |
|------|-------------|
| 155 | pass21 sema tail/ladder; SPH s2r+store_imm; G2 program launch pass21 |
| 156 | MME RAM_ADDR/RAM_DATA scaffold; channel prime optional probe; G2 launch sema tail |
| 157 | G1 copy+host sema; G3 inv/report+host; G4 NVDEC/NVENC bringup pass21 symmetry |
| 158 | Channel G2/G3 bringup pass21 retry; Gallium `memory_barrier` pass21 barrier |

## Channel bringup ladder (silicon order)

**G2** (`nv_channel_g2_bringup_slice_submit`):
1. QMD sema-only pass17 (no host sema in prep)
2. pass17 smoke slice fallback
3. pass19 QMD sema + host sema tail
4. **pass21** program launch (`nv_compute_emit_g2_program_launch_pass21`) with pass21 host sema

**G3** (`nv_channel_g3_bringup_slice_submit`):
1. pass18 bringup + 3D report sema
2. **pass21** inv ladder + host sema tail on same sema VA (if report wait fails)

**G4** (channel video submit paths): pass17/21 NVDEC/NVENC bringup with pass21 host sema tail helpers.

## Gallium

- `nvgpu_memory_barrier`: `PIPE_BARRIER_ALL` and selective shader/tex barriers use
  `nv_3d_emit_g3_barrier_*_pass21` (NVC597 inv + WFI); trivial flags keep legacy helper.

## Still live-trace / RE blockers

1. Live RAM_DATA/PCAS/INLINE method **order** (not imm presence)
2. Real MME ISA (non-END programs) for path C indirect
3. Silicon sema completion G1–G4 (no `/dev/nvidia*` on autoloop host)
4. Full NIR → SASS beyond pass21 s2r+store_imm / exit smoke

## Constants (mesa)

```c
NV_PASS21_HOST_SEMA_DEFAULT_MODE   // BLOB1004_ALIGN4
NV_PASS21_HOST_SEMA_DEFAULT_EXEC   // 0x1004
NV_PASS21_HOST_SEMA_DEFAULT_SLOT   // SLOT_C
NV_MME_PASS21_RAM_DATA_METHOD_OFF  // 0x3884
NV_MME_PASS21_RAM_ADDR_METHOD_OFF  // 0x385c
NV_PASS21_RE_G0_G4_UNIFIED_TAIL    // 1
NV_PASS21_RE_PATH_C_STILL_GATED    // 1
```

## Pass21 completion (tick160)

- Implementation wire-up complete through tick159 (G0–G4 sema symmetry, MME RAM_DATA
  scaffold + env probe, Gallium/Vulkan barriers, channel G1/G2/G3 ladders).
- Compute NIR depth: `nv_pass21_compute_object` + shader kind ladder
  (exit → store_imm → s2r multi-SR → s2r+store_imm); not full NIR lower yet.
- Static RE in `re_pass21/` remains scaffold dirs; measurements carried from pass18–20.
- No `/dev/nvidia*` on autoloop host: silicon sema/MME/PCAS order still unproven.

**Status:** pass21 implementation phase **COMPLETE** for mesa userspace scaffolding;
next RE/HW work is live trace + real MME ISA + full NIR→SASS.

*Artifacts: `/tmp/nvidia-reveng-pp-v2/re_pass{18,19,20,21}/`; mesa traces pass18–21.*
