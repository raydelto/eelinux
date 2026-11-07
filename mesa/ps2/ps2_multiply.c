/* SPDX-License-Identifier: MIT */
#include "ps2_multiply.h"
#include "ps2_gs_lut.h"
#include "ps2_gs_transfer.h"
#include "ps2_gs_logic.h"
#include <errno.h>
static int transfer(struct ps2_filter_workspace *f,uint32_t s,uint32_t d,
    unsigned w,unsigned h,unsigned mode)
{return ps2_rop_sample(f->gs,s,d,0,0,0,0,w,h,0xffffff,mode,128);}
static int lookup(struct ps2_filter_workspace *f,uint32_t s,uint32_t d,uint32_t p,
    const unsigned char table[256],unsigned w,unsigned h)
{return ps2_surface_lut_palette(f->gs,p,table) ||
    ps2_surface_channel_lut(f->gs,s,d,p,w,h,0xffffff)?-1:0;}
static int add_low(struct ps2_filter_workspace *f,uint32_t term,uint32_t p,unsigned w,unsigned h)
{
    if(transfer(f,f->sum[0],f->saturated,w,h,PS2GS_TRANSFER_REPLACE) ||
        transfer(f,term,f->saturated,w,h,PS2GS_TRANSFER_ADD_CLAMP) ||
        transfer(f,term,f->sum[0],w,h,PS2GS_TRANSFER_ADD) ||
        transfer(f,f->sum[0],f->saturated,w,h,PS2GS_TRANSFER_SUBTRACT))return -1;
    unsigned char nonzero[256];for(unsigned i=0;i<256;++i)nonzero[i]=!!i;
    return lookup(f,f->saturated,f->carry,p,nonzero,w,h) ||
        transfer(f,f->carry,f->sum[1],w,h,PS2GS_TRANSFER_ADD)?-1:0;
}
static int operands(struct ps2_filter_workspace *f,uint32_t a,uint32_t b,
    uint32_t destination,uint32_t palette,unsigned width,unsigned height)
{
    if(!f || !f->gs || !width || !height || width>64 || height>32 || f->bytes<3){errno=EINVAL;return -1;}
    struct ps2_surface_client *gs=f->gs;
    const struct ps2_surface *s=ps2_surface_lookup(gs,a),*m=ps2_surface_lookup(gs,b),
        *d=ps2_surface_lookup(gs,destination),*p=ps2_surface_lookup(gs,palette);
    if(!a || !b || !destination || !palette || destination==palette || !s || !m || !d || !p ||
        s->bits_per_pixel!=32 || m->bits_per_pixel!=32 || d->bits_per_pixel!=32 ||
        s->width!=64 || s->height!=32 || m->width!=64 || m->height!=32 ||
        d->width<width || d->height<height || p->width!=16 || p->height!=16 ||
        p->bits_per_pixel!=32){errno=EINVAL;return -1;}
    if(!(gs->caps.features&PS2GS_HW_RGBA_TRANSFER) || !(gs->caps.features&PS2GS_HW_CHANNEL_MAP)){
        errno=ENOTSUP;return -1;}
    uint32_t scratch[]={f->tap,f->term,f->sum[0],f->sum[1],f->saturated,f->carry};
    for(unsigned i=0;i<sizeof(scratch)/sizeof(*scratch);++i)
        if(a==scratch[i] || b==scratch[i] || destination==scratch[i] || palette==scratch[i]){
            errno=EINVAL;return -1;}
    return 0;
}
static int accumulate(struct ps2_filter_workspace *f,uint32_t a,uint32_t b,
    uint32_t destination,uint32_t palette,unsigned width,unsigned height)
{
    if(operands(f,a,b,destination,palette,width,height))return -1;
    struct ps2_surface_client *gs=f->gs;
    if(ps2_surface_solid_logic(gs,f->sum[0],0,0,width,height,0,0xffffff,0) ||
        ps2_surface_solid_logic(gs,f->sum[1],0,0,width,height,0,0xffffff,0))return -1;
    unsigned char table[256];uint32_t mapped[256];
    /* Nibble factors scaled by 8 and 16 make the GS /128 blend product
     * an exact integer in [0,225]. Four partial products retain all 16 bits. */
    for(unsigned c=0;c<3;++c)for(unsigned ah=0;ah<2;++ah)for(unsigned bh=0;bh<2;++bh){
        for(unsigned i=0;i<256;++i)table[i]=((i>>(ah*4))&15)*8;
        if(lookup(f,a,f->tap,palette,table,width,height))return -1;
        for(unsigned i=0;i<256;++i)mapped[i]=(((i>>(bh*4))&15)*16)<<24;
        if(ps2_surface_map_palette(gs,palette,mapped) ||
            ps2_surface_channel_map(gs,b,f->term,palette,width,height,UINT32_MAX,c) ||
            ps2_surface_rgba_rectangle(gs,f->term,f->tap,0,0,0,0,width,height,0xff000000,PS2GS_RGBA_REPLACE) ||
            ps2_surface_rgba_rectangle(gs,f->tap,f->term,0,0,0,0,width,height,255U<<(c*8),PS2GS_RGBA_MULTIPLY))return -1;
        unsigned shift=(ah+bh)*4;
        if(shift<8){
            for(unsigned i=0;i<256;++i)table[i]=(i<<shift)&255;
            if(lookup(f,f->term,f->tap,palette,table,width,height) ||
                add_low(f,f->tap,palette,width,height))return -1;
        }
        if(shift){
            for(unsigned i=0;i<256;++i)table[i]=(i<<shift)>>8;
            if(lookup(f,f->term,f->tap,palette,table,width,height) ||
                transfer(f,f->tap,f->sum[1],width,height,PS2GS_TRANSFER_ADD))return -1;
        }
    }
    return 0;
}
int ps2_native_product(struct ps2_filter_workspace *f,uint32_t a,uint32_t b,
    uint32_t low,uint32_t high,uint32_t palette,unsigned width,unsigned height)
{
    const struct ps2_surface *h=f && f->gs?ps2_surface_lookup(f->gs,high):NULL;
    if(!h || h->bits_per_pixel!=32 || h->width<width || h->height<height || low==high || high==palette){errno=EINVAL;return -1;}
    uint32_t scratch[]={f->tap,f->term,f->sum[0],f->sum[1],f->saturated,f->carry};
    for(unsigned i=0;i<sizeof(scratch)/sizeof(*scratch);++i)if(high==scratch[i]){errno=EINVAL;return -1;}
    return accumulate(f,a,b,low,palette,width,height) ||
        transfer(f,f->sum[0],low,width,height,0) || transfer(f,f->sum[1],high,width,height,0)?-1:0;
}
static int normalize(struct ps2_filter_workspace *f,
    uint32_t destination,uint32_t palette,unsigned width,unsigned height)
{
    struct ps2_surface_client *gs=f->gs;
    /* round(p/255) = (p + 128 + ((p + 128) >> 8)) >> 8.
     * The maximum intermediate is below 65536. Carry detection remains GS. */
    if(ps2_surface_solid_logic(gs,f->tap,0,0,width,height,0x808080,0xffffff,3) ||
        add_low(f,f->tap,palette,width,height) ||
        transfer(f,f->sum[1],f->tap,width,height,PS2GS_TRANSFER_REPLACE) ||
        add_low(f,f->tap,palette,width,height))return -1;
    return transfer(f,f->sum[1],destination,width,height,PS2GS_TRANSFER_REPLACE);
}
int ps2_native_multiply(struct ps2_filter_workspace *f,uint32_t a,uint32_t b,
    uint32_t destination,uint32_t palette,unsigned width,unsigned height)
{
    return accumulate(f,a,b,destination,palette,width,height) ||
        normalize(f,destination,palette,width,height)?-1:0;
}
int ps2_native_exclusion(struct ps2_filter_workspace *f,uint32_t a,uint32_t b,
    uint32_t destination,const uint32_t work[3],uint32_t palette,unsigned width,unsigned height)
{
    /* Keep both unrounded products: round((a*(255-b)+b*(255-a))/255).
     * Their sum is at most 65025, so the same two-byte normalizer is exact.
     * Only uniform inverse palettes are generated on CPU. */
    if(!work){errno=EINVAL;return -1;}
    if(operands(f,a,b,destination,palette,width,height) ||
        operands(f,work[0],work[1],work[2],palette,width,height))return -1;
    for(unsigned i=0;i<3;++i){
        const struct ps2_surface *s=ps2_surface_lookup(f->gs,work[i]);
        if(!s || s->bits_per_pixel!=32 || s->width!=64 || s->height!=32 ||
            work[i]==a || work[i]==b || work[i]==destination || work[i]==palette ||
            work[i]==f->sum[2] || work[i]==f->sum[3]){errno=EINVAL;return -1;}
        for(unsigned j=0;j<i;++j)if(work[i]==work[j]){errno=EINVAL;return -1;}
    }
    unsigned char inverse[256];for(unsigned i=0;i<256;++i)inverse[i]=255-i;
    return lookup(f,b,work[0],palette,inverse,width,height) ||
        ps2_native_product(f,a,work[0],work[1],work[2],palette,width,height) ||
        lookup(f,a,work[0],palette,inverse,width,height) ||
        accumulate(f,b,work[0],destination,palette,width,height) ||
        add_low(f,work[1],palette,width,height) ||
        transfer(f,work[2],f->sum[1],width,height,PS2GS_TRANSFER_ADD) ||
        normalize(f,destination,palette,width,height)?-1:0;
}
static int select_byte(struct ps2_filter_workspace *f,uint32_t destination,uint32_t yes,uint32_t no,
    uint32_t predicate,uint32_t p,unsigned w,unsigned h)
{
    uint32_t table[256];
    for(unsigned branch=0;branch<2;++branch)for(unsigned c=0;c<3;++c){
        for(unsigned i=0;i<256;++i)table[i]=(uint32_t)((!!i)==branch?128:0)<<24;
        if(transfer(f,branch?yes:no,f->tap,w,h,PS2GS_TRANSFER_REPLACE) ||
            ps2_surface_map_palette(f->gs,p,table) ||
            ps2_surface_channel_map(f->gs,predicate,f->tap,p,w,h,0xff000000,c) ||
            ps2_surface_rgba_rectangle(f->gs,f->tap,branch?f->term:f->saturated,
                0,0,0,0,w,h,255U<<(c*8),PS2GS_RGBA_MULTIPLY))return -1;
    }
    return transfer(f,f->term,destination,w,h,PS2GS_TRANSFER_REPLACE) ||
        transfer(f,f->saturated,destination,w,h,PS2GS_TRANSFER_ADD)?-1:0;
}
static int add_byte(struct ps2_filter_workspace *f,uint32_t a,uint32_t b,uint32_t carry,
    uint32_t p,unsigned w,unsigned h)
{
    unsigned char nonzero[256];for(unsigned i=0;i<256;++i)nonzero[i]=!!i;
    return transfer(f,a,f->saturated,w,h,PS2GS_TRANSFER_REPLACE) ||
        transfer(f,b,f->saturated,w,h,PS2GS_TRANSFER_ADD_CLAMP) ||
        transfer(f,b,a,w,h,PS2GS_TRANSFER_ADD) ||
        transfer(f,a,f->saturated,w,h,PS2GS_TRANSFER_SUBTRACT) ||
        lookup(f,f->saturated,carry,p,nonzero,w,h)?-1:0;
}
static int blend_operands(struct ps2_filter_workspace *f,uint32_t a,uint32_t b,
    uint32_t sa,uint32_t da,uint32_t destination,const uint32_t *work,unsigned work_count,
    uint32_t p,unsigned w,unsigned h)
{
    if(!work){errno=EINVAL;return -1;}
    if(operands(f,a,b,destination,p,w,h) || operands(f,sa,da,destination,p,w,h))return -1;
    unsigned required=PS2GS_HW_CHANNEL_LUT|PS2GS_HW_CHANNEL_MAP|PS2GS_HW_TEXTURE_TRANSFER|
        PS2GS_HW_RGBA_TRANSFER|PS2GS_HW_SOLID_LOGIC;
    if((f->gs->caps.features&required)!=required){errno=ENOTSUP;return -1;}
    uint32_t private[]={f->tap,f->term,f->sum[0],f->sum[1],f->sum[2],f->saturated,f->carry,f->sum[3]};
    unsigned count=f->bytes==3?7:8;
    if(f->bytes>4){errno=EINVAL;return -1;}
    uint32_t input[]={a,b,sa,da,destination,p};
    for(unsigned i=0;i<count;++i){
        const struct ps2_surface *s=ps2_surface_lookup(f->gs,private[i]);
        if(!s || s->bits_per_pixel!=32 || s->width!=64 || s->height!=32){errno=EINVAL;return -1;}
        for(unsigned j=0;j<i;++j)if(private[i]==private[j]){errno=EINVAL;return -1;}
        for(unsigned j=0;j<6;++j)if(private[i]==input[j]){errno=EINVAL;return -1;}
    }
    for(unsigned i=0;i<work_count;++i){
        const struct ps2_surface *s=ps2_surface_lookup(f->gs,work[i]);
        if(!s || s->bits_per_pixel!=32 || s->width!=64 || s->height!=32){errno=EINVAL;return -1;}
        for(unsigned j=0;j<count;++j)if(work[i]==private[j]){errno=EINVAL;return -1;}
        for(unsigned j=0;j<6;++j)if(work[i]==input[j]){errno=EINVAL;return -1;}
        for(unsigned j=0;j<i;++j)if(work[i]==work[j]){errno=EINVAL;return -1;}
    }
    return 0;
}
int ps2_native_order_blend(struct ps2_filter_workspace *f,uint32_t a,uint32_t b,
    uint32_t sa,uint32_t da,uint32_t destination,const uint32_t work[4],uint32_t p,
    unsigned w,unsigned h,unsigned mode)
{
    if(mode>PS2_NATIVE_DIFFERENCE){errno=EINVAL;return -1;}
    if(blend_operands(f,a,b,sa,da,destination,work,4,p,w,h))return -1;
    struct ps2_surface_client *gs=f->gs;unsigned char table[256];uint32_t mapped[256];
    if(ps2_native_product(f,a,da,work[0],work[1],p,w,h) ||
        ps2_native_product(f,b,sa,work[2],work[3],p,w,h) ||
        ps2_surface_solid_logic(gs,f->sum[2],0,0,w,h,0,0xffffff,0))return -1;
    /* Unsigned P>Q. Each higher unequal byte overrides the low-byte result. */
    for(unsigned byte=0;byte<2;++byte){
        if(transfer(f,work[byte],f->tap,w,h,PS2GS_TRANSFER_REPLACE) ||
            transfer(f,work[byte+2],f->tap,w,h,PS2GS_TRANSFER_SUBTRACT) ||
            transfer(f,f->tap,f->sum[2],w,h,PS2GS_TRANSFER_ADD_CLAMP) ||
            transfer(f,work[byte+2],f->carry,w,h,PS2GS_TRANSFER_REPLACE) ||
            transfer(f,work[byte],f->carry,w,h,PS2GS_TRANSFER_SUBTRACT))return -1;
        for(unsigned c=0;c<3;++c){
            for(unsigned i=0;i<256;++i)mapped[i]=i?0:0x80000000U;
            if(transfer(f,f->sum[2],f->tap,w,h,PS2GS_TRANSFER_REPLACE) ||
                ps2_surface_map_palette(gs,p,mapped) ||
                ps2_surface_channel_map(gs,f->carry,f->tap,p,w,h,0xff000000,c) ||
                ps2_surface_rgba_rectangle(gs,f->tap,f->sum[2],0,0,0,0,w,h,255U<<(c*8),PS2GS_RGBA_MULTIPLY))return -1;
        }
    }
    for(unsigned byte=0;byte<2;++byte)
        if(select_byte(f,work[byte],work[byte+(mode==PS2_NATIVE_DARKEN?0:2)],
            work[byte+(mode==PS2_NATIVE_DARKEN?2:0)],f->sum[2],p,w,h))return -1;
    /* N=255*(S+D)-max(S*Da,D*Sa) for Darken, subtract min for
     * Lighten and twice min for Difference. N is nonnegative and can
     * exceed 65535 for non-premultiplied inputs. Retain the seventeenth bit. */
    if(ps2_surface_solid_logic(gs,f->sum[2],0,0,w,h,0xffffff,0xffffff,3) ||
        ps2_native_product(f,a,f->sum[2],work[2],work[3],p,w,h) ||
        accumulate(f,b,f->sum[2],destination,p,w,h) ||
        transfer(f,f->sum[1],f->term,w,h,PS2GS_TRANSFER_REPLACE) ||
        add_low(f,work[2],p,w,h) ||
        transfer(f,f->term,f->saturated,w,h,PS2GS_TRANSFER_REPLACE) ||
        transfer(f,f->sum[1],f->saturated,w,h,PS2GS_TRANSFER_SUBTRACT))return -1;
    for(unsigned i=0;i<256;++i)table[i]=!!i;
    if(lookup(f,f->saturated,f->sum[2],p,table,w,h) ||
        add_byte(f,f->sum[1],work[3],f->carry,p,w,h) ||
        transfer(f,f->carry,f->sum[2],w,h,PS2GS_TRANSFER_ADD))return -1;
    for(unsigned pass=0;pass<(mode==PS2_NATIVE_DIFFERENCE?2U:1U);++pass){
        if(ps2_surface_solid_logic(gs,work[2],0,0,w,h,0,0xffffff,0))return -1;
        for(unsigned byte=0;byte<3;++byte){
            for(unsigned i=0;i<256;++i)table[i]=255-i;
            if(byte<2){if(lookup(f,work[byte],f->term,p,table,w,h))return -1;}
            else if(ps2_surface_solid_logic(gs,f->term,0,0,w,h,0xffffff,0xffffff,3))return -1;
            if(add_byte(f,f->sum[byte],f->term,f->carry,p,w,h))return -1;
            for(unsigned i=0;i<256;++i)table[i]=!i;
            if(lookup(f,work[2],f->term,p,table,w,h) ||
                add_byte(f,f->sum[byte],f->term,work[3],p,w,h) ||
                transfer(f,f->carry,work[3],w,h,PS2GS_TRANSFER_ADD) ||
                lookup(f,work[3],work[2],p,table,w,h))return -1;
        }
    }
    /* Every value with high byte >=254 already rounds to at least 255.
     * Save this predicate before normalization can wrap its byte result. */
    for(unsigned i=0;i<256;++i)table[i]=i>=254;
    return lookup(f,f->sum[1],work[2],p,table,w,h) ||
        transfer(f,f->sum[2],work[2],w,h,PS2GS_TRANSFER_ADD_CLAMP) ||
        normalize(f,destination,p,w,h) ||
        ps2_surface_solid_logic(gs,work[3],0,0,w,h,0xffffff,0xffffff,3) ||
        select_byte(f,destination,work[3],destination,work[2],p,w,h)?-1:0;
}
enum product_sign { PRODUCT_PLUS,PRODUCT_MINUS,PRODUCT_MINUS_HIGH,PRODUCT_MINUS_LOW };
static int add_signed_product(struct ps2_filter_workspace *f,const uint32_t work[6],
    uint32_t p,unsigned w,unsigned h,unsigned sign,unsigned high_only)
{
    unsigned char table[256];
    /* sum[0..1] hold the unrounded product; sum[2] is the high-branch
     * predicate. Accumulate in external pages so the next product can use
     * the private low/high planes. Subtraction uses complement plus carry. */
    for(unsigned i=0;i<256;++i)table[i]=sign==PRODUCT_MINUS?1:
        sign==PRODUCT_MINUS_HIGH?!!i:sign==PRODUCT_MINUS_LOW?!i:0;
    if(lookup(f,f->sum[2],work[4],p,table,w,h))return -1;
    for(unsigned byte=0;byte<3;++byte){
        if(byte<2){
            for(unsigned i=0;i<256;++i)table[i]=sign==PRODUCT_PLUS?i:255-i;
            if(lookup(f,f->sum[byte],work[3],p,table,w,h))return -1;
            if(sign>=PRODUCT_MINUS_HIGH && select_byte(f,work[3],
                sign==PRODUCT_MINUS_HIGH?work[3]:f->sum[byte],
                sign==PRODUCT_MINUS_HIGH?f->sum[byte]:work[3],f->sum[2],p,w,h))return -1;
            /* This term is -Sa*Da in the high branch and zero in the low
             * branch. ~0 with the incoming +1 leaves the accumulator intact. */
            if(high_only && select_byte(f,work[3],work[3],work[5],f->sum[2],p,w,h))return -1;
        }else{
            for(unsigned i=0;i<256;++i)table[i]=sign==PRODUCT_MINUS?255:
                sign==PRODUCT_MINUS_HIGH?(i?255:0):sign==PRODUCT_MINUS_LOW?(i?0:255):0;
            if(lookup(f,f->sum[2],work[3],p,table,w,h))return -1;
        }
        if(add_byte(f,work[byte],work[3],f->carry,p,w,h) ||
            add_byte(f,work[byte],work[4],work[3],p,w,h) ||
            transfer(f,f->carry,work[3],w,h,PS2GS_TRANSFER_ADD) ||
            transfer(f,work[3],work[4],w,h,PS2GS_TRANSFER_REPLACE))return -1;
    }
    return 0;
}
int ps2_native_piecewise_blend(struct ps2_filter_workspace *f,uint32_t a,uint32_t b,
    uint32_t sa,uint32_t da,uint32_t destination,const uint32_t work[6],uint32_t p,
    unsigned w,unsigned h,unsigned mode)
{
    if(mode>PS2_NATIVE_HARDLIGHT){errno=EINVAL;return -1;}
    if(blend_operands(f,a,b,sa,da,destination,work,6,p,w,h))return -1;
    struct ps2_surface_client *gs=f->gs;unsigned char table[256];
    for(unsigned i=0;i<256;++i)table[i]=i<128?2*i:255;
    if(lookup(f,mode==PS2_NATIVE_OVERLAY?b:a,work[3],p,table,w,h) ||
        transfer(f,mode==PS2_NATIVE_OVERLAY?da:sa,f->sum[2],w,h,PS2GS_TRANSFER_REPLACE) ||
        transfer(f,work[3],f->sum[2],w,h,PS2GS_TRANSFER_SUBTRACT))return -1;
    /* High branch iff 2*D>=Da (Overlay), or 2*S>=Sa (HardLight).
     * Saturating the doubled byte to 255 preserves this comparison. */
    for(unsigned i=0;i<256;++i)table[i]=!i;
    if(lookup(f,f->sum[2],work[3],p,table,w,h) ||
        transfer(f,work[3],f->sum[2],w,h,PS2GS_TRANSFER_REPLACE) ||
        ps2_surface_solid_logic(gs,work[5],0,0,w,h,0xffffff,0xffffff,3))return -1;
    for(unsigned byte=0;byte<3;++byte)
        if(ps2_surface_solid_logic(gs,work[byte],0,0,w,h,0,0xffffff,0))return -1;
    /* Low: 255*(S+D)-S*Da-D*Sa+2*S*D.
     * High: 255*(S+D)+S*Da+D*Sa-2*S*D-Sa*Da.
     * Products and signed intermediate sums are retained without rounding. */
    if(accumulate(f,a,work[5],destination,p,w,h) ||
        add_signed_product(f,work,p,w,h,PRODUCT_PLUS,0) ||
        accumulate(f,b,work[5],destination,p,w,h) ||
        add_signed_product(f,work,p,w,h,PRODUCT_PLUS,0) ||
        accumulate(f,a,da,destination,p,w,h) ||
        add_signed_product(f,work,p,w,h,PRODUCT_MINUS_LOW,0) ||
        accumulate(f,b,sa,destination,p,w,h) ||
        add_signed_product(f,work,p,w,h,PRODUCT_MINUS_LOW,0) ||
        accumulate(f,a,b,destination,p,w,h) ||
        add_signed_product(f,work,p,w,h,PRODUCT_MINUS_HIGH,0) ||
        add_signed_product(f,work,p,w,h,PRODUCT_MINUS_HIGH,0) ||
        accumulate(f,sa,da,destination,p,w,h) ||
        add_signed_product(f,work,p,w,h,PRODUCT_MINUS,1))return -1;
    for(unsigned byte=0;byte<2;++byte)
        if(transfer(f,work[byte],f->sum[byte],w,h,PS2GS_TRANSFER_REPLACE))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i>=254;
    return lookup(f,work[1],work[4],p,table,w,h) ||
        transfer(f,work[2],work[4],w,h,PS2GS_TRANSFER_ADD_CLAMP) ||
        normalize(f,destination,p,w,h) ||
        select_byte(f,destination,work[5],destination,work[4],p,w,h)?-1:0;
}
static int add_carry(struct ps2_filter_workspace *f,uint32_t a,uint32_t b,
    uint32_t carry,uint32_t palette,unsigned width)
{
    unsigned char table[256];for(unsigned i=0;i<256;++i)table[i]=!!i;
    return transfer(f,a,f->saturated,width,1,0) ||
        transfer(f,b,f->saturated,width,1,PS2GS_TRANSFER_ADD_CLAMP) ||
        transfer(f,b,a,width,1,PS2GS_TRANSFER_ADD) ||
        transfer(f,a,f->saturated,width,1,PS2GS_TRANSFER_SUBTRACT) ||
        lookup(f,f->saturated,carry,palette,table,width,1)?-1:0;
}
int ps2_native_saturate_alpha(struct ps2_filter_workspace *f,uint32_t s,uint32_t m,uint32_t d,
    uint32_t low,uint32_t high,uint32_t extra,uint32_t factor,uint32_t palette,unsigned width)
{
    unsigned char table[256];
    if(ps2_native_product(f,s,m,low,high,palette,width,1))return -1;
    /* N = source * mask + destination * 255, retaining a seventeenth
     * overflow bit. The overflow always saturates the final alpha. */
    for(unsigned i=0;i<256;++i)table[i]=(256-i)&255;
    if(lookup(f,d,s,palette,table,width,1))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i?i-1:0;
    if(lookup(f,d,factor,palette,table,width,1) || add_carry(f,high,factor,extra,palette,width) ||
        add_carry(f,low,s,m,palette,width) || add_carry(f,high,m,factor,palette,width) ||
        transfer(f,factor,extra,width,1,PS2GS_TRANSFER_ADD_CLAMP))return -1;
    /* For N = 256H + L, floor(N*256/65025) is H plus predicates
     * (2H+L >= 255) and (2H+L >= 509), with final clamping. */
    if(transfer(f,low,s,width,1,0) || transfer(f,high,d,width,1,0) ||
        add_carry(f,s,high,m,palette,width) || add_carry(f,s,high,factor,palette,width) ||
        transfer(f,factor,m,width,1,PS2GS_TRANSFER_ADD) ||
        ps2_surface_solid_logic(f->gs,f->tap,0,0,width,1,0x010101,0xffffff,3) ||
        add_carry(f,s,f->tap,factor,palette,width) || transfer(f,factor,m,width,1,PS2GS_TRANSFER_ADD))return -1;
    for(unsigned i=0;i<256;++i)table[i]=!!i;
    if(lookup(f,m,factor,palette,table,width,1) ||
        ps2_surface_solid_logic(f->gs,f->tap,0,0,width,1,0x020202,0xffffff,3) ||
        add_carry(f,s,f->tap,f->carry,palette,width) || transfer(f,f->carry,m,width,1,PS2GS_TRANSFER_ADD))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i>=2;
    if(lookup(f,m,s,palette,table,width,1) || transfer(f,factor,d,width,1,PS2GS_TRANSFER_ADD_CLAMP) ||
        transfer(f,s,d,width,1,PS2GS_TRANSFER_ADD_CLAMP))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i?255:0;
    return lookup(f,extra,m,palette,table,width,1) || transfer(f,m,d,width,1,PS2GS_TRANSFER_ADD_CLAMP)?-1:0;
}
