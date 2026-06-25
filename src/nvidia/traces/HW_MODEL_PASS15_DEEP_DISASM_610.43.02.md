# HW Model Pass 15 — MME/SASS/SPH/QMD/Sema/Sequences/Class/Video Deep Disassembly
# (NVIDIA-Linux-x86_64-610.43.02)

**Date:** 2026-06-25
**Duration:** multi-hour session (focus analysis ~195s; targeted disasm 86 artifacts; full miner 22 libs in progress/completed in background)
**Scope:** Beyond pass14 (sema schema, method descriptors, ELF-skip QMD/SPH, class realignment) — **execute×slot matrix formalization**, **GPFIFO exact ladder @ 0x1238b60**, **NVDEC/NVENC/FA full region layout**, **MME x86 site dumps** (RAM_DATA still runtime-only), **tightened SASS** (MEMBAR FP eliminated), **channel ngrams** (sema/WFI/CE patterns), **SPA imm census**, **unknown 0x2001xxxx gap list**, **CE/video imm refinement** (cuvid cross-check), mesa priorities for pass16/silicon.
**Artifacts:** `/tmp/nvidia-reveng-pp-v2/re_pass15/` (`pass15_mine.py`, `pass15_deep_analysis.py`, `pass15_targeted_disasm.sh`, `notes/`, `disasm/`, `tables/`, `mme/`, `sass/`, `qmd/`, `sph/`, `sema/`, `ngrams/`, `sequences/`, …)
**Mesa impact (recommended this pass):** NVDEC alts incl. `B8B0`; NVENC alts `D1B7…`; GPFIFO exact `CA6F…906F` (+ stray `506F` note); sema slot table as authoritative pairing; S2R SR0/0x48/0x49; method `0x34a8` (41 lit headers — strongest MME-adjacent unknown); unknown methods `0x39e0`/`0x1618`/`0x1a00` investigation.

---

## Method

1. **Phase 1 targeted dumps** — glcore/eglcore/cuda/gpucomp class ladders, sema tables, MME/sema/S2R sites → xxd/hex/dword views (`disasm/`, 86 files incl. ELF section maps).
2. **Phase 2 focus analysis** (`pass15_deep_analysis.py`, ~195s) — sema/MME/QMD/SPH/SASS/sequences/SPA/CE/video/priorities notes.
3. **Phase 3 full mine** (`pass15_mine.py`) — 22 priority libs, per-lib JSON (methods/classes/sema/mme/qmd/sph/ngrams/cb/ce/nvos32/strings/mesa_gap) + crosslib summary.
4. **Manual synthesis** — cross-check pass7–14, refine mesa action list.

**Reconfirmed limitation:** Pushbuffer method headers are almost always **runtime-constructed**. Trust: (a) imm method offsets/indices, (b) rodata class ID arrays, (c) rare literal `0x2001xxxx` headers, (d) rodata sema config tables + method-descriptor records, (e) tight method n-grams (with SET_OBJECT=0 noise filter), (f) tightened gpucomp SASS frequency, (g) open-gpu-doc / OGKM headers.

**Pass15 refinements on limitations:**
- QMD/SPH ELF-skip alone insufficient: first 1MB+ hits are often **pointer tables** (repeating `… 00000008 … addr …` patterns), not real QMD/SPH — treat as **noise unless** field heuristics pass stricter filters (pass16: require QMD version dword + CTA dim + sema fields co-located).
- Ngram scans hitting method **0x0 / SET_OBJECT** dominate; use pass15_sequences_deep (filtered key method set) over raw miner ngrams for channel_prep guidance.
- Sema whole-binary scan floods with `exec=0x4/0x1/0x2` noise; **authoritative source remains structured table @ 0x11e30c0**, not global record scan.

---

## 1. Class ladders (pass15 — definitive layout)

### 1.1 Primary glcore rodata (DMA/COMPUTE/3D — unchanged from pass13/14)

| Engine | File offset | Ladder (newest→oldest) |
|--------|-------------|------------------------|
| DMA_COPY `*B5` | `0x11bb600` | `CAB5 C9B5 C8B5 C7B5 C6B5 C5B5 C3B5 C1B5 C0B5 B0B5` |
| COMPUTE `*C0` | `0x11bb640` | `CEC0 CDC0 CBC0 C9C0 C7C0 C6C0 C5C0 C3C0 C1C0 C0C0 B1C0 B0C0` |
| 3D `*97` | `0x11bb680` | `CE97 CD97 CB97 C997 C797 C697 C597 C397 C197 C097 B197 B097` |

