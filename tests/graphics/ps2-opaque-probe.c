/* SPDX-License-Identifier: MIT */
#include "ps2-gs-texture.h"
#include "ps2_gs_logic.h"
#include "ps2_gs_render.h"
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
static unsigned solids, packets, presentations;
static int gs_closed, failed, first_failure, later_failure, bar_failure, present_failure, interrupt_wait;
static struct ps2gs_surface_create buffers[32];
static uint32_t *pixels[32];
int ps2_opaque_probe_main(int, char**);
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
    int bit=scene==3 ? (phase(x,px+3,21)/7+phase(y,py+5,15)/5)&1 :
        (phase(x,px,32)/16+phase(y,py,32)/16)&1;
    return bit ? (scene==4?0xffff00:0xff00ff) : (scene==3?0xff0000:scene==4?0x0080ff:0x00ffff);
}
int __wrap_nanosleep(const struct timespec *delay,struct timespec *remaining)
{
    (void)remaining;assert(delay->tv_sec==1 && !delay->tv_nsec && !failed);++waits;
    assert(waits<=4 && solids && !packets && presentations==(waits==4));
    if(waits>=3) assert(solids);
    for(int y=0;y<576;++y) for(int x=0;x<720;++x) {
        uint32_t color=0x200c06;
        if(waits==2) {
            if(x>=168 && y>=208 && x<552 && y<336) {
                color=x>=354 && x<366 ? 0xff0000 :
                    ((phase(x,175,32)/16+phase(y,213,32)/16)&1)?0xff00ff:0x00ffff;
            }
        } else {
            color=expected_panel(x,y,152,208,waits);
            if(color==0x200c06) color=expected_panel(x,y,376,208,waits);
            if(waits>=3 && x>=152 && x<568 && y>=368 && y<384) color=waits==3?0xffff00:0xffffff;
        }
        assert(pixels[0][y*720+x]==color);
    }
    if(interrupt_wait) { raise(SIGINT);errno=EINTR;return -1; }return 0;
}
static uint32_t boolean(unsigned alu,uint32_t s,uint32_t d)
{
    switch(alu) {
    case 0:return 0;case 1:return s&d;case 2:return s&~d;case 3:return s;
    case 4:return ~s&d;case 5:return d;case 6:return s^d;case 7:return s|d;
    case 8:return ~(s|d);case 9:return ~(s^d);case 10:return ~d;
    case 11:return s|~d;case 12:return ~s;case 13:return ~s|d;
    case 14:return ~(s&d);default:assert(alu==15);return ~0U;
    }
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
        assert(creates == 1 && r->width == 192 && r->height == 128);
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
        struct ps2gs_surface_fill *r = arg; ++fills;
        fill(r->handle,r->x,r->y,r->width,r->height,r->rgba); return 0;
    }
    if (op == PS2GS_RENDER_SUBMIT) {
        struct ps2gs_render_submit *r=arg;assert(r->abi==1 && !r->depth && r->count && r->count<=64);++packets;
        assert(0);
        assert(r->width==buffers[r->target].width && r->height==buffers[r->target].height && r->bits_per_pixel==32);
        const struct ps2gs_command *c=(void*)(uintptr_t)r->commands;
        for(unsigned i=0;i<r->count;++i) {
            assert(c[i].op==PS2GS_FILL && !c[i].flags && !(c[i].v[0].x%16) && !(c[i].v[0].y%16));
            fill(r->target,c[i].v[0].x/16,c[i].v[0].y/16,c[i].width,c[i].height,c[i].v[0].rgba);
        }
        return 0;
    }
    if (op == PS2GS_SOLID_LOGIC) {
        struct ps2gs_solid_logic *r=arg;assert(r->abi==1 && r->alu<=15 && r->plane_mask<=0xffffff);++solids;
        if((first_failure && solids==1) || (later_failure && solids==129) ||
           (bar_failure && r->width==416 && r->height==16 && r->color==0xffffff)) {failed=1;errno=EIO;return -1;}
        rect(r->handle,r->x,r->y,r->width,r->height);
        for(unsigned j=r->y;j<r->y+r->height;++j) for(unsigned i=r->x;i<r->x+r->width;++i) {
            uint32_t *v=&pixels[r->handle][j*buffers[r->handle].width+i];
            *v=(*v&~r->plane_mask)|(boolean(r->alu,r->color,*v)&r->plane_mask);
        }
        return 0;
    }
    assert(op == PS2GS_SURFACE_COPY);
    struct ps2gs_surface_copy *r = arg; assert(!(r->width%2));
    rect(r->source,r->sx,r->sy,r->width,r->height); rect(r->destination,r->dx,r->dy,r->width,r->height);
    assert(r->source==1 && !r->destination && r->width==192 && r->height==128);++presentations;
    if(present_failure) {assert(presentations==1);failed=1;errno=EIO;return -1;}
    for (unsigned j = 0; j < r->height; ++j)
        memmove(pixels[r->destination]+(r->dy+j)*buffers[r->destination].width+r->dx,
            pixels[r->source]+(r->sy+j)*buffers[r->source].width+r->sx,r->width*4);
    return 0;
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    first_failure=!strcmp(argv[1],"first-failure");later_failure=!strcmp(argv[1],"later-failure");
    bar_failure=!strcmp(argv[1],"bar-failure");present_failure=!strcmp(argv[1],"present-failure");
    interrupt_wait=!strcmp(argv[1],"interrupt");
    assert(first_failure || later_failure || bar_failure || present_failure || interrupt_wait || !strcmp(argv[1],"success"));
    char *args[]={"ps2-gs-opaque-test","--brief",NULL};int result=ps2_opaque_probe_main(2,args);
    assert(result==(first_failure || later_failure || bar_failure || present_failure || interrupt_wait?1:0));
    assert(changes==1 && restores==1 && closes==3 && pans==1 && gs_closed && uploads==0 && creates==1);
    if(first_failure) assert(failed && solids==1 && !waits && !packets);
    else if(later_failure) assert(failed && solids==129 && !waits && !packets);
    else if(bar_failure) assert(failed && waits==3 && presentations==1);
    else if(present_failure) assert(failed && presentations==1 && waits==3);
    else assert(!failed && destroys==creates && waits==(interrupt_wait?1:4));
    puts("native opaque Boolean stipple probe lifecycle PASS"); return 0;
}
