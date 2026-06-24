# Pass 5 — Very deep disassembly: how NVIDIA 610.43.02 binaries program hardware

**Date:** 2026-06-24  
**Scope:** Whole-day RE pass across 12 proprietary `.so` files (glcore, eglcore, cuda, glsi, glx, glvkspirv, rtcore, gpucomp, cfg, ml, encode, nvcuvid).  
**Method:** ELF structural scan, imm32/disp32 pattern mining, method-header histograms, class-ladder clustering, 74 targeted `objdump` windows, sema/CE/MME/GPFIFO/TSG/token/kick block analysis, cross-lib device-path/string mining, correlation with open `libdrm_nvidia` + mesa `nv_*`.  
**Artifacts:** `/tmp/nvidia-reveng-pp-v2/re_disasm/deep5/` (JSON, 74 disasm files, ladders, strings, method forms, pass5b notes).  
**Prior passes:** `HW_MODEL_FROM_BINARIES_610.43.02.md` (1), `HW_MODEL_DEEP_DISASM_610.43.02.md` (2–3), `HW_MODEL_PASS4_DEEP_DISASM_610.43.02.md` (4).

---

## 0. Executive summary — what the hardware is (reinforced)

NVIDIA GPUs (Fermi+ / GPFIFO era) are **channel machines**. Normal userspace does **not** poke engine MMIO per draw/copy. Instead:

```
┌─────────────────────────────────────────────────────────────────────┐
│  COLD PATH (ioctl / RmControl / RmAlloc — rare)                     │
│  client → device → subdevice → VASpace → TSG? → GPFIFO channel       │
│  → USERD map → engine objects (3D/CE/COMPUTE) → usermode (C361+)    │
│  → schedule (A06F/A06C) → SET_NOTIF_INDEX → GET_WORK_SUBMIT_TOKEN   │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│  HOT PATH (userspace only — no ioctl)                               │
│  1. Write method stream into GPU-mapped pushbuffer (subch 0..7)     │
│  2. Write GPFIFO ring entry(ies): VA>>2 + length[30:10] + flags     │
│  3. Publish USERD.GPPut @ +0x8c  (optionally multi-USERD loop)      │
│  4. if gpfifo_class > 0xC36E:  sfence;  usermode+0x90 = RM token    │
│  5. HW: PBDMA consumes GPFIFO → methods run on engines              │
│  6. Completion: engine/host semaphores write memory; GPGet advances │
└─────────────────────────────────────────────────────────────────────┘
```

**Pass 5 strengthens this model with:**

| Area | New / deeper evidence |
|------|----------------------|
| Kick | Full multi-USERD loop (up to 9), secondary flag gate at `0x224(%rbp)`, token table at `0x1418(%rbp)` |
| Token | Notif index math (`lea 0x10(%r12,%rax)`), dual RmControl indirection paths |
| Host sema | Exact push layout: `0x20040004` + 4 dwords; execute imm `0x1001` in primary path |
| CE | Dynamic header build `or $0x20018000`; LAUNCH_DMA `0x200180c0`; sema methods 0x40c–0x414; alt imm path `0x80000451` |
| GPFIFO | 74× `0x1fffff` mask sites; entry word1 = `(len & 0x1fffff)` merged with high flags |
| TSG | A06C classlist scan, alloc, `0xa06c0103` GET_INTERLEAVE, schedule path |
| Class ladders | Code (not just rodata): GPFIFO/CE/3D/COMPUTE/USERMODE newest-first try chains |
| MME | Real method traffic at `0x3800+` (not just GL immediate-mode strings) |
| cuda | QMD launch queue, UVM devices, kick site at `1ae27f` may be **non-USERD** control block |
| Cross-lib | 12 libs ranked by HW involvement; glcore/eglcore primary, cuda compute/UVM, others peripheral |
| Open gaps | Multi-USERD, TSG, MME, possible sema execute bit mismatch (`0x1001` vs open `0x01100002`) |

---

## 1. Which libraries program hardware (12-lib scan)

| Library | MB | sfence | Role | `/dev/nvidia*` | Notes |
|---------|---:|-------:|------|----------------|-------|
| **libnvidia-glcore** | 41.8 | **974** | **Primary GL channel programmer** | `nvidia%d`, modeset, caps, imex | 5× `C36E` gate; 25 class clusters; 74 GPFIFO masks |
| **libnvidia-eglcore** | 39.1 | 17 | **GLES/EGL channel programmer** | same family | ~same sema/CE/kick patterns; fewer sfence specializations |
| **libcuda** | 112.6 | 23 | **Compute/CE/UVM** | + **`nvidiactl`**, **`nvidia-uvm`**, **`nvidia-uvm-tools`** | QMD launch queue; no `C36E` imm; TSG schedule |
| libnvidia-gpucomp | 110.9 | 0 | Offline/compiler helper | — | Huge; GPFIFO-mask hits but not primary channel kick |
| libnvidia-rtcore | 44.9 | 0 | OptiX/RT | — | doorbell-offset noise; not channel setup |
| libnvcuvid | 27.5 | 11 | Video decode | limited | Some HW touch |
| libnvidia-glvkspirv | 10.4 | 0 | SPIR-V helper | — | compiler |
| libnvidia-ml | 2.7 | 0 | NVML | — | management, not pushbuffer |
| libGLX_nvidia | 1.2 | 0 | GLX loader | indirect | trampolines into glcore |
| libnvidia-glsi | 0.6 | 1 | WSI/setup | modprobe/DRM | little HW |
| libnvidia-cfg | 0.4 | 0 | Config | — | |
| libnvidia-encode | 0.3 | 0 | NVENC thin | — | |

