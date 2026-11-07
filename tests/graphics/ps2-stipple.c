/* SPDX-License-Identifier: MIT */
#include "previous-fill.c"
static PixmapRec mono_pixmap;
static unsigned char mono_bits[8192], original_bits[8192];
static int fail_seed_at, mutate_at_fence, fail_mono_create;
static size_t fail_malloc_size;
void *__real_malloc(size_t);
void *__wrap_malloc(size_t size)
{
    if(fail_malloc_size && size==fail_malloc_size) { fail_malloc_size=0;errno=ENOMEM;return NULL; }
    return __real_malloc(size);
}
int __wrap_ioctl(int fd,unsigned long op,...)
{
    va_list args;va_start(args,op);void *arg=va_arg(args,void*);va_end(args);
    if(op==PS2GS_SURFACE_CREATE && fail_mono_create) { ++calls;errno=ENOSPC;return -1; }
    if(op==PS2GS_RENDER_SUBMIT && mutate_at_fence) { memset(mono_bits,0xff,sizeof(mono_bits));mutate_at_fence=0; }
    if(op==PS2GS_SOLID_LOGIC && fail_seed_at && --fail_seed_at==0) {
        int old=fail_ioctl;fail_ioctl=1;int status=fill_ioctl(fd,op,arg);fail_ioctl=old;return status;
    }
    return fill_ioctl(fd,op,arg);
}
static unsigned live_models(void)
{ unsigned n=0;for(unsigned i=0;i<PS2GS_MAX_SURFACES+1;++i) n+=!!models[i].data;return n; }
static void mono_setup(int redirect,int bpp,unsigned w,unsigned h)
{
    fill_setup(redirect,32,3,3);destroy_tile();
    if(bpp==16) {
        exa.driver.DestroyPixmap(&xs,xp.devPrivates);int pitch;
        xp.devPrivates=exa.driver.CreatePixmap2(&xs,128,96,15,0,16,&pitch);assert(xp.devPrivates);
        xp.drawable.depth=window.drawable.depth=15;xp.drawable.bitsPerPixel=window.drawable.bitsPerPixel=16;
    }
    memset(mono_bits,0xa5,sizeof(mono_bits));unsigned stride=((w+31)/32)*4;
    assert(stride*h<=sizeof(mono_bits));
    mono_pixmap=(PixmapRec){.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.width=w,.height=h,.depth=1,.bitsPerPixel=1},
        .devKind=stride,.devPrivate={.ptr=mono_bits}};
    for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w;++x) {
        unsigned char mask=1U<<(x%8);unsigned char *p=mono_bits+y*stride+x/8;
        if((x*5+y*3+x*y)%7<3) *p|=mask;else *p&=~mask;
    }
    gc.fillStyle=FillOpaqueStippled;gc.stipple=&mono_pixmap;gc.fgPixel=bpp==16?0x536b:0x91d3f7;
    gc.bgPixel=bpp==16?0x2143:0x135729;gc.tileIsPixel=TRUE;gc.tile.pixel=0xabcdef;
}
static unsigned mono_phase(int64_t v,unsigned size) { int64_t r=v%size;return r<0?r+size:r; }
static void mono_reference(DrawablePtr drawable,xRectangle *rects,int n,DDXPointPtr spans,int *widths,int direct,int msb)
{
    struct pixels *d=model(paint_handle());seed(d);size_t bytes=(size_t)d->width*d->height*4;
    uint32_t *expected=malloc(bytes);assert(expected);memcpy(expected,d->data,bytes);
    memcpy(original_bits,mono_bits,sizeof(mono_bits));
    uint32_t mask=drawable->depth==15?0x7fff:0xffffff,unused=drawable->depth==15?0x8000:0x80000000;
    int ox=drawable->type==DRAWABLE_WINDOW?xp.screen_x:0,oy=drawable->type==DRAWABLE_WINDOW?xp.screen_y:0;
    int tx=gc.miTranslate?drawable->x:0,ty=gc.miTranslate?drawable->y:0;unsigned changed=0;
    for(unsigned y=0;y<d->height;++y) for(unsigned x=0;x<d->width;++x) {
        int px=x+ox,py=y+oy;
        if(!direct && !pixman_region_contains_point(&clip_region,px,py,NULL)) continue;
        for(int i=0;i<n;++i) {
            int covered=rects ? px>=rects[i].x+tx && px<rects[i].x+tx+rects[i].width &&
                py>=rects[i].y+ty && py<rects[i].y+ty+rects[i].height :
                py==spans[i].y && px>=spans[i].x && px<spans[i].x+widths[i];
            if(!covered || !(gc.planemask&mask) || gc.alu==GXnoop) continue;
            unsigned u=mono_phase((int64_t)px-drawable->x-gc.patOrg.x,mono_pixmap.drawable.width);
            unsigned v=mono_phase((int64_t)py-drawable->y-gc.patOrg.y,mono_pixmap.drawable.height);
            int bit=!!(original_bits[v*mono_pixmap.devKind+u/8] & (msb?0x80U>>(u%8):1U<<(u%8)));
            expected[y*d->width+x]=((bit?gc.fgPixel:gc.bgPixel)&mask)|unused;++changed;
        }
    }
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);unsigned target=r->target,depth=r->depth;
    unsigned live=live_models(),sync=sync_calls,damage=damage_calls,pending=processed;
    xRectangle saved[8];DDXPointRec ps[8];int ws[8];assert(n<=8);
    if(rects) memcpy(saved,rects,n*sizeof(*rects));else {memcpy(ps,spans,n*sizeof(*spans));memcpy(ws,widths,n*sizeof(*widths));}
    int mutated=mutate_at_fence;
    if(direct) {
        struct ps2_pipe_mono_pattern mono={mono_bits,(size_t)mono_pixmap.devKind*mono_pixmap.drawable.height,
            mono_pixmap.devKind,mono_pixmap.drawable.width,mono_pixmap.drawable.height,msb};
        struct ps2_pipe_solid_box boxes[8];
        for(int i=0;i<n;++i) boxes[i]=(struct ps2_pipe_solid_box){rects[i].x+tx-ox,rects[i].y+ty-oy,
            rects[i].x+tx-ox+rects[i].width,rects[i].y+ty-oy+rects[i].height};
        assert(!ps2_pipe_opaque_stipple_rectangles(ctx,paint_handle(),&mono,boxes,n,
            drawable->x+gc.patOrg.x-ox,drawable->y+gc.patOrg.y-oy,gc.fgPixel&mask,gc.bgPixel&mask));
        assert(sync_calls==sync && damage_calls==damage && processed==pending);
    } else {
        if(rects) gc.ops->PolyFillRect(drawable,&gc,n,rects);else gc.ops->FillSpans(drawable,&gc,n,spans,widths,0);
        assert(sync_calls==sync+!!changed && damage_calls==damage+!!changed && processed==pending+!!changed);
        if(changed) assert(notified==drawable && pixman_region_equal(&damage_region,&clip_region));
    }
    assert(!memcmp(expected,d->data,bytes));assert(live_models()==live);
    if(!mutated) assert(!memcmp(original_bits,mono_bits,sizeof(mono_bits)));else memcpy(mono_bits,original_bits,sizeof(mono_bits));
    if(rects) assert(!memcmp(saved,rects,n*sizeof(*rects)));else {assert(!memcmp(ps,spans,n*sizeof(*spans)));assert(!memcmp(ws,widths,n*sizeof(*widths)));}
    assert(r->target==target && r->depth==depth && color_resource->reference.count==3 && depth_resource->reference.count==3);
    free(expected);
}
static void opaque_rectangles_case(void)
{
    mono_setup(0,32,7,5);xRectangle r[]={{-5,-4,70,45},{34,8,40,50},{1,1,0,0}};
    mono_reference(&window.drawable,r,3,NULL,NULL,0,0);assert(logic_calls && tile_submits && local_copies && !draw_delegates);
    gc.patOrg=(DDXPointRec){17,-27};mono_reference(&window.drawable,r,3,NULL,NULL,0,0);fill_cleanup();
}
static void opaque_spans_case(void)
{
    mono_setup(0,32,9,3);DDXPointRec p[]={{15,34},{30,44},{87,65},{25,71}};int w[]={99,0,25,43};
    for(int mode=0;mode<2;++mode) {gc.miTranslate=mode;mono_reference(&window.drawable,NULL,4,p,w,0,0);}
    gc.miTranslate=FALSE;xRectangle r={20,30,80,45};mono_reference(&window.drawable,&r,1,NULL,NULL,0,0);fill_cleanup();
}
static void opaque_redirected_case(void)
{
    mono_setup(1,16,11,7);xRectangle r[]={{-3,-2,70,70},{20,15,80,31}};
    mono_reference(&window.drawable,r,2,NULL,NULL,0,0);xp.drawable.x=7;xp.drawable.y=9;gc.patOrg=(DDXPointRec){-17,21};
    mono_reference(&xp.drawable,r,2,NULL,NULL,0,0);fill_cleanup();
}
static void opaque_colors_case(void)
{
    mono_setup(1,32,13,5);xRectangle r={0,0,60,50};
    for(int kind=0;kind<3;++kind) {memset(mono_bits,kind?0xff:0,sizeof(mono_bits));if(kind==2) gc.fgPixel=gc.bgPixel;
        mono_reference(&window.drawable,&r,1,NULL,NULL,0,0);}
    fill_cleanup();
}
static void opaque_large_case(void)
{
    mono_setup(0,32,256,256);gc.miTranslate=FALSE;RegionReset(&clip_region,&(BoxRec){0,0,720,576});
    xRectangle r={0,0,720,576};gc.patOrg=(DDXPointRec){-255,253};mono_reference(&window.drawable,&r,1,NULL,NULL,0,0);fill_cleanup();
}
static void opaque_tiny_case(void)
{
    mono_setup(0,32,2,2);gc.miTranslate=FALSE;RegionReset(&clip_region,&(BoxRec){0,0,720,576});xRectangle r={0,0,720,576};
    for(int size=1;size<=2;++size) {mono_pixmap.drawable.width=mono_pixmap.drawable.height=size;
        unsigned before=output_submits;mono_reference(&window.drawable,&r,1,NULL,NULL,0,0);assert(output_submits-before<=10);}
    fill_cleanup();
}
static void opaque_noop_case(void)
{
    mono_setup(1,32,5,3);struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,1,1,0));
    unsigned before=calls;xRectangle box={0,0,50,50};gc.alu=GXnoop;gc.ops->PolyFillRect(&window.drawable,&gc,1,&box);
    gc.alu=GXxor;gc.planemask=0;gc.ops->PolyFillRect(&window.drawable,&gc,1,&box);gc.alu=GXcopy;gc.planemask=~0U;
    box.width=0;gc.ops->PolyFillRect(&window.drawable,&gc,1,&box);gc.ops->FillSpans(&window.drawable,&gc,0,NULL,NULL,0);
    RegionEmpty(&clip_region);box.width=50;gc.ops->PolyFillRect(&window.drawable,&gc,1,&box);
    assert(calls==before && r->count==1 && !damage_calls);r->count=0;fill_cleanup();
}
static void opaque_preflight_case(void)
{
    mono_setup(1,32,17,3);struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,1,1,0));
    xRectangle box={0,0,50,50};
    /* Opaque ALUs and partial masks are covered by the general opaque suite. */
    /* FillStippled is covered by the transparent suite; it is now supported. */
    PixmapRec valid=mono_pixmap;gc.stipple=NULL;fill_reject(1,&box,NULL,NULL,0);gc.stipple=&mono_pixmap;
    mono_pixmap.drawable.depth=24;fill_reject(1,&box,NULL,NULL,0);mono_pixmap=valid;
    mono_pixmap.drawable.bitsPerPixel=8;fill_reject(1,&box,NULL,NULL,0);mono_pixmap=valid;
    mono_pixmap.drawable.width=257;fill_reject(1,&box,NULL,NULL,0);mono_pixmap=valid;
    mono_pixmap.devKind=2;fill_reject(1,&box,NULL,NULL,0);mono_pixmap=valid;
    mono_pixmap.devPrivate.ptr=NULL;fill_reject(1,&box,NULL,NULL,0);mono_pixmap=valid;
    ScreenRec foreign={0};mono_pixmap.drawable.pScreen=&foreign;fill_reject(1,&box,NULL,NULL,0);mono_pixmap=valid;
    DDXPointRec points[]={{25,34},{25,35}};int widths[]={3,-1};fill_reject(2,NULL,points,widths,0);
    struct ps2_pipe_mono_pattern mono={mono_bits,sizeof(mono_bits),4,17,3,0};struct ps2_pipe_solid_box jobs[]={{0,0,10,10},{0,0,129,1}};
    unsigned before=calls;assert(ps2_pipe_opaque_stipple_rectangles(ctx,paint_handle(),&mono,jobs,2,0,0,1,2)==-1 && calls==before);
    mono.bytes=1;assert(ps2_pipe_opaque_stipple_rectangles(ctx,paint_handle(),&mono,jobs,1,0,0,1,2)==-1 && calls==before);mono.bytes=sizeof(mono_bits);
    mono.msb_first=2;assert(ps2_pipe_opaque_stipple_rectangles(ctx,paint_handle(),&mono,jobs,1,0,0,1,2)==-1 && calls==before);mono.msb_first=0;
    uint32_t features=r->gs->caps.features;r->gs->caps.features&=~PS2GS_HW_SOLID_LOGIC;
    assert(ps2_pipe_opaque_stipple_rectangles(ctx,paint_handle(),&mono,jobs,1,0,0,1,2)==-1 && errno==ENOTSUP && calls==before);r->gs->caps.features=features;
    assert(r->count==1 && !render_calls && !damage_calls);r->count=0;fill_cleanup();
}
static void opaque_allocation_case(void)
{
    mono_setup(1,32,17,3);struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,1,1,0));
    xRectangle box={0,0,50,50};fail_malloc_size=9;fill_reject(1,&box,NULL,NULL,0);assert(r->count==1 && !damage_calls);
    fail_mono_create=1;fill_reject(1,&box,NULL,NULL,2);
    assert(r->count==1 && !r->gs->error && !damage_calls);
    fail_mono_create=0;gc.ops->PolyFillRect(&window.drawable,&gc,1,&box);
    assert(!r->count && !r->gs->error && damage_calls==1);fill_cleanup();
}
static void opaque_failure_case(int kind)
{
    mono_setup(kind==2?0:1,32,2,2);struct pixels *d=model(paint_handle());seed(d);size_t bytes=(size_t)d->width*d->height*4;
    uint32_t *saved=malloc(bytes);assert(saved);memcpy(saved,d->data,bytes);xRectangle box={0,0,70,50};
    if(kind==2) {gc.miTranslate=FALSE;RegionReset(&clip_region,&(BoxRec){0,0,720,576});box=(xRectangle){0,0,640,256};}
    if(kind==0) fail_seed_at=2;else if(kind==1) fail_copy_at=1;else fail_texture_at=3;
    expect_fatal=1;if(!setjmp(fatal)) {gc.ops->PolyFillRect(&window.drawable,&gc,1,&box);assert(0);}
    assert(!expect_fatal && ps2_pipe_context_error(ctx)==EIO && !damage_calls && !processed);
    if(kind<2) assert(!memcmp(saved,d->data,bytes));
    unsigned before=calls;fill_reject(1,&box,NULL,NULL,0);assert(calls==before);free(saved);fill_cleanup();
}
static void opaque_bitorder_case(void)
{
    mono_setup(0,32,17,3);gc.miTranslate=FALSE;gc.patOrg=(DDXPointRec){-32760,32760};
    xRectangle box={20,30,50,40};for(int msb=0;msb<2;++msb) mono_reference(&window.drawable,&box,1,NULL,NULL,1,msb);
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,1,1,0));mutate_at_fence=1;
    mono_reference(&window.drawable,&box,1,NULL,NULL,1,0);assert(!mutate_at_fence);fill_cleanup();
}
static void opaque_random_case(void)
{
    mono_setup(1,32,17,9);unsigned state=0x519e;
    for(unsigned i=0;i<32;++i) {
        state=state*1664525+1013904223;gc.patOrg.x=(int)(state%101)-50;
        state=state*1664525+1013904223;gc.patOrg.y=(int)(state%103)-51;
        state=state*1664525+1013904223;mono_pixmap.drawable.width=1+state%33;
        state=state*1664525+1013904223;mono_pixmap.drawable.height=1+state%31;
        mono_pixmap.devKind=((mono_pixmap.drawable.width+31)/32)*4;
        for(unsigned j=0;j<(unsigned)mono_pixmap.devKind*mono_pixmap.drawable.height;++j) {state=state*1664525+1013904223;mono_bits[j]=state>>24;}
        gc.funcs->ValidateGC(&gc,GCAllBits,&window.drawable);
        xRectangle boxes[]={{-3,-1,45,41},{20,7,73,38}};mono_reference(&window.drawable,boxes,2,NULL,NULL,0,0);
    }
    fill_cleanup();
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strcmp(argv[1],"rectangles")) opaque_rectangles_case();else if(!strcmp(argv[1],"spans")) opaque_spans_case();
    else if(!strcmp(argv[1],"redirected")) opaque_redirected_case();else if(!strcmp(argv[1],"colors")) opaque_colors_case();
    else if(!strcmp(argv[1],"large")) opaque_large_case();else if(!strcmp(argv[1],"tiny")) opaque_tiny_case();
    else if(!strcmp(argv[1],"noop")) opaque_noop_case();else if(!strcmp(argv[1],"preflight")) opaque_preflight_case();
    else if(!strcmp(argv[1],"allocation")) opaque_allocation_case();else if(!strcmp(argv[1],"seedfailure")) opaque_failure_case(0);
    else if(!strcmp(argv[1],"expandfailure")) opaque_failure_case(1);else if(!strcmp(argv[1],"laterfailure")) opaque_failure_case(2);
    else if(!strcmp(argv[1],"bitorder")) opaque_bitorder_case();else if(!strcmp(argv[1],"random")) opaque_random_case();else assert(0);
    puts("native opaque stipple case PASS");return 0;
}
