/* SPDX-License-Identifier: MIT */
#ifndef PS2_PIPE_CURSOR_H
#define PS2_PIPE_CURSOR_H
#include "ps2_context_link.h"
#include <stdint.h>
#include <stddef.h>
struct ps2_pipe_cursor;
struct ps2_cursor_image {
    const unsigned char *source, *mask;
    size_t source_bytes, mask_bytes, stride;
    unsigned width, height, xhot, yhot, msb_first;
    uint32_t foreground, background;
};
/* Single submission thread, one monochrome or ARGB cursor per GS front buffer. Native
 * save-under storage is 64x64; a bounded 4096-run heap plan draws masked pixels.
 * Source bits are snapshotted. No colour destination is read by the CPU.
 * Front width must be aligned to the GS local-copy unit (2 CT32 / 4 CT16).
 * Caller must remove any front observer installed later before destruction. */
struct ps2_pipe_cursor *ps2_pipe_cursor_create(struct pipe_context *);
int ps2_pipe_cursor_set(struct ps2_pipe_cursor *, const struct ps2_cursor_image *, int x, int y);
/* Original premultiplied AARRGGBB words are copied unchanged. GS converts and
 * blends them onto the native framebuffer; pixels are never blended on CPU. */
int ps2_pipe_cursor_set_argb(struct ps2_pipe_cursor *, const void *, size_t bytes,
    unsigned width, unsigned height, unsigned xhot, unsigned yhot, int x, int y);
int ps2_pipe_cursor_move(struct ps2_pipe_cursor *, int x, int y);
/* invalidate restores synchronously without flushing: safe inside a native
 * front-access callback while the existing render batch is being submitted. */
int ps2_pipe_cursor_invalidate(struct ps2_pipe_cursor *);
int ps2_pipe_cursor_invalidate_region(struct ps2_pipe_cursor *,unsigned x,unsigned y,unsigned w,unsigned h);
int ps2_pipe_cursor_flush(struct ps2_pipe_cursor *);
int ps2_pipe_cursor_suspend(struct ps2_pipe_cursor *, int suspended);
int ps2_pipe_cursor_destroy(struct ps2_pipe_cursor *);
#endif
