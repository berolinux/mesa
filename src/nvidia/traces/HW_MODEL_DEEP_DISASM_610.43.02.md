# Deep disassembly analysis: NVIDIA 610.43.02 — how the hardware is programmed

**Generated:** 2026-06-24 (second pass — x86 disasm + structural binary scan)
**Scope:** Read-only RE of proprietary `.so` files from `NVIDIA-Linux-x86_64-610.43.02/`,
cross-checked against `open-gpu-kernel-modules` class headers.
**Goal:** Understand *how the silicon is driven from userspace*, not how GL/CUDA APIs work.

---

## Executive summary (read this first)

NVIDIA Fermi+ GPUs are programmed as a **channel/pushbuffer machine**, not via per-draw ioctl
and not via direct engine MMIO from normal app paths.

```
┌──────────── userspace (glcore / eglcore / cuda / our open stack) ────────────┐
│  1. RmAlloc objects via ioctl on /dev/nvidia*  (setup only; RM in kernel)   │
│  2. Build method stream in GPU-mapped pushbuffer (header + data words)      │
│  3. Write GPFIFO ring entry(ies) → GPU-VA of push segment + length/flags    │
│  4. sfence; write USERD.GPPut; write usermode[+0x90] = work_submit_token    │
└──────────────────────────────────┬───────────────────────────────────────────┘
                                   │ doorbell wakes PBDMA
┌──────────────────────────────────▼───────────────────────────────────────────┐
│  HW GPFIFO/PBDMA pulls entries; executes methods on 8 subchannels           │
│  SET_OBJECT binds engine class; CE/3D/compute/2D run; semas/notifiers write │
│  USERD.GPGet advances as HW consumes ring                                   │
└─────────────────────────────────────────────────────────────────────────────┘
```

**Binary evidence strength:** architecture = very high; class ladders = high; exact GPFIFO
entry bitfields / first ioctl order = still need live trace on real GPU.

This model is exactly what open `libdrm_nvidia` + mesa `nv_channel` / `nv_smoke_hw` implement.
The blobs do **not** suggest a different bring-up strategy.

---

## 1. Which libraries actually program hardware

| Library | Size | Direct HW path? | Evidence |
|---------|------|-----------------|----------|
| `libnvidia-glcore.so.610.43.02` | 41.8 MB | **Yes — primary GL/3D/CE** | `ioctl`/`mmap`/`open` imports; 974× `sfence`; class IDs; method headers; pushbuffer strings |
| `libnvidia-eglcore.so.610.43.02` | 39.1 MB | **Yes — parallel GLES/EGL** | Same class/method profile as glcore; only 18× sfence (fewer submit specializations?) |
| `libcuda.so.610.43.02` | 112.6 MB | **Yes — compute/CE/QMD + UVM** | QMD/SASS strings; heaviest `C3C0`/`C7C0`/`C8C0`; `/dev/nvidia-uvm` |
| `libnvidia-glsi.so.610.43.02` | 0.6 MB | Setup/WSI only | Device paths, DRM; almost no method headers |
| `libGLX_nvidia.so.610.43.02` | 1.2 MB | No (loader) | No `ioctl` import; delegates to glcore |

**Critical architectural fact:** There is **no** small exported `libnvidia-rm.so` with `RmAlloc` symbols.
The RM *client* is **statically linked inside** glcore/eglcore/cuda (stripped). Open userspace must
implement the ioctl/NVOS protocol itself (`libdrm_nvidia`) — cannot `dlsym` the blob for RM.

GL/EGL uses primarily `/dev/nvidia%d` + modeset/caps. CUDA additionally opens `/dev/nvidia-uvm`.
Open G1/G2 bring-up should **avoid UVM** and use explicit NVOS32 alloc + NVOS46 map only.

---

## 2. Object / class model (from embedded IDs)

Classes are 16-bit engine/object type IDs. RM allocates an instance; userspace programs it by
emitting `SET_OBJECT` (method 0) with the class value, then engine methods on that subchannel.

### 2.1 Class frequency (u16 occurrences across whole ELF)

Refined pass 2026-06-24; counts are structural (rodata tables + code immediates + debug tables).

| Class | Name (approx) | glcore | eglcore | cuda | glsi | Role |
|------:|---------------|-------:|--------:|-----:|-----:|------|
| `0xC86F` | GPFIFO Blackwell? | 19 | 21 | 71 | 0 | Channel GPFIFO (newest in 610) |
| `0xC56F` | GPFIFO Hopper | 15 | 22 | 11 | 1 | |
| `0xC46F` | GPFIFO Ampere | 16 | 21 | 8 | 1 | |
| `0xC36F` | GPFIFO Turing | 22 | 21 | 14 | 2 | Common baseline channel |
| `0xC06F` | GPFIFO Volta | 74 | 21 | 8 | 0 | |
| `0xB06F` | GPFIFO Pascal | 121 | 35 | 36 | 1 | |
| `0xC8B5` | DMA CE newest | 60 | 49 | 117 | 1 | **G1 CE ladder head** |
| `0xC7B5` | DMA CE Hopper | 32 | 19 | 15 | 1 | |
| `0xC6B5` | DMA CE Ampere-B | 47 | 48 | 11 | 1 | Well documented in OGKM `clc6b5.h` |
| `0xC5B5`/`C3B5`/`C0B5`/`A0B5` | older CE | present | present | present | rare | fallback ladder |
| `0xC897` | 3D newest | 81 | 253 | 23 | 0 | G3 head (eglcore very heavy) |
| `0xC797`/`C697`/`C597`/`C397` | 3D ladder | present | present | less | rare | |
| `0xC8C0` | Compute newest | 90 | 107 | 203 | 5 | G2 head |
| `0xC7C0` | Compute Hopper | 269 | 235 | 376 | 17 | |
| `0xC3C0` | Compute Turing | **821** | **860** | **1208** | 56 | Dominates all libs (QMD era) |
| `0xC361` | USERMODE Volta | 22 | 23 | 10 | 2 | **Doorbell class** |
| `0xC661`/`C761` | USERMODE Hopper/Blackwell? | 11/23 | 17/21 | 6/3 | 1/0 | usermode ladder |
| `0x902D` | FERMI_TWOD_A | 18 | 20 | 21 | 2 | 2D blit fallback |

