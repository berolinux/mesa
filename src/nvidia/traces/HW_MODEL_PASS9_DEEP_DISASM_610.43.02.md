# Pass 9 — Very deep disassembly: how NVIDIA 610.43.02 binaries program hardware

**Date:** 2026-06-24  
**Scope:** Long RE pass extending passes 1–8; 25-library whole-file + exec imm mining (`pass9_imm_summary.json`); 55+ targeted `objdump` windows (glcore/eglcore/cuda/xdrv/nvcuvid/glsi/**vdpau**/**vksc**/encode); class ladder matrices; vdpau/nvcuvid/vksc cold-path param-size verification; sema dword-order variants; driver feed-in (libdrm A06F BIND, multi-USERD slots).  
**Artifacts:** `/tmp/nvidia-reveng-pp-v2/re_disasm/deep9/` (`disasm/` ×55+, `tables/pass9_*.{json,tsv}`, `strings/`, `notes/`).  
**Prior passes:** `HW_MODEL_FROM_BINARIES_610.43.02.md` (1), `HW_MODEL_DEEP_DISASM_610.43.02.md` (2–3), `HW_MODEL_PASS4…` (4), `HW_MODEL_PASS5…` (5), `HW_MODEL_PASS6…` (6), `HW_MODEL_PASS7…` (7), `HW_MODEL_PASS8…` (8).

---

## 0. Executive summary — what pass 9 adds

Pass 9 **reconfirms** the pass 5–8 channel-machine model via independent re-disassembly, **verifies exact RmControl param sizes** from the smallest programmer (`libvdpau_nvidia` ~0.6 MB), **extends class ladder inventories** (whole-file rodata counts), and **documents sema emit variants** with byte-level layouts.

| Finding | Pass 8 said | Pass 9 concludes | Impact on open |
|---------|-------------|------------------|----------------|
| **A06F BIND/SCHED param sizes** | inferred | **vdpau@345c8**: BIND `r8d=4` (NvU32 engineType only); **@3461b**: SCHED `r8d=3` (3×NvBool: bEnable/bSkipSubmit/bSkipEnable) | libdrm `NVA06F_CTRL_GPFIFO_SCHEDULE_PARAMS` gains `bSkipEnable`; implement `nvidia_rm_gpfifo_bind` / `bind_and_schedule` |
| **vdpau sema exec=0x2** | mode 4/5 | **Confirmed @28c33–28c9a**: `0x20040004` + hi@+4 + lo@+8 + payload@+0xc + **exec@+0x10=`0x2`**; alternate path `0x20050017` (INC5 method 0x17) for non-sema case | Keep sema modes 4/5; optional INC5 probe path |
| **vksc sema layout** | assumed glcore | **@5dd6d2**: `0x20040004` then `add $0x14,%rdx` (5 dwords); packs `movabs $0x100100000000` → **payload=0, exec=0x1001** at +0xc/+0x10; hi@-0x10 lo@-0xc relative to post-cursor | Confirms glcore-compatible sema mode 1 (exec=0x1001) |
| **nvcuvid token order** | SET_NOTIF then GET_TOKEN | **@1b5b7 then @1b5eb**: NOTIF index **`0x1`** (`WORK_SUBMIT_TOKEN`) then GET_TOKEN; **@6a301/6a347** second BIND+SCHED site | Open already correct; notif index=1 is canonical |
| **nvcuvid ladder sites** | pass8 partial | BIND@`1a719`/`6a301`, SCHED@`1a7e1`/`1aa35`/`6a347`, NOTIF@`1b5b7`/`6b23c`, TOKEN@`1b5eb`/`6b279` — **two full ladders** (primary + secondary channel?) | Multi-channel video path; open single-channel sufficient for G1 |
| **glcore cold-path sites** | a52b69 | Reconfirmed: BIND@`a52b69`/`a52db5`, SCHED@`a52bbc`/`a52de0`, NOTIF@`a53229`/`a532e1`, TOKEN@`a53269`/`a5333e`; sema@`a2ff90`/`b38fcf`/`b3e3b5`/… (8+ sites) | No change |
| **Class ladders (rodata)** | imm in exec only | Whole-file counts: **eglcore has C66F+C36E+CC97** (newest GPFIFO/3D); **glcore sparse GPFIFO imm** (tables indirect) but rich CE/3D/ENC; **cuda full CCC0…C3C0** (22×CCC0) + C86F; **vdpau CE C8B5..C5B5** + 3D C997/CB97 + VID C9B0..C4B0 + UM C361/C661 | Open ladders: add C66F/C36E fallback; C997 before C897; C8C0 before C7C0 (tick85/87 done) |
| **MME method imm 0x3800** | stubs | glcore **24×** `0x3800` in rodata/code; eglcore 14×; cuda **2110×** (QMD/MME noise in compute); nvcuvid 158× | MME still END stubs in open; volume confirms method is real but ISA opaque |
| **Fault buffer c36f0109** | 0 hits | Still **0** in all pass9 scans | Skip |
| **present/rtcore/encode** | no channel | present/rtcore/encode: **no A06F/C36F ladder imms** (encode has class noise only) | Unchanged |
| **Imm miner caveat** | pass8 exec-only | Exact LE imm in PT_LOAD misses many sites because x86 encodes as `mov $imm,%reg` (byte-swapped in insn stream but miner looks for aligned dword). **objdump -d \| grep** is authoritative for ctrl IDs; whole-file rodata miner good for class IDs | Always cross-check with objdump for A06F/C36F |

