/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "ps2_transform.h"
#undef ARRAY_SIZE
#include "ps2_copy_exa.h"
#include "ps2_context_bridge.h"
#include "util/u_inlines.h"
#undef ARRAY_SIZE
#include "pixmapstr.h"
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

/* Actual Mesa resource/MVP/GS and Xorg EXA copy callbacks with minimal server
 * bookkeeping. No X server, shader execution or CPU destination renderer. */
static unsigned private_slots;
static PixmapPtr front;
static jmp_buf abort_callback;
static volatile sig_atomic_t stopped;
static void interrupt(int sig){(void)sig;stopped=1;}
Bool dixRegisterPrivateKey(DevPrivateKey key,DevPrivateType type,unsigned size)
{
    if(type!=PRIVATE_SCREEN || size)return FALSE;
    if(!key->initialized){
        if(private_slots==16)return FALSE;
        *key=(DevPrivateKeyRec){.initialized=TRUE,.type=type,.offset=private_slots++*sizeof(void*)};
    }
    return TRUE;
}
void *exaGetPixmapDriverPrivate(PixmapPtr p){return p->devPrivates;}
void exaMarkSync(ScreenPtr s){(void)s;}
void FatalError(const char *fmt,...)
{
    va_list args;va_start(args,fmt);vfprintf(stderr,fmt,args);va_end(args);
    longjmp(abort_callback,1);
}
static PixmapPtr get_front(ScreenPtr s){(void)s;return front;}
static int present(struct ps2_surface_exa *exa,PixmapPtr source,PixmapPtr destination,int x,int y)
{
    if(setjmp(abort_callback))return -1;
    if(!exa->driver.PrepareCopy(source,destination,1,1,GXcopy,0xffffff)){
        fputs("ps2-gs: Xorg PrepareCopy rejected presentation\n",stderr);return -1;
    }
    exa->driver.Copy(destination,0,0,x,y,192,128);
    exa->driver.DoneCopy(destination);return 0;
}
static int pause_scene(unsigned seconds)
{
    struct timespec delay={.tv_sec=seconds};
    while(!stopped && nanosleep(&delay,&delay))if(errno!=EINTR)return -1;
    return stopped?-1:0;
}
static const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
static const struct ps2_transform_viewport viewport={0,0,192,128,0,1};
static void vertex(struct ps2_transform_vertex *v,float x,float y,float z,const float color[4])
{*v=(struct ps2_transform_vertex){.position={x,y,z,1}};memcpy(v->color,color,16);}
static int transformed(struct pipe_context *ctx,struct ps2_transform_vertex *v,unsigned n,const float m[16])
{return ps2_pipe_transform_triangles(ctx,v,n,m,&viewport,1,NULL);}
static int reference(struct pipe_context *ctx,const float p[3][4],const float color[3][4])
{return ps2_pipe_triangle(ctx,p,color,NULL,1);}
static int scene_triangle(struct pipe_context *ctx,unsigned side)
{
    const float colors[3][4]={{1,0,0,1},{0,1,0,1},{0,0,1,1}};
    if(!side){
        const float m[16]={.6f,-.25f,0,.05f,.25f,.6f,0,-.05f,0,0,1,0,0,0,0,1};
        struct ps2_transform_vertex v[3];
        vertex(v,-.75f,-.65f,0,colors[0]);vertex(v+1,.75f,-.65f,0,colors[1]);vertex(v+2,0,.8f,0,colors[2]);
        return transformed(ctx,v,3,m);
    }
    /* Independently evaluated matrix above, expressed in window coordinates. */
    const float p[3][4]={{73.2f,104.16f,.5f,1},{159.6f,80.16f,.5f,1},{81.6f,36.48f,.5f,1}};
    return reference(ctx,p,colors);
}
static int scene_depth(struct pipe_context *ctx,unsigned side)
{
    const float colors[3][4]={{0,1,1,1},{1,0,0,1},{1,1,0,1}};
    const float sizes[3]={.6f,.9f,.35f},depths[3]={-.5f,.5f,.8f};
    struct ps2_transform_vertex v[9];
    for(unsigned i=0;i<3;++i){unsigned j=side?2-i:i;float s=sizes[j];
        vertex(v+3*i,-s,-s,depths[j],colors[j]);vertex(v+3*i+1,s,-s,depths[j],colors[j]);vertex(v+3*i+2,0,s,depths[j],colors[j]);
    }
    return transformed(ctx,v,9,identity);
}
static int scene_clip(struct pipe_context *ctx,unsigned side)
{
    const float yellow[4]={1,1,0,1};
    if(!side){struct ps2_transform_vertex v[3];
        vertex(v,-2,0,-2,yellow);vertex(v+1,0,1,0,yellow);vertex(v+2,0,-1,0,yellow);
        return transformed(ctx,v,3,identity);
    }
    const float p[4][4]={{0,32,0,1},{96,0,.5f,1},{96,128,.5f,1},{0,96,0,1}};
    float triangle[3][4],colors[3][4];
    for(unsigned i=1;i<3;++i){unsigned index[3]={0,i,i+1};
        for(unsigned j=0;j<3;++j){memcpy(triangle[j],p[index[j]],16);memcpy(colors[j],yellow,16);}
        if(reference(ctx,triangle,colors))return -1;
    }
    return 0;
}
static int scene_cube(struct pipe_context *ctx)
{
    /* Projection near=.5, far=6 after x/y rotations and a camera at z=3.
     * Clip w=-camera.z; faces have constant opaque colours. */
    const float m[16]={.9208f,0,.6447f,0,.2673f,1.2747f,-.3817f,0,
        .5704f,-.5072f,-.8147f,2.454545f,.4825f,-.4295f,-.6892f,3};
    const float corners[8][3]={{-.65f,-.65f,-.65f},{.65f,-.65f,-.65f},{.65f,.65f,-.65f},{-.65f,.65f,-.65f},
        {-.65f,-.65f,.65f},{.65f,-.65f,.65f},{.65f,.65f,.65f},{-.65f,.65f,.65f}};
    const unsigned faces[6][4]={{0,1,2,3},{4,7,6,5},{0,4,5,1},{3,2,6,7},{0,3,7,4},{1,5,6,2}};
    const float colors[6][4]={{1,0,0,1},{0,1,1,1},{1,0,1,1},{1,1,0,1},{0,0,1,1},{0,1,0,1}};
    struct ps2_transform_vertex v[36];
    for(unsigned f=0;f<6;++f)for(unsigned i=0;i<6;++i){const unsigned indices[6]={0,1,2,0,2,3};
        const float *p=corners[faces[f][indices[i]]];vertex(v+6*f+i,p[0],p[1],p[2],colors[f]);
    }
    return transformed(ctx,v,36,m);
}
int main(int argc,char **argv)
{
    unsigned seconds=10;
    if(argc==2 && !strcmp(argv[1],"--brief"))seconds=1;
    else if(argc!=1){fputs("usage: ps2-gs-transform-test [--brief]\n",stderr);return 2;}
    stopped=0;
    struct sigaction action={.sa_handler=interrupt};sigemptyset(&action.sa_mask);
    if(sigaction(SIGINT,&action,NULL) || sigaction(SIGTERM,&action,NULL) || sigaction(SIGHUP,&action,NULL))return 1;
    int tty=-1,fb=-1,graphics=0,status=1;
    struct pipe_screen *screen=NULL;struct pipe_context *context=NULL;
    struct pipe_resource *color[2]={0},*depth[2]={0};
    struct pipe_surface *cs[2]={0},*zs[2]={0};
    struct ps2_surface_exa exa={0};struct ps2_exa_context_bridge bridge={0};struct ps2_copy_exa copy={0};
    void *privates[16]={0};ScreenRec xs={.GetScreenPixmap=get_front,.devPrivates=(void*)privates};
    PixmapRec fp={0},cp[2]={0};uint32_t handles[2];front=&fp;
    tty=open("/dev/tty0",O_RDWR|O_CLOEXEC);int mode;
    if(tty<0 || ioctl(tty,KDGETMODE,&mode) || mode!=KD_TEXT || ioctl(tty,KDSETMODE,KD_GRAPHICS))goto out;
    graphics=1;fb=open("/dev/fb0",O_RDWR|O_CLOEXEC);struct fb_var_screeninfo var;
    if(fb<0 || ioctl(fb,FBIOGET_VSCREENINFO,&var))goto out;
    var.xoffset=var.yoffset=0;var.vmode&=~FB_VMODE_YWRAP;
    if(ioctl(fb,FBIOPAN_DISPLAY,&var))goto out;
    screen=ps2_pipe_screen_create("/dev/ps2-gs");
    if(!screen || !(context=screen->context_create(screen,NULL,0)))goto out;
    struct ps2_render_client *render=ps2_pipe_context_render(context);struct ps2_surface_client *gs=render->gs;
    unsigned width=gs->caps.width,height=gs->caps.height;
    if(gs->caps.bits_per_pixel!=32 || width<448 || height<288){errno=ENOTSUP;goto out;}
    printf("ps2-gs: native transformed triangles / Xorg presentation %ux%u features=%x\n",width,height,gs->caps.features);fflush(stdout);
    fp.drawable=(DrawableRec){.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=24,.bitsPerPixel=32,.width=width,.height=height};
    for(unsigned i=0;i<2;++i){
        struct pipe_resource t={.target=PIPE_TEXTURE_2D,.format=PIPE_FORMAT_R8G8B8X8_UNORM,
            .width0=192,.height0=128,.depth0=1,.array_size=1,.bind=PIPE_BIND_RENDER_TARGET};
        color[i]=screen->resource_create(screen,&t);t.format=PIPE_FORMAT_Z16_UNORM;t.bind=PIPE_BIND_DEPTH_STENCIL;
        depth[i]=screen->resource_create(screen,&t);
        if(!color[i] || !depth[i])goto out;
        struct pipe_surface s={.format=color[i]->format};cs[i]=context->create_surface(context,color[i],&s);
        s.format=depth[i]->format;zs[i]=context->create_surface(context,depth[i],&s);
        if(!cs[i] || !zs[i])goto out;
        handles[i]=((struct ps2_resource*)color[i])->handle;
        cp[i].drawable=(DrawableRec){.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=24,.bitsPerPixel=32,.width=192,.height=128};
        cp[i].devPrivates=(void*)&handles[i];
    }
    if(!ps2_surface_exa_prepare(&xs,&exa,gs) || !ps2_exa_context_bridge_prepare(&xs,&bridge,&exa,context) ||
        !ps2_copy_exa_prepare(&xs,&copy,&exa,context))goto out;
    const char *descriptions[]={"matching rotated RGB shaded triangles",
        "matching cyan near triangles over red far triangles, no yellow; opposite submission orders; green bar",
        "matching yellow clipped shapes; left/near clip planes versus independent window reference",
        "matching angled perspective colour cubes with hidden faces removed by Z16; white bar"};
    int x=(width-416)/2,y=(height-192)/2;
    for(unsigned scene=0;scene<4;++scene){
        if(ps2_surface_fill(gs,0,0,0,width,height,0x80200c06))goto out;
        printf("ps2-gs: transform scene %u/4: %s\n",scene+1,descriptions[scene]);fflush(stdout);
        for(unsigned side=0;side<2;++side){
            struct pipe_framebuffer_state fbstate={.width=192,.height=128,.nr_cbufs=1,.cbufs={cs[side]},.zsbuf=zs[side]};
            context->set_framebuffer_state(context,&fbstate);
            union pipe_color_union navy={.f={6.0f/255,12.0f/255,32.0f/255,1}};
            context->clear(context,PIPE_CLEAR_COLOR0|PIPE_CLEAR_DEPTH,NULL,&navy,1,0);
            if(ps2_pipe_context_error(context) ||
               (scene==0?scene_triangle(context,side):scene==1?scene_depth(context,side):
                scene==2?scene_clip(context,side):scene_cube(context)))goto out;
            /* Scene reference triangles remain queued here; actual Xorg Copy
             * must fence them before GS local-to-local presentation. */
            if(present(&exa,&cp[side],&fp,x+224*side,y))goto out;
        }
        if((scene==1 || scene==3) && ps2_surface_fill(gs,0,x,y+160,416,16,scene==1?0x8000ff00:0x80ffffff))goto out;
        if(pause_scene(seconds))goto out;
    }
    status=0;
out:
    if(stopped || (context && ps2_pipe_context_error(context)))status=1;
    if(setjmp(abort_callback))return 1;
    ps2_copy_exa_cleanup(&xs,&copy);ps2_exa_context_bridge_cleanup(&bridge);
    for(unsigned i=0;i<2;++i){pipe_surface_reference(&cs[i],NULL);pipe_surface_reference(&zs[i],NULL);
        pipe_resource_reference(&color[i],NULL);pipe_resource_reference(&depth[i],NULL);}
    if(context)context->destroy(context);
    if(screen)screen->destroy(screen);
    if(fb>=0)close(fb);
    if(graphics && ioctl(tty,KDSETMODE,KD_TEXT))status=1;
    if(tty>=0)close(tty);
    if(!status)puts("ps2-gs: native transformed triangle command/depth/presentation PASSED; pixels require visual qualification");
    else fprintf(stderr,"ps2-gs: native transformed triangle probe FAILED: %s\n",strerror(errno?errno:EIO));
    return status;
}
