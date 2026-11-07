/* SPDX-License-Identifier: MIT */
#include "previous-copy.c"
#include "ps2_copy_window.h"
static struct ps2_copy_window window_owner;
RegDataRec RegionBrokenData = {0};
static int fail_registration;
Bool dixRegisterPrivateKey(DevPrivateKey key, DevPrivateType type, unsigned size)
{ return fail_registration ? FALSE : original_register_private(key, type, size); }
static void forbidden_window(WindowPtr w, DDXPointRec old, RegionPtr source)
{ (void)w; (void)old; (void)source; assert(0); }
static void window_setup(int owned)
{
    gc_setup(1);
    xs.CopyWindow = forbidden_window;
    window.drawable.width = 128; window.drawable.height = 96; window.realized = TRUE;
    if (owned) {
        int pitch;
        static PixmapRec front; front = xp; front_pixmap = &front;
        xp.devPrivates = exa.driver.CreatePixmap2(&xs, 128, 96, 24, 0, 32, &pitch); assert(xp.devPrivates);
        xp.drawable.width = 128; xp.drawable.height = 96;
#ifdef COMPOSITE
        xp.screen_x = 20; xp.screen_y = 30;
#endif
    }
    pixman_region_init_rect(&window.borderClip, 0, 0, 720, 576);
    assert(ps2_copy_window_prepare(&xs, &window_owner, &exa, ctx));
}
static void window_cleanup(void)
{
    ps2_copy_window_cleanup(&window_owner); ps2_copy_window_cleanup(&window_owner);
    assert(xs.CopyWindow == forbidden_window);
    pixman_region_fini(&window.borderClip); gc_cleanup();
}
static uint32_t window_handle(void)
{ uint32_t h = 0; if (xp.devPrivates) memcpy(&h, xp.devPrivates, 4); return h; }
static void checked_move(DDXPointRec old, RegionPtr source)
{
    uint32_t h = window_handle(); struct pixels *p = model(h);
    seed(p);
    size_t bytes = (size_t)p->width*p->height*4;
    uint32_t *snapshot = malloc(bytes), *expected = malloc(bytes); assert(snapshot && expected);
    memcpy(snapshot, p->data, bytes); memcpy(expected, p->data, bytes);
    int dx = old.x-window.drawable.x, dy = old.y-window.drawable.y;
    int ox = xp.screen_x, oy = xp.screen_y;
    /* Independently scan pixel centers and region membership in screen space.
     * The expected source is immutable even across multiple overlapping bands. */
    unsigned changed = 0;
    for (int y = 0; y < (int)p->height; ++y) for (int x = 0; x < (int)p->width; ++x) {
        if (!pixman_region_contains_point(&window.borderClip, x+ox, y+oy, NULL) ||
            !pixman_region_contains_point(source, x+ox+dx, y+oy+dy, NULL)) continue;
        assert(x+dx >= 0 && y+dy >= 0 && x+dx < (int)p->width && y+dy < (int)p->height);
        expected[y*p->width+x] = snapshot[(y+dy)*p->width+x+dx]; ++changed;
    }
    RegionRec translated; pixman_region_init(&translated); assert(pixman_region_copy(&translated, source));
    pixman_region_translate(&translated, -dx, -dy);
    struct ps2_render_client *r = ps2_pipe_context_render(ctx);
    uint32_t target = r->target, depth = r->depth;
    unsigned sync = sync_calls, damage = damage_calls;
    xs.CopyWindow(&window, old, source);
    assert(!memcmp(expected, p->data, bytes));
    assert(pixman_region_equal(source, &translated));
    assert(sync_calls == sync + !!(changed && (dx || dy)) && damage_calls == damage);
    assert(target == r->target && depth == r->depth && color_resource->reference.count == 3 && depth_resource->reference.count == 3);
    pixman_region_fini(&translated); free(snapshot); free(expected);
}
static void move_case(void)
{
    window_setup(0);
    for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) if (x || y) {
        window.drawable.x = 100+x*7; window.drawable.y = 120+y*5;
        RegionRec source; pixman_region_init_rect(&source, 100, 120, 129, 65);
        checked_move((DDXPointRec){100,120}, &source); pixman_region_fini(&source);
    }
    assert(local_copies && textured_copies && !draw_delegates); window_cleanup();
}
static void clip_case(void)
{
    window_setup(0); window.drawable.x = 107; window.drawable.y = 125;
    pixman_region_fini(&window.borderClip);
    pixman_box16_t clip[] = {{110,130,150,155},{160,130,207,155},{120,170,229,194}};
    assert(pixman_region_init_rects(&window.borderClip, clip, 3));
    pixman_box16_t original[] = {{100,120,145,150},{154,120,220,150},{110,160,225,190}};
    RegionRec source; assert(pixman_region_init_rects(&source, original, 3));
    checked_move((DDXPointRec){100,120}, &source);
    assert(RegionNumRects(&source) == 3); pixman_region_fini(&source); window_cleanup();
}
static void redirect_case(void)
{
    window_setup(1);
    for (int fmt = 0; fmt < 2; ++fmt) {
        if (fmt) {
            exa.driver.DestroyPixmap(&xs, xp.devPrivates);
            int pitch; xp.devPrivates = exa.driver.CreatePixmap2(&xs,128,96,15,0,16,&pitch); assert(xp.devPrivates);
            xp.drawable.depth = window.drawable.depth = 15;
            xp.drawable.bitsPerPixel = window.drawable.bitsPerPixel = 16;
        }
        for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) if (x || y) {
            window.drawable.x = 40+x*7; window.drawable.y = 50+y*5;
            RegionRec source; pixman_region_init_rect(&source,40,50,67,35);
            checked_move((DDXPointRec){40,50}, &source); pixman_region_fini(&source);
        }
    }
    window_cleanup();
}
static void noop_case(void)
{
    window_setup(0); struct ps2_render_client *r = ps2_pipe_context_render(ctx);
    assert(!ps2_render_fill(r,0,0,2,2,0)); unsigned before = calls;
    RegionRec source; pixman_region_init(&source);
    checked_move((DDXPointRec){10,10}, &source);
    pixman_region_fini(&source); pixman_region_init_rect(&source,30,40,10,10);
    checked_move((DDXPointRec){20,30}, &source);
    pixman_region_fini(&window.borderClip); pixman_region_init(&window.borderClip);
    checked_move((DDXPointRec){10,20}, &source);
    assert(calls == before && r->count == 1 && !sync_calls);
    pixman_region_fini(&source); window_cleanup();
}
static void invoke_failure(DDXPointRec old, RegionPtr source)
{
    expect_fatal = 1;
    if (!setjmp(fatal)) { xs.CopyWindow(&window, old, source); assert(0); }
}
static void rejected(DDXPointRec old, RegionPtr source)
{
    RegionRec saved; pixman_region_init(&saved); assert(pixman_region_copy(&saved, source));
    unsigned before = calls, sync = sync_calls;
    invoke_failure(old, source);
    assert(calls == before && sync_calls == sync && pixman_region_equal(source, &saved));
    pixman_region_fini(&saved);
}
static void preflight_case(void)
{
    window_setup(0); struct ps2_render_client *r = ps2_pipe_context_render(ctx);
    assert(!ps2_render_fill(r,0,0,2,2,0));
    RegionRec source; pixman_region_init_rect(&source,100,100,20,20);
    window.drawable.x = 10; window.drawable.y = 30;
    /* Destination negative / source outside native storage, later box invalid. */
    pixman_region_fini(&source); pixman_region_init_rect(&source,-10,100,20,20);
    rejected((DDXPointRec){-20,30}, &source);
    pixman_region_fini(&source); pixman_region_init_rect(&source,710,100,20,20);
    rejected((DDXPointRec){20,30}, &source);
    pixman_region_fini(&source);
    pixman_box16_t boxes[] = {{50,50,60,60},{710,50,730,60}};
    assert(pixman_region_init_rects(&source,boxes,2)); rejected((DDXPointRec){20,30}, &source);
    pixman_region_fini(&source); pixman_region_init_rect(&source,32750,50,10,10);
    window.drawable.x = 100; rejected((DDXPointRec){0,30}, &source);
    pixman_region_fini(&source); pixman_region_init_rect(&source,-32760,50,10,10);
    window.drawable.x = -100; rejected((DDXPointRec){0,30}, &source);
    assert(r->count == 1 && !render_calls); pixman_region_fini(&source); window_cleanup();
}
static void capacity_case(void)
{
    window_setup(0); struct ps2_render_client *r = ps2_pipe_context_render(ctx);
    assert(!ps2_render_fill(r,0,0,2,2,0));
    pixman_box16_t boxes[257]; for (int i = 0; i < 257; ++i) boxes[i]=(pixman_box16_t){50+2*i,50,51+2*i,60};
    RegionRec source; assert(pixman_region_init_rects(&source,boxes,257));
    rejected((DDXPointRec){21,30}, &source);
    pixman_region_fini(&source); pixman_region_init_rect(&source,100,100,13,9);
    unsigned before=calls; fail_create=1; expect_fatal=1;
    invoke_failure((DDXPointRec){21,30},&source);
    assert(calls==before+1 && r->count==1 && !render_calls && source.extents.x1==100);
    fail_create=0; pixman_region_fini(&source); window_cleanup();
}
static PixmapPtr missing_window(WindowPtr w) { (void)w; return NULL; }
static void ownership_case(void)
{
    window_setup(1); struct ps2_render_client *r=ps2_pipe_context_render(ctx);
    assert(!ps2_render_fill(r,0,0,2,2,0));
    RegionRec source; pixman_region_init_rect(&source,40,50,13,9);
    xs.GetWindowPixmap=missing_window; rejected((DDXPointRec){21,30},&source); xs.GetWindowPixmap=get_window;
    ScreenRec foreign={0}; xp.drawable.pScreen=&foreign; rejected((DDXPointRec){21,30},&source); xp.drawable.pScreen=&xs;
    void *private=xp.devPrivates; xp.devPrivates=NULL; rejected((DDXPointRec){21,30},&source); xp.devPrivates=private;
    uint32_t h=window_handle(),zero=0,bad=9999;
    memcpy(private,&zero,4); rejected((DDXPointRec){21,30},&source);
    memcpy(private,&bad,4); rejected((DDXPointRec){21,30},&source); memcpy(private,&h,4);
    --xp.drawable.width; rejected((DDXPointRec){21,30},&source); ++xp.drawable.width;
    xp.drawable.depth=16; rejected((DDXPointRec){21,30},&source); xp.drawable.depth=24;
    window.drawable.bitsPerPixel=16; rejected((DDXPointRec){21,30},&source); window.drawable.bitsPerPixel=32;
    assert(r->count==1); pixman_region_fini(&source); window_cleanup();
}
static CopyWindowProcPtr damage_lower;
static CloseScreenProcPtr damage_close_lower;
static unsigned wrapper_notifications;
static void damage_wrapper(WindowPtr w, DDXPointRec old, RegionPtr source)
{
    /* Mirror pinned Xorg Damage's translate/append/restore/unwrap/call/rewrap. */
    int dx=w->drawable.x-old.x,dy=w->drawable.y-old.y;
    pixman_region_translate(source,dx,dy); ++wrapper_notifications;
    DamageRegionAppend(&w->drawable,source); pixman_region_translate(source,-dx,-dy);
    xs.CopyWindow=damage_lower; xs.CopyWindow(w,old,source);
    DamageRegionProcessPending(&w->drawable); damage_lower=xs.CopyWindow; xs.CopyWindow=damage_wrapper;
}
static Bool damage_close(ScreenPtr s)
{ s->CopyWindow=damage_lower; s->CloseScreen=damage_close_lower; return s->CloseScreen(s); }
static void wrappers_case(void)
{
    window_setup(0); damage_lower=xs.CopyWindow; damage_close_lower=xs.CloseScreen;
    xs.CopyWindow=damage_wrapper; xs.CloseScreen=damage_close;
    RegionRec source; pixman_region_init_rect(&source,100,100,13,9);
    window.drawable.x=21; unsigned damage=damage_calls;
    xs.CopyWindow(&window,(DDXPointRec){20,30},&source);
    assert(xs.CopyWindow==damage_wrapper && damage_calls==damage+1 && processed==1 && wrapper_notifications==1);
    assert(source.extents.x1==101 && source.extents.x2==114);
    /* Our owner is above the fixture GC CloseScreen; reverse nesting closes it
     * before GC, just as wrappers around the native screen are unwound. */
    assert(!xs.CloseScreen(&xs)); assert(!window_owner.installed && !gc_screen.installed && close_calls==1);
    pixman_region_fini(&source); window_cleanup();
}
static void window_lifetime_case(void)
{
    window_setup(0); ctx->destroy(ctx); release_external(); screen->destroy(screen); assert(!closes);
    RegionRec source; pixman_region_init_rect(&source,100,100,13,9);
    rejected((DDXPointRec){21,30},&source);
    ps2_copy_window_cleanup(&window_owner); assert(!closes);
    ps2_gc_screen_cleanup(&gc_screen); assert(closes==1);
    pixman_region_fini(&source); pixman_region_fini(&window.borderClip); pixman_region_fini(&clip_region);
}
static void window_failure_case(void)
{
    window_setup(0); RegionRec source; pixman_region_init_rect(&source,100,100,64,20);
    fail_ioctl=EIO; expect_fatal=1; unsigned before=calls;
    invoke_failure((DDXPointRec){21,30},&source);
    assert(calls==before+1 && source.extents.x1==100 && !sync_calls);
    before=calls; rejected((DDXPointRec){21,30},&source); assert(calls==before);
    pixman_region_fini(&source); window_cleanup(); assert(closes==1);
}
static void installation_case(void)
{
    window_setup(0); struct ps2_copy_window duplicate={0}; CopyWindowProcPtr hook=xs.CopyWindow;
    assert(!ps2_copy_window_prepare(&xs,&duplicate,&exa,ctx));
    xs.CopyWindow=forbidden_window; expect_fatal=1;
    if (!setjmp(fatal)) { ps2_copy_window_cleanup(&window_owner); assert(0); }
    assert(window_owner.installed); xs.CopyWindow=hook; ps2_copy_window_cleanup(&window_owner);
    fail_registration=1; assert(!ps2_copy_window_prepare(&xs,&window_owner,&exa,ctx));
    assert(!window_owner.installed && xs.CopyWindow==forbidden_window); fail_registration=0;
    xs.GetWindowPixmap=NULL; assert(!ps2_copy_window_prepare(&xs,&window_owner,&exa,ctx)); xs.GetWindowPixmap=get_window;
    assert(ps2_copy_window_prepare(&xs,&window_owner,&exa,ctx)); window_cleanup();
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strcmp(argv[1],"moves")) move_case();
    else if(!strcmp(argv[1],"clipping")) clip_case();
    else if(!strcmp(argv[1],"redirected")) redirect_case();
    else if(!strcmp(argv[1],"noop")) noop_case();
    else if(!strcmp(argv[1],"preflight")) preflight_case();
    else if(!strcmp(argv[1],"capacity")) capacity_case();
    else if(!strcmp(argv[1],"ownership")) ownership_case();
    else if(!strcmp(argv[1],"wrappers")) wrappers_case();
    else if(!strcmp(argv[1],"lifetime")) window_lifetime_case();
    else if(!strcmp(argv[1],"failure")) window_failure_case();
    else if(!strcmp(argv[1],"installation")) installation_case();
    else assert(0);
    puts("native CopyWindow and shared Mesa copy test PASS"); return 0;
}