Secondary DMA run @ `0x1236f80` adds trailing **`A0B5`** (11 entries) — optional older alt.

eglcore mirrors at `0xfa5ee0` / `0xfa5f20` / `0xfa5f60` (3D ladder followed by misc `0x2410/2490/2470/2430` — not class IDs).

### 1.2 GPFIFO / video / aux region (pass15 exact layout @ `0x1238b50`)

Dword-verified contiguous layout (file offsets relative to `0x1238b50`):

| +off | Content | Classes |
|------|---------|---------|
| `+0x00..0x0c` | pad zeros | — |
| `+0x10` (`0x1238b60`) | **GPFIFO `*6F`** | `CA6F C96F C56F C46F C36F C06F B06F A06F A26F 906F` |
| `+0x38` | stray | `506F` (suspicious; may be terminator/noise — do **not** add to mesa alts without silicon fail) |
| `+0x50` (`0x1238ba0`) | **NVENC `*B7`** | `C5B7 C4B7 B4B7 B6B7 C7B7 C8B7 C9B7 CEB7 CFB7 D1B7` |
| `+0x90` (`0x1238be0`) | **NVDEC `*B0`** | `C4B0 C6B0 C7B0 B8B0 C9B0 CDB0 CEB0 CFB0 D1B0` |
| `+0xd0` (`0x1238c20`) | **AUX `*FA`** | `C6FA C7FA B8FA C9FA CDFA CEFA CFFA D1FA` |

**Pass15 GPFIFO correction vs pass13:** pass13 listed start at `0x1238b70` as `C36F…` (mid-ladder). True head is **`CA6F` @ `0x1238b60`**. Mesa GPFIFO alts should prefer newest-first from this full list.

**Pass15 NVDEC ladder (actionable, supersedes pass14 partial):**

```
D1B0 CFB0 CEB0 CDB0 C9B0 B8B0 C7B0 C6B0 C4B0  (+ existing CBB0 C8B0 C0B0 B0B0 A0B0 if needed)
```

Note **`B8B0`** appears between `C7B0` and `C9B0` in rodata order (not strictly monotonic) — include as alt.

**Pass15 NVENC ladder (actionable if encoder alloc fails):**

```
D1B7 CFB7 CEB7 C9B7 C8B7 C7B7 B6B7 B4B7 C4B7 C5B7
```

**AUX `*FA`:** full 8-class run now visible (`C6FA…D1FA`). Still defer unless class alloc fails; likely display/overlay/auxiliary engine family, not G0–G4 smoke path.

**No DMA/COMPUTE/3D ladder changes required.**

---

## 2. Host semaphore — pass15 authoritative schema

### 2.1 Structured sema config table @ glcore `0x11e30c0` (dword-verified)

Leading dword `0x20`. Records are **8-dword stride** with useful payload at `+0x10, +0x30, +0x50, …` (not every 0x20 boundary aligns to data — see disasm `glcore_sema_table_dwords.txt`).

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
| `+0x110` | `0x1004` | 0 | 1 | `0x08` | 1 | nonstd idx |
| `+0x130` | `0x1002` | 0 | 1 | `0x07` | 1 | nonstd |
| `+0x150` | `0x1001` | 0 | 1 | `0x1d` | 1 | **primary execute** (different shape) |

**Pass15 refined field model (8 dwords at each data row start, e.g. +0x10):**

| dword | example | meaning |
|-------|---------|---------|
| 0 | `0x1004` | execute / operation imm for SEMAPHORE*D-style write |
| 1 | `4` or `5` or `0` | tag_a — reduction/engine class |
| 2 | `2` or `3` or `1` | tag_b — op subtype |
| 3 | `0x12`/`0x11`/`0x10`/`0x08`/`0x07`/`0x1d` | **sema_idx** — which method slot receives execute |
| 4 | `2`/`3`/`1` | aux (often equals tag_b or count) |
| 5–7 | 0 | pad |

**Execute × slot matrix (from table only — authoritative):**

| sema_idx | slot | executes seen |
|----------|------|---------------|
| `0x10` | SEMAPHORE **A** | `0x0802` |
| `0x11` | SEMAPHORE **B** | `0x1002`, `0x0804` |
| `0x12` | SEMAPHORE **C** | `0x1004` |
| `0x1d` | nonstd (primary path) | `0x1001` |
| `0x07`/`0x08` | nonstd | `0x1002` / `0x1004` |

