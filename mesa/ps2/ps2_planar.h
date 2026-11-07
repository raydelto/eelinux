/* SPDX-License-Identifier: MIT */
#ifndef PS2_PIPE_PLANAR_H
#define PS2_PIPE_PLANAR_H
#include "ps2_context_link.h"
#include <stddef.h>
#include <stdint.h>
#define PS2_PIPE_MAX_PLANAR_BYTES (2U * 1024U * 1024U)
#define PS2_PIPE_MAX_PLANAR_BOXES 4096U
#define PS2_PIPE_MAX_PLANAR_PIXELS 524288U
struct ps2_pipe_planar_source {
    const unsigned char *bits;
    size_t bytes, stride, plane_stride;
    unsigned width, height, depth;
    int msb_first;
};
struct ps2_pipe_planar_box {
    unsigned source_x, source_y, x, y, width, height;
};
/* Source bitmap/planes only, depth 1 or the native destination depth 15/24.
 * Planes are in descending significance, rows independently padded. The depth
 * 1 path maps bits to foreground/background; colour planes ignore those two
 * values. Snapshot bits and ordered clipped boxes before the original fence.
 * Emit at most 16384 native coloured runs: full RGB-mask GXcopy uses complete
 * GS colour stores; other ALUs/masks use Boolean rectangles. Full-copy spare
 * bits are unspecified; other ALUs/masks preserve them. No destination readback,
 * colour-pixel expansion buffer or shader capability. All ALUs and RGB masks.
 * Width includes left pad (<=2079), height <=2048; one submission thread. */
int ps2_pipe_planar_boxes(struct pipe_context *, uint32_t destination,
    unsigned bpp, const struct ps2_pipe_planar_source *,
    const struct ps2_pipe_planar_box *, unsigned count,
    uint32_t foreground, uint32_t background, uint32_t mask, unsigned alu);
#endif
