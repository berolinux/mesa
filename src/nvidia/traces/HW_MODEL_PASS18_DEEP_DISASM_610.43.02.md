# HW Model Pass 18 — Pipeline Signatures, Sema Mirrors, Inv/CB/PCAS Depth, Cross-Lib Imm Matrix
# (NVIDIA-Linux-x86_64-610.43.02)

**Date:** 2026-06-25  
**Duration:** multi-hour session (phase1 targeted dumps + 16-lib imm index; phase1 SASS partial ~20 min then interrupted/resumed; phase2 deep analysis ~543s; phase3 supplement + phase4 24-lib mine running/complete in parallel; synthesis)  
**Scope:** Beyond pass17 (formal sema 11 rows, cross-lib ladders, MME imm catalog, QMD/SPH weak, S2R multi-SR) — **eglcore sema mirror confirmation**, **G0–G4 pipeline completeness matrix**, **invalidate/CB/report imm ladders + ngrams**, **PCAS/INLINE_QMD/compute imm + co-occurrence**, **MME ordered distances refined (0x3800→0x39e0 dist=360 dominant)**, **RAM_DATA imm re-evaluation + 0x385c proxy patterns**, **16-lib imm site index**, **9-lib class ladder auto-scan (incl. vksc-core full mirror)**, **NVOS32/ioctl string markers**, **pass17→18 delta**, **mesa P0–P3 refined for pass19/silicon/live**.  
**Artifacts:** `/tmp/nvidia-reveng-pp-v2/re_pass18/` (`pass18_mine.py`, `pass18_deep_analysis.py`, `pass18_supplement.py`, `pass18_targeted_disasm.sh`, `pass18_master.sh`, `notes/`, `disasm/`+`x86_callers/`+`sass/` ~900+ files, `tables/`, `sema/`, `qmd/`, `sph/`, `mme/`, `mme_isa/`, `classes/`, `channel/`, `g2/`–`g4/`, `invalidate/`, `cb/`, `pcas/`, `emitters/`, `delta_p17/`, …)  
**Mesa impact (recommended this pass):** Keep pass17 sema as default (eglcore mirror @ `0x114f2f0` proves identical policy in EGL path). **Promote P1 inv/CB/report** (`0x0b00`–`0x0b0c`, `0x1280`–`0x1294`, `0x0d00`–`0x0d0c`) if not fully wired in `nv_3d_methods.h` / Gallium. **MME prime** remains `0x3400`/`0x34a8`/`0x3800`/`0x39e0` with post-config distance bias **360 bytes** (also 72/120/192/256/376). Treat pass18 `0x3884` imm hits as **noisy/permissive** — still require live trace for real RAM_DATA payloads. Do **not** promote QMD/SPH/MME-ISA static candidates. G2 PCAS/INLINE imm present but **co-occurrence weak** (emit order live-only).

---

## Method

1. **Phase 1 targeted dumps** (`pass18_targeted_disasm.sh`) — ELF sections/dynamic (17 libs); class/sema/GPFIFO/video anchors; sema descriptor cluster dumps (12 pages); method imm site index across **16 priority libs** (~26k site lines); gpucomp/ptx/nvvm/cuda/opencl SASS/S2R (partial — full byte-scan aborted mid-cuda due to runtime, supplemented with pass17 directional multi-SR); strings filters; class ladder auto-scan (9 libs with runs, incl. **vksc-core** full mirror + nvidia_drv partial).
2. **Phase 2 focus analysis** (`pass18_deep_analysis.py`, ~543s) — sema formal + egl mirror hunt, classes, MME/distances/proxies, PCAS/QMD/SPH, inv/CB/report, SASS, channel/pipelines, SPA/CE/video, NVOS32 markers, pass17 delta, mesa priorities, executive → `notes/pass18_*.md`.
3. **Phase 3 supplemental probes** (`pass18_supplement.py`) — exact header dwords, cross-lib imm matrix, sema integrity, emitter ordered chains, 0x3800 peer distances, string markers, class ladder exact verify.
4. **Phase 4 full mine** (`pass18_mine.py`) — 24 priority libs, per-lib JSON + `tables/pass18_crosslib_summary.json`.
5. **Manual synthesis** — this report; cross-check pass11–17; refine mesa action list.

