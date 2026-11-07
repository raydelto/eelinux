/* SPDX-License-Identifier: MIT */
#include "ps2_composite.h"
#include "ps2_pipe.h"
#include "ps2_gs_logic.h"
#include "ps2_gs_lut.h"
#include "ps2_rop.h"
#include "ps2_filter.h"
#include "ps2_multiply.h"
#include "ps2_wide.h"
#include "ps2_wide_filter.h"
#include "ps2_format.h"
#include "ps2_gs_transfer.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <pixman.h>

struct texel { uint16_t x, y; };
static int filter_valid(const struct ps2_picture_sample *s)
{
    if(s->bilinear>1 || s->convolution>1 || (s->bilinear && s->convolution))return 0;
    if(!s->convolution)return !s->filter_nparams;
    if(!s->filter_params || s->filter_nparams<3 || s->filter_nparams>262144 ||
        (uintptr_t)s->filter_params>UINTPTR_MAX-(size_t)s->filter_nparams*4+1)return 0;
    int32_t width=s->filter_params[0],height=s->filter_params[1];
    return width>0 && height>0 && width<=2048*65536 && height<=2048*65536 &&
        !(width&65535) && !(height&65535) &&
        (uint64_t)(width/65536)*(height/65536)+2==s->filter_nparams;
}
static int filtered_sample(const struct ps2_picture_sample *s)
{return s->bilinear || s->convolution;}
static int sum(int64_t a, int64_t b, int64_t *out)
{
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) return -1;
    *out = a + b; return 0;
}
static int coordinate(int64_t value, unsigned size, unsigned repeat, unsigned *out)
{
    if (repeat == PS2_REPEAT_NONE && (value < 0 || value >= size)) return 0;
    if (repeat == PS2_REPEAT_PAD) value = value < 0 ? 0 : value >= size ? size - 1 : value;
    if (repeat == PS2_REPEAT_NORMAL || repeat == PS2_REPEAT_REFLECT) {
        int64_t period = repeat == PS2_REPEAT_REFLECT ? size * 2 : size;
        value %= period; if (value < 0) value += period;
        if (value >= size) value = period - 1 - value;
    }
    *out = value; return 1;
}
static int point(const struct ps2_picture_sample *s, int64_t x, int64_t y,
    unsigned width, unsigned height, struct texel *out)
{
    out->x = out->y = UINT16_MAX;
    if (s->transformed) {
        if (x < -32768 || x > 32767 || y < -32768 || y > 32767) return -1;
        int64_t fx = x * 65536 + 32768, fy = y * 65536 + 32768, v[3];
        for (unsigned i = 0; i < 3; ++i)
            if (sum((int64_t)s->matrix[i][0] * fx, (int64_t)s->matrix[i][1] * fy, v + i) ||
                sum(v[i], (int64_t)s->matrix[i][2] * 65536, v + i)) return -1;
        if(s->matrix[2][0] || s->matrix[2][1] || s->matrix[2][2]!=65536){
            /* Match the pinned pixman general fetcher's 16.16 3D point
             * rounding, unsigned projective division and wrapped result.
             * This is coordinate arithmetic only; colour sampling is GS. */
            int32_t fixed[3];
            for(unsigned i=0;i<3;++i){
                if(sum(v[i],32768,v+i))return -1;
                int64_t q=v[i]/65536;if(v[i]<0 && v[i]%65536)--q;
                if(q<INT32_MIN || q>INT32_MAX)return -1;
                fixed[i]=q;
            }
            if(!fixed[2])return 0;
            for(unsigned i=0;i<2;++i){
                uint32_t wrapped=fixed[2]?(uint32_t)(((uint64_t)(int64_t)fixed[i]<<16)/(uint64_t)(int64_t)fixed[2]):0;
                int32_t signed_fixed;memcpy(&signed_fixed,&wrapped,4);
                int64_t value=(int64_t)signed_fixed-1;
                v[i]=value/65536;if(value<0 && value%65536)--v[i];
            }
            x=v[0];y=v[1];
        }else{
        /* Pinned pixman/Xorg nearest uses rounded 16.16 coordinates minus
         * one fixed-point epsilon: exact texel boundaries choose the texel
         * on their left. The affine denominator is exactly 2^32. */
        if (sum(v[0], -32768, v) || sum(v[1], -32768, v + 1)) return -1;
        x = v[0] / v[2]; y = v[1] / v[2];
        if (v[0] % v[2] && ((v[0] < 0) != (v[2] < 0))) --x;
        if (v[1] % v[2] && ((v[1] < 0) != (v[2] < 0))) --y;
        }
    }
    unsigned u, v;
    if (coordinate(x, width, s->repeat, &u) && coordinate(y, height, s->repeat, &v)) {
        out->x = u; out->y = v;
    }
    return 0;
}
static uint32_t float_bits(float f) { uint32_t v; memcpy(&v, &f, 4); return v; }
static int mask_fixed(const struct ps2_picture_sample *s,int64_t x,int64_t y,int32_t fixed[3])
{
    if(x < -32768 || x > 32767 || y < -32768 || y > 32767)return -1;
    int64_t fx=x*65536+32768,fy=y*65536+32768;
    if(!s->transformed){fixed[0]=fx;fixed[1]=fy;fixed[2]=65536;return 0;}
    int32_t vector[3];
    for(unsigned i=0;i<3;++i){int64_t value;
        if(sum((int64_t)s->matrix[i][0]*fx,(int64_t)s->matrix[i][1]*fy,&value) ||
            sum(value,(int64_t)s->matrix[i][2]*65536,&value) || sum(value,32768,&value))return -1;
        int64_t q=value/65536;if(value<0 && value%65536)--q;
        if(q<INT32_MIN || q>INT32_MAX)return -1;
        vector[i]=q;
    }
    fixed[2]=vector[2];
    for(unsigned i=0;i<2;++i){uint32_t wrapped=vector[2]?
        (uint32_t)(((uint64_t)(int64_t)vector[i]<<16)/(uint64_t)(int64_t)vector[2]):0;
        memcpy(fixed+i,&wrapped,4);}
    return 0;
}
static void bilinear_taps(const int32_t fixed[3],const struct ps2_picture_sample *s,
    unsigned width,unsigned height,struct ps2_filter_tap taps[4])
{
    int64_t px=(int64_t)fixed[0]-32768,py=(int64_t)fixed[1]-32768;
    unsigned dx=((uint32_t)px>>9)&127,dy=((uint32_t)py>>9)&127;
    int64_t ix=px/65536,iy=py/65536;
    if(px<0 && px%65536)--ix;
    if(py<0 && py%65536)--iy;
    for(unsigned i=0;i<4;++i){unsigned u,v;
        taps[i]=(struct ps2_filter_tap){.x=UINT16_MAX,.y=UINT16_MAX,
            .weight=(i&1?dx:128-dx)*(i&2?dy:128-dy)};
        if(fixed[2] && coordinate(ix+(i&1),width,s->repeat,&u) && coordinate(iy+(i>>1),height,s->repeat,&v)){
            taps[i].x=u;taps[i].y=v;
        }
    }
}
static void convolution_origin(const int32_t fixed[3],const struct ps2_picture_sample *s,int64_t *x,int64_t *y)
{
    for(unsigned i=0;i<2;++i){
        uint32_t wrapped=(uint32_t)fixed[i]-1-((uint32_t)s->filter_params[i]-65536)/2;
        int32_t signed_fixed;memcpy(&signed_fixed,&wrapped,4);
        int64_t value=signed_fixed/65536;
        if(signed_fixed<0 && signed_fixed%65536)--value;
        *(i?y:x)=value;
    }
}
static unsigned padded(unsigned n) { unsigned p = 1; while (p < n) p *= 2; return p; }
static int swap_rgb(struct ps2_surface_client *gs,uint32_t source,uint32_t destination,
    uint32_t palette,unsigned width)
{
    uint32_t table[256];
    if(ps2_rop_sample(gs,source,destination,0,0,0,0,width,1,0xffffff,0,128))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i<<16;
    if(ps2_surface_map_palette(gs,palette,table) ||
        ps2_surface_channel_map(gs,source,destination,palette,width,1,0xff0000,0))return -1;
    for(unsigned i=0;i<256;++i)table[i]=i;
    return ps2_surface_map_palette(gs,palette,table) ||
        ps2_surface_channel_map(gs,source,destination,palette,width,1,0xff,2)?-1:0;
}
static int draw_run(struct ps2_surface_client *gs, uint32_t source, uint32_t destination,
    const struct ps2_surface *s, unsigned x, unsigned y, unsigned width,
    const struct texel *t, int step, unsigned over)
{
    if (t->x == UINT16_MAX) return over ? 0 :
        ps2_surface_solid_logic(gs, destination, x, y, width, 1, 0,
            ps2_surface_lookup(gs,destination)->bits_per_pixel == 16 ? 0x7fff : 0xffffff, 0);
    /* Keep reversed coordinates positive, including the final texel zero.
     * Edge- and centre-sampling renderers must both select the same texel. */
    if (step < 0 && width > t->x) {
        if (t->x && draw_run(gs,source,destination,s,x,y,t->x,t,step,over))return -1;
        struct texel last=*t;last.x=0;
        return draw_run(gs,source,destination,s,x+t->x,y,1,&last,0,over);
    }
    float left = step < 0 ? t->x + .75f : t->x;
    float right = step < 0 ? left - width : step > 0 ? left + width : left + 1;
    unsigned xx[4] = {x, x + width, x + width, x}, yy[4] = {y, y, y + 1, y + 1};
    float u[4] = {left, right, right, left}, v[4] = {t->y, t->y, t->y + 1, t->y + 1};
    static const unsigned indices[2][3] = {{0,1,2},{0,2,3}};
    struct ps2gs_texture_triangle triangles[2] = {0};
    for (unsigned i = 0; i < 2; ++i) for (unsigned j = 0; j < 3; ++j) {
        unsigned k = indices[i][j];
        triangles[i].v[j] = (struct ps2gs_texture_vertex){
            .position = {.x = xx[k] * 16, .y = yy[k] * 16, .rgba = 0x80808080},
            .s = float_bits(u[k] / padded(s->width)), .t = float_bits(v[k] / padded(s->height)),
            .q = 0x3f800000};
    }
    struct ps2_render_client temporary = {.gs = gs, .target = destination};
    return ps2_texture_triangles(&temporary, source, triangles, 2);
}
/* The source descriptor selects GS channel extraction or packed coverage.
 * This routine computes coordinates only; every sampled byte stays on GS. */