**Implication:** Runtime selects the **newest class the GPU advertises** (RM classlist / device info),
else falls back. Open code must implement **newest-first ladders** for GPFIFO, CE, 3D, compute, usermode.

Tight consecutive u32 ladder tables were **not** found (classes stored in scattered tables / structs /
switch data, not one linear array). Frequency evidence is still strong.

### 2.2 Channel object stack (allocate all of these)

Order is logical; exact NVOS ioctl sequence needs live trace:

1. **Client** (NV0000 family) + **device** (NV0080) + **subdevice** (NV2080)
2. **VASpace** — all GPU VAs for push, GPFIFO ring, semas, BOs live here
3. **Channel group (TSG)** + **GPFIFO channel** (`C86F`…`C36F`…`A06F`)
4. **USERD** — host-mapped control page (`Nvc36fControl` in `clc36f.h`)
5. **Engine children** under channel: CE (`C8B5`…), 3D (`C897`…), compute (`C8C0`…), optional 2D (`902D`)
6. **Usermode object** (`C761`/`C661`/`C361`) — mapped; doorbell at **+0x90**
7. **Error notifier** buffer
8. **GPFIFO_SCHEDULE** (RmControl) — without this, semas hang with no fault
9. **work_submit_token** via `NVC36F_CTRL_CMD_GPFIFO_GET_WORK_SUBMIT_TOKEN` (OGKM confirms RPC)

Fail any layer → open CLI triage order: ENODEV → channel EIO → sema timeout → GPGet stuck → notifier fault.

---

## 3. Pushbuffer protocol (Fermi+ method headers)

### 3.1 Encoding

Every method packet starts with a 32-bit header (little-endian in GPU memory):

| Bits | Field | Values |
|------|-------|--------|
| 31:29 | type | `001` = incrementing methods, `011` = non-incrementing, `100` = immediate, … |
| 28:16 | count / imm | # data words (INC/NI) or 13-bit immediate (IMM) |
| 15:13 | subchannel | 0..7 — which of 8 engine pipes |
| 12:0 | method>>2 | byte offset on **currently bound** engine object ÷ 4 |

Rebind engine: **`SET_OBJECT` (method byte offset 0x0000)** + one data word = class ID
(e.g. `0x0000C6B5`).

This matches mesa `nv_push_method()` / `nv_push_set_object()` and OGKM headers.

### 3.2 Method header **literals** embedded in binaries (smoking gun)

Exact u32 values that encode known operations appear in glcore/eglcore/cuda as constants
(template streams, fast-path emitters, or rodata tables):

| Header (hex) | Decoded | glcore n | cuda n | Meaning |
|-------------:|---------|---------:|-------:|---------|
| `0x20010000` | INC s0 m0x0000 c1 | 18 | 9 | **SET_OBJECT** on subch 0 |
| `0x20018000` | INC s4 m0x0000 c1 | 9 | 7 | **SET_OBJECT** on subch **4** |
| `0x200180c0` | INC s4 m0x0300 c1 | **17** | **22** | **CE LAUNCH_DMA @ 0x300** on subch 4 |
| `0x20040000` | INC s0 m0x0000 c4 | 4 | **2974** | SET_OBJECT multi-word / cuda heavy |
| `0x20018100` | INC s4 m0x0400 c1 | 1 | — | CE `SET_SRC_PHYS_MODE` |
| `0x20048100` | INC s4 m0x0400 c4 | — | 8 | CE phys mode block |
| `0x20050018` | INC s0 m0x0060 c5 | — | 11 | CE sema A block (cuda) |
| `0x20020005` | INC s0 m0x0014 c2 | 2 | — | **Host sema B** (GPFIFO class methods 0x10-0x1c) |

`NVC6B5_LAUNCH_DMA` is defined at method offset **`0x00000300`** in OGKM `clc6b5.h`.
Header `0x200180c0` = type INC, count 1, subch 4, method index `0x300>>2 = 0xC0`. **Exact match.**

This is among the strongest single pieces of evidence that:
- CE runs on **subchannel 4** in the production driver
- Modern CE uses **LAUNCH_DMA at 0x300** (not only older 0x200 variants)
- SET_OBJECT precedes engine work on the same subchannel

### 3.3 Subchannel role (histogram of INC headers, refined pass)

Note: refined pass filters `count < 64` and `method < 0x8000`; earlier pass counted more broadly
(including false positives from non-push data). Both agree on **qualitative** roles.

| Subch | glcore (refined) | eglcore | cuda (refined) | Inferred role |
|------:|-----------------:|--------:|---------------:|---------------|
| 0 | 1699 | 1573 | (high in broad scan) | **Host/GPFIFO methods** (sema 0x10-0x1c), primary SET_OBJECT |
| 1 | 494 | 501 | moderate | Aux / secondary engines |
| 2 | **10049** | **10072** | moderate | Heavy **3D/compute state** methods in GL (offsets 0x4a0, 0x8a0, …) |
| 3 | 2257 | 2240 | high in broad | More 3D/compute state |
| **4** | 646 refined / **very high** broad | 359 / high | **dominant** in broad | **CE + main engine pipe** (LAUNCH_DMA literals confirm CE here) |
| 5-7 | low | low | moderate | Aux / less used |

