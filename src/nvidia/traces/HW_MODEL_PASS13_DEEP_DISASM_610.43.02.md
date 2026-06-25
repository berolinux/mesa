# HW Model Pass 13 — Emitter Reconstruction / Sequence N-grams / Sema Rodata Tables
# (NVIDIA-Linux-x86_64-610.43.02)

**Date:** 2026-06-25
**Duration:** ~52 min full miner (22 libs, 3103s) + targeted objdump (~234 snippets) + focused/deep follow-up
**Scope:** Beyond pass12 imm windows — method-header construction, ordered method n-grams,
tight sema (SEMAPHORE* proximate), rodata sema config tables (new execute modes),
G2/G3 channel-prep sequences, CE/video/draw path reconstruction, literal `0x2001xxxx`
headers, PCAS vs inline-QMD frequency, gpucomp SASS/SPH, NVOS32 func runs, mesa ladder diff.
**Artifacts:** `/tmp/nvidia-reveng-pp-v2/re_pass13/` (`pass13_mine.py`, `pass13_targeted_disasm.sh`,
`tables/` 23, `rodata/`, `sema/` 22, `qmd/`, `sph/`, `mme/`, `sass/`, `methods/`, `sequences/` 44,
`xrefs/`, `nvos32/`, `ioctl/`, `nvenc/`, `disasm/` 234, `notes/`)
**Mesa impact (implemented):** sema ladder +6 modes (`0x1004`/`0x0804`/`0x0802` × align4/shift2)
in `nv_push.h` (16 total); selftests -482..-494; trace at `mesa/src/nvidia/traces/HW_MODEL_PASS13_…`.
G2/G3 channel_prep order refined from cuda n-grams (pass12 helpers already align).

---

## Method

1. **Phase 1 targeted dumps** — glcore/eglcore/cuda rodata class ladders; sema/MME/PCAS/QMD
   imm sites → `objdump -d` windows (~222 snippets in `disasm/`).
2. **Phase 2 full mine** (`pass13_mine.py`) — 22 priority libs (~500+ MB), per-lib JSON for
   class clusters, method histograms, header-construction scans, method n-gram4/6,
   tight sema, QMD/SPH/MME/SASS, NVOS32/ioctl imms, interesting strings.
3. **Phase 3 focused analysis** (`notes/pass13_focused_analysis.md`) — sema-tight per imm,
   critical method n-gram5, QMD candidates (cuda), MME upload order, G2/G3 prep n-gram6.
4. **Phase 4 deep follow-up** (`notes/pass13_deep_followup.md`) — sema rodata tables,
   literal headers, CE/video/draw chains, SPH/SASS, NVOS32 ascending func runs,
   PCAS vs QMD counts, invalidate sextets.
5. **Manual synthesis** — cross-check pass7–12, open-gpu-doc, mesa `nv_*` emitters.

**Reconfirmed limitation:** Pushbuffer method *headers* (`0x2001xxxx`) are almost always
**runtime-constructed**. Trust: (a) imm method offsets / indices, (b) rodata class ID arrays,
(c) rare literal headers (SPA_CMP, LAUNCH_DMA, MME_SCRATCH, WFI), (d) rodata sema config
tables, (e) method n-grams within tight byte windows, (f) open-gpu-doc / OGKM headers.

---

## 1. Class ladders (pass13 re-confirmation + eglcore offsets)

### 1.1 Primary glcore rodata (unchanged, re-dumped @ `0x11bb600`)

| Engine | File offset | Ladder (newest→oldest in array) |
|--------|-------------|----------------------------------|
| DMA_COPY `*B5` | `0x11bb600` | `CAB5 C9B5 C8B5 C7B5 C6B5 C5B5 C3B5 C1B5 C0B5 B0B5` |
| COMPUTE `*C0` | `0x11bb640` | `CEC0 CDC0 CBC0 C9C0 C7C0 C6C0 C5C0 C3C0 C1C0 C0C0 B1C0 B0C0` |
| 3D `*97` | `0x11bb680` | `CE97 CD97 CB97 C997 C797 C697 C597 C397 C197 C097 B197 B097` |
| GPFIFO `*6F` | `0x1238b70` | `CA6F C96F C56F C46F C36F C06F B06F A06F A26F 906F` |
| NVENC `*B7` | `0x1238bb4` | rodata **oldest-first**: `C5B7 C4B7 B4B7 B6B7 C7B7 C8B7 C9B7 CEB7 CFB7 D1B7` |
| NVDEC (partial) | `0x1238bf0` | `C4B0 C6B0 C7B0 B8B0 C9B0` (+ global hits favor `A0B0/B0B0/C0B0/C8B0/CBB0`) |

### 1.2 eglcore parallel ladders (new pass13 offsets)

| Engine | eglcore file offset | Ladder head |
|--------|---------------------|-------------|
| DMA_COPY | `0xfa5ee0` | `CAB5 C9B5 C8B5 C7B5 C6B5 C5B5 C3B5 C1B5 C0B5 B0B5` (identical to glcore) |
| COMPUTE | `0xfa5f20` | `CEC0 CDC0 CBC0 …` |
| 3D | `0xfa5f60` | `CE97 CD97 CB97 …` |
| GPFIFO | `0x1253d80` | `CA6F C96F C56F …` |

