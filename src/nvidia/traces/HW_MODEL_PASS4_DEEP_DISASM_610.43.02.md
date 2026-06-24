# Pass 4 — Deep disassembly: how NVIDIA 610.43.02 binaries program hardware

**Date:** 2026-06-24  
**Scope:** Extended RE of proprietary `.so` files (glcore, eglcore, cuda, glsi, glx, glvkspirv, rtcore).  
**Method:** ELF structural scan (7 libs), x86 store-pattern scan (disp32 0x8c/0x90), targeted `objdump` of kick/schedule/token/CE/GPFIFO/push-cursor sites, class ladder rodata dumps, OGKM header correlation.  
**Artifacts:** `/tmp/nvidia-reveng-pp-v2/re_disasm/deep4/` (JSON, disasm snippets, tables).  
**Prior passes:** `HW_MODEL_FROM_BINARIES_610.43.02.md` (pass 1), `HW_MODEL_DEEP_DISASM_610.43.02.md` (pass 2–3).

---

## 0. Executive summary — what the hardware is

NVIDIA GPUs (Fermi+ / GPFIFO era) are **channel machines**. Userspace does not poke engine MMIO per draw in the normal path. Instead:

1. **RM setup (ioctl, rare):** allocate objects (client, device, subdevice, VASpace, TSG, GPFIFO channel, USERD, engines, usermode, notifiers); map buffers; schedule channel; fetch work_submit_token.
2. **Hot path (no ioctl):** write method stream into GPU-mapped pushbuffer; write GPFIFO ring entry(ies); publish **USERD.GPPut @ +0x8c**; optionally **sfence**; ring **usermode doorbell @ +0x90** with RM token.
3. **Hardware:** PBDMA/GPFIFO consumes ring; methods execute on **8 subchannels** after **SET_OBJECT** binds a class instance; engines write semaphores/notifiers; **USERD.GPGet @ +0x88** advances.

This is exactly what open `libdrm_nvidia` + mesa `nv_channel` / `nv_smoke_hw` implement. Pass 4 strengthens confidence with **cross-library** counts and **more disassembled functional blocks** (schedule, GPFIFO entry length encoding, cuda CE emit template, push-cursor vtable family).

---

## 1. Which libraries program hardware (cross-lib scan)

| Library | Size | sfence | GPPut@0x8c stores | doorbell@0x90 stores | Direct HW? | Kernel surface |
|---------|-----:|-------:|------------------:|---------------------:|------------|----------------|
| **libnvidia-glcore** | 41.8 MB | **974** | **581** | **252** | **Yes — primary GL** | `/dev/nvidia%d`, modeset, caps; `ioctl`+`mmap`+`open` |
| **libnvidia-eglcore** | 39.1 MB | 17 | **576** | **233** | **Yes — GLES/EGL** | same; fewer sfence specializations |
| **libcuda** | 112.6 MB | 23 | 77 | 99 | **Yes — compute/CE/UVM** | `nvidiactl`, `nvidia%d`, **`nvidia-uvm`**, caps |
| libnvidia-glsi | 0.6 MB | 1 | 6 | 9 | Setup/WSI only | modprobe, DRM, caps |
| libGLX_nvidia | 1.2 MB | 0 | 3 | 22 | Loader into glcore | indirect |
| libnvidia-rtcore | 44.9 MB | 0 | 25 | 228 | OptiX/RT (shader/host) | no `/dev/nvidia` strings in scan |
| libnvidia-glvkspirv | 10.4 MB | 0 | 10 | 130 | SPIR-V/compiler helper | no device paths |

**Architectural facts:**

