/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "ps2_gs_logic.h"
#include "ps2_gs_lut.h"
#include "ps2-gs-rgba-transfer.h"
#include <errno.h>
#include <sys/ioctl.h>
#include <time.h>

_Static_assert(sizeof(struct ps2gs_solid_logic) == 40, "solid logic ABI");
_Static_assert(sizeof(struct ps2gs_solid_batch) == 40, "solid batch ABI");
static int fail(struct ps2_surface_client *gs, int error)
{ if (!gs->error) gs->error = error; errno = gs->error; return -1; }
static int rgb_solid_logic(struct ps2_surface_client *gs, uint32_t handle,
    unsigned x, unsigned y, unsigned width, unsigned height,
    uint32_t color, uint32_t plane_mask, unsigned alu)
{
    if (!gs) { errno = EINVAL; return -1; }
    if (gs->error) return fail(gs, gs->error);
    if (gs->fd < 0) return fail(gs, EBADF);
    const struct ps2_surface *b = ps2_surface_lookup(gs, handle);
    if (!b || !width || !height || x >= b->width || y >= b->height ||
        width > b->width-x || height > b->height-y || alu > 15)
        return fail(gs, EINVAL);
    unsigned bits = b->bits_per_pixel == 16 ? 0x7fff : 0xffffff;
    if ((color | plane_mask) & ~bits) return fail(gs, EINVAL);
    if (!(gs->caps.features & PS2GS_HW_SOLID_LOGIC)) return fail(gs, ENOTSUP);
    struct ps2gs_solid_logic r = { .abi = PS2GS_ABI_VERSION, .handle = handle,
        .x = x, .y = y, .width = width, .height = height,
        .color = color, .plane_mask = plane_mask, .alu = alu };
    if (ps2_surface_front_region(gs, handle,x,y,width,height)) return -1;
    for (unsigned retry = 0; retry < 100; ++retry) {
        if (!ioctl(gs->fd, PS2GS_SOLID_LOGIC, &r)) return 0;
        if (errno != EAGAIN) return fail(gs, errno);
        struct timespec delay = { .tv_nsec = 10000000 };
        while (nanosleep(&delay, &delay) < 0)
            if (errno != EINTR) return fail(gs, errno);
    }
    return fail(gs, EAGAIN);
}
static unsigned alpha_logic(unsigned alu,unsigned source,unsigned destination)
{
    switch(alu){case 0:return 0;case 1:return source&destination;case 2:return source&~destination;
    case 3:return source;case 4:return ~source&destination;case 5:return destination;
    case 6:return source^destination;case 7:return source|destination;case 8:return ~(source|destination);
    case 9:return ~(source^destination);case 10:return ~destination;case 11:return source|~destination;
    case 12:return ~source;case 13:return ~source|destination;case 14:return ~(source&destination);default:return 255;}
}
int ps2_surface_solid_logic(struct ps2_surface_client *gs,uint32_t handle,
    unsigned x,unsigned y,unsigned width,unsigned height,uint32_t color,uint32_t mask,unsigned alu)
{
    if(!gs){errno=EINVAL;return -1;}
    if(gs->error)return fail(gs,gs->error);
    if(gs->fd<0)return fail(gs,EBADF);
    const struct ps2_surface *b=ps2_surface_lookup(gs,handle);
    if(!b || !width || !height || x>=b->width || y>=b->height || width>b->width-x || height>b->height-y ||
        alu>15 || ((color|mask)&~ps2_surface_native_mask(gs,handle)))return fail(gs,EINVAL);
    if(!(gs->caps.features&PS2GS_HW_SOLID_LOGIC))return fail(gs,ENOTSUP);
    if(!(mask&0xff000000))return rgb_solid_logic(gs,handle,x,y,width,height,color&0xffffff,mask,alu);
    if(!mask || alu==5)return 0;
    if(gs->fd<0)return fail(gs,EBADF);
    uint32_t scratch=0,rgba_scratch=0,palette=0,table[256];
    int result=-1,error=errno;
    if(ps2_surface_create(gs,64,32,32,&scratch) || ps2_surface_create(gs,16,16,32,&palette) ||
        ((gs->caps.features&PS2GS_HW_RGBA_TRANSFER) && ps2_surface_create(gs,64,32,32,&rgba_scratch)))goto out;
    /* Keep the intermediate alpha value in a colour byte, as in the native
     * Boolean copy engine. Its final channel map writes the alpha byte. */
    for(unsigned i=0;i<256;++i)table[i]=alpha_logic(alu,color>>24,i)&255;
    result=0;
    for(unsigned yy=0;!result && yy<height;){unsigned h=32-((y+yy)&31);if(h>height-yy)h=height-yy;
        for(unsigned xx=0;!result && xx<width;){unsigned w=64-((x+xx)&63);if(w>width-xx)w=width-xx;
            result=ps2_surface_map_palette(gs,palette,table) ||
                ps2_surface_channel_map_blit(gs,handle,scratch,palette,x+xx,y+yy,0,0,w,h,255,3) ||
                ps2_surface_alpha_write(gs,scratch,rgba_scratch,handle,palette,x+xx,y+yy,w,h,mask&0xff000000)?-1:0;
            xx+=w;
        }
        yy+=h;
    }
    /* The alpha and RGB plane operations commute. Consume every original
     * alpha byte before the RGB operation changes the source target. */
    if(!result && (mask&0xffffff))
        result=rgb_solid_logic(gs,handle,x,y,width,height,color&0xffffff,mask&0xffffff,alu);
out:
    error=errno;
    if(rgba_scratch && !gs->error && ps2_surface_destroy(gs,rgba_scratch) && !result){result=-1;error=errno;}
    if(palette && !gs->error && ps2_surface_destroy(gs,palette) && !result){result=-1;error=errno;}
    if(scratch && !gs->error && ps2_surface_destroy(gs,scratch) && !result){result=-1;error=errno;}
    if(result)errno=error;
    return result;
}
int ps2_surface_solid_batch(struct ps2_surface_client *gs, uint32_t handle,
    const struct ps2gs_solid_logic *rectangles, unsigned count)
{
    if (!gs) { errno=EINVAL; return -1; }
    if (gs->error) return fail(gs,gs->error);
    if (gs->fd<0) return fail(gs,EBADF);
    const struct ps2_surface *s=ps2_surface_lookup(gs,handle);
    if (!s || !rectangles || !count || count>PS2GS_MAX_COMMANDS) return fail(gs,EINVAL);
    if (!(gs->caps.features&PS2GS_HW_SOLID_BATCH)) return fail(gs,ENOTSUP);
    unsigned bits=ps2_surface_native_mask(gs,handle);
    unsigned x=s->width,y=s->height,x2=0,y2=0;
    for (unsigned i=0;i<count;++i) {
        const struct ps2gs_solid_logic *b=rectangles+i;
        if (b->abi!=PS2GS_ABI_VERSION || b->handle!=handle || b->reserved || b->alu>15 ||
            ((b->color|b->plane_mask)&~bits) || b->alu!=rectangles[0].alu ||
            b->plane_mask!=rectangles[0].plane_mask || !b->width || !b->height ||
            b->x>=s->width || b->y>=s->height || b->width>s->width-b->x || b->height>s->height-b->y)
            return fail(gs,EINVAL);
        if(b->x<x)x=b->x;
        if(b->y<y)y=b->y;
        if(b->x+b->width>x2)x2=b->x+b->width;
        if(b->y+b->height>y2)y2=b->y+b->height;
    }
    if(rectangles[0].plane_mask&0xff000000){
        for(unsigned i=0;i<count;++i){const struct ps2gs_solid_logic *r=rectangles+i;
            if(ps2_surface_solid_logic(gs,handle,r->x,r->y,r->width,r->height,r->color,r->plane_mask,r->alu))return -1;}
        return 0;
    }
    if (ps2_surface_front_region(gs,handle,x,y,x2-x,y2-y)) return -1;
    struct ps2gs_solid_batch batch={.abi=PS2GS_ABI_VERSION,.handle=handle,.count=count,
        .plane_mask=rectangles[0].plane_mask,.alu=rectangles[0].alu,
        .width=s->width,.height=s->height,.bits_per_pixel=s->bits_per_pixel,
        .rectangles=(uintptr_t)rectangles};
    for(unsigned retry=0;retry<100;++retry) {
        if(!ioctl(gs->fd,PS2GS_SOLID_BATCH,&batch))return 0;
        if(errno!=EAGAIN)return fail(gs,errno);
        struct timespec delay={.tv_nsec=10000000};
        while(nanosleep(&delay,&delay)<0)if(errno!=EINTR)return fail(gs,errno);
    }
    return fail(gs,EAGAIN);
}