static int sample_row(struct ps2_filter_workspace *f,uint32_t source,uint32_t target,
    uint32_t temporary,uint32_t palette,const struct ps2_picture_sample *sample,
    int64_t x,int64_t y,unsigned width)
{
    const struct ps2_surface *s=ps2_surface_lookup(f->gs,source);
    unsigned sw=f->source_kind?f->source_width:s->width;
    unsigned sh=f->source_kind?f->source_height:s->height;
    for(unsigned i=0;i<width;){
        if(!filtered_sample(sample)){
            struct texel t,next;
            if(point(sample,x+i,y,sw,sh,&t))return -1;
            int step=0;unsigned count=1;
            if(i+1<width && t.x!=UINT16_MAX){
                if(point(sample,x+i+1,y,sw,sh,&next))return -1;
                step=(int)next.x-t.x;
                if(next.y!=t.y || step < -1 || step > 1)step=2;
            }
            while(i+count<width){
                if(point(sample,x+i+count,y,sw,sh,&next))return -1;
                if(t.x==UINT16_MAX?next.x!=UINT16_MAX:
                    step==2 || next.y!=t.y || (int)next.x!=(int)t.x+step*(int)count)break;
                ++count;
            }
            if(t.x==UINT16_MAX){
                if(ps2_surface_solid_logic(f->gs,target,i,0,count,1,0,0xffffff,0))return -1;
            }else if(ps2_filter_fetch(f,source,target,palette,t.x,t.y,i,count,count==1?0:step))return -1;
            i+=count;continue;
        }
        int32_t fixed[3];if(mask_fixed(sample,x+i,y,fixed))return -1;
        if(sample->convolution){
            unsigned kw=sample->filter_params[0]/65536,kh=sample->filter_params[1]/65536;
            int64_t ix,iy;convolution_origin(fixed,sample,&ix,&iy);
            if(ps2_filter_convolution_begin(f))return -1;
            for(unsigned v=0;v<kh;++v)for(unsigned u=0;u<kw;++u){unsigned tx,ty;
                int32_t coefficient=sample->filter_params[2+v*kw+u];
                if(coefficient && fixed[2] && coordinate(ix+u,sw,sample->repeat,&tx) &&
                    coordinate(iy+v,sh,sample->repeat,&ty) &&
                    ps2_filter_convolution_tap(f,source,palette,tx,ty,coefficient))return -1;
            }
            if(ps2_filter_convolution_end(f,temporary,palette) ||
                ps2_rop_sample(f->gs,temporary,target,0,0,i,0,1,1,0xffffff,0,128))return -1;
            ++i;continue;
        }
        struct ps2_filter_tap taps[4],next[4];bilinear_taps(fixed,sample,sw,sh,taps);
        unsigned count=1;int step=0;
        if(i+1<width){
            if(mask_fixed(sample,x+i+1,y,fixed))return -1;
            bilinear_taps(fixed,sample,sw,sh,next);
            for(unsigned j=0;j<4;++j)if(taps[j].weight && taps[j].x!=UINT16_MAX && next[j].x!=UINT16_MAX){
                step=(int)next[j].x-taps[j].x;break;}
        }
        while(i+count<width && step>=-1 && step<=1){
            if(mask_fixed(sample,x+i+count,y,fixed))return -1;
            bilinear_taps(fixed,sample,sw,sh,next);unsigned j;
            for(j=0;j<4;++j)if(next[j].weight!=taps[j].weight || (taps[j].weight &&
                (taps[j].x==UINT16_MAX?next[j].x!=UINT16_MAX:
                 next[j].y!=taps[j].y || (int)next[j].x!=(int)taps[j].x+step*(int)count)))break;
            if(j!=4)break;
            ++count;
        }
        if(ps2_filter_bilinear(f,source,temporary,palette,taps,count,count==1?0:step) ||
            ps2_rop_sample(f->gs,temporary,target,0,0,i,0,count,1,0xffffff,0,128))return -1;
        i+=count;
    }
    return 0;
}
enum factor_kind { FACTOR_ZERO, FACTOR_ONE, FACTOR_ALPHA, FACTOR_INVERSE };
static int factor_plane(struct ps2_surface_client *gs,uint32_t source,uint32_t destination,
    uint32_t palette,unsigned width,unsigned kind)
{
    if(kind<=FACTOR_ONE)return ps2_surface_solid_logic(gs,destination,0,0,width,1,
        kind==FACTOR_ONE?0xffffff:0,0xffffff,3);
    unsigned char table[256];for(unsigned i=0;i<256;++i)table[i]=kind==FACTOR_ALPHA?i:255-i;
    return ps2_surface_lut_palette(gs,palette,table) ||
        ps2_surface_channel_lut(gs,source,destination,palette,width,1,0xffffff)?-1:0;
}
static int apply_factor(struct ps2_filter_workspace *f,uint32_t value,uint32_t factor,
    uint32_t palette,unsigned width,unsigned kind)
{
    if(kind==FACTOR_ONE)return 0;
    if(kind==FACTOR_ZERO)return ps2_surface_solid_logic(f->gs,value,0,0,width,1,0,0xffffff,0);
    return ps2_native_multiply(f,value,factor,value,palette,width,1);
}
static int expand_rgb555(struct ps2_surface_client *gs,uint32_t source,uint32_t temporary,
    uint32_t palette,unsigned width)
{
    unsigned char table[256];for(unsigned i=0;i<256;++i)table[i]=i|(i>>5);
    return ps2_surface_lut_palette(gs,palette,table) ||
        ps2_surface_channel_lut(gs,source,temporary,palette,width,1,0xffffff) ||
        ps2_rop_sample(gs,temporary,source,0,0,0,0,width,1,0xffffff,0,128)?-1:0;
}
static int write_colour(struct ps2_rop_workspace *w,uint32_t palette,uint32_t destination,
    unsigned x,unsigned y,unsigned width,const struct ps2_picture_sample *sample,unsigned bpp)
{
    struct ps2_surface_client *gs=w->gs;uint32_t colour=w->b;
    if(sample->destination_swap){if(swap_rgb(gs,w->b,w->a,palette,width))return -1;colour=w->a;}
    if(sample->destination_rotate){
        uint32_t table[256];
        for(unsigned c=0;c<2;++c){
            for(unsigned i=0;i<256;++i)table[i]=i<<((c+1)*8);
            if(ps2_surface_map_palette(gs,palette,table) ||
                ps2_surface_channel_map(gs,w->b,w->a,palette,width,1,255U<<((c+1)*8),c))return -1;
        }
        for(unsigned i=0;i<256;++i)table[i]=i | i<<24;
        return ps2_surface_map_palette(gs,palette,table) ||
            ps2_surface_channel_map(gs,w->b,w->c,palette,width,1,UINT32_MAX,2) ||
            ps2_surface_rgba_rectangle(gs,w->c,w->a,0,0,0,0,width,1,0xff000000,PS2GS_RGBA_REPLACE) ||
            ps2_surface_rgba_rectangle(gs,w->a,destination,0,0,x,y,width,1,0xffffff00,PS2GS_RGBA_REPLACE)?-1:0;
    }
    return ps2_rop_sample(gs,colour,destination,0,0,x,y,width,1,bpp==16?0x7fff:0xffffff,0,128);
}
static int alpha_map_valid(struct ps2_surface_client *gs,const struct ps2_picture_alpha_map *m)
{
    if(m->enabled>1)return 0;
    if(!m->enabled)return 1;
    const struct ps2_surface *s=ps2_surface_lookup(gs,m->surface);
    return s && m->width && m->height && m->width<=1024 && m->height<=1024 &&
        s->width==m->width && s->height==m->height &&
        m->has_alpha<=1 && m->alpha_only<=1 && m->swap<=1 && m->rotate<=1 &&
        m->alpha_bits<=8 && (!m->alpha_only || !m->alpha_bits || m->alpha_bits==1 || m->alpha_bits==4 || m->alpha_bits==8) &&
        m->rgb_bits[0]<=8 && m->rgb_bits[1]<=8 && m->rgb_bits[2]<=8 &&
        !(m->swap && m->rotate) && (!m->alpha_only || m->has_alpha) &&
        (!m->has_alpha || (m->surface && s->bits_per_pixel==32 && s->pixel_depth==32)) &&
        (!m->rotate || (m->surface && s->bits_per_pixel==32 && s->pixel_depth==32));
}
static int alpha_map_write(struct ps2_rop_workspace *w,uint32_t alpha,uint32_t colour,uint32_t palette,
    const struct ps2_picture_alpha_map *map,unsigned x,unsigned y,unsigned width)
{
    unsigned mx=(int64_t)x-map->x,my=(int64_t)y-map->y;
    struct ps2_picture_sample format={.destination_swap=map->swap,.destination_rotate=map->rotate};
    const struct ps2_surface *s=ps2_surface_lookup(w->gs,map->surface);
    if(!map->alpha_only){uint32_t saved=w->b;w->b=colour;
        int status=write_colour(w,palette,map->surface,mx,my,width,&format,s->bits_per_pixel);
        w->b=saved;if(status)return -1;}
    if(!map->has_alpha)return 0;
    if(!map->rotate)return ps2_surface_alpha_write(w->gs,alpha,w->c,map->surface,palette,mx,my,width,1,0xff000000);
    uint32_t table[256];for(unsigned i=0;i<256;++i)table[i]=i;
    return ps2_surface_map_palette(w->gs,palette,table) ||
        ps2_surface_channel_map_region(w->gs,alpha,map->surface,palette,0,0,mx,my,width,1,255,0)?-1:0;
}
static int unorm_valid(const struct ps2_unorm_layout *layout)
{
    return layout->alpha_bits<=8 && layout->rgb_bits[0]<=8 &&
        layout->rgb_bits[1]<=8 && layout->rgb_bits[2]<=8;
}
static struct ps2_unorm_layout unorm_layout(struct ps2_unorm_layout layout,unsigned bpp)
{
    for(unsigned c=0;c<3;++c)if(!layout.rgb_bits[c])layout.rgb_bits[c]=bpp==16?5:8;
    if(!layout.alpha_bits)layout.alpha_bits=8;
    return layout;
}
/* A decoded packed canvas must observe each store at its original precision,
 * including a following scanline that reads the writable alpha-map alias.
 * This table is uniform format metadata; all pixel lookups execute on GS. */
static int quantize_store(struct ps2_surface_client *gs,uint32_t source,uint32_t temporary,
    uint32_t palette,const unsigned bits[3],unsigned width)
{
    unsigned char table[256];unsigned changed=0;
    for(unsigned c=0;c<3;++c)if(bits[c]<8){
        for(unsigned i=0;i<256;++i){
            unsigned code=i>>(8-bits[c]),value=code,filled=bits[c];
            while(filled<8){value=(value<<bits[c])|code;filled+=bits[c];}
            table[i]=value>>(filled-8);
        }
        if(ps2_surface_lut_palette(gs,palette,table) ||
            ps2_surface_channel_lut(gs,source,temporary,palette,width,1,255U<<(c*8)))return -1;
        changed|=255U<<(c*8);
    }
    return changed?ps2_rop_sample(gs,temporary,source,0,0,0,0,width,1,changed,0,128):0;
}
static int opaque_sample(const struct ps2_picture_sample *s,int64_t x,int64_t y,
    int64_t width,int64_t height,unsigned sw,unsigned sh)
{
    if(s->gradient.kind){
        if(!s->repeat || s->convolution || s->source_alpha_map.enabled)return 0;
        return ps2_gradient_opaque(&s->gradient);
    }
    if(!sw || !sh || width<=0 || height<=0 || s->source_argb ||
        (s->source_solid && s->solid_rgba[3]!=65535) ||
        (s->has_alpha && s->alpha!=255) || s->source_alpha_map.enabled || s->convolution)return 0;
    if(s->repeat!=PS2_REPEAT_NONE)return 1;
    if(x < -32768 || y < -32768 || x+width>32768 || y+height>32768)return 0;
    int64_t lo[2]={INT64_MAX,INT64_MAX},hi[2]={INT64_MIN,INT64_MIN};
    for(unsigned corner=0;corner<4;++corner){
        pixman_vector_t v={.vector={(x+(corner&1?width-1:0))*65536+32768,
            (y+(corner&2?height-1:0))*65536+32768,65536}};
        /* Public fixed-point geometry only: this is the same corner
         * transformation used by pixman's opacity extent analysis. */
        if(s->transformed && !pixman_transform_point((const pixman_transform_t*)s->matrix,&v))return 0;
        for(unsigned c=0;c<2;++c){if(v.vector[c]<lo[c])lo[c]=v.vector[c];if(v.vector[c]>hi[c])hi[c]=v.vector[c];}
    }
    int64_t low_offset=s->bilinear?32768:1,high_offset=s->bilinear?32768:-1;
    return lo[0]-low_offset>=0 && lo[1]-low_offset>=0 &&
        hi[0]+high_offset<(int64_t)sw*65536 && hi[1]+high_offset<(int64_t)sh*65536;
}
static int float_constant(struct ps2_surface_client *gs,const uint32_t out[4],uint32_t bits)
{
    for(unsigned byte=0;byte<4;++byte)
        if(ps2_surface_solid_logic(gs,out[byte],0,0,1,1,((bits>>(byte*8))&255)*0x010101,0xffffff,3))return -1;
    return 0;
}
static int solid_valid(const struct ps2_picture_sample *s)
{
    return !s->source_solid || (s->source_solid==1 && s->has_alpha==1 &&
        s->alpha==(unsigned)(s->solid_rgba[3]>>8) && s->repeat==PS2_REPEAT_NORMAL &&
        !s->transformed && !s->bilinear && !s->convolution && !s->source_argb &&
        !s->source_swap && !s->source_rotate && !s->source_alpha_map.enabled);
}
static int solid_float(struct ps2_surface_client *gs,const uint32_t out[4],
    const struct ps2_picture_sample *s,unsigned alpha)
{
    uint32_t raw[3];
    for(unsigned c=0;c<3;++c){float value=s->solid_rgba[alpha?3:c]*(1.0f/65535);
        memcpy(raw+c,&value,sizeof(*raw));}
    for(unsigned byte=0;byte<4;++byte){uint32_t colour=0;
        for(unsigned c=0;c<3;++c)colour|=((raw[c]>>(byte*8))&255)<<(c*8);
        if(ps2_surface_solid_logic(gs,out[byte],0,0,1,1,colour,0xffffff,3))return -1;
    }
    return 0;
}
static int sample_float(struct ps2_divide_workspace *wide,struct ps2_filter_workspace *f,
    uint32_t source,const uint32_t out[4],uint32_t codes,uint32_t temporary,uint32_t palette,
    const unsigned bits[3],const struct ps2_picture_sample *sample,int64_t x,int64_t y)
{
    if(sample->source_solid)return solid_float(f->gs,out,sample,f->source_kind!=PS2_FILTER_RGB);
    const struct ps2_surface *s=ps2_surface_lookup(f->gs,source);
    unsigned sw=f->source_kind?f->source_width:s->width,sh=f->source_kind?f->source_height:s->height;
    if(sample->convolution){
        int32_t fixed[3];if(mask_fixed(sample,x,y,fixed))return -1;
        int64_t ix,iy;convolution_origin(fixed,sample,&ix,&iy);
        unsigned kw=sample->filter_params[0]/65536,kh=sample->filter_params[1]/65536;
        for(unsigned byte=0;byte<4;++byte)
            if(ps2_surface_solid_logic(f->gs,out[byte],0,0,1,1,0,0xffffff,0))return -1;
        for(unsigned v=0;v<kh;++v)for(unsigned u=0;u<kw;++u){unsigned tx,ty;
            int32_t coefficient=sample->filter_params[2+v*kw+u];
            if(coefficient && fixed[2] && coordinate(ix+u,sw,sample->repeat,&tx) &&
                coordinate(iy+v,sh,sample->repeat,&ty) &&
                (ps2_filter_fetch(f,source,codes,palette,tx,ty,0,1,0) ||
                 ps2_native_convolution_float_tap(wide,f,codes,bits,coefficient,out,palette,1,1)))return -1;
        }
        if(ps2_native_convolution_float_end(wide,f,out,out,palette,1,1))return -1;
    }else if(sample->bilinear){
        int32_t fixed[3];if(mask_fixed(sample,x,y,fixed))return -1;
        int64_t px=(int64_t)fixed[0]-32768,py=(int64_t)fixed[1]-32768;
        int64_t ix=px/65536,iy=py/65536;
        if(px<0 && px%65536)--ix;
        if(py<0 && py%65536)--iy;
        struct ps2_filter_tap taps[4];
        for(unsigned i=0;i<4;++i){unsigned u,v;
            taps[i]=(struct ps2_filter_tap){.x=UINT16_MAX,.y=UINT16_MAX};
            if(fixed[2] && coordinate(ix+(i&1),sw,sample->repeat,&u) && coordinate(iy+(i>>1),sh,sample->repeat,&v)){
                taps[i].x=u;taps[i].y=v;
            }
        }
        if(ps2_native_bilinear_float(wide,f,source,out,bits,palette,taps,(uint32_t)px&65535,(uint32_t)py&65535,1,0))return -1;
    }else if(sample_row(f,source,codes,temporary,palette,sample,x,y,1) ||
        ps2_native_unorm_float(f->gs,codes,out,bits,palette,1,1))return -1;
    if(f->source_kind==PS2_FILTER_RGB && sample->source_swap)for(unsigned byte=0;byte<4;++byte)
        if(swap_rgb(f->gs,out[byte],temporary,palette,1) ||
            ps2_rop_sample(f->gs,temporary,out[byte],0,0,0,0,1,1,0xffffff,PS2GS_TRANSFER_REPLACE,128))return -1;
    return 0;
}
static int packed10_map(const struct ps2_picture_alpha_map *map)
{
    return map->enabled && (map->rgb_bits[0]>8 || map->rgb_bits[1]>8 || map->rgb_bits[2]>8);
}
static int packed10_composite(struct pipe_context *,uint32_t,uint32_t,const struct ps2_pipe_copy_box *,
    const struct ps2_picture_sample *,const struct ps2_picture_bitmap *,unsigned);
