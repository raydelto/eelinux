/* SPDX-License-Identifier: MIT */
#include "ps2_polygon_tile.h"
#include "ps2_tile.h"
#include "ps2_rop.h"
#include <errno.h>
#include <stdlib.h>
int ps2_pipe_tile_logic_polygon(struct pipe_context *ctx,uint32_t source,uint32_t dest,unsigned bpp,
    const struct ps2_pipe_polygon_point *points,unsigned count,const struct ps2_pipe_solid_box *clips,
    unsigned clip_count,unsigned winding,int32_t ox,int32_t oy,uint32_t mask,unsigned alu,unsigned *emitted)
{
    if(emitted)*emitted=0;
    if(ps2_pipe_context_acquire(ctx))return -1;
    struct ps2_pipe_solid_run *spans=NULL;unsigned n=0;
    int status=ps2_pipe_polygon_spans(ctx,dest,bpp,points,count,clips,clip_count,winding,&spans,&n);
    struct ps2_pipe_solid_box *boxes=NULL;
    if(!status && n){
        boxes=malloc(n*sizeof(*boxes));if(!boxes){errno=ENOMEM;status=-1;}
        else{for(unsigned i=0;i<n;++i)boxes[i]=spans[i].box;
            status=ps2_surface_depth(ps2_surface_lookup(ps2_pipe_context_render(ctx)->gs,dest))!=32 && alu==3 && mask==(bpp==16?0x7fffU:0xffffffU) ?
                ps2_pipe_tile_geometry(ctx,source,dest,boxes,n,ox,oy) :
                ps2_pipe_tile_logic_geometry(ctx,source,dest,boxes,n,ox,oy,mask,alu);}
    }
    int error=errno;if(!status && emitted)*emitted=n;
    free(boxes);free(spans);ps2_pipe_context_release(ctx);if(status)errno=error;return status;
}
int ps2_pipe_tile_polygon(struct pipe_context *ctx,uint32_t source,uint32_t dest,unsigned bpp,
    const struct ps2_pipe_polygon_point *points,unsigned count,const struct ps2_pipe_solid_box *clips,
    unsigned clip_count,unsigned winding,int32_t ox,int32_t oy,unsigned *emitted)
{return ps2_pipe_tile_logic_polygon(ctx,source,dest,bpp,points,count,clips,clip_count,winding,ox,oy,
    bpp==16?0x7fff:0xffffff,3,emitted);}
