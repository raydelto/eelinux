/* SPDX-License-Identifier: MIT */
#include "ps2_packed.h"
#include "ps2_composite.h"
#include "ps2_gs_logic.h"
#include "ps2_gs_lut.h"
#include "ps2-gs-rgba-transfer.h"
#include <errno.h>
#include <string.h>
int ps2_packed_layout_valid(const struct ps2_packed_layout *l)
{
    if(!l)return 0;
    uint32_t occupied=0;
    for(unsigned c=0;c<4;++c){
        if(l->bits[c]>(c==3?8U:10U) || l->shift[c]>31 || l->bits[c]+l->shift[c]>32 || (c<3 && !l->bits[c]))return 0;
        uint32_t mask=((1U<<l->bits[c])-1)<<l->shift[c];
        if(mask&occupied)return 0;
        occupied|=mask;
    }
    return 1;
}
int ps2_packed_release(struct ps2_packed_workspace *w)
{
    int result=0,error=errno;
    for(unsigned i=0;i<4;++i)if(w->scratch[i] && !w->gs->error && ps2_surface_destroy(w->gs,w->scratch[i])){
        if(!result)error=errno;
        result=-1;
    }
    memset(w,0,sizeof(*w));errno=error;return result;
}
int ps2_packed_reserve(struct ps2_packed_workspace *w,struct ps2_surface_client *gs)
{
    if(!w || !gs || w->gs){errno=EINVAL;return -1;}
    w->gs=gs;
    for(unsigned i=0;i<4;++i)if(ps2_surface_create(gs,64,32,32,w->scratch+i) || ps2_surface_set_depth(gs,w->scratch[i],32)){
        int error=errno;ps2_packed_release(w);errno=error;return -1;
    }
    return 0;
}
static int validate(struct ps2_packed_workspace *w,uint32_t raw,unsigned x,unsigned y,
    const struct ps2_packed_layout *l,const uint32_t planes[8],uint32_t palette,unsigned width,unsigned height)
{
    if(!w || !w->gs || !planes || !ps2_packed_layout_valid(l) || !width || !height || width>64 || height>32){errno=EINVAL;return -1;}
    struct ps2_surface_client *gs=w->gs;const struct ps2_surface *s=ps2_surface_lookup(gs,raw),*p=ps2_surface_lookup(gs,palette);
    if(!s || s->bits_per_pixel!=32 || x>s->width || width>s->width-x || y>s->height || height>s->height-y ||
        !palette || !p || p->bits_per_pixel!=32 || p->width!=16 || p->height!=16 || palette==raw){errno=EINVAL;return -1;}
    uint32_t occupied=0;for(unsigned c=0;c<4;++c)occupied|=((1U<<l->bits[c])-1)<<l->shift[c];
    if((occupied&0xff000000) && (!raw || (occupied&~ps2_surface_native_mask(gs,raw)))){errno=EINVAL;return -1;}
    uint32_t handles[12];memcpy(handles,planes,32);memcpy(handles+8,w->scratch,16);
    for(unsigned i=0;i<12;++i){const struct ps2_surface *v=ps2_surface_lookup(gs,handles[i]);
        if(!handles[i] || !v || v->bits_per_pixel!=32 || v->width!=64 || v->height!=32 || handles[i]==palette || handles[i]==raw ||
            (i>=8 && v->pixel_depth!=32)){errno=EINVAL;return -1;}
        for(unsigned j=0;j<i;++j)if(handles[i]==handles[j]){errno=EINVAL;return -1;}
    }
    unsigned required=PS2GS_HW_SOLID_LOGIC|PS2GS_HW_CHANNEL_LUT|PS2GS_HW_CHANNEL_MAP|
        PS2GS_HW_CHANNEL_MAP_BLIT|PS2GS_HW_RGBA_TRANSFER|PS2GS_HW_TEXTURE_TRANSFER;
    if((gs->caps.features&required)!=required){errno=ENOTSUP;return -1;}
    return 0;
}
int ps2_packed_fetch(struct ps2_packed_workspace *w,uint32_t raw,unsigned x,unsigned y,
    const struct ps2_packed_layout *l,const uint32_t out[8],uint32_t p,unsigned width,unsigned height)
{
    if(validate(w,raw,x,y,l,out,p,width,height))return -1;
    struct ps2_surface_client *gs=w->gs;uint32_t low=w->scratch[0],high=w->scratch[1],value=w->scratch[2],selected=w->scratch[3],table[256];
    for(unsigned i=0;i<8;++i)if(ps2_surface_solid_logic(gs,out[i],0,0,width,height,0,0xffffff,3))return -1;
    for(unsigned c=0;c<4;++c){unsigned bits=l->bits[c],shift=l->shift[c],mask=c==3?0xffffff:255U<<(8*c);
        const uint32_t *target=out+(c==3?4:0);
        if(!bits){for(unsigned b=0;b<4;++b)if(ps2_surface_solid_logic(gs,target[b],0,0,width,height,((0x3f800000U>>(8*b))&255)*0x010101,mask,3))return -1;continue;}
        if(ps2_surface_solid_logic(gs,low,0,0,width,height,0,0xffffff,3) || ps2_surface_solid_logic(gs,high,0,0,width,height,0,0xffffff,3))return -1;
        for(unsigned byte=0;byte<4;++byte){uint32_t part=(UINT32_C(255)<<(8*byte))&(((1U<<bits)-1)<<shift);
            if(!part)continue;
            for(unsigned half=0;half<2;++half){unsigned contribution=(part>>shift)>>(8*half)&255;
                if(!contribution)continue;
                for(unsigned i=0;i<256;++i){uint32_t v=((uint32_t)i<<(8*byte))>>shift;table[i]=((v>>(8*half))&((1U<<bits)-1)&255)*0x010101;}
                if(ps2_surface_map_palette(gs,p,table) || ps2_surface_channel_map_region(gs,raw,half?high:low,p,x,y,0,0,width,height,contribution*0x010101,byte))return -1;
            }
        }
        float reciprocal=1.0f/((1U<<bits)-1);unsigned banks=bits>8?1U<<(bits-8):1;
        for(unsigned bank=0;bank<banks;++bank){uint32_t constants[256];
            for(unsigned i=0;i<256;++i){float v=((bank<<8)|(i&((1U<<bits)-1)))*reciprocal;memcpy(constants+i,&v,4);}
            for(unsigned b=0;b<4;++b){unsigned char lut[256];for(unsigned i=0;i<256;++i)lut[i]=(constants[i]>>(8*b))&255;
                if(ps2_surface_lut_palette(gs,p,lut) || ps2_surface_channel_lut(gs,low,value,p,width,height,0xffffff))return -1;
                for(unsigned i=0;i<256;++i)table[i]=(i==bank?128U:0U)<<24;
                if(ps2_surface_map_palette(gs,p,table) || ps2_surface_channel_map(gs,high,value,p,width,height,0xff000000,0) ||
                    ps2_surface_rgba_rectangle(gs,value,selected,0,0,0,0,width,height,0xffffff,PS2GS_RGBA_MULTIPLY) ||
                    ps2_rop_sample(gs,selected,target[b],0,0,0,0,width,height,mask,PS2GS_TRANSFER_ADD,128))return -1;
            }
        }
    }
    return 0;
}
int ps2_packed_store(struct ps2_packed_workspace *w,struct ps2_divide_workspace *divide,struct ps2_filter_workspace *filter,
    const uint32_t in[8],uint32_t raw,unsigned x,unsigned y,const struct ps2_packed_layout *l,uint32_t p,unsigned width,unsigned height)
{
    if(validate(w,raw,x,y,l,in,p,width,height))return -1;
    if(!divide || !filter || divide->gs!=w->gs || filter->gs!=w->gs){errno=EINVAL;return -1;}
    uint32_t codes[2]={w->scratch[0],w->scratch[1]},alpha[2]={w->scratch[2],w->scratch[3]},table[256];
    const unsigned rgb[]={l->bits[0],l->bits[1],l->bits[2]},a[]={l->bits[3],l->bits[3],l->bits[3]};
    const uint32_t outputs[]={codes[0],codes[1],alpha[0],alpha[1]};
    if(ps2_native_float_operands(divide,filter,in,8,outputs,4,p,width,height))return -1;
    if(ps2_native_float_unorm_words(divide,filter,in,codes,rgb,p,width,height) ||
        (l->bits[3] && ps2_native_float_unorm_words(divide,filter,in+4,alpha,a,p,width,height)))return -1;
    if(ps2_surface_solid_logic(w->gs,raw,x,y,width,height,0,ps2_surface_native_mask(w->gs,raw),3))return -1;
    for(unsigned c=0;c<4;++c)if(l->bits[c])for(unsigned half=0;half<(l->bits[c]>8?2U:1U);++half){
        uint32_t component_mask=(((1U<<l->bits[c])-1)&(255U<<(8*half)))<<l->shift[c];
        for(unsigned i=0;i<256;++i)table[i]=((i<<(8*half))&((1U<<l->bits[c])-1))<<l->shift[c];
        if(ps2_surface_map_palette(w->gs,p,table) || ps2_surface_channel_map_region(w->gs,c==3?alpha[0]:codes[half],raw,p,
            0,0,x,y,width,height,component_mask,c==3?0:c))return -1;
    }
    return 0;
}
static int constant(struct ps2_surface_client *gs,const uint32_t out[4],uint32_t value,unsigned width,unsigned height)
{
    for(unsigned b=0;b<4;++b)if(ps2_surface_solid_logic(gs,out[b],0,0,width,height,((value>>(8*b))&255)*0x010101,0xffffff,3))return -1;
    return 0;
}
static int copy(struct ps2_surface_client *gs,const uint32_t in[4],const uint32_t out[4],unsigned width,unsigned height)
{
    for(unsigned b=0;b<4;++b)if(ps2_rop_sample(gs,in[b],out[b],0,0,0,0,width,height,0xffffff,PS2GS_TRANSFER_REPLACE,128))return -1;
    return 0;
}
static int factor(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,unsigned code,
    const uint32_t sa[4],const uint32_t da[4],const uint32_t out[4],uint32_t p,unsigned width,unsigned height)
{
    if(code<2)return constant(w->gs,out,code?0x3f800000:0,width,height);
    const uint32_t *a=code<4?sa:da;
    if(code&1)return constant(w->gs,out,0x3f800000,width,height) || ps2_native_float_signed(w,f,out,a,out,p,width,height,PS2_FLOAT_SUBTRACT)?-1:0;
    return copy(w->gs,a,out,width,height);
}
static int packed_blend(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t source[8],const uint32_t mask[8],const uint32_t dest[8],const uint32_t scratch[12],uint32_t p,
    unsigned width,unsigned height,unsigned op,unsigned ca)
{
    if(!w || !f || !w->gs || f->gs!=w->gs || !source || !mask || !dest || op>PS2_COMPOSITE_XOR || ca>1 || !width || !height || width>64 || height>32){errno=EINVAL;return -1;}
    uint32_t all[36];memcpy(all,source,32);memcpy(all+8,mask,32);memcpy(all+16,dest,32);
    unsigned count=scratch?36:24;if(scratch)memcpy(all+24,scratch,48);
    for(unsigned i=0;i<count;++i)for(unsigned j=0;j<i;++j)if(all[i]==all[j]){errno=EINVAL;return -1;}
    if(ps2_native_float_operands(w,f,all,count,dest,8,p,width,height))return -1;
    if(!(w->gs->caps.features&PS2GS_HW_SOLID_LOGIC)){errno=ENOTSUP;return -1;}
    /* 0/1 constants, 2/3 Sa/(1-Sa), 4/5 Da/(1-Da). */
    const unsigned sf[]={1,1,0,0,0,0,1,5,4,5,4,5,5},df[]={0,3,0,1,2,3,1,1,0,0,3,2,3};
    uint32_t h[20]={0};int result=-1,error=0;
    unsigned borrowed=scratch?12:0;if(scratch)memcpy(h,scratch,48);
    for(unsigned i=borrowed;i<20;++i)if(ps2_surface_create(w->gs,64,32,32,h+i))goto done;
    for(unsigned a=0;a<2;++a){const uint32_t *s=source+(a?4:0),*m=mask+(a || !ca?4:0),*d=dest+(a?4:0);
        if(ps2_native_float_signed(w,f,s,m,h,p,width,height,PS2_FLOAT_MULTIPLY) ||
            ps2_native_float_signed(w,f,source+4,m,h+4,p,width,height,PS2_FLOAT_MULTIPLY) ||
            factor(w,f,sf[op],h+4,dest+4,h+8,p,width,height) ||
            factor(w,f,df[op],h+4,dest+4,h+12,p,width,height) ||
            ps2_native_float_signed(w,f,h,h+8,h,p,width,height,PS2_FLOAT_MULTIPLY) ||
            ps2_native_float_signed(w,f,d,h+12,h+16,p,width,height,PS2_FLOAT_MULTIPLY) ||
            ps2_native_float_signed(w,f,h,h+16,d,p,width,height,PS2_FLOAT_ADD))goto done;
    }
    result=0;
done:
    error=errno;for(unsigned i=borrowed;i<20;++i)if(h[i] && !w->gs->error && ps2_surface_destroy(w->gs,h[i])){if(!result)error=errno;result=-1;}
    errno=error;return result;
}
int ps2_packed_blend(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t s[8],const uint32_t m[8],const uint32_t d[8],uint32_t p,unsigned width,unsigned height,unsigned op,unsigned ca)
{return packed_blend(w,f,s,m,d,NULL,p,width,height,op,ca);}
int ps2_packed_blend_scratch(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t s[8],const uint32_t m[8],const uint32_t d[8],const uint32_t scratch[12],uint32_t p,
    unsigned width,unsigned height,unsigned op,unsigned ca)
{
    if(!scratch){errno=EINVAL;return -1;}
    return packed_blend(w,f,s,m,d,scratch,p,width,height,op,ca);
}
