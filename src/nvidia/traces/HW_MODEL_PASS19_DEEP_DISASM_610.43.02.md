# HW Model Pass 19 — NVC597 vs Imm-Family Split, Emitter Chain Reality, VKSC Sema Mirror
# (NVIDIA-Linux-x86_64-610.43.02)

**Date:** 2026-06-25  
**Duration:** ~13.5 min automated deep analysis (`pass19_deep_analysis.py`, 809.5s) + synthesis; builds on multi-hour pass11–18 artifact base (~900+ pass18 files, pass17 sema/MME/QMD depth). Full x86 CFG pass deferred to pass20 (hours of objdump/xref).  
**Scope:** Beyond pass18 (pipeline imm completeness, egl sema mirror, permissive imm matrix) — **separate NVC597 method offsets from imm-family aliases**, **17 emitter ordered-chain probes (forward_ok reality check)**, **vksc sema mirror byte-identical to glcore**, **PCAS cooc at ±4KB**, **MME exact-header zero reconfirm**, **mesa gap post tick146–149**, **pass18 delta refine**.  
**Artifacts:** `/tmp/nvidia-reveng-pp-v2/re_pass19/` (`pass19_deep_analysis.py`, `notes/` ×12, `sema/`, `mme/`, `pcas/`, `qmd/`, `sph/`, `channel/`, `g2/`–`g4/`, `invalidate/`, `cb/`, `report_sema/`, `emitters/`, `tables/pass19_imm_matrix.json`, `delta_p18/`, `mesa_gap/`, `sass/`)  
**Mesa impact:** ticks 146–149 already ship pass17 sema + pass18 inv/CB/report helpers. Pass19 says: **prefer NVC597 offsets** (`0x021c`, `0x133x`, `0x238x`, `0x1b0x`) for Maxwell+ 3D; treat imm-family `0x0b00`/`0x1280`/`0x0d00` as **class-dependent aliases, not interchangeable**. **Emitter ordered templates almost never appear in static binary** (forward_ok 0/25 on 16/17 chains) — confirms runtime construction; mesa must keep explicit emit ladders, not expect to “discover” order from adjacency. **vksc** shares sema table (Vulkan SC path safe with pass17 policy).

---

## Method

1. Load glcore + 8 peer libs (eglcore, cuda, gpucomp, opencl, vdpau, cuvid, encode, vksc).
2. Sema formal walk (11 rows) + egl/vksc mirror hunt with full 11-row + glcore equality check.
3. Class ladder dword verify (unchanged primary tables).
4. Cross-lib imm matrix with **NVC597-aware** method set (`0x021c`, `0x1b00..`, `0x2380..`, plus pass18 families).
5. **17 emitter ordered chains** — greedy forward search within 8KB of successive imm sites (stricter test than pass18 “imm present”).
6. MME imm + exact `0x2000`/`0x2001` header scan + `0x385c` RAM proxies.
7. PCAS/QMD/SPH imm + cooc at ±2KB and ±4KB; weak structure cands (capped scans).
8. Inv/CB/report family counts + NVC597 window ladder probes.
9. G0–G4 pipeline imm completeness + emitter forward_ok merge.
10. Limited gpucomp multi-SR sample (stride 256; pass18 full scan hung).
11. Mesa priorities post tick148–149; pass18 delta JSON.
12. Executive summary.

**Reconfirmed limitation:** Pushbuffer headers are **runtime-constructed** (`0x2001xxxx`/`0x2000xxxx` exact = **0** for all key methods tested). Imm method offsets in x86 code are common; **ordered multi-method templates in the binary are rare** (pass19’s central new measurement).

---

## 1. Class ladders (unchanged, dword-verified)

| Engine | Offset | Ladder |
|--------|--------|--------|
| DMA `*B5` | `0x11bb600` | `CAB5 … B0B5` |
| COMPUTE `*C0` | `0x11bb640` | `CEC0 … B0C0` |
| 3D `*97` | `0x11bb680` | `CE97 … B097` |
| GPFIFO `*6F` | `0x1238b60` | `CA6F … 906F` |
| NVENC `*B7` | `0x1238ba0` | `C5B7 … D1B7` |
| NVDEC `*B0` | `0x1238be0` | `C4B0 … D1B0` |
| AUX `*FA` | `0x1238c20` | `C6FA … D1FA` |
| DMA secondary | `0x1236f80` | … + **`A0B5`** |

Cross-lib: eglcore/vksc mirror full sets (pass18); cuda/opencl GPFIFO-only in rodata scan. **No mesa class table changes.**

---

## 2. Host semaphore — pass19 triple mirror confirmation

### 2.1 glcore primary @ `0x11e30c0` — **11/11 OK**

Same authoritative rows as pass16–18 (exec/tag_a/tag_b/sema_idx/aux). Tail +0x170.. noise — do not implement.