- **No exported `RmAlloc` / `libnvidia-rm.so`.** RM client is **statically compiled** into glcore/eglcore/cuda (stripped). Open userspace must implement ioctl/NVOS (`libdrm_nvidia`) — cannot `dlsym` the blob.
- **eglcore has almost as many GPPut/doorbell stores as glcore** (576/233 vs 581/252) but only **17 sfence** vs **974**. Memory ordering is still required; eglcore likely relies more on compiler barriers / other fence paths / fewer submit specializations.
- **cuda uses fewer direct 0x8c/0x90 stores** but still has them; also uses **UVM** (`/dev/nvidia-uvm`) which GL/EGL largely avoid. Open G1/G2 smoke should **avoid UVM**.
- **rtcore/glvkspirv** have doorbell-offset immediates but are not primary channel programmers (compiler/RT stacks).

---

## 2. The complete kick path (disassembly-proven)

### 2.1 Primary kick — `glcore` VA `0xac5540` (pass 3 + reconfirmed pass 4)

```asm
; Publish GPPut to every USERD in the set (MIG / multi-subdevice / multi-runqueue)
ac5540:  mov  0x370(%rbp,%rax,8), %rdx   ; USERD host mapping[i]
ac5548:  add  $1, %rax
ac554c:  mov  %esi, 0x8c(%rdx)           ; *** USERD.GPPut = new put index ***
ac5552:  cmp  %eax, %r15d
ac5555:  ja   ac5540

; Gate: only Turing+ GPFIFO classes use usermode doorbell
ac5557:  cmpl $0xc36e, 0x1030(%rbp)      ; channel class in context
ac5561:  jbe  skip_doorbell              ; <= C36E: GPPut-only (pre-Turing)

; Order all prior stores before doorbell is globally visible
ac5585:  sfence

; Ring doorbell with RM-issued work_submit_token
ac55b0:  mov  0x8(%rax), %edx            ; token from context table
ac55b3:  mov  usermode_map(%r8,%rcx,8), %rax
ac55bf:  mov  %edx, 0x90(%rax)           ; *** NVC361_NOTIFY_CHANNEL_PENDING ***
```

Sibling site `0xac5833` repeats `cmpl $0xc36e` + `sfence` + `0x90` (alternate flag/runqueue path).

**USERD field map (`clc36f.h` `Nvc36fControl`) — offsets confirmed by store counts:**

| Offset | Field | Writer | Pass 4 evidence |
|-------:|-------|--------|-----------------|
| 0x40 / 0x44 | Put / Get (legacy PB) | mixed |  stores present but secondary |
| **0x88** | **GPGet** | **HW** (host polls) | host should not write in normal path |
| **0x8c** | **GPPut** | **host** | **581** glcore / **576** eglcore / **77** cuda stores |
| **0x90** (usermode, not USERD) | **doorbell** | **host** | **252** / **233** / **99** stores |

**Critical correction (pass 1 error):** GPPut is **`+0x8c`**, not `+0x4c`. Open `nvidia_rm.h` is correct (`GPPut /* 0x8c */`).

### 2.2 Class threshold `0xC36E`

Five exec hits in glcore including `0xa531f3` (token setup) and `0xac555d`/`0xac5833` (kick). Meaning:

- Allocated GPFIFO class **`> 0xC36E`** (`C36F`, `C46F`, `C56F`, `C86F`, …) → **must** program usermode+token+doorbell.
- Class **`<= 0xC36E`** (`C06F`, `B06F`, `A06F`, …) → **GPPut-only** kick may suffice (legacy).

Any modern GPU open bring-up will get `>= C36F` → doorbell required. Open tick76 implemented this gate.

### 2.3 Token / notif setup — `glcore` VA `0xa531d0`

Same `0xc36e` gate, then RmControl sequence (matches `ctrlc36f.h`):

```asm
a531ed:  cmpl $0xc36e, 0x1030(%rbp)
         ; if > C36E:
a53229:  mov  $0xc36f010a, %edx     ; SET_WORK_SUBMIT_TOKEN_NOTIF_INDEX
a53230:  call <RmControl wrapper>
a53269:  mov  $0xc36f0108, %edx     ; GET_WORK_SUBMIT_TOKEN
a53270:  call <RmControl wrapper>
         ; token stored in context; later loaded at ac55b0
```

