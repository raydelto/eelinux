/* SPDX-License-Identifier: MIT */
#ifndef PS2_GS_LOGIC_CLIENT_H
#define PS2_GS_LOGIC_CLIENT_H
#include "ps2_gs_surface.h"
#include "ps2-gs-logic.h"
#include "ps2-gs-batch.h"
/* Native pixel values; alpha is preserved. Sticky hardware failure, one
 * drawing thread, pre-effect EAGAIN retries only. No destination mapping. */
int ps2_surface_solid_logic(struct ps2_surface_client *, uint32_t handle,
    unsigned x, unsigned y, unsigned width, unsigned height,
    uint32_t color, uint32_t plane_mask, unsigned alu);
int ps2_surface_solid_batch(struct ps2_surface_client *, uint32_t handle,
    const struct ps2gs_solid_logic *rectangles, unsigned count);
#endif