**Architectural facts (pass 5 confirmed):**

1. **No exported `RmAlloc` / separate `libnvidia-rm.so`.** RM client is statically compiled into glcore/eglcore/cuda (stripped). Open userspace **must** implement ioctl/NVOS in `libdrm_nvidia` — cannot `dlsym` the blob.
2. **glcore/eglcore are the gold standard** for graphics channel/submit/sema/CE patterns.
3. **cuda is a parallel universe** for compute: QMD (`CNPqmdLaunch_st`, `cnpLaunchQueue*`), UVM (`/dev/nvidia-uvm`), possibly different control-block layouts at sites that superficially look like `+0x8c`/`+0x90` stores.
4. **gpucomp/rtcore/glvkspirv** are mostly not channel programmers despite large size and some offset immediates.
5. **glcore uses libc `ioctl` PLT** (no direct `syscall` with rax=16 found in scan) — normal; open should do the same.

### Device path inventory

| Path | glcore | eglcore | cuda |
|------|:------:|:-------:|:----:|
| `/dev/nvidia%d` | ✓ | ✓ | ✓ |
| `/dev/nvidiactl` | — | — | ✓ |
| `/dev/nvidia-modeset` | ✓ | ✓ | — |
| `/dev/nvidia-caps…` | ✓ | ✓ | ✓ |
| `nvidia-drm` (string) | ✓ | ✓ | — |
| `/dev/nvidia-uvm` | — | — | ✓ |
| `/dev/nvidia-uvm-tools` | — | — | ✓ |

**Open smoke/G1/G2/G3 should avoid UVM** (matches prior strategy).

---

## 2. The complete kick path (disassembly-proven, pass 5 detail)

### 2.1 Primary kick — `glcore` VA `0xac5540` (full context `0xac5520`)

```asm
; Preconditions: r10d = new GPPut index; %rbp = channel/context
; 0x370(%rbp) = array of USERD host mappings (up to 9 / MIG / multi-subdevice)

ac5526:  mov  $0x9, %r15d              ; max USERD slots to consider
ac552c:  mov  $0x8, %edi
ac5533:  mov  %r10d, %esi              ; GPPut value
ac5540:  mov  0x370(%rbp,%rax,8), %rdx ; USERD[i] host ptr
ac5548:  add  $1, %rax
ac554c:  mov  %esi, 0x8c(%rdx)         ; *** USERD.GPPut = put index ***
ac5552:  cmp  %eax, %r15d
ac5555:  ja   ac5540                   ; loop all USERDs

; Gate 1: only Turing+ GPFIFO classes use usermode doorbell
ac5557:  cmpl $0xc36e, 0x1030(%rbp)    ; channel GPFIFO class
ac5561:  jbe  ac55ca                   ; <= C36E: GPPut-only, skip doorbell

; Gate 2: context flag at +0x224 — if non-zero, also skip doorbell
ac5563:  mov  0x224(%rbp), %r8d
ac556a:  test %r8d, %r8d
ac556d:  jne  ac55ca

; Order all prior stores (GPFIFO entries + GPPut) before doorbell
ac5585:  sfence

; Ring doorbell(s) with RM work_submit_token from context table
ac558f:  ; index math using 0x3d8(%rbp) and 0x1418(%rbp,%rdx,8)
ac55b0:  mov  0x8(%rax), %edx          ; token from per-channel slot
ac55b3:  mov  0x27550(%r8,%rcx,8), %rax ; usermode_map[i]
ac55bf:  mov  %edx, 0x90(%rax)         ; *** NVC361_NOTIFY_CHANNEL_PENDING ***
ac55c5:  cmp  %ecx, %r15d
ac55c8:  ja   ac558f                   ; multi-doorbell loop (MIG)
```

**Sibling site** `0xac5833` repeats `cmpl $0xc36e` + `sfence` + `0x90` (alternate flag/runqueue path).

### 2.2 USERD / usermode field map (offsets confirmed)

| Offset | Location | Field | Writer | Evidence |
|-------:|----------|-------|--------|----------|
| **0x88** | USERD | **GPGet** | **HW** (host polls) | host must not write in normal path |
| **0x8c** | USERD | **GPPut** | **host** | 581+ stores pass4; pass5 loop at ac554c |
| **0x90** | **usermode** (not USERD) | **doorbell** | **host** | ac55bf; NVC361_NOTIFY_CHANNEL_PENDING |
| 0x40/0x44 | USERD | legacy Put/Get | mixed/legacy | secondary |

**Critical (pass 1 error, pass 4–5 confirmed):** GPPut is **`+0x8c`**, not `+0x4c`. Open `nvidia_rm.h` is correct.

### 2.3 Class threshold `0xC36E` (5 exec sites in glcore)

| VA | Context |
|----|---------|
| `0xa531f3` | Token setup gate |
| `0xa70485` | Secondary setup/kick path |
| `0xac555d` | Primary kick gate |
| `0xac5833` | Alternate kick gate |
| `0x10770ed` | Another submit specialization |

**Rule (open-critical):**

- GPFIFO class **`> 0xC36E`** (`C36F`, `C46F`, `C56F`, `C86F`, …) → **must** have usermode + token + doorbell.
- Class **`<= 0xC36E`** (`C06F`, `B06F`, `A06F`, …) → **GPPut-only** may suffice.

Any modern GPU bring-up gets `>= C36F` → doorbell required. Open `nvidia_gpfifo_class_needs_doorbell()` implements this.

**Extra gate (pass 5):** `0x224(%rbp)` non-zero suppresses doorbell even when class > C36E. Open does not model this flag (unknown meaning — possibly error/suspended/MIG mode). Low priority unless HW shows doorbell-but-no-progress with valid token.

