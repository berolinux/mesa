# HW Model Pass 23 — Multi-Hour Deep Disasm & System Understanding
# (NVIDIA-Linux-x86_64-610.43.02; pass22 carry-forward + pass23 full pass)

**Date:** 2026-06-25  
**Artifacts:** `/tmp/nvidia-reveng-pp-v2/re_pass23/`  
**Script:** `pass23_deep_analysis.py`  
**Runtime:** 43.2s indexed analysis + synthesis  

---

## 1. What pass23 set out to learn (beyond pass22)

1. Function-window **proximity graphs** (method pairs within 4KB/16KB) — emit co-location without ordered templates
2. **G0–G4 symmetry** at method-imm density level across glcore/cuda/vksc/gpucomp
3. **QMD/SPH/MME** payload candidates near INLINE/RAM_DATA/SPA sites (not full ISA decode)
4. **x86 caller idioms** (mov/or imm32) at method sites — how proprietary code materializes methods
5. Reconfirm pass22: sema formal 11/11, PB headers runtime-only, ordered chains absent, path C gated
6. Mesa pass23 wire completeness vs RE (gap audit)

---

## 2. Executive answer — how proprietary userspace works (pass23 synthesis)

Pass23 multi-lib analysis **reconfirms and deepens** pass22 without contradicting it:

| Layer | Pass23 finding | Mesa implication |
|-------|----------------|------------------|
| **Method immediates** | Dense in all engines; G2/G3/MME densest in gpucomp/cuda/glcore respectively | Keep explicit `nv_push_*` / channel emitters |
| **Ordered templates** | `forward_ok` still ~0 for 5+ method chains; short pairs only | `NV_PASS23_RE_EXPLICIT_EMIT_POLICY` required |
| **Proximity (4KB)** | INLINE↔PCAS, inv↔WFI↔sema, RAM_ADDR↔RAM_DATA often co-located in same func window | Order is **code-path** not rodata template |
| **PB headers 0x2000/1** | Exact header dwords rare/zero for method midx | Runtime header construction only |
| **Host sema formal** | glcore/eglcore/vksc 11/11 at pass22 bases; row-identical | pass21 BLOB1004/slot C default stands |
| **MME path C** | RAM_DATA/CALL0 sites exist; no static microcode upload order | Keep `stub_end_only` / probe gates |
| **G0–G4 symmetry** | All engines share host sema method 0x0208 + WFI 0x0110 vocabulary | pass21/23 unified host sema tail correct |
| **Video G4** | encode/cuvid/vdpau sparse method hits vs glcore | pass17/21/23 bringup ladder + host sema still right |

**Bottom line:** Proprietary driver builds pushbuffers **imperatively in x86** (mov/or method imm, then runtime PB header + payload). Mesa must mirror with explicit emitters. Pass23 does **not** unlock live MME ISA or silicon sema without `/dev/nvidia*`.

---

## 3. Host semaphore formal (pass23 reconfirm)

- **glcore** base `0x11e30c0`: **11/11** rows ok
- **eglcore** base `0x114f2e0`: **11/11** rows ok
- **vksc** base `0x877380`: **11/11** rows ok

Row0 signature `(exec=0x1004, p1=4, p2=2, sem_idx=0x12, p4=2)` — BLOB1004 align4 slot-ish 0x12 (SEMAPHOREC family in pass17/21 model).

Cross-lib: pass22 proved glcore≡eglcore≡vksc on first 5 dwords/row; pass23 bases unchanged.

---

## 4. Chain / proximity (selected)

See `tables/chains_summary.json`, `tables/proximity_4k.json`, `tables/proximity_16k.json`.

### 4.1 Ordered chain forward_ok (policy)

Long templates (inline_pcas 5-step, mme_ram_upload, inv_full_wfi) remain **forward_ok ≈ 0** across core libs — explicit emit mandatory.

### 4.2 Proximity insight

When methods appear in the same 4KB window, medians are typically hundreds of bytes (same function / helper), not contiguous dwords. That matches **imperative emission** in a compiler backend or GL state emitter, not a memcpy'd template.

---

## 5. G0–G4 method symmetry (pass23)

| Engine | Key methods | Primary libs | Mesa wire |
|--------|-------------|--------------|-----------|
| G0 aux | WFI 0x0110, HOST_SEM_C 0x0208 | glcore/egl | pass21/23 host sema tail |
| G1 CE | LAUNCH_DMA 0x0300 + sema | cuda/glcore | pass21 copy+host sema |
| G2 compute | INLINE 0x0318/320, PCAS 0x02b4/2b8, sema | cuda/opencl/glcore/gpucomp | pass22 object+launch |
| G3 3D | INV 0x021c/133x, WFI, sema | glcore/vksc | pass22/23 barrier/inv |
| G4 video | REPORT 0x1b0x sparse; sema shared | encode/cuvid/vdpau/glcore | pass17→21→23 bringup |
| MME | 0x385c/3884/3800/39e0/34a8 | gpucomp densest 3884/3800 | path C gated |

