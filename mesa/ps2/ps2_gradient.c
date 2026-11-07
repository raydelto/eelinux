/* SPDX-License-Identifier: MIT */
/* Gradient conventions follow pixman, whose relevant sources carry:
 * Copyright 2000 SuSE, Inc.; Copyright 2007 Red Hat, Inc.;
 * Copyright 2000 Keith Packard, member of The XFree86 Project, Inc.;
 * Copyright 2005 Lars Knoll & Zack Rusin, Trolltech.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Keith Packard not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission. Keith Packard makes no representations
 * about the suitability of this software for any purpose. It is provided
 * "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */
#include "ps2_gradient.h"
#include "ps2_gs_logic.h"
#include "ps2_gs_lut.h"
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <pixman.h>
#include <string.h>

int ps2_gradient_valid(const struct ps2_gradient *g)
{
    if(!g || (g->kind<1 || g->kind>3) || g->exclude_opaque>1 || !g->stops || !g->nstops || g->nstops>4096)return 0;
    for(unsigned i=0;i<g->nstops;++i)
        if(g->stops[i].x<0 || g->stops[i].x>65536 || (i && g->stops[i].x<g->stops[i-1].x))return 0;
    if(g->kind==3)return 1;
    int64_t dx=(int64_t)g->points[2]-g->points[0],dy=(int64_t)g->points[3]-g->points[1];
    if(g->kind==2 && (g->radii[0]<0 || g->radii[1]<0 || dx<INT32_MIN || dx>INT32_MAX || dy<INT32_MIN || dy>INT32_MAX))return 0;
    uint64_t ax=dx<0?-dx:dx,ay=dy<0?-dy:dy;
    return ax*ax<=INT64_MAX && ay*ay<=INT64_MAX-ax*ax;
}
static int fixed64(double v,int64_t *out)
{
    /* Repeat interval endpoints can extend two periods past the ordinate. */
    if(!isfinite(v) || v<= (double)(INT64_MIN+131072) || v>= (double)(INT64_MAX-131072)){errno=ERANGE;return -1;}
    *out=(int64_t)v;return 0;
}
static int dot(int64_t dx,int64_t dy,int32_t x,int32_t y,int64_t *out)
{
    int64_t a=dx*x,b=dy*y;
    if((b>0 && a>INT64_MAX-b) || (b<0 && a<INT64_MIN-b)){errno=ERANGE;return -1;}
    *out=a+b;return 0;
}
static int linear_row(const struct ps2_gradient *g,const int32_t matrix[3][3],
    unsigned transformed,int x,int y,unsigned width,unsigned height,unsigned row,int64_t *positions)
{
    if(!ps2_gradient_valid(g) || !positions || !width || !height || row>=height || transformed>1 ||
        (transformed && !matrix) || x< -32768 || (int64_t)x+width>32767 || y< -32768 ||
        (int64_t)y+height>32767){errno=EINVAL;return -1;}
    int64_t dx=(int64_t)g->points[2]-g->points[0],dy=(int64_t)g->points[3]-g->points[1];
    int64_t length=dx*dx+dy*dy;
    int32_t unit[3]={65536,0,0};
    if(transformed)for(unsigned i=0;i<3;++i)unit[i]=matrix[i][0];
    /* Match the reference's horizontal iterator reuse, including its
     * sub-epsilon vertical variation test over the complete request. */
    unsigned horizontal=0;
    if(length && (!transformed || (!matrix[2][0] && !matrix[2][1] && matrix[2][2]))){
        int64_t vertical;
        if(dot(dx,dy,transformed?matrix[0][1]:0,transformed?matrix[1][1]:65536,&vertical))return -1;
        double vz=transformed?matrix[2][2]:65536;
        double inc=height*65536.0*65536.0*vertical/(vz*(double)length);
        horizontal=inc> -1 && inc<1;
    }
    pixman_vector_t v={.vector={x*65536+32768,(int32_t)(((int64_t)y+(horizontal?0:(int64_t)row))*65536+32768),65536}};
    if(transformed && !pixman_transform_point_3d((const pixman_transform_t*)matrix,&v)){errno=ERANGE;return -1;}
    int64_t anchor,step;
    if(dot(dx,dy,g->points[0],g->points[1],&anchor) || dot(dx,dy,unit[0],unit[1],&step))return -1;
    if(!length || !unit[2]){
        double inc=0;int64_t first=0;
        if(length && v.vector[2]){
            double inv=65536.0*65536.0/(length*(double)v.vector[2]),v2=v.vector[2]*(1.0/65536.0);
            int64_t value;if(dot(dx,dy,v.vector[0],v.vector[1],&value))return -1;
            double t=(value-anchor*v2)*inv;
            if(fixed64(t,&first))return -1;
            inc=step*inv;
        }
        int64_t total;if(fixed64(inc*width,&total))return -1;
        for(unsigned i=0;i<width;++i){int64_t offset=0;
            if(total && fixed64(inc*i,&offset))return -1;
            if((offset>0 && first>INT64_MAX-offset) || (offset<0 && first<INT64_MIN-offset)){errno=ERANGE;return -1;}
            positions[i]=first+offset;
        }
    }else{
        double t=0;
        for(unsigned i=0;i<width;++i){
            if(v.vector[2]){
                double inv=65536.0*65536.0/(length*(double)v.vector[2]),v2=v.vector[2]*(1.0/65536.0);
                int64_t value;if(dot(dx,dy,v.vector[0],v.vector[1],&value))return -1;
                t=(value-anchor*v2)*inv;
            }
            if(fixed64(t,positions+i))return -1;
            if(i+1<width)for(unsigned c=0;c<3;++c){int64_t next=(int64_t)v.vector[c]+unit[c];
                if(next<INT32_MIN || next>INT32_MAX){errno=ERANGE;return -1;}v.vector[c]=next;
            }
        }
    }
    return 0;
}