**Bring-up guidance:**
- **G0/G1 host sema:** subch **0**, methods `0x10`/`0x14`/`0x18`/`0x1c` on GPFIFO class (`clc36f.h`)
- **G1 CE:** subch **4**, `SET_OBJECT` CE class, then CE methods, `LAUNCH_DMA` @ `0x300`
- Do not start with subch 2/3 3D state — enormous surface, not needed for first green

### 3.4 Host semaphore (first silicon gate)

On the **GPFIFO class** (not CE), methods:
- `0x10` SEMAPHORE_A (addr hi/lo parts depending on class)
- `0x14` SEMAPHORE_B
- `0x18` SEMAPHORE_C / payload
- `0x1c` SEMAPHORE_D / execute (RELEASE=2, WFI, 4B vs 16B sema)

Header literals `0x20020005` (HOST_SEM_B) etc. appear in glcore. If host sema fails on silicon:
fix **schedule / USERD / GPPut / doorbell** — not CE class or LAUNCH_DMA.

---

## 4. Submission path — x86 disassembly evidence

### 4.1 sfence is first-class in the driver

| Library | `sfence` (`0F AE F8`) count |
|---------|----------------------------:|
| glcore | **974** |
| eglcore | 18 |
| cuda | (high; full scan pending) |
| glsi | 1 |

Memory ordering before publishing work to the GPU is **required** by the driver design.
Open code must `sfence` (or equivalent barrier) before GPPut/doorbell.

### 4.2 Pushbuffer cursor advancement (disassembled pattern)

At `libnvidia-glcore.so` VA `0x69c540` and dozens of sibling functions in the `0x69c500–0x69d900`
cluster (exported under stripped names like `_nv019glcore`):

```asm
; pattern repeated ~20+ times with different vtable slots
mov    (%rsi), %rbp              ; pushbuffer / method cursor context
lea    ...(%rip), %rax           ; TLS key / global
mov    0x8(%rax), %rax
mov    %fs:(%rax), %rax          ; thread-local driver state
call   *0x548(%rax)              ; indirect: flush/submit/kick variant (slot varies 0x548..0x690+)
sfence                           ; order stores before GPU sees them
mov    0x0(%rbp), %eax           ; load method header at cursor
shr    $0xd, %eax                ; extract bits 28:16 = method count field
lea    0x0(%rbp,%rax,4), %rax    ; advance cursor by count words (×4 bytes)
mov    %rax, (%rbx)              ; store updated cursor
ret
```

**Interpretation:**
1. Driver maintains a **pushbuffer cursor** (`%rbp` points at current method header).
2. Before advancing, it calls a **per-context function pointer** (vtable in TLS) — almost certainly
   the actual **kick/submit** (GPPut + doorbell) for that channel/context type.
3. **`sfence` always precedes cursor consumption** in this family — confirms ordering model.
4. `shr $0xd` on the method header is exactly extracting the Fermi+ **count** field (bits 28:16),
   proving the code is operating on **real method headers**, not abstract commands.

Different vtable slots (`0x548`, `0x550`, `0x558`, … `0x690`) are specializations (different
engines, flush modes, or channel types) — not different architectures.

### 4.3 USERD + doorbell (from headers + constants; limited x86 addressing)

`Nvc36fControl` (`clc36f.h`) — **offsets confirmed by 581× `mov …, 0x8c(%reg)` in glcore `.text`:**

| Offset | Field | Writer | Role |
|-------:|-------|--------|------|
| 0x00.. | Put / Get / Ref (legacy FIFO fields) | mixed | Pre-GPFIFO / compat |
| **0x88** | **GPGet** | **hw** | HW progress through GPFIFO ring (host polls) |
| **0x8c** | **GPPut** | **host** | Host publishes new GPFIFO entries (kick step 1) |

Note: some pass-1 notes listed GPPut at `+0x4c`; that is **wrong for Turing+ `Nvc36fControl`**.
The live driver writes **`+0x8c`**. Open `nv_channel` / smoke must use `0x8c`.

`VOLTA_USERMODE_A` (`clc361.h`):

| Offset | Field |
|-------:|-------|
| 0x80/0x84 | TIME_0/1 |
| **0x90** | **`NVC361_NOTIFY_CHANNEL_PENDING`** — **doorbell** |

Whole-file u32 constant hits in glcore (earlier pass): `0x88` ~2859, `0x8c` ~1116, `0x90` ~2770,
`0x84` ~3121. These are **not** all USERD accesses (many false positives), but magnitudes are
consistent with these offsets being first-class in the binary.

**Pass 3 (2026-06-24) supersedes the above uncertainty.** Direct x86 stores to `0x8c` and `0x90`
are abundant in `libnvidia-glcore.so` executable segment:

| Pattern | Count in `.text` | Meaning |
|---------|-----------------:|---------|
| `mov %regd, 0x8c(%reg64)` | **581** | **USERD.GPPut** write |
| `mov %regd, 0x90(%reg64)` | **252** | **usermode doorbell** write |

OGKM RPC gets the doorbell value: `NVC36F_CTRL_CMD_GPFIFO_GET_WORK_SUBMIT_TOKEN` (`0xc36f0108`)
returns a token; userspace writes it to usermode+0x90. It is **not** a compile-time constant.

**Kick sequence (now validated by direct disassembly — see §4.4):**

