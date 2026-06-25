# HW Model Pass 27 — Implementation Complete (tick191; RE pending)

**Date:** 2026-06-26  
**Driver:** NVIDIA-Linux-x86_64-610.43.02  
**Status:** Mesa wire complete; full pass27 RE pending.

## Mesa wire-up (ticks 188–191)

| Tick | Deliverable |
|------|-------------|
| 188 | Pass27 RE constants; pass26 G4 NVDEC channel pass26 |
| 189 | G0–G4 pass27 symmetry; pass27 G2 launch/dispatch; channel pass27-first |
| 190 | G1/G3/G4 pass27 emitters; channel G1/G3/G4 pass27 retry |
| 191 | Implementation audit (`nv_pass27_implementation_audit_ok`); wire complete |

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

## Implementation audit (tick191)

`nv_pass27_implementation_audit_ok()` checks:
- Scaffold (tick188) + G0-G4 symmetry (tick189) + G1/G3/G4 helpers (tick190)
- G2 channel launch ladder (tick189) + G1/G3/G4 channel retry (tick190)
- Wire complete flag + policy coherent with pass26 audit

## Still pending

1. Full pass27 multi-hour RE (`re_pass27/`)
2. Pass28 scaffold (next pass)
3. NIR/MME/silicon blocks unchanged

*Pass23 deep + pass24 gpucomp interim remain primary RE measurements.*
