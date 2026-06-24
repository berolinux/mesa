# Pass 6 — Very deep disassembly: how NVIDIA 610.43.02 binaries program hardware

**Date:** 2026-06-24  
**Scope:** Long RE pass extending passes 1–5; targeted `objdump` on 55+ windows; imm32/ctrl-id/method-header mining on glcore/eglcore/cuda; cross-check with open-gpu-kernel-modules headers (`ctrlc36f.h`, `ctrla06c.h`, `ctrla06f.h`); 22-library role map.  
**Artifacts:** `/tmp/nvidia-reveng-pp-v2/re_disasm/deep6/` (disasm/, notes/, ladders/, strings/, tables/).  
**Prior passes:** `HW_MODEL_FROM_BINARIES_610.43.02.md` (1), `HW_MODEL_DEEP_DISASM_610.43.02.md` (2–3), `HW_MODEL_PASS4_DEEP_DISASM_610.43.02.md` (4), `HW_MODEL_PASS5_DEEP_DISASM_610.43.02.md` (5).

---

## 0. Executive summary — what pass 6 changes

Pass 6 **confirms** the pass 5 channel-machine model and **corrects/clarifies** several items that directly affect open userspace bring-up (G1 host sema, schedule path, fault buffer).

| Finding | Pass 5 said | Pass 6 concludes | Impact on open |
|---------|-------------|------------------|----------------|
| **Fault method buffer `0xc36f0109`** | Optional bring-up step | **0 hits** in glcore/eglcore/cuda; kernel says **SR-IOV/vGPU guest only** | **Skip for normal G1/G2/G3** — not on blob hot path |
| **A06C `0xa06c0103`** | GET_INTERLEAVE (mislabeled) | **`NVA06C_CTRL_CMD_SET_TIMESLICE`** (paramsize 8 = `NvU64`) | Correct TSG ladder if/when used |
| **A06F bind ID** | Corrected to `0xa06f0104` in tick79 | **Confirmed**: `0xa06f0104` then `0xa06f0103`; **`0xa06f0102` = 0 hits** | BIND+SCHEDULE order is mandatory |
| **A06C schedule/bind** | Used by blob | **0 hits** in glcore/eglcore; **cuda only** (7× schedule, 13× timeslice) | GL/EGL path = channel (A06F), not TSG |
| **Host sema execute** | `0x1001` primary, `0x01100002` open gap | **`0x1001` in all sema_hdr blocks**; `0x01100002` exists (5× glcore) but **never next to sema_hdr** | Default **`0x1001`**; keep `0x01100002` as A/B fallback only |
| **CE sema methods `0x40c+`** | Mentioned | **No const INC headers** for 0x40c–0x41c; CE uses LAUNCH_DMA + IMM alt + INC5 setup | CE completion via host sema or engine sema built dynamically |
| **MME** | Real traffic at 0x3800+ | Confirmed: **`0x20010e00` (method 0x3800)** only as const; data words computed at runtime | END-only stubs OK until 3D; indirect MME is real |
| **Kick / token / CE / class ladders** | Solid | **Re-verified** via fresh objdump; no model change | P0 checklist unchanged |

### Reinforced hardware model (unchanged architecture)

```
COLD PATH (ioctl / RmControl / RmAlloc — rare)
  client → device → subdevice → VASpace → [optional TSG A06C] → GPFIFO channel
  → USERD map → engine objects (3D/CE/COMPUTE) → usermode (C361+)
  → A06F_BIND (0xa06f0104) → A06F_SCHEDULE (0xa06f0103, enable=1)
  → SET_NOTIF_INDEX (0xc36f010a) → GET_WORK_SUBMIT_TOKEN (0xc36f0108)
  → [optional A06C_SET_TIMESLICE if TSG]   [SKIP fault buffer unless SR-IOV]

HOT PATH (userspace only — no ioctl)
  1. Write method stream into GPU-mapped pushbuffer (subch 0..7)
  2. Write GPFIFO ring entry(ies): VA>>2 + (len & 0x1fffff) + flags
  3. Publish USERD.GPPut @ +0x8c  (loop up to 9 USERD slots for MIG/multi-subdevice)
  4. if gpfifo_class > 0xC36E && !flag@0x224:  sfence;  usermode+0x90 = RM token
  5. HW: PBDMA consumes GPFIFO → methods run on engines
  6. Completion: host sema INC4 block writes memory; GPGet advances
```