1. Write method stream into pushbuffer (GPU VA)
2. Write GPFIFO ring entry(ies) at current GPPut index
3. Store new index to **USERD + 0x8c (GPPut)** — may loop over multiple USERD pages/subdevices
4. If channel class **> 0xC36E** (i.e. Turing+ GPFIFO `C36F` and newer): **`sfence`**, then store
   **work_submit_token** to **usermode map + 0x90** (`NVC361_NOTIFY_CHANNEL_PENDING`)
5. Older channels (`<= 0xC36E`) skip the usermode doorbell path (legacy kick only)
6. Poll sema / USERD.GPGet (`+0x88`) / notifier for completion

---

## 4.4 Pass 3 smoking gun: complete kick at `glcore+0xac5540`

Disassembled from `libnvidia-glcore.so.610.43.02` at VA `0xac5540` (function cluster under
`_nv037glcore`). This is the **production submit/kick** for multi-USERD channels:

```asm
; --- publish GPPut to every USERD in the set ---
ac5540:  mov    0x370(%rbp,%rax,8),%rdx   ; USERD host mapping for index %rax
ac5548:  add    $0x1,%rax
ac554c:  mov    %esi,0x8c(%rdx)           ; *** USERD.GPPut = new put index ***
ac5552:  cmp    %eax,%r15d
ac5555:  ja     ac5540                    ; loop all USERDs

; --- gate: only Turing+ GPFIFO classes use usermode doorbell ---
ac5557:  cmpl   $0xc36e,0x1030(%rbp)      ; channel class in context struct
ac5561:  jbe    ac55ca                    ; <= C36E: skip doorbell (pre-Turing path)
ac5563:  ... flag checks ...

; --- order all prior stores (push + GPFIFO ring + GPPut) before doorbell ---
ac5585:  sfence

; --- ring doorbell with RM-issued work_submit_token ---
ac5592:  mov    0x3d8(%rbp),%eax          ; token / runqueue index math
ac55a8:  add    0x1418(%rbp,%rdx,8),%rax  ; token table lookup
ac55b0:  mov    0x8(%rax),%edx            ; *** work_submit_token value ***
ac55b3:  mov    0x27550(%r8,%rcx,8),%rax  ; usermode object host mapping
ac55bf:  mov    %edx,0x90(%rax)           ; *** NVC361_NOTIFY_CHANNEL_PENDING ***
```

**What this proves about the hardware (and what open code must do):**

1. **GPPut is at USERD + 0x8c**, exactly `Nvc36fControl.GPPut` in `clc36f.h` (field at 0x8c, not 0x4c).
2. **GPGet is at USERD + 0x88** (companion; host reads, HW writes) — symmetric to GPPut.
3. **Doorbell is usermode + 0x90**, exactly `NVC361_NOTIFY_CHANNEL_PENDING`.
4. **`sfence` sits between GPPut and doorbell**, not before GPPut in this path — so ring/push stores
   must already be globally visible *or* the GPPut loop provides enough ordering for older CPUs;
   open code should still barrier before GPPut *and* before doorbell (harmless, matches other clusters).
5. **Doorbell payload is a dynamic token** looked up from context state (`0x3d8` / `0x1418` tables),
   obtained earlier via RmControl `GET_WORK_SUBMIT_TOKEN` — not `1`, not channel ID, not GPPut index.
6. **Class threshold `0xC36E`** means: if your allocated GPFIFO class is `C36F`/`C46F`/`C56F`/`C86F`
   you **must** program usermode+doorbell; if somehow on `C06F`/`B06F`/older, doorbell may be optional
   (legacy doorbell-less submit). Open bring-up on any modern GPU will be `>= C36F` → doorbell required.
7. **Multi-USERD loop** (`0x370(%rbp,idx,8)`) — MIG / multi-subdevice / multi-runqueue setups write
   GPPut to *every* mapped USERD. Single-GPU single-channel open smoke: one USERD is enough, but
   the loop shows the driver never assumes only one.

Second sibling site at `0xac5859` repeats the same `sfence` + `0x90` pattern (variant for different
flag/runqueue selection). Across all of glcore `.text`: **581 GPPut stores, 252 doorbell stores** —
submit is not a rare ioctl; it is a hot userspace path.

### 4.5 RM control for token / notif — disasm at `glcore+0xa53229`

```asm
a53229:  mov    $0xc36f010a,%edx          ; SET_WORK_SUBMIT_TOKEN_NOTIF_INDEX
a5322e:  mov    %ebx,%edi                 ; channel / object handle
a53230:  call   <RmControl wrapper>

a53269:  mov    $0xc36f0108,%edx          ; GET_WORK_SUBMIT_TOKEN
a5326e:  mov    %ebx,%edi
a53270:  call   <RmControl wrapper>
; result stored into context token table (feeds 0xac55b0 load above)
```

**Corrected command map** (pass 2 had wrong labels — fix against `ctrlc36f.h`):

| ID | OGKM name | Role |
|---:|-----------|------|
| `0xc36f0108` | `NVC36F_CTRL_CMD_GPFIFO_GET_WORK_SUBMIT_TOKEN` | Returns doorbell token |
| `0xc36f0109` | `NVC36F_CTRL_CMD_GPFIFO_UPDATE_FAULT_METHOD_BUFFER` | Fault buffer VA setup |
| `0xc36f010a` | `NVC36F_CTRL_CMD_GPFIFO_SET_WORK_SUBMIT_TOKEN_NOTIF_INDEX` | Binds which notifier index the token signals |

Channel **schedule** is a separate RmControl (often on TSG/channel group, not this triplet). Open code
must still issue schedule before expecting work to run; binaries embed `0xc36f0108`/`0xa` in channel
setup, not on every kick.

