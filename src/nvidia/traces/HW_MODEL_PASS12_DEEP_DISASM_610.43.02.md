# HW Model Pass 12 — Function-Level / Imm-Window Deep RE (NVIDIA-Linux-x86_64-610.43.02)

**Date:** 2026-06-25  
**Duration:** ~24 min full miner + targeted follow-up analysis  
**Scope:** Function-level imm windows, method-offset histograms, QMD/SPH/MME/SASS
heuristics, sema execute sites, class cluster re-scan, video (vdpau/encode/cuvid)
cross-check. Builds on pass11 class ladders.  
**Artifacts:** `/tmp/nvidia-reveng-pp-v2/re_pass12/` (`pass12_mine.py`, `tables/`,
`rodata/`, `sema/`, `qmd/`, `sph/`, `mme/`, `sass/`, `methods/`, `nvenc/`,
`disasm_snippets/`, `notes/`)  
**Mesa impact:** sema ladder 0x1000/0x1002 modes; G2 WFI+PCAS2 prep; G3 MME
channel-prep helper; pass12 selftests -450..-460; trace doc under
`mesa/src/nvidia/traces/`.

---

## Method

1. **Phase 1 targeted dumps** — exact glcore rodata windows (pass11 offsets +
   neighbors), cuda QMD/sema, gpucomp SASS/SPH, vdpau/encode/cuvid method maps,
   sema/PCAS/MME/shader imm file offsets for later objdump.
2. **Phase 2 full mine** — 17 libraries (~395 MB), per-lib JSON for class
   clusters, method histograms (3D/CE/GPFIFO/compute/NVENC/NVDEC), sema sites,
   QMD/SPH/MME/SASS heuristics, ioctl/string extracts.
3. **Phase 3 cross-lib summary** — sema imm histogram, cluster suffix ranks,
   method-hit aggregates.
4. **Manual synthesis** — cross-check vs pass7–11, open-gpu-doc method names,
   mesa `nv_*` emitters.

**Reconfirmed limitation:** Pushbuffer method *headers* (`0x2001xxxx`) are almost
always **runtime-constructed** (shift/or in code). Trust: (a) imm method offsets
and method *indices* (`moff >> 2`) in executable, (b) rodata **class ID arrays**,
(c) rare literal headers (eglcore sema inc4), (d) open-gpu-doc / OGKM headers,
(e) gpucomp SASS constant signatures.

---

## 1. Class ladders (pass12 re-confirmation)

### 1.1 Primary glcore rodata (unchanged from pass11, re-dumped)

| Engine | File offset | Ladder (newest→oldest in array) |
|--------|-------------|----------------------------------|
| DMA_COPY `*B5` | `0x11bb600` (18593280) | `CAB5 C9B5 C8B5 C7B5 C6B5 C5B5 C3B5 C1B5 C0B5 B0B5` |
| COMPUTE `*C0` | `0x11bb640` (18593344) | `CEC0 CDC0 CBC0 C9C0 C7C0 C6C0 C5C0 C3C0 C1C0 C0C0 B1C0 B0C0` |
| 3D `*97` | `0x11bb680` (18593408) | `CE97 CD97 CB97 C997 C797 C697 C597 C397 C197 C097 B197 B097` |
| GPFIFO `*6F` | `0x1238b70` (19106672) | `CA6F C96F C56F C46F C36F C06F B06F A06F A26F 906F` |
| NVENC `*B7` | `0x1238bb4` (19106720) | rodata order is **oldest-first**: `C5B7 C4B7 B4B7 B6B7 C7B7 C8B7 C9B7 CEB7 CFB7 D1B7` |
| NVDEC (partial side) | `0x1238bf0` area | `C4B0 C6B0 C7B0 B8B0 C9B0` (+ global hits favor `A0B0/B0B0/C0B0/C8B0/CBB0`) |

**Gaps vs mesa ladders (intentional intermediates kept as alts):**
- DMA: mesa keeps `C4B5`, `A0B5` (not in primary glcore array; pass9/cuda hits).
- Compute: mesa keeps `CCC0 CAC0 C8C0 C4C0` (skipped in primary glcore; cuda has
  mixed clusters with `C5C0 CAC0`).
- 3D: mesa keeps `CC97 CA97 C897 C497`.
- GPFIFO: mesa keeps `C86F C76F C66F C36E A16F 506F`; primary glcore has **no**
  `C86F/C76F/C66F/506F` in the contiguous ladder — those are pass9/cuda alts only.
- NVENC: mesa prefers newest-first `D1B7…B4B7` (inverse of rodata storage order).