eglcore mirrors glcore engine ladders exactly; only file offsets differ (smaller image).

### 1.3 Mesa vs glcore (only_mesa alts — keep)

| Engine | only_mesa (not in primary glcore ladder) | Rationale |
|--------|------------------------------------------|-----------|
| DMA | `C4B5 A0B5` | pass9/cuda alts |
| COMPUTE | `CCC0 CAC0 C8C0 C4C0` | cuda intermediate / mixed clusters |
| 3D | `CC97 CA97 C897 C497` | pass9/cuda alts |
| GPFIFO | `C86F C76F C66F C36E A16F 506F` | pass9/cuda only |
| NVENC | mesa newest-first `D1B7…B4B7` | inverse of rodata storage |
| NVDEC | `CBB0 C8B0 C0B0 B0B0 A0B0` | global hit ranked, not contiguous rodata |

**No pass13 ladder changes required** — mesa alts remain correct.

---

## 2. Host semaphore — pass13 breakthrough (rodata config tables)

### 2.1 glcore sema execute/mode rodata @ `0x11e30c0`–`0x11e3220`

Pass13 located a **structured sema configuration table** (not x86 noise) with execute
immediates paired with SEMAPHORE method indices (`0x10`/`0x11`/`0x12` = A/B/C `>>2`).

Representative 8-dword records (annotated):

| +off | dwords (key fields) | Interpretation |
|------|---------------------|----------------|
| `+0x00` | `… 00001004 00000004 00000002 00000012` | **exec `0x1004`**, sema idx `0x12` (SEMA_C) |
| `+0x20` | `… 00001002 00000004 00000002 00000011` | **exec `0x1002`**, sema idx `0x11` (SEMA_B) |
| `+0x40` | `… 00000804 00000004 00000002 00000011` | **exec `0x0804`**, sema idx `0x11` |
| `+0x60` | `… 00000802 00000004 00000002 00000010` | **exec `0x0802`**, sema idx `0x10` (SEMA_A) |
| `+0x80`..`+0xe0` | same family with tag `00000003` / `00000005` | alternate channel/engine variants |
| `+0x100` | `… 00001004 00000000 00000001 00000008` | `0x1004` + small tag `1`/`8` |
| `+0x120` | `… 00001002 00000000 00000001 00000007` | `0x1002` |
| `+0x140` | `… 00001001 00000000 00000001 0000001d` | **exec `0x1001`** (primary blob) |

**New execute values** (not in pass8/10/11/12 ladder):

| Exec imm | Evidence | Priority for mesa ladder |
|----------|----------|--------------------------|
| `0x1001` | rodata table + pass8 trace | **#1 primary** (keep) |
| `0x1002` | rodata table + pass12 | **#2** (keep) |
| `0x1004` | rodata table (same family as 0x1002/0x1001) | **#3 NEW** — try after 0x1002 |
| `0x1000` | structured tables @ `0x10efe6c`+ (glcore/eglcore method-desc style) | **#4** (keep; noisy in .text) |
| `0x0804` | rodata table (half-word variant of 0x1004?) | **#5 NEW** — experimental |
| `0x0802` | rodata table (half-word variant of 0x1002?) | **#6 NEW** — experimental |
| `0x0002` | vdpau primary; glcore also scattered | **#7** vdpau path (keep) |
| `0x0001` | appears in sema ABCD windows (cuda/glcore) | **#8** weak; may be payload not execute |

### 2.2 cuda sema `0x1001` structured records

Repeating pattern (6+ sites, e.g. `0x299bed8`, `0x344ab68`, `0x3cc4a50`, `0x4ed2c28`):

```
00000003 00000099 00000004 00000000 00000000 00000000 00001001 70000000 00000040 …
```

`0x1001` execute + `0x40` (SEMAPHOREA method offset) in same record — **confirms cuda uses
same 0x1001 primary** as glcore blob path.

### 2.3 Tight sema (SEMAPHORE* proximate) — caveats

Auto tight-scanner still noisy (ELF headers, x86 immediates colliding with method index
`0x10`/`0x40`). **Authoritative sources ranked:**

1. Rodata sema config table @ glcore `0x11e30c0` (section 2.1)
2. cuda `0x1001`+`0x40` records (section 2.2)
3. pass8/10/11 dedicated imm sites (x86 `movl $0x1001`)
4. vdpau execute `0x2` (pass8)
5. n-gram / sema ABCD quad windows (weak)

GPFIFO sema ABCD quadruplets (methods `0x40/44/48/4c` or indices `0x10/11/12/13` within
16 dwords): glcore 36, cuda 178, vdpau 4. Exec imms inside those windows skew to `1`/`2`
(likely payload or mode tags, not necessarily SEMAPHORED execute).

### 2.4 Recommended mesa sema ladder (pass13 extended)

Default bring-up order (extends pass12 with `0x1004`/`0x0804`/`0x0802`):

