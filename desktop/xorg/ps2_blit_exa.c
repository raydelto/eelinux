/* SPDX-License-Identifier: MIT */
#include "ps2_blit_exa.h"
#include "os.h"
#include "servermd.h"
#include "windowstr.h"
#include "ps2_tile.h"
#include "ps2_gs_lut.h"
#include "ps2_gs_logic.h"
#include "ps2-gs-map.h"
#include "ps2-gs-map-blit.h"
#include "ps2-gs-rgba-transfer.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
static DevPrivateKeyRec key;
static struct ps2_blit_exa *backend(ScreenPtr screen)
{ return dixLookupPrivate(&screen->devPrivates, &key); }
static const struct ps2_surface *storage(PixmapPtr pixmap, struct ps2_blit_exa *b)
{
    if (!pixmap || pixmap->drawable.pScreen != b->screen || b->exa->gs != b->gs) return NULL;
    uint32_t handle = 0;
    if (pixmap != b->screen->GetScreenPixmap(b->screen)) {
        const uint32_t *private = exaGetPixmapDriverPrivate(pixmap);
        if (!private || !*private) return NULL;
        handle = *private;
    }
    const struct ps2_surface *s = ps2_surface_lookup(b->gs, handle);
    if (!s || (s->bits_per_pixel != 16 && s->bits_per_pixel != 32) ||
        pixmap->drawable.width != s->width || pixmap->drawable.height != s->height ||
        pixmap->drawable.bitsPerPixel != s->bits_per_pixel ||
        pixmap->drawable.depth != ps2_surface_depth(s)) return NULL;
    return s;
}
static Bool same_storage(const struct ps2_surface *a, const struct ps2_surface *b)
{
    return a && b && a->handle == b->handle && a->width == b->width && a->height == b->height &&
        a->bits_per_pixel == b->bits_per_pixel && a->buffer_width == b->buffer_width && a->bytes == b->bytes && a->pixel_depth==b->pixel_depth;
}
static PixmapPtr picture_pixmap(PicturePtr p)
{
    DrawablePtr d=p?p->pDrawable:NULL;
    if(!d || !d->pScreen)return NULL;
    if(d->type==DRAWABLE_PIXMAP)return (PixmapPtr)d;
    return d->type==DRAWABLE_WINDOW && d->pScreen->GetWindowPixmap?
        d->pScreen->GetWindowPixmap((WindowPtr)d):NULL;
}
static Bool same_backing(PixmapPtr a,PixmapPtr c,struct ps2_blit_exa *b)
{
    if(!b || !a || !c || a->drawable.type!=DRAWABLE_PIXMAP || c->drawable.type!=DRAWABLE_PIXMAP ||
        a->drawable.pScreen!=b->screen || c->drawable.pScreen!=b->screen)return FALSE;
    const struct ps2_surface *sa=storage(a,b),*sc=storage(c,b);
    if(sa || sc)return same_storage(sa,sc);
    struct ps2_alpha_source aa,ac;
    return ps2_surface_exa_alpha_source(a,&aa)==1 && ps2_surface_exa_alpha_source(c,&ac)==1 &&
        aa.bits==ac.bits && aa.bytes==ac.bytes && aa.stride==ac.stride && aa.width==ac.width &&
        aa.height==ac.height && aa.bits_per_pixel==ac.bits_per_pixel && aa.depth==ac.depth;
}
static Bool supported_filter(PicturePtr p)
{
    if(p->filter==PictFilterNearest || p->filter==PictFilterBilinear)return !p->filter_nparams;
    if(p->filter!=PictFilterConvolution || !p->filter_params || p->filter_nparams<3 || p->filter_nparams>262144 ||
        (uintptr_t)p->filter_params>UINTPTR_MAX-(size_t)p->filter_nparams*4+1)return FALSE;
    int32_t w=p->filter_params[0],h=p->filter_params[1];
    return w>0 && h>0 && w<=2048*65536 && h<=2048*65536 && !(w&65535) && !(h&65535) &&
        (uint64_t)(w/65536)*(h/65536)+2==(unsigned)p->filter_nparams;
}
static unsigned packed10(PicturePtr p)
{
    if(!p || !p->pDrawable || (p->pDrawable->depth!=30 && p->pDrawable->depth!=32) || p->pDrawable->bitsPerPixel!=32)return 0;
    if(p->pDrawable->depth==30 && p->format!=PICT_x2r10g10b10 && p->format!=PICT_x2b10g10r10)return 0;
    switch(p->format){case PICT_a2r10g10b10:return PS2_PACKED10_ARGB;case PICT_a2b10g10r10:return PS2_PACKED10_ABGR;
    case PICT_x2r10g10b10:return PS2_PACKED10_RGB;case PICT_x2b10g10r10:return PS2_PACKED10_BGR;default:return 0;}
}
static int byte_format(PicturePtr p)
{
    if(!p || !p->pDrawable)return -1;
    if(p->pDrawable->depth==16 && p->pDrawable->bitsPerPixel==16){
        switch(p->format){
        case PICT_r5g6b5:return PS2_WORD_RGB565;
        case PICT_b5g6r5:return PS2_WORD_BGR565;
        case PICT_a1r5g5b5:return PS2_WORD_ARGB1555;
        case PICT_a1b5g5r5:return PS2_WORD_ABGR1555;
        case PICT_a4r4g4b4:return PS2_WORD_ARGB4444;
        case PICT_a4b4g4r4:return PS2_WORD_ABGR4444;
        case PICT_x1r5g5b5:return PS2_WORD_RGB555;
        case PICT_x1b5g5r5:return PS2_WORD_BGR555;
        case PICT_x4r4g4b4:return PS2_WORD_RGB444;
        case PICT_x4b4g4r4:return PS2_WORD_BGR444;
        default:return -1;
        }
    }
    if(p->pDrawable->depth!=8 || p->pDrawable->bitsPerPixel!=8)return -1;
    switch(p->format){
    case PICT_r3g3b2:return PS2_BYTE_RGB332;
    case PICT_b2g3r3:return PS2_BYTE_BGR233;
    case PICT_a2r2g2b2:return PS2_BYTE_ARGB2222;
    case PICT_a2b2g2r2:return PS2_BYTE_ABGR2222;
    default:return -1;
    }
}
static void gradient_geometry(PicturePtr p,struct ps2_gradient *out)
{
    *out=(struct ps2_gradient){.kind=p->pSourcePict->type,.exclude_opaque=!!p->componentAlpha};
    if(out->kind==SourcePictTypeLinear){const PictLinearGradient *g=&p->pSourcePict->linear;
        out->points[0]=g->p1.x;out->points[1]=g->p1.y;out->points[2]=g->p2.x;out->points[3]=g->p2.y;
    }else if(out->kind==SourcePictTypeRadial){const PictRadialGradient *g=&p->pSourcePict->radial;
        out->points[0]=g->c1.x;out->points[1]=g->c1.y;out->points[2]=g->c2.x;out->points[3]=g->c2.y;
        out->radii[0]=g->c1.radius;out->radii[1]=g->c2.radius;
    }else{out->points[0]=p->pSourcePict->conical.center.x;out->points[1]=p->pSourcePict->conical.center.y;
        out->angle=p->pSourcePict->conical.angle;}
}
static Bool gradient_picture(PicturePtr p)
{
    if(!p || p->pDrawable || !p->pSourcePict || p->pSourcePict->type<SourcePictTypeLinear ||
        p->pSourcePict->type>SourcePictTypeConical || p->alphaMap || p->repeatType>RepeatReflect || !supported_filter(p))return FALSE;
    const PictGradient *g=&p->pSourcePict->gradient;
    if(!g->stops || g->nstops<1 || g->nstops>4096)return FALSE;
    for(int i=0;i<g->nstops;++i)if(g->stops[i].x<0 || g->stops[i].x>65536 ||
        (i && g->stops[i].x<g->stops[i-1].x))return FALSE;
    const struct ps2_gradient_stop zero={0};struct ps2_gradient shape;gradient_geometry(p,&shape);
    shape.nstops=1;shape.stops=&zero;return ps2_gradient_valid(&shape);
}
static Bool gradient_sample(struct ps2_blit_exa *b,PicturePtr p,struct ps2_picture_sample *out,unsigned slot)
{
    const PictGradient *g=&p->pSourcePict->gradient;
    struct ps2_gradient_stop *stops=malloc((size_t)g->nstops*sizeof(*stops));
    if(!stops)return FALSE;
    for(int i=0;i<g->nstops;++i){const PictGradientStop *s=g->stops+i;
        stops[i]=(struct ps2_gradient_stop){.x=s->x,.rgba={s->color.red,s->color.green,s->color.blue,s->color.alpha}};
    }
    b->gradient_stops[slot]=stops;
    *out=(struct ps2_picture_sample){.transformed=!!p->transform,.repeat=p->repeat?p->repeatType:RepeatNone,
        .source_argb=1,.bilinear=p->filter==PictFilterBilinear,.convolution=p->filter==PictFilterConvolution,
        .filter_params=p->filter_params,.filter_nparams=p->filter_nparams};
    gradient_geometry(p,&out->gradient);out->gradient.nstops=g->nstops;out->gradient.stops=stops;
    if(p->transform)memcpy(out->matrix,p->transform->matrix,sizeof(out->matrix));
    return TRUE;
}
static Bool window_matrix(PicturePtr p,int32_t out[3][3])
{
    memcpy(out,p->transform->matrix,sizeof(p->transform->matrix));
    if(p->pDrawable->type!=DRAWABLE_WINDOW)return TRUE;
    PixmapPtr pixmap=picture_pixmap(p);if(!pixmap)return FALSE;
    int64_t x=p->pDrawable->x,y=p->pDrawable->y;
#ifdef COMPOSITE
    x-=pixmap->screen_x;y-=pixmap->screen_y;
#endif
    /* Composite callbacks address backing-pixmap pixels. The Picture matrix
     * addresses window-relative input/output, so translate both sides:
     * T_backing = translate(origin) * T_window * translate(-origin).
     * Integer translations are exact. Wide intermediates preserve cancelling
     * terms even when an individual translated matrix exceeds 16.16 range. */
    int64_t shifted[3][3],result[3][3];
    for(unsigned row=0;row<3;++row){
        shifted[row][0]=out[row][0];shifted[row][1]=out[row][1];
        int64_t a,b,c;
        if(__builtin_mul_overflow(x,(int64_t)out[row][0],&a) ||
           __builtin_mul_overflow(y,(int64_t)out[row][1],&b) ||
           __builtin_sub_overflow((int64_t)out[row][2],a,&c) ||
           __builtin_sub_overflow(c,b,&shifted[row][2]))return FALSE;
    }
    for(unsigned column=0;column<3;++column){
        int64_t a,b;
        if(__builtin_mul_overflow(x,shifted[2][column],&a) ||
           __builtin_mul_overflow(y,shifted[2][column],&b) ||
           __builtin_add_overflow(shifted[0][column],a,&result[0][column]) ||
           __builtin_add_overflow(shifted[1][column],b,&result[1][column]))return FALSE;
        result[2][column]=shifted[2][column];
    }
    for(unsigned row=0;row<3;++row)for(unsigned column=0;column<3;++column){
        if(result[row][column]<INT32_MIN || result[row][column]>INT32_MAX)return FALSE;
        out[row][column]=(int32_t)result[row][column];
    }
    return TRUE;
}
static Bool sampled_drawable(PicturePtr p)
{
    if(!p->transform && !p->repeat)return TRUE;
    PixmapPtr pixmap=picture_pixmap(p);
    if(!pixmap || pixmap->drawable.x || pixmap->drawable.y ||
       pixmap->drawable.width>1024 || pixmap->drawable.height>1024)return FALSE;
    if(p->transform){int32_t matrix[3][3];return window_matrix(p,matrix);}
    return TRUE;
}
static Bool picture(PicturePtr p, Bool source)
{
    if(source && gradient_picture(p))return TRUE;
    if(source && p && !p->pDrawable && !p->alphaMap && p->pSourcePict &&
        p->pSourcePict->type==SourcePictTypeSolidFill){
        uint32_t c=p->pSourcePict->solidFill.color,a=c>>24;
        return ((c>>16)&255)<=a && ((c>>8)&255)<=a && (c&255)<=a;
    }
    return p && p->pDrawable &&
        (source ? p->repeatType<=RepeatReflect && sampled_drawable(p) :
            !p->transform && !p->repeat) &&
        (source?supported_filter(p):p->filter==PictFilterNearest && !p->filter_nparams) &&
        (packed10(p) || byte_format(p)>=0 || ((p->format == PICT_x8b8g8r8 || p->format == PICT_x8r8g8b8) && p->pDrawable->bitsPerPixel == 32 &&
          (p->pDrawable->depth == 24 || p->pDrawable->depth == 30 || p->pDrawable->depth == 32)) ||
         ((p->format == PICT_x1b5g5r5 || p->format == PICT_x1r5g5b5 || p->format==PICT_x4b4g4r4 ||
           p->format==PICT_x4r4g4b4) && p->pDrawable->bitsPerPixel == 16 && p->pDrawable->depth == 15) ||
         ((p->format == PICT_a8b8g8r8 || p->format == PICT_a8r8g8b8 || p->format == PICT_b8g8r8a8 ||
           p->format == PICT_b8g8r8x8) && p->pDrawable->bitsPerPixel == 32 && p->pDrawable->depth == 32));
}
static Bool rgb444(PicturePtr p)
{return p && p->pDrawable && p->pDrawable->depth==15 && (p->format==PICT_x4b4g4r4 || p->format==PICT_x4r4g4b4);}
static Bool argb(PicturePtr p)
{return p && p->pDrawable && (packed10(p)==PS2_PACKED10_ARGB || packed10(p)==PS2_PACKED10_ABGR || p->format==PICT_a8b8g8r8 || p->format==PICT_a8r8g8b8 || p->format==PICT_b8g8r8a8 ||
    p->format==PICT_a2r2g2b2 || p->format==PICT_a2b2g2r2 || p->format==PICT_a1r5g5b5 ||
    p->format==PICT_a1b5g5r5 || p->format==PICT_a4r4g4b4 || p->format==PICT_a4b4g4r4);}
