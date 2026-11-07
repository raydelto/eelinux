/* SPDX-License-Identifier: MIT */
#include "ps2_divide.h"
#include "ps2_gs_lut.h"
#include "ps2_gs_logic.h"
#include "ps2-gs-rgba-transfer.h"
#include <errno.h>
#include <string.h>

static unsigned handles(struct ps2_divide_workspace *w,uint32_t **out)
{
    unsigned count=0;
    for(unsigned i=0;i<4;++i){out[count++]=w->remainder+i;
        out[count++]=w->shifted+i;out[count++]=w->difference+i;}
    out[count++]=&w->borrow;out[count++]=&w->carry;out[count++]=&w->saturated;
    out[count++]=&w->term;out[count++]=&w->temporary;
    out[count++]=&w->selected;out[count++]=&w->retained;
    return count;
}
int ps2_divide_release(struct ps2_divide_workspace *w)
{
    if(!w){errno=EINVAL;return -1;}
    uint32_t *owned[19];unsigned count=handles(w,owned);int status=0,error=errno;
    for(unsigned i=0;i<count;++i)if(*owned[i] && w->gs && !w->gs->error &&
        ps2_surface_destroy(w->gs,*owned[i])){if(!status)error=errno;status=-1;}
    memset(w,0,sizeof(*w));errno=error;return status;
}
int ps2_divide_reserve(struct ps2_divide_workspace *w,struct ps2_surface_client *gs)
{
    if(!w || !gs || w->gs){errno=EINVAL;return -1;}
    unsigned required=PS2GS_HW_CHANNEL_LUT|PS2GS_HW_CHANNEL_MAP|
        PS2GS_HW_TEXTURE_TRANSFER|PS2GS_HW_RGBA_TRANSFER;
    if((gs->caps.features&required)!=required){errno=ENOTSUP;return -1;}
    *w=(struct ps2_divide_workspace){.gs=gs};
    uint32_t *owned[19];unsigned count=handles(w,owned);
    for(unsigned i=0;i<count;++i)if(ps2_surface_create(gs,64,32,32,owned[i])){
        int error=errno;ps2_divide_release(w);errno=error;return -1;}
    return 0;
}
static int transfer(struct ps2_divide_workspace *w,uint32_t s,uint32_t d,
    unsigned width,unsigned height,unsigned mode)
{return ps2_rop_sample(w->gs,s,d,0,0,0,0,width,height,0xffffff,mode,128);}
static int lookup(struct ps2_divide_workspace *w,uint32_t s,uint32_t d,uint32_t p,
    const unsigned char table[256],unsigned width,unsigned height)
{return ps2_surface_lut_palette(w->gs,p,table) ||
    ps2_surface_channel_lut(w->gs,s,d,p,width,height,0xffffff)?-1:0;}
