/* SPDX-License-Identifier: MIT */
/* Native core-X DDX: no colour framebuffer mapping or shadow rendering. */
#define _POSIX_C_SOURCE 200809L
#include "xorg-server.h"
#include "ps2_pipe.h"
#undef ARRAY_SIZE
#include "xf86.h"
#include "xf86_OSproc.h"
#include "xf86Module.h"
#include "xf86Modes.h"
#include "fb.h"
#include "micmap.h"
#include "mipointer.h"
#include "damage.h"
#include "ps2_context_bridge.h"
#include "ps2_logic_exa.h"
#include "ps2_copy_exa.h"
#include "ps2_blit_exa.h"
#include "ps2_alpha_map_screen.h"
#include "ps2_render_geometry.h"
#include "ps2_texture_exa.h"
#include "ps2_copy.h"
#include "ps2_copy_window.h"
#include "ps2_paint_window.h"
#include "ps2_solid.h"
#include "ps2_fill.h"
#include "ps2_image_gc.h"
#include "ps2_polygon_gc.h"
#include "ps2_geometry.h"
#include "ps2_readback.h"
#include "ps2_cursor_screen.h"
#include "ps2-gs-map-blit.h"
#include "resource.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
typedef struct {
    int fb;
    struct fb_var_screeninfo mode;
    struct pipe_screen *pipe;
    struct pipe_context *context;
    struct ps2_surface_exa exa;
    struct ps2_exa_context_bridge bridge;
    struct ps2_logic_exa logic;
    struct ps2_copy_exa copy;
    struct ps2_texture_exa texture;
    struct ps2_blit_exa blit;
    struct ps2_alpha_map_screen alpha_map;
    struct ps2_render_geometry render_geometry;
    struct ps2_copy_window windows;
    struct ps2_paint_window paint;
    struct ps2_gc_screen gc;
    struct ps2_solid_screen solid;
    struct ps2_copy_screen copies;
    struct ps2_fill_screen fill;
    struct ps2_image_screen images;
    struct ps2_polygon_screen polygons;
    struct ps2_geometry_screen geometry;
    struct ps2_cursor_screen cursor;
    CloseScreenProcPtr close;
    CreateScreenResourcesProcPtr resources;
    ChangeWindowAttributesProcPtr attributes;
    GetImageProcPtr bitmap_get_image;
    GetSpansProcPtr bitmap_get_spans;
    BitmapToRegionProcPtr bitmap_to_region;
    Bool initialized;
} PS2Rec;
static Bool preinit(ScrnInfoPtr,int);
static Bool screen_init(ScreenPtr,int,char**);
static Bool probe(DriverPtr,int);
static Bool close_screen(ScreenPtr);
static Bool enter_vt(ScrnInfoPtr);
static void leave_vt(ScrnInfoPtr);
static Bool switch_mode(ScrnInfoPtr,DisplayModePtr);
static void adjust(ScrnInfoPtr,int,int);
static ModeStatus valid_mode(ScrnInfoPtr,DisplayModePtr,Bool,int);
static void free_screen(ScrnInfoPtr);
static Bool driver_func(ScrnInfoPtr,xorgDriverFuncOp,void*);
static void identify(int flags){(void)flags;xf86Msg(X_INFO,"PS2 native GS core-X driver\n");}
static const OptionInfoRec options[]={{-1,NULL,OPTV_NONE,{0},FALSE}};
static const OptionInfoRec *available(int chip,int bus){(void)chip;(void)bus;return options;}
_X_EXPORT DriverRec PS2={.driverVersion=1000,.driverName="ps2",.Identify=identify,.Probe=probe,
    .AvailableOptions=available,.driverFunc=driver_func};
static XF86ModuleVersionInfo version={"ps2",MODULEVENDORSTRING,MODINFOSTRING1,MODINFOSTRING2,
    XORG_VERSION_CURRENT,1,0,0,ABI_CLASS_VIDEODRV,ABI_VIDEODRV_VERSION,MOD_CLASS_VIDEODRV,{0,0,0,0}};
