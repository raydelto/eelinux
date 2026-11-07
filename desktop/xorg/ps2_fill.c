/* SPDX-License-Identifier: MIT */
#include "ps2_fill.h"
#include "ps2_tile.h"
#include "ps2_gs_logic.h"
#include "ps2_rop.h"
#include "windowstr.h"
#include "regionstr.h"
#include "damage.h"
#include "os.h"
#include "servermd.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define MAX_INPUTS 4096
#if BITMAP_BIT_ORDER == MSBFirst
#define SERVER_MSB_FIRST 1
#else
#define SERVER_MSB_FIRST 0
#endif
static DevPrivateKeyRec key;
static const GCOps operations;
static Bool close_screen(ScreenPtr);
struct plan {
    struct ps2_fill_screen *owner;
    DrawablePtr drawable;
    GCPtr gc;
    struct ps2_pipe_solid_box *boxes;
    unsigned count, width, height;
    unsigned planning, planned, emitted;
    uint64_t batch_pixels;
    uint32_t source, destination, mask, color;
    uint32_t background;
    struct ps2_pipe_mono_pattern mono;
    int64_t tx, ty, ox, oy, pattern_x, pattern_y;
};
static void fail(struct plan *p, const char *reason)
{
    if (p) free(p->boxes);
    FatalError("PS2 GS: native tiled GC fill failed: %s\n", reason);
}
static struct ps2_fill_screen *owner(DrawablePtr d, GCPtr gc)
{
    if (!d || !gc || !d->pScreen || d->pScreen != gc->pScreen) fail(NULL,"invalid drawable or GC");
    struct ps2_fill_screen *b = dixLookupPrivate(&d->pScreen->devPrivates,&key);
    if (!b || !b->installed || !b->gc->installed || b->gc->fill_ops != &operations ||
        !b->gc->solid_ops) fail(NULL,"missing fill/solid GC owner");
    return b;
}
static void begin(struct plan *p, DrawablePtr d, GCPtr gc, int count, const void *input)
{
    *p = (struct plan){.owner=owner(d,gc),.drawable=d,.gc=gc,.planning=1};
    if (count<0 || count>MAX_INPUTS || (count && !input) || !gc->pCompositeClip ||
        RegionBroken(gc->pCompositeClip) ||
        (gc->fillStyle != FillTiled && gc->fillStyle != FillOpaqueStippled &&
         gc->fillStyle != FillStippled) || gc->alu>GXset)
        fail(p,"invalid tiled fill request");
    if (ps2_pipe_context_acquire(p->owner->context)) fail(p,"context is closed or failed");
    ps2_pipe_context_release(p->owner->context);
    ScreenPtr screen=d->pScreen;
    PixmapPtr pixmap=d->type==DRAWABLE_PIXMAP ? (PixmapPtr)d :
        d->type==DRAWABLE_WINDOW && screen->GetWindowPixmap ? screen->GetWindowPixmap((WindowPtr)d) : NULL;
    if (!pixmap || pixmap->drawable.pScreen!=screen || !screen->GetScreenPixmap)
        fail(p,"missing destination storage");
    if (pixmap!=screen->GetScreenPixmap(screen)) {
        const void *private=exaGetPixmapDriverPrivate(pixmap);
        if (!private) fail(p,"missing owned destination");
        memcpy(&p->destination,private,4);
        if (!p->destination) fail(p,"unallocated destination");
    }
    struct ps2_render_client *render=ps2_pipe_context_render(p->owner->context);
    const struct ps2_surface *s=ps2_surface_lookup(render->gs,p->destination);
    if (!s || render->gs!=p->owner->gc->exa->gs ||
        (s->bits_per_pixel!=16 && s->bits_per_pixel!=32) ||
        pixmap->drawable.width!=s->width || pixmap->drawable.height!=s->height ||
        pixmap->drawable.depth!=d->depth || pixmap->drawable.bitsPerPixel!=s->bits_per_pixel ||
        d->bitsPerPixel!=s->bits_per_pixel || d->depth!=ps2_surface_depth(s))
        fail(p,"incompatible destination storage");
    p->width=s->width; p->height=s->height;
    uint32_t bits=ps2_surface_native_mask(render->gs,p->destination);
    p->mask=gc->planemask&bits; p->color=gc->tile.pixel&bits;
    if (gc->fillStyle==FillOpaqueStippled || gc->fillStyle==FillStippled) {
        PixmapPtr mono=gc->stipple;
        if (!mono || mono->drawable.pScreen!=screen) fail(p,"missing or foreign monochrome stipple");
        struct ps2_mono_source owned={0};
        int mono_owned=ps2_surface_exa_mono_source(mono,&owned);
        if (mono_owned<0) fail(p,"invalid owned monochrome stipple header");
        const unsigned char *source_bits=mono_owned ? owned.bits : mono ? mono->devPrivate.ptr : NULL;
        if (!mono || mono->drawable.pScreen!=screen || mono->drawable.type!=DRAWABLE_PIXMAP ||
            mono->drawable.depth!=1 || mono->drawable.bitsPerPixel!=1 || !source_bits ||
            !mono->drawable.width || !mono->drawable.height || mono->drawable.width>256 ||
            mono->drawable.height>256 || mono->devKind<=0 || mono->devKind<(mono->drawable.width+7)/8)
            fail(p,"invalid server monochrome stipple");
        size_t row=(mono->drawable.width+7)/8, stride=mono->devKind;
        if ((size_t)mono->drawable.height-1>(SIZE_MAX-row)/stride) fail(p,"stipple stride overflow");
        size_t bytes=(mono->drawable.height-1)*stride+row;
        if ((uintptr_t)source_bits>UINTPTR_MAX-(bytes-1)) fail(p,"stipple address overflow");
        p->mono=(struct ps2_pipe_mono_pattern){.bits=source_bits,
            .stride=stride,.bytes=bytes,
            .width=mono->drawable.width,.height=mono->drawable.height,.msb_first=SERVER_MSB_FIRST};
        p->color=gc->fgPixel&bits; p->background=gc->bgPixel&bits;
    } else if (!gc->tileIsPixel) {
        if (p->mask && gc->alu!=GXnoop && (gc->alu!=GXcopy || p->mask!=bits) &&
            !(render->gs->caps.features&PS2GS_HW_TEXTURE_TRANSFER))
            fail(p,"kernel lacks native Boolean texture transfers");
        PixmapPtr tile=gc->tile.pixmap;
        if (!tile || tile==screen->GetScreenPixmap(screen) || tile->drawable.pScreen!=screen ||
            tile->drawable.depth!=d->depth || tile->drawable.bitsPerPixel!=s->bits_per_pixel)
            fail(p,"incompatible tile pixmap");
        const void *private=exaGetPixmapDriverPrivate(tile);
        if (!private) fail(p,"missing owned tile");
        memcpy(&p->source,private,4);
        const struct ps2_surface *t=ps2_surface_lookup(render->gs,p->source);
        if (!p->source || p->source==p->destination || !t || !t->width || !t->height ||
            t->width>1024 || t->height>1024 || tile->drawable.width!=t->width ||
            tile->drawable.height!=t->height || t->bits_per_pixel!=s->bits_per_pixel ||
            !(render->gs->caps.features&PS2GS_HW_TEXTURE)) fail(p,"stale, aliased or unsupported tile");
    }
#ifdef COMPOSITE
    if (d->type==DRAWABLE_WINDOW) { p->ox=pixmap->screen_x; p->oy=pixmap->screen_y; }
#endif
    p->tx=(gc->miTranslate?d->x:0)-p->ox; p->ty=(gc->miTranslate?d->y:0)-p->oy;
    p->pattern_x=(int64_t)d->x+gc->patOrg.x-p->ox;
    p->pattern_y=(int64_t)d->y+gc->patOrg.y-p->oy;
    if (p->pattern_x<INT32_MIN || p->pattern_x>INT32_MAX ||
        p->pattern_y<INT32_MIN || p->pattern_y>INT32_MAX) fail(p,"pattern origin overflow");
    int n=RegionNumRects(gc->pCompositeClip); const BoxRec *boxes=RegionRects(gc->pCompositeClip);
    if (n<0) fail(p,"invalid clip complexity");
    for (int i=0;i<n;++i) if (boxes[i].x2<boxes[i].x1 || boxes[i].y2<boxes[i].y1)
        fail(p,"invalid clip box");
}
static int64_t max64(int64_t a,int64_t b) { return a>b?a:b; }
static int64_t min64(int64_t a,int64_t b) { return a<b?a:b; }
static void submit(struct plan *);
static void append(struct plan *p,int64_t x1,int64_t y1,int64_t x2,int64_t y2)
{
    if (x1<INT32_MIN || y1<INT32_MIN || x2>INT32_MAX || y2>INT32_MAX || x2<x1 || y2<y1)
        fail(p,"request coordinate overflow");
    if (x1==x2 || y1==y2) return;
    int n=RegionNumRects(p->gc->pCompositeClip); const BoxRec *boxes=RegionRects(p->gc->pCompositeClip);
    for (int i=0;i<n;++i) {
        int64_t a=max64(max64(x1,boxes[i].x1-p->ox),0), b=max64(max64(y1,boxes[i].y1-p->oy),0);
        int64_t r=min64(min64(x2,boxes[i].x2-p->ox),p->width), t=min64(min64(y2,boxes[i].y2-p->oy),p->height);
        if (a>=r || b>=t) continue;
        if(p->planning){p->planned=1;continue;}
        /* Split only geometry. Each GS batch remains within the backend's
         * box and pixel budgets, preserving request order for Boolean ALUs. */
        while(b<t){
            uint64_t width=(uint64_t)(r-a);
            int64_t end=min64(t,b+PS2_PIPE_MAX_TILE_PIXELS/width);
            uint64_t pixels=width*(uint64_t)(end-b);
            if(p->count==PS2_PIPE_MAX_TILE_BOXES ||
               p->batch_pixels+pixels>PS2_PIPE_MAX_TILE_PIXELS)submit(p);
            p->boxes[p->count++]=(struct ps2_pipe_solid_box){a,b,r,end};
            p->batch_pixels+=pixels;b=end;
        }
    }
}
static int prepare_emission(struct plan *p)
{
    if(!p->planned || !p->mask || p->gc->alu==GXnoop)return 0;
    if(!(p->boxes=calloc(PS2_PIPE_MAX_TILE_BOXES,sizeof(*p->boxes))))fail(p,"fill plan allocation");
    p->planning=0;return 1;
}
static void submit(struct plan *p)
{
    if(!p->count)return;
    unsigned emitted=0;
    int status=p->gc->fillStyle==FillStippled ?
        ps2_pipe_stipple_rectangles(p->owner->context,p->destination,&p->mono,p->boxes,p->count,
            p->pattern_x,p->pattern_y,p->color,p->mask,p->gc->alu,&emitted) :
        p->gc->fillStyle==FillOpaqueStippled &&
        (p->drawable->depth==32 || p->gc->alu!=GXcopy || p->mask!=(p->drawable->depth==15 ? 0x7fffU : 0xffffffU)) ?
        ps2_pipe_opaque_stipple_logic_rectangles(p->owner->context,p->destination,&p->mono,p->boxes,p->count,
            p->pattern_x,p->pattern_y,p->color,p->background,p->mask,p->gc->alu,&emitted) :
        p->gc->fillStyle==FillOpaqueStippled ?
        ps2_pipe_opaque_stipple_rectangles(p->owner->context,p->destination,&p->mono,p->boxes,p->count,
            p->pattern_x,p->pattern_y,p->color,p->background) :
        p->gc->tileIsPixel ? ps2_pipe_solid_rectangles(p->owner->context,p->destination,
        p->boxes,p->count,p->color,p->mask,p->gc->alu) :
        p->drawable->depth!=32 && p->gc->alu==GXcopy && p->mask==(p->drawable->depth==15?0x7fffU:0xffffffU) ?
        ps2_pipe_tile_rectangles(p->owner->context,p->source,p->destination,p->boxes,p->count,p->pattern_x,p->pattern_y) :
        ps2_pipe_tile_logic_geometry(p->owner->context,p->source,p->destination,p->boxes,p->count,
            p->pattern_x,p->pattern_y,p->mask,p->gc->alu);
    if (status) fail(p,strerror(errno));
    p->emitted|=p->gc->fillStyle!=FillStippled || emitted;
    p->count=0;p->batch_pixels=0;
}
static void finish(struct plan *p)
{
    submit(p);
    free(p->boxes);
    if(!p->emitted)return;
    exaMarkSync(p->drawable->pScreen);
    /* Match existing GC solid damage coverage; no new region allocation after drawing. */
    DamageRegionAppend(p->drawable,p->gc->pCompositeClip); DamageRegionProcessPending(p->drawable);
}
static void fill_spans(DrawablePtr d,GCPtr gc,int n,DDXPointPtr points,int *widths,int sorted)
{
    struct ps2_fill_screen *b=owner(d,gc);
    if (gc->fillStyle==FillSolid) { b->gc->solid_ops->FillSpans(d,gc,n,points,widths,sorted); return; }
    struct plan p; begin(&p,d,gc,n,points);
    if (n && !widths) fail(&p,"missing span widths");
    for(unsigned pass=0;pass<2;++pass){
        for (int i=0;i<n;++i) {
            if (widths[i]<0) fail(&p,"negative span width");
            append(&p,(int64_t)points[i].x-p.ox,(int64_t)points[i].y-p.oy,
                (int64_t)points[i].x+widths[i]-p.ox,(int64_t)points[i].y+1-p.oy);
        }
        if(!pass && !prepare_emission(&p))return;
    }
    finish(&p);
}
static void fill_rectangles(DrawablePtr d,GCPtr gc,int n,xRectangle *rectangles)
{
    struct ps2_fill_screen *b=owner(d,gc);
    if (gc->fillStyle==FillSolid) { b->gc->solid_ops->PolyFillRect(d,gc,n,rectangles); return; }
    struct plan p; begin(&p,d,gc,n,rectangles);
    for(unsigned pass=0;pass<2;++pass){
        for (int i=0;i<n;++i) append(&p,rectangles[i].x+p.tx,rectangles[i].y+p.ty,
            rectangles[i].x+p.tx+rectangles[i].width,rectangles[i].y+p.ty+rectangles[i].height);
        if(!pass && !prepare_emission(&p))return;
    }
    finish(&p);
}
static const GCOps operations={.FillSpans=fill_spans,.PolyFillRect=fill_rectangles};
Bool ps2_fill_screen_prepare(ScreenPtr screen,struct ps2_fill_screen *b,struct ps2_gc_screen *gc)
{
    struct ps2_render_client *r=gc?ps2_pipe_context_render(gc->context):NULL;
    if (!screen || !b || b->installed || !gc || !gc->installed || gc->screen!=screen ||
        !gc->solid_ops || gc->fill_ops || !screen->CloseScreen || screen->CloseScreen==close_screen ||
        !r || r->gs!=gc->exa->gs || !(r->gs->caps.features&PS2GS_HW_TEXTURE) ||
        !(r->gs->caps.features&PS2GS_HW_SOLID_LOGIC) ||
        !dixRegisterPrivateKey(&key,PRIVATE_SCREEN,0) || dixLookupPrivate(&screen->devPrivates,&key) ||
        ps2_pipe_context_acquire(gc->context)) return FALSE;
    *b=(struct ps2_fill_screen){.gc=gc,.context=gc->context,.screen=screen,
        .CloseScreen=screen->CloseScreen,.installed=TRUE};
    dixSetPrivate(&screen->devPrivates,&key,b); screen->CloseScreen=close_screen;
    if (!ps2_gc_screen_set_fill_ops(gc,&operations)) FatalError("PS2 GS: tiled fill installation failed\n");
    return TRUE;
}
void ps2_fill_screen_cleanup(struct ps2_fill_screen *b)
{
    if (!b || !b->installed) return;
    if (b->screen->CloseScreen!=close_screen || b->gc->fill_ops!=&operations ||
        dixLookupPrivate(&b->screen->devPrivates,&key)!=b || !ps2_gc_screen_set_fill_ops(b->gc,NULL))
        FatalError("PS2 GS: tiled fill cleanup order\n");
    b->screen->CloseScreen=b->CloseScreen; dixSetPrivate(&b->screen->devPrivates,&key,NULL);
    struct pipe_context *context=b->context; memset(b,0,sizeof(*b)); ps2_pipe_context_release(context);
}
static Bool close_screen(ScreenPtr screen)
{
    struct ps2_fill_screen *b=dixLookupPrivate(&screen->devPrivates,&key);
    if (!b || !b->installed) FatalError("PS2 GS: missing tiled fill screen owner\n");
    CloseScreenProcPtr lower=b->CloseScreen; ps2_fill_screen_cleanup(b); return lower(screen);
}