int ps2_pipe_composite_rect(struct pipe_context *ctx,uint32_t source,uint32_t destination,
    const struct ps2_pipe_copy_box *input,const struct ps2_picture_sample *input_sample,
    const struct ps2_picture_bitmap *input_mask,unsigned operation)
{
    if(input_sample && (input_sample->source_packed10 || input_sample->destination_packed10 ||
        (input_mask && input_mask->sample.source_packed10) ||
        packed10_map(&input_sample->source_alpha_map) || packed10_map(&input_sample->destination_alpha_map) ||
        (input_mask && packed10_map(&input_mask->sample.source_alpha_map))))
        return packed10_composite(ctx,source,destination,input,input_sample,input_mask,operation);
    /* The pinned operator optimizer canonicalizes these six aliases before
     * choosing its byte/float pipeline, even for filtered or masked sources. */
    if(operation>=PS2_COMPOSITE_HSL_HUE && operation<=PS2_COMPOSITE_HSL_LUMINOSITY &&
        input_mask && input_mask->component_alpha)operation=PS2_COMPOSITE_DST;
    unsigned pd_operation=operation>=PS2_COMPOSITE_DISJOINT_OVER && operation<=PS2_COMPOSITE_CONJOINT_XOR?operation:0;
    unsigned hsl_operation=operation>=PS2_COMPOSITE_HSL_HUE && operation<=PS2_COMPOSITE_HSL_LUMINOSITY?operation:0;
    unsigned blend_operation=operation>=PS2_COMPOSITE_COLOR_DODGE && operation<=PS2_COMPOSITE_HSL_LUMINOSITY?operation:0;
    unsigned disjoint_saturate=operation==PS2_COMPOSITE_DISJOINT_OVER_REVERSE || pd_operation || blend_operation;
    if(operation>=PS2_COMPOSITE_DISJOINT_CLEAR && operation<=PS2_COMPOSITE_CONJOINT_DST){
        static const unsigned basic[]={PS2_COMPOSITE_CLEAR,PS2_COMPOSITE_SRC,PS2_COMPOSITE_DST,
            PS2_COMPOSITE_CLEAR,PS2_COMPOSITE_SRC,PS2_COMPOSITE_DST};
        operation=basic[operation-PS2_COMPOSITE_DISJOINT_CLEAR];
    }
    if(disjoint_saturate)operation=PS2_COMPOSITE_SATURATE;
    if(!ctx || !input || !input_sample || operation>PS2_COMPOSITE_HARDLIGHT || input_sample->transformed>1 ||
        input_sample->repeat>PS2_REPEAT_REFLECT || input_sample->has_alpha>1 ||
        (input_sample->has_alpha && input_sample->alpha>255) || input_sample->source_swap>1 ||
        input_sample->destination_swap>1 || input_sample->source_argb>1 || input_sample->destination_argb>1 ||
        input_sample->source_rotate>1 || input_sample->destination_rotate>1 ||
        (input_sample->source_rotate && input_sample->source_swap) ||
        (input_sample->destination_rotate && input_sample->destination_swap) ||
        (input_sample->has_alpha && input_sample->source_argb) || input_sample->alpha_only_destination>1 ||
        (operation==PS2_COMPOSITE_SATURATE && !disjoint_saturate && !input_sample->destination_argb && !input_sample->destination_alpha_map.enabled) ||
        !filter_valid(input_sample) || !solid_valid(input_sample) ||
        (input_mask && !solid_valid(&input_mask->sample)) ||
        ((input_sample->source_solid || (input_mask && input_mask->sample.source_solid)) && operation!=PS2_COMPOSITE_SATURATE) ||
        !unorm_valid(&input_sample->source_unorm) ||
        !unorm_valid(&input_sample->destination_unorm) ||
        (input_mask && !unorm_valid(&input_mask->sample.source_unorm))){errno=EINVAL;return -1;}
    unsigned gradients=!!input_sample->gradient.kind+!!(input_mask && input_mask->sample.gradient.kind);
    if((input_sample->gradient.kind && (!ps2_gradient_valid(&input_sample->gradient) || input_sample->source_solid || input_sample->source_alpha_map.enabled)) ||
        (input_mask && input_mask->sample.gradient.kind && (!ps2_gradient_valid(&input_mask->sample.gradient) ||
            !input_mask->has_surface || input_mask->sample.source_solid || input_mask->sample.source_alpha_map.enabled))){errno=EINVAL;return -1;}
    if(ps2_pipe_context_acquire(ctx))return -1;
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);struct ps2_surface_client *gs=r->gs;
    const struct ps2_surface *s=ps2_surface_lookup(gs,source),*d=ps2_surface_lookup(gs,destination);
    struct ps2_pipe_copy_box box=*input;
    struct ps2_picture_sample sample=*input_sample;
    struct ps2_picture_bitmap mask=input_mask?*input_mask:(struct ps2_picture_bitmap){0};
    /* The pinned Render oracle reduces Saturate to OverReverse when a
     * repeated source and unified mask are known opaque. That reduction
     * uses its narrow byte arithmetic, including rounding. Preserve the
     * same hardware path instead of introducing a wide-rounding difference.
     * Component-alpha masks retain the independent wide channel factors. */
    if(operation==PS2_COMPOSITE_SATURATE && !disjoint_saturate &&
        opaque_sample(&sample,box.sx,box.sy,box.width,box.height,s?s->width:0,s?s->height:0) &&
        (!input_mask || (mask.has_surface && !mask.component_alpha &&
            opaque_sample(&mask.sample,mask.x,mask.y,box.width,box.height,mask.width,mask.height))))
        operation=PS2_COMPOSITE_OVER_REVERSE;
    struct ps2_rop_workspace w={0};struct ps2_filter_workspace f={0};struct ps2_divide_workspace wide={0};
    unsigned alpha_result_only=sample.alpha_only_destination &&
        (!sample.destination_alpha_map.enabled || sample.destination_alpha_map.alpha_only);
    /* Preserve the established scalar-nearest coverage path. Filtered and
     * component-alpha coverage use the same original-precision GS arithmetic
     * as colour targets; only the alpha result is subsequently published. */
    unsigned wide_saturate=disjoint_saturate || (operation==PS2_COMPOSITE_SATURATE &&
        (!sample.alpha_only_destination || sample.source_solid || gradients || sample.bilinear || sample.convolution ||
            sample.source_alpha_map.enabled || sample.destination_alpha_map.enabled ||
            (input_mask && (mask.sample.source_solid || mask.sample.bilinear || mask.sample.convolution || mask.component_alpha || mask.sample.source_alpha_map.enabled))));
    /* DisjointOverReverse has Saturate's factors but the reference never
     * applies Saturate's opaque-source byte reduction to that opcode. */
    unsigned float_saturate=wide_saturate && (disjoint_saturate || gradients || sample.alpha_only_destination || sample.source_solid || sample.bilinear || sample.convolution ||
        sample.source_alpha_map.enabled || sample.destination_alpha_map.enabled ||
        (input_mask && (mask.sample.source_solid || mask.sample.bilinear || mask.sample.convolution || mask.sample.source_alpha_map.enabled)));
    uint32_t floating[16]={0};int64_t *source_positions=NULL,*mask_positions=NULL;
    struct texel *plan=NULL;uint32_t snapshot=0,palette=0,mask_surface=0,mask_snapshot=0,sa=0,da=0,ma=0,sf=0,ca=0,mf=0;
    uint32_t alpha_snapshot[2]={0},row_alpha=0,row_map=0,order_work=0,piecewise_work[2]={0};
    unsigned shifted_map=0;
    int status=-1,error=EINVAL;
    if(!s || !d || s->width>1024 || s->height>1024 || box.dx<0 || box.dy<0 || box.width<0 || box.height<0 ||
        (sample.source_solid && (!source || s->width!=1 || s->height!=1 || s->bits_per_pixel!=32)) ||
        (int64_t)box.dx+box.width>d->width || (int64_t)box.dy+box.height>d->height ||
        (sample.source_argb && (!source || s->bits_per_pixel!=32 || s->pixel_depth!=32)) ||
        (sample.destination_argb && (!destination || d->bits_per_pixel!=32 || d->pixel_depth!=32)) ||
        (sample.source_rotate && (!source || s->bits_per_pixel!=32 || s->pixel_depth!=32)) ||
        (sample.destination_rotate && (!destination || d->bits_per_pixel!=32 || d->pixel_depth!=32)))goto out;
    if(!alpha_map_valid(gs,&sample.source_alpha_map) || !alpha_map_valid(gs,&sample.destination_alpha_map) ||
        (input_mask && !alpha_map_valid(gs,&mask.sample.source_alpha_map)))goto out;
    if(sample.destination_alias && (!sample.destination_alpha_map.enabled ||
        !ps2_format_alias_valid(sample.destination_alias,gs,destination,sample.destination_alpha_map.surface)))goto out;
    if(sample.destination_alpha_map.enabled){
        const struct ps2_picture_alpha_map *m=&sample.destination_alpha_map;
        int64_t x=(int64_t)box.dx-m->x,y=(int64_t)box.dy-m->y;
        if(x<0 || y<0 || x+box.width>m->width || y+box.height>m->height)goto out;
        shifted_map=sample.destination_alias || (m->surface==destination && (m->x || m->y));
        if(m->surface==destination && (shifted_map || operation==PS2_COMPOSITE_SATURATE)){
            struct ps2_unorm_layout layout=unorm_layout(sample.destination_unorm,d->bits_per_pixel);
            unsigned incompatible=m->alpha_only!=sample.alpha_only_destination ||
                (m->alpha_bits?m->alpha_bits:8)!=layout.alpha_bits;
            for(unsigned c=0;c<3 && !m->alpha_only;++c)
                incompatible|=(m->rgb_bits[c]?m->rgb_bits[c]:d->bits_per_pixel==16?5:8)!=layout.rgb_bits[c];
            if(incompatible){
            error=ENOTSUP;goto out;
            }
        }
    }
    unsigned destination_alpha=sample.destination_argb || sample.destination_alpha_map.enabled;
    size_t pixels=(size_t)box.width*box.height;
    if((uint64_t)box.width*box.height>PS2_PIPE_MAX_COPY_PIXELS){error=E2BIG;goto out;}
    size_t mask_row=mask.bits_per_pixel==8?mask.width:(mask.width+7)/8;
    if(input_mask && (!mask.width || !mask.height || mask.width>2048 || mask.height>2048 ||
        mask.sample.has_alpha>1 || (mask.sample.has_alpha && (mask.sample.alpha>255 || mask.sample.source_argb)) ||
        (mask.bits_per_pixel!=0 && mask.bits_per_pixel!=1 && mask.bits_per_pixel!=8) ||
        mask.msb_first>1 || mask.sample.transformed>1 || mask.sample.repeat>PS2_REPEAT_REFLECT ||
        !filter_valid(&mask.sample) || mask.has_surface>1 || mask.component_alpha>1 ||
        (mask.alpha_bits && mask.alpha_bits!=1 && mask.alpha_bits!=4 && mask.alpha_bits!=8) ||
        (mask.alpha_bits>1 && mask.bits_per_pixel!=8) || (mask.alpha_bits==1 && mask.bits_per_pixel==8)))goto out;
    if(input_mask && !mask.has_surface && (!mask.bits || mask.stride<mask_row || mask.bytes>1024*1024 ||
        mask.bytes<mask_row || mask.height-1>(mask.bytes-mask_row)/mask.stride ||
        (uintptr_t)mask.bits>UINTPTR_MAX-(mask.bytes-1)))goto out;
    if(input_mask && mask.has_surface){const struct ps2_surface *m=ps2_surface_lookup(gs,mask.surface);
        if(!m || m->width!=mask.width || m->height!=mask.height || m->width>1024 || m->height>1024 ||
            (mask.sample.source_solid && (!mask.surface || m->width!=1 || m->height!=1 || m->bits_per_pixel!=32)) ||
            mask.sample.source_argb>1 || mask.sample.source_swap>1 || mask.sample.source_rotate>1 ||
            (mask.sample.source_swap && mask.sample.source_rotate) ||
            ((mask.sample.source_argb || mask.sample.source_rotate) && (!mask.surface || m->bits_per_pixel!=32 || m->pixel_depth!=32)))goto out;
    }
    if(input_mask && mask.sample.source_solid && !mask.has_surface)goto out;
    if(!pixels || operation==PS2_COMPOSITE_DST){status=0;goto out;}
    if((sample.gradient.kind && !ps2_gradient_extent_valid(sample.matrix,sample.transformed,box.sx,box.sy,box.width,box.height)) ||
        (mask.sample.gradient.kind && !ps2_gradient_extent_valid(mask.sample.matrix,mask.sample.transformed,mask.x,mask.y,box.width,box.height))){
        status=0;goto out;
    }
    if(operation==PS2_COMPOSITE_SATURATE && !wide_saturate &&
        (filtered_sample(&sample) || (input_mask && (filtered_sample(&mask.sample) || mask.component_alpha)))){
        error=ENOTSUP;goto out;}
    unsigned simple=!gradients && !input_mask && !sample.source_alpha_map.enabled && !sample.destination_alpha_map.enabled &&
        !filtered_sample(&sample) && !sample.source_swap && !sample.destination_swap &&
        !sample.source_rotate && !sample.destination_rotate && !sample.source_argb && !sample.destination_argb &&
        (!sample.has_alpha || sample.alpha==255) && s->bits_per_pixel==d->bits_per_pixel &&
        (operation==PS2_COMPOSITE_SRC || operation==PS2_COMPOSITE_OVER || operation==PS2_COMPOSITE_CLEAR);
    unsigned required=PS2GS_HW_TEXTURE|PS2GS_HW_SOLID_LOGIC;
    if(!simple)required|=PS2GS_HW_CHANNEL_LUT|PS2GS_HW_CHANNEL_MAP|PS2GS_HW_CHANNEL_MAP_BLIT|
        PS2GS_HW_TEXTURE_TRANSFER|PS2GS_HW_RGBA_TRANSFER;
    if((gs->caps.features&required)!=required){error=ENOTSUP;goto out;}
    if((sample.convolution && !sample.gradient.kind && pixels*(uint64_t)(sample.filter_nparams-2)>16777216) ||
        (input_mask && mask.sample.convolution && !mask.sample.gradient.kind && pixels*(uint64_t)(mask.sample.filter_nparams-2)>16777216)){
        error=E2BIG;goto out;}
    size_t geometry=gradients?0:pixels*sizeof(*plan);
    size_t parameters=(sample.convolution && !sample.gradient.kind?sample.filter_nparams*4:0)+
        (input_mask && mask.sample.convolution && !mask.sample.gradient.kind?mask.sample.filter_nparams*4:0);
    size_t position_offset=(geometry+parameters+7)&~(size_t)7;
    if(gradients && (unsigned)box.width>(SIZE_MAX-position_offset)/(8*gradients)){error=E2BIG;goto out;}
    plan=malloc(position_offset+(size_t)box.width*8*gradients);if(!plan){error=ENOMEM;goto out;}
    int64_t *positions=(void*)((unsigned char*)plan+position_offset);
    if(sample.gradient.kind){source_positions=positions;positions+=box.width;}
    if(mask.sample.gradient.kind)mask_positions=positions;
    for(unsigned row=0;row<(unsigned)box.height && gradients;++row){
        if((source_positions && ps2_gradient_plan_row(&sample.gradient,sample.matrix,sample.transformed,sample.repeat,
                box.sx,box.sy,box.width,box.height,row,source_positions)) ||
            (mask_positions && ps2_gradient_plan_row(&mask.sample.gradient,mask.sample.matrix,mask.sample.transformed,mask.sample.repeat,
                mask.x,mask.y,box.width,box.height,row,mask_positions))){error=errno;goto out;}
    }
    unsigned char *params=(unsigned char*)plan+geometry;
    if(sample.convolution && !sample.gradient.kind){memcpy(params,sample.filter_params,sample.filter_nparams*4);sample.filter_params=(void*)params;params+=sample.filter_nparams*4;}
    if(input_mask && mask.sample.convolution && !mask.sample.gradient.kind){memcpy(params,mask.sample.filter_params,mask.sample.filter_nparams*4);mask.sample.filter_params=(void*)params;}
    /* Validate all coordinate arithmetic before allocating or submitting GS
     * work. Client bytes are subsequently uploaded verbatim, never filtered. */
    for(int y=0;y<box.height;++y)for(int x=0;x<box.width;++x){int32_t fixed[3];struct texel mt;
        if(!sample.gradient.kind && (filtered_sample(&sample)?mask_fixed(&sample,(int64_t)box.sx+x,(int64_t)box.sy+y,fixed):
            point(&sample,(int64_t)box.sx+x,(int64_t)box.sy+y,s->width,s->height,gradients?&mt:plan+y*box.width+x)))goto out;
        if(input_mask && !mask.sample.gradient.kind && (filtered_sample(&mask.sample)?
            mask_fixed(&mask.sample,(int64_t)mask.x+x,(int64_t)mask.y+y,fixed):
            point(&mask.sample,(int64_t)mask.x+x,(int64_t)mask.y+y,mask.width,mask.height,&mt)))goto out;
    }
    if(!simple){
        if(ps2_rop_reserve(&w,gs,d->bits_per_pixel) || ps2_surface_create(gs,16,16,32,&palette) ||
            ps2_filter_reserve(&f,gs,(sample.convolution && !sample.gradient.kind) || (input_mask && mask.sample.convolution && !mask.sample.gradient.kind)?4:3) ||
            ps2_surface_create(gs,64,32,32,&sa) || ps2_surface_create(gs,64,32,32,&da) ||
            ps2_surface_create(gs,64,32,32,&ma) || ps2_surface_create(gs,64,32,32,&sf)){
            error=errno;goto out;}
        if(input_mask && !mask.has_surface && ps2_surface_create(gs,(mask_row+3)/4,mask.height,32,&mask_surface)){error=errno;goto out;}
        if(mask.component_alpha && ((!wide_saturate && ps2_surface_create(gs,64,32,32,&ca)) ||
            ps2_surface_create(gs,64,32,32,&mf))){error=errno;goto out;}
        if(operation>=PS2_COMPOSITE_DARKEN && ps2_surface_create(gs,64,32,32,&order_work)){error=errno;goto out;}
        if(operation>=PS2_COMPOSITE_OVERLAY)for(unsigned i=0;i<2;++i)
            if(ps2_surface_create(gs,64,32,32,piecewise_work+i)){error=errno;goto out;}
        if((wide_saturate || gradients) && ps2_divide_reserve(&wide,gs)){error=errno;goto out;}
        if(float_saturate || gradients)for(unsigned i=0;i<16;++i)if(gradients || !alpha_result_only || i%8>=4)
            if(ps2_surface_create(gs,64,32,32,floating+i)){error=errno;goto out;}
        if(shifted_map && (ps2_surface_create(gs,box.width,1,32,&row_alpha) ||
            ps2_surface_create(gs,box.width,1,32,&row_map) || ps2_surface_set_depth(gs,row_map,32))){
            error=errno;goto out;}
    }
    if((!source || source==destination || (sample.destination_alpha_map.enabled && source==sample.destination_alpha_map.surface)) &&
        ps2_surface_create(gs,s->width,s->height,s->bits_per_pixel,&snapshot)){
        error=errno;goto out;}
    if(snapshot && s->pixel_depth==32 && ps2_surface_set_depth(gs,snapshot,32)){error=errno;goto out;}
    if(mask.has_surface && (!mask.surface || mask.surface==destination ||
        (sample.destination_alpha_map.enabled && mask.surface==sample.destination_alpha_map.surface))){
        const struct ps2_surface *m=ps2_surface_lookup(gs,mask.surface);
        if(ps2_surface_create(gs,m->width,m->height,m->bits_per_pixel,&mask_snapshot) ||
            (m->pixel_depth==32 && ps2_surface_set_depth(gs,mask_snapshot,32))){
            error=errno;goto out;}
    }
    status=ps2_render_flush(r);
    if(!status && snapshot){
        const struct ps2_pipe_copy_box copy={0,0,0,0,s->width,s->height};
        status=sample.source_argb || sample.source_rotate?
            ps2_surface_rgba_rectangle(gs,source,snapshot,0,0,0,0,s->width,s->height,UINT32_MAX,PS2GS_RGBA_REPLACE):
            ps2_pipe_copy_boxes(ctx,source,snapshot,&copy,1);
        source=snapshot;s=ps2_surface_lookup(gs,source);
    }
    if(!status && mask_snapshot){
        const struct ps2_pipe_copy_box copy={0,0,0,0,mask.width,mask.height};
        status=mask.sample.source_argb || mask.sample.source_rotate?
            ps2_surface_rgba_rectangle(gs,mask.surface,mask_snapshot,0,0,0,0,mask.width,mask.height,UINT32_MAX,PS2GS_RGBA_REPLACE):
            ps2_pipe_copy_boxes(ctx,mask.surface,mask_snapshot,&copy,1);
        mask.surface=mask_snapshot;
    }
    struct ps2_picture_alpha_map *alpha_maps[]={&sample.source_alpha_map,&mask.sample.source_alpha_map};
    for(unsigned i=0;!status && i<2;++i){struct ps2_picture_alpha_map *m=alpha_maps[i];
        if(!m->enabled || (m->surface!=destination &&
            (!sample.destination_alpha_map.enabled || m->surface!=sample.destination_alpha_map.surface)))continue;
        const struct ps2_surface *original=ps2_surface_lookup(gs,m->surface);
        status=ps2_surface_create(gs,m->width,m->height,original->bits_per_pixel,&alpha_snapshot[i]);
        if(!status && original->pixel_depth==32)status=ps2_surface_set_depth(gs,alpha_snapshot[i],32);
        if(!status)status=original->pixel_depth==32?
            ps2_surface_rgba_rectangle(gs,m->surface,alpha_snapshot[i],0,0,0,0,m->width,m->height,UINT32_MAX,PS2GS_RGBA_REPLACE):
            ps2_rop_sample(gs,m->surface,alpha_snapshot[i],0,0,0,0,m->width,m->height,original->bits_per_pixel==16?0x7fff:0xffffff,0,128);
        if(!status)m->surface=alpha_snapshot[i];
    }
    if(!status && mask_surface){
        uint32_t packed[512];unsigned row_bytes=((mask_row+3)/4)*4;
        for(unsigned y=0;!status && y<mask.height;++y){
            memset(packed,0,row_bytes);memcpy(packed,mask.bits+y*mask.stride,mask_row);
            status=ps2_surface_upload(gs,mask_surface,0,y,row_bytes/4,1,packed,row_bytes,row_bytes);
        }
    }
    static const unsigned source_factors[]={FACTOR_ONE,FACTOR_ONE,FACTOR_ZERO,FACTOR_ZERO,FACTOR_ZERO,
        FACTOR_ZERO,FACTOR_ONE,FACTOR_INVERSE,FACTOR_ALPHA,FACTOR_INVERSE,FACTOR_ALPHA,FACTOR_INVERSE,FACTOR_INVERSE};
    static const unsigned destination_factors[]={FACTOR_ZERO,FACTOR_INVERSE,FACTOR_ZERO,FACTOR_ONE,FACTOR_ALPHA,
        FACTOR_INVERSE,FACTOR_ONE,FACTOR_ONE,FACTOR_ZERO,FACTOR_ZERO,FACTOR_INVERSE,FACTOR_ALPHA,FACTOR_INVERSE};
    for(int y=0;!status && y<box.height;++y)for(int x=0;!status && x<box.width;){
        /* Coordinates were validated for every row before submission. Keep
         * only one row in RAM; regenerating geometry never evaluates colour. */
        if(gradients && !x){
            if(source_positions)status=ps2_gradient_plan_row(&sample.gradient,sample.matrix,sample.transformed,sample.repeat,
                box.sx,box.sy,box.width,box.height,y,source_positions);
            if(!status && mask_positions)status=ps2_gradient_plan_row(&mask.sample.gradient,mask.sample.matrix,mask.sample.transformed,mask.sample.repeat,
                mask.x,mask.y,box.width,box.height,y,mask_positions);
            if(status)break;
        }
        if(simple){
            const struct texel *t=plan+y*box.width+x;unsigned count=1;int step=0;
            if(x+1<box.width && t->x!=UINT16_MAX && t[1].y==t->y){step=(int)t[1].x-t->x;if(step < -1 || step > 1)step=2;}
            while(x+count<(unsigned)box.width){const struct texel *next=t+count;
                if(t->x==UINT16_MAX?next->x!=UINT16_MAX:step==2 || next->y!=t->y || (int)next->x!=(int)t->x+step*(int)count)break;
                ++count;}
            status=operation==PS2_COMPOSITE_CLEAR?ps2_surface_solid_logic(gs,destination,box.dx+x,box.dy+y,count,1,0,
                d->bits_per_pixel==16?0x7fff:0xffffff,0):draw_run(gs,source,destination,s,box.dx+x,box.dy+y,count,t,step,operation==PS2_COMPOSITE_OVER);
            x+=count;continue;
        }
        if(shifted_map && !x){
            /* Pixman's destination iterator fetches the entire alpha-map
             * scanline before either store. Retain those bytes on GS even
             * when the parent row will overwrite their original locations. */
            f.source_kind=PS2_FILTER_CHANNEL;f.source_width=d->width;f.source_height=d->height;
            f.source_channel=sample.destination_rotate?0:3;f.alpha_map=sample.destination_alpha_map;
            for(unsigned offset=0;!status && offset<(unsigned)box.width;offset+=64){
                unsigned n=box.width-offset;if(n>64)n=64;
                status=ps2_filter_fetch(&f,destination,da,palette,box.dx+offset,box.dy+y,0,n,1) ||
                    ps2_rop_sample(gs,da,row_alpha,0,0,offset,0,n,1,0xffffff,0,128)?-1:0;
            }
        }
        unsigned count=box.width-x;if(count>64)count=64;if(float_saturate || gradients)count=1;
        if(sample.gradient.kind){
            if(!status && !float_saturate)status=ps2_gradient_fetch(&wide,&f,&sample.gradient,sample.repeat,
                source_positions[x],floating,floating+4,w.a,sa,palette,0);
        }else{
        f.source_kind=PS2_FILTER_RGB;f.source_rotate=sample.source_rotate;f.alpha_map=(struct ps2_picture_alpha_map){0};
        if(!status)status=sample_row(&f,source,w.a,w.c,palette,&sample,(int64_t)box.sx+x,(int64_t)box.sy+y,count);
        if(!status && s->bits_per_pixel==16 && !filtered_sample(&sample))status=expand_rgb555(gs,w.a,w.c,palette,count);
        if(!status && sample.source_swap)status=swap_rgb(gs,w.a,w.c,palette,count) ||
            ps2_rop_sample(gs,w.c,w.a,0,0,0,0,count,1,0xffffff,0,128)?-1:0;
        f.source_kind=sample.source_argb?PS2_FILTER_CHANNEL:PS2_FILTER_CONSTANT;
        f.source_channel=sample.source_rotate?0:3;f.source_width=s->width;f.source_height=s->height;
        f.source_constant=sample.has_alpha?sample.alpha:255;
        f.alpha_map=sample.source_alpha_map;
        if(!status)status=sample_row(&f,source,sa,w.c,palette,&sample,(int64_t)box.sx+x,(int64_t)box.sy+y,count);
        }
        if(!status && sample.destination_rotate)status=ps2_surface_rgb_rotate(gs,destination,w.b,palette,box.dx+x,box.dy+y,0,0,count,1);
        else if(!status){unsigned offset;
            status=ps2_rop_capture(&w,destination,w.destination,box.dx+x,box.dy+y,count,1,&offset) ||
                ps2_rop_sample(gs,w.destination,w.b,offset,0,0,0,count,1,0xffffff,0,128)?-1:0;
        }
        if(!status && d->bits_per_pixel==16)status=expand_rgb555(gs,w.b,w.c,palette,count);
        if(!status && sample.destination_swap)status=swap_rgb(gs,w.b,w.c,palette,count) ||
            ps2_rop_sample(gs,w.c,w.b,0,0,0,0,count,1,0xffffff,0,128)?-1:0;
        f.source_kind=PS2_FILTER_CHANNEL;f.source_width=d->width;f.source_height=d->height;
        f.source_channel=sample.destination_rotate?0:3;
        f.alpha_map=sample.destination_alpha_map;
        if(!status && shifted_map)status=ps2_rop_sample(gs,row_alpha,da,x,0,0,0,count,1,0xffffff,0,128);
        else if(!status)status=destination_alpha?ps2_filter_fetch(&f,destination,da,palette,box.dx+x,box.dy+y,0,count,1):
            ps2_surface_solid_logic(gs,da,0,0,count,1,0xffffff,0xffffff,3);
        if(!status && input_mask){
            f.source_width=mask.width;f.source_height=mask.height;f.source_msb_first=mask.msb_first;
            f.source_alpha_bits=mask.alpha_bits;
            uint32_t mh=mask.has_surface?mask.surface:mask_surface;
            if(mask.sample.gradient.kind){
                if(!float_saturate)status=ps2_gradient_fetch(&wide,&f,&mask.sample.gradient,mask.sample.repeat,
                    mask_positions[x],floating+8,floating+12,mask.component_alpha?ma:w.c,
                    mask.component_alpha?mf:ma,palette,0);
            }else{
            if(mask.component_alpha){
                /* An alpha-only picture contributes zero RGB components.
                 * Its independent alpha channel still masks source alpha. */
                if(!mask.has_surface)status=ps2_surface_solid_logic(gs,ma,0,0,count,1,0,0xffffff,3);
                else{
                    f.source_kind=PS2_FILTER_RGB;f.source_rotate=mask.sample.source_rotate;f.alpha_map.enabled=0;
                    status=sample_row(&f,mh,ma,w.c,palette,&mask.sample,(int64_t)mask.x+x,(int64_t)mask.y+y,count);
                    if(!status && ps2_surface_lookup(gs,mh)->bits_per_pixel==16 && !filtered_sample(&mask.sample))
                        status=expand_rgb555(gs,ma,w.c,palette,count);
                    if(!status && mask.sample.source_swap)status=swap_rgb(gs,ma,w.c,palette,count) ||
                        ps2_rop_sample(gs,w.c,ma,0,0,0,0,count,1,0xffffff,0,128)?-1:0;
                }
            }
            f.source_kind=mask.has_surface?(mask.sample.source_argb?PS2_FILTER_CHANNEL:PS2_FILTER_CONSTANT):
                mask.bits_per_pixel==8?PS2_FILTER_A8:PS2_FILTER_A1;
            f.source_channel=mask.sample.source_rotate?0:3;f.source_constant=mask.sample.has_alpha?mask.sample.alpha:255;
            f.alpha_map=mask.sample.source_alpha_map;
            if(!status)status=sample_row(&f,mh,mask.component_alpha?mf:ma,w.c,palette,&mask.sample,
                (int64_t)mask.x+x,(int64_t)mask.y+y,count);
            }
            if(!status && mask.component_alpha && !wide_saturate)status=ps2_native_multiply(&f,sa,ma,ca,palette,count,1);
            if(!status && operation!=PS2_COMPOSITE_SATURATE)status=ps2_native_multiply(&f,w.a,ma,w.a,palette,count,1) ||
                ps2_native_multiply(&f,sa,mask.component_alpha?mf:ma,sa,palette,count,1)?-1:0;
        }
        if(operation==PS2_COMPOSITE_SATURATE){
            if(!status && !input_mask)status=ps2_surface_solid_logic(gs,ma,0,0,count,1,0xffffff,0xffffff,3);
            if(!status && float_saturate){
                struct ps2_unorm_layout sl=unorm_layout(sample.source_unorm,s->bits_per_pixel),
                    dl=unorm_layout(sample.destination_unorm,d->bits_per_pixel);
                unsigned source_alpha_bits=sample.source_alpha_map.enabled && sample.source_alpha_map.alpha_bits?
                    sample.source_alpha_map.alpha_bits:sample.source_alpha_map.enabled?8:sl.alpha_bits;
                const unsigned sabits[]={source_alpha_bits,source_alpha_bits,source_alpha_bits};
                if(sample.gradient.kind){
                    if(!status)status=ps2_gradient_fetch(&wide,&f,&sample.gradient,sample.repeat,
                        source_positions[x],floating,floating+4,0,0,palette,1);
                }else{
                f.alpha_map.enabled=0;f.source_kind=PS2_FILTER_RGB;f.source_rotate=sample.source_rotate;
                if(!alpha_result_only)status=sample_float(&wide,&f,source,floating,w.a,w.c,palette,sl.rgb_bits,&sample,(int64_t)box.sx+x,(int64_t)box.sy+y);
                f.source_kind=sample.source_argb?PS2_FILTER_CHANNEL:PS2_FILTER_CONSTANT;
                f.source_width=s->width;f.source_height=s->height;f.source_channel=sample.source_rotate?0:3;
                f.source_constant=sample.has_alpha?sample.alpha:255;
                f.alpha_map=sample.source_alpha_map;
                if(!status)status=sample_float(&wide,&f,source,floating+4,sa,w.c,palette,sabits,&sample,(int64_t)box.sx+x,(int64_t)box.sy+y);
                }
                uint32_t mh=mask.has_surface?mask.surface:mask_surface;
                if(!status && mask.sample.gradient.kind){
                    status=ps2_gradient_fetch(&wide,&f,&mask.sample.gradient,mask.sample.repeat,
                        mask_positions[x],floating+8,floating+12,0,0,palette,1);
                }else if(!status && (!input_mask || (mask.has_surface && !mask.component_alpha &&
                    opaque_sample(&mask.sample,mask.x,mask.y,box.width,box.height,mask.width,mask.height))))
                    status=float_constant(gs,floating+12,0x3f800000);
                else if(!status){
                    const struct ps2_surface *ms=mask.has_surface?ps2_surface_lookup(gs,mh):NULL;
                    struct ps2_unorm_layout ml=unorm_layout(mask.sample.source_unorm,ms?ms->bits_per_pixel:32);
                    unsigned abits=mask.has_surface?ml.alpha_bits:mask.alpha_bits?mask.alpha_bits:mask.bits_per_pixel==8?8:1;
                    if(mask.sample.source_alpha_map.enabled)abits=mask.sample.source_alpha_map.alpha_bits?mask.sample.source_alpha_map.alpha_bits:8;
                    const unsigned mabits[]={abits,abits,abits};
                    f.source_width=mask.width;f.source_height=mask.height;f.source_msb_first=mask.msb_first;f.source_alpha_bits=mask.alpha_bits;
                    if(mask.component_alpha && !alpha_result_only){
                        f.source_kind=PS2_FILTER_RGB;f.source_rotate=mask.sample.source_rotate;f.alpha_map.enabled=0;
                        status=mask.has_surface?sample_float(&wide,&f,mh,floating+8,ma,w.c,palette,ml.rgb_bits,&mask.sample,
                            (int64_t)mask.x+x,(int64_t)mask.y+y):float_constant(gs,floating+8,0);
                    }
                    f.source_kind=mask.has_surface?(mask.sample.source_argb?PS2_FILTER_CHANNEL:PS2_FILTER_CONSTANT):
                        mask.bits_per_pixel==8?PS2_FILTER_A8:PS2_FILTER_A1;
                    f.source_channel=mask.sample.source_rotate?0:3;f.source_constant=mask.sample.has_alpha?mask.sample.alpha:255;
                    f.alpha_map=mask.sample.source_alpha_map;
                    if(!status)status=sample_float(&wide,&f,mh,floating+12,mf?mf:ma,w.c,palette,mabits,&mask.sample,
                        (int64_t)mask.x+x,(int64_t)mask.y+y);
                }
                struct ps2_saturate_map_target target={.input_bits=sample.destination_alpha_map.alpha_bits?
                    sample.destination_alpha_map.alpha_bits:8,.map=sf,
                    .map_bits=sample.destination_alpha_map.alpha_bits?sample.destination_alpha_map.alpha_bits:8};
                if(sample.destination_alpha_map.enabled && !sample.destination_alpha_map.alpha_only){
                    target.colour=ma;
                    const struct ps2_surface *map_surface=ps2_surface_lookup(gs,sample.destination_alpha_map.surface);
                    for(unsigned c=0;c<3;++c)target.rgb_bits[c]=sample.destination_alpha_map.rgb_bits[c]?
                        sample.destination_alpha_map.rgb_bits[c]:map_surface->bits_per_pixel==16?5:8;
                }
                const struct ps2_saturate_map_target *mapped=sample.destination_alpha_map.enabled?&target:NULL;
                if(!status && hsl_operation)status=alpha_result_only?
                    ps2_native_hsl_alpha_float_mapped(&wide,&f,floating+4,floating+12,da,dl.alpha_bits,
                        mapped,palette,1,1,hsl_operation-PS2_COMPOSITE_HSL_HUE):
                    ps2_native_hsl_float_mapped_consume(&wide,&f,floating,floating+4,
                        floating+12,floating+12,w.b,da,&dl,mapped,palette,1,1,hsl_operation-PS2_COMPOSITE_HSL_HUE);
                else if(!status && blend_operation==PS2_COMPOSITE_SOFTLIGHT)status=alpha_result_only?
                    ps2_native_softlight_alpha_float_mapped(&wide,&f,floating+4,floating+12,da,dl.alpha_bits,
                        mapped,palette,1,1):
                    ps2_native_softlight_float_mapped(&wide,&f,floating,floating+4,
                        mask.component_alpha?floating+8:floating+12,floating+12,w.b,da,&dl,mapped,palette,1,1);
                else if(!status && blend_operation)status=alpha_result_only?
                    ps2_native_dodge_burn_alpha_float_mapped(&wide,&f,floating+4,floating+12,da,dl.alpha_bits,
                        mapped,palette,1,1,blend_operation-PS2_COMPOSITE_COLOR_DODGE):
                    ps2_native_dodge_burn_float_mapped(&wide,&f,floating,floating+4,
                        mask.component_alpha?floating+8:floating+12,floating+12,w.b,da,&dl,mapped,palette,1,1,
                        blend_operation-PS2_COMPOSITE_COLOR_DODGE);
                else if(!status && pd_operation)status=alpha_result_only?
                    ps2_native_porter_duff_alpha_float_mapped(&wide,&f,floating+4,floating+12,da,dl.alpha_bits,
                        mapped,palette,1,1,pd_operation):
                    ps2_native_porter_duff_float_mapped(&wide,&f,floating,floating+4,
                        mask.component_alpha?floating+8:floating+12,floating+12,w.b,da,&dl,mapped,palette,1,1,pd_operation);
                else if(!status)status=alpha_result_only?
                    ps2_native_saturate_alpha_float_mapped(&wide,&f,floating+4,floating+12,da,dl.alpha_bits,mapped,palette,1,1):
                    ps2_native_saturate_float_mapped(&wide,&f,floating,floating+4,
                        mask.component_alpha?floating+8:floating+12,floating+12,w.b,da,&dl,mapped,palette,1,1);
            }else if(!status && wide_saturate){
                const struct ps2_surface *ms=mask.has_surface?ps2_surface_lookup(gs,mask.surface):NULL;
                struct ps2_unorm_layout layouts[]={unorm_layout(sample.source_unorm,s->bits_per_pixel),
                    unorm_layout(mask.sample.source_unorm,ms?ms->bits_per_pixel:32),
                    unorm_layout(sample.destination_unorm,d->bits_per_pixel)};
                if(!mask.has_surface && input_mask)layouts[1].alpha_bits=mask.alpha_bits?mask.alpha_bits:mask.bits_per_pixel==8?8:1;
                status=ps2_native_saturate_unorm(&wide,&f,w.a,sa,ma,mask.component_alpha?mf:ma,w.b,da,layouts,palette,count,1);
            }else if(!status)status=ps2_native_saturate_alpha(&f,sa,ma,da,w.a,w.b,w.c,sf,palette,count);
        }
        if(!status && (operation==PS2_COMPOSITE_SCREEN || operation==PS2_COMPOSITE_EXCLUSION || operation>=PS2_COMPOSITE_DARKEN)){
            /* Screen and Exclusion cancel the RGB alpha factors; ordered
             * blends compare unrounded alpha products. All use Over's alpha
             * through complement multiplication. No operands leave GS. */
            const uint32_t work[]={w.c,sf,ma};
            if(operation>=PS2_COMPOSITE_OVERLAY){
                const uint32_t pieces[]={w.c,sf,ma,order_work,piecewise_work[0],piecewise_work[1]};
                status=ps2_native_piecewise_blend(&f,w.a,w.b,mask.component_alpha?ca:sa,da,w.b,
                    pieces,palette,count,1,operation-PS2_COMPOSITE_OVERLAY);
            }else if(operation>=PS2_COMPOSITE_DARKEN){
                const uint32_t ordered[]={w.c,sf,ma,order_work};
                status=ps2_native_order_blend(&f,w.a,w.b,mask.component_alpha?ca:sa,da,w.b,
                    ordered,palette,count,1,operation-PS2_COMPOSITE_DARKEN);
            }else if(operation==PS2_COMPOSITE_EXCLUSION)
                status=ps2_native_exclusion(&f,w.a,w.b,w.b,work,palette,count,1);
            else status=factor_plane(gs,w.a,sf,palette,count,FACTOR_INVERSE) ||
                factor_plane(gs,w.b,ma,palette,count,FACTOR_INVERSE) ||
                ps2_native_multiply(&f,sf,ma,w.a,palette,count,1) ||
                factor_plane(gs,w.a,w.b,palette,count,FACTOR_INVERSE)?-1:0;
            if(!status && destination_alpha)status=factor_plane(gs,sa,sf,palette,count,FACTOR_INVERSE) ||
                factor_plane(gs,da,ma,palette,count,FACTOR_INVERSE) ||
                ps2_native_multiply(&f,sf,ma,sa,palette,count,1) ||
                factor_plane(gs,sa,da,palette,count,FACTOR_INVERSE)?-1:0;
        }
        if(!status && operation==PS2_COMPOSITE_MULTIPLY){
            /* Pixman's byte Multiply rounds each of the three products
             * separately, then clamps their sum. Component alpha supplies
             * source-alpha times mask RGB for the destination RGB factor;
             * the alpha result uses the independent scalar mask alpha. */
            status=factor_plane(gs,da,sf,palette,count,FACTOR_INVERSE) ||
                ps2_native_multiply(&f,w.a,sf,w.c,palette,count,1) ||
                factor_plane(gs,mask.component_alpha?ca:sa,ma,palette,count,FACTOR_INVERSE) ||
                ps2_native_multiply(&f,w.b,ma,ma,palette,count,1) ||
                ps2_native_multiply(&f,w.a,w.b,w.b,palette,count,1) ||
                ps2_rop_sample(gs,w.c,w.b,0,0,0,0,count,1,0xffffff,PS2GS_TRANSFER_ADD_CLAMP,128) ||
                ps2_rop_sample(gs,ma,w.b,0,0,0,0,count,1,0xffffff,PS2GS_TRANSFER_ADD_CLAMP,128)?-1:0;
            if(!status && destination_alpha)status=
                ps2_native_multiply(&f,sa,sf,w.c,palette,count,1) ||
                factor_plane(gs,sa,sf,palette,count,FACTOR_INVERSE) ||
                ps2_native_multiply(&f,da,sf,ma,palette,count,1) ||
                ps2_native_multiply(&f,sa,da,da,palette,count,1) ||
                ps2_rop_sample(gs,w.c,da,0,0,0,0,count,1,0xffffff,PS2GS_TRANSFER_ADD_CLAMP,128) ||
                ps2_rop_sample(gs,ma,da,0,0,0,0,count,1,0xffffff,PS2GS_TRANSFER_ADD_CLAMP,128)?-1:0;
        }
        unsigned porter_duff=operation<=PS2_COMPOSITE_XOR;
        unsigned sk=porter_duff?source_factors[operation]:FACTOR_ZERO;
        unsigned dk=porter_duff?destination_factors[operation]:FACTOR_ZERO;
        if(!status && mask.component_alpha && porter_duff)status=factor_plane(gs,sa,mf,palette,count,dk);
        if(!status && porter_duff)status=factor_plane(gs,da,sf,palette,count,sk) || factor_plane(gs,mask.component_alpha?ca:sa,ma,palette,count,dk) ||
            apply_factor(&f,w.a,sf,palette,count,sk) || apply_factor(&f,w.b,ma,palette,count,dk)?-1:0;
        if(!status && destination_alpha && porter_duff)status=apply_factor(&f,sa,sf,palette,count,sk) ||
            apply_factor(&f,da,mask.component_alpha?mf:ma,palette,count,dk) ||
            ps2_rop_sample(gs,sa,da,0,0,0,0,count,1,0xffffff,PS2GS_TRANSFER_ADD_CLAMP,128)?-1:0;
        if(!status && operation<PS2_COMPOSITE_SCREEN && !wide_saturate && (!sample.alpha_only_destination ||
            (sample.destination_alpha_map.enabled && !sample.destination_alpha_map.alpha_only)))
            status=ps2_rop_sample(gs,w.a,w.b,0,0,0,0,count,1,0xffffff,PS2GS_TRANSFER_ADD_CLAMP,128);
        if(!status && shifted_map && !sample.destination_alias){
            struct ps2_unorm_layout layout=unorm_layout(sample.destination_unorm,d->bits_per_pixel);
            const unsigned abits[]={layout.alpha_bits,layout.alpha_bits,layout.alpha_bits};
            if(!sample.alpha_only_destination)status=quantize_store(gs,w.b,w.c,palette,layout.rgb_bits,count);
            if(!status)status=quantize_store(gs,da,w.c,palette,abits,count);
        }
        /* Stash canonical map outputs before parent format conversion uses
         * w.a/w.c or quantization loses precision for a different map
         * format. The whole map row is published after the parent row. */
        if(!status && shifted_map){
            if(!sample.destination_alpha_map.alpha_only)
                status=ps2_rop_sample(gs,float_saturate?ma:w.b,row_map,0,0,x,0,count,1,0xffffff,0,128);
            if(!status)status=ps2_surface_alpha_write(gs,float_saturate?sf:da,w.c,row_map,palette,x,0,count,1,0xff000000);
        }
        if(!status && sample.destination_alias){
            struct ps2_unorm_layout layout=unorm_layout(sample.destination_unorm,d->bits_per_pixel);
            const unsigned abits[]={layout.alpha_bits,layout.alpha_bits,layout.alpha_bits};
            if(!sample.alpha_only_destination)status=quantize_store(gs,w.b,w.c,palette,layout.rgb_bits,count);
            if(!status)status=quantize_store(gs,da,w.c,palette,abits,count);
        }
        if(!status && !sample.alpha_only_destination)
            status=write_colour(&w,palette,destination,box.dx+x,box.dy+y,count,&sample,d->bits_per_pixel);
        if(!status && sample.destination_argb){
            if(sample.destination_rotate){uint32_t table[256];for(unsigned i=0;i<256;++i)table[i]=i;
                status=ps2_surface_map_palette(gs,palette,table) ||
                    ps2_surface_channel_map_region(gs,da,destination,palette,0,0,box.dx+x,box.dy+y,count,1,255,0)?-1:0;
            }else status=ps2_surface_alpha_write(gs,da,w.c,destination,palette,box.dx+x,box.dy+y,count,1,0xff000000);
        }
        if(!status && sample.destination_alpha_map.enabled && !shifted_map)
            status=alpha_map_write(&w,float_saturate?sf:da,float_saturate?ma:w.b,palette,&sample.destination_alpha_map,box.dx+x,box.dy+y,count);
        x+=count;
        if(!status && shifted_map && x==box.width){
            if(sample.destination_alias)status=ps2_format_alias_parent(sample.destination_alias,box.dx,box.dy+y,box.width);
            f.source_kind=PS2_FILTER_CHANNEL;f.source_width=box.width;f.source_height=1;
            f.source_channel=3;f.alpha_map.enabled=0;
            for(unsigned offset=0;!status && offset<(unsigned)box.width;offset+=64){
                unsigned n=box.width-offset;if(n>64)n=64;
                status=ps2_rop_sample(gs,row_map,w.b,offset,0,0,0,n,1,0xffffff,0,128) ||
                    ps2_filter_fetch(&f,row_map,da,palette,offset,0,0,n,1) ||
                    alpha_map_write(&w,da,w.b,palette,&sample.destination_alpha_map,box.dx+offset,box.dy+y,n)?-1:0;
            }
            if(!status && sample.destination_alias)status=ps2_format_alias_map(sample.destination_alias,
                box.dx-sample.destination_alpha_map.x,box.dy+y-sample.destination_alpha_map.y,box.width,box.dy+y);
        }
    }
    error=errno;
