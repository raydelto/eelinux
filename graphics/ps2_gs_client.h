/* SPDX-License-Identifier: MIT */
#ifndef PS2_GS_CLIENT_H
#define PS2_GS_CLIENT_H
#include <stdint.h>
#include <stddef.h>
#include "ps2-gs.h"

/* One drawing thread owns this queue. Failure is sticky: a partially executed
 * hardware batch is never replayed and never replaced by CPU rasterization. */
struct ps2_gs_client {
    int fd, error;
    unsigned count;
    struct ps2gs_caps caps;
    struct ps2gs_command commands[PS2GS_MAX_COMMANDS];
};
int ps2_gs_open(struct ps2_gs_client *, const char *device);
void ps2_gs_close(struct ps2_gs_client *);
int ps2_gs_flush(struct ps2_gs_client *);
int ps2_gs_fill(struct ps2_gs_client *, unsigned x, unsigned y,
               unsigned width, unsigned height, uint32_t rgba);
int ps2_gs_copy(struct ps2_gs_client *, unsigned sx, unsigned sy,
               unsigned dx, unsigned dy, unsigned width, unsigned height);
int ps2_gs_triangle(struct ps2_gs_client *, const struct ps2gs_vertex vertices[3], int depth);
int ps2_gs_clear_depth(struct ps2_gs_client *);
/* Upload already existing native pixels. This is a hardware transfer, not a
 * CPU renderer. Bounds cover all addressed rows, including caller padding.
 * Queued drawing is fenced first. A partial upload failure is never replayed. */
int ps2_gs_upload(struct ps2_gs_client *, unsigned x, unsigned y,
                  unsigned width, unsigned height, const void *pixels,
                  size_t source_bytes, size_t stride);
uint32_t ps2_gs_pixel_color(uint32_t pixel, unsigned bits_per_pixel);
#endif
