/* SPDX-License-Identifier: MIT */
#include "previous-stipple.c"
static PixmapRec owned_mono;
static void create_mono(unsigned w,unsigned h)
{
    int pitch=-1;
    owned_mono=(PixmapRec){.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=1,.bitsPerPixel=1}};
    owned_mono.devPrivates=exa.driver.CreatePixmap2(&xs,w,h,1,0,1,&pitch);
    assert(owned_mono.devPrivates);
    assert(exa.driver.ModifyPixmapHeader(&owned_mono,w,h,1,1,pitch,NULL));
    owned_mono.devPrivate.ptr=NULL; /* Actual EXA hides it after ModifyPixmapHeader. */
}
static void destroy_mono(void)
{
    exa.driver.DestroyPixmap(&xs,owned_mono.devPrivates);
    owned_mono.devPrivates=NULL;
}
static struct ps2_mono_source mono_view(void)
{
    struct ps2_mono_source source;
    assert(ps2_surface_exa_mono_source(&owned_mono,&source)==1);
    return source;
}
static void allocation_case(void)
{
    mono_setup(0,32,17,3);unsigned before=calls,live=live_models();
    create_mono(17,3);struct ps2_mono_source source=mono_view();
    assert(source.width==17 && source.height==3 && source.stride==4 && source.bytes==12);
    for(size_t i=0;i<source.bytes;++i) assert(!source.bits[i]);
    assert(exa.driver.PixmapIsOffscreen(&owned_mono));
    assert(!exa.driver.PrepareSolid(&owned_mono,GXcopy,1,1));
    assert(!exa.driver.PrepareCopy(&owned_mono,&xp,1,1,GXcopy,~0U));
    assert(!exa.driver.UploadToScreen(&owned_mono,0,0,17,3,(char*)mono_bits,4));
    assert(calls==before && live_models()==live && exa.monochrome_count==1 && exa.monochrome_bytes==12);
    destroy_mono();assert(!exa.monochrome_count && !exa.monochrome_bytes && !exa.monochrome);
    create_mono(0,0);source=mono_view();assert(!source.bits && !source.bytes && !source.stride);
    destroy_mono();fill_cleanup();
}
static void budget_case(void)
{
    mono_setup(0,32,2,2);void *blocks[2];int pitch;
    unsigned before=calls;
    for(unsigned i=0;i<2;++i) {blocks[i]=exa.driver.CreatePixmap2(&xs,2048,2048,1,0,1,&pitch);assert(blocks[i] && pitch==256);}
    assert(exa.monochrome_bytes==1048576);
    assert(!exa.driver.CreatePixmap2(&xs,1,1,1,0,1,&pitch));
    exa.driver.DestroyPixmap(&xs,blocks[0]);create_mono(2048,2048);destroy_mono();
    exa.driver.DestroyPixmap(&xs,blocks[1]);assert(calls==before && !exa.monochrome_bytes && !exa.monochrome_count);
    fill_cleanup();
}
static void count_case(void)
{
    mono_setup(0,32,2,2);void *blocks[1024];int pitch;
    for(unsigned i=0;i<1024;++i) {blocks[i]=exa.driver.CreatePixmap2(&xs,0,0,1,0,1,&pitch);assert(blocks[i]);}
    assert(!exa.driver.CreatePixmap2(&xs,0,0,1,0,1,&pitch));
    exa.driver.DestroyPixmap(&xs,blocks[400]);blocks[400]=exa.driver.CreatePixmap2(&xs,1,1,1,0,1,&pitch);assert(blocks[400]);
    for(unsigned i=0;i<1024;++i) exa.driver.DestroyPixmap(&xs,blocks[i]);
    assert(!exa.monochrome && !exa.monochrome_count && !exa.monochrome_bytes);fill_cleanup();
}
static void headers_case(void)
{
    mono_setup(0,32,17,3);create_mono(17,3);struct ps2_mono_source source=mono_view();
    assert(!exa.driver.ModifyPixmapHeader(&owned_mono,18,3,1,1,4,NULL));
    assert(!exa.driver.ModifyPixmapHeader(&owned_mono,17,4,1,1,4,NULL));
    assert(!exa.driver.ModifyPixmapHeader(&owned_mono,17,3,24,32,4,NULL));
    assert(!exa.driver.ModifyPixmapHeader(&owned_mono,17,3,1,1,8,NULL));
    assert(!exa.driver.ModifyPixmapHeader(&owned_mono,17,3,1,1,4,mono_bits));
    PixmapRec saved=owned_mono;
    owned_mono.devKind=8;assert(ps2_surface_exa_mono_source(&owned_mono,&source)==-1);
    assert(!exa.driver.PixmapIsOffscreen(&owned_mono));owned_mono=saved;
    owned_mono.drawable.depth=24;assert(ps2_surface_exa_mono_source(&owned_mono,&source)==-1);owned_mono=saved;
    owned_mono.devPrivate.ptr=mono_bits;assert(ps2_surface_exa_mono_source(&owned_mono,&source)==-1);owned_mono=saved;
    source=mono_view();assert(exa.driver.ModifyPixmapHeader(&owned_mono,0,0,0,0,0,(void*)source.bits));
    destroy_mono();fill_cleanup();
}
static void access_case(void)
{
    mono_setup(0,32,9,3);create_mono(9,3);unsigned before=calls;
    expect_fatal=1;if(!setjmp(fatal)){exa.driver.PrepareAccess(&owned_mono,EXA_PREPARE_DEST);assert(0);}
    assert(!owned_mono.devPrivate.ptr && mono_view().bits[0]==0 && calls==before);
    expect_fatal=1;if(!setjmp(fatal)) {exa.driver.PrepareAccess(&xp,EXA_PREPARE_DEST);assert(0);}
    assert(calls==before);destroy_mono();fill_cleanup();
}
static void alpha_storage(void)
{
    mono_setup(0,32,9,3);int pitch;unsigned before=calls;
    PixmapRec p={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=8,.bitsPerPixel=8}};
    p.devPrivates=exa.driver.CreatePixmap2(&xs,17,3,8,0,8,&pitch);assert(p.devPrivates && pitch==20);
    assert(exa.driver.ModifyPixmapHeader(&p,17,3,8,8,pitch,NULL));p.devPrivate.ptr=NULL;
    struct ps2_alpha_source view;struct ps2_mono_source mono;
    assert(ps2_surface_exa_alpha_source(&p,&view)==1 && view.bits_per_pixel==8 && view.bytes==60);
    assert(ps2_surface_exa_mono_source(&p,&mono)==0 && exa.driver.PixmapIsOffscreen(&p));
    for(unsigned i=0;i<view.bytes;++i)assert(!view.bits[i]);
    expect_fatal=1;if(!setjmp(fatal)){exa.driver.PrepareAccess(&p,EXA_PREPARE_DEST);assert(0);}
    assert(!p.devPrivate.ptr && view.bits[0]==0);
    assert(!exa.driver.ModifyPixmapHeader(&p,17,3,1,1,pitch,NULL));
    p.devKind=24;assert(ps2_surface_exa_alpha_source(&p,&view)==-1);p.devKind=pitch;
    assert(calls==before && exa.monochrome_bytes==60);
    exa.driver.DestroyPixmap(&xs,p.devPrivates);assert(!exa.monochrome_bytes && !exa.monochrome_count);
    fill_cleanup();
}
static void use_owned(void)
{
    create_mono(mono_pixmap.drawable.width,mono_pixmap.drawable.height);
    struct ps2_mono_source source=mono_view();memcpy((void*)source.bits,mono_bits,source.bytes);
    gc.stipple=&owned_mono;
}
static void pixels_case(void)
{
    for(int bpp=16;bpp<=32;bpp+=16) {
        mono_setup(1,bpp,17,9);use_owned();unsigned before=tile_submits;
        xRectangle r[]={{-3,-1,63,47},{23,19,54,32}};
        mono_reference(&window.drawable,r,2,NULL,NULL,0,0);
        assert(!owned_mono.devPrivate.ptr && tile_submits>before);
        DDXPointRec p[]={{25,34},{40,55},{71,66}};int widths[]={80,32,42};
        mono_reference(&window.drawable,NULL,3,p,widths,0,0);
        destroy_mono();fill_cleanup();
    }
}
static void edit_case(void)
{
    mono_setup(1,32,21,15);use_owned();xRectangle r={0,0,90,70};
    for(unsigned i=0;i<4;++i) {
        struct ps2_mono_source source=mono_view();
        for(size_t j=0;j<source.bytes;++j) mono_bits[j]=((i+j)*37)^0xa5;
        /* Simulated raw client source bytes; no CPU raster access. */
        memcpy((void*)source.bits,mono_bits,source.bytes);
        gc.patOrg=(DDXPointRec){(int)i-3,7-(int)i};
        gc.funcs->ValidateGC(&gc,GCAllBits,&window.drawable);
        mono_reference(&window.drawable,&r,1,NULL,NULL,0,0);
    }
    destroy_mono();fill_cleanup();
}
static unsigned mono_draws;
static void bitmap_validate(GCPtr g,unsigned long mask,DrawablePtr d)
{ lower_state(g);assert(mask==(GCAllBits&~(GCTile|GCStipple)) && d==&owned_mono.drawable);replace_tables(g); }
static void bitmap_rect(DrawablePtr d,GCPtr g,int n,xRectangle *r)
{
    lower_state(g);assert(d==&owned_mono.drawable && g->depth==1 && n==1);
    assert(exa.driver.PrepareAccess(&owned_mono,EXA_PREPARE_DEST));
    for(unsigned y=r->y;y<(unsigned)r->y+r->height;++y) for(unsigned x=r->x;x<(unsigned)r->x+r->width;++x)
        ((unsigned char*)owned_mono.devPrivate.ptr)[y*owned_mono.devKind+x/8]|=1U<<(x%8);
    exa.driver.FinishAccess(&owned_mono,EXA_PREPARE_DEST);++mono_draws;replace_tables(g);
}
static int bitmap_text(DrawablePtr d,GCPtr g,int x,int y,int n,char *chars)
{ lower_state(g);assert(d==&owned_mono.drawable && chars && n==1 && y==0);++mono_draws;replace_tables(g);return x+7; }
static void gc_case(void)
{
    mono_setup(1,32,17,9);create_mono(17,9);
    void *privates[16]={0};GC mg={.pScreen=&xs,.depth=1,.devPrivates=(void*)privates};
    assert(xs.CreateGC(&mg));
    for(int i=0;i<2;++i) {lower_funcs[i].ValidateGC=bitmap_validate;lower_ops[i].PolyFillRect=bitmap_rect;lower_ops[i].PolyText8=bitmap_text;}
    mg.funcs->ValidateGC(&mg,GCAllBits,&owned_mono.drawable);unsigned before=calls;
    xRectangle r={2,1,9,3};for(int i=0;i<3;++i) {
        expect_fatal=1;if(!setjmp(fatal)){mg.ops->PolyFillRect(&owned_mono.drawable,&mg,1,&r);assert(0);}
        expect_fatal=1;if(!setjmp(fatal)){mg.ops->PolyText8(&owned_mono.drawable,&mg,5,0,1,"A");assert(0);}
        mg.funcs->ChangeGC(&mg,GCForeground);mg.funcs->ValidateGC(&mg,GCAllBits,&owned_mono.drawable);
    }
    assert(mono_draws==0 && calls==before && !owned_mono.devPrivate.ptr);
    struct ps2_mono_source source=mono_view();
    for(unsigned y=0;y<9;++y) for(unsigned x=0;x<17;++x)
        assert(!(source.bits[y*source.stride+x/8]&(1U<<(x%8))));
    mg.funcs->DestroyGC(&mg);assert(gc_screen.count==1);
    destroy_mono();fill_cleanup();
}
static void mono_failure_case(void)
{
    mono_setup(0,32,2,2);unsigned before=calls;int pitch=99;
    allocation_failure=1;assert(!exa.driver.CreatePixmap2(&xs,17,3,1,0,1,&pitch));
    assert(!exa.monochrome_count && !exa.monochrome_bytes && calls==before);
    mono_calloc_countdown=2;assert(!exa.driver.CreatePixmap2(&xs,17,3,1,0,1,&pitch));
    assert(!exa.monochrome_count && !exa.monochrome_bytes && calls==before);
    create_mono(17,3);gs.error=EIO;destroy_mono();gs.error=0;
    assert(!exa.monochrome_count && !exa.monochrome_bytes && calls==before);fill_cleanup();
}
static void foreign_case(void)
{
    mono_setup(1,32,17,3);use_owned();struct ps2_mono_source source;PixmapRec saved=owned_mono;
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,1,1,0));
    xRectangle box={0,0,50,50};owned_mono.devKind=8;fill_reject(1,&box,NULL,NULL,0);owned_mono=saved;
    owned_mono.drawable.width=18;fill_reject(1,&box,NULL,NULL,0);owned_mono=saved;
    ScreenRec foreign={0};owned_mono.drawable.pScreen=&foreign;fill_reject(1,&box,NULL,NULL,0);owned_mono=saved;
    assert(ps2_surface_exa_mono_source(&xp,&source)==0);
    assert(r->count==1 && !damage_calls);r->count=0;destroy_mono();fill_cleanup();
}
static void borrowed_case(unsigned guards)
{
    mono_setup(0,32,9,3);unsigned before=calls;
    const unsigned depths[]={1,4,8,16};
    for(unsigned k=0;k<4;++k){
        unsigned char data[128],other[128],saved[128];
        for(unsigned i=0;i<128;++i)data[i]=saved[i]=i*73+29,other[i]=i*37+11;
        unsigned depth=depths[k],bpp=k==3?16:k?8:1,stride=k==3?16:k?8:4;int pitch;
        PixmapRec p={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=depth,.bitsPerPixel=bpp}};
        p.devPrivates=exa.driver.CreatePixmap2(&xs,0,0,depth,0,bpp,&pitch);assert(p.devPrivates);
        assert(exa.driver.ModifyPixmapHeader(&p,7,5,depth,k==1?4:bpp,stride,data));
        p.devPrivate.ptr=NULL; /* EXA keeps its sys_ptr hidden. */
        struct ps2_alpha_source view;
        assert(ps2_surface_exa_alpha_source(&p,&view)==0);
        assert(ps2_surface_exa_borrowed_source(&p,&view)==1 && view.bits==data && view.bytes==stride*5 &&
            view.width==7 && view.height==5 && view.depth==depth && view.bits_per_pixel==bpp);
        assert(!exa.monochrome_bytes && exa.monochrome_count==1 && calls==before);
        if(guards){
            PixmapRec good=p;p.drawable.width=8;assert(ps2_surface_exa_borrowed_source(&p,&view)==-1);p=good;
            p.devKind+=4;assert(ps2_surface_exa_borrowed_source(&p,&view)==-1);p=good;
            assert(!exa.driver.ModifyPixmapHeader(&p,1025,5,depth,bpp,stride,other));
            assert(!exa.driver.ModifyPixmapHeader(&p,9,5,depth,bpp,1,other));
            assert(!exa.driver.ModifyPixmapHeader(&p,7,1024,depth,bpp,2048,other));
            assert(ps2_surface_exa_borrowed_source(&p,&view)==1 && view.bits==data);
            expect_fatal=1;if(!setjmp(fatal)){exa.driver.PrepareAccess(&p,EXA_PREPARE_DEST);assert(0);}
            assert(!p.devPrivate.ptr && !memcmp(saved,data,128));
        }else{
            unsigned next_stride=k==3?20:k?12:4;
            assert(exa.driver.ModifyPixmapHeader(&p,9,3,depth,bpp,next_stride,other));p.devPrivate.ptr=NULL;
            assert(ps2_surface_exa_borrowed_source(&p,&view)==1 && view.bits==other && view.bytes==next_stride*3);
            assert(!memcmp(saved,data,128));
            (void)exa.driver.ModifyPixmapHeader(&p,9,3,depth,bpp,next_stride,NULL);
            assert(ps2_surface_exa_borrowed_source(&p,&view)==0);
        }
        if(!guards){
            const unsigned recycled_depths[]={8,1,16,4,1,8,16,4};
            for(unsigned j=0;j<8;++j){
                unsigned rd=recycled_depths[j],rb=rd==1?1:rd==16?16:8,rs=rd==1?4:rd==16?16:8;
                assert(exa.driver.ModifyPixmapHeader(&p,7,5,rd,rd,-1,data));
                p.devPrivate.ptr=NULL;
                assert(ps2_surface_exa_borrowed_source(&p,&view)==1 && view.depth==rd &&
                    view.bits_per_pixel==rb && view.stride==rs && view.bits==data);
                assert(!memcmp(saved,data,128));
            }
        }
        exa.driver.DestroyPixmap(&xs,p.devPrivates);
        assert(!exa.monochrome_count && !exa.monochrome_bytes && calls==before && !memcmp(data,saved,128));
    }
    fill_cleanup();
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strcmp(argv[1],"borrowed-lifetime"))borrowed_case(0);
    else if(!strcmp(argv[1],"borrowed-guards"))borrowed_case(1);
    else if(!strcmp(argv[1],"allocation")) allocation_case();else if(!strcmp(argv[1],"budget")) budget_case();
    else if(!strcmp(argv[1],"count")) count_case();else if(!strcmp(argv[1],"headers")) headers_case();
    else if(!strcmp(argv[1],"access")) access_case();else if(!strcmp(argv[1],"pixels")) pixels_case();
    else if(!strcmp(argv[1],"source-edit")) edit_case();else if(!strcmp(argv[1],"gc")) gc_case();
    else if(!strcmp(argv[1],"failure")) mono_failure_case();else if(!strcmp(argv[1],"foreign")) foreign_case();
    else if(!strcmp(argv[1],"alpha-storage"))alpha_storage();else assert(0);
    puts("source-only monochrome pixmap case PASS");return 0;
}