**Reconfirmed limitation:** Pushbuffer method headers are almost always **runtime-constructed**. Trust order: (a) imm method offsets/indices, (b) rodata class ID arrays, (c) rodata sema config tables (11 rows only — stop before tail noise), (d) tight method n-grams (SET_OBJECT=0 filtered), (e) heuristic gpucomp SASS/S2R (directional multi-SR only), (f) weak QMD/SPH/MME-ISA (do not ship), (g) open-gpu-doc / OGKM headers, (h) **live trace** for RAM_DATA payloads, PCAS/INLINE emit order, real QMD/SPH blobs.

**Pass18 refinements on limitations:**
- **Permissive imm scanner caveat:** pass18 counts dwords with `low16==method && high16<0x100` (plus exact method dword). This finds more hits than pass16/17 strict scans, including possible non-method noise (esp. small/common offsets like `0x0200`, `0x0110`, and even `0x3884`). **Cross-check with pass17 counts and rodata/context before promoting.** Exact `0x2001xxxx`/`0x2000xxxx` headers still effectively **0** for key methods (runtime-built).
- **Sema cluster count low (12)** vs pass17 (140): pass18 used stricter `sema_row_ok` (exec∈known set, idx∈known set, tags<16) on full glcore scan — primary table 11 + 1 stray = honest tight count. Pass17 broader descriptor taxonomy remains valid for research; implementers use **11-row formal only**.
- **SASS phase1 incomplete** on cuda/opencl full scan; use pass17 multi-SR windows (`00/01/03/48/49/4c/50`) + pass18 partial gpucomp/ptx/nvvm artifacts.
- **QMD/SPH multi-stage still insufficient** (40/30 weak cands); ELF/reloc reject helps but does not yield shippable layouts.
- **PCAS/QMD method imm present but co-occurrence ~0** within ±2KB for `0x6a04`↔`0x6a08` / `0x02b4`↔`0x6a04` — methods exist in binary but not as static adjacent templates; runtime emitter builds sequences.

---

## 1. Class ladders (pass18 — reconfirmed + vksc-core)

### 1.1 Primary glcore rodata (dword-verified, unchanged)

| Engine | File offset | Ladder (newest→oldest) |
|--------|-------------|------------------------|
| DMA_COPY `*B5` | `0x11bb600` | `CAB5 C9B5 C8B5 C7B5 C6B5 C5B5 C3B5 C1B5 C0B5 B0B5` |
| COMPUTE `*C0` | `0x11bb640` | `CEC0 CDC0 CBC0 C9C0 C7C0 C6C0 C5C0 C3C0 C1C0 C0C0 B1C0 B0C0` |
| 3D `*97` | `0x11bb680` | `CE97 CD97 CB97 C997 C797 C697 C597 C397 C197 C097 B197 B097` |

Secondary DMA @ `0x1236f80` adds trailing **`A0B5`** (11 entries).

### 1.2 GPFIFO / video / aux region @ `0x1238b50` (reconfirmed)

| +off | Content | Classes |
|------|---------|---------|
| `+0x10` (`0x1238b60`) | **GPFIFO `*6F`** | `CA6F C96F C56F C46F C36F C06F B06F A06F A26F 906F` |
| `+0x50` (`0x1238ba0`) | **NVENC `*B7`** | `C5B7 C4B7 B4B7 B6B7 C7B7 C8B7 C9B7 CEB7 CFB7 D1B7` |
| `+0x90` (`0x1238be0`) | **NVDEC `*B0`** | `C4B0 C6B0 C7B0 B8B0 C9B0 CDB0 CEB0 CFB0 D1B0` |
| `+0xd0` (`0x1238c20`) | **AUX `*FA`** | `C6FA C7FA B8FA C9FA CDFA CEFA CFFA D1FA` |

### 1.3 Cross-lib consensus (pass18 expanded)

| Lib | Finding |
|-----|---------|
| **eglcore** | Full mirror of all ladders (DMA/C/3D/GPFIFO/NVENC/NVDEC/FA) — **identical class lists** |
| **vksc-core** | **New pass18:** 8 ladder runs matching glcore/eglcore engine set — Vulkan SC / safety-critical path uses same rodata ladders |
| **cuda / opencl** | GPFIFO only (1 run each) — CUDA/compute subset (`C86F`/`A16F` variant documented pass16/17) |
| **vdpau** | Partial: 3D + NVDEC (×2) + DMA — video path subset |
| **glsi / ml / nvidia_drv** | DMA/3D/GPFIFO fragments only |
| **cuvid / encode / rtcore** | Auto ladder scan **0 runs** (phase1/2) — classes constructed or non-contiguous |