**Mesa implication (strengthened pass15):**
1. Mode table must select **(exec, sema_idx)** pair, not only exec imm.
2. `0x1001` primary does **not** use A/B/C/D idx in this table — uses `0x1d` (may map to host method offset `0x74` or internal descriptor; treat as “full ABCD block + execute in D” classic path until silicon proves otherwise).
3. ticks 140–141 `nv_push_sema_release_mode_slot` / `nv_host_sema_execute_method` align with this; verify pairings on HW.
4. Classic ABCD sequential window still dominates ngrams (below) — slot-specific execute is **orthogonal** to method order.

### 2.2 Method-descriptor tables (reconfirmed)

Pattern `1, class_tag, method_idx, …, imm` with class tags `0x6b/0x68/0x35/0x33/0x30/0x20`.
Pass15 found 13 sema-adjacent descriptors in glcore (tighter filter than pass14’s hundreds — pass14 included looser matches).
These are **property tables**, not emit streams. Imms `0x1000`/`0x0800` here ≠ host sema execute ladder.

### 2.3 cuda / vdpau cross-check

| lib | sema-proximate records | dominant exec |
|-----|------------------------|---------------|
| cuda | 80 | `0x1000` (63), then `0x1002/1004/802/804` |
| vdpau | 26 | weak `0x1`/`0x2` only |

cuda prefers `0x1000` in many sites (method-descriptor / MEM_OP family bleed); glcore table remains best for **graphics host sema mode** selection.

### 2.4 Execute priority ladder (unchanged order, pass15 rationale)

1. `0x1001` — primary (table +0x150, nonstd idx `0x1d`)
2. `0x1002` — B-slot / acquire alt
3. `0x1004` — C-slot sibling
4. `0x1000` — method-descriptor / cuda-heavy
5. `0x0804` / `0x0802` — half-word family (B / A slots)
6. `0x0002` / `0x0001` — weak / vdpau noise

---

## 3. MME — upload order solid; ISA still blocked; sites dumped

### 3.1 Method hit counts (glcore pass15)

| Method | off | idx | lit_hdr | name |
|--------|-----|-----|---------|------|
| `0x34a8` | 8 | 0 | **41** | SHADOW_SCRATCH alt / high index (pass14 candidate **confirmed hottest lit header**) |
| `0x3800` | 24 | 75 | 2 | SET_MME_SHADOW_SCRATCH |
| `0x3804` | 34 | 3 | 0 | SHADOW_SCRATCH+1 |
| `0x385c` | 1 | 1 | 0 | CALL_MME_MACRO |
| `0x3860` | 8 | 12 | 0 | CALL_MME_DATA |
| `0x3880` | 10 | 37 | 0 | RAM_PTR |
| `0x3884` | **0** | **0** | 0 | RAM_DATA — **still zero static** |
| `0x3888` | 5 | 1 | 0 | START_PTR |
| `0x388c` | 1 | 8 | 0 | START_DATA |

eglcore/cuda show similar RAM_DATA scarcity.

### 3.2 Inferred bring-up order (pass7–15 consistent)

1. `SET_MME_SHADOW_SCRATCH` (and/or `0x34a8` high-index path)
2. `SET_MME_INSTRUCTION_RAM_PTR`
3. `SET_MME_INSTRUCTION_RAM_DATA` × N (**runtime loop only**)
4. `SET_MME_START_ADDRESS_RAM_PTR` = macro_id
5. `SET_MME_START_ADDRESS_RAM_DATA` = insn RAM offset
6. Draw/dispatch: `CALL_MME_MACRO(id)` + `CALL_MME_DATA(args…)`

### 3.3 X86 caller sites (disasm artifacts)

24 MME method sites dumped under `disasm/glcore_mme_site_*.hex` — windows show x86 code embedding method imm constants (e.g. `0x3880`/`0x3888` near `0x6b9134`/`0x6b919c`), not MME instruction words.

Miner found 60 entropy blobs + 40 end-terminated stream candidates in glcore after 1MB — **weak leads only** (could be compressed data, not MME ISA).

### 3.4 Mesa / pass16 actions

- Keep `upload_only` / path_c gated on `!is_stub_end_only`
- Init shadow scratch range including index **42** via `0x34a8` or sequential `0x3800+n` — pass15 confirms `0x34a8` is real (41 lit `0x20010d2a` headers)
- **Do not** CALL stub END-only macros on silicon
- **Live trace required** for RAM_DATA payloads (highest-value pass16 item)