---

## 1. Which libraries program hardware (pass 6, 22 libs)

Primary programmers unchanged; pass 6 adds peripheral classification.

| Tier | Library | MB | Role | `/dev/nvidia*` | Notes |
|------|---------|---:|------|----------------|-------|
| **T0** | **libnvidia-glcore** | ~40 | **Primary GL channel** | nvidia%d, modeset, caps | 974 sfence; 5× C36E; canonical kick/sema/CE/token |
| **T0** | **libnvidia-eglcore** | ~38 | **GLES/EGL channel** | same | Mirrors glcore patterns; fewer sfence specializations |
| **T0** | **libcuda** | ~113 | **Compute/CE/UVM** | + nvidiactl, nvidia-uvm | TSG-heavy; no C36E imm; no sema_hdr const |
| **T1** | libnvidia-opencl | ~104 | OpenCL on cuda stack | indirect | Same universe as cuda |
| **T1** | libnvcuvid | ~27 | Video decode | limited | Some HW touch |
| **T1** | libnvidia-present | ~6.5 | Present/WSI | indirect | Present path, not channel setup |
| **T1** | nvidia_drv.so (X) | ~3.5 | Xorg modeset | modeset | Display, not GPFIFO kick |
| **T2** | libnvidia-fbc | — | Framebuffer capture | limited | |
| **T2** | libnvidia-ml | ~2.7 | NVML management | — | No pushbuffer |
| **T2** | libnvidia-cfg | ~0.4 | Config | — | |
| **T2** | libGLX/EGL_nvidia | ~1.2 | Loaders | indirect | Trampolines into glcore/eglcore |
| **T2** | libnvidia-glsi | ~0.6 | WSI/setup | modprobe/DRM | Little HW |
| **T3** | gpucomp, rtcore, glvkspirv, nvvm, ptxjit, tileiras, ngx, vksc, tls, encode, opticalflow, allocator | large–small | Compiler/helper/RT/enc | mostly none | Offset immediates are noise, not channel programming |

**Architectural facts (pass 6 reconfirmed):**

1. **No exported `RmAlloc` / separate `libnvidia-rm.so`.** RM is statically compiled into glcore/eglcore/cuda. Open **must** implement NVOS/ioctl in `libdrm_nvidia`.
2. **glcore/eglcore are the gold standard** for graphics channel/submit/sema/CE/token/schedule.
3. **cuda is a parallel universe**: QMD launch queues, UVM devices, TSG schedule (`A06C`), no C36E doorbell gate imm, no `0x20040004` sema header const.
4. **Fault buffer is not used by normal userspace** (see §3).
5. **glcore uses libc `ioctl` PLT** (no direct `syscall` rax=16) — normal.

---

## 2. Kick path — pass 6 re-verification (`glcore` `0xac5520`)

Fresh objdump at `ac5520` confirms pass 5 exactly:

```asm
ac5526:  mov  r15d, 0x9              ; max 9 USERD slots
ac552c:  mov  edi, 0x8
; loop:
ac5540:  mov  rdx, [rbp+rax*8+0x370] ; USERD host mapping array
ac5548:  add  rax, 1
ac554c:  mov  [rdx+0x8c], esi        ; *** GPPut = new index ***
ac5552:  cmp  r15d, eax
ac5555:  ja   ac5540                 ; multi-USERD loop

ac5557:  cmp  dword [rbp+0x1030], 0xc36e   ; gpfifo class
ac5561:  jbe  ac55ca                 ; skip doorbell if class <= C36E

ac5563:  mov  r8d, [rbp+0x224]       ; secondary disable flag
ac556a:  test r8d, r8d
ac556d:  jne  ac55ca                 ; skip doorbell if set

ac5585:  sfence                      ; *** order GPPut before doorbell ***
; then multi-doorbell loop: usermode_map[i]+0x90 = work_submit_token
```