out:
    for(unsigned i=0;i<16;++i)if(floating[i] && !gs->error && ps2_surface_destroy(gs,floating[i]) && !status){status=-1;error=errno;}
    if(wide.gs && ps2_divide_release(&wide) && !status){status=-1;error=errno;}
    if(f.gs && ps2_filter_release(&f) && !status){status=-1;error=errno;}
    uint32_t handles[]={piecewise_work[0],piecewise_work[1],order_work,row_alpha,row_map,alpha_snapshot[0],alpha_snapshot[1],mf,ca,sf,ma,da,sa,mask_surface,mask_snapshot,palette,snapshot};
    for(unsigned i=0;i<sizeof(handles)/sizeof(*handles);++i)
        if(handles[i] && !gs->error && ps2_surface_destroy(gs,handles[i]) && !status){status=-1;error=errno;}
    if(w.gs && ps2_rop_release(&w) && !status){status=-1;error=errno;}
    free(plan);ps2_pipe_context_release(ctx);if(status)errno=error;return status;
}

static struct ps2_packed_layout packed10_layout(const struct ps2_picture_sample *s,unsigned destination)
{
    unsigned id=destination?s->destination_packed10:s->source_packed10,
        swap=destination?s->destination_swap:s->source_swap,rotate=destination?s->destination_rotate:s->source_rotate,
        alpha=destination?s->destination_argb:s->source_argb;
    if(id)return (struct ps2_packed_layout){.bits={10,10,10,id<=PS2_PACKED10_ABGR?2:0},
        .shift={id==PS2_PACKED10_ABGR || id==PS2_PACKED10_BGR?0:20,10,
            id==PS2_PACKED10_ABGR || id==PS2_PACKED10_BGR?20:0,30}};
    const struct ps2_unorm_layout *precision=destination?&s->destination_unorm:&s->source_unorm;
    struct ps2_packed_layout layout={.shift={rotate?8:swap?16:0,rotate?16:8,rotate?24:swap?0:16,rotate?0:24}};
    for(unsigned c=0;c<4;++c){
        layout.bits[c]=c==3?alpha?(precision->alpha_bits?precision->alpha_bits:8):0:
            precision->rgb_bits[c]?precision->rgb_bits[c]:8;
        if(layout.bits[c] && layout.bits[c]<=8)layout.shift[c]+=8-layout.bits[c];
    }
    return layout;
}
static int packed10_unorm_valid(const struct ps2_unorm_layout *u)
{
    return u->rgb_bits[0]<=8 && u->rgb_bits[1]<=8 && u->rgb_bits[2]<=8 && u->alpha_bits<=8;
}
static int packed10_storage_valid(struct ps2_surface_client *gs,uint32_t handle,const struct ps2_packed_layout *layout)
{
    const struct ps2_surface *image=ps2_surface_lookup(gs,handle);
    if(!image || !ps2_packed_layout_valid(layout))return 0;
    if(image->bits_per_pixel==16){
        unsigned bits=layout->bits[0];
        return ps2_surface_depth(image)==15 && !layout->bits[3] && (bits==4 || bits==5) &&
            layout->bits[1]==bits && layout->bits[2]==bits && layout->shift[1]==16-bits &&
            ((layout->shift[0]==8-bits && layout->shift[2]==24-bits) ||
             (layout->shift[0]==24-bits && layout->shift[2]==8-bits));
    }
    if(image->bits_per_pixel!=32)return 0;
    uint32_t used=0;for(unsigned c=0;c<4;++c)used|=((1U<<layout->bits[c])-1)<<layout->shift[c];
    return !(used&~ps2_surface_native_mask(gs,handle));
}
static int packed10_sample_valid(const struct ps2_picture_sample *s)
{
    if(!s || s->source_packed10>PS2_PACKED10_BGR || s->transformed>1 || s->repeat>3 ||
        s->source_swap>1 || s->source_rotate>1 || s->source_argb>1 || s->has_alpha>1 ||
        (s->source_swap && s->source_rotate) || !filter_valid(s) || !solid_valid(s) ||
        (s->has_alpha && !s->source_solid) ||
        (s->source_alpha_map.enabled && (s->source_solid || s->gradient.kind)))return 0;
    if(s->source_packed10 && (s->source_swap || s->source_rotate || s->source_solid || s->gradient.kind ||
        s->source_argb!=(s->source_packed10<=PS2_PACKED10_ABGR)))return 0;
    if(!s->source_packed10 && !packed10_unorm_valid(&s->source_unorm))return 0;
    return !s->gradient.kind || (ps2_gradient_valid(&s->gradient) && !s->source_solid);
}
static int packed10_geometry(const struct ps2_picture_sample *s,int x,int y,unsigned width,unsigned height,
    unsigned sw,unsigned sh)
{
    if(s->source_solid || s->gradient.kind)return 0;
    for(unsigned row=0;row<height;++row)for(unsigned col=0;col<width;++col){
        int32_t fixed[3];struct texel t;
        if(filtered_sample(s)?mask_fixed(s,(int64_t)x+col,(int64_t)y+row,fixed):point(s,(int64_t)x+col,(int64_t)y+row,sw,sh,&t))return -1;
    }
    return 0;
}
static int packed10_read_map_valid(struct ps2_surface_client *gs,const struct ps2_picture_alpha_map *map)
{
    if(!map->enabled)return map->enabled==0;
    struct ps2_picture_alpha_map plain=*map;
    if(packed10_map(map)){
        const struct ps2_surface *image=ps2_surface_lookup(gs,map->surface);
        if(!map->surface || !image || image->bits_per_pixel!=32 || (ps2_surface_native_mask(gs,map->surface)&(map->has_alpha?UINT32_MAX:0x3fffffff))!=(map->has_alpha?UINT32_MAX:0x3fffffff) ||
            map->has_alpha>1 || map->alpha_only || map->swap>1 || map->rotate ||
            map->alpha_bits!=(map->has_alpha?2U:0U))return 0;
        for(unsigned c=0;c<3;++c)if(map->rgb_bits[c]!=10)return 0;
        for(unsigned c=0;c<3;++c)plain.rgb_bits[c]=8;
    }
    const struct ps2_surface *image=ps2_surface_lookup(gs,map->surface);
    if(image && image->bits_per_pixel==16){
        unsigned bits=map->rgb_bits[0]?map->rgb_bits[0]:5;
        if(ps2_surface_depth(image)!=15 || map->has_alpha || map->alpha_only || map->rotate ||
            (bits!=4 && bits!=5) || (map->rgb_bits[1]?map->rgb_bits[1]:5)!=bits ||
            (map->rgb_bits[2]?map->rgb_bits[2]:5)!=bits)return 0;
    }
    return alpha_map_valid(gs,&plain);
}
/* Native CT16 operands remain in their original GS backing. Only the texel
 * being sampled and the row being published need CT32 scratch. In particular,
 * a one-pixel Render request never allocates a decoded display-sized canvas. */
