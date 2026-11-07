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
static unsigned changes,restores,closes,pans,creates,destroys,renders,samples,uploads,copies,fills;
static int gs_closed,failure,interrupt_wait;
static struct ps2gs_texture_triangle perspective[2];
static double float_value(uint32_t bits)
{ float value; memcpy(&value,&bits,sizeof(value)); return value; }
static void verify_planar_perspective(void)
{
    /* Independent projection reference: every horizontal section of a planar
     * rectangle maps linearly across its width, in both submitted triangles.
     * An inconsistent reciprocal W produces a kink at their shared diagonal.
     */
    const struct ps2gs_texture_vertex *tl=&perspective[0].v[0],*tr=&perspective[0].v[1];
    const struct ps2gs_texture_vertex *br=&perspective[1].v[1],*bl=&perspective[1].v[2];
    for(unsigned n=0;n<2;++n) for(unsigned a=1;a<=3;++a) for(unsigned b=1;b<=3;++b) {
        double weight[3]={a/10.0,b/10.0,(10-a-b)/10.0},x=0,y=0,s=0,t=0,q=0;
        for(unsigned k=0;k<3;++k) {
            const struct ps2gs_texture_vertex *v=&perspective[n].v[k];
            x+=weight[k]*v->position.x; y+=weight[k]*v->position.y;
            s+=weight[k]*float_value(v->s); t+=weight[k]*float_value(v->t);
            q+=weight[k]*float_value(v->q);
        }
        double row=(y-tl->position.y)/(br->position.y-tl->position.y);
        double left=tl->position.x*(1-row)+bl->position.x*row;
        double right=tr->position.x*(1-row)+br->position.x*row;
        double reference_u=(x-left)/(right-left);
        double reference_v=row*float_value(br->q)/((1-row)*float_value(tl->q)+row*float_value(br->q));
        double du=s/q-reference_u,dv=t/q-reference_v;
        assert(du>-0.00001 && du<0.00001 && dv>-0.00001 && dv<0.00001);
    }
}
int ps2_texture_probe_main(int,char**);
void debug_describe_resource(char *buf,const struct pipe_resource *r) { assert(r); buf[0]=0; }
void debug_describe_sampler_view(char *buf,const struct pipe_sampler_view *v) { assert(v); buf[0]=0; }
int __wrap_open(const char *path,int flags,...)
{
    (void)flags;
    if(!strcmp(path,"/dev/ps2-gs")) return 9;
    if(!strcmp(path,"/dev/tty0")) return 10;
    assert(!strcmp(path,"/dev/fb0")); return 11;
}
int __wrap_close(int fd) { assert(fd==9||fd==10||fd==11); if(fd==9) gs_closed=1; ++closes; return 0; }
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
    if(op==PS2GS_SURFACE_CREATE || op==PS2GS_DEPTH_CREATE) {
        struct ps2gs_surface_create *c=arg; c->handle=++creates; c->buffer_width=(c->width+63)&~63U;
        unsigned bpp=op==PS2GS_DEPTH_CREATE?16:c->bits_per_pixel,mask=bpp==16?63:31;
        c->allocated_bytes=c->buffer_width*((c->height+mask)&~mask)*(bpp/8); return 0;
    }
    if(op==PS2GS_RENDER_SUBMIT) {
        struct ps2gs_render_submit *r=arg; ++renders;
        assert(r->count==2 && r->target==1 && r->depth==2 && r->width==448 && r->height==256);
        const struct ps2gs_command *commands=(const void*)(uintptr_t)r->commands;
        assert(commands[0].op==PS2GS_FILL && commands[1].op==PS2GS_CLEAR_DEPTH); return 0;
    }
    if(op==PS2GS_SURFACE_UPLOAD) {
        const struct ps2gs_surface_upload *u=arg; ++uploads;
        assert(u->handle==3 && u->width==32 && u->bytes<=3072 && u->bytes==u->width*u->height*4);
        if(!u->y) {
            uint32_t pixel; memcpy(&pixel,(void*)(uintptr_t)u->pixels,4);
            assert(pixel==(uploads<=2?0x800000ffU:0x80ff0000U));
        }
        return 0;
    }
    if(op==PS2GS_TEXTURE_SUBMIT) {
        struct ps2gs_texture_submit *r=arg; ++samples;
        assert(r->count==1 && r->target==1 && r->depth==2 && r->texture==3 && renders==1);
        const struct ps2gs_texture_triangle *t=(const void*)(uintptr_t)r->triangles;
        assert(t->flags==(PS2GS_DEPTH_TEST|PS2GS_TEXTURE_MODULATE));
        assert(t->v[0].q==(samples<=2?0x3f800000U:0x3f000000U));
        if(samples>=3) perspective[samples-3]=*t;
        if(samples==4) verify_planar_perspective();
        if(failure) { errno=EIO; return -1; } return 0;
    }
    if(op==PS2GS_SURFACE_FILL) { ++fills; return 0; }
    if(op==PS2GS_SURFACE_COPY) { ++copies; return 0; }
    assert(op==PS2GS_SURFACE_DESTROY); ++destroys; return 0;
}
int main(int argc,char **argv)
{
    assert(argc==2); failure=!strcmp(argv[1],"failure"); interrupt_wait=!strcmp(argv[1],"interrupt");
    assert(failure||interrupt_wait||!strcmp(argv[1],"success"));
    char *args[]={"ps2-gs-texture-test","--brief",NULL}; int result=ps2_texture_probe_main(2,args);
    assert(result==(failure||interrupt_wait?1:0));
    assert(changes==1 && restores==1 && closes==3 && pans==1 && creates==3 && renders==1);
    assert(samples==(failure?1:4) && uploads==(failure?2:4));
    assert(copies==(failure?0:1) && fills==(failure?0:1) && destroys==(failure?0:3));
    puts("native texture probe lifecycle PASS"); return 0;
}
