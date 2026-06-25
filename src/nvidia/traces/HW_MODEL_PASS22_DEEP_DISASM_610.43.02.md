# HW Model Pass 22 — Multi-Hour Deep Disasm & System Understanding
# (NVIDIA-Linux-x86_64-610.43.02; pass18–21 carry-forward + pass22 full pass)

**Date:** 2026-06-25  
**Artifacts:** `/tmp/nvidia-reveng-pp-v2/re_pass22/`  
**Scripts:** `pass22_deep_analysis.py` (slow full-file chains), `pass22_supplement_deep.py` (fast mmap index, 16 libs, 36s)  
**Mesa baseline:** pass21 wire-up ticks 155–160 (`66fd6a7962e` tip at pass22 start)

---

## 1. What pass22 set out to learn

Beyond pass18–21 (method vocab, sema formal, INLINE→PCAS medians, path C gate), pass22 asked:

1. Are ordered multi-method **templates** statically recoverable (INLINE→PCAS, MME RAM upload, inv+WFI+sema)?
2. Do glcore / eglcore / vksc host-sema formal tables remain **byte-identical** at refined file offsets?
3. Are pushbuffer headers (`0x2000|midx`, `0x2001|midx`) present in static libs or only built at runtime?
4. How does **gpucomp** (compiler/backend, 105 MB) differ from glcore/cuda in MME/RAM_DATA density?
5. What should mesa **not change** vs what remains **silicon/live-trace blocked**?

---

## 2. Executive answer (how the proprietary userspace works)

NVIDIA userspace (610.43.02) does **not** ship pre-baked pushbuffer templates as contiguous ordered dword sequences for the sequences mesa needs. Instead:

| Layer | Static evidence | Mesa implication |
|-------|-----------------|------------------|
| **Method immediates** | Abundant in x86 (WFI/HOST_SEM/PCAS/INLINE/MME as imm16 in mov/or patterns) | Helpers may reference method offsets; presence ≠ order |
| **Pushbuffer headers** | Almost always **zero** exact `0x2000/0x2001` for method midx in rodata/text | `nv_push_*` must construct headers at emit time |
| **Ordered chains** | `forward_ok` near-zero for 5+ method templates; only short pairs (e.g. PCAS_A→B) sometimes hit | Emit explicitly in channel/G1–G4 helpers; do not pattern-mine order |
| **Host sema formal** | 11-row table embedded in glcore/eglcore/vksc; **row-identical** across three | pass21 `BLOB1004` / slot C default is authoritative |
| **MME RAM_DATA/ADDR** | Method imm sites exist (gpucomp densest for 0x3884); no static microcode order | pass21 RAM scaffold + `NV_MME_PASS21_PROBE_RAM_DATA` probe only; path C gated |
| **Compute launch** | INLINE/PCAS/QMD method sites; medians measure proximity not emission order | pass20/21 explicit INLINE then PCAS then host sema tail |
| **Video (G4)** | encode/cuvid/vdpau have sparse method hits; class ladders in rodata | pass17/21 NVDEC/NVENC bringup + pass21 host sema symmetry |

**Bottom line:** mesa's pass21 architecture (unified G0–G4 host sema tail, explicit emitters, path C gate, runtime headers) matches how the proprietary driver is built. Pass22 strengthens this; it does not unlock live MME ISA or silicon sema completion without `/dev/nvidia*`.

---

## 3. Host semaphore formal table (refined)

### 3.1 Signature hunt (pass22 correction)

Pass17/21 labeled egl/vksc bases off-by-**0x10** as file offsets. Signature scan of row0 `(1004, 4, 2, 0x12, 2)`:

| Lib | File-offset base (row0 @ base+0x10) | ok |
|-----|--------------------------------------|-----|
| glcore | `0x11e30c0` | **11/11** |
| eglcore | `0x114f2e0` (was listed `0x114f2f0`) | **11/11** |
| vksc | `0x877380` (was listed `0x877390`) | **11/11** |
| cuda | *no embedded formal table* | — |

### 3.2 All 11 rows (glcore; egl/vksc byte-identical)