### 1.2 Cross-lib cluster counts (pass12)

| Suffix | Clusters | Notes |
|--------|----------|-------|
| `*B0` | 15 | NVDEC + many Fermi display/misc classes (noisy) |
| `*B5` | 10 | DMA_COPY ladders (glcore/eglcore/cuda) |
| `*97` | 7 | 3D ladders |
| `*6F` | 6 | GPFIFO (+ gpucomp false-positive A6xx blocks) |
| `*C0` | 5 | Compute ladders |
| `*B7` | 3 | NVENC (glcore/eglcore/cuvid) |

**cuvid** had the highest engine-ish cluster count (119) — rich NVDEC/NVENC class
tables plus many non-engine `9xxx` blocks (codec/config IDs; filter by suffix).

**gpucomp / glvkspirv** top "clusters" are compiler/SPIR-V constant pools
(`A66F A672…`) — **not** engine class ladders; ignore for alloc order.

---

## 2. Host semaphore (pass12 refinement)

### 2.1 glcore sema imm file offsets (raw `0x1001` / `0x1000` / `0x1002` / `0x2`)

| Imm | Sample file offs (first few) | Interpretation |
|-----|------------------------------|----------------|
| `0x1001` | 10763064, 10764744, 15891252, 16025012, … (15+ sites) | **Primary blob execute** (pass8/10/11) |
| `0x1000` | many early/mid-file sites (112, 168, 3253220, …) | Acquire/release alt; may be non-sema noise in ELF |
| `0x1002` | 2565036, 10459252, 14257108, … | Rare execute/alt mode |
| `0x0002` | (vdpau primary; glcore also has scattered `2`) | **VDPAU execute** |

### 2.2 Cross-lib sema histogram caveat

Auto sema-site scanner is **noisy** (method index `0x13` = SEMAPHORED/4 appears
in x86 code as part of unrelated immediates). Histogram top hits
`0x1`/`0x2`/`0x1000` include non-sema traffic. **Trust ranked by specificity:**

1. `0x1001` — glcore dedicated imm sites + pass8 trace (authoritative blob).
2. `0x0002` — vdpau method path (authoritative alt).
3. `0x1000` / `0x1002` — documented pass11/12 alts; try after 0x1001 fails on HW.
4. `0x01100002` — open-header theoretical only (not observed as push execute).

### 2.3 Mesa sema ladder (pass12)

Default bring-up order (tick129–134, pass12 extends with 0x1000/0x1002 modes):

```
BLOB_ALIGN4(0x1001) → BLOB_SHIFT2(0x1001) →
BLOB1000_ALIGN4 → BLOB1000_SHIFT2 →
BLOB1002_ALIGN4 → BLOB1002_SHIFT2 →
VDPAU_ALIGN4(0x2) → VDPAU_SHIFT2(0x2) →
OPEN_ALIGN4 → OPEN_SHIFT2
```

(Implementation: extend `enum nv_host_sema_mode` + ladder fill; keep first-win
cache on successful channel submit.)

---

## 3. Compute / QMD / PCAS (G2 path)

### 3.1 Method imm sites (glcore)

| Method | Offset | Method index (`>>2`) | glcore imm evidence |
|--------|--------|----------------------|---------------------|
| `SET_INLINE_QMD_ADDRESS_A` | `0x2f00` | `0xbc0` | many `0x2f00` at 3224728+ |
| `SEND_PCAS_A` | `0x2e00` | `0xb80` | `0x2e00` / `0xb80` at 4198012+ |
| `WAIT_FOR_IDLE` (compute) | `0x2c0` | `0xb0` | high method-hit count |
| `SET_SPA_VERSION` | `0x1a00` | `0x680` | present in compute map |
| `SET_SHADER_LOCAL_MEM_*` | `0x6c4..0x6dc` | — | **cuda** strong hits; glcore weaker |

### 3.2 QMD heuristic candidates

300 QMD-sized blocks across libs with plausible CTA dims (1..1024 threads) and
shared-mem < 2 MB. Many are false positives (generic tables), but structure
aligns with mesa `nv_qmd_desc` fields:

| QMD off | Field (mesa) |
|---------|----------------|
| `0x38..0x4c` | CTA raster + thread dims |
| `0x50` | shared_mem_size |
| `0x54` | qmd_version / sass_version packed |
| `0x58` | local_mem_low |
| `0x60..0x74` | sema release0/1 addr + payload |
| `0x80` | program_offset |
| `0x84` | register_count_v |

### 3.3 G2 bring-up order (mesa, pass12 refined)

