/* SPDX-License-Identifier: MIT */
#include "ps2_copy.h"
#include "ps2_copy_batch.h"
#include "ps2_rop.h"
#include "ps2_gs_surface.h"
#include "windowstr.h"
#include "regionstr.h"
#include "damage.h"
#include "mi.h"
#include "os.h"
#include <errno.h>
#include <limits.h>
#include <string.h>

static DevPrivateKeyRec key;
static const GCOps operations;
static Bool close_screen(ScreenPtr);
struct storage { uint32_t handle; int ox,oy; const struct ps2_surface *surface; };
struct request { struct ps2_copy_screen *owner; struct storage source,destination; };
static void fail(const char *reason) { FatalError("PS2 GS: native CopyArea failed: %s\n",reason); }
static struct storage storage(DrawablePtr d,struct ps2_copy_screen *b)
{
    ScreenPtr s=b->screen;
    PixmapPtr p=d->type==DRAWABLE_PIXMAP ? (PixmapPtr)d :
        d->type==DRAWABLE_WINDOW && s->GetWindowPixmap ? s->GetWindowPixmap((WindowPtr)d) : NULL;
    struct storage result={0};
    if(!p || p->drawable.pScreen!=s || !s->GetScreenPixmap) fail("missing drawable storage");
    if(p!=s->GetScreenPixmap(s)) {
        const void *private=exaGetPixmapDriverPrivate(p);
        if(!private) fail("missing owned pixmap");
        memcpy(&result.handle,private,sizeof(result.handle));
        if(!result.handle) fail("unallocated pixmap");
    }
    struct ps2_render_client *r=ps2_pipe_context_render(b->context);
    result.surface=r ? ps2_surface_lookup(r->gs,result.handle) : NULL;
    if(!result.surface || r->gs!=b->gc->exa->gs ||
        p->drawable.width!=result.surface->width || p->drawable.height!=result.surface->height ||
        p->drawable.bitsPerPixel!=result.surface->bits_per_pixel ||
        d->bitsPerPixel!=result.surface->bits_per_pixel ||
        d->depth!=ps2_surface_depth(result.surface)) fail("incompatible native storage");
#ifdef COMPOSITE
    if(d->type==DRAWABLE_WINDOW) { result.ox=p->screen_x;result.oy=p->screen_y; }
#endif
    return result;
}
static void copy_region(DrawablePtr source,DrawablePtr d,GCPtr gc,BoxPtr boxes,int n,
    int dx,int dy,Bool reverse,Bool upside,Pixel plane,void *closure)
{
    (void)source;(void)reverse;(void)upside;(void)plane;
    struct request *r=closure;
    if(n<0 || n>(int)PS2_PIPE_MAX_COPY_BOXES || (n && !boxes)) fail("clipped region exceeds native limit");
    struct ps2_pipe_copy_box jobs[PS2_PIPE_MAX_COPY_BOXES];
    for(int i=0;i<n;++i) {
        const BoxRec *b=boxes+i;
        jobs[i]=(struct ps2_pipe_copy_box){.sx=(int)b->x1+dx-r->source.ox,
            .sy=(int)b->y1+dy-r->source.oy,.dx=(int)b->x1-r->destination.ox,
            .dy=(int)b->y1-r->destination.oy,.width=(int)b->x2-b->x1,.height=(int)b->y2-b->y1};
    }
    uint32_t bits=ps2_surface_native_mask(ps2_pipe_context_render(r->owner->context)->gs,r->destination.handle);
    if(!n || !(gc->planemask&bits) || gc->alu==GXnoop) return;
    uint32_t mask=gc->planemask&bits;
    int status=d->depth!=32 && gc->alu==GXcopy && mask==bits ?
        ps2_pipe_copy_boxes(r->owner->context,r->source.handle,r->destination.handle,jobs,n) :
        ps2_pipe_rop_copy_boxes(r->owner->context,r->source.handle,r->destination.handle,jobs,n,mask,gc->alu);
    if(status) fail(strerror(errno));
    exaMarkSync(d->pScreen);
    DamageRegionAppend(d,gc->pCompositeClip);DamageRegionProcessPending(d);
}
static int coordinates(DrawablePtr d,int x,int y,int w,int h)
{
    int64_t a=(int64_t)d->x+x,b=(int64_t)d->y+y;
    return a>=SHRT_MIN && b>=SHRT_MIN && a+w<=SHRT_MAX && b+h<=SHRT_MAX;
}
static RegionPtr copy_area(DrawablePtr source,DrawablePtr d,GCPtr gc,int x,int y,int w,int h,int dx,int dy)
{
    if(!source || !d || !gc || source->pScreen!=d->pScreen || d->pScreen!=gc->pScreen ||
        !gc->pCompositeClip || source->depth!=d->depth || w<0 || h<0 || w>2048 || h>2048 ||
        !coordinates(source,x,y,w,h) || !coordinates(d,dx,dy,w,h)) fail("invalid drawable or copy coordinates");
    struct ps2_copy_screen *b=dixLookupPrivate(&d->pScreen->devPrivates,&key);
    if(!b || !b->installed || !b->gc->installed || b->gc->copy_ops!=&operations ||
        !b->screen->SourceValidate) fail("missing copy owner or source validation");
    if(ps2_pipe_context_acquire(b->context)) fail("context is closed or failed");
    ps2_pipe_context_release(b->context);
    struct request r={.owner=b,.source=storage(source,b),.destination=storage(d,b)};
    uint32_t mask=ps2_surface_native_mask(ps2_pipe_context_render(b->context)->gs,r.destination.handle);
    if(gc->alu>GXset) fail("invalid copy ALU");
    if(!(b->gc->exa->gs->caps.features&PS2GS_HW_TEXTURE_TRANSFER) && gc->alu!=GXnoop &&
        ((gc->alu!=GXcopy) || ((gc->planemask&mask) && (gc->planemask&mask)!=mask)))
        fail("kernel lacks native Boolean texture transfers");
    /* miDoCopy performs geometry/exposure bookkeeping only; this callback
     * exclusively submits native GS drawing, including all overlap handling. */
    return miDoCopy(source,d,gc,x,y,w,h,dx,dy,copy_region,0,&r);
}
static const GCOps operations={.CopyArea=copy_area};
Bool ps2_copy_screen_prepare(ScreenPtr s,struct ps2_copy_screen *b,struct ps2_gc_screen *gc)
{
    if(!s || !b || b->installed || !gc || !gc->installed || gc->screen!=s || gc->copy_ops ||
        !s->CloseScreen || !s->SourceValidate || !dixRegisterPrivateKey(&key,PRIVATE_SCREEN,0) ||
        dixLookupPrivate(&s->devPrivates,&key) || ps2_pipe_context_acquire(gc->context)) return FALSE;
    *b=(struct ps2_copy_screen){.gc=gc,.context=gc->context,.screen=s,.CloseScreen=s->CloseScreen,.installed=TRUE};
    if(!ps2_gc_screen_set_copy_ops(gc,&operations)) {
        ps2_pipe_context_release(b->context);memset(b,0,sizeof(*b));return FALSE;
    }
    dixSetPrivate(&s->devPrivates,&key,b);s->CloseScreen=close_screen;return TRUE;
}
void ps2_copy_screen_cleanup(struct ps2_copy_screen *b)
{
    if(!b || !b->installed) return;
    if(b->screen->CloseScreen!=close_screen || !b->gc->installed ||
        b->gc->copy_ops!=&operations || !ps2_gc_screen_set_copy_ops(b->gc,NULL)) fail("copy callback owner changed");
    b->screen->CloseScreen=b->CloseScreen;dixSetPrivate(&b->screen->devPrivates,&key,NULL);
    struct pipe_context *ctx=b->context;memset(b,0,sizeof(*b));ps2_pipe_context_release(ctx);
}
static Bool close_screen(ScreenPtr s)
{
    struct ps2_copy_screen *b=dixLookupPrivate(&s->devPrivates,&key);
    if(!b || !b->installed) fail("missing copy screen at close");
    CloseScreenProcPtr lower=b->CloseScreen;ps2_copy_screen_cleanup(b);return lower(s);
}