| k | execute | p1 | p2 | sem_idx | p4 | Interpretation (pass17/21) |
|---|---------|----|----|---------|-----|---------------------------|
| 0 | 0x1004 | 4 | 2 | 0x12 | 2 | BLOB1004 align4, slot-ish 0x12 |
| 1 | 0x1002 | 4 | 2 | 0x11 | 2 | BLOB1002 |
| 2 | 0x0804 | 4 | 2 | 0x11 | 2 | 0804 family |
| 3 | 0x0802 | 4 | 2 | 0x10 | 2 | 0802 family |
| 4 | 0x1004 | 5 | 3 | 0x12 | 3 | variant tier 5/3 |
| 5 | 0x1002 | 5 | 3 | 0x11 | 3 | |
| 6 | 0x0804 | 5 | 3 | 0x11 | 3 | |
| 7 | 0x0802 | 5 | 3 | 0x10 | 3 | |
| 8 | 0x1004 | 0 | 1 | 0x08 | 1 | sparse tier |
| 9 | 0x1002 | 0 | 1 | 0x07 | 1 | |
| 10 | 0x1001 | 0 | 1 | 0x1d | 1 | BLOB1001 align4 (`NV_HOST_SEMA_MODE_BLOB_ALIGN4`) |

**Cross-lib identity:** glcore ≡ eglcore ≡ vksc on first 5 dwords of every row (`cross_lib_identity.json`).

**Mesa policy (unchanged, reconfirmed):**  
`NV_PASS21_HOST_SEMA_DEFAULT_MODE` = BLOB1004_ALIGN4, execute `0x1004`, SEMAPHOREC (slot C), via `nv_push_g0_g4_host_sema_tail_pass21` for G0–G4 symmetry. Ladder `nv_pass21_g0_g4_sema_mode_ladder_fill` covers 1004→1002→0804→0802→1001 for silicon retry.

---

## 4. Method immediates — abundance without order

Single O(n) dword scan (imm: low16==method, hi16<0x100). Cap 3k–5k sites/method.

### 4.1 Core libs (selected methods)

| Method | Meaning | glcore | eglcore | cuda | vksc | gpucomp |
|--------|---------|--------|---------|------|------|---------|
| 0x0110 | WFI | 1091 | high | 2783 | — | 4000+ |
| 0x0200 | HOST_SEM_A | 3000+ | — | — | — | — |
| 0x0208 | HOST_SEM_C | 284 | 273 | 620 | 67 | **1738** |
| 0x021c | INV_SHADER NVC597 | 163 | — | 395 | — | 412 |
| 0x02b4 | SEND_PCAS_A | 144 | — | 141 | — | 751 |
| 0x02b8 | SEND_PCAS_B | 421 | — | 609 | — | — |
| 0x0318 | INLINE_QMD_A | 250 | — | 297 | — | 729 |
| 0x0320 | LOAD_INLINE_QMD0 | 950 | — | 2274 | — | — |
| 0x385c | MME_RAM_ADDR | 7 | — | 40 | — | 25 |
| 0x3884 | MME_RAM_DATA | 22 | 20 | 11 | 11 | **127** |
| 0x39e0 | MME_POST | 29 | — | 128 | — | 35 |
| 0x3800 | MME_CALL0 | 59 | — | 2959 | — | 1035 |
| 0x34a8 | MME_SC42 | 16 | — | 21 | — | 19 |
| 0x1b00 | REPORT_A | 86 | — | 775 | — | 179 |

**gpucomp** is the MME/PCAS/INLINE **density king** (compiler/backend blobs embed more method constants). **glcore/egl** dominate host sema/WFI for GL paths. **cuda** dominates MME_CALL0 and LAUNCH_DMA-style 0x0300 (capped 3000).

### 4.2 Ordered chain probe (`forward_ok` / tried @ 8 KiB window)

Greedy: site of method[0], then ascending sites of method[1..] within 8192 B.

| Chain | glcore | cuda | vksc | Meaning |
|-------|--------|------|------|---------|
| inline_pcas (318→31c→320→2b4→2b8) | **0/30** | **0/30** | 0/n | No static 5-step template |
| mme_ram_upload (385c→3884×2→39e0) | **0/7** | **0/30** | — | No static RAM upload sequence |
| inv_wfi_sema (21c→1330→110→208) | **0/30** | — | — | Inv+WFI+sema not laid out linearly |
| host_abcd (200→204→208→20c) | **0/30** | — | — | Host sem slots not sequential in x86 |
| qmd_pcas_only (2b4→2b8) | **7/30** | **11/30** | some | Short pair sometimes co-located |
| launch_sema (300→208) | — | **3/30** | — | Occasional proximity only |
| report_host | **0/30** | — | — | Report block not statically chained to sema |

**Thesis (pass19/22, now quantified on 16 libs):** proprietary emitters build method streams in registers/stack at runtime. Static RE finds **constants and distance heuristics**, not submit order. Mesa must keep **explicit emit helpers** (`nv_compute_emit_*`, `nv_3d_emit_*`, `nv_push_g0_g4_host_sema_tail_pass21`).

---

## 5. INLINE → PCAS distances (pass20 reconfirm + pass22 delta)