| ID | Name | When |
|---:|------|------|
| `0xc36f010a` | `SET_WORK_SUBMIT_TOKEN_NOTIF_INDEX` | Once at channel setup (before get-token in blob) |
| `0xc36f0108` | `GET_WORK_SUBMIT_TOKEN` | Once at setup; result is doorbell payload |
| `0xc36f0109` | `UPDATE_FAULT_METHOD_BUFFER` | Fault buffer VA (not every submit) |

Doorbell value is **not** `1`, channel ID, or GPPut index — it is the **RM token**.

### 2.4 Channel schedule — `glcore` VA `0xa52bbc`

```asm
a52bbc:  mov  $0xa06f0103, %edx     ; NVA06F_CTRL_CMD_GPFIFO_SCHEDULE
a52bc4:  call <RmControl>
; also variant with %ecx = 0xa06f0103 at a52de0
```

Schedule is **separate from** token cmds. Without schedule: semaphores hang, GPGet stays 0, **no fault** — looks like broken kick. Open code tries channel (`A06F`) and TSG (`A06C`) schedule; pass 4 did not find `0xa06c0101` as a simple imm in glcore exec (TSG schedule may use different encoding/wrapper).

### 2.5 CUDA kick variant — `libcuda` VA `0x1ae240`

CUDA has a **USERD field writer** that programs multiple slots (possibly multi-runqueue / multi-entry USERD layout), not only simple put+doorbell:

```asm
1ae240:  mov  %eax, 0x84(%rdi)      ; field @ +0x84
1ae252:  mov  %eax, 0x88(%rdi)      ; GPGet area (init/setup path?)
1ae27f:  mov  %eax, 0x8c(%rdi)      ; GPPut — built from length/flags math
1ae291:  mov  %eax, 0x90(%rdi)      ; +0x90 on SAME mapping as USERD in this path
1ae2c4:  mov  %eax, 0x94(%rdi)      ; +0x94 further field
```

**Caution:** At `0x1ae291` the store to `0x90(%rdi)` is on the **same base as GPPut** (`%rdi` = USERD-like block), whereas glcore doorbell uses a **separate usermode mapping**. Possible interpretations:

1. CUDA sometimes uses an extended USERD / combined control page layout.
2. This function is **initializing** a shadow/control structure, not the live kick.
3. Some GPU gens alias doorbell at USERD+0x90 (less likely given `clc361.h` usermode object).

Open code should follow **glcore's split model** (USERD for GPPut/GPGet, separate usermode for `NVC361_NOTIFY_CHANNEL_PENDING`) because that matches OGKM headers and the high-confidence `ac5540` kick. Treat cuda `0x1ae240` as secondary until live trace confirms.

CUDA still has `GET_WORK_SUBMIT_TOKEN` imm at `0x47b71f` and many other sites (6+ exec hits).

---

## 3. GPFIFO entry construction

### 3.1 Length / flags encoding — `glcore` VA `0xa317c2`

```asm
a317b3:  shr  $0x4, %rcx            ; address-related shift
a317b7:  shr  $0x24, %rdx
a317c2:  and  $0x1fffff, %edx       ; *** 21-bit length field ***
a317c8:  and  $0xffe00000, %esi     ; upper flag bits preserved
a317ce:  or   %esi, %edx            ; merge length | flags → entry word
a317d0:  mov  %edx, 0x1fc(%rsp)     ; store entry component
```

Also `and $0xfe000000` on another dword (`a31792`) — high flag nibble manipulation.

**Hardware model:** GPFIFO ring entries are typically **2× u32 (8 bytes)**:

- Word 0: pushbuffer GPU address (often `va >> 2` or similar alignment encoding — exact shift varies by class/gen; open `nvidia_gp_entry_pack` must match class).
- Word 1: **length in low 21 bits** (`0x1fffff` mask appears in glcore exec at 8+ sites) plus **level/sync/fetch/wait flags** in upper bits (`0xfe000000` / `0xffe00000` patterns).

