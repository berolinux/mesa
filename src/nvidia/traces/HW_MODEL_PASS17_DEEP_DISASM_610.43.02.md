# HW Model Pass 17 — Method Descriptors, Formal Sema, Cross-Lib Consensus, MME/SASS/QMD Depth
# (NVIDIA-Linux-x86_64-610.43.02)

**Date:** 2026-06-25  
**Duration:** multi-hour session (targeted disasm ~5980 artifacts; focus analysis ~163s; supplement probes; full miner 22 libs ~146s; total pipeline ~8 min wall + synthesis)  
**Scope:** Beyond pass16 (corrected sema alignment, strict QMD/SPH weak verdict, MME imm-site catalog, S2R multi-SR, channel ngrams, cross-lib ladders) — **formal sema JSON with execute×idx matrix + extra descriptor clusters**, **full cross-lib class ladder re-scan** (glcore/eglcore/cuda/opencl/vdpau + cuvid/encode/rtcore negative), **MME imm/cooc/coupling/RAM-chain metrics + weak ISA entropy cands**, **QMD/SPH multi-stage refine (still weak; honest)**, **SASS/S2R SR histogram + multi-SR windows + opcode families on gpucomp**, **channel ngrams + named pipeline signatures**, **SPA/CE/video imm tables**, **22-lib miner cross-summary**, **mesa P0–P3 refined for pass18/silicon/live trace**.  
**Artifacts:** `/tmp/nvidia-reveng-pp-v2/re_pass17/` (`pass17_mine.py`, `pass17_deep_analysis.py`, `pass17_supplement.py`, `pass17_targeted_disasm.sh`, `notes/`, `disasm/`+`x86_callers/`+`sass/` ~6000+ files, `tables/`, `sema/`, `qmd/`, `sph/`, `mme/`, `mme_isa/`, `classes/`, `ngrams/`, …)  
**Mesa impact (recommended this pass):** Confirm/wire remaining pass15–17 P0 (class alts, sema formal modes, `0x34a8`, multi-SR S2R/SPH, sema+WFI, inv/CB); treat `0x39e0` as optional MME post-config (coupled to `0x3800` at dists 72/192/376 in static windows); do **not** promote pass17 QMD/SPH/MME-ISA static candidates; live trace still required for RAM_DATA + real QMD/SPH/PCAS.

---

## Method

1. **Phase 1 targeted dumps** (`pass17_targeted_disasm.sh`) — ELF sections/dynamic; class/sema/GPFIFO/video anchors (glcore+eglcore+cuda); sema neighborhood; method imm site index + window bins (glcore/eglcore/cuda, 910/910/1257 hits); rodata scan slabs; gpucomp SASS/S2R windows (360 hits); strings filters; objdump attempts → `disasm/` + `x86_callers/` + `sass/` (~5980 files phase1; ~6324 total pass17 tree).
2. **Phase 2 focus analysis** (`pass17_deep_analysis.py`, ~163s) — sema formal, classes cross-lib, MME/unknown, QMD/SPH multi-stage, SASS/S2R, sequences/ngrams, SPA/CE/video, mesa priorities, executive summary → `notes/pass17_*.md`.
3. **Phase 3 supplemental probes** (`pass17_supplement.py`) — exact header dword search (0x2001/0x2000 = 0 for all key methods), imm site contexts, 0x3800→0x39e0 ordered distances, eglcore sema mirror, inv/CB imms, string marker contexts, sema integrity check, header-shaped noise estimate.
4. **Phase 4 full mine** (`pass17_mine.py`, ~146s) — 22 priority libs, per-lib JSON + `tables/pass17_crosslib_summary.json`.
5. **Manual synthesis** — cross-check pass11–16, refine mesa action list and confidence matrix.

**Reconfirmed limitation:** Pushbuffer method headers are almost always **runtime-constructed**. Trust order: (a) imm method offsets/indices, (b) rodata class ID arrays, (c) rodata sema config tables, (d) tight method n-grams (SET_OBJECT=0 filtered), (e) heuristic gpucomp SASS/S2R, (f) weak QMD/SPH/MME-ISA candidates (do not ship), (g) open-gpu-doc / OGKM headers.

