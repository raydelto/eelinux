/* SPDX-License-Identifier: MIT */
#ifndef PS2_GS_RENDER_CLIENT_H
#define PS2_GS_RENDER_CLIENT_H
#include "ps2_gs_surface.h"
#include "ps2-gs-render.h"
struct ps2_depth {
    uint32_t handle, width, height, color_bpp, buffer_width, bytes;
};
struct ps2_render_client {
    struct ps2_surface_client *gs;
    struct ps2_depth depths[PS2GS_MAX_SURFACES];
    uint32_t target, depth, count;
    struct ps2gs_command commands[PS2GS_MAX_COMMANDS];
};
/* Shares an existing exclusive GS file, not a new open or a CPU framebuffer.
 * One submission thread; callers fence before direct surface uploads/copies
 * and destroy all resources before closing the owning surface client. */
int ps2_render_init(struct ps2_render_client *, struct ps2_surface_client *);
int ps2_render_depth_create(struct ps2_render_client *, unsigned width, unsigned height,
                            unsigned color_bpp, uint32_t *handle);
int ps2_render_depth_destroy(struct ps2_render_client *, uint32_t handle);
int ps2_render_bind(struct ps2_render_client *, uint32_t color, uint32_t depth);
int ps2_render_flush(struct ps2_render_client *);
int ps2_render_clear_depth(struct ps2_render_client *);
int ps2_render_fill(struct ps2_render_client *, unsigned x, unsigned y,
                    unsigned width, unsigned height, uint32_t rgba);
int ps2_render_triangle(struct ps2_render_client *, const struct ps2gs_vertex[3], int depth_test);
#endif
