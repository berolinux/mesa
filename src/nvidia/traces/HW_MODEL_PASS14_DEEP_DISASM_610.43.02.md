# HW Model Pass 14 — MME/SASS/SPH/QMD/Emitter/CFG Deep Disassembly
# (NVIDIA-Linux-x86_64-610.43.02)

**Date:** 2026-06-25
**Duration:** multi-hour session (focus analysis ~83s; deep follow-up ~121s; full miner 22 libs in progress/completed; targeted objdump 32 snippets)
**Scope:** Beyond pass13 (sema rodata, header construction, n-grams) — sema *record schema*, method-descriptor tables, refined QMD/SPH (ELF-skip), gpucomp SASS opcode/SR/kernel patterns, MME upload order, class ladder realignment (NVDEC extension), CE/video imm refinement, mesa implementation priorities.
**Artifacts:** `/tmp/nvidia-reveng-pp-v2/re_pass14/` (`pass14_mine.py`, `pass14_focus_analysis.py`, `pass14_targeted_disasm.sh`, `notes/`, `disasm/`, `tables/`, `mme/`, `sass/`, `qmd/`, `sph/`, `sema/`, `sequences/`, …)
**Mesa impact (recommended this pass):** NVDEC class alts `CDB0/CEB0/CFB0/D1B0`; sema slot selection from table `sema_idx`; S2R SR0/SR1/SR3 smoke; optional method `0x34a8` investigation; document sema schema in traces.

---

## Method

1. **Phase 1 targeted dumps** — glcore/eglcore/cuda class ladders, sema tables, literal SPA/MME/CE sites → `objdump`/xxd (`disasm/`, 32 files).
2. **Phase 2 focus analysis** (`pass14_focus_analysis.py`, ~83s) — sema/MME/QMD/SPH/SASS/channel-prep/invalidate/video/literal-header notes.
3. **Phase 3 deep follow-up** (`notes/pass14_deep_followup.md`, ~121s) — refined sema schema, method descriptors, ELF-skipped QMD/SPH, S2R SR map, MME ngrams, class ladder probes, CE/video, mesa priorities.
4. **Phase 4 full mine** (`pass14_mine.py`) — 22 priority libs, per-lib JSON for all engines + crosslib summary.
5. **Manual synthesis** — cross-check pass7–13, open-gpu-doc, mesa `nv_*` emitters.

**Reconfirmed limitation:** Pushbuffer method headers are almost always **runtime-constructed**. Trust: (a) imm method offsets/indices, (b) rodata class ID arrays, (c) rare literal `0x2001xxxx` headers, (d) rodata sema config tables + method-descriptor records, (e) tight method n-grams, (f) gpucomp SASS frequency (noisy but directional), (g) open-gpu-doc / OGKM headers.

**New pass14 limitation note:** Naive QMD/SPH scans of whole binaries hit ELF/program headers at offset 0 — always start refined scans after ~1MB or require internal dim/mask heuristics.

---

## 1. Class ladders (pass14 realignment)

### 1.1 Primary glcore rodata (DMA/COMPUTE/3D unchanged)

| Engine | File offset | Ladder (newest→oldest in array) |
|--------|-------------|----------------------------------|
| DMA_COPY `*B5` | `0x11bb600` | `CAB5 C9B5 C8B5 C7B5 C6B5 C5B5 C3B5 C1B5 C0B5 B0B5` |
| COMPUTE `*C0` | `0x11bb640` | `CEC0 CDC0 CBC0 C9C0 C7C0 C6C0 C5C0 C3C0 C1C0 C0C0 B1C0 B0C0` |
| 3D `*97` | `0x11bb680` | `CE97 CD97 CB97 C997 C797 C697 C597 C397 C197 C097 B197 B097` |

### 1.2 GPFIFO / video region (pass14 probe — may be multi-ladder overlap)

Pass13 listed GPFIFO at `0x1238b70` as `CA6F…906F`. Pass14 dword probe at that offset shows **partial / overlapping** class runs (video classes bleed in depending on alignment):

| Probe offset | Classes seen |
|--------------|--------------|
| `0x1238b50` | probe for GPFIFO head |
| `0x1238b70` | `C36F C06F B06F A06F A26F 906F` then `C5B7 C4B7` (NVENC bleed) |
| `0x1238b90` | continuation |
| `0x1238bb0`/`0x1238bb4` | NVENC `C8B7 C9B7 CEB7 CFB7 D1B7` + NVDEC start |
| `0x1238be8`/`0x1238bf0`/`0x1238c00` | NVDEC incl. **newer** `CDB0 CEB0 CFB0 D1B0` |

