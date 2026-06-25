# Pass24 gpucomp interim RE (tick180)

**Lib:** `libnvidia-gpucomp.so.610.43.02` (~105 MB)  
**Artifacts:** `/tmp/nvidia-reveng-pp-v2/re_pass24/tables/gpucomp_pass24_focus.json`

## Method imm counts (capped scan)

| Method | Count | Role |
|--------|------:|------|
| 0x0110 WFI | 5000+ | capped |
| 0x02b8 PCAS_B | 4912 | compute/MME density |
| 0x0300 LAUNCH_DMA | 4906 | CE/compute overlap in backend |
| 0x0208 HOST_SEM_C | 1738 | sema vocabulary in compiler |
| 0x3800 MME_CALL0 | 1035 | MME call sites |
| 0x0320 LOAD_INLINE_QMD0 | 1073 | QMD load |
| 0x02b4 PCAS_A | 751 | PCAS |
| 0x0318 INLINE_QMD_A | 729 | inline QMD |
| 0x3884 RAM_DATA | 127 | MME upload payload imm |
| 0x385c RAM_ADDR | 25 | MME addr |
| 0x39e0 MME_POST | 35 | post |
| 0x34a8 MME_SC42 | 19 | scratch |

## Ordered chains (64KB forward window, sample 50)

| Chain | forward_ok | Mesa takeaway |
|-------|------------|---------------|
| mme_ram_upload (385c→3884×2→39e0) | **2** | Rare proximity; **not** static template |
| mme_scratch_post | 1 | Same |
| inline_pcas 5-step | **0** | Explicit emit mandatory |
| qmd_pcas (2b4→2b8) | 42 | Short pair only |

## RAM_DATA ±512B peers

Only **3** sampled RAM_DATA sites had other MME/sema methods within 512B — confirms pass23: upload order is imperative/runtime, not rodata chain.

## Mesa constants (tick180)

`NV_PASS24_RE_GPUCOMP_FOCUS_TICK180`, `NV_PASS24_GPUCOMP_*`, `nv_pass24_gpucomp_focus_ok()`.

## Pass25 scaffold

`NV_PASS25_RE_SCAFFOLD` inherits pass24 impl wire + pass24/23 explicit-emit; full pass25 RE pending.

*Full pass24 multi-lib disasm still pending (`NV_PASS24_RE_FULL_DISASM_PENDING`).*