```
BLOB_ALIGN4(0x1001) → BLOB_SHIFT2(0x1001) →
BLOB1002_ALIGN4 → BLOB1002_SHIFT2 →
BLOB1004_ALIGN4 → BLOB1004_SHIFT2 →          /* pass13 NEW */
BLOB1000_ALIGN4 → BLOB1000_SHIFT2 →
BLOB0804_ALIGN4 → BLOB0804_SHIFT2 →          /* pass13 NEW experimental */
BLOB0802_ALIGN4 → BLOB0802_SHIFT2 →          /* pass13 NEW experimental */
VDPAU_ALIGN4(0x2) → VDPAU_SHIFT2(0x2) →
OPEN_ALIGN4 → OPEN_SHIFT2
```

(Implementation: extend `enum nv_host_sema_mode` + `nv_host_sema_ladder_fill`; keep
first-win sticky cache on successful channel submit.)

---

## 3. Compute / QMD / PCAS (G2 path)

### 3.1 Method imm frequency (pass13)

| Method | glcore offs / idx hits | eglcore | cuda | Notes |
|--------|------------------------|---------|------|-------|
| `SET_INLINE_QMD_ADDRESS_A` `0x2f00` / `0xbc0` | 36 / 38 | 36 / 42 | 191 / 1304 | cuda dominates |
| `SEND_PCAS_A` `0x2e00` / `0xb80` | 19 / 36 | 14 / 33 | 282 / **1439** | cuda prefers PCAS |
| `SEND_SIGNALING_PCAS_B` `0x2e04` / `0xb81` | 1 / 10 | — / 12 | — / 110 | signaling rare in glcore |
| `SEND_PCAS2_B` `0x2e08` / `0xb82` | 3 / 3 | 3 / 5 | 21 / 10 | newer classes |
| `LOAD_INLINE_QMD_DATA` `0x2f08` / `0xbc2` | 3 / 3 | 4 / 3 | 30 / 7 | fewer than QMD_A |

**Implication:** glcore/eglcore favor **inline QMD** (`0x2f00`) roughly equally with PCAS_A;
cuda strongly favors **PCAS_A** (`0xb80` idx 1439). Mesa should keep both paths; smoke/G2
bring-up with inline QMD is correct for GL-shaped channels; compute-only may prefer PCAS.

### 3.2 G2 channel-prep n-gram6 (within 8KB, pass13 focused)

**glcore** (weak counts, method imm only — still directional):

```
WFI → CWD → SET_OBJ …
LMEM_NT_A → LMEM_NT_B → LMEM_NT_C  (triple local-mem non-throttled)
SET_OBJ → WFI → QMD_A …
INV_SC → WFI → SET_OBJ …
SPA → … → WFI
```

**cuda** (stronger; 40–50× counts):

```
WFI → SET_OBJ → INV_SD → QMD_B → SIG_PCAS_B → INV_TH   (50×)
SET_OBJ → INV_SD → QMD_B → SIG_PCAS_B → INV_TH → INV_SD
SET_OBJ… → INV_SC → INV_TS → SET_OBJ…
```

**Pass13 refined G2 bring-up order (mesa):**

```
SET_OBJECT(compute_class)
SET_SPA_VERSION (0x53 smoke default / device SM override)
SET_CWD_CONTROL(0) [optional SET_CWD_SLOT_COUNT]
INVALIDATE_SHADER_CACHES + texture/sampler/data/instruction/constant caches
[optional SET_SHADER_LOCAL_MEMORY_A/B + NON_THROTTLED_A/B/C]
WFI (engine 0x2c0; optional GPFIFO WFI 0x84 before first launch)
materialize QMD (invalidate_caches=1, barrier_count, sema release0)
SET_INLINE_QMD_ADDRESS_A/B + LOAD_INLINE_QMD_DATA
  OR mem QMD + SEND_PCAS_A [+ SEND_SIGNALING_PCAS_B]
  OR SEND_PCAS2_B / SEND_SIGNALING_PCAS2_B for newer classes
```

Agrees with pass12; cuda n-grams add **INV_SD / INV_TH interleaved with QMD_B + SIG_PCAS_B**
— optional second invalidate wave after PCAS for compute-heavy paths.

### 3.3 QMD templates

Heuristic CTA-dim scan produces many false positives in cuda ELF (random tables with
plausible 1..1024 dims). **Do not treat early-file cuda hits @ 0x320+ as real QMD.**

Trust QMD field layout from open-gpu-doc / mesa `nv_qmd_desc` (pass12 table unchanged):

| QMD off | Field |
|---------|-------|
| `0x38..0x4c` | CTA raster + thread dims |
| `0x50` | shared_mem_size |
| `0x54` | qmd_version / sass_version packed |
| `0x58` | local_mem_low |
| `0x60..0x74` | sema release0/1 addr + payload |
| `0x80` | program_offset |
| `0x84` | register_count_v |

Real QMD is almost always **runtime-filled** (cuda driver), not static rodata.

### 3.4 Literal method headers (rare)

| Header | Meaning | glcore | cuda |
|--------|---------|--------|------|
| `0x20010680` | inc1 `SET_SPA_VERSION` compute (`0x1a00>>2=0x680`) | 6 | — |
| `0x200120b0` | inc1 engine `WFI` (`0x2c0>>2=0xb0`) | — | **12** |
| `0x200181c0` | inc1 `LAUNCH_DMA` (`0x700>>2=0x1c0`) | 2 | — |
| `0x20010e00` | inc1 `SET_MME_SHADOW_SCRATCH` | 2 | — |
| `0x20012018` | inc1 `SET_REFERENCE` (`0x60>>2=0x18`) | 1 | — |

