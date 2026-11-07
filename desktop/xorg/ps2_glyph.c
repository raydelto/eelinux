/* SPDX-License-Identifier: MIT */
#include "ps2_glyph.h"
#include "ps2_gs_bitmap.h"
#include "ps2_gs_client.h"
#include "os.h"
#include "windowstr.h"
#include "regionstr.h"
#include "damage.h"
#include <X11/fonts/fontstruct.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
/* Xorg's core glyph ABI supplies server-format bits padded to four bytes,
 * as in miPolyGlyphBlt. R5900 uses LSB-first server bit/byte order. */
#if X_BYTE_ORDER == X_LITTLE_ENDIAN
#define SERVER_MSB_FIRST 0
#else
#define SERVER_MSB_FIRST 1
#endif
static DevPrivateKeyRec glyph_key;
Bool ps2_glyph_register_private(void)
{ return dixRegisterPrivateKey(&glyph_key,PRIVATE_GC,0); }
static void fail(const char *reason) { FatalError("PS2 GS: native core glyph operation failed: %s\n",reason); }
static int32_t coordinate(int64_t value)
{
    if(value<INT32_MIN || value>INT32_MAX) fail("coordinate overflow");
    return value;
}
struct glyph_spans {DrawablePtr d;GCPtr gc;int ox,oy,image;Pixel foreground;};
static int emit_spans(void *data,unsigned x,unsigned y,unsigned width,unsigned height,int background)
{
    struct glyph_spans *s=data;DDXPointRec points[256];int widths[256];
    if((int64_t)x+s->ox<INT16_MIN || (int64_t)x+s->ox>INT16_MAX ||
        (int64_t)y+s->oy<INT16_MIN || (int64_t)y+s->oy+height-1>INT16_MAX)fail("glyph span coordinate overflow");
    if(s->image)s->gc->fgPixel=background?s->gc->bgPixel:s->foreground;
    for(unsigned first=0;first<height;first+=256){unsigned count=height-first>256?256:height-first;
        for(unsigned i=0;i<count;++i){points[i]=(DDXPointRec){x+s->ox,y+s->oy+first+i};widths[i]=width;}
        s->gc->ops->FillSpans(s->d,s->gc,count,points,widths,TRUE);}
    return 0;
}
static void glyphs(DrawablePtr drawable,GCPtr gc,int x,int y,unsigned count,CharInfoPtr *characters,int image)
{
    struct ps2_glyph_gc *b=dixLookupPrivate(&gc->devPrivates,&glyph_key);
    if(!b || !b->context || !drawable || drawable->pScreen!=gc->pScreen || !gc->font ||
        !gc->pCompositeClip || count>4096 || (count && !characters)) fail("invalid GC or glyph list");
    if(!count) return;
    /* The long-lived GC lease protects memory, but does not authorize drawing
     * after pipe_context destruction. Check the actual context readiness. */
    if(ps2_pipe_context_acquire(b->context)) fail("context is closed or failed");
    ps2_pipe_context_release(b->context);
    /* ImageGlyphBlt uses GXcopy and solid text regardless of GC alu/fillStyle,
     * matching miImageGlyphBlt. PolyGlyphBlt observes both settings. */
    uint32_t text_bits=drawable->depth==32?UINT32_MAX:((1U<<drawable->depth)-1);
    Bool spans=drawable->depth>=30 || (!image && (gc->alu!=GXcopy || gc->fillStyle!=FillSolid)) ||
        (gc->planemask & text_bits)!=text_bits;
    if((drawable->depth!=15 && drawable->depth!=24 && drawable->depth!=30 && drawable->depth!=32) ||
        (spans && (!b->native_spans || !gc->ops->FillSpans)))fail("unsupported text raster state");
    ScreenPtr screen=drawable->pScreen;
    PixmapPtr pixmap=NULL;
    if(drawable->type==DRAWABLE_PIXMAP) pixmap=(PixmapPtr)drawable;
    else if(drawable->type==DRAWABLE_WINDOW && screen->GetWindowPixmap)
        pixmap=screen->GetWindowPixmap((WindowPtr)drawable);
    if(!pixmap || pixmap->drawable.pScreen!=screen) fail("missing drawable storage");
    uint32_t target=0;
    if(pixmap!=screen->GetScreenPixmap(screen)) {
        /* Existing PS2 EXA driver-private storage begins with the owned handle. */
        const void *private=exaGetPixmapDriverPrivate(pixmap);
        if(!private) fail("missing owned pixmap");
        memcpy(&target,private,sizeof(target));
        if(!target) fail("unallocated pixmap");
    }
    struct ps2_render_client *render=ps2_pipe_context_render(b->context);
    const struct ps2_surface *storage=render ? ps2_surface_lookup(render->gs,target) : NULL;
    if(!storage || render->gs!=b->exa->gs || storage->width!=pixmap->drawable.width ||
        storage->height!=pixmap->drawable.height || storage->bits_per_pixel!=pixmap->drawable.bitsPerPixel ||
        drawable->depth!=ps2_surface_depth(storage)) fail("incompatible pixmap format");
    int ox=0,oy=0;
#ifdef COMPOSITE
    if(drawable->type==DRAWABLE_WINDOW) { ox=pixmap->screen_x; oy=pixmap->screen_y; }
#endif
    int64_t px=(int64_t)x+(gc->miTranslate ? drawable->x : 0)-ox;
    int64_t py=(int64_t)y+(gc->miTranslate ? drawable->y : 0)-oy;
    int clip_count=RegionNumRects(gc->pCompositeClip);
    const BoxRec *boxes=RegionRects(gc->pCompositeClip);
    if(clip_count<0 || clip_count>256) fail("clip complexity exceeds native limit");
    struct ps2_bitmap_box clips[256];
    for(int i=0;i<clip_count;++i) clips[i]=(struct ps2_bitmap_box){
        coordinate((int64_t)boxes[i].x1-ox),coordinate((int64_t)boxes[i].y1-oy),
        coordinate((int64_t)boxes[i].x2-ox),coordinate((int64_t)boxes[i].y2-oy)};
    struct ps2_bitmap_glyph *jobs=calloc(count,sizeof(*jobs));
    if(!jobs) fail("out of memory");
    int64_t start=px;
    for(unsigned i=0;i<count;++i) {
        CharInfoPtr c=characters[i];
        if(!c) { free(jobs); fail("null glyph"); }
        int width=(int)c->metrics.rightSideBearing-c->metrics.leftSideBearing;
        int height=(int)c->metrics.ascent+c->metrics.descent;
        if(width<0 || height<0 || width>256 || height>256) { free(jobs); fail("unsupported glyph dimensions"); }
        size_t stride=(((unsigned)width+7)/8+3)&~3U;
        int64_t gx=px+c->metrics.leftSideBearing,gy=py-c->metrics.ascent;
        if(gx<INT32_MIN || gx>INT32_MAX || gy<INT32_MIN || gy>INT32_MAX) {
            free(jobs); fail("glyph coordinate overflow");
        }
        jobs[i]=(struct ps2_bitmap_glyph){.x=gx,
            .y=gy,.width=width,.height=height,
            .bits=(const unsigned char*)c->bits,.stride=stride,.bytes=stride*height};
        px+=c->metrics.characterWidth;
    }
    struct ps2_bitmap_box background;
    if(image) {
        int height=(int)gc->font->info.fontAscent+gc->font->info.fontDescent;
        if(height<0 || start<INT32_MIN || start>INT32_MAX || px<INT32_MIN || px>INT32_MAX ||
            py-gc->font->info.fontAscent<INT32_MIN || py-gc->font->info.fontAscent>INT32_MAX ||
            py+gc->font->info.fontDescent<INT32_MIN || py+gc->font->info.fontDescent>INT32_MAX) {
            free(jobs); fail("invalid font extent");
        }
        background=(struct ps2_bitmap_box){coordinate(px<start ? px : start),
            coordinate(py-gc->font->info.fontAscent),coordinate(px>start ? px : start),
            coordinate(py+gc->font->info.fontDescent)};
    }
    uint32_t fg=ps2_gs_pixel_color(gc->fgPixel,storage->bits_per_pixel);
    uint32_t bg=ps2_gs_pixel_color(gc->bgPixel,storage->bits_per_pixel);
    int status;
    if(spans){
        if(ox<INT16_MIN || oy<INT16_MIN || (int64_t)ox+storage->width-1>INT16_MAX ||
            (int64_t)oy+storage->height-1>INT16_MAX){free(jobs);fail("glyph span extent overflow");}
        struct glyph_spans state={drawable,gc,ox,oy,image,gc->fgPixel};
        unsigned alu=gc->alu,fill=gc->fillStyle;
        if(image){gc->alu=GXcopy;gc->fillStyle=FillSolid;}
        status=ps2_bitmap_spans(storage,jobs,count,clips,clip_count,SERVER_MSB_FIRST,
            image?&background:NULL,emit_spans,&state);
        gc->fgPixel=state.foreground;gc->alu=alu;gc->fillStyle=fill;
    }else status=ps2_bitmap_draw(render,target,jobs,count,clips,clip_count,SERVER_MSB_FIRST,
        fg,image ? &background : NULL,bg);
    free(jobs);
    if(status) fail(strerror(errno));
    /* These ops replace the normal EXA/damage text wrappers. Conservatively
     * report the composite clip as damaged, then process pending listeners. */
    exaMarkSync(screen);
    DamageRegionAppend(drawable,gc->pCompositeClip);
    DamageRegionProcessPending(drawable);
}
static void poly(DrawablePtr d,GCPtr gc,int x,int y,unsigned count,CharInfoPtr *characters,void *base)
{ (void)base; glyphs(d,gc,x,y,count,characters,0); }
static void image(DrawablePtr d,GCPtr gc,int x,int y,unsigned count,CharInfoPtr *characters,void *base)
{ (void)base; glyphs(d,gc,x,y,count,characters,1); }
Bool ps2_glyph_gc_attach(GCPtr gc,struct ps2_glyph_gc *b,struct ps2_surface_exa *exa,struct pipe_context *context)
{
    struct ps2_render_client *render=ps2_pipe_context_render(context);
    if(!gc || !b || b->gc || !gc->ops || gc->ops->PolyGlyphBlt==poly || !exa ||
        !render || render->gs!=exa->gs || !ps2_glyph_register_private() ||
        ps2_pipe_context_acquire(context)) return FALSE;
    b->gc=gc; b->original=gc->ops; b->ops=*gc->ops; b->exa=exa; b->context=context;
    b->ops.PolyGlyphBlt=poly; b->ops.ImageGlyphBlt=image;
    dixSetPrivate(&gc->devPrivates,&glyph_key,b); gc->ops=&b->ops;
    return TRUE;
}
void ps2_glyph_gc_detach(struct ps2_glyph_gc *b)
{
    if(!b || !b->gc) return;
    if(b->gc->ops!=&b->ops) fail("GC ops changed before glyph detach");
    b->gc->ops=b->original;
    dixSetPrivate(&b->gc->devPrivates,&glyph_key,NULL);
    struct pipe_context *context=b->context;
    memset(b,0,sizeof(*b)); ps2_pipe_context_release(context);
}
