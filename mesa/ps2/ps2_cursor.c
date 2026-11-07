/* SPDX-License-Identifier: MIT */
#include "ps2_cursor.h"
#include "ps2_gs_render.h"
#include "ps2_gs_logic.h"
#include "ps2_solid_batch.h"
#include "ps2_composite.h"
#include "ps2-gs-map-blit.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
struct image {
    unsigned char source[512], mask[512];
    unsigned width,height,xhot,yhot,msb_first;
    uint32_t foreground,background;
    unsigned colour;
    uint32_t argb[64*64];
};
struct ps2_pipe_cursor {
    struct pipe_context *context;
    struct ps2_surface_client *gs;
    struct image image;
    struct ps2_pipe_solid_run runs[4096];
    unsigned count,width,height,bpp,unit;
    uint32_t saved;
    uint32_t argb_surface;
    struct ps2_pipe_copy_box argb_box;
    int x,y,sx,sy,sw,sh;
    int wanted,visible,suspended,busy;
};
static int front_access(void *data);
static int front_region(void *data,unsigned x,unsigned y,unsigned w,unsigned h);
static int valid(struct ps2_pipe_cursor *c)
{
    if(!c || c->gs->front_access!=front_access || c->gs->front_access_data!=c){errno=EINVAL;return -1;}
    if(c->gs->error){errno=c->gs->error;return -1;}
    if(c->gs->front.width!=c->width || c->gs->front.height!=c->height ||
       c->gs->front.bits_per_pixel!=c->bpp){errno=ENOTSUP;return -1;}
    if(ps2_pipe_context_acquire(c->context))return -1;
    ps2_pipe_context_release(c->context);return 0;
}
static int hide(struct ps2_pipe_cursor *c)
{
    if(!c->visible)return 0;
    /* Save-under is a pixel-preserving transfer, not a textured draw. STQ
     * interpolation can change the sampled texel on physical GS hardware;
     * repeated cursor moves then propagate that change across the desktop.
     * Both ends remain in GS VRAM. Width is rounded to the transfer unit by
     * plan(), including cursor positions clipped at the screen edges. */
    int result=ps2_surface_copy(c->gs,c->saved,0,0,0,c->sx,c->sy,c->sw,c->sh);
    if(!result)c->visible=0;
    return result;
}
static int front_access(void *data)
{
    struct ps2_pipe_cursor *c=data;
    if(c->busy || !c->visible)return 0;
    c->busy=1;int result=hide(c);c->busy=0;return result;
}
static int front_region(void *data,unsigned x,unsigned y,unsigned w,unsigned h)
{
    struct ps2_pipe_cursor *c=data;
    if(c->busy || !c->visible || !w || !h ||
       (uint64_t)x+w<=(unsigned)c->sx || x>=(unsigned)(c->sx+c->sw) ||
       (uint64_t)y+h<=(unsigned)c->sy || y>=(unsigned)(c->sy+c->sh))return 0;
    return front_access(c);
}
struct ps2_pipe_cursor *ps2_pipe_cursor_create(struct pipe_context *context)
{
    struct ps2_render_client *r=ps2_pipe_context_render(context);
    if(!r || !r->gs){errno=EINVAL;return NULL;}
    struct ps2_surface_client *gs=r->gs;
    unsigned unit=gs->front.bits_per_pixel==16?4:2;
    if(gs->front_access || gs->front_region || !gs->front.width || !gs->front.height || gs->front.width%unit ||
       (gs->front.bits_per_pixel!=16 && gs->front.bits_per_pixel!=32) ||
       !(gs->caps.features&PS2GS_HW_SOLID_LOGIC) ||
       !(gs->caps.features&PS2GS_HW_SURFACES)){errno=ENOTSUP;return NULL;}
    if(ps2_pipe_context_acquire(context))return NULL;
    struct ps2_pipe_cursor *c=calloc(1,sizeof(*c));
    if(!c){ps2_pipe_context_release(context);return NULL;}
    c->context=context;c->gs=gs;c->width=gs->front.width;
    c->height=gs->front.height;c->bpp=gs->front.bits_per_pixel;c->unit=unit;
    if(ps2_surface_create(gs,64,64,c->bpp,&c->saved)){
        int error=errno;free(c);ps2_pipe_context_release(context);errno=error;return NULL;
    }
    gs->front_access=front_access;gs->front_region=front_region;gs->front_access_data=c;return c;
}
static int bit(const unsigned char *bytes,unsigned x,unsigned y,unsigned msb)
{ return (bytes[y*8+x/8]>>(msb?7-x%8:x%8))&1; }
static int snapshot(struct ps2_pipe_cursor *c,struct image *out,const struct ps2_cursor_image *in)
{
    uint32_t bits=c->bpp==16?0x7fff:0xffffff;
    if(!in->source || !in->mask || !in->width || !in->height || in->width>64 || in->height>64 ||
       in->xhot>=in->width || in->yhot>=in->height || in->msb_first>1 ||
       ((in->foreground|in->background)&~bits)){errno=EINVAL;return -1;}
    size_t row=(in->width+7)/8;
    if(in->stride<row || (in->height>1 && in->stride>(SIZE_MAX-row)/(in->height-1))){errno=EINVAL;return -1;}
    size_t bytes=(in->height-1)*in->stride+row;
    if(in->source_bytes<bytes || in->mask_bytes<bytes ||
       (uintptr_t)in->source>UINTPTR_MAX-(bytes-1) || (uintptr_t)in->mask>UINTPTR_MAX-(bytes-1)){
        errno=EINVAL;return -1;
    }
    *out=(struct image){.width=in->width,.height=in->height,.xhot=in->xhot,.yhot=in->yhot,
        .msb_first=in->msb_first,.foreground=in->foreground,.background=in->background};
    for(unsigned y=0;y<in->height;++y){
        memcpy(out->source+y*8,in->source+y*in->stride,row);
        memcpy(out->mask+y*8,in->mask+y*in->stride,row);
    }
    return 0;
}
static void plan(struct ps2_pipe_cursor *c,const struct image *im,int x,int y)
{
    c->count=0;int64_t left=(int64_t)x-im->xhot,top=(int64_t)y-im->yhot;
    if(im->colour){
        int64_t a=left<0?0:left,b=left+im->width;
        int64_t t=top<0?0:top,d=top+im->height;
        b=b>c->width?c->width:b;d=d>c->height?c->height:d;
        if(a>=b || t>=d){c->sw=c->sh=0;return;}
        c->argb_box=(struct ps2_pipe_copy_box){a-left,t-top,a,t,b-a,d-t};
        c->count=1;
    }
    else
    for(unsigned j=0;j<im->height;++j){
        int64_t py=top+j;if(py<0 || py>=c->height)continue;
        for(unsigned i=0;i<im->width;){
            unsigned start=i,foreground=bit(im->source,i,j,im->msb_first);
            if(!bit(im->mask,i,j,im->msb_first)){++i;continue;}
            do{++i;}while(i<im->width && bit(im->mask,i,j,im->msb_first) &&
                (unsigned)bit(im->source,i,j,im->msb_first)==foreground);
            int64_t a=left+start,b=left+i;a=a<0?0:a;b=b>c->width?c->width:b;
            if(a>=b)continue;
            c->runs[c->count++]=(struct ps2_pipe_solid_run){.box={a,py,b,py+1},
                .color=foreground?im->foreground:im->background};
        }
    }
    if(!c->count){c->sw=c->sh=0;return;}
    int64_t a=left<0?0:left,b=left+im->width;
    int64_t t=top<0?0:top,d=top+im->height;
    b=b>c->width?c->width:b;d=d>c->height?c->height:d;
    unsigned w=((unsigned)(b-a)+c->unit-1)&~(c->unit-1);
    if(a+w>c->width)a=c->width-w;
    c->sx=a;c->sy=t;c->sw=w;c->sh=d-t;
}
static int redraw(struct ps2_pipe_cursor *c)
{
    if(ps2_render_flush(ps2_pipe_context_render(c->context)))return -1;
    c->busy=1;
    /* plan reuses the saved-position fields, so callers must hide the old
     * cursor before replacing its plan. */
    int result=0;
    if(c->wanted && !c->suspended && c->count){
        result=ps2_surface_copy(c->gs,0,c->saved,c->sx,c->sy,0,0,c->sw,c->sh);
        if(!result){
            if(c->image.colour){
                const struct ps2_picture_sample sample={.source_argb=1,.source_swap=1};
                result=ps2_pipe_composite_rect(c->context,c->argb_surface,0,
                    &c->argb_box,&sample,NULL,PS2_COMPOSITE_OVER);
            }else result=ps2_pipe_solid_runs(c->context,0,c->runs,c->count,
                c->bpp==16?0x7fff:0xffffff,3);
        }
        if(!result)c->visible=1;
    }
    c->busy=0;return result;
}
int ps2_pipe_cursor_invalidate(struct ps2_pipe_cursor *c)
{ if(valid(c))return -1;return front_access(c); }
int ps2_pipe_cursor_invalidate_region(struct ps2_pipe_cursor *c,unsigned x,unsigned y,unsigned w,unsigned h)
{ if(valid(c))return -1;return front_region(c,x,y,w,h); }
static int relocate(struct ps2_pipe_cursor *c,const struct image *image,int wanted,int x,int y)
{
    /* Both flush and hide still use the old save-under coordinates. All image
     * validation and ownership checks have already completed. */
    if(ps2_render_flush(ps2_pipe_context_render(c->context)) || front_access(c))return -1;
    if(image!=&c->image)c->image=*image;
    c->wanted=wanted;c->x=x;c->y=y;
    plan(c,&c->image,x,y);return redraw(c);
}
int ps2_pipe_cursor_set(struct ps2_pipe_cursor *c,const struct ps2_cursor_image *image,int x,int y)
{
    if(valid(c))return -1;
    struct image snapshot_image={0};
    if(image && snapshot(c,&snapshot_image,image))return -1;
    return relocate(c,&snapshot_image,!!image,x,y);
}
int ps2_pipe_cursor_set_argb(struct ps2_pipe_cursor *c,const void *pixels,size_t bytes,
    unsigned width,unsigned height,unsigned xhot,unsigned yhot,int x,int y)
{
    if(valid(c))return -1;
    if(!pixels || !width || !height || width>64 || height>64 || xhot>=width || yhot>=height ||
        bytes<(size_t)width*height*4 || (uintptr_t)pixels>UINTPTR_MAX-((size_t)width*height*4-1)){
        errno=EINVAL;return -1;
    }
    if(!(c->gs->caps.features&PS2GS_HW_CHANNEL_MAP_BLIT)){errno=ENOTSUP;return -1;}
    struct image snapshot_image={.width=width,.height=height,.xhot=xhot,.yhot=yhot,.colour=1};
    for(unsigned row=0;row<height;++row)
        memcpy(snapshot_image.argb+row*64,(const unsigned char *)pixels+row*width*4,width*4);
    if(!c->argb_surface){
        uint32_t surface=0;
        if(ps2_surface_create(c->gs,64,64,32,&surface))return -1;
        if(ps2_surface_set_depth(c->gs,surface,32)){
            int error=errno;
            if(ps2_surface_destroy(c->gs,surface))return -1;
            errno=error;return -1;
        }
        c->argb_surface=surface;
    }
    if(ps2_surface_upload(c->gs,c->argb_surface,0,0,width,height,
        snapshot_image.argb,sizeof(snapshot_image.argb),64*4))return -1;
    return relocate(c,&snapshot_image,1,x,y);
}
int ps2_pipe_cursor_move(struct ps2_pipe_cursor *c,int x,int y)
{ if(valid(c))return -1;return relocate(c,&c->image,c->wanted,x,y); }
int ps2_pipe_cursor_flush(struct ps2_pipe_cursor *c)
{
    if(valid(c) || ps2_render_flush(ps2_pipe_context_render(c->context)))return -1;
    if(c->visible || !c->wanted || c->suspended)return 0;
    plan(c,&c->image,c->x,c->y);return redraw(c);
}
int ps2_pipe_cursor_suspend(struct ps2_pipe_cursor *c,int suspended)
{
    if(suspended!=0 && suspended!=1){errno=EINVAL;return -1;}
    if(valid(c))return -1;
    if(ps2_render_flush(ps2_pipe_context_render(c->context)) || front_access(c))return -1;
    c->suspended=suspended;
    return suspended?0:ps2_pipe_cursor_flush(c);
}
int ps2_pipe_cursor_destroy(struct ps2_pipe_cursor *c)
{
    if(!c)return 0;
    if(c->gs->front_access!=front_access || c->gs->front_access_data!=c){errno=EBUSY;return -1;}
    int result=0,error=0;
    if(!c->gs->error && (ps2_render_flush(ps2_pipe_context_render(c->context)) || front_access(c)))
        {result=-1;error=errno;}
    c->gs->front_access=NULL;c->gs->front_region=NULL;c->gs->front_access_data=NULL;
    if(!c->gs->error && c->argb_surface && ps2_surface_destroy(c->gs,c->argb_surface)){result=-1;error=errno;}
    if(!c->gs->error && ps2_surface_destroy(c->gs,c->saved)){result=-1;error=errno;}
    struct pipe_context *context=c->context;free(c);ps2_pipe_context_release(context);
    if(result)errno=error;
    return result;
}
