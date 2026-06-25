# HW Model Pass 28 — Scaffold (inherits pass27 impl wire; tick191)

**Date:** 2026-06-26  
**Driver:** NVIDIA-Linux-x86_64-610.43.02  
**Status:** Scaffold only; full pass28 RE pending.

## Mesa wire-up (tick 191)

| Tick | Deliverable |
|------|-------------|
| 191 | Pass28 scaffold + G0-G4 symmetry + G2 launch/dispatch; channel pass28-first |

## G0–G4 (pass28)

| Engine | Entry |
|--------|-------|
| G0–G4 sema | `nv_push_g0_g4_host_sema_tail_pass28` → pass27/26/21 |
| G2 compute | `nv_pass28_compute_object_emit_launch` → pass27 → pass26 → pass25 → pass24 → pass22 |
| Dispatch tail | `nv_pass28_emit_compute_dispatch_host_sema_tail` |

## Still pending

1. Pass28 G1/G3/G4 helpers (tick192)
2. Full pass28 multi-hour RE (`re_pass28/`)
3. NIR/MME/silicon blocks unchanged

*Pass23 deep + pass24 gpucomp interim remain primary RE measurements.*
