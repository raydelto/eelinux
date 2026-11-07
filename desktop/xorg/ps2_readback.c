/* SPDX-License-Identifier: MIT */
#include "ps2_readback.h"
#include "ps2_rop.h"
#include "ps2_cursor_screen.h"
#include "ps2_coverage.h"
#include "windowstr.h"
#include "servermd.h"
#include "regionstr.h"
#include "os.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
RegionPtr ps2_readback_bitmap_region(struct pipe_context *ctx,PixmapPtr pixmap)
{
    struct ps2_mono_source source;
    if(!ctx || !pixmap || pixmap->drawable.depth!=1 || pixmap->drawable.bitsPerPixel!=1 ||
       pixmap->drawable.x || pixmap->drawable.y ||
       ps2_surface_exa_mono_source(pixmap,&source)!=1 || source.width>2048 || source.height>2048)
        FatalError("PS2 GS: invalid owned bitmap region source\n");
    if(!source.width || !source.height)return RegionCreate(NULL,0);
    size_t row=(source.width+7)/8;
    if(!source.bits || source.stride<row ||
       source.stride>SIZE_MAX/(source.height-1?source.height-1:1) ||
       (source.height-1)*source.stride>source.bytes ||
       row>source.bytes-(source.height-1)*source.stride)
        FatalError("PS2 GS: invalid bitmap region storage\n");
    if(ps2_render_flush(ps2_pipe_context_render(ctx)))
        FatalError("PS2 GS: bitmap region synchronization failed\n");
    /* Shape masks and GC clip masks are converted to rectangle geometry here.
     * The immutable protocol bits were uploaded or drawn/packed by GS. This
     * constructs clipping metadata only: it never maps a raster destination,
     * calls EXA PrepareAccess, or draws/filters/blends pixels on the CPU. */
    xRectangle *rects=NULL;
    unsigned count=0,capacity=0;
    for(unsigned y=0;y<source.height;++y){
        const unsigned char *line=source.bits+y*source.stride;
        unsigned x=0;
        while(x<source.width){
            unsigned mask=BITMAP_BIT_ORDER==MSBFirst?0x80U>>(x%8):1U<<(x%8);
            if(!(line[x/8]&mask)){++x;continue;}
            unsigned start=x++;
            while(x<source.width){
                mask=BITMAP_BIT_ORDER==MSBFirst?0x80U>>(x%8):1U<<(x%8);
                if(!(line[x/8]&mask))break;
                ++x;
            }
            /* Bound temporary geometry storage to 2 MiB in a 32 MiB guest.
             * Exhaustion is reported as BadAlloc by the server, without a
             * software rendering path or changes to the source bitmap. */
            if(count==capacity){
                unsigned next=capacity?capacity*2:64;
                if(next>262144){free(rects);return NULL;}
                xRectangle *grown=realloc(rects,(size_t)next*sizeof(*rects));
                if(!grown){free(rects);return NULL;}
                rects=grown;capacity=next;
            }
            rects[count++]=(xRectangle){start,y,x-start,1};
        }
    }
    RegionPtr region=RegionFromRects(count,rects,YXBanded);
    free(rects);
    return region;
}
struct view {uint32_t handle;int ox,oy;unsigned bpp,width,height;};
static void coverage_view(struct ps2_coverage_surface *c,struct pipe_context *ctx,DrawablePtr d)
{
    struct ps2_alpha_source bytes;
    if(d->type!=DRAWABLE_PIXMAP || d->x || d->y || ps2_surface_exa_alpha_source((PixmapPtr)d,&bytes)!=1)
        FatalError("PS2 GS: invalid owned coverage reply source\n");
    struct ps2_picture_bitmap bitmap={.bits=bytes.bits,.bytes=bytes.bytes,.stride=bytes.stride,
        .width=bytes.width,.height=bytes.height,.bits_per_pixel=bytes.bits_per_pixel,.alpha_bits=bytes.depth,
        .msb_first=BITMAP_BIT_ORDER==MSBFirst};
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);
    if(ps2_render_flush(r) || ps2_coverage_decode_core(c,r->gs,&bitmap))
        FatalError("PS2 GS: coverage reply import failed\n");
}
static void coverage_reply(struct ps2_coverage_surface *c,struct pipe_context *ctx,int x,int y,
    unsigned width,unsigned height,uint32_t mask,unsigned bpp,char *out,size_t stride)
{
    if(x<0 || y<0 || (uint64_t)x+width>c->width || (uint64_t)y+height>c->height)
        FatalError("PS2 GS: invalid coverage reply geometry\n");
    uint32_t row[1024];mask&=(1U<<c->alpha_bits)-1;
    memset(out,0,stride*height);
    for(unsigned line=0;line<height;++line){
        if(ps2_pipe_download(ctx,c->surface,x,y+line,width,1,mask,row,width*4,width*4))
            FatalError("PS2 GS: coverage reply readback failed\n");
        /* Masking was performed on GS. The CPU only serializes the reply. */
        for(unsigned column=0;column<width;++column){
            if(bpp>=8)memcpy(out+line*stride+column*(bpp/8),row+column,bpp/8);
            else if(row[column])out[line*stride+column/8]|=BITMAP_BIT_ORDER==MSBFirst?0x80U>>(column%8):1U<<(column%8);
        }
    }
}
static struct view view(struct pipe_context *ctx,DrawablePtr d)
{
    ScreenPtr screen=d->pScreen;
    PixmapPtr p=d->type==DRAWABLE_PIXMAP?(PixmapPtr)d:
        d->type==DRAWABLE_WINDOW && screen->GetWindowPixmap?screen->GetWindowPixmap((WindowPtr)d):NULL;
    struct view v={0};
    if(!p || !screen->GetScreenPixmap)FatalError("PS2 GS: missing readback storage\n");
    if(p!=screen->GetScreenPixmap(screen)){
        const void *private=exaGetPixmapDriverPrivate(p);
        if(!private)FatalError("PS2 GS: missing owned readback pixmap\n");
        memcpy(&v.handle,private,4);if(!v.handle)FatalError("PS2 GS: unallocated readback pixmap\n");
    }
    const struct ps2_surface *s=ps2_surface_lookup(ps2_pipe_context_render(ctx)->gs,v.handle);
    if(!s || p->drawable.width!=s->width || p->drawable.height!=s->height ||
        p->drawable.bitsPerPixel!=s->bits_per_pixel || d->bitsPerPixel!=s->bits_per_pixel ||
        d->depth!=ps2_surface_depth(s))FatalError("PS2 GS: stale readback storage\n");
    v.width=s->width;v.height=s->height;v.bpp=s->bits_per_pixel;
#ifdef COMPOSITE
    if(d->type==DRAWABLE_WINDOW){v.ox=p->screen_x;v.oy=p->screen_y;}
#endif
    return v;
}
static void download(struct pipe_context *ctx,const struct view *v,int64_t x,int64_t y,
    unsigned w,unsigned h,uint32_t mask,void *out,size_t bytes,size_t stride)
{
    if(x<0 || y<0 || x+w>v->width || y+h>v->height ||
        ps2_pipe_download(ctx,v->handle,x,y,w,h,mask,out,bytes,stride))
        FatalError("PS2 GS: native client readback failed: handle=%u box=%lld,%lld %ux%u bpp=%u: %s\n",
            v->handle,(long long)x,(long long)y,w,h,v->bpp,strerror(errno?errno:EINVAL));
}
void ps2_readback_image(struct pipe_context *ctx,DrawablePtr d,int x,int y,int w,int h,
    unsigned format,unsigned long mask,char *out)
{
    if(!d || (w && h && !out) || w<0 || h<0 || w>2048 || h>2048 || (format!=ZPixmap && format!=XYPixmap))
        FatalError("PS2 GS: invalid GetImage request\n");
    if(!w || !h)return;
    if(d->depth==1 || d->depth==4 || d->depth==8 || d->depth==12 || d->depth==16){
        if(format==XYPixmap && (mask&(mask-1)))FatalError("PS2 GS: coverage XY reply requires one plane\n");
        struct ps2_coverage_surface c={0};coverage_view(&c,ctx,d);
        coverage_reply(&c,ctx,x,y,w,h,mask,format==XYPixmap?1:d->bitsPerPixel,out,
            format==XYPixmap?BitmapBytePad(w):PixmapBytePad(w,d->depth));
        if(ps2_coverage_release(&c))FatalError("PS2 GS: coverage reply release failed\n");
        return;
    }
    struct view v=view(ctx,d);mask&=ps2_surface_native_mask(ps2_pipe_context_render(ctx)->gs,v.handle);
    int64_t sx=(int64_t)d->x+x-v.ox,sy=(int64_t)d->y+y-v.oy;
    if(format==ZPixmap){size_t stride=PixmapBytePad(w,d->depth);memset(out,0,stride*h);
        download(ctx,&v,sx,sy,w,h,mask,out,stride*h,stride);
        if(!v.handle)ps2_cursor_screen_refresh(d->pScreen);
        return;}
    /* Xorg calls this once per selected XY plane. Packing a client reply is
     * serialization, not destination rendering. The plane is isolated on GS. */
    if(mask&(mask-1))FatalError("PS2 GS: GetImage XY reply requires one plane\n");
    size_t stride=BitmapBytePad(w),native_row=(size_t)w*(v.bpp/8);
    unsigned char row[8192];memset(out,0,stride*h);
    if(!mask)return;
    for(int line=0;line<h;++line){download(ctx,&v,sx,sy+line,w,1,mask,row,native_row,native_row);
        for(int column=0;column<w;++column){uint32_t pixel=0;memcpy(&pixel,row+column*(v.bpp/8),v.bpp/8);
            if(pixel)out[line*stride+column/8]|=BITMAP_BIT_ORDER==MSBFirst?0x80U>>(column%8):1U<<(column%8);}}
    if(!v.handle)ps2_cursor_screen_refresh(d->pScreen);
}
void ps2_readback_spans(struct pipe_context *ctx,DrawablePtr d,int max_width,
    DDXPointPtr points,int *widths,int count,char *out)
{
    if(!d || (count && !out) || count<0 || count>4096 || max_width<0 || max_width>2048 ||
        (count && (!points || !widths)))FatalError("PS2 GS: invalid GetSpans request\n");
    if(!count)return;
    if(d->depth==1 || d->depth==4 || d->depth==8 || d->depth==12 || d->depth==16){
        struct ps2_coverage_surface c={0};coverage_view(&c,ctx,d);size_t bytes=0;
        for(int i=0;i<count;++i){
            if(widths[i]<0 || widths[i]>max_width)FatalError("PS2 GS: invalid coverage reply span width\n");
            size_t stride=PixmapBytePad(widths[i],d->depth);bytes+=stride;
            if(bytes>16U*1024*1024)FatalError("PS2 GS: coverage span reply budget exceeded\n");
            if(widths[i])coverage_reply(&c,ctx,points[i].x,points[i].y,widths[i],1,(1U<<d->depth)-1,d->bitsPerPixel,out,stride);
            out+=stride;
        }
        if(ps2_coverage_release(&c))FatalError("PS2 GS: coverage span reply release failed\n");
        return;
    }
    struct view v=view(ctx,d);
    size_t bytes=0;
    for(int i=0;i<count;++i){
        if(widths[i]<0 || widths[i]>max_width ||
        (int64_t)points[i].x-v.ox<0 || (int64_t)points[i].y-v.oy<0 ||
        (int64_t)points[i].x-v.ox+widths[i]>v.width || (int64_t)points[i].y-v.oy>=v.height){
            FatalError("PS2 GS: invalid source span geometry\n");
        }
        bytes+=PixmapBytePad(widths[i],d->depth);if(bytes>16U*1024*1024)FatalError("PS2 GS: span reply budget exceeded\n");}
    for(int i=0;i<count;++i){size_t row=PixmapBytePad(widths[i],d->depth);memset(out,0,row);
        if(widths[i])download(ctx,&v,(int64_t)points[i].x-v.ox,(int64_t)points[i].y-v.oy,widths[i],1,
            ps2_surface_native_mask(ps2_pipe_context_render(ctx)->gs,v.handle),out,row,row);out+=row;}
    if(!v.handle)ps2_cursor_screen_refresh(d->pScreen);
}
