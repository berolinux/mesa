# HW Model Pass 16 — Strict QMD/SPH, MME/Unknown Methods, Sema Matrix, SASS/Sequences Deep Disassembly
# (NVIDIA-Linux-x86_64-610.43.02)

**Date:** 2026-06-25
**Duration:** multi-hour session (targeted disasm ~993 artifacts; focus analysis ~165s; supplement probes; full miner 22 libs ~146s)
**Scope:** Beyond pass15 (execute×slot matrix, GPFIFO/NVDEC/NVENC/FA exact region, MME sites, tightened SASS, filtered ngrams) — **corrected sema table alignment JSON**, **stricter QMD/SPH scoring** (pointer-table reject; honest weak-hit verdict), **MME/unknown method imm-site catalog** (0x34a8/0x1618/0x39e0/0x1a00 contexts + 0x3800↔0x39e0 coupling), **S2R multi-SR refined** (SR0/0x48 dominant; 0x49/0x01/0x03/0x4c), **channel ngrams with inv/CB**, **cross-lib class ladder consensus** (eglcore mirrors + cuda GPFIFO variant), **CE/video small-imm filter**, **mesa P0–P3 priorities for pass17/silicon/live trace**.
**Artifacts:** `/tmp/nvidia-reveng-pp-v2/re_pass16/` (`pass16_mine.py`, `pass16_deep_analysis.py`, `pass16_supplement.py`, `pass16_targeted_disasm.sh`, `notes/`, `disasm/` ~993 files, `tables/`, `sema/`, `qmd/`, `sph/`, `mme/`, `mme_isa/`, `sass/`, `classes/`, …)
**Mesa impact (recommended this pass):** Confirm/wire remaining P0 from pass15 (NVDEC/GPFIFO alts, sema modes, `0x34a8`, multi-SR S2R); treat `0x39e0` as **MME-adjacent post-config** (follows `0x3800` in static windows); do **not** promote pass16 QMD/SPH static candidates; live trace still required for RAM_DATA + real QMD/SPH.

---

## Method

1. **Phase 1 targeted dumps** (`pass16_targeted_disasm.sh`) — glcore/eglcore/cuda method imm sites (MME/unknown/sema), GPFIFO/video/aux/sema regions, gpucomp S2R windows, cuvid/encode class ladder auto-scan, objdump snippets → `disasm/` (~993 files), `x86_callers/`.
2. **Phase 2 focus analysis** (`pass16_deep_analysis.py`, ~165s) — sema/MME/QMD/SPH/SASS/sequences/SPA/CE/video/priorities notes.
3. **Phase 3 supplemental probes** (`pass16_supplement.py`) — **fixed sema row alignment** (pass15 data at +0x10/+0x30/…), exact header dword search, method site contexts, 0x3800↔0x39e0 coupling, inv/CB imm presence.
4. **Phase 4 full mine** (`pass16_mine.py`, ~146s) — 22 priority libs, per-lib JSON + `tables/pass16_crosslib_summary.json`.
5. **Manual synthesis** — cross-check pass7–15, refine mesa action list and confidence matrix.

**Reconfirmed limitation:** Pushbuffer method headers are almost always **runtime-constructed**. Trust: (a) imm method offsets/indices, (b) rodata class ID arrays, (c) rare exact header dwords if present, (d) rodata sema config tables + method-descriptor records, (e) tight method n-grams (SET_OBJECT=0 filtered), (f) tightened/heuristic gpucomp SASS, (g) open-gpu-doc / OGKM headers.

