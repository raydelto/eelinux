/* SPDX-License-Identifier: MIT */
#ifndef PS2_PIPE_TILE_H
#define PS2_PIPE_TILE_H
#include "ps2_solid_batch.h"
#include <stddef.h>
#include <stdint.h>
#define PS2_PIPE_MAX_TILE_BOXES 256U
#define PS2_PIPE_MAX_TILE_PIECES 65536U
#define PS2_PIPE_MAX_TILE_PIXELS 1048576U
/* Repeat a distinct owned native RGBX/RGB555 pixmap, <=1024 per dimension.
 * Rectangles and tile origin are destination-local; negative origins work.
 * Validate the entire request and reserve optional 64x32 GS scratch before
 * fencing. Small tiles expand on GS into a repeated block, <=8192 bytes.
 * Preserve live Mesa bindings; no source readback or CPU destination pixels.
 */
int ps2_pipe_tile_rectangles(struct pipe_context *, uint32_t source, uint32_t destination,
    const struct ps2_pipe_solid_box *, unsigned count, int32_t origin_x, int32_t origin_y);
/* Geometry spans may exceed the public rectangle request's 256-box limit.
 * Snapshot <=16384 boxes; retain the same 1 Mi-pixel/65536-piece budgets. */
int ps2_pipe_tile_geometry(struct pipe_context *,uint32_t source,uint32_t destination,
    const struct ps2_pipe_solid_box *,unsigned count,int32_t origin_x,int32_t origin_y);
struct ps2_pipe_mono_pattern {
    const unsigned char *bits;
    size_t bytes, stride;
    unsigned width, height;
    int msb_first;
};
/* Read an existing monochrome source bitmap (<=256x256), snapshot at most
 * 8192 source bytes, and construct a temporary color pattern using GS fills.
 * Repeat it through the native tile path; GXcopy/full RGB masks only.
 * All boxes and source metadata, heap/GS reservations precede queue flush.
 * No CPU color-pixel expansion or destination rasterization. Temporary GS
 * resources are released on success or reclaimed on failed-session closure.
 */
int ps2_pipe_opaque_stipple_rectangles(struct pipe_context *, uint32_t destination,
    const struct ps2_pipe_mono_pattern *, const struct ps2_pipe_solid_box *, unsigned count,
    int32_t origin_x, int32_t origin_y, uint32_t foreground, uint32_t background);
#define PS2_PIPE_MAX_STIPPLE_RUNS 262144U
#define PS2_PIPE_MAX_STIPPLE_LOGIC_RUNS 16384U
/* Transparent stipple: leave zero bits unchanged and emit native rectangles
 * for one-bit runs, merging identical neighboring source rows. Snapshot only
 * source bits (<=8192 bytes); validate/count the entire request before fencing,
 * then stream bounded native packets without a destination-pixel/run array.
 * GXcopy/full RGB uses owned-render packets; other ALUs/masks use GS Boolean
 * rectangles, with a smaller work limit. Optional emitted is zero on no-op or
 * error and the native rectangle count on success. No new GL capability. */
int ps2_pipe_stipple_rectangles(struct pipe_context *, uint32_t destination,
    const struct ps2_pipe_mono_pattern *, const struct ps2_pipe_solid_box *, unsigned count,
    int32_t origin_x, int32_t origin_y, uint32_t foreground, uint32_t plane_mask,
    unsigned alu, unsigned *emitted);
/* General opaque stipple: partition each box into disjoint zero/one runs so
 * every destination pixel applies exactly one background/foreground ALU.
 * All sixteen Boolean ALUs and RGB masks; request order is retained on overlap.
 * Equal source colours under the active mask collapse to one rectangle per box.
 * Same snapshot/preflight/run limits as transparent stipples. The existing
 * opaque GXcopy/full-mask tile adapter remains the efficient repeat path. */
int ps2_pipe_opaque_stipple_logic_rectangles(struct pipe_context *, uint32_t destination,
    const struct ps2_pipe_mono_pattern *, const struct ps2_pipe_solid_box *, unsigned count,
    int32_t origin_x, int32_t origin_y, uint32_t foreground, uint32_t background,
    uint32_t plane_mask, unsigned alu, unsigned *emitted);
#endif
