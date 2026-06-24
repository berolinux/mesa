# Hardware model inferred from NVIDIA 610.43.02 binary .so analysis

Generated: 2026-06-24T16:21:38Z

Sources (read-only RE, no execution):
- `libnvidia-glcore.so.610.43.02` (~40 MB) — OpenGL/GLX core, primary HW programmer for graphics
- `libnvidia-eglcore.so.610.43.02` (~38 MB) — EGL/GLES core, parallel class/method profile
- `libcuda.so.610.43.02` (~108 MB) — CUDA runtime, heaviest compute/CE/QMD path
- `libnvidia-glsi.so.610.43.02` (~0.6 MB) — system/DRM/WSI/modprobe (not main submit path)
- Cross-check: `open-gpu-kernel-modules` class headers (`clc36f.h`, `clc6b5.h`, `clc597.h`, `clc3c0.h`, `clc361.h`)

Method: ELF scan for embedded class IDs (u16/u32), Fermi+ push method header histograms
(top bits 001 = incrementing methods), SET_OBJECT+class pairs, device-node/ioctl strings,
dynamic symbol imports (ioctl, mmap, open), correlation with open class method offsets.
Symbols are stripped; almost no NVOS/GPFIFO C strings in glcore — evidence is structural.

---

## 0. Bottom line: how the hardware actually works

The GPU is **not** programmed by userspace poking engine MMIO for each draw/copy in the normal path.
It is programmed as a **channel machine**:

1. Userspace builds **pushbuffers** (method header + data streams).
2. Userspace enqueues **GPFIFO** ring entries pointing at GPU-VA push segments.
3. Userspace updates **USERD.GPPut** (with sfence) and rings **usermode doorbell** (VOLTA_USERMODE_A+).
4. Kernel **RM** (`nvidia.ko`) owns objects (channel, TSG, VASpace, engines) via ioctl/NVOS on `/dev/nvidia*`.
5. Hardware **PBDMA/GPFIFO** pulls entries; **8 subchannels** execute methods after **SET_OBJECT** binds a class.
6. Engines (CE/3D/compute/2D) run; write **semas** and **notifiers**; USERD shows progress.

This is exactly what open `libdrm_nvidia` + mesa `nv_channel` / `nv_smoke_hw` target.
Binary RE strongly validates that model; it does **not** suggest ioctl-per-draw.

---

## 1. Library roles (kernel surface)

| Library | Size | Role | Kernel touchpoints |
|---------|------|------|--------------------|
| `libnvidia-glsi.so` | ~0.6 MB | Profiles, modprobe, DRM/KMS/WSI, version gate | `/dev/nvidia%d`, nvidia-caps, `drmIoctl`, `mmap` |
| `libnvidia-glcore.so` | ~40 MB | Full GL: state, shaders, push, present | `/dev/nvidia%d`, modeset, caps; imports **ioctl+mmap+open** directly |
| `libnvidia-eglcore.so` | ~38 MB | EGL/GLES; same HW machinery as glcore | same pattern |
| `libcuda.so` | ~108 MB | Contexts, memcpy, launch, streams | `/dev/nvidiactl`, `/dev/nvidia%d`, **/dev/nvidia-uvm**, caps |
| `libGLX_nvidia.so` | ~1.2 MB | Thin GLX loader into glcore | indirect |

**Key insight:** No small exported `RmAlloc` API `.so`. RM client is **statically compiled into glcore/cuda** (stripped).
Open userspace must speak ioctl/NVOS (`libdrm_nvidia`), not `dlsym` the blob.

CUDA adds **UVM** (`/dev/nvidia-uvm`). GL/EGL is primarily **RM + channel** only.
Open G1/G2 smoke should use explicit NVOS46 maps and avoid UVM.

---

## 2. Object / class model

Binaries embed complete **generation ladders** of class IDs. Runtime picks newest class the GPU
advertises (RM GET_ENGINE_CLASSLIST / device info), else falls back.

### GPFIFO

