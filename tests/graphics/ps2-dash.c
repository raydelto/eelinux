/* SPDX-License-Identifier: MIT */
#include "previous-lines.c"
static unsigned char dash_bytes[256];
static int mutate_dash_at_fence;
int __wrap_ioctl(int fd,unsigned long op,...)
{
    va_list a;va_start(a,op);void *arg=va_arg(a,void*);va_end(a);
    if(mutate_dash_at_fence && op==PS2GS_RENDER_SUBMIT){memset(dash_bytes,1,sizeof(dash_bytes));mutate_dash_at_fence=0;}
    return lines_ioctl(fd,op,arg);
}
/* Unmodified pinned fbSegment/fbBresDash is the coverage + dash reference.
 * Reduced raster ops preserve the spare RGBX/RGB555 bit, keeping it on the
 * generic path. Unsupported optimized/non-solid reference paths are fatal. */
#include "ps2-dash-fb-stubs.h"
static uint16_t packed[96][128];
static void golden_segment(int x1,int y1,int x2,int y2,int last,int *offset)
{
    void *sp[16]={0},*gp[16]={0};ScreenRec s={.devPrivates=(void*)sp};
    dixSetPrivate(&s.devPrivates,miZeroLineScreenKey,(void*)(uintptr_t)miGetZeroLineBias(&xs));
    FbScreenPrivRec spriv={.gcPrivateKeyRec={.offset=14*sizeof(void*),.initialized=TRUE,.type=PRIVATE_GC}};
    dixSetPrivate(&s.devPrivates,&fb_screen_key,&spriv);
    FbGCPrivRec fpriv={0};GC g=gc;g.pScreen=&s;g.devPrivates=(void*)gp;g.fillStyle=FillSolid;
    dixSetPrivate(&g.devPrivates,&spriv.gcPrivateKeyRec,&fpriv);
    for(unsigned i=0;i<gc.numInDashList;++i)fpriv.dashLength+=gc.dash[i];
    unsigned bpp=window.drawable.bitsPerPixel;uint32_t mask=gc.planemask&(bpp==16?0x7fff:0xffffff);
    reduced(gc.alu,gc.fgPixel,mask,bpp,&fpriv.and,&fpriv.xor);
    reduced(gc.alu,gc.bgPixel,mask,bpp,&fpriv.bgand,&fpriv.bgxor);
    assert(fpriv.and && fpriv.bgand);
    PixmapRec p={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&s,.width=128,.height=96,.bitsPerPixel=bpp},
        .devKind=128*(bpp/8),.devPrivate={.ptr=bpp==16 ? (void*)packed : (void*)expected}};
    if(bpp==16)for(unsigned y=0;y<96;++y)for(unsigned x=0;x<128;++x)packed[y][x]=expected[y][x];
    int ox=0,oy=0;
#ifdef COMPOSITE
    ox=xp.screen_x;oy=xp.screen_y;