```
SET_OBJECT(compute_class)
SET_SPA_VERSION (0x53 default / device SM)
SET_CWD_CONTROL(0)
[optional SET_CWD_SLOT_COUNT]
INVALIDATE_SHADER_CACHES + texture caches
[optional SET_SHADER_LOCAL_MEMORY_A/B + NON_THROTTLED_A/B/C]
WFI (host GPFIFO sema / engine WFI before first launch — pass12 add)
materialize QMD (invalidate_caches=1, barrier_count, sema release0)
SET_INLINE_QMD_ADDRESS_A/B + LOAD_INLINE_QMD_DATA (or mem QMD + SEND_PCAS_A/B)
[optional SEND_PCAS2_B for newer classes]
```

**SPA version note:** pass12 keeps `0x53` as smoke default (Ampere-era band,
matches G3 pipeline bind). Device-specific SASS from `nv_device_info` should
override on real HW (SM 8.6 → 0x86, Ada 8.9 → 0x89, Hopper 9.0 → 0x90, etc.).

---

## 4. 3D / MME / shader bind (G3 path)

### 4.1 Method hit aggregates (all libs, top 3D)

| Method | Hits (noisy; includes non-header imm) | Role |
|--------|----------------------------------------|------|
| `SET_PIPELINE_SHADER` `0x2000` | ~2M | Stage enable/bind |
| `SET_OBJECT` `0x200` | ~278k | Class bind |
| `SET_MME_SHADOW_SCRATCH` `0x3800` | ~142k | MME scratch |
| `SET_PIPELINE_PROGRAM` `0x2200` | ~108k | Program VA |
| `CALL_MME_DATA` `0x3860` | ~60k | MME macro arg |
| `SET_PROGRAM_REGION_A` `0x2440` | ~30k | Program heap |
| `SET_SPA_VERSION` `0x2600` | present | SPA for 3D |
| `INVALIDATE_*_CACHE` `0x2c80..0x2c94` | present | Post-bind inv |
| `WAIT_FOR_IDLE` `0x2c0` | ~37k | Engine WFI |

### 4.2 MME imm sites (glcore)

| Imm / index | Meaning |
|-------------|---------|
| `0x3860` | `CALL_MME_DATA` method offset (many sites from 2426136) |
| `0x3880` | `SET_MME_INSTRUCTION_RAM_PTR` |
| `0x3888` | `SET_MME_START_ADDRESS_RAM_PTR` |
| `0xe20` | `0x3880 >> 2` (method index for INST_RAM_PTR — runtime header build) |

**Upload order (mesa `nv_mme_emit_upload_only`, pass12 confirms):**
```
SET_MME_INSTRUCTION_RAM_PTR(slot_base)
SET_MME_INSTRUCTION_RAM_DATA × N (insn stream)
SET_MME_START_ADDRESS_RAM_PTR(slot)
SET_MME_START_ADDRESS_RAM_DATA(entry_pc)
```
Stubs remain end-only (`is_stub_end_only`); real MME loops are pass5 scaffolds
only — needs more targeted disasm of `CALL_MME_MACRO` callers for production.

### 4.3 Shader / SPH

**gpucomp SPH candidates:** 80+ blocks; type dwords include `0x00000001` (VTG/PS),
`0x00010102` (extended/compute-ish), with trailing register/attribute counts.
Mesa `nv_sph_build_*_mov_imm_exit` for VS/FS/GS/TCS/TES is sufficient for smoke;
real compiler path is gpucomp (closed) — open path is NIR→SASS in `nv_sass.*`.

**SASS signatures in gpucomp (pass12):**

| Pattern | Sample hi/lo | Mesa emitter |
|---------|--------------|--------------|
| EXIT `0x7918xxxx` | `0x7918870f` etc (111 hits) | `nv_sass_emit_exit` |
| MOV `0x5c98xxxx` | `0x5c98840f` (42k hits — noisy) | `nv_sass_emit_mov_*` |
| NOP `0x50b0xxxx` | `0x50b0e3e8` (34 hits) | `nv_sass_emit_nop` |
| S2R `0xf0c8xxxx` | 20 hits | `nv_sass_emit_s2r` |

---

## 5. CE / DMA (G1 path)

Method hits strongly favor physical/offset programming then `LAUNCH_DMA` `0x700`:

```
OFFSET_IN_UPPER/LOWER → OFFSET_OUT_UPPER/LOWER →
PITCH_IN/OUT → LINE_LENGTH_IN → LINE_COUNT →
[SET_SRC_PHYS / SET_DST_PHYS on some classes] →
LAUNCH_DMA
```