### 2.4 Memory ordering

| Fence | glcore count | Role |
|-------|-------------:|------|
| **sfence** | **974** | Primary: order PB/GPFIFO/GPPut stores before doorbell |
| mfence | 5 | Rare full barrier |
| lfence | 5 | Rare load barrier |

eglcore has only **17 sfence** but **~576 GPPut / ~233 doorbell** stores (pass4 uncensored counts) — relies more on compiler barriers / fewer specialized kick paths / other orderings. Open should **always sfence** between GPPut and doorbell (matches glcore primary path; safe over-ordering).

**Pass 5 proximity scan caveat:** naive byte-scan of `8c 00 00 00` near `0f ae f8` returned 0 hits because the displacement bytes appear in many non-store contexts; the **proven** sfence is in the kick function itself at `ac5585`, between GPPut loop and doorbell loop — sufficient.

### 2.5 Token / notif setup — `glcore` VA `0xa531d0` / `0xa531ed`

```asm
a531ed:  cmpl $0xc36e, 0x1030(%rbp)
a531f7:  jbe  a532f8                 ; pre-Turing: no token path

; Loop over channels/slots (r14 counter, r12 stride 0x4c0):
a53229:  mov  $0xc36f010a, %edx      ; SET_WORK_SUBMIT_TOKEN_NOTIF_INDEX
a5322e:  mov  %ebx, %edi             ; client/device handle context
a53230:  call <RmControl wrapper>    ; f681a0 (or vtable indirection)

a5323d:  movl $0, 0x4c(%rsp)         ; clear output slot
a53269:  mov  $0xc36f0108, %edx      ; GET_WORK_SUBMIT_TOKEN
a53270:  call <RmControl wrapper>
a53275:  add  $1, %r14
a53279:  add  $0x4c0, %r12d          ; next slot stride
```

**Notif index computation** (before SET_NOTIF):

```asm
a532a3:  mov  0x3d8(%r15), %eax
a532af:  shl  $0x4, %eax
a532b2:  lea  0x10(%r12,%rax,1), %eax
a532b7:  shr  $0x4, %eax
a532ba:  mov  %eax, 0x50(%rsp)       ; notif index param
```

| Ctrl ID | Name | When | Params |
|--------:|------|------|--------|
| `0xc36f010a` | `SET_WORK_SUBMIT_TOKEN_NOTIF_INDEX` | Once per channel at setup | 4 bytes (index) |
| `0xc36f0108` | `GET_WORK_SUBMIT_TOKEN` | Once at setup; result is doorbell payload | 4 bytes out |
| `0xc36f0109` | `UPDATE_FAULT_METHOD_BUFFER` | Fault buffer VA (not every submit) | — |

**Doorbell value is the RM token**, not `1`, channel ID, or GPPut index. Token is later loaded at kick via `mov 0x8(%rax), %edx` from a per-channel context table (`0x1418(%rbp, …)` indexed).

Open order (tick76/77): notif-before-token — **matches blob**.

### 2.6 Channel schedule — `glcore` VA `0xa52bb0`

```asm
a52bb6:  mov  $0x3, %r8d             ; param size?
a52bbc:  mov  $0xa06f0103, %edx      ; NVA06F_CTRL_CMD_GPFIFO_SCHEDULE
a52bc1:  mov  %r13d, %esi
a52bc4:  call f681a0                 ; RmControl
a52bd2:  test %eax, %eax
a52bd4:  jne  a52948                 ; fail → cleanup/return 0
```

Also `0xa06f0104` (BIND?) appears in RM ctrl scan (2 hits).

### 2.7 TSG path — `glcore` VA `0xa456f0` (pass 5 new depth)

```asm
; Scan class list for TSG class 0xA06C
a456fd:  cmpl $0xa06c, (%rax)
a45703:  jne  a456f0                 ; keep scanning

; Alloc TSG object (size 0x68)
a4570c:  mov  $0x68, %esi
a45711:  mov  $0x1, %edi
a45716:  call *0x8(%rax)             ; allocator vtable

; RmAlloc with class 0xA06C
a4575f:  mov  $0xa06c, %ecx
a45764:  call 1023bf0                ; RmAlloc wrapper

; Optional: GET_INTERLEAVE
a457bf:  mov  $0xa06c0103, %edx      ; A06C_TSG_GET_INTERLEAVE
a457d2:  call f681a0                 ; RmControl
```

| Ctrl ID | Name | Role |
|--------:|------|------|
| `0xa06c0101` | `A06C_TSG_GPFIFO_SCHEDULE` | Schedule TSG (cuda has this; glcore uses channel-level A06F primarily) |
| `0xa06c0103` | `A06C_TSG_GET_INTERLEAVE` | Query interleave (glcore a457bf) |
| `0xa06c0104` | `A06C_TSG_SET_INTERLEAVE` | 1 hit in glcore |

**Open impact:** TSG is **optional** for minimal G1 (single channel + A06F schedule works in principle). Blob prefers TSG on modern drivers for timeslicing/interleave. If open channel schedule fails or progress stalls with valid doorbell, try TSG alloc + `A06C` schedule.

---

## 3. GPFIFO entry encoding (disassembly-proven)

### 3.1 Length mask / shift sites

- **74** occurrences of imm `0x001fffff` (21-bit length mask) in glcore exec.
- **200+** `shl $0xa` (shift 10) sites (capped scan) — length field at bits **[30:10]** of entry word1.

### 3.2 Builder fragment — `glcore` VA `0xa317c2`

