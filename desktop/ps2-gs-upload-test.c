/* SPDX-License-Identifier: MIT */
/* Native GS image-transfer diagnostic. Generated pixels are test input, not a
 * CPU rasterizer. The kernel transfers them through GIF to the visible GS.
 */
#define _POSIX_C_SOURCE 200809L
#include "ps2_gs_client.h"
#include <errno.h>
#include <linux/kd.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

static volatile sig_atomic_t stop;
static void interrupted(int sig) { (void)sig; stop=1; }

int main(int argc,char **argv)
{
    unsigned seconds=4;
    if (argc==2 && !strcmp(argv[1],"--brief")) seconds=1;
    else if (argc!=1) { fputs("Usage: ps2-gs-upload-test [--brief]\n",stderr); return 2; }
    struct ps2_gs_client gs={.fd=-1};
    int tty=-1,old=KD_TEXT,changed=0,result=1;
    if (ps2_gs_open(&gs,"/dev/fb0")) { perror("ps2-gs: open upload engine"); goto out; }
    if (!(gs.caps.features & PS2GS_HW_UPLOAD) || gs.caps.width<160 || gs.caps.height<120) {
        fputs("ps2-gs: this kernel/mode has no compatible image upload\n",stderr); goto out;
    }
    tty=open("/dev/tty0",O_RDWR|O_CLOEXEC);
    if (tty<0 || ioctl(tty,KDGETMODE,&old)<0) { perror("ps2-gs: console mode"); goto out; }
    if (old!=KD_TEXT) { fputs("ps2-gs: console is already owned by a graphics client\n",stderr); goto out; }
    struct sigaction action={.sa_handler=interrupted}; sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT,&action,NULL) || sigaction(SIGTERM,&action,NULL) ||
        sigaction(SIGHUP,&action,NULL)) { perror("ps2-gs: signal handler"); goto out; }
    printf("ps2-gs: image upload %ux%u %ubpp; expected colour bars and checkerboard\n",
        gs.caps.width,gs.caps.height,gs.caps.bits_per_pixel); fflush(stdout);
    if (ioctl(tty,KDSETMODE,KD_GRAPHICS)<0) { perror("ps2-gs: take console"); goto out; }
    changed=1;
    if (ps2_gs_fill(&gs,0,0,gs.caps.width,gs.caps.height,0x80200b07)) goto drawing_error;
    /* Odd width/height exercise final GIF padding, row boundaries and several
     * bounded transfers. The explicit source stride includes unused bytes. */
    enum { W=131,H=65,PITCH=528 };
    unsigned char pixels[H*PITCH]; memset(pixels,0xa5,sizeof(pixels));
    unsigned bpp=gs.caps.bits_per_pixel/8;
    for (unsigned y=0;y<H;++y) for (unsigned x=0;x<W;++x) {
        unsigned color;
        if (y<32) color=x<33 ? 0x0000ff : x<66 ? 0x00ff00 : x<99 ? 0xff0000 : 0xffffff;
        else color=((x/8+y/8)&1) ? 0xffffff : 0;
        if (bpp==4) {
            uint32_t p=color|0x80000000; memcpy(pixels+y*PITCH+x*4,&p,4);
        } else {
            uint16_t p=((color&255)>>3) | (((color>>8)&255)>>3)<<5 |
                       (((color>>16)&255)>>3)<<10 | 0x8000;
            memcpy(pixels+y*PITCH+x*2,&p,2);
        }
    }
    if (ps2_gs_upload(&gs,16,24,W,H,pixels,sizeof(pixels),PITCH)) goto drawing_error;
    struct timespec remaining={.tv_sec=seconds};
    while (!stop && nanosleep(&remaining,&remaining)<0 && errno==EINTR) {}
    if (!stop) {
        puts("ps2-gs: image transfer/fence PASSED; pixel correctness requires visual qualification");
        result=0;
    }
    goto out;
drawing_error:
    errno=gs.error; perror("ps2-gs: hardware image upload");
out:
    if (changed && ioctl(tty,KDSETMODE,old)<0) { perror("ps2-gs: restore console"); result=1; }
    if (tty>=0) close(tty);
    ps2_gs_close(&gs);
    return result;
}
