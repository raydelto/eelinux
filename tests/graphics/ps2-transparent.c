/* SPDX-License-Identifier: MIT */
#include "previous-mono.c"
static unsigned transparent_packets,transparent_commands;
static int fail_packet_at,mutate_owned;
int __wrap_ioctl(int fd,unsigned long op,...)
{
    va_list args;va_start(args,op);void *arg=va_arg(args,void*);va_end(args);
    if(op==PS2GS_RENDER_SUBMIT && mutate_owned) {
        struct ps2_mono_source source=mono_view();memset((void*)source.bits,0xff,source.bytes);mutate_owned=0;
    }
    if(op==PS2GS_RENDER_SUBMIT && fail_packet_at && --fail_packet_at==0) {++calls;errno=EIO;return -1;}
    /* The older glyph fixture models a 128x96 destination. This suite has
     * its own full-sized pixel model below, including the front buffer. */
    uint32_t inspected=inspected_target;
    if(op==PS2GS_RENDER_SUBMIT) inspected_target=UINT32_MAX;
    int result=stippler_ioctl(fd,op,arg);
    inspected_target=inspected;
    if(!result && op==PS2GS_RENDER_SUBMIT) {
        const struct ps2gs_render_submit *s=arg;const struct ps2gs_command *c=(void*)(uintptr_t)s->commands;
        struct pixels *d=model(s->target);assert(s->count && s->count<=64);
        ++transparent_packets;transparent_commands+=s->count;
        for(unsigned i=0;i<s->count;++i) {
            assert(c[i].op==PS2GS_FILL && !c[i].flags);
            unsigned x=c[i].v[0].x/16,y=c[i].v[0].y/16;
            assert(x+c[i].width<=d->width && y+c[i].height<=d->height);
            uint32_t color=c[i].v[0].rgba;
            if(s->bits_per_pixel==16) color=0x8000|((color&255)>>3)|(((color>>8)&255)>>3<<5)|(((color>>16)&255)>>3<<10);
            for(unsigned j=y;j<y+c[i].height;++j) for(unsigned k=x;k<x+c[i].width;++k) d->data[j*d->width+k]=color;
        }
    }
    return result;
}
static void transparent_setup(int redirect,int bpp,unsigned w,unsigned h)
{ mono_setup(redirect,bpp,w,h);gc.fillStyle=FillStippled; }
static void transparent_reference(DrawablePtr drawable,xRectangle *rects,int n,DDXPointPtr spans,int *widths,int direct,int msb)
{
    struct pixels *d=model(paint_handle());seed(d);
    uint32_t mask=drawable->depth==15?0x7fff:0xffffff,unused=drawable->depth==15?0x8000:0x80000000;
    for(unsigned i=0;i<d->width*d->height;++i) d->data[i]=(d->data[i]&mask)|unused;
    size_t bytes=(size_t)d->width*d->height*4;uint32_t *expected=malloc(bytes);assert(expected);memcpy(expected,d->data,bytes);
    const unsigned char *source=gc.stipple==&owned_mono ? mono_view().bits : mono_bits;
    memcpy(original_bits,source,mono_pixmap.devKind*mono_pixmap.drawable.height);
    int ox=drawable->type==DRAWABLE_WINDOW?xp.screen_x:0,oy=drawable->type==DRAWABLE_WINDOW?xp.screen_y:0;
    int tx=gc.miTranslate?drawable->x:0,ty=gc.miTranslate?drawable->y:0;unsigned changed=0;
    for(unsigned y=0;y<d->height;++y) for(unsigned x=0;x<d->width;++x) {
        int px=x+ox,py=y+oy;if(!direct && !pixman_region_contains_point(&clip_region,px,py,NULL)) continue;
        for(int i=0;i<n;++i) {
            int covered=rects ? px>=rects[i].x+tx && px<rects[i].x+tx+rects[i].width &&
                py>=rects[i].y+ty && py<rects[i].y+ty+rects[i].height :
                py==spans[i].y && px>=spans[i].x && px<spans[i].x+widths[i];
            if(!covered || !(gc.planemask&mask) || gc.alu==GXnoop) continue;
            unsigned u=mono_phase((int64_t)px-drawable->x-gc.patOrg.x,mono_pixmap.drawable.width);
            unsigned v=mono_phase((int64_t)py-drawable->y-gc.patOrg.y,mono_pixmap.drawable.height);
            if(!(original_bits[v*mono_pixmap.devKind+u/8]&(msb?0x80U>>(u%8):1U<<(u%8)))) continue;
            uint32_t *pixel=expected+y*d->width+x,pm=gc.planemask&mask;
            *pixel=(*pixel&~pm)|(fill_boolean(gc.alu,gc.fgPixel,*pixel)&pm);++changed;
        }
    }
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);unsigned target=r->target,depth=r->depth,live=live_models();
    unsigned sync=sync_calls,damage=damage_calls,pending=processed;
    xRectangle saved[8];DDXPointRec ps[8];int ws[8];assert(n<=8);
    if(rects) memcpy(saved,rects,n*sizeof(*rects));else {memcpy(ps,spans,n*sizeof(*spans));memcpy(ws,widths,n*sizeof(*widths));}
    int mutated=mutate_owned;
    if(direct) {
        struct ps2_pipe_mono_pattern mono={source,(size_t)mono_pixmap.devKind*mono_pixmap.drawable.height,
            mono_pixmap.devKind,mono_pixmap.drawable.width,mono_pixmap.drawable.height,msb};
        struct ps2_pipe_solid_box boxes[8];
        for(int i=0;i<n;++i) boxes[i]=(struct ps2_pipe_solid_box){rects[i].x+tx-ox,rects[i].y+ty-oy,
            rects[i].x+tx-ox+rects[i].width,rects[i].y+ty-oy+rects[i].height};
        unsigned emitted=123;
        assert(!ps2_pipe_stipple_rectangles(ctx,paint_handle(),&mono,boxes,n,drawable->x+gc.patOrg.x-ox,
            drawable->y+gc.patOrg.y-oy,gc.fgPixel&mask,gc.planemask&mask,gc.alu,&emitted));
        assert(!!emitted==!!changed && sync_calls==sync && damage_calls==damage && processed==pending);
    } else {
        if(rects) gc.ops->PolyFillRect(drawable,&gc,n,rects);else gc.ops->FillSpans(drawable,&gc,n,spans,widths,0);
        assert(sync_calls==sync+!!changed && damage_calls==damage+!!changed && processed==pending+!!changed);
        if(changed) assert(notified==drawable && pixman_region_equal(&damage_region,&clip_region));
    }
    assert(!memcmp(expected,d->data,bytes) && live_models()==live);
    if(!mutated) assert(!memcmp(original_bits,source,mono_pixmap.devKind*mono_pixmap.drawable.height));
    if(rects) assert(!memcmp(saved,rects,n*sizeof(*rects)));else {assert(!memcmp(ps,spans,n*sizeof(*spans)));assert(!memcmp(ws,widths,n*sizeof(*widths)));}
    assert(r->target==target && r->depth==depth && color_resource->reference.count==3 && depth_resource->reference.count==3);
    free(expected);
}
static void trans_rectangles_case(void)
{
    transparent_setup(0,32,7,5);xRectangle r[]={{-5,-4,70,45},{34,8,40,50},{1,1,0,0}};
    transparent_reference(&window.drawable,r,3,NULL,NULL,0,0);gc.patOrg=(DDXPointRec){17,-27};
    transparent_reference(&window.drawable,r,3,NULL,NULL,0,0);assert(transparent_packets && !tile_submits);fill_cleanup();
}
static void trans_spans_case(void)
{
    transparent_setup(0,32,9,3);DDXPointRec p[]={{15,34},{30,44},{87,65},{25,71}};int w[]={99,0,25,43};
    for(int mode=0;mode<2;++mode) {gc.miTranslate=mode;transparent_reference(&window.drawable,NULL,4,p,w,0,0);}
    gc.miTranslate=FALSE;xRectangle r={20,30,80,45};transparent_reference(&window.drawable,&r,1,NULL,NULL,0,0);fill_cleanup();
}
static void trans_redirected_case(void)
{
    transparent_setup(1,16,11,7);xRectangle r[]={{-3,-2,70,70},{20,15,80,31}};
    transparent_reference(&window.drawable,r,2,NULL,NULL,0,0);xp.drawable.x=7;xp.drawable.y=9;gc.patOrg=(DDXPointRec){-17,21};
    transparent_reference(&xp.drawable,r,2,NULL,NULL,0,0);fill_cleanup();
}
static void trans_alu_case(void)
{
    for(int bpp=16;bpp<=32;bpp+=16) {
        transparent_setup(1,bpp,13,5);xRectangle r[]={{0,0,65,51},{17,7,65,61},{24,19,45,42}};
        for(unsigned alu=0;alu<16;++alu) for(unsigned partial=0;partial<2;++partial) {
            gc.alu=alu;gc.planemask=partial ? (bpp==16?0x5295:0xa5c369) : ~0U;
            transparent_reference(&window.drawable,r,3,NULL,NULL,0,0);
        }
        assert(logic_calls);fill_cleanup();
    }
}
static void trans_owned_case(void)
{
    transparent_setup(1,32,21,15);use_owned();xRectangle r={0,0,90,70};
    transparent_reference(&window.drawable,&r,1,NULL,NULL,0,0);assert(!owned_mono.devPrivate.ptr);
    destroy_mono();fill_cleanup();
}
static void trans_noop_case(void)
{
    transparent_setup(1,32,5,3);struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,1,1,0));
    unsigned before=calls;xRectangle box={0,0,50,50};gc.alu=GXnoop;gc.ops->PolyFillRect(&window.drawable,&gc,1,&box);
    gc.alu=GXxor;gc.planemask=0;gc.ops->PolyFillRect(&window.drawable,&gc,1,&box);gc.alu=GXcopy;gc.planemask=~0U;
    box.width=0;gc.ops->PolyFillRect(&window.drawable,&gc,1,&box);gc.ops->FillSpans(&window.drawable,&gc,0,NULL,NULL,0);
    RegionEmpty(&clip_region);box.width=50;gc.ops->PolyFillRect(&window.drawable,&gc,1,&box);
    assert(calls==before && r->count==1 && !damage_calls);r->count=0;fill_cleanup();
}
static void trans_empty_case(void)
{
    transparent_setup(1,32,17,3);memset(mono_bits,0,sizeof(mono_bits));
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,1,1,0));unsigned before=calls;
    xRectangle box={0,0,90,70};gc.ops->PolyFillRect(&window.drawable,&gc,1,&box);
    assert(calls==before && r->count==1 && !damage_calls);r->count=0;fill_cleanup();
}
static void trans_large_case(void)
{
    transparent_setup(0,32,2,2);mono_bits[0]=1;mono_bits[4]=2;
    gc.miTranslate=FALSE;gc.patOrg=(DDXPointRec){0,0};RegionReset(&clip_region,&(BoxRec){0,0,720,576});xRectangle r={0,0,720,576};
    transparent_reference(&window.drawable,&r,1,NULL,NULL,0,0);assert(transparent_commands==207360 && transparent_packets==3240);
    memset(mono_bits,0xff,sizeof(mono_bits));unsigned before=transparent_commands;
    transparent_reference(&window.drawable,&r,1,NULL,NULL,0,0);assert(transparent_commands-before==1);
    fill_cleanup();
}
static void trans_preflight_case(void)
{
    transparent_setup(1,32,17,3);struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,1,1,0));
    xRectangle box={0,0,50,50};PixmapRec saved=mono_pixmap;mono_pixmap.devKind=2;fill_reject(1,&box,NULL,NULL,0);mono_pixmap=saved;
    mono_pixmap.drawable.width=257;fill_reject(1,&box,NULL,NULL,0);mono_pixmap=saved;
    DDXPointRec p[]={{25,34},{25,35}};int widths[]={3,-1};fill_reject(2,NULL,p,widths,0);
    struct ps2_pipe_mono_pattern mono={mono_bits,sizeof(mono_bits),4,17,3,0};struct ps2_pipe_solid_box b[]={{0,0,10,10},{0,0,129,1}};
    unsigned before=calls,emitted=99;
    assert(ps2_pipe_stipple_rectangles(ctx,paint_handle(),&mono,b,2,0,0,1,0xffffff,GXcopy,&emitted)==-1 && !emitted && calls==before);
    mono.bytes=1;assert(ps2_pipe_stipple_rectangles(ctx,paint_handle(),&mono,b,1,0,0,1,0xffffff,GXcopy,NULL)==-1 && calls==before);
    mono.bytes=sizeof(mono_bits);mono.msb_first=2;
    assert(ps2_pipe_stipple_rectangles(ctx,paint_handle(),&mono,b,1,0,0,1,0xffffff,GXcopy,NULL)==-1 && calls==before);
    mono.msb_first=0;uint32_t features=r->gs->caps.features;r->gs->caps.features&=~(PS2GS_HW_SOLID_LOGIC|PS2GS_HW_OWNED_RENDER);
    assert(ps2_pipe_stipple_rectangles(ctx,paint_handle(),&mono,b,1,0,0,1,0xffffff,GXcopy,NULL)==-1 && errno==ENOTSUP && calls==before);
    r->gs->caps.features=features;assert(r->count==1 && !damage_calls);r->count=0;fill_cleanup();
}
static void trans_budget_case(void)
{
    transparent_setup(0,32,2,2);mono_bits[0]=1;mono_bits[4]=2;
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,1,1,0));
    struct ps2_pipe_mono_pattern mono={mono_bits,8,4,2,2,0};struct ps2_pipe_solid_box boxes[]={{0,0,720,576},{0,0,720,576},{0,0,720,576}};
    unsigned before=calls;
    assert(ps2_pipe_stipple_rectangles(ctx,0,&mono,boxes,2,0,0,1,0xffffff,GXcopy,NULL)==-1 && errno==E2BIG && calls==before);
    assert(ps2_pipe_stipple_rectangles(ctx,0,&mono,boxes,3,0,0,1,0xffffff,GXcopy,NULL)==-1 && errno==E2BIG && calls==before);
    boxes[0]=(struct ps2_pipe_solid_box){0,0,256,256};
    assert(ps2_pipe_stipple_rectangles(ctx,0,&mono,boxes,1,0,0,1,0xffffff,GXxor,NULL)==-1 && errno==E2BIG && calls==before);
    assert(r->count==1 && !damage_calls);r->count=0;fill_cleanup();
}
static void trans_allocation_case(void)
{
    transparent_setup(1,32,17,3);struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,1,1,0));
    fail_malloc_size=9;xRectangle box={0,0,50,50};fill_reject(1,&box,NULL,NULL,0);assert(r->count==1 && !damage_calls);r->count=0;fill_cleanup();
}
static void trans_snapshot_case(void)
{
    transparent_setup(1,32,17,9);use_owned();struct ps2_render_client *r=ps2_pipe_context_render(ctx);
    assert(!ps2_render_fill(r,0,0,1,1,0));mutate_owned=1;xRectangle box={0,0,90,70};
    transparent_reference(&window.drawable,&box,1,NULL,NULL,0,0);assert(!mutate_owned);
    struct ps2_mono_source source=mono_view();for(size_t i=0;i<source.bytes;++i) assert(source.bits[i]==0xff);
    destroy_mono();fill_cleanup();
}
static void trans_failure_case(int later)
{
    transparent_setup(1,32,2,2);mono_bits[0]=1;mono_bits[4]=2;struct pixels *d=model(paint_handle());seed(d);
    size_t bytes=(size_t)d->width*d->height*4;uint32_t *saved=malloc(bytes);assert(saved);memcpy(saved,d->data,bytes);
    xRectangle box={0,0,110,85};fail_packet_at=later?3:1;expect_fatal=1;
    if(!setjmp(fatal)) {gc.ops->PolyFillRect(&window.drawable,&gc,1,&box);assert(0);}
    assert(ps2_pipe_context_error(ctx)==EIO && !damage_calls && !sync_calls);
    assert(later ? memcmp(saved,d->data,bytes)!=0 : memcmp(saved,d->data,bytes)==0);
    unsigned before=calls;expect_fatal=1;
    if(!setjmp(fatal)) {gc.funcs->ValidateGC(&gc,GCAllBits,&window.drawable);assert(0);}
    assert(calls==before);free(saved);fill_cleanup();
}
static void trans_random_case(void)
{
    transparent_setup(1,32,17,9);unsigned state=0x987651;
    for(unsigned i=0;i<32;++i) {
        state=state*1664525+1013904223;gc.patOrg.x=(int)(state%101)-50;
        state=state*1664525+1013904223;gc.patOrg.y=(int)(state%103)-51;
        state=state*1664525+1013904223;mono_pixmap.drawable.width=1+state%33;
        state=state*1664525+1013904223;mono_pixmap.drawable.height=1+state%31;
        mono_pixmap.devKind=((mono_pixmap.drawable.width+31)/32)*4;
        for(unsigned j=0;j<(unsigned)mono_pixmap.devKind*mono_pixmap.drawable.height;++j) {state=state*1664525+1013904223;mono_bits[j]=state>>24;}
        gc.alu=i%16;gc.planemask=i%3?~0U:0x5193ce;gc.funcs->ValidateGC(&gc,GCAllBits,&window.drawable);
        xRectangle boxes[]={{-3,-1,45,41},{20,7,73,38}};transparent_reference(&window.drawable,boxes,2,NULL,NULL,0,0);
    }
    fill_cleanup();
}
static void trans_bitorders_case(void)
{
    transparent_setup(1,32,17,3);gc.miTranslate=FALSE;gc.patOrg=(DDXPointRec){-32760,32760};
    xRectangle box={0,0,80,70};for(int msb=0;msb<2;++msb) transparent_reference(&xp.drawable,&box,1,NULL,NULL,1,msb);
    struct ps2_pipe_mono_pattern mono={mono_bits,sizeof(mono_bits),4,17,3,0};struct ps2_pipe_solid_box b={0,0,20,20};unsigned emitted;
    assert(!ps2_pipe_stipple_rectangles(ctx,paint_handle(),&mono,&b,1,INT32_MIN,INT32_MAX,1,0xffffff,GXcopy,&emitted));
    assert(emitted);fill_cleanup();
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strcmp(argv[1],"rectangles")) trans_rectangles_case();else if(!strcmp(argv[1],"spans")) trans_spans_case();
    else if(!strcmp(argv[1],"redirected")) trans_redirected_case();else if(!strcmp(argv[1],"alu")) trans_alu_case();
    else if(!strcmp(argv[1],"owned")) trans_owned_case();else if(!strcmp(argv[1],"noop")) trans_noop_case();
    else if(!strcmp(argv[1],"empty")) trans_empty_case();else if(!strcmp(argv[1],"large")) trans_large_case();
    else if(!strcmp(argv[1],"preflight")) trans_preflight_case();else if(!strcmp(argv[1],"budget")) trans_budget_case();
    else if(!strcmp(argv[1],"allocation")) trans_allocation_case();else if(!strcmp(argv[1],"snapshot")) trans_snapshot_case();
    else if(!strcmp(argv[1],"firstfailure")) trans_failure_case(0);else if(!strcmp(argv[1],"laterfailure")) trans_failure_case(1);
    else if(!strcmp(argv[1],"random")) trans_random_case();else if(!strcmp(argv[1],"bitorders")) trans_bitorders_case();else assert(0);
    puts("native transparent stipple case PASS");return 0;
}
