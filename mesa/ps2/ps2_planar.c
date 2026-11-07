/* SPDX-License-Identifier: MIT */
#include "ps2_planar.h"
#include "ps2_gs_logic.h"
#include "ps2_gs_render.h"
#include "ps2_solid_batch.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static uint32_t source_colour(const struct ps2_pipe_planar_source *s,
    unsigned x,unsigned y,uint32_t foreground,uint32_t background)
{
    unsigned shift=s->msb_first?7-x%8:x%8;
    size_t offset=y*s->stride+x/8;
    if(s->depth==1)return ((s->bits[offset]>>shift)&1)?foreground:background;
    uint32_t colour=0;
    for(unsigned plane=0;plane<s->depth;++plane)
        colour=(colour<<1)|((s->bits[plane*s->plane_stride+offset]>>shift)&1);
    return colour;
}

int ps2_pipe_planar_boxes(struct pipe_context *ctx,uint32_t destination,
    unsigned bpp,const struct ps2_pipe_planar_source *input_source,
    const struct ps2_pipe_planar_box *input,unsigned count,
    uint32_t foreground,uint32_t background,uint32_t mask,unsigned alu)
{
    if(!ctx || !input_source || (bpp!=16 && bpp!=32) || alu>15 ||
       count>PS2_PIPE_MAX_PLANAR_BOXES || (count && !input)){
        errno=EINVAL;return -1;
    }
    if(ps2_pipe_context_acquire(ctx))return -1;
    struct ps2_pipe_planar_source s=*input_source;
    struct ps2_render_client *render=ps2_pipe_context_render(ctx);
    const struct ps2_surface *d=render?ps2_surface_lookup(render->gs,destination):NULL;
    struct ps2_pipe_planar_box *boxes=NULL;
    struct ps2_pipe_solid_run *runs=NULL;
    unsigned char *snapshot=NULL;
    unsigned bits=ps2_surface_native_mask(render->gs,destination),run_count=0;
    uint64_t area=0;
    int result=-1,error=EINVAL;
    if(!d || d->bits_per_pixel!=bpp || (mask&~bits) ||
       (s.depth!=1 && s.depth!=ps2_surface_depth(d)) ||
       (s.msb_first!=0 && s.msb_first!=1) || s.width>2079 || s.height>2048 ||
       s.bytes>PS2_PIPE_MAX_PLANAR_BYTES || (s.bytes && (!s.bits ||
       (uintptr_t)s.bits>UINTPTR_MAX-(s.bytes-1))))goto out;
    int full_copy=alu==3 && mask==bits;
    if(!(render->gs->caps.features&(full_copy?PS2GS_HW_FILL:PS2GS_HW_SOLID_LOGIC))){error=ENOTSUP;goto out;}
    if(s.width && s.height){
        size_t row=(s.width+7)/8;
        if(s.stride<row || s.plane_stride>s.bytes/s.depth ||
           row>s.plane_stride || s.height-1>(s.plane_stride-row)/s.stride)goto out;
    }else if(count)goto out;
    for(unsigned i=0;i<count;++i){
        const struct ps2_pipe_planar_box *b=&input[i];
        if(!b->width || !b->height || b->source_x>s.width || b->source_y>s.height ||
           b->width>s.width-b->source_x || b->height>s.height-b->source_y ||
           b->x>d->width || b->y>d->height || b->width>d->width-b->x ||
           b->height>d->height-b->y)goto out;
        area+=(uint64_t)b->width*b->height;
        if(area>PS2_PIPE_MAX_PLANAR_PIXELS){error=E2BIG;goto out;}
    }
    if(!count || !mask || alu==5){result=0;goto out;}
    boxes=malloc(count*sizeof(*boxes));snapshot=malloc(s.bytes);
    runs=malloc(PS2_PIPE_MAX_SOLID_BOXES*sizeof(*runs));
    if(!boxes || !snapshot || !runs){error=ENOMEM;goto out;}
    memcpy(boxes,input,count*sizeof(*boxes));memcpy(snapshot,s.bits,s.bytes);s.bits=snapshot;
    foreground&=bits;background&=bits;
    for(unsigned i=0;i<count;++i){
        const struct ps2_pipe_planar_box *b=&boxes[i];
        for(unsigned y=0;y<b->height;++y)for(unsigned x=0;x<b->width;){
            unsigned start=x;
            uint32_t colour=source_colour(&s,b->source_x+x,b->source_y+y,foreground,background);
            do{
                ++x;
                if(x==b->width)break;
            }while(source_colour(&s,b->source_x+x,b->source_y+y,foreground,background)==colour);
            if(run_count==PS2_PIPE_MAX_SOLID_BOXES){error=E2BIG;goto out;}
            runs[run_count++]=(struct ps2_pipe_solid_run){
                .box={b->x+start,b->y+y,b->x+x,b->y+y+1},.color=colour};
        }
    }
    if(full_copy){
        result=ps2_render_flush(render);
        for(unsigned i=0;!result && i<run_count;++i){
            const struct ps2_pipe_solid_run *run=&runs[i];
            uint32_t colour=run->color;
            /* RGBX spare bits are unspecified for full-copy images. A complete
             * colour store also gives freshly allocated GS targets a defined
             * opaque alpha value. Partial masks/other ALUs preserve spare bits. */
            if(bpp==16)colour=((colour&31)<<3)|(((colour>>5)&31)<<11)|(((colour>>10)&31)<<19);
            result=ps2_surface_fill(render->gs,destination,run->box.x1,run->box.y1,
                run->box.x2-run->box.x1,run->box.y2-run->box.y1,d->pixel_depth>=30?colour:0x80000000|colour);
        }
    }else result=ps2_pipe_solid_runs(ctx,destination,runs,run_count,mask,alu);
    error=errno;
out:
    free(runs);free(snapshot);free(boxes);ps2_pipe_context_release(ctx);
    if(result)errno=error;
    return result;
}
