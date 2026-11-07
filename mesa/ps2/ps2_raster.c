/* SPDX-License-Identifier: MIT */
#include "ps2_raster.h"
#include <errno.h>
#include <math.h>

static int reject(struct ps2_gs_client *gs)
{
    if (!gs->error) gs->error = ENOTSUP;
    gs->count = 0;
    errno = gs->error;
    return -1;
}

static unsigned channel(float value, unsigned max)
{
    if (value <= 0) return 0;
    if (value >= 1) return max;
    return value * max + 0.5f;
}

static int rgba(const float color[4], uint32_t *packed)
{
    for (unsigned i = 0; i < 4; ++i) if (!isfinite(color[i])) return -1;
    *packed = channel(color[0],255) | (channel(color[1],255)<<8) |
        (channel(color[2],255)<<16) | (channel(color[3],128)<<24);
    return 0;
}

int ps2_gallium_clear_color(struct ps2_gs_client *gs, const struct pipe_scissor_state *scissor,
                            const union pipe_color_union *color)
{
    uint32_t packed;
    if (gs->error) return reject(gs);
    if (!color || rgba(color->f, &packed)) return reject(gs);
    unsigned x = scissor ? scissor->minx : 0, y = scissor ? scissor->miny : 0;
    unsigned right = scissor ? scissor->maxx : gs->caps.width;
    unsigned bottom = scissor ? scissor->maxy : gs->caps.height;
    /* Gallium's max bounds are exclusive. Empty clears draw nothing. */
    if (right <= x || bottom <= y) return 0;
    return ps2_gs_fill(gs, x, y, right-x, bottom-y, packed);
}

int ps2_gallium_triangle(struct ps2_gs_client *gs, const float position[3][4],
                        const float color[3][4], int depth_test)
{
    struct ps2gs_vertex vertices[3];
    if (!position || !color) return reject(gs);
    for (unsigned i = 0; i < 3; ++i) {
        for (unsigned j = 0; j < 4; ++j) if (!isfinite(position[i][j])) return reject(gs);
        /* Clipping/viewport conversion belongs upstream of this raster stage.
         * Never clamp an out-of-range triangle into a different polygon. */
        if (position[i][0] < 0 || position[i][0] > gs->caps.width ||
            position[i][1] < 0 || position[i][1] > gs->caps.height ||
            position[i][2] < 0 || position[i][2] > 1) return reject(gs);
        vertices[i].x = position[i][0] * 16.0f + 0.5f;
        vertices[i].y = position[i][1] * 16.0f + 0.5f;
        /* GS depth grows towards the viewer; Gallium near depth is zero. */
        vertices[i].z = (1.0f-position[i][2]) * 65535.0f + 0.5f;
        if (rgba(color[i], &vertices[i].rgba)) return reject(gs);
    }
    return ps2_gs_triangle(gs, vertices, depth_test);
}

int ps2_gallium_upload_color(struct ps2_gs_client *gs, enum pipe_format format,
                             const struct pipe_box *box, const void *pixels,
                             size_t source_bytes, size_t stride)
{
    if (gs->error) return reject(gs);
    if (!box || box->x < 0 || box->y < 0 || box->z || box->depth != 1 ||
        box->width <= 0 || box->height <= 0) return reject(gs);
    /* No hidden CPU format conversion, alpha rescaling or pixel rendering.
     * Independent textures and alpha-bearing formats need a resource API. */
    if ((gs->caps.bits_per_pixel == 32 && format != PIPE_FORMAT_R8G8B8X8_UNORM) ||
        (gs->caps.bits_per_pixel == 16 && format != PIPE_FORMAT_R5G5B5X1_UNORM))
        return reject(gs);
    return ps2_gs_upload(gs, box->x, box->y, box->width, box->height,
                         pixels, source_bytes, stride);
}
