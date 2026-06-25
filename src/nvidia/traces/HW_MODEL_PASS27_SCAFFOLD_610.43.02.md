# HW Model Pass 27 — Scaffold (inherits pass26 impl wire; tick188)

**Date:** 2026-06-25  
**Driver:** NVIDIA-Linux-x86_64-610.43.02  
**Status:** Scaffold only; full pass27 RE pending.

## Mesa wire-up (tick188)

| Deliverable | Notes |
|-------------|-------|
| Pass27 RE constants | `NV_PASS27_RE_*`, `nv_pass27_policy_ok()` |
| Pass26 G4 channel | NVDEC pass26 retry after pass25 (`NV_PASS26_IMPL_G4_CHANNEL_TICK188`) |
| Inherits | pass26 impl wire complete + pass25/24/23 explicit-emit |

## Still pending

1. Full pass27 multi-hour RE (`re_pass27/`)
2. Pass24/25/26 full RE complete
3. NIR/MME/silicon blocks unchanged

*Pass23 deep + pass24 gpucomp interim remain primary RE measurements.*
