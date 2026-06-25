# HW Model Pass 25 — Scaffold (inherits pass24 impl wire; tick180–181)

**Date:** 2026-06-25  
**Driver:** NVIDIA-Linux-x86_64-610.43.02  
**Status:** Scaffold + G0–G4 symmetry wire; full pass25 RE pending.

## Mesa wire-up

| Tick | Deliverable |
|------|-------------|
| 180 | Pass25 RE constants; pass24 gpucomp interim RE |
| 181 | G0–G4 pass25 symmetry; pass25 G2 launch/dispatch; channel pass25-first ladder |
| 182 | G1/G3/G4 pass25 emitters; channel G1/G3 pass25 retry |
| 183 | Implementation audit (`nv_pass25_implementation_audit_ok`); wire complete flag |

## G0–G4 (pass25)

| Engine | Entry |
|--------|-------|
| G0–G4 sema | `nv_push_g0_g4_host_sema_tail_pass25` → pass24/21 |
| G2 compute | `nv_pass25_compute_object_emit_launch` → pass24 → pass22 |
| Dispatch tail | `nv_pass25_emit_compute_dispatch_host_sema_tail` |

## Pass25 RE vs implementation

| Layer | State |
|-------|-------|
| Mesa G0–G4 helpers + channel | **WIRE_COMPLETE** (`NV_PASS25_IMPL_WIRE_COMPLETE`) |
| `re_pass25/` multi-hour disasm | **PENDING** |
| Pass24 full multi-lib RE | **PENDING** (`NV_PASS24_RE_FULL_DISASM_PENDING`) |

## Still pending

1. Full pass25 multi-hour RE (`re_pass25/`)
2. Pass24 full multi-lib RE complete
3. NIR/MME/silicon blocks unchanged

*Pass23 deep model + pass24 gpucomp interim remain primary RE measurements.*
