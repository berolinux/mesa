# Pass 8 — Very deep disassembly: how NVIDIA 610.43.02 binaries program hardware

**Date:** 2026-06-24  
**Scope:** Long RE pass extending passes 1–7; 23-library imm32/ctrl/class mining (`pass8_mine.py`, ~78s); 70+ targeted `objdump` windows (glcore/eglcore/cuda/xdrv/nvcuvid/glsi/**vdpau**/**vksc**); kernel header cross-check (`ctrla06f*.h`, `ctrlc36f.h`, `ctrla06c.h`); peripheral-lib ladder confirmation; kick-gate / sema-execute / CE-launch refinement.  
**Artifacts:** `/tmp/nvidia-reveng-pp-v2/re_disasm/deep8/` (`disasm/` ×73, `tables/pass8_imm_summary.json`, `methods/*`, `strings/`, `notes/`).  
**Prior passes:** `HW_MODEL_FROM_BINARIES_610.43.02.md` (1), `HW_MODEL_DEEP_DISASM_610.43.02.md` (2–3), `HW_MODEL_PASS4…` (4), `HW_MODEL_PASS5…` (5), `HW_MODEL_PASS6…` (6), `HW_MODEL_PASS7…` (7).

---

## 0. Executive summary — what pass 8 adds

Pass 8 **reconfirms** the pass 5–7 channel-machine model, **widens** the programmer-library set (vdpau, vksc), and **corrects** several high-impact misreads that affect open bring-up ladders.

| Finding | Pass 7 said | Pass 8 concludes | Impact on open |
|---------|-------------|------------------|----------------|
| **`0x01100002` sema execute** | 5× glcore, A/B fallback only | Sites are **debug/config table loads** (`mov $0x1100002,%edx` into `a85340` with 0x2065xxxx IDs) — **NOT host-sema emit** | **Drop `0x01100002` from sema ladders entirely** — only noise |
| **vdpau host sema** | not scanned | **1× `0x20040004` sema_hdr** at `28c33`; dword order **hi@+4, lo@+8, payload@+0xc, exec@+0x10=`0x2`** (not `0x1001`) | Add **exec=0x2** as alternate sema mode (mode 4/5) for G1 host sema ladder |
| **vksc channel ladder** | not scanned | **Full A06F_BIND→SCHED→SET_NOTIF→GET_TOKEN**; **7× sema_hdr**; **13× CE launch** | 7th independent confirmation of canonical ladder (Vulkan SC / safety subset) |
| **vdpau channel ladder** | not scanned | **Full BIND→SCHED→NOTIF→TOKEN** at `345c8`/`3461b`/`3560b`/`35642`; CE launches; `/dev/nvidia%d` | 8th confirmation; video path mirrors graphics |
| **SET_ERROR_NOTIFIER ID** | pass7/tick82 used `0xa06f0108` | Kernel-confirmed `NVA06F_CTRL_CMD_SET_ERROR_NOTIFIER = 0xa06f0108`; pass8 miner mislabeled `0xa06f010b` (that ID is unused/wrong) | Open tick82 code is correct (`a06f0108`); **optional** — 0 hits in normal cold-path count for exact imm (may be indirect/dynamic) |
| **A06F ctrl table (kernel)** | partial | Full public IDs: SCHED=`0103`, BIND=`0104`, ERR_NOTIF=`0108`, SET_IL=`0109`, GET_IL=`0110`, RESTART_RL=`0111`, STOP_CH=`0112`, GET_CTX=`0113`; internal STOP=`0301`, SCHED=`0303` | Open only needs BIND+SCHED+optional ERR_NOTIF |
| **Kick `0x224(%rbp)` gate** | ambiguous second check | Pass 8 full window: **if flag==0** → call `a72c50` then **re-enter multi-USERD loop at `ac5531`** (not skip kick); if flag!=0 enter loop directly; doorbell second check still `jne ac55ca` skip if non-zero after GPPut | Flag is **channel-mode / multi-GPU active** — zero path still kicks after helper call; open should **always GPPut; doorbell if class>C36E && token+usermode** |
| **CE launch variants** | `0x200180c0` primary | Confirmed at `b71e52`; **alternate path** `0x80000451` + `0x20050056` INC5 setup when phys/remap mode; launch-line `0x800002c` / `0x800000c` / `0x800403c` | Open G1 CE should try: standard LAUNCH_DMA inc1; if fails, IMM-alt `0x80000451` path (lower priority) |
| **cuda `1ae27f`** | GPFIFO entry encode | Reconfirmed: writes **packed fields at control-block +0x8c..+0xac** (`len&0x1ffff`, VA>>4 into high, flags<<18/19) — multi-entry encoder, not USERD GPPut loop | Never copy cuda offsets to glcore kick |
| **present / rtcore / glxserver / egl_wl2** | indirect | **Zero** A06F/C36F/sema_hdr channel imms (rtcore has class/CE noise only, no schedule ladder) | Do not RE these for channel bring-up |
| **Fault buffer `0xc36f0109`** | 0 hits / SR-IOV | Still **0 hits** across all 23 libs | Skip for normal G1/G2/G3 |
| **Method-header auto-scan** | polluted | Pass 8 restricts to interest methods; still treat as secondary to targeted imm+objdump | Trust objdump windows only |

