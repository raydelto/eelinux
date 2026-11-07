/* SPDX-License-Identifier: MIT */
#include "ps2_wide_filter.h"
#include "ps2_gs_lut.h"
#include "ps2_gs_logic.h"
#include "ps2-gs-rgba-transfer.h"
#include <errno.h>
#include <string.h>
static int page(struct ps2_surface_client *gs,uint32_t h)
{
    const struct ps2_surface *s=ps2_surface_lookup(gs,h);
    return h && s && s->bits_per_pixel==32 && s->width==64 && s->height==32;
}
int ps2_native_bilinear_float(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    uint32_t source,const uint32_t out[4],const unsigned bits[3],uint32_t p,
    const struct ps2_filter_tap taps[4],unsigned fx,unsigned fy,unsigned width,int step)
{
    if(!w || !f || !w->gs || f->gs!=w->gs || !out || !bits || !taps || !width || width>64 ||
        fx>65535 || fy>65535 || step < -1 || step > 1 || (f->bytes!=3 && f->bytes!=4) ||
        f->source_kind>PS2_FILTER_CONSTANT || f->source_rotate>1 || f->source_channel>3 ||
        f->source_constant>255 || f->source_msb_first>1 ||
        (f->source_alpha_bits && f->source_alpha_bits!=1 && f->source_alpha_bits!=4 && f->source_alpha_bits!=8)){
        errno=EINVAL;return -1;
    }
    struct ps2_surface_client *gs=w->gs;
    const struct ps2_surface *map=NULL;
    if(f->alpha_map.enabled){
        const struct ps2_picture_alpha_map *m=&f->alpha_map;
        map=ps2_surface_lookup(gs,m->surface);
        if(m->enabled!=1 || !map || !m->width || !m->height || m->width>1024 || m->height>1024 ||
            map->width!=m->width || map->height!=m->height || m->has_alpha>1 || m->alpha_only>1 ||
            m->swap>1 || m->rotate>1 || (m->swap && m->rotate) ||
            (m->alpha_only && (!m->has_alpha || (m->alpha_bits && m->alpha_bits!=1 && m->alpha_bits!=4 && m->alpha_bits!=8))) ||
            m->surface==p || m->alpha_bits>8 || (m->has_alpha && (!m->surface || map->bits_per_pixel!=32 || map->pixel_depth!=32)) ||
            (m->rotate && (!m->surface || map->bits_per_pixel!=32 || map->pixel_depth!=32)) ||
            (f->source_kind!=PS2_FILTER_CHANNEL && f->source_kind!=PS2_FILTER_CONSTANT)){
            errno=EINVAL;return -1;
        }
    }
    const struct ps2_surface *s=ps2_surface_lookup(gs,source),*palette=ps2_surface_lookup(gs,p);
    unsigned sw=f->source_kind?f->source_width:s?s->width:0,
        sh=f->source_kind?f->source_height:s?s->height:0;
    if(!s || !p || !palette || palette->bits_per_pixel!=32 || palette->width!=16 || palette->height!=16 ||
        !sw || !sh || sw>2048 || sh>2048 ||
        (f->source_kind==PS2_FILTER_RGB && s->bits_per_pixel!=16 && s->bits_per_pixel!=32) ||
        (f->source_kind!=PS2_FILTER_RGB && f->source_kind!=PS2_FILTER_CONSTANT && s->bits_per_pixel!=32) ||
        (f->source_kind!=PS2_FILTER_CONSTANT &&
            (s->height!=sh || s->width<(f->source_kind==PS2_FILTER_A1?(sw+31)/32:
                f->source_kind==PS2_FILTER_A8?(sw+3)/4:sw)))){errno=EINVAL;return -1;}
    for(unsigned c=0;c<3;++c)if(bits[c]<1 || bits[c]>8){errno=EINVAL;return -1;}
    if(map)for(unsigned c=0;c<3;++c)if(bits[c]!=(f->alpha_map.alpha_bits?f->alpha_map.alpha_bits:8)){
        errno=EINVAL;return -1;
    }
    uint32_t private[]={w->remainder[0],w->remainder[1],w->remainder[2],w->remainder[3],
        w->shifted[0],w->shifted[1],w->shifted[2],w->shifted[3],
        w->difference[0],w->difference[1],w->difference[2],w->difference[3],
        w->borrow,w->carry,w->saturated,w->term,w->temporary,w->selected,w->retained,
        f->tap,f->term,f->sum[0],f->sum[1],f->sum[2],f->saturated,f->carry,f->sum[3]};
    unsigned count=sizeof(private)/sizeof(*private)-(f->bytes==3);
    for(unsigned i=0;i<count;++i){
        if(!page(gs,private[i]) || private[i]==p || private[i]==source ||
            (map && private[i]==map->handle)){errno=EINVAL;return -1;}
        for(unsigned j=0;j<i;++j)if(private[i]==private[j]){errno=EINVAL;return -1;}
    }
    for(unsigned i=0;i<4;++i){
        if(!page(gs,out[i]) || out[i]==p || out[i]==source ||
            (map && out[i]==map->handle)){errno=EINVAL;return -1;}
        for(unsigned j=0;j<i;++j)if(out[i]==out[j]){errno=EINVAL;return -1;}
        for(unsigned j=0;j<count;++j)if(out[i]==private[j]){errno=EINVAL;return -1;}
        if(taps[i].x==UINT16_MAX && taps[i].y==UINT16_MAX)continue;
        int last=(int)taps[i].x+step*(int)(width-1);
        if(taps[i].x>=sw || taps[i].y>=sh || last<0 || last>=(int)sw){errno=EINVAL;return -1;}
    }
    unsigned required=PS2GS_HW_CHANNEL_LUT|PS2GS_HW_CHANNEL_MAP|PS2GS_HW_CHANNEL_MAP_BLIT|
        PS2GS_HW_TEXTURE_TRANSFER|PS2GS_HW_RGBA_TRANSFER;
    if((gs->caps.features&required)!=required){errno=ENOTSUP;return -1;}
    /* Exact binary fractions; only geometry-derived constants are computed
     * here. Keep the pinned pixman's weight and accumulation order. */
    float x=fx*(1.0f/65536),y=fy*(1.0f/65536),ix=1.0f-x,iy=1.0f-y;
    float weights[]={ix*iy,x*iy,ix*y,x*y};
    uint32_t owned[9]={0};int result=-1,error=0;
    for(unsigned i=0;i<9;++i)if(ps2_surface_create(gs,64,32,32,owned+i))goto done;
    uint32_t codes=owned[0],*values=owned+1,*weight=owned+5;
    for(unsigned tap=0;tap<4;++tap){uint32_t raw;memcpy(&raw,weights+tap,4);
        if(taps[tap].x==UINT16_MAX){
            if(ps2_surface_solid_logic(gs,codes,0,0,width,1,0,0xffffff,3))goto done;
        }else if(ps2_filter_fetch(f,source,codes,p,taps[tap].x,taps[tap].y,0,width,step))goto done;
        if(ps2_native_unorm_float(gs,codes,values,bits,p,width,1))goto done;
        for(unsigned byte=0;byte<4;++byte)
            if(ps2_surface_solid_logic(gs,weight[byte],0,0,width,1,((raw>>(byte*8))&255)*0x010101,0xffffff,3))goto done;
        if(ps2_native_float_multiply(w,f,values,weight,values,p,width,1))goto done;
        if(tap){
            if(ps2_native_float_add(w,f,out,values,out,p,width,1,0))goto done;
        }else for(unsigned byte=0;byte<4;++byte)
            if(ps2_rop_sample(gs,values[byte],out[byte],0,0,0,0,width,1,0xffffff,PS2GS_TRANSFER_REPLACE,128))goto done;
    }
    result=0;
done:
    error=errno;
    for(unsigned i=0;i<9;++i)if(owned[i] && !gs->error && ps2_surface_destroy(gs,owned[i])){
        if(!result)error=errno;
        result=-1;
    }
    errno=error;return result;
}
