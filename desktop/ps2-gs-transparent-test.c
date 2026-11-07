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
    else if(argc!=1) { fputs("usage: ps2-gs-transparent-test [--brief]\n",stderr);return 2; }
    stopped=0;callback_failure=0;
    struct sigaction action={.sa_handler=interrupt};sigemptyset(&action.sa_mask);
    if(sigaction(SIGINT,&action,NULL) || sigaction(SIGTERM,&action,NULL) || sigaction(SIGHUP,&action,NULL)) return 1;
    int tty=-1,fb=-1,graphics=0,region_ready=0,gc_created=0,status=1;
    struct pipe_screen *screen=NULL;struct pipe_context *context=NULL;
    struct pipe_resource *canvas=NULL;
    struct ps2_surface_exa exa={0};struct ps2_gc_screen bridge={0};
    struct ps2_solid_screen solid={0};struct ps2_fill_screen fill={0};
    void *screen_privates[16]={0},*gc_privates[16]={0};
    ScreenRec xs={.GetScreenPixmap=get_front,.GetWindowPixmap=get_window,.CreateGC=create_gc,
        .CloseScreen=close_screen,.devPrivates=(void*)screen_privates};
    PixmapRec fp={0},cp={0},tp={0},bp={0};WindowRec window={0};
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
    printf("ps2-gs: transparent stipple GC / shared Mesa %ux%u features=%x\n",width,height,gs->caps.features);fflush(stdout);
    struct pipe_resource t={.target=PIPE_TEXTURE_2D,.format=PIPE_FORMAT_R8G8B8X8_UNORM,
        .width0=192,.height0=128,.depth0=1,.array_size=1,.bind=PIPE_BIND_RENDER_TARGET};
    canvas=screen->resource_create(screen,&t);if(!canvas) goto out;
    fp.drawable=(DrawableRec){.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=24,.bitsPerPixel=32,.width=width,.height=height};
    cp.drawable=fp.drawable;cp.drawable.width=192;cp.drawable.height=128;
    ch=((struct ps2_resource*)canvas)->handle;cp.devPrivates=(void*)&ch;
    unsigned char checker_bits[32*4]={0},stripe_bits[15*4]={0};
    for(unsigned j=0;j<32;++j) for(unsigned i=0;i<32;++i)
        if((i/16+j/16)&1) checker_bits[j*4+i/8]|=1U<<(i%8);
    for(unsigned j=0;j<15;++j) for(unsigned i=0;i<21;++i)
        if((i/7+j/5)&1) stripe_bits[j*4+i/8]|=1U<<(i%8);
    tp=(PixmapRec){.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.width=32,.height=32,.depth=1,.bitsPerPixel=1},
        .devKind=4,.devPrivate={.ptr=checker_bits}};
    bp=(PixmapRec){.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.width=21,.height=15,.depth=1,.bitsPerPixel=1},
        .devKind=4,.devPrivate={.ptr=stripe_bits}};
#ifdef COMPOSITE
    cp.screen_x=100;cp.screen_y=100;