**Open gaps vs blob kick:**
- ✅ GPPut @ +0x8c, doorbell @ +0x90, sfence, C36E gate
- ⚠️ Multi-USERD loop (up to 9) — open typically writes one USERD
- ⚠️ Flag @ `0x224(%rbp)` — unknown; if set, blob skips doorbell entirely (submission may rely on another path)

---

## 3. Fault method buffer — **skip for normal bring-up** (major pass 6 finding)

### Evidence

| Source | Evidence |
|--------|----------|
| glcore imm scan | **`0xc36f0109` count = 0** |
| eglcore imm scan | **0** |
| cuda imm scan | **0** |
| `ctrlc36f.h` | `NVC36F_CTRL_CMD_GPFIFO_UPDATE_FAULT_METHOD_BUFFER` (0xc36f0109) — *"updates the HOST CE Fault method buffer data structure of **Virtual channel created for SR-IOV guest**"* |
| Params | `NvU64 bar2Addr[2]` (`MAX_RUNQUEUES=2`) — guest BAR2 addresses |

### Conclusion

This control is for **vGPU/SR-IOV virtual channels**, not normal desktop/server channel setup. The proprietary userspace on a normal host **never issues it** (at least not with this imm in the executable segment of the three primary libs).

**Action for open userspace:**
- Do **not** block G1/G2/G3 on fault buffer setup
- Optional defensive implement is fine; treat failures as non-fatal
- Prior tick80 WIP adding fault buffer is **P2/defensive**, not P0

---

## 4. Schedule / bind / timeslice — corrected ladder

### 4.1 Channel path (glcore/eglcore) — **this is what open must do**

Disasm at `a52b4a`–`a52bc9` (glcore):

```asm
; After channel/engine alloc succeeds...
a52b69:  mov  edx, 0xa06f0104        ; NVA06F_CTRL_CMD_BIND (corrected ID)
a52b6e:  mov  esi, r13d              ; channel handle
a52b71:  mov  r8d, 0x4               ; paramsize = 4
a52b77:  call RmControl_indirection  ; f681a0

a52b8b:  mov  byte [rsp+0x41], 0x1   ; schedule enable = true
a52b90:  lea  rcx, [rsp+0x41]
a52bb6:  mov  r8d, 0x3               ; paramsize = 3 (1-byte enable + pad?)
a52bbc:  mov  edx, 0xa06f0103        ; NVA06F_CTRL_CMD_GPFIFO_SCHEDULE
a52bc1:  mov  esi, r13d
a52bc4:  call RmControl_indirection
```

| Ctrl ID | Name (open kernel) | glcore | eglcore | cuda | paramsize (blob) |
|---------|-------------------|-------:|--------:|-----:|------------------|
| `0xa06f0104` | A06F_BIND | **2** | 1 | 0 | 4 |
| `0xa06f0103` | A06F_GPFIFO_SCHEDULE | **2** | 1 | 7 | 3 (enable byte) |
| `0xa06f0102` | (unused / wrong) | **0** | 0 | 0 | — |
| `0xa06f0101` | RESET_CHANNEL? | — | — | — | — |

**Order is BIND then SCHEDULE.** Open `nv_channel_try_schedule` should match this (tick79 did).

### 4.2 TSG path (cuda primary; glcore optional timeslice only)

Open kernel `ctrla06c.h`:

| Ctrl ID | Name | glcore | eglcore | cuda | Notes |
|---------|------|-------:|--------:|-----:|-------|
| `0xa06c0101` | GPFIFO_SCHEDULE | **0** | 0 | **7** | TSG schedule — compute |
| `0xa06c0102` | BIND | **0** | 0 | **0** | Not as const imm |
| `0xa06c0103` | **SET_TIMESLICE** | **3** | 2 | **13** | Was mislabeled GET_INTERLEAVE in pass5 |
| `0xa06c0104` | GET_TIMESLICE | 1 | — | — | |
| `0xa06c0105` | PREEMPT | 0 | 0 | — | |
| `0xa06c0107` | SET_INTERLEAVE_LEVEL | — | — | — | True interleave ctrl |

Disasm at `a457bf` (glcore TSG timeslice):

