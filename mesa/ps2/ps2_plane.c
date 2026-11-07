/* SPDX-License-Identifier: MIT */
#include "ps2_plane.h"
#include "ps2_pipe.h"
#include "ps2_rop.h"
#include "ps2_gs_lut.h"
#include "ps2_gs_logic.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
static uint32_t expanded_colour(uint32_t pixel,unsigned bpp)
{return bpp==32?pixel:((pixel&31)<<3)|(((pixel>>5)&31)<<11)|(((pixel>>10)&31)<<19);}
int ps2_pipe_plane_copy_boxes(struct pipe_context *ctx,uint32_t source,uint32_t destination,
    const struct ps2_pipe_copy_box *input,unsigned count,uint32_t plane,uint32_t foreground,
    uint32_t background,uint32_t mask,unsigned alu)
{
    if(!ctx || count>PS2_PIPE_MAX_COPY_BOXES || (count && !input) || alu>15){errno=EINVAL;return -1;}
    if(ps2_pipe_context_acquire(ctx))return -1;
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);struct ps2_surface_client *gs=r->gs;
    const struct ps2_surface *s=ps2_surface_lookup(gs,source),*d=ps2_surface_lookup(gs,destination);
    struct ps2_rop_workspace w={0},capture={.gs=gs};
    struct ps2_pipe_copy_box *boxes=NULL;uint32_t snapshot=0,palette=0,converted=0;
    int status=-1,error=EINVAL;uint64_t area=0;
    if(!s || !d || !plane || (plane&(plane-1)) || (plane&~ps2_surface_native_mask(gs,source)) ||
        (mask&~ps2_surface_native_mask(gs,destination)))goto out;
    for(unsigned i=0;i<count;++i){const struct ps2_pipe_copy_box *b=input+i;
        if(b->sx<0 || b->sy<0 || b->dx<0 || b->dy<0 || b->width<=0 || b->height<=0 ||
            (int64_t)b->sx+b->width>s->width || (int64_t)b->sy+b->height>s->height ||
            (int64_t)b->dx+b->width>d->width || (int64_t)b->dy+b->height>d->height)goto out;
        area+=(uint64_t)b->width*b->height;if(area>PS2_PIPE_MAX_COPY_PIXELS){error=E2BIG;goto out;}
    }
    if(!count || !mask || alu==5){status=0;goto out;}
    unsigned required=PS2GS_HW_CHANNEL_MAP_BLIT|PS2GS_HW_TEXTURE_TRANSFER|PS2GS_HW_SOLID_LOGIC;
    if((plane&0xff000000) || (mask&0xff000000))required|=PS2GS_HW_RGBA_TRANSFER;
    if((gs->caps.features&required)!=required){error=ENOTSUP;goto out;}
    boxes=malloc(count*sizeof(*boxes));if(!boxes){error=ENOMEM;goto out;}
    memcpy(boxes,input,count*sizeof(*boxes));
    if(ps2_rop_reserve(&w,gs,d->bits_per_pixel) ||
        ps2_surface_create(gs,128,32,s->bits_per_pixel,&capture.source) ||
        ps2_surface_create(gs,16,16,32,&palette) ||
        ps2_surface_create(gs,64,32,d->bits_per_pixel,&converted)){error=errno;goto out;}
    if(d->pixel_depth>=30 && ps2_surface_set_depth(gs,converted,d->pixel_depth)){error=errno;goto out;}
    if(!source || source==destination){
        if(ps2_surface_create(gs,s->width,s->height,s->bits_per_pixel,&snapshot) ||
            ps2_surface_set_depth(gs,snapshot,ps2_surface_depth(s))){error=errno;goto out;}
    }
    status=ps2_render_flush(r);
    if(!status && snapshot){
        struct ps2_pipe_copy_box copy={0,0,0,0,s->width,s->height};
        status=s->pixel_depth>=30 && (plane&0xff000000)?
            ps2_surface_rgba_rectangle(gs,source,snapshot,0,0,0,0,s->width,s->height,ps2_surface_native_mask(gs,source),PS2GS_RGBA_REPLACE):
            ps2_pipe_copy_boxes(ctx,source,snapshot,&copy,1);
        source=snapshot;s=ps2_surface_lookup(gs,source);
    }
    unsigned bit=0;while(!(plane&(1U<<bit)))++bit;
    unsigned channel=s->bits_per_pixel==16?bit/5:bit/8;
    unsigned byte_bit=s->bits_per_pixel==16?bit%5+3:bit%8;
    uint32_t table[256],bits=ps2_surface_native_mask(gs,destination);
    foreground=expanded_colour(foreground&bits,d->bits_per_pixel);
    background=expanded_colour(background&bits,d->bits_per_pixel);
    for(unsigned i=0;i<256;++i)table[i]=i&(1U<<byte_bit)?foreground:background;
    if(!status)status=ps2_surface_map_palette(gs,palette,table);
    for(unsigned i=0;!status && i<count;++i){const struct ps2_pipe_copy_box *b=boxes+i;
        for(unsigned y=0;!status && y<(unsigned)b->height;y+=32)
        for(unsigned x=0;!status && x<(unsigned)b->width;x+=64){
            unsigned width=b->width-x,height=b->height-y,offset=0;
            if(width>64)width=64;
            if(height>32)height=32;
            uint32_t mapped_source=source;unsigned sx=b->sx+x,sy=b->sy+y;
            if(s->bits_per_pixel==16){
                status=ps2_rop_capture(&capture,source,capture.source,sx,sy,width,height,&offset) ||
                    ps2_rop_sample(gs,capture.source,w.a,offset,0,0,0,width,height,0xffffff,0,128)?-1:0;
                mapped_source=w.a;sx=sy=0;
            }
            if(!status)status=ps2_surface_channel_map_region(gs,mapped_source,w.c,palette,sx,sy,0,0,width,height,UINT32_MAX,channel);
            if(!status)status=d->bits_per_pixel==32 && (bits&0xff000000)?
                ps2_surface_rgba_rectangle(gs,w.c,converted,0,0,0,0,width,height,bits,PS2GS_RGBA_REPLACE):
                ps2_rop_sample(gs,w.c,converted,0,0,0,0,width,height,bits,0,128);
            if(!status)status=ps2_rop_rectangle(&w,converted,destination,0,0,b->dx+x,b->dy+y,width,height,mask,alu);
        }
    }
    error=errno;
out:
    uint32_t handles[]={converted,palette,snapshot};
    for(unsigned i=0;i<sizeof(handles)/sizeof(*handles);++i)
        if(handles[i] && !gs->error && ps2_surface_destroy(gs,handles[i]) && !status){status=-1;error=errno;}
    if(capture.source && ps2_rop_release(&capture) && !status){status=-1;error=errno;}
    if(w.gs && ps2_rop_release(&w) && !status){status=-1;error=errno;}
    free(boxes);ps2_pipe_context_release(ctx);if(status)errno=error;return status;
}
