/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "ps2_gs_transfer.h"
#include "ps2-gs-lut.h"
#include <errno.h>
#include <sys/ioctl.h>
#include <time.h>
#include <string.h>
_Static_assert(sizeof(struct ps2gs_texture_transfer) == 48, "texture transfer ABI");
static int fail(struct ps2_render_client *r, int error)
{
    if (!r->gs->error) r->gs->error = error;
    r->count = 0; errno = r->gs->error; return -1;
}
int ps2_texture_transfer(struct ps2_render_client *r, uint32_t texture_handle,
    const struct ps2gs_texture_triangle *triangles, unsigned count, uint32_t mask, unsigned mode)
{
    if (r->gs->error) return fail(r, r->gs->error);
    const struct ps2_surface *color = ps2_surface_lookup(r->gs, r->target);
    const struct ps2_surface *texture = texture_handle ? ps2_surface_lookup(r->gs, texture_handle) : NULL;
    if (!(r->gs->caps.features & PS2GS_HW_TEXTURE_TRANSFER)) return fail(r, ENOTSUP);
    if (!color || !texture || color == texture || !triangles || !count || count > 64 ||
        r->depth || texture->width > 1024 || texture->height > 1024 ||
        mode > PS2GS_TRANSFER_ADD_CLAMP || (mask & ~(color->bits_per_pixel == 16 ? 0x7fffU : 0xffffffU)))
        return fail(r, EINVAL);
    if(mode==PS2GS_TRANSFER_ADD_CLAMP && !(r->gs->caps.features&PS2GS_HW_CHANNEL_LUT))return fail(r,ENOTSUP);
    for (unsigned i = 0; i < count; ++i) {
        const struct ps2gs_texture_triangle *t = triangles + i;
        if ((t->flags & ~PS2GS_TEXTURE_MODULATE) || t->reserved[0] || t->reserved[1] || t->reserved[2])
            return fail(r, EINVAL);
        for (unsigned j = 0; j < 3; ++j) {
            const struct ps2gs_texture_vertex *v = t->v + j;
            if (v->q < 0x39800000 || v->q > 0x45800000 || v->s > v->q || v->t > v->q ||
                v->position.x > color->width * 16 || v->position.y > color->height * 16 || v->position.z > 65535)
                return fail(r, EINVAL);
        }
    }
    if (!mask) return 0;
    if (ps2_render_flush(r) || ps2_texture_front_region(r,triangles,count)) return -1;
    struct ps2gs_texture_transfer request = {
        .draw = {.abi = PS2GS_ABI_VERSION, .target = r->target, .texture = texture_handle,
            .count = count, .width = color->width, .height = color->height,
            .bits_per_pixel = color->bits_per_pixel, .triangles = (uintptr_t)triangles},
        .plane_mask = mask, .mode = mode };
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        if (!ioctl(r->gs->fd, PS2GS_TEXTURE_TRANSFER, &request)) return 0;
        if (errno != EAGAIN) return fail(r, errno);
        struct timespec delay = {.tv_nsec = 10000000};
        while (nanosleep(&delay, &delay) < 0)
            if (errno != EINTR) return fail(r, errno);
    }
    return fail(r, EAGAIN);
}
int ps2_texture_rgba_transfer(struct ps2_render_client *r,uint32_t texture_handle,
    const struct ps2gs_texture_triangle *triangles,unsigned count,uint32_t mask,unsigned mode)
{
    if(r->gs->error)return fail(r,r->gs->error);
    const struct ps2_surface *d=ps2_surface_lookup(r->gs,r->target),*s=ps2_surface_lookup(r->gs,texture_handle);
    if(!(r->gs->caps.features&PS2GS_HW_RGBA_TRANSFER))return fail(r,ENOTSUP);
    if(!r->target || !texture_handle || !d || !s || d==s || d->bits_per_pixel!=32 || s->bits_per_pixel!=32 ||
        !triangles || !count || count>64 || r->depth || s->width>1024 || s->height>1024 ||
        mode>PS2GS_RGBA_MULTIPLY || (mode==PS2GS_RGBA_MULTIPLY && (mask&0xff000000)))return fail(r,EINVAL);
    for(unsigned i=0;i<count;++i){const struct ps2gs_texture_triangle *t=triangles+i;
        if(t->flags || t->reserved[0] || t->reserved[1] || t->reserved[2])return fail(r,EINVAL);
        for(unsigned j=0;j<3;++j){const struct ps2gs_texture_vertex *v=t->v+j;
            if(v->q<0x39800000 || v->q>0x45800000 || v->s>v->q || v->t>v->q ||
                v->position.x>d->width*16 || v->position.y>d->height*16 || v->position.z>65535)return fail(r,EINVAL);}}
    if(!mask)return 0;
    if(ps2_render_flush(r))return -1;
    struct ps2gs_texture_transfer request={.draw={.abi=PS2GS_ABI_VERSION,.target=r->target,.texture=texture_handle,
        .count=count,.width=d->width,.height=d->height,.bits_per_pixel=32,.triangles=(uintptr_t)triangles},
        .plane_mask=mask,.mode=mode};
    for(unsigned attempt=0;attempt<100;++attempt){
        if(!ioctl(r->gs->fd,PS2GS_TEXTURE_RGBA_TRANSFER,&request))return 0;
        if(errno!=EAGAIN)return fail(r,errno);
        struct timespec delay={.tv_nsec=10000000};
        while(nanosleep(&delay,&delay)<0)if(errno!=EINTR)return fail(r,errno);
    }
    return fail(r,EAGAIN);
}
static uint32_t bits(float value){uint32_t result;memcpy(&result,&value,4);return result;}
static unsigned padded(unsigned n){unsigned p=1;while(p<n)p*=2;return p;}
int ps2_surface_rgba_rectangle(struct ps2_surface_client *gs,uint32_t source,uint32_t destination,
    unsigned sx,unsigned sy,unsigned dx,unsigned dy,unsigned width,unsigned height,uint32_t mask,unsigned mode)
{
    const struct ps2_surface *s=ps2_surface_lookup(gs,source),*d=ps2_surface_lookup(gs,destination);
    if(!source || !destination || !s || !d || s==d || !width || !height || sx>=s->width || sy>=s->height ||
        dx>=d->width || dy>=d->height || width>s->width-sx || height>s->height-sy ||
        width>d->width-dx || height>d->height-dy){errno=EINVAL;return -1;}
    if(s->bits_per_pixel!=32 || d->bits_per_pixel!=32 || s->width>1024 || s->height>1024 ||
        mode>PS2GS_RGBA_MULTIPLY || (mode==PS2GS_RGBA_MULTIPLY && (mask&0xff000000))){errno=EINVAL;return -1;}
    /* A quarter texel keeps edge- and centre-sampling renderers inside the
     * same nearest texel even after GS interpolation rounding. A trailing
     * edge at the padded texture limit needs its own constant-texel strip;
     * no S/T coordinate may exceed Q in the typed kernel ABI. */
    if(width>1 && sx+width==padded(s->width))
        return ps2_surface_rgba_rectangle(gs,source,destination,sx,sy,dx,dy,width-1,height,mask,mode) ||
            ps2_surface_rgba_rectangle(gs,source,destination,sx+width-1,sy,dx+width-1,dy,1,height,mask,mode)?-1:0;
    if(height>1 && sy+height==padded(s->height))
        return ps2_surface_rgba_rectangle(gs,source,destination,sx,sy,dx,dy,width,height-1,mask,mode) ||
            ps2_surface_rgba_rectangle(gs,source,destination,sx,sy+height-1,dx,dy+height-1,width,1,mask,mode)?-1:0;
    unsigned x[4]={dx,dx+width,dx+width,dx},y[4]={dy,dy,dy+height,dy+height};
    float left=sx+.25f,right=left+(width==1?.5f:width),top=sy+.25f,bottom=top+(height==1?.5f:height);
    float u[4]={left,right,right,left},v[4]={top,top,bottom,bottom};
    static const unsigned indices[2][3]={{0,1,2},{0,2,3}};
    struct ps2gs_texture_triangle triangles[2]={0};
    for(unsigned i=0;i<2;++i)for(unsigned j=0;j<3;++j){unsigned k=indices[i][j];
        triangles[i].v[j]=(struct ps2gs_texture_vertex){
            .position={.x=x[k]*16,.y=y[k]*16,.rgba=0x80808080},
            .s=bits(u[k]/padded(s->width)),.t=bits(v[k]/padded(s->height)),.q=0x3f800000};}
    struct ps2_render_client temporary={.gs=gs,.target=destination};
    return ps2_texture_rgba_transfer(&temporary,source,triangles,2,mask,mode);
}