Embedded counts (glcore pass 3): `0xc36f0108` ×2, `0xc36f010a` ×2 in whole file — rare because setup
is once per channel, not per frame. Contrast: 581 GPPut writes (every submit).

### 4.6 CE method emission — live pushbuffer writer at `glcore+0xd6d0b0`

Not a rodata template: executable code **constructs** Fermi+ headers at runtime:

```asm
d6d0b0:  mov    0x5e8(%rsi),%rax          ; channel / push context
d6d0b7:  shr    $0x2,%edx                 ; method_byte_off >> 2  → method index
d6d0ba:  and    $0xfff,%edx               ; mask method index (13 bits)
d6d0c0:  mov    0x68(%rax),%rsi           ; current push cursor (GPU-mapped host ptr)
d6d0c4:  or     $0x20018000,%edx          ; *** INC, count=1, subch=4, method=0 (SET_OBJECT) ***
d6d0ca:  mov    %edx,(%rsi)               ; store header word
d6d0cc:  mov    0x68(%rax),%rdx
d6d0d0:  mov    %ecx,0x4(%rdx)            ; store class ID data word (e.g. 0xC6B5)
d6d0d3:  addq   $0x8,0x68(%rax)           ; advance cursor by 2 dwords
d6d0d8:  xor    %eax,%eax
d6d0da:  ret
```

**Hardware insight:** subchannel is **baked into the header constant** (`0x20018000` = subch 4),
not selected by a separate MMIO write. Rebinding an engine = write SET_OBJECT header + class on
that subchannel's stream; PBDMA routes methods to the object last bound on that subch.

CE launch site `0xb71e52` writes literal `0x200180c0` (INC, count=1, subch=4, method `0x300>>2`)
then the LAUNCH_DMA control dword — exact match to `NVC6B5_LAUNCH_DMA` at byte offset `0x300`.

Alternate CE packet forms in same function use `0x80000451` / `0x20050056` (type-4 / multi-word
encodings) — the driver has **multiple CE emit specializations** (legacy vs modern, sema-included
vs separate). Open G1 should implement the simple `0x200180c0` + control dword path first.

### 4.7 Push cursor model (confirmed again)

Channel context object holds at `+0x68` a **host pointer into the current push segment**. Emitters:
1. Load cursor
2. Store header (+ optional data words)
3. Add 4×(1+count) to cursor field
4. Eventually kick (GPPut/doorbell) via indirect vtable (`call *0x548(%tls_ctx)` family at `0x69c540+`)

GPU sees the same bytes via GPU-VA mapping of that segment; host cursor and GPU-VA base are paired
at segment bind time.

---

## 5. Engine programming sequences

### 5.1 CE / DMA copy (G1 — first real engine work)

Supported by method header literals + `clc6b5.h` / modeset internal push examples in OGKM.

**Allocate:** CE object under channel, class ladder newest-first:
`C8B5 → C7B5 → C6B5 → C5B5 → C3B5 → C0B5 → A0B5`

**Emit on subch 4 (or try 4 first, fall back if needed):**

```
SET_OBJECT          0x0000   = CE_class (e.g. 0xC6B5 or 0xC8B5)
[optional] SET_SRC_PHYS_MODE  0x0400
[optional] SET_DST_PHYS_MODE  0x0404
OFFSET_IN_UPPER     0x0100   GPU VA hi
OFFSET_IN_LOWER     0x0104   GPU VA lo
OFFSET_OUT_UPPER    0x0108
OFFSET_OUT_LOWER    0x010c
PITCH_IN/OUT        0x0110 / 0x0114   (pitch copies)
LINE_LENGTH_IN      0x0118
LINE_COUNT          0x011c
SET_SEMAPHORE_A/B   0x0060 / 0x0064   sema GPU VA
SET_SEMAPHORE_PAYLOAD 0x0068
LAUNCH_DMA          0x0300   pipelined/non-pipelined + flush + sema type bits
```

All addresses are **GPU VAs in channel VASpace**, never host pointers.
Prefer sema in **VRAM** first (fewer coherency surprises).

**Phys modes** (from `clc6b5.h`): FB, coherent sysmem, noncoherent, peer — open G1 can start with
pure virtual/FB and avoid sysmem modes until host sema works.

### 5.2 Compute (G2)

Dominated by class `0xC3C0` (Turing compute) through `0xC8C0` in all binaries.
cuda strings prove first-class support:

- `QMD %d size %lld GPU VA 0x%llx`
- `SASS` / `SASS generation failed` / `sass_%2d%s`
- `--annotateQmdState`

Compute is **not** CE. Launch path: bind compute class → upload **SPH/SASS** to GPU memory →
build **QMD** (queue meta descriptor) at GPU VA → methods point at QMD/SPA → sema.
Avoid UVM; map explicitly.

glcore also has QMD annotate flags (`--annotateQmdState`) — GL uses compute internally too.

### 5.3 3D (G3)

Ladder to `0xC897` / `C797` / `C697` / `C597` / `C397` / `B197` / `B097`.
eglcore has enormous `C897` u16 count (253) — GLES path heavily targets newest 3D.

Method surface is huge (state at 0x2d20, 0x2520, 0x3520, etc. on various subch in broad scans).
Bring-up: clear color or single triangle only; defer full GL state.

### 5.4 2D blit fallback

`0x902D` FERMI_TWOD_A still present in all main libs. Useful if 3D class selection is painful
for simple copies (though CE is preferred for G1).

### 5.5 CE channel multiplicity

String in glcore/eglcore: `CE CHANNEL INDEX %d, is %s` — production driver can have **multiple
CE channels** (async copy engines). Open G1 only needs one CE object on the primary graphics channel.

---

## 6. Memory model

