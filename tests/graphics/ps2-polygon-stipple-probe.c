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
#undef ARRAY_SIZE
#include "scrnintstr.h"
#include "windowstr.h"
#include "gcstruct.h"
#include "mi.h"
#include "miline.h"
#include "pixmapstr.h"
#include "fb.h"
static unsigned changes, restores, closes, pans, waits, creates, destroys, uploads, fills;
static unsigned solids, packets, presentations, polygon_fills;
static int gs_closed, failed, first_failure, later_failure, bar_failure, present_failure, interrupt_wait;
static struct ps2gs_surface_create buffers[32];
static uint32_t *pixels[32];
int ps2_polygon_stipple_probe_main(int, char**);
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
static uint32_t reference_pixels[720*576];
static unsigned scene;
static void expected_fill(int x,int y,int w,int h,uint32_t c)
{for(int j=y;j<y+h;++j)for(int i=x;i<x+w;++i)reference_pixels[j*720+i]=c;}
void miReferenceFillPolygon(DrawablePtr,GCPtr,int,int,int,DDXPointPtr);
static BoxRec reference_clips[2];static unsigned reference_clip_count;
static unsigned reference_pattern_style;static uint32_t reference_background;
static void reference_spans(DrawablePtr d,GCPtr gc,int n,DDXPointPtr points,int *widths,int sorted)
{
 (void)d;(void)sorted;
 for(int i=0;i<n;++i)for(int x=points[i].x;x<points[i].x+widths[i];++x){int y=points[i].y;
  if(x<0 || y<0 || x>=720 || y>=576)continue;
  for(unsigned c=0;c<reference_clip_count;++c){BoxRec b=reference_clips[c];
   if(x<b.x1 || x>=b.x2 || y<b.y1 || y>=b.y2)continue;
   int sx=(x-152)%16,sy=(y-208)%16;if(sx<0)sx+=16;if(sy<0)sy+=16;
   int value=(sx/8+sy/8)&1;if(!value && reference_pattern_style==FillStippled)break;
   uint32_t colour=value?gc->fgPixel:reference_background;
   uint32_t *q=&reference_pixels[y*720+x];*q=gc->alu==GXxor?*q^colour:colour;break;
  }
 }
}
static const GCOps reference_ops={.FillSpans=reference_spans};
static void reference_polygon(int x,int y,const DDXPointRec *p,unsigned n,unsigned rule,uint32_t colour,unsigned alu)
{
 DDXPointRec copy[16];assert(n<=16);for(unsigned i=0;i<n;++i)copy[i]=(DDXPointRec){p[i].x+x,p[i].y+y};
 DrawableRec d={0};GC gc={.ops=&reference_ops,.fillRule=rule,.fgPixel=colour,.alu=alu};
 miReferenceFillPolygon(&d,&gc,Complex,CoordModeOrigin,n,copy);
}
static void expected_scene(unsigned which)
{
 const DDXPointRec arrow[]={{16,32},{104,32},{104,8},{176,64},{104,120},{104,96},{16,96}};
 const DDXPointRec diamond[]={{224,0},{448,72},{224,144},{0,72}};
 const DDXPointRec ring[]={{8,8},{184,8},{184,120},{8,120},{8,8},{56,40},{136,40},{136,88},{56,88},{56,40},{8,8}};
 const DDXPointRec star[]={{96,8},{116,44},{176,44},{132,76},{148,120},{96,94},{44,120},{60,76},{16,44},{76,44}};
 expected_fill(0,0,720,576,0x200c06);reference_clips[0]=(BoxRec){0,0,720,576};reference_clip_count=1;reference_pattern_style=FillOpaqueStippled;reference_background=0xffff;
 if(which==1){reference_polygon(152,208,arrow,7,EvenOddRule,0xffff00,GXcopy);reference_polygon(376,208,arrow,7,EvenOddRule,0xffff00,GXcopy);}
 else if(which==2){reference_pattern_style=FillStippled;
  expected_fill(152,208,416,128,0xff);expected_fill(152,208,192,128,0xff0000);expected_fill(376,208,192,128,0xff0000);
  reference_clips[0]=(BoxRec){152,208,344,336};reference_clips[1]=(BoxRec){376,208,568,336};reference_clip_count=2;
  reference_polygon(136,200,diamond,4,EvenOddRule,0xffff,GXcopy);
 }else if(which==3){reference_background=0;
  expected_fill(152,208,192,128,0xff0000);expected_fill(376,208,192,128,0xff0000);
  reference_polygon(152,208,ring,11,EvenOddRule,0xffffff,GXxor);reference_polygon(376,208,ring,11,WindingRule,0xffffff,GXxor);
  expected_fill(152,368,416,16,0xff00);
 }else{reference_background=0xffff00;
  expected_fill(152,208,192,128,0);expected_fill(376,208,192,128,0);
  reference_polygon(152,208,star,10,EvenOddRule,0xff00ff,GXcopy);reference_polygon(376,208,star,10,EvenOddRule,0xff00ff,GXcopy);
  expected_fill(152,368,416,16,0xffffff);
 }
}
int __wrap_nanosleep(const struct timespec *delay,struct timespec *remaining)
{
    (void)remaining;assert(delay->tv_sec==1 && !delay->tv_nsec && !failed);++waits;scene=waits;
    assert(waits<=4 && uploads && polygon_fills && !packets && presentations==(waits==4));
    expected_scene(scene);
    assert(!memcmp(pixels[0],reference_pixels,sizeof(reference_pixels)));
    if(interrupt_wait){raise(SIGINT);errno=EINTR;return -1;}return 0;
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
        assert((creates == 1 && r->width == 192 && r->height == 128) ||
               (creates == 2 && r->width == 64 && r->height == 64));
        r->handle = creates; r->buffer_width = (r->width+63)&~63U;
        r->allocated_bytes = r->buffer_width*((r->height+31)&~31U)*4; buffers[creates] = *r;
        pixels[creates] = calloc(r->width*r->height,4); assert(pixels[creates]); return 0;
    }
    if (op == PS2GS_SURFACE_DESTROY) {
        struct ps2gs_surface_destroy *r = arg; assert(r->handle < 32 && pixels[r->handle]);
        free(pixels[r->handle]); pixels[r->handle] = NULL; ++destroys; return 0;
    }
    if (op == PS2GS_SURFACE_UPLOAD) {
        const struct ps2gs_surface_upload *r=arg;++uploads;
        const uint32_t *v=(void*)(uintptr_t)r->pixels;
        if(bar_failure && r->width==416 && (v[0]&0xffffff)==0xffffff) {failed=1;errno=EIO;return -1;}
        rect(r->handle,r->x,r->y,r->width,r->height);assert(r->bytes==r->width*r->height*4);
        for(unsigned y=0;y<r->height;++y)for(unsigned x=0;x<r->width;++x)
            pixels[r->handle][(r->y+y)*buffers[r->handle].width+r->x+x]=v[y*r->width+x]&0xffffff;
        return 0;
    }
    if (op == PS2GS_SURFACE_FILL) {
        struct ps2gs_surface_fill *r = arg; ++fills;
        if(r->height==1){++polygon_fills;
            if((first_failure && polygon_fills==1) || (later_failure && polygon_fills==10)){failed=1;errno=EIO;return -1;}
        }
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
    if(r->source==1 && r->destination==0 && r->width==192 && r->height==128)++presentations;
    if(present_failure && presentations) {assert(presentations==1);failed=1;errno=EIO;return -1;}
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
    char *args[]={"ps2-gs-polygon-stipple-test","--brief",NULL};int result=ps2_polygon_stipple_probe_main(2,args);
    assert(result==(first_failure || later_failure || bar_failure || present_failure || interrupt_wait?1:0));
    assert(changes==1 && restores==1 && closes==3 && pans==1 && gs_closed && polygon_fills>0 && creates==2);
    if(first_failure) assert(failed && polygon_fills==1 && !waits && !packets);
    else if(later_failure) assert(failed && polygon_fills==10 && !waits && !packets);
    else if(bar_failure) assert(failed && waits==3 && presentations==1);
    else if(present_failure) assert(failed && presentations==1 && waits==3);
    else assert(!failed && destroys==creates && waits==(interrupt_wait?1:4));
    puts("native stippled polygon probe lifecycle PASS"); return 0;
}