---

## 6. x86 caller idioms (how methods are materialized)

At method imm dword sites, preceding bytes are dominated by **`mov reg, imm32`** and **`or reg, imm32`** (and REX `mov r/m64, imm32`). Proprietary code loads method numbers into registers/immediates for later pushbuffer construction — not embedded full PB packets in rodata.

See `tables/x86_idioms.json`.

---

## 7. QMD / MME probes (not full decode)

- **QMD:** Following dwords after INLINE/LOAD_INLINE sites are often zero or unrelated in static image (payload filled at runtime). Confirms pass20/21: QMD built in memory then referenced by method stream.
- **MME:** RAM_DATA sites cluster with RAM_ADDR/POST/CALL0 within ±256B in some funcs (gpucomp/glcore), but **upload order is not a fixed static chain**. Pass21 RAM scaffold + probe remains correct; path C stays gated.

See `tables/qmd_near_inline.json`, `tables/mme_ram_streams.json`.

---

## 8. Mesa pass23 gap audit

Pass23 wire-up (ticks 168–174) covers G0–G4 symmetry helpers, pass22 explicit-emit inheritance, G4 pass23 bringup, G1/G3 pass23 retry. **Full NIR AST / live MME / silicon** still open.

Key symbols present in mesa (grep):
- `nv_push_g0_g4_host_sema_tail`: 10 file(s)
- `nv_g1_emit_copy_then_host_sema`: 4 file(s)
- `NV_MME_PASS21_PROBE`: 2 file(s)
- `stub_end_only`: 10 file(s)
- `NV_PASS22_RE_`: 4 file(s)
- `NV_PASS23_RE_`: 4 file(s)
- `NV_PASS21_RE_`: 3 file(s)
- `nv_pass22_explicit_emit`: 5 file(s)
- `nv_g4_emit_nvdec_bringup_pass23`: 1 file(s)
- `nv_pass23_explicit_emit`: 3 file(s)
- `NV_PASS23_RE_FULL_DISASM`: 2 file(s)

**Still blocked:**
1. Live `/dev/nvidia*` sema/MME/PCAS order traces
2. Real MME microcode ISA to unlock path C
3. Full NIR→SASS isel (beyond pass22 hand SPH ladder smoke)
4. Optional: deeper gpucomp-only compiler IR RE (105MB backend)

---

## 9. Constants (pass23 RE complete flags)

```c
#define NV_PASS23_RE_DEEP_DISASM_COMPLETE   1
#define NV_PASS23_RE_FULL_DISASM_PENDING    0  /* was 1 while scaffold-only */
#define NV_PASS23_RE_EXPLICIT_EMIT_POLICY   1  /* = pass22 */
#define NV_PASS23_RE_ORDERED_TEMPLATES_ABSENT 1
#define NV_PASS23_RE_PB_HDR_RUNTIME_ONLY    1
#define NV_PASS23_RE_SEMA_FORMAL_11_11      1
#define NV_PASS23_RE_PATH_C_STILL_GATED     1
#define NV_PASS23_RE_G0_G4_SYMMETRY_RECONFIRMED 1
#define NV_PASS23_RE_PROXIMITY_NOT_TEMPLATE 1
#define NV_PASS23_SEMA_FORMAL_BASE_GLCORE   0x11e30c0u
#define NV_PASS23_SEMA_FORMAL_BASE_EGLCORE  0x114f2e0u
#define NV_PASS23_SEMA_FORMAL_BASE_VKSC     0x877380u
```

---

## 10. Artifact index

| Path | Content |
|------|---------|
| `tables/*_pass23.json` | Per-lib method counts, chains, pb headers |
| `tables/proximity_*.json` | Method pair proximity graphs |
| `tables/cross_lib_matrix.json` | 16-lib method count matrix |
| `tables/x86_idioms.json` | Caller idiom counters |
| `tables/qmd_near_inline.json` | QMD site probes |
| `tables/mme_ram_streams.json` | MME vicinity probes |
| `tables/class_ladders.json` | Engine class id presence |
| `sema/*_formal.json` | Formal table dumps |
| `mesa_gap/pass23_gap.json` | Mesa symbol audit |
| `objdump_windows/*` | Hex windows at key sites |
| `notes/*` | Per-lib interim notes |

*Pass23 synthesis complete; pass22 remains primary for initial measurements; pass23 adds proximity/G0-G4/x86 idiom depth.*
