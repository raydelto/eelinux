/* SPDX-License-Identifier: MIT */
#include "ps2_solid.h"
#include "ps2_gs_logic.h"
#include "ps2_solid_batch.h"
#include "windowstr.h"
#include "regionstr.h"
#include "damage.h"
#include "os.h"
#include "miline.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_INPUTS 4096
#define MAX_CLIPS 256
#define MAX_RECTS 16384U
typedef struct ps2_pipe_solid_box rectangle;
struct plan {
    struct ps2_solid_screen *owner;
    DrawablePtr drawable;
    GCPtr gc;
    rectangle clips[MAX_CLIPS];
    struct ps2_pipe_solid_run *rects;
    unsigned char dashes[256];
    unsigned dash_count;
    uint64_t dash_phase;
    uint32_t background,ink;
    unsigned clips_count, count, capacity;
    uint32_t handle, color, mask;
    unsigned width, height, alu;
    int64_t tx, ty, ox, oy;
};
static DevPrivateKeyRec key;
static const GCOps operations;
static Bool close_screen(ScreenPtr);
static void fail(struct plan *p, const char *reason)
{
    if (p) free(p->rects);
    FatalError("PS2 GS: native solid GC operation failed: %s\n", reason);
}
static void begin(struct plan *p, DrawablePtr d, GCPtr gc, int count, const void *input,
                  Bool fill, Bool line)
{
    *p = (struct plan){ .drawable = d, .gc = gc };
    if (!d || !gc || d->pScreen != gc->pScreen || count < 0 || count > MAX_INPUTS ||
        (count && !input) || !gc->pCompositeClip) fail(p, "invalid drawable or request");
    struct ps2_solid_screen *b = dixLookupPrivate(&d->pScreen->devPrivates, &key);
    if (!b || !b->installed || !b->gc->installed || !b->gc->solid_ops ||
        b->gc->solid_ops != &operations) fail(p, "missing solid GC owner");
    p->owner = b;
    if (ps2_pipe_context_acquire(b->context)) fail(p, "context is closed or failed");
    ps2_pipe_context_release(b->context);
    if (gc->alu > GXset || (fill && gc->fillStyle != FillSolid) ||
        (line && (gc->lineStyle > LineDoubleDash || gc->lineWidth != 0 || gc->capStyle > CapProjecting ||
                  (gc->lineStyle != LineSolid && gc->fillStyle != FillSolid))))
        fail(p, "unsupported solid raster state");
    ScreenPtr s = d->pScreen;
    PixmapPtr pixmap = d->type == DRAWABLE_PIXMAP ? (PixmapPtr)d :
        d->type == DRAWABLE_WINDOW && s->GetWindowPixmap ? s->GetWindowPixmap((WindowPtr)d) : NULL;
    if (!pixmap || pixmap->drawable.pScreen != s || !s->GetScreenPixmap)
        fail(p, "missing drawable storage");
    if (pixmap != s->GetScreenPixmap(s)) {
        const void *private = exaGetPixmapDriverPrivate(pixmap);
        if (!private) fail(p, "missing owned pixmap");
        memcpy(&p->handle, private, sizeof(p->handle));
        if (!p->handle) {
            char reason[192];
            snprintf(reason, sizeof(reason),
                "unallocated pixmap depth=%u bpp=%u size=%ux%u GC-depth=%u fill=%u line=%u count=%d",
                d->depth, d->bitsPerPixel, d->width, d->height, gc->depth,
                (unsigned)fill, (unsigned)line, count);
            fail(p, reason);
        }
    }
    struct ps2_render_client *render = ps2_pipe_context_render(b->context);
    const struct ps2_surface *storage = render ? ps2_surface_lookup(render->gs, p->handle) : NULL;
    if (!storage || render->gs != b->gc->exa->gs ||
        !(render->gs->caps.features & PS2GS_HW_SOLID_LOGIC) ||
        pixmap->drawable.width != storage->width || pixmap->drawable.height != storage->height ||
        pixmap->drawable.bitsPerPixel != storage->bits_per_pixel ||
        d->bitsPerPixel != storage->bits_per_pixel ||
        d->depth != ps2_surface_depth(storage)) fail(p, "incompatible native storage");
    p->width = storage->width; p->height = storage->height; p->alu = gc->alu;
    uint32_t bits = ps2_surface_native_mask(render->gs,p->handle);
    p->color = gc->fgPixel & bits; p->mask = gc->planemask & bits;
    p->ink=p->color;p->background=gc->bgPixel&bits;
    if(line && gc->lineStyle!=LineSolid){
        if(!gc->dash || !gc->numInDashList || gc->numInDashList>256 || (gc->numInDashList&1))
            fail(p,"invalid or excessive normalized dash list");
        p->dash_count=gc->numInDashList;p->dash_phase=gc->dashOffset;
        for(unsigned i=0;i<p->dash_count;++i){
            p->dashes[i]=gc->dash[i];if(!p->dashes[i])fail(p,"zero dash length");
        }
    }
#ifdef COMPOSITE
    if (d->type == DRAWABLE_WINDOW) { p->ox = pixmap->screen_x; p->oy = pixmap->screen_y; }
#endif
    p->tx = (gc->miTranslate ? d->x : 0) - p->ox;
    p->ty = (gc->miTranslate ? d->y : 0) - p->oy;
    int n = RegionNumRects(gc->pCompositeClip);
    const BoxRec *boxes = RegionRects(gc->pCompositeClip);
    if (n < 0 || n > MAX_CLIPS) fail(p, "clip complexity exceeds native limit");
    for (int i = 0; i < n; ++i) {
        if (boxes[i].x2 < boxes[i].x1 || boxes[i].y2 < boxes[i].y1)
            fail(p, "invalid clip box");
        p->clips[i] = (rectangle){ boxes[i].x1-p->ox, boxes[i].y1-p->oy,
                                    boxes[i].x2-p->ox, boxes[i].y2-p->oy };
    }
    p->clips_count = n;
}
static int64_t max64(int64_t a, int64_t b) { return a > b ? a : b; }
static int64_t min64(int64_t a, int64_t b) { return a < b ? a : b; }
static void append(struct plan *p, int64_t x1, int64_t y1, int64_t x2, int64_t y2)
{
    if (x1 < INT32_MIN || y1 < INT32_MIN || x2 > INT32_MAX || y2 > INT32_MAX ||
        x2 < x1 || y2 < y1) fail(p, "coordinate overflow");
    if (x1 == x2 || y1 == y2) return;
    for (unsigned i = 0; i < p->clips_count; ++i) {
        rectangle c = p->clips[i];
        int64_t a = max64(max64(x1, c.x1), 0), b = max64(max64(y1, c.y1), 0);
        int64_t r = min64(min64(x2, c.x2), p->width), t = min64(min64(y2, c.y2), p->height);
        if (a >= r || b >= t) continue;
        if (p->count == MAX_RECTS) fail(p, "clipped rectangle budget exceeded");
        if (p->count == p->capacity) {
            unsigned capacity = p->capacity ? p->capacity*2 : 64;
            struct ps2_pipe_solid_run *rects = realloc(p->rects, capacity*sizeof(*rects));
            if (!rects) fail(p, "out of memory planning solid drawing");
            p->rects = rects; p->capacity = capacity;
        }
        p->rects[p->count++] = (struct ps2_pipe_solid_run){.box={a,b,r,t},.color=p->ink};
    }
}
static void finish(struct plan *p)
{
    if (!p->count || !p->mask || p->alu == GXnoop) { free(p->rects); return; }
    int status = ps2_pipe_solid_runs(p->owner->context,p->handle,
        p->rects,p->count,p->mask,p->alu);
    int error = errno;
    if (status) fail(p, strerror(error));
    free(p->rects);
    exaMarkSync(p->drawable->pScreen);
    DamageRegionAppend(p->drawable, p->gc->pCompositeClip);
    DamageRegionProcessPending(p->drawable);
}
/* Thin line segments exclude the last endpoint, unless include_last is set.
 * This avoids double-XOR at polyline joins and closed rectangle corners. */
