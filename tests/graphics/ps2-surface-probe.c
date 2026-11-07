/* SPDX-License-Identifier: MIT */
#include "ps2_gs_surface.h"
#include <assert.h>
#include <errno.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static unsigned creates,uploads,copies,fills,changes,restores,closes,destroys,pans;
static int failure,interrupt_wait,gs_closed;
int ps2_surface_probe_main(int,char **);
int __wrap_open(const char *path,int flags,...)
{
    (void)flags;
    if (!strcmp(path,"/dev/ps2-gs")) return 9;
    if (!strcmp(path,"/dev/tty0")) return 10;
    assert(!strcmp(path,"/dev/fb0")); return 11;
}
int __wrap_close(int fd) { assert(fd==9||fd==10||fd==11); if(fd==9) gs_closed=1; ++closes; return 0; }
int __wrap_nanosleep(const struct timespec *duration,struct timespec *remaining)
{
    (void)remaining; assert(duration->tv_sec==1 && !duration->tv_nsec);
    if(interrupt_wait) { raise(SIGINT); errno=EINTR; return -1; }
    return 0;
}
int __wrap_ioctl(int fd,unsigned long op,...)
{
    va_list args; va_start(args,op);
    if(fd==10) {
        if(op==KDGETMODE) *va_arg(args,int*)=KD_TEXT;
        else {
            assert(op==KDSETMODE); int mode=va_arg(args,int);
            if(mode==KD_GRAPHICS) ++changes;
            else { assert(mode==KD_TEXT && gs_closed); ++restores; }
        }
        va_end(args); return 0;
    }
    void *arg=va_arg(args,void*); va_end(args);
    if(fd==11) {
        struct fb_var_screeninfo *v=arg;
        if(op==FBIOGET_VSCREENINFO) *v=(struct fb_var_screeninfo){.yoffset=16,.vmode=FB_VMODE_YWRAP};
        else { assert(op==FBIOPAN_DISPLAY && !v->xoffset && !v->yoffset && !(v->vmode&FB_VMODE_YWRAP)); ++pans; }
        return 0;
    }
    assert(fd==9);
    if(op==PS2GS_GET_CAPS) {
        *(struct ps2gs_caps*)arg=(struct ps2gs_caps){.abi=1,.features=51,.width=720,.height=576,.bits_per_pixel=32}; return 0;
    }
    if(op==PS2GS_SURFACE_CREATE) {
        struct ps2gs_surface_create *c=arg; c->handle=++creates;
        c->buffer_width=(c->width+63)&~63U; c->allocated_bytes=c->buffer_width*((c->height+31)&~31U)*4; return 0;
    }
    if(op==PS2GS_SURFACE_FILL) { ++fills; return 0; }
    if(op==PS2GS_SURFACE_COPY) { ++copies; return 0; }
    if(op==PS2GS_SURFACE_DESTROY) { ++destroys; return 0; }
    assert(op==PS2GS_SURFACE_UPLOAD);
    struct ps2gs_surface_upload *u=arg; assert(u->handle==1 && u->width==132 && u->bytes<=3072);
    ++uploads; if(failure) { errno=EIO; return -1; } return 0;
}
int main(int argc,char **argv)
{
    assert(argc==2);
    failure=!strcmp(argv[1],"failure"); interrupt_wait=!strcmp(argv[1],"interrupt");
    assert(failure||interrupt_wait||!strcmp(argv[1],"success"));
    char *args[]={"ps2-gs-surface-test","--brief",NULL};
    int ret=ps2_surface_probe_main(2,args);
    assert(ret==(failure||interrupt_wait?1:0));
    assert(changes==1 && restores==1 && closes==3 && pans==1 && creates==2);
    assert(uploads==(failure?1:13) && fills==(failure?1:2) && copies==(failure?0:3));
    assert(destroys==(failure?0:2));
    puts("PS2 offscreen probe console lifecycle test passed"); return 0;
}