### Reinforced hardware model (pass 8)

```
COLD PATH (ioctl / RmControl / RmAlloc — rare)
  client → device → subdevice → VASpace → [optional TSG A06C — compute only]
  → GPFIFO channel (class ladder C36F…A06F / newest-first)
  → USERD map (array up to 9 for MIG/multi-subdevice)
  → engine objects (3D / CE / COMPUTE / video)
  → usermode object (C361+)
  → A06F_BIND (0xa06f0104, paramsize 4, engine id in params)
  → A06F_SCHEDULE (0xa06f0103, enable=1, paramsize 3)
  → [optional A06F_SET_ERROR_NOTIFIER 0xa06f0108, bNotifyEachChannelInTSG]
  → [optional A06C_SET_TIMESLICE 0xa06c0103 if TSG]
  → SET_NOTIF_INDEX (0xc36f010a)  [graphics/video/vksc; cuda SKIPS]
  → GET_WORK_SUBMIT_TOKEN (0xc36f0108)
  → [SKIP fault buffer 0xc36f0109 unless SR-IOV/vGPU]

HOT PATH (userspace only — no ioctl)  [glcore canonical @ ac5526]
  1. Write method stream into GPU-mapped pushbuffer (subch 0..7)
  2. Write GPFIFO ring entry(ies): (VA>>2) + (len & 0x1fffff) + flags
  3. Multi-USERD GPPut loop (i=0..8): USERD[i].GPPut @ +0x8c = new_index
     (if ctx+0x224==0: helper a72c50 then still runs loop)
  4. if gpfifo_class > 0xC36E && secondary_gate_allows:
       sfence
       multi-doorbell loop (i=0..8): usermode_map[i]+0x90 = work_submit_token
       (token from table @ ctx+0x1418 + stride; optional alt via 0x12330 bit2)
  5. HW: PBDMA consumes GPFIFO → methods run on engines
  6. Completion: host sema INC4 block
       glcore/eglcore/vksc: 0x20040004 + hi + lo + payload + exec 0x1001
       vdpau alt:            0x20040004 + hi + lo + payload + exec 0x0002

CUDA / OPENCL PARALLEL UNIVERSE
  - QMD launch queues; UVM (/dev/nvidia-uvm)
  - A06C TSG schedule/timeslice dominant; A06F_SCHED without BIND in imm scan
  - GET_TOKEN without SET_NOTIF
  - GPFIFO entry construction in control blocks (1ae27f) — not USERD kick
  - No sema_hdr 0x20040004; completion via engine/QMD/UVM paths
```

---

## 1. Which libraries program hardware (pass 8, 23 libs)

Imm counts = exact 32-bit little-endian matches in **executable** PT_LOAD segments only.

| Tier | Library | MB | sema_hdr | bind | sched | token | notif | a06c_ts | CE launch | Role |
|------|---------|---:|---------:|-----:|------:|------:|------:|--------:|----------:|------|
| **T0** | **libnvidia-glcore** | 41.8 | **9** | **2** | **2** | **2** | **2** | 3 | 28 | **Primary GL channel** (gold standard) |
| **T0** | **libnvidia-eglcore** | 39.1 | **7** | **1** | **1** | **1** | **1** | 2 | 24 | **GLES/EGL channel** |
| **T0** | **libnvidia-vksc-core** | 11.1 | **7** | **1** | **1** | **1** | **1** | 2 | 13 | **Vulkan SC channel** (new pass8) |
| **T0** | **libvdpau_nvidia** | 0.6 | **1** | **1** | **1** | **1** | **1** | 0 | 6 | **VDPAU video channel** (new pass8; sema exec=0x2) |
| **T0** | **libcuda** | 112.6 | **0** | **0** | **7** | **7** | **0** | **13** | 41 | **Compute/CE/UVM/QMD** |
| **T0** | **libnvidia-opencl** | 108.2 | **0** | **0** | **7** | **7** | **0** | **13** | 41 | OpenCL on cuda stack |
| **T1** | **nvidia_drv.so** (X) | 3.6 | 0 | **2** | **2** | **2** | **2** | 0 | 5 | **Xorg modeset + channel** |
| **T1** | **libnvcuvid** | 27.5 | 0 | **2** | **3** | **2** | **2** | 0 | 0 | **Video decode channel** |
| **T1** | **libnvidia-glsi** | 0.6 | 0 | **1** | **1** | **1** | **1** | 0 | 1 | **WSI/setup channel** |
| **T2** | libnvidia-ml | 2.7 | 0 | 0 | 1 | 1 | 0 | 0 | 0 | NVML; incidental imms |
| **T2** | libnvidia-rtcore | 44.9 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | RT/OptiX; class noise only |
| **T2** | libnvidia-present | 6.8 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | **No channel programming** |
| **T2** | libGLX/EGL_nvidia, glxserver, egl_wl2, cfg, encode, fbc, api, ngx, allocator, optical | varies | 0 | 0 | 0 | 0 | 0 | 0 | 0 | Loaders/helpers |

