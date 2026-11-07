/* SPDX-License-Identifier: MIT */
/* Sample positions follow pixman's public fixed-point edge geometry helpers
 * (Keith Packard). No pixman pixel rasterizer or compositor is called here. */
#include "ps2_render_raster.h"
#include "ps2_coverage.h"
#include "ps2_gs_lut.h"
#include "ps2_gs_transfer.h"
#include "ps2-gs-map-blit.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#define TILE 16U
#define MAX_TRAPS 8192U
#define MAX_WORK (16U*1024U*1024U)
struct span { unsigned x1,x2,y; };
struct plan { struct span spans[240];unsigned count,full; };
struct job { pixman_trapezoid_t trap;unsigned x1,y1,x2,y2; };
struct workspace { struct ps2_surface_client *gs;uint32_t grid,vertical,count; };
static unsigned nx(unsigned n){return n==1?1:(1U<<(n/2))+1;}
static unsigned ny(unsigned n){return n==1?1:(1U<<(n/2))-1;}
static int64_t floor_fixed(int64_t v)
{int64_t q=v/65536;return q-(v<0 && v%65536!=0);}
static int sample_x(int32_t x,unsigned bits)
{
    if(bits==1)return (int)floor_fixed((int64_t)x+32767);
    unsigned count=nx(bits),small=65536/count,first=(65536-(count-1)*small)/2;
    int64_t whole=floor_fixed(x);unsigned fraction=(unsigned)((int64_t)x-whole*65536);
    return (int)(whole*count+(fraction+first)/small);
}
static int translated(int32_t value,int offset,int32_t *out)
{
    int64_t v=(int64_t)value+(int64_t)offset*65536;
    if(v<INT32_MIN || v>INT32_MAX){errno=EOVERFLOW;return -1;}
    *out=(int32_t)v;return 0;
}
static int line_x(const pixman_line_fixed_t *line,int32_t y,int64_t *out)
{
    pixman_point_fixed_t a=line->p1,b=line->p2;
    if(a.y>b.y){pixman_point_fixed_t t=a;a=b;b=t;}
    int64_t dx=(int64_t)b.x-a.x,dy=(int64_t)b.y-a.y;
    if(!dy || dy>INT32_MAX || dx<INT32_MIN+1LL || dx>INT32_MAX){errno=EOVERFLOW;return -1;}
    *out=(int64_t)a.x+((int64_t)y-a.y)*dx/dy;
    if(*out<INT32_MIN || *out>INT32_MAX){errno=EOVERFLOW;return -1;}
    return 0;
}
static int make_job(struct job *j,const struct ps2_raster_target *d,
    const pixman_trapezoid_t *in,int xoff,int yoff)
{
    if(!pixman_trapezoid_valid(in))return 0;
    *j=(struct job){.trap=*in};pixman_trapezoid_t *t=&j->trap;
    if(translated(in->top,yoff,&t->top) || translated(in->bottom,yoff,&t->bottom))return -1;
    pixman_line_fixed_t *lines[2]={&t->left,&t->right};
    for(unsigned k=0;k<2;++k){pixman_line_fixed_t original=*lines[k];
        if(translated(original.p1.x,xoff,&lines[k]->p1.x) ||
           translated(original.p2.x,xoff,&lines[k]->p2.x) ||
           translated(original.p1.y,yoff,&lines[k]->p1.y) ||
           translated(original.p2.y,yoff,&lines[k]->p2.y))return -1;}
    int64_t top=t->top>0?t->top:0,bottom=t->bottom;
    if(bottom>(int64_t)d->height*65536)bottom=(int64_t)d->height*65536;
    if(top>=bottom)return 0;
    int32_t first=pixman_sample_ceil_y((int32_t)top,d->depth);
    int32_t last=pixman_sample_floor_y((int32_t)bottom,d->depth);
    if(first>last)return 0;
    int64_t positions[4];
    if(line_x(&t->left,first,&positions[0]) || line_x(&t->left,last,&positions[1]) ||
       line_x(&t->right,first,&positions[2]) || line_x(&t->right,last,&positions[3]))return -1;
    int64_t low=positions[0],high=low;
    for(unsigned k=1;k<4;++k){if(positions[k]<low)low=positions[k];if(positions[k]>high)high=positions[k];}
    low=floor_fixed(low)-1;high=floor_fixed(high)+2;
    if(low<0)low=0;
    if(high>d->width)high=d->width;
    if(low>=high)return 0;
    j->x1=(unsigned)low&~(TILE-1);j->x2=(unsigned)high;
    j->y1=((unsigned)first>>16)&~(TILE-1);j->y2=((unsigned)last>>16)+1;
    return 1;
}
static int make_plan(struct plan *p,const struct job *job,const struct ps2_raster_target *d,
    unsigned x,unsigned y)
{
    *p=(struct plan){0};unsigned xs=nx(d->depth),ys=ny(d->depth);
    unsigned w=d->width-x<TILE?d->width-x:TILE,h=d->height-y<TILE?d->height-y:TILE;
    int32_t top=job->trap.top,bottom=job->trap.bottom;
    if(top<(int32_t)(y*65536U))top=(int32_t)(y*65536U);
    if(bottom>(int32_t)((y+h)*65536U))bottom=(int32_t)((y+h)*65536U);
    int32_t t=pixman_sample_ceil_y(top,d->depth),b=pixman_sample_floor_y(bottom,d->depth);
    if(t>b)return 0;
    pixman_edge_t left,right;
    pixman_line_fixed_edge_init(&left,d->depth,t,&job->trap.left,0,0);
    pixman_line_fixed_edge_init(&right,d->depth,t,&job->trap.right,0,0);
    unsigned small=65536/ys,big=65536-(ys-1)*small,first=big/2,last=first+(ys-1)*small;
    p->full=1;
    for(int32_t at=t;;){
        int l=sample_x(left.x,d->depth)-(int)(x*xs),r=sample_x(right.x,d->depth)-(int)(x*xs);
        if(l<0)l=0;
        if(r>(int)(w*xs))r=w*xs;
        unsigned fraction=(unsigned)at&65535,k=(fraction-first)/small;
        unsigned row=(((unsigned)at>>16)-y)*ys+k;
        if(r>l){
            if(p->count>=240 || row>=TILE*ys){errno=EPROTO;return -1;}
            p->spans[p->count++]=(struct span){(unsigned)l,(unsigned)r,row};
            if(l || r!=(int)(w*xs))p->full=0;
        }else p->full=0;
        if(at==b)break;
        int step=fraction==last?(int)big:(int)small;
        pixman_edge_step(&left,step);pixman_edge_step(&right,step);at+=step;
    }
    p->full=p->full && p->count==h*ys;return 0;
}
static int release(struct workspace *w)
{
    int status=0,error=errno;uint32_t handles[]={w->count,w->vertical,w->grid};
    for(unsigned i=0;i<3;++i)if(handles[i] && !w->gs->error && ps2_surface_destroy(w->gs,handles[i])){
        if(!status)error=errno;
        status=-1;}
    *w=(struct workspace){0};errno=error;return status;
}
static int initialize(struct workspace *w,struct ps2_surface_client *gs)
{
    *w=(struct workspace){.gs=gs};
    if(ps2_surface_create(gs,272,240,32,&w->grid) ||
       ps2_surface_create(gs,272,32,32,&w->vertical) ||
       ps2_surface_create(gs,32,32,32,&w->count)){
        int error=errno;release(w);errno=error;return -1;}
    return 0;
}
static uint32_t float_bits(float v){uint32_t bits;memcpy(&bits,&v,4);return bits;}
struct batch { struct ps2_render_client render;struct ps2gs_texture_triangle triangles[64];unsigned count,tw,th;uint32_t source; };
static int flush_batch(struct batch *b)
{
    if(!b->count)return 0;
    int status=ps2_texture_transfer(&b->render,b->source,b->triangles,b->count,255,PS2GS_TRANSFER_ADD_CLAMP);
    b->count=0;return status;
}
static int quad(struct batch *b,unsigned x,unsigned y,unsigned w,unsigned h,float sx,float sy,float sw,float sh)
{
    if(b->count==64 && flush_batch(b))return -1;
    unsigned px[4]={x,x+w,x+w,x},py[4]={y,y,y+h,y+h};
    float u[4]={sx,sx+sw,sx+sw,sx},v[4]={sy,sy,sy+sh,sy+sh};
    static const unsigned indices[2][3]={{0,1,2},{0,2,3}};
    for(unsigned i=0;i<2;++i){struct ps2gs_texture_triangle *t=b->triangles+b->count++;*t=(struct ps2gs_texture_triangle){0};
        for(unsigned j=0;j<3;++j){unsigned k=indices[i][j];
            t->v[j]=(struct ps2gs_texture_vertex){.position={.x=px[k]*16,.y=py[k]*16,.rgba=0x80808080},
                .s=float_bits(u[k]/b->tw),.t=float_bits(v[k]/b->th),.q=0x3f800000};}}
    return 0;
}
static int resolve(struct workspace *w,unsigned bits)
{
    struct ps2_surface_client *gs=w->gs;unsigned xs=nx(bits),ys=ny(bits);
    if(bits==1)return ps2_surface_copy(gs,w->grid,w->count,0,0,0,0,TILE,TILE);
    if(ps2_surface_fill(gs,w->vertical,0,0,272,32,0) ||
       ps2_surface_fill(gs,w->count,0,0,32,32,0))return -1;
    /* Constant coordinates along the reduced axis avoid strided sampling
     * precision. Along the other axis the quarter-texel identity mapping
     * stays in the same texel under both edge and centre sampling. */
    struct batch batch={.render={.gs=gs,.target=w->vertical},.source=w->grid,.tw=512,.th=256};
    for(unsigned y=0;y<TILE;++y)for(unsigned tap=0;tap<ys;++tap)
        if(quad(&batch,0,y,TILE*xs,1,.25f,y*ys+tap+.25f,TILE*xs,.5f))return -1;
    if(flush_batch(&batch))return -1;
    batch=(struct batch){.render={.gs=gs,.target=w->count},.source=w->vertical,.tw=512,.th=32};
    for(unsigned x=0;x<TILE;++x)for(unsigned tap=0;tap<xs;++tap)
        if(quad(&batch,x,0,1,TILE,x*xs+tap+.25f,.25f,.5f,TILE))return -1;
    return flush_batch(&batch);
}
static int draw_plan(struct workspace *w,const struct plan *p,unsigned bits)
{
    if(p->full)return ps2_surface_fill(w->gs,w->count,0,0,TILE,TILE,(1U<<bits)-1);
    if(ps2_surface_fill(w->gs,w->grid,0,0,272,240,0))return -1;
    struct ps2_render_client render={.gs=w->gs,.target=w->grid};
    for(unsigned i=0;i<p->count;++i){const struct span *s=p->spans+i;
        if(ps2_render_fill(&render,s->x1,s->y,s->x2-s->x1,1,1))return -1;}
    return ps2_render_flush(&render) || resolve(w,bits)?-1:0;
}
static int publish(struct workspace *w,struct pipe_context *ctx,const struct ps2_raster_target *d,unsigned x,unsigned y)
{
    unsigned width=d->width-x<TILE?d->width-x:TILE,height=d->height-y<TILE?d->height-y:TILE;
    size_t offset=(size_t)y*d->stride+(d->depth==1?x/8:x);
    unsigned row=d->depth==1?(width+7)/8:width;
    struct ps2_picture_bitmap bytes={.bits=d->pixels+offset,.bytes=(height-1)*d->stride+row,.stride=d->stride,
        .width=width,.height=height,.msb_first=d->msb_first,.bits_per_pixel=d->depth==1?1:8,.alpha_bits=d->depth};
    struct ps2_coverage_surface old={0};int status=-1,error=errno;
    if(ps2_coverage_decode_core(&old,w->gs,&bytes))return -1;
    struct batch add={.render={.gs=w->gs,.target=old.surface},.source=w->count,.tw=32,.th=32};
    if(quad(&add,0,0,width,height,.25f,.25f,width==1?.5f:width,height==1?.5f:height) || flush_batch(&add))goto out;
    if(d->depth!=8){uint32_t table[256];unsigned max=(1U<<d->depth)-1;
        for(unsigned i=0;i<256;++i)table[i]=i>max?max:i;
        if(ps2_surface_map_palette(w->gs,old.palette,table) ||
           ps2_surface_channel_map_region(w->gs,old.surface,w->count,old.palette,0,0,0,0,width,height,255,0) ||
           ps2_surface_rgba_rectangle(w->gs,w->count,old.surface,0,0,0,0,width,height,255,PS2GS_RGBA_REPLACE))goto out;}
    status=ps2_coverage_encode(&old,ctx,d->pixels+offset,bytes.bytes,d->stride);
out:
    error=errno;if(ps2_coverage_release(&old) && !status){status=-1;error=errno;}errno=error;return status;
}
int ps2_pipe_raster_trapezoids(struct pipe_context *ctx,const struct ps2_raster_target *d,
    int xoff,int yoff,const pixman_trapezoid_t *traps,unsigned count)
{
    if(!ctx || !d || (count && !traps) || count>MAX_TRAPS || d->msb_first>1 ||
        (d->depth!=1 && d->depth!=4 && d->depth!=8) || d->width>2048 || d->height>2048){errno=EINVAL;return -1;}
    if(!count || !d->width || !d->height)return 0;
    unsigned row=d->depth==1?(d->width+7)/8:d->width;
    if(!d->pixels || d->stride<row || d->bytes<row || d->height-1>(d->bytes-row)/d->stride ||
        (uintptr_t)d->pixels>UINTPTR_MAX-(d->bytes-1)){errno=EINVAL;return -1;}
    struct job *jobs=calloc(count,sizeof(*jobs));if(!jobs)return -1;
    unsigned n=0;uint64_t work=0;struct plan plan;int status=-1,error=errno;
    for(unsigned i=0;i<count;++i){int valid=make_job(jobs+n,d,traps+i,xoff,yoff);if(valid<0)goto preflight_out;if(valid)++n;}
    for(unsigned i=0;i<n;++i)for(unsigned y=jobs[i].y1;y<jobs[i].y2;y+=TILE)for(unsigned x=jobs[i].x1;x<jobs[i].x2;x+=TILE){
        if(make_plan(&plan,jobs+i,d,x,y))goto preflight_out;
        work+=plan.count?(plan.full?256:256*nx(d->depth)*ny(d->depth)):1;
        if(work>MAX_WORK){errno=E2BIG;goto preflight_out;}}
    if(!n){status=0;goto preflight_out;}
    if(ps2_pipe_context_acquire(ctx))goto preflight_out;
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);struct workspace workspace={0};
    if(!r || !r->gs){errno=EINVAL;goto acquired_out;}
    unsigned needed=PS2GS_HW_TEXTURE_TRANSFER|PS2GS_HW_CHANNEL_LUT|PS2GS_HW_RGBA_TRANSFER|PS2GS_HW_CHANNEL_MAP_BLIT;
    if((r->gs->caps.features&needed)!=needed){errno=ENOTSUP;goto acquired_out;}
    if(ps2_render_flush(r) || initialize(&workspace,r->gs))goto acquired_out;
    for(unsigned i=0;i<n;++i)for(unsigned y=jobs[i].y1;y<jobs[i].y2;y+=TILE)for(unsigned x=jobs[i].x1;x<jobs[i].x2;x+=TILE){
        if(make_plan(&plan,jobs+i,d,x,y))goto acquired_out;
        if(plan.count && (draw_plan(&workspace,&plan,d->depth) || publish(&workspace,ctx,d,x,y)))goto acquired_out;}
    status=0;