**Pass16 refinements on limitations:**
- **Sema table mis-alignment trap:** scanning 8-dword rows from offset 0 mis-reads leading `0x20` as exec; authoritative rows start at **+0x10** with **0x20 stride** (exec, tag_a, tag_b, sema_idx, aux, pad…).
- **QMD/SPH strict scoring still insufficient:** score≥6 hits cluster in reloc/descriptor regions (`addr, 0, small, 0, addr…` variants pass weak filters). **Do not** replace mesa `nv_qmd.h` / `nv_sph.h`.
- **`0x2001xxxx` exact header scan = 0** in this pass on glcore/eglcore/cuda for targets like `0x20010d2a`. Pass15 lit-header counts (41× for `0x34a8`) may reflect a different mask/encoding pass or intermediate tool; **imm method-offset hits remain reliable** (8× `0x34a8` in glcore). Broader `0x20xxxxxx` header-shaped dwords are noisy (x86/imm bleed) — use with caution.
- **RAM_DATA (`0x3884`)** imm still **0** in all three primary libs; egl/glcore may show 1 spurious lit_hdr_idx depending on mask — treat as **runtime-only** until live trace.

---

## 1. Class ladders (pass16 — reconfirmed + cross-lib)

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
| `+0x38` | stray | `506F` — **do not add** to mesa alts |
| `+0x50` (`0x1238ba0`) | **NVENC `*B7`** | `C5B7 C4B7 B4B7 B6B7 C7B7 C8B7 C9B7 CEB7 CFB7 D1B7` |
| `+0x90` (`0x1238be0`) | **NVDEC `*B0`** | `C4B0 C6B0 C7B0 B8B0 C9B0 CDB0 CEB0 CFB0 D1B0` |
| `+0xd0` (`0x1238c20`) | **AUX `*FA`** | `C6FA C7FA B8FA C9FA CDFA CEFA CFFA D1FA` |

### 1.3 Cross-lib consensus (pass16 new)

| Lib | Finding |
|-----|---------|
| **eglcore** | Full mirror of all ladders (DMA/C/3D/GPFIFO/NVENC/NVDEC/FA) at different rodata bases (`0xfa5ee0` primary; GPFIFO `0x1253d80`; NVENC/NVDEC `0x12689a0`/`0x12689e0`; FA `0x1254960`) — **identical class lists** |
| **cuda** | Only GPFIFO run found in auto-scan: `C96F C86F C56F C46F C36F C06F B06F A16F` @ `0x67c5180` — **missing CA6F head**, has **C86F** and **A16F** not in glcore ladder; treat as CUDA-specific subset/alt, not graphics primary |
| **cuvid / encode** | Auto ladder scan **0 runs** (classes likely constructed or in non-contiguous tables) — rely on glcore/eglcore rodata for video class alts |

**No DMA/COMPUTE/3D ladder changes required.** Mesa should ensure GPFIFO/NVDEC/NVENC alts from §1.2 are wired (pass15 P0).

---

## 2. Host semaphore — pass16 corrected authoritative schema

### 2.1 Structured sema config table @ glcore `0x11e30c0`

Leading dword `0x20`. **Data rows at +0x10, +0x30, +0x50, …** (0x20 stride), fields:

| dword | meaning |
|-------|---------|
| 0 | execute imm (`0x1004`, `0x1002`, `0x0804`, `0x0802`, `0x1001`, …) |
| 1 | tag_a (4/5/0) |
| 2 | tag_b (2/3/1) |
| 3 | **sema_idx** (0x12/0x11/0x10/0x08/0x07/0x1d) |
| 4 | aux (often equals tag_b) |
| 5–7 | pad zeros |

### 2.2 Full table (pass16 corrected — 11 rows)

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

JSON: `re_pass16/sema/pass16_sema_corrected.json`

### 2.3 Execute × sema_idx matrix (authoritative)

| sema_idx | slot | executes |
|----------|------|----------|
| `0x10` | SEMAPHORE **A** (method `0x40`) | `0x0802` |
| `0x11` | SEMAPHORE **B** (method `0x44`) | `0x1002`, `0x0804` |
| `0x12` | SEMAPHORE **C** (method `0x48`) | `0x1004` |
| `0x1d` | nonstd primary | `0x1001` |
| `0x07` | nonstd | `0x1002` |
| `0x08` | nonstd | `0x1004` |

### 2.4 Execute → sema_idx (inverse)