Mesa `nv_copy_methods.h` / G1 smoke paths align; no pass12 ladder change.

---

## 6. NVENC / NVDEC (video)

### 6.1 Method frequency (vdpau / encode / cuvid)

| Method | vdpau hits | encode hits | cuvid hits |
|--------|-----------|-------------|------------|
| `APP_ID` `0x200` | 129 | 52 | 35581 |
| `CTRL` `0x400` | 1231 | 359 | 48562 |
| `PIC_SETUP` `0x404` | 53 | 32 | 3245 |
| `IN` `0x408` | 28 | 24 | 2224 |
| `BS` `0x40c` | 41 | 10 | 694 |
| `RC` `0x410` | 87 | — | — |
| `STATUS` `0x414` | 18 | — | — |
| `OUT_ST` `0x718` | present | present | present |
| `OUT_BS` `0x71c` | present | present | present |

**Canonical emit order (mesa, unchanged, reinforced):**
```
APP_ID(0x200) → CTRL(0x400) → PIC_SETUP(0x404) → IN(0x408) →
BS(0x40c) → RC(0x410) → STATUS(0x414) → OUT_ST(0x718) → OUT_BS(0x71c) →
[optional OUT_MB 0x720] → EXECUTE
```

NVDEC: `SET_CONTROL(0x400) … SET_STATUS(0x418) …` then execute/kick; status BO
poll via `nv_nvdec_status_poll_snapshot` (tick133).

**Preferred classes for 610 bring-up:** NVENC `C8B7` (highest glcore class hits)
with ladder head `D1B7`; NVDEC `C8B0`/`C9B0` with ladder head `CBB0`.

---

## 7. Ioctl / RM surface notes

Ioctl miner is coarse (many false positives). Continue relying on open-gpu-kernel
modules `nvidia-drm` / `nv-ioctl` paths and existing mesa `nv_rm_*` /
`libdrm_nvidia` for NVOS32 alloc/map/channel submit. Pass12 did not change RM
ABI; channel alloc still uses class ladders from §1.

---

## 8. Mesa wiring summary (pass12 increment)

| Area | Change |
|------|--------|
| `nv_push.h` | Add `NV_HOST_SEMA_MODE_BLOB1000_*` / `BLOB1002_*`; extend ladder |
| `nv_qmd.h` | `nv_compute_emit_g2_channel_prep` (WFI + inv + optional PCAS2 flag) |
| `nv_3d_methods.h` | `nv_3d_emit_g3_channel_prep` (SPA + MME upload stubs + inv) |
| `nv_mme.h` | pass12 comment on `0xe20` method-index construction |
| `nv_smoke_selftest.h` | pass12 checks -450..-460 |
| traces | `HW_MODEL_PASS12_DEEP_DISASM_610.43.02.md` copy |

---

## 9. Remaining RE / bring-up gaps

1. **Function-level objdump** of sema/PCAS/MME sites (file offs recorded; needs
   VA mapping via `readelf -l` + targeted `objdump -d --start-address`).
2. **Real MME programs** — replace end-only stubs with disasm of hot macros
   (clear/draw-indirect) from glcore callers of `0x385c`/`0x3860`.
3. **NIR→SASS completeness** — smoke EXIT/MOV/store works; full shader model
   needs more gpucomp constant tables / SM-specific opcode maps.
4. **Silicon validation** — G1 CE sema, G2 QMD+PCAS, G3 clear/draw, NVENC/NVDEC
   status poll on `/dev/nvidia*`.
5. **NVDEC full ladder** — no single clean contiguous `C8B0..` array in glcore;
   refine from cuvid class tables.

---

## 10. Artifact index

```
/tmp/nvidia-reveng-pp-v2/re_pass12/
  pass12_mine.py
  HW_MODEL_PASS12_DEEP_DISASM_610.43.02.md   (this file)
  mine_run.log
  tables/master.json, cross_lib_summary.json
  rodata/glcore_targeted.json, glcore_all_clusters.json, cuda_targeted.json
  sema/*.json
  qmd/*.json
  sph/*.json, sph/gpucomp_sph.json
  mme/*.json
  sass/*.json, sass/gpucomp_deep.json
  methods/*_methods.json
  nvenc/{vdpau,encode,cuvid}_methods.json
  disasm_snippets/glcore_{sema_imm,pcas,mme,shader}_*_offs.json
  notes/auto_findings.md, phase1_summary.json
  ioctl/*.json
  strings/*.txt
```
