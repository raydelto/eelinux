/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "ps2_gs_surface.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <linux/fb.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stopped;
static void interrupt(int signal) { (void)signal; stopped=1; }

int main(int argc,char **argv)
{
    unsigned seconds=4;
    if (argc==2 && !strcmp(argv[1],"--brief")) seconds=1;
    else if (argc!=1) { fprintf(stderr,"Usage: ps2-gs-surface-test [--brief]\n"); return 2; }
    struct sigaction action={.sa_handler=interrupt}; sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT,&action,NULL) || sigaction(SIGTERM,&action,NULL) || sigaction(SIGHUP,&action,NULL)) {
        perror("ps2-gs: signal setup"); return 1;
    }
    struct ps2_surface_client gs={.fd=-1};
    uint32_t a=0,b=0;
    int tty=-1,fb=-1,graphics=0,status=1,mode;
    tty=open("/dev/tty0",O_RDWR|O_CLOEXEC);
    if (tty<0 || ioctl(tty,KDGETMODE,&mode) || mode!=KD_TEXT) {
        fprintf(stderr,"ps2-gs: active text console required\n"); goto out;
    }
    if (ioctl(tty,KDSETMODE,KD_GRAPHICS)) { perror("ps2-gs: graphics mode"); goto out; }
    graphics=1;
    fb=open("/dev/fb0",O_RDWR|O_CLOEXEC);
    struct fb_var_screeninfo var;
    if (fb<0 || ioctl(fb,FBIOGET_VSCREENINFO,&var)) { perror("ps2-gs: read front-buffer pan"); goto out; }
    if (var.xoffset || var.yoffset) {
        var.xoffset=0; var.yoffset=0; var.vmode &= ~FB_VMODE_YWRAP;
        if (ioctl(fb,FBIOPAN_DISPLAY,&var)) { perror("ps2-gs: reset front-buffer pan"); goto out; }
    }
    close(fb); fb=-1;
    if (ps2_surface_open(&gs,"/dev/ps2-gs")) { perror("ps2-gs: open surface session"); goto out; }
    if (gs.caps.width<416 || gs.caps.height<200) {
        fprintf(stderr,"ps2-gs: display too small for surface probe\n"); goto out;
    }
    printf("ps2-gs: owned surfaces %ux%u %ubpp features=%x\n",
        gs.caps.width,gs.caps.height,gs.caps.bits_per_pixel,gs.caps.features); fflush(stdout);
    unsigned cpp=gs.caps.bits_per_pixel/8;
    enum { WIDTH=132,HEIGHT=65,PITCH=544 };
    unsigned char pixels[HEIGHT*PITCH]; memset(pixels,0xa5,sizeof(pixels));
    static const uint32_t colors32[]={0x800000ff,0x8000ff00,0x80ff0000,0x80ffffff};
    static const uint16_t colors16[]={0x801f,0x83e0,0xfc00,0xffff};
    for (unsigned y=0;y<HEIGHT;++y) for(unsigned x=0;x<WIDTH;++x) {
        unsigned color=y<32?x/33:((x/8+y/8)&1?3:4);
        if (cpp==4) { uint32_t c=color<4?colors32[color]:0x80000000; memcpy(pixels+y*PITCH+x*cpp,&c,cpp); }
        else { uint16_t c=color<4?colors16[color]:0x8000; memcpy(pixels+y*PITCH+x*cpp,&c,cpp); }
    }
    unsigned right=gs.caps.width/2+8, top=(gs.caps.height-97)/2;
    if (ps2_surface_fill(&gs,0,0,0,gs.caps.width,gs.caps.height,0x80200c06) ||
        ps2_surface_create(&gs,WIDTH,HEIGHT,gs.caps.bits_per_pixel,&a) ||
        ps2_surface_create(&gs,196,97,gs.caps.bits_per_pixel,&b) ||
        ps2_surface_upload(&gs,a,0,0,WIDTH,HEIGHT,pixels,sizeof(pixels),PITCH) ||
        ps2_surface_copy(&gs,a,b,0,0,16,8,WIDTH,HEIGHT) ||
        /* Overwrite A after copying: B must retain its independent pixels. */
        ps2_surface_fill(&gs,a,0,0,WIDTH,HEIGHT,0x80e0c820) ||
        ps2_surface_copy(&gs,a,0,0,0,gs.caps.width/2-148,top+8,WIDTH,HEIGHT) ||
        ps2_surface_copy(&gs,b,0,0,0,right,top,196,97)) {
        perror("ps2-gs: surface transfer"); goto out;
    }
    struct timespec delay={.tv_sec=seconds};
    while(!stopped && nanosleep(&delay,&delay)) {
        if (errno!=EINTR) { perror("ps2-gs: display timer"); goto out; }
    }
    if (!stopped) status=0;
out:
    /* Release ownership before fbcon restores its font/mode. Close frees all
     * storage even after a failed or invalidated session. No replay on error. */
    if (gs.fd>=0 && !gs.error) {
        if (a && ps2_surface_destroy(&gs,a)) status=1;
        if (b && !gs.error && ps2_surface_destroy(&gs,b)) status=1;
    }
    ps2_surface_close(&gs);
    if (fb>=0) close(fb);
    if (graphics && ioctl(tty,KDSETMODE,KD_TEXT)) { perror("ps2-gs: restore text"); status=1; }
    if (tty>=0) close(tty);
    if (!status) puts("ps2-gs: offscreen transfer/fence PASSED; pixel correctness requires visual qualification");
    return status;
}
