/* SPDX-License-Identifier: MIT */
#include "ps2_render_bridge.h"
#include "os.h"
#include <errno.h>
#include <string.h>
static DevPrivateKeyRec bridge_key;
static struct ps2_exa_render_bridge *bridge(ScreenPtr screen)
{ return dixLookupPrivate(&screen->devPrivates, &bridge_key); }
static void fence(struct ps2_exa_render_bridge *b)
{
    if (ps2_render_flush(b->render))
        FatalError("PS2 GS: shared Xorg/Mesa fence failed: %s\n", strerror(b->render->gs->error));
}
static void *create(ScreenPtr screen, int width, int height, int depth, int usage, int bpp, int *pitch)
{
    struct ps2_exa_render_bridge *b = bridge(screen); fence(b);
    return b->original.CreatePixmap2(screen, width, height, depth, usage, bpp, pitch);
}
static void destroy(ScreenPtr screen, void *private)
{
    struct ps2_exa_render_bridge *b = bridge(screen);
    /* Cleanup after a failed context cannot fence or replay it. The owning
     * file's final close releases any kernel allocations left by failure. */
    if (!b->render->gs->error) fence(b);
    b->original.DestroyPixmap(screen, private);
}
static Bool header(PixmapPtr pixmap, int w, int h, int depth, int bpp, int pitch, void *pixels)
{
    struct ps2_exa_render_bridge *b = bridge(pixmap->drawable.pScreen); fence(b);
    return b->original.ModifyPixmapHeader(pixmap, w, h, depth, bpp, pitch, pixels);
}
static Bool prepare_solid(PixmapPtr p, int alu, Pixel mask, Pixel color)
{
    struct ps2_exa_render_bridge *b = bridge(p->drawable.pScreen); fence(b);
    return b->original.PrepareSolid(p, alu, mask, color);
}
static void solid(PixmapPtr p, int x1, int y1, int x2, int y2)
{
    struct ps2_exa_render_bridge *b = bridge(p->drawable.pScreen); fence(b);
    b->original.Solid(p, x1, y1, x2, y2);
}
static Bool prepare_copy(PixmapPtr source, PixmapPtr destination, int xd, int yd, int alu, Pixel mask)
{
    struct ps2_exa_render_bridge *b = bridge(destination->drawable.pScreen); fence(b);
    return b->original.PrepareCopy(source, destination, xd, yd, alu, mask);
}
static void copy(PixmapPtr p, int sx, int sy, int dx, int dy, int w, int h)
{
    struct ps2_exa_render_bridge *b = bridge(p->drawable.pScreen); fence(b);
    b->original.Copy(p, sx, sy, dx, dy, w, h);
}
static Bool upload(PixmapPtr p, int x, int y, int w, int h, char *pixels, int pitch)
{
    struct ps2_exa_render_bridge *b = bridge(p->drawable.pScreen); fence(b);
    return b->original.UploadToScreen(p, x, y, w, h, pixels, pitch);
}
Bool ps2_exa_render_bridge_prepare(ScreenPtr screen, struct ps2_exa_render_bridge *b,
                                   struct ps2_surface_exa *exa, struct ps2_render_client *render)
{
    if (!screen || !b || !exa || !render || render->gs != exa->gs || render->gs->error ||
        !exa->driver.CreatePixmap2 || !exa->driver.DestroyPixmap || !exa->driver.ModifyPixmapHeader ||
        !exa->driver.PrepareSolid || !exa->driver.Solid || !exa->driver.PrepareCopy ||
        !exa->driver.Copy || !exa->driver.UploadToScreen || exa->driver.CreatePixmap2 == create)
        return FALSE;
    if (!dixRegisterPrivateKey(&bridge_key, PRIVATE_SCREEN, 0)) return FALSE;
    memset(b, 0, sizeof(*b)); b->exa = exa; b->render = render; b->original = exa->driver;
    dixSetPrivate(&screen->devPrivates, &bridge_key, b);
    exa->driver.CreatePixmap2 = create; exa->driver.DestroyPixmap = destroy;
    exa->driver.ModifyPixmapHeader = header; exa->driver.PrepareSolid = prepare_solid;
    exa->driver.Solid = solid; exa->driver.PrepareCopy = prepare_copy; exa->driver.Copy = copy;
    exa->driver.UploadToScreen = upload;
    return TRUE;
}
