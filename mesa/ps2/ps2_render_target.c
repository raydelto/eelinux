/* SPDX-License-Identifier: MIT */
#include "ps2_render_target.h"
#include "util/u_inlines.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct ps2_depth_resource {
    struct pipe_resource base;
    struct ps2_render_client *owner;
    uint32_t handle;
};
static int reject(struct ps2_render_target *target, int error)
{
    if (!target->render->gs->error) target->render->gs->error = error;
    target->render->count = 0; errno = target->render->gs->error; return -1;
}
struct pipe_resource *ps2_depth_resource_create(struct pipe_screen *screen, struct ps2_render_client *render,
                                                const struct pipe_resource *t, unsigned color_bpp)
{
    if (!screen || !render || !t || t->target != PIPE_TEXTURE_2D || t->format != PIPE_FORMAT_Z16_UNORM ||
        !t->width0 || !t->height0 || t->width0 > 2048 || t->height0 > 2048 || t->depth0 != 1 ||
        t->array_size != 1 || t->last_level || t->nr_samples > 1 || t->nr_storage_samples > 1 ||
        t->nr_sparse_levels || t->flags || t->next || t->bind != PIPE_BIND_DEPTH_STENCIL ||
        (color_bpp != 16 && color_bpp != 32)) { errno = ENOTSUP; return NULL; }
    struct ps2_depth_resource *r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    if (ps2_render_depth_create(render, t->width0, t->height0, color_bpp, &r->handle)) { free(r); return NULL; }
    r->base = *t; pipe_reference_init(&r->base.reference, 1);
    r->base.screen = screen; r->owner = render;
    return &r->base;
}
void ps2_depth_resource_destroy(struct pipe_resource *base)
{
    if (!base) return;
    struct ps2_depth_resource *r = (struct ps2_depth_resource *)base;
    if (r->owner->gs->fd >= 0 && !r->owner->gs->error)
        ps2_render_depth_destroy(r->owner, r->handle);
    free(r);
}
int ps2_render_target_init(struct ps2_render_target *t, struct pipe_screen *screen, struct ps2_render_client *r)
{
    if (!t || !screen || !r || !r->gs) { errno = EINVAL; return -1; }
    memset(t, 0, sizeof(*t)); t->screen = screen; t->render = r; return 0;
}
int ps2_render_target_bind(struct ps2_render_target *t, struct pipe_resource *color, struct pipe_resource *depth)
{
    if (!color || color->screen != t->screen || !(color->bind & PIPE_BIND_RENDER_TARGET) ||
        (color->format != PIPE_FORMAT_R8G8B8X8_UNORM && color->format != PIPE_FORMAT_R5G5B5X1_UNORM))
        return reject(t, EINVAL);
    struct ps2_resource *c = (struct ps2_resource *)color;
    if (c->owner != t->render->gs) return reject(t, EXDEV);
    uint32_t depth_handle = 0;
    if (depth) {
        if (depth->screen != t->screen || depth->format != PIPE_FORMAT_Z16_UNORM ||
            !(depth->bind & PIPE_BIND_DEPTH_STENCIL)) return reject(t, EINVAL);
        struct ps2_depth_resource *d = (struct ps2_depth_resource *)depth;
        if (d->owner != t->render) return reject(t, EXDEV);
        depth_handle = d->handle;
    }
    if (ps2_render_bind(t->render, c->handle, depth_handle)) return -1;
    pipe_resource_reference(&t->color, color);
    pipe_resource_reference(&t->depth, depth);
    return 0;
}
void ps2_render_target_cleanup(struct ps2_render_target *t)
{
    if (!t || !t->render) return;
    ps2_render_flush(t->render);
    pipe_resource_reference(&t->color, NULL);
    pipe_resource_reference(&t->depth, NULL);
    t->render->target = 0; t->render->depth = 0;
}
static unsigned channel(float v, unsigned maximum)
{ return v <= 0 ? 0 : v >= 1 ? maximum : (unsigned)(v * maximum + 0.5f); }
static int rgba(const float color[4], uint32_t *packed)
{
    for (unsigned i = 0; i < 4; ++i) if (!isfinite(color[i])) return -1;
    *packed = channel(color[0],255) | channel(color[1],255)<<8 |
        channel(color[2],255)<<16 | channel(color[3],128)<<24;
    return 0;
}
int ps2_render_target_clear(struct ps2_render_target *t, const struct pipe_scissor_state *s,
                            const union pipe_color_union *color, int clear_depth)
{
    if (!t->color || (clear_depth != 0 && clear_depth != 1)) return reject(t, EINVAL);
    unsigned x = s ? s->minx : 0, y = s ? s->miny : 0;
    unsigned right = s ? s->maxx : t->color->width0, bottom = s ? s->maxy : t->color->height0;
    /* This depth command clears the full target only. A partial depth clear
     * needs an explicit bounded ABI rather than silently clearing extra pixels. */
    if (clear_depth && (!t->depth || x || y || right != t->color->width0 || bottom != t->color->height0))
        return reject(t, ENOTSUP);
    uint32_t packed = 0;
    if (color && rgba(color->f, &packed)) return reject(t, EINVAL);
    if (right <= x || bottom <= y) return 0;
    if (color && ps2_render_fill(t->render, x, y, right-x, bottom-y, packed)) return -1;
    return clear_depth ? ps2_render_clear_depth(t->render) : 0;
}
int ps2_render_target_triangle(struct ps2_render_target *t, const float position[3][4],
                               const float color[3][4], int depth_test)
{
    if (!t->color || !position || !color) return reject(t, EINVAL);
    struct ps2gs_vertex vertices[3];
    for (unsigned i = 0; i < 3; ++i) {
        for (unsigned j = 0; j < 4; ++j) if (!isfinite(position[i][j])) return reject(t, EINVAL);
        if (position[i][0] < 0 || position[i][0] > t->color->width0 ||
            position[i][1] < 0 || position[i][1] > t->color->height0 ||
            position[i][2] < 0 || position[i][2] > 1) return reject(t, ENOTSUP);
        vertices[i].x = position[i][0] * 16 + 0.5f;
        vertices[i].y = position[i][1] * 16 + 0.5f;
        vertices[i].z = (1-position[i][2]) * 65535 + 0.5f;
        if (rgba(color[i], &vertices[i].rgba)) return reject(t, EINVAL);
    }
    return ps2_render_triangle(t->render, vertices, depth_test);
}
