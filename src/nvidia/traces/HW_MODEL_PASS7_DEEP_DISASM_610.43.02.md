# Pass 7 — Very deep disassembly: how NVIDIA 610.43.02 binaries program hardware

**Date:** 2026-06-24  
**Scope:** Long RE pass extending passes 1–6; 12-library imm32/ctrl/class mining (`pass7_mine.py`, ~314s); 50 targeted `objdump` windows (glcore/eglcore/cuda/xdrv/nvcuvid/glsi); kernel header cross-check (`ctrlc36f.h`, `ctrla06f.h`, `ctrla06c.h`); peripheral-lib channel ladder discovery; kick/sema/token/CE/cuda-QMD depth.  
**Artifacts:** `/tmp/nvidia-reveng-pp-v2/re_disasm/deep7/` (`disasm/` ×50, `tables/pass7_imm_summary.json`, `methods/*`, `strings/`, `notes/`, `nvos/`).  
**Prior passes:** `HW_MODEL_FROM_BINARIES_610.43.02.md` (1), `HW_MODEL_DEEP_DISASM_610.43.02.md` (2–3), `HW_MODEL_PASS4_DEEP_DISASM_610.43.02.md` (4), `HW_MODEL_PASS5_DEEP_DISASM_610.43.02.md` (5), `HW_MODEL_PASS6_DEEP_DISASM_610.43.02.md` (6).

---

## 0. Executive summary — what pass 7 adds

Pass 7 **reconfirms** the pass 5/6 channel-machine model and **expands** it with peripheral-library ladders, kick-path structural detail (`0x224` gate semantics refined), sema address ordering, cuda control-block encoding, and a corrected multi-lib role map.

| Finding | Pass 6 said | Pass 7 concludes | Impact on open |
|---------|-------------|------------------|----------------|
| **Peripheral channel ladders** | glcore/eglcore/cuda only | **xdrv, nvcuvid, glsi** all implement full **A06F_BIND→SCHEDULE→SET_NOTIF→GET_TOKEN** | Canonical ladder is not GL-only; independent confirmation from 3 more binaries |
| **`libnvidia-present`** | indirect / present path | **Zero** channel/sema/CE imm hits in `.text` | Present is WSI/composition only — not a channel programmer |
| **`0x01100002` sema execute** | 5× glcore, not near sema_hdr | Confirmed 5 hits (`a4498f`, `def971`, `e205f4`, `18f8373`, `19652b3`); **none** are host-sema emit sites | Keep as **A/B fallback only**; never primary |
| **Sema addr order in push** | addr_hi @ +4, addr_lo @ +8 | Confirmed at `b6c938`: **hi first, then lo**, execute `0x1001` @ +0x10, payload @ +0xc | Open must match dword order (mode 2/3 ladders) |
| **Kick `0x224(%rbp)` flag** | unknown; skips doorbell if set | Pass 7: also tested **early** at `ac551d` (je far `ac591a` if zero); if **non-zero**, takes main path with multi-USERD; if **zero**, jumps to alternate tail | Flag is **enable / active-channel** style, not disable; re-read as positive gate for primary kick |
| **Kick doorbell token source** | token table `0x1418(%rbp)` | Refined: token loaded from computed table entry `+0x8`, written to `usermode_map[i]+0x90`; multi-doorbell loop up to 9; optional alt stride via `0x12330` bit2 | Multi-doorbell optional for single-GPU; multi-USERD GPPut is the P0 gap |
| **cuda `1ae27f` site** | suspected non-USERD | **Confirmed**: writes **packed control-block fields** at +0x88..+0x9c (`len&0x1fffff`, VA>>4 into high bits, flags<<18/19) — **GPFIFO entry construction**, not GPPut doorbell | cuda kick ≠ glcore kick; do not copy cuda offsets blindly |
| **cuda/opencl sema_hdr** | no `0x20040004` const | Reconfirmed: **0** sema_hdr; **11** `0x1001` (dispatch noise / other sema); completion via **different path** (QMD/engine sema/UVM) | Open G1/G2/G3 must follow **glcore**, not cuda |
| **A06C IDs** | schedule=0101, bind=0102, timeslice=0103 | Kernel-confirmed; glcore has **GET_TIMESLICE `0xa06c0104`** at `5c995b` (1 hit); **no** A06C schedule/bind in glcore/eglcore/xdrv/nvcuvid/glsi | Graphics = A06F only |
| **CE class ladder** | C8B5/C6B5 family | Reconfirmed at `a5401b`: accepts **C1B5, C8B5, C9B5/CAB5 range, C6B5** via cmp/sub/test chains | Open CE ladder should include C8B5→C6B5 (and neighbors) |
| **GPFIFO class ladder** | C36F/A06F/… | Reconfirmed at `a52921`: **C36F, A26F, B06F family, 906F, A06F** accepted | Open already has newest-first ladders |
| **Method-header auto-scan** | useful for known forms | Pass 7 auto-histogram heavily polluted by x86 opcode bytes (`0x24xxxxxx` as `and` imm, etc.) | **Trust targeted imm + objdump only** for method headers; ignore auto top-100 |
| **Ctrl-prefix auto-scan** | useful for A06F/C36F | Prefix set too broad (`0x100`, `0x116` = x86 noise) | Use **exact imm** tables below, not prefix noise |

