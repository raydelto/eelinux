/* SPDX-License-Identifier: MIT */
#include "previous-composite.c"
#include "ps2_plane.h"

static PixmapPtr depth30_pixmap(unsigned depth)
{
    PixmapPtr p=calloc(1,sizeof(*p));assert(p);int pitch;
    p->drawable=(DrawableRec){.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=depth,.bitsPerPixel=32,.width=13,.height=7};
    p->devPrivates=exa.driver.CreatePixmap2(&xs,13,7,depth,0,32,&pitch);assert(p->devPrivates);p->devKind=pitch;return p;
}
static void depth30_guards(void)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    unsigned before=attempts,draws=transfer_calls,features=gs->caps.features;
    gs->caps.features&=~PS2GS_HW_CHANNEL_MAP_BLIT;
    assert(ps2_surface_set_depth(gs,source_handle,30)<0 && errno==ENOTSUP);
    assert(ps2_surface_depth(ps2_surface_lookup(gs,source_handle))==24);
    gs->caps.features=features;
    assert(!ps2_surface_set_depth(gs,source_handle,30) && !ps2_surface_set_depth(gs,destination_handle(),30));
    assert(ps2_surface_depth(ps2_surface_lookup(gs,source_handle))==30 && ps2_surface_native_mask(gs,source_handle)==0x3fffffff);
    assert(ps2_surface_set_depth(gs,source_handle,31)<0 && errno==EINVAL);
    assert(ps2_surface_depth(ps2_surface_lookup(gs,source_handle))==30 && attempts==before && transfer_calls==draws);
    struct ps2_picture_sample valid={.source_packed10=PS2_PACKED10_RGB,.destination_packed10=PS2_PACKED10_BGR};
    struct ps2_pipe_copy_box box={0,0,3,2,1,1};
    struct pixels *sp=model(source_handle);
    for(unsigned invalid=0;invalid<4;++invalid){struct ps2_picture_sample s=valid;
        struct ps2_picture_bitmap m={.has_surface=1,.surface=source_handle,.width=sp->width,.height=sp->height,.sample=valid};
        if(invalid==0)s.source_argb=1,s.source_packed10=PS2_PACKED10_ARGB;
        if(invalid==1)s.destination_argb=1,s.destination_packed10=PS2_PACKED10_ABGR;
        if(invalid==2)m.sample.source_argb=1,m.sample.source_packed10=PS2_PACKED10_ARGB;
        if(invalid==3)s.destination_alpha_map=(struct ps2_picture_alpha_map){.enabled=1,.surface=source_handle,
            .width=sp->width,.height=sp->height,.rgb_bits={10,10,10},.has_alpha=1,.alpha_bits=2};
        assert(ps2_pipe_composite_rect(ctx,source_handle,destination_handle(),&box,&s,invalid==2?&m:NULL,PS2_COMPOSITE_SRC)<0 && errno==EINVAL);
        assert(attempts==before && transfer_calls==draws);
    }
    puts("PS2 depth30 preflight raw30_mask=3fffffff alpha32_rejected=4 no_submission=1 PASS");finish_rop();
}
static void depth30_core(void)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    assert(!ps2_surface_set_depth(gs,source_handle,30) && !ps2_surface_set_depth(gs,destination_handle(),30));
    struct pixels *s=model(source_handle),*d=model(destination_handle());
    const uint32_t masks[]={0x3fffffff,0x315981a1,0x3f000000,0x20000000,0xffffff,0};unsigned checks=0;
    for(unsigned alu=0;alu<16;++alu)for(unsigned mask=0;mask<6;++mask){
        for(unsigned i=0;i<s->width*s->height;++i)s->data[i]=(i*8191U+0x91714931U)^(i*0x10103U);
        for(unsigned i=0;i<d->width*d->height;++i)d->data[i]=(i*7919U+0xd3196131U)^(i*0x10103U);
        const struct ps2_pipe_copy_box box={2,1,7,5,71,43};check(alu,masks[mask],source_handle,destination_handle(),&box,1);++checks;
    }
    for(unsigned direction=0;direction<4;++direction)for(unsigned alu=0;alu<16;++alu){
        for(unsigned i=0;i<d->width*d->height;++i)d->data[i]=(i*7919U+0x73196131U)^(i*0x10103U);
        struct ps2_pipe_copy_box box={7,5,7+(direction&1?3:-3),5+(direction&2?2:-2),71,43};
        check(alu,0x3fffffff,destination_handle(),destination_handle(),&box,1);++checks;
    }
    for(unsigned bit=24;bit<30;++bit)for(unsigned alu=0;alu<16;++alu){
        for(unsigned i=0;i<s->width*s->height;++i)s->data[i]=(i*8191U+0x31714931U)^(i*0x10103U);
        for(unsigned i=0;i<d->width*d->height;++i)d->data[i]=(i*7919U+0x93196131U)^(i*0x10103U);
        size_t bytes=d->width*d->height*4;uint32_t *expected=malloc(bytes);assert(expected);memcpy(expected,d->data,bytes);
        const struct ps2_pipe_copy_box box={2,1,7,5,7,3};
        for(int y=0;y<box.height;++y)for(int x=0;x<box.width;++x){unsigned si=(box.sy+y)*s->width+box.sx+x,di=(box.dy+y)*d->width+box.dx+x;
            uint32_t colour=s->data[si]&(1U<<bit)?0x2a7139e1:0x19735a13;
            expected[di]=(expected[di]&0xc0000000)|(oracle(alu,colour,expected[di])&0x3fffffff);}
        assert(!ps2_pipe_plane_copy_boxes(ctx,source_handle,destination_handle(),&box,1,1U<<bit,0x2a7139e1,0x19735a13,0x3fffffff,alu));
        assert(!memcmp(expected,d->data,bytes));free(expected);++checks;
    }
    assert(checks==256);printf("PS2 depth30 core requests=%u all16_ALUs partial_masks overlap4 high_planes6 PASS\n",checks);finish_rop();
}
static void depth30_render(unsigned filter,unsigned aliases,unsigned maps)
{
    rgba_enabled=1;setup_rop(32);
    struct ps2_exa_context_bridge bridge={0};struct ps2_texture_exa texture={0};struct ps2_blit_exa blit={0};
    assert(ps2_exa_context_bridge_prepare(&xs,&bridge,&exa,ctx) &&
        ps2_texture_exa_prepare(&xs,&texture,&exa,ps2_pipe_context_render(ctx)) && ps2_blit_exa_prepare(&xs,&blit,&exa,ctx));
    const unsigned rgb[]={PICT_x2r10g10b10,PICT_x2b10g10r10},argb[]={PICT_a2r10g10b10,PICT_a2b10g10r10};
    PictTransform transform={.matrix={{49152,21845,173},{0,98304,-21845},{65536/19,0,65536}}};
    int32_t kernel[]={131072,65536,98303,-32767};unsigned checks=0;
    for(unsigned layout=0;layout<2;++layout)for(unsigned role=aliases?0:1;role<(aliases?4U:8U);++role)for(unsigned op=0;op<53;++op){
        unsigned format[4],depth[4];PixmapPtr pixels[4];PictureRec picture[4];pixman_image_t *ref[4];void *memory[4];
        for(unsigned i=0;i<4;++i){
            unsigned raw30=aliases || (role&(1U<<i));depth[i]=raw30?30:32;
            format[i]=raw30?rgb[(layout+i)%2]:argb[(layout+i)%2];
            if(aliases==2 && i<3)format[i]=(layout+i)%2?PICT_x8b8g8r8:PICT_x8r8g8b8;
            if(maps && i<3){depth[i]=32;format[i]=i==1?PICT_b8g8r8a8:PICT_a8r8g8b8;}
            if(maps && i==3){depth[i]=30;format[i]=rgb[layout];}
            pixels[i]=depth30_pixmap(depth[i]);map_fixture_seed(pixels[i],layout*17+role*13+op*7+i*19);
            picture[i]=(PictureRec){.pDrawable=&pixels[i]->drawable,.format=format[i],.filter=PictFilterNearest};
            ref[i]=map_fixture_image(pixels[i],format[i],memory+i);
        }
        if(aliases){
            pixman_image_unref(ref[3]);free(memory[3]);coverage_test_destroy(pixels[3]);pixels[3]=pixels[2];memory[3]=memory[2];
            ref[3]=pixman_image_create_bits((pixman_format_code_t)format[3],13,7,memory[3],52);assert(ref[3]);picture[3].pDrawable=&pixels[2]->drawable;
            picture[2].alphaMap=picture+3;picture[2].alphaOrigin=(DDXPointRec){role&1?-1:1,role&2?-1:1};
            pixman_image_set_alpha_map(ref[2],ref[3],picture[2].alphaOrigin.x,picture[2].alphaOrigin.y);
            if(role==3){for(unsigned i=0;i<2;++i){
                pixman_image_unref(ref[i]);free(memory[i]);coverage_test_destroy(pixels[i]);pixels[i]=pixels[2];
                memory[i]=malloc(52*7);assert(memory[i]);memcpy(memory[i],memory[2],52*7);
                ref[i]=pixman_image_create_bits((pixman_format_code_t)format[i],13,7,memory[i],52);assert(ref[i]);picture[i].pDrawable=&pixels[2]->drawable;
            }}
        }
        if(maps){unsigned parent=role%3;picture[parent].alphaMap=picture+3;picture[parent].alphaOrigin=(DDXPointRec){1,1};pixman_image_set_alpha_map(ref[parent],ref[3],1,1);}
        for(unsigned i=0;i<2;++i){picture[i].repeat=TRUE;picture[i].repeatType=(role+op)%4;pixman_image_set_repeat(ref[i],picture[i].repeatType);
            picture[i].filter=filter==2?PictFilterConvolution:filter?PictFilterBilinear:PictFilterNearest;
            if(filter){picture[i].transform=&transform;assert(pixman_image_set_transform(ref[i],(pixman_transform_t*)&transform));}
            if(filter==2){picture[i].filter_params=kernel;picture[i].filter_nparams=4;}
            assert(pixman_image_set_filter(ref[i],filter==2?PIXMAN_FILTER_CONVOLUTION:filter?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,picture[i].filter_params,picture[i].filter_nparams));
        }
        picture[1].componentAlpha=op&1;pixman_image_set_component_alpha(ref[1],picture[1].componentAlpha);
        unsigned masked=role&1,width=aliases?2:1,height=aliases && role!=3?2:1;
        pixman_image_composite32(packed10_all_ops[op],ref[0],masked?ref[1]:NULL,ref[2],2,1,3,2,3,2,width,height);
        compositor_active=1;
        if(!exa.driver.CheckComposite(packed10_all_ops[op],picture,masked?picture+1:NULL,picture+2) ||
            !exa.driver.PrepareComposite(packed10_all_ops[op],picture,masked?picture+1:NULL,picture+2,pixels[0],masked?pixels[1]:NULL,pixels[2])){
            fprintf(stderr,"depth30 prepare layout=%u role=%u filter=%u alias=%u maps=%u op=%u errno=%d\n",layout,role,filter,aliases,maps,packed10_all_ops[op],errno);abort();}
        fprintf(stderr,"depth30 request layout=%u role=%u filter=%u alias=%u maps=%u op=%u\n",layout,role,filter,aliases,maps,packed10_all_ops[op]);
        exa.driver.Composite(pixels[2],2,1,3,2,3,2,width,height);compositor_active=0;exa.driver.DoneComposite(pixels[2]);
        map_fixture_expect(pixels[2],format[2],memory[2],layout,role,filter);
        if(aliases || (maps && role%3==2))map_fixture_expect(pixels[3],format[3],memory[3],layout,role,filter);
        for(unsigned i=0;i<4;++i){pixman_image_unref(ref[i]);if(!aliases || i!=3)free(memory[i]);
            if(!aliases || (i<2 && role!=3) || i==2)coverage_test_destroy(pixels[i]);}
        ++checks;
    }
    assert(checks==(aliases?424U:742U));printf("PS2 depth30 Render filter=%u aliases=%u maps=%u requests=%u PASS\n",filter,aliases,maps,checks);
    ps2_blit_exa_cleanup(&xs,&blit);ps2_exa_context_bridge_cleanup(&bridge);finish_rop();
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strcmp(argv[1],"depth30-guards")){depth30_guards();return 0;}
    if(!strcmp(argv[1],"depth30-core")){depth30_core();return 0;}
    unsigned filter=strstr(argv[1],"convolution")?2:strstr(argv[1],"bilinear")?1:0;
    depth30_render(filter,strstr(argv[1],"rgb8")?2:strstr(argv[1],"alias")?1:0,strstr(argv[1],"maps")!=NULL);return 0;
}
