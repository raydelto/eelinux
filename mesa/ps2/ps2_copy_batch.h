/* SPDX-License-Identifier: MIT */
#ifndef PS2_COPY_BATCH_H
#define PS2_COPY_BATCH_H
#include <stdint.h>
struct pipe_context;
struct ps2_pipe_copy_box { int32_t sx, sy, dx, dy, width, height; };
#define PS2_PIPE_MAX_COPY_BOXES 256U
#define PS2_PIPE_MAX_COPY_PIXELS 1048576U
/* Native GXcopy/full RGB mask. Boxes form a nonoverlapping banded region with
 * one source-to-destination translation, in any order. Handles belong to this
 * context's exclusive GS file; zero names its front surface. The whole region
 * is validated before flushing or allocating. Overlaps use memmove ordering.
 * Unaligned widths use one 8 KiB GS scratch page, never CPU pixel readback.
 * Color/depth bindings and resource references remain unchanged. */
int ps2_pipe_copy_boxes(struct pipe_context *, uint32_t source, uint32_t destination,
    const struct ps2_pipe_copy_box *, unsigned count);
#endif