| execute | sema_idxs | mesa mode |
|---------|-----------|-----------|
| `0x1001` | `0x1d` | primary; emit classic ABCD + exec in D (`0x4c`) **or** slot-mapped `0x1d` path until silicon proves one |
| `0x1002` | `0x11`, `0x07` | prefer B / method `0x44` |
| `0x1004` | `0x12`, `0x08` | prefer C / method `0x48` |
| `0x0802` | `0x10` | A / method `0x40` |
| `0x0804` | `0x11` | B / method `0x44` |
| `0x1000` | (descriptors only) | avoid for graphics host sema |

### 2.5 Method-descriptor taxonomy (pass16)

80 sema-adjacent descriptors sampled in glcore (`1, class_tag, method_idx, …, imm`):
- class_tag hist: `0x6b`×16, `0x68`×14, `0x35`×16, `0x33`×16, `0x30`×15, `0x40`×2, `0x20`×1
- Dominant imm in descriptors: **`0x1000`** (property/MEM_OP family), not host execute ladder
- method_idx `0x10`/`0x11`/`0x12` appear as **descriptor indices**, not pushbuffer method offs — do not confuse with sema_idx in config table

### 2.6 Cross-lib tight-window noise (why table wins)

Whole-binary “sema-proximate” scans flood with `exec=0x1/0x2` (not real sema executes):

| lib | tight_records | real-ish execs present |
|-----|---------------:|------------------------|
| glcore | 23254 | `0x1000`×824, `0x802`×12, `0x1002`×12, `0x1004`×9 |
| cuda | 109887 | `0x1000`×4796, `0x804`×1325, `0x1004`×228 |
| opencl | 105484 | similar to cuda |
| vdpau | 131 | weak only |

**Authoritative source remains structured table @ 0x11e30c0**, not global record scan.

---

## 3. MME + unknown methods (pass16 imm-site depth)

### 3.1 Method hit table (imm counts — reliable)

| method | name / guess | gl imm | eg imm | cu imm |
|--------|--------------|-------:|-------:|-------:|
| `0x34a8` | SHADOW_SCRATCH_ALT / high_idx42? | **8** | 3 | 5 |
| `0x3800` | SET_MME_SHADOW_SCRATCH | 24 | 14 | **2110** |
| `0x3804` | SHADOW_SCRATCH+1 | 34 | 35 | 456 |
| `0x385c` | CALL_MME_MACRO | 1 | 1 | 20 |
| `0x3860` | CALL_MME_DATA | 8 | 5 | 83 |
| `0x3880` | RAM_PTR | 10 | 6 | 145 |
| `0x3884` | RAM_DATA | **0** | **0** | **0** |
| `0x3888` | START_PTR | 5 | 2 | 9 |
| `0x388c` | START_DATA | 1 | 1 | 19 |
| `0x39e0` | UNKNOWN_post_MME (MME-adjacent) | **9** | 6 | 44 |
| `0x3998` | UNKNOWN_near_MME | 1 | 1 | 3 |
| `0x1618` | UNKNOWN_color_target_region? | 6 | 8 | 11 |
| `0x1a00` | SPA_VERSION_3D / blend region? | 20 | 14 | 556 |
| `0x15bc` | UNKNOWN_color_target | 1 | 21 | 26 |
| `0x6a04` | UNKNOWN_high | 0 | 0 | 34 |

### 3.2 glcore imm site offsets (disasm artifacts available)

| method | example imm sites (file offs) |
|--------|-------------------------------|
| `0x34a8` | `0xdf0aa8`, `0xe9d9c8`, `0xf92894`, `0xfab984`, `0x2766e04`, … |
| `0x1618` | `0x6a3304`, `0xc573a4`, `0x10c752c`, `0x12579e0`, … |
| `0x39e0` | `0x6d7da0`, `0x1075a74`, `0x1075aec`, `0x11b855c`, `0x12ff620`, … |
| `0x3800` | `0x313520`, `0x31369c`, `0x63f980`, `0x6b8c90`, `0x1075974`, … |
| `0x3880` | `0x6b9134`, `0x933d08`, `0x13c121c`, … (near `0x3888` @ `0x6b919c`) |
| `0x3860` | `0x250518`, `0x251f78`, `0x373c1c`, `0x13fe268`, … |
| `0x385c` | `0x13c2138` (single CALL_MME_MACRO imm in glcore) |

