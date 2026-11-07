/* SPDX-License-Identifier: MIT */
#include "ps2_filter.h"
#include "ps2_gs_lut.h"
#include "ps2_gs_logic.h"
#include <errno.h>
#include <string.h>

int ps2_filter_release(struct ps2_filter_workspace *f)
{
    int status=0,error=errno;
    uint32_t handles[]={f->tap,f->term,f->sum[0],f->sum[1],f->sum[2],f->sum[3],f->saturated,f->carry};
    for(unsigned i=0;i<sizeof(handles)/sizeof(*handles);++i)
        if(handles[i] && !f->gs->error && ps2_surface_destroy(f->gs,handles[i])){
            if(!status)error=errno;
            status=-1;
        }
    memset(f,0,sizeof(*f));errno=error;return status;
}
int ps2_filter_reserve(struct ps2_filter_workspace *f,struct ps2_surface_client *gs,unsigned bytes)
{
    *f=(struct ps2_filter_workspace){.gs=gs,.bytes=bytes};
    if(bytes!=3 && bytes!=4){errno=EINVAL;return -1;}
    if(!(gs->caps.features&PS2GS_HW_CHANNEL_LUT) || !(gs->caps.features&PS2GS_HW_TEXTURE_TRANSFER)){
        errno=ENOTSUP;return -1;
    }
    uint32_t *handles[]={&f->tap,&f->term,&f->sum[0],&f->sum[1],&f->sum[2],&f->saturated,&f->carry};
    for(unsigned i=0;i<sizeof(handles)/sizeof(*handles);++i)
        if(ps2_surface_create(gs,64,32,32,handles[i])){
            int error=errno;ps2_filter_release(f);errno=error;return -1;
        }
    if(bytes==4 && ps2_surface_create(gs,64,32,32,&f->sum[3])){
        int error=errno;ps2_filter_release(f);errno=error;return -1;
    }
    return 0;
}
static int lookup(struct ps2_filter_workspace *f,uint32_t source,uint32_t destination,
    uint32_t palette,const unsigned char table[256],unsigned width)
{
    return ps2_surface_lut_palette(f->gs,palette,table) ||
        ps2_surface_channel_lut(f->gs,source,destination,palette,width,1,0xffffff)?-1:0;
}
static int transfer(struct ps2_filter_workspace *f,uint32_t source,uint32_t destination,unsigned width,unsigned mode)
{return ps2_rop_sample(f->gs,source,destination,0,0,0,0,width,1,0xffffff,mode,128);}
static int add_byte(struct ps2_filter_workspace *f,unsigned byte,uint32_t term,uint32_t palette,unsigned width)
{
    if(byte==f->bytes-1)return transfer(f,term,f->sum[byte],width,PS2GS_TRANSFER_ADD);
    /* The difference between saturated and wrapping addition is zero
     * exactly when there is no carry. A native lookup turns that difference
     * into a one-byte carry; propagate before overwriting any scratch. */
    if(transfer(f,f->sum[byte],f->saturated,width,PS2GS_TRANSFER_REPLACE) ||
        transfer(f,term,f->saturated,width,PS2GS_TRANSFER_ADD_CLAMP) ||
        transfer(f,term,f->sum[byte],width,PS2GS_TRANSFER_ADD) ||
        transfer(f,f->sum[byte],f->saturated,width,PS2GS_TRANSFER_SUBTRACT))return -1;
    unsigned char nonzero[256];for(unsigned i=0;i<256;++i)nonzero[i]=!!i;
    if(lookup(f,f->saturated,f->carry,palette,nonzero,width))return -1;
    return add_byte(f,byte+1,f->carry,palette,width);
}
int ps2_filter_fetch(struct ps2_filter_workspace *f,uint32_t source,uint32_t destination,
    uint32_t palette,unsigned x,unsigned y,unsigned dx,unsigned width,int step)
{
    const struct ps2_surface *s=ps2_surface_lookup(f->gs,source);
    unsigned sw=f->source_kind?f->source_width:s?s->width:0;
    unsigned sh=f->source_kind?f->source_height:s?s->height:0;
    if(!s || !width || width>64 || dx+width>64 || x>=sw || y>=sh ||
        step < -1 || step > 1 || (int)x+step*(int)(width-1)<0 ||
        (int)x+step*(int)(width-1)>=(int)sw || f->source_kind>PS2_FILTER_CONSTANT ||
        f->source_channel>3 || f->source_constant>255){errno=EINVAL;return -1;}
    if(f->alpha_map.enabled){
        struct ps2_filter_workspace mapped=*f;
        mapped.alpha_map.enabled=0;
        mapped.source_kind=f->alpha_map.has_alpha?PS2_FILTER_CHANNEL:PS2_FILTER_CONSTANT;
        mapped.source_channel=f->alpha_map.rotate?0:3;
        mapped.source_width=f->alpha_map.width;mapped.source_height=f->alpha_map.height;
        mapped.source_constant=255;
        /* A run can cross either map edge in either direction. Keep the
         * parent picture's texel bounds; map bounds contribute alpha zero. */
        for(unsigned i=0;i<width;){
            int64_t mx=(int64_t)x+step*(int)i-f->alpha_map.x,my=(int64_t)y-f->alpha_map.y;
            unsigned inside=mx>=0 && my>=0 && mx<mapped.source_width && my<mapped.source_height;
            unsigned count=1;
            while(i+count<width){int64_t next=mx+step*(int)count;
                unsigned next_inside=next>=0 && my>=0 && next<mapped.source_width && my<mapped.source_height;
                if(next_inside!=inside)break;
                ++count;
            }
            if(inside?ps2_filter_fetch(&mapped,f->alpha_map.surface,destination,palette,mx,my,dx+i,count,step):
                ps2_surface_solid_logic(f->gs,destination,dx+i,0,count,1,0,0xffffff,3))return -1;
            i+=count;
        }
        return 0;
    }
    if(f->source_kind==PS2_FILTER_CONSTANT)
        return ps2_surface_solid_logic(f->gs,destination,dx,0,width,1,
            f->source_constant*0x010101U,0xffffff,3);
    if(f->source_kind==PS2_FILTER_RGB){
        if(step==1)return f->source_rotate?
            ps2_surface_rgb_rotate(f->gs,source,destination,palette,x,y,dx,0,width,1):
            ps2_rop_sample(f->gs,source,destination,x,y,dx,0,width,1,0xffffff,0,128);
        for(unsigned i=0;i<width;++i)
            if(f->source_rotate?ps2_surface_rgb_rotate(f->gs,source,destination,palette,x+step*(int)i,y,dx+i,0,1,1):
                ps2_rop_sample(f->gs,source,destination,x+step*(int)i,y,dx+i,0,1,1,0xffffff,0,128))return -1;
        return 0;
    }
    uint32_t table[256];
    if(f->source_kind!=PS2_FILTER_A1){
        for(unsigned i=0;i<256;++i)table[i]=(f->source_kind==PS2_FILTER_A8 && f->source_alpha_bits==4?
            (i&15)*17:i)*0x010101U;
        if(ps2_surface_map_palette(f->gs,palette,table))return -1;
        if(f->source_kind==PS2_FILTER_CHANNEL && step==1)
            return ps2_surface_channel_map_region(f->gs,source,destination,palette,x,y,dx,0,width,1,0xffffff,f->source_channel);
    }
    for(unsigned i=0;i<width;++i){unsigned px=x+step*(int)i,channel=f->source_channel;
        if(f->source_kind==PS2_FILTER_A8){channel=px%4;px/=4;}
        else if(f->source_kind==PS2_FILTER_A1){
            unsigned bit=f->source_msb_first?7-(px%8):px%8;
            for(unsigned j=0;j<256;++j)table[j]=(j&(1U<<bit))?0xffffff:0;
            if(ps2_surface_map_palette(f->gs,palette,table))return -1;
            channel=(px/8)%4;px/=32;
        }
        if(ps2_surface_channel_map_region(f->gs,source,destination,palette,px,y,dx+i,0,1,1,0xffffff,channel))return -1;
    }
    return 0;
}
int ps2_filter_bilinear(struct ps2_filter_workspace *f,uint32_t source,uint32_t destination,
    uint32_t palette,const struct ps2_filter_tap taps[4],unsigned width,int step)
{
    const struct ps2_surface *s=ps2_surface_lookup(f->gs,source);
    unsigned sw=f->source_kind?f->source_width:s?s->width:0;
    unsigned sh=f->source_kind?f->source_height:s?s->height:0;
    unsigned total=0;
    if(!s || !width || width>64 || step < -1 || step > 1){errno=EINVAL;return -1;}
    for(unsigned i=0;i<4;++i){
        total+=taps[i].weight;
        if(taps[i].weight>16384 || (taps[i].weight && taps[i].x!=UINT16_MAX &&
            (taps[i].x>=sw || taps[i].y>=sh || (int)taps[i].x+step*(int)(width-1)<0 ||
            (int)taps[i].x+step*(int)(width-1)>=(int)sw))){errno=EINVAL;return -1;}
    }
    if(total!=16384){errno=EINVAL;return -1;}
    for(unsigned i=0;i<3;++i)
        if(ps2_surface_solid_logic(f->gs,f->sum[i],0,0,width,1,0,0xffffff,0))return -1;
    unsigned char table[256];
    for(unsigned tap=0;tap<4;++tap){
        if(!taps[tap].weight || taps[tap].x==UINT16_MAX)continue;
        /* Reversed and repeated runs use the same exact nearest sampling
         * contract as the compositor, rather than GS fractional filtering. */
        if(ps2_filter_fetch(f,source,f->tap,palette,taps[tap].x,taps[tap].y,0,width,step))return -1;
        /* Three byte planes retain all fractional bits until all four
         * products have been summed. CT16 fetch expands by shifting; XRender
         * requires replication of the five high bits before arithmetic. */
        for(int byte=2;byte>=0;--byte){
            for(unsigned i=0;i<256;++i){unsigned c=f->source_kind==PS2_FILTER_RGB && s->bits_per_pixel==16?i|(i>>5):i;
                table[i]=(c*taps[tap].weight)>>(byte*8);}
            if(lookup(f,f->tap,f->term,palette,table,width) ||
                add_byte(f,byte,f->term,palette,width))return -1;
        }
    }
    for(unsigned i=0;i<256;++i)table[i]=i<<2;
    if(lookup(f,f->sum[2],destination,palette,table,width))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i>>6;
    return lookup(f,f->sum[1],f->term,palette,table,width) ||
        transfer(f,f->term,destination,width,PS2GS_TRANSFER_ADD)?-1:0;
}
int ps2_filter_convolution_begin(struct ps2_filter_workspace *f)
{
    if(f->bytes!=4){errno=EINVAL;return -1;}
    for(unsigned i=0;i<4;++i)
        if(ps2_surface_solid_logic(f->gs,f->sum[i],0,0,1,1,i==1?0x808080:0,0xffffff,3))return -1;
    return 0;
}
int ps2_filter_convolution_tap(struct ps2_filter_workspace *f,uint32_t source,
    uint32_t palette,unsigned x,unsigned y,int32_t coefficient)
{
    const struct ps2_surface *s=ps2_surface_lookup(f->gs,source);
    unsigned sw=f->source_kind?f->source_width:s?s->width:0;
    unsigned sh=f->source_kind?f->source_height:s?s->height:0;
    if(f->bytes!=4 || !s || x>=sw || y>=sh){errno=EINVAL;return -1;}
    if(!coefficient)return 0;
    if(ps2_filter_fetch(f,source,f->tap,palette,x,y,0,1,0))return -1;
    unsigned char table[256];
    for(int byte=3;byte>=0;--byte){
        for(unsigned i=0;i<256;++i){unsigned c=f->source_kind==PS2_FILTER_RGB && s->bits_per_pixel==16?i|(i>>5):i;
            table[i]=(uint32_t)((int64_t)c*coefficient)>>(byte*8);}
        if(lookup(f,f->tap,f->term,palette,table,1) || add_byte(f,byte,f->term,palette,1))return -1;
    }
    return 0;
}
int ps2_filter_convolution_end(struct ps2_filter_workspace *f,uint32_t destination,uint32_t palette)
{
    unsigned char table[256];
    /* Select byte 2 only when byte 3 is zero, without a CPU colour readback.
     * Split its seven high bits and low bit, inject the predicate above each
     * part, and use two palette lookups as a native conditional operation. */
    for(unsigned i=0;i<256;++i)table[i]=i>>1;
    if(lookup(f,f->sum[2],f->term,palette,table,1))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i?0:128;
    if(lookup(f,f->sum[3],f->carry,palette,table,1) || transfer(f,f->carry,f->term,1,PS2GS_TRANSFER_ADD))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i>=128?(i&127)*2:0;
    if(lookup(f,f->term,destination,palette,table,1))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i&1;
    if(lookup(f,f->sum[2],f->term,palette,table,1))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i?0:2;
    if(lookup(f,f->sum[3],f->carry,palette,table,1) || transfer(f,f->carry,f->term,1,PS2GS_TRANSFER_ADD))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i==3;
    if(lookup(f,f->term,f->saturated,palette,table,1) || transfer(f,f->saturated,destination,1,PS2GS_TRANSFER_ADD))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i && i<128?255:0;
    return lookup(f,f->sum[3],f->term,palette,table,1) ||
        transfer(f,f->term,destination,1,PS2GS_TRANSFER_ADD_CLAMP)?-1:0;
}