| Class | glcore u16 | eglcore u16 | cuda u16 | glcore u32 | Meaning |
|------:|-----------:|------------:|---------:|-----------:|---------|
| `0xA06F` | 226 | 89 | 79 | 3 |  |
| `0xA16F` | 46 | 37 | 31 | 1 |  |
| `0xA26F` | 62 | 74 | 38 | 2 |  |
| `0xB06F` | 221 | 73 | 68 | 4 | Pascal GPFIFO |
| `0xC06F` | 133 | 84 | 18 | 2 | Volta GPFIFO |
| `0xC36F` | 37 | 40 | 28 | 8 | Turing GPFIFO channel |
| `0xC46F` | 32 | 43 | 43 | 3 | Ampere GPFIFO |
| `0xC56F` | 29 | 35 | 32 | 2 | Hopper GPFIFO |
| `0xC86F` | 118 | 124 | 114 | 1 | Blackwell-era GPFIFO (present in 610) |

### CE/DMA

| Class | glcore u16 | eglcore u16 | cuda u16 | glcore u32 | Meaning |
|------:|-----------:|------------:|---------:|-----------:|---------|
| `0xC3B5` | 42 | 56 | 22 | 9 |  |
| `0xC5B5` | 41 | 47 | 9 | 11 |  |
| `0xC6B5` | 64 | 69 | 22 | 11 | AMPERE_DMA_COPY_A / Hopper-line CE |
| `0xC7B5` | 50 | 37 | 32 | 4 |  |
| `0xC8B5` | 132 | 115 | 252 | 29 | Newest CE in 610 ladder (very common) |

### 3D

| Class | glcore u16 | eglcore u16 | cuda u16 | glcore u32 | Meaning |
|------:|-----------:|------------:|---------:|-----------:|---------|
| `0xB097` | 116 | 74 | 38 | 19 |  |
| `0xB197` | 41 | 54 | 28 | 9 |  |
| `0xC397` | 61 | 43 | 21 | 7 |  |
| `0xC597` | 87 | 97 | 15 | 50 | AMPERE_B 3D |
| `0xC697` | 70 | 68 | 53 | 3 |  |
| `0xC797` | 73 | 62 | 62 | 5 |  |
| `0xC897` | 365 | 306 | 42 | 35 | Newest 3D in 610 |

### COMPUTE

| Class | glcore u16 | eglcore u16 | cuda u16 | glcore u32 | Meaning |
|------:|-----------:|------------:|---------:|-----------:|---------|
| `0xB0C0` | 174 | 166 | 139 | 6 |  |
| `0xB1C0` | 216 | 224 | 239 | 7 |  |
| `0xC3C0` | 2269 | 2551 | 2779 | 8 | TURING_COMPUTE_A (QMD/SPA) |
| `0xC4C0` | 116 | 97 | 199 | 1 |  |
| `0xC5C0` | 147 | 138 | 180 | 50 |  |
| `0xC6C0` | 209 | 230 | 442 | 31 |  |
| `0xC7C0` | 537 | 444 | 520 | 27 | Hopper compute |
| `0xC8C0` | 121 | 141 | 224 | 0 | Newest compute |

### 2D

| Class | glcore u16 | eglcore u16 | cuda u16 | glcore u32 | Meaning |
|------:|-----------:|------------:|---------:|-----------:|---------|
| `0x902D` | 69 | 56 | 71 | 22 | FERMI_TWOD_A — blit fallback |

### USERMODE

| Class | glcore u16 | eglcore u16 | cuda u16 | glcore u32 | Meaning |
|------:|-----------:|------------:|---------:|-----------:|---------|
| `0xC361` | 48 | 51 | 17 | 5 | VOLTA_USERMODE_A — doorbell (`NVC361_NOTIFY_CHANNEL_PENDING` @ +0x90) |
| `0xC661` | 28 | 32 | 24 | 1 | Hopper usermode |
| `0xC761` | 53 | 51 | 14 | 1 | Blackwell usermode |

### Channel stack (allocate all of these)