static int add_geometry(int64_t a,int64_t b,int64_t *out)
{
    if((b>0 && a>INT64_MAX-b) || (b<0 && a<INT64_MIN-b)){errno=ERANGE;return -1;}
    *out=a+b;return 0;
}
static int multiply_geometry(int64_t a,int64_t b,int64_t *out)
{
    if((a>0 && ((b>0 && a>INT64_MAX/b) || (b<0 && b<INT64_MIN/a))) ||
        (a<0 && ((b>0 && a<INT64_MIN/b) || (b<0 && a<INT64_MAX/b)))){errno=ERANGE;return -1;}
    *out=a*b;return 0;
}
static int dot3(int64_t x,int64_t y,int64_t z,int64_t a,int64_t b,int64_t c,int64_t *out)
{
    int64_t first,second,third;
    return multiply_geometry(x,a,&first) || multiply_geometry(y,b,&second) || multiply_geometry(z,c,&third) ||
        add_geometry(first,second,&first) || add_geometry(first,third,out)?-1:0;
}
int ps2_gradient_opaque(const struct ps2_gradient *g)
{
    if(!ps2_gradient_valid(g) || g->exclude_opaque)return 0;
    for(unsigned i=0;i<g->nstops;++i)if(g->stops[i].rgba[3]!=65535)return 0;
    if(g->kind==2){int64_t dx=(int64_t)g->points[2]-g->points[0],dy=(int64_t)g->points[3]-g->points[1],
            dr=(int64_t)g->radii[1]-g->radii[0],a;
        if(dot3(dx,dy,-dr,dx,dy,dr,&a) || a>=0)return 0;
    }
    return 1;
}
/* Solve only circle membership and the gradient coordinate. An absent root
 * is an absent source sample, represented by INT64_MIN, and cleared on GS. */
