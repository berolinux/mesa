/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * GPFIFO channel management.  Allocation sequence follows nvidia-push
 * (open-gpu-kernel-modules/src/common/unix/nvidia-push/src/nvidia-push-init.c):
 *   1. Allocate USERD memory (NV01_MEMORY_LOCAL_USER or SYSTEM)
 *   2. Allocate error notifier memory
 *   3. Allocate pushbuffer + GPFIFO ring in mappable memory
 *   4. RmAlloc(GPFIFO class, NV_CHANNEL_ALLOC_PARAMS)
 *   5. Map USERD, schedule channel, optionally get work-submit token
 *   6. Kickoff: write GPFIFO entries, update GPPut in USERD / doorbell
 */

#ifndef NV_CHANNEL_H
#define NV_CHANNEL_H

#include <stdbool.h>
#include <stdint.h>

#include "nv_device_info.h"

#ifdef __cplusplus
extern "C" {
#endif

struct nv_rm_device;
struct nv_rm_bo;

#define NV_CHANNEL_DEFAULT_GPFIFO_ENTRIES  512
#define NV_CHANNEL_DEFAULT_PUSH_DWORDS     (64 * 1024)
#define NV_CHANNEL_USERD_SIZE              4096
#define NV_CHANNEL_NOTIFIER_SIZE           4096
#define NV_CHANNEL_MAX_USERD_SLOTS         9
/* NV_CHANNEL_ALLOC_PARAMS.hUserdMemory[] length is NV_MAX_SUBDEVICES (8) */
#define NV_CHANNEL_MAX_USERD_HANDLES       8

struct nv_channel {
   struct nv_rm_device *rm;
   const struct nv_device_info *info;

   uint32_t h_channel;
   uint32_t h_userd_mem;
   uint32_t h_error_notifier;      /* memory handle for notifier BO */
   uint32_t h_error_ctxdma;        /* NV01_CONTEXT_ERROR_TO_MEMORY over notifier */
   /* tick99: owned async RC event (eventfd + NV01_EVENT_OS_EVENT); 0 / -1 if none */
   uint32_t h_error_event;         /* NV01_EVENT handle under subdevice */
   int error_event_fd;             /* eventfd for RC notifications; -1 if none */
   uint32_t error_event_notify_index; /* NV2080_NOTIFIERS_* used at setup */
   uint32_t h_push_mem;
   uint32_t h_gpfifo_mem;
   uint32_t h_vaspace;             /* FERMI_VASPACE_A passed at channel alloc */
   uint32_t h_channel_group;       /* KEPLER_CHANNEL_GROUP_A if used */
   uint32_t h_ctxshare;            /* FERMI_CONTEXT_SHARE_A if used */
   /* Engine objects allocated under channel (RM context; SET_OBJECT still uses class ID) */
   uint32_t h_obj_copy;            /* DMA copy class (e.g. AMPERE_DMA_COPY_A) */
   uint32_t h_obj_compute;         /* compute class */
   uint32_t h_obj_3d;              /* 3D class */
   uint32_t h_obj_nvdec;           /* NVDEC video decode (tick89; optional) */
   uint32_t h_obj_nvenc;           /* NVENC video encode (tick89; optional) */
   uint32_t h_obj_copy_parent;     /* RM parent used for alloc (for correct free) */
   uint32_t h_obj_compute_parent;
   uint32_t h_obj_3d_parent;
   uint32_t h_obj_nvdec_parent;
   uint32_t h_obj_nvenc_parent;
   uint32_t class_copy_bound;      /* class ID used for h_obj_copy alloc (0 if none) */
   uint32_t class_compute_bound;
   uint32_t class_3d_bound;
   uint32_t class_nvdec_bound;
   uint32_t class_nvenc_bound;
   int engine_alloc_rc;            /* 0 ok, negative if all engine allocs failed */
   uint32_t engine_type;
   uint32_t gpfifo_class;
   uint32_t work_submit_token;
   bool has_work_submit_token;
   bool scheduled;
   int schedule_rc;                /* last GPFIFO_SCHEDULE errno (0 = ok/n/a) */
   int schedule_bind_rc;           /* last A06C/A06F BIND errno (0 ok / -1 n/a) */
   /*
    * Which schedule path succeeded (smoke/triage):
    *   0 = not scheduled
    *   1 = A06C TSG schedule (optionally after BIND)
    *   2 = A06F channel schedule (optionally after BIND)
    */
   int schedule_path;
   bool use_channel_group;

   /*
    * Host sema emit mode that worked on this channel (pass5 A/B ladder).
    * -1 = unknown (try full ladder); else nv_host_sema_mode value to reuse first.
    */
   int host_sema_mode_pref;
   /* Fault method buffer BO (optional; pass5 0xc36f0109 UPDATE_FAULT_METHOD_BUFFER) */
   struct nv_rm_bo *fault_method_bo;
   uint64_t fault_method_gpu_addr;
   int fault_method_rc;            /* 0 ok, negative if setup failed, -1 n/a */

   /* CPU mappings */
   volatile uint32_t *userd;       /* USERD control block (GPPut/GPGet/...) */
   /*
    * Pass8/tick87: multi-USERD for MIG/multi-subdevice (glcore ac5526 loops 9).
    * Slot 0 mirrors userd; additional slots registered via nv_channel_add_userd_slot.
    * Kickoff passes all non-NULL slots to nvidia_gpfifo_submit_one_multi.
    */
   volatile void *userd_slots[NV_CHANNEL_MAX_USERD_SLOTS];
   unsigned userd_slot_count;      /* 1..9; at least 1 when userd mapped */
   /* tick105: RM handles for NV_CHANNEL_ALLOC_PARAMS.hUserdMemory[0..7] */
   uint32_t userd_handles[NV_CHANNEL_MAX_USERD_HANDLES];
   uint64_t userd_handle_offsets[NV_CHANNEL_MAX_USERD_HANDLES];
   unsigned userd_handle_count;    /* 0 = use h_userd_mem only at slot 0 */
   volatile void *error_notifier;  /* mapped notifier memory (nvidia_notification_t) */
   uint32_t *gpfifo_cpu;           /* GPFIFO ring (pairs of dwords) */
   uint32_t *push_cpu;             /* pushbuffer backing */
   uint64_t gpfifo_gpu_addr;       /* GPU VA/offset of GPFIFO ring start */
   uint64_t push_gpu_addr;         /* GPU VA/offset of pushbuffer start */
   uint32_t gpfifo_entries;        /* ring capacity in entries */
   uint32_t gpfifo_put;            /* next entry index to write */
   uint32_t push_dw_size;
   uint32_t push_dw_used;
   uint32_t push_dw_base;          /* start of current kickoff segment */

   /* Doorbell: pointer into device usermode region (not owned by channel) */
   volatile void *usermode_map;
   /* Optional multi-doorbell maps (pass8: usermode_map[i]+0x90); slot 0 = usermode_map */
   volatile void *usermode_slots[NV_CHANNEL_MAX_USERD_SLOTS];
   unsigned usermode_slot_count;

   struct nv_rm_bo *userd_bo;
   /* tick106: extra USERD BOs for multi-subdevice hUserdMemory[1..7] (MIG path) */
   struct nv_rm_bo *userd_extra_bos[NV_CHANNEL_MAX_USERD_HANDLES];
   unsigned userd_extra_bo_count;
   struct nv_rm_bo *notifier_bo;
   struct nv_rm_bo *gpfifo_bo;
   struct nv_rm_bo *push_bo;
};

/** Register an additional USERD host mapping (slot 1..8). Slot 0 is primary userd. */
int nv_channel_add_userd_slot(struct nv_channel *ch, volatile void *userd_map);
/** Register an additional usermode/doorbell page (MIG multi-subdevice). */
int nv_channel_add_usermode_slot(struct nv_channel *ch, volatile void *usermode_map);

/**
 * tick105: register RM memory handle for multi-USERD channel alloc (hUserdMemory[i]).
 * Slot 0 is always primary h_userd_mem; slots 1..7 are optional MIG/subdevice USERD BOs.
 * Handles are applied at channel RmAlloc time via nv_channel_fill_userd_alloc_params().
 */
int nv_channel_set_userd_handle(struct nv_channel *ch, unsigned slot,
                                uint32_t h_userd_mem, uint64_t userd_offset);
/** Copy registered handles/offsets into channel alloc params (up to 8 slots). */
unsigned nv_channel_fill_userd_alloc_params(const struct nv_channel *ch,
                                            uint32_t *h_userd_memory_out,
                                            uint64_t *userd_offset_out,
                                            unsigned max_slots);

/**
 * tick106: allocate and register additional USERD memory BOs for slots 1..n-1
 * (slot 0 is primary userd_bo).  n_slots is desired total handles (1..8).
 * Best-effort: returns number of handles registered (at least 1 if primary ok).
 * Env NV_CHANNEL_USERD_SLOTS=N overrides (capped at 8).  Default: 1, or
 * subdevice_count when RM reports >1, or NV_CHANNEL_MULTI_USERD=1 forces >=2.
 */
unsigned nv_channel_alloc_extra_userd_slots(struct nv_channel *ch,
                                           unsigned n_slots_total);

struct nv_channel *
nv_channel_create(struct nv_rm_device *rm, uint32_t engine_type,
                  uint32_t gpfifo_entries, uint32_t push_dwords);

void
nv_channel_destroy(struct nv_channel *ch);

/**
 * Resolve engine class for smoke/submit when ch->info is missing or class is 0.
 * order: explicit arg > ch->info > Turing/Ampere-ish fallbacks (host encode
 * selftests use NVC6B5/NVC3C0/NVC597 placeholders; HW should use real classlist).
 */
uint32_t nv_channel_resolve_class_copy(const struct nv_channel *ch,
                                       uint32_t explicit_class);
uint32_t nv_channel_resolve_class_compute(const struct nv_channel *ch,
                                          uint32_t explicit_class);
uint32_t nv_channel_resolve_class_3d(const struct nv_channel *ch,
                                     uint32_t explicit_class);
uint32_t nv_channel_resolve_class_nvdec(const struct nv_channel *ch,
                                        uint32_t explicit_class);
uint32_t nv_channel_resolve_class_nvenc(const struct nv_channel *ch,
                                        uint32_t explicit_class);

/** Begin recording methods into the pushbuffer; returns pointer to write at */
uint32_t *
nv_channel_push_begin(struct nv_channel *ch, uint32_t need_dwords);

/**
 * Ensure channel is scheduled + work_submit_token/usermode doorbell ready.
 * Called lazily from kickoff if create-time setup was incomplete.
 * Returns 0 even if schedule/token still missing (GPPut-only path may work);
 * use nv_channel_submit_preflight() for strict G1 bring-up checks.
 */
int nv_channel_ensure_submit_ready(struct nv_channel *ch);

/**
 * Best-effort RmAlloc of copy/compute/3D engine objects (parent = channel,
 * then channel_group, device, subdevice; class alternates on failure).
 * Required on many RM paths before engine methods; SET_OBJECT still uses class ID.
 * On success, class_*_bound is set and resolve_class_* prefers it.
 * Safe to call multiple times; skips engines already allocated.
 */
int nv_channel_ensure_engine_objects(struct nv_channel *ch);

/**
 * Ensure USERD / GPFIFO / push BOs are mapped into device VASpace (NVOS46).
 * Channel alloc and GPFIFO entry addresses require real GPU VAs, not only RM
 * phys offsets.  Called from ensure_submit_ready; safe to call repeatedly.
 */
int nv_channel_ensure_buffers_gpu_va(struct nv_channel *ch);

/**
 * G1/G2 bring-up preflight: ensure_submit_ready + verify channel has USERD/GPFIFO/push
 * and is scheduled.  On failure sets *detail_out (optional) to a short errno-ish code:
 *  -EINVAL missing objects, -EAGAIN not scheduled, 0 ok (token/doorbell optional).
 */
int nv_channel_submit_preflight(struct nv_channel *ch, int *detail_out);

/** Commit pushbuffer segment and submit via GPFIFO + GPPut update */
int
nv_channel_kickoff(struct nv_channel *ch);

/** Current push dwords written since last kickoff begin */
uint32_t
nv_channel_push_used(struct nv_channel *ch);

/** Wait for GPU to consume GPFIFO (poll GPGet) with timeout in ns; 0 = try once */
int
nv_channel_wait_idle(struct nv_channel *ch, uint64_t timeout_ns);

/**
 * Submit push segment (kickoff) then optionally wait for GPFIFO drain.
 * wait_timeout_ns: 0 = kick only; UINT64_MAX-style large = wait long.
 * Returns kickoff error or wait error.
 */
int
nv_channel_submit_and_wait(struct nv_channel *ch, uint64_t wait_timeout_ns);

/** Kickoff if there are pending push dwords; no-op if empty. */
int
nv_channel_flush(struct nv_channel *ch);

/**
 * Poll channel error notifier (nvidia_notification_t in error_notifier BO).
 * Clears status to DONE_SUCCESS after successful read when clear_on_ok is set.
 * Returns 0 if idle/success, -EIO if error status, -ETIMEDOUT, -EINVAL.
 */
int
nv_channel_check_notifier(struct nv_channel *ch, bool clear_on_ok,
                          uint64_t timeout_ns);

/** Non-blocking notifier peek: 0=ok/idle, -EIO=error, -EAGAIN=in progress. */
int
nv_channel_notifier_status(struct nv_channel *ch, uint16_t *status_out,
                           uint32_t *info32_out);

/**
 * Best-effort channel recovery after error notifier / hung submit (tick90/91).
 * If h_channel_group: A06C PREEMPT (wait) first, then optional A06F STOP,
 * RESTART_RUNLIST, re-BIND+SCHEDULE.  Does not free engine objects.
 * Returns 0 if rescheduled, else last errno (recovery is best-effort).
 */
int nv_channel_recover(struct nv_channel *ch, bool stop_first);

/** Fetch A06F context ID into *ctx_id_out (diagnostics; non-fatal if fails). */
int nv_channel_get_context_id(struct nv_channel *ch, uint32_t *ctx_id_out);

/**
 * tick91: TSG (A06C) helpers when ch->h_channel_group is set; else -ENOTSUP.
 * timeslice_us: SET/GET on channel group; interleave: A06C levels (0/1/2).
 * preempt: group-level (preferred over A06F STOP for multi-channel TSGs).
 * get_tsg_id: A06C GET_INFO hardware TSG id for diagnostics.
 */
int nv_channel_tsg_set_timeslice(struct nv_channel *ch, uint64_t timeslice_us);
int nv_channel_tsg_get_timeslice(struct nv_channel *ch, uint64_t *timeslice_us_out);
int nv_channel_tsg_preempt(struct nv_channel *ch, bool wait,
                           bool manual_timeout, uint32_t timeout_us);
int nv_channel_tsg_get_id(struct nv_channel *ch, uint32_t *tsg_id_out);
int nv_channel_tsg_set_interleave(struct nv_channel *ch, uint32_t level);
int nv_channel_tsg_get_interleave(struct nv_channel *ch, uint32_t *level_out);
/** A06F SET_ERROR_NOTIFIER; notify_tsg applies policy to all channels in TSG. */
int nv_channel_set_error_notifier_policy(struct nv_channel *ch,
                                         bool notify_each_in_tsg);

/**
 * tick92: NV0080 device FIFO helpers (object = device handle via ch->rm).
 * stop/start_runlist: engine-wide runlist pause (privileged / mode-dependent).
 * idle_self: IDLE_CHANNELS with this channel only (flags/timeout from caller).
 * get_latency_buffer: engine gp/pb entry counts for ring sizing diagnostics.
 */
int nv_channel_fifo_stop_runlist(struct nv_channel *ch);
int nv_channel_fifo_start_runlist(struct nv_channel *ch);
int nv_channel_fifo_idle_self(struct nv_channel *ch, uint32_t flags,
                              uint32_t timeout_us);
int nv_channel_fifo_get_latency_buffer(struct nv_channel *ch,
                                       uint32_t *gp_entries_out,
                                       uint32_t *pb_entries_out);

/**
 * tick93: optional async error path — allocate NV01_EVENT_OS_EVENT on subdevice
 * for channel error notifier (notify_index typically NV2080_NOTIFIERS_RC or 0).
 * Stores handle in *h_event_out; caller owns and must nv_rm_free_object.
 * os_event_fd: eventfd/pipe read end; also register via nvidia_rm_alloc_os_event
 * on the device fd path when available.
 */
int nv_channel_alloc_error_event(struct nv_channel *ch, int os_event_fd,
                                 uint32_t notify_index, uint32_t *h_event_out);
/** Poll RM event queue once; convenience over nv_rm_get_event_data. */
int nv_channel_poll_rm_event(struct nv_channel *ch, uint32_t *h_object_out,
                             uint32_t *notify_index_out, uint32_t *info32_out,
                             uint16_t *info16_out, uint32_t *more_out);

/**
 * tick99: create eventfd + NV01_EVENT_OS_EVENT for channel RC errors and
 * store on the channel (idempotent if already set).  Caller may pass
 * external_fd >= 0 to use an existing fd (channel does not own/close it);
 * external_fd < 0 allocates a new eventfd (channel owns and closes on destroy).
 * Returns 0 on success, negative on failure (channel still usable without event).
 */
int nv_channel_ensure_error_event(struct nv_channel *ch, int external_fd,
                                  uint32_t notify_index);

/** Tear down channel-owned error event/eventfd (safe if never set). */
void nv_channel_destroy_error_event(struct nv_channel *ch);

/** eventfd fd for poll/epoll (-1 if none). Do not close unless you own external. */
static inline int
nv_channel_error_event_fd(const struct nv_channel *ch)
{
   return ch ? ch->error_event_fd : -1;
}

/**
 * tick94: Bind channel error CTXDMA to channel (NVOS49) if h_error_ctxdma set.
 * Called best-effort after create; also invocable explicitly.
 */
int nv_channel_bind_error_ctxdma(struct nv_channel *ch);
/** NVOS30 single-channel idle (spin+pb default flags if flags==0). */
int nv_channel_idle_rm(struct nv_channel *ch, uint32_t flags,
                       uint32_t timeout_us);

/** Host reset of error notifier before a submit (clears stale IN_PROGRESS). */
void
nv_channel_notifier_reset(struct nv_channel *ch);

/**
 * Advance push cursor by dwords written via nv_channel_push_begin() pointer
 * (or direct writes into the returned region). Call before kickoff/flush.
 */
void
nv_channel_push_advance(struct nv_channel *ch, uint32_t dwords_written);

/** GPFIFO put index (for smoke/debug). */
static inline uint32_t
nv_channel_gpfifo_put(const struct nv_channel *ch)
{
   return ch ? ch->gpfifo_put : 0;
}

/**
 * Kickoff pending push, optionally wait GPFIFO idle, then check error notifier.
 * wait_timeout_ns: 0 = kick only (no GPFIFO wait); notifier still checked if
 * check_notifier is true (non-blocking peek).
 * Returns first error among kick / wait / notifier.
 */
int
nv_channel_submit_wait_check(struct nv_channel *ch, uint64_t wait_timeout_ns,
                             bool check_notifier, bool clear_notifier_on_ok);

/**
 * Poll a host-mappable sema dword (GEQ payload). Used after CE sema-release
 * LAUNCH_DMA when sema BO is CPU-mapped. timeout_ns 0 = try once.
 */
int
nv_channel_wait_sema_cpu(volatile uint32_t *sema_cpu, uint32_t payload,
                         uint64_t timeout_ns);

/**
 * tick109: effective sema/GPFIFO wait timeout from channel probe (virt stretches).
 * requested 0 => treat as default 2s then apply virt floor; non-zero uses max(req, virt min).
 */
static inline uint64_t
nv_channel_effective_wait_timeout_ns(const struct nv_channel *ch,
                                     uint64_t requested_ns)
{
   uint64_t d = requested_ns ? requested_ns : 2000000000ull;
   const struct nv_device_info *info = ch ? ch->info : NULL;
   return nv_device_info_gpfifo_stall_ns(info, d);
}

/**
 * Full vertical-slice wait: GPFIFO drain + sema GEQ + optional notifier check.
 * sema_cpu may be NULL (skip sema wait). payload 0 skips sema wait.
 * tick109: internally applies nv_channel_effective_wait_timeout_ns when ch->info set.
 */
int
nv_channel_submit_wait_sema(struct nv_channel *ch,
                            volatile uint32_t *sema_cpu, uint32_t sema_payload,
                            uint64_t wait_timeout_ns, bool check_notifier);

/**
 * G1 vertical slice (hardware path): SET_OBJECT copy class, pitch buffer copy
 * with CE one-word sema on LAUNCH_DMA, flush + wait sema GEQ (+ optional
 * notifier).  sema_cpu must be host-mapped sema BO; reset to 0 before call
 * (or pass sema_reset=true).  class_copy 0 uses ch->info->class_copy.
 *
 * Returns 0 on sema completion; negative errno on encode/submit/wait failure.
 * Without HAVE_LIBDRM_NVIDIA returns -ENOSYS (use host selftest for encode-only).
 */
int
nv_channel_g1_ce_copy_sema_submit(struct nv_channel *ch,
                                  uint32_t class_copy,
                                  uint64_t src_gpu_addr,
                                  uint64_t dst_gpu_addr,
                                  uint32_t size_bytes,
                                  uint64_t sema_gpu_addr,
                                  volatile uint32_t *sema_cpu,
                                  uint32_t sema_payload,
                                  bool sema_reset,
                                  uint64_t wait_timeout_ns,
                                  bool check_notifier);

/**
 * G1 copy+sema with class alternates (bound/info/C7B5/C6B5/C5B5/...).
 * On success, *class_used_out (optional) receives the working class_copy.
 * pipelined=true uses PIPELINED LAUNCH_DMA (second pass if non-pipelined fails).
 * tick110: each class/subch also tries combined sema, split sema-only follow-up,
 * and pitch2d (height=1) emit modes before moving to the next class.
 */
int
nv_channel_g1_ce_copy_sema_submit_try_classes(struct nv_channel *ch,
                                              uint64_t src_gpu_addr,
                                              uint64_t dst_gpu_addr,
                                              uint32_t size_bytes,
                                              uint64_t sema_gpu_addr,
                                              volatile uint32_t *sema_cpu,
                                              uint32_t sema_payload,
                                              bool sema_reset,
                                              uint64_t wait_timeout_ns,
                                              bool check_notifier,
                                              bool try_pipelined,
                                              uint32_t *class_used_out);

/**
 * Pass7 G1 strategy when host sema works but CE sema does not: emit CE copy
 * without CE sema (LAUNCH_DMA data only), then NVC36F host sema release.
 * Completes via host sema memory write; validates copy via sema_cpu/payload.
 */
int
nv_channel_g1_ce_copy_then_host_sema_submit(struct nv_channel *ch,
                                            uint32_t class_copy,
                                            uint64_t src_gpu_addr,
                                            uint64_t dst_gpu_addr,
                                            uint32_t size_bytes,
                                            uint64_t sema_gpu_addr,
                                            volatile uint32_t *sema_cpu,
                                            uint32_t sema_payload,
                                            bool sema_reset,
                                            uint64_t wait_timeout_ns,
                                            bool check_notifier,
                                            int *host_sema_mode_out);

/**
 * G3: clear/draw methods without 3D sema; complete via host sema (tick86).
 * Use when host sema ok but 3D sema/clear path fails (kickoff works).
 */
int
nv_channel_g3_clear_then_host_sema_submit(struct nv_channel *ch,
                                          uint32_t class_3d,
                                          uint64_t ct_gpu_addr,
                                          uint32_t ct_w, uint32_t ct_h,
                                          uint32_t ct_format,
                                          const uint32_t color_ui[4],
                                          bool emit_draw,
                                          uint64_t sema_gpu_addr,
                                          volatile uint32_t *sema_cpu,
                                          uint32_t sema_payload,
                                          bool sema_reset,
                                          uint64_t wait_timeout_ns,
                                          bool check_notifier,
                                          int *host_sema_mode_out,
                                          uint32_t *class_used_out);

/** G1 sema-only CE fence (no data transfer) + submit/wait. */
int
nv_channel_g1_ce_sema_only_submit(struct nv_channel *ch,
                                  uint32_t class_copy,
                                  uint64_t sema_gpu_addr,
                                  volatile uint32_t *sema_cpu,
                                  uint32_t sema_payload,
                                  bool sema_reset,
                                  uint64_t wait_timeout_ns,
                                  bool check_notifier);

/**
 * G1 tertiary probe: CE REMAP u32 fill to dst + sema (no src buffer).
 * Isolates pitch-copy OFFSET_IN/src issues from class/sema/kickoff.
 */
int
nv_channel_g1_ce_remap_fill_sema_submit(struct nv_channel *ch,
                                        uint32_t class_copy,
                                        uint64_t dst_gpu_addr,
                                        uint32_t size_bytes,
                                        uint32_t fill_data,
                                        uint64_t sema_gpu_addr,
                                        volatile uint32_t *sema_cpu,
                                        uint32_t sema_payload,
                                        bool sema_reset,
                                        uint64_t wait_timeout_ns,
                                        bool check_notifier);

/**
 * G1 quaternary probe: host/GPFIFO NVC36F SEMAPHORE* release only (no CE/3D/compute
 * SET_OBJECT).  If this succeeds, kickoff/GPPut/doorbell works and failure is in
 * engine methods/class; if it fails, fix schedule/doorbell/USERD first.
 */
int
nv_channel_gpfifo_host_sema_submit(struct nv_channel *ch,
                                   uint64_t sema_gpu_addr,
                                   volatile uint32_t *sema_cpu,
                                   uint32_t sema_payload,
                                   bool sema_reset,
                                   uint64_t wait_timeout_ns,
                                   bool check_notifier);

/**
 * Host sema with silicon A/B mode ladder (pass5 / 610.43.02 glcore b6c952).
 * Tries blob 0x1001, then vdpau 0x2 (pass8), then open-header bitfields last.
 * On success writes winning mode into *mode_used_out (if non-NULL).
 * On total failure mode_used_out gets last attempted mode.
 */
int
nv_channel_gpfifo_host_sema_submit_ex(struct nv_channel *ch,
                                      uint64_t sema_gpu_addr,
                                      volatile uint32_t *sema_cpu,
                                      uint32_t sema_payload,
                                      bool sema_reset,
                                      uint64_t wait_timeout_ns,
                                      bool check_notifier,
                                      int *mode_used_out);

/** Snapshot USERD GPGet/GPPut and host gpfifo_put index (0 if unavailable). */
void nv_channel_userd_snapshot(struct nv_channel *ch,
                               uint32_t *gp_get_out, uint32_t *gp_put_out,
                               uint32_t *host_put_out);

/**
 * G2 vertical slice (hardware path): compute SET_OBJECT + SPA/CWD init
 * (optional) + invalidate + smoke QMD materialize + SEND_PCAS with QMD sema
 * release0, then submit_wait_sema.
 *
 * qmd_host must be host-writable 256B at qmd_gpu_addr (scratch BO). program_gpu
 * may be 0 for encode/submit plumbing tests (HW will fault if shader invalid).
 * class_compute 0 uses ch->info->class_compute.
 */
int
nv_channel_g2_compute_smoke_sema_submit(struct nv_channel *ch,
                                        uint32_t class_compute,
                                        uint64_t program_gpu_addr,
                                        uint32_t register_count,
                                        uint8_t sass_version,
                                        uint64_t qmd_gpu_addr,
                                        void *qmd_host,
                                        uint64_t sema_gpu_addr,
                                        volatile uint32_t *sema_cpu,
                                        uint32_t sema_payload,
                                        bool sema_reset,
                                        bool emit_init_state,
                                        bool method_invalidate,
                                        uint64_t wait_timeout_ns,
                                        bool check_notifier);

/**
 * G2 smoke submit trying class_compute alternates (bound/info/CCC0..C3C0 pass8).
 * On success sets class_compute_bound and *class_used_out if non-NULL.
 */
int
nv_channel_g2_compute_smoke_sema_submit_try_classes(struct nv_channel *ch,
                                                    uint64_t program_gpu_addr,
                                                    uint32_t register_count,
                                                    uint8_t sass_version,
                                                    uint64_t qmd_gpu_addr,
                                                    void *qmd_host,
                                                    uint64_t sema_gpu_addr,
                                                    volatile uint32_t *sema_cpu,
                                                    uint32_t sema_payload,
                                                    bool sema_reset,
                                                    bool emit_init_state,
                                                    bool method_invalidate,
                                                    uint64_t wait_timeout_ns,
                                                    bool check_notifier,
                                                    uint32_t *class_used_out);

/**
 * G2: QMD/PCAS without QMD sema; completion via host sema (tick85, mirrors G1 ce_hs).
 * Use when host sema ok but QMD sema path fails.
 */
int
nv_channel_g2_compute_smoke_then_host_sema_submit(struct nv_channel *ch,
                                                  uint32_t class_compute,
                                                  uint64_t program_gpu_addr,
                                                  uint32_t register_count,
                                                  uint8_t sass_version,
                                                  uint64_t qmd_gpu_addr,
                                                  void *qmd_host,
                                                  uint64_t sema_gpu_addr,
                                                  volatile uint32_t *sema_cpu,
                                                  uint32_t sema_payload,
                                                  bool sema_reset,
                                                  bool emit_init_state,
                                                  bool method_invalidate,
                                                  uint64_t wait_timeout_ns,
                                                  bool check_notifier,
                                                  int *host_sema_mode_out,
                                                  uint32_t *class_used_out);

/**
 * G2 with caller-filled nv_qmd_desc (e.g. store-imm smoke shader params).
 * sema on QMD release0 via sema_gpu_addr/payload if non-zero.
 */
struct nv_qmd_desc; /* from nv_qmd.h — include before use in C files */

int
nv_channel_g2_compute_dispatch_sema_submit(struct nv_channel *ch,
                                           uint32_t class_compute,
                                           const struct nv_qmd_desc *desc,
                                           uint64_t qmd_gpu_addr,
                                           void *qmd_host,
                                           uint64_t sema_gpu_addr,
                                           volatile uint32_t *sema_cpu,
                                           uint32_t sema_payload,
                                           bool sema_reset,
                                           bool emit_init_state,
                                           bool method_invalidate,
                                           uint64_t wait_timeout_ns,
                                           bool check_notifier);

/**
 * G3 vertical slice (hardware path, no MME): SET_OBJECT 3D, optional pitch
 * colour target, colour clear, optional smoke triangle draw, 3D report sema,
 * submit_wait_sema.  class_3d 0 uses ch->info->class_3d.
 *
 * ct_gpu_addr 0 skips CT bind (clear methods only; HW may NOP without RT).
 * emit_draw false = clear+sema only (safer encode test without shaders).
 */
int
nv_channel_g3_clear_sema_submit(struct nv_channel *ch,
                                uint32_t class_3d,
                                uint64_t ct_gpu_addr,
                                uint32_t ct_w, uint32_t ct_h,
                                uint32_t ct_format,
                                const uint32_t color_ui[4],
                                bool emit_draw,
                                uint64_t sema_gpu_addr,
                                volatile uint32_t *sema_cpu,
                                uint32_t sema_payload,
                                bool sema_reset,
                                uint64_t wait_timeout_ns,
                                bool check_notifier);

/** G3: 3D report sema only (fence marker) + submit/wait. */
int
nv_channel_g3_sema_only_submit(struct nv_channel *ch,
                               uint32_t class_3d,
                               uint64_t sema_gpu_addr,
                               volatile uint32_t *sema_cpu,
                               uint32_t sema_payload,
                               bool sema_reset,
                               uint64_t wait_timeout_ns,
                               bool check_notifier);

/**
 * tick113: G3 CT + ZT bind, colour+depth/stencil clear, sema submit/wait.
 * zt_gpu_addr 0 falls back to colour-only ladder (nv_channel_g3_clear_sema_submit).
 * zt_format 0 => Z24S8; zs clear uses depth+stencil for combined formats.
 */
int
nv_channel_g3_clear_rt_sema_submit(struct nv_channel *ch,
                                   uint32_t class_3d,
                                   uint64_t ct_gpu_addr,
                                   uint32_t ct_w, uint32_t ct_h,
                                   uint32_t ct_format,
                                   const uint32_t color_ui[4],
                                   uint64_t zt_gpu_addr,
                                   uint32_t zt_format,
                                   float depth_val, uint32_t stencil_val,
                                   uint64_t sema_gpu_addr,
                                   volatile uint32_t *sema_cpu,
                                   uint32_t sema_payload,
                                   bool sema_reset,
                                   uint64_t wait_timeout_ns,
                                   bool check_notifier);

#ifdef __cplusplus
}
#endif

#endif /* NV_CHANNEL_H */