### Reinforced hardware model (pass 7)

```
COLD PATH (ioctl / RmControl / RmAlloc — rare)
  client → device → subdevice → VASpace → [optional TSG A06C — compute only]
  → GPFIFO channel (class ladder C36F…A06F)
  → USERD map (array up to 9 for MIG/multi-subdevice)
  → engine objects (3D / CE / COMPUTE / video)
  → usermode object (C361+)
  → A06F_BIND (0xa06f0104, paramsize 4)
  → A06F_SCHEDULE (0xa06f0103, enable=1, paramsize 3)
  → [optional A06C_SET_TIMESLICE if TSG]
  → SET_NOTIF_INDEX (0xc36f010a)  [glcore/eglcore/xdrv/nvcuvid/glsi; cuda skips]
  → GET_WORK_SUBMIT_TOKEN (0xc36f0108)
  → [SKIP fault buffer 0xc36f0109 unless SR-IOV/vGPU]

HOT PATH (userspace only — no ioctl)  [glcore canonical @ ac5526]
  1. Write method stream into GPU-mapped pushbuffer (subch 0..7)
  2. Write GPFIFO ring entry(ies): (VA>>2) + (len & 0x1fffff) + flags
  3. if channel-active flag @ context+0x224 non-zero:
       for i in 0..8:  USERD[i].GPPut @ +0x8c  = new_index
  4. if gpfifo_class > 0xC36E && !secondary_gate:
       sfence
       for i in 0..8:  usermode_map[i]+0x90 = work_submit_token
  5. HW: PBDMA consumes GPFIFO → methods run on engines
  6. Completion: host sema INC4 block (0x20040004 + 4 dwords, exec 0x1001)
     writes sema memory; GPGet advances

CUDA PARALLEL UNIVERSE
  - QMD launch queues (cnpLaunchQueue*, CNPqmdLaunch_st)
  - UVM devices (/dev/nvidia-uvm)
  - A06C TSG schedule/timeslice dominant
  - GPFIFO entry encoding at control-block offsets (1ae27f) — not USERD GPPut loop
  - No sema_hdr 0x20040004 const; no C36E doorbell gate imm
```

---

## 1. Which libraries program hardware (pass 7, 12-lib imm scan)

Imm counts = exact 32-bit little-endian matches in **executable** PT_LOAD segments only.

| Tier | Library | MB (file) | sema_hdr | sema_1001 | A06F bind | A06F sched | C36F token | C36F notif | A06C ts | CE launch | Role |
|------|---------|----------:|---------:|----------:|----------:|-----------:|-----------:|-----------:|--------:|----------:|------|
| **T0** | **libnvidia-glcore** | 40 | **9** | 74 | **2** | **2** | **2** | **2** | 3 | 28 | **Primary GL channel** |
| **T0** | **libnvidia-eglcore** | 37 | **7** | 36 | **1** | **1** | **1** | **1** | 2 | 24 | **GLES/EGL channel** |
| **T0** | **libcuda** | 108 | **0** | 11† | **0** | **7** | **7** | **0** | **13** | 41 | **Compute/CE/UVM/QMD** |
| **T0** | **libnvidia-opencl** | 103 | **0** | 11† | **0** | **7** | **7** | **0** | **13** | 41 | OpenCL on cuda stack |
| **T1** | **nvidia_drv.so** (X) | 3.5 | 0 | 2† | **2** | **2** | **2** | **2** | 0 | 5 | **Xorg modeset + channel** |
| **T1** | **libnvcuvid** | 26 | 0 | 8† | **2** | **3** | **2** | **2** | 0 | 0 | **Video decode channel** |
| **T1** | **libnvidia-glsi** | 0.6 | 0 | 1† | **1** | **1** | **1** | **1** | 0 | 1 | **WSI/setup channel** |
| **T2** | libnvidia-fbc | 0.2 | 0 | 2† | 0 | 0 | 0 | 0 | 0 | 0 | Capture; sema noise only |
| **T2** | libnvidia-ml | 2.7 | 0 | 1† | 0 | 1 | 1 | 0 | 0 | 0 | NVML; incidental imms |
| **T2** | libGLX_nvidia | 1.2 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | Loader/trampoline |
| **T2** | libnvidia-encode | 0.3 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | Thin NVENC |
| **T3** | **libnvidia-present** | 6.5 | **0** | **0** | **0** | **0** | **0** | **0** | 0 | 0 | **No channel programming** |

† `0x1001` is a common small integer; only sites co-located with sema_hdr / known sema blocks are authoritative (glcore `b6c952`, eglcore mirrors). cuda/opencl/xdrv/nvcuvid/glsi `0x1001` counts include dispatch-table noise.

**Pass 7 architectural facts:**

