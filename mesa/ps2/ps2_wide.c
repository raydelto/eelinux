/* SPDX-License-Identifier: MIT */
#include "ps2_wide.h"
#include "ps2_composite.h"
#include "ps2_gs_lut.h"
#include "ps2_gs_logic.h"
#include "ps2-gs-rgba-transfer.h"
#include <errno.h>
#include <float.h>
#include <string.h>
_Static_assert(sizeof(float)==4 && FLT_RADIX==2 && FLT_MANT_DIG==24 && FLT_MAX_EXP==128,"IEEE binary32 lookup constants");
static int page(struct ps2_surface_client *gs,uint32_t h)
{
    const struct ps2_surface *s=ps2_surface_lookup(gs,h);
    return h && s && s->bits_per_pixel==32 && s->width==64 && s->height==32;
}
static int transfer(struct ps2_divide_workspace *w,uint32_t s,uint32_t d,unsigned width,unsigned height,unsigned mode)
{return ps2_rop_sample(w->gs,s,d,0,0,0,0,width,height,0xffffff,mode,128);}
static int add(struct ps2_divide_workspace *w,uint32_t a,uint32_t b,uint32_t p,unsigned width,unsigned height)
{
    unsigned char nonzero[256];for(unsigned i=0;i<256;++i)nonzero[i]=!!i;
    return transfer(w,a,w->saturated,width,height,PS2GS_TRANSFER_REPLACE) ||
        transfer(w,b,w->saturated,width,height,PS2GS_TRANSFER_ADD_CLAMP) ||
        transfer(w,b,a,width,height,PS2GS_TRANSFER_ADD) ||
        transfer(w,a,w->saturated,width,height,PS2GS_TRANSFER_SUBTRACT) ||
        ps2_surface_lut_palette(w->gs,p,nonzero) ||
        ps2_surface_channel_lut(w->gs,w->saturated,w->carry,p,width,height,0xffffff)?-1:0;
}
static int accumulate(struct ps2_divide_workspace *w,const uint32_t sum[6],uint32_t term,
    unsigned offset,uint32_t p,unsigned width,unsigned height)
{
    for(unsigned byte=offset;byte<6;++byte){
        if(add(w,sum[byte],byte==offset?term:w->carry,p,width,height))return -1;
    }
    return 0;
}
int ps2_native_unorm_float(struct ps2_surface_client *gs,uint32_t source,
    const uint32_t out[4],const unsigned bits[3],uint32_t p,unsigned width,unsigned height)
{
    if(!gs || !out || !bits || !page(gs,source) || !width || !height || width>64 || height>32){errno=EINVAL;return -1;}
    const struct ps2_surface *palette=ps2_surface_lookup(gs,p);
    if(!p || !palette || palette->bits_per_pixel!=32 || palette->width!=16 || palette->height!=16){errno=EINVAL;return -1;}
    for(unsigned i=0;i<3;++i)if(bits[i]<1 || bits[i]>8){errno=EINVAL;return -1;}
    for(unsigned i=0;i<4;++i){
        if(!page(gs,out[i]) || out[i]==source || out[i]==p){errno=EINVAL;return -1;}
        for(unsigned j=0;j<i;++j)if(out[i]==out[j]){errno=EINVAL;return -1;}
    }
    if(!(gs->caps.features&PS2GS_HW_CHANNEL_LUT)){errno=ENOTSUP;return -1;}
    for(unsigned channel=0;channel<3;++channel){
        float reciprocal=1.0f/((1U<<bits[channel])-1);
        uint32_t constants[256];
        for(unsigned i=0;i<256;++i){float value=(i>>(8-bits[channel]))*reciprocal;memcpy(constants+i,&value,4);}
        for(unsigned byte=0;byte<4;++byte){
            unsigned char table[256];for(unsigned i=0;i<256;++i)table[i]=(constants[i]>>(byte*8))&255;
            if(ps2_surface_lut_palette(gs,p,table) ||
                ps2_surface_channel_lut(gs,source,out[byte],p,width,height,255U<<(channel*8)))return -1;
        }
    }
    return 0;
}
static int validate(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t *input,unsigned inputs,const uint32_t *out,unsigned outputs,uint32_t p,unsigned width,unsigned height)
{
    if(!w || !f || !w->gs || f->gs!=w->gs || (f->bytes!=3 && f->bytes!=4) || !input || !out ||
        !width || !height || width>64 || height>32){errno=EINVAL;return -1;}
    struct ps2_surface_client *gs=w->gs;const struct ps2_surface *palette=ps2_surface_lookup(gs,p);
    if(!p || !palette || palette->bits_per_pixel!=32 || palette->width!=16 || palette->height!=16){errno=EINVAL;return -1;}
    uint32_t private[]={w->remainder[0],w->remainder[1],w->remainder[2],w->remainder[3],
        w->shifted[0],w->shifted[1],w->shifted[2],w->shifted[3],
        w->difference[0],w->difference[1],w->difference[2],w->difference[3],
        w->borrow,w->carry,w->saturated,w->term,w->temporary,w->selected,w->retained,
        f->tap,f->term,f->sum[0],f->sum[1],f->sum[2],f->saturated,f->carry,f->sum[3]};
    unsigned count=sizeof(private)/sizeof(*private)-(f->bytes==3);
    for(unsigned i=0;i<count;++i){
        if(!page(gs,private[i]) || private[i]==p){errno=EINVAL;return -1;}
        for(unsigned j=0;j<i;++j)if(private[i]==private[j]){errno=EINVAL;return -1;}
    }
    for(unsigned i=0;i<inputs+outputs;++i){uint32_t h=i<inputs?input[i]:out[i-inputs];
        if(!page(gs,h) || h==p){errno=EINVAL;return -1;}
        for(unsigned j=0;j<count;++j)if(h==private[j]){errno=EINVAL;return -1;}
        if(i>=inputs)for(unsigned j=inputs;j<i;++j)if(h==out[j-inputs]){errno=EINVAL;return -1;}
    }
    unsigned required=PS2GS_HW_CHANNEL_LUT|PS2GS_HW_CHANNEL_MAP|
        PS2GS_HW_TEXTURE_TRANSFER|PS2GS_HW_RGBA_TRANSFER;
    if((gs->caps.features&required)!=required){errno=ENOTSUP;return -1;}
    return 0;
}
int ps2_native_float_operands(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t *input,unsigned inputs,const uint32_t *output,unsigned outputs,uint32_t p,unsigned width,unsigned height)
{
    if(inputs>128 || outputs>128 || inputs+outputs>128){errno=EINVAL;return -1;}
    return validate(w,f,input,inputs,output,outputs,p,width,height);
}
static int product24(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t a[3],const uint32_t b[3],uint32_t p,unsigned width,unsigned height)
{
    struct ps2_surface_client *gs=w->gs;
    uint32_t sum[]={w->remainder[0],w->remainder[1],w->remainder[2],w->remainder[3],w->shifted[0],w->shifted[1]};
    for(unsigned i=0;i<6;++i)if(ps2_surface_solid_logic(gs,sum[i],0,0,width,height,0,0xffffff,0))return -1;
    /* Nine byte products, each retaining its low and high byte, accumulate
     * into a 48-bit register. Every carry propagates through GS passes. */
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)
        if(ps2_native_product(f,a[i],b[j],w->term,w->temporary,p,width,height) ||
            accumulate(w,sum,w->term,i+j,p,width,height) ||
            accumulate(w,sum,w->temporary,i+j+1,p,width,height))return -1;
    return 0;
}
int ps2_native_product24(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t a[3],const uint32_t b[3],const uint32_t out[6],uint32_t p,unsigned width,unsigned height)
{
    if(!a || !b){errno=EINVAL;return -1;}
    uint32_t input[]={a[0],a[1],a[2],b[0],b[1],b[2]};
    if(validate(w,f,input,6,out,6,p,width,height) || product24(w,f,a,b,p,width,height))return -1;
    uint32_t sum[]={w->remainder[0],w->remainder[1],w->remainder[2],w->remainder[3],w->shifted[0],w->shifted[1]};
    for(unsigned i=0;i<6;++i)if(transfer(w,sum[i],out[i],width,height,PS2GS_TRANSFER_REPLACE))return -1;
    return 0;
}
static int lookup(struct ps2_divide_workspace *w,uint32_t source,uint32_t out,uint32_t p,
    const unsigned char table[256],unsigned width,unsigned height)
{return ps2_surface_lut_palette(w->gs,p,table) || ps2_surface_channel_lut(w->gs,source,out,p,width,height,0xffffff)?-1:0;}
static int mask_zero(struct ps2_divide_workspace *w,uint32_t value,uint32_t nonzero,
    uint32_t p,unsigned width,unsigned height)
{
    uint32_t table[256];for(unsigned i=0;i<256;++i)table[i]=i?0x80000000:0;
    for(unsigned c=0;c<3;++c)
        if(transfer(w,value,w->term,width,height,PS2GS_TRANSFER_REPLACE) ||
            ps2_surface_map_palette(w->gs,p,table) ||
            ps2_surface_channel_map(w->gs,nonzero,w->term,p,width,height,0xff000000,c) ||
            ps2_surface_rgba_rectangle(w->gs,w->term,value,0,0,0,0,width,height,255U<<(c*8),PS2GS_RGBA_MULTIPLY))return -1;
    return 0;
}
static int select_plane(struct ps2_divide_workspace *w,uint32_t out,uint32_t yes,uint32_t no,
    uint32_t predicate,uint32_t p,unsigned width,unsigned height)
{
    uint32_t table[256];
    for(unsigned branch=0;branch<2;++branch){
        uint32_t source=branch?yes:no,target=branch?w->selected:w->retained;
        for(unsigned c=0;c<3;++c){
            for(unsigned i=0;i<256;++i)table[i]=(uint32_t)((!!i)==branch?128:0)<<24;
            if(transfer(w,source,w->term,width,height,PS2GS_TRANSFER_REPLACE) ||
                ps2_surface_map_palette(w->gs,p,table) ||
                ps2_surface_channel_map(w->gs,predicate,w->term,p,width,height,0xff000000,c) ||
                ps2_surface_rgba_rectangle(w->gs,w->term,target,0,0,0,0,width,height,255U<<(c*8),PS2GS_RGBA_MULTIPLY))return -1;
        }
    }
    return transfer(w,w->selected,out,width,height,PS2GS_TRANSFER_REPLACE) ||
        transfer(w,w->retained,out,width,height,PS2GS_TRANSFER_ADD)?-1:0;
}
static int tiny_product_flags(struct ps2_divide_workspace *,const uint32_t [2],uint32_t,unsigned,unsigned);
static int tiny_product(struct ps2_divide_workspace *,struct ps2_filter_workspace *,const uint32_t [6],uint32_t,unsigned,unsigned);
static int tiny_numerator(struct ps2_divide_workspace *,const uint32_t [4],const uint32_t [4],uint32_t,uint32_t,uint32_t,uint32_t,unsigned,unsigned);
static int tiny_nonzero(struct ps2_divide_workspace *,const uint32_t [4],uint32_t,uint32_t,unsigned,unsigned);
static int float_multiply_core(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t a[4],const uint32_t b[4],const uint32_t out[4],uint32_t p,unsigned width,unsigned height,unsigned tiny)
{
    if(!a || !b){errno=EINVAL;return -1;}
    uint32_t input[]={a[0],a[1],a[2],a[3],b[0],b[1],b[2],b[3]};
    if(validate(w,f,input,8,out,4,p,width,height))return -1;
    unsigned char table[256];uint32_t exponents[]={w->borrow,w->selected};
    const uint32_t *operands[]={a,b};uint32_t high[]={w->difference[0],w->difference[3]};
    for(unsigned operand=0;operand<2;++operand){
        for(unsigned i=0;i<256;++i)table[i]=(i<<1)&255;
        if(lookup(w,operands[operand][3],exponents[operand],p,table,width,height))return -1;
        for(unsigned i=0;i<256;++i)table[i]=i>>7;
        if(lookup(w,operands[operand][2],w->term,p,table,width,height) ||
            transfer(w,w->term,exponents[operand],width,height,PS2GS_TRANSFER_ADD))return -1;
        for(unsigned i=0;i<256;++i)table[i]=(i&127)|128;
        if(lookup(w,operands[operand][2],high[operand],p,table,width,height) ||
            mask_zero(w,high[operand],exponents[operand],p,width,height))return -1;
    }
    for(unsigned i=0;i<256;++i)table[i]=!!i;
    if(lookup(w,exponents[0],w->term,p,table,width,height) || lookup(w,exponents[1],w->temporary,p,table,width,height) ||
        transfer(w,w->term,w->shifted[3],width,height,PS2GS_TRANSFER_REPLACE) ||
        transfer(w,w->temporary,w->shifted[3],width,height,PS2GS_TRANSFER_ADD))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i==2;
    if(lookup(w,w->shifted[3],w->temporary,p,table,width,height) ||
        transfer(w,w->temporary,w->shifted[3],width,height,PS2GS_TRANSFER_REPLACE) ||
        transfer(w,exponents[0],w->shifted[2],width,height,PS2GS_TRANSFER_REPLACE) ||
        transfer(w,exponents[1],w->shifted[2],width,height,PS2GS_TRANSFER_ADD))return -1;
    for(unsigned i=0;i<256;++i)table[i]=(i-127)&255;
    if(lookup(w,w->shifted[2],w->term,p,table,width,height) ||
        transfer(w,w->term,w->shifted[2],width,height,PS2GS_TRANSFER_REPLACE))return -1;
    if(tiny && tiny_product_flags(w,exponents,p,width,height))return -1;
    uint32_t ma[]={a[0],a[1],high[0]},mb[]={b[0],b[1],high[1]};
    if(product24(w,f,ma,mb,p,width,height))return -1;
    uint32_t product[]={w->remainder[0],w->remainder[1],w->remainder[2],w->remainder[3],w->shifted[0],w->shifted[1]};
    if(tiny && tiny_product(w,f,product,p,width,height))return -1;
    uint32_t normal=w->difference[3],exponent=w->shifted[2],valid=w->shifted[3];
    for(unsigned i=0;i<256;++i)table[i]=i>>7;
    if(lookup(w,product[5],normal,p,table,width,height))return -1;
    for(unsigned byte=0;byte<3;++byte){
        for(unsigned i=0;i<256;++i)table[i]=i>>7;
        if(lookup(w,product[byte+2],w->difference[byte],p,table,width,height))return -1;
        for(unsigned i=0;i<256;++i)table[i]=(i<<1)&255;
        if(lookup(w,product[byte+3],w->term,p,table,width,height) ||
            transfer(w,w->term,w->difference[byte],width,height,PS2GS_TRANSFER_ADD) ||
            select_plane(w,w->difference[byte],product[byte+3],w->difference[byte],normal,p,width,height))return -1;
    }
    for(unsigned i=0;i<256;++i)table[i]=(i&127)>64;
    if(lookup(w,product[2],w->borrow,p,table,width,height))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i>128;
    if(lookup(w,product[2],w->carry,p,table,width,height) ||
        select_plane(w,w->borrow,w->carry,w->borrow,normal,p,width,height))return -1;
    for(unsigned i=0;i<256;++i)table[i]=(i&127)==64;
    if(lookup(w,product[2],w->carry,p,table,width,height))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i==128;
    if(lookup(w,product[2],w->saturated,p,table,width,height) ||
        select_plane(w,w->carry,w->saturated,w->carry,normal,p,width,height))return -1;
    for(unsigned i=0;i<256;++i)table[i]=!!i;
    if(lookup(w,product[0],w->term,p,table,width,height) || lookup(w,product[1],w->temporary,p,table,width,height) ||
        transfer(w,w->term,w->saturated,width,height,PS2GS_TRANSFER_REPLACE) ||
        transfer(w,w->temporary,w->saturated,width,height,PS2GS_TRANSFER_ADD))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i&1;
    if(lookup(w,w->difference[0],w->temporary,p,table,width,height) ||
        transfer(w,w->temporary,w->saturated,width,height,PS2GS_TRANSFER_ADD))return -1;
    for(unsigned i=0;i<256;++i)table[i]=!!i;
    if(lookup(w,w->saturated,w->temporary,p,table,width,height))return -1;
    for(unsigned i=0;i<256;++i)table[i]=!i;
    if(lookup(w,w->carry,w->term,p,table,width,height) ||
        transfer(w,w->term,w->temporary,width,height,PS2GS_TRANSFER_SUBTRACT) ||
        transfer(w,w->borrow,w->temporary,width,height,PS2GS_TRANSFER_ADD) ||
        add(w,w->difference[0],w->temporary,p,width,height) ||
        add(w,w->difference[1],w->carry,p,width,height) || add(w,w->difference[2],w->carry,p,width,height) ||
        transfer(w,normal,exponent,width,height,PS2GS_TRANSFER_ADD) ||
        transfer(w,w->carry,exponent,width,height,PS2GS_TRANSFER_ADD))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i?128:0;
    if(lookup(w,w->carry,w->term,p,table,width,height) ||
        transfer(w,w->term,w->difference[2],width,height,PS2GS_TRANSFER_ADD) ||
        transfer(w,w->difference[0],out[0],width,height,PS2GS_TRANSFER_REPLACE) ||
        transfer(w,w->difference[1],out[1],width,height,PS2GS_TRANSFER_REPLACE))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i&127;
    if(lookup(w,w->difference[2],out[2],p,table,width,height))return -1;
    for(unsigned i=0;i<256;++i)table[i]=(i&1)*128;
    if(lookup(w,exponent,w->term,p,table,width,height) ||
        transfer(w,w->term,out[2],width,height,PS2GS_TRANSFER_ADD))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i>>1;
    if(lookup(w,exponent,out[3],p,table,width,height))return -1;
    for(unsigned i=0;i<4;++i)if(mask_zero(w,out[i],valid,p,width,height))return -1;
    if(tiny){const uint32_t result[]={f->tap,f->term,f->sum[0],f->sum[1]};
        for(unsigned i=0;i<4;++i)if(select_plane(w,out[i],result[i],out[i],f->carry,p,width,height))return -1;
    }
    return 0;
}
int ps2_native_float_multiply(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t a[4],const uint32_t b[4],const uint32_t out[4],uint32_t p,unsigned width,unsigned height)
{return float_multiply_core(w,f,a,b,out,p,width,height,0);}