**Pass17 refinements on limitations:**
- **Sema table tail bleed:** scanning 16 rows past valid data picks up non-sema dwords at +0x170..+0x1f0 (ASCII/binary noise). **Authoritative rows remain 11** (pass16): +0x10 through +0x150 only. Pass17 formal JSON includes tail for transparency; implementers must stop at `0x1d` primary_nonstd row.
- **QMD/SPH multi-stage still insufficient:** score≥6/7 hits include ELF/reloc/x86 bleed (`48 89` rex patterns explicitly seen in cuda refine). **Do not** replace mesa `nv_qmd.h` / `nv_sph.h`.
- **`0x2001xxxx` exact header scan = 0** on glcore/eglcore/cuda for all key methods (supplement confirms pass16). Imm method-offset hits remain reliable.
- **RAM_DATA (`0x3884`)** imm still **0** in glcore/eglcore/cuda; miner shows sparse hits only in gpucomp/opencl/ptx/glsi/vdpau/nvoptix/nvidia_drv (likely non-method noise). Treat as **runtime-only** until live trace.
- **S2R SR histogram is byte-frequency biased** (SR0/0x48 dominate any binary); use only as directional ranking with multi-SR window co-presence, not absolute counts.

---

## 1. Class ladders (pass17 — reconfirmed + expanded cross-lib)

### 1.1 Primary glcore rodata (unchanged, dword-verified)

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

### 1.3 Cross-lib consensus (pass17 expanded)

| Lib | Finding |
|-----|---------|
| **eglcore** | Full mirror of all ladders (DMA/C/3D/GPFIFO/NVENC/NVDEC/FA) at different rodata bases — **identical class lists** to glcore |
| **cuda** | GPFIFO only: `C96F C86F C56F C46F C36F C06F B06F A16F` @ `0x67c5180` — **missing CA6F head**, has **C86F** and **A16F** not in glcore; CUDA/compute subset |
| **opencl** | Same as cuda GPFIFO variant @ `0x638f3a0` — confirms CUDA/OpenCL share alt ladder, not graphics primary |
| **vdpau** | Partial: 3D subset (`C597..C997`), NVDEC (2 copies, missing `CEB0` vs glcore), DMA head (`CAB5..C5B5`) — video path uses dec/dma subset |
| **cuvid / encode / rtcore** | Auto ladder scan **0 runs** — classes constructed or non-contiguous; rely on glcore/eglcore rodata + vdpau partial |

**No DMA/COMPUTE/3D ladder changes required.** Mesa should ensure GPFIFO/NVDEC/NVENC alts from §1.2 are wired; document cuda/opencl `C86F`/`A16F` as non-graphics; do not ship `*FA` or `506F` without silicon.

JSON: `re_pass17/classes/pass17_all_ladders.json`, per-lib `classes/*.json`.

---

## 2. Host semaphore — pass17 formal (authoritative 11 rows)

### 2.1 Structured sema config table @ glcore `0x11e30c0`

Leading dword `0x20`. **Data rows at +0x10, +0x30, … +0x150** (0x20 stride), fields:

| dword | meaning |
|-------|---------|
| 0 | execute imm (`0x1004`, `0x1002`, `0x0804`, `0x0802`, `0x1001`) |
| 1 | tag_a (4/5/0) |
| 2 | tag_b (2/3/1) |
| 3 | **sema_idx** (0x12/0x11/0x10/0x08/0x07/0x1d) |
| 4 | aux (often equals tag_b) |
| 5–7 | pad zeros |

### 2.2 Authoritative table (11 rows — stop before tail noise)

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

**Do not implement rows at +0x170..+0x1f0** (pass17 overscan noise: `0x31233023` etc.).

JSON: `re_pass17/sema/pass17_sema_formal.json` (trim to 11 rows for mesa; full file has overscan).

### 2.3 Execute × sema_idx matrix (authoritative only)

| sema_idx | slot | executes |
|----------|------|----------|
| `0x12` | C | `0x1004` |
| `0x11` | B | `0x1002`, `0x0804` |
| `0x10` | A | `0x0802` |
| `0x08` | nonstd | `0x1004` |
| `0x07` | nonstd | `0x1002` |
| `0x1d` | primary nonstd | `0x1001` |

Tag groups: (4,2) and (5,3) for A/B/C ladder; (0,1) for nonstd/`0x1d`.

### 2.4 Method-descriptor taxonomy (pass17 new depth)

