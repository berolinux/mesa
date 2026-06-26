# HW Model Pass 31 — Scaffold (inherits pass30 impl wire; tick197)

**Date:** 2026-06-26  
**Driver:** NVIDIA-Linux-x86_64-610.43.02  
**Status:** Scaffold only; full pass31 RE pending.

## Mesa wire-up (tick197)

| Tick | Deliverable |
|------|-------------|
| 197 | Pass30 impl audit + Pass31 scaffold + G0-G4 symmetry + G2 launch/dispatch; channel pass31-first |

## G0–G4 (pass31)

| Engine | Entry |
|--------|-------|
| G0–G4 sema | `nv_push_g0_g4_host_sema_tail_pass31` → pass30/29/…/21 |
| G2 compute | `nv_pass31_compute_object_emit_launch` → pass30 → … → pass22 |
| Dispatch tail | `nv_pass31_emit_compute_dispatch_host_sema_tail` |

## Channel ladder (pass31)

| Engine | Retry path |
|--------|-----------|
| G2 compute | pass22 → pass24 → … → pass30 → **pass31** |

## Still pending

1. Pass31 G1/G3/G4 helpers (tick198)
2. Pass31 impl audit (tick199)
3. Full pass31 multi-hour RE (`re_pass31/`)
4. NIR/MME/silicon blocks unchanged