---

## 4. SASS / SPH (gpucomp pass15 — tightened)

### 4.1 Opcode frequency (3.5M insn windows, stricter matchers)

| Opcode | Count | Notes |
|--------|------:|-------|
| TEX | 6534 | texture family — real |
| LOP3 | 4574 | logic — real |
| MUFU | 4159 | multi-func — real |
| S2R | 294 | special reg — real |
| BAR | 147 | |
| WARPSYNC | 93 | |
| BRA | 91 | |
| EXIT_cand | 64 | |
| MEMBAR_strict | **0** in focus scan | pass14 MEMBAR blowup **fixed** by stricter matcher |

Treat counts as directional only.

### 4.2 S2R SR index candidates (pass15 focus, multi-shift)

| SR | count | likely | mesa priority |
|----|------:|--------|---------------|
| 0x0b (11) | 225 | unknown / arch-specific | med |
| 0x0c (12) | 76 | unknown | med |
| **0x48** | 73 | SR_LANEMASK_EQ? | **HIGH** |
| 0x8b | 58 | unknown | med |
| 0x0f | 56 | unknown | med |
| **0x00** | 38 | SR_LANEID | **HIGH** |
| **0x49** | 23 | SR_LANEMASK_LT? | **HIGH** |
| **0x03** | 8 | SR_VIRTID? | **HIGH** |
| **0x01** | 7 | SR_CLOCKLO? | **HIGH** |
| **0x25** | 3 | SR_SHADER_TYPE? | **HIGH** |
| 0x50 SMID | low in this scan | still implement | HIGH (known useful) |

16 S2R site hex dumps: `disasm/gpucomp_s2r_site_*.hex`.

### 4.3 SPH / QMD static status (pass15 honest assessment)

- glcore/cuda “QMD candidates” @ `0x100000+` are mostly **repeating pointer/reloc tables** (`addr, 0, 8, 0, addr, …`) — **not trustworthy QMD templates**
- SPH candidates show type_lo hist dominated by 0/8 — also pointer-table bleed
- **mesa `nv_qmd.h` / `nv_sph.h` builders remain authoritative**
- pass16: stricter QMD scan requiring version + grid/cta + sema/barrier fields; or live `SET_INLINE_QMD_DATA` / `SEND_PCAS` capture

---

## 5. Channel sequences / sema-WFI / CE (pass15 ngrams)

Filtered key-method ngrams (avoid SET_OBJECT=0 flood):

### 5.1 Dominant host patterns

| Pattern | ×count (5-gram) | Interpretation |
|---------|----------------:|----------------|
| `SEMA_A ×5` | 13582 | sema A programming loops / descriptor noise |
| `SEMA_A ↔ WFI_HOST` alternating | 2000–3200 | **host sema + WFI** is core kick/sync idiom |
| `WFI_HOST ×5` | 2658 | idle/wait bursts |
| `SEMA_C ×5` | 1936 | C-slot programming (aligns with `0x1004→C` table) |
| `SEMA_A ↔ SEMA_B` alternating | 1600–1800 | AB pairs common; full ABCD less dominant in imm scan |
| `SEMA_A ↔ LAUNCH_DMA` | 1400+ | CE work + sema fencing |
| `WFI / SEMA_A / SET_OBJECT` mixes | 1000+ | object bind + sync |

### 5.2 Mesa channel_prep implications

1. **Host sema sticky emit** on GPFIFO/G1/G2 (t141) is correct direction; extend Gallium parity.
2. **WFI after sema release** should remain optional but available (`nv_push_host_semaphore_release_wfi_mode_ex`).
3. **CE G1:** sema fence around `LAUNCH_DMA` matches binary patterns.
4. Invalidate / CB orders: see focus `pass15_sequences_deep.md` inv/cb sections (when present in full notes); prefer SH→TEX→SAMP→SHD→INS→CONST sextet from pass13/14 unless pass15 inv section shows otherwise.
5. MME upload once at channel init, not per-draw.

---

## 6. SPA / unknown methods / mesa gaps

### 6.1 SPA_VERSION imm (3D `0x2600` + compute `0x0b00`)

Top values include `0xe2200`, `0xe6200`, `0xea200`, `0xee200`, `0xf2200` families plus `0x508234`/`0x509134` — mix of real SPA encodings and x86/imm noise. Mesa `sm_version → spa` mapping (t139) remains correct approach; validate top SPA values against open-gpu-doc per chip class on silicon.