**No DMA/COMPUTE/3D ladder changes required.** Wire GPFIFO/NVDEC/NVENC alts; document vksc-core as full graphics ladder peer; do not ship `*FA` or `506F` without silicon.

JSON: `re_pass18/classes/pass18_all_ladders.json`, `pass18_all_ladders_scan.json`.

---

## 2. Host semaphore — pass18 formal + egl mirror

### 2.1 Structured sema config table @ glcore `0x11e30c0`

Leading dword `0x20`. **Data rows at +0x10, +0x30, … +0x150** (0x20 stride). **Authoritative: 11 rows.** Tail +0x170.. = noise (ASCII/`3f800000` floats) — **do not implement**.

| +off | exec | tag_a | tag_b | sema_idx | aux | Slot |
|------|------|-------|-------|----------|-----|------|
| `+0x10` | `0x1004` | 4 | 2 | `0x12` | 2 | **C** |
| `+0x30` | `0x1002` | 4 | 2 | `0x11` | 2 | **B** |
| `+0x50` | `0x0804` | 4 | 2 | `0x11` | 2 | **B** |
| `+0x70` | `0x0802` | 4 | 2 | `0x10` | 2 | **A** |
| `+0x90` | `0x1004` | 5 | 3 | `0x12` | 3 | **C** |
| `+0xb0` | `0x1002` | 5 | 3 | `0x11` | 3 | **B** |
| `+0xd0` | `0x0804` | 5 | 3 | `0x11` | 3 | **B** |
| `+0xf0` | `0x0802` | 5 | 3 | `0x10` | 3 | **A** |
| `+0x110` | `0x1004` | 0 | 1 | `0x08` | 1 | nonstd |
| `+0x130` | `0x1002` | 0 | 1 | `0x07` | 1 | nonstd |
| `+0x150` | `0x1001` | 0 | 1 | `0x1d` | 1 | **primary nonstd** |

### 2.2 Execute × sema_idx matrix (authoritative only)

| sema_idx | slot | executes |
|----------|------|----------|
| `0x12` | C | `0x1004` |
| `0x11` | B | `0x1002`, `0x0804` |
| `0x10` | A | `0x0802` |
| `0x08` | nonstd | `0x1004` |
| `0x07` | nonstd | `0x1002` |
| `0x1d` | primary nonstd | `0x1001` |

Tag groups: (4,2) and (5,3) for A/B/C ladder; (0,1) for nonstd/`0x1d`.

### 2.3 **eglcore sema mirror (pass18 new — HIGH confidence)**

Full 11-row walk at **`0x114f2f0`** (second candidate `0x114f370` is mid-table row9 start). Dwords **byte-identical** to glcore primary table. EGL/Vulkan-on-EGL paths share sema policy — mesa pass17 default is correct for both GL and EGL stacks.

JSON: `re_pass18/sema/pass18_sema_formal.json`, `pass18_eglcore_sema_mirror_114f2f0.json`, `pass18_sema_clusters.json`.

### 2.4 Sema descriptor clusters (strict)

Tight scan: **12** valid sema-shaped rows in glcore — 11 in page `0x11e3000` (primary) + 1 stray at `0x1119ce0`. Pass17's 140-count used looser descriptor taxonomy; implementers still use only the formal 11-row table.

**cuda sema mirror:** 0 candidates with row0=`0x1004`/idx12 + stride pattern (compute may build sema differently or only at runtime).

---

## 3. MME / unknown methods / emitter distances

### 3.1 Imm method counts (permissive scan; compare pass17 for sanity)

| Method | glcore | eglcore | cuda | Role |
|--------|--------|---------|------|------|
| `0x3400` | 54 | 46 | 100+ | MME_SHADOW_SCRATCH / base |
| `0x34a8` | 16 | 15 | 21 | MME scratch42 (pass15–17 P0) |
| `0x3800` | 59 | 49 | 100+ | MME_DMA_SYSMEMBAR |
| `0x385c` | 7 | 10 | 40 | MME_RAM_ADDR |
| `0x3880` | 100+ | 100+ | 100+ | MME_CONFIG_END (noisy count) |
| `0x3884` | 22 | 20 | 11 | MME_RAM_DATA (**permissive; see §3.3**) |
| `0x3998` | 12 | 7 | 13 | MME unk |
| `0x39e0` | 29 | 28 | 100+ | MME post-config probe |
| `0x15bc` | 14 | 32 | 55 | unk neighbor |
| `0x1618` | 100+ | 100+ | 100+ | unk (very common imm — noisy) |
| `0x1a00`..`0x1a0c` | 15–60 | 6–32 | 21–100+ | unk block |