```asm
a457bf:  mov  edx, 0xa06c0103        ; SET_TIMESLICE
a457c4:  mov  edi, r9d               ; TSG handle
a457c7:  mov  r8d, 0x8               ; paramsize = 8 (NvU64 timeslice_us)
a457cd:  mov  [rsp+0x18], rax        ; timeslice value
a457d2:  call RmControl_indirection
```

cuda at `34360d`:

```asm
34360d:  mov  edx, 0xa06c0103        ; SET_TIMESLICE (same)
343612:  call ...
343617:  cmp  eax, 0x35              ; error code dispatch table
```

**Open implication:** For basic GL/EGL/smoke (G1/G2/G3), implement **A06F_BIND + A06F_SCHEDULE**. A06C timeslice/schedule only if allocating a TSG (optional optimization / compute later).

---

## 5. Work submit token setup — pass 6 re-verification

Disasm at `a5324a` (glcore token loop body):

```asm
; SET_NOTIF_INDEX first (nearby at a5322a / a532e2 sites)
; then:
a53263:  mov  r8d, 0x4               ; paramsize 4
a53269:  mov  edx, 0xc36f0108        ; GET_WORK_SUBMIT_TOKEN
a5326e:  mov  edi, ebx               ; channel handle
a53270:  call RmControl_indirection
a53275:  add  r14, 1                 ; next subdevice/runqueue
a53279:  add  r12d, 0x4c0            ; notif index stride
; notif index math (a532a3 area):
a532af:  shl  eax, 4
a532b2:  lea  eax, [r12+rax+0x10]
a532b7:  shr  eax, 4                 ; index = (base + slot*stride + 0x10) >> 4
a532ba:  mov  [rsp+0x50], eax        ; SET_NOTIF_INDEX param
```

| Ctrl ID | Name | glcore | eglcore | cuda |
|---------|------|-------:|--------:|-----:|
| `0xc36f010a` | SET_WORK_SUBMIT_TOKEN_NOTIF_INDEX | 2 | 1 | **0** |
| `0xc36f0108` | GET_WORK_SUBMIT_TOKEN | 2 | 1 | 7 |

**cuda gets token without SET_NOTIF** (0 hits for 010a) — different error-context / notif layout. Open GL path should keep **SET_NOTIF then GET_TOKEN**.

Kernel (`ctrlc36f.h`): token is 32-bit opaque; doorbell write finishes submit; notif index defaults to `NV_CHANNELGPFIFO_NOTIFICATION_TYPE_WORK_SUBMIT_TOKEN` and can be relocated.

---

## 6. Host semaphore — canonical emit (G1 critical)

### 6.1 Primary form (blob, all 9 sema_hdr sites)

Disasm at `b6c938` (canonical):

```asm
b6c938:  mov  dword [rbx],     0x20040004   ; INC4 s0 methods 0x10,0x14,0x18,0x1c
b6c93e:  mov  eax, [rsp+0xc]
b6c946:  mov  dword [rbx+0xc], r13d         ; SEMAPHOREC = payload (signal value)
b6c94a:  mov  dword [rbx+0x4], eax          ; SEMAPHOREA = addr_hi
b6c94d:  mov  rax, [rsp+0x8]
b6c952:  mov  dword [rbx+0x10], 0x1001      ; SEMAPHORED = execute *** 0x1001 ***
b6c959:  mov  dword [rbx+0x8], eax          ; SEMAPHOREB = addr_lo
; then updates push cursor at rbp+0x80, checks against limit rbp+0x88
```

**Push layout (5 dwords):**

| Offset | Value | Method | Meaning |
|-------:|-------|--------|---------|
| +0x00 | `0x20040004` | header | INC4, subch 0, start method 0x10 |
| +0x04 | `addr_hi` | 0x10 SEMAPHOREA | sema GPU VA high |
| +0x08 | `addr_lo` | 0x14 SEMAPHOREB | sema GPU VA low |
| +0x0c | `payload` | 0x18 SEMAPHOREC | signal value (often monotonic) |
| +0x10 | **`0x00001001`** | 0x1c SEMAPHORED | execute/release |

### 6.2 Execute value comparison

