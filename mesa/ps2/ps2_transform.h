/* SPDX-License-Identifier: MIT */
#ifndef PS2_PIPE_TRANSFORM_H
#define PS2_PIPE_TRANSFORM_H
#include "ps2_pipe.h"
#define PS2_PIPE_MAX_TRANSFORM_TRIANGLES 256U
#define PS2_PIPE_MAX_CLIPPED_VERTICES 10U
#define PS2_PIPE_MAX_TRANSFORM_OUTPUT_TRIANGLES 2048U
struct ps2_transform_vertex { float position[4],color[4]; };
struct ps2_transform_viewport {
    unsigned x,y,width,height;
    float min_depth,max_depth;
};
/* Explicit opaque triangle-list geometry helper, separate from draw_vbo.
 * A row-major MVP multiplies each column position vector. Clip against
 * -w<=x,y,z<=w and w>=2^-16; positive NDC y maps up on the GS. Depth maps to
 * [min_depth,max_depth] and the existing Z16 reversed GS depth convention.
 * Colours are unit-range RGBA with alpha=1. GS Gouraud interpolation is affine
 * in screen space; this is not a shader or perspective-correct varying API.
 * R5900 scalar transformation/clipping only: no VU program or CPU fragment
 * renderer. Prepare every output before binding/fencing/drawing. Geometry
 * errors leave the original queue and live context intact; hardware errors
 * remain sticky. Context framebuffer/sampler references remain unchanged.
 * Up to 768 vertices and 2048 output triangles, with <=196608 heap bytes.
 */
int ps2_pipe_transform_triangles(struct pipe_context *,const struct ps2_transform_vertex *,
    unsigned vertex_count,const float matrix[16],const struct ps2_transform_viewport *,
    int depth_test,unsigned *triangles_emitted);
#endif