static void *setup(void *module,void *opts,int *major,int *minor)
{
    static Bool loaded=FALSE;(void)opts;(void)minor;
    if(loaded){if(major)*major=LDR_ONCEONLY;return NULL;}
    loaded=TRUE;xf86AddDriver(&PS2,module,HaveDriverFuncs);return (void*)1;
}
_X_EXPORT XF86ModuleData ps2ModuleData={&version,setup,NULL};
static Bool driver_func(ScrnInfoPtr s,xorgDriverFuncOp op,void *data)
{(void)s;if(op!=GET_REQUIRED_HW_INTERFACES)return FALSE;*(CARD32*)data=0;return TRUE;}
static Bool probe(DriverPtr driver,int flags)
{
    if(flags&PROBE_DETECT)return FALSE;
    GDevPtr *devices=NULL;int n=xf86MatchDevice("ps2",&devices);Bool found=FALSE;
    for(int i=0;i<n;++i){int entity=xf86ClaimFbSlot(driver,0,devices[i],TRUE);
        ScrnInfoPtr s=xf86ConfigFbEntity(NULL,0,entity,NULL,NULL,NULL,NULL);
        if(!s)continue;
        s->driverVersion=1000;s->driverName="ps2";s->name="PS2";s->Probe=probe;s->PreInit=preinit;
        s->ScreenInit=screen_init;s->SwitchMode=switch_mode;s->AdjustFrame=adjust;
        s->EnterVT=enter_vt;s->LeaveVT=leave_vt;s->ValidMode=valid_mode;s->FreeScreen=free_screen;found=TRUE;
    }
    free(devices);return found;
}
static Bool preinit(ScrnInfoPtr s,int flags)
{
    if(flags&PROBE_DETECT || s->numEntities!=1 || s->driverPrivate)return FALSE;
    PS2Rec *p=calloc(1,sizeof(*p));if(!p)return FALSE;s->driverPrivate=p;p->fb=-1;
    const char *stage="open /dev/fb0";
    p->fb=open("/dev/fb0",O_RDWR|O_CLOEXEC);
    if(p->fb<0)goto fail;
    stage="read framebuffer mode";
    if(ioctl(p->fb,FBIOGET_VSCREENINFO,&p->mode))goto fail;
    /* A direct Xorg/startx invocation must also accept the console's native
     * 32-bit mode. Select RGB555 storage without altering display timing. */
    if(p->mode.bits_per_pixel!=16){
        stage="select native RGB555 framebuffer storage";
        p->mode.bits_per_pixel=16;p->mode.activate=FB_ACTIVATE_NOW;
        if(ioctl(p->fb,FBIOPUT_VSCREENINFO,&p->mode) ||
           ioctl(p->fb,FBIOGET_VSCREENINFO,&p->mode))goto fail;
    }
    struct fb_var_screeninfo *v=&p->mode;
    stage="validate GS pixel format";errno=ENOTSUP;
    if(!v->xres || !v->yres || v->xres>2048 || v->yres>2048 ||
       (v->bits_per_pixel!=16 && v->bits_per_pixel!=32) || v->red.offset ||
       v->red.length!=(v->bits_per_pixel==16?5U:8U) || v->green.length!=v->red.length || v->blue.length!=v->red.length ||
       v->green.offset!=v->red.length || v->blue.offset!=2*v->red.length)goto fail;
    s->monitor=s->confScreen->monitor;
    int depth=v->bits_per_pixel==16?15:24;
    stage="select Xorg depth";errno=EINVAL;
    if(!xf86SetDepthBpp(s,depth,depth,v->bits_per_pixel,Support32bppFb) || s->depth!=depth || s->bitsPerPixel!=(int)v->bits_per_pixel)goto fail;
    stage="register native depth30 pixmaps";
    if(!xf86AddPixFormat(s,30,32,BITMAP_SCANLINE_PAD))goto fail;
    rgb weight={v->red.length,v->green.length,v->blue.length};
    rgb masks={(1UL<<v->red.length)-1,((1UL<<v->green.length)-1)<<v->green.offset,((1UL<<v->blue.length)-1)<<v->blue.offset};
    Gamma gamma={0,0,0};
    stage="select TrueColor visual";
    if(!xf86SetWeight(s,weight,masks) || !xf86SetDefaultVisual(s,TrueColor) || !xf86SetGamma(s,gamma))goto fail;
    s->rgbBits=v->red.length;s->progClock=TRUE;s->chipset="Graphics Synthesizer";s->videoRam=4096;
    s->virtualX=v->xres;s->virtualY=v->yres;s->displayWidth=(v->xres+63)&~63U;
    DisplayModeRec mode={.name="PS2-current",.type=M_T_DRIVER|M_T_PREFERRED,
        .Clock=v->pixclock?1000000000U/v->pixclock:27000,
        .HDisplay=v->xres,.HSyncStart=v->xres+v->right_margin,.HSyncEnd=v->xres+v->right_margin+v->hsync_len,
        .HTotal=v->xres+v->right_margin+v->hsync_len+v->left_margin,
        .VDisplay=v->yres,.VSyncStart=v->yres+v->lower_margin,.VSyncEnd=v->yres+v->lower_margin+v->vsync_len,
        .VTotal=v->yres+v->lower_margin+v->vsync_len+v->upper_margin};
    stage="create display mode";errno=ENOMEM;
    s->modes=xf86DuplicateMode(&mode);if(!s->modes)goto fail;
    s->modes->next=s->modes->prev=s->modes;s->currentMode=s->modes;
    xf86SetCrtcForModes(s,0);xf86SetDpi(s,0,0);xf86PrintDepthBpp(s);
    stage="load EXA module";errno=ENOENT;
    if(!xf86LoadSubModule(s,"exa"))goto fail;
    return TRUE;
fail:
    xf86DrvMsg(s->scrnIndex,X_ERROR,"native GS PreInit failed at %s: %s\n",stage,strerror(errno?errno:ENOTSUP));
    if(p->fb>=0)close(p->fb);
    free(p);s->driverPrivate=NULL;return FALSE;
}
static void get_image(DrawablePtr d,int x,int y,int w,int h,unsigned format,unsigned long mask,char *out)
{
    PS2Rec *p=xf86ScreenToScrn(d->pScreen)->driverPrivate;
    ps2_readback_image(p->context,d,x,y,w,h,format,mask,out);
}
static void get_spans(DrawablePtr d,int max,DDXPointPtr points,int *widths,int n,char *out)
{
    PS2Rec *p=xf86ScreenToScrn(d->pScreen)->driverPrivate;
    ps2_readback_spans(p->context,d,max,points,widths,n,out);
}
static Bool resources(ScreenPtr screen)
{
    PS2Rec *p=xf86ScreenToScrn(screen)->driverPrivate;
    screen->CreateScreenResources=p->resources;Bool ok=screen->CreateScreenResources(screen);
    p->resources=screen->CreateScreenResources;screen->CreateScreenResources=resources;
    if(!ok)return FALSE;
    screen->CloseScreen=p->close;
    ok=ps2_cursor_screen_prepare(screen,&p->cursor,p->context,xf86GetPointerScreenFuncs());
    p->close=screen->CloseScreen;screen->CloseScreen=close_screen;
    if(ok)xf86DrvMsg(xf86ScreenToScrn(screen)->scrnIndex,X_INFO,"native screen resources and GS cursor ready\n");
    return ok;
}
static Bool save_screen(ScreenPtr screen,int mode){(void)screen;(void)mode;return TRUE;}
static void add_picture_format(PictFormatPtr formats,int *count,uint32_t code,unsigned depth)
{
    for(int i=0;i<*count;++i)if(formats[i].format==code && formats[i].depth==depth)return;
    if(*count>=1024)FatalError("PS2 GS: too many Render formats\n");
    unsigned a=PICT_FORMAT_A(code),r=PICT_FORMAT_R(code),g=PICT_FORMAT_G(code),b=PICT_FORMAT_B(code);
    PictFormatPtr f=formats+(*count)++;
    *f=(PictFormatRec){.id=FakeClientID(0),.format=code,.depth=depth,.type=PictTypeDirect,
        .direct={.alphaMask=(1U<<a)-1,.redMask=(1U<<r)-1,.greenMask=(1U<<g)-1,.blueMask=(1U<<b)-1}};
    switch(PICT_FORMAT_TYPE(code)){
    case PICT_TYPE_ARGB:f->direct.red=g+b;f->direct.green=b;f->direct.alpha=r+g+b;break;
    case PICT_TYPE_ABGR:f->direct.green=r;f->direct.blue=r+g;f->direct.alpha=r+g+b;break;
    case PICT_TYPE_BGRA:{unsigned bits=PICT_FORMAT_BPP(code);
        f->direct.red=bits-b-g-r;f->direct.green=bits-b-g;f->direct.blue=bits-b;break;}
    case PICT_TYPE_A:break;
    default:FatalError("PS2 GS: invalid direct Render format\n");
    }
    if(!a)f->direct.alpha=0;
}
static Bool native_picture_init(ScreenPtr screen,Bool alpha)
{
    if(!alpha)return fbPictureInit(screen,NULL,0);
    PictFormatPtr formats=calloc(1024,sizeof(*formats));if(!formats)return FALSE;
    int count=0;
    /* Preserve the pinned server's mandatory and depth-derived formats.
     * PictureInit owns this array and registers its resources internally. */
    add_picture_format(formats,&count,PICT_a1,1);
    add_picture_format(formats,&count,PICT_a8,8);
    add_picture_format(formats,&count,PICT_x4a4,4);
    add_picture_format(formats,&count,PICT_x4a4,8);
    add_picture_format(formats,&count,PICT_r3g3b2,8);
    add_picture_format(formats,&count,PICT_b2g3r3,8);
    add_picture_format(formats,&count,PICT_a2r2g2b2,8);
    add_picture_format(formats,&count,PICT_a2b2g2r2,8);
    add_picture_format(formats,&count,PICT_a8r8g8b8,32);
    add_picture_format(formats,&count,PICT_x8r8g8b8,32);
    add_picture_format(formats,&count,PICT_b8g8r8a8,32);
    add_picture_format(formats,&count,PICT_b8g8r8x8,32);
    add_picture_format(formats,&count,PICT_a8b8g8r8,32);
    for(int i=0;i<screen->numDepths;++i){unsigned depth=screen->allowedDepths[i].depth,bpp=BitsPerPixel(depth);
        if(bpp==16){
            if(depth>=12){add_picture_format(formats,&count,PICT_x4r4g4b4,depth);add_picture_format(formats,&count,PICT_x4b4g4r4,depth);}
            if(depth>=15){add_picture_format(formats,&count,PICT_x1r5g5b5,depth);add_picture_format(formats,&count,PICT_x1b5g5r5,depth);}
            if(depth>=16){const uint32_t codes[]={PICT_a1r5g5b5,PICT_a1b5g5r5,PICT_r5g6b5,PICT_b5g6r5,PICT_a4r4g4b4,PICT_a4b4g4r4};
                for(unsigned j=0;j<sizeof(codes)/sizeof(*codes);++j)add_picture_format(formats,&count,codes[j],depth);}
        }else if(bpp==32){
            if(depth>=24){add_picture_format(formats,&count,PICT_x8r8g8b8,depth);add_picture_format(formats,&count,PICT_x8b8g8r8,depth);}
            if(depth>=30){const uint32_t codes[]={PICT_a2r10g10b10,PICT_x2r10g10b10,PICT_a2b10g10r10,PICT_x2b10g10r10};
                for(unsigned j=0;j<sizeof(codes)/sizeof(*codes);++j)
                    if(depth>=32 || !PICT_FORMAT_A(codes[j]))add_picture_format(formats,&count,codes[j],depth);}
        }
    }
    /* Native TrueColor visual masks match the RGBX/RGB555 entries above. */
    return fbPictureInit(screen,formats,count);
}
static Bool attributes(WindowPtr window,unsigned long mask)
{
    (void)mask;
    /* Core X already owns and validates window attributes. Upstream fb's
     * remaining callback only pads colour tiles for its CPU fill loops; GS
     * PaintWindow samples the original dimensions and needs no padding. */
    return window && window->drawable.pScreen && xf86ScreenToScrn(window->drawable.pScreen)->driverPrivate;
}
static RegionPtr bitmap_to_region(PixmapPtr pixmap)
{
    PS2Rec *p=xf86ScreenToScrn(pixmap->drawable.pScreen)->driverPrivate;
    return ps2_readback_bitmap_region(p->context,pixmap);
}
static Bool screen_init(ScreenPtr screen,int argc,char **argv)
{
    (void)argc;(void)argv;ScrnInfoPtr s=xf86ScreenToScrn(screen);PS2Rec *p=s->driverPrivate;
    p->mode.xoffset=p->mode.yoffset=0;p->mode.vmode&=~FB_VMODE_YWRAP;
    if(ioctl(p->fb,FBIOPAN_DISPLAY,&p->mode))return FALSE;
    p->pipe=ps2_pipe_screen_create("/dev/ps2-gs");
    if(!p->pipe || !(p->context=p->pipe->context_create(p->pipe,NULL,0)))return FALSE;
    struct ps2_render_client *render=ps2_pipe_context_render(p->context);
    if(render->gs->caps.width!=p->mode.xres || render->gs->caps.height!=p->mode.yres || render->gs->caps.bits_per_pixel!=p->mode.bits_per_pixel)return FALSE;
    miClearVisualTypes();
    if(!miSetVisualTypes(s->depth,TrueColorMask,s->rgbBits,TrueColor) || !miSetPixmapDepths() ||
       !fbScreenInit(screen,NULL,s->virtualX,s->virtualY,s->xDpi,s->yDpi,s->displayWidth,s->bitsPerPixel))return FALSE;
    for(int i=0;i<screen->numVisuals;++i){VisualPtr v=screen->visuals+i;
        if(v->class==TrueColor || v->class==DirectColor){v->offsetRed=s->offset.red;v->offsetGreen=s->offset.green;v->offsetBlue=s->offset.blue;
            v->redMask=s->mask.red;v->greenMask=s->mask.green;v->blueMask=s->mask.blue;}}
    if(!native_picture_init(screen,!!(render->gs->caps.features&PS2GS_HW_CHANNEL_MAP_BLIT)))return FALSE;
    screen->SaveScreen=save_screen;
    if(!ps2_surface_exa_prepare(screen,&p->exa,render->gs) ||
       !ps2_exa_context_bridge_prepare(screen,&p->bridge,&p->exa,p->context) ||
       !ps2_logic_exa_prepare(screen,&p->logic,&p->exa,p->context) ||
       !ps2_copy_exa_prepare(screen,&p->copy,&p->exa,p->context) ||
       !ps2_texture_exa_prepare(screen,&p->texture,&p->exa,render) ||
       !ps2_blit_exa_prepare(screen,&p->blit,&p->exa,p->context) || !exaDriverInit(screen,&p->exa.driver) ||
       !ps2_copy_window_prepare(screen,&p->windows,&p->exa,p->context) ||
       !ps2_paint_window_prepare(screen,&p->paint,&p->exa,p->context) || !DamageSetup(screen) ||
       !ps2_gc_screen_prepare(screen,&p->gc,&p->exa,p->context) || !ps2_solid_screen_prepare(screen,&p->solid,&p->gc) ||
       !ps2_copy_screen_prepare(screen,&p->copies,&p->gc) ||
       !ps2_fill_screen_prepare(screen,&p->fill,&p->gc) || !ps2_image_screen_prepare(screen,&p->images,&p->gc) ||
       !ps2_polygon_screen_prepare(screen,&p->polygons,&p->gc) || !ps2_geometry_screen_prepare(screen,&p->geometry,&p->gc) ||
       !ps2_alpha_map_screen_prepare(screen,&p->alpha_map,&p->blit) ||
       !ps2_render_geometry_prepare(screen,&p->render_geometry,&p->blit))return FALSE;
    /* EXA wraps fb's read callbacks during initialization. Install native
     * transfers afterward so its CPU PrepareAccess fallback is never entered. */
    p->bitmap_get_image=screen->GetImage;p->bitmap_get_spans=screen->GetSpans;
    screen->GetImage=get_image;screen->GetSpans=get_spans;
    /* EXA's BitmapToRegion enters CPU raster access even for an immutable A1
     * clip mask. Convert owned protocol storage to geometry directly instead. */
    p->bitmap_to_region=screen->BitmapToRegion;screen->BitmapToRegion=bitmap_to_region;
    xf86SetBlackWhitePixels(screen);xf86SetBackingStore(screen);
    if(!miCreateDefColormap(screen))return FALSE;
    p->close=screen->CloseScreen;screen->CloseScreen=close_screen;
    p->attributes=screen->ChangeWindowAttributes;screen->ChangeWindowAttributes=attributes;
    p->resources=screen->CreateScreenResources;screen->CreateScreenResources=resources;p->initialized=TRUE;s->vtSema=TRUE;
    xf86DrvMsg(s->scrnIndex,X_INFO,"native core-X GS/Mesa/EXA/GC/Damage integration installed; no mapped colour framebuffer\n");
    return TRUE;
}
static Bool close_screen(ScreenPtr screen)
{
    ScrnInfoPtr s=xf86ScreenToScrn(screen);PS2Rec *p=s->driverPrivate;
    screen->CloseScreen=p->close;screen->CreateScreenResources=p->resources;
    screen->ChangeWindowAttributes=p->attributes;
    screen->BitmapToRegion=p->bitmap_to_region;
    Bool ok=screen->CloseScreen(screen);
    ps2_blit_exa_cleanup(screen,&p->blit);ps2_copy_exa_cleanup(screen,&p->copy);
    ps2_logic_exa_cleanup(screen,&p->logic);ps2_exa_context_bridge_cleanup(&p->bridge);
    if(p->context){p->context->destroy(p->context);p->context=NULL;}
    if(p->pipe){p->pipe->destroy(p->pipe);p->pipe=NULL;}
    s->vtSema=FALSE;p->initialized=FALSE;return ok;
}
static Bool enter_vt(ScrnInfoPtr s)
{
    PS2Rec *p=s->driverPrivate;struct fb_var_screeninfo v;
    if(ioctl(p->fb,FBIOGET_VSCREENINFO,&v) || v.xres!=p->mode.xres || v.yres!=p->mode.yres || v.bits_per_pixel!=p->mode.bits_per_pixel)return FALSE;
    return !p->cursor.installed || ps2_cursor_screen_suspend(&p->cursor,FALSE);
}
static void leave_vt(ScrnInfoPtr s)
{PS2Rec *p=s->driverPrivate;if(p->cursor.installed && !ps2_cursor_screen_suspend(&p->cursor,TRUE))xf86DrvMsg(s->scrnIndex,X_ERROR,"GS cursor VT suspend failed\n");}
static Bool switch_mode(ScrnInfoPtr s,DisplayModePtr m){return valid_mode(s,m,FALSE,0)==MODE_OK;}
static void adjust(ScrnInfoPtr s,int x,int y){if(x || y)xf86DrvMsg(s->scrnIndex,X_ERROR,"native GS panning is unsupported\n");}
static ModeStatus valid_mode(ScrnInfoPtr s,DisplayModePtr m,Bool verbose,int flags)
{(void)verbose;(void)flags;PS2Rec *p=s->driverPrivate;return m->HDisplay==(int)p->mode.xres && m->VDisplay==(int)p->mode.yres?MODE_OK:MODE_BAD;}
static void free_screen(ScrnInfoPtr s)
{
    PS2Rec *p=s->driverPrivate;if(!p)return;
    if(p->context)p->context->destroy(p->context);
    if(p->pipe)p->pipe->destroy(p->pipe);
    if(p->fb>=0)close(p->fb);
    free(p);s->driverPrivate=NULL;
}
