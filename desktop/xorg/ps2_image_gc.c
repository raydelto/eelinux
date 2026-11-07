/* SPDX-License-Identifier: MIT */
#include "ps2_image_gc.h"
#include "ps2_image.h"
#include "ps2_planar.h"
#include "ps2_plane.h"
#include "ps2_coverage.h"
#include "ps2_cursor_screen.h"
#include "ps2_readback.h"
#include "ps2_gs_logic.h"
#include "windowstr.h"
#include "regionstr.h"
#include "damage.h"
#include "servermd.h"
#include "os.h"
#include "mi.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
static DevPrivateKeyRec key;
static const GCOps operations;
static Bool close_screen(ScreenPtr);
struct plan {
    struct ps2_image_screen *owner;
    DrawablePtr drawable;
    GCPtr gc;
    struct ps2_pipe_image_box *boxes;
    struct ps2_pipe_planar_box *planar_boxes;
    struct ps2_pipe_planar_source planar;
    unsigned left_pad;
    unsigned count,width,height,bpp;
    uint32_t destination,mask;
    int64_t ox,oy;
};
static void fail(struct plan *p,const char *reason)
{if(p){free(p->boxes);free(p->planar_boxes);}FatalError("PS2 GS: native client image failed: %s\n",reason);}
static void begin(struct plan *p,DrawablePtr d,GCPtr gc)
{
    *p=(struct plan){.drawable=d,.gc=gc};
    if(!d || !gc || !d->pScreen || d->pScreen!=gc->pScreen || !gc->pCompositeClip ||
       RegionBroken(gc->pCompositeClip) || gc->alu>GXset)fail(p,"invalid drawable or GC");
    p->owner=dixLookupPrivate(&d->pScreen->devPrivates,&key);
    struct ps2_image_screen *b=p->owner;
    if(!b || !b->installed || !b->gc->installed || b->gc->image_ops!=&operations ||
       ps2_pipe_context_acquire(b->context))fail(p,"missing or closed image owner");
    ps2_pipe_context_release(b->context);
    ScreenPtr screen=d->pScreen;
    PixmapPtr pixmap=d->type==DRAWABLE_PIXMAP?(PixmapPtr)d:
        d->type==DRAWABLE_WINDOW && screen->GetWindowPixmap?screen->GetWindowPixmap((WindowPtr)d):NULL;
    if(!pixmap || pixmap->drawable.pScreen!=screen || !screen->GetScreenPixmap || pixmap->devPrivate.ptr)
        fail(p,"missing native colour storage");
    if(pixmap!=screen->GetScreenPixmap(screen)){
        const void *private=exaGetPixmapDriverPrivate(pixmap);
        if(!private)fail(p,"missing owned destination");
        memcpy(&p->destination,private,4);
        if(!p->destination)fail(p,"unallocated destination");
    }
    struct ps2_render_client *r=ps2_pipe_context_render(b->context);
    const struct ps2_surface *s=r?ps2_surface_lookup(r->gs,p->destination):NULL;
    if(!s || r->gs!=b->gc->exa->gs || (s->bits_per_pixel!=16 && s->bits_per_pixel!=32) ||
       pixmap->drawable.width!=s->width || pixmap->drawable.height!=s->height ||
       pixmap->drawable.bitsPerPixel!=s->bits_per_pixel || d->bitsPerPixel!=s->bits_per_pixel ||
       d->depth!=ps2_surface_depth(s) || pixmap->drawable.depth!=d->depth)
        fail(p,"incompatible image destination");
    p->width=s->width;p->height=s->height;p->bpp=s->bits_per_pixel;
    p->mask=gc->planemask&ps2_surface_native_mask(r->gs,p->destination);
#ifdef COMPOSITE
    if(d->type==DRAWABLE_WINDOW){p->ox=pixmap->screen_x;p->oy=pixmap->screen_y;}
#endif
    int n=RegionNumRects(gc->pCompositeClip);const BoxRec *boxes=RegionRects(gc->pCompositeClip);
    if(n<0 || n>256)fail(p,"clip complexity exceeds native limit");
    for(int i=0;i<n;++i)if(boxes[i].x2<boxes[i].x1 || boxes[i].y2<boxes[i].y1)
        fail(p,"invalid image clip box");
}
static int64_t maximum(int64_t a,int64_t b){return a>b?a:b;}
static int64_t minimum(int64_t a,int64_t b){return a<b?a:b;}
static void append(struct plan *p,int64_t x,int64_t y,unsigned w,unsigned h,size_t offset,size_t stride)
{
    if(x<INT32_MIN || y<INT32_MIN || x+w>INT32_MAX || y+h>INT32_MAX)
        fail(p,"image coordinate overflow");
    if(!w || !h)return;
    const BoxRec *clips=RegionRects(p->gc->pCompositeClip);int n=RegionNumRects(p->gc->pCompositeClip);
    for(int i=0;i<n;++i){
        int64_t a=maximum(maximum(x,clips[i].x1-p->ox),0),b=maximum(maximum(y,clips[i].y1-p->oy),0);
        int64_t c=minimum(minimum(x+w,clips[i].x2-p->ox),p->width),d=minimum(minimum(y+h,clips[i].y2-p->oy),p->height);
        if(a>=c || b>=d)continue;
        if(p->count==PS2_PIPE_MAX_IMAGE_BOXES)fail(p,"clipped image plan exceeds limit");
        if(p->planar.depth){
            if(!p->planar_boxes && !(p->planar_boxes=calloc(PS2_PIPE_MAX_PLANAR_BOXES,sizeof(*p->planar_boxes))))
                fail(p,"planar image allocation");
            p->planar_boxes[p->count++]=(struct ps2_pipe_planar_box){p->left_pad+a-x,b-y,a,b,c-a,d-b};
        }else{
            if(!p->boxes && !(p->boxes=calloc(PS2_PIPE_MAX_IMAGE_BOXES,sizeof(*p->boxes))))
                fail(p,"image plan allocation");
            p->boxes[p->count++]=(struct ps2_pipe_image_box){offset+(b-y)*stride+(a-x)*(p->bpp/8),stride,a,b,c-a,d-b};
        }
    }
}
static void finish(struct plan *p,const void *source,size_t bytes)
{
    if(bytes>PS2_PIPE_MAX_IMAGE_BYTES || (bytes && (!source || (uintptr_t)source>UINTPTR_MAX-(bytes-1))))
        fail(p,"image source length or address overflow");
    if(!p->count || !p->mask || p->gc->alu==GXnoop){free(p->boxes);free(p->planar_boxes);return;}
    p->planar.bits=source;p->planar.bytes=bytes;
    int status=p->planar.depth?ps2_pipe_planar_boxes(p->owner->context,p->destination,p->bpp,&p->planar,
        p->planar_boxes,p->count,p->gc->fgPixel,p->gc->bgPixel,p->mask,p->gc->alu):
        ps2_pipe_image_boxes(p->owner->context,p->destination,p->bpp,source,bytes,
        p->boxes,p->count,p->mask,p->gc->alu);
    if(status)fail(p,strerror(errno));
    free(p->boxes);free(p->planar_boxes);
    exaMarkSync(p->drawable->pScreen);
    DamageRegionAppend(p->drawable,p->gc->pCompositeClip);DamageRegionProcessPending(p->drawable);
}
static void put_image(DrawablePtr d,GCPtr gc,int depth,int x,int y,int w,int h,int pad,int format,char *source)
{
    struct plan p;begin(&p,d,gc);
    if(w<0 || h<0 || w>2048 || h>2048)
        fail(&p,"unsupported image format or dimensions");
    size_t stride,bytes;
    if(format==ZPixmap){
        if(depth!=d->depth || pad)fail(&p,"invalid ZPixmap depth or left pad");
        stride=((size_t)w*(p.bpp/8)+3)&~(size_t)3;bytes=stride*h;
    }else if(format==XYBitmap || format==XYPixmap){
        if(depth!=(format==XYBitmap?1:d->depth) || pad<0 || pad>=BITMAP_SCANLINE_PAD)
            fail(&p,"invalid XY image depth or left pad");
        stride=BitmapBytePad(w+pad);bytes=stride*h*depth;p.left_pad=pad;
        p.planar=(struct ps2_pipe_planar_source){.stride=stride,.plane_stride=stride*h,
            .width=w+pad,.height=h,.depth=depth,.msb_first=BITMAP_BIT_ORDER==MSBFirst};
    }else{fail(&p,"unsupported image format");return;}
    if(bytes>PS2_PIPE_MAX_IMAGE_BYTES)fail(&p,"image source budget exceeded");
    /* PutImage is drawable-relative even when miTranslate is false. */
    append(&p,(int64_t)x+d->x-p.ox,(int64_t)y+d->y-p.oy,w,h,0,stride);
    finish(&p,source,bytes);
}
static void set_spans(DrawablePtr d,GCPtr gc,char *source,DDXPointPtr points,int *widths,int n,int sorted)
{
    (void)sorted;struct plan p;begin(&p,d,gc);
    if(n<0 || n>4096 || (n && (!points || !widths)))fail(&p,"invalid span inputs");
    size_t bytes=0;
    for(int i=0;i<n;++i){
        if(widths[i]<0 || widths[i]>2048)fail(&p,"invalid source span width");
        size_t stride=((size_t)widths[i]*(p.bpp/8)+3)&~(size_t)3;
        if(stride>PS2_PIPE_MAX_IMAGE_BYTES-bytes)fail(&p,"span source budget exceeded");
        /* SetSpans points are already translated; preserve input overlap order. */
        append(&p,(int64_t)points[i].x-p.ox,(int64_t)points[i].y-p.oy,widths[i],1,bytes,stride);
        bytes+=stride;
    }
    finish(&p,source,bytes);
}
struct plane_request {struct plan plan;int sx,sy;const unsigned char *bits;size_t bytes;uint32_t source;Bool colour;};
static void plane_region(DrawablePtr source,DrawablePtr d,GCPtr gc,BoxPtr boxes,int n,
    int dx,int dy,Bool reverse,Bool upside,Pixel plane,void *closure)
{
    (void)source;(void)reverse;(void)upside;
    struct plane_request *r=closure;struct plan *p=&r->plan;
    if(n<0 || n>(int)PS2_PIPE_MAX_PLANAR_BOXES || (n && !boxes))fail(p,"CopyPlane clip complexity");
    if(r->colour){
        if(n>(int)PS2_PIPE_MAX_COPY_BOXES)fail(p,"colour CopyPlane clip complexity");
        struct ps2_pipe_copy_box *jobs=n?malloc(n*sizeof(*jobs)):NULL;
        if(n && !jobs)fail(p,"colour CopyPlane allocation");
        for(int i=0;i<n;++i)jobs[i]=(struct ps2_pipe_copy_box){
            boxes[i].x1+dx-r->sx,boxes[i].y1+dy-r->sy,
            boxes[i].x1-p->ox,boxes[i].y1-p->oy,boxes[i].x2-boxes[i].x1,boxes[i].y2-boxes[i].y1};
        int status=ps2_pipe_plane_copy_boxes(p->owner->context,r->source,p->destination,jobs,n,
            plane,gc->fgPixel,gc->bgPixel,p->mask,gc->alu);
        free(jobs);
        if(status)fail(p,strerror(errno));
        if(n && p->mask && gc->alu!=GXnoop){exaMarkSync(d->pScreen);
            DamageRegionAppend(d,gc->pCompositeClip);DamageRegionProcessPending(d);}
        if(!r->source || !p->destination)ps2_cursor_screen_refresh(d->pScreen);
        return;
    }
    p->planar_boxes=n?calloc(n,sizeof(*p->planar_boxes)):NULL;if(n && !p->planar_boxes)fail(p,"CopyPlane allocation");
    for(int i=0;i<n;++i)p->planar_boxes[i]=(struct ps2_pipe_planar_box){
        .source_x=(int)boxes[i].x1+dx-r->sx,.source_y=(int)boxes[i].y1+dy-r->sy,
        .x=boxes[i].x1-p->ox,.y=boxes[i].y1-p->oy,
        .width=(int)boxes[i].x2-boxes[i].x1,.height=(int)boxes[i].y2-boxes[i].y1};
    p->count=n;finish(p,r->bits,r->bytes);p->count=0;p->planar_boxes=NULL;
}
static RegionPtr copy_plane(DrawablePtr source,DrawablePtr d,GCPtr gc,int x,int y,int w,int h,int dx,int dy,unsigned long plane)
{
    struct plane_request r={0};
    int64_t sx=source?(int64_t)source->x+x:0,sy=source?(int64_t)source->y+y:0;
    int64_t tx=d?(int64_t)d->x+dx:0,ty=d?(int64_t)d->y+dy:0;
    if(!source || !d || !gc || !d->pScreen || d->pScreen!=gc->pScreen ||
       !d->pScreen->SourceValidate || source->pScreen!=d->pScreen || source->depth>32 ||
       !plane || (plane&(plane-1)) || (uint64_t)plane>=(UINT64_C(1)<<source->depth) || w<0 || h<0 || w>2048 || h>2048 ||
       sx<INT16_MIN || sy<INT16_MIN || sx+w>INT16_MAX || sy+h>INT16_MAX ||
       tx<INT16_MIN || ty<INT16_MIN || tx+w>INT16_MAX || ty+h>INT16_MAX)
        fail(&r.plan,"invalid CopyPlane source or geometry");
    if(d->depth==1 || d->depth==4 || d->depth==8 || d->depth==12 || d->depth==16)
        FatalError("PS2 GS: raw CopyPlane must use the native GC bridge\n");
    begin(&r.plan,d,gc);
    if(source->depth==4 || source->depth==8 || source->depth==12 || source->depth==16){
        struct ps2_alpha_source bytes;
        if(source->type!=DRAWABLE_PIXMAP || (source->bitsPerPixel!=8 && source->bitsPerPixel!=16) ||
            ps2_surface_exa_alpha_source((PixmapPtr)source,&bytes)!=1)
            fail(&r.plan,"invalid owned raw CopyPlane source");
        struct ps2_coverage_surface decoded={0};
        struct ps2_picture_bitmap bitmap={.bits=bytes.bits,.bytes=bytes.bytes,.stride=bytes.stride,
            .width=bytes.width,.height=bytes.height,.bits_per_pixel=bytes.bits_per_pixel,.alpha_bits=bytes.depth};
        struct ps2_surface_client *gs=ps2_pipe_context_render(r.plan.owner->context)->gs;
        if(ps2_render_flush(ps2_pipe_context_render(r.plan.owner->context)) ||
            ps2_coverage_decode_core(&decoded,gs,&bitmap))fail(&r.plan,"GS A8 CopyPlane import failed");
        r.source=decoded.surface;r.colour=TRUE;r.sx=source->x;r.sy=source->y;
        RegionPtr result=miDoCopy(source,d,gc,x,y,w,h,dx,dy,plane_region,plane,&r);
        if(ps2_coverage_release(&decoded))fail(&r.plan,"GS A8 CopyPlane release failed");
        return result;
    }
    if(source->depth!=1){
        ScreenPtr screen=d->pScreen;
        PixmapPtr pixmap=source->type==DRAWABLE_PIXMAP?(PixmapPtr)source:
            source->type==DRAWABLE_WINDOW && screen->GetWindowPixmap?screen->GetWindowPixmap((WindowPtr)source):NULL;
        if(!pixmap || pixmap->devPrivate.ptr || !screen->GetScreenPixmap)fail(&r.plan,"missing colour CopyPlane storage");
        if(pixmap!=screen->GetScreenPixmap(screen)){
            const void *private=exaGetPixmapDriverPrivate(pixmap);
            if(!private)fail(&r.plan,"missing colour CopyPlane source");
            memcpy(&r.source,private,4);if(!r.source)fail(&r.plan,"unallocated colour CopyPlane source");
        }
        const struct ps2_surface *s=ps2_surface_lookup(ps2_pipe_context_render(r.plan.owner->context)->gs,r.source);
        if(!s || pixmap->drawable.width!=s->width || pixmap->drawable.height!=s->height ||
           pixmap->drawable.bitsPerPixel!=s->bits_per_pixel || source->bitsPerPixel!=s->bits_per_pixel ||
           source->depth!=ps2_surface_depth(s))fail(&r.plan,"incompatible colour CopyPlane source");
#ifdef COMPOSITE
        if(source->type==DRAWABLE_WINDOW){r.sx=pixmap->screen_x;r.sy=pixmap->screen_y;}
#endif
        r.colour=TRUE;
        return miDoCopy(source,d,gc,x,y,w,h,dx,dy,plane_region,plane,&r);
    }
    if(source->type!=DRAWABLE_PIXMAP || source->bitsPerPixel!=1)fail(&r.plan,"invalid monochrome CopyPlane source");
    PixmapPtr pixmap=(PixmapPtr)source;struct ps2_mono_source mono={0};
    int owned=ps2_surface_exa_mono_source(pixmap,&mono);if(owned<0)fail(&r.plan,"invalid owned bitmap");
    if(!owned){if(!pixmap->devPrivate.ptr || pixmap->devKind<BitmapBytePad(source->width))fail(&r.plan,"invalid source bitmap");
        mono=(struct ps2_mono_source){pixmap->devPrivate.ptr,(size_t)pixmap->devKind*source->height,pixmap->devKind,source->width,source->height};}
    r.plan.planar=(struct ps2_pipe_planar_source){.stride=mono.stride,.plane_stride=mono.bytes,
        .width=mono.width,.height=mono.height,.depth=1,.msb_first=BITMAP_BIT_ORDER==MSBFirst};
    r.sx=source->x;r.sy=source->y;r.bits=mono.bits;r.bytes=mono.bytes;
    return miDoCopy(source,d,gc,x,y,w,h,dx,dy,plane_region,plane,&r);
}
static const GCOps operations={.PutImage=put_image,.SetSpans=set_spans,.CopyPlane=copy_plane};
Bool ps2_image_screen_prepare(ScreenPtr screen,struct ps2_image_screen *b,struct ps2_gc_screen *gc)
{
    struct ps2_render_client *r=gc?ps2_pipe_context_render(gc->context):NULL;
    if(!screen || !b || b->installed || !gc || !gc->installed || gc->screen!=screen || gc->image_ops || gc->plane_ops ||
       !screen->CloseScreen || screen->CloseScreen==close_screen || !r || r->gs!=gc->exa->gs ||
       !(r->gs->caps.features&PS2GS_HW_UPLOAD) || !(r->gs->caps.features&PS2GS_HW_SOLID_LOGIC) ||
       !dixRegisterPrivateKey(&key,PRIVATE_SCREEN,0) || dixLookupPrivate(&screen->devPrivates,&key) ||
       ps2_pipe_context_acquire(gc->context))return FALSE;
    *b=(struct ps2_image_screen){.screen=screen,.gc=gc,.context=gc->context,.CloseScreen=screen->CloseScreen,.installed=TRUE};
    dixSetPrivate(&screen->devPrivates,&key,b);screen->CloseScreen=close_screen;
    if(!ps2_gc_screen_set_image_ops(gc,&operations) || !ps2_gc_screen_set_plane_ops(gc,&operations))FatalError("PS2 GS: image GC installation failed\n");
    return TRUE;
}
void ps2_image_screen_cleanup(struct ps2_image_screen *b)
{
    if(!b || !b->installed)return;
    if(b->screen->CloseScreen!=close_screen || b->gc->image_ops!=&operations || b->gc->plane_ops!=&operations ||
       dixLookupPrivate(&b->screen->devPrivates,&key)!=b || !ps2_gc_screen_set_image_ops(b->gc,NULL) || !ps2_gc_screen_set_plane_ops(b->gc,NULL))
        FatalError("PS2 GS: image GC cleanup order\n");
    b->screen->CloseScreen=b->CloseScreen;dixSetPrivate(&b->screen->devPrivates,&key,NULL);
    struct pipe_context *context=b->context;memset(b,0,sizeof(*b));ps2_pipe_context_release(context);
}
static Bool close_screen(ScreenPtr screen)
{
    struct ps2_image_screen *b=dixLookupPrivate(&screen->devPrivates,&key);
    if(!b || !b->installed)FatalError("PS2 GS: missing image screen owner\n");
    CloseScreenProcPtr lower=b->CloseScreen;ps2_image_screen_cleanup(b);return lower(screen);
}