Confirms SPA_VERSION, WFI, LAUNCH_DMA, MME_SCRATCH are sometimes emitted as **literal
headers** (not always runtime-or'd) — useful for objdump xrefs / smoke golden matching.

---

## 4. 3D / MME / shader bind (G3 path)

### 4.1 G3 channel-prep n-gram6 (glcore)

Dominant patterns (10–23×):

```
PIPE_SH → PIPE_PR → SPA → PIPE_SH…     (SET_PIPELINE_SHADER / PROGRAM / SPA_VERSION)
SET_OBJ → … → PIPE_SH → SPA
SPA → PIPE_PR → PIPE_SH…
PIPE_SH → SET_OBJ → SH_EXC → SET_OBJ…  (SET_SHADER_EXCEPTIONS in bind path)
```

**Pass13 refined G3 bring-up order (mesa, aligns pass12 `nv_3d_emit_g3_channel_prep`):**

```
SET_OBJECT(3d_class)
SET_SPA_VERSION (0x53 / device)
[optional SET_SHADER_EXCEPTIONS]
SET_PROGRAM_REGION_A/B (program heap VA)
SET_PIPELINE_SHADER (enable stages) + SET_PIPELINE_PROGRAM + SET_PIPELINE_REGISTER_COUNT
[optional MME upload: IPTR → IDATA×N → SPTR → SDATA; CALL_MME_MACRO/DATA if !stub]
INVALIDATE_SHADER_CACHES + texture/sampler/data/instruction/constant (0x2c80..0x2c94)
WFI (0x2c0)
[draw path: viewport/scissor/render-enable → BEGIN/END or DRAW_*]
```

### 4.2 MME upload evidence

Method-offset n-grams near MME markers are sparse (runtime header build dominates), but
observed index-level patterns include:

```
IPTR_i → CDATA_i → SCRATCH_i …
SCRATCH → CDATA → IPTR → SPTR   (one hit: upload-ish order)
SPTR_i → SCRATCH_i → CDATA_i → IPTR_i
```

**Canonical upload order (unchanged, pass12/13 confirmed by method map + open-gpu-doc):**

```
SET_MME_INSTRUCTION_RAM_PTR(slot_base)
SET_MME_INSTRUCTION_RAM_DATA × N
SET_MME_START_ADDRESS_RAM_PTR(slot)
SET_MME_START_ADDRESS_RAM_DATA(entry_pc)
[CALL_MME_MACRO / CALL_MME_DATA for non-stub macros]
```

Stubs remain end-only (`is_stub_end_only`); path C gated `!is_stub_end_only` (tick137 WIP).
Real MME loops still need dedicated CALL_MME_MACRO caller disasm (future pass14).

### 4.3 Invalidate-cache sextets

Full 3D invalidate bundle (`0x2c80..0x2c94` all six method offs within 24 dwords):
scanned but counts low in imm-only pass (headers runtime-built). Mesa should still emit
full invalidate wave after program/MME bind (pass12 behavior correct).

### 4.4 Draw path n-grams (glcore, section 14 deep follow-up)

Patterns cluster around `BEGIN`/`END`, `DRAW_VA`/`DRAW_IB`, viewport (`VP_SX`), scissor
(`SC_EN`), render-enable (`RE_A`), `WFI`, `NOP`. Smoke G3 sema-only path doesn't need
full draw; future raster smoke should follow:

```
SET_RENDER_ENABLE_* → SET_VIEWPORT_* → SET_SCISSOR_* →
SET_VERTEX_ARRAY_* / SET_INDEX_BUFFER_* → BEGIN → DRAW_* / END → WFI
```

---

## 5. CE / DMA (G1 path)

CE method n-grams heavily favor address programming then launch:

```
IN_U / IN_L / OUT_U / OUT_L → P_IN / P_OUT → LL / LC → [SRC_P / DST_P] → LAUNCH
```

glcore shows strong `SRC_P` + `IN_U` interleaving (physical + virtual offset programming).
cuda has massive `IN_U` self-repeats (noise from method index collision) plus `DST_P` chains.

**Mesa G1 order (unchanged, pass12/13 confirmed):**

```
OFFSET_IN_UPPER/LOWER → OFFSET_OUT_UPPER/LOWER →
PITCH_IN/OUT → LINE_LENGTH_IN → LINE_COUNT →
[SET_SRC_PHYS / SET_DST_PHYS] → LAUNCH_DMA (0x700)
[+ host sema via GPFIFO SEMAPHORE* + execute]
```

Literal header `0x200181c0` (LAUNCH_DMA inc1) in glcore — good smoke golden anchor.

---

## 6. NVENC / NVDEC (video / G4)

### 6.1 cuvid method chains

Dominant: `APP` (`0x200`) + `CTRL` (`0x400`) tightly interleaved (1000–3000× n-grams).
`PIC` (`0x404`) appears in self-repeat runs (298×). Full encode/decode setup is mostly
runtime-built; pass12 method frequency table remains authoritative.

### 6.2 Canonical emit order (reinforced, unchanged)

**NVENC:**
```
APP_ID(0x200) → CTRL(0x400) → PIC_SETUP(0x404) → IN(0x408) →
BS(0x40c) → RC(0x410) → STATUS(0x414) → OUT_ST(0x718) → OUT_BS(0x71c) →
[OUT_MB 0x720] → EXECUTE
```

**NVDEC:**
```
SET_CONTROL(0x400) → SET_DRV_PIC_SETUP(0x404) → SET_IN_BUF(0x408) →
SET_PIC_HIST(0x40c) → SET_FILTER_BUF(0x410) → SET_OUT_BUF(0x414) →
SET_STATUS(0x418) → SET_DISPLAY_BUF(0x41c) → kick/execute
```

Preferred classes: NVENC `C8B7` (high glcore hits) / ladder head `D1B7`; NVDEC `C8B0`/`C9B0`
/ ladder head `CBB0`.

vdpau sema execute remains `0x2` (no `0x1001` tight sites in vdpau).

---

## 7. SASS / SPH / gpucomp

### 7.1 SASS opcode frequency (gpucomp hi16, known families only)

| Op | hi16 | Count | Mesa emitter |
|----|------|-------|--------------|
| IMAD | `0x5c24` | 992 | `nv_sass_emit_imad` (if present) |
| IADD3 | `0x5c10` | 128 | — |
| FADD | `0x5c58` | 120 | — |
| LOP3 | `0x5c40` | 96 | — |
| MUFU | `0x5080` | 67 | — |
| STS | `0xef50` | 50 | — |
| LDG | `0xeed0` | 45 | — |
| SHF | `0x5c48` | 44 | — |
| FFMA | `0x5c28` | 34 | — |
| BAR | `0xf0a8` | 26 | — |
| LDS | `0xef48` | 23 | — |
| S2R | `0xf0c8` | 20 | `nv_sass_emit_s2r` |
| EXIT | `0x7918` | 17 | `nv_sass_emit_exit` |
| FMUL | `0x5c60` | 17 | — |
| CS2R | `0x50c8` | 16 | — |
| MOV | `0x5c98` | 14 | `nv_sass_emit_mov_*` |
| STG | `0xeed8` | 14 | `nv_sass_emit_stg` (store-imm smoke) |
| NOP | `0x50b0` | 12 | `nv_sass_emit_nop` |
| BRA | `0xe240` | 9 | — |

**Note:** pass12 counted MOV/EXIT much higher because it scanned full dwords without
hi16 filtering; pass13 stricter hi16 pass is more accurate for **compiler constant
pools** but misses SASS embedded in x86-adjacent blobs. Use both.

Smoke kernels: `MOV imm` + `STG`/`STS` + `EXIT` (+ optional `S2R` for thread id) remains
correct minimal path (`nv_sph_build_compute_store_imm` / `mov_imm_exit`).

### 7.2 SPH types (gpucomp)

| type dword | Role |
|------------|------|
| `0x00000001` | VTG/PS SPH v1 (most common) |
| `0x00010001` | Compute-ish SPH v1 (with size/attr fields) |
| `0x00020001` | Compute/extended SPH v2 |
| `0x00010102` / `0x00020102` | pass12 extended forms |

Many early-file `type=0x1` hits are **ELF/metadata false positives** (e.g. @0x40). Real SPH
templates cluster mid/late image; mesa hand-built SPH for smoke is sufficient.

---

## 8. NVOS32 / RM / ioctl

### 8.1 Ascending function ID runs (rodata)

glcore has ascending `1..16` / `1..21` / `1..32` runs at `0x10b4d84`, `0x11bba84`,
`0x11bcc84`, `0x12026e4` — consistent with NVOS32 function enum tables (ALLOC_SIZE=1,
FREE=2, … HW_FREE=0x10, plus extended IDs to 32).

cuda similar runs at `0x64b9a24`, `0x67c5e80`, etc.

### 8.2 ioctl imms

Scanned for `0xC0xx46xx` / `0x000046xx` patterns in glsi/glcore/cuda; detailed per-lib
files in `ioctl/`. Primary channel to RM remains `ioctl` on `/dev/nvidiactl` +
`/dev/nvidiaN` + `/dev/nvidia-uvm` (cuda) as pass1–8 established.

No pass13 change to mesa `nv_rm` ioctl numbers (already from open-gpu-kernel-modules headers).

---

## 9. Targeted disasm notes

### 9.1 pass12 sema imm sites (glcore `0xa43b38` etc.)

Several pass12-listed `0x1001` file offsets disassemble as **large `movl $imm` stores to
global data** (configuration struct init), not pushbuffer emit loops. Example
`glcore_sema_off_10763064.s`: sequential `c7 05 …` stores including small ints and
`0x111` — likely driver global init, not sema execute path.

**Lesson:** prefer rodata sema tables (section 2.1) and cuda `0x1001`+`0x40` records over
raw imm file-offset lists from pass12.

### 9.2 Artifact counts

| Artifact | Count / location |
|----------|------------------|
| disasm snippets | ~222 in `disasm/` (+ MANIFEST.txt) |
| rodata ladder dumps | glcore 6 engines + eglcore/cuda ladder scans |
| miner per-lib tables | 22 libs (when complete) |
| focused + deep notes | `notes/pass13_focused_analysis.md`, `pass13_deep_followup.md` |
| sass report | `sass/gpucomp_opcode_report.md` |
| video seq | `nvenc/*.video_seq.md` |
| ioctl strings/imms | `ioctl/` |

---

## 10. Mesa action items (pass13 → implementation)

| Priority | Action | Files |
|----------|--------|-------|
| **P0** | Add sema modes `0x1004` (and optionally `0x0804`/`0x0802`) to ladder after `0x1002` | `nv_push.h` / sema helpers, `nv_channel.c` ladder, `nv_smoke_selftest.h` |
| **P1** | G2 optional post-PCAS invalidate wave (INV_SD/INV_TH) for compute path | `nv_compute_methods.h`, `nv_channel.c` g2 bringup |
| **P1** | Keep dual QMD path: inline QMD (GL) + PCAS_A (cuda-shaped); no change if both exist | `nv_qmd.h`, `nvrm_pipeline.c` |
| **P2** | G3 draw-path helper for future raster smoke (viewport/scissor/BEGIN/END) | `nv_3d_methods.h` |
| **P2** | Finish tick137: `nv_sph_build_compute_mov_imm_exit`, `nv_channel_nvdec_smoke_slice_submit` | WIP from prior segment |
| **P3** | MME real macro disasm (CALL_MME_MACRO callers) — pass14 | `nv_mme.h` |
| **P3** | Device SM → SPA_VERSION table (0x86/0x89/0x90…) | `nv_device_info` |

**No class ladder changes.** Mesa alts vs glcore primary remain intentional.

---

## 11. Confidence / limitations

| Area | Confidence | Notes |
|------|------------|-------|
| Class ladders | **High** | rodata re-dump + eglcore mirror |
| Sema `0x1001` primary | **High** | rodata table + cuda records + pass8 |
| Sema `0x1002`/`0x1004`/`0x0804`/`0x0802` | **Medium-High** | rodata table structure; HW untested |
| Sema `0x1000` | **Medium** | structured tables exist; .text noisy |
| Sema `0x2` vdpau | **High** | pass8 + vdpau exclusive |
| G2/G3 prep order | **Medium-High** | n-grams directional; imm-only misses runtime headers |
| QMD static templates | **Low** | mostly runtime; use open-gpu-doc fields |
| MME real loops | **Low** | stubs/end-only still; needs pass14 |
| SASS full ISA | **Low-Medium** | smoke ops sufficient; compiler is gpucomp |
| Video execute/kick | **Medium** | method order clear; execute imm varies |
| Silicon G1–G4 | **Unproven** | no `/dev/nvidia*` in build env |

---

## 12. Cross-pass index

| Pass | Focus |
|------|-------|
| 1 | Class tables, priorities |
| 2–3 | Deep disasm intro |
| 4–7 | Method/engine refinement |
| 8 | Sema `0x1001` / `0x2`, submit paths |
| 9–10 | Ladders, video, QMD |
| 11 | Class ladders, sema modes |
| 12 | Imm windows, sema `0x1000`/`0x1002`, G2/G3 channel_prep |
| **13** | **Emitter n-grams, sema rodata tables (`0x1004`/`0x0804`/`0x0802`), PCAS vs QMD freq, eglcore ladder offsets, literal headers, draw/CE/video sequences** |

---

## 13. Files

| Path | Content |
|------|---------|
| `/tmp/nvidia-reveng-pp-v2/re_pass13/HW_MODEL_PASS13_DEEP_DISASM_610.43.02.md` | This document |
| `/tmp/nvidia-reveng-pp-v2/re_pass13/pass13_mine.py` | Full miner |
| `/tmp/nvidia-reveng-pp-v2/re_pass13/pass13_targeted_disasm.sh` | Targeted objdump/rodata |
| `/tmp/nvidia-reveng-pp-v2/re_pass13/notes/auto_findings.md` | Machine summary (post-miner) |
| `/tmp/nvidia-reveng-pp-v2/re_pass13/notes/pass13_focused_analysis.md` | Sema/G2/G3/QMD/MME |
| `/tmp/nvidia-reveng-pp-v2/re_pass13/notes/pass13_deep_followup.md` | Headers/CE/video/SPH/NVOS32 |
| `/tmp/nvidia-reveng-pp-v2/re_pass13/notes/mesa_vs_glcore_ladders.md` | Ladder diff |
| `/tmp/nvidia-reveng-pp-v2/re_pass13/disasm/` | ~222 objdump windows |
| `mesa/src/nvidia/traces/HW_MODEL_PASS13_DEEP_DISASM_610.43.02.md` | Repo copy (when copied) |

---

## 14. Machine miner appendix (post-run)

- Libs: 22
- Total miner time: 3103.09s
- Tight sema global top: {'0x00000001': 1295, '0x00000002': 448, '0x00010001': 282, '0x00001000': 104, '0x00001002': 22, '0x00001001': 9}

### Top global method n-gram4 (bring-up sequences)

- (12820) SET_CONTROL → APP_ID → SEMAPHOREA → SEMAPHOREA
- (11427) SEMAPHOREA → SET_CONTROL → APP_ID → SEMAPHOREA
- (8938) APP_ID → SEMAPHOREA → SEMAPHOREA → SEMAPHOREA
- (8857) SEMAPHOREA → APP_ID → SET_CONTROL → APP_ID
- (8720) APP_ID → SET_CONTROL → APP_ID → SEMAPHOREA
- (7584) SET_CONTROL → SEMAPHOREA → SET_CONTROL → SEMAPHOREA
- (7548) SEMAPHOREA → SET_CONTROL → SEMAPHOREA → SET_CONTROL
- (7137) SET_CONTROL → APP_ID → APP_ID → APP_ID
- (7026) APP_ID → SET_CONTROL → SET_CONTROL → SET_CONTROL
- (6943) SET_CONTROL → SET_CONTROL → APP_ID → APP_ID
- (6674) APP_ID → SEMAPHOREA → SEMAPHOREA → SET_CONTROL
- (6575) SEMAPHOREA → SEQ_HDR → SEMAPHOREA → SEQ_HDR
- (6410) SEMAPHOREA → SEMAPHOREA → SET_CONTROL → APP_ID
- (6166) APP_ID → SET_CONTROL → APP_ID → SET_CONTROL
- (5692) SET_CONTROL → SET_CONTROL → SET_CONTROL → APP_ID
- (5427) SEMAPHOREA → SEMAPHOREA → SEMAPHOREA → APP_ID
- (5295) APP_ID → APP_ID → APP_ID → SET_CONTROL
- (5147) SEMAPHOREA → SEMAPHOREA → APP_ID → SET_CONTROL
- (5073) APP_ID → APP_ID → SET_CONTROL → APP_ID
- (4871) SEQ_HDR → SEMAPHOREA → SEQ_HDR → SEMAPHOREA
- (4497) SET_CONTROL → SET_PIPELINE_SHADER → SET_PIPELINE_SHADER → SET_PIPELINE_SHADER
- (4464) SET_REFERENCE → SEMAPHOREA → SET_REFERENCE → SEMAPHOREA
- (4380) SEMAPHOREA → SET_REFERENCE → SEMAPHOREA → SET_REFERENCE
- (4184) SET_SRC_WIDTH → SET_DST_WIDTH → SEMAPHOREC → SEMAPHORED
- (4183) SET_DST_WIDTH → SEMAPHOREC → SEMAPHORED → NON_STALL_INTERRUPT

### Method hit aggregates (selected engines, top 8)


**methods_3d**
- SET_PIPELINE_SHADER: 3555484
- SET_OBJECT: 470634
- SET_MME_SHADOW_SCRATCH: 263669
- SET_PIPELINE_PROGRAM: 200416
- SET_SCISSOR_ENABLE: 172656
- SET_INDEX_BUFFER_A: 103431
- NO_OPERATION: 100879
- SET_NOTIFY: 87734

**methods_compute**
- INVALIDATE_SHADER_CACHES: 163208
- SET_SHADER_LOCAL_MEMORY_D: 60608
- WAIT_FOR_IDLE: 58779
- INVALIDATE_TEXTURE_HEADER_CACHE: 55344
- SET_SHADER_LOCAL_MEMORY_A: 44340
- SET_SHADER_SHARED_MEM_WINDOW: 35114
- INVALIDATE_INSTRUCTION_CACHE: 26483
- SET_SHADER_LOCAL_MEMORY_I: 23584

**methods_ce**
- SET_SRC_PHYS: 800317
- OFFSET_IN_UPPER: 470634
- OFFSET_OUT_LOWER: 207083
- PITCH_OUT: 201077
- PITCH_IN: 149651
- SET_DST_PHYS: 142682
- SET_DST_WIDTH: 102821
- LAUNCH_DMA: 91041

**methods_gpfifo**
- SEMAPHOREA: 680394
- SET_OBJECT: 470634
- SEMAPHOREC: 273605
- NON_STALL_INTERRUPT: 239009
- SET_REFERENCE: 224176
- SEMAPHOREB: 129924
- SEMAPHORED: 115714
- WFI: 98382

**methods_nvenc**
- CTRL: 800317
- APP_ID: 470634
- PIC_SETUP: 142682
- SEQ_HDR: 91041
- STATUS: 64381
- RC: 51877
- OUT_MB: 39979
- PIC_HDR: 34830

**methods_nvdec**
- SET_CONTROL: 800317
- SET_DRV_PIC_SETUP: 142682
- SET_OUT_BUF: 64381
- SET_FILTER_BUF: 51877
- SET_IN_BUF: 34117
- SET_PIC_HIST: 32101
- SET_STATUS: 23294
- SET_DISPLAY_BUF: 18903

### Per-lib timing (top 10 by sema_tight / elapsed)

- libnvidia-gpucomp.so.610.43.02: 553.17s sema_tight=120 qmd=60 sph=80
- libnvidia-opencl.so.610.43.02: 461.03s sema_tight=120 qmd=60 sph=40
- libcuda.so.610.43.02: 460.35s sema_tight=120 qmd=60 sph=40
- libnvidia-nvvm.so.610.43.02: 403.3s sema_tight=120 qmd=60 sph=40
- libnvoptix.so.610.43.02: 218.68s sema_tight=120 qmd=30 sph=40
- libnvidia-rtcore.so.610.43.02: 217.35s sema_tight=120 qmd=30 sph=40
- libnvidia-ptxjitcompiler.so.610.43.02: 183.98s sema_tight=120 qmd=30 sph=40
- libnvidia-glcore.so.610.43.02: 178.78s sema_tight=120 qmd=30 sph=80
- libnvidia-eglcore.so.610.43.02: 164.04s sema_tight=120 qmd=30 sph=80
- libnvcuvid.so.610.43.02: 113.77s sema_tight=120 qmd=30 sph=40
- libnvidia-vksc-core.so.610.43.02: 51.79s sema_tight=120 qmd=30 sph=40
- libnvidia-glvkspirv.so.610.43.02: 50.25s sema_tight=120 qmd=30 sph=40

### Auto-findings n-gram excerpt

```
## Global method n-gram4 (bring-up sequences)
- (12820) SET_CONTROL → APP_ID → SEMAPHOREA → SEMAPHOREA
- (11427) SEMAPHOREA → SET_CONTROL → APP_ID → SEMAPHOREA
- (8938) APP_ID → SEMAPHOREA → SEMAPHOREA → SEMAPHOREA
- (8857) SEMAPHOREA → APP_ID → SET_CONTROL → APP_ID
- (8720) APP_ID → SET_CONTROL → APP_ID → SEMAPHOREA
- (7584) SET_CONTROL → SEMAPHOREA → SET_CONTROL → SEMAPHOREA
- (7548) SEMAPHOREA → SET_CONTROL → SEMAPHOREA → SET_CONTROL
- (7137) SET_CONTROL → APP_ID → APP_ID → APP_ID
- (7026) APP_ID → SET_CONTROL → SET_CONTROL → SET_CONTROL
- (6943) SET_CONTROL → SET_CONTROL → APP_ID → APP_ID
- (6674) APP_ID → SEMAPHOREA → SEMAPHOREA → SET_CONTROL
- (6575) SEMAPHOREA → SEQ_HDR → SEMAPHOREA → SEQ_HDR
- (6410) SEMAPHOREA → SEMAPHOREA → SET_CONTROL → APP_ID
- (6166) APP_ID → SET_CONTROL → APP_ID → SET_CONTROL
- (5692) SET_CONTROL → SET_CONTROL → SET_CONTROL → APP_ID
- (5427) SEMAPHOREA → SEMAPHOREA → SEMAPHOREA → APP_ID
- (5295) APP_ID → APP_ID → APP_ID → SET_CONTROL
- (5147) SEMAPHOREA → SEMAPHOREA → APP_ID → SET_CONTROL
- (5073) APP_ID → APP_ID → SET_CONTROL → APP_ID
- (4871) SEQ_HDR → SEMAPHOREA → SEQ_HDR → SEMAPHOREA
- (4497) SET_CONTROL → SET_PIPELINE_SHADER → SET_PIPELINE_SHADER → SET_PIPELINE_SHADER
- (4464) SET_REFERENCE → SEMAPHOREA → SET_REFERENCE → SEMAPHOREA
- (4380) SEMAPHOREA → SET_REFERENCE → SEMAPHOREA → SET_REFERENCE
- (4184) SET_SRC_WIDTH → SET_DST_WIDTH → SEMAPHOREC → SEMAPHORED
- (4183) SET_DST_WIDTH → SEMAPHOREC → SEMAPHORED → NON_STALL_INTERRUPT
- (3970) APP_ID → APP_ID → APP_ID → SEMAPHOREA
- (3954) SET_CONTROL → APP_ID → SET_CONTROL → SET_CONTROL
- (3940) SEMAPHOREA → SEMAPHOREA → APP_ID → APP_ID
- (3925) SET_PIPELINE_SHADER → SET_PIPELINE_SHADER → SET_PIPELINE_SHADER → SEMAPHOREA
- (3537) APP_ID → SEMAPHOREA → APP_ID → SET_CONTROL
- (3533) SET_CONTROL → APP_ID → SEMAPHOREA → SET_CONTROL
- (3501) SEMAPHOREA → APP_ID → APP_ID → SET_CONTROL
- (3424) APP_ID → APP_ID → SEMAPHOREA → SEMAPHOREA
- (3275) APP_ID → APP_ID → SET_CONTROL → SET_CONTROL
- (3177) SET_CONTROL → SET_CONTROL → SET_PIPELINE_SHADER → SET_PIPELINE_SHADER
- (2820) SEMAPHOREA → SEMAPHOREA → SEMAPHOREA → SET_REFERENCE
- (2710) SEMAPHOREC → SEMAPHORED → NON_STALL_INTERRUPT → SET_SRC_WIDTH
- (2700) SEMAPHORED → NON_STALL_INTERRUPT → SET_SRC_WIDTH → SET_DST_WIDTH
- (2678) NON_STALL_INTERRUPT → SET_SRC_WIDTH → SET_DST_WIDTH → SEMAPHOREC
- (2591) SEMAPHOREA → SEMAPHOREB → SEMAPHOREA → SEMAPHOREB


```