```asm
a317b3:  shr  $0x4, %rcx             ; related address/index prep
a317b7:  shr  $0x24, %rdx
a317c2:  and  $0x1fffff, %edx        ; *** 21-bit length ***
a317c8:  and  $0xffe00000, %esi      ; preserve high 11 flag bits
a317ce:  or   %esi, %edx             ; merge length into word1
a317d0:  mov  %edx, 0x1fc(%rsp)      ; store entry word1
```

### 3.3 Entry format (OGKM + binary)

| Word | Field | Encoding |
|-----:|-------|----------|
| 0 | GET / PB VA | `VA >> 2` in bits [31:2]; low bits may carry PRIV/LEVEL/SYNC depending on class |
| 1 | LENGTH + flags | `LENGTH = (bytes/8 or dwords?) & 0x1fffff` at bits [30:10]; high bits flags |

Open `nvidia_gpfifo_submit_one_ex` clamps length to 21 bits — **correct**.

---

## 4. Method stream / subchannels (what engines execute)

### 4.1 Method header forms (Fermi+ userspace encoding)

| Form | Top nibble | Example | Meaning |
|------|-----------:|---------|---------|
| **INC1** | `0x2` | `0x20010000` | incrementing, count=1, subch0, method 0 (SET_OBJECT) |
| **INC4** | `0x2` | `0x20040004` | incrementing, count=4, subch0, method 0x10 (sema block) |
| **INC2** | `0x2` | `0x20020005` | incrementing, count=2, subch0, method 0x14 |
| **NONINC** | `0x4` | `0x40000000` | non-incrementing method 0 |
| **IMM** | `0x8` | `0x80000000` | immediate method (data in header / next word variants) |

Generic INC1 constructor (matches blob at `d6d0c4`):

```
hdr = 0x20000000 | (count << 16) | (subch << 13) | method
     = 0x20010000 | (subch << 13) | method     // for count=1
```

CE SET_OBJECT subch4: `0x20018000 | method` with method=0.

### 4.2 Subchannel convention (blob practice)

| Subch | Typical object | Evidence |
|------:|----------------|----------|
| **0** | GPFIFO/host (C36F) or 3D (C597/C897) after SET_OBJECT | sema `0x20040004`; SET_OBJECT `0x20010000` (271 hits) |
| **1** | Compute (C3C0/C7C0/C8C0) | inc1 s1 methods sparse but present |
| **2** | 2D / aux | `0x20014000` rare |
| **3** | — | NI s3 common in data |
| **4** | **Copy engine (C6B5/C8B5)** | `0x20018000` SET_OBJECT (21), `0x200180c0` LAUNCH_DMA (28) |
| **5–7** | Aux / video / other | occasional |

Open G1 uses: subch0 host sema, subch4 CE — **matches blob**.

### 4.3 Host semaphore (NVC36F) — primary kickoff probe

**Primary emit form** (`glcore` `0xb6c938`) — 9 sites with imm `0x20040004`:

```asm
b6c938:  movl $0x20040004, (%rbx)     ; INC4, subch0, method 0x10
b6c93e:  mov  0xc(%rsp), %eax         ; sema_addr_hi prep
b6c946:  mov  %r13d, 0xc(%rbx)        ; +0x0c = SEMAPHOREC payload
b6c94a:  mov  %eax, 0x4(%rbx)         ; +0x04 = SEMAPHOREA addr_hi
b6c94d:  mov  0x8(%rsp), %rax         ; sema_addr_lo
b6c952:  movl $0x1001, 0x10(%rbx)     ; +0x10 = SEMAPHORED execute
b6c959:  mov  %eax, 0x8(%rbx)         ; +0x08 = SEMAPHOREB addr_lo
; cursor advanced to +0x14 (lea 0x14(%rbx), %rsi earlier)
```

**Pushbuffer layout (5 dwords = 20 bytes):**

| Offset | Value | Method | Meaning |
|-------:|-------|--------|---------|
| +0x00 | `0x20040004` | header | inc4 @ method 0x10 |
| +0x04 | `addr >> 32` | 0x10 SEMAPHOREA | high address bits |
| +0x08 | `addr` (lo, possibly `>>2` encoded) | 0x14 SEMAPHOREB | low address |
| +0x0c | `payload` | 0x18 SEMAPHOREC | signal value |
| +0x10 | **`0x00001001`** | 0x1c SEMAPHORED | execute / op |

**Alternate forms:**

| Header | Sites | Role |
|--------|------:|------|
| `0x20020005` (INC2 @ 0x14) | 4 | Partial sema update (addr_lo + payload?) |
| `0x20010007` (INC1 @ 0x1c) | 1 | Execute-only |

#### 4.3.1 ⚠️ Sema execute: blob `0x1001` vs open `0x01100002`

Open `nv_push_host_semaphore_release` emits:

```c
NVC36F_SEMAPHORED_OPERATION_RELEASE |   /* 0x2 */
NVC36F_SEMAPHORED_RELEASE_WFI_DIS |     /* 1<<20 */
NVC36F_SEMAPHORED_RELEASE_SIZE_4BYTE    /* 1<<24 */
/* = 0x01100002 */
```

Blob primary path hardcodes **`0x1001`** (`= 0x1 | 0x1000`).

Possible interpretations:

1. **Different bitfield layout** in the specific class version the emit targets (still NVC36F family but different define set than open headers).
2. **`0x1001` is a reduced/legacy encoding** that HW still accepts (bit0=op variant, bit12=size/switch).
3. **Open is more correct for Turing+** and blob uses a compatibility constant that happens to work for its tested path.

**Action for silicon bring-up:** if host sema does not complete with open encoding, **try emitting `0x1001` as execute** (match blob exactly) as an A/B test. This is one of the highest-value pass5 deltas for G1.

