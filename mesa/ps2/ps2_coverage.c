/* SPDX-License-Identifier: MIT */
#include "ps2_coverage.h"
#include "ps2_filter.h"
#include "ps2_gs_lut.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
int ps2_coverage_release(struct ps2_coverage_surface *c)
{
    int status=0,error=errno;uint32_t handles[]={c->rgba,c->bytes,c->palette,c->packed,c->surface};
    for(unsigned i=0;i<sizeof(handles)/sizeof(*handles);++i)
        if(handles[i] && !c->gs->error && ps2_surface_destroy(c->gs,handles[i])){
            if(!status)error=errno;
            status=-1;
        }
    memset(c,0,sizeof(*c));errno=error;return status;
}
static int decode(struct ps2_coverage_surface *c,struct ps2_surface_client *gs,
    const struct ps2_picture_bitmap *p,unsigned core)
{
    if(!c || !gs || !p){errno=EINVAL;return -1;}
    *c=(struct ps2_coverage_surface){.gs=gs,.original_bytes=*p};
    unsigned row=p->bits_per_pixel>=8?p->width*(p->bits_per_pixel/8):(p->width+7)/8;
    unsigned logical=p->alpha_bits?p->alpha_bits:p->bits_per_pixel==0?1:p->bits_per_pixel;
    unsigned valid= p->bits_per_pixel==16 ? core && (logical==12 || logical==16) :
        p->bits_per_pixel==8 ? logical==4 || logical==8 :
        (p->bits_per_pixel==0 || p->bits_per_pixel==1) && logical==1;
    if(!p->width || !p->height || p->width>1024 || p->height>1024 || !p->bits ||
        !valid || p->msb_first>1 ||
        p->stride<row || p->bytes<row || p->bytes>1024*1024 ||
        p->height-1>(p->bytes-row)/p->stride || (uintptr_t)p->bits>UINTPTR_MAX-(p->bytes-1)){
        errno=EINVAL;return -1;}
    if(!(gs->caps.features&PS2GS_HW_RGBA_TRANSFER) || !(gs->caps.features&PS2GS_HW_CHANNEL_MAP_BLIT)){
        errno=ENOTSUP;return -1;}
    c->width=p->width;c->height=p->height;c->bpp=p->bits_per_pixel>=8?p->bits_per_pixel:1;c->msb_first=p->msb_first;c->row=row;c->core=core;
    c->alpha_bits=logical;
    if(ps2_surface_create(gs,p->width,p->height,32,&c->surface) ||
        ps2_surface_set_depth(gs,c->surface,core?24:32) ||
        ps2_surface_create(gs,(row+3)/4,p->height,32,&c->packed) ||
        ps2_surface_set_depth(gs,c->packed,32) ||
        ps2_surface_create(gs,16,16,32,&c->palette) || ps2_surface_create(gs,64,32,32,&c->bytes) ||
        ps2_surface_create(gs,64,32,32,&c->rgba))goto fail;
    uint32_t packed[512];unsigned stride=((row+3)/4)*4;
    if(ps2_surface_fill(gs,c->surface,0,0,p->width,p->height,0))goto fail;
    for(unsigned y=0;y<p->height;++y){
        memset(packed,0,stride);memcpy(packed,p->bits+y*p->stride,row);
        if(ps2_surface_upload(gs,c->packed,0,y,stride/4,1,packed,stride,stride))goto fail;
    }
    /* One immutable packed word supplies a vertical strip of logical pixels.
     * Keep palette phases outside the row loop and let the GS map each strip
     * in bounded 32-row page pieces. Pixel decoding remains a GS lookup; the
     * CPU submits geometry/constants and copies the original protocol bytes.
     * This avoids a syscall and palette upload for individual bitmap pixels. */
    unsigned phases=c->bpp==16?2:c->bpp==8?4:32;
    uint32_t table[256];
    for(unsigned phase=0;phase<phases && phase<p->width;++phase){
        unsigned parts=c->bpp==16?2:1;
        for(unsigned part=0;part<parts;++part){
            unsigned bit=c->msb_first?(phase&~7U)+7-(phase&7):phase;
            unsigned channel=c->bpp==16?phase*2+part:c->bpp==8?phase:bit/8;
            uint32_t mask=c->bpp==16?((255U<<(part*8))&((1U<<logical)-1)):
                core?((1U<<logical)-1):0xff000000U;
            for(unsigned i=0;i<256;++i){
                uint32_t value=c->bpp==16?i<<(part*8):c->bpp==8?
                    (logical==4?i&15:i):!!(i&(1U<<(bit%8)));
                table[i]=core?value:c->bpp==8?(logical==4?value*17:value)<<24:
                    value?0xff000000U:0;
            }
            if(ps2_surface_map_palette(gs,c->palette,table))goto fail;
            for(unsigned x=phase;x<p->width;x+=phases)
                if(ps2_surface_channel_map_region(gs,c->packed,c->surface,c->palette,
                    x/phases,0,x,0,1,p->height,mask,channel))goto fail;
        }
    }
    return 0;
fail:
    {int error=errno;ps2_coverage_release(c);errno=error;return -1;}
}
int ps2_coverage_decode(struct ps2_coverage_surface *c,struct ps2_surface_client *gs,const struct ps2_picture_bitmap *p)
{return decode(c,gs,p,0);}
int ps2_coverage_decode_core(struct ps2_coverage_surface *c,struct ps2_surface_client *gs,const struct ps2_picture_bitmap *p)
{return decode(c,gs,p,1);}
int ps2_coverage_refresh_row(struct ps2_coverage_surface *c,unsigned y)
{
    if(!c || !c->gs || c->core || c->bpp!=8 || y>=c->height ||
        !ps2_surface_lookup(c->gs,c->surface) || !ps2_surface_lookup(c->gs,c->packed)){
        errno=EINVAL;return -1;}
    struct ps2_filter_workspace f={.gs=c->gs,.source_kind=PS2_FILTER_A8,
        .source_width=c->width,.source_height=c->height,.source_alpha_bits=c->alpha_bits};
    for(unsigned x=0;x<c->width;){unsigned w=c->width-x;if(w>64)w=64;
        if(ps2_filter_fetch(&f,c->packed,c->bytes,c->palette,x,y,0,w,1) ||
            ps2_surface_alpha_write(c->gs,c->bytes,c->rgba,c->surface,c->palette,x,y,w,1,0xff000000))return -1;
        x+=w;
    }
    return 0;
}
int ps2_coverage_pack_region(struct ps2_coverage_surface *c,unsigned x,unsigned y,unsigned width)
{
    if(!c || !c->gs || c->core || c->bpp!=8 || y>=c->height || !width || x>=c->width ||
        width>c->width-x || !ps2_surface_lookup(c->gs,c->surface) || !ps2_surface_lookup(c->gs,c->packed)){
        errno=EINVAL;return -1;}
    uint32_t table[256];
    for(unsigned phase=0;phase<4;++phase){
        uint32_t mask=(c->alpha_bits==4?15U:255U)<<(phase*8);
        for(unsigned i=0;i<256;++i)table[i]=(c->alpha_bits==4?i>>4:i)<<(phase*8);
        if(ps2_surface_map_palette(c->gs,c->palette,table))return -1;
        for(unsigned k=x;k<x+width;++k)if(k%4==phase)
            if(ps2_surface_channel_map_region(c->gs,c->surface,c->packed,c->palette,k,y,k/4,y,1,1,mask,3))return -1;
    }
    return 0;
}
int ps2_coverage_encode(struct ps2_coverage_surface *c,struct pipe_context *ctx,
    unsigned char *pixels,size_t bytes,size_t stride)
{
    if(!c || !c->gs || !ctx || !pixels || stride<c->row || bytes<c->row ||
        c->height-1>(bytes-c->row)/stride || (uintptr_t)pixels>UINTPTR_MAX-(bytes-1)){
        errno=EINVAL;return -1;}
    unsigned packed_stride=((c->row+3)/4)*4, phases=c->bpp==16?2:c->bpp==8?4:32;
    unsigned char *reply=malloc((size_t)packed_stride*c->height);if(!reply){errno=ENOMEM;return -1;}
    int status=-1,error=errno;uint32_t table[256];
    if(c->bpp==16){
        uint32_t bits=(1U<<c->alpha_bits)-1;
        for(unsigned phase=0;phase<2 && phase<c->width;++phase)for(unsigned part=0;part<2;++part){
            unsigned shift=(phase*2+part)*8;uint32_t mask=((bits>>(part*8))&255)<<shift;
            for(unsigned i=0;i<256;++i)table[i]=i<<shift;
            if(ps2_surface_map_palette(c->gs,c->palette,table))goto out;
            for(unsigned x=phase;x<c->width;x+=2)
                if(ps2_surface_channel_map_region(c->gs,c->surface,c->packed,c->palette,x,0,x/2,0,1,c->height,mask,part))goto out;
        }
    }else
    for(unsigned phase=0;phase<phases && phase<c->width;++phase){unsigned bit=c->msb_first?(phase&~7U)+7-(phase&7):phase;
        uint32_t mask=c->bpp==8?(c->alpha_bits==4?15U:255U)<<(phase*8):1U<<bit;
        for(unsigned i=0;i<256;++i)table[i]=c->bpp==8?
            (c->alpha_bits==4?(c->core?i&15:i>>4):i)<<(phase*8):(c->core?(i&1):i>=128)?mask:0;
        if(ps2_surface_map_palette(c->gs,c->palette,table))goto out;
        for(unsigned x=phase;x<c->width;x+=phases)
            if(ps2_surface_channel_map_region(c->gs,c->surface,c->packed,c->palette,x,0,x/phases,0,1,c->height,mask,c->core?0:3))goto out;
    }
    status=ps2_pipe_download(ctx,c->packed,0,0,packed_stride/4,c->height,UINT32_MAX,
        reply,(size_t)packed_stride*c->height,packed_stride);
    if(!status)for(unsigned y=0;y<c->height;++y)memcpy(pixels+y*stride,reply+y*packed_stride,c->row);
out:
    error=errno;free(reply);if(status)errno=error;return status;
}
