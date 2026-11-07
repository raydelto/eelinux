/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "ps2_pipe.h"
#include "util/u_inlines.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <linux/fb.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
static volatile sig_atomic_t stopped;
static void interrupt(int signal) { (void)signal; stopped=1; }
static void pattern(unsigned *pixels,int updated)
{
    unsigned colors[]={0x800000ff,0x8000ff00,0x80ff0000,0x80ffffff};
    if (updated) { colors[0]=0x80ff0000; colors[2]=0x800000ff; }
    for(unsigned y=0;y<32;++y) for(unsigned x=0;x<32;++x)
        pixels[y*32+x]=y<16 ? colors[x/8] : ((x/4+y/4)&1) ? 0x80ffffff : 0x80000000;
}
static int quad(struct pipe_context *ctx,const float p[4][4])
{
    const float uv[4][2]={{0,0},{1,0},{1,1},{0,1}};
    const unsigned indices[2][3]={{0,1,2},{0,2,3}};
    for(unsigned i=0;i<2;++i) {
        float pos[3][4],tex[3][2],color[3][4];
        for(unsigned j=0;j<3;++j) {
            memcpy(pos[j],p[indices[i][j]],sizeof(pos[j]));
            memcpy(tex[j],uv[indices[i][j]],sizeof(tex[j]));
            for(unsigned k=0;k<4;++k) color[j][k]=1;
        }
        if(ps2_pipe_triangle(ctx,pos,color,tex,1)) return -1;
    }
    return 0;
}
int main(int argc,char **argv)
{
    unsigned seconds=4;
    if(argc==2 && !strcmp(argv[1],"--brief")) seconds=1;
    else if(argc!=1) { fprintf(stderr,"Usage: ps2-gs-context-test [--brief]\n"); return 2; }
    struct sigaction action={.sa_handler=interrupt}; sigemptyset(&action.sa_mask);
    if(sigaction(SIGINT,&action,NULL) || sigaction(SIGTERM,&action,NULL) || sigaction(SIGHUP,&action,NULL)) return 1;
    struct pipe_screen *screen=NULL; struct pipe_context *ctx=NULL;
    struct pipe_resource *color=NULL,*depth=NULL,*tex=NULL;
    struct pipe_surface *color_surface=NULL,*depth_surface=NULL;
    struct pipe_sampler_view *view=NULL; void *sampler_state=NULL;
    struct ps2_surface_client *gs=NULL;
    int tty=-1,fb=-1,mode,graphics=0,status=1;
    tty=open("/dev/tty0",O_RDWR|O_CLOEXEC);
    if(tty<0 || ioctl(tty,KDGETMODE,&mode) || mode!=KD_TEXT || ioctl(tty,KDSETMODE,KD_GRAPHICS)) goto out;
    graphics=1; fb=open("/dev/fb0",O_RDWR|O_CLOEXEC); struct fb_var_screeninfo var;
    if(fb<0 || ioctl(fb,FBIOGET_VSCREENINFO,&var)) goto out;
    if(var.xoffset || var.yoffset) {
        var.xoffset=var.yoffset=0; var.vmode&=~FB_VMODE_YWRAP;
        if(ioctl(fb,FBIOPAN_DISPLAY,&var)) goto out;
    }
    close(fb); fb=-1;
    screen=ps2_pipe_screen_create("/dev/ps2-gs");
    if(!screen || !(ctx=screen->context_create(screen,NULL,0))) goto out;
    gs=ps2_pipe_context_render(ctx)->gs;
    if(gs->caps.bits_per_pixel!=32 || gs->caps.width<448 || gs->caps.height<256) { errno=ENOTSUP; goto out; }
    printf("ps2-gs: Mesa screen/context %ux%u features=%x\n",gs->caps.width,gs->caps.height,gs->caps.features); fflush(stdout);
    struct pipe_resource t={.target=PIPE_TEXTURE_2D,.format=PIPE_FORMAT_R8G8B8X8_UNORM,
        .width0=448,.height0=256,.depth0=1,.array_size=1,.bind=PIPE_BIND_RENDER_TARGET};
    color=screen->resource_create(screen,&t); t.format=PIPE_FORMAT_Z16_UNORM; t.bind=PIPE_BIND_DEPTH_STENCIL;
    depth=screen->resource_create(screen,&t);
    t.format=PIPE_FORMAT_R8G8B8X8_UNORM; t.bind=PIPE_BIND_SAMPLER_VIEW; t.width0=t.height0=32;
    tex=screen->resource_create(screen,&t);
    if(!color || !depth || !tex) goto out;
    struct pipe_surface st={.format=color->format};
    color_surface=ctx->create_surface(ctx,color,&st); st.format=depth->format;
    depth_surface=ctx->create_surface(ctx,depth,&st);
    if(!color_surface || !depth_surface) goto out;
    struct pipe_framebuffer_state frame={.width=448,.height=256,.nr_cbufs=1,.cbufs={color_surface},.zsbuf=depth_surface};
    ctx->set_framebuffer_state(ctx,&frame);
    struct pipe_sampler_view vt={.target=PIPE_TEXTURE_2D,.format=tex->format,.swizzle_r=PIPE_SWIZZLE_X,
        .swizzle_g=PIPE_SWIZZLE_Y,.swizzle_b=PIPE_SWIZZLE_Z,.swizzle_a=PIPE_SWIZZLE_1};
    view=ctx->create_sampler_view(ctx,tex,&vt);
    struct pipe_sampler_state sampler={.wrap_s=PIPE_TEX_WRAP_CLAMP_TO_EDGE,.wrap_t=PIPE_TEX_WRAP_CLAMP_TO_EDGE,
        .min_img_filter=PIPE_TEX_FILTER_NEAREST,.mag_img_filter=PIPE_TEX_FILTER_NEAREST,.min_mip_filter=PIPE_TEX_MIPFILTER_NONE};
    sampler_state=ctx->create_sampler_state(ctx,&sampler);
    if(!view || !sampler_state) goto out;
    ctx->bind_sampler_states(ctx,PIPE_SHADER_FRAGMENT,0,1,&sampler_state);
    ctx->set_sampler_views(ctx,PIPE_SHADER_FRAGMENT,0,1,0,false,&view);
    union pipe_color_union navy={.f={6.0f/255,12.0f/255,32.0f/255,1}};
    ctx->clear(ctx,PIPE_CLEAR_COLOR0|PIPE_CLEAR_DEPTH,NULL,&navy,1,0);
    unsigned pixels[32*32]; struct pipe_box box={.width=32,.height=32,.depth=1};
    const float left[4][4]={{16,16,.5f,1},{208,16,.5f,1},{208,240,.5f,1},{16,240,.5f,1}};
    const float right[4][4]={{294,48,.5f,.5f},{386,48,.5f,.5f},{432,240,.5f,1},{248,240,.5f,1}};
    pattern(pixels,0); ctx->texture_subdata(ctx,tex,0,0,&box,pixels,32*4,0);
    if(ps2_pipe_context_error(ctx) || quad(ctx,left)) goto out;
    pattern(pixels,1); ctx->texture_subdata(ctx,tex,0,0,&box,pixels,32*4,0);
    if(ps2_pipe_context_error(ctx) || quad(ctx,right)) goto out;
    ctx->flush(ctx,NULL,0);
    if(ps2_pipe_context_error(ctx) || ps2_surface_fill(gs,0,0,0,gs->caps.width,gs->caps.height,0x80200c06) ||
        ps2_surface_copy(gs,((struct ps2_resource*)color)->handle,0,0,0,
            (gs->caps.width-448)/2,(gs->caps.height-256)/2,448,256)) goto out;
    struct timespec delay={.tv_sec=seconds};
    while(!stopped && nanosleep(&delay,&delay)) if(errno!=EINTR) goto out;
    if(!stopped) status=0;
out:
    if(ctx && ps2_pipe_context_error(ctx)) { errno=ps2_pipe_context_error(ctx); status=1; }
    if(status && !stopped) perror("ps2-gs: Mesa screen/context");
    if(sampler_state) ctx->delete_sampler_state(ctx,sampler_state);
    if(ctx) ctx->destroy(ctx);
    pipe_sampler_view_reference(&view,NULL);
    pipe_surface_reference(&color_surface,NULL); pipe_surface_reference(&depth_surface,NULL);
    pipe_resource_reference(&tex,NULL); pipe_resource_reference(&color,NULL); pipe_resource_reference(&depth,NULL);
    if(screen) screen->destroy(screen); /* Closes GS ownership before restoring the console. */
    if(fb>=0) close(fb);
    if(graphics && ioctl(tty,KDSETMODE,KD_TEXT)) status=1;
    if(tty>=0) close(tty);
    if(!status) puts("ps2-gs: screen/context transfer/fence PASSED; expect original rectangle left, updated perspective grid right, then text");
    return status;
}