**Pass 8 architectural facts:**

1. **Eight independent binaries** implement the A06F+token ladder: glcore, eglcore, **vksc**, **vdpau**, xdrv, nvcuvid, glsi (+ cuda/opencl with partial/token-only variant).
2. **vksc is essentially a trimmed glcore/eglcore** — same offsets (`0x3d8`, `0x12320`/`0x12330`, notif stride `0x4c0`), same RmControl indirection (`call *0x1b8(%rax)`).
3. **vdpau is a miniature independent channel programmer** (~0.6 MB) with full ladder + sema_hdr + CE — best small reference for open bring-up.
4. **cuda/opencl** remain a **separate universe** (TSG-heavy, no sema_hdr, no SET_NOTIF).
5. **present** still does not program GPFIFO (confirmed with pass8 matrix all zeros).
6. **Fault buffer `0xc36f0109`**: still **0 hits** across all 23 scanned libs.
7. **No separate `libnvidia-rm.so`** — RM statically compiled into each programmer lib.

### Device paths (strings pass 8)

| Path | glcore | eglcore | cuda | vdpau | vksc | present |
|------|:------:|:-------:|:----:|:-----:|:----:|:-------:|
| `/dev/nvidia%d` | ✓ | ✓ | ✓ | ✓ | (via gl stack) | — |
| `/dev/nvidiactl` | — | — | ✓ | — | — | — |
| `/dev/nvidia-modeset` | ✓ | ✓ | — | ✓ | — | — |
| `/dev/nvidia-caps…` | ✓ | ✓ | ✓ | ✓ | — | — |
| `nvidia-drm` | ✓ | ✓ | — | ✓ | — | — |
| `/dev/nvidia-uvm` | — | — | ✓ | — | — | — |

Open smoke/G1/G2/G3: **avoid UVM**, use `/dev/nvidia0` (+ modeset only if needed).

---

## 2. The kick path — pass 8 refined (`glcore` `0xac54c0`–`ac5a88`)

### 2.1 Annotated primary kick (widened window)

```asm
; Preconditions: r10d = new GPPut index; %rbp = channel/context struct
; 0x370(%rbp) = USERD host-mapping array (pointers, up to 9)
; 0x1030(%rbp) = gpfifo class (C36E / C36F / …)
; 0x224(%rbp)  = channel/multi-GPU mode flag
; 0x1418(%rbp) = work_submit_token table base
; 0x12330(obj) bit2 = alternate doorbell stride

ac5506:  mov  r9d, [rbp+0x224]
ac5511:  mov  rax, [rbp+0x370]       ; first USERD ptr (cached to stack)
ac551d:  test r9d, r9d
ac5520:  je   ac591a                 ; if flag==0: helper then re-enter loop

ac5526:  mov  r15d, 0x9              ; max 9 USERD slots
ac552c:  mov  edi, 0x8
ac5531:  mov  esi, r10d              ; GPPut value = new index
; multi-USERD GPPut loop:
ac5540:  mov  rdx, [rbp+rax*8+0x370] ; USERD[i] host mapping
ac5548:  add  rax, 1
ac554c:  mov  [rdx+0x8c], esi        ; *** GPPut = new index ***
ac5552:  cmp  r15d, eax
ac5555:  ja   ac5540                 ; loop while i < 9

ac5557:  cmp  dword [rbp+0x1030], 0xc36e
ac5561:  jbe  ac55ca                 ; skip doorbell if class <= C36E (pre-Volta)

ac5563:  mov  r8d, [rbp+0x224]
ac556a:  test r8d, r8d
ac556d:  jne  ac55ca                 ; secondary gate: skip doorbell if flag set
ac5585:  sfence                      ; *** order GPPut before doorbell ***
; multi-doorbell loop (i via rcx, max r15d=9):
ac55a8:  add  rax, [rbp+rdx*8+0x1418] ; token table entry
ac55b0:  mov  edx, [rax+0x8]         ; work_submit_token
ac55b3:  mov  rax, [r8+rcx*8+0x27550]; usermode_map[i]
ac55bf:  mov  [rax+0x90], edx        ; *** doorbell write ***
ac55c8:  ja   ac558f                 ; loop

ac55ca:  ; post-kick bookkeeping / optional indirect call / ret

; --- zero-flag path (pass 8 clarification) ---
ac591a:  mov  rdi, rbp
ac591d:  xor  esi, esi
ac5924:  call a72c50                 ; helper (likely init/sync USERD state)
ac592e:  xor  edi, edi
ac5930:  jmp  ac5531                 ; *** RE-ENTER multi-USERD loop (not skip kick!) ***
```