static int clear(struct ps2_divide_workspace *w,uint32_t d,unsigned width,unsigned height)
{return ps2_surface_solid_logic(w->gs,d,0,0,width,height,0,0xffffff,0);}
static int add_carry(struct ps2_divide_workspace *w,uint32_t a,uint32_t b,
    uint32_t carry,uint32_t p,unsigned width,unsigned height)
{
    unsigned char table[256];for(unsigned i=0;i<256;++i)table[i]=!!i;
    return transfer(w,a,w->saturated,width,height,PS2GS_TRANSFER_REPLACE) ||
        transfer(w,b,w->saturated,width,height,PS2GS_TRANSFER_ADD_CLAMP) ||
        transfer(w,b,a,width,height,PS2GS_TRANSFER_ADD) ||
        transfer(w,a,w->saturated,width,height,PS2GS_TRANSFER_SUBTRACT) ||
        lookup(w,w->saturated,carry,p,table,width,height)?-1:0;
}
static int shifted_divisor(struct ps2_divide_workspace *w,const uint32_t d[3],
    uint32_t p,unsigned shift,unsigned width,unsigned height)
{
    unsigned char table[256];
    for(unsigned byte=0;byte<4;++byte){
        if(byte<3){
            for(unsigned i=0;i<256;++i)table[i]=(i<<shift)&255;
            if(lookup(w,d[byte],w->shifted[byte],p,table,width,height))return -1;
        }else if(clear(w,w->shifted[byte],width,height))return -1;
        if(byte && shift){
            for(unsigned i=0;i<256;++i)table[i]=i>>(8-shift);
            if(lookup(w,d[byte-1],w->term,p,table,width,height) ||
                transfer(w,w->term,w->shifted[byte],width,height,PS2GS_TRANSFER_ADD))return -1;
        }
    }
    return 0;
}
static int subtract_word(struct ps2_divide_workspace *w,uint32_t p,unsigned width,unsigned height)
{
    unsigned char table[256];
    if(clear(w,w->borrow,width,height))return -1;
    for(unsigned byte=0;byte<4;++byte){
        for(unsigned i=0;i<256;++i)table[i]=255-i;
        if(lookup(w,w->shifted[byte],w->term,p,table,width,height) ||
            transfer(w,w->remainder[byte],w->difference[byte],width,height,PS2GS_TRANSFER_REPLACE) ||
            add_carry(w,w->difference[byte],w->term,w->carry,p,width,height))return -1;
        for(unsigned i=0;i<256;++i)table[i]=i?0:1;
        if(lookup(w,w->borrow,w->term,p,table,width,height) ||
            add_carry(w,w->difference[byte],w->term,w->borrow,p,width,height) ||
            transfer(w,w->carry,w->borrow,width,height,PS2GS_TRANSFER_ADD))return -1;
        /* Either carry means no borrow. Carries are mutually exclusive. */
        for(unsigned i=0;i<256;++i)table[i]=i?0:1;
        if(lookup(w,w->borrow,w->term,p,table,width,height) ||
            transfer(w,w->term,w->borrow,width,height,PS2GS_TRANSFER_REPLACE))return -1;
    }
    return 0;
}
static int select_remainder(struct ps2_divide_workspace *w,uint32_t p,unsigned width,unsigned height)
{
    uint32_t colors[256];
    for(unsigned byte=0;byte<4;++byte){
        for(unsigned keep=0;keep<2;++keep){
            uint32_t input=keep?w->remainder[byte]:w->difference[byte];
            uint32_t output=keep?w->retained:w->selected;
            for(unsigned channel=0;channel<3;++channel){
                for(unsigned i=0;i<256;++i)colors[i]=(uint32_t)((!!i)==keep?128:0)<<24;
                if(transfer(w,input,w->temporary,width,height,PS2GS_TRANSFER_REPLACE) ||
                    ps2_surface_map_palette(w->gs,p,colors) ||
                    ps2_surface_channel_map(w->gs,w->borrow,w->temporary,p,width,height,0xff000000,channel) ||
                    ps2_surface_rgba_rectangle(w->gs,w->temporary,output,0,0,0,0,width,height,
                        255U<<(channel*8),PS2GS_RGBA_MULTIPLY))return -1;
            }
        }
        if(transfer(w,w->selected,w->remainder[byte],width,height,PS2GS_TRANSFER_REPLACE) ||
            transfer(w,w->retained,w->remainder[byte],width,height,PS2GS_TRANSFER_ADD))return -1;
    }
    return 0;
}
static int page(struct ps2_surface_client *gs,uint32_t handle,unsigned width,unsigned height)
{
    const struct ps2_surface *s=ps2_surface_lookup(gs,handle);
    return handle && s && s->bits_per_pixel==32 && s->width>=width && s->height>=height;
}
int ps2_native_divide_u32_u24(struct ps2_divide_workspace *w,const uint32_t n[4],
    const uint32_t d[3],uint32_t q,uint32_t p,unsigned width,unsigned height)
{
    if(!w || !w->gs || !n || !d || !width || !height || width>64 || height>32 ||
        !page(w->gs,q,width,height) || q==p){errno=EINVAL;return -1;}
    const struct ps2_surface *palette=ps2_surface_lookup(w->gs,p);
    if(!palette || !p || palette->bits_per_pixel!=32 || palette->width!=16 || palette->height!=16){errno=EINVAL;return -1;}
    uint32_t *owned[19];unsigned count=handles(w,owned);
    for(unsigned i=0;i<count;++i){
        const struct ps2_surface *scratch=ps2_surface_lookup(w->gs,*owned[i]);
        if(!page(w->gs,*owned[i],64,32) || scratch->width!=64 || scratch->height!=32 ||
            *owned[i]==q || *owned[i]==p){errno=EINVAL;return -1;}
        for(unsigned j=0;j<i;++j)if(*owned[i]==*owned[j]){errno=EINVAL;return -1;}
    }
    for(unsigned i=0;i<7;++i){uint32_t input=i<4?n[i]:d[i-4];
        if(!page(w->gs,input,width,height) || input==q || input==p){errno=EINVAL;return -1;}
        const struct ps2_surface *operand=ps2_surface_lookup(w->gs,input);
        if(i>=4 && (operand->width!=64 || operand->height!=32)){errno=EINVAL;return -1;}
        for(unsigned j=0;j<count;++j)if(input==*owned[j]){errno=EINVAL;return -1;}
    }
    unsigned required=PS2GS_HW_CHANNEL_LUT|PS2GS_HW_CHANNEL_MAP|
        PS2GS_HW_TEXTURE_TRANSFER|PS2GS_HW_RGBA_TRANSFER;
    if((w->gs->caps.features&required)!=required){errno=ENOTSUP;return -1;}
    if(clear(w,q,width,height))return -1;
    for(unsigned i=0;i<4;++i)if(transfer(w,n[i],w->remainder[i],width,height,PS2GS_TRANSFER_REPLACE))return -1;
    for(int bit=7;bit>=0;--bit){
        unsigned char table[256];for(unsigned i=0;i<256;++i)table[i]=i?0:1U<<bit;
        if(shifted_divisor(w,d,p,bit,width,height) || subtract_word(w,p,width,height) ||
            lookup(w,w->borrow,w->term,p,table,width,height) ||
            transfer(w,w->term,q,width,height,PS2GS_TRANSFER_ADD) ||
            select_remainder(w,p,width,height))return -1;
    }
    return 0;
}
