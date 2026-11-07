/* SPDX-License-Identifier: MIT */
#ifndef PS2_PIPE_POLYGON_STIPPLE_H
#define PS2_PIPE_POLYGON_STIPPLE_H
#include "ps2_polygon.h"
#include "ps2_tile.h"
#define PS2_PIPE_MAX_POLYGON_STIPPLE_PIXELS 1048576U
/* Repeat an existing <=256x256 1bpp source in clipped polygon spans. Origin
 * is absolute destination-local. opaque=0 preserves zero bits; opaque=1
 * partitions background/foreground. Snapshot metadata/bits and prepare all
 * geometry/colour runs before the original fence. At most 16384 output runs
 * and 1048576 examined pixels. No destination mapping or colour pixel array.
 * All Boolean ALUs/RGB masks; full RGB GXcopy fills have unspecified spare bits.
 */
int ps2_pipe_stipple_polygon(struct pipe_context *,uint32_t destination,unsigned bpp,
    const struct ps2_pipe_polygon_point *,unsigned count,
    const struct ps2_pipe_solid_box *,unsigned clip_count,unsigned winding,
    const struct ps2_pipe_mono_pattern *,unsigned opaque,int32_t origin_x,int32_t origin_y,
    uint32_t foreground,uint32_t background,uint32_t mask,unsigned alu,unsigned *emitted);
#endif
