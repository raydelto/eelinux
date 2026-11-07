/* SPDX-License-Identifier: MIT */
#ifndef PS2_PIPE_IMAGE_H
#define PS2_PIPE_IMAGE_H
#include "ps2_context_link.h"
#include <stdint.h>
#include <stddef.h>
#define PS2_PIPE_MAX_IMAGE_BYTES (2U * 1024U * 1024U)
#define PS2_PIPE_MAX_IMAGE_BOXES 4096U
#define PS2_PIPE_MAX_IMAGE_PIXELS 524288U
struct ps2_pipe_image_box {
    size_t offset, stride;
    unsigned x, y, width, height;
};
/* Source-only client pixels, in the native RGBX/32 or RGB555/16 layout.
 * Snapshot source and ordered boxes before fencing the original queue.
 * GXcopy/full RGB mask uses native uploads; other ALUs/masks use at most
 * 16384 coloured Boolean runs. Overlapping boxes retain input order.
 * Invalid, empty, noop, allocation and work-limit requests do not flush.
 * No destination mapping or readback. One submission thread. */
int ps2_pipe_image_boxes(struct pipe_context *, uint32_t destination,
    unsigned bpp, const void *source, size_t bytes,
    const struct ps2_pipe_image_box *, unsigned count, uint32_t mask, unsigned alu);
#endif
