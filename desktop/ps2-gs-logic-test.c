/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "ps2_logic.h"
#undef ARRAY_SIZE
#include "ps2_logic_exa.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <signal.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
/* Standalone native callback diagnostic, not a complete X server. */
static unsigned private_slots;
static PixmapPtr front;
static volatile sig_atomic_t stopped;
static jmp_buf abort_callback;
static void interrupt(int signal) { (void)signal; stopped=1; }
Bool dixRegisterPrivateKey(DevPrivateKey key,DevPrivateType type,unsigned size)
{
    if(size || type!=PRIVATE_SCREEN) return FALSE;
    if(!key->initialized) {
        if(private_slots==16) return FALSE;
        *key=(DevPrivateKeyRec){.initialized=TRUE,.type=type,.offset=private_slots++*sizeof(void*)};
    }
    return TRUE;
}
void *exaGetPixmapDriverPrivate(PixmapPtr pixmap) { return pixmap->devPrivates; }
void FatalError(const char *format,...)
{
    va_list args; va_start(args,format); vfprintf(stderr,format,args); va_end(args);
    longjmp(abort_callback,1);
}
static PixmapPtr get_front(ScreenPtr screen) { (void)screen; return front; }
/* Keep the FatalError boundary outside the caller's resource cleanup scope. */
static int rectangle(struct ps2_surface_exa *exa, PixmapPtr p,
    int x, int y, int w, int h, unsigned alu, unsigned color, unsigned mask)
{
    if(setjmp(abort_callback)) return -1;
    if(!exa->driver.PrepareSolid(p,alu,mask,color)) { errno=ENOTSUP; return -1; }
    exa->driver.Solid(p,x,y,x+w,y+h);
    exa->driver.DoneSolid(p);
    return 0;
}
static int wait_scene(unsigned seconds)
{
    for(unsigned i=0;i<seconds && !stopped;++i) {
        struct timespec delay={.tv_sec=1};
        while(nanosleep(&delay,&delay)<0) if(errno!=EINTR || stopped) return -1;
    }
    return stopped ? -1 : 0;
}
int main(int argc,char **argv)
{
    unsigned seconds=3;
    if(argc==2 && !strcmp(argv[1],"--brief")) seconds=1;
    else if(argc!=1) { fprintf(stderr,"Usage: ps2-gs-logic-test [--brief]\n"); return 2; }
    struct sigaction action={.sa_handler=interrupt}; sigemptyset(&action.sa_mask);
    if(sigaction(SIGINT,&action,NULL) || sigaction(SIGTERM,&action,NULL) || sigaction(SIGHUP,&action,NULL)) return 1;
    struct pipe_screen *screen=NULL; struct pipe_context *context=NULL;
    struct ps2_surface_exa exa={0}; struct ps2_exa_context_bridge ordering={0}; struct ps2_logic_exa logic={0};
    void *privates[16]={0}; ScreenRec xscreen={.devPrivates=(void*)privates,.GetScreenPixmap=get_front};
    PixmapRec pixmap={.drawable={.pScreen=&xscreen,.depth=24,.bitsPerPixel=32}};
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
    if(!screen || !(context=screen->context_create(screen,NULL,0))) goto out;
    struct ps2_surface_client *gs=ps2_pipe_context_render(context)->gs;
    if(gs->caps.bits_per_pixel!=32 || gs->caps.width<448 || gs->caps.height<320) { errno=ENOTSUP; goto out; }
    pixmap.drawable.width=gs->caps.width; pixmap.drawable.height=gs->caps.height; front=&pixmap;
    if(!ps2_surface_exa_prepare(&xscreen,&exa,gs) ||
        !ps2_exa_context_bridge_prepare(&xscreen,&ordering,&exa,context) ||
        !ps2_logic_exa_prepare(&xscreen,&logic,&exa,context)) goto out;
    printf("ps2-gs: native solid logic %ux%u features=%x\n",gs->caps.width,gs->caps.height,gs->caps.features); fflush(stdout);
    struct pipe_box full={.width=gs->caps.width,.height=gs->caps.height,.depth=1};
    if(ps2_pipe_solid_logic(context,NULL,&full,0x201006,0xffffff,3)) goto out;
    int left=(gs->caps.width-432)/2,top=(gs->caps.height-288)/2;
    for(unsigned alu=0;alu<16;++alu) {
        int x=left+(alu%4)*112,y=top+(alu/4)*76;
        if(rectangle(&exa,&pixmap,x,y,96,60,3,0x957c2a,0xffffff) ||
            rectangle(&exa,&pixmap,x,y,96,60,alu,0x56c3a9,0xffffff)) goto out;
    }
    if(wait_scene(seconds)) goto out;
    /* A white XOR outline must disappear after drawing exactly twice. */
    for(unsigned pass=0;pass<2;++pass) {
        if(rectangle(&exa,&pixmap,left-4,top-4,432+8,2,6,0xffffff,0xffffff) ||
            rectangle(&exa,&pixmap,left-4,top+288+2,432+8,2,6,0xffffff,0xffffff) ||
            rectangle(&exa,&pixmap,left-4,top-2,2,288+4,6,0xffffff,0xffffff) ||
            rectangle(&exa,&pixmap,left+432+2,top-2,2,288+4,6,0xffffff,0xffffff)) goto out;
        if(wait_scene(seconds)) goto out;
    }
    puts("ps2-gs: solid logic command/fence PASSED; expect 4x4 ALU tiles, XOR outline appearing then disappearing, then console");
    status=0;
out:
    ps2_logic_exa_cleanup(&xscreen,&logic);
    ps2_exa_context_bridge_cleanup(&ordering);
    if(context) context->destroy(context);
    if(screen) screen->destroy(screen);
    if(fb>=0) close(fb);
    if(graphics && ioctl(tty,KDSETMODE,KD_TEXT)) status=1;
    if(tty>=0) close(tty);
    return status;
}