Global aligned row-shaped sema descriptors (exec∈SEMA_EXECS, sema_idx∈{0x10,0x11,0x12,0x1d,0x07,0x08}, tags <16): **140** in glcore.

Top 4K clusters: `0x11e3000` (11, primary table), `0x11c7000` (8), `0x20fb000` (7), `0x11b2000`/`0x123e000`/`0x1251000` (6 each).

**129 extra** descriptors outside primary table saved to `sema/pass17_sema_desc_extra.json` — likely engine-specific copies or method-descriptor records with class_tag adjacent; map incrementally on silicon, not wholesale.

### 2.5 Cross-lib sema

| Lib | Finding |
|-----|---------|
| **eglcore** | Mirror first row `0x1004,4,2,0x12` @ `0x114f2f0`; full row dump in `sema/pass17_eglcore_sema_rows.json` |
| **cuda** | Exact first-row pattern **not found** (sema programming differs / runtime-only) |
| **miner sema_rows** | glcore 140, eglcore 127, cuda 573, gpucomp 655, cuvid 69, opencl high — cuda/gpucomp inflated by weak filter (noise), not additional formal tables |

---

## 3. MME + unknown methods (pass17 imm/cooc/coupling depth)

### 3.1 Imm method offset counts (key set)

| method | glcore | egl | cuda | hdr2001 | notes |
|--------|--------|-----|------|---------|-------|
| `0x34a8` | **36** | 10 | 11 | 0 | scratch42 / pass15–17 P0 |
| `0x3800` | 2348 | 2371 | 112141 | 0 | MME_DMA_SYSMEMBAR (cuda noisy) |
| `0x3804` | 552 | 552 | 563 | 0 | MME neighbor |
| `0x385c` | 1 | 1 | 22 | 0 | RAM_ADDR rare static |
| `0x3860` | 15 | 10 | 85 | 0 | |
| `0x3880` | 28 | 19 | 152 | 0 | config/end region |
| `0x3884` | **0** | **0** | **0** | 0 | **RAM_DATA runtime-only** |
| `0x3888` | 10 | 5 | 9 | 0 | |
| `0x388c` | 1 | 2 | 19 | 0 | |
| `0x39e0` | **16** | 11 | 46 | 0 | MME post-config probe |
| `0x3998` | 9 | 2 | 15 | 0 | unknown/MME-adj |
| `0x1618` | 32 | 28 | 102 | 0 | unknown P1 |
| `0x1a00` | 658 | 618 | 6624 | 0 | unknown (noisy in cuda) |
| `0x0b00` | 1906 | 1871 | 12397 | 0 | invalidate / shader caches |
| `0x0110` | 4625 | 4383 | 11271 | 0 | WFI (imm count high = noise risk) |

Cross-lib miner `0x34a8`: glcore 36, egl 10, cuda 11, cuvid 8, GLX 8, nvidia_drv 9, nvvm 8 — **graphics/video path present, not universal**.

Cross-lib miner `0x3884`: **0** in glcore/eglcore/cuda; sparse 1–4 elsewhere — **do not treat as method programming evidence**.

Cross-lib miner `0x39e0`: glcore 16, egl 11, cuda 46, opencl 40, cuvid 17, nvoptix 19 — broader than `0x34a8`; still treat as optional post-MME.

### 3.2 0x3800 ↔ 0x39e0 coupling

- Pairs within 512B (unordered): **5**
- Ordered (39e0 after 3800, ≤1KB): supplement histogram (dists present)
- Dist min/med/max (unordered pairs): **72 / 192 / 376**
- Methods within ±256B of `0x3800` imm (sampled): `0x39e0` in 3/24 sites, `0x388c` in 1/24

**Interpretation:** static coupling exists but is **sparse** (most `0x3800` sites are not near `0x39e0`). Mesa optional probe after MME config is justified; not required every path.

### 3.3 RAM_DATA sequential programming

Sites with ≥3 of RAM chain (`0x385c/3860/3880/3884/3888/388c`) within 512B of `0x385c`: **0** in glcore.

Upload path is **runtime method-index programmed**; exact `0x3884` imm absent from static image.

### 3.4 MME ISA entropy candidates

40 weak candidates (stride 64, mid entropy, reject pointer-heavy). Early offsets (`0x100`..`0x3400`) dominated by ELF/reloc-like structures (`6474e55x` markers, addr/0 alternating). **Do not promote to real MME ISA.** Saved bins: `mme_isa/cand_*.bin`.