**Pass14 NVDEC ladder extension (actionable):**

| Class | Notes |
|-------|-------|
| `C4B0 C6B0 C7B0 C9B0` | pass13 partial |
| **`CDB0 CEB0 CFB0 D1B0`** | **NEW pass14** — add to mesa NVDEC alts (newest-first with existing `CBB0 C8B0 C0B0 B0B0 A0B0`) |

Also saw `C6FA C7FA` near NVDEC region — possible auxiliary class family (display/overlay?); defer unless silicon fails class alloc.

eglcore mirrors glcore ladders at pass13 offsets (`0xfa5ee0` DMA, etc.) — unchanged.

**No DMA/COMPUTE/3D ladder changes required.**

---

## 2. Host semaphore — pass14 schema refinement

### 2.1 Structured sema config table @ glcore `0x11e30c0`

Leading dword `0x20` (entry count / table size descriptor), then **8-dword stride** records:

| +off | exec | tag_a | tag_b | sema_idx | Slot |
|------|------|-------|-------|----------|------|
| `+0x10` | `0x1004` | 4 | 2 | `0x12` | SEMAPHORE**C** |
| `+0x30` | `0x1002` | 4 | 2 | `0x11` | SEMAPHORE**B** |
| `+0x50` | `0x0804` | 4 | 2 | `0x11` | B |
| `+0x70` | `0x0802` | 4 | 2 | `0x10` | SEMAPHORE**A** |
| `+0x90` | `0x1004` | 5 | 3 | `0x12` | C (alt tags) |
| `+0xb0` | `0x1002` | 5 | 3 | `0x11` | B |
| `+0xd0` | `0x0804` | 5 | 3 | `0x11` | B |
| `+0xf0` | `0x0802` | 5 | 3 | `0x10` | A |
| `+0x110` | `0x1004` | 0 | 1 | `0x08` | non-standard idx |
| `+0x130` | `0x1002` | 0 | 1 | `0x07` | non-standard |
| `+0x140` | **`0x1001`** | 0 | 1 | `0x1d` | primary execute (different shape) |

**Field meanings (refined pass14):**

| Field | Values seen | Interpretation |
|-------|-------------|----------------|
| `exec` | `0x1001/02/04`, `0x0802/04` | SEMAPHORED operation/execute payload |
| `tag_a` | 4, 5, 0 | engine/channel variant or reduction width class |
| `tag_b` | 2, 3, 1 | operation subtype (release/acquire/reduction flavor) |
| `sema_idx` | `0x10..0x13` (A..D), sometimes `0x07/08/1d` | which SEMAPHORE* method gets the execute dword |

**Mesa implication:** Current mesa always programs SEMAPHORE A/B/C/D as a fixed ABCD block with execute in D (or similar). Table shows driver sometimes targets **C with 0x1004**, **B with 0x1002**, **A with 0x0802** — slot selection is part of the sema mode, not always D. Low priority to implement slot-aware sema unless silicon fails; document for bring-up debug.

**Execute priority ladder (pass13+14, unchanged order, refined rationale):**

1. `0x1001` — primary (table +0x140 + cuda records)
2. `0x1002` — acquire/alternate (table + pass12)
3. `0x1004` — sibling of 0x1002 (table)
4. `0x1000` — method-descriptor tables (many `tag=0x6b/0x68` records)
5. `0x0804` / `0x0802` — half-word family (table)
6. `0x0002` — vdpau / weak path
7. `0x0001` — weak / payload noise

### 2.2 Method-descriptor tables (new pass14 systematization)

Pattern: `1, class_tag, method_idx, 0, 9, aux, imm, …`

glcore has **hundreds** of such records across class tags `0x30/0x33/0x35/0x68/0x6b/…`. Many pair sema method indices (`0x10/11/12`) with imm `0x1000` — these are **method property tables** (not direct emit streams), explaining pass12 noise where imm sema values appeared far from SEMAPHORED sites.

Trust sema execute values from:
1. Tight sema-proximate x86 sites (pass13)
2. Structured config table @ `0x11e30c0` (pass13/14)
3. cuda sema records with `0x1001` + `0x40` (SEMAPHOREA offset) co-located (pass13)

---

## 3. MME — upload order confirmed; ISA still open

