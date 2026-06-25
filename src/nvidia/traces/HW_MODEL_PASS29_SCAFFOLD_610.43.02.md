# HW Model Pass 29 — Scaffold (inherits pass28 impl wire; tick193)

**Date:** 2026-06-26  
**Driver:** NVIDIA-Linux-x86_64-610.43.02  
**Status:** Scaffold only; full pass29 RE pending.

## Mesa wire-up (tick193)

| Tick | Deliverable |
|------|-------------|
| 193 | Pass28 impl audit + Pass29 scaffold + G0-G4 symmetry + G2 launch/dispatch; channel pass29-first |

## G0–G4 (pass29)

| Engine | Entry |
|--------|-------|
| G0–G4 sema | `nv_push_g0_g4_host_sema_tail_pass29` → pass28/27/26/21 |
| G2 compute | `nv_pass29_compute_object_emit_launch` → pass28 → pass27 → … → pass22 |
| Dispatch tail | `nv_pass29_emit_compute_dispatch_host_sema_tail` |

## Channel ladder (pass29)

| Engine | Retry path |
|--------|-----------|
| G2 compute | pass22 → pass24 → pass25 → pass26 → pass27 → pass28 → **pass29** |

## Still pending

1. Pass29 G1/G3/G4 helpers (tick194)
2. Pass29 impl audit (tick195)
3. Full pass29 multi-hour RE (`re_pass29/`)
4. NIR/MME/silicon blocks unchanged