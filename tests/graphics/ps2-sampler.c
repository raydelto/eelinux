/* SPDX-License-Identifier: MIT */
#include "ps2_sampler.h"
#include "pipe/p_context.h"
#include "util/u_inlines.h"
#undef ARRAY_SIZE
#include "ps2_texture_exa.h"
#include "previous-render-backends.c"
static unsigned texture_calls;
static unsigned texture_triangle_count;
/* Informational debug labels only; actual upstream atomic references and
 * destruction callbacks still run. */
void debug_describe_sampler_view(char *buf,const struct pipe_sampler_view *view)
{ assert(view); buf[0]=0; }
static struct ps2gs_texture_triangle sampled[64];
int __wrap_ioctl(int fd,unsigned long op,...)
{
    va_list args; va_start(args,op); void *arg=va_arg(args,void*); va_end(args);
    if (op==PS2GS_TEXTURE_SUBMIT) {
        ++calls;
        if (retry_ioctl) { --retry_ioctl; errno=EAGAIN; return -1; }
        if (fail_ioctl) { errno=fail_ioctl; return -1; }
        const struct ps2gs_texture_submit *s=arg; assert(s->count && s->count<=64);
        memcpy(sampled,(void*)(uintptr_t)s->triangles,s->count*sizeof(*sampled));
        texture_triangle_count=s->count; ++texture_calls; return 0;
    }
    int result=render_ioctl(fd,op,arg);
    if (!result && op==PS2GS_GET_CAPS) ((struct ps2gs_caps*)arg)->features|=PS2GS_HW_TEXTURE;
    return result;
}
static const struct pipe_sampler_state nearest={.wrap_s=PIPE_TEX_WRAP_CLAMP_TO_EDGE,
    .wrap_t=PIPE_TEX_WRAP_CLAMP_TO_EDGE,.min_img_filter=PIPE_TEX_FILTER_NEAREST,
    .mag_img_filter=PIPE_TEX_FILTER_NEAREST,.min_mip_filter=PIPE_TEX_MIPFILTER_NONE};
