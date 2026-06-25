# HW Model Pass 24 — Scaffold + Implementation Audit (inherits pass23 deep)

**Date:** 2026-06-25  
**Driver:** NVIDIA-Linux-x86_64-610.43.02  
**Status:** Mesa pass24 **implementation wire complete** (ticks 176–179); full multi-hour `re_pass24/` disasm **still pending**.

## Inherited from pass23 (authoritative RE)

| Topic | Pass24 position |
|-------|-----------------|
| Explicit emit policy | Required (`nv_pass24_explicit_emit_required` → pass23/22) |
| Ordered templates | Absent statically (pass23 reconfirm) |
| Host sema formal | 11/11 glcore/egl/vksc; pass23 bases |
| MME path C | Still gated |
| G0–G4 symmetry | pass23 reconfirm + pass24 mesa ladder |
| Proximity ≠ template | pass23 graphs |

## Mesa wire-up (ticks 176–179)

| Tick | Deliverable |
|------|-------------|
| 176 | Pass24 RE constants; policy gate; G2 pass24 launch; NIR pass24 ladder; selftest -848..-855 |
| 177 | Channel pass24→pass22→pass21 G2 ladder; Gallium/VK pass24 dispatch tail; -856..-860 |
| 178 | G1/G3/G4 pass24 emitters + channel retry; G0–G4 symmetry flags; -861..-866 |
| 179 | Implementation audit (`nv_pass24_implementation_audit_ok`); this trace update; -867..-872 |

## G0–G4 pass24 implementation map

| Engine | Pass24 entry | Channel |
|--------|--------------|---------|
| G0 aux | `nv_push_g0_g4_host_sema_tail_pass24` | via unified sema on 3D subch |
| G1 CE | `nv_g1_emit_copy_then_host_sema_pass24` | pass23 then pass24 retry |
| G2 compute | `nv_pass24_compute_object_emit_launch` | pass24 first, then pass22/21 |
| G3 3D | `nv_3d_emit_g3_*_pass24` | pass23 then pass24 inv+host |
| G4 video | `nv_g4_emit_*_bringup_pass24` | pass23 then pass24 NVDEC/NVENC |

## Pass24 RE vs implementation

| Layer | State |
|-------|-------|
| Mesa G0–G4 helpers + channel | **WIRE_COMPLETE** (`NV_PASS24_IMPL_WIRE_COMPLETE`) |
| `re_pass24/` multi-hour disasm | **PENDING** (`NV_PASS24_RE_FULL_DISASM_PENDING`) |
| gpucomp MME/caller CFG focus | Planned only |
| Silicon `/dev/nvidia*` | Blocked on autoloop host |

## Constants

```c
NV_PASS24_RE_SCAFFOLD
NV_PASS24_IMPL_WIRE_COMPLETE
NV_PASS24_IMPL_AUDIT_TICK179
NV_PASS24_RE_FULL_DISASM_PENDING   // still 1
nv_pass24_implementation_audit_ok()
nv_pass24_g0_g4_symmetry_ok()
```

## Still blocked / next RE

1. Full pass24 gpucomp-first multi-hour disasm (`/tmp/nvidia-reveng-pp-v2/re_pass24/`)
2. Real NIR AST isel (hand SPH ladder still)
3. Live MME ISA / path C unlock
4. Hardware G0–G4 sema completion

*Artifacts: this scaffold; pass23 `HW_MODEL_PASS23_DEEP_DISASM` remains primary for static measurements.*