1. **Five independent binaries** implement the same A06F+token ladder: glcore, eglcore, xdrv, nvcuvid, glsi. This is the **universal graphics/video channel setup**, not a glcore quirk.
2. **cuda/opencl** are a **separate universe**: TSG (A06C) heavy, GET_TOKEN without SET_NOTIF, no sema_hdr, QMD launches.
3. **present** does not program GPFIFO at all (6.5 MB of present/WSI code with zero relevant imms).
4. **No separate `libnvidia-rm.so`** — RM still statically compiled into each programmer lib.
5. **Fault buffer `0xc36f0109`**: still **0 hits** across all 12 scanned libs. SR-IOV/vGPU only.

### Device paths (strings pass 7)

| Path | glcore | eglcore | cuda | present | xdrv |
|------|:------:|:-------:|:----:|:-------:|:----:|
| `/dev/nvidia%d` | ✓ | ✓ | ✓ | — | (indirect) |
| `/dev/nvidiactl` | — | — | ✓ | — | — |
| `/dev/nvidia-modeset` | ✓ | ✓ | — | — | ✓ |
| `/dev/nvidia-caps…` | ✓ | ✓ | ✓ | — | — |
| `nvidia-drm` | ✓ | ✓ | — | — | — |
| `/dev/nvidia-uvm` | — | — | ✓ | — | — |

Open smoke/G1/G2/G3: **avoid UVM**, use `/dev/nvidia0` (+ modeset only if needed).

---

## 2. The kick path — pass 7 refined (`glcore` `0xac5500`–`ac55ca`)

### 2.1 Full annotated kick (re-disassembled wider window)

```asm
; Preconditions: r10d = new GPPut index; %rbp = channel/context struct
; 0x370(%rbp) = USERD host-mapping array (pointers, up to 9)
; 0x1030(%rbp) = gpfifo class (C36E / C36F / …)
; 0x224(%rbp)  = channel-active / kick-enable flag (see §2.2)

ac5506:  mov  r9d, [rbp+0x224]
ac550d:  mov  [rsp+0x34], eax
ac5511:  mov  rax, [rbp+0x370]       ; first USERD ptr (cached)
ac551d:  test r9d, r9d
ac5520:  je   ac591a                 ; *** if flag==0: alternate path (no multi-USERD kick) ***

ac5526:  mov  r15d, 0x9              ; max 9 USERD slots
ac552c:  mov  edi, 0x8
; multi-USERD GPPut loop:
ac5540:  mov  rdx, [rbp+rax*8+0x370] ; USERD[i] host mapping
ac5548:  add  rax, 1
ac554c:  mov  [rdx+0x8c], esi        ; *** GPPut = new index (esi = r10d copy) ***
ac5552:  cmp  r15d, eax
ac5555:  ja   ac5540                 ; loop while i < 9

ac5557:  cmp  dword [rbp+0x1030], 0xc36e   ; gpfifo class
ac5561:  jbe  ac55ca                 ; skip doorbell if class <= C36E (pre-Volta)

ac5563:  mov  r8d, [rbp+0x224]
ac556a:  test r8d, r8d
ac556d:  jne  ac55ca                 ; *** if flag STILL non-zero after GPPut: skip doorbell? ***
; NOTE: pass 5/6 read this as "skip if set". Combined with ac5520 (skip entire
; primary path if zero), the net effect is subtle: primary path entered only if
; flag non-zero at start; doorbell skipped if flag non-zero at this second check.
; Possible explanations: (a) flag is mutated between checks, (b) objdump/cond
; inversion in secondary gate, (c) second check is different condition in live trace.
; Open implication: implement GPPut always; implement doorbell when class>C36E
; and token+usermode valid. Live silicon validates; do not overfit the second gate.

ac556f:  mov  r8, [rsp+0x20]         ; global/device context
ac5574:  movzx eax, byte [r8+0x12330]
ac557c:  and  esi, 0x4               ; bit2 selects alt doorbell stride
ac5585:  sfence                      ; *** order all GPPut stores before doorbell ***

; multi-doorbell loop (i=0..8):
ac558f:  test sil, sil
ac5592:  mov  eax, [rbp+0x3d8]       ; base slot index
ac5598:  cmovne edx, ecx             ; alt index if bit2 set
ac559d:  add  eax, 0x6
ac55a2:  cltq
ac55a4:  shl  rax, 4                 ; * 16
ac55a8:  add  rax, [rbp+rdx*8+0x1418] ; token table entry
ac55b0:  mov  edx, [rax+0x8]         ; *** work_submit_token from table+8 ***
ac55b3:  mov  rax, [r8+rcx*8+0x27550] ; usermode_map[i]
ac55bb:  add  rcx, 1
ac55bf:  mov  [rax+0x90], edx        ; *** doorbell = token ***
ac55c5:  cmp  r15d, ecx
ac55c8:  ja   ac558f
ac55ca:  ...                         ; post-kick bookkeeping / optional callback
```

### 2.2 Context struct offsets (glcore kick-related, pass 7)