| sema_idx | slot | executes |
|----------|------|----------|
| `0x12` | C | `0x1004` |
| `0x11` | B | `0x1002`, `0x0804` |
| `0x10` | A | `0x0802` |
| `0x08` / `0x07` | nonstd | `0x1004` / `0x1002` |
| `0x1d` | primary nonstd | `0x1001` |

### 2.2 Mirrors (byte-identical 11-row walks)

| Lib | Offset | match_glcore |
|-----|--------|--------------|
| **eglcore** | `0x114f2f0` | **True** (pass18) |
| **vksc-core** | `0x877390` | **True** (**pass19 new**) |

**Implication:** GL, EGL, and Vulkan SC userspace all embed the **same sema policy table**. Mesa pass17 default is correct across graphics stacks; no per-API sema table fork needed.

JSON: `re_pass19/sema/pass19_sema_formal.json`, `pass19_eglcore_mirror_*.json`, `pass19_vksc_mirror_*.json`.

---

## 3. NVC597 vs imm-family method split (pass19 core taxonomy)

Pass18 tracked many methods as raw imm offsets. Pass19 separates **class-correct NVC597 3D methods** (what mesa `nv_3d_methods.h` uses) from **imm-family clusters** that may belong to other classes or shared helpers.

### 3.1 NVC597 / Maxwell+ 3D (mesa-correct)

| Method | Role | glcore imm (capped) |
|--------|------|---------------------|
| `0x021c` | INVALIDATE_SHADER_CACHES | 50–100 |
| `0x1330` | INVALIDATE_SAMPLER_CACHE | 46 |
| `0x1334` | INVALIDATE_TEXTURE_HEADER_CACHE | 16 |
| `0x1338` | INVALIDATE_TEXTURE_DATA_CACHE | 16 |
| `0x2380`/`0x2384`/`0x2388` | SET_CONSTANT_BUFFER_SELECTOR A/B/C | 48/50/17 |
| `0x2410` | BIND_GROUP_CONSTANT_BUFFER(0) | 50 |
| `0x1b00`/`0x1b04`/`0x1b08`/`0x1b0c` | SET_REPORT_SEMAPHORE A–D | 50/12/19/15 |
| `0x0110` | WAIT_FOR_IDLE | 100 |
| `0x0540`… | pipeline/shader | high |

ticks 148–149 helpers use these. **Keep them.**

### 3.2 Imm-family clusters (do not assume = NVC597)

| Family | Range | glcore | Notes |
|--------|-------|--------|-------|
| inv `0x0b00` | `0x0b00`–`0x0b3c` | 17–50 | pass18 P1; may be other classes / older ladders |
| CB `0x1280` | `0x1280`–`0x12bc` | 6–50 | only chain with **1/25** static ordered hit |
| report `0x0d00` | `0x0d00`–`0x0d2c` | 13–50 | not the same as `0x1b00` report sema |

**Mesa rule:** For `*97` 3D class paths, emit **NVC597** methods. Only add imm-family offsets if class docs/silicon prove them for that class.

### 3.3 Window ladder probes (adjacent imm in ±window)

| Probe | Result | Interpretation |
|-------|--------|----------------|
| `0x021c` window has `0x133x`/`0x0110` imm | **1/60** | static adjacency rare; channel_prep still correct to emit explicitly |
| `0x1b00` window has B+C+D imm | **0/50** | report sema built incrementally / via helpers, not rodata template |
| `0x2380` window has B+C imm | **0/50** | CB selector similarly runtime-assembled |
| imm-family `0x1280` chain | **1/25** | weak exception; one span=784B sample |

---

## 4. Emitter ordered chains — **runtime construction proven**

Pass19’s most important negative result: **greedy forward ordered imm chains almost never succeed** even with 8KB windows and methods that individually have 50+ imm hits.

| Chain | imm counts (sample) | forward_ok /25 |
|-------|---------------------|----------------|
| MME_prime `3400→34a8→3800→39e0` | 50,16,50,29 | **0** |
| MME_full_no_ram / _ram | … | **0** |
| host_sema `200→204→208→20c` | 50×4 | **0** |
| report_sema NVC597 `1b00…1b0c` | 50,12,19,15 | **0** |
| inv_nvc597 / inv_b00 / inv_wfi_host | high | **0** |
| cb_sel_nvc597 | 48,50,17 | **0** |
| cb_fam_1280 | 50,34,16,41 | **1** (only success) |
| g1_ce_sema / g2_qmd_pcas / g2_inline | … | **0** |
| g3_clear_host / g3_inv_report / g3_inv_cb_report | … | **0** |