Hex/dword windows: `disasm/glcore_meth_*_site_*.hex` / `.dws`; caller index: `x86_callers/meth_sites_glcore.txt`.

### 3.3 MME method imm ngrams (order within ~256B)

| Pattern | ×count | Interpretation |
|---------|-------:|----------------|
| `0x3804` → `0x3804` | 15 | sequential shadow scratch +1 loops |
| `0x3800` → `0x39e0` | **4** | **scratch then post-MME unknown** |
| `0x3860` → `0x3880` | 4 | CALL_MME_DATA then RAM_PTR (upload-ish) |
| `0x3880` → `0x3888` | 1 | RAM_PTR then START_PTR (classic order fragment) |
| `0x3800` → `0x388c` → `0x39e0` | 1 | scratch → START_DATA → post-MME |
| `0x385c` → `0x3880` | 1 | CALL then RAM_PTR |
| `0x3998` → `0x39e0` | 1 | near-MME unknown chain |

**Pass16 insight:** `0x39e0` is **MME-adjacent post-config**, not an isolated random 3D method. Mesa: after MME shadow/RAM/START programming, optionally probe `0x39e0` on silicon if channel state incomplete; do not block smoke on it.

### 3.4 Inferred MME bring-up order (pass7–16 consistent)

1. `SET_MME_SHADOW_SCRATCH` (`0x3800`) and/or **`0x34a8`** high-index path (scratch 42 / alt addressing)
2. Optional sequential `0x3804+` scratch fills
3. `SET_MME_INSTRUCTION_RAM_PTR` (`0x3880`)
4. `SET_MME_INSTRUCTION_RAM_DATA` (`0x3884`) × N — **runtime only; zero static imm**
5. `SET_MME_START_ADDRESS_RAM_PTR` (`0x3888`) = macro_id
6. `SET_MME_START_ADDRESS_RAM_DATA` (`0x388c`) = insn RAM offset
7. Optional **`0x39e0`** post-config (pass16)
8. Draw/dispatch: `CALL_MME_MACRO` (`0x385c`) + `CALL_MME_DATA` (`0x3860`)

### 3.5 Method `0x34a8` / index `0xd2a`

- Offset `0x34a8` = index `0xd2a` (3386); classic scratch `0x3800` = index `0xe00` (3584)
- Sits **before** classic MME block `0x3800–0x388c` in method space
- Hypothesis (pass14–16): alternate/high-index shadow scratch (index **42** via single method or non-sequential address)
- **P0 implement:** emit `0x34a8` during MME init; fallback sequential `0x3800 + 4*i` for i in 0..63
- Exact header `0x20010d2a` **not found** as static dword in pass16 scan — rely on imm-offset programming like other methods

### 3.6 MME ISA entropy candidates

25 weak candidates saved under `mme_isa/cand_0x*.bin` (glcore 0x200000..0x1800000, entropy 5.5–6.1, small-dword-ish). **Verdict unchanged:** do not promote to mesa MME bytecode without live `RAM_DATA` trace.

### 3.7 Unknown method mesa strategy

| Priority | Method | Evidence | Action |
|----------|--------|----------|--------|
| P0 | `0x34a8` | 8 imm glcore; MME init path | implement/verify |
| P1 | `0x39e0` | 9 imm; follows `0x3800` in ngrams | MME-adjacent probe/doc; optional emit after MME upload |
| P1 | `0x1618` | 6 imm; color-target region guess | document; emit only if state completeness needs |
| P2 | `0x1a00` | 20 imm; SPA/blend overlap | may already be SPA_VERSION path; don't double-emit blindly |
| P2 | `0x15bc`, `0x3998`, `0x6a04` | lower counts | document only |
| P2 | `0x2b4`, `0x258`, `0x1f0` | 24 imm each (capped scan) | low host/misc; investigate only if silicon fails |

