# HW Model Pass 11 — Deep RE / Disasm (NVIDIA-Linux-x86_64-610.43.02)

**Date:** 2026-06-25  
**Scope:** Multi-hour pass over proprietary userspace blobs; focus on class ladders,
host sema modes, NVENC/NVDEC method templates, and engine selection tables.  
**Artifacts:** `/tmp/nvidia-reveng-pp-v2/re_pass11/` (`pass11_mine.py`, `tables/`,
`classes/`, `methods/`, `strings/`, `notes/pass11_class_ladders.txt`)  
**Mesa impact:** `nv_device_info.c` class ladders updated; selftest -425..-431.

## Method

1. **Whole-binary dword miner** (`pass11_mine.py`) — class ID / method header /
   ctrl-cmd histograms across 17 libraries (~400 MB total).
2. **Phase2 refined scan** — rodata class *clusters* (contiguous `0x9xxx–0xdxxx`
   engine IDs), important ASCII strings, CE method histograms, sema mode proximity.
3. **Targeted ladder dumps** — exact dword windows at known glcore/cuda offsets.
4. **Cross-check** vs pass7–10 docs and open-gpu-doc method names (C9B7 SetOut*,
   NVC36F sema, NVC6B5 LAUNCH_DMA).

**Limitation (reconfirmed):** Most pushbuffer method *headers* (`0x2001xxxx`) are
**constructed at runtime** (shift/or in code), not stored as rodata templates.
Trust: (a) objdump/imm windows, (b) rodata **class ID arrays**, (c) rare literal
headers in eglcore/vdpau, (d) open-gpu-doc / OGKM class headers.

## Primary class ladders (glcore rodata — authoritative)

### DMA_COPY (`*B5`) — glcore `@ 0x11bb600`
```
CAB5 C9B5 C8B5 C7B5 C6B5 C5B5 C3B5 C1B5 C0B5 B0B5
```
Secondary (pass9/alt): `C4B5`, `A0B5` (cuda/glcore hit map).

### COMPUTE (`*C0`) — glcore `@ 0x11bb640`
```
CEC0 CDC0 CBC0 C9C0 C7C0 C6C0 C5C0 C3C0 C1C0 C0C0 B1C0 B0C0
```
Note: **skips** `CAC0`/`C8C0`/`C4C0` in this contiguous array; those appear in
pass9 cuda frequency tables and remain in mesa ladder as intermediate alts.

### 3D (`*97`) — glcore `@ 0x11bb680`
```
CE97 CD97 CB97 C997 C797 C697 C597 C397 C197 C097 B197 B097
```
`C597` is the dominant *imm* / smoke placeholder (Ampere-B / Hopper-A band).

### GPFIFO (`*6F`) — glcore `@ 0x1238b70`
```
CA6F C96F C56F C46F C36F C06F B06F A06F A26F 906F 506F
```
Cuda alt table also lists `C86F` / `C66F` (pass9 eglcore). Mesa ladder tries
`CA6F → C96F → C86F → C76F → …`.

### NVENC (`*B7`) — glcore `@ 0x1238bb4` (mixed order in table)
```
… C5B7 C4B7 B4B7 B6B7 C7B7 C8B7 C9B7 CEB7 CFB7 D1B7 …
```
Dominant in glcore frequency scan: **C8B7** (29×). Mesa ladder prefers newest
first: `D1B7 CFB7 CEB7 C9B7 C8B7 C7B7 … B4B7`.

### NVDEC (`*B0`)
No single clean contiguous `C8B0` ladder in glcore (seed scan hit 0); global
dword hits strongly favor `A0B0/B0B0/C0B0/C4B0/C8B0/C9B0`. NVENC side table
references `C4B0 C6B0 C7B0 B8B0 C9B0`. Mesa ladder: `CBB0 CAB0 C9B0 … A0B0`.

## Global class hit summary (pass11 miner, all libs)

| Class | Hits | Notes |
|-------|------|-------|
| A0B0 (NVDEC) | 155 | oldest-heavy in cuda |
| C8B7 (NVENC) | 92 | primary encode class in 610 |
| C8B0 (NVDEC) | 90 | |
| B0B0 | 88 | |
| C0B0 | 80 | |
| C0B5 (DMA) | 78 | |
| 90B5 | 74 | Fermi CE still referenced |
| C5C0 (compute) | 74 | |
| C597 (3D) | 55 | smoke/G3 default |
| C797 | 42 | Blackwell 3D |
| C56F (GPFIFO) | 11 | Hopper channel |
| C9B7 | 5 | newer NVENC (SetOut 0x718/0x71c) |
| C36F | 4 | Ampere GPFIFO |

## Host sema (pass11 confirmation)

| Execute imm | Source | Mesa mode |
|-------------|--------|-----------|
| `0x1001` | glcore imm (many sites); pass8/10 primary blob | `NV_HOST_SEMA_MODE_BLOB_*` |
| `0x0002` | vdpau | `NV_HOST_SEMA_MODE_VDPAU_*` |
| `0x1000` / `0x1002` | rare imm in histograms | documented only (`NVC36F_SEMAPHORED_RELEASE_BLOB_1000/1002`) |
| `0x01100002` | open-header theoretical | last resort (`OPEN_*`) |