static Bool rotated(PicturePtr p)
{return p && p->pDrawable && (p->format==PICT_b8g8r8a8 || p->format==PICT_b8g8r8x8);}
static Bool bitmap_picture(PicturePtr p)
{
    return p && p->pDrawable && p->pDrawable->type==DRAWABLE_PIXMAP &&
        !p->pDrawable->x && !p->pDrawable->y &&
        ((p->pDrawable->depth==1 && p->pDrawable->bitsPerPixel==1 && p->format==PICT_a1) ||
         ((p->pDrawable->depth==4 || p->pDrawable->depth==8) && p->pDrawable->bitsPerPixel==8 && p->format==PICT_x4a4) ||
         (p->pDrawable->depth==8 && p->pDrawable->bitsPerPixel==8 && p->format==PICT_a8)) &&
        p->repeatType<=RepeatReflect && supported_filter(p);
}
static PicturePtr alpha_map(PicturePtr p)
{
    if(!p)return NULL;
    if(p->alphaMap)return p->alphaMap;
    struct ps2_blit_exa *b=p->pDrawable?backend(p->pDrawable->pScreen):NULL;
    if(b && b->alpha_dispatch)for(unsigned i=0;i<3;++i)
        if(b->alpha_pictures[i]==p)return b->alpha_maps[i];
    return NULL;
}
static int native15_format(PicturePtr p)
{
    if(!p || !p->pDrawable || p->pDrawable->depth!=15 || p->pDrawable->bitsPerPixel!=16)return -1;
    switch(p->format){
    case PICT_x1r5g5b5:return PS2_WORD_RGB555;
    case PICT_x1b5g5r5:return PS2_WORD_BGR555;
    case PICT_x4r4g4b4:return PS2_WORD_RGB444;
    case PICT_x4b4g4r4:return PS2_WORD_BGR444;
    default:return -1;
    }
}
static Bool map_alias(PicturePtr p,PixmapPtr pixmap,struct ps2_blit_exa *b)
{
    PicturePtr m=alpha_map(p);
    return m && m->pDrawable && m->pDrawable->type==DRAWABLE_PIXMAP &&
        same_backing(pixmap,(PixmapPtr)m->pDrawable,b);
}
static Bool native15_alias(PicturePtr p,PixmapPtr pixmap,struct ps2_blit_exa *b)
{
    PicturePtr m=alpha_map(p);
    return m && m->format!=p->format && native15_format(p)>=0 && native15_format(m)>=0 &&
        map_alias(p,pixmap,b);
}
static Bool alpha_map_picture(PicturePtr p)
{
    PicturePtr m=alpha_map(p);
    if(!m)return TRUE;
    if(!p->pDrawable || !m->pDrawable || m->pDrawable->type!=DRAWABLE_PIXMAP ||
        m->pDrawable->pScreen!=p->pDrawable->pScreen)return FALSE;
    PictureRec plain=*m;plain.alphaMap=NULL;plain.transform=NULL;plain.repeat=FALSE;
    plain.filter=PictFilterNearest;plain.filter_nparams=0;
    return bitmap_picture(&plain) || picture(&plain,FALSE);
}
static struct ps2_unorm_layout precision(PicturePtr p)
{
    return (struct ps2_unorm_layout){.rgb_bits={PICT_FORMAT_R(p->format),PICT_FORMAT_G(p->format),PICT_FORMAT_B(p->format)},
        .alpha_bits=PICT_FORMAT_A(p->format)};
}
static Bool sample(PicturePtr p,struct ps2_picture_sample *out)
{
    struct ps2_picture_sample s={.transformed=!!p->transform,.repeat=p->repeat?p->repeatType:RepeatNone,
        .bilinear=p->filter==PictFilterBilinear,.convolution=p->filter==PictFilterConvolution,
        .filter_params=p->filter_params,.filter_nparams=p->filter_nparams,.source_unorm=precision(p)};
    if(p->transform && !window_matrix(p,s.matrix))return FALSE;
    *out=s;return TRUE;
}
static int canonical_operation(int op)
{
    switch(op){
    case PictOpDisjointClear:case PictOpConjointClear:return PictOpClear;
    case PictOpDisjointSrc:case PictOpConjointSrc:return PictOpSrc;
    case PictOpDisjointDst:case PictOpConjointDst:return PictOpDst;
    default:return op;
    }
}
static int masked_operation(int op,PicturePtr source,PicturePtr mask,PicturePtr destination)
{
    /* A float HSL/CA combiner leaves its destination planes unchanged, but
     * its iterator still stores those planes through a writable alpha map.
     * Preserve that opcode so the packed path can publish both formats. */
    if(op>=PictOpHSLHue && op<=PictOpHSLLuminosity && mask && mask->componentAlpha &&
        !(alpha_map(destination) && (packed10(source) || packed10(mask) || packed10(destination) ||
          packed10(alpha_map(source)) || packed10(alpha_map(mask)) || packed10(alpha_map(destination)))))return PictOpDst;
    return canonical_operation(op);
}
static Bool ratio_operation(int op)
{return (op>=PictOpDisjointOver && op<=PictOpDisjointXor) ||
    (op>=PictOpConjointOver && op<=PictOpConjointXor);}