static int float_divide_core(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t a[4],const uint32_t b[4],const uint32_t out[4],uint32_t p,unsigned width,unsigned height,unsigned tiny)
{
    if(!a || !b){errno=EINVAL;return -1;}
    uint32_t input[]={a[0],a[1],a[2],a[3],b[0],b[1],b[2],b[3]};
    if(validate(w,f,input,8,out,4,p,width,height))return -1;
    /* Keep the operands and quotient digits outside the restoring divider's
     * register bank. All source reads finish before output publication. */
    /* Division does not use the multiply/filter register bank. Borrow its
     * seven pages rather than allocating another copy of that storage. */
    uint32_t owned[7]={0},registers[14]={f->tap,f->term,f->sum[0],f->sum[1],f->sum[2],f->saturated,f->carry};
    uint32_t *d=registers,*n=registers+3,*q=registers+7;
    uint32_t exponent=0,valid=0;int result=-1,error=0;
    for(unsigned i=0;i<7;++i){if(ps2_surface_create(w->gs,64,32,32,owned+i))goto done;registers[i+7]=owned[i];}
    exponent=registers[12];valid=registers[13];
    unsigned char table[256];uint32_t ea=w->borrow,eb=w->carry;
    const uint32_t *operands[]={a,b};uint32_t e[]={ea,eb};
    for(unsigned operand=0;operand<2;++operand){
        for(unsigned i=0;i<256;++i)table[i]=(i<<1)&255;
        if(lookup(w,operands[operand][3],e[operand],p,table,width,height))goto done;
        for(unsigned i=0;i<256;++i)table[i]=i>>7;
        if(lookup(w,operands[operand][2],w->term,p,table,width,height) ||
            transfer(w,w->term,e[operand],width,height,PS2GS_TRANSFER_ADD))goto done;
    }
    if(tiny && tiny_numerator(w,a,n,ea,valid,q[0],p,width,height))goto done;
    for(unsigned i=0;i<256;++i)table[i]=!!i;
    if((!tiny && lookup(w,ea,valid,p,table,width,height)) ||
        transfer(w,ea,exponent,width,height,PS2GS_TRANSFER_REPLACE))goto done;
    /* GS subtraction clamps to zero. Form the biased exponent difference
     * with modular addition instead, retaining negative EA-EB differences. */
    for(unsigned i=0;i<256;++i)table[i]=(126-i)&255;
    if(lookup(w,eb,w->term,p,table,width,height) ||
        transfer(w,w->term,exponent,width,height,PS2GS_TRANSFER_ADD))goto done;
    for(unsigned i=0;i<2;++i)if((!tiny && transfer(w,a[i],n[i],width,height,PS2GS_TRANSFER_REPLACE)) ||
        transfer(w,b[i],d[i],width,height,PS2GS_TRANSFER_REPLACE))goto done;
    for(unsigned i=0;i<256;++i)table[i]=(i&127)|128;
    if((!tiny && lookup(w,a[2],n[2],p,table,width,height)) || mask_zero(w,n[2],valid,p,width,height) ||
        lookup(w,b[2],d[2],p,table,width,height) ||
        ps2_surface_solid_logic(w->gs,n[3],0,0,width,height,0,0xffffff,0))goto done;
    for(unsigned digit=0;digit<5;++digit){
        if(ps2_native_divide_u32_u24(w,n,d,q[digit],p,width,height))goto done;
        if(digit<4){
            if(ps2_surface_solid_logic(w->gs,n[0],0,0,width,height,0,0xffffff,0))goto done;
            for(unsigned byte=1;byte<4;++byte)
                if(transfer(w,w->remainder[byte-1],n[byte],width,height,PS2GS_TRANSFER_REPLACE))goto done;
        }
    }
    /* Mantissa ratio is in [1/2,2). q[0] selects the one-bit normalization.
     * Four fractional digits plus the final remainder retain every bit
     * needed for correctly rounded binary32 division. */
    for(unsigned byte=0;byte<3;++byte){
        for(unsigned i=0;i<256;++i)table[i]=i>>1;
        if(lookup(w,q[3-byte],w->difference[byte],p,table,width,height))goto done;
        for(unsigned i=0;i<256;++i)table[i]=byte==2?128:(i&1)*128;
        if(lookup(w,q[byte==2?0:2-byte],w->term,p,table,width,height) ||
            transfer(w,w->term,w->difference[byte],width,height,PS2GS_TRANSFER_ADD) ||
            select_plane(w,w->difference[byte],w->difference[byte],q[3-byte],q[0],p,width,height))goto done;
    }
    for(unsigned i=0;i<256;++i)table[i]=i&1;
    if(lookup(w,q[3],w->borrow,p,table,width,height))goto done;
    for(unsigned i=0;i<256;++i)table[i]=i>>7;
    if(lookup(w,q[4],w->carry,p,table,width,height) ||
        select_plane(w,w->borrow,w->borrow,w->carry,q[0],p,width,height))goto done;
    for(unsigned i=0;i<256;++i)table[i]=!!i;
    if(lookup(w,q[4],w->carry,p,table,width,height))goto done;
    for(unsigned i=0;i<256;++i)table[i]=!!(i&127);
    if(lookup(w,q[4],w->saturated,p,table,width,height) ||
        select_plane(w,w->carry,w->carry,w->saturated,q[0],p,width,height))goto done;
    for(unsigned byte=0;byte<4;++byte){
        for(unsigned i=0;i<256;++i)table[i]=!!i;
        if(lookup(w,w->remainder[byte],w->term,p,table,width,height) ||
            transfer(w,w->term,w->carry,width,height,PS2GS_TRANSFER_ADD))goto done;
    }
    for(unsigned i=0;i<256;++i)table[i]=i&1;
    if(lookup(w,w->difference[0],w->term,p,table,width,height) ||
        transfer(w,w->term,w->carry,width,height,PS2GS_TRANSFER_ADD))goto done;
    for(unsigned i=0;i<256;++i)table[i]=!!i;
    if(lookup(w,w->carry,w->temporary,p,table,width,height))goto done;
    for(unsigned i=0;i<256;++i)table[i]=!i;
    if(lookup(w,w->borrow,w->term,p,table,width,height) ||
        transfer(w,w->term,w->temporary,width,height,PS2GS_TRANSFER_SUBTRACT) ||
        add(w,w->difference[0],w->temporary,p,width,height) ||
        add(w,w->difference[1],w->carry,p,width,height) ||
        add(w,w->difference[2],w->carry,p,width,height) ||
        transfer(w,q[0],exponent,width,height,PS2GS_TRANSFER_ADD) ||
        transfer(w,w->carry,exponent,width,height,PS2GS_TRANSFER_ADD))goto done;
    for(unsigned i=0;i<256;++i)table[i]=i?128:0;
    if(lookup(w,w->carry,w->term,p,table,width,height) ||
        transfer(w,w->term,w->difference[2],width,height,PS2GS_TRANSFER_ADD) ||
        transfer(w,w->difference[0],out[0],width,height,PS2GS_TRANSFER_REPLACE) ||
        transfer(w,w->difference[1],out[1],width,height,PS2GS_TRANSFER_REPLACE))goto done;
    for(unsigned i=0;i<256;++i)table[i]=i&127;
    if(lookup(w,w->difference[2],out[2],p,table,width,height))goto done;
    for(unsigned i=0;i<256;++i)table[i]=(i&1)*128;
    if(lookup(w,exponent,w->term,p,table,width,height) ||
        transfer(w,w->term,out[2],width,height,PS2GS_TRANSFER_ADD))goto done;
    for(unsigned i=0;i<256;++i)table[i]=i>>1;
    if(lookup(w,exponent,out[3],p,table,width,height))goto done;
    for(unsigned i=0;i<4;++i)if(mask_zero(w,out[i],valid,p,width,height))goto done;
    result=0;
done:
    error=errno;
    for(unsigned i=0;i<7;++i)if(owned[i] && !w->gs->error && ps2_surface_destroy(w->gs,owned[i])){
        if(!result)error=errno;
        result=-1;
    }
    errno=error;return result;
}
int ps2_native_float_divide(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t a[4],const uint32_t b[4],const uint32_t out[4],uint32_t p,unsigned width,unsigned height)
{return float_divide_core(w,f,a,b,out,p,width,height,0);}

static int clear_plane(struct ps2_divide_workspace *w,uint32_t h,unsigned width,unsigned height)
{return ps2_surface_solid_logic(w->gs,h,0,0,width,height,0,0xffffff,0);}
/* Shift a four-byte register into shifted[], retaining any lost bit in the
 * low bit. The table and transfer operations never read pixels on the CPU. */
static int right_jam(struct ps2_divide_workspace *w,const uint32_t word[4],
    unsigned shift,uint32_t p,unsigned width,unsigned height)
{
    unsigned char table[256];unsigned bytes=shift/8,bits=shift%8;
    if(clear_plane(w,w->borrow,width,height))return -1;
    for(unsigned byte=0;byte<4;++byte){
        for(unsigned i=0;i<256;++i)table[i]=byte<bytes?!!i:byte==bytes && bits?!!(i&((1U<<bits)-1)):0;
        if(lookup(w,word[byte],w->term,p,table,width,height) ||
            transfer(w,w->term,w->borrow,width,height,PS2GS_TRANSFER_ADD))return -1;
        if(byte+bytes<4){
            for(unsigned i=0;i<256;++i)table[i]=i>>bits;
            if(lookup(w,word[byte+bytes],w->shifted[byte],p,table,width,height))return -1;
            if(bits && byte+bytes+1<4){
                for(unsigned i=0;i<256;++i)table[i]=(i<<(8-bits))&255;
                if(lookup(w,word[byte+bytes+1],w->term,p,table,width,height) ||
                    transfer(w,w->term,w->shifted[byte],width,height,PS2GS_TRANSFER_ADD))return -1;
            }
        }else if(clear_plane(w,w->shifted[byte],width,height))return -1;
    }
    for(unsigned i=0;i<256;++i)table[i]=!!i;
    if(lookup(w,w->borrow,w->temporary,p,table,width,height))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i&1;
    return lookup(w,w->shifted[0],w->term,p,table,width,height) ||
        transfer(w,w->term,w->temporary,width,height,PS2GS_TRANSFER_SUBTRACT) ||
        transfer(w,w->temporary,w->shifted[0],width,height,PS2GS_TRANSFER_ADD)?-1:0;
}
static int left_word(struct ps2_divide_workspace *w,const uint32_t word[4],
    unsigned shift,uint32_t p,unsigned width,unsigned height)
{
    unsigned char table[256];unsigned bytes=shift/8,bits=shift%8;
    for(unsigned byte=0;byte<4;++byte){
        if(byte>=bytes){
            for(unsigned i=0;i<256;++i)table[i]=(i<<bits)&255;
            if(lookup(w,word[byte-bytes],w->shifted[byte],p,table,width,height))return -1;
            if(bits && byte>bytes){
                for(unsigned i=0;i<256;++i)table[i]=i>>(8-bits);
                if(lookup(w,word[byte-bytes-1],w->term,p,table,width,height) ||
                    transfer(w,w->term,w->shifted[byte],width,height,PS2GS_TRANSFER_ADD))return -1;
            }
        }else if(clear_plane(w,w->shifted[byte],width,height))return -1;
    }
    return 0;
}
static int exponent_plane(struct ps2_divide_workspace *w,const uint32_t value[4],
    uint32_t out,uint32_t p,unsigned width,unsigned height)
{
    unsigned char table[256];for(unsigned i=0;i<256;++i)table[i]=(i<<1)&255;
    if(lookup(w,value[3],out,p,table,width,height))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i>>7;
    return lookup(w,value[2],w->term,p,table,width,height) ||
        transfer(w,w->term,out,width,height,PS2GS_TRANSFER_ADD)?-1:0;
}
static int mantissa_grs(struct ps2_divide_workspace *w,const uint32_t value[4],uint32_t exponent,
    const uint32_t out[4],uint32_t p,unsigned width,unsigned height)
{
    unsigned char table[256];
    for(unsigned byte=0;byte<3;++byte){
        for(unsigned i=0;i<256;++i)table[i]=(i<<3)&255;
        if(lookup(w,value[byte],out[byte],p,table,width,height))return -1;
        if(byte){
            for(unsigned i=0;i<256;++i)table[i]=i>>5;
            if(lookup(w,value[byte-1],w->term,p,table,width,height) ||
                transfer(w,w->term,out[byte],width,height,PS2GS_TRANSFER_ADD))return -1;
        }
    }
    for(unsigned i=0;i<256;++i)table[i]=((i&127)>>5)|4;
    return lookup(w,value[2],out[3],p,table,width,height) ||
        mask_zero(w,out[3],exponent,p,width,height)?-1:0;
}
int ps2_native_float_add(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t a[4],const uint32_t b[4],const uint32_t out[4],uint32_t p,
    unsigned width,unsigned height,unsigned subtract)
{
    if(!a || !b || subtract>1){errno=EINVAL;return -1;}
    uint32_t input[]={a[0],a[1],a[2],a[3],b[0],b[1],b[2],b[3]};
    if(validate(w,f,input,8,out,4,p,width,height))return -1;
    uint32_t owned[5]={0},registers[12]={f->tap,f->term,f->sum[0],f->sum[1],f->sum[2],f->saturated,f->carry};
    uint32_t *large=registers,*small=registers+4;int result=-1,error=0;
    for(unsigned i=0;i<5;++i){if(ps2_surface_create(w->gs,64,32,32,owned+i))goto done;registers[i+7]=owned[i];}
    uint32_t exponent=registers[8],distance=registers[9],valid=registers[10],predicate=registers[11];
    uint32_t ea=w->difference[0],eb=w->difference[1];unsigned char table[256];
    if(exponent_plane(w,a,ea,p,width,height) || exponent_plane(w,b,eb,p,width,height) ||
        mantissa_grs(w,a,ea,large,p,width,height) || mantissa_grs(w,b,eb,small,p,width,height) ||
        transfer(w,eb,predicate,width,height,PS2GS_TRANSFER_REPLACE) ||
        transfer(w,ea,predicate,width,height,PS2GS_TRANSFER_SUBTRACT) ||
        select_plane(w,exponent,eb,ea,predicate,p,width,height) ||
        transfer(w,ea,distance,width,height,PS2GS_TRANSFER_REPLACE) ||
        transfer(w,eb,distance,width,height,PS2GS_TRANSFER_SUBTRACT) ||
        transfer(w,predicate,distance,width,height,PS2GS_TRANSFER_ADD))goto done;
    for(unsigned byte=0;byte<4;++byte)
        if(select_plane(w,w->remainder[byte],small[byte],large[byte],predicate,p,width,height) ||
            select_plane(w,w->shifted[byte],large[byte],small[byte],predicate,p,width,height))goto done;
    for(unsigned byte=0;byte<4;++byte)
        if(transfer(w,w->remainder[byte],large[byte],width,height,PS2GS_TRANSFER_REPLACE) ||
            transfer(w,w->shifted[byte],small[byte],width,height,PS2GS_TRANSFER_REPLACE))goto done;
    for(unsigned stage=0;stage<6;++stage){unsigned shift=1U<<stage;
        for(unsigned i=0;i<256;++i)table[i]=stage==5?i>=32:!!(i&shift);
        if(lookup(w,distance,predicate,p,table,width,height) || right_jam(w,small,shift,p,width,height))goto done;
        for(unsigned byte=0;byte<4;++byte)
            if(select_plane(w,small[byte],w->shifted[byte],small[byte],predicate,p,width,height))goto done;
    }
    if(ps2_surface_solid_logic(w->gs,w->borrow,0,0,width,height,subtract?0x010101:0,0xffffff,3))goto done;
    for(unsigned byte=0;byte<4;++byte){
        for(unsigned i=0;i<256;++i)table[i]=subtract?255-i:i;
        if(lookup(w,small[byte],w->term,p,table,width,height) || add(w,large[byte],w->term,p,width,height) ||
            transfer(w,w->carry,w->difference[3],width,height,PS2GS_TRANSFER_REPLACE) ||
            add(w,large[byte],w->borrow,p,width,height) ||
            transfer(w,w->carry,w->borrow,width,height,PS2GS_TRANSFER_REPLACE) ||
            transfer(w,w->difference[3],w->borrow,width,height,PS2GS_TRANSFER_ADD))goto done;
    }
    if(clear_plane(w,valid,width,height))goto done;
    for(unsigned byte=0;byte<4;++byte){
        for(unsigned i=0;i<256;++i)table[i]=!!i;
        if(lookup(w,large[byte],w->term,p,table,width,height) ||
            transfer(w,w->term,valid,width,height,PS2GS_TRANSFER_ADD))goto done;
    }
    if(subtract){
        for(unsigned stage=0;stage<5;++stage){unsigned shift=16U>>stage,cut=27-shift;
            if(clear_plane(w,predicate,width,height))goto done;
            for(unsigned byte=cut/8;byte<4;++byte){
                for(unsigned i=0;i<256;++i)table[i]=byte==cut/8?i>=(1U<<(cut%8)):!!i;
                if(lookup(w,large[byte],w->term,p,table,width,height) ||
                    transfer(w,w->term,predicate,width,height,PS2GS_TRANSFER_ADD))goto done;
            }
            for(unsigned i=0;i<256;++i)table[i]=!i;
            if(lookup(w,predicate,w->temporary,p,table,width,height) ||
                transfer(w,w->temporary,predicate,width,height,PS2GS_TRANSFER_REPLACE) ||
                left_word(w,large,shift,p,width,height))goto done;
            for(unsigned byte=0;byte<4;++byte)
                if(select_plane(w,large[byte],w->shifted[byte],large[byte],predicate,p,width,height))goto done;
            for(unsigned i=0;i<256;++i)table[i]=i?shift:0;
            if(lookup(w,predicate,w->term,p,table,width,height) ||
                transfer(w,w->term,exponent,width,height,PS2GS_TRANSFER_SUBTRACT))goto done;
        }
    }else{
        for(unsigned i=0;i<256;++i)table[i]=!!(i&8);
        if(lookup(w,large[3],predicate,p,table,width,height) || right_jam(w,large,1,p,width,height))goto done;
        for(unsigned byte=0;byte<4;++byte)
            if(select_plane(w,large[byte],w->shifted[byte],large[byte],predicate,p,width,height))goto done;
        if(transfer(w,predicate,exponent,width,height,PS2GS_TRANSFER_ADD))goto done;
    }
    for(unsigned i=0;i<256;++i)table[i]=((i&7)>4 || ((i&7)==4 && (i&8)));
    if(lookup(w,large[0],w->temporary,p,table,width,height))goto done;
    for(unsigned byte=0;byte<3;++byte){
        for(unsigned i=0;i<256;++i)table[i]=i>>3;
        if(lookup(w,large[byte],w->difference[byte],p,table,width,height))goto done;
        for(unsigned i=0;i<256;++i)table[i]=(i<<5)&255;
        if(lookup(w,large[byte+1],w->term,p,table,width,height) ||
            transfer(w,w->term,w->difference[byte],width,height,PS2GS_TRANSFER_ADD))goto done;
    }
    if(add(w,w->difference[0],w->temporary,p,width,height) ||
        add(w,w->difference[1],w->carry,p,width,height) || add(w,w->difference[2],w->carry,p,width,height) ||
        transfer(w,w->carry,exponent,width,height,PS2GS_TRANSFER_ADD))goto done;
    for(unsigned i=0;i<256;++i)table[i]=i?128:0;
    if(lookup(w,w->carry,w->term,p,table,width,height) ||
        transfer(w,w->term,w->difference[2],width,height,PS2GS_TRANSFER_ADD) ||
        transfer(w,w->difference[0],out[0],width,height,PS2GS_TRANSFER_REPLACE) ||
        transfer(w,w->difference[1],out[1],width,height,PS2GS_TRANSFER_REPLACE))goto done;
    for(unsigned i=0;i<256;++i)table[i]=i&127;
    if(lookup(w,w->difference[2],out[2],p,table,width,height))goto done;
    for(unsigned i=0;i<256;++i)table[i]=(i&1)*128;
    if(lookup(w,exponent,w->term,p,table,width,height) ||
        transfer(w,w->term,out[2],width,height,PS2GS_TRANSFER_ADD))goto done;
    for(unsigned i=0;i<256;++i)table[i]=i>>1;
    if(lookup(w,exponent,out[3],p,table,width,height))goto done;
    for(unsigned i=0;i<4;++i)if(mask_zero(w,out[i],valid,p,width,height))goto done;
    result=0;
done:
    error=errno;
    for(unsigned i=0;i<5;++i)if(owned[i] && !w->gs->error && ps2_surface_destroy(w->gs,owned[i])){
        if(!result)error=errno;
        result=-1;
    }
    errno=error;return result;
}
static int right_plain(struct ps2_divide_workspace *w,const uint32_t word[4],
    unsigned shift,uint32_t p,unsigned width,unsigned height)
{
    unsigned char table[256];unsigned bytes=shift/8,bits=shift%8;
    for(unsigned byte=0;byte<4;++byte){
        if(byte+bytes<4){
            for(unsigned i=0;i<256;++i)table[i]=i>>bits;
            if(lookup(w,word[byte+bytes],w->shifted[byte],p,table,width,height))return -1;
            if(bits && byte+bytes+1<4){
                for(unsigned i=0;i<256;++i)table[i]=(i<<(8-bits))&255;
                if(lookup(w,word[byte+bytes+1],w->term,p,table,width,height) ||
                    transfer(w,w->term,w->shifted[byte],width,height,PS2GS_TRANSFER_ADD))return -1;
            }
        }else if(clear_plane(w,w->shifted[byte],width,height))return -1;
    }
    return 0;
}
int ps2_native_float_unorm(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t in[4],uint32_t out,const unsigned bits[3],uint32_t p,unsigned width,unsigned height)
{
    if(!in || !bits){errno=EINVAL;return -1;}
    for(unsigned c=0;c<3;++c)if(bits[c]<1 || bits[c]>8){errno=EINVAL;return -1;}
    if(validate(w,f,in,4,&out,1,p,width,height))return -1;
    uint32_t exponent=w->difference[3],distance=w->difference[2],predicate=w->difference[1],clamped=w->difference[0];
    unsigned char table[256];
    if(exponent_plane(w,in,exponent,p,width,height))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i>=127;
    if(lookup(w,exponent,clamped,p,table,width,height))return -1;
    for(unsigned c=0;c<3;++c){
        for(unsigned i=0;i<256;++i)table[i]=(150-i-bits[c])&255;
        if(ps2_surface_lut_palette(w->gs,p,table) ||
            ps2_surface_channel_lut(w->gs,exponent,distance,p,width,height,255U<<(c*8)))return -1;
    }
    for(unsigned byte=0;byte<2;++byte)
        if(transfer(w,in[byte],w->remainder[byte],width,height,PS2GS_TRANSFER_REPLACE))return -1;
    for(unsigned i=0;i<256;++i)table[i]=(i&127)|128;
    if(lookup(w,in[2],w->remainder[2],p,table,width,height) ||
        mask_zero(w,w->remainder[2],exponent,p,width,height) || clear_plane(w,w->remainder[3],width,height))return -1;
    for(unsigned stage=0;stage<6;++stage){unsigned shift=1U<<stage;
        for(unsigned i=0;i<256;++i)table[i]=stage==5?i>=32:!!(i&shift);
        if(lookup(w,distance,predicate,p,table,width,height) || right_plain(w,w->remainder,shift,p,width,height))return -1;
        for(unsigned byte=0;byte<4;++byte)
            if(select_plane(w,w->remainder[byte],w->shifted[byte],w->remainder[byte],predicate,p,width,height))return -1;
    }
    for(unsigned c=0;c<3;++c){
        for(unsigned i=0;i<256;++i)table[i]=(1U<<bits[c])-1;
        if(ps2_surface_lut_palette(w->gs,p,table) ||
            ps2_surface_channel_lut(w->gs,clamped,w->carry,p,width,height,255U<<(c*8)))return -1;
    }
    return select_plane(w,out,w->carry,w->remainder[0],clamped,p,width,height);
}
int ps2_native_float_unorm_words(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t in[4],const uint32_t out[2],const unsigned bits[3],uint32_t p,unsigned width,unsigned height)
{
    if(!in || !out || !bits){errno=EINVAL;return -1;}
    for(unsigned c=0;c<3;++c)if(bits[c]<1 || bits[c]>16){errno=EINVAL;return -1;}
    if(validate(w,f,in,4,out,2,p,width,height))return -1;
    for(unsigned i=0;i<2;++i)for(unsigned j=0;j<4;++j)if(out[i]==in[j]){errno=EINVAL;return -1;}
    uint32_t exponent=w->difference[3],distance=w->difference[2],predicate=w->difference[1],clamped=w->difference[0];
    unsigned char table[256];
    if(exponent_plane(w,in,exponent,p,width,height))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i>=127;
    if(lookup(w,exponent,clamped,p,table,width,height))return -1;
    for(unsigned c=0;c<3;++c){
        for(unsigned i=0;i<256;++i)table[i]=(150-i-bits[c])&255;
        if(ps2_surface_lut_palette(w->gs,p,table) ||
            ps2_surface_channel_lut(w->gs,exponent,distance,p,width,height,255U<<(c*8)))return -1;
    }
    for(unsigned byte=0;byte<2;++byte)
        if(transfer(w,in[byte],w->remainder[byte],width,height,PS2GS_TRANSFER_REPLACE))return -1;
    for(unsigned i=0;i<256;++i)table[i]=(i&127)|128;
    if(lookup(w,in[2],w->remainder[2],p,table,width,height) ||
        mask_zero(w,w->remainder[2],exponent,p,width,height) || clear_plane(w,w->remainder[3],width,height))return -1;
    for(unsigned stage=0;stage<6;++stage){unsigned shift=1U<<stage;
        for(unsigned i=0;i<256;++i)table[i]=stage==5?i>=32:!!(i&shift);
        if(lookup(w,distance,predicate,p,table,width,height) || right_plain(w,w->remainder,shift,p,width,height))return -1;
        for(unsigned byte=0;byte<4;++byte)
            if(select_plane(w,w->remainder[byte],w->shifted[byte],w->remainder[byte],predicate,p,width,height))return -1;
    }
    for(unsigned byte=0;byte<2;++byte){
        for(unsigned c=0;c<3;++c){
            for(unsigned i=0;i<256;++i)table[i]=(((1U<<bits[c])-1)>>(byte*8))&255;
            if(ps2_surface_lut_palette(w->gs,p,table) ||
                ps2_surface_channel_lut(w->gs,clamped,w->carry,p,width,height,255U<<(c*8)))return -1;
        }
        if(select_plane(w,out[byte],w->carry,w->remainder[byte],clamped,p,width,height))return -1;
    }
    for(unsigned i=0;i<256;++i)table[i]=!(i&128);
    if(lookup(w,in[3],w->saturated,p,table,width,height) ||
        mask_zero(w,out[0],w->saturated,p,width,height) || mask_zero(w,out[1],w->saturated,p,width,height))return -1;
    return 0;
}