1. **Client / device / subdevice** — RM handles (NV0000 / NV0080 / NV2080 family)
2. **VASpace** — GPU VA for push, GPFIFO, BOs (NVOS46-style map)
3. **Channel group (TSG)** + **GPFIFO channel** (class ladder ending `0xC86F`…`C36F`)
4. **USERD** — host-mapped; HW updates `GPGet`, host writes `GPPut` (`clc36f.h` `Nvc36fControl`: GPGet@0x88, GPPut@0x8c)
5. **Engine children under channel** — CE, 3D, compute, optional 2D (`0x902D`)
6. **Usermode object** (`0xC361`/`C661`/`C761`) — doorbell mapping
7. **Error notifier** buffer

Fail any layer → `nv_smoke_hw` triage: ENODEV / EIO channel / sema timeout / GPGet stuck / notifier fault.

---

## 3. Pushbuffer protocol

### Method header (Fermi+)

Words with top bits `001` (`0x2xxxxxxx`) dominate = **incrementing method** packets:

| Bits | Field | Meaning |
|------|-------|---------|
| 31:29 | type | `001` INC, `011` non-inc, `100` immediate, … |
| 28:16 | count/imm | # data words (INC/NI) or immediate value (IMM) |
| 15:13 | subchannel | which of 8 engine pipes |
| 12:0 | method>>2 | byte offset on **currently bound** engine object |

Rebind engine: **`SET_OBJECT` (method 0x0000)** + data = class ID (`0x0000C6B5`, etc.).
Matches mesa `nv_push_method()` / `nv_push_set_object()`.

### Subchannel INC totals (this scan)

- **glcore**: subch0=23671, subch1=6180, subch2=23945, subch3=11933, subch4=90561, subch5=2336, subch6=17783, subch7=5513
- **eglcore**: subch0=19832, subch1=5842, subch2=22413, subch3=12068, subch4=77033, subch5=2238, subch6=15966, subch7=6008
- **cuda**: subch0=36584, subch1=4481, subch2=3760, subch3=47685, subch4=151089, subch5=5746, subch6=21546, subch7=8450

| Subch | Blob role | Open driver |
|------:|-----------|-------------|
| 0 | Host/GPFIFO + 3D primary; host sema 0x10-0x1c | GPFIFO sema / NV_PUSH_SUBCH_3D |
| 1-3 | Secondary / aux | context-dependent |
| **4** | **Highest volume** — CE + main 3D/compute state | CE for G1; expect 3D state here too |
| 5-7 | Lower aux | optional |

### SET_OBJECT + class sites

- **glcore**: 0 sites; top: 
- **eglcore**: 0 sites; top: 
- **cuda**: 0 sites; top: 

G1 must **RmAlloc** CE under channel **and** emit `SET_OBJECT` with CE class before CE methods.

### Top glcore methods