### 3.2 `0x3800` → `0x39e0` ordered distances (glcore)

| dist (bytes) | count | Notes |
|--------------|-------|-------|
| **360** (`0x168`) | **4** | **Dominant pass18** — refine pass17 72/192/376 set |
| 120 (`0x78`) | 2 | pass17 also saw |
| 256 (`0x100`) | 1 | |
| 376 (`0x178`) | 1 | pass17 |
| 72 (`0x48`) | 1 | pass17 |
| 192 (`0xc0`) | 1 | pass17 |
| 140, 240 | 1 each | |

**Mesa:** emit `0x39e0` as optional post-`0x3800` config; allow variable gap; most common static spacing **360 bytes** of intermediate code/data (not necessarily 360/4 method dwords — x86 distance between imm sites).

### 3.3 `0x34a8` co-occurrence (within ±1KB of imm sites)

Weak spatial coupling in static binary: near `0x3400` 3/16, near `0x3880` 2/16, near `0x385c` 1/16, **0/16** near `0x3800`/`0x39e0`/`0x0110` under this window. Emitters likely interleave via registers/runtime; **order still authoritative from live/golden traces + mesa pass15–17 prime ladder**, not static proximity alone.

### 3.4 RAM_DATA (`0x3884`) — pass18 nuance

Pass17: imm **0** in primary libs (strict). Pass18 permissive: **22/20/11** hits in glcore/eglcore/cuda. **Do not treat as solved.**

**Proxy patterns via `0x385c` sites** (4 glcore sites with ≥8 small following dwords):
- `0x13c2138`: `385c, 0, 00010008, 1e, 3880, …` — looks like addr/config then `0x3880` end, **no clear `0x3884` stream in static window**
- Other sites show x86-ish neighbors (`fe1c34b0` etc.) — likely false structure

**Verdict unchanged:** real MME RAM upload is **runtime-only**; live trace mandatory. Proxy search useful for locating `0x385c`/`0x3880` config bookends only.

### 3.5 Weak MME-ISA (gpucomp heuristic)

30 candidates — entropy/small-dword runs only. **Do not ship.**

JSON: `re_pass18/mme/pass18_mme_deep.json`, `mme_isa/pass18_mme_isa_weak.json`.

---

## 4. PCAS / INLINE_QMD / compute methods

### 4.1 Imm counts (glcore focus; limit-capped at 100 in analyzer)

| Method | glcore | eglcore | cuda | opencl | Notes |
|--------|--------|---------|------|--------|-------|
| `0x6a00` | 100+ | 100+ | 100+ | 100+ | QMD base region |
| `0x6a04` | **10** | **3** | **45** | **10** | COMPUTE_QMD — sparse in graphics libs |
| `0x6a08` | **10** | **11** | **33** | **10** | COMPUTE_PCAS — sparse in graphics |
| `0x6a0c` | 3 | 4 | 45 | 45 | signal/unk |
| `0x6a10` | 47 | 31 | 100+ | 100+ | |
| `0x02b4` | 100+ | 100+ | 100+ | 100+ | INLINE_QMD_DATA (noisy common) |
| `0x02b8`..`0x02d4` | high | high | high | high | INLINE/PCAS family imms abundant but ambiguous |
| `0x0110`/`0x0200`/`0x020c` | 100+ | 100+ | 100+ | 100+ | WFI / host sema (ubiquitous) |

**cuda/opencl** have **richer `0x6a04`/`0x6a08`/`0x6a0c`** than glcore — confirms compute driver paths encode QMD/PCAS methods more explicitly; graphics may funnel through different helpers.

### 4.2 Co-occurrence (glcore, ±2KB) — **weak / critical finding**

| pair | a_hits | near_b |
|------|--------|--------|
| `0x6a04`+`0x6a08` | 10 | **0** |
| `0x6a04`+`0x6a00` | 10 | **0** |
| `0x02b4`+`0x6a04` | 100 | **0** |
| `0x02b4`+`0x02c0` | 100 | **2** |
| `0x6a08`+`0x02c0` | 10 | **0** |
| `0x6a08`+`0x0110` | 10 | **0** |
| `0x6a04`+`0x020c` | 10 | **0** |
| `0x02c0`+`0x02c4` | 100 | **0** |