---

## 4. SASS / SPH / QMD (pass16 honest assessment)

### 4.1 SASS opcode families (gpucomp, 4M × 16B windows, heuristic)

| family | count | notes |
|--------|------:|-------|
| TEX_fam | 11269 | directional |
| BRA_cand | 8971 | noisy |
| LOP3_fam | 7244 | |
| WARPSYNC_fam | 6019 | |
| MUFU_fam | 5160 | |
| BAR_fam | 1869 | |
| EXIT_cand | 662 | |
| MEMBAR_strict | 260 | stricter pass14; not zero (matcher differs from pass15 focus) |
| S2R_op_hi | 104 / miner 112 | real but sparse |

Treat as directional only.

### 4.2 S2R SR priorities (pass16 refined)

| SR | pass16 focus count | miner lite | mesa priority | guess |
|----|-------------------:|-----------:|---------------|-------|
| **0x00** | 143 | 20 | **HIGH** | SR_LANEID |
| **0x48** | 86 | **23** | **HIGH** | SR_LANEMASK_EQ? |
| 0x91 | 105 | — | low | encoding bleed / ignore |
| 0x4c | 29 | 10 | med | SR_LANEMASK_GE? |
| 0x8b | 37 | — | med | unknown high |
| **0x01** | 14 | — | **HIGH** | SR_CLOCKLO? |
| **0x03** | 12 | 6 | **HIGH** | SR_VIRTID? |
| **0x49** | 11 | 3 | **HIGH** | SR_LANEMASK_LT? |
| **0x02** | 11 | 8 | med | SR_CLOCKHI? |
| 0x0f | 23 | 2 | med | unknown |
| **0x50** | low here | — | **HIGH** | SR_SMID (known useful; implement anyway) |
| **0x25** | low here | — | **HIGH** | SR_SHADER_TYPE? (implement if t144/pass15 listed) |

**Mesa multi-SR smoke (recommended):** SR0, 0x01, 0x03, 0x25, **0x48**, **0x49**, **0x50**, optional 0x4c/0x02.

Sample S2R windows + bins: `notes/pass16_sass_deep.md`, `sass/s2r_0x*.bin`, `disasm/gpucomp_s2r_p16_site_*.hex`.

### 4.3 QMD / SPH strict scan — still not trustworthy

Pass16 required multi-field plausibility and rejected obvious `addr,0,8,0,addr` shapes. Results:

- glcore/cuda/opencl each produced **60** score≥6 QMD and SPH hits (cap)
- Previews still dominated by **reloc/descriptor patterns**: alternating small ids + `0x0277xxxx` / `0x06b6xxxx` pointers
- Top “QMD” @ glcore `0x24ccc0` score 12 looks like **structured table**, not a 256B compute QMD template
- Top SPH scores max out at **6** (bare threshold) with pointer bleed

**Artifacts saved** (for future stricter filters / diffing only): `qmd/*.json|bin`, `sph/*.json|bin`.

**Pass16 conclusion (stronger than pass15):** mesa `nv_qmd.h` / `nv_sph.h` / pass15 QMD defaults + invalidate bits remain **sole authoritative builders**. Live capture via `SEND_PCAS` / `INLINE_QMD_DATA` / compute dispatch is **P2 highest-value RE** item alongside MME RAM_DATA.

---

## 5. Channel sequences / ngrams (pass16 filtered key set)

Key-method imm chain length (excl SET_OBJECT): **60002** in glcore.

### 5.1 Unigram (selected)