**Pass 8 kick conclusions for open:**

1. **Always publish GPPut** to all valid USERD slots (open implements 1-slot; multi-slot when multi-subdevice exists).
2. **Always sfence + doorbell** when `gpfifo_class > C36E` and token+usermode valid — do not try to mirror the `0x224` secondary gate without live trace (it may be mutated or MIG-specific).
3. Zero-flag path still kicks; helper `a72c50` is not a "skip kick" — it is a **setup-then-kick** branch.

### 2.2 GPFIFO entry encode (glcore `ac56e0` region)

```asm
ac56e0:  mov  r13d, [rax]            ; VA low / entry word0
ac56e3:  mov  ecx, [rax+0x8]         ; length-related
ac56ed:  and  r13d, 0xfffffffc       ; align VA
ac56f1:  and  eax, 0x80000200        ; flag bits
ac56fa:  shl  eax, 0x8               ; length into entry bits
ac5702:  or   edx, eax               ; compose entry word1
ac5704:  mov  r12d, edx
; later store r13d/r12d into GPFIFO ring at 0x328(%rbp) base
ac577c:  sfence                      ; before alternate kick path
```

Matches open `VA>>2` + `(len & 0x1fffff)` model; flags include `0x200`/`0x80000000` family.

---

## 3. Host semaphore — pass 8 definitive layout

### 3.1 glcore / eglcore / vksc primary (`b6c938`)

```asm
b6c938:  mov  dword [rbx], 0x20040004   ; INC4 method 0x10 (sema addr start)
b6c93e:  mov  eax, [rsp+0xc]            ; sema addr hi
b6c946:  mov  [rbx+0xc], r13d           ; payload (inc value)
b6c94a:  mov  [rbx+0x4], eax            ; addr hi @ +4
b6c94d:  mov  rax, [rsp+0x8]            ; sema addr lo
b6c952:  mov  dword [rbx+0x10], 0x1001  ; *** execute: RELEASE/INC = 0x1001 ***
b6c959:  mov  [rbx+0x8], eax            ; addr lo @ +8
```

**Pushblock (5 dwords, subch 0):**

| Offset | Value | Meaning |
|-------:|-------|---------|
| +0x00 | `0x20040004` | method header: inc=4, subch=0, method=0x10 |
| +0x04 | sema_addr >> 32 | address high |
| +0x08 | sema_addr & 0xffffffff | address low |
| +0x0c | payload | increment/payload |
| +0x10 | `0x1001` | sema execute (release/inc) |

### 3.2 vdpau alternate execute (`28c33`)

```asm
28c33:  mov  dword [rax], 0x20040004
28c59:  mov  [rax+0x4], edx            ; addr hi
28c6e:  mov  [rax+0x4], r12d           ; (cursor advanced) addr lo
28c84:  mov  [rax+0x4], r13d           ; payload
28c9a:  mov  dword [rax+0x4], 0x2      ; *** execute = 0x2 (NOT 0x1001) ***
```

**Pass 8 sema modes for open G1 (recommended ladder order):**

| Mode | Header | Addr order | Exec | Source |
|-----:|--------|------------|------|--------|
| 2 (primary) | `0x20040004` | hi then lo | `0x1001` | glcore/eglcore/vksc |
| 3 | `0x20040004` | lo then hi (if needed) | `0x1001` | A/B only |
| 4 (**new**) | `0x20040004` | hi then lo | `0x0002` | **vdpau** |
| 5 (**new**) | `0x20040004` | lo then hi | `0x0002` | vdpau A/B |
| ~~1~~ | — | — | ~~`0x01100002`~~ | **REMOVED** (debug noise) |

### 3.3 `0x01100002` debunked (`a4498e`)