static int radial_parameter(double a,double b,double c,double inva,double dr,double mindr,unsigned repeat,int64_t *out)
{
    double roots[2];unsigned count=0;*out=INT64_MIN;
    if(a==0){if(b==0)return 0;roots[count++]=32768.0*c/b;}
    else{double discriminant=b*b+a*(-c)+0.0;
        if(discriminant<0)return 0;
        double root=sqrt(discriminant);roots[count++]=(b+root)*inva;roots[count++]=(b-root)*inva;
    }
    for(unsigned i=0;i<count;++i)if(repeat?roots[i]*dr>=mindr:roots[i]>=0 && roots[i]<=65536)
        return fixed64(roots[i],out);
    return 0;
}
int ps2_gradient_extent_valid(const int32_t matrix[3][3],unsigned transformed,
    int x,int y,unsigned width,unsigned height)
{
    if(transformed>1 || (transformed && !matrix) || !width || !height ||
        (int64_t)x-1<INT16_MIN || (int64_t)y-1<INT16_MIN ||
        (int64_t)x+width+1>INT16_MAX || (int64_t)y+height+1>INT16_MAX)return 0;
    if(!transformed)return 1;
    for(unsigned i=0;i<4;++i){pixman_vector_t v={.vector={
        (int32_t)((i&1)?(int64_t)x*65536+32768:((int64_t)x+width)*65536-32768),
        (int32_t)((i&2)?(int64_t)y*65536+32768:((int64_t)y+height)*65536-32768),65536}};
        if(!pixman_transform_point((const pixman_transform_t*)matrix,&v))return 0;
    }
    return 1;
}
int ps2_gradient_plan_row(const struct ps2_gradient *g,const int32_t matrix[3][3],
    unsigned transformed,unsigned repeat,int x,int y,unsigned width,unsigned height,unsigned row,int64_t *positions)
{
    if(!ps2_gradient_valid(g) || !positions || !width || !height || row>=height || transformed>1 || repeat>3 ||
        (transformed && !matrix) || x< -32768 || (int64_t)x+width>32767 || y< -32768 || (int64_t)y+height>32767){errno=EINVAL;return -1;}
    if(!ps2_gradient_extent_valid(matrix,transformed,x,y,width,height)){
        for(unsigned i=0;i<width;++i)positions[i]=INT64_MIN;
        return 0;
    }
    if(g->kind==1)return linear_row(g,matrix,transformed,x,y,width,height,row,positions);
    pixman_vector_t v={.vector={x*65536+32768,(int32_t)(((int64_t)y+row)*65536+32768),65536}};
    int32_t unit[3]={65536,0,0};
    if(transformed){if(!pixman_transform_point_3d((const pixman_transform_t*)matrix,&v)){errno=ERANGE;return -1;}
        for(unsigned c=0;c<3;++c)unit[c]=matrix[c][0];}
    unsigned affine=!unit[2] && v.vector[2]==65536;
    if(g->kind==3){
        double rx=v.vector[0]/65536.0,ry=v.vector[1]/65536.0,rz=v.vector[2]/65536.0,
            cx=unit[0]/65536.0,cy=unit[1]/65536.0,cz=unit[2]/65536.0;
        int32_t degrees=g->angle%(360*65536);if(degrees<0)degrees+=360*65536;
        const double pi=3.14159265358979323846;
        double angle=((degrees/65536.0)/180.0)*pi;
        if(affine){rx-=g->points[0]/65536.0;ry-=g->points[1]/65536.0;}
        for(unsigned i=0;i<width;++i){double px=rx,py=ry;
            if(!affine){px=rz?rx/rz:0;py=rz?ry/rz:0;px-=g->points[0]/65536.0;py-=g->points[1]/65536.0;}
            double t=atan2(py,px)+angle;
            while(t<0)t+=2*pi;
            while(t>=2*pi)t-=2*pi;
            t=1-t*(1/(2*pi));
            if(fixed64(t*65536,positions+i))return -1;
            rx+=cx;ry+=cy;if(!affine)rz+=cz;
        }
        return 0;
    }
    int64_t dx=(int64_t)g->points[2]-g->points[0],dy=(int64_t)g->points[3]-g->points[1],
        dr=(int64_t)g->radii[1]-g->radii[0],integer_a;
    if(dot3(dx,dy,-dr,dx,dy,dr,&integer_a))return -1;
    double a=integer_a,inva=a?65536.0/a:0,mindr=-65536.0*g->radii[0];
    if(affine){
        int64_t px=(int64_t)v.vector[0]-g->points[0],py=(int64_t)v.vector[1]-g->points[1];
        if(px<INT32_MIN || px>INT32_MAX || py<INT32_MIN || py>INT32_MAX){errno=ERANGE;return -1;}
        int64_t b,db,c,dc,ddc;
        if(dot3(px,py,g->radii[0],dx,dy,dr,&b) || dot3(unit[0],unit[1],0,dx,dy,0,&db) ||
            dot3(px,py,-(int64_t)g->radii[0],px,py,g->radii[0],&c) ||
            dot3(2*px+unit[0],2*py+unit[1],0,unit[0],unit[1],0,&dc) ||
            dot3(unit[0],unit[1],0,unit[0],unit[1],0,&ddc) || multiply_geometry(ddc,2,&ddc))return -1;
        for(unsigned i=0;i<width;++i){
            if(radial_parameter(a,b,c,inva,dr,mindr,repeat,positions+i))return -1;
            if(i+1<width && (add_geometry(b,db,&b) || add_geometry(c,dc,&c) || add_geometry(dc,ddc,&dc)))return -1;
        }
    }else{
        for(unsigned i=0;i<width;++i){positions[i]=INT64_MIN;
            if(v.vector[2]){double inv=65536.0/v.vector[2],px=v.vector[0]*inv-g->points[0],py=v.vector[1]*inv-g->points[1];
                double b=px*dx+py*dy+g->radii[0]*(double)dr,c=px*px+py*py+(-g->radii[0])*(double)g->radii[0];
                if(radial_parameter(a,b,c,inva,dr,mindr,repeat,positions+i))return -1;
            }
            if(i+1<width)for(unsigned c=0;c<3;++c){int64_t next=(int64_t)v.vector[c]+unit[c];
                if(next<INT32_MIN || next>INT32_MAX){errno=ERANGE;return -1;}v.vector[c]=next;}
        }
    }
    return 0;
}
static int constant(struct ps2_surface_client *gs,const uint32_t out[4],const float value[3])
{
    uint32_t bits[3];memcpy(bits,value,sizeof(bits));
    for(unsigned byte=0;byte<4;++byte){uint32_t rgb=0;
        for(unsigned c=0;c<3;++c)rgb|=((bits[c]>>(8*byte))&255)<<(8*c);
        if(ps2_surface_solid_logic(gs,out[byte],0,0,1,1,rgb,0xffffff,3))return -1;
    }
    return 0;
}
/* Uniform interval coefficients. The ordinate is never combined with a
 * colour here: that product and every sample-dependent colour sum use GS. */