Also note: blob stores **addr_lo at +0x08** as full low 32 bits from stack (`0x8(%rsp)`), not obviously `>>2` masked in this site — open uses `(addr >> 2) & 0x3fffffff` in SEMAPHOREB. Verify against `clc36f.h` which encoding is correct; if HW rejects sema, A/B both execute bits and address encoding.

### 4.4 Copy engine (C6B5/C8B5) on subch4

#### SET_OBJECT — `glcore` `0xd6d0b0` (dynamic header build)

```asm
d6d0b7:  shr  $0x2, %edx
d6d0ba:  and  $0xfff, %edx
d6d0c4:  or   $0x20018000, %edx      ; INC1 s4 method 0 + optional method bits
d6d0ca:  mov  %edx, (%rsi)           ; store header to push cursor
d6d0d0:  mov  %ecx, 0x4(%rdx)        ; store engine object handle
d6d0d3:  addq $0x8, 0x68(%rax)       ; advance cursor by 8 bytes
```

Open: emit `0x20018000` + CE object handle — **matches**.

#### LAUNCH_DMA — `glcore` `0xb71e52` (method 0x300)

```asm
b71e52:  movl $0x200180c0, (%rax)    ; INC1 s4 method 0x0c0 = byte off 0x300
b71e58:  add  $0x8, %rax
b71e5c:  mov  %edx, -0x4(%rax)       ; LAUNCH_DMA control dword
```

**28** sites with `0x200180c0` in glcore exec.  
**4** sites with legacy `0x20018080` (method 0x200).

#### CE sema methods (for completion on copy)

| Method off | hdr (inc1 s4) | glcore count | Name |
|-----------:|---------------|-------------:|------|
| 0x000 | `0x20018000` | 21 | SET_OBJECT |
| 0x200 | `0x20018080` | 4 | LAUNCH_DMA (legacy) |
| **0x300** | **`0x200180c0`** | **28** | **LAUNCH_DMA** |
| 0x400 | `0x20018100` | 2 | SET_SRC_PHYS_MODE |
| 0x404 | `0x20018101` | 2 | SET_DST_PHYS_MODE |
| 0x40c | `0x20018103` | 2 | SET_SEMAPHORE_A |
| 0x410 | `0x20018104` | 11 | SET_SEMAPHORE_B |
| 0x414 | `0x20018105` | 5 | SET_SEMAPHORE_PAYLOAD |

#### Alternate CE emit path (`0xb71e93`)

```asm
b71e93:  movl $0x80000451, (%rsi)     ; IMM form variant
b71ea1:  movl $0x20050056, 0x4(%rsi)  ; inc5 block for multi-dword setup
; ... address/pitch/size setup ...
b71eff:  movl $0x2800403f, 0x30(%rsi) ; another header form
```

Used when certain mode flags are set (`0x231b0(%rdi)` etc.). Open minimal G1 can ignore this path; use classic SET_OBJECT + offset/pitch methods + LAUNCH_DMA.

### 4.5 MME (macro methods) — not just GL immediate mode

Pass4 strings mostly hit `glBegin`/`glEnd` "immediate mode" — misleading. Pass5 method scan finds **real MME method headers** in glcore:

| Method byte off | inc1 hdr | count | Notes |
|----------------:|----------|------:|-------|
| **0x3800** | `0x20010e00` | 5 | MME base / macro start |
| 0x3928 | `0x20010e4a` | 3 | |
| 0x3930–0x3940 | … | 1–2 | |
| 0x3960 | `0x20010e58` | 2 | |
| **0x3998** | `0x20010e66` | **21** | hot macro slot |
| **0x39e0** | `0x20010e78` | **45** | hottest MME method |
| 0x3800 (non-inc) | `0x40000e00` | 2 | NI form |

String `VulkanZCullMmeDisabled` confirms MME is a real HW feature path in the driver.

**Open impact:** MME is still **END-only stubs** in mesa. Not needed for G1 (host sema + CE). Needed for efficient 3D state/setup (G3+). Priority: after G1 silicon green.

### 4.6 Compute / QMD (cuda + glcore compute ladders)

**glcore compute class ladder** (`0x5922f6` and peers): tries `C7C0 → C6C0 → C5C0 → C3C0 → C1C0 → C0C0 → B1C0 → B0C0`.

**cuda QMD symbols** (not in glcore):

- `CNPqmdLaunch_st`
- `cnpLaunchQueueLaunchMarkInvalid`
- `cnpLaunchQueueCheckAndLaunchIfNecessary_id`
- `cnpLaunchQueueFixLaunchPointers`
- `cnpSchedulerCompleteGrid` (device-side scheduler in embedded code)

Compute method headers in glcore are sparse (few direct inc1 s1 hits) — compute setup is mostly through specialized helpers / QMD memory descriptors, not simple method streams like CE.

**Open G2:** compute class ladder + QMD fill + sema; avoid cuda UVM path.

---

## 5. Class ladders (code + rodata, newest-first)

Blob does **not** hardcode one class. It walks ladders trying newest supported class first (RmAlloc fails → try older).

### 5.1 GPFIFO / channel (`a52920` code chain)

```asm
a52920:  ; cmp with 0xc36f in nearby bytes
a5292e:  cmp  $0xa26f, %eax
a52933:  je   accept
a52937:  sub  $0xb06f, %eax
a5293c:  test $0xffffefff, %eax     ; accept B06F / C06F family
a52941:  je   accept
a5295c:  cmp  $0x906f, %eax
a52967:  cmp  $0xa06f, %eax
```

**Ladder (newest → oldest, combined from code + rodata clusters):**

