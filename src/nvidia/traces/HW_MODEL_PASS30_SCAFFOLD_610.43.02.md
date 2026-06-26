# HW Model Pass 30 — Scaffold (inherits pass29 impl wire; tick195)

**Date:** 2026-06-26  
**Driver:** NVIDIA-Linux-x86_64-610.43.02  
**Status:** Scaffold only; full pass30 RE pending.

## Mesa wire-up (ticks 195–196)

| Tick | Deliverable |
|------|-------------|
| 195 | Pass29 impl audit + Pass30 scaffold + G0-G4 symmetry + G2 launch/dispatch; channel pass30-first |
| 196 | G1/G3/G4 pass30 emitters; channel G1/G3/G4 pass30 retry |

## G0–G4 (pass30)

| Engine | Entry |
|--------|-------|
| G0–G4 sema | `nv_push_g0_g4_host_sema_tail_pass30` → pass29/28/27/26/21 |
| G1 CE copy | `nv_g1_emit_copy_then_host_sema_pass30` → pass30 sema tail |
| G2 compute | `nv_pass30_compute_object_emit_launch` → pass29 → pass28 → … → pass22 |
| G3 3D barrier | `nv_3d_emit_g3_barrier_all_pass30`, `nv_3d_emit_g3_inv_wfi_host_sema_pass30` |
| G4 NVDEC | `nv_g4_emit_nvdec_bringup_pass30` → pass29 |
| G4 NVENC | `nv_g4_emit_nvenc_bringup_pass30` → pass29 |
| Dispatch tail | `nv_pass30_emit_compute_dispatch_host_sema_tail` |

## Channel ladder (pass30)

| Engine | Retry path |
|--------|-----------|
| G1 CE | pass21 → pass22 → pass24 → … → pass29 → **pass30** |
| G2 compute | pass22 → pass24 → pass25 → pass26 → pass27 → pass28 → pass29 → **pass30** |
| G3 3D | pass21 → pass22 → pass24 → … → pass29 → **pass30** |
| G4 NVDEC | pass17 → pass21 → pass23 → … → pass29 → **pass30** |
| G4 NVENC | pass17 → pass23 → pass24 → pass27 → pass28 → pass29 → **pass30** |

## Implementation audit (tick197)

- `nv_pass30_implementation_audit_ok()` validates all pass30 constants
- `NV_PASS30_IMPL_WIRE_COMPLETE` = 1
- `NV_PASS30_RE_TRACE_SCAFFOLD_TICK197` = 1
- Pass31 scaffold created (inherits pass30 wire)

## Still pending

1. Full pass30 multi-hour RE (`re_pass30/`)
2. NIR/MME/silicon blocks unchanged
3. Pass31 G1/G3/G4 helpers (tick198)