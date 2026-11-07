/* SPDX-License-Identifier: MIT */
#ifndef PS2_PIPE_H
#define PS2_PIPE_H
#include "ps2_sampler.h"
#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "ps2_context_link.h"

/* Native resource/context foundation, not a complete OpenGL pipe driver.
 * Shader caps are zero and draw_vbo rejects unimplemented shader execution.
 * Only one context/submission thread is supported. The borrowed render client
 * must outlive all screen resources and context leases. */
struct pipe_screen *ps2_pipe_screen_create_shared(struct ps2_render_client *);
struct pipe_screen *ps2_pipe_screen_create(const char *device);
struct ps2_render_target *ps2_pipe_context_target(struct pipe_context *);
int ps2_pipe_context_error(struct pipe_context *);
/* An Xorg callback owner keeps a lease until it has restored its callbacks. */
/* Bounded final-stage adapter for already transformed/clipped vertices. This
 * is intentionally separate from draw_vbo until the shader pipeline exists. */
int ps2_pipe_triangle(struct pipe_context *, const float position[3][4],
    const float color[3][4], const float uv[3][2], int depth_test);
#endif
