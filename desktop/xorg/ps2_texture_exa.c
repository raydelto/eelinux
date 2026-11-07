/* SPDX-License-Identifier: MIT */
#include "ps2_texture_exa.h"
#include "os.h"
#include <string.h>
static DevPrivateKeyRec texture_key;
static struct ps2_texture_exa *backend(ScreenPtr screen)
{ return dixLookupPrivate(&screen->devPrivates,&texture_key); }
static const struct ps2_surface *storage(struct ps2_texture_exa *b,PixmapPtr p)
{
    if (!p || !b->exa->driver.PixmapIsOffscreen(p)) return NULL;
    uint32_t handle=0;
    if (p!=p->drawable.pScreen->GetScreenPixmap(p->drawable.pScreen)) {
        /* CreatePixmap2 in the owned EXA storage component returns a private
         * whose first uint32_t is the opaque kernel handle, never a GS address. */
        const void *private=exaGetPixmapDriverPrivate(p);
        if (!private) return NULL;
        memcpy(&handle,private,sizeof(handle));
    }
    return ps2_surface_lookup(b->render->gs,handle);
}
static Bool picture(PicturePtr p)
{
    return p && p->pDrawable && !p->alphaMap && !p->transform && !p->repeat &&
        p->filter==PictFilterNearest && !p->filter_nparams &&
        ((p->format==PICT_x8b8g8r8 && p->pDrawable->bitsPerPixel==32 && p->pDrawable->depth==24) ||
         (p->format==PICT_x1b5g5r5 && p->pDrawable->bitsPerPixel==16 && p->pDrawable->depth==15));
}
static Bool check(int op,PicturePtr src,PicturePtr mask,PicturePtr dst)
{
    return op==PictOpSrc && !mask && picture(src) && picture(dst) &&
        src->pDrawable->pScreen==dst->pDrawable->pScreen &&
        src->pDrawable->width<=1024 && src->pDrawable->height<=1024;
}
static Bool prepare(int op,PicturePtr src,PicturePtr mask,PicturePtr dst,
    PixmapPtr source,PixmapPtr mask_pixmap,PixmapPtr destination)
{
    if (!check(op,src,mask,dst) || mask_pixmap || !source || !destination ||
        source->drawable.pScreen!=destination->drawable.pScreen) return FALSE;
    struct ps2_texture_exa *b=backend(destination->drawable.pScreen);
    const struct ps2_surface *s=storage(b,source),*d=storage(b,destination);
    if (!s || !d || !s->handle || s==d || s->width>1024 || s->height>1024 || b->render->gs->error) return FALSE;
    b->source=s->handle; b->target=d->handle; return TRUE;
}
static uint32_t bits(float v) { uint32_t result; memcpy(&result,&v,sizeof(result)); return result; }
static unsigned padded(unsigned size) { unsigned value=1; while(value<size) value*=2; return value; }
static void composite(PixmapPtr dst,int sx,int sy,int mx,int my,int dx,int dy,int width,int height)
{
    (void)mx; (void)my;
    if (width<=0 || height<=0) return;
    struct ps2_texture_exa *b=backend(dst->drawable.pScreen);
    const struct ps2_surface *s=ps2_surface_lookup(b->render->gs,b->source), *d=storage(b,dst);
    if (!s || !d || d->handle!=b->target || sx<0 || sy<0 || dx<0 || dy<0 ||
        (unsigned)sx>=s->width || (unsigned)sy>=s->height || (unsigned)dx>=d->width || (unsigned)dy>=d->height ||
        (unsigned)width>s->width-(unsigned)sx || (unsigned)height>s->height-(unsigned)sy ||
        (unsigned)width>d->width-(unsigned)dx || (unsigned)height>d->height-(unsigned)dy)
        FatalError("PS2 GS: invalid or unclipped textured X composite\n");
    uint32_t old_target=b->render->target, old_depth=b->render->depth;
    if (ps2_render_bind(b->render,d->handle,0)) FatalError("PS2 GS: composite target bind failed\n");
    unsigned x[4]={dx,dx+width,dx+width,dx},y[4]={dy,dy,dy+height,dy+height};
    unsigned u[4]={sx,sx+width,sx+width,sx},v[4]={sy,sy,sy+height,sy+height};
    unsigned indices[2][3]={{0,1,2},{0,2,3}};
    struct ps2gs_texture_triangle triangles[2]={0};
    for (unsigned i=0;i<2;++i) for (unsigned j=0;j<3;++j) {
        unsigned k=indices[i][j];
        triangles[i].v[j]=(struct ps2gs_texture_vertex){.position={.x=x[k]*16,.y=y[k]*16,.rgba=0x80808080},
            .s=bits((float)u[k]/padded(s->width)),.t=bits((float)v[k]/padded(s->height)),.q=0x3f800000};
    }
    if (ps2_texture_triangles(b->render,s->handle,triangles,2) || ps2_render_bind(b->render,old_target,old_depth))
        FatalError("PS2 GS: textured X composite failed; hardware work is not replayed\n");
}
static void done(PixmapPtr p) { (void)p; }
Bool ps2_texture_exa_prepare(ScreenPtr screen,struct ps2_texture_exa *b,
    struct ps2_surface_exa *exa,struct ps2_render_client *render)
{
    if (!screen || !b || !exa || !render || render->gs!=exa->gs || render->gs->error ||
        !(render->gs->caps.features&PS2GS_HW_TEXTURE) || !exa->driver.PixmapIsOffscreen ||
        exa->driver.PrepareComposite) return FALSE;
    if (!dixRegisterPrivateKey(&texture_key,PRIVATE_SCREEN,0)) return FALSE;
    memset(b,0,sizeof(*b)); b->exa=exa; b->render=render; dixSetPrivate(&screen->devPrivates,&texture_key,b);
    exa->driver.CheckComposite=check; exa->driver.PrepareComposite=prepare;
    exa->driver.Composite=composite; exa->driver.DoneComposite=done; return TRUE;
}
