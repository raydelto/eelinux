/* SPDX-License-Identifier: MIT */
#include "previous-composite.c"
static pixman_image_t *front_reference(uint32_t handle,pixman_format_code_t code,void **memory)
{
    struct pixels *p=model(handle);unsigned cpp=PIXMAN_FORMAT_BPP(code)/8,stride=(p->width*cpp+3)&~3U;
    *memory=calloc(stride,p->height);assert(*memory);
    for(unsigned y=0;y<p->height;++y)for(unsigned x=0;x<p->width;++x)
        memcpy((char*)*memory+y*stride+x*cpp,p->data+y*p->width+x,cpp);
    pixman_image_t *ref=pixman_image_create_bits(code,p->width,p->height,*memory,stride);assert(ref);return ref;
}
static unsigned front_operation(unsigned index)
{
    static const unsigned ops[]={PS2_COMPOSITE_CLEAR,PS2_COMPOSITE_SRC,PS2_COMPOSITE_DST,PS2_COMPOSITE_OVER,PS2_COMPOSITE_OVER_REVERSE,
        PS2_COMPOSITE_IN,PS2_COMPOSITE_IN_REVERSE,PS2_COMPOSITE_OUT,PS2_COMPOSITE_OUT_REVERSE,PS2_COMPOSITE_ATOP,PS2_COMPOSITE_ATOP_REVERSE,PS2_COMPOSITE_XOR,PS2_COMPOSITE_ADD,PS2_COMPOSITE_SATURATE,
        PS2_COMPOSITE_DISJOINT_CLEAR,PS2_COMPOSITE_DISJOINT_SRC,PS2_COMPOSITE_DISJOINT_DST,PS2_COMPOSITE_DISJOINT_OVER,PS2_COMPOSITE_DISJOINT_OVER_REVERSE,
        PS2_COMPOSITE_DISJOINT_IN,PS2_COMPOSITE_DISJOINT_IN_REVERSE,PS2_COMPOSITE_DISJOINT_OUT,PS2_COMPOSITE_DISJOINT_OUT_REVERSE,PS2_COMPOSITE_DISJOINT_ATOP,PS2_COMPOSITE_DISJOINT_ATOP_REVERSE,PS2_COMPOSITE_DISJOINT_XOR,
        PS2_COMPOSITE_CONJOINT_CLEAR,PS2_COMPOSITE_CONJOINT_SRC,PS2_COMPOSITE_CONJOINT_DST,PS2_COMPOSITE_CONJOINT_OVER,PS2_COMPOSITE_CONJOINT_OVER_REVERSE,PS2_COMPOSITE_CONJOINT_IN,PS2_COMPOSITE_CONJOINT_IN_REVERSE,PS2_COMPOSITE_CONJOINT_OUT,PS2_COMPOSITE_CONJOINT_OUT_REVERSE,PS2_COMPOSITE_CONJOINT_ATOP,PS2_COMPOSITE_CONJOINT_ATOP_REVERSE,PS2_COMPOSITE_CONJOINT_XOR,
        PS2_COMPOSITE_MULTIPLY,PS2_COMPOSITE_SCREEN,PS2_COMPOSITE_OVERLAY,PS2_COMPOSITE_DARKEN,PS2_COMPOSITE_LIGHTEN,PS2_COMPOSITE_COLOR_DODGE,PS2_COMPOSITE_COLOR_BURN,PS2_COMPOSITE_HARDLIGHT,PS2_COMPOSITE_SOFTLIGHT,PS2_COMPOSITE_DIFFERENCE,PS2_COMPOSITE_EXCLUSION,PS2_COMPOSITE_HSL_HUE,PS2_COMPOSITE_HSL_SATURATION,PS2_COMPOSITE_HSL_COLOR,PS2_COMPOSITE_HSL_LUMINOSITY};
    return ops[index];
}
int main(int argc,char **argv)
{
    assert(argc==2);unsigned bpp=!strcmp(argv[1],"front16")?16:32;
    assert(!strcmp(argv[1],"front16") || !strcmp(argv[1],"front32"));
    rgba_enabled=1;setup_rop(bpp);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    gs->front.width=model(0)->width=19;gs->front.height=model(0)->height=11;
    uint32_t raw;assert(!ps2_surface_create(gs,19,11,32,&raw) && !ps2_surface_set_depth(gs,raw,32));
    int32_t kernel[]={131072,65536,98303,-32767};unsigned requests=0;
    const pixman_transform_t transform={{{49152,21845,173},{0,98304,-21845},{65536/19,0,65536}}};
    for(unsigned role=0;role<5;++role)for(unsigned filter=0;filter<3;++filter)for(unsigned op=0;op<53;++op){
        seed(model(0));rgba_seed(model(raw));uint32_t sh=role==1 || role==3?0:raw,dh=role==1?raw:0,mh=role==3?raw:0;
        unsigned masked=role==2 || role==3;
        pixman_format_code_t native=bpp==16?PIXMAN_x1b5g5r5:PIXMAN_x8b8g8r8,sc=sh?PIXMAN_a2r10g10b10:native,
            dc=dh?PIXMAN_a2b10g10r10:native,mc=mh?PIXMAN_a2b10g10r10:native;
        struct ps2_picture_sample s={.source_argb=!!sh,.source_packed10=sh?PS2_PACKED10_ARGB:0,
            .destination_argb=!!dh,.destination_packed10=dh?PS2_PACKED10_ABGR:0,
            .repeat=PS2_REPEAT_REFLECT,.bilinear=filter==1,.convolution=filter==2,
            .filter_params=filter==2?kernel:NULL,.filter_nparams=filter==2?4:0};
        if(!sh)s.source_unorm=(struct ps2_unorm_layout){.rgb_bits={bpp==16?5:8,bpp==16?5:8,bpp==16?5:8}};
        if(!dh)s.destination_unorm=(struct ps2_unorm_layout){.rgb_bits={bpp==16?5:8,bpp==16?5:8,bpp==16?5:8}};
        struct ps2_picture_bitmap m={.has_surface=1,.surface=mh,.width=19,.height=11,.x=1,.y=2,.component_alpha=op&1,
            .sample={.source_argb=!!mh,.source_packed10=mh?PS2_PACKED10_ABGR:0,.repeat=PS2_REPEAT_REFLECT,
            .bilinear=filter==1,.convolution=filter==2,.filter_params=filter==2?kernel:NULL,.filter_nparams=filter==2?4:0}};
        if(!mh)m.sample.source_unorm=s.destination_unorm;
        void *sm,*dm,*mm=NULL;pixman_image_t *si=front_reference(sh,sc,&sm),*di=front_reference(dh,dc,&dm),
            *mi=masked?front_reference(mh,mc,&mm):NULL,*ai=NULL;
        if(role==4 && bpp==16){
            s.destination_alpha_map=(struct ps2_picture_alpha_map){.enabled=1,.surface=0,.width=19,.height=11,
                .rgb_bits={4,4,4},.swap=1,.x=1,.y=-1};
            ai=pixman_image_create_bits(PIXMAN_x4r4g4b4,19,11,dm,40);assert(ai);pixman_image_set_alpha_map(di,ai,1,-1);
        }
        for(unsigned i=0;i<(masked?2U:1U);++i){pixman_image_t *ref=i?mi:si;struct ps2_picture_sample *sample=i?&m.sample:&s;
            pixman_image_set_repeat(ref,PIXMAN_REPEAT_REFLECT);
            if(filter){sample->transformed=1;memcpy(sample->matrix,&transform,sizeof(transform));assert(pixman_image_set_transform(ref,&transform));}
            assert(pixman_image_set_filter(ref,filter==2?PIXMAN_FILTER_CONVOLUTION:filter?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,filter==2?kernel:NULL,filter==2?4:0));
        }
        if(mi)pixman_image_set_component_alpha(mi,m.component_alpha);
        unsigned x=op%3==0?18:3,y=op%3==0?10:2;
        if(role==4)x=3,y=2;
        struct ps2_pipe_copy_box box={2,1,x,y,1,1};
        pixman_image_composite32(packed10_all_ops[op],si,mi,di,2,1,1,2,x,y,1,1);
        compositor_active=1;
        if(ps2_pipe_composite_rect(ctx,sh,dh,&box,&s,masked?&m:NULL,front_operation(op))){
            fprintf(stderr,"front failed bpp=%u role=%u filter=%u op=%u errno=%d\n",bpp,role,filter,packed10_all_ops[op],errno);abort();}
        compositor_active=0;unsigned cpp=bpp==16 && !dh?2:4,stride=(19*cpp+3)&~3U,mask=dh?UINT32_MAX:bpp==16?0x7fff:0xffffff;
        for(unsigned yy=0;yy<11;++yy)for(unsigned xx=0;xx<19;++xx){uint32_t value=0;memcpy(&value,(char*)dm+yy*stride+xx*cpp,cpp);
            if((value^model(dh)->data[yy*19+xx])&mask){fprintf(stderr,"front mismatch bpp=%u role=%u filter=%u op=%u xy=%u,%u expected=%08x actual=%08x\n",bpp,role,filter,packed10_all_ops[op],xx,yy,value,model(dh)->data[yy*19+xx]);abort();}}
        pixman_image_unref(si);pixman_image_unref(di);if(mi)pixman_image_unref(mi);if(ai)pixman_image_unref(ai);free(sm);free(dm);free(mm);++requests;
    }
    assert(!ps2_surface_destroy(gs,raw));finish_rop();printf("PS2 packed front bpp=%u requests=%u PASS\n",bpp,requests);return 0;
}