**Interpretation:** method immediates exist in x86 code but **not as static ordered templates** adjacent in the binary. Mesa G2 bringup cannot derive PCAS/INLINE order from static RE alone — **live capture / silicon** remains the blocker (aligns pass17 P2).

### 4.3 QMD / SPH structure search (stricter)

- QMD weak candidates score≥5: **40** (do not ship)
- SPH weak candidates score≥3: **30** (do not ship)

Keep mesa `nv_qmd.h` / `nv_sph.h` pass15–17 defaults; refine only from live/docs.

JSON: `re_pass18/pcas/pass18_pcas_qmd.json`, `qmd/pass18_qmd_weak.json`, `sph/pass18_sph_weak.json`.

---

## 5. Invalidate / constant-buffer / report sema (pass18 P1 depth)

### 5.1 Invalidate family imm (glcore, non-zero throughout `0x0b00`–`0x0b3c`)

| Method | hits (capped ~50) | Likely role |
|--------|-------------------|-------------|
| `0x0b00` | 50 | INV_SHADER_CACHES (primary) |
| `0x0b04` | 40 | INV_TEX_DATA |
| `0x0b08` | 31 | INV_TEX_HDR |
| `0x0b0c` | 29 | INV_SAMPLER |
| `0x0b10` | 47 | INV_CONSTANT / related |
| `0x0b14`..`0x0b3c` | 17–50 | extended inv/flush family |

**Inv ladder in ±256B of `0x0b00` sites containing `0x0b04`/`0x0b08` imm:** only **4/80** — static adjacency rare; runtime builds multi-inv sequences.

### 5.2 CB bind family imm (`0x1280`–`0x12bc`)

| Method | hits | Likely role |
|--------|------|-------------|
| `0x1280` | 50 | SET_CONSTANT_BUFFER_SELECTOR / bind start |
| `0x1284` | 34 | BIND_GROUP / slot |
| `0x1288` | 16 | BIND offset |
| `0x128c` | 12 | BIND size |
| `0x1290` | 41 | BIND addr lo |
| `0x1294` | 9 | BIND addr hi |
| `0x1298`..`0x12bc` | 6–50 | extended bind/update |

### 5.3 Report sema (3D) vs host sema

| Method | hits | Notes |
|--------|------|-------|
| `0x0d00`..`0x0d04` | 50/50 | SET_REPORT_SEMAPHORE A/B |
| `0x0d08`..`0x0d0c` | 30/20 | C/D |
| `0x0d10`..`0x0d2c` | 13–50 | extended report/query |

Distinct from host sema region `0x0200`/`0x0240`. Mesa should keep **two sema concepts**: host/channel sema (pass17 formal execute modes) vs 3D report sema methods.

### 5.4 Pipeline/shader methods

`0x0540`..`0x055c` all heavily present (SET_PIPELINE_SHADER / program region). `0x0c00`/`0x0c04` shader control / SPH version region present.

### 5.5 G3 ngrams near `0x0b00` (imm co-presence ±1KB)

Mostly lone `0b00` (51×); occasional `0b00+0d00` (6×), rare `0b00+1280+0540`, `0b00+020c`, `0b00+1280+0d00`. Suggests invalidate often emitted **alone or with report sema**, not always bundled with CB/host sema in static windows.

JSON: `re_pass18/invalidate/pass18_inv_cb.json`, `cb/pass18_cb_inv.json`.

---

## 6. Channel / G0–G4 pipeline signatures (pass18 core deliverable)

Imm-presence completeness (permissive; useful as **upper bound** on what methods exist in driver, not emit order):

| Pipeline | Score | Status |
|----------|-------|--------|
| G1_CE_minimal (`0300,0400,0200,020c`) | 4/4 | COMPLETE_IMM |
| G1_CE_with_WFI (+`0110`) | 5/5 | COMPLETE_IMM |
| G2_QMD_PCAS (`6a04,6a08,020c`) | 3/3 | COMPLETE_IMM |
| G2_inline_QMD (`02b4,6a04,6a08`) | 3/3 | COMPLETE_IMM |
| G3_MME_prime (`3400,34a8,3800,39e0`) | 4/4 | COMPLETE_IMM |
| G3_MME_full_static (+`385c,3880`) | 6/6 | COMPLETE_IMM |
| G3_MME_with_RAM (+`3884`) | 7/7 | COMPLETE_IMM (**imm only; payload live**) |
| G3_inv_cb (`0b00,0b04,1280,1284`) | 4/4 | COMPLETE_IMM |
| G3_clear_then_host (`0b00,0110,0200,020c`) | 4/4 | COMPLETE_IMM |
| G3_report_sema (`0d00..0d0c`) | 4/4 | COMPLETE_IMM |
| G4_video_host (`0200,020c,0110`) | 3/3 | COMPLETE_IMM |