`C86F → C56F → C46F → C36F → C06F → B06F → A26F → A16F → A06F → 906F → 506F`

Open should try the same order (already partially in mesa/libdrm class lists).

### 5.2 Copy engine

`C8B5 → C7B5 → C6B5 → C5B5 → C3B5 → C1B5 → C0B5 → B0B5 → A0B5`  
(clusters at `ab66cc`, `a5401c`, `e587a6`, `e9333e`)

### 5.3 3D

`C897 → C797 → C697 → C597 → C397 → C197 → C097 → B197 → B097`  
(clusters at `593c0d`, `593ccb`, `11bb510`)

### 5.4 Compute

`C8C0 → C7C0 → C6C0 → C5C0 → C3C0 → C1C0 → C0C0 → B1C0 → B0C0`  
(clusters at `5922f6`, `ae63fd`, `d6483a`)

### 5.5 Usermode (doorbell region)

`C761 → C661 → C561 → C461 → C361`  
(clusters at `e92e58`, `e93182` mixed with GPFIFO/CE)

### 5.6 TSG

`A06C` (Kepler+ timeslice group) — scanned from classlist at `a456fd`, not a deep ladder.

---

## 6. RM control IDs actually used (filtered, non-flood)

| ID | Name | glcore hits | Role |
|---:|------|------------:|------|
| `0x00000214` | `NV0000_GPU_GET_ID_INFO_V2` | 2588 | GPU enumerate |
| `0x00000102` | `NV0000_SYSTEM_GET_CPU_INFO` | 1812 | host info |
| `0x00000101` | `NV0000_SYSTEM_GET_FEATURES` | 1413 | feature bits |
| `0x00000202` | `NV0000_GPU_GET_ATTACHED_IDS` | 950 | |
| `0x00800001` | `NV0080_GPU_GET_CLASSLIST` | 60 | class enumerate |
| `0x20800102` | `NV2080_GPU_GET_INFO` | 11 | subdevice info |
| **`0xc36f0108`** | **GET_WORK_SUBMIT_TOKEN** | **2** | doorbell payload |
| **`0xc36f010a`** | **SET_WORK_SUBMIT_TOKEN_NOTIF_INDEX** | **2** | notif slot |
| **`0xa06f0103`** | **A06F_GPFIFO_SCHEDULE** | **2** | run channel |
| `0xa06f0104` | A06F_BIND? | 2 | |
| `0xa06c0103` | A06C_GET_INTERLEAVE | 3 | TSG |
| `0xa06c0104` | A06C_SET_INTERLEAVE | 1 | TSG |
| `0x00800104` | `NV0080_GPU_GET_CLASSLIST_V2` | 2 | |

Channel/submit path uses a **tiny** set of ctrl IDs. Open only needs these plus alloc/map/free NVOS ioctls.

---

## 7. cuda-specific findings (do not blindly copy)

### 7.1 Device nodes

cuda opens **`/dev/nvidiactl`** and **`/dev/nvidia-uvm`** (+ tools). Graphics path (glcore) does **not** require UVM.

### 7.2 Kick site `0x1ae27f` — possibly not USERD

```asm
1ae27f:  mov  %eax, 0x8c(%rdi)       ; looks like GPPut
1ae291:  mov  %eax, 0x90(%rdi)       ; looks like doorbell — SAME base rdi!
1ae2c4:  mov  %eax, 0x94(%rdi)
1ae312:  mov  %eax, 0x9c(%rdi)
; also writes 0x98, 0xa0, ... in sequence
```

In glcore kick, **GPPut and doorbell use different bases** (`USERD` vs `usermode_map`). Here **both offsets are on `%rdi`** with sequential fields `0x8c..0xa0` — this is likely a **QMD/control-block structure copy**, not the GPFIFO USERD/doorbell pair.

**Do not treat cuda `1ae27f` as evidence for GPPut/doorbell layout.** Trust glcore `ac5540` / `ac55bf`.

### 7.3 No `C36E` threshold in cuda exec

cuda may gate doorbell/token differently (always-on for its channel classes, or indirect). Open graphics should follow **glcore** gates, not cuda.

### 7.4 QMD is first-class in cuda

Launch path goes through `cnpLaunchQueue*` and `CNPqmdLaunch_st` — memory descriptor in GPU VA, not just method stream. Open G2 must implement QMD structure (from OGKM/headers/traces), not only SET_OBJECT + methods.

---

## 8. End-to-end HW programming model (canonical sequence)

### 8.1 One-time setup (ioctl)

```
1.  open /dev/nvidiactl + /dev/nvidiaN  (not UVM for GL/G1)
2.  RmAlloc client (NV01_ROOT / NV0000)
3.  RmAlloc device (NV01_DEVICE_0 / 0x80/0x2080 family)
4.  RmAlloc subdevice (NV20_SUBDEVICE_0)
5.  RmControl GET_CLASSLIST / GET_ENGINES as needed
6.  RmAlloc VASpace
7.  RmAlloc TSG (A06C) — optional but blob does it
8.  RmAlloc GPFIFO channel (ladder C86F..A06F)
9.  RmAlloc/map USERD (channel control; host maps GPGet/GPPut page)
10. RmAlloc usermode (ladder C761..C361) + mmap doorbell page
11. RmAlloc engine objects: 3D (C897..), CE (C8B5..), COMPUTE (C8C0..)
12. Map pushbuffer + GPFIFO ring (sysmem, GPU VA)
13. RmControl A06F_GPFIFO_SCHEDULE (and/or A06C_TSG schedule)
14. if class > C36E:
      RmControl C36F_SET_WORK_SUBMIT_TOKEN_NOTIF_INDEX
      RmControl C36F_GET_WORK_SUBMIT_TOKEN  → save token
15. Zero USERD control block (GPPut=0, etc.)
```