### Reinforced hardware model (pass 9 — canonical, unchanged structure)

```
COLD PATH (ioctl / RmControl / RmAlloc — rare)
  client → device → subdevice → VASpace → [optional TSG A06C — compute only]
  → GPFIFO channel (class ladder C86F…A06F / newest-first; eglcore tries C66F/C36E too)
  → USERD map (array up to 9 for MIG/multi-subdevice; glcore@ac5526 r15d=9)
  → engine objects (3D / CE / COMPUTE / video)
  → usermode object (C361 / C661 / C761 ladder)
  → A06F_BIND (0xa06f0104, paramsSize=4, engineType=NV2080_ENGINE_TYPE_*)
  → A06F_SCHEDULE (0xa06f0103, paramsSize=3, bEnable=1, bSkipSubmit=0, bSkipEnable=0)
  → [optional A06F_SET_ERROR_NOTIFIER 0xa06f0108]
  → [optional A06C_SET_TIMESLICE 0xa06c0103 if TSG; cuda 5× sites]
  → SET_NOTIF_INDEX (0xc36f010a, index=1 = WORK_SUBMIT_TOKEN)  [graphics/video/vksc; cuda SKIPS]
  → GET_WORK_SUBMIT_TOKEN (0xc36f0108)
  → [SKIP fault buffer 0xc36f0109 unless SR-IOV/vGPU]

HOT PATH (userspace only — no ioctl)  [glcore canonical @ ac5526]
  1. Write method stream into GPU-mapped pushbuffer (subch 0..7)
  2. Write GPFIFO ring entry(ies): (VA>>2) + (len & 0x1fffff) + flags
  3. Multi-USERD GPPut loop (i=0..8): USERD[i].GPPut @ +0x8c = new_index
     (if ctx+0x224==0: helper a72c50 then still runs loop @ ac591a→ac5531)
  4. if gpfifo_class > 0xC36E && secondary_gate_allows (!ctx+0x224):
       sfence
       multi-doorbell loop (i=0..8): usermode_map[i]+0x90 = work_submit_token
       (token from table @ ctx+0x1418 + stride; bit2 @ 0x12330 selects alt stride)
  5. HW: PBDMA consumes GPFIFO → methods run on engines
  6. Completion: host sema INC4 block (5 dwords, header 0x20040004)
       glcore/eglcore/vksc: hi, lo, payload, exec 0x1001
       vdpau alt:            hi, lo, payload, exec 0x0002

CUDA / OPENCL PARALLEL UNIVERSE
  - QMD launch queues; UVM (/dev/nvidia-uvm)
  - A06C TSG schedule/timeslice dominant (5× a06c0103 in cuda)
  - GET_TOKEN without SET_NOTIF (imm scan)
  - GPFIFO entry construction in control blocks (1ae27f) — multi-slot encoder
    writes packed fields at +0x8c..+0xac (NOT USERD GPPut loop)
  - No sema_hdr 0x20040004 in cuda; completion via engine/QMD/UVM paths
```

---

## 1. Which libraries program hardware (pass 9)

### 1.1 Programmer tier (objdump-confirmed ctrl IDs + rodata class counts)