static struct ps2_packed_layout packed10_native16_layout(unsigned bits,unsigned swap)
{
    return (struct ps2_packed_layout){.bits={bits,bits,bits,0},
        .shift={swap?bits*2:0,bits,swap?0:bits*2,0}};
}
static int packed10_native16_publish(struct ps2_surface_client *gs,uint32_t raw,uint32_t rgb,
    uint32_t target,uint32_t palette,unsigned sx,unsigned sy,unsigned dx,unsigned dy,unsigned width)
{
    uint32_t table[256];
    for(unsigned i=0;i<256;++i)table[i]=((i&31)<<3)|((i>>5)<<11);
    if(ps2_surface_map_palette(gs,palette,table) ||
        ps2_surface_channel_map_region(gs,raw,rgb,palette,sx,sy,0,0,width,1,0x38f8,0))return -1;
    for(unsigned i=0;i<256;++i)table[i]=((i&3)<<14)|(((i>>2)&31)<<19);
    return ps2_surface_map_palette(gs,palette,table) ||
        ps2_surface_channel_map_region(gs,raw,rgb,palette,sx,sy,0,0,width,1,0xf8c000,1) ||
        ps2_rop_sample(gs,rgb,target,0,0,dx,dy,width,1,0x7fff,PS2GS_TRANSFER_REPLACE,128)?-1:0;
}
/* Map precision describes the original backing, not its decoded CT32 view.
 * Ten-bit maps use swap for their raw RGB/BGR field order. Other maps are
 * quantized independently, then GS lookups restore their decoded bytes. */
