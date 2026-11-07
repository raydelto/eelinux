/* SPDX-License-Identifier: MIT */
#include "ps2_image.h"
#include "ps2_gs_logic.h"
#include "ps2_gs_render.h"
#include "ps2_solid_batch.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
int ps2_pipe_image_boxes(struct pipe_context *ctx,uint32_t destination,
    unsigned bpp,const void *source,size_t bytes,
    const struct ps2_pipe_image_box *input,unsigned count,uint32_t mask,unsigned alu)
{
    if(!ctx || (bpp!=16 && bpp!=32) || alu>15 || count>PS2_PIPE_MAX_IMAGE_BOXES ||
       bytes>PS2_PIPE_MAX_IMAGE_BYTES || (count && !input) || (bytes && !source)){
        errno=EINVAL;return -1;
    }
    if(ps2_pipe_context_acquire(ctx))return -1;
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);
    const struct ps2_surface *d=r?ps2_surface_lookup(r->gs,destination):NULL;
    struct ps2_pipe_image_box *boxes=NULL;
    unsigned char *pixels=NULL;
    struct ps2_pipe_solid_run *runs=NULL;
    int status=-1,error=EINVAL;
    unsigned cpp=bpp/8,bits=ps2_surface_native_mask(r->gs,destination),run_count=0;
    uint64_t area=0;
    if(!d || d->bits_per_pixel!=bpp || (mask&~bits) ||
       (bytes && (uintptr_t)source>UINTPTR_MAX-(bytes-1)))goto out;
    int upload=alu==3 && mask==bits;
    if(!(r->gs->caps.features&(upload?PS2GS_HW_UPLOAD:PS2GS_HW_SOLID_LOGIC))){error=ENOTSUP;goto out;}
    for(unsigned i=0;i<count;++i){
        const struct ps2_pipe_image_box *b=&input[i];
        size_t row=(size_t)b->width*cpp;
        if(!b->width || !b->height || b->x>d->width || b->y>d->height ||
           b->width>d->width-b->x || b->height>d->height-b->y || b->stride<row ||
           b->offset>bytes || row>bytes-b->offset ||
           b->height-1>(bytes-b->offset-row)/b->stride)goto out;
        area+=(uint64_t)b->width*b->height;
        if(area>PS2_PIPE_MAX_IMAGE_PIXELS){error=E2BIG;goto out;}
    }
    if(!count || !mask || alu==5){status=0;goto out;}
    boxes=malloc(count*sizeof(*boxes));pixels=malloc(bytes);
    if(!boxes || !pixels){error=ENOMEM;goto out;}
    memcpy(boxes,input,count*sizeof(*boxes));memcpy(pixels,source,bytes);
    if(!upload){
        runs=malloc(PS2_PIPE_MAX_SOLID_BOXES*sizeof(*runs));
        if(!runs){error=ENOMEM;goto out;}
        for(unsigned i=0;i<count;++i){
            const struct ps2_pipe_image_box *b=&boxes[i];
            for(unsigned y=0;y<b->height;++y)for(unsigned x=0;x<b->width;){
                unsigned start=x;uint32_t color=0,next=0;
                memcpy(&color,pixels+b->offset+y*b->stride+x*cpp,cpp);color&=bits;
                do{
                    ++x;if(x==b->width)break;
                    next=0;memcpy(&next,pixels+b->offset+y*b->stride+x*cpp,cpp);next&=bits;
                }while(next==color);
                if(run_count==PS2_PIPE_MAX_SOLID_BOXES){error=E2BIG;goto out;}
                runs[run_count++]=(struct ps2_pipe_solid_run){
                    .box={b->x+start,b->y+y,b->x+x,b->y+y+1},.color=color};
            }
        }
        status=ps2_pipe_solid_runs(ctx,destination,runs,run_count,mask,alu);
    }else{
        status=ps2_render_flush(r);
        for(unsigned i=0;!status && i<count;++i){
            const struct ps2_pipe_image_box *b=&boxes[i];
            status=ps2_surface_upload(r->gs,destination,b->x,b->y,b->width,b->height,
                pixels+b->offset,bytes-b->offset,b->stride);
        }
    }
    error=errno;
out:
    free(runs);free(pixels);free(boxes);ps2_pipe_context_release(ctx);
    if(status)errno=error;
    return status;
}
