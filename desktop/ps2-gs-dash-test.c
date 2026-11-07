/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "ps2_pipe.h"
#include "ps2_gs_client.h"
#undef ARRAY_SIZE
#include "ps2_solid.h"
#include "ps2_fill.h"
#include "ps2_blit.h"
#include "util/u_inlines.h"
#undef ARRAY_SIZE
#include "windowstr.h"
#include "dixfont.h"
#include <X11/fonts/fontstruct.h>
#include "damage.h"
#include "miline.h"
DevPrivateKeyRec miZeroLineScreenKeyRec;
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
BoxRec RegionEmptyBox={0};
RegDataRec RegionEmptyData={0},RegionBrokenData={0};
static PixmapPtr front,backing;
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
static PixmapPtr get_window(WindowPtr w) { (void)w;return backing; }
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
/* Independent incremental geometry for the point reference. It never computes
 * or stores colour pixels. Source segments in this probe are at most 192 long. */
static int point_reference(DrawablePtr d,GCPtr gc,int n,const xSegment *segs,int ox,int oy,int last)
{
    unsigned phase=gc->dashOffset,total=0;for(unsigned i=0;i<gc->numInDashList;++i)total+=gc->dash[i];
    Pixel foreground=gc->fgPixel;
    for(int i=0;i<n;++i) {
        int x=segs[i].x1,y=segs[i].y1,dx=segs[i].x2-x,dy=segs[i].y2-y;
        int sx=dx<0?-1:1,sy=dy<0?-1:1,ax=dx<0?-dx:dx,ay=dy<0?-dy:dy;
        int ym=ax<=ay,major=ym?ay:ax,minor=ym?ax:ay;
        unsigned oct=(dx<0?4:0)|(dy<0?2:0)|(ym?1:0);
        int e=2*minor-major-((DEFAULTZEROLINEBIAS>>oct)&1);
        DDXPointRec points[2][193];int counts[2]={0,0},count=major+last;
        if(count>193 || !total){errno=E2BIG;return -1;}
        for(int k=0;k<count;++k) {
            unsigned at=(phase+k)%total,index=0;
            while(at>=gc->dash[index]){at-=gc->dash[index];++index;}
            unsigned bg=index&1;
            if(!bg || gc->lineStyle==LineDoubleDash)points[bg][counts[bg]++]=(DDXPointRec){x+ox,y+oy};
            if(e>=0){if(ym)x+=sx;else y+=sy;e-=2*major;}
            if(ym)y+=sy;else x+=sx;e+=2*minor;
        }
        gc->fgPixel=foreground;
        if(counts[0] && draw(d,gc,3,counts[0],points[0],NULL))return -1;
        gc->fgPixel=gc->bgPixel;
        if(counts[1] && draw(d,gc,3,counts[1],points[1],NULL))return -1;
        gc->fgPixel=foreground;phase=last ? gc->dashOffset : phase+count;
    }
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
    else if(argc!=1) { fputs("usage: ps2-gs-dash-test [--brief]\n",stderr);return 2; }
    stopped=0;callback_failure=0;
    struct sigaction action={.sa_handler=interrupt};sigemptyset(&action.sa_mask);
    if(sigaction(SIGINT,&action,NULL) || sigaction(SIGTERM,&action,NULL) || sigaction(SIGHUP,&action,NULL)) return 1;
    int tty=-1,fb=-1,graphics=0,region_ready=0,gc_created=0,status=1;
    struct pipe_screen *screen=NULL;struct pipe_context *context=NULL;
    struct pipe_resource *canvas=NULL;
    struct ps2_surface_exa exa={0};struct ps2_gc_screen bridge={0};
    struct ps2_solid_screen solid={0};
    void *screen_privates[16]={0},*gc_privates[16]={0};
    ScreenRec xs={.GetScreenPixmap=get_front,.GetWindowPixmap=get_window,.CreateGC=create_gc,
        .CloseScreen=close_screen,.devPrivates=(void*)screen_privates};
    PixmapRec fp={0},cp={0};WindowRec window={0};
    uint32_t ch=0;GC gc={0};RegionRec region;front=backing=&fp;
    tty=open("/dev/tty0",O_RDWR|O_CLOEXEC);int mode;
    if(tty<0 || ioctl(tty,KDGETMODE,&mode) || mode!=KD_TEXT || ioctl(tty,KDSETMODE,KD_GRAPHICS)) goto out;
    graphics=1;fb=open("/dev/fb0",O_RDWR|O_CLOEXEC);struct fb_var_screeninfo var;
    if(fb<0 || ioctl(fb,FBIOGET_VSCREENINFO,&var)) goto out;
    var.xoffset=var.yoffset=0;var.vmode&=~FB_VMODE_YWRAP;
    if(ioctl(fb,FBIOPAN_DISPLAY,&var)) goto out;
    screen=ps2_pipe_screen_create("/dev/ps2-gs");
    if(!screen || !(context=screen->context_create(screen,NULL,0))) goto out;
    struct ps2_render_client *render=ps2_pipe_context_render(context);struct ps2_surface_client *gs=render->gs;
    unsigned width=gs->caps.width,height=gs->caps.height;
    if(gs->caps.bits_per_pixel!=32 || width<448 || height<288) { errno=ENOTSUP;goto out; }
    printf("ps2-gs: thin dash GC / shared Mesa %ux%u features=%x\n",width,height,gs->caps.features);fflush(stdout);
    struct pipe_resource t={.target=PIPE_TEXTURE_2D,.format=PIPE_FORMAT_R8G8B8X8_UNORM,
        .width0=192,.height0=128,.depth0=1,.array_size=1,.bind=PIPE_BIND_RENDER_TARGET};
    canvas=screen->resource_create(screen,&t);if(!canvas) goto out;
    fp.drawable=(DrawableRec){.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=24,.bitsPerPixel=32,.width=width,.height=height};
    cp.drawable=fp.drawable;cp.drawable.width=192;cp.drawable.height=128;
    ch=((struct ps2_resource*)canvas)->handle;cp.devPrivates=(void*)&ch;
#ifdef COMPOSITE
    cp.screen_x=100;cp.screen_y=100;
#endif
    if(!dixRegisterPrivateKey(miZeroLineScreenKey,PRIVATE_SCREEN,0)) goto out;
    dixSetPrivate(&xs.devPrivates,miZeroLineScreenKey,(void*)(uintptr_t)DEFAULTZEROLINEBIAS);
    if(!ps2_surface_exa_prepare(&xs,&exa,gs)) goto out;
    pixman_region_init_rect(&region,0,0,width,height);region_ready=1;
    unsigned char dashes[2]={7,4};
    gc=(GC){.pScreen=&xs,.dash=dashes,.numInDashList=2,.alu=GXcopy,.fillStyle=FillSolid,.lineStyle=LineOnOffDash,.lineWidth=0,.capStyle=CapButt,.fgPixel=0xffffff,
        .planemask=0xffffff,.pCompositeClip=&region,.devPrivates=(void*)gc_privates};
    if(!ps2_gc_screen_prepare(&xs,&bridge,&exa,context) || !ps2_solid_screen_prepare(&xs,&solid,&bridge) ||
        !xs.CreateGC(&gc)) goto out;
    gc_created=1;DrawablePtr d=&fp.drawable;
    /* CPU arrays hold geometry only. Colour destinations, Boolean ALUs and
     * presentation use GS operations; no colour framebuffer mapping/upload. */
    int x=(width-448)/2,y=(height-288)/2;
    xSegment rays[8];int n=0;
    for(int sx=-1;sx<=1;sx+=2)for(int sy=-1;sy<=1;sy+=2)for(int steep=0;steep<2;++steep)
        rays[n++]=(xSegment){96,64,96+sx*(steep?37:84),64+sy*(steep?56:37)};
    puts("ps2-gs: dash scene 1/4: matching cyan on/off dashed fans; point reference beside native segments");fflush(stdout);
    if(ps2_surface_fill(gs,0,0,0,width,height,0x80200c06)) goto out;
    gc.fgPixel=0xffff00;
    if(point_reference(d,&gc,8,rays,x+16,y+64,1)) goto out;
    xSegment translated[8];
    for(int i=0;i<8;++i)translated[i]=(xSegment){rays[i].x1+x+240,rays[i].y1+y+64,rays[i].x2+x+240,rays[i].y2+y+64};
    if(draw(d,&gc,2,8,translated,NULL) || pause_scene(seconds)) goto out;
    puts("ps2-gs: dash scene 2/4: yellow/cyan double-dash diagonals on blue with a preserved red central gap");fflush(stdout);
    if(ps2_surface_fill(gs,0,0,0,width,height,0x80200c06) ||
       ps2_surface_fill(gs,0,x+32,y+64,384,128,0x80ff0000) ||
       ps2_surface_fill(gs,0,x+218,y+64,12,128,0x800000ff)) goto out;
    pixman_box16_t clips[]={{x+32,y+64,x+218,y+192},{x+230,y+64,x+416,y+192}};
    RegionUninit(&region);if(!pixman_region_init_rects(&region,clips,2)){region_ready=0;goto out;}
    xSegment crossing[]={{x+32,y+64,x+415,y+191},{x+32,y+191,x+415,y+64},
        {x+32,y+96,x+415,y+160},{x+32,y+160,x+415,y+96}};
    gc.fgPixel=0x00ffff;gc.bgPixel=0xffff00;gc.lineStyle=LineDoubleDash;
    dashes[0]=9;dashes[1]=5;gc.dashOffset=6;
    if(draw(d,&gc,2,4,crossing,NULL) || pause_scene(seconds)) goto out;
    puts("ps2-gs: dash scene 3/4: matching white/yellow double-dash XOR diamonds; two XOR passes restore green bar");fflush(stdout);
    RegionReset(&region,&(BoxRec){0,0,width,height});
    if(ps2_surface_fill(gs,0,0,0,width,height,0x80200c06) ||
       ps2_surface_fill(gs,0,x+16,y+64,192,128,0x80000000) ||
       ps2_surface_fill(gs,0,x+240,y+64,192,128,0x80000000) ||
       ps2_surface_fill(gs,0,x+16,y+224,416,16,0x8000ff00)) goto out;
    xSegment diamond[]={{96,8,184,64},{184,64,96,120},{96,120,8,64},{8,64,96,8}};
    gc.alu=GXxor;gc.fgPixel=0xffffff;gc.bgPixel=0x00ffff;gc.lineStyle=LineDoubleDash;
    dashes[0]=11;dashes[1]=5;gc.dashOffset=3;
    if(point_reference(d,&gc,4,diamond,x+16,y+64,0)) goto out;
    DDXPointRec pts[]={{x+336,y+72},{x+424,y+128},{x+336,y+184},{x+248,y+128},{x+336,y+72}};
    for(int i=0;i<3;++i)if(draw(d,&gc,5,5,pts,NULL)) goto out;
    gc.fgPixel=0xff00ff;gc.lineStyle=LineOnOffDash;xSegment bar={x+16,y+224,x+431,y+239};
    if(draw(d,&gc,2,1,&bar,NULL) || draw(d,&gc,2,1,&bar,NULL) || pause_scene(seconds)) goto out;
    puts("ps2-gs: dash scene 4/4: matching magenta/cyan double-dash front/backing diamonds with white completion bar");fflush(stdout);
    if(ps2_surface_fill(gs,0,0,0,width,height,0x80200c06)) goto out;
    gc.alu=GXcopy;gc.fgPixel=0xff00ff;gc.bgPixel=0xffff00;gc.lineStyle=LineDoubleDash;
    dashes[0]=6;dashes[1]=4;gc.dashOffset=2;
    DDXPointRec front_pts[]={{x+112,y+72},{x+200,y+128},{x+112,y+184},{x+24,y+128},{x+112,y+72}};
    if(draw(d,&gc,5,5,front_pts,NULL) || ps2_surface_fill(gs,ch,0,0,192,128,0x80200c06)) goto out;
    backing=&cp;int ox=0,oy=0;
#ifdef COMPOSITE
    ox=cp.screen_x;oy=cp.screen_y;
#endif
    window.drawable=(DrawableRec){.type=DRAWABLE_WINDOW,.pScreen=&xs,.depth=24,.bitsPerPixel=32,
        .x=ox,.y=oy,.width=192,.height=128};window.realized=TRUE;
    RegionReset(&region,&(BoxRec){ox,oy,ox+192,oy+128});gc.miTranslate=TRUE;
    DDXPointRec local[]={{96,8},{184,64},{96,120},{8,64},{96,8}};
    const struct ps2_pipe_blit_rect present={.sw=192,.sh=128,.dx=x+240,.dy=y+64,.dw=192,.dh=128};
    if(draw(&window.drawable,&gc,5,5,local,NULL) || ps2_pipe_blit_rect(context,ch,0,&present)) goto out;
    backing=&fp;gc.miTranslate=FALSE;gc.fgPixel=0xffffff;
    RegionReset(&region,&(BoxRec){0,0,width,height});xRectangle panel={x+16,y+224,416,16};
    if(draw(d,&gc,0,1,&panel,NULL) || pause_scene(seconds)) goto out;
    status=0;
out:
    if(callback_failure || stopped || (context && ps2_pipe_context_error(context))) status=1;
    if(gc_created) gc.funcs->DestroyGC(&gc);
    if(server_gc_count) status=1;
    if(solid.installed || bridge.installed) if(!xs.CloseScreen(&xs)) status=1;
    if(region_ready) pixman_region_fini(&region);
    if(canvas) pipe_resource_reference(&canvas,NULL);
    if(context) context->destroy(context);
    if(screen) screen->destroy(screen);
    if(fb>=0) close(fb);
    if(graphics && ioctl(tty,KDSETMODE,KD_TEXT)) status=1;
    if(tty>=0) close(tty);
    if(!status) puts("ps2-gs: thin dash GC command/fence PASSED; dash pixels require visual qualification");
    else fprintf(stderr,"ps2-gs: thin dash GC probe FAILED: %s\n",strerror(errno?errno:EIO));
    return status;
}
