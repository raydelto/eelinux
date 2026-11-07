/* SPDX-License-Identifier: MIT */
#include "ps2_copy_exa.h"
#include "ps2_rop.h"
#include "os.h"
#include <errno.h>
#include <string.h>
static DevPrivateKeyRec key;
static struct ps2_copy_exa *backend(ScreenPtr screen)
{ return dixLookupPrivate(&screen->devPrivates, &key); }
static const struct ps2_surface *storage(PixmapPtr pixmap, struct ps2_copy_exa *b)
{
    if (!pixmap || pixmap->drawable.pScreen != b->screen || b->exa->gs != b->gs) return NULL;
    uint32_t handle = 0;
    if (pixmap != b->screen->GetScreenPixmap(b->screen)) {
        const uint32_t *private = exaGetPixmapDriverPrivate(pixmap);
        if (!private || !*private) return NULL;
        handle = *private;
    }
    const struct ps2_surface *s = ps2_surface_lookup(b->gs, handle);
    if (!s || (s->bits_per_pixel != 16 && s->bits_per_pixel != 32) ||
        pixmap->drawable.width != s->width || pixmap->drawable.height != s->height ||
        pixmap->drawable.bitsPerPixel != s->bits_per_pixel ||
        pixmap->drawable.depth != ps2_surface_depth(s)) return NULL;
    return s;
}
static Bool same_storage(const struct ps2_surface *a, const struct ps2_surface *b)
{
    return a && a->handle == b->handle && a->width == b->width &&
        a->height == b->height && a->bits_per_pixel == b->bits_per_pixel &&
        a->buffer_width == b->buffer_width && a->bytes == b->bytes && a->pixel_depth==b->pixel_depth;
}
static void live(struct ps2_copy_exa *b)
{
    if (ps2_pipe_context_acquire(b->context))
        FatalError("PS2 GS: EXA copy context is closed or failed\n");
    ps2_pipe_context_release(b->context);
}
static Bool prepare(PixmapPtr source, PixmapPtr destination,
    int xdir, int ydir, int alu, Pixel mask)
{
    if (!destination || !destination->drawable.pScreen) return FALSE;
    struct ps2_copy_exa *b = backend(destination->drawable.pScreen);
    const struct ps2_surface *s = b && b->attached ? storage(source, b) : NULL;
    const struct ps2_surface *d = s ? storage(destination, b) : NULL;
    if (!d || b->prepared || s->bits_per_pixel != d->bits_per_pixel ||
        (xdir != -1 && xdir != 1) || (ydir != -1 && ydir != 1) ||
        alu < GXclear || alu > GXset) return FALSE;
    uint32_t bits = ps2_surface_native_mask(b->gs,d->handle);
    uint32_t rgb_mask = mask & bits;
    Bool noop = alu == GXnoop || !rgb_mask;
    if (!noop && (alu != GXcopy || rgb_mask != bits) &&
        !(b->gs->caps.features&PS2GS_HW_TEXTURE_TRANSFER)) return FALSE;
    /* PrepareCopy has no boxes yet. Do not flush queued Mesa work here. */
    live(b);
    b->source_pixmap = source; b->destination_pixmap = destination;
    b->source = *s; b->destination = *d;
    b->plane_mask = rgb_mask; b->alu = alu;
    b->noop = noop; b->prepared = TRUE;
    return TRUE;
}
static void copy(PixmapPtr destination, int sx, int sy, int dx, int dy, int width, int height)
{
    struct ps2_copy_exa *b = destination && destination->drawable.pScreen ?
        backend(destination->drawable.pScreen) : NULL;
    if (!b || !b->attached || !b->prepared || destination != b->destination_pixmap ||
        !same_storage(storage(b->source_pixmap, b), &b->source) ||
        !same_storage(storage(destination, b), &b->destination) ||
        sx < 0 || sy < 0 || dx < 0 || dy < 0 || width < 0 || height < 0 ||
        (int64_t)sx + width > b->source.width || (int64_t)sy + height > b->source.height ||
        (int64_t)dx + width > b->destination.width || (int64_t)dy + height > b->destination.height)
        FatalError("PS2 GS: invalid prepared EXA copy rectangle\n");
    live(b);
    if (b->noop || !width || !height) return;
    const struct ps2_pipe_copy_box box = { sx, sy, dx, dy, width, height };
    int status=b->destination.pixel_depth!=32 && b->alu==GXcopy && b->plane_mask==(b->destination.bits_per_pixel==16?0x7fffU:0xffffffU) ?
        ps2_pipe_copy_boxes(b->context,b->source.handle,b->destination.handle,&box,1) :
        ps2_pipe_rop_copy_boxes(b->context,b->source.handle,b->destination.handle,&box,1,b->plane_mask,b->alu);
    if (status)
        FatalError("PS2 GS: EXA copy failed: %s\n", strerror(errno));
}
static void done(PixmapPtr destination)
{
    struct ps2_copy_exa *b = destination && destination->drawable.pScreen ?
        backend(destination->drawable.pScreen) : NULL;
    if (!b || !b->attached || !b->prepared || destination != b->destination_pixmap)
        FatalError("PS2 GS: EXA copy completion order\n");
    b->prepared = FALSE; b->noop = FALSE;
    b->source_pixmap = NULL; b->destination_pixmap = NULL;
}
Bool ps2_copy_exa_prepare(ScreenPtr screen, struct ps2_copy_exa *b,
    struct ps2_surface_exa *exa, struct pipe_context *context)
{
    struct ps2_render_client *render = ps2_pipe_context_render(context);
    if (!screen || !screen->GetScreenPixmap || !b || b->attached || !exa || !render ||
        render->gs != exa->gs || !(exa->gs->caps.features & PS2GS_HW_COPY) ||
        !exa->driver.PrepareCopy || !exa->driver.Copy || !exa->driver.DoneCopy ||
        exa->driver.Copy == copy || !dixRegisterPrivateKey(&key, PRIVATE_SCREEN, 0) ||
        backend(screen) || ps2_pipe_context_acquire(context)) return FALSE;
    *b = (struct ps2_copy_exa){ .screen = screen, .exa = exa, .gs = exa->gs, .context = context,
        .PrepareCopy = exa->driver.PrepareCopy, .Copy = exa->driver.Copy,
        .DoneCopy = exa->driver.DoneCopy, .attached = TRUE };
    dixSetPrivate(&screen->devPrivates, &key, b);
    exa->driver.PrepareCopy = prepare; exa->driver.Copy = copy; exa->driver.DoneCopy = done;
    return TRUE;
}
void ps2_copy_exa_cleanup(ScreenPtr screen, struct ps2_copy_exa *b)
{
    if (!b || !b->attached) return;
    if (!screen || backend(screen) != b || b->exa->driver.PrepareCopy != prepare ||
        b->exa->driver.Copy != copy || b->exa->driver.DoneCopy != done)
        FatalError("PS2 GS: EXA copy cleanup order\n");
    b->exa->driver.PrepareCopy = b->PrepareCopy;
    b->exa->driver.Copy = b->Copy; b->exa->driver.DoneCopy = b->DoneCopy;
    dixSetPrivate(&screen->devPrivates, &key, NULL);
    struct pipe_context *context = b->context;
    memset(b, 0, sizeof(*b));
    ps2_pipe_context_release(context);
}
