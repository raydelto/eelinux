/* SPDX-License-Identifier: MIT */
#include "ps2_pipe.h"
#include "ps2_solid_batch.h"
#include "ps2_gs_logic.h"
#include <errno.h>
static int submit_solids(struct ps2_render_client *render,uint32_t handle,
    const struct ps2_pipe_solid_box *boxes,const struct ps2_pipe_solid_run *runs,
    unsigned count,uint32_t color,uint32_t mask,unsigned alu)
{
    if(ps2_render_flush(render))return -1;
    /* Geometry and colours only: destination pixels never leave GS memory.
     * Older kernels retain the identical individually fenced operation. */
    if(!(render->gs->caps.features&PS2GS_HW_SOLID_BATCH)) {
        for(unsigned i=0;i<count;++i) {
            const struct ps2_pipe_solid_box *b=runs ? &runs[i].box : boxes+i;
            if(ps2_surface_solid_logic(render->gs,handle,b->x1,b->y1,
                b->x2-b->x1,b->y2-b->y1,runs ? runs[i].color : color,mask,alu))return -1;
        }
        return 0;
    }
    struct ps2gs_solid_logic batch[PS2GS_MAX_COMMANDS];
    for(unsigned i=0;i<count;) {
        unsigned n=count-i;if(n>PS2GS_MAX_COMMANDS)n=PS2GS_MAX_COMMANDS;
        for(unsigned j=0;j<n;++j) {
            const struct ps2_pipe_solid_box *b=runs ? &runs[i+j].box : boxes+i+j;
            batch[j]=(struct ps2gs_solid_logic){.abi=PS2GS_ABI_VERSION,.handle=handle,
                .x=b->x1,.y=b->y1,.width=b->x2-b->x1,.height=b->y2-b->y1,
                .color=runs ? runs[i+j].color : color,.plane_mask=mask,.alu=alu};
        }
        if(ps2_surface_solid_batch(render->gs,handle,batch,n))return -1;
        i+=n;
    }
    return 0;
}
int ps2_pipe_solid_rectangles(struct pipe_context *context,uint32_t handle,
    const struct ps2_pipe_solid_box *boxes,unsigned count,uint32_t color,uint32_t mask,unsigned alu)
{
    if(!context || count>PS2_PIPE_MAX_SOLID_BOXES || (count && !boxes) || alu>15) {
        errno=EINVAL;return -1;
    }
    if(ps2_pipe_context_acquire(context)) return -1;
    int status=-1,error=EINVAL;
    struct ps2_render_client *render=ps2_pipe_context_render(context);
    const struct ps2_surface *storage=render ? ps2_surface_lookup(render->gs,handle) : NULL;
    if(!storage) goto out;
    uint32_t bits=ps2_surface_native_mask(render->gs,handle);
    if((color|mask)&~bits) goto out;
    if(!(render->gs->caps.features & PS2GS_HW_SOLID_LOGIC)) { error=ENOTSUP;goto out; }
    for(unsigned i=0;i<count;++i) {
        const struct ps2_pipe_solid_box *b=boxes+i;
        if(b->x1<0 || b->y1<0 || b->x2<=b->x1 || b->y2<=b->y1 ||
            (unsigned)b->x2>storage->width || (unsigned)b->y2>storage->height) goto out;
    }
    if(!count || !mask || alu==5) { status=0;goto out; }
    status=submit_solids(render,handle,boxes,NULL,count,color,mask,alu);
    error=errno;
out:
    ps2_pipe_context_release(context);
    if(status) errno=error;
    return status;
}

/* Closed-form Bresenham position. Keeping the product undoubled fits uint64_t
 * even for the full signed-32-bit endpoint range (R5900 has no __int128). */