| Offset | Role | Evidence |
|-------:|------|----------|
| `+0x224` | kick/channel-active gate | `ac5506`, `ac5563` |
| `+0x370`..`+0x3b0` | USERD host ptr array (9× u64) | `ac5511`, `ac5540` |
| `+0x3d8` | token table base slot | `ac5592`, notif math `a532a3` |
| `+0x1030` | gpfifo class | `ac5557`, `a531ed` |
| `+0x1418` | token table (per-slot entries) | `ac55a8` |
| `+0x80` / `+0x88` | push cursor / limit | sema block `b6c981` |
| `+0x540` | sema/push helper object | `b6c90b` |
| `+0x1240` | device/global context ptr | `b6c95c`, `a533d9` |

### 2.3 Open gaps vs blob kick (prioritized)

| Priority | Gap | Open status | Action |
|:--------:|-----|-------------|--------|
| **P0** | GPPut @ USERD+0x8c | implemented | verify on silicon |
| **P0** | sfence before doorbell | implemented | keep |
| **P0** | doorbell @ usermode+0x90 with RM token | implemented | keep; need valid token from GET_TOKEN |
| **P0** | C36E gate (skip doorbell if class ≤ C36E) | implemented | keep |
| **P1** | Multi-USERD loop (up to 9) | often single USERD only | for MIG/multi-subdevice: loop all mapped USERDs with same GPPut |
| **P1** | Multi-doorbell loop | often single | mirror multi-USERD if multi usermode maps |
| **P2** | Token table at +0x1418 / notif stride 0x4c0 | simplified | current single-token path OK for smoke |
| **P2** | `0x224` gate semantics | ignore / always kick | live trace if submit fails with valid setup |
| **P3** | Post-kick callback `call *0x238(%rax)` at `ac55f0` | not needed | bookkeeping only |

---

## 3. Schedule / bind / timeslice — universal ladder (pass 7 multi-lib)

### 3.1 Channel path — **five binaries, identical order**

| Step | Ctrl ID | Kernel name | paramsize | glcore | eglcore | xdrv | nvcuvid | glsi | cuda |
|-----:|---------|-------------|----------:|-------:|--------:|-----:|--------:|-----:|-----:|
| 1 | `0xa06f0104` | `NVA06F_CTRL_CMD_BIND` | 4 | 2 | 1 | 2 | 2 | 1 | **0** |
| 2 | `0xa06f0103` | `NVA06F_CTRL_CMD_GPFIFO_SCHEDULE` | 3 (enable byte) | 2 | 1 | 2 | 3 | 1 | **7** |
| — | `0xa06f0102` | (unused as imm) | — | 0 | 0 | 0 | 0 | 0 | 0 |
| — | `0xa06f0108` | `SET_ERROR_NOTIFIER` | — | 0† | 0 | 0 | 0 | 0 | 0 |

† `a06f0108` not in pass7 HIGH_VALUE hit list as exact imm in primary scan; kernel documents it for error notifier setup (optional / alternate path).

**glcore** (`a52b69`–`a52bc4`):
```asm
a52b69:  mov  edx, 0xa06f0104        ; BIND
a52b71:  mov  r8d, 0x4               ; paramsize 4
a52b77:  call RmControl_indirection  ; f681a0
a52b8b:  movb [rsp+0x41], 0x1        ; schedule enable = true
a52bb6:  mov  r8d, 0x3               ; paramsize 3
a52bbc:  mov  edx, 0xa06f0103        ; SCHEDULE
a52bc4:  call RmControl_indirection
```

**xdrv** (`9f261`–`9f2a7`) — same order, via indirect `call *(%rax)` RM vtable:
```asm
9f261:  mov  edx, 0xa06f0104
9f274:  mov  r8d, 0x4
9f27e:  call *(%rax)
9f297:  movb [rsp+0x25], 0x1
9f2a1:  mov  r8d, 0x3
9f2a7:  mov  edx, 0xa06f0103
9f2b7:  call *(%rax)
```

**nvcuvid** (`1a719`):
```asm
1a719:  mov  edx, 0xa06f0104
1a713:  mov  r8d, 0x4
1a72e:  call ...
; schedule follows at 1a7e2 (imm scan)
```

**glsi** (`1c03a` / `1c091`) — same IDs, slightly different RM call convention (xor/encode handles into edx).

**Open:** `nv_channel_try_schedule` A06F-first (tick79/80) is correct. Do **not** require A06C for G1/G2/G3.

### 3.2 TSG path — cuda/opencl only (graphics optional timeslice)

| Ctrl ID | Kernel name | glcore | eglcore | cuda | opencl | xdrv/nvcuvid/glsi |
|---------|-------------|-------:|--------:|-----:|-------:|------------------:|
| `0xa06c0101` | `GPFIFO_SCHEDULE` (TSG) | 0 | 0 | **7** | **7** | 0 |
| `0xa06c0102` | `BIND` (TSG) | 0 | 0 | 0 | 0 | 0 |
| `0xa06c0103` | `SET_TIMESLICE` | **3** | **2** | **13** | **13** | 0 |
| `0xa06c0104` | `GET_TIMESLICE` | **1** (`5c995b`) | — | — | — | 0 |
| `0xa06c0105` | `PREEMPT` | 0 | 0 | — | — | 0 |
| `0xa06c0107` | `SET_INTERLEAVE_LEVEL` | 0 | 0 | — | — | 0 |
| `0xa06c0108` | `GET_INTERLEAVE_LEVEL` | 0 | 0 | — | — | 0 |

