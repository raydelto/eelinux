/* SPDX-License-Identifier: MIT */
#ifndef PS2_PIPE_LOGIC_H
#define PS2_PIPE_LOGIC_H
#include "ps2_pipe.h"
#include "ps2_gs_logic.h"
/* Explicit native constant-source operation on a color resource, or front
 * when resource is NULL. Flush the shared Mesa queue without changing its
 * target/depth binding. This is not a shader or OpenGL logic-op capability. */
int ps2_pipe_solid_logic(struct pipe_context *, struct pipe_resource *,
    const struct pipe_box *, uint32_t color, uint32_t plane_mask, unsigned alu);
#endif