int ps2_native_float_byte(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t in[4],uint32_t out,uint32_t p,unsigned width,unsigned height)
{
    if(!in){errno=EINVAL;return -1;}
    if(validate(w,f,in,4,&out,1,p,width,height))return -1;
    for(unsigned i=0;i<4;++i)if(out==in[i]){errno=EINVAL;return -1;}
    uint32_t exponent=w->difference[3],distance=w->difference[2],predicate=w->difference[1],saved=w->difference[0];
    unsigned char table[256];
    if(exponent_plane(w,in,exponent,p,width,height))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i>=150?0:150-i>32?32:150-i;
    if(lookup(w,exponent,distance,p,table,width,height))return -1;
    for(unsigned i=0;i<2;++i)if(transfer(w,in[i],w->remainder[i],width,height,PS2GS_TRANSFER_REPLACE))return -1;
    for(unsigned i=0;i<256;++i)table[i]=(i&127)|128;
    if(lookup(w,in[2],w->remainder[2],p,table,width,height) || clear_plane(w,w->remainder[3],width,height))return -1;
    for(unsigned stage=0;stage<6;++stage){unsigned shift=1U<<stage;
        for(unsigned i=0;i<256;++i)table[i]=!!(i&shift);
        if(lookup(w,distance,predicate,p,table,width,height) || right_plain(w,w->remainder,shift,p,width,height))return -1;
        for(unsigned byte=0;byte<4;++byte)
            if(select_plane(w,w->remainder[byte],w->shifted[byte],w->remainder[byte],predicate,p,width,height))return -1;
    }
    if(transfer(w,w->remainder[0],saved,width,height,PS2GS_TRANSFER_REPLACE) ||
        transfer(w,in[0],w->remainder[0],width,height,PS2GS_TRANSFER_REPLACE))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i<150?0:i-150>8?8:i-150;
    if(lookup(w,exponent,distance,p,table,width,height))return -1;
    for(unsigned stage=0;stage<4;++stage){unsigned shift=1U<<stage;
        for(unsigned i=0;i<256;++i)table[i]=!!(i&shift);
        if(lookup(w,distance,predicate,p,table,width,height))return -1;
        for(unsigned i=0;i<256;++i)table[i]=(i<<shift)&255;
        if(lookup(w,w->remainder[0],w->temporary,p,table,width,height) ||
            select_plane(w,w->remainder[0],w->temporary,w->remainder[0],predicate,p,width,height))return -1;
    }
    for(unsigned i=0;i<256;++i)table[i]=i>=150;
    if(lookup(w,exponent,predicate,p,table,width,height) ||
        select_plane(w,out,w->remainder[0],saved,predicate,p,width,height))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i>=159;
    if(lookup(w,exponent,predicate,p,table,width,height) ||
        ps2_surface_solid_logic(w->gs,w->carry,0,0,width,height,0xffffff,0xffffff,3) ||
        select_plane(w,out,w->carry,out,predicate,p,width,height))return -1;
    for(unsigned i=0;i<256;++i)table[i]=!(i&128);
    return lookup(w,in[3],predicate,p,table,width,height) || mask_zero(w,out,predicate,p,width,height)?-1:0;
}
/* Converting a positive binary32 sum to uint32 and back changes only values
 * below 2^23. In the 2^-32-scaled register, clear fraction bits below 2^-32.
 * Overflow becomes float(UINT32_MAX)*2^-32 == 1. No integer pixels leave GS. */
static int convolution_round(struct ps2_divide_workspace *w,const uint32_t value[4],
    uint32_t p,unsigned width,unsigned height)
{
    unsigned char table[256];uint32_t exponent=w->difference[0],clamped=w->difference[1],valid=w->difference[2];
    if(exponent_plane(w,value,exponent,p,width,height))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i>=127;
    if(lookup(w,exponent,clamped,p,table,width,height))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i>=95;
    if(lookup(w,exponent,valid,p,table,width,height))return -1;
    for(unsigned bit=0;bit<23;++bit){
        for(unsigned i=0;i<256;++i)table[i]=i<118-bit;
        if(lookup(w,exponent,w->remainder[1],p,table,width,height))return -1;
        for(unsigned i=0;i<256;++i)table[i]=i&(1U<<(bit%8));
        if(lookup(w,value[bit/8],w->remainder[0],p,table,width,height) ||
            mask_zero(w,w->remainder[0],w->remainder[1],p,width,height) ||
            transfer(w,w->remainder[0],value[bit/8],width,height,PS2GS_TRANSFER_SUBTRACT))return -1;
    }
    const unsigned one[]={0,0,128,63};
    for(unsigned byte=0;byte<4;++byte)
        if(ps2_surface_solid_logic(w->gs,w->remainder[0],0,0,width,height,one[byte]*0x010101,0xffffff,3) ||
            select_plane(w,value[byte],w->remainder[0],value[byte],clamped,p,width,height) ||
            mask_zero(w,value[byte],valid,p,width,height))return -1;
    return 0;
}
int ps2_native_convolution_float_tap(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    uint32_t codes,const unsigned bits[3],int32_t coefficient,const uint32_t accumulator[4],
    uint32_t p,unsigned width,unsigned height)
{
    if(!bits || !accumulator){errno=EINVAL;return -1;}
    for(unsigned c=0;c<3;++c)if(bits[c]<1 || bits[c]>8){errno=EINVAL;return -1;}
    uint32_t input[]={codes,accumulator[0],accumulator[1],accumulator[2],accumulator[3]};
    if(validate(w,f,input,5,accumulator,4,p,width,height))return -1;
    for(unsigned byte=0;byte<4;++byte)if(codes==accumulator[byte]){errno=EINVAL;return -1;}
    if(!coefficient)return 0;
    uint32_t owned[9]={0};unsigned count=coefficient<0?9:4;int result=-1,error=0;
    for(unsigned i=0;i<count;++i)if(ps2_surface_create(w->gs,64,32,32,owned+i))goto done;
    float factor=(float)(coefficient<0?-(int64_t)coefficient:(int64_t)coefficient);
    for(unsigned c=0;c<3;++c){uint32_t constants[256];
        float reciprocal=1.0f/((1U<<bits[c])-1);
        for(unsigned i=0;i<256;++i){
            float value=(i>>(8-bits[c]))*reciprocal;
            float product=value*factor;product*=0x1p-32f;memcpy(constants+i,&product,4);
        }
        for(unsigned byte=0;byte<4;++byte){unsigned char table[256];
            for(unsigned i=0;i<256;++i)table[i]=(constants[i]>>(byte*8))&255;
            if(ps2_surface_lut_palette(w->gs,p,table) ||
                ps2_surface_channel_lut(w->gs,codes,owned[byte],p,width,height,255U<<(c*8)))goto done;
        }
    }
    if(coefficient<0){
        uint32_t predicate=owned[8];unsigned char table[256];
        if(clear_plane(w,predicate,width,height))goto done;
        /* Positive binary32 bit patterns compare as unsigned integers. Scan
         * low bytes first; each higher unequal byte determines the order. */
        for(unsigned byte=0;byte<4;++byte){
            if(transfer(w,owned[byte],w->difference[0],width,height,PS2GS_TRANSFER_REPLACE) ||
                transfer(w,accumulator[byte],w->difference[0],width,height,PS2GS_TRANSFER_SUBTRACT) ||
                transfer(w,accumulator[byte],w->difference[1],width,height,PS2GS_TRANSFER_REPLACE) ||
                transfer(w,owned[byte],w->difference[1],width,height,PS2GS_TRANSFER_SUBTRACT) ||
                transfer(w,w->difference[0],predicate,width,height,PS2GS_TRANSFER_ADD_CLAMP))goto done;
            for(unsigned i=0;i<256;++i)table[i]=!i;
            if(lookup(w,w->difference[1],w->difference[2],p,table,width,height) ||
                mask_zero(w,predicate,w->difference[2],p,width,height))goto done;
        }
        for(unsigned byte=0;byte<4;++byte)
            if(select_plane(w,owned[byte+4],accumulator[byte],owned[byte],predicate,p,width,height))goto done;
    }
    if(ps2_native_float_add(w,f,accumulator,owned+(coefficient<0?4:0),accumulator,p,width,height,coefficient<0) ||
        convolution_round(w,accumulator,p,width,height))goto done;
    result=0;
done:
    error=errno;
    for(unsigned i=0;i<count;++i)if(owned[i] && !w->gs->error && ps2_surface_destroy(w->gs,owned[i])){
        if(!result)error=errno;
        result=-1;
    }
    errno=error;return result;
}
int ps2_native_convolution_float_sample(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t sample[4],int32_t coefficient,const uint32_t accumulator[4],
    uint32_t p,unsigned width,unsigned height)
{
    if(!sample || !accumulator){errno=EINVAL;return -1;}
    uint32_t input[8];memcpy(input,sample,16);memcpy(input+4,accumulator,16);
    if(validate(w,f,input,8,accumulator,4,p,width,height))return -1;
    if(!(w->gs->caps.features&PS2GS_HW_SOLID_LOGIC)){errno=ENOTSUP;return -1;}
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)
        if(sample[i]==accumulator[j]){errno=EINVAL;return -1;}
    if(!coefficient)return 0;
    uint32_t owned[13]={0};unsigned count=coefficient<0?13:8;int result=-1,error=0;
    for(unsigned i=0;i<count;++i)if(ps2_surface_create(w->gs,64,32,32,owned+i))goto done;
    /* Scaling by a power of two preserves the binary32 product rounding.
     * Coefficients and these four uniform bytes contain no sampled colour. */
    float factor=(float)(coefficient<0?-(int64_t)coefficient:(int64_t)coefficient)*0x1p-32f;
    uint32_t word;memcpy(&word,&factor,4);
    for(unsigned byte=0;byte<4;++byte)
        if(ps2_surface_solid_logic(w->gs,owned[4+byte],0,0,width,height,
            ((word>>(byte*8))&255)*0x010101,0xffffff,3))goto done;
    if(ps2_native_float_multiply(w,f,sample,owned+4,owned,p,width,height))goto done;
    if(coefficient<0){
        uint32_t predicate=owned[12];unsigned char table[256];
        if(clear_plane(w,predicate,width,height))goto done;
        for(unsigned byte=0;byte<4;++byte){
            if(transfer(w,owned[byte],w->difference[0],width,height,PS2GS_TRANSFER_REPLACE) ||
                transfer(w,accumulator[byte],w->difference[0],width,height,PS2GS_TRANSFER_SUBTRACT) ||
                transfer(w,accumulator[byte],w->difference[1],width,height,PS2GS_TRANSFER_REPLACE) ||
                transfer(w,owned[byte],w->difference[1],width,height,PS2GS_TRANSFER_SUBTRACT) ||
                transfer(w,w->difference[0],predicate,width,height,PS2GS_TRANSFER_ADD_CLAMP))goto done;
            for(unsigned i=0;i<256;++i)table[i]=!i;
            if(lookup(w,w->difference[1],w->difference[2],p,table,width,height) ||
                mask_zero(w,predicate,w->difference[2],p,width,height))goto done;
        }
        for(unsigned byte=0;byte<4;++byte)
            if(select_plane(w,owned[8+byte],accumulator[byte],owned[byte],predicate,p,width,height))goto done;
    }
    if(ps2_native_float_add(w,f,accumulator,owned+(coefficient<0?8:0),accumulator,p,width,height,coefficient<0) ||
        convolution_round(w,accumulator,p,width,height))goto done;
    result=0;
