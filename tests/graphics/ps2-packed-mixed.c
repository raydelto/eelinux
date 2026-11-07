/* SPDX-License-Identifier: MIT */
#include "previous-composite.c"

static const unsigned mixed_formats[]={PICT_a1,PICT_x4a4,PICT_a8,
    PICT_r3g3b2,PICT_b2g3r3,PICT_a2r2g2b2,PICT_a2b2g2r2,
    PICT_r5g6b5,PICT_b5g6r5,PICT_a1r5g5b5,PICT_a1b5g5r5,PICT_a4r4g4b4,PICT_a4b4g4r4,
    PICT_x1r5g5b5,PICT_x1b5g5r5,PICT_x4r4g4b4,PICT_x4b4g4r4,
    PICT_x1r5g5b5,PICT_x1b5g5r5,PICT_x4r4g4b4,PICT_x4b4g4r4};

static PixmapPtr mixed_pixmap(unsigned format,unsigned native)
{
    if(PICT_FORMAT_R(format)!=10 && !native)return map_fixture_pixmap(format,0);
    PixmapPtr p=calloc(1,sizeof(*p));assert(p);int pitch;
    unsigned depth=native?15:32,bpp=native?16:32;
    p->drawable=(DrawableRec){.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=depth,.bitsPerPixel=bpp,.width=13,.height=7};
    p->devPrivates=exa.driver.CreatePixmap2(&xs,13,7,depth,0,bpp,&pitch);assert(p->devPrivates);p->devKind=pitch;return p;
}
static void mixed_case(unsigned role,unsigned filter)
{
    rgba_enabled=1;setup_rop(32);
    struct ps2_exa_context_bridge bridge={0};struct ps2_texture_exa texture={0};struct ps2_blit_exa blit={0};
    assert(ps2_exa_context_bridge_prepare(&xs,&bridge,&exa,ctx) &&
        ps2_texture_exa_prepare(&xs,&texture,&exa,ps2_pipe_context_render(ctx)) && ps2_blit_exa_prepare(&xs,&blit,&exa,ctx));
    const unsigned codes[]={PICT_a2r10g10b10,PICT_a2b10g10r10,PICT_x2r10g10b10,PICT_x2b10g10r10};
    PictTransform transform={.matrix={{49152,21845,173},{0,98304,-21845},{65536/19,0,65536}}};
    int32_t kernel[]={131072,65536,98303,-32767};unsigned checks=0;
    for(unsigned format=0;format<21;++format)for(unsigned op=0;op<53;++op){
        unsigned layout=(format+op)%4,indices[]={format,(format+7)%21,(format+11)%21},fc[4]={0};
        for(unsigned i=0;i<3;++i)fc[i]=role>=3?mixed_formats[indices[i]]:role==i?mixed_formats[format]:codes[(layout+i)%4];
        fc[3]=codes[(layout+3)%4];
        PixmapPtr pixels[4];PictureRec pictures[4];pixman_image_t *reference[4];void *memory[4];
        for(unsigned i=0;i<4;++i){
            pixels[i]=mixed_pixmap(fc[i],role>=3?i<3 && indices[i]>=17:i==role && format>=17);
            map_fixture_seed(pixels[i],format*13+op*7+i*19);
            pictures[i]=(PictureRec){.pDrawable=&pixels[i]->drawable,.format=fc[i],.filter=PictFilterNearest};
            reference[i]=map_fixture_image(pixels[i],fc[i],memory+i);
        }
        for(unsigned i=0;i<2;++i){
            pictures[i].repeat=TRUE;pictures[i].repeatType=(format+op)%4;
            pixman_image_set_repeat(reference[i],pictures[i].repeatType);
            if(filter){pictures[i].transform=&transform;assert(pixman_image_set_transform(reference[i],(pixman_transform_t*)&transform));}
            pictures[i].filter=filter==2?PictFilterConvolution:filter?PictFilterBilinear:PictFilterNearest;
            if(filter==2){pictures[i].filter_params=kernel;pictures[i].filter_nparams=4;}
            assert(pixman_image_set_filter(reference[i],filter==2?PIXMAN_FILTER_CONVOLUTION:filter?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,
                pictures[i].filter_params,pictures[i].filter_nparams));
        }
        if(role>=3){unsigned parent=role==3?0:2;
            pictures[parent].alphaMap=pictures+3;pictures[parent].alphaOrigin=(DDXPointRec){1,1};
            pixman_image_set_alpha_map(reference[parent],reference[3],1,1);
        }
        pictures[1].componentAlpha=(format+op)&1;pixman_image_set_component_alpha(reference[1],pictures[1].componentAlpha);
        unsigned masked=role==1 || op%7!=0,width=role==4 && op%13==0?2:1,height=width;
        pixman_image_composite32(packed10_all_ops[op],reference[0],masked?reference[1]:NULL,reference[2],2,1,3,2,3,2,width,height);
        compositor_active=1;
        if(!exa.driver.CheckComposite(packed10_all_ops[op],pictures,masked?pictures+1:NULL,pictures+2) ||
            !exa.driver.PrepareComposite(packed10_all_ops[op],pictures,masked?pictures+1:NULL,pictures+2,pixels[0],masked?pixels[1]:NULL,pixels[2])){
            fprintf(stderr,"mixed prepare role=%u filter=%u format=%u op=%u errno=%d\n",role,filter,format,packed10_all_ops[op],errno);abort();}
        fprintf(stderr,"mixed request role=%u filter=%u format=%u op=%u\n",role,filter,format,packed10_all_ops[op]);
        exa.driver.Composite(pixels[2],2,1,3,2,3,2,width,height);compositor_active=0;exa.driver.DoneComposite(pixels[2]);
        map_fixture_expect(pixels[2],fc[2],memory[2],role,format,filter);
        if(role==4)map_fixture_expect(pixels[3],fc[3],memory[3],role,format,filter);
        for(unsigned i=0;i<4;++i){pixman_image_unref(reference[i]);free(memory[i]);coverage_test_destroy(pixels[i]);}
        ++checks;
    }
    assert(checks==1113);printf("PS2 mixed packed10 role=%u filter=%u requests=%u PASS\n",role,filter,checks);
    ps2_blit_exa_cleanup(&xs,&blit);ps2_exa_context_bridge_cleanup(&bridge);finish_rop();
}
static void mixed_alias_case(unsigned filter)
{
    rgba_enabled=1;setup_rop(32);
    struct ps2_exa_context_bridge bridge={0};struct ps2_texture_exa texture={0};struct ps2_blit_exa blit={0};
    assert(ps2_exa_context_bridge_prepare(&xs,&bridge,&exa,ctx) &&
        ps2_texture_exa_prepare(&xs,&texture,&exa,ps2_pipe_context_render(ctx)) && ps2_blit_exa_prepare(&xs,&blit,&exa,ctx));
    const unsigned codes[]={PICT_a2r10g10b10,PICT_a2b10g10r10,PICT_x2r10g10b10,PICT_x2b10g10r10};
    PictTransform transform={.matrix={{49152,21845,173},{0,98304,-21845},{65536/19,0,65536}}};
    int32_t kernel[]={131072,65536,98303,-32767};unsigned checks=0;
    for(unsigned format=0;format<21;++format)for(unsigned op=0;op<53;++op){
        unsigned mf=format==0?0:format<7?1+format%6:format<17?7+(format-6)%10:17+(format-16)%4,
            sc=codes[(format+op)%4],dc=mixed_formats[format],mc=mixed_formats[mf];
        PixmapPtr sp=mixed_pixmap(sc,0),dp=mixed_pixmap(dc,format>=17);
        map_fixture_seed(sp,format*13+op*7);map_fixture_seed(dp,format*13+op*7+19);
        PictureRec src={.pDrawable=&sp->drawable,.format=sc,.filter=PictFilterNearest,.repeat=TRUE,.repeatType=RepeatReflect},
            mask={.pDrawable=&dp->drawable,.format=mc,.filter=PictFilterNearest,.repeat=TRUE,.repeatType=RepeatReflect,.componentAlpha=op&1},
            map={.pDrawable=&dp->drawable,.format=mc,.filter=PictFilterNearest},
            dest={.pDrawable=&dp->drawable,.format=dc,.filter=PictFilterNearest,.alphaMap=&map,.alphaOrigin={op&1?-1:1,op&2?-1:1}};
        void *sm,*dm,*mm;pixman_image_t *si=map_fixture_image(sp,sc,&sm),*di=map_fixture_image(dp,dc,&dm),*mi=map_fixture_image(dp,mc,&mm);
        unsigned stride=(13*PIXMAN_FORMAT_BPP(dc)+31)/32*4;
        pixman_image_t *ai=pixman_image_create_bits((pixman_format_code_t)mc,13,7,dm,stride);assert(ai);
        pixman_image_set_alpha_map(di,ai,dest.alphaOrigin.x,dest.alphaOrigin.y);pixman_image_set_component_alpha(mi,mask.componentAlpha);
        for(unsigned i=0;i<2;++i){PicturePtr p=i?&mask:&src;pixman_image_t *ref=i?mi:si;
            p->filter=filter==2?PictFilterConvolution:filter?PictFilterBilinear:PictFilterNearest;
            pixman_image_set_repeat(ref,PIXMAN_REPEAT_REFLECT);
            if(filter){p->transform=&transform;assert(pixman_image_set_transform(ref,(pixman_transform_t*)&transform));}
            if(filter==2){p->filter_params=kernel;p->filter_nparams=4;}
            assert(pixman_image_set_filter(ref,filter==2?PIXMAN_FILTER_CONVOLUTION:filter?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,p->filter_params,p->filter_nparams));
        }
        unsigned masked=op%3,width=op%13?1:2,height=width;
        pixman_image_composite32(packed10_all_ops[op],si,masked?mi:NULL,di,2,1,3,2,3,2,width,height);
        compositor_active=1;
        if(!exa.driver.CheckComposite(packed10_all_ops[op],&src,masked?&mask:NULL,&dest) ||
            !exa.driver.PrepareComposite(packed10_all_ops[op],&src,masked?&mask:NULL,&dest,sp,masked?dp:NULL,dp)){
            fprintf(stderr,"mixed alias prepare filter=%u format=%u op=%u errno=%d\n",filter,format,packed10_all_ops[op],errno);abort();}
        fprintf(stderr,"mixed alias filter=%u format=%u map=%u op=%u\n",filter,format,mf,packed10_all_ops[op]);
        exa.driver.Composite(dp,2,1,3,2,3,2,width,height);compositor_active=0;exa.driver.DoneComposite(dp);
        map_fixture_expect(dp,dc,dm,5,format,filter);map_fixture_expect(dp,mc,dm,5,mf,filter);
        pixman_image_unref(si);pixman_image_unref(mi);pixman_image_unref(di);pixman_image_unref(ai);free(sm);free(dm);free(mm);
        coverage_test_destroy(sp);coverage_test_destroy(dp);++checks;
    }
    assert(checks==1113);printf("PS2 mixed packed10 alias filter=%u requests=%u PASS\n",filter,checks);
    ps2_blit_exa_cleanup(&xs,&blit);ps2_exa_context_bridge_cleanup(&bridge);finish_rop();
}
static void mixed_guards(void)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    assert(!ps2_surface_set_depth(gs,source_handle,32) && !ps2_surface_set_depth(gs,destination_handle(),32));
    struct ps2_picture_sample sample={.source_argb=1,.destination_argb=1,.source_packed10=1,.destination_packed10=2};
    struct ps2_pipe_copy_box box={0,0,3,2,1,1};unsigned checks=0;
    for(unsigned invalid=0;invalid<11;++invalid){struct ps2_picture_sample s=sample;
        if(invalid<4){s.source_packed10=0;if(invalid<3)s.source_unorm.rgb_bits[invalid]=9;else s.source_unorm.alpha_bits=9;}
        else if(invalid<8){s.destination_packed10=0;if(invalid<7)s.destination_unorm.rgb_bits[invalid-4]=9;else s.destination_unorm.alpha_bits=9;}
        else if(invalid==8)s.alpha_only_destination=2;
        else if(invalid==9)s.alpha_only_destination=1;
        else s.destination_unorm.rgb_bits[0]=11,s.destination_packed10=0;
        unsigned before=attempts,draws=transfer_calls;
        assert(ps2_pipe_composite_rect(ctx,source_handle,destination_handle(),&box,&s,NULL,PS2_COMPOSITE_SRC)<0 && errno==EINVAL);
        assert(attempts==before && transfer_calls==draws);++checks;
    }
    struct ps2_divide_workspace w={0};struct ps2_filter_workspace f={0};uint32_t h[36]={0},palette,small,narrow;
    assert(!ps2_divide_reserve(&w,gs) && !ps2_filter_reserve(&f,gs,3) && !ps2_surface_create(gs,16,16,32,&palette));
    assert(!ps2_surface_create(gs,1,1,32,&small) && !ps2_surface_create(gs,64,32,16,&narrow));
    for(unsigned i=0;i<36;++i)assert(!ps2_surface_create(gs,64,32,32,h+i));
    for(unsigned invalid=0;invalid<13;++invalid){uint32_t scratch[12];memcpy(scratch,h+24,48);
        if(invalid==1)scratch[1]=scratch[0];
        if(invalid==2)scratch[0]=h[0];
        if(invalid==3)scratch[0]=h[16];
        if(invalid==4)scratch[0]=w.remainder[0];
        if(invalid==5)scratch[0]=f.tap;
        if(invalid==6)scratch[0]=palette;
        if(invalid==7)scratch[0]=0;
        if(invalid==8)scratch[0]=small;
        if(invalid==9)scratch[0]=narrow;
        unsigned before=attempts,draws=transfer_calls;
        assert(ps2_packed_blend_scratch(&w,&f,h,h+8,h+16,invalid?scratch:NULL,palette,
            invalid==12?65:1,1,invalid==10?PS2_COMPOSITE_SATURATE:PS2_COMPOSITE_OVER,invalid==11?2:0)<0 && errno==EINVAL);
        assert(attempts==before && transfer_calls==draws);++checks;
    }
    for(unsigned i=0;i<24;++i)memset(model(h[i])->data,i+1,64*32*4);
    uint32_t *saved=malloc(16*64*32*4);assert(saved);
    for(unsigned i=0;i<16;++i)memcpy(saved+i*64*32,model(h[i])->data,64*32*4);
    assert(!ps2_packed_blend_scratch(&w,&f,h,h+8,h+16,h+24,palette,1,1,PS2_COMPOSITE_CLEAR,0));
    for(unsigned i=0;i<16;++i)assert(!memcmp(saved+i*64*32,model(h[i])->data,64*32*4));
    for(unsigned i=24;i<36;++i)assert(ps2_surface_lookup(gs,h[i]));
    free(saved);for(unsigned i=0;i<36;++i)assert(!ps2_surface_destroy(gs,h[i]));
    assert(!ps2_surface_destroy(gs,palette) && !ps2_surface_destroy(gs,small) && !ps2_surface_destroy(gs,narrow));
    assert(!ps2_filter_release(&f) && !ps2_divide_release(&w));
    assert(checks==24);printf("PS2 mixed preflight guards=%u borrowed_ownership_preserved=1 immutable_inputs=1 PASS\n",checks);finish_rop();
}
int main(int argc,char **argv)
{
    if(argc==2 && !strcmp(argv[1],"mixed-guards")){mixed_guards();return 0;}
    assert(argc==2);unsigned role,filter;assert(sscanf(argv[1],"mixed-%u-%u",&role,&filter)==2 && role<6 && filter<3);
    if(role==5)mixed_alias_case(filter);else mixed_case(role,filter);return 0;
}