Pass 5 mislabeled `0xa06c0103` as GET_INTERLEAVE — pass 6/7 + kernel confirm **SET_TIMESLICE** (paramsize 8 = `NvU64` µs).

---

## 4. Work submit token — multi-lib confirmation

### 4.1 Order: SET_NOTIF_INDEX then GET_TOKEN (graphics)

| Ctrl ID | Kernel name | glcore | eglcore | xdrv | nvcuvid | glsi | cuda | opencl |
|---------|-------------|-------:|--------:|-----:|--------:|-----:|-----:|-------:|
| `0xc36f010a` | `SET_WORK_SUBMIT_TOKEN_NOTIF_INDEX` | 2 | 1 | 2 | 2 | 1 | **0** | **0** |
| `0xc36f0108` | `GET_WORK_SUBMIT_TOKEN` | 2 | 1 | 2 | 2 | 1 | **7** | **7** |
| `0xc36f0109` | `UPDATE_FAULT_METHOD_BUFFER` | **0** | **0** | **0** | **0** | **0** | **0** | **0** |

**glcore notif index math** (`a532a3`–`a532ba`):
```asm
a532a3:  mov  eax, [r15+0x3d8]       ; base
a532af:  shl  eax, 4
a532b2:  lea  eax, [r12+rax+0x10]    ; r12 strides by 0x4c0 per subdevice
a532b7:  shr  eax, 4                 ; index = (base*16 + slot*0x4c0 + 0x10) >> 4
a532ba:  mov  [rsp+0x50], eax        ; SET_NOTIF_INDEX param (u32)
a53229:  mov  edx, 0xc36f010a
a5322e:  call RmControl
; then:
a53269:  mov  edx, 0xc36f0108        ; GET_TOKEN
a53270:  call RmControl              ; result in [rsp+0x4c] area
a53275:  add  r14, 1                 ; next subdevice
a53279:  add  r12d, 0x4c0            ; notif stride
```

**cuda**: GET_TOKEN only (7×), **no SET_NOTIF** — uses default error-context notif index (`NV_CHANNELGPFIFO_NOTIFICATION_TYPE_WORK_SUBMIT_TOKEN` per kernel comment). Open GL path should still do **SET_NOTIF then GET_TOKEN** (glcore/xdrv/nvcuvid/glsi all agree).

**Token consumption at kick:** loaded from table entry `+0x8`, stored to `usermode_map[i] + 0x90` (see §2).

---

## 5. Host semaphore — canonical emit (G1 critical)

### 5.1 Primary form (glcore `b6c938`, eglcore mirrors)

```asm
b6c938:  mov  dword [rbx],     0x20040004   ; header: INC4, subch0, methods 0x10..0x1c
b6c93e:  mov  eax, [rsp+0xc]
b6c946:  mov  dword [rbx+0xc], r13d         ; +0x0c SEMAPHOREC = payload (signal value)
b6c94a:  mov  dword [rbx+0x4], eax          ; +0x04 SEMAPHOREA = addr_hi
b6c94d:  mov  rax, [rsp+0x8]
b6c952:  mov  dword [rbx+0x10], 0x1001      ; +0x10 SEMAPHORED = execute *** 0x1001 ***
b6c959:  mov  dword [rbx+0x8], eax          ; +0x08 SEMAPHOREB = addr_lo
; then update push cursor rbp+0x80, check limit rbp+0x88
```

**Push layout (5 dwords, subchannel 0):**

| Offset | Value | Method (NVC36F) | Meaning |
|-------:|-------|-----------------|---------|
| +0x00 | `0x20040004` | header | INC4, start method 0x10 (= 0x40>>2 sema base) |
| +0x04 | `addr_hi` | 0x10 SEMAPHOREA | sema GPU VA [63:32] |
| +0x08 | `addr_lo` | 0x14 SEMAPHOREB | sema GPU VA [31:0] (may be `>>2` or `&~3` per mode) |
| +0x0c | `payload` | 0x18 SEMAPHOREC | value written on release |
| +0x10 | `0x1001` | 0x1c SEMAPHORED | execute: release+WFI bits (blob primary) |

**Write order in memory:** header, hi, payload, execute, lo — but **lo is at +0x08** (written last in code; field order in block is hi@+4, lo@+8, payload@+c, exec@+10).

### 5.2 `0x01100002` — not host sema (pass 7 confirmation)

| VA (glcore) | Near sema_hdr? | Likely role |
|-------------|----------------|-------------|
| `0xa4498f` | no | 3D/engine method execute or acquire form |
| `0xdef971` | no | same family |
| `0xe205f4` | no | same family |
| `0x18f8373` | no (rodata/code tail) | unlikely sema |
| `0x19652b3` | no | unlikely sema |

