/* SPDX-License-Identifier: MIT */
#include "ps2-gs-logic.h"
#include "ps2-gs-texture.h"
#include "ps2-gs-surface.h"
#include "ps2-gs.h"
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
#include "ps2-line-bias-stub.h"
static unsigned changes,restores,closes,pans,solids,waits,local_copies,textured_copies,creates,destroys,uploads;
static int gs_closed,failure,interrupt_wait;
int ps2_copy_probe_main(int,char**);
void debug_describe_resource(char *buf,const struct pipe_resource *r) { assert(r);buf[0]=0; }
void debug_describe_sampler_view(char *buf,const struct pipe_sampler_view *v) { assert(v);buf[0]=0; }
void debug_describe_surface(char *buf,const struct pipe_surface *s) { assert(s);buf[0]=0; }
int __wrap_open(const char *path,int flags,...)
{
    (void)flags;
    if(!strcmp(path,"/dev/ps2-gs")) return 9;
    if(!strcmp(path,"/dev/tty0")) return 10;
    assert(!strcmp(path,"/dev/fb0"));return 11;
}
int __wrap_close(int fd)
{ assert(fd==9||fd==10||fd==11);if(fd==9) gs_closed=1;++closes;return 0; }
int __wrap_nanosleep(const struct timespec *delay,struct timespec *remaining)
{
    (void)remaining;assert(delay->tv_sec==1 && !delay->tv_nsec);++waits;
    if(interrupt_wait) { raise(SIGINT);errno=EINTR;return -1; }return 0;
}
int __wrap_ioctl(int fd,unsigned long op,...)
{
    va_list args;va_start(args,op);
    if(fd==10) {
        if(op==KDGETMODE) *va_arg(args,int*)=KD_TEXT;
        else { assert(op==KDSETMODE);int mode=va_arg(args,int);
            if(mode==KD_GRAPHICS) ++changes;else { assert(mode==KD_TEXT && gs_closed);++restores; } }
        va_end(args);return 0;
    }
    void *arg=va_arg(args,void*);va_end(args);
    if(fd==11) {
        struct fb_var_screeninfo *v=arg;
        if(op==FBIOGET_VSCREENINFO) *v=(struct fb_var_screeninfo){.yoffset=16,.vmode=FB_VMODE_YWRAP};
        else { assert(op==FBIOPAN_DISPLAY && !v->yoffset && !(v->vmode&FB_VMODE_YWRAP));++pans; }return 0;
    }
    assert(fd==9);
    if(op==PS2GS_GET_CAPS) { *(struct ps2gs_caps*)arg=(struct ps2gs_caps){.abi=1,.features=511,.width=720,.height=576,.bits_per_pixel=32};return 0; }
    if(op==PS2GS_SURFACE_CREATE) {
        struct ps2gs_surface_create *r=arg;assert(r->width==64 && r->height==32 && r->bits_per_pixel==32);
        r->handle=++creates;r->buffer_width=64;r->allocated_bytes=8192;return 0;
    }
    if(op==PS2GS_SURFACE_DESTROY) { ++destroys;return 0; }
    if(op==PS2GS_SURFACE_UPLOAD) {
        const struct ps2gs_surface_upload *r=arg;assert(!r->handle && r->bytes<=3072 && r->width && r->height);++uploads;return 0;
    }
    if(op==PS2GS_SURFACE_COPY) {
        const struct ps2gs_surface_copy *r=arg;assert(!r->source && (r->destination==creates || !r->destination) && !(r->width%2));++local_copies;return 0;
    }
    if(op==PS2GS_TEXTURE_SUBMIT) {
        const struct ps2gs_texture_submit *r=arg;assert(r->texture==creates && !r->target && !r->depth && r->count==2);
        ++textured_copies;if(failure) { assert(textured_copies==1);errno=EIO;return -1; }return 0;
    }
    assert(op==PS2GS_SOLID_LOGIC);++solids;
    const struct ps2gs_solid_logic *r=arg;
    assert(r->abi==1 && !r->handle && !r->reserved && r->width && r->height);
    assert(r->x+r->width<=720 && r->y+r->height<=576 && r->plane_mask==0xffffff && r->alu==3);
    return 0;
}

int main(int argc,char **argv)
{
    assert(argc==2);failure=!strcmp(argv[1],"failure");interrupt_wait=!strcmp(argv[1],"interrupt");
    assert(failure||interrupt_wait||!strcmp(argv[1],"success"));
    char *args[]={"ps2-gs-copy-test","--brief",NULL};int result=ps2_copy_probe_main(2,args);
    assert(result==(failure||interrupt_wait ? 1 : 0));
    assert(changes==1 && restores==1 && closes==3 && pans==1 && gs_closed);
    assert(uploads && solids==(failure||interrupt_wait ? 1 : 2));
    assert(local_copies==(failure ? 1 : interrupt_wait ? 4 : 16));
    assert(textured_copies==(failure ? 1 : interrupt_wait ? 3 : 12) && creates==(failure||interrupt_wait ? 1 : 3));
    assert(destroys==(failure ? 0 : interrupt_wait ? 1 : 3));
    assert(waits==(failure ? 0 : interrupt_wait ? 1 : 3));
    puts("native CopyArea probe lifecycle PASS");return 0;
}
