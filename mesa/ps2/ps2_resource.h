/* SPDX-License-Identifier: MIT */
#ifndef PS2_PIPE_RESOURCE_H
#define PS2_PIPE_RESOURCE_H
#include "pipe/p_state.h"
#include "ps2_gs_surface.h"

struct ps2_resource {
    struct pipe_resource base;
    struct ps2_surface_client *owner;
    uint32_t handle;
};
/* Native level-zero color storage only. No sampler or OpenGL capabilities are
 * advertised by this component. The eventual pipe_screen owns the shared GS
 * session and must destroy resources before closing it. No CPU pixel mapping. */
struct pipe_resource *ps2_resource_create(struct pipe_screen *, struct ps2_surface_client *,
                                          const struct pipe_resource *template);
void ps2_resource_destroy(struct pipe_resource *);
int ps2_resource_upload(struct pipe_resource *, unsigned level, const struct pipe_box *,
                         const void *pixels, size_t bytes, size_t stride);
int ps2_resource_copy(struct pipe_resource *destination, unsigned dx, unsigned dy,
                       struct pipe_resource *source, const struct pipe_box *);
#endif