done:
    error=errno;
    for(unsigned i=0;i<count;++i)if(owned[i] && !w->gs->error && ps2_surface_destroy(w->gs,owned[i])){
        if(!result)error=errno;
        result=-1;
    }
    errno=error;return result;
}
int ps2_native_convolution_float_end(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t accumulator[4],const uint32_t out[4],uint32_t p,unsigned width,unsigned height)
{
    if(!accumulator){errno=EINVAL;return -1;}
    if(validate(w,f,accumulator,4,out,4,p,width,height))return -1;
    uint32_t exponent=w->difference[0],clamped=w->difference[1],valid=w->difference[2];unsigned char table[256];
    if(exponent_plane(w,accumulator,exponent,p,width,height))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i>=111 && i<126;
    if(lookup(w,exponent,clamped,p,table,width,height))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i>=95 && i<126;
    if(lookup(w,exponent,valid,p,table,width,height))return -1;
    for(unsigned byte=0;byte<4;++byte){
        for(unsigned i=0;i<256;++i)table[i]=byte==3?i+8:i;
        if(lookup(w,accumulator[byte],w->remainder[byte],p,table,width,height))return -1;
    }
    const unsigned one[]={0,0,128,63};
    for(unsigned byte=0;byte<4;++byte)
        if(ps2_surface_solid_logic(w->gs,w->difference[3],0,0,width,height,one[byte]*0x010101,0xffffff,3) ||
            select_plane(w,out[byte],w->difference[3],w->remainder[byte],clamped,p,width,height) ||
            mask_zero(w,out[byte],valid,p,width,height))return -1;
    return 0;
}
static int floating_one(struct ps2_divide_workspace *w,const uint32_t out[4],unsigned width,unsigned height)
{
    const unsigned bytes[]={0,0,128,63};
    for(unsigned byte=0;byte<4;++byte)
        if(ps2_surface_solid_logic(w->gs,out[byte],0,0,width,height,bytes[byte]*0x010101,0xffffff,3))return -1;
    return 0;
}
int ps2_native_float_compare(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t a[4],const uint32_t b[4],uint32_t out,uint32_t p,unsigned width,unsigned height,unsigned relation)
{
    if(!a || !b || relation>PS2_FLOAT_GE){errno=EINVAL;return -1;}
    uint32_t input[]={a[0],a[1],a[2],a[3],b[0],b[1],b[2],b[3]};
    if(validate(w,f,input,8,&out,1,p,width,height))return -1;
    unsigned swap=relation==PS2_FLOAT_LT || relation==PS2_FLOAT_GE;
    const uint32_t *left=swap?b:a,*right=swap?a:b;unsigned char table[256];
    if(clear_plane(w,w->borrow,width,height))return -1;
    /* Positive IEEE bit patterns have unsigned numeric order. A higher
     * unequal byte replaces the accumulated order of the lower bytes. */
    for(unsigned byte=0;byte<4;++byte){
        if(transfer(w,left[byte],w->difference[0],width,height,PS2GS_TRANSFER_REPLACE) ||
            transfer(w,right[byte],w->difference[0],width,height,PS2GS_TRANSFER_SUBTRACT) ||
            transfer(w,right[byte],w->difference[1],width,height,PS2GS_TRANSFER_REPLACE) ||
            transfer(w,left[byte],w->difference[1],width,height,PS2GS_TRANSFER_SUBTRACT) ||
            transfer(w,w->difference[0],w->borrow,width,height,PS2GS_TRANSFER_ADD_CLAMP))return -1;
        for(unsigned i=0;i<256;++i)table[i]=!i;
        if(lookup(w,w->difference[1],w->difference[2],p,table,width,height) ||
            mask_zero(w,w->borrow,w->difference[2],p,width,height))return -1;
    }
    for(unsigned i=0;i<256;++i)table[i]=relation==PS2_FLOAT_LE || relation==PS2_FLOAT_GE?!i:!!i;
    return lookup(w,w->borrow,out,p,table,width,height);
}
int ps2_native_float_select(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t yes[4],const uint32_t no[4],uint32_t predicate,const uint32_t out[4],
    uint32_t p,unsigned width,unsigned height)
{
    if(!yes || !no){errno=EINVAL;return -1;}
    uint32_t input[]={yes[0],yes[1],yes[2],yes[3],no[0],no[1],no[2],no[3],predicate};
    if(validate(w,f,input,9,out,4,p,width,height))return -1;
    for(unsigned byte=0;byte<4;++byte)
        if(select_plane(w,w->remainder[byte],yes[byte],no[byte],predicate,p,width,height))return -1;
    for(unsigned byte=0;byte<4;++byte)
        if(transfer(w,w->remainder[byte],out[byte],width,height,PS2GS_TRANSFER_REPLACE))return -1;
    return 0;
}
int ps2_native_float_swizzle(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t value[4],const unsigned channels[3],const uint32_t out[4],uint32_t p,unsigned width,unsigned height)
{
    if(!value || !channels || channels[0]>2 || channels[1]>2 || channels[2]>2){errno=EINVAL;return -1;}
    if(validate(w,f,value,4,out,4,p,width,height))return -1;
    uint32_t table[256];for(unsigned i=0;i<256;++i)table[i]=i*0x010101;
    if(ps2_surface_map_palette(w->gs,p,table))return -1;
    for(unsigned byte=0;byte<4;++byte)for(unsigned c=0;c<3;++c)
        if(ps2_surface_channel_map(w->gs,value[byte],w->remainder[byte],p,width,height,255U<<(8*c),channels[c]))return -1;
    for(unsigned byte=0;byte<4;++byte)
        if(transfer(w,w->remainder[byte],out[byte],width,height,PS2GS_TRANSFER_REPLACE))return -1;
    return 0;
}
int ps2_native_float_signed_compare(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t a[4],const uint32_t b[4],uint32_t out,uint32_t p,unsigned width,unsigned height,unsigned relation)
{
    if(!a || !b || relation>PS2_FLOAT_GE){errno=EINVAL;return -1;}
    uint32_t input[]={a[0],a[1],a[2],a[3],b[0],b[1],b[2],b[3]};
    if(validate(w,f,input,8,&out,1,p,width,height))return -1;
    uint32_t owned[9]={0};int result=-1,error=0;unsigned char table[256];
    for(unsigned i=0;i<9;++i)if(ps2_surface_create(w->gs,64,32,32,owned+i))goto done;
    for(unsigned operand=0;operand<2;++operand){const uint32_t *value=operand?b:a;
        for(unsigned i=0;i<256;++i)table[i]=i>>7;
        if(lookup(w,value[3],owned[8],p,table,width,height) ||
            exponent_plane(w,value,w->borrow,p,width,height) || mask_zero(w,owned[8],w->borrow,p,width,height))goto done;
        for(unsigned byte=0;byte<4;++byte){
            for(unsigned i=0;i<256;++i)table[i]=byte==3?(i&127)|128:i;
            if(lookup(w,value[byte],owned[operand*4+byte],p,table,width,height))goto done;
            for(unsigned i=0;i<256;++i)table[i]=255-i;
            if(lookup(w,value[byte],w->temporary,p,table,width,height) ||
                select_plane(w,owned[operand*4+byte],w->temporary,owned[operand*4+byte],owned[8],p,width,height))goto done;
        }
    }
    /* The monotone sign-magnitude key uses unsigned byte order. The
     * existing comparator performs exactly that order without decoding. */
    if(ps2_native_float_compare(w,f,owned,owned+4,out,p,width,height,relation))goto done;
    result=0;
done:
    error=errno;
    for(unsigned i=0;i<9;++i)if(owned[i] && !w->gs->error && ps2_surface_destroy(w->gs,owned[i])){
        if(!result)error=errno;
        result=-1;
    }
    errno=error;return result;
}
static int copy_float(struct ps2_divide_workspace *,const uint32_t [4],const uint32_t [4],unsigned,unsigned);
/* Subtract shifted[] from remainder[], replacing shifted[] with the modular
 * difference. borrow is one exactly when the unsigned subtraction is negative.
 * difference[] is deliberately untouched: the square-root keeps its root there. */
static int sqrt_subtract(struct ps2_divide_workspace *w,uint32_t p,unsigned width,unsigned height)
{
    unsigned char table[256];
    if(clear_plane(w,w->borrow,width,height))return -1;
    for(unsigned byte=0;byte<4;++byte){
        for(unsigned i=0;i<256;++i)table[i]=255-i;
        if(lookup(w,w->shifted[byte],w->temporary,p,table,width,height) ||
            transfer(w,w->remainder[byte],w->shifted[byte],width,height,PS2GS_TRANSFER_REPLACE) ||
            add(w,w->shifted[byte],w->temporary,p,width,height) ||
            transfer(w,w->carry,w->temporary,width,height,PS2GS_TRANSFER_REPLACE))return -1;
        for(unsigned i=0;i<256;++i)table[i]=!i;
        if(lookup(w,w->borrow,w->term,p,table,width,height) ||
            add(w,w->shifted[byte],w->term,p,width,height) ||
            transfer(w,w->carry,w->temporary,width,height,PS2GS_TRANSFER_ADD))return -1;
        for(unsigned i=0;i<256;++i)table[i]=!i;
        if(lookup(w,w->temporary,w->borrow,p,table,width,height))return -1;
    }
    return 0;
}
static int sqrt_shift(struct ps2_divide_workspace *w,const uint32_t word[4],
    const uint32_t out[4],unsigned shift,uint32_t p,unsigned width,unsigned height)
{
    unsigned char table[256];
    for(int byte=3;byte>=0;--byte){
        for(unsigned i=0;i<256;++i)table[i]=(i<<shift)&255;
        if(lookup(w,word[byte],w->temporary,p,table,width,height) ||
            transfer(w,w->temporary,out[byte],width,height,PS2GS_TRANSFER_REPLACE))return -1;
        if(byte){
            for(unsigned i=0;i<256;++i)table[i]=i>>(8-shift);
            if(lookup(w,word[byte-1],w->term,p,table,width,height) ||
                transfer(w,w->term,out[byte],width,height,PS2GS_TRANSFER_ADD))return -1;
        }
    }
    return 0;
}
int ps2_native_float_sqrt(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t value[4],const uint32_t out[4],uint32_t p,unsigned width,unsigned height)
{
    if(!value){errno=EINVAL;return -1;}
    if(validate(w,f,value,4,out,4,p,width,height))return -1;
    /* Borrow only the unused filter bank. No extra surfaces or CPU reads. */
    uint32_t m[]={f->tap,f->term,f->sum[0]},parity=f->sum[1],exponent=f->sum[2],valid=f->saturated;
    unsigned char table[256];
    if(exponent_plane(w,value,exponent,p,width,height))return -1;
    for(unsigned i=0;i<256;++i)table[i]=!!i;
    if(lookup(w,exponent,valid,p,table,width,height))return -1;
    for(unsigned i=0;i<256;++i)table[i]=!(i&1);
    if(lookup(w,exponent,parity,p,table,width,height))return -1;
    for(unsigned i=0;i<256;++i)table[i]=(i+127)>>1;
    if(lookup(w,exponent,w->temporary,p,table,width,height) ||
        transfer(w,w->temporary,exponent,width,height,PS2GS_TRANSFER_REPLACE))return -1;
    for(unsigned byte=0;byte<3;++byte){
        for(unsigned i=0;i<256;++i)table[i]=byte==2?(i&127)|128:i;
        if(lookup(w,value[byte],m[byte],p,table,width,height))return -1;
    }
    for(unsigned byte=0;byte<4;++byte)if(clear_plane(w,w->remainder[byte],width,height) ||
        clear_plane(w,w->difference[byte],width,height))return -1;
    /* R = M << (23 + parity), Q=floor(sqrt(R)). Feed two bits per step.
     * R has at most 48 bits, Q at most 24; the remainder/trial fit 32 bits. */
    for(int digit=23;digit>=0;--digit){
        if(sqrt_shift(w,w->remainder,w->remainder,2,p,width,height))return -1;
        for(unsigned branch=0;branch<2;++branch){
            uint32_t pair=branch?w->carry:w->temporary;
            if(clear_plane(w,pair,width,height))return -1;
            for(unsigned byte=0;byte<3;++byte){
                for(unsigned i=0;i<256;++i){unsigned bits=0;
                    for(unsigned bit=0;bit<2;++bit){int position=2*digit-23-(int)branch+(int)bit;
                        if(position>=0 && position/8==(int)byte)bits|=((i>>(position%8))&1)<<bit;}
                    table[i]=bits;
                }
                if(lookup(w,m[byte],w->term,p,table,width,height) ||
                    transfer(w,w->term,pair,width,height,PS2GS_TRANSFER_ADD))return -1;
            }
        }
        if(select_plane(w,w->borrow,w->carry,w->temporary,parity,p,width,height) ||
            transfer(w,w->borrow,w->remainder[0],width,height,PS2GS_TRANSFER_ADD) ||
            sqrt_shift(w,w->difference,w->shifted,2,p,width,height) ||
            ps2_surface_solid_logic(w->gs,w->term,0,0,width,height,0x010101,0xffffff,3) ||
            transfer(w,w->term,w->shifted[0],width,height,PS2GS_TRANSFER_ADD) ||
            sqrt_subtract(w,p,width,height))return -1;
        for(unsigned byte=0;byte<4;++byte)
            if(select_plane(w,w->remainder[byte],w->remainder[byte],w->shifted[byte],w->borrow,p,width,height))return -1;
        if(sqrt_shift(w,w->difference,w->difference,1,p,width,height))return -1;
        for(unsigned i=0;i<256;++i)table[i]=!i;
        if(lookup(w,w->borrow,w->term,p,table,width,height) ||
            transfer(w,w->term,w->difference[0],width,height,PS2GS_TRANSFER_ADD))return -1;
    }
    /* Round up iff R-Q*Q > Q. The exact midpoint has a fractional .25,
     * so an integral radicand cannot tie. Compare remainder with Q+1. */
    for(unsigned byte=0;byte<4;++byte)
        if(transfer(w,w->difference[byte],w->shifted[byte],width,height,PS2GS_TRANSFER_REPLACE))return -1;
    if(ps2_surface_solid_logic(w->gs,w->temporary,0,0,width,height,0x010101,0xffffff,3) ||
        add(w,w->shifted[0],w->temporary,p,width,height))return -1;
    for(unsigned byte=1;byte<4;++byte)if(add(w,w->shifted[byte],w->carry,p,width,height))return -1;
    if(sqrt_subtract(w,p,width,height))return -1;
    for(unsigned i=0;i<256;++i)table[i]=!i;
    if(lookup(w,w->borrow,w->temporary,p,table,width,height) ||
        add(w,w->difference[0],w->temporary,p,width,height))return -1;
    for(unsigned byte=1;byte<4;++byte)if(add(w,w->difference[byte],w->carry,p,width,height))return -1;
    if(transfer(w,w->difference[3],exponent,width,height,PS2GS_TRANSFER_ADD) ||
        transfer(w,w->difference[0],out[0],width,height,PS2GS_TRANSFER_REPLACE) ||
        transfer(w,w->difference[1],out[1],width,height,PS2GS_TRANSFER_REPLACE))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i&127;
    if(lookup(w,w->difference[2],out[2],p,table,width,height))return -1;
    for(unsigned i=0;i<256;++i)table[i]=(i&1)*128;
    if(lookup(w,exponent,w->term,p,table,width,height) ||
        transfer(w,w->term,out[2],width,height,PS2GS_TRANSFER_ADD))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i>>1;
    if(lookup(w,exponent,out[3],p,table,width,height))return -1;
    for(unsigned byte=0;byte<4;++byte)if(mask_zero(w,out[byte],valid,p,width,height))return -1;
    return 0;
}
static int float_signed_core(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t a[4],const uint32_t b[4],const uint32_t out[4],uint32_t p,
    unsigned width,unsigned height,unsigned operation,unsigned tiny,unsigned compact)
{
    if(!a || !b || operation>PS2_FLOAT_DIVIDE){errno=EINVAL;return -1;}
    uint32_t input[]={a[0],a[1],a[2],a[3],b[0],b[1],b[2],b[3]};
    if(validate(w,f,input,8,out,4,p,width,height))return -1;
    uint32_t owned[16]={0},*left=owned,*right=owned+4;
    const uint32_t *result=compact?out:owned+8;
    uint32_t sign=0,other=0,order=0,different=0;int status=-1,error=0;unsigned char table[256];
    unsigned count=compact?12:16,flags=compact?8:12;
    for(unsigned i=0;i<count;++i)if(ps2_surface_create(w->gs,64,32,32,owned+i))goto done;
    sign=owned[flags];other=owned[flags+1];order=owned[flags+2];different=owned[flags+3];
    for(unsigned byte=0;byte<4;++byte){
        for(unsigned i=0;i<256;++i)table[i]=byte==3?i&127:i;
        if(lookup(w,a[byte],left[byte],p,table,width,height) ||
            lookup(w,b[byte],right[byte],p,table,width,height))goto done;
    }
    for(unsigned i=0;i<256;++i)table[i]=i>>7;
    if(lookup(w,a[3],sign,p,table,width,height))goto done;
    for(unsigned i=0;i<256;++i)table[i]=(i>>7)^(operation==PS2_FLOAT_SUBTRACT);
    if(lookup(w,b[3],other,p,table,width,height) ||
        transfer(w,sign,different,width,height,PS2GS_TRANSFER_REPLACE) ||
        transfer(w,other,different,width,height,PS2GS_TRANSFER_ADD))goto done;
    for(unsigned i=0;i<256;++i)table[i]=i&1;
    if(lookup(w,different,w->temporary,p,table,width,height) ||
        transfer(w,w->temporary,different,width,height,PS2GS_TRANSFER_REPLACE))goto done;
    if(operation<=PS2_FLOAT_SUBTRACT){
        if(ps2_native_float_compare(w,f,left,right,order,p,width,height,PS2_FLOAT_GE) ||
            select_plane(w,sign,sign,other,order,p,width,height) ||
            ps2_native_float_select(w,f,left,right,order,result,p,width,height) ||
            ps2_native_float_select(w,f,right,left,order,right,p,width,height) ||
            copy_float(w,result,left,width,height) ||
            ps2_native_float_add(w,f,left,right,result,p,width,height,0) ||
            ps2_native_float_add(w,f,left,right,left,p,width,height,1) ||
            ps2_native_float_select(w,f,left,result,different,result,p,width,height))goto done;
    }else{
        if(transfer(w,different,sign,width,height,PS2GS_TRANSFER_REPLACE) ||
            (operation==PS2_FLOAT_MULTIPLY?float_multiply_core(w,f,left,right,result,p,width,height,tiny):
                float_divide_core(w,f,left,right,result,p,width,height,tiny)))goto done;
    }
    if((tiny?tiny_nonzero(w,result,w->borrow,p,width,height):exponent_plane(w,result,w->borrow,p,width,height)) || mask_zero(w,sign,w->borrow,p,width,height))goto done;
    for(unsigned i=0;i<256;++i)table[i]=i?128:0;
    if(lookup(w,sign,w->term,p,table,width,height) ||
        transfer(w,w->term,result[3],width,height,PS2GS_TRANSFER_ADD) ||
        (!compact && copy_float(w,result,out,width,height)))goto done;
    status=0;
done:
    error=errno;
    for(unsigned i=0;i<16;++i)if(owned[i] && !w->gs->error && ps2_surface_destroy(w->gs,owned[i])){
        if(!status)error=errno;
        status=-1;
    }
    errno=error;return status;
}
int ps2_native_float_signed(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t a[4],const uint32_t b[4],const uint32_t out[4],uint32_t p,unsigned width,unsigned height,unsigned operation)
{return float_signed_core(w,f,a,b,out,p,width,height,operation,0,0);}
int ps2_native_float_signed_tiny(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t a[4],const uint32_t b[4],const uint32_t out[4],uint32_t p,unsigned width,unsigned height,unsigned operation)
{return float_signed_core(w,f,a,b,out,p,width,height,operation,1,0);}
static int tiny_nonzero(struct ps2_divide_workspace *w,const uint32_t value[4],uint32_t out,
    uint32_t p,unsigned width,unsigned height)
{
    unsigned char table[256];for(unsigned i=0;i<256;++i)table[i]=!!i;
    if(clear_plane(w,out,width,height))return -1;
    for(unsigned byte=0;byte<4;++byte)if(lookup(w,value[byte],w->term,p,table,width,height) ||
        transfer(w,w->term,out,width,height,PS2GS_TRANSFER_ADD))return -1;
    return 0;
}
static int tiny_product_flags(struct ps2_divide_workspace *w,const uint32_t exponents[2],
    uint32_t p,unsigned width,unsigned height)
{
    unsigned char table[256];for(unsigned i=0;i<256;++i)table[i]=i<127;
    if(lookup(w,exponents[0],w->term,p,table,width,height) ||
        lookup(w,exponents[1],w->temporary,p,table,width,height) ||
        transfer(w,w->term,w->retained,width,height,PS2GS_TRANSFER_REPLACE) ||
        transfer(w,w->temporary,w->retained,width,height,PS2GS_TRANSFER_ADD))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i==2;
    if(lookup(w,w->retained,w->temporary,p,table,width,height) ||
        transfer(w,w->temporary,w->retained,width,height,PS2GS_TRANSFER_REPLACE))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i==0 || i>=128;
    if(lookup(w,w->shifted[2],w->term,p,table,width,height) ||
        transfer(w,w->term,w->retained,width,height,PS2GS_TRANSFER_ADD))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i==2;
    if(lookup(w,w->retained,w->temporary,p,table,width,height) ||
        transfer(w,w->shifted[3],w->retained,width,height,PS2GS_TRANSFER_REPLACE) ||
        transfer(w,w->temporary,w->retained,width,height,PS2GS_TRANSFER_ADD))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i==2?2:0;
    return lookup(w,w->retained,w->term,p,table,width,height) ||
        transfer(w,w->term,w->shifted[3],width,height,PS2GS_TRANSFER_ADD)?-1:0;
}
static int tiny_right_jam(struct ps2_divide_workspace *w,const uint32_t value[6],
    const uint32_t out[6],unsigned shift,uint32_t p,unsigned width,unsigned height)
{
    unsigned bytes=shift/8,bits=shift%8;unsigned char table[256];
    if(clear_plane(w,w->saturated,width,height))return -1;
    for(unsigned byte=0;byte<6;++byte){
        for(unsigned i=0;i<256;++i)table[i]=byte<bytes?!!i:byte==bytes && bits?!!(i&((1U<<bits)-1)):0;
        if(lookup(w,value[byte],w->term,p,table,width,height) ||
            transfer(w,w->term,w->saturated,width,height,PS2GS_TRANSFER_ADD))return -1;
        if(byte+bytes<6){
            for(unsigned i=0;i<256;++i)table[i]=i>>bits;
            if(lookup(w,value[byte+bytes],out[byte],p,table,width,height))return -1;
            if(bits && byte+bytes+1<6){
                for(unsigned i=0;i<256;++i)table[i]=(i<<(8-bits))&255;
                if(lookup(w,value[byte+bytes+1],w->term,p,table,width,height) ||
                    transfer(w,w->term,out[byte],width,height,PS2GS_TRANSFER_ADD))return -1;
            }
        }else if(clear_plane(w,out[byte],width,height))return -1;
    }
    for(unsigned i=0;i<256;++i)table[i]=!!i;
    if(lookup(w,w->saturated,w->temporary,p,table,width,height))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i&1;
    return lookup(w,out[0],w->term,p,table,width,height) ||
        transfer(w,w->term,w->temporary,width,height,PS2GS_TRANSFER_SUBTRACT) ||
        transfer(w,w->temporary,out[0],width,height,PS2GS_TRANSFER_ADD)?-1:0;
}
static int tiny_product(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t product[6],uint32_t p,unsigned width,unsigned height)
{
    uint32_t value[]={f->tap,f->term,f->sum[0],f->sum[1],f->sum[2],f->saturated},distance=f->carry;
    uint32_t shifted[]={w->difference[0],w->difference[1],w->difference[2],w->difference[3],w->borrow,w->carry};
    unsigned char table[256];
    for(unsigned byte=0;byte<6;++byte)if(transfer(w,product[byte],value[byte],width,height,PS2GS_TRANSFER_REPLACE))return -1;
    /* Exact subnormal code is RNE(P48 / 2^(151-Ea-Eb)). Retain three
     * guard/round/sticky bits before the single rounding, avoiding double
     * rounding of a normal temporary at the subnormal boundary. */
    for(unsigned i=0;i<256;++i)table[i]=(21-i)&255;
    if(lookup(w,w->shifted[2],distance,p,table,width,height))return -1;
    for(unsigned stage=0;stage<7;++stage){unsigned shift=1U<<stage;
        if(tiny_right_jam(w,value,shifted,shift,p,width,height))return -1;
        for(unsigned i=0;i<256;++i)table[i]=stage==6?i>=64:!!(i&shift);
        if(lookup(w,distance,w->temporary,p,table,width,height))return -1;
        for(unsigned byte=0;byte<6;++byte)
            if(select_plane(w,value[byte],shifted[byte],value[byte],w->temporary,p,width,height))return -1;
    }
    for(unsigned byte=0;byte<3;++byte){
        for(unsigned i=0;i<256;++i)table[i]=i>>3;
        if(lookup(w,value[byte],w->difference[byte],p,table,width,height))return -1;
        for(unsigned i=0;i<256;++i)table[i]=(i<<5)&255;
        if(lookup(w,value[byte+1],w->term,p,table,width,height) ||
            transfer(w,w->term,w->difference[byte],width,height,PS2GS_TRANSFER_ADD))return -1;
    }
    for(unsigned i=0;i<256;++i)table[i]=(i&7)>4 || ((i&7)==4 && (i&8));
    if(lookup(w,value[0],w->temporary,p,table,width,height) || add(w,w->difference[0],w->temporary,p,width,height) ||
        add(w,w->difference[1],w->carry,p,width,height) || add(w,w->difference[2],w->carry,p,width,height))return -1;
    for(unsigned byte=0;byte<3;++byte)if(transfer(w,w->difference[byte],value[byte],width,height,PS2GS_TRANSFER_REPLACE))return -1;
    for(unsigned i=0;i<256;++i)table[i]=!!(i&2);
    return clear_plane(w,value[3],width,height) || lookup(w,w->shifted[3],distance,p,table,width,height)?-1:0;
}
static int tiny_numerator(struct ps2_divide_workspace *w,const uint32_t value[4],const uint32_t n[4],
    uint32_t exponent,uint32_t valid,uint32_t predicate,uint32_t p,unsigned width,unsigned height)
{
    unsigned char table[256];
    for(unsigned byte=0;byte<2;++byte)if(transfer(w,value[byte],n[byte],width,height,PS2GS_TRANSFER_REPLACE))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i&127;
    if(lookup(w,value[2],n[2],p,table,width,height))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i?128:0;
    if(lookup(w,exponent,w->term,p,table,width,height) ||
        transfer(w,w->term,n[2],width,height,PS2GS_TRANSFER_ADD) || clear_plane(w,n[3],width,height) ||
        tiny_nonzero(w,n,valid,p,width,height))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i?i:1;
    if(lookup(w,exponent,w->temporary,p,table,width,height) ||
        transfer(w,w->temporary,exponent,width,height,PS2GS_TRANSFER_REPLACE))return -1;
    for(unsigned stage=0;stage<5;++stage){unsigned shift=16U>>stage,cut=24-shift;
        if(clear_plane(w,predicate,width,height))return -1;
        for(unsigned byte=cut/8;byte<4;++byte){
            for(unsigned i=0;i<256;++i)table[i]=byte==cut/8?i>=(1U<<(cut%8)):!!i;
            if(lookup(w,n[byte],w->term,p,table,width,height) ||
                transfer(w,w->term,predicate,width,height,PS2GS_TRANSFER_ADD))return -1;
        }
        for(unsigned i=0;i<256;++i)table[i]=!i;
        if(lookup(w,predicate,w->temporary,p,table,width,height) ||
            transfer(w,w->temporary,predicate,width,height,PS2GS_TRANSFER_REPLACE) || left_word(w,n,shift,p,width,height))return -1;
        for(unsigned byte=0;byte<4;++byte)
            if(select_plane(w,n[byte],w->shifted[byte],n[byte],predicate,p,width,height))return -1;
        for(unsigned i=0;i<256;++i)table[i]=(i-shift)&255;
        if(lookup(w,exponent,w->temporary,p,table,width,height) ||
            select_plane(w,exponent,w->temporary,exponent,predicate,p,width,height))return -1;
    }
    return 0;
}