| subch | method | count | Best name | Meaning |
|------:|-------:|------:|-----------|---------|
| 4 | `0x2d20` | 19587 | _3D/compute state_ | 3D/shader/tex/draw or compute QMD/SPA state |
| 4 | `0x2520` | 10592 | _3D/compute state_ | 3D/shader/tex/draw or compute QMD/SPA state |
| 4 | `0x3520` | 6083 | _3D/compute state_ | 3D/shader/tex/draw or compute QMD/SPA state |
| 4 | `0x2d30` | 4533 | _3D/compute state_ | 3D/shader/tex/draw or compute QMD/SPA state |
| 4 | `0x2d24` | 4197 | _3D/compute state_ | 3D/shader/tex/draw or compute QMD/SPA state |
| 0 | `0x0000` | 3952 | NVC36F_DMA_OPCODE_METHOD | SET_OBJECT — bind class |
| 4 | `0x2530` | 3617 | _3D/compute state_ | 3D/shader/tex/draw or compute QMD/SPA state |
| 2 | `0x08a0` | 3405 | _3D/compute state_ | 3D/shader/tex/draw or compute QMD/SPA state |
| 6 | `0x1d20` | 3375 | _3D/compute state_ | 3D/shader/tex/draw or compute QMD/SPA state |
| 4 | `0x0d20` | 3330 | _3D/compute state_ | 3D/shader/tex/draw or compute QMD/SPA state |
| 4 | `0x2d10` | 3014 | _3D/compute state_ | 3D/shader/tex/draw or compute QMD/SPA state |
| 0 | `0x0d20` | 2875 | _3D/compute state_ | 3D/shader/tex/draw or compute QMD/SPA state |
| 4 | `0x2d04` | 2647 | _3D/compute state_ | 3D/shader/tex/draw or compute QMD/SPA state |
| 4 | `0x2510` | 2634 | _3D/compute state_ | 3D/shader/tex/draw or compute QMD/SPA state |
| 2 | `0x04c0` | 2626 | _3D/compute state_ | 3D/shader/tex/draw or compute QMD/SPA state |
| 4 | `0x3530` | 1999 | _3D/compute state_ | 3D/shader/tex/draw or compute QMD/SPA state |
| 4 | `0x2c00` | 1771 | _3D/compute state_ | 3D/shader/tex/draw or compute QMD/SPA state |
| 2 | `0x04a0` | 1701 | _3D/compute state_ | 3D/shader/tex/draw or compute QMD/SPA state |
| 0 | `0x0028` | 1696 | NVC36F_MEM_OP_A | 3D/shader/tex/draw or compute QMD/SPA state |
| 4 | `0x2400` | 1524 | _3D/compute state_ | 3D/shader/tex/draw or compute QMD/SPA state |
| 6 | `0x0c00` | 1227 | _3D/compute state_ | 3D/shader/tex/draw or compute QMD/SPA state |
| 4 | `0x3524` | 1212 | _3D/compute state_ | 3D/shader/tex/draw or compute QMD/SPA state |

Methods like `0x2d20`/`0x2520`/`0x3520` on subch 4 dominate because glcore is a **full 3D driver**.
For bring-up only host sema (subch 0, `0x10`-`0x1c`) and CE (`SET_OBJECT` + `0x240`/`0x300`/`0x400`…) matter.

### Host sema (first silicon gate)

`clc36f.h` on **GPFIFO class**: methods `0x10`/`0x14`/`0x18`/`0x1c` = SEMAPHORE A/B/C/D.
40-bit sema GPU VA + payload + op (`RELEASE=2`, WFI en/dis, 4B vs 16B).
Appears on **subch 0** in binaries. = `nv_channel_gpfifo_host_sema_submit`.
If this fails: fix schedule/USERD/GPPut/doorbell — **not** CE class.

### CE path (G1)

`clc6b5.h` (`0xC6B5`; ladders also `C7B5`/`C8B5` very present in 610):
1. `SET_OBJECT` CE class (try subch **4**)
2. Optional `SET_SRC/DST_PHYS_MODE` (FB / coherent sysmem / noncoherent / peer)
3. `OFFSET_IN/OUT_*`, pitch, line length/count
4. `SET_SEMAPHORE_A/B` + `PAYLOAD`
5. `LAUNCH_DMA` @ `0x300` (pipelined/non-pipelined + flush)

Class ladder **newest-first**: `C8B5 → C7B5 → C6B5 → C5B5 → C3B5`.

### Compute (G2)

Classes `0xC3C0`…`0xC8C0` dominate cuda. Launch via **QMD + SPA/SASS + SPH**, not CE.
glcore/cuda include SM trap / QMD annotate / disassemble tooling — confirms separate SM memory spaces
and QMD as first-class. Avoid UVM; explicit NVOS46 maps only.

### 3D (G3)

Ladder to `0xC897`/`C797`/…; enormous method surface. Start with clear/one tri.
`0x902D` FERMI_TWOD still in ladders as blit fallback.

---

## 4. Submission / kickoff

### GPFIFO
Push in GPU memory; ring of entries (GPU-VA + length/flags). Host advances **GPPut**; HW advances **GPGet**.
No per-method ioctl once channel runs — setup ioctls only.

### USERD (`Nvc36fControl`)
| Offset | Field | Writer |
|-------:|-------|--------|
| 0x40 | Put | host/hw |
| 0x44 | Get | hw |
| 0x48 | Reference | hw |
| 0x88 | GPGet | hw |
| 0x8c | GPPut | **host** |

