/* SPDX-License-Identifier: MIT */
#ifndef PS2_BLIT_H
#define PS2_BLIT_H
#include <stdint.h>
#include "ps2_context_link.h"
/* Integer pixel edges. Negative source extents flip sampling; destination
 * extents are nonnegative. Optional scissor uses exclusive right/bottom edges.
 * Same native RGB format, nearest sampling, no alpha or depth/stencil work.
 * No CPU destination pixels; all geometry is checked before queue flushing. */
struct ps2_pipe_blit_rect {
    int32_t sx, sy, sw, sh, dx, dy, dw, dh;
    uint32_t scissor_enable, xmin, ymin, xmax, ymax, noop;
};
int ps2_pipe_blit_rect(struct pipe_context *, uint32_t source, uint32_t destination,
    const struct ps2_pipe_blit_rect *);
#endif