### 3.5 Imm site contexts (supplement highlights)

- **`0x34a8`**: appears in x86 compare/load immediates (`c085`/`74` branches) and rodata method tables (`0x2766e04` region with neighboring `0x34a0/0x34b0/0x34d4/0x34f0`) — **both code-imm and table-imm**, implement as method offset.
- **`0x39e0`**: x86 imm compares (`ff814100 000039e0`) plus sequential method lists (`0x11b855c` method ramp `0x3968..0x3a8c`) — **table-listed method**, not only code noise.
- **`0x1618`**: code imm + method ramp at `0x12579e0` — real method index in tables.
- **`0x3884`**: no imm sites in primary scan (consistent with 0 count).

Exact `0x2001xxxx` headers: **0** for all probed methods in glcore/eglcore/cuda.

JSON: `mme/pass17_mme_site_index.json`, `mme/pass17_mme_cooc.json`.

---

## 4. SASS / SPH / QMD (pass17 honest assessment)

### 4.1 QMD/SPH multi-stage verdict: still weak

| Lib | QMD score≥6 (capped 80) | SPH score≥5 (capped 80) | Notes |
|-----|-------------------------|-------------------------|-------|
| glcore | 80 | 80 | early hits = ELF/reloc (`025f89xx`, `6474e55x`) |
| cuda | 80 | 80 | refine shows x86 `48 89` / `c085` bleed |
| opencl | 80 | 80 | mirrors cuda structure |
| ptx | 80 | 80 | same weak pattern |

Refine pass (score≥7, ±4K around first hits): produced additional candidates but **including obvious x86 code** in cuda (`0x22b240`, `0x236ac0`). Ptr-table reject works for some shapes but not enough.

**Verdict:** Do **not** replace mesa QMD/SPH builders. Live trace of first compute launch remains pass18 P0 for RE.

Artifacts: `qmd/pass17_qmd_hits.json`, `sph/pass17_sph_hits.json`, `qmd/*_sc*.bin` (directional only).

### 4.2 S2R SR priorities (pass17 refined ranking)

Byte-frequency in gpucomp (filtered windows; **directional only**, SR0 over-counts):

| SR | pass17 hits (approx rank) | mesa priority | guess |
|----|---------------------------|---------------|-------|
| `0x00` | #1 dominant | **P0 smoke** | thread/lane id |
| `0x48` | #2 | **P0 smoke** | pass16 dominant alt |
| `0x01` | #3 | P1 | cta/warp dim |
| `0x4c` | #4 | P1 | pass16 secondary |
| `0x02` | #5 | P2 | maybe |
| `0x49` | #6 | P1 | pass16 secondary |
| `0x03` | #7 | P1 | grid/block |
| `0x20` | mid | P2 | clock? |
| `0x50` | mid | P2 | smem? |

**Multi-SR windows** (≥3 target SRs in 128B): 40 captured — common clusters include `{0x00,0x01,0x02,0x04}`, `{0x03,0x49,0x4c}`, `{0x30,0x48,0x50}`, `{0x48,0x50,0x60}`. Supports mesa **multi-SR SPH/SASS smoke** (pass16 7-SR layout) as reasonable coverage of dominant families.

Opcode family heuristic (gpucomp dwords): `lo_hi` huge, `op_50` ~89k, `op_38` ~62k, `op_91` ~18k (S2R-ish), `op_e3` ~14k (EXIT-ish), `ctrl_5c` ~1.3k. Directional only.

ptxjitcompiler ~37 MB, nvvm ~78 MB — SASS primarily in gpucomp (~111 MB).

JSON: `sass/pass17_sr_hist.json`, bins `sass/s2r_sr*_*.bin`.

### 4.3 SPH multi-SR (mesa carry-forward)

Pass16/17: implement multi-SR SPH with at least SR `0x00, 0x48, 0x01, 0x03, 0x49, 0x4c` (+ one more for 7-SR layout). Validate on silicon; static RE cannot prove SPH field encoding.

---

## 5. Channel sequences / ngrams (pass17 quantified)

### 5.1 Key imm hit counts (glcore, method-key set)

Dominant: `0x0000` (SET_OBJECT, 981k — filter out), `0x0040` (25k), `0x0100` (11k), `0x0200`/`0x0400` (~7k each).

