# HW Model Pass 30 — Scaffold (inherits pass29 impl wire; tick195)

**Date:** 2026-06-26  
**Driver:** NVIDIA-Linux-x86_64-610.43.02  
**Status:** Scaffold only; full pass30 RE pending.

## Mesa wire-up (tick195)

| Tick | Deliverable |
|------|-------------|
| 195 | Pass29 impl audit + Pass30 scaffold + G0-G4 symmetry + G2 launch/dispatch; channel pass30-first |

## G0–G4 (pass30)

| Engine | Entry |
|--------|-------|
| G0–G4 sema | `nv_push_g0_g4_host_sema_tail_pass30` → pass29/28/27/26/21 |
| G2 compute | `nv_pass30_compute_object_emit_launch` → pass29 → pass28 → … → pass22 |
| Dispatch tail | `nv_pass30_emit_compute_dispatch_host_sema_tail` |

## Channel ladder (pass30)

| Engine | Retry path |
|--------|-----------|
| G2 compute | pass22 → pass24 → pass25 → pass26 → pass27 → pass28 → pass29 → **pass30** |

## Still pending

1. Pass30 G1/G3/G4 helpers (tick196)
2. Pass30 impl audit (tick197)
3. Full pass30 multi-hour RE (`re_pass30/`)
4. NIR/MME/silicon blocks unchanged