#endif
    RegionRec local;pixman_region_init(&local);assert(pixman_region_copy(&local,&clip_region));
    pixman_region_translate(&local,-ox,-oy);pixman_region_intersect_rect(&local,&local,0,0,128,96);g.pCompositeClip=&local;
    int tx=(gc.miTranslate ? window.drawable.x : 0)-ox,ty=(gc.miTranslate ? window.drawable.y : 0)-oy;
    fbSegment(&p.drawable,&g,x1+tx,y1+ty,x2+tx,y2+ty,last,offset);
    pixman_region_fini(&local);
    if(bpp==16)for(unsigned y=0;y<96;++y)for(unsigned x=0;x<128;++x)expected[y][x]=packed[y][x];
}
static void dash_setup(void)
{
    solid_setup(1);gc.fillStyle=FillSolid;gc.lineStyle=LineOnOffDash;
    dash_bytes[0]=5;dash_bytes[1]=3;gc.dash=dash_bytes;gc.numInDashList=2;gc.dashOffset=0;
    gc.fgPixel=0x1234ef;gc.bgPixel=0xfedc56;
}
static void dash_segments(int n,xSegment *ss)
{
    xSegment save[32];assert(n<=32);memcpy(save,ss,n*sizeof(*ss));
    for(int i=0;i<n;++i){int off=gc.dashOffset;golden_segment(ss[i].x1,ss[i].y1,ss[i].x2,ss[i].y2,gc.capStyle!=CapNotLast,&off);}
    gc.ops->PolySegment(&window.drawable,&gc,n,ss);equal();assert(!memcmp(save,ss,n*sizeof(*ss)));
}
static void dash_polyline(int mode,int n,DDXPointPtr pp)
{
    int off=gc.dashOffset,x=pp[0].x,y=pp[0].y;
    for(int i=1;i<n;++i){int nx=(mode==CoordModePrevious?x:0)+pp[i].x,ny=(mode==CoordModePrevious?y:0)+pp[i].y;
        golden_segment(x,y,nx,ny,0,&off);x=nx;y=ny;}
    if(gc.capStyle!=CapNotLast && (n==2 || x!=pp[0].x || y!=pp[0].y))golden_segment(x,y,x,y,1,&off);
    gc.ops->Polylines(&window.drawable,&gc,mode,n,pp);equal();
}
static void onoff(void)
{
    dash_setup();full_clip();gc.alu=GXxor;
    xSegment ss[16];int n=0;
    for(int sx=-1;sx<=1;sx+=2)for(int sy=-1;sy<=1;sy+=2)for(int steep=0;steep<2;++steep){
        int x=64+sx*(steep?17:41),y=48+sy*(steep?37:13);ss[n++]=(xSegment){64,48,x,y};ss[n++]=(xSegment){x,y,64,48};}
    for(unsigned bias=0;bias<256;++bias){set_bias(bias);seed(0x80345678);dash_segments(n,ss);}
    solid_cleanup();
}
static void doubled(void)
{
    dash_setup();gc.lineStyle=LineDoubleDash;
    xSegment ss[]={{0,0,70,50},{70,50,0,0},{0,40,70,5},{0,0,70,50}};
    for(unsigned alu=0;alu<16;++alu)for(unsigned m=0;m<2;++m){seed(0x80345678);gc.alu=alu;gc.planemask=m?0x1357bf:0xffffff;dash_segments(4,ss);}
    solid_cleanup();
}
static void offsets(void)
{
    dash_setup();full_clip();xSegment ss[]={{0,9,127,81},{127,81,0,9},{5,80,109,80}};
    unsigned char lists[][6]={{7,2,4,7,2,4},{1,1,1,1,1,1},{255,1,3,5,20,40}};
    for(unsigned l=0;l<3;++l){memcpy(dash_bytes,lists[l],6);gc.numInDashList=6;
        for(unsigned off=0;off<60;++off){gc.dashOffset=off;seed(0x80345678);dash_segments(3,ss);}}
    gc.dashOffset=65535;dash_segments(3,ss);solid_cleanup();
}
static void clipped(void)
{
    dash_setup();gc.lineStyle=LineDoubleDash;xSegment ss[]={{-140,-90,230,140},{230,140,-140,-90},{-90,60,180,-5},{5,-150,65,160}};
    for(unsigned off=0;off<17;++off){gc.dashOffset=off;dash_segments(4,ss);}
    solid_cleanup();
}
static void join_phase(void)
{
    dash_setup();full_clip();gc.alu=GXxor;
    DDXPointRec open[]={{5,5},{45,27},{30,80},{110,60}};
    DDXPointRec closed[]={{5,5},{45,27},{30,80},{5,5}};
    DDXPointRec previous[]={{5,5},{40,22},{-15,53},{-25,-75}};
    for(unsigned style=LineOnOffDash;style<=LineDoubleDash;++style){gc.lineStyle=style;
        for(unsigned off=0;off<12;++off){gc.dashOffset=off;seed(0x80345678);
            dash_polyline(CoordModeOrigin,4,open);dash_polyline(CoordModeOrigin,4,open);
            dash_polyline(CoordModeOrigin,4,closed);dash_polyline(CoordModeOrigin,4,closed);
            dash_polyline(CoordModePrevious,4,previous);dash_polyline(CoordModePrevious,4,previous);
            for(unsigned y=0;y<96;++y)for(unsigned x=0;x<128;++x)assert(drawn[y][x]==0x80345678);}}
    solid_cleanup();
}
static void rectangle_phase(void)
{
    dash_setup();full_clip();gc.lineStyle=LineDoubleDash;gc.alu=GXxor;
    xRectangle rr[]={{3,5,31,19},{40,12,25,47},{70,15,0,35},{78,21,30,0},{9,9,0,0}};
    for(unsigned i=0;i<5;++i){int off=gc.dashOffset,x=rr[i].x,y=rr[i].y,r=x+rr[i].width,b=y+rr[i].height;
        if(x==r || y==b)golden_segment(x,y,r,b,1,&off);
        else{golden_segment(x,y,r,y,0,&off);golden_segment(r,y,r,b,0,&off);golden_segment(r,b,x,b,0,&off);golden_segment(x,b,x,y,0,&off);}}
    gc.ops->PolyRectangle(&window.drawable,&gc,5,rr);equal();solid_cleanup();
}
static void endpoints(void)
{
    dash_setup();full_clip();xSegment ss[]={{8,8,38,29},{20,20,20,20},{-80,-40,160,80},{160,80,-80,-40}};
    for(unsigned cap=0;cap<4;++cap)for(unsigned style=1;style<=2;++style){gc.capStyle=cap;gc.lineStyle=style;dash_segments(4,ss);}
    solid_cleanup();
}
static void owned_dash(void)
{
    dash_setup();static PixmapRec f;f=xp;front_pixmap=&f;int pitch;
    void *priv=exa.driver.CreatePixmap2(&xs,128,96,15,0,16,&pitch);assert(priv);
    xp.drawable.width=128;xp.drawable.height=96;xp.drawable.depth=15;xp.drawable.bitsPerPixel=16;xp.devPrivates=priv;
    window.drawable.depth=15;window.drawable.bitsPerPixel=16;memcpy(&expected_handle,priv,4);
#ifdef COMPOSITE
    xp.screen_x=7;xp.screen_y=9;
#endif
    gc.lineStyle=LineDoubleDash;xSegment ss[]={{0,0,65,45},{65,45,0,0},{0,40,80,5}};
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);unsigned target=r->target,depth=r->depth;
    for(unsigned alu=0;alu<16;++alu){seed(0x8123);gc.alu=alu;gc.planemask=0x1357;dash_segments(3,ss);assert(r->target==target && r->depth==depth);}
    solid_cleanup();
}
static void patterned(void)
{dash_setup();xSegment s={0,0,60,40};for(unsigned f=FillTiled;f<=FillOpaqueStippled;++f){gc.fillStyle=f;must_fail_line(&s,1);}solid_cleanup();}
static void noops(void)
{
    dash_setup();full_clip();struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,90,90,1,1,0));
    unsigned before=calls;xSegment s={0,0,50,20};gc.alu=GXnoop;gc.ops->PolySegment(&window.drawable,&gc,1,&s);
    gc.alu=GXcopy;gc.planemask=0;gc.ops->PolySegment(&window.drawable,&gc,1,&s);
    gc.planemask=0xffffff;gc.dashOffset=5;s=(xSegment){1,1,2,1};gc.ops->PolySegment(&window.drawable,&gc,1,&s);
    assert(calls==before && r->count==1 && !damage_calls);r->count=0;solid_cleanup();
}
static void invalid(void)
{
    dash_setup();xSegment s={1,1,60,40};gc.numInDashList=0;must_fail_line(&s,1);gc.numInDashList=3;must_fail_line(&s,1);
    gc.numInDashList=258;must_fail_line(&s,1);gc.numInDashList=2;dash_bytes[1]=0;must_fail_line(&s,1);dash_bytes[1]=3;
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,90,90,1,1,0));unsigned before=calls;
    struct ps2_pipe_solid_run runs[]={{{1,1,3,2},0x123456},{{4,4,5,5},0x1000000}};
    assert(ps2_pipe_solid_runs(ctx,0,runs,2,0xffffff,GXcopy)==-1 && errno==EINVAL && r->count==1 && calls==before);
    runs[1].color=0; runs[1].box.x2=r->gs->caps.width+1;
    assert(ps2_pipe_solid_runs(ctx,0,runs,2,0xffffff,GXcopy)==-1 && calls==before);
    r->gs->caps.features&=~PS2GS_HW_SOLID_LOGIC;
    assert(ps2_pipe_solid_runs(ctx,0,runs,1,0xffffff,GXcopy)==-1 && errno==ENOTSUP && calls==before);
    r->gs->caps.features|=PS2GS_HW_SOLID_LOGIC;r->count=0;solid_cleanup();
}
static void dash_budget(void)
{
    dash_setup();full_clip();gc.lineStyle=LineDoubleDash;gc.capStyle=CapNotLast;dash_bytes[0]=dash_bytes[1]=1;
    xSegment ss[257];for(unsigned i=0;i<257;++i)ss[i]=(xSegment){0,0,64,64};
    gc.ops->PolySegment(&window.drawable,&gc,256,ss);assert(logic_calls==16384);
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,90,90,1,1,0));unsigned before=calls,damage=damage_calls;
    must_fail_line(ss,257);assert(calls==before && r->count==1 && damage_calls==damage);r->count=0;solid_cleanup();
}
static void dash_alloc(void)
{dash_setup();xSegment s={0,0,60,40};struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,90,90,1,1,0));
 fail_realloc=1;must_fail_line(&s,1);assert(r->count==1 && !damage_calls);r->count=0;solid_cleanup();}
