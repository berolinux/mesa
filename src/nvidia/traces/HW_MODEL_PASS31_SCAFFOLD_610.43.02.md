# HW Model Pass 31 — Scaffold (inherits pass30 impl wire; tick197)

**Date:** 2026-06-26  
**Driver:** NVIDIA-Linux-x86_64-610.43.02  
**Status:** Scaffold only; full pass31 RE pending.

## Mesa wire-up (tick197)

| Tick | Deliverable |
|------|-------------|
| 197 | Pass30 impl audit + Pass31 scaffold + G0-G4 symmetry + G2 launch/dispatch; channel pass31-first |
| 198 | Pass31 G1/G3/G4 helpers + channel G1/G3/G4 pass31 retry ladder; selftest -1012..-1016 |

## G0–G4 (pass31)

| Engine | Entry |
|--------|-------|
| G0–G4 sema | `nv_push_g0_g4_host_sema_tail_pass31` → pass30/29/…/21 |
| G1 CE copy | `nv_g1_emit_copy_then_host_sema_pass31` → pass31 sema tail |
| G2 compute | `nv_pass31_compute_object_emit_launch` → pass30 → … → pass22 |
| G3 3D barrier | `nv_3d_emit_g3_barrier_all_pass31` / `nv_3d_emit_g3_inv_wfi_host_sema_pass31` |
| G4 NVDEC | `nv_g4_emit_nvdec_bringup_pass31` → pass30 → pass29 |
| G4 NVENC | `nv_g4_emit_nvenc_bringup_pass31` → pass30 → pass29 |
| Dispatch tail | `nv_pass31_emit_compute_dispatch_host_sema_tail` |

## Channel ladder (pass31)

| Engine | Retry path |
|--------|-----------|
| G1 CE | pass21 → … → pass30 → **pass31** |
| G2 compute | pass22 → pass24 → … → pass30 → **pass31** |
| G3 3D | pass21 → … → pass30 → **pass31** |
| G4 NVDEC | pass17 → … → pass30 → **pass31** |
| G4 NVENC | pass17 → … → pass30 → **pass31** |

## Still pending

1. Pass31 impl audit (tick199)
2. Pass32 scaffold (tick199)
3. Full pass31 multi-hour RE (`re_pass31/`)
4. NIR/MME/silicon blocks unchanged