| method | name | count |
|--------|------|------:|
| `0x40` | SEMA_A | 24659 |
| `0x100` | WFI_HOST | 10655 |
| `0x200` | OFF_IN_U / SET_OBJ collision | 7215 |
| `0x400` | SET_SRC_PHYS (CE) | 7194 |
| `0x48` | SEMA_C | 4438 |
| `0x44` | SEMA_B | 2090 |
| `0x4c` | SEMA_D | 1835 |
| `0x74` | SEMA_EXEC | 575 |
| `0x2600` | SPA_3D | 442 |
| `0x2200` | CB_SEL_A | 277 |
| `0x700` | LAUNCH_DMA | 157 |
| `0x0b00` | SPA_C | 87 |
| `0x1280` | INV_A | 32 |
| `0x3800` | MME_SCRATCH | 24 |
| `0x34a8` | MME_SCRATCH_ALT | 8 |
| `0x3880` | MME_RAM_PTR | 10 |
| `0x385c` | CALL_MME | 1 |

### 5.2 Dominant patterns

| Pattern | ×count | Interpretation |
|---------|-------:|----------------|
| `SEMA_A ×2/×3` | 10k+ | sema A programming loops / descriptor noise |
| `WFI_HOST → SEMA_A` | 7563 | **host sema + WFI** core idiom |
| `SEMA_A → WFI_HOST` | 3816 | symmetric sema↔WFI |
| `SEMA_A → OFF/SET_OBJ → WFI` (3-gram) | 4035 | object bind + sync |
| `SEMA_A ↔ SET_SRC_PHYS` | 3k+ | CE address setup fenced by sema |
| `SEMA_C ×3` | 2247 | C-slot loops (aligns with `0x1004→C` table) |
| `SEMA_C ↔ SEMA_D` | ~490 each way | CD pairs; full ABCD less dominant than A-centric |
| `SEMA_EXEC ×2` | 287 | execute method loops |

4-gram highlight: `SEMA_A → OFF/SET_OBJ → WFI → SEMA_A` ×3955 — canonical channel_prep fragment.

### 5.3 Sema / WFI / CE adjacency summary

- sema↔WFI adjacent pairs: **very high** (thousands each direction)
- sema↔LAUNCH_DMA: present but lower (`0x700` only 157 imm total — CE often uses `0x400`/`0x200` neighbors more in imm scan)
- SEMA_A dominates imm frequency; slot-specific execute is **orthogonal** (table-driven) to method order

### 5.4 Mesa channel_prep implications

1. **Host sema sticky emit** on GPFIFO/G1/G2 (t141+) is correct; extend Gallium/Vulkan parity.
2. **WFI after sema** remains first-class (`nv_push_host_semaphore_release_wfi_mode_ex`).
3. **CE G1:** sema fence around address setup / `LAUNCH_DMA`; `SET_SRC_PHYS` (`0x400`) heavily sema-adjacent.
4. **MME upload once** at channel init (`0x3800`/`0x34a8`/`0x3880`/`0x3888`; never CALL stub END-only).
5. **CB:** `0x2200` (SEL_A) ×277, `0x2210` (BIND) ×7, `0x1f00`/`0x1f04` LD_CB — select+bind path real; counts asymmetric (SEL much more than BIND in imm scan — BIND may be runtime-header only).
6. **Invalidate:** `0x1280`×32, `0x1288`/`0x1290`×14 — partial sextet presence; keep pass13/14 SH→TEX→SAMP→SHD→INS→CONST order unless silicon fails.

Full ngrams: `notes/pass16_sequences_deep.md`.

---

## 6. SPA / CE / video

### 6.1 SPA_VERSION

Methods `0x2600` (3D), `0x0b00` (compute), `0x1a00` (ambiguous SPA/blend) have substantial imm traffic. Top families remain pass15-style (`0xe?200`, `0x50xxxx`) mixed with huge code noise. **Mesa t139 sm→spa table** remains correct approach; silicon-validate on bring-up.

### 6.2 CE LAUNCH_DMA (`0x700`)

| lib | total imm-adjacent | small (<0x10000) |
|-----|-------------------:|-----------------:|
| glcore | 157 | **10** |
| cuvid | (see note) | low useful |

**Trust small control imms / open-gpu-doc bitfields only.** Large immediates (pass15 `0x078ef80x` family) are code noise.