#endif
    if(!ps2_surface_exa_prepare(&xs,&exa,gs)) goto out;
    PixmapPtr patterns[2]={&tp,&bp};
    for(unsigned i=0;i<2;++i) {
        PixmapPtr p=patterns[i];const void *bits=p->devPrivate.ptr;int old_pitch=p->devKind,pitch=0;
        p->devPrivates=exa.driver.CreatePixmap2(&xs,p->drawable.width,p->drawable.height,1,0,1,&pitch);
        if(!p->devPrivates || !exa.driver.ModifyPixmapHeader(p,0,0,1,1,pitch,NULL)) goto out;
        p->devPrivate.ptr=NULL;
        if(!exa.driver.PrepareAccess(p,EXA_PREPARE_DEST)) goto out;
        for(unsigned j=0;j<p->drawable.height;++j)
            memcpy((unsigned char*)p->devPrivate.ptr+j*pitch,(const unsigned char*)bits+j*old_pitch,(p->drawable.width+7)/8);
        exa.driver.FinishAccess(p,EXA_PREPARE_DEST);
        struct ps2_mono_source source;
        if(p->devPrivate.ptr || ps2_surface_exa_mono_source(p,&source)!=1) goto out;
    }
    puts("ps2-gs: two driver-owned monochrome sources created and hidden; color patterns stay on GS");fflush(stdout);
    pixman_region_init_rect(&region,0,0,width,height);region_ready=1;
    gc=(GC){.pScreen=&xs,.alu=GXcopy,.fillStyle=FillStippled,.stipple=&tp,.fgPixel=0xffffff,.bgPixel=0xff3300,
        .planemask=0xffffff,.pCompositeClip=&region,.devPrivates=(void*)gc_privates};
    if(!ps2_gc_screen_prepare(&xs,&bridge,&exa,context) || !ps2_solid_screen_prepare(&xs,&solid,&bridge) ||
        !ps2_fill_screen_prepare(&xs,&fill,&bridge) || !xs.CreateGC(&gc)) goto out;
    gc_created=1;DrawablePtr d=&fp.drawable;
    /* Only bitmap source bits are stored in main RAM. Every colour fill,
     * Boolean operation and presentation below executes through GS. */
    int x=(width-448)/2,y=(height-288)/2;
    xRectangle panel={x+16,y+64,192,128};
    puts("ps2-gs: transparent scene 1/4: matching red/white rectangle and span panels; zero bits retain red");fflush(stdout);
    if(ps2_surface_fill(gs,0,0,0,width,height,0x80200c06) ||
       ps2_surface_fill(gs,0,x+16,y+64,192,128,0x800000ff) ||
       ps2_surface_fill(gs,0,x+240,y+64,192,128,0x800000ff)) goto out;
    gc.patOrg=(DDXPointRec){x+16,y+64};
    if(draw(d,&gc,0,1,&panel,NULL)) goto out;
    DDXPointRec points[128];int widths[128];
    for(int j=0;j<128;++j) { points[j]=(DDXPointRec){x+240,y+64+j};widths[j]=192; }
    gc.patOrg=(DDXPointRec){x+240,y+64};
    if(draw(d,&gc,4,128,points,widths) || pause_scene(seconds)) goto out;
    puts("ps2-gs: transparent scene 2/4: shifted blue/cyan checker; central gap retains red");fflush(stdout);
    if(ps2_surface_fill(gs,0,0,0,width,height,0x80200c06) ||
       ps2_surface_fill(gs,0,x+32,y+64,384,128,0x80ff0000) ||
       ps2_surface_fill(gs,0,x+218,y+64,12,128,0x800000ff)) goto out;
    pixman_box16_t clips[]={{x+32,y+64,x+218,y+192},{x+230,y+64,x+416,y+192}};
    RegionUninit(&region);if(!pixman_region_init_rects(&region,clips,2)) { region_ready=0;goto out; }
    panel=(xRectangle){x+32,y+64,384,128};gc.patOrg=(DDXPointRec){x+39,y+69};gc.fgPixel=0xffff00;
    if(draw(d,&gc,0,1,&panel,NULL) || pause_scene(seconds)) goto out;
    puts("ps2-gs: transparent scene 3/4: matching cyan/white partial-mask patterns; XOR twice restores yellow bar");fflush(stdout);
    if(ps2_surface_fill(gs,0,0,0,width,height,0x80200c06) ||
       ps2_surface_fill(gs,0,x+16,y+64,192,128,0x80ffff00) ||
       ps2_surface_fill(gs,0,x+240,y+64,192,128,0x80ffff00) ||
       ps2_surface_fill(gs,0,x+16,y+224,416,16,0x8000ffff)) goto out;
    RegionReset(&region,&(BoxRec){0,0,width,height});gc.stipple=&bp;gc.fgPixel=0xffffff;gc.planemask=0xff;
    panel=(xRectangle){x+16,y+64,192,128};gc.patOrg=(DDXPointRec){x+19,y+69};
    if(draw(d,&gc,0,1,&panel,NULL)) goto out;
    gc.patOrg=(DDXPointRec){x+243,y+69};
    if(draw(d,&gc,4,128,points,widths)) goto out;
    gc.stipple=&tp;gc.planemask=0xffffff;gc.alu=GXxor;gc.patOrg=(DDXPointRec){x+16,y+224};
    panel=(xRectangle){x+16,y+224,416,16};
    if(draw(d,&gc,0,1,&panel,NULL) || draw(d,&gc,0,1,&panel,NULL) || pause_scene(seconds)) goto out;
    puts("ps2-gs: transparent scene 4/4: matching orange/green front/backing panels with magenta completion bar");fflush(stdout);
    if(ps2_surface_fill(gs,0,0,0,width,height,0x80200c06) ||
       ps2_surface_fill(gs,0,x+16,y+64,192,128,0x800080ff) ||
       ps2_surface_fill(gs,ch,0,0,192,128,0x800080ff)) goto out;
    gc.alu=GXcopy;gc.fgPixel=0x00ff00;panel=(xRectangle){x+16,y+64,192,128};gc.patOrg=(DDXPointRec){x+16,y+64};
    if(draw(d,&gc,0,1,&panel,NULL)) goto out;
    backing=&cp;int ox=0,oy=0;
