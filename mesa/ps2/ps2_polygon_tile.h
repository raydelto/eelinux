/* SPDX-License-Identifier: MIT */
#ifndef PS2_PIPE_POLYGON_TILE_H
#define PS2_PIPE_POLYGON_TILE_H
#include "ps2_polygon.h"
int ps2_pipe_tile_logic_polygon(struct pipe_context *,uint32_t source,uint32_t dest,unsigned bpp,
    const struct ps2_pipe_polygon_point *,unsigned count,const struct ps2_pipe_solid_box *,unsigned clip_count,
    unsigned winding,int32_t origin_x,int32_t origin_y,uint32_t plane_mask,unsigned alu,unsigned *emitted);
int ps2_pipe_tile_polygon(struct pipe_context *,uint32_t source,uint32_t destination,unsigned bpp,
    const struct ps2_pipe_polygon_point *,unsigned count,const struct ps2_pipe_solid_box *,unsigned clip_count,
    unsigned winding,int32_t origin_x,int32_t origin_y,unsigned *emitted);
#endif
