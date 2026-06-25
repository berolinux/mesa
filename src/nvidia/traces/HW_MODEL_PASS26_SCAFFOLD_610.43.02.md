# HW Model Pass 26 — Scaffold (inherits pass25 impl wire; tick184)

**Date:** 2026-06-25  
**Driver:** NVIDIA-Linux-x86_64-610.43.02  
**Status:** Scaffold only; full pass26 RE pending.

## Mesa wire-up (tick184)

| Deliverable | Notes |
|-------------|-------|
| Pass26 RE constants | `NV_PASS26_RE_*`, `nv_pass26_policy_ok()` |
| Pass25 G4 channel | NVDEC pass25 retry after pass24 (`NV_PASS25_IMPL_G4_CHANNEL_TICK184`) |
| Inherits | pass25 impl wire complete + pass24/23 explicit-emit |

## Still pending

1. Full pass26 multi-hour RE (`re_pass26/`)
2. Pass24/25 full RE complete
3. NIR/MME/silicon blocks unchanged

*Pass23 deep + pass24 gpucomp interim remain primary RE measurements.*
