/* SPDX-License-Identifier: MIT */
#include "ps2_pipe.h"
#include "ps2_tile.h"
#include "ps2_gs_logic.h"
#include "ps2_gs_client.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static uint32_t bits(float value) { uint32_t result; memcpy(&result, &value, 4); return result; }
static unsigned padded(unsigned value) { unsigned result=1; while(result<value) result*=2; return result; }
static unsigned phase(int coordinate, int32_t origin, unsigned size)
{
    int64_t value=((int64_t)coordinate-origin)%size;
    return value<0 ? value+size : value;
}
static void rectangle(struct ps2gs_texture_triangle *triangles, unsigned tw, unsigned th,
    unsigned sx, unsigned sy, unsigned dx, unsigned dy, unsigned width, unsigned height)
{
    const unsigned x[4]={dx,dx+width,dx+width,dx},y[4]={dy,dy,dy+height,dy+height};
    const unsigned u[4]={sx,sx+width,sx+width,sx},v[4]={sy,sy,sy+height,sy+height};
    static const unsigned indices[2][3]={{0,1,2},{0,2,3}};
    memset(triangles,0,2*sizeof(*triangles));
    for(unsigned i=0;i<2;++i) for(unsigned j=0;j<3;++j) {
        unsigned k=indices[i][j];
        triangles[i].v[j]=(struct ps2gs_texture_vertex){
            .position={.x=x[k]*16,.y=y[k]*16,.rgba=0x80808080},
            .s=bits((float)u[k]/padded(tw)),.t=bits((float)v[k]/padded(th)),.q=0x3f800000};
    }
}
static int expand(struct ps2_surface_client *gs, uint32_t source, uint32_t scratch,
    unsigned tw, unsigned th, unsigned width, unsigned height, unsigned base)
{
    struct ps2gs_texture_triangle triangles[8];
    unsigned n=base/tw;
    for(unsigned i=0;i<n;++i) rectangle(triangles+2*i,tw,th,0,0,i*tw,0,tw,th);
    struct ps2_render_client temporary={.gs=gs,.target=scratch};
    if(ps2_texture_triangles(&temporary,source,triangles,2*n)) return -1;
    for(unsigned filled=base;filled<width;) {
        unsigned step=width-filled<filled ? width-filled : filled;
        if(ps2_surface_copy(gs,scratch,scratch,0,0,filled,0,step,th)) return -1;
        filled+=step;
    }
    for(unsigned filled=th;filled<height;) {
        unsigned step=height-filled<filled ? height-filled : filled;
        if(ps2_surface_copy(gs,scratch,scratch,0,0,0,filled,width,step)) return -1;
        filled+=step;
    }
    return 0;
}
static int mono_seed(struct ps2_surface_client *gs, uint32_t target,
    const struct ps2_pipe_mono_pattern *mono, uint32_t foreground, uint32_t background, uint32_t mask)
{
    if(ps2_surface_solid_logic(gs,target,0,0,mono->width,mono->height,background,mask,3)) return -1;
    if(foreground==background) return 0;
    for(unsigned y=0;y<mono->height;++y) {
        const unsigned char *row=mono->bits+y*mono->stride;
        unsigned x=0;
        while(x<mono->width) {
            unsigned left=x;
            int value=!!(row[x/8] & (mono->msb_first ? 0x80U>>(x%8) : 1U<<(x%8)));
            do {
                ++x;
            } while(x<mono->width && value==!!(row[x/8] &
                (mono->msb_first ? 0x80U>>(x%8) : 1U<<(x%8))));
            if(value && ps2_surface_solid_logic(gs,target,left,y,x-left,1,foreground,mask,3)) return -1;
        }
    }
    return 0;
}
static int tiles(struct pipe_context *context, uint32_t source, uint32_t destination,
    const struct ps2_pipe_solid_box *boxes, unsigned count, int32_t origin_x, int32_t origin_y,
    const struct ps2_pipe_mono_pattern *mono, uint32_t foreground, uint32_t background,unsigned box_limit)
{
    if(count>box_limit || (count && !boxes)) { errno=EINVAL; return -1; }
    if(ps2_pipe_context_acquire(context)) return -1;
    struct ps2_render_client *render=ps2_pipe_context_render(context);
    struct ps2_surface_client *gs=render->gs;
    const struct ps2_surface *s=ps2_surface_lookup(gs,source),*d=ps2_surface_lookup(gs,destination);
    int status=-1,error=EINVAL;
    uint32_t scratch=0, pattern=0;
    unsigned char *snapshot=NULL;
    if(!d || (d->bits_per_pixel!=16 && d->bits_per_pixel!=32) ||
        d->width>2048 || d->height>2048) goto out;
    if(!mono && (!source || source==destination || !s || s->bits_per_pixel!=d->bits_per_pixel ||
        !s->width || !s->height || s->width>1024 || s->height>1024)) goto out;
    if(!(gs->caps.features&PS2GS_HW_TEXTURE)) { error=ENOTSUP; goto out; }
    uint32_t mask=d->bits_per_pixel==16 ? 0x7fff : 0xffffff;
    if(mono) {
        if(!mono->width || !mono->height || mono->width>256 || mono->height>256 ||
            !mono->bits || (mono->msb_first!=0 && mono->msb_first!=1) ||
            ((foreground|background)&~mask)) goto out;
        size_t row=(mono->width+7)/8;
        if(mono->stride<row || mono->height-1>(SIZE_MAX-row)/mono->stride) goto out;
        size_t bytes=(mono->height-1)*mono->stride+row;
        if(mono->bytes<bytes || (uintptr_t)mono->bits>UINTPTR_MAX-(bytes-1)) goto out;
        if(!(gs->caps.features&PS2GS_HW_SOLID_LOGIC)) { error=ENOTSUP; goto out; }
    }
    unsigned tw=mono ? mono->width : s->width,th=mono ? mono->height : s->height;
    unsigned width=tw,height=th,base=tw;
    /* The seed's repeated width must satisfy GS local-transfer alignment.
     * At most four native texture rectangles seed it, even for a 1x1 tile. */
    unsigned unit=d->bits_per_pixel==16 ? 4 : 2;
    while(base%unit) base+=tw;
    if(base<=64 && th<=32 && (gs->caps.features&PS2GS_HW_COPY)) {
        width=64/base*base; height=32/th*th;
    }
    uint64_t pixels=0,pieces=0;
    for(unsigned i=0;i<count;++i) {
        const struct ps2_pipe_solid_box *b=boxes+i;
        if(b->x1<0 || b->y1<0 || b->x2<=b->x1 || b->y2<=b->y1 ||
            (unsigned)b->x2>d->width || (unsigned)b->y2>d->height) goto out;
        unsigned w=b->x2-b->x1,h=b->y2-b->y1;
        pixels+=(uint64_t)w*h;
        pieces+=(uint64_t)((phase(b->x1,origin_x,width)+w+width-1)/width)*
            ((phase(b->y1,origin_y,height)+h+height-1)/height);
        if(pixels>PS2_PIPE_MAX_TILE_PIXELS || pieces>PS2_PIPE_MAX_TILE_PIECES) { error=E2BIG; goto out; }
    }
    if(!count) { status=0; goto out; }
    struct ps2_pipe_mono_pattern saved={0};
    if(mono) {
        size_t row=(tw+7)/8;
        snapshot=malloc(row*th);
        if(!snapshot) { error=errno; goto out; }
        for(unsigned y=0;y<th;++y) memcpy(snapshot+y*row,mono->bits+y*mono->stride,row);
        saved=*mono; saved.bits=snapshot; saved.stride=row; saved.bytes=row*th;
        if(ps2_surface_create(gs,tw,th,d->bits_per_pixel,&pattern)) { error=errno; goto cleanup; }
        source=pattern;
    }
    if((width!=tw || height!=th) && ps2_surface_create(gs,width,height,d->bits_per_pixel,&scratch)) {
        error=errno; goto cleanup;
    }
    status=ps2_render_flush(render);
    if(!status && mono) status=mono_seed(gs,source,&saved,foreground,background,mask);
    if(!status && scratch) status=expand(gs,source,scratch,tw,th,width,height,base);
    struct ps2_render_client temporary={.gs=gs,.target=destination};
    struct ps2gs_texture_triangle triangles[64];
    unsigned used=0;
    for(unsigned i=0;!status && i<count;++i) {
        const struct ps2_pipe_solid_box *b=boxes+i;
        for(int y=b->y1;!status && y<b->y2;) {
            unsigned sy=phase(y,origin_y,height),h=height-sy;
            if(h>(unsigned)(b->y2-y)) h=b->y2-y;
            for(int x=b->x1;!status && x<b->x2;) {
                unsigned sx=phase(x,origin_x,width),w=width-sx;
                if(w>(unsigned)(b->x2-x)) w=b->x2-x;
                rectangle(triangles+used,width,height,sx,sy,x,y,w,h); used+=2; x+=w;
                if(used==64) { status=ps2_texture_triangles(&temporary,scratch?scratch:source,triangles,used); used=0; }
            }
            y+=h;
        }
    }
    if(!status && used) status=ps2_texture_triangles(&temporary,scratch?scratch:source,triangles,used);
    error=errno;
cleanup:
    if(scratch && !gs->error) {
        int cleanup=ps2_surface_destroy(gs,scratch);
        if(!status && cleanup) { status=-1; error=errno; }
    }
    if(pattern && !gs->error) {
        int cleanup=ps2_surface_destroy(gs,pattern);
        if(!status && cleanup) { status=-1; error=errno; }
    }
    /* A sticky device failure leaves scratch to exclusive-file teardown. */
out:
    free(snapshot);
    ps2_pipe_context_release(context);
    if(status) errno=error;
    return status;
}
int ps2_pipe_tile_rectangles(struct pipe_context *context, uint32_t source, uint32_t destination,
    const struct ps2_pipe_solid_box *boxes, unsigned count, int32_t origin_x, int32_t origin_y)
{
    return tiles(context,source,destination,boxes,count,origin_x,origin_y,NULL,0,0,PS2_PIPE_MAX_TILE_BOXES);
}
int ps2_pipe_tile_geometry(struct pipe_context *context,uint32_t source,uint32_t destination,
    const struct ps2_pipe_solid_box *boxes,unsigned count,int32_t origin_x,int32_t origin_y)
{
    if(count>16384 || (count && !boxes)){errno=EINVAL;return -1;}
    struct ps2_pipe_solid_box *saved=count?malloc(count*sizeof(*saved)):NULL;
    if(count && !saved){errno=ENOMEM;return -1;}
    if(count)memcpy(saved,boxes,count*sizeof(*saved));
    int result=tiles(context,source,destination,saved,count,origin_x,origin_y,NULL,0,0,16384);
    int error=errno;free(saved);if(result)errno=error;return result;
}
int ps2_pipe_opaque_stipple_rectangles(struct pipe_context *context, uint32_t destination,
    const struct ps2_pipe_mono_pattern *mono, const struct ps2_pipe_solid_box *boxes, unsigned count,
    int32_t origin_x, int32_t origin_y, uint32_t foreground, uint32_t background)
{
    if(!mono) { errno=EINVAL; return -1; }
    return tiles(context,0,destination,boxes,count,origin_x,origin_y,mono,foreground,background,PS2_PIPE_MAX_TILE_BOXES);
}
static int mono_bit(const struct ps2_pipe_mono_pattern *mono,unsigned x,unsigned y)
{
    return !!(mono->bits[y*mono->stride+x/8] &
        (mono->msb_first ? 0x80U>>(x%8) : 1U<<(x%8)));
}
static void mono_bands(const struct ps2_pipe_mono_pattern *mono,uint16_t *heights)
{
    unsigned char equal[256];unsigned all=1;
    for(unsigned y=0;y<mono->height;++y) {
        equal[y]=1;
        for(unsigned x=0;x<mono->width;++x)
            if(mono_bit(mono,x,y)!=mono_bit(mono,x,(y+1)%mono->height)) { equal[y]=0;break; }
        all&=equal[y];
    }
    for(unsigned y=0;y<mono->height;++y) {
        unsigned h=1;
        if(all) h=mono->height;
        else while(h<mono->height && equal[(y+h-1)%mono->height]) ++h;
        heights[y]=h;
    }
}
/* A null render counts the exact output without issuing any device operation.
 * The second pass consumes the same immutable snapshot and preserves request
 * order, including overlapping rectangles with non-idempotent Boolean ALUs. */