static int line_run(void *data,const rectangle *r)
{ append(data,r->x1,r->y1,r->x2,r->y2);return 0; }
static int dash_run(void *data,const rectangle *r,unsigned background)
{
    struct plan *p=data;p->ink=background ? p->background : p->color;
    append(p,r->x1,r->y1,r->x2,r->y2);return 0;
}
static void segment(struct plan *p,int64_t x1,int64_t y1,int64_t x2,int64_t y2,Bool include_last)
{
    x1+=p->tx;y1+=p->ty;x2+=p->tx;y2+=p->ty;
    if(x1<INT32_MIN || x1>INT32_MAX || y1<INT32_MIN || y1>INT32_MAX ||
       x2<INT32_MIN || x2>INT32_MAX || y2<INT32_MIN || y2>INT32_MAX)
        fail(p,"line coordinate overflow");
    /* miZeroLine clips to the drawable before FillSpans/composite clipping and
     * saturates the upper drawable coordinates to INT16_MAX. Keep the original
     * endpoints for raster phase, then translate the bounds into backing storage. */
    int64_t left=p->gc->miTranslate ? p->drawable->x : 0;
    int64_t top=p->gc->miTranslate ? p->drawable->y : 0;
    int64_t right=min64(left+p->drawable->width-1,INT16_MAX)+1;
    int64_t bottom=min64(top+p->drawable->height-1,INT16_MAX)+1;
    rectangle bounds={max64(left-p->ox,0),max64(top-p->oy,0),
        min64(right-p->ox,p->width),min64(bottom-p->oy,p->height)};
    if(p->dash_count){
        struct ps2_pipe_dash_pattern dash={p->dashes,p->dash_count,p->dash_phase,
            p->gc->lineStyle==LineDoubleDash};
        int64_t dx=x2-x1,dy=y2-y1;
        uint64_t length=max64(dx<0 ? -dx : dx,dy<0 ? -dy : dy)+include_last;
        p->dash_phase+=length;
        if(bounds.x1>=bounds.x2 || bounds.y1>=bounds.y2)return;
        if(ps2_pipe_zero_dash(x1,y1,x2,y2,miGetZeroLineBias(p->drawable->pScreen)&255,
            include_last,&bounds,&dash,dash_run,p))fail(p,"dash planning failed");
        return;
    }
    if(bounds.x1>=bounds.x2 || bounds.y1>=bounds.y2) return;
    if(ps2_pipe_zero_line(x1,y1,x2,y2,miGetZeroLineBias(p->drawable->pScreen)&255,
                         include_last,&bounds,line_run,p)) fail(p,"line planning failed");
}
static void fill_spans(DrawablePtr d, GCPtr gc, int n, DDXPointPtr points, int *widths, int sorted)
{
    (void)sorted;
    struct plan p; begin(&p,d,gc,n,points,TRUE,FALSE);
    if (n && !widths) fail(&p,"missing span widths");
    for (int i=0;i<n;++i) {
        if (widths[i]<0) fail(&p,"negative span width");
        /* DDX FillSpans points are already translated by the caller. */
        append(&p,(int64_t)points[i].x-p.ox,(int64_t)points[i].y-p.oy,
            (int64_t)points[i].x+widths[i]-p.ox,(int64_t)points[i].y+1-p.oy);
    }
    finish(&p);
}
static void fill_rectangles(DrawablePtr d,GCPtr gc,int n,xRectangle *rects)
{
    struct plan p; begin(&p,d,gc,n,rects,TRUE,FALSE);
    for(int i=0;i<n;++i) append(&p,rects[i].x+p.tx,rects[i].y+p.ty,
        rects[i].x+p.tx+rects[i].width,rects[i].y+p.ty+rects[i].height);
    finish(&p);
}
static void check_mode(struct plan *p,int mode)
{ if(mode!=CoordModeOrigin && mode!=CoordModePrevious) fail(p,"invalid coordinate mode"); }
static void points(DrawablePtr d,GCPtr gc,int mode,int n,DDXPointPtr pts)
{
    struct plan p; begin(&p,d,gc,n,pts,FALSE,FALSE); check_mode(&p,mode);
    int64_t x=0,y=0;
    for(int i=0;i<n;++i) {
        x=(mode==CoordModePrevious && i ? x : 0)+pts[i].x;
        y=(mode==CoordModePrevious && i ? y : 0)+pts[i].y;
        append(&p,x+p.tx,y+p.ty,x+p.tx+1,y+p.ty+1);
    }
    finish(&p);
}
static void segments(DrawablePtr d,GCPtr gc,int n,xSegment *segs)
{
    struct plan p; begin(&p,d,gc,n,segs,FALSE,TRUE);
    for(int i=0;i<n;++i){
        p.dash_phase=gc->dashOffset;
        segment(&p,segs[i].x1,segs[i].y1,segs[i].x2,segs[i].y2,gc->capStyle!=CapNotLast);
    }
    finish(&p);
}
static void lines(DrawablePtr d,GCPtr gc,int mode,int n,DDXPointPtr pts)
{
    struct plan p; begin(&p,d,gc,n,pts,FALSE,TRUE); check_mode(&p,mode);
    if(n<2) { finish(&p); return; }
    int64_t x=pts[0].x,y=pts[0].y;
    for(int i=1;i<n;++i) {
        int64_t nx=(mode==CoordModePrevious ? x : 0)+pts[i].x;
        int64_t ny=(mode==CoordModePrevious ? y : 0)+pts[i].y;
        segment(&p,x,y,nx,ny,FALSE); x=nx; y=ny;
    }
    if(gc->capStyle!=CapNotLast && (n==2 || x!=pts[0].x || y!=pts[0].y))
        segment(&p,x,y,x,y,TRUE);
    finish(&p);
}
static void rectangles(DrawablePtr d,GCPtr gc,int n,xRectangle *rects)
{
    struct plan p; begin(&p,d,gc,n,rects,FALSE,TRUE);
    for(int i=0;i<n;++i) {
        p.dash_phase=gc->dashOffset;
        int64_t x=rects[i].x,y=rects[i].y;
        /* Match miPolyRectangle's zero-width path coordinate saturation. */
        int64_t r=min64(x+rects[i].width,INT16_MAX),b=min64(y+rects[i].height,INT16_MAX);
        /* A collapsed outline still visits its pixels once, including ends. */
        if(x==r || y==b) { segment(&p,x,y,r,b,TRUE); continue; }
        segment(&p,x,y,r,y,FALSE); segment(&p,r,y,r,b,FALSE);
        segment(&p,r,b,x,b,FALSE); segment(&p,x,b,x,y,FALSE);
    }
    finish(&p);
}
static const GCOps operations = {.FillSpans=fill_spans,.PolyPoint=points,.Polylines=lines,
    .PolySegment=segments,.PolyRectangle=rectangles,.PolyFillRect=fill_rectangles};