static void sampler_mesa(void)
{
    init(); struct pipe_screen screen={.resource_destroy=resource_destroy};
    struct pipe_context ctx={.screen=&screen,.sampler_view_destroy=ps2_sampler_view_destroy};
    struct pipe_resource t={.target=PIPE_TEXTURE_2D,.format=PIPE_FORMAT_R8G8B8X8_UNORM,
        .width0=128,.height0=96,.depth0=1,.array_size=1,.bind=PIPE_BIND_RENDER_TARGET};
    struct pipe_resource *c=ps2_resource_create(&screen,&gs,&t); assert(c);
    t.width0=33; t.height0=17; t.bind=PIPE_BIND_SAMPLER_VIEW;
    struct pipe_resource *tex=ps2_resource_create(&screen,&gs,&t); assert(tex);
    struct pipe_sampler_view template={.target=PIPE_TEXTURE_2D,.format=t.format,
        .swizzle_r=PIPE_SWIZZLE_X,.swizzle_g=PIPE_SWIZZLE_Y,.swizzle_b=PIPE_SWIZZLE_Z,.swizzle_a=PIPE_SWIZZLE_1};
    struct pipe_sampler_view *view=ps2_sampler_view_create(&ctx,&render,tex,&template); assert(view);
    assert(tex->reference.count==2);
    template.u.tex.last_level=1; assert(!ps2_sampler_view_create(&ctx,&render,tex,&template));
    struct pipe_sampler_state bad=nearest; bad.mag_img_filter=PIPE_TEX_FILTER_LINEAR;
    assert(!ps2_sampler_state_supported(&bad) && ps2_sampler_state_supported(&nearest));
    struct ps2_render_target target; assert(!ps2_render_target_init(&target,&screen,&render));
    assert(!ps2_render_target_bind(&target,c,NULL));
    struct draw_stage *stage=ps2_textured_draw_stage_create((struct draw_context*)1,&target,view,&nearest,1,0); assert(stage);
    assert(view->reference.count==2); assert(!ps2_textured_draw_stage_outputs(stage,3,0,1,2));
    struct vertex_header *v[3]; struct prim_header p={0};
    for (unsigned i=0;i<3;++i) {
        v[i]=calloc(1,sizeof(*v[i])+3*sizeof(float[4])); assert(v[i]); p.v[i]=v[i];
        v[i]->data[0][0]=i==1?100:10; v[i]->data[0][1]=i==2?90:10;
        v[i]->data[0][2]=0.25f; v[i]->data[0][3]=i==1?0.5f:1;
        for (unsigned j=0;j<4;++j) v[i]->data[1][j]=1;
        v[i]->data[2][0]=i==1?1:0; v[i]->data[2][1]=i==2?1:0;
    }
    stage->tri(stage,&p); assert(texture_calls==1 && sampled[0].v[1].q==0x3f000000);
    assert(sampled[0].v[1].s==0x3e840000 && sampled[0].v[0].position.rgba==0x80808080);
    for (unsigned i=0;i<3;++i) free(v[i]);
    stage->destroy(stage);
    assert(view->reference.count==1); pipe_sampler_view_reference(&view,NULL); assert(tex->reference.count==1);
    ps2_render_target_cleanup(&target); pipe_resource_reference(&tex,NULL); pipe_resource_reference(&c,NULL);
    assert(destroys==2); ps2_surface_close(&gs);
}
static void sampler_transport(void)
{
    init(); uint32_t texture; assert(!ps2_surface_create(&gs,32,32,32,&texture));
    assert(!ps2_render_fill(&render,0,0,1,1,0));
    struct ps2gs_texture_triangle t={0};
    for (unsigned i=0;i<3;++i) t.v[i]=(struct ps2gs_texture_vertex){.position={.x=i*16,.y=i*16},.q=0x3f800000};
    retry_ioctl=1; assert(!ps2_texture_triangles(&render,texture,&t,1));
    assert(render_calls==1 && texture_calls==1 && !render.count && sleeps==1);
    assert(!ps2_render_fill(&render,0,0,1,1,0));
    struct pipe_resource resource={.target=PIPE_TEXTURE_2D}; struct ps2_resource tex={.base=resource,.owner=&gs,.handle=texture};
    unsigned pixels[32]={0}; struct pipe_box box={.width=32,.height=1,.depth=1};
    expected_cpp=4; expected_pixels=(const unsigned char*)pixels; expected_stride=sizeof(pixels);
    expected_x=expected_y=0;
    assert(!ps2_sampler_upload(&render,&tex.base,&box,pixels,sizeof(pixels),sizeof(pixels)) && render_calls==2);
    fail_ioctl=EIO; assert(ps2_texture_triangles(&render,texture,&t,1)==-1 && errno==EIO);
    unsigned before=calls; fail_ioctl=0; assert(ps2_texture_triangles(&render,texture,&t,1)==-1 && calls==before);
    ps2_surface_close(&gs);
    init(); assert(!ps2_surface_create(&gs,32,32,32,&texture)); t.v[0].q=0x7f800000;
    before=calls; assert(ps2_texture_triangles(&render,texture,&t,1)==-1 && errno==EINVAL && calls==before);
    ps2_surface_close(&gs);
}
static void sampler_composite(void)
{
    init(); ScreenRec screen={.GetScreenPixmap=get_front}; void *privates[3]={0};
    screen.devPrivates=(PrivatePtr)privates;
    PixmapRec front={.drawable={.pScreen=&screen,.depth=24,.bitsPerPixel=32,.width=720,.height=576}};
    front_pixmap=&front;
    struct ps2_surface_exa exa; assert(ps2_surface_exa_prepare(&screen,&exa,&gs));
    struct ps2_exa_render_bridge bridge; assert(ps2_exa_render_bridge_prepare(&screen,&bridge,&exa,&render));
    struct ps2_texture_exa texture; assert(ps2_texture_exa_prepare(&screen,&texture,&exa,&render));
    assert(!ps2_texture_exa_prepare(&screen,&texture,&exa,&render));
    int pitch; void *private=exa.driver.CreatePixmap2(&screen,32,32,24,0,32,&pitch); assert(private);
    PixmapRec source={.drawable={.pScreen=&screen,.depth=24,.bitsPerPixel=32,.width=32,.height=32},.devPrivates=private};
    PictureRec src={.pDrawable=&source.drawable,.format=PICT_x8b8g8r8};
    PictureRec dst={.pDrawable=&front.drawable,.format=PICT_x8b8g8r8};
    assert(exa.driver.CheckComposite(PictOpSrc,&src,NULL,&dst));
    assert(!exa.driver.CheckComposite(PictOpOver,&src,NULL,&dst));
    assert(!exa.driver.CheckComposite(PictOpSrc,&src,&src,&dst));
    src.format=PICT_x8r8g8b8; assert(!exa.driver.CheckComposite(PictOpSrc,&src,NULL,&dst)); src.format=PICT_x8b8g8r8;
    uint32_t color,depth; assert(!ps2_surface_create(&gs,64,32,32,&color));
    assert(!ps2_render_depth_create(&render,64,32,32,&depth)); assert(!ps2_render_bind(&render,color,depth));
    assert(!ps2_render_fill(&render,0,0,1,1,0));
    assert(exa.driver.PrepareComposite(PictOpSrc,&src,NULL,&dst,&source,NULL,&front));
    exa.driver.Composite(&front,1,2,0,0,3,4,16,16);
    assert(texture_calls==1 && texture_triangle_count==2 && render_calls==1);
    assert(render.target==color && render.depth==depth && !render.count);
    assert(sampled[0].v[0].position.x==3*16 && sampled[0].v[0].position.y==4*16);
    assert(sampled[0].v[0].s==0x3d000000 && !sampled[0].flags);
    exa.driver.DoneComposite(&front); exa.driver.DestroyPixmap(&screen,private);
    assert(!ps2_render_depth_destroy(&render,depth)); assert(!ps2_render_bind(&render,0,0));
    assert(!ps2_surface_destroy(&gs,color)); ps2_surface_close(&gs);
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if (!strcmp(argv[1],"mesa")) sampler_mesa(); else if (!strcmp(argv[1],"transport")) sampler_transport();
    else if (!strcmp(argv[1],"xorg")) xorg_order_case();
    else if (!strcmp(argv[1],"composite")) sampler_composite(); else assert(0);
    puts("native sampler component test PASS"); return 0;
}