acquired_out:
    error=errno;if(workspace.gs && release(&workspace) && !status){status=-1;error=errno;}
    ps2_pipe_context_release(ctx);errno=error;
preflight_out:
    error=errno;free(jobs);errno=error;return status;
}
int ps2_pipe_raster_traps(struct pipe_context *ctx,const struct ps2_raster_target *d,
    int xoff,int yoff,const pixman_trap_t *in,unsigned count)
{
    if(count>MAX_TRAPS || (count && !in)){errno=EINVAL;return -1;}
    if(!count)return ps2_pipe_raster_trapezoids(ctx,d,xoff,yoff,NULL,0);
    pixman_trapezoid_t *traps=calloc(count,sizeof(*traps));if(!traps)return -1;
    for(unsigned i=0;i<count;++i)traps[i]=(pixman_trapezoid_t){.top=in[i].top.y,.bottom=in[i].bot.y,
        .left={{in[i].top.l,in[i].top.y},{in[i].bot.l,in[i].bot.y}},
        .right={{in[i].top.r,in[i].top.y},{in[i].bot.r,in[i].bot.y}}};
    int status=ps2_pipe_raster_trapezoids(ctx,d,xoff,yoff,traps,count),error=errno;free(traps);errno=error;return status;
}
static int greater(pixman_point_fixed_t a,pixman_point_fixed_t b)
{return a.y>b.y || (a.y==b.y && a.x>b.x);}
int ps2_pipe_raster_triangles(struct pipe_context *ctx,const struct ps2_raster_target *d,
    int xoff,int yoff,const pixman_triangle_t *in,unsigned count)
{
    if(count>MAX_TRAPS/2 || (count && !in)){errno=EINVAL;return -1;}
    if(!count)return ps2_pipe_raster_trapezoids(ctx,d,xoff,yoff,NULL,0);
    pixman_trapezoid_t *traps=calloc(count*2,sizeof(*traps));if(!traps)return -1;
    for(unsigned i=0;i<count;++i){pixman_point_fixed_t a=in[i].p1,b=in[i].p2,c=in[i].p3,t;
        if(greater(a,b)){t=a;a=b;b=t;}if(greater(a,c)){t=a;a=c;c=t;}
        int64_t bx=(int64_t)b.x-a.x,by=(int64_t)b.y-a.y,cx=(int64_t)c.x-a.x,cy=(int64_t)c.y-a.y;
        if(bx<INT32_MIN+1LL || bx>INT32_MAX || cx<INT32_MIN+1LL || cx>INT32_MAX ||
           by>INT32_MAX || cy>INT32_MAX){free(traps);errno=EOVERFLOW;return -1;}
        if(by*cx-cy*bx<0){t=b;b=c;c=t;}
        pixman_trapezoid_t *p=traps+2*i;
        p[0]=(pixman_trapezoid_t){.top=a.y,.bottom=b.y<c.y?b.y:c.y,.left={a,b},.right={a,c}};
        p[1]=p[0];
        if(c.y<b.y){p[1].top=c.y;p[1].bottom=b.y;p[1].right=(pixman_line_fixed_t){c,b};}
        else{p[1].top=b.y;p[1].bottom=c.y;p[1].left=(pixman_line_fixed_t){b,c};}}
    int status=ps2_pipe_raster_trapezoids(ctx,d,xoff,yoff,traps,count*2),error=errno;free(traps);errno=error;return status;
}