static int packed10_map_store(struct ps2_packed_workspace *p,struct ps2_divide_workspace *w,
    struct ps2_filter_workspace *f,const uint32_t in[8],const struct ps2_picture_alpha_map *map,
    uint32_t temporary,uint32_t row,unsigned x,unsigned y,uint32_t palette)
{
    const struct ps2_surface *image=ps2_surface_lookup(w->gs,map->surface);
    struct ps2_packed_layout layout={0};
    if(image->bits_per_pixel==16){
        layout=packed10_native16_layout(map->rgb_bits[0]?map->rgb_bits[0]:5,map->swap);
        if(ps2_surface_lookup(w->gs,row)->bits_per_pixel==32)
            return ps2_packed_store(p,w,f,in,row,x,y,&layout,palette,1,1);
        return ps2_packed_store(p,w,f,in,temporary,0,0,&layout,palette,1,1) ||
            packed10_native16_publish(w->gs,temporary,p->scratch[0],row,palette,0,0,x,y,1)?-1:0;
    }
    if(packed10_map(map)){
        layout=(struct ps2_packed_layout){.bits={10,10,10,map->has_alpha?2:0},
            .shift={map->swap?20:0,10,map->swap?0:20,30}};
        return ps2_packed_store(p,w,f,in,row,x,y,&layout,palette,1,1);
    }
    unsigned rotated=image->bits_per_pixel==32 && map->rotate,
        swapped=image->bits_per_pixel==32 && map->swap;
    unsigned bytes[]={rotated?1:swapped?2:0,rotated?2:1,rotated?3:swapped?0:2,rotated?0:3};
    for(unsigned c=0;c<4;++c){
        layout.bits[c]=c==3?map->has_alpha?(map->alpha_bits?map->alpha_bits:8):0:
            map->alpha_only?8:map->rgb_bits[c]?map->rgb_bits[c]:image->bits_per_pixel==16?5:8;
        layout.shift[c]=bytes[c]*8+(layout.bits[c]?8-layout.bits[c]:0);
    }
    if(ps2_packed_store(p,w,f,in,temporary,0,0,&layout,palette,1,1) ||
        ps2_surface_solid_logic(w->gs,row,x,y,1,1,0,ps2_surface_native_mask(w->gs,row),3))return -1;
    for(unsigned c=map->alpha_only?3:0;c<4;++c)if(layout.bits[c]){
        unsigned bits=layout.bits[c];uint32_t table[256];
        for(unsigned i=0;i<256;++i){
            unsigned code=i>>(8-bits),value=code,filled=bits;
            while(filled<8){value=(value<<bits)|code;filled+=bits;}
            table[i]=(value>>(filled-8))<<(bytes[c]*8);
        }
        if(ps2_surface_map_palette(w->gs,palette,table) ||
            ps2_surface_channel_map_region(w->gs,temporary,row,palette,0,0,x,y,1,1,
                255U<<(bytes[c]*8),bytes[c]))return -1;
    }
    return 0;
}
static int packed10_map_publish(struct ps2_surface_client *gs,const struct ps2_picture_alpha_map *map,
    uint32_t row,uint32_t temporary,uint32_t scratch[2],uint32_t palette,unsigned x,unsigned y,unsigned width)
{
    const struct ps2_surface *image=ps2_surface_lookup(gs,map->surface);
    unsigned mx=(int64_t)x-map->x,my=(int64_t)y-map->y;
    if(!map->surface && image->bits_per_pixel==32)
        return ps2_rop_sample(gs,row,0,0,0,mx,my,width,1,0xffffff,PS2GS_TRANSFER_REPLACE,128);
    if(image->bits_per_pixel==32)
        return ps2_surface_rgba_rectangle(gs,row,map->surface,0,0,mx,my,width,1,
            ps2_surface_native_mask(gs,map->surface),PS2GS_RGBA_REPLACE);
    (void)temporary;
    for(unsigned offset=0;offset<width;offset+=64){
        unsigned count=width-offset;if(count>64)count=64;
        if(packed10_native16_publish(gs,row,scratch[0],map->surface,palette,offset,0,mx+offset,my,count))return -1;
    }
    return 0;
}
/* Replace alpha at the original parent texel, before any filtering. The
 * map's own transform, repeat and nested map are deliberately not sampled.
 * Decoded maps retain their original alpha's high bits; raw A2 maps use
 * the same GS channel extraction without reducing the parent's RGB10. */
