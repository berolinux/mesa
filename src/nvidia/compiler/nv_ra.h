/*
 * Copyright 2026 - Open NVIDIA userspace driver project
 * SPDX-License-Identifier: MIT
 *
 * Linear-scan register allocator for NIR SSA -> SASS GPRs.
 * Assigns hardware registers 1..254 (255 = RZ/unused in emitters).
 * Tracks live ranges per SSA def and reuses registers when ranges end.
 */
#ifndef NV_RA_H
#define NV_RA_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct nir_shader;
struct nir_def;

#define NV_RA_MAX_SSA     4096
#define NV_RA_HW_FIRST    1
#define NV_RA_HW_LAST     254
#define NV_RA_HW_RZ       255

struct nv_ra_live {
   uint32_t start;   /* first use/def program point */
   uint32_t end;     /* last use program point (inclusive) */
   uint8_t  hw_reg;  /* assigned hardware register */
   bool     active;
   bool     spilled; /* true if assigned to spill slot (beyond HW_LAST) */
   uint16_t spill_slot; /* index into spill area when spilled */
};

struct nv_ra_context {
   struct nv_ra_live live[NV_RA_MAX_SSA];
   uint16_t num_ssa;
   uint16_t max_hw_reg;  /* highest hw reg assigned + 1 */
   uint16_t spill_count; /* number of spilled SSA values */
   uint32_t prog_point;  /* incrementing counter during scan */
   bool     ok;
   bool     had_spill;   /* true if any value was spilled */
};

void nv_ra_context_init(struct nv_ra_context *ra);
void nv_ra_context_finish(struct nv_ra_context *ra);

/** Build live ranges and assign registers from NIR (function impls only). */
bool nv_ra_allocate(struct nv_ra_context *ra, const struct nir_shader *nir);

/** Lookup hw register for SSA def; returns NV_RA_HW_RZ if unknown. */
uint8_t nv_ra_reg_for_def(const struct nv_ra_context *ra, const struct nir_def *def);

/** Register count suitable for SPH (at least 4, at most 255). */
uint16_t nv_ra_register_count(const struct nv_ra_context *ra, uint16_t min_regs);

/** True if allocation required spill slots (caller may insert LD/ST spill). */
static inline bool
nv_ra_had_spill(const struct nv_ra_context *ra)
{
   return ra && ra->had_spill;
}

/** Spill slot count (each slot is 4 bytes in the per-thread local spill area). */
static inline uint16_t
nv_ra_spill_count(const struct nv_ra_context *ra)
{
   return ra ? ra->spill_count : 0;
}

/** Bytes of local memory needed for spill slots (aligned to 16B). */
static inline uint32_t
nv_ra_spill_local_bytes(const struct nv_ra_context *ra)
{
   uint32_t n = ra ? (uint32_t)ra->spill_count * 4u : 0;
   return (n + 15u) & ~15u;
}

/** True if this SSA def was spilled (hw_reg may be RZ; use spill path). */
bool nv_ra_def_spilled(const struct nv_ra_context *ra, const struct nir_def *def);

/** Spill slot index for a spilled def (0..spill_count-1); 0xffff if not spilled. */
uint16_t nv_ra_def_spill_slot(const struct nv_ra_context *ra,
                              const struct nir_def *def);

/**
 * Scratch HW register reserved for spill/reload temporaries (last usable GPR
 * below RZ).  Isel uses this for address/data around LDG/STG of spill slots.
 */
#define NV_RA_SPILL_TMP_REG  254

#ifdef __cplusplus
}
#endif

#endif /* NV_RA_H */
