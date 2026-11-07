/* SPDX-License-Identifier: MIT */
#include "ps2_render_target.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
struct ps2_owned_stage {
    struct draw_stage base;
    struct ps2_render_target *target;
    unsigned outputs, position, color;
    int depth;
};
static void reject(struct ps2_owned_stage *s, int error)
{
    if (!s->target->render->gs->error) s->target->render->gs->error = error;
    s->target->render->count = 0;
}
static void triangle(struct draw_stage *base, struct prim_header *p)
{
    struct ps2_owned_stage *s = (struct ps2_owned_stage *)base;
    if (s->target->render->gs->error) return;
    if (!s->outputs || !p) { reject(s, EINVAL); return; }
    float positions[3][4], colors[3][4];
    for (unsigned i = 0; i < 3; ++i) {
        if (!p->v[i] || p->v[i]->clipmask) { reject(s, ENOTSUP); return; }
        memcpy(positions[i], p->v[i]->data[s->position], sizeof(positions[i]));
        memcpy(colors[i], p->v[i]->data[s->color], sizeof(colors[i]));
    }
    ps2_render_target_triangle(s->target, positions, colors, s->depth);
}
static void unsupported(struct draw_stage *b, struct prim_header *p)
{ (void)p; reject((struct ps2_owned_stage *)b, ENOTSUP); }
static void flush(struct draw_stage *b, unsigned flags)
{ (void)flags; ps2_render_flush(((struct ps2_owned_stage *)b)->target->render); }
static void reset(struct draw_stage *b) { (void)b; }
static void destroy(struct draw_stage *b) { free(b); }
struct draw_stage *ps2_owned_draw_stage_create(struct draw_context *draw, struct ps2_render_target *target)
{
    if (!draw || !target || !target->render || !target->color || target->render->gs->error) {
        errno = EINVAL; return NULL;
    }
    struct ps2_owned_stage *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->target = target; s->base.draw = draw; s->base.name = "ps2-gs-owned";
    s->base.tri = triangle; s->base.point = unsupported; s->base.line = unsupported;
    s->base.flush = flush; s->base.reset_stipple_counter = reset; s->base.destroy = destroy;
    return &s->base;
}
int ps2_owned_draw_stage_outputs(struct draw_stage *b, unsigned outputs, unsigned position, unsigned color, int depth)
{
    if (!b) { errno = EINVAL; return -1; }
    struct ps2_owned_stage *s = (struct ps2_owned_stage *)b;
    if (!outputs || outputs > PIPE_MAX_SHADER_OUTPUTS || position >= outputs || color >= outputs ||
        position == color || (depth != 0 && depth != 1) || (depth && !s->target->depth)) {
        reject(s, EINVAL); errno = s->target->render->gs->error; return -1;
    }
    if (ps2_render_flush(s->target->render)) return -1;
    s->outputs = outputs; s->position = position; s->color = color; s->depth = depth;
    return 0;
}
