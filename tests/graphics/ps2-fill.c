/* SPDX-License-Identifier: MIT */
#include "previous-tiled.c"
#include "ps2_fill.h"
#include "ps2_solid.h"
static struct ps2_fill_screen fill_owner;
static struct ps2_solid_screen fill_solid;
int __wrap_ioctl(int fd,unsigned long op,...)
{
    va_list args;va_start(args,op);void *arg=va_arg(args,void*);va_end(args);
    if(op==PS2GS_RENDER_SUBMIT) return glyph_ioctl(fd,op,arg);
    if(op!=PS2GS_SOLID_LOGIC) return tile_ioctl(fd,op,arg);
    assert(fd==999);++calls;
    if(fail_ioctl) { errno=EIO;return -1; }
    const struct ps2gs_solid_logic *r=arg;struct pixels *p=model(r->handle);
    assert(r->abi==1 && r->alu<16 && r->width && r->height);
    assert(r->x+r->width<=p->width && r->y+r->height<=p->height);
    for(unsigned y=r->y;y<r->y+r->height;++y) for(unsigned x=r->x;x<r->x+r->width;++x) {
        uint32_t a=r->color,b=p->data[y*p->width+x],v=0;
        if(r->alu&1) v|=a&b;
        if(r->alu&2) v|=a&~b;
        if(r->alu&4) v|=~a&b;
        if(r->alu&8) v|=~a&~b;
        p->data[y*p->width+x]=(b&~r->plane_mask)|(v&r->plane_mask);
    }
    ++logic_calls;return 0;
}
static uint32_t fill_boolean(unsigned alu,uint32_t a,uint32_t b)
{
    switch(alu) {
    case GXclear:return 0;case GXand:return a&b;case GXandReverse:return a&~b;case GXcopy:return a;
    case GXandInverted:return ~a&b;case GXnoop:return b;case GXxor:return a^b;case GXor:return a|b;
    case GXnor:return ~(a|b);case GXequiv:return ~(a^b);case GXinvert:return ~b;case GXorReverse:return a|~b;
    case GXcopyInverted:return ~a;case GXorInverted:return ~a|b;case GXnand:return ~(a&b);case GXset:return ~0U;
    default:assert(0);return 0;
    }
}
static void fill_setup(int redirected,int bpp,unsigned tw,unsigned th)
{
    paint_setup(redirected,bpp);
    assert(ps2_solid_screen_prepare(&xs,&fill_solid,&gc_screen));
    assert(ps2_fill_screen_prepare(&xs,&fill_owner,&gc_screen));
    make_tile(tw,th,bpp);gc.fillStyle=FillTiled;gc.tileIsPixel=FALSE;gc.tile.pixmap=&tile_pixmap;
    gc.patOrg=(DDXPointRec){-9,-11};gc.alu=GXcopy;gc.planemask=~0U;
    pixman_region_fini(&clip_region);
    pixman_box16_t boxes[]={{20,30,70,55},{85,30,120,55},{25,60,130,95}};
    assert(pixman_region_init_rects(&clip_region,boxes,3));
}
static void fill_cleanup(void)
{
    destroy_tile();ps2_fill_screen_cleanup(&fill_owner);ps2_fill_screen_cleanup(&fill_owner);
    ps2_solid_screen_cleanup(&fill_solid);paint_cleanup();
}
static void fill_reference(DrawablePtr drawable,xRectangle *rectangles,int n,DDXPointPtr spans,int *widths)
{
    struct pixels *d=model(paint_handle()),*s=model(active_tile);seed(d);
    size_t bytes=(size_t)d->width*d->height*4,sbytes=(size_t)s->width*s->height*4;
    uint32_t *expected=malloc(bytes),*saved_source=malloc(sbytes);assert(expected && saved_source);
    memcpy(expected,d->data,bytes);memcpy(saved_source,s->data,sbytes);
    int ox=drawable->type==DRAWABLE_WINDOW?xp.screen_x:0,oy=drawable->type==DRAWABLE_WINDOW?xp.screen_y:0;
    int tx=gc.miTranslate?drawable->x:0,ty=gc.miTranslate?drawable->y:0;
    uint32_t bits=drawable->depth==15?0x7fff:0xffffff,unused=drawable->depth==15?0x8000:0x80000000;
    unsigned changed=0;
    for(unsigned y=0;y<d->height;++y) for(unsigned x=0;x<d->width;++x) {
        int px=x+ox,py=y+oy;
        if(!pixman_region_contains_point(&clip_region,px,py,NULL)) continue;
        for(int i=0;i<n;++i) {
            int covered=rectangles ? px>=rectangles[i].x+tx && px<rectangles[i].x+tx+rectangles[i].width &&
                py>=rectangles[i].y+ty && py<rectangles[i].y+ty+rectangles[i].height :
                py==spans[i].y && px>=spans[i].x && px<spans[i].x+widths[i];
            if(!covered || !(gc.planemask&bits) || gc.alu==GXnoop) continue;
            uint32_t *v=expected+y*d->width+x;
            if(gc.fillStyle==FillSolid || gc.tileIsPixel) {
                uint32_t a=gc.fillStyle==FillSolid?gc.fgPixel:gc.tile.pixel,mask=gc.planemask&bits;
                *v=(*v&~mask)|(fill_boolean(gc.alu,a,*v)&mask);
            } else {
                int u=modulo((int64_t)px-drawable->x-gc.patOrg.x,s->width);
                int v0=modulo((int64_t)py-drawable->y-gc.patOrg.y,s->height);
                *v=(saved_source[v0*s->width+u]&bits)|unused;
            }
            ++changed;
        }
    }
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);unsigned target=r->target,depth=r->depth;
    unsigned sync=sync_calls,damage=damage_calls,pending=processed;
    assert(n>=0 && n<=4096);
    xRectangle *saved_rects=malloc((n+1)*sizeof(*saved_rects));
    DDXPointRec *saved_spans=malloc((n+1)*sizeof(*saved_spans));
    int *saved_widths=malloc((n+1)*sizeof(*saved_widths));
    assert(saved_rects && saved_spans && saved_widths);
    if(rectangles) memcpy(saved_rects,rectangles,n*sizeof(*rectangles));
    else { memcpy(saved_spans,spans,n*sizeof(*spans));memcpy(saved_widths,widths,n*sizeof(*widths)); }
    if(rectangles) gc.ops->PolyFillRect(drawable,&gc,n,rectangles);
    else gc.ops->FillSpans(drawable,&gc,n,spans,widths,0);
    assert(!memcmp(expected,d->data,bytes) && !memcmp(saved_source,s->data,sbytes));
    if(rectangles) assert(!memcmp(saved_rects,rectangles,n*sizeof(*rectangles)));
    else assert(!memcmp(saved_spans,spans,n*sizeof(*spans)) && !memcmp(saved_widths,widths,n*sizeof(*widths)));
    assert(sync_calls==sync+!!changed && damage_calls==damage+!!changed && processed==pending+!!changed);
    if(changed) assert(notified==drawable && pixman_region_equal(&damage_region,&clip_region));
    assert(r->target==target && r->depth==depth && color_resource->reference.count==3 && depth_resource->reference.count==3);
    free(expected);free(saved_source);free(saved_rects);free(saved_spans);free(saved_widths);
}
static void fill_rect_case(void)
{
    fill_setup(0,32,7,5);xRectangle rects[]={{-5,-4,70,45},{34,8,40,50},{1,1,0,0}};
    fill_reference(&window.drawable,rects,3,NULL,NULL);assert(tile_submits && local_copies && !draw_delegates);
    gc.patOrg=(DDXPointRec){17,-27};fill_reference(&window.drawable,rects,3,NULL,NULL);fill_cleanup();
}
static void fill_span_case(void)
{
    fill_setup(0,32,5,3);DDXPointRec spans[]={{15,34},{30,44},{87,65},{25,71}};int widths[]={99,0,25,43};
    for(int mode=0;mode<2;++mode) { gc.miTranslate=mode;fill_reference(&window.drawable,NULL,4,spans,widths); }
    gc.miTranslate=FALSE;xRectangle r={20,30,80,45};fill_reference(&window.drawable,&r,1,NULL,NULL);fill_cleanup();
}
static void fill_redirect_case(void)
{
    fill_setup(1,32,7,5);xRectangle rs[]={{-3,-2,70,70},{20,15,80,31}};
    fill_reference(&window.drawable,rs,2,NULL,NULL);destroy_tile();
    exa.driver.DestroyPixmap(&xs,xp.devPrivates);int pitch;
    xp.devPrivates=exa.driver.CreatePixmap2(&xs,128,96,15,0,16,&pitch);assert(xp.devPrivates);
    xp.drawable.depth=window.drawable.depth=15;xp.drawable.bitsPerPixel=window.drawable.bitsPerPixel=16;
    make_tile(3,7,16);gc.tile.pixmap=&tile_pixmap;fill_reference(&window.drawable,rs,2,NULL,NULL);
    xp.drawable.x=7;xp.drawable.y=9;gc.patOrg=(DDXPointRec){-17,21};
    fill_reference(&xp.drawable,rs,2,NULL,NULL);fill_cleanup();
}
static void fill_pixel_case(void)
{
    fill_setup(1,32,3,3);gc.tileIsPixel=TRUE;gc.tile.pixel=0x654321;gc.fgPixel=0xabcdef;
    xRectangle rs[]={{0,0,42,29},{7,3,27,25}};
    for(unsigned alu=0;alu<16;++alu) { gc.alu=alu;gc.planemask=0xf13f77;fill_reference(&window.drawable,rs,2,NULL,NULL); }
    assert(logic_calls && !tile_submits);fill_cleanup();
}
static void fill_solid_case(void)
{
    fill_setup(0,32,3,3);gc.fillStyle=FillSolid;gc.fgPixel=0xff7733;gc.alu=GXxor;gc.planemask=0xfffeff;
    xRectangle r={0,0,60,50};fill_reference(&window.drawable,&r,1,NULL,NULL);
    DDXPointRec p={25,34};int w=38;fill_reference(&window.drawable,NULL,1,&p,&w);
    assert(logic_calls && !tile_submits && gc.ops->PolyPoint==gc_screen.solid_ops->PolyPoint);
    fill_cleanup();
}
static void fill_noop_case(void)
{
    fill_setup(1,32,3,5);struct ps2_render_client *r=ps2_pipe_context_render(ctx);
    assert(!ps2_render_fill(r,0,0,1,1,0));unsigned before=calls;xRectangle box={0,0,50,50};
    gc.alu=GXnoop;gc.ops->PolyFillRect(&window.drawable,&gc,1,&box);
    gc.alu=GXxor;gc.planemask=0;gc.ops->PolyFillRect(&window.drawable,&gc,1,&box);
    gc.alu=GXcopy;gc.planemask=~0U;box.width=0;gc.ops->PolyFillRect(&window.drawable,&gc,1,&box);
    gc.ops->FillSpans(&window.drawable,&gc,0,NULL,NULL,0);
    RegionEmpty(&clip_region);box.width=50;gc.ops->PolyFillRect(&window.drawable,&gc,1,&box);
    assert(calls==before && r->count==1 && !damage_calls);r->count=0;fill_cleanup();
}
static void fill_reject(int n,xRectangle *rectangles,DDXPointPtr points,int *widths,unsigned extra)
{
    unsigned before=calls;expect_fatal=1;
    if(!setjmp(fatal)) {
        if(rectangles) gc.ops->PolyFillRect(&window.drawable,&gc,n,rectangles);
        else gc.ops->FillSpans(&window.drawable,&gc,n,points,widths,0);
        assert(0);
    }
    assert(calls==before+extra && !expect_fatal);
}
static void fill_preflight_case(void)
{
    fill_setup(1,32,7,5);struct ps2_render_client *r=ps2_pipe_context_render(ctx);
    assert(!ps2_render_fill(r,0,0,1,1,0));xRectangle box={0,0,70,50};
    gc.alu=GXxor;fill_reject(1,&box,NULL,NULL,0);gc.alu=GXcopy;
    gc.planemask=0xfffffe;fill_reject(1,&box,NULL,NULL,0);gc.planemask=~0U;
    gc.fillStyle=FillStippled;fill_reject(1,&box,NULL,NULL,0);gc.fillStyle=FillTiled;
    fill_reject(4097,&box,NULL,NULL,0);DDXPointRec pts[]={{25,34},{25,35}};int widths[]={3,-1};fill_reject(2,NULL,pts,widths,0);
    fill_reject(1,NULL,pts,NULL,0);
    gc.tile.pixmap=NULL;fill_reject(1,&box,NULL,NULL,0);gc.tile.pixmap=&xp;fill_reject(1,&box,NULL,NULL,0);gc.tile.pixmap=&tile_pixmap;
    void *private=tile_pixmap.devPrivates;tile_pixmap.devPrivates=NULL;fill_reject(1,&box,NULL,NULL,0);tile_pixmap.devPrivates=private;
    uint32_t h=active_tile,bad=9999;memcpy(private,&bad,4);fill_reject(1,&box,NULL,NULL,0);memcpy(private,&h,4);
    tile_pixmap.drawable.width=8;fill_reject(1,&box,NULL,NULL,0);tile_pixmap.drawable.width=7;
    tile_pixmap.drawable.depth=15;fill_reject(1,&box,NULL,NULL,0);tile_pixmap.drawable.depth=24;
    ScreenRec foreign={0};tile_pixmap.drawable.pScreen=&foreign;fill_reject(1,&box,NULL,NULL,0);tile_pixmap.drawable.pScreen=&xs;
    uint32_t f=r->gs->caps.features;r->gs->caps.features&=~PS2GS_HW_TEXTURE;fill_reject(1,&box,NULL,NULL,0);r->gs->caps.features=f;
    assert(r->count==1 && !render_calls && !damage_calls);r->count=0;fill_cleanup();
}
static void fill_capacity_case(void)
{
    fill_setup(0,32,1,1);RegionReset(&clip_region,&(BoxRec){0,0,720,576});gc.miTranslate=FALSE;
    xRectangle boxes[257];for(unsigned i=0;i<257;++i) boxes[i]=(xRectangle){i%128,i/128,1,1};
    fill_reference(&window.drawable,boxes,257,NULL,NULL);
    xRectangle full[]={{0,0,720,576},{0,0,720,576},{0,0,720,576}};
    fill_reference(&window.drawable,full,3,NULL,NULL);assert(output_submits);
    unsigned damage=damage_calls,render=render_calls;
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,1,1,0));
    allocation_failure=1;fill_reject(1,boxes,NULL,NULL,0);assert(r->count==1 && render_calls==render);
    fail_create=1;fill_reject(1,boxes,NULL,NULL,2);fail_create=0;
    assert(r->count==1 && render_calls==render && damage_calls==damage && !r->gs->error);
    gc.ops->PolyFillRect(&window.drawable,&gc,1,boxes);
    assert(!r->count && damage_calls==damage+1);fill_cleanup();
}
static void fill_complex_case(void)
{
    for(unsigned format=0;format<2;++format){
        fill_setup(1,format?16:32,7,5);
        /* Disjoint grid forces a canonical region with 1024 clip boxes. */
        pixman_box16_t clips[1024];
        for(unsigned i=0;i<1024;++i)clips[i]=(pixman_box16_t){20+2*(i%32),30+2*(i/32),21+2*(i%32),31+2*(i/32)};
        pixman_region_fini(&clip_region);assert(pixman_region_init_rects(&clip_region,clips,1024));
        assert(RegionNumRects(&clip_region)==1024);
        xRectangle full={0,0,128,96};fill_reference(&window.drawable,&full,1,NULL,NULL);
        /* 513 overlapping inputs must preserve multiplicity and ALU order
         * across batch boundaries; full and partial masks use independent pixels. */
        RegionReset(&clip_region,&(BoxRec){20,30,148,126});
        xRectangle rectangles[513];
        for(unsigned i=0;i<513;++i)rectangles[i]=(xRectangle){i%7,i%5,10+i%3,8+i%4};
        gc.tileIsPixel=TRUE;gc.tile.pixel=0x654321;
        for(unsigned alu=0;alu<16;++alu)for(unsigned partial=0;partial<2;++partial){
            gc.alu=alu;gc.planemask=partial?0x3a57c1:~0U;
            fill_reference(&window.drawable,rectangles,513,NULL,NULL);
        }
        /* Late invalid span must be caught before the first of >256 boxes. */
        DDXPointRec points[514];int widths[514];
        for(unsigned i=0;i<514;++i){points[i]=(DDXPointRec){21,31+i%64};widths[i]=5;}
        widths[513]=-1;unsigned before=calls;fill_reject(514,NULL,points,widths,0);assert(calls==before);
        fill_cleanup();
    }
}
static void fill_failure_case(int which)
{
    fill_setup(0,32,1,1);RegionReset(&clip_region,&(BoxRec){0,0,720,576});gc.miTranslate=FALSE;
    struct pixels *d=model(0);seed(d);size_t bytes=(size_t)d->width*d->height*4;
    uint32_t *before=malloc(bytes);assert(before);memcpy(before,d->data,bytes);xRectangle box={0,0,720,576};
    if(which==1) fail_copy_at=1;else fail_texture_at=which==0?1:3;
    fill_reject(1,&box,NULL,NULL,which==0?2:which==1?3:14);
    if(which<2) assert(!memcmp(before,d->data,bytes));else assert(memcmp(before,d->data,bytes));
    assert(!damage_calls);unsigned calls_before=calls;fill_reject(1,&box,NULL,NULL,0);assert(calls==calls_before);
    free(before);fill_cleanup();
}
static void fill_rewrap_case(void)
{
    fill_setup(0,32,7,5);void (*native)(DrawablePtr,GCPtr,int,xRectangle*)=gc.ops->PolyFillRect;
    xs.SourceValidate=source_validate;gc.fExpose=TRUE;
    assert(ps2_copy_screen_prepare(&xs,&copy_screen,&gc_screen));
    /* Place the later copy wrapper below fill for valid reverse close order. */
    ps2_copy_screen_cleanup(&copy_screen);ps2_fill_screen_cleanup(&fill_owner);
    assert(ps2_copy_screen_prepare(&xs,&copy_screen,&gc_screen));assert(ps2_fill_screen_prepare(&xs,&fill_owner,&gc_screen));
    gc.funcs->ChangeGC(&gc,GCForeground);gc.funcs->ValidateGC(&gc,GCAllBits,&window.drawable);
    assert(gc.ops->PolyFillRect==native);xRectangle box={0,0,70,50};fill_reference(&window.drawable,&box,1,NULL,NULL);
    assert(ps2_gc_screen_set_solid_ops(&gc_screen,gc_screen.solid_ops) && gc.ops->PolyFillRect==native);
    assert(gc.ops->CopyArea(&window.drawable,&window.drawable,&gc,0,0,20,10,5,5)==&returned_region);
    assert(gc.ops->PolyFillRect==native);gc.fillStyle=FillSolid;
    pixman_box16_t text_clips[]={{20,30,26,42},{28,30,32,42}};
    pixman_region_fini(&clip_region);assert(pixman_region_init_rects(&clip_region,text_clips,2));
    text_check();gc.fillStyle=FillTiled;
    ps2_fill_screen_cleanup(&fill_owner);assert(gc.ops->PolyFillRect==gc_screen.solid_ops->PolyFillRect);
    ps2_copy_screen_cleanup(&copy_screen);fill_cleanup();
}
static void fill_lifetime_case(void)
{
    fill_setup(1,32,7,5);ctx->destroy(ctx);release_external();screen->destroy(screen);assert(!closes);
    xRectangle box={0,0,70,50};fill_reject(1,&box,NULL,NULL,0);
    assert(!xs.CloseScreen(&xs) && closes==1 && !fill_owner.installed && !fill_solid.installed && !paint_owner.installed);
    lower_state(&gc);gc.funcs->DestroyGC(&gc);pixman_region_fini(&clip_region);
    pixman_region_fini(&damage_region);pixman_region_fini(&window.clipList);
    ps2_fill_screen_cleanup(&fill_owner);
}
static void fill_install_case(void)
{
    paint_setup(0,32);assert(!ps2_fill_screen_prepare(&xs,&fill_owner,&gc_screen));
    assert(ps2_solid_screen_prepare(&xs,&fill_solid,&gc_screen));
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);uint32_t f=r->gs->caps.features;
    CloseScreenProcPtr close=xs.CloseScreen;r->gs->caps.features&=~PS2GS_HW_TEXTURE;
    assert(!ps2_fill_screen_prepare(&xs,&fill_owner,&gc_screen) && xs.CloseScreen==close && !gc_screen.fill_ops);r->gs->caps.features=f;
    GCOps incomplete={0};assert(!ps2_gc_screen_set_fill_ops(&gc_screen,&incomplete));
    fail_registration=1;assert(!ps2_fill_screen_prepare(&xs,&fill_owner,&gc_screen));fail_registration=0;
    assert(ps2_fill_screen_prepare(&xs,&fill_owner,&gc_screen));assert(!ps2_fill_screen_prepare(&xs,&fill_owner,&gc_screen));
    const GCOps *ops=gc_screen.fill_ops;assert(!ps2_gc_screen_set_fill_ops(&gc_screen,&incomplete) && gc_screen.fill_ops==ops);
    ps2_fill_screen_cleanup(&fill_owner);ps2_solid_screen_cleanup(&fill_solid);paint_cleanup();
}
static void fill_random_case(void)
{
    fill_setup(1,32,1,1);destroy_tile();
    for(unsigned i=0;i<64;++i) {
        make_tile(1+paint_random_value()%69,1+paint_random_value()%41,32);gc.tile.pixmap=&tile_pixmap;
        gc.patOrg=(DDXPointRec){(int)(paint_random_value()%200)-100,(int)(paint_random_value()%200)-100};
        gc.miTranslate=i&1;xRectangle rs[3];DDXPointRec points[3];int widths[3];
        for(int j=0;j<3;++j) {
            rs[j]=(xRectangle){(int)(paint_random_value()%80)-20,(int)(paint_random_value()%80)-20,paint_random_value()%60,paint_random_value()%60};
            points[j]=(DDXPointRec){(int)(paint_random_value()%160)-10,30+paint_random_value()%70};widths[j]=paint_random_value()%80;
        }
        fill_reference(&window.drawable,rs,3,NULL,NULL);fill_reference(&window.drawable,NULL,3,points,widths);destroy_tile();
    }
    ps2_fill_screen_cleanup(&fill_owner);ps2_solid_screen_cleanup(&fill_solid);paint_cleanup();
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strcmp(argv[1],"rectangles")) fill_rect_case();else if(!strcmp(argv[1],"spans")) fill_span_case();
    else if(!strcmp(argv[1],"redirected")) fill_redirect_case();else if(!strcmp(argv[1],"pixel")) fill_pixel_case();
    else if(!strcmp(argv[1],"solid")) fill_solid_case();else if(!strcmp(argv[1],"noop")) fill_noop_case();
    else if(!strcmp(argv[1],"preflight")) fill_preflight_case();else if(!strcmp(argv[1],"capacity")) fill_capacity_case();
    else if(!strcmp(argv[1],"complex")) fill_complex_case();
    else if(!strcmp(argv[1],"seedfailure")) fill_failure_case(0);else if(!strcmp(argv[1],"expandfailure")) fill_failure_case(1);
    else if(!strcmp(argv[1],"failure")) fill_failure_case(2);else if(!strcmp(argv[1],"rewrap")) fill_rewrap_case();
    else if(!strcmp(argv[1],"lifetime")) fill_lifetime_case();else if(!strcmp(argv[1],"installation")) fill_install_case();
    else if(!strcmp(argv[1],"random")) fill_random_case();else assert(0);
    puts("native tiled GC fill test PASS");return 0;
}
