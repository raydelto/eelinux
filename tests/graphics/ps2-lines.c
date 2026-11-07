/* SPDX-License-Identifier: MIT */
#include "previous-solid.c"
#include "mi.h"
#include "miline.h"
static unsigned fail_line_at, attempted_lines;
int __wrap_ioctl(int fd,unsigned long op,...)
{
    va_list a;va_start(a,op);void *arg=va_arg(a,void*);va_end(a);
    if(op==PS2GS_SOLID_LOGIC && ++attempted_lines==fail_line_at) { ++calls;errno=EIO;return -1; }
    return solid_ioctl(fd,op,arg);
}
/* miZeroDashLine is linked with the primary reference but is not exercised. */
void miWideDash(DrawablePtr d,GCPtr g,int mode,int n,DDXPointPtr pts)
{ (void)d;(void)g;(void)mode;(void)n;(void)pts;assert(0); }
static void reference_spans(DrawablePtr d,GCPtr g,int n,DDXPointPtr pts,int *widths,int sorted)
{
    (void)d;(void)g;(void)sorted;
    for(int i=0;i<n;++i) for(int x=pts[i].x;x<pts[i].x+widths[i];++x) ref_pixel(x,pts[i].y);
}
static void upstream(int mode,int n,DDXPointPtr pts)
{
    static const GCOps ops={.FillSpans=reference_spans};GC shadow=gc;shadow.ops=&ops;
    miZeroLine(&window.drawable,&shadow,mode,n,pts);
}
static void upstream_segment(xSegment s)
{ DDXPointRec pts[]={{s.x1,s.y1},{s.x2,s.y2}};upstream(CoordModeOrigin,2,pts); }
static void check_segments(int n,xSegment *s)
{
    xSegment saved[64];assert(n<=64);memcpy(saved,s,n*sizeof(*s));
    for(int i=0;i<n;++i) upstream_segment(s[i]);
    gc.ops->PolySegment(&window.drawable,&gc,n,s);equal();assert(!memcmp(saved,s,n*sizeof(*s)));
}
static void check_line(int mode,int n,DDXPointPtr pts)
{
    DDXPointRec saved[4096];assert(n<=4096);memcpy(saved,pts,n*sizeof(*pts));
    upstream(mode,n,pts);gc.ops->Polylines(&window.drawable,&gc,mode,n,pts);
    equal();assert(!memcmp(saved,pts,n*sizeof(*pts)));
}
static void full_clip(void)
{
    window.drawable.x=window.drawable.y=0;
    pixman_region_fini(&clip_region);pixman_region_init_rect(&clip_region,0,0,128,96);
}
static void set_bias(unsigned bias)
{ dixSetPrivate(&xs.devPrivates,miZeroLineScreenKey,(void*)(uintptr_t)bias); }
static void octants(void)
{
    solid_setup(1);full_clip();gc.fgPixel=0xffffff;gc.alu=GXxor;
    xSegment segs[32];unsigned n=0;
    for(int sx=-1;sx<=1;sx+=2) for(int sy=-1;sy<=1;sy+=2)
        for(int steep=0;steep<2;++steep) {
            int x=64+sx*(steep ? 17 : 41),y=48+sy*(steep ? 37 : 13);
            segs[n++]=(xSegment){64,48,x,y};segs[n++]=(xSegment){x,y,64,48};
        }
    check_segments(n,segs);solid_cleanup();
}
static void ties(void)
{
    solid_setup(1);full_clip();gc.fgPixel=0xfedcba;gc.alu=GXcopy;
    for(unsigned bias=0;bias<256;++bias) {
        seed(0x80000000);set_bias(bias);xSegment ss[16];int n=0;
        for(int sx=-1;sx<=1;sx+=2) for(int sy=-1;sy<=1;sy+=2) for(int steep=0;steep<2;++steep) {
            int x=64+sx*(steep ? 10 : 20),y=48+sy*(steep ? 20 : 10);
            ss[n++]=(xSegment){64,48,x,y};ss[n++]=(xSegment){x,y,64,48};
        }
        check_segments(n,ss);
    }
    solid_cleanup();
}
static void clipping(void)
{
    solid_setup(1);gc.alu=GXxor;gc.fgPixel=0xabcdef;
    xSegment ss[]={{-32768,-10000,32767,10001},{-32768,10001,32767,-10000},
        {-80,-120,140,200},{140,200,-80,-120},{-80,35,140,36},{12,-300,13,300},
        {300,20,-300,45},{12,300,45,-300},{-30,-30,-1,-1},{-100,40,500,20}};
    for(unsigned mode=0;mode<2;++mode) {
        gc.miTranslate=mode;check_segments(sizeof(ss)/sizeof(*ss),ss);
    }
    /* Drawable clipping independent of a deliberately wider composite region. */
    full_clip();window.drawable.width=33;window.drawable.height=29;gc.miTranslate=1;
    xSegment edge[]={{0,0,90,80},{90,80,0,0},{-10,10,90,10},{10,-10,10,90}};
    check_segments(4,edge);solid_cleanup();
}
static void caps(void)
{
    solid_setup(1);full_clip();gc.alu=GXxor;gc.fgPixel=0xabc123;
    xSegment ss[]={{8,8,30,19},{30,19,8,8},{20,20,20,20},{-80,-40,160,80},
        {160,80,-80,-40},{3,95,100,96}};
    for(unsigned cap=0;cap<4;++cap) {gc.capStyle=cap;check_segments(6,ss);}
    DDXPointRec same[]={{21,22},{21,22}};
    for(unsigned cap=0;cap<4;++cap) {gc.capStyle=cap;check_line(CoordModeOrigin,2,same);}
    solid_cleanup();
}
static void joins(void)
{
    solid_setup(1);full_clip();gc.alu=GXxor;gc.fgPixel=0xffffff;
    DDXPointRec open[]={{5,5},{50,27},{30,70},{90,80}};
    DDXPointRec closed[]={{5,5},{50,27},{30,70},{5,5}};
    DDXPointRec previous[]={{5,5},{45,22},{-20,43},{-25,-65}};
    for(unsigned cap=0;cap<4;++cap) {
        gc.capStyle=cap;seed(0x80345678);
        check_line(CoordModeOrigin,4,open);check_line(CoordModeOrigin,4,open);
        check_line(CoordModeOrigin,4,closed);check_line(CoordModeOrigin,4,closed);
        check_line(CoordModePrevious,4,previous);check_line(CoordModePrevious,4,previous);
        for(unsigned y=0;y<96;++y)for(unsigned x=0;x<128;++x)assert(drawn[y][x]==0x80345678);
    }
    solid_cleanup();
}
static void fillstyle(void)
{
    solid_setup(1);full_clip();gc.fgPixel=0x345678;gc.alu=GXcopy;
    xSegment s={7,9,70,57};DDXPointRec p[]={{8,10},{44,45},{77,11}};
    for(unsigned style=FillSolid;style<=FillOpaqueStippled;++style) {
        gc.fillStyle=style;check_segments(1,&s);check_line(CoordModeOrigin,3,p);
    }
    solid_cleanup();
}
static void owned_lines(void)
{
    solid_setup(1);static PixmapRec front_copy;front_copy=xp;front_pixmap=&front_copy;
    int pitch;void *priv=exa.driver.CreatePixmap2(&xs,128,96,15,0,16,&pitch);assert(priv);
    xp.drawable.width=128;xp.drawable.height=96;xp.drawable.depth=15;xp.drawable.bitsPerPixel=16;xp.devPrivates=priv;
    window.drawable.depth=15;window.drawable.bitsPerPixel=16;memcpy(&expected_handle,priv,4);
#ifdef COMPOSITE
    xp.screen_x=7;xp.screen_y=9;
#endif
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);unsigned target=r->target,depth=r->depth;
    xSegment ss[]={{0,0,70,50},{70,50,0,0},{0,40,80,5},{20,10,25,65}};
    for(unsigned alu=0;alu<16;++alu) for(unsigned m=0;m<2;++m) {
        seed(0x8123);gc.alu=alu;gc.fgPixel=0xdeadbeef;gc.planemask=m ? 0x1234 : 0x7fff;
        check_segments(4,ss);assert(r->target==target && r->depth==depth);
    }
    solid_cleanup();
}
static void random_lines(void)
{
    solid_setup(1);
    for(unsigned i=0;i<512;++i) {
        seed(next());gc.alu=next()%16;gc.fgPixel=next();gc.planemask=next();gc.capStyle=next()%4;set_bias(next()%256);
        xSegment ss[8];for(unsigned j=0;j<8;++j) ss[j]=(xSegment){(int)(next()%400)-180,(int)(next()%300)-120,
            (int)(next()%400)-180,(int)(next()%300)-120};
        check_segments(8,ss);
        DDXPointRec pp[6];for(unsigned j=0;j<6;++j) pp[j]=(DDXPointRec){(int)(next()%80)-25,(int)(next()%80)-25};
        check_line(i&1 ? CoordModePrevious : CoordModeOrigin,6,pp);
    }
    solid_cleanup();
}
static unsigned emitted;
static uint8_t coverage[96][128];
static int count_run(void *data,const struct ps2_pipe_solid_box *b)
{
    (void)data;++emitted;assert(b->x1>=0 && b->y1>=0 && b->x2<=128 && b->y2<=96);
    for(int y=b->y1;y<b->y2;++y)for(int x=b->x1;x<b->x2;++x) { assert(!coverage[y][x]);coverage[y][x]=1; }
    return 0;
}
static void extreme(void)
{
    struct ps2_pipe_solid_box b={0,0,128,96};
    assert(!ps2_pipe_zero_line(INT32_MIN,INT32_MIN,INT32_MAX,INT32_MAX,0,1,&b,count_run,NULL));
    for(unsigned y=0;y<96;++y)for(unsigned x=0;x<128;++x)assert(coverage[y][x]==(x==y));
    assert(emitted==96);memset(coverage,0,sizeof(coverage));emitted=0;
    assert(!ps2_pipe_zero_line(INT32_MIN,50,INT32_MAX,50,255,1,&b,count_run,NULL));assert(emitted==1);
    for(unsigned y=0;y<96;++y)for(unsigned x=0;x<128;++x)assert(coverage[y][x]==(y==50));
    assert(ps2_pipe_zero_line(0,0,1,1,256,1,&b,count_run,NULL)==-1 && errno==EINVAL);
    solid_setup(1);full_clip();gc.alu=GXcopy;
    DDXPointRec pp[4096];pp[0]=(DDXPointRec){20,20};
    for(unsigned i=1;i<4096;++i) pp[i]=(DDXPointRec){32767,32767};
    check_line(CoordModePrevious,4096,pp);solid_cleanup();
}
static void budget(void)
{
    solid_setup(1);full_clip();gc.capStyle=CapNotLast;
    xSegment ss[257];for(unsigned i=0;i<257;++i)ss[i]=(xSegment){0,0,64,64};
    gc.ops->PolySegment(&window.drawable,&gc,256,ss);assert(logic_calls==16384);
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,90,90,1,1,0));
    unsigned before=calls,damage=damage_calls;must_fail_line(ss,257);
    assert(calls==before && r->count==1 && damage_calls==damage);r->count=0;solid_cleanup();
}
static void allocation(void)
{
    solid_setup(1);xSegment s={0,0,60,40};struct ps2_render_client *r=ps2_pipe_context_render(ctx);
    assert(!ps2_render_fill(r,90,90,1,1,0));fail_realloc=1;must_fail_line(&s,1);
    assert(r->count==1 && !damage_calls);r->count=0;solid_cleanup();
}
static void later_failure(void)
{
    solid_setup(1);full_clip();xSegment s={1,1,60,60};fail_line_at=10;expect_fatal=1;
    if(!setjmp(fatal)){gc.ops->PolySegment(&window.drawable,&gc,1,&s);assert(0);}
    assert(logic_calls==9 && !damage_calls && !sync_calls);expect_fatal=0;
    must_fail_line(&s,1);assert(attempted_lines==10);solid_cleanup();
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strcmp(argv[1],"octants"))octants();else if(!strcmp(argv[1],"ties"))ties();
    else if(!strcmp(argv[1],"clipping"))clipping();else if(!strcmp(argv[1],"caps"))caps();
    else if(!strcmp(argv[1],"joins"))joins();else if(!strcmp(argv[1],"fillstyle"))fillstyle();
    else if(!strcmp(argv[1],"owned"))owned_lines();else if(!strcmp(argv[1],"random"))random_lines();
    else if(!strcmp(argv[1],"extreme"))extreme();else if(!strcmp(argv[1],"budget"))budget();
    else if(!strcmp(argv[1],"allocation"))allocation();else if(!strcmp(argv[1],"failure"))later_failure();else assert(0);
    puts("native Xorg thin line test PASS");return 0;
}