static void snapshot(void)
{
    dash_setup();full_clip();gc.lineStyle=LineDoubleDash;xSegment s={5,5,100,40};int off=gc.dashOffset;
    golden_segment(s.x1,s.y1,s.x2,s.y2,1,&off);struct ps2_render_client *r=ps2_pipe_context_render(ctx);
    assert(!ps2_render_fill(r,120,90,1,1,0x80345678));mutate_dash_at_fence=1;
    gc.ops->PolySegment(&window.drawable,&gc,1,&s);equal();assert(!mutate_dash_at_fence);solid_cleanup();
}
static void bg_failure(void)
{
    dash_setup();full_clip();gc.lineStyle=LineDoubleDash;dash_bytes[0]=dash_bytes[1]=1;xSegment s={1,1,60,1};
    fail_line_at=2;expect_fatal=1;if(!setjmp(fatal)){gc.ops->PolySegment(&window.drawable,&gc,1,&s);assert(0);}
    assert(logic_calls==1 && !damage_calls && !sync_calls);expect_fatal=0;must_fail_line(&s,1);assert(attempted_lines==2);solid_cleanup();
}
static uint8_t dash_coverage[96][128];static unsigned dash_emitted;static int reject_emit;
static int collect_dash(void *data,const struct ps2_pipe_solid_box *b,unsigned background)
{
    (void)data;if(reject_emit){errno=ENOMEM;return -1;}++dash_emitted;
    for(int y=b->y1;y<b->y2;++y)for(int x=b->x1;x<b->x2;++x){assert(!dash_coverage[y][x]);dash_coverage[y][x]=background?2:1;}
    return 0;
}
static void direct(void)
{
    const unsigned char list[]={2,3,1};struct ps2_pipe_dash_pattern d={list,3,UINT64_MAX,1};struct ps2_pipe_solid_box b={0,0,128,96};
    assert(!ps2_pipe_zero_dash(INT32_MIN,50,INT32_MAX,50,0,1,&b,&d,collect_dash,NULL));
    for(unsigned x=0;x<128;++x){unsigned phase=((UINT64_MAX%12)+(((uint64_t)x+2147483648ULL)%12))%12;
        unsigned pos=0,parity=0;while(phase>=list[pos]){phase-=list[pos];pos=(pos+1)%3;parity^=1;}assert(dash_coverage[50][x]==(parity?2:1));}
    memset(dash_coverage,0,sizeof(dash_coverage));d.offset=0;d.double_dash=0;
    assert(!ps2_pipe_zero_dash(0,0,30,0,0,1,&b,&d,collect_dash,NULL));
    reject_emit=1;assert(ps2_pipe_zero_dash(0,0,5,5,0,1,&b,&d,collect_dash,NULL)==-1 && errno==ENOMEM);
    d.count=0;assert(ps2_pipe_zero_dash(0,0,5,5,0,1,&b,&d,collect_dash,NULL)==-1 && errno==EINVAL);
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strcmp(argv[1],"onoff"))onoff();else if(!strcmp(argv[1],"double"))doubled();else if(!strcmp(argv[1],"offsets"))offsets();
    else if(!strcmp(argv[1],"clipping"))clipped();else if(!strcmp(argv[1],"joins"))join_phase();else if(!strcmp(argv[1],"rectangles"))rectangle_phase();
    else if(!strcmp(argv[1],"caps"))endpoints();else if(!strcmp(argv[1],"owned"))owned_dash();else if(!strcmp(argv[1],"fillstyle"))patterned();
    else if(!strcmp(argv[1],"noop"))noops();else if(!strcmp(argv[1],"preflight"))invalid();else if(!strcmp(argv[1],"budget"))dash_budget();
    else if(!strcmp(argv[1],"allocation"))dash_alloc();else if(!strcmp(argv[1],"snapshot"))snapshot();else if(!strcmp(argv[1],"failure"))bg_failure();
    else if(!strcmp(argv[1],"direct"))direct();else assert(0);
    puts("native Xorg thin dash test PASS");return 0;
}