| Concept | Rule |
|---------|------|
| Method addresses | Always **GPU VA** in channel VASpace |
| BO lifecycle | NVOS32 allocate → NVOS46 map → use `dma_offset` in methods |
| Host pointers in methods | **Never** (except via coherent sysmem phys mode, advanced) |
| Pushbuffer location | GPU-mapped; host writes via mmap'd BAR/sysmem mapping |
| GPFIFO ring | GPU-mapped; host writes entries, updates GPPut |
| Semaphores | GPU-mapped; host polls via CPU mapping of same BO |
| CUDA UVM | Auto-migrating; **skip for open bring-up** |

No `NVOS` C strings in stripped glcore — use OGKM `nvos.h` for structure layouts.

---

## 7. Synchronization layers (ordered bring-up criteria)

| # | Mechanism | Proves | If fails |
|---|-----------|--------|----------|
| 0 | USERD GPGet/GPPut move | Channel scheduled; GPFIFO consuming | schedule / doorbell / GPPut / token |
| 1 | Host sema (GPFIFO 0x10-0x1c, subch 0) | Methods execute on channel | same as 0; not CE |
| 2 | Engine sema (CE 0x60-0x68 or 3D sema) | Engine ran | class / SET_OBJECT / methods / VA |
| 3 | Error notifier non-zero | HW fault vs hang | decode notifier; fix VA/class/perms |
| 4 | GL/VK/CUDA semaphores | App-level | on top of 1-2 |

**Never skip layer 1** when debugging CE/3D. A CE sema timeout with GPGet stuck is a
**kickoff** bug, not a LAUNCH_DMA bug.

---

## 8. Kernel / RM boundary (what binaries tell us)

**Userspace does:**
- open `/dev/nvidiactl`, `/dev/nvidia%d`, optionally `/dev/nvidia-uvm`, nvidia-caps
- `ioctl` for RM alloc/control/map (NVOS protocol, structures in OGKM)
- `mmap` for USERD, usermode, push, BOs
- build methods, GPFIFO entries, semas entirely in userspace
- sfence + GPPut + doorbell without kernel on submit hot path

**Kernel (`nvidia.ko` / OGKM) does:**
- object database (handles, classes, parents)
- VASpace management
- channel/TSG scheduling (`GPFIFO_SCHEDULE`)
- work_submit_token issuance
- fault handling, notifiers
- actual MMIO to GPU for setup; **not** per-method on normal path

**Not in normal path:** ioctl-per-draw, ioctl-per-copy, userspace direct engine MMIO.

---

## 9. Open userspace mapping (what we implement)

| HW concept | Open component |
|------------|----------------|
| RM ioctl/NVOS | `libdrm/nvidia/` (`nvidia_rm.c`, `nvidia_device.c`, …) |
| Channel / GPFIFO / USERD / doorbell | `mesa/src/nvidia/rm/nv_channel.c` |
| Push helpers | `mesa/src/nvidia/common/nv_push*.h` |
| Class ladders | `nv_device_info_fill_class_ladder` + channel/CE alloc retries |
| Host sema / CE G1 | `nv_smoke_hw` G1 slices |
| Compute G2 | QMD/SPH path (in progress) |
| 3D G3 | Gallium nvgpu / Vulkan nvrm (long pole) |
| HW CLI | `nvidia_smoke_hw_cli` ordered triage |

**Validated by this RE pass (do not regress):**
1. Newest-first class ladders including **C8xx** (610 has Blackwell-era IDs)
2. Usermode ladder **C761 → C661 → C361**
3. GPFIFO ladder **C86F → C56F → C46F → C36F → …**
4. CE ladder **C8B5 → C7B5 → C6B5 → …**, LAUNCH_DMA @ **0x300**, try **subch 4**
5. Host sema on **subch 0** methods **0x10-0x1c** as first HW pass
6. sfence before GPPut/doorbell
7. work_submit_token to usermode **+0x90**
8. No UVM for smoke; explicit maps only

---

## 10. Gaps remaining (static RE insufficient)

| Gap | Why static RE fails | How to close |
|-----|---------------------|--------------|
| Exact GPFIFO entry bitfields (GP_ENTRY0/1) | No clear string; layout in scattered code | Dump ring from running blob; OGKM `dev_pbdma` |
| work_submit_token multi-channel slots | Token from RM at runtime | Trace usermode+0x90 writes |
| First ioctl / NVOS fill order | Stripped, no NVOS strings | `strace` / LD_PRELOAD on blob vs `nvidia_smoke_hw_cli` |
| Byte-perfect first CE push | Headers yes; full stream no | Capture GPFIFO segment after trivial blit on HW |
| Which subch for 3D vs compute on which GPU | Histograms noisy | Live classlist + trace |
| Hopper/Blackwell method deltas | C7/C8 classes present; methods may differ | Diff OGKM headers when available; live capture |

**Static RE is excellent for architecture, ladders, and bring-up order. It cannot alone
produce first green G1 on silicon** — that needs the trace/diff loop on a real GPU box.

---

## 11. Confidence matrix

| Claim | Confidence | Basis |
|-------|------------|-------|
| Push + GPFIFO + USERD + doorbell architecture | **Very high** | classes, headers, sfence×974, method literals, device strings |
| Subch 4 is primary CE pipe | **High** | `0x200180c0` LAUNCH_DMA×17/22; SET_OBJECT s4 |
| Host sema subch 0 is first gate | **Very high** | clc36f + header literals + smoke design |
| LAUNCH_DMA at method 0x300 for modern CE | **Very high** | header decode + clc6b5.h exact |
| Newest-first ladders incl C8xx in 610 | **High** | u16/u32 across glcore/eglcore/cuda |
| Doorbell at usermode +0x90 with RM token | **High** | clc361.h + OGKM GET_WORK_SUBMIT_TOKEN RPC |
| sfence required before publish | **Very high** | 974 sites; submit cluster pattern |
| RM client static in glcore/cuda | **Very high** | no rm .so; ioctl imports in main libs |
| Exact GPFIFO entry layout | **Low–med** | needs live dump |
| Exact first-boot ioctl order | **Low–med** | needs strace |