Open `NV_HOST_SEMA_MODE_OPEN_*` uses `0x01100002` as A/B fallback only. Primary: **`0x1001`** with addr `>>2` (mode 2) or `addr&~3` (mode 3). Sticky mode after silicon success (tick80).

### 5.3 sema_hdr distribution

| Library | `0x20040004` count | Authoritative? |
|---------|-------------------:|----------------|
| glcore | **9** | yes — primary |
| eglcore | **7** | yes — mirrors glcore |
| cuda/opencl/xdrv/nvcuvid/glsi/present | **0** | no const; cuda builds sema differently or uses engine sema |

---

## 6. Class ladders (code, not rodata)

### 6.1 GPFIFO class (`glcore` `a52921`)

Accepted classes (cmp/je/ja chain):
- `0xC36F` — VOLTA_CHANNEL_GPFIFO_A (and successors via ja branch)
- `0xA26F` — older
- `0xB06F` family (`sub $0xb06f; test $0xffffefff`)
- `0x906F`
- `0xA06F` — KEPLER_CHANNEL_GPFIFO_A

On accept: stores `0x200` to setup flags, proceeds to alloc/bind/schedule.

### 6.2 CE class (`glcore` `a5401b`)

```asm
a5401b:  cmp  eax, 0xc1b5            ; TURING / ampere-era CE variants
a5404b:  cmp  eax, 0xc8b5            ; C8B5 — common target
a54054:  sub  eax, 0xc9b5            ; C9B5 / CAB5 range (test $0xfffffeff)
a54069:  cmp  eax, 0xc6b5            ; C6B5 — older CE
```

Open CE ladder (newest-first): **C8B5, C7B5, C6B5, C5B5, C4B5, C3B5, C0B5, B0B5, A0B5** — matches pass5/6; pass7 confirms C1B5/C9B5 neighbors in the accept chain.

Imm class counts (glcore): `cls_c8b5_ce=29`, `cls_c6b5_ce=11`, `cls_c597_3d=50`, `cls_c6c0_compute=31`.

### 6.3 3D / compute (imm presence only)

| Class family | glcore hits | eglcore | cuda |
|--------------|------------:|--------:|-----:|
| C597 3D | 50 | 41 | 0 |
| C6C0 compute | 31 | 28 | 7 |
| C7C0 / C9C0 / CBC0 / CCC0 | present in ladders | present | present |
| C361 usermode | 5 | 4 | 0 |
| C36F gpfifo | 3 | 2 | 7 |

---

## 7. CE / copy methods

### 7.1 Confirmed constants (glcore)

| Imm | Meaning | glcore count |
|-----|---------|-------------:|
| `0x200180c0` | LAUNCH_DMA header (INC1, method 0xc0) | 28 |
| `0x20018000` | CE method base / or-mask for dynamic headers | (shared) |
| `0x80000451` | alt IMM path | present (pass5/6) |
| `0x20010e00` | MME method 0x3800 INC1 | 4 |

CE sema methods (0x40c–0x41c) are **built dynamically** (or into register headers), not as standalone `0x2001xxxx` consts for those exact method numbers in the pass7 targeted set. Completion for open G1 uses **host sema on subch 0**, not CE sema — correct strategy.

### 7.2 SET_OBJECT / subchannel

CE object bound via SET_OBJECT on subchannel (typically subch 4 for copy in open smoke). glcore `d6d0c4` / `b71e52` sites handle class-specific CE setup + LAUNCH_DMA.

---

## 8. cuda parallel universe (pass 7 depth)

### 8.1 GPFIFO entry construction (`1ae250`–`1ae312`) — not USERD kick

```asm
1ae252:  mov  [rdi+0x88], eax        ; field at +0x88
1ae258:  mov  eax, [rsi+0x5c]
1ae262:  and  r8d, 0x1fffff          ; *** length mask (same as GPFIFO entry) ***
1ae269:  cmpb [rsi+0x68], 0
1ae26d:  setne dl
1ae270:  shr  rax, 4                 ; VA >> 4  (note: not >> 2)
1ae274:  shl  eax, 0x13              ; into high bits
1ae277:  shl  edx, 0x12              ; flag bit
1ae27a:  or   eax, r8d
1ae27f:  mov  [rdi+0x8c], eax        ; *** packed entry word — NOT GPPut to USERD ***
; optionally more entries at +0x90..+0x9c with same encode pattern
```

**Interpretation:** cuda builds **GPFIFO ring entry dwords** into a control structure (`rdi`), using `len & 0x1fffff` and shifted VA. This is **entry formatting**, not the glcore multi-USERD GPPut + doorbell sequence. Open must **not** treat `1ae27f` as a kick template.

### 8.2 QMD / launch

Strings confirm extensive QMD machinery:
- `CNPqmdLaunch_st`, `cnpLaunchQueue*`, `cnpLaunchDevice`, `cnpLaunchDeviceV2`
- Device-side trampolines (`syscall_trampoline_cnpLaunchDevice*`)

Compute completion is via QMD progress / engine semaphores / UVM, not glcore host sema INC4.

### 8.3 Devices

cuda opens `/dev/nvidiactl`, `/dev/nvidia-uvm`, `/dev/nvidia-uvm-tools` in addition to `/dev/nvidia%d`. Open smoke avoids these.

