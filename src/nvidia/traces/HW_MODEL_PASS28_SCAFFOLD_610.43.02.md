# HW Model Pass 28 — Scaffold (inherits pass27 impl wire; tick191)

**Date:** 2026-06-26  
**Driver:** NVIDIA-Linux-x86_64-610.43.02  
**Status:** Scaffold only; full pass28 RE pending.

## Mesa wire-up (ticks 191–192)

| Tick | Deliverable |
|------|-------------|
| 191 | Pass28 scaffold + G0-G4 symmetry + G2 launch/dispatch; channel pass28-first |
| 192 | G1/G3/G4 pass28 emitters; channel G1/G3/G4 pass28 retry |

## G0–G4 (pass28)

| Engine | Entry |
|--------|-------|
| G0–G4 sema | `nv_push_g0_g4_host_sema_tail_pass28` → pass27/26/21 |
| G1 CE copy | `nv_g1_emit_copy_then_host_sema_pass28` → pass27 |
| G2 compute | `nv_pass28_compute_object_emit_launch` → pass27 → pass26 → pass25 → pass24 → pass22 |
| G3 3D barrier | `nv_3d_emit_g3_barrier_all_pass28`, `nv_3d_emit_g3_inv_wfi_host_sema_pass28` |
| G4 NVDEC | `nv_g4_emit_nvdec_bringup_pass28` → pass27 |
| G4 NVENC | `nv_g4_emit_nvenc_bringup_pass28` → pass27 |
| Dispatch tail | `nv_pass28_emit_compute_dispatch_host_sema_tail` |

## Channel ladder (pass28)

| Engine | Retry path |
|--------|-----------|
| G1 CE | pass21 → pass22 → pass24 → pass25 → pass26 → pass27 → **pass28** |
| G3 3D | pass21 → pass22 → pass24 → pass25 → pass26 → pass27 → **pass28** |
| G4 NVDEC | pass17 → pass21 → pass23 → pass24 → pass25 → pass26 → pass27 → **pass28** |
| G4 NVENC | pass17 → pass23 → pass24 → pass27 → **pass28** |

## Implementation audit (tick193)

- `nv_pass28_implementation_audit_ok()` validates all pass28 constants
- `NV_PASS28_IMPL_WIRE_COMPLETE` = 1
- `NV_PASS28_RE_TRACE_SCAFFOLD_TICK193` = 1
- Pass29 scaffold created (inherits pass28 wire)

## Still pending

1. Full pass28 multi-hour RE (`re_pass28/`)
2. NIR/MME/silicon blocks unchanged
3. Pass29 G1/G3/G4 helpers (tick194)

*Pass23 deep + pass24 gpucomp interim remain primary RE measurements.*