### 8.2 Per-submit hot path (no ioctl)

```
1.  Emit methods to pushbuffer at current cursor:
      - [G1 probe] host sema inc4 block on subch0 (optional WFI first)
      - [G1 work]  SET_OBJECT CE on subch4; offsets/pitch/size; sema A/B/payload;
                   LAUNCH_DMA (0x200180c0) with RELEASE_ONE_WORD + FLUSH + PITCH
      - [G2]       compute SET_OBJECT + QMD addr methods + launch
      - [G3]       3D SET_OBJECT + state/draws (+ MME later)
2.  Build GPFIFO entry:
      word0 = (pb_gpu_va >> 2) | flags
      word1 = ((length_dwords_or_units & 0x1fffff) << 10) | high_flags
3.  Write entry into GPFIFO ring at host_gpfifo_put index
4.  next_put = (put + 1) % entries
5.  For each USERD in set (usually 1; blob up to 9):
      USERD[i].GPPut = next_put          // offset +0x8c
6.  if gpfifo_class > 0xC36E && have token && usermode_map:
      sfence
      for each usermode in set:
        usermode[i] + 0x90 = work_submit_token
7.  Poll completion:
      - sema memory == expected payload, and/or
      - USERD.GPGet advances toward GPPut
```

### 8.3 What hardware does (inferred, not in userspace)

```
doorbell/GPPut wakes PBDMA
  → PBDMA reads GPFIFO entry → fetches pushbuffer methods
  → SET_OBJECT binds class instance to subchannel
  → engine executes methods (CE copies, 3D draws, compute QMD launch)
  → sema/notifier writes signal completion to sysmem
  → GPGet advances; faults go to fault buffer (UPDATE_FAULT_METHOD_BUFFER)
```

---

## 9. Open userspace alignment matrix

| # | HW requirement (from binaries) | Open status | Priority |
|---|--------------------------------|-------------|----------|
| 1 | GPPut @ USERD **+0x8c** | ✅ `nvidia_rm.h` / `submit_one_ex` | done |
| 2 | Doorbell @ usermode **+0x90** = RM token | ✅ `nvidia_rm_doorbell_ring` | done |
| 3 | **sfence** between GPPut and doorbell | ✅ tick77 submit_one_ex | done |
| 4 | Doorbell only if **class > 0xC36E** | ✅ `nvidia_gpfifo_class_needs_doorbell` | done |
| 5 | SET_NOTIF_INDEX **before** GET_TOKEN | ✅ tick76 nv_channel | done |
| 6 | A06F_GPFIFO_SCHEDULE `0xa06f0103` | ✅ implemented | done |
| 7 | GPFIFO length **21-bit** clamp | ✅ submit_one_ex | done |
| 8 | Host sema inc4 `0x20040004` layout | ✅ via `nv_push_method` ×4 | verify exec bits |
| 9 | Host sema execute **`0x1001` A/B** vs open `0x01100002` | ⚠️ **try on silicon** | **G1 critical** |
| 10 | Host sema addr_lo encoding (`>>2` vs full) | ⚠️ verify vs clc36f | G1 critical |
| 11 | CE SET_OBJECT `0x20018000` subch4 | ✅ | done |
| 12 | CE LAUNCH_DMA `0x200180c0` method 0x300 | ✅ | done |
| 13 | CE sema A/B/payload before launch | ✅ smoke selftest checks | done |
| 14 | Class ladders newest-first | ✅ partial ladders in code | keep extending |
| 15 | Multi-USERD GPPut loop (MIG) | ❌ single USERD | low (non-MIG first) |
| 16 | TSG A06C alloc + schedule | ❌ optional | medium if schedule fails |
| 17 | MME methods `0x3800+` | ❌ END-only stubs | post-G1 (G3) |
| 18 | QMD compute launch (cuda-style) | ❌ partial/stub | G2 |
| 19 | Avoid UVM for GL/G1 | ✅ by design | done |
| 20 | Fault buffer `0xc36f0109` | ❌ optional | debug aid |
| 21 | Preemption / CTXSW bind | ❌ | advanced |
| 22 | cuda kick at 1ae27f | N/A — don't copy | note only |

---

## 10. G1 bring-up triage order (silicon, informed by pass 5)

When `nvidia_smoke_hw` fails on a real GPU, use this order (also in `nvidia_smoke_hw_cli` help):

```
1. open_rc / device node
   → /dev/nvidiactl + /dev/nvidiaN present? permissions? (not UVM)

2. channel alloc
   → GPFIFO class ladder; USERD map; usermode map (C361+)
   → log allocated gpfifo_class; confirm > C36E ⇒ doorbell expected

3. schedule
   → A06F_GPFIFO_SCHEDULE success?
   → if fail, try TSG A06C path (blob does this)

4. token
   → SET_NOTIF_INDEX then GET_TOKEN; non-zero token stored?

5. submit_ready / kick
   → GPFIFO entry written; USERD.GPPut updated (read back +0x8c)
   → if class > C36E: sfence + doorbell write observed
   → poll USERD.GPGet (+0x88): if unchanged, kick not reaching HW

6. g1_host_sema (BEFORE CE)
   → inc4 sema block only; poll sema memory
   → if fail: try execute=0x1001 (blob) vs 0x01100002 (open headers)
   → if fail: try addr_lo full-32 vs >>2 encoding
   → if fail: add WFI before sema (open has release_wfi helper)
   → if still fail: schedule/kick broken — do not debug CE yet

7. g1 CE copy
   → SET_OBJECT C8B5.. ladder; subch4; LAUNCH_DMA 0x300 with sema release
   → if host_sema ok but CE fails: class/handle/VA/pitch/flags issue

8. only then G2 (compute/QMD) / G3 (3D/MME)
```

