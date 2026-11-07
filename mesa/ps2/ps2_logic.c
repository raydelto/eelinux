/* SPDX-License-Identifier: MIT */
#include "ps2_logic.h"
#include <errno.h>
int ps2_pipe_solid_logic(struct pipe_context *context, struct pipe_resource *resource,
    const struct pipe_box *box, uint32_t color, uint32_t plane_mask, unsigned alu)
{
    if (ps2_pipe_context_acquire(context)) return -1;
    struct ps2_render_client *render = ps2_pipe_context_render(context);
    uint32_t handle = 0;
    int result = -1;
    if (!box || box->x < 0 || box->y < 0 || box->z || box->depth != 1 ||
        box->width <= 0 || box->height <= 0 || alu > 15) { errno = EINVAL; goto out; }
    if (resource) {
        if (resource->screen != context->screen ||
            (resource->format != PIPE_FORMAT_R8G8B8X8_UNORM &&
             resource->format != PIPE_FORMAT_R5G5B5X1_UNORM)) { errno = ENOTSUP; goto out; }
        struct ps2_resource *r = (void *)resource;
        if (r->owner != render->gs || !r->handle) { errno = EXDEV; goto out; }
        handle = r->handle;
    }
    const struct ps2_surface *buffer = ps2_surface_lookup(render->gs, handle);
    if (!buffer || (unsigned)box->x >= buffer->width || (unsigned)box->y >= buffer->height ||
        (unsigned)box->width > buffer->width-box->x ||
        (unsigned)box->height > buffer->height-box->y) { errno = EINVAL; goto out; }
    uint32_t bits = ps2_surface_native_mask(render->gs,handle);
    if ((color | plane_mask) & ~bits) { errno = EINVAL; goto out; }
    if (!(render->gs->caps.features & PS2GS_HW_SOLID_LOGIC)) { errno = ENOTSUP; goto out; }
    if (ps2_render_flush(render)) goto out;
    result = ps2_surface_solid_logic(render->gs, handle, box->x, box->y,
        box->width, box->height, color, plane_mask, alu);
out:
    ps2_pipe_context_release(context);
    return result;
}