/* s contains the masked source alpha; d contains destination alpha. */
static int saturate_factor_float(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t factor[4],const uint32_t s[4],const uint32_t d[4],
    uint32_t zero,uint32_t p,unsigned width,unsigned height)
{
    unsigned char table[256];
    for(unsigned i=0;i<256;++i)table[i]=!i;
    if(lookup(w,s[3],zero,p,table,width,height) || floating_one(w,factor,width,height) ||
        ps2_native_float_add(w,f,factor,d,factor,p,width,height,1) || floating_one(w,d,width,height))return -1;
    /* Replace zero denominators before division, then select Fa=1 for those
     * lanes. This preserves Saturate's defined zero-source-alpha behavior. */
    for(unsigned byte=0;byte<4;++byte)
        if(select_plane(w,s[byte],d[byte],s[byte],zero,p,width,height))return -1;
    if(ps2_native_float_divide(w,f,factor,s,factor,p,width,height) ||
        exponent_plane(w,factor,w->difference[0],p,width,height))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i>=127;
    if(lookup(w,w->difference[0],w->borrow,p,table,width,height) ||
        transfer(w,zero,w->borrow,width,height,PS2GS_TRANSFER_ADD))return -1;
    for(unsigned byte=0;byte<4;++byte)
        if(select_plane(w,factor[byte],d[byte],factor[byte],w->borrow,p,width,height))return -1;
    return 0;
}
static int saturate_factor(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    uint32_t sa,uint32_t mask,uint32_t da,const uint32_t factor[4],const uint32_t s[4],const uint32_t d[4],
    unsigned source_alpha_bits,const unsigned mask_bits[3],unsigned destination_alpha_bits,
    uint32_t zero,uint32_t p,unsigned width,unsigned height)
{
    const unsigned sbits[]={source_alpha_bits,source_alpha_bits,source_alpha_bits},
        dbits[]={destination_alpha_bits,destination_alpha_bits,destination_alpha_bits};
    return ps2_native_unorm_float(w->gs,sa,s,sbits,p,width,height) ||
        ps2_native_unorm_float(w->gs,mask,d,mask_bits,p,width,height) ||
        ps2_native_float_multiply(w,f,s,d,s,p,width,height) ||
        ps2_native_unorm_float(w->gs,da,d,dbits,p,width,height) ||
        saturate_factor_float(w,f,factor,s,d,zero,p,width,height)?-1:0;
}
static int expand_unorm(struct ps2_divide_workspace *w,uint32_t codes,const unsigned bits[3],
    uint32_t p,unsigned width,unsigned height)
{
    if(bits[0]==8 && bits[1]==8 && bits[2]==8)return 0;
    for(unsigned c=0;c<3;++c){unsigned char table[256];unsigned n=bits[c];
        for(unsigned i=0;i<256;++i){unsigned q=i&((1U<<n)-1),v=q,filled=n;
            while(filled<8){v=(v<<n)|q;filled+=n;}table[i]=v>>(filled-8);}
        if(ps2_surface_lut_palette(w->gs,p,table) ||
            ps2_surface_channel_lut(w->gs,codes,w->term,p,width,height,255U<<(c*8)))return -1;
    }
    return transfer(w,w->term,codes,width,height,PS2GS_TRANSFER_REPLACE);
}
/* s contains the masked source channel; destination retains its UNORM code. */
static int saturate_channel_float(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    uint32_t destination,const uint32_t factor[4],const uint32_t s[4],const uint32_t d[4],
    const unsigned destination_bits[3],const struct ps2_saturate_map_target *target,unsigned alpha,
    uint32_t p,unsigned width,unsigned height)
{
    const unsigned input_bits[]={target && alpha?target->input_bits:destination_bits[0],
        target && alpha?target->input_bits:destination_bits[1],target && alpha?target->input_bits:destination_bits[2]};
    uint32_t secondary=target?(alpha?target->map:target->colour):0;
    const unsigned map_bits[]={target?(alpha?target->map_bits:target->rgb_bits[0]):8,
        target?(alpha?target->map_bits:target->rgb_bits[1]):8,target?(alpha?target->map_bits:target->rgb_bits[2]):8};
    return ps2_native_float_multiply(w,f,s,factor,s,p,width,height) ||
        ps2_native_unorm_float(w->gs,destination,d,input_bits,p,width,height) ||
        ps2_native_float_add(w,f,s,d,s,p,width,height,0) ||
        ps2_native_float_unorm(w,f,s,destination,destination_bits,p,width,height) ||
        expand_unorm(w,destination,destination_bits,p,width,height) ||
        (secondary && (ps2_native_float_unorm(w,f,s,secondary,map_bits,p,width,height) ||
            expand_unorm(w,secondary,map_bits,p,width,height)))?-1:0;
}
static int saturate_channel(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    uint32_t source,uint32_t mask,uint32_t destination,const uint32_t factor[4],const uint32_t s[4],const uint32_t d[4],
    const unsigned source_bits[3],const unsigned mask_bits[3],const unsigned destination_bits[3],
    uint32_t p,unsigned width,unsigned height)
{
    return ps2_native_unorm_float(w->gs,source,s,source_bits,p,width,height) ||
        ps2_native_unorm_float(w->gs,mask,d,mask_bits,p,width,height) ||
        ps2_native_float_multiply(w,f,s,d,s,p,width,height) ||
        saturate_channel_float(w,f,destination,factor,s,d,destination_bits,NULL,0,p,width,height)?-1:0;
}
static int copy_float(struct ps2_divide_workspace *w,const uint32_t source[4],const uint32_t out[4],
    unsigned width,unsigned height)
{
    for(unsigned byte=0;byte<4;++byte)
        if(transfer(w,source[byte],out[byte],width,height,PS2GS_TRANSFER_REPLACE))return -1;
    return 0;
}
int ps2_native_saturate_alpha_float(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t sa[4],const uint32_t ma[4],uint32_t da,unsigned alpha_bits,
    uint32_t p,unsigned width,unsigned height)
{
    return ps2_native_saturate_alpha_float_mapped(w,f,sa,ma,da,alpha_bits,NULL,p,width,height);
}
int ps2_native_saturate_alpha_float_mapped(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t sa[4],const uint32_t ma[4],uint32_t da,unsigned alpha_bits,
    const struct ps2_saturate_map_target *target,uint32_t p,unsigned width,unsigned height)
{
    if(!sa || !ma || alpha_bits<1 || alpha_bits>8 ||
        (target && (target->input_bits<1 || target->input_bits>8 || !target->map ||
            target->map_bits<1 || target->map_bits>8 || target->map==da || target->colour))){errno=EINVAL;return -1;}
    uint32_t input[9];memcpy(input,sa,4*sizeof(*sa));memcpy(input+4,ma,4*sizeof(*ma));input[8]=da;
    uint32_t output[]={da,target?target->map:0};
    if(validate(w,f,input,9,output,target?2:1,p,width,height))return -1;
    for(unsigned i=0;i<8;++i)if(input[i]==da){errno=EINVAL;return -1;}
    if(target)for(unsigned i=0;i<9;++i)if(input[i]==target->map){errno=EINVAL;return -1;}
    uint32_t owned[8]={0};int result=-1,error=0;
    for(unsigned i=0;i<8;++i)if(ps2_surface_create(w->gs,64,32,32,owned+i))goto done;
    const unsigned bits[]={alpha_bits,alpha_bits,alpha_bits};
    unsigned in=target?target->input_bits:alpha_bits;
    const unsigned input_bits[]={in,in,in},map_bits[]={target?target->map_bits:8,
        target?target->map_bits:8,target?target->map_bits:8};
    if(ps2_native_float_multiply(w,f,sa,ma,owned,p,width,height) ||
        ps2_native_unorm_float(w->gs,da,owned+4,input_bits,p,width,height) ||
        ps2_native_float_add(w,f,owned,owned+4,owned,p,width,height,0) ||
        ps2_native_float_unorm(w,f,owned,da,bits,p,width,height) ||
        expand_unorm(w,da,bits,p,width,height) ||
        (target && (ps2_native_float_unorm(w,f,owned,target->map,map_bits,p,width,height) ||
            expand_unorm(w,target->map,map_bits,p,width,height))))goto done;
    result=0;
done:
    error=errno;
    for(unsigned i=0;i<8;++i)if(owned[i] && !w->gs->error && ps2_surface_destroy(w->gs,owned[i])){
        if(!result)error=errno;
        result=-1;
    }
    errno=error;return result;
}
int ps2_native_saturate_float(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t source[4],const uint32_t sa[4],const uint32_t mask[4],const uint32_t ma[4],
    uint32_t destination,uint32_t da,const struct ps2_unorm_layout *layout,
    uint32_t p,unsigned width,unsigned height)
{
    return ps2_native_saturate_float_mapped(w,f,source,sa,mask,ma,destination,da,layout,NULL,p,width,height);
}
int ps2_native_saturate_float_mapped(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t source[4],const uint32_t sa[4],const uint32_t mask[4],const uint32_t ma[4],
    uint32_t destination,uint32_t da,const struct ps2_unorm_layout *layout,
    const struct ps2_saturate_map_target *target,uint32_t p,unsigned width,unsigned height)
{
    if(!source || !sa || !mask || !ma || !layout ||
        (target && (target->input_bits<1 || target->input_bits>8 || !target->map ||
            target->map_bits<1 || target->map_bits>8 || target->map==destination || target->map==da ||
            (target->colour && (target->colour==target->map || target->colour==destination || target->colour==da))))){errno=EINVAL;return -1;}
    if(target && target->colour)for(unsigned c=0;c<3;++c)if(target->rgb_bits[c]<1 || target->rgb_bits[c]>8){errno=EINVAL;return -1;}
    struct ps2_unorm_layout precision=*layout;
    if(precision.alpha_bits>8){errno=EINVAL;return -1;}
    if(!precision.alpha_bits)precision.alpha_bits=8;
    for(unsigned c=0;c<3;++c){
        if(precision.rgb_bits[c]>8){errno=EINVAL;return -1;}
        if(!precision.rgb_bits[c])precision.rgb_bits[c]=8;
    }
    uint32_t input[18],out[]={destination,da,target?target->map:0,target?target->colour:0};
    const uint32_t *planes[]={source,sa,mask,ma};
    for(unsigned i=0;i<4;++i)for(unsigned byte=0;byte<4;++byte)input[i*4+byte]=planes[i][byte];
    input[16]=destination;input[17]=da;
    if(validate(w,f,input,18,out,target?(target->colour?4:3):2,p,width,height))return -1;
    for(unsigned i=0;i<16;++i)if(input[i]==destination || input[i]==da){errno=EINVAL;return -1;}
    if(target)for(unsigned i=0;i<18;++i)if(input[i]==target->map || (target->colour && input[i]==target->colour)){errno=EINVAL;return -1;}
    uint32_t owned[13]={0};int result=-1,error=0;
    for(unsigned i=0;i<13;++i)if(ps2_surface_create(w->gs,64,32,32,owned+i))goto done;
    uint32_t *factor=owned,*s=owned+4,*d=owned+8,zero=owned[12];
    const unsigned abits[]={precision.alpha_bits,precision.alpha_bits,precision.alpha_bits};
    const unsigned input_bits[]={target?target->input_bits:precision.alpha_bits,
        target?target->input_bits:precision.alpha_bits,target?target->input_bits:precision.alpha_bits};
    for(unsigned alpha=0;alpha<2;++alpha){
        const uint32_t *m=alpha?ma:mask,*src=alpha?sa:source;
        unsigned scalar=!memcmp(mask,ma,4*sizeof(*mask));
        if(!alpha || !scalar){
            if(copy_float(w,sa,s,width,height) || copy_float(w,m,d,width,height) ||
                ps2_native_float_multiply(w,f,s,d,s,p,width,height) ||
                ps2_native_unorm_float(w->gs,da,d,input_bits,p,width,height) ||
                saturate_factor_float(w,f,factor,s,d,zero,p,width,height))goto done;
        }
        if(copy_float(w,src,s,width,height) || copy_float(w,m,d,width,height) ||
            ps2_native_float_multiply(w,f,s,d,s,p,width,height) ||
            saturate_channel_float(w,f,alpha?da:destination,factor,s,d,
                alpha?abits:precision.rgb_bits,target,alpha,p,width,height))goto done;
    }
    result=0;
done:
    error=errno;
    for(unsigned i=0;i<13;++i)if(owned[i] && !w->gs->error && ps2_surface_destroy(w->gs,owned[i])){
        if(!result)error=errno;
        result=-1;
    }
    errno=error;return result;
}
enum pd_factor {
    PD_ZERO,PD_ONE,PD_DA_OVER_SA,PD_SA_OVER_DA,PD_INV_DA_OVER_SA,PD_INV_SA_OVER_DA,
    PD_ONE_MINUS_DA_OVER_SA,PD_ONE_MINUS_SA_OVER_DA,
    PD_ONE_MINUS_INV_DA_OVER_SA,PD_ONE_MINUS_INV_SA_OVER_DA
};
static const unsigned char pd_factors[][2]={
    {PD_ONE,PD_INV_SA_OVER_DA},
    {PD_ONE_MINUS_INV_DA_OVER_SA,PD_ZERO},{PD_ZERO,PD_ONE_MINUS_INV_SA_OVER_DA},
    {PD_INV_DA_OVER_SA,PD_ZERO},{PD_ZERO,PD_INV_SA_OVER_DA},
    {PD_ONE_MINUS_INV_DA_OVER_SA,PD_INV_SA_OVER_DA},
    {PD_INV_DA_OVER_SA,PD_ONE_MINUS_INV_SA_OVER_DA},{PD_INV_DA_OVER_SA,PD_INV_SA_OVER_DA},
    {PD_ONE,PD_ONE_MINUS_SA_OVER_DA},{PD_ONE_MINUS_DA_OVER_SA,PD_ONE},
    {PD_DA_OVER_SA,PD_ZERO},{PD_ZERO,PD_SA_OVER_DA},
    {PD_ONE_MINUS_DA_OVER_SA,PD_ZERO},{PD_ZERO,PD_ONE_MINUS_SA_OVER_DA},
    {PD_DA_OVER_SA,PD_ONE_MINUS_SA_OVER_DA},{PD_ONE_MINUS_DA_OVER_SA,PD_SA_OVER_DA},
    {PD_ONE_MINUS_DA_OVER_SA,PD_ONE_MINUS_SA_OVER_DA}
};
_Static_assert(sizeof(pd_factors)/sizeof(*pd_factors)==PS2_COMPOSITE_CONJOINT_XOR-PS2_COMPOSITE_DISJOINT_OVER+1,
    "one factor pair per disjoint/conjoint operation");
/* s and d are disposable masked source/destination alpha registers. Clamp
 * the nonnegative ratio before subtracting it from one: this is identical
 * to clamp(1-ratio), including the separately rounded subtraction, while
 * keeping negative values outside the unsigned GS float register domain. */