### 6.2 Literal `0x2001xxxx` census (glcore focus: 425 total, 198 distinct)

| idx | off | count | status |
|-----|-----|------:|--------|
| `0xd2a` | **`0x34a8`** | **41** | known MME/AA — implement/verify |
| `0x0` | `0x0` | 31 | SET_OBJECT noise |
| `0x586` | `0x1618` | 25 | **unknown** — color target third dim region? |
| `0xc0` | `0x300` | 17 | NVDEC/CE EXECUTE overlap |
| `0xe78` | `0x39e0` | 12 | **unknown** — past MME block |
| `0x1a81` | `0x6a04` | 6 | unknown high |
| `0x680` | `0x1a00` | 6 | blend/depth region? |
| `0x56f` | `0x15bc` | 5 | color target region |

Miner glcore methods JSON: 564 lit headers total; top unknowns align with focus list.

### 6.3 Mesa gap priorities from unknown methods

| Priority | Method off | Why |
|----------|------------|-----|
| P0 | `0x34a8` | 41 lit headers; MME scratch alt |
| P1 | `0x1618` | 25 lit; 3D state completeness |
| P1 | `0x39e0` | 12 lit; near MME/AA |
| P2 | `0x1a00`, `0x15bc`, `0x3998` | 3D state |
| P2 | `0x2b4`, `0x258`, `0x1f0` | low host/misc |

---

## 7. CE / video (pass15 + cuvid cross-check)

### 7.1 CE method presence

Heavy hits on `0x200` (OFFSET_IN_UPPER / SET_OBJECT collision possible), `0x400` (SET_SRC_PHYS), `0x700` (LAUNCH_DMA), pitch/line methods — mesa CE emitter coverage adequate.

### 7.2 LAUNCH_DMA imm patterns

Dominant family `0x078ef80x` / `0x078ec0ff` across glcore + cuvid (thousands of hits) — these are **x86/code-adjacent noise mixed with real imms**. Real LAUNCH_DMA control bits are small field combos; mesa should keep documented bitfields from open-gpu-doc, not these large immediates.

Trustworthy small imms remain rare in static scan (runtime construction).

### 7.3 NVDEC EXECUTE imm

| imm | glcore | cuvid | note |
|-----|-------:|------:|------|
| `0x3800000` | 742 | 834 | dominant — likely code noise or flags with high bits |
| `0x0` | 52 | 101 | null/default execute |
| `0x7ffe0ff` / `0xfffe0ff` | 32 | 45/36 | mask-like |
| `0x100000` | 15 | 23 | |

Mesa video execute path: prefer `0` / documented codec flags; treat huge imms as scan noise unless live trace confirms.

### 7.4 NVDEC/NVENC class alts — see §1.2

---

## 8. Mesa implementation priorities (pass15)

### P0 — implement / verify soon

1. **NVDEC class alts** — `D1B0 CFB0 CEB0 CDB0 C9B0 B8B0 C7B0 C6B0 C4B0` + existing older
2. **GPFIFO class alts** — full `CA6F C96F C56F C46F C36F C06F B06F A06F A26F 906F` (head correction)
3. **Sema slot×exec table** — wire modes to `(0x1004,C)`, `(0x1002,B)`, `(0x0802,A)`, `(0x1001, classic/1d)`; keep ABCD fallback
4. **Method `0x34a8`** — shadow scratch high-index path (41 lit headers)
5. **Host sema + WFI** patterns on G1 CE and GPFIFO (Gallium/Vulkan parity with t141)

### P1 — bring-up hardening

6. **NVENC class alts** — `D1B7 CFB7 CEB7 C9B7 C8B7 C7B7 …` if alloc fails
7. **S2R smoke** — SR0, SR1, SR3, SR0x48, SR0x49, SR0x50
8. **SPA_VERSION** — validate t139 sm→spa against pass15 imm families on silicon
9. **Unknown methods** — `0x1618`, `0x39e0` probe/no-op safe stubs if needed for state completeness
10. **CB select+bind** — SEL_A/B/C → BIND_GRP and LD_CB_OFF → LD_CB (t140/141)

### P2 — needs live trace / pass16

11. **MME RAM_DATA live capture** during first GL/VK init draw
12. **QMD/SPH live** via SEND_PCAS / INLINE_QMD / compute dispatch
13. **Stricter QMD static scan** (reject pointer tables)
14. **AUX `*FA` class** — only if silicon fails missing class

