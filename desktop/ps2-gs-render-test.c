/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "ps2_render_target.h"
#include "pipe/p_screen.h"
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
static void interrupt(int signal) { (void)signal; stopped = 1; }
static void destroy_resource(struct pipe_screen *screen, struct pipe_resource *resource)
{
    (void)screen;
    if (resource->format == PIPE_FORMAT_Z16_UNORM) ps2_depth_resource_destroy(resource);
    else ps2_resource_destroy(resource);
}
static int triangle(struct ps2_render_target *target, float size, float z, const float color[4])
{
    float position[3][4] = { {160,128-size,z,1}, {160-size,128+size,z,1}, {160+size,128+size,z,1} };
    float colors[3][4];
    for (unsigned i = 0; i < 3; ++i) memcpy(colors[i], color, sizeof(colors[i]));
    return ps2_render_target_triangle(target, position, colors, 1);
}
int main(int argc, char **argv)
{
    unsigned seconds = 4;
    if (argc == 2 && !strcmp(argv[1], "--brief")) seconds = 1;
    else if (argc != 1) { fprintf(stderr, "Usage: ps2-gs-render-test [--brief]\n"); return 2; }
    struct sigaction action = {.sa_handler=interrupt}; sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT,&action,NULL) || sigaction(SIGTERM,&action,NULL) || sigaction(SIGHUP,&action,NULL)) return 1;
    struct ps2_surface_client gs = {.fd=-1}; struct ps2_render_client render;
    struct ps2_render_target target = {0};
    struct pipe_resource *color = NULL, *depth = NULL;
    struct pipe_screen screen = {.resource_destroy=destroy_resource};
    int tty=-1, fb=-1, graphics=0, mode, status=1;
    tty=open("/dev/tty0",O_RDWR|O_CLOEXEC);
    if (tty<0 || ioctl(tty,KDGETMODE,&mode) || mode!=KD_TEXT) goto out;
    if (ioctl(tty,KDSETMODE,KD_GRAPHICS)) goto out;
    graphics=1;
    fb=open("/dev/fb0",O_RDWR|O_CLOEXEC); struct fb_var_screeninfo var;
    if (fb<0 || ioctl(fb,FBIOGET_VSCREENINFO,&var)) goto out;
    if (var.xoffset || var.yoffset) {
        var.xoffset=var.yoffset=0; var.vmode &= ~FB_VMODE_YWRAP;
        if (ioctl(fb,FBIOPAN_DISPLAY,&var)) goto out;
    }
    close(fb); fb=-1;
    if (ps2_surface_open(&gs,"/dev/ps2-gs") || ps2_render_init(&render,&gs) ||
        ps2_render_target_init(&target,&screen,&render)) goto out;
    if (gs.caps.width<320 || gs.caps.height<256) { errno=ENOTSUP; goto out; }
    printf("ps2-gs: Mesa owned rendering %ux%u %ubpp features=%x\n",
        gs.caps.width,gs.caps.height,gs.caps.bits_per_pixel,gs.caps.features); fflush(stdout);
    struct pipe_resource t = {.target=PIPE_TEXTURE_2D,
        .format=gs.caps.bits_per_pixel==32 ? PIPE_FORMAT_R8G8B8X8_UNORM : PIPE_FORMAT_R5G5B5X1_UNORM,
        .width0=320,.height0=256,.depth0=1,.array_size=1,.bind=PIPE_BIND_RENDER_TARGET};
    color=ps2_resource_create(&screen,&gs,&t);
    t.format=PIPE_FORMAT_Z16_UNORM; t.bind=PIPE_BIND_DEPTH_STENCIL;
    depth=ps2_depth_resource_create(&screen,&render,&t,gs.caps.bits_per_pixel);
    if (!color || !depth || ps2_render_target_bind(&target,color,depth)) goto out;
    union pipe_color_union navy = {.f={6.0f/255,12.0f/255,32.0f/255,1}};
    const float cyan[4]={0,0.8f,0.9f,1}, red[4]={0.9f,0.15f,0,1}, yellow[4]={1,1,0,1};
    /* Near cyan first; later, farther red/yellow must not overwrite it.
     * The yellow triangle is entirely hidden by the near cyan triangle. */
    if (ps2_render_target_clear(&target,NULL,&navy,1) || triangle(&target,82,0.25f,cyan) ||
        triangle(&target,112,0.75f,red) || triangle(&target,60,0.9f,yellow) || ps2_render_flush(&render)) goto out;
    if (ps2_surface_fill(&gs,0,0,0,gs.caps.width,gs.caps.height,0x80200c06) ||
        ps2_surface_copy(&gs,((struct ps2_resource*)color)->handle,0,0,0,
            (gs.caps.width-320)/2,(gs.caps.height-256)/2,320,256)) goto out;
    struct timespec delay={.tv_sec=seconds};
    while (!stopped && nanosleep(&delay,&delay)) if (errno!=EINTR) goto out;
    if (!stopped) status=0;
out:
    if (status && !stopped) perror("ps2-gs: Mesa owned rendering");
    /* The framebuffer adapter holds references and fences before releasing
     * them. On failed drawing, close frees storage without replay. */
    if (target.render) ps2_render_target_cleanup(&target);
    pipe_resource_reference(&color,NULL); pipe_resource_reference(&depth,NULL);
    if (gs.error) status=1;
    ps2_surface_close(&gs);
    if (fb>=0) close(fb);
    if (graphics && ioctl(tty,KDSETMODE,KD_TEXT)) status=1;
    if (tty>=0) close(tty);
    if (!status) puts("ps2-gs: owned Mesa transfer/fence PASSED; expect cyan over red, no yellow, then text");
    return status;
}