---

## 9. MME (method macro engine)

| Imm | glcore | eglcore | cuda |
|-----|-------:|--------:|-----:|
| `0x20010e00` (method 0x3800 INC1) | 4 | 4 | 0 |

MME program data is **runtime-computed** (not large const blocks of MME opcodes in imm scan). Open END-only / stub MME is acceptable until real 3D; indirect MME is on the critical path for full GL but not G1/G2/G3.

---

## 10. NVOS / ioctl surface (kernel cross-ref)

Open implements these in `libdrm_nvidia` (`nvidia_rm.c`). Pass 7 did not find new ioctl numbers; key kernel defines:

| NV_ESC / NVOS | Role |
|---------------|------|
| `NV_ESC_RM_ALLOC` / NVOS64 | object alloc (client/device/subdevice/channel/engine) |
| `NV_ESC_RM_CONTROL` | RmControl (A06F/C36F/A06C/2080/…) |
| `NV_ESC_RM_FREE` | free object |
| `NV_ESC_RM_MAP_MEMORY` / `UNMAP` | CPU map USERD, pushbuffer, sema BO |
| `NV_ESC_RM_VID_HEAP_CONTROL` | vidmem alloc |
| `NV_ESC_RM_DUP_OBJECT` / `SHARE` | handle dup/share |
| `NV_ESC_IOCTL_XFER_CMD` | large param xfer |
| `NV_ESC_CARD_INFO` / `CHECK_VERSION_STR` / `SYS_PARAMS` | init |
| `NV_ESC_REGISTER_FD` | fd registration |

RM is **not** dlsym-able from the blob; open must keep its own ioctl layer.

---

## 11. Open userspace action checklist (pass 7 prioritized)

### P0 — must match for G1 host sema / kickoff

1. **Channel schedule:** `A06F_BIND (0xa06f0104)` then `A06F_SCHEDULE (0xa06f0103, enable=1)` — done (tick79/80).
2. **Token setup:** `SET_NOTIF_INDEX (0xc36f010a)` then `GET_WORK_SUBMIT_TOKEN (0xc36f0108)` when class > C36E.
3. **Kick:** write GPFIFO entry; publish **GPPut @ USERD+0x8c**; if class > C36E: **sfence**; **doorbell @ usermode+0x90 = token**.
4. **Host sema:** emit `0x20040004` + hi/lo/payload/`0x1001`; try modes 2 (`addr>>2`) and 3 (`addr&~3`); sticky on success.
5. **Skip fault buffer** (`0xc36f0109`) for normal host — non-fatal if attempted.
6. **Do not** use cuda kick/sema patterns for G1/G2/G3.

### P1 — improve bring-up robustness

7. **Multi-USERD GPPut** (loop up to 9) when multiple USERD mappings exist (MIG/multi-subdevice).
8. **CE class ladder** include C8B5/C6B5/C1B5 neighbors; SET_OBJECT on correct subch; LAUNCH_DMA `0x200180c0` family.
9. **Live trace** vs pass6/pass7 goldens: compare push dwords, sema layout, GPPut/doorbell order.
10. **G2/G3** only after G1 host sema observes memory write.

### P2 — later / optional

11. A06C TSG schedule/timeslice (compute / multi-channel fairness).
12. MME indirect (real 3D).
13. QMD / UVM (cuda interop only).
14. Multi-doorbell / token table stride `0x4c0` for multi-subdevice.
15. `0x224` gate / secondary doorbell conditions — refine only with live traces.

### P3 — do not block on