static int mono_runs(const struct ps2_pipe_mono_pattern *mono,const uint16_t *heights,
    const struct ps2_pipe_solid_box *boxes,unsigned count,int32_t ox,int32_t oy,
    struct ps2_render_client *render,uint32_t color,uint32_t background,uint32_t mask,unsigned alu,
    int opaque,int uniform,int fast,unsigned limit,unsigned *runs)
{
    unsigned used=0;
    for(unsigned i=0;i<count;++i) {
        const struct ps2_pipe_solid_box *b=boxes+i;
        for(int y=b->y1;y<b->y2;) {
            unsigned sy=phase(y,oy,mono->height),h=heights[sy];
            if(uniform || h==mono->height || h>(unsigned)(b->y2-y)) h=b->y2-y;
            unsigned sx=phase(b->x1,ox,mono->width);
            int x=b->x1;
            while(x<b->x2) {
                int left=x,value=uniform ? 1 : mono_bit(mono,sx,sy);
                if(uniform) x=b->x2;
                else do { ++x;if(++sx==mono->width) sx=0; }
                while(x<b->x2 && value==mono_bit(mono,sx,sy));
                if(!opaque && !value) continue;
                if(used==limit) { errno=E2BIG;return -1; }
                ++used;
                uint32_t source=value ? color : background;
                if(render && (fast ? ps2_render_fill(render,left,y,x-left,h,source) :
                    ps2_surface_solid_logic(render->gs,render->target,left,y,x-left,h,source,mask,alu))) return -1;
            }
            y+=h;
        }
    }
    *runs=used;return 0;
}
static int stipple_runs(struct pipe_context *context,uint32_t destination,
    const struct ps2_pipe_mono_pattern *mono,const struct ps2_pipe_solid_box *boxes,unsigned count,
    int32_t ox,int32_t oy,uint32_t foreground,uint32_t background,uint32_t mask,unsigned alu,
    int opaque,unsigned *emitted)
{
    if(emitted) *emitted=0;
    if(!mono || count>PS2_PIPE_MAX_TILE_BOXES || (count && !boxes) || alu>15) { errno=EINVAL;return -1; }
    if(ps2_pipe_context_acquire(context)) return -1;
    struct ps2_render_client *original=ps2_pipe_context_render(context);
    struct ps2_surface_client *gs=original->gs;
    const struct ps2_surface *d=ps2_surface_lookup(gs,destination);
    int status=-1,error=EINVAL;unsigned char *snapshot=NULL;
    if(!d || (d->bits_per_pixel!=16 && d->bits_per_pixel!=32) || d->width>2048 || d->height>2048 ||
        !mono->bits || !mono->width || !mono->height || mono->width>256 || mono->height>256 ||
        (mono->msb_first!=0 && mono->msb_first!=1)) goto out;
    uint32_t bits=ps2_surface_native_mask(gs,destination);
    if((foreground|mask|(opaque ? background : 0))&~bits) goto out;
    size_t row=(mono->width+7)/8;
    if(mono->stride<row || mono->height-1>(SIZE_MAX-row)/mono->stride) goto out;
    size_t bytes=(mono->height-1)*mono->stride+row;
    if(mono->bytes<bytes || (uintptr_t)mono->bits>UINTPTR_MAX-(bytes-1)) goto out;
    uint64_t pixels=0;
    for(unsigned i=0;i<count;++i) {
        const struct ps2_pipe_solid_box *b=boxes+i;
        if(b->x1<0 || b->y1<0 || b->x2<=b->x1 || b->y2<=b->y1 ||
            (unsigned)b->x2>d->width || (unsigned)b->y2>d->height) goto out;
        pixels+=(uint64_t)(b->x2-b->x1)*(b->y2-b->y1);
        if(pixels>PS2_PIPE_MAX_TILE_PIXELS) { error=E2BIG;goto out; }
    }
    if(!count || !mask || alu==5) { status=0;goto out; }
    int fast=d->pixel_depth!=32 && alu==3 && mask==bits && (gs->caps.features&PS2GS_HW_OWNED_RENDER);
    if(!fast && !(gs->caps.features&PS2GS_HW_SOLID_LOGIC)) { error=ENOTSUP;goto out; }
    snapshot=malloc(row*mono->height);
    if(!snapshot) { error=errno;goto out; }
    for(unsigned y=0;y<mono->height;++y) memcpy(snapshot+y*row,mono->bits+y*mono->stride,row);
    struct ps2_pipe_mono_pattern saved=*mono;saved.bits=snapshot;saved.stride=row;saved.bytes=row*mono->height;
    uint16_t heights[256];mono_bands(&saved,heights);
    unsigned runs=0,limit=fast ? PS2_PIPE_MAX_STIPPLE_RUNS : PS2_PIPE_MAX_STIPPLE_LOGIC_RUNS;
    int uniform=opaque && !((foreground^background)&mask);
    if(mono_runs(&saved,heights,boxes,count,ox,oy,NULL,0,0,mask,alu,opaque,uniform,fast,limit,&runs)) { error=errno;goto out; }
    if(!runs) { status=0;goto out; }
    struct ps2_render_client temporary={.gs=gs,.target=destination};
    uint32_t color=fast ? ps2_gs_pixel_color(foreground,d->bits_per_pixel) : foreground;
    uint32_t bg=fast ? ps2_gs_pixel_color(background,d->bits_per_pixel) : background;
    status=ps2_render_flush(original);
    if(!status) status=mono_runs(&saved,heights,boxes,count,ox,oy,&temporary,color,bg,mask,alu,opaque,uniform,fast,limit,&runs);
    if(!status && fast) status=ps2_render_flush(&temporary);
    error=errno;
    if(!status && emitted) *emitted=runs;
out:
    free(snapshot);ps2_pipe_context_release(context);
    if(status) errno=error;
    return status;
}
int ps2_pipe_stipple_rectangles(struct pipe_context *context,uint32_t destination,
    const struct ps2_pipe_mono_pattern *mono,const struct ps2_pipe_solid_box *boxes,unsigned count,
    int32_t ox,int32_t oy,uint32_t foreground,uint32_t mask,unsigned alu,unsigned *emitted)
{
    return stipple_runs(context,destination,mono,boxes,count,ox,oy,foreground,0,mask,alu,0,emitted);
}
int ps2_pipe_opaque_stipple_logic_rectangles(struct pipe_context *context,uint32_t destination,
    const struct ps2_pipe_mono_pattern *mono,const struct ps2_pipe_solid_box *boxes,unsigned count,
    int32_t ox,int32_t oy,uint32_t foreground,uint32_t background,uint32_t mask,unsigned alu,unsigned *emitted)
{
    return stipple_runs(context,destination,mono,boxes,count,ox,oy,foreground,background,mask,alu,1,emitted);
}