static int pd_factor_float(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    unsigned kind,const uint32_t factor[4],const uint32_t s[4],const uint32_t d[4],
    uint32_t zero,uint32_t p,unsigned width,unsigned height)
{
    if(kind==PD_ONE)return floating_one(w,factor,width,height);
    if(kind==PD_ZERO){
        for(unsigned byte=0;byte<4;++byte)if(clear_plane(w,factor[byte],width,height))return -1;
        return 0;
    }
    unsigned swap=kind==PD_SA_OVER_DA || kind==PD_INV_SA_OVER_DA ||
        kind==PD_ONE_MINUS_SA_OVER_DA || kind==PD_ONE_MINUS_INV_SA_OVER_DA;
    unsigned inverse=kind==PD_INV_DA_OVER_SA || kind==PD_INV_SA_OVER_DA ||
        kind==PD_ONE_MINUS_INV_DA_OVER_SA || kind==PD_ONE_MINUS_INV_SA_OVER_DA;
    unsigned complement=kind>=PD_ONE_MINUS_DA_OVER_SA;
    const uint32_t *numerator=swap?s:d,*denominator=swap?d:s;
    unsigned char table[256];
    if(inverse){
        if(floating_one(w,factor,width,height) ||
            ps2_native_float_add(w,f,factor,numerator,factor,p,width,height,1))return -1;
    }else if(copy_float(w,numerator,factor,width,height))return -1;
    /* A zero denominator selects ratio=1 (and complemented factor=0).
     * Detect all four bytes before replacing it with a finite one. */
    if(clear_plane(w,zero,width,height))return -1;
    for(unsigned byte=0;byte<4;++byte)
        if(transfer(w,denominator[byte],zero,width,height,PS2GS_TRANSFER_ADD_CLAMP))return -1;
    for(unsigned i=0;i<256;++i)table[i]=!i;
    if(lookup(w,zero,w->difference[0],p,table,width,height) ||
        transfer(w,w->difference[0],zero,width,height,PS2GS_TRANSFER_REPLACE) ||
        floating_one(w,numerator,width,height))return -1;
    for(unsigned byte=0;byte<4;++byte)
        if(select_plane(w,denominator[byte],numerator[byte],denominator[byte],zero,p,width,height))return -1;
    if(ps2_native_float_divide(w,f,factor,denominator,factor,p,width,height) ||
        exponent_plane(w,factor,w->difference[0],p,width,height))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i>=127;
    if(lookup(w,w->difference[0],w->borrow,p,table,width,height) ||
        transfer(w,zero,w->borrow,width,height,PS2GS_TRANSFER_ADD_CLAMP))return -1;
    for(unsigned byte=0;byte<4;++byte)
        if(select_plane(w,factor[byte],numerator[byte],factor[byte],w->borrow,p,width,height))return -1;
    return complement?ps2_native_float_add(w,f,numerator,factor,factor,p,width,height,1):0;
}
static int pd_prepare_factors(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const unsigned char kinds[2],const uint32_t sa[4],const uint32_t mask[4],uint32_t da,
    const unsigned input_bits[3],uint32_t owned[17],uint32_t p,unsigned width,unsigned height)
{
    for(unsigned factor=0;factor<2;++factor){
        if(kinds[factor]>PD_ONE && (copy_float(w,sa,owned+8,width,height) ||
            copy_float(w,mask,owned+12,width,height) ||
            ps2_native_float_multiply(w,f,owned+8,owned+12,owned+8,p,width,height) ||
            ps2_native_unorm_float(w->gs,da,owned+12,input_bits,p,width,height)))return -1;
        if(pd_factor_float(w,f,kinds[factor],owned+4*factor,owned+8,owned+12,owned[16],p,width,height))return -1;
    }
    return 0;
}
static int pd_channel_float(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t source[4],const uint32_t mask[4],uint32_t destination,const unsigned bits[3],
    const struct ps2_saturate_map_target *target,unsigned alpha,const unsigned char kinds[2],uint32_t owned[17],
    uint32_t p,unsigned width,unsigned height)
{
    const unsigned input_bits[]={target && alpha?target->input_bits:bits[0],
        target && alpha?target->input_bits:bits[1],target && alpha?target->input_bits:bits[2]};
    const unsigned secondary_bits[]={target?(alpha?target->map_bits:target->rgb_bits[0]):8,
        target?(alpha?target->map_bits:target->rgb_bits[1]):8,target?(alpha?target->map_bits:target->rgb_bits[2]):8};
    uint32_t secondary=target?(alpha?target->map:target->colour):0;
    /* Products round independently before the final addition, as in the
     * pinned float combiner. No sampled value is downloaded to the CPU. */
    for(unsigned factor=0;factor<2;++factor){
        uint32_t *value=owned+8+4*factor;
        if(kinds[factor]==PD_ZERO){
            for(unsigned byte=0;byte<4;++byte)if(clear_plane(w,value[byte],width,height))return -1;
        }else if((factor?ps2_native_unorm_float(w->gs,destination,value,input_bits,p,width,height):
            ps2_native_float_multiply(w,f,source,mask,value,p,width,height)) ||
            (kinds[factor]!=PD_ONE && ps2_native_float_multiply(w,f,value,owned+4*factor,value,p,width,height)))return -1;
    }
    return
        ps2_native_float_add(w,f,owned+8,owned+12,owned+8,p,width,height,0) ||
        ps2_native_float_unorm(w,f,owned+8,destination,bits,p,width,height) ||
        expand_unorm(w,destination,bits,p,width,height) ||
        (secondary && (ps2_native_float_unorm(w,f,owned+8,secondary,secondary_bits,p,width,height) ||
            expand_unorm(w,secondary,secondary_bits,p,width,height)))?-1:0;
}
int ps2_native_porter_duff_float_mapped(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t source[4],const uint32_t sa[4],const uint32_t mask[4],const uint32_t ma[4],
    uint32_t destination,uint32_t da,const struct ps2_unorm_layout *layout,
    const struct ps2_saturate_map_target *target,uint32_t p,unsigned width,unsigned height,unsigned operation)
{
    if(operation<PS2_COMPOSITE_DISJOINT_OVER || operation>PS2_COMPOSITE_CONJOINT_XOR ||
        !source || !sa || !mask || !ma || !layout ||
        (target && (target->input_bits<1 || target->input_bits>8 || !target->map ||
            target->map_bits<1 || target->map_bits>8 || target->map==destination || target->map==da ||
            (target->colour && (target->colour==target->map || target->colour==destination || target->colour==da))))){errno=EINVAL;return -1;}
    if(target && target->colour)for(unsigned c=0;c<3;++c)if(target->rgb_bits[c]<1 || target->rgb_bits[c]>8){errno=EINVAL;return -1;}
    struct ps2_unorm_layout precision=*layout;
    if(precision.alpha_bits>8){errno=EINVAL;return -1;}
    if(!precision.alpha_bits)precision.alpha_bits=8;
    for(unsigned c=0;c<3;++c){
        if(precision.rgb_bits[c]>8){errno=EINVAL;return -1;}
        if(!precision.rgb_bits[c])precision.rgb_bits[c]=8;
    }
    uint32_t input[18],out[]={destination,da,target?target->map:0,target?target->colour:0};
    const uint32_t *planes[]={source,sa,mask,ma};
    for(unsigned i=0;i<4;++i)for(unsigned byte=0;byte<4;++byte)input[i*4+byte]=planes[i][byte];
    input[16]=destination;input[17]=da;
    if(validate(w,f,input,18,out,target?(target->colour?4:3):2,p,width,height))return -1;
    for(unsigned i=0;i<16;++i)if(input[i]==destination || input[i]==da){errno=EINVAL;return -1;}
    if(target)for(unsigned i=0;i<18;++i)if(input[i]==target->map || (target->colour && input[i]==target->colour)){errno=EINVAL;return -1;}
    uint32_t owned[17]={0};int result=-1,error=0;
    for(unsigned i=0;i<17;++i)if(ps2_surface_create(w->gs,64,32,32,owned+i))goto done;
    const unsigned abits[]={precision.alpha_bits,precision.alpha_bits,precision.alpha_bits},
        input_bits[]={target?target->input_bits:precision.alpha_bits,
            target?target->input_bits:precision.alpha_bits,target?target->input_bits:precision.alpha_bits};
    const unsigned char *kinds=pd_factors[operation-PS2_COMPOSITE_DISJOINT_OVER];
    for(unsigned alpha=0;alpha<2;++alpha){
        const uint32_t *m=alpha?ma:mask,*src=alpha?sa:source;
        if((!alpha || memcmp(mask,ma,4*sizeof(*mask))) &&
            pd_prepare_factors(w,f,kinds,sa,m,da,input_bits,owned,p,width,height))goto done;
        if(pd_channel_float(w,f,src,m,alpha?da:destination,alpha?abits:precision.rgb_bits,target,alpha,kinds,
            owned,p,width,height))goto done;
    }
    result=0;
done:
    error=errno;
    for(unsigned i=0;i<17;++i)if(owned[i] && !w->gs->error && ps2_surface_destroy(w->gs,owned[i])){
        if(!result)error=errno;
        result=-1;
    }
    errno=error;return result;
}
int ps2_native_porter_duff_alpha_float_mapped(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t sa[4],const uint32_t ma[4],uint32_t da,unsigned alpha_bits,
    const struct ps2_saturate_map_target *target,uint32_t p,unsigned width,unsigned height,unsigned operation)
{
    if(operation<PS2_COMPOSITE_DISJOINT_OVER || operation>PS2_COMPOSITE_CONJOINT_XOR ||
        !sa || !ma || alpha_bits<1 || alpha_bits>8 ||
        (target && (target->input_bits<1 || target->input_bits>8 || !target->map ||
            target->map_bits<1 || target->map_bits>8 || target->map==da || target->colour))){errno=EINVAL;return -1;}
    uint32_t input[9];memcpy(input,sa,4*sizeof(*sa));memcpy(input+4,ma,4*sizeof(*ma));input[8]=da;
    uint32_t output[]={da,target?target->map:0};
    if(validate(w,f,input,9,output,target?2:1,p,width,height))return -1;
    for(unsigned i=0;i<8;++i)if(input[i]==da){errno=EINVAL;return -1;}
    if(target)for(unsigned i=0;i<9;++i)if(input[i]==target->map){errno=EINVAL;return -1;}
    uint32_t owned[17]={0};int result=-1,error=0;
    for(unsigned i=0;i<17;++i)if(ps2_surface_create(w->gs,64,32,32,owned+i))goto done;
    const unsigned bits[]={alpha_bits,alpha_bits,alpha_bits},input_bits[]={target?target->input_bits:alpha_bits,
        target?target->input_bits:alpha_bits,target?target->input_bits:alpha_bits};
    if(pd_prepare_factors(w,f,pd_factors[operation-PS2_COMPOSITE_DISJOINT_OVER],sa,ma,da,input_bits,
        owned,p,width,height) || pd_channel_float(w,f,sa,ma,da,bits,target,1,
            pd_factors[operation-PS2_COMPOSITE_DISJOINT_OVER],owned,p,width,height))goto done;
    result=0;
done:
    error=errno;
    for(unsigned i=0;i<17;++i)if(owned[i] && !w->gs->error && ps2_surface_destroy(w->gs,owned[i])){
        if(!result)error=errno;
        result=-1;
    }
    errno=error;return result;
}
static int floating_zero(struct ps2_divide_workspace *w,const uint32_t out[4],unsigned width,unsigned height)
{
    for(unsigned byte=0;byte<4;++byte)if(clear_plane(w,out[byte],width,height))return -1;
    return 0;
}
static int floating_constant(struct ps2_divide_workspace *w,const uint32_t out[4],
    uint32_t bits,unsigned width,unsigned height)
{
    for(unsigned byte=0;byte<4;++byte)
        if(ps2_surface_solid_logic(w->gs,out[byte],0,0,width,height,((bits>>(8*byte))&255)*0x010101,0xffffff,3))return -1;
    return 0;
}
static int hsl_signed(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t a[4],const uint32_t b[4],const uint32_t out[4],uint32_t p,
    unsigned width,unsigned height,unsigned operation)
{return float_signed_core(w,f,a,b,out,p,width,height,operation,0,1);}
static int hsl_signed_tiny(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t a[4],const uint32_t b[4],const uint32_t out[4],uint32_t p,
    unsigned width,unsigned height,unsigned operation)
{return float_signed_core(w,f,a,b,out,p,width,height,operation,1,1);}
static int hsl_lum(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t colour[4],const uint32_t out[4],const uint32_t x[4],const uint32_t y[4],
    uint32_t p,unsigned width,unsigned height)
{
    const uint32_t weights[]={0x3e99999a,0x3f170a3d,0x3de147ae};
    const unsigned red[]={0,0,0},green[]={1,1,1},blue[]={2,2,2};
    for(unsigned byte=0;byte<4;++byte){uint32_t rgb=0;
        for(unsigned c=0;c<3;++c)rgb|=((weights[c]>>(8*byte))&255)<<(8*c);
        if(ps2_surface_solid_logic(w->gs,x[byte],0,0,width,height,rgb,0xffffff,3))return -1;
    }
    return hsl_signed(w,f,colour,x,out,p,width,height,PS2_FLOAT_MULTIPLY) ||
        ps2_native_float_swizzle(w,f,out,red,x,p,width,height) ||
        ps2_native_float_swizzle(w,f,out,green,y,p,width,height) ||
        hsl_signed(w,f,x,y,x,p,width,height,PS2_FLOAT_ADD) ||
        ps2_native_float_swizzle(w,f,out,blue,y,p,width,height) ||
        hsl_signed(w,f,x,y,out,p,width,height,PS2_FLOAT_ADD)?-1:0;
}
static int hsl_extreme(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t colour[4],const uint32_t out[4],const uint32_t temporary[4],
    uint32_t predicate,uint32_t index,uint32_t p,unsigned width,unsigned height,unsigned maximum)
{
    const unsigned red[]={0,0,0};
    if(ps2_native_float_swizzle(w,f,colour,red,out,p,width,height) ||
        (index && clear_plane(w,index,width,height)))return -1;
    for(unsigned c=1;c<3;++c){const unsigned channel[]={c,c,c};
        if(ps2_native_float_swizzle(w,f,colour,channel,temporary,p,width,height) ||
            ps2_native_float_signed_compare(w,f,out,temporary,predicate,p,width,height,maximum?PS2_FLOAT_GT:PS2_FLOAT_LE) ||
            ps2_native_float_select(w,f,out,temporary,predicate,out,p,width,height))return -1;
        if(index && (ps2_surface_solid_logic(w->gs,w->temporary,0,0,width,height,c*0x010101,0xffffff,3) ||
            select_plane(w,index,index,w->temporary,predicate,p,width,height)))return -1;
    }
    return 0;
}
static int hsl_set_sat(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t colour[4],const uint32_t sat[4],const uint32_t minimum[4],const uint32_t x[4],const uint32_t t[4],
    const uint32_t flags[3],uint32_t p,unsigned width,unsigned height)
{
    uint32_t zero=flags[0],maximum=flags[1],index=flags[2];unsigned char table[256];
    if(hsl_extreme(w,f,colour,minimum,x,zero,0,p,width,height,0) ||
        hsl_extreme(w,f,colour,t,x,zero,index,p,width,height,1) ||
        ps2_native_float_add(w,f,t,minimum,t,p,width,height,1) ||
        exponent_plane(w,t,w->borrow,p,width,height))return -1;
    for(unsigned i=0;i<256;++i)table[i]=!i;
    if(lookup(w,w->borrow,zero,p,table,width,height) || floating_one(w,x,width,height) ||
        ps2_native_float_select(w,f,x,t,zero,t,p,width,height) ||
        ps2_native_float_add(w,f,colour,minimum,x,p,width,height,1) ||
        float_multiply_core(w,f,x,sat,x,p,width,height,1) ||
        float_divide_core(w,f,x,t,x,p,width,height,1))return -1;
    /* The pinned ordering selects the last maximum and first minimum.
     * Assign maximum=sat directly; a tied middle still keeps its separately
     * rounded product/division. Replacing every maximum by sat would differ. */
    for(unsigned c=0;c<3;++c){
        for(unsigned i=0;i<256;++i)table[i]=i==c;
        if(ps2_surface_lut_palette(w->gs,p,table) ||
            ps2_surface_channel_lut(w->gs,index,maximum,p,width,height,255U<<(8*c)))return -1;
    }
    return ps2_native_float_select(w,f,sat,x,maximum,colour,p,width,height) ||
        floating_zero(w,x,width,height) || ps2_native_float_select(w,f,x,colour,zero,colour,p,width,height)?-1:0;
}
static int hsl_clip(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t colour[4],const uint32_t lum[4],const uint32_t alpha[4],
    const uint32_t n[4],const uint32_t maximum[4],const uint32_t x[4],const uint32_t y[4],
    const uint32_t flags[3],uint32_t p,unsigned width,unsigned height)
{
    uint32_t low=flags[0],high=flags[1],zero=flags[2];unsigned char table[256];
    if(hsl_lum(w,f,colour,lum,n,maximum,p,width,height) ||
        hsl_extreme(w,f,colour,n,x,low,0,p,width,height,0) ||
        hsl_extreme(w,f,colour,maximum,x,low,0,p,width,height,1))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i>>7;
    if(lookup(w,n[3],low,p,table,width,height) ||
        ps2_native_float_signed_compare(w,f,maximum,alpha,high,p,width,height,PS2_FLOAT_GT) ||
        hsl_signed(w,f,lum,n,n,p,width,height,PS2_FLOAT_SUBTRACT) ||
        exponent_plane(w,n,w->borrow,p,width,height))return -1;
    for(unsigned i=0;i<256;++i)table[i]=!i;
    if(lookup(w,w->borrow,zero,p,table,width,height) || floating_one(w,x,width,height) ||
        ps2_native_float_select(w,f,x,n,zero,n,p,width,height) ||
        hsl_signed(w,f,colour,lum,x,p,width,height,PS2_FLOAT_SUBTRACT) ||
        hsl_signed_tiny(w,f,x,lum,x,p,width,height,PS2_FLOAT_MULTIPLY) ||
        hsl_signed_tiny(w,f,x,n,x,p,width,height,PS2_FLOAT_DIVIDE) ||
        hsl_signed(w,f,lum,x,x,p,width,height,PS2_FLOAT_ADD) ||
        floating_zero(w,y,width,height) || ps2_native_float_select(w,f,y,x,zero,x,p,width,height) ||
        ps2_native_float_select(w,f,x,colour,low,colour,p,width,height) ||
        /* The high branch intentionally reuses the original maximum and
         * luminance, while operating on the low branch's updated colour. */
        hsl_signed(w,f,maximum,lum,n,p,width,height,PS2_FLOAT_SUBTRACT) ||
        exponent_plane(w,n,w->borrow,p,width,height))return -1;
    for(unsigned i=0;i<256;++i)table[i]=!i;
    return lookup(w,w->borrow,zero,p,table,width,height) || floating_one(w,x,width,height) ||
        ps2_native_float_select(w,f,x,n,zero,n,p,width,height) ||
        hsl_signed(w,f,colour,lum,x,p,width,height,PS2_FLOAT_SUBTRACT) ||
        hsl_signed(w,f,alpha,lum,y,p,width,height,PS2_FLOAT_SUBTRACT) ||
        hsl_signed_tiny(w,f,x,y,x,p,width,height,PS2_FLOAT_MULTIPLY) ||
        hsl_signed_tiny(w,f,x,n,x,p,width,height,PS2_FLOAT_DIVIDE) ||
        hsl_signed(w,f,lum,x,x,p,width,height,PS2_FLOAT_ADD) ||
        ps2_native_float_select(w,f,alpha,x,zero,x,p,width,height) ||
        ps2_native_float_select(w,f,x,colour,high,colour,p,width,height)?-1:0;
}
static int hsl_rgb(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    uint32_t r[39],uint32_t p,unsigned width,unsigned height,unsigned mode)
{
    uint32_t *s=r,*sa=r+4,*d=r+8,*da=r+12,*result=r+16,*lum=r+20,*alpha=r+24,*x=r+28,*y=r+32,*flags=r+36;
    unsigned source_colour=mode==0 || mode==2;uint32_t *colour=source_colour?s:d,*other=source_colour?d:s;
    if(floating_one(w,x,width,height) || ps2_native_float_add(w,f,x,sa,x,p,width,height,1) ||
        ps2_native_float_multiply(w,f,x,d,x,p,width,height) ||
        floating_one(w,y,width,height) || ps2_native_float_add(w,f,y,da,y,p,width,height,1) ||
        ps2_native_float_multiply(w,f,y,s,y,p,width,height) ||
        ps2_native_float_add(w,f,x,y,result,p,width,height,0) ||
        ps2_native_float_multiply(w,f,sa,da,alpha,p,width,height) ||
        hsl_lum(w,f,mode==3?s:d,lum,x,y,p,width,height) ||
        ps2_native_float_multiply(w,f,lum,mode==3?da:sa,lum,p,width,height) ||
        ps2_native_float_multiply(w,f,colour,source_colour?da:sa,colour,p,width,height))return -1;
    if(mode<2){
        if(hsl_extreme(w,f,other,x,y,flags[0],0,p,width,height,0) ||
            hsl_extreme(w,f,other,y,source_colour?da:sa,flags[0],0,p,width,height,1) ||
            ps2_native_float_add(w,f,y,x,x,p,width,height,1) ||
            ps2_native_float_multiply(w,f,x,source_colour?sa:da,x,p,width,height) ||
            hsl_set_sat(w,f,colour,x,other,sa,da,flags,p,width,height))return -1;
    }
    /* Both original alpha registers are now free scratch space. */
    if(hsl_lum(w,f,colour,sa,da,x,p,width,height) ||
        hsl_signed(w,f,lum,sa,lum,p,width,height,PS2_FLOAT_SUBTRACT) ||
        hsl_signed(w,f,colour,lum,colour,p,width,height,PS2_FLOAT_ADD) ||
        hsl_clip(w,f,colour,lum,alpha,sa,da,x,y,flags,p,width,height) ||
        hsl_signed(w,f,result,colour,result,p,width,height,PS2_FLOAT_ADD))return -1;
    unsigned char table[256];for(unsigned i=0;i<256;++i)table[i]=!(i&128);
    if(lookup(w,result[3],w->borrow,p,table,width,height))return -1;
    for(unsigned byte=0;byte<4;++byte)if(mask_zero(w,result[byte],w->borrow,p,width,height))return -1;
    return 0;
}
static int softlight_rgb(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    uint32_t r[31],uint32_t p,unsigned width,unsigned height)
{
    uint32_t *s=r,*sa=r+4,*d=r+8,*da=r+12,*result=r+16,*x=r+20,*y=r+24;
    uint32_t low=r[28],polynomial=r[29],radical=r[30];unsigned char table[256];
    /* Save delta=2*s-sa before replacing s by the independently rounded
     * PDF cross terms and sa by delta. result then holds the base d*sa. */
    if(ps2_native_float_add(w,f,s,s,result,p,width,height,0) ||
        ps2_native_float_compare(w,f,result,sa,low,p,width,height,PS2_FLOAT_LE) ||
        ps2_native_float_signed(w,f,result,sa,result,p,width,height,PS2_FLOAT_SUBTRACT) ||
        floating_one(w,x,width,height) || ps2_native_float_add(w,f,x,sa,x,p,width,height,1) ||
        ps2_native_float_multiply(w,f,x,d,x,p,width,height) ||
        floating_one(w,y,width,height) || ps2_native_float_add(w,f,y,da,y,p,width,height,1) ||
        ps2_native_float_multiply(w,f,y,s,y,p,width,height) ||
        ps2_native_float_add(w,f,x,y,s,p,width,height,0) ||
        ps2_native_float_multiply(w,f,d,sa,y,p,width,height) ||
        copy_float(w,result,sa,width,height) || copy_float(w,y,result,width,height) ||
        ps2_native_float_add(w,f,d,d,x,p,width,height,0) ||
        ps2_native_float_add(w,f,x,x,x,p,width,height,0) ||
        ps2_native_float_compare(w,f,x,da,polynomial,p,width,height,PS2_FLOAT_LE) ||
        floating_zero(w,x,width,height) ||
        ps2_native_float_compare(w,f,da,x,radical,p,width,height,PS2_FLOAT_LE) ||
        floating_one(w,x,width,height) ||
        ps2_native_float_select(w,f,x,da,radical,da,p,width,height) ||
        transfer(w,low,polynomial,width,height,PS2GS_TRANSFER_SUBTRACT) ||
        transfer(w,radical,polynomial,width,height,PS2GS_TRANSFER_SUBTRACT) ||
        transfer(w,radical,low,width,height,PS2GS_TRANSFER_SUBTRACT) ||
        transfer(w,low,radical,width,height,PS2GS_TRANSFER_ADD) ||
        transfer(w,polynomial,radical,width,height,PS2GS_TRANSFER_ADD))return -1;
    for(unsigned i=0;i<256;++i)table[i]=!i;
    if(lookup(w,radical,w->temporary,p,table,width,height) ||
        transfer(w,w->temporary,radical,width,height,PS2GS_TRANSFER_REPLACE))return -1;
    /* Low branch: d*sa - d*(da-d)*(sa-2*s)/da. Negating delta preserves
     * exactly the rounding of sa-2*s, including raw nonpremultiplied RGB. */
    if(ps2_native_float_signed(w,f,da,d,x,p,width,height,PS2_FLOAT_SUBTRACT) ||
        ps2_native_float_signed(w,f,d,x,x,p,width,height,PS2_FLOAT_MULTIPLY))return -1;
    for(unsigned byte=0;byte<4;++byte){
        for(unsigned i=0;i<256;++i)table[i]=byte==3?i^128:i;
        if(lookup(w,sa[byte],y[byte],p,table,width,height))return -1;
    }
    if(ps2_native_float_signed(w,f,x,y,x,p,width,height,PS2_FLOAT_MULTIPLY) ||
        ps2_native_float_signed(w,f,x,da,x,p,width,height,PS2_FLOAT_DIVIDE) ||
        floating_zero(w,y,width,height) || ps2_native_float_select(w,f,x,y,low,x,p,width,height) ||
        ps2_native_float_signed(w,f,result,x,result,p,width,height,PS2_FLOAT_SUBTRACT) ||
        /* High polynomial: (2*s-sa)*d*((16*d/da-12)*d/da+3). */
        floating_constant(w,x,0x41800000,width,height) ||
        ps2_native_float_multiply(w,f,x,d,x,p,width,height) ||
        ps2_native_float_divide(w,f,x,da,x,p,width,height) ||
        floating_constant(w,y,0x41400000,width,height) ||
        ps2_native_float_signed(w,f,x,y,x,p,width,height,PS2_FLOAT_SUBTRACT) ||
        ps2_native_float_signed(w,f,x,d,x,p,width,height,PS2_FLOAT_MULTIPLY) ||
        ps2_native_float_signed(w,f,x,da,x,p,width,height,PS2_FLOAT_DIVIDE) ||
        floating_constant(w,y,0x40400000,width,height) ||
        ps2_native_float_signed(w,f,x,y,x,p,width,height,PS2_FLOAT_ADD) ||
        ps2_native_float_signed(w,f,sa,d,y,p,width,height,PS2_FLOAT_MULTIPLY) ||
        ps2_native_float_signed(w,f,y,x,x,p,width,height,PS2_FLOAT_MULTIPLY) ||
        floating_zero(w,y,width,height) || ps2_native_float_select(w,f,x,y,polynomial,x,p,width,height) ||
        ps2_native_float_signed(w,f,result,x,result,p,width,height,PS2_FLOAT_ADD) ||
        /* High radical: (sqrt(d*da)-d)*(2*s-sa). */
        ps2_native_float_multiply(w,f,d,da,x,p,width,height) ||
        ps2_native_float_sqrt(w,f,x,x,p,width,height) ||
        ps2_native_float_signed(w,f,x,d,x,p,width,height,PS2_FLOAT_SUBTRACT) ||
        ps2_native_float_signed(w,f,x,sa,x,p,width,height,PS2_FLOAT_MULTIPLY) ||
        floating_zero(w,y,width,height) || ps2_native_float_select(w,f,x,y,radical,x,p,width,height) ||
        ps2_native_float_signed(w,f,result,x,result,p,width,height,PS2_FLOAT_ADD) ||
        ps2_native_float_signed(w,f,s,result,result,p,width,height,PS2_FLOAT_ADD))return -1;
    /* pixman's UNORM store clamps negative results before quantization. */
    for(unsigned i=0;i<256;++i)table[i]=!(i&128);
    if(lookup(w,result[3],w->borrow,p,table,width,height))return -1;
    for(unsigned byte=0;byte<4;++byte)if(mask_zero(w,result[byte],w->borrow,p,width,height))return -1;
    return 0;
}
static int dodge_burn_rgb(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    uint32_t owned[31],uint32_t p,unsigned width,unsigned height,unsigned mode)
{
    uint32_t *s=owned,*sa=owned+4,*d=owned+8,*da=owned+12,
        *x=owned+16,*y=owned+20,*z=owned+24,b0=owned[28],b1=owned[29],b2=owned[30];
    if(!mode){
        /* Dodge's threshold subtracts two separately rounded products. A
         * negative threshold always takes the saturated branch, so clamping
         * only this comparison operand to zero preserves that decision. */
        if(ps2_native_float_multiply(w,f,sa,da,x,p,width,height) ||
            ps2_native_float_multiply(w,f,s,da,y,p,width,height) ||
            ps2_native_float_compare(w,f,x,y,b2,p,width,height,PS2_FLOAT_GE) ||
            ps2_native_float_select(w,f,x,y,b2,z,p,width,height) ||
            ps2_native_float_add(w,f,z,y,y,p,width,height,1) ||
            ps2_native_float_multiply(w,f,d,sa,z,p,width,height) ||
            ps2_native_float_compare(w,f,z,y,b1,p,width,height,PS2_FLOAT_GE) ||
            floating_zero(w,x,width,height) ||
            ps2_native_float_compare(w,f,d,x,b0,p,width,height,PS2_FLOAT_LE) ||
            ps2_native_float_compare(w,f,sa,s,b2,p,width,height,PS2_FLOAT_GE) ||
            ps2_native_float_select(w,f,sa,s,b2,y,p,width,height) ||
            ps2_native_float_add(w,f,y,s,y,p,width,height,1) ||
            ps2_native_float_compare(w,f,y,x,b2,p,width,height,PS2_FLOAT_LE) ||
            transfer(w,b2,b1,width,height,PS2GS_TRANSFER_ADD_CLAMP) ||
            floating_one(w,x,width,height) ||
            ps2_native_float_select(w,f,x,y,b2,y,p,width,height) ||
            ps2_native_float_multiply(w,f,sa,sa,z,p,width,height) ||
            ps2_native_float_multiply(w,f,z,d,z,p,width,height) ||
            ps2_native_float_divide(w,f,z,y,z,p,width,height) ||
            ps2_native_float_multiply(w,f,sa,da,y,p,width,height) ||
            ps2_native_float_select(w,f,y,z,b1,z,p,width,height) ||
            floating_zero(w,x,width,height) ||
            ps2_native_float_select(w,f,x,z,b0,z,p,width,height))return -1;
    }else{
        /* Burn tests d>=da before its zero branch. In inactive lanes the
         * quotient is replaced by zero before subtracting it from da; the
         * active strict product comparison guarantees quotient<=da. */
        if(ps2_native_float_compare(w,f,d,da,b0,p,width,height,PS2_FLOAT_GE) ||
            ps2_native_float_compare(w,f,da,d,b2,p,width,height,PS2_FLOAT_GE) ||
            ps2_native_float_select(w,f,da,d,b2,x,p,width,height) ||
            ps2_native_float_add(w,f,x,d,x,p,width,height,1) ||
            ps2_native_float_multiply(w,f,sa,x,y,p,width,height) ||
            ps2_native_float_multiply(w,f,s,da,z,p,width,height) ||
            ps2_native_float_compare(w,f,y,z,b1,p,width,height,PS2_FLOAT_GE) ||
            floating_zero(w,z,width,height) ||
            ps2_native_float_compare(w,f,s,z,b2,p,width,height,PS2_FLOAT_LE) ||
            transfer(w,b2,b1,width,height,PS2GS_TRANSFER_ADD_CLAMP) ||
            floating_one(w,z,width,height) ||
            ps2_native_float_select(w,f,z,s,b2,x,p,width,height) ||
            ps2_native_float_divide(w,f,y,x,y,p,width,height) ||
            floating_zero(w,z,width,height) ||
            ps2_native_float_select(w,f,z,y,b1,y,p,width,height) ||
            ps2_native_float_add(w,f,da,y,y,p,width,height,1) ||
            ps2_native_float_multiply(w,f,sa,y,y,p,width,height) ||
            floating_zero(w,x,width,height) ||
            ps2_native_float_select(w,f,x,y,b1,y,p,width,height) ||
            ps2_native_float_multiply(w,f,sa,da,x,p,width,height) ||
            ps2_native_float_select(w,f,x,y,b0,z,p,width,height))return -1;
    }
    /* PDF combines the two cross terms before adding the blend term. Keep
     * that binary32 evaluation order; quantization clamps only afterwards. */
    return floating_one(w,x,width,height) ||
        ps2_native_float_add(w,f,x,sa,x,p,width,height,1) ||
        ps2_native_float_multiply(w,f,x,d,x,p,width,height) ||
        floating_one(w,y,width,height) ||
        ps2_native_float_add(w,f,y,da,y,p,width,height,1) ||
        ps2_native_float_multiply(w,f,y,s,y,p,width,height) ||
        ps2_native_float_add(w,f,x,y,x,p,width,height,0) ||
        ps2_native_float_add(w,f,x,z,x,p,width,height,0)?-1:0;
}
static int dodge_burn_store(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t value[4],uint32_t destination,const unsigned bits[3],
    const struct ps2_saturate_map_target *target,unsigned alpha,uint32_t p,unsigned width,unsigned height)
{
    uint32_t secondary=target?(alpha?target->map:target->colour):0;
    const unsigned extra_bits[]={target?(alpha?target->map_bits:target->rgb_bits[0]):8,
        target?(alpha?target->map_bits:target->rgb_bits[1]):8,target?(alpha?target->map_bits:target->rgb_bits[2]):8};
    return ps2_native_float_unorm(w,f,value,destination,bits,p,width,height) ||
        expand_unorm(w,destination,bits,p,width,height) ||
        (secondary && (ps2_native_float_unorm(w,f,value,secondary,extra_bits,p,width,height) ||
            expand_unorm(w,secondary,extra_bits,p,width,height)))?-1:0;
}
static int dodge_burn_alpha(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t sa[4],const uint32_t ma[4],uint32_t da,unsigned alpha_bits,
    const struct ps2_saturate_map_target *target,uint32_t owned[12],uint32_t p,unsigned width,unsigned height)
{
    unsigned in=target?target->input_bits:alpha_bits;
    const unsigned input_bits[]={in,in,in},bits[]={alpha_bits,alpha_bits,alpha_bits};
    return ps2_native_float_multiply(w,f,sa,ma,owned,p,width,height) ||
        ps2_native_unorm_float(w->gs,da,owned+4,input_bits,p,width,height) ||
        ps2_native_float_multiply(w,f,owned,owned+4,owned+8,p,width,height) ||
        ps2_native_float_add(w,f,owned+4,owned,owned,p,width,height,0) ||
        ps2_native_float_add(w,f,owned,owned+8,owned,p,width,height,1) ||
        dodge_burn_store(w,f,owned,da,bits,target,1,p,width,height)?-1:0;
}
int ps2_native_dodge_burn_float_mapped(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t source[4],const uint32_t sa[4],const uint32_t mask[4],const uint32_t ma[4],
    uint32_t destination,uint32_t da,const struct ps2_unorm_layout *layout,
    const struct ps2_saturate_map_target *target,uint32_t p,unsigned width,unsigned height,unsigned mode)
{
    if(mode>1 || !source || !sa || !mask || !ma || !layout ||
        (target && (target->input_bits<1 || target->input_bits>8 || !target->map ||
            target->map_bits<1 || target->map_bits>8 || target->map==destination || target->map==da ||
            (target->colour && (target->colour==target->map || target->colour==destination || target->colour==da))))){errno=EINVAL;return -1;}
    if(target && target->colour)for(unsigned c=0;c<3;++c)if(target->rgb_bits[c]<1 || target->rgb_bits[c]>8){errno=EINVAL;return -1;}
    struct ps2_unorm_layout precision=*layout;
    if(precision.alpha_bits>8){errno=EINVAL;return -1;}
    if(!precision.alpha_bits)precision.alpha_bits=8;
    for(unsigned c=0;c<3;++c){
        if(precision.rgb_bits[c]>8){errno=EINVAL;return -1;}
        if(!precision.rgb_bits[c])precision.rgb_bits[c]=8;
    }
    uint32_t input[18],out[]={destination,da,target?target->map:0,target?target->colour:0};
    const uint32_t *planes[]={source,sa,mask,ma};
    for(unsigned i=0;i<4;++i)for(unsigned byte=0;byte<4;++byte)input[i*4+byte]=planes[i][byte];
    input[16]=destination;input[17]=da;
    if(validate(w,f,input,18,out,target?(target->colour?4:3):2,p,width,height))return -1;
    for(unsigned i=0;i<16;++i)if(input[i]==destination || input[i]==da){errno=EINVAL;return -1;}
    if(target)for(unsigned i=0;i<18;++i)if(input[i]==target->map || (target->colour && input[i]==target->colour)){errno=EINVAL;return -1;}
    uint32_t owned[31]={0};int result=-1,error=0;
    for(unsigned i=0;i<31;++i)if(ps2_surface_create(w->gs,64,32,32,owned+i))goto done;
    unsigned in=target?target->input_bits:precision.alpha_bits;const unsigned input_bits[]={in,in,in};
    if(ps2_native_float_multiply(w,f,source,mask,owned,p,width,height) ||
        ps2_native_float_multiply(w,f,sa,mask,owned+4,p,width,height) ||
        ps2_native_unorm_float(w->gs,destination,owned+8,precision.rgb_bits,p,width,height) ||
        ps2_native_unorm_float(w->gs,da,owned+12,input_bits,p,width,height) ||
        dodge_burn_rgb(w,f,owned,p,width,height,mode) ||
        dodge_burn_store(w,f,owned+16,destination,precision.rgb_bits,target,0,p,width,height) ||
        dodge_burn_alpha(w,f,sa,ma,da,precision.alpha_bits,target,owned,p,width,height))goto done;
    result=0;
done:
    error=errno;
    for(unsigned i=0;i<31;++i)if(owned[i] && !w->gs->error && ps2_surface_destroy(w->gs,owned[i])){
        if(!result)error=errno;
        result=-1;
    }
    errno=error;return result;
}
int ps2_native_dodge_burn_alpha_float_mapped(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t sa[4],const uint32_t ma[4],uint32_t da,unsigned alpha_bits,
    const struct ps2_saturate_map_target *target,uint32_t p,unsigned width,unsigned height,unsigned mode)
{
    if(mode>1 || !sa || !ma || alpha_bits<1 || alpha_bits>8 ||
        (target && (target->input_bits<1 || target->input_bits>8 || !target->map ||
            target->map_bits<1 || target->map_bits>8 || target->map==da || target->colour))){errno=EINVAL;return -1;}
    uint32_t input[9];memcpy(input,sa,4*sizeof(*sa));memcpy(input+4,ma,4*sizeof(*ma));input[8]=da;
    uint32_t output[]={da,target?target->map:0};
    if(validate(w,f,input,9,output,target?2:1,p,width,height))return -1;
    for(unsigned i=0;i<8;++i)if(input[i]==da){errno=EINVAL;return -1;}
    if(target)for(unsigned i=0;i<9;++i)if(input[i]==target->map){errno=EINVAL;return -1;}
    uint32_t owned[12]={0};int result=-1,error=0;
    for(unsigned i=0;i<12;++i)if(ps2_surface_create(w->gs,64,32,32,owned+i))goto done;
    if(dodge_burn_alpha(w,f,sa,ma,da,alpha_bits,target,owned,p,width,height))goto done;
    result=0;
done:
    error=errno;
    for(unsigned i=0;i<12;++i)if(owned[i] && !w->gs->error && ps2_surface_destroy(w->gs,owned[i])){
        if(!result)error=errno;
        result=-1;
    }
    errno=error;return result;
}
int ps2_native_softlight_float_mapped(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t source[4],const uint32_t sa[4],const uint32_t mask[4],const uint32_t ma[4],
    uint32_t destination,uint32_t da,const struct ps2_unorm_layout *layout,
    const struct ps2_saturate_map_target *target,uint32_t p,unsigned width,unsigned height)
{
    if(!source || !sa || !mask || !ma || !layout ||
        (target && (target->input_bits<1 || target->input_bits>8 || !target->map ||
            target->map_bits<1 || target->map_bits>8 || target->map==destination || target->map==da ||
            (target->colour && (target->colour==target->map || target->colour==destination || target->colour==da))))){errno=EINVAL;return -1;}
    if(target && target->colour)for(unsigned c=0;c<3;++c)if(target->rgb_bits[c]<1 || target->rgb_bits[c]>8){errno=EINVAL;return -1;}
    struct ps2_unorm_layout precision=*layout;
    if(precision.alpha_bits>8){errno=EINVAL;return -1;}
    if(!precision.alpha_bits)precision.alpha_bits=8;
    for(unsigned c=0;c<3;++c){
        if(precision.rgb_bits[c]>8){errno=EINVAL;return -1;}
        if(!precision.rgb_bits[c])precision.rgb_bits[c]=8;
    }
    uint32_t input[18],out[]={destination,da,target?target->map:0,target?target->colour:0};
    const uint32_t *planes[]={source,sa,mask,ma};
    for(unsigned i=0;i<4;++i)for(unsigned byte=0;byte<4;++byte)input[i*4+byte]=planes[i][byte];
    input[16]=destination;input[17]=da;
    if(validate(w,f,input,18,out,target?(target->colour?4:3):2,p,width,height))return -1;
    for(unsigned i=0;i<16;++i)if(input[i]==destination || input[i]==da){errno=EINVAL;return -1;}
    if(target)for(unsigned i=0;i<18;++i)if(input[i]==target->map || (target->colour && input[i]==target->colour)){errno=EINVAL;return -1;}
    uint32_t owned[31]={0};int result=-1,error=0;
    for(unsigned i=0;i<31;++i)if(ps2_surface_create(w->gs,64,32,32,owned+i))goto done;
    unsigned in=target?target->input_bits:precision.alpha_bits;const unsigned input_bits[]={in,in,in};
    if(ps2_native_float_multiply(w,f,source,mask,owned,p,width,height) ||
        ps2_native_float_multiply(w,f,sa,mask,owned+4,p,width,height) ||
        ps2_native_unorm_float(w->gs,destination,owned+8,precision.rgb_bits,p,width,height) ||
        ps2_native_unorm_float(w->gs,da,owned+12,input_bits,p,width,height) ||
        softlight_rgb(w,f,owned,p,width,height) ||
        dodge_burn_store(w,f,owned+16,destination,precision.rgb_bits,target,0,p,width,height) ||
        dodge_burn_alpha(w,f,sa,ma,da,precision.alpha_bits,target,owned,p,width,height))goto done;
    result=0;
done:
    error=errno;
    for(unsigned i=0;i<31;++i)if(owned[i] && !w->gs->error && ps2_surface_destroy(w->gs,owned[i])){
        if(!result)error=errno;
        result=-1;
    }
    errno=error;return result;
}