Bool ps2_solid_screen_prepare(ScreenPtr screen,struct ps2_solid_screen *b,struct ps2_gc_screen *gc)
{
    struct ps2_render_client *render=gc ? ps2_pipe_context_render(gc->context) : NULL;
    if(!screen || !b || b->installed || !gc || !gc->installed || gc->screen!=screen || gc->solid_ops ||
        !screen->CloseScreen || screen->CloseScreen==close_screen || !render ||
        render->gs!=gc->exa->gs || !(render->gs->caps.features & PS2GS_HW_SOLID_LOGIC) ||
        !dixRegisterPrivateKey(&key,PRIVATE_SCREEN,0) || dixLookupPrivate(&screen->devPrivates,&key) ||
        ps2_pipe_context_acquire(gc->context)) return FALSE;
    *b=(struct ps2_solid_screen){.gc=gc,.context=gc->context,.screen=screen,
        .CloseScreen=screen->CloseScreen,.installed=TRUE};
    dixSetPrivate(&screen->devPrivates,&key,b);
    screen->CloseScreen=close_screen;
    if(!ps2_gc_screen_set_solid_ops(gc,&operations)) FatalError("PS2 GS: solid GC installation failed\n");
    return TRUE;
}
void ps2_solid_screen_cleanup(struct ps2_solid_screen *b)
{
    if(!b || !b->installed) return;
    if(b->screen->CloseScreen!=close_screen || b->gc->solid_ops!=&operations ||
        !ps2_gc_screen_set_solid_ops(b->gc,NULL)) FatalError("PS2 GS: solid GC cleanup order\n");
    b->screen->CloseScreen=b->CloseScreen;
    dixSetPrivate(&b->screen->devPrivates,&key,NULL);
    struct pipe_context *context=b->context;
    memset(b,0,sizeof(*b));
    ps2_pipe_context_release(context);
}
static Bool close_screen(ScreenPtr screen)
{
    struct ps2_solid_screen *b=dixLookupPrivate(&screen->devPrivates,&key);
    if(!b || !b->installed) FatalError("PS2 GS: missing solid GC screen owner\n");
    CloseScreenProcPtr lower=b->CloseScreen;
    ps2_solid_screen_cleanup(b);
    return lower(screen);
}