static Bool wide_operation(int op)
{return op==PictOpSaturate || ratio_operation(op) || op==PictOpColorDodge || op==PictOpColorBurn || op==PictOpSoftLight ||
    (op>=PictOpHSLHue && op<=PictOpHSLLuminosity);}
static Bool solid_mask_operation(int op)
{
    return (op>=PictOpClear && op<=PictOpDst) || wide_operation(op) || op==PictOpScreen ||
        op==PictOpExclusion || op==PictOpMultiply || op==PictOpDarken || op==PictOpLighten ||
        op==PictOpDifference || op==PictOpOverlay || op==PictOpHardLight;
}
static int operation(int op,Bool destination_argb)
{
    op=canonical_operation(op);
    if(op==PictOpColorDodge)return PS2_COMPOSITE_COLOR_DODGE;
    if(op==PictOpColorBurn)return PS2_COMPOSITE_COLOR_BURN;
    if(op==PictOpSoftLight)return PS2_COMPOSITE_SOFTLIGHT;
    if(op>=PictOpHSLHue && op<=PictOpHSLLuminosity)return PS2_COMPOSITE_HSL_HUE+op-PictOpHSLHue;
    if(op>=PictOpDisjointOver && op<=PictOpDisjointXor){
        static const int operations[]={PS2_COMPOSITE_DISJOINT_OVER,PS2_COMPOSITE_DISJOINT_OVER_REVERSE,
            PS2_COMPOSITE_DISJOINT_IN,PS2_COMPOSITE_DISJOINT_IN_REVERSE,
            PS2_COMPOSITE_DISJOINT_OUT,PS2_COMPOSITE_DISJOINT_OUT_REVERSE,
            PS2_COMPOSITE_DISJOINT_ATOP,PS2_COMPOSITE_DISJOINT_ATOP_REVERSE,PS2_COMPOSITE_DISJOINT_XOR};
        return operations[op-PictOpDisjointOver];
    }
    if(op>=PictOpConjointOver && op<=PictOpConjointXor)
        return PS2_COMPOSITE_CONJOINT_OVER+op-PictOpConjointOver;
    if(op==PictOpScreen)return PS2_COMPOSITE_SCREEN;
    if(op==PictOpExclusion)return PS2_COMPOSITE_EXCLUSION;
    if(op==PictOpMultiply)return PS2_COMPOSITE_MULTIPLY;
    if(op==PictOpDarken)return PS2_COMPOSITE_DARKEN;
    if(op==PictOpLighten)return PS2_COMPOSITE_LIGHTEN;
    if(op==PictOpDifference)return PS2_COMPOSITE_DIFFERENCE;
    if(op==PictOpOverlay)return PS2_COMPOSITE_OVERLAY;
    if(op==PictOpHardLight)return PS2_COMPOSITE_HARDLIGHT;
    if(destination_argb){
        static const int operations[]={PS2_COMPOSITE_CLEAR,PS2_COMPOSITE_SRC,PS2_COMPOSITE_DST,
            PS2_COMPOSITE_OVER,PS2_COMPOSITE_OVER_REVERSE,PS2_COMPOSITE_IN,PS2_COMPOSITE_IN_REVERSE,
            PS2_COMPOSITE_OUT,PS2_COMPOSITE_OUT_REVERSE,PS2_COMPOSITE_ATOP,PS2_COMPOSITE_ATOP_REVERSE,
            PS2_COMPOSITE_XOR,PS2_COMPOSITE_ADD,PS2_COMPOSITE_SATURATE};
        return op>=PictOpClear && op<=PictOpSaturate?operations[op]:-1;
    }
    switch(op){
    case PictOpClear:case PictOpOut:return PS2_COMPOSITE_CLEAR;
    case PictOpSrc:case PictOpIn:return PS2_COMPOSITE_SRC;
    case PictOpDst:case PictOpOverReverse:case PictOpSaturate:return PS2_COMPOSITE_DST;
    case PictOpOver:case PictOpAtop:return PS2_COMPOSITE_OVER;
    case PictOpInReverse:case PictOpAtopReverse:return PS2_COMPOSITE_IN_REVERSE;
    case PictOpOutReverse:case PictOpXor:return PS2_COMPOSITE_OUT_REVERSE;
    case PictOpAdd:return PS2_COMPOSITE_ADD;
    default:return -1;
    }
}
static Bool coverage_target(PicturePtr p)
{
    return bitmap_picture(p) && !p->transform && !p->repeat;
}
static Bool wide_unorm_picture(PicturePtr p)
{
    if(p && !p->pDrawable)return picture(p,TRUE);
    if(!p || !p->pDrawable || !supported_filter(p))return FALSE;
    return bitmap_picture(p) || picture(p,TRUE);
}
static Bool packed10_operand(PicturePtr p,Bool source)
{
    if(!p)return FALSE;
    if(!p->pDrawable)return source && (gradient_picture(p) || picture(p,TRUE));
    return source?bitmap_picture(p) || picture(p,TRUE):coverage_target(p) || picture(p,FALSE);
}
static Bool packed10_check(int op,PicturePtr source,PicturePtr mask,PicturePtr destination)
{
    if(operation(op,TRUE)<0 || !packed10_operand(source,TRUE) || !packed10_operand(destination,FALSE) ||
        (mask && !packed10_operand(mask,TRUE)) || !destination->pDrawable->pScreen ||
        (source->pDrawable && source->pDrawable->pScreen!=destination->pDrawable->pScreen) ||
        (mask && mask->pDrawable && mask->pDrawable->pScreen!=destination->pDrawable->pScreen))return FALSE;
    struct ps2_blit_exa *b=backend(destination->pDrawable->pScreen);
    unsigned required=PS2GS_HW_SOLID_LOGIC|PS2GS_HW_CHANNEL_LUT|PS2GS_HW_CHANNEL_MAP|PS2GS_HW_CHANNEL_MAP_BLIT|PS2GS_HW_RGBA_TRANSFER|PS2GS_HW_TEXTURE_TRANSFER;
    return b && b->attached && (b->gs->caps.features&required)==required;
}
static Bool check(int op, PicturePtr source, PicturePtr mask, PicturePtr destination)
{
    op=masked_operation(op,source,mask,destination);
    if(!source || !alpha_map_picture(source) || !alpha_map_picture(mask) || !alpha_map_picture(destination))return FALSE;
    if(wide_operation(op) && op!=PictOpSaturate && (!wide_unorm_picture(source) || !wide_unorm_picture(destination) ||
        (mask && !wide_unorm_picture(mask))))return FALSE;
    if(packed10(source) || packed10(mask) || packed10(destination) || packed10(alpha_map(source)) ||
        packed10(alpha_map(mask)) || packed10(alpha_map(destination)))return packed10_check(op,source,mask,destination);
    PicturePtr target_map=alpha_map(destination);
    if(target_map &&
        (bitmap_picture(destination) || byte_format(destination)>=0 || rgb444(destination) || rgb444(target_map))){
        struct ps2_blit_exa *b=destination->pDrawable->pScreen?backend(destination->pDrawable->pScreen):NULL;
        PixmapPtr pixmap=picture_pixmap(destination);
        /* Byte/word and native depth-15 views share packed GS storage in
         * scanline store order; no CPU pixel conversion is permitted. */
        if(map_alias(destination,pixmap,b) && target_map->format!=destination->format &&
            (destination->pDrawable->depth!=8 && destination->pDrawable->depth!=16 && !native15_alias(destination,pixmap,b)))return FALSE;
    }
    /* Saturate's GS wide bilinear sampler retains all 16 fraction bits;
     * convolution preserves the R5900 unsigned conversion after every tap.
     * PrepareAccess rejects every CPU raster path, including coverage. */
    if(op==PictOpSaturate && coverage_target(destination) &&
        (source->filter!=PictFilterNearest || (mask && (mask->filter!=PictFilterNearest || mask->componentAlpha))) &&
        (!wide_unorm_picture(source) || (mask && !wide_unorm_picture(mask))))return FALSE;
    if(op==PictOpSaturate && !coverage_target(destination) && (argb(destination) || alpha_map(destination)) &&
        (!wide_unorm_picture(source) || !wide_unorm_picture(destination) ||
         (mask && !wide_unorm_picture(mask))))return FALSE;
    if(operation(op,TRUE)>=0 && coverage_target(destination) && destination->pDrawable->pScreen &&
        (bitmap_picture(source) || picture(source,TRUE)) &&
        (!mask || bitmap_picture(mask) || ((mask->pDrawable || gradient_picture(mask) || solid_mask_operation(op)) && picture(mask,TRUE))) &&
        (!source->pDrawable || source->pDrawable->pScreen==destination->pDrawable->pScreen) &&
        (!mask || !mask->pDrawable || mask->pDrawable->pScreen==destination->pDrawable->pScreen)){
        struct ps2_blit_exa *b=backend(destination->pDrawable->pScreen);
        unsigned required=PS2GS_HW_CHANNEL_LUT|PS2GS_HW_CHANNEL_MAP|PS2GS_HW_CHANNEL_MAP_BLIT|PS2GS_HW_RGBA_TRANSFER;
        return b && b->attached && (b->gs->caps.features&required)==required;
    }
    if (operation(op,argb(destination) || !!alpha_map(destination))<0 || (mask && !(bitmap_picture(mask) || ((mask->pDrawable || gradient_picture(mask) || solid_mask_operation(op)) && picture(mask,TRUE)))) ||
        !(picture(source,TRUE) || bitmap_picture(source)) || !picture(destination,FALSE) ||
        !destination->pDrawable->pScreen || (source->pDrawable && source->pDrawable->pScreen != destination->pDrawable->pScreen) ||
        (mask && mask->pDrawable && mask->pDrawable->pScreen!=destination->pDrawable->pScreen)) return FALSE;
    struct ps2_blit_exa *b = backend(destination->pDrawable->pScreen);
    Bool swapped=source->format==PICT_x8r8g8b8 || source->format==PICT_a8r8g8b8 || source->format==PICT_x1r5g5b5 ||
        destination->format==PICT_x8r8g8b8 || destination->format==PICT_a8r8g8b8 || destination->format==PICT_x1r5g5b5;
    Bool simple=!mask && !alpha_map(source) && !alpha_map(destination) && !bitmap_picture(source) && byte_format(source)<0 && byte_format(destination)<0 &&
        !rgb444(source) && !rgb444(destination) && source->pDrawable &&
        source->pDrawable->bitsPerPixel==destination->pDrawable->bitsPerPixel &&
        source->filter==PictFilterNearest && !swapped && !argb(source) && !argb(destination) &&
        !rotated(source) && !rotated(destination) && (op==PictOpSrc || op==PictOpOver || op==PictOpClear);
    unsigned required=PS2GS_HW_CHANNEL_LUT|PS2GS_HW_CHANNEL_MAP|PS2GS_HW_CHANNEL_MAP_BLIT|
        PS2GS_HW_TEXTURE_TRANSFER|PS2GS_HW_RGBA_TRANSFER;
    if(!simple && (!b || (b->gs->caps.features&required)!=required))return FALSE;
    return b && b->attached && (!(rotated(source) || rotated(destination)) || (b->gs->caps.features&PS2GS_HW_RGBA_TRANSFER)) &&
        (!swapped || (b->gs->caps.features&PS2GS_HW_CHANNEL_MAP)) &&
        (!(argb(source) || argb(destination)) ||
         ((b->gs->caps.features&PS2GS_HW_CHANNEL_MAP_BLIT) && (b->gs->caps.features&PS2GS_HW_RGBA_TRANSFER)));
}
static void release_coverage(struct ps2_blit_exa *b)
{
    for(unsigned i=0;i<2;++i){free(b->gradient_stops[i]);b->gradient_stops[i]=NULL;}
    memset(&b->destination_alias,0,sizeof(b->destination_alias));
    for(unsigned i=0;i<3;++i){
        if(ps2_format_release(b->alpha_format+i) ||
            (b->alpha_coverage[i].gs && ps2_coverage_release(b->alpha_coverage+i)))
            FatalError("PS2 GS: alpha map surface release failed\n");
        b->alpha_pixmaps[i]=NULL;
    }
    if(ps2_format_release(&b->format_source) || ps2_format_release(&b->format_destination) ||
        ps2_format_release(&b->format_mask))FatalError("PS2 GS: format surface release failed\n");
    if(b->coverage_source.gs && ps2_coverage_release(&b->coverage_source) && !b->gs->error)
        FatalError("PS2 GS: coverage source release failed\n");
    if(b->coverage_destination.gs && ps2_coverage_release(&b->coverage_destination) && !b->gs->error)
        FatalError("PS2 GS: coverage destination release failed\n");
    if(b->coverage_mask.gs && ps2_coverage_release(&b->coverage_mask) && !b->gs->error)
        FatalError("PS2 GS: coverage mask release failed\n");
    b->coverage_colour_source=FALSE;
    uint32_t solids[]={b->solid,b->solid_mask};
    for(unsigned i=0;i<2;++i)if(solids[i] && !b->gs->error && ps2_surface_destroy(b->gs,solids[i]))
        FatalError("PS2 GS: solid picture release failed\n");
    b->solid=b->solid_mask=0;
    memset(&b->source_coverage,0,sizeof(b->source_coverage));memset(&b->coverage,0,sizeof(b->coverage));
}
static void live(struct ps2_blit_exa *b)
{
    if(ps2_pipe_context_acquire(b->context))FatalError("PS2 GS: composite context is closed or failed\n");
    ps2_pipe_context_release(b->context);
}
static int read_alpha_source(PixmapPtr pixmap,struct ps2_alpha_source *out)
{
    int owned=ps2_surface_exa_alpha_source(pixmap,out);
    return owned?owned:ps2_surface_exa_borrowed_source(pixmap,out);
}
static struct ps2_picture_bitmap coverage_bitmap(const struct ps2_alpha_source *a)
{
    return (struct ps2_picture_bitmap){.bits=a->bits,.bytes=a->bytes,.stride=a->stride,
        .width=a->width,.height=a->height,.bits_per_pixel=a->bits_per_pixel,.alpha_bits=a->depth,.msb_first=BITMAP_BIT_ORDER==MSBFirst};
}
static Bool decode_byte_picture(struct ps2_format_surface *f,struct ps2_blit_exa *b,PixmapPtr pixmap,PicturePtr p,Bool read_only)
{
    struct ps2_alpha_source bytes;
    if((read_only?read_alpha_source(pixmap,&bytes):ps2_surface_exa_alpha_source(pixmap,&bytes))!=1)return FALSE;
    struct ps2_picture_bitmap raw=coverage_bitmap(&bytes);
    unsigned format=byte_format(p);
    return !(raw.bits_per_pixel==16?ps2_format_decode16(f,b->gs,&raw,format):ps2_format_decode8(f,b->gs,&raw,format));
}
/* Read-only decoded operands retain their image and original-storage metadata.
 * Upload/codec scratch is dead before blending and must not occupy GS slots
 * alongside the floating compositor's transient register surfaces. */
