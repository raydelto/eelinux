/* SPDX-License-Identifier: MIT */
#include "ps2_rop.h"
#include "ps2_pipe.h"
#include "ps2_gs_logic.h"
#include "ps2_gs_lut.h"
#include "ps2_gs_transfer.h"
#include "ps2_tile.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
static uint32_t bits(float f) { uint32_t v; memcpy(&v, &f, 4); return v; }
static unsigned padded(unsigned n) { unsigned p = 1; while (p < n) p *= 2; return p; }
int ps2_rop_sample(struct ps2_surface_client *gs, uint32_t source, uint32_t destination,
    unsigned sx, unsigned sy, unsigned dx, unsigned dy, unsigned w, unsigned h,
    uint32_t mask, unsigned mode, unsigned factor)
{
    const struct ps2_surface *s = ps2_surface_lookup(gs, source);
    /* Match the existing RGBA nearest path: a quarter texel stays within
     * the same source texel under GS interpolation rounding. Split padded
     * trailing edges so the typed S/T coordinates never exceed Q. */
    if(!s){errno=EINVAL;return -1;}
    if(w>1 && sx+w==padded(s->width))
        return ps2_rop_sample(gs,source,destination,sx,sy,dx,dy,w-1,h,mask,mode,factor) ||
            ps2_rop_sample(gs,source,destination,sx+w-1,sy,dx+w-1,dy,1,h,mask,mode,factor)?-1:0;
    if(h>1 && sy+h==padded(s->height))
        return ps2_rop_sample(gs,source,destination,sx,sy,dx,dy,w,h-1,mask,mode,factor) ||
            ps2_rop_sample(gs,source,destination,sx,sy+h-1,dx,dy+h-1,w,1,mask,mode,factor)?-1:0;
    unsigned x[4] = {dx, dx+w, dx+w, dx}, y[4] = {dy, dy, dy+h, dy+h};
    float left=sx+.25f,right=left+(w==1?.5f:w),top=sy+.25f,bottom=top+(h==1?.5f:h);
    float u[4]={left,right,right,left},v[4]={top,top,bottom,bottom};
    static const unsigned indices[2][3] = {{0,1,2},{0,2,3}};
    struct ps2gs_texture_triangle triangles[2] = {0};
    for (unsigned i=0; i<2; ++i) {
        triangles[i].flags = factor != 128 ? PS2GS_TEXTURE_MODULATE : 0;
        for (unsigned j=0; j<3; ++j) {
            unsigned k=indices[i][j];
            triangles[i].v[j] = (struct ps2gs_texture_vertex){
                .position = {.x=x[k]*16, .y=y[k]*16,
                    .rgba=0x80000000U | factor | factor<<8 | factor<<16},
                .s=bits((float)u[k]/padded(s->width)), .t=bits((float)v[k]/padded(s->height)),
                .q=0x3f800000};
        }
    }
    struct ps2_render_client r = {.gs=gs, .target=destination};
    return ps2_texture_transfer(&r, source, triangles, 2, mask, mode);
}
int ps2_rop_release(struct ps2_rop_workspace *w)
{
    int status=0, error=errno;
    uint32_t handles[] = {w->palette,w->alpha_destination,w->alpha_source,w->c,w->b,w->a,w->destination,w->source};
    for (unsigned i=0; i<sizeof(handles)/sizeof(*handles); ++i)
        if (handles[i] && !w->gs->error && ps2_surface_destroy(w->gs,handles[i])) {
            if (!status) error=errno;
            status=-1;
        }
    memset(w,0,sizeof(*w)); errno=error; return status;
}
int ps2_rop_reserve(struct ps2_rop_workspace *w, struct ps2_surface_client *gs, unsigned bpp)
{
    *w=(struct ps2_rop_workspace){.gs=gs};
    if (!(gs->caps.features&PS2GS_HW_TEXTURE_TRANSFER) ||
        !(gs->caps.features&PS2GS_HW_SOLID_LOGIC)) { errno=ENOTSUP; return -1; }
    if (ps2_surface_create(gs,128,32,bpp,&w->source) ||
        ps2_surface_create(gs,128,32,bpp,&w->destination) ||
        ps2_surface_create(gs,64,32,32,&w->a) ||
        ps2_surface_create(gs,64,32,32,&w->b) ||
        ps2_surface_create(gs,64,32,32,&w->c)) {
        int error=errno; ps2_rop_release(w); errno=error; return -1;
    }
    return 0;
}
int ps2_rop_alpha_reserve(struct ps2_rop_workspace *w)
{
    if(w->alpha_source && w->alpha_destination && w->palette)return 0;
    if(!(w->gs->caps.features&PS2GS_HW_CHANNEL_MAP_BLIT)){errno=ENOTSUP;return -1;}
    if((!w->alpha_source && ps2_surface_create(w->gs,64,32,32,&w->alpha_source)) ||
        (!w->alpha_destination && ps2_surface_create(w->gs,64,32,32,&w->alpha_destination)) ||
        (!w->palette && ps2_surface_create(w->gs,16,16,32,&w->palette)))return -1;
    return 0;
}
static int clear(struct ps2_rop_workspace *w, uint32_t handle, unsigned width, unsigned height)
{ return ps2_surface_solid_logic(w->gs,handle,0,0,width,height,0,0xffffff,0); }
int ps2_rop_capture(struct ps2_rop_workspace *w, uint32_t source, uint32_t snapshot,
    unsigned sx, unsigned sy, unsigned width, unsigned height, unsigned *offset)
{
    const struct ps2_surface *s=ps2_surface_lookup(w->gs,source);
    unsigned unit=s->bits_per_pixel==16 ? 4 : 2, aligned=(width+unit-1)&~(unit-1);
    *offset=0;
    if (s->width<unit) {
        return ps2_rop_sample(w->gs,source,snapshot,sx,sy,0,0,width,height,
            s->bits_per_pixel==16?0x7fff:0xffffff,PS2GS_TRANSFER_REPLACE,128);
    }
    /* A whole odd-width pixmap may be narrower than its rounded transfer.
     * Copy the aligned prefix and the final complete unit at their exact
     * positions. The overlapping snapshot pixels receive identical values;
     * this also handles tall pixmaps without texturing the original source. */
    if (aligned>s->width) {
        unsigned prefix=width&~(unit-1);
        if (ps2_surface_copy(w->gs,source,snapshot,sx,sy,0,0,prefix,height)) return -1;
        return ps2_surface_copy(w->gs,source,snapshot,sx+width-unit,sy,width-unit,0,unit,height);
    }
    if (sx+aligned>s->width) { *offset=sx+aligned-s->width; sx-=*offset; }
    return ps2_surface_copy(w->gs,source,snapshot,sx,sy,0,0,aligned,height);
}
static int rgb_rectangle(struct ps2_rop_workspace *w, uint32_t source, uint32_t destination,
    unsigned sx, unsigned sy, unsigned dx, unsigned dy, unsigned width, unsigned height,
    uint32_t mask, unsigned alu)
{
    struct ps2_surface_client *gs=w->gs;
    const struct ps2_surface *d=ps2_surface_lookup(gs,destination);
    unsigned so=0,doff=0;
    if (!width || !height || !mask || alu==5) return 0;
    if (width>64 || height>32 || alu>15) { errno=EINVAL; return -1; }
    if (alu==0 || alu==10 || alu==15)
        return ps2_surface_solid_logic(gs,destination,dx,dy,width,height,0,mask,alu);
    if (ps2_rop_capture(w,source,w->source,sx,sy,width,height,&so)) return -1;
    if (alu==3 || alu==12) {
        if (ps2_rop_sample(gs,w->source,destination,so,0,dx,dy,width,height,mask,0,128)) return -1;
        return alu==12 ? ps2_surface_solid_logic(gs,destination,dx,dy,width,height,0,mask,10) : 0;
    }
    /* Algebraic normal form of the X11 truth table over GF(2):
     * f(S,D)=constant XOR source*S XOR dest*D XOR both*(S AND D).
     * Isolated-bit addition modulo 256 is XOR. AND is the carry from S+D.
     * CT16 expands five-bit channels at bits 3..7; CT32 temporaries retain
     * carries that a five-bit scratch target would discard. */
    unsigned constant=(alu>>3)&1, sc=((alu>>1)&1)^constant;
    unsigned dc=((alu>>2)&1)^constant, both=(alu&1)^((alu>>1)&1)^((alu>>2)&1)^constant;
    if (both && ps2_rop_capture(w,destination,w->destination,dx,dy,width,height,&doff)) return -1;
    for (unsigned k=d->bits_per_pixel==16?3:0; k<8; ++k) {
        uint32_t native=d->bits_per_pixel==16 ?
            (1U<<(k-3))|(1U<<(k+2))|(1U<<(k+7)) : (1U<<k)|(1U<<(k+8))|(1U<<(k+16));
        native &= mask;
        if (!native) continue;
        uint32_t rgb=d->bits_per_pixel==16 ?
            ((native&31)<<3)|(((native>>5)&31)<<11)|(((native>>10)&31)<<19) : native;
        if (clear(w,w->a,width,height) || ps2_rop_sample(gs,w->source,w->a,so,0,0,0,width,height,rgb,0,128)) return -1;
        if (both) {
            if (clear(w,w->b,width,height) || clear(w,w->c,width,height)) return -1;
            if (k==7) {
                /* Halve bit 7 before adding, then threshold the retained
                 * bit-7 carry with MODULATE 255/128 and clamping. */
                if (ps2_rop_sample(gs,w->destination,w->b,doff,0,0,0,width,height,rgb>>1,0,64) ||
                    ps2_rop_sample(gs,w->a,w->b,0,0,0,0,width,height,rgb,1,64) ||
                    ps2_rop_sample(gs,w->b,w->c,0,0,0,0,width,height,rgb,0,255)) return -1;
            } else if (ps2_rop_sample(gs,w->destination,w->b,doff,0,0,0,width,height,rgb,0,128) ||
                ps2_rop_sample(gs,w->a,w->b,0,0,0,0,width,height,rgb<<1,1,128) ||
                ps2_rop_sample(gs,w->b,w->c,0,0,0,0,width,height,rgb,0,64)) return -1;
        }
        if (!dc && ps2_surface_solid_logic(gs,destination,dx,dy,width,height,0,native,0)) return -1;
        if (constant && ps2_surface_solid_logic(gs,destination,dx,dy,width,height,0,native,10)) return -1;
        if (sc && ps2_rop_sample(gs,w->a,destination,0,0,dx,dy,width,height,native,1,128)) return -1;
        if (both && ps2_rop_sample(gs,w->c,destination,0,0,dx,dy,width,height,native,1,128)) return -1;
    }
    return 0;
}
int ps2_rop_rectangle(struct ps2_rop_workspace *w,uint32_t source,uint32_t destination,
    unsigned sx,unsigned sy,unsigned dx,unsigned dy,unsigned width,unsigned height,uint32_t mask,unsigned alu)
{
    if(!(mask&0xff000000))return rgb_rectangle(w,source,destination,sx,sy,dx,dy,width,height,mask,alu);
    const struct ps2_surface *s=ps2_surface_lookup(w->gs,source),*d=ps2_surface_lookup(w->gs,destination);
    if(!s || !d || s->bits_per_pixel!=32 || d->bits_per_pixel!=32 || !source || !destination ||
        width>64 || height>32 || alu>15 || (mask&~ps2_surface_native_mask(w->gs,destination))){errno=EINVAL;return -1;}
    if(!width || !height || alu==5)return 0;
    if(ps2_rop_alpha_reserve(w))return -1;
    uint32_t table[256];for(unsigned i=0;i<256;++i)table[i]=i*0x010101U;
    /* Snapshot both alpha operands before colour or alpha writes. Palette
     * mapping reads alpha directly on GS, including one-pixel pixmaps. */
    if(ps2_surface_map_palette(w->gs,w->palette,table) ||
        ps2_surface_channel_map_region(w->gs,source,w->alpha_source,w->palette,sx,sy,0,0,width,height,0xffffff,3) ||
        ps2_surface_channel_map_region(w->gs,destination,w->alpha_destination,w->palette,dx,dy,0,0,width,height,0xffffff,3) ||
        rgb_rectangle(w,source,destination,sx,sy,dx,dy,width,height,mask&0xffffff,alu) ||
        rgb_rectangle(w,w->alpha_source,w->alpha_destination,0,0,0,0,width,height,
            (mask>>24)*0x010101U,alu))return -1;
    return ps2_surface_alpha_write(w->gs,w->alpha_destination,w->alpha_source,destination,w->palette,
        dx,dy,width,height,mask&0xff000000);
}
static int before(const struct ps2_pipe_copy_box *a,const struct ps2_pipe_copy_box *b)
{ return a->dy<b->dy || (a->dy==b->dy && a->dx<b->dx); }
int ps2_pipe_rop_copy_boxes(struct pipe_context *ctx,uint32_t source,uint32_t destination,
    const struct ps2_pipe_copy_box *input,unsigned count,uint32_t mask,unsigned alu)
{
    if (!ctx || count>PS2_PIPE_MAX_COPY_BOXES || (count && !input) || alu>15) { errno=EINVAL; return -1; }
    if (ps2_pipe_context_acquire(ctx)) return -1;
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);
    struct ps2_surface_client *gs=r->gs;
    const struct ps2_surface *s=ps2_surface_lookup(gs,source), *d=ps2_surface_lookup(gs,destination);
    struct ps2_pipe_copy_box boxes[PS2_PIPE_MAX_COPY_BOXES];
    struct ps2_rop_workspace w={0};
    int status=-1,error=EINVAL;
    if (!s || !d || s->bits_per_pixel!=d->bits_per_pixel ||
        (mask&~ps2_surface_native_mask(gs,destination))) goto out;
    int64_t shift_x=count?(int64_t)input[0].dx-input[0].sx:0;
    int64_t shift_y=count?(int64_t)input[0].dy-input[0].sy:0;
    uint64_t pixels=0;
    for (unsigned i=0;i<count;++i) {
        struct ps2_pipe_copy_box b=input[i];
        if (b.sx<0 || b.sy<0 || b.dx<0 || b.dy<0 || b.width<=0 || b.height<=0 ||
            (int64_t)b.sx+b.width>s->width || (int64_t)b.sy+b.height>s->height ||
            (int64_t)b.dx+b.width>d->width || (int64_t)b.dy+b.height>d->height ||
            (int64_t)b.dx-b.sx!=shift_x || (int64_t)b.dy-b.sy!=shift_y) goto out;
        pixels+=(uint64_t)b.width*b.height;
        if (pixels>PS2_PIPE_MAX_COPY_PIXELS) { error=E2BIG; goto out; }
        unsigned j=i;
        while (j && before(&b,boxes+j-1)) { boxes[j]=boxes[j-1]; --j; }
        boxes[j]=b;
    }
    int64_t band_end=0;
    for (unsigned i=0;i<count;++i) {
        const struct ps2_pipe_copy_box *b=boxes+i;
        if (!i || b->dy!=boxes[i-1].dy) {
            if (i && b->dy<band_end) goto out;
            band_end=(int64_t)b->dy+b->height;
        } else if ((int64_t)b->dy+b->height!=band_end || b->dx<(int64_t)boxes[i-1].dx+boxes[i-1].width) goto out;
    }
    if (!count || !mask || alu==5) { status=0; goto out; }
    if (alu==3 && d->pixel_depth!=32 && mask==(d->bits_per_pixel==16?0x7fffU:0xffffffU)) {
        status=ps2_pipe_copy_boxes(ctx,source,destination,boxes,count); error=errno; goto out;
    }
    if ((s->width<(s->bits_per_pixel==16?4U:2U) && (!source || s->height>1024)) ||
        (d->width<(d->bits_per_pixel==16?4U:2U) && (!destination || d->height>1024))) {
        error=ENOTSUP; goto out;
    }
    if (ps2_rop_reserve(&w,gs,d->bits_per_pixel) || ((mask&0xff000000) && ps2_rop_alpha_reserve(&w))) {
        error=errno;ps2_rop_release(&w);goto out;
    }
    status=ps2_render_flush(r);
    unsigned band=count;
    while (!status && band) {
        unsigned first,last;
        if (source==destination && shift_y>0) {
            last=band; first=last-1;
            while (first && boxes[first-1].dy==boxes[last-1].dy) --first;
            band=first;
        } else {
            first=count-band; last=first+1;
            while (last<count && boxes[last].dy==boxes[first].dy) ++last;
            band=count-last;
        }
        for (unsigned j=first; !status && j<last; ++j) {
            unsigned index=source==destination && shift_x>0 ? last-1-(j-first):j;
            const struct ps2_pipe_copy_box *b=boxes+index;
            for (unsigned rows=b->height; !status && rows;) {
                unsigned h=rows>32?32:rows, y=source==destination && shift_y>0?rows-h:b->height-rows;
                rows-=h;
                for (unsigned columns=b->width; !status && columns;) {
                    unsigned width=columns>64?64:columns, x=source==destination && shift_x>0?columns-width:b->width-columns;
                    columns-=width;
                    status=ps2_rop_rectangle(&w,source,destination,b->sx+x,b->sy+y,b->dx+x,b->dy+y,width,h,mask,alu);
                }
            }
        }
    }
    error=errno;
    if (ps2_rop_release(&w) && !status) { status=-1; error=errno; }
