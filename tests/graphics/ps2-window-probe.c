/* SPDX-License-Identifier: MIT */
#include "ps2-gs-texture.h"
#include "pipe/p_state.h"
#include <assert.h>
#include <errno.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static unsigned changes, restores, closes, pans, waits, creates, destroys, uploads;
static unsigned fills, clears, window_copies, source_presentations, canvas_presentations;
static unsigned uploaded_rows;
static int gs_closed, failed, move_failure, present_failure, interrupt_wait;
static struct ps2gs_surface_create buffers[32];
int ps2_window_probe_main(int, char**);
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
{ assert(fd == 9 || fd == 10 || fd == 11); if (fd == 9) { assert(!gs_closed); gs_closed = 1; } ++closes; return 0; }
int __wrap_nanosleep(const struct timespec *delay, struct timespec *remaining)
{
    (void)remaining; assert(delay->tv_sec == 1 && !delay->tv_nsec && !failed); ++waits;
    static const unsigned expected_moves[] = {0,1,3,4}, expected_sources[] = {2,2,4,5};
    assert(waits <= 4 && window_copies == expected_moves[waits-1] && source_presentations == expected_sources[waits-1]);
    assert(canvas_presentations == (waits == 4));
    if (interrupt_wait) { raise(SIGINT); errno = EINTR; return -1; } return 0;
}
static void rect(unsigned handle, unsigned x, unsigned y, unsigned width, unsigned height)
{
    assert(handle < 32 && buffers[handle].width && width && height);
    assert(x+width <= buffers[handle].width && y+height <= buffers[handle].height);
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
        buffers[0] = (struct ps2gs_surface_create){.width = 720, .height = 576, .bits_per_pixel = 32}; return 0;
    }
    if (op == PS2GS_SURFACE_CREATE) {
        struct ps2gs_surface_create *r = arg; assert(r->bits_per_pixel == 32 && r->abi == 1 && ++creates < 32);
        if (creates == 1) assert(r->width == 129 && r->height == 65);
        else if (creates == 2) assert(r->width == 256 && r->height == 192);
        else assert(r->width == 64 && r->height == 32);
        r->handle = creates; r->buffer_width = (r->width+63)&~63U;
        r->allocated_bytes = r->buffer_width*((r->height+31)&~31U)*4; buffers[creates] = *r; return 0;
    }
    if (op == PS2GS_SURFACE_DESTROY) {
        struct ps2gs_surface_destroy *r = arg; assert(r->handle < 32 && buffers[r->handle].width);
        buffers[r->handle].width = 0; ++destroys; return 0;
    }
    if (op == PS2GS_SURFACE_UPLOAD) {
        struct ps2gs_surface_upload *r = arg;
        assert(r->handle == 1 && r->width == 129 && !r->x && r->y == uploaded_rows && r->height == 5 && r->bytes == 129*5*4);
        uploaded_rows += r->height;
        ++uploads; return 0;
    }
    if (op == PS2GS_SURFACE_FILL) {
        struct ps2gs_surface_fill *r = arg; assert(!r->handle); rect(0,r->x,r->y,r->width,r->height); ++fills; return 0;
    }
    if (op == PS2GS_RENDER_SUBMIT) {
        struct ps2gs_render_submit *r = arg; assert(r->target == 2 && !r->depth && r->count == 1 && r->width == 256 && r->height == 192);
        const struct ps2gs_command *c = (void*)(uintptr_t)r->commands;
        assert(c->op == PS2GS_FILL && c->width == 256 && c->height == 192); ++clears; return 0;
    }
    if (op == PS2GS_TEXTURE_SUBMIT) {
        struct ps2gs_texture_submit *r = arg; assert(r->texture && r->target != r->texture && !r->depth && r->count == 2);
        const struct ps2gs_texture_triangle *ts = (void*)(uintptr_t)r->triangles;
        for (unsigned i = 0; i < 2; ++i) for (unsigned j = 0; j < 3; ++j)
            assert(!ts[i].flags && ts[i].v[j].position.x <= r->width*16 && ts[i].v[j].position.y <= r->height*16 && ts[i].v[j].q == 0x3f800000);
        return 0;
    }
    assert(op == PS2GS_SURFACE_COPY);
    struct ps2gs_surface_copy *r = arg; assert(!(r->width%2));
    rect(r->source,r->sx,r->sy,r->width,r->height); rect(r->destination,r->dx,r->dy,r->width,r->height);
    if (r->source == r->destination) {
        assert(!r->source || r->source == 2); ++window_copies;
        if (move_failure) { assert(window_copies == 1); failed = 1; errno = EIO; return -1; }
    }
    if (r->source == 1 && !r->destination) {
        assert(r->width == 128 && r->height == 65); ++source_presentations;
        if (present_failure) { assert(source_presentations == 1); failed = 1; errno = EIO; return -1; }
    }
    if (r->source == 2 && !r->destination) { assert(r->width == 256 && r->height == 192); ++canvas_presentations; }
    return 0;
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    move_failure = !strcmp(argv[1], "move-failure"); present_failure = !strcmp(argv[1], "present-failure");
    interrupt_wait = !strcmp(argv[1], "interrupt");
    assert(move_failure || present_failure || interrupt_wait || !strcmp(argv[1], "success"));
    char *args[] = {"ps2-gs-window-test", "--brief", NULL}; int result = ps2_window_probe_main(2, args);
    assert(result == (move_failure || present_failure || interrupt_wait ? 1 : 0));
    assert(changes == 1 && restores == 1 && closes == 3 && pans == 1 && gs_closed && uploads == 13 && uploaded_rows == 65);
    if (move_failure) assert(failed && window_copies == 1 && source_presentations == 2 && waits == 1 && !clears);
    else if (present_failure) assert(failed && source_presentations == 1 && !waits && !window_copies);
    else { assert(!failed && destroys == creates && waits == (interrupt_wait ? 1 : 4));
        assert(window_copies == (interrupt_wait ? 0 : 4) && clears == (interrupt_wait ? 0 : 1)); }
    puts("native CopyWindow probe lifecycle PASS"); return 0;
}
