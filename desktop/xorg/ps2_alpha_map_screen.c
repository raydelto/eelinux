/* SPDX-License-Identifier: MIT */
#include "ps2_alpha_map_screen.h"
#include "mipict.h"
#include "windowstr.h"
#include "damage.h"
#include "os.h"
#include <string.h>
#include <stdint.h>

static DevPrivateKeyRec key;
static void composite(CARD8,PicturePtr,PicturePtr,PicturePtr,INT16,INT16,INT16,INT16,INT16,INT16,CARD16,CARD16);
static Bool close_screen(ScreenPtr);
static struct ps2_alpha_map_screen *owner(ScreenPtr screen)
{
    struct ps2_alpha_map_screen *p=screen?dixLookupPrivate(&screen->devPrivates,&key):NULL;
    if(!p || !p->installed || !p->blit->attached)FatalError("PS2 GS: missing alpha map Render owner\n");
    return p;
}
static PixmapPtr picture_pixmap(PicturePtr picture,int *xoff,int *yoff)
{
    *xoff=*yoff=0;
    if(!picture || !picture->pDrawable)return NULL;
    DrawablePtr d=picture->pDrawable;
    PixmapPtr pixmap=d->type==DRAWABLE_PIXMAP?(PixmapPtr)d:
        d->type==DRAWABLE_WINDOW && d->pScreen->GetWindowPixmap?
            d->pScreen->GetWindowPixmap((WindowPtr)d):NULL;
#ifdef COMPOSITE
    if(pixmap && d->type==DRAWABLE_WINDOW){*xoff=-pixmap->screen_x;*yoff=-pixmap->screen_y;}
#endif
    return pixmap;
}
static void point(DDXPointPtr p,int32_t x,int32_t y)
{
    if(x<INT16_MIN || x>INT16_MAX || y<INT16_MIN || y>INT16_MAX)
        FatalError("PS2 GS: alpha map clip coordinates exceed region bounds\n");
    p->x=x;p->y=y;
}
static PictureRec region_picture(PicturePtr picture,Bool destination)
{
    PictureRec copy=*picture;
    int32_t x=picture->pDrawable?picture->pDrawable->x:0,y=picture->pDrawable?picture->pDrawable->y:0;
    if(copy.alphaMap){
        /* mi's target-map clip translates by negative alphaOrigin;
         * source-map clipping translates by positive alphaOrigin.
         * Convert the protocol's drawable-relative map position to its
         * global region position without changing the real Picture. */
        int32_t ax=copy.alphaOrigin.x+x,ay=copy.alphaOrigin.y+y;
        point(&copy.alphaOrigin,destination?-ax:ax,destination?-ay:ay);
    }
    if(!destination && copy.clientClip)point(&copy.clipOrigin,copy.clipOrigin.x+x,copy.clipOrigin.y+y);
    return copy;
}
static Bool borrowed_bitmap(PicturePtr picture)
{
    if(!picture || !picture->pDrawable || picture->pDrawable->type!=DRAWABLE_PIXMAP)return FALSE;
    struct ps2_alpha_source bytes;
    return ps2_surface_exa_borrowed_source((PixmapPtr)picture->pDrawable,&bytes)==1;
}
static void composite(CARD8 op,PicturePtr source,PicturePtr mask,PicturePtr destination,
    INT16 sx,INT16 sy,INT16 mx,INT16 my,INT16 dx,INT16 dy,CARD16 width,CARD16 height)
{
    if(!source || !destination || !destination->pDrawable)FatalError("PS2 GS: invalid Render dispatch\n");
    /* Transform, repeat and filter describe picture sampling. Render ignores
     * them on a destination, including when that Picture is also the source.
     * Keep the original source state and normalize only the destination view
     * for the native/EXA validation gates. The view lives through DoneComposite. */
    PictureRec target;
    if(destination->transform || destination->repeat || destination->filter!=PictFilterNearest ||
       destination->filter_nparams){
        target=*destination;target.transform=NULL;target.repeat=FALSE;target.repeatType=RepeatNone;
        target.filter=PictFilterNearest;target.filter_nparams=0;target.filter_params=NULL;
        destination=&target;
    }
    ScreenPtr screen=destination->pDrawable->pScreen;
    struct ps2_alpha_map_screen *p=owner(screen);
    PictureScreenPtr pictures=GetPictureScreen(screen);
    struct ps2_blit_exa *b=p->blit;
    if(b->alpha_dispatch || pictures->Composite!=composite)FatalError("PS2 GS: recursive alpha map dispatch\n");
    /* EXA 2.6 leaves source pixmap offsets uninitialized for procedural
     * sources. Use our initialized drawable offsets for gradients too. */
    Bool procedural=(!source->pDrawable && source->pSourcePict && source->pSourcePict->type!=SourcePictTypeSolidFill) ||
        (mask && !mask->pDrawable && mask->pSourcePict && mask->pSourcePict->type!=SourcePictTypeSolidFill);
    Bool window=(source->pDrawable && source->pDrawable->type==DRAWABLE_WINDOW) ||
        (mask && mask->pDrawable && mask->pDrawable->type==DRAWABLE_WINDOW);
    Bool direct=source->alphaMap || (mask && mask->alphaMap) || destination->alphaMap || procedural || window ||
        borrowed_bitmap(source) || borrowed_bitmap(mask);
    if(direct){
        RegionRec region;
        /* Match EXA's global drawable coordinates. mi performs clipping
         * and SourceValidate, not pixels. Keep maps attached so both their
         * clipping and native GS fetch descriptors remain explicit. */
        INT16 xs=sx+(source->pDrawable?source->pDrawable->x:0),ys=sy+(source->pDrawable?source->pDrawable->y:0);
        INT16 xm=mx+(mask && mask->pDrawable?mask->pDrawable->x:0),ym=my+(mask && mask->pDrawable?mask->pDrawable->y:0);
        INT16 xd=dx+destination->pDrawable->x,yd=dy+destination->pDrawable->y;
        PictureRec source_region=region_picture(source,FALSE),destination_region=region_picture(destination,TRUE);
        PictureRec mask_region;if(mask)mask_region=region_picture(mask,FALSE);
        if(!miComputeCompositeRegion(&region,&source_region,mask?&mask_region:NULL,&destination_region,
            xs,ys,xm,ym,xd,yd,width,height))return;
        int sox,soy,mox,moy,dox,doy;
        PixmapPtr sp=picture_pixmap(source,&sox,&soy),mp=picture_pixmap(mask,&mox,&moy),
            dp=picture_pixmap(destination,&dox,&doy);
        if(!dp || (source->pDrawable && !sp) || (mask && mask->pDrawable && !mp) ||
            !b->exa->driver.CheckComposite(op,source,mask,destination) ||
            !b->exa->driver.PrepareComposite(op,source,mask,destination,sp,mp,dp))
            FatalError("PS2 GS: alpha map request unsupported or unavailable; no software raster fallback\n");
        b->alpha_dispatch=TRUE;
        unsigned count=RegionNumRects(&region);BoxPtr boxes=RegionRects(&region);
        for(unsigned i=0;i<count;++i){const BoxRec *box=boxes+i;
            b->exa->driver.Composite(dp,box->x1+xs-xd+sox,box->y1+ys-yd+soy,
                box->x1+xm-xd+mox,box->y1+ym-yd+moy,box->x1+dox,box->y1+doy,
                box->x2-box->x1,box->y2-box->y1);
        }
        b->exa->driver.DoneComposite(dp);b->alpha_dispatch=FALSE;exaMarkSync(screen);
        DamageRegionAppend(destination->pDrawable,&region);DamageRegionProcessPending(destination->pDrawable);
        if(destination->alphaMap){
            RegionTranslate(&region,-destination->pDrawable->x-destination->alphaOrigin.x,
                -destination->pDrawable->y-destination->alphaOrigin.y);
            DamageRegionAppend(destination->alphaMap->pDrawable,&region);
            DamageRegionProcessPending(destination->alphaMap->pDrawable);
        }
        RegionUninit(&region);return;
    }
    pictures->Composite=p->composite;
    pictures->Composite(op,source,mask,destination,sx,sy,mx,my,dx,dy,width,height);
    p->composite=pictures->Composite;pictures->Composite=composite;
}
static Bool close_screen(ScreenPtr screen)
{
    struct ps2_alpha_map_screen *p=owner(screen);
    PictureScreenPtr pictures=GetPictureScreen(screen);
    if(pictures->Composite!=composite || pictures->Glyphs!=miGlyphs || p->blit->alpha_dispatch)FatalError("PS2 GS: alpha map close order\n");
    pictures->Composite=p->composite;pictures->Glyphs=p->glyphs;screen->CloseScreen=p->close;p->installed=FALSE;
    dixSetPrivate(&screen->devPrivates,&key,NULL);
    return screen->CloseScreen(screen);
}
Bool ps2_alpha_map_screen_prepare(ScreenPtr screen,struct ps2_alpha_map_screen *p,struct ps2_blit_exa *b)
{
    PictureScreenPtr pictures=screen?GetPictureScreen(screen):NULL;
    if(!screen || !p || p->installed || !b || !b->attached || b->screen!=screen || !pictures ||
        !pictures->Composite || pictures->Composite==composite || !screen->CloseScreen ||
        !dixRegisterPrivateKey(&key,PRIVATE_SCREEN,0) || dixLookupPrivate(&screen->devPrivates,&key))return FALSE;
    *p=(struct ps2_alpha_map_screen){.screen=screen,.blit=b,.composite=pictures->Composite,.glyphs=pictures->Glyphs,
        .close=screen->CloseScreen,.installed=TRUE};
    dixSetPrivate(&screen->devPrivates,&key,p);
    /* The pinned MI glyph dispatcher only computes positions, creates and
     * clears mask pictures through our native GC, and calls CompositePicture.
     * Both individual glyphs and overlapping aggregate masks therefore stay
     * on GS. EXA's separate CPU-written1024x320 glyph cache is not used. */
    pictures->Glyphs=miGlyphs;
    pictures->Composite=composite;screen->CloseScreen=close_screen;return TRUE;
}