out:
    ps2_pipe_context_release(ctx);
    if (status) errno=error;
    return status;
}
static unsigned phase(int coordinate,int32_t origin,unsigned period)
{int64_t v=((int64_t)coordinate-origin)%period;return v<0?v+period:v;}
int ps2_pipe_tile_logic_geometry(struct pipe_context *ctx,uint32_t source,uint32_t destination,
    const struct ps2_pipe_solid_box *input,unsigned count,int32_t ox,int32_t oy,uint32_t mask,unsigned alu)
{
    if(!ctx || count>16384 || (count && !input) || alu>15){errno=EINVAL;return -1;}
    if(ps2_pipe_context_acquire(ctx))return -1;
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);
    struct ps2_surface_client *gs=r->gs;
    const struct ps2_surface *s=ps2_surface_lookup(gs,source),*d=ps2_surface_lookup(gs,destination);
    struct ps2_rop_workspace workspace={0};
    struct ps2_pipe_solid_box *boxes=NULL;
    int status=-1,error=EINVAL;
    if(!source || source==destination || !s || !d || s->bits_per_pixel!=d->bits_per_pixel ||
        s->width>1024 || s->height>1024 || (mask&~ps2_surface_native_mask(gs,destination)))goto out;
    uint64_t pixels=0,pieces=0;
    for(unsigned i=0;i<count;++i){const struct ps2_pipe_solid_box *b=input+i;
        if(b->x1<0 || b->y1<0 || b->x2<=b->x1 || b->y2<=b->y1 ||
            (unsigned)b->x2>d->width || (unsigned)b->y2>d->height)goto out;
        pixels+=(uint64_t)(b->x2-b->x1)*(b->y2-b->y1);
        for(int y=b->y1;y<b->y2;){
            unsigned sy=phase(y,oy,s->height),h=s->height-sy;if(h>32)h=32;if(h>(unsigned)(b->y2-y))h=b->y2-y;
            for(int x=b->x1;x<b->x2;){unsigned sx=phase(x,ox,s->width),w=s->width-sx;
                if(w>64)w=64;
                if(w>(unsigned)(b->x2-x))w=b->x2-x;
                ++pieces;x+=w;}
            y+=h;}
        if(pixels>PS2_PIPE_MAX_TILE_PIXELS || pieces>PS2_PIPE_MAX_TILE_PIECES){error=E2BIG;goto out;}}
    if(!count || !mask || alu==5){status=0;goto out;}
    boxes=malloc(count*sizeof(*boxes));if(!boxes){error=errno;goto out;}
    memcpy(boxes,input,count*sizeof(*boxes));
    if(alu==3 && d->pixel_depth!=32 && mask==(d->bits_per_pixel==16?0x7fffU:0xffffffU)){
        status=ps2_pipe_tile_geometry(ctx,source,destination,boxes,count,ox,oy);error=errno;goto out;}
    if(ps2_rop_reserve(&workspace,gs,d->bits_per_pixel) || ((mask&0xff000000) && ps2_rop_alpha_reserve(&workspace))){
        error=errno;ps2_rop_release(&workspace);goto out;
    }
    status=ps2_render_flush(r);
    for(unsigned i=0;!status && i<count;++i){const struct ps2_pipe_solid_box *b=boxes+i;
        for(int y=b->y1;!status && y<b->y2;){
            unsigned sy=phase(y,oy,s->height),h=s->height-sy;if(h>32)h=32;if(h>(unsigned)(b->y2-y))h=b->y2-y;
            for(int x=b->x1;!status && x<b->x2;){unsigned sx=phase(x,ox,s->width),w=s->width-sx;
                if(w>64)w=64;
                if(w>(unsigned)(b->x2-x))w=b->x2-x;
                status=ps2_rop_rectangle(&workspace,source,destination,sx,sy,x,y,w,h,mask,alu);x+=w;}
            y+=h;}}
    error=errno;
    if(ps2_rop_release(&workspace) && !status){status=-1;error=errno;}
