/* SPDX-License-Identifier: MIT */
#include "previous-copy.c"
#include "ps2_paint_window.h"
#include "ps2_copy_window.h"
static struct ps2_paint_window paint_owner;
static struct ps2_copy_window move_owner;
RegDataRec RegionBrokenData = {0};
static int fail_registration, fail_logic_at;
static unsigned logic_calls;
static DrawablePtr notified;
static RegionRec damage_region;
Bool dixRegisterPrivateKey(DevPrivateKey key, DevPrivateType type, unsigned size)
{ return fail_registration ? FALSE : original_register_private(key, type, size); }
void DamageRegionAppend(DrawablePtr d, RegionPtr r)
{
    assert(d && r); ++damage_calls; notified=d;
    assert(pixman_region_copy(&damage_region, r));
}
void DamageRegionProcessPending(DrawablePtr d)
{ assert(d == notified); ++processed; }
int __wrap_ioctl(int fd, unsigned long op, ...)
{
    va_list args; va_start(args,op); void *arg=va_arg(args,void*); va_end(args);
    if (op != PS2GS_SOLID_LOGIC) return copy_ioctl(fd,op,arg);
    assert(fd==999); ++calls;
    if (fail_ioctl || (fail_logic_at && --fail_logic_at==0)) { errno=EIO; return -1; }
    const struct ps2gs_solid_logic *r=arg; struct pixels *p=model(r->handle);
    assert(r->abi==1 && r->alu==GXcopy && !r->reserved && r->width && r->height);
    assert(r->x+r->width<=p->width && r->y+r->height<=p->height);
    for (unsigned y=r->y; y<r->y+r->height; ++y) for (unsigned x=r->x; x<r->x+r->width; ++x) {
        uint32_t *v=p->data+y*p->width+x; *v=(*v&~r->plane_mask)|(r->color&r->plane_mask);
    }
    ++logic_calls; return 0;
}
static void forbidden_paint(WindowPtr w, RegionPtr r, int what)
{ (void)w; (void)r; (void)what; assert(0); }
static void forbidden_move(WindowPtr w, DDXPointRec old, RegionPtr r)
{ (void)w; (void)old; (void)r; assert(0); }
static void paint_setup(int redirect, int bpp)
{
    gc_setup(1); xs.PaintWindow=forbidden_paint; xs.CopyWindow=forbidden_move;
    window.drawable.width=128; window.drawable.height=96; window.realized=TRUE;
    if (redirect) {
        static PixmapRec front; front=xp; front_pixmap=&front;
        int pitch; xp.devPrivates=exa.driver.CreatePixmap2(&xs,128,96,bpp==16?15:24,0,bpp,&pitch);
        assert(xp.devPrivates); xp.drawable.width=128; xp.drawable.height=96;
        xp.drawable.depth=window.drawable.depth=bpp==16?15:24;
        xp.drawable.bitsPerPixel=window.drawable.bitsPerPixel=bpp;
        xp.screen_x=20; xp.screen_y=30;
    }
    pixman_region_init_rect(&window.clipList,20,30,128,96);
    pixman_region_init(&damage_region);
    window.backgroundState=BackgroundPixel; window.background.pixel=0xab123456;
    window.borderIsPixel=TRUE; window.border.pixel=0xcd654321;
    assert(ps2_copy_window_prepare(&xs,&move_owner,&exa,ctx));
    assert(ps2_paint_window_prepare(&xs,&paint_owner,&exa,ctx));
}
static uint32_t paint_handle(void)
{ uint32_t h=0; if(xp.devPrivates) memcpy(&h,xp.devPrivates,4); return h; }
static void paint_cleanup(void)
{
    ps2_paint_window_cleanup(&paint_owner); ps2_paint_window_cleanup(&paint_owner);
    assert(xs.PaintWindow==forbidden_paint);
    ps2_copy_window_cleanup(&move_owner);
    pixman_region_fini(&damage_region); pixman_region_fini(&window.clipList); gc_cleanup();
}
static void checked_paint(RegionPtr r, int what, uint32_t pixel)
{
    struct pixels *p=model(paint_handle()); seed(p);
    size_t bytes=(size_t)p->width*p->height*4;
    uint32_t *expected=malloc(bytes); assert(expected); memcpy(expected,p->data,bytes);
    uint32_t mask=window.drawable.depth==15?0x7fff:0xffffff;
    int ox=xp.screen_x,oy=xp.screen_y;
    unsigned count=0;
    RegionRec expected_damage,saved; pixman_region_init(&expected_damage); pixman_region_init(&saved);
    assert(pixman_region_copy(&saved,r));
    /* Independent pixel-center membership, with no use of driver paint jobs. */
    for(unsigned y=0;y<p->height;++y) for(unsigned x=0;x<p->width;++x) {
        if(!pixman_region_contains_point(r,x+ox,y+oy,NULL) ||
            (what==PW_BACKGROUND && !pixman_region_contains_point(&window.clipList,x+ox,y+oy,NULL))) continue;
        expected[y*p->width+x]=(expected[y*p->width+x]&~mask)|(pixel&mask); ++count;
        int dx=x+(what==PW_BACKGROUND?ox:0),dy=y+(what==PW_BACKGROUND?oy:0);
        assert(pixman_region_union_rect(&expected_damage,&expected_damage,dx,dy,1,1));
    }
    struct ps2_render_client *render=ps2_pipe_context_render(ctx);
    uint32_t target=render->target,depth=render->depth;
    unsigned sync=sync_calls,damage=damage_calls,pending=processed;
    xs.PaintWindow(&window,r,what);
    assert(!memcmp(expected,p->data,bytes) && pixman_region_equal(r,&saved));
    assert(sync_calls==sync+!!count && damage_calls==damage+!!count && processed==pending+!!count);
    if(count) {
        assert(notified==(what==PW_BACKGROUND?&window.drawable:&xp.drawable));
        assert(pixman_region_equal(&damage_region,&expected_damage));
    }
    assert(render->target==target && render->depth==depth && color_resource->reference.count==3 && depth_resource->reference.count==3);
    free(expected); pixman_region_fini(&saved); pixman_region_fini(&expected_damage);
}
static void background_case(void)
{
    paint_setup(0,32); pixman_region_fini(&window.clipList);
    pixman_box16_t clip[]={{20,30,50,50},{60,30,90,50},{30,60,110,80}};
    assert(pixman_region_init_rects(&window.clipList,clip,3));
    RegionRec r; pixman_region_init_rect(&r,10,20,125,75);
    struct ps2_render_client *render=ps2_pipe_context_render(ctx);
    assert(!ps2_render_fill(render,0,0,1,1,0));
    checked_paint(&r,PW_BACKGROUND,window.background.pixel);
    assert(render_calls==1 && logic_calls==3 && !draw_delegates);
    pixman_region_fini(&r); paint_cleanup();
}
static void border_case(void)
{
    paint_setup(0,32); RegionRec r; pixman_region_init_rect(&r,-5,-8,160,140);
    pixman_region_fini(&window.clipList); pixman_region_init(&window.clipList);
    checked_paint(&r,PW_BORDER,window.border.pixel);
    assert(logic_calls==1); pixman_region_fini(&r); paint_cleanup();
}
static void inherited_case(void)
{
    paint_setup(0,32);
    WindowRec parent={.drawable={.pScreen=&xs},.backgroundState=ParentRelative};
    WindowRec root={.drawable={.pScreen=&xs},.backgroundState=BackgroundPixel,.background={.pixel=0xfedcba}};
    window.backgroundState=ParentRelative; window.parent=&parent; parent.parent=&root;
    RegionRec r; pixman_region_init_rect(&r,0,0,300,300);
    checked_paint(&r,PW_BACKGROUND,root.background.pixel);
    unsigned before=calls; root.backgroundState=None; xs.PaintWindow(&window,&r,PW_BACKGROUND); assert(calls==before);
    root.backgroundState=BackgroundPixel; root.inhibitBGPaint=TRUE;
    xs.PaintWindow(&window,&r,PW_BACKGROUND); assert(calls==before);
    /* Inhibition belongs to the resolved ancestor, matching miPaintWindow. */
    root.inhibitBGPaint=FALSE; window.inhibitBGPaint=TRUE;
    checked_paint(&r,PW_BACKGROUND,root.background.pixel);
    pixman_region_fini(&r); paint_cleanup();
}
static void redirect_case(void)
{
    paint_setup(1,32); RegionRec r; pixman_region_init_rect(&r,12,18,150,130);
    checked_paint(&r,PW_BACKGROUND,window.background.pixel); checked_paint(&r,PW_BORDER,window.border.pixel);
    exa.driver.DestroyPixmap(&xs,xp.devPrivates); int pitch;
    xp.devPrivates=exa.driver.CreatePixmap2(&xs,128,96,15,0,16,&pitch); assert(xp.devPrivates);
    xp.drawable.depth=window.drawable.depth=15; xp.drawable.bitsPerPixel=window.drawable.bitsPerPixel=16;
    checked_paint(&r,PW_BACKGROUND,window.background.pixel); checked_paint(&r,PW_BORDER,window.border.pixel);
    pixman_region_fini(&r); paint_cleanup();
}
static void noop_case(void)
{
    paint_setup(1,32); struct ps2_render_client *r=ps2_pipe_context_render(ctx);
    assert(!ps2_render_fill(r,0,0,2,2,0)); unsigned before=calls;
    RegionRec region; pixman_region_init(&region); xs.PaintWindow(&window,&region,PW_BACKGROUND);
    pixman_region_fini(&region); pixman_region_init_rect(&region,300,400,20,30);
    checked_paint(&region,PW_BACKGROUND,window.background.pixel); checked_paint(&region,PW_BORDER,window.border.pixel);
    window.backgroundState=None; xs.PaintWindow(&window,NULL,PW_BACKGROUND);
    window.backgroundState=BackgroundPixel; window.inhibitBGPaint=TRUE; xs.PaintWindow(&window,NULL,PW_BACKGROUND);
    assert(calls==before && r->count==1 && !logic_calls && !sync_calls && !damage_calls);
    pixman_region_fini(&region); paint_cleanup();
}
static void invoke_failure(RegionPtr r,int what)
{ expect_fatal=1; if(!setjmp(fatal)) { xs.PaintWindow(&window,r,what); assert(0); } }
static void rejected(RegionPtr r,int what)
{ unsigned before=calls; invoke_failure(r,what); assert(calls==before && !logic_calls && !damage_calls && !sync_calls); }
static void preflight_case(void)
{
    paint_setup(1,32); struct ps2_render_client *render=ps2_pipe_context_render(ctx);
    assert(!ps2_render_fill(render,0,0,2,2,0));
    RegionRec r; pixman_region_init_rect(&r,20,30,80,50);
    rejected(NULL,PW_BACKGROUND); rejected(&r,99);
    window.backgroundState=BackgroundPixmap; window.background.pixmap=NULL;
    rejected(&r,PW_BACKGROUND); window.backgroundState=BackgroundPixel; window.background.pixel=0xab123456;
    window.borderIsPixel=FALSE; window.border.pixmap=NULL;
    rejected(&r,PW_BORDER); window.borderIsPixel=TRUE; window.border.pixel=0xcd654321;
    window.backgroundState=ParentRelative; rejected(&r,PW_BACKGROUND);
    window.parent=&window; rejected(&r,PW_BACKGROUND); window.parent=NULL; window.backgroundState=BackgroundPixel;
    xp.screen_x=32700; rejected(&r,PW_BORDER); xp.screen_x=20;
    allocation_failure=1; rejected(&r,PW_BORDER);
    RegionRec broken={.data=&RegionBrokenData}; rejected(&broken,PW_BACKGROUND);
    assert(render->count==1 && !render_calls); pixman_region_fini(&r); paint_cleanup();
}
static void complex_region_case(void)
{
    const unsigned counts[]={257,512,1024};
    for(unsigned depth=0;depth<2;++depth)for(unsigned clip=0;clip<2;++clip)
    for(unsigned size=0;size<sizeof(counts)/sizeof(*counts);++size){
        paint_setup(1,depth?16:32);
        pixman_box16_t boxes[1024];unsigned count=counts[size];
        for(unsigned i=0;i<count;++i){int x=20+(i%32)*2,y=30+(i/32)*2;
            boxes[i]=(pixman_box16_t){x,y,x+1,y+1};}
        RegionRec region;assert(pixman_region_init_rects(&region,boxes,count));
        assert((unsigned)pixman_region_n_rects(&region)==count);
        if(clip){
            assert(pixman_region_copy(&window.clipList,&region));
            pixman_region_fini(&region);pixman_region_init_rect(&region,20,30,128,96);
        }
        unsigned before=logic_calls;
        checked_paint(&region,PW_BACKGROUND,window.background.pixel);
        assert(logic_calls-before==count && !draw_delegates);
        pixman_region_fini(&region);paint_cleanup();
    }
}
static PixmapPtr missing(WindowPtr w) { (void)w; return NULL; }
static void ownership_case(void)
{
    paint_setup(1,32); RegionRec r; pixman_region_init_rect(&r,20,30,80,50);
    xs.GetWindowPixmap=missing; rejected(&r,PW_BACKGROUND); xs.GetWindowPixmap=get_window;
    ScreenRec foreign={0}; xp.drawable.pScreen=&foreign; rejected(&r,PW_BACKGROUND); xp.drawable.pScreen=&xs;
    void *private=xp.devPrivates; xp.devPrivates=NULL; rejected(&r,PW_BACKGROUND); xp.devPrivates=private;
    uint32_t h=paint_handle(),bad=9999; memcpy(private,&bad,4); rejected(&r,PW_BACKGROUND); memcpy(private,&h,4);
    --xp.drawable.width; rejected(&r,PW_BACKGROUND); ++xp.drawable.width;
    xp.drawable.depth=16; rejected(&r,PW_BACKGROUND); xp.drawable.depth=24;
    window.drawable.bitsPerPixel=16; rejected(&r,PW_BACKGROUND); window.drawable.bitsPerPixel=32;
    pixman_region_fini(&r); paint_cleanup();
}
static void paint_lifetime_case(void)
{
    paint_setup(0,32); ctx->destroy(ctx); release_external(); screen->destroy(screen); assert(!closes);
    RegionRec r; pixman_region_init_rect(&r,20,30,80,50); rejected(&r,PW_BACKGROUND);
    assert(!xs.CloseScreen(&xs)); assert(!paint_owner.installed && !move_owner.installed && !gc_screen.installed && close_calls==1 && closes==1);
    assert(xs.PaintWindow==forbidden_paint && xs.CopyWindow==forbidden_move);
    pixman_region_fini(&r); pixman_region_fini(&window.clipList); pixman_region_fini(&damage_region); pixman_region_fini(&clip_region);
}
static void paint_failure_case(void)
{
    paint_setup(0,32); pixman_box16_t boxes[]={{20,30,40,50},{60,30,80,50}};
    RegionRec r,saved; assert(pixman_region_init_rects(&r,boxes,2)); pixman_region_init(&saved); assert(pixman_region_copy(&saved,&r));
    fail_logic_at=2; invoke_failure(&r,PW_BACKGROUND);
    assert(logic_calls==1 && !damage_calls && !sync_calls && ps2_pipe_context_error(ctx)==EIO && pixman_region_equal(&r,&saved));
    unsigned before=calls; invoke_failure(&r,PW_BACKGROUND); assert(calls==before && logic_calls==1);
    pixman_region_fini(&saved); pixman_region_fini(&r); paint_cleanup(); assert(closes==1);
}
static void installation_case(void)
{
    paint_setup(0,32); struct ps2_paint_window duplicate={0};
    assert(!ps2_paint_window_prepare(&xs,&duplicate,&exa,ctx));
    PaintWindowProcPtr hook=xs.PaintWindow; xs.PaintWindow=forbidden_paint; expect_fatal=1;
    if(!setjmp(fatal)) { ps2_paint_window_cleanup(&paint_owner); assert(0); }
    assert(paint_owner.installed); xs.PaintWindow=hook; ps2_paint_window_cleanup(&paint_owner);
    fail_registration=1; assert(!ps2_paint_window_prepare(&xs,&paint_owner,&exa,ctx)); fail_registration=0;
    struct ps2_render_client *r=ps2_pipe_context_render(ctx); uint32_t features=r->gs->caps.features;
    r->gs->caps.features&=~PS2GS_HW_SOLID_LOGIC; assert(!ps2_paint_window_prepare(&xs,&paint_owner,&exa,ctx)); r->gs->caps.features=features;
    xs.GetScreenPixmap=NULL; assert(!ps2_paint_window_prepare(&xs,&paint_owner,&exa,ctx)); xs.GetScreenPixmap=get_front;
    assert(!paint_owner.installed && xs.PaintWindow==forbidden_paint);
    assert(ps2_paint_window_prepare(&xs,&paint_owner,&exa,ctx)); paint_cleanup();
}
static uint32_t random_seed=19;
static unsigned paint_random_value(void) { random_seed=random_seed*1664525U+1013904223U; return random_seed; }
static void paint_random_case(void)
{
    paint_setup(1,32);
    for(int i=0;i<30;++i) {
        pixman_box16_t boxes[5];
        for(int j=0;j<5;++j) {
            int x=(int)(paint_random_value()%180)-10,y=(int)(paint_random_value()%140)-10;
            boxes[j]=(pixman_box16_t){x,y,x+1+(int)(paint_random_value()%45),y+1+(int)(paint_random_value()%35)};
        }
        RegionRec r; assert(pixman_region_init_rects(&r,boxes,5));
        window.background.pixel=paint_random_value(); window.border.pixel=paint_random_value();
        checked_paint(&r,i%2?PW_BORDER:PW_BACKGROUND,i%2?window.border.pixel:window.background.pixel);
        pixman_region_fini(&r);
    }
    paint_cleanup();
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strcmp(argv[1],"background")) background_case();
    else if(!strcmp(argv[1],"border")) border_case();
    else if(!strcmp(argv[1],"inherited")) inherited_case();
    else if(!strcmp(argv[1],"redirected")) redirect_case();
    else if(!strcmp(argv[1],"noop")) noop_case();
    else if(!strcmp(argv[1],"preflight")) preflight_case();
    else if(!strcmp(argv[1],"ownership")) ownership_case();
    else if(!strcmp(argv[1],"lifetime")) paint_lifetime_case();
    else if(!strcmp(argv[1],"failure")) paint_failure_case();
    else if(!strcmp(argv[1],"installation")) installation_case();
    else if(!strcmp(argv[1],"random")) paint_random_case();
    else if(!strcmp(argv[1],"complex")) complex_region_case();
    else assert(0);
    puts("native PaintWindow and shared Mesa solid test PASS"); return 0;
}