### 6.3 NVDEC EXECUTE (`0x300`)

Huge imms (`0x3800000`, mask-like `0x7ffe0ff`) dominate; small/zero executes are the only plausible static signal. Mesa video: prefer `0` / documented codec flags.

### 6.4 Video class alts

From §1.2; newest-first recommendations:

```
NVDEC: D1B0 CFB0 CEB0 CDB0 C9B0 B8B0 C7B0 C6B0 C4B0  (+ CBB0 C8B0 C0B0 B0B0 A0B0)
NVENC: D1B7 CFB7 CEB7 C9B7 C8B7 C7B7 B6B7 B4B7 C4B7 C5B7
```

---

## 7. Mesa implementation priorities (pass16)

### P0 — implement / verify soon (carry pass15 + pass16 confirm)

1. **NVDEC class alts** — full ladder incl. `B8B0`
2. **GPFIFO class alts** — full `CA6F…906F` (head correction); do not add `506F`
3. **Sema slot×exec matrix** — modes `(0x1001, classic/0x1d)`, `(0x1002,B/0x11)`, `(0x1004,C/0x12)`, `(0x0802,A/0x10)`, `(0x0804,B/0x11)`; ABCD fallback
4. **Method `0x34a8`** — MME shadow scratch alt / index-42 path
5. **Host sema + WFI** on GPFIFO/G1/G2 — Gallium/Vulkan parity with t141+
6. **S2R multi-SR smoke** — SR0, 0x01, 0x03, 0x25, **0x48**, **0x49**, **0x50**

### P1 — bring-up hardening

7. **NVENC class alts** — if encoder alloc fails
8. **SPA_VERSION** — validate sm→spa vs imm families on silicon
9. **Method `0x39e0`** — MME-adjacent post-config; optional after MME upload
10. **Unknown `0x1618` / `0x15bc` / `0x3998`** — document; emit only if completeness requires
11. **CB select+bind / invalidate** — confirm order on silicon; imm counts partial
12. **QMD pass15 defaults + invalidate bits** — keep mesa builder only

### P2 — needs live trace / pass17

13. **MME RAM_DATA (`0x3884`)** capture on first GL/VK init/draw — **highest-value live RE**
14. **QMD/SPH live** via SEND_PCAS / INLINE_QMD / compute dispatch
15. **Sema `0x1d` method mapping** — confirm classic D@`0x4c` vs alternate host method on silicon
16. **Stricter QMD/SPH static** — only if live blobs provide ground truth templates
17. **AUX `*FA`** — only if class alloc fails for missing engine

### P3 — longer arc

18. NIR → SASS (gpucomp/ptxjit deep or user-approved nouveau/nvk double-check)
19. Full Gallium/Vulkan beyond smoke
20. Video encode/decode e2e bitstreams

### Explicit non-goals

- Do **not** replace mesa QMD/SPH with pass16 static “candidates” (reloc-table bleed)
- Do **not** CALL stub END-only MME macros
- Do **not** add `506F` or `*FA` to production class alts without silicon evidence
- Do **not** trust huge LAUNCH_DMA/EXECUTE imms from static scan
- Do **not** trust global sema tight-window scans over table @ `0x11e30c0`
- Do **not** modify nouveau/nvk without double-check

---

## 8. Confidence matrix (pass11 → pass16)

