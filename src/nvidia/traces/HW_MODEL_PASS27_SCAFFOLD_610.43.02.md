# HW Model Pass 27 — Scaffold (inherits pass26 impl wire; tick188)

**Date:** 2026-06-25  
**Driver:** NVIDIA-Linux-x86_64-610.43.02  
**Status:** Scaffold only; full pass27 RE pending.

## Mesa wire-up (ticks 188–189)

| Tick | Deliverable |
|------|-------------|
| 188 | Pass27 RE constants; pass26 G4 NVDEC channel pass26 |
| 189 | G0–G4 pass27 symmetry; pass27 G2 launch/dispatch; channel pass27-first |

## G0–G4 (pass27)

| Engine | Entry |
|--------|-------|
| G0–G4 sema | `nv_push_g0_g4_host_sema_tail_pass27` → pass26/21 |
| G2 compute | `nv_pass27_compute_object_emit_launch` → pass26 → pass25 → pass24 → pass22 |

## Still pending

1. Full pass27 multi-hour RE (`re_pass27/`)
2. Pass24/25/26 full RE complete
3. NIR/MME/silicon blocks unchanged

*Pass23 deep + pass24 gpucomp interim remain primary RE measurements.*
