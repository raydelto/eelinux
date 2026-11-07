/* SPDX-License-Identifier: MIT */
#include "ps2-gs-texture.h"
#include "ps2_gs_logic.h"
#include "pipe/p_state.h"
#include <assert.h>
#include <errno.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ps2-line-bias-stub.h"
static unsigned changes, restores, closes, pans, waits, creates, destroys, uploads, fills;
static unsigned solids, textures, expansions, presentations;
static int gs_closed, failed, seed_failure, expand_failure, present_failure, interrupt_wait;
static struct ps2gs_surface_create buffers[32];
static uint32_t *pixels[32];
int ps2_stipple_probe_main(int, char**);
void debug_describe_resource(char *buf, const struct pipe_resource *r) { assert(r); buf[0] = 0; }
void debug_describe_sampler_view(char *buf, const struct pipe_sampler_view *v) { assert(v); buf[0] = 0; }
void debug_describe_surface(char *buf, const struct pipe_surface *s) { assert(s); buf[0] = 0; }
int __wrap_open(const char *path, int flags, ...)
{
    (void)flags;
    if (!strcmp(path, "/dev/ps2-gs")) return 9;
    if (!strcmp(path, "/dev/tty0")) return 10;
    assert(!strcmp(path, "/dev/fb0")); return 11;
}
int __wrap_close(int fd)
{
    assert(fd == 9 || fd == 10 || fd == 11);
    if (fd == 9) {
        assert(!gs_closed); gs_closed = 1;
        for (unsigned i = 0; i < 32; ++i) { free(pixels[i]); pixels[i] = NULL; }
    }
    ++closes; return 0;
}
static void rect(unsigned handle, unsigned x, unsigned y, unsigned width, unsigned height)
{
    assert(handle < 32 && pixels[handle] && width && height);
    assert(x+width <= buffers[handle].width && y+height <= buffers[handle].height);
}
static void fill(unsigned handle, unsigned x, unsigned y, unsigned w, unsigned h, uint32_t color)
{
    rect(handle,x,y,w,h);
    for (unsigned j = y; j < y+h; ++j) for (unsigned i = x; i < x+w; ++i)
        pixels[handle][j*buffers[handle].width+i] = color & 0xffffff;
}
static int phase(int coordinate,int origin,int size)
{ int p=(coordinate-origin)%size;return p<0?p+size:p; }
static uint32_t expected_panel(int x,int y,int px,int py,unsigned scene)
{
    if(x<px || y<py || x>=px+192 || y>=py+128) return 0x200c06;
    if(scene==3) return ((phase(x,px+3,21)/7+phase(y,py+5,15)/5)&1)?0x00ffff:0xffff00;
    return ((phase(x,px,32)/16+phase(y,py,32)/16)&1)?0xffffff:0xff3300;
}
int __wrap_nanosleep(const struct timespec *delay,struct timespec *remaining)
{
    (void)remaining;assert(delay->tv_sec==1 && !delay->tv_nsec && !failed);++waits;
    assert(waits<=4 && fills==waits && solids);
    assert(presentations==(waits==4) && textures && expansions);
    for(int y=0;y<576;++y) for(int x=0;x<720;++x) {
        uint32_t color=0x200c06;
        if(waits==2) {
            if(x>=168 && y>=208 && x<552 && y<336 && !(x>=354 && x<366))
                color=((phase(x,175,32)/16+phase(y,213,32)/16)&1)?0xffffff:0xff3300;
        } else {
            color=expected_panel(x,y,152,208,waits);
            if(color==0x200c06) color=expected_panel(x,y,376,208,waits);
            if(waits==4 && x>=152 && x<568 && y>=368 && y<384) color=0x00ff00;
        }
        assert(pixels[0][y*720+x]==color);
    }
    if(interrupt_wait) { raise(SIGINT);errno=EINTR;return -1; }return 0;
}
static unsigned padded(unsigned n) { unsigned p = 1; while (p < n) p *= 2; return p; }
static unsigned texel(uint32_t word, unsigned dimension)
{
    float f; memcpy(&f, &word, 4); float value = f*padded(dimension);
    assert(value >= 0 && value <= dimension && value == (unsigned)value); return value;
}
int __wrap_ioctl(int fd, unsigned long op, ...)
{
    va_list args; va_start(args, op);
    if (fd == 10) {
        if (op == KDGETMODE) *va_arg(args, int*) = KD_TEXT;
        else { assert(op == KDSETMODE); int mode = va_arg(args, int);
            if (mode == KD_GRAPHICS) ++changes; else { assert(mode == KD_TEXT && gs_closed); ++restores; } }
        va_end(args); return 0;
    }
    void *arg = va_arg(args, void*); va_end(args);
    if (fd == 11) {
        struct fb_var_screeninfo *v = arg;
        if (op == FBIOGET_VSCREENINFO) *v = (struct fb_var_screeninfo){.yoffset = 16, .vmode = FB_VMODE_YWRAP};
        else { assert(op == FBIOPAN_DISPLAY && !v->yoffset && !(v->vmode & FB_VMODE_YWRAP)); ++pans; } return 0;
    }
    assert(fd == 9 && !failed && !gs_closed);
    if (op == PS2GS_GET_CAPS) {
        *(struct ps2gs_caps*)arg = (struct ps2gs_caps){.abi = 1, .features = 511, .width = 720, .height = 576, .bits_per_pixel = 32};
        buffers[0] = (struct ps2gs_surface_create){.width = 720, .height = 576, .bits_per_pixel = 32};
        pixels[0] = calloc(720*576,4); assert(pixels[0]); return 0;
    }
    if (op == PS2GS_SURFACE_CREATE) {
        struct ps2gs_surface_create *r = arg; assert(r->bits_per_pixel == 32 && r->abi == 1 && ++creates < 32);
        if (creates == 1) assert(r->width == 192 && r->height == 128);
        else assert(r->width <= 64 && r->height <= 32 && r->width*r->height*4 <= 8192);
        r->handle = creates; r->buffer_width = (r->width+63)&~63U;
        r->allocated_bytes = r->buffer_width*((r->height+31)&~31U)*4; buffers[creates] = *r;
        pixels[creates] = calloc(r->width*r->height,4); assert(pixels[creates]); return 0;
    }
    if (op == PS2GS_SURFACE_DESTROY) {
        struct ps2gs_surface_destroy *r = arg; assert(r->handle < 32 && pixels[r->handle]);
        free(pixels[r->handle]); pixels[r->handle] = NULL; ++destroys; return 0;
    }
    if (op == PS2GS_SURFACE_UPLOAD) { assert(0); }
    if (op == PS2GS_SURFACE_FILL) {
        struct ps2gs_surface_fill *r = arg; assert(!r->handle); ++fills;
        fill(0,r->x,r->y,r->width,r->height,r->rgba); return 0;
    }
    if (op == PS2GS_SOLID_LOGIC) {
        struct ps2gs_solid_logic *r = arg; assert(r->handle>1 && r->alu == 3 && r->plane_mask == 0xffffff); ++solids;
        if(seed_failure && solids==2) {failed=1;errno=EIO;return -1;}
        fill(r->handle,r->x,r->y,r->width,r->height,r->color); return 0;
    }
    if (op == PS2GS_TEXTURE_SUBMIT) {
        struct ps2gs_texture_submit *r = arg; assert(r->texture && r->target != r->texture && !r->depth);
        assert(r->count && r->count <= 64 && !(r->count%2)); ++textures;
        const struct ps2gs_texture_triangle *ts = (void*)(uintptr_t)r->triangles;
        for (unsigned i = 0; i < r->count; i += 2) {
            for (unsigned k = 0; k < 2; ++k) for (unsigned j = 0; j < 3; ++j)
                assert(!ts[i+k].flags && !(ts[i+k].v[j].position.x%16) && !(ts[i+k].v[j].position.y%16) && ts[i+k].v[j].q == 0x3f800000);
            unsigned x = ts[i].v[0].position.x/16, y = ts[i].v[0].position.y/16;
            unsigned w = ts[i].v[2].position.x/16-x, h = ts[i].v[2].position.y/16-y;
            unsigned sx = texel(ts[i].v[0].s,buffers[r->texture].width), sy = texel(ts[i].v[0].t,buffers[r->texture].height);
            rect(r->target,x,y,w,h); rect(r->texture,sx,sy,w,h);
            assert(texel(ts[i].v[2].s,buffers[r->texture].width) == sx+w);
            assert(texel(ts[i].v[2].t,buffers[r->texture].height) == sy+h);
            assert(!memcmp(&ts[i].v[0],&ts[i+1].v[0],sizeof(ts[i].v[0])) && !memcmp(&ts[i].v[2],&ts[i+1].v[1],sizeof(ts[i].v[0])));
            for (unsigned j = 0; j < h; ++j) for (unsigned k = 0; k < w; ++k)
                pixels[r->target][(y+j)*buffers[r->target].width+x+k] = pixels[r->texture][(sy+j)*buffers[r->texture].width+sx+k];
        }
        return 0;
    }
    assert(op == PS2GS_SURFACE_COPY);
    struct ps2gs_surface_copy *r = arg; assert(!(r->width%2));
    rect(r->source,r->sx,r->sy,r->width,r->height); rect(r->destination,r->dx,r->dy,r->width,r->height);
    if (r->source == r->destination) {
        assert(r->source > 1); ++expansions;
        if (expand_failure) { assert(expansions == 1); failed = 1; errno = EIO; return -1; }
    } else {
        assert(r->source == 1 && !r->destination && r->width == 192 && r->height == 128); ++presentations;
        if (present_failure) { assert(presentations == 1); failed = 1; errno = EIO; return -1; }
    }
    for (unsigned j = 0; j < r->height; ++j)
        memmove(pixels[r->destination]+(r->dy+j)*buffers[r->destination].width+r->dx,
            pixels[r->source]+(r->sy+j)*buffers[r->source].width+r->sx,r->width*4);
    return 0;
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    seed_failure = !strcmp(argv[1], "seed-failure"); expand_failure = !strcmp(argv[1], "expand-failure");
    present_failure = !strcmp(argv[1], "present-failure"); interrupt_wait = !strcmp(argv[1], "interrupt");
    assert(seed_failure || expand_failure || present_failure || interrupt_wait || !strcmp(argv[1], "success"));
    char *args[] = {"ps2-gs-stipple-test", "--brief", NULL}; int result = ps2_stipple_probe_main(2, args);
    assert(result == (seed_failure || expand_failure || present_failure || interrupt_wait ? 1 : 0));
    assert(changes == 1 && restores == 1 && closes == 3 && pans == 1 && gs_closed && uploads == 0);
    if (seed_failure) assert(failed && textures==0 && !waits && solids==2);
    else if (expand_failure) assert(failed && expansions == 1 && !waits && !presentations);
    else if (present_failure) assert(failed && presentations == 1 && waits == 3);
    else assert(!failed && destroys == creates && waits == (interrupt_wait ? 1 : 4));
    puts("native opaque stipple probe lifecycle PASS"); return 0;
}
