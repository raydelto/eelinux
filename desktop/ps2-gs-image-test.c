/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "ps2_pipe.h"
#include "ps2_gs_client.h"
#undef ARRAY_SIZE
#include "ps2_solid.h"
#include "ps2_cursor.h"
#include "ps2_cursor_screen.h"
#include "cursorstr.h"
#include "ps2_fill.h"
#include "ps2_image_gc.h"
#include "ps2_image.h"
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
#include <stdlib.h>
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
static miPointerSpriteFuncPtr cursor_sprites;
static CloseScreenProcPtr pointer_close_lower;
static struct { DamageReportFunc report;DamageDestroyFunc destroy;void *data; } cursor_damage;
static Bool pointer_close(ScreenPtr s){s->CloseScreen=pointer_close_lower;return s->CloseScreen(s);}
Bool miPointerInitialize(ScreenPtr s,miPointerSpriteFuncPtr sprites,miPointerScreenFuncPtr funcs,Bool wait)
{ (void)funcs;(void)wait;cursor_sprites=sprites;pointer_close_lower=s->CloseScreen;s->CloseScreen=pointer_close;return TRUE; }
DamagePtr DamageCreate(DamageReportFunc report,DamageDestroyFunc destroy,DamageReportLevel level,Bool internal,ScreenPtr s,void *data)
{ (void)level;(void)internal;(void)s;cursor_damage.report=report;cursor_damage.destroy=destroy;cursor_damage.data=data;return (DamagePtr)&cursor_damage; }
void DamageSetReportAfterOp(DamagePtr d,Bool after){(void)d;(void)after;}
void DamageRegister(DrawablePtr d,DamagePtr damage){(void)d;(void)damage;}
void DamageUnregister(DamagePtr damage){(void)damage;}
void DamageDestroy(DamagePtr damage){cursor_damage.destroy(damage,cursor_damage.data);cursor_damage.report=NULL;}
void DamageEmpty(DamagePtr damage){(void)damage;}
static void block(ScreenPtr s,void *timeout){(void)s;(void)timeout;}
static void best(int type,unsigned short *w,unsigned short *h,ScreenPtr s){(void)type;(void)w;(void)h;(void)s;}
static Bool offscreen(ScreenPtr *s,int *x,int *y){(void)s;(void)x;(void)y;return FALSE;}
static void cross_screen(ScreenPtr s,int entering){(void)s;(void)entering;}
static void warp(DeviceIntPtr d,ScreenPtr s,int x,int y){(void)d;(void)s;(void)x;(void)y;}
static miPointerScreenFuncRec pointer_funcs={offscreen,cross_screen,warp};
static void arrow_bits(unsigned char *source,unsigned char *mask)
{
    memset(source,0,96);memset(mask,0,96);
    for(unsigned y=0;y<24;++y)for(unsigned x=0;x<24;++x){
        int opaque=(y<18 && x<=y/2+1)||(y>=12 && x>=4 && x<=7);
        int foreground=(y<18 && x>0 && x<y/2+1)||(y>=12 && x>=5 && x<=6);
        unsigned bit=BITMAP_BIT_ORDER==MSBFirst?7-x%8:x%8;
        if(opaque)mask[y*4+x/8]|=1U<<bit;
        if(foreground)source[y*4+x/8]|=1U<<bit;
    }
}
static int cursor_action(ScreenPtr s,DeviceIntPtr d,CursorPtr cursor,int kind,int x,int y)
{
    if(setjmp(abort_callback))return -1;
    if(kind==0)cursor_sprites->SetCursor(d,s,cursor,x,y);
    else if(kind==1)cursor_sprites->MoveCursor(d,s,x,y);
    else if(kind==2){int timeout=0;s->BlockHandler(s,&timeout);}
    else if(kind==3){if(!cursor_sprites->UnrealizeCursor(d,s,cursor))return -1;}
    else cursor_sprites->DeviceCursorCleanup(d,s);
    return 0;
}
static int close_native(ScreenPtr s)
{ if(setjmp(abort_callback))return -1;return s->CloseScreen(s)?0:-1; }
static void image_source(uint32_t *pixels,unsigned w,unsigned h,unsigned kind)
{
    static const uint32_t colors[2][6]={{0xff,0xff00,0xff0000,0xffff,0xffff00,0xff00ff},
        {0xffff00,0xffff,0xff00ff,0xffffff,0xff00,0xff0000}};
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
        uint32_t color=kind==2?(((x/16+y/16)&1)?0xffff:0):colors[kind==4][x/32%6];
        if(kind!=2 && ((y/16)&1))color=(color&0xfefefe)>>1;
        pixels[y*w+x]=0x80000000|color;
    }
}
static int client_image(DrawablePtr d,GCPtr gc,uint32_t *pixels,int x,int y,unsigned w,unsigned h,int spans)
{
    if(setjmp(abort_callback))return -1;
    gc->funcs->ChangeGC(gc,GCFunction|GCPlaneMask);gc->funcs->ValidateGC(gc,GCAllBits,d);
    if(spans){DDXPointRec points[144];int widths[144];if(h>144)return -1;
        for(unsigned i=0;i<h;++i){points[i]=(DDXPointRec){x,y+i};widths[i]=w;}
        gc->ops->SetSpans(d,gc,(char*)pixels,points,widths,h,1);
    }else gc->ops->PutImage(d,gc,d->depth,x,y,w,h,0,ZPixmap,(char*)pixels);
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
    unsigned seconds=10;
    if(argc==2 && !strcmp(argv[1],"--brief")) seconds=1;
    else if(argc!=1) { fputs("usage: ps2-gs-image-test [--brief]\n",stderr);return 2; }
    stopped=0;callback_failure=0;
    struct sigaction action={.sa_handler=interrupt};sigemptyset(&action.sa_mask);
    if(sigaction(SIGINT,&action,NULL) || sigaction(SIGTERM,&action,NULL) || sigaction(SIGHUP,&action,NULL)) return 1;
    int tty=-1,fb=-1,graphics=0,region_ready=0,gc_created=0,status=1;
    struct pipe_screen *screen=NULL;struct pipe_context *context=NULL;
    struct pipe_resource *canvas=NULL;
    struct ps2_surface_exa exa={0};struct ps2_gc_screen bridge={0};
    struct ps2_solid_screen solid={0};struct ps2_cursor_screen cursor_owner={0};struct ps2_image_screen image_owner={0};
    uint32_t *client_pixels=NULL;
    void *screen_privates[16]={0},*gc_privates[16]={0};
    ScreenRec xs={.GetScreenPixmap=get_front,.GetWindowPixmap=get_window,.CreateGC=create_gc,
        .CloseScreen=close_screen,.BlockHandler=block,.QueryBestSize=best,.rootDepth=24,.devPrivates=(void*)screen_privates};
    PixmapRec fp={0},cp={0};
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
    printf("ps2-gs: native client image / shared Mesa %ux%u features=%x\n",width,height,gs->caps.features);fflush(stdout);
    struct pipe_resource t={.target=PIPE_TEXTURE_2D,.format=PIPE_FORMAT_R8G8B8X8_UNORM,
        .width0=192,.height0=128,.depth0=1,.array_size=1,.bind=PIPE_BIND_RENDER_TARGET};
    canvas=screen->resource_create(screen,&t);if(!canvas) goto out;
    fp.drawable=(DrawableRec){.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=24,.bitsPerPixel=32,.width=width,.height=height};
    ch=((struct ps2_resource*)canvas)->handle;
    cp.drawable=(DrawableRec){.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=24,.bitsPerPixel=32,.width=192,.height=128};cp.devPrivates=(void*)&ch;
    client_pixels=malloc(448*144*4);if(!client_pixels)goto out;
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
    /* Only monochrome source bits and reference geometry reside in CPU RAM.
     * Native GS copies save/restore backgrounds and draw colour destinations. */
    unsigned char source[96],mask[96];arrow_bits(source,mask);
    CursorBits bits={.source=source,.mask=mask,.width=24,.height=24};
    CursorRec cursor={.bits=&bits,.foreRed=65535,.foreGreen=65535,.foreBlue=65535};
    DeviceIntPtr pointer=(DeviceIntPtr)(uintptr_t)1;
    if(!ps2_image_screen_prepare(&xs,&image_owner,&bridge) ||
       !ps2_cursor_screen_prepare(&xs,&cursor_owner,context,&pointer_funcs) ||
       !cursor_sprites->DeviceCursorInitialize(pointer,&xs) || !cursor_sprites->RealizeCursor(pointer,&xs,&cursor))goto out;
    int x=(width-448)/2,y=(height-288)/2,l=x+16,r=x+240,top=y+64;
    puts("ps2-gs: image scene 1/4: matching six-colour shaded bars; PutImage on left and SetSpans on right");fflush(stdout);
    image_source(client_pixels,192,128,1);
    if(ps2_surface_fill(gs,0,0,0,width,height,0x80200c06) ||
       client_image(d,&gc,client_pixels,l,top,192,128,0) || client_image(d,&gc,client_pixels,r,top,192,128,1) || pause_scene(seconds))goto out;
    puts("ps2-gs: image scene 2/4: shifted shaded colour bars clipped into two panels with red central gap");fflush(stdout);
    gc.fgPixel=0xff;xRectangle base={l,top,416,128};if(draw(d,&gc,0,1,&base,NULL))goto out;
    pixman_region_clear(&region);pixman_region_union_rect(&region,&region,l,top,192,128);pixman_region_union_rect(&region,&region,r,top,192,128);
    image_source(client_pixels,448,144,1);
    if(client_image(d,&gc,client_pixels,l-11,top-7,448,144,0) || pause_scene(seconds))goto out;
    puts("ps2-gs: image scene 3/4: matching white/blue XOR checker panels with green completion bar");fflush(stdout);
    pixman_region_clear(&region);pixman_region_union_rect(&region,&region,0,0,width,height);
    if(ps2_surface_fill(gs,0,0,0,width,height,0x80200c06))goto out;
    gc.fgPixel=0xff0000;xRectangle panels[2]={{l,top,192,128},{r,top,192,128}};
    if(draw(d,&gc,0,2,panels,NULL))goto out;
    image_source(client_pixels,192,128,2);gc.alu=GXxor;
    if(client_image(d,&gc,client_pixels,l,top,192,128,0) || client_image(d,&gc,client_pixels,r,top,192,128,1))goto out;
    gc.alu=GXcopy;for(unsigned i=0;i<416*16;++i)client_pixels[i]=0x8000ff00;
    if(client_image(d,&gc,client_pixels,l,y+224,416,16,0) || pause_scene(seconds))goto out;
    puts("ps2-gs: image scene 4/4: matching front/backing colour bars after cursor-overdraw restoration; white completion bar");fflush(stdout);
    image_source(client_pixels,192,128,4);
    if(cursor_action(&xs,pointer,&cursor,0,r+64,top+48) || client_image(d,&gc,client_pixels,l,top,192,128,0))goto out;
    pixman_region_clear(&region);pixman_region_union_rect(&region,&region,0,0,192,128);
    if(client_image(&cp.drawable,&gc,client_pixels,0,0,192,128,0) || ps2_surface_copy(gs,ch,0,0,0,r,top,192,128))goto out;
    if(cursor_action(&xs,pointer,&cursor,2,0,0) || cursor_action(&xs,pointer,&cursor,3,0,0))goto out;
    pixman_region_clear(&region);pixman_region_union_rect(&region,&region,0,0,width,height);
    for(unsigned i=0;i<416*16;++i)client_pixels[i]=0x80ffffff;
    if(client_image(d,&gc,client_pixels,l,y+224,416,16,0) || pause_scene(seconds))goto out;
    if(cursor_action(&xs,pointer,&cursor,4,0,0))goto out;
    status=0;
out:
    if(callback_failure || stopped || (context && ps2_pipe_context_error(context))) status=1;
    if(gc_created) gc.funcs->DestroyGC(&gc);
    if(server_gc_count) status=1;
    if(cursor_owner.installed || image_owner.installed || solid.installed || bridge.installed) if(close_native(&xs)) status=1;
    if(region_ready) pixman_region_fini(&region);
    free(client_pixels);
    if(canvas) pipe_resource_reference(&canvas,NULL);
    if(context) context->destroy(context);
    if(screen) screen->destroy(screen);
    if(fb>=0) close(fb);
    if(graphics && ioctl(tty,KDSETMODE,KD_TEXT)) status=1;
    if(tty>=0) close(tty);
    if(!status) puts("ps2-gs: native client image command/fence PASSED; image pixels require visual qualification");
    else fprintf(stderr,"ps2-gs: native client image probe FAILED: %s\n",strerror(errno?errno:EIO));
    return status;
}