Pass 4 confirms the driver **masks length to 21 bits** and ORs with flag fields — open entry packers must not overflow length into flag bits.

### 3.2 Ring full / stall

Kick path loops while `GPGet == next_put` would overwrite unread entries (libdrm `nvidia_gpfifo_submit_one` mirrors this). Host must wait or fail with `-EAGAIN`/`-ETIMEDOUT`.

---

## 4. Pushbuffer / method protocol

### 4.1 Fermi+ method header (unchanged, reconfirmed)

| Bits | Field |
|------|-------|
| 31:29 | type: 1=inc methods, 2=non-inc, 3=imm, 4=other |
| 28:16 | count (or imm payload for type 3) |
| 15:13 | **subchannel 0–7** |
| 12:0 | method index = (byte offset on bound object) / 4 |

`SET_OBJECT` = method byte offset `0x0000` + one data word = class ID.

### 4.2 Live CE emitter — `glcore` `0xd6d0b0` (reconfirmed)

```asm
d6d0b7:  shr  $2, %edx              ; method_off >> 2
d6d0ba:  and  $0xfff, %edx          ; 13-bit method index
d6d0c0:  mov  0x68(%rax), %rsi      ; push cursor (host ptr into GPU-mapped segment)
d6d0c4:  or   $0x20018000, %edx     ; INC | c=1 | subch=4 | m=0 → SET_OBJECT on CE pipe
d6d0ca:  mov  %edx, (%rsi)          ; header
d6d0d0:  mov  %ecx, 4(%rdx)         ; class ID (e.g. C6B5/C8B5)
d6d0d3:  addq $8, 0x68(%rax)        ; advance cursor 2 dwords
```

**Subchannel is in the header constant**, not a separate MMIO write. CE primary pipe = **subch 4**.

### 4.3 CE LAUNCH_DMA — `glcore` `0xb71e52` + `cuda` `0x1ac653`

glcore:
```asm
b71e52:  movl $0x200180c0, (%rax)   ; INC | c=1 | subch=4 | method 0x300>>2
b71e58:  add  $8, %rax
b71e5c:  mov  %edx, -4(%rax)        ; LAUNCH_DMA control dword
```

cuda builds a **multi-method CE template** in one function:
```asm
1ac630:  movl $0x20048090, (%rdi)   ; multi-word CE method block header
1ac646:  movl $0x20018087, 0x14(%rdi)
1ac653:  movl $0x200180c0, 0x1c(%rdi) ; LAUNCH_DMA @ 0x300 on subch 4
1ac65d:  mov  %eax, 0x18(%rdi)      ; launch control bits
1ac668:  mov  %eax, 0x20(%rdi)      ; sema/post-launch data
```

`0x200180c0` = exact `NVC6B5_LAUNCH_DMA` at method offset `0x300` (`clc6b5.h`). Phys modes in OGKM are at `0x260`/`0x264` (`SET_SRC/DST_PHYS_MODE`); pass 4 also scanned for headers at `0x400`/`0x404` as alternate encodings (generation-dependent).

### 4.4 Host semaphore (G0 gate) — subch 0, methods `0x10`–`0x1c`

Header literals in exec: `0x20020005` (HOST_SEMA_B c2), `0x20040004` (block c4). These program **GPFIFO class** semaphores (release/WFI), not CE. If host sema fails on silicon → fix schedule/GPPut/doorbell/token, **not** CE methods.

### 4.5 Push cursor + indirect kick vtable — `glcore` `0x69c500+`

Repeated ~20+ times with different vtable slots (`0x548` … `0x590` …):

```asm
69c55b:  call *0x548(%rax)          ; per-context kick/flush specialization
69c561:  sfence
69c563:  mov  (%rbp), %eax          ; load method header at cursor
69c567:  shr  $0xd, %eax            ; extract count field (bits 28:16)
69c56a:  lea  (%rbp,%rax,4), %rax   ; advance cursor by count dwords
69c56e:  mov  %rax, (%rbx)          ; store updated cursor
```

