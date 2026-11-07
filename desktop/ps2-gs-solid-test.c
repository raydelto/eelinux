/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "ps2_pipe.h"
#undef ARRAY_SIZE
#include "ps2_solid.h"
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
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

/* Minimal server bookkeeping exercises the actual GC/Mesa/GS callbacks.
 * This diagnostic is not an X server, TWM session or OpenGL application. */
static unsigned private_slots, server_gc_count;
static PixmapPtr front;
static int callback_failure;
static jmp_buf abort_callback;
static volatile sig_atomic_t stopped;
static void interrupt(int sig) { (void)sig; stopped=1; }
Bool dixRegisterPrivateKey(DevPrivateKey key,DevPrivateType type,unsigned size)
{
    if(size || (type!=PRIVATE_SCREEN && type!=PRIVATE_GC) ||
        (type==PRIVATE_GC && !key->initialized && server_gc_count)) return FALSE;
    if(!key->initialized) {
        if(private_slots==16) return FALSE;
        *key=(DevPrivateKeyRec){.initialized=TRUE,.type=type,.offset=private_slots++*sizeof(void*)};
    }
    return TRUE;
}
void *exaGetPixmapDriverPrivate(PixmapPtr pixmap) { return pixmap->devPrivates; }
void exaMarkSync(ScreenPtr screen) { (void)screen; }
void DamageRegionAppend(DrawablePtr d,const RegionPtr r) { (void)d;(void)r; }
void DamageRegionProcessPending(DrawablePtr d) { (void)d; }
void FatalError(const char *format,...)
{
    va_list args;va_start(args,format);vfprintf(stderr,format,args);va_end(args);
    callback_failure=1;longjmp(abort_callback,1);
}
void GetGlyphs(FontPtr font,unsigned long count,unsigned char *text,FontEncoding encoding,
               unsigned long *n,CharInfoPtr *glyphs)
{ font->get_glyphs(font,count,text,encoding,n,glyphs); }
static PixmapPtr get_front(ScreenPtr s) { (void)s;return front; }
static void validate_gc(GCPtr gc,unsigned long mask,DrawablePtr d) { (void)gc;(void)mask;(void)d; }
static void change_gc(GCPtr gc,unsigned long mask) { (void)gc;(void)mask; }
static void copy_gc(GCPtr from,unsigned long mask,GCPtr to) { (void)from;(void)mask;(void)to; }
static void destroy_gc(GCPtr gc) { (void)gc;--server_gc_count; }
static void change_clip(GCPtr gc,int type,void *value,int n) { (void)gc;(void)type;(void)value;(void)n; }
static void destroy_clip(GCPtr gc) { (void)gc; }
static void copy_clip(GCPtr to,GCPtr from) { (void)to;(void)from; }
static const GCFuncs server_funcs={validate_gc,change_gc,copy_gc,destroy_gc,change_clip,destroy_clip,copy_clip};
static const GCOps server_ops={0};
static Bool create_gc(GCPtr gc) { gc->funcs=&server_funcs;gc->ops=&server_ops;++server_gc_count;return TRUE; }
static Bool close_screen(ScreenPtr s) { (void)s;return TRUE; }
static int draw(DrawablePtr d,GCPtr gc,int kind,int n,void *items,int *widths)
{
    if(setjmp(abort_callback)) return -1;
    gc->funcs->ChangeGC(gc,GCForeground|GCFunction);
    gc->funcs->ValidateGC(gc,GCAllBits,d);
    if(kind==0) gc->ops->PolyFillRect(d,gc,n,items);
    else if(kind==1) gc->ops->PolyRectangle(d,gc,n,items);
    else if(kind==2) gc->ops->PolySegment(d,gc,n,items);
    else if(kind==3) gc->ops->PolyPoint(d,gc,CoordModeOrigin,n,items);
    else if(kind==4) gc->ops->FillSpans(d,gc,n,items,widths,1);
    else gc->ops->Polylines(d,gc,CoordModeOrigin,n,items);
    return 0;
}
static int pause_scene(unsigned seconds)
{
    struct timespec delay={.tv_sec=seconds};
    while(!stopped && nanosleep(&delay,&delay)) if(errno!=EINTR) return -1;
    return stopped ? -1 : 0;
}
int main(int argc,char **argv)
{
    unsigned seconds=3;
    if(argc==2 && !strcmp(argv[1],"--brief")) seconds=1;
    else if(argc!=1) { fputs("usage: ps2-gs-solid-test [--brief]\n",stderr);return 2; }
    signal(SIGINT,interrupt);signal(SIGTERM,interrupt);signal(SIGHUP,interrupt);
    int tty=-1,fb=-1,graphics=0,region_ready=0,gc_created=0,status=1;
    struct pipe_screen *screen=NULL;struct pipe_context *context=NULL;
    struct ps2_surface_exa exa={0};struct ps2_gc_screen bridge={0};struct ps2_solid_screen solid={0};
    void *screen_privates[16]={0},*gc_privates[16]={0};
    ScreenRec xs={.GetScreenPixmap=get_front,.CreateGC=create_gc,.CloseScreen=close_screen,.devPrivates=(void*)screen_privates};
    PixmapRec pixmap={0};GC gc={0};RegionRec region;front=&pixmap;
    tty=open("/dev/tty0",O_RDWR|O_CLOEXEC);int mode;
    if(tty<0 || ioctl(tty,KDGETMODE,&mode) || mode!=KD_TEXT || ioctl(tty,KDSETMODE,KD_GRAPHICS)) goto out;
    graphics=1;fb=open("/dev/fb0",O_RDWR|O_CLOEXEC);struct fb_var_screeninfo var;
    if(fb<0 || ioctl(fb,FBIOGET_VSCREENINFO,&var)) goto out;
    var.xoffset=var.yoffset=0;var.vmode&=~FB_VMODE_YWRAP;
    if(ioctl(fb,FBIOPAN_DISPLAY,&var)) goto out;
    screen=ps2_pipe_screen_create("/dev/ps2-gs");
    if(!screen || !(context=screen->context_create(screen,NULL,0))) goto out;
    struct ps2_render_client *render=ps2_pipe_context_render(context);
    unsigned width=render->gs->caps.width,height=render->gs->caps.height;
    if(render->gs->caps.bits_per_pixel!=32 || width<448 || height<320) { errno=ENOTSUP;goto out; }
    printf("ps2-gs: native Xorg solid GC %ux%u features=%x\n",width,height,render->gs->caps.features);fflush(stdout);
    pixmap.drawable=(DrawableRec){.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=24,.bitsPerPixel=32,.width=width,.height=height};
    if(!ps2_surface_exa_prepare(&xs,&exa,render->gs)) goto out;
    pixman_region_init_rect(&region,0,0,width,height);region_ready=1;
    gc=(GC){.pScreen=&xs,.alu=GXcopy,.fillStyle=FillSolid,.lineStyle=LineSolid,.capStyle=CapButt,
        .planemask=0xffffff,.pCompositeClip=&region,.devPrivates=(void*)gc_privates};
    if(!ps2_gc_screen_prepare(&xs,&bridge,&exa,context) ||
        !ps2_solid_screen_prepare(&xs,&solid,&bridge) || !xs.CreateGC(&gc)) goto out;
    gc_created=1;DrawablePtr d=&pixmap.drawable;
    xRectangle background={0,0,width,height};gc.fgPixel=0x201006;
    if(draw(d,&gc,0,1,&background,NULL)) goto out;
    int x=(width-320)/2,y=(height-180)/2;
    xRectangle panel={x,y,320,180};gc.fgPixel=0x403018;
    if(draw(d,&gc,0,1,&panel,NULL)) goto out;
    xRectangle title={x+1,y+1,318,20};gc.fgPixel=0xffff00;
    if(draw(d,&gc,0,1,&title,NULL)) goto out;
    xRectangle body={x+12,y+36,296,130};gc.fgPixel=0x201006;
    if(draw(d,&gc,0,1,&body,NULL)) goto out;
    xRectangle border={x,y,319,179};gc.fgPixel=0xffff00;
    if(draw(d,&gc,1,1,&border,NULL)) goto out;
    xSegment bars[]={{x+24,y+57,x+294,y+57},{x+24,y+82,x+210,y+82}};
    if(draw(d,&gc,2,2,bars,NULL)) goto out;
    DDXPointRec dots[]={{x+289,y+10},{x+300,y+10}};gc.fgPixel=0x201006;
    if(draw(d,&gc,3,2,dots,NULL)) goto out;
    DDXPointRec spans[]={{x+24,y+110},{x+24,y+111}};int widths[]={146,146};gc.fgPixel=0x996644;
    if(draw(d,&gc,4,2,spans,widths)) goto out;
    DDXPointRec corner[]={{x+270,y+130},{x+294,y+130},{x+294,y+150}};gc.fgPixel=0xffff00;
    if(draw(d,&gc,5,3,corner,NULL) || pause_scene(seconds)) goto out;
    gc.alu=GXxor;gc.fgPixel=0xffffff;
    for(unsigned i=0;i<3;++i) {
        xRectangle outline={x-30+20*i,y-20+12*i,300,210};
        if(draw(d,&gc,1,1,&outline,NULL) || pause_scene(seconds) ||
            draw(d,&gc,1,1,&outline,NULL) || pause_scene(seconds)) goto out;
    }
    status=0;
out:
    if(callback_failure || (context && ps2_pipe_context_error(context))) status=1;
    if(status && !stopped && !callback_failure) perror("ps2-gs: native solid GC probe");
    if(gc_created) gc.funcs->DestroyGC(&gc);
    if(server_gc_count) status=1;
    if(solid.installed || bridge.installed) (void)xs.CloseScreen(&xs);
    ps2_solid_screen_cleanup(&solid);ps2_gc_screen_cleanup(&bridge);
    if(region_ready) pixman_region_fini(&region);
    if(context) context->destroy(context);
    if(screen) screen->destroy(screen);
    if(fb>=0) close(fb);
    if(graphics && ioctl(tty,KDSETMODE,KD_TEXT)) status=1;
    if(tty>=0) close(tty);
    if(!status) puts("ps2-gs: solid GC command/fence PASSED; expect cyan panel and three white outlines appearing then erasing, then console");
    return status;
}
