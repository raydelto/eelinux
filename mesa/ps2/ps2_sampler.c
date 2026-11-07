/* SPDX-License-Identifier: MIT */
#include "ps2_sampler.h"
#include "pipe/p_context.h"
#include "util/u_inlines.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
struct ps2_sampler_view { struct pipe_sampler_view base; struct ps2_render_client *owner; };
int ps2_sampler_state_supported(const struct pipe_sampler_state *s)
{
    return s && s->wrap_s == PIPE_TEX_WRAP_CLAMP_TO_EDGE && s->wrap_t == PIPE_TEX_WRAP_CLAMP_TO_EDGE &&
        s->min_img_filter == PIPE_TEX_FILTER_NEAREST && s->mag_img_filter == PIPE_TEX_FILTER_NEAREST &&
        s->min_mip_filter == PIPE_TEX_MIPFILTER_NONE && !s->compare_mode && !s->unnormalized_coords &&
        s->max_anisotropy <= 1 && !s->seamless_cube_map && !s->reduction_mode &&
        !s->lod_bias && isfinite(s->min_lod) && isfinite(s->max_lod) && s->min_lod <= s->max_lod;
}
struct pipe_sampler_view *ps2_sampler_view_create(struct pipe_context *ctx, struct ps2_render_client *r,
    struct pipe_resource *texture, const struct pipe_sampler_view *t)
{
    if (!ctx || !ctx->sampler_view_destroy || !r || !texture || !t || ctx->screen != texture->screen ||
        texture->target != PIPE_TEXTURE_2D || !(texture->bind & PIPE_BIND_SAMPLER_VIEW) ||
        texture->width0 > 1024 || texture->height0 > 1024 || t->target != PIPE_TEXTURE_2D ||
        t->format != texture->format || (t->format != PIPE_FORMAT_R8G8B8X8_UNORM &&
        t->format != PIPE_FORMAT_R5G5B5X1_UNORM) || t->is_tex2d_from_buf ||
        t->u.tex.first_level || t->u.tex.last_level || t->u.tex.first_layer || t->u.tex.last_layer ||
        t->swizzle_r != PIPE_SWIZZLE_X || t->swizzle_g != PIPE_SWIZZLE_Y ||
        t->swizzle_b != PIPE_SWIZZLE_Z || t->swizzle_a != PIPE_SWIZZLE_1 ||
        ((struct ps2_resource*)texture)->owner != r->gs || !(r->gs->caps.features & PS2GS_HW_TEXTURE)) {
        errno = ENOTSUP; return NULL;
    }
    struct ps2_sampler_view *view = calloc(1, sizeof(*view));
    if (!view) return NULL;
    view->base = *t; view->base.texture = NULL; view->base.context = ctx; view->owner = r;
    pipe_reference_init(&view->base.reference, 1);
    pipe_resource_reference(&view->base.texture, texture);
    return &view->base;
}
void ps2_sampler_view_destroy(struct pipe_context *ctx, struct pipe_sampler_view *base)
{
    if (!base) return;
    struct ps2_sampler_view *view = (struct ps2_sampler_view*)base;
    (void)ctx;
    ps2_render_flush(view->owner);
    pipe_resource_reference(&base->texture, NULL); free(view);
}
int ps2_sampler_upload(struct ps2_render_client *r, struct pipe_resource *texture, const struct pipe_box *box,
                       const void *pixels, size_t bytes, size_t stride)
{
    if (!texture || ((struct ps2_resource*)texture)->owner != r->gs) { errno = EXDEV; return -1; }
    if (ps2_render_flush(r)) return -1;
    return ps2_resource_upload(texture, 0, box, pixels, bytes, stride);
}
static int fail(struct ps2_render_target *t, int error)
{
    if (!t || !t->render || !t->render->gs) { errno = error; return -1; }
    if (!t->render->gs->error) t->render->gs->error = error;
    t->render->count = 0; errno = t->render->gs->error; return -1;
}
static unsigned channel(float v) { return v <= 0 ? 0 : v >= 1 ? 128 : (unsigned)(v*128 + 0.5f); }
static uint32_t bits(float value) { uint32_t result; memcpy(&result, &value, sizeof(result)); return result; }
static float coordinate_scale(unsigned size)
{
    unsigned padded=1; while (padded<size) padded*=2;
    return (float)size/padded;
}
int ps2_sampler_triangle(struct ps2_render_target *t, struct pipe_sampler_view *view,
    const struct pipe_sampler_state *state, const float p[3][4], const float uv[3][2],
    const float color[3][4], int modulate, int depth_test)
{
    if (!t || !t->render || !t->color || !view || !p || !uv || !color ||
        ((struct ps2_sampler_view*)view)->owner != t->render || view->texture == t->color ||
        !ps2_sampler_state_supported(state) || (modulate != 0 && modulate != 1) ||
        (depth_test != 0 && depth_test != 1) || (depth_test && !t->depth)) return fail(t, ENOTSUP);
    struct ps2gs_texture_triangle triangle = { .flags =
        (depth_test ? PS2GS_DEPTH_TEST : 0) | (modulate ? PS2GS_TEXTURE_MODULATE : 0) };
    float scale_s=coordinate_scale(view->texture->width0), scale_t=coordinate_scale(view->texture->height0);
    for (unsigned i = 0; i < 3; ++i) {
        for (unsigned j = 0; j < 4; ++j) if (!isfinite(p[i][j]) || !isfinite(color[i][j])) return fail(t, EINVAL);
        if (!isfinite(uv[i][0]) || !isfinite(uv[i][1]) || uv[i][0] < 0 || uv[i][0] > 1 ||
            uv[i][1] < 0 || uv[i][1] > 1 || p[i][0] < 0 || p[i][1] < 0 ||
            p[i][0] > t->color->width0 || p[i][1] > t->color->height0 ||
            p[i][2] < 0 || p[i][2] > 1 || p[i][3] < 1.0f/4096 || p[i][3] > 4096) return fail(t, ENOTSUP);
        struct ps2gs_texture_vertex *v = &triangle.v[i];
        v->position = (struct ps2gs_vertex) { .x = p[i][0]*16+0.5f, .y = p[i][1]*16+0.5f,
            .z = (1-p[i][2])*65535+0.5f, .rgba = channel(color[i][0]) | channel(color[i][1])<<8 |
            channel(color[i][2])<<16 | channel(color[i][3])<<24 };
        v->q = bits(p[i][3]); v->s = bits(uv[i][0]*scale_s*p[i][3]); v->t = bits(uv[i][1]*scale_t*p[i][3]);
    }
    return ps2_texture_triangles(t->render, ((struct ps2_resource*)view->texture)->handle, &triangle, 1);
}