MME/unknown in key set (tighter than global imm count): `0x3800` 24, `0x39e0` 9, `0x34a8` 8, `0x1618` 6, `0x385c` 1, `0x3884` 0.

### 5.2 Top structural patterns (SET_OBJECT-lead filtered)

**Bigrams:** `0x40→0x200`, `0x100→0x40`, `0x40→0x100`, `0x200→0x100`, `0x400→0x40`, `0x300→0x400` (CE launch pair).

**Trigrams:** `0x40→0x200→0x100` (4865), `0x40→0x100→0x300` (3640), `0x400→0x40→0x200` (2251).

**4-grams:** `0x40→0x100→0x300→0x400` (2912) — **setup/CE pipeline signature**; `0x400→0x40→0x200→0x100` (2250).

### 5.3 Named pipeline signatures (complete windows, all methods present)

Defined in analysis: MME_upload (`3800+385c+3880+39e0`), MME_scratch (`34a8+3800`), sema_host (`200/204/208/20c`), invalidate (`0b00+0700`), WFI_sema (`110+200`), CE_launch (`400+300`), unknown_1618 (`1618+1a00`).

Counts written in `notes/pass17_sequences_deep.md` pipeline section (run-specific; typically low for full MME_upload due to `0x385c` rarity and `0x3884` absence).

**Mesa implication:** channel_prep should emit **partial MME sequences** (3800/3880/34a8/39e0 optional) without requiring full RAM chain static evidence.

JSON: `ngrams/pass17_ngrams.json`.

---

## 6. SPA / CE / video (pass17)

### 6.1 SPA / setup imm (glcore, count 1..5000 filter)

Wide method ramp in `0x200`..`0x2fc` and `0x800`..`0x8fc` with varying counts; many mid-range counts plausibly real methods, high counts (`>1000`) treat as maybe_noise. Full table: `spa/pass17_spa_imm.json`.

Notable from method set: `0x0b00` invalidate strong in all primary libs; `0x1618`/`0x1a00` present; `0x2600` in sequences (442 key-set hits).

### 6.2 CE methods

`0x300`/`0x400` heavily represented (CE launch/line-length families); sema addr region `0x200`..`0x20c` also high (shared with host sema — context-dependent).

### 6.3 Video class ID presence

glcore/eglcore: full NVDEC/NVENC/FA ladders in rodata (imm class ID counts secondary).  
vdpau: NVDEC ladder present (2 copies), partial DMA/3D.  
cuvid/encode: no contiguous class runs in auto-scan; imm class presence varies.

**Mesa:** wire glcore/eglcore video ladders; vdpau confirms NVDEC subset usable for decode smoke; do not block on cuvid/encode static ladders.

---

## 7. Mesa implementation priorities (pass17)

### P0 — implement / verify soon (carry pass15–16 + pass17 confirm)

1. **GPFIFO / NVDEC / NVENC class alts** from §1.2 (eglcore mirror confirmed; vdpau partial NVDEC)
2. **Host sema formal modes** — 11-row table; slots A/B/C + `0x1d` primary nonstd; execute ladder; **ignore +0x170 tail**
3. **MME `0x34a8` scratch42** — 36 glcore imm, rodata method tables, code compares; implement in channel_prime / MME path
4. **MME `0x39e0` post-config** — optional after `0x3800` (coupled sparsely); behind flag / pass16 probe API
5. **Multi-SR S2R/SPH smoke** — SR `0x00`, `0x48` dominant; also `0x01`, `0x03`, `0x49`, `0x4c` (+ pass16 7-SR layout)
6. **Sema + WFI** coherence in G1/G2/G3 channel prep
7. **Invalidate / CB** `0x0b00` / `0x0700` / `0x2600` in channel sequences where appropriate
8. **CE pipeline ngram** `0x40→0x100→0x300→0x400` / `0x300→0x400` as smoke ordering hint

### P1 — strengthen emitters

1. **QMD pass16/17 defaults** — sema_release0, invalidate, encode_full; **not** static scan candidates
2. **SPH multi-SR header** — validate field layout on silicon
3. **CE small-imm filter** — reject huge LAUNCH_DMA execute imms from static noise
4. **CUDA/OpenCL GPFIFO variant** — document `C86F`/`A16F` as compute-only subset
5. **Method-descriptor extra clusters** — `sema/pass17_sema_desc_extra.json` incremental class_tag map on silicon
6. **vdpau NVDEC subset** — optional decode class fallback list

