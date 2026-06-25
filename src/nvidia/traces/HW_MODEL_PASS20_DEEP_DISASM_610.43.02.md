# HW Model Pass 20 — x86 Emitter CFG, INLINE→PCAS Distances, Path C Gate
# (NVIDIA-Linux-x86_64-610.43.02)

**Date:** 2026-06-25
**Scope:** pass19 deferred x86 CFG: imm sites annotated with nearest function
prologues (endbr64/push rbp) and LEA proximity; INLINE_QMD_A→SEND_PCAS_A median
imm distances; sema~WFI/PCAS cooc within 256B (cuda/vksc). Mesa tick152 wires
constants + path C indirect gate (refuse stub CALL) + indirect ladder pass20.

## Key measurements

| Lib | elapsed | cfg_samples | inline→PCAS median | sema_cooc_256 |
|-----|---------|-------------|--------------------|---------------|
| glcore | 176.97s | 1 | 428 | {} |
| eglcore | 146.22s | 0 | None | {} |
| cuda | 285.68s | 21 | 2204 | {'sema~0x0110': 2, 'sema~0x02b4': 1} |
| vksc | 76.62s | 8 | 2724 | {'sema~0x0110': 4, 'sema~0x02b4': 1} |
| gpucomp | 547.42s | 0 | null | {} |

## Mesa impact (tick152)

- `NV_PASS20_INLINE_TO_PCAS_MEDIAN_{GLCORE,CUDA,VKSC}` in `nv_qmd.h`
- `nv_pass20_inline_pcas_span_ok` / `nv_compute_emit_inline_qmd_launch_pass20` (optional WFI tail)
- `nv_mme_path_c_indirect_ready` / `nv_mme_emit_path_c_indirect_if_ready` — CALL only when non-stub
- `nv_3d_try_draw_indirect_path_c` gated on path_c_indirect_ready (was kick-if-uploaded only)
- `nv_3d_emit_draw_indirect_ladder_pass20` + Gallium `draw_vbo` indirect uses ladder
- Selftest -646..-655

## Still live-trace / RE

- RAM_DATA 0x3884 runtime-only (glcore func dist 2064 to imm site)
- MME indirect ISA still stub_end_only — path C returns false; host A/B authoritative
- sema formal 11/11 glcore reconfirmed

*Artifacts: `/tmp/nvidia-reveng-pp-v2/re_pass20/`*

## tick153 addendum

- gpucomp pass20 complete: RAM_DATA imm capped 100, MME_SC42=19, MME_POST=35
- G4 video pass17 host sema tails (`nv_g4_emit_nvdec/nvenc_bringup_pass17`)
- MME pass20 scaffold method constants; indirect path C still stub-gated
- No /dev/nvidia* this host — silicon sema unproven
