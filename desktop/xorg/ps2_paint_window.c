/* SPDX-License-Identifier: MIT */
#include "ps2_paint_window.h"
#include "ps2_solid_batch.h"
#include "ps2_tile.h"
#include "ps2_gs_logic.h"
#include "ps2_gs_render.h"
#include "windowstr.h"
#include "regionstr.h"
#include "damage.h"
#include "os.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static DevPrivateKeyRec key;
static Bool close_screen(ScreenPtr);
static void fail(const char *reason)
{ FatalError("PS2 GS: native PaintWindow failed: %s\n", reason); }
static struct ps2_paint_window *owner(ScreenPtr s)
{ return dixLookupPrivate(&s->devPrivates, &key); }
static Bool valid_region(RegionPtr r)
{
    if (!r || RegionBroken(r)) return FALSE;
    int n = RegionNumRects(r);
    if (n < 0) return FALSE;
    BoxPtr boxes = RegionRects(r);
    for (int i = 0; i < n; ++i)
        if (boxes[i].x1 >= boxes[i].x2 || boxes[i].y1 >= boxes[i].y2) return FALSE;
    return TRUE;
}
static WindowPtr background(WindowPtr w, ScreenPtr screen)
{
    WindowPtr slow = w, fast = w;
    while (w->backgroundState == ParentRelative) {
        w = w->parent;
        if (!w || w->drawable.pScreen != screen) fail("invalid background ancestor");
        /* Reject a corrupt parent cycle without bounding valid tree depth. */
        slow = slow ? slow->parent : NULL;
        fast = fast && fast->parent ? fast->parent->parent : NULL;
        if (slow && slow == fast) fail("background ancestor cycle");
    }
    return w;
}
static void paint_window(WindowPtr window, RegionPtr region, int what)
{
    ScreenPtr screen = window ? window->drawable.pScreen : NULL;
    struct ps2_paint_window *b = screen ? owner(screen) : NULL;
    if (!b || !b->installed || b->screen != screen ||
        (what != PW_BACKGROUND && what != PW_BORDER)) fail("invalid screen or paint selector");
    uint32_t color=0;
    PixmapPtr tile=NULL;
    WindowPtr aligned=window;
    if (what == PW_BACKGROUND) {
        WindowPtr ancestor = background(window, screen); aligned=ancestor;
#ifdef COMPOSITE
        if (ancestor->inhibitBGPaint) return;
#endif
        if (ancestor->backgroundState == None) return;
        if (ancestor->backgroundState == BackgroundPixmap) tile=ancestor->background.pixmap;
        else if (ancestor->backgroundState == BackgroundPixel) color=ancestor->background.pixel;
        else fail("invalid background state");
        if (ancestor->backgroundState == BackgroundPixmap && !tile) fail("missing background tile");
    } else {
        if (window->borderIsPixel) color=window->border.pixel;
        else {
            tile=window->border.pixmap; aligned=background(window,screen);
            if (!tile) fail("missing border tile");
        }
    }
    if (!valid_region(region) || (what == PW_BACKGROUND && !valid_region(&window->clipList)))
        fail("invalid paint region");
    struct ps2_render_client *render = ps2_pipe_context_render(b->context);
    if (!render || b->exa->gs != render->gs || ps2_pipe_context_acquire(b->context))
        fail("context is closed, failed or foreign");
    ps2_pipe_context_release(b->context);
    PixmapPtr pixmap = screen->GetWindowPixmap(window);
    if (!pixmap || pixmap->drawable.pScreen != screen) fail("missing window storage");
    uint32_t handle = 0;
    if (pixmap != screen->GetScreenPixmap(screen)) {
        const void *private = exaGetPixmapDriverPrivate(pixmap);
        if (!private) fail("missing owned pixmap");
        memcpy(&handle, private, sizeof(handle));
        if (!handle) fail("unallocated pixmap");
    }
    const struct ps2_surface *s = ps2_surface_lookup(b->exa->gs, handle);
    if (!s || (s->bits_per_pixel != 16 && s->bits_per_pixel != 32) ||
        pixmap->drawable.width != s->width || pixmap->drawable.height != s->height ||
        pixmap->drawable.bitsPerPixel != s->bits_per_pixel ||
        pixmap->drawable.depth != (s->bits_per_pixel == 16 ? 15 : 24) ||
        window->drawable.depth != pixmap->drawable.depth || window->drawable.bitsPerPixel != s->bits_per_pixel)
        fail("incompatible native storage");
    uint32_t tile_handle=0;
    if (tile) {
        if (tile->drawable.pScreen!=screen || tile==screen->GetScreenPixmap(screen) ||
            tile->drawable.depth!=pixmap->drawable.depth || tile->drawable.bitsPerPixel!=s->bits_per_pixel)
            fail("incompatible tile pixmap");
        const void *private=exaGetPixmapDriverPrivate(tile);
        if (!private) fail("missing owned tile");
        memcpy(&tile_handle,private,sizeof(tile_handle));
        const struct ps2_surface *t=ps2_surface_lookup(b->exa->gs,tile_handle);
        if (!tile_handle || !t || t->width!=tile->drawable.width || t->height!=tile->drawable.height ||
            t->bits_per_pixel!=s->bits_per_pixel) fail("stale or incompatible tile storage");
    }
    int ox = 0, oy = 0;
#ifdef COMPOSITE
    ox = pixmap->screen_x; oy = pixmap->screen_y;
#endif
    if (ox < SHRT_MIN || oy < SHRT_MIN || ox + (int)s->width > SHRT_MAX || oy + (int)s->height > SHRT_MAX)
        fail("backing pixmap bounds overflow");
    BoxRec box = {ox, oy, ox + s->width, oy + s->height};
    RegionRec bounds, clipped;
    RegionInit(&bounds, &box, 1); RegionNull(&clipped);
    const char *error = NULL;
    struct ps2_pipe_solid_box *jobs = NULL;
    if (!RegionIntersect(&clipped, region, &bounds) ||
        (what == PW_BACKGROUND && !RegionIntersect(&clipped, &clipped, &window->clipList))) {
        error = "paint region allocation"; goto out;
    }
    if (!valid_region(&clipped)) { error = "invalid clipped region"; goto out; }
    int n = RegionNumRects(&clipped);
    if (!n) goto out;
    unsigned capacity=(unsigned)n<PS2_PAINT_BATCH_BOXES?(unsigned)n:PS2_PAINT_BATCH_BOXES;
    jobs = calloc(capacity, sizeof(*jobs));
    if (!jobs) { error = "paint plan allocation"; goto out; }
    BoxPtr boxes = RegionRects(&clipped);
    uint32_t mask = s->bits_per_pixel == 16 ? 0x7fff : 0xffffff;
    /* Xaw shaped widgets can produce hundreds of valid clip rectangles.
     * Bound temporary storage and each GS submission, rather than rejecting
     * a complete server-owned region because it exceeds one batch. */
    for(int offset=0;offset<n;){
        unsigned count=(unsigned)(n-offset);if(count>capacity)count=capacity;
        for(unsigned i=0;i<count;++i){
            const BoxRec *box=&boxes[offset+i];
            jobs[i]=(struct ps2_pipe_solid_box){box->x1-ox,box->y1-oy,box->x2-ox,box->y2-oy};
        }
        int status=tile ? ps2_pipe_tile_rectangles(b->context,tile_handle,handle,jobs,count,
            (int)aligned->drawable.x-ox,(int)aligned->drawable.y-oy) :
            ps2_pipe_solid_rectangles(b->context,handle,jobs,count,color & mask,mask,GXcopy);
        if(status){error=strerror(errno);goto out;}
        offset+=(int)count;
    }
    exaMarkSync(screen);
    DrawablePtr drawable = &window->drawable;
    if (what == PW_BORDER) {
        /* Damage takes pixmap-local coordinates for a redirected border. */
        RegionTranslate(&clipped, -ox, -oy); drawable = &pixmap->drawable;
    }
    DamageRegionAppend(drawable, &clipped); DamageRegionProcessPending(drawable);
out:
    free(jobs); RegionUninit(&clipped); RegionUninit(&bounds);
    if (error) fail(error);
}
Bool ps2_paint_window_prepare(ScreenPtr screen, struct ps2_paint_window *b,
    struct ps2_surface_exa *exa, struct pipe_context *context)
{
    struct ps2_render_client *r = ps2_pipe_context_render(context);
    if (!screen || !b || b->installed || !exa || !r || r->gs != exa->gs ||
        !(r->gs->caps.features & PS2GS_HW_SOLID_LOGIC) || !screen->GetWindowPixmap || !screen->GetScreenPixmap ||
        !screen->PaintWindow || screen->PaintWindow == paint_window || !screen->CloseScreen ||
        !dixRegisterPrivateKey(&key, PRIVATE_SCREEN, 0) || owner(screen) || ps2_pipe_context_acquire(context)) return FALSE;
    *b = (struct ps2_paint_window){.screen = screen, .exa = exa, .context = context,
        .PaintWindow = screen->PaintWindow, .CloseScreen = screen->CloseScreen, .installed = TRUE};
    dixSetPrivate(&screen->devPrivates, &key, b);
    screen->PaintWindow = paint_window; screen->CloseScreen = close_screen; return TRUE;
}
void ps2_paint_window_cleanup(struct ps2_paint_window *b)
{
    if (!b || !b->installed) return;
    if (owner(b->screen) != b || b->screen->PaintWindow != paint_window || b->screen->CloseScreen != close_screen)
        fail("callback cleanup order");
    b->screen->PaintWindow = b->PaintWindow; b->screen->CloseScreen = b->CloseScreen;
    dixSetPrivate(&b->screen->devPrivates, &key, NULL);
    struct pipe_context *context = b->context;
    memset(b, 0, sizeof(*b)); ps2_pipe_context_release(context);
}
static Bool close_screen(ScreenPtr screen)
{
    struct ps2_paint_window *b = owner(screen);
    if (!b || !b->installed) fail("missing screen at close");
    CloseScreenProcPtr lower = b->CloseScreen;
    ps2_paint_window_cleanup(b); return lower(screen);
}
