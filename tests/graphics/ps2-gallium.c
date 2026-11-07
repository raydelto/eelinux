/* SPDX-License-Identifier: MIT */
#include "ps2_raster.h"
#include "ps2_draw.h"
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned submissions;
static int io_error;
static unsigned uploads;
int __wrap_ioctl(int fd, unsigned long request, ...)
{
    assert(fd == 999);
    va_list args; va_start(args, request);
    void *argument = va_arg(args, void *); va_end(args);
    if (request == PS2GS_UPLOAD) {
        struct ps2gs_upload *u = argument;
        assert(u->x==5 && u->y==6 && u->image_width==3 && u->image_height==2);
        assert(u->bytes==(u->bits_per_pixel==32 ? 24 : 12));
        ++uploads;
        if (io_error) { errno=io_error; return -1; }
        return 0;
    }
    assert(request == PS2GS_SUBMIT);
    struct ps2gs_submit *s = argument;
    assert(s->count && s->count <= PS2GS_MAX_COMMANDS);
    ++submissions;
    if (io_error) { errno = io_error; return -1; }
    return 0;
}
static struct ps2_gs_client fresh(void)
{
    submissions=uploads=0; io_error=0;
    return (struct ps2_gs_client) { .fd=999,
        .caps={.abi=PS2GS_ABI_VERSION, .width=720, .height=576, .bits_per_pixel=32,
               .max_commands=64, .features=15} };
}
static void clear(void)
{
    struct ps2_gs_client gs = fresh();
    union pipe_color_union color = {.f={1.0f,0.5f,0.0f,1.0f}};
    struct pipe_scissor_state scissor={.minx=5,.miny=6,.maxx=15,.maxy=26};
    assert(!ps2_gallium_clear_color(&gs,&scissor,&color));
    assert(gs.count==1 && gs.commands[0].op==PS2GS_FILL);
    assert(gs.commands[0].width==10 && gs.commands[0].height==20);
    assert(gs.commands[0].v[0].x==80 && gs.commands[0].v[0].y==96);
    assert(gs.commands[0].v[0].rgba==0x800080ff);
    scissor.maxx=5;
    assert(!ps2_gallium_clear_color(&gs,&scissor,&color) && gs.count==1);
    assert(!ps2_gallium_clear_color(&gs,NULL,&color));
    assert(gs.commands[1].width==720 && gs.commands[1].height==576);
    color.f[1]=NAN;
    assert(ps2_gallium_clear_color(&gs,NULL,&color)==-1 && !gs.count && !submissions);
    assert(gs.error==ENOTSUP);
    color.f[1]=0; scissor.maxx=5;
    assert(ps2_gallium_clear_color(&gs,&scissor,&color)==-1);
}
static void triangle(void)
{
    struct ps2_gs_client gs = fresh();
    float pos[3][4]={{0.5f,1.25f,0,1},{720,0,1,1},{0,576,0.5f,1}};
    float col[3][4]={{1,0,0,1},{0,1,0,1},{0,0,1,1}};
    assert(!ps2_gallium_triangle(&gs,pos,col,1));
    struct ps2gs_command *c = &gs.commands[0];
    assert(c->op==PS2GS_TRIANGLE && c->flags==PS2GS_DEPTH_TEST);
    assert(c->v[0].x==8 && c->v[0].y==20 && c->v[0].z==65535 && c->v[0].rgba==0x800000ff);
    assert(c->v[1].x==720*16 && c->v[1].z==0 && c->v[1].rgba==0x8000ff00);
    assert(c->v[2].y==576*16 && c->v[2].z==32768 && c->v[2].rgba==0x80ff0000);
    pos[1][0]=-0.1f;
    assert(ps2_gallium_triangle(&gs,pos,col,0)==-1 && gs.error==ENOTSUP && !gs.count);
    assert(!submissions);
    gs=fresh(); pos[1][0]=INFINITY;
    assert(ps2_gallium_triangle(&gs,pos,col,0)==-1 && !submissions);
    gs=fresh(); pos[1][0]=1; col[0][3]=NAN;
    assert(ps2_gallium_triangle(&gs,pos,col,0)==-1);
    gs=fresh(); col[0][3]=1; gs.caps.features &= ~PS2GS_HW_DEPTH16;
    assert(ps2_gallium_triangle(&gs,pos,col,1)==-1 && !gs.count);
}
static void stage(void)
{
    struct ps2_gs_client gs = fresh(); struct draw_context context={0};
    struct draw_stage *s = ps2_draw_stage_create(&context,&gs);
    assert(s && s->draw==&context && s->tri && s->flush && s->destroy);
    assert(!ps2_draw_stage_outputs(s,2,1,0,1));
    struct prim_header p={0};
    for (unsigned i=0; i<3; ++i) {
        p.v[i]=calloc(1,sizeof(struct vertex_header)+2*sizeof(float[4]));
        assert(p.v[i]);
        p.v[i]->data[0][i]=1; p.v[i]->data[0][3]=1;
        p.v[i]->data[1][0]=10+i*5; p.v[i]->data[1][1]=20+i*10;
        p.v[i]->data[1][2]=0.25f; p.v[i]->data[1][3]=1;
    }
    s->tri(s,&p);
    assert(gs.count==1 && !gs.error);
    assert(gs.commands[0].v[0].x==160 && gs.commands[0].v[0].rgba==0x800000ff);
    assert(gs.commands[0].v[1].x==240 && gs.commands[0].v[1].rgba==0x8000ff00);
    assert(!ps2_draw_stage_outputs(s,2,1,0,0));
    assert(submissions==1 && !gs.count);
    s->tri(s,&p); assert(gs.count==1 && gs.commands[0].flags==0);
    s->flush(s,0); assert(submissions==2 && !gs.count);
    p.v[1]->clipmask=1; s->tri(s,&p);
    assert(gs.error==ENOTSUP && !gs.count && submissions==2);
    s->flush(s,0); assert(submissions==2);
    s->destroy(s);
    for (unsigned i=0; i<3; ++i) free(p.v[i]);
    gs=fresh(); s=ps2_draw_stage_create(&context,&gs); assert(s);
    assert(ps2_draw_stage_outputs(s,2,2,0,0)==-1 && gs.error==EINVAL);
    s->destroy(s);
    gs=fresh(); s=ps2_draw_stage_create(&context,&gs); assert(s);
    s->point(s,NULL); assert(gs.error==ENOTSUP && !submissions); s->destroy(s);
    gs=fresh(); s=ps2_draw_stage_create(&context,&gs); assert(s);
    s->line(s,NULL); assert(gs.error==ENOTSUP); s->destroy(s);
    gs=fresh(); s=ps2_draw_stage_create(&context,&gs); assert(s);
    assert(!ps2_gs_fill(&gs,0,0,2,2,0)); io_error=ETIMEDOUT;
    assert(ps2_draw_stage_outputs(s,2,0,1,0)==-1 && gs.error==ETIMEDOUT);
    s->flush(s,0); s->destroy(s); assert(submissions==1);
}
static void upload(void)
{
    struct ps2_gs_client gs=fresh(); gs.caps.features |= PS2GS_HW_UPLOAD;
    struct pipe_box box={.x=5,.y=6,.width=3,.height=2,.depth=1};
    unsigned char pixels[28]={0};
    assert(!ps2_gallium_upload_color(&gs,PIPE_FORMAT_R8G8B8X8_UNORM,&box,pixels,28,16));
    assert(uploads==1 && !gs.count);
    gs.caps.bits_per_pixel=16;
    assert(!ps2_gallium_upload_color(&gs,PIPE_FORMAT_R5G5B5X1_UNORM,&box,pixels,14,8));
    assert(uploads==2);
    assert(ps2_gallium_upload_color(&gs,PIPE_FORMAT_R5G6B5_UNORM,&box,pixels,14,8)==-1);
    assert(gs.error==ENOTSUP && uploads==2);
    gs=fresh(); gs.caps.features |= PS2GS_HW_UPLOAD; box.z=1;
    assert(ps2_gallium_upload_color(&gs,PIPE_FORMAT_R8G8B8X8_UNORM,&box,pixels,28,16)==-1 && !uploads);
    gs=fresh(); gs.caps.features |= PS2GS_HW_UPLOAD; box.z=0; io_error=EIO;
    assert(ps2_gallium_upload_color(&gs,PIPE_FORMAT_R8G8B8X8_UNORM,&box,pixels,28,16)==-1 && uploads==1);
    assert(ps2_gallium_upload_color(&gs,PIPE_FORMAT_R8G8B8X8_UNORM,&box,pixels,28,16)==-1 && uploads==1);
}
int main(int argc, char **argv)
{
    assert(argc==2);
    if (!strcmp(argv[1],"clear")) clear();
    else if (!strcmp(argv[1],"triangle")) triangle();
    else if (!strcmp(argv[1],"stage")) stage();
    else if (!strcmp(argv[1],"upload")) upload();
    else return 2;
    puts("PS2 Gallium raster test passed"); return 0;
}
