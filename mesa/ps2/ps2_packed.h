/* SPDX-License-Identifier: MIT */
#ifndef PS2_PACKED_FLOAT_H
#define PS2_PACKED_FLOAT_H
#include "ps2_wide.h"
enum ps2_packed10_format { PS2_PACKED10_NONE,PS2_PACKED10_ARGB,PS2_PACKED10_ABGR,
    PS2_PACKED10_RGB,PS2_PACKED10_BGR };
struct ps2_packed_layout { unsigned bits[4],shift[4]; };
struct ps2_packed_workspace { struct ps2_surface_client *gs;uint32_t scratch[4]; };
int ps2_packed_layout_valid(const struct ps2_packed_layout *);
int ps2_packed_reserve(struct ps2_packed_workspace *,struct ps2_surface_client *);
int ps2_packed_release(struct ps2_packed_workspace *);
/* Original raw CT32 codes to eight normalized binary32 RGB/alpha planes.
 * Lookup tables are uniform programs, selected and evaluated entirely on GS. */
int ps2_packed_fetch(struct ps2_packed_workspace *,uint32_t raw,unsigned x,unsigned y,
    const struct ps2_packed_layout *,const uint32_t output[8],uint32_t palette,unsigned width,unsigned height);
int ps2_packed_store(struct ps2_packed_workspace *,struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t input[8],uint32_t raw,unsigned x,unsigned y,const struct ps2_packed_layout *,uint32_t palette,
    unsigned width,unsigned height);
/* All thirteen basic Porter-Duff operations. Source/mask remain read-only;
 * destination planes are replaced. Component-alpha factors stay on GS. */
int ps2_packed_blend(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t source[8],const uint32_t mask[8],const uint32_t destination[8],
    uint32_t palette,unsigned width,unsigned height,unsigned operation,unsigned component_alpha);
/* Borrow twelve dead sampling registers, reducing live GS surface slots.
 * The caller retains ownership; source and mask planes remain read-only. */
int ps2_packed_blend_scratch(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const uint32_t source[8],const uint32_t mask[8],const uint32_t destination[8],const uint32_t scratch[12],
    uint32_t palette,unsigned width,unsigned height,unsigned operation,unsigned component_alpha);
#endif
