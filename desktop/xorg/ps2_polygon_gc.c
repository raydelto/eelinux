/* SPDX-License-Identifier: MIT */
#include "ps2_polygon_gc.h"
#include "ps2_polygon.h"
#include "ps2_polygon_stipple.h"
#include "ps2_polygon_tile.h"
#include "ps2_rop.h"
#include "servermd.h"
#include "windowstr.h"
#include "regionstr.h"
#include "damage.h"
#include "os.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
static DevPrivateKeyRec key;
static const GCOps operations;
static Bool close_screen(ScreenPtr);
static void fail(struct ps2_pipe_polygon_point *points,const char *reason)
{free(points);FatalError("PS2 GS: native polygon fill failed: %s\n",reason);}
static int64_t max64(int64_t a,int64_t b){return a>b?a:b;}
static int64_t min64(int64_t a,int64_t b){return a<b?a:b;}
static void polygon(DrawablePtr d,GCPtr gc,int shape,int mode,int count,DDXPointPtr input)
{
    if(!d || !gc || !d->pScreen || d->pScreen!=gc->pScreen || count<0 ||
       count>(int)PS2_PIPE_MAX_POLYGON_POINTS || (count && !input) ||
       (shape!=Complex && shape!=Nonconvex && shape!=Convex) ||
       (mode!=CoordModeOrigin && mode!=CoordModePrevious) ||
       gc->fillStyle>FillOpaqueStippled ||
       (gc->fillRule!=EvenOddRule && gc->fillRule!=WindingRule) || gc->alu>GXset ||
       !gc->pCompositeClip || RegionBroken(gc->pCompositeClip))fail(NULL,"invalid request or raster state");
    struct ps2_polygon_screen *owner=dixLookupPrivate(&d->pScreen->devPrivates,&key);
    if(!owner || !owner->installed || !owner->gc->installed || owner->gc->polygon_ops!=&operations ||
       ps2_pipe_context_acquire(owner->context))fail(NULL,"missing or failed polygon owner");
    ps2_pipe_context_release(owner->context);
    ScreenPtr screen=d->pScreen;
    PixmapPtr pixmap=d->type==DRAWABLE_PIXMAP?(PixmapPtr)d:
        d->type==DRAWABLE_WINDOW && screen->GetWindowPixmap?screen->GetWindowPixmap((WindowPtr)d):NULL;
    if(!pixmap || pixmap->drawable.pScreen!=screen || !screen->GetScreenPixmap)fail(NULL,"missing storage");
    uint32_t handle=0;
    if(pixmap!=screen->GetScreenPixmap(screen)){
        const void *private=exaGetPixmapDriverPrivate(pixmap);
        if(!private)fail(NULL,"missing owned pixmap");
        memcpy(&handle,private,sizeof(handle));if(!handle)fail(NULL,"unallocated pixmap");
    }
    struct ps2_render_client *render=ps2_pipe_context_render(owner->context);
    const struct ps2_surface *storage=render?ps2_surface_lookup(render->gs,handle):NULL;
    if(!storage || render->gs!=owner->gc->exa->gs ||
       pixmap->drawable.width!=storage->width || pixmap->drawable.height!=storage->height ||
       pixmap->drawable.bitsPerPixel!=storage->bits_per_pixel || d->bitsPerPixel!=storage->bits_per_pixel ||
       d->depth!=ps2_surface_depth(storage))fail(NULL,"incompatible native storage");
    int64_t ox=0,oy=0;
#ifdef COMPOSITE
    if(d->type==DRAWABLE_WINDOW){ox=pixmap->screen_x;oy=pixmap->screen_y;}
#endif
    int n=RegionNumRects(gc->pCompositeClip);const BoxRec *boxes=RegionRects(gc->pCompositeClip);
    if(n<0 || n>(int)PS2_PIPE_MAX_POLYGON_CLIPS)fail(NULL,"clip complexity exceeded");
    struct ps2_pipe_solid_box clips[PS2_PIPE_MAX_POLYGON_CLIPS];unsigned clipped=0;
    for(int i=0;i<n;++i){
        if(boxes[i].x1>boxes[i].x2 || boxes[i].y1>boxes[i].y2)fail(NULL,"invalid clip box");
        int64_t x1=max64(boxes[i].x1-ox,0),y1=max64(boxes[i].y1-oy,0);
        int64_t x2=min64(boxes[i].x2-ox,storage->width),y2=min64(boxes[i].y2-oy,storage->height);
        if(x1<x2 && y1<y2)clips[clipped++]=(struct ps2_pipe_solid_box){x1,y1,x2,y2};
    }
    struct ps2_pipe_mono_pattern mono={0};
    int64_t pattern_x=(int64_t)d->x+gc->patOrg.x-ox,pattern_y=(int64_t)d->y+gc->patOrg.y-oy;
    uint32_t tile_handle=0;
    if(gc->fillStyle==FillTiled && !gc->tileIsPixel){
        PixmapPtr tile=gc->tile.pixmap;
        uint32_t bits=ps2_surface_native_mask(render->gs,handle);
        if(!tile || tile==screen->GetScreenPixmap(screen) || tile->drawable.pScreen!=screen ||
           tile->drawable.depth!=d->depth || tile->drawable.bitsPerPixel!=storage->bits_per_pixel ||
           !exaGetPixmapDriverPrivate(tile))fail(NULL,"invalid polygon tile source");
        memcpy(&tile_handle,exaGetPixmapDriverPrivate(tile),4);
        const struct ps2_surface *t=ps2_surface_lookup(render->gs,tile_handle);
        if(!tile_handle || tile_handle==handle || !t || tile->drawable.width!=t->width || tile->drawable.height!=t->height ||
           t->bits_per_pixel!=storage->bits_per_pixel || pattern_x<INT32_MIN || pattern_x>INT32_MAX ||
           pattern_y<INT32_MIN || pattern_y>INT32_MAX)fail(NULL,"stale polygon tile source or origin");
        if((gc->planemask&bits) && gc->alu!=GXnoop && (gc->alu!=GXcopy || (gc->planemask&bits)!=bits) &&
           !(render->gs->caps.features&PS2GS_HW_TEXTURE_TRANSFER))
            fail(NULL,"kernel lacks native Boolean texture transfers");
    }else if(gc->fillStyle==FillStippled || gc->fillStyle==FillOpaqueStippled){
        PixmapPtr source=gc->stipple;struct ps2_mono_source owned={0};
        if(!source || source->drawable.pScreen!=screen || source->drawable.type!=DRAWABLE_PIXMAP ||
           source->drawable.depth!=1 || source->drawable.bitsPerPixel!=1 || !source->drawable.width ||
           !source->drawable.height || source->drawable.width>256 || source->drawable.height>256 ||
           source->devKind<=0 || source->devKind<(source->drawable.width+7)/8)
            fail(NULL,"invalid monochrome polygon stipple");
        int native=ps2_surface_exa_mono_source(source,&owned);
        if(native<0)fail(NULL,"invalid owned monochrome stipple header");
        const unsigned char *bits=native?owned.bits:source->devPrivate.ptr;
        size_t row=(source->drawable.width+7)/8,stride=source->devKind;
        if(!bits || (size_t)source->drawable.height-1>(SIZE_MAX-row)/stride)
            fail(NULL,"invalid stipple source or stride");
        size_t bytes=(source->drawable.height-1)*stride+row;
        if((uintptr_t)bits>UINTPTR_MAX-(bytes-1) || pattern_x<INT32_MIN || pattern_x>INT32_MAX ||
           pattern_y<INT32_MIN || pattern_y>INT32_MAX)fail(NULL,"stipple address or origin overflow");
        mono=(struct ps2_pipe_mono_pattern){.bits=bits,.bytes=bytes,.stride=stride,
            .width=source->drawable.width,.height=source->drawable.height,
            .msb_first=BITMAP_BIT_ORDER==MSBFirst};
    }
    struct ps2_pipe_polygon_point *points=count?malloc(count*sizeof(*points)):NULL;
    if(count && !points)fail(NULL,"out of memory");
    int64_t x=0,y=0;
    for(int i=0;i<count;++i){
        x=(mode==CoordModePrevious && i?x:0)+input[i].x;
        y=(mode==CoordModePrevious && i?y:0)+input[i].y;
        int64_t sx=x+(gc->miTranslate?d->x:0),sy=y+(gc->miTranslate?d->y:0);
        /* MI stores translated vertices in signed 16-bit DDXPointRec. Reject
         * overflow instead of wrapping client geometry into another polygon. */
        if(sx<INT16_MIN || sx>INT16_MAX || sy<INT16_MIN || sy>INT16_MAX)
            fail(points,"translated coordinate overflow");
        points[i]=(struct ps2_pipe_polygon_point){sx-ox,sy-oy};
    }
    uint32_t bits=ps2_surface_native_mask(render->gs,handle);
    unsigned emitted=0;
    int status=(!count || !clipped || !(gc->planemask&bits) || gc->alu==GXnoop)?0:
        gc->fillStyle==FillTiled && !gc->tileIsPixel ?
        ps2_pipe_tile_logic_polygon(owner->context,tile_handle,handle,storage->bits_per_pixel,points,count,
            clips,clipped,gc->fillRule==WindingRule,pattern_x,pattern_y,gc->planemask&bits,gc->alu,&emitted) :
        gc->fillStyle==FillSolid || gc->fillStyle==FillTiled ?
        ps2_pipe_fill_polygon(owner->context,handle,storage->bits_per_pixel,points,count,
            clips,clipped,gc->fillRule==WindingRule,(gc->fillStyle==FillTiled?gc->tile.pixel:gc->fgPixel)&bits,gc->planemask&bits,gc->alu,&emitted) :
        ps2_pipe_stipple_polygon(owner->context,handle,storage->bits_per_pixel,points,count,
            clips,clipped,gc->fillRule==WindingRule,&mono,gc->fillStyle==FillOpaqueStippled,
            pattern_x,pattern_y,gc->fgPixel&bits,gc->bgPixel&bits,gc->planemask&bits,gc->alu,&emitted);
    int error=errno;free(points);
    if(status)fail(NULL,strerror(error));
    if(emitted){
        exaMarkSync(screen);DamageRegionAppend(d,gc->pCompositeClip);DamageRegionProcessPending(d);
    }
}
static const GCOps operations={.FillPolygon=polygon};
Bool ps2_polygon_screen_prepare(ScreenPtr screen,struct ps2_polygon_screen *b,struct ps2_gc_screen *gc)
{
    if(!screen || !b || b->installed || !gc || !gc->installed || gc->screen!=screen ||
       gc->polygon_ops || !screen->CloseScreen || screen->CloseScreen==close_screen ||
       !dixRegisterPrivateKey(&key,PRIVATE_SCREEN,0) || dixLookupPrivate(&screen->devPrivates,&key) ||
       ps2_pipe_context_acquire(gc->context))return FALSE;
    *b=(struct ps2_polygon_screen){screen,gc,gc->context,screen->CloseScreen,TRUE};
    dixSetPrivate(&screen->devPrivates,&key,b);screen->CloseScreen=close_screen;
    if(!ps2_gc_screen_set_polygon_ops(gc,&operations))FatalError("PS2 GS: polygon installation failed\n");
    return TRUE;
}
void ps2_polygon_screen_cleanup(struct ps2_polygon_screen *b)
{
    if(!b || !b->installed)return;
    if(b->screen->CloseScreen!=close_screen || b->gc->polygon_ops!=&operations ||
       !ps2_gc_screen_set_polygon_ops(b->gc,NULL))FatalError("PS2 GS: polygon cleanup order\n");
    b->screen->CloseScreen=b->CloseScreen;dixSetPrivate(&b->screen->devPrivates,&key,NULL);
    struct pipe_context *context=b->context;memset(b,0,sizeof(*b));ps2_pipe_context_release(context);
}
static Bool close_screen(ScreenPtr screen)
{
    struct ps2_polygon_screen *b=dixLookupPrivate(&screen->devPrivates,&key);
    if(!b || !b->installed)FatalError("PS2 GS: missing polygon screen owner\n");
    CloseScreenProcPtr lower=b->CloseScreen;ps2_polygon_screen_cleanup(b);return lower(screen);
}