```asm
a4498e:  mov  edx, 0x1100002
a44993:  lea  rcx, [rip+0x1d9899e]     ; static table slot
a4499a:  mov  esi, 0x20654715          ; config/debug ID family
a449a6:  call a85340                   ; table/registry writer
```

Same pattern at `def971`, `e205f4` — all config/debug, **never** written into pushbuffer sema blocks. Pass 7 "A/B fallback" was overly cautious; pass 8 says **do not emit `0x01100002`**.

---

## 4. Channel setup ladder — pass 8 multi-lib confirmation

### 4.1 Canonical order (8 binaries agree)

```
1. RmAlloc GPFIFO channel (class ladder newest-first: C36F/C56F/…/A06F)
2. Map USERD (host VA for GPGet/GPPut)
3. RmAlloc engines (3D / CE / COMPUTE as needed)
4. RmAlloc usermode (C361+) + map doorbell page
5. A06F_BIND      0xa06f0104  paramsize=4   (engine/runlist bind)
6. A06F_SCHEDULE  0xa06f0103  paramsize=3   enable=1
7. [opt] A06F_SET_ERROR_NOTIFIER 0xa06f0108  (bool only in OGKM)
8. [opt] A06C_SET_TIMESLICE 0xa06c0103      (TSG only; cuda/opencl/vksc partial)
9. C36F_SET_NOTIF_INDEX 0xc36f010a  paramsize=4  (notif index formula below)
10. C36F_GET_WORK_SUBMIT_TOKEN 0xc36f0108 paramsize=4  → token dword
11. [SKIP] C36F_UPDATE_FAULT_METHOD_BUFFER 0xc36f0109  (SR-IOV only, 0 hits)
```

### 4.2 Notif index formula (glcore `a532a3`, vksc `4d64ee`, xdrv `a0198`)

```asm
; glcore / vksc:
mov  eax, [ctx+0x3d8]       ; base counter / subdevice index
shl  eax, 4
lea  eax, [r12 + rax + 0x10]  ; r12 = i * 0x4c0 (stride per slot)
shr  eax, 4                 ; notif_index = (base*16 + i*0x4c0 + 0x10) >> 4
mov  [params], eax
RmControl(0xc36f010a, params, 4)
; then GET_TOKEN with zeroed output slot
```

xdrv simpler: stores `ebx+3` as notif index candidate (`a0188: lea eax,[rbx+0x3]`).

Open already ladders notif indices 0..N; pass 8 confirms **stride 0x4c0 per multi-channel slot** is real.

### 4.3 A06F kernel ctrl table (complete, pass 8)

| ID | Name | Used in normal bring-up? |
|----|------|--------------------------|
| `0xa06f0103` | GPFIFO_SCHEDULE | **Yes** (enable=1) |
| `0xa06f0104` | BIND | **Yes** (before schedule) |
| `0xa06f0108` | SET_ERROR_NOTIFIER | Optional (tick82) |
| `0xa06f0109` | SET_INTERLEAVE_LEVEL | Rare (glcore 2× at `a58086`) |
| `0xa06f0110` | GET_INTERLEAVE_LEVEL | Rare |
| `0xa06f0111` | RESTART_RUNLIST | Recovery only |
| `0xa06f0112` | STOP_CHANNEL | Teardown/error |
| `0xa06f0113` | GET_CONTEXT_ID | Query |
| `0xa06f0301` | INTERNAL_STOP_CHANNEL | Internal |
| `0xa06f0303` | INTERNAL_GPFIFO_SCHEDULE | Internal |

**Note:** pass8 miner initially labeled `0xa06f010b` as err_notif — that was wrong; real ID is `0xa06f0108`.

### 4.4 vdpau ladder (compact reference, `345c8`–`34640`)

```asm
345c8:  mov  edx, 0xa06f0104        ; BIND
345e1:  mov  r8d, 4                 ; paramsize
345f5:  call *[r12+0x148]           ; RmControl vtable
34614:  movb [..], 1                ; enable
3461b:  mov  edx, 0xa06f0103        ; SCHEDULE
34627:  mov  r8d, 3
34637:  call *[r12+0x148]
; later 3560b: C36F_SET_NOTIF 0xc36f010a
; later 35642: C36F_GET_TOKEN 0xc36f0108
```

Same order as glcore; RmControl via `0x148` vtable (vdpau) vs `0x1b8` indirect (glcore/vksc).

---

## 5. CE / copy engine — pass 8 depth

### 5.1 CE class ladder (glcore `a54000` region, reconfirmed)

Accepts via cmp/sub/test chains: **C1B5, C8B5, C9B5/CAB5 range, C6B5** (and neighbors). Open newest-first ladder C8B5→C6B5→… remains correct.

Imm class counts (pass8 matrix, representative):