**Family coverage:** G0 4/4, G1 11/11, G2 11/11, G3 28/28, G4 7/7 methods with ≥1 imm hit.

### 6.1 Top method pair ordered distances (glcore sample)

| pair | best_dist | total_ordered |
|------|-----------|---------------|
| `0x0200`→`0x0400` | 384 | 15 |
| `0x0300`→`0x0400` | 192 | 9 |
| `0x0400`→`0x0540` | 240 | 9 |
| `0x0200`→`0x0300` | 192 | 8 |
| `0x3800`→`0x39e0` | **360** | 7 |
| `0x0b00`→`0x0d00` | 384 | 5 |
| `0x0b00`→`0x1280` | 1440 | 4 |
| `0x0110`→`0x0200` | 360 | 3 |

CE path: sema/host → LAUNCH_DMA → LINE_LENGTH distances cluster **192–384**. MME: **360** for sysmembar→post_probe. Inv→report **384**.

JSON: `re_pass18/channel/pass18_channel.json`, `g2/pass18_g2.json`, `g3/pass18_g3.json`, `g4/pass18_g4.json`, `ngrams/pass18_ngrams.json`.

---

## 7. SASS / S2R (pass18 partial + pass17 carry-forward)

Phase1 SASS full scan completed gpucomp/ptx/nvvm partially (~265 sass artifacts) before cuda/opencl timeout; markers backfilled.

**Mesa multi-SR targets (unchanged, pass17 authoritative directional set):**
`SR0 (0x00)`, `SR1 (0x01)`, `SR3 (0x03)`, `SR48 (0x48 clock)`, `SR49 (0x49)`, `SR4c (0x4c)`, `SR50 (0x50)`.

Use **multi-SR window co-presence**, not absolute SR byte histograms (byte-frequency biased in any large binary).

JSON/notes: `re_pass18/sass/`, `notes/pass18_sass_deep.md`; supplement with `re_pass17/sass/`.

---

## 8. SPA / CE / video (summary)

- **CE methods** (`0x0300`/`0x0400` family): fully represented in imm matrix; pair distances support G1 minimal/WFI pipelines.
- **SPA/3D methods** (`0x0500`–`0x05xx`, `0x0c00`, `0x2000` range): many non-zero imm entries (see `spa/pass18_spa.json` top-50) — useful for gap-audit vs `nv_3d_methods.h` / class headers, not as ordered emit scripts.
- **Video libs:** vdpau/cuvid/encode carry host sema + common video method imms; class ladders only in vdpau (partial) + glcore/eglcore primary for NVDEC/NVENC IDs.

---

## 9. NVOS32 / ioctl / string markers

Cross-lib string hits for `NVOS32`, `RmAlloc`/`RmControl`, `GPFIFO`, class tokens (`NVC*6F`, `NVC*97`, …), `QMD`, `PCAS`, `Semaphore`, `Invalidate`, `ConstantBuffer` — concentrated in glcore/eglcore/cuda/gpucomp/opencl/vdpau/cuvid as expected. No new NVOS number table extracted beyond confirming RM/ioctl surface area is string-rich in userspace (implementation remains mesa `nv_rm` + libdrm_nvidia).

JSON: `re_pass18/nvos32/pass18_nvos32.json`, `ioctl/pass18_ioctl_markers.json`.

---

## 10. Pass17 → pass18 delta

| Area | pass17 | pass18 | Action |
|------|--------|--------|--------|
| Sema formal 11 rows | authoritative | **reconfirmed** + **egl mirror `0x114f2f0`** | no mesa table change; confidence ↑ for EGL |
| Sema clusters | 140 loose | 12 strict | implement 11 only |
| Class ladders | glcore/egl/cuda/vdpau | +**vksc-core** full, nvidia_drv partial | document vksc peer |
| MME `0x34a8`/`0x39e0` | imm present | imm present; **dist 360 dominant** | refine post-config spacing note |
| RAM_DATA `0x3884` | imm 0 strict | imm 22 permissive + proxies | **still live-trace**; don't over-trust permissive |
| PCAS/INLINE | weak | imm present, **cooc ~0** | confirms live-only emit order |
| Inv/CB/report | pass17 light | **full imm ladders** | **P1 wire in mesa** |
| G0–G4 pipelines | ngrams | **completeness matrix** | tick149+ audit vs emitters |
| QMD/SPH static | weak | weak (40/30) | no ship |
| SASS multi-SR | pass17 full | pass18 partial | keep pass17 set |

