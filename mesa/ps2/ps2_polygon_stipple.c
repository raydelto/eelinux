/* SPDX-License-Identifier: MIT */
#include "ps2_polygon_stipple.h"
#include "ps2_gs_logic.h"
#include "ps2_gs_render.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
static unsigned phase(int32_t position,int32_t origin,unsigned size)
{
    int64_t n=((int64_t)position-origin)%size;
    return n<0?n+size:n;
}
static int bit(const struct ps2_pipe_mono_pattern *p,unsigned x,unsigned y)
{
    return !!(p->bits[y*p->stride+x/8] & (p->msb_first?0x80U>>(x%8):1U<<(x%8)));
}
int ps2_pipe_stipple_polygon(struct pipe_context *ctx,uint32_t destination,unsigned bpp,
    const struct ps2_pipe_polygon_point *points,unsigned count,
    const struct ps2_pipe_solid_box *clips,unsigned clip_count,unsigned winding,
    const struct ps2_pipe_mono_pattern *input,unsigned opaque,int32_t ox,int32_t oy,
    uint32_t foreground,uint32_t background,uint32_t mask,unsigned alu,unsigned *emitted)
{
    if(emitted)*emitted=0;
    if(!input || (bpp!=16 && bpp!=32) || opaque>1 || alu>15){errno=EINVAL;return -1;}
    /* Copy the header before any allocation, reservation or queue operation. */
    struct ps2_pipe_mono_pattern mono=*input;
    uint32_t bits=bpp==16?0x7fff:UINT32_MAX;
    if(!mono.bits || !mono.width || !mono.height || mono.width>256 || mono.height>256 ||
       (mono.msb_first!=0 && mono.msb_first!=1) ||
       ((foreground|mask|(opaque?background:0))&~bits)){errno=EINVAL;return -1;}
    size_t row=(mono.width+7)/8;
    if(mono.stride<row || mono.height-1>(SIZE_MAX-row)/mono.stride){errno=EINVAL;return -1;}
    size_t bytes=(mono.height-1)*mono.stride+row;
    if(mono.bytes<bytes || (uintptr_t)mono.bits>UINTPTR_MAX-(bytes-1)){errno=EINVAL;return -1;}
    if(ps2_pipe_context_acquire(ctx))return -1;
    struct ps2_render_client *render=ps2_pipe_context_render(ctx);
    struct ps2_pipe_solid_run *spans=NULL,*runs=NULL;
    unsigned char *snapshot=NULL;
    unsigned span_count=0,run_count=0;
    int result=-1,error=EINVAL,full_copy=0;
    const struct ps2_surface *d=ps2_surface_lookup(render->gs,destination);
    if(!d || d->bits_per_pixel!=bpp)goto out;
    bits=ps2_surface_native_mask(render->gs,destination);
    if((foreground|mask|(opaque?background:0))&~bits)goto out;
    full_copy=alu==3 && mask==bits;
    if(mask && alu!=5 && !(render->gs->caps.features&(full_copy?PS2GS_HW_FILL:PS2GS_HW_SOLID_LOGIC))){error=ENOTSUP;goto out;}
    if(ps2_pipe_polygon_spans(ctx,destination,bpp,points,count,clips,clip_count,winding,&spans,&span_count)){
        error=errno;goto out;
    }
    if(!span_count || !mask || alu==5){result=0;goto out;}
    uint64_t pixels=0;
    for(unsigned i=0;i<span_count;++i){
        pixels+=(unsigned)(spans[i].box.x2-spans[i].box.x1);
        if(pixels>PS2_PIPE_MAX_POLYGON_STIPPLE_PIXELS){error=E2BIG;goto out;}
    }
    snapshot=malloc(row*mono.height);runs=malloc(PS2_PIPE_MAX_SOLID_BOXES*sizeof(*runs));
    if(!snapshot || !runs){error=ENOMEM;goto out;}
    for(unsigned y=0;y<mono.height;++y)memcpy(snapshot+y*row,mono.bits+y*mono.stride,row);
    mono.bits=snapshot;mono.stride=row;mono.bytes=row*mono.height;
    int uniform=opaque && !((foreground^background)&mask);
    for(unsigned i=0;i<span_count;++i){
        struct ps2_pipe_solid_box b=spans[i].box;
        unsigned sy=phase(b.y1,oy,mono.height),sx=phase(b.x1,ox,mono.width);
        for(int x=b.x1;x<b.x2;){
            int left=x,value=uniform?1:bit(&mono,sx,sy);
            if(uniform)x=b.x2;
            else do{++x;if(++sx==mono.width)sx=0;}while(x<b.x2 && value==bit(&mono,sx,sy));
            if(!opaque && !value)continue;
            if(run_count==PS2_PIPE_MAX_SOLID_BOXES){error=E2BIG;goto out;}
            runs[run_count++]=(struct ps2_pipe_solid_run){.box={left,b.y1,x,b.y2},
                .color=value?foreground:background};
        }
    }
    if(!run_count){result=0;goto out;}
    if(full_copy){
        result=ps2_render_flush(render);
        for(unsigned i=0;!result && i<run_count;++i){
            const struct ps2_pipe_solid_box *b=&runs[i].box;
            uint32_t colour=runs[i].color;
            if(bpp==16)colour=((colour&31)<<3)|(((colour>>5)&31)<<11)|(((colour>>10)&31)<<19);
            result=ps2_surface_fill(render->gs,destination,b->x1,b->y1,b->x2-b->x1,1,d->pixel_depth>=30?colour:0x80000000|colour);
        }
    }else result=ps2_pipe_solid_runs(ctx,destination,runs,run_count,mask,alu);
    error=errno;
    if(!result && emitted)*emitted=run_count;
out:
    free(runs);free(snapshot);free(spans);ps2_pipe_context_release(ctx);
    if(result)errno=error;
    return result;
}