| Class imm | glcore | eglcore | cuda | vksc | xdrv |
|-----------|-------:|--------:|-----:|-----:|-----:|
| C8B5 | 29 | 31 | 3 | 11 | 2 |
| C6B5 | (in ladder) | … | … | … | … |
| C1B5 | (in ladder) | … | … | … | … |

### 5.2 CE method emit (`b71e0a`–`b71e5c`)

**Path A — standard (subch-dependent setup + LAUNCH_DMA):**

```asm
b71e0a:  mov  dword [rsi], 0x20048090   ; INC4 method 0x240? setup block
b71e1a:  mov  [rsi+0x4], edx           ; addr hi
b71e29:  mov  [rsi+0x8], eax           ; addr lo
b71e31:  mov  [rsi+0xc], r10           ; line/pitch data
b71e52:  mov  dword [rax], 0x200180c0  ; *** LAUNCH_DMA inc1 method 0x300 ***
b71e5c:  mov  [rax-0x4], edx           ; launch line/control dword
```

Launch-line values observed: `0x800002c`, `0x800000c`, or `(mode<<5)&0x60 | 0x800000c`.

**Path B — IMM alternate (phys/remap / certain modes):**

```asm
b71e93:  mov  dword [rsi], 0x80000451  ; non-inc / special header
b71ea1:  mov  dword [rsi+0x4], 0x20050056  ; INC5 method 0x158 setup
; ... addr/pitch/control ...
; may emit second 0x20050056 block for dst
```

**Open G1 CE strategy (pass 8):**

1. SET_OBJECT subch 4 with CE class from ladder.
2. Emit source/dest VA setup (methods 0x200–0x2a0 region; exact offsets class-dependent).
3. Emit `0x200180c0` + launch line `0x8000000c` or `0x8000002c`.
4. Complete with **host sema** (pass7/8 strategy: CE copy then host sema; or host sema only as kickoff probe).
5. If CE sema fails but host sema works: `g1_ce_host_sema` path (already in open tick82).

CE engine sema methods `0x40c–0x41c` still **not** found as static INC headers — built dynamically or unused in favor of host sema.

---

## 6. Compute / QMD / 3D / MME (pass 8 status)

### 6.1 Compute class imm counts

| Class | glcore | eglcore | cuda | vksc |
|-------|-------:|--------:|-----:|-----:|
| C7C0 | 27 | 26 | 6 | 6 |
| C8C0 | (in ladders) | … | … | … |

cuda/opencl: QMD launch (`cnpLaunchQueue*`, `CNPqmdLaunch_st` strings), UVM, A06C schedule/timeslice — **G2 open path should follow glcore compute ladder + QMD only when targeting compute-only**, not for G1 CE.

### 6.2 3D class imm (glcore)

C997=4, CA97/CB97/CC97 present in ladders at `593be0`. G3 is future; MME real at `0x3800` (`0x20010e00` ×4 glcore/eglcore only).

### 6.3 MME (`e1d780`)

Only header `0x20010e00` (method 0x3800 inc1 subch0) as const; indirect data words computed at runtime. END-only stubs OK until 3D bring-up.

### 6.4 Video (nvcuvid / vdpau / xdrv)

- nvcuvid: full A06F+token ladder; NVDEC class imms sparse in scan (may be dynamic).
- vdpau: CE + sema + ladder (see §3.2, §4.4).
- xdrv: heavy NVDEC class imm noise (33× C8B0) + full ladder + CE launch ×5; display class imm sparse.

---

## 7. Context struct offsets (glcore, pass 8 consolidated)

| Offset | Role | Evidence |
|-------:|------|----------|
| `+0x8` | channel id / minor | kick prelude |
| `+0x80` / `+0x88` | push cursor / limit | sema emit + kick |
| `+0x224` | multi-GPU / channel mode flag | kick gate |
| `+0x228` | secondary struct; `+0x88` stores GPPut | kick epilogue |
| `+0x318` | bit0 alternate kick path | `ac5782` |
| `+0x328` | GPFIFO ring base | entry write |
| `+0x33c` | GPFIFO put/index | entry loop |
| `+0x348` | pending GPFIFO entry table | `ac56c4` |
| `+0x350` | pending entry count | kick/entry |
| `+0x370`..`+0x3b0` | USERD[0..8] host mappings | GPPut loop |
| `+0x3d0`/`+0x3d8`/`+0x3dc` | subdevice / token index base | notif/token |
| `+0x3f0`/`+0x3f4` | mode/counters | kick branches |
| `+0x540` | pushbuffer / sema alloc helper obj | sema emit |
| `+0x1030` | gpfifo class (C36E threshold) | doorbell gate |
| `+0x1064` | notif formula input | sched bind |
| `+0x1240` | channel/device obj (flags at +0xc, +0x272c8) | sema/kick |
| `+0x1418` | work_submit_token table | doorbell |
| `+0x20fe4` | per-engine token cache | entry path |
| `+0x22e50` bit2 | entry/flag behavior | entry loop |
| `+0x22eb0` | optional sema side struct | sema emit |
| `+0x27550` (on obj) | usermode_map[i] array | doorbell |
| `+0x12330` (on obj) bit2 | alt doorbell stride | doorbell |
| `+0xf49`/`+0xfb9` | dirty / need-kick flags | sema/kick set |

