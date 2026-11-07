/* SPDX-License-Identifier: MIT */
/* Pixel model belongs only to this test. Production rasterizes in GS VRAM. */
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
int ps2_transform_probe_main(int,char**);
void debug_describe_resource(char *buf,const struct pipe_resource *r){assert(r);buf[0]=0;}
void debug_describe_sampler_view(char *buf,const struct pipe_sampler_view *v){assert(v);buf[0]=0;}
void debug_describe_surface(char *buf,const struct pipe_surface *s){assert(s);buf[0]=0;}
static struct {unsigned width,height,depth;uint32_t *pixels;} buffers[5];
static unsigned changes,restores,closes,pans,waits,creates,destroys,packets,triangles,presentations;
static int gs_closed,failed,first_failure,later_failure,present_failure,bar_failure,interrupt_wait;
int __wrap_open(const char *path,int flags,...)
{(void)flags;if(!strcmp(path,"/dev/ps2-gs"))return 9;if(!strcmp(path,"/dev/tty0"))return 10;assert(!strcmp(path,"/dev/fb0"));return 11;}
int __wrap_close(int fd)
{
    assert(fd==9 || fd==10 || fd==11);
    if(fd==9){assert(!gs_closed);gs_closed=1;for(unsigned i=0;i<5;++i){free(buffers[i].pixels);buffers[i].pixels=NULL;}}
    ++closes;return 0;
}
static void allocate(unsigned h,unsigned w,unsigned height,unsigned depth)
{assert(h<5 && !buffers[h].pixels);buffers[h].width=w;buffers[h].height=height;buffers[h].depth=depth;buffers[h].pixels=calloc(w*height,4);assert(buffers[h].pixels);}
static void rect(unsigned h,unsigned x,unsigned y,unsigned w,unsigned height)
{assert(h<5 && buffers[h].pixels && w && height && x+w<=buffers[h].width && y+height<=buffers[h].height);}
static void fill(unsigned h,unsigned x,unsigned y,unsigned w,unsigned height,uint32_t c)
{rect(h,x,y,w,height);for(unsigned j=y;j<y+height;++j)for(unsigned i=x;i<x+w;++i)buffers[h].pixels[j*buffers[h].width+i]=c;}
static double edge(const struct ps2gs_vertex *a,const struct ps2gs_vertex *b,double x,double y)
{return ((double)b->x-a->x)*(y-a->y)-((double)b->y-a->y)*(x-a->x);}
static void triangle(const struct ps2gs_render_submit *r,const struct ps2gs_command *c)
{
    assert(c->flags==PS2GS_DEPTH_TEST && !c->width && !c->height && r->depth && buffers[r->depth].depth);
    for(unsigned i=0;i<3;++i)assert(c->v[i].x<=192*16 && c->v[i].y<=128*16 && c->v[i].z<=65535 && c->v[i].rgba>>24==128);
    const struct ps2gs_vertex *v=c->v;double area=edge(v,v+1,v[2].x,v[2].y);assert(area);
    for(unsigned y=0;y<128;++y)for(unsigned x=0;x<192;++x){double px=x*16+8,py=y*16+8;
        double weights[3]={edge(v+1,v+2,px,py)/area,edge(v+2,v,px,py)/area,edge(v,v+1,px,py)/area};
        if(weights[0]<0 || weights[1]<0 || weights[2]<0)continue;
        unsigned z=(unsigned)(weights[0]*v[0].z+weights[1]*v[1].z+weights[2]*v[2].z+.5);
        unsigned index=y*192+x;if(z<buffers[r->depth].pixels[index])continue;
        uint32_t color=0;
        for(unsigned channel=0;channel<3;++channel){double value=0;
            for(unsigned i=0;i<3;++i)value+=weights[i]*((v[i].rgba>>(8*channel))&255);
            color|=(uint32_t)(value+.5)<<(8*channel);
        }
        buffers[r->depth].pixels[index]=z;buffers[r->target].pixels[index]=color;
    }
}
static void check_scene(unsigned scene)
{
    assert(presentations==2*scene && triangles && packets && buffers[1].pixels && buffers[3].pixels);
    unsigned counts[8]={0};
    for(unsigned y=0;y<128;++y)for(unsigned x=0;x<192;++x){unsigned index=y*192+x;
        uint32_t a=buffers[1].pixels[index],b=buffers[3].pixels[index];
        assert(a==b);
        assert(buffers[0].pixels[(192+y)*720+152+x]==a && buffers[0].pixels[(192+y)*720+376+x]==b);
        if(a==0xffff00)++counts[0];
        if(a==255)++counts[1];
        if(a==0xffff)++counts[2];
        if(a==0x200c06)++counts[3];
        if(a==0xff00ff)++counts[4];
        if(a==0xff0000)++counts[5];
        if(a==0xff00)++counts[6];
    }
    assert(counts[3]>1000);
    if(scene==1){uint32_t p=buffers[1].pixels[74*192+105];assert((p&255)>30 && ((p>>8)&255)>30 && ((p>>16)&255)>30);}
    if(scene==2){assert(counts[0]>4000 && counts[1]>1000 && !counts[2]);assert(buffers[1].pixels[64*192+96]==0xffff00);}
    if(scene==3){assert(counts[2]>8000 && buffers[1].pixels[64*192+32]==0xffff && buffers[1].pixels[64*192+120]==0x200c06);}
    if(scene==4){unsigned visible=0;for(unsigned i=0;i<8;++i)if(i!=3 && counts[i]>100)++visible;assert(visible>=3);}
    for(unsigned y=0;y<576;++y)for(unsigned x=0;x<720;++x){
        if(y>=192 && y<320 && ((x>=152 && x<344)||(x>=376 && x<568)))continue;
        uint32_t expected=0x200c06;
        if((scene==2 || scene==4) && y>=352 && y<368 && x>=152 && x<568)expected=scene==2?0xff00:0xffffff;
        assert(buffers[0].pixels[y*720+x]==expected);
    }
}
int __wrap_nanosleep(const struct timespec *delay,struct timespec *remaining)
{
    (void)remaining;assert(delay->tv_sec==1 && !delay->tv_nsec && !failed);++waits;assert(waits<=4);check_scene(waits);
    if(interrupt_wait){raise(SIGINT);errno=EINTR;return -1;}return 0;
}
int __wrap_ioctl(int fd,unsigned long op,...)
{
    va_list args;va_start(args,op);
    if(fd==10){if(op==KDGETMODE)*va_arg(args,int*)=KD_TEXT;
        else{assert(op==KDSETMODE);int mode=va_arg(args,int);if(mode==KD_GRAPHICS)++changes;else{assert(mode==KD_TEXT && gs_closed);++restores;}}
        va_end(args);return 0;}
    void *arg=va_arg(args,void*);va_end(args);
    if(fd==11){struct fb_var_screeninfo *v=arg;
        if(op==FBIOGET_VSCREENINFO)*v=(struct fb_var_screeninfo){.yoffset=16,.vmode=FB_VMODE_YWRAP};
        else{assert(op==FBIOPAN_DISPLAY && !v->xoffset && !v->yoffset && !(v->vmode&FB_VMODE_YWRAP));++pans;}return 0;}
    assert(fd==9 && !failed && !gs_closed);
    if(op==PS2GS_GET_CAPS){*(struct ps2gs_caps*)arg=(struct ps2gs_caps){.abi=1,.features=511,.width=720,.height=576,.bits_per_pixel=32};allocate(0,720,576,0);return 0;}
    if(op==PS2GS_SURFACE_CREATE){struct ps2gs_surface_create *r=arg;
        assert(r->abi==1 && r->width==192 && r->height==128 && r->bits_per_pixel==32 && ++creates<=4);
        r->handle=creates;r->buffer_width=192;r->allocated_bytes=192*128*4;allocate(creates,192,128,0);return 0;}
    if(op==PS2GS_DEPTH_CREATE){struct ps2gs_depth_create *r=arg;
        assert(r->abi==1 && r->width==192 && r->height==128 && r->color_bits_per_pixel==32 && ++creates<=4);
        r->handle=creates;r->buffer_width=192;r->allocated_bytes=192*128*2;allocate(creates,192,128,1);return 0;}
    if(op==PS2GS_SURFACE_DESTROY){struct ps2gs_surface_destroy *r=arg;assert(r->handle<5 && buffers[r->handle].pixels);
        free(buffers[r->handle].pixels);buffers[r->handle].pixels=NULL;++destroys;return 0;}
    if(op==PS2GS_SURFACE_FILL){struct ps2gs_surface_fill *r=arg;
        if(bar_failure && (r->rgba&0xffffff)==0xffffff){failed=1;errno=EIO;return -1;}
        assert(!buffers[r->handle].depth);fill(r->handle,r->x,r->y,r->width,r->height,r->rgba&0xffffff);return 0;}
    if(op==PS2GS_RENDER_SUBMIT){struct ps2gs_render_submit *r=arg;assert(r->abi==1 && r->count && r->count<=64);
        assert(r->width==192 && r->height==128 && r->bits_per_pixel==32 && (r->target==1 || r->target==3) && r->depth==r->target+1);++packets;
        if((first_failure && packets==1)||(later_failure && packets==4)){failed=1;errno=EIO;return -1;}
        const struct ps2gs_command *c=(void*)(uintptr_t)r->commands;
        for(unsigned i=0;i<r->count;++i){
            if(c[i].op==PS2GS_FILL){assert(!c[i].flags);fill(r->target,c[i].v[0].x/16,c[i].v[0].y/16,c[i].width,c[i].height,c[i].v[0].rgba&0xffffff);}
            else if(c[i].op==PS2GS_CLEAR_DEPTH){assert(!c[i].flags);fill(r->depth,0,0,192,128,0);}
            else{assert(c[i].op==PS2GS_TRIANGLE);triangle(r,c+i);++triangles;}
        }
        return 0;}
    assert(op==PS2GS_SURFACE_COPY);struct ps2gs_surface_copy *r=arg;
    assert((r->source==1 || r->source==3) && !r->destination && !r->sx && !r->sy && r->width==192 && r->height==128);
    rect(r->source,r->sx,r->sy,r->width,r->height);rect(0,r->dx,r->dy,r->width,r->height);++presentations;
    if(present_failure && presentations==1){failed=1;errno=EIO;return -1;}
    for(unsigned y=0;y<128;++y)memcpy(buffers[0].pixels+(r->dy+y)*720+r->dx,buffers[r->source].pixels+y*192,192*4);
    return 0;
}
int main(int argc,char **argv)
{
    assert(argc==2);first_failure=!strcmp(argv[1],"first-failure");later_failure=!strcmp(argv[1],"later-failure");
    present_failure=!strcmp(argv[1],"present-failure");bar_failure=!strcmp(argv[1],"bar-failure");interrupt_wait=!strcmp(argv[1],"interrupt");
    assert(first_failure || later_failure || present_failure || bar_failure || interrupt_wait || !strcmp(argv[1],"success"));
    char *args[]={"ps2-gs-transform-test","--brief",NULL};int result=ps2_transform_probe_main(2,args);
    if(result && !failed && !interrupt_wait)fprintf(stderr,"unexpected probe error: packets=%u triangles=%u presentations=%u waits=%u\n",packets,triangles,presentations,waits);
    assert(result==(first_failure || later_failure || present_failure || bar_failure || interrupt_wait?1:0));
    assert(changes==1 && restores==1 && closes==3 && pans==1 && gs_closed && creates==4);
    if(first_failure)assert(failed && packets==1 && !waits && !presentations);
    else if(later_failure)assert(failed && packets==4 && waits==1 && presentations==3);
    else if(present_failure)assert(failed && presentations==1 && !waits);
    else if(bar_failure)assert(failed && presentations==8 && waits==3);
    else assert(!failed && destroys==creates && waits==(interrupt_wait?1:4));
    puts("native transformed triangle probe lifecycle PASS");return 0;
}
