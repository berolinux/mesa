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

#ifdef __cplusplus
}
#endif

#endif /* NV_RA_H */
