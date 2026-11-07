/* SPDX-License-Identifier: MIT */
#ifndef PS2_GALLIUM_RASTER_H
#define PS2_GALLIUM_RASTER_H
#include "pipe/p_state.h"
#include "ps2_gs_client.h"

/* GS raster output for a Gallium draw stage. Inputs are transformed, clipped
 * window-space vertices. This does not advertise a complete OpenGL profile. */
int ps2_gallium_clear_color(struct ps2_gs_client *, const struct pipe_scissor_state *,
                            const union pipe_color_union *);
int ps2_gallium_triangle(struct ps2_gs_client *, const float position[3][4],
                        const float color[3][4], int depth_test);
/* Front-buffer transfer for native opaque color formats. This is not yet
 * pipe_context.texture_subdata or an independent texture resource allocator. */
int ps2_gallium_upload_color(struct ps2_gs_client *, enum pipe_format,
                             const struct pipe_box *, const void *, size_t, size_t);
#endif
