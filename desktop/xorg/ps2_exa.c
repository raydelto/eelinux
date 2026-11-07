/* SPDX-License-Identifier: MIT */
#include "ps2_exa.h"
#include "os.h"
#include <errno.h>
#include <string.h>

static DevPrivateKeyRec ps2_exa_key;

static struct ps2_exa *backend(PixmapPtr pixmap)
{
    return dixLookupPrivate(&pixmap->drawable.pScreen->devPrivates, &ps2_exa_key);
}

static Bool front(PixmapPtr pixmap)
{
    ScreenPtr screen = pixmap->drawable.pScreen;
    struct ps2_exa *ps2 = backend(pixmap);
    return ps2 && pixmap == screen->GetScreenPixmap(screen) &&
        pixmap->drawable.width == ps2->gs->caps.width &&
        pixmap->drawable.height == ps2->gs->caps.height &&
        pixmap->drawable.bitsPerPixel == ps2->gs->caps.bits_per_pixel &&
        pixmap->drawable.depth == (ps2->gs->caps.bits_per_pixel == 16 ? 15 : 24);
}

static void result(struct ps2_exa *ps2, int status)
{
    if (status) FatalError("PS2 GS drawing failed: %s; no CPU raster fallback\n", strerror(ps2->gs->error));
}

static Bool prepare_solid(PixmapPtr pixmap, int alu, Pixel mask, Pixel color)
{
    if (!front(pixmap) || alu != GXcopy ||
        (mask & ((1UL << pixmap->drawable.depth) - 1)) != ((1UL << pixmap->drawable.depth) - 1))
        return FALSE;
    struct ps2_exa *ps2 = backend(pixmap);
    ps2->color = ps2_gs_pixel_color(color, ps2->gs->caps.bits_per_pixel);
    return TRUE;
}

static void solid(PixmapPtr pixmap, int x1, int y1, int x2, int y2)
{
    struct ps2_exa *ps2 = backend(pixmap);
    if (x2 <= x1 || y2 <= y1) return;
    if (x1 < 0 || y1 < 0) FatalError("PS2 GS: solid rectangle was not clipped\n");
    result(ps2, ps2_gs_fill(ps2->gs, x1, y1, x2 - x1, y2 - y1, ps2->color));
}

static void done(PixmapPtr pixmap)
{
    struct ps2_exa *ps2 = backend(pixmap);
    result(ps2, ps2_gs_flush(ps2->gs));
}

static Bool prepare_copy(PixmapPtr source, PixmapPtr destination, int xdir, int ydir, int alu, Pixel mask)
{
    (void)xdir; (void)ydir;
    return source->drawable.pScreen == destination->drawable.pScreen &&
        front(source) && front(destination) && alu == GXcopy &&
        (mask & ((1UL << destination->drawable.depth) - 1)) == ((1UL << destination->drawable.depth) - 1);
}

static void copy(PixmapPtr pixmap, int sx, int sy, int dx, int dy, int width, int height)
{
    struct ps2_exa *ps2 = backend(pixmap);
    if (width <= 0 || height <= 0) return;
    if (sx < 0 || sy < 0 || dx < 0 || dy < 0) FatalError("PS2 GS: copy rectangle was not clipped\n");
    result(ps2, ps2_gs_copy(ps2->gs, sx, sy, dx, dy, width, height));
}

static Bool prepare_access(PixmapPtr pixmap, int index)
{
    (void)pixmap; (void)index;
    /* EXA otherwise silently invokes its CPU renderer for unsupported ops. */
    FatalError("PS2 GS: unsupported X drawing operation; CPU rasterization is disabled\n");
    return FALSE;
}

static Bool upload(PixmapPtr pixmap, int x, int y, int width, int height,
                   char *source, int pitch)
{
    if (!front(pixmap)) return FALSE;
    struct ps2_exa *ps2 = backend(pixmap);
    if (!(ps2->gs->caps.features & PS2GS_HW_UPLOAD)) return FALSE;
    if (width <= 0 || height <= 0) return TRUE;
    if (x < 0 || y < 0 || !source || pitch <= 0 ||
        (unsigned)width > ps2->gs->caps.width ||
        (unsigned)height > ps2->gs->caps.height)
        FatalError("PS2 GS: invalid X image upload\n");
    size_t row = (size_t)width * (ps2->gs->caps.bits_per_pixel / 8);
    if ((size_t)pitch < row || (size_t)(height - 1) > (SIZE_MAX - row) / (size_t)pitch)
        FatalError("PS2 GS: invalid X image stride\n");
    result(ps2, ps2_gs_upload(ps2->gs, x, y, width, height, source,
        (size_t)(height - 1) * pitch + row, pitch));
    return TRUE;
}

static void wait_marker(ScreenPtr screen, int marker)
{
    (void)marker; /* Submissions are fenced synchronously in the GS. */
    struct ps2_exa *ps2 = dixLookupPrivate(&screen->devPrivates, &ps2_exa_key);
    result(ps2, ps2_gs_flush(ps2->gs));
}

Bool ps2_exa_prepare(ScreenPtr screen, struct ps2_exa *ps2, struct ps2_gs_client *gs)
{
    if (!dixRegisterPrivateKey(&ps2_exa_key, PRIVATE_SCREEN, 0)) return FALSE;
    memset(ps2, 0, sizeof(*ps2));
    ps2->gs = gs;
    ps2->driver.exa_major = EXA_VERSION_MAJOR;
    ps2->driver.exa_minor = EXA_VERSION_MINOR;
    ps2->driver.maxX = gs->caps.width;
    ps2->driver.maxY = gs->caps.height;
    ps2->driver.PrepareSolid = prepare_solid;
    ps2->driver.Solid = solid;
    ps2->driver.DoneSolid = done;
    ps2->driver.PrepareCopy = prepare_copy;
    ps2->driver.Copy = copy;
    ps2->driver.DoneCopy = done;
    ps2->driver.PrepareAccess = prepare_access;
    ps2->driver.UploadToScreen = upload;
    ps2->driver.WaitMarker = wait_marker;
    dixSetPrivate(&screen->devPrivates, &ps2_exa_key, ps2);
    return TRUE;
}
