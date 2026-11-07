/* SPDX-License-Identifier: MIT */
#include "xorg-server.h"
#include "ps2_pipe.h"
#undef ARRAY_SIZE
#include "miline.h"
DevPrivateKeyRec miZeroLineScreenKeyRec;
#include "ps2_glyph.h"
#include "ps2_gs_bitmap.h"
#include <X11/fonts/fontstruct.h>
#include "windowstr.h"
#include "damage.h"
#include "previous-pipe.c"
static unsigned damage_calls,sync_calls,processed,submitted;
static uint32_t drawn[96][128];
static uint32_t inspected_target;
Bool dixRegisterPrivateKey(DevPrivateKey key,DevPrivateType type,unsigned size)
{
    assert((type==PRIVATE_SCREEN || type==PRIVATE_GC) && !size);
    if(!key->initialized) *key=(DevPrivateKeyRec){.initialized=TRUE,.type=type,.offset=private_slots++*sizeof(void*)};
    return TRUE;
}
void exaMarkSync(ScreenPtr s) { assert(s); ++sync_calls; }
void DamageRegionAppend(DrawablePtr d,const RegionPtr r) { assert(d && r); ++damage_calls; }
void DamageRegionProcessPending(DrawablePtr d) { assert(d); ++processed; }
int __wrap_ioctl(int fd,unsigned long op,...)
{
    va_list args; va_start(args,op); void *arg=va_arg(args,void*); va_end(args);
    int result=pipe_ioctl(fd,op,arg);
    if(!result && op==PS2GS_RENDER_SUBMIT) {
        const struct ps2gs_render_submit *s=arg; ++submitted;
        const struct ps2gs_command *c=(void*)(uintptr_t)s->commands;
        if(s->target==inspected_target) for(unsigned i=0;i<s->count;++i) {
            assert(c[i].op==PS2GS_FILL);
            unsigned x=c[i].v[0].x/16,y=c[i].v[0].y/16;
            assert(x+c[i].width<=128 && y+c[i].height<=96);
            for(unsigned j=y;j<y+c[i].height;++j) for(unsigned k=x;k<x+c[i].width;++k) drawn[j][k]=c[i].v[0].rgba;
        }
    }
    return result;
}
static int in_clip(int x,int y,const struct ps2_bitmap_box *clips,unsigned count)
{
    for(unsigned i=0;i<count;++i) if(x>=clips[i].x1 && x<clips[i].x2 && y>=clips[i].y1 && y<clips[i].y2) return 1;
    return 0;
}
static void reference(const struct ps2_bitmap_glyph *glyph,const struct ps2_bitmap_box *clips,unsigned n,int msb,
    uint32_t fg,const struct ps2_bitmap_box *background,uint32_t bg)
{
    for(int y=0;y<96;++y) for(int x=0;x<128;++x) {
        uint32_t expected=0;
        if(in_clip(x,y,clips,n)) {
            if(background && x>=background->x1 && x<background->x2 && y>=background->y1 && y<background->y2) expected=bg;
            int a=x-glyph->x,b=y-glyph->y;
            if(a>=0 && b>=0 && (unsigned)a<glyph->width && (unsigned)b<glyph->height &&
                (glyph->bits[b*glyph->stride+a/8] & (msb ? 128>>(a%8) : 1<<(a%8)))) expected=fg;
        }
        assert(drawn[y][x]==expected);
    }
}
static const unsigned char bitmap[]={0x91,0x01,0xff,0xff,0x7e,0x00,0xff,0xff,0xff,0x01,0xff,0xff};
static void bitmap_case(void)
{
    init(); uint32_t handle; assert(!ps2_surface_create(&gs,128,96,32,&handle)); inspected_target=handle;
    struct ps2_bitmap_box clips[]={{0,0,6,12},{8,0,12,12}};
    struct ps2_bitmap_glyph g={.x=2,.y=4,.width=9,.height=3,.bits=bitmap,.bytes=sizeof(bitmap),.stride=4};
    for(int msb=0;msb<=1;++msb) {
        memset(drawn,0,sizeof(drawn));
        assert(!ps2_bitmap_draw(&render,handle,&g,1,clips,2,msb,0x8000ffff,NULL,0));
        reference(&g,clips,2,msb,0x8000ffff,NULL,0);
        assert(!render.target && !render.depth && !render.count);
    }
    assert(!ps2_surface_destroy(&gs,handle)); ps2_surface_close(&gs);
}
static void invalid_case(void)
{
    init(); struct ps2_bitmap_box clip={0,0,128,96};
    struct ps2_bitmap_glyph g[2]={{.width=9,.height=3,.bits=bitmap,.bytes=sizeof(bitmap),.stride=4},
        {.width=9,.height=3,.bits=bitmap,.bytes=1,.stride=4}};
    unsigned before=calls;
    assert(ps2_bitmap_draw(&render,0,g,2,&clip,1,0,0,NULL,0)==-1 && errno==EINVAL && calls==before);
    g[1]=g[0]; g[1].stride=SIZE_MAX;
    assert(ps2_bitmap_draw(&render,0,g,2,&clip,1,0,0,NULL,0)==-1 && calls==before);
    assert(!ps2_bitmap_draw(&render,0,g,1,NULL,0,0,0,NULL,0) && calls==before);
    g[0].x=INT32_MAX;
    assert(!ps2_bitmap_draw(&render,0,g,1,&clip,1,0,0,NULL,0) && calls==before);
    ps2_surface_close(&gs);
}
static ScreenRec xs;
static PixmapRec xp;
static WindowRec window;
static GC gc;
static FontRec font;
static GCOps original;
static struct ps2_surface_exa exa;
static struct ps2_glyph_gc glyph_gc;
static CharInfoRec character;
static RegionRec clip_region;
static int owns_screen;
static void *screen_privates[16],*gc_privates[16];
static PixmapPtr get_window(WindowPtr w) { assert(w==&window); return &xp; }
static void xorg_setup(int own,int redirect)
{
    setup(own);
    owns_screen=own;
    xs=(ScreenRec){.GetScreenPixmap=get_front,.GetWindowPixmap=get_window,.devPrivates=(void*)screen_privates};
    assert(dixRegisterPrivateKey(miZeroLineScreenKey,PRIVATE_SCREEN,0));
    dixSetPrivate(&xs.devPrivates,miZeroLineScreenKey,(void*)(uintptr_t)DEFAULTZEROLINEBIAS);
    xp=(PixmapRec){.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=24,.bitsPerPixel=32,.width=720,.height=576}};
    front_pixmap=&xp;
    assert(ps2_surface_exa_prepare(&xs,&exa,ps2_pipe_context_render(ctx)->gs));
    if(redirect) {
        int pitch; void *p=exa.driver.CreatePixmap2(&xs,128,96,24,0,32,&pitch); assert(p);
        static PixmapRec front; front=xp; front_pixmap=&front;
        xp.drawable.width=128; xp.drawable.height=96; xp.devPrivates=p;
#ifdef COMPOSITE
        xp.screen_x=20; xp.screen_y=30;
#endif
        memcpy(&inspected_target,p,4);
    }
    window.drawable=(DrawableRec){.type=DRAWABLE_WINDOW,.pScreen=&xs,.depth=24,.bitsPerPixel=32,.x=20,.y=30};
    font.info.fontAscent=3; font.info.fontDescent=1;
    pixman_box16_t boxes[]={{20,30,26,42},{28,30,32,42}};
    assert(pixman_region_init_rects(&clip_region,boxes,2));
    gc=(GC){.pScreen=&xs,.font=&font,.ops=&original,.alu=GXcopy,.fillStyle=FillSolid,
        .planemask=0xffffff,.fgPixel=0xffff,.bgPixel=0xff0000,.miTranslate=1,
        .pCompositeClip=&clip_region,.devPrivates=(void*)gc_privates};
    assert(ps2_glyph_gc_attach(&gc,&glyph_gc,&exa,ctx));
    assert(!ps2_glyph_gc_attach(&gc,&glyph_gc,&exa,ctx));
    character=(CharInfoRec){.metrics={.leftSideBearing=-1,.rightSideBearing=8,.ascent=3,.descent=0,.characterWidth=10},
        .bits=(char*)bitmap};
}
static void xorg_cleanup(void)
{
    ps2_glyph_gc_detach(&glyph_gc); assert(gc.ops==&original);
    ps2_glyph_gc_detach(&glyph_gc);
    if(xp.devPrivates) exa.driver.DestroyPixmap(&xs,xp.devPrivates);
    pixman_region_fini(&clip_region);
    ctx->destroy(ctx); release_external(); screen->destroy(screen);
    if(!owns_screen) ps2_surface_close(&gs);
}
static void poly_case(int redirect)
{
    xorg_setup(0,redirect); CharInfoPtr chars[]={&character};
    uint32_t saved=render.target,depth=render.depth;
    /* Fence preceding Mesa work before glyph drawing. */
    union pipe_color_union red={.f={1,0,0,1}}; ctx->clear(ctx,PIPE_CLEAR_COLOR0,NULL,&red,1,0);
    DrawablePtr destination=&window.drawable;
#ifndef COMPOSITE
    if(redirect) {
        destination=&xp.drawable; gc.miTranslate=0;
        pixman_region_fini(&clip_region);
        pixman_box16_t boxes[]={{0,0,6,12},{8,0,12,12}};
        assert(pixman_region_init_rects(&clip_region,boxes,2));
    }
#endif
    gc.ops->PolyGlyphBlt(destination,&gc,3,7,1,chars,NULL);
    assert(damage_calls==1 && sync_calls==1 && processed==1 && render.target==saved && render.depth==depth && !render.count);
    struct ps2_bitmap_box clips[2]={{20,30,26,42},{28,30,32,42}};
    int ox=redirect ? 20 : 0,oy=redirect ? 30 : 0;
    for(unsigned i=0;i<2;++i) { clips[i].x1-=ox; clips[i].x2-=ox; clips[i].y1-=oy; clips[i].y2-=oy; }
    struct ps2_bitmap_glyph g={.x=22-ox,.y=34-oy,.width=9,.height=3,.bits=bitmap,.stride=4};
    reference(&g,clips,2,0,0x8000ffff,NULL,0);
    xorg_cleanup();
}
static void image_case(void)
{
    xorg_setup(0,0); CharInfoPtr chars[]={&character};
    gc.alu=GXxor; gc.fillStyle=FillStippled; character.metrics.characterWidth=-10;
    gc.ops->ImageGlyphBlt(&window.drawable,&gc,10,7,1,chars,NULL);
    struct ps2_bitmap_box clips[2]={{20,30,26,42},{28,30,32,42}},background={20,34,30,38};
    struct ps2_bitmap_glyph g={.x=29,.y=34,.width=9,.height=3,.bits=bitmap,.stride=4};
    reference(&g,clips,2,0,0x8000ffff,&background,0x80ff0000);
    assert(gc.alu==GXxor && gc.fillStyle==FillStippled && gc.fgPixel==0xffff);
    xorg_cleanup();
}
static void lifetime_case(void)
{
    xorg_setup(1,0); ctx->destroy(ctx); release_external(); screen->destroy(screen); assert(!closes);
    unsigned before=calls; CharInfoPtr chars[]={&character}; expect_fatal=1;
    if(!setjmp(fatal)) { gc.ops->PolyGlyphBlt(&window.drawable,&gc,3,7,1,chars,NULL); assert(0); }
    assert(calls==before && ps2_pipe_context_error(ctx)==ESHUTDOWN);
    ps2_glyph_gc_detach(&glyph_gc); assert(closes==1 && gc.ops==&original && !glyph_gc.gc);
    pixman_region_fini(&clip_region); ps2_glyph_gc_detach(&glyph_gc); assert(closes==1);
}
static void unsupported_case(void)
{
    xorg_setup(0,0); CharInfoPtr chars[]={&character}; unsigned before=calls;
    expect_fatal=1; gc.planemask=1;
    if(!setjmp(fatal)) { gc.ops->PolyGlyphBlt(&window.drawable,&gc,0,4,1,chars,NULL); assert(0); }
    gc.planemask=0xffffff; gc.alu=GXxor;
    expect_fatal=1;
    if(!setjmp(fatal)) { gc.ops->PolyGlyphBlt(&window.drawable,&gc,0,4,1,chars,NULL); assert(0); }
    gc.alu=GXcopy; character.metrics.rightSideBearing=300;
    expect_fatal=1;
    if(!setjmp(fatal)) { gc.ops->PolyGlyphBlt(&window.drawable,&gc,0,4,1,chars,NULL); assert(0); }
    assert(calls==before && !damage_calls); expect_fatal=0; xorg_cleanup();
}
static void failure_case(void)
{
    xorg_setup(1,0); CharInfoPtr chars[]={&character}; fail_ioctl=EIO; expect_fatal=1;
    if(!setjmp(fatal)) { gc.ops->PolyGlyphBlt(&window.drawable,&gc,3,7,1,chars,NULL); assert(0); }
    assert(ps2_pipe_context_error(ctx)==EIO && !render.count && !damage_calls);
    unsigned before=calls; fail_ioctl=0;
    expect_fatal=1;
    if(!setjmp(fatal)) { gc.ops->PolyGlyphBlt(&window.drawable,&gc,3,7,1,chars,NULL); assert(0); }
    xorg_cleanup(); assert(calls==before && closes==1);
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strcmp(argv[1],"bitmap")) bitmap_case(); else if(!strcmp(argv[1],"invalid")) invalid_case();
    else if(!strcmp(argv[1],"poly")) poly_case(0); else if(!strcmp(argv[1],"redirected")) poly_case(1);
    else if(!strcmp(argv[1],"image")) image_case(); else if(!strcmp(argv[1],"lifetime")) lifetime_case();
    else if(!strcmp(argv[1],"unsupported")) unsupported_case(); else if(!strcmp(argv[1],"failure")) failure_case(); else assert(0);
    puts("native Xorg glyph test PASS"); return 0;
}
