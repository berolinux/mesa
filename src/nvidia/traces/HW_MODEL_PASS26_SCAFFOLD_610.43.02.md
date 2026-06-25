# HW Model Pass 26 — Scaffold (inherits pass25 impl wire; tick184)

**Date:** 2026-06-25  
**Driver:** NVIDIA-Linux-x86_64-610.43.02  
**Status:** Scaffold only; full pass26 RE pending.

## Mesa wire-up (ticks 184–185)

| Tick | Deliverable |
|------|-------------|
| 184 | Pass26 RE constants; pass25 G4 NVDEC channel pass25 |
| 185 | G0–G4 pass26 symmetry; pass26 G2 launch/dispatch; channel pass26-first |

## G0–G4 (pass26)

| Engine | Entry |
|--------|-------|
| G0–G4 sema | `nv_push_g0_g4_host_sema_tail_pass26` → pass25/21 |
| G2 compute | `nv_pass26_compute_object_emit_launch` → pass25 → pass24 → pass22 |

## Still pending

1. Full pass26 multi-hour RE (`re_pass26/`)
2. Pass24/25 full RE complete
3. NIR/MME/silicon blocks unchanged

*Pass23 deep + pass24 gpucomp interim remain primary RE measurements.*