#ifdef COMPOSITE
    ox=cp.screen_x;oy=cp.screen_y;
#endif
    window.drawable=(DrawableRec){.type=DRAWABLE_WINDOW,.pScreen=&xs,.depth=24,.bitsPerPixel=32,
        .x=ox,.y=oy,.width=192,.height=128};window.realized=TRUE;
    RegionReset(&region,&(BoxRec){ox,oy,ox+192,oy+128});gc.miTranslate=TRUE;gc.patOrg=(DDXPointRec){0,0};
    panel=(xRectangle){0,0,192,128};
    const struct ps2_pipe_blit_rect present={.sw=192,.sh=128,.dx=x+240,.dy=y+64,.dw=192,.dh=128};
    if(draw(&window.drawable,&gc,0,1,&panel,NULL) || ps2_pipe_blit_rect(context,ch,0,&present)) goto out;
    backing=&fp;gc.miTranslate=FALSE;gc.fillStyle=FillSolid;gc.fgPixel=0xff00ff;
    RegionReset(&region,&(BoxRec){0,0,width,height});panel=(xRectangle){x+16,y+224,416,16};
    if(draw(d,&gc,0,1,&panel,NULL) || pause_scene(seconds)) goto out;
    status=0;
out:
    if(callback_failure || stopped || (context && ps2_pipe_context_error(context))) status=1;
    if(gc_created) gc.funcs->DestroyGC(&gc);
    if(server_gc_count) status=1;
    if(fill.installed || solid.installed || bridge.installed) if(!xs.CloseScreen(&xs)) status=1;
    if(region_ready) pixman_region_fini(&region);
    if(tp.devPrivates) exa.driver.DestroyPixmap(&xs,tp.devPrivates);
    if(bp.devPrivates) exa.driver.DestroyPixmap(&xs,bp.devPrivates);
    if(exa.monochrome_count || exa.monochrome_bytes || exa.monochrome) status=1;
    if(canvas) pipe_resource_reference(&canvas,NULL);
    if(context) context->destroy(context);
    if(screen) screen->destroy(screen);
    if(fb>=0) close(fb);
    if(graphics && ioctl(tty,KDSETMODE,KD_TEXT)) status=1;
    if(tty>=0) close(tty);
    if(!status) puts("ps2-gs: transparent stipple GC command/fence PASSED; transparent pixels require visual qualification");
    else fprintf(stderr,"ps2-gs: transparent stipple GC probe FAILED: %s\n",strerror(errno?errno:EIO));
    return status;
}