static uint64_t minor_at(uint64_t k, uint64_t minor, uint64_t major, unsigned tie)
{
    uint64_t product=k*minor, q=product/major, remainder=product%major;
    return q+(2*remainder>=major+tie);
}
static uint64_t clamp_step(int64_t step, uint64_t count)
{ return step<0 ? 0 : (uint64_t)step>count ? count : (uint64_t)step; }
static uint64_t minor_lower(uint64_t lo, uint64_t hi, int64_t value,
                            uint64_t minor, uint64_t major, unsigned tie)
{
    if(value<=0) return lo;
    while(lo<hi) {
        uint64_t mid=lo+(hi-lo)/2;
        if(minor_at(mid,minor,major,tie)<(uint64_t)value) lo=mid+1;
        else hi=mid;
    }
    return lo;
}
static int zero_line(int32_t x1,int32_t y1,int32_t x2,int32_t y2,
    unsigned bias,int include_last,const struct ps2_pipe_solid_box *bounds,
    const struct ps2_pipe_dash_pattern *dash,ps2_pipe_dash_emit emit,void *data)
{
    if(!bounds || !emit || bias>255 || (include_last!=0 && include_last!=1) ||
       bounds->x1>bounds->x2 || bounds->y1>bounds->y2) { errno=EINVAL;return -1; }
    unsigned char lengths[256];unsigned elements=0,period=0,index=0,remaining=0,background=0;
    uint64_t phase=0;
    if(dash) {
        if(!dash->lengths || !dash->count || dash->count>256 ||
            (dash->double_dash!=0 && dash->double_dash!=1)){errno=EINVAL;return -1;}
        elements=dash->count;
        for(unsigned i=0;i<elements;++i) {
            lengths[i]=dash->lengths[i];if(!lengths[i]){errno=EINVAL;return -1;}
            period+=lengths[i];
        }
        if(elements&1) period*=2;
        phase=dash->offset%period;
    }
    if(bounds->x1==bounds->x2 || bounds->y1==bounds->y2) return 0;
    int64_t dx=(int64_t)x2-x1,dy=(int64_t)y2-y1;
    int sx=dx<0 ? -1 : 1, sy=dy<0 ? -1 : 1;
    uint64_t ax=dx<0 ? -dx : dx, ay=dy<0 ? -dy : dy;
    if(!ax && !ay) {
        if(!include_last || x1<bounds->x1 || x1>=bounds->x2 ||
           y1<bounds->y1 || y1>=bounds->y2) return 0;
        if(dash) {
            while(phase>=lengths[index]){phase-=lengths[index];index=(index+1)%elements;background^=1;}
            if(background && !dash->double_dash)return 0;
        }
        struct ps2_pipe_solid_box b={x1,y1,x1+1,y1+1};return emit(data,&b,background);
    }
    int ymajor=ax<=ay;
    unsigned octant=(dx<0 ? 4 : 0)|(dy<0 ? 2 : 0)|(ymajor ? 1 : 0);
    unsigned tie=(bias>>octant)&1;
    uint64_t major=ymajor ? ay : ax,minor=ymajor ? ax : ay;
    uint64_t count=major+include_last;
    int64_t a0=ymajor ? y1 : x1, b0=ymajor ? x1 : y1;
    int sa=ymajor ? sy : sx, sb=ymajor ? sx : sy;
    int64_t amin=ymajor ? bounds->y1 : bounds->x1;
    int64_t amax=ymajor ? bounds->y2 : bounds->x2;
    int64_t bmin=ymajor ? bounds->x1 : bounds->y1;
    int64_t bmax=ymajor ? bounds->x2 : bounds->y2;
    /* Work is bounded by visible major-axis extent, not endpoint distance.
     * Binary-search the monotonic minor coordinate without restarting phase. */
    uint64_t lo=clamp_step(sa>0 ? amin-a0 : a0-amax+1,count);
    uint64_t hi=clamp_step(sa>0 ? amax-a0 : a0-amin+1,count);
    if(lo>=hi) return 0;
    int64_t first=sb>0 ? bmin-b0 : b0-bmax+1;
    int64_t last=sb>0 ? bmax-b0 : b0-bmin+1;
    uint64_t end=minor_lower(lo,hi,last,minor,major,tie);
    lo=minor_lower(lo,end,first,minor,major,tie);hi=end;
    if(lo>=hi) return 0;
    if(dash) {
        phase=(phase+lo%period)%period;
        while(phase>=lengths[index]){phase-=lengths[index];index=(index+1)%elements;background^=1;}
        remaining=lengths[index]-phase;
    }
    struct ps2_pipe_solid_box run={0};
    unsigned prior_background=0;
    uint64_t prior=UINT64_MAX;
    for(uint64_t k=lo;k<hi;++k) {
        uint64_t m=minor_at(k,minor,major,tie);
        int64_t a=a0+sa*(int64_t)k,b=b0+sb*(int64_t)m;
        int32_t x=ymajor ? b : a,y=ymajor ? a : b;
        if(dash && background && !dash->double_dash) {
            if(prior!=UINT64_MAX && emit(data,&run,prior_background))return -1;
            prior=UINT64_MAX;
        } else if(m!=prior || background!=prior_background) {
            if(prior!=UINT64_MAX && emit(data,&run,prior_background)) return -1;
            run=(struct ps2_pipe_solid_box){x,y,x+1,y+1};prior=m;prior_background=background;
        } else {
            if(x<run.x1) run.x1=x;
            if(y<run.y1) run.y1=y;
            if(x>=run.x2) run.x2=x+1;
            if(y>=run.y2) run.y2=y+1;
        }
        if(dash && !--remaining) {
            index=(index+1)%elements;remaining=lengths[index];background^=1;
        }
    }
    return prior==UINT64_MAX ? 0 : emit(data,&run,prior_background);
}
struct solid_emitter { ps2_pipe_line_emit emit; void *data; };
static int solid_run(void *data,const struct ps2_pipe_solid_box *b,unsigned background)
{ (void)background;struct solid_emitter *e=data;return e->emit(e->data,b); }
int ps2_pipe_zero_line(int32_t x1,int32_t y1,int32_t x2,int32_t y2,unsigned bias,
    int include_last,const struct ps2_pipe_solid_box *bounds,ps2_pipe_line_emit emit,void *data)
{
    if(!emit){errno=EINVAL;return -1;}
    struct solid_emitter e={emit,data};
    return zero_line(x1,y1,x2,y2,bias,include_last,bounds,NULL,solid_run,&e);
}
int ps2_pipe_zero_dash(int32_t x1,int32_t y1,int32_t x2,int32_t y2,unsigned bias,
    int include_last,const struct ps2_pipe_solid_box *bounds,const struct ps2_pipe_dash_pattern *dash,
    ps2_pipe_dash_emit emit,void *data)
{
    if(!dash){errno=EINVAL;return -1;}
    return zero_line(x1,y1,x2,y2,bias,include_last,bounds,dash,emit,data);
}
int ps2_pipe_solid_runs(struct pipe_context *context,uint32_t handle,
    const struct ps2_pipe_solid_run *runs,unsigned count,uint32_t mask,unsigned alu)
{
    if(!context || count>PS2_PIPE_MAX_SOLID_BOXES || (count && !runs) || alu>15){errno=EINVAL;return -1;}
    if(ps2_pipe_context_acquire(context))return -1;
    int status=-1,error=EINVAL;
    struct ps2_render_client *render=ps2_pipe_context_render(context);
    const struct ps2_surface *s=render ? ps2_surface_lookup(render->gs,handle) : NULL;
    if(!s)goto out;
    uint32_t bits=ps2_surface_native_mask(render->gs,handle);
    if(mask&~bits)goto out;
    if(!(render->gs->caps.features&PS2GS_HW_SOLID_LOGIC)){error=ENOTSUP;goto out;}
    for(unsigned i=0;i<count;++i){
        const struct ps2_pipe_solid_box *b=&runs[i].box;
        if((runs[i].color&~bits) || b->x1<0 || b->y1<0 || b->x2<=b->x1 || b->y2<=b->y1 ||
            (unsigned)b->x2>s->width || (unsigned)b->y2>s->height)goto out;
    }
    if(!count || !mask || alu==5){status=0;goto out;}
    status=submit_solids(render,handle,NULL,runs,count,0,mask,alu);
    error=errno;
out:
    ps2_pipe_context_release(context);if(status)errno=error;return status;
}

