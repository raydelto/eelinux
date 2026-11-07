/* SPDX-License-Identifier: MIT */
#include "ps2_gs_client.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static struct ps2gs_caps advertised;
static unsigned opens, closes, submits, sleeps, successful;
static int retry_errors, submit_error, interrupt_sleep;
static struct ps2gs_command captured[PS2GS_MAX_COMMANDS];
static unsigned captured_count;
static unsigned uploads, fail_upload;
static unsigned image_x, image_y, image_width, image_height;
static size_t image_stride;
static const unsigned char *image_source;
static unsigned char received[2048 * 4 * 20];

int __wrap_open(const char *path, int flags, ...)
{
    assert(!strcmp(path, "/test/fb0"));
    assert(flags == (O_RDWR | O_CLOEXEC));
    ++opens;
    return 9;
}
int __wrap_close(int fd) { assert(fd == 9); ++closes; return 0; }
int __wrap_nanosleep(const struct timespec *delay, struct timespec *remaining)
{
    assert(delay->tv_sec == 0 && delay->tv_nsec <= 10000000);
    ++sleeps;
    if (interrupt_sleep) {
        interrupt_sleep = 0;
        remaining->tv_sec = 0; remaining->tv_nsec = 5000000;
        errno = EINTR; return -1;
    }
    return 0;
}
int __wrap_ioctl(int fd, unsigned long op, ...)
{
    assert(fd == 9);
    va_list args; va_start(args, op); void *data = va_arg(args, void *); va_end(args);
    if (op == PS2GS_GET_CAPS) { *(struct ps2gs_caps *)data = advertised; return 0; }
    if (op == PS2GS_UPLOAD) {
        struct ps2gs_upload *u=data;
        assert(u->abi==1 && !u->reserved && u->width==advertised.width);
        assert(u->height==advertised.height && u->bits_per_pixel==advertised.bits_per_pixel);
        assert(u->x>=image_x && u->y>=image_y && u->image_width && u->image_height);
        assert(u->x+u->image_width<=image_x+image_width);
        assert(u->y+u->image_height<=image_y+image_height);
        unsigned bpp=u->bits_per_pixel/8;
        assert(u->bytes==u->image_width*u->image_height*bpp && u->bytes<=3072);
        ++uploads;
        if (uploads==fail_upload) { errno=EIO; return -1; }
        const unsigned char *p=(const unsigned char *)(uintptr_t)u->pixels;
        for (unsigned row=0;row<u->image_height;++row) {
            size_t offset=(u->y-image_y+row)*image_stride+(u->x-image_x)*bpp;
            assert(!memcmp(p+row*u->image_width*bpp,image_source+offset,u->image_width*bpp));
            memcpy(received+(u->y-image_y+row)*image_width*bpp+(u->x-image_x)*bpp,
                p+row*u->image_width*bpp,u->image_width*bpp);
        }
        return 0;
    }
    assert(op == PS2GS_SUBMIT);
    struct ps2gs_submit *s = data;
    assert(s->abi == PS2GS_ABI_VERSION && !s->reserved);
    assert(s->width == advertised.width && s->height == advertised.height);
    assert(s->bits_per_pixel == advertised.bits_per_pixel);
    assert(s->count && s->count <= PS2GS_MAX_COMMANDS);
    ++submits;
    if (retry_errors) { --retry_errors; errno = EAGAIN; return -1; }
    if (submit_error) { errno = submit_error; return -1; }
    memcpy(captured, (void *)(uintptr_t)s->commands, s->count * sizeof(*captured));
    captured_count = s->count; ++successful;
    return 0;
}
static void reset(void)
{
    advertised = (struct ps2gs_caps) { .abi=PS2GS_ABI_VERSION,
        .width=720, .height=576, .bits_per_pixel=32, .max_commands=64,
        .features=PS2GS_HW_FILL|PS2GS_HW_COPY|PS2GS_HW_TRIANGLE|PS2GS_HW_DEPTH16 };
    opens=closes=submits=sleeps=successful=captured_count=0;
    retry_errors=submit_error=interrupt_sleep=0;
    uploads=fail_upload=0; memset(received,0xa5,sizeof(received));
}
static void batching(void)
{
    reset(); struct ps2_gs_client gs;
    assert(!ps2_gs_open(&gs, "/test/fb0"));
    for (unsigned i=0; i<64; ++i) assert(!ps2_gs_fill(&gs,i,2,3,4,0x80112233));
    assert(gs.count==64 && !submits);
    assert(!ps2_gs_fill(&gs,100,2,3,4,0x80445566));
    assert(submits==1 && captured_count==64 && gs.count==1);
    assert(captured[63].v[0].x==63*16 && captured[63].v[0].y==32);
    assert(!ps2_gs_flush(&gs));
    assert(submits==2 && captured_count==1 && captured[0].v[0].rgba==0x80445566);
    assert(!ps2_gs_flush(&gs) && submits==2);
    assert(!ps2_gs_fill(&gs,1,1,2,2,0));
    ps2_gs_close(&gs); assert(closes==1 && submits==2 && gs.count==0);
}
static void errors(void)
{
    reset(); struct ps2_gs_client gs;
    assert(!ps2_gs_open(&gs, "/test/fb0"));
    assert(!ps2_gs_fill(&gs,0,0,720,576,0));
    retry_errors=2; interrupt_sleep=1;
    assert(!ps2_gs_flush(&gs));
    assert(submits==3 && sleeps==3 && successful==1);
    assert(!ps2_gs_fill(&gs,0,0,2,2,0)); submit_error=EIO;
    assert(ps2_gs_flush(&gs)==-1 && gs.error==EIO && gs.count==0);
    assert(submits==4);
    assert(ps2_gs_flush(&gs)==-1 && errno==EIO && submits==4);
    assert(ps2_gs_fill(&gs,0,0,1,1,0)==-1 && submits==4);
    ps2_gs_close(&gs);
    assert(!ps2_gs_open(&gs, "/test/fb0"));
    assert(!ps2_gs_fill(&gs,0,0,2,2,0)); submit_error=EINTR;
    assert(ps2_gs_flush(&gs)==-1 && gs.error==EINTR && submits==5);
    ps2_gs_close(&gs);
    assert(!ps2_gs_open(&gs, "/test/fb0"));
    assert(!ps2_gs_fill(&gs,0,0,2,2,0)); submit_error=0; retry_errors=200;
    assert(ps2_gs_flush(&gs)==-1 && gs.error==EAGAIN && gs.count==0);
    assert(submits==105 && sleeps==103); ps2_gs_close(&gs);
}
static void bounds(void)
{
    reset(); struct ps2_gs_client gs;
    assert(!ps2_gs_open(&gs, "/test/fb0"));
    assert(!ps2_gs_copy(&gs,718,574,0,0,2,2));
    assert(gs.commands[0].v[0].x==718*16 && gs.commands[0].v[1].x==0);
    assert(ps2_gs_fill(&gs,UINT32_MAX,0,1,1,0)==-1 && gs.error==EINVAL);
    assert(gs.count==0 && !submits); ps2_gs_close(&gs);
    assert(!ps2_gs_open(&gs, "/test/fb0"));
    assert(ps2_gs_copy(&gs,0,0,0,0,3,1)==-1 && gs.error==ENOTSUP);
    ps2_gs_close(&gs); advertised.bits_per_pixel=16;
    assert(!ps2_gs_open(&gs, "/test/fb0"));
    assert(!ps2_gs_copy(&gs,0,0,1,1,4,1));
    assert(ps2_gs_copy(&gs,0,0,0,0,2,1)==-1 && gs.error==ENOTSUP);
    ps2_gs_close(&gs);
    assert(!ps2_gs_open(&gs, "/test/fb0"));
    struct ps2gs_vertex v[3]={{.x=720*16,.y=576*16,.z=65535},{0},{0}};
    assert(!ps2_gs_triangle(&gs,v,1));
    assert(gs.commands[0].flags==PS2GS_DEPTH_TEST);
    v[2].z=65536;
    assert(ps2_gs_triangle(&gs,v,1)==-1 && !gs.count);
    ps2_gs_close(&gs); advertised.features &= ~PS2GS_HW_DEPTH16;
    assert(!ps2_gs_open(&gs, "/test/fb0"));
    assert(ps2_gs_clear_depth(&gs)==-1 && gs.error==ENOTSUP);
    ps2_gs_close(&gs);
}
static void capabilities(void)
{
    reset(); struct ps2_gs_client gs;
    advertised.abi=99;
    assert(ps2_gs_open(&gs,"/test/fb0")==-1 && errno==ENOTSUP && gs.fd==-1 && closes==1);
    ps2_gs_close(&gs); assert(closes==1);
    advertised.abi=PS2GS_ABI_VERSION; advertised.max_commands=63;
    assert(ps2_gs_open(&gs,"/test/fb0")==-1);
    advertised.max_commands=64; advertised.bits_per_pixel=24;
    assert(ps2_gs_open(&gs,"/test/fb0")==-1);
    advertised.bits_per_pixel=16; advertised.width=2049;
    assert(ps2_gs_open(&gs,"/test/fb0")==-1);
    advertised.width=720; advertised.features &= ~PS2GS_HW_TRIANGLE;
    assert(ps2_gs_open(&gs,"/test/fb0")==-1);
}
static void colors(void)
{
    assert(ps2_gs_pixel_color(0x1f,16)==0x800000ff);
    assert(ps2_gs_pixel_color(0x3e0,16)==0x8000ff00);
    assert(ps2_gs_pixel_color(0x7c00,16)==0x80ff0000);
    assert(ps2_gs_pixel_color(0x7fff,16)==0x80ffffff);
    assert(ps2_gs_pixel_color(0xff112233,32)==0x80112233);
}
static void image(void)
{
    reset(); advertised.features |= PS2GS_HW_UPLOAD; advertised.width=2048;
    struct ps2_gs_client gs; assert(!ps2_gs_open(&gs,"/test/fb0"));
    unsigned char source[3*8200];
    for (unsigned i=0;i<sizeof(source);++i) source[i]=(i*13+i/257)&255;
    image_source=source; image_x=0; image_y=3; image_width=2048; image_height=3; image_stride=8200;
    assert(!ps2_gs_fill(&gs,0,0,1,1,0));
    assert(!ps2_gs_upload(&gs,0,3,2048,3,source,sizeof(source),8200));
    assert(submits==1 && uploads==9 && !gs.count);
    for (unsigned row=0;row<3;++row) assert(!memcmp(received+row*8192,source+row*8200,8192));
    ps2_gs_close(&gs);
    advertised.bits_per_pixel=16; assert(!ps2_gs_open(&gs,"/test/fb0"));
    image_x=7; image_y=9; image_width=17; image_height=13; image_stride=40;
    assert(!ps2_gs_upload(&gs,7,9,17,13,source,514,40));
    assert(uploads==10);
    for (unsigned row=0;row<13;++row) assert(!memcmp(received+row*34,source+row*40,34));
    ps2_gs_close(&gs);
}
static void image_errors(void)
{
    reset(); struct ps2_gs_client gs; unsigned char source[3*8200]={0};
    assert(!ps2_gs_open(&gs,"/test/fb0"));
    assert(ps2_gs_upload(&gs,0,0,1,1,source,4,4)==-1 && gs.error==ENOTSUP && !uploads);
    ps2_gs_close(&gs); advertised.features |= PS2GS_HW_UPLOAD;
    assert(!ps2_gs_open(&gs,"/test/fb0"));
    assert(ps2_gs_upload(&gs,0,0,2,2,source,15,8)==-1 && gs.error==EINVAL && !uploads);
    ps2_gs_close(&gs); assert(!ps2_gs_open(&gs,"/test/fb0"));
    assert(ps2_gs_upload(&gs,0,0,2,2,source,sizeof(source),SIZE_MAX)==-1 && !uploads);
    ps2_gs_close(&gs); assert(!ps2_gs_open(&gs,"/test/fb0"));
    assert(ps2_gs_upload(&gs,0,0,2,2,(void *)(UINTPTR_MAX-3),16,8)==-1 && !uploads);
    ps2_gs_close(&gs); assert(!ps2_gs_open(&gs,"/test/fb0"));
    assert(ps2_gs_upload(&gs,0,0,2,2,NULL,16,8)==-1 && !uploads);
    ps2_gs_close(&gs); advertised.width=2048; assert(!ps2_gs_open(&gs,"/test/fb0"));
    image_source=source; image_x=0; image_y=0; image_width=2048; image_height=3; image_stride=8200;
    fail_upload=2;
    assert(ps2_gs_upload(&gs,0,0,2048,3,source,sizeof(source),8200)==-1 && gs.error==EIO);
    assert(uploads==2 && !gs.count);
    assert(ps2_gs_upload(&gs,0,0,2048,3,source,sizeof(source),8200)==-1 && uploads==2);
    assert(ps2_gs_flush(&gs)==-1 && !submits); ps2_gs_close(&gs);
}
int main(int argc, char **argv)
{
    assert(argc==2);
    if (!strcmp(argv[1],"batching")) batching();
    else if (!strcmp(argv[1],"errors")) errors();
    else if (!strcmp(argv[1],"bounds")) bounds();
    else if (!strcmp(argv[1],"capabilities")) capabilities();
    else if (!strcmp(argv[1],"colors")) colors();
    else if (!strcmp(argv[1],"image")) image();
    else if (!strcmp(argv[1],"image-errors")) image_errors();
    else return 2;
    puts("PS2 GS client test passed"); return 0;
}