### P2 — needs live trace / pass18

1. **MME RAM_DATA (`0x3884`) real stream** — imm 0 in primary libs; capture proprietary upload
2. **Real QMD/SPH/PCAS blobs** from first compute launch — train matchers; pass17 refine insufficient
3. **MME real ISA** — entropy candidates weak/ELF-bleed; END-only stubs remain
4. **SPA exact method families** vs open-gpu-doc — imm tables directional
5. **Video e2e** — cuvid/encode ladders still sparse outside glcore/eglcore/vdpau
6. **Extra sema descriptor class_tag → engine** full map

### P3 — long pole

1. NIR → SASS real compiler (gpucomp RE directional only)
2. Full Gallium/Vulkan beyond smoke
3. Video encode/decode bitstreams
4. RT / mesh / advanced 3D

### Explicit non-goals (reconfirmed pass17)

- Do **not** replace mesa QMD/SPH with pass17 static candidates (x86/reloc bleed confirmed)
- Do **not** CALL stub END-only MME macros as if real ISA
- Do **not** add `506F` or `*FA` to production class alts without silicon
- Do **not** implement sema rows past +0x150 (tail noise)
- Do **not** trust global sema scans over formal table @ `0x11e30c0`
- Do **not** trust `0x3884` imm hits outside glcore/eglcore/cuda as RAM_DATA evidence
- Do **not** treat cuda/opencl GPFIFO ladder as graphics primary
- Do **not** modify nouveau/nvk without double-check
- Do **not** trust S2R absolute hit counts (byte-frequency bias)

---

## 8. Confidence matrix (pass11 → pass17)

| Area | pass11 | pass12 | pass13 | pass14 | pass15 | pass16 | pass17 |
|------|--------|--------|--------|--------|--------|--------|--------|
| Class ladders DMA/C/3D | med | high | high | high | high | high | **high** |
| GPFIFO ladder | low | med | med | med | high | high | **high** (egl mirror) |
| NVDEC/NVENC ladders | low | med | med | med+ | high | high | **high** (+vdpau partial) |
| AUX *FA | — | — | — | note | documented | documented | **documented** |
| CUDA/OpenCL GPFIFO variant | — | — | — | — | — | med | **med+** (opencl mirror) |
| Sema execute ladder | med | med | high | high | high | high | **high** |
| Sema slot selection | — | — | med | med+ | high | high | **high** (formal; tail caveat) |
| Sema extra descriptors | — | — | — | — | note | note | **med** (129 extras; unmapped) |
| MME upload order | med | med | high | high | high | high | **high** |
| MME real ISA | low | low | low | low | low | low | **low** (cands ELF-bleed) |
| Method 0x34a8 | — | — | — | note | med+ | high | **high** (imm+tables+x86) |
| Method 0x39e0 | — | — | — | — | note | med+ | **med+** (coupled; sparse) |
| RAM_DATA 0x3884 | low | low | low | low | low | low | **low** (still 0 primary) |
| SASS opcode families | med | med | med | med- | med+ | med | **med** (heuristic) |
| S2R SR map | low | low | med | med | med+ | med+ | **med+** (rank + multi-SR) |
| QMD/SPH static | low | low | low | low- | low | low | **low** (refine still weak) |
| Channel ngrams | low | med | med | med | med+ | med+ | **med+** (pipelines quantified) |
| CE/video imms | low | med | med | med | med | med | **med** (+vdpau) |
| Unknown method catalog | low | med | med | med | med+ | med+ | **med+** (site contexts) |
| Header exact dwords | low | low | low | low | low | low | **low** (0 confirmed) |

---

## 9. Artifacts index