**Why this matters:** pass18 said “pipeline COMPLETE_IMM” (every method exists somewhere). Pass19 says “**ordered multi-method scripts are not sitting in the binary as searchable imm ladders**.” Proprietary driver builds method sequences in registers/stack at runtime. Open mesa **must continue hand-authored emit helpers** (ticks 146–149); more static RE will not replace that.

### 4.1 `0x3800` peer distances (reconfirm pass18)

| Peer | Top distances (bytes) | Pairs |
|------|----------------------|-------|
| **`0x39e0`** | **360×4**, 120×2, 720, 376, 256, 192 | 13 |
| `0x3880` | 96×4, 32×2, … | 14 |
| `0x34a8` | 1440, 1536 | 2 |
| `0x1b00` | 812, 432, 2000, … | 5 |
| `0x385c` / `0x3884` / `0x0110` / `0x021c` | — | **0** ordered |

**MME post-config:** optional `0x39e0` after `0x3800`; **360** still dominant static x86 distance (not a pushbuffer dword count).

**RAM_DATA:** no ordered `0x3800→0x3884` pairs; exact headers 0; permissive imm 22 in glcore — **still live-only payloads**.

JSON: `re_pass19/emitters/pass19_emitter_chains.json`.

---

## 5. MME / exact headers / RAM proxies

| Item | Result |
|------|--------|
| `0x34a8` / `0x39e0` / `0x3800` imm | present (16/29/59 glcore) — ship prime path |
| Exact `0x2001`/`0x2000` headers (8 key methods) | **all 0** |
| `0x3884` permissive imm | 22/20/11 glcore/egl/cuda — **noise-prone; not proof of static upload** |
| `0x385c` neighborhoods with small dwords / nearby `0x3880` | 4 proxies — config bookends only |

Do **not** implement MME ISA from static entropy.

---

## 6. PCAS / INLINE_QMD / QMD / SPH

### 6.1 Imm (glcore vs cuda)

| Method | glcore | cuda | Note |
|--------|--------|------|------|
| `0x6a04` QMD | **10** | **45** | compute driver richer |
| `0x6a08` PCAS | **10** | **33** | same |
| `0x6a0c` | 3 | 45 | |
| `0x02b4` INLINE | 100+ | 100+ | noisy common imm |

### 6.2 Co-occurrence (±2KB / ±4KB) — still essentially zero

All tested pairs (`6a04+6a08`, `02b4+6a04`, `6a08+02c0`, …) show **near2k≈0, near4k≈0** (same pass18 conclusion; widened window does not help).

### 6.3 Structure search

- QMD weak score≥5: **25** (capped; do not ship)
- SPH weak: **20** (do not ship)

Keep `nv_qmd.h` / `nv_sph.h` pass15–17 defaults.

---

## 7. Channel / pipeline imm completeness vs emitter reality

### 7.1 Imm presence (upper bound — all COMPLETE_IMM)

G1/G2/G3/G4 tracked pipelines all show full imm presence including NVC597 inv/CB/report sets. This is **necessary but not sufficient**.

### 7.2 Emitter forward_ok (sufficient for “found static script”)

Essentially **all zero** except one weak `0x1280` family hit. **Trust mesa emit order from design + live/golden traces, not static adjacency mining.**

---

## 8. SASS / multi-SR

Limited gpucomp stride-256 probe (avoid pass18 hang). Mesa directional set unchanged:

**SR0, SR1, SR3, SR48 (clock), SR49, SR4c, SR50** — multi-SR co-presence only; ignore absolute SR byte histograms.

Pass20: strided/SIMD full scan on cuda/opencl if needed.

---

## 9. Cross-lib imm matrix (9 libs)

Full matrix: `re_pass19/tables/pass19_imm_matrix.json`.

Notable:
- **vksc** has graphics-like imm (`0x021c`, `0x1b00`, `0x2380`, sema mirror) — not a thin shim only.
- **vdpau** sparse on NVC597 3D methods (`0x1b00`/`0x2380` often 0) — video path different.
- **cuda/opencl** dominate compute method density (`0x6a04`/`0x6a08`).

---

## 10. Pass18 → pass19 delta

| Area | pass18 | pass19 | Action |
|------|--------|--------|--------|
| Sema mirrors | egl `0x114f2f0` | +**vksc `0x877390`** identical | no code fork |
| Pipeline | COMPLETE_IMM | + **forward_ok ~0** | stop expecting static scripts |
| Inv/CB/report | imm families | **NVC597 vs family split** | prefer NVC597 in 3D |
| MME 3800→39e0 | dist 360 | reconfirmed 360×4 | optional post-config |
| PCAS cooc | ±2k ~0 | ±4k ~0 | live only |
| QMD/SPH | weak | weak 25/20 | keep mesa defs |
| Mesa code | helpers t148–149 | priorities refine only | t150–151 wire paths |

---

## 11. Mesa priorities (post tick146–149)

