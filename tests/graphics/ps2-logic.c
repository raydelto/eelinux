/* SPDX-License-Identifier: MIT */
#include "ps2_logic.h"
#undef ARRAY_SIZE
#include "ps2_logic_exa.h"
#include "previous-logic-pipe.c"
#include <limits.h>
static struct ps2gs_solid_logic last_logic;
static unsigned logic_calls;
static int advertised=1;
int __wrap_ioctl(int fd,unsigned long op,...)
{
    va_list args; va_start(args,op); void *arg=va_arg(args,void*); va_end(args);
    if(op!=PS2GS_SOLID_LOGIC) {
        int r=pipe_ioctl(fd,op,arg);
        if(!r && op==PS2GS_GET_CAPS && advertised) ((struct ps2gs_caps*)arg)->features|=PS2GS_HW_SOLID_LOGIC;
        return r;
    }
    assert(fd==999); ++calls;
    if(retry_ioctl) { --retry_ioctl; errno=EAGAIN; return -1; }
    if(fail_ioctl) { errno=fail_ioctl; return -1; }
    last_logic=*(struct ps2gs_solid_logic*)arg; ++logic_calls;
    assert(last_logic.abi==1 && !last_logic.reserved && last_logic.alu<16);
    return 0;
}
static void transport(void)
{
    assert(!ps2_surface_open(&gs,"/dev/ps2-gs")); retry_ioctl=2;
    assert(!ps2_surface_solid_logic(&gs,0,1,2,3,4,0x123456,0xffffff,6));
    assert(sleeps==2 && logic_calls==1 && last_logic.color==0x123456 && last_logic.x==1);
    fail_ioctl=EIO; unsigned before=calls;
    assert(ps2_surface_solid_logic(&gs,0,0,0,1,1,0,1,3)==-1 && errno==EIO && calls==before+1);
    fail_ioctl=0; before=calls;
    assert(ps2_surface_solid_logic(&gs,0,0,0,1,1,0,1,3)==-1 && errno==EIO && calls==before);
    ps2_surface_close(&gs);
}
static ScreenRec xs;
static PixmapRec front;
static void *private_storage[16];
static struct ps2_surface_exa exa;
static struct ps2_exa_context_bridge bridge;
static struct ps2_logic_exa logic;
static void xsetup(void)
{
    xs=(ScreenRec){.GetScreenPixmap=get_front,.devPrivates=(void*)private_storage};
    front=(PixmapRec){.drawable={.pScreen=&xs,.depth=24,.bitsPerPixel=32,.width=720,.height=576}};
    front_pixmap=&front;
    assert(ps2_surface_exa_prepare(&xs,&exa,ps2_pipe_context_render(ctx)->gs));
    assert(ps2_exa_context_bridge_prepare(&xs,&bridge,&exa,ctx));
}
static void logic_exa_case(void)
{
    setup(1); xsetup(); assert(ps2_logic_exa_prepare(&xs,&logic,&exa,ctx));
    assert(!ps2_logic_exa_prepare(&xs,&logic,&exa,ctx));
    union pipe_color_union red={.f={1,0,0,1}};
    ctx->clear(ctx,PIPE_CLEAR_COLOR0,NULL,&red,1,0); assert(!render_calls);
    for(unsigned alu=0;alu<16;++alu) {
        assert(exa.driver.PrepareSolid(&front,alu,0xff124356,0xaadebc9a));
        exa.driver.Solid(&front,1,2,4,6);
        assert(last_logic.alu==alu && last_logic.color==0xdebc9a && last_logic.plane_mask==0x124356);
        assert(last_logic.handle==0 && last_logic.width==3 && last_logic.height==4);
    }
    assert(render_calls==1 && logic_calls==16);
    unsigned before=calls;
    assert(!exa.driver.PrepareSolid(&front,16,~0U,0));
    exa.driver.Solid(&front,1,1,1,2); assert(calls==before);
    /* Native RGB555 masks and offscreen handles use the same callback path. */
    int pitch; void *p=exa.driver.CreatePixmap2(&xs,17,9,15,0,16,&pitch); assert(p);
    PixmapRec off={.drawable={.pScreen=&xs,.depth=15,.bitsPerPixel=16,.width=17,.height=9},.devPrivates=p};
    assert(exa.driver.PrepareSolid(&off,6,0xffab,0xffff)); exa.driver.Solid(&off,0,0,17,9);
    assert(last_logic.handle==*(uint32_t*)p && last_logic.plane_mask==0x7fab && last_logic.color==0x7fff);
    exa.driver.DestroyPixmap(&xs,p);
    ps2_logic_exa_cleanup(&xs,&logic); ps2_exa_context_bridge_cleanup(&bridge);
    ctx->destroy(ctx); release_external(); screen->destroy(screen); assert(closes==1);
}
static void logic_mesa_case(void)
{
    setup(1); assert(!ps2_pipe_triangle(ctx,position,colors,NULL,1));
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);
    uint32_t saved_color=r->target,saved_depth=r->depth;
    int references=color_resource->reference.count;
    struct pipe_box box={.x=1,.y=2,.width=3,.height=4,.depth=1};
    assert(!ps2_pipe_solid_logic(ctx,color_resource,&box,0x34abef,0x555aaa,6));
    assert(render_calls==1 && last_logic.handle==((struct ps2_resource*)color_resource)->handle);
    assert(r->target==saved_color && r->depth==saved_depth && color_resource->reference.count==references);
    assert(!ps2_pipe_solid_logic(ctx,NULL,&box,0,0xffffff,10) && last_logic.handle==0);
    unsigned before=calls;
    assert(ps2_pipe_solid_logic(ctx,depth_resource,&box,0,1,3)==-1 && errno==ENOTSUP && calls==before);
    box.x=INT_MAX; assert(ps2_pipe_solid_logic(ctx,color_resource,&box,0,1,3)==-1 && calls==before);
    box.x=1; assert(ps2_pipe_solid_logic(ctx,color_resource,&box,0x1000000,1,3)==-1 && calls==before);
    ctx->destroy(ctx); release_external(); screen->destroy(screen); assert(closes==1);
}
static void unsupported(void)
{
    advertised=0; setup(1); xsetup(); unsigned before=calls;
    assert(!ps2_logic_exa_prepare(&xs,&logic,&exa,ctx) && !logic.attached);
    struct pipe_box box={.width=1,.height=1,.depth=1};
    assert(ps2_pipe_solid_logic(ctx,color_resource,&box,0,1,3)==-1 && errno==ENOTSUP && calls==before);
    assert(ps2_surface_solid_logic(ps2_pipe_context_render(ctx)->gs,0,0,0,1,1,0,1,3)==-1 && calls==before);
    ps2_exa_context_bridge_cleanup(&bridge); ctx->destroy(ctx); release_external(); screen->destroy(screen);
}
static void lifetime(void)
{
    setup(1); xsetup(); ExaDriverRec saved=exa.driver;
    assert(ps2_logic_exa_prepare(&xs,&logic,&exa,ctx));
    ctx->destroy(ctx); release_external(); screen->destroy(screen); assert(!closes);
    expect_fatal=1; if(!setjmp(fatal)) { exa.driver.PrepareSolid(&front,3,~0U,0); assert(0); }
    unsigned before=calls;
    ps2_logic_exa_cleanup(&xs,&logic); assert(!closes && calls==before);
    assert(saved.PrepareSolid==exa.driver.PrepareSolid && saved.Solid==exa.driver.Solid);
    ps2_exa_context_bridge_cleanup(&bridge); assert(closes==1);
    ps2_logic_exa_cleanup(&xs,&logic); assert(closes==1);
}
static void failed(void)
{
    setup(1); xsetup(); assert(ps2_logic_exa_prepare(&xs,&logic,&exa,ctx));
    assert(exa.driver.PrepareSolid(&front,6,~0U,0x543abc)); fail_ioctl=EIO;
    expect_fatal=1; if(!setjmp(fatal)) { exa.driver.Solid(&front,0,0,3,4); assert(0); }
    unsigned before=calls; fail_ioctl=0;
    expect_fatal=1; if(!setjmp(fatal)) { exa.driver.PrepareSolid(&front,6,~0U,0); assert(0); }
    ps2_logic_exa_cleanup(&xs,&logic); ps2_exa_context_bridge_cleanup(&bridge);
    ctx->destroy(ctx); release_external(); screen->destroy(screen);
    assert(calls==before && !logic_calls && closes==1);
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strcmp(argv[1],"transport")) transport(); else if(!strcmp(argv[1],"exa")) logic_exa_case();
    else if(!strcmp(argv[1],"mesa")) logic_mesa_case(); else if(!strcmp(argv[1],"unsupported")) unsupported();
    else if(!strcmp(argv[1],"lifetime")) lifetime(); else if(!strcmp(argv[1],"failure")) failed(); else return 2;
    puts("PS2 GS solid logic backend test PASS"); return 0;
}