### 3.1 Method hit counts (glcore pass14 probe)

| Method | off hits | idx hits | literal hdr |
|--------|----------|----------|-------------|
| SCRATCH `0x3800` | 24 | 75 | 2 |
| CALL_MACRO `0x385c` | 1 | 1 | 0 |
| CALL_DATA `0x3860` | 8 | 12 | 0 |
| RAM_PTR `0x3880` | 10 | 37 | 0 |
| RAM_DATA `0x3884` | 0 | 0 | 0 |
| START_PTR `0x3888` | 5 | 1 | 0 |
| START_DATA `0x388c` | 1 | 8 | 0 |

RAM_DATA imm scarcity in static scan: data is **runtime-fed** (loop writing instruction RAM), not rodata constants. CALL is rare statically (macros invoked from dynamic paths).

### 3.2 Inferred bring-up order (pass7–14 consistent)

1. `SET_MME_SHADOW_SCRATCH` (optional zero/init)
2. `SET_MME_INSTRUCTION_RAM_PTR` = base/slot
3. `SET_MME_INSTRUCTION_RAM_DATA` × N (insn dwords; runtime)
4. `SET_MME_START_ADDRESS_RAM_PTR` = macro_id
5. `SET_MME_START_ADDRESS_RAM_DATA` = insn RAM offset for that macro
6. At draw/dispatch: `CALL_MME_MACRO(id)` + `CALL_MME_DATA(args…)`

**Mesa:** `upload_only` / path_c gated on `!is_stub_end_only` remains correct. Do not CALL stub END-only macros on silicon.

### 3.3 Real MME ISA — still not recovered from static RE

Rodata END-terminated stream heuristics hit ELF/`0x00000001` noise at file start. Method-proximity windows capture x86 code, not MME insn words.

**Next steps for pass15 / silicon:**
- Live trace `SET_MME_INSTRUCTION_RAM_DATA` payloads during GL/VK init
- Entropy scan for compressed macro blobs in glcore/eglcore data segments
- Cross-ref Fermi/Kepler MME public encoding (limited; Turing+ may differ)

---

## 4. SASS / SPH (gpucomp pass14)

### 4.1 Named opcode frequency (3M insn scan, approximate matchers)

| Opcode | Count (approx) | Notes |
|--------|----------------|-------|
| MEMBAR | 27771 | likely over-matched (pattern collision) |
| LOP3 | 10328 | logic — real |
| MUFU | 9203 | multi-func — real |
| WARPSYNC | 2074 | |
| TLD / TEX / TMML | 1607/166/388 | texture family |
| S2R | 537 | special register read |
| MOV | 450 | |
| LDG / STG | 276/37 | global mem |
| EXIT | 180 | |
| BAR / BRA | 438/409 | |
| LDC / ULDC | 202/101 | const cache |
| NOP | 1042 | |

Treat absolute counts as directional; matcher has false positives (MEMBAR especially). Relative presence of S2R/MOV/EXIT/STG/LDG/TEX confirms mesa smoke opcode set is in the right family.

### 4.2 S2R SR index candidates

Dominant SR values in S2R-matched instructions (multiple field-shift probes):

| SR | Likely meaning (Maxwell+ public / nouveau) | Mesa smoke priority |
|----|---------------------------------------------|---------------------|
| `0x00` | SR_LANEID / TID.X (context-dependent) | **#1** (tick139) |
| `0x01` | TID.Y | #2 |
| `0x02` | TID.Z | #3 |
| `0x03` | CTAID.X / NTID | #4 |
| `0x04`/`0x05` | CTAID.Y/Z | optional |
| `0x0f`/`0x10` | CLOCK LO/HI | debug only |
| `0x48` | SMID / thread mask family | optional |
| `0x50`–`0x54` | EQ/LT/LE/GT/GE masks | vote/ballot |

### 4.3 Short kernel patterns (gpucomp)

Common EXIT-containing micro-sequences (matcher-based):
- `S2R … MOV … EXIT` — tick139 mode 3
- `MOV … EXIT` — tick121+ graphics/compute smoke
- `NOP … EXIT` — minimal valid shader
- `S2R … STG … EXIT` — store-tid smoke (needs valid addr in reg)
- `LDG … STG … EXIT` — mem copy kernel stub

**Mesa:** Keep modes 0–3; consider mode 4 = NOP+EXIT only for minimal SPH validation.

### 4.4 SPH / QMD static templates