out:
    free(boxes);ps2_pipe_context_release(ctx);if(status)errno=error;return status;
}
int ps2_pipe_download(struct pipe_context *ctx,uint32_t source,unsigned x,unsigned y,
    unsigned width,unsigned height,uint32_t mask,void *pixels,size_t bytes,size_t stride)
{
    if(ps2_pipe_context_acquire(ctx))return -1;
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);
    struct ps2_surface_client *gs=r->gs;
    const struct ps2_surface *s=ps2_surface_lookup(gs,source);
    struct ps2_rop_workspace w={.gs=gs};
    unsigned char packed[8192];
    int status=-1,error=EINVAL;
    if(!s || !pixels || !width || !height || x>=s->width || y>=s->height ||
        width>s->width-x || height>s->height-y || (mask&~ps2_surface_native_mask(gs,source)))goto out;
    unsigned cpp=s->bits_per_pixel/8;size_t row=(size_t)width*cpp;
    if(stride<row || height-1>(SIZE_MAX-row)/stride || bytes<(height-1)*stride+row ||
        (uintptr_t)pixels>UINTPTR_MAX-((height-1)*stride+row-1))goto out;
    if(!(gs->caps.features&PS2GS_HW_READBACK) || !(gs->caps.features&PS2GS_HW_TEXTURE_TRANSFER)){
        error=ENOTSUP;goto out;}
    unsigned raw32=mask==UINT32_MAX && s->bits_per_pixel==32 &&
        (s->width>=2 || (gs->caps.features&PS2GS_HW_RGBA_TRANSFER));
    if((!raw32 && ps2_surface_create(gs,128,32,s->bits_per_pixel,&w.source)) ||
        ps2_surface_create(gs,64,32,s->bits_per_pixel,&w.destination)){error=errno;goto cleanup;}
    if(mask&0xff000000){
        if(ps2_surface_set_depth(gs,w.destination,32)){error=errno;goto cleanup;}
        if(!raw32 && (ps2_surface_create(gs,64,32,32,&w.a) || ps2_surface_create(gs,64,32,32,&w.b) ||
            ps2_surface_create(gs,64,32,32,&w.c) || ps2_rop_alpha_reserve(&w))){error=errno;goto cleanup;}
    }
    status=ps2_render_flush(r);
    for(unsigned yy=0;!status && yy<height;yy+=32)for(unsigned xx=0;!status && xx<width;xx+=64){
        unsigned h=height-yy>32?32:height-yy,ww=width-xx>64?64:width-xx;
        unsigned offset=0;
        if(raw32){
            /* A complete protocol reply needs no plane isolation or colour
             * arithmetic. Capture all four bytes with a GS local copy. The
             * singleton texture path also preserves the source alpha byte. */
            status=s->width<2?ps2_surface_rgba_rectangle(gs,source,w.destination,x+xx,y+yy,0,0,ww,h,
                UINT32_MAX,PS2GS_RGBA_REPLACE):
                ps2_rop_capture(&w,source,w.destination,x+xx,y+yy,ww,h,&offset);
        }else{
            status=ps2_surface_solid_logic(gs,w.destination,0,0,64,32,0,
                s->bits_per_pixel==16?0x7fff:mask&0xff000000?UINT32_MAX:0xffffff,0);
            if(!status && mask)status=ps2_rop_rectangle(&w,source,w.destination,x+xx,y+yy,0,0,ww,h,mask,3);
        }
        if(!status)status=ps2_surface_download(gs,w.destination,0,0,64,32,packed,64*32*cpp);
        if(!status)for(unsigned line=0;line<h;++line)
            memcpy((unsigned char*)pixels+(yy+line)*stride+xx*cpp,packed+(line*64+offset)*cpp,ww*cpp);
    }
    error=errno;
cleanup:
    if(ps2_rop_release(&w) && !status){status=-1;error=errno;}
out:
    ps2_pipe_context_release(ctx);if(status)errno=error;return status;
}