| Tier | Library | MB | BIND sites | SCHED sites | TOKEN | NOTIF | sema_hdr (objdump) | CE launch (rodata) | Role |
|------|---------|---:|:----------:|:-----------:|:-----:|:-----:|:-------------------:|-------------------:|------|
| **T0** | **libnvidia-glcore** | 41.8 | a52b69, a52db5 | a52bbc, a52de0 | a53269, a5333e | a53229, a532e1 | a2ff90, b38fcf, b3e3b5, … (8+) | 17 | **Primary GL channel** |
| **T0** | **libnvidia-eglcore** | 39.1 | (via shared patterns) | 9e0984 | (shared) | (shared) | b27af0, b28db0, b28ea0 | 13 | **GLES/EGL** |
| **T0** | **libnvidia-vksc-core** | 11.1 | (pass8 ladder) | (pass8) | 4d653c | (pass8) | 5dd6d4 | 4 | **Vulkan SC** |
| **T0** | **libvdpau_nvidia** | 0.6 | **345c8** | **3461b** | **35641** | **3560a** | **28c33** (exec=2) | 0 imm / CE classes present | **Video — best small ref** |
| **T0** | **libcuda** | 112.6 | 0 imm (indirect) | 0 imm | 0 imm | 0 imm | 0 | 22 | **Compute/CE/UVM/QMD** |
| **T0** | **libnvidia-opencl** | 108.2 | mirrors cuda | mirrors | mirrors | mirrors | 0 | 22 | OpenCL on cuda |
| **T1** | **libnvcuvid** | 27.5 | 1a719, 6a301 | 1a7e1, 1aa35, 6a347 | 1b5eb, 6b279 | 1b5b7, 6b23c | 0 | 0 | **Video decode (2 ladders)** |
| **T1** | **nvidia_drv.so** | 3.6 | (pass8) | 9f2a8 | (pass8) | (pass8) | (pass8) | 2 | **Xorg modeset** |
| **T1** | **libnvidia-glsi** | 0.6 | (pass8) | (pass8) | (pass8) | (pass8) | 0 | 1 | **WSI/setup** |
| **T2** | present / rtcore / encode / ml / loaders | varies | 0 | 0–incidental | 0 | 0 | 0 | 0 | Not channel programmers |

**Pass 9 architectural facts (additions):**