static void program(const struct ps2_gradient *g,unsigned repeat,int64_t pos,float slope[4],float base[4])
{
    struct ps2_gradient_stop before={.x=INT32_MIN},after={.x=INT32_MAX};
    if(repeat==1){before=g->stops[g->nstops-1];before.x-=65536;after=g->stops[0];after.x+=65536;}
    else if(repeat==3){before=g->stops[0];before.x=-before.x;after=g->stops[g->nstops-1];after.x=131072-after.x;}
    else if(repeat==2){memcpy(before.rgba,g->stops[0].rgba,sizeof(before.rgba));memcpy(after.rgba,g->stops[g->nstops-1].rgba,sizeof(after.rgba));}
    int64_t x=pos;
    if(repeat==1 || repeat==3){x=(uint32_t)pos&65535;if(repeat==3 && ((uint32_t)pos&65536))x=65536-x;}
    unsigned n=0;while(n<g->nstops && x>=g->stops[n].x)++n;
    const struct ps2_gradient_stop *left=n?g->stops+n-1:&before,*right=n<g->nstops?g->stops+n:&after;
    int64_t lx=left->x,rx=right->x;
    if(repeat==1){lx+=pos-x;rx+=pos-x;}
    else if(repeat==3){
        if((uint32_t)pos&65536){int64_t t=65536-rx;rx=65536-lx;lx=t;
            const struct ps2_gradient_stop *tmp=left;left=right;right=tmp;x=65536-x;}
        lx+=pos-x;rx+=pos-x;
    }else if(!repeat){if(!n)right=left;else if(n==g->nstops)left=right;}
    float lxf=lx*(1.0f/65536.0f),rxf=rx*(1.0f/65536.0f);
    for(unsigned c=0;c<4;++c){float l=left->rgba[c]*(1.0f/257.0f),r=right->rgba[c]*(1.0f/257.0f);
        if(rxf-lxf==0.0f || lx==INT32_MIN || rx==INT32_MAX){slope[c]=0; base[c]=(l+r)/510.0f;}
        else{float rec=1.0f/(rxf-lxf);base[c]=(l*rxf-r*lxf)*rec*(1.0f/255.0f);slope[c]=(r-l)*rec*(1.0f/255.0f);}
    }
}
int ps2_gradient_fetch(struct ps2_divide_workspace *w,struct ps2_filter_workspace *f,
    const struct ps2_gradient *g,unsigned repeat,int64_t position,
    const uint32_t rgb[4],const uint32_t alpha[4],uint32_t rgb_byte,uint32_t alpha_byte,uint32_t palette,unsigned wide)
{
    if(!w || !f || !w->gs || f->gs!=w->gs || !ps2_gradient_valid(g) || repeat>3 || !rgb || !alpha || wide>1 ||
        (!wide && (!rgb_byte || !alpha_byte || rgb_byte==alpha_byte)) ||
        (position!=INT64_MIN && (position<INT64_MIN+131072 || position>INT64_MAX-131072))){errno=EINVAL;return -1;}
    if(f->bytes!=3 && f->bytes!=4){errno=EINVAL;return -1;}
    const struct ps2_surface *pal=ps2_surface_lookup(w->gs,palette);
    if(!palette || !pal || pal->bits_per_pixel!=32 || pal->width!=16 || pal->height!=16){errno=EINVAL;return -1;}
    uint32_t private[]={w->remainder[0],w->remainder[1],w->remainder[2],w->remainder[3],
        w->shifted[0],w->shifted[1],w->shifted[2],w->shifted[3],
        w->difference[0],w->difference[1],w->difference[2],w->difference[3],
        w->borrow,w->carry,w->saturated,w->term,w->temporary,w->selected,w->retained,
        f->tap,f->term,f->sum[0],f->sum[1],f->sum[2],f->saturated,f->carry,f->sum[3]};
    unsigned count=sizeof(private)/sizeof(*private)-(f->bytes==3);
    for(unsigned i=0;i<count;++i){const struct ps2_surface *s=ps2_surface_lookup(w->gs,private[i]);
        if(!private[i] || !s || s->bits_per_pixel!=32 || s->width!=64 || s->height!=32 || private[i]==palette){errno=EINVAL;return -1;}
        for(unsigned j=0;j<i;++j)if(private[i]==private[j]){errno=EINVAL;return -1;}
    }
    uint32_t handles[10];memcpy(handles,rgb,16);memcpy(handles+4,alpha,16);handles[8]=rgb_byte;handles[9]=alpha_byte;
    for(unsigned i=0;i<(wide?8U:10U);++i){const struct ps2_surface *s=ps2_surface_lookup(w->gs,handles[i]);
        if(!handles[i] || !s || s->bits_per_pixel!=32 || s->width!=64 || s->height!=32 || handles[i]==palette){errno=EINVAL;return -1;}
        for(unsigned j=0;j<i;++j)if(handles[i]==handles[j]){errno=EINVAL;return -1;}
        for(unsigned j=0;j<count;++j)if(handles[i]==private[j]){errno=EINVAL;return -1;}
    }
    unsigned required=PS2GS_HW_SOLID_LOGIC|PS2GS_HW_CHANNEL_LUT|PS2GS_HW_CHANNEL_MAP|PS2GS_HW_TEXTURE_TRANSFER|PS2GS_HW_RGBA_TRANSFER;
    if((w->gs->caps.features&required)!=required){errno=ENOTSUP;return -1;}
    if(position==INT64_MIN){
        const float zero[3]={0,0,0};
        if(constant(w->gs,rgb,zero) || constant(w->gs,alpha,zero))return -1;
        return wide?0:ps2_surface_solid_logic(w->gs,rgb_byte,0,0,1,1,0,0xffffff,3) ||
            ps2_surface_solid_logic(w->gs,alpha_byte,0,0,1,1,0,0xffffff,3)?-1:0;
    }
    float slopes[4],bases[4];program(g,repeat,position,slopes,bases);
    float ordinate=(float)position*(1.0f/65536.0f),y[3]={ordinate,ordinate,ordinate},a[3]={slopes[3],slopes[3],slopes[3]};
    uint32_t scratch[4]={0};int result=-1,error=0;
    for(unsigned i=0;i<4;++i)if(ps2_surface_create(w->gs,64,32,32,scratch+i))goto done;
    if(constant(w->gs,rgb,slopes) || constant(w->gs,scratch,y) ||
        ps2_native_float_signed(w,f,rgb,scratch,rgb,palette,1,1,PS2_FLOAT_MULTIPLY) ||
        constant(w->gs,scratch,bases) || ps2_native_float_signed(w,f,rgb,scratch,rgb,palette,1,1,PS2_FLOAT_ADD) ||
        constant(w->gs,alpha,a) || constant(w->gs,scratch,y) ||
        ps2_native_float_signed(w,f,alpha,scratch,alpha,palette,1,1,PS2_FLOAT_MULTIPLY))goto done;
    for(unsigned c=0;c<3;++c)a[c]=bases[3];
    if(constant(w->gs,scratch,a) || ps2_native_float_signed(w,f,alpha,scratch,alpha,palette,1,1,PS2_FLOAT_ADD))goto done;
    if(!wide){for(unsigned c=0;c<3;++c)a[c]=255;
        if(constant(w->gs,scratch,a) || ps2_native_float_signed(w,f,alpha,scratch,alpha,palette,1,1,PS2_FLOAT_MULTIPLY))goto done;
    }
    if(ps2_native_float_signed(w,f,rgb,alpha,rgb,palette,1,1,PS2_FLOAT_MULTIPLY))goto done;
    if(!wide){for(unsigned c=0;c<3;++c)a[c]=.5f;
        if(constant(w->gs,scratch,a) || ps2_native_float_signed(w,f,rgb,scratch,rgb,palette,1,1,PS2_FLOAT_ADD) ||
            ps2_native_float_signed(w,f,alpha,scratch,alpha,palette,1,1,PS2_FLOAT_ADD) ||
            ps2_native_float_byte(w,f,rgb,rgb_byte,palette,1,1) || ps2_native_float_byte(w,f,alpha,alpha_byte,palette,1,1))goto done;
    }
    result=0;
done:
    error=errno;
    for(unsigned i=0;i<4;++i)if(scratch[i] && !w->gs->error && ps2_surface_destroy(w->gs,scratch[i])){if(!result)error=errno;result=-1;}
    errno=error;return result;
}