**Interpretation:**

1. Driver maintains a **pushbuffer cursor** into GPU-mapped memory.
2. Before/around advancing, calls **TLS/context vtable** — submit/kick variants (different engines, flush modes, channels).
3. **`shr $0xd` on method header** proves the code treats real Fermi+ method headers (count in bits 28:16).
4. Many slots = specialization, not alternate architectures.

Channel context field at **`+0x68`** holds the cursor (see CE emitter). GPU sees the same bytes via GPU-VA of the segment.

---

## 5. Object / class ladders (rodata tables + frequency)

### 5.1 Embedded newest-first ladders (`glcore` rodata)

Dumped pass 4 from exact file offsets:

**CE** (`0x11bb60c`, `0x1236f8c`):
```
C7B5, C6B5, C5B5, C3B5, C1B5, C0B5, B0B5 [, A0B5 at second site]
```
Plus small capability integers (`1,3,7,4,0x24,9`) — likely partner/instance flags, not classes.

**Compute** (`0x11bb650`):
```
C7C0, C6C0, C5C0, C3C0, C1C0, C0C0, B1C0, B0C0
```
Immediately followed by **3D ladder** (`CE97…B097`) — shared classlist blob for GR.

**3D** (`0x11bb520` region): `C797, C697, C597, C397, …`  
(`C897` appears heavily in u16 frequency elsewhere — additional tables/code immediates.)

**GPFIFO** (`0x1238b6c`):
```
C46F, C36F, C06F, B06F, A06F, A26F, 906F, 506F
```
Nearby display/video runs (`C5B7…D1B7`, `C4B0…`) — **do not** allocate as channel classes.

**USERMODE:** no tight 3+ cluster; frequency orders `C761 > C661 > C561 > C461 > C361`. Doorbell offset `+0x90` stable from `C361` (`clc361.h`).

### 5.2 Frequency (u16, whole-file — noisy but directionally correct)

| Family | Dominant in glcore/eglcore | Dominant in cuda |
|--------|---------------------------|------------------|
| GPFIFO | B06F/A06F high; C36F/C86F present | C86F/B06F |
| CE | C8B5, C0B5, A0B5 | C8B5, C0B5, A0B5 |
| 3D | C897 (eglcore very heavy) | lower |
| Compute | **C3C0 dominates all** (QMD era) | **C3C0 extreme** (2779 u16) |
| Usermode | C761/C361 | C461/C661 |

Runtime selects **newest class GPU advertises** via RM classlist, else falls back. Open `nv_device_info_fill_class_ladder` must implement newest-first ladders.

### 5.3 Channel object stack (allocate order)

1. Client (`NV0000`) + device (`NV0080`) + subdevice (`NV2080`)
2. **VASpace** — all GPU VAs (push, ring, semas, BOs)
3. **TSG** (`A06C`) + **GPFIFO channel** (`C86F…C36F…`)
4. **USERD** — host-mapped control page (`Nvc36fControl`)
5. Engine children under channel: CE, 3D, compute, optional 2D (`902D`)
6. **Usermode** (`C761…C361`) — doorbell page
7. Error notifier buffer
8. **GPFIFO_SCHEDULE** (`A06F` / `A06C` ctrl)
9. **SET_NOTIF_INDEX** + **GET_WORK_SUBMIT_TOKEN** (if class > C36E)

Fail any layer → triage: ENODEV → channel EIO → sema timeout (no fault) → GPGet stuck → notifier fault.

---

## 6. Engine programming sequences (bring-up order)

### 6.1 G0 — Host sema (proves kick works)

- Subchannel **0**, GPFIFO class methods `0x10`/`0x14`/`0x18`/`0x1c`
- RELEASE/WFI sema write to GPU VA (prefer VRAM sema first)
- If fails: schedule / GPPut@0x8c / doorbell@0x90 / token / sfence — **not** CE

