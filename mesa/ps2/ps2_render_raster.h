/* SPDX-License-Identifier: MIT */
#ifndef PS2_PIPE_RENDER_RASTER_H
#define PS2_PIPE_RENDER_RASTER_H
#include "ps2_context_link.h"
#include <pixman.h>
#include <stddef.h>
#include <stdint.h>
struct ps2_raster_target {
    unsigned char *pixels;
    size_t bytes,stride;
    unsigned width,height,depth,msb_first;
};
/* CPU edge stepping emits geometric spans in sample space. GS renders unit
 * samples, resolves their coverage and adds/clamps it to the original raw
 * A1/A4/A8 bytes. CPU publication only copies GS-encoded protocol bytes. */
int ps2_pipe_raster_trapezoids(struct pipe_context *,const struct ps2_raster_target *,
    int xoff,int yoff,const pixman_trapezoid_t *,unsigned count);
int ps2_pipe_raster_traps(struct pipe_context *,const struct ps2_raster_target *,
    int xoff,int yoff,const pixman_trap_t *,unsigned count);
int ps2_pipe_raster_triangles(struct pipe_context *,const struct ps2_raster_target *,
    int xoff,int yoff,const pixman_triangle_t *,unsigned count);
#endif
