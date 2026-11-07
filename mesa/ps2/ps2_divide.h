/* SPDX-License-Identifier: MIT */
#ifndef PS2_NATIVE_DIVIDE_H
#define PS2_NATIVE_DIVIDE_H
#include "ps2_rop.h"

/* Little-endian byte planes: each RGB channel is an independent integer.
 * Fixed GS passes compute min(255, numerator / denominator). A zero divisor
 * produces 255. Operands stay in VRAM; only constant palettes reach the CPU.
 * This arithmetic primitive does not advertise new XRender operators. */
struct ps2_divide_workspace {
    struct ps2_surface_client *gs;
    uint32_t remainder[4], shifted[4], difference[4];
    uint32_t borrow, carry, saturated, term, temporary, selected, retained;
};
int ps2_divide_reserve(struct ps2_divide_workspace *,struct ps2_surface_client *);
int ps2_divide_release(struct ps2_divide_workspace *);
int ps2_native_divide_u32_u24(struct ps2_divide_workspace *,
    const uint32_t numerator[4],const uint32_t denominator[3],uint32_t quotient,
    uint32_t palette,unsigned width,unsigned height);
#endif