static void sampled_storage(struct ps2_blit_exa *b,struct ps2_format_surface *f,struct ps2_coverage_surface *c)
{
    uint32_t *scratch[]={&f->words,&f->scratch,&f->palette,&f->rgba,&c->packed,&c->palette,&c->bytes,&c->rgba};
    for(unsigned i=0;i<sizeof(scratch)/sizeof(*scratch);++i)if(*scratch[i]){
        if(ps2_surface_destroy(b->gs,*scratch[i]))FatalError("PS2 GS: sampled operand scratch release failed\n");
        *scratch[i]=0;
    }
}
static Bool prepare_alpha_map(struct ps2_blit_exa *b,PicturePtr parent,PixmapPtr parent_pixmap,
    const struct ps2_surface *parent_surface,unsigned index,Bool packed_request,
    struct ps2_picture_alpha_map *result)
{
    PicturePtr map=alpha_map(parent);
    if(!map){*result=(struct ps2_picture_alpha_map){0};return TRUE;}
    PixmapPtr pixmap=(PixmapPtr)map->pDrawable;
    const struct ps2_surface *surface=NULL;
    PictureRec plain=*map;plain.alphaMap=NULL;plain.transform=NULL;plain.repeat=FALSE;
    plain.filter=PictFilterNearest;plain.filter_nparams=0;
    Bool coverage=bitmap_picture(&plain),format=byte_format(map)>=0,opaque444=rgb444(map);
    Bool shared=index==2 && map->format==parent->format && map_alias(parent,parent_pixmap,b);
    if(shared){
        if(!parent_surface)return FALSE;
        surface=parent_surface;
    }else if(coverage){
        struct ps2_alpha_source bytes;
        if(ps2_surface_exa_alpha_source(pixmap,&bytes)!=1)return FALSE;
        struct ps2_picture_bitmap image=coverage_bitmap(&bytes);
        if(map->format==PICT_x4a4)image.alpha_bits=4;
        if(ps2_coverage_decode(b->alpha_coverage+index,b->gs,&image))return FALSE;
        surface=ps2_surface_lookup(b->gs,b->alpha_coverage[index].surface);
    }else if(format){
        if(!decode_byte_picture(b->alpha_format+index,b,pixmap,map,index!=2))return FALSE;
        surface=ps2_surface_lookup(b->gs,b->alpha_format[index].surface);
    }else{
        surface=storage(pixmap,b);
        if(!surface)return FALSE;
        if(!packed_request && index==2 && native15_alias(parent,parent_pixmap,b)){
            if(ps2_format_decode_native15(b->alpha_format+index,b->gs,surface->handle,native15_format(map)))return FALSE;
            surface=ps2_surface_lookup(b->gs,b->alpha_format[index].surface);format=TRUE;
        }else if(opaque444 && !packed_request){
            if(ps2_format_decode444(b->alpha_format+index,b->gs,surface->handle,map->format==PICT_x4r4g4b4))return FALSE;
            surface=ps2_surface_lookup(b->gs,b->alpha_format[index].surface);
        }
    }
    if(!surface)return FALSE;
    b->alpha_pixmaps[index]=pixmap;
    int32_t origin_x=parent->alphaOrigin.x,origin_y=parent->alphaOrigin.y;
    /* Alpha origins are relative to the parent drawable, while the GS
     * sampler addresses its backing pixmap. Redirected windows can have
     * a further backing-pixmap origin. */
    if(parent->pDrawable->type==DRAWABLE_WINDOW){
        if(!parent_pixmap)return FALSE;
        origin_x+=parent->pDrawable->x;origin_y+=parent->pDrawable->y;
#ifdef COMPOSITE
        origin_x-=parent_pixmap->screen_x;origin_y-=parent_pixmap->screen_y;
#endif
    }
    *result=(struct ps2_picture_alpha_map){.enabled=1,.surface=surface->handle,
        .width=surface->width,.height=surface->height,.has_alpha=coverage || argb(map),.alpha_only=coverage,
        .alpha_bits=PICT_FORMAT_A(map->format),
        .rgb_bits={PICT_FORMAT_R(map->format),PICT_FORMAT_G(map->format),PICT_FORMAT_B(map->format)},
        .x=origin_x,.y=origin_y,
        .swap=!format && (!opaque444 || packed_request) && !coverage &&
            (map->format==PICT_x8r8g8b8 || map->format==PICT_a8r8g8b8 || map->format==PICT_x1r5g5b5 ||
             map->format==PICT_a2r10g10b10 || map->format==PICT_x2r10g10b10 || map->format==PICT_x4r4g4b4),
        .rotate=!coverage && rotated(map)};
    return TRUE;
}
static Bool prepare(int op,PicturePtr source,PicturePtr mask,PicturePtr destination,
    PixmapPtr source_pixmap,PixmapPtr mask_pixmap,PixmapPtr destination_pixmap)
{
    op=masked_operation(op,source,mask,destination);
    if(!check(op,source,mask,destination) || (!!(mask && mask->pDrawable)!=!!mask_pixmap) || !destination_pixmap)return FALSE;
    struct ps2_blit_exa *b=backend(destination->pDrawable->pScreen);
    Bool packed_request=packed10(source) || packed10(mask) || packed10(destination) ||
        packed10(alpha_map(source)) || packed10(alpha_map(mask)) || packed10(alpha_map(destination));
    Bool bitmap=bitmap_picture(source),constant=!source->pDrawable,alpha_target=coverage_target(destination);
    Bool byte_source=byte_format(source)>=0,byte_destination=byte_format(destination)>=0;
    const struct ps2_surface *s=constant || bitmap || byte_source?NULL:storage(source_pixmap,b);
    const struct ps2_surface *d=alpha_target || byte_destination?NULL:storage(destination_pixmap,b);
    if(b->prepared || (!alpha_target && !byte_destination && !d) || (constant?source_pixmap!=NULL:
        (!bitmap && !byte_source && (!s || source_pixmap->drawable.depth!=source->pDrawable->depth))) ||
        destination_pixmap->drawable.depth!=destination->pDrawable->depth)return FALSE;
    live(b);
    struct ps2_alpha_source mono={0};
    Bool constant_mask=mask && !mask->pDrawable;
    Bool native_mask=mask && picture(mask,TRUE);
    Bool byte_mask=mask && byte_format(mask)>=0;
    const struct ps2_surface *mask_storage=native_mask && !byte_mask?storage(mask_pixmap,b):NULL;
    if(mask && (native_mask?!mask_storage && !byte_mask && !constant_mask:read_alpha_source(mask_pixmap,&mono)!=1))return FALSE;
    if(ps2_render_flush(ps2_pipe_context_render(b->context)))FatalError("PS2 GS: composite source fence failed\n");
    if(byte_source){
        if(!decode_byte_picture(&b->format_source,b,source_pixmap,source,TRUE)){release_coverage(b);return FALSE;}
        s=ps2_surface_lookup(b->gs,b->format_source.surface);
    }
    if(byte_destination){
        if(!decode_byte_picture(&b->format_destination,b,destination_pixmap,destination,FALSE)){release_coverage(b);return FALSE;}
        d=ps2_surface_lookup(b->gs,b->format_destination.surface);
    }
    if(byte_mask){
        if(!decode_byte_picture(&b->format_mask,b,mask_pixmap,mask,TRUE)){release_coverage(b);return FALSE;}
        mask_storage=ps2_surface_lookup(b->gs,b->format_mask.surface);
    }
    if(!packed_request && rgb444(source)){
        if(ps2_format_decode444(&b->format_source,b->gs,s->handle,source->format==PICT_x4r4g4b4)){
            release_coverage(b);return FALSE;}
        s=ps2_surface_lookup(b->gs,b->format_source.surface);
    }
    if(!packed_request && native15_alias(destination,destination_pixmap,b)){
        if(ps2_format_decode_native15(&b->format_destination,b->gs,d->handle,native15_format(destination))){
            release_coverage(b);return FALSE;}
        d=ps2_surface_lookup(b->gs,b->format_destination.surface);
    }else if(!packed_request && rgb444(destination)){
        if(ps2_format_decode444(&b->format_destination,b->gs,d->handle,destination->format==PICT_x4r4g4b4)){
            release_coverage(b);return FALSE;}
        d=ps2_surface_lookup(b->gs,b->format_destination.surface);
    }
    if(!packed_request && native_mask && rgb444(mask)){
        if(ps2_format_decode444(&b->format_mask,b->gs,mask_storage->handle,mask->format==PICT_x4r4g4b4)){
            release_coverage(b);return FALSE;}
        mask_storage=ps2_surface_lookup(b->gs,b->format_mask.surface);
    }
    if(bitmap){
        if(read_alpha_source(source_pixmap,&b->source_coverage)!=1)return FALSE;
        struct ps2_picture_bitmap bytes=coverage_bitmap(&b->source_coverage);
        if(source->format==PICT_x4a4)bytes.alpha_bits=4;
        if(ps2_coverage_decode(&b->coverage_source,b->gs,&bytes)){release_coverage(b);return FALSE;}
        s=ps2_surface_lookup(b->gs,b->coverage_source.surface);b->coverage_colour_source=TRUE;
    }
    if(alpha_target){
        if(ps2_surface_exa_alpha_source(destination_pixmap,&b->coverage)!=1){release_coverage(b);return FALSE;}
        struct ps2_picture_bitmap bytes=coverage_bitmap(&b->coverage);
        if(destination->format==PICT_x4a4)bytes.alpha_bits=4;
        if(ps2_coverage_decode(&b->coverage_destination,b->gs,&bytes)){release_coverage(b);return FALSE;}
        d=ps2_surface_lookup(b->gs,b->coverage_destination.surface);
    }
    if(packed_request && mask && !native_mask){
        struct ps2_picture_bitmap bytes=coverage_bitmap(&mono);
        if(mask->format==PICT_x4a4)bytes.alpha_bits=4;
        if(ps2_coverage_decode(&b->coverage_mask,b->gs,&bytes)){release_coverage(b);return FALSE;}
        mask_storage=ps2_surface_lookup(b->gs,b->coverage_mask.surface);native_mask=TRUE;
    }
    struct ps2_picture_alpha_map source_map={0},destination_map={0},mask_map={0};
    if(!prepare_alpha_map(b,source,source_pixmap,s,0,packed_request,&source_map) ||
        !prepare_alpha_map(b,destination,destination_pixmap,d,2,packed_request,&destination_map) ||
        (mask && !prepare_alpha_map(b,mask,mask_pixmap,mask_storage,1,packed_request,&mask_map))){release_coverage(b);return FALSE;}
    if(constant){
        if(ps2_surface_create(b->gs,1,1,32,&b->solid)){release_coverage(b);return FALSE;}
        uint32_t c=gradient_picture(source)?0:source->pSourcePict->solidFill.color;
        if(gradient_picture(source) && ps2_surface_set_depth(b->gs,b->solid,32)){release_coverage(b);return FALSE;}
        uint32_t native=(c&0xff00ff00U)|((c&255)<<16)|((c>>16)&255);
        if(ps2_surface_fill(b->gs,b->solid,0,0,1,1,native))FatalError("PS2 GS: solid source initialization failed\n");
        s=ps2_surface_lookup(b->gs,b->solid);
    }
    if(constant_mask){
        if(ps2_surface_create(b->gs,1,1,32,&b->solid_mask)){release_coverage(b);return FALSE;}
        uint32_t c=gradient_picture(mask)?0:mask->pSourcePict->solidFill.color;
        if(gradient_picture(mask) && ps2_surface_set_depth(b->gs,b->solid_mask,32)){release_coverage(b);return FALSE;}
        uint32_t native=(c&0xff00ff00U)|((c&255)<<16)|((c>>16)&255);
        if(ps2_surface_fill(b->gs,b->solid_mask,0,0,1,1,native))FatalError("PS2 GS: solid mask initialization failed\n");
        mask_storage=ps2_surface_lookup(b->gs,b->solid_mask);
    }
    b->source_pixmap=source_pixmap;b->destination_pixmap=destination_pixmap;
    b->source=*s;b->destination=*d;b->prepared=TRUE;
    if(constant)b->sample=(struct ps2_picture_sample){.repeat=PS2_REPEAT_NORMAL,.has_alpha=1,
        .alpha=source->pSourcePict->solidFill.color>>24};
    else if(!sample(source,&b->sample)){b->prepared=FALSE;release_coverage(b);return FALSE;}
    b->sample.source_swap=!constant && !bitmap && !b->format_source.surface && (source->format==PICT_x8r8g8b8 || source->format==PICT_a8r8g8b8 || source->format==PICT_x1r5g5b5 || (packed_request && source->format==PICT_x4r4g4b4));
    b->sample.destination_swap=!alpha_target && !b->format_destination.surface && (destination->format==PICT_x8r8g8b8 || destination->format==PICT_a8r8g8b8 || destination->format==PICT_x1r5g5b5 || (packed_request && destination->format==PICT_x4r4g4b4));
    b->sample.source_argb=bitmap || argb(source);b->sample.destination_argb=alpha_target || argb(destination);
    b->sample.source_rotate=!bitmap && rotated(source);b->sample.destination_rotate=!alpha_target && rotated(destination);
    b->sample.alpha_only_destination=alpha_target;
    b->sample.source_packed10=packed10(source);b->sample.destination_packed10=packed10(destination);
    b->sample.destination_unorm=precision(destination);
    b->sample.source_alpha_map=source_map;b->sample.destination_alpha_map=destination_map;
    PicturePtr target_map=alpha_map(destination);
    if(target_map && target_map->format!=destination->format && map_alias(destination,destination_pixmap,b) &&
        (b->format_destination.packed || b->coverage_destination.surface)){
        b->destination_alias=(struct ps2_format_alias){
            .format={b->format_destination.packed?&b->format_destination:NULL,b->alpha_format[2].packed?b->alpha_format+2:NULL},
            .coverage={b->coverage_destination.surface?&b->coverage_destination:NULL,b->alpha_coverage[2].surface?b->alpha_coverage+2:NULL}};
        if(!ps2_format_alias_valid(&b->destination_alias,b->gs,d->handle,destination_map.surface)){
            b->prepared=FALSE;release_coverage(b);return FALSE;}
        b->sample.destination_alias=&b->destination_alias;
    }
    if(constant && !gradient_picture(source) && ((wide_operation(op) && (op!=PictOpSaturate || alpha_target || argb(destination) || destination_map.enabled)) || packed_request)){
        const xRenderColor *c=&source->pSourcePict->solidFill.fullcolor;
        b->sample.source_solid=1;
        b->sample.solid_rgba[0]=c->red;b->sample.solid_rgba[1]=c->green;
        b->sample.solid_rgba[2]=c->blue;b->sample.solid_rgba[3]=c->alpha;
    }
    b->over=alpha_target && op==PictOpSaturate?PS2_COMPOSITE_SATURATE:
        operation(op,b->sample.destination_argb || b->sample.destination_alpha_map.enabled ||
            packed_request);
    b->masked=!!mask;b->mask=mask?native_mask?(struct ps2_picture_bitmap){.has_surface=1,
        .surface=mask_storage->handle,.component_alpha=!!mask->componentAlpha,
        .width=mask_storage->width,.height=mask_storage->height}:coverage_bitmap(&mono):(struct ps2_picture_bitmap){0};
    if(mask){b->mask.component_alpha=!!mask->componentAlpha;
        if(mask->format==PICT_x4a4)b->mask.alpha_bits=4;
        if(constant_mask)b->mask.sample=(struct ps2_picture_sample){.repeat=PS2_REPEAT_NORMAL,.has_alpha=1,
            .alpha=mask->pSourcePict->solidFill.color>>24};
        else if(!sample(mask,&b->mask.sample)){b->prepared=FALSE;release_coverage(b);return FALSE;}
        b->mask.sample.source_packed10=packed10(mask);
        b->mask.sample.source_argb=bitmap_picture(mask) || argb(mask);b->mask.sample.source_rotate=rotated(mask);
        b->mask.sample.source_swap=!constant_mask && !b->format_mask.surface && (mask->format==PICT_x8r8g8b8 || mask->format==PICT_a8r8g8b8 || mask->format==PICT_x1r5g5b5 || (packed_request && mask->format==PICT_x4r4g4b4));
        b->mask.sample.source_alpha_map=mask_map;
        if(constant_mask && !gradient_picture(mask)){const xRenderColor *c=&mask->pSourcePict->solidFill.fullcolor;
            b->mask.sample.source_solid=packed_request || b->over==PS2_COMPOSITE_SATURATE ||
                b->over==PS2_COMPOSITE_DISJOINT_OVER_REVERSE ||
                (b->over>=PS2_COMPOSITE_DISJOINT_OVER && b->over<=PS2_COMPOSITE_HSL_LUMINOSITY);
            b->mask.sample.solid_rgba[0]=c->red;b->mask.sample.solid_rgba[1]=c->green;
            b->mask.sample.solid_rgba[2]=c->blue;b->mask.sample.solid_rgba[3]=c->alpha;
        }
    }
    if(gradient_picture(source)){
        struct ps2_picture_sample generated;
        if(!gradient_sample(b,source,&generated,0)){b->prepared=FALSE;release_coverage(b);return FALSE;}
        b->sample.gradient=generated.gradient;b->sample.transformed=generated.transformed;
        memcpy(b->sample.matrix,generated.matrix,sizeof(generated.matrix));b->sample.repeat=generated.repeat;
        b->sample.source_argb=1;b->sample.has_alpha=0;b->sample.source_solid=0;
        b->sample.bilinear=generated.bilinear;b->sample.convolution=generated.convolution;
        b->sample.filter_nparams=generated.filter_nparams;b->sample.filter_params=generated.filter_params;
    }
    if(gradient_picture(mask) && !gradient_sample(b,mask,&b->mask.sample,1)){b->prepared=FALSE;release_coverage(b);return FALSE;}
    if(packed_request){
        sampled_storage(b,&b->format_source,&b->coverage_source);
        sampled_storage(b,&b->format_mask,&b->coverage_mask);
        for(unsigned i=0;i<2;++i)sampled_storage(b,b->alpha_format+i,b->alpha_coverage+i);
    }
    return TRUE;
}
static void composite(PixmapPtr destination, int sx, int sy, int mx, int my, int dx, int dy, int width, int height)
{
    struct ps2_blit_exa *b = destination && destination->drawable.pScreen ? backend(destination->drawable.pScreen) : NULL;
    if(!b || !b->attached || !b->prepared || destination!=b->destination_pixmap ||
        !same_storage(b->solid?ps2_surface_lookup(b->gs,b->solid):b->format_source.surface?
            ps2_surface_lookup(b->gs,b->format_source.surface):b->coverage_source.surface?
            ps2_surface_lookup(b->gs,b->coverage_source.surface):storage(b->source_pixmap,b),&b->source) ||
        !same_storage(b->format_destination.surface?ps2_surface_lookup(b->gs,b->format_destination.surface):
            b->coverage_destination.surface?ps2_surface_lookup(b->gs,b->coverage_destination.surface):
            storage(destination,b),&b->destination))
        FatalError("PS2 GS: invalid prepared composite storage\n");
    if((b->format_source.surface && (!b->format_source.packed || b->format_source.original.bits_per_pixel) &&
            !same_storage(storage(b->source_pixmap,b),&b->format_source.original)) ||
        (b->format_destination.surface && (!b->format_destination.packed || b->format_destination.original.bits_per_pixel) &&
            !same_storage(storage(destination,b),&b->format_destination.original)))
        FatalError("PS2 GS: invalid original RGB444 storage\n");
    if(b->format_source.packed && !b->format_source.original.bits_per_pixel){struct ps2_alpha_source live;
        if(read_alpha_source(b->source_pixmap,&live)!=1 || live.bits!=b->format_source.original_bytes.bits ||
            live.width!=b->format_source.original_bytes.width || live.height!=b->format_source.original_bytes.height ||
            live.stride!=b->format_source.original_bytes.stride)
            FatalError("PS2 GS: invalid prepared byte colour source\n");
    }
    if(b->coverage_colour_source){struct ps2_alpha_source live;
        if(read_alpha_source(b->source_pixmap,&live)!=1 || live.bits!=b->source_coverage.bits ||
            live.bytes!=b->source_coverage.bytes || live.stride!=b->source_coverage.stride ||
            live.width!=b->source_coverage.width || live.height!=b->source_coverage.height)
            FatalError("PS2 GS: invalid prepared coverage source\n");
    }
    const struct ps2_pipe_blit_rect box = {.sx = sx, .sy = sy, .sw = width, .sh = height,
        .dx = dx, .dy = dy, .dw = width, .dh = height};
    int status;
    Bool plain=!b->sample.source_swap && !b->sample.destination_swap && !b->sample.source_argb &&
        !b->sample.destination_argb && b->source.bits_per_pixel==b->destination.bits_per_pixel;
    plain=plain && !b->sample.source_alpha_map.enabled && !b->sample.destination_alpha_map.enabled &&
        !b->sample.source_packed10 && !b->sample.destination_packed10;
    if(plain && b->over<=PS2_COMPOSITE_OVER && !b->sample.bilinear && !b->sample.convolution && !b->sample.has_alpha && !b->sample.transformed && !b->sample.repeat && !b->masked && sx>=0 && sy>=0 &&
       (int64_t)sx+width<=b->source.width && (int64_t)sy+height<=b->source.height)
        status=ps2_pipe_blit_rect(b->context,b->source.handle,b->destination.handle,&box);
    else if(plain && b->over<=PS2_COMPOSITE_OVER && !b->sample.bilinear && !b->sample.convolution && !b->sample.has_alpha && !b->sample.transformed && b->sample.repeat==PS2_REPEAT_NORMAL && !b->masked &&
        b->source.handle && b->source.handle!=b->destination.handle){
        const struct ps2_pipe_solid_box tile={dx,dy,dx+width,dy+height};
        status=ps2_pipe_tile_rectangles(b->context,b->source.handle,b->destination.handle,&tile,1,dx-sx,dy-sy);
    }else{
        struct ps2_pipe_copy_box rect={sx,sy,dx,dy,width,height};
        struct ps2_picture_bitmap mask=b->mask;mask.x=mx;mask.y=my;
#ifdef PS2_DDX_DIAGNOSTICS
        if(b->solid && b->masked && width==7 && height==5 && mx>=0 && my>=0 &&
            (unsigned)mx+7<=mask.width && (unsigned)my<mask.height && mask.bits_per_pixel==8)
            ErrorF("PS2 glyph sample: solid=%u alpha=%u op=%u mask=%ux%u stride=%zu at=%d,%d values=%u,%u,%u\n",
                b->solid,b->sample.alpha,b->over,mask.width,mask.height,mask.stride,mx,my,
                mask.bits[my*mask.stride+mx],mask.bits[my*mask.stride+mx+1],mask.bits[my*mask.stride+mx+2]);
#endif
        status=ps2_pipe_composite_rect(b->context,b->source.handle,b->destination.handle,&rect,
            &b->sample,b->masked?&mask:NULL,b->over);
    }
    if (status)
        FatalError("PS2 GS: native composite failed: %s\n", strerror(errno));
}
static void done(PixmapPtr destination)
{
    struct ps2_blit_exa *b = destination && destination->drawable.pScreen ? backend(destination->drawable.pScreen) : NULL;
    if (!b || !b->attached || !b->prepared || destination != b->destination_pixmap)
        FatalError("PS2 GS: composite completion order\n");
    if(b->format_destination.surface && !b->format_destination.packed &&
        (!same_storage(storage(destination,b),&b->format_destination.original) || ps2_format_encode444(&b->format_destination)))
        FatalError("PS2 GS: RGB444 encode failed\n");
    if(b->format_destination.packed && b->format_destination.original.bits_per_pixel){
        if(!b->sample.destination_alias)
            for(unsigned y=0;y<b->destination.height;++y)
                if(ps2_format_pack_region(&b->format_destination,0,y,b->destination.width))
                    FatalError("PS2 GS: native depth-15 colour pack failed\n");
        if(!same_storage(storage(destination,b),&b->format_destination.original) || ps2_format_store_native15(&b->format_destination))
            FatalError("PS2 GS: native depth-15 alias store failed\n");
    }else if(b->format_destination.packed){struct ps2_alpha_source bytes;
        if(ps2_surface_exa_alpha_source(destination,&bytes)!=1 || bytes.bits!=b->format_destination.original_bytes.bits ||
            bytes.width!=b->format_destination.original_bytes.width || bytes.height!=b->format_destination.original_bytes.height ||
            bytes.stride!=b->format_destination.original_bytes.stride ||
            (bytes.bits_per_pixel==16?
                ps2_format_encode16(&b->format_destination,b->context,(unsigned char*)bytes.bits,bytes.bytes,bytes.stride):
                ps2_format_encode8(&b->format_destination,b->context,(unsigned char*)bytes.bits,bytes.bytes,bytes.stride)))
            FatalError("PS2 GS: byte colour encode failed\n");
    }
    if(b->coverage_destination.surface){struct ps2_alpha_source live;
        if(ps2_surface_exa_alpha_source(destination,&live)!=1 || live.bits!=b->coverage.bits ||
            live.width!=b->coverage.width || live.height!=b->coverage.height || live.stride!=b->coverage.stride ||
            ps2_coverage_encode(&b->coverage_destination,b->context,(unsigned char*)live.bits,live.bytes,live.stride))
            FatalError("PS2 GS: coverage client transfer failed: %s\n",strerror(errno));
    }
    if(b->alpha_pixmaps[2] && !b->sample.destination_alias){
        struct ps2_format_surface *f=b->alpha_format+2;
        struct ps2_coverage_surface *c=b->alpha_coverage+2;
        if(f->packed || c->surface){
            struct ps2_alpha_source bytes;
            if(ps2_surface_exa_alpha_source(b->alpha_pixmaps[2],&bytes)!=1 ||
                (c->surface?ps2_coverage_encode(c,b->context,(unsigned char*)bytes.bits,bytes.bytes,bytes.stride):
                 bytes.bits_per_pixel==16?ps2_format_encode16(f,b->context,(unsigned char*)bytes.bits,bytes.bytes,bytes.stride):
                 ps2_format_encode8(f,b->context,(unsigned char*)bytes.bits,bytes.bytes,bytes.stride)))
                FatalError("PS2 GS: alpha map client transfer failed\n");
        }else if(f->surface && ps2_format_encode444(f))FatalError("PS2 GS: RGB444 alpha map encode failed\n");
    }
    b->prepared = FALSE; b->source_pixmap = NULL; b->destination_pixmap = NULL;
    release_coverage(b);
    if(b->solid){
        if(!b->gs->error && ps2_surface_destroy(b->gs,b->solid))FatalError("PS2 GS: solid source release failed\n");
        b->solid=0;
    }
    b->masked=FALSE;memset(&b->mask,0,sizeof(b->mask));
}
Bool ps2_blit_exa_prepare(ScreenPtr screen, struct ps2_blit_exa *b,
    struct ps2_surface_exa *exa, struct pipe_context *context)
{
    struct ps2_render_client *render = ps2_pipe_context_render(context);
    if (!screen || !screen->GetScreenPixmap || !b || b->attached || !exa || !render || render->gs != exa->gs ||
        !(exa->gs->caps.features & PS2GS_HW_COPY) || !exa->driver.CheckComposite || !exa->driver.PrepareComposite ||
        !exa->driver.Composite || !exa->driver.DoneComposite || exa->driver.Composite == composite ||
        !dixRegisterPrivateKey(&key, PRIVATE_SCREEN, 0) || backend(screen) || ps2_pipe_context_acquire(context)) return FALSE;
    *b = (struct ps2_blit_exa){.screen = screen, .exa = exa, .gs = exa->gs, .context = context,
        .CheckComposite = exa->driver.CheckComposite, .PrepareComposite = exa->driver.PrepareComposite,
        .Composite = exa->driver.Composite, .DoneComposite = exa->driver.DoneComposite, .attached = TRUE};
    dixSetPrivate(&screen->devPrivates, &key, b);
    exa->driver.CheckComposite = check; exa->driver.PrepareComposite = prepare;
    exa->driver.Composite = composite; exa->driver.DoneComposite = done;
    return TRUE;
}
void ps2_blit_exa_cleanup(ScreenPtr screen, struct ps2_blit_exa *b)
{
    if (!b || !b->attached) return;
    if (!screen || backend(screen) != b || b->exa->driver.CheckComposite != check ||
        b->exa->driver.PrepareComposite != prepare || b->exa->driver.Composite != composite || b->exa->driver.DoneComposite != done)
        FatalError("PS2 GS: composite cleanup order\n");
    b->exa->driver.CheckComposite = b->CheckComposite; b->exa->driver.PrepareComposite = b->PrepareComposite;
    b->exa->driver.Composite = b->Composite; b->exa->driver.DoneComposite = b->DoneComposite;
    dixSetPrivate(&screen->devPrivates, &key, NULL);
    struct pipe_context *context = b->context; memset(b, 0, sizeof(*b)); ps2_pipe_context_release(context);
}
