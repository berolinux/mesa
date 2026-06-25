# HW Model Pass 24 — Scaffold (inherits pass23 deep; tick176)

**Date:** 2026-06-25  
**Driver:** NVIDIA-Linux-x86_64-610.43.02  
**Status:** Scaffold only — full multi-hour `re_pass24/` disasm not completed this tick.

## Inherited from pass23 (authoritative)

| Topic | Pass24 position |
|-------|-----------------|
| Explicit emit policy | Required (`nv_pass24_explicit_emit_required` → pass23/22) |
| Ordered templates | Absent statically (pass23 reconfirm) |
| Host sema formal | 11/11 glcore/egl/vksc; pass23 bases |
| MME path C | Still gated |
| G0–G4 symmetry | Reconfirmed pass23 |
| Proximity ≠ template | pass23 graphs |

## Mesa wire-up (tick176)

| Deliverable | Symbol / helper |
|-------------|-----------------|
| Pass24 RE constants | `NV_PASS24_RE_*` in `nv_qmd.h` / `nv_video_methods.h` |
| Policy gate | `nv_pass23_24_emit_policy_gate`, `nv_pass24_policy_ok` |
| G2 launch pass24 | `nv_pass24_compute_object_emit_launch` |
| NIR pass24 ladder | `nv_pass24_nir_depth_ladder_build_all` (hand SPH, 5 kinds) |
| Selftest | -848..-855 |

## Planned pass24 RE focus (pending)

1. gpucomp-only MME/caller CFG depth (`re_pass24/`)
2. Silicon probe hooks if `/dev/nvidia*` available
3. Deeper x86 caller idioms at RAM_DATA/PCAS sites in gpucomp

## Still blocked

1. Full pass24 multi-lib/gpucomp disasm
2. Real NIR AST isel
3. Live MME ISA / path C unlock
4. Hardware G0–G4 sema completion

*Artifacts: this scaffold; pass23 deep model remains primary for measurements.*