### 6.2 G1 — CE copy (first real engine work)

Allocate CE: `C8B5 → C7B5 → C6B5 → … → A0B5`

Emit on **subch 4** (blob primary; open may try subch 0 as fallback only):
```
SET_OBJECT          0x0000 = CE_class
[opt] SRC/DST_PHYS  0x0260 / 0x0264  (or gen-specific 0x400/404)
OFFSET_IN/OUT       0x0100 … 0x010c
PITCH / LINE_*      0x0110 … 0x011c
SET_SEMAPHORE_*     0x0060 … 0x0068
LAUNCH_DMA          0x0300  (pipelined/non-pipelined + flush + sema bits)
```
All addresses = **GPU VAs in channel VASpace**. cuda builds multi-method blocks (`0x20048090` … `0x200180c0`) — open can start with minimal SET_OBJECT + offsets + LAUNCH_DMA + sema.

### 6.3 G2 — Compute (QMD / SASS / SPH)

- Classes `C8C0…C3C0` (C3C0 dominates binaries)
- Upload SPH/SASS to GPU memory; build **QMD** at GPU VA; methods point at QMD/SPA
- Strings: `--annotateQmdState`, SASS disassemble debug, `QMD` annotate
- **Not CE.** Avoid UVM; explicit maps only
- Open smoke: host sema first, then compute sema+optional store-imm shader

### 6.4 G3 — 3D clear

- Classes `C897…B097`
- Report sema `0x0b00+`, clear surface `0x0c00`, enormous state surface
- Only after G0/G1 green; use sema-only secondary probe to isolate clear vs class/sema

### 6.5 Subchannel role summary

| Subch | Role | Bring-up |
|------:|------|----------|
| **0** | Host/GPFIFO sema, primary SET_OBJECT | **G0 first** |
| 1 | Compute/aux (some paths) | G2 |
| 2–3 | Heavy 3D/compute state | Defer |
| **4** | **CE** (proven by live emitters) | **G1** |
| 5–7 | Aux | Low |

---

## 7. RM / ioctl surface (what binaries touch)

### 7.1 Imports (all of glcore/eglcore/cuda/glsi)

`ioctl`, `mmap`/`mmap64`, `munmap`, `open`/`open64`/`openat`, `read`/`write`, `dlopen` — **no** `drmIoctl` in glcore/cuda primary (glsi/DRM path separate).

### 7.2 Device paths

| Path | glcore/egl | cuda | glsi |
|------|:----------:|:----:|:----:|
| `/dev/nvidia%d` | yes | yes | yes |
| `/dev/nvidiactl` | (via internal) | **yes** | — |
| `/dev/nvidia-uvm` | — | **yes** | — |
| `/dev/nvidia-modeset` | yes | — | — |
| `/dev/nvidia-caps…` | yes | yes | yes |
| `nvidia-drm` | yes | — | yes |

### 7.3 RM control IDs (embedded; setup frequency)

Common across libs: `NV0000` build/features, `NV0080` classlist, `NV2080` GPU info/engines/classlist, `C36F` token/notif (rare — once per channel), `A06F` schedule (setup).

Whole-file counts for `0x00000001` are huge (false positives from incrementing counters/data). Trust **exec-segment** hits for control flow (`a53229`, `a52bbc`).

---

## 8. Cross-cutting hardware mental model (diagram)

