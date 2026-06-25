# HW Model Pass 23 — Scaffold (inherits pass22; ticks 168–172)

**Date:** 2026-06-25  
**Driver:** NVIDIA-Linux-x86_64-610.43.02  
**Status:** Scaffold superseded by tick175 — see `HW_MODEL_PASS23_DEEP_DISASM_610.43.02.md` and `/tmp/nvidia-reveng-pp-v2/re_pass23/`.

## Inherited from pass22 (authoritative)

| Topic | Pass23 position |
|-------|-----------------|
| Explicit emit policy | Required (`nv_pass23_explicit_emit_required` → pass22) |
| Ordered templates | Absent statically (INLINE/PCAS/MME/inv chains) |
| PB headers | Runtime-only via `nv_push_*` |
| Host sema formal | 11/11 glcore/egl/vksc; BLOB1004/slot C default |
| MME path C | Still gated (`stub_end_only`) |
| INLINE→PCAS medians | glcore 428 / cuda 2204 / vksc 2724 |

## Mesa wire-up (ticks 168–172)

| Tick | Deliverable |
|------|-------------|
| 168 | Pass23 RE constants; G4 `nv_g4_emit_*_bringup_pass23` |
| 169 | Channel NVDEC pass21/pass23 retry after pass17 |
| 170 | Channel NVENC pass21/pass23 retry (parity) |
| 171 | G0–G4 symmetry audit; `nv_push_g0_g4_host_sema_tail_pass23` |
| 172 | This scaffold trace |

## G0–G4 symmetry (pass23)

| Engine | Implementation |
|--------|----------------|
| G0 aux | pass21/23 host sema tail on 3D subch methods |
| G1 CE | `nv_g1_emit_copy_then_host_sema_pass21` |
| G2 compute | pass22 compute object + pass21/22 launch |
| G3 3D | pass22 barrier / inv + pass21 host sema |
| G4 video | pass17 → pass21 → pass23 bringup ladder |

## Still blocked / next RE

1. ~~Full pass23 multi-lib disasm~~ — **done tick175** (`re_pass23/`, 16 libs)
2. Real NIR AST isel (still SASS smoke / hand SPH for pass22 kinds)
3. Silicon sema/MME/PCAS order on `/dev/nvidia*`
4. Live MME ISA to unlock path C

## Constants

```c
NV_PASS23_RE_SCAFFOLD
NV_PASS23_RE_EXPLICIT_EMIT_POLICY      // = pass22
NV_PASS23_G0_G4_SYMMETRY_AUDIT
NV_PASS23_G4_VIDEO_PASS21_PASS23
```

*Artifacts: mesa/src/nvidia/traces/HW_MODEL_PASS23_SCAFFOLD_610.43.02.md; pass22 deep model still primary for measurements.*
