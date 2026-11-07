/* SPDX-License-Identifier: MIT */
#include "ps2_geometry.h"
#include "mi.h"
#include "os.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
static DevPrivateKeyRec key;
static const GCOps operations;
static Bool close_screen(ScreenPtr);
static struct ps2_geometry_screen *owner(DrawablePtr d,GCPtr gc,int count,const void *data)
{
    if(!d || !gc || d->pScreen!=gc->pScreen || count<0 || count>1024 || (count && !data) ||
       gc->depth==1 || gc->alu>GXset || gc->fillStyle>FillOpaqueStippled || gc->lineStyle>LineDoubleDash ||
       gc->lineWidth>2048 || gc->capStyle>CapProjecting || gc->joinStyle>JoinBevel ||
       !gc->pCompositeClip || RegionBroken(gc->pCompositeClip))FatalError("PS2 GS: invalid MI geometry request\n");
    struct ps2_geometry_screen *b=dixLookupPrivate(&d->pScreen->devPrivates,&key);
    if(!b || !b->installed || b->gc->geometry_ops!=&operations || !b->gc->fill_ops || !b->gc->solid_ops ||
       ps2_pipe_context_acquire(b->gc->context))FatalError("PS2 GS: invalid MI geometry owner\n");
    ps2_pipe_context_release(b->gc->context);return b;
}
static DDXPointPtr points(DrawablePtr d,GCPtr gc,int mode,int n,DDXPointPtr input)
{
    owner(d,gc,n,input);
    if(mode!=CoordModeOrigin && mode!=CoordModePrevious)FatalError("PS2 GS: invalid geometry coordinate mode\n");
    DDXPointPtr saved=n?malloc(n*sizeof(*saved)):NULL;
    if(n && !saved)FatalError("PS2 GS: geometry snapshot allocation\n");
    int64_t x=0,y=0,last_x=0,last_y=0;uint64_t work=0;
    for(int i=0;i<n;++i){x=(mode==CoordModePrevious && i?x:0)+input[i].x;y=(mode==CoordModePrevious && i?y:0)+input[i].y;
        int64_t sx=x+(gc->miTranslate?d->x:0),sy=y+(gc->miTranslate?d->y:0);
        if(sx<INT16_MIN || sx>INT16_MAX || sy<INT16_MIN || sy>INT16_MAX){free(saved);FatalError("PS2 GS: geometry coordinate overflow\n");}
        if(i)work+=(uint64_t)llabs(x-last_x)+llabs(y-last_y)+gc->lineWidth+1;
        last_x=x;last_y=y;
    }
    if(work>4194304){free(saved);FatalError("PS2 GS: line geometry work exceeded\n");}
    if(n)memcpy(saved,input,n*sizeof(*saved));
    return saved;
}
static void lines(DrawablePtr d,GCPtr gc,int mode,int n,DDXPointPtr input)
{
    struct ps2_geometry_screen *b=owner(d,gc,n,input);
    if(!gc->lineWidth && gc->fillStyle==FillSolid){b->gc->solid_ops->Polylines(d,gc,mode,n,input);return;}
    DDXPointPtr saved=points(d,gc,mode,n,input);miPolylines(d,gc,mode,n,saved);free(saved);
}
static void segments(DrawablePtr d,GCPtr gc,int n,xSegment *input)
{
    struct ps2_geometry_screen *b=owner(d,gc,n,input);
    if(!gc->lineWidth && gc->fillStyle==FillSolid){b->gc->solid_ops->PolySegment(d,gc,n,input);return;}
    xSegment *saved=n?malloc(n*sizeof(*saved)):NULL;if(n && !saved)FatalError("PS2 GS: segment snapshot allocation\n");
    if(n)memcpy(saved,input,n*sizeof(*saved));
    uint64_t work=0;
    for(int i=0;i<n;++i){DDXPointRec p[2]={{saved[i].x1,saved[i].y1},{saved[i].x2,saved[i].y2}};
        work+=(uint64_t)abs((int)p[1].x-p[0].x)+abs((int)p[1].y-p[0].y)+gc->lineWidth+1;
        DDXPointPtr checked=points(d,gc,CoordModeOrigin,2,p);free(checked);}
    if(work>4194304){free(saved);FatalError("PS2 GS: segment geometry work exceeded\n");}
    for(int i=0;i<n;++i){DDXPointRec p[2]={{saved[i].x1,saved[i].y1},{saved[i].x2,saved[i].y2}};miPolylines(d,gc,CoordModeOrigin,2,p);}
    free(saved);
}
static void rectangles(DrawablePtr d,GCPtr gc,int n,xRectangle *input)
{
    struct ps2_geometry_screen *b=owner(d,gc,n,input);
    if(!gc->lineWidth && gc->fillStyle==FillSolid){b->gc->solid_ops->PolyRectangle(d,gc,n,input);return;}
    xRectangle *saved=n?malloc(n*sizeof(*saved)):NULL;if(n && !saved)FatalError("PS2 GS: rectangle snapshot allocation\n");
    if(n)memcpy(saved,input,n*sizeof(*saved));
    uint64_t work=0;
    for(int i=0;i<n;++i){int64_t x=(int64_t)saved[i].x+(gc->miTranslate?d->x:0),y=(int64_t)saved[i].y+(gc->miTranslate?d->y:0);
        if(x<INT16_MIN || y<INT16_MIN || x+saved[i].width>INT16_MAX || y+saved[i].height>INT16_MAX){free(saved);FatalError("PS2 GS: rectangle geometry overflow\n");}
        work+=2U*((uint64_t)saved[i].width+saved[i].height+gc->lineWidth+1);
    }
    if(work>4194304){free(saved);FatalError("PS2 GS: rectangle geometry work exceeded\n");}
    miPolyRectangle(d,gc,n,saved);free(saved);
}
static void arcs(DrawablePtr d,GCPtr gc,int n,xArc *input,Bool filled)
{
    owner(d,gc,n,input);uint64_t work=0;
    for(int i=0;i<n;++i){
        if(input[i].width>2048 || input[i].height>2048)FatalError("PS2 GS: arc geometry extent exceeded\n");
        int64_t x=(int64_t)input[i].x+(gc->miTranslate?d->x:0),y=(int64_t)input[i].y+(gc->miTranslate?d->y:0);
        if(x<INT16_MIN || y<INT16_MIN || x+input[i].width>INT16_MAX || y+input[i].height>INT16_MAX)
            FatalError("PS2 GS: arc geometry coordinate overflow\n");
        work+=input[i].height+gc->lineWidth+1;
    }
    if(work>4194304)FatalError("PS2 GS: arc geometry work exceeded\n");
    xArc *saved=n?malloc(n*sizeof(*saved)):NULL;if(n && !saved)FatalError("PS2 GS: arc snapshot allocation\n");
    if(n)memcpy(saved,input,n*sizeof(*saved));
    if(filled)miPolyFillArc(d,gc,n,saved);else miPolyArc(d,gc,n,saved);free(saved);
}
static void draw_arcs(DrawablePtr d,GCPtr gc,int n,xArc *a){arcs(d,gc,n,a,FALSE);}
static void fill_arcs(DrawablePtr d,GCPtr gc,int n,xArc *a){arcs(d,gc,n,a,TRUE);}
static void push(GCPtr gc,PixmapPtr bitmap,DrawablePtr d,int w,int h,int x,int y)
{
    owner(d,gc,1,bitmap);
    if(bitmap->drawable.pScreen!=d->pScreen || bitmap->drawable.depth!=1 || w<0 || h<0 || w>2048 || h>2048 ||
       w>bitmap->drawable.width || h>bitmap->drawable.height)FatalError("PS2 GS: invalid source-only PushPixels\n");
    miPushPixels(gc,bitmap,d,w,h,x,y);
}
static const GCOps operations={.Polylines=lines,.PolySegment=segments,.PolyRectangle=rectangles,
    .PolyArc=draw_arcs,.PolyFillArc=fill_arcs,.PushPixels=push};
