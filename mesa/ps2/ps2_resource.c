/* SPDX-License-Identifier: MIT */
#include "ps2_resource.h"
#include "util/u_inlines.h"
#include <errno.h>
#include <stdlib.h>

struct pipe_resource *ps2_resource_create(struct pipe_screen *screen, struct ps2_surface_client *gs,
                                          const struct pipe_resource *template)
{
    if (!template || !gs || template->target != PIPE_TEXTURE_2D ||
        !template->width0 || !template->height0 || template->width0 > 2048 || template->height0 > 2048 ||
        template->depth0 != 1 || template->array_size != 1 || template->last_level ||
        template->nr_samples > 1 || template->nr_storage_samples > 1 ||
        template->nr_sparse_levels || template->flags || template->next ||
        (template->bind & ~(PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW)) ||
        (template->format != PIPE_FORMAT_R8G8B8X8_UNORM && template->format != PIPE_FORMAT_R5G5B5X1_UNORM)) {
        errno = ENOTSUP; return NULL;
    }
    struct ps2_resource *resource = calloc(1, sizeof(*resource));
    if (!resource) return NULL;
    unsigned bpp = template->format == PIPE_FORMAT_R8G8B8X8_UNORM ? 32 : 16;
    if (ps2_surface_create(gs, template->width0, template->height0, bpp, &resource->handle)) {
        free(resource); return NULL;
    }
    resource->base = *template;
    pipe_reference_init(&resource->base.reference, 1);
    resource->base.screen = screen;
    resource->owner = gs;
    return &resource->base;
}

void ps2_resource_destroy(struct pipe_resource *base)
{
    if (!base) return;
    struct ps2_resource *resource = (struct ps2_resource *)base;
    if (resource->owner->fd >= 0 && !resource->owner->error)
        ps2_surface_destroy(resource->owner, resource->handle);
    /* A failed session retains kernel storage until close; never replay draws
     * or implicitly create a replacement CPU resource during destruction. */
    free(resource);
}

static int box_valid(const struct pipe_box *box)
{
    return box && box->x >= 0 && box->y >= 0 && !box->z &&
        box->width > 0 && box->height > 0 && box->depth == 1;
}

int ps2_resource_upload(struct pipe_resource *base, unsigned level, const struct pipe_box *box,
                         const void *pixels, size_t bytes, size_t stride)
{
    if (!base || level || !box_valid(box)) { errno = ENOTSUP; return -1; }
    struct ps2_resource *resource = (struct ps2_resource *)base;
    return ps2_surface_upload(resource->owner, resource->handle, box->x, box->y,
                              box->width, box->height, pixels, bytes, stride);
}

int ps2_resource_copy(struct pipe_resource *destination, unsigned dx, unsigned dy,
                       struct pipe_resource *source, const struct pipe_box *box)
{
    if (!source || !destination || !box_valid(box)) { errno = ENOTSUP; return -1; }
    struct ps2_resource *s = (struct ps2_resource *)source, *d = (struct ps2_resource *)destination;
    if (s->owner != d->owner) { errno = EXDEV; return -1; }
    return ps2_surface_copy(s->owner, s->handle, d->handle, box->x, box->y,
                            dx, dy, box->width, box->height);
}
