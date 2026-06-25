# HW Model Pass 22 — Deep Disasm Multi-Hour RE
# (NVIDIA-Linux-x86_64-610.43.02)

**Started:** 2026-06-25 17:59:20
**Finished:** 2026-06-25 20:53:41
**Total elapsed:** 10460.5s
**Libs analyzed:** glcore, eglcore, cuda, vksc, gpucomp, opencl, encode, cuvid, vdpau, rtcore, ptxjit, glsi, cfg, ml

## Method

1. Load 12+ proprietary libs from 610.43.02 installer tree.
2. Imm method census + exact pushbuffer header scan (0x2000/0x2001).
3. Ordered emitter chain probes (11 chains, forward greedy 8KB).
4. Host sema formal table reconfirm (glcore/egl/vksc).
5. Sema co-occurrence at 256B/1KB/4KB; INLINE→PCAS distance stats.
6. MME RAM_ADDR/RAM_DATA order proxy + func-prologue distances.
7. x86 CFG deep samples (func head + imm context + CALL near).
8. Class ladder dword verify; cross-lib method matrix.
9. Mesa pass21 wire gap audit.

## Key results by library

| Lib | elapsed | inline→PCAS med | RAM_DATA func dist med | chain hits (sum) |
|-----|---------|-----------------|------------------------|------------------|
| glcore | 949.93s | 428 | 2064 | 8/293 |
| eglcore | 905.74s | 6108 | None | 4/295 |
| cuda | 1845.11s | 2204 | 2704 | 10/321 |
| vksc | 277.47s | 2724 | 356 | 7/276 |
| gpucomp | 1908.66s | None | None | 3/165 |
| opencl | 1825.14s | 1348 | 808 | 5/322 |
| encode | 2.63s | None | None | 2/75 |
| cuvid | 590.11s | 1084 | None | 8/278 |
| vdpau | 11.19s | None | None | 12/55 |
| rtcore | 1128.1s | None | 3732 | 19/262 |
| ptxjit | 948.96s | None | 1000 | 18/284 |
| glsi | 10.38s | None | None | 2/74 |
| cfg | 6.64s | None | None | 8/68 |
| ml | 49.12s | 5736 | 246 | 1/94 |

## Chain forward_ok detail (glcore primary)

- **inline_pcas**: 0/30 `['0x0318', '0x031c', '0x0320', '0x02b4', '0x02b8']`
- **inline_pcas_wfi**: 0/30 `['0x0318', '0x02b4', '0x0110']`
- **mme_ram_upload**: 0/7 `['0x385c', '0x3884', '0x3884', '0x39e0']`
- **mme_scratch_post**: 1/16 `['0x34a8', '0x3800', '0x39e0']`
  - ex: ['0x276d3ec', '0x276e1b4', '0x276f24c']
- **inv_wfi_sema**: 0/30 `['0x021c', '0x1330', '0x0110', '0x0208']`
- **inv_full_wfi**: 0/30 `['0x021c', '0x1330', '0x1334', '0x1338', '0x0110']`
- **cb_bind**: 0/30 `['0x2380', '0x2384', '0x2388', '0x2410']`
- **report_host**: 0/30 `['0x1b00', '0x1b04', '0x1b08', '0x1b0c', '0x0208']`
- **qmd_pcas_only**: 7/30 `['0x02b4', '0x02b8']`
  - ex: ['0x413188', '0x415184']
  - ex: ['0x5ad4b0', '0x5ae120']
- **g1_launch_sema**: 0/30 `['0x0300', '0x0208']`
- **compute_wfi_pcas**: 0/30 `['0x0110', '0x02b4']`

## Sema formal

- glcore glcore_0x11e30c0: **11/11**
- glcore egl_0x114f2f0: **0/11**
- glcore vksc_0x877390: **0/11**
- eglcore glcore_0x11e30c0: **0/11**
- eglcore egl_0x114f2f0: **0/11**
- eglcore vksc_0x877390: **0/11**
- vksc vksc_0x877390: **0/11**

## MME / RAM_DATA