1. **Nine independent binaries** implement some form of A06F/C36F ladder (pass8's eight + clearer nvcuvid dual-ladder).
2. **vdpau remains the best bring-up reference** — entire channel setup fits in ~4 KB of disasm with explicit param sizes.
3. **nvcuvid runs the ladder twice** (offsets ~1a7xx and ~6a3xx) — likely per-session or per-engine (NVDEC vs secondary).
4. **Class IDs live primarily in rodata tables**, not always as `mov $class,%reg` immediates — miner undercounts GPFIFO in glcore; eglcore/cuda have richer imm footprints.
5. **C66F / C36E appear in eglcore/vksc** — open GPFIFO ladder should include these between C76F and C46F.
6. **C761 usermode** still absent from whole-file counts in pass9 (0 hits); C661 present in eglcore/vdpau/cuda/nvcuvid/xdrv.

### 1.2 Class ladder matrix (whole-file, pass9 — selected)

**GPFIFO (newest first for RmAlloc try):**
```
C86F (cuda/nvcuvid) → C76F (?) → C66F (eglcore) → C56F (gl/egl/vksc/xdrv/cuda)
→ C46F → C36F → C36E (eglcore/vksc) → A06F (gl/egl/vksc)
```
Open tick87+ should try: `C86F, C76F, C66F, C56F, C46F, C36F, C36E, A06F`.

**CE (copy engine):**
```
C8B5 (all T0/T1) → C7B5 → C6B5 → C5B5 → C4B5 → C3B5 → C0B5 (fallback, 11× glcore)
```

**3D:**
```
CC97 (egl/vksc/cuda) → CB97 → CA97 (egl/cuda) → C997 (gl/egl/vksc/vdpau/xdrv)
→ C897 → C797 → C697 → C597 (dominant in gl/egl, 17–20×) → C497 → C397
```
Open tick86 uses CC97…C397; pass9 confirms C997/CB97/C897 all present.

**Compute:**
```
CCC0 (cuda 22×) → CBC0 → CAC0 → C9C0 → C8C0 (cuda 45×; glcore 0 direct imm)
→ C7C0 → C6C0 → C5C0 → C4C0 → C3C0
```
Open tick85 uses CCC0…C3C0; pass9 confirms C8C0 is cuda-primary (try early in ladder).

**Usermode:**
```
C761 (header only / pass8 note) → C661 (egl/vdpau/cuda/nvcuvid/xdrv) → C561 (?) → C461 (cuda 14×) → C361 (all T0/T1)
```

**Video (NVDEC-ish):**
```
C9B0 (vdpau/nvcuvid/cuda) → C8B0 → C7B0 → C6B0 → C5B0 → C4B0 → C3B0
```
**NVENC-ish (C*B7):** glcore/eglcore/vksc have heavy C8B7 (18–29×); encode lib has almost nothing (delegates).

---

## 2. The kick path — pass 9 re-annotated (glcore `ac54c0`–`ac55ca`)

Re-disassembled; identical to pass 8 with minor context extension.

```asm
; %rbp = channel/context; r10d = new GPPut index
ac5506:  mov  r9d, [rbp+0x224]       ; multi-GPU / channel-mode flag
ac5511:  mov  rax, [rbp+0x370]       ; USERD[0] host mapping (array base)
ac551d:  test r9d, r9d
ac5520:  je   ac591a                 ; flag==0: helper a72c50 then re-enter loop
ac5526:  mov  r15d, 0x9              ; *** max 9 USERD slots ***
ac552c:  mov  edi, 0x8
ac5531:  mov  esi, r10d              ; GPPut = new ring index
; multi-USERD GPPut loop:
ac5540:  mov  rdx, [rbp+rax*8+0x370] ; USERD[i]
ac5548:  add  rax, 1
ac554c:  mov  [rdx+0x8c], esi        ; *** GPPut write ***
ac5552:  cmp  r15d, eax
ac5555:  ja   ac5540                 ; while i < 9
ac5557:  cmp  dword [rbp+0x1030], 0xc36e
ac5561:  jbe  ac55ca                 ; skip doorbell if class <= C36E (pre-Volta)
ac5563:  mov  r8d, [rbp+0x224]
ac556a:  test r8d, r8d
ac556d:  jne  ac55ca                 ; secondary gate: skip doorbell if flag SET
ac5574:  movzbl eax, [r8+0x12330]    ; (r8 from stack obj) bit2 = alt stride
ac5585:  sfence                      ; *** order GPPut before doorbell ***
; multi-doorbell loop:
ac55a8:  add  rax, [rbp+rdx*8+0x1418]; token table base + stride
ac55b0:  mov  edx, [rax+0x8]         ; token dword
ac55b3:  mov  rax, [r8+rcx*8+0x27550]; usermode map[i]
ac55bf:  mov  [rax+0x90], edx        ; *** doorbell = work_submit_token ***
ac55c5:  cmp  r15d, ecx
ac55c8:  ja   ac558f
ac55ca:  ...                         ; done / secondary work
```

**Open implementation status (tick87 partial):**
- `NV_CHANNEL_MAX_USERD_SLOTS = 9`
- `userd_slots[]` / `usermode_slots[]` + `nv_channel_add_userd_slot` / `add_usermode_slot`
- kickoff passes multi arrays into `nvidia_gpfifo_submit_one_multi`
- libdrm: `nvidia_rm_gpfifo_bind` + `bind_and_schedule` (pass9)

---

## 3. Cold path — vdpau miniature reference (best bring-up template)

### 3.1 BIND + SCHEDULE (`345c8`–`34643`)

```asm
345c8:  mov  edx, 0xa06f0104         ; NVA06F_CTRL_CMD_BIND
345d4:  lea  rcx, [rbp-0x1d8]        ; params = engineType (NvU32)
345db:  mov  r8d, 0x4                ; *** paramsSize = 4 ***
345e4:  mov  [rbp-0x1d8], eax        ; engineType from prior setup
345fc:  call *[r12+0x148]            ; RmControl indirection
34614:  movb [rbp-0x1db], 0x1        ; bEnable = 1 (first byte of sched params)
3461b:  mov  edx, 0xa06f0103         ; NVA06F_CTRL_CMD_GPFIFO_SCHEDULE
34620:  lea  rcx, [rbp-0x1db]        ; params = 3×NvBool on stack
34627:  mov  r8d, 0x3                ; *** paramsSize = 3 ***
34637:  call *[r12+0x148]            ; RmControl
```

**Open must match:** BIND with exactly 4-byte params; SCHEDULE with 3-byte params (bEnable=1, bSkipSubmit=0, bSkipEnable=0). Zero-init is correct.

### 3.2 SET_NOTIF + GET_TOKEN (`3560a`–`35641`, pass8 sites)

```asm
3560a:  mov  edx, 0xc36f010a         ; SET_WORK_SUBMIT_TOKEN_NOTIF_INDEX
35641:  mov  edx, 0xc36f0108         ; GET_WORK_SUBMIT_TOKEN
```
(vdpau order matches glcore/nvcuvid: NOTIF then TOKEN.)

### 3.3 Host sema emit (`28c33`–`28c9a`) — exec=0x2

```asm
28c33:  movl $0x20040004, (%rax)     ; INC4 method header (count=4, method=0x004)
28c47:  lea  rdx, [rax+4]
; write sema address hi (r12 >> 32) @ +4
28c59:  mov  [rax+4], edx
; write sema address lo (r12d) @ +8
28c6e:  mov  [rax+4], r12d           ; (cursor advanced; effective +8)
; write payload (r13d) @ +0xc
28c84:  mov  [rax+4], r13d
; write execute = 0x2 @ +0x10
28c9a:  movl $0x2, [rax+4]
```

**Alternate non-sema path @28cc8:** `0x20050017` (INC5, method 0x017) — different method family, not host sema.

### 3.4 vksc sema (`5dd6d2`–`5dd6f1`) — exec=0x1001, 5-dword block

```asm
5dd6d2:  movl $0x20040004, (%rdx)    ; header
5dd6d8:  add  rdx, 0x14              ; advance cursor by 5 dwords
5dd6dc:  movabs rax, 0x100100000000  ; low 32=0 (payload), high 32=0x1001 (exec)
5dd6ea:  mov  [rdx-0xc], r15d        ; sema lo
5dd6ee:  mov  [rdx-0x10], esi        ; sema hi
5dd6f1:  mov  [rdx-0x8], rax         ; payload=0, exec=0x1001
```

---

## 4. nvcuvid — dual ladder confirmation

| Site | Cmd | Notes |
|------|-----|-------|
| `1a719` | A06F_BIND | primary channel setup |
| `1a7e1` / `1aa35` | A06F_SCHED | enable |
| `1b5b7` | C36F_NOTIF | index=1 at `0x38(%rsp)` |
| `1b5eb` | C36F_TOKEN | get work_submit_token |
| `6a301` | A06F_BIND | secondary ladder |
| `6a347` | A06F_SCHED | secondary |
| `6b23c` | C36F_NOTIF | secondary |
| `6b279` | C36F_TOKEN | secondary |

Notif index is always **1** (`NV_CHANNELGPFIFO_NOTIFICATION_TYPE_WORK_SUBMIT_TOKEN`).

---

## 5. cuda GPFIFO encoder (`1ae27f`) — not a kick path

Reconfirmed: writes multi-entry GPFIFO **control-block** fields at `+0x8c, +0x90, +0x94, … +0xac` with encoding:
- `len & 0x1ffff`
- `VA >> 4` shifted into high bits (`shl $0x13`)
- flags at bit 18/19

This is **not** the glcore USERD GPPut loop. Open G1/G2/G3 must use glcore kick semantics, never copy cuda offsets.

A06C timeslice (`0xa06c0103`) appears at `1364cf0`, `136cf70`, `1376cb0`, `1381990`, `138bca0` in cuda — TSG path only.

---

## 6. CE launch — pass 9 spot-check

glcore CE launch imm `0x200180c0` at `b71e54` (and 16 other rodata/code sites). Pass8 alternate `0x80000451` + `0x20050056` path remains lower priority for open G1.

Launch-line markers in rodata: `0x0800000c` / `0x0800002c` / `0x0800004c` (tick84 ladders).

---

## 7. Method families (MME / QMD / PCAS)

| Imm / method | glcore | eglcore | cuda | nvcuvid | Interpretation |
|--------------|-------:|--------:|-----:|--------:|----------------|
| `0x3800` (MME range) | 24 | 14 | 2110 | 158 | MME macro method base; open still emits END stubs |
| `0x2e00` (QMD-ish) | present | present | present | — | QMD send / compute dispatch region |
| `0x2c00` (PCAS-ish) | present | present | present | — | PCAS / semaphore-acquire region |
| sema_exec `0x1001` | 15 | 17 | 24 | 2 | Host sema execute (gl/vksc path) |
| sema_noise `0x01100002` | 1 | 2 | 0 | 0 | **Not sema** — debug/config (pass8 debunk) |

Open MME remains `nv_mme_emit_*` with END stubs (tick86); real ISA requires further pass on MME bytecode tables (out of scope for channel bring-up).

---

## 8. Device paths (pass 9 strings — unchanged)

| Path | glcore | eglcore | cuda | vdpau | vksc | nvcuvid |
|------|:------:|:-------:|:----:|:-----:|:----:|:-------:|
| `/dev/nvidia%d` | ✓ | ✓ | ✓ | ✓ | via gl | ✓ |
| `/dev/nvidiactl` | — | — | ✓ | — | — | — |
| `/dev/nvidia-modeset` | ✓ | ✓ | — | ✓ | — | — |
| `/dev/nvidia-uvm` | — | — | ✓ | — | — | — |
| `nvidia-drm` | ✓ | ✓ | — | ✓ | — | — |

Open G1/G2/G3: `/dev/nvidia0` only; avoid UVM.

---

## 9. Open driver action items (pass 9 → code)

| # | Action | Status |
|---|--------|--------|
| 1 | libdrm `nvidia_rm_gpfifo_bind` / `bind_and_schedule` | **Done pass9** |
| 2 | libdrm `NVA06F_CTRL_GPFIFO_SCHEDULE_PARAMS.bSkipEnable` | **Done pass9** |
| 3 | mesa multi-USERD slots (max 9) + multi submit | **Tick87 WIP** (in tree, uncommitted) |
| 4 | GPFIFO class ladder add C66F, C36E | Open follow-up |
| 5 | Compute ladder C8C0 before C7C0 | Done tick85/87 |
| 6 | 3D ladder C997/CB97/CC97 | Done tick86 |
| 7 | Host sema modes: exec 0x1001 (1) + exec 0x2 (4/5 vdpau) | Done pass8 |
| 8 | Drop 0x01100002 from sema ladders | Done pass8 |
| 9 | Skip fault buffer 0xc36f0109 | Done pass6 |
| 10 | MME real ISA | Deferred |
| 11 | Multi-USERD >1 slot when multi-subdevice exists | Needs HW / multi-GPU |
| 12 | Use `nvidia_rm_gpfifo_bind_and_schedule` from mesa | Optional cleanup |

---

## 10. Artifact index

```
/tmp/nvidia-reveng-pp-v2/re_disasm/deep9/
  pass9_mine.py
  tables/
    pass9_imm_summary.json      # per-lib counts_all / counts_exec / sites
    pass9_tier_matrix.tsv
    pass9_class_ladders.tsv
    pass9_imm_sites.txt
  disasm/
    glcore_kick_full.s          # ac54c0 multi-USERD kick
    glcore_bind_sched.s         # a52b40 BIND/SCHED
    glcore_ce_launch.s          # b71e30 CE
    glcore_mme_3800.s / glcore_qmd_2e00.s
    vdpau_bind_sched_token.s    # 345c0 miniature ladder
    vdpau_sema_exec2.s          # 28c20 sema exec=2
    nvcuvid_token_notif.s       # 1b580 NOTIF+TOKEN
    nvcuvid_sched.s             # 6a300 secondary ladder
    vksc_token.s / vksc_sema.s  # 4d6500 / 5dd6a0
    cuda_gpfifo_encode.s        # 1ae260 control-block encoder
    cuda_a06c_timeslice.s       # 1364cc0
    eglcore_sema.s / eglcore_sched.s
    + 40 more targeted windows
  strings/                      # device paths, HW keywords
  methods/ notes/ funcs/
```

---

## 11. Confidence & methodology notes

1. **Ctrl IDs:** High confidence via `objdump -d | grep imm` (x86 `mov $imm,%reg` encoding).
2. **Param sizes:** High confidence from vdpau explicit `r8d=4` / `r8d=3` + OGKM `ctrla06fgpfifo.h`.
3. **Sema layouts:** High confidence from vdpau/vksc sequential dword stores.
4. **Kick loop:** High confidence from glcore re-disasm matching pass7/8.
5. **Class ladders:** Medium confidence for exact RmAlloc order (rodata counts ≠ call order); ladders are try-newest-first with fallback — over-including classes is safe.
6. **Cuda universe:** High confidence it is separate; do not mix kick/sema models.
7. **Agent has no GPU:** All open code validates compile/link only; silicon must run smoke on HW box (`ENODEV`/`open_rc=-19` expected here).

---

*End of pass 9. Next pass candidates: MME bytecode tables; GPFIFO class try-order from actual RmAlloc branch tables (not just imm counts); multi-subdevice USERD population from MIG paths; nvcuvid secondary ladder purpose.*
