/* SPDX-License-Identifier: MIT */
#include "previous-copy.c"

static struct pipe_resource *new_color(enum pipe_format format,unsigned width,unsigned height)
{
    struct pipe_resource t={.target=PIPE_TEXTURE_2D,.format=format,.width0=width,.height0=height,
        .depth0=1,.array_size=1,.bind=PIPE_BIND_RENDER_TARGET|PIPE_BIND_SAMPLER_VIEW};
    struct pipe_resource *r=screen->resource_create(screen,&t);assert(r);return r;
}
static void resource_cleanup(void)
{ ctx->destroy(ctx);release_external();screen->destroy(screen); }
static void callback_copy(struct pipe_resource *s,struct pipe_resource *d,const struct ps2_pipe_copy_box *b)
{
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);
    int sr=s->reference.count,dr=d->reference.count;
    int cr=color_resource->reference.count,zr=depth_resource->reference.count;
    int vr=texture_view ? texture_view->reference.count : 0;
    uint32_t target=r->target,depth=r->depth,source=((struct ps2_resource*)s)->handle,destination=((struct ps2_resource*)d)->handle;
    uint32_t *expected=copy_reference(source,destination,b,1);
    struct pipe_box box={b->sx,b->sy,0,b->width,b->height,1};
    ctx->resource_copy_region(ctx,d,0,b->dx,b->dy,0,s,0,&box);
    assert(!ps2_pipe_context_error(ctx));
    assert(!memcmp(expected,model(destination)->data,(size_t)d->width0*d->height0*4));free(expected);
    assert(r->target==target && r->depth==depth && sr==s->reference.count && dr==d->reference.count);
    assert(cr==color_resource->reference.count && zr==depth_resource->reference.count);
    assert(!texture_view || texture_view->reference.count==vr);
}
static void resource_formats(void)
{
    setup(1);
    for(unsigned f=0;f<2;++f) {
        enum pipe_format format=f ? PIPE_FORMAT_R5G5B5X1_UNORM : PIPE_FORMAT_R8G8B8X8_UNORM;
        struct pipe_resource *s=new_color(format,137,79),*d=new_color(format,149,83);
        seed(model(((struct ps2_resource*)s)->handle));seed(model(((struct ps2_resource*)d)->handle));
        for(unsigned w=1;w<=137;++w) {
            struct ps2_pipe_copy_box b={137-w,3,149-w,7,w,65};callback_copy(s,d,&b);
        }
        pipe_resource_reference(&s,NULL);pipe_resource_reference(&d,NULL);
    }
    struct pipe_resource *s=new_color(PIPE_FORMAT_R5G5B5X1_UNORM,3,40),*d=new_color(s->format,17,40);
    seed(model(((struct ps2_resource*)s)->handle));
    struct ps2_pipe_copy_box b={0,0,7,0,3,40};callback_copy(s,d,&b);
    pipe_resource_reference(&s,NULL);pipe_resource_reference(&d,NULL);resource_cleanup();
}
static void resource_overlap(void)
{
    setup(1);
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
        seed(model(((struct ps2_resource*)color_resource)->handle));
        struct ps2_pipe_copy_box b={20,20,20+x*7,20+y*5,73,51};callback_copy(color_resource,color_resource,&b);
    }
    resource_cleanup();
}
static void resource_preflight(void)
{
    for(unsigned test=0;test<18;++test) {
        setup(1);struct ps2_render_client *r=ps2_pipe_context_render(ctx);
        assert(!ps2_render_fill(r,0,0,1,1,0));unsigned before=calls;
        struct pipe_box box={0,0,0,13,9,1};const struct pipe_box *b=&box;
        unsigned dx=20,dy=20,dz=0,level=0,source_level=0;
        switch(test) {
        case 0:b=NULL;break;case 1:box.x=-1;break;case 2:box.y=-1;break;
        case 3:box.width=0;break;case 4:box.height=-1;break;
        case 5:box.x=INT_MAX;break;case 6:box.width=INT_MAX;break;
        case 7:box.y=95;break;case 8:box.x=127;break;case 9:dx=127;break;
        case 10:dy=95;break;case 11:dx=UINT_MAX;break;case 12:dy=UINT_MAX;break;
        case 13:box.z=1;break;case 14:box.depth=2;break;case 15:dz=1;break;
        case 16:level=1;break;case 17:source_level=1;break;
        }
        ctx->resource_copy_region(ctx,color_resource,level,dx,dy,dz,color_resource,source_level,b);
        assert(ps2_pipe_context_error(ctx)==(test<13 ? EINVAL : ENOTSUP));
        assert(calls==before && r->count==1);
        box=(struct pipe_box){0,0,0,13,9,1};
        ctx->resource_copy_region(ctx,color_resource,0,20,20,0,color_resource,0,&box);
        assert(calls==before && r->count==1);resource_cleanup();
    }
}
static void resource_ownership(void)
{
    for(unsigned test=0;test<7;++test) {
        setup(1);struct ps2_render_client *r=ps2_pipe_context_render(ctx);
        assert(!ps2_render_fill(r,0,0,1,1,0));unsigned before=calls;
        struct pipe_resource *s=color_resource,*d=color_resource;
        struct pipe_screen foreign={0};struct ps2_surface_client owner={0};
        struct ps2_resource *native=(void*)color_resource;
        struct pipe_screen *saved_screen=s->screen;struct ps2_surface_client *saved_owner=native->owner;
        unsigned target=s->target;enum pipe_format format=s->format;
        switch(test) {
        case 0:s=NULL;break;case 1:d=NULL;break;case 2:s->screen=&foreign;break;
        case 3:native->owner=&owner;break;case 4:s=d=depth_resource;break;
        case 5:s->format=PIPE_FORMAT_R8G8B8A8_UNORM;break;case 6:s->target=PIPE_BUFFER;break;
        }
        struct pipe_box box={0,0,0,13,9,1};ctx->resource_copy_region(ctx,d,0,20,20,0,s,0,&box);
        assert(ps2_pipe_context_error(ctx)==(test<2 ? EINVAL : test==3 ? EXDEV : ENOTSUP));
        assert(calls==before && r->count==1);
        color_resource->screen=saved_screen;native->owner=saved_owner;
        color_resource->target=target;color_resource->format=format;resource_cleanup();
    }
}
static void resource_capacity(void)
{
    setup(1);struct ps2_render_client *r=ps2_pipe_context_render(ctx);
    assert(!ps2_render_fill(r,0,0,1,1,0));unsigned submitted=render_calls,copied=local_copies+textured_copies;
    struct pipe_box box={0,0,0,13,9,1};fail_create=1;
    ctx->resource_copy_region(ctx,color_resource,0,20,20,0,color_resource,0,&box);
    assert(ps2_pipe_context_error(ctx)==ENOSPC && r->count==1 && render_calls==submitted);
    assert(local_copies+textured_copies==copied);unsigned before=calls;fail_create=0;
    ctx->resource_copy_region(ctx,color_resource,0,20,20,0,color_resource,0,&box);
    assert(calls==before);resource_cleanup();assert(calls==before && closes==1);
}
static void resource_failure(void)
{
    setup(1);struct pipe_box box={0,0,0,13,9,1};fail_copy_at=2;
    ctx->resource_copy_region(ctx,color_resource,0,20,20,0,color_resource,0,&box);
    assert(ps2_pipe_context_error(ctx)==EIO);unsigned before=calls;
    ctx->resource_copy_region(ctx,color_resource,0,20,20,0,color_resource,0,&box);
    assert(calls==before);resource_cleanup();assert(calls==before && closes==1);
}
static void resource_shared(void)
{
    copy_setup();texture_setup();struct ps2_render_client *r=ps2_pipe_context_render(ctx);
    uint32_t target=r->target,depth=r->depth;
    seed(model(((struct ps2_resource*)color_resource)->handle));
    assert(!ps2_render_fill(r,0,0,1,1,0));unsigned rendered=render_calls;
    struct ps2_pipe_copy_box b={0,0,30,20,13,9};callback_copy(color_resource,color_resource,&b);
    assert(render_calls==rendered+1 && !r->count);
    /* Mesa's harmless identity copy and the following Xorg copy use the same
     * file and preserve the live color/depth/sampler state and references. */
    assert(!ps2_render_fill(r,0,0,1,1,0));unsigned before=calls;
    b=(struct ps2_pipe_copy_box){3,5,3,5,13,9};callback_copy(color_resource,color_resource,&b);
    assert(calls==before && r->count==1);
    assert(!gc.ops->CopyArea(&window.drawable,&window.drawable,&gc,0,0,13,9,20,20));
    assert(!r->count && r->target==target && r->depth==depth && texture_view->reference.count==2);
    assert(color_resource->reference.count==3 && depth_resource->reference.count==3 && damage_calls==1);
    cleanup();assert(closes==1);
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strcmp(argv[1],"formats")) resource_formats();else if(!strcmp(argv[1],"overlap")) resource_overlap();
    else if(!strcmp(argv[1],"preflight")) resource_preflight();else if(!strcmp(argv[1],"ownership")) resource_ownership();
    else if(!strcmp(argv[1],"capacity")) resource_capacity();else if(!strcmp(argv[1],"failure")) resource_failure();
    else if(!strcmp(argv[1],"shared")) resource_shared();else assert(0);
    puts("native Gallium resource copy callback test PASS");return 0;
}