### glcore
- ram_order_proxy: `{'addr_before_data_256': 2, 'data_before_addr_256': 0, 'ram_data_imm_capped': 22, 'ram_addr_imm_capped': 7}`
- ram_data_func_dist: `{'n': 2, 'median': 2064, 'sample': [1328, 2064]}`
- mme_ngrams_512: `{'3800~post': 6}`
- exact_hdrs 0x3884: `{'moff': '0x3884', 'hdr_2000': 0, 'hdr_2001': 0}`
### cuda
- ram_order_proxy: `{'addr_before_data_256': 0, 'data_before_addr_256': 0, 'ram_data_imm_capped': 11, 'ram_addr_imm_capped': 40}`
- ram_data_func_dist: `{'n': 6, 'median': 2704, 'sample': [28, 28, 808, 2704, 4184, 4664]}`
- mme_ngrams_512: `{}`
- exact_hdrs 0x3884: `{'moff': '0x3884', 'hdr_2000': 0, 'hdr_2001': 0}`
### gpucomp
- ram_order_proxy: `{'addr_before_data_256': 0, 'data_before_addr_256': 0, 'ram_data_imm_capped': 60, 'ram_addr_imm_capped': 25}`
- ram_data_func_dist: `{'n': 0, 'median': None, 'sample': []}`
- mme_ngrams_512: `{}`
- exact_hdrs 0x3884: `{'moff': '0x3884', 'hdr_2000': 0, 'hdr_2001': 0}`
### vksc
- ram_order_proxy: `{'addr_before_data_256': 0, 'data_before_addr_256': 0, 'ram_data_imm_capped': 11, 'ram_addr_imm_capped': 2}`
- ram_data_func_dist: `{'n': 5, 'median': 356, 'sample': [24, 52, 356, 1643, 4188]}`
- mme_ngrams_512: `{'3800~post': 2, '3800~ramd': 1}`
- exact_hdrs 0x3884: `{'moff': '0x3884', 'hdr_2000': 0, 'hdr_2001': 0}`

## INLINE → PCAS

- **glcore**: `{'n': 1, 'median': 428, 'p25': 428, 'p75': 428, 'min': 428, 'max': 428, 'sample': [428]}`
- **cuda**: `{'n': 2, 'median': 2204, 'p25': 1348, 'p75': 2204, 'min': 1348, 'max': 2204, 'sample': [1348, 2204]}`
- **vksc**: `{'n': 4, 'median': 2724, 'p25': 1528, 'p75': 3808, 'min': 660, 'max': 3808, 'sample': [660, 1528, 2724, 3808]}`
- **gpucomp**: `{'n': 0, 'median': None, 'p25': None, 'p75': None, 'min': None, 'max': None, 'sample': []}`
- **opencl**: `{'n': 1, 'median': 1348, 'p25': 1348, 'p75': 1348, 'min': 1348, 'max': 1348, 'sample': [1348]}`

## Exact pushbuffer headers (runtime-only reconfirm)

If hdr_2000/hdr_2001 are 0 for method offs, headers are built at runtime.

- glcore: `{'0x3884': {'moff': '0x3884', 'hdr_2000': 0, 'hdr_2001': 0}, '0x385c': {'moff': '0x385c', 'hdr_2000': 0, 'hdr_2001': 0}, '0x39e0': {'moff': '0x39e0', 'hdr_2000': 0, 'hdr_2001': 12}, '0x02b4': {'moff': '0x02b4', 'hdr_2000': 0, 'hdr_2001': 0}, '0x0318': {'moff': '0x0318', 'hdr_2000': 1, 'hdr_2001': 0}, '0x021c': {'moff': '0x021c', 'hdr_2000': 0, 'hdr_2001': 0}, '0x0208': {'moff': '0x0208', 'hdr_2000': 0, 'hdr_2001': 0}}`
- cuda: `{'0x3884': {'moff': '0x3884', 'hdr_2000': 0, 'hdr_2001': 0}, '0x385c': {'moff': '0x385c', 'hdr_2000': 0, 'hdr_2001': 0}, '0x39e0': {'moff': '0x39e0', 'hdr_2000': 0, 'hdr_2001': 0}, '0x02b4': {'moff': '0x02b4', 'hdr_2000': 0, 'hdr_2001': 0}, '0x0318': {'moff': '0x0318', 'hdr_2000': 0, 'hdr_2001': 0}, '0x021c': {'moff': '0x021c', 'hdr_2000': 0, 'hdr_2001': 0}, '0x0208': {'moff': '0x0208', 'hdr_2000': 5, 'hdr_2001': 0}}`

## Mesa pass21 gap (post tick160)

### Wired
- nv_push_g0_g4_host_sema_tail_pass21
- nv_pass21_g0_g4_sema_mode_ladder_fill
- nv_mme_emit_ram_*_pass21
- nv_compute_emit_g2_program_launch_pass21
- nv_pass21_compute_object
- nv_3d_emit_g3_barrier_*_pass21
- G1/G2/G3/G4 channel pass21 ladders

### Still open (pass22 confirms need live trace / ISA)
- Live RAM_DATA/PCAS/INLINE method order (static forward_ok ~0 expected)
- Real MME ISA non-END programs for path C indirect
- Silicon sema completion G0-G4
- Full NIR->SASS beyond pass21 depth ladder
- Pushbuffer 0x2000/0x2001 headers runtime-only (reconfirm pass22)

## Implications for mesa (tick161+)

1. Keep pass21 unified host sema (1004/slot C default) — formal table stable.
2. Do not expect static ordered templates for INLINE/PCAS/RAM_DATA — emit explicitly.
3. Path C MME remains gated until live ISA; pass21 RAM_DATA probe is vocab-only.
4. Pass22 chain forward_ok near-zero validates pass19 emitter reality thesis.
5. Next high-value: silicon capture with NV_MME_PASS21_PROBE_RAM_DATA=1; full NIR.

*Artifacts: `/tmp/nvidia-reveng-pp-v2/re_pass22/`*