| Path | Description |
|------|-------------|
| `pass17_mine.py` | Full 22-lib miner (~146s) |
| `pass17_deep_analysis.py` | Focus analysis (~163s) |
| `pass17_supplement.py` | Header/site/coupling/sema integrity probes |
| `pass17_targeted_disasm.sh` | Phase 1 dumps (~5980 disasm/x86/sass files) |
| `pass17_master.log` | Pipeline phase timestamps |
| `notes/pass17_sema_formal.md` | Sema formal + descriptor clusters |
| `notes/pass17_classes_crosslib.md` | Cross-lib ladders + consensus |
| `notes/pass17_mme_deep.md` | MME/unknown methods |
| `notes/pass17_qmd_sph_deep.md` | Strict QMD/SPH (weak; do not ship) |
| `notes/pass17_sass_deep.md` | SASS/S2R |
| `notes/pass17_sequences_deep.md` | Channel ngrams + pipelines |
| `notes/pass17_spa_ce_video_deep.md` | SPA/CE/video |
| `notes/pass17_mesa_priorities.md` | Priority list |
| `notes/pass17_focused_analysis.md` | Executive summary |
| `notes/pass17_supplement.md` | Header/site/coupling detail |
| `sema/pass17_sema_formal.json` | Sema mode table (trim to 11 rows for mesa) |
| `sema/pass17_sema_desc_extra.json` | Extra descriptor clusters |
| `sema/pass17_eglcore_sema_rows.json` | eglcore mirror rows |
| `tables/pass17_crosslib_summary.json` | Miner cross-lib summary |
| `tables/*.full.json` | Per-lib full mine blobs |
| `disasm/` | Hex/xxd/dword/ELF artifacts |
| `x86_callers/meth_sites_*_p17.txt` | Method imm site indexes |
| `qmd/`, `sph/` | Weak multi-stage candidates (do not use in mesa) |
| `mme_isa/cand_*.bin` | Weak entropy candidates |
| `sass/` | S2R windows + SR hist |
| `classes/` | Ladder JSON verify |
| `ngrams/pass17_ngrams.json` | Bigram/trigram/quad counts |
| `spa/pass17_spa_imm.json` | SPA imm counts |
| `HW_MODEL_PASS17_DEEP_DISASM_610.43.02.md` | This document |

---

## 10. Suggested next steps (pass18 / mesa / silicon)

1. **Mesa audit:** confirm ticks 142–146 fully wire pass15–17 P0 (class alts, sema formal 11-row modes, `0x34a8`, multi-SR S2R/SPH, sema+WFI, inv/CB, optional `0x39e0`); add/extend host selftests (-579+).
2. **Optional mesa:** formal sema slot policy table in `nv_push`/`nv_channel` sourced from pass17 JSON (trimmed); document cuda/opencl GPFIFO alt separately.
3. **Silicon:** sema slot modes vs classic ABCD; SPA families; NVDEC/GPFIFO class alts; `0x34a8` scratch init; multi-SR SPH.
4. **Live trace (pass18 RE priority):** attach to proprietary GL/VK/CUDA init — capture MME `RAM_DATA` stream and first compute `QMD`/`SPH`/`PCAS` blobs; diff against mesa builders; use as ground-truth to train pass18 matchers.
5. **Pass18 static RE (if live unavailable):** method-descriptor class_tag incremental map from `sema_desc_extra`; tighter QMD/SPH only with live templates; continue gpucomp SASS only as directional; expand vdpau/video imm families.

---

## 11. Pass17 vs pass16 delta (executive)

| Item | pass16 | pass17 |
|------|--------|--------|
| Sema | Corrected 11-row alignment | Formal JSON + execute×idx + **129 extra desc clusters** + egl mirror dump; **tail overscan caveat** |
| Classes | glcore/egl/cuda | +**opencl** (cuda GPFIFO mirror), **vdpau** (NVDEC/DMA/3D partial), rtcore/cuvid/encode negative |
| MME | imm sites, 0x39e0 note | **cooc/coupling stats**, RAM chain 0, site contexts (code+table), cross-lib miner counts |
| QMD/SPH | strict weak | multi-stage refine; **x86 bleed documented**; still weak |
| SASS | multi-SR refined | **SR rank + 40 multi-SR windows** + opcode families |
| Sequences | filtered ngrams | **quantified pipelines** + CE 4-gram signature |
| Artifacts | ~993 disasm | **~6000+** disasm/x86/sass/tables |
| RAM_DATA | imm 0 | imm 0 primary **reconfirmed** (sparse non-primary miner noise only) |
| Headers 0x2001 | 0 / unreliable | **0 exact confirmed** all key methods |

---

*End of pass17 report. Primary work product is evidence + priorities; implementation continues in mesa `nv_*` emitters and host selftests on branch `nvidia`. Static RE ceiling for QMD/SPH/MME-ISA/RAM_DATA is essentially reached without live trace — pass18 should prioritize capture or mesa P0 silicon bring-up.*
