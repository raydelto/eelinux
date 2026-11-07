/* SPDX-License-Identifier: MIT */
#include "ps2_pipe.h"
#undef ARRAY_SIZE
#include "ps2_gs_logic.h"
#include "ps2_solid.h"
#include "ps2_solid_batch.h"
#define FatalError lower_fixture_FatalError
#include "previous-gc.c"
#undef FatalError
void FatalError(const char *format,...)
{
    if(!expect_fatal) {
        va_list args;va_start(args,format);vfprintf(stderr,format,args);va_end(args);
    }
    assert(expect_fatal && format);expect_fatal=0;longjmp(fatal,1);
}
static struct ps2_solid_screen solid_screen;
static uint32_t expected[96][128];
static unsigned logic_calls,batch_calls;
static int fail_realloc, require_fence;
static uint32_t expected_handle;
void *__real_realloc(void *,size_t);
void *__wrap_realloc(void *p,size_t n)
{ if(fail_realloc) { fail_realloc=0; errno=ENOMEM; return NULL; } return __real_realloc(p,n); }
int __wrap_ioctl(int fd,unsigned long op,...)
{
    va_list args; va_start(args,op); void *arg=va_arg(args,void*); va_end(args);
    if(op==PS2GS_SOLID_BATCH) {
        const struct ps2gs_solid_batch *batch=arg;
        assert(batch->count && batch->count<=64 && batch->handle==expected_handle);
        const struct ps2gs_solid_logic *rectangles=(void*)(uintptr_t)batch->rectangles;
        unsigned before=calls;
        for(unsigned i=0;i<batch->count;++i) {
            int status=__wrap_ioctl(fd,PS2GS_SOLID_LOGIC,rectangles+i);
            if(status)return status;
        }
        calls=before+1;++batch_calls;return 0;
    }
    if(op!=PS2GS_SOLID_LOGIC) {
        int r=glyph_ioctl(fd,op,arg);
        if(!r && op==PS2GS_GET_CAPS) ((struct ps2gs_caps*)arg)->features|=PS2GS_HW_SOLID_LOGIC;
        return r;
    }
    assert(fd==999); ++calls;
    if(fail_ioctl) { errno=fail_ioctl; return -1; }
    struct ps2gs_solid_logic *r=arg;
    assert(r->handle==expected_handle && r->abi==1 && !r->reserved && r->alu<16);
    assert(r->width && r->height && r->x+r->width<=128 && r->y+r->height<=96);
    if(require_fence) { assert(render_calls); require_fence=0; }
    ++logic_calls;
    for(unsigned y=r->y;y<r->y+r->height;++y) for(unsigned x=r->x;x<r->x+r->width;++x) {
        /* Transport model uses the truth table; the reference uses a switch. */
        uint32_t a=r->color,b=drawn[y][x],v=0;
        if(r->alu&1) v|=a&b;
        if(r->alu&2) v|=a&~b;
        if(r->alu&4) v|=~a&b;
        if(r->alu&8) v|=~a&~b;
        drawn[y][x]=(b&~r->plane_mask)|(v&r->plane_mask);
    }
    return 0;
}
static uint32_t boolean(unsigned alu,uint32_t a,uint32_t b)
{
    switch(alu) {
    case GXclear:return 0; case GXand:return a&b; case GXandReverse:return a&~b;
    case GXcopy:return a; case GXandInverted:return ~a&b; case GXnoop:return b;
    case GXxor:return a^b; case GXor:return a|b; case GXnor:return ~(a|b);
    case GXequiv:return ~(a^b); case GXinvert:return ~b; case GXorReverse:return a|~b;
    case GXcopyInverted:return ~a; case GXorInverted:return ~a|b;
    case GXnand:return ~(a&b); case GXset:return ~0U;
    default:assert(0);return 0;
    }
}
static void seed(uint32_t value)
{ for(unsigned y=0;y<96;++y) for(unsigned x=0;x<128;++x) drawn[y][x]=expected[y][x]=value; }
static void ref_pixel(int64_t x,int64_t y)
{
    int ox=0,oy=0;
#ifdef COMPOSITE
    ox=xp.screen_x; oy=xp.screen_y;
#endif
    if(x<ox || y<oy || x>=ox+128 || y>=oy+96 ||
        !pixman_region_contains_point(&clip_region,x,y,NULL)) return;
    uint32_t mask=gc.planemask&(window.drawable.depth==15 ? 0x7fff : 0xffffff);
    uint32_t *v=&expected[y-oy][x-ox]; *v=(*v&~mask)|(boolean(gc.alu,gc.fgPixel,*v)&mask);
}
static void ref_relative(int x,int y)
{ ref_pixel(x+(gc.miTranslate ? window.drawable.x : 0),y+(gc.miTranslate ? window.drawable.y : 0)); }
static void ref_line(int x1,int y1,int x2,int y2,int include_last)
{
    assert(x1==x2 || y1==y2);
    int dx=(x2>x1)-(x2<x1),dy=(y2>y1)-(y2<y1);
    while(x1!=x2 || y1!=y2) { ref_relative(x1,y1); x1+=dx; y1+=dy; }
    if(include_last) ref_relative(x2,y2);
}
static void ref_rect(xRectangle r,int fill)
{
    if(fill) {
        for(unsigned j=0;j<r.height;++j) for(unsigned i=0;i<r.width;++i) ref_relative(r.x+i,r.y+j);
    } else {
        /* Enumerate each outline pixel once, independent of the driver's four strips. */
        for(unsigned j=0;j<=r.height;++j) for(unsigned i=0;i<=r.width;++i)
            if(!i || !j || i==r.width || j==r.height) ref_relative(r.x+i,r.y+j);
    }
}
static void equal(void) { assert(!memcmp(drawn,expected,sizeof(expected))); }
static void solid_setup(int owned)
{
    gc_setup(owned);
    gc.lineWidth=0; gc.lineStyle=LineSolid; gc.capStyle=CapButt;
    window.drawable.width=128; window.drawable.height=96;
    pixman_region_fini(&clip_region);
    pixman_box16_t boxes[]={{20,30,60,43},{25,47,70,80}};
    assert(pixman_region_init_rects(&clip_region,boxes,2));
    assert(ps2_solid_screen_prepare(&xs,&solid_screen,&gc_screen));
    assert(!ps2_solid_screen_prepare(&xs,&solid_screen,&gc_screen));
    expected_handle=0; seed(0x80345678);
}
static void solid_cleanup(void)
{ ps2_solid_screen_cleanup(&solid_screen); ps2_solid_screen_cleanup(&solid_screen); gc_cleanup(); }
static void fills(void)
{
    solid_setup(1);
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);
    assert(!ps2_render_fill(r,90,90,1,1,0x80345678)); require_fence=1;
    for(unsigned alu=0;alu<16;++alu) {
        gc.alu=alu; gc.fgPixel=0x785634; gc.planemask=0xff1f3f7f; seed(0x80345678);
        xRectangle rects[]={{-2,-1,14,12},{5,14,16,9},{0,0,0,0}};
        for(unsigned i=0;i<3;++i) ref_rect(rects[i],1);
        gc.ops->PolyFillRect(&window.drawable,&gc,3,rects); equal();
        DDXPointRec spans[]={{20,32},{22,48},{25,50}}; int widths[]={12,13,0};
        for(unsigned i=0;i<3;++i) for(int j=0;j<widths[i];++j) ref_pixel(spans[i].x+j,spans[i].y);
        gc.ops->FillSpans(&window.drawable,&gc,3,spans,widths,0); equal();
        DDXPointRec pts[]={{5,5},{1,0},{0,15},{0,0}}; int x=0,y=0;
        for(unsigned i=0;i<4;++i) { x+=pts[i].x;y+=pts[i].y;ref_relative(x,y); }
        gc.fillStyle=FillTiled;
        gc.ops->PolyPoint(&window.drawable,&gc,CoordModePrevious,4,pts); equal(); gc.fillStyle=FillSolid;
    }
    assert(render_calls==1 && !require_fence && damage_calls==processed && sync_calls==damage_calls);
    unsigned before=calls; gc.planemask=0;
    xRectangle r0={0,0,10,10}; gc.ops->PolyFillRect(&window.drawable,&gc,1,&r0); assert(calls==before);
    solid_cleanup();
}
static void line_case(void)
{
    solid_setup(1); gc.alu=GXxor; gc.fgPixel=0xffffff;
    xRectangle rects[]={{1,1,25,25},{5,5,0,8},{8,3,9,0},{7,7,0,0}};
    for(unsigned i=0;i<4;++i) ref_rect(rects[i],0);
    gc.ops->PolyRectangle(&window.drawable,&gc,4,rects); equal();
    for(unsigned i=0;i<4;++i) ref_rect(rects[i],0);
    gc.ops->PolyRectangle(&window.drawable,&gc,4,rects); equal();
    for(unsigned y=0;y<96;++y) for(unsigned x=0;x<128;++x) assert(drawn[y][x]==0x80345678);
    for(int cap=CapNotLast;cap<=CapProjecting;++cap) {
        gc.capStyle=cap;
        xSegment segs[]={{1,2,30,2},{30,5,2,5},{6,23,6,3},{6,3,6,23},{4,4,4,4}};
        for(unsigned i=0;i<5;++i) ref_line(segs[i].x1,segs[i].y1,segs[i].x2,segs[i].y2,cap!=CapNotLast);
        gc.ops->PolySegment(&window.drawable,&gc,5,segs); equal();
        DDXPointRec line[]={{2,2},{24,0},{0,23},{-24,0},{0,-23}};
        int x=line[0].x,y=line[0].y;
        for(unsigned i=1;i<5;++i) { ref_line(x,y,x+line[i].x,y+line[i].y,0);x+=line[i].x;y+=line[i].y; }
        gc.ops->Polylines(&window.drawable,&gc,CoordModePrevious,5,line); equal();
        DDXPointRec same[]={{8,6},{8,6}};
        ref_line(8,6,8,6,cap!=CapNotLast);
        gc.ops->Polylines(&window.drawable,&gc,CoordModeOrigin,2,same); equal();
    }
    solid_cleanup();
}
static void must_fail_line(xSegment *s,int n)
{
    unsigned before=calls; expect_fatal=1;
    if(!setjmp(fatal)) { gc.ops->PolySegment(&window.drawable,&gc,n,s); assert(0); }
    assert(calls==before); expect_fatal=0;
}
static void preflight(void)
{
    solid_setup(1); xSegment segs[]={{1,1,10,1},{2,2,3,3}};
    gc.lineWidth=1;must_fail_line(segs,1);gc.lineWidth=0;
    gc.lineStyle=LineOnOffDash;must_fail_line(segs,1);gc.lineStyle=LineSolid;
    must_fail_line(segs,4097); must_fail_line(NULL,1);
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);
    assert(!ps2_render_fill(r,90,90,1,1,0x80345678));
    unsigned before=calls; expect_fatal=1;
    DDXPointRec pts[]={{20,32},{21,33}};int widths[]={3,-1};
    if(!setjmp(fatal)) { gc.ops->FillSpans(&window.drawable,&gc,2,pts,widths,0);assert(0); }
    assert(calls==before && r->count==1);
    fail_realloc=1;expect_fatal=1;
    if(!setjmp(fatal)) { gc.ops->PolySegment(&window.drawable,&gc,1,segs);assert(0); }
    assert(calls==before && r->count==1); expect_fatal=0;
    /* All 16385th clipped rectangles reject before flushing the queued fill. */
    xRectangle rects[4096];for(unsigned i=0;i<4096;++i) rects[i]=(xRectangle){5,0,25,49};
    if(!setjmp(fatal)) { expect_fatal=1;gc.ops->PolyRectangle(&window.drawable,&gc,4096,rects);assert(0); }
    assert(calls==before && r->count==1);expect_fatal=0;
    r->count=0; solid_cleanup();
}
static void owned(void)
{
    solid_setup(1);
    static PixmapRec front;front=xp;front_pixmap=&front;
    int pitch;void *private=exa.driver.CreatePixmap2(&xs,128,96,15,0,16,&pitch);assert(private);
    xp.drawable.width=128;xp.drawable.height=96;xp.drawable.depth=15;xp.drawable.bitsPerPixel=16;xp.devPrivates=private;
    window.drawable.depth=15;window.drawable.bitsPerPixel=16;memcpy(&expected_handle,private,4);
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);unsigned target=r->target,depth=r->depth;
    assert(target && depth && color_resource->reference.count==3 && depth_resource->reference.count==3);
    gc.fgPixel=0xabcdef;gc.planemask=0xf12345;gc.alu=GXequiv;seed(0x8123);
    xRectangle rectangles[]={{-2,-2,21,31},{7,19,6,7}};
    for(unsigned i=0;i<2;++i) ref_rect(rectangles[i],1);
    gc.ops->PolyFillRect(&window.drawable,&gc,2,rectangles);equal();
    assert(r->target==target && r->depth==depth);
    assert(color_resource->reference.count==3 && depth_resource->reference.count==3);
    ps2_solid_screen_cleanup(&solid_screen);gc_cleanup();
}
static void rewrap(void)
{
    gc_setup(1);gc.lineStyle=LineSolid;gc.capStyle=CapButt;
    assert(ps2_solid_screen_prepare(&xs,&solid_screen,&gc_screen));
    void (*native)(DrawablePtr,GCPtr,int,xRectangle*)=gc.ops->PolyFillRect;
    gc.funcs->ChangeGC(&gc,GCForeground);gc.funcs->ValidateGC(&gc,GCAllBits,&window.drawable);
    assert(gc.ops->PolyFillRect==native);
    assert(gc.ops->CopyArea(&window.drawable,&window.drawable,&gc,1,2,3,4,5,6)==&returned_region);
    assert(gc.ops->PolyFillRect==native);text_check();
    ps2_solid_screen_cleanup(&solid_screen);assert(gc.ops->PolyFillRect!=native);
    xRectangle rect={.width=4};gc.ops->PolyFillRect(&window.drawable,&gc,1,&rect);assert(draw_delegates==2);
    gc_cleanup();
}
static void lifetime(void)
{
    solid_setup(1);ctx->destroy(ctx);release_external();screen->destroy(screen);assert(!closes);
    xSegment s={1,1,2,1};must_fail_line(&s,1);
    assert(!xs.CloseScreen(&xs));assert(closes==1 && close_calls==1 && !solid_screen.installed);
    lower_state(&gc);gc.funcs->DestroyGC(&gc);pixman_region_fini(&clip_region);
    ps2_solid_screen_cleanup(&solid_screen);assert(closes==1);
}
static void hardware_failure(void)
{
    solid_setup(1);xSegment s={1,1,20,1};unsigned before=calls;
    fail_ioctl=EIO;expect_fatal=1;
    if(!setjmp(fatal)) { gc.ops->PolySegment(&window.drawable,&gc,1,&s);assert(0); }
    assert(calls==before+1 && !logic_calls);fail_ioctl=0;
    must_fail_line(&s,1);assert(!damage_calls);expect_fatal=0;solid_cleanup();assert(closes==1);
}
static uint32_t rng=0x72abcd;
static uint32_t next(void) { rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng; }
static void random_pixels(void)
{
    solid_setup(1);
    for(unsigned i=0;i<256;++i) {
        seed(next());gc.alu=next()%16;gc.fgPixel=next();gc.planemask=next();gc.capStyle=next()%4;
        xRectangle rs[3];
        for(unsigned j=0;j<3;++j) { rs[j]=(xRectangle){(int)(next()%50)-10,(int)(next()%60)-10,next()%25,next()%25};ref_rect(rs[j],0); }
        gc.ops->PolyRectangle(&window.drawable,&gc,3,rs);equal();
        xSegment ss[4];
        for(unsigned j=0;j<4;++j) {
            int x=(int)(next()%50)-10,y=(int)(next()%60)-10;
            int x2=j&1 ? (int)(next()%50)-10 : x,y2=j&1 ? y : (int)(next()%60)-10;
            ss[j]=(xSegment){x,y,x2,y2};ref_line(x,y,x2,y2,gc.capStyle!=CapNotLast);
        }
        gc.ops->PolySegment(&window.drawable,&gc,4,ss);equal();
    }
    solid_cleanup();
}
static void installation(void)
{
    gc_setup(1);GCOps bad={0};assert(!ps2_gc_screen_set_solid_ops(&gc_screen,&bad));
    CloseScreenProcPtr lower=xs.CloseScreen;const GCOps *ops=gc.ops;
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);r->gs->caps.features&=~PS2GS_HW_SOLID_LOGIC;
    assert(!ps2_solid_screen_prepare(&xs,&solid_screen,&gc_screen));
    assert(xs.CloseScreen==lower && gc.ops==ops && !gc_screen.solid_ops);
    r->gs->caps.features|=PS2GS_HW_SOLID_LOGIC;
    assert(ps2_solid_screen_prepare(&xs,&solid_screen,&gc_screen));solid_cleanup();
}
static void batch(void)
{
    solid_setup(1);struct ps2_render_client *r=ps2_pipe_context_render(ctx);
    assert(!ps2_render_fill(r,90,90,1,1,0x80345678));
    struct ps2_pipe_solid_box boxes[]={{20,32,24,34},{30,48,35,52}};
    unsigned before=calls,target=r->target,depth=r->depth;
    boxes[1].x1=-1;
    assert(ps2_pipe_solid_rectangles(ctx,0,boxes,2,0x123456,0xffffff,GXcopy)==-1 && errno==EINVAL);
    assert(calls==before && r->count==1 && !r->gs->error);boxes[1].x1=30;
    assert(ps2_pipe_solid_rectangles(ctx,0,boxes,2,0xff123456,0xffffff,GXcopy)==-1 && calls==before);
    assert(ps2_pipe_solid_rectangles(ctx,0,boxes,16385,0,0xffffff,GXcopy)==-1 && calls==before);
    assert(!ps2_pipe_solid_rectangles(ctx,0,NULL,0,0,0xffffff,GXcopy) && calls==before && r->count==1);
    require_fence=1;
    assert(!ps2_pipe_solid_rectangles(ctx,0,boxes,2,0x123456,0xffffff,GXcopy));
    gc.fgPixel=0x123456;
    for(unsigned i=0;i<2;++i) for(int y=boxes[i].y1;y<boxes[i].y2;++y)
        for(int x=boxes[i].x1;x<boxes[i].x2;++x) ref_pixel(x,y);
    equal();assert(!r->count && !require_fence && r->target==target && r->depth==depth);
    solid_cleanup();
}
static void fast_batch(void)
{
    solid_setup(1);struct ps2_render_client *r=ps2_pipe_context_render(ctx);
    r->gs->caps.features|=PS2GS_HW_SOLID_BATCH;
    struct ps2_pipe_solid_run runs[130];
    for(unsigned i=0;i<130;++i)runs[i]=(struct ps2_pipe_solid_run){
        .box={i%100,i%80,i%100+2,i%80+2},.color=i*3871};
    for(unsigned alu=0;alu<16;++alu) {
        seed(0xa3456789);uint32_t mask=alu&1 ? 0xffffff : 0xf13c57;
        unsigned before=calls,batches=batch_calls;
        assert(!ps2_pipe_solid_runs(ctx,0,runs,130,mask,alu));
        for(unsigned i=0;i<130;++i)for(int y=runs[i].box.y1;y<runs[i].box.y2;++y)
            for(int x=runs[i].box.x1;x<runs[i].box.x2;++x)
                expected[y][x]=(expected[y][x]&~mask)|(boolean(alu,runs[i].color,expected[y][x])&mask);
        equal();assert(calls-before==(alu==GXnoop ? 0U : 3U));
        assert(batch_calls-batches==(alu==GXnoop ? 0U : 3U));
    }
    unsigned before=calls;assert(!ps2_render_fill(r,90,90,1,1,0x80345678));
    runs[129].box.x1=-1;
    assert(ps2_pipe_solid_runs(ctx,0,runs,130,0xffffff,GXcopy)==-1 && errno==EINVAL);
    assert(calls==before && r->count==1 && !r->gs->error);
    r->count=0;solid_cleanup();
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strcmp(argv[1],"fills")) fills();else if(!strcmp(argv[1],"lines")) line_case();
    else if(!strcmp(argv[1],"preflight")) preflight();else if(!strcmp(argv[1],"owned")) owned();
    else if(!strcmp(argv[1],"rewrap")) rewrap();else if(!strcmp(argv[1],"lifetime")) lifetime();
    else if(!strcmp(argv[1],"failure")) hardware_failure();else if(!strcmp(argv[1],"random")) random_pixels();
    else if(!strcmp(argv[1],"installation")) installation();else if(!strcmp(argv[1],"batch")) batch();
    else if(!strcmp(argv[1],"fast-batch"))fast_batch();else assert(0);
    puts("native Xorg solid GC test PASS");return 0;
}