### Already shipped — confirm only
- pass17 sema formal + emit ladders (channel/Vulkan/Gallium)
- MME prime (`0x34a8`/`0x3800`/`0x39e0`); `g3_channel_prep_pass17`
- pass18 `inv_wfi_host_sema`, `cb_bind_group`, `report_sema`, `inv_cb_report`, `bringup_slice_pass18`
- nvgpu `memory_barrier` full inv+WFI
- multi-SR SPH directional; class ladders in rodata

### P0
1. Keep pass17 sema (egl+vksc mirrors identical).
2. Prefer **NVC597** inv/CB/report in 3D (`0x021c`/`0x133x`/`0x238x`/`0x1b0x`).
3. G3 push buffers ≥2048 dwords for MME prime selftests.

### P1 (next ticks)
1. Ensure channel_prep always emits **full NVC597 inv set** (021c + tex caches) — explicit, not relying on static adjacency.
2. Gallium **real constbuf bind** → `nv_3d_emit_g3_cb_bind_group_pass18` (not only smoke/barrier).
3. Audit **query/occlusion** report sema completeness (`0x1b00` path).
4. G1 CE pass17 sema on all CE submits.
5. Comment-only: `0x3800`→`0x39e0` ~360B x86 spacing.

### P2 — live / silicon (cannot close with static RE)
1. `0x3884` RAM_DATA payloads
2. PCAS / INLINE_QMD order
3. Real QMD/SPH blobs
4. pass17 vs classic sema on HW
5. AUX `*FA` / `506F`

### P3 — pass20 (multi-hour)
1. **x86 emitter CFG** at sema/MME/inv imm sites (objdump + callgraph; pass19 defers)
2. Map imm-family `0x0b00`/`0x1280`/`0x0d00` to class IDs
3. Video G4 live method completeness
4. Full SASS multi-SR on cuda/opencl (strided)

### Confidence matrix

| Area | Confidence | Action |
|------|------------|--------|
| Sema 11-row + egl/vksc mirrors | **HIGH** | shipped |
| Class ladders | **HIGH** | shipped |
| NVC597 inv/CB/report as 3D methods | **MED-HIGH** | prefer; t148–149 helpers OK |
| imm-family ≠ NVC597 | **MED** | class-dependent |
| MME prime/post imm | **MED-HIGH** | shipped |
| Static ordered emit scripts | **VERY LOW** | don’t hunt; hand-author |
| PCAS/INLINE order | **LOW** | live |
| RAM_DATA / QMD / SPH static | **VERY LOW** | live |

### Tick suggestions
- **t150:** G2 QMD sema tightening only; document PCAS/INLINE as HW-capture blockers (no fake static order).
- **t151:** Gallium constbuf → pass18 CB helper; query report sema audit.
- **t152+:** live trace if HW; pass20 x86 CFG.

---

## 12. How the binary driver “works” (synthesized mental model)

After pass11–19 static work, the honest architecture picture:

1. **Class selection** — rodata ladders (newest→oldest `*B5`/`*C0`/`*97`/`*6F`/`*B7`/`*B0`) for RM alloc / SET_OBJECT; mesa mirrors these.
2. **Pushbuffer construction** — almost entirely **runtime** in x86 (imm method numbers embedded in code; headers `0x2000xxxx` built in registers). Static RE finds method *vocabulary*, not *sentences*.
3. **Host sema policy** — rare rodata table (11 rows) shared across glcore/eglcore/vksc; execute×idx matrix is real and shippable (pass17).
4. **3D path** — NVC597 method space (inv `0x021c`/`0x133x`, CB `0x238x`/`0x2410`, report sema `0x1b0x`, draws/clears/MME `0x34xx`/`0x38xx`). Mesa helpers match this.
5. **MME** — scratch/sysmembar/post-probe imm exist; **RAM program bytes do not** appear as static imm streams (runtime upload).
6. **Compute** — QMD/PCAS method numbers more common in cuda/opencl than glcore; **order/layout not in binary** as adjacent imm; live capture required.
7. **SASS/SPH** — shader headers/code generated or loaded at runtime; multi-SR set is directional only from gpucomp heuristics.

Open userspace strategy that matches this: **ship rodata/policy tables + hand-written emit ladders + live/HW validation** — not “decompile the full emitter.”

---

## 13. Reproduce

```bash
python3 /tmp/nvidia-reveng-pp-v2/re_pass19/pass19_deep_analysis.py
# outputs: /tmp/nvidia-reveng-pp-v2/re_pass19/notes/pass19_*.md + JSON subdirs
```

**Report:** this file in `mesa/src/nvidia/traces/`.

---

*Pass19 reframes pass18 “everything is COMPLETE_IMM” as “vocabulary is complete; sentences are runtime-only.” Next high-value work is implementation paths (t150–151) or live trace / x86 CFG (pass20), not another imm recount.*
