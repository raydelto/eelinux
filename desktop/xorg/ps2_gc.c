/* SPDX-License-Identifier: MIT */
#include "ps2_gc.h"
#include "os.h"
#include "dixfont.h"
#include "ps2_coverage.h"
#include "ps2_gs_lut.h"
#include "servermd.h"
#include <X11/fonts/fontstruct.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define MAX_GCS 1024U
struct ps2_gc_private {
    struct ps2_gc_screen *owner;
    struct ps2_gc_private *next, **previous;
    GCPtr gc;
    const GCFuncs *funcs;
    const GCOps *mono_lower;
    struct ps2_glyph_gc glyph;
};
static DevPrivateKeyRec screen_key, gc_key;
static const GCFuncs funcs;
static Bool create_gc(GCPtr);
static Bool close_screen(ScreenPtr);
static void install_ops(GCPtr, struct ps2_gc_private *);
struct coverage_pixmap {
    PixmapPtr pixmap;
    PixmapRec header;
    struct ps2_alpha_source bytes;
    struct ps2_coverage_surface gs;
    uint32_t handle;
};
struct coverage_gc {
    struct ps2_gc_private *owner;
    struct coverage_pixmap pixmaps[3];
    unsigned count,depth;
    Pixel foreground,background,mask;
    GCOps operations;
    PixmapPtr stipple;
    PixmapRec stipple_header;
};
static void coverage_begin(struct coverage_gc *,DrawablePtr,GCPtr,DrawablePtr);
static void coverage_end(struct coverage_gc *,GCPtr);
static void coverage_put_image(DrawablePtr,GCPtr,int,int,int,int,int,int,int,char *);
static void coverage_set_spans(DrawablePtr,GCPtr,char *,DDXPointPtr,int *,int,int);

static void fail(const char *reason)
{
    FatalError("PS2 GS: GC integration failed: %s\n", reason);
}
static struct ps2_gc_private *lookup(GCPtr gc)
{
    struct ps2_gc_private *p = dixLookupPrivate(&gc->devPrivates, &gc_key);
    if (!p || p->gc != gc || !p->owner->installed)
        fail("missing GC owner");
    return p;
}
static void live(struct ps2_gc_private *p)
{
    if (ps2_pipe_context_acquire(p->owner->context))
        fail("context is closed or failed");
    ps2_pipe_context_release(p->owner->context);
}
static struct ps2_gc_private *begin(GCPtr gc)
{
    struct ps2_gc_private *p = lookup(gc);
    if (gc->funcs != &funcs) fail("GC funcs changed without unwrapping");
    ps2_glyph_gc_detach(&p->glyph);
    if (p->mono_lower) { gc->ops = p->mono_lower; p->mono_lower = NULL; }
    gc->funcs = p->funcs;
    return p;
}
static void finish(GCPtr gc, struct ps2_gc_private *p, Bool drawing)
{
    if (!gc->funcs || gc->funcs == &funcs || !gc->ops)
        fail("lower GC callbacks are invalid");
    p->funcs = gc->funcs;
    gc->funcs = &funcs;
    if (drawing) install_ops(gc, p);
}
static void validate(GCPtr gc, unsigned long changes, DrawablePtr drawable)
{
    live(lookup(gc));
    struct ps2_gc_private *p = begin(gc);
    if (!gc->funcs->ValidateGC) fail("missing ValidateGC");
    /* EXA maps tiles whenever FillTiled is set, even for tileIsPixel. fb then
     * pads tile/stipple storage for CPU loops. Our native operations use the
     * original source dimensions; keep clip/ROP/dash validation, but hide
     * these unused source preparations from the lower colour GC callback. */
    unsigned fill=gc->fillStyle;
    Bool tile=gc->tileIsPixel;
    PixmapPtr stipple=gc->stipple;
    {
        gc->fillStyle=FillSolid;gc->tileIsPixel=TRUE;gc->stipple=NULL;
        changes&=~(GCTile|GCStipple);
    }
    gc->funcs->ValidateGC(gc, changes, drawable);
    gc->fillStyle=fill;gc->tileIsPixel=tile;gc->stipple=stipple;
    finish(gc, p, TRUE);
}
static void change(GCPtr gc, unsigned long mask)
{
    struct ps2_gc_private *p = begin(gc);
    gc->funcs->ChangeGC(gc, mask);
    finish(gc, p, FALSE);
}
static void copy_gc(GCPtr source, unsigned long mask, GCPtr destination)
{
    struct ps2_gc_private *p = begin(destination);
    destination->funcs->CopyGC(source, mask, destination);
    finish(destination, p, FALSE);
}
static void remove_gc(struct ps2_gc_private *p)
{
    *p->previous = p->next;
    if (p->next) p->next->previous = p->previous;
    --p->owner->count;
    dixSetPrivate(&p->gc->devPrivates, &gc_key, NULL);
}
static void destroy(GCPtr gc)
{
    struct ps2_gc_private *p = begin(gc);
    remove_gc(p);
    /* The lower callback may release GC private storage; do not touch gc after. */
    gc->funcs->DestroyGC(gc);
    free(p);
}
static void change_clip(GCPtr gc, int type, void *value, int count)
{
    struct ps2_gc_private *p = begin(gc);
    gc->funcs->ChangeClip(gc, type, value, count);
    finish(gc, p, FALSE);
}
static void destroy_clip(GCPtr gc)
{
    struct ps2_gc_private *p = begin(gc);
    gc->funcs->DestroyClip(gc);
    finish(gc, p, FALSE);
}
static void copy_clip(GCPtr destination, GCPtr source)
{
    struct ps2_gc_private *p = begin(destination);
    destination->funcs->CopyClip(destination, source);
    finish(destination, p, FALSE);
}
static const GCFuncs funcs = {
    validate, change, copy_gc, destroy, change_clip, destroy_clip, copy_clip
};

