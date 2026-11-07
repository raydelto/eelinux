/* SPDX-License-Identifier: MIT */
#ifndef PS2_RENDER_TARGET_H
#define PS2_RENDER_TARGET_H
#include "pipe/p_state.h"
#include "draw/draw_pipe.h"
#include "ps2_resource.h"
#include "ps2_gs_render.h"

/* Framebuffer adapter for a future pipe_context. It retains pipe_resource
 * references while bound and fences before rebinding/destroying them. Color
 * resources use the existing owned storage factory; depth uses Z16_UNORM.
 * No GL profile, shader, texture sampler or complete context is advertised. */
struct ps2_render_target {
    struct ps2_render_client *render;
    struct pipe_screen *screen;
    struct pipe_resource *color, *depth;
};
struct pipe_resource *ps2_depth_resource_create(struct pipe_screen *, struct ps2_render_client *,
                                                const struct pipe_resource *template, unsigned color_bpp);
void ps2_depth_resource_destroy(struct pipe_resource *);
int ps2_render_target_init(struct ps2_render_target *, struct pipe_screen *, struct ps2_render_client *);
int ps2_render_target_bind(struct ps2_render_target *, struct pipe_resource *color, struct pipe_resource *depth);
void ps2_render_target_cleanup(struct ps2_render_target *);
int ps2_render_target_clear(struct ps2_render_target *, const struct pipe_scissor_state *,
                            const union pipe_color_union *, int clear_depth);
int ps2_render_target_triangle(struct ps2_render_target *, const float position[3][4],
                               const float color[3][4], int depth_test);
struct draw_stage *ps2_owned_draw_stage_create(struct draw_context *, struct ps2_render_target *);
int ps2_owned_draw_stage_outputs(struct draw_stage *, unsigned outputs,
                                 unsigned position_slot, unsigned color_slot, int depth_test);
#endif