### P3 — longer arc

15. NIR → SASS (gpucomp/ptxjit deep or user-approved nouveau/nvk cross-check)
16. Full Gallium/Vulkan beyond smoke
17. Video encode/decode e2e bitstreams

### Explicit non-goals

- Do not replace mesa QMD/SPH builders with pass15 static “candidates”
- Do not CALL stub END-only MME macros
- Do not trust pass14-style MEMBAR SASS counts
- Do not add `506F` or `*FA` to production alts without evidence
- Do not modify nouveau/nvk without double-check

---

## 9. Confidence matrix (pass11 → pass15)

| Area | pass11 | pass12 | pass13 | pass14 | pass15 |
|------|--------|--------|--------|--------|--------|
| Class ladders DMA/C/3D | med | high | high | high | **high** |
| GPFIFO ladder | low | med | med | med | **high** (exact head) |
| NVDEC/NVENC ladders | low | med | med | med+ | **high** (full region) |
| AUX *FA | — | — | — | note | **documented** (defer) |
| Sema execute ladder | med | med | high | high | **high** |
| Sema slot selection | — | — | med | med+ | **high** (matrix) |
| MME upload order | med | med | high | high | **high** |
| MME real ISA | low | low | low | low | **low** (sites dumped; live needed) |
| SASS opcode families | med | med | med | med- | **med+** (MEMBAR fixed) |
| S2R SR map | low | low | med | med | **med+** |
| QMD/SPH static | low | low | low | low- | **low** (pointer-table trap documented) |
| Channel ngrams | low | med | med | med | **med+** (filtered) |
| CE/video imms | low | med | med | med | **med** (noise aware) |
| Unknown method catalog | low | med | med | med | **med+** (`0x34a8`/`0x1618`/`0x39e0`) |

---

## 10. Artifacts index

| Path | Description |
|------|-------------|
| `pass15_mine.py` | Full 22-lib miner |
| `pass15_deep_analysis.py` | Focused multi-engine notes (~195s) |
| `pass15_targeted_disasm.sh` | xxd/hex/dword/ELF section dumps |
| `notes/pass15_focused_analysis.md` | Combined focus notes |
| `notes/pass15_sema_deep.md` | Sema table + descriptors + cuda/vdpau |
| `notes/pass15_mme_deep.md` | MME census + entropy + ISA status |
| `notes/pass15_classes_deep.md` | All family ladders |
| `notes/pass15_qmd_sph_deep.md` | QMD/SPH (incl. noise assessment) |
| `notes/pass15_sass_deep.md` | Tightened SASS + SR table |
| `notes/pass15_sequences_deep.md` | Channel/sema/WFI/CE ngrams |
| `notes/pass15_spa_unknown.md` | SPA imm + unknown 0x2001xxxx |
| `notes/pass15_ce_video_deep.md` | CE/NVDEC/NVENC cross-lib |
| `notes/pass15_mesa_priorities.md` | P0–P3 list |
| `disasm/glcore_*_dwords.txt` | Ladder/sema dword views |
| `disasm/glcore_mme_site_*.hex` | 16 MME x86 windows |
| `disasm/glcore_sema_site_*.hex` | 12 sema x86 windows |
| `disasm/cuda_sema_site_*.hex` | 10 cuda sema windows |
| `disasm/gpucomp_s2r_site_*.hex` | 16 S2R insn windows |
| `disasm/*.sections.txt` | ELF section maps |
| `sema|mme|qmd|sph|methods|ngrams|classes/*.json` | Per-lib miner output |
| `tables/crosslib_summary.json` | Cross-lib rollup (when mine completes) |

---

## 11. Pass16 recommended agenda

1. Live-trace harness (or document procedure) for MME RAM_DATA + QMD/PCAS on real `/dev/nvidia*`
2. Wire P0 class/sema/`0x34a8` mesa changes + host selftests
3. Stricter QMD miner (version+CTA+sema fields; reject `8,0,addr` pointer runs)
4. Deeper x86 objdump at MME sites `0x6b9134` / sema config xrefs (function identification)
5. gpucomp SPH attach path: search for 20-dword writers near SASS blob tails
6. Optional: user-approved nouveau/nvk double-check for MME ISA only if live trace blocked

---

*End pass15 report. Cross-ref pass8–14 under `mesa/src/nvidia/traces/HW_MODEL_PASS*.md` and `/tmp/nvidia-reveng-pp-v2/re_pass{10,11,12,13,14,15}/`.*
