/* SPDX-License-Identifier: MIT */
#ifndef PS2_PIPE_SOLID_BATCH_H
#define PS2_PIPE_SOLID_BATCH_H
#include <stdint.h>
struct pipe_context;
struct ps2_pipe_solid_box { int32_t x1,y1,x2,y2; };
#define PS2_PIPE_MAX_SOLID_BOXES 16384U
/* Explicit native adapter for file-owned color handles shared with Xorg.
 * Preflight every box before fencing or drawing. Preserve Gallium color/depth
 * bindings and references; lease the context through the entire submission.
 * No GL capability is advertised, and no CPU pixel fallback is provided.
 */
int ps2_pipe_solid_rectangles(struct pipe_context *, uint32_t handle,
    const struct ps2_pipe_solid_box *, unsigned count, uint32_t color,
    uint32_t plane_mask, unsigned alu);
/* Geometry-only zero-width line planner. Bounds are half-open destination
 * coordinates. Octant bits are X-decreasing=4, Y-decreasing=2, Y-major=1;
 * bias bit N prefers the axial pixel on an exact tie in octant N, as in Xorg.
 * The original line phase is preserved when clipped, including huge endpoints.
 * Emits disjoint horizontal/vertical runs in traversal order, excludes the last
 * endpoint unless requested, and propagates callback failure. No allocation,
 * submission or colour destination access occurs here. Caller must collect and
 * preflight the complete request before ps2_pipe_solid_rectangles.
 */
typedef int (*ps2_pipe_line_emit)(void *, const struct ps2_pipe_solid_box *);
int ps2_pipe_zero_line(int32_t x1, int32_t y1, int32_t x2, int32_t y2,
    unsigned bias, int include_last, const struct ps2_pipe_solid_box *bounds,
    ps2_pipe_line_emit, void *data);
/* Preflight ordered per-run foreground/background colours before one fence.
 * The colour is a native RGBX/RGB555 pixel, with no CPU destination access. */
struct ps2_pipe_solid_run { struct ps2_pipe_solid_box box; uint32_t color; };
int ps2_pipe_solid_runs(struct pipe_context *, uint32_t handle,
    const struct ps2_pipe_solid_run *, unsigned count, uint32_t plane_mask, unsigned alu);
struct ps2_pipe_dash_pattern {
    const unsigned char *lengths;
    unsigned count;
    uint64_t offset;
    int double_dash;
};
/* Geometry only: positive lengths, at most 256; odd-length lists repeat twice.
 * Dash phase uses original major-axis pixel distance, including clipped-away
 * pixels. The callback receives background=1 for odd runs in double-dash mode.
 * On/off odd runs emit nothing. Snapshot the list locally before callbacks.
 * Caller controls segment phase/reset and preflights the complete colour plan. */
typedef int (*ps2_pipe_dash_emit)(void *, const struct ps2_pipe_solid_box *, unsigned background);
int ps2_pipe_zero_dash(int32_t x1,int32_t y1,int32_t x2,int32_t y2,unsigned bias,
    int include_last,const struct ps2_pipe_solid_box *,const struct ps2_pipe_dash_pattern *,
    ps2_pipe_dash_emit,void *data);
#endif
