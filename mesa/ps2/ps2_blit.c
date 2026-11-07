/* SPDX-License-Identifier: MIT */
#include "ps2_pipe.h"
#include "ps2_blit.h"
#include "ps2_copy_batch.h"
#include <errno.h>
#include <string.h>
static uint32_t bits(float value) { uint32_t result; memcpy(&result, &value, 4); return result; }
static unsigned padded(unsigned value) { unsigned result = 1; while (result < value) result *= 2; return result; }
int ps2_pipe_blit_rect(struct pipe_context *context, uint32_t source, uint32_t destination,
    const struct ps2_pipe_blit_rect *box)
{
    if (!box || box->scissor_enable > 1 || box->noop > 1) { errno = EINVAL; return -1; }
    if (ps2_pipe_context_acquire(context)) return -1;
    struct ps2_render_client *render = ps2_pipe_context_render(context);
    struct ps2_surface_client *gs = render->gs;
    const struct ps2_surface *s = ps2_surface_lookup(gs, source), *d = ps2_surface_lookup(gs, destination);
    int status = -1, error = EINVAL;
    if (!s || !d || s->bits_per_pixel != d->bits_per_pixel ||
        s->width > 2048 || s->height > 2048 || d->width > 2048 || d->height > 2048 ||
        (s->bits_per_pixel != 16 && s->bits_per_pixel != 32) ||
        box->sx < 0 || box->sy < 0 || box->dx < 0 || box->dy < 0 || box->dw < 0 || box->dh < 0 ||
        (uint32_t)box->sx > s->width || (uint32_t)box->sy > s->height ||
        (int64_t)box->sx + box->sw < 0 || (int64_t)box->sy + box->sh < 0 ||
        (int64_t)box->sx + box->sw > s->width || (int64_t)box->sy + box->sh > s->height ||
        (int64_t)box->dx + box->dw > d->width || (int64_t)box->dy + box->dh > d->height ||
        (box->scissor_enable && (box->xmin > box->xmax || box->ymin > box->ymax))) goto out;
    if (!box->dw || !box->dh) { status = 0; goto out; }
    if (!box->sw || !box->sh) goto out;
    if (box->noop) { status = 0; goto out; }
    int x1 = box->dx, y1 = box->dy, x2 = box->dx + box->dw, y2 = box->dy + box->dh;
    if (box->scissor_enable) {
        if (box->xmin > (unsigned)x1) x1 = box->xmin < (unsigned)x2 ? (int)box->xmin : x2;
        if (box->ymin > (unsigned)y1) y1 = box->ymin < (unsigned)y2 ? (int)box->ymin : y2;
        if (box->xmax < (unsigned)x2) x2 = box->xmax;
        if (box->ymax < (unsigned)y2) y2 = box->ymax;
    }
    if (x1 >= x2 || y1 >= y2) { status = 0; goto out; }
    if (box->sw == box->dw && box->sh == box->dh) {
        /* Also permits front-buffer and overlapping copies without sampling
         * a texture and its own render target. The batch owns odd-width tails. */
        const struct ps2_pipe_copy_box copy = { box->sx + x1 - box->dx, box->sy + y1 - box->dy,
            x1, y1, x2 - x1, y2 - y1 };
        status = ps2_pipe_copy_boxes(context, source, destination, &copy, 1);
        error = errno; goto out;
    }
    if (!source || source == destination || s->width > 1024 || s->height > 1024 ||
        !(gs->caps.features & PS2GS_HW_TEXTURE)) { error = ENOTSUP; goto out; }
    unsigned x[4] = { x1, x2, x2, x1 }, y[4] = { y1, y1, y2, y2 };
    static const unsigned indices[2][3] = {{0,1,2}, {0,2,3}};
    struct ps2gs_texture_triangle triangles[2] = {0};
    for (unsigned i = 0; i < 2; ++i) for (unsigned j = 0; j < 3; ++j) {
        unsigned k = indices[i][j];
        /* Clip destination geometry, then evaluate the original mapping at
         * those edges. Scissoring must not restart or stretch the texture. */
        double u = box->sx + (double)((int)x[k] - box->dx) * box->sw / box->dw;
        double v = box->sy + (double)((int)y[k] - box->dy) * box->sh / box->dh;
        triangles[i].v[j] = (struct ps2gs_texture_vertex){
            .position = {.x = x[k]*16, .y = y[k]*16, .rgba = 0x80808080},
            .s = bits((float)(u / padded(s->width))),
            .t = bits((float)(v / padded(s->height))), .q = 0x3f800000 };
    }
    status = ps2_render_flush(render);
    /* Isolated submission state preserves the live framebuffer, depth and
     * sampler bindings. Kernel packets set their complete GS context. */
    struct ps2_render_client temporary = {.gs = gs, .target = destination};
    if (!status) status = ps2_texture_triangles(&temporary, source, triangles, 2);
    error = errno;
out:
    ps2_pipe_context_release(context);
    if (status) errno = error;
    return status;
}
