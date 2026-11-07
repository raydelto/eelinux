/* SPDX-License-Identifier: MIT */
#ifndef PS2_SAMPLER_H
#define PS2_SAMPLER_H
#include "ps2_render_target.h"
#include "ps2_gs_texture.h"
/* Real Gallium view/reference objects, level-zero native RGB only. The caller
 * supplies its context's sampler_view_destroy callback. No GL caps implied. */
int ps2_sampler_state_supported(const struct pipe_sampler_state *);
struct pipe_sampler_view *ps2_sampler_view_create(struct pipe_context *, struct ps2_render_client *,
    struct pipe_resource *, const struct pipe_sampler_view *);
void ps2_sampler_view_destroy(struct pipe_context *, struct pipe_sampler_view *);
int ps2_sampler_upload(struct ps2_render_client *, struct pipe_resource *, const struct pipe_box *,
                       const void *, size_t bytes, size_t stride);
/* position.xyz are window coordinates and normalized depth, position.w is
 * reciprocal clip W. uv is normalized and bounded [0,1]. Computes STQ on CPU;
 * interpolation, texture sampling and all pixel drawing execute in the GS. */
int ps2_sampler_triangle(struct ps2_render_target *, struct pipe_sampler_view *,
    const struct pipe_sampler_state *, const float position[3][4], const float uv[3][2],
    const float color[3][4], int modulate, int depth_test);
struct draw_stage *ps2_textured_draw_stage_create(struct draw_context *, struct ps2_render_target *,
    struct pipe_sampler_view *, const struct pipe_sampler_state *, int modulate, int depth_test);
int ps2_textured_draw_stage_outputs(struct draw_stage *, unsigned outputs,
    unsigned position_slot, unsigned color_slot, unsigned texture_slot);
#endif
