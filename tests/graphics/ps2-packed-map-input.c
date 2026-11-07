/* SPDX-License-Identifier: MIT */
#include "previous-composite.c"

int main(int argc,char **argv)
{
    assert(argc==2 && !strcmp(argv[1],"write-input-alias"));
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    assert(!ps2_surface_set_depth(gs,source_handle,32) && !ps2_surface_set_depth(gs,destination_handle(),32));xp.drawable.depth=32;
    uint32_t image;assert(!ps2_surface_create(gs,31,23,32,&image) && !ps2_surface_set_depth(gs,image,32));
    struct ps2_exa_context_bridge bridge={0};struct ps2_texture_exa texture={0};struct ps2_blit_exa blit={0};
    assert(ps2_exa_context_bridge_prepare(&xs,&bridge,&exa,ctx) && ps2_texture_exa_prepare(&xs,&texture,&exa,ps2_pipe_context_render(ctx)) && ps2_blit_exa_prepare(&xs,&blit,&exa,ctx));
    struct pixels *sp=model(source_handle),*dp=model(destination_handle()),*ip=model(image);
    PixmapRec map_pixmap={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=32,.bitsPerPixel=32,.width=sp->width,.height=sp->height},.devPrivates=(void*)&source_handle},
        image_pixmap={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=32,.bitsPerPixel=32,.width=ip->width,.height=ip->height},.devPrivates=(void*)&image};
    const unsigned codes[]={PICT_a2r10g10b10,PICT_a2b10g10r10,PICT_x2r10g10b10,PICT_x2b10g10r10};unsigned checks=0;
    for(unsigned layout=0;layout<4;++layout)for(unsigned role=0;role<3;++role)for(unsigned op=0;op<53;++op){
        rgba_seed(sp);rgba_seed(dp);rgba_seed(ip);
        PixmapPtr source=role==0?&map_pixmap:role==1?&image_pixmap:&xp;
        struct pixels *pixels=role==0?sp:role==1?ip:dp;
        PictureRec src={.pDrawable=&source->drawable,.format=codes[(layout+2)%4],.filter=PictFilterNearest,.repeat=TRUE,.repeatType=RepeatReflect},
            mask={.pDrawable=&map_pixmap.drawable,.format=codes[(layout+3)%4],.filter=PictFilterNearest,.repeat=TRUE,.repeatType=RepeatReflect,.componentAlpha=role==2},
            map={.pDrawable=&map_pixmap.drawable,.format=codes[layout],.filter=PictFilterNearest},
            dest={.pDrawable=&xp.drawable,.format=codes[(layout+1)%4],.filter=PictFilterNearest,.alphaMap=&map,.alphaOrigin={1,1}};
        void *sm=malloc((size_t)pixels->width*pixels->height*4),*dm=malloc((size_t)dp->width*dp->height*4),
            *mm=malloc((size_t)sp->width*sp->height*4),*am=malloc((size_t)sp->width*sp->height*4);assert(sm && dm && mm && am);
        memcpy(sm,pixels->data,(size_t)pixels->width*pixels->height*4);memcpy(dm,dp->data,(size_t)dp->width*dp->height*4);
        memcpy(mm,sp->data,(size_t)sp->width*sp->height*4);memcpy(am,sp->data,(size_t)sp->width*sp->height*4);
        pixman_image_t *si=pixman_image_create_bits((pixman_format_code_t)src.format,pixels->width,pixels->height,sm,pixels->width*4),
            *di=pixman_image_create_bits((pixman_format_code_t)dest.format,dp->width,dp->height,dm,dp->width*4),
            *mi=pixman_image_create_bits((pixman_format_code_t)mask.format,sp->width,sp->height,mm,sp->width*4),
            *ai=pixman_image_create_bits((pixman_format_code_t)map.format,sp->width,sp->height,am,sp->width*4);assert(si && di && mi && ai);
        pixman_image_set_repeat(si,PIXMAN_REPEAT_REFLECT);pixman_image_set_repeat(mi,PIXMAN_REPEAT_REFLECT);
        pixman_image_set_component_alpha(mi,mask.componentAlpha);pixman_image_set_alpha_map(di,ai,1,1);
        /* Source/mask aliases have the driver's immutable-input contract;
         * destination and its writable map remain live across scanlines. */
        pixman_image_composite32(packed10_all_ops[op],si,role?mi:NULL,di,2,0,2,0,3,2,2,2);
        compositor_active=1;
        assert(exa.driver.CheckComposite(packed10_all_ops[op],&src,role?&mask:NULL,&dest) &&
            exa.driver.PrepareComposite(packed10_all_ops[op],&src,role?&mask:NULL,&dest,source,role?&map_pixmap:NULL,&xp));
        exa.driver.Composite(&xp,2,0,2,0,3,2,2,2);compositor_active=0;exa.driver.DoneComposite(&xp);
        for(unsigned i=0;i<dp->width*dp->height;++i)if(dp->data[i]!=((uint32_t*)dm)[i]){
            fprintf(stderr,"mapped input parent layout=%u role=%u op=%u i=%u expected=%08x actual=%08x\n",layout,role,packed10_all_ops[op],i,((uint32_t*)dm)[i],dp->data[i]);abort();}
        for(unsigned i=0;i<sp->width*sp->height;++i)if(sp->data[i]!=((uint32_t*)am)[i]){
            fprintf(stderr,"mapped input store layout=%u role=%u op=%u i=%u expected=%08x actual=%08x\n",layout,role,packed10_all_ops[op],i,((uint32_t*)am)[i],sp->data[i]);abort();}
        ++checks;pixman_image_unref(si);pixman_image_unref(mi);pixman_image_unref(di);pixman_image_unref(ai);free(sm);free(dm);free(mm);free(am);
    }
    assert(checks==636);printf("PS2 packed10 mapped input aliases requests=%u both_writable_backings=1 PASS\n",checks);
    ps2_blit_exa_cleanup(&xs,&blit);ps2_exa_context_bridge_cleanup(&bridge);assert(!ps2_surface_destroy(gs,image));finish_rop();return 0;
}