Open need not mirror all; critical for bring-up: USERD+0x8c, usermode+0x90, class>C36E gate, sema 5-dword block.

---

## 8. What each major binary *does* (behavioral summary)

### 8.1 `libnvidia-glcore.so` (~42 MB) — gold standard
Full RM+channel+push+kick+sema+CE+3D+compute+MME. Canonical kick at `ac5526`, sema at `b6c938`, CE at `b71e52`, ladders at `a52900`/`a54000`/`5922d0`/`593be0`. Programs `/dev/nvidia%d`, modeset, caps, nvidia-drm. **Follow this for all open G1/G2/G3.**

### 8.2 `libnvidia-eglcore.so` (~39 MB)
Mirror of glcore for GLES/EGL; slightly fewer specializations. Same ladder/sema/CE patterns at different VAs.

### 8.3 `libnvidia-vksc-core.so` (~11 MB) — pass 8 new
Vulkan SC safety subset with **full channel machine** (bind/sched/notif/token/sema/CE). Offsets align with glcore (`0x3d8`, `0x4c0` notif stride, `0x12320` flags). Confirms ladder is not GL-only.

### 8.4 `libvdpau_nvidia.so` (~0.6 MB) — pass 8 new
Compact channel programmer: ladder + sema_hdr + CE + device paths. **Sema execute `0x2`** is the only major semantic delta vs glcore. Excellent small reference implementation.

### 8.5 `libcuda.so` / `libnvidia-opencl.so` (~110 MB)
Compute/UVM/QMD universe. A06C TSG schedule/timeslice; GET_TOKEN without SET_NOTIF; no sema_hdr; GPFIFO entry encode in control blocks (`1ae27f`); CE launches for copies; heavy NVDEC/compute class imms. **Do not copy kick/sema from here to open graphics path.**

### 8.6 `nvidia_drv.so` (~3.6 MB)
Xorg modeset + full A06F+token ladder + CE launches. Notif/token at `a0170`/`a01a1`/`a01d9`. Display/NVDEC class imm heavy.

### 8.7 `libnvcuvid.so` (~27 MB)
Video decode channel: A06F bind/sched (2 sites), token/notif, sema_1001 noise. Little/no CE launch imm; decode via NVDEC objects.

### 8.8 `libnvidia-glsi.so` (~0.6 MB)
WSI/setup channel: minimal but complete A06F+token+sema site. Loader/setup role.

### 8.9 `libnvidia-present.so` (~6.8 MB)
Present/WSI/composition only. **Zero** channel/sema/CE/token imms. Do not RE for channel bring-up.

### 8.10 `libnvidia-rtcore.so` (~45 MB)
OptiX/RT core; class/CE imm noise; **no** schedule/bind/token ladder. Compiler/runtime helper, not channel programmer.

### 8.11 Loaders (`libGLX_nvidia`, `libEGL_nvidia`, `libglxserver_nvidia`, `libnvidia-egl-wayland2`)
Trampolines into glcore/eglcore/present. No direct HW programming.

### 8.12 Thin helpers (`cfg`, `ml`, `encode`, `fbc`, `api`, `ngx`, `allocator`, `optical`)
Management/encode/capture/API. Incidental imms only (ml has 1× sched/token noise).

---

## 9. Open bring-up checklist (pass 8 → mesa/libdrm actions)

### P0 — must match blob (agent optimizes; silicon validates)

1. **Cold path:** client→device→subdevice→VASpace→GPFIFO(class ladder)→USERD map→engines→usermode(C361+)→**A06F_BIND→A06F_SCHEDULE**→**SET_NOTIF→GET_TOKEN**.
2. **Hot path:** push methods → GPFIFO entry (VA>>2, len&0x1fffff) → **GPPut@USERD+0x8c** → **sfence** → **doorbell@usermode+0x90=token** (if class>C36E).
3. **Host sema primary:** `0x20040004` + hi + lo + payload + **`0x1001`** (modes 2/3 sticky).
4. **Host sema alternate (pass 8):** same but exec **`0x0002`** (modes 4/5; vdpau).
5. **Remove** `0x01100002` from sema ladders (debug noise).
6. **CE:** class ladder C8B5→…→C6B5; SET_OBJECT subch4; LAUNCH_DMA `0x200180c0`; complete via host sema (`g1_ce_host_sema`).
7. **Skip** fault buffer `0xc36f0109` unless SR-IOV.
8. **Skip** UVM / cuda QMD path for G1 graphics smoke.
9. **Optional:** `SET_ERROR_NOTIFIER 0xa06f0108` (already in tick82; safe no-op if unsupported).