Nearest forward imm distance 0x0318 → 0x02b4 within 8 KiB (median of samples). **Not emission order proof.**

| Lib | pass20 median | pass22 median | n (samples) |
|-----|---------------|---------------|-------------|
| glcore | 428 | **428** | 1–few |
| cuda | 2204 | **2204** | 2 |
| vksc | 2724 | **2724** | ok |
| eglcore | — | **6108** | pass22 only |
| gpucomp | null | **228** (interim n=1) | sparse |
| opencl | — | **1348** | pass22 |
| cuvid | — | **1084** | pass22 |
| rtcore | — | **100** | pass22 |

Stable pass20 constants in `nv_qmd.h` remain valid for glcore/cuda/vksc. eglcore/opencl/cuvid medians are extra RE context only unless mesa adds EGL-specific spans.

---

## 6. MME / RAM_DATA / RAM_ADDR / path C

### 6.1 Static picture

- **RAM_DATA (0x3884):** sparse in glcore (22 imm), denser in gpucomp (127). Func-prologue distance samples glcore median **2064** B (pass20/22 match) — site sits deep inside functions, not at entry templates.
- **RAM_ADDR (0x385c):** very sparse (glcore 7, cuda 40, gpucomp 25). ram_addr→data forward median sometimes tiny (glcore/egl **40** B) when both exist — suggests same function may mention both constants without proving submit order.
- **MME_POST (0x39e0), MME_CALL0 (0x3800), MME_SC42 (0x34a8):** present; chain `mme_scratch_post` / `mme_ram_upload` forward_ok still ~0.
- **No embedded formal sema table in cuda** — sema policy is shared via runtime/other libs for GL/VK; CUDA uses its own paths (still has 0x0208 imm sites).

### 6.2 Path C gate (unchanged)

Mesa `nv_mme_path_c_indirect_ready` / stub_end_only: pass22 finds **no** static non-END MME program blobs sufficient to unlock indirect CALL. Host paths A/B/C' remain authoritative. Env `NV_MME_PASS21_PROBE_RAM_DATA=1` = vocab/probe only on silicon.

---

## 7. Pushbuffer headers (runtime-only)

Exact static dwords `0x2000|midx` / `0x2001|midx` for method midx = method_off>>2:

| Lib | 0x0208 (midx 0x82) | 0x02b4 (0xad) | 0x0318 (0xc6) | 0x3884 (0xe21) | 0x39e0 (0xe78) |
|-----|--------------------|--------------|--------------|-----------------|----------------|
| glcore | 0/0 | 0/0 | **1**/0 | 0/0 | 0/**12** |
| cuda | **5**/0 | 0/0 | 0/0 | 0/0 | 0/0 |
| vksc | 0/0 | 0/0 | **1**/0 | 0/0 | 0/**7** |
| gpucomp | 0/0 | 0/0 | **1**/0 | 0/0 | 0/0 |

Almost never both header forms for the methods mesa emits. Occasional 0x2001 on MME_POST midx is noise/coincidence in data, not a template library. **Mesa must always synthesize headers in `nv_push_method` / channel submit.**

---

## 8. Class ladders (rodata)

Full/partial dword sequences for DMA/COMPUTE/3D/GPFIFO/NVENC/NVDEC/AUX_FA class ID lists appear in glcore/cuda/vksc/gpucomp (see `classes/*_ladders.json`). Supports mesa class selection ladders (pass21 compute object kind ladder, channel class picks) as **ID tables**, not method order templates.

---

## 9. Sema multi-radius co-occurrence (heuristic only)

Within 256/1024/4096 B of HOST_SEM_C (0x0208) imm sites, peer method imm counts (glcore interim):

| Radius | Top peers |
|--------|-----------|
| 256 B | WFI(0x110)×3, INLINE(0x318)×2, RAM_DATA×1 |
| 1024 B | WFI×5, INLINE×3, PCAS×1 |
| 4096 B | WFI×14, INLINE×6, INV×2, PCAS×1 |

cuda: WFI dominates at all radii; weak PCAS/INLINE. Supports pass21 placing **WFI + host sema** in barrier/tail paths and treating PCAS/INLINE as **separate** compute launch blocks — not one mined super-template.

---

## 10. Objdump / ELF phase

60 targeted objdump windows at sema bases + method imm sites (`objdump_windows/`). readelf section headers for glcore/cuda/gpucomp. Most method "sites" disassemble as **data/imm embeds inside .text**, not clean leaf functions named SEMAPHORE_* — consistent with compiler-inlined constants.

Sema formal region itself is **rodata-style dword table**, not executable prologue (correct for pass17 model).