| Value | glcore | eglcore | cuda | Near sema_hdr? | Role |
|-------|-------:|--------:|-----:|:--------------:|------|
| **`0x00001001`** | 30+ | 69 | 5183* | **Yes (all sema_hdr blocks)** | **Primary blob execute** |
| `0x01100002` | 5 | 5 | 50 | **No** | Secondary/legacy; possibly report sema or different engine |
| `0x20020005` | 4 | — | — | alt INC2 at method 0x14 | Partial sema update |
| `0x20010007` | 1 | — | — | INC1 execute only | Rare |

\*cuda's 5183× `0x1001` is dominated by non-sema uses of the small imm; cuda has **0×** `0x20040004` sema_hdr.

### 6.3 Open action (highest-value G1 delta)

```
if (host_sema_mode == BLOB_DEFAULT || host_sema_mode == TRY_1001)
    sema_execute = 0x00001001;   // match blob exactly
else if (host_sema_mode == OPEN_LEGACY)
    sema_execute = 0x01100002;   // prior open attempt
// A/B harness should try 0x1001 FIRST
```

Pass 5 already identified this; pass 6 **strengthens** it: `0x01100002` is never co-located with the sema INC4 header in glcore, so it is unlikely to be the normal host sema execute path.

---

## 7. Copy Engine (CE) — method stream (G2/G3)

### 7.1 SET_OBJECT (subch 4)

Dynamic header build (pass 5 site `d6d0c4`): `or $0x20018000, %reg` then store object handle. Const imm `0x20018000` appears 21× in glcore.

### 7.2 LAUNCH_DMA method 0x300 (primary)

Disasm at `b71e52` (one of 28 sites):

```asm
b71e46:  shl  edx, 5
b71e49:  and  edx, 0x60
b71e4c:  or   edx, 0x800000c         ; launch flags base
b71e52:  mov  dword [rax], 0x200180c0 ; INC1 s4 method 0x300 = LAUNCH_DMA
b71e58:  add  rax, 8
b71e5c:  mov  dword [rax-4], edx      ; launch parameter dword
```

| Header | Method | glcore count | Meaning |
|--------|--------|-------------:|---------|
| `0x200180c0` | 0x300 | **28** | LAUNCH_DMA (modern) |
| `0x20018080` | 0x200 | 4 | LAUNCH_DMA (legacy offset) |
| `0x80000451` | 0x451? IMM | 4 | Alt CE path |
| `0x20050056` | 0x158 INC5 | 6 | Address/setup block after IMM alt |

### 7.3 Alt IMM path (`b71e93`)

```asm
b71e93:  mov  dword [rsi], 0x80000451     ; IMM s4 method 0x451?
b71ea1:  mov  dword [rsi+4], 0x20050056   ; INC5 s4 method 0x158
b71ea8:  shl  edx, 6
b71eab:  mov  qword [rsi+8], r10          ; 64-bit address data
```

Used when certain mode conditions fail (`rsp+0x28` mode checks, `rdi+0x231b0` feature flag).

### 7.4 CE sema / phys methods