static int packed10_texel(struct ps2_packed_workspace *p,struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    uint32_t raw,const struct ps2_picture_sample *s,unsigned x,unsigned y,const uint32_t out[8],
    const uint32_t temp[12],uint32_t palette,uint32_t front_texel)
{
    struct ps2_packed_layout layout=packed10_layout(s,0);
    const struct ps2_surface *image=ps2_surface_lookup(w->gs,raw);
    uint32_t texel=raw;unsigned tx=x,ty=y;
    if(!raw){
        /* The display is a transfer endpoint, never a GS texture handle. */
        struct ps2_rop_workspace capture={.gs=w->gs};unsigned offset;
        if(ps2_rop_capture(&capture,0,front_texel,x,y,1,1,&offset))return -1;
        texel=front_texel;tx=offset;ty=0;
    }
    if(image->bits_per_pixel==16){
        if(ps2_rop_sample(w->gs,texel,temp[8],tx,ty,0,0,1,1,0xffffff,PS2GS_TRANSFER_REPLACE,128))return -1;
        texel=temp[8];tx=ty=0;
        if(layout.bits[0]==4){
            uint32_t table[256];
            for(unsigned c=0;c<3;++c){
                for(unsigned i=0;i<256;++i)table[i]=(i>>3)<<(c*5);
                if(ps2_surface_map_palette(w->gs,palette,table) ||
                    ps2_surface_channel_map_region(w->gs,temp[8],temp[9],palette,0,0,0,0,1,1,31U<<(c*5),c))return -1;
            }
            texel=temp[9];layout=packed10_native16_layout(4,s->source_swap);
        }
    }
    if(ps2_packed_fetch(p,texel,tx,ty,&layout,out,palette,1,1))return -1;
    if(!s->source_alpha_map.enabled)return 0;
    struct ps2_filter_workspace mapped=*f;mapped.source_kind=PS2_FILTER_CONSTANT;
    mapped.source_width=image->width;mapped.source_height=image->height;mapped.source_constant=255;
    mapped.alpha_map=s->source_alpha_map;
    unsigned precision=mapped.alpha_map.has_alpha?(mapped.alpha_map.alpha_bits?mapped.alpha_map.alpha_bits:8):8;
    const unsigned bits[]={precision,precision,precision};
    return ps2_filter_fetch(&mapped,raw,temp[8],palette,x,y,0,1,0) ||
        ps2_native_unorm_float(w->gs,temp[8],out+4,bits,palette,1,1)?-1:0;
}
static int packed10_fetch(struct ps2_packed_workspace *p,struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    uint32_t raw,const struct ps2_picture_sample *s,int x,int y,int64_t ordinate,const uint32_t out[8],
    const uint32_t temp[12],uint32_t palette,uint32_t front_texel)
{
    if(s->source_solid)return solid_float(w->gs,out,s,0) || solid_float(w->gs,out+4,s,1)?-1:0;
    if(s->gradient.kind)return ps2_gradient_fetch(w,f,&s->gradient,s->repeat,ordinate,out,out+4,0,0,palette,1);
    const struct ps2_surface *image=ps2_surface_lookup(w->gs,raw);
    if(s->convolution){
        int32_t fixed[3];int64_t ix,iy;
        if(mask_fixed(s,x,y,fixed) || float_constant(w->gs,out,0) || float_constant(w->gs,out+4,0))return -1;
        convolution_origin(fixed,s,&ix,&iy);
        unsigned kw=s->filter_params[0]/65536,kh=s->filter_params[1]/65536;
        for(unsigned v=0;v<kh;++v)for(unsigned u=0;u<kw;++u){unsigned tx,ty;
            int32_t coefficient=s->filter_params[2+v*kw+u];
            if(coefficient && fixed[2] && coordinate(ix+u,image->width,s->repeat,&tx) &&
                coordinate(iy+v,image->height,s->repeat,&ty)){
                if(packed10_texel(p,w,f,raw,s,tx,ty,temp,temp,palette,front_texel))return -1;
                for(unsigned alpha=0;alpha<2;++alpha)
                    if(ps2_native_convolution_float_sample(w,f,temp+4*alpha,coefficient,out+4*alpha,palette,1,1))return -1;
            }
        }
        return ps2_native_convolution_float_end(w,f,out,out,palette,1,1) ||
            ps2_native_convolution_float_end(w,f,out+4,out+4,palette,1,1)?-1:0;
    }
    if(!s->bilinear){struct texel t;if(point(s,x,y,image->width,image->height,&t))return -1;
        if(t.x==UINT16_MAX)return float_constant(w->gs,out,0) || float_constant(w->gs,out+4,0)?-1:0;
        return packed10_texel(p,w,f,raw,s,t.x,t.y,out,temp,palette,front_texel);
    }
    int32_t fixed[3];if(mask_fixed(s,x,y,fixed))return -1;
    int64_t px=(int64_t)fixed[0]-32768,py=(int64_t)fixed[1]-32768,ix=px/65536,iy=py/65536;
    if(px<0 && px%65536)--ix;
    if(py<0 && py%65536)--iy;
    float fx=((uint32_t)px&65535)*(1.0f/65536),fy=((uint32_t)py&65535)*(1.0f/65536);
    const float weights[]={(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy};
    for(unsigned tap=0;tap<4;++tap){unsigned tx,ty;
        if(fixed[2] && coordinate(ix+(tap&1),image->width,s->repeat,&tx) && coordinate(iy+(tap>>1),image->height,s->repeat,&ty)){
            if(packed10_texel(p,w,f,raw,s,tx,ty,temp,temp,palette,front_texel))return -1;
        }else if(float_constant(w->gs,temp,0) || float_constant(w->gs,temp+4,0))return -1;
        if(float_constant(w->gs,temp+8,float_bits(weights[tap])))return -1;
        for(unsigned alpha=0;alpha<2;++alpha){
            if(ps2_native_float_multiply(w,f,temp+4*alpha,temp+8,temp+4*alpha,palette,1,1))return -1;
            if(tap){if(ps2_native_float_add(w,f,out+4*alpha,temp+4*alpha,out+4*alpha,palette,1,1,0))return -1;}
            else for(unsigned b=0;b<4;++b)if(ps2_rop_sample(w->gs,temp[4*alpha+b],out[4*alpha+b],0,0,0,0,1,1,0xffffff,PS2GS_TRANSFER_REPLACE,128))return -1;
        }
    }
    return 0;
}
static int packed10_composite(struct pipe_context *ctx,uint32_t source,uint32_t destination,
    const struct ps2_pipe_copy_box *box,const struct ps2_picture_sample *s,const struct ps2_picture_bitmap *mask,unsigned op)
{
    unsigned hsl_store=s && s->destination_alpha_map.enabled && mask && mask->component_alpha &&
        op>=PS2_COMPOSITE_HSL_HUE && op<=PS2_COMPOSITE_HSL_LUMINOSITY;
    if(op>=PS2_COMPOSITE_DISJOINT_CLEAR && op<=PS2_COMPOSITE_CONJOINT_DST){const unsigned simple[]={2,0,3,2,0,3};op=simple[op-PS2_COMPOSITE_DISJOINT_CLEAR];}
    if(op>=PS2_COMPOSITE_HSL_HUE && op<=PS2_COMPOSITE_HSL_LUMINOSITY && mask && mask->component_alpha)op=PS2_COMPOSITE_DST;
    if(!ctx || !box || op>PS2_COMPOSITE_HSL_LUMINOSITY || !packed10_sample_valid(s) || s->destination_packed10>PS2_PACKED10_BGR ||
        s->destination_argb>1 || s->destination_swap>1 || s->destination_rotate>1 || (s->destination_swap && s->destination_rotate) ||
        s->alpha_only_destination>1 ||
        (!s->destination_packed10 && !packed10_unorm_valid(&s->destination_unorm)) ||
        (s->destination_packed10 && s->alpha_only_destination) ||
        (s->destination_packed10 && (s->destination_swap || s->destination_rotate || s->destination_argb!=(s->destination_packed10<=PS2_PACKED10_ABGR))) ||
        (mask && (!mask->has_surface || mask->component_alpha>1 || !packed10_sample_valid(&mask->sample)))){errno=EINVAL;return -1;}
    if(ps2_pipe_context_acquire(ctx))return -1;
    struct ps2_picture_sample sample=*s;struct ps2_picture_bitmap copied_mask={0};
    s=&sample;if(mask){copied_mask=*mask;mask=&copied_mask;}
    struct ps2_render_client *r=ps2_pipe_context_render(ctx);struct ps2_surface_client *gs=r->gs;
    const struct ps2_surface *image=ps2_surface_lookup(gs,source),*target=ps2_surface_lookup(gs,destination),
        *mi=mask?ps2_surface_lookup(gs,mask->surface):NULL;
    int status=-1,error=EINVAL;uint32_t handles[37]={0},snapshot[2]={0},rows[2]={0},palette=0,front_texel=0;
    struct ps2_packed_workspace p={0};struct ps2_divide_workspace w={0};struct ps2_filter_workspace f={0};
    int64_t *positions=NULL,*sp=NULL,*mp=NULL;unsigned char *parameters=NULL;
    struct ps2_packed_layout dl=packed10_layout(s,1),sl=packed10_layout(s,0),ml=mask?packed10_layout(&mask->sample,0):sl;
    if(!image || !target || (image->bits_per_pixel!=16 && image->bits_per_pixel!=32) || (target->bits_per_pixel!=16 && target->bits_per_pixel!=32) ||
        image->width>1024 || image->height>1024 || target->width>1024 || target->height>1024 ||
        !packed10_storage_valid(gs,source,&sl) || !packed10_storage_valid(gs,destination,&dl) ||
        box->width<0 || box->height<0 || box->dx<0 || box->dy<0 || (int64_t)box->dx+box->width>target->width ||
        (int64_t)box->dy+box->height>target->height ||
        (mask && (!mi || (mi->bits_per_pixel!=16 && mi->bits_per_pixel!=32) || mi->width>1024 || mi->height>1024 || mask->width!=mi->width || mask->height!=mi->height ||
            !packed10_storage_valid(gs,mask->surface,&ml))))goto done;
    if(!packed10_read_map_valid(gs,&sample.source_alpha_map) || !packed10_read_map_valid(gs,&sample.destination_alpha_map) ||
        (mask && !packed10_read_map_valid(gs,&mask->sample.source_alpha_map)))goto done;
    const struct ps2_picture_alpha_map *map=&sample.destination_alpha_map;
    if(sample.destination_alias && (!map->enabled ||
        !ps2_format_alias_valid(sample.destination_alias,gs,destination,map->surface)))goto done;
    unsigned decoded_target=!destination || (!s->destination_packed10 && (s->alpha_only_destination ||
        dl.bits[0]<8 || dl.bits[1]<8 || dl.bits[2]<8 || (dl.bits[3] && dl.bits[3]<8)));
    struct ps2_picture_alpha_map parent={.surface=destination,.has_alpha=s->destination_argb,
        .alpha_only=s->alpha_only_destination,.alpha_bits=s->destination_unorm.alpha_bits,
        .rgb_bits={s->destination_unorm.rgb_bits[0],s->destination_unorm.rgb_bits[1],s->destination_unorm.rgb_bits[2]},
        .swap=s->destination_swap,.rotate=s->destination_rotate};
    if(map->enabled){
        int64_t x=(int64_t)box->dx-map->x,y=(int64_t)box->dy-map->y;
        if(x<0 || y<0 || x+box->width>map->width || y+box->height>map->height)goto done;
    }
    unsigned required=PS2GS_HW_SOLID_LOGIC|PS2GS_HW_CHANNEL_LUT|PS2GS_HW_CHANNEL_MAP|
        PS2GS_HW_CHANNEL_MAP_BLIT|PS2GS_HW_RGBA_TRANSFER|PS2GS_HW_TEXTURE_TRANSFER;
    if((gs->caps.features&required)!=required){error=ENOTSUP;goto done;}
    if(!box->width || !box->height || (op==PS2_COMPOSITE_DST && !hsl_store)){status=0;goto done;}
    /* Pixman's public operator table optimizes basic InReverse to an
     * explicit Dst when source and mask are known opaque. With a writable
     * map that optimization must suppress both iterator stores. */
    if(map->enabled && op==PS2_COMPOSITE_IN_REVERSE &&
        opaque_sample(s,box->sx,box->sy,box->width,box->height,image->width,image->height) &&
        (!mask || (!mask->component_alpha && opaque_sample(&mask->sample,mask->x,mask->y,
            box->width,box->height,mi->width,mi->height)))){status=0;goto done;}
    if((!s->source_solid && !ps2_gradient_extent_valid(s->matrix,s->transformed,box->sx,box->sy,box->width,box->height)) ||
        (mask && !mask->sample.source_solid && !ps2_gradient_extent_valid(mask->sample.matrix,mask->sample.transformed,mask->x,mask->y,box->width,box->height))){status=0;goto done;}
    if(op==PS2_COMPOSITE_CLEAR && !map->enabled){status=ps2_render_flush(r) || ps2_surface_solid_logic(gs,destination,box->dx,box->dy,box->width,box->height,0,
        ps2_surface_native_mask(gs,destination),3)?-1:0;error=errno;goto done;}
    uint64_t pixels=(uint64_t)box->width*box->height;
    unsigned source_params=sample.convolution && !sample.gradient.kind?sample.filter_nparams:0,
        mask_params=mask && mask->sample.convolution && !mask->sample.gradient.kind?mask->sample.filter_nparams:0;
    if((source_params && pixels*(source_params-2)>16777216) ||
        (mask_params && pixels*(mask_params-2)>16777216)){error=E2BIG;goto done;}
    if(source_params || mask_params){
        parameters=malloc((size_t)(source_params+mask_params)*4);
        if(!parameters){error=ENOMEM;goto done;}
        if(source_params){memcpy(parameters,sample.filter_params,(size_t)source_params*4);sample.filter_params=(void*)parameters;}
        if(mask_params){memcpy(parameters+(size_t)source_params*4,copied_mask.sample.filter_params,(size_t)mask_params*4);
            copied_mask.sample.filter_params=(void*)(parameters+(size_t)source_params*4);}
    }
    if(packed10_geometry(s,box->sx,box->sy,box->width,box->height,image->width,image->height) ||
        (mask && packed10_geometry(&mask->sample,mask->x,mask->y,box->width,box->height,mi->width,mi->height))){error=errno;goto done;}
    if(s->gradient.kind || (mask && mask->sample.gradient.kind)){
        positions=malloc((size_t)box->width*16);if(!positions){error=ENOMEM;goto done;}
        sp=positions;mp=positions+box->width;
        for(unsigned y=0;y<(unsigned)box->height;++y)
            if((s->gradient.kind && ps2_gradient_plan_row(&s->gradient,s->matrix,s->transformed,s->repeat,box->sx,box->sy,box->width,box->height,y,sp)) ||
                (mask && mask->sample.gradient.kind && ps2_gradient_plan_row(&mask->sample.gradient,mask->sample.matrix,mask->sample.transformed,mask->sample.repeat,mask->x,mask->y,box->width,box->height,y,mp))){error=errno;goto done;}
    }
    if(ps2_packed_reserve(&p,gs) || ps2_divide_reserve(&w,gs) || ps2_filter_reserve(&f,gs,3) ||
        ps2_surface_create(gs,16,16,32,&palette)){error=errno;goto done;}
    for(unsigned i=0;i<(map->enabled || decoded_target?37U:36U);++i)if(ps2_surface_create(gs,64,32,32,handles+i)){error=errno;goto done;}
    if(handles[36] && ps2_surface_set_depth(gs,handles[36],32)){error=errno;goto done;}
    if((!source || !destination || (mask && !mask->surface)) &&
        ps2_surface_create(gs,64,32,gs->front.bits_per_pixel,&front_texel)){error=errno;goto done;}
    if(map->enabled){
        for(unsigned i=0;i<2;++i)if((i || map->has_alpha) &&
            (ps2_surface_create(gs,box->width,1,32,rows+i) || ps2_surface_set_depth(gs,rows[i],32))){error=errno;goto done;}
    }
    uint32_t inputs[]={source,mask?mask->surface:0,sample.source_alpha_map.surface,mask?mask->sample.source_alpha_map.surface:0};
    unsigned active[]={1,!!mask,sample.source_alpha_map.enabled,mask?mask->sample.source_alpha_map.enabled:0};
    uint32_t targets[]={destination,map->surface};
    for(unsigned t=0;t<(map->enabled?2U:1U);++t){
        if(t && targets[t]==targets[0])continue;
        const struct ps2_surface *backing=ps2_surface_lookup(gs,targets[t]);unsigned aliased=0;
        for(unsigned i=0;i<4;++i)aliased|=active[i] && inputs[i]==targets[t];
        /* Opaque CT16 alpha-map-only readers are constant; colour readers
         * still need an immutable snapshot when their backing is written. */
        if(backing->bits_per_pixel==16){aliased=0;
            for(unsigned i=0;i<2;++i)aliased|=active[i] && inputs[i]==targets[t];}
        if(aliased && (ps2_surface_create(gs,backing->width,backing->height,backing->bits_per_pixel,snapshot+t) ||
            ps2_surface_set_depth(gs,snapshot[t],backing->bits_per_pixel==16?15:32))){error=errno;goto done;}
    }
    if(ps2_render_flush(r)){error=errno;goto done;}
    for(unsigned t=0;t<2;++t)if(snapshot[t]){
        const struct ps2_surface *backing=ps2_surface_lookup(gs,targets[t]);
        int copied;
        if(backing->bits_per_pixel==16 || !targets[t]){
            struct ps2_rop_workspace capture={.gs=gs};unsigned offset;
            copied=ps2_rop_capture(&capture,targets[t],snapshot[t],0,0,backing->width,backing->height,&offset);
        }else copied=ps2_surface_rgba_rectangle(gs,targets[t],snapshot[t],0,0,0,0,backing->width,backing->height,
            ps2_surface_native_mask(gs,targets[t]),PS2GS_RGBA_REPLACE);
        if(copied){error=errno;goto done;}
        for(unsigned i=0;i<4;++i)if(active[i] && inputs[i]==targets[t])inputs[i]=snapshot[t];
    }
    if(sample.source_alpha_map.enabled)sample.source_alpha_map.surface=inputs[2];
    if(mask && copied_mask.sample.source_alpha_map.enabled)copied_mask.sample.source_alpha_map.surface=inputs[3];
    status=0;
    for(unsigned y=0;!status && y<(unsigned)box->height;++y){
        struct ps2_picture_sample dest_sample={.source_argb=s->destination_argb,.source_swap=s->destination_swap,
            .source_rotate=s->destination_rotate,.source_packed10=s->destination_packed10,.source_unorm=s->destination_unorm};
        if(map->enabled){
            if(map->has_alpha){
                status=ps2_surface_rgba_rectangle(gs,map->surface,rows[0],box->dx-map->x,box->dy+y-map->y,
                    0,0,box->width,1,UINT32_MAX,PS2GS_RGBA_REPLACE);
                dest_sample.source_alpha_map=*map;dest_sample.source_alpha_map.surface=rows[0];
                dest_sample.source_alpha_map.width=box->width;dest_sample.source_alpha_map.height=1;
                dest_sample.source_alpha_map.x=box->dx;dest_sample.source_alpha_map.y=box->dy+y;
            }
        }
        if(!status && s->gradient.kind)status=ps2_gradient_plan_row(&s->gradient,s->matrix,s->transformed,s->repeat,box->sx,box->sy,box->width,box->height,y,sp);
        if(!status && mask && mask->sample.gradient.kind)status=ps2_gradient_plan_row(&mask->sample.gradient,mask->sample.matrix,mask->sample.transformed,mask->sample.repeat,mask->x,mask->y,box->width,box->height,y,mp);
        for(unsigned x=0;!status && x<(unsigned)box->width;++x){
            if(op!=PS2_COMPOSITE_DST)status=packed10_fetch(&p,&w,&f,inputs[0],s,box->sx+x,box->sy+y,s->gradient.kind?sp[x]:0,handles,handles+24,palette,front_texel);
            if(!status && op!=PS2_COMPOSITE_DST)status=mask?packed10_fetch(&p,&w,&f,inputs[1],&mask->sample,mask->x+x,mask->y+y,mask->sample.gradient.kind?mp[x]:0,handles+8,handles+24,palette,front_texel):
                float_constant(gs,handles+8,0x3f800000) || float_constant(gs,handles+12,0x3f800000)?-1:0;
            if(!status)status=packed10_texel(&p,&w,&f,destination,&dest_sample,box->dx+x,box->dy+y,handles+16,handles+24,palette,front_texel) ||
                (map->enabled && !map->has_alpha?float_constant(gs,handles+20,0x3f800000):0) ||
                (op==PS2_COMPOSITE_DST?0:op<=PS2_COMPOSITE_XOR?ps2_packed_blend_scratch(&w,&f,handles,handles+8,handles+16,handles+24,palette,1,1,op,mask?mask->component_alpha:0):
                    ps2_native_float_blend_consume(&w,&f,handles,handles+8,handles+16,handles+24,palette,1,1,op,mask?mask->component_alpha:0)) ||
                (map->enabled?packed10_map_store(&p,&w,&f,handles+16,map,handles[36],rows[1],x,0,palette):0) ||
                (!destination && target->bits_per_pixel==32?
                    packed10_map_store(&p,&w,&f,handles+16,&parent,handles[36],front_texel,0,0,palette) ||
                    ps2_rop_sample(gs,front_texel,0,0,0,box->dx+x,box->dy+y,1,1,0xffffff,PS2GS_TRANSFER_REPLACE,128):
                decoded_target?packed10_map_store(&p,&w,&f,handles+16,&parent,handles[36],destination,box->dx+x,box->dy+y,palette):
                    ps2_packed_store(&p,&w,&f,handles+16,destination,box->dx+x,box->dy+y,&dl,palette,1,1))?-1:0;
        }
        if(!status && sample.destination_alias)status=ps2_format_alias_parent(sample.destination_alias,box->dx,box->dy+y,box->width);
        if(!status && map->enabled)status=packed10_map_publish(gs,map,rows[1],handles[36],handles+24,palette,box->dx,box->dy+y,box->width);
        if(!status && sample.destination_alias)status=ps2_format_alias_map(sample.destination_alias,
            box->dx-map->x,box->dy+y-map->y,box->width,box->dy+y);
    }
    error=errno;
done:
    for(unsigned i=0;i<37;++i)if(handles[i] && !gs->error && ps2_surface_destroy(gs,handles[i])){if(!status)error=errno;status=-1;}
    for(unsigned i=0;i<2;++i){
        if(snapshot[i] && !gs->error && ps2_surface_destroy(gs,snapshot[i])){if(!status)error=errno;status=-1;}
        if(rows[i] && !gs->error && ps2_surface_destroy(gs,rows[i])){if(!status)error=errno;status=-1;}
    }
    if(front_texel && !gs->error && ps2_surface_destroy(gs,front_texel)){if(!status)error=errno;status=-1;}
    if(palette && !gs->error && ps2_surface_destroy(gs,palette)){if(!status)error=errno;status=-1;}
    if(f.gs && ps2_filter_release(&f) && !status){status=-1;error=errno;}
    if(w.gs && ps2_divide_release(&w) && !status){status=-1;error=errno;}
    if(p.gs && ps2_packed_release(&p) && !status){status=-1;error=errno;}
    free(parameters);free(positions);ps2_pipe_context_release(ctx);if(status)errno=error;return status;
}