```
┌──────────────────── userspace (glcore / eglcore / cuda / open mesa) ─────────────┐
│ SETUP (ioctl, once per object):                                                   │
│   RmAlloc: client → device → subdevice → VASpace → TSG → GPFIFO ch → engines     │
│            → USERD map → usermode map → notifier                                  │
│   RmControl: SCHEDULE (A06F/A06C) → SET_NOTIF_INDEX → GET_WORK_SUBMIT_TOKEN      │
│                                                                                   │
│ HOT PATH (per submit, no ioctl):                                                  │
│   1. Emit methods at push_cursor (+0x68 in ch ctx); headers encode subch+method  │
│   2. Pack GPFIFO entry: {pb_gpu_va, len&0x1fffff | flags}; write ring[put]       │
│   3. mov next_put → USERD+0x8c  (loop all USERDs if multi)                       │
│   4. if gpfifo_class > 0xC36E: sfence; mov token → usermode+0x90                 │
│   5. Poll sema CPU map / USERD+0x88 GPGet / error notifier                       │
└────────────────────────────────────┬──────────────────────────────────────────────┘
                                     │ doorbell wakes runqueue
┌────────────────────────────────────▼──────────────────────────────────────────────┐
│ GPU: GPFIFO/PBDMA reads ring GPGet..GPPut; fetches push by GPU VA; 8 subchannels │
│      execute methods on last SET_OBJECT class per subch; engines write semas      │
└───────────────────────────────────────────────────────────────────────────────────┘
```

**There is no per-draw ioctl in the normal path.** If open G1 hangs with no fault, the bug is almost always in steps 3–4 or missing schedule — not in CE method field values.

---

## 9. Implications for open userspace (actionable checklist)

| # | Requirement | Blob evidence | Open status |
|--:|-------------|---------------|-------------|
| 1 | USERD.GPPut at **+0x8c** | 581/576/77 stores | `nvidia_rm.h` correct |
| 2 | Doorbell at usermode **+0x90** with RM token | 252/233/99 stores + `ac55bf` | implemented |
| 3 | `sfence` before doorbell (Turing+) | `ac5585` | `nvidia_gpfifo_submit_one` |
| 4 | Skip doorbell if class `<= 0xC36E` | `ac5557` | tick76 `nv_channel` |
| 5 | NOTIF_INDEX then GET_TOKEN at setup | `a53229`/`a53269` | tick76 `nv_channel` |
| 6 | Schedule channel/TSG once | `a52bbc` `0xa06f0103` | implemented |
| 7 | GPFIFO entry length **21-bit** + flags | `a317c2` `and $0x1fffff` | audit `nvidia_gp_entry_pack` |
| 8 | CE on **subch 4**, LAUNCH_DMA **0x300** | `d6d0c4` / `b71e52` / cuda `1ac653` | G1 path |
| 9 | Host sema on **subch 0** first | header literals + kick isolation | G0/G1 gate |
| 10 | Newest-first class ladders | rodata tables §5 | `nv_device_info_fill_class_ladder` |
| 11 | No UVM for GL-style bring-up | cuda-only `/dev/nvidia-uvm` | smoke uses NVOS46 |
| 12 | Multi-USERD GPPut loop | `ac5540` loop | single USERD OK for smoke |
| 13 | Push cursor in channel ctx | `+0x68` in emitters | `nv_channel` push fields |
| 14 | Method count in header bits 28:16 | `shr $0xd` at `69c567` | `nv_push_method` |

**First green on silicon:**
1. open + channel + USERD + usermode + schedule + token  
2. host sema subch 0 → sema memory changes  
3. CE subch 4 copy + sema  
4. only then compute/3D  

Triage CLI output (`nvidia_smoke_hw_cli`) maps directly to this order.

---

## 10. What pass 4 did *not* fully resolve (needs GPU box / more RE)

1. **Exact GPFIFO entry word0 address encoding** per class (shift/align differs by generation) — needs live ring dump vs `nvidia_gp_entry_pack`.
2. **TSG schedule imm** `0xa06c0101` not found as simple exec imm in glcore — may be indirect; verify open TSG schedule path on HW.
3. **cuda USERD+0x90 vs separate usermode** — ambiguous at `0x1ae240`; prefer glcore model until traced.
4. **eglcore only 17 sfence** — which barrier substitutes? Possibly `__sync_synchronize` only or weaker paths.
5. **Full ioctl/NVOS parameter layouts** — still best sourced from OGKM headers, not blob (stripped, no RmAlloc symbols).
6. **QMD/SASS/SPH exact method streams** — strings + class dominance only; need live compute trace.
7. **MME** — app/quirk strings (`VulkanZCullMmeDisabled`), not core kick model.
8. **rtcore/glvkspirv** — not analyzed in depth (compiler/RT, not primary channel driver).

