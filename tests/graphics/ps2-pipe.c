/* SPDX-License-Identifier: MIT */
#include "ps2_pipe.h"
#undef ARRAY_SIZE
#include "ps2_context_bridge.h"
#include "previous-sampler.c"
/* Only the optional debug label is stubbed, not references or destruction. */
void debug_describe_surface(char *buf,const struct pipe_surface *surface)
{ assert(surface); buf[0]=0; }
int __wrap_ioctl(int fd,unsigned long op,...)
{ va_list args; va_start(args,op); void *arg=va_arg(args,void*); va_end(args); return sampler_ioctl(fd,op,arg); }
static struct pipe_screen *screen;
static struct pipe_context *ctx;
static struct pipe_resource *color_resource,*depth_resource,*texture_resource;
static struct pipe_surface *color_surface,*depth_surface;
static struct pipe_sampler_view *texture_view;
static void setup(int own)
{
    if (own) screen=ps2_pipe_screen_create("/dev/ps2-gs");
    else { init(); screen=ps2_pipe_screen_create_shared(&render); }
    assert(screen); ctx=screen->context_create(screen,(void*)42,0); assert(ctx && ctx->priv==(void*)42);
    assert(!screen->context_create(screen,NULL,0));
    assert(!strcmp(screen->get_name(screen),"PS2 Graphics Synthesizer"));
    assert(!screen->get_shader_param(screen,PIPE_SHADER_VERTEX,PIPE_SHADER_CAP_MAX_INSTRUCTIONS));
    struct pipe_resource t={.target=PIPE_TEXTURE_2D,.format=PIPE_FORMAT_R8G8B8X8_UNORM,
        .width0=128,.height0=96,.depth0=1,.array_size=1,.bind=PIPE_BIND_RENDER_TARGET};
    assert(screen->is_format_supported(screen,t.format,t.target,0,0,t.bind));
    assert(!screen->is_format_supported(screen,PIPE_FORMAT_R8G8B8A8_UNORM,t.target,0,0,t.bind));
    color_resource=screen->resource_create(screen,&t); assert(color_resource);
    t.format=PIPE_FORMAT_Z16_UNORM; t.bind=PIPE_BIND_DEPTH_STENCIL;
    depth_resource=screen->resource_create(screen,&t); assert(depth_resource);
    struct pipe_surface st={.format=color_resource->format};
    color_surface=ctx->create_surface(ctx,color_resource,&st); assert(color_surface);
    st.format=depth_resource->format;
    depth_surface=ctx->create_surface(ctx,depth_resource,&st); assert(depth_surface);
    struct pipe_framebuffer_state fb={.width=128,.height=96,.nr_cbufs=1,.cbufs={color_surface},.zsbuf=depth_surface};
    ctx->set_framebuffer_state(ctx,&fb); assert(!ps2_pipe_context_error(ctx));
    assert(color_resource->reference.count==3 && depth_resource->reference.count==3);
}
static void texture_setup(void)
{
    struct pipe_resource t={.target=PIPE_TEXTURE_2D,.format=PIPE_FORMAT_R8G8B8X8_UNORM,
        .width0=33,.height0=17,.depth0=1,.array_size=1,.bind=PIPE_BIND_SAMPLER_VIEW};
    texture_resource=screen->resource_create(screen,&t); assert(texture_resource);
    struct pipe_sampler_view v={.target=t.target,.format=t.format,.swizzle_r=PIPE_SWIZZLE_X,
        .swizzle_g=PIPE_SWIZZLE_Y,.swizzle_b=PIPE_SWIZZLE_Z,.swizzle_a=PIPE_SWIZZLE_1};
    texture_view=ctx->create_sampler_view(ctx,texture_resource,&v); assert(texture_view);
    void *state=ctx->create_sampler_state(ctx,&nearest); assert(state);
    ctx->bind_sampler_states(ctx,PIPE_SHADER_FRAGMENT,0,1,&state);
    ctx->delete_sampler_state(ctx,state); /* Bound state is an independent copy. */
    ctx->set_sampler_views(ctx,PIPE_SHADER_FRAGMENT,0,1,0,false,&texture_view);
    assert(texture_view->reference.count==2);
}
static const float position[3][4]={{8,8,.25f,1},{100,8,.25f,.5f},{8,88,.25f,1}};
static const float colors[3][4]={{0,1,1,1},{0,1,1,1},{0,1,1,1}};
static const float uv[3][2]={{0,0},{1,0},{0,1}};
static void release_external(void)
{
    pipe_sampler_view_reference(&texture_view,NULL);
    pipe_surface_reference(&color_surface,NULL); pipe_surface_reference(&depth_surface,NULL);
    pipe_resource_reference(&texture_resource,NULL);
    pipe_resource_reference(&color_resource,NULL); pipe_resource_reference(&depth_resource,NULL);
}
static void framebuffer_case(void)
{
    setup(0); union pipe_color_union blue={.f={0,0,1,1}};
    ctx->clear(ctx,PIPE_CLEAR_COLOR0|PIPE_CLEAR_DEPTH,NULL,&blue,1,0);
    assert(!ps2_pipe_triangle(ctx,position,colors,NULL,1)); ctx->flush(ctx,NULL,0);
    assert(!ps2_pipe_context_error(ctx) && render_calls==1 && last_submit.count==3);
    assert(last_commands[0].op==PS2GS_FILL && last_commands[1].op==PS2GS_CLEAR_DEPTH);
    assert(last_commands[2].op==PS2GS_TRIANGLE && last_commands[2].v[0].z==49151);
    ctx->destroy(ctx); release_external(); screen->destroy(screen);
    assert(destroys==2); ps2_surface_close(&gs);
}
static void sampler_case(void)
{
    setup(0); texture_setup();
    union pipe_color_union navy={.f={0,0,0,1}}; ctx->clear(ctx,PIPE_CLEAR_COLOR0,NULL,&navy,1,0);
    unsigned pixels[2*36]={0}; struct pipe_box box={.width=33,.height=2,.depth=1};
    expected_pixels=(void*)pixels; expected_stride=36*4; expected_cpp=4;
    ctx->texture_subdata(ctx,texture_resource,0,0,&box,pixels,36*4,0);
    assert(uploads==1 && render_calls==1 && !ps2_pipe_context_error(ctx));
    assert(!ps2_render_bind(&render,0,0)); /* Shared Xorg changes the hardware target. */
    assert(!ps2_pipe_triangle(ctx,position,colors,uv,1));
    assert(texture_calls==1 && render.target==((struct ps2_resource*)color_resource)->handle);
    assert(sampled[0].v[1].s==0x3e840000 && sampled[0].v[1].q==0x3f000000);
    struct pipe_sampler_view *transferred=NULL;
    pipe_sampler_view_reference(&transferred,texture_view);
    ctx->set_sampler_views(ctx,PIPE_SHADER_FRAGMENT,0,1,0,true,&transferred);
    assert(texture_view->reference.count==2); /* Exact owned-reference replacement. */
    ctx->set_sampler_views(ctx,PIPE_SHADER_FRAGMENT,0,0,1,false,NULL);
    assert(texture_view->reference.count==1);
    ctx->destroy(ctx); release_external(); screen->destroy(screen); ps2_surface_close(&gs);
}
static void lifetime_case(void)
{
    setup(1); texture_setup();
    screen->destroy(screen); /* External resources/context keep the owned file alive. */
    assert(!closes && !screen->context_create(screen,NULL,0));
    ctx->destroy(ctx); assert(!closes);
    pipe_surface_reference(&color_surface,NULL); pipe_surface_reference(&depth_surface,NULL);
    pipe_sampler_view_reference(&texture_view,NULL); /* Last context objects release its deferred memory. */
    assert(!closes);
    pipe_resource_reference(&color_resource,NULL); pipe_resource_reference(&depth_resource,NULL);
    assert(!closes); pipe_resource_reference(&texture_resource,NULL); assert(closes==1 && destroys==3);
}
static void xorg_case(void)
{
    setup(1); struct ps2_render_client *shared=ps2_pipe_context_render(ctx);
    ScreenRec xscreen={.GetScreenPixmap=get_front}; void *privates[3]={0}; xscreen.devPrivates=(void*)privates;
    PixmapRec front={.drawable={.pScreen=&xscreen,.depth=24,.bitsPerPixel=32,.width=720,.height=576}};
    front_pixmap=&front;
    struct ps2_surface_exa exa; assert(ps2_surface_exa_prepare(&xscreen,&exa,shared->gs));
    ExaDriverRec before=exa.driver; struct ps2_exa_context_bridge bridge={0};
    assert(ps2_exa_context_bridge_prepare(&xscreen,&bridge,&exa,ctx));
    assert(!ps2_exa_context_bridge_prepare(&xscreen,&bridge,&exa,ctx));
    union pipe_color_union red={.f={1,0,0,1}}; ctx->clear(ctx,PIPE_CLEAR_COLOR0,NULL,&red,1,0);
    assert(exa.driver.PrepareSolid(&front,GXcopy,0xffffff,0x00ff00));
    assert(render_calls==1); exa.driver.Solid(&front,0,0,1,1); assert(last_fill.rgba==0x8000ff00);
    struct ps2_texture_exa texture; assert(ps2_texture_exa_prepare(&xscreen,&texture,&exa,shared));
    ctx->destroy(ctx); release_external(); screen->destroy(screen); assert(!closes);
    ps2_exa_context_bridge_cleanup(&bridge); assert(closes==1);
    assert(!memcmp(&before,&exa.driver,sizeof(before)) && !bridge.attached && !bridge.context);
    ps2_exa_context_bridge_cleanup(&bridge); assert(closes==1);
}
static void failure_case(void)
{
    setup(1); union pipe_color_union red={.f={1,0,0,1}};
    ctx->clear(ctx,PIPE_CLEAR_COLOR0,NULL,&red,1,0); fail_ioctl=EIO;
    ctx->flush(ctx,NULL,0); assert(ps2_pipe_context_error(ctx)==EIO);
    unsigned before=calls; fail_ioctl=0;
    assert(ps2_pipe_triangle(ctx,position,colors,NULL,1)==-1);
    ctx->clear(ctx,PIPE_CLEAR_COLOR0,NULL,&red,1,0); ctx->flush(ctx,NULL,0);
    ctx->destroy(ctx); release_external(); screen->destroy(screen);
    assert(calls==before && closes==1); /* Failed session storage is released by close, never replay. */
}
static void unsupported_case(void)
{
    setup(0); unsigned before=calls;
    ctx->draw_vbo(ctx,NULL,0,NULL,NULL,0); assert(ps2_pipe_context_error(ctx)==ENOTSUP && calls==before);
    ctx->destroy(ctx); release_external(); screen->destroy(screen); ps2_surface_close(&gs);
    /* A fresh context also rejects a surface belonging to another context. */
    screen=ps2_pipe_screen_create("/dev/ps2-gs"); assert(screen); ctx=screen->context_create(screen,NULL,0); assert(ctx);
    struct pipe_framebuffer_state fb={.width=1,.height=1,.nr_cbufs=1};
    struct pipe_resource fake_resource={.screen=screen,.bind=PIPE_BIND_RENDER_TARGET};
    struct pipe_surface foreign={.texture=&fake_resource,.width=1,.height=1}; fb.cbufs[0]=&foreign;
    before=calls; ctx->set_framebuffer_state(ctx,&fb); assert(ps2_pipe_context_error(ctx)==EINVAL && calls==before);
    ctx->destroy(ctx); screen->destroy(screen);
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strcmp(argv[1],"framebuffer")) framebuffer_case(); else if(!strcmp(argv[1],"sampler")) sampler_case();
    else if(!strcmp(argv[1],"lifetime")) lifetime_case(); else if(!strcmp(argv[1],"xorg")) xorg_case();
    else if(!strcmp(argv[1],"failure")) failure_case(); else if(!strcmp(argv[1],"unsupported")) unsupported_case(); else assert(0);
    puts("native pipe context test PASS"); return 0;
}
