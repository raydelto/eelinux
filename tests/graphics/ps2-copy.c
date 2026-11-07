/* SPDX-License-Identifier: MIT */
#include "ps2_pipe.h"
#undef ARRAY_SIZE
#include "ps2_copy.h"
#include "ps2_copy_batch.h"
#include "ps2_gs_logic.h"
#define FatalError lower_fixture_FatalError
#define __wrap_close previous_close
#include "previous-gc.c"
#undef __wrap_close
#undef FatalError
void FatalError(const char *format,...)
{
    if(!expect_fatal) { va_list args;va_start(args,format);vfprintf(stderr,format,args);va_end(args); }
    assert(expect_fatal && format);expect_fatal=0;longjmp(fatal,1);
}
struct pixels { uint32_t handle,width,height,*data; };
static struct pixels models[PS2GS_MAX_SURFACES+1];
static unsigned copy_edge_rounding;
static unsigned local_copies,textured_copies,source_validations,exposures,children_calls;
static int fail_copy_at,fail_create;
static struct ps2_copy_screen copy_screen;
BoxRec RegionEmptyBox={0};RegDataRec RegionEmptyData={0};
void *xreallocarray(void *p,size_t n,size_t size) { assert(!size || n<=SIZE_MAX/size);return realloc(p,n*size); }
void RegionDestroy(RegionPtr r) { pixman_region_fini(r);free(r); }
RegionPtr RegionCreate(BoxPtr box,int size)
{
    (void)size;RegionPtr r=malloc(sizeof(*r));if(!r)return NULL;
    if(box)pixman_region_init_rect(r,box->x1,box->y1,box->x2-box->x1,box->y2-box->y1);
    else pixman_region_init(r);
    return r;
}
RegionPtr RegionFromRects(int count,xRectangle *rects,int ordering)
{
    (void)ordering;RegionPtr r=RegionCreate(NULL,0);if(!r)return NULL;
    for(int i=0;i<count;++i)assert(pixman_region_union_rect(r,r,rects[i].x,rects[i].y,rects[i].width,rects[i].height));
    return r;
}
RegionPtr NotClippedByChildren(WindowPtr w)
{
    ++children_calls;RegionPtr r=malloc(sizeof(*r));assert(r);
    pixman_region_init(r);assert(pixman_region_copy(r,&w->clipList));return r;
}
RegionPtr miHandleExposures(DrawablePtr src,DrawablePtr dst,GCPtr g,int x,int y,int w,int h,int dx,int dy)
{ assert(src && dst && g && w>=0 && h>=0);(void)x;(void)y;(void)dx;(void)dy;++exposures;return &returned_region; }
static void source_validate(DrawablePtr d,int x,int y,int w,int h,unsigned mode)
{ assert(d && w>=0 && h>=0 && mode<=IncludeInferiors);(void)x;(void)y;++source_validations; }
static struct pixels *model(uint32_t handle)
{ for(unsigned i=0;i<PS2GS_MAX_SURFACES+1;++i) if(models[i].data && models[i].handle==handle) return models+i;assert(0);return NULL; }
static void allocate_model(uint32_t handle,unsigned w,unsigned h)
{
    unsigned i;for(i=0;i<PS2GS_MAX_SURFACES+1 && models[i].data;++i) {}
    assert(i<PS2GS_MAX_SURFACES+1);models[i]=(struct pixels){handle,w,h,malloc((size_t)w*h*4)};assert(models[i].data);
    memset(models[i].data,0,(size_t)w*h*4);
}
int __wrap_close(int fd)
{ for(unsigned i=0;i<PS2GS_MAX_SURFACES+1;++i) { free(models[i].data);models[i].data=NULL; }return previous_close(fd); }
static float value(uint32_t u) { float f;memcpy(&f,&u,4);return f; }
int __wrap_ioctl(int fd,unsigned long op,...)
{
    va_list args;va_start(args,op);void *arg=va_arg(args,void*);va_end(args);
    if(op==PS2GS_TEXTURE_SUBMIT) {
        ++calls;
        if(fail_ioctl || (fail_copy_at && --fail_copy_at==0)) { errno=EIO;return -1; }
        const struct ps2gs_texture_submit *r=arg;assert(r->abi==1 && r->count==2 && !r->depth && r->texture && r->texture!=r->target);
        struct pixels *s=model(r->texture),*d=model(r->target);
        const struct ps2gs_texture_triangle *t=(void*)(uintptr_t)r->triangles;
        assert(!t[0].flags && !t[1].flags && t[0].v[0].q==0x3f800000);
        unsigned tw=1,th=1;while(tw<s->width) tw*=2;while(th<s->height) th*=2;
        int x1=t[0].v[0].position.x/16,y1=t[0].v[0].position.y/16;
        int x2=t[0].v[2].position.x/16,y2=t[0].v[2].position.y/16;
        float u1=value(t[0].v[0].s)*tw,v1=value(t[0].v[0].t)*th;
        float u2=value(t[0].v[2].s)*tw,v2=value(t[0].v[2].t)*th;
        assert(x2>x1 && y2>y1 && x2<=(int)d->width && y2<=(int)d->height);
        /* Model nearest sampling at fragment centers, independently from the
         * native tiled copy plan. The expected output is a source snapshot. */
        for(int y=y1;y<y2;++y) for(int x=x1;x<x2;++x) {
            float phase=copy_edge_rounding?0.f:.5f,error=copy_edge_rounding?1.f/65536.f:0.f;
            int u=u1-error+((float)x-x1+phase)*(u2-u1)/(x2-x1);
            int v=v1-error+((float)y-y1+phase)*(v2-v1)/(y2-y1);
            assert(u>=0 && v>=0 && u<(int)s->width && v<(int)s->height);
            d->data[y*d->width+x]=s->data[v*s->width+u];
        }
        ++textured_copies;return 0;
    }
    if(op==PS2GS_SURFACE_CREATE && fail_create) { ++calls;errno=ENOSPC;return -1; }
    if(op==PS2GS_SURFACE_COPY && (fail_ioctl || (fail_copy_at && --fail_copy_at==0))) { ++calls;errno=EIO;return -1; }
    int result=pipe_ioctl(fd,op,arg);if(result) return result;
    if(op==PS2GS_GET_CAPS) {
        ((struct ps2gs_caps*)arg)->features|=PS2GS_HW_SOLID_LOGIC;
        allocate_model(0,720,576);
    } else if(op==PS2GS_SURFACE_CREATE) {
        const struct ps2gs_surface_create *c=arg;allocate_model(c->handle,c->width,c->height);
    } else if(op==PS2GS_DEPTH_CREATE) {
        const struct ps2gs_depth_create *c=arg;allocate_model(c->handle,c->width,c->height);
    } else if(op==PS2GS_SURFACE_DESTROY) {
        struct pixels *p=model(((struct ps2gs_surface_destroy*)arg)->handle);free(p->data);p->data=NULL;
    } else if(op==PS2GS_SURFACE_COPY) {
        const struct ps2gs_surface_copy *r=arg;struct pixels *s=model(r->source),*d=model(r->destination);
        assert(r->sx+r->width<=s->width && r->dx+r->width<=d->width && r->sy+r->height<=s->height && r->dy+r->height<=d->height);
        for(unsigned row=0;row<r->height;++row) {
            unsigned y=s==d && r->dy>r->sy ? r->height-1-row : row;
            memmove(d->data+(r->dy+y)*d->width+r->dx,s->data+(r->sy+y)*s->width+r->sx,r->width*4);
        }
        ++local_copies;
    }
    return 0;
}
static void seed(struct pixels *p)
{ for(unsigned y=0;y<p->height;++y) for(unsigned x=0;x<p->width;++x) p->data[y*p->width+x]=0x80000000|((x*17+y*13)&0xffffff); }
static uint32_t *copy_reference(uint32_t source,uint32_t destination,const struct ps2_pipe_copy_box *boxes,unsigned n)
{
    struct pixels *s=model(source),*d=model(destination);
    uint32_t *snapshot=malloc((size_t)s->width*s->height*4),*result=malloc((size_t)d->width*d->height*4);assert(snapshot && result);
    memcpy(snapshot,s->data,(size_t)s->width*s->height*4);memcpy(result,d->data,(size_t)d->width*d->height*4);
    for(unsigned i=0;i<n;++i) for(int y=0;y<boxes[i].height;++y) for(int x=0;x<boxes[i].width;++x)
        result[(boxes[i].dy+y)*d->width+boxes[i].dx+x]=snapshot[(boxes[i].sy+y)*s->width+boxes[i].sx+x];
    free(snapshot);return result;
}
static void batch(uint32_t source,uint32_t destination,const struct ps2_pipe_copy_box *boxes,unsigned n)
{
    uint32_t *expected=copy_reference(source,destination,boxes,n);struct pixels *d=model(destination);
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);unsigned target=r->target,depth=r->depth;
    assert(!ps2_pipe_copy_boxes(ctx,source,destination,boxes,n));
    assert(!memcmp(expected,d->data,(size_t)d->width*d->height*4));free(expected);
    assert(r->target==target && r->depth==depth && color_resource->reference.count==3 && depth_resource->reference.count==3);
}
static void copy_setup(void)
{
    gc_setup(1);xs.SourceValidate=source_validate;
    window.drawable.width=128;window.drawable.height=96;window.realized=TRUE;
    pixman_region_init_rect(&window.clipList,20,30,128,96);pixman_region_init_rect(&window.borderClip,20,30,128,96);
    pixman_region_fini(&clip_region);pixman_region_init_rect(&clip_region,20,30,128,96);
    assert(ps2_copy_screen_prepare(&xs,&copy_screen,&gc_screen));
    assert(!ps2_copy_screen_prepare(&xs,&copy_screen,&gc_screen));seed(model(0));
}
static void cleanup(void)
{
    ps2_copy_screen_cleanup(&copy_screen);ps2_copy_screen_cleanup(&copy_screen);
    pixman_region_fini(&window.clipList);pixman_region_fini(&window.borderClip);gc_cleanup();
}
static void widths(void)
{
    copy_setup();struct ps2_render_client *r=ps2_pipe_context_render(ctx);
    assert(!ps2_render_bind(r,((struct ps2_resource*)color_resource)->handle,ps2_pipe_context_target(ctx)->render->depth));
    assert(!ps2_render_fill(r,0,0,2,2,0));
    struct ps2_pipe_copy_box a={12,10,140,20,64,40};batch(0,0,&a,1);assert(render_calls==1 && !textured_copies);
    a=(struct ps2_pipe_copy_box){589,10,140,100,131,65};batch(0,0,&a,1);assert(textured_copies && local_copies);
    uint32_t tiny,dest;assert(!ps2_surface_create(r->gs,3,40,16,&tiny));assert(!ps2_surface_create(r->gs,17,40,16,&dest));seed(model(tiny));
    a=(struct ps2_pipe_copy_box){0,0,7,0,3,40};batch(tiny,dest,&a,1);
    assert(!ps2_surface_destroy(r->gs,tiny) && !ps2_surface_destroy(r->gs,dest));cleanup();
}
static void overlap(void)
{
    copy_setup();
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) if(x || y) {
        seed(model(0));struct ps2_pipe_copy_box b[3]={{100,120,100+x*7,120+y*5,129,65},
            {240,120,240+x*7,120+y*5,67,65},{110,195,110+x*7,195+y*5,197,35}};
        struct ps2_pipe_copy_box reverse[3]={b[2],b[1],b[0]};batch(0,0,reverse,3);
    }
    cleanup();
}
static void preflight(void)
{
    copy_setup();struct ps2_render_client *r=ps2_pipe_context_render(ctx);assert(!ps2_render_fill(r,0,0,1,1,0));unsigned before=calls;
    struct ps2_pipe_copy_box b[2]={{0,0,20,20,13,9},{0,0,719,20,13,9}};
    assert(ps2_pipe_copy_boxes(ctx,0,0,b,2)==-1 && errno==EINVAL && calls==before && r->count==1);
    b[1]=(struct ps2_pipe_copy_box){4,0,24,20,13,9};assert(ps2_pipe_copy_boxes(ctx,0,0,b,2)==-1 && calls==before);
    assert(ps2_pipe_copy_boxes(ctx,9876,0,b,1)==-1 && calls==before);
    assert(ps2_pipe_copy_boxes(ctx,0,0,b,257)==-1 && calls==before);
    gc.planemask=0xff;expect_fatal=1;
    if(!setjmp(fatal)) { gc.ops->CopyArea(&window.drawable,&window.drawable,&gc,0,0,13,9,20,20);assert(0); }
    assert(calls==before && !source_validations);gc.planemask=0xffffff;
    expect_fatal=1;
    if(!setjmp(fatal)) { gc.ops->CopyArea(&window.drawable,&window.drawable,&gc,INT_MAX,0,13,9,20,20);assert(0); }
    assert(calls==before && !source_validations);
    gc.alu=GXnoop;assert(!gc.ops->CopyArea(&window.drawable,&window.drawable,&gc,0,0,13,9,20,20));
    assert(calls==before && !damage_calls);gc.alu=GXcopy;
    fail_create=1;assert(ps2_pipe_copy_boxes(ctx,0,0,b,1)==-1 && errno==ENOSPC && !render_calls && r->count==1);
    fail_create=0;cleanup();
}
static void clipping(void)
{
    copy_setup();pixman_region_fini(&clip_region);
    pixman_box16_t boxes[]={{60,70,80,82},{85,70,109,82},{65,90,111,101}};assert(pixman_region_init_rects(&clip_region,boxes,3));
    struct ps2_pipe_copy_box b[3]={{20,30,60,70,20,12},{45,30,85,70,24,12},{25,50,65,90,46,11}};
    uint32_t *expected=copy_reference(0,0,b,3);
    assert(!gc.ops->CopyArea(&window.drawable,&window.drawable,&gc,0,0,128,96,40,40));
    assert(!memcmp(expected,model(0)->data,(size_t)720*576*4));free(expected);
    assert(source_validations==1 && damage_calls==1 && sync_calls==1 && processed==1);
    int pitch;void *private=exa.driver.CreatePixmap2(&xs,128,96,24,0,32,&pitch);assert(private);
    PixmapRec owned={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=24,.bitsPerPixel=32,.width=128,.height=96},.devPrivates=private};
    uint32_t handle;memcpy(&handle,private,4);
    pixman_region_fini(&clip_region);pixman_region_init_rect(&clip_region,7,3,45,20);
    struct ps2_pipe_copy_box p={20,30,7,3,45,20};expected=copy_reference(0,handle,&p,1);
    assert(!gc.ops->CopyArea(&xp.drawable,&owned.drawable,&gc,20,30,45,20,7,3));
    assert(!memcmp(expected,model(handle)->data,(size_t)128*96*4));free(expected);
    exa.driver.DestroyPixmap(&xs,private);cleanup();
}
static void exposure_case(void)
{
    copy_setup();gc.fExpose=TRUE;gc.subWindowMode=IncludeInferiors;
    assert(gc.ops->CopyArea(&window.drawable,&window.drawable,&gc,-10,-5,50,40,20,20)==&returned_region);
    assert(source_validations==1 && exposures==1); /* same-window/clientClip shortcut */
    WindowRec other=window;other.parent=&window;other.drawable.x=40;
    /* The storage callback accepts only the fixture's window, so use the
     * actual same drawable with client clipping to exercise child clipping. */
    window.parent=&other;gc.clientClip=&clip_region;
    assert(gc.ops->CopyArea(&window.drawable,&window.drawable,&gc,0,0,13,9,20,20)==&returned_region);
    assert(children_calls==1 && exposures==2);
    window.realized=FALSE;unsigned before=calls;assert(!gc.ops->CopyArea(&window.drawable,&window.drawable,&gc,0,0,13,9,20,20) && calls==before);
    cleanup();
}
static void rewrap(void)
{
    copy_setup();const GCOps *ops=gc.ops;gc.funcs->ChangeGC(&gc,GCForeground);gc.funcs->ValidateGC(&gc,GCAllBits,&window.drawable);
    assert(gc.ops==ops && gc_screen.copy_ops);
    assert(!gc.ops->CopyArea(&window.drawable,&window.drawable,&gc,0,0,13,9,20,20));
    GCOps incomplete={0};assert(!ps2_gc_screen_set_copy_ops(&gc_screen,&incomplete));
    ps2_copy_screen_cleanup(&copy_screen);assert(!gc_screen.copy_ops);
    assert(gc.ops->CopyArea(&window.drawable,&window.drawable,&gc,1,2,3,4,5,6)==&returned_region);
    assert(draw_delegates==1);cleanup();
}
static void lifetime(void)
{
    copy_setup();ctx->destroy(ctx);expect_fatal=1;
    if(!setjmp(fatal)) { gc.ops->CopyArea(&window.drawable,&window.drawable,&gc,0,0,13,9,20,20);assert(0); }
    assert(!closes);assert(!xs.CloseScreen(&xs));assert(!copy_screen.installed && !gc_screen.installed && !closes);
    pixman_region_fini(&clip_region);pixman_region_fini(&window.clipList);pixman_region_fini(&window.borderClip);
    release_external();screen->destroy(screen);assert(closes==1);
}
static void failure(void)
{
    copy_setup();struct ps2_pipe_copy_box b={0,0,20,20,13,9};fail_copy_at=2;
    assert(ps2_pipe_copy_boxes(ctx,0,0,&b,1)==-1 && errno==EIO);unsigned before=calls;
    assert(ps2_pipe_copy_boxes(ctx,0,0,&b,1)==-1 && calls==before && !damage_calls);
    expect_fatal=1;if(!setjmp(fatal)) { gc.ops->CopyArea(&window.drawable,&window.drawable,&gc,0,0,13,9,20,20);assert(0); }
    assert(calls==before);cleanup();assert(closes==1);
}
static uint32_t state=0x81423079;
static unsigned random_value(void) { state=state*1664525+1013904223;return state; }
static void random_case(void)
{
    copy_setup();
    for(unsigned i=0;i<256;++i) {
        unsigned w=1+random_value()%197,h=1+random_value()%91;
        unsigned sx=random_value()%(720-w),sy=random_value()%(576-h);
        unsigned dx=random_value()%(720-w),dy=random_value()%(576-h);
        struct ps2_pipe_copy_box b={sx,sy,dx,dy,w,h};batch(0,0,&b,1);
    }
    cleanup();
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strcmp(argv[1],"widths-edge")){copy_edge_rounding=1;widths();}
    else if(!strcmp(argv[1],"widths")) widths();else if(!strcmp(argv[1],"overlap")) overlap();
    else if(!strcmp(argv[1],"preflight")) preflight();else if(!strcmp(argv[1],"clipping")) clipping();
    else if(!strcmp(argv[1],"exposures")) exposure_case();else if(!strcmp(argv[1],"rewrap")) rewrap();
    else if(!strcmp(argv[1],"lifetime")) lifetime();else if(!strcmp(argv[1],"failure")) failure();
    else if(!strcmp(argv[1],"random")) random_case();else assert(0);
    puts("native CopyArea and Mesa copy test PASS");return 0;
}