Refined scans (skip first ~1MB, require dim runs + mask fields) still produce candidates but **high false-positive rate**. QMD/SPH are primarily **runtime-constructed** in cuda/gpucomp from NIR/PTX/SASS pipelines, not stored as complete templates in rodata.

Trust mesa QMD field layout from open-gpu-doc / prior pass11–12 annotated fields; refine only from live QMD dumps on silicon.

SPH 20-dword layout: mesa `nv_sph.h` builders remain authoritative; pass14 did not overturn type/version encoding.

---

## 5. Channel-prep / bring-up sequences

### 5.1 glcore key-method ngram5 (tight span)

Dominant patterns include:
- Video/NVDEC control interleaved with SET_OBJECT + SEMA_A (high frequency — method index collision with other engines possible; treat cautiously)
- `LAUNCH_DMA` + `SEMA_D` + `SEMA_A` — CE completion sema (matches mesa G1 sema-after-copy)
- `SEMA_A` × N + `SEMA_D` — sema ABCD block construction
- `SEMA_A` + `WFI` — host wait path
- `SPA_3D_off` near NVDEC/control region — SPA version in channel init (matches G2/G3 channel_prep)

### 5.2 cuda key-method ngram5

Very high `SEMA_A` ↔ `NVDEC_CTRL` alternation — **method index aliasing** (`0x100` = 0x400>>2 appears in many engines). Do not over-interpret as actual NVDEC in cuda compute paths.

Trust cuda sema records (`0x1001` + `0x40`) and explicit PCAS/QMD method offs (`0x2e00`, `0x2f00`) more than index-only n-grams.

### 5.3 Recommended channel_prep order (mesa alignment — pass12/13/14)

**G1 CE:** SET_OBJECT → (optional phys/pitch setup) → LAUNCH_DMA → sema ABCD (`0x1001` primary)
**G2 Compute:** SET_OBJECT → SET_SPA_VERSION → local/shared mem windows → invalidate sextet → (inline QMD or PCAS) → sema/WFI
**G3 3D:** SET_OBJECT → SET_SPA_VERSION → program region → MME upload_only (if ready) → invalidate → render/ZT/CT bind → draw/MME_CALL
**G4 Video:** SET_OBJECT → control/pic_setup/bufs/status → execute → sema

---

## 6. Invalidate caches + constant buffers

### 6.1 Invalidate

Index-imm ordered runs in .text are weak/noisy. **Keep open-gpu-doc / class header order:**

**3D (`0x2c80+`):** SHADER_CACHES → TEX_DATA → SAMPLER → SHADER_DATA → INSTRUCTION → CONSTANT
**Compute (`0x2c00+`):** SHADER_CACHES → TEX_HEADER → TEX_SAMPLER → TEX_DATA → SHADER_DATA → INSTRUCTION → CONSTANT

Pass14 post-launch invalidate in G2 (tick139) remains justified.

### 6.2 Constant buffer bind order

Method offs `0x2800..0x2814`:
`SEL_A` → `SEL_B` → `SEL_C` → `BIND_GROUP_CONSTANT_BUFFER` → `LOAD_CONSTANT_BUFFER_OFFSET` → `LOAD_CONSTANT_BUFFER`

Mesa should emit selector triple before bind/load; verify Gallium/Vulkan UBO paths.

---

## 7. CE / Video

### 7.1 CE LAUNCH_DMA

Literal header `0x200181c0` confirmed (pass13/14). Nearby small imms include `0x1/0x2/0x5/0x9/0x11` style bit flags — launch mode (pipelined/flush/semaphore/reduction). Mesa `nv_copy_methods.h` should preserve multi-bit launch payload, not only `1`.

### 7.2 NVDEC / NVENC

glcore/vdpau hit counts confirm methods `0x400..0x41c` (NVDEC control/pic/bufs/status/display) and NVENC `0x700/0x704/0x708/0x718/0x71c/0x720` (headers/out_*) are live. Pass10 `OUT_ST 0x718` / `OUT_BS 0x71c` still valid.

Execute path: NVDEC `0x300` EXECUTE present in method map; wire if not already in smoke_slice.

---

## 8. Literal method headers (glcore census)

Top `0x2001xxxx` indices (pass14 full scan) — mostly **unknown** methods (runtime tables still dominate construction):

