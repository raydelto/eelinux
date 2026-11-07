/* SPDX-License-Identifier: MIT */
#ifndef PS2_CONTEXT_LINK_H
#define PS2_CONTEXT_LINK_H
/* Narrow lifetime interface: Xorg need not include Gallium's internal headers
 * or expose its macro namespace. The implementations live in the Mesa PS2
 * context library, linked by the eventual native DDX. */
struct pipe_context;
struct ps2_render_client;
struct ps2_render_client *ps2_pipe_context_render(struct pipe_context *);
int ps2_pipe_context_acquire(struct pipe_context *);
void ps2_pipe_context_release(struct pipe_context *);
#endif