16. `libnvidia-present` channel code (doesn't exist).
17. Auto method-header histograms (x86 noise).
18. Fault method buffer on normal desktop/server.

---

## 12. Cross-pass confidence matrix

| Mechanism | Pass 5 | Pass 6 | Pass 7 | Open status |
|-----------|:------:|:------:|:------:|-------------|
| Channel machine model | ✓ | ✓ | ✓ reinforced | implementing |
| GPPut +0x8c | ✓ | ✓ | ✓ refined gates | done |
| Doorbell +0x90 + token | ✓ | ✓ | ✓ token table detail | done |
| sfence before doorbell | ✓ | ✓ | ✓ | done |
| C36E doorbell gate | ✓ | ✓ | ✓ | done |
| Multi-USERD (9) | noted | noted | confirmed | **P1 gap** |
| A06F BIND+SCHEDULE order | partial | ✓ | ✓ **5 libs** | done |
| A06C = TSG/compute only | partial | ✓ | ✓ | correct skip |
| sema `0x20040004`+`0x1001` | ✓ | ✓ | ✓ addr order | done + sticky |
| sema `0x01100002` primary | gap | reject | reject (5 noise sites) | A/B only |
| Fault buffer normal path | optional | **skip** | **skip** (0/12 libs) | non-fatal |
| Token SET_NOTIF then GET | ✓ | ✓ | ✓ **5 libs**; cuda skips notif | done |
| CE LAUNCH_DMA / class ladder | ✓ | ✓ | ✓ C1B5/C8B5/C6B5 | partial |
| cuda ≠ glcore kick | suspected | suspected | **confirmed** entry encode | avoid |
| present no channel | — | indirect | **zero imms** | ignore |
| xdrv/nvcuvid/glsi ladders | — | — | **new pass7** | corroboration only |

---

## 13. Artifact index

```
/tmp/nvidia-reveng-pp-v2/re_disasm/deep7/
  pass7_mine.py
  tables/pass7_imm_summary.json          # 12-lib imm counts + hits + method/ctrl (noisy)
  methods/{glcore,eglcore,cuda,opencl,nvcuvid,present}_*.json
  disasm/                                # 50 objdump windows
    glcore_kick_wide_ac5500.s            # primary kick
    glcore_kick_core_ac5540.s
    glcore_sched_bind_a52b40.s           # A06F BIND+SCHEDULE
    glcore_token_notif_a531c0.s          # SET_NOTIF + GET_TOKEN
    glcore_sema_b6c900.s                 # sema_hdr + 0x1001
    glcore_sema_detail_b6c920.s
    glcore_ce_ladder_a54000.s            # CE class accept
    glcore_gpfifo_ladder_a52900.s        # GPFIFO class accept
    glcore_3d_ladder_593be0.s
    glcore_compute_ladder_5922d0.s
    glcore_mme_3800_e1d780.s
    glcore_usermode_e92e30.s
    glcore_tsg_a456e0.s / glcore_a06c_ts_5c9a20.s
    glcore_01100002_*.s                  # non-sema 0x01100002 sites
    eglcore_*.s
    cuda_kick_1ae250.s / cuda_a06c_ts_3435e0.s / cuda_sema_1001_24de80.s
    xdrv_a06f_bind_9f240.s / xdrv_token_*.s / xdrv_ce_launch_342c0.s
    nvcuvid_a06f_1a700.s / nvcuvid_token_*.s / nvcuvid_sema_8cfe0.s
    glsi_a06f_1c020.s / glsi_token_1bba0.s / glsi_sema_40ce0.s
  strings/pass7_hw_strings.txt
  notes/kernel_ctrl_cmds*.txt
  nvos/kernel_nvos_esc.txt

mesa/src/nvidia/traces/HW_MODEL_PASS7_DEEP_DISASM_610.43.02.md  # this file
```

---

## 14. Methodology notes / caveats

1. **Imm32 scans** match any 4-byte LE sequence in executable segments (including unaligned / interior of larger immediates / x86 opcode bytes). Counts for small values (`0x1001`, `0x8c`, class IDs) are **upper bounds**; targeted objdump is authoritative.
2. **Method-header auto-histogram** (`0x2xxxxxxx` with count_field filter) is **heavily polluted** by x86 (`0x24` = `and`, `0x48` REX.W, etc.). Pass 7 does **not** promote auto top-100 as real method headers. Known good headers: `0x20040004`, `0x200180c0`, `0x20010e00`, `0x20018000` family.
3. **Ctrl-prefix scan** with broad prefixes (`0x100`, `0x108`, …) is noise; exact IDs in imm tables are trustworthy.
4. **No live GPU** on the agent host — silicon validation of sema modes / kick remains with the user machine running `nvidia_smoke_hw_cli`.
5. **Stripped binaries** — all names are objdump best-effort (`_nv037glcore@@Base+offset`); offsets are stable within 610.43.02 build.

---

## 15. One-page "what the binaries do"

NVIDIA userspace does **not** MMIO-program engines for normal work. It:

1. **Opens** `/dev/nvidiaN` (and modeset/caps as needed; cuda also ctl/uvm).
2. **Allocates** via RM ioctl: client → device → subdevice → VASpace → GPFIFO channel → USERD → engines → usermode.
3. **Binds & schedules** the channel (`A06F_BIND` then `A06F_SCHEDULE`); optionally timeslices a TSG (`A06C_SET_TIMESLICE`) if using channel groups (compute).
4. **Fetches** a doorbell token (`SET_NOTIF_INDEX` + `GET_WORK_SUBMIT_TOKEN`) for Volta+ classes.
5. **Writes** pushbuffers (method headers + data) in CPU-mapped GPU memory.
6. **Enqueues** work by writing GPFIFO entries (VA + length + flags) and publishing **GPPut** in USERD.
7. **Rings** the doorbell (`usermode+0x90 = token`) after an `sfence`, unless class is pre-Volta (≤ C36E).
8. **Completes** work via host semaphores (`0x20040004` / `0x1001` block writing sema memory) or engine-specific semaphores / QMD progress (cuda).
9. **Never** (on normal hosts) sets up the SR-IOV fault method buffer.

Open mesa/libdrm should implement exactly that loop — matching glcore/eglcore/xdrv/nvcuvid/glsi, not cuda's QMD/UVM universe — until compute interop is explicitly in scope.

---

*End of pass 7. Next RE pass (8) candidates: live-trace correlation on real silicon, full NVOS alloc sequence disasm (RmAlloc param blocks), error-notifier / RC recovery paths, and CE sema dynamic header construction sites.*