Triage: neither moves = not published/scheduled; GPPut moves GPGet stuck = doorbell/schedule; both move sema wrong = methods/VA/class.

### Usermode doorbell (`0xC361`)
From `clc361.h`: **`NVC361_NOTIFY_CHANNEL_PENDING` @ +0x90** is the doorbell poke.
Also TIME_0/1 @ 0x80/0x84. Class appears as full u32 in glcore/eglcore/glsi.

Kick sequence:
1. Write GPFIFO entry(ies)
2. **sfence**
3. Update USERD GPPut
4. Write work_submit_token to usermode **+0x90**

### Schedule
RmControl `GPFIFO_SCHEDULE` on channel/TSG required or semas hang with no fault.

---

## 5. Memory model

All method addresses are **GPU VAs** in channel VASpace, never host pointers.
Every GPU-touched buffer: NVOS32 alloc + NVOS46 map + use `dma_offset` in methods.
No NVOS strings in binaries (stripped); use OGKM `nvos.h`. CE phys modes support FB and coherent sysmem.

---

## 6. Sync layers

| # | Mechanism | Proves |
|---|-----------|--------|
| 0 | USERD GPGet/GPPut | Channel consuming GPFIFO |
| 1 | Host sema GPFIFO `NVC36F_SEMAPHORE*` | Kickoff + channel methods run |
| 2 | Engine sema `NVC6B5_SET_SEMAPHORE_*` | CE/3D/compute ran |
| 3 | Error notifier | Fault vs timeout |
| 4 | GL/VK/CUDA semaphores | App-level on top of 1-2 |

G1 order: **0 → 1 → 2 → payload**. Do not skip 1.

---

## 7. Gaps (need live trace)

| Gap | Close with |
|-----|------------|
| Exact GPFIFO entry bitfields | OGKM dev_pbdma + dump from running blob |
| Token multi-channel doorbell slots | trace writes to usermode map |
| First ioctl / NVOS fill order | strace / LD_PRELOAD on blob vs open CLI |
| Byte-perfect first CE stream | capture push from GPFIFO after trivial blit |

Static RE: excellent for architecture/ladders/methods/subch. Insufficient alone for first green G1.

---

## 8. Actionable priorities

### P0 channel
1. GPFIFO class ladder: `C86F → C56F → C46F → C36F → C06F → B06F → …`
2. USERD + sfence + GPPut + doorbell @ usermode+0x90 with work_submit_token
3. GPFIFO_SCHEDULE
4. Host sema subch 0 RELEASE+WFI — **first HW pass criterion**

### P0 G1 CE
5. CE ladder: `C8B5 → C7B5 → C6B5 → C5B5 → C3B5`
6. RmAlloc CE; SET_OBJECT on subch 4; offsets; sema; LAUNCH_DMA
7. All BOs NVOS46 same VASpace; VRAM sema first

### P1 G2/G3
8. Compute `C8C0…C3C0`; QMD+SPA; no UVM
9. 3D to `C897`/fallback; clear/tri only; keep `902D` 2D blit

### P2 live trace
10. strace openat/ioctl/mmap on `nvidia_smoke_hw_cli` vs blob GL clear
11. Diff GPFIFO segment + USERD

---

## 9. Confidence

| Claim | Conf | Basis |
|-------|------|-------|
| Push+GPFIFO+USERD+doorbell arch | Very high | classes, headers, methods, devices |
| Subch 4 primary CE/3D pipe | High | INC histograms |
| Host sema subch 0 first gate | Very high | 0x10-0x1c + clc36f + smoke design |
| Newest-first incl C8xx | High | u16/u32 across 3 libs |
| Doorbell usermode+0x90 | High | clc361.h; confirm token via RM/trace |
| Exact entry bits / ioctl order | Low-med | need live trace |

---

## 10. Artifacts

- `/tmp/nvidia-reveng-pp-v2/re_disasm/HW_MODEL_FROM_BINARIES.md`
- `mesa/src/nvidia/traces/HW_MODEL_FROM_BINARIES_610.43.02.md`
- `/tmp/nvidia-reveng-pp-v2/re_disasm/*.strings_hw.txt`

