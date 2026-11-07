/* SPDX-License-Identifier: MIT */
/* GS sample counting replaces EXA's software-only geometric mask rasterizer. */
#include "ps2_render_geometry.h"
#include "ps2_render_raster.h"
#include "windowstr.h"
#include "dixstruct.h"
#include "damage.h"
#include "os.h"
#include <errno.h>
#include <limits.h>
#include <string.h>
static DevPrivateKeyRec key;
static struct ps2_render_geometry *owner(ScreenPtr screen)
{
    struct ps2_render_geometry *p=screen?dixLookupPrivate(&screen->devPrivates,&key):NULL;
    if(!p || !p->installed || !p->blit || !p->blit->attached)
        FatalError("PS2 GS: missing Render geometry owner\n");
    return p;
}
static void checked(int status)
{
    if(status)FatalError("PS2 GS: geometric coverage failed: %s; no software raster fallback\n",strerror(errno));
}
static PixmapPtr picture_pixmap(PicturePtr picture,int *xoff,int *yoff)
{
    *xoff=*yoff=0;
    if(!picture || !picture->pDrawable)return NULL;
    DrawablePtr d=picture->pDrawable;
    PixmapPtr p=d->type==DRAWABLE_PIXMAP?(PixmapPtr)d:
        d->type==DRAWABLE_WINDOW && d->pScreen->GetWindowPixmap?d->pScreen->GetWindowPixmap((WindowPtr)d):NULL;
    if(p && d->type==DRAWABLE_WINDOW){
        *xoff=d->x;*yoff=d->y;
#ifdef COMPOSITE
        *xoff-=p->screen_x;*yoff-=p->screen_y;
#endif
    }
    return p;
}
static Bool target(PicturePtr picture,struct ps2_raster_target *d,int *xoff,int *yoff)
{
    unsigned bits=PICT_FORMAT_BPP(picture->format);
    /* Pixman's public edge rasterizer has no 16/32-bit case. AddTraps
     * on these pictures is a no-op, rather than a colour composite. */
    if(bits!=1 && bits!=4 && bits!=8)return FALSE;
    PixmapPtr p=picture_pixmap(picture,xoff,yoff);
    if(!p || ps2_surface_exa_raster_target(p,d)!=1)
        FatalError("PS2 GS: geometric coverage target is not owned\n");
    d->depth=bits;return TRUE;
}
static void damaged(PicturePtr picture)
{
    exaMarkSync(picture->pDrawable->pScreen);
    RegionRec region;DrawablePtr d=picture->pDrawable;
    BoxRec box={d->x,d->y,d->x+d->width,d->y+d->height};
    RegionInit(&region,&box,1);DamageRegionAppend(d,&region);
    DamageRegionProcessPending(d);RegionUninit(&region);
}
static void rasterize(PicturePtr picture,xTrapezoid *trap,int xoff,int yoff)
{
    struct ps2_render_geometry *p=owner(picture->pDrawable->pScreen);
    struct ps2_raster_target d;int x,y;
    if(target(picture,&d,&x,&y)){
        checked(ps2_pipe_raster_trapezoids(p->blit->context,&d,xoff+x,yoff+y,(const pixman_trapezoid_t*)trap,1));
        damaged(picture);
    }
}
static void add_traps(PicturePtr picture,INT16 xoff,INT16 yoff,int count,xTrap *traps)
{
    struct ps2_render_geometry *p=owner(picture->pDrawable->pScreen);
    if(count<=0)return;
    struct ps2_raster_target d;int x,y;
    if(target(picture,&d,&x,&y)){
        checked(ps2_pipe_raster_traps(p->blit->context,&d,xoff+x,yoff+y,(const pixman_trap_t*)traps,(unsigned)count));
        damaged(picture);
    }
}
static void add_triangles(PicturePtr picture,INT16 xoff,INT16 yoff,int count,xTriangle *triangles)
{
    struct ps2_render_geometry *p=owner(picture->pDrawable->pScreen);
    if(count<=0)return;
    struct ps2_raster_target d;int x,y;
    if(target(picture,&d,&x,&y)){
        checked(ps2_pipe_raster_triangles(p->blit->context,&d,xoff+x,yoff+y,(const pixman_triangle_t*)triangles,(unsigned)count));
        damaged(picture);
    }
}
static int floor_fixed(int32_t value)
{int q=value/65536;return q-(value<0 && value%65536!=0);}
static int ceil_fixed(int32_t value)
{int q=value/65536;return q+(value>0 && value%65536!=0);}
static Bool bounded(unsigned op)
{
    switch(op){
    case PictOpDst:case PictOpOver:case PictOpOverReverse:case PictOpOutReverse:
    case PictOpAtop:case PictOpXor:case PictOpAdd:return TRUE;
    default:return FALSE;
    }
}
static Bool rectangle(CARD8 op,PicturePtr source,PicturePtr destination,
    INT16 sx,INT16 sy,const pixman_trapezoid_t *t)
{
    if(!pixman_trapezoid_valid(t) || (t->top&65535) || (t->bottom&65535) ||
       (t->left.p1.x&65535) || (t->right.p1.x&65535) ||
       t->left.p1.x!=t->left.p2.x || t->right.p1.x!=t->right.p2.x)return FALSE;
    int left=floor_fixed(t->left.p1.x),right=floor_fixed(t->right.p1.x),
        top=floor_fixed(t->top),bottom=floor_fixed(t->bottom);
    int width=destination->pDrawable->width,height=destination->pDrawable->height;
    if(!bounded(op) && (left>0 || top>0 || right<width || bottom<height))return FALSE;
    if(left<0)left=0;
    if(top<0)top=0;
    if(right>width)right=width;
    if(bottom>height)bottom=height;
    /* An integer rectangle has full coverage at every sample. The opaque
     * mask is the identity, so the established GS compositor can draw the
     * rectangle directly without allocating a display-sized temporary. */
    if(left<right && top<bottom)
        CompositePicture(op,source,NULL,destination,(INT16)(sx+left),(INT16)(sy+top),0,0,left,top,right-left,bottom-top);
    return TRUE;
}
static void point_bounds(int32_t x,int32_t y,int *left,int *top,int *right,int *bottom)
{
    int a=floor_fixed(x),b=ceil_fixed(x),c=floor_fixed(y),d=ceil_fixed(y);
    if(a<*left)*left=a;
    if(b>*right)*right=b;
    if(c<*top)*top=c;
    if(d>*bottom)*bottom=d;
}
static PicturePtr alpha_picture(ScreenPtr screen,unsigned bits,unsigned w,unsigned h)
{
    PictFormatPtr format=PictureMatchFormat(screen,bits,bits==1?PICT_a1:bits==4?PICT_x4a4:PICT_a8);
    if(!format)FatalError("PS2 GS: missing geometric alpha format\n");
    PixmapPtr pixmap=screen->CreatePixmap(screen,w,h,bits,0);
    if(!pixmap)return NULL;
    /* The native raw-pixmap constructor zero-initializes all protocol storage.
     * No EXA PrepareAccess or software PolyFillRect is needed to clear it. */
    struct ps2_raster_target target;
    if(ps2_surface_exa_raster_target(pixmap,&target)!=1){
        screen->DestroyPixmap(pixmap);FatalError("PS2 GS: unowned geometric mask\n");}
    int error;PicturePtr picture=CreatePicture(0,&pixmap->drawable,format,0,NULL,serverClient,&error);
    screen->DestroyPixmap(pixmap);return picture;
}
static void shapes(CARD8 op,PicturePtr source,PicturePtr destination,PictFormatPtr format,
    INT16 sx,INT16 sy,int count,const void *input,Bool triangle)
{
    if(count<=0 || op==PictOpDst)return;
    ScreenPtr screen=destination->pDrawable->pScreen;
    struct ps2_render_geometry *p=owner(screen);
    const pixman_triangle_t *tris=input;const pixman_trapezoid_t *traps=input;
    /* Render anchors source coordinates at the first input point, even when
     * an individual triangle is sorted or an invalid shape precedes it. */
    sx=(INT16)(sx-floor_fixed(triangle?tris[0].p1.x:traps[0].left.p1.x));
    sy=(INT16)(sy-floor_fixed(triangle?tris[0].p1.y:traps[0].left.p1.y));
    unsigned bits=format?PICT_FORMAT_A(format->format):destination->polyEdge==PolyEdgeSharp?1:8;
    if(bits!=1 && bits!=4)bits=8;
    unsigned stride=triangle?sizeof(*tris):sizeof(*traps);
    for(int first=0;first<count;){
        int n=format?count:1,left=0,top=0,right=destination->pDrawable->width,bottom=destination->pDrawable->height;
        const unsigned char *data=(const unsigned char*)input+(size_t)first*stride;
        if(!triangle && n==1 && rectangle(op,source,destination,sx,sy,(const pixman_trapezoid_t*)data)){
            first+=n;continue;
        }
        if(bounded(op)){
            left=top=INT_MAX;right=bottom=INT_MIN;
            for(int i=0;i<n;++i){
                if(triangle){const pixman_triangle_t *t=(const void*)(data+(size_t)i*stride);
                    point_bounds(t->p1.x,t->p1.y,&left,&top,&right,&bottom);
                    point_bounds(t->p2.x,t->p2.y,&left,&top,&right,&bottom);
                    point_bounds(t->p3.x,t->p3.y,&left,&top,&right,&bottom);
                }else{const pixman_trapezoid_t *t=(const void*)(data+(size_t)i*stride);
                    if(!pixman_trapezoid_valid(t))continue;
                    const pixman_line_fixed_t *lines[2]={&t->left,&t->right};
                    for(unsigned k=0;k<2;++k){
                        point_bounds(lines[k]->p1.x,t->top,&left,&top,&right,&bottom);
                        point_bounds(lines[k]->p2.x,t->bottom,&left,&top,&right,&bottom);
                    }
                }
            }
            if(left<0)left=0;
            if(top<0)top=0;
            if(right>destination->pDrawable->width)right=destination->pDrawable->width;
            if(bottom>destination->pDrawable->height)bottom=destination->pDrawable->height;
        }
        if(left<right && top<bottom){
            PicturePtr mask=alpha_picture(screen,bits,right-left,bottom-top);
            if(!mask)FatalError("PS2 GS: geometric mask allocation failed; no software raster fallback\n");
            struct ps2_raster_target target;PixmapPtr pixmap=(PixmapPtr)mask->pDrawable;
            if(ps2_surface_exa_raster_target(pixmap,&target)!=1)FatalError("PS2 GS: invalid geometric mask\n");
            checked(triangle?ps2_pipe_raster_triangles(p->blit->context,&target,-left,-top,(const pixman_triangle_t*)data,n):
                ps2_pipe_raster_trapezoids(p->blit->context,&target,-left,-top,(const pixman_trapezoid_t*)data,n));
            exaMarkSync(screen);
            CompositePicture(op,source,mask,destination,(INT16)(sx+left),(INT16)(sy+top),0,0,left,top,right-left,bottom-top);
            FreePicture(mask,0);
        }
        first+=n;
    }
}
static void trapezoids(CARD8 op,PicturePtr src,PicturePtr dst,PictFormatPtr format,
    INT16 sx,INT16 sy,int count,xTrapezoid *traps)
{shapes(op,src,dst,format,sx,sy,count,traps,FALSE);}
static void triangles(CARD8 op,PicturePtr src,PicturePtr dst,PictFormatPtr format,
    INT16 sx,INT16 sy,int count,xTriangle *tris)
{shapes(op,src,dst,format,sx,sy,count,tris,TRUE);}
static Bool close_screen(ScreenPtr screen)
{
    struct ps2_render_geometry *p=owner(screen);PictureScreenPtr pictures=GetPictureScreen(screen);
    if(pictures->Trapezoids!=trapezoids || pictures->Triangles!=triangles ||
       pictures->AddTraps!=add_traps || pictures->AddTriangles!=add_triangles || pictures->RasterizeTrapezoid!=rasterize)
        FatalError("PS2 GS: Render geometry close order\n");
    pictures->Trapezoids=p->trapezoids;pictures->Triangles=p->triangles;
    pictures->AddTraps=p->traps;pictures->AddTriangles=p->add_triangles;pictures->RasterizeTrapezoid=p->rasterize;
    screen->CloseScreen=p->close;p->installed=FALSE;dixSetPrivate(&screen->devPrivates,&key,NULL);
    struct pipe_context *ctx=p->blit->context;ps2_pipe_context_release(ctx);
    return screen->CloseScreen(screen);
}
Bool ps2_render_geometry_prepare(ScreenPtr screen,struct ps2_render_geometry *p,struct ps2_blit_exa *b)
{
    PictureScreenPtr pictures=screen?GetPictureScreen(screen):NULL;
    if(!screen || !p || p->installed || !b || !b->attached || b->screen!=screen || !pictures ||
       !screen->CloseScreen || !pictures->Trapezoids || !pictures->Triangles || !pictures->AddTraps ||
       !pictures->AddTriangles || !pictures->RasterizeTrapezoid ||
       !dixRegisterPrivateKey(&key,PRIVATE_SCREEN,0) || dixLookupPrivate(&screen->devPrivates,&key) ||
       ps2_pipe_context_acquire(b->context))return FALSE;
    *p=(struct ps2_render_geometry){.screen=screen,.blit=b,.trapezoids=pictures->Trapezoids,.triangles=pictures->Triangles,
        .traps=pictures->AddTraps,.add_triangles=pictures->AddTriangles,.rasterize=pictures->RasterizeTrapezoid,
        .close=screen->CloseScreen,.installed=TRUE};
    dixSetPrivate(&screen->devPrivates,&key,p);
    pictures->Trapezoids=trapezoids;pictures->Triangles=triangles;pictures->AddTraps=add_traps;
    pictures->AddTriangles=add_triangles;pictures->RasterizeTrapezoid=rasterize;
    screen->CloseScreen=close_screen;return TRUE;
}