/* Lower EXA/damage ops can replace both tables while dispatching. Restore the
 * native hooks around every non-text op instead of copying those ops blindly. */
#define FORWARD_VOID(name, signature, arguments) \
static void forward_##name signature { \
    if(gc->depth==1 || gc->depth==4 || gc->depth==8 || gc->depth==12 || gc->depth==16){ \
        struct coverage_gc state;coverage_begin(&state,d,gc,NULL); \
        if(!gc->ops->name)fail("missing native coverage " #name); \
        gc->ops->name arguments;coverage_end(&state,gc);return; \
    } \
    live(lookup(gc)); \
    struct ps2_gc_private *p = begin(gc); \
    if (!gc->ops->name) fail("missing " #name); \
    gc->ops->name arguments; \
    finish(gc, p, TRUE); \
}
FORWARD_VOID(FillSpans, (DrawablePtr d, GCPtr gc, int n, DDXPointPtr points, int *widths, int sorted),
             (d, gc, n, points, widths, sorted))
FORWARD_VOID(PolyPoint, (DrawablePtr d, GCPtr gc, int mode, int n, DDXPointPtr points), (d, gc, mode, n, points))
FORWARD_VOID(Polylines, (DrawablePtr d, GCPtr gc, int mode, int n, DDXPointPtr points), (d, gc, mode, n, points))
FORWARD_VOID(PolySegment, (DrawablePtr d, GCPtr gc, int n, xSegment *segments), (d, gc, n, segments))
FORWARD_VOID(PolyRectangle, (DrawablePtr d, GCPtr gc, int n, xRectangle *rects), (d, gc, n, rects))
FORWARD_VOID(PolyArc, (DrawablePtr d, GCPtr gc, int n, xArc *arcs), (d, gc, n, arcs))
FORWARD_VOID(FillPolygon, (DrawablePtr d, GCPtr gc, int shape, int mode, int n, DDXPointPtr points),
             (d, gc, shape, mode, n, points))
FORWARD_VOID(PolyFillRect, (DrawablePtr d, GCPtr gc, int n, xRectangle *rects), (d, gc, n, rects))
FORWARD_VOID(PolyFillArc, (DrawablePtr d, GCPtr gc, int n, xArc *arcs), (d, gc, n, arcs))
FORWARD_VOID(PushPixels, (GCPtr gc, PixmapPtr bitmap, DrawablePtr d, int w, int h, int x, int y),
             (gc, bitmap, d, w, h, x, y))
FORWARD_VOID(PolyGlyphBlt, (DrawablePtr d, GCPtr gc, int x, int y, unsigned n, CharInfoPtr *glyphs, void *base),
             (d, gc, x, y, n, glyphs, base))
FORWARD_VOID(ImageGlyphBlt, (DrawablePtr d, GCPtr gc, int x, int y, unsigned n, CharInfoPtr *glyphs, void *base),
             (d, gc, x, y, n, glyphs, base))
#undef FORWARD_VOID
static void forward_SetSpans(DrawablePtr d,GCPtr gc,char *bits,DDXPointPtr points,int *widths,int n,int sorted)
{
    if(gc->depth==1 || gc->depth==4 || gc->depth==8 || gc->depth==12 || gc->depth==16){coverage_set_spans(d,gc,bits,points,widths,n,sorted);return;}
    struct ps2_gc_private *p=begin(gc);live(p);
    if(!gc->ops->SetSpans)fail("missing SetSpans");
    gc->ops->SetSpans(d,gc,bits,points,widths,n,sorted);finish(gc,p,TRUE);
}
static void forward_PutImage(DrawablePtr d,GCPtr gc,int depth,int x,int y,int w,int h,int pad,int format,char *bits)
{
    if(gc->depth==1 || gc->depth==4 || gc->depth==8 || gc->depth==12 || gc->depth==16){coverage_put_image(d,gc,depth,x,y,w,h,pad,format,bits);return;}
    struct ps2_gc_private *p=begin(gc);live(p);
    if(!gc->ops->PutImage)fail("missing PutImage");
    gc->ops->PutImage(d,gc,depth,x,y,w,h,pad,format,bits);finish(gc,p,TRUE);
}
#define FORWARD_REGION(name, signature, arguments) \
static RegionPtr forward_##name signature { \
    if(gc->depth==1 || gc->depth==4 || gc->depth==8 || gc->depth==12 || gc->depth==16){ \
        if(!source || source->depth!=gc->depth)fail("invalid coverage CopyArea source depth"); \
        struct coverage_gc state;coverage_begin(&state,d,gc,source); \
        if(!gc->ops->name)fail("missing native coverage " #name); \
        RegionPtr region=gc->ops->name arguments;coverage_end(&state,gc);return region; \
    } \
    live(lookup(gc)); \
    struct ps2_gc_private *p = begin(gc); \
    if (!gc->ops->name) fail("missing " #name); \
    RegionPtr region = gc->ops->name arguments; \
    finish(gc, p, TRUE); \
    return region; \
}
FORWARD_REGION(CopyArea, (DrawablePtr source, DrawablePtr d, GCPtr gc, int x, int y, int w, int h, int dx, int dy),
               (source, d, gc, x, y, w, h, dx, dy))
#undef FORWARD_REGION
static RegionPtr forward_CopyPlane(DrawablePtr source, DrawablePtr d, GCPtr gc,
    int x, int y, int w, int h, int dx, int dy, unsigned long plane)
{
    struct ps2_gc_private *p = lookup(gc); live(p);
    if(gc->depth==1 || gc->depth==4 || gc->depth==8 || gc->depth==12 || gc->depth==16){
        struct coverage_gc state;coverage_begin(&state,d,gc,source);
        if(!gc->ops->CopyPlane)fail("missing native coverage CopyPlane");
        RegionPtr result=gc->ops->CopyPlane(source,d,gc,x,y,w,h,dx,dy,plane);
        coverage_end(&state,gc);return result;
    }
    p = begin(gc);
    if (!gc->ops->CopyPlane) fail("missing CopyPlane");
    RegionPtr result = gc->ops->CopyPlane(source,d,gc,x,y,w,h,dx,dy,plane);
    finish(gc,p,TRUE); return result;
}

static int text(DrawablePtr d, GCPtr gc, int x, int y, int count,
                unsigned char *chars, Bool wide, Bool image)
{
    struct ps2_gc_private *p = lookup(gc);
    live(p);
    if (gc->ops != &p->glyph.ops || !p->glyph.gc || count < 0 || count > 255 ||
        (count && !chars) || !gc->font || !gc->font->get_glyphs)
        fail("invalid core text request");
    if (!count) return x;
    CharInfoPtr glyphs[255];
    unsigned long n = 0;
    FontEncoding encoding = wide ?
        (gc->font->info.lastRow ? TwoD16Bit : Linear16Bit) : Linear8Bit;
    GetGlyphs(gc->font, count, chars, encoding, &n, glyphs);
    if (n > (unsigned)count) fail("font returned too many glyphs");
    int64_t end = x;
    for (unsigned long i = 0; i < n; ++i) {
        if (!glyphs[i]) fail("font returned null glyph");
        end += glyphs[i]->metrics.characterWidth;
    }
    if (end < INT_MIN || end > INT_MAX) fail("text advance overflow");
    if (n) {
        if (image) gc->ops->ImageGlyphBlt(d, gc, x, y, n, glyphs, NULL);
        else gc->ops->PolyGlyphBlt(d, gc, x, y, n, glyphs, NULL);
    }
    return end;
}
static int poly8(DrawablePtr d, GCPtr gc, int x, int y, int n, char *chars)
{
    if ((gc->depth != 1 && gc->depth != 4 && gc->depth != 8 && gc->depth != 12 && gc->depth != 16)) return text(d, gc, x, y, n, (unsigned char *)chars, FALSE, FALSE);
    struct coverage_gc state;coverage_begin(&state,d,gc,NULL);
    int result=text(d,gc,x,y,n,(unsigned char*)chars,FALSE,FALSE);coverage_end(&state,gc);return result;
}
static int poly16(DrawablePtr d, GCPtr gc, int x, int y, int n, unsigned short *chars)
{
    if ((gc->depth != 1 && gc->depth != 4 && gc->depth != 8 && gc->depth != 12 && gc->depth != 16)) return text(d, gc, x, y, n, (unsigned char *)chars, TRUE, FALSE);
    struct coverage_gc state;coverage_begin(&state,d,gc,NULL);
    int result=text(d,gc,x,y,n,(unsigned char*)chars,TRUE,FALSE);coverage_end(&state,gc);return result;
}
static void image8(DrawablePtr d, GCPtr gc, int x, int y, int n, char *chars)
{
    if ((gc->depth != 1 && gc->depth != 4 && gc->depth != 8 && gc->depth != 12 && gc->depth != 16)) { (void)text(d,gc,x,y,n,(unsigned char *)chars,FALSE,TRUE); return; }
    struct coverage_gc state;coverage_begin(&state,d,gc,NULL);
    (void)text(d,gc,x,y,n,(unsigned char*)chars,FALSE,TRUE);coverage_end(&state,gc);
}
static void image16(DrawablePtr d, GCPtr gc, int x, int y, int n, unsigned short *chars)
{
    if ((gc->depth != 1 && gc->depth != 4 && gc->depth != 8 && gc->depth != 12 && gc->depth != 16)) { (void)text(d,gc,x,y,n,(unsigned char *)chars,TRUE,TRUE); return; }
    struct coverage_gc state;coverage_begin(&state,d,gc,NULL);
    (void)text(d,gc,x,y,n,(unsigned char*)chars,TRUE,TRUE);coverage_end(&state,gc);
}
static void solid_ops(struct ps2_gc_private *p)
{
    const GCOps *native = p->owner->solid_ops;
#define SOLID(name) p->glyph.ops.name = native ? native->name : forward_##name
    SOLID(FillSpans); SOLID(PolyPoint); SOLID(Polylines);
    SOLID(PolySegment); SOLID(PolyRectangle); SOLID(PolyFillRect);
#undef SOLID
    if (p->owner->fill_ops) {
        p->glyph.ops.FillSpans = p->owner->fill_ops->FillSpans;
        p->glyph.ops.PolyFillRect = p->owner->fill_ops->PolyFillRect;
    }
}
static void geometry_ops(struct ps2_gc_private *p)
{
    const GCOps *ops=p->owner->geometry_ops;
    if(!ops)return;
    p->glyph.ops.Polylines=ops->Polylines;
    p->glyph.ops.PolySegment=ops->PolySegment;
    p->glyph.ops.PolyRectangle=ops->PolyRectangle;
    p->glyph.ops.PolyArc=ops->PolyArc;
    p->glyph.ops.PolyFillArc=ops->PolyFillArc;
    p->glyph.ops.PushPixels=ops->PushPixels;
}
Bool ps2_gc_screen_set_geometry_ops(struct ps2_gc_screen *b,const GCOps *ops)
{
    if(!b || !b->installed || (ops && (!ops->Polylines || !ops->PolySegment ||
        !ops->PolyRectangle || !ops->PolyArc || !ops->PolyFillArc || !ops->PushPixels)))return FALSE;
    b->geometry_ops=ops;
    for(struct ps2_gc_private *p=b->gcs;p;p=p->next)if(p->glyph.gc){
        solid_ops(p);p->glyph.ops.PolyArc=forward_PolyArc;
        p->glyph.ops.PolyFillArc=forward_PolyFillArc;p->glyph.ops.PushPixels=forward_PushPixels;
        geometry_ops(p);
    }
    return TRUE;
}
static void copy_ops(struct ps2_gc_private *p)
{
    const GCOps *native=p->owner->copy_ops;
    p->glyph.ops.CopyArea=native ? native->CopyArea : forward_CopyArea;
    const GCOps *plane=p->owner->plane_ops;
    p->glyph.ops.CopyPlane=plane ? plane->CopyPlane : forward_CopyPlane;
}
static void image_ops(struct ps2_gc_private *p)
{
    const GCOps *native=p->owner->image_ops;
    p->glyph.ops.PutImage=native?native->PutImage:forward_PutImage;
    p->glyph.ops.SetSpans=native?native->SetSpans:forward_SetSpans;
}
static void polygon_ops(struct ps2_gc_private *p)
{
    const GCOps *native=p->owner->polygon_ops;
    p->glyph.ops.FillPolygon=native?native->FillPolygon:forward_FillPolygon;
}
Bool ps2_gc_screen_set_polygon_ops(struct ps2_gc_screen *b,const GCOps *ops)
{
    if(!b || !b->installed || (ops && !ops->FillPolygon))return FALSE;
    b->polygon_ops=ops;
    for(struct ps2_gc_private *p=b->gcs;p;p=p->next)
        if(p->glyph.gc)polygon_ops(p);
    return TRUE;
}
Bool ps2_gc_screen_set_image_ops(struct ps2_gc_screen *b,const GCOps *ops)
{
    if(!b || !b->installed || (ops && (!ops->PutImage || !ops->SetSpans)))return FALSE;
    b->image_ops=ops;
    for(struct ps2_gc_private *p=b->gcs;p;p=p->next)
        if(p->glyph.gc)image_ops(p);
    return TRUE;
}
Bool ps2_gc_screen_set_copy_ops(struct ps2_gc_screen *b,const GCOps *ops)
{
    if(!b || !b->installed || (ops && !ops->CopyArea)) return FALSE;
    b->copy_ops=ops;
    for(struct ps2_gc_private *p=b->gcs;p;p=p->next)
        if(p->glyph.gc) copy_ops(p);
    return TRUE;
}
Bool ps2_gc_screen_set_plane_ops(struct ps2_gc_screen *b,const GCOps *ops)
{
    if(!b || !b->installed || (ops && !ops->CopyPlane))return FALSE;
    b->plane_ops=ops;
    for(struct ps2_gc_private *p=b->gcs;p;p=p->next)if(p->glyph.gc)copy_ops(p);
    return TRUE;
}
Bool ps2_gc_screen_set_solid_ops(struct ps2_gc_screen *b, const GCOps *ops)
{
    if (!b || !b->installed || (ops && (!ops->FillSpans || !ops->PolyPoint ||
        !ops->Polylines || !ops->PolySegment || !ops->PolyRectangle || !ops->PolyFillRect)))
        return FALSE;
    b->solid_ops = ops;
    for (struct ps2_gc_private *p = b->gcs; p; p = p->next)
        if (p->glyph.gc) { solid_ops(p); geometry_ops(p); }
    return TRUE;
}
Bool ps2_gc_screen_set_fill_ops(struct ps2_gc_screen *b, const GCOps *ops)
{
    if (!b || !b->installed || (ops && (!ops->FillSpans || !ops->PolyFillRect))) return FALSE;
    b->fill_ops = ops;
    for (struct ps2_gc_private *p = b->gcs; p; p = p->next)
        if (p->glyph.gc) { solid_ops(p); geometry_ops(p); }
    return TRUE;
}
static void coverage_pixmap(struct coverage_gc *state,DrawablePtr d)
{
    if(!d || (d->depth!=1 && d->depth!=4 && d->depth!=8 && d->depth!=12 && d->depth!=16))return;
    if(d->type!=DRAWABLE_PIXMAP || d->x || d->y || state->count==3)fail("invalid coverage drawable");
    PixmapPtr pixmap=(PixmapPtr)d;
    for(unsigned i=0;i<state->count;++i)if(state->pixmaps[i].pixmap==pixmap)return;
    struct coverage_pixmap *p=state->pixmaps+state->count;
    p->pixmap=pixmap;p->header=*pixmap;
    if(ps2_surface_exa_alpha_source(pixmap,&p->bytes)!=1)fail("missing owned coverage storage");
    struct ps2_picture_bitmap bytes={.bits=p->bytes.bits,.bytes=p->bytes.bytes,.stride=p->bytes.stride,
        .width=p->bytes.width,.height=p->bytes.height,.bits_per_pixel=p->bytes.bits_per_pixel,.alpha_bits=p->bytes.depth,
        .msb_first=BITMAP_BIT_ORDER==MSBFirst};
    struct ps2_surface_client *gs=state->owner->owner->exa->gs;
    if(ps2_coverage_decode_core(&p->gs,gs,&bytes))fail("GS coverage import failed");
    void *private=exaGetPixmapDriverPrivate(pixmap);
    if(!private)fail("missing coverage driver private");
    memcpy(&p->handle,private,4);memcpy(private,&p->gs.surface,4);
    pixmap->drawable.depth=24;pixmap->drawable.bitsPerPixel=32;
    pixmap->devPrivate.ptr=NULL;
    pixmap->devKind=ps2_surface_lookup(gs,p->gs.surface)->buffer_width*4;
    ++state->count;
}
static void coverage_begin(struct coverage_gc *state,DrawablePtr d,GCPtr gc,DrawablePtr source)
{
    struct ps2_gc_private *p=lookup(gc);live(p);
    *state=(struct coverage_gc){.owner=p,.depth=gc->depth,.foreground=gc->fgPixel,
        .background=gc->bgPixel,.mask=gc->planemask,.operations=*gc->ops,.stipple=gc->stipple};
    if(!d || d->pScreen!=gc->pScreen || d->depth!=gc->depth || !p->mono_lower || p->glyph.gc)
        fail("invalid native coverage GC");
    if(gc->stipple==(PixmapPtr)d){state->stipple_header=*(PixmapPtr)d;gc->stipple=&state->stipple_header;}
    coverage_pixmap(state,d);coverage_pixmap(state,source);
    if(gc->fillStyle==FillTiled && !gc->tileIsPixel)coverage_pixmap(state,&gc->tile.pixmap->drawable);
    gc->depth=24;uint32_t bits=(1U<<state->depth)-1;
    gc->fgPixel&=bits;gc->bgPixel&=bits;gc->planemask&=bits;
    gc->ops=p->mono_lower;
    if(!ps2_glyph_gc_attach(gc,&p->glyph,p->owner->exa,p->owner->context))fail("GS coverage glyph attachment");
    const GCOps *solid=p->owner->solid_ops,*fill=p->owner->fill_ops,*geometry=p->owner->geometry_ops;
    p->glyph.ops.FillSpans=fill?fill->FillSpans:solid?solid->FillSpans:NULL;
    p->glyph.ops.PolyFillRect=fill?fill->PolyFillRect:solid?solid->PolyFillRect:NULL;
    p->glyph.ops.PolyPoint=solid?solid->PolyPoint:NULL;
    p->glyph.ops.Polylines=geometry?geometry->Polylines:solid?solid->Polylines:NULL;
    p->glyph.ops.PolySegment=geometry?geometry->PolySegment:solid?solid->PolySegment:NULL;
    p->glyph.ops.PolyRectangle=geometry?geometry->PolyRectangle:solid?solid->PolyRectangle:NULL;
    p->glyph.ops.PolyArc=geometry?geometry->PolyArc:NULL;
    p->glyph.ops.PolyFillArc=geometry?geometry->PolyFillArc:NULL;
    p->glyph.ops.PushPixels=geometry?geometry->PushPixels:NULL;
    p->glyph.ops.CopyArea=p->owner->copy_ops?p->owner->copy_ops->CopyArea:NULL;
    p->glyph.ops.CopyPlane=p->owner->plane_ops?p->owner->plane_ops->CopyPlane:NULL;
    p->glyph.ops.FillPolygon=p->owner->polygon_ops?p->owner->polygon_ops->FillPolygon:NULL;
    p->glyph.ops.PutImage=p->owner->image_ops?p->owner->image_ops->PutImage:NULL;
    p->glyph.ops.SetSpans=p->owner->image_ops?p->owner->image_ops->SetSpans:NULL;
    p->glyph.native_spans=!!p->glyph.ops.FillSpans;
}
static void coverage_end(struct coverage_gc *state,GCPtr gc)
{
    struct ps2_gc_private *p=state->owner;
    if(!state->count || gc->depth!=24)fail("native coverage GC completion order");
    ps2_glyph_gc_detach(&p->glyph);p->glyph.ops=state->operations;gc->ops=&p->glyph.ops;
    gc->depth=state->depth;gc->fgPixel=state->foreground;gc->bgPixel=state->background;
    gc->planemask=state->mask;gc->stipple=state->stipple;
    for(unsigned i=state->count;i--;){struct coverage_pixmap *q=state->pixmaps+i;
        if(!i && ps2_coverage_encode(&q->gs,p->owner->context,(unsigned char*)q->bytes.bits,q->bytes.bytes,q->bytes.stride))
            fail("GS coverage client transfer failed");
        void *private=exaGetPixmapDriverPrivate(q->pixmap);memcpy(private,&q->handle,4);
        q->pixmap->drawable.depth=q->header.drawable.depth;
        q->pixmap->drawable.bitsPerPixel=q->header.drawable.bitsPerPixel;
        q->pixmap->devPrivate=q->header.devPrivate;q->pixmap->devKind=q->header.devKind;
        if(ps2_coverage_release(&q->gs))fail("GS coverage scratch release failed");
    }
}
static void coverage_put_image(DrawablePtr d,GCPtr gc,int depth,int x,int y,int w,int h,int pad,int format,char *pixels)
{
    if(!d || !gc || w<0 || h<0 || w>1024 || h>1024 || pad<0 || pad>31 ||
        (format!=ZPixmap && format!=XYBitmap && format!=XYPixmap) ||
        (format==XYBitmap?depth!=1:depth!=gc->depth) || (format==ZPixmap && pad) ||
        (w && h && !pixels))fail("invalid coverage PutImage");
    if(!w || !h)return;
    ScreenPtr screen=d->pScreen;
    if(!screen->CreatePixmap || !screen->DestroyPixmap)fail("missing coverage client pixmap callbacks");
    unsigned source_depth=format==XYBitmap?1:gc->depth,width=w+(format==ZPixmap?0:pad);
    PixmapPtr source=screen->CreatePixmap(screen,width,h,source_depth,0);
    if(!source)fail("coverage client pixmap allocation");
    struct ps2_alpha_source bytes;
    if(ps2_surface_exa_alpha_source(source,&bytes)!=1)fail("invalid coverage client pixmap");
    size_t stride=format==ZPixmap?(((size_t)w*bytes.bits_per_pixel+31)/32)*4:(size_t)BitmapBytePad(width);
    if(format!=XYPixmap || source_depth==1)
        for(int row=0;row<h;++row)memcpy((unsigned char*)bytes.bits+row*bytes.stride,pixels+row*stride,stride);
    struct coverage_gc state;coverage_begin(&state,d,gc,&source->drawable);
    if(format==XYPixmap && source_depth!=1){
        struct ps2_surface_client *gs=state.owner->owner->exa->gs;
        uint32_t handle;memcpy(&handle,exaGetPixmapDriverPrivate(source),4);
        for(unsigned plane=0;plane<source_depth;++plane){
            struct ps2_picture_bitmap bitmap={.bits=(unsigned char*)pixels+plane*stride*h,.bytes=stride*h,
                .stride=stride,.width=width,.height=h,.bits_per_pixel=1,.msb_first=BITMAP_BIT_ORDER==MSBFirst};
            struct ps2_coverage_surface bit={0};uint32_t table[256];unsigned shift=source_depth-1-plane;
            for(unsigned i=0;i<256;++i)table[i]=(i&1)<<shift;
            if(ps2_coverage_decode_core(&bit,gs,&bitmap) || ps2_surface_map_palette(gs,bit.palette,table) ||
                ps2_surface_channel_map_region(gs,bit.surface,handle,bit.palette,0,0,0,0,width,h,1U<<shift,0) ||
                ps2_coverage_release(&bit))fail("GS coverage planar upload failed");
        }
    }
    RegionPtr region;Bool exposures=gc->fExpose;gc->fExpose=FALSE;
    if(format==XYBitmap){
        if(!gc->ops->CopyPlane)fail("missing native coverage image CopyPlane");
        region=gc->ops->CopyPlane(&source->drawable,d,gc,pad,0,w,h,x,y,1);
    }else{
        if(!gc->ops->CopyArea)fail("missing native coverage image CopyArea");
        region=gc->ops->CopyArea(&source->drawable,d,gc,format==XYPixmap?pad:0,0,w,h,x,y);
    }
    gc->fExpose=exposures;
    if(region){RegionUninit(region);free(region);}
    coverage_end(&state,gc);screen->DestroyPixmap(source);
}
static void coverage_set_spans(DrawablePtr d,GCPtr gc,char *pixels,DDXPointPtr points,int *widths,int count,int sorted)
{
    (void)sorted;
    if(!d || !gc || count<0 || count>4096 || (count && (!pixels || !points || !widths)))fail("invalid coverage SetSpans");
    size_t offset=0;
    for(int i=0;i<count;++i){
        if(widths[i]<0 || widths[i]>1024)fail("invalid coverage span width");
        size_t stride=(((size_t)widths[i]*(gc->depth==1?1:gc->depth<=8?8:16)+31)/32)*4;
        if(offset+stride>16U*1024*1024)fail("coverage span input budget exceeded");
        coverage_put_image(d,gc,gc->depth,points[i].x,points[i].y,widths[i],1,0,ZPixmap,pixels+offset);
        offset+=stride;
    }
}

static void install_ops(GCPtr gc, struct ps2_gc_private *p)
{
    if (gc->depth == 1 || gc->depth == 4 || gc->depth == 8 || gc->depth == 12 || gc->depth == 16) {
        p->mono_lower = gc->ops;
        p->glyph.ops = *gc->ops;
        gc->ops = &p->glyph.ops;
    } else if (!ps2_glyph_gc_attach(gc, &p->glyph, p->owner->exa, p->owner->context))
        fail("cannot attach native glyph operations");
#define FORWARD(name) p->glyph.ops.name = forward_##name
    FORWARD(FillSpans); FORWARD(SetSpans); FORWARD(PutImage);
    FORWARD(CopyArea); FORWARD(CopyPlane); FORWARD(PolyPoint); FORWARD(Polylines);
    FORWARD(PolySegment); FORWARD(PolyRectangle); FORWARD(PolyArc);
    FORWARD(FillPolygon); FORWARD(PolyFillRect); FORWARD(PolyFillArc); FORWARD(PushPixels);
    if (gc->depth == 1 || gc->depth == 4 || gc->depth == 8 || gc->depth == 12 || gc->depth == 16) { FORWARD(PolyGlyphBlt); FORWARD(ImageGlyphBlt); }
#undef FORWARD
    /* Coverage drawing is rebound to GS by the wrappers above. Colour
     * drawing installs native hooks here. Neither path permits CPU raster access. */
    if ((gc->depth != 1 && gc->depth != 4 && gc->depth != 8 && gc->depth != 12 && gc->depth != 16)) { solid_ops(p); copy_ops(p); image_ops(p); polygon_ops(p); geometry_ops(p); }
    p->glyph.native_spans=p->owner->solid_ops && p->owner->fill_ops;
    p->glyph.ops.PolyText8 = poly8; p->glyph.ops.PolyText16 = poly16;
    p->glyph.ops.ImageText8 = image8; p->glyph.ops.ImageText16 = image16;
}
static Bool complete(const GCFuncs *f)
{
    return f && f->ValidateGC && f->ChangeGC && f->CopyGC && f->DestroyGC &&
           f->ChangeClip && f->DestroyClip && f->CopyClip;
}
static Bool create_gc(GCPtr gc)
{
    struct ps2_gc_screen *b = dixLookupPrivate(&gc->pScreen->devPrivates, &screen_key);
    if (!b || !b->installed || b->count == MAX_GCS ||
        ps2_pipe_context_acquire(b->context)) return FALSE;
    ps2_pipe_context_release(b->context);
    struct ps2_gc_private *p = calloc(1, sizeof(*p));
    if (!p) return FALSE;
    b->screen->CreateGC = b->CreateGC;
    Bool ok = b->screen->CreateGC(gc);
    b->CreateGC = b->screen->CreateGC;
    b->screen->CreateGC = create_gc;
    if (!ok) { free(p); return FALSE; }
    if (!complete(gc->funcs) || !gc->ops) {
        if (gc->funcs && gc->funcs->DestroyGC) gc->funcs->DestroyGC(gc);
        free(p); return FALSE;
    }
    p->owner = b; p->gc = gc; p->funcs = gc->funcs;
    p->next = b->gcs; p->previous = &b->gcs;
    if (p->next) p->next->previous = &p->next;
    b->gcs = p; ++b->count;
    dixSetPrivate(&gc->devPrivates, &gc_key, p);
    gc->funcs = &funcs;
    return TRUE;
}
static Bool close_screen(ScreenPtr screen)
{
    struct ps2_gc_screen *b = dixLookupPrivate(&screen->devPrivates, &screen_key);
    if (!b) fail("missing screen owner");
    CloseScreenProcPtr lower = b->CloseScreen;
    ps2_gc_screen_cleanup(b);
    return lower(screen);
}
Bool ps2_gc_screen_prepare(ScreenPtr screen, struct ps2_gc_screen *b,
                           struct ps2_surface_exa *exa, struct pipe_context *context)
{
    struct ps2_render_client *render = ps2_pipe_context_render(context);
    if (!screen || !b || b->installed || !screen->CreateGC || !screen->CloseScreen ||
        screen->CreateGC == create_gc || !exa || !render || render->gs != exa->gs ||
        !dixRegisterPrivateKey(&screen_key, PRIVATE_SCREEN, 0) ||
        !dixRegisterPrivateKey(&gc_key, PRIVATE_GC, 0) ||
        !ps2_glyph_register_private() ||
        dixLookupPrivate(&screen->devPrivates, &screen_key) ||
        ps2_pipe_context_acquire(context)) return FALSE;
    *b = (struct ps2_gc_screen){.screen = screen, .exa = exa, .context = context,
        .CreateGC = screen->CreateGC, .CloseScreen = screen->CloseScreen, .installed = TRUE};
    dixSetPrivate(&screen->devPrivates, &screen_key, b);
    screen->CreateGC = create_gc; screen->CloseScreen = close_screen;
    return TRUE;
}
void ps2_gc_screen_cleanup(struct ps2_gc_screen *b)
{
    if (!b || !b->installed) return;
    if (b->screen->CreateGC != create_gc || b->screen->CloseScreen != close_screen)
        fail("screen callbacks changed without unwrapping");
    b->screen->CreateGC = b->CreateGC; b->screen->CloseScreen = b->CloseScreen;
    while (b->gcs) {
        struct ps2_gc_private *p = b->gcs;
        ps2_glyph_gc_detach(&p->glyph);
        if (p->mono_lower) p->gc->ops = p->mono_lower;
        if (p->gc->funcs == &funcs) p->gc->funcs = p->funcs;
        remove_gc(p);
        free(p);
    }
    struct pipe_context *context = b->context;
    dixSetPrivate(&b->screen->devPrivates, &screen_key, NULL);
    memset(b, 0, sizeof(*b));
    ps2_pipe_context_release(context);
}