| Area | pass11 | pass12 | pass13 | pass14 | pass15 | pass16 |
|------|--------|--------|--------|--------|--------|--------|
| Class ladders DMA/C/3D | med | high | high | high | high | **high** |
| GPFIFO ladder | low | med | med | med | high | **high** (eglcore mirror) |
| NVDEC/NVENC ladders | low | med | med | med+ | high | **high** (cross-lib) |
| AUX *FA | — | — | — | note | documented | **documented** (eglcore mirror) |
| CUDA GPFIFO variant | — | — | — | — | — | **med** (C86F/A16F subset) |
| Sema execute ladder | med | med | high | high | high | **high** |
| Sema slot selection | — | — | med | med+ | high | **high** (corrected JSON) |
| MME upload order | med | med | high | high | high | **high** (+0x39e0 post) |
| MME real ISA | low | low | low | low | low | **low** (entropy weak; RAM_DATA=0) |
| Method 0x34a8 | — | — | — | note | med+ | **high** (imm sites; implement) |
| Method 0x39e0 | — | — | — | — | note | **med+** (MME-coupled) |
| SASS opcode families | med | med | med | med- | med+ | **med** (heuristic; MEMBAR 260) |
| S2R SR map | low | low | med | med | med+ | **med+** (0x00/0x48 dominant) |
| QMD/SPH static | low | low | low | low- | low | **low** (strict still weak) |
| Channel ngrams | low | med | med | med | med+ | **med+** (60k key chain) |
| CE/video imms | low | med | med | med | med | **med** (small-imm filter) |
| Unknown method catalog | low | med | med | med | med+ | **med+** (site dumps) |

---

## 9. Artifacts index

| Path | Description |
|------|-------------|
| `pass16_mine.py` | Full 22-lib miner |
| `pass16_deep_analysis.py` | Focus analysis (~165s) |
| `pass16_supplement.py` | Corrected sema + header/site probes |
| `pass16_targeted_disasm.sh` | Phase 1 dumps (~993 disasm files) |
| `notes/pass16_sema_deep.md` | Sema (initial; see supplement for alignment fix) |
| `notes/pass16_supplement.md` | **Corrected sema table + method contexts** |
| `notes/pass16_mme_deep.md` | MME/unknown methods |
| `notes/pass16_qmd_sph_deep.md` | Strict QMD/SPH (weak hits documented) |
| `notes/pass16_sass_deep.md` | SASS/S2R |
| `notes/pass16_sequences_deep.md` | Channel ngrams |
| `notes/pass16_classes_ce_video_deep.md` | Class/CE/video |
| `notes/pass16_spa_deep.md` | SPA families |
| `notes/pass16_mesa_priorities.md` | Priority list |
| `notes/pass16_focused_analysis.md` | Executive summary |
| `sema/pass16_sema_corrected.json` | Authoritative sema mode table |
| `tables/pass16_crosslib_summary.json` | Miner cross-lib summary |
| `tables/*.full.json` | Per-lib full mine blobs |
| `disasm/` | ~993 hex/xxd/dword/objdump artifacts |
| `x86_callers/meth_sites_*.txt` | Method imm/hdr site indexes |
| `qmd/`, `sph/` | Weak strict-scan candidates (do not use in mesa) |
| `mme_isa/cand_*.bin` | Weak entropy candidates |
| `sass/` | S2R windows + gpucomp miner lite |
| `classes/` | Ladder JSON verify |
| `HW_MODEL_PASS16_DEEP_DISASM_610.43.02.md` | This document |

---

## 10. Suggested next steps (pass17 / mesa / silicon)

1. **Mesa audit:** confirm ticks 142–144 (and any WIP t144) fully wire pass15/16 P0 (class alts, sema modes, `0x34a8`, multi-SR S2R, sema+WFI); add selftests if missing.
2. **Optional mesa:** document/emit `0x39e0` as MME post-config probe behind a flag; never required for smoke.
3. **Silicon:** sema slot modes vs classic ABCD; SPA families; NVDEC/GPFIFO class alts; `0x34a8` scratch init.
4. **Live trace (pass17 RE priority):** attach to proprietary GL/VK init — capture MME `RAM_DATA` stream and first compute `QMD`/`SPH`/`PCAS` blobs; diff against mesa builders.
5. **Pass17 static RE (if live unavailable):** use live ground-truth templates to train stricter QMD/SPH matchers; continue gpucomp SASS only as directional; expand method-descriptor class_tag → engine map.

---

*End of pass16 report. Primary work product is evidence + priorities; implementation continues in mesa `nv_*` emitters and host selftests on branch `nvidia`.*
