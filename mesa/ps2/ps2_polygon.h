/* SPDX-License-Identifier: MIT */
#ifndef PS2_PIPE_POLYGON_H
#define PS2_PIPE_POLYGON_H
#include "ps2_context_link.h"
#include "ps2_solid_batch.h"
#define PS2_PIPE_MAX_POLYGON_POINTS 4096U
#define PS2_PIPE_MAX_POLYGON_CLIPS 256U
#define PS2_PIPE_MAX_POLYGON_EDGE_TESTS 4194304U
#define PS2_PIPE_POLYGON_COORD_LIMIT 65535
struct ps2_pipe_polygon_point { int32_t x,y; };
/* Points are absolute storage coordinates. Clips must be disjoint, nonempty,
 * within storage. winding=0 selects even-odd, winding=1 nonzero winding.
 * Ignore horizontal edges; include top/left and exclude bottom/right edges.
 * Snapshot/plan all geometry before the original fence. At most 16384 runs;
 * no destination pixels or colour expansion array. Full RGB GXcopy uses GS
 * fills (spare bits unspecified); other ALUs/masks preserve spare bits. */
int ps2_pipe_fill_polygon(struct pipe_context *,uint32_t destination,unsigned bpp,
    const struct ps2_pipe_polygon_point *,unsigned count,
    const struct ps2_pipe_solid_box *,unsigned clip_count,unsigned winding,
    uint32_t colour,uint32_t mask,unsigned alu,unsigned *runs_emitted);
/* Geometry-only plan, with the same storage/clip/edge/run checks. Returns
 * owned one-row disjoint spans (colour zero); caller frees the array. Does not
 * fence or access GS pixels. The caller must keep its context lease and use
 * this immutable plan when preparing patterned work before submission. */
int ps2_pipe_polygon_spans(struct pipe_context *,uint32_t destination,unsigned bpp,
    const struct ps2_pipe_polygon_point *,unsigned count,
    const struct ps2_pipe_solid_box *,unsigned clip_count,unsigned winding,
    struct ps2_pipe_solid_run **spans,unsigned *span_count);
#endif