| idx | moff | count | known? |
|-----|------|-------|--------|
| `0xd2a` | `0x34a8` | 41 | **investigate** — likely 3D method |
| `0x000` | `0x0` | 31 | noise/NOP-ish |
| `0x586` | `0x1618` | 25 | near vertex array region |
| `0x0c0` | `0x300` | 17 | multi-engine (EXECUTE/APP_ID family) |
| `0x680` | `0x1a00` | 6 | **SPA_CMP** (known) |
| `0x1c0` | `0x700` | via other sites | **LAUNCH_DMA** (known) |
| `0xe00` | `0x3800` | via sites | **MME_SCRATCH** (known) |

Pass15: map `0x34a8` via open-gpu-doc class headers for `CE97`/`C997` 3D.

---

## 9. Mesa implementation priorities (pass14)

| Pri | Finding | Action |
|-----|---------|--------|
| **P0** | Sema table schema (exec+tags+sema_idx) | Document in traces; optional slot-aware sema emitter |
| **P0** | `0x1001` primary confirmed | Keep ladder #1 (tick138 done) |
| **P1** | NVDEC classes `CDB0/CEB0/CFB0/D1B0` | Add to `nv_device_info` / class ladder alts |
| **P1** | S2R SR0 dominant; SR1/SR3 useful | Extend SASS smoke SR set |
| **P1** | NOP+EXIT kernel pattern | Add minimal shader mode |
| **P2** | MME upload_only / no stub CALL | Keep path_c gate |
| **P2** | CB SEL×3 → BIND → LOAD_OFF → LOAD | Audit UBO emit order |
| **P2** | Method `0x34a8` high literal count | Class-header lookup |
| **P3** | Real MME/QMD/SPH from live trace | pass15 + silicon |
| **P3** | Invalidate order | Keep open-gpu-doc |

---

## 10. Cross-pass stability

| Area | pass11 | pass12 | pass13 | pass14 |
|------|--------|--------|--------|--------|
| Class ladders DMA/3D/CMP | stable | stable | stable | stable |
| Sema execute ladder | 0x1001/02/00/02 | +tight filter | +0x1004/0804/0802 rodata | +schema (tags+sema_idx) |
| MME order | upload+call | upload_only option | path_c gate | confirmed; ISA still open |
| SASS smoke | EXIT/MOV | +LDG/STG | +S2R | +SR map, kernel patterns |
| QMD/SPH templates | partial | partial | candidates | ELF-skip; still runtime-built |
| NVDEC classes | partial | partial | C4B0..C9B0 | +CDB0..D1B0 |

---

## 11. Artifact index

| Path | Content |
|------|---------|
| `pass14_mine.py` | Full 22-lib miner |
| `pass14_focus_analysis.py` | Focused multi-engine notes |
| `pass14_targeted_disasm.sh` | objdump/xxd windows |
| `notes/pass14_focused_analysis.md` | Combined focus notes |
| `notes/pass14_deep_followup.md` | Refined schema/SR/class/priorities |
| `notes/pass14_sema_deep.md` | Sema table + 235 structured records |
| `notes/pass14_mme_deep.md` | MME sites + macro candidates |
| `notes/pass14_sass_deep.md` | gpucomp SASS stats |
| `notes/pass14_qmd_sph_deep.md` | QMD/SPH candidates (incl. ELF noise) |
| `notes/pass14_channel_prep.md` | ngram5 bring-up |
| `notes/pass14_video_ce.md` | Video/CE imms |
| `notes/pass14_literal_headers.md` | 0x2001xxxx census |
| `disasm/` | 32 targeted snippets |
| `tables/` | Per-lib summaries + crosslib |
| `mme/ sass/ qmd/ sph/ sema/ sequences/ …` | Per-lib miner JSON |

---

## 12. Pass15 / next RE targets

1. **Live MME RAM_DATA capture** on silicon during context init (highest value for real ISA)
2. **Map method `0x34a8`** (idx `0xd2a`) in open-gpu-doc / class headers
3. **Entropy scan** glcore/eglcore for compressed MME/SASS blobs
4. **QMD live dump** after cuda/mesa compute launch; annotate field offsets vs open-gpu-doc
5. **Sema slot-aware emitter** experiment if sema-only tests fail on HW
6. **NVDEC class ladder** silicon try with `D1B0` first
7. Continue full miner completion + mesa_gap cross-ref when all 22 libs finished

---

*End pass14 report. Cross-ref pass8–13 under `mesa/src/nvidia/traces/HW_MODEL_PASS*.md` and `/tmp/nvidia-reveng-pp-v2/re_pass{10,11,12,13,14}/`.*
