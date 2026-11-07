/* SPDX-License-Identifier: MIT */
#include "previous-rop.c"
void miGlyphs(CARD8 op,PicturePtr source,PicturePtr destination,PictFormatPtr format,
    INT16 x,INT16 y,int count,GlyphListPtr lists,GlyphPtr *glyphs)
{
    (void)op;(void)source;(void)destination;(void)format;(void)x;(void)y;
    (void)count;(void)lists;(void)glyphs;abort();
}
#include "ps2_composite.h"
#include "ps2_coverage.h"
#include "ps2_gs_lut.h"
#include "ps2_blit_exa.h"
#include "ps2_solid.h"
#include "ps2_divide.h"
#include "ps2_wide.h"
#include "ps2_wide_filter.h"
#include "ps2_packed.h"
#include <math.h>
static int native_failure;
static unsigned composite_sampled;
static int composite_via_exa;
static int compositor_active;
static unsigned native_ioctl_calls;
static unsigned hsl_wire(unsigned op)
{return op>=PictOpHSLHue && op<=PictOpHSLLuminosity;}
static unsigned hsl_private(unsigned op)
{return op>=PS2_COMPOSITE_HSL_HUE && op<=PS2_COMPOSITE_HSL_LUMINOSITY;}
static unsigned ratio_test_operation(unsigned op)
{return hsl_private(op) || op==PS2_COMPOSITE_SOFTLIGHT || op==PS2_COMPOSITE_COLOR_DODGE || op==PS2_COMPOSITE_COLOR_BURN || op==PS2_COMPOSITE_DISJOINT_OVER_REVERSE || (op>=PS2_COMPOSITE_DISJOINT_OVER && op<=PS2_COMPOSITE_CONJOINT_XOR);}
static unsigned ratio_test_wire(unsigned op)
{
    if(hsl_private(op))return PictOpHSLHue+op-PS2_COMPOSITE_HSL_HUE;
    if(op==PS2_COMPOSITE_SOFTLIGHT)return PictOpSoftLight;
    if(op==PS2_COMPOSITE_COLOR_DODGE)return PictOpColorDodge;
    if(op==PS2_COMPOSITE_COLOR_BURN)return PictOpColorBurn;
    static const unsigned wire[]={PictOpDisjointOver,PictOpDisjointIn,PictOpDisjointInReverse,PictOpDisjointOut,PictOpDisjointOutReverse,PictOpDisjointAtop,PictOpDisjointAtopReverse,PictOpDisjointXor,PictOpConjointOver,PictOpConjointOverReverse,PictOpConjointIn,PictOpConjointInReverse,PictOpConjointOut,PictOpConjointOutReverse,PictOpConjointAtop,PictOpConjointAtopReverse,PictOpConjointXor};
    assert(op>=PS2_COMPOSITE_DISJOINT_OVER && op<=PS2_COMPOSITE_CONJOINT_XOR);
    return wire[op-PS2_COMPOSITE_DISJOINT_OVER];
}
static unsigned extended_operation(const char *name)
{
    if(!strncmp(name,"hsl-luminosity-",15))return PS2_COMPOSITE_HSL_LUMINOSITY;
    if(!strncmp(name,"hsl-color-",10))return PS2_COMPOSITE_HSL_COLOR;
    if(!strncmp(name,"hsl-saturation-",15))return PS2_COMPOSITE_HSL_SATURATION;
    if(!strncmp(name,"hsl-hue-",8))return PS2_COMPOSITE_HSL_HUE;
    if(!strncmp(name,"softlight-",10))return PS2_COMPOSITE_SOFTLIGHT;
    if(!strncmp(name,"dodge-",6))return PS2_COMPOSITE_COLOR_DODGE;
    if(!strncmp(name,"burn-",5))return PS2_COMPOSITE_COLOR_BURN;
    static const char *names[]={"djover-","djin-","djinrev-","djout-","djoutrev-","djatop-","djatoprev-","djxor-","cjover-","cjoverrev-","cjin-","cjinrev-","cjout-","cjoutrev-","cjatop-","cjatoprev-","cjxor-"};
    for(unsigned i=0;i<17;++i)if(!strncmp(name,names[i],strlen(names[i])))return PS2_COMPOSITE_DISJOINT_OVER+i;
    return strstr(name,"djclear")?PS2_COMPOSITE_DISJOINT_CLEAR:strstr(name,"djsrc")?PS2_COMPOSITE_DISJOINT_SRC:strstr(name,"djdst")?PS2_COMPOSITE_DISJOINT_DST:strstr(name,"cjclear")?PS2_COMPOSITE_CONJOINT_CLEAR:strstr(name,"cjsrc")?PS2_COMPOSITE_CONJOINT_SRC:strstr(name,"cjdst")?PS2_COMPOSITE_CONJOINT_DST:strstr(name,"djoverrev")?PS2_COMPOSITE_DISJOINT_OVER_REVERSE:strstr(name,"overlay")?PS2_COMPOSITE_OVERLAY:strstr(name,"hardlight")?PS2_COMPOSITE_HARDLIGHT:strstr(name,"darken")?PS2_COMPOSITE_DARKEN:strstr(name,"lighten")?PS2_COMPOSITE_LIGHTEN:strstr(name,"difference")?PS2_COMPOSITE_DIFFERENCE:strstr(name,"multiply")?PS2_COMPOSITE_MULTIPLY:strstr(name,"exclusion")?PS2_COMPOSITE_EXCLUSION:strstr(name,"screen")?PS2_COMPOSITE_SCREEN:0;}
static void rgba_seed(struct pixels *);
static PixmapPtr alpha_reply_pixmap;
static uint32_t alpha_reply_surface;
static void sync_alpha_reply(void)
{
    if(!alpha_reply_pixmap)return;
    struct ps2_alpha_source bytes;assert(ps2_surface_exa_alpha_source(alpha_reply_pixmap,&bytes)==1);
    struct pixels *p=model(alpha_reply_surface);
    for(unsigned y=0;y<bytes.height;++y)for(unsigned x=0;x<bytes.width;++x){
        const unsigned char *row=bytes.bits+y*bytes.stride;
        unsigned alpha=bytes.depth==1?((row[x/8]>>(BITMAP_BIT_ORDER==MSBFirst?7-(x&7):(x&7)))&1)*255:
            bytes.depth==4?(row[x]&15)*17:row[x];
        p->data[y*p->width+x]=alpha<<24;
    }
}
int __wrap_ioctl(int fd,unsigned long op,...)
{
    ++native_ioctl_calls;
    va_list a;va_start(a,op);void *arg=va_arg(a,void*);va_end(a);
    /* Drawing must issue no readback, including isolated alpha. Client
     * transfer occurs separately after GS has encoded A1/A8 output bytes. */
    if(op==PS2GS_SURFACE_DOWNLOAD)assert(!compositor_active);
    if(op==PS2GS_SURFACE_FILL){const struct ps2gs_surface_fill *r=arg;struct pixels *p=model(r->handle);
        uint32_t color=r->rgba;
        if(model_bpp(p)==16)color=((color>>3)&31)|(((color>>11)&31)<<5)|(((color>>19)&31)<<10)|((color>>16)&0x8000);
        for(unsigned y=r->y;y<r->y+r->height;++y)for(unsigned x=r->x;x<r->x+r->width;++x)p->data[y*p->width+x]=color;
        return 0;}
    if(op==PS2GS_GET_CAPS){int status=rop_ioctl(fd,op,arg);
        if(!status)((struct ps2gs_caps*)arg)->features|=PS2GS_HW_CHANNEL_LUT|PS2GS_HW_CHANNEL_MAP|
            PS2GS_HW_CHANNEL_MAP_BLIT|PS2GS_HW_RGBA_TRANSFER;
        return status;}
    if(op==PS2GS_CHANNEL_MAP){
        const struct ps2gs_channel_map *r=arg;
        struct pixels *s=model(r->source),*d=model(r->destination),*palette=model(r->palette);
        for(unsigned y=0;y<r->height;++y)for(unsigned x=0;x<r->width;++x){
            unsigned i=(s->data[y*s->width+x]>>(r->source_channel*8))&255;
            i=(i&~24U)|((i&8)<<1)|((i&16)>>1);
            unsigned di=y*d->width+x;d->data[di]=(d->data[di]&~r->plane_mask)|(palette->data[i]&r->plane_mask);
        }
        return 0;
    }
    if(op==PS2GS_CHANNEL_LUT){
        const struct ps2gs_channel_lut *r=arg;
        struct pixels *s=model(r->source),*d=model(r->destination),*palette=model(r->palette);
        for(unsigned y=0;y<r->height;++y)for(unsigned x=0;x<r->width;++x){
            uint32_t sv=s->data[y*s->width+x],out=0;
            for(unsigned ch=0;ch<3;++ch){unsigned i=(sv>>(ch*8))&255;
                i=(i&~24U)|((i&8)<<1)|((i&16)>>1);
                out|=(palette->data[i]&255)<<(ch*8);}
            unsigned di=y*d->width+x;d->data[di]=(d->data[di]&~r->plane_mask)|(out&r->plane_mask);
        }
        return 0;
    }
    if(op==PS2GS_TEXTURE_SUBMIT){++composite_sampled;if(native_failure){errno=EIO;return -1;}
        const struct ps2gs_texture_submit *r=arg;
        if(model_bpp(model(r->texture))!=model_bpp(model(r->target))){
            struct ps2gs_texture_transfer transfer={.draw=*r,.plane_mask=0xffffff};
            return rop_ioctl(fd,PS2GS_TEXTURE_TRANSFER,&transfer);
        }
        if(r->count>2){const struct ps2gs_texture_triangle *t=(void*)(uintptr_t)r->triangles;
            for(unsigned i=0;i<r->count;i+=2){struct ps2gs_texture_submit draw=*r;draw.count=2;draw.triangles=(uintptr_t)(t+i);
                int result=rop_ioctl(fd,op,&draw);if(result)return result;}
            return 0;}}
    return rop_ioctl(fd,op,arg);
}
static pixman_image_t *reference_image_layout(struct pixels *p,unsigned bpp,unsigned swap,unsigned alpha,unsigned rotate,void **memory)
{
    size_t stride=(p->width*(bpp/8)+3)&~3U;
    unsigned char *bytes=malloc(stride*p->height);assert(bytes);memset(bytes,0,stride*p->height);
    for(unsigned y=0;y<p->height;++y)for(unsigned x=0;x<p->width;++x)
        memcpy(bytes+y*stride+x*(bpp/8),p->data+y*p->width+x,bpp/8);
    *memory=bytes;
    pixman_image_t *result=pixman_image_create_bits(rotate?(alpha?PIXMAN_b8g8r8a8:PIXMAN_b8g8r8x8):bpp==16?(swap?PIXMAN_x1r5g5b5:PIXMAN_x1b5g5r5):
        (alpha?(swap?PIXMAN_a8r8g8b8:PIXMAN_a8b8g8r8):(swap?PIXMAN_x8r8g8b8:PIXMAN_x8b8g8r8)),
        p->width,p->height,(uint32_t*)bytes,stride);assert(result);return result;
}
static pixman_image_t *reference_image(struct pixels *p,unsigned bpp,unsigned swap,unsigned alpha,void **memory)
{return reference_image_layout(p,bpp,swap,alpha,0,memory);}
static pixman_image_t *reference_alpha_map(const struct ps2_picture_alpha_map *m,void **memory)
{
    struct pixels *p=model(m->surface);
    if(!m->alpha_only)return reference_image_layout(p,model_bpp(p),m->swap,m->has_alpha,m->rotate,memory);
    unsigned depth=m->alpha_bits?m->alpha_bits:8;
    unsigned stride=depth==1?((m->width+31)/32)*4:(m->width+3)&~3U;
    unsigned char *bytes=calloc(stride,m->height);assert(bytes);
    for(unsigned y=0;y<m->height;++y)for(unsigned x=0;x<m->width;++x)
        if(depth==1)bytes[y*stride+x/8]|=((p->data[y*p->width+x]>>31)&1)<<(x&7);
        else bytes[y*stride+x]=p->data[y*p->width+x]>>(depth==4?28:24);
    *memory=bytes;
    pixman_image_t *image=pixman_image_create_bits(depth==1?PIXMAN_a1:depth==4?PIXMAN_x4a4:PIXMAN_a8,
        m->width,m->height,(uint32_t*)bytes,stride);
    assert(image);return image;
}
static struct ps2_picture_sample identity(unsigned repeat)
{
    return (struct ps2_picture_sample){.transformed=1,.repeat=repeat,
        .matrix={{65536,0,0},{0,65536,0},{0,0,65536}}};
}
static void configure(pixman_image_t *reference_image,const struct ps2_picture_sample *s)
{
    pixman_transform_t transform;memcpy(transform.matrix,s->matrix,sizeof(s->matrix));
    if(s->transformed)assert(pixman_image_set_transform(reference_image,&transform));
    pixman_image_set_repeat(reference_image,(pixman_repeat_t)s->repeat);
    assert(pixman_image_set_filter(reference_image,s->convolution?PIXMAN_FILTER_CONVOLUTION:
        s->bilinear?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,s->filter_params,s->filter_nparams));
}
static unsigned char composite_mask_bits[4*17];
static struct ps2_picture_bitmap make_mask(unsigned repeat)
{
    memset(composite_mask_bits,0,sizeof(composite_mask_bits));
    for(unsigned y=0;y<17;++y)for(unsigned x=0;x<21;++x)
        if((x/3+y/2)%3)composite_mask_bits[y*4+x/8]|=1U<<(x%8);
    return (struct ps2_picture_bitmap){.bits=composite_mask_bits,.bytes=sizeof(composite_mask_bits),.stride=4,
        .width=21,.height=17,.x=-3,.y=2,.sample=identity(repeat)};
}
static void composite_reference(uint32_t sh,uint32_t dh,const struct ps2_pipe_copy_box *box,
    const struct ps2_picture_sample *sample,const struct ps2_picture_bitmap *mask,unsigned over)
{
    struct pixels *s=model(sh),*d=model(dh);unsigned bpp=model_bpp(d),cpp=bpp/8;
    void *source_bytes,*dest_bytes;
    unsigned sbpp=model_bpp(s);
    pixman_image_t *src=reference_image_layout(s,sbpp,sample->source_swap,sample->source_argb,sample->source_rotate,&source_bytes),
        *dst=reference_image_layout(d,bpp,sample->destination_swap,sample->destination_argb,sample->destination_rotate,&dest_bytes),*mi=NULL;
    if(sample->has_alpha){
        assert(sbpp==32);unsigned stride=(s->width*4+3)&~3U;
        for(unsigned y=0;y<s->height;++y)for(unsigned x=0;x<s->width;++x){uint32_t *p=(uint32_t*)((unsigned char*)source_bytes+y*stride+x*4);
            *p=(*p&0xffffff)|(sample->alpha<<24);}
        pixman_image_unref(src);src=pixman_image_create_bits(sample->source_swap?PIXMAN_a8r8g8b8:PIXMAN_a8b8g8r8,
            s->width,s->height,source_bytes,stride);assert(src);
    }
    if(sample->source_solid){pixman_color_t c={.red=sample->solid_rgba[0],.green=sample->solid_rgba[1],
        .blue=sample->solid_rgba[2],.alpha=sample->solid_rgba[3]};
        pixman_image_unref(src);src=pixman_image_create_solid_fill(&c);assert(src);}
    configure(src,sample);
    void *mask_memory=NULL;
    if(mask){
        if(mask->sample.source_solid){pixman_color_t c={.red=mask->sample.solid_rgba[0],.green=mask->sample.solid_rgba[1],
            .blue=mask->sample.solid_rgba[2],.alpha=mask->sample.solid_rgba[3]};
            mi=pixman_image_create_solid_fill(&c);
        }else if(mask->has_surface){struct pixels *m=model(mask->surface);
            mi=reference_image_layout(m,model_bpp(m),mask->sample.source_swap,mask->sample.source_argb,mask->sample.source_rotate,&mask_memory);
        }else mi=pixman_image_create_bits(mask->bits_per_pixel==8?(mask->alpha_bits==4?PIXMAN_x4a4:PIXMAN_a8):PIXMAN_a1,mask->width,mask->height,(uint32_t*)mask->bits,mask->stride);
        assert(mi);configure(mi,&mask->sample);pixman_image_set_component_alpha(mi,mask->component_alpha);
    }
    const struct ps2_picture_alpha_map *maps[]={&sample->source_alpha_map,
        mask?&mask->sample.source_alpha_map:NULL,&sample->destination_alpha_map};
    pixman_image_t *parents[]={src,mi,dst},*map_images[3]={0};void *map_bytes[3]={0};
    for(unsigned i=0;i<3;++i)if(maps[i] && maps[i]->enabled){
        map_images[i]=reference_alpha_map(maps[i],map_bytes+i);
        pixman_image_set_alpha_map(parents[i],map_images[i],maps[i]->x,maps[i]->y);
    }
    static const pixman_op_t operations[]={PIXMAN_OP_SRC,PIXMAN_OP_OVER,PIXMAN_OP_CLEAR,PIXMAN_OP_DST,
        PIXMAN_OP_IN_REVERSE,PIXMAN_OP_OUT_REVERSE,PIXMAN_OP_ADD,PIXMAN_OP_OVER_REVERSE,
        PIXMAN_OP_IN,PIXMAN_OP_OUT,PIXMAN_OP_ATOP,PIXMAN_OP_ATOP_REVERSE,PIXMAN_OP_XOR,PIXMAN_OP_SATURATE,PIXMAN_OP_SCREEN,PIXMAN_OP_EXCLUSION,PIXMAN_OP_MULTIPLY,PIXMAN_OP_DARKEN,PIXMAN_OP_LIGHTEN,PIXMAN_OP_DIFFERENCE,PIXMAN_OP_OVERLAY,PIXMAN_OP_HARD_LIGHT,PIXMAN_OP_DISJOINT_CLEAR,PIXMAN_OP_DISJOINT_SRC,PIXMAN_OP_DISJOINT_DST,PIXMAN_OP_CONJOINT_CLEAR,PIXMAN_OP_CONJOINT_SRC,PIXMAN_OP_CONJOINT_DST,PIXMAN_OP_DISJOINT_OVER_REVERSE,PIXMAN_OP_DISJOINT_OVER,PIXMAN_OP_DISJOINT_IN,PIXMAN_OP_DISJOINT_IN_REVERSE,PIXMAN_OP_DISJOINT_OUT,PIXMAN_OP_DISJOINT_OUT_REVERSE,PIXMAN_OP_DISJOINT_ATOP,PIXMAN_OP_DISJOINT_ATOP_REVERSE,PIXMAN_OP_DISJOINT_XOR,PIXMAN_OP_CONJOINT_OVER,PIXMAN_OP_CONJOINT_OVER_REVERSE,PIXMAN_OP_CONJOINT_IN,PIXMAN_OP_CONJOINT_IN_REVERSE,PIXMAN_OP_CONJOINT_OUT,PIXMAN_OP_CONJOINT_OUT_REVERSE,PIXMAN_OP_CONJOINT_ATOP,PIXMAN_OP_CONJOINT_ATOP_REVERSE,PIXMAN_OP_CONJOINT_XOR,PIXMAN_OP_COLOR_DODGE,PIXMAN_OP_COLOR_BURN,PIXMAN_OP_SOFT_LIGHT,PIXMAN_OP_HSL_HUE,PIXMAN_OP_HSL_SATURATION,PIXMAN_OP_HSL_COLOR,PIXMAN_OP_HSL_LUMINOSITY};
    pixman_image_composite32(operations[over],src,mi,dst,box->sx,box->sy,
        mask?mask->x:0,mask?mask->y:0,box->dx,box->dy,box->width,box->height);
    compositor_active=1;
    if(composite_via_exa){
        exa.driver.Composite(&xp,box->sx,box->sy,mask?mask->x:0,mask?mask->y:0,
            box->dx,box->dy,box->width,box->height);compositor_active=0;exa.driver.DoneComposite(&xp);
        sync_alpha_reply();
    }else if(ps2_pipe_composite_rect(ctx,sh,dh,box,sample,mask,over)){
        fprintf(stderr,"composite rejected bpp=%u op=%u repeat=%u bilinear=%u mask=%u errno=%d\n",
            bpp,over,sample->repeat,sample->bilinear,!!mask,errno);
        fprintf(stderr,"source=%u dest=%u matrix=%d,%d,%d / %d,%d,%d / %d,%d,%d\n",sh,dh,
            sample->matrix[0][0],sample->matrix[0][1],sample->matrix[0][2],sample->matrix[1][0],sample->matrix[1][1],sample->matrix[1][2],
            sample->matrix[2][0],sample->matrix[2][1],sample->matrix[2][2]);abort();}
    compositor_active=0;
    unsigned stride=(d->width*cpp+3)&~3U,bits=bpp==16?0x7fff:sample->destination_argb?UINT32_MAX:sample->destination_rotate?0xffffff00:0xffffff;
    for(unsigned y=0;y<d->height;++y)for(unsigned x=0;x<d->width;++x){uint32_t v=0;
        memcpy(&v,(unsigned char*)dest_bytes+y*stride+x*cpp,cpp);
        if((v&bits)!=(d->data[y*d->width+x]&bits)){
            fprintf(stderr,"composite mismatch bpp=%u over=%u repeat=%u mask=%u x=%u y=%u expected=%x actual=%x\n",
                bpp,over,sample->repeat,!!mask,x,y,v,d->data[y*d->width+x]);abort();}}
    if(sample->destination_alpha_map.enabled){
        const struct ps2_picture_alpha_map *m=&sample->destination_alpha_map;
        struct pixels *p=model(m->surface);unsigned mbpp=model_bpp(p),mcpp=m->alpha_only?1:mbpp/8;
        unsigned adepth=m->alpha_bits?m->alpha_bits:8;
        unsigned mstride=m->alpha_only && adepth==1?((m->width+31)/32)*4:(m->width*mcpp+3)&~3U;
        unsigned mbits=m->alpha_only?0xff000000:mbpp==16?0x7fff:m->has_alpha?UINT32_MAX:m->rotate?0xffffff00:0xffffff;
        for(unsigned y=0;y<m->height;++y)for(unsigned x=0;x<m->width;++x){uint32_t wanted=0;
            if(m->alpha_only){
                unsigned char *row=(unsigned char*)map_bytes[2]+y*mstride;
                wanted=(uint32_t)(adepth==1?((row[x/8]>>(x&7))&1)*255:adepth==4?(row[x]&15)*17:row[x])<<24;
            }else memcpy(&wanted,(unsigned char*)map_bytes[2]+y*mstride+x*mcpp,mcpp);
            if((p->data[y*p->width+x]&mbits)!=(wanted&mbits)){
                fprintf(stderr,"alpha map store mismatch op=%u filter=%u,%u x=%u y=%u expected=%08x actual=%08x\n",
                    over,sample->bilinear,sample->convolution,x,y,wanted,p->data[y*p->width+x]);abort();}
        }
    }
    if(mi)pixman_image_unref(mi);
    free(mask_memory);
    pixman_image_unref(dst);pixman_image_unref(src);free(dest_bytes);free(source_bytes);
    for(unsigned i=0;i<3;++i){if(map_images[i])pixman_image_unref(map_images[i]);free(map_bytes[i]);}
}
static void alpha_maps_case(const char *name)
{
    unsigned blend=extended_operation(name);
    unsigned bpp=strstr(name,"555")?16:32;
    rgba_enabled=1;setup_rop(bpp);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    uint32_t maps[3]={0};
    for(unsigned i=0;i<3;++i){assert(!ps2_surface_create(gs,31,23,32,maps+i));
        assert(!ps2_surface_set_depth(gs,maps[i],32));}
    int32_t params[]={2*65536,65536,2*65536,-65536};
    struct ps2_pipe_copy_box box={2,1,5,4,5,3};
    if(ratio_test_operation(blend)){box.width=box.height=1;params[2]=49152;params[3]=16384;}
    struct ps2_picture_bitmap mask={.has_surface=1,.surface=maps[1],.width=31,.height=23,
        .x=4,.y=2,.sample={.repeat=PS2_REPEAT_REFLECT,.source_argb=1}};
    for(unsigned kind=0;kind<3;++kind)for(unsigned filter=0;filter<3;++filter)
    for(unsigned repeat=0;repeat<4;++repeat)for(unsigned op=blend?blend:0;op<=(blend?blend:PS2_COMPOSITE_XOR);++op){
        seed(model(source_handle));seed(model(destination_handle()));
        for(unsigned i=0;i<3;++i)rgba_seed(model(maps[i]));
        struct ps2_picture_sample sample=identity(repeat);
        sample.bilinear=filter==1;sample.convolution=filter==2;
        if(filter==2){sample.filter_params=params;sample.filter_nparams=4;}
        sample.matrix[0][0]=98304;sample.matrix[1][1]=32768;
        sample.matrix[0][2]=-21845;sample.matrix[2][0]=65536/15;
        struct ps2_picture_alpha_map map={.enabled=1,.width=31,.height=23,.has_alpha=kind!=2,
            .alpha_only=kind==1,.x=3,.y=-1};
        map.swap=kind!=1 && strstr(name,"swapped");map.rotate=kind!=1 && strstr(name,"rotated");
        if(strstr(name,"source") || strstr(name,"combined")){sample.source_alpha_map=map;sample.source_alpha_map.surface=maps[0];}
        if(strstr(name,"destination") || strstr(name,"combined")){sample.destination_alpha_map=map;sample.destination_alpha_map.surface=maps[2];}
        mask.sample.source_alpha_map=(struct ps2_picture_alpha_map){0};
        if(strstr(name,"mask") || strstr(name,"combined")){mask.sample.source_alpha_map=map;mask.sample.source_alpha_map.surface=maps[0];}
        mask.sample.bilinear=filter==1;mask.sample.convolution=filter==2;
        mask.sample.filter_params=filter==2?params:NULL;mask.sample.filter_nparams=filter==2?4:0;
        mask.component_alpha=kind==0 && !hsl_private(op);
        composite_reference(source_handle,destination_handle(),&box,&sample,&mask,op);
    }
    for(unsigned i=0;i<3;++i)assert(!ps2_surface_destroy(gs,maps[i]));
    finish_rop();
}
static void saturate_map_case(unsigned role,unsigned bpp)
{
    rgba_enabled=1;setup_rop(bpp);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    if(bpp==32){assert(!ps2_surface_set_depth(gs,source_handle,32));
        assert(!ps2_surface_set_depth(gs,destination_handle(),32));}
    uint32_t maps[3]={0},mh=0;
    for(unsigned i=0;i<3;++i)assert(!ps2_surface_create(gs,31,23,32,maps+i) && !ps2_surface_set_depth(gs,maps[i],32));
    assert(!ps2_surface_create(gs,31,23,32,&mh) && !ps2_surface_set_depth(gs,mh,32));
    const unsigned depths[]={1,4,8};struct ps2_pipe_copy_box box={2,1,5,4,2,1};
    for(unsigned kind=0;kind<3;++kind)for(unsigned filter=0;filter<2;++filter)
    for(unsigned repeat=0;repeat<4;++repeat)for(unsigned component=0;component<2;++component){
        rgba_seed(model(source_handle));rgba_seed(model(destination_handle()));rgba_seed(model(mh));
        if(bpp==16){seed(model(source_handle));seed(model(destination_handle()));}
        for(unsigned m=0;m<3;++m)for(unsigned i=0;i<31*23;++i){
            unsigned n=depths[(kind+m)%3],v=(i*73+m*37+kind*19)&((1U<<n)-1);
            model(maps[m])->data[i]=(uint32_t)(n==1?v*255:n==4?v*17:v)<<24;
        }
        struct ps2_picture_sample sample=identity(repeat);
        sample.source_argb=sample.destination_argb=bpp==32;sample.bilinear=filter;
        sample.matrix[0][0]=98304;sample.matrix[1][1]=32768;sample.matrix[0][2]=-21845;
        sample.matrix[2][0]=65536/15;
        struct ps2_picture_bitmap mask={.has_surface=1,.surface=mh,.width=31,.height=23,.x=4,.y=2,
            .component_alpha=component,.sample={.source_argb=1,.repeat=PS2_REPEAT_REFLECT,.bilinear=filter}};
        struct ps2_picture_alpha_map map={.enabled=1,.width=31,.height=23,.has_alpha=1,.alpha_only=1,.x=3,.y=-1};
        if(role==0 || role==2){sample.source_alpha_map=map;sample.source_alpha_map.surface=maps[0];sample.source_alpha_map.alpha_bits=depths[kind];}
        if(role==1 || role==2){mask.sample.source_alpha_map=map;mask.sample.source_alpha_map.surface=maps[1];mask.sample.source_alpha_map.alpha_bits=depths[(kind+1)%3];}
        if(role>=2){sample.destination_alpha_map=map;sample.destination_alpha_map.surface=maps[2];sample.destination_alpha_map.alpha_bits=depths[(kind+2)%3];}
        composite_reference(source_handle,destination_handle(),&box,&sample,&mask,PS2_COMPOSITE_SATURATE);
    }
    for(unsigned i=0;i<3;++i)assert(!ps2_surface_destroy(gs,maps[i]));
    assert(!ps2_surface_destroy(gs,mh));finish_rop();
}
static void composite_case(const char *name)
{
    unsigned bpp=!strcmp(name,"rgb555")?16:32;setup_rop(bpp);
    struct pixels *s=model(source_handle),*d=model(destination_handle());seed(s);seed(d);
    struct ps2_picture_sample sample=identity(PS2_REPEAT_NONE);
    struct ps2_pipe_copy_box box={-7,-3,5,4,92,67};
    struct ps2_picture_bitmap mask=make_mask(PS2_REPEAT_NORMAL);
    unsigned over=!strcmp(name,"over") || !strcmp(name,"mask-over"),masked=!strncmp(name,"mask-",5);
    if(!strcmp(name,"normal"))sample.repeat=PS2_REPEAT_NORMAL;
    if(!strcmp(name,"pad"))sample.repeat=PS2_REPEAT_PAD;
    if(!strcmp(name,"reflect"))sample.repeat=PS2_REPEAT_REFLECT;
    if(!strcmp(name,"scale")){sample.matrix[0][0]=98304;sample.matrix[1][1]=32768;sample.matrix[0][2]=-21845;}
    if(!strcmp(name,"boundaries")){sample.matrix[0][0]=131072;sample.matrix[1][1]=131072;sample.repeat=PS2_REPEAT_REFLECT;}
    if(!strcmp(name,"rotate")){sample.matrix[0][0]=0;sample.matrix[0][1]=-65536;sample.matrix[0][2]=61*65536;
        sample.matrix[1][0]=65536;sample.matrix[1][1]=0;}
    if(!strcmp(name,"shear")){sample.matrix[0][1]=16384;sample.matrix[1][0]=-21845;sample.repeat=PS2_REPEAT_REFLECT;}
    if(!strcmp(name,"projective")){sample.matrix[2][0]=65536/15;sample.matrix[2][1]=65536/19;sample.repeat=PS2_REPEAT_PAD;}
    if(!strcmp(name,"negative-q")){sample.matrix[0][0]=-65536;sample.matrix[1][1]=-65536;sample.matrix[2][2]=-65536;}
    if(!strcmp(name,"mask-transform")){mask.sample.matrix[0][0]=32768;mask.sample.matrix[0][1]=21845;mask.sample.repeat=PS2_REPEAT_REFLECT;}
    uint32_t sh=source_handle;
    if(!strcmp(name,"alias"))sh=destination_handle();
    if(!strcmp(name,"front")){sh=0;seed(model(0));box.sx=95;box.sy=105;}
    composite_reference(sh,destination_handle(),&box,&sample,masked?&mask:NULL,over);
    finish_rop();
}
static void guards(int noop,int allocation)
{
    setup_rop(32);struct ps2_render_client *r=ps2_pipe_context_render(ctx);
    assert(!ps2_render_fill(r,0,0,1,1,0));unsigned before=attempts,copies=calls;
    struct ps2_pipe_copy_box box={0,0,3,2,19,11};struct ps2_picture_sample sample=identity(0);
    if(noop){box.width=0;assert(!ps2_pipe_composite_rect(ctx,source_handle,0,&box,&sample,NULL,0));}
    else if(allocation){malloc_fail=1;mal_calls=0;
        assert(ps2_pipe_composite_rect(ctx,source_handle,0,&box,&sample,NULL,0)<0 && errno==ENOMEM);malloc_fail=0;}
    else{for(unsigned i=0;i<7;++i){struct ps2_pipe_copy_box bad=box;struct ps2_picture_sample state=sample;
        switch(i){case 0:bad.dx=-1;break;case 1:bad.width=INT32_MAX;break;case 2:state.repeat=4;break;
            case 3:state.transformed=2;break;case 4:bad.sx=INT32_MAX;break;
            case 5:state.source_swap=2;break;case 6:state.destination_swap=2;break;}
        assert(ps2_pipe_composite_rect(ctx,source_handle,0,&bad,&state,NULL,0)<0 && errno==EINVAL);}}
    assert(attempts==before && calls==copies && r->count==1);finish_rop();
}
static void failed(void)
{
    setup_rop(32);struct ps2_pipe_copy_box box={0,0,3,2,19,11};struct ps2_picture_sample sample=identity(0);
    native_failure=1;assert(ps2_pipe_composite_rect(ctx,source_handle,destination_handle(),&box,&sample,NULL,0)<0 && errno==EIO);
    unsigned before=composite_sampled;native_failure=0;
    assert(ps2_pipe_composite_rect(ctx,source_handle,destination_handle(),&box,&sample,NULL,0)<0 && composite_sampled==before);
    polygon_finish();
}
static void composite_exa_case(const char *name)
{
    setup_rop(32);struct ps2_exa_context_bridge bridge={0};struct ps2_texture_exa textures={0};struct ps2_blit_exa compositor={0};
    assert(ps2_exa_context_bridge_prepare(&xs,&bridge,&exa,ctx));
    assert(ps2_texture_exa_prepare(&xs,&textures,&exa,ps2_pipe_context_render(ctx)));
    assert(ps2_blit_exa_prepare(&xs,&compositor,&exa,ctx));
    PixmapRec src={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=24,.bitsPerPixel=32,.width=83,.height=61},.devPrivates=(void*)&source_handle};
    PictureRec a={.pDrawable=&src.drawable,.format=PICT_x8b8g8r8,.filter=PictFilterNearest,.repeat=1,.repeatType=RepeatNormal};
    PictureRec b={.pDrawable=&xp.drawable,.format=PICT_x8b8g8r8,.filter=PictFilterNearest};
    struct ps2_picture_sample state=identity(PS2_REPEAT_NORMAL);
    if(!strcmp(name,"exa-swapped-source")){a.format=PICT_x8r8g8b8;state.source_swap=1;}
    if(!strcmp(name,"exa-swapped-destination")){b.format=PICT_x8r8g8b8;state.destination_swap=1;}
    PictTransform transform;
    if(strcmp(name,"exa")){state.matrix[0][0]=131072;state.matrix[0][1]=16384;
        memcpy(transform.matrix,state.matrix,sizeof(state.matrix));a.transform=&transform;}
    struct ps2_picture_bitmap mask=make_mask(PS2_REPEAT_REFLECT);
    PixmapRec mp={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=1,.bitsPerPixel=1,.width=21,.height=17}};
    PictureRec m={.pDrawable=&mp.drawable,.format=PICT_a1,.filter=PictFilterNearest,.repeat=1,.repeatType=RepeatReflect};
    Bool masked=!strcmp(name,"exa-mask");
    if(masked){int pitch;mp.devPrivates=exa.driver.CreatePixmap2(&xs,21,17,1,0,1,&pitch);assert(mp.devPrivates);mp.devKind=pitch;
        struct ps2_mono_source bits;assert(ps2_surface_exa_mono_source(&mp,&bits)==1);assert(!mask.msb_first);
        memcpy((void*)bits.bits,mask.bits,mask.bytes);}
    seed(model(source_handle));seed(model(destination_handle()));
    assert(exa.driver.CheckComposite(PictOpOver,&a,masked?&m:NULL,&b));
    assert(exa.driver.PrepareComposite(PictOpOver,&a,masked?&m:NULL,&b,&src,masked?&mp:NULL,&xp));
    struct ps2_pipe_copy_box box={-7,-3,5,4,92,67};composite_via_exa=1;
    composite_reference(source_handle,destination_handle(),&box,&state,masked?&mask:NULL,1);
    if(masked)exa.driver.DestroyPixmap(&xs,mp.devPrivates);
    ps2_blit_exa_cleanup(&xs,&compositor);ps2_exa_context_bridge_cleanup(&bridge);finish_rop();
}
static void alpha_case(const char *name)
{
    unsigned bpp=strstr(name,"555")?16:32,over=strstr(name,"over")!=NULL;
    setup_rop(bpp);seed(model(source_handle));seed(model(destination_handle()));
    unsigned char bits[64*4];
    struct ps2_picture_bitmap mask={.bits=bits,.bytes=sizeof(bits),.stride=64,
        .width=64,.height=4,.bits_per_pixel=8,.sample=identity(PS2_REPEAT_NORMAL)};
    struct ps2_picture_sample state=identity(PS2_REPEAT_REFLECT);
    struct ps2_pipe_copy_box box={7,3,5,4,64,4};
    for(unsigned page=0;page<4;++page){
        for(unsigned i=0;i<sizeof(bits);++i)bits[i]=strstr(name,"ramps")?(i+page*64)&255:page*64+31;
        composite_reference(source_handle,destination_handle(),&box,&state,&mask,over);
    }
    finish_rop();
}
static void solid_case(const char *name)
{
    unsigned bpp=strstr(name,"555")?16:32;
    setup_rop(bpp);struct ps2_exa_context_bridge bridge={0};struct ps2_texture_exa textures={0};struct ps2_blit_exa compositor={0};
    assert(ps2_exa_context_bridge_prepare(&xs,&bridge,&exa,ctx));
    assert(ps2_texture_exa_prepare(&xs,&textures,&exa,ps2_pipe_context_render(ctx)));
    assert(ps2_blit_exa_prepare(&xs,&compositor,&exa,ctx));
    SourcePict source={.solidFill={.type=SourcePictTypeSolidFill,.color=0x81392765}};
    PictureRec a={.pSourcePict=&source,.format=PICT_a8r8g8b8,.filter=PictFilterNearest};
    PictureRec d={.pDrawable=&xp.drawable,.format=bpp==16?PICT_x1b5g5r5:PICT_x8b8g8r8,.filter=PictFilterNearest};
    struct ps2_picture_sample state={.repeat=PS2_REPEAT_NORMAL,.has_alpha=1,.alpha=129};
    unsigned over=strstr(name,"over")!=NULL;
    seed(model(destination_handle()));
    assert(exa.driver.CheckComposite(over?PictOpOver:PictOpSrc,&a,NULL,&d));
    assert(exa.driver.PrepareComposite(over?PictOpOver:PictOpSrc,&a,NULL,&d,NULL,NULL,&xp));
    struct ps2_pipe_copy_box box={-7,-3,5,4,92,67};composite_via_exa=1;
    composite_reference(compositor.solid,destination_handle(),&box,&state,NULL,over);
    assert(!compositor.solid && !compositor.prepared);
    ps2_blit_exa_cleanup(&xs,&compositor);ps2_exa_context_bridge_cleanup(&bridge);finish_rop();
}
static void porter_duff(const char *name)
{
    unsigned bpp=strstr(name,"555")?16:32;setup_rop(bpp);seed(model(source_handle));
    struct ps2_picture_sample state=identity(PS2_REPEAT_NONE);
    struct ps2_pipe_copy_box box={-7,-3,5,4,92,67};unsigned char bits[64*4];
    for(unsigned i=0;i<sizeof(bits);++i)bits[i]=i;
    struct ps2_picture_bitmap mask={.bits=bits,.bytes=sizeof(bits),.stride=64,.width=64,.height=4,
        .bits_per_pixel=8,.sample=identity(PS2_REPEAT_REFLECT)};
    for(unsigned op=0;op<=PS2_COMPOSITE_ADD;++op){seed(model(destination_handle()));
        composite_reference(source_handle,destination_handle(),&box,&state,&mask,op);}
    finish_rop();
}
static void bilinear_mask_case(const char *name)
{
    unsigned bpp=strstr(name,"555")?16:32;setup_rop(bpp);seed(model(source_handle));
    unsigned char bits[8*5];
    for(unsigned i=0;i<sizeof(bits);++i)bits[i]=i*73;
    struct ps2_picture_bitmap mask={.bits=bits,.bytes=sizeof(bits),.stride=8,.width=7,.height=5,
        .bits_per_pixel=strstr(name,"a1")?1:8,.x=-3,.y=-2,.sample=identity(PS2_REPEAT_NONE)};
    mask.sample.bilinear=1;mask.sample.matrix[0][0]=98304;mask.sample.matrix[1][0]=21845;
    mask.sample.matrix[1][1]=32768;mask.sample.matrix[0][2]=-21845;
    struct ps2_picture_sample state=identity(PS2_REPEAT_REFLECT);
    struct ps2_pipe_copy_box box={7,3,5,4,31,17};
    for(unsigned projective=0;projective<2;++projective)for(unsigned repeat=0;repeat<4;++repeat)
    for(unsigned op=0;op<=PS2_COMPOSITE_ADD;++op){
        mask.sample.repeat=repeat;mask.sample.matrix[2][0]=projective?65536/17:0;
        mask.sample.matrix[2][1]=projective?65536/19:0;
        seed(model(destination_handle()));composite_reference(source_handle,destination_handle(),&box,&state,&mask,op);
    }
    finish_rop();
}
static void colour_filter_case(const char *name)
{
    unsigned bpp=strstr(name,"555")?16:32;setup_rop(bpp);seed(model(source_handle));
    struct ps2_picture_sample state=identity(PS2_REPEAT_NONE);state.bilinear=1;
    state.matrix[0][0]=98304;state.matrix[1][0]=21845;state.matrix[1][1]=32768;state.matrix[0][2]=-21845;
    unsigned char bits[8*5];for(unsigned i=0;i<sizeof(bits);++i)bits[i]=i*73;
    struct ps2_picture_bitmap mask={.bits=bits,.bytes=sizeof(bits),.stride=8,.width=7,.height=5,
        .bits_per_pixel=strstr(name,"a1")?1:8,.x=-1,.y=1,.sample=identity(PS2_REPEAT_REFLECT)};
    mask.sample.bilinear=!!strstr(name,"mask-linear");mask.sample.matrix[0][2]=21845;
    struct ps2_pipe_copy_box box={-3,-2,5,4,13,7};
    for(unsigned projective=0;projective<2;++projective)for(unsigned repeat=0;repeat<4;++repeat){
        state.repeat=repeat;state.matrix[2][0]=projective?65536/17:0;state.matrix[2][1]=projective?65536/19:0;
        for(unsigned op=0;op<=PS2_COMPOSITE_ADD;++op){
            seed(model(destination_handle()));composite_reference(source_handle,destination_handle(),&box,&state,
                strstr(name,"mask")?&mask:NULL,op);
        }
    }
    /* Identity, reversed sampling, all-zero Q and alias/front snapshots. */
    for(unsigned transform=0;transform<4;++transform){
        state=identity(PS2_REPEAT_REFLECT);state.bilinear=1;
        if(transform==1){state.matrix[0][0]=-65536;state.matrix[0][2]=20*65536;}
        if(transform==2)state.matrix[2][2]=0;
        if(transform==3){state.matrix[0][2]=511;state.matrix[1][2]=65025;}
        seed(model(destination_handle()));
        composite_reference(source_handle,destination_handle(),&box,&state,NULL,PS2_COMPOSITE_OVER);
        seed(model(destination_handle()));
        composite_reference(destination_handle(),destination_handle(),&box,&state,NULL,PS2_COMPOSITE_OVER);
    }
    seed(model(0));seed(model(destination_handle()));
    composite_reference(0,destination_handle(),&box,&state,NULL,PS2_COMPOSITE_SRC);
    finish_rop();
}
static void convolution_case(const char *name)
{
    unsigned bpp=strstr(name,"555")?16:32;setup_rop(bpp);
    struct pixels *s=model(source_handle);
    /* All channel ranges, including saturated sums and sign differences. */
    for(unsigned y=0;y<s->height;++y)for(unsigned x=0;x<s->width;++x){
        uint32_t c=(x*73+y*37)&255,v=c|((255-c)<<8)|(((x+y)&1?255:0)<<16);
        s->data[y*s->width+x]=bpp==16?((v>>3)&31)|(((v>>11)&31)<<5)|(((v>>19)&31)<<10):v;
    }
    int32_t kernels[][11]={
        {65536,65536,65536},
        {2*65536,2*65536,16384,16384,16384,16384},
        {3*65536,65536,-32768,131072,-32768},
        {3*65536,3*65536,0,-65536,0,-65536,5*65536,-65536,0,-65536,0},
        {65536,65536,INT32_MAX},
        {65536,65536,INT32_MIN},
        {65536,65536,0},
        {2*65536,65536,511,65025}
    };
    unsigned char bits[8*5];for(unsigned i=0;i<sizeof(bits);++i)bits[i]=i*73;
    struct ps2_picture_bitmap mask={.bits=bits,.bytes=sizeof(bits),.stride=8,.width=7,.height=5,
        .bits_per_pixel=strstr(name,"a1")?1:8,.x=-1,.y=1,.sample=identity(PS2_REPEAT_REFLECT)};
    struct ps2_pipe_copy_box box={-1,-1,5,4,5,3};
    for(unsigned kernel=0;kernel<sizeof(kernels)/sizeof(*kernels);++kernel)
    for(unsigned repeat=0;repeat<4;++repeat)for(unsigned over=0;over<2;++over){
        struct ps2_picture_sample state=identity(repeat);
        state.convolution=1;state.filter_params=kernels[kernel];
        state.filter_nparams=2+(kernels[kernel][0]/65536)*(kernels[kernel][1]/65536);
        if(kernel&1){state.matrix[0][0]=98304;state.matrix[1][0]=21845;state.matrix[0][2]=-21845;}
        if(kernel&2){state.matrix[2][0]=65536/17;state.matrix[2][1]=65536/19;}
        if(strstr(name,"mask")){mask.sample.convolution=1;mask.sample.filter_params=state.filter_params;
            mask.sample.filter_nparams=state.filter_nparams;}
        seed(model(destination_handle()));composite_reference(source_handle,destination_handle(),&box,&state,
            strstr(name,"mask")?&mask:NULL,over);
    }
    finish_rop();
}
static void filter_guards(void)
{
    setup_rop(32);struct ps2_render_client *r=ps2_pipe_context_render(ctx);
    assert(!ps2_render_fill(r,0,0,1,1,0));unsigned before=attempts,copies=calls;
    struct ps2_pipe_copy_box box={0,0,3,2,19,11};int32_t params[]={65536,65536,65536};
    struct ps2_picture_sample valid=identity(0);valid.convolution=1;valid.filter_params=params;valid.filter_nparams=3;
    for(unsigned i=0;i<8;++i){struct ps2_picture_sample bad=valid;int32_t local[3];memcpy(local,params,sizeof(local));
        bad.filter_params=local;
        switch(i){case 0:bad.bilinear=1;break;case 1:bad.filter_params=NULL;break;case 2:bad.filter_nparams=2;break;
        case 3:local[0]=0;break;case 4:local[1]=65537;break;case 5:local[0]=2049*65536;break;
        case 6:local[1]=131072;break;case 7:bad.convolution=0;break;}
        assert(ps2_pipe_composite_rect(ctx,source_handle,0,&box,&bad,NULL,0)<0 && errno==EINVAL);}
    assert(attempts==before && calls==copies && r->count==1);
    valid.bilinear=1;valid.convolution=0;valid.filter_nparams=0;valid.filter_params=NULL;
    valid.matrix[2][2]=0;
    /* Q=0 is a transparent sample, including nearest and bilinear masks. */
    struct ps2_picture_sample nearest=valid;nearest.bilinear=0;
    seed(model(destination_handle()));composite_reference(source_handle,destination_handle(),&box,&nearest,NULL,PS2_COMPOSITE_OVER);
    assert(attempts>before || calls>copies);
    finish_rop();
}
static void formats_case(const char *name)
{
    unsigned sbpp=!strcmp(name,"formats-555") || !strcmp(name,"formats-555-32")?16:32;
    unsigned dbpp=!strcmp(name,"formats-555") || !strcmp(name,"formats-32-555")?16:32;
    setup_rop(dbpp);
    struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    if(sbpp!=dbpp){assert(!ps2_surface_destroy(gs,source_handle));assert(!ps2_surface_create(gs,83,61,sbpp,&source_handle));}
    seed(model(source_handle));
    unsigned char coverage_bits[8*5];for(unsigned i=0;i<sizeof(coverage_bits);++i)coverage_bits[i]=i*73;
    struct ps2_picture_bitmap mask={.bits=coverage_bits,.bytes=sizeof(coverage_bits),.stride=8,
        .width=7,.height=5,.bits_per_pixel=8,.x=-1,.y=1,.sample={.repeat=PS2_REPEAT_REFLECT}};
    int32_t coefficients[]={2*65536,65536,2*65536,-65536};
    struct ps2_pipe_copy_box box={-1,-1,2,1,3,3};
    for(unsigned swapped=0;swapped<4;++swapped)for(unsigned filter=0;filter<3;++filter)
    for(unsigned repeat=0;repeat<4;++repeat)for(unsigned operation=0;operation<=PS2_COMPOSITE_ADD;++operation){
        struct ps2_picture_sample sample=identity(repeat);sample.source_swap=swapped&1;sample.destination_swap=swapped>>1;
        sample.matrix[0][0]=98304;sample.matrix[1][1]=32768;sample.matrix[0][2]=-21845;
        sample.matrix[2][0]=65536/15;sample.matrix[2][1]=65536/19;
        sample.bilinear=filter==1;sample.convolution=filter==2;
        if(filter==2){sample.filter_params=coefficients;sample.filter_nparams=4;}
        seed(model(destination_handle()));composite_reference(source_handle,destination_handle(),&box,&sample,&mask,operation);
    }
    if(sbpp==dbpp){
        struct ps2_picture_sample sample=identity(PS2_REPEAT_REFLECT);sample.source_swap=1;sample.destination_swap=0;
        seed(model(destination_handle()));box=(struct ps2_pipe_copy_box){2,1,3,2,71,3};
        composite_reference(destination_handle(),destination_handle(),&box,&sample,NULL,PS2_COMPOSITE_SRC);
        seed(model(0));box.sx=95;box.sy=105;
        composite_reference(0,destination_handle(),&box,&sample,NULL,PS2_COMPOSITE_SRC);
    }
    finish_rop();
}
static unsigned mul255(unsigned a,unsigned b){return (a*b+127)/255;}
static void rgba_seed(struct pixels *p)
{
    for(unsigned y=0;y<p->height;++y)for(unsigned x=0;x<p->width;++x){
        unsigned i=y*p->width+x,a=(i*73+29)&255;
        p->data[i]=(a<<24)|mul255((i*17+191)&255,a)|(mul255((i*37+13)&255,a)<<8)|
            (mul255((i*53+127)&255,a)<<16);
    }
}
static void exclusion_bytes_case(unsigned guards_only)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    struct ps2_filter_workspace f={0};assert(!ps2_filter_reserve(&f,gs,3));
    uint32_t h[6],palette,small,word;
    for(unsigned i=0;i<6;++i)assert(!ps2_surface_create(gs,64,32,32,h+i));
    assert(!ps2_surface_create(gs,16,16,32,&palette));
    assert(!ps2_surface_create(gs,1,1,32,&small));assert(!ps2_surface_create(gs,64,32,16,&word));
    const uint32_t work[]={h[3],h[4],h[5]};
    if(guards_only){
        unsigned guards=0;
#define REJECT_EXCLUSION(call) do{unsigned before=attempts;assert((call)==-1 && before==attempts);++guards;}while(0)
        REJECT_EXCLUSION(ps2_native_exclusion(NULL,h[0],h[1],h[2],work,palette,1,1));
        REJECT_EXCLUSION(ps2_native_exclusion(&f,h[0],h[1],h[2],NULL,palette,1,1));
        REJECT_EXCLUSION(ps2_native_exclusion(&f,0,h[1],h[2],work,palette,1,1));
        REJECT_EXCLUSION(ps2_native_exclusion(&f,h[0],0,h[2],work,palette,1,1));
        REJECT_EXCLUSION(ps2_native_exclusion(&f,h[0],h[1],0,work,palette,1,1));
        REJECT_EXCLUSION(ps2_native_exclusion(&f,h[0],h[1],h[2],work,0,1,1));
        REJECT_EXCLUSION(ps2_native_exclusion(&f,h[0],h[1],h[2],work,palette,0,1));
        REJECT_EXCLUSION(ps2_native_exclusion(&f,h[0],h[1],h[2],work,palette,1,0));
        REJECT_EXCLUSION(ps2_native_exclusion(&f,h[0],h[1],h[2],work,palette,65,1));
        REJECT_EXCLUSION(ps2_native_exclusion(&f,h[0],h[1],h[2],work,palette,1,33));
        REJECT_EXCLUSION(ps2_native_exclusion(&f,small,h[1],h[2],work,palette,1,1));
        REJECT_EXCLUSION(ps2_native_exclusion(&f,word,h[1],h[2],work,palette,1,1));
        struct ps2_filter_workspace bad=f;bad.bytes=2;
        REJECT_EXCLUSION(ps2_native_exclusion(&bad,h[0],h[1],h[2],work,palette,1,1));
        uint32_t features=gs->caps.features;gs->caps.features&=~PS2GS_HW_RGBA_TRANSFER;
        REJECT_EXCLUSION(ps2_native_exclusion(&f,h[0],h[1],h[2],work,palette,1,1));gs->caps.features=features;
        const uint32_t forbidden[]={h[0],h[1],h[2],palette,f.tap,f.term,f.sum[0],f.sum[1],f.sum[2],f.saturated,f.carry,small,word};
        for(unsigned i=0;i<sizeof(forbidden)/sizeof(*forbidden);++i){
            uint32_t invalid[]={work[0],work[1],forbidden[i]};
            REJECT_EXCLUSION(ps2_native_exclusion(&f,h[0],h[1],h[2],invalid,palette,1,1));
        }
        uint32_t duplicate[]={work[0],work[0],work[2]};
        REJECT_EXCLUSION(ps2_native_exclusion(&f,h[0],h[1],h[2],duplicate,palette,1,1));
        assert(guards==28);
#undef REJECT_EXCLUSION
    }else{
        unsigned checks=0,rounding_counterexamples=0;
        for(unsigned alias=0;alias<3;++alias)for(unsigned base=0;base<65536;base+=64*32){
            uint32_t expected[64*32],inputs[2][64*32];uint32_t output=alias?h[alias-1]:h[2];
            for(unsigned i=0;i<64*32;++i){unsigned a=(base+i)>>8,b=(base+i)&255;
                inputs[0][i]=0xa1000000U|a|((255-a)<<8)|(a<<16);
                inputs[1][i]=0xb2000000U|b|(b<<8)|((255-b)<<16);
                expected[i]=alias?inputs[alias-1][i]&0xff000000U:0xc3000000U;
                for(unsigned c=0;c<3;++c){unsigned s=(inputs[0][i]>>(8*c))&255,d=(inputs[1][i]>>(8*c))&255;
                    unsigned wanted=s+d-(2*s*d+127)/255;
                    expected[i]|=wanted<<(8*c);
                    rounding_counterexamples+=wanted!=(s*(255-d)+127)/255+(d*(255-s)+127)/255;
                }
                model(h[0])->data[i]=inputs[0][i];model(h[1])->data[i]=inputs[1][i];model(h[2])->data[i]=0xc3456789U;
            }
            compositor_active=1;assert(!ps2_native_exclusion(&f,h[0],h[1],output,work,palette,64,32));compositor_active=0;
            for(unsigned i=0;i<64*32;++i){assert(model(output)->data[i]==expected[i]);
                for(unsigned j=0;j<2;++j)if(h[j]!=output)assert(model(h[j])->data[i]==inputs[j][i]);
                checks+=3;}
        }
        /* A partial page must preserve every pixel outside its rectangle. */
        for(unsigned i=0;i<64*32;++i){model(h[0])->data[i]=0xa17f80ffU;model(h[1])->data[i]=0xb280ff01U;model(h[2])->data[i]=0xc3456789U;}
        compositor_active=1;assert(!ps2_native_exclusion(&f,h[0],h[1],h[2],work,palette,13,7));compositor_active=0;
        for(unsigned y=0;y<32;++y)for(unsigned x=0;x<64;++x)if(x>=13 || y>=7)assert(model(h[2])->data[y*64+x]==0xc3456789U);
        assert(checks==589824 && rounding_counterexamples);
        printf("PS2 Exclusion exhaustive channel_checks=%u separate-rounding-counterexamples=%u PASS\n",checks,rounding_counterexamples);
    }
    assert(!ps2_filter_release(&f));for(unsigned i=0;i<6;++i)assert(!ps2_surface_destroy(gs,h[i]));
    assert(!ps2_surface_destroy(gs,palette) && !ps2_surface_destroy(gs,small) && !ps2_surface_destroy(gs,word));finish_rop();
}
static void order_blends_bytes_case(unsigned guards_only)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    struct ps2_filter_workspace f={0};assert(!ps2_filter_reserve(&f,gs,3));
    uint32_t h[9],palette,small,word;
    for(unsigned i=0;i<9;++i)assert(!ps2_surface_create(gs,64,32,32,h+i));
    assert(!ps2_surface_create(gs,16,16,32,&palette));
    assert(!ps2_surface_create(gs,1,1,32,&small) && !ps2_surface_create(gs,64,32,16,&word));
    const uint32_t work[]={h[5],h[6],h[7],h[8]};
    if(guards_only){
        unsigned guards=0;
#define REJECT_ORDER(call) do{unsigned before=attempts;assert((call)==-1 && before==attempts);++guards;}while(0)
#define ORDER(fs,inputs,out,scratch,p,w,ht,mode) ps2_native_order_blend(fs,(inputs)[0],(inputs)[1],(inputs)[2],(inputs)[3],out,scratch,p,w,ht,mode)
        REJECT_ORDER(ORDER(NULL,h,h[4],work,palette,1,1,0));
        REJECT_ORDER(ORDER(&f,h,h[4],NULL,palette,1,1,0));
        REJECT_ORDER(ORDER(&f,h,h[4],work,0,1,1,0));
        REJECT_ORDER(ORDER(&f,h,h[4],work,palette,0,1,0));
        REJECT_ORDER(ORDER(&f,h,h[4],work,palette,1,0,0));
        REJECT_ORDER(ORDER(&f,h,h[4],work,palette,65,1,0));
        REJECT_ORDER(ORDER(&f,h,h[4],work,palette,1,33,0));
        REJECT_ORDER(ORDER(&f,h,h[4],work,palette,1,1,3));
        for(unsigned role=0;role<4;++role)for(unsigned invalid=0;invalid<4;++invalid){
            uint32_t in[]={h[0],h[1],h[2],h[3]};in[role]=invalid==0?0:invalid==1?small:invalid==2?word:f.sum[2];
            REJECT_ORDER(ORDER(&f,in,h[4],work,palette,1,1,0));
        }
        REJECT_ORDER(ORDER(&f,h,0,work,palette,1,1,0));
        REJECT_ORDER(ORDER(&f,h,word,work,palette,1,1,0));
        REJECT_ORDER(ORDER(&f,h,small,work,palette,2,1,0));
        REJECT_ORDER(ORDER(&f,h,palette,work,palette,1,1,0));
        REJECT_ORDER(ps2_native_multiply(&f,h[0],h[1],palette,palette,1,1));
        REJECT_ORDER(ps2_native_product(&f,h[0],h[1],h[4],palette,palette,1,1));
        REJECT_ORDER(ps2_native_product(&f,h[0],h[1],palette,h[4],palette,1,1));
        REJECT_ORDER(ps2_native_exclusion(&f,h[0],h[1],palette,work,palette,1,1));
        for(unsigned invalid=0;invalid<5;++invalid){struct ps2_filter_workspace bad=f;
            if(invalid==0)bad.bytes=2;else if(invalid==1)bad.bytes=5;
            else if(invalid==2)bad.sum[2]=0;else if(invalid==3)bad.sum[2]=bad.sum[0];else bad.gs=NULL;
            REJECT_ORDER(ORDER(&bad,h,h[4],work,palette,1,1,0));
        }
        uint32_t features=gs->caps.features;
        const uint32_t required[]={PS2GS_HW_CHANNEL_LUT,PS2GS_HW_CHANNEL_MAP,PS2GS_HW_TEXTURE_TRANSFER,
            PS2GS_HW_RGBA_TRANSFER,PS2GS_HW_SOLID_LOGIC};
        for(unsigned i=0;i<5;++i){gs->caps.features=features&~required[i];
            REJECT_ORDER(ORDER(&f,h,h[4],work,palette,1,1,0));assert(errno==ENOTSUP);}
        gs->caps.features=features;
        const uint32_t forbidden[]={h[0],h[1],h[2],h[3],h[4],palette,0,small,word,
            f.tap,f.term,f.sum[0],f.sum[1],f.sum[2],f.saturated,f.carry};
        for(unsigned pos=0;pos<4;++pos){
            for(unsigned i=0;i<sizeof(forbidden)/sizeof(*forbidden);++i){
                uint32_t bad[]={work[0],work[1],work[2],work[3]};bad[pos]=forbidden[i];
                REJECT_ORDER(ORDER(&f,h,h[4],bad,palette,1,1,0));
            }
            for(unsigned other=0;other<pos;++other){uint32_t bad[]={work[0],work[1],work[2],work[3]};bad[pos]=bad[other];
                REJECT_ORDER(ORDER(&f,h,h[4],bad,palette,1,1,0));}
        }
        assert(guards==112);printf("PS2 ordered blend preflight guards=%u PASS\n",guards);
#undef ORDER
#undef REJECT_ORDER
    }else{
        unsigned checks=0,clamps=0,rounding=0;const unsigned edges[]={0,1,127,128,254,255};
        for(unsigned mode=0;mode<3;++mode)for(unsigned group=0;group<4;++group){
            if(f.bytes!=3+(group&1)){assert(!ps2_filter_release(&f));assert(!ps2_filter_reserve(&f,gs,3+(group&1)));}
            for(unsigned alias=0;alias<5;++alias)for(unsigned base=0;base<(alias?2048U:65536U);base+=2048){
                uint32_t inputs[4][2048],expected[2048],out=alias?h[alias-1]:h[4];
                for(unsigned i=0;i<2048;++i){unsigned v=base+i,s=v>>8,d=v&255;
                    for(unsigned j=0;j<4;++j)inputs[j][i]=(0xa1U+j)<<24;
                    expected[i]=alias?inputs[alias-1][i]:0xc3000000U;
                    for(unsigned c=0;c<3;++c){unsigned sc=c==1?255-s:s,dc=c==2?255-d:d;
                        unsigned sa=group==0?255:group==1?(s*73+d*37+c*19)&255:
                            group==2?(sc>((s*17+d*7)&255)?sc:(s*17+d*7)&255):edges[(v+c)%6];
                        unsigned da=group==0?255:group==1?(d*29+s*17+c*43)&255:
                            group==2?(dc>((s*11+d*13)&255)?dc:(s*11+d*13)&255):edges[(v/6+c)%6];
                        inputs[0][i]|=sc<<(8*c);inputs[1][i]|=dc<<(8*c);
                        inputs[2][i]|=sa<<(8*c);inputs[3][i]|=da<<(8*c);
                        /* Independent PDF cross terms plus min/max/absolute blend. */
                        unsigned p=sc*da,q=dc*sa,blend=mode==0?(p<q?p:q):mode==1?(p>q?p:q):p>q?p-q:q-p;
                        unsigned numerator=sc*(255-da)+dc*(255-sa)+blend;
                        unsigned wanted=numerator>65025?255:(numerator+127)/255;
                        expected[i]|=wanted<<(8*c);clamps+=numerator>65535;
                        unsigned separate=(sc*(255-da)+127)/255+(dc*(255-sa)+127)/255+(blend+127)/255;
                        rounding+=wanted!=(separate>255?255:separate);
                    }
                    for(unsigned j=0;j<4;++j)model(h[j])->data[i]=inputs[j][i];
                    model(h[4])->data[i]=0xc3456789U;
                }
                compositor_active=1;assert(!ps2_native_order_blend(&f,h[0],h[1],h[2],h[3],out,work,palette,64,32,mode));compositor_active=0;
                for(unsigned i=0;i<2048;++i){if(model(out)->data[i]!=expected[i]){
                    fprintf(stderr,"ordered blend mode=%u group=%u alias=%u vector=%u expected=%08x got=%08x\n",mode,group,alias,base+i,expected[i],model(out)->data[i]);abort();}
                    for(unsigned j=0;j<4;++j)if(h[j]!=out)assert(model(h[j])->data[i]==inputs[j][i]);
                    checks+=3;
                }
            }
        }
        for(unsigned mode=0;mode<3;++mode){
            for(unsigned i=0;i<2048;++i){for(unsigned j=0;j<4;++j)model(h[j])->data[i]=(0xa1U+j)<<24|((i*73+j*17)&255)*0x010101;
                model(h[4])->data[i]=0xc3456789U;}
            compositor_active=1;assert(!ps2_native_order_blend(&f,h[0],h[1],h[2],h[3],h[4],work,palette,13,7,mode));compositor_active=0;
            for(unsigned y=0;y<32;++y)for(unsigned x=0;x<64;++x)if(x>=13 || y>=7)assert(model(h[4])->data[y*64+x]==0xc3456789U);
        }
        assert(checks==2654208 && clamps && rounding);
        printf("PS2 ordered blends channel_checks=%u overflow_vectors=%u separate-rounding-counterexamples=%u PASS\n",checks,clamps,rounding);
    }
    assert(!ps2_filter_release(&f));for(unsigned i=0;i<9;++i)assert(!ps2_surface_destroy(gs,h[i]));
    assert(!ps2_surface_destroy(gs,palette) && !ps2_surface_destroy(gs,small) && !ps2_surface_destroy(gs,word));finish_rop();
}
static void piecewise_bytes_case(unsigned guards_only)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    struct ps2_filter_workspace f={0};assert(!ps2_filter_reserve(&f,gs,3));
    uint32_t h[11],palette,small,word;
    for(unsigned i=0;i<11;++i)assert(!ps2_surface_create(gs,64,32,32,h+i));
    assert(!ps2_surface_create(gs,16,16,32,&palette));
    assert(!ps2_surface_create(gs,1,1,32,&small) && !ps2_surface_create(gs,64,32,16,&word));
    const uint32_t work[]={h[5],h[6],h[7],h[8],h[9],h[10]};
    if(guards_only){
        unsigned guards=0;
#define REJECT_ORDER(call) do{unsigned before=attempts;assert((call)==-1 && before==attempts);++guards;}while(0)
#define ORDER(fs,inputs,out,scratch,p,w,ht,mode) ps2_native_piecewise_blend(fs,(inputs)[0],(inputs)[1],(inputs)[2],(inputs)[3],out,scratch,p,w,ht,mode)
        REJECT_ORDER(ORDER(NULL,h,h[4],work,palette,1,1,0));
        REJECT_ORDER(ORDER(&f,h,h[4],NULL,palette,1,1,0));
        REJECT_ORDER(ORDER(&f,h,h[4],work,0,1,1,0));
        REJECT_ORDER(ORDER(&f,h,h[4],work,palette,0,1,0));
        REJECT_ORDER(ORDER(&f,h,h[4],work,palette,1,0,0));
        REJECT_ORDER(ORDER(&f,h,h[4],work,palette,65,1,0));
        REJECT_ORDER(ORDER(&f,h,h[4],work,palette,1,33,0));
        REJECT_ORDER(ORDER(&f,h,h[4],work,palette,1,1,2));
        for(unsigned role=0;role<4;++role)for(unsigned invalid=0;invalid<4;++invalid){
            uint32_t in[]={h[0],h[1],h[2],h[3]};in[role]=invalid==0?0:invalid==1?small:invalid==2?word:f.sum[2];
            REJECT_ORDER(ORDER(&f,in,h[4],work,palette,1,1,0));
        }
        REJECT_ORDER(ORDER(&f,h,0,work,palette,1,1,0));
        REJECT_ORDER(ORDER(&f,h,word,work,palette,1,1,0));
        REJECT_ORDER(ORDER(&f,h,small,work,palette,2,1,0));
        REJECT_ORDER(ORDER(&f,h,palette,work,palette,1,1,0));
        REJECT_ORDER(ps2_native_multiply(&f,h[0],h[1],palette,palette,1,1));
        REJECT_ORDER(ps2_native_product(&f,h[0],h[1],h[4],palette,palette,1,1));
        REJECT_ORDER(ps2_native_product(&f,h[0],h[1],palette,h[4],palette,1,1));
        REJECT_ORDER(ps2_native_exclusion(&f,h[0],h[1],palette,work,palette,1,1));
        for(unsigned invalid=0;invalid<5;++invalid){struct ps2_filter_workspace bad=f;
            if(invalid==0)bad.bytes=2;else if(invalid==1)bad.bytes=5;
            else if(invalid==2)bad.sum[2]=0;else if(invalid==3)bad.sum[2]=bad.sum[0];else bad.gs=NULL;
            REJECT_ORDER(ORDER(&bad,h,h[4],work,palette,1,1,0));
        }
        uint32_t features=gs->caps.features;
        const uint32_t required[]={PS2GS_HW_CHANNEL_LUT,PS2GS_HW_CHANNEL_MAP,PS2GS_HW_TEXTURE_TRANSFER,
            PS2GS_HW_RGBA_TRANSFER,PS2GS_HW_SOLID_LOGIC};
        for(unsigned i=0;i<5;++i){gs->caps.features=features&~required[i];
            REJECT_ORDER(ORDER(&f,h,h[4],work,palette,1,1,0));assert(errno==ENOTSUP);}
        gs->caps.features=features;
        const uint32_t forbidden[]={h[0],h[1],h[2],h[3],h[4],palette,0,small,word,
            f.tap,f.term,f.sum[0],f.sum[1],f.sum[2],f.saturated,f.carry};
        for(unsigned pos=0;pos<6;++pos){
            for(unsigned i=0;i<sizeof(forbidden)/sizeof(*forbidden);++i){
                uint32_t bad[]={work[0],work[1],work[2],work[3],work[4],work[5]};bad[pos]=forbidden[i];
                REJECT_ORDER(ORDER(&f,h,h[4],bad,palette,1,1,0));
            }
            for(unsigned other=0;other<pos;++other){uint32_t bad[]={work[0],work[1],work[2],work[3],work[4],work[5]};bad[pos]=bad[other];
                REJECT_ORDER(ORDER(&f,h,h[4],bad,palette,1,1,0));}
        }
        assert(guards==153);printf("PS2 piecewise blend preflight guards=%u PASS\n",guards);
#undef ORDER
#undef REJECT_ORDER
    }else{
        unsigned checks=0,clamps=0,rounding=0;const unsigned edges[]={0,1,127,128,254,255};
        for(unsigned mode=0;mode<2;++mode)for(unsigned group=0;group<5;++group){
            if(f.bytes!=3+(group&1)){assert(!ps2_filter_release(&f));assert(!ps2_filter_reserve(&f,gs,3+(group&1)));}
            for(unsigned alias=0;alias<5;++alias)for(unsigned base=0;base<(alias?2048U:65536U);base+=2048){
                uint32_t inputs[4][2048],expected[2048],out=alias?h[alias-1]:h[4];
                for(unsigned i=0;i<2048;++i){unsigned v=base+i,s=v>>8,d=v&255;
                    for(unsigned j=0;j<4;++j)inputs[j][i]=(0xa1U+j)<<24;
                    expected[i]=alias?inputs[alias-1][i]:0xc3000000U;
                    for(unsigned c=0;c<3;++c){unsigned sc=c==1?255-s:s,dc=c==2?255-d:d;
                        int boundary_s=2*sc+(int)((v+c)%3)-1,boundary_d=2*dc+(int)((v/3+c)%3)-1;
                        unsigned sa=group==0?255:group==1?(s*73+d*37+c*19)&255:
                            group==2?(sc>((s*17+d*7)&255)?sc:(s*17+d*7)&255):group==3?edges[(v+c)%6]:
                            boundary_s<0?0:boundary_s>255?255:(unsigned)boundary_s;
                        unsigned da=group==0?255:group==1?(d*29+s*17+c*43)&255:
                            group==2?(dc>((s*11+d*13)&255)?dc:(s*11+d*13)&255):group==3?edges[(v/6+c)%6]:
                            boundary_d<0?0:boundary_d>255?255:(unsigned)boundary_d;
                        inputs[0][i]|=sc<<(8*c);inputs[1][i]|=dc<<(8*c);
                        inputs[2][i]|=sa<<(8*c);inputs[3][i]|=da<<(8*c);
                        /* Independent PDF cross terms plus min/max/absolute blend. */
                        int blend=(mode==0?2*dc<da:2*sc<sa)?2*sc*dc:(int)(sa*da)-2*((int)da-dc)*((int)sa-sc);
                        unsigned numerator=sc*(255-da)+dc*(255-sa)+blend;
                        unsigned wanted=numerator>65025?255:(numerator+127)/255;
                        expected[i]|=wanted<<(8*c);clamps+=numerator>65535;
                        unsigned separate=(sc*(255-da)+127)/255+(dc*(255-sa)+127)/255+(blend+127)/255;
                        rounding+=wanted!=(separate>255?255:separate);
                    }
                    for(unsigned j=0;j<4;++j)model(h[j])->data[i]=inputs[j][i];
                    model(h[4])->data[i]=0xc3456789U;
                }
                compositor_active=1;assert(!ps2_native_piecewise_blend(&f,h[0],h[1],h[2],h[3],out,work,palette,64,32,mode));compositor_active=0;
                for(unsigned i=0;i<2048;++i){if(model(out)->data[i]!=expected[i]){
                    fprintf(stderr,"piecewise blend mode=%u group=%u alias=%u vector=%u expected=%08x got=%08x\n",mode,group,alias,base+i,expected[i],model(out)->data[i]);abort();}
                    for(unsigned j=0;j<4;++j)if(h[j]!=out)assert(model(h[j])->data[i]==inputs[j][i]);
                    checks+=3;
                }
            }
        }
        for(unsigned mode=0;mode<2;++mode){
            for(unsigned i=0;i<2048;++i){for(unsigned j=0;j<4;++j)model(h[j])->data[i]=(0xa1U+j)<<24|((i*73+j*17)&255)*0x010101;
                model(h[4])->data[i]=0xc3456789U;}
            compositor_active=1;assert(!ps2_native_piecewise_blend(&f,h[0],h[1],h[2],h[3],h[4],work,palette,13,7,mode));compositor_active=0;
            for(unsigned y=0;y<32;++y)for(unsigned x=0;x<64;++x)if(x>=13 || y>=7)assert(model(h[4])->data[y*64+x]==0xc3456789U);
        }
        assert(checks==2211840 && clamps && rounding);
        printf("PS2 piecewise blends channel_checks=%u overflow_vectors=%u separate-rounding-counterexamples=%u PASS\n",checks,clamps,rounding);
    }
    assert(!ps2_filter_release(&f));for(unsigned i=0;i<11;++i)assert(!ps2_surface_destroy(gs,h[i]));
    assert(!ps2_surface_destroy(gs,palette) && !ps2_surface_destroy(gs,small) && !ps2_surface_destroy(gs,word));finish_rop();
}
static void blend_solid_case(unsigned xop)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    assert(!ps2_surface_set_depth(gs,destination_handle(),32));xp.drawable.depth=32;
    struct ps2_exa_context_bridge bridge={0};struct ps2_texture_exa texture={0};struct ps2_blit_exa blit={0};
    assert(ps2_exa_context_bridge_prepare(&xs,&bridge,&exa,ctx));
    assert(ps2_texture_exa_prepare(&xs,&texture,&exa,ps2_pipe_context_render(ctx)));
    assert(ps2_blit_exa_prepare(&xs,&blit,&exa,ctx));
    const unsigned alphas[]={0,1,127,128,255};
    for(unsigned ai=0;ai<5;++ai)for(unsigned alpha=0;alpha<2;++alpha)for(unsigned masked=0;masked<(hsl_wire(xop)?2U:3U);++masked){
        unsigned a=alphas[ai],r=a*57/255,g=a*113/255,b=a*191/255;
        SourcePict sp={.solidFill={.type=SourcePictTypeSolidFill,.color=(a<<24)|(r<<16)|(g<<8)|b,.fullcolor={.red=r*257,.green=g*257,.blue=b*257,.alpha=a*257}}},
            mp={.solidFill={.type=SourcePictTypeSolidFill,.color=0xc1804031,.fullcolor={.red=128*257,.green=64*257,.blue=49*257,.alpha=193*257}}};
        PictureRec source={.pSourcePict=&sp,.format=PICT_a8r8g8b8,.filter=PictFilterNearest},
            mask={.pSourcePict=&mp,.format=PICT_a8r8g8b8,.filter=PictFilterNearest,.componentAlpha=masked==2},
            destination={.pDrawable=&xp.drawable,.format=alpha?PICT_a8b8g8r8:PICT_x8b8g8r8,.filter=PictFilterNearest};
        pixman_color_t sc={.red=r*257,.green=g*257,.blue=b*257,.alpha=a*257},
            mc={.red=128*257,.green=64*257,.blue=49*257,.alpha=193*257};
        rgba_seed(model(destination_handle()));void *expected;
        pixman_image_t *si=pixman_image_create_solid_fill(&sc),*mi=pixman_image_create_solid_fill(&mc),
            *di=reference_image(model(destination_handle()),32,0,alpha,&expected);
        assert(si && mi && di);pixman_image_set_component_alpha(mi,masked==2);
        unsigned rw=hsl_wire(xop) || xop==PictOpSoftLight || xop==PictOpColorDodge || xop==PictOpColorBurn || (xop>=PictOpDisjointOver && xop<=PictOpDisjointXor) || (xop>=PictOpConjointOver && xop<=PictOpConjointXor)?1:7,rh=hsl_wire(xop) || xop==PictOpSoftLight || xop==PictOpColorDodge || xop==PictOpColorBurn || (xop>=PictOpDisjointOver && xop<=PictOpDisjointXor) || (xop>=PictOpConjointOver && xop<=PictOpConjointXor)?1:3;
        pixman_image_composite32((pixman_op_t)xop,si,masked?mi:NULL,di,0,0,0,0,3,2,rw,rh);
        compositor_active=1;assert(exa.driver.CheckComposite(xop,&source,masked?&mask:NULL,&destination));
        assert(exa.driver.PrepareComposite(xop,&source,masked?&mask:NULL,&destination,NULL,NULL,&xp));
        exa.driver.Composite(&xp,0,0,0,0,3,2,rw,rh);compositor_active=0;exa.driver.DoneComposite(&xp);
        struct pixels *d=model(destination_handle());
        for(unsigned y=0;y<d->height;++y)for(unsigned x=0;x<d->width;++x){uint32_t wanted;
            memcpy(&wanted,(unsigned char*)expected+(y*d->width+x)*4,4);
            assert(!((wanted^d->data[y*d->width+x])&(alpha?UINT32_MAX:0xffffff)));}
        unsigned before=attempts;assert(!exa.driver.CheckComposite(255,&source,NULL,&destination) && before==attempts);
        pixman_image_unref(si);pixman_image_unref(mi);pixman_image_unref(di);free(expected);
    }
    ps2_blit_exa_cleanup(&xs,&blit);ps2_exa_context_bridge_cleanup(&bridge);finish_rop();
}
static void rgba_case(const char *name)
{
    unsigned blend=extended_operation(name);
    if(blend)name=strchr(name,'-')+1;
    rgba_enabled=1;setup_rop(32);
    struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    unsigned sr=!strncmp(name,"bg",2) && !strstr(name,"destination"),
        dr=!strncmp(name,"bg",2) && !strstr(name,"source");
    unsigned sa=strcmp(name,"rgba-opaque-source")!=0 && !(sr && !strncmp(name,"bgrx",4)),
        da=strcmp(name,"rgba-opaque-destination")!=0 && !(dr && !strncmp(name,"bgrx",4));
    if(sa || sr)assert(!ps2_surface_set_depth(gs,source_handle,32));
    if(da || dr)assert(!ps2_surface_set_depth(gs,destination_handle(),32));
    rgba_seed(model(source_handle));
    if(sr){struct pixels *p=model(source_handle);for(unsigned i=0;i<p->width*p->height;++i)
        p->data[i]=(p->data[i]<<8)|(p->data[i]>>24);}
    unsigned char bits[8*5];for(unsigned i=0;i<sizeof(bits);++i)bits[i]=i*73;
    struct ps2_picture_bitmap mask={.bits=bits,.bytes=sizeof(bits),.stride=8,.width=7,.height=5,
        .bits_per_pixel=8,.x=-1,.y=1,.sample=identity(PS2_REPEAT_REFLECT)};
    int32_t coefficients[]={2*65536,65536,2*65536,-65536};
    struct ps2_pipe_copy_box box={-1,-1,2,1,3,3};
    if(ratio_test_operation(blend)){box.width=box.height=1;coefficients[2]=49152;coefficients[3]=16384;}
    for(unsigned swapped=0;swapped<4;++swapped)for(unsigned filter=0;filter<3;++filter)
    for(unsigned repeat=0;repeat<4;++repeat)for(unsigned masked=0;masked<2;++masked)
    for(unsigned operation=blend?blend:0;operation<=(blend?blend:PS2_COMPOSITE_XOR);++operation){
        struct ps2_picture_sample sample=identity(repeat);sample.source_swap=sr?0:swapped&1;sample.destination_swap=dr?0:swapped>>1;
        sample.source_rotate=sr;sample.destination_rotate=dr;
        sample.source_argb=sa;sample.destination_argb=da;
        sample.matrix[0][0]=98304;sample.matrix[1][1]=32768;sample.matrix[0][2]=-21845;
        sample.matrix[2][0]=65536/15;sample.matrix[2][1]=65536/19;
        sample.bilinear=filter==1;sample.convolution=filter==2;
        if(filter==2){sample.filter_params=coefficients;sample.filter_nparams=4;}
        rgba_seed(model(destination_handle()));
        if(dr){struct pixels *p=model(destination_handle());for(unsigned i=0;i<p->width*p->height;++i)
            p->data[i]=(p->data[i]<<8)|(p->data[i]>>24);}
        composite_reference(source_handle,destination_handle(),&box,&sample,
            masked?&mask:NULL,operation);
    }
    if(sa && da){
        struct ps2_picture_sample sample=identity(PS2_REPEAT_REFLECT);
        sample.source_argb=sample.destination_argb=1;sample.source_swap=dr?0:1;
        sample.source_rotate=sample.destination_rotate=dr;
        box=(struct ps2_pipe_copy_box){2,1,3,2,ratio_test_operation(blend)?2:71,ratio_test_operation(blend)?1:3};rgba_seed(model(destination_handle()));
        composite_reference(destination_handle(),destination_handle(),&box,&sample,NULL,blend?blend:PS2_COMPOSITE_OVER);
    }
    finish_rop();
}
static unsigned alpha_op(unsigned op,unsigned s,unsigned d)
{
    unsigned value;
    switch(op){
    case PictOpClear:return 0;case PictOpSrc:return s;case PictOpDst:return d;
    case PictOpOver:value=s+mul255(d,255-s);break;
    case PictOpOverReverse:value=d+mul255(s,255-d);break;
    case PictOpIn:return mul255(s,d);case PictOpInReverse:return mul255(d,s);
    case PictOpOut:return mul255(s,255-d);case PictOpOutReverse:return mul255(d,255-s);
    case PictOpAtop:value=mul255(s,d)+mul255(d,255-s);break;
    case PictOpAtopReverse:value=mul255(d,s)+mul255(s,255-d);break;
    case PictOpXor:value=mul255(s,255-d)+mul255(d,255-s);break;
    default:value=s+d;break;}
    return value>255?255:value;
}
static void coverage_exa_case(void)
{
    setup_rop(32);struct ps2_exa_context_bridge bridge={0};struct ps2_texture_exa textures={0};struct ps2_blit_exa compositor={0};
    assert(ps2_exa_context_bridge_prepare(&xs,&bridge,&exa,ctx));
    assert(ps2_texture_exa_prepare(&xs,&textures,&exa,ps2_pipe_context_render(ctx)));
    assert(ps2_blit_exa_prepare(&xs,&compositor,&exa,ctx));
    PixmapRec dst={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=8,.bitsPerPixel=8,.width=9,.height=5}},mask=dst;
    int pitch;dst.devPrivates=exa.driver.CreatePixmap2(&xs,9,5,8,0,8,&pitch);dst.devKind=pitch;
    mask.devPrivates=exa.driver.CreatePixmap2(&xs,9,5,8,0,8,&pitch);mask.devKind=pitch;
    struct ps2_alpha_source dv,mv;assert(ps2_surface_exa_alpha_source(&dst,&dv)==1);
    assert(ps2_surface_exa_alpha_source(&mask,&mv)==1);
    SourcePict source={.solidFill={.type=SourcePictTypeSolidFill,.color=0x81000000,.fullcolor={.alpha=0x8181}}};
    PictureRec s={.pSourcePict=&source,.format=PICT_a8r8g8b8,.filter=PictFilterNearest};
    PictureRec d={.pDrawable=&dst.drawable,.format=PICT_a8,.filter=PictFilterNearest};
    PictureRec m={.pDrawable=&mask.drawable,.format=PICT_a8,.filter=PictFilterNearest};
    for(unsigned y=0;y<5;++y)for(unsigned x=0;x<9;++x)((unsigned char*)mv.bits)[y*mv.stride+x]=(y*9+x)*73;
    for(unsigned op=0;op<=PictOpSaturate;++op)for(unsigned masked=0;masked<2;++masked){
        for(unsigned y=0;y<5;++y)for(unsigned x=0;x<9;++x)((unsigned char*)dv.bits)[y*dv.stride+x]=(y*9+x)*37;
        assert(exa.driver.CheckComposite(op,&s,masked?&m:NULL,&d));
        assert(exa.driver.PrepareComposite(op,&s,masked?&m:NULL,&d,NULL,masked?&mask:NULL,&dst));
        compositor_active=1;exa.driver.Composite(&dst,0,0,1,1,2,1,5,3);compositor_active=0;
        exa.driver.DoneComposite(&dst);
        assert(!compositor.coverage_source.surface && !compositor.coverage_destination.surface);
        for(unsigned y=0;y<5;++y)for(unsigned x=0;x<9;++x){unsigned initial=((y*9+x)*37)&255,wanted=initial;
            if(x>=2 && x<7 && y>=1 && y<4){unsigned a=masked?mv.bits[y*mv.stride+x-1]:255;
                wanted=alpha_op(op,mul255(129,a),initial);
                /* Saturate uses pixman's float path: combine the unrounded
                 * normalized source/mask alpha, then quantize to 256 bins. */
                if(op==PictOpSaturate){wanted=(129*a+initial*255)*256/(255*255);if(wanted>255)wanted=255;}}
            if(dv.bits[y*dv.stride+x]!=wanted){fprintf(stderr,"coverage mismatch op=%u mask=%u x=%u y=%u expected=%u actual=%u\n",
                op,masked,x,y,wanted,dv.bits[y*dv.stride+x]);abort();}}
    }
    exa.driver.DestroyPixmap(&xs,dst.devPrivates);exa.driver.DestroyPixmap(&xs,mask.devPrivates);
    ps2_blit_exa_cleanup(&xs,&compositor);ps2_exa_context_bridge_cleanup(&bridge);finish_rop();
}
static void coverage_cross_case(const char *name)
{
    unsigned saturate_only=strstr(name,"saturate-coverage")!=NULL,blend=extended_operation(name);
    unsigned xop=blend>=PS2_COMPOSITE_DISJOINT_OVER?ratio_test_wire(blend):blend==PS2_COMPOSITE_DISJOINT_CLEAR?PictOpDisjointClear:blend==PS2_COMPOSITE_DISJOINT_SRC?PictOpDisjointSrc:blend==PS2_COMPOSITE_DISJOINT_DST?PictOpDisjointDst:blend==PS2_COMPOSITE_CONJOINT_CLEAR?PictOpConjointClear:blend==PS2_COMPOSITE_CONJOINT_SRC?PictOpConjointSrc:blend==PS2_COMPOSITE_CONJOINT_DST?PictOpConjointDst:ratio_test_operation(blend)?PictOpDisjointOverReverse:blend==PS2_COMPOSITE_OVERLAY?PictOpOverlay:blend==PS2_COMPOSITE_HARDLIGHT?PictOpHardLight:blend==PS2_COMPOSITE_DARKEN?PictOpDarken:blend==PS2_COMPOSITE_LIGHTEN?PictOpLighten:blend==PS2_COMPOSITE_DIFFERENCE?PictOpDifference:blend==PS2_COMPOSITE_MULTIPLY?PictOpMultiply:blend==PS2_COMPOSITE_EXCLUSION?PictOpExclusion:PictOpScreen;
    unsigned native_bpp=strstr(name,"555")?16:32,native_alpha=strstr(name,"argb")!=NULL;
    unsigned to_colour=strstr(name,"to-colour")!=NULL;
    unsigned component_alpha=strstr(name,"coverage-ca-")!=NULL;
    rgba_enabled=1;setup_rop(native_bpp);
    struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    if(native_alpha){assert(!ps2_surface_set_depth(gs,source_handle,32));
        assert(!ps2_surface_set_depth(gs,destination_handle(),32));xp.drawable.depth=32;}
    struct ps2_exa_context_bridge bridge={0};struct ps2_texture_exa textures={0};struct ps2_blit_exa compositor={0};
    assert(ps2_exa_context_bridge_prepare(&xs,&bridge,&exa,ctx));
    assert(ps2_texture_exa_prepare(&xs,&textures,&exa,ps2_pipe_context_render(ctx)));
    assert(ps2_blit_exa_prepare(&xs,&compositor,&exa,ctx));
    uint32_t source_private=source_handle;
    const struct pixels *sp=model(source_handle);
    PixmapRec colour_source={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=native_alpha?32:native_bpp==16?15:24,
        .bitsPerPixel=native_bpp,.width=sp->width,.height=sp->height},.devPrivates=(void*)&source_private};
    uint32_t native_format=native_bpp==16?PICT_x1b5g5r5:native_alpha?PICT_a8b8g8r8:PICT_x8b8g8r8;
    PictureRec native_s={.pDrawable=&colour_source.drawable,.format=native_format,.filter=PictFilterNearest};
    PictureRec native_d={.pDrawable=&xp.drawable,.format=native_format,.filter=PictFilterNearest};
    const unsigned depths[]={1,4,8};
    for(unsigned kind=0;kind<3;++kind){unsigned coverage_bpp=depths[kind],physical=coverage_bpp==4?8:coverage_bpp;
        PixmapRec cp={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=coverage_bpp,
            .bitsPerPixel=physical,.width=9,.height=5}},mp=cp;
        mp.drawable.depth=component_alpha?coverage_bpp:8;mp.drawable.bitsPerPixel=mp.drawable.depth==4?8:mp.drawable.depth;
        int pitch;cp.devPrivates=exa.driver.CreatePixmap2(&xs,9,5,coverage_bpp,0,physical,&pitch);cp.devKind=pitch;
        mp.devPrivates=exa.driver.CreatePixmap2(&xs,9,5,mp.drawable.depth,0,mp.drawable.bitsPerPixel,&pitch);mp.devKind=pitch;
        struct ps2_alpha_source cv,mv;assert(ps2_surface_exa_alpha_source(&cp,&cv)==1);
        assert(ps2_surface_exa_alpha_source(&mp,&mv)==1);
        for(unsigned i=0;i<cv.bytes;++i)((unsigned char*)cv.bits)[i]=i*73+29;
        for(unsigned i=0;i<mv.bytes;++i)((unsigned char*)mv.bits)[i]=i*37+19;
        PictureRec coverage={.pDrawable=&cp.drawable,.format=coverage_bpp==1?PICT_a1:coverage_bpp==4?PICT_x4a4:PICT_a8,.filter=PictFilterNearest};
        PictureRec mask={.pDrawable=&mp.drawable,.format=mp.drawable.depth==1?PICT_a1:mp.drawable.depth==4?PICT_x4a4:PICT_a8,
            .filter=PictFilterNearest,.repeat=1,.repeatType=RepeatReflect,.componentAlpha=component_alpha};
        int32_t coefficients[]={2*65536,65536,2*65536,-65536};
        int32_t wide_coefficients[]={2*65536,65536,49152,16384};
        PictTransform transform={.matrix={{98304,0,-21845},{0,32768,21845},{65536/15,65536/19,65536}}};
        for(unsigned filter=0;filter<(saturate_only?2U:3U);++filter)for(unsigned repeat=0;repeat<4;++repeat)
        for(unsigned masked=0;masked<2;++masked)for(unsigned op=blend?xop:saturate_only?PictOpSaturate:0;op<=(blend?xop:to_colour?PictOpAdd:PictOpSaturate);++op){
            PicturePtr source=to_colour?&coverage:&native_s,destination=to_colour?&native_d:&coverage;
            source->transform=&transform;source->repeat=repeat!=0;source->repeatType=repeat;
            source->filter=filter==2?PictFilterConvolution:filter==1?PictFilterBilinear:PictFilterNearest;
            /* Negative/out-of-range float-to-unsigned casts differ on host
             * and R5900. This portable pixman matrix uses bounded positive
             * wide kernels; signed target behavior has dedicated tests. */
            source->filter_params=filter==2?((op==PictOpSaturate || (hsl_wire(op) || op==PictOpSoftLight || op==PictOpColorDodge || op==PictOpColorBurn || (op==PictOpDisjointOverReverse || (op>=PictOpDisjointOver && op<=PictOpDisjointXor) || (op>=PictOpConjointOver && op<=PictOpConjointXor))))?wide_coefficients:coefficients):NULL;
            source->filter_nparams=filter==2?4:0;
            if(!to_colour){coverage.transform=NULL;coverage.repeat=0;coverage.filter=PictFilterNearest;
                coverage.filter_params=NULL;coverage.filter_nparams=0;}
            struct pixels *native_model=model(to_colour?destination_handle():source_handle);
            if(native_alpha)rgba_seed(native_model);else seed(native_model);
            unsigned char *expected=malloc(cv.bytes);assert(expected);memcpy(expected,cv.bits,cv.bytes);
            void *native_memory=NULL;pixman_image_t *ni=reference_image(native_model,native_bpp,0,native_alpha,&native_memory);
            pixman_image_t *ci=pixman_image_create_bits(coverage_bpp==1?PIXMAN_a1:coverage_bpp==4?PIXMAN_x4a4:PIXMAN_a8,9,5,
                (uint32_t*)(to_colour?cv.bits:expected),cv.stride);
            pixman_image_t *mi=pixman_image_create_bits(mp.drawable.depth==1?PIXMAN_a1:mp.drawable.depth==4?PIXMAN_x4a4:PIXMAN_a8,9,5,(uint32_t*)mv.bits,mv.stride);
            assert(ci && mi);pixman_image_set_repeat(mi,PIXMAN_REPEAT_REFLECT);
            pixman_image_set_component_alpha(mi,component_alpha);
            pixman_image_t *si=to_colour?ci:ni,*di=to_colour?ni:ci;
            assert(pixman_image_set_transform(si,(pixman_transform_t*)&transform));
            pixman_image_set_repeat(si,(pixman_repeat_t)repeat);
            assert(pixman_image_set_filter(si,filter==2?PIXMAN_FILTER_CONVOLUTION:filter==1?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,
                source->filter_params,source->filter_nparams));
            unsigned rw=(hsl_wire(op) || op==PictOpSoftLight || op==PictOpColorDodge || op==PictOpColorBurn || (op==PictOpDisjointOverReverse || (op>=PictOpDisjointOver && op<=PictOpDisjointXor) || (op>=PictOpConjointOver && op<=PictOpConjointXor)))?1:3,rh=(hsl_wire(op) || op==PictOpSoftLight || op==PictOpColorDodge || op==PictOpColorBurn || (op==PictOpDisjointOverReverse || (op>=PictOpDisjointOver && op<=PictOpDisjointXor) || (op>=PictOpConjointOver && op<=PictOpConjointXor)))?1:2;
            pixman_image_composite32((pixman_op_t)op,si,masked?mi:NULL,di,-1,-1,-1,1,2,1,rw,rh);
            if(!saturate_only && !to_colour && op==PictOpSaturate &&
                (filter==1 || (!filter && masked && component_alpha))){
                /* Bilinear and nearest component-alpha extensions retain
                 * their focused matrices. Convolution now executes here. */
                pixman_image_unref(mi);pixman_image_unref(ci);pixman_image_unref(ni);free(native_memory);free(expected);continue;
            }
            assert(exa.driver.CheckComposite(op,source,masked?&mask:NULL,destination));
            assert(exa.driver.PrepareComposite(op,source,masked?&mask:NULL,destination,
                to_colour?&cp:&colour_source,masked?&mp:NULL,to_colour?&xp:&cp));
            compositor_active=1;exa.driver.Composite(to_colour?&xp:&cp,-1,-1,-1,1,2,1,rw,rh);compositor_active=0;
            exa.driver.DoneComposite(to_colour?&xp:&cp);
            if(to_colour){size_t stride=(native_model->width*(native_bpp/8)+3)&~3U;
                for(unsigned y=0;y<native_model->height;++y)for(unsigned x=0;x<native_model->width;++x){uint32_t wanted=0;
                    memcpy(&wanted,(unsigned char*)native_memory+y*stride+x*(native_bpp/8),native_bpp/8);
                    uint32_t bits=native_bpp==16?0x7fff:native_alpha?UINT32_MAX:0xffffff;
                    if((wanted^native_model->data[y*native_model->width+x])&bits){
                        fprintf(stderr,"coverage-to-colour mismatch bpp=%u filter=%u repeat=%u mask=%u op=%u x=%u y=%u wanted=%08x got=%08x\n",
                            coverage_bpp,filter,repeat,masked,op,x,y,wanted,native_model->data[y*native_model->width+x]);abort();}}
            }else for(unsigned i=0;i<cv.bytes;++i)if((expected[i]^cv.bits[i])&(coverage_bpp==4?15:255)){
                fprintf(stderr,"colour-to-coverage mismatch bpp=%u filter=%u repeat=%u mask=%u op=%u byte=%u wanted=%u got=%u\n",
                    coverage_bpp,filter,repeat,masked,op,i,expected[i],cv.bits[i]);abort();}
            assert(!compositor.coverage_source.surface && !compositor.coverage_destination.surface && !compositor.solid);
            pixman_image_unref(ni);pixman_image_unref(ci);pixman_image_unref(mi);free(native_memory);free(expected);
        }
        exa.driver.DestroyPixmap(&xs,cp.devPrivates);exa.driver.DestroyPixmap(&xs,mp.devPrivates);
    }
    ps2_blit_exa_cleanup(&xs,&compositor);
    ps2_exa_context_bridge_cleanup(&bridge);finish_rop();
}
static void saturate_coverage_alias_case(void)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    assert(!ps2_surface_set_depth(gs,source_handle,32));rgba_seed(model(source_handle));
    struct ps2_exa_context_bridge bridge={0};struct ps2_texture_exa textures={0};struct ps2_blit_exa compositor={0};
    assert(ps2_exa_context_bridge_prepare(&xs,&bridge,&exa,ctx));
    assert(ps2_texture_exa_prepare(&xs,&textures,&exa,ps2_pipe_context_render(ctx)));
    assert(ps2_blit_exa_prepare(&xs,&compositor,&exa,ctx));
    PixmapRec colour={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=32,.bitsPerPixel=32,.width=83,.height=61},
        .devPrivates=(void*)&source_handle};
    const unsigned depths[]={1,4,8};
    for(unsigned kind=0;kind<3;++kind){unsigned depth=depths[kind],bpp=depth==1?1:8;int pitch;
        PixmapRec raw={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=depth,.bitsPerPixel=bpp,.width=9,.height=5}},mp=raw;
        raw.devPrivates=exa.driver.CreatePixmap2(&xs,9,5,depth,0,bpp,&pitch);raw.devKind=pitch;
        mp.devPrivates=exa.driver.CreatePixmap2(&xs,9,5,depth,0,bpp,&pitch);mp.devKind=pitch;
        struct ps2_alpha_source dv,mv;assert(ps2_surface_exa_alpha_source(&raw,&dv)==1 && ps2_surface_exa_alpha_source(&mp,&mv)==1);
        unsigned format=depth==1?PICT_a1:depth==4?PICT_x4a4:PICT_a8;
        pixman_format_code_t pf=depth==1?PIXMAN_a1:depth==4?PIXMAN_x4a4:PIXMAN_a8;
        PictTransform st={.matrix={{49152,21845,173},{0,98304,-21845},{65536/19,0,65536}}},
            mt={.matrix={{98304,0,257},{0,32768,21845},{0,0,65536}}};
        for(unsigned alias=0;alias<3;++alias)for(unsigned mix=1;mix<=3;++mix)for(unsigned component=0;component<2;++component){
            for(unsigned i=0;i<dv.bytes;++i)((unsigned char*)dv.bits)[i]=i*37+131;
            for(unsigned i=0;i<mv.bytes;++i)((unsigned char*)mv.bits)[i]=i*73+29;
            PictureRec destination={.pDrawable=&raw.drawable,.format=format,.filter=PictFilterNearest},
                source={.pDrawable=alias==1?&raw.drawable:alias==0?&mp.drawable:&colour.drawable,
                    .format=alias==2?PICT_a8b8g8r8:format,.filter=mix&1?PictFilterBilinear:PictFilterNearest,
                    .transform=&st,.repeat=1,.repeatType=RepeatReflect},
                mask={.pDrawable=alias==2?&raw.drawable:&mp.drawable,.format=format,
                    .filter=mix&2?PictFilterBilinear:PictFilterNearest,.transform=&mt,.repeat=1,
                    .repeatType=RepeatPad,.componentAlpha=component};
            unsigned char *expected=malloc(dv.bytes);assert(expected);memcpy(expected,dv.bits,dv.bytes);
            pixman_image_t *di=pixman_image_create_bits(pf,9,5,(uint32_t*)expected,dv.stride),*si;
            void *sm=NULL;
            if(alias==2)si=reference_image(model(source_handle),32,0,1,&sm);
            else si=pixman_image_create_bits(pf,9,5,(uint32_t*)(alias==1?dv.bits:mv.bits),alias==1?dv.stride:mv.stride);
            pixman_image_t *mi=pixman_image_create_bits(pf,9,5,(uint32_t*)(alias==2?dv.bits:mv.bits),alias==2?dv.stride:mv.stride);
            assert(si && mi && di && pixman_image_set_transform(si,(pixman_transform_t*)&st) &&
                pixman_image_set_transform(mi,(pixman_transform_t*)&mt));
            pixman_image_set_repeat(si,PIXMAN_REPEAT_REFLECT);pixman_image_set_repeat(mi,PIXMAN_REPEAT_PAD);
            pixman_image_set_component_alpha(mi,component);
            assert(pixman_image_set_filter(si,mix&1?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,NULL,0) &&
                pixman_image_set_filter(mi,mix&2?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,NULL,0));
            pixman_image_composite32(PIXMAN_OP_SATURATE,si,mi,di,1,1,-1,1,3,2,2,1);
            assert(exa.driver.CheckComposite(PictOpSaturate,&source,&mask,&destination));
            assert(exa.driver.PrepareComposite(PictOpSaturate,&source,&mask,&destination,
                alias==1?&raw:alias==0?&mp:&colour,alias==2?&raw:&mp,&raw));
            compositor_active=1;exa.driver.Composite(&raw,1,1,-1,1,3,2,2,1);compositor_active=0;
            exa.driver.DoneComposite(&raw);
            for(unsigned i=0;i<dv.bytes;++i)if((expected[i]^dv.bits[i])&(depth==4?15:255)){
                fprintf(stderr,"coverage Saturate alias mismatch depth=%u alias=%u mix=%u ca=%u byte=%u wanted=%u got=%u\n",
                    depth,alias,mix,component,i,expected[i],dv.bits[i]);abort();}
            pixman_image_unref(si);pixman_image_unref(mi);pixman_image_unref(di);free(sm);free(expected);
        }
        for(unsigned invalid=0;invalid<6;++invalid){
            PictureRec source={.pDrawable=&colour.drawable,.format=PICT_a8b8g8r8,.filter=PictFilterBilinear},
                destination={.pDrawable=&raw.drawable,.format=format,.filter=PictFilterNearest},
                mask={.pDrawable=&mp.drawable,.format=format,.filter=PictFilterBilinear};
            SourcePict solid={.solidFill={.type=SourcePictTypeSolidFill,.color=0x81000000}};
            if(invalid==0)source.filter=PictFilterConvolution;
            if(invalid==1)mask.filter=PictFilterConvolution;
            if(invalid==2){source.alphaMap=&mask;mask.format=0;}
            /* A word format cannot view byte/bit storage. */
            if(invalid==3){destination.alphaMap=&mask;destination.alphaOrigin.x=1;
                mask.pDrawable=&raw.drawable;mask.format=PICT_a4r4g4b4;}
            if(invalid==4){mask.alphaMap=&source;source.format=0;}
            if(invalid==5){source.pDrawable=NULL;source.pSourcePict=&solid;solid.solidFill.type=SourcePictTypeLinear;}
            unsigned before=attempts,draws=transfer_calls;
            assert(!exa.driver.CheckComposite(PictOpSaturate,&source,&mask,&destination) &&
                !exa.driver.PrepareComposite(PictOpSaturate,&source,&mask,&destination,&colour,&mp,&raw));
            assert(attempts==before && draws==transfer_calls);
        }
        exa.driver.DestroyPixmap(&xs,raw.devPrivates);exa.driver.DestroyPixmap(&xs,mp.devPrivates);
    }
    ps2_blit_exa_cleanup(&xs,&compositor);ps2_exa_context_bridge_cleanup(&bridge);finish_rop();
}
static PixmapPtr coverage_test_pixmap(ScreenPtr screen,int width,int height,int depth,unsigned usage)
{
    (void)usage;PixmapPtr p=calloc(1,sizeof(*p));assert(p);int pitch;
    p->drawable=(DrawableRec){.type=DRAWABLE_PIXMAP,.pScreen=screen,.depth=depth,.bitsPerPixel=depth==4?8:depth,.width=width,.height=height};
    p->devPrivates=exa.driver.CreatePixmap2(screen,width,height,depth,0,depth==4?8:depth,&pitch);assert(p->devPrivates);
    p->devKind=pitch;return p;
}
static Bool coverage_test_destroy(PixmapPtr p)
{exa.driver.DestroyPixmap(p->drawable.pScreen,p->devPrivates);free(p);return TRUE;}
static unsigned raw_value(const unsigned char *p,unsigned bpp,unsigned x)
{unsigned value=0;if(bpp>=8)memcpy(&value,p+x*(bpp/8),bpp/8);else value=!!(p[x/8]&(1U<<(x%8)));return value;}
static unsigned coverage_byte(const struct ps2_alpha_source *a,unsigned x,unsigned y)
{return raw_value(a->bits+y*a->stride,a->bits_per_pixel,x)&((1U<<a->depth)-1);}
static unsigned coverage_test_alu,coverage_test_format;
static void coverage_expect(const struct ps2_alpha_source *a,const unsigned *expected)
{
    for(unsigned y=0;y<a->height;++y)for(unsigned x=0;x<a->width;++x)
        if(coverage_byte(a,x,y)!=expected[y*a->width+x]){
            fprintf(stderr,"core coverage mismatch bpp=%u alu=%u format=%u x=%u y=%u expected=%u actual=%u\n",
                a->bits_per_pixel,coverage_test_alu,coverage_test_format,x,y,expected[y*a->width+x],coverage_byte(a,x,y));abort();}
}
static void coverage_core_case(const char *name)
{
    const unsigned depths[]={1,4,8,16};
    for(unsigned k=0;k<4;++k){unsigned bpp=depths[k],physical=bpp==4?8:bpp;
        rgba_enabled=1;setup_rop(32);xs.SourceValidate=source_validate;
        struct ps2_exa_context_bridge coverage_bridge={0};
        assert(ps2_exa_context_bridge_prepare(&xs,&coverage_bridge,&exa,ctx));
        struct ps2_solid_screen coverage_solid={0};struct ps2_copy_screen coverage_copy={0};
        assert(ps2_solid_screen_prepare(&xs,&coverage_solid,&gc_screen));
        assert(ps2_copy_screen_prepare(&xs,&coverage_copy,&gc_screen));
        xs.CreatePixmap=coverage_test_pixmap;xs.DestroyPixmap=coverage_test_destroy;
        PixmapPtr d=xs.CreatePixmap(&xs,53,13,bpp,0),s=xs.CreatePixmap(&xs,53,13,bpp,0);
        struct ps2_alpha_source dv,sv;assert(ps2_surface_exa_alpha_source(d,&dv)==1);
        assert(ps2_surface_exa_alpha_source(s,&sv)==1);
        for(unsigned i=0;i<sv.bytes;++i)((unsigned char*)sv.bits)[i]=i*73+19;
        pixman_region_fini(&clip_region);pixman_region_init_rect(&clip_region,2,1,49,11);
        gc.depth=bpp;gc.funcs->ValidateGC(&gc,GCAllBits,&window.drawable);
        gc.fExpose=FALSE;
        unsigned expected[53*13],saved[53*13];unsigned char bits[13*56*8],reply[13*112];
        for(unsigned i=0;i<sizeof(bits);++i)bits[i]=i*37+73;
        for(unsigned alu=0;alu<16;++alu){
            coverage_test_alu=alu;
            unsigned mask=bpp==1?1:0xa55a&((1U<<bpp)-1);gc.planemask=mask;gc.alu=alu;
            gc.fgPixel=(bpp==1?1:0x69c3)&((1U<<bpp)-1);gc.bgPixel=(bpp==1?0:0x963c)&((1U<<bpp)-1);
            for(unsigned i=0;i<dv.bytes;++i)((unsigned char*)dv.bits)[i]=i*37+11;
            for(unsigned y=0;y<13;++y)for(unsigned x=0;x<53;++x)expected[y*53+x]=coverage_byte(&dv,x,y);
            if(!strcmp(name,"coverage-core")){
                xRectangle rect={-1,-2,31,11};
                for(unsigned y=1;y<9;++y)for(unsigned x=2;x<30;++x){unsigned i=y*53+x;
                    expected[i]=(expected[i]&~mask)|(oracle(alu,gc.fgPixel,expected[i])&mask);}
                gc.ops->PolyFillRect(&d->drawable,&gc,1,&rect);coverage_expect(&dv,expected);
                for(unsigned y=2;y<10;++y)for(unsigned x=5;x<38;++x){unsigned i=y*53+x;
                    expected[i]=(expected[i]&~mask)|(oracle(alu,coverage_byte(&sv,x-2,y-1),expected[i])&mask);}
                RegionPtr r=gc.ops->CopyArea(&s->drawable,&d->drawable,&gc,3,1,33,8,5,2);if(r)RegionDestroy(r);
                coverage_expect(&dv,expected);memcpy(saved,expected,sizeof(saved));
                for(unsigned y=3;y<11;++y)for(unsigned x=9;x<42;++x){unsigned i=y*53+x;
                    expected[i]=(expected[i]&~mask)|(oracle(alu,saved[(y-1)*53+x-4],expected[i])&mask);}
                r=gc.ops->CopyArea(&d->drawable,&d->drawable,&gc,5,2,33,8,9,3);if(r)RegionDestroy(r);
                coverage_expect(&dv,expected);
            }else if(!strcmp(name,"coverage-core-uploads")){
                unsigned stride=PixmapBytePad(33,bpp);
                for(unsigned y=2;y<9;++y)for(unsigned x=5;x<38;++x){unsigned i=y*53+x,u=x-5,v=y-2;
                    expected[i]=raw_value(bits+v*stride,physical,u)&((1U<<bpp)-1);}
                assert(exa.driver.UploadToScreen(d,5,2,33,7,(char*)bits,stride));coverage_expect(&dv,expected);
            }else if(!strcmp(name,"coverage-core-images")){
                for(unsigned format=0;format<=2;++format){
                    coverage_test_format=format;
                    unsigned depth=format==XYBitmap?1:bpp,pad=format==ZPixmap?0:3;
                    unsigned stride=format==ZPixmap?PixmapBytePad(33,bpp):BitmapBytePad(33+pad);
                    for(unsigned y=2;y<9;++y)for(unsigned x=5;x<38;++x){unsigned i=y*53+x,u=x-5+pad,v=y-2,value=0;
                        if(format==ZPixmap)value=raw_value(bits+v*stride,physical,u)&((1U<<bpp)-1);
                        else if(format==XYBitmap)value=bits[v*stride+u/8]&(1U<<(u%8))?gc.fgPixel:gc.bgPixel;
                        else for(unsigned plane=0;plane<bpp;++plane)value|=!!(bits[plane*stride*7+v*stride+u/8]&(1U<<(u%8)))<<(bpp-1-plane);
                        expected[i]=(expected[i]&~mask)|(oracle(alu,value,expected[i])&mask);
                    }
                    gc.ops->PutImage(&d->drawable,&gc,depth,5,2,33,7,pad,format,(char*)bits);coverage_expect(&dv,expected);
                }
                DDXPointRec points[]={{5,2},{9,4}};int widths[]={33,13};unsigned offset=0;
                for(unsigned row=0;row<2;++row){unsigned stride=PixmapBytePad(widths[row],bpp);
                    for(unsigned x=0;x<(unsigned)widths[row];++x){unsigned i=points[row].y*53+points[row].x+x;
                        unsigned value=raw_value(bits+offset,physical,x)&((1U<<bpp)-1);
                        expected[i]=(expected[i]&~mask)|(oracle(alu,value,expected[i])&mask);}
                    offset+=stride;
                }
                gc.ops->SetSpans(&d->drawable,&gc,(char*)bits,points,widths,2,0);coverage_expect(&dv,expected);
            }else{
                unsigned stride=PixmapBytePad(37,bpp);memset(reply,0xa7,sizeof(reply));
                ps2_readback_image(ctx,&d->drawable,7,3,37,5,ZPixmap,mask,(char*)reply);
                for(unsigned y=0;y<5;++y)for(unsigned x=0;x<37;++x){unsigned wanted=expected[(y+3)*53+x+7]&mask;
                    unsigned got=raw_value(reply+y*stride,physical,x);assert(got==wanted);}
                for(unsigned i=stride*5;i<sizeof(reply);++i)assert(reply[i]==0xa7);
                DDXPointRec points[]={{3,2},{5,4}};int widths[]={13,33};
                memset(reply,0xa7,sizeof(reply));ps2_readback_spans(ctx,&d->drawable,33,points,widths,2,(char*)reply);
                unsigned offset=0;
                for(unsigned row=0;row<2;++row){for(unsigned x=0;x<(unsigned)widths[row];++x){
                    unsigned got=raw_value(reply+offset,physical,x);
                    assert(got==expected[points[row].y*53+points[row].x+x]);}
                    offset+=PixmapBytePad(widths[row],bpp);}
                for(unsigned i=offset;i<sizeof(reply);++i)assert(reply[i]==0xa7);
            }
            assert(d->drawable.depth==bpp && d->drawable.bitsPerPixel==physical && !d->devPrivate.ptr);
        }
        xs.DestroyPixmap(s);xs.DestroyPixmap(d);
        ps2_copy_screen_cleanup(&coverage_copy);ps2_solid_screen_cleanup(&coverage_solid);
        ps2_exa_context_bridge_cleanup(&coverage_bridge);finish_rop();
    }
}

static void component_alpha_case(unsigned bpp,unsigned blend)
{
    rgba_enabled=1;setup_rop(bpp);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    assert(!ps2_surface_destroy(gs,source_handle));assert(!ps2_surface_create(gs,83,61,32,&source_handle));
    assert(!ps2_surface_set_depth(gs,source_handle,32));rgba_seed(model(source_handle));
    if(bpp==32)assert(!ps2_surface_set_depth(gs,destination_handle(),32));
    int32_t coeff[]={2*65536,65536,2*65536,-65536};
    struct ps2_pipe_copy_box box={-1,2,3,2,3,2};
    if(ratio_test_operation(blend)){box.width=box.height=1;coeff[2]=49152;coeff[3]=16384;}
    for(unsigned layout=0;layout<6;++layout){uint32_t mh;
        assert(!ps2_surface_create(gs,13,7,layout<2?16:32,&mh));
        if(layout>=4)assert(!ps2_surface_set_depth(gs,mh,32));
        struct pixels *m=model(mh);
        for(unsigned i=0;i<m->width*m->height;++i){uint32_t v=(i*73)|(((i*37+127)&255)<<8)|(((i*19+73)&255)<<16)|(((i*17+59)&255)<<24);
            m->data[i]=layout<2?((v>>3)&31)|(((v>>11)&31)<<5)|(((v>>19)&31)<<10):layout==5?(v<<8)|(v>>24):v;}
        for(unsigned filter=0;filter<3;++filter)for(unsigned repeat=0;repeat<4;++repeat)
        for(unsigned op=blend?blend:0;op<=(blend?blend:PS2_COMPOSITE_XOR);++op){
            struct ps2_picture_sample s=identity(PS2_REPEAT_REFLECT);s.source_argb=1;s.destination_argb=bpp==32;
            s.source_swap=1;s.destination_swap=1;s.bilinear=filter==1;s.convolution=filter==2;
            s.matrix[0][0]=98304;s.matrix[1][1]=32768;s.matrix[0][2]=-21845;
            if(s.convolution){s.filter_params=coeff;s.filter_nparams=4;}
            struct ps2_picture_bitmap mask={.has_surface=1,.surface=mh,.component_alpha=!hsl_private(blend),.width=13,.height=7,
                .x=-2,.y=1,.sample=identity(repeat)};
            mask.sample.source_swap=layout==1 || layout==3;mask.sample.source_argb=layout>=4;
            mask.sample.source_rotate=layout==5;mask.sample.bilinear=(filter+1)%3==1;
            mask.sample.convolution=(filter+1)%3==2;
            mask.sample.matrix[0][1]=21845;mask.sample.matrix[2][0]=65536/17;
            if(mask.sample.convolution){mask.sample.filter_params=coeff;mask.sample.filter_nparams=4;}
            if(bpp==32)rgba_seed(model(destination_handle()));else seed(model(destination_handle()));
            composite_reference(source_handle,destination_handle(),&box,&s,&mask,op);
        }
        assert(!ps2_surface_destroy(gs,mh));
    }
    if(bpp==32){
        struct ps2_picture_sample s=identity(PS2_REPEAT_REFLECT);s.source_argb=s.destination_argb=1;
        struct ps2_picture_bitmap mask={.has_surface=1,.surface=destination_handle(),.component_alpha=!hsl_private(blend),
            .width=model(destination_handle())->width,.height=model(destination_handle())->height,.sample=identity(PS2_REPEAT_NORMAL)};
        mask.sample.source_argb=1;mask.x=2;mask.y=1;rgba_seed(model(destination_handle()));
        box=(struct ps2_pipe_copy_box){2,1,3,2,71,3};
        composite_reference(source_handle,destination_handle(),&box,&s,&mask,PS2_COMPOSITE_OVER);
    }
    finish_rop();
}
static void coverage_a8_copyplane(void)
{
    for(unsigned source_depth=4;source_depth<=16;source_depth*=2)
    for(unsigned layout=0;layout<3;++layout){unsigned bpp=layout?32:16;
        rgba_enabled=1;setup_rop(bpp);xs.SourceValidate=source_validate;
        struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
        if(layout==2){assert(!ps2_surface_set_depth(gs,destination_handle(),32));
            xp.drawable.depth=window.drawable.depth=gc.depth=32;}
        xp.screen_x=xp.screen_y=0;window.drawable.x=window.drawable.y=0;
        window.realized=TRUE;
        window.drawable.width=xp.drawable.width;window.drawable.height=xp.drawable.height;
        pixman_region_fini(&clip_region);pixman_region_init_rect(&clip_region,2,1,49,11);
        gc.funcs->ValidateGC(&gc,GCAllBits,&window.drawable);gc.fExpose=FALSE;
        PixmapPtr s=coverage_test_pixmap(&xs,53,13,source_depth,0);
        struct ps2_alpha_source bytes;assert(ps2_surface_exa_alpha_source(s,&bytes)==1);
        for(unsigned i=0;i<bytes.bytes;++i)((unsigned char*)bytes.bits)[i]=i*73+19;
        unsigned char *original=malloc(bytes.bytes);assert(original);memcpy(original,bytes.bits,bytes.bytes);
        struct pixels *d=model(destination_handle());size_t n=d->width*d->height;
        uint32_t *expected=malloc(n*4);assert(expected);
        uint32_t bits=layout==2?UINT32_MAX:bpp==16?0x7fff:0xffffff;
        for(unsigned plane=0;plane<source_depth;++plane)for(unsigned alu=0;alu<16;++alu)for(unsigned full=0;full<2;++full){
            seed(d);memcpy(expected,d->data,n*4);gc.alu=alu;
            gc.planemask=full?bits:bits&0x5731a5cf;gc.fgPixel=bits&0x817f3b19;gc.bgPixel=bits&0xf1392b47;
            for(unsigned y=2;y<11;++y)for(unsigned x=5;x<38;++x){unsigned i=y*d->width+x;
                uint32_t value=coverage_byte(&bytes,x-2,y-1)&(1U<<plane)?gc.fgPixel:gc.bgPixel;
                expected[i]=(expected[i]&~gc.planemask)|(oracle(alu,value,expected[i])&gc.planemask);}
            compositor_active=1;
            RegionPtr r=gc.ops->CopyPlane(&s->drawable,&window.drawable,&gc,3,1,33,9,5,2,1U<<plane);
            compositor_active=0;if(r)RegionDestroy(r);
            for(unsigned i=0;i<n;++i)if((expected[i]^d->data[i])&bits){
                fprintf(stderr,"A8 CopyPlane mismatch layout=%u plane=%u alu=%u full=%u x=%u y=%u wanted=%08x got=%08x\n",
                    layout,plane,alu,full,i%d->width,i/d->width,expected[i],d->data[i]);abort();}
            assert(!memcmp(original,bytes.bits,bytes.bytes));
        }
        free(expected);free(original);coverage_test_destroy(s);finish_rop();
    }
}
static void rgba_alias_boundaries(void)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    assert(!ps2_surface_set_depth(gs,source_handle,32));rgba_seed(model(source_handle));
    const unsigned widths[]={1,2,3,7,63,64,65,71};
    for(unsigned i=0;i<sizeof(widths)/sizeof(*widths);++i)for(unsigned rotate=0;rotate<2;++rotate){
        uint32_t h;unsigned width=widths[i];
        assert(!ps2_surface_create(gs,width,3,32,&h) && !ps2_surface_set_depth(gs,h,32));
        rgba_seed(model(h));
        struct ps2_picture_sample s=identity(PS2_REPEAT_NORMAL);
        s.source_argb=s.destination_argb=1;s.source_rotate=s.destination_rotate=rotate;
        struct ps2_pipe_copy_box box={1,1,0,0,width,3};
        composite_reference(h,h,&box,&s,NULL,PS2_COMPOSITE_SRC);
        rgba_seed(model(h));
        struct ps2_picture_bitmap mask={.has_surface=1,.surface=h,.component_alpha=1,
            .width=width,.height=3,.x=1,.y=1,.sample=identity(PS2_REPEAT_NORMAL)};
        mask.sample.source_argb=1;mask.sample.source_rotate=rotate;
        s.source_rotate=0;box.sx=box.sy=0;
        composite_reference(source_handle,h,&box,&s,&mask,PS2_COMPOSITE_OVER);
        assert(!ps2_surface_destroy(gs,h));
    }
    finish_rop();
}
static void component_alpha_coverage(unsigned bpp)
{
    rgba_enabled=1;setup_rop(bpp);
    struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    if(bpp==32){assert(!ps2_surface_set_depth(gs,source_handle,32));
        assert(!ps2_surface_set_depth(gs,destination_handle(),32));}
    unsigned char bytes[16*7];for(unsigned i=0;i<sizeof(bytes);++i)bytes[i]=i*73+29;
    int32_t coefficients[]={2*65536,65536,2*65536,-65536};
    struct ps2_pipe_copy_box box={-1,2,3,2,7,3};
    const unsigned depths[]={1,4,8};
    for(unsigned kind=0;kind<3;++kind)for(unsigned filter=0;filter<3;++filter)
    for(unsigned repeat=0;repeat<4;++repeat)for(unsigned op=0;op<=PS2_COMPOSITE_XOR;++op){
        unsigned mbpp=depths[kind]==4?8:depths[kind];
        struct ps2_picture_sample s=identity(PS2_REPEAT_REFLECT);
        s.source_argb=s.destination_argb=bpp==32;
        struct ps2_picture_bitmap m={.bits=bytes,.bytes=sizeof(bytes),.stride=mbpp==8?16:4,
            .width=13,.height=7,.bits_per_pixel=mbpp,.alpha_bits=depths[kind],.component_alpha=1,.x=-2,.y=1,.sample=identity(repeat)};
        m.sample.matrix[0][0]=98304;m.sample.matrix[1][1]=32768;m.sample.matrix[0][2]=-21845;
        m.sample.matrix[2][0]=65536/15;m.sample.matrix[2][1]=65536/19;
        s.bilinear=m.sample.bilinear=filter==1;s.convolution=m.sample.convolution=filter==2;
        s.filter_params=m.sample.filter_params=filter==2?coefficients:NULL;
        s.filter_nparams=m.sample.filter_nparams=filter==2?4:0;
        if(bpp==32){rgba_seed(model(source_handle));rgba_seed(model(destination_handle()));}
        else{seed(model(source_handle));seed(model(destination_handle()));}
        composite_reference(source_handle,destination_handle(),&box,&s,&m,op);
    }
    finish_rop();
}
static void component_alpha_exa(void)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    assert(!ps2_surface_set_depth(gs,source_handle,32));assert(!ps2_surface_set_depth(gs,destination_handle(),32));xp.drawable.depth=32;
    struct ps2_exa_context_bridge bridge={0};struct ps2_texture_exa textures={0};struct ps2_blit_exa compositor={0};
    assert(ps2_exa_context_bridge_prepare(&xs,&bridge,&exa,ctx));
    assert(ps2_texture_exa_prepare(&xs,&textures,&exa,ps2_pipe_context_render(ctx)));
    assert(ps2_blit_exa_prepare(&xs,&compositor,&exa,ctx));
    uint32_t mh;assert(!ps2_surface_create(gs,13,7,32,&mh));assert(!ps2_surface_set_depth(gs,mh,32));
    PixmapRec src={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=32,.bitsPerPixel=32,.width=83,.height=61},.devPrivates=(void*)&source_handle};
    PixmapRec mp={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=32,.bitsPerPixel=32,.width=13,.height=7},.devPrivates=(void*)&mh};
    PictureRec a={.pDrawable=&src.drawable,.format=PICT_a8b8g8r8,.filter=PictFilterNearest,.repeat=1,.repeatType=RepeatReflect};
    PictureRec b={.pDrawable=&xp.drawable,.format=PICT_a8b8g8r8,.filter=PictFilterNearest};
    PictureRec m={.pDrawable=&mp.drawable,.format=PICT_a8b8g8r8,.filter=PictFilterBilinear,.repeat=1,.repeatType=RepeatNormal,.componentAlpha=TRUE};
    rgba_seed(model(source_handle));rgba_seed(model(destination_handle()));seed(model(mh));
    struct ps2_picture_sample s=identity(PS2_REPEAT_REFLECT);s.source_argb=s.destination_argb=1;
    struct ps2_picture_bitmap mask={.has_surface=1,.surface=mh,.component_alpha=1,.width=13,.height=7,
        .x=-2,.y=1,.sample=identity(PS2_REPEAT_NORMAL)};mask.sample.bilinear=1;mask.sample.source_argb=1;
    assert(exa.driver.CheckComposite(PictOpOver,&a,&m,&b));
    assert(exa.driver.PrepareComposite(PictOpOver,&a,&m,&b,&src,&mp,&xp));
    struct ps2_pipe_copy_box box={-1,2,3,2,3,2};composite_via_exa=1;
    composite_reference(source_handle,destination_handle(),&box,&s,&mask,PS2_COMPOSITE_OVER);
    assert(!ps2_surface_destroy(gs,mh));ps2_blit_exa_cleanup(&xs,&compositor);ps2_exa_context_bridge_cleanup(&bridge);finish_rop();
}

static pixman_image_t *rgb444_reference(struct pixels *p,uint32_t format,void **memory)
{
    unsigned cpp=PICT_FORMAT_BPP(format)/8,stride=(p->width*cpp+3)&~3U;
    unsigned char *bytes=calloc(p->height,stride);assert(bytes);
    for(unsigned y=0;y<p->height;++y)for(unsigned x=0;x<p->width;++x)
        memcpy(bytes+y*stride+x*cpp,p->data+y*p->width+x,cpp);
    *memory=bytes;
    pixman_image_t *result=pixman_image_create_bits((pixman_format_code_t)format,p->width,p->height,(uint32_t*)bytes,stride);
    assert(result);return result;
}
static void rgb444_codec(void)
{
    rgba_enabled=1;setup_rop(16);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    struct pixels *p=model(source_handle);size_t n=p->width*p->height;
    for(unsigned swap=0;swap<2;++swap){
        uint32_t format=swap?PICT_x4r4g4b4:PICT_x4b4g4r4;
        for(unsigned i=0;i<n;++i)p->data[i]=(i&0xfff)|((i*37&7)<<12);
        void *raw;pixman_image_t *si=rgb444_reference(p,format,&raw);
        uint32_t *rgba=calloc(n,4);assert(rgba);
        pixman_image_t *di=pixman_image_create_bits(PIXMAN_x8b8g8r8,p->width,p->height,rgba,p->width*4);assert(di);
        pixman_image_composite32(PIXMAN_OP_SRC,si,NULL,di,0,0,0,0,0,0,p->width,p->height);
        struct ps2_format_surface f={0};compositor_active=1;
        assert(!ps2_format_decode444(&f,gs,source_handle,swap));
        struct pixels *decoded=model(f.surface);
        for(unsigned i=0;i<n;++i)assert(!((rgba[i]^decoded->data[i])&0xffffff));
        compositor_active=0;pixman_image_unref(si);pixman_image_unref(di);free(raw);
        for(unsigned i=0;i<n;++i)decoded->data[i]=rgba[i]=(i*6173+0x7181a5)^(i*0x30307);
        void *wanted;pixman_image_t *dest=rgb444_reference(p,format,&wanted);
        pixman_image_t *src=pixman_image_create_bits(PIXMAN_x8b8g8r8,p->width,p->height,rgba,p->width*4);assert(src);
        pixman_image_composite32(PIXMAN_OP_SRC,src,NULL,dest,0,0,0,0,0,0,p->width,p->height);
        compositor_active=1;assert(!ps2_format_encode444(&f));compositor_active=0;
        unsigned stride=(p->width*2+3)&~3U;
        for(unsigned y=0;y<p->height;++y)for(unsigned x=0;x<p->width;++x){uint16_t v;
            memcpy(&v,(unsigned char*)wanted+y*stride+x*2,2);unsigned i=y*p->width+x;
            assert(!((v^p->data[i])&0xfff));assert((p->data[i]&0x7000)==((i*37&7)<<12));}
        assert(!ps2_format_release(&f));pixman_image_unref(src);pixman_image_unref(dest);free(wanted);free(rgba);
    }
    finish_rop();
}
static void native15_codec(unsigned guards)
{
    rgba_enabled=1;setup_rop(16);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    struct pixels *p=model(source_handle);unsigned n=p->width*p->height;
    const uint32_t formats[]={PICT_x1r5g5b5,PICT_x1b5g5r5,PICT_x4r4g4b4,PICT_x4b4g4r4};
    if(guards){
        uint32_t invalid_source=0;assert(!ps2_surface_create(gs,13,7,32,&invalid_source));
        for(unsigned invalid=0;invalid<10;++invalid){
            struct ps2_format_surface f={0};uint32_t source=source_handle;unsigned format=PS2_WORD_RGB555,features=gs->caps.features;
            if(invalid==2)source=UINT32_MAX;
            if(invalid==3)source=invalid_source;
            if(invalid==4)format=PS2_WORD_RGB555-1;
            if(invalid==5)format=PS2_WORD_BGR444+1;
            if(invalid==6)gs->caps.features&=~PS2GS_HW_CHANNEL_MAP_BLIT;
            if(invalid==7)gs->caps.features&=~PS2GS_HW_TEXTURE_TRANSFER;
            if(invalid==8)gs->caps.features&=~PS2GS_HW_RGBA_TRANSFER;
            if(invalid==9)gs->caps.features&=~PS2GS_HW_SOLID_LOGIC;
            unsigned before=attempts,draws=transfer_calls;
            assert(ps2_format_decode_native15(invalid==0?NULL:&f,invalid==1?NULL:gs,source,format)<0 &&
                errno==(invalid>=6?ENOTSUP:EINVAL));
            assert(attempts==before && transfer_calls==draws);gs->caps.features=features;
        }
        struct ps2_format_surface f={0};assert(!ps2_format_decode_native15(&f,gs,source_handle,PS2_WORD_RGB555));
        for(unsigned invalid=0;invalid<6;++invalid){struct ps2_format_surface probe=f;
            if(invalid==0)probe.original.handle=UINT32_MAX;
            if(invalid==1)probe.packed=0;
            if(invalid==2)probe.surface=0;
            if(invalid==3)probe.original.width++;
            if(invalid==4)probe.byte_format=PS2_WORD_RGB565;
            if(invalid==5)probe.original.buffer_width++;
            unsigned before=attempts,draws=transfer_calls;
            assert(ps2_format_store_native15(&probe)<0 && errno==EINVAL && attempts==before && transfer_calls==draws);
        }
        assert(!ps2_format_release(&f) && !ps2_surface_destroy(gs,invalid_source));
    }else for(unsigned layout=0;layout<4;++layout)for(unsigned batch=0;batch<7;++batch){
        for(unsigned i=0;i<n;++i)p->data[i]=(batch*n+i)&0x7fff;
        void *raw;pixman_image_t *si=rgb444_reference(p,formats[layout],&raw);
        uint32_t *rgba=calloc(n,4);assert(rgba);
        pixman_image_t *di=pixman_image_create_bits(PIXMAN_x8b8g8r8,p->width,p->height,rgba,p->width*4);assert(di);
        pixman_image_composite32(PIXMAN_OP_SRC,si,NULL,di,0,0,0,0,0,0,p->width,p->height);
        struct ps2_format_surface f={0};compositor_active=1;
        assert(!ps2_format_decode_native15(&f,gs,source_handle,PS2_WORD_RGB555+layout));
        struct pixels *decoded=model(f.surface);
        for(unsigned i=0;i<n;++i)assert(!((rgba[i]^decoded->data[i])&0xffffff));
        assert(!ps2_format_store_native15(&f));
        for(unsigned i=0;i<n;++i)assert(p->data[i]==((batch*n+i)&0x7fff));
        compositor_active=0;
        for(unsigned i=0;i<n;++i)decoded->data[i]=rgba[i]=(i*6173+batch*7919+0x7181a5)^(i*0x30307);
        void *wanted;pixman_image_t *dest=rgb444_reference(p,formats[layout],&wanted),
            *src=pixman_image_create_bits(PIXMAN_x8b8g8r8,p->width,p->height,rgba,p->width*4);assert(src);
        pixman_image_composite32(PIXMAN_OP_SRC,src,NULL,dest,0,0,0,0,0,0,p->width,p->height);
        compositor_active=1;
        for(unsigned y=0;y<p->height;++y)assert(!ps2_format_pack_region(&f,0,y,p->width));
        assert(!ps2_format_store_native15(&f));compositor_active=0;
        unsigned stride=(p->width*2+3)&~3U,mask=layout<2?0x7fff:0xfff;
        for(unsigned y=0;y<p->height;++y)for(unsigned x=0;x<p->width;++x){unsigned i=y*p->width+x;
            uint16_t v;memcpy(&v,(unsigned char*)wanted+y*stride+x*2,2);
            assert(!((v^p->data[i])&mask));
            if(layout>=2)assert((p->data[i]&0x7000)==(((batch*n+i)&0x7fff)&0x7000));
        }
        assert(!ps2_format_release(&f));pixman_image_unref(src);pixman_image_unref(dest);free(wanted);
        pixman_image_unref(si);pixman_image_unref(di);free(raw);free(rgba);
    }
    finish_rop();
}
static void rgb444_exa(void)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    struct ps2_exa_context_bridge bridge={0};struct ps2_texture_exa textures={0};struct ps2_blit_exa compositor={0};
    assert(ps2_exa_context_bridge_prepare(&xs,&bridge,&exa,ctx));
    assert(ps2_texture_exa_prepare(&xs,&textures,&exa,ps2_pipe_context_render(ctx)));
    assert(ps2_blit_exa_prepare(&xs,&compositor,&exa,ctx));
    const uint32_t formats[]={PICT_x4b4g4r4,PICT_x4r4g4b4,PICT_a8b8g8r8,PICT_x1b5g5r5};
    int32_t params[]={2*65536,65536,2*65536,-65536};
    PictTransform transform={.matrix={{98304,0,-21845},{0,32768,21845},{65536/15,65536/19,65536}}};
    for(unsigned sf=0;sf<4;++sf)for(unsigned df=0;df<4;++df){
        if(sf>=2 && df>=2)continue;
        unsigned sbpp=PICT_FORMAT_BPP(formats[sf]),dbpp=PICT_FORMAT_BPP(formats[df]);uint32_t sh,dh;
        assert(!ps2_surface_create(gs,13,7,sbpp,&sh));assert(!ps2_surface_create(gs,19,11,dbpp,&dh));
        if(sbpp==32)assert(!ps2_surface_set_depth(gs,sh,32));
        if(dbpp==32)assert(!ps2_surface_set_depth(gs,dh,32));
        PixmapRec sp={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=sbpp==16?15:32,
            .bitsPerPixel=sbpp,.width=13,.height=7},.devPrivates=(void*)&sh};
        PixmapRec dp={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=dbpp==16?15:32,
            .bitsPerPixel=dbpp,.width=19,.height=11},.devPrivates=(void*)&dh};
        PictureRec s={.pDrawable=&sp.drawable,.format=formats[sf],.filter=PictFilterNearest,.repeat=1,.repeatType=RepeatReflect,.transform=&transform};
        PictureRec d={.pDrawable=&dp.drawable,.format=formats[df],.filter=PictFilterNearest};
        PictureRec m={.pDrawable=&dp.drawable,.format=formats[df],.filter=PictFilterNearest,.repeat=1,.repeatType=RepeatNormal,.componentAlpha=1};
        for(unsigned filter=0;filter<3;++filter)for(unsigned masked=0;masked<2;++masked)for(unsigned op=0;op<=PictOpAdd;++op){
            s.filter=m.filter=filter==2?PictFilterConvolution:filter==1?PictFilterBilinear:PictFilterNearest;
            s.filter_params=m.filter_params=filter==2?params:NULL;s.filter_nparams=m.filter_nparams=filter==2?4:0;
            if(sbpp==32)rgba_seed(model(sh));else seed(model(sh));
            if(dbpp==32)rgba_seed(model(dh));else seed(model(dh));
            void *sm,*dm,*mm;pixman_image_t *si=rgb444_reference(model(sh),formats[sf],&sm),
                *di=rgb444_reference(model(dh),formats[df],&dm),*mi=rgb444_reference(model(dh),formats[df],&mm);
            assert(pixman_image_set_transform(si,(pixman_transform_t*)&transform));
            pixman_image_set_repeat(si,PIXMAN_REPEAT_REFLECT);pixman_image_set_repeat(mi,PIXMAN_REPEAT_NORMAL);pixman_image_set_component_alpha(mi,TRUE);
            pixman_filter_t pf=filter==2?PIXMAN_FILTER_CONVOLUTION:filter==1?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST;
            assert(pixman_image_set_filter(si,pf,filter==2?params:NULL,filter==2?4:0));
            assert(pixman_image_set_filter(mi,pf,filter==2?params:NULL,filter==2?4:0));
            pixman_image_composite32((pixman_op_t)op,si,masked?mi:NULL,di,-1,-1,-2,1,3,2,7,3);
            compositor_active=1;assert(exa.driver.CheckComposite(op,&s,masked?&m:NULL,&d));
            assert(exa.driver.PrepareComposite(op,&s,masked?&m:NULL,&d,&sp,masked?&dp:NULL,&dp));
            exa.driver.Composite(&dp,-1,-1,-2,1,3,2,7,3);exa.driver.DoneComposite(&dp);compositor_active=0;
            unsigned cpp=dbpp/8,stride=(19*cpp+3)&~3U,mask=df<2?0xfff:dbpp==16?0x7fff:UINT32_MAX;
            for(unsigned y=0;y<11;++y)for(unsigned x=0;x<19;++x){uint32_t v=0;memcpy(&v,(unsigned char*)dm+y*stride+x*cpp,cpp);
                if((v^model(dh)->data[y*19+x])&mask){fprintf(stderr,"RGB444 EXA mismatch s=%u d=%u filter=%u CA=%u op=%u x=%u y=%u expected=%08x got=%08x\n",
                    sf,df,filter,masked,op,x,y,v,model(dh)->data[y*19+x]);abort();}}
            pixman_image_unref(si);pixman_image_unref(mi);pixman_image_unref(di);free(sm);free(mm);free(dm);
        }
        assert(!ps2_surface_destroy(gs,sh));assert(!ps2_surface_destroy(gs,dh));
    }
    ps2_blit_exa_cleanup(&xs,&compositor);ps2_exa_context_bridge_cleanup(&bridge);finish_rop();
}
static void byte_colour_codec(void)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    const pixman_format_code_t formats[]={PIXMAN_r3g3b2,PIXMAN_b2g3r3,PIXMAN_a2r2g2b2,PIXMAN_a2b2g2r2};
    unsigned char bytes[36*9];uint32_t rgba[33*9];
    for(unsigned format=0;format<4;++format){
        for(unsigned i=0;i<sizeof(bytes);++i)bytes[i]=i;
        pixman_image_t *si=pixman_image_create_bits(formats[format],33,9,(uint32_t*)bytes,36),
            *di=pixman_image_create_bits(PIXMAN_a8b8g8r8,33,9,rgba,33*4);assert(si && di);
        pixman_image_composite32(PIXMAN_OP_SRC,si,NULL,di,0,0,0,0,0,0,33,9);
        struct ps2_picture_bitmap raw={.bits=bytes,.bytes=sizeof(bytes),.stride=36,.width=33,.height=9,.bits_per_pixel=8};
        struct ps2_format_surface f={0};compositor_active=1;
        assert(!ps2_format_decode8(&f,gs,&raw,format));compositor_active=0;
        struct pixels *decoded=model(f.surface);unsigned mask=format<2?0xffffff:UINT32_MAX;
        for(unsigned i=0;i<33*9;++i)assert(!((rgba[i]^decoded->data[i])&mask));
        pixman_image_unref(si);pixman_image_unref(di);
        rgba_seed(decoded);for(unsigned i=0;i<33*9;++i)rgba[i]=decoded->data[i];
        unsigned char expected[sizeof(bytes)];memcpy(expected,bytes,sizeof(bytes));
        si=pixman_image_create_bits(PIXMAN_a8b8g8r8,33,9,rgba,33*4);
        di=pixman_image_create_bits(formats[format],33,9,(uint32_t*)expected,36);assert(si && di);
        pixman_image_composite32(PIXMAN_OP_SRC,si,NULL,di,0,0,0,0,0,0,33,9);
        assert(!ps2_format_encode8(&f,ctx,bytes,sizeof(bytes),36));assert(!memcmp(bytes,expected,sizeof(bytes)));
        assert(!ps2_format_release(&f));pixman_image_unref(si);pixman_image_unref(di);
    }
    finish_rop();
}
static void word_colour_codec(void)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    const pixman_format_code_t formats[]={PIXMAN_r5g6b5,PIXMAN_b5g6r5,PIXMAN_a1r5g5b5,PIXMAN_a1b5g5r5,
        PIXMAN_a4r4g4b4,PIXMAN_a4b4g4r4,PIXMAN_x1r5g5b5,PIXMAN_x1b5g5r5,PIXMAN_x4r4g4b4,PIXMAN_x4b4g4r4};
    const unsigned width=257,height=256,stride=516;size_t n=width*height;
    unsigned char *bytes=malloc(stride*height),*expected=malloc(stride*height);uint32_t *rgba=malloc(n*4);assert(bytes && expected && rgba);
    for(unsigned format=0;format<10;++format){
        memset(bytes,0xa7,stride*height);
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){uint16_t value=y*width+x;memcpy(bytes+y*stride+x*2,&value,2);}
        pixman_image_t *si=pixman_image_create_bits(formats[format],width,height,(uint32_t*)bytes,stride),
            *di=pixman_image_create_bits(PIXMAN_a8b8g8r8,width,height,rgba,width*4);assert(si && di);
        pixman_image_composite32(PIXMAN_OP_SRC,si,NULL,di,0,0,0,0,0,0,width,height);
        struct ps2_picture_bitmap raw={.bits=bytes,.bytes=stride*height,.stride=stride,.width=width,.height=height,.bits_per_pixel=16};
        struct ps2_format_surface f={0};compositor_active=1;
        assert(!ps2_format_decode16(&f,gs,&raw,PS2_WORD_RGB565+format));compositor_active=0;
        struct pixels *decoded=model(f.surface);unsigned mask=format>=2 && format<6?UINT32_MAX:0xffffff;
        for(unsigned i=0;i<n;++i)if((rgba[i]^decoded->data[i])&mask){
            fprintf(stderr,"word decode format=%u i=%u wanted=%08x got=%08x\n",format,i,rgba[i],decoded->data[i]);abort();}
        pixman_image_unref(si);pixman_image_unref(di);
        rgba_seed(decoded);memcpy(rgba,decoded->data,n*4);memcpy(expected,bytes,stride*height);
        si=pixman_image_create_bits(PIXMAN_a8b8g8r8,width,height,rgba,width*4);
        di=pixman_image_create_bits(formats[format],width,height,(uint32_t*)expected,stride);assert(si && di);
        pixman_image_composite32(PIXMAN_OP_SRC,si,NULL,di,0,0,0,0,0,0,width,height);
        assert(!ps2_format_encode16(&f,ctx,bytes,stride*height,stride));
        unsigned word_mask=format>=8?0xfff:format>=6?0x7fff:0xffff;
        for(unsigned y=0;y<height;++y){for(unsigned x=0;x<width;++x){
            unsigned value=raw_value(bytes+y*stride,16,x),wanted=raw_value(expected+y*stride,16,x);
            assert(!((value^wanted)&word_mask));assert((value&~word_mask)==((y*width+x)&0xffff&~word_mask));}
            assert(bytes[y*stride+width*2]==0xa7 && bytes[y*stride+width*2+1]==0xa7);}
        assert(!ps2_format_release(&f));pixman_image_unref(si);pixman_image_unref(di);
    }
    free(bytes);free(expected);free(rgba);finish_rop();
}
static void byte_colour_exa(unsigned word,unsigned saturate)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    assert(!ps2_surface_set_depth(gs,source_handle,32));rgba_seed(model(source_handle));
    struct ps2_exa_context_bridge bridge={0};struct ps2_texture_exa textures={0};struct ps2_blit_exa compositor={0};
    assert(ps2_exa_context_bridge_prepare(&xs,&bridge,&exa,ctx));
    assert(ps2_texture_exa_prepare(&xs,&textures,&exa,ps2_pipe_context_render(ctx)));
    assert(ps2_blit_exa_prepare(&xs,&compositor,&exa,ctx));
    const uint32_t byte_formats[]={PICT_r3g3b2,PICT_b2g3r3,PICT_a2r2g2b2,PICT_a2b2g2r2};
    const uint32_t word_formats[]={PICT_r5g6b5,PICT_b5g6r5,PICT_a1r5g5b5,PICT_a1b5g5r5,
        PICT_a4r4g4b4,PICT_a4b4g4r4,PICT_x1r5g5b5,PICT_x1b5g5r5,PICT_x4r4g4b4,PICT_x4b4g4r4};
    const uint32_t *formats=word?word_formats:byte_formats;
    PixmapRec sp={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=32,.bitsPerPixel=32,.width=83,.height=61},.devPrivates=(void*)&source_handle};
    PictureRec source={.pDrawable=&sp.drawable,.format=PICT_a8b8g8r8,.filter=PictFilterNearest,.repeat=1,.repeatType=RepeatReflect};
    int32_t params[]={2*65536,65536,2*65536,-65536};
    if((saturate==17 || saturate>=100)){params[2]=49152;params[3]=16384;}
    PictTransform transform={.matrix={{98304,0,-21845},{0,32768,21845},{65536/15,65536/19,65536}}};
    for(unsigned format=0;format<(word?10U:4U);++format){
        PixmapPtr bp=coverage_test_pixmap(&xs,13,7,word?16:8,0);struct ps2_alpha_source bv;
        assert(ps2_surface_exa_alpha_source(bp,&bv)==1);
        PictureRec byte={.pDrawable=&bp->drawable,.format=formats[format],.filter=PictFilterNearest};
        PictureRec mask=byte;mask.repeat=1;mask.repeatType=RepeatNormal;mask.componentAlpha=!(saturate>=149 && saturate<=152);
        for(unsigned reverse=0;reverse<2;++reverse)for(unsigned filter=saturate==2?1:0;filter<(saturate==2?2U:saturate==1?1U:3U);++filter)
        for(unsigned masked=0;masked<2;++masked)for(unsigned op=saturate>=100?ratio_test_wire(saturate-100):saturate==11?PictOpDisjointClear:saturate==12?PictOpDisjointSrc:saturate==13?PictOpDisjointDst:saturate==14?PictOpConjointClear:saturate==15?PictOpConjointSrc:saturate==16?PictOpConjointDst:(saturate==17 || saturate>=100)?PictOpDisjointOverReverse:saturate==9?PictOpOverlay:saturate==10?PictOpHardLight:saturate==6?PictOpDarken:saturate==7?PictOpLighten:saturate==8?PictOpDifference:saturate==5?PictOpMultiply:saturate==4?PictOpExclusion:saturate==3?PictOpScreen:saturate?PictOpSaturate:0;op<=(saturate>=100?ratio_test_wire(saturate-100):saturate==11?PictOpDisjointClear:saturate==12?PictOpDisjointSrc:saturate==13?PictOpDisjointDst:saturate==14?PictOpConjointClear:saturate==15?PictOpConjointSrc:saturate==16?PictOpConjointDst:(saturate==17 || saturate>=100)?PictOpDisjointOverReverse:saturate==9?PictOpOverlay:saturate==10?PictOpHardLight:saturate==6?PictOpDarken:saturate==7?PictOpLighten:saturate==8?PictOpDifference:saturate==5?PictOpMultiply:saturate==4?PictOpExclusion:saturate==3?PictOpScreen:saturate?PictOpSaturate:PictOpAdd);++op){
            for(unsigned i=0;i<bv.bytes;++i)((unsigned char*)bv.bits)[i]=i*73+19;
            rgba_seed(model(source_handle));
            PicturePtr s=reverse?&byte:&source,d=reverse?&source:&byte;
            s->transform=&transform;s->filter=mask.filter=filter==2?PictFilterConvolution:filter==1?PictFilterBilinear:PictFilterNearest;
            s->filter_params=mask.filter_params=filter==2?params:NULL;s->filter_nparams=mask.filter_nparams=filter==2?4:0;
            d->transform=NULL;d->filter=PictFilterNearest;d->filter_params=NULL;d->filter_nparams=0;
            d->repeat=FALSE;s->repeat=TRUE;s->repeatType=RepeatReflect;
            void *native;pixman_image_t *ni=rgb444_reference(model(source_handle),PICT_a8b8g8r8,&native);
            unsigned char expected[28*7];memcpy(expected,bv.bits,bv.bytes);
            pixman_image_t *bi=pixman_image_create_bits((pixman_format_code_t)formats[format],13,7,(uint32_t*)expected,bv.stride),
                *mi=pixman_image_create_bits((pixman_format_code_t)formats[format],13,7,(uint32_t*)bv.bits,bv.stride);assert(bi && mi);
            pixman_image_t *si=reverse?bi:ni,*di=reverse?ni:bi;
            assert(pixman_image_set_transform(si,(pixman_transform_t*)&transform));pixman_image_set_repeat(si,PIXMAN_REPEAT_REFLECT);
            pixman_filter_t pf=filter==2?PIXMAN_FILTER_CONVOLUTION:filter==1?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST;
            assert(pixman_image_set_filter(si,pf,filter==2?params:NULL,filter==2?4:0));
            assert(pixman_image_set_filter(mi,pf,filter==2?params:NULL,filter==2?4:0));
            pixman_image_set_repeat(mi,PIXMAN_REPEAT_NORMAL);pixman_image_set_component_alpha(mi,mask.componentAlpha);
            unsigned width=(saturate==17 || saturate>=100)?1:saturate==2?2:7,height=(saturate==17 || saturate>=100) || saturate==2?1:3;
            pixman_image_composite32((pixman_op_t)op,si,masked?mi:NULL,di,-1,-1,-2,1,3,2,width,height);
            compositor_active=1;assert(exa.driver.CheckComposite(op,s,masked?&mask:NULL,d));
            if(!exa.driver.PrepareComposite(op,s,masked?&mask:NULL,d,reverse?bp:&sp,masked?bp:NULL,reverse?&sp:bp)){
                fprintf(stderr,"packed EXA prepare format=%u word=%u reverse=%u masked=%u op=%u errno=%d\n",
                    format,word,reverse,masked,op,errno);abort();}
            exa.driver.Composite(reverse?&sp:bp,-1,-1,-2,1,3,2,width,height);compositor_active=0;
            exa.driver.DoneComposite(reverse?&sp:bp);
            if(!reverse){
                unsigned pixel_mask=word?(format>=8?0xfff:format>=6?0x7fff:0xffff):0xff;
                for(unsigned y=0;y<7;++y)for(unsigned x=0;x<13;++x)
                    assert(!((raw_value(expected+y*bv.stride,bv.bits_per_pixel,x)^coverage_byte(&bv,x,y))&pixel_mask));
            }
            else for(unsigned y=0;y<61;++y)for(unsigned x=0;x<83;++x){uint32_t v;
                memcpy(&v,(unsigned char*)native+(y*83+x)*4,4);
                if(v!=model(source_handle)->data[y*83+x]){
                    fprintf(stderr,"packed EXA pixel format=%u word=%u reverse=%u masked=%u filter=%u op=%u x=%u y=%u wanted=%08x got=%08x\n",
                        format,word,reverse,masked,filter,op,x,y,v,model(source_handle)->data[y*83+x]);
                    float fetched[4]={0};pixman_image_t *fi=pixman_image_create_bits(PIXMAN_rgba_float,1,1,(uint32_t*)fetched,16);
                    assert(fi);pixman_image_composite32(PIXMAN_OP_SRC,si,NULL,fi,(int)x-4,(int)y-3,0,0,0,0,1,1);
                    unsigned index=y*83+x,a=(index*73+29)&255;
                    fprintf(stderr,"source float=%g,%g,%g,%g prior=%08x bits=%u,%u,%u,%u\n",
                        fetched[0],fetched[1],fetched[2],fetched[3],(a<<24)|mul255((index*17+191)&255,a)|
                        (mul255((index*37+13)&255,a)<<8)|(mul255((index*53+127)&255,a)<<16),
                        compositor.sample.source_unorm.rgb_bits[0],compositor.sample.source_unorm.rgb_bits[1],
                        compositor.sample.source_unorm.rgb_bits[2],compositor.sample.source_unorm.alpha_bits);
                    pixman_image_unref(fi);abort();}}
            pixman_image_unref(ni);pixman_image_unref(bi);pixman_image_unref(mi);free(native);
        }
        coverage_test_destroy(bp);
    }
    ps2_blit_exa_cleanup(&xs,&compositor);ps2_exa_context_bridge_cleanup(&bridge);finish_rop();
}
static void borrowed_colour_case(unsigned word)
{
    rgba_enabled=1;setup_rop(32);
    struct ps2_exa_context_bridge bridge={0};struct ps2_texture_exa textures={0};struct ps2_blit_exa compositor={0};
    assert(ps2_exa_context_bridge_prepare(&xs,&bridge,&exa,ctx));
    assert(ps2_texture_exa_prepare(&xs,&textures,&exa,ps2_pipe_context_render(ctx)));
    assert(ps2_blit_exa_prepare(&xs,&compositor,&exa,ctx));
    const uint32_t byte_formats[]={PICT_r3g3b2,PICT_b2g3r3,PICT_a2r2g2b2,PICT_a2b2g2r2};
    const uint32_t word_formats[]={PICT_r5g6b5,PICT_b5g6r5,PICT_a1r5g5b5,PICT_a1b5g5r5,
        PICT_a4r4g4b4,PICT_a4b4g4r4,PICT_x1r5g5b5,PICT_x1b5g5r5,PICT_x4r4g4b4,PICT_x4b4g4r4};
    const uint32_t *formats=word?word_formats:byte_formats;
    unsigned stride=word?16:8,depth=word?16:8;
    for(unsigned f=0;f<(word?10U:4U);++f){
        unsigned char data[80],saved[80];for(unsigned i=0;i<80;++i)data[i]=saved[i]=i*73+19;
        PixmapPtr input=coverage_test_pixmap(&xs,0,0,1,0),output=coverage_test_pixmap(&xs,7,5,depth,0);
        assert(exa.driver.ModifyPixmapHeader(input,7,5,depth,depth,-1,data));input->devPrivate.ptr=NULL;
        struct ps2_alpha_source borrowed,destination;
        assert(ps2_surface_exa_alpha_source(input,&borrowed)==0 && ps2_surface_exa_borrowed_source(input,&borrowed)==1);
        assert(borrowed.bits==data && borrowed.stride==stride && borrowed.bytes==stride*5);
        assert(ps2_surface_exa_alpha_source(output,&destination)==1);
        PictureRec s={.pDrawable=&input->drawable,.format=formats[f],.filter=PictFilterNearest};
        PictureRec d={.pDrawable=&output->drawable,.format=formats[f],.filter=PictFilterNearest};
        compositor_active=1;assert(exa.driver.CheckComposite(PictOpSrc,&s,NULL,&d));
        assert(exa.driver.PrepareComposite(PictOpSrc,&s,NULL,&d,input,NULL,output));
        exa.driver.Composite(output,0,0,0,0,0,0,7,5);compositor_active=0;exa.driver.DoneComposite(output);
        unsigned mask=word?(f>=8?0xfff:f>=6?0x7fff:0xffff):0xff;
        for(unsigned y=0;y<5;++y)for(unsigned x=0;x<7;++x)
            assert(!((raw_value(data+y*stride,depth,x)^coverage_byte(&destination,x,y))&mask));
        assert(!memcmp(data,saved,80));
        /* A protocol staging pointer remains read-only even after successful
         * native sampling. It must never be accepted as a render target. */
        assert(!exa.driver.PrepareComposite(PictOpSrc,&d,NULL,&s,output,NULL,input));
        assert(!memcmp(data,saved,80) && !ps2_pipe_context_render(ctx)->gs->error);
        coverage_test_destroy(input);coverage_test_destroy(output);
    }
    ps2_blit_exa_cleanup(&xs,&compositor);ps2_exa_context_bridge_cleanup(&bridge);finish_rop();
    printf("PS2 borrowed colour glyph formats=%u readonly_destination_rejected=1 PASS\n",word?10:4);
}
static void alpha_map_aliases(void)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    assert(!ps2_surface_set_depth(gs,source_handle,32) && !ps2_surface_set_depth(gs,destination_handle(),32));
    uint32_t output=0;assert(!ps2_surface_create(gs,83,61,32,&output) && !ps2_surface_set_depth(gs,output,32));
    int32_t params[]={2*65536,65536,2*65536,-65536};
    for(unsigned alias=0;alias<3;++alias)for(unsigned filter=0;filter<3;++filter)
    for(unsigned op=0;op<=PS2_COMPOSITE_XOR;++op){
        rgba_seed(model(source_handle));rgba_seed(model(destination_handle()));rgba_seed(model(output));
        struct ps2_picture_sample sample=identity(PS2_REPEAT_REFLECT);
        sample.source_argb=sample.destination_argb=1;
        sample.bilinear=filter==1;sample.convolution=filter==2;
        if(filter==2){sample.filter_params=params;sample.filter_nparams=4;}
        sample.matrix[0][0]=98304;sample.matrix[0][2]=-21845;
        const struct pixels *original=model(destination_handle());
        sample.source_alpha_map=(struct ps2_picture_alpha_map){.enabled=1,.surface=destination_handle(),
            .width=original->width,.height=original->height,.has_alpha=1,.x=-3,.y=1};
        sample.destination_alpha_map=(struct ps2_picture_alpha_map){.enabled=1,.surface=output,
            .width=83,.height=61,.has_alpha=1,.x=-1,.y=-1};
        struct ps2_picture_bitmap mask={.has_surface=1,.surface=source_handle,.width=83,.height=61,
            .x=5,.y=3,.component_alpha=alias==2,.sample={.source_argb=1,.repeat=PS2_REPEAT_REFLECT,
            .source_alpha_map={.enabled=1,.surface=output,.width=83,.height=61,.has_alpha=1}}};
        struct ps2_pipe_copy_box box={2,1,5,4,71,3};
        composite_reference(alias==1?output:source_handle,destination_handle(),&box,&sample,&mask,op);
    }
    assert(!ps2_surface_destroy(gs,output));finish_rop();
}
static void alpha_map_guards(void)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    assert(!ps2_surface_set_depth(gs,source_handle,32));
    struct pixels *s=model(source_handle);
    struct ps2_picture_alpha_map map={.enabled=1,.surface=source_handle,.width=s->width,.height=s->height,.has_alpha=1};
    struct ps2_pipe_copy_box box={0,0,3,2,5,3};
    for(unsigned kind=0;kind<9;++kind){
        struct ps2_picture_sample sample={.source_alpha_map=map};
        switch(kind){case 0:sample.source_alpha_map.enabled=2;break;
        case 1:sample.source_alpha_map.surface=UINT32_MAX;break;
        case 2:--sample.source_alpha_map.width;break;
        case 3:sample.source_alpha_map.has_alpha=2;break;
        case 4:sample.source_alpha_map.has_alpha=0;sample.source_alpha_map.alpha_only=1;break;
        case 5:sample.source_alpha_map.rotate=sample.source_alpha_map.swap=1;break;
        case 6:sample.source_alpha_map.alpha_only=1;sample.source_alpha_map.alpha_bits=2;break;
        case 7:sample.destination_alpha_map=map;sample.destination_alpha_map.x=10;break;
        case 8:sample.destination_alpha_map=map;sample.destination_alpha_map.y=10;break;}
        unsigned before=attempts,copies=calls;
        assert(ps2_pipe_composite_rect(ctx,source_handle,destination_handle(),&box,&sample,NULL,PS2_COMPOSITE_SRC)<0);
        assert(errno==EINVAL && attempts==before && calls==copies);
    }
    finish_rop();
}
static void alpha_map_exa(unsigned windows)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    struct ps2_exa_context_bridge bridge={0};struct ps2_texture_exa texture={0};struct ps2_blit_exa blit={0};
    assert(ps2_exa_context_bridge_prepare(&xs,&bridge,&exa,ctx));
    assert(ps2_texture_exa_prepare(&xs,&texture,&exa,ps2_pipe_context_render(ctx)));
    assert(ps2_blit_exa_prepare(&xs,&blit,&exa,ctx));
    PixmapRec source={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=24,.bitsPerPixel=32,
        .width=83,.height=61},.devPrivates=(void*)&source_handle};
    uint32_t mask_handle=0,reference_handle=0;
    assert(!ps2_surface_create(gs,31,23,32,&mask_handle) && !ps2_surface_set_depth(gs,mask_handle,32));
    assert(!ps2_surface_create(gs,31,23,32,&reference_handle) && !ps2_surface_set_depth(gs,reference_handle,32));
    PixmapRec mask_pixmap={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=32,.bitsPerPixel=32,
        .width=31,.height=23},.devPrivates=(void*)&mask_handle};
    PictureRec s={.pDrawable=&source.drawable,.format=PICT_x8b8g8r8,.filter=PictFilterNearest,
        .repeat=1,.repeatType=RepeatReflect};
    PictureRec m={.pDrawable=&mask_pixmap.drawable,.format=PICT_a8b8g8r8,.filter=PictFilterNearest,
        .repeat=1,.repeatType=RepeatReflect};
    PictureRec d={.pDrawable=&xp.drawable,.format=PICT_x8b8g8r8,.filter=PictFilterNearest};
    WindowRec source_window={.drawable=source.drawable},mask_window={.drawable=mask_pixmap.drawable},destination_window={.drawable=xp.drawable};
    if(windows){
        source_window.drawable.type=mask_window.drawable.type=destination_window.drawable.type=DRAWABLE_WINDOW;
        source_window.drawable.x=7;source_window.drawable.y=5;source.screen_x=3;source.screen_y=2;
        mask_window.drawable.x=4;mask_window.drawable.y=3;mask_pixmap.screen_x=2;mask_pixmap.screen_y=1;
        destination_window.drawable.x=9;destination_window.drawable.y=7;xp.screen_x=xp.screen_y=1;
        s.pDrawable=&source_window.drawable;m.pDrawable=&mask_window.drawable;d.pDrawable=&destination_window.drawable;
        s.repeat=m.repeat=FALSE;
    }
    const unsigned depths[]={1,4,8};
    int32_t params[]={2*65536,65536,2*65536,-65536};
    for(unsigned kind=0;kind<3;++kind){
        PixmapPtr pixmap=coverage_test_pixmap(&xs,31,23,depths[kind],0);
        struct ps2_alpha_source bytes;assert(ps2_surface_exa_alpha_source(pixmap,&bytes)==1);
        PictureRec map={.pDrawable=&pixmap->drawable,.format=kind==0?PICT_a1:kind==1?PICT_x4a4:PICT_a8,
            .filter=PictFilterBilinear,.repeat=1,.repeatType=RepeatReflect};
        for(unsigned role=0;role<4;++role)for(unsigned filter=0;filter<(windows?1U:3U);++filter)for(unsigned op=0;op<=PictOpAdd;++op){
            seed(model(source_handle));seed(model(destination_handle()));rgba_seed(model(mask_handle));
            for(unsigned i=0;i<bytes.bytes;++i)((unsigned char*)bytes.bits)[i]=i*73+29;
            alpha_reply_pixmap=pixmap;alpha_reply_surface=reference_handle;sync_alpha_reply();alpha_reply_pixmap=NULL;
            struct ps2_picture_sample sample=identity(PS2_REPEAT_REFLECT);
            sample.matrix[0][0]=98304;sample.matrix[1][1]=32768;sample.matrix[0][2]=-21845;
            sample.matrix[2][0]=65536/15;
            if(windows)sample=(struct ps2_picture_sample){0};
            sample.bilinear=filter==1;sample.convolution=filter==2;
            if(filter==2){sample.filter_params=params;sample.filter_nparams=4;}
            PictTransform transform;memcpy(transform.matrix,sample.matrix,sizeof(transform.matrix));s.transform=&transform;
            if(windows)s.transform=NULL;
            s.filter=filter==2?PictFilterConvolution:filter==1?PictFilterBilinear:PictFilterNearest;
            s.filter_params=filter==2?params:NULL;s.filter_nparams=filter==2?4:0;
            s.alphaMap=role==0 || role==3?&map:NULL;m.alphaMap=role==1 || role==3?&map:NULL;
            d.alphaMap=role>=2?&map:NULL;
            s.alphaOrigin=(DDXPointRec){3,-1};m.alphaOrigin=(DDXPointRec){3,-1};d.alphaOrigin=(DDXPointRec){3,-1};
            struct ps2_picture_alpha_map descriptor={.enabled=1,.surface=reference_handle,
                .width=31,.height=23,.has_alpha=1,.alpha_only=1,.alpha_bits=depths[kind],.x=3,.y=-1};
            if(s.alphaMap){sample.source_alpha_map=descriptor;
                if(windows){sample.source_alpha_map.x+=7-3;sample.source_alpha_map.y+=5-2;}}
            if(d.alphaMap){sample.destination_alpha_map=descriptor;
                if(windows){sample.destination_alpha_map.x+=9-1;sample.destination_alpha_map.y+=7-1;}}
            struct ps2_picture_bitmap mask={.has_surface=1,.surface=mask_handle,.width=31,.height=23,
                .x=4,.y=2,.sample={.repeat=PS2_REPEAT_REFLECT,.source_argb=1}};
            if(m.alphaMap)mask.sample.source_alpha_map=descriptor;
            if(windows){mask.sample.repeat=PS2_REPEAT_NONE;
                if(m.alphaMap){mask.sample.source_alpha_map.x+=4-2;mask.sample.source_alpha_map.y+=3-1;}}
            static const unsigned operations[]={PS2_COMPOSITE_CLEAR,PS2_COMPOSITE_SRC,PS2_COMPOSITE_DST,
                PS2_COMPOSITE_OVER,PS2_COMPOSITE_OVER_REVERSE,PS2_COMPOSITE_IN,PS2_COMPOSITE_IN_REVERSE,
                PS2_COMPOSITE_OUT,PS2_COMPOSITE_OUT_REVERSE,PS2_COMPOSITE_ATOP,PS2_COMPOSITE_ATOP_REVERSE,
                PS2_COMPOSITE_XOR,PS2_COMPOSITE_ADD};
            assert(exa.driver.CheckComposite(op,&s,&m,&d));
            assert(exa.driver.PrepareComposite(op,&s,&m,&d,&source,&mask_pixmap,&xp));
            if(d.alphaMap){alpha_reply_pixmap=pixmap;alpha_reply_surface=reference_handle;}
            composite_via_exa=1;struct ps2_pipe_copy_box box=windows?
                (struct ps2_pipe_copy_box){14,8,15,9,5,3}:(struct ps2_pipe_copy_box){2,1,5,4,5,3};
            composite_reference(source_handle,destination_handle(),&box,&sample,&mask,operations[op]);
            composite_via_exa=0;alpha_reply_pixmap=NULL;
        }
        assert(!s.alphaMap || s.alphaMap==&map);
        coverage_test_destroy(pixmap);
    }
    s.alphaMap=m.alphaMap=d.alphaMap=NULL;
    ps2_blit_exa_cleanup(&xs,&blit);ps2_exa_context_bridge_cleanup(&bridge);
    assert(!ps2_surface_destroy(gs,reference_handle) && !ps2_surface_destroy(gs,mask_handle));
    finish_rop();
}
static void alpha_map_raw_alias(unsigned shifted)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    assert(!ps2_surface_set_depth(gs,source_handle,32));
    struct ps2_exa_context_bridge bridge={0};struct ps2_texture_exa texture={0};struct ps2_blit_exa blit={0};
    assert(ps2_exa_context_bridge_prepare(&xs,&bridge,&exa,ctx));
    assert(ps2_texture_exa_prepare(&xs,&texture,&exa,ps2_pipe_context_render(ctx)));
    assert(ps2_blit_exa_prepare(&xs,&blit,&exa,ctx));
    const PictFormatShort formats[]={PICT_a1,PICT_x4a4,PICT_a8,PICT_r3g3b2,PICT_b2g3r3,
        PICT_a2r2g2b2,PICT_a2b2g2r2,PICT_r5g6b5,PICT_b5g6r5,PICT_a1r5g5b5,PICT_a1b5g5r5,
        PICT_a4r4g4b4,PICT_a4b4g4r4,PICT_x1r5g5b5,PICT_x1b5g5r5,PICT_x4r4g4b4,PICT_x4b4g4r4};
    PixmapRec sp={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=32,.bitsPerPixel=32,
        .width=83,.height=61},.devPrivates=(void*)&source_handle};
    PictureRec source={.pDrawable=&sp.drawable,.format=PICT_a8b8g8r8,.repeat=TRUE,.repeatType=RepeatReflect};
    PictTransform transform={.matrix={{98304,0,-21845},{0,32768,21845},{0,0,65536}}};source.transform=&transform;
    int32_t params[]={2*65536,65536,2*65536,-65536};
    for(unsigned layout=0;layout<17;++layout){
        unsigned depth=layout==0?1:layout==1?4:layout<7?8:16;
        PixmapPtr dp=coverage_test_pixmap(&xs,13,7,depth,0);
        struct ps2_alpha_source raw;assert(ps2_surface_exa_alpha_source(dp,&raw)==1);
        PictureRec map={.pDrawable=&dp->drawable,.format=formats[layout],.filter=PictFilterNearest};
        PictureRec destination={.pDrawable=&dp->drawable,.format=formats[layout],.filter=PictFilterNearest,.alphaMap=&map};
        for(unsigned origin=shifted?1:0;origin<(shifted?3U:layout==2?3U:1U);++origin)for(unsigned filter=0;filter<3;++filter)
        for(unsigned op_=0;op_<=PictOpAdd;++op_){
            destination.alphaOrigin=(DDXPointRec){origin==1?1:origin==2?-1:0,origin==1?1:origin==2?-1:0};
            rgba_seed(model(source_handle));for(unsigned i=0;i<raw.bytes;++i)((unsigned char*)raw.bits)[i]=i*73+29;
            unsigned char expected[28*7];assert(raw.bytes<=sizeof(expected));memcpy(expected,raw.bits,raw.bytes);
            void *source_bytes;pixman_image_t *si=reference_image(model(source_handle),32,0,1,&source_bytes);
            pixman_image_t *di=pixman_image_create_bits((pixman_format_code_t)formats[layout],13,7,(uint32_t*)expected,raw.stride),
                *ai=pixman_image_create_bits((pixman_format_code_t)formats[layout],13,7,(uint32_t*)expected,raw.stride);
            assert(di && ai);pixman_image_set_alpha_map(di,ai,destination.alphaOrigin.x,destination.alphaOrigin.y);
            assert(pixman_image_set_transform(si,(pixman_transform_t*)&transform));pixman_image_set_repeat(si,PIXMAN_REPEAT_REFLECT);
            source.filter=filter==2?PictFilterConvolution:filter==1?PictFilterBilinear:PictFilterNearest;
            source.filter_params=filter==2?params:NULL;source.filter_nparams=filter==2?4:0;
            assert(pixman_image_set_filter(si,filter==2?PIXMAN_FILTER_CONVOLUTION:filter==1?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,
                filter==2?params:NULL,filter==2?4:0));
            pixman_image_composite32(op_,si,NULL,di,2,1,0,0,3,3,7,3);
            compositor_active=1;assert(exa.driver.CheckComposite(op_,&source,NULL,&destination));
            assert(exa.driver.PrepareComposite(op_,&source,NULL,&destination,&sp,NULL,dp));
            assert(blit.sample.destination_alpha_map.surface==blit.destination.handle);
            exa.driver.Composite(dp,2,1,0,0,3,3,7,3);compositor_active=0;exa.driver.DoneComposite(dp);
            unsigned mask=layout==0?1:layout==1?15:layout>=15?0xfff:layout>=13?0x7fff:depth==16?0xffff:0xff;
            for(unsigned y=0;y<7;++y)for(unsigned x=0;x<13;++x){
                unsigned wanted=raw_value(expected+y*raw.stride,raw.bits_per_pixel,x),actual=coverage_byte(&raw,x,y);
                if((wanted^actual)&mask){fprintf(stderr,"raw alpha alias layout=%u origin=%u filter=%u op=%u x=%u y=%u expected=%x actual=%x\n",
                    layout,origin,filter,op_,x,y,wanted,actual);abort();}}
            pixman_image_unref(si);pixman_image_unref(di);pixman_image_unref(ai);free(source_bytes);
        }
        if(layout==3){destination.alphaOrigin=(DDXPointRec){0,0};map.format=PICT_a4r4g4b4;
            unsigned before=attempts;assert(!exa.driver.CheckComposite(PictOpOver,&source,NULL,&destination) && attempts==before);}
        coverage_test_destroy(dp);
    }
    ps2_blit_exa_cleanup(&xs,&blit);ps2_exa_context_bridge_cleanup(&bridge);finish_rop();
}
/* Independent pixman images intentionally share the same raw backing.
 * Compare the complete pixmap, including pixels outside both store ROIs. */
static WindowPtr alias_window;
static PixmapPtr alias_backing;
static PixmapPtr alias_window_pixmap(WindowPtr w)
{assert(w==alias_window);return alias_backing;}
static void format_alias_case(unsigned words,unsigned boundary,unsigned backing)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    assert(!ps2_surface_set_depth(gs,source_handle,32));
    struct ps2_exa_context_bridge bridge={0};struct ps2_texture_exa texture={0};struct ps2_blit_exa blit={0};
    assert(ps2_exa_context_bridge_prepare(&xs,&bridge,&exa,ctx));
    assert(ps2_texture_exa_prepare(&xs,&texture,&exa,ps2_pipe_context_render(ctx)));
    assert(ps2_blit_exa_prepare(&xs,&blit,&exa,ctx));
    const PictFormatShort bytes[]={PICT_x4a4,PICT_a8,PICT_r3g3b2,PICT_b2g3r3,PICT_a2r2g2b2,PICT_a2b2g2r2},
        wide[]={PICT_r5g6b5,PICT_b5g6r5,PICT_a1r5g5b5,PICT_a1b5g5r5,PICT_a4r4g4b4,PICT_a4b4g4r4,
            PICT_x1r5g5b5,PICT_x1b5g5r5,PICT_x4r4g4b4,PICT_x4b4g4r4};
    const PictFormatShort *formats=words==2?wide+6:words?wide:bytes;unsigned count=words==2?4:words?10:6;
    PixmapRec sp={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=32,.bitsPerPixel=32,
        .width=83,.height=61},.devPrivates=(void*)&source_handle};
    PictTransform st={.matrix={{98304,8192,-21845},{0,49152,21845},{128,-64,65536}}},
        mt={.matrix={{49152,0,19661},{8192,98304,-21845},{0,0,65536}}};
    int32_t params[]={2*65536,65536,49152,16384};
    for(unsigned a=0;a<count;++a)for(unsigned b=0;b<count;++b){
        if((a==b && !backing) || (boundary==1 && (a!=0 || b!=count-1)) || (boundary==2 && (a!=2 || b!=3)))continue;
        if((boundary==17 || boundary>=100) && b!=(a+1)%count)continue;
        unsigned width=boundary==1?79:13,height=7;
        uint32_t native_handle=0;PixmapRec native_pixmap={0};unsigned char native_bytes[160*7];
        PixmapPtr dp;struct ps2_alpha_source raw;
        if(words==2){
            assert(!ps2_surface_create(gs,width,height,16,&native_handle));
            native_pixmap=(PixmapRec){.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=15,.bitsPerPixel=16,
                .width=width,.height=height},.devPrivates=(void*)&native_handle};dp=&native_pixmap;
            unsigned stride=(width*2+3)&~3U;
            raw=(struct ps2_alpha_source){.bits=native_bytes,.bytes=stride*height,.stride=stride,
                .width=width,.height=height,.bits_per_pixel=16,.depth=15};
        }else{dp=coverage_test_pixmap(&xs,width,height,words?16:8,0);assert(ps2_surface_exa_alpha_source(dp,&raw)==1);}
        /* Separate headers may name the same owned storage. A window names
         * its backing through GetWindowPixmap, with a different origin. */
        PixmapRec map_pixmap=*dp;
        WindowRec window_view={.drawable=dp->drawable};
        GetWindowPixmapProcPtr previous_window_pixmap=xs.GetWindowPixmap;
        int xoff=0,yoff=0;
        if(backing==2){
            window_view.drawable.type=DRAWABLE_WINDOW;
            window_view.drawable.width-=4;window_view.drawable.height-=2;
            window_view.drawable.x=1;window_view.drawable.y=1;
#ifdef COMPOSITE
            window_view.drawable.x=5;window_view.drawable.y=4;
            dp->screen_x=3;dp->screen_y=2;
            xoff=-dp->screen_x;yoff=-dp->screen_y;
#endif
            xoff+=window_view.drawable.x;yoff+=window_view.drawable.y;
            alias_window=&window_view;alias_backing=dp;xs.GetWindowPixmap=alias_window_pixmap;
        }
        PictureRec source={.pDrawable=&sp.drawable,.format=PICT_a8b8g8r8,.repeat=TRUE,.repeatType=RepeatReflect,.transform=&st},
            mask={.pDrawable=&sp.drawable,.format=PICT_a8b8g8r8,.repeat=TRUE,.repeatType=RepeatPad,.transform=&mt},
            map={.pDrawable=backing==1?&map_pixmap.drawable:&dp->drawable,.format=formats[b],.filter=PictFilterNearest},
            destination={.pDrawable=backing==2?&window_view.drawable:&dp->drawable,.format=formats[a],.filter=PictFilterNearest,.alphaMap=&map};
        for(unsigned run=0;run<(boundary==2?1U:boundary?3U:16U);++run){
            unsigned op=boundary>=100?ratio_test_wire(boundary-100):boundary==11?PictOpDisjointClear:boundary==12?PictOpDisjointSrc:boundary==13?PictOpDisjointDst:boundary==14?PictOpConjointClear:boundary==15?PictOpConjointSrc:boundary==16?PictOpConjointDst:(boundary==17 || boundary>=100)?PictOpDisjointOverReverse:boundary==9?PictOpOverlay:boundary==10?PictOpHardLight:boundary==6?PictOpDarken:boundary==7?PictOpLighten:boundary==8?PictOpDifference:boundary==5?PictOpMultiply:boundary==4?PictOpExclusion:boundary==3?PictOpScreen:boundary?run==0?PictOpSrc:run==1?PictOpOver:PictOpAdd:run<13?run:PictOpSaturate,
                filter=boundary?run:run<13?(a+b+run)%3:run-13,origin=(a+b+run)%3,component=(a+b+run)&1;
            if(hsl_wire(op))component=0;
            unsigned x=3,y=3,rw=boundary==1?71:(op==PictOpSaturate || (hsl_wire(op) || op==PictOpSoftLight || op==PictOpColorDodge || op==PictOpColorBurn || (op==PictOpDisjointOverReverse || (op>=PictOpDisjointOver && op<=PictOpDisjointXor) || (op>=PictOpConjointOver && op<=PictOpConjointXor))))?1:3,rh=(boundary==17 || boundary>=100)?1:2;
            int offset=origin==1?1:origin==2?-1:0;
            if(backing && run%5==3)offset=2;
            if(backing && run%5==4)offset=-2;
            destination.alphaOrigin=(DDXPointRec){offset-xoff,offset-yoff};
            source.filter=filter==2?PictFilterConvolution:filter?PictFilterBilinear:PictFilterNearest;
            source.filter_params=filter==2?params:NULL;source.filter_nparams=filter==2?4:0;
            mask.filter=filter==1?PictFilterBilinear:PictFilterNearest;mask.componentAlpha=component;
            rgba_seed(model(source_handle));for(unsigned i=0;i<raw.bytes;++i)((unsigned char*)raw.bits)[i]=i*73+29;
            if(words==2)for(unsigned yy=0;yy<height;++yy)for(unsigned xx=0;xx<width;++xx)
                model(native_handle)->data[yy*width+xx]=raw_value(raw.bits+yy*raw.stride,16,xx)&0x7fff;
            unsigned char *expected=malloc(raw.bytes);assert(expected);memcpy(expected,raw.bits,raw.bytes);
            void *sm,*mm;pixman_image_t *si=reference_image(model(source_handle),32,0,1,&sm),
                *mi=reference_image(model(source_handle),32,0,1,&mm),
                *di=pixman_image_create_bits((pixman_format_code_t)formats[a],width,height,(uint32_t*)expected,raw.stride),
                *ai=pixman_image_create_bits((pixman_format_code_t)formats[b],width,height,(uint32_t*)expected,raw.stride);
            assert(si && mi && di && ai);
            assert(pixman_image_set_transform(si,(pixman_transform_t*)&st) && pixman_image_set_transform(mi,(pixman_transform_t*)&mt));
            pixman_image_set_repeat(si,PIXMAN_REPEAT_REFLECT);pixman_image_set_repeat(mi,PIXMAN_REPEAT_PAD);
            pixman_image_set_component_alpha(mi,component);pixman_image_set_alpha_map(di,ai,offset,offset);
            assert(pixman_image_set_filter(si,filter==2?PIXMAN_FILTER_CONVOLUTION:filter?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,
                filter==2?params:NULL,filter==2?4:0));
            assert(pixman_image_set_filter(mi,filter==1?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,NULL,0));
            pixman_image_composite32(op,si,mi,di,2,1,1,2,x,y,rw,rh);
            compositor_active=1;assert(exa.driver.CheckComposite(op,&source,&mask,&destination));
            assert(exa.driver.PrepareComposite(op,&source,&mask,&destination,&sp,&sp,dp));
            assert(blit.sample.destination_alpha_map.x==offset && blit.sample.destination_alpha_map.y==offset);
            if(a==b)assert(!blit.sample.destination_alias && blit.sample.destination_alpha_map.surface==blit.destination.handle);
            else assert(blit.sample.destination_alias && blit.sample.destination_alpha_map.surface!=blit.destination.handle);
            if(boundary==2)for(unsigned invalid=0;invalid<9;++invalid){
                struct ps2_format_surface pf=*blit.destination_alias.format[0],mf=*blit.destination_alias.format[1];
                struct ps2_format_alias probe={.format={&pf,&mf}};
                struct ps2_picture_sample sample=blit.sample;sample.destination_alias=&probe;
                if(invalid==0)mf.surface=pf.surface;
                if(invalid==1)pf.packed=0;
                if(invalid==2){if(words==2)mf.original.handle=UINT32_MAX;else mf.original_bytes.bits++;}
                if(invalid==3)mf.original_bytes.stride++;
                if(invalid==4)mf.original_bytes.width++;
                if(invalid==5)mf.original_bytes.bits_per_pixel=words==2?8:16;
                if(invalid==6)probe.format[0]=NULL;
                if(invalid==7)probe.coverage[0]=&blit.coverage_source;
                if(invalid==8)sample.destination_alpha_map.enabled=0;
                struct ps2_pipe_copy_box box={2,1,x,y,rw,rh};unsigned before=attempts,draws=transfer_calls;
                assert(ps2_pipe_composite_rect(ctx,blit.source.handle,blit.destination.handle,&box,&sample,&blit.mask,PS2_COMPOSITE_OVER)<0 && errno==EINVAL);
                assert(attempts==before && transfer_calls==draws);
            }
            exa.driver.Composite(dp,2,1,1,2,x,y,rw,rh);compositor_active=0;exa.driver.DoneComposite(dp);
            unsigned maskbits=0;
            for(unsigned i=0;i<2;++i){PictFormatShort f=formats[i?b:a];
                unsigned n=PICT_FORMAT_A(f)+PICT_FORMAT_R(f)+PICT_FORMAT_G(f)+PICT_FORMAT_B(f);maskbits|=(1U<<n)-1;}
            for(unsigned yy=0;yy<height;++yy)for(unsigned xx=0;xx<width;++xx){
                unsigned wanted=raw_value(expected+yy*raw.stride,raw.bits_per_pixel,xx),actual=words==2?model(native_handle)->data[yy*width+xx]:coverage_byte(&raw,xx,yy);
                if((wanted^actual)&maskbits&(words==2?0x7fff:UINT32_MAX)){fprintf(stderr,"format alias backing=%u words=%u parent=%u map=%u op=%u filter=%u offset=%d ca=%u at=%u,%u wanted=%x got=%x\n",
                    backing,words,a,b,op,filter,offset,component,xx,yy,wanted,actual);abort();}}
            pixman_image_unref(si);pixman_image_unref(mi);pixman_image_unref(di);pixman_image_unref(ai);free(sm);free(mm);free(expected);
        }
        xs.GetWindowPixmap=previous_window_pixmap;alias_window=NULL;alias_backing=NULL;
        if(words==2)assert(!ps2_surface_destroy(gs,native_handle));else coverage_test_destroy(dp);
    }
    ps2_blit_exa_cleanup(&xs,&blit);ps2_exa_context_bridge_cleanup(&bridge);finish_rop();
}
static void divide_case(unsigned kind)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    struct ps2_divide_workspace w={0};assert(!ps2_divide_reserve(&w,gs));
    uint32_t n[4]={0},d[3]={0},q=0,p=0;
    for(unsigned i=0;i<4;++i)assert(!ps2_surface_create(gs,64,32,32,n+i));
    for(unsigned i=0;i<3;++i)assert(!ps2_surface_create(gs,64,32,32,d+i));
    assert(!ps2_surface_create(gs,64,32,32,&q) && !ps2_surface_create(gs,16,16,32,&p));
    if(kind==2){
        for(unsigned invalid=0;invalid<9;++invalid){
            uint32_t nn[4],dd[3],qq=q,pp=p;memcpy(nn,n,sizeof(n));memcpy(dd,d,sizeof(d));
            unsigned width=64,height=32,features=gs->caps.features;
            switch(invalid){case 0:width=0;break;case 1:height=33;break;
            case 2:nn[2]=UINT32_MAX;break;case 3:dd[1]=p;break;case 4:qq=n[0];break;
            case 5:nn[1]=w.term;break;case 6:pp=q;break;
            case 7:gs->caps.features&=~PS2GS_HW_RGBA_TRANSFER;break;
            case 8:dd[0]=w.remainder[3];break;}
            unsigned before=attempts,draws=transfer_calls;
            assert(ps2_native_divide_u32_u24(&w,nn,dd,qq,pp,width,height)<0);
            assert(errno==(invalid==7?ENOTSUP:EINVAL) && attempts==before && transfer_calls==draws);
            gs->caps.features=features;
        }
    }else{
        uint32_t expected[2048];unsigned state=0x19d705a3;
        const uint32_t divisors[]={0,1,2,127,128,255,256,257,65535,65536,65537,0x7fffff,0x800000,0xfffffe,0xffffff};
        for(unsigned batch=0;batch<(kind?48U:32U);++batch){
            for(unsigned i=0;i<2048;++i){
                expected[i]=0;
                for(unsigned channel=0;channel<3;++channel){
                    unsigned pair=batch*2048+i;uint32_t numerator,denominator;
                    if(!kind){numerator=((pair>>8)+channel*73)&255;denominator=((pair&255)+channel*53)&255;}
                    else{
                        state=state*1664525U+1013904223U;denominator=divisors[(i+channel)%15];
                        numerator=state;
                        if(batch<24){
                            uint64_t boundary=(uint64_t)denominator*(batch*11+channel);
                            int offset=(int)(i%5)-2;
                            numerator=boundary+offset>UINT32_MAX?UINT32_MAX:(uint32_t)(boundary+offset);
                        }else if(batch>=36)denominator=state&0xffffff;
                    }
                    uint32_t quotient=denominator?numerator/denominator:255;
                    if(quotient>255)quotient=255;
                    expected[i]|=quotient<<(channel*8);
                    for(unsigned byte=0;byte<4;++byte){uint32_t *value=&model(n[byte])->data[i];
                        *value=(*value&~(255U<<(channel*8)))|(((numerator>>(byte*8))&255)<<(channel*8));}
                    for(unsigned byte=0;byte<3;++byte){uint32_t *value=&model(d[byte])->data[i];
                        *value=(*value&~(255U<<(channel*8)))|(((denominator>>(byte*8))&255)<<(channel*8));}
                }
                model(q)->data[i]=0xa5332211;
            }
            compositor_active=1;assert(!ps2_native_divide_u32_u24(&w,n,d,q,p,64,32));compositor_active=0;
            for(unsigned i=0;i<2048;++i)if(model(q)->data[i]!=(expected[i]|0xa5000000)){
                fprintf(stderr,"division mismatch kind=%u batch=%u pixel=%u expected=%08x actual=%08x\n",
                    kind,batch,i,expected[i]|0xa5000000,model(q)->data[i]);abort();}
        }
    }
    assert(!ps2_divide_release(&w));
    for(unsigned i=0;i<4;++i)assert(!ps2_surface_destroy(gs,n[i]));
    for(unsigned i=0;i<3;++i)assert(!ps2_surface_destroy(gs,d[i]));
    assert(!ps2_surface_destroy(gs,q) && !ps2_surface_destroy(gs,p));finish_rop();
}
static void wide_product_case(unsigned guards)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    struct ps2_divide_workspace w={0};struct ps2_filter_workspace f={0};
    assert(!ps2_divide_reserve(&w,gs) && !ps2_filter_reserve(&f,gs,4));
    uint32_t a[3]={0},b[3]={0},out[6]={0},p=0;
    for(unsigned i=0;i<3;++i)assert(!ps2_surface_create(gs,64,32,32,a+i) && !ps2_surface_create(gs,64,32,32,b+i));
    for(unsigned i=0;i<6;++i)assert(!ps2_surface_create(gs,64,32,32,out+i));
    assert(!ps2_surface_create(gs,16,16,32,&p));
    if(guards){
        for(unsigned invalid=0;invalid<8;++invalid){
            uint32_t aa[3],bb[3],oo[6],palette=p;memcpy(aa,a,sizeof(a));memcpy(bb,b,sizeof(b));memcpy(oo,out,sizeof(out));
            unsigned width=64,height=32,features=gs->caps.features;
            switch(invalid){case 0:width=65;break;case 1:height=0;break;case 2:aa[0]=w.term;break;
            case 3:bb[2]=f.sum[3];break;case 4:oo[5]=oo[0];break;case 5:aa[2]=UINT32_MAX;break;
            case 6:palette=out[0];break;case 7:gs->caps.features&=~PS2GS_HW_RGBA_TRANSFER;break;}
            unsigned before=attempts,draws=transfer_calls;
            assert(ps2_native_product24(&w,&f,aa,bb,oo,palette,width,height)<0);
            assert(errno==(invalid==7?ENOTSUP:EINVAL) && attempts==before && transfer_calls==draws);
            gs->caps.features=features;
        }
    }else{
        uint32_t expected[6][2048];unsigned state=0x571392ef;
        const uint32_t corners[]={0,1,2,127,128,255,256,257,65535,65536,65537,0x7fffff,0x800000,0xfffffe,0xffffff};
        for(unsigned batch=0;batch<24;++batch){
            unsigned width=batch==22?13:64,height=batch==22?3:32;
            for(unsigned i=0;i<2048;++i){
                for(unsigned byte=0;byte<6;++byte){expected[byte][i]=0xa5332211;model(out[byte])->data[i]=0xa5332211;}
                for(unsigned channel=0;channel<3;++channel){
                    state=state*1664525U+1013904223U;uint32_t av=state&0xffffff;
                    state=state*1664525U+1013904223U;uint32_t bv=state&0xffffff;
                    if(batch<8){av=corners[(i+batch*3+channel)%15];bv=corners[(i/15+batch*7+channel)%15];}
                    uint64_t product=(uint64_t)av*bv;
                    for(unsigned byte=0;byte<3;++byte){
                        uint32_t *va=&model(a[byte])->data[i],*vb=&model(b[byte])->data[i],mask=255U<<(channel*8);
                        *va=(*va&~mask)|(((av>>(byte*8))&255)<<(channel*8));
                        *vb=(*vb&~mask)|(((bv>>(byte*8))&255)<<(channel*8));
                    }
                    if(i%64<width && i/64<height)for(unsigned byte=0;byte<6;++byte){uint32_t mask=255U<<(channel*8);
                        expected[byte][i]=(expected[byte][i]&~mask)|(((product>>(byte*8))&255)<<(channel*8));}
                }
            }
            uint32_t result[6];memcpy(result,out,sizeof(out));
            if(batch==23){for(unsigned i=0;i<3;++i){result[i]=a[i];result[i+3]=b[i];}
                for(unsigned byte=0;byte<6;++byte)for(unsigned i=0;i<2048;++i)
                    expected[byte][i]=(expected[byte][i]&0xffffff)|(model(result[byte])->data[i]&0xff000000);}
            compositor_active=1;assert(!ps2_native_product24(&w,&f,a,b,result,p,width,height));compositor_active=0;
            for(unsigned byte=0;byte<6;++byte)for(unsigned i=0;i<2048;++i)if(model(result[byte])->data[i]!=expected[byte][i]){
                fprintf(stderr,"wide product batch=%u byte=%u pixel=%u expected=%08x actual=%08x\n",
                    batch,byte,i,expected[byte][i],model(result[byte])->data[i]);abort();}
        }
    }
    assert(!ps2_divide_release(&w) && !ps2_filter_release(&f));
    for(unsigned i=0;i<3;++i)assert(!ps2_surface_destroy(gs,a[i]) && !ps2_surface_destroy(gs,b[i]));
    for(unsigned i=0;i<6;++i)assert(!ps2_surface_destroy(gs,out[i]));
    assert(!ps2_surface_destroy(gs,p));finish_rop();
}
static void unorm_float_case(void)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    uint32_t source=0,out[4]={0},palette=0;
    assert(!ps2_surface_create(gs,64,32,32,&source) && !ps2_surface_create(gs,16,16,32,&palette));
    for(unsigned i=0;i<4;++i)assert(!ps2_surface_create(gs,64,32,32,out+i));
    const pixman_format_code_t formats[]={PIXMAN_x8b8g8r8,PIXMAN_b5g6r5,PIXMAN_x1b5g5r5,
        PIXMAN_a4b4g4r4,PIXMAN_b2g3r3,PIXMAN_a2b2g2r2};
    const unsigned sizes[][3]={{8,8,8},{5,6,5},{5,5,5},{4,4,4},{3,3,2},{2,2,2}};
    for(unsigned layout=0;layout<6;++layout){
        unsigned cpp=layout==0?4:layout<4?2:1;unsigned char raw[8192];float expected[2048*4];
        memset(raw,0,sizeof(raw));
        for(unsigned i=0;i<2048;++i){uint32_t packed=0,expanded=0;unsigned shift=0;
            for(unsigned c=0;c<3;++c){unsigned n=sizes[layout][c],value=(i*(c*37+73)+c*29)&((1U<<n)-1);
                packed|=value<<shift;shift+=n;
                unsigned byte=value;
                /* Independent protocol bit replication into the narrow GS
                 * canvas; pixman performs the wide conversion separately. */
                unsigned filled=n;while(filled<8){byte=(byte<<n)|value;filled+=n;}byte>>=filled-8;
                expanded|=byte<<(c*8);
            }
            if(layout==3)packed|=0xf000;
            if(layout==5)packed|=0xc0;
            memcpy(raw+i*cpp,&packed,cpp);model(source)->data[i]=expanded;
            for(unsigned byte=0;byte<4;++byte)model(out[byte])->data[i]=0xa5332211;
        }
        pixman_image_t *si=pixman_image_create_bits(formats[layout],64,32,(uint32_t*)raw,64*cpp),
            *di=pixman_image_create_bits(PIXMAN_rgba_float,64,32,(uint32_t*)expected,64*4*sizeof(float));assert(si && di);
        pixman_image_composite32(PIXMAN_OP_SRC,si,NULL,di,0,0,0,0,0,0,64,32);
        compositor_active=1;assert(!ps2_native_unorm_float(gs,source,out,sizes[layout],palette,64,32));compositor_active=0;
        for(unsigned i=0;i<2048;++i)for(unsigned c=0;c<3;++c){uint32_t actual=0,wanted;
            memcpy(&wanted,expected+i*4+c,4);
            for(unsigned byte=0;byte<4;++byte){uint32_t value=model(out[byte])->data[i];
                assert((value>>24)==0xa5);actual|=((value>>(c*8))&255)<<(byte*8);}
            if(actual!=wanted){fprintf(stderr,"UNORM float layout=%u pixel=%u channel=%u expected=%08x actual=%08x\n",
                layout,i,c,wanted,actual);abort();}
        }
        pixman_image_unref(si);pixman_image_unref(di);
    }
    unsigned bits_[3]={8,8,8};
    for(unsigned invalid=0;invalid<4;++invalid){uint32_t result[4];memcpy(result,out,sizeof(out));unsigned features=gs->caps.features;
        if(invalid==0)bits_[1]=0;
        if(invalid==1)result[0]=source;
        if(invalid==2)result[1]=result[3];
        if(invalid==3)gs->caps.features&=~PS2GS_HW_CHANNEL_LUT;
        unsigned before=attempts,draws=transfer_calls;
        assert(ps2_native_unorm_float(gs,source,result,bits_,palette,64,32)<0 && errno==(invalid==3?ENOTSUP:EINVAL));
        assert(attempts==before && draws==transfer_calls);bits_[1]=8;gs->caps.features=features;
    }
    for(unsigned i=0;i<4;++i)assert(!ps2_surface_destroy(gs,out[i]));
    assert(!ps2_surface_destroy(gs,source) && !ps2_surface_destroy(gs,palette));finish_rop();
}
static void saturate_dispatch_case(unsigned exa_case,unsigned filtered)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    assert(!ps2_surface_set_depth(gs,source_handle,32) && !ps2_surface_set_depth(gs,destination_handle(),32));
    xp.drawable.depth=32;
    struct ps2_exa_context_bridge bridge={0};struct ps2_texture_exa textures={0};struct ps2_blit_exa compositor={0};
    if(exa_case){assert(ps2_exa_context_bridge_prepare(&xs,&bridge,&exa,ctx));
        assert(ps2_texture_exa_prepare(&xs,&textures,&exa,ps2_pipe_context_render(ctx)));
        assert(ps2_blit_exa_prepare(&xs,&compositor,&exa,ctx));}
    uint32_t mh=0;assert(!ps2_surface_create(gs,7,5,32,&mh) && !ps2_surface_set_depth(gs,mh,32));
    PixmapRec src={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=32,.bitsPerPixel=32,.width=83,.height=61},.devPrivates=(void*)&source_handle};
    PixmapRec mp={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=32,.bitsPerPixel=32,.width=7,.height=5},.devPrivates=(void*)&mh};
    const unsigned formats[]={PICT_a8b8g8r8,PICT_a8r8g8b8,PICT_b8g8r8a8};
    int32_t kernel[]={2*65536,65536,49152,16384};
    for(unsigned layout=0;layout<3;++layout)for(unsigned masked=0;masked<3;++masked)
    for(unsigned mix=filtered?1:0;mix<=(filtered?3U:0U);++mix){
        if(filtered && !masked && mix==2)continue;
        rgba_seed(model(source_handle));rgba_seed(model(destination_handle()));rgba_seed(model(mh));
        if(layout==2){struct pixels *d=model(destination_handle());for(unsigned i=0;i<d->width*d->height;++i)d->data[i]=(d->data[i]<<8)|(d->data[i]>>24);}
        struct ps2_picture_sample state={.source_argb=1,.destination_argb=1,.destination_swap=layout==1,.destination_rotate=layout==2,.repeat=PS2_REPEAT_REFLECT};
        struct ps2_picture_bitmap mask={.has_surface=1,.surface=mh,.width=7,.height=5,.x=-1,.y=1,
            .component_alpha=masked==2,.sample={.source_argb=1,.repeat=PS2_REPEAT_REFLECT}};
        PictTransform st={.matrix={{98304,21845,-21845},{0,32768,21845},{65536/17,0,65536}}},
            mt={.matrix={{49152,0,21845},{0,98304,-13107},{0,0,65536}}};
        if(filtered){state.transformed=mask.sample.transformed=1;
            memcpy(state.matrix,st.matrix,sizeof(state.matrix));memcpy(mask.sample.matrix,mt.matrix,sizeof(mask.sample.matrix));
            state.bilinear=!!(mix&1);mask.sample.bilinear=!!(mix&2);}
        if(filtered==2){
            state.convolution=state.bilinear;state.bilinear=0;
            state.filter_params=state.convolution?kernel:NULL;state.filter_nparams=state.convolution?4:0;
            mask.sample.convolution=mask.sample.bilinear;mask.sample.bilinear=0;
            mask.sample.filter_params=mask.sample.convolution?kernel:NULL;mask.sample.filter_nparams=mask.sample.convolution?4:0;
        }
        struct ps2_pipe_copy_box box={-1,-1,3,2,filtered?2:3,filtered?1:2};
        if(exa_case){
            PictureRec a={.pDrawable=&src.drawable,.format=PICT_a8b8g8r8,.filter=PictFilterNearest,.repeat=1,.repeatType=RepeatReflect},
                b={.pDrawable=&xp.drawable,.format=formats[layout],.filter=PictFilterNearest},
                m={.pDrawable=&mp.drawable,.format=PICT_a8b8g8r8,.filter=PictFilterNearest,.repeat=1,.repeatType=RepeatReflect,.componentAlpha=masked==2};
            if(filtered){a.transform=&st;m.transform=&mt;
                a.filter=state.convolution?PictFilterConvolution:state.bilinear?PictFilterBilinear:PictFilterNearest;
                m.filter=mask.sample.convolution?PictFilterConvolution:mask.sample.bilinear?PictFilterBilinear:PictFilterNearest;
                a.filter_params=state.convolution?kernel:NULL;a.filter_nparams=state.filter_nparams;
                m.filter_params=mask.sample.convolution?kernel:NULL;m.filter_nparams=mask.sample.filter_nparams;}
            assert(exa.driver.CheckComposite(PictOpSaturate,&a,masked?&m:NULL,&b));
            assert(exa.driver.PrepareComposite(PictOpSaturate,&a,masked?&m:NULL,&b,&src,masked?&mp:NULL,&xp));
            composite_via_exa=1;
        }
        composite_reference(source_handle,destination_handle(),&box,&state,masked?&mask:NULL,PS2_COMPOSITE_SATURATE);
        composite_via_exa=0;
    }
    if(exa_case){PictureRec a={.pDrawable=&src.drawable,.format=PICT_a8b8g8r8,.filter=PictFilterConvolution},
        b={.pDrawable=&xp.drawable,.format=PICT_a8b8g8r8,.filter=PictFilterNearest};
        unsigned before=attempts,draws=transfer_calls;assert(!exa.driver.CheckComposite(PictOpSaturate,&a,NULL,&b));
        assert(attempts==before && draws==transfer_calls);
        ps2_blit_exa_cleanup(&xs,&compositor);ps2_exa_context_bridge_cleanup(&bridge);
    }
    assert(!ps2_surface_destroy(gs,mh));finish_rop();
}
static void saturate_rgba8_case(unsigned guards,unsigned float_inputs)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    struct ps2_divide_workspace w={0};struct ps2_filter_workspace f={0};uint32_t h[6]={0},p=0,fp[16]={0};
    assert(!ps2_divide_reserve(&w,gs) && !ps2_filter_reserve(&f,gs,3));
    for(unsigned i=0;i<6;++i)assert(!ps2_surface_create(gs,64,32,32,h+i));
    assert(!ps2_surface_create(gs,16,16,32,&p));
    if(float_inputs)for(unsigned i=0;i<16;++i)assert(!ps2_surface_create(gs,64,32,32,fp+i));
    if(guards){unsigned before=attempts,draws=transfer_calls;
        if(float_inputs){
            for(unsigned invalid=0;invalid<8;++invalid){struct ps2_unorm_layout layout={0};uint32_t source[4];
                memcpy(source,fp,sizeof(source));uint32_t destination=h[4],da=h[5];unsigned features=gs->caps.features;
                if(invalid==0)source[0]=w.carry;
                if(invalid==1)destination=fp[0];
                if(invalid==2)layout.rgb_bits[0]=9;
                if(invalid==3)layout.alpha_bits=9;
                if(invalid==4)destination=da;
                if(invalid==5)gs->caps.features&=~PS2GS_HW_CHANNEL_MAP;
                assert(ps2_native_saturate_float(&w,&f,invalid==6?NULL:source,fp+4,
                    invalid==7?NULL:fp+8,fp+12,destination,da,&layout,p,64,32)<0 && errno==(invalid==5?ENOTSUP:EINVAL));
                assert(attempts==before && draws==transfer_calls);gs->caps.features=features;
            }
        }
        assert(ps2_native_saturate_rgba8(&w,&f,h[0],h[1],h[2],h[3],h[0],h[5],p,64,32)<0 && errno==EINVAL);
        assert(attempts==before && draws==transfer_calls);
        for(unsigned invalid=0;invalid<7;++invalid){struct ps2_unorm_layout layouts[3]={0};
            if(invalid<3)layouts[invalid].rgb_bits[invalid]=9;
            else if(invalid<6)layouts[invalid-3].alpha_bits=9;
            assert(ps2_native_saturate_unorm(&w,&f,h[0],h[1],h[2],h[3],h[4],h[5],
                invalid==6?NULL:layouts,p,64,32)<0 && errno==EINVAL);
            assert(attempts==before && draws==transfer_calls);
        }
    }else{
        unsigned state=0x159425ac;
        for(unsigned batch=0;batch<12;++batch){
            uint32_t source[2048],mask[2048],destination[2048];unsigned component=batch>=6;
            unsigned width=batch==11?13:64,height=batch==11?3:32;
            for(unsigned i=0;i<2048;++i){
                state=state*1664525U+1013904223U;source[i]=state;
                state=state*1664525U+1013904223U;mask[i]=batch<2?UINT32_MAX:state;
                state=state*1664525U+1013904223U;destination[i]=state;
                if(i%17==0)source[i]&=0xffffff;
                if(i%19==0)destination[i]|=0xff000000;
                if(i%23==0)mask[i]&=0xffffff;
                if(!(batch&1)){unsigned a=source[i]>>24,da=destination[i]>>24;
                    for(unsigned c=0;c<3;++c){unsigned s=(source[i]>>(c*8))&255,d=(destination[i]>>(c*8))&255;
                        source[i]=(source[i]&~(255U<<(c*8)))|mul255(s,a)<<(c*8);
                        destination[i]=(destination[i]&~(255U<<(c*8)))|mul255(d,da)<<(c*8);}
                }
                model(h[0])->data[i]=0xa5000000|(source[i]&0xffffff);
                model(h[1])->data[i]=0xa5000000|(source[i]>>24)*0x010101;
                model(h[2])->data[i]=0xa5000000|(component?(mask[i]&0xffffff):(mask[i]>>24)*0x010101);
                model(h[3])->data[i]=0xa5000000|(mask[i]>>24)*0x010101;
                model(h[4])->data[i]=0xa5000000|(destination[i]&0xffffff);
                model(h[5])->data[i]=0xa5000000|(destination[i]>>24)*0x010101;
            }
            pixman_image_t *si=pixman_image_create_bits(PIXMAN_a8b8g8r8,64,32,source,256),
                *mi=pixman_image_create_bits(PIXMAN_a8b8g8r8,64,32,mask,256),
                *di=pixman_image_create_bits(PIXMAN_a8b8g8r8,64,32,destination,256);assert(si && mi && di);
            pixman_image_set_component_alpha(mi,component);
            pixman_image_composite32(PIXMAN_OP_SATURATE,si,mi,di,0,0,0,0,0,0,width,height);
            compositor_active=1;
            if(float_inputs){unsigned bits_[]={8,8,8};struct ps2_unorm_layout layout={0};
                for(unsigned i=0;i<4;++i)assert(!ps2_native_unorm_float(gs,h[i],fp+i*4,bits_,p,64,32));
                assert(!ps2_native_saturate_float(&w,&f,fp,fp+4,component?fp+8:fp+12,fp+12,h[4],h[5],&layout,p,width,height));
            }else assert(!ps2_native_saturate_rgba8(&w,&f,h[0],h[1],h[2],h[3],h[4],h[5],p,width,height));
            compositor_active=0;
            for(unsigned i=0;i<2048;++i){uint32_t rgb=0xa5000000|(destination[i]&0xffffff),alpha=0xa5000000|(destination[i]>>24)*0x010101;
                if(model(h[4])->data[i]!=rgb || model(h[5])->data[i]!=alpha){fprintf(stderr,
                    "wide Saturate batch=%u pixel=%u source=%08x mask=%08x expected=%08x rgb=%08x alpha=%08x\n",
                    batch,i,source[i],mask[i],destination[i],model(h[4])->data[i],model(h[5])->data[i]);abort();}
            }
            pixman_image_unref(si);pixman_image_unref(mi);pixman_image_unref(di);
        }
    }
    assert(!ps2_divide_release(&w) && !ps2_filter_release(&f));
    for(unsigned i=0;i<6;++i)assert(!ps2_surface_destroy(gs,h[i]));
    if(float_inputs)for(unsigned i=0;i<16;++i)assert(!ps2_surface_destroy(gs,fp[i]));
    assert(!ps2_surface_destroy(gs,p));finish_rop();
}
static void saturate_alpha_float_case(unsigned guards)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    struct ps2_divide_workspace w={0};struct ps2_filter_workspace f={0};uint32_t input[8]={0},out=0,p=0;
    assert(!ps2_divide_reserve(&w,gs) && !ps2_filter_reserve(&f,gs,3));
    for(unsigned i=0;i<8;++i)assert(!ps2_surface_create(gs,64,32,32,input+i));
    assert(!ps2_surface_create(gs,64,32,32,&out) && !ps2_surface_create(gs,16,16,32,&p));
    if(guards){unsigned before=attempts,draws=transfer_calls;
        for(unsigned invalid=0;invalid<9;++invalid){uint32_t sa[4];memcpy(sa,input,sizeof(sa));
            unsigned n=8,features=gs->caps.features,width=64;uint32_t destination=out;
            if(invalid==0)n=0;
            if(invalid==1)n=9;
            if(invalid==2)sa[0]=w.carry;
            if(invalid==3)destination=input[0];
            if(invalid==4)gs->caps.features&=~PS2GS_HW_CHANNEL_MAP;
            if(invalid==5)width=65;
            if(invalid==6)sa[0]=p;
            assert(ps2_native_saturate_alpha_float(&w,&f,invalid==7?NULL:sa,
                invalid==8?NULL:input+4,destination,n,p,width,32)<0 && errno==(invalid==4?ENOTSUP:EINVAL));
            assert(attempts==before && draws==transfer_calls);gs->caps.features=features;
        }
    }else{
        const unsigned depths[]={1,4,8};unsigned state=0x514af903;
        for(unsigned kind=0;kind<3;++kind)for(unsigned batch=0;batch<4;++batch){unsigned n=depths[kind],max=(1U<<n)-1;
            unsigned width=batch==3?13:64,height=batch==3?3:32,stride=n==1?8:64;
            unsigned char raw[2048]={0};float source[8192],mask[8192];
            for(unsigned i=0;i<2048;++i){unsigned q=(i*73+29)&max;
                if(n==1)raw[i/8]|=q<<(i&7);else raw[i]=q|(n==4?0xa0:0);
                model(out)->data[i]=0xa5000000|((q*255/max)*0x010101);
                for(unsigned field=0;field<2;++field){uint32_t bits_;
                    if(batch<2){float value=((i*(field?37:17)+field*19)%257)*(1.0f/256);
                        memcpy(&bits_,&value,4);if(batch && bits_ && i%3==0)--bits_;
                        if(batch && bits_<0x3f800000 && i%3==1)++bits_;
                    }else{state=state*1664525U+1013904223U;
                        bits_=((87+state%40)<<23)|(state&0x7fffff);if(i%17==0)bits_=0;}
                    float *value=field?mask:source;
                    for(unsigned c=0;c<4;++c)memcpy(value+i*4+c,&bits_,4);
                    for(unsigned byte=0;byte<4;++byte)model(input[field*4+byte])->data[i]=
                        0xa5000000|((bits_>>(byte*8))&255)*0x010101;
                }
            }
            pixman_image_t *si=pixman_image_create_bits(PIXMAN_rgba_float,64,32,(uint32_t*)source,1024),
                *mi=pixman_image_create_bits(PIXMAN_rgba_float,64,32,(uint32_t*)mask,1024),
                *di=pixman_image_create_bits(n==1?PIXMAN_a1:n==4?PIXMAN_x4a4:PIXMAN_a8,64,32,(uint32_t*)raw,stride);
            assert(si && mi && di);pixman_image_set_component_alpha(mi,batch&1);
            pixman_image_composite32(PIXMAN_OP_SATURATE,si,mi,di,0,0,0,0,0,0,width,height);
            compositor_active=1;assert(!ps2_native_saturate_alpha_float(&w,&f,input,input+4,out,n,p,width,height));compositor_active=0;
            for(unsigned i=0;i<2048;++i){unsigned code=n==1?(raw[i/8]>>(i&7))&1:raw[i]&max;
                uint32_t wanted=0xa5000000|(code*255/max)*0x010101;
                if(model(out)->data[i]!=wanted){fprintf(stderr,"alpha float Saturate n=%u batch=%u pixel=%u wanted=%08x got=%08x\n",
                    n,batch,i,wanted,model(out)->data[i]);abort();}
                for(unsigned field=0;field<2;++field){uint32_t bits_;memcpy(&bits_,(field?mask:source)+i*4,4);
                    for(unsigned byte=0;byte<4;++byte)assert(model(input[field*4+byte])->data[i]==
                        (0xa5000000|((bits_>>(byte*8))&255)*0x010101));}
            }
            pixman_image_unref(si);pixman_image_unref(mi);pixman_image_unref(di);
        }
    }
    for(unsigned i=0;i<8;++i)assert(!ps2_surface_destroy(gs,input[i]));
    assert(!ps2_surface_destroy(gs,out) && !ps2_surface_destroy(gs,p) && !ps2_divide_release(&w) && !ps2_filter_release(&f));finish_rop();
}
static void float_unorm_case(void)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    struct ps2_divide_workspace w={0};struct ps2_filter_workspace f={0};
    assert(!ps2_divide_reserve(&w,gs) && !ps2_filter_reserve(&f,gs,3));
    uint32_t a[4]={0},out=0,p=0;unsigned state=0x5415ac93;
    for(unsigned byte=0;byte<4;++byte)assert(!ps2_surface_create(gs,64,32,32,a+byte));
    assert(!ps2_surface_create(gs,64,32,32,&out) && !ps2_surface_create(gs,16,16,32,&p));
    const pixman_format_code_t formats[]={PIXMAN_x8b8g8r8,PIXMAN_b5g6r5,PIXMAN_x1b5g5r5,
        PIXMAN_a4b4g4r4,PIXMAN_b2g3r3,PIXMAN_a2b2g2r2};
    const unsigned sizes[][3]={{8,8,8},{5,6,5},{5,5,5},{4,4,4},{3,3,2},{2,2,2}};
    for(unsigned layout=0;layout<6;++layout)for(unsigned batch=0;batch<4;++batch){
        float values[8192];unsigned char expected[8192]={0};unsigned cpp=layout==0?4:layout<4?2:1;
        uint32_t result=layout==5 && batch==3?a[0]:out;
        for(unsigned i=0;i<2048;++i){
            for(unsigned byte=0;byte<4;++byte)model(a[byte])->data[i]=0xa5000000;
            model(out)->data[i]=0xa5332211;values[i*4+3]=1;
            for(unsigned c=0;c<3;++c){uint32_t raw;
                if(batch<2){float value=((i*17+c*29)%513)*(1.0f/256);memcpy(&raw,&value,4);
                    if(batch && raw && i%3==0)--raw;
                    if(batch && i%3==1)++raw;
                }else{state=state*1664525U+1013904223U;raw=((100+state%31)<<23)|(state&0x7fffff);
                    if(i%17==0)raw=0;
                    if(i%19==0)raw=state&0x7fffff;}
                memcpy(values+i*4+c,&raw,4);
                for(unsigned byte=0;byte<4;++byte)model(a[byte])->data[i]|=((raw>>(byte*8))&255)<<(c*8);
            }
        }
        pixman_image_t *si=pixman_image_create_bits(PIXMAN_rgba_float,64,32,(uint32_t*)values,64*4*sizeof(float)),
            *di=pixman_image_create_bits(formats[layout],64,32,(uint32_t*)expected,64*cpp);assert(si && di);
        pixman_image_composite32(PIXMAN_OP_SRC,si,NULL,di,0,0,0,0,0,0,64,32);
        compositor_active=1;assert(!ps2_native_float_unorm(&w,&f,a,result,sizes[layout],p,64,32));compositor_active=0;
        for(unsigned i=0;i<2048;++i){uint32_t packed=0,wanted=0xa5000000;unsigned shift=0;
            memcpy(&packed,expected+i*cpp,cpp);
            for(unsigned c=0;c<3;++c){unsigned n=sizes[layout][c];wanted|=((packed>>shift)&((1U<<n)-1))<<(c*8);shift+=n;}
            if(model(result)->data[i]!=wanted){fprintf(stderr,"float UNORM layout=%u batch=%u pixel=%u expected=%08x actual=%08x\n",
                layout,batch,i,wanted,model(result)->data[i]);abort();}
        }
        pixman_image_unref(si);pixman_image_unref(di);
    }
    unsigned bits_[3]={8,8,8};
    for(unsigned invalid=0;invalid<3;++invalid){uint32_t result=out;unsigned features=gs->caps.features;
        if(invalid==0)bits_[1]=9;
        if(invalid==1)result=w.remainder[0];
        if(invalid==2)gs->caps.features&=~PS2GS_HW_CHANNEL_MAP;
        unsigned before=attempts,draws=transfer_calls;
        assert(ps2_native_float_unorm(&w,&f,a,result,bits_,p,64,32)<0 && errno==(invalid==2?ENOTSUP:EINVAL));
        assert(before==attempts && draws==transfer_calls);bits_[1]=8;gs->caps.features=features;
    }
    assert(!ps2_divide_release(&w) && !ps2_filter_release(&f));
    for(unsigned byte=0;byte<4;++byte)assert(!ps2_surface_destroy(gs,a[byte]));
    assert(!ps2_surface_destroy(gs,out) && !ps2_surface_destroy(gs,p));finish_rop();
}
static void saturate_bilinear_raw_masks(void)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    assert(!ps2_surface_set_depth(gs,source_handle,32) && !ps2_surface_set_depth(gs,destination_handle(),32));
    for(unsigned kind=0;kind<3;++kind)for(unsigned component=0;component<2;++component)
    for(unsigned mix=1;mix<=3;++mix)for(unsigned repeat=0;repeat<4;++repeat){
        rgba_seed(model(source_handle));rgba_seed(model(destination_handle()));
        unsigned char raw[40]={0};unsigned n=kind==0?1:kind==1?4:8,stride=kind==0?4:8;
        for(unsigned i=0;i<35;++i){unsigned q=(i*73+19)&((1U<<n)-1);
            if(n==1)raw[(i/7)*stride+(i%7)/8]|=q<<((i%7)&7);
            else raw[(i/7)*stride+i%7]=q|(n==4?0xa0:0);}
        struct ps2_picture_sample sample=identity(repeat);
        sample.source_argb=sample.destination_argb=sample.transformed=1;sample.bilinear=!!(mix&1);
        sample.matrix[0][0]=49152;sample.matrix[0][2]=173;sample.matrix[1][1]=98304;sample.matrix[1][2]=-21845;
        struct ps2_picture_bitmap mask={.bits=raw,.bytes=stride*5,.stride=stride,.width=7,.height=5,
            .bits_per_pixel=n==1?1:8,.alpha_bits=n==4?4:0,.component_alpha=component,.x=-1,.y=1,.sample=identity(repeat)};
        mask.sample.bilinear=!!(mix&2);mask.sample.transformed=1;
        mask.sample.matrix[0][0]=98304;mask.sample.matrix[0][2]=257;mask.sample.matrix[1][1]=32768;mask.sample.matrix[1][2]=-13107;
        struct ps2_pipe_copy_box box={-1,-1,3,2,2,1};
        composite_reference(source_handle,destination_handle(),&box,&sample,&mask,PS2_COMPOSITE_SATURATE);
    }
    finish_rop();
}
static void saturate_opaque_extent(void)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    assert(!ps2_surface_set_depth(gs,source_handle,32) && !ps2_surface_set_depth(gs,destination_handle(),32));
    uint32_t mask_handle=0,word=0;
    assert(!ps2_surface_create(gs,7,5,32,&mask_handle) && !ps2_surface_set_depth(gs,mask_handle,32));
    assert(!ps2_surface_create(gs,83,61,16,&word));
    for(unsigned i=0;i<83*61;++i)model(word)->data[i]=(i*73+19)&0x7fff;
    for(unsigned source_kind=0;source_kind<2;++source_kind)for(unsigned linear=0;linear<2;++linear)
    for(unsigned masked=0;masked<3;++masked)for(unsigned transformed=0;transformed<2;++transformed){
        rgba_seed(model(source_handle));rgba_seed(model(destination_handle()));rgba_seed(model(mask_handle));
        struct ps2_picture_sample state=identity(PS2_REPEAT_NONE);
        state.destination_argb=1;state.bilinear=linear;state.transformed=transformed;
        if(transformed){state.matrix[0][2]=13107;state.matrix[1][2]=21845;}
        struct ps2_picture_bitmap mask={.has_surface=1,.surface=mask_handle,.width=7,.height=5,.x=1,.y=1,
            .component_alpha=masked==2,.sample=identity(PS2_REPEAT_NONE)};
        mask.sample.source_argb=masked==2;mask.sample.bilinear=linear;mask.sample.transformed=transformed;
        if(transformed){mask.sample.matrix[0][2]=21845;mask.sample.matrix[1][2]=-13107;}
        struct ps2_pipe_copy_box box={1,2,3,2,2,1};
        composite_reference(source_kind?word:source_handle,destination_handle(),&box,&state,masked?&mask:NULL,PS2_COMPOSITE_SATURATE);
    }
    assert(!ps2_surface_destroy(gs,mask_handle) && !ps2_surface_destroy(gs,word));finish_rop();
}
static uint32_t convolution_target_cast(float value)
{
    if(value<=0)return 0;
    if(value>=0x1p32f)return UINT32_MAX;
    return (uint32_t)value;
}
static void convolution_float_case(unsigned guards)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    struct ps2_divide_workspace w={0};struct ps2_filter_workspace f={0};uint32_t h[9]={0},p=0;
    assert(!ps2_divide_reserve(&w,gs) && !ps2_filter_reserve(&f,gs,4));
    for(unsigned i=0;i<9;++i)assert(!ps2_surface_create(gs,64,32,32,h+i));
    assert(!ps2_surface_create(gs,16,16,32,&p));
    if(guards){
        for(unsigned invalid=0;invalid<12;++invalid){
            uint32_t acc[4],out[4],codes=h[8],palette=p;unsigned bits_[]={8,8,8},width=64,features=gs->caps.features;
            memcpy(acc,h,sizeof(acc));memcpy(out,h+4,sizeof(out));
            if(invalid==0)bits_[1]=0;
            if(invalid==1)bits_[2]=9;
            if(invalid==2)codes=acc[0];
            if(invalid==3)acc[2]=acc[1];
            if(invalid==4)acc[0]=w.term;
            if(invalid==5)codes=f.carry;
            if(invalid==6)palette=out[0];
            if(invalid==7)width=65;
            if(invalid==8)gs->caps.features&=~PS2GS_HW_RGBA_TRANSFER;
            if(invalid==9)out[1]=out[0];
            if(invalid==10)out[0]=f.sum[3];
            if(invalid==11)acc[0]=UINT32_MAX;
            unsigned before=attempts,draws=transfer_calls;
            int result=invalid>=9?ps2_native_convolution_float_end(&w,&f,acc,out,palette,width,1):
                ps2_native_convolution_float_tap(&w,&f,codes,bits_,65536,acc,palette,width,1);
            assert(result<0 && errno==(invalid==8?ENOTSUP:EINVAL));
            assert(before==attempts && draws==transfer_calls);gs->caps.features=features;
        }
        unsigned bits_[]={8,8,8},before=attempts,draws=transfer_calls;
        assert(!ps2_native_convolution_float_tap(&w,&f,h[8],bits_,0,h,p,64,1));
        assert(before==attempts && draws==transfer_calls);
    }else{
        const unsigned layouts[][3]={{8,8,8},{3,3,2},{4,4,4},{1,5,6}};
        const uint32_t corners[]={0,1,32767,32768,65535,65536,0x7fffff,0x800000,0x800001,
            0x7fffff80,0x80000000,0xffffff00,0xfffffffe,0xffffffff};
        const int32_t coefficients[]={1,-1,32767,-32768,65535,-65536,65536,131071,
            0x7fffffff,(-2147483647-1),17,-257};
        uint32_t state=0x79813527,expected[64][3];
        for(unsigned layout=0;layout<4;++layout)for(unsigned kind=0;kind<12;++kind){
            for(unsigned i=0;i<2048;++i){model(h[8])->data[i]=0x5a719331;
                for(unsigned byte=0;byte<8;++byte)model(h[byte])->data[i]=0xa5332211;}
            for(unsigned i=0;i<64;++i)for(unsigned c=0;c<3;++c){
                state=state*1664525U+1013904223U;
                expected[i][c]=i<28?corners[(i+c)%14]:state;
                float value=(float)expected[i][c]*0x1p-32f;uint32_t raw;memcpy(&raw,&value,4);
                for(unsigned byte=0;byte<4;++byte){uint32_t *v=&model(h[byte])->data[i];
                    *v=(*v&~(255U<<(c*8)))|(((raw>>(byte*8))&255)<<(c*8));}
            }
            for(unsigned tap=0;tap<3;++tap){int32_t coefficient=coefficients[(kind+tap)%12];
                for(unsigned i=0;i<64;++i){uint32_t expanded=0;
                    for(unsigned c=0;c<3;++c){unsigned n=layouts[layout][c],mask=(1U<<n)-1;
                        state=state*1664525U+1013904223U;unsigned code=i<4?(i==0?0:i==1?mask:mask/2):state&mask;
                        unsigned v=code,filled=n;while(filled<8){v=(v<<n)|code;filled+=n;}expanded|=(v>>(filled-8))<<(c*8);
                        volatile float reciprocal=1.0f/mask,value=code*reciprocal,product=value*(float)coefficient;
                        volatile float sum=(float)expected[i][c]+product;
                        expected[i][c]=convolution_target_cast(sum);
                    }
                    model(h[8])->data[i]=0x5a000000|expanded;
                }
                compositor_active=1;assert(!ps2_native_convolution_float_tap(&w,&f,h[8],layouts[layout],coefficient,h,p,64,1));compositor_active=0;
                for(unsigned i=0;i<2048;++i)for(unsigned byte=0;byte<4;++byte){uint32_t wanted=0xa5332211;
                    if(i<64){wanted=0xa5000000;for(unsigned c=0;c<3;++c){float value=(float)expected[i][c]*0x1p-32f;uint32_t raw;memcpy(&raw,&value,4);
                        wanted|=((raw>>(byte*8))&255)<<(c*8);}}
                    if(model(h[byte])->data[i]!=wanted){fprintf(stderr,"convolution accumulator layout=%u kind=%u tap=%u pixel=%u byte=%u wanted=%08x got=%08x\n",
                        layout,kind,tap,i,byte,wanted,model(h[byte])->data[i]);abort();}
                }
            }
            uint32_t out[4];for(unsigned byte=0;byte<4;++byte)out[byte]=kind&1?h[3-byte]:h[4+byte];
            compositor_active=1;assert(!ps2_native_convolution_float_end(&w,&f,h,out,p,64,1));compositor_active=0;
            for(unsigned i=0;i<64;++i)for(unsigned byte=0;byte<4;++byte){uint32_t wanted=0xa5000000;
                for(unsigned c=0;c<3;++c){float value=expected[i][c]&0x80000000U?0:(float)expected[i][c]/65536.f;
                    if(value>1)value=1;
                    uint32_t raw;memcpy(&raw,&value,4);wanted|=((raw>>(byte*8))&255)<<(c*8);}
                assert(model(out[byte])->data[i]==wanted);
            }
        }
    }
    assert(!ps2_divide_release(&w) && !ps2_filter_release(&f));
    for(unsigned i=0;i<9;++i)assert(!ps2_surface_destroy(gs,h[i]));
    assert(!ps2_surface_destroy(gs,p));finish_rop();
}
static void wide_bilinear_case(unsigned guards)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    assert(!ps2_surface_set_depth(gs,source_handle,32));
    struct ps2_divide_workspace w={0};struct ps2_filter_workspace f={0};uint32_t out[4]={0},p=0;
    assert(!ps2_divide_reserve(&w,gs) && !ps2_filter_reserve(&f,gs,3));
    for(unsigned i=0;i<4;++i)assert(!ps2_surface_create(gs,64,32,32,out+i));
    assert(!ps2_surface_create(gs,16,16,32,&p));
    const struct ps2_filter_tap original[]={{2,3,0},{3,3,0},{2,4,0},{3,4,0}};
    if(guards){
        for(unsigned invalid=0;invalid<9;++invalid){struct ps2_filter_tap taps[4];memcpy(taps,original,sizeof(taps));
            uint32_t result[4];memcpy(result,out,sizeof(result));unsigned bits_[]={8,8,8};
            unsigned features=gs->caps.features,fx=127,width=64;int step=1;
            if(invalid==0)fx=65536;
            if(invalid==1)bits_[2]=0;
            if(invalid==2)result[0]=w.term;
            if(invalid==3)result[0]=result[1];
            if(invalid==4)taps[2].x=UINT16_MAX;
            if(invalid==5)step=2;
            if(invalid==6)width=65;
            if(invalid==7)gs->caps.features&=~PS2GS_HW_CHANNEL_MAP;
            if(invalid==8)f.alpha_map.enabled=1;
            unsigned before=attempts,draws=transfer_calls;
            assert(ps2_native_bilinear_float(&w,&f,source_handle,result,bits_,p,taps,fx,32768,width,step)<0 &&
                errno==(invalid==7?ENOTSUP:EINVAL));
            assert(before==attempts && draws==transfer_calls);gs->caps.features=features;f.alpha_map.enabled=0;
        }
    }else{
        const pixman_format_code_t formats[]={PIXMAN_a8b8g8r8,PIXMAN_b2g3r3,PIXMAN_a4b4g4r4};
        const unsigned sizes[][4]={{8,8,8,8},{3,3,2,0},{4,4,4,4}};
        const unsigned fractions[]={0,1,257,19661,32767,32768,65535};
        unsigned char raw[83*4*61+4*61];
        for(unsigned layout=0;layout<3;++layout){unsigned cpp=layout==0?4:layout==1?1:2,stride=(83*cpp+3)&~3U;
            memset(raw,0,sizeof(raw));
            for(unsigned i=0;i<83*61;++i){uint32_t packed=0,expanded=0;unsigned shift=0;
                for(unsigned c=0;c<4;++c){unsigned n=sizes[layout][c];if(!n){expanded|=255U<<(c*8);continue;}
                    unsigned value=(i*(c*37+73)+c*29)&((1U<<n)-1),v=value,filled=n;
                    packed|=value<<shift;shift+=n;
                    while(filled<8){v=(v<<n)|value;filled+=n;}expanded|=(v>>(filled-8))<<(c*8);
                }
                memcpy(raw+(i/83)*stride+(i%83)*cpp,&packed,cpp);model(source_handle)->data[i]=expanded;
            }
            for(unsigned alpha=0;alpha<2;++alpha)for(unsigned xi=0;xi<7;++xi)for(unsigned yi=0;yi<7;++yi){
                unsigned fx=fractions[xi],fy=fractions[yi],width=xi==6 && yi==6?13:64;
                struct ps2_filter_tap taps[4];memcpy(taps,original,sizeof(taps));
                unsigned clipped=xi==6 && yi==6;
                if(clipped)taps[2].x=taps[2].y=taps[3].x=taps[3].y=UINT16_MAX;
                float expected[64*4];memset(expected,0,sizeof(expected));
                pixman_image_t *si=pixman_image_create_bits(formats[layout],83,clipped?4:61,(uint32_t*)raw,stride),
                    *di=pixman_image_create_bits(PIXMAN_rgba_float,width,1,(uint32_t*)expected,width*16);assert(si && di);
                pixman_transform_t transform={{{65536,0,fx},{0,65536,fy},{0,0,65536}}};
                assert(pixman_image_set_transform(si,&transform) && pixman_image_set_filter(si,PIXMAN_FILTER_BILINEAR,NULL,0));
                pixman_image_composite32(PIXMAN_OP_SRC,si,NULL,di,2,3,0,0,0,0,width,1);
                unsigned bits_[3];for(unsigned c=0;c<3;++c)bits_[c]=alpha?(sizes[layout][3]?sizes[layout][3]:8):sizes[layout][c];
                f.source_kind=alpha?(sizes[layout][3]?PS2_FILTER_CHANNEL:PS2_FILTER_CONSTANT):PS2_FILTER_RGB;
                f.source_channel=3;f.source_constant=255;f.source_width=83;f.source_height=61;
                for(unsigned byte=0;byte<4;++byte)for(unsigned i=0;i<2048;++i)model(out[byte])->data[i]=0xa5332211;
                compositor_active=1;assert(!ps2_native_bilinear_float(&w,&f,source_handle,out,bits_,p,taps,fx,fy,width,1));compositor_active=0;
                for(unsigned i=0;i<2048;++i)for(unsigned byte=0;byte<4;++byte){uint32_t wanted=0xa5332211;
                    if(i<width){wanted=0xa5000000;
                        for(unsigned c=0;c<3;++c){uint32_t value;memcpy(&value,expected+i*4+(alpha?3:c),4);
                            wanted|=((value>>(byte*8))&255)<<(c*8);}}
                    if(model(out[byte])->data[i]!=wanted){fprintf(stderr,"wide bilinear layout=%u alpha=%u fx=%u fy=%u byte=%u pixel=%u wanted=%08x got=%08x\n",
                        layout,alpha,fx,fy,byte,i,wanted,model(out[byte])->data[i]);abort();}
                }
                pixman_image_unref(si);pixman_image_unref(di);
            }
        }
    }
    assert(!ps2_divide_release(&w) && !ps2_filter_release(&f));
    for(unsigned i=0;i<4;++i)assert(!ps2_surface_destroy(gs,out[i]));
    assert(!ps2_surface_destroy(gs,p));finish_rop();
}
static void float_arithmetic_case(unsigned guards,unsigned operation)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    struct ps2_divide_workspace w={0};struct ps2_filter_workspace f={0};
    assert(!ps2_divide_reserve(&w,gs) && !ps2_filter_reserve(&f,gs,3));
    uint32_t a[4]={0},b[4]={0},out[4]={0},p=0;
    for(unsigned i=0;i<4;++i)assert(!ps2_surface_create(gs,64,32,32,a+i) &&
        !ps2_surface_create(gs,64,32,32,b+i) && !ps2_surface_create(gs,64,32,32,out+i));
    assert(!ps2_surface_create(gs,16,16,32,&p));
    if(guards){
        for(unsigned invalid=0;invalid<8;++invalid){
            uint32_t result[4],input[4];memcpy(result,out,sizeof(out));memcpy(input,a,sizeof(a));
            unsigned features=gs->caps.features,width=64;uint32_t palette=p,saved=w.remainder[1];
            if(invalid==0)result[1]=result[0];
            if(invalid==1)result[0]=w.borrow;
            if(invalid==2)input[0]=w.carry;
            if(invalid==3)palette=out[0];
            if(invalid==4)width=65;
            if(invalid==5)gs->caps.features&=~PS2GS_HW_CHANNEL_MAP;
            if(invalid==6)w.remainder[1]=w.remainder[0];
            unsigned before=attempts,draws=transfer_calls;
            if(invalid==7 && operation<2)continue;
            int status=operation==1?ps2_native_float_divide(&w,&f,input,b,result,palette,width,32):
                operation>=2?ps2_native_float_add(&w,&f,input,b,result,palette,width,32,invalid==7?2:operation==3):
                ps2_native_float_multiply(&w,&f,input,b,result,palette,width,32);
            assert(status<0 && errno==(invalid==5?ENOTSUP:EINVAL));
            assert(before==attempts && draws==transfer_calls);
            gs->caps.features=features;w.remainder[1]=saved;
        }
    }else{
        uint32_t expected[4][2048];unsigned state=0x730ac251;
        for(unsigned batch=0;batch<40;++batch){
            uint32_t result[4];memcpy(result,batch==39?a:batch==38?b:out,sizeof(out));
            for(unsigned i=0;i<2048;++i){
                for(unsigned byte=0;byte<4;++byte){model(a[byte])->data[i]=0xa5000000;model(b[byte])->data[i]=0xa5000000;
                    model(out[byte])->data[i]=0xa5000000;expected[byte][i]=0xa5000000;}
                for(unsigned c=0;c<3;++c){
                    unsigned pair=batch*2048+i;float av,bv;
                    if(batch<32){float reciprocal=1.0f/255;
                        av=(((pair>>8)+c*73)&255)*reciprocal;
                        bv=(operation==1?(((pair&255)+c*53)%255+1):((pair+c*53)&255))*reciprocal;
                    }else{
                        /* Filters can produce 2^-40 taps and 2^-80 masked
                         * products. Cover those normal exponents and large
                         * alignment gaps without introducing subnormals. */
                        unsigned base=operation>=2?39:operation?47:87,
                            range=operation>=2?112:operation?104:40;
                        state=state*1664525U+1013904223U;uint32_t bits_=((base+(state%range))<<23)|(state&0x7fffff);memcpy(&av,&bits_,4);
                        state=state*1664525U+1013904223U;bits_=((base+(state%range))<<23)|(state&0x7fffff);memcpy(&bv,&bits_,4);
                        if(i%17==0)av=0;
                        if(i%19==0)bv=1;
                        if(operation && i%23==0)av=bv;
                        if(operation && i%29==0)av=1;
                        if(operation>=2 && i%31==0)bv=0;
                        if(operation>=2 && i%37==0){uint32_t bits_;memcpy(&bits_,&av,4);if(bits_)++bits_;memcpy(&bv,&bits_,4);}
                        if(operation>=2 && i%41==0)bv=av*0.5f;
                    }
                    if(operation==3 && av<bv){float swap=av;av=bv;bv=swap;}
                    float product=operation==1?av/bv:operation==2?av+bv:operation==3?av-bv:av*bv;
                    uint32_t aa,bb,value;memcpy(&aa,&av,4);memcpy(&bb,&bv,4);memcpy(&value,&product,4);
                    for(unsigned byte=0;byte<4;++byte){
                        model(a[byte])->data[i]|=((aa>>(byte*8))&255)<<(c*8);
                        model(b[byte])->data[i]|=((bb>>(byte*8))&255)<<(c*8);
                        expected[byte][i]|=((value>>(byte*8))&255)<<(c*8);
                    }
                }
            }
            compositor_active=1;assert(!(operation==1?ps2_native_float_divide(&w,&f,a,b,result,p,64,32):
                operation>=2?ps2_native_float_add(&w,&f,a,b,result,p,64,32,operation==3):
                ps2_native_float_multiply(&w,&f,a,b,result,p,64,32)));compositor_active=0;
            for(unsigned byte=0;byte<4;++byte)for(unsigned i=0;i<2048;++i)if(model(result[byte])->data[i]!=expected[byte][i]){
                fprintf(stderr,"float %s batch=%u byte=%u pixel=%u expected=%08x actual=%08x\n",
                    operation==1?"divide":operation==2?"add":operation==3?"subtract":"multiply",
                    batch,byte,i,expected[byte][i],model(result[byte])->data[i]);
                for(unsigned k=0;k<4;++k)fprintf(stderr,"input%u a=%08x b=%08x result=%08x expected=%08x\n",k,
                    model(a[k])->data[i],model(b[k])->data[i],model(result[k])->data[i],expected[k][i]);abort();}
        }
    }
    assert(!ps2_divide_release(&w) && !ps2_filter_release(&f));
    for(unsigned i=0;i<4;++i)assert(!ps2_surface_destroy(gs,a[i]) && !ps2_surface_destroy(gs,b[i]) && !ps2_surface_destroy(gs,out[i]));
    assert(!ps2_surface_destroy(gs,p));finish_rop();
}
/* Independent solid-fill Render oracle; included by ps2-composite.c. */
static PictureRec solid_test_picture(SourcePict *storage,const uint16_t rgba[4])
{
    *storage=(SourcePict){.solidFill={.type=SourcePictTypeSolidFill,
        .color=((uint32_t)(rgba[3]>>8)<<24)|((uint32_t)(rgba[0]>>8)<<16)|
            ((uint32_t)(rgba[1]>>8)<<8)|(rgba[2]>>8),
        .fullcolor={.red=rgba[0],.green=rgba[1],.blue=rgba[2],.alpha=rgba[3]}}};
    return (PictureRec){.pSourcePict=storage,.format=PICT_a8r8g8b8,.filter=PictFilterNearest};
}
static pixman_image_t *solid_test_reference(const uint16_t rgba[4])
{
    pixman_color_t c={.red=rgba[0],.green=rgba[1],.blue=rgba[2],.alpha=rgba[3]};
    pixman_image_t *image=pixman_image_create_solid_fill(&c);assert(image);return image;
}
static void solid_saturate_case(unsigned group)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    assert(!ps2_surface_set_depth(gs,source_handle,32));
    struct ps2_exa_context_bridge bridge={0};struct ps2_texture_exa textures={0};struct ps2_blit_exa compositor={0};
    assert(ps2_exa_context_bridge_prepare(&xs,&bridge,&exa,ctx));
    assert(ps2_texture_exa_prepare(&xs,&textures,&exa,ps2_pipe_context_render(ctx)));
    assert(ps2_blit_exa_prepare(&xs,&compositor,&exa,ctx));
    uint32_t mh=0;assert(!ps2_surface_create(gs,7,5,32,&mh) && !ps2_surface_set_depth(gs,mh,32));
    PixmapRec native={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=32,.bitsPerPixel=32,.width=83,.height=61},
        .devPrivates=(void*)&source_handle},mp={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=32,
        .bitsPerPixel=32,.width=7,.height=5},.devPrivates=(void*)&mh};
    const uint32_t formats[]={PICT_a8b8g8r8,PICT_a8r8g8b8,PICT_b8g8r8a8,
        PICT_a2r2g2b2,PICT_a2b2g2r2,PICT_a1r5g5b5,PICT_a1b5g5r5,PICT_a4r4g4b4,PICT_a4b4g4r4,
        PICT_r3g3b2,PICT_b2g3r3,PICT_r5g6b5,PICT_b5g6r5,PICT_x1r5g5b5,PICT_x1b5g5r5,
        PICT_x4r4g4b4,PICT_x4b4g4r4,PICT_a1,PICT_x4a4,PICT_a8};
    const unsigned alpha[]={0,1,127,128,255,256,257,32767,32768,65280,65407,65534,65535};
    PictTransform st={.matrix={{49152,21845,173},{0,98304,-21845},{65536/19,0,65536}}},
        mt={.matrix={{98304,0,257},{0,32768,21845},{0,0,65536}}};
    int32_t coefficients[]={2*65536,65536,2*65536,-65536};
    for(unsigned layout=group==0?0:group==1?3:17;layout<(group==0?3U:group==1?17U:20U);++layout){
        unsigned format=formats[layout],logical_depth=PIXMAN_FORMAT_DEPTH(format),bpp=PIXMAN_FORMAT_BPP(format),
            depth=bpp==8 || bpp==16?bpp:logical_depth,pitch;
        PixmapRec target={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=depth,.bitsPerPixel=bpp,.width=13,.height=7}};
        int native_pitch;target.devPrivates=exa.driver.CreatePixmap2(&xs,13,7,depth,0,bpp,&native_pitch);assert(target.devPrivates);
        target.devKind=native_pitch;pitch=(13*bpp+31)/32*4;
        struct ps2_alpha_source bytes;int raw=ps2_surface_exa_alpha_source(&target,&bytes);
        assert(raw==0 || raw==1);struct pixels *dm=raw?NULL:model(*(uint32_t*)target.devPrivates);
        for(unsigned colour=0;colour<(group==1?3U:13U);++colour)
        for(unsigned mode=0;mode<8;++mode){
            unsigned ai=group==1?(colour==0?3:colour==1?11:12):colour;
            unsigned a=alpha[ai],ma=alpha[(ai*3+7)%13];
            uint16_t rgba[]={a*73/100,a*31/100,a*51/100,a},mask_colour[]={ma*61/100,ma*89/100,ma*37/100,ma};
            SourcePict source_storage,mask_storage;
            PictureRec source=solid_test_picture(&source_storage,rgba),mask=solid_test_picture(&mask_storage,mask_colour),
                destination={.pDrawable=&target.drawable,.format=format,.filter=PictFilterNearest};
            unsigned masked=mode!=0,source_image=mode>=6,mask_image=mode>=1 && mode<=3;
            unsigned ca=mode==3 || mode==5 || mode==7,linear=(ai+mode)&1;
            rgba_seed(model(source_handle));rgba_seed(model(mh));
            if(source_image)source=(PictureRec){.pDrawable=&native.drawable,.format=PICT_a8b8g8r8,
                .filter=linear?PictFilterBilinear:PictFilterNearest,.transform=&st,.repeat=1,.repeatType=RepeatReflect};
            else if(mode==2){source.filter=PictFilterConvolution;source.filter_params=coefficients;source.filter_nparams=4;}
            if(mask_image)mask=(PictureRec){.pDrawable=&mp.drawable,.format=PICT_a8b8g8r8,
                .filter=linear?PictFilterBilinear:PictFilterNearest,.transform=&mt,.repeat=1,.repeatType=ai%4};
            else if(mode==5){mask.filter=PictFilterConvolution;mask.filter_params=coefficients;mask.filter_nparams=4;}
            mask.componentAlpha=ca;
            unsigned char expected[52*7];
            if(raw){for(unsigned i=0;i<bytes.bytes;++i)((unsigned char*)bytes.bits)[i]=i*73+19;
                assert(bytes.bytes<=sizeof(expected));memcpy(expected,bytes.bits,bytes.bytes);}
            else{rgba_seed(dm);memcpy(expected,dm->data,pitch*7);}
            void *source_memory=NULL,*mask_memory=NULL;
            pixman_image_t *si=source_image?reference_image(model(source_handle),32,0,1,&source_memory):solid_test_reference(rgba),
                *mi=!masked?NULL:mask_image?reference_image(model(mh),32,0,1,&mask_memory):solid_test_reference(mask_colour),
                *di=pixman_image_create_bits((pixman_format_code_t)format,13,7,(uint32_t*)expected,raw?bytes.stride:pitch);
            assert(si && di);if(source_image){assert(pixman_image_set_transform(si,(pixman_transform_t*)&st));
                pixman_image_set_repeat(si,PIXMAN_REPEAT_REFLECT);}
            assert(pixman_image_set_filter(si,source.filter==PictFilterConvolution?PIXMAN_FILTER_CONVOLUTION:
                source.filter==PictFilterBilinear?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,source.filter_params,source.filter_nparams));
            if(mi){pixman_image_set_component_alpha(mi,ca);
                if(mask_image){assert(pixman_image_set_transform(mi,(pixman_transform_t*)&mt));pixman_image_set_repeat(mi,(pixman_repeat_t)mask.repeatType);}
                assert(pixman_image_set_filter(mi,mask.filter==PictFilterConvolution?PIXMAN_FILTER_CONVOLUTION:
                    mask.filter==PictFilterBilinear?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,mask.filter_params,mask.filter_nparams));}
            pixman_image_composite32(PIXMAN_OP_SATURATE,si,mi,di,-1,-1,-1,1,3,2,2,1);
            assert(exa.driver.CheckComposite(PictOpSaturate,&source,masked?&mask:NULL,&destination));
            assert(exa.driver.PrepareComposite(PictOpSaturate,&source,masked?&mask:NULL,&destination,
                source_image?&native:NULL,masked && mask_image?&mp:NULL,&target));
            compositor_active=1;exa.driver.Composite(&target,-1,-1,-1,1,3,2,2,1);compositor_active=0;
            exa.driver.DoneComposite(&target);
            unsigned pixel_mask=(uint32_t)((UINT64_C(1)<<logical_depth)-1);
            for(unsigned y=0;y<7;++y)for(unsigned x=0;x<13;++x){
                unsigned wanted=raw_value(expected+y*(raw?bytes.stride:pitch),bpp,x)&pixel_mask,
                    actual=(raw?coverage_byte(&bytes,x,y):dm->data[y*13+x])&pixel_mask;
                if(wanted!=actual){fprintf(stderr,"solid Saturate layout=%u alpha=%u mode=%u linear=%u x=%u y=%u wanted=%08x got=%08x\n",
                    layout,a,mode,linear,x,y,wanted,actual);abort();}}
            assert(!compositor.solid && !compositor.solid_mask && !compositor.prepared);
            pixman_image_unref(si);if(mi)pixman_image_unref(mi);pixman_image_unref(di);free(source_memory);free(mask_memory);
        }
        exa.driver.DestroyPixmap(&xs,target.devPrivates);
    }
    assert(!ps2_surface_destroy(gs,mh));ps2_blit_exa_cleanup(&xs,&compositor);ps2_exa_context_bridge_cleanup(&bridge);finish_rop();
}
static void solid_disjoint_case(unsigned group,unsigned operation)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    assert(!ps2_surface_set_depth(gs,source_handle,32));
    struct ps2_exa_context_bridge bridge={0};struct ps2_texture_exa textures={0};struct ps2_blit_exa compositor={0};
    assert(ps2_exa_context_bridge_prepare(&xs,&bridge,&exa,ctx));
    assert(ps2_texture_exa_prepare(&xs,&textures,&exa,ps2_pipe_context_render(ctx)));
    assert(ps2_blit_exa_prepare(&xs,&compositor,&exa,ctx));
    uint32_t mh=0;assert(!ps2_surface_create(gs,7,5,32,&mh) && !ps2_surface_set_depth(gs,mh,32));
    PixmapRec native={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=32,.bitsPerPixel=32,.width=83,.height=61},
        .devPrivates=(void*)&source_handle},mp={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=32,
        .bitsPerPixel=32,.width=7,.height=5},.devPrivates=(void*)&mh};
    const uint32_t formats[]={PICT_a8b8g8r8,PICT_a8r8g8b8,PICT_b8g8r8a8,
        PICT_a2r2g2b2,PICT_a2b2g2r2,PICT_a1r5g5b5,PICT_a1b5g5r5,PICT_a4r4g4b4,PICT_a4b4g4r4,
        PICT_r3g3b2,PICT_b2g3r3,PICT_r5g6b5,PICT_b5g6r5,PICT_x1r5g5b5,PICT_x1b5g5r5,
        PICT_x4r4g4b4,PICT_x4b4g4r4,PICT_a1,PICT_x4a4,PICT_a8};
    const unsigned alpha[]={0,1,127,128,255,256,257,32767,32768,65280,65407,65534,65535};
    PictTransform st={.matrix={{49152,21845,173},{0,98304,-21845},{65536/19,0,65536}}},
        mt={.matrix={{98304,0,257},{0,32768,21845},{0,0,65536}}};
    int32_t coefficients[]={2*65536,65536,2*65536,-65536};
    for(unsigned layout=group==0?0:group==1?3:17;layout<(group==0?3U:group==1?17U:20U);++layout){
        unsigned format=formats[layout],logical_depth=PIXMAN_FORMAT_DEPTH(format),bpp=PIXMAN_FORMAT_BPP(format),
            depth=bpp==8 || bpp==16?bpp:logical_depth,pitch;
        PixmapRec target={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=depth,.bitsPerPixel=bpp,.width=13,.height=7}};
        int native_pitch;target.devPrivates=exa.driver.CreatePixmap2(&xs,13,7,depth,0,bpp,&native_pitch);assert(target.devPrivates);
        target.devKind=native_pitch;pitch=(13*bpp+31)/32*4;
        struct ps2_alpha_source bytes;int raw=ps2_surface_exa_alpha_source(&target,&bytes);
        assert(raw==0 || raw==1);struct pixels *dm=raw?NULL:model(*(uint32_t*)target.devPrivates);
        for(unsigned colour=0;colour<(group==1?3U:13U);++colour)
        for(unsigned mode=0;mode<8;++mode){
            unsigned ai=group==1?(colour==0?3:colour==1?11:12):colour;
            unsigned a=alpha[ai],ma=alpha[(ai*3+7)%13];
            uint16_t rgba[]={a*73/100,a*31/100,a*51/100,a},mask_colour[]={ma*61/100,ma*89/100,ma*37/100,ma};
            SourcePict source_storage,mask_storage;
            PictureRec source=solid_test_picture(&source_storage,rgba),mask=solid_test_picture(&mask_storage,mask_colour),
                destination={.pDrawable=&target.drawable,.format=format,.filter=PictFilterNearest};
            unsigned masked=mode!=0,source_image=mode>=6,mask_image=mode>=1 && mode<=3;
            unsigned ca=(mode==3 || mode==5 || mode==7) && !hsl_wire(operation),linear=(ai+mode)&1;
            rgba_seed(model(source_handle));rgba_seed(model(mh));
            if(source_image)source=(PictureRec){.pDrawable=&native.drawable,.format=PICT_a8b8g8r8,
                .filter=linear?PictFilterBilinear:PictFilterNearest,.transform=&st,.repeat=1,.repeatType=RepeatReflect};
            else if(mode==2){source.filter=PictFilterConvolution;source.filter_params=coefficients;source.filter_nparams=4;}
            if(mask_image)mask=(PictureRec){.pDrawable=&mp.drawable,.format=PICT_a8b8g8r8,
                .filter=linear?PictFilterBilinear:PictFilterNearest,.transform=&mt,.repeat=1,.repeatType=ai%4};
            else if(mode==5){mask.filter=PictFilterConvolution;mask.filter_params=coefficients;mask.filter_nparams=4;}
            mask.componentAlpha=ca;
            unsigned char expected[52*7];
            if(raw){for(unsigned i=0;i<bytes.bytes;++i)((unsigned char*)bytes.bits)[i]=i*73+19;
                assert(bytes.bytes<=sizeof(expected));memcpy(expected,bytes.bits,bytes.bytes);}
            else{rgba_seed(dm);memcpy(expected,dm->data,pitch*7);}
            void *source_memory=NULL,*mask_memory=NULL;
            pixman_image_t *si=source_image?reference_image(model(source_handle),32,0,1,&source_memory):solid_test_reference(rgba),
                *mi=!masked?NULL:mask_image?reference_image(model(mh),32,0,1,&mask_memory):solid_test_reference(mask_colour),
                *di=pixman_image_create_bits((pixman_format_code_t)format,13,7,(uint32_t*)expected,raw?bytes.stride:pitch);
            assert(si && di);if(source_image){assert(pixman_image_set_transform(si,(pixman_transform_t*)&st));
                pixman_image_set_repeat(si,PIXMAN_REPEAT_REFLECT);}
            assert(pixman_image_set_filter(si,source.filter==PictFilterConvolution?PIXMAN_FILTER_CONVOLUTION:
                source.filter==PictFilterBilinear?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,source.filter_params,source.filter_nparams));
            if(mi){pixman_image_set_component_alpha(mi,ca);
                if(mask_image){assert(pixman_image_set_transform(mi,(pixman_transform_t*)&mt));pixman_image_set_repeat(mi,(pixman_repeat_t)mask.repeatType);}
                assert(pixman_image_set_filter(mi,mask.filter==PictFilterConvolution?PIXMAN_FILTER_CONVOLUTION:
                    mask.filter==PictFilterBilinear?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,mask.filter_params,mask.filter_nparams));}
            pixman_image_composite32((pixman_op_t)operation,si,mi,di,-1,-1,-1,1,3,2,2,1);
            assert(exa.driver.CheckComposite(operation,&source,masked?&mask:NULL,&destination));
            assert(exa.driver.PrepareComposite(operation,&source,masked?&mask:NULL,&destination,
                source_image?&native:NULL,masked && mask_image?&mp:NULL,&target));
            compositor_active=1;exa.driver.Composite(&target,-1,-1,-1,1,3,2,2,1);compositor_active=0;
            exa.driver.DoneComposite(&target);
            unsigned pixel_mask=(uint32_t)((UINT64_C(1)<<logical_depth)-1);
            for(unsigned y=0;y<7;++y)for(unsigned x=0;x<13;++x){
                unsigned wanted=raw_value(expected+y*(raw?bytes.stride:pitch),bpp,x)&pixel_mask,
                    actual=(raw?coverage_byte(&bytes,x,y):dm->data[y*13+x])&pixel_mask;
                if(wanted!=actual){fprintf(stderr,"solid wide operation layout=%u alpha=%u mode=%u linear=%u x=%u y=%u wanted=%08x got=%08x\n",
                    layout,a,mode,linear,x,y,wanted,actual);abort();}}
            assert(!compositor.solid && !compositor.solid_mask && !compositor.prepared);
            pixman_image_unref(si);if(mi)pixman_image_unref(mi);pixman_image_unref(di);free(source_memory);free(mask_memory);
        }
        exa.driver.DestroyPixmap(&xs,target.devPrivates);
    }
    assert(!ps2_surface_destroy(gs,mh));ps2_blit_exa_cleanup(&xs,&compositor);ps2_exa_context_bridge_cleanup(&bridge);finish_rop();
}
static void solid_saturate_guards(void)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    assert(!ps2_surface_set_depth(gs,destination_handle(),32));uint32_t sh=0,mh=0;
    assert(!ps2_surface_create(gs,1,1,32,&sh) && !ps2_surface_create(gs,1,1,32,&mh));
    for(unsigned invalid=0;invalid<11;++invalid){
        struct ps2_picture_sample s={.source_solid=1,.solid_rgba={0x1234,0x2711,0x3189,0x8187},
            .has_alpha=1,.alpha=0x81,.repeat=PS2_REPEAT_NORMAL,.destination_argb=1};
        struct ps2_picture_bitmap m={.has_surface=1,.surface=mh,.width=1,.height=1,
            .sample={.source_solid=1,.solid_rgba={0x2711,0x1234,0x3189,0x8187},
                .has_alpha=1,.alpha=0x81,.repeat=PS2_REPEAT_NORMAL}};
        uint32_t source=sh;unsigned op=PS2_COMPOSITE_SATURATE,features=gs->caps.features;
        if(invalid==0)s.source_solid=2;
        if(invalid==1)s.alpha=0x80;
        if(invalid==2)s.repeat=PS2_REPEAT_NONE;
        if(invalid==3)s.source_argb=1;
        if(invalid==4)source=source_handle;
        if(invalid==5)m.has_surface=0;
        if(invalid==6)m.sample.has_alpha=0;
        if(invalid==7)m.sample.source_alpha_map.enabled=1;
        if(invalid==8)op=PS2_COMPOSITE_SRC;
        if(invalid==9)m.sample.source_solid=2;
        if(invalid==10)gs->caps.features&=~PS2GS_HW_CHANNEL_MAP;
        struct ps2_pipe_copy_box box={0,0,3,2,2,1};unsigned before=attempts,draws=transfer_calls;
        assert(ps2_pipe_composite_rect(ctx,source,destination_handle(),&box,&s,&m,op)<0 &&
            errno==(invalid==10?ENOTSUP:EINVAL));assert(attempts==before && draws==transfer_calls);
        gs->caps.features=features;
    }
    assert(!ps2_surface_destroy(gs,sh) && !ps2_surface_destroy(gs,mh));finish_rop();
}
static PixmapPtr map_fixture_pixmap(unsigned format,unsigned native16)
{
    PixmapPtr p=calloc(1,sizeof(*p));assert(p);int pitch;
    unsigned bpp=PIXMAN_FORMAT_BPP(format),depth=bpp==8 || (bpp==16 && !native16)?bpp:PIXMAN_FORMAT_DEPTH(format);
    p->drawable=(DrawableRec){.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=depth,.bitsPerPixel=bpp,.width=13,.height=7};
    p->devPrivates=exa.driver.CreatePixmap2(&xs,13,7,depth,0,bpp,&pitch);assert(p->devPrivates);p->devKind=pitch;
    return p;
}
static void map_fixture_seed(PixmapPtr p,unsigned salt)
{
    struct ps2_alpha_source raw;int owned=ps2_surface_exa_alpha_source(p,&raw);
    if(owned==1){for(unsigned i=0;i<raw.bytes;++i)((unsigned char*)raw.bits)[i]=(i*73+salt*37+19)^(i*17);}
    else{assert(!owned);struct pixels *m=model(*(uint32_t*)p->devPrivates);
        for(unsigned i=0;i<m->width*m->height;++i)m->data[i]=(i*7919+salt*65537+0x196131)^(i*0x10103U);}
}
static pixman_image_t *map_fixture_image(PixmapPtr p,unsigned format,void **memory)
{
    unsigned bpp=PIXMAN_FORMAT_BPP(format),stride=(13*bpp+31)/32*4;
    unsigned char *bytes=calloc(stride,7);assert(bytes);struct ps2_alpha_source raw;
    if(ps2_surface_exa_alpha_source(p,&raw)==1){for(unsigned y=0;y<7;++y)memcpy(bytes+y*stride,raw.bits+y*raw.stride,stride);}
    else{struct pixels *m=model(*(uint32_t*)p->devPrivates);
        for(unsigned y=0;y<7;++y)for(unsigned x=0;x<13;++x)memcpy(bytes+y*stride+x*(bpp/8),m->data+y*13+x,bpp/8);}
    *memory=bytes;pixman_image_t *image=pixman_image_create_bits(format,13,7,(uint32_t*)bytes,stride);assert(image);return image;
}
static void map_fixture_expect(PixmapPtr p,unsigned format,const void *memory,unsigned layout,unsigned kind,unsigned filter)
{
    unsigned bpp=PIXMAN_FORMAT_BPP(format),stride=(13*bpp+31)/32*4,
        mask=(uint32_t)((UINT64_C(1)<<PIXMAN_FORMAT_DEPTH(format))-1);
    struct ps2_alpha_source raw;int owned=ps2_surface_exa_alpha_source(p,&raw);struct pixels *m=owned==1?NULL:model(*(uint32_t*)p->devPrivates);
    for(unsigned y=0;y<7;++y)for(unsigned x=0;x<13;++x){
        unsigned wanted=raw_value((const unsigned char*)memory+y*stride,bpp,x)&mask,
            actual=(owned==1?coverage_byte(&raw,x,y):m->data[y*13+x])&mask;
        if(wanted!=actual){fprintf(stderr,"Saturate map format=%08x layout=%u map=%u filter=%u x=%u y=%u expected=%08x got=%08x\n",
            format,layout,kind,filter,x,y,wanted,actual);abort();}
    }
}
static void saturate_map_exa_case(unsigned group,unsigned convolution)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    assert(!ps2_surface_set_depth(gs,source_handle,32));
    struct ps2_exa_context_bridge bridge={0};struct ps2_texture_exa textures={0};struct ps2_blit_exa compositor={0};
    assert(ps2_exa_context_bridge_prepare(&xs,&bridge,&exa,ctx));
    assert(ps2_texture_exa_prepare(&xs,&textures,&exa,ps2_pipe_context_render(ctx)));
    assert(ps2_blit_exa_prepare(&xs,&compositor,&exa,ctx));
    uint32_t mh=0;assert(!ps2_surface_create(gs,7,5,32,&mh) && !ps2_surface_set_depth(gs,mh,32));
    PixmapRec sp={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=32,.bitsPerPixel=32,.width=83,.height=61},.devPrivates=(void*)&source_handle},
        mp={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=32,.bitsPerPixel=32,.width=7,.height=5},.devPrivates=(void*)&mh};
    const unsigned formats[]={PICT_a8r8g8b8,PICT_a8b8g8r8,PICT_b8g8r8a8,PICT_x8r8g8b8,PICT_x1b5g5r5,
        PICT_a2r2g2b2,PICT_a2b2g2r2,PICT_r3g3b2,PICT_b2g3r3,PICT_r5g6b5,PICT_b5g6r5,
        PICT_a1r5g5b5,PICT_a1b5g5r5,PICT_a4r4g4b4,PICT_a4b4g4r4,
        PICT_x1r5g5b5,PICT_x1b5g5r5,PICT_x4r4g4b4,PICT_x4b4g4r4,PICT_a1,PICT_x4a4,PICT_a8},
        map_formats[]={PICT_a1,PICT_x4a4,PICT_a8,PICT_a2r2g2b2,PICT_a4r4g4b4,PICT_b8g8r8a8,PICT_r5g6b5,PICT_x8r8g8b8};
    PictTransform st={.matrix={{49152,21845,173},{0,98304,-21845},{65536/19,0,65536}}};
    int32_t kernel[]={2*65536,2*65536,16384,16384,16384,16384};
    for(unsigned layout=group==0?0:group==1?5:19;layout<(group==0?5U:group==1?19U:22U);++layout){
        PixmapPtr dp=map_fixture_pixmap(formats[layout],layout==4);
        for(unsigned kind=0;kind<(convolution?3U:8U);++kind){PixmapPtr maps[3];
            for(unsigned i=0;i<3;++i)maps[i]=map_fixture_pixmap(map_formats[kind],0);
            for(unsigned filter=0;filter<2;++filter){
                map_fixture_seed(dp,layout+17);for(unsigned i=0;i<3;++i)map_fixture_seed(maps[i],kind*3+i);
                rgba_seed(model(source_handle));rgba_seed(model(mh));
                PictureRec am[3];pixman_image_t *ai[3];void *ab[3];
                for(unsigned i=0;i<3;++i){unsigned alias=filter && i<2 && (kind+i)%3==0;
                    PixmapPtr image=maps[alias?2:i];am[i]=(PictureRec){.pDrawable=&image->drawable,.format=map_formats[kind],.filter=PictFilterNearest};
                    ai[i]=map_fixture_image(image,map_formats[kind],ab+i);}
                PictureRec source={.pDrawable=&sp.drawable,.format=PICT_a8r8g8b8,.filter=filter?PictFilterBilinear:PictFilterNearest,
                    .repeat=1,.repeatType=RepeatReflect,.transform=&st,.alphaMap=&am[0],.alphaOrigin={-1,-1}},
                    mask={.pDrawable=&mp.drawable,.format=PICT_a8b8g8r8,.filter=filter?PictFilterBilinear:PictFilterNearest,
                    .repeat=1,.repeatType=RepeatReflect,.transform=&st,.alphaMap=&am[1],.alphaOrigin={1,0},.componentAlpha=kind&1},
                    destination={.pDrawable=&dp->drawable,.format=formats[layout],.filter=PictFilterNearest,.alphaMap=&am[2],.alphaOrigin={1,-1}};
                if(convolution){source.filter=PictFilterConvolution;source.filter_params=kernel;source.filter_nparams=6;
                    if(filter){mask.filter=PictFilterConvolution;mask.filter_params=kernel;mask.filter_nparams=6;}}
                void *sb,*mb,*db;pixman_image_t *si=reference_image(model(source_handle),32,1,1,&sb),
                    *mi=reference_image(model(mh),32,0,1,&mb),*di=map_fixture_image(dp,formats[layout],&db);
                pixman_image_set_alpha_map(si,ai[0],-1,-1);pixman_image_set_alpha_map(mi,ai[1],1,0);pixman_image_set_alpha_map(di,ai[2],1,-1);
                pixman_image_set_component_alpha(mi,mask.componentAlpha);
                assert(pixman_image_set_transform(si,(pixman_transform_t*)&st) && pixman_image_set_transform(mi,(pixman_transform_t*)&st));
                assert(pixman_image_set_filter(si,convolution?PIXMAN_FILTER_CONVOLUTION:filter?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,
                    source.filter_params,source.filter_nparams) &&
                    pixman_image_set_filter(mi,convolution && filter?PIXMAN_FILTER_CONVOLUTION:filter?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,
                    mask.filter_params,mask.filter_nparams));
                pixman_image_set_repeat(si,PIXMAN_REPEAT_REFLECT);pixman_image_set_repeat(mi,PIXMAN_REPEAT_REFLECT);
                pixman_image_composite32(PIXMAN_OP_SATURATE,si,mi,di,-1,1,2,1,3,2,1,1);
                assert(exa.driver.CheckComposite(PictOpSaturate,&source,&mask,&destination));
                assert(exa.driver.PrepareComposite(PictOpSaturate,&source,&mask,&destination,&sp,&mp,dp));
                compositor_active=1;exa.driver.Composite(dp,-1,1,2,1,3,2,1,1);compositor_active=0;exa.driver.DoneComposite(dp);
                map_fixture_expect(dp,formats[layout],db,layout,kind,filter);map_fixture_expect(maps[2],map_formats[kind],ab[2],layout,kind,filter);
                pixman_image_unref(si);pixman_image_unref(mi);pixman_image_unref(di);free(sb);free(mb);free(db);
                for(unsigned i=0;i<3;++i){pixman_image_unref(ai[i]);free(ab[i]);}
            }
            for(unsigned i=0;i<3;++i)coverage_test_destroy(maps[i]);
        }
        coverage_test_destroy(dp);
    }
    assert(!ps2_surface_destroy(gs,mh));ps2_blit_exa_cleanup(&xs,&compositor);ps2_exa_context_bridge_cleanup(&bridge);finish_rop();
}
static void shifted_map_exa_case(unsigned group)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    assert(!ps2_surface_set_depth(gs,source_handle,32));
    struct ps2_exa_context_bridge bridge={0};struct ps2_texture_exa textures={0};struct ps2_blit_exa compositor={0};
    assert(ps2_exa_context_bridge_prepare(&xs,&bridge,&exa,ctx));
    assert(ps2_texture_exa_prepare(&xs,&textures,&exa,ps2_pipe_context_render(ctx)));
    assert(ps2_blit_exa_prepare(&xs,&compositor,&exa,ctx));
    uint32_t mh=0;assert(!ps2_surface_create(gs,7,5,32,&mh) && !ps2_surface_set_depth(gs,mh,32));
    PixmapRec sp={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=32,.bitsPerPixel=32,.width=83,.height=61},.devPrivates=(void*)&source_handle},
        mp={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=32,.bitsPerPixel=32,.width=7,.height=5},.devPrivates=(void*)&mh};
    const unsigned formats[]={PICT_a8r8g8b8,PICT_a8b8g8r8,PICT_b8g8r8a8,PICT_x8r8g8b8,PICT_x1b5g5r5,
        PICT_a2r2g2b2,PICT_a2b2g2r2,PICT_r3g3b2,PICT_b2g3r3,PICT_r5g6b5,PICT_b5g6r5,
        PICT_a1r5g5b5,PICT_a1b5g5r5,PICT_a4r4g4b4,PICT_a4b4g4r4,
        PICT_x1r5g5b5,PICT_x1b5g5r5,PICT_x4r4g4b4,PICT_x4b4g4r4,PICT_a1,PICT_x4a4,PICT_a8};
    const DDXPointRec origins[]={{1,0},{-1,0},{0,1},{1,-1}};
    PictTransform st={.matrix={{49152,21845,173},{0,98304,-21845},{65536/19,0,65536}}};
    for(unsigned layout=group==0?0:group==1?5:19;layout<(group==0?5U:group==1?19U:22U);++layout){
        PixmapPtr dp=map_fixture_pixmap(formats[layout],layout==4);
        for(unsigned origin=0;origin<4;++origin)for(unsigned filter=0;filter<2;++filter){
            map_fixture_seed(dp,layout+17);rgba_seed(model(source_handle));rgba_seed(model(mh));
            PictureRec map={.pDrawable=&dp->drawable,.format=formats[layout],.filter=PictFilterNearest},
                destination={.pDrawable=&dp->drawable,.format=formats[layout],.filter=PictFilterNearest,.alphaMap=&map,.alphaOrigin=origins[origin]},
                source={.pDrawable=&sp.drawable,.format=PICT_a8r8g8b8,.filter=filter?PictFilterBilinear:PictFilterNearest,
                    .repeat=1,.repeatType=RepeatReflect,.transform=&st},
                mask={.pDrawable=&mp.drawable,.format=PICT_a8b8g8r8,.filter=filter?PictFilterBilinear:PictFilterNearest,
                    .repeat=1,.repeatType=RepeatReflect,.transform=&st,.componentAlpha=filter};
            void *sb,*mb,*db;pixman_image_t *si=reference_image(model(source_handle),32,1,1,&sb),
                *mi=reference_image(model(mh),32,0,1,&mb),*di=map_fixture_image(dp,formats[layout],&db),
                *ai=pixman_image_create_bits(formats[layout],13,7,db,(13*PIXMAN_FORMAT_BPP(formats[layout])+31)/32*4);
            assert(ai);pixman_image_set_alpha_map(di,ai,origins[origin].x,origins[origin].y);
            pixman_image_set_component_alpha(mi,filter);
            assert(pixman_image_set_transform(si,(pixman_transform_t*)&st) && pixman_image_set_transform(mi,(pixman_transform_t*)&st));
            assert(pixman_image_set_filter(si,filter?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,NULL,0) &&
                pixman_image_set_filter(mi,filter?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,NULL,0));
            pixman_image_set_repeat(si,PIXMAN_REPEAT_REFLECT);pixman_image_set_repeat(mi,PIXMAN_REPEAT_REFLECT);
            pixman_image_composite32(PIXMAN_OP_SATURATE,si,mi,di,-1,1,2,1,3,2,3,2);
            assert(exa.driver.CheckComposite(PictOpSaturate,&source,&mask,&destination));
            assert(exa.driver.PrepareComposite(PictOpSaturate,&source,&mask,&destination,&sp,&mp,dp));
            assert(compositor.sample.destination_alpha_map.surface==compositor.destination.handle);
            compositor_active=1;exa.driver.Composite(dp,-1,1,2,1,3,2,3,2);compositor_active=0;exa.driver.DoneComposite(dp);
            map_fixture_expect(dp,formats[layout],db,layout,origin,filter);
            pixman_image_unref(si);pixman_image_unref(mi);pixman_image_unref(di);pixman_image_unref(ai);free(sb);free(mb);free(db);
        }
        coverage_test_destroy(dp);
    }
    assert(!ps2_surface_destroy(gs,mh));ps2_blit_exa_cleanup(&xs,&compositor);ps2_exa_context_bridge_cleanup(&bridge);finish_rop();
}
static void shifted_map_boundaries(void)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    uint32_t dh=0;assert(!ps2_surface_create(gs,83,61,32,&dh) && !ps2_surface_set_depth(gs,dh,32) &&
        !ps2_surface_set_depth(gs,source_handle,32));
    const int origins[4][2]={{1,0},{-1,0},{0,1},{1,-1}};
    const unsigned operations[]={PS2_COMPOSITE_SRC,PS2_COMPOSITE_OVER,PS2_COMPOSITE_XOR};
    const pixman_op_t reference_ops[]={PIXMAN_OP_SRC,PIXMAN_OP_OVER,PIXMAN_OP_XOR};
    for(unsigned layout=0;layout<3;++layout)for(unsigned origin=0;origin<4;++origin)
    for(unsigned alias=0;alias<3;++alias)for(unsigned op=0;op<3;++op){
        rgba_seed(model(source_handle));rgba_seed(model(dh));
        struct ps2_picture_sample sample={.source_argb=1,.destination_argb=1,.source_swap=layout==1,
            .destination_swap=layout==1,.source_rotate=layout==2,.destination_rotate=layout==2,.repeat=PS2_REPEAT_REFLECT,
            .destination_alpha_map={.enabled=1,.surface=dh,.width=83,.height=61,.has_alpha=1,
                .swap=layout==1,.rotate=layout==2,.x=origins[origin][0],.y=origins[origin][1]}};
        struct ps2_picture_bitmap mask={.has_surface=1,.surface=source_handle,.width=83,.height=61,
            .x=2,.y=1,.component_alpha=alias==2,.sample={.source_argb=1,.repeat=PS2_REPEAT_REFLECT}};
        if(alias==2){sample.source_alpha_map=sample.destination_alpha_map;
            mask.sample.source_alpha_map=sample.destination_alpha_map;}
        uint32_t sh=alias==1?dh:source_handle;void *sb,*mb,*db,*sab=NULL,*mab=NULL;
        pixman_image_t *si=reference_image_layout(model(sh),32,layout==1,1,layout==2,&sb),
            *mi=reference_image(model(source_handle),32,0,1,&mb),
            *di=reference_image_layout(model(dh),32,layout==1,1,layout==2,&db),
            *ai=pixman_image_create_bits(layout==2?PIXMAN_b8g8r8a8:layout==1?PIXMAN_a8r8g8b8:PIXMAN_a8b8g8r8,83,61,db,83*4),
            *sai=NULL,*mai=NULL;
        assert(ai);pixman_image_set_alpha_map(di,ai,origins[origin][0],origins[origin][1]);
        if(alias==2){sai=reference_alpha_map(&sample.source_alpha_map,&sab);mai=reference_alpha_map(&mask.sample.source_alpha_map,&mab);
            pixman_image_set_alpha_map(si,sai,origins[origin][0],origins[origin][1]);
            pixman_image_set_alpha_map(mi,mai,origins[origin][0],origins[origin][1]);}
        configure(si,&sample);configure(mi,&mask.sample);pixman_image_set_component_alpha(mi,mask.component_alpha);
        pixman_image_composite32(reference_ops[op],si,mi,di,2,1,2,1,3,2,71,3);
        struct ps2_pipe_copy_box box={2,1,3,2,71,3};compositor_active=1;
        assert(!ps2_pipe_composite_rect(ctx,sh,dh,&box,&sample,&mask,operations[op]));compositor_active=0;
        for(unsigned i=0;i<83*61;++i)if(model(dh)->data[i]!=((uint32_t*)db)[i]){
            fprintf(stderr,"shifted map boundary layout=%u origin=%u alias=%u op=%u index=%u wanted=%08x got=%08x\n",
                layout,origin,alias,op,i,((uint32_t*)db)[i],model(dh)->data[i]);abort();}
        pixman_image_unref(si);pixman_image_unref(mi);pixman_image_unref(di);pixman_image_unref(ai);
        if(sai)pixman_image_unref(sai);
        if(mai)pixman_image_unref(mai);
        free(sb);free(mb);free(db);free(sab);free(mab);
    }
    assert(!ps2_surface_destroy(gs,dh));finish_rop();
}
static void saturate_map_guards(void)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    struct ps2_divide_workspace w={0};struct ps2_filter_workspace f={0};uint32_t fp[16]={0},h[4]={0},p=0;
    assert(!ps2_divide_reserve(&w,gs) && !ps2_filter_reserve(&f,gs,3));
    for(unsigned i=0;i<16;++i)assert(!ps2_surface_create(gs,64,32,32,fp+i));
    for(unsigned i=0;i<4;++i)assert(!ps2_surface_create(gs,64,32,32,h+i));
    assert(!ps2_surface_create(gs,16,16,32,&p));
    for(unsigned alpha=0;alpha<2;++alpha)for(unsigned invalid=0;invalid<13;++invalid){
        struct ps2_saturate_map_target target={.input_bits=4,.map=h[2],.map_bits=1,.colour=alpha?0:h[3],.rgb_bits={5,6,5}};
        unsigned features=gs->caps.features,width=1,height=1;
        switch(invalid){case 0:target.input_bits=0;break;case 1:target.input_bits=9;break;
        case 2:target.map_bits=0;break;case 3:target.map_bits=9;break;
        case 4:target.map=h[1];break;case 5:target.map=fp[4];break;
        case 6:target.map=w.term;break;case 7:target.map=p;break;
        case 8:if(alpha)target.colour=h[3];else target.colour=fp[0];break;
        case 9:if(alpha)target.map=0;else target.rgb_bits[1]=9;break;
        case 10:width=65;break;case 11:height=0;break;
        case 12:gs->caps.features&=~PS2GS_HW_CHANNEL_MAP;break;}
        unsigned before=attempts,draws=transfer_calls;struct ps2_unorm_layout layout={.rgb_bits={8,8,8},.alpha_bits=8};
        int status=alpha?ps2_native_saturate_alpha_float_mapped(&w,&f,fp+4,fp+12,h[1],8,&target,p,width,height):
            ps2_native_saturate_float_mapped(&w,&f,fp,fp+4,fp+8,fp+12,h[0],h[1],&layout,&target,p,width,height);
        if(!(status<0 && errno==(invalid==12?ENOTSUP:EINVAL))){
            fprintf(stderr,"map guard alpha=%u invalid=%u status=%d errno=%d\n",alpha,invalid,status,errno);abort();}
        assert(attempts==before && transfer_calls==draws);gs->caps.features=features;
    }
    assert(!ps2_divide_release(&w) && !ps2_filter_release(&f));
    for(unsigned i=0;i<16;++i)assert(!ps2_surface_destroy(gs,fp[i]));
    for(unsigned i=0;i<4;++i)assert(!ps2_surface_destroy(gs,h[i]));
    assert(!ps2_surface_destroy(gs,p));finish_rop();
}
static void pd_rounding_case(void)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    assert(!ps2_surface_set_depth(gs,source_handle,32) && !ps2_surface_set_depth(gs,destination_handle(),32));
    for(unsigned i=0;i<model(source_handle)->width*model(source_handle)->height;++i)model(source_handle)->data[i]=0xff010101;
    struct ps2_picture_sample sample=identity(PS2_REPEAT_NORMAL);sample.destination_argb=1;
    struct ps2_pipe_copy_box box={0,0,3,2,1,1};
    unsigned result[2];
    for(unsigned i=0;i<2;++i){struct pixels *d=model(destination_handle());
        for(unsigned n=0;n<d->width*d->height;++n)d->data[n]=0x01000000;
        composite_reference(source_handle,destination_handle(),&box,&sample,NULL,
            i?PS2_COMPOSITE_DISJOINT_OVER_REVERSE:PS2_COMPOSITE_SATURATE);
        result[i]=d->data[2*d->width+3];
    }
    assert(result[0]==0xff010101 && result[1]==0xff000000);
    printf("PS2 PD opaque rounding Saturate=%08x DisjointOverReverse=%08x PASS\n",result[0],result[1]);
    finish_rop();
}
static void pd_ratio_guards_case(void)
{
    unsigned guards=0;
    for(unsigned failure=0;failure<3;++failure){
        rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
        struct ps2_divide_workspace w={0};struct ps2_filter_workspace f={0};uint32_t fp[16]={0},h[4]={0},p=0;
        assert(!ps2_divide_reserve(&w,gs) && !ps2_filter_reserve(&f,gs,3));
        for(unsigned i=0;i<16;++i)assert(!ps2_surface_create(gs,64,32,32,fp+i));
        for(unsigned i=0;i<4;++i)assert(!ps2_surface_create(gs,64,32,32,h+i));
        assert(!ps2_surface_create(gs,16,16,32,&p));
        struct ps2_unorm_layout layout={.rgb_bits={8,8,8},.alpha_bits=8};
        if(!failure){
            for(unsigned alpha=0;alpha<2;++alpha)for(unsigned invalid=0;invalid<27;++invalid){
                struct ps2_saturate_map_target target={.input_bits=4,.map=h[2],.map_bits=1,
                    .colour=alpha?0:h[3],.rgb_bits={5,6,5}};
                unsigned features=gs->caps.features,width=1,height=1,operation=PS2_COMPOSITE_CONJOINT_XOR,abits=8;
                const uint32_t *sa=fp+4,*ma=fp+12;const struct ps2_unorm_layout *precision=&layout;
                switch(invalid){
                case 0:target.input_bits=0;break;case 1:target.input_bits=9;break;
                case 2:target.map_bits=0;break;case 3:target.map_bits=9;break;
                case 4:target.map=h[1];break;case 5:target.map=fp[4];break;
                case 6:target.map=w.term;break;case 7:target.map=p;break;
                case 8:if(alpha)target.colour=h[3];else target.colour=fp[0];break;
                case 9:if(alpha)target.map=0;else target.rgb_bits[1]=9;break;
                case 10:width=65;break;case 11:height=0;break;
                case 12:gs->caps.features&=~PS2GS_HW_CHANNEL_MAP;break;
                case 13:operation=PS2_COMPOSITE_DISJOINT_OVER-1;break;
                case 14:operation=PS2_COMPOSITE_CONJOINT_XOR+1;break;
                case 15:operation=UINT_MAX;break;case 16:operation=0;break;
                case 17:sa=NULL;break;case 18:ma=NULL;break;
                case 19:if(alpha)abits=9;else precision=NULL;break;
                case 20:width=0;break;case 21:height=33;break;
                case 22:target.map=h[0];if(alpha)target.map=h[1];break;
                case 23:target.map=fp[12];break;
                case 24:target.map=f.tap;break;
                case 25:target.map=source_handle;break;
                case 26:target.colour=target.map;break;
                }
                unsigned before=attempts,draws=transfer_calls;
                int status=alpha?ps2_native_porter_duff_alpha_float_mapped(&w,&f,sa,ma,h[1],abits,&target,p,width,height,operation):
                    ps2_native_porter_duff_float_mapped(&w,&f,fp,sa,fp+8,ma,h[0],h[1],precision,&target,p,width,height,operation);
                assert(status<0 && errno==(invalid==12?ENOTSUP:EINVAL) && attempts==before && transfer_calls==draws);
                gs->caps.features=features;++guards;
            }
            assert(!ps2_divide_release(&w) && !ps2_filter_release(&f));
            for(unsigned i=0;i<16;++i)assert(!ps2_surface_destroy(gs,fp[i]));
            for(unsigned i=0;i<4;++i)assert(!ps2_surface_destroy(gs,h[i]));
            assert(!ps2_surface_destroy(gs,p));finish_rop();
        }else{
            fail_create=failure==1;transfer_failure=failure==2;
            assert(ps2_native_porter_duff_float_mapped(&w,&f,fp,fp+4,fp+8,fp+12,h[0],h[1],&layout,NULL,p,1,1,
                PS2_COMPOSITE_DISJOINT_ATOP)<0 && errno==(failure==1?ENOSPC:EIO) && gs->error==(failure==1?0:EIO));
            fail_create=transfer_failure=0;unsigned before=attempts,draws=transfer_calls;
            int retry=ps2_native_porter_duff_float_mapped(&w,&f,fp,fp+4,fp+8,fp+12,h[0],h[1],&layout,NULL,p,1,1,
                PS2_COMPOSITE_CONJOINT_XOR);
            if(failure==1)assert(!retry && !gs->error && attempts>before);
            else assert(retry<0 && gs->error==EIO && attempts==before && transfer_calls==draws);
            polygon_finish();
            for(unsigned i=0;i<PS2GS_MAX_SURFACES+1;++i)assert(!models[i].data);
        }
    }
    assert(guards==54);printf("PS2 PD ratio direct preflight guards=%u allocation recovery=1 submission failure latching=1 PASS\n",guards);
}
static void dodge_burn_guards_case(void)
{
    unsigned guards=0;
    for(unsigned failure=0;failure<3;++failure){
        rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
        struct ps2_divide_workspace w={0};struct ps2_filter_workspace f={0};uint32_t fp[16]={0},h[4]={0},p=0;
        assert(!ps2_divide_reserve(&w,gs) && !ps2_filter_reserve(&f,gs,3));
        for(unsigned i=0;i<16;++i)assert(!ps2_surface_create(gs,64,32,32,fp+i));
        for(unsigned i=0;i<4;++i)assert(!ps2_surface_create(gs,64,32,32,h+i));
        assert(!ps2_surface_create(gs,16,16,32,&p));
        struct ps2_unorm_layout layout={.rgb_bits={8,8,8},.alpha_bits=8};
        if(!failure){
            for(unsigned alpha=0;alpha<2;++alpha)for(unsigned invalid=0;invalid<27;++invalid){
                struct ps2_saturate_map_target target={.input_bits=4,.map=h[2],.map_bits=1,
                    .colour=alpha?0:h[3],.rgb_bits={5,6,5}};
                unsigned features=gs->caps.features,width=1,height=1,operation=0,abits=8;
                const uint32_t *sa=fp+4,*ma=fp+12;const struct ps2_unorm_layout *precision=&layout;
                switch(invalid){
                case 0:target.input_bits=0;break;case 1:target.input_bits=9;break;
                case 2:target.map_bits=0;break;case 3:target.map_bits=9;break;
                case 4:target.map=h[1];break;case 5:target.map=fp[4];break;
                case 6:target.map=w.term;break;case 7:target.map=p;break;
                case 8:if(alpha)target.colour=h[3];else target.colour=fp[0];break;
                case 9:if(alpha)target.map=0;else target.rgb_bits[1]=9;break;
                case 10:width=65;break;case 11:height=0;break;
                case 12:gs->caps.features&=~PS2GS_HW_CHANNEL_MAP;break;
                case 13:operation=2;break;
                case 14:operation=3;break;
                case 15:operation=UINT_MAX;break;case 16:operation=4;break;
                case 17:sa=NULL;break;case 18:ma=NULL;break;
                case 19:if(alpha)abits=9;else precision=NULL;break;
                case 20:width=0;break;case 21:height=33;break;
                case 22:target.map=h[0];if(alpha)target.map=h[1];break;
                case 23:target.map=fp[12];break;
                case 24:target.map=f.tap;break;
                case 25:target.map=source_handle;break;
                case 26:target.colour=target.map;break;
                }
                unsigned before=attempts,draws=transfer_calls;
                int status=alpha?ps2_native_dodge_burn_alpha_float_mapped(&w,&f,sa,ma,h[1],abits,&target,p,width,height,operation):
                    ps2_native_dodge_burn_float_mapped(&w,&f,fp,sa,fp+8,ma,h[0],h[1],precision,&target,p,width,height,operation);
                assert(status<0 && errno==(invalid==12?ENOTSUP:EINVAL) && attempts==before && transfer_calls==draws);
                gs->caps.features=features;++guards;
            }
            assert(!ps2_divide_release(&w) && !ps2_filter_release(&f));
            for(unsigned i=0;i<16;++i)assert(!ps2_surface_destroy(gs,fp[i]));
            for(unsigned i=0;i<4;++i)assert(!ps2_surface_destroy(gs,h[i]));
            assert(!ps2_surface_destroy(gs,p));finish_rop();
        }else{
            fail_create=failure==1;transfer_failure=failure==2;
            assert(ps2_native_dodge_burn_float_mapped(&w,&f,fp,fp+4,fp+8,fp+12,h[0],h[1],&layout,NULL,p,1,1,
                0)<0 && errno==(failure==1?ENOSPC:EIO) && gs->error==(failure==1?0:EIO));
            fail_create=transfer_failure=0;unsigned before=attempts,draws=transfer_calls;
            int retry=ps2_native_dodge_burn_float_mapped(&w,&f,fp,fp+4,fp+8,fp+12,h[0],h[1],&layout,NULL,p,1,1,
                1);
            if(failure==1)assert(!retry && !gs->error && attempts>before);
            else assert(retry<0 && gs->error==EIO && attempts==before && transfer_calls==draws);
            polygon_finish();
            for(unsigned i=0;i<PS2GS_MAX_SURFACES+1;++i)assert(!models[i].data);
        }
    }
    assert(guards==54);printf("PS2 dodge/burn direct preflight guards=%u allocation recovery=1 submission failure latching=1 PASS\n",guards);
}
static void hsl_guards_case(void)
{
    unsigned guards=0;
    for(unsigned failure=0;failure<3;++failure){
        rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
        struct ps2_divide_workspace w={0};struct ps2_filter_workspace f={0};uint32_t fp[16]={0},h[4]={0},p=0;
        assert(!ps2_divide_reserve(&w,gs) && !ps2_filter_reserve(&f,gs,3));
        for(unsigned i=0;i<16;++i)assert(!ps2_surface_create(gs,64,32,32,fp+i));
        for(unsigned i=0;i<4;++i)assert(!ps2_surface_create(gs,64,32,32,h+i));
        assert(!ps2_surface_create(gs,16,16,32,&p));
        struct ps2_unorm_layout layout={.rgb_bits={8,8,8},.alpha_bits=8};
        if(!failure){
            for(unsigned alpha=0;alpha<2;++alpha)for(unsigned invalid=0;invalid<27;++invalid){
                struct ps2_saturate_map_target target={.input_bits=4,.map=h[2],.map_bits=1,
                    .colour=alpha?0:h[3],.rgb_bits={5,6,5}};
                unsigned features=gs->caps.features,width=1,height=1,operation=0,abits=8;
                const uint32_t *sa=fp+4,*ma=fp+12;const struct ps2_unorm_layout *precision=&layout;
                switch(invalid){
                case 0:target.input_bits=0;break;case 1:target.input_bits=9;break;
                case 2:target.map_bits=0;break;case 3:target.map_bits=9;break;
                case 4:target.map=h[1];break;case 5:target.map=fp[4];break;
                case 6:target.map=w.term;break;case 7:target.map=p;break;
                case 8:if(alpha)target.colour=h[3];else target.colour=fp[0];break;
                case 9:if(alpha)target.map=0;else target.rgb_bits[1]=9;break;
                case 10:width=65;break;case 11:height=0;break;
                case 12:gs->caps.features&=~PS2GS_HW_CHANNEL_MAP;break;
                case 13:operation=4;break;
                case 14:operation=5;break;
                case 15:operation=UINT_MAX;break;case 16:operation=255;break;
                case 17:sa=NULL;break;case 18:ma=NULL;break;
                case 19:if(alpha)abits=9;else precision=NULL;break;
                case 20:width=0;break;case 21:height=33;break;
                case 22:target.map=h[0];if(alpha)target.map=h[1];break;
                case 23:target.map=fp[12];break;
                case 24:target.map=f.tap;break;
                case 25:target.map=source_handle;break;
                case 26:target.colour=target.map;break;
                }
                unsigned before=attempts,draws=transfer_calls;
                int status=alpha?ps2_native_hsl_alpha_float_mapped(&w,&f,sa,ma,h[1],abits,&target,p,width,height,operation):
                    ps2_native_hsl_float_mapped(&w,&f,fp,sa,ma,ma,h[0],h[1],precision,&target,p,width,height,operation);
                assert(status<0 && errno==(invalid==12?ENOTSUP:EINVAL) && attempts==before && transfer_calls==draws);
                gs->caps.features=features;++guards;
            }
            assert(!ps2_divide_release(&w) && !ps2_filter_release(&f));
            for(unsigned i=0;i<16;++i)assert(!ps2_surface_destroy(gs,fp[i]));
            for(unsigned i=0;i<4;++i)assert(!ps2_surface_destroy(gs,h[i]));
            assert(!ps2_surface_destroy(gs,p));finish_rop();
        }else{
            fail_create=failure==1;transfer_failure=failure==2;
            assert(ps2_native_hsl_float_mapped(&w,&f,fp,fp+4,fp+12,fp+12,h[0],h[1],&layout,NULL,p,1,1,
                0)<0 && errno==(failure==1?ENOSPC:EIO) && gs->error==(failure==1?0:EIO));
            fail_create=transfer_failure=0;unsigned before=attempts,draws=transfer_calls;
            int retry=ps2_native_hsl_float_mapped(&w,&f,fp,fp+4,fp+12,fp+12,h[0],h[1],&layout,NULL,p,1,1,
                1);
            if(failure==1)assert(!retry && !gs->error && attempts>before);
            else assert(retry<0 && gs->error==EIO && attempts==before && transfer_calls==draws);
            polygon_finish();
            for(unsigned i=0;i<PS2GS_MAX_SURFACES+1;++i)assert(!models[i].data);
        }
    }
    assert(guards==54);printf("PS2 HSL direct preflight guards=%u allocation recovery=1 submission failure latching=1 PASS\n",guards);
}
static void softlight_guards_case(void)
{
    unsigned guards=0;
    for(unsigned failure=0;failure<3;++failure){
        rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
        struct ps2_divide_workspace w={0};struct ps2_filter_workspace f={0};uint32_t fp[16]={0},h[4]={0},p=0;
        assert(!ps2_divide_reserve(&w,gs) && !ps2_filter_reserve(&f,gs,3));
        for(unsigned i=0;i<16;++i)assert(!ps2_surface_create(gs,64,32,32,fp+i));
        for(unsigned i=0;i<4;++i)assert(!ps2_surface_create(gs,64,32,32,h+i));
        assert(!ps2_surface_create(gs,16,16,32,&p));
        struct ps2_unorm_layout layout={.rgb_bits={8,8,8},.alpha_bits=8};
        if(!failure){
            for(unsigned alpha=0;alpha<2;++alpha)for(unsigned invalid=0;invalid<27;++invalid){
                struct ps2_saturate_map_target target={.input_bits=4,.map=h[2],.map_bits=1,
                    .colour=alpha?0:h[3],.rgb_bits={5,6,5}};
                unsigned features=gs->caps.features,width=1,height=1,abits=8;
                const uint32_t *sa=fp+4,*ma=fp+12;const struct ps2_unorm_layout *precision=&layout;
                if(invalid>=13 && invalid<=16)continue;
                switch(invalid){
                case 0:target.input_bits=0;break;case 1:target.input_bits=9;break;
                case 2:target.map_bits=0;break;case 3:target.map_bits=9;break;
                case 4:target.map=h[1];break;case 5:target.map=fp[4];break;
                case 6:target.map=w.term;break;case 7:target.map=p;break;
                case 8:if(alpha)target.colour=h[3];else target.colour=fp[0];break;
                case 9:if(alpha)target.map=0;else target.rgb_bits[1]=9;break;
                case 10:width=65;break;case 11:height=0;break;
                case 12:gs->caps.features&=~PS2GS_HW_CHANNEL_MAP;break;
                case 17:sa=NULL;break;case 18:ma=NULL;break;
                case 19:if(alpha)abits=9;else precision=NULL;break;
                case 20:width=0;break;case 21:height=33;break;
                case 22:target.map=h[0];if(alpha)target.map=h[1];break;
                case 23:target.map=fp[12];break;
                case 24:target.map=f.tap;break;
                case 25:target.map=source_handle;break;
                case 26:target.colour=target.map;break;
                }
                unsigned before=attempts,draws=transfer_calls;
                int status=alpha?ps2_native_softlight_alpha_float_mapped(&w,&f,sa,ma,h[1],abits,&target,p,width,height):
                    ps2_native_softlight_float_mapped(&w,&f,fp,sa,fp+8,ma,h[0],h[1],precision,&target,p,width,height);
                assert(status<0 && errno==(invalid==12?ENOTSUP:EINVAL) && attempts==before && transfer_calls==draws);
                gs->caps.features=features;++guards;
            }
            assert(!ps2_divide_release(&w) && !ps2_filter_release(&f));
            for(unsigned i=0;i<16;++i)assert(!ps2_surface_destroy(gs,fp[i]));
            for(unsigned i=0;i<4;++i)assert(!ps2_surface_destroy(gs,h[i]));
            assert(!ps2_surface_destroy(gs,p));finish_rop();
        }else{
            fail_create=failure==1;transfer_failure=failure==2;
            assert(ps2_native_softlight_float_mapped(&w,&f,fp,fp+4,fp+8,fp+12,h[0],h[1],&layout,NULL,p,1,1)<0 && errno==(failure==1?ENOSPC:EIO) && gs->error==(failure==1?0:EIO));
            fail_create=transfer_failure=0;unsigned before=attempts,draws=transfer_calls;
            int retry=ps2_native_softlight_float_mapped(&w,&f,fp,fp+4,fp+8,fp+12,h[0],h[1],&layout,NULL,p,1,1);
            if(failure==1)assert(!retry && !gs->error && attempts>before);
            else assert(retry<0 && gs->error==EIO && attempts==before && transfer_calls==draws);
            polygon_finish();
            for(unsigned i=0;i<PS2GS_MAX_SURFACES+1;++i)assert(!models[i].data);
        }
    }
    assert(guards==46);printf("PS2 SoftLight direct preflight guards=%u allocation recovery=1 submission failure latching=1 PASS\n",guards);
}
static void float_branch_case(unsigned guards)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    struct ps2_divide_workspace w={0};struct ps2_filter_workspace f={0};uint32_t h[13]={0},p=0;
    assert(!ps2_divide_reserve(&w,gs) && !ps2_filter_reserve(&f,gs,3));
    for(unsigned i=0;i<13;++i)assert(!ps2_surface_create(gs,64,32,32,h+i));
    assert(!ps2_surface_create(gs,16,16,32,&p));
    if(guards){
        for(unsigned select=0;select<2;++select)for(unsigned bad=0;bad<12;++bad){
            const uint32_t *a=h,*b=h+4,*out=h+8;uint32_t dest=h[12],palette=p;
            unsigned width=1,height=1,relation=PS2_FLOAT_GE,bytes=f.bytes;
            switch(bad){case 0:a=NULL;break;case 1:b=NULL;break;
            case 2:if(select)out=NULL;else dest=0;break;
            case 3:width=0;break;case 4:width=65;break;case 5:height=0;break;case 6:height=33;break;
            case 7:palette=h[12];break;case 8:dest=w.term;break;
            case 9:f.bytes=2;break;case 10:if(select)dest=0;else relation=4;break;
            case 11:gs->caps.features&=~PS2GS_HW_CHANNEL_LUT;break;}
            unsigned before=attempts,draws=transfer_calls,features=gs->caps.features;
            int status=select?ps2_native_float_select(&w,&f,a,b,dest,out,palette,width,height):
                ps2_native_float_compare(&w,&f,a,b,dest,palette,width,height,relation);
            assert(status<0 && attempts==before && transfer_calls==draws);
            f.bytes=bytes;if(bad==11)gs->caps.features=features|PS2GS_HW_CHANNEL_LUT;
        }
        puts("PS2 float branch direct preflight guards=24 PASS");
    }else{
        uint32_t values[2][2048][3];unsigned comparisons=0,selections=0;
        for(unsigned kind=0;kind<2;++kind)for(unsigned i=0;i<2048;++i)for(unsigned c=0;c<3;++c){
            unsigned v=i*0x343fdU+c*0x12345U+kind*0x31795U;
            values[kind][i][c]=((1+(v%254))<<23)|(v&0x7fffff);
            if(i%7==0)values[kind][i][c]=0;
            if(i%7==1)values[kind][i][c]=0x3f800000U+kind;
            if(i%7==2)values[kind][i][c]=0x3f800000U-kind;
            if(i%7==3)values[kind][i][c]=1U<<23;
        }
        for(unsigned operation=0;operation<4;++operation)for(unsigned alias=0;alias<2;++alias){
            for(unsigned byte=0;byte<4;++byte)for(unsigned i=0;i<2048;++i){
                uint32_t a=0x91000000,b=0x92000000;
                for(unsigned c=0;c<3;++c){a|=((values[0][i][c]>>(8*byte))&255)<<(8*c);
                    b|=((values[1][i][c]>>(8*byte))&255)<<(8*c);}
                model(h[byte])->data[i]=a;model(h[4+byte])->data[i]=b;
            }
            uint32_t predicate=alias?h[operation]:h[12];
            compositor_active=1;assert(!ps2_native_float_compare(&w,&f,h,h+4,predicate,p,64,32,operation));compositor_active=0;
            for(unsigned i=0;i<2048;++i)for(unsigned c=0;c<3;++c){float a=number(values[0][i][c]),b=number(values[1][i][c]);
                unsigned wanted=operation==0?a<b:operation==1?a<=b:operation==2?a>b:a>=b;
                assert(((model(predicate)->data[i]>>(8*c))&255)==wanted);++comparisons;}
        }
        for(unsigned alias=0;alias<5;++alias){
            for(unsigned byte=0;byte<4;++byte)for(unsigned i=0;i<2048;++i){
                uint32_t a=0x91000000,b=0x92000000;
                for(unsigned c=0;c<3;++c){a|=((values[0][i][c]>>(8*byte))&255)<<(8*c);
                    b|=((values[1][i][c]>>(8*byte))&255)<<(8*c);}
                model(h[byte])->data[i]=a;model(h[4+byte])->data[i]=b;
            }
            for(unsigned i=0;i<2048;++i){uint32_t v=0x93000000;
                for(unsigned c=0;c<3;++c)v|=((i+c)%4==0?0:(i+c)%4==1?1:(i+c)%4==2?7:255)<<(8*c);
                model(h[12])->data[i]=v;}
            uint32_t out[4];for(unsigned byte=0;byte<4;++byte)out[byte]=alias==0?h[8+byte]:alias==1?h[byte]:alias==2?h[4+byte]:alias==3?h[3-byte]:byte?h[byte]:h[12];
            compositor_active=1;assert(!ps2_native_float_select(&w,&f,h,h+4,h[12],out,p,64,32));compositor_active=0;
            for(unsigned byte=0;byte<4;++byte)for(unsigned i=0;i<2048;++i)for(unsigned c=0;c<3;++c){
                unsigned wanted=(values[(i+c)%4==0][i][c]>>(8*byte))&255;
                assert(((model(out[byte])->data[i]>>(8*c))&255)==wanted);++selections;}
        }
        assert(comparisons==49152 && selections==122880);
        printf("PS2 float branch comparisons=%u selected_byte_lanes=%u PASS\n",comparisons,selections);
    }
    assert(!ps2_divide_release(&w) && !ps2_filter_release(&f));
    for(unsigned i=0;i<13;++i)assert(!ps2_surface_destroy(gs,h[i]));
    assert(!ps2_surface_destroy(gs,p));finish_rop();
}
static void dodge_burn_register_case(void)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    struct ps2_divide_workspace w={0};struct ps2_filter_workspace f={0};uint32_t h[18]={0},p=0;
    assert(!ps2_divide_reserve(&w,gs) && !ps2_filter_reserve(&f,gs,3));
    for(unsigned i=0;i<18;++i)assert(!ps2_surface_create(gs,64,32,32,h+i));
    assert(!ps2_surface_create(gs,16,16,32,&p));
    const unsigned edges[]={0,1,127,128,255,256,257,32767,32768,65280,65534,65535};
    const struct ps2_unorm_layout layout={.rgb_bits={8,8,8},.alpha_bits=8};
    unsigned checks=0,changed=0;
    for(unsigned op=0;op<2;++op)for(unsigned component=0;component<2;++component){
        float source[256][4],mask[256][4];uint32_t expected[256];
        for(unsigned i=0;i<256;++i){
            unsigned sa=edges[i%12],ma=edges[(i*7+3)%12];
            float reciprocal=1.0f/65535;
            source[i][3]=sa*reciprocal;mask[i][3]=ma*reciprocal;
            expected[i]=0;
            for(unsigned c=0;c<3;++c){unsigned s=(i+c)%5==0?sa:(i+c)%5==1?sa?sa-1:0:
                    (i+c)%5==2?65535:(i*179+c*8317)&65535;
                source[i][c]=s*reciprocal;mask[i][c]=edges[(i+c*3)%12]*reciprocal;
                expected[i]|=((i*73+c*127)&255)<<(8*c);
            }
            expected[i]|=(edges[(i+5)%12]>>8)<<24;
            model(h[16])->data[i]=0x91000000|(expected[i]&0xffffff);
            model(h[17])->data[i]=0x92000000|(expected[i]>>24)*0x010101;
            const float values[]={source[i][0],source[i][1],source[i][2],source[i][3],
                mask[i][0],mask[i][1],mask[i][2],mask[i][3]};
            for(unsigned group=0;group<4;++group)for(unsigned byte=0;byte<4;++byte){uint32_t rgb=0x93000000;
                for(unsigned c=0;c<3;++c){float v=values[group==0?c:group==1?3:group==2?4+c:7];uint32_t bits;
                    memcpy(&bits,&v,4);rgb|=((bits>>(8*byte))&255)<<(8*c);}
                model(h[group*4+byte])->data[i]=rgb;
            }
        }
        for(unsigned i=256;i<2048;++i){model(h[16])->data[i]=0x9155aacc;model(h[17])->data[i]=0x92775511;}
        pixman_image_t *si=pixman_image_create_bits(PIXMAN_rgba_float,64,4,(uint32_t*)source,64*16),
            *mi=pixman_image_create_bits(PIXMAN_rgba_float,64,4,(uint32_t*)mask,64*16),
            *di=pixman_image_create_bits(PIXMAN_a8b8g8r8,64,4,expected,256);assert(si && mi && di);
        pixman_image_set_component_alpha(mi,component);
        pixman_image_composite32(op?PIXMAN_OP_COLOR_BURN:PIXMAN_OP_COLOR_DODGE,si,mi,di,0,0,0,0,0,0,64,4);
        compositor_active=1;assert(!ps2_native_dodge_burn_float_mapped(&w,&f,h,h+4,component?h+8:h+12,h+12,
            h[16],h[17],&layout,NULL,p,64,4,op));compositor_active=0;
        for(unsigned i=0;i<256;++i){uint32_t rgb=model(h[16])->data[i]&0xffffff,a=model(h[17])->data[i]&0xffffff;
            if(rgb!=(expected[i]&0xffffff) || a!=(expected[i]>>24)*0x010101){
                fprintf(stderr,"dodge/burn registers op=%u ca=%u i=%u expected=%08x rgb=%06x alpha=%06x\n",op,component,i,expected[i],rgb,a);abort();}
            changed+=rgb!=(((i*73)&255)|(((i*73+127)&255)<<8)|(((i*73+254)&255)<<16));checks+=4;}
        for(unsigned i=256;i<2048;++i)assert(model(h[16])->data[i]==0x9155aacc && model(h[17])->data[i]==0x92775511);
        pixman_image_unref(si);pixman_image_unref(mi);pixman_image_unref(di);
    }
    assert(checks==4096 && changed);printf("PS2 dodge/burn register channel_checks=%u changed_pixels=%u PASS\n",checks,changed);
    assert(!ps2_divide_release(&w) && !ps2_filter_release(&f));
    for(unsigned i=0;i<18;++i)assert(!ps2_surface_destroy(gs,h[i]));
    assert(!ps2_surface_destroy(gs,p));finish_rop();
}
static void softlight_register_case(void)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    struct ps2_divide_workspace w={0};struct ps2_filter_workspace f={0};uint32_t h[18]={0},p=0;
    assert(!ps2_divide_reserve(&w,gs) && !ps2_filter_reserve(&f,gs,3));
    for(unsigned i=0;i<18;++i)assert(!ps2_surface_create(gs,64,32,32,h+i));
    assert(!ps2_surface_create(gs,16,16,32,&p));
    const unsigned edges[]={0,1,127,128,255,256,257,32767,32768,65280,65534,65535};
    const struct ps2_unorm_layout layout={.rgb_bits={8,8,8},.alpha_bits=8};
    unsigned checks=0,changed=0;
    for(unsigned op=0;op<1;++op)for(unsigned component=0;component<2;++component){
        float source[256][4],mask[256][4];uint32_t expected[256];
        for(unsigned i=0;i<256;++i){
            unsigned sa=edges[i%12],ma=edges[(i*7+3)%12];
            float reciprocal=1.0f/65535;
            source[i][3]=sa*reciprocal;mask[i][3]=ma*reciprocal;
            expected[i]=0;
            for(unsigned c=0;c<3;++c){unsigned s=(i+c)%5==0?sa:(i+c)%5==1?sa?sa-1:0:
                    (i+c)%5==2?65535:(i*179+c*8317)&65535;
                source[i][c]=s*reciprocal;mask[i][c]=edges[(i+c*3)%12]*reciprocal;
                expected[i]|=((i*73+c*127)&255)<<(8*c);
            }
            expected[i]|=(edges[(i+5)%12]>>8)<<24;
            model(h[16])->data[i]=0x91000000|(expected[i]&0xffffff);
            model(h[17])->data[i]=0x92000000|(expected[i]>>24)*0x010101;
            const float values[]={source[i][0],source[i][1],source[i][2],source[i][3],
                mask[i][0],mask[i][1],mask[i][2],mask[i][3]};
            for(unsigned group=0;group<4;++group)for(unsigned byte=0;byte<4;++byte){uint32_t rgb=0x93000000;
                for(unsigned c=0;c<3;++c){float v=values[group==0?c:group==1?3:group==2?4+c:7];uint32_t bits;
                    memcpy(&bits,&v,4);rgb|=((bits>>(8*byte))&255)<<(8*c);}
                model(h[group*4+byte])->data[i]=rgb;
            }
        }
        for(unsigned i=256;i<2048;++i){model(h[16])->data[i]=0x9155aacc;model(h[17])->data[i]=0x92775511;}
        pixman_image_t *si=pixman_image_create_bits(PIXMAN_rgba_float,64,4,(uint32_t*)source,64*16),
            *mi=pixman_image_create_bits(PIXMAN_rgba_float,64,4,(uint32_t*)mask,64*16),
            *di=pixman_image_create_bits(PIXMAN_a8b8g8r8,64,4,expected,256);assert(si && mi && di);
        pixman_image_set_component_alpha(mi,component);
        pixman_image_composite32(PIXMAN_OP_SOFT_LIGHT,si,mi,di,0,0,0,0,0,0,64,4);
        compositor_active=1;assert(!ps2_native_softlight_float_mapped(&w,&f,h,h+4,component?h+8:h+12,h+12,
            h[16],h[17],&layout,NULL,p,64,4));compositor_active=0;
        for(unsigned i=0;i<256;++i){uint32_t rgb=model(h[16])->data[i]&0xffffff,a=model(h[17])->data[i]&0xffffff;
            if(rgb!=(expected[i]&0xffffff) || a!=(expected[i]>>24)*0x010101){
                fprintf(stderr,"softlight registers op=%u ca=%u i=%u expected=%08x rgb=%06x alpha=%06x\n",op,component,i,expected[i],rgb,a);abort();}
            changed+=rgb!=(((i*73)&255)|(((i*73+127)&255)<<8)|(((i*73+254)&255)<<16));checks+=4;}
        for(unsigned i=256;i<2048;++i)assert(model(h[16])->data[i]==0x9155aacc && model(h[17])->data[i]==0x92775511);
        pixman_image_unref(si);pixman_image_unref(mi);pixman_image_unref(di);
    }
    assert(checks==2048 && changed);printf("PS2 SoftLight register channel_checks=%u changed_pixels=%u PASS\n",checks,changed);
    assert(!ps2_divide_release(&w) && !ps2_filter_release(&f));
    for(unsigned i=0;i<18;++i)assert(!ps2_surface_destroy(gs,h[i]));
    assert(!ps2_surface_destroy(gs,p));finish_rop();
}
static void hsl_register_case(unsigned boundary)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    struct ps2_divide_workspace w={0};struct ps2_filter_workspace f={0};uint32_t h[18]={0},p=0;
    assert(!ps2_divide_reserve(&w,gs) && !ps2_filter_reserve(&f,gs,3));
    for(unsigned i=0;i<18;++i)assert(!ps2_surface_create(gs,64,32,32,h+i));
    assert(!ps2_surface_create(gs,16,16,32,&p));
    const unsigned edges[]={0,1,127,128,255,256,257,32767,32768,65280,65534,65535};
    const struct ps2_unorm_layout layout={.rgb_bits={8,8,8},.alpha_bits=8};
    unsigned checks=0,changed=0;
    for(unsigned op=0;op<4;++op)for(unsigned component=0;component<1;++component){
        float source[256][4],mask[256][4];uint32_t expected[256];
        for(unsigned i=0;i<256;++i){
            unsigned sa=edges[i%12],ma=edges[(i*7+3)%12];
            float reciprocal=1.0f/65535;
            source[i][3]=sa*reciprocal;mask[i][3]=ma*reciprocal;
            expected[i]=0;
            for(unsigned c=0;c<3;++c){unsigned s=(i+c)%5==0?sa:(i+c)%5==1?sa?sa-1:0:
                    (i+c)%5==2?65535:(i*179+c*8317)&65535;
                source[i][c]=s*reciprocal;mask[i][c]=edges[(i+c*3)%12]*reciprocal;
                if(boundary==1){
                    static const unsigned permutations[6][3]={{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
                    unsigned v[3]={0,sa/2,sa};
                    if(i%11==0)v[0]=v[1]=v[2]=sa/2;
                    if(i%11==1)v[1]=v[2];
                    if(i%11==2)v[0]=v[1];
                    source[i][c]=v[permutations[i%6][c]]*reciprocal;
                }
                if(boundary==2){
                    source[i][3]=(i%3?1.0f:0x1p-40f);
                    mask[i][3]=(i%5?0x1p-40f:1.0f);
                    source[i][c]=(c+1)*0x1p-42f;
                    if(i%7==0)source[i][c]=1.0f-(c+1)*0x1p-12f;
                }
                expected[i]|=((i*73+c*127)&255)<<(8*c);
            }
            expected[i]|=(edges[(i+5)%12]>>8)<<24;
            model(h[16])->data[i]=0x91000000|(expected[i]&0xffffff);
            model(h[17])->data[i]=0x92000000|(expected[i]>>24)*0x010101;
            const float values[]={source[i][0],source[i][1],source[i][2],source[i][3],
                mask[i][0],mask[i][1],mask[i][2],mask[i][3]};
            for(unsigned group=0;group<4;++group)for(unsigned byte=0;byte<4;++byte){uint32_t rgb=0x93000000;
                for(unsigned c=0;c<3;++c){float v=values[group==0?c:group==1?3:group==2?4+c:7];uint32_t bits;
                    memcpy(&bits,&v,4);rgb|=((bits>>(8*byte))&255)<<(8*c);}
                model(h[group*4+byte])->data[i]=rgb;
            }
        }
        for(unsigned i=256;i<2048;++i){model(h[16])->data[i]=0x9155aacc;model(h[17])->data[i]=0x92775511;}
        pixman_image_t *si=pixman_image_create_bits(PIXMAN_rgba_float,64,4,(uint32_t*)source,64*16),
            *mi=pixman_image_create_bits(PIXMAN_rgba_float,64,4,(uint32_t*)mask,64*16),
            *di=pixman_image_create_bits(PIXMAN_a8b8g8r8,64,4,expected,256);assert(si && mi && di);
        pixman_image_set_component_alpha(mi,component);
        pixman_image_composite32((PIXMAN_OP_HSL_HUE+op),si,mi,di,0,0,0,0,0,0,64,4);
        compositor_active=1;assert(!ps2_native_hsl_float_mapped(&w,&f,h,h+4,component?h+8:h+12,h+12,
            h[16],h[17],&layout,NULL,p,64,4,op));compositor_active=0;
        for(unsigned i=0;i<256;++i){uint32_t rgb=model(h[16])->data[i]&0xffffff,a=model(h[17])->data[i]&0xffffff;
            if(rgb!=(expected[i]&0xffffff) || a!=(expected[i]>>24)*0x010101){
                fprintf(stderr,"HSL registers op=%u ca=%u i=%u expected=%08x rgb=%06x alpha=%06x\n",op,component,i,expected[i],rgb,a);abort();}
            changed+=rgb!=(((i*73)&255)|(((i*73+127)&255)<<8)|(((i*73+254)&255)<<16));checks+=4;}
        for(unsigned i=256;i<2048;++i)assert(model(h[16])->data[i]==0x9155aacc && model(h[17])->data[i]==0x92775511);
        pixman_image_unref(si);pixman_image_unref(mi);pixman_image_unref(di);
    }
    assert(checks==4096 && changed);printf("PS2 HSL register boundary=%u channel_checks=%u changed_pixels=%u PASS\n",boundary,checks,changed);
    assert(!ps2_divide_release(&w) && !ps2_filter_release(&f));
    for(unsigned i=0;i<18;++i)assert(!ps2_surface_destroy(gs,h[i]));
    assert(!ps2_surface_destroy(gs,p));finish_rop();
}
static void softlight_arithmetic_case(unsigned kind)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    struct ps2_divide_workspace w={0};struct ps2_filter_workspace f={0};uint32_t h[12]={0},p=0;
    assert(!ps2_divide_reserve(&w,gs) && !ps2_filter_reserve(&f,gs,3));
    for(unsigned i=0;i<12;++i)assert(!ps2_surface_create(gs,64,32,32,h+i));
    assert(!ps2_surface_create(gs,16,16,32,&p));
    unsigned checks=0,state=0x2f19a731;
    if(kind==2){
        for(unsigned signed_op=0;signed_op<2;++signed_op)for(unsigned bad=0;bad<14;++bad){
            uint32_t a[4],b[4],out[4],palette=p;memcpy(a,h,sizeof(a));memcpy(b,h+4,sizeof(b));memcpy(out,h+8,sizeof(out));
            const uint32_t *ap=a,*bp=b,*op=out;unsigned width=64,height=32,operation=PS2_FLOAT_ADD;
            unsigned features=gs->caps.features,bytes=f.bytes;uint32_t saved=w.remainder[0];
            switch(bad){case 0:ap=NULL;break;case 1:op=NULL;break;case 2:out[3]=out[0];break;
            case 3:a[0]=w.term;break;case 4:out[0]=f.tap;break;case 5:palette=h[8];break;
            case 6:width=0;break;case 7:width=65;break;case 8:height=0;break;case 9:height=33;break;
            case 10:gs->caps.features&=~PS2GS_HW_RGBA_TRANSFER;break;case 11:f.bytes=2;break;
            case 12:w.remainder[0]=w.remainder[1];break;case 13:if(signed_op)operation=4;else a[0]=p;break;}
            unsigned before=attempts,draws=transfer_calls;
            int status=signed_op?ps2_native_float_signed(&w,&f,ap,bp,op,palette,width,height,operation):
                ps2_native_float_sqrt(&w,&f,ap,op,palette,width,height);
            assert(status<0 && errno==(bad==10?ENOTSUP:EINVAL) && attempts==before && transfer_calls==draws);
            gs->caps.features=features;f.bytes=bytes;w.remainder[0]=saved;++checks;
        }
        puts("PS2 sqrt/signed direct preflight guards=28 PASS");
    }else{
        for(unsigned operation=0;operation<(kind?4U:1U);++operation)for(unsigned batch=0;batch<(kind?3U:8U);++batch){
            uint32_t values[2][2048][3],expected[2048][3],out[4];
            unsigned width=batch==7?13:64,height=batch==7?3:32;
            for(unsigned byte=0;byte<4;++byte)out[byte]=batch%3==0?h[8+byte]:batch%3==1?h[byte]:h[7-byte];
            for(unsigned i=0;i<2048;++i)for(unsigned c=0;c<3;++c){
                for(unsigned operand=0;operand<2;++operand){state=state*1664525U+1013904223U;
                    unsigned exponent=kind?80+state%64:1+(i*3+c+batch*71)%254;
                    uint32_t bits=(exponent<<23)|(state&0x7fffff);
                    if(!kind && i%8==0)bits=exponent<<23;
                    if(!kind && i%8==1)bits=(exponent<<23)|0x7fffff;
                    if(!kind && i%8==2)bits=(exponent<<23)|1;
                    if(i%19==0 && (operation!=PS2_FLOAT_DIVIDE || operand==0))bits=0;
                    if(kind && ((state>>23)&1))bits|=0x80000000;
                    values[operand][i][c]=bits;
                }
                if(kind && i%7==0)values[1][i][c]=values[0][i][c]^(i%2?0x80000000:0);
                if(kind && operation==PS2_FLOAT_DIVIDE && !(values[1][i][c]&0x7fffffff))values[1][i][c]=0xbf800000;
                volatile float a=number(values[0][i][c]),b=number(values[1][i][c]);
                float result=!kind?sqrtf(a):operation==PS2_FLOAT_ADD?a+b:operation==PS2_FLOAT_SUBTRACT?a-b:
                    operation==PS2_FLOAT_MULTIPLY?a*b:a/b;
                if(result==0)result=0;
                memcpy(&expected[i][c],&result,4);
                assert(isfinite(result) && (result==0 || isnormal(result)));
            }
            for(unsigned byte=0;byte<4;++byte)for(unsigned i=0;i<2048;++i){
                uint32_t a=0x91000000,b=0x91000000;
                for(unsigned c=0;c<3;++c){a|=((values[0][i][c]>>(8*byte))&255)<<(8*c);
                    b|=((values[1][i][c]>>(8*byte))&255)<<(8*c);}
                model(h[byte])->data[i]=a;model(h[4+byte])->data[i]=b;model(h[8+byte])->data[i]=0x91000000;
            }
            uint32_t before[4][2048];for(unsigned byte=0;byte<4;++byte)memcpy(before[byte],model(out[byte])->data,8192);
            compositor_active=1;
            assert(!(kind?ps2_native_float_signed(&w,&f,h,h+4,out,p,width,height,operation):
                ps2_native_float_sqrt(&w,&f,h,out,p,width,height)));compositor_active=0;
            for(unsigned i=0;i<2048;++i)for(unsigned c=0;c<3;++c){
                if(i%64>=width || i/64>=height){for(unsigned byte=0;byte<4;++byte)assert(model(out[byte])->data[i]==before[byte][i]);continue;}
                uint32_t actual=0;for(unsigned byte=0;byte<4;++byte)actual|=((model(out[byte])->data[i]>>(8*c))&255)<<(8*byte);
                if(actual!=expected[i][c]){fprintf(stderr,"sqrt/signed kind=%u op=%u batch=%u lane=%u/%u a=%08x b=%08x expected=%08x actual=%08x\n",
                    kind,operation,batch,i,c,values[0][i][c],values[1][i][c],expected[i][c],actual);abort();}
                ++checks;
            }
        }
        printf("PS2 %s arithmetic checked_lanes=%u PASS\n",kind?"signed":"sqrt",checks);
    }
    assert(!ps2_divide_release(&w) && !ps2_filter_release(&f));
    for(unsigned i=0;i<12;++i)assert(!ps2_surface_destroy(gs,h[i]));
    assert(!ps2_surface_destroy(gs,p));finish_rop();
}
static void tiny_arithmetic_case(void)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    struct ps2_divide_workspace w={0};struct ps2_filter_workspace f={0};uint32_t h[12]={0},p=0;
    assert(!ps2_divide_reserve(&w,gs) && !ps2_filter_reserve(&f,gs,3));
    for(unsigned i=0;i<12;++i)assert(!ps2_surface_create(gs,64,32,32,h+i));
    assert(!ps2_surface_create(gs,16,16,32,&p));unsigned state=0x5f39a17b,checks=0;
    for(unsigned division=0;division<2;++division)for(unsigned batch=0;batch<8;++batch){
        uint32_t values[2][2048][3],expected[2048][3],out[4];
        for(unsigned byte=0;byte<4;++byte)out[byte]=batch%3==0?h[8+byte]:batch%3==1?h[byte]:h[7-byte];
        for(unsigned i=0;i<2048;++i)for(unsigned c=0;c<3;++c){
            for(unsigned operand=0;operand<2;++operand){state=state*1664525U+1013904223U;
                unsigned exponent=division?(operand?47+state%16:state%41):1+state%120;
                values[operand][i][c]=(exponent<<23)|(state&0x7fffff)|(state&0x80000000);
            }
            if(!division && i%7<5){
                const uint32_t a[]={0x00800000,0x00800000,0x00800000,0x00800001,0x00ffffff};
                const uint32_t b[]={0x3f000000,0x34000000,0x33800000,0x33800000,0x3f000000};
                values[0][i][c]=a[i%7]|((c&1)<<31);values[1][i][c]=b[i%7];
            }
            if(i%23==0)values[0][i][c]=0x80000000;
            volatile float a=number(values[0][i][c]),b=number(values[1][i][c]);float result=division?a/b:a*b;
            if(result==0)result=0;
            memcpy(&expected[i][c],&result,4);
            assert(isfinite(result) && (!division || result==0 || isnormal(result)));
        }
        for(unsigned byte=0;byte<4;++byte)for(unsigned i=0;i<2048;++i){uint32_t a=0x91000000,b=0x91000000;
            for(unsigned c=0;c<3;++c){a|=((values[0][i][c]>>(8*byte))&255)<<(8*c);b|=((values[1][i][c]>>(8*byte))&255)<<(8*c);}
            model(h[byte])->data[i]=a;model(h[4+byte])->data[i]=b;model(h[8+byte])->data[i]=0x91000000;
        }
        compositor_active=1;assert(!ps2_native_float_signed_tiny(&w,&f,h,h+4,out,p,64,32,
            division?PS2_FLOAT_DIVIDE:PS2_FLOAT_MULTIPLY));compositor_active=0;
        for(unsigned i=0;i<2048;++i)for(unsigned c=0;c<3;++c){uint32_t actual=0;
            for(unsigned byte=0;byte<4;++byte)actual|=((model(out[byte])->data[i]>>(8*c))&255)<<(8*byte);
            if(actual!=expected[i][c]){fprintf(stderr,"tiny arithmetic div=%u batch=%u pixel=%u/%u a=%08x b=%08x expected=%08x actual=%08x\n",
                division,batch,i,c,values[0][i][c],values[1][i][c],expected[i][c],actual);abort();}++checks;
        }
    }
    assert(checks==98304);printf("PS2 tiny arithmetic checked_lanes=%u PASS\n",checks);
    assert(!ps2_divide_release(&w) && !ps2_filter_release(&f));
    for(unsigned i=0;i<12;++i)assert(!ps2_surface_destroy(gs,h[i]));
    assert(!ps2_surface_destroy(gs,p));finish_rop();
}
static void float_routing_case(unsigned kind)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    struct ps2_divide_workspace w={0};struct ps2_filter_workspace f={0};uint32_t h[13]={0},p=0;
    assert(!ps2_divide_reserve(&w,gs) && !ps2_filter_reserve(&f,gs,3));
    for(unsigned i=0;i<13;++i)assert(!ps2_surface_create(gs,64,32,32,h+i));
    assert(!ps2_surface_create(gs,16,16,32,&p));unsigned checks=0;
    if(kind==2){
        for(unsigned route=0;route<2;++route)for(unsigned bad=0;bad<14;++bad){
            uint32_t a[4],b[4],out[4],palette=p,predicate=h[12];memcpy(a,h,sizeof(a));memcpy(b,h+4,sizeof(b));memcpy(out,h+8,sizeof(out));
            unsigned channels[3]={0,1,2},width=64,height=32,relation=0,features=gs->caps.features,bytes=f.bytes;
            const uint32_t *ap=a,*bp=b,*result=out;const unsigned *ch=channels;
            switch(bad){case 0:ap=NULL;break;case 1:if(route)ch=NULL;else bp=NULL;break;
            case 2:if(route)result=NULL;else predicate=0;break;case 3:if(route)out[1]=out[0];else predicate=w.term;break;
            case 4:a[0]=f.tap;break;case 5:palette=h[8];break;case 6:width=0;break;case 7:width=65;break;
            case 8:height=0;break;case 9:height=33;break;case 10:gs->caps.features&=~PS2GS_HW_CHANNEL_MAP;break;
            case 11:f.bytes=2;break;case 12:if(route)channels[2]=3;else relation=4;break;case 13:a[3]=p;break;}
            unsigned before=attempts,draws=transfer_calls;
            int status=route?ps2_native_float_swizzle(&w,&f,ap,ch,result,palette,width,height):
                ps2_native_float_signed_compare(&w,&f,ap,bp,predicate,palette,width,height,relation);
            assert(status<0 && errno==(bad==10?ENOTSUP:EINVAL) && attempts==before && transfer_calls==draws);
            gs->caps.features=features;f.bytes=bytes;++checks;
        }
        puts("PS2 signed compare/routing preflight guards=28 PASS");
    }else{
        uint32_t values[2][2048][3];unsigned state=0x4f19aa37;
        for(unsigned operand=0;operand<2;++operand)for(unsigned i=0;i<2048;++i)for(unsigned c=0;c<3;++c){
            state=state*1664525U+1013904223U;uint32_t bits=((1+state%254)<<23)|(state&0x7fffff)|(state&0x80000000);
            if(i%7==0)bits=(operand^(i&1))<<31;
            if(i%7==1)bits=0x3f800000U+operand;
            if(i%7==2)bits=0xbf800000U+operand;
            if(i%7==3)bits=0x00800000U|(operand<<31);
            values[operand][i][c]=bits;
        }
        for(unsigned operation=0;operation<(kind?27U:4U);++operation)for(unsigned alias=0;alias<(kind?1U:2U);++alias){
            for(unsigned byte=0;byte<4;++byte)for(unsigned i=0;i<2048;++i){uint32_t a=0x91000000,b=0x91000000;
                for(unsigned c=0;c<3;++c){a|=((values[0][i][c]>>(8*byte))&255)<<(8*c);b|=((values[1][i][c]>>(8*byte))&255)<<(8*c);}
                model(h[byte])->data[i]=a;model(h[4+byte])->data[i]=b;
            }
            if(kind){
                unsigned channels[]={operation%3,operation/3%3,operation/9};uint32_t out[4];
                for(unsigned byte=0;byte<4;++byte)out[byte]=operation%3==0?h[8+byte]:operation%3==1?h[byte]:h[3-byte];
                compositor_active=1;assert(!ps2_native_float_swizzle(&w,&f,h,channels,out,p,64,32));compositor_active=0;
                for(unsigned byte=0;byte<4;++byte)for(unsigned i=0;i<2048;++i)for(unsigned c=0;c<3;++c){
                    unsigned expected=(values[0][i][channels[c]]>>(8*byte))&255;
                    assert(((model(out[byte])->data[i]>>(8*c))&255)==expected);++checks;
                }
            }else{
                uint32_t out=alias?h[operation]:h[12];
                compositor_active=1;assert(!ps2_native_float_signed_compare(&w,&f,h,h+4,out,p,64,32,operation));compositor_active=0;
                for(unsigned i=0;i<2048;++i)for(unsigned c=0;c<3;++c){float a=number(values[0][i][c]),b=number(values[1][i][c]);
                    unsigned expected=operation==0?a<b:operation==1?a<=b:operation==2?a>b:a>=b;
                    assert(((model(out)->data[i]>>(8*c))&255)==expected);++checks;
                }
            }
        }
        assert(checks==(kind?663552U:49152U));printf("PS2 %s checked_lanes=%u PASS\n",kind?"routing":"signed comparison",checks);
    }
    assert(!ps2_divide_release(&w) && !ps2_filter_release(&f));
    for(unsigned i=0;i<13;++i)assert(!ps2_surface_destroy(gs,h[i]));
    assert(!ps2_surface_destroy(gs,p));finish_rop();
}
static void pd_guards_case(void)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    assert(!ps2_surface_set_depth(gs,source_handle,32) && !ps2_surface_set_depth(gs,destination_handle(),32));
    xp.drawable.depth=32;
    struct ps2_exa_context_bridge bridge={0};struct ps2_texture_exa texture={0};struct ps2_blit_exa blit={0};
    assert(ps2_exa_context_bridge_prepare(&xs,&bridge,&exa,ctx));
    assert(ps2_texture_exa_prepare(&xs,&texture,&exa,ps2_pipe_context_render(ctx)));
    assert(ps2_blit_exa_prepare(&xs,&blit,&exa,ctx));
    struct pixels *sp=model(source_handle);
    PixmapRec source_pixmap={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=32,.bitsPerPixel=32,
        .width=sp->width,.height=sp->height},.devPrivates=(void*)&source_handle};
    PictureRec source={.pDrawable=&source_pixmap.drawable,.format=PICT_a8b8g8r8,.filter=PictFilterNearest},
        destination={.pDrawable=&xp.drawable,.format=PICT_a8b8g8r8,.filter=PictFilterNearest};
    const int accepted[]={PictOpClear,PictOpSrc,PictOpDst,PictOpOver,PictOpOverReverse,PictOpIn,PictOpInReverse,
        PictOpOut,PictOpOutReverse,PictOpAtop,PictOpAtopReverse,PictOpXor,PictOpAdd,PictOpSaturate,
        PictOpMultiply,PictOpScreen,PictOpOverlay,PictOpDarken,PictOpLighten,PictOpHardLight,PictOpDifference,PictOpExclusion,
        PictOpDisjointClear,PictOpDisjointSrc,PictOpDisjointDst,PictOpConjointClear,PictOpConjointSrc,PictOpConjointDst,
        PictOpDisjointOverReverse,PictOpDisjointOver,PictOpDisjointIn,PictOpDisjointInReverse,PictOpDisjointOut,PictOpDisjointOutReverse,PictOpDisjointAtop,PictOpDisjointAtopReverse,PictOpDisjointXor,PictOpConjointOver,PictOpConjointOverReverse,PictOpConjointIn,PictOpConjointInReverse,PictOpConjointOut,PictOpConjointOutReverse,PictOpConjointAtop,PictOpConjointAtopReverse,PictOpConjointXor,PictOpColorDodge,PictOpColorBurn,PictOpSoftLight,PictOpHSLHue,PictOpHSLSaturation,PictOpHSLColor,PictOpHSLLuminosity};
    const int added[]={PictOpDisjointClear,PictOpDisjointSrc,PictOpDisjointDst,PictOpConjointClear,
        PictOpConjointSrc,PictOpConjointDst,PictOpDisjointOverReverse};
    unsigned guards=0,admitted=0;
    for(unsigned alpha=0;alpha<2;++alpha){destination.format=alpha?PICT_a8b8g8r8:PICT_x8b8g8r8;
        for(int op=0;op<256;++op){unsigned wanted=0,before=attempts;
            for(unsigned i=0;i<sizeof(accepted)/sizeof(*accepted);++i)wanted|=op==accepted[i];
            assert(!!exa.driver.CheckComposite(op,&source,NULL,&destination)==wanted && attempts==before);
            guards+=!wanted;admitted+=wanted;
        }
        const int extreme[]={-1,INT_MIN,INT_MAX};
        for(unsigned i=0;i<3;++i){unsigned before=attempts;
            assert(!exa.driver.CheckComposite(extreme[i],&source,NULL,&destination) && attempts==before);++guards;}
    }
    destination.format=PICT_a8b8g8r8;
    for(unsigned i=0;i<7;++i){unsigned before=attempts;
        source.filter=PictFilterConvolution;
        assert(!exa.driver.CheckComposite(added[i],&source,NULL,&destination) && attempts==before);++guards;
        source.filter=PictFilterNearest;destination.format=PICT_a2r10g10b10;
        /* Canonical aliases and the new float disjoint OverReverse are admitted. */
        assert(!!exa.driver.CheckComposite(added[i],&source,NULL,&destination)==1 && attempts==before);;
        destination.format=PICT_a8b8g8r8;
        struct ps2_picture_sample bad=identity(PS2_REPEAT_NORMAL);bad.transformed=2;
        struct ps2_pipe_copy_box box={0,0,0,0,1,1};
        assert(ps2_pipe_composite_rect(ctx,source_handle,destination_handle(),&box,&bad,NULL,
            PS2_COMPOSITE_DISJOINT_CLEAR+i)==-1 && attempts==before);++guards;
    }
    const unsigned invalid[]={53,54,55,56,63,255,256,UINT_MAX};
    for(unsigned i=0;i<8;++i){unsigned before=attempts;
        struct ps2_picture_sample sample=identity(PS2_REPEAT_NORMAL);
        struct ps2_pipe_copy_box box={0,0,0,0,1,1};
        assert(ps2_pipe_composite_rect(ctx,source_handle,destination_handle(),&box,&sample,NULL,invalid[i])==-1 && attempts==before);++guards;
    }
    assert(guards==434 && admitted==106);
    printf("PS2 PD opcode admission=%u preflight guards=%u PASS\n",admitted,guards);
    unsigned noops=0;
    PictureRec mask=source;mask.componentAlpha=TRUE;
    for(unsigned opaque=0;opaque<2;++opaque){destination.format=opaque?PICT_x8b8g8r8:PICT_a8b8g8r8;
        for(unsigned op=PictOpHSLHue;op<=PictOpHSLLuminosity;++op){unsigned before=attempts,draws=transfer_calls;
            assert(exa.driver.CheckComposite(op,&source,&mask,&destination));
            assert(exa.driver.PrepareComposite(op,&source,&mask,&destination,&source_pixmap,&source_pixmap,&xp));
            assert(blit.over==PS2_COMPOSITE_DST);
            exa.driver.Composite(&xp,0,0,0,0,0,0,1,1);exa.driver.DoneComposite(&xp);
            assert(attempts==before && transfer_calls==draws);++noops;
        }
    }
    for(unsigned op=PS2_COMPOSITE_HSL_HUE;op<=PS2_COMPOSITE_HSL_LUMINOSITY;++op){
        struct ps2_picture_sample sample=identity(PS2_REPEAT_NORMAL);
        struct ps2_picture_bitmap m={.component_alpha=1,.has_surface=1,.surface=source_handle,
            .width=sp->width,.height=sp->height,.sample={.source_argb=1}};
        struct ps2_pipe_copy_box box={0,0,0,0,1,1};unsigned before=attempts,draws=transfer_calls;
        assert(!ps2_pipe_composite_rect(ctx,source_handle,destination_handle(),&box,&sample,&m,op));
        assert(attempts==before && transfer_calls==draws);++noops;
    }
    assert(noops==12);puts("PS2 HSL component-alpha Dst noops=12 PASS");
    ps2_blit_exa_cleanup(&xs,&blit);ps2_exa_context_bridge_cleanup(&bridge);finish_rop();
}

static void gradient_linear_case(unsigned kind,unsigned variant)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    struct ps2_divide_workspace w={0};struct ps2_filter_workspace f={0};
    assert(!ps2_divide_reserve(&w,gs) && !ps2_filter_reserve(&f,gs,3));
    uint32_t h[10]={0},p=0;for(unsigned i=0;i<10;++i)assert(!ps2_surface_create(gs,64,32,32,h+i));
    assert(!ps2_surface_create(gs,16,16,32,&p));
    const struct ps2_gradient_stop stops[]={ {8192,{65535,11413,8317,32767}},
        {32768,{9113,65535,17831,65535}},{32768,{61731,2931,63813,0}},{57344,{8171,19131,65535,41311}} };
    struct ps2_gradient g={.kind=kind,.nstops=4,.stops=stops,.points={-65536,32768,3*65536,2*65536},
        .radii={32768,3*65536},.angle=-37*65536};
    if(variant==1){g.points[2]=g.points[0];g.points[3]=g.points[1];
        if(kind==2){g.points[2]+=65536;g.radii[1]=g.radii[0]+65536;}}
    if(variant==2){g.points[2]=g.points[0]+65536;g.points[3]=g.points[1]+65536;g.radii[1]=5*65536;
        g.points[0]=g.points[1]=32768;g.angle=397*65536;}
    if(variant==3){g.points[2]=g.points[0];g.points[3]=g.points[1];g.radii[1]=g.radii[0];}
    const int32_t matrices[5][3][3]={{{65536,0,0},{0,65536,0},{0,0,65536}},
        {{49152,21845,173},{-8192,98304,-21845},{0,0,65536}},
        {{65536,8192,-21845},{4096,49152,21845},{4096,-2048,65536}},
        {{65536,8192,-21845},{4096,49152,21845},{65536,0,98304}},
        {{65536,8192,-21845},{4096,49152,21845},{65536,0,32768}}};
    unsigned checks=0;
    for(unsigned transform=0;transform<5;++transform)for(unsigned repeat=0;repeat<4;++repeat)for(unsigned wide=0;wide<2;++wide){
        pixman_gradient_stop_t refstops[4];for(unsigned i=0;i<4;++i)refstops[i]=(pixman_gradient_stop_t){.x=stops[i].x,
            .color={stops[i].rgba[0],stops[i].rgba[1],stops[i].rgba[2],stops[i].rgba[3]}};
        pixman_point_fixed_t p1={g.points[0],g.points[1]},p2={g.points[2],g.points[3]};
        pixman_image_t *si=kind==1?pixman_image_create_linear_gradient(&p1,&p2,refstops,4):
            kind==2?pixman_image_create_radial_gradient(&p1,&p2,g.radii[0],g.radii[1],refstops,4):
            pixman_image_create_conical_gradient(&p1,g.angle,refstops,4);assert(si);
        pixman_image_set_repeat(si,repeat);if(transform)assert(pixman_image_set_transform(si,(const pixman_transform_t*)matrices[transform]));
        uint32_t bytes[12]={0};float values[12][4]={{0}};
        pixman_image_t *di=pixman_image_create_bits(wide?PIXMAN_rgba_float:PIXMAN_a8b8g8r8,4,3,
            wide?(uint32_t*)values:bytes,wide?64:16);assert(di);
        pixman_image_composite32(PIXMAN_OP_SRC,si,NULL,di,-2,-1,0,0,0,0,4,3);
        for(unsigned row=0;row<3;++row){int64_t positions[4];
            assert(!ps2_gradient_plan_row(&g,matrices[transform],!!transform,repeat,-2,-1,4,3,row,positions));
            for(unsigned x=0;x<4;++x){unsigned index=row*4+x;
                compositor_active=1;assert(!ps2_gradient_fetch(&w,&f,&g,repeat,positions[x],h,h+4,h[8],h[9],p,wide));compositor_active=0;
                if(wide){for(unsigned c=0;c<4;++c){uint32_t actual=0,wanted;memcpy(&wanted,&values[index][c],4);
                        for(unsigned b=0;b<4;++b)actual|=((model(h[(c==3?4:0)+b])->data[0]>>(8*(c==3?0:c)))&255)<<(8*b);
                        if(actual!=wanted){fprintf(stderr,"gradient float transform=%u repeat=%u i=%u channel=%u t=%lld expected=%08x actual=%08x\n",transform,repeat,index,c,(long long)positions[x],wanted,actual);abort();}++checks;
                    }}else{uint32_t actual=(model(h[8])->data[0]&0xffffff)|((model(h[9])->data[0]&255)<<24);
                    if(actual!=bytes[index]){fprintf(stderr,"gradient byte transform=%u repeat=%u i=%u t=%lld expected=%08x actual=%08x\n",transform,repeat,index,(long long)positions[x],bytes[index],actual);abort();}++checks;
                }
            }
        }
        pixman_image_unref(si);pixman_image_unref(di);
    }
    assert(checks==1200);printf("PS2 gradient kind=%u variant=%u independent byte/float checks=%u PASS\n",kind,variant,checks);
    for(unsigned i=0;i<10;++i)assert(!ps2_surface_destroy(gs,h[i]));
    assert(!ps2_surface_destroy(gs,p));
    assert(!ps2_filter_release(&f) && !ps2_divide_release(&w));finish_rop();
}
static void gradient_byte_case(void)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    struct ps2_divide_workspace w={0};struct ps2_filter_workspace f={0};
    assert(!ps2_divide_reserve(&w,gs) && !ps2_filter_reserve(&f,gs,3));uint32_t h[5],p;
    for(unsigned i=0;i<5;++i)assert(!ps2_surface_create(gs,64,32,32,h+i));
    assert(!ps2_surface_create(gs,16,16,32,&p));
    const float edge[]={-1.e20f,-4294967296.0f,-255.5f,-1.5f,-.5f,-0.0f,0,.5f,1,1.5f,127.5f,255.5f,
        256,257,511.9f,65535,16777215,2147483648.0f,4294967040.0f,4294967296.0f,1.e20f};
    unsigned checks=0;
    for(unsigned index=0;index<2048;++index)for(unsigned c=0;c<3;++c){
        float v=index<21?edge[(index+c*7)%21]:((int)(index*8317+c*179)%98304-32768)*.125f;uint32_t bits;memcpy(&bits,&v,4);
        for(unsigned b=0;b<4;++b){model(h[b])->data[index]&=~(255U<<(8*c));model(h[b])->data[index]|=((bits>>(8*b))&255)<<(8*c);}
    }
    compositor_active=1;assert(!ps2_native_float_byte(&w,&f,h,h[4],p,64,32));compositor_active=0;
    for(unsigned index=0;index<2048;++index)for(unsigned c=0;c<3;++c){float v;uint32_t bits=0;
        for(unsigned b=0;b<4;++b)bits|=((model(h[b])->data[index]>>(8*c))&255)<<(8*b);
        memcpy(&v,&bits,4);
        uint32_t expected=v<0?0:v>=4294967296.0f?UINT32_MAX:(uint32_t)v,actual=(model(h[4])->data[index]>>(8*c))&255;
        assert(actual==(expected&255));++checks;
    }
    assert(checks==6144);printf("PS2 gradient R5900 byte conversion lanes=%u PASS\n",checks);
    for(unsigned i=0;i<5;++i)assert(!ps2_surface_destroy(gs,h[i]));
    assert(!ps2_surface_destroy(gs,p));
    assert(!ps2_filter_release(&f) && !ps2_divide_release(&w));finish_rop();
}
static void gradient_dispatch_case(unsigned wide,unsigned kind,unsigned opacity)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    assert(!ps2_surface_set_depth(gs,source_handle,32) && !ps2_surface_set_depth(gs,destination_handle(),32));xp.drawable.depth=32;
    struct ps2_exa_context_bridge bridge={0};struct ps2_texture_exa texture={0};struct ps2_blit_exa blit={0};
    assert(ps2_exa_context_bridge_prepare(&xs,&bridge,&exa,ctx));
    assert(ps2_texture_exa_prepare(&xs,&texture,&exa,ps2_pipe_context_render(ctx)));
    assert(ps2_blit_exa_prepare(&xs,&blit,&exa,ctx));
    struct pixels *sp=model(source_handle),*dp=model(destination_handle());
    PixmapRec source_pixmap={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=32,.bitsPerPixel=32,
        .width=sp->width,.height=sp->height},.devPrivates=(void*)&source_handle};
    PictureRec image={.pDrawable=&source_pixmap.drawable,.format=PICT_a8b8g8r8,.filter=PictFilterNearest,.repeat=TRUE,.repeatType=RepeatReflect},
        dest={.pDrawable=&xp.drawable,.format=PICT_a8b8g8r8,.filter=PictFilterNearest};
    PictGradientStop stops[]={{0,{65535,11413,8317,32767}},{32768,{9113,65535,17831,65535}},{65536,{8171,19131,65535,41311}}};
    SourcePict source={.linear={.type=SourcePictTypeLinear,.nstops=3,.stops=stops,.p1={0,0},.p2={8*65536,3*65536}}};
    if(kind==2)source=(SourcePict){.radial={.type=SourcePictTypeRadial,.nstops=3,.stops=stops,
        .c1={0,0,32768},.c2={8*65536,3*65536,9*65536}}};
    if(kind==3)source=(SourcePict){.conical={.type=SourcePictTypeConical,.nstops=3,.stops=stops,
        .center={0,0},.angle=-37*65536}};
    PictureRec gradient={.pSourcePict=&source,.filter=PictFilterBilinear,.repeat=TRUE,.repeatType=RepeatReflect};
    PictTransform transform={.matrix={{49152,8192,173},{0,98304,-21845},{0,0,65536}}};gradient.transform=&transform;
    int32_t coefficients[]={2*65536,65536,2*65536,-65536};
    if(opacity){for(unsigned i=0;i<3;++i)stops[i].color.alpha=65535;image.format=PICT_x8b8g8r8;}
    if(opacity==2){gradient.filter=PictFilterConvolution;gradient.filter_params=coefficients;gradient.filter_nparams=4;}
    const unsigned narrow_ops[]={PictOpSrc,PictOpOver,PictOpScreen,PictOpMultiply,PictOpOverlay},
        wide_ops[]={PictOpSaturate,PictOpDisjointOver,PictOpSoftLight,PictOpHSLHue};
    const unsigned *ops=wide?wide_ops:narrow_ops;unsigned checks=0;
    for(unsigned op=0;op<(opacity?1U:wide?4U:5U);++op)for(unsigned role=0;role<3;++role)for(unsigned ca=0;ca<2;++ca){
        rgba_seed(sp);rgba_seed(dp);
        PictureRec src=role==1?image:gradient,mask=role==0?image:gradient;mask.componentAlpha=ca;
        PicturePtr mp=role==0 && !ca?NULL:&mask;
        void *source_bytes=NULL;pixman_image_t *si;
        pixman_gradient_stop_t rs[3];for(unsigned i=0;i<3;++i)rs[i]=(pixman_gradient_stop_t){.x=stops[i].x,.color={stops[i].color.red,stops[i].color.green,stops[i].color.blue,stops[i].color.alpha}};
        pixman_point_fixed_t p1={0,0},p2={8*65536,3*65536};
        if(role==1){si=reference_image(sp,32,0,!opacity,&source_bytes);pixman_image_set_repeat(si,PIXMAN_REPEAT_REFLECT);}
        else{si=kind==1?pixman_image_create_linear_gradient(&p1,&p2,rs,3):kind==2?pixman_image_create_radial_gradient(&p1,&p2,32768,9*65536,rs,3):pixman_image_create_conical_gradient(&p1,-37*65536,rs,3);assert(si);pixman_image_set_repeat(si,PIXMAN_REPEAT_REFLECT);
            assert(pixman_image_set_transform(si,(pixman_transform_t*)&transform));}
        void *mask_bytes=NULL;pixman_image_t *mi=NULL;
        if(mp){if(role==0){mi=reference_image(sp,32,0,!opacity,&mask_bytes);pixman_image_set_repeat(mi,PIXMAN_REPEAT_REFLECT);}
            else{mi=kind==1?pixman_image_create_linear_gradient(&p1,&p2,rs,3):kind==2?pixman_image_create_radial_gradient(&p1,&p2,32768,9*65536,rs,3):pixman_image_create_conical_gradient(&p1,-37*65536,rs,3);assert(mi);pixman_image_set_repeat(mi,PIXMAN_REPEAT_REFLECT);
                assert(pixman_image_set_transform(mi,(pixman_transform_t*)&transform));}pixman_image_set_component_alpha(mi,ca);}
        if(opacity==2){if(role!=1)assert(pixman_image_set_filter(si,PIXMAN_FILTER_CONVOLUTION,coefficients,4));
            if(mi && role!=0)assert(pixman_image_set_filter(mi,PIXMAN_FILTER_CONVOLUTION,coefficients,4));}
        uint32_t *expected=malloc(dp->width*dp->height*4);assert(expected);memcpy(expected,dp->data,dp->width*dp->height*4);
        pixman_image_t *di=pixman_image_create_bits(PIXMAN_a8b8g8r8,dp->width,dp->height,expected,dp->width*4);assert(di);
        pixman_image_composite32(ops[op],si,mi,di,2,1,1,2,3,2,1,1);
        compositor_active=1;
        assert(exa.driver.CheckComposite(ops[op],&src,mp,&dest));
        assert(exa.driver.PrepareComposite(ops[op],&src,mp,&dest,role==1?&source_pixmap:NULL,mp && role==0?&source_pixmap:NULL,&xp));
        exa.driver.Composite(&xp,2,1,1,2,3,2,1,1);compositor_active=0;exa.driver.DoneComposite(&xp);
        for(unsigned i=0;i<dp->width*dp->height;++i)if(dp->data[i]!=expected[i]){
            fprintf(stderr,"gradient dispatch wide=%u op=%u role=%u ca=%u i=%u expected=%08x actual=%08x\n",wide,ops[op],role,ca,i,expected[i],dp->data[i]);abort();}
        ++checks;pixman_image_unref(si);if(mi)pixman_image_unref(mi);pixman_image_unref(di);free(expected);free(source_bytes);free(mask_bytes);
    }
    printf("PS2 gradient EXA dispatch kind=%u wide=%u opacity=%u requests=%u PASS\n",kind,wide,opacity,checks);
    ps2_blit_exa_cleanup(&xs,&blit);ps2_exa_context_bridge_cleanup(&bridge);finish_rop();
}


static void gradient_packed_case(unsigned kind)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    assert(!ps2_surface_set_depth(gs,source_handle,32));rgba_seed(model(source_handle));
    struct ps2_exa_context_bridge bridge={0};struct ps2_texture_exa texture={0};struct ps2_blit_exa blit={0};
    assert(ps2_exa_context_bridge_prepare(&xs,&bridge,&exa,ctx));
    assert(ps2_texture_exa_prepare(&xs,&texture,&exa,ps2_pipe_context_render(ctx)));
    assert(ps2_blit_exa_prepare(&xs,&blit,&exa,ctx));
    PictGradientStop stops[]={{8192,{65535,11413,8317,32767}},{32768,{9113,65535,17831,65535}},
        {32768,{61731,2931,63813,0}},{57344,{8171,19131,65535,41311}}};
    SourcePict source={.linear={.type=SourcePictTypeLinear,.nstops=4,.stops=stops,.p1={-65536,32768},.p2={3*65536,2*65536}}};
    if(kind==2)source=(SourcePict){.radial={.type=SourcePictTypeRadial,.nstops=4,.stops=stops,
        .c1={-65536,32768,32768},.c2={3*65536,2*65536,3*65536}}};
    if(kind==3)source=(SourcePict){.conical={.type=SourcePictTypeConical,.nstops=4,.stops=stops,.center={-65536,32768},.angle=-37*65536}};
    PictTransform transform={.matrix={{49152,21845,173},{0,98304,-21845},{65536/19,0,65536}}};
    PictureRec gradient={.pSourcePict=&source,.filter=PictFilterNearest,.repeat=TRUE,.repeatType=RepeatReflect,.transform=&transform};
    pixman_gradient_stop_t rs[4];for(unsigned i=0;i<4;++i)rs[i]=(pixman_gradient_stop_t){.x=stops[i].x,
        .color={stops[i].color.red,stops[i].color.green,stops[i].color.blue,stops[i].color.alpha}};
    pixman_point_fixed_t first={-65536,32768},last={3*65536,2*65536};
    pixman_image_t *si=kind==1?pixman_image_create_linear_gradient(&first,&last,rs,4):
        kind==2?pixman_image_create_radial_gradient(&first,&last,32768,3*65536,rs,4):
        pixman_image_create_conical_gradient(&first,-37*65536,rs,4);assert(si);
    assert(pixman_image_set_transform(si,(pixman_transform_t*)&transform));pixman_image_set_repeat(si,PIXMAN_REPEAT_REFLECT);
    const PictFormatShort codes[]={PICT_x4a4,PICT_a2r2g2b2,PICT_a4r4g4b4};unsigned checks=0;
    for(unsigned layout=0;layout<3;++layout){unsigned depth=layout==2?16:layout==0?4:8;
        PixmapPtr dp=coverage_test_pixmap(&xs,19,11,depth,0);struct ps2_alpha_source raw;
        assert(ps2_surface_exa_alpha_source(dp,&raw)==1);
        PictureRec dest={.pDrawable=&dp->drawable,.format=codes[layout],.filter=PictFilterNearest};
        for(unsigned i=0;i<raw.bytes;++i)((unsigned char*)raw.bits)[i]=0;
        for(unsigned y=0;y<11;++y)for(unsigned x=0;x<19;++x){unsigned n=y*19+x,
            value=((n*8191U+0x317915)^(n*0x10103U))&((1U<<depth)-1);
            memcpy((unsigned char*)raw.bits+y*raw.stride+x*(raw.bits_per_pixel/8),&value,raw.bits_per_pixel/8);}
        unsigned char *expected=malloc(raw.bytes);assert(expected);memcpy(expected,raw.bits,raw.bytes);
        pixman_image_t *di=pixman_image_create_bits((pixman_format_code_t)codes[layout],19,11,(uint32_t*)expected,raw.stride);assert(di);
        pixman_image_composite32(PictOpSaturate,si,NULL,di,-1,1,0,0,3,2,1,1);
        compositor_active=1;assert(exa.driver.CheckComposite(PictOpSaturate,&gradient,NULL,&dest));
        assert(exa.driver.PrepareComposite(PictOpSaturate,&gradient,NULL,&dest,NULL,NULL,dp));
        exa.driver.Composite(dp,-1,1,0,0,3,2,1,1);compositor_active=0;exa.driver.DoneComposite(dp);
        for(unsigned y=0;y<11;++y)for(unsigned x=0;x<19;++x){unsigned a=coverage_byte(&raw,x,y),b=raw_value(expected+y*raw.stride,raw.bits_per_pixel,x);
            if(a!=b){fprintf(stderr,"gradient packed kind=%u layout=%u x=%u y=%u expected=%x actual=%x\n",kind,layout,x,y,b,a);abort();}}
        ++checks;pixman_image_unref(di);free(expected);coverage_test_destroy(dp);
    }
    printf("PS2 gradient packed kind=%u requests=%u PASS\n",kind,checks);
    pixman_image_unref(si);ps2_blit_exa_cleanup(&xs,&blit);ps2_exa_context_bridge_cleanup(&bridge);finish_rop();
}

static void gradient_guards_case(void)
{
    unsigned checks=0;
    for(unsigned failure=0;failure<3;++failure){
        rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
        struct ps2_divide_workspace w={0};struct ps2_filter_workspace f={0};
        assert(!ps2_divide_reserve(&w,gs) && !ps2_filter_reserve(&f,gs,3));uint32_t h[10],p;
        for(unsigned i=0;i<10;++i)assert(!ps2_surface_create(gs,64,32,32,h+i));
        assert(!ps2_surface_create(gs,16,16,32,&p));
        struct ps2_gradient_stop stops[2]={{0,{65535,8317,17831,32767}},{65536,{17831,65535,31831,41311}}};
        struct ps2_gradient good={.kind=1,.nstops=2,.stops=stops,.points={0,0,65536,0}};
        if(!failure){
            for(unsigned invalid=0;invalid<37;++invalid){
                struct ps2_gradient g=good;struct ps2_gradient_stop badstops[2];memcpy(badstops,stops,sizeof(stops));g.stops=badstops;
                struct ps2_divide_workspace ww=w,*wp=&ww;struct ps2_filter_workspace ff=f,*fp=&ff;
                uint32_t rgb[4],alpha[4],rb=h[8],ab=h[9],pal=p;memcpy(rgb,h,16);memcpy(alpha,h+4,16);
                const uint32_t *rp=rgb,*ap=alpha;unsigned repeat=0,wide=0,features=gs->caps.features;int64_t position=0;
                switch(invalid){
                case 0:g.kind=0;break;case 1:g.kind=4;break;case 2:g.nstops=0;break;case 3:g.stops=NULL;break;
                case 4:g.nstops=4097;break;case 5:badstops[0].x=-1;break;case 6:badstops[1].x=65537;break;
                case 7:badstops[0].x=65536;badstops[1].x=0;break;case 8:g.exclude_opaque=2;break;
                case 9:g.kind=2;g.radii[0]=-1;break;case 10:g.points[0]=INT32_MIN;g.points[2]=INT32_MAX;break;
                case 11:wp=NULL;break;case 12:fp=NULL;break;case 13:repeat=4;break;case 14:wide=2;break;
                case 15:rp=NULL;break;case 16:ap=NULL;break;case 17:rgb[0]=UINT_MAX;break;case 18:rgb[0]=rgb[1];break;
                case 19:rgb[0]=w.borrow;break;case 20:rb=0;break;case 21:ab=rb;break;case 22:pal=0;break;case 23:pal=h[0];break;
                case 24:ff.bytes=2;break;case 25:ff.gs=NULL;break;case 26:ww.gs=NULL;break;case 27:ww.borrow=0;break;
                case 28:ff.tap=ff.term;break;case 29:wide=1;alpha[0]=rgb[0];break;
                case 35:position=INT64_MAX;break;case 36:position=INT64_MIN+1;break;
                default:{const unsigned required[]={PS2GS_HW_SOLID_LOGIC,PS2GS_HW_CHANNEL_LUT,PS2GS_HW_CHANNEL_MAP,
                    PS2GS_HW_TEXTURE_TRANSFER,PS2GS_HW_RGBA_TRANSFER};gs->caps.features&=~required[invalid-30];break;}
                }
                unsigned before=attempts,draws=transfer_calls;
                assert(ps2_gradient_fetch(wp,fp,&g,repeat,position,rp,ap,rb,ab,pal,wide)<0);
                assert(errno==(invalid>=30 && invalid<35?ENOTSUP:EINVAL) && attempts==before && transfer_calls==draws);++checks;gs->caps.features=features;
            }
            for(unsigned invalid=0;invalid<14;++invalid){struct ps2_gradient g=good;
                const int32_t identity[3][3]={{65536,0,0},{0,65536,0},{0,0,65536}};
                int32_t badmatrix[3][3]={{INT32_MAX,INT32_MAX,INT32_MAX},{0,65536,0},{0,0,65536}};
                const int32_t (*matrix)[3]=identity;unsigned transformed=0,repeat=0,width=4,height=3,row=0;int x=0,y=0;
                int64_t positions[4],*out=positions;
                switch(invalid){case 0:g.kind=4;break;case 1:width=0;break;case 2:height=0;break;case 3:row=3;break;
                case 4:transformed=2;break;case 5:transformed=1;matrix=NULL;break;case 6:repeat=4;break;
                case 7:x=-32769;break;case 8:x=32767;break;case 9:y=-32769;break;case 10:y=32767;break;
                case 11:out=NULL;break;case 12:g.points[0]=INT32_MIN;g.points[2]=INT32_MAX;break;
                case 13:transformed=1;matrix=badmatrix;x=32760;y=32760;break;}
                unsigned before=attempts,draws=transfer_calls;
                int result=ps2_gradient_plan_row(&g,matrix,transformed,repeat,x,y,width,height,row,out);
                if(invalid==13){assert(!result);for(unsigned i=0;i<width;++i)assert(positions[i]==INT64_MIN);}
                else assert(result<0);
                assert(attempts==before && transfer_calls==draws);++checks;
            }
            for(unsigned i=0;i<10;++i)assert(!ps2_surface_destroy(gs,h[i]));
            assert(!ps2_surface_destroy(gs,p));assert(!ps2_filter_release(&f) && !ps2_divide_release(&w));finish_rop();
        }else{
            fail_create=failure==1;transfer_failure=failure==2;
            assert(ps2_gradient_fetch(&w,&f,&good,0,32768,h,h+4,h[8],h[9],p,0)<0 && errno==(failure==1?ENOSPC:EIO) && gs->error==(failure==1?0:EIO));
            fail_create=transfer_failure=0;unsigned before=attempts,draws=transfer_calls;
            int retry=ps2_gradient_fetch(&w,&f,&good,0,32768,h,h+4,h[8],h[9],p,0);
            if(failure==1)assert(!retry && !gs->error && attempts>before);
            else assert(retry<0 && gs->error==EIO && attempts==before && transfer_calls==draws);
            polygon_finish();for(unsigned i=0;i<PS2GS_MAX_SURFACES+1;++i)assert(!models[i].data);
        }
    }
    assert(checks==51);printf("PS2 gradient preflight guards=%u allocation recovery=1 submission failure latching=1 PASS\n",checks);
}

static void packed10_operator_guards_case(void)
{
    unsigned checks=0,noops=0;
    for(unsigned failure=0;failure<3;++failure){
        rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
        struct ps2_divide_workspace w={0};struct ps2_filter_workspace f={0};
        assert(!ps2_divide_reserve(&w,gs) && !ps2_filter_reserve(&f,gs,3));uint32_t h[36],p;
        for(unsigned i=0;i<36;++i)assert(!ps2_surface_create(gs,64,32,32,h+i));
        assert(!ps2_surface_create(gs,16,16,32,&p));
        if(!failure){
            for(unsigned invalid=0;invalid<28;++invalid){
                struct ps2_divide_workspace ww=w,*wp=&ww;struct ps2_filter_workspace ff=f,*fp=&ff;
                uint32_t all[36];memcpy(all,h,sizeof(all));const uint32_t *s=all,*m=all+8,*d=all+16,*t=all+24;
                unsigned op=PS2_COMPOSITE_SCREEN,ca=0,width=1,height=1,features=gs->caps.features;uint32_t pal=p;
                switch(invalid){case 0:wp=NULL;break;case 1:fp=NULL;break;case 2:s=NULL;break;case 3:m=NULL;break;
                case 4:d=NULL;break;case 5:t=NULL;break;case 6:op=PS2_COMPOSITE_SRC;break;
                case 7:op=PS2_COMPOSITE_DISJOINT_CLEAR;break;case 8:op=UINT_MAX;break;case 9:ca=2;break;
                case 10:width=0;break;case 11:height=33;break;case 12:all[0]=0;break;case 13:all[8]=all[0];break;
                case 14:all[16]=all[0];break;case 15:all[24]=all[16];break;case 16:all[24]=w.borrow;break;
                case 17:pal=0;break;case 18:pal=h[0];break;case 19:ff.bytes=2;break;case 20:ww.borrow=0;break;
                case 21:gs->caps.features&=~PS2GS_HW_RGBA_TRANSFER;break;case 22:gs->caps.features&=~PS2GS_HW_SOLID_LOGIC;break;
                case 23:gs->caps.features&=~PS2GS_HW_CHANNEL_LUT;break;case 24:gs->caps.features&=~PS2GS_HW_CHANNEL_MAP;break;
                case 25:gs->caps.features&=~PS2GS_HW_TEXTURE_TRANSFER;break;case 26:all[24]=f.tap;break;case 27:all[1]=all[0];break;}
                unsigned before=attempts,draws=transfer_calls;
                assert(ps2_native_float_blend_consume(wp,fp,s,m,d,t,pal,width,height,op,ca)<0 && attempts==before && transfer_calls==draws);++checks;
                gs->caps.features=features;
            }
            for(unsigned op=PS2_COMPOSITE_HSL_HUE;op<=PS2_COMPOSITE_HSL_LUMINOSITY;++op){unsigned before=attempts,draws=transfer_calls;
                assert(!ps2_native_float_blend_consume(&w,&f,h,h+8,h+16,h+24,p,1,1,op,1) && attempts==before && transfer_calls==draws);++noops;}
            for(unsigned i=0;i<36;++i)assert(!ps2_surface_destroy(gs,h[i]));
            assert(!ps2_surface_destroy(gs,p) && !ps2_filter_release(&f) && !ps2_divide_release(&w));finish_rop();
        }else{
            fail_create=failure==1;transfer_failure=failure==2;
            assert(ps2_native_float_blend_consume(&w,&f,h,h+8,h+16,h+24,p,1,1,PS2_COMPOSITE_HSL_COLOR,0)<0 && errno==(failure==1?ENOSPC:EIO) && gs->error==(failure==1?0:EIO));
            fail_create=transfer_failure=0;unsigned before=attempts,draws=transfer_calls;
            int retry=ps2_native_float_blend_consume(&w,&f,h,h+8,h+16,h+24,p,1,1,PS2_COMPOSITE_HSL_COLOR,0);
            if(failure==1)assert(!retry && !gs->error && attempts>before);
            else assert(retry<0 && gs->error==EIO && attempts==before && transfer_calls==draws);
            polygon_finish();for(unsigned i=0;i<PS2GS_MAX_SURFACES+1;++i)assert(!models[i].data);
        }
    }
    assert(checks==28 && noops==4);printf("PS2 packed10 operator guards=%u HSL_CA_noops=%u allocation recovery=1 submission failure latching=1 PASS\n",checks,noops);
}
static void packed10_quantize_case(void)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    struct ps2_divide_workspace w={0};struct ps2_filter_workspace f={0};
    assert(!ps2_divide_reserve(&w,gs) && !ps2_filter_reserve(&f,gs,3));uint32_t h[6],p;
    for(unsigned b=0;b<6;++b)assert(!ps2_surface_create(gs,64,32,32,h+b));
    assert(!ps2_surface_create(gs,16,16,32,&p));unsigned checks=0,state=0x51a713f9;
    for(unsigned n=1;n<=16;++n){unsigned sizes[]={n,17-n,n==16?10:n+1};uint32_t expected[2048][3];
        for(unsigned i=0;i<2048;++i){for(unsigned b=0;b<6;++b)model(h[b])->data[i]=0x91000000;
            for(unsigned c=0;c<3;++c){state=state*1664525U+1013904223U;
                uint32_t raw=((1+state%254)<<23)|(state&0x7fffff)|(state&0x80000000);
                if(i%8<4){float v=((i*31+c*17)%1025)*(1.0f/1024);memcpy(&raw,&v,4);
                    if(i%8==1 && raw)--raw;
                    if(i%8==2)++raw;
                    if(i%8==3)raw|=0x80000000;}
                if(i%8==4)raw=state&0x807fffff;
                if(i%8==5)raw=i&1?0x3f800000:0x80000000;
                float v=number(raw);unsigned limit=(1U<<sizes[c])-1;
                expected[i][c]=v<=0?0:v>=1?limit:(unsigned)(v*(1U<<sizes[c]));
                for(unsigned b=0;b<4;++b)model(h[b])->data[i]|=((raw>>(8*b))&255)<<(8*c);
            }
        }
        compositor_active=1;assert(!ps2_native_float_unorm_words(&w,&f,h,h+4,sizes,p,64,32));compositor_active=0;
        for(unsigned i=0;i<2048;++i)for(unsigned c=0;c<3;++c){unsigned actual=
            ((model(h[4])->data[i]>>(8*c))&255)|(((model(h[5])->data[i]>>(8*c))&255)<<8);
            if(actual!=expected[i][c]){fprintf(stderr,"packed quantize n=%u pixel=%u c=%u expected=%u actual=%u\n",n,i,c,expected[i][c],actual);abort();}++checks;}
    }
    assert(checks==98304);printf("PS2 packed UNORM1..16 signed/clamped quantization lanes=%u PASS\n",checks);
    for(unsigned b=0;b<6;++b)assert(!ps2_surface_destroy(gs,h[b]));
    assert(!ps2_surface_destroy(gs,p) && !ps2_filter_release(&f) && !ps2_divide_release(&w));finish_rop();
}
static void packed10_guards_case(void)
{
    unsigned checks=0;
    for(unsigned failure=0;failure<3;++failure){
        rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
        struct ps2_packed_workspace packed={0};struct ps2_divide_workspace w={0};struct ps2_filter_workspace f={0};
        assert(!ps2_packed_reserve(&packed,gs) && !ps2_divide_reserve(&w,gs) && !ps2_filter_reserve(&f,gs,3));
        uint32_t h[24],raw,p;for(unsigned i=0;i<24;++i)assert(!ps2_surface_create(gs,64,32,32,h+i));
        assert(!ps2_surface_create(gs,64,32,32,&raw) && !ps2_surface_set_depth(gs,raw,32) && !ps2_surface_create(gs,16,16,32,&p));
        struct ps2_packed_layout good={.bits={10,10,10,2},.shift={20,10,0,30}};
        if(!failure){
            for(unsigned invalid=0;invalid<18;++invalid){struct ps2_packed_workspace ww=packed;
                struct ps2_packed_layout l=good;uint32_t planes[8];memcpy(planes,h,32);uint32_t r=raw,pal=p;
                unsigned x=0,y=0,width=1,height=1,features=gs->caps.features;
                switch(invalid){case 0:l.bits[0]=11;break;case 1:l.bits[1]=0;break;case 2:l.bits[3]=9;break;
                case 3:l.shift[0]=31;break;case 4:l.shift[1]=0;break;case 5:planes[1]=planes[0];break;
                case 6:planes[0]=raw;break;case 7:planes[0]=packed.scratch[0];break;case 8:ww.scratch[1]=ww.scratch[0];break;
                case 9:pal=0;break;case 10:r=0;break;case 11:x=64;break;case 12:y=32;break;
                case 13:width=65;break;case 14:height=0;break;
                case 15:gs->caps.features&=~PS2GS_HW_CHANNEL_MAP_BLIT;break;
                case 16:gs->caps.features&=~PS2GS_HW_RGBA_TRANSFER;break;
                case 17:gs->caps.features&=~PS2GS_HW_SOLID_LOGIC;break;}
                unsigned before=attempts,draws=transfer_calls;
                assert(ps2_packed_fetch(&ww,r,x,y,&l,planes,pal,width,height)<0 && attempts==before && transfer_calls==draws);++checks;
                assert(ps2_packed_store(&ww,&w,&f,planes,r,x,y,&l,pal,width,height)<0 && attempts==before && transfer_calls==draws);++checks;
                gs->caps.features=features;
            }
            for(unsigned invalid=0;invalid<10;++invalid){uint32_t rgb[24];memcpy(rgb,h,sizeof(rgb));
                unsigned op=PS2_COMPOSITE_SRC,ca=0,width=1,features=gs->caps.features;struct ps2_filter_workspace ff=f;
                switch(invalid){case 0:op=PS2_COMPOSITE_SATURATE;break;case 1:ca=2;break;case 2:width=65;break;
                case 3:rgb[8]=rgb[0];break;case 4:rgb[16]=w.borrow;break;case 5:rgb[0]=p;break;case 6:rgb[9]=0;break;
                case 7:ff.bytes=2;break;case 8:ff.tap=ff.term;break;case 9:gs->caps.features&=~PS2GS_HW_SOLID_LOGIC;break;}
                unsigned before=attempts,draws=transfer_calls;
                assert(ps2_packed_blend(&w,&ff,rgb,rgb+8,rgb+16,p,width,1,op,ca)<0 && attempts==before && transfer_calls==draws);++checks;
                gs->caps.features=features;
            }
            unsigned before=attempts,draws=transfer_calls,bits_[3]={10,10,10};uint32_t out[]={h[0],h[1]};
            assert(ps2_native_float_unorm_words(&w,&f,h,out,bits_,p,1,1)<0 && attempts==before && transfer_calls==draws);++checks;
            assert(ps2_native_float_operands(&w,&f,h,UINT_MAX,h,1,p,1,1)<0 && attempts==before && transfer_calls==draws);++checks;
            assert(!ps2_surface_set_depth(gs,source_handle,32) && !ps2_surface_set_depth(gs,destination_handle(),32));
            for(unsigned invalid=0;invalid<7;++invalid){struct ps2_picture_sample sample={.repeat=1,.source_argb=1,.destination_argb=1,
                    .source_packed10=1,.destination_packed10=2};unsigned op=PS2_COMPOSITE_SRC;
                struct ps2_pipe_copy_box box={0,0,3,2,1,1};
                switch(invalid){case 0:op=UINT_MAX;break;case 1:sample.source_packed10=5;break;
                case 2:sample.source_alpha_map.enabled=1;break;case 3:sample.destination_alpha_map.enabled=1;break;
                case 4:sample.source_swap=1;break;case 5:sample.destination_argb=0;break;case 6:sample.convolution=1;break;}
                before=attempts;draws=transfer_calls;
                assert(ps2_pipe_composite_rect(ctx,source_handle,destination_handle(),&box,&sample,NULL,op)<0 && attempts==before && transfer_calls==draws);++checks;
            }
            for(unsigned i=0;i<24;++i)assert(!ps2_surface_destroy(gs,h[i]));
            assert(!ps2_surface_destroy(gs,raw) && !ps2_surface_destroy(gs,p));
            assert(!ps2_filter_release(&f) && !ps2_divide_release(&w) && !ps2_packed_release(&packed));finish_rop();
        }else{
            fail_create=failure==1;transfer_failure=failure==2;
            assert(ps2_packed_blend(&w,&f,h,h+8,h+16,p,1,1,PS2_COMPOSITE_OVER,0)<0 && errno==(failure==1?ENOSPC:EIO) && gs->error==(failure==1?0:EIO));
            fail_create=transfer_failure=0;unsigned before=attempts,draws=transfer_calls;
            int retry=ps2_packed_blend(&w,&f,h,h+8,h+16,p,1,1,PS2_COMPOSITE_OVER,0);
            if(failure==1)assert(!retry && !gs->error && attempts>before);
            else assert(retry<0 && gs->error==EIO && attempts==before && transfer_calls==draws);
            polygon_finish();for(unsigned i=0;i<PS2GS_MAX_SURFACES+1;++i)assert(!models[i].data);
        }
    }
    assert(checks==55);printf("PS2 packed10 preflight guards=%u allocation recovery=1 submission failure latching=1 PASS\n",checks);
}
static void packed10_codec_case(void)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    struct ps2_packed_workspace packed={0};struct ps2_divide_workspace wide={0};struct ps2_filter_workspace f={0};
    assert(!ps2_packed_reserve(&packed,gs) && !ps2_divide_reserve(&wide,gs) && !ps2_filter_reserve(&f,gs,3));
    uint32_t raw=0,p=0,h[8];assert(!ps2_surface_create(gs,64,32,32,&raw) && !ps2_surface_set_depth(gs,raw,32));
    assert(!ps2_surface_create(gs,16,16,32,&p));for(unsigned i=0;i<8;++i)assert(!ps2_surface_create(gs,64,32,32,h+i));
    unsigned checks=0;
    for(unsigned kind=0;kind<4;++kind){struct ps2_packed_layout layout={.bits={10,10,10,kind<2?2:0},.shift={kind&1?0:20,10,kind&1?20:0,30}};
        pixman_format_code_t code=kind==0?PIXMAN_a2r10g10b10:kind==1?PIXMAN_a2b10g10r10:kind==2?PIXMAN_x2r10g10b10:PIXMAN_x2b10g10r10;
        uint32_t original[2048];float values[2048][4];
        for(unsigned i=0;i<2048;++i){uint32_t rgb[4]={i&1023,(i*73+519)&1023,(i*179+7)&1023,i>>9};
            original[i]=rgb[0]<<layout.shift[0]|rgb[1]<<10|rgb[2]<<layout.shift[2]|rgb[3]<<30;
            model(raw)->data[i]=original[i];}
        pixman_image_t *si=pixman_image_create_bits(code,64,32,original,256),
            *di=pixman_image_create_bits(PIXMAN_rgba_float,64,32,(uint32_t*)values,1024);assert(si && di);
        pixman_image_composite32(PIXMAN_OP_SRC,si,NULL,di,0,0,0,0,0,0,64,32);
        compositor_active=1;assert(!ps2_packed_fetch(&packed,raw,0,0,&layout,h,p,64,32));compositor_active=0;
        for(unsigned i=0;i<2048;++i)for(unsigned c=0;c<4;++c){uint32_t wanted,actual=0;memcpy(&wanted,&values[i][c],4);
            for(unsigned b=0;b<4;++b)actual|=((model(h[(c==3?4:0)+b])->data[i]>>(8*(c==3?0:c)))&255)<<(8*b);
            if(actual!=wanted){fprintf(stderr,"packed10 fetch kind=%u i=%u c=%u expected=%08x actual=%08x\n",kind,i,c,wanted,actual);abort();}++checks;}
        compositor_active=1;assert(!ps2_packed_store(&packed,&wide,&f,h,raw,0,0,&layout,p,64,32));compositor_active=0;
        for(unsigned i=0;i<2048;++i){uint32_t wanted=original[i]&(kind<2?UINT32_MAX:0x3fffffff);
            if(model(raw)->data[i]!=wanted){fprintf(stderr,"packed10 store kind=%u i=%u expected=%08x actual=%08x\n",kind,i,wanted,model(raw)->data[i]);abort();}++checks;}
        pixman_image_unref(si);pixman_image_unref(di);
    }
    assert(checks==40960);printf("PS2 packed10 codec byte/float checks=%u PASS\n",checks);
    for(unsigned i=0;i<8;++i)assert(!ps2_surface_destroy(gs,h[i]));
    assert(!ps2_surface_destroy(gs,raw) && !ps2_surface_destroy(gs,p));
    assert(!ps2_filter_release(&f) && !ps2_divide_release(&wide) && !ps2_packed_release(&packed));finish_rop();
}


static void packed10_convolution_preflight_case(void)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    assert(!ps2_surface_set_depth(gs,source_handle,32) && !ps2_surface_set_depth(gs,destination_handle(),32));xp.drawable.depth=32;
    struct ps2_exa_context_bridge bridge={0};struct ps2_texture_exa texture={0};struct ps2_blit_exa blit={0};
    assert(ps2_exa_context_bridge_prepare(&xs,&bridge,&exa,ctx));
    assert(ps2_texture_exa_prepare(&xs,&texture,&exa,ps2_pipe_context_render(ctx)));
    assert(ps2_blit_exa_prepare(&xs,&blit,&exa,ctx));
    struct pixels *sp=model(source_handle),*dp=model(destination_handle());
    PixmapRec source_pixmap={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=32,.bitsPerPixel=32,
        .width=sp->width,.height=sp->height},.devPrivates=(void*)&source_handle};
    int32_t kernel[]={65536,65536,65536};unsigned checks=0;
    const struct ps2_picture_sample valid={.source_argb=1,.destination_argb=1,.source_packed10=1,.destination_packed10=2,
        .convolution=1,.filter_params=kernel,.filter_nparams=3};
    for(unsigned masked=0;masked<2;++masked)for(unsigned invalid=0;invalid<10;++invalid){
        int32_t local[3];memcpy(local,kernel,sizeof(local));struct ps2_picture_sample sample=valid,bad=valid;bad.filter_params=local;
        struct ps2_picture_bitmap mask={.has_surface=1,.surface=source_handle,.width=sp->width,.height=sp->height,.sample=valid};
        switch(invalid){case 0:bad.bilinear=1;break;case 1:bad.filter_params=NULL;break;case 2:bad.filter_nparams=2;break;
        case 3:bad.filter_nparams=262145;break;case 4:local[0]=0;break;case 5:local[0]=65537;break;
        case 6:local[1]=2049*65536;break;case 7:local[1]=131072;break;
        case 8:bad.filter_params=(const int32_t*)(UINTPTR_MAX-3);break;case 9:bad.convolution=0;break;}
        if(masked)mask.sample=bad;else sample=bad;
        struct ps2_pipe_copy_box box={0,0,3,2,1,1};unsigned before=attempts,draws=transfer_calls;
        assert(ps2_pipe_composite_rect(ctx,source_handle,destination_handle(),&box,&sample,masked?&mask:NULL,PS2_COMPOSITE_SRC)<0);
        assert(attempts==before && transfer_calls==draws);++checks;
        PictureRec src={.pDrawable=&source_pixmap.drawable,.format=PICT_a2r10g10b10,.filter=PictFilterConvolution,
            .filter_params=kernel,.filter_nparams=3},mp=src,
            dest={.pDrawable=&xp.drawable,.format=PICT_a2b10g10r10,.filter=PictFilterNearest};
        PicturePtr picture=masked?&mp:&src;picture->filter=bad.bilinear?PictFilterBilinear:bad.convolution?PictFilterConvolution:PictFilterNearest;
        picture->filter_params=(int32_t*)bad.filter_params;picture->filter_nparams=bad.filter_nparams;
        assert(!exa.driver.CheckComposite(PictOpSrc,&src,masked?&mp:NULL,&dest) && attempts==before && transfer_calls==draws);++checks;
    }
    int32_t *large=calloc(4098,4);assert(large);large[0]=2048*65536;large[1]=2*65536;
    for(unsigned masked=0;masked<2;++masked){
        struct ps2_picture_sample sample=valid;
        struct ps2_picture_bitmap mask={.has_surface=1,.surface=source_handle,.width=sp->width,.height=sp->height,.sample=valid};
        struct ps2_picture_sample *filtered=masked?&mask.sample:&sample;filtered->filter_params=large;filtered->filter_nparams=4098;
        struct ps2_pipe_copy_box box={0,0,0,0,dp->width,dp->height};unsigned before=attempts,draws=transfer_calls;
        assert((uint64_t)box.width*box.height*4096>16777216);
        assert(ps2_pipe_composite_rect(ctx,source_handle,destination_handle(),&box,&sample,masked?&mask:NULL,PS2_COMPOSITE_OVER)<0 && errno==E2BIG);
        assert(attempts==before && transfer_calls==draws);++checks;
        sample=valid;mask.sample=valid;box.width=box.height=1;malloc_fail=1;mal_calls=0;
        assert(ps2_pipe_composite_rect(ctx,source_handle,destination_handle(),&box,&sample,masked?&mask:NULL,PS2_COMPOSITE_OVER)<0 && errno==ENOMEM);
        malloc_fail=0;assert(attempts==before && transfer_calls==draws);++checks;
    }
    free(large);assert(checks==44);printf("PS2 packed10 convolution preflight=%u source_mask_params_budget_allocation PASS\n",checks);
    ps2_blit_exa_cleanup(&xs,&blit);ps2_exa_context_bridge_cleanup(&bridge);finish_rop();
}
static void packed10_convolution_float_case(unsigned guards)
{
    unsigned checks=0;
    for(unsigned failure=0;failure<(guards?3U:1U);++failure){
        rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
        struct ps2_divide_workspace w={0};struct ps2_filter_workspace f={0};uint32_t h[12],p;
        assert(!ps2_divide_reserve(&w,gs) && !ps2_filter_reserve(&f,gs,3));
        for(unsigned i=0;i<12;++i)assert(!ps2_surface_create(gs,64,32,32,h+i));
        assert(!ps2_surface_create(gs,16,16,32,&p));
        if(failure){
            fail_create=failure==1;transfer_failure=failure==2;
            assert(ps2_native_convolution_float_sample(&w,&f,h+4,-65536,h,p,64,1)<0 && errno==(failure==1?ENOSPC:EIO) && gs->error==(failure==1?0:EIO));
            fail_create=transfer_failure=0;unsigned before=attempts,draws=transfer_calls;
            int retry=ps2_native_convolution_float_sample(&w,&f,h+4,-65536,h,p,64,1);
            if(failure==1)assert(!retry && !gs->error && attempts>before);
            else assert(retry<0 && gs->error==EIO && attempts==before && transfer_calls==draws);
            polygon_finish();for(unsigned i=0;i<PS2GS_MAX_SURFACES+1;++i)assert(!models[i].data);continue;
        }
        if(guards){
            for(unsigned invalid=0;invalid<21;++invalid){
                uint32_t in[4],acc[4],palette=p;memcpy(in,h+4,16);memcpy(acc,h,16);
                const uint32_t *sample=in,*total=acc;unsigned width=64,height=1,features=gs->caps.features;
                struct ps2_divide_workspace ww=w,*wp=&ww;struct ps2_filter_workspace ff=f,*fp=&ff;
                switch(invalid){case 0:wp=NULL;break;case 1:fp=NULL;break;case 2:sample=NULL;break;case 3:total=NULL;break;
                case 4:in[0]=acc[1];break;case 5:acc[2]=acc[1];break;case 6:in[0]=w.term;break;case 7:acc[0]=f.carry;break;
                case 8:palette=0;break;case 9:palette=h[0];break;case 10:width=0;break;case 11:width=65;break;case 12:height=33;break;
                case 13:ff.bytes=2;break;case 14:ww.borrow=0;break;case 15:in[2]=UINT32_MAX;break;
                case 16:gs->caps.features&=~PS2GS_HW_SOLID_LOGIC;break;case 17:gs->caps.features&=~PS2GS_HW_CHANNEL_LUT;break;
                case 18:gs->caps.features&=~PS2GS_HW_CHANNEL_MAP;break;case 19:gs->caps.features&=~PS2GS_HW_TEXTURE_TRANSFER;break;
                case 20:gs->caps.features&=~PS2GS_HW_RGBA_TRANSFER;break;}
                unsigned before=attempts,draws=transfer_calls;
                assert(ps2_native_convolution_float_sample(wp,fp,sample,65536,total,palette,width,height)<0 && before==attempts && draws==transfer_calls);
                gs->caps.features=features;++checks;
            }
            unsigned before=attempts,draws=transfer_calls;
            assert(!ps2_native_convolution_float_sample(&w,&f,h+4,0,h,p,64,1) && before==attempts && draws==transfer_calls);
        }else{
            const unsigned layouts[][3]={{10,10,10},{2,2,2},{8,8,8},{1,5,10}};
            const uint32_t corners[]={0,1,32767,32768,65535,65536,0x7fffff,0x800000,0x800001,
                0x7fffff80,0x80000000,0xffffff00,0xfffffffe,0xffffffff};
            const int32_t coefficients[]={1,-1,32767,-32768,65535,-65536,65536,131071,
                INT32_MAX,INT32_MIN,17,-257};
            uint32_t state=0x17893527,expected[64][3];
            for(unsigned layout=0;layout<4;++layout)for(unsigned kind=0;kind<12;++kind){
                for(unsigned b=0;b<12;++b)for(unsigned i=0;i<2048;++i)model(h[b])->data[i]=0xa5332211;
                for(unsigned i=0;i<64;++i)for(unsigned c=0;c<3;++c){
                    state=state*1664525U+1013904223U;expected[i][c]=i<28?corners[(i+c)%14]:state;
                    float value=(float)expected[i][c]*0x1p-32f;uint32_t raw;memcpy(&raw,&value,4);
                    for(unsigned b=0;b<4;++b){uint32_t *v=&model(h[b])->data[i];*v=(*v&~(255U<<(c*8)))|(((raw>>(b*8))&255)<<(c*8));}
                }
                for(unsigned tap=0;tap<3;++tap){int32_t coefficient=coefficients[(kind+tap)%12];uint32_t original[4][2048];
                    for(unsigned i=0;i<64;++i)for(unsigned c=0;c<3;++c){unsigned mask=(1U<<layouts[layout][c])-1;
                        state=state*1664525U+1013904223U;unsigned code=i<4?(i==0?0:i==1?mask:mask/2):state&mask;
                        volatile float reciprocal=1.0f/mask,value=code*reciprocal,product=value*(float)coefficient;
                        volatile float sum=(float)expected[i][c]+product;expected[i][c]=convolution_target_cast(sum);
                        uint32_t raw;float sample=value;memcpy(&raw,&sample,4);
                        for(unsigned b=0;b<4;++b){uint32_t *v=&model(h[4+b])->data[i];*v=(*v&~(255U<<(c*8)))|(((raw>>(b*8))&255)<<(c*8));}
                    }
                    for(unsigned b=0;b<4;++b)memcpy(original[b],model(h[4+b])->data,sizeof(original[b]));
                    compositor_active=1;assert(!ps2_native_convolution_float_sample(&w,&f,h+4,coefficient,h,p,64,1));compositor_active=0;
                    for(unsigned b=0;b<4;++b)assert(!memcmp(original[b],model(h[4+b])->data,sizeof(original[b])));
                    for(unsigned i=0;i<2048;++i)for(unsigned b=0;b<4;++b){uint32_t wanted=0xa5332211;
                        if(i<64){wanted=0xa5000000;for(unsigned c=0;c<3;++c){float value=(float)expected[i][c]*0x1p-32f;uint32_t raw;memcpy(&raw,&value,4);wanted|=((raw>>(b*8))&255)<<(c*8);}}
                        if(model(h[b])->data[i]!=wanted){fprintf(stderr,"packed10 convolution accumulator layout=%u kind=%u tap=%u i=%u b=%u expected=%08x actual=%08x\n",layout,kind,tap,i,b,wanted,model(h[b])->data[i]);abort();}
                    }
                    checks+=64*3;
                }
                const uint32_t *out=kind&1?h:h+8;
                compositor_active=1;assert(!ps2_native_convolution_float_end(&w,&f,h,out,p,64,1));compositor_active=0;
                for(unsigned i=0;i<64;++i)for(unsigned b=0;b<4;++b){uint32_t wanted=0xa5000000;
                    for(unsigned c=0;c<3;++c){float value=expected[i][c]&0x80000000U?0:(float)expected[i][c]/65536.f;if(value>1)value=1;
                        uint32_t raw;memcpy(&raw,&value,4);wanted|=((raw>>(b*8))&255)<<(c*8);}
                    assert(model(out[b])->data[i]==wanted);
                }
            }
        }
        for(unsigned i=0;i<12;++i)assert(!ps2_surface_destroy(gs,h[i]));
        assert(!ps2_surface_destroy(gs,p) && !ps2_filter_release(&f) && !ps2_divide_release(&w));finish_rop();
    }
    assert(checks==(guards?21U:27648U));
    printf("PS2 packed10 convolution %s=%u%s PASS\n",guards?"guards":"accumulator_channels",checks,guards?" zero_noop=1 allocation_recovery=1 submission_latch=1":" readonly_samples=1");
}
static void packed10_alpha_read_guards_case(void)
{
    unsigned checks=0;
    for(unsigned failure=0;failure<3;++failure){
        rgba_enabled=1;setup_rop(32);struct ps2_render_client *r=ps2_pipe_context_render(ctx);struct ps2_surface_client *gs=r->gs;
        assert(!ps2_surface_set_depth(gs,source_handle,32) && !ps2_surface_set_depth(gs,destination_handle(),32));
        uint32_t small;assert(!ps2_surface_create(gs,83,61,16,&small));
        struct ps2_picture_sample valid={.source_argb=1,.destination_argb=1,.source_packed10=1,.destination_packed10=2};
        const struct ps2_picture_alpha_map map={.enabled=1,.surface=source_handle,.width=83,.height=61,
            .has_alpha=1,.alpha_bits=2,.rgb_bits={10,10,10}};
        struct ps2_pipe_copy_box box={0,0,3,2,1,1};
        if(!failure){
            assert(!ps2_render_fill(r,0,0,1,1,0));unsigned queued=r->count;
            for(unsigned role=0;role<2;++role)for(unsigned invalid=0;invalid<18;++invalid){
                struct ps2_picture_sample source=valid;
                struct ps2_picture_bitmap mask={.has_surface=1,.surface=source_handle,.width=83,.height=61,.sample=valid};
                struct ps2_picture_sample *s=role?&mask.sample:&source;s->source_alpha_map=map;
                struct ps2_picture_alpha_map *m=&s->source_alpha_map;
                switch(invalid){case 0:m->enabled=2;break;case 1:m->surface=UINT32_MAX;break;case 2:m->width=0;break;
                case 3:--m->height;break;case 4:m->has_alpha=0;break;case 5:m->alpha_only=1;break;
                case 6:m->swap=2;break;case 7:m->rotate=1;break;case 8:m->alpha_bits=9;break;case 9:m->rgb_bits[1]=11;break;
                case 10:m->rgb_bits[0]=9;break;case 11:assert(!ps2_surface_set_depth(gs,source_handle,24));break;
                case 12:m->surface=small;break;case 13:m->surface=0;break;case 14:m->alpha_bits=0;break;case 15:m->width=1025;break;
                case 16:s->source_solid=1;break;case 17:s->source_packed10=0;s->gradient.kind=1;break;}
                unsigned before=attempts,draws=transfer_calls;
                assert(ps2_pipe_composite_rect(ctx,source_handle,destination_handle(),&box,&source,role?&mask:NULL,PS2_COMPOSITE_OVER)<0 && errno==EINVAL);
                assert(attempts==before && transfer_calls==draws && r->count==queued);++checks;
                if(invalid==11)assert(!ps2_surface_set_depth(gs,source_handle,32));
            }
            assert(!ps2_render_flush(r) && !ps2_surface_destroy(gs,small));finish_rop();
        }else{
            valid.source_alpha_map=map;fail_create=failure==1;transfer_failure=failure==2;
            assert(ps2_pipe_composite_rect(ctx,source_handle,destination_handle(),&box,&valid,NULL,PS2_COMPOSITE_OVER)<0 && errno==(failure==1?ENOSPC:EIO) && gs->error==(failure==1?0:EIO));
            fail_create=transfer_failure=0;unsigned before=attempts,draws=transfer_calls;
            int retry=ps2_pipe_composite_rect(ctx,source_handle,destination_handle(),&box,&valid,NULL,PS2_COMPOSITE_OVER);
            if(failure==1)assert(!retry && !gs->error && attempts>before);
            else assert(retry<0 && gs->error==EIO && attempts==before && transfer_calls==draws);
            polygon_finish();for(unsigned i=0;i<PS2GS_MAX_SURFACES+1;++i)assert(!models[i].data);
        }
    }
    assert(checks==36);printf("PS2 packed10 alpha-read guards=%u queued_batch_preserved=1 allocation_recovery=1 submission_latch=1 PASS\n",checks);
}
static const unsigned packed10_all_ops[]={PictOpClear,PictOpSrc,PictOpDst,PictOpOver,PictOpOverReverse,PictOpIn,PictOpInReverse,
        PictOpOut,PictOpOutReverse,PictOpAtop,PictOpAtopReverse,PictOpXor,PictOpAdd,PictOpSaturate,
        PictOpDisjointClear,PictOpDisjointSrc,PictOpDisjointDst,PictOpDisjointOver,PictOpDisjointOverReverse,
        PictOpDisjointIn,PictOpDisjointInReverse,PictOpDisjointOut,PictOpDisjointOutReverse,PictOpDisjointAtop,PictOpDisjointAtopReverse,PictOpDisjointXor,
        PictOpConjointClear,PictOpConjointSrc,PictOpConjointDst,PictOpConjointOver,PictOpConjointOverReverse,
        PictOpConjointIn,PictOpConjointInReverse,PictOpConjointOut,PictOpConjointOutReverse,PictOpConjointAtop,PictOpConjointAtopReverse,PictOpConjointXor,
        PictOpMultiply,PictOpScreen,PictOpOverlay,PictOpDarken,PictOpLighten,PictOpColorDodge,PictOpColorBurn,PictOpHardLight,PictOpSoftLight,PictOpDifference,PictOpExclusion,
        PictOpHSLHue,PictOpHSLSaturation,PictOpHSLColor,PictOpHSLLuminosity};

static void packed10_alpha_write_guards_case(void)
{
    unsigned checks=0;
    for(unsigned failure=0;failure<3;++failure){
        rgba_enabled=1;setup_rop(32);struct ps2_render_client *r=ps2_pipe_context_render(ctx);struct ps2_surface_client *gs=r->gs;
        assert(!ps2_surface_set_depth(gs,source_handle,32) && !ps2_surface_set_depth(gs,destination_handle(),32));
        uint32_t small;assert(!ps2_surface_create(gs,83,61,16,&small));
        struct ps2_picture_sample valid={.source_argb=1,.destination_argb=1,.source_packed10=1,.destination_packed10=2,
            .destination_alpha_map={.enabled=1,.surface=source_handle,.width=83,.height=61,
                .has_alpha=1,.alpha_bits=2,.rgb_bits={10,10,10},.swap=1,.x=1,.y=1}};
        struct ps2_pipe_copy_box box={0,0,3,2,2,2};
        struct ps2_format_alias invalid_alias={0};
        if(!failure){
            assert(!ps2_render_fill(r,0,0,1,1,0));unsigned queued=r->count;
            for(unsigned clear=0;clear<2;++clear)for(unsigned invalid=0;invalid<24;++invalid){
                struct ps2_picture_sample s=valid;struct ps2_picture_alpha_map *m=&s.destination_alpha_map;
                switch(invalid){case 0:m->enabled=2;break;case 1:m->surface=UINT32_MAX;break;case 2:m->width=0;break;
                case 3:--m->height;break;case 4:m->has_alpha=0;break;case 5:m->alpha_only=1;break;
                case 6:m->swap=2;break;case 7:m->rotate=1;break;case 8:m->alpha_bits=9;break;case 9:m->rgb_bits[1]=11;break;
                case 10:m->rgb_bits[0]=9;break;case 11:assert(!ps2_surface_set_depth(gs,source_handle,24));break;
                case 12:m->surface=small;break;case 13:m->surface=0;break;case 14:m->alpha_bits=0;break;case 15:m->width=1025;break;
                case 16:m->x=4;break;case 17:m->x=-79;break;case 18:m->y=3;break;case 19:m->y=-58;break;
                case 20:m->has_alpha=2;break;case 21:s.destination_alias=&invalid_alias;break;
                case 22:s.alpha_only_destination=1;break;case 23:m->alpha_bits=3;break;}
                unsigned before=attempts,draws=transfer_calls;
                assert(ps2_pipe_composite_rect(ctx,source_handle,destination_handle(),&box,&s,NULL,clear?PS2_COMPOSITE_CLEAR:PS2_COMPOSITE_OVER)<0 && errno==EINVAL);
                assert(attempts==before && transfer_calls==draws && r->count==queued);++checks;
                if(invalid==11)assert(!ps2_surface_set_depth(gs,source_handle,32));
            }
            unsigned before=attempts,draws=transfer_calls;
            assert(!ps2_pipe_composite_rect(ctx,source_handle,destination_handle(),&box,&valid,NULL,PS2_COMPOSITE_DST));
            struct ps2_picture_sample opaque=valid;opaque.source_argb=0;opaque.source_packed10=PS2_PACKED10_RGB;opaque.repeat=PS2_REPEAT_NORMAL;
            assert(!ps2_pipe_composite_rect(ctx,source_handle,destination_handle(),&box,&opaque,NULL,PS2_COMPOSITE_IN_REVERSE));
            box.width=0;assert(!ps2_pipe_composite_rect(ctx,source_handle,destination_handle(),&box,&valid,NULL,PS2_COMPOSITE_OVER));
            assert(attempts==before && transfer_calls==draws && r->count==queued);
            assert(!ps2_render_flush(r) && !ps2_surface_destroy(gs,small));finish_rop();
        }else{
            fail_create=failure==1;transfer_failure=failure==2;
            assert(ps2_pipe_composite_rect(ctx,source_handle,destination_handle(),&box,&valid,NULL,PS2_COMPOSITE_OVER)<0 && errno==(failure==1?ENOSPC:EIO) && gs->error==(failure==1?0:EIO));
            fail_create=transfer_failure=0;unsigned before=attempts,draws=transfer_calls;
            int retry=ps2_pipe_composite_rect(ctx,source_handle,destination_handle(),&box,&valid,NULL,PS2_COMPOSITE_OVER);
            if(failure==1)assert(!retry && !gs->error && attempts>before);
            else assert(retry<0 && gs->error==EIO && attempts==before && transfer_calls==draws);
            polygon_finish();for(unsigned i=0;i<PS2GS_MAX_SURFACES+1;++i)assert(!models[i].data);
        }
    }
    assert(checks==48);printf("PS2 packed10 alpha-write guards=%u queued_batch_preserved=1 noops=3 allocation_recovery=1 submission_latch=1 PASS\n",checks);
}
static void packed10_alpha_write_case(unsigned mode)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    assert(!ps2_surface_set_depth(gs,source_handle,32) && !ps2_surface_set_depth(gs,destination_handle(),32));xp.drawable.depth=32;
    struct ps2_exa_context_bridge bridge={0};struct ps2_texture_exa texture={0};struct ps2_blit_exa blit={0};
    assert(ps2_exa_context_bridge_prepare(&xs,&bridge,&exa,ctx) && ps2_texture_exa_prepare(&xs,&texture,&exa,ps2_pipe_context_render(ctx)) && ps2_blit_exa_prepare(&xs,&blit,&exa,ctx));
    uint32_t mh;assert(!ps2_surface_create(gs,31,23,32,&mh) && !ps2_surface_set_depth(gs,mh,32));
    struct pixels *sp=model(source_handle),*dp=model(destination_handle()),*mp=model(mh);
    PixmapRec source_pixmap={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=32,.bitsPerPixel=32,.width=sp->width,.height=sp->height},.devPrivates=(void*)&source_handle},
        mask_pixmap={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=32,.bitsPerPixel=32,.width=mp->width,.height=mp->height},.devPrivates=(void*)&mh};
    const unsigned codes[]={PICT_a2r10g10b10,PICT_a2b10g10r10,PICT_x2r10g10b10,PICT_x2b10g10r10},
        maps[]={PICT_a1,PICT_x4a4,PICT_a8,PICT_a8r8g8b8,PICT_a8b8g8r8,PICT_b8g8r8a8,
            PICT_a2r10g10b10,PICT_a2b10g10r10,PICT_x2r10g10b10,PICT_x2b10g10r10,
            PICT_a2r2g2b2,PICT_a4r4g4b4,PICT_r5g6b5,PICT_x4r4g4b4,PICT_x1r5g5b5,PICT_x1b5g5r5};
    PictTransform transform={.matrix={{49152,21845,173},{0,98304,-21845},{65536/19,0,65536}}},pole={.matrix={{65536,0,0},{0,65536,0},{0,0,0}}};
    int32_t kernel[]={131072,65536,98303,-32767};unsigned checks=0;
    for(unsigned layout=0;layout<4;++layout)for(unsigned variation=0;variation<(mode>=3?4U:16U);++variation)
    for(unsigned operation=0;operation<53;++operation){
        rgba_seed(sp);rgba_seed(dp);rgba_seed(mp);
        unsigned sc=mode==4?PICT_a8r8g8b8:codes[layout],dc=mode==4?PICT_a8b8g8r8:codes[(layout+1)%4],mc=mode==4?PICT_a8b8g8r8:codes[(layout+2)%4],
            map_code=mode==4?codes[(layout+variation)%4]:mode==3?variation<2?codes[(layout+variation)%4]:variation==2?PICT_a8r8g8b8:PICT_b8g8r8a8:maps[variation];
        unsigned alias=mode==3,inputs_alias=alias && variation==3;
        PictureRec src={.pDrawable=inputs_alias?&xp.drawable:&source_pixmap.drawable,.format=sc,.filter=PictFilterNearest,.repeat=TRUE,.repeatType=layout},
            mask={.pDrawable=inputs_alias?&xp.drawable:&mask_pixmap.drawable,.format=mc,.filter=PictFilterNearest,.repeat=TRUE,.repeatType=layout,.componentAlpha=variation&1},
            dest={.pDrawable=&xp.drawable,.format=dc,.filter=PictFilterNearest},map={0};
        src.transform=mask.transform=mode==1 || mode==2?&transform:NULL;
        src.filter=mask.filter=mode==2?PictFilterConvolution:mode==1?PictFilterBilinear:PictFilterNearest;
        if(mode==2){src.filter_params=mask.filter_params=kernel;src.filter_nparams=mask.filter_nparams=4;}
        PixmapPtr map_pixmap=NULL;void *map_memory=NULL;pixman_image_t *map_image;
        size_t sb=(size_t)(inputs_alias?dp:sp)->width*(inputs_alias?dp:sp)->height*4,db=(size_t)dp->width*dp->height*4,mb=(size_t)(inputs_alias?dp:mp)->width*(inputs_alias?dp:mp)->height*4;
        void *sm=malloc(sb),*dm=malloc(db),*mm=malloc(mb);assert(sm && dm && mm);
        memcpy(sm,(inputs_alias?dp:sp)->data,sb);memcpy(dm,dp->data,db);memcpy(mm,(inputs_alias?dp:mp)->data,mb);
        pixman_image_t *si=pixman_image_create_bits(sc,(inputs_alias?dp:sp)->width,(inputs_alias?dp:sp)->height,sm,(inputs_alias?dp:sp)->width*4),
            *di=pixman_image_create_bits(dc,dp->width,dp->height,dm,dp->width*4),
            *mi=pixman_image_create_bits(mc,(inputs_alias?dp:mp)->width,(inputs_alias?dp:mp)->height,mm,(inputs_alias?dp:mp)->width*4);assert(si && di && mi);
        if(alias){map.pDrawable=&xp.drawable;map_image=pixman_image_create_bits(map_code,dp->width,dp->height,dm,dp->width*4);assert(map_image);}
        else{
            if(PICT_FORMAT_R(map_code)==10 || map_code==PICT_x4r4g4b4){int pitch;unsigned bpp=PICT_FORMAT_R(map_code)==10?32:16,d=bpp==32?32:15;
                map_pixmap=calloc(1,sizeof(PixmapRec));assert(map_pixmap);map_pixmap->drawable=(DrawableRec){.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=d,.bitsPerPixel=bpp,.width=13,.height=7};
                map_pixmap->devPrivates=exa.driver.CreatePixmap2(&xs,13,7,d,0,bpp,&pitch);assert(map_pixmap->devPrivates);map_pixmap->devKind=pitch;
            }else map_pixmap=map_fixture_pixmap(map_code,variation>=14);
            map_fixture_seed(map_pixmap,layout*17+variation);
            if(PICT_FORMAT_R(map_code)==10){struct pixels *m=model(*(uint32_t*)map_pixmap->devPrivates);for(unsigned n=0;n<m->width*m->height;++n)m->data[n]|=(n%4)<<30;}
            map.pDrawable=&map_pixmap->drawable;map_image=map_fixture_image(map_pixmap,map_code,&map_memory);
        }
        map.format=map_code;map.transform=&pole;map.repeat=TRUE;map.repeatType=RepeatReflect;map.filter=PictFilterBilinear;
        assert(pixman_image_set_transform(map_image,(pixman_transform_t*)&pole));pixman_image_set_repeat(map_image,PIXMAN_REPEAT_REFLECT);assert(pixman_image_set_filter(map_image,PIXMAN_FILTER_BILINEAR,NULL,0));
        dest.alphaMap=&map;dest.alphaOrigin=(DDXPointRec){.x=alias?(variation&1?-1:1):1,.y=alias?(variation<2?1:-1):1};
        pixman_image_set_alpha_map(di,map_image,dest.alphaOrigin.x,dest.alphaOrigin.y);
        SourcePict procedural={0};PictGradientStop stops[]={{0,{11113,7711,17131,32767}},{32768,{17431,21431,8411,41311}},{65536,{9113,11731,43111,65535}}};
        if(mode==4){
            src.pDrawable=NULL;src.pSourcePict=&procedural;pixman_image_unref(si);
            if(!variation){xRenderColor c={11113,7711,17131,32767};procedural=(SourcePict){.solidFill={.type=SourcePictTypeSolidFill,.color=0x7f2b1e42,.fullcolor=c}};pixman_color_t colour={c.red,c.green,c.blue,c.alpha};si=pixman_image_create_solid_fill(&colour);}
            else{
                procedural=(SourcePict){.linear={.type=SourcePictTypeLinear,.nstops=3,.stops=stops,.p1={0,0},.p2={8*65536,3*65536}}};
                if(variation==2)procedural=(SourcePict){.radial={.type=SourcePictTypeRadial,.nstops=3,.stops=stops,.c1={0,0,32768},.c2={8*65536,3*65536,9*65536}}};
                if(variation==3)procedural=(SourcePict){.conical={.type=SourcePictTypeConical,.nstops=3,.stops=stops,.center={0,0},.angle=-37*65536}};
                pixman_gradient_stop_t rs[3];for(unsigned i=0;i<3;++i)rs[i]=(pixman_gradient_stop_t){.x=stops[i].x,.color={stops[i].color.red,stops[i].color.green,stops[i].color.blue,stops[i].color.alpha}};
                pixman_point_fixed_t a={0,0},b={8*65536,3*65536};si=variation==1?pixman_image_create_linear_gradient(&a,&b,rs,3):variation==2?pixman_image_create_radial_gradient(&a,&b,32768,9*65536,rs,3):pixman_image_create_conical_gradient(&a,-37*65536,rs,3);
            }
            assert(si);
        }
        for(unsigned i=0;i<2;++i){PicturePtr p=i?&mask:&src;pixman_image_t *r=i?mi:si;pixman_image_set_repeat(r,p->repeatType);
            if(p->transform)assert(pixman_image_set_transform(r,(pixman_transform_t*)p->transform));
            assert(pixman_image_set_filter(r,p->filter==PictFilterConvolution?PIXMAN_FILTER_CONVOLUTION:p->filter==PictFilterBilinear?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,p->filter_params,p->filter_nparams));}
        pixman_image_set_component_alpha(mi,mask.componentAlpha);
        unsigned masked=variation&2,width=alias?2:1,height=alias && !inputs_alias?2:1;
        pixman_image_composite32(packed10_all_ops[operation],si,masked?mi:NULL,di,2,1,3,2,3,2,width,height);
        compositor_active=1;
        if(!exa.driver.CheckComposite(packed10_all_ops[operation],&src,masked?&mask:NULL,&dest) ||
            !exa.driver.PrepareComposite(packed10_all_ops[operation],&src,masked?&mask:NULL,&dest,src.pDrawable?(inputs_alias?&xp:&source_pixmap):NULL,masked?(inputs_alias?&xp:&mask_pixmap):NULL,&xp)){
            fprintf(stderr,"packed10 alpha-write prepare mode=%u layout=%u map=%u op=%u errno=%d\n",mode,layout,variation,packed10_all_ops[operation],errno);abort();}
        fprintf(stderr,"packed10 alpha-write request mode=%u layout=%u map=%u op=%u\n",mode,layout,variation,packed10_all_ops[operation]);
        exa.driver.Composite(&xp,2,1,3,2,3,2,width,height);compositor_active=0;exa.driver.DoneComposite(&xp);
        unsigned pixel_mask=!alias && (dc==PICT_x2r10g10b10 || dc==PICT_x2b10g10r10)?0x3fffffff:UINT32_MAX;
        for(unsigned n=0;n<dp->width*dp->height;++n)if((dp->data[n]^((uint32_t*)dm)[n])&pixel_mask){fprintf(stderr,"packed10 alpha-write parent mode=%u layout=%u map=%u op=%u n=%u expected=%08x got=%08x\n",mode,layout,variation,packed10_all_ops[operation],n,((uint32_t*)dm)[n],dp->data[n]);abort();}
        if(map_pixmap){map_fixture_expect(map_pixmap,map_code,map_memory,layout,variation,mode);coverage_test_destroy(map_pixmap);}
        ++checks;pixman_image_unref(map_image);pixman_image_unref(si);pixman_image_unref(mi);pixman_image_unref(di);free(map_memory);free(sm);free(dm);free(mm);
    }
    printf("PS2 packed10 alpha-write mode=%u requests=%u PASS\n",mode,checks);
    assert(!ps2_surface_destroy(gs,mh));ps2_blit_exa_cleanup(&xs,&blit);ps2_exa_context_bridge_cleanup(&bridge);finish_rop();
}

static void packed10_dispatch_case(unsigned mode)
{
    unsigned mapped=mode>=16;if(mapped)mode-=16;
    unsigned convolution=!mapped && mode>=12;if(convolution)mode-=12;
    unsigned extended=mapped || convolution || mode>=6;if(extended && !convolution && !mapped)mode-=6;
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    assert(!ps2_surface_set_depth(gs,source_handle,32) && !ps2_surface_set_depth(gs,destination_handle(),32));xp.drawable.depth=32;
    struct ps2_exa_context_bridge bridge={0};struct ps2_texture_exa texture={0};struct ps2_blit_exa blit={0};
    assert(ps2_exa_context_bridge_prepare(&xs,&bridge,&exa,ctx));
    assert(ps2_texture_exa_prepare(&xs,&texture,&exa,ps2_pipe_context_render(ctx)));
    assert(ps2_blit_exa_prepare(&xs,&blit,&exa,ctx));
    uint32_t mh=0;assert(!ps2_surface_create(gs,31,23,32,&mh) && !ps2_surface_set_depth(gs,mh,32));
    struct pixels *sp=model(source_handle),*dp=model(destination_handle()),*mp=model(mh);
    PixmapRec source_pixmap={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=32,.bitsPerPixel=32,.width=sp->width,.height=sp->height},.devPrivates=(void*)&source_handle},
        mask_pixmap={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=32,.bitsPerPixel=32,.width=mp->width,.height=mp->height},.devPrivates=(void*)&mh};
    const PictFormatShort codes[]={PICT_a2r10g10b10,PICT_a2b10g10r10,PICT_x2r10g10b10,PICT_x2b10g10r10};
    const unsigned basic_ops[]={PictOpSrc,PictOpOver,PictOpClear,PictOpDst,PictOpInReverse,PictOpOutReverse,PictOpAdd,
        PictOpOverReverse,PictOpIn,PictOpOut,PictOpAtop,PictOpAtopReverse,PictOpXor};

    const unsigned *ops=extended?packed10_all_ops:basic_ops;unsigned nops=extended?53:13;
    PictTransform transform={.matrix={{49152,21845,173},{0,98304,-21845},{65536/19,0,65536}}};
    PictTransform pole={.matrix={{65536,0,0},{0,65536,0},{0,0,0}}};
    int32_t kernels[][6]={{65536,65536,65536},{131072,65536,98303,-32767},
        {131072,131072,INT32_MAX,INT32_MIN,257,65535},{65536,65536,0}};
    const unsigned kernel_sizes[]={3,4,6,3};
    const unsigned map_formats[]={PICT_a1,PICT_x4a4,PICT_a8,PICT_a8r8g8b8,PICT_b8g8r8a8,
        PICT_a2r10g10b10,PICT_a2b10g10r10,PICT_x2b10g10r10,PICT_a2r2g2b2,PICT_a4r4g4b4,PICT_r5g6b5,PICT_x4r4g4b4};
    unsigned checks=0;
    for(unsigned layout=0;layout<4;++layout)for(unsigned variation=0;variation<(mapped?(mode>=3?4U:9U):convolution?(mode==2?2U:mode==3?4U:3U):mode<2?3U:mode==4?3U:mode==5?2U:1U);++variation)
    for(unsigned operation=0;operation<nops;++operation){
        rgba_seed(sp);rgba_seed(dp);rgba_seed(mp);
        unsigned sc=codes[layout],dc=codes[(layout+1)%4],mc=codes[(layout+2)%4];
        if(!mapped && mode<2){if(variation==1)sc=PICT_a8b8g8r8;if(variation==2)dc=PICT_a8b8g8r8;}
        unsigned map_role=mapped?(mode==3?variation<2?variation+1:3:variation%3+1):0,
            map_index=layout*3+variation/3;
        if(mapped && mode==4){map_role=2;sc=dc=mc=PICT_a8r8g8b8;}
        if(mapped && mode!=3 && (((map_role&1) && map_index>=5 && map_index<=7) ||
            ((map_role&2) && (map_index+5)%12>=5 && (map_index+5)%12<=7)))sc=dc=mc=PICT_a8r8g8b8;
        PictureRec src={.pDrawable=&source_pixmap.drawable,.format=sc,.filter=mode==1?PictFilterBilinear:PictFilterNearest,.repeat=TRUE,.repeatType=RepeatReflect},
            mask={.pDrawable=&mask_pixmap.drawable,.format=mc,.filter=PictFilterBilinear,.repeat=TRUE,.repeatType=RepeatReflect,.componentAlpha=layout&1},
            dest={.pDrawable=&xp.drawable,.format=dc,.filter=PictFilterNearest};
        src.transform=mode==1?&transform:NULL;
        PicturePtr masked=mode>=2?&mask:NULL;
        unsigned source_alias=mapped?mode==3 && variation==3:convolution?mode==2 && !variation:mode==3,
            mask_alias=mapped?source_alias:convolution && mode==2 && variation;
        if(source_alias){src.pDrawable=&xp.drawable;masked=NULL;}
        if(mask_alias)mask.pDrawable=&xp.drawable;
        if(convolution){unsigned kernel=mode==3?variation:variation;
            src.filter=PictFilterConvolution;src.filter_params=kernels[kernel];src.filter_nparams=kernel_sizes[kernel];
            src.transform=&transform;src.repeatType=(layout+variation)%4;
            masked=mode==1 || mask_alias?&mask:NULL;
            if(masked){mask.filter=PictFilterConvolution;mask.filter_params=kernels[(kernel+1)%4];
                mask.filter_nparams=kernel_sizes[(kernel+1)%4];mask.transform=&transform;
                mask.repeatType=(layout+variation+1)%4;mask.componentAlpha=(layout+variation)&1;}
            if(mode==3){src.transform=variation==3?&pole:NULL;src.repeatType=layout;}
        }
        if(mapped){
            masked=map_role&2?&mask:NULL;
            src.filter=mask.filter=mode==2?PictFilterConvolution:mode==1?PictFilterBilinear:PictFilterNearest;
            src.transform=mask.transform=mode==3?NULL:&transform;src.repeatType=mask.repeatType=layout;
            if(mode==2){src.filter_params=mask.filter_params=kernels[1];src.filter_nparams=mask.filter_nparams=4;}
        }
        size_t sb=(size_t)(source_alias?dp:sp)->width*(source_alias?dp:sp)->height*4,db=(size_t)dp->width*dp->height*4,
            mb=(size_t)(mask_alias?dp:mp)->width*(mask_alias?dp:mp)->height*4;
        void *sm=malloc(sb),*dm=malloc(db),*mm=malloc(mb);assert(sm && dm && mm);
        memcpy(sm,(source_alias?dp:sp)->data,sb);memcpy(dm,dp->data,db);memcpy(mm,(mask_alias?dp:mp)->data,mb);
        pixman_image_t *si=pixman_image_create_bits((pixman_format_code_t)sc,(source_alias?dp:sp)->width,(source_alias?dp:sp)->height,sm,(source_alias?dp:sp)->width*4),
            *di=pixman_image_create_bits((pixman_format_code_t)dc,dp->width,dp->height,dm,dp->width*4),
            *mi=pixman_image_create_bits((pixman_format_code_t)mc,(mask_alias?dp:mp)->width,(mask_alias?dp:mp)->height,mm,(mask_alias?dp:mp)->width*4);assert(si && di && mi);
        SourcePict procedural={0},procedural_mask={0};PictGradientStop stops[]={{0,{11113,7711,17131,32767}},
            {32768,{17431,21431,8411,41311}},{65536,{9113,11731,43111,65535}}};
        if(!convolution && !mapped && mode==4){unsigned kind=variation+1;
            procedural=(SourcePict){.linear={.type=SourcePictTypeLinear,.nstops=3,.stops=stops,.p1={0,0},.p2={8*65536,3*65536}}};
            if(kind==2)procedural=(SourcePict){.radial={.type=SourcePictTypeRadial,.nstops=3,.stops=stops,.c1={0,0,32768},.c2={8*65536,3*65536,9*65536}}};
            if(kind==3)procedural=(SourcePict){.conical={.type=SourcePictTypeConical,.nstops=3,.stops=stops,.center={0,0},.angle=-37*65536}};
            procedural_mask=procedural;src.pDrawable=mask.pDrawable=NULL;src.pSourcePict=&procedural;mask.pSourcePict=&procedural_mask;
            src.transform=mask.transform=&transform;src.filter=mask.filter=PictFilterBilinear;
            pixman_gradient_stop_t rs[3];for(unsigned i=0;i<3;++i)rs[i]=(pixman_gradient_stop_t){.x=stops[i].x,
                .color={stops[i].color.red,stops[i].color.green,stops[i].color.blue,stops[i].color.alpha}};
            pixman_point_fixed_t a={0,0},b={8*65536,3*65536};pixman_image_unref(si);pixman_image_unref(mi);
            si=kind==1?pixman_image_create_linear_gradient(&a,&b,rs,3):kind==2?pixman_image_create_radial_gradient(&a,&b,32768,9*65536,rs,3):pixman_image_create_conical_gradient(&a,-37*65536,rs,3);
            mi=kind==1?pixman_image_create_linear_gradient(&a,&b,rs,3):kind==2?pixman_image_create_radial_gradient(&a,&b,32768,9*65536,rs,3):pixman_image_create_conical_gradient(&a,-37*65536,rs,3);assert(si && mi);
        }
        if(!convolution && !mapped && mode==5){
            xRenderColor a={11113,7711,17131,32767},b={17431,21431,8411,41311};
            procedural=(SourcePict){.solidFill={.type=SourcePictTypeSolidFill,.color=0x7f2b1e42,.fullcolor=a}};
            procedural_mask=(SourcePict){.solidFill={.type=SourcePictTypeSolidFill,.color=0xa1445320,.fullcolor=b}};
            src.pDrawable=mask.pDrawable=NULL;src.pSourcePict=&procedural;mask.pSourcePict=&procedural_mask;
            src.repeatType=mask.repeatType=RepeatNormal;src.filter=mask.filter=PictFilterNearest;src.transform=mask.transform=NULL;
            mask.componentAlpha=variation;
            pixman_color_t ac={a.red,a.green,a.blue,a.alpha},bc={b.red,b.green,b.blue,b.alpha};
            pixman_image_unref(si);pixman_image_unref(mi);si=pixman_image_create_solid_fill(&ac);mi=pixman_image_create_solid_fill(&bc);assert(si && mi);
        }
        if(mapped && mode==4){
            src.pDrawable=NULL;src.pSourcePict=&procedural;pixman_image_unref(si);
            if(!variation){xRenderColor colour={11113,7711,17131,32767};
                procedural=(SourcePict){.solidFill={.type=SourcePictTypeSolidFill,.color=0x7f2b1e42,.fullcolor=colour}};
                pixman_color_t c={colour.red,colour.green,colour.blue,colour.alpha};si=pixman_image_create_solid_fill(&c);
            }else{
                procedural=(SourcePict){.linear={.type=SourcePictTypeLinear,.nstops=3,.stops=stops,.p1={0,0},.p2={8*65536,3*65536}}};
                if(variation==2)procedural=(SourcePict){.radial={.type=SourcePictTypeRadial,.nstops=3,.stops=stops,.c1={0,0,32768},.c2={8*65536,3*65536,9*65536}}};
                if(variation==3)procedural=(SourcePict){.conical={.type=SourcePictTypeConical,.nstops=3,.stops=stops,.center={0,0},.angle=-37*65536}};
                pixman_gradient_stop_t rs[3];for(unsigned i=0;i<3;++i)rs[i]=(pixman_gradient_stop_t){.x=stops[i].x,
                    .color={stops[i].color.red,stops[i].color.green,stops[i].color.blue,stops[i].color.alpha}};
                pixman_point_fixed_t a={0,0},b={8*65536,3*65536};
                si=variation==1?pixman_image_create_linear_gradient(&a,&b,rs,3):variation==2?pixman_image_create_radial_gradient(&a,&b,32768,9*65536,rs,3):pixman_image_create_conical_gradient(&a,-37*65536,rs,3);
            }
            assert(si);
        }
        PixmapPtr map_pixmaps[2]={0};PictureRec map_pictures[2]={0};pixman_image_t *map_images[2]={0};void *map_memory[2]={0};
        for(unsigned i=0;mapped && i<2;++i){
            unsigned code=mode>=3?codes[(layout+i+variation)%4]:map_formats[(map_index+i*5)%12];
            if(mode==3){map_pictures[i]=(PictureRec){.pDrawable=&xp.drawable,.format=code};
                map_memory[i]=malloc(db);assert(map_memory[i]);memcpy(map_memory[i],dp->data,db);
                map_images[i]=pixman_image_create_bits(code,dp->width,dp->height,map_memory[i],dp->width*4);assert(map_images[i]);
            }else{
                if(PICT_FORMAT_R(code)==10 || code==PICT_x4r4g4b4){int pitch;unsigned bpp=PICT_FORMAT_R(code)==10?32:16,d=PICT_FORMAT_R(code)==10?32:15;
                    map_pixmaps[i]=calloc(1,sizeof(PixmapRec));assert(map_pixmaps[i]);
                    map_pixmaps[i]->drawable=(DrawableRec){.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=d,.bitsPerPixel=bpp,.width=13,.height=7};
                    map_pixmaps[i]->devPrivates=exa.driver.CreatePixmap2(&xs,13,7,d,0,bpp,&pitch);assert(map_pixmaps[i]->devPrivates);map_pixmaps[i]->devKind=pitch;
                }else map_pixmaps[i]=map_fixture_pixmap(code,0);
                map_fixture_seed(map_pixmaps[i],layout+variation+i*7);
                if(PICT_FORMAT_R(code)==10){struct pixels *m=model(*(uint32_t*)map_pixmaps[i]->devPrivates);
                    for(unsigned n=0;n<m->width*m->height;++n)m->data[n]|=((n+variation+i)%4)<<30;}
                map_pictures[i]=(PictureRec){.pDrawable=&map_pixmaps[i]->drawable,.format=code};
                map_images[i]=map_fixture_image(map_pixmaps[i],code,map_memory+i);
            }
            map_pictures[i].filter=PictFilterBilinear;map_pictures[i].transform=&pole;map_pictures[i].repeat=TRUE;map_pictures[i].repeatType=RepeatReflect;
            assert(pixman_image_set_transform(map_images[i],(pixman_transform_t*)&pole));pixman_image_set_repeat(map_images[i],PIXMAN_REPEAT_REFLECT);
            assert(pixman_image_set_filter(map_images[i],PIXMAN_FILTER_BILINEAR,NULL,0));
            PicturePtr parent=i?&mask:&src;pixman_image_t *reference=i?mi:si;
            parent->alphaOrigin=(DDXPointRec){.x=mode==3?1:(variation%3==0?2:-1),.y=mode==3?0:1};
            if(map_role&(1U<<i)){parent->alphaMap=map_pictures+i;
                pixman_image_set_alpha_map(reference,map_images[i],parent->alphaOrigin.x,parent->alphaOrigin.y);}
        }
        if(mapped && mode!=3){map_pictures[0].alphaMap=map_pictures+1;
            pixman_image_set_alpha_map(map_images[0],map_images[1],7,-3);}
        pixman_image_set_repeat(si,src.repeatType);pixman_image_set_repeat(mi,mask.repeatType);pixman_image_set_component_alpha(mi,mask.componentAlpha);
        if(src.transform)assert(pixman_image_set_transform(si,(pixman_transform_t*)src.transform));
        if(mask.transform)assert(pixman_image_set_transform(mi,(pixman_transform_t*)mask.transform));
        assert(pixman_image_set_filter(si,src.filter==PictFilterConvolution?PIXMAN_FILTER_CONVOLUTION:src.filter==PictFilterBilinear?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,src.filter_params,src.filter_nparams));
        assert(pixman_image_set_filter(mi,mask.filter==PictFilterConvolution?PIXMAN_FILTER_CONVOLUTION:mask.filter==PictFilterBilinear?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,mask.filter_params,mask.filter_nparams));
        unsigned width=mapped?(mode==3?2:1):convolution?(source_alias || mask_alias?2:1):mode==3?4:1;
        int sx=mapped && mode==3?3:source_alias?1:convolution && mode==3?-1:2,sy=mapped && mode==3?2:convolution && mode==3?-1:1;
        pixman_image_composite32(ops[operation],si,masked?mi:NULL,di,sx,sy,3,2,3,2,width,1);
        compositor_active=1;
        if(!exa.driver.CheckComposite(ops[operation],&src,masked,&dest) ||
            !exa.driver.PrepareComposite(ops[operation],&src,masked,&dest,src.pDrawable?(source_alias?&xp:&source_pixmap):NULL,
                masked && mask.pDrawable?(mask_alias?&xp:&mask_pixmap):NULL,&xp)){
            fprintf(stderr,"packed10 prepare mode=%u format=%u variation=%u op=%u errno=%d\n",mode,layout,variation,ops[operation],errno);abort();}
        if(extended)fprintf(stderr,"packed10 request mode=%u layout=%u variation=%u op=%u private=%u\n",mode,layout,variation,ops[operation],blit.over);
        exa.driver.Composite(&xp,sx,sy,3,2,3,2,width,1);compositor_active=0;exa.driver.DoneComposite(&xp);
        uint32_t *wanted=dm;unsigned pixel_mask=(dc==PICT_x2r10g10b10 || dc==PICT_x2b10g10r10)?0x3fffffff:UINT32_MAX;
        for(unsigned i=0;i<dp->width*dp->height;++i)if((dp->data[i]^wanted[i])&pixel_mask){
            fprintf(stderr,"packed10 dispatch mode=%u format=%u variation=%u op=%u i=%u expected=%08x actual=%08x\n",mode,layout,variation,ops[operation],i,wanted[i],dp->data[i]);abort();}
        ++checks;pixman_image_unref(si);pixman_image_unref(mi);pixman_image_unref(di);free(sm);free(mm);free(dm);
        for(unsigned i=0;mapped && i<2;++i){
            if(map_pixmaps[i]){map_fixture_expect(map_pixmaps[i],map_pictures[i].format,map_memory[i],layout,variation,mode);coverage_test_destroy(map_pixmaps[i]);}
            pixman_image_unref(map_images[i]);free(map_memory[i]);
        }
    }
    printf("PS2 packed10 EXA mode=%u extended=%u%s%s requests=%u PASS\n",mode,extended,convolution?" convolution=1":"",mapped?" alpha_read=1":"",checks);
    assert(!ps2_surface_destroy(gs,mh));ps2_blit_exa_cleanup(&xs,&blit);ps2_exa_context_bridge_cleanup(&bridge);finish_rop();
}

static void coverage_strips_case(void)
{
    const unsigned sizes[][2]={{1,1},{7,31},{31,32},{32,33},{33,100},{65,33},{150,100}};
    const unsigned depths[]={1,4,8,12,16};
    rgba_enabled=1;setup_rop(32);
    struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    unsigned scenarios=0;
    for(unsigned d=0;d<5;++d)for(unsigned k=0;k<7;++k)
    for(unsigned msb=0;msb<(depths[d]==1?2U:1U);++msb)
    for(unsigned core=0;core<2;++core){
        unsigned depth=depths[d],bpp=depth==1?1:depth<=8?8:16;
        if(!core && bpp==16)continue;
        unsigned w=sizes[k][0],h=sizes[k][1],row=bpp==1?(w+7)/8:w*(bpp/8),stride=row+7;
        unsigned char *input=malloc(stride*h),*output=malloc(stride*h),*saved=malloc(stride*h);
        assert(input && output && saved);
        for(unsigned i=0;i<stride*h;++i)input[i]=i*73U+k*19U+d*37U+11U;
        memcpy(saved,input,stride*h);memset(output,0xa5,stride*h);
        struct ps2_picture_bitmap bitmap={.bits=input,.bytes=stride*h,.stride=stride,
            .width=w,.height=h,.bits_per_pixel=bpp,.alpha_bits=depth,.msb_first=msb};
        struct ps2_coverage_surface c={0};unsigned before=native_ioctl_calls;
        assert(!(core?ps2_coverage_decode_core(&c,gs,&bitmap):ps2_coverage_decode(&c,gs,&bitmap)));
        struct pixels *pixels=model(c.surface);
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
            const unsigned char *p=input+y*stride;
            unsigned v=bpp==1?(p[x/8]>>(msb?7-x%8:x%8))&1:bpp==8?p[x]:p[x*2]|p[x*2+1]<<8;
            v&=(1U<<depth)-1;
            uint32_t expected=core?v:(depth==1?v*255:depth==4?v*17:v)<<24;
            assert(pixels->data[y*w+x]==expected);
        }
        assert(!ps2_coverage_encode(&c,ctx,output,stride*h,stride));
        assert(!memcmp(input,saved,stride*h));
        for(unsigned y=0;y<h;++y){
            assert(!memcmp(input+y*stride,output+y*stride,row));
            for(unsigned x=row;x<stride;++x)assert(output[y*stride+x]==0xa5);
        }
        unsigned submissions=native_ioctl_calls-before;
        if(w==150 && h==100 && depth==1){
            printf("coverage strips core=%u msb=%u submissions=%u\n",core,msb,submissions);
            assert(submissions<3500);
        }
        assert(!ps2_coverage_release(&c));free(input);free(output);free(saved);++scenarios;
    }
    printf("coverage strip scenarios=%u PASS\n",scenarios);finish_rop();
}
int main(int argc,char **argv)
{
    if(argc==2 && !strcmp(argv[1],"coverage-strips")){coverage_strips_case();return 0;}
    if(argc==2 && !strcmp(argv[1],"borrowed-byte-glyphs")){borrowed_colour_case(0);return 0;}
    if(argc==2 && !strcmp(argv[1],"borrowed-word-glyphs")){borrowed_colour_case(1);return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-alpha-write-guards")){packed10_alpha_write_guards_case();return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-alpha-write")){packed10_alpha_write_case(0);return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-alpha-write-bilinear")){packed10_alpha_write_case(1);return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-alpha-write-convolution")){packed10_alpha_write_case(2);return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-alpha-write-alias")){packed10_alpha_write_case(3);return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-alpha-write-procedural")){packed10_alpha_write_case(4);return 0;}

    if(argc==2 && !strcmp(argv[1],"packed10-alpha-read-guards")){packed10_alpha_read_guards_case();return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-alpha-read")){packed10_dispatch_case(16);return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-alpha-read-bilinear")){packed10_dispatch_case(17);return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-alpha-read-convolution")){packed10_dispatch_case(18);return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-alpha-read-alias")){packed10_dispatch_case(19);return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-alpha-read-procedural")){packed10_dispatch_case(20);return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-convolution-preflight")){packed10_convolution_preflight_case();return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-convolution-float")){packed10_convolution_float_case(0);return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-convolution-guards")){packed10_convolution_float_case(1);return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-convolution")){packed10_dispatch_case(12);return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-convolution-masks")){packed10_dispatch_case(13);return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-convolution-alias")){packed10_dispatch_case(14);return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-convolution-edges")){packed10_dispatch_case(15);return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-operator-guards")){packed10_operator_guards_case();return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-operators")){packed10_dispatch_case(6);return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-operators-bilinear")){packed10_dispatch_case(7);return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-operators-masks")){packed10_dispatch_case(8);return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-operators-alias")){packed10_dispatch_case(9);return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-operators-gradients")){packed10_dispatch_case(10);return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-operators-solids")){packed10_dispatch_case(11);return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-quantize")){packed10_quantize_case();return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-guards")){packed10_guards_case();return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-basic")){packed10_dispatch_case(0);return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-bilinear")){packed10_dispatch_case(1);return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-masks")){packed10_dispatch_case(2);return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-alias")){packed10_dispatch_case(3);return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-gradients")){packed10_dispatch_case(4);return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-solids")){packed10_dispatch_case(5);return 0;}
    if(argc==2 && !strcmp(argv[1],"packed10-codec")){packed10_codec_case();return 0;}
    if(argc==2 && !strcmp(argv[1],"gradient-guards")){gradient_guards_case();return 0;}
    if(argc==2 && !strcmp(argv[1],"gradient-opacity")){for(unsigned k=1;k<=3;++k)gradient_dispatch_case(1,k,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"gradient-opacity-convolution")){for(unsigned k=1;k<=3;++k)gradient_dispatch_case(1,k,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"gradient-linear-degenerate")){gradient_linear_case(1,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"gradient-radial-degenerate")){gradient_linear_case(2,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"gradient-radial-contained")){gradient_linear_case(2,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"gradient-radial-empty")){gradient_linear_case(2,3);return 0;}
    if(argc==2 && !strcmp(argv[1],"gradient-conical-centre")){gradient_linear_case(3,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"gradient-radial")){gradient_linear_case(2,0);return 0;}
    if(argc==2 && !strcmp(argv[1],"gradient-conical")){gradient_linear_case(3,0);return 0;}
    if(argc==2 && !strcmp(argv[1],"gradient-radial-dispatch-narrow")){gradient_dispatch_case(0,2,0);return 0;}
    if(argc==2 && !strcmp(argv[1],"gradient-radial-dispatch-wide")){gradient_dispatch_case(1,2,0);gradient_packed_case(2);return 0;}
    if(argc==2 && !strcmp(argv[1],"gradient-conical-dispatch-narrow")){gradient_dispatch_case(0,3,0);return 0;}
    if(argc==2 && !strcmp(argv[1],"gradient-conical-dispatch-wide")){gradient_dispatch_case(1,3,0);gradient_packed_case(3);return 0;}
    if(argc==2 && !strcmp(argv[1],"gradient-linear")){gradient_linear_case(1,0);return 0;}
    if(argc==2 && !strcmp(argv[1],"gradient-byte")){gradient_byte_case();return 0;}
    if(argc==2 && !strcmp(argv[1],"gradient-dispatch-narrow")){gradient_dispatch_case(0,1,0);return 0;}
    if(argc==2 && !strcmp(argv[1],"gradient-dispatch-wide")){gradient_dispatch_case(1,1,0);gradient_packed_case(1);return 0;}
    if(argc==2 && !strcmp(argv[1],"float-tiny")){tiny_arithmetic_case();return 0;}
    if(argc==2 && !strcmp(argv[1],"float-routing")){float_routing_case(1);return 0;}
    if(argc==2 && !strcmp(argv[1],"float-routing-compare")){float_routing_case(0);return 0;}
    if(argc==2 && !strcmp(argv[1],"float-routing-guards")){float_routing_case(2);return 0;}

    if(argc==2 && !strcmp(argv[1],"hsl-boundaries")){hsl_register_case(1);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-tiny")){hsl_register_case(2);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-guards")){hsl_guards_case();return 0;}

    if(argc==2 && !strcmp(argv[1],"hsl-registers")){hsl_register_case(0);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-hue-rgba")){rgba_case("hsl-hue-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-hue-bgra")){rgba_case("hsl-hue-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-hue-opaque")){rgba_case("hsl-hue-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-hue-native555")){component_alpha_case(16,PS2_COMPOSITE_HSL_HUE);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-hue-byte")){byte_colour_exa(0,149);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-hue-word")){byte_colour_exa(1,149);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-hue-coverage")){coverage_cross_case("hsl-hue-coverage-from-colour-argb");coverage_cross_case("hsl-hue-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-hue-map")){alpha_maps_case("hsl-hue-alpha-map-combined");alpha_maps_case("hsl-hue-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-hue-alias-byte")){format_alias_case(0,149,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-hue-alias-word")){format_alias_case(1,149,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-hue-alias-native15")){format_alias_case(2,149,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-hue-solid")){blend_solid_case(PictOpHSLHue);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-hue-solid16-native")){solid_disjoint_case(0,PictOpHSLHue);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-hue-solid16-packed")){solid_disjoint_case(1,PictOpHSLHue);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-hue-solid16-coverage")){solid_disjoint_case(2,PictOpHSLHue);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-saturation-rgba")){rgba_case("hsl-saturation-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-saturation-bgra")){rgba_case("hsl-saturation-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-saturation-opaque")){rgba_case("hsl-saturation-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-saturation-native555")){component_alpha_case(16,PS2_COMPOSITE_HSL_SATURATION);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-saturation-byte")){byte_colour_exa(0,150);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-saturation-word")){byte_colour_exa(1,150);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-saturation-coverage")){coverage_cross_case("hsl-saturation-coverage-from-colour-argb");coverage_cross_case("hsl-saturation-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-saturation-map")){alpha_maps_case("hsl-saturation-alpha-map-combined");alpha_maps_case("hsl-saturation-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-saturation-alias-byte")){format_alias_case(0,150,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-saturation-alias-word")){format_alias_case(1,150,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-saturation-alias-native15")){format_alias_case(2,150,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-saturation-solid")){blend_solid_case(PictOpHSLSaturation);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-saturation-solid16-native")){solid_disjoint_case(0,PictOpHSLSaturation);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-saturation-solid16-packed")){solid_disjoint_case(1,PictOpHSLSaturation);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-saturation-solid16-coverage")){solid_disjoint_case(2,PictOpHSLSaturation);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-color-rgba")){rgba_case("hsl-color-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-color-bgra")){rgba_case("hsl-color-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-color-opaque")){rgba_case("hsl-color-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-color-native555")){component_alpha_case(16,PS2_COMPOSITE_HSL_COLOR);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-color-byte")){byte_colour_exa(0,151);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-color-word")){byte_colour_exa(1,151);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-color-coverage")){coverage_cross_case("hsl-color-coverage-from-colour-argb");coverage_cross_case("hsl-color-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-color-map")){alpha_maps_case("hsl-color-alpha-map-combined");alpha_maps_case("hsl-color-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-color-alias-byte")){format_alias_case(0,151,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-color-alias-word")){format_alias_case(1,151,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-color-alias-native15")){format_alias_case(2,151,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-color-solid")){blend_solid_case(PictOpHSLColor);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-color-solid16-native")){solid_disjoint_case(0,PictOpHSLColor);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-color-solid16-packed")){solid_disjoint_case(1,PictOpHSLColor);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-color-solid16-coverage")){solid_disjoint_case(2,PictOpHSLColor);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-luminosity-rgba")){rgba_case("hsl-luminosity-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-luminosity-bgra")){rgba_case("hsl-luminosity-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-luminosity-opaque")){rgba_case("hsl-luminosity-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-luminosity-native555")){component_alpha_case(16,PS2_COMPOSITE_HSL_LUMINOSITY);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-luminosity-byte")){byte_colour_exa(0,152);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-luminosity-word")){byte_colour_exa(1,152);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-luminosity-coverage")){coverage_cross_case("hsl-luminosity-coverage-from-colour-argb");coverage_cross_case("hsl-luminosity-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-luminosity-map")){alpha_maps_case("hsl-luminosity-alpha-map-combined");alpha_maps_case("hsl-luminosity-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-luminosity-alias-byte")){format_alias_case(0,152,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-luminosity-alias-word")){format_alias_case(1,152,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-luminosity-alias-native15")){format_alias_case(2,152,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-luminosity-solid")){blend_solid_case(PictOpHSLLuminosity);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-luminosity-solid16-native")){solid_disjoint_case(0,PictOpHSLLuminosity);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-luminosity-solid16-packed")){solid_disjoint_case(1,PictOpHSLLuminosity);return 0;}
    if(argc==2 && !strcmp(argv[1],"hsl-luminosity-solid16-coverage")){solid_disjoint_case(2,PictOpHSLLuminosity);return 0;}

    if(argc==2 && !strcmp(argv[1],"softlight-guards")){softlight_guards_case();return 0;}
    if(argc==2 && !strcmp(argv[1],"softlight-registers")){softlight_register_case();return 0;}
    if(argc==2 && !strcmp(argv[1],"softlight-rgba")){rgba_case("softlight-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"softlight-bgra")){rgba_case("softlight-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"softlight-opaque")){rgba_case("softlight-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"softlight-native555")){component_alpha_case(16,PS2_COMPOSITE_SOFTLIGHT);return 0;}
    if(argc==2 && !strcmp(argv[1],"softlight-byte")){byte_colour_exa(0,148);return 0;}
    if(argc==2 && !strcmp(argv[1],"softlight-word")){byte_colour_exa(1,148);return 0;}
    if(argc==2 && !strcmp(argv[1],"softlight-coverage")){coverage_cross_case("softlight-coverage-from-colour-argb");coverage_cross_case("softlight-coverage-ca-from-colour-argb");coverage_cross_case("softlight-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"softlight-map")){alpha_maps_case("softlight-alpha-map-combined");alpha_maps_case("softlight-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"softlight-alias-byte")){format_alias_case(0,148,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"softlight-alias-word")){format_alias_case(1,148,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"softlight-alias-native15")){format_alias_case(2,148,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"softlight-solid")){blend_solid_case(PictOpSoftLight);return 0;}
    if(argc==2 && !strcmp(argv[1],"softlight-solid16-native")){solid_disjoint_case(0,PictOpSoftLight);return 0;}
    if(argc==2 && !strcmp(argv[1],"softlight-solid16-packed")){solid_disjoint_case(1,PictOpSoftLight);return 0;}
    if(argc==2 && !strcmp(argv[1],"softlight-solid16-coverage")){solid_disjoint_case(2,PictOpSoftLight);return 0;}

    if(argc==2 && !strcmp(argv[1],"float-sqrt")){softlight_arithmetic_case(0);return 0;}
    if(argc==2 && !strcmp(argv[1],"float-signed")){softlight_arithmetic_case(1);return 0;}
    if(argc==2 && !strcmp(argv[1],"float-softlight-guards")){softlight_arithmetic_case(2);return 0;}
    if(argc==2 && !strcmp(argv[1],"dodge-burn-registers")){dodge_burn_register_case();return 0;}
    if(argc==2 && !strcmp(argv[1],"dodge-burn-guards")){dodge_burn_guards_case();return 0;}
    if(argc==2 && !strcmp(argv[1],"float-branches")){float_branch_case(0);return 0;}
    if(argc==2 && !strcmp(argv[1],"float-branch-guards")){float_branch_case(1);return 0;}
    if(argc==2 && !strcmp(argv[1],"dodge-rgba")){rgba_case("dodge-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"dodge-bgra")){rgba_case("dodge-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"dodge-opaque")){rgba_case("dodge-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"dodge-native555")){component_alpha_case(16,PS2_COMPOSITE_COLOR_DODGE);return 0;}
    if(argc==2 && !strcmp(argv[1],"dodge-byte")){byte_colour_exa(0,146);return 0;}
    if(argc==2 && !strcmp(argv[1],"dodge-word")){byte_colour_exa(1,146);return 0;}
    if(argc==2 && !strcmp(argv[1],"dodge-coverage")){coverage_cross_case("dodge-coverage-from-colour-argb");coverage_cross_case("dodge-coverage-ca-from-colour-argb");coverage_cross_case("dodge-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"dodge-map")){alpha_maps_case("dodge-alpha-map-combined");alpha_maps_case("dodge-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"dodge-alias-byte")){format_alias_case(0,146,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"dodge-alias-word")){format_alias_case(1,146,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"dodge-alias-native15")){format_alias_case(2,146,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"dodge-solid")){blend_solid_case(PictOpColorDodge);return 0;}
    if(argc==2 && !strcmp(argv[1],"dodge-solid16-native")){solid_disjoint_case(0,PictOpColorDodge);return 0;}
    if(argc==2 && !strcmp(argv[1],"dodge-solid16-packed")){solid_disjoint_case(1,PictOpColorDodge);return 0;}
    if(argc==2 && !strcmp(argv[1],"dodge-solid16-coverage")){solid_disjoint_case(2,PictOpColorDodge);return 0;}
    if(argc==2 && !strcmp(argv[1],"burn-rgba")){rgba_case("burn-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"burn-bgra")){rgba_case("burn-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"burn-opaque")){rgba_case("burn-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"burn-native555")){component_alpha_case(16,PS2_COMPOSITE_COLOR_BURN);return 0;}
    if(argc==2 && !strcmp(argv[1],"burn-byte")){byte_colour_exa(0,147);return 0;}
    if(argc==2 && !strcmp(argv[1],"burn-word")){byte_colour_exa(1,147);return 0;}
    if(argc==2 && !strcmp(argv[1],"burn-coverage")){coverage_cross_case("burn-coverage-from-colour-argb");coverage_cross_case("burn-coverage-ca-from-colour-argb");coverage_cross_case("burn-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"burn-map")){alpha_maps_case("burn-alpha-map-combined");alpha_maps_case("burn-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"burn-alias-byte")){format_alias_case(0,147,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"burn-alias-word")){format_alias_case(1,147,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"burn-alias-native15")){format_alias_case(2,147,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"burn-solid")){blend_solid_case(PictOpColorBurn);return 0;}
    if(argc==2 && !strcmp(argv[1],"burn-solid16-native")){solid_disjoint_case(0,PictOpColorBurn);return 0;}
    if(argc==2 && !strcmp(argv[1],"burn-solid16-packed")){solid_disjoint_case(1,PictOpColorBurn);return 0;}
    if(argc==2 && !strcmp(argv[1],"burn-solid16-coverage")){solid_disjoint_case(2,PictOpColorBurn);return 0;}

    if(argc==2 && !strcmp(argv[1],"pd-ratio-guards")){pd_ratio_guards_case();return 0;}
    if(argc==2 && !strcmp(argv[1],"djover-rgba")){rgba_case("djover-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"djover-bgra")){rgba_case("djover-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"djover-opaque")){rgba_case("djover-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"djover-native555")){component_alpha_case(16,PS2_COMPOSITE_DISJOINT_OVER);return 0;}
    if(argc==2 && !strcmp(argv[1],"djover-byte")){byte_colour_exa(0,129);return 0;}
    if(argc==2 && !strcmp(argv[1],"djover-word")){byte_colour_exa(1,129);return 0;}
    if(argc==2 && !strcmp(argv[1],"djover-coverage")){coverage_cross_case("djover-coverage-from-colour-argb");coverage_cross_case("djover-coverage-ca-from-colour-argb");coverage_cross_case("djover-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"djover-map")){alpha_maps_case("djover-alpha-map-combined");alpha_maps_case("djover-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"djover-alias-byte")){format_alias_case(0,129,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"djover-alias-word")){format_alias_case(1,129,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"djover-alias-native15")){format_alias_case(2,129,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"djover-solid")){blend_solid_case(PictOpDisjointOver);return 0;}
    if(argc==2 && !strcmp(argv[1],"djin-rgba")){rgba_case("djin-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"djin-bgra")){rgba_case("djin-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"djin-opaque")){rgba_case("djin-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"djin-native555")){component_alpha_case(16,PS2_COMPOSITE_DISJOINT_IN);return 0;}
    if(argc==2 && !strcmp(argv[1],"djin-byte")){byte_colour_exa(0,130);return 0;}
    if(argc==2 && !strcmp(argv[1],"djin-word")){byte_colour_exa(1,130);return 0;}
    if(argc==2 && !strcmp(argv[1],"djin-coverage")){coverage_cross_case("djin-coverage-from-colour-argb");coverage_cross_case("djin-coverage-ca-from-colour-argb");coverage_cross_case("djin-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"djin-map")){alpha_maps_case("djin-alpha-map-combined");alpha_maps_case("djin-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"djin-alias-byte")){format_alias_case(0,130,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"djin-alias-word")){format_alias_case(1,130,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"djin-alias-native15")){format_alias_case(2,130,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"djin-solid")){blend_solid_case(PictOpDisjointIn);return 0;}
    if(argc==2 && !strcmp(argv[1],"djinrev-rgba")){rgba_case("djinrev-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"djinrev-bgra")){rgba_case("djinrev-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"djinrev-opaque")){rgba_case("djinrev-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"djinrev-native555")){component_alpha_case(16,PS2_COMPOSITE_DISJOINT_IN_REVERSE);return 0;}
    if(argc==2 && !strcmp(argv[1],"djinrev-byte")){byte_colour_exa(0,131);return 0;}
    if(argc==2 && !strcmp(argv[1],"djinrev-word")){byte_colour_exa(1,131);return 0;}
    if(argc==2 && !strcmp(argv[1],"djinrev-coverage")){coverage_cross_case("djinrev-coverage-from-colour-argb");coverage_cross_case("djinrev-coverage-ca-from-colour-argb");coverage_cross_case("djinrev-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"djinrev-map")){alpha_maps_case("djinrev-alpha-map-combined");alpha_maps_case("djinrev-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"djinrev-alias-byte")){format_alias_case(0,131,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"djinrev-alias-word")){format_alias_case(1,131,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"djinrev-alias-native15")){format_alias_case(2,131,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"djinrev-solid")){blend_solid_case(PictOpDisjointInReverse);return 0;}
    if(argc==2 && !strcmp(argv[1],"djout-rgba")){rgba_case("djout-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"djout-bgra")){rgba_case("djout-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"djout-opaque")){rgba_case("djout-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"djout-native555")){component_alpha_case(16,PS2_COMPOSITE_DISJOINT_OUT);return 0;}
    if(argc==2 && !strcmp(argv[1],"djout-byte")){byte_colour_exa(0,132);return 0;}
    if(argc==2 && !strcmp(argv[1],"djout-word")){byte_colour_exa(1,132);return 0;}
    if(argc==2 && !strcmp(argv[1],"djout-coverage")){coverage_cross_case("djout-coverage-from-colour-argb");coverage_cross_case("djout-coverage-ca-from-colour-argb");coverage_cross_case("djout-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"djout-map")){alpha_maps_case("djout-alpha-map-combined");alpha_maps_case("djout-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"djout-alias-byte")){format_alias_case(0,132,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"djout-alias-word")){format_alias_case(1,132,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"djout-alias-native15")){format_alias_case(2,132,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"djout-solid")){blend_solid_case(PictOpDisjointOut);return 0;}
    if(argc==2 && !strcmp(argv[1],"djoutrev-rgba")){rgba_case("djoutrev-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"djoutrev-bgra")){rgba_case("djoutrev-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"djoutrev-opaque")){rgba_case("djoutrev-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"djoutrev-native555")){component_alpha_case(16,PS2_COMPOSITE_DISJOINT_OUT_REVERSE);return 0;}
    if(argc==2 && !strcmp(argv[1],"djoutrev-byte")){byte_colour_exa(0,133);return 0;}
    if(argc==2 && !strcmp(argv[1],"djoutrev-word")){byte_colour_exa(1,133);return 0;}
    if(argc==2 && !strcmp(argv[1],"djoutrev-coverage")){coverage_cross_case("djoutrev-coverage-from-colour-argb");coverage_cross_case("djoutrev-coverage-ca-from-colour-argb");coverage_cross_case("djoutrev-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"djoutrev-map")){alpha_maps_case("djoutrev-alpha-map-combined");alpha_maps_case("djoutrev-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"djoutrev-alias-byte")){format_alias_case(0,133,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"djoutrev-alias-word")){format_alias_case(1,133,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"djoutrev-alias-native15")){format_alias_case(2,133,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"djoutrev-solid")){blend_solid_case(PictOpDisjointOutReverse);return 0;}
    if(argc==2 && !strcmp(argv[1],"djatop-rgba")){rgba_case("djatop-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"djatop-bgra")){rgba_case("djatop-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"djatop-opaque")){rgba_case("djatop-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"djatop-native555")){component_alpha_case(16,PS2_COMPOSITE_DISJOINT_ATOP);return 0;}
    if(argc==2 && !strcmp(argv[1],"djatop-byte")){byte_colour_exa(0,134);return 0;}
    if(argc==2 && !strcmp(argv[1],"djatop-word")){byte_colour_exa(1,134);return 0;}
    if(argc==2 && !strcmp(argv[1],"djatop-coverage")){coverage_cross_case("djatop-coverage-from-colour-argb");coverage_cross_case("djatop-coverage-ca-from-colour-argb");coverage_cross_case("djatop-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"djatop-map")){alpha_maps_case("djatop-alpha-map-combined");alpha_maps_case("djatop-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"djatop-alias-byte")){format_alias_case(0,134,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"djatop-alias-word")){format_alias_case(1,134,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"djatop-alias-native15")){format_alias_case(2,134,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"djatop-solid")){blend_solid_case(PictOpDisjointAtop);return 0;}
    if(argc==2 && !strcmp(argv[1],"djatoprev-rgba")){rgba_case("djatoprev-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"djatoprev-bgra")){rgba_case("djatoprev-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"djatoprev-opaque")){rgba_case("djatoprev-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"djatoprev-native555")){component_alpha_case(16,PS2_COMPOSITE_DISJOINT_ATOP_REVERSE);return 0;}
    if(argc==2 && !strcmp(argv[1],"djatoprev-byte")){byte_colour_exa(0,135);return 0;}
    if(argc==2 && !strcmp(argv[1],"djatoprev-word")){byte_colour_exa(1,135);return 0;}
    if(argc==2 && !strcmp(argv[1],"djatoprev-coverage")){coverage_cross_case("djatoprev-coverage-from-colour-argb");coverage_cross_case("djatoprev-coverage-ca-from-colour-argb");coverage_cross_case("djatoprev-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"djatoprev-map")){alpha_maps_case("djatoprev-alpha-map-combined");alpha_maps_case("djatoprev-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"djatoprev-alias-byte")){format_alias_case(0,135,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"djatoprev-alias-word")){format_alias_case(1,135,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"djatoprev-alias-native15")){format_alias_case(2,135,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"djatoprev-solid")){blend_solid_case(PictOpDisjointAtopReverse);return 0;}
    if(argc==2 && !strcmp(argv[1],"djxor-rgba")){rgba_case("djxor-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"djxor-bgra")){rgba_case("djxor-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"djxor-opaque")){rgba_case("djxor-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"djxor-native555")){component_alpha_case(16,PS2_COMPOSITE_DISJOINT_XOR);return 0;}
    if(argc==2 && !strcmp(argv[1],"djxor-byte")){byte_colour_exa(0,136);return 0;}
    if(argc==2 && !strcmp(argv[1],"djxor-word")){byte_colour_exa(1,136);return 0;}
    if(argc==2 && !strcmp(argv[1],"djxor-coverage")){coverage_cross_case("djxor-coverage-from-colour-argb");coverage_cross_case("djxor-coverage-ca-from-colour-argb");coverage_cross_case("djxor-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"djxor-map")){alpha_maps_case("djxor-alpha-map-combined");alpha_maps_case("djxor-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"djxor-alias-byte")){format_alias_case(0,136,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"djxor-alias-word")){format_alias_case(1,136,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"djxor-alias-native15")){format_alias_case(2,136,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"djxor-solid")){blend_solid_case(PictOpDisjointXor);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjover-rgba")){rgba_case("cjover-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjover-bgra")){rgba_case("cjover-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjover-opaque")){rgba_case("cjover-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjover-native555")){component_alpha_case(16,PS2_COMPOSITE_CONJOINT_OVER);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjover-byte")){byte_colour_exa(0,137);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjover-word")){byte_colour_exa(1,137);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjover-coverage")){coverage_cross_case("cjover-coverage-from-colour-argb");coverage_cross_case("cjover-coverage-ca-from-colour-argb");coverage_cross_case("cjover-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjover-map")){alpha_maps_case("cjover-alpha-map-combined");alpha_maps_case("cjover-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjover-alias-byte")){format_alias_case(0,137,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjover-alias-word")){format_alias_case(1,137,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjover-alias-native15")){format_alias_case(2,137,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjover-solid")){blend_solid_case(PictOpConjointOver);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjoverrev-rgba")){rgba_case("cjoverrev-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjoverrev-bgra")){rgba_case("cjoverrev-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjoverrev-opaque")){rgba_case("cjoverrev-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjoverrev-native555")){component_alpha_case(16,PS2_COMPOSITE_CONJOINT_OVER_REVERSE);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjoverrev-byte")){byte_colour_exa(0,138);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjoverrev-word")){byte_colour_exa(1,138);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjoverrev-coverage")){coverage_cross_case("cjoverrev-coverage-from-colour-argb");coverage_cross_case("cjoverrev-coverage-ca-from-colour-argb");coverage_cross_case("cjoverrev-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjoverrev-map")){alpha_maps_case("cjoverrev-alpha-map-combined");alpha_maps_case("cjoverrev-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjoverrev-alias-byte")){format_alias_case(0,138,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjoverrev-alias-word")){format_alias_case(1,138,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjoverrev-alias-native15")){format_alias_case(2,138,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjoverrev-solid")){blend_solid_case(PictOpConjointOverReverse);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjin-rgba")){rgba_case("cjin-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjin-bgra")){rgba_case("cjin-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjin-opaque")){rgba_case("cjin-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjin-native555")){component_alpha_case(16,PS2_COMPOSITE_CONJOINT_IN);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjin-byte")){byte_colour_exa(0,139);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjin-word")){byte_colour_exa(1,139);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjin-coverage")){coverage_cross_case("cjin-coverage-from-colour-argb");coverage_cross_case("cjin-coverage-ca-from-colour-argb");coverage_cross_case("cjin-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjin-map")){alpha_maps_case("cjin-alpha-map-combined");alpha_maps_case("cjin-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjin-alias-byte")){format_alias_case(0,139,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjin-alias-word")){format_alias_case(1,139,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjin-alias-native15")){format_alias_case(2,139,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjin-solid")){blend_solid_case(PictOpConjointIn);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjinrev-rgba")){rgba_case("cjinrev-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjinrev-bgra")){rgba_case("cjinrev-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjinrev-opaque")){rgba_case("cjinrev-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjinrev-native555")){component_alpha_case(16,PS2_COMPOSITE_CONJOINT_IN_REVERSE);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjinrev-byte")){byte_colour_exa(0,140);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjinrev-word")){byte_colour_exa(1,140);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjinrev-coverage")){coverage_cross_case("cjinrev-coverage-from-colour-argb");coverage_cross_case("cjinrev-coverage-ca-from-colour-argb");coverage_cross_case("cjinrev-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjinrev-map")){alpha_maps_case("cjinrev-alpha-map-combined");alpha_maps_case("cjinrev-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjinrev-alias-byte")){format_alias_case(0,140,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjinrev-alias-word")){format_alias_case(1,140,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjinrev-alias-native15")){format_alias_case(2,140,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjinrev-solid")){blend_solid_case(PictOpConjointInReverse);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjout-rgba")){rgba_case("cjout-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjout-bgra")){rgba_case("cjout-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjout-opaque")){rgba_case("cjout-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjout-native555")){component_alpha_case(16,PS2_COMPOSITE_CONJOINT_OUT);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjout-byte")){byte_colour_exa(0,141);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjout-word")){byte_colour_exa(1,141);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjout-coverage")){coverage_cross_case("cjout-coverage-from-colour-argb");coverage_cross_case("cjout-coverage-ca-from-colour-argb");coverage_cross_case("cjout-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjout-map")){alpha_maps_case("cjout-alpha-map-combined");alpha_maps_case("cjout-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjout-alias-byte")){format_alias_case(0,141,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjout-alias-word")){format_alias_case(1,141,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjout-alias-native15")){format_alias_case(2,141,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjout-solid")){blend_solid_case(PictOpConjointOut);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjoutrev-rgba")){rgba_case("cjoutrev-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjoutrev-bgra")){rgba_case("cjoutrev-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjoutrev-opaque")){rgba_case("cjoutrev-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjoutrev-native555")){component_alpha_case(16,PS2_COMPOSITE_CONJOINT_OUT_REVERSE);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjoutrev-byte")){byte_colour_exa(0,142);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjoutrev-word")){byte_colour_exa(1,142);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjoutrev-coverage")){coverage_cross_case("cjoutrev-coverage-from-colour-argb");coverage_cross_case("cjoutrev-coverage-ca-from-colour-argb");coverage_cross_case("cjoutrev-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjoutrev-map")){alpha_maps_case("cjoutrev-alpha-map-combined");alpha_maps_case("cjoutrev-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjoutrev-alias-byte")){format_alias_case(0,142,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjoutrev-alias-word")){format_alias_case(1,142,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjoutrev-alias-native15")){format_alias_case(2,142,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjoutrev-solid")){blend_solid_case(PictOpConjointOutReverse);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjatop-rgba")){rgba_case("cjatop-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjatop-bgra")){rgba_case("cjatop-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjatop-opaque")){rgba_case("cjatop-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjatop-native555")){component_alpha_case(16,PS2_COMPOSITE_CONJOINT_ATOP);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjatop-byte")){byte_colour_exa(0,143);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjatop-word")){byte_colour_exa(1,143);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjatop-coverage")){coverage_cross_case("cjatop-coverage-from-colour-argb");coverage_cross_case("cjatop-coverage-ca-from-colour-argb");coverage_cross_case("cjatop-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjatop-map")){alpha_maps_case("cjatop-alpha-map-combined");alpha_maps_case("cjatop-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjatop-alias-byte")){format_alias_case(0,143,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjatop-alias-word")){format_alias_case(1,143,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjatop-alias-native15")){format_alias_case(2,143,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjatop-solid")){blend_solid_case(PictOpConjointAtop);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjatoprev-rgba")){rgba_case("cjatoprev-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjatoprev-bgra")){rgba_case("cjatoprev-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjatoprev-opaque")){rgba_case("cjatoprev-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjatoprev-native555")){component_alpha_case(16,PS2_COMPOSITE_CONJOINT_ATOP_REVERSE);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjatoprev-byte")){byte_colour_exa(0,144);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjatoprev-word")){byte_colour_exa(1,144);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjatoprev-coverage")){coverage_cross_case("cjatoprev-coverage-from-colour-argb");coverage_cross_case("cjatoprev-coverage-ca-from-colour-argb");coverage_cross_case("cjatoprev-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjatoprev-map")){alpha_maps_case("cjatoprev-alpha-map-combined");alpha_maps_case("cjatoprev-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjatoprev-alias-byte")){format_alias_case(0,144,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjatoprev-alias-word")){format_alias_case(1,144,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjatoprev-alias-native15")){format_alias_case(2,144,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjatoprev-solid")){blend_solid_case(PictOpConjointAtopReverse);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjxor-rgba")){rgba_case("cjxor-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjxor-bgra")){rgba_case("cjxor-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjxor-opaque")){rgba_case("cjxor-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjxor-native555")){component_alpha_case(16,PS2_COMPOSITE_CONJOINT_XOR);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjxor-byte")){byte_colour_exa(0,145);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjxor-word")){byte_colour_exa(1,145);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjxor-coverage")){coverage_cross_case("cjxor-coverage-from-colour-argb");coverage_cross_case("cjxor-coverage-ca-from-colour-argb");coverage_cross_case("cjxor-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjxor-map")){alpha_maps_case("cjxor-alpha-map-combined");alpha_maps_case("cjxor-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjxor-alias-byte")){format_alias_case(0,145,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjxor-alias-word")){format_alias_case(1,145,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjxor-alias-native15")){format_alias_case(2,145,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjxor-solid")){blend_solid_case(PictOpConjointXor);return 0;}

    if(argc==2 && !strcmp(argv[1],"pd-rounding")){pd_rounding_case();return 0;}
    if(argc==2 && !strcmp(argv[1],"pd-guards")){pd_guards_case();return 0;}
    if(argc==2 && !strcmp(argv[1],"piecewise-bytes")){piecewise_bytes_case(0);return 0;}
    if(argc==2 && !strcmp(argv[1],"piecewise-guards")){piecewise_bytes_case(1);return 0;}
    if(argc==2 && !strcmp(argv[1],"order-blends-bytes")){order_blends_bytes_case(0);return 0;}
    if(argc==2 && !strcmp(argv[1],"order-blends-guards")){order_blends_bytes_case(1);return 0;}
    if(argc==2 && !strcmp(argv[1],"exclusion-bytes")){exclusion_bytes_case(0);return 0;}
    if(argc==2 && !strcmp(argv[1],"exclusion-guards")){exclusion_bytes_case(1);return 0;}
    if(argc==2 && !strcmp(argv[1],"djoverrev-opaque-source")){rgba_case("djoverrev-rgba-opaque-source");return 0;}
    if(argc==2 && !strcmp(argv[1],"djoverrev-solid16-native")){solid_disjoint_case(0,PictOpDisjointOverReverse);return 0;}
    if(argc==2 && !strcmp(argv[1],"djoverrev-solid16-packed")){solid_disjoint_case(1,PictOpDisjointOverReverse);return 0;}
    if(argc==2 && !strcmp(argv[1],"djoverrev-solid16-coverage")){solid_disjoint_case(2,PictOpDisjointOverReverse);return 0;}
    if(argc==2 && !strcmp(argv[1],"pd-basic-solid-clear")){blend_solid_case(PictOpClear);return 0;}
    if(argc==2 && !strcmp(argv[1],"pd-basic-solid-src")){blend_solid_case(PictOpSrc);return 0;}
    if(argc==2 && !strcmp(argv[1],"pd-basic-solid-dst")){blend_solid_case(PictOpDst);return 0;}
    if(argc==2 && !strcmp(argv[1],"djclear-solid")){blend_solid_case(PictOpDisjointClear);return 0;}
    if(argc==2 && !strcmp(argv[1],"djclear-rgba")){rgba_case("djclear-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"djclear-bgra")){rgba_case("djclear-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"djclear-opaque")){rgba_case("djclear-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"djclear-native555")){component_alpha_case(16,PS2_COMPOSITE_DISJOINT_CLEAR);return 0;}
    if(argc==2 && !strcmp(argv[1],"djclear-byte")){byte_colour_exa(0,11);return 0;}
    if(argc==2 && !strcmp(argv[1],"djclear-word")){byte_colour_exa(1,11);return 0;}
    if(argc==2 && !strcmp(argv[1],"djclear-coverage")){
        coverage_cross_case("djclear-coverage-from-colour-argb");coverage_cross_case("djclear-coverage-ca-from-colour-argb");
        coverage_cross_case("djclear-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"djclear-map")){alpha_maps_case("djclear-alpha-map-combined");alpha_maps_case("djclear-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"djclear-alias-byte")){format_alias_case(0,11,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"djclear-alias-word")){format_alias_case(1,11,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"djclear-alias-native15")){format_alias_case(2,11,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"djsrc-solid")){blend_solid_case(PictOpDisjointSrc);return 0;}
    if(argc==2 && !strcmp(argv[1],"djsrc-rgba")){rgba_case("djsrc-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"djsrc-bgra")){rgba_case("djsrc-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"djsrc-opaque")){rgba_case("djsrc-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"djsrc-native555")){component_alpha_case(16,PS2_COMPOSITE_DISJOINT_SRC);return 0;}
    if(argc==2 && !strcmp(argv[1],"djsrc-byte")){byte_colour_exa(0,12);return 0;}
    if(argc==2 && !strcmp(argv[1],"djsrc-word")){byte_colour_exa(1,12);return 0;}
    if(argc==2 && !strcmp(argv[1],"djsrc-coverage")){
        coverage_cross_case("djsrc-coverage-from-colour-argb");coverage_cross_case("djsrc-coverage-ca-from-colour-argb");
        coverage_cross_case("djsrc-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"djsrc-map")){alpha_maps_case("djsrc-alpha-map-combined");alpha_maps_case("djsrc-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"djsrc-alias-byte")){format_alias_case(0,12,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"djsrc-alias-word")){format_alias_case(1,12,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"djsrc-alias-native15")){format_alias_case(2,12,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"djdst-solid")){blend_solid_case(PictOpDisjointDst);return 0;}
    if(argc==2 && !strcmp(argv[1],"djdst-rgba")){rgba_case("djdst-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"djdst-bgra")){rgba_case("djdst-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"djdst-opaque")){rgba_case("djdst-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"djdst-native555")){component_alpha_case(16,PS2_COMPOSITE_DISJOINT_DST);return 0;}
    if(argc==2 && !strcmp(argv[1],"djdst-byte")){byte_colour_exa(0,13);return 0;}
    if(argc==2 && !strcmp(argv[1],"djdst-word")){byte_colour_exa(1,13);return 0;}
    if(argc==2 && !strcmp(argv[1],"djdst-coverage")){
        coverage_cross_case("djdst-coverage-from-colour-argb");coverage_cross_case("djdst-coverage-ca-from-colour-argb");
        coverage_cross_case("djdst-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"djdst-map")){alpha_maps_case("djdst-alpha-map-combined");alpha_maps_case("djdst-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"djdst-alias-byte")){format_alias_case(0,13,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"djdst-alias-word")){format_alias_case(1,13,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"djdst-alias-native15")){format_alias_case(2,13,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjclear-solid")){blend_solid_case(PictOpConjointClear);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjclear-rgba")){rgba_case("cjclear-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjclear-bgra")){rgba_case("cjclear-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjclear-opaque")){rgba_case("cjclear-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjclear-native555")){component_alpha_case(16,PS2_COMPOSITE_CONJOINT_CLEAR);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjclear-byte")){byte_colour_exa(0,14);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjclear-word")){byte_colour_exa(1,14);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjclear-coverage")){
        coverage_cross_case("cjclear-coverage-from-colour-argb");coverage_cross_case("cjclear-coverage-ca-from-colour-argb");
        coverage_cross_case("cjclear-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjclear-map")){alpha_maps_case("cjclear-alpha-map-combined");alpha_maps_case("cjclear-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjclear-alias-byte")){format_alias_case(0,14,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjclear-alias-word")){format_alias_case(1,14,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjclear-alias-native15")){format_alias_case(2,14,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjsrc-solid")){blend_solid_case(PictOpConjointSrc);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjsrc-rgba")){rgba_case("cjsrc-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjsrc-bgra")){rgba_case("cjsrc-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjsrc-opaque")){rgba_case("cjsrc-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjsrc-native555")){component_alpha_case(16,PS2_COMPOSITE_CONJOINT_SRC);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjsrc-byte")){byte_colour_exa(0,15);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjsrc-word")){byte_colour_exa(1,15);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjsrc-coverage")){
        coverage_cross_case("cjsrc-coverage-from-colour-argb");coverage_cross_case("cjsrc-coverage-ca-from-colour-argb");
        coverage_cross_case("cjsrc-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjsrc-map")){alpha_maps_case("cjsrc-alpha-map-combined");alpha_maps_case("cjsrc-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjsrc-alias-byte")){format_alias_case(0,15,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjsrc-alias-word")){format_alias_case(1,15,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjsrc-alias-native15")){format_alias_case(2,15,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjdst-solid")){blend_solid_case(PictOpConjointDst);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjdst-rgba")){rgba_case("cjdst-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjdst-bgra")){rgba_case("cjdst-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjdst-opaque")){rgba_case("cjdst-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjdst-native555")){component_alpha_case(16,PS2_COMPOSITE_CONJOINT_DST);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjdst-byte")){byte_colour_exa(0,16);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjdst-word")){byte_colour_exa(1,16);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjdst-coverage")){
        coverage_cross_case("cjdst-coverage-from-colour-argb");coverage_cross_case("cjdst-coverage-ca-from-colour-argb");
        coverage_cross_case("cjdst-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjdst-map")){alpha_maps_case("cjdst-alpha-map-combined");alpha_maps_case("cjdst-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"cjdst-alias-byte")){format_alias_case(0,16,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjdst-alias-word")){format_alias_case(1,16,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"cjdst-alias-native15")){format_alias_case(2,16,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"djoverrev-solid")){blend_solid_case(PictOpDisjointOverReverse);return 0;}
    if(argc==2 && !strcmp(argv[1],"djoverrev-rgba")){rgba_case("djoverrev-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"djoverrev-bgra")){rgba_case("djoverrev-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"djoverrev-opaque")){rgba_case("djoverrev-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"djoverrev-native555")){component_alpha_case(16,PS2_COMPOSITE_DISJOINT_OVER_REVERSE);return 0;}
    if(argc==2 && !strcmp(argv[1],"djoverrev-byte")){byte_colour_exa(0,17);return 0;}
    if(argc==2 && !strcmp(argv[1],"djoverrev-word")){byte_colour_exa(1,17);return 0;}
    if(argc==2 && !strcmp(argv[1],"djoverrev-coverage")){
        coverage_cross_case("djoverrev-coverage-from-colour-argb");coverage_cross_case("djoverrev-coverage-ca-from-colour-argb");
        coverage_cross_case("djoverrev-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"djoverrev-map")){alpha_maps_case("djoverrev-alpha-map-combined");alpha_maps_case("djoverrev-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"djoverrev-alias-byte")){format_alias_case(0,17,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"djoverrev-alias-word")){format_alias_case(1,17,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"djoverrev-alias-native15")){format_alias_case(2,17,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"overlay-solid")){blend_solid_case(PictOpOverlay);return 0;}
    if(argc==2 && !strcmp(argv[1],"overlay-rgba")){rgba_case("overlay-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"overlay-bgra")){rgba_case("overlay-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"overlay-opaque")){rgba_case("overlay-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"overlay-native555")){component_alpha_case(16,PS2_COMPOSITE_OVERLAY);return 0;}
    if(argc==2 && !strcmp(argv[1],"overlay-byte")){byte_colour_exa(0,9);return 0;}
    if(argc==2 && !strcmp(argv[1],"overlay-word")){byte_colour_exa(1,9);return 0;}
    if(argc==2 && !strcmp(argv[1],"overlay-coverage")){
        coverage_cross_case("overlay-coverage-from-colour-argb");coverage_cross_case("overlay-coverage-ca-from-colour-argb");
        coverage_cross_case("overlay-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"overlay-map")){alpha_maps_case("overlay-alpha-map-combined");alpha_maps_case("overlay-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"overlay-alias-byte")){format_alias_case(0,9,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"overlay-alias-word")){format_alias_case(1,9,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"overlay-alias-native15")){format_alias_case(2,9,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"hardlight-solid")){blend_solid_case(PictOpHardLight);return 0;}
    if(argc==2 && !strcmp(argv[1],"hardlight-rgba")){rgba_case("hardlight-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"hardlight-bgra")){rgba_case("hardlight-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"hardlight-opaque")){rgba_case("hardlight-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"hardlight-native555")){component_alpha_case(16,PS2_COMPOSITE_HARDLIGHT);return 0;}
    if(argc==2 && !strcmp(argv[1],"hardlight-byte")){byte_colour_exa(0,10);return 0;}
    if(argc==2 && !strcmp(argv[1],"hardlight-word")){byte_colour_exa(1,10);return 0;}
    if(argc==2 && !strcmp(argv[1],"hardlight-coverage")){
        coverage_cross_case("hardlight-coverage-from-colour-argb");coverage_cross_case("hardlight-coverage-ca-from-colour-argb");
        coverage_cross_case("hardlight-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"hardlight-map")){alpha_maps_case("hardlight-alpha-map-combined");alpha_maps_case("hardlight-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"hardlight-alias-byte")){format_alias_case(0,10,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"hardlight-alias-word")){format_alias_case(1,10,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"hardlight-alias-native15")){format_alias_case(2,10,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"darken-solid")){blend_solid_case(PictOpDarken);return 0;}
    if(argc==2 && !strcmp(argv[1],"darken-rgba")){rgba_case("darken-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"darken-bgra")){rgba_case("darken-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"darken-opaque")){rgba_case("darken-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"darken-native555")){component_alpha_case(16,PS2_COMPOSITE_DARKEN);return 0;}
    if(argc==2 && !strcmp(argv[1],"darken-byte")){byte_colour_exa(0,6);return 0;}
    if(argc==2 && !strcmp(argv[1],"darken-word")){byte_colour_exa(1,6);return 0;}
    if(argc==2 && !strcmp(argv[1],"darken-coverage")){
        coverage_cross_case("darken-coverage-from-colour-argb");coverage_cross_case("darken-coverage-ca-from-colour-argb");
        coverage_cross_case("darken-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"darken-map")){alpha_maps_case("darken-alpha-map-combined");alpha_maps_case("darken-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"darken-alias-byte")){format_alias_case(0,6,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"darken-alias-word")){format_alias_case(1,6,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"darken-alias-native15")){format_alias_case(2,6,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"lighten-solid")){blend_solid_case(PictOpLighten);return 0;}
    if(argc==2 && !strcmp(argv[1],"lighten-rgba")){rgba_case("lighten-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"lighten-bgra")){rgba_case("lighten-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"lighten-opaque")){rgba_case("lighten-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"lighten-native555")){component_alpha_case(16,PS2_COMPOSITE_LIGHTEN);return 0;}
    if(argc==2 && !strcmp(argv[1],"lighten-byte")){byte_colour_exa(0,7);return 0;}
    if(argc==2 && !strcmp(argv[1],"lighten-word")){byte_colour_exa(1,7);return 0;}
    if(argc==2 && !strcmp(argv[1],"lighten-coverage")){
        coverage_cross_case("lighten-coverage-from-colour-argb");coverage_cross_case("lighten-coverage-ca-from-colour-argb");
        coverage_cross_case("lighten-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"lighten-map")){alpha_maps_case("lighten-alpha-map-combined");alpha_maps_case("lighten-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"lighten-alias-byte")){format_alias_case(0,7,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"lighten-alias-word")){format_alias_case(1,7,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"lighten-alias-native15")){format_alias_case(2,7,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"difference-solid")){blend_solid_case(PictOpDifference);return 0;}
    if(argc==2 && !strcmp(argv[1],"difference-rgba")){rgba_case("difference-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"difference-bgra")){rgba_case("difference-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"difference-opaque")){rgba_case("difference-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"difference-native555")){component_alpha_case(16,PS2_COMPOSITE_DIFFERENCE);return 0;}
    if(argc==2 && !strcmp(argv[1],"difference-byte")){byte_colour_exa(0,8);return 0;}
    if(argc==2 && !strcmp(argv[1],"difference-word")){byte_colour_exa(1,8);return 0;}
    if(argc==2 && !strcmp(argv[1],"difference-coverage")){
        coverage_cross_case("difference-coverage-from-colour-argb");coverage_cross_case("difference-coverage-ca-from-colour-argb");
        coverage_cross_case("difference-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"difference-map")){alpha_maps_case("difference-alpha-map-combined");alpha_maps_case("difference-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"difference-alias-byte")){format_alias_case(0,8,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"difference-alias-word")){format_alias_case(1,8,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"difference-alias-native15")){format_alias_case(2,8,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"multiply-solid")){blend_solid_case(PictOpMultiply);return 0;}
    if(argc==2 && !strcmp(argv[1],"multiply-rgba")){rgba_case("multiply-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"multiply-bgra")){rgba_case("multiply-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"multiply-opaque")){rgba_case("multiply-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"multiply-native555")){component_alpha_case(16,PS2_COMPOSITE_MULTIPLY);return 0;}
    if(argc==2 && !strcmp(argv[1],"multiply-byte")){byte_colour_exa(0,5);return 0;}
    if(argc==2 && !strcmp(argv[1],"multiply-word")){byte_colour_exa(1,5);return 0;}
    if(argc==2 && !strcmp(argv[1],"multiply-coverage")){
        coverage_cross_case("multiply-coverage-from-colour-argb");coverage_cross_case("multiply-coverage-ca-from-colour-argb");
        coverage_cross_case("multiply-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"multiply-map")){alpha_maps_case("multiply-alpha-map-combined");alpha_maps_case("multiply-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"multiply-alias-byte")){format_alias_case(0,5,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"multiply-alias-word")){format_alias_case(1,5,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"multiply-alias-native15")){format_alias_case(2,5,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"exclusion-solid")){blend_solid_case(PictOpExclusion);return 0;}
    if(argc==2 && !strcmp(argv[1],"exclusion-rgba")){rgba_case("exclusion-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"exclusion-bgra")){rgba_case("exclusion-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"exclusion-opaque")){rgba_case("exclusion-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"exclusion-native555")){component_alpha_case(16,PS2_COMPOSITE_EXCLUSION);return 0;}
    if(argc==2 && !strcmp(argv[1],"exclusion-byte")){byte_colour_exa(0,4);return 0;}
    if(argc==2 && !strcmp(argv[1],"exclusion-word")){byte_colour_exa(1,4);return 0;}
    if(argc==2 && !strcmp(argv[1],"exclusion-coverage")){
        coverage_cross_case("exclusion-coverage-from-colour-argb");coverage_cross_case("exclusion-coverage-ca-from-colour-argb");
        coverage_cross_case("exclusion-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"exclusion-map")){alpha_maps_case("exclusion-alpha-map-combined");alpha_maps_case("exclusion-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"exclusion-alias-byte")){format_alias_case(0,4,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"exclusion-alias-word")){format_alias_case(1,4,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"exclusion-alias-native15")){format_alias_case(2,4,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"screen-solid")){blend_solid_case(PictOpScreen);return 0;}
    if(argc==2 && !strcmp(argv[1],"screen-rgba")){rgba_case("screen-rgba");return 0;}
    if(argc==2 && !strcmp(argv[1],"screen-bgra")){rgba_case("screen-bgra");return 0;}
    if(argc==2 && !strcmp(argv[1],"screen-opaque")){rgba_case("screen-rgba-opaque-destination");return 0;}
    if(argc==2 && !strcmp(argv[1],"screen-native555")){component_alpha_case(16,PS2_COMPOSITE_SCREEN);return 0;}
    if(argc==2 && !strcmp(argv[1],"screen-byte")){byte_colour_exa(0,3);return 0;}
    if(argc==2 && !strcmp(argv[1],"screen-word")){byte_colour_exa(1,3);return 0;}
    if(argc==2 && !strcmp(argv[1],"screen-coverage")){
        coverage_cross_case("screen-coverage-from-colour-argb");coverage_cross_case("screen-coverage-ca-from-colour-argb");
        coverage_cross_case("screen-coverage-to-colour-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"screen-map")){alpha_maps_case("screen-alpha-map-combined");alpha_maps_case("screen-alpha-map-combined-555");return 0;}
    if(argc==2 && !strcmp(argv[1],"screen-alias-byte")){format_alias_case(0,3,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"screen-alias-word")){format_alias_case(1,3,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"screen-alias-native15")){format_alias_case(2,3,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"convolution-float")){convolution_float_case(0);return 0;}
    if(argc==2 && !strcmp(argv[1],"convolution-float-guards")){convolution_float_case(1);return 0;}
    if(argc==2 && !strcmp(argv[1],"native15-codec")){native15_codec(0);return 0;}
    if(argc==2 && !strcmp(argv[1],"native15-codec-guards")){native15_codec(1);return 0;}
    if(argc==2 && !strcmp(argv[1],"native15-alias")){format_alias_case(2,0,0);return 0;}
    if(argc==2 && !strcmp(argv[1],"native15-alias-boundaries")){format_alias_case(2,1,0);return 0;}
    if(argc==2 && !strcmp(argv[1],"native15-alias-guards")){format_alias_case(2,2,0);return 0;}
    if(argc==2 && !strcmp(argv[1],"format-alias-guards")){format_alias_case(0,2,0);return 0;}
    if(argc==2 && !strcmp(argv[1],"format-alias-byte")){format_alias_case(0,0,0);return 0;}
    if(argc==2 && !strcmp(argv[1],"format-alias-word")){format_alias_case(1,0,0);return 0;}
    if(argc==2 && !strcmp(argv[1],"format-alias-boundaries")){format_alias_case(0,1,0);format_alias_case(1,1,0);return 0;}
    if(argc==2 && !strcmp(argv[1],"backing-alias-byte")){format_alias_case(0,0,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"backing-alias-word")){format_alias_case(1,0,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"window-alias-native15")){format_alias_case(2,0,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"window-alias-word")){format_alias_case(1,0,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"window-alias-boundaries")){format_alias_case(2,1,2);format_alias_case(1,1,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"shifted-map-native")){shifted_map_exa_case(0);return 0;}
    if(argc==2 && !strcmp(argv[1],"shifted-map-packed")){shifted_map_exa_case(1);return 0;}
    if(argc==2 && !strcmp(argv[1],"shifted-map-coverage")){shifted_map_exa_case(2);return 0;}
    if(argc==2 && !strcmp(argv[1],"shifted-map-basic")){alpha_map_raw_alias(1);return 0;}
    if(argc==2 && !strcmp(argv[1],"shifted-map-boundaries")){shifted_map_boundaries();return 0;}
    if(argc==2 && !strcmp(argv[1],"saturate-map-guards")){saturate_map_guards();return 0;}
    if(argc==2 && !strcmp(argv[1],"convolution-map-native")){saturate_map_exa_case(0,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"convolution-map-packed")){saturate_map_exa_case(1,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"convolution-map-coverage")){saturate_map_exa_case(2,1);return 0;}
    if(argc==2 && !strcmp(argv[1],"convolution-dispatch")){saturate_dispatch_case(0,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"convolution-exa")){saturate_dispatch_case(1,2);return 0;}
    if(argc==2 && !strcmp(argv[1],"saturate-map-native-exa")){saturate_map_exa_case(0,0);return 0;}
    if(argc==2 && !strcmp(argv[1],"saturate-map-packed-exa")){saturate_map_exa_case(1,0);return 0;}
    if(argc==2 && !strcmp(argv[1],"saturate-map-coverage-exa")){saturate_map_exa_case(2,0);return 0;}
    if(argc==2 && !strcmp(argv[1],"saturate-map-source")){saturate_map_case(0,32);return 0;}
    if(argc==2 && !strcmp(argv[1],"saturate-map-mask")){saturate_map_case(1,32);return 0;}
    if(argc==2 && !strcmp(argv[1],"saturate-map-combined")){saturate_map_case(2,32);return 0;}
    if(argc==2 && !strcmp(argv[1],"saturate-map-destination")){saturate_map_case(3,32);return 0;}
    if(argc==2 && !strcmp(argv[1],"saturate-map-555")){saturate_map_case(2,16);return 0;}
    if(argc==2 && !strcmp(argv[1],"saturate-solid-guards")){solid_saturate_guards();return 0;}
    if(argc==2 && !strcmp(argv[1],"saturate-solid-native")){solid_saturate_case(0);return 0;}
    if(argc==2 && !strcmp(argv[1],"saturate-solid-packed")){solid_saturate_case(1);return 0;}
    if(argc==2 && !strcmp(argv[1],"saturate-solid-coverage")){solid_saturate_case(2);return 0;}
    if(argc==2 && !strcmp(argv[1],"saturate-alpha-float")){saturate_alpha_float_case(0);return 0;}
    if(argc==2 && !strcmp(argv[1],"saturate-alpha-float-guards")){saturate_alpha_float_case(1);return 0;}
    assert(argc==2);
    if(!strcmp(argv[1],"wide-bilinear")){wide_bilinear_case(0);return 0;}
    if(!strcmp(argv[1],"wide-bilinear-guards")){wide_bilinear_case(1);return 0;}
    if(!strcmp(argv[1],"saturate-opaque-extent")){saturate_opaque_extent();return 0;}
    if(!strcmp(argv[1],"saturate-bilinear-raw-masks")){saturate_bilinear_raw_masks();return 0;}
    if(!strcmp(argv[1],"float-multiply")){float_arithmetic_case(0,0);return 0;}
    if(!strcmp(argv[1],"float-multiply-guards")){float_arithmetic_case(1,0);return 0;}
    if(!strcmp(argv[1],"float-divide")){float_arithmetic_case(0,1);return 0;}
    if(!strcmp(argv[1],"float-divide-guards")){float_arithmetic_case(1,1);return 0;}
    if(!strcmp(argv[1],"float-add")){float_arithmetic_case(0,2);return 0;}
    if(!strcmp(argv[1],"float-add-guards")){float_arithmetic_case(1,2);return 0;}
    if(!strcmp(argv[1],"float-subtract")){float_arithmetic_case(0,3);return 0;}
    if(!strcmp(argv[1],"float-subtract-guards")){float_arithmetic_case(1,3);return 0;}
    if(!strcmp(argv[1],"float-unorm")){float_unorm_case();return 0;}
    if(!strcmp(argv[1],"saturate-rgba8")){saturate_rgba8_case(0,0);return 0;}
    if(!strcmp(argv[1],"saturate-rgba8-guards")){saturate_rgba8_case(1,0);return 0;}
    if(!strcmp(argv[1],"saturate-float")){saturate_rgba8_case(0,1);return 0;}
    if(!strcmp(argv[1],"saturate-float-guards")){saturate_rgba8_case(1,1);return 0;}
    if(!strcmp(argv[1],"saturate-dispatch")){saturate_dispatch_case(0,0);return 0;}
    if(!strcmp(argv[1],"saturate-exa")){saturate_dispatch_case(1,0);return 0;}
    if(!strcmp(argv[1],"saturate-bilinear-dispatch")){saturate_dispatch_case(0,1);return 0;}
    if(!strcmp(argv[1],"saturate-bilinear-exa")){saturate_dispatch_case(1,1);return 0;}
    if(!strcmp(argv[1],"saturate-coverage-aliases")){saturate_coverage_alias_case();return 0;}
    if(!strncmp(argv[1],"saturate-coverage",17)){coverage_cross_case(argv[1]);return 0;}
    if(!strcmp(argv[1],"unorm-float")){unorm_float_case();return 0;}
    if(!strcmp(argv[1],"wide-product")){wide_product_case(0);return 0;}
    if(!strcmp(argv[1],"wide-product-guards")){wide_product_case(1);return 0;}
    if(!strcmp(argv[1],"divide-bytes")){divide_case(0);return 0;}
    if(!strcmp(argv[1],"divide-wide")){divide_case(1);return 0;}
    if(!strcmp(argv[1],"divide-guards")){divide_case(2);return 0;}
    if(!strcmp(argv[1],"alpha-map-raw-alias")){alpha_map_raw_alias(0);return 0;}
    if(!strcmp(argv[1],"preflight"))guards(0,0);
    else if(!strcmp(argv[1],"rgb444-codec"))rgb444_codec();
    else if(!strcmp(argv[1],"rgb444-exa"))rgb444_exa();
    else if(!strcmp(argv[1],"byte-colour-codec"))byte_colour_codec();
    else if(!strcmp(argv[1],"byte-colour-exa"))byte_colour_exa(0,0);
    else if(!strcmp(argv[1],"word-colour-codec"))word_colour_codec();
    else if(!strcmp(argv[1],"word-colour-exa"))byte_colour_exa(1,0);
    else if(!strcmp(argv[1],"saturate-byte-exa"))byte_colour_exa(0,1);
    else if(!strcmp(argv[1],"saturate-word-exa"))byte_colour_exa(1,1);
    else if(!strcmp(argv[1],"saturate-bilinear-byte-exa"))byte_colour_exa(0,2);
    else if(!strcmp(argv[1],"saturate-bilinear-word-exa"))byte_colour_exa(1,2);
    else if(!strcmp(argv[1],"alpha-map-exa"))alpha_map_exa(0);
    else if(!strcmp(argv[1],"alpha-map-window-exa"))alpha_map_exa(1);
    else if(!strcmp(argv[1],"alpha-map-aliases"))alpha_map_aliases();
    else if(!strcmp(argv[1],"alpha-map-guards"))alpha_map_guards();
    else if(!strncmp(argv[1],"alpha-map-",10))alpha_maps_case(argv[1]);
    else if(!strcmp(argv[1],"noop"))guards(1,0);
    else if(!strcmp(argv[1],"allocation"))guards(0,1);
    else if(!strncmp(argv[1],"coverage-core",13))coverage_core_case(argv[1]);
    else if(!strcmp(argv[1],"coverage-a8-copyplane"))coverage_a8_copyplane();
    else if(!strcmp(argv[1],"component-alpha"))component_alpha_case(32,0);
    else if(!strcmp(argv[1],"component-alpha-555"))component_alpha_case(16,0);
    else if(!strcmp(argv[1],"component-alpha-exa"))component_alpha_exa();
    else if(!strcmp(argv[1],"component-alpha-coverage"))component_alpha_coverage(32);
    else if(!strcmp(argv[1],"component-alpha-coverage-555"))component_alpha_coverage(16);
    else if(!strcmp(argv[1],"rgba-alias-boundaries"))rgba_alias_boundaries();
    else if(!strcmp(argv[1],"failure"))failed();
    else if(!strncmp(argv[1],"rgba",4) || !strncmp(argv[1],"bg",2))rgba_case(argv[1]);
    else if(!strncmp(argv[1],"exa",3))composite_exa_case(argv[1]);
    else if(!strncmp(argv[1],"alpha",5))alpha_case(argv[1]);
    else if(!strncmp(argv[1],"solid",5))solid_case(argv[1]);
    else if(!strncmp(argv[1],"porter",6))porter_duff(argv[1]);
    else if(!strncmp(argv[1],"colour-filter",13))colour_filter_case(argv[1]);
    else if(!strncmp(argv[1],"convolution",11))convolution_case(argv[1]);
    else if(!strcmp(argv[1],"filter-guards"))filter_guards();
    else if(!strncmp(argv[1],"formats",7))formats_case(argv[1]);
    else if(!strncmp(argv[1],"bilinear",8))bilinear_mask_case(argv[1]);
    else if(!strcmp(argv[1],"coverage-exa"))coverage_exa_case();
    else if(!strncmp(argv[1],"coverage-",9))coverage_cross_case(argv[1]);
    else composite_case(argv[1]);
    puts("PS2 nearest composite/pixman PASS");return 0;
}