---

## 11. Mesa pass21 gap audit (post tick160)

Grep across nvgpu + nvidia vulkan/common headers: pass21 symbols present (`nv_push_g0_g4_host_sema_tail_pass21`, `NV_PASS21_*`, `nv_pass21_compute_object`, G3 barrier pass21, MME RAM_DATA/ADDR constants, path C gate). Open items remain gates/live-trace:

1. Silicon sema completion G1–G4 (no `/dev/nvidia*` on autoloop host)
2. Live RAM_DATA / INLINE / PCAS **order** capture
3. Real MME ISA beyond stub END (path C indirect)
4. Full NIR → SASS beyond pass21 kind ladder (exit / store_imm / s2r multi / s2r+store)

Pass22 does **not** add mandatory mesa code changes; optional tick161 = commit this trace + constant comments only.

---

## 12. How the open mesa stack should map (mental model)

```
App (GL/VK/CL)
  → mesa state tracker / Vulkan nvrm
  → nv_channel / nv_push (runtime headers + methods)
  → G0 host / G1 copy / G2 compute / G3 3D / G4 video slices
  → pass21 host sema tail (all engines, formal row policy)
  → G2: QMD/SPH/SASS object (pass21 kind ladder) + INLINE/PCAS explicit
  → G3: NVC597 inv/CB/report + WFI + pass21 barrier helpers
  → G4: NVDEC/NVENC pic/setup bringup + pass21 sema
  → ioctl /dev/nvidia* (RM) — NOT exercised on RE host
```

Proprietary driver does the same **logically**, but with opaque x86 emitters; pass22 shows we cannot lift those emitters as ordered templates, only as **policy constants + method vocabulary**.

---

## 13. Implications for mesa (tick161+)

1. **Keep pass21 unified host sema** — formal 11/11 + cross-lib identity reconfirmed (fix bases for egl/vksc).
2. **Keep explicit emit** — chain forward_ok near-zero validates pass19/21 design.
3. **Keep path C gated** — no static MME program unlock.
4. **Keep runtime push headers** — static 0x2000/0x2001 effectively absent for emit methods.
5. **Optional:** document pass22 egl/vksc sema bases (`0x114f2e0`, `0x877380`) in traces; pass20 INLINE medians unchanged for glcore/cuda/vksc.
6. **Silicon next:** run smoke with pass21 sema ladder + `NV_MME_PASS21_PROBE_RAM_DATA=1`; capture pushbuffer; compare to pass22 expectations.
7. **NIR next:** extend beyond `nv_pass21_compute_object` kind ladder toward real lower/opt.

---

## 14. Artifact index

| Path | Content |
|------|---------|
| `tables/pass22s_summary.json` | Per-lib elapsed, medians, chain_ok_sum |
| `tables/*_pass22s.json` | Full per-lib analysis (16 libs) |
| `tables/cross_lib_method_matrix.json` | Method×lib imm counts |
| `sema/*_formal*.txt`, `SEMA_HUNT.md` | Formal dumps + signature hunt |
| `sema/cross_lib_identity.json` | glcore≡egl≡vksc |
| `classes/*_ladders.json` | Class ID rodata hits |
| `objdump_windows/` | 60+ disasm snippets + ELF headers |
| `notes/GLCORE_DEEP_INTERIM.md` etc. | Fast interim scans |
| `mesa_gap/pass22_gap.json` | Wire audit |
| `delta_p21/inline_pcas_delta.json` | pass20 vs pass22 medians |
| `pass22_supplement.log` | Supplement run log |
| `SUPPLEMENT_COMPLETE.txt` | Done marker (36.1s, 16 libs) |

Companion `pass22_deep_analysis.py` may still run slowly on glcore (O(n) exact_hdr × methods); supplement is authoritative for pass22 synthesis.

---

## 15. Pass timeline (RE → mesa)

| Pass | Focus | Mesa ticks |
|------|-------|------------|
| 17 | Sema formal 11 rows | sema modes / BLOB1004 |
| 18 | Multi-lib scaffold | class/method vocab |
| 19 | NVC597 vs imm-family; emitter reality | inv/CB/report helpers |
| 20 | INLINE→PCAS medians; path C gate | tick152–153 |
| 21 | G0–G4 unified sema; MME RAM scaffold; compute depth | tick155–160 |
| **22** | **Reconfirm + quantify + system understanding** | **trace/doc; tick161 optional** |

---

*Pass22 deep RE pass complete (supplement). Primary understanding: proprietary userspace is runtime-emitter + embedded policy tables; open mesa pass21 mirrors that split correctly.*