---

## 11. Mesa priorities (refined for pass19 / silicon / live trace)

### P0 — wire or confirm now (static evidence strong)

1. **Host sema pass17 formal (11 rows)** — pass18 reconfirms + egl mirror. Default emit mode pass17; ladder pass17→classic→slot. (ticks 146–148)
2. **Class ladders** — DMA/COMPUTE/3D/GPFIFO/NVENC/NVDEC primary + A0B5 secondary; cuda/opencl GPFIFO alt non-graphics only.
3. **MME `0x34a8`** — imm present; G3 prime path (scratch42).
4. **MME `0x39e0`** — imm present; optional post-`0x3800`; prefer allowing ~360B-scale emitter gap (plus 72/120/192/256/376).
5. **WFI (`0x0110`) + sema** — G3 clear_then_host; channel/Vulkan submit ladders.
6. **Multi-SR S2R/SPH** — `00/01/03/48/49/4c/50` directional in shader/SPH emitters.

### P1 — implement with pass18 evidence, verify on silicon

1. **Invalidate ladder** — at minimum `0x0b00`; add `0x0b04`/`0x0b08`/`0x0b0c`/`0x0b10` if class docs agree; don't require static adjacency.
2. **CB bind family** — `0x1280`/`0x1284`/`0x1288`/`0x128c`/`0x1290`/`0x1294` in 3D/context bind path.
3. **Report sema `0x0d00`..** — 3D path; separate from host sema `0x0200` region.
4. **G1 CE** — LAUNCH_DMA/LINE_LENGTH + pass17 host sema (partially wired).
5. **G2 QMD defaults pass15–17** — keep; document PCAS/INLINE as live-trace blockers (imm exists, order doesn't).
6. **GPFIFO/NVDEC/NVENC alts** — ensure mesa class tables include §1.2 lists.
7. **G3 selftest buffers** — pass17 MME prime + sema needs ≥1024–2048 dwords (t148 assert).

### P2 — static weak; live trace / silicon required

1. **MME RAM_DATA (`0x3884`) real payloads** — permissive imm ≠ solved; runtime upload stream.
2. **Real MME microcode** — not reconstructible from static imm.
3. **QMD/SPH static candidates** — do not replace `nv_qmd.h` / `nv_sph.h`.
4. **PCAS / INLINE_QMD emit order** — co-occurrence failure; need capture.
5. **AUX `*FA` / `506F`** — do not ship without silicon.
6. **MME ISA weak candidates** — research only.

### P3 — research / pass19 angles

1. Full x86 emitter CFG from sema/MME/inv imm sites (objdump + xrefs; multi-hour dedicated pass).
2. Walk all eglcore sema mirror rows vs glcore for any delta (pass18: identical at `0x114f2f0`).
3. Video G4 method completeness vs vdpau/cuvid/encode imm matrices + live bitstream.
4. Cross-check open-gpu-doc class headers vs pass18 SPA imm families (`spa/pass18_spa.json`).
5. Live G1–G4 sema pass17 vs classic payload/execute on real HW.
6. Re-run optimized SASS scanner (stride/SIMD) on cuda/opencl to complete pass18 partial.

### Confidence matrix

| Area | Confidence | Action |
|------|------------|--------|
| Sema 11-row formal + egl mirror | **HIGH** | ship (done pass17/t146–148) |
| Class ladders primary + vksc peer | **HIGH** | ship/confirm |
| MME `0x34a8` / `0x39e0` / `0x3800` | **MED-HIGH** | ship prime path |
| Inv/CB/report imm families | **MED** | wire + silicon |
| G0–G4 method existence | **MED** | audit emitters; order live |
| Multi-SR SPH | **MED** | wire directional |
| PCAS/INLINE imm existence | **MED** | methods exist |
| PCAS/INLINE emit order | **LOW** | live trace |
| QMD/SPH static structs | **LOW** | keep mesa defs |
| RAM_DATA payloads | **VERY LOW static** | live trace |
| MME ISA | **VERY LOW** | research only |

### Tick suggestions (mesa/libdrm only)

- **t149:** wire pass18 inv/CB/report gaps vs `nv_3d_methods.h` / `nvgpu_context.c`; enlarge G3 selftest buffers; finish t148 if still open.
- **t150:** G2 bringup — document PCAS/INLINE as HW-capture blockers; tighten QMD sema pass17 defaults only; no fake static QMD blobs.
- **t151+:** live trace harness hooks (if HW available) for `0x3884`/PCAS/INLINE; pass19 x86 emitter CFG.

---

## 12. Phase1 imm site index (16 libs) — scale

| Lib | imm hits (all focus methods) | methods with hits |
|-----|------------------------------|-------------------|
| glcore | 2604 | 80 |
| eglcore | 2497 | 80 |
| cuda | 3018 | 80 |
| gpucomp | 2980 | 80 |
| opencl | 2950 | 80 |
| nvvm | 3070 | 80 |
| ptx | 2469 | 80 |
| rtcore | 2121 | 80 |
| cuvid | 2113 | 77 |
| ml | 707 | 76 |
| glx | 464 | 68 |
| cfg | 391 | 53 |
| vdpau | 359 | 51 |
| glsi | 347 | 55 |
| fbc | 151 | 36 |
| encode | 118 | 37 |

Site lists: `re_pass18/x86_callers/meth_sites_*.txt`, `meth_counts_*.json`, `meth_counts_all_libs.json`.

---

## 13. Trust order & honest blockers (executive)

**Ship from static RE (with silicon sanity):**
- Sema 11-row formal (glcore `0x11e30c0` = eglcore `0x114f2f0`)
- Class ladders §1
- MME prime `0x3400`/`0x34a8`/`0x3800`/`0x39e0` (+ optional `0x385c`/`0x3880` bookends)
- Inv/CB/report method families as API surface
- Multi-SR SPH directional set
- G1/G3/G4 host sema + WFI patterns (pass17 emit modes)

**Blocked on live trace / HW:**
- MME `0x3884` RAM payload bytes
- PCAS / INLINE_QMD ordering and QMD memory layout in flight
- Real SPH/QMD binary structures beyond mesa defaults
- Exact inv multi-method sequences per class
- Sema pass17 vs classic on silicon (payload/execute observed)

**Do not implement from pass18 weak hits alone:**
- QMD/SPH static candidates (40/30)
- MME ISA entropy blobs (30)
- AUX `*FA` / stray `506F`
- Permissive `0x3884` imm as proof of static upload templates

---

## 14. Artifact index

| Path | Content |
|------|---------|
| `re_pass18/notes/pass18_*.md` | Focus notes (12 files) |
| `re_pass18/sema/` | Formal JSON, egl mirror, clusters |
| `re_pass18/mme/`, `mme_isa/` | MME imm/distances/proxies, weak ISA |
| `re_pass18/pcas/`, `qmd/`, `sph/` | Compute/PCAS/QMD/SPH |
| `re_pass18/invalidate/`, `cb/` | Inv/CB/report |
| `re_pass18/channel/`, `g2/`–`g4/`, `ngrams/` | Pipeline signatures |
| `re_pass18/classes/` | Ladders + 9-lib scan |
| `re_pass18/x86_callers/` | 16-lib imm sites/counts |
| `re_pass18/disasm/` | xxd/hex/dword dumps at anchors |
| `re_pass18/sass/` | Partial SASS/S2R |
| `re_pass18/strings/` | HW-filtered strings |
| `re_pass18/tables/` | Per-lib mine JSON + cross summary |
| `re_pass18/delta_p17/`, `mesa_gap/` | Delta + priorities JSON |
| `re_pass18/pass18_*.py`, `pass18_*.sh` | Reproducible pipeline |

**Reproduce:**
```bash
bash /tmp/nvidia-reveng-pp-v2/re_pass18/pass18_master.sh
# or stepwise:
bash /tmp/nvidia-reveng-pp-v2/re_pass18/pass18_targeted_disasm.sh
python3 /tmp/nvidia-reveng-pp-v2/re_pass18/pass18_deep_analysis.py
python3 /tmp/nvidia-reveng-pp-v2/re_pass18/pass18_supplement.py
python3 /tmp/nvidia-reveng-pp-v2/re_pass18/pass18_mine.py
```

---

*Pass18 closes the pass11–18 static RE arc on 610.43.02 with pipeline completeness + egl sema mirror as primary new value; pass19 should prioritize x86 emitter CFG and/or live trace over another full imm recount.*