int ps2_native_softlight_alpha_float_mapped(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t sa[4],const uint32_t ma[4],uint32_t da,unsigned alpha_bits,
    const struct ps2_saturate_map_target *target,uint32_t p,unsigned width,unsigned height)
{
    return ps2_native_dodge_burn_alpha_float_mapped(w,f,sa,ma,da,alpha_bits,target,p,width,height,0);
}
static int hsl_float_mapped(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t source[4],const uint32_t sa[4],const uint32_t mask[4],const uint32_t ma[4],
    uint32_t destination,uint32_t da,const struct ps2_unorm_layout *layout,
    const struct ps2_saturate_map_target *target,uint32_t p,unsigned width,unsigned height,unsigned mode,unsigned consume)
{
    if(mode>3 || !source || !sa || !mask || !ma || !layout ||
        (target && (target->input_bits<1 || target->input_bits>8 || !target->map ||
            target->map_bits<1 || target->map_bits>8 || target->map==destination || target->map==da ||
            (target->colour && (target->colour==target->map || target->colour==destination || target->colour==da))))){errno=EINVAL;return -1;}
    for(unsigned byte=0;byte<4;++byte)if(mask[byte]!=ma[byte]){errno=ENOTSUP;return -1;}
    if(consume)for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)
        if(source[i]==sa[j] || source[i]==mask[j]){errno=EINVAL;return -1;}
    if(target && target->colour)for(unsigned c=0;c<3;++c)if(target->rgb_bits[c]<1 || target->rgb_bits[c]>8){errno=EINVAL;return -1;}
    struct ps2_unorm_layout precision=*layout;
    if(precision.alpha_bits>8){errno=EINVAL;return -1;}
    if(!precision.alpha_bits)precision.alpha_bits=8;
    for(unsigned c=0;c<3;++c){
        if(precision.rgb_bits[c]>8){errno=EINVAL;return -1;}
        if(!precision.rgb_bits[c])precision.rgb_bits[c]=8;
    }
    uint32_t input[18],out[]={destination,da,target?target->map:0,target?target->colour:0};
    const uint32_t *planes[]={source,sa,mask,ma};
    for(unsigned i=0;i<4;++i)for(unsigned byte=0;byte<4;++byte)input[i*4+byte]=planes[i][byte];
    input[16]=destination;input[17]=da;
    if(validate(w,f,input,18,out,target?(target->colour?4:3):2,p,width,height))return -1;
    for(unsigned i=0;i<16;++i)if(input[i]==destination || input[i]==da){errno=EINVAL;return -1;}
    if(target)for(unsigned i=0;i<18;++i)if(input[i]==target->map || (target->colour && input[i]==target->colour)){errno=EINVAL;return -1;}
    uint32_t owned[39]={0};int result=-1,error=0;
    /* Sampling no longer needs source after the masked copies below. The
     * consuming entry point borrows those four pages for hsl_rgb's x value. */
    for(unsigned i=0;i<39;++i){
        if(consume && i>=28 && i<32)owned[i]=source[i-28];
        else if(ps2_surface_create(w->gs,64,32,32,owned+i))goto done;
    }
    unsigned in=target?target->input_bits:precision.alpha_bits;const unsigned input_bits[]={in,in,in};
    if(ps2_native_float_multiply(w,f,source,mask,owned,p,width,height) ||
        ps2_native_float_multiply(w,f,sa,mask,owned+4,p,width,height) ||
        ps2_native_unorm_float(w->gs,destination,owned+8,precision.rgb_bits,p,width,height) ||
        ps2_native_unorm_float(w->gs,da,owned+12,input_bits,p,width,height) ||
        hsl_rgb(w,f,owned,p,width,height,mode) ||
        dodge_burn_store(w,f,owned+16,destination,precision.rgb_bits,target,0,p,width,height) ||
        dodge_burn_alpha(w,f,sa,ma,da,precision.alpha_bits,target,owned,p,width,height))goto done;
    result=0;
