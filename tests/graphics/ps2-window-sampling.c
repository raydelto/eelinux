/* SPDX-License-Identifier: MIT */
#include "previous-composite.c"

/* Check the actual EXA descriptors against independent homogeneous point
 * evaluation in the original Window coordinate system. Native XRender tests
 * separately compare final GS pixels, including all six filter names. */
static void check_points(const struct ps2_picture_sample *sample,const PictTransform *t,int ox,int oy)
{
    assert(sample->transformed);
    for(int y=-11;y<=23;y+=17)for(int x=-19;x<=31;x+=25){
        int64_t local[3]={x,y,1},raw[3]={x+ox,y+oy,1},want[3]={0},got[3]={0};
        for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j){
            want[i]+=(int64_t)t->matrix[i][j]*local[j];
            got[i]+=(int64_t)sample->matrix[i][j]*raw[j];
        }
        want[0]+=(int64_t)ox*want[2];want[1]+=(int64_t)oy*want[2];
        for(unsigned i=0;i<3;++i)assert(got[i]==want[i]);
    }
}
int main(int argc,char **argv)
{
    assert(argc==2);const char *name=argv[1];
    setup_rop(32);struct ps2_exa_context_bridge bridge={0};struct ps2_texture_exa textures={0};struct ps2_blit_exa blit={0};
    assert(ps2_exa_context_bridge_prepare(&xs,&bridge,&exa,ctx));
    assert(ps2_texture_exa_prepare(&xs,&textures,&exa,ps2_pipe_context_render(ctx)));
    assert(ps2_blit_exa_prepare(&xs,&blit,&exa,ctx));
    PixmapRec backing={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=24,.bitsPerPixel=32,.width=83,.height=61},.devPrivates=(void*)&source_handle};
    WindowRec window={.drawable=backing.drawable};window.drawable.type=DRAWABLE_WINDOW;
    window.drawable.x=17;window.drawable.y=9;window.drawable.width=23;window.drawable.height=19;
    if(!strcmp(name,"negative")){window.drawable.x=-23;window.drawable.y=-31;}
    if(!strcmp(name,"extreme")){window.drawable.x=INT16_MIN;window.drawable.y=INT16_MAX;}
#ifdef COMPOSITE
    if(!strcmp(name,"redirected")){backing.screen_x=11;backing.screen_y=-5;}
#else
    assert(strcmp(name,"redirected"));
#endif
    int ox=window.drawable.x,oy=window.drawable.y;
#ifdef COMPOSITE
    ox-=backing.screen_x;oy-=backing.screen_y;
#endif
    GetWindowPixmapProcPtr previous=xs.GetWindowPixmap;
    alias_window=&window;alias_backing=&backing;xs.GetWindowPixmap=alias_window_pixmap;
    PictTransform t={.matrix={{98304,8192,-21845},{-4096,49152,21845},{0,0,65536}}};
    if(!strcmp(name,"projective") || !strcmp(name,"redirected")){t.matrix[2][0]=128;t.matrix[2][1]=-64;}
    if(!strcmp(name,"extreme"))t=(PictTransform){.matrix={{65536,0,0},{0,65536,0},{0,0,65536}}};
    if(!strcmp(name,"overflow")){t.matrix[0][0]=INT32_MAX;window.drawable.x=INT16_MIN;}
    PictureRec w={.pDrawable=&window.drawable,.format=PICT_x8b8g8r8,.filter=PictFilterNearest,.transform=&t},
        p={.pDrawable=&backing.drawable,.format=PICT_x8b8g8r8,.filter=PictFilterNearest},
        d={.pDrawable=&xp.drawable,.format=PICT_x8b8g8r8,.filter=PictFilterNearest};
    if(!strcmp(name,"repeat"))w.transform=NULL;
    unsigned checks=0;
    for(unsigned role=0;role<3;++role)for(unsigned repeat=0;repeat<4;++repeat){
        w.repeat=repeat!=0;w.repeatType=repeat;
        PicturePtr source=role==1?&p:&w,mask=role==0?NULL:role==1?&w:&w;
        unsigned before=attempts,draws=transfer_calls;
        Bool supported=exa.driver.CheckComposite(PictOpOver,source,mask,&d);
        if(!strcmp(name,"overflow")){
            assert(!supported && attempts==before && transfer_calls==draws && !ps2_pipe_context_render(ctx)->gs->error);++checks;continue;
        }
        assert(supported);
        assert(exa.driver.PrepareComposite(PictOpOver,source,mask,&d,&backing,mask?&backing:NULL,&xp));
        if(role!=1){assert(blit.sample.repeat==repeat);if(w.transform)check_points(&blit.sample,&t,ox,oy);else assert(!blit.sample.transformed);}
        if(mask){assert(blit.mask.sample.repeat==repeat);if(w.transform)check_points(&blit.mask.sample,&t,ox,oy);else assert(!blit.mask.sample.transformed);}
        exa.driver.DoneComposite(&xp);++checks;
    }
    xs.GetWindowPixmap=previous;alias_window=NULL;alias_backing=NULL;
    ps2_blit_exa_cleanup(&xs,&blit);ps2_exa_context_bridge_cleanup(&bridge);finish_rop();
    printf("Window sampling %s checks=%u PASS\n",name,checks);return 0;
}