---

## 12. Mental model for continuing RE / implementation

Think of the GPU as a **remote method interpreter**:

1. **RM** is the object manager (kernel): creates named engines, maps memory, schedules channels.
2. **Pushbuffer** is a bytecode stream of (header, data…) for those objects.
3. **GPFIFO** is a ring of pointers into that bytecode.
4. **USERD + doorbell** is how the CPU says "new work available" without a syscall.
5. **Semaphores** are how the GPU says "done" without a syscall.
6. **Classes** are ISA versions for each engine; ladders exist because one driver supports many GPUs.

Everything else (GL state, CUDA streams, Vulkan queues) is software layering on top of this machine.

When debugging open userspace on real silicon, always ask:
- Did RM setup succeed? (ENODEV / EIO at open/alloc)
- Is the channel scheduled? (GPGet never moves)
- Did kick work? (GPPut moved, GPGet stuck → doorbell/token)
- Did methods run? (host sema)
- Did the *engine* run? (CE/3D sema + payload)

That order is what the binaries implement; it is what we should implement.

---

## 13. Pass 3: embedded class ladders (contiguous u32 tables in glcore rodata)

Not switch/case scatter only — **actual sorted newest-first tables** exist. Dumped from
`libnvidia-glcore.so` file offsets (pass 3 structural scan):

### CE (`file+0x11bb60c` and `+0x1236f8c`)

```
C7B5, C6B5, C5B5, C3B5, C1B5, C0B5, B0B5 [, A0B5 at second site]
```

Second site adds `A0B5` and small integer capability flags (`1,3,7,4,0x24,9,…`) — likely
engine-partner / instance counts, not classes.

**Open ladder should start at `C8B5`** (highest in 610 frequency tables, may live in a newer
table not at this exact offset) then fall through `C7B5…A0B5`. Pass 3 u16 counts: glcore
`C8B5`=132, `C6B5`=64 — both first-class.

### Compute (`file+0x11bb650`)

```
C7C0, C6C0, C5C0, C3C0, C1C0, C0C0, B1C0, B0C0
```

Immediately followed by a **3D ladder** (`CE97…B097`) — shared classlist blob for GR engines.

### 3D (`file+0x11bb520` region)

```
C797, C697, C597, C397, C197, … B097
```

eglcore has far more `C897` u16 hits (306) than this particular table shows — newer 3D class
lives in additional tables / code immediates.

### GPFIFO (`file+0x1238b6c`)

```
C46F, C36F, C06F, B06F, A06F, A26F, 906F, 506F
```

Nearby: display/video class runs (`C5B7…D1B7`, `C4B0…C7B0`) — **not** GPFIFO; do not allocate
those as channel classes.

**USERMODE ladder:** no tight 3+ class cluster found (classes appear singly in alloc paths).
Frequency still orders: `C761 > C661 > C561 > C461 > C361` in glcore u16 counts. Doorbell
offset `+0x90` is stable from `C361` (`clc361.h`) through at least this generation.

---

## 14. Pass 3: how the silicon actually works (consolidated model)

```
                         ┌─────────────────────────────────────────┐
                         │           Host CPU (userspace)          │
                         │  glcore / eglcore / cuda / open mesa    │
                         └─────────────┬───────────────────────────┘
                                       │ ioctl NVOS (alloc/map/ctrl)
                                       │ once per object lifetime
                         ┌─────────────▼───────────────────────────┐
                         │     nvidia.ko RM (kernel, closed)       │
                         │  owns: channel, TSG, VASpace, engines,  │
                         │  USERD page, usermode page, notifiers   │
                         └─────────────┬───────────────────────────┘
                                       │ returns handles + GPU VAs
                                       │ + mmap of USERD & usermode
         per-submit hot path (no ioctl):
         ┌─────────────────────────────▼───────────────────────────┐
         │ 1. Emit methods into pushbuffer (host writes; GPU VA)   │
         │    header = type|count|subch|method_idx  (Fermi+)       │
         │    SET_OBJECT(subch, class) binds engine on that subch  │
         │ 2. Write GPFIFO ring entry: {push_gpu_va, len, flags} │
         │ 3. mov GPPut_index → USERD+0x8c   (×N USERDs if multi) │
         │ 4. if class > C36E: sfence; mov token → usermode+0x90   │
         └─────────────────────────────┬───────────────────────────┘
                                       │ doorbell wakes runqueue
         ┌─────────────────────────────▼───────────────────────────┐
         │  GPU: PBDMA / GPFIFO engine                             │
         │  - reads ring from GPGet..GPPut                         │
         │  - fetches push segments by GPU VA                      │
         │  - dispatches methods to 8 subchannel pipelines         │
         │  - each subch has a bound object (class instance)       │
         │  - CE/3D/compute/2D execute; write semas & notifiers    │
         │  - advances USERD.GPGet as ring drains                  │
         └─────────────────────────────────────────────────────────┘
```

**There is no per-draw ioctl in the normal path.** RM is setup/teardown/control only.
If open G1 hangs with no fault, the bug is almost always in steps 3–4 (schedule missing,
wrong USERD, wrong doorbell token, missing sfence, class `<= C36E` path divergence) — not in
CE method fields.

