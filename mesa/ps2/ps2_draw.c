/* SPDX-License-Identifier: MIT */
#include "ps2_draw.h"
#include "ps2_raster.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

struct ps2_draw_stage {
    struct draw_stage base;
    struct ps2_gs_client *gs;
    unsigned outputs, position, color;
    int depth;
};

static void reject(struct ps2_draw_stage *stage, int error)
{
    if (!stage->gs->error) stage->gs->error = error;
    stage->gs->count = 0;
}

static void triangle(struct draw_stage *base, struct prim_header *primitive)
{
    struct ps2_draw_stage *stage = (struct ps2_draw_stage *)base;
    if (stage->gs->error) return;
    if (!stage->outputs || !primitive) { reject(stage, EINVAL); return; }
    float positions[3][4], colors[3][4];
    for (unsigned i = 0; i < 3; ++i) {
        if (!primitive->v[i] || primitive->v[i]->clipmask) {
            reject(stage, ENOTSUP); return;
        }
        memcpy(positions[i], primitive->v[i]->data[stage->position], sizeof(positions[i]));
        memcpy(colors[i], primitive->v[i]->data[stage->color], sizeof(colors[i]));
    }
    ps2_gallium_triangle(stage->gs, positions, colors, stage->depth);
}

static void unsupported(struct draw_stage *base, struct prim_header *primitive)
{
    (void)primitive;
    reject((struct ps2_draw_stage *)base, ENOTSUP);
}

static void flush(struct draw_stage *base, unsigned flags)
{
    (void)flags;
    ps2_gs_flush(((struct ps2_draw_stage *)base)->gs);
}

static void reset_stipple(struct draw_stage *base)
{
    (void)base; /* No stippled primitive is accepted by this stage. */
}

static void destroy(struct draw_stage *base)
{
    /* draw_context/pipe_context must fence before destruction. Destruction
     * itself must never replay or submit commands after a failed flush. */
    free(base);
}

struct draw_stage *ps2_draw_stage_create(struct draw_context *draw, struct ps2_gs_client *gs)
{
    if (!draw || !gs || gs->fd < 0 || gs->error) { errno = EINVAL; return NULL; }
    struct ps2_draw_stage *stage = calloc(1, sizeof(*stage));
    if (!stage) return NULL;
    stage->gs = gs;
    stage->base.draw = draw;
    stage->base.name = "ps2-gs";
    stage->base.tri = triangle;
    stage->base.point = unsupported;
    stage->base.line = unsupported;
    stage->base.flush = flush;
    stage->base.reset_stipple_counter = reset_stipple;
    stage->base.destroy = destroy;
    return &stage->base;
}

int ps2_draw_stage_outputs(struct draw_stage *base, unsigned outputs,
                           unsigned position, unsigned color, int depth)
{
    if (!base) { errno = EINVAL; return -1; }
    struct ps2_draw_stage *stage = (struct ps2_draw_stage *)base;
    if (!outputs || outputs > PIPE_MAX_SHADER_OUTPUTS || position >= outputs ||
        color >= outputs || position == color || (depth != 0 && depth != 1)) {
        reject(stage, EINVAL); errno = stage->gs->error; return -1;
    }
    if (depth && !(stage->gs->caps.features & PS2GS_HW_DEPTH16)) {
        reject(stage, ENOTSUP); errno = stage->gs->error; return -1;
    }
    /* A state change must not reinterpret commands queued under prior state. */
    if (ps2_gs_flush(stage->gs)) return -1;
    stage->outputs = outputs; stage->position = position;
    stage->color = color; stage->depth = depth;
    return 0;
}
