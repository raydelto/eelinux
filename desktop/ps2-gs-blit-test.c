/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "ps2_pipe.h"
#include "util/u_inlines.h"
#undef ARRAY_SIZE
#include "ps2_blit_exa.h"
#include "ps2_context_bridge.h"
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

/* Minimal Xorg bookkeeping, with actual Gallium blit and EXA composite hooks.
 * No X server, GLX context, shaders or CPU destination rasterization. */
static unsigned private_slots;
static PixmapPtr front;
static int callback_failure;
static jmp_buf abort_callback;
static volatile sig_atomic_t stopped;
static void interrupt(int sig) { (void)sig; stopped=1; }
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
    va_list args;va_start(args,format);vfprintf(stderr,format,args);va_end(args);
    callback_failure=1;longjmp(abort_callback,1);
}
static PixmapPtr get_front(ScreenPtr screen) { (void)screen;return front; }
static int composite(struct ps2_surface_exa *exa,PixmapPtr source,PixmapPtr destination,
    int sx,int sy,int dx,int dy,int width,int height)
{
    PictureRec sp={.pDrawable=&source->drawable,.format=PICT_x8b8g8r8,.filter=PictFilterNearest};
    PictureRec dp={.pDrawable=&destination->drawable,.format=PICT_x8b8g8r8,.filter=PictFilterNearest};
    if(setjmp(abort_callback)) return -1;
    if(!exa->driver.CheckComposite(PictOpSrc,&sp,NULL,&dp) ||
        !exa->driver.PrepareComposite(PictOpSrc,&sp,NULL,&dp,source,NULL,destination)) {
        errno=ENOTSUP;return -1;
    }
    exa->driver.Composite(destination,sx,sy,0,0,dx,dy,width,height);
    exa->driver.DoneComposite(destination);return 0;
}
static int blit(struct pipe_context *context,struct pipe_resource *source,
    struct pipe_resource *destination,int sx,int sy,int sw,int sh,int dx,int dy,int dw,int dh,
    const struct pipe_scissor_state *clip)
{
    struct pipe_blit_info info={.src={.resource=source,.format=source->format,
        .box={.x=sx,.y=sy,.width=sw,.height=sh,.depth=1}},
        .dst={.resource=destination,.format=destination->format,
        .box={.x=dx,.y=dy,.width=dw,.height=dh,.depth=1}},
        .mask=PIPE_MASK_R|PIPE_MASK_G|PIPE_MASK_B,.filter=PIPE_TEX_FILTER_NEAREST};
    if(clip) { info.scissor_enable=true;info.scissor=*clip; }
    context->blit(context,&info);
    return ps2_pipe_context_error(context) ? -1 : 0;
}
static int pause_scene(unsigned seconds)
{
    struct timespec delay={.tv_sec=seconds};
    while(!stopped && nanosleep(&delay,&delay)) if(errno!=EINTR) return -1;
    return stopped ? -1 : 0;
}
static void clear(struct pipe_context *context)
{
    union pipe_color_union navy={.f={6.0f/255,12.0f/255,32.0f/255,1}};
    context->clear(context,PIPE_CLEAR_COLOR0,NULL,&navy,1,0);
}
int main(int argc,char **argv)
{
    unsigned seconds=3;
    if(argc==2 && !strcmp(argv[1],"--brief")) seconds=1;
    else if(argc!=1) { fputs("usage: ps2-gs-blit-test [--brief]\n",stderr);return 2; }
    struct sigaction action={.sa_handler=interrupt};sigemptyset(&action.sa_mask);
    if(sigaction(SIGINT,&action,NULL) || sigaction(SIGTERM,&action,NULL) || sigaction(SIGHUP,&action,NULL)) return 1;
    int tty=-1,fb=-1,graphics=0,status=1,mode;
    struct pipe_screen *screen=NULL;struct pipe_context *context=NULL;
    struct pipe_resource *source=NULL,*canvas=NULL;struct pipe_surface *surface=NULL;
    struct ps2_surface_exa exa={0};struct ps2_exa_context_bridge ordering={0};
    struct ps2_texture_exa texture={0};struct ps2_blit_exa native={0};
    void *privates[16]={0};ScreenRec xs={.GetScreenPixmap=get_front,.devPrivates=(void*)privates};
    PixmapRec front_pixmap={0},canvas_pixmap={0};uint32_t canvas_handle=0;
    front=&front_pixmap;
    tty=open("/dev/tty0",O_RDWR|O_CLOEXEC);
    if(tty<0 || ioctl(tty,KDGETMODE,&mode) || mode!=KD_TEXT || ioctl(tty,KDSETMODE,KD_GRAPHICS)) goto out;
    graphics=1;fb=open("/dev/fb0",O_RDWR|O_CLOEXEC);struct fb_var_screeninfo var;
    if(fb<0 || ioctl(fb,FBIOGET_VSCREENINFO,&var)) goto out;
    var.xoffset=var.yoffset=0;var.vmode&=~FB_VMODE_YWRAP;
    if(ioctl(fb,FBIOPAN_DISPLAY,&var)) goto out;
    screen=ps2_pipe_screen_create("/dev/ps2-gs");
    if(!screen || !(context=screen->context_create(screen,NULL,0))) goto out;
    struct ps2_render_client *render=ps2_pipe_context_render(context);
    struct ps2_surface_client *gs=render->gs;
    unsigned width=gs->caps.width,height=gs->caps.height;
    if(gs->caps.bits_per_pixel!=32 || width<448 || height<288) { errno=ENOTSUP;goto out; }
    printf("ps2-gs: Gallium blit / EXA composite %ux%u features=%x\n",width,height,gs->caps.features);fflush(stdout);
    struct pipe_resource template={.target=PIPE_TEXTURE_2D,.format=PIPE_FORMAT_R8G8B8X8_UNORM,
        .width0=33,.height0=21,.depth0=1,.array_size=1,.bind=PIPE_BIND_SAMPLER_VIEW};
    source=screen->resource_create(screen,&template);
    template.width0=448;template.height0=288;template.bind=PIPE_BIND_RENDER_TARGET;
    canvas=screen->resource_create(screen,&template);
    if(!source || !canvas) goto out;
    struct pipe_surface st={.format=canvas->format};surface=context->create_surface(context,canvas,&st);
    if(!surface) goto out;
    struct pipe_framebuffer_state frame={.width=448,.height=288,.nr_cbufs=1,.cbufs={surface}};
    context->set_framebuffer_state(context,&frame);
    front_pixmap.drawable=(DrawableRec){.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=24,.bitsPerPixel=32,.width=width,.height=height};
    canvas_pixmap.drawable=(DrawableRec){.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=24,.bitsPerPixel=32,.width=448,.height=288};
    canvas_handle=((struct ps2_resource*)canvas)->handle;canvas_pixmap.devPrivates=(void*)&canvas_handle;
    if(!ps2_surface_exa_prepare(&xs,&exa,gs) ||
        !ps2_exa_context_bridge_prepare(&xs,&ordering,&exa,context) ||
        !ps2_texture_exa_prepare(&xs,&texture,&exa,render) ||
        !ps2_blit_exa_prepare(&xs,&native,&exa,context)) goto out;
    uint32_t pixels[33*21];static const uint32_t bars[]={0x800000ff,0x8000ff00,0x80ff0000,0x80ffffff};
    for(unsigned y=0;y<21;++y) for(unsigned x=0;x<33;++x)
        pixels[y*33+x]=y<10 ? bars[x*4/33] : (((x/4+(y-10)/4)&1) ? 0x80ffffff : 0x80000000);
    struct pipe_box upload={.width=33,.height=21,.depth=1};
    context->texture_subdata(context,source,0,0,&upload,pixels,33*4,0);
    if(ps2_pipe_context_error(context) || ps2_surface_fill(gs,0,0,0,width,height,0x80200c06)) goto out;
    int x=(width-448)/2,y=(height-288)/2;
    puts("ps2-gs: blit scene 1/4: large, medium and original-size nearest copies");fflush(stdout);
    clear(context);
    if(blit(context,source,canvas,0,0,33,21,24,56,165,105,NULL) ||
        blit(context,source,canvas,0,0,33,21,280,56,99,63,NULL) ||
        blit(context,source,canvas,0,0,33,21,313,155,33,21,NULL) ||
        composite(&exa,&canvas_pixmap,&front_pixmap,0,0,x,y,448,288) || pause_scene(seconds)) goto out;
    puts("ps2-gs: blit scene 2/4: normal, horizontal, vertical, both flips");fflush(stdout);
    clear(context);
    if(blit(context,source,canvas,0,0,33,21,24,32,132,84,NULL) ||
        blit(context,source,canvas,33,0,-33,21,248,32,132,84,NULL) ||
        blit(context,source,canvas,0,21,33,-21,24,164,132,84,NULL) ||
        blit(context,source,canvas,33,21,-33,-21,248,164,132,84,NULL) ||
        composite(&exa,&canvas_pixmap,&front_pixmap,0,0,x,y,448,288) || pause_scene(seconds)) goto out;
    puts("ps2-gs: blit scene 3/4: full left panel, central crop of matching right panel");fflush(stdout);
    clear(context);struct pipe_scissor_state clip={.minx=281,.miny=101,.maxx=380,.maxy=164};
    if(blit(context,source,canvas,0,0,33,21,24,80,165,105,NULL) ||
        blit(context,source,canvas,0,0,33,21,248,80,165,105,&clip) ||
        composite(&exa,&canvas_pixmap,&front_pixmap,0,0,x,y,448,288) || pause_scene(seconds)) goto out;
    puts("ps2-gs: blit scene 4/4: identical odd-width copies, then overlapping EXA move");fflush(stdout);
    clear(context);
    if(blit(context,source,canvas,0,0,33,21,24,80,129,65,NULL)) goto out;
    struct pipe_box copy={.x=24,.y=80,.width=129,.height=65,.depth=1};
    context->resource_copy_region(context,canvas,0,248,80,0,canvas,0,&copy);
    if(ps2_pipe_context_error(context) ||
        composite(&exa,&canvas_pixmap,&front_pixmap,0,0,x,y,448,288) || pause_scene(seconds) ||
        composite(&exa,&front_pixmap,&front_pixmap,x+248,y+80,x+255,y+85,129,65) || pause_scene(seconds)) goto out;
    status=0;
out:
    if(context && ps2_pipe_context_error(context)) { errno=ps2_pipe_context_error(context);status=1; }
    if(callback_failure) status=1;
    if(status && !stopped && !callback_failure) perror("ps2-gs: Gallium blit / EXA composite probe");
    ps2_blit_exa_cleanup(&xs,&native);ps2_exa_context_bridge_cleanup(&ordering);
    if(context) context->destroy(context);
    pipe_surface_reference(&surface,NULL);pipe_resource_reference(&canvas,NULL);pipe_resource_reference(&source,NULL);
    if(screen) screen->destroy(screen);
    if(fb>=0) close(fb);
    if(graphics && ioctl(tty,KDSETMODE,KD_TEXT)) status=1;
    if(tty>=0) close(tty);
    if(!status) puts("ps2-gs: blit/composite command/fence PASSED; visual pixels require qualification; console restored");
    return status;
}