done:
    error=errno;
    for(unsigned i=0;i<39;++i)if(!(consume && i>=28 && i<32) && owned[i] && !w->gs->error && ps2_surface_destroy(w->gs,owned[i])){
        if(!result)error=errno;
        result=-1;
    }
    errno=error;return result;
}
int ps2_native_hsl_float_mapped(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t source[4],const uint32_t sa[4],const uint32_t mask[4],const uint32_t ma[4],
    uint32_t destination,uint32_t da,const struct ps2_unorm_layout *layout,
    const struct ps2_saturate_map_target *target,uint32_t p,unsigned width,unsigned height,unsigned mode)
{return hsl_float_mapped(w,f,source,sa,mask,ma,destination,da,layout,target,p,width,height,mode,0);}
int ps2_native_hsl_float_mapped_consume(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t source[4],const uint32_t sa[4],const uint32_t mask[4],const uint32_t ma[4],
    uint32_t destination,uint32_t da,const struct ps2_unorm_layout *layout,
    const struct ps2_saturate_map_target *target,uint32_t p,unsigned width,unsigned height,unsigned mode)
{return hsl_float_mapped(w,f,source,sa,mask,ma,destination,da,layout,target,p,width,height,mode,1);}

int ps2_native_hsl_alpha_float_mapped(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t sa[4],const uint32_t ma[4],uint32_t da,unsigned alpha_bits,
    const struct ps2_saturate_map_target *target,uint32_t p,unsigned width,unsigned height,unsigned mode)
{
    if(mode>3){errno=EINVAL;return -1;}
    return ps2_native_dodge_burn_alpha_float_mapped(w,f,sa,ma,da,alpha_bits,target,p,width,height,0);
}
int ps2_native_saturate_unorm(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    uint32_t source,uint32_t sa,uint32_t mask,uint32_t ma,uint32_t destination,uint32_t da,
    const struct ps2_unorm_layout layouts[3],uint32_t p,unsigned width,unsigned height)
{
    if(!layouts){errno=EINVAL;return -1;}
    struct ps2_unorm_layout precision[3];memcpy(precision,layouts,sizeof(precision));
    for(unsigned i=0;i<3;++i){
        if(precision[i].alpha_bits>8){errno=EINVAL;return -1;}
        if(!precision[i].alpha_bits)precision[i].alpha_bits=8;
        for(unsigned c=0;c<3;++c){
            if(precision[i].rgb_bits[c]>8){errno=EINVAL;return -1;}
            if(!precision[i].rgb_bits[c])precision[i].rgb_bits[c]=8;
        }
    }
    if(mask==ma)for(unsigned c=0;c<3;++c)precision[1].rgb_bits[c]=precision[1].alpha_bits;
    uint32_t input[]={source,sa,mask,ma,destination,da},out[]={destination,da};
    if(validate(w,f,input,6,out,2,p,width,height))return -1;
    for(unsigned i=0;i<6;++i)for(unsigned j=0;j<i;++j)
        if(input[i]==input[j] && !(i==3 && j==2)){errno=EINVAL;return -1;}
    uint32_t owned[13]={0};int result=-1,error=0;
    for(unsigned i=0;i<13;++i)if(ps2_surface_create(w->gs,64,32,32,owned+i))goto done;
    uint32_t *factor=owned,*s=owned+4,*d=owned+8,zero=owned[12];
    unsigned sabits[]={precision[0].alpha_bits,precision[0].alpha_bits,precision[0].alpha_bits},
        mabits[]={precision[1].alpha_bits,precision[1].alpha_bits,precision[1].alpha_bits},
        dabits[]={precision[2].alpha_bits,precision[2].alpha_bits,precision[2].alpha_bits};
    if(saturate_factor(w,f,sa,mask,da,factor,s,d,precision[0].alpha_bits,precision[1].rgb_bits,precision[2].alpha_bits,zero,p,width,height) ||
        saturate_channel(w,f,source,mask,destination,factor,s,d,precision[0].rgb_bits,precision[1].rgb_bits,precision[2].rgb_bits,p,width,height) ||
        (mask!=ma && saturate_factor(w,f,sa,ma,da,factor,s,d,precision[0].alpha_bits,mabits,precision[2].alpha_bits,zero,p,width,height)) ||
        saturate_channel(w,f,sa,ma,da,factor,s,d,sabits,mabits,dabits,p,width,height))goto done;
    result=0;
done:
    error=errno;
    for(unsigned i=0;i<13;++i)if(owned[i] && !w->gs->error && ps2_surface_destroy(w->gs,owned[i])){
        if(!result)error=errno;
        result=-1;
    }
    errno=error;return result;
}
int ps2_native_saturate_rgba8(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    uint32_t source,uint32_t sa,uint32_t mask,uint32_t ma,uint32_t destination,uint32_t da,
    uint32_t p,unsigned width,unsigned height)
{
    const struct ps2_unorm_layout layouts[3]={0};
    return ps2_native_saturate_unorm(w,f,source,sa,mask,ma,destination,da,layouts,p,width,height);
}

/* Packed destinations retain the float result until their own GS encoder.
 * The existing byte/word entry points above keep their original contracts. */
static int packed_ratio_float(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t s[8],const uint32_t m[8],const uint32_t d[8],uint32_t p,
    unsigned width,unsigned height,unsigned op,unsigned ca)
{
    const unsigned char saturate[]={PD_INV_DA_OVER_SA,PD_ONE};
    const unsigned char *kinds=op==PS2_COMPOSITE_SATURATE || op==PS2_COMPOSITE_DISJOINT_OVER_REVERSE?
        saturate:pd_factors[op-PS2_COMPOSITE_DISJOINT_OVER];
    uint32_t h[17]={0};int result=-1,error=0;
    for(unsigned i=0;i<17;++i)if(ps2_surface_create(w->gs,64,32,32,h+i))goto done;
    for(unsigned a=0;a<2;++a){const uint32_t *mask=m+(a || !ca?4:0),*source=s+(a?4:0),*dest=d+(a?4:0);
        for(unsigned factor=0;factor<2;++factor){
            if(kinds[factor]>PD_ONE && (ps2_native_float_multiply(w,f,s+4,mask,h+8,p,width,height) ||
                copy_float(w,d+4,h+12,width,height)))goto done;
            if(pd_factor_float(w,f,kinds[factor],h+4*factor,h+8,h+12,h[16],p,width,height))goto done;
        }
        if(ps2_native_float_multiply(w,f,source,mask,h+8,p,width,height) ||
            ps2_native_float_multiply(w,f,h+8,h,h+8,p,width,height) ||
            ps2_native_float_multiply(w,f,dest,h+4,h+12,p,width,height) ||
            ps2_native_float_add(w,f,h+8,h+12,dest,p,width,height,0))goto done;
    }
    result=0;
done:
    error=errno;for(unsigned i=0;i<17;++i)if(h[i] && !w->gs->error && ps2_surface_destroy(w->gs,h[i])){
        if(!result)error=errno;
        result=-1;
    }
    errno=error;return result;
}
static int packed_pdf_simple(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    uint32_t r[39],uint32_t p,unsigned width,unsigned height,unsigned op)
{
    const uint32_t *s=r,*sa=r+4,*d=r+8,*da=r+12;uint32_t *base=r+16,*x=r+20,*y=r+24,*z=r+28;
    uint32_t flag=r[32];
#define MATH(a,b,out,kind) ps2_native_float_signed(w,f,a,b,out,p,width,height,PS2_FLOAT_##kind)
    if(floating_one(w,x,width,height) || MATH(x,sa,x,SUBTRACT) || MATH(x,d,x,MULTIPLY) ||
        floating_one(w,y,width,height) || MATH(y,da,y,SUBTRACT) || MATH(y,s,y,MULTIPLY) || MATH(x,y,base,ADD))return -1;
    if(op==PS2_COMPOSITE_MULTIPLY){if(MATH(d,s,x,MULTIPLY))return -1;}
    else if(op==PS2_COMPOSITE_OVERLAY || op==PS2_COMPOSITE_HARDLIGHT){
        const uint32_t *test=op==PS2_COMPOSITE_OVERLAY?d:s,*alpha=op==PS2_COMPOSITE_OVERLAY?da:sa;
        if(MATH(test,test,x,ADD) || ps2_native_float_signed_compare(w,f,x,alpha,flag,p,width,height,PS2_FLOAT_LT) ||
            MATH(s,s,z,ADD) || MATH(z,d,z,MULTIPLY) ||
            MATH(da,d,x,SUBTRACT) || MATH(x,x,x,ADD) || MATH(sa,s,y,SUBTRACT) ||
            MATH(x,y,x,MULTIPLY) || MATH(sa,da,y,MULTIPLY) || MATH(y,x,x,SUBTRACT) ||
            ps2_native_float_select(w,f,z,x,flag,x,p,width,height))return -1;
    }else{
        if(MATH(d,sa,x,MULTIPLY) || MATH(s,da,y,MULTIPLY))return -1;
        if(op==PS2_COMPOSITE_DARKEN || op==PS2_COMPOSITE_LIGHTEN){
            if(ps2_native_float_signed_compare(w,f,x,y,flag,p,width,height,PS2_FLOAT_LT) ||
                ps2_native_float_select(w,f,op==PS2_COMPOSITE_DARKEN?x:y,op==PS2_COMPOSITE_DARKEN?y:x,flag,x,p,width,height))return -1;
        }else if(op==PS2_COMPOSITE_DIFFERENCE){
            unsigned char table[256];for(unsigned i=0;i<256;++i)table[i]=i&127;
            if(MATH(x,y,x,SUBTRACT) || lookup(w,x[3],w->term,p,table,width,height) ||
                transfer(w,w->term,x[3],width,height,PS2GS_TRANSFER_REPLACE))return -1;
        }else{
            if(MATH(x,y,x,ADD))return -1;
            if(op==PS2_COMPOSITE_EXCLUSION){if(MATH(d,d,z,ADD) || MATH(z,s,z,MULTIPLY))return -1;}
            else if(MATH(s,d,z,MULTIPLY))return -1;
            if(MATH(x,z,x,SUBTRACT))return -1;
        }
    }
    return MATH(base,x,base,ADD);
#undef MATH
}
int ps2_native_float_blend_consume(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const uint32_t s[8],const uint32_t m[8],const uint32_t d[8],const uint32_t t[12],
    uint32_t p,unsigned width,unsigned height,unsigned op,unsigned ca)
{
    if(!s || !m || !d || !t || ca>1 || op<PS2_COMPOSITE_SATURATE || op>PS2_COMPOSITE_HSL_LUMINOSITY ||
        (op>=PS2_COMPOSITE_DISJOINT_CLEAR && op<=PS2_COMPOSITE_CONJOINT_DST)){errno=EINVAL;return -1;}
    uint32_t all[36];memcpy(all,s,32);memcpy(all+8,m,32);memcpy(all+16,d,32);memcpy(all+24,t,48);
    if(ps2_native_float_operands(w,f,all,36,d,8,p,width,height))return -1;
    for(unsigned i=0;i<36;++i)for(unsigned j=0;j<i;++j)if(all[i]==all[j]){errno=EINVAL;return -1;}
    if(!(w->gs->caps.features&PS2GS_HW_SOLID_LOGIC)){errno=ENOTSUP;return -1;}
    if(op>=PS2_COMPOSITE_HSL_HUE && ca)return 0;
    if(op==PS2_COMPOSITE_SATURATE || (op>=PS2_COMPOSITE_DISJOINT_OVER_REVERSE && op<=PS2_COMPOSITE_CONJOINT_XOR))
        return packed_ratio_float(w,f,s,m,d,p,width,height,op,ca);
    uint32_t r[39],owned[7]={0};int result=-1,error=0;
    memcpy(r,s,16);memcpy(r+4,m,16);memcpy(r+8,d,32);memcpy(r+16,t,16);
    memcpy(r+20,m+4,16);memcpy(r+24,t+4,32);
    for(unsigned i=0;i<7;++i){if(ps2_surface_create(w->gs,64,32,32,owned+i))goto done;r[32+i]=owned[i];}
    const uint32_t *mask=ca?m:m+4;
    /* Preserve scalar alpha separately before kernels consume their RGB and
     * effective-alpha registers. No borrowed surface ownership is changed. */
    if(ps2_native_float_multiply(w,f,s,mask,s,p,width,height) ||
        ps2_native_float_multiply(w,f,s+4,mask,m,p,width,height) ||
        ps2_native_float_multiply(w,f,s+4,m+4,s+4,p,width,height) ||
        ps2_native_float_multiply(w,f,s+4,d+4,t,p,width,height) ||
        ps2_native_float_add(w,f,d+4,s+4,s+4,p,width,height,0) ||
        ps2_native_float_add(w,f,s+4,t,s+4,p,width,height,1))goto done;
    if(op>=PS2_COMPOSITE_HSL_HUE){if(hsl_rgb(w,f,r,p,width,height,op-PS2_COMPOSITE_HSL_HUE))goto done;}
    else if(op==PS2_COMPOSITE_SOFTLIGHT){if(softlight_rgb(w,f,r,p,width,height))goto done;}
    else if(op==PS2_COMPOSITE_COLOR_DODGE || op==PS2_COMPOSITE_COLOR_BURN){
        if(dodge_burn_rgb(w,f,r,p,width,height,op==PS2_COMPOSITE_COLOR_BURN))goto done;
    }else if(packed_pdf_simple(w,f,r,p,width,height,op))goto done;
    if(copy_float(w,r+16,d,width,height) || copy_float(w,s+4,d+4,width,height))goto done;
    result=0;
done:
    error=errno;for(unsigned i=0;i<7;++i)if(owned[i] && !w->gs->error && ps2_surface_destroy(w->gs,owned[i])){
        if(!result)error=errno;
        result=-1;
    }
    errno=error;return result;
}
