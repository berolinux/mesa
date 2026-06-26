# HW Model Pass 29 — Scaffold (inherits pass28 impl wire; tick193)

**Date:** 2026-06-26  
**Driver:** NVIDIA-Linux-x86_64-610.43.02  
**Status:** Scaffold only; full pass29 RE pending.

## Mesa wire-up (ticks 193–194)

| Tick | Deliverable |
|------|-------------|
| 193 | Pass28 impl audit + Pass29 scaffold + G0-G4 symmetry + G2 launch/dispatch; channel pass29-first |
| 194 | G1/G3/G4 pass29 emitters; channel G1/G3/G4 pass29 retry |

## G0–G4 (pass29)

| Engine | Entry |
|--------|-------|
| G0–G4 sema | `nv_push_g0_g4_host_sema_tail_pass29` → pass28/27/26/21 |
| G1 CE copy | `nv_g1_emit_copy_then_host_sema_pass29` → pass29 sema tail |
| G2 compute | `nv_pass29_compute_object_emit_launch` → pass28 → pass27 → … → pass22 |
| G3 3D barrier | `nv_3d_emit_g3_barrier_all_pass29`, `nv_3d_emit_g3_inv_wfi_host_sema_pass29` |
| G4 NVDEC | `nv_g4_emit_nvdec_bringup_pass29` → pass28 |
| G4 NVENC | `nv_g4_emit_nvenc_bringup_pass29` → pass28 |
| Dispatch tail | `nv_pass29_emit_compute_dispatch_host_sema_tail` |

## Channel ladder (pass29)

| Engine | Retry path |
|--------|-----------|
| G1 CE | pass21 → pass22 → pass24 → pass25 → pass26 → pass27 → pass28 → **pass29** |
| G2 compute | pass22 → pass24 → pass25 → pass26 → pass27 → pass28 → **pass29** |
| G3 3D | pass21 → pass22 → pass24 → pass25 → pass26 → pass27 → pass28 → **pass29** |
| G4 NVDEC | pass17 → pass21 → pass23 → pass24 → pass25 → pass26 → pass27 → pass28 → **pass29** |
| G4 NVENC | pass17 → pass23 → pass24 → pass27 → pass28 → **pass29** |

## Implementation audit (tick195)

- `nv_pass29_implementation_audit_ok()` validates all pass29 constants
- `NV_PASS29_IMPL_WIRE_COMPLETE` = 1
- `NV_PASS29_RE_TRACE_SCAFFOLD_TICK195` = 1
- Pass30 scaffold created (inherits pass29 wire)

## Still pending

1. Full pass29 multi-hour RE (`re_pass29/`)
2. NIR/MME/silicon blocks unchanged
3. Pass30 G1/G3/G4 helpers (tick196)