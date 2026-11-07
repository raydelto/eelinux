/* SPDX-License-Identifier: MIT */
#ifndef PS2_PIPE_PLANE_H
#define PS2_PIPE_PLANE_H
#include "ps2_copy_batch.h"
/* Snapshot aliases/front storage before any destination writes. GS lookups
 * select foreground/background from source bits; all ALUs/masks stay on GS.
 * Source and destination may have different native depths or alias. */
int ps2_pipe_plane_copy_boxes(struct pipe_context *, uint32_t source,
    uint32_t destination, const struct ps2_pipe_copy_box *, unsigned count,
    uint32_t plane, uint32_t foreground, uint32_t background,
    uint32_t plane_mask, unsigned alu);
#endif