### P1 — multi-slot / MIG

10. Multi-USERD GPPut loop (up to 9) when multi-subdevice/MIG.
11. Multi-doorbell loop with token table stride; notif index stride `0x4c0`.
12. Do not overfit `0x224` secondary gate without silicon trace.

### P2 — later

13. MME indirect at 0x3800+ (real in glcore; stubs OK until 3D).
14. CE IMM-alt path `0x80000451` if standard launch fails on silicon.
15. A06C TSG only if compute/TSG channel required.
16. Live trace on HW box to confirm sema hs_mode winner, spath, CE class/VA.

### Agent vs silicon (unchanged contract)

| Work | Agent (no GPU) | Silicon (HW box) |
|------|----------------|------------------|
| Ladder/ctrl IDs/order | optimize from RE | validate rc=0 |
| Kick GPPut/doorbell/token | implement/harden | confirm USERD GPGet advances |
| Host sema modes 2/3/4/5 | implement all; sticky winner | confirm sema_obs increments |
| CE class/launch/VA | implement ladders | confirm copy + sema |
| Fault buffer / UVM / present | skip | n/a |
| ENODEV/open_rc=-19 | **expected** | n/a |

---

## 10. Artifact index

| Path | Contents |
|------|----------|
| `re_disasm/deep8/pass8_mine.py` | 23-lib imm/method/string miner |
| `re_disasm/deep8/tables/pass8_imm_summary.json` | Full scan results |
| `re_disasm/deep8/tables/pass8_matrix.txt` | Cross-lib key pattern matrix |
| `re_disasm/deep8/methods/*_methods.json` | Per-lib method/imm counts |
| `re_disasm/deep8/strings/*_hw.txt` | HW-relevant string samples |
| `re_disasm/deep8/disasm/*.s` | 73 targeted objdump windows |
| `re_disasm/deep8/notes/pass8_mine.log` | Miner runtime log |
| `re_disasm/deep8/notes/pass8_strings_dev.txt` | Device/RM string hits |
| `mesa/src/nvidia/traces/HW_MODEL_PASS8_DEEP_DISASM_610.43.02.md` | This document (repo copy) |

### Key disasm windows (quick links)

| Topic | File |
|-------|------|
| Kick full + doorbell | `glcore_kick_full_ac5500.s` |
| Kick zero-flag re-enter | `glcore_kick_alt_tail_ac5910.s` |
| Host sema primary | `glcore_sema_primary_b6c900.s` |
| `0x01100002` debunked | `glcore_sema_01100002_a4498f.s` |
| BIND+SCHED | `glcore_sched_bind_a52b40.s` |
| NOTIF+TOKEN | `glcore_token_notif_a531c0.s` |
| CE launch A/B | `glcore_ce_launch_b71e20.s` |
| CE ladder | `glcore_ce_ladder_a54000.s` |
| cuda GPFIFO encode | `cuda_kick_1ae250.s` |
| xdrv notif/token | `xdrv_token_notif_a0170.s` |
| vdpau sema exec=2 | `vdpau_sema_hdr_20040004_28c35.s` |
| vdpau BIND+SCHED | `vdpau_a06f0104_bind_345c9.s` |
| vksc notif/token | `vksc_c36f010a_notif_4d64fe.s` |

---

## 11. Pass 8 → pass 9 follow-ups

1. Implement sema modes 4/5 (`exec=0x2`) in `nv_channel` host sema ladder; remove `0x01100002`.
2. Live silicon: capture which sema mode wins; capture whether CE needs IMM-alt `0x80000451`.
3. Optional: objdump `a72c50` (kick zero-flag helper) and `f681a0` (RmControl thunk) for NVOS/ioctl struct sizes.
4. Optional: scan `libnvidia-gpucomp` / `libnvidia-ptxjitcompiler` only if G2 shader path blocked (compiler, not channel).
5. Cross-check tick82 `SET_ERROR_NOTIFIER` on silicon (optional; not on blob hot path in imm scan).

---

*End of pass 8. Total miner wall time ~78s; targeted disasm ~73 windows; 23 libraries classified.*
