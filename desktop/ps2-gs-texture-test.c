/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "ps2_sampler.h"
#include "pipe/p_screen.h"
#include "pipe/p_context.h"
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
static void destroy_resource(struct pipe_screen *s,struct pipe_resource *r)
{ (void)s; if (r->format==PIPE_FORMAT_Z16_UNORM) ps2_depth_resource_destroy(r); else ps2_resource_destroy(r); }
static void pattern(unsigned *pixels,int updated)
{
    unsigned colors[]={0x800000ff,0x8000ff00,0x80ff0000,0x80ffffff};
    if (updated) { colors[0]=0x80ff0000; colors[2]=0x800000ff; }
    for (unsigned y=0;y<32;++y) for (unsigned x=0;x<32;++x)
        pixels[y*32+x]=y<16?colors[x/8]:((x/4+y/4)&1)?0x80ffffff:0x80000000;
}
static int quad(struct ps2_render_target *t,struct pipe_sampler_view *view,
    const struct pipe_sampler_state *sampler,const float p[4][4])
{
    const float uv[4][2]={{0,0},{1,0},{1,1},{0,1}};
    unsigned indices[2][3]={{0,1,2},{0,2,3}};
    for (unsigned i=0;i<2;++i) {
        float pos[3][4],tex[3][2],color[3][4];
        for (unsigned j=0;j<3;++j) {
            memcpy(pos[j],p[indices[i][j]],sizeof(pos[j])); memcpy(tex[j],uv[indices[i][j]],sizeof(tex[j]));
            for (unsigned k=0;k<4;++k) color[j][k]=1;
        }
        if (ps2_sampler_triangle(t,view,sampler,pos,tex,color,1,1)) return -1;
    }
    return 0;
}
int main(int argc,char **argv)
{
    unsigned seconds=4; if (argc==2 && !strcmp(argv[1],"--brief")) seconds=1;
    else if (argc!=1) { fprintf(stderr,"Usage: ps2-gs-texture-test [--brief]\n"); return 2; }
    struct sigaction action={.sa_handler=interrupt}; sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT,&action,NULL) || sigaction(SIGTERM,&action,NULL) || sigaction(SIGHUP,&action,NULL)) return 1;
    struct ps2_surface_client gs={.fd=-1}; struct ps2_render_client render;
    struct ps2_render_target target={0}; struct pipe_resource *color=NULL,*depth=NULL,*tex=NULL;
    struct pipe_sampler_view *view=NULL; struct pipe_screen screen={.resource_destroy=destroy_resource};
    struct pipe_context ctx={.screen=&screen,.sampler_view_destroy=ps2_sampler_view_destroy};
    int tty=-1,fb=-1,graphics=0,mode,status=1;
    tty=open("/dev/tty0",O_RDWR|O_CLOEXEC);
    if (tty<0 || ioctl(tty,KDGETMODE,&mode) || mode!=KD_TEXT || ioctl(tty,KDSETMODE,KD_GRAPHICS)) goto out;
    graphics=1; fb=open("/dev/fb0",O_RDWR|O_CLOEXEC); struct fb_var_screeninfo var;
    if (fb<0 || ioctl(fb,FBIOGET_VSCREENINFO,&var)) goto out;
    if (var.xoffset || var.yoffset) { var.xoffset=var.yoffset=0; var.vmode&=~FB_VMODE_YWRAP;
        if (ioctl(fb,FBIOPAN_DISPLAY,&var)) goto out; }
    close(fb); fb=-1;
    if (ps2_surface_open(&gs,"/dev/ps2-gs") || ps2_render_init(&render,&gs) ||
        ps2_render_target_init(&target,&screen,&render)) goto out;
    if (gs.caps.bits_per_pixel!=32 || gs.caps.width<448 || gs.caps.height<256) { errno=ENOTSUP; goto out; }
    printf("ps2-gs: Mesa texture sampling %ux%u features=%x\n",gs.caps.width,gs.caps.height,gs.caps.features); fflush(stdout);
    struct pipe_resource t={.target=PIPE_TEXTURE_2D,.format=PIPE_FORMAT_R8G8B8X8_UNORM,
        .width0=448,.height0=256,.depth0=1,.array_size=1,.bind=PIPE_BIND_RENDER_TARGET};
    color=ps2_resource_create(&screen,&gs,&t); t.format=PIPE_FORMAT_Z16_UNORM; t.bind=PIPE_BIND_DEPTH_STENCIL;
    depth=ps2_depth_resource_create(&screen,&render,&t,32);
    t.format=PIPE_FORMAT_R8G8B8X8_UNORM; t.bind=PIPE_BIND_SAMPLER_VIEW; t.width0=t.height0=32;
    tex=ps2_resource_create(&screen,&gs,&t);
    struct pipe_sampler_view templ={.target=PIPE_TEXTURE_2D,.format=t.format,
        .swizzle_r=PIPE_SWIZZLE_X,.swizzle_g=PIPE_SWIZZLE_Y,.swizzle_b=PIPE_SWIZZLE_Z,.swizzle_a=PIPE_SWIZZLE_1};
    if (!color || !depth || !tex || !(view=ps2_sampler_view_create(&ctx,&render,tex,&templ)) ||
        ps2_render_target_bind(&target,color,depth)) goto out;
    struct pipe_sampler_state sampler={.wrap_s=PIPE_TEX_WRAP_CLAMP_TO_EDGE,.wrap_t=PIPE_TEX_WRAP_CLAMP_TO_EDGE,
        .min_img_filter=PIPE_TEX_FILTER_NEAREST,.mag_img_filter=PIPE_TEX_FILTER_NEAREST,.min_mip_filter=PIPE_TEX_MIPFILTER_NONE};
    union pipe_color_union navy={.f={6.0f/255,12.0f/255,32.0f/255,1}};
    unsigned pixels[32*32]; struct pipe_box box={.width=32,.height=32,.depth=1};
    const float left[4][4]={{16,16,.5f,1},{208,16,.5f,1},{208,240,.5f,1},{16,240,.5f,1}};
    /* A projected planar rectangle needs width proportional to reciprocal W.
     * The top width is 92, half the bottom width of 184, matching Q=.5 / 1.
     * Otherwise the two triangles give different horizontal texture scales.
     */
    const float right[4][4]={{294,48,.5f,.5f},{386,48,.5f,.5f},{432,240,.5f,1},{248,240,.5f,1}};
    if (ps2_render_target_clear(&target,NULL,&navy,1)) goto out;
    pattern(pixels,0);
    if (ps2_sampler_upload(&render,tex,&box,pixels,sizeof(pixels),32*4) || quad(&target,view,&sampler,left)) goto out;
    pattern(pixels,1);
    if (ps2_sampler_upload(&render,tex,&box,pixels,sizeof(pixels),32*4) || quad(&target,view,&sampler,right) || ps2_render_flush(&render)) goto out;
    if (ps2_surface_fill(&gs,0,0,0,gs.caps.width,gs.caps.height,0x80200c06) ||
        ps2_surface_copy(&gs,((struct ps2_resource*)color)->handle,0,0,0,
            (gs.caps.width-448)/2,(gs.caps.height-256)/2,448,256)) goto out;
    struct timespec delay={.tv_sec=seconds};
    while (!stopped && nanosleep(&delay,&delay)) if (errno!=EINTR) goto out;
    if (!stopped) status=0;
out:
    if (status && !stopped) perror("ps2-gs: Mesa texture sampling");
    pipe_sampler_view_reference(&view,NULL);
    if (target.render) ps2_render_target_cleanup(&target);
    pipe_resource_reference(&tex,NULL); pipe_resource_reference(&color,NULL); pipe_resource_reference(&depth,NULL);
    if (gs.error) status=1;
    ps2_surface_close(&gs);
    if (fb>=0) close(fb);
    if (graphics && ioctl(tty,KDSETMODE,KD_TEXT)) status=1;
    if (tty>=0) close(tty);
    if (!status) puts("ps2-gs: texture sampling/fence PASSED; expect original square left, updated perspective trapezoid right, then text");
    return status;
}
