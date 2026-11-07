/* SPDX-License-Identifier: MIT */
#include "previous-copy.c"
#include "ps2_copy_exa.h"
static struct ps2_copy_exa copy_exa;
static struct ps2_exa_context_bridge context_bridge;
static ExaDriverRec original_exa, ordered_exa;
static int fail_registration;
Bool dixRegisterPrivateKey(DevPrivateKey key,DevPrivateType type,unsigned size)
{ return fail_registration ? FALSE : original_register_private(key,type,size); }
static void exa_setup(void)
{
    xorg_setup(1,0); ps2_glyph_gc_detach(&glyph_gc);
    original_exa=exa.driver;
    assert(ps2_exa_context_bridge_prepare(&xs,&context_bridge,&exa,ctx));
    ordered_exa=exa.driver;
    assert(ps2_copy_exa_prepare(&xs,&copy_exa,&exa,ctx));
    seed(model(0));
}
static void exa_cleanup(void)
{
    ps2_copy_exa_cleanup(&xs,&copy_exa); ps2_copy_exa_cleanup(&xs,&copy_exa);
    assert(!memcmp(&exa.driver,&ordered_exa,sizeof(ordered_exa)));
    ps2_exa_context_bridge_cleanup(&context_bridge);
    assert(!memcmp(&exa.driver,&original_exa,sizeof(original_exa)));
    pixman_region_fini(&clip_region);
    ctx->destroy(ctx); release_external(); screen->destroy(screen);
}
static PixmapRec pixmap(unsigned width,unsigned height,unsigned bpp)
{
    int pitch;unsigned depth=bpp==16 ? 15 : 24;
    void *private=exa.driver.CreatePixmap2(&xs,width,height,depth,0,bpp,&pitch);assert(private);
    return (PixmapRec){.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=depth,
        .bitsPerPixel=bpp,.width=width,.height=height},.devPrivates=private};
}
static uint32_t handle(PixmapPtr p)
{ uint32_t result=0;if(p!=&xp) memcpy(&result,p->devPrivates,4);return result; }
static void free_pixmap(PixmapPtr p) { exa.driver.DestroyPixmap(&xs,p->devPrivates);p->devPrivates=NULL; }
static void exa_copy(PixmapPtr s,PixmapPtr d,const struct ps2_pipe_copy_box *boxes,unsigned n,int xdir,int ydir)
{
    uint32_t *expected=copy_reference(handle(s),handle(d),boxes,n);
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);uint32_t target=r->target,depth=r->depth;
    assert(exa.driver.PrepareCopy(s,d,xdir,ydir,GXcopy,~(Pixel)0));
    for(unsigned i=0;i<n;++i) {
        const struct ps2_pipe_copy_box *b=boxes+i;
        exa.driver.Copy(d,b->sx,b->sy,b->dx,b->dy,b->width,b->height);
    }
    exa.driver.DoneCopy(d);assert(!copy_exa.prepared);
    assert(!memcmp(expected,model(handle(d))->data,(size_t)d->drawable.width*d->drawable.height*4));free(expected);
    assert(r->target==target && r->depth==depth && color_resource->reference.count==3 && depth_resource->reference.count==3);
}
static void exa_formats(void)
{
    exa_setup();
    for(unsigned bpp=16;bpp<=32;bpp+=16) {
        PixmapRec s=pixmap(137,79,bpp),d=pixmap(149,83,bpp);
        seed(model(handle(&s)));seed(model(handle(&d)));
        for(int w=1;w<=137;++w) {
            struct ps2_pipe_copy_box b={137-w,3,149-w,7,w,65};exa_copy(&s,&d,&b,1,1,1);
        }
        free_pixmap(&s);free_pixmap(&d);
    }
    PixmapRec tiny=pixmap(3,40,16),d=pixmap(17,40,16);seed(model(handle(&tiny)));
    struct ps2_pipe_copy_box b={0,0,7,0,3,40};exa_copy(&tiny,&d,&b,1,1,1);
    free_pixmap(&tiny);free_pixmap(&d);
    PixmapRec owned=pixmap(129,65,32);seed(model(handle(&owned)));
    b=(struct ps2_pipe_copy_box){0,0,103,129,129,65};exa_copy(&owned,&xp,&b,1,1,1);
    b=(struct ps2_pipe_copy_box){103,129,0,0,129,65};exa_copy(&xp,&owned,&b,1,1,1);
    free_pixmap(&owned);exa_cleanup();
}
static void exa_overlap(void)
{
    exa_setup();
    /* The caller supplies EXA's ordered rectangle stream. Copies within each
     * rectangle use the shared GS memmove path; the snapshot checks all bands. */
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) if(x || y) {
        seed(model(0));struct ps2_pipe_copy_box b[4];unsigned n=0;
        for(unsigned row=0;row<2;++row) for(unsigned col=0;col<2;++col) {
            int sx=100+(x>0 ? 1-col : col)*137,sy=120+(y>0 ? 1-row : row)*70;
            b[n++]=(struct ps2_pipe_copy_box){sx,sy,sx+x*7,sy+y*5,129,65};
        }
        exa_copy(&xp,&xp,b,4,x>0 ? -1 : 1,y>0 ? -1 : 1);
    }
    exa_cleanup();
}
static void exa_preflight(void)
{
    exa_setup();PixmapRec s=pixmap(129,65,32),d=pixmap(129,65,32);
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);
    assert(!ps2_render_fill(r,0,0,1,1,0));unsigned before=calls;
    for(unsigned i=0;i<17;++i) {
        int sx=0,sy=0,dx=10,dy=10,w=13,h=9;PixmapPtr dest=&d;
        assert(exa.driver.PrepareCopy(&s,&d,1,1,GXcopy,0xffffff));
        switch(i) {
        case 0:sx=-1;break;case 1:sy=-1;break;case 2:dx=-1;break;case 3:dy=-1;break;
        case 4:w=-1;break;case 5:h=-1;break;case 6:sx=INT_MAX;break;case 7:w=INT_MAX;break;
        case 8:sy=64;break;case 9:dx=128;break;case 10:dy=64;break;case 11:dest=&xp;break;
        case 12:s.drawable.width--;break;case 13:d.drawable.depth=32;break;
        case 14:copy_exa.source.handle=9999;break;case 15:copy_exa.prepared=FALSE;break;
        case 16:exa.gs=NULL;break;
        }
        expect_fatal=1;if(!setjmp(fatal)) { exa.driver.Copy(dest,sx,sy,dx,dy,w,h);assert(0); }
        assert(calls==before && r->count==1 && !render_calls && !textured_copies && !local_copies);
        s.drawable.width=129;d.drawable.depth=24;exa.gs=r->gs;copy_exa.prepared=TRUE;exa.driver.DoneCopy(&d);
    }
    /* A retired native source handle cannot be reused by the prepared copy. */
    assert(exa.driver.PrepareCopy(&s,&d,1,1,GXcopy,0xffffff));
    free_pixmap(&s);before=calls;
    expect_fatal=1;if(!setjmp(fatal)) { exa.driver.Copy(&d,0,0,10,10,13,9);assert(0); }
    assert(calls==before && r->count==0); /* DestroyPixmap fenced only the older draw. */
    exa.driver.DoneCopy(&d);free_pixmap(&d);exa_cleanup();
}
static void exa_unsupported(void)
{
    exa_setup();struct ps2_render_client *r=ps2_pipe_context_render(ctx);
    assert(!ps2_render_fill(r,0,0,1,1,0));unsigned before=calls;
    for(int alu=0;alu<16;++alu) if(alu!=GXcopy && alu!=GXnoop)
        assert(!exa.driver.PrepareCopy(&xp,&xp,1,1,alu,0xffffff));
    assert(!exa.driver.PrepareCopy(&xp,&xp,1,1,GXcopy,0xff));
    assert(!exa.driver.PrepareCopy(&xp,&xp,0,1,GXcopy,0xffffff));
    assert(!exa.driver.PrepareCopy(&xp,&xp,1,2,GXcopy,0xffffff));
    assert(!exa.driver.PrepareCopy(NULL,&xp,1,1,GXcopy,0xffffff));
    PixmapRec foreign=xp;ScreenRec other=xs;foreign.drawable.pScreen=&other;
    assert(!exa.driver.PrepareCopy(&foreign,&xp,1,1,GXcopy,0xffffff));
    foreign=xp;foreign.drawable.depth=32;
    assert(!exa.driver.PrepareCopy(&foreign,&xp,1,1,GXcopy,0xffffff));
    assert(calls==before && r->count==1 && !copy_exa.prepared);
    /* Native storage forbids EXA's CPU access path after an unsupported op. */
    expect_fatal=1;if(!setjmp(fatal)) { exa.driver.PrepareAccess(&xp,EXA_PREPARE_DEST);assert(0); }
    assert(calls==before && r->count==1);exa_cleanup();
}
static void exa_shared(void)
{
    exa_setup();texture_setup();struct ps2_render_client *r=ps2_pipe_context_render(ctx);
    assert(!ps2_render_fill(r,0,0,1,1,0));unsigned before=calls;
    for(unsigned i=0;i<4;++i) {
        int alu=i==0 ? GXnoop : GXcopy;Pixel mask=i==1 ? 0xff000000 : 0xffffff;
        assert(exa.driver.PrepareCopy(&xp,&xp,1,1,alu,mask));
        if(i<2) exa.driver.Copy(&xp,0,0,13,9,13,9);
        else if(i==2) exa.driver.Copy(&xp,0,0,13,9,0,0);
        else exa.driver.Copy(&xp,0,0,0,0,13,9);
        exa.driver.DoneCopy(&xp);assert(calls==before && r->count==1);
    }
    struct ps2_pipe_copy_box b={12,10,23,29,13,9};exa_copy(&xp,&xp,&b,1,1,1);
    assert(render_calls==1 && !r->count && texture_view->reference.count==2);
    exa_cleanup();assert(closes==1);
}
static void exa_lifetime(void)
{
    exa_setup();ctx->destroy(ctx);release_external();screen->destroy(screen);unsigned before=calls;
    expect_fatal=1;if(!setjmp(fatal)) { exa.driver.PrepareCopy(&xp,&xp,1,1,GXcopy,0xffffff);assert(0); }
    assert(calls==before && !closes);
    ps2_copy_exa_cleanup(&xs,&copy_exa);assert(!closes && !copy_exa.attached);
    ps2_exa_context_bridge_cleanup(&context_bridge);assert(closes==1);
    assert(!memcmp(&exa.driver,&original_exa,sizeof(original_exa)));
    pixman_region_fini(&clip_region);
}
static void exa_failure(void)
{
    for(volatile unsigned mode=0;mode<2;++mode) {
        exa_setup();struct ps2_render_client *r=ps2_pipe_context_render(ctx);
        assert(!ps2_render_fill(r,0,0,1,1,0));unsigned submitted=render_calls,copied=local_copies+textured_copies;
        assert(exa.driver.PrepareCopy(&xp,&xp,1,1,GXcopy,0xffffff));
        if(!mode) fail_create=1;else fail_copy_at=2;
        expect_fatal=1;if(!setjmp(fatal)) { exa.driver.Copy(&xp,0,0,20,20,13,9);assert(0); }
        if(!mode) assert(r->count==1 && render_calls==submitted && local_copies+textured_copies==copied);
        unsigned before=calls;
        expect_fatal=1;if(!setjmp(fatal)) { exa.driver.Copy(&xp,0,0,20,20,13,9);assert(0); }
        assert(calls==before);fail_create=0;fail_copy_at=0;exa_cleanup();assert(calls==before && closes==mode+1);
    }
}
static void exa_installation(void)
{
    xorg_setup(1,0);ps2_glyph_gc_detach(&glyph_gc);original_exa=exa.driver;
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);uint32_t features=r->gs->caps.features;
    r->gs->caps.features&=~PS2GS_HW_COPY;
    assert(!ps2_copy_exa_prepare(&xs,&copy_exa,&exa,ctx));r->gs->caps.features=features;
    exa.driver.DoneCopy=NULL;assert(!ps2_copy_exa_prepare(&xs,&copy_exa,&exa,ctx));exa.driver=original_exa;
    struct ps2_surface_exa foreign=exa;struct ps2_surface_client owner={0};foreign.gs=&owner;
    assert(!ps2_copy_exa_prepare(&xs,&copy_exa,&foreign,ctx));
    fail_registration=1;assert(!ps2_copy_exa_prepare(&xs,&copy_exa,&exa,ctx));fail_registration=0;
    assert(!memcmp(&exa.driver,&original_exa,sizeof(original_exa)) && !copy_exa.attached);
    assert(ps2_copy_exa_prepare(&xs,&copy_exa,&exa,ctx));
    struct ps2_copy_exa duplicate={0};assert(!ps2_copy_exa_prepare(&xs,&duplicate,&exa,ctx));
    assert(exa.driver.PrepareCopy(&xp,&xp,1,1,GXcopy,0xffffff));
    PixmapRec wrong=xp;expect_fatal=1;
    if(!setjmp(fatal)) { exa.driver.DoneCopy(&wrong);assert(0); }
    assert(copy_exa.prepared);exa.driver.DoneCopy(&xp);
    void (*saved)(PixmapPtr)=exa.driver.DoneCopy;exa.driver.DoneCopy=original_exa.DoneCopy;
    expect_fatal=1;if(!setjmp(fatal)) { ps2_copy_exa_cleanup(&xs,&copy_exa);assert(0); }
    assert(copy_exa.attached);exa.driver.DoneCopy=saved;
    ps2_copy_exa_cleanup(&xs,&copy_exa);assert(!memcmp(&exa.driver,&original_exa,sizeof(original_exa)));
    assert(ps2_copy_exa_prepare(&xs,&copy_exa,&exa,ctx));ps2_copy_exa_cleanup(&xs,&copy_exa);
    pixman_region_fini(&clip_region);ctx->destroy(ctx);release_external();screen->destroy(screen);
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strcmp(argv[1],"formats")) exa_formats();else if(!strcmp(argv[1],"overlap")) exa_overlap();
    else if(!strcmp(argv[1],"preflight")) exa_preflight();else if(!strcmp(argv[1],"unsupported")) exa_unsupported();
    else if(!strcmp(argv[1],"shared")) exa_shared();else if(!strcmp(argv[1],"lifetime")) exa_lifetime();
    else if(!strcmp(argv[1],"failure")) exa_failure();else if(!strcmp(argv[1],"installation")) exa_installation();
    else assert(0);
    puts("native EXA copy adapter test PASS");return 0;
}
