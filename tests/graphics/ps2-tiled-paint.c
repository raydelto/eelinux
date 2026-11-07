/* SPDX-License-Identifier: MIT */
#include "previous-paint.c"
#include "ps2_tile.h"
static PixmapRec tile_pixmap;
static unsigned tile_submits,output_submits,texture_rects,max_packet,max_scratch_bytes;
static int fail_texture_at;
static uint32_t active_tile;
int __wrap_ioctl(int fd,unsigned long op,...)
{
    va_list args;va_start(args,op);void *arg=va_arg(args,void*);va_end(args);
    if(op==PS2GS_SURFACE_CREATE) {
        const struct ps2gs_surface_create *r=arg;
        if(active_tile) {
            assert(r->width<=64 && r->height<=32);
            unsigned bytes=r->width*r->height*r->bits_per_pixel/8;
            if(bytes>max_scratch_bytes) max_scratch_bytes=bytes;
        }
    }
    if(op!=PS2GS_TEXTURE_SUBMIT) return paint_ioctl(fd,op,arg);
    assert(fd==999);++calls;
    if(fail_ioctl || (fail_texture_at && --fail_texture_at==0)) { errno=EIO;return -1; }
    const struct ps2gs_texture_submit *r=arg;
    assert(r->abi==1 && !r->depth && r->texture && r->texture!=r->target && r->count && !(r->count%2) && r->count<=64);
    if(r->count>max_packet) max_packet=r->count;
    ++tile_submits;output_submits+=r->target==paint_handle();
    struct pixels *s=model(r->texture),*d=model(r->target);
    const struct ps2gs_texture_triangle *triangles=(void*)(uintptr_t)r->triangles;
    unsigned tw=1,th=1;while(tw<s->width) tw*=2;while(th<s->height) th*=2;
    uint32_t mask=r->bits_per_pixel==16?0x7fff:0xffffff;
    uint32_t unused=r->bits_per_pixel==16?0x8000:0x80000000;
    for(unsigned i=0;i<r->count;i+=2) {
        const struct ps2gs_texture_triangle *t=triangles+i;
        assert(!t[0].flags && !t[1].flags && !t[0].reserved[0] && t[0].v[0].q==0x3f800000);
        int x1=t[0].v[0].position.x/16,y1=t[0].v[0].position.y/16;
        int x2=t[0].v[2].position.x/16,y2=t[0].v[2].position.y/16;
        float u1=value(t[0].v[0].s)*tw,v1=value(t[0].v[0].t)*th;
        float u2=value(t[0].v[2].s)*tw,v2=value(t[0].v[2].t)*th;
        assert(x2>x1 && y2>y1 && x2<=(int)d->width && y2<=(int)d->height);
        /* Model GS nearest sampling; expected output separately uses modulo. */
        for(int y=y1;y<y2;++y) for(int x=x1;x<x2;++x) {
            int u=u1+((float)x-x1+.5f)*(u2-u1)/(x2-x1);
            int v=v1+((float)y-y1+.5f)*(v2-v1)/(y2-y1);
            assert(u>=0 && v>=0 && u<(int)s->width && v<(int)s->height);
            d->data[y*d->width+x]=(s->data[v*s->width+u]&mask)|unused;
        }
        ++texture_rects;
    }
    return 0;
}
static void make_tile(unsigned w,unsigned h,int bpp)
{
    int pitch;tile_pixmap=(PixmapRec){.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.width=w,.height=h,
        .depth=bpp==16?15:24,.bitsPerPixel=bpp}};
    tile_pixmap.devPrivates=exa.driver.CreatePixmap2(&xs,w,h,bpp==16?15:24,0,bpp,&pitch);
    assert(tile_pixmap.devPrivates);memcpy(&active_tile,tile_pixmap.devPrivates,4);
    struct pixels *p=model(active_tile);
    uint32_t mask=bpp==16?0x7fff:0xffffff,unused=bpp==16?0x8000:0x80000000;
    for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w;++x)
        p->data[y*w+x]=(((x+1)*103+(y+3)*7151)&mask)|unused;
    window.backgroundState=BackgroundPixmap;window.background.pixmap=&tile_pixmap;
    window.borderIsPixel=FALSE;window.border.pixmap=&tile_pixmap;
}
static void destroy_tile(void)
{ active_tile=0;exa.driver.DestroyPixmap(&xs,tile_pixmap.devPrivates);tile_pixmap.devPrivates=NULL; }
static int modulo(int64_t value,unsigned size)
{ while(value<0) value+=size;return value%size; }
static void tile_reference(RegionPtr region,int what,WindowPtr ancestor,int direct)
{
    struct pixels *s=model(active_tile),*d=model(paint_handle());seed(d);
    size_t bytes=(size_t)d->width*d->height*4,sbytes=(size_t)s->width*s->height*4;
    uint32_t *expected=malloc(bytes),*saved_source=malloc(sbytes);assert(expected && saved_source);
    memcpy(expected,d->data,bytes);memcpy(saved_source,s->data,sbytes);
    int ox=xp.screen_x,oy=xp.screen_y;
    int ax=ancestor->drawable.x,ay=ancestor->drawable.y;
    uint32_t mask=window.drawable.depth==15?0x7fff:0xffffff,unused=window.drawable.depth==15?0x8000:0x80000000;
    RegionRec saved,expected_damage;pixman_region_init(&saved);pixman_region_init(&expected_damage);assert(pixman_region_copy(&saved,region));
    unsigned changed=0;
    for(unsigned y=0;y<d->height;++y) for(unsigned x=0;x<d->width;++x) {
        int sx=x+ox,sy=y+oy;
        if(!pixman_region_contains_point(region,sx,sy,NULL) ||
            (!direct && what==PW_BACKGROUND && !pixman_region_contains_point(&window.clipList,sx,sy,NULL))) continue;
        int u=modulo((int64_t)sx-ax,s->width),v=modulo((int64_t)sy-ay,s->height);
        expected[y*d->width+x]=(saved_source[v*s->width+u]&mask)|unused;++changed;
        assert(pixman_region_union_rect(&expected_damage,&expected_damage,x+(what==PW_BACKGROUND?ox:0),y+(what==PW_BACKGROUND?oy:0),1,1));
    }
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);uint32_t target=r->target,depth=r->depth;
    unsigned sync=sync_calls,damage=damage_calls;
    if(direct) {
        struct ps2_pipe_solid_box jobs[256];int n=RegionNumRects(region);assert(n<=256);BoxPtr boxes=RegionRects(region);
        for(int i=0;i<n;++i) jobs[i]=(struct ps2_pipe_solid_box){boxes[i].x1-ox,boxes[i].y1-oy,boxes[i].x2-ox,boxes[i].y2-oy};
        assert(!ps2_pipe_tile_rectangles(ctx,active_tile,paint_handle(),jobs,n,ax-ox,ay-oy));
        assert(sync_calls==sync && damage_calls==damage);
    } else {
        xs.PaintWindow(&window,region,what);
        assert(sync_calls==sync+!!changed && damage_calls==damage+!!changed);
        if(changed) assert(pixman_region_equal(&damage_region,&expected_damage));
    }
    assert(!memcmp(expected,d->data,bytes) && !memcmp(saved_source,s->data,sbytes) && pixman_region_equal(region,&saved));
    assert(r->target==target && r->depth==depth && color_resource->reference.count==3 && depth_resource->reference.count==3);
    free(expected);free(saved_source);pixman_region_fini(&saved);pixman_region_fini(&expected_damage);
}
static void tiled_background(void)
{
    paint_setup(0,32);make_tile(7,5,32);window.drawable.x=23;window.drawable.y=37;
    pixman_region_fini(&window.clipList);pixman_box16_t boxes[]={{20,30,50,50},{60,30,90,50},{30,60,110,80}};
    assert(pixman_region_init_rects(&window.clipList,boxes,3));RegionRec r;pixman_region_init_rect(&r,0,0,140,110);
    struct ps2_render_client *render=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(render,0,0,1,1,0));
    tile_reference(&r,PW_BACKGROUND,&window,0);assert(render_calls==1 && max_scratch_bytes<=8192 && local_copies && !draw_delegates);
    pixman_region_fini(&r);destroy_tile();paint_cleanup();
}
static void tiled_border(void)
{
    paint_setup(0,32);make_tile(3,7,32);
    WindowRec parent={.drawable={.pScreen=&xs},.backgroundState=ParentRelative};
    WindowRec root={.drawable={.pScreen=&xs,.x=-11,.y=-13},.backgroundState=BackgroundPixmap};
    root.background.pixmap=&tile_pixmap;parent.parent=&root;window.parent=&parent;window.backgroundState=ParentRelative;
    RegionRec r;pixman_region_init_rect(&r,10,15,140,110);
    tile_reference(&r,PW_BACKGROUND,&root,0);
    /* The ancestor background is unrelated to the child's border image. */
    root.backgroundState=BackgroundPixel;root.background.pixel=0xabcdef;
    tile_reference(&r,PW_BORDER,&root,0);
    pixman_region_fini(&r);destroy_tile();paint_cleanup();
}
static void tiled_redirect(void)
{
    paint_setup(1,32);make_tile(5,3,32);window.drawable.x=-7;window.drawable.y=-9;
    RegionRec r;pixman_region_init_rect(&r,12,18,150,130);
    tile_reference(&r,PW_BACKGROUND,&window,0);tile_reference(&r,PW_BORDER,&window,0);destroy_tile();
    exa.driver.DestroyPixmap(&xs,xp.devPrivates);int pitch;xp.devPrivates=exa.driver.CreatePixmap2(&xs,128,96,15,0,16,&pitch);assert(xp.devPrivates);
    xp.drawable.depth=window.drawable.depth=15;xp.drawable.bitsPerPixel=window.drawable.bitsPerPixel=16;
    make_tile(7,5,16);tile_reference(&r,PW_BACKGROUND,&window,0);tile_reference(&r,PW_BORDER,&window,0);
    pixman_region_fini(&r);destroy_tile();paint_cleanup();
}
static void tiled_tiny(void)
{
    paint_setup(0,32);RegionRec r;pixman_region_init_rect(&r,0,0,720,576);
    for(unsigned size=1;size<=2;++size) {
        make_tile(size,size,32);unsigned before=output_submits;
        tile_reference(&r,PW_BORDER,&window,0);assert(output_submits-before<=10 && max_scratch_bytes<=8192 && max_packet==64);destroy_tile();
    }
    pixman_region_fini(&r);paint_cleanup();
}
static void tiled_large(void)
{
    paint_setup(0,32);RegionRec r;pixman_region_init_rect(&r,0,0,720,576);
    make_tile(127,67,32);tile_reference(&r,PW_BORDER,&window,0);assert(!max_scratch_bytes && max_packet==64);destroy_tile();
    make_tile(1024,1024,32);tile_reference(&r,PW_BORDER,&window,0);destroy_tile();
    make_tile(63,37,32);tile_reference(&r,PW_BORDER,&window,0);destroy_tile();
    pixman_region_fini(&r);paint_cleanup();
}
static void tiled_preflight(void)
{
    paint_setup(1,32);make_tile(7,5,32);RegionRec r;pixman_region_init_rect(&r,20,30,80,50);
    struct ps2_render_client *render=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(render,0,0,1,1,0));
    ScreenRec foreign={0};tile_pixmap.drawable.pScreen=&foreign;rejected(&r,PW_BACKGROUND);tile_pixmap.drawable.pScreen=&xs;
    void *private=tile_pixmap.devPrivates;tile_pixmap.devPrivates=NULL;rejected(&r,PW_BORDER);tile_pixmap.devPrivates=private;
    uint32_t h=active_tile,bad=9999;memcpy(private,&bad,4);rejected(&r,PW_BACKGROUND);memcpy(private,&h,4);
    tile_pixmap.drawable.width=8;rejected(&r,PW_BACKGROUND);tile_pixmap.drawable.width=7;
    tile_pixmap.drawable.depth=15;rejected(&r,PW_BACKGROUND);tile_pixmap.drawable.depth=24;
    window.background.pixmap=&xp;rejected(&r,PW_BACKGROUND);window.background.pixmap=&tile_pixmap;
    struct ps2_pipe_solid_box jobs[]={{1,1,20,20},{120,1,140,20}};unsigned before=calls;
    assert(ps2_pipe_tile_rectangles(ctx,active_tile,paint_handle(),jobs,2,0,0)==-1 && errno==EINVAL && calls==before);
    assert(ps2_pipe_tile_rectangles(ctx,0,paint_handle(),jobs,1,0,0)==-1 && calls==before);
    uint32_t features=render->gs->caps.features;render->gs->caps.features&=~PS2GS_HW_TEXTURE;
    assert(ps2_pipe_tile_rectangles(ctx,active_tile,paint_handle(),jobs,1,0,0)==-1 && errno==ENOTSUP && calls==before);
    render->gs->caps.features=features;assert(render->count==1 && !render_calls && !tile_submits);
    pixman_region_fini(&r);destroy_tile();paint_cleanup();
}
static void tiled_capacity(void)
{
    paint_setup(0,32);make_tile(1,1,32);struct ps2_render_client *r=ps2_pipe_context_render(ctx);
    assert(!ps2_render_fill(r,0,0,1,1,0));struct ps2_pipe_solid_box b={0,0,720,576};unsigned before=calls;
    assert(ps2_pipe_tile_rectangles(ctx,active_tile,0,&b,257,0,0)==-1 && calls==before);
    r->gs->caps.features&=~PS2GS_HW_COPY;
    assert(ps2_pipe_tile_rectangles(ctx,active_tile,0,&b,1,0,0)==-1 && errno==E2BIG && calls==before);
    r->gs->caps.features|=PS2GS_HW_COPY;
    struct ps2_pipe_solid_box repeated[3]={b,b,b};
    assert(ps2_pipe_tile_rectangles(ctx,active_tile,0,repeated,3,0,0)==-1 && errno==E2BIG && calls==before);
    /* Healthy ENOSPC is followed by the existing display-handle rejection
     * probe; neither request draws or consumes the pending render queue. */
    fail_create=1;assert(ps2_pipe_tile_rectangles(ctx,active_tile,0,&b,1,0,0)==-1 && errno==ENOSPC && calls==before+2 && r->count==1 && !render_calls && !r->gs->error);
    fail_create=0;
    assert(!ps2_pipe_tile_rectangles(ctx,active_tile,0,&b,1,0,0) && !r->count && render_calls==1 && !r->gs->error);
    destroy_tile();paint_cleanup();
}
static void tiled_noop(void)
{
    paint_setup(1,32);make_tile(7,5,32);struct ps2_render_client *r=ps2_pipe_context_render(ctx);
    assert(!ps2_render_fill(r,0,0,1,1,0));unsigned before=calls;
    assert(!ps2_pipe_tile_rectangles(ctx,active_tile,paint_handle(),NULL,0,INT32_MIN,INT32_MAX));
    RegionRec region;pixman_region_init_rect(&region,400,500,10,10);xs.PaintWindow(&window,&region,PW_BACKGROUND);
    window.backgroundState=None;xs.PaintWindow(&window,NULL,PW_BACKGROUND);
    window.backgroundState=BackgroundPixmap;window.background.pixmap=&tile_pixmap;window.inhibitBGPaint=TRUE;xs.PaintWindow(&window,NULL,PW_BACKGROUND);
    assert(calls==before && r->count==1 && !sync_calls && !damage_calls);
    pixman_region_fini(&region);destroy_tile();paint_cleanup();
}
static void tiled_failure(void)
{
    paint_setup(0,32);make_tile(3,5,32);RegionRec r;pixman_region_init_rect(&r,0,0,720,576);
    /* Fail after the seed and first output packet: effects cannot be replayed. */
    fail_texture_at=3;invoke_failure(&r,PW_BORDER);assert(output_submits==1 && !damage_calls && !sync_calls && ps2_pipe_context_error(ctx)==EIO);
    unsigned before=calls;invoke_failure(&r,PW_BORDER);assert(calls==before);
    pixman_region_fini(&r);destroy_tile();paint_cleanup();assert(closes==1);
}
static void tiled_early_failure(int expansion)
{
    paint_setup(0,32);make_tile(3,5,32);RegionRec r;pixman_region_init_rect(&r,0,0,720,576);
    struct pixels *p=model(0);seed(p);size_t bytes=(size_t)p->width*p->height*4;
    uint32_t *snapshot=malloc(bytes);assert(snapshot);memcpy(snapshot,p->data,bytes);
    if(expansion) fail_copy_at=2;else fail_texture_at=1;
    invoke_failure(&r,PW_BORDER);
    assert(!memcmp(snapshot,p->data,bytes) && !output_submits && !damage_calls && !sync_calls && ps2_pipe_context_error(ctx)==EIO);
    unsigned before=calls;invoke_failure(&r,PW_BORDER);assert(calls==before);
    free(snapshot);pixman_region_fini(&r);destroy_tile();paint_cleanup();assert(closes==1);
}
static void tiled_lifetime(void)
{
    paint_setup(0,32);make_tile(5,7,32);destroy_tile();
    ctx->destroy(ctx);release_external();screen->destroy(screen);assert(!closes);
    RegionRec r;pixman_region_init_rect(&r,20,30,80,50);unsigned before=calls;invoke_failure(&r,PW_BACKGROUND);assert(calls==before);
    assert(!xs.CloseScreen(&xs));assert(closes==1 && !paint_owner.installed && !move_owner.installed && !gc_screen.installed);
    pixman_region_fini(&r);pixman_region_fini(&window.clipList);pixman_region_fini(&damage_region);pixman_region_fini(&clip_region);
}
static void tiled_random(void)
{
    paint_setup(1,32);
    for(unsigned i=0;i<32;++i) {
        unsigned w=1+paint_random_value()%69,h=1+paint_random_value()%41;make_tile(w,h,32);
        window.drawable.x=(int)(paint_random_value()%120)-60;window.drawable.y=(int)(paint_random_value()%120)-60;
        pixman_box16_t boxes[4];
        for(int j=0;j<4;++j) { int x=20+paint_random_value()%90,y=30+paint_random_value()%60;
            boxes[j]=(pixman_box16_t){x,y,x+1+(int)(paint_random_value()%30),y+1+(int)(paint_random_value()%30)}; }
        RegionRec r;assert(pixman_region_init_rects(&r,boxes,4));tile_reference(&r,PW_BORDER,&window,i%2);pixman_region_fini(&r);destroy_tile();
    }
    /* Signed origin subtraction uses 64-bit arithmetic, including extremes. */
    make_tile(7,5,32);struct ps2_pipe_solid_box b={0,0,17,13};
    assert(!ps2_pipe_tile_rectangles(ctx,active_tile,paint_handle(),&b,1,INT32_MIN,INT32_MAX));destroy_tile();paint_cleanup();
}
static void tiled_complex(void)
{
    const unsigned counts[]={257,512,1024};
    for(unsigned depth=0;depth<2;++depth)for(unsigned mode=0;mode<2;++mode)
    for(unsigned size=0;size<sizeof(counts)/sizeof(*counts);++size){
        paint_setup(1,depth?16:32);make_tile(7,5,depth?16:32);
        window.drawable.x=-7;window.drawable.y=11;
        pixman_box16_t boxes[1024];unsigned count=counts[size];
        for(unsigned i=0;i<count;++i){int x=20+(i%32)*2,y=30+(i/32)*2;
            boxes[i]=(pixman_box16_t){x,y,x+1,y+1};}
        RegionRec region;assert(pixman_region_init_rects(&region,boxes,count));
        assert((unsigned)RegionNumRects(&region)==count);
        tile_reference(&region,mode?PW_BORDER:PW_BACKGROUND,&window,0);
        assert(!draw_delegates && max_packet<=64 && max_scratch_bytes<=8192);
        pixman_region_fini(&region);destroy_tile();paint_cleanup();
    }
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strcmp(argv[1],"background")) tiled_background();else if(!strcmp(argv[1],"border")) tiled_border();
    else if(!strcmp(argv[1],"redirected")) tiled_redirect();else if(!strcmp(argv[1],"tiny")) tiled_tiny();
    else if(!strcmp(argv[1],"large")) tiled_large();else if(!strcmp(argv[1],"preflight")) tiled_preflight();
    else if(!strcmp(argv[1],"capacity")) tiled_capacity();else if(!strcmp(argv[1],"noop")) tiled_noop();
    else if(!strcmp(argv[1],"failure")) tiled_failure();else if(!strcmp(argv[1],"lifetime")) tiled_lifetime();
    else if(!strcmp(argv[1],"seedfailure")) tiled_early_failure(0);else if(!strcmp(argv[1],"expandfailure")) tiled_early_failure(1);
    else if(!strcmp(argv[1],"random")) tiled_random();
    else if(!strcmp(argv[1],"complex")) tiled_complex();else assert(0);
    puts("native tiled PaintWindow and Mesa repeat test PASS");return 0;
}
