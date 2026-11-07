/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "ps2_pipe.h"
#undef ARRAY_SIZE
#include "ps2_gc.h"
#include "dixfont.h"
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
static unsigned private_slots, server_gc_count;
static PixmapPtr front;
static int callback_failure;
static jmp_buf abort_callback;
static volatile sig_atomic_t stopped;
static void interrupt(int signal) { (void)signal; stopped=1; }
Bool dixRegisterPrivateKey(DevPrivateKey key,DevPrivateType type,unsigned size)
{
    if(size || (type!=PRIVATE_SCREEN && type!=PRIVATE_GC)) return FALSE;
    /* Real Xorg cannot grow GC private keys after GC allocation. */
    if(type==PRIVATE_GC && !key->initialized && server_gc_count) return FALSE;
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
static int draw_text(DrawablePtr d,GCPtr gc,int x,int y,unsigned count,void *text,int kind)
{
    if(setjmp(abort_callback)) return -1;
    gc->funcs->ChangeGC(gc,GCForeground);
    gc->funcs->ValidateGC(gc,GCAllBits,d);
    if(kind==1) gc->ops->ImageText16(d,gc,x,y,count,text);
    else if(kind==2) (void)gc->ops->PolyText16(d,gc,x,y,count,text);
    else (void)gc->ops->PolyText8(d,gc,x,y,count,text);
    return 0;
}
static PixmapPtr get_front(ScreenPtr screen) { (void)screen; return front; }
/* Minimal server/font bookkeeping around the actual lifecycle and text hooks.
 * This is not a full X server. GetGlyphs forwards through the real Font ABI. */
static CharInfoPtr active_font;
static void validate_gc(GCPtr gc,unsigned long changes,DrawablePtr d)
{ (void)gc; (void)changes; (void)d; }
static void change_gc(GCPtr gc,unsigned long mask) { (void)gc; (void)mask; }
static void copy_gc(GCPtr source,unsigned long mask,GCPtr destination)
{ (void)source; (void)mask; (void)destination; }
static void destroy_gc(GCPtr gc) { (void)gc; --server_gc_count; }
static void change_clip(GCPtr gc,int type,void *value,int count)
{ (void)gc; (void)type; (void)value; (void)count; }
static void destroy_clip(GCPtr gc) { (void)gc; }
static void copy_clip(GCPtr destination,GCPtr source) { (void)destination; (void)source; }
static const GCFuncs server_funcs={validate_gc,change_gc,copy_gc,destroy_gc,
                                  change_clip,destroy_clip,copy_clip};
static const GCOps server_ops={0};
static Bool create_gc(GCPtr gc)
{ gc->funcs=&server_funcs; gc->ops=&server_ops; ++server_gc_count; return TRUE; }
static Bool close_screen(ScreenPtr screen) { (void)screen; return TRUE; }
static int font_glyphs(FontPtr font,unsigned long count,unsigned char *text,
                       FontEncoding encoding,unsigned long *n,CharInfoPtr *glyphs)
{
    (void)font; const char *letters="PLAYSTION2G "; *n=0;
    for(unsigned long i=0;i<count;++i) {
        unsigned char c=encoding==Linear8Bit ? text[i] : text[2*i+1];
        const char *found=c ? strchr(letters,c) : NULL;
        if(found) glyphs[(*n)++]=&active_font[found-letters];
    }
    return 0;
}
void GetGlyphs(FontPtr font,unsigned long count,unsigned char *text,FontEncoding encoding,
               unsigned long *n,CharInfoPtr *glyphs)
{ font->get_glyphs(font,count,text,encoding,n,glyphs); }
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
    else if(argc!=1) { fputs("Usage: ps2-gs-gc-test [--brief]\n",stderr); return 2; }
    struct sigaction action={.sa_handler=interrupt}; sigemptyset(&action.sa_mask);
    if(sigaction(SIGINT,&action,NULL) || sigaction(SIGTERM,&action,NULL) || sigaction(SIGHUP,&action,NULL)) return 1;
    struct pipe_screen *screen=NULL; struct pipe_context *context=NULL;
    struct ps2_gc_screen bridge={0}; struct ps2_surface_exa exa;
    void *screen_privates[16]={0},*gc_privates[16]={0};
    ScreenRec xs={.GetScreenPixmap=get_front,.CreateGC=create_gc,.CloseScreen=close_screen,.devPrivates=(void*)screen_privates};
    PixmapRec pixmap={0}; front=&pixmap;
    RegionRec region; int region_ready=0;
    FontRec font={.info={.fontAscent=14,.fontDescent=2},.get_glyphs=font_glyphs};
    GC gc={0}; int gc_created=0;
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
    printf("ps2-gs: native Xorg GC text %ux%u features=%x\n",width,height,render->gs->caps.features); fflush(stdout);
    pixmap.drawable=(DrawableRec){.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=24,.bitsPerPixel=32,.width=width,.height=height};
    if(!ps2_surface_exa_prepare(&xs,&exa,render->gs)) goto out;
    pixman_region_init_rect(&region,0,0,width,height); region_ready=1;
    gc=(GC){.pScreen=&xs,.font=&font,.alu=GXcopy,.fillStyle=FillSolid,
        .planemask=0xffffff,.fgPixel=0xffff00,.bgPixel=0x403018,
        .pCompositeClip=&region,.devPrivates=(void*)gc_privates};
    if(!ps2_gc_screen_prepare(&xs,&bridge,&exa,context) || !xs.CreateGC(&gc)) goto out;
    gc_created=1;
    if(ps2_render_fill(render,0,0,width,height,0x80200c06)) goto out;
    unsigned char bits[12][16][4]; CharInfoRec chars[12]; make_font(chars,bits);
    active_font=chars;
    char text[]="PLAYSTATION 2";
    unsigned short wide[13];
    for(unsigned i=0;i<13;++i) wide[i]=(unsigned char)text[i]<<8;
    int x=(width-13*12)/2,y=(height-80)/2;
    if(draw_text(&pixmap.drawable,&gc,x,y,13,text,0)) goto out;
    gc.fgPixel=0xffffff;
    if(draw_text(&pixmap.drawable,&gc,x,y+32,13,wide,1)) goto out;
    pixman_region_fini(&region);
    pixman_box16_t clips[]={{x,y+50,x+48,y+82},{x+72,y+50,x+13*12,y+82}};
    if(!pixman_region_init_rects(&region,clips,2)) { region_ready=0; goto out; }
    gc.fgPixel=0xffff00; font.info.lastRow=1;
    gc.funcs->ChangeClip(&gc,CT_REGION,&region,2);
    if(draw_text(&pixmap.drawable,&gc,x,y+64,13,wide,2)) goto out;
    struct timespec delay={.tv_sec=seconds};
    while(!stopped && nanosleep(&delay,&delay)) if(errno!=EINTR) goto out;
    if(!stopped) status=0;
out:
    if(callback_failure || (context && ps2_pipe_context_error(context))) status=1;
    if(status && !stopped && !callback_failure) perror("ps2-gs: native GC probe");
    if(gc_created) gc.funcs->DestroyGC(&gc);
    if(server_gc_count) status=1;
    if(bridge.installed) (void)xs.CloseScreen(&xs);
    ps2_gc_screen_cleanup(&bridge);
    if(region_ready) pixman_region_fini(&region);
    if(context) context->destroy(context);
    if(screen) screen->destroy(screen);
    if(fb>=0) close(fb);
    if(graphics && ioctl(tty,KDSETMODE,KD_TEXT)) status=1;
    if(tty>=0) close(tty);
    if(!status) puts("ps2-gs: GC text/lifetime command/fence PASSED; expect cyan text, white text with background, clipped cyan text, then console");
    return status;
}