Bool ps2_geometry_screen_prepare(ScreenPtr s,struct ps2_geometry_screen *b,struct ps2_gc_screen *gc)
{
    if(!s || !b || b->installed || !gc || !gc->installed || gc->screen!=s || gc->geometry_ops || !gc->fill_ops ||
       !s->CloseScreen || !dixRegisterPrivateKey(&key,PRIVATE_SCREEN,0) || dixLookupPrivate(&s->devPrivates,&key) ||
       ps2_pipe_context_acquire(gc->context))return FALSE;
    *b=(struct ps2_geometry_screen){s,gc,s->CloseScreen,TRUE};dixSetPrivate(&s->devPrivates,&key,b);s->CloseScreen=close_screen;
    return ps2_gc_screen_set_geometry_ops(gc,&operations);
}
void ps2_geometry_screen_cleanup(struct ps2_geometry_screen *b)
{
    if(!b || !b->installed)return;
    if(b->screen->CloseScreen!=close_screen || b->gc->geometry_ops!=&operations)FatalError("PS2 GS: geometry cleanup order\n");
    ps2_gc_screen_set_geometry_ops(b->gc,NULL);b->screen->CloseScreen=b->CloseScreen;dixSetPrivate(&b->screen->devPrivates,&key,NULL);
    struct pipe_context *ctx=b->gc->context;memset(b,0,sizeof(*b));ps2_pipe_context_release(ctx);
}
static Bool close_screen(ScreenPtr s)
{struct ps2_geometry_screen *b=dixLookupPrivate(&s->devPrivates,&key);CloseScreenProcPtr lower=b->CloseScreen;ps2_geometry_screen_cleanup(b);return lower(s);}
