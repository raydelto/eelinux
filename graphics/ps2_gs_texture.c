/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "ps2_gs_texture.h"
#include <errno.h>
#include <sys/ioctl.h>
#include <time.h>
_Static_assert(sizeof(struct ps2gs_texture_vertex) == 28, "texture vertex ABI");
_Static_assert(sizeof(struct ps2gs_texture_triangle) == 100, "texture triangle ABI");
_Static_assert(sizeof(struct ps2gs_texture_submit) == 40, "texture submit ABI");
static int fail(struct ps2_render_client *r, int error)
{
    if (!r->gs->error) r->gs->error = error;
    r->count = 0; errno = r->gs->error; return -1;
}
int ps2_texture_front_region(struct ps2_render_client *r,
    const struct ps2gs_texture_triangle *triangles,unsigned count)
{
    unsigned left=UINT32_MAX,top=UINT32_MAX,right=0,bottom=0;
    for(unsigned i=0;i<count;++i)for(unsigned j=0;j<3;++j){
        unsigned x=triangles[i].v[j].position.x,y=triangles[i].v[j].position.y;
        if(x<left)left=x;
        if(y<top)top=y;
        if(x>right)right=x;
        if(y>bottom)bottom=y;
    }
    left/=16;top/=16;right=(right+15)/16;bottom=(bottom+15)/16;
    return right>left && bottom>top?
        ps2_surface_front_region(r->gs,r->target,left,top,right-left,bottom-top):0;
}
int ps2_texture_triangles(struct ps2_render_client *r, uint32_t texture_handle,
                          const struct ps2gs_texture_triangle *triangles, unsigned count)
{
    const struct ps2_surface *color = ps2_surface_lookup(r->gs, r->target);
    const struct ps2_surface *texture = texture_handle ? ps2_surface_lookup(r->gs, texture_handle) : NULL;
    if (r->gs->error) return fail(r, r->gs->error);
    if (!(r->gs->caps.features & PS2GS_HW_TEXTURE)) return fail(r, ENOTSUP);
    if (!color || !texture || color == texture || !triangles || !count || count > 64 ||
        texture->width > 1024 || texture->height > 1024) return fail(r, EINVAL);
    for (unsigned i = 0; i < count; ++i) {
        const struct ps2gs_texture_triangle *t = &triangles[i];
        if ((t->flags & ~(PS2GS_DEPTH_TEST | PS2GS_TEXTURE_MODULATE)) ||
            t->reserved[0] || t->reserved[1] || t->reserved[2] ||
            ((t->flags & PS2GS_DEPTH_TEST) && !r->depth)) return fail(r, EINVAL);
        for (unsigned j = 0; j < 3; ++j) {
            const struct ps2gs_texture_vertex *v = &t->v[j];
            if (v->q < 0x39800000 || v->q > 0x45800000 || v->s > v->q || v->t > v->q ||
                v->position.x > color->width * 16 || v->position.y > color->height * 16 ||
                v->position.z > 65535) return fail(r, EINVAL);
        }
    }
    if (ps2_render_flush(r)) return -1;
    struct ps2gs_texture_submit submit = { .abi = PS2GS_ABI_VERSION, .target = r->target,
        .depth = r->depth, .texture = texture_handle, .count = count, .width = color->width,
        .height = color->height, .bits_per_pixel = color->bits_per_pixel, .triangles = (uintptr_t)triangles };
    if (ps2_texture_front_region(r,triangles,count)) return fail(r, errno);
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        if (!ioctl(r->gs->fd, PS2GS_TEXTURE_SUBMIT, &submit)) return 0;
        if (errno != EAGAIN) return fail(r, errno);
        struct timespec delay = {.tv_nsec = 10000000};
        while (nanosleep(&delay, &delay)) if (errno != EINTR) return fail(r, errno);
    }
    return fail(r, EAGAIN);
}