eglcore retains literal `0x20040004` inc4 sema header in code stream (not glcore
rodata). Ladder order unchanged: blob align4 → blob shift2 → vdpau → open.

## NVENC method programming (pass10/11 — unchanged, reinforced)

Canonical order in mesa `nv_nvenc_emit_frame_setup`:
```
APP_ID(0x200) → CTRL(0x400) → PIC_SETUP(0x404) → IN(0x408) →
BS(0x40c) → RC(0x410) → STATUS(0x414) → OUT_ST(0x718) → OUT_BS(0x71c) →
EXECUTE(0x300) → WFI
```
- `0x718`/`0x71c`: upper 32 bits of 40-bit buffer GPU address (C9B7+ style).
- Lower `0x40c`/`0x414`: `gpu_addr >> 8` (byte offset in 256-byte units).
- `pic_stat` / `nvenc_pic_stat_s` (~128 B) at STATUS buffer; see tick125 helpers.
- glcore 3d_hist showed `0x718` method header **once** as literal — rare but
  confirms the method number exists in the binary, not only in headers.

## NVDEC status (tick126 / pass11)

`nvdec_status_s` layout from open-gpu-doc `nvdec_drv.h`; host polls sema then
reads status BO. Not re-derived from blob (few literals); keep silicon validate
as open item.

## CE / DMA methods (NVC6B5 family — open-gpu-doc aligned)

Key methods unchanged in `nv_copy_methods.h`:
`OFFSET_IN/OUT`, `PITCH_*`, `LINE_LENGTH_IN`, `LINE_COUNT`, `LAUNCH_DMA(0x300)`,
`SET_SEMAPHORE_*`, phys modes, blocklinear origins.

pass11 CE histogram on glcore showed sparse literal headers (`0x400`, `0x041`,
`0x030`) — consistent with runtime construction.

## 3D methods (NVC597 family)

pass10/tick128 viewport/scissor/clip methods remain primary for G3:
`SET_VIEWPORT_SCALE/OFFSET`, `SET_SCISSOR_*`, `SET_VIEWPORT_CLIP_*`,
`BEGIN/END`, `DRAW`, `CLEAR_SURFACE`, MME upload `0x3800+`.

pass11 3d_hist top literals are noisy (insn collision); do **not** promote
`0x34a`/`0x161` etc. without objdump window confirmation.

## QMD / compute

cuda strings heavily reference `cnpQmdLaunch*`, `cnpv2Qmd*`, `CUtoolsCnpGetGridQmd`.
QMD v02.02 (256 B / 64 dwords) + `SEND_PCAS` / inline `LOAD_INLINE_QMD_DATA`
path in `nv_qmd.h` remains correct model. Real SASS/NIR still stub/smoke.

## MME / SASS

pass11 found **zero** `0x3800` MME method headers as rodata literals in priority
libs (runtime-built). MME upload/smoke stubs in `nv_mme.h` / `nv_sph.h` unchanged;
real microcode extraction needs function-level objdump (future pass12).

## encode.so / cuvid notes

- `libnvidia-encode.so` is thin (API shim); real NVENC programmer is glcore/gpucomp.
- `libnvcuvid.so` has substantial NVDEC class/string surface; decode paths prefer
  its class ladder refinement when RM classlist incomplete.

## Mesa code changes (this pass)

1. **`nv_device_info.c`**: class ladders extended/reordered from pass11 glcore
   rodata (CA6F/CAB5/CE97/CEC0/D1B7/CBB0 heads; fuller intermediate IDs).
2. **`nv_push.h`**: pass11 sema imm notes; `0x1000`/`0x1002` constants documented.
3. **`nv_smoke_selftest.h`**: checks -425..-431 for ladder heads + sema constants.

## Open items (pass12+ / silicon)

- [ ] HW validate G1–G3 / NVDEC / NVENC on `/dev/nvidia*` (agent host: no device).
- [ ] Confirm `pic_stat` / `nvdec_status` dword offsets on silicon.
- [ ] Function-level objdump of glcore sema emit (`0x1001` write sites).
- [ ] MME instruction table extraction from gpucomp/glcore.
- [ ] Real NIR→SASS; retire MOV/EXIT smoke shaders.
- [ ] Gallium/Vulkan completeness (viewport/scissor done tick130; more state).
- [ ] libdrm nvidia unchanged since tick112; revisit if RM ioctl gaps appear.

## Library inventory scanned

| Library | Size | class clusters | method runs | hw strings |
|---------|------|----------------|-------------|------------|
| glcore | 41.8 MB | 48 | 23 | 887 |
| eglcore | 39.1 MB | 47 | 21 | 1109 |
| cuda | 112.6 MB | 500 | 156 | 36702 |
| gpucomp | 110.9 MB | 9 | 30 | 1939 |
| nvcuvid | 27.5 MB | 75 | 13 | 857 |
| rtcore | 44.9 MB | — | 2 | 327 |
| vksc-core | 11.1 MB | — | 6 | 1371 |
| vdpau | 0.6 MB | 4 | 1 | 14 |
| encode | 0.3 MB | 2 | 0 | 49 |
| others | … | … | … | … |

## Reproducer

```bash
python3 /tmp/nvidia-reveng-pp-v2/re_pass11/pass11_mine.py
# phase2 + ladder dumps: see pass11_mine.py / notes/pass11_class_ladders.txt
```
