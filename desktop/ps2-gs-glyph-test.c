/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "ps2_pipe.h"
#undef ARRAY_SIZE
#include "ps2_glyph.h"
#include <X11/fonts/fontstruct.h>
#include "damage.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <signal.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

/* This standalone diagnostic supplies minimal server bookkeeping around the
 * actual driver GC callbacks. It is not an X server or an Xorg session. GPU
 * syscalls and Gallium/GC resource lifetimes remain the actual native code. */
static unsigned private_slots;
static PixmapPtr front;
static int callback_failure;
static jmp_buf abort_callback;
static volatile sig_atomic_t stopped;
static void interrupt(int signal) { (void)signal; stopped=1; }
Bool dixRegisterPrivateKey(DevPrivateKey key,DevPrivateType type,unsigned size)
{
    if(size || (type!=PRIVATE_SCREEN && type!=PRIVATE_GC)) return FALSE;
    if(!key->initialized) {
        if(private_slots==16) return FALSE;
        *key=(DevPrivateKeyRec){.initialized=TRUE,.type=type,.offset=private_slots++*sizeof(void*)};
    }
    return TRUE;
}
void *exaGetPixmapDriverPrivate(PixmapPtr pixmap) { return pixmap->devPrivates; }
void exaMarkSync(ScreenPtr s) { (void)s; }
void DamageRegionAppend(DrawablePtr d,const RegionPtr r) { (void)d; (void)r; }
void DamageRegionProcessPending(DrawablePtr d) { (void)d; }
void FatalError(const char *format,...)
{
    va_list args; va_start(args,format); vfprintf(stderr,format,args); va_end(args);
    callback_failure=1;
    /* Real Xorg FatalError terminates the server. The diagnostic retains its
     * process cleanup so it can release GS ownership and restore the console.
     * Driver FatalError must not return, so transfer to the diagnostic boundary. */
    longjmp(abort_callback,1);
}
static int draw_text(DrawablePtr d,GCPtr gc,int x,int y,unsigned count,CharInfoPtr *text,int image)
{
    if(setjmp(abort_callback)) return -1;
    if(image) gc->ops->ImageGlyphBlt(d,gc,x,y,count,text,NULL);
    else gc->ops->PolyGlyphBlt(d,gc,x,y,count,text,NULL);
    return 0;
}
static PixmapPtr get_front(ScreenPtr screen) { (void)screen; return front; }
static const unsigned char letters[][8]={
    {0x1e,0x12,0x12,0x1e,0x02,0x02,0x02,0}, /* P */
    {0x02,0x02,0x02,0x02,0x02,0x02,0x1e,0}, /* L */
    {0x0c,0x12,0x12,0x1e,0x12,0x12,0x12,0}, /* A */
    {0x12,0x12,0x12,0x0c,0x04,0x04,0x04,0}, /* Y */
    {0x1c,0x02,0x02,0x0c,0x10,0x10,0x0e,0}, /* S */
    {0x1f,0x04,0x04,0x04,0x04,0x04,0x04,0}, /* T */
    {0x0e,0x04,0x04,0x04,0x04,0x04,0x0e,0}, /* I */
    {0x0c,0x12,0x12,0x12,0x12,0x12,0x0c,0}, /* O */
    {0x12,0x16,0x16,0x1a,0x1a,0x12,0x12,0}, /* N */
    {0x0c,0x12,0x10,0x08,0x04,0x02,0x1e,0}, /* 2 */
    {0x1c,0x02,0x02,0x1a,0x12,0x12,0x1c,0}, /* G */
    {0,0,0,0,0,0,0,0}, /* space */
};
static void make_font(CharInfoRec chars[12],unsigned char bits[12][16][4])
{
    for(unsigned i=0;i<12;++i) {
        for(unsigned y=0;y<16;++y) {
            unsigned char value=letters[i][y/2];
            for(unsigned x=0;x<4;++x) bits[i][y][x]=0;
            bits[i][y][0]=value;
        }
        chars[i]=(CharInfoRec){.metrics={.leftSideBearing=0,.rightSideBearing=8,
            .ascent=14,.descent=2,.characterWidth=12},.bits=(char*)bits[i]};
    }
}
int main(int argc,char **argv)
{
    unsigned seconds=4;
    if(argc==2 && !strcmp(argv[1],"--brief")) seconds=1;
    else if(argc!=1) { fputs("Usage: ps2-gs-glyph-test [--brief]\n",stderr); return 2; }
    struct sigaction action={.sa_handler=interrupt}; sigemptyset(&action.sa_mask);
    if(sigaction(SIGINT,&action,NULL) || sigaction(SIGTERM,&action,NULL) || sigaction(SIGHUP,&action,NULL)) return 1;
    struct pipe_screen *screen=NULL; struct pipe_context *context=NULL;
    struct ps2_glyph_gc bridge={0}; struct ps2_surface_exa exa;
    void *screen_privates[16]={0},*gc_privates[16]={0};
    ScreenRec xs={.GetScreenPixmap=get_front,.devPrivates=(void*)screen_privates};
    PixmapRec pixmap={0}; front=&pixmap;
    RegionRec region; int region_ready=0;
    FontRec font={.info={.fontAscent=14,.fontDescent=2}};
    GCOps original={0}; GC gc={0};
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
    struct ps2_render_client *render=ps2_pipe_context_render(context);
    unsigned width=render->gs->caps.width,height=render->gs->caps.height;
    if(render->gs->caps.bits_per_pixel!=32 || width<320 || height<240) { errno=ENOTSUP; goto out; }
    printf("ps2-gs: native Xorg core glyphs %ux%u features=%x\n",width,height,render->gs->caps.features); fflush(stdout);
    pixmap.drawable=(DrawableRec){.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=24,.bitsPerPixel=32,.width=width,.height=height};
    if(!ps2_surface_exa_prepare(&xs,&exa,render->gs)) goto out;
    pixman_region_init_rect(&region,0,0,width,height); region_ready=1;
    gc=(GC){.pScreen=&xs,.font=&font,.ops=&original,.alu=GXcopy,.fillStyle=FillSolid,
        .planemask=0xffffff,.fgPixel=0xffff00,.bgPixel=0x403018,
        .pCompositeClip=&region,.devPrivates=(void*)gc_privates};
    if(!ps2_glyph_gc_attach(&gc,&bridge,&exa,context)) goto out;
    if(ps2_render_fill(render,0,0,width,height,0x80200c06)) goto out;
    unsigned char bits[12][16][4]; CharInfoRec chars[12]; make_font(chars,bits);
    const unsigned order[]={0,1,2,3,4,5,2,5,6,7,8,11,9};
    CharInfoPtr text[13]; for(unsigned i=0;i<13;++i) text[i]=&chars[order[i]];
    int x=(width-13*12)/2,y=(height-80)/2;
    if(draw_text(&pixmap.drawable,&gc,x,y,13,text,0)) goto out;
    gc.fgPixel=0xffffff;
    if(draw_text(&pixmap.drawable,&gc,x,y+32,13,text,1)) goto out;
    pixman_region_fini(&region);
    pixman_box16_t clips[]={{x,y+50,x+48,y+82},{x+72,y+50,x+13*12,y+82}};
    if(!pixman_region_init_rects(&region,clips,2)) { region_ready=0; goto out; }
    gc.fgPixel=0xffff00;
    if(draw_text(&pixmap.drawable,&gc,x,y+64,13,text,0)) goto out;
    struct timespec delay={.tv_sec=seconds};
    while(!stopped && nanosleep(&delay,&delay)) if(errno!=EINTR) goto out;
    if(!stopped) status=0;
out:
    if(callback_failure || (context && ps2_pipe_context_error(context))) status=1;
    if(status && !stopped && !callback_failure) perror("ps2-gs: native glyph probe");
    ps2_glyph_gc_detach(&bridge);
    if(region_ready) pixman_region_fini(&region);
    if(context) context->destroy(context);
    if(screen) screen->destroy(screen);
    if(fb>=0) close(fb);
    if(graphics && ioctl(tty,KDSETMODE,KD_TEXT)) status=1;
    if(tty>=0) close(tty);
    if(!status) puts("ps2-gs: native glyph command/fence PASSED; expect cyan text, white text with background, clipped cyan text, then console");
    return status;
}