---

## 11. Key virtual addresses quick reference (610.43.02 glcore)

| VA | Function |
|----|----------|
| `0xac5540` | **Primary kick** — multi-USERD GPPut loop |
| `0xac5557` | C36E doorbell gate |
| `0xac5585` | sfence before doorbell |
| `0xac55bf` | doorbell store usermode+0x90 |
| `0xac5833` | alternate kick C36E/sfence/doorbell |
| `0xa531ed` | token setup C36E gate |
| `0xa53229` | SET_NOTIF_INDEX `0xc36f010a` |
| `0xa53269` | GET_TOKEN `0xc36f0108` |
| `0xa52bbc` | SCHEDULE `0xa06f0103` |
| `0xa456fd` | TSG class 0xA06C scan |
| `0xa4575f` | TSG RmAlloc class A06C |
| `0xa457bf` | TSG GET_INTERLEAVE `0xa06c0103` |
| `0xa52920` | GPFIFO class accept ladder |
| `0xa317c2` | GPFIFO length `and $0x1fffff` |
| `0xb6c938` | **Host sema** inc4 emit + execute `0x1001` |
| `0xd6d0c4` | CE SET_OBJECT `or $0x20018000` |
| `0xb71e52` | CE LAUNCH_DMA `movl $0x200180c0` |
| `0xb71e93` | CE alternate IMM path `0x80000451` |
| `0xab66cc` | CE class ladder cluster |
| `0x5922f6` | COMPUTE class ladder cluster |
| `0x593c0d` | 3D class ladder cluster |

cuda (use cautiously):

| VA | Note |
|----|------|
| `0x1ae27f` | ⚠️ sequential 0x8c/0x90/0x94 on same base — likely **not** USERD/doorbell |
| `0x4776ea` | TSG schedule `0xa06c0101` region |
| `0x43cd1a` | C8C0 compute class ref |

---

## 12. Artifacts & methodology

### 12.1 On-disk artifacts

```
/tmp/nvidia-reveng-pp-v2/re_disasm/deep5/
  pass5_all_libs.json          # full structural scan (12 libs)
  pass5_structural_summary.md  # auto summary tables
  pass5b_notes.md              # sema/CE/MME/GPFIFO targeted notes
  disasm/                      # 74 objdump windows
  tables/                      # class ladder rodata dumps
  strings/                     # curated HW strings per lib
  methods/                     # method form histograms (glcore/eglcore/cuda)
```

Prior passes: `deep/`, `deep3/`, `deep4/` under same `re_disasm/` root.

### 12.2 Techniques used

1. **ELF PT_LOAD PF_X scan** — sfence/mfence/lfence counts; imm32 mining; disp32 proximity.
2. **Whole-file class clustering** — 3+ distinct class IDs within 80-byte windows → ladders.
3. **Known method header set** — inc1/inc2/inc4/ni/imm for sema/CE/MME/SET_OBJECT.
4. **Targeted objdump** — kick/token/schedule/TSG/sema/CE/GPFIFO/ladder sites.
5. **String classification** — `/dev/nvidia*`, QMD, SASS, pushbuffer, preemption (noisy but useful for cuda).
6. **Cross-correlation** — open `libdrm_nvidia` / `nv_channel` / `nv_3d_methods.h` / smoke harness.

### 12.3 Limitations

- **Stripped binaries** — no function names; VAs are version-specific to **610.43.02**.
- **Heuristic method histogram** (`top4_s0_…`) is noisy (counts any imm with top nibble 2/4/8); prefer known-header counts.
- **disp32 store counts in pass5 capped at 200** — pass4 uncensored counts (581/252) more accurate for magnitude.
- **No live trace** on this agent host (`/dev/nvidia*` absent). Model is static RE + OGKM headers; silicon must validate sema execute bits and kick progress.
- **cuda 1ae27f** not fully structurally typed — treat as non-authoritative for USERD.

---

## 13. Bottom line for fully working open userspace

The proprietary stack is a **well-factored channel programmer**:

1. **RM (ioctl)** sets up object graph and issues the few ctrl commands that cannot be done in userspace (alloc, map, schedule, token).
2. **Userspace hot path** is pure memory: methods → GPFIFO entry → GPPut → (sfence) → doorbell.
3. **Engines** are bound via SET_OBJECT on subchannels; CE/3D/compute are parallel object ladders, not separate kernel ABIs.
4. **Completion** is semaphores in GPU-accessible memory (host sema on C36F for kickoff; engine sema on CE/3D for work).
5. **MME/QMD/preemption** are optimizations/layers on top — not required for first pixel/copy.

Open `libdrm_nvidia` + mesa `nvgpu`/`nvrm` already implement the core of (1)–(4) for G1. Pass 5's highest-value remaining deltas for **silicon G1** are:

1. **A/B sema execute `0x1001` vs `0x01100002`**
2. **A/B sema address low encoding**
3. **Confirm GPGet advances after doorbell** (proves kick; if not, schedule/token/class)
4. **Optional TSG** if channel-only schedule fails
5. Only then CE class/VA debugging

G2 (compute/QMD) and G3 (3D/MME) build on the same channel/kick substrate; they do not replace it.

---

*Pass 5 complete. Artifacts in `/tmp/nvidia-reveng-pp-v2/re_disasm/deep5/`. Prior passes 1–4 remain valid; pass 5 adds sema exact layout, TSG depth, MME method evidence, cuda caveats, 12-lib scope, and silicon A/B items.*
