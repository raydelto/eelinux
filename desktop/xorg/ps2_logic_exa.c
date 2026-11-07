/* SPDX-License-Identifier: MIT */
#include "ps2_logic_exa.h"
#include "os.h"
#include <errno.h>
#include <string.h>
static DevPrivateKeyRec key;
static struct ps2_logic_exa *backend(ScreenPtr screen)
{ return dixLookupPrivate(&screen->devPrivates, &key); }
static const struct ps2_surface *storage(PixmapPtr pixmap, struct ps2_logic_exa *b)
{
    uint32_t handle = 0;
    if (pixmap != pixmap->drawable.pScreen->GetScreenPixmap(pixmap->drawable.pScreen)) {
        /* The existing owned EXA private is an opaque handle, never pixels. */
        const uint32_t *private = exaGetPixmapDriverPrivate(pixmap);
        if (!private || !*private) return NULL;
        handle = *private;
    }
    const struct ps2_surface *s = ps2_surface_lookup(b->exa->gs, handle);
    if (!s || pixmap->drawable.width != s->width || pixmap->drawable.height != s->height ||
        pixmap->drawable.bitsPerPixel != s->bits_per_pixel ||
        pixmap->drawable.depth != ps2_surface_depth(s)) return NULL;
    return s;
}
static Bool prepare(PixmapPtr pixmap, int alu, Pixel mask, Pixel color)
{
    struct ps2_logic_exa *b = backend(pixmap->drawable.pScreen);
    if (!b || !b->attached || alu < 0 || alu > 15 || !storage(pixmap, b)) return FALSE;
    if (ps2_pipe_context_acquire(b->context))
        FatalError("PS2 GS: solid logic context is closed or failed\n");
    ps2_pipe_context_release(b->context);
    uint32_t bits = ps2_surface_native_mask(b->exa->gs,storage(pixmap,b)->handle);
    b->color = color & bits; b->plane_mask = mask & bits; b->alu = alu;
    return TRUE;
}
static void solid(PixmapPtr pixmap, int x1, int y1, int x2, int y2)
{
    struct ps2_logic_exa *b = backend(pixmap->drawable.pScreen);
    const struct ps2_surface *s = b ? storage(pixmap, b) : NULL;
    if (!s || x1 < 0 || y1 < 0 || x2 < x1 || y2 < y1 ||
        (unsigned)x2 > s->width || (unsigned)y2 > s->height)
        FatalError("PS2 GS: invalid clipped solid logic rectangle\n");
    if (x1 == x2 || y1 == y2) return;
    if (ps2_pipe_context_acquire(b->context))
        FatalError("PS2 GS: solid logic context is closed or failed\n");
    struct ps2_render_client *render = ps2_pipe_context_render(b->context);
    int status = ps2_render_flush(render);
    if (!status) status = ps2_surface_solid_logic(b->exa->gs, s->handle,
        x1, y1, x2-x1, y2-y1, b->color, b->plane_mask, b->alu);
    ps2_pipe_context_release(b->context);
    if (status) FatalError("PS2 GS: solid logic failed: %s\n", strerror(errno));
}
Bool ps2_logic_exa_prepare(ScreenPtr screen, struct ps2_logic_exa *b,
    struct ps2_surface_exa *exa, struct pipe_context *context)
{
    struct ps2_render_client *render = ps2_pipe_context_render(context);
    if (!screen || !b || b->attached || !exa || !render || render->gs != exa->gs ||
        !(exa->gs->caps.features & PS2GS_HW_SOLID_LOGIC) ||
        !exa->driver.PrepareSolid || !exa->driver.Solid || exa->driver.Solid == solid ||
        !dixRegisterPrivateKey(&key, PRIVATE_SCREEN, 0) ||
        dixLookupPrivate(&screen->devPrivates, &key) || ps2_pipe_context_acquire(context)) return FALSE;
    *b = (struct ps2_logic_exa){ .exa = exa, .context = context,
        .PrepareSolid = exa->driver.PrepareSolid, .Solid = exa->driver.Solid, .attached = TRUE };
    dixSetPrivate(&screen->devPrivates, &key, b);
    exa->driver.PrepareSolid = prepare; exa->driver.Solid = solid;
    return TRUE;
}
void ps2_logic_exa_cleanup(ScreenPtr screen, struct ps2_logic_exa *b)
{
    if (!b || !b->attached) return;
    if (!screen || backend(screen) != b || b->exa->driver.PrepareSolid != prepare ||
        b->exa->driver.Solid != solid) FatalError("PS2 GS: solid logic cleanup order\n");
    b->exa->driver.PrepareSolid = b->PrepareSolid; b->exa->driver.Solid = b->Solid;
    dixSetPrivate(&screen->devPrivates, &key, NULL);
    struct pipe_context *context = b->context;
    memset(b, 0, sizeof(*b));
    ps2_pipe_context_release(context);
}
