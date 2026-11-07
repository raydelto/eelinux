/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "ps2_gs_render.h"
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>

_Static_assert(sizeof(struct ps2gs_depth_create) == 32, "depth ABI");
_Static_assert(sizeof(struct ps2gs_render_submit) == 40, "render ABI");
static int fail(struct ps2_render_client *r, int error)
{
    if (!r->gs->error) r->gs->error = error;
    r->count = 0;
    errno = r->gs->error;
    return -1;
}
static int request(struct ps2_render_client *r, unsigned long op, void *data)
{
    if (r->gs->error) return fail(r, r->gs->error);
    if (r->gs->fd < 0) return fail(r, EBADF);
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        if (!ioctl(r->gs->fd, op, data)) return 0;
        if (errno != EAGAIN) return fail(r, errno);
        struct timespec delay = { .tv_nsec = 10000000 };
        while (nanosleep(&delay, &delay) < 0)
            if (errno != EINTR) return fail(r, errno);
    }
    return fail(r, EAGAIN);
}
static struct ps2_depth *depth_lookup(struct ps2_render_client *r, uint32_t handle)
{
    if (!handle) return NULL;
    for (unsigned i = 0; i < PS2GS_MAX_SURFACES; ++i)
        if (r->depths[i].handle == handle) return &r->depths[i];
    return NULL;
}
int ps2_render_init(struct ps2_render_client *r, struct ps2_surface_client *gs)
{
    if (!r || !gs) { errno = EINVAL; return -1; }
    memset(r, 0, sizeof(*r)); r->gs = gs;
    if (gs->fd < 0 || gs->error) return fail(r, gs->error ? gs->error : EBADF);
    if (!(gs->caps.features & PS2GS_HW_OWNED_RENDER)) return fail(r, ENOTSUP);
    return 0;
}
int ps2_render_flush(struct ps2_render_client *r)
{
    if (r->gs->error) return fail(r, r->gs->error);
    if (!r->count) return 0;
    const struct ps2_surface *color = ps2_surface_lookup(r->gs, r->target);
    if (!color || (r->depth && !depth_lookup(r, r->depth))) return fail(r, EINVAL);
    struct ps2gs_render_submit submit = { .abi = PS2GS_ABI_VERSION,
        .target = r->target, .depth = r->depth, .count = r->count,
        .width = color->width, .height = color->height, .bits_per_pixel = color->bits_per_pixel,
        .commands = (uintptr_t)r->commands };
    unsigned left=color->width,top=color->height,right=0,bottom=0;
    for(unsigned i=0;i<r->count;++i){
        const struct ps2gs_command *c=r->commands+i;
        unsigned x,y,x2,y2;
        if(c->op==PS2GS_CLEAR_DEPTH)continue;
        if(c->op==PS2GS_FILL){x=c->v[0].x/16;y=c->v[0].y/16;x2=x+c->width;y2=y+c->height;}
        else{
            x=x2=c->v[0].x;y=y2=c->v[0].y;
            for(unsigned j=1;j<3;++j){
                if(c->v[j].x<x)x=c->v[j].x;
                if(c->v[j].y<y)y=c->v[j].y;
                if(c->v[j].x>x2)x2=c->v[j].x;
                if(c->v[j].y>y2)y2=c->v[j].y;
            }
            x/=16;y/=16;x2=(x2+15)/16;y2=(y2+15)/16;
        }
        if(x<left)left=x;
        if(y<top)top=y;
        if(x2>right)right=x2;
        if(y2>bottom)bottom=y2;
    }
    if(right>left && bottom>top &&
       ps2_surface_front_region(r->gs,r->target,left,top,right-left,bottom-top))return fail(r,errno);
    if (request(r, PS2GS_RENDER_SUBMIT, &submit)) return -1;
    r->count = 0;
    return 0;
}
int ps2_render_depth_create(struct ps2_render_client *r, unsigned width, unsigned height,
                            unsigned bpp, uint32_t *handle)
{
    if (handle) *handle = 0;
    if (!handle || !width || !height || width > 2048 || height > 2048 ||
        (bpp != 16 && bpp != 32)) return fail(r, EINVAL);
    if (ps2_render_flush(r)) return -1;
    unsigned slot;
    for (slot = 0; slot < PS2GS_MAX_SURFACES && r->depths[slot].handle; ++slot) {}
    if (slot == PS2GS_MAX_SURFACES) return fail(r, ENOSPC);
    struct ps2gs_depth_create c = { .abi = PS2GS_ABI_VERSION,
        .width = width, .height = height, .color_bits_per_pixel = bpp };
    if (request(r, PS2GS_DEPTH_CREATE, &c)) return -1;
    unsigned bw = (width + 63) & ~63U, rows = (height + 63) & ~63U;
    if (!c.handle || ps2_surface_lookup(r->gs, c.handle) || depth_lookup(r, c.handle) ||
        c.abi != PS2GS_ABI_VERSION || c.width != width || c.height != height ||
        c.color_bits_per_pixel != bpp || c.reserved || c.buffer_width != bw ||
        c.allocated_bytes != bw * rows * 2 || c.allocated_bytes > 4U * 1024 * 1024)
        return fail(r, EPROTO);
    r->depths[slot] = (struct ps2_depth) { .handle = c.handle, .width = width,
        .height = height, .color_bpp = bpp, .buffer_width = bw, .bytes = c.allocated_bytes };
    *handle = c.handle;
    return 0;
}
int ps2_render_depth_destroy(struct ps2_render_client *r, uint32_t handle)
{
    struct ps2_depth *d = depth_lookup(r, handle);
    if (!d) return fail(r, EINVAL);
    if (ps2_render_flush(r)) return -1;
    struct ps2gs_surface_destroy destroy = { .abi = PS2GS_ABI_VERSION, .handle = handle };
    if (request(r, PS2GS_SURFACE_DESTROY, &destroy)) return -1;
    memset(d, 0, sizeof(*d));
    if (r->depth == handle) r->depth = 0;
    return 0;
}
int ps2_render_bind(struct ps2_render_client *r, uint32_t color_handle, uint32_t depth_handle)
{
    const struct ps2_surface *color = ps2_surface_lookup(r->gs, color_handle);
    struct ps2_depth *depth = depth_lookup(r, depth_handle);
    if (!color || (depth_handle && (!depth || depth->width != color->width ||
        depth->height != color->height || depth->color_bpp != color->bits_per_pixel ||
        depth->buffer_width != ((color->width + 63) & ~63U)))) return fail(r, EINVAL);
    if (ps2_render_flush(r)) return -1;
    r->target = color_handle; r->depth = depth_handle;
    return 0;
}
static int queue(struct ps2_render_client *r, const struct ps2gs_command *c)
{
    if (r->gs->error) return fail(r, r->gs->error);
    if (r->count == PS2GS_MAX_COMMANDS && ps2_render_flush(r)) return -1;
    r->commands[r->count++] = *c;
    return 0;
}
int ps2_render_clear_depth(struct ps2_render_client *r)
{
    if (!r->depth || !depth_lookup(r, r->depth)) return fail(r, ENOTSUP);
    struct ps2gs_command command = { .op = PS2GS_CLEAR_DEPTH };
    return queue(r, &command);
}
int ps2_render_fill(struct ps2_render_client *r, unsigned x, unsigned y,
                    unsigned width, unsigned height, uint32_t rgba)
{
    const struct ps2_surface *c = ps2_surface_lookup(r->gs, r->target);
    if (!c || !width || !height || x >= c->width || y >= c->height ||
        width > c->width - x || height > c->height - y) return fail(r, EINVAL);
    struct ps2gs_command command = { .op = PS2GS_FILL, .width = width, .height = height,
        .v = { { .x = x * 16, .y = y * 16, .rgba = rgba } } };
    return queue(r, &command);
}
int ps2_render_triangle(struct ps2_render_client *r, const struct ps2gs_vertex vertices[3], int depth_test)
{
    const struct ps2_surface *c = ps2_surface_lookup(r->gs, r->target);
    if (!c || !vertices || (depth_test != 0 && depth_test != 1)) return fail(r, EINVAL);
    if (depth_test && (!r->depth || !depth_lookup(r, r->depth))) return fail(r, ENOTSUP);
    for (unsigned i = 0; i < 3; ++i)
        if (vertices[i].x > c->width * 16 || vertices[i].y > c->height * 16 || vertices[i].z > 65535)
            return fail(r, EINVAL);
    struct ps2gs_command command = { .op = PS2GS_TRIANGLE,
        .flags = depth_test ? PS2GS_DEPTH_TEST : 0 };
    memcpy(command.v, vertices, sizeof(command.v));
    return queue(r, &command);
}