---

## 11. Artifacts index

| Path | Content |
|------|---------|
| `mesa/src/nvidia/traces/HW_MODEL_PASS4_DEEP_DISASM_610.43.02.md` | **This document** |
| `mesa/src/nvidia/traces/HW_MODEL_DEEP_DISASM_610.43.02.md` | Pass 2–3 (kick smoking gun, ladders) |
| `mesa/src/nvidia/traces/HW_MODEL_FROM_BINARIES_610.43.02.md` | Pass 1 class/priority tables |
| `/tmp/nvidia-reveng-pp-v2/re_disasm/deep4/pass4_all_libs.json` | Full structural scan (7 libs) |
| `/tmp/nvidia-reveng-pp-v2/re_disasm/deep4/pass4_structural_summary.md` | Auto-generated per-lib summary |
| `/tmp/nvidia-reveng-pp-v2/re_disasm/deep4/disasm/glcore_kick_ac54e0.txt` | Full kick function |
| `/tmp/nvidia-reveng-pp-v2/re_disasm/deep4/disasm/glcore_token_sched_a531d0.txt` | Token/notif + C36E gate |
| `/tmp/nvidia-reveng-pp-v2/re_disasm/deep4/disasm/glcore_schedule_a52bbd.txt` | A06F schedule |
| `/tmp/nvidia-reveng-pp-v2/re_disasm/deep4/disasm/glcore_gpfifo_entry_a317c4.txt` | 21-bit length entry build |
| `/tmp/nvidia-reveng-pp-v2/re_disasm/deep4/disasm/glcore_ce_set_object_d6d0a0.txt` | Live SET_OBJECT s4 |
| `/tmp/nvidia-reveng-pp-v2/re_disasm/deep4/disasm/glcore_ce_launch_dma_b71e20.txt` | LAUNCH_DMA emit |
| `/tmp/nvidia-reveng-pp-v2/re_disasm/deep4/disasm/glcore_push_cursor_sfence_69c500.txt` | Cursor/vtable/sfence family |
| `/tmp/nvidia-reveng-pp-v2/re_disasm/deep4/disasm/cuda_kick_1ae27f.txt` | CUDA USERD multi-field writer |
| `/tmp/nvidia-reveng-pp-v2/re_disasm/deep4/disasm/cuda_launch_dma_1ac656.txt` | CUDA CE method template |
| `/tmp/nvidia-reveng-pp-v2/re_disasm/deep4/tables/class_ladders_dump.txt` | Rodata ladder hex |

---

## 12. Recommended next RE (on GPU box, whole-day follow-ups)

1. **`strace -e openat,ioctl,mmap`** trivial GL clear under blob — capture ioctl order vs open smoke.
2. **LD_PRELOAD ioctl logger** — diff blob vs `nvidia_smoke_hw_cli --slices 1`.
3. **Single-step / trace stores** to USERD+0x8c and usermode+0x90; compare token value and order to `ac5540`.
4. **Dump GPFIFO ring + push** after open host sema; compare entry words to `a317c2` encoding.
5. **CE emit trace** — compare open subch4 stream to `d6d0b0`/`b71e52`/`1ac653` templates.
6. **Compute QMD** — trace one cuda launch; map methods to `clc3c0.h` / QMD layout.
7. **eglcore barrier story** — why 17 sfence vs 974; does it still order correctly on x86 WC mappings?
8. **TSG vs channel schedule** — which succeeds on target GPU; document in smoke `g1_schedule_rc`.

---

*End of pass 4. Architecture is channel/pushbuffer/GPFIFO/USERD/doorbell; blobs implement it densely in glcore/eglcore/cuda; open userspace should mirror the kick and class ladders, not invent ioctl-per-draw.*
