/* SPDX-License-Identifier: MIT */
#include "ps2-gs-render.h"
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
static unsigned changes,restores,closes,pans,renders,commands;
static int gs_closed,failure,interrupt_wait;
int ps2_glyph_probe_main(int,char**);
void debug_describe_resource(char *buf,const struct pipe_resource *r) { assert(r); buf[0]=0; }
void debug_describe_sampler_view(char *buf,const struct pipe_sampler_view *v) { assert(v); buf[0]=0; }
void debug_describe_surface(char *buf,const struct pipe_surface *s) { assert(s); buf[0]=0; }
int __wrap_open(const char *path,int flags,...)
{
    (void)flags;
    if(!strcmp(path,"/dev/ps2-gs")) return 9;
    if(!strcmp(path,"/dev/tty0")) return 10;
    assert(!strcmp(path,"/dev/fb0")); return 11;
}
int __wrap_close(int fd)
{ assert(fd==9||fd==10||fd==11); if(fd==9) gs_closed=1; ++closes; return 0; }
int __wrap_nanosleep(const struct timespec *d,struct timespec *remaining)
{
    (void)remaining; assert(d->tv_sec==1 && !d->tv_nsec);
    if(interrupt_wait) { raise(SIGINT); errno=EINTR; return -1; } return 0;
}
int __wrap_ioctl(int fd,unsigned long op,...)
{
    va_list args; va_start(args,op);
    if(fd==10) {
        if(op==KDGETMODE) *va_arg(args,int*)=KD_TEXT;
        else { assert(op==KDSETMODE); int mode=va_arg(args,int);
            if(mode==KD_GRAPHICS) ++changes; else { assert(mode==KD_TEXT && gs_closed); ++restores; } }
        va_end(args); return 0;
    }
    void *arg=va_arg(args,void*); va_end(args);
    if(fd==11) {
        struct fb_var_screeninfo *v=arg;
        if(op==FBIOGET_VSCREENINFO) *v=(struct fb_var_screeninfo){.yoffset=16,.vmode=FB_VMODE_YWRAP};
        else { assert(op==FBIOPAN_DISPLAY && !v->yoffset && !(v->vmode&FB_VMODE_YWRAP)); ++pans; }
        return 0;
    }
    assert(fd==9);
    if(op==PS2GS_GET_CAPS) { *(struct ps2gs_caps*)arg=(struct ps2gs_caps){.abi=1,.features=255,.width=720,.height=576,.bits_per_pixel=32}; return 0; }
    assert(op==PS2GS_RENDER_SUBMIT);
    ++renders;
    if(failure) { assert(renders==1); errno=EIO; return -1; }
    const struct ps2gs_render_submit *s=arg;
    assert(!s->target && !s->depth && s->width==720 && s->height==576 && s->count && s->count<=64);
    const struct ps2gs_command *c=(const void*)(uintptr_t)s->commands;
    for(unsigned i=0;i<s->count;++i) {
        assert(c[i].op==PS2GS_FILL && !c[i].flags && c[i].width && c[i].height);
        unsigned x=c[i].v[0].x/16,y=c[i].v[0].y/16;
        assert(x+c[i].width<=720 && y+c[i].height<=576);
        ++commands;
    }
    return 0;
}
int main(int argc,char **argv)
{
    assert(argc==2); failure=!strcmp(argv[1],"failure"); interrupt_wait=!strcmp(argv[1],"interrupt");
    assert(failure||interrupt_wait||!strcmp(argv[1],"success"));
    char *args[]={"ps2-gs-glyph-test","--brief",NULL}; int result=ps2_glyph_probe_main(2,args);
    assert(result==(failure||interrupt_wait?1:0));
    assert(changes==1 && restores==1 && closes==3 && pans==1 && gs_closed);
    assert(failure ? renders==1 && !commands : renders>3 && commands>200);
    puts("native glyph probe lifecycle PASS"); return 0;
}
