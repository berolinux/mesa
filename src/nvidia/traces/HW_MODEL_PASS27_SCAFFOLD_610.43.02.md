# HW Model Pass 27 — Scaffold (inherits pass26 impl wire; tick188)

**Date:** 2026-06-25  
**Driver:** NVIDIA-Linux-x86_64-610.43.02  
**Status:** Scaffold only; full pass27 RE pending.

## Mesa wire-up (ticks 188–190)

| Tick | Deliverable |
|------|-------------|
| 188 | Pass27 RE constants; pass26 G4 NVDEC channel pass26 |
| 189 | G0–G4 pass27 symmetry; pass27 G2 launch/dispatch; channel pass27-first |
| 190 | G1/G3/G4 pass27 emitters; channel G1/G3/G4 pass27 retry |

## G0–G4 (pass27)

| Engine | Entry |
|--------|-------|
| G0–G4 sema | `nv_push_g0_g4_host_sema_tail_pass27` → pass26/21 |
| G1 CE copy | `nv_g1_emit_copy_then_host_sema_pass27` → pass26 |
| G2 compute | `nv_pass27_compute_object_emit_launch` → pass26 → pass25 → pass24 → pass22 |
| G3 3D barrier | `nv_3d_emit_g3_barrier_all_pass27`, `nv_3d_emit_g3_inv_wfi_host_sema_pass27` |
| G4 NVDEC | `nv_g4_emit_nvdec_bringup_pass27` → pass26 |
| G4 NVENC | `nv_g4_emit_nvenc_bringup_pass27` → pass26 |

## Channel ladder (pass27)

| Engine | Retry path |
|--------|-----------|
| G1 CE | pass21 → pass22 → pass24 → pass25 → pass26 → **pass27** |
| G3 3D | pass21 → pass22 → pass24 → pass25 → pass26 → **pass27** |
| G4 NVDEC | pass17 → pass21 → pass23 → pass24 → pass25 → pass26 → **pass27** |
| G4 NVENC | pass17 → pass23 → pass24 → **pass27** |

## Still pending

1. Full pass27 multi-hour RE (`re_pass27/`)
2. Pass27 impl audit (tick191)
3. NIR/MME/silicon blocks unchanged

*Pass23 deep + pass24 gpucomp interim remain primary RE measurements.*