Pass 6 searched for const INC headers on subch4 methods 0x400–0x41c (SET_SRC/DST_PHYS, sema region):
- `0x20010100` (method 0x400): **1** hit only
- `0x20010103`..`0x20060103` (methods 0x40c+): **0** hits as const
- CE sema/report methods are **built dynamically** (register-or'd headers), not embedded as immediates

**Open CE path for G2/G3:**
1. SET_OBJECT subch4 with CE class handle (C8B5 → C6B5 → … ladder)
2. SET_SRC/DST addresses (methods vary by class; use open class headers)
3. LAUNCH_DMA `0x200180c0` with appropriate flags
4. Host sema on subch0 for completion (don't rely on CE sema methods yet)

### 7.5 CE class ladder (code, not rodata)

Pass 5/6 sites `ab66cc`, `a5401c`: try **C8B5, C6B5, C5B5, C1B5, C0B5, A0B5** (newest-first).

---

## 8. GPFIFO entry construction

Pass 5/6: **74+** sites of `0x1fffff` mask in glcore for 21-bit length field.

Canonical entry (two dwords, little-endian):
```
word0 = (pushbuffer_gpu_va >> 2) | flags_lo
word1 = (length_dwords & 0x1fffff) | flags_hi   // sometimes (len << 10) forms exist
```

Length is in **dwords** (method stream size / 4), masked to 21 bits. Pass 5 noted both `and $0x1fffff` and `shl $10` patterns; open must emit the form matching the channel's GPFIFO class (C36F+ typically uses the direct mask form in the primary path).

---

## 9. MME (method 0x3800 region)

Disasm at `e1d79a` (one of 5 const `0x20010e00` sites):

```asm
e1d77f:  or   r11d, 0xa00008e4       ; MME control/data bits
e1d791:  lea  edx, [rdi+0xd40]       ; derived offset
e1d797:  mov  dword [r14], 0x20010e00 ; INC1 s0 method 0x3800 (MME insn slot 0)
e1d79e:  mov  dword [r14+4], edx      ; MME data word 0
e1d7a2:  mov  dword [r14+8], r11d     ; MME data word 1
e1d7ac:  mov  dword [r14+0xc], 0x20010e00 ; second MME inc1
e1d7b4:  mov  dword [r14+0x10], edi    ; offset 0xd80 derived
e1d7b8:  mov  dword [r14+0x14], r11d
e1d7bc:  mov  dword [r14+0x18], esi    ; final data
```

Nearby alt path uses `0x20010141` (method 0x504?) when a flag is set.

**Only method 0x3800** appears as a constant method-header imm (5×). Other MME methods (0x3804–0x3ffc) are either rare or fully dynamic. Open END-only MME stubs remain acceptable until real 3D; indirect MME is a real production path in the blob.

---

## 10. Class ladders (code try-chains)

Reconfirmed from pass 5; pass 6 adds no new classes but strengthens that ladders are **in executable code** (cmp/ja chains), not only rodata tables.

| Engine | Newest-first try order (glcore samples) |
|--------|------------------------------------------|
| **GPFIFO** | C36F, A26F, B06F, 906F, A06F (`a52920`) |
| **CE** | C8B5, C6B5, C5B5, C1B5, C0B5, A0B5 (`ab66cc` / `a5401c`) |
| **COMPUTE** | C7C0, C6C0, C5C0, C3C0, C1C0, C0C0, B1C0, B0C0 (`5922f6`) |
| **3D** | C897/C697, C597, C397, C197, C097, B197, B097 (`593c0d`) |
| **USERMODE** | C561, C461, C361 (`e92e58`) |
| **Mixed alloc** | C86F/C8B5/C7B5/C6B5/B06F/A06F/A0B5 (`e9333e`) |

Open should implement newest-first with graceful fallback on `NV_ERR_INVALID_CLASS` / alloc failure.

---

## 11. cuda vs glcore divergence (compute path)

| Feature | glcore (GL gold) | cuda (compute) | Open smoke target |
|---------|------------------|----------------|-------------------|
| C36E doorbell gate | 5 imm sites | **0** | Use glcore model |
| sema_hdr `0x20040004` | 9 | **0** | Use glcore model |
| sema execute `0x1001` | in sema blocks | imm noise only | Use glcore model |
| A06F_BIND | 2 | 0 | Required |
| A06F_SCHEDULE | 2 | 7 | Required |
| A06C_SCHEDULE | 0 | **7** | Not for G1 |
| A06C_TIMESLICE | 3 | **13** | Optional |
| SET_NOTIF | 2 | **0** | Required (GL path) |
| GET_TOKEN | 2 | 7 | Required |
| `/dev/nvidia-uvm` | no | **yes** | **Avoid for smoke** |
| QMD / cnpLaunchQueue | no | **yes** | Later (compute) |
| Kick at 0x8c/0x90 | USERD proven | site `1ae27f` may be different structure | **Don't copy cuda kick blindly** |

---

## 12. Open userspace priority matrix (pass 6 final)

### P0 — must match blob for G1 (host sema completes)

1. ✅ GPPut @ USERD+0x8c (not 0x4c)
2. ✅ Doorbell @ usermode+0x90 with RM `work_submit_token`
3. ✅ `sfence` between GPPut and doorbell
4. ✅ class > `0xC36E` gate for doorbell
5. ✅ `SET_NOTIF_INDEX` (`0xc36f010a`) then `GET_TOKEN` (`0xc36f0108`)
6. ✅ `A06F_BIND` (`0xa06f0104`, paramsize 4) then `A06F_SCHEDULE` (`0xa06f0103`, enable=1)
7. ⚠️ Host sema INC4 `0x20040004` + execute **`0x00001001`** (default; A/B with `0x01100002` second)
8. ✅ CE: SET_OBJECT subch4 + LAUNCH_DMA `0x200180c0` @ method 0x300
9. ✅ GPFIFO entry: VA>>2 + `(len & 0x1fffff)` in word1
10. ✅ Class ladders newest-first (GPFIFO/CE/USERMODE minimum)

### P1 — improves reliability / multi-GPU

11. Multi-USERD loop (up to 9) for MIG/multi-subdevice
12. A06C_SET_TIMESLICE if TSG allocated
13. CE alt IMM path `0x80000451` + `0x20050056` for certain copy modes
14. Secondary sema execute `0x01100002` as explicit A/B fallback
15. Investigate flag @ ctx+0x224 (doorbell suppress)

### P2 — not blocking G1/G2/G3 on normal hosts

16. ❌ **Fault method buffer `0xc36f0109` — skip** (SR-IOV/vGPU only; 0 blob hits)
17. A06C_BIND / A06C_SCHEDULE — cuda/TSG only
18. MME indirect (method 0x3800+) — real in blob; END-only stubs OK until 3D
19. QMD / compute launch — cuda/opencl only
20. 3D report semaphore / draw methods — graphics path later
21. CE engine sema methods 0x40c+ — dynamically built; host sema sufficient for smoke

---

## 13. Confidence matrix (pass 6)

| Claim | Confidence | Evidence |
|-------|:----------:|----------|
| GPPut @ USERD+0x8c | **Very high** | 581+ stores pass4; multi-USERD loop disasm |
| Doorbell @ usermode+0x90 + token | **Very high** | Kick disasm + token setup + kernel header |
| sfence before doorbell | **Very high** | `ac5585` in kick |
| C36E class gate | **Very high** | 5 imm sites, all in gate positions |
| sema execute `0x1001` primary | **Very high** | All sema_hdr blocks; canonical `b6c952` |
| sema execute `0x01100002` secondary | **Medium** | 5 hits, not near sema_hdr |
| A06F_BIND = `0xa06f0104` | **Very high** | Disasm + 2 sites; `0102` = 0 |
| A06F_SCHEDULE after BIND | **Very high** | Fall-through at `a52b69`→`a52bbc` |
| A06C_0103 = SET_TIMESLICE | **Very high** | Kernel header + paramsize 8 |
| Fault buffer not used normally | **Very high** | 0 imm hits + SR-IOV kernel docs |
| LAUNCH_DMA `0x200180c0` | **Very high** | 28 sites, disasm |
| GPFIFO 21-bit length mask | **Very high** | 74+ sites |
| MME method 0x3800 real | **High** | 5 sites, multi-dword emit |
| cuda kick ≠ USERD | **Medium** | Different structure at `1ae27f`; no C36E |
| Multi-USERD = MIG | **Medium** | r15d=9 loop; plausible multi-subdevice |
| Flag 0x224 doorbell suppress | **Low** | Observed; purpose unknown |

---

## 14. Recommended silicon bring-up sequence (aligned to blob)

```
1.  open /dev/nvidiactl + /dev/nvidiaN  (NOT nvidia-uvm for smoke)
2.  NVOS alloc: client → device → subdevice(2080) → VASpace(90F1)
3.  Alloc GPFIFO channel (C36F..A06F ladder)
4.  Map USERD; alloc/map pushbuffer + sema BO in VASpace
5.  Alloc USERMODE (C561..C361); get usermode doorbell mapping
6.  Alloc CE (C8B5..A0B5 ladder) if G2/G3
7.  A06F_BIND (0xa06f0104)
8.  A06F_SCHEDULE (0xa06f0103, enable=1)
9.  SET_NOTIF_INDEX (0xc36f010a)  [compute notif index from slot]
10. GET_WORK_SUBMIT_TOKEN (0xc36f0108) → store token
11. [SKIP] UPDATE_FAULT_METHOD_BUFFER unless SR-IOV guest
12. Emit methods into pushbuffer:
      - optional SET_OBJECT subch4 + CE setup + LAUNCH_DMA (G2/G3)
      - host sema INC4 block with execute 0x1001 (G1)
13. Write GPFIFO entry (VA>>2, len & 0x1fffff)
14. USERD+0x8c = GPPut
15. if class > C36E: sfence; usermode+0x90 = token
16. Poll sema memory for payload
17. If timeout: retry sema execute 0x01100002; dump GPGet/GPPut; check RC
```

---

## 15. Artifacts index

| Path | Contents |
|------|----------|
| `mesa/src/nvidia/traces/HW_MODEL_PASS6_DEEP_DISASM_610.43.02.md` | **This document** |
| `/tmp/nvidia-reveng-pp-v2/re_disasm/deep6/disasm/` | 55+ targeted objdump windows |
| `/tmp/nvidia-reveng-pp-v2/re_disasm/deep6/notes/pass6b_targeted.md` | Targeted analysis notes |
| `/tmp/nvidia-reveng-pp-v2/re_disasm/deep5/` | Pass 5 full structural scan (12 libs, 74 disasm) |
| `open-gpu-kernel-modules/.../ctrl/ctrlc36f.h` | Token / fault / notif definitions |
| `open-gpu-kernel-modules/.../ctrl/ctrla06c.h` | TSG schedule / bind / timeslice / interleave |
| `open-gpu-kernel-modules/.../ctrl/ctrla06f.h` | Channel GPFIFO schedule / bind |

### Key disasm windows (glcore unless noted)

| Window | VA / file off | Topic |
|--------|---------------|-------|
| `glcore_kick_ac5540.s` | `ac5520` | Multi-USERD GPPut + C36E + sfence |
| `glcore_a06f_bind_s0_0xa52b6a.s` | `a52b4a` | BIND then SCHEDULE |
| `glcore_a06c_timeslice_s1_0xa457c0.s` | `a457a0` | SET_TIMESLICE paramsize 8 |
| `glcore_sema_b6c938.s` | `b6c920` | sema INC4 + execute 0x1001 |
| `glcore_get_token_s0_0xa5326a.s` | `a5324a` | GET_TOKEN + notif index math |
| `glcore_launch_dma_s3_0xb71e54.s` | `b71e34` | LAUNCH_DMA + IMM alt |
| `glcore_mme_3800_s0_0xe1d79a.s` | `e1d77a` | MME method 0x3800 emit |
| `glcore_gpfifo_ladder_a52920.s` | `a52900` | GPFIFO class ladder |
| `glcore_ce_ladder_ab66cc.s` | `ab66b0` | CE class ladder |
| `cuda_a06c_ts_s0_0x34360e.s` | `3435ee` | cuda SET_TIMESLICE |
| `eglcore_a06f_bind_s0_0x9e093a.s` | `9e093a` | eglcore mirrors glcore |

---

## 16. Pass 6 vs pass 5 delta summary

**New / corrected:**
1. Fault buffer is **SR-IOV-only** — remove from P0 bring-up
2. A06C_0103 definitively **SET_TIMESLICE** (kernel-backed); interleave is 0107
3. A06C_SCHEDULE/BIND absent from glcore/eglcore const imms — GL uses channel schedule only
4. sema `0x01100002` confirmed **not** in sema_hdr proximity — demote to A/B only
5. CE sema methods 0x40c+ have **no const headers** — don't block on them
6. 22-library tier map (present, xdrv, opencl, fbc added to classification)
7. Silicon bring-up sequence written as explicit 17-step checklist matching blob order

**Unchanged (still solid):**
- Kick, token, sema layout, CE LAUNCH_DMA, class ladders, GPFIFO encoding, no separate RM lib

**Open code implications (for next implement ticks):**
1. Ensure `host_sema_mode` defaults to / tries **`0x1001` first**
2. Ensure schedule path does **BIND (`0xa06f0104`) then SCHEDULE (`0xa06f0103`)**
3. Make fault buffer setup **optional/non-fatal** or remove from critical path
4. Keep TSG/A06C behind optional path; not required for G1
5. Run silicon A/B: sema 0x1001 vs 0x01100002 with schedule_path/bind_rc instrumentation