**Subchannel roles (refined, pass 3 histograms are noisy due to ASCII/false positives in type-4;
executable-segment immediates are trustworthy):**

| Subch | Evidence | Role for bring-up |
|------:|----------|-------------------|
| 0 | `0x20010000` SET_OBJECT; host sema headers `0x20020005` | GPFIFO/host methods, sema gate |
| 1 | rare `0x20012000` in exec | compute/aux in some paths |
| 2 | heavy method traffic in rodata scans | 3D/compute state (defer) |
| 3 | SET_OBJECT NI header `0x40006000` common | 3D/aux (defer) |
| **4** | **`0x20018000` / `0x200180c0` constructed in `.text`** | **CE primary pipe** |
| 5–7 | low | aux |

---

## 15. Pass 3: implications for open userspace (actionable checklist)

Map directly onto `libdrm_nvidia` + `nv_channel` + `nv_smoke_hw`:

| # | Requirement | Blob evidence | Open status / note |
|--:|-------------|---------------|-------------------|
| 1 | USERD.GPPut at **+0x8c** (not +0x4c) | 581× stores | Verify `nv_channel` / smoke writes `0x8c` |
| 2 | Doorbell at usermode **+0x90** | 252× stores + `clc361.h` | Already targeted; must use RM token |
| 3 | Token via `RmControl 0xc36f0108` | `a53269` call site | Must call before first kick |
| 4 | Optional `0xc36f010a` notif index | `a53229` before get-token | Helps notifier-based waits |
| 5 | `sfence` before doorbell (Turing+) | `ac5585` | Required on x86 |
| 6 | Skip doorbell only if class `<= 0xC36E` | `ac5557` compare | Modern GPUs always doorbell |
| 7 | CE on **subch 4**, `LAUNCH_DMA` @ **0x300** | `d6d0c4` / `b71e52` | G1 path; try subch 0 only as fallback |
| 8 | SET_OBJECT = header `0x2001_s_000` + class dword | live emitter | Already in `nv_push_set_object` |
| 9 | Host sema on **subch 0** methods 0x10–0x1c | header literals + OGKM | G0/G1 gate before CE |
| 10 | Newest-first class ladders | rodata tables §13 | `nv_device_info_fill_class_ladder` |
| 11 | No UVM for GL-style bring-up | glcore lacks `/dev/nvidia-uvm` strings vs cuda | Smoke uses NVOS46 only |
| 12 | RM client is internal to blob | no `RmAlloc` export | Open implements ioctl/NVOS fully |
| 13 | Multi-USERD GPPut loop | `ac5540` loop | Single USERD OK for smoke |
| 14 | Schedule channel/TSG once | separate from token cmds | Without it: sema timeout, no fault |

**First green on silicon should be:** alloc channel+USERD+usermode → get token → schedule →
push host sema on subch 0 → GPPut+sfence+doorbell → observe sema memory change.
Only then CE on subch 4. Only then compute/3D.

---

## 16. Artifacts

| Path | Content |
|------|---------|
| `mesa/src/nvidia/traces/HW_MODEL_FROM_BINARIES_610.43.02.md` | Pass 1 (class tables, priorities) |
| `mesa/src/nvidia/traces/HW_MODEL_DEEP_DISASM_610.43.02.md` | **This document** (pass 2+3) |
| `/tmp/nvidia-reveng-pp-v2/re_disasm/deep3/deep3_structural_report.md` | Pass 3 full structural scan (glcore/eglcore/cuda/glsi) |
| `/tmp/nvidia-reveng-pp-v2/re_disasm/deep3/glcore_targeted_disasm.txt` | Kick / CE / sema / schedule sites |
| `/tmp/nvidia-reveng-pp-v2/re_disasm/deep3/submit_kick_ac54e0.txt` | Full submit function objdump |
| `/tmp/nvidia-reveng-pp-v2/re_disasm/deep3/rmctrl_gpfifo_a531e0.txt` | Token/notif RmControl block |
| `/tmp/nvidia-reveng-pp-v2/re_disasm/deep3/gput_0x8c_stores.txt` | All 581 GPPut store VAs |
| `/tmp/nvidia-reveng-pp-v2/re_disasm/deep3/doorbell_0x90_stores.txt` | All 252 doorbell store VAs |
| `/tmp/nvidia-reveng-pp-v2/re_disasm/deep/glcore_submit_disasm.txt` | Pass 2 push-cursor/sfence cluster |
| `/tmp/nvidia-reveng-pp-v2/re_disasm/*.strings_hw.txt` | Per-lib string extracts |

**Method:** ELF structural scan (class IDs, method headers, RM ctrl IDs, ladder proximity) +
executable-segment immediate scan (pyelftools) + targeted `objdump` of kick/CE/RmControl sites +
x86 store-pattern scan for disp32 `0x8c`/`0x90` + OGKM header correlation. No execution of
proprietary code; no GPU required.

---

## 17. Recommended next RE actions (on GPU box)

1. Run blob `glxgears`/trivial clear under `strace -e openat,ioctl,mmap` — capture ioctl order.
2. LD_PRELOAD logger on `ioctl`/`mmap` comparing blob vs `nvidia_smoke_hw_cli --slices 1`.
3. Single-step or trace open path stores to USERD+0x8c and usermode+0x90; compare token value
   and ordering to `ac5540` sequence.
4. After host sema green: dump GPFIFO ring + push segment; compare CE emit to `d6d0b0`/`b71e52`.
5. Confirm open code uses GPPut **0x8c** (audit for any remaining 0x4c references).
6. Only then refine compute QMD/SPH and 3D clear beyond header-driven sequences.
