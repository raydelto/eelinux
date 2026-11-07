/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "ps2_gs_lut.h"
#include "ps2_gs_transfer.h"
#include <errno.h>
#include <sys/ioctl.h>
#include <time.h>
_Static_assert(sizeof(struct ps2gs_channel_lut) == 32, "channel lookup ABI");
_Static_assert(sizeof(struct ps2gs_channel_map) == 32, "channel mapping ABI");
_Static_assert(sizeof(struct ps2gs_channel_map_blit) == 48, "channel mapping blit ABI");
int ps2_surface_map_palette(struct ps2_surface_client *gs, uint32_t palette, const uint32_t table[256])
{
    const struct ps2_surface *p=ps2_surface_lookup(gs,palette);
    if (!table || !palette || !p || p->width!=16 || p->height!=16 || p->bits_per_pixel!=32) {
        errno=EINVAL;return -1;
    }
    uint32_t pixels[256];
    for(unsigned i=0;i<256;++i){
        unsigned index=(i&~24U)|((i&8)<<1)|((i&16)>>1);
        pixels[index]=table[i];
    }
    return ps2_surface_upload(gs,palette,0,0,16,16,pixels,sizeof(pixels),64);
}
int ps2_surface_lut_palette(struct ps2_surface_client *gs, uint32_t palette, const unsigned char table[256])
{
    const struct ps2_surface *p=ps2_surface_lookup(gs,palette);
    if (!table || !palette || !p || p->width!=16 || p->height!=16 || p->bits_per_pixel!=32) {
        errno=EINVAL;return -1;
    }
    uint32_t pixels[256];
    for(unsigned i=0;i<256;++i){
        unsigned index=(i&~24U)|((i&8)<<1)|((i&16)>>1);
        pixels[index]=0x80000000U|table[i]|(uint32_t)table[i]<<8|(uint32_t)table[i]<<16;
    }
    return ps2_surface_upload(gs,palette,0,0,16,16,pixels,sizeof(pixels),64);
}
int ps2_surface_channel_lut(struct ps2_surface_client *gs, uint32_t source,
    uint32_t destination, uint32_t palette, unsigned width, unsigned height, uint32_t mask)
{
    if(gs->error){errno=gs->error;return -1;}
    const struct ps2_surface *s=ps2_surface_lookup(gs,source),*d=ps2_surface_lookup(gs,destination),
        *p=ps2_surface_lookup(gs,palette);
    if(!(gs->caps.features&PS2GS_HW_CHANNEL_LUT)){errno=ENOTSUP;return -1;}
    if(!source || !destination || !palette || source==destination || source==palette || destination==palette ||
        !s || !d || !p || s->width!=64 || s->height!=32 || d->width!=64 || d->height!=32 ||
        s->bits_per_pixel!=32 || d->bits_per_pixel!=32 || p->bits_per_pixel!=32 || p->width!=16 || p->height!=16 ||
        !width || !height || width>64 || height>32 || (mask&~0xffffffU)){errno=EINVAL;return -1;}
    if(!mask)return 0;
    struct ps2gs_channel_lut request={.abi=PS2GS_ABI_VERSION,.source=source,.destination=destination,
        .palette=palette,.width=width,.height=height,.plane_mask=mask};
    for(unsigned attempt=0;attempt<100;++attempt){
        if(!ioctl(gs->fd,PS2GS_CHANNEL_LUT,&request))return 0;
        if(errno!=EAGAIN){gs->error=errno;return -1;}
        struct timespec delay={.tv_nsec=10000000};
        while(nanosleep(&delay,&delay)<0)if(errno!=EINTR){gs->error=errno;return -1;}
    }
    gs->error=errno=EAGAIN;return -1;
}
static int map_blit_valid(struct ps2_surface_client *gs,uint32_t source,uint32_t destination,
    uint32_t palette,unsigned sx,unsigned sy,unsigned dx,unsigned dy,unsigned width,unsigned height,unsigned channel)
{
    if(gs->error){errno=gs->error;return 0;}
    const struct ps2_surface *s=ps2_surface_lookup(gs,source),*d=ps2_surface_lookup(gs,destination),
        *p=ps2_surface_lookup(gs,palette);
    if(!(gs->caps.features&PS2GS_HW_CHANNEL_MAP_BLIT)){errno=ENOTSUP;return 0;}
    if(!source || !destination || !palette || source==destination || source==palette || destination==palette ||
        !s || !d || !p || s->bits_per_pixel!=32 || d->bits_per_pixel!=32 || p->bits_per_pixel!=32 ||
        p->width!=16 || p->height!=16 || !width || !height || channel>3 ||
        sx>=s->width || sy>=s->height || dx>=d->width || dy>=d->height ||
        width>s->width-sx || height>s->height-sy || width>d->width-dx || height>d->height-dy){errno=EINVAL;return 0;}
    return 1;
}
int ps2_surface_channel_map_blit(struct ps2_surface_client *gs,uint32_t source,uint32_t destination,
    uint32_t palette,unsigned sx,unsigned sy,unsigned dx,unsigned dy,
    unsigned width,unsigned height,uint32_t mask,unsigned channel)
{
    if(!map_blit_valid(gs,source,destination,palette,sx,sy,dx,dy,width,height,channel))return -1;
    if(width>64-(sx&63) || height>32-(sy&31)){errno=EINVAL;return -1;}
    if(!mask)return 0;
    struct ps2gs_channel_map_blit r={.abi=PS2GS_ABI_VERSION,.source=source,.destination=destination,.palette=palette,
        .width=width,.height=height,.plane_mask=mask,.source_channel=channel,.sx=sx,.sy=sy,.dx=dx,.dy=dy};
    for(unsigned attempt=0;attempt<100;++attempt){
        if(!ioctl(gs->fd,PS2GS_CHANNEL_MAP_BLIT,&r))return 0;
        if(errno!=EAGAIN){gs->error=errno;return -1;}
        struct timespec delay={.tv_nsec=10000000};
        while(nanosleep(&delay,&delay)<0)if(errno!=EINTR){gs->error=errno;return -1;}
    }
    gs->error=errno=EAGAIN;return -1;
}
int ps2_surface_channel_map_region(struct ps2_surface_client *gs,uint32_t source,uint32_t destination,
    uint32_t palette,unsigned sx,unsigned sy,unsigned dx,unsigned dy,
    unsigned width,unsigned height,uint32_t mask,unsigned channel)
{
    if(!map_blit_valid(gs,source,destination,palette,sx,sy,dx,dy,width,height,channel))return -1;
    if(!mask)return 0;
    for(unsigned y=0;y<height;){unsigned h=32-((sy+y)&31);if(h>height-y)h=height-y;
        for(unsigned x=0;x<width;){unsigned w=64-((sx+x)&63);if(w>width-x)w=width-x;
            if(ps2_surface_channel_map_blit(gs,source,destination,palette,sx+x,sy+y,dx+x,dy+y,w,h,mask,channel))return -1;
            x+=w;
        }
        y+=h;
    }
    return 0;
}
int ps2_surface_alpha_read_byte(struct ps2_surface_client *gs,uint32_t source,unsigned alpha_byte,
    unsigned x,unsigned y,unsigned width,unsigned height,unsigned char *alpha,size_t bytes)
{
    const struct ps2_surface *s=ps2_surface_lookup(gs,source);
    if((alpha_byte!=0 && alpha_byte!=3) || !source || !s || s->bits_per_pixel!=32 || !width || !height || x>=s->width || y>=s->height ||
        width>s->width-x || height>s->height-y || !alpha || bytes<(size_t)width*height ||
        (uintptr_t)alpha>UINTPTR_MAX-((size_t)width*height-1)){errno=EINVAL;return -1;}
    if(!(gs->caps.features&PS2GS_HW_CHANNEL_MAP_BLIT) || !(gs->caps.features&PS2GS_HW_READBACK)){
        errno=ENOTSUP;return -1;}
    uint32_t scratch=0,palette=0,table[256],packed[2048];int status=-1,error=errno;
    if(ps2_surface_create(gs,64,32,32,&scratch) || ps2_surface_create(gs,16,16,32,&palette))goto out;
    for(unsigned i=0;i<256;++i)table[i]=i;
    if(ps2_surface_map_palette(gs,palette,table))goto out;
    status=0;
    for(unsigned yy=0;!status && yy<height;){unsigned h=32-((y+yy)&31);if(h>height-yy)h=height-yy;
        for(unsigned xx=0;!status && xx<width;){unsigned w=64-((x+xx)&63);if(w>width-xx)w=width-xx;
            status=ps2_surface_fill(gs,scratch,0,0,64,32,0) ||
                ps2_surface_channel_map_blit(gs,source,scratch,palette,x+xx,y+yy,0,0,w,h,255,alpha_byte) ||
                ps2_surface_download(gs,scratch,0,0,64,32,packed,sizeof(packed))?-1:0;
            if(!status)for(unsigned row=0;row<h;++row)for(unsigned column=0;column<w;++column)
                alpha[(yy+row)*width+xx+column]=(unsigned char)packed[row*64+column];
            xx+=w;
        }
        yy+=h;
    }
out:
    error=errno;
    if(palette && !gs->error && ps2_surface_destroy(gs,palette) && !status){status=-1;error=errno;}
    if(scratch && !gs->error && ps2_surface_destroy(gs,scratch) && !status){status=-1;error=errno;}
    if(status)errno=error;
    return status;
}
int ps2_surface_alpha_read(struct ps2_surface_client *gs,uint32_t source,
    unsigned x,unsigned y,unsigned width,unsigned height,unsigned char *alpha,size_t bytes)
{return ps2_surface_alpha_read_byte(gs,source,3,x,y,width,height,alpha,bytes);}
int ps2_surface_rgb_rotate(struct ps2_surface_client *gs,uint32_t source,uint32_t destination,
    uint32_t palette,unsigned sx,unsigned sy,unsigned dx,unsigned dy,unsigned width,unsigned height)
{
    uint32_t table[256];
    for(unsigned c=0;c<3;++c){
        for(unsigned i=0;i<256;++i)table[i]=i<<(c*8);
        if(ps2_surface_map_palette(gs,palette,table) ||
            ps2_surface_channel_map_region(gs,source,destination,palette,sx,sy,dx,dy,width,height,255U<<(c*8),c+1))return -1;
    }
    return 0;
}
int ps2_surface_channel_map(struct ps2_surface_client *gs, uint32_t source,
    uint32_t destination, uint32_t palette, unsigned width, unsigned height,
    uint32_t mask, unsigned channel)
{
    if(gs->error){errno=gs->error;return -1;}
    const struct ps2_surface *s=ps2_surface_lookup(gs,source),*d=ps2_surface_lookup(gs,destination),
        *p=ps2_surface_lookup(gs,palette);
    if(!(gs->caps.features&PS2GS_HW_CHANNEL_MAP)){errno=ENOTSUP;return -1;}
    if(!source || !destination || !palette || source==destination || source==palette || destination==palette ||
        !s || !d || !p || s->width!=64 || s->height!=32 || d->width!=64 || d->height!=32 ||
        s->bits_per_pixel!=32 || d->bits_per_pixel!=32 || p->bits_per_pixel!=32 || p->width!=16 || p->height!=16 ||
        !width || !height || width>64 || height>32 || channel>3){errno=EINVAL;return -1;}
    if(!mask)return 0;
    struct ps2gs_channel_map request={.abi=PS2GS_ABI_VERSION,.source=source,.destination=destination,
        .palette=palette,.width=width,.height=height,.plane_mask=mask,.source_channel=channel};
    for(unsigned attempt=0;attempt<100;++attempt){
        if(!ioctl(gs->fd,PS2GS_CHANNEL_MAP,&request))return 0;
        if(errno!=EAGAIN){gs->error=errno;return -1;}
        struct timespec delay={.tv_nsec=10000000};
        while(nanosleep(&delay,&delay)<0)if(errno!=EINTR){gs->error=errno;return -1;}
    }
    gs->error=errno=EAGAIN;return -1;
}
int ps2_surface_alpha_write(struct ps2_surface_client *gs,uint32_t source,uint32_t rgba_scratch,
    uint32_t destination,uint32_t palette,unsigned dx,unsigned dy,unsigned width,unsigned height,uint32_t mask)
{
    if(mask&0xffffff){errno=EINVAL;return -1;}
    uint32_t table[256];
    if(gs->caps.features&PS2GS_HW_RGBA_TRANSFER){
        /* A complete RGBA scratch write also makes every lookup byte explicit
         * before the masked CT32 texture replacement. No CPU pixels are read. */
        for(unsigned i=0;i<256;++i)table[i]=i | i<<24;
        return ps2_surface_map_palette(gs,palette,table) ||
            ps2_surface_channel_map_region(gs,source,rgba_scratch,palette,0,0,0,0,width,height,UINT32_MAX,0) ||
            ps2_surface_rgba_rectangle(gs,rgba_scratch,destination,0,0,dx,dy,width,height,mask,PS2GS_RGBA_REPLACE)?-1:0;
    }
    for(unsigned i=0;i<256;++i)table[i]=i<<24;
    return ps2_surface_map_palette(gs,palette,table) ||
        ps2_surface_channel_map_region(gs,source,destination,palette,0,0,dx,dy,width,height,mask,0)?-1:0;
}
