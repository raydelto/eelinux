/* SPDX-License-Identifier: MIT */
#include "ps2_sampler.h"
#include "util/u_inlines.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
struct ps2_texture_stage {
    struct draw_stage base;
    struct ps2_render_target *target;
    struct pipe_sampler_view *view;
    struct pipe_sampler_state sampler;
    unsigned outputs, position, color, texture;
    int modulate, depth;
};
static void reject(struct ps2_texture_stage *s)
{
    if (!s->target->render->gs->error) s->target->render->gs->error = ENOTSUP;
    s->target->render->count = 0;
}
static void triangle(struct draw_stage *b, struct prim_header *p)
{
    struct ps2_texture_stage *s = (struct ps2_texture_stage*)b;
    if (s->target->render->gs->error) return;
    if (!s->outputs || !p) { reject(s); return; }
    float positions[3][4], colors[3][4], uv[3][2];
    for (unsigned i = 0; i < 3; ++i) {
        if (!p->v[i] || p->v[i]->clipmask) { reject(s); return; }
        memcpy(positions[i], p->v[i]->data[s->position], sizeof(positions[i]));
        memcpy(colors[i], p->v[i]->data[s->color], sizeof(colors[i]));
        memcpy(uv[i], p->v[i]->data[s->texture], sizeof(uv[i]));
    }
    ps2_sampler_triangle(s->target, s->view, &s->sampler, positions, uv, colors, s->modulate, s->depth);
}
static void unsupported(struct draw_stage *b, struct prim_header *p)
{ (void)p; reject((struct ps2_texture_stage*)b); }
static void flush(struct draw_stage *b, unsigned flags)
{ (void)flags; ps2_render_flush(((struct ps2_texture_stage*)b)->target->render); }
static void reset(struct draw_stage *b) { (void)b; }
static void destroy(struct draw_stage *b)
{
    struct ps2_texture_stage *s = (struct ps2_texture_stage*)b;
    ps2_render_flush(s->target->render);
    pipe_sampler_view_reference(&s->view, NULL); free(s);
}
struct draw_stage *ps2_textured_draw_stage_create(struct draw_context *draw, struct ps2_render_target *target,
    struct pipe_sampler_view *view, const struct pipe_sampler_state *sampler, int modulate, int depth_test)
{
    if (!draw || !target || !target->render || !target->color || !view ||
        view->texture->screen != target->screen || !ps2_sampler_state_supported(sampler) ||
        (modulate != 0 && modulate != 1) || (depth_test != 0 && depth_test != 1) ||
        (depth_test && !target->depth) || target->render->gs->error) { errno = ENOTSUP; return NULL; }
    struct ps2_texture_stage *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->target = target; s->sampler = *sampler; s->modulate = modulate; s->depth = depth_test;
    pipe_sampler_view_reference(&s->view, view);
    s->base.draw = draw; s->base.name = "ps2-gs-textured"; s->base.tri = triangle;
    s->base.point = unsupported; s->base.line = unsupported; s->base.flush = flush;
    s->base.reset_stipple_counter = reset; s->base.destroy = destroy;
    return &s->base;
}
int ps2_textured_draw_stage_outputs(struct draw_stage *b, unsigned outputs,
    unsigned position, unsigned color, unsigned texture)
{
    if (!b) { errno = EINVAL; return -1; }
    struct ps2_texture_stage *s = (struct ps2_texture_stage*)b;
    if (!outputs || outputs > PIPE_MAX_SHADER_OUTPUTS || position >= outputs || color >= outputs ||
        texture >= outputs || position == color || position == texture || color == texture) {
        reject(s); errno = ENOTSUP; return -1;
    }
    if (ps2_render_flush(s->target->render)) return -1;
    s->outputs = outputs; s->position = position; s->color = color; s->texture = texture;
    return 0;
}
