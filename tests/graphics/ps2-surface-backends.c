/* SPDX-License-Identifier: MIT */
#include "ps2_surface_exa.h"
#include "ps2_resource.h"
#include "os.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct ps2_surface_client gs;
static unsigned calls,creates,destroys,uploads,sleeps,closes,next_handle=1;
static int retry_open,retry_ioctl,fail_ioctl,corrupt_reply,expect_fatal;
static const unsigned char *expected_pixels;
static size_t expected_stride;
static unsigned expected_x,expected_y,expected_cpp;
static int create_error,probe_error,probe_success;
static unsigned probes;
static struct ps2gs_surface_fill last_fill;
static struct ps2gs_surface_copy last_copy;
static PixmapPtr front_pixmap;
static jmp_buf fatal;

void FatalError(const char *format,...)
{ assert(expect_fatal && format); expect_fatal=0; longjmp(fatal,1); }
Bool dixRegisterPrivateKey(DevPrivateKey key,DevPrivateType type,unsigned size)
{ assert(type==PRIVATE_SCREEN && !size); *key=(DevPrivateKeyRec){.initialized=TRUE,.type=type}; return TRUE; }
void *exaGetPixmapDriverPrivate(PixmapPtr pixmap) { return (void *)pixmap->devPrivates; }
static PixmapPtr get_front(ScreenPtr screen) { assert(front_pixmap->drawable.pScreen==screen); return front_pixmap; }
int __wrap_open(const char *device,int flags,...)
{
    assert(!strcmp(device,"/dev/ps2-gs") && flags==(O_RDWR|O_CLOEXEC));
    if (retry_open) { --retry_open; errno=EAGAIN; return -1; }
    return 999;
}
int __wrap_close(int fd) { assert(fd==999); ++closes; return 0; }
int __wrap_nanosleep(const struct timespec *delay,struct timespec *remaining)
{ (void)remaining; assert(delay->tv_nsec==10000000); ++sleeps; return 0; }
int __wrap_ioctl(int fd,unsigned long op,...)
{
    assert(fd==999); ++calls;
    va_list args; va_start(args,op); void *argument=va_arg(args,void*); va_end(args);
    if (retry_ioctl) { --retry_ioctl; errno=EAGAIN; return -1; }
    if (fail_ioctl) { errno=fail_ioctl; return -1; }
    if (op==PS2GS_SURFACE_CREATE && create_error) {
        errno=create_error;create_error=0;return -1;
    }
    if (op==PS2GS_SURFACE_DESTROY && !((struct ps2gs_surface_destroy*)argument)->handle) {
        const struct ps2gs_surface_destroy *d=argument;
        assert(d->abi==PS2GS_ABI_VERSION && !d->reserved[0] && !d->reserved[1]);++probes;
        if(probe_success)return 0;
        errno=probe_error?probe_error:ENOENT;return -1;
    }
    if (op==PS2GS_GET_CAPS) {
        *(struct ps2gs_caps*)argument=(struct ps2gs_caps){.abi=1,.features=51,
            .width=720,.height=576,.bits_per_pixel=32,.max_commands=64}; return 0;
    }
    if (op==PS2GS_SURFACE_CREATE) {
        struct ps2gs_surface_create *c=argument;
        assert(c->abi==1 && !c->handle && !c->reserved && !c->buffer_width && !c->allocated_bytes);
        c->handle=next_handle++; c->buffer_width=(c->width+63)&~63U;
        unsigned rows=(c->height+(c->bits_per_pixel==16?63:31)) & ~(c->bits_per_pixel==16?63U:31U);
        c->allocated_bytes=c->buffer_width*rows*(c->bits_per_pixel/8);
        if (corrupt_reply) ++c->buffer_width;
        ++creates; return 0;
    }
    if (op==PS2GS_SURFACE_DESTROY) {
        struct ps2gs_surface_destroy *d=argument;
        assert(d->handle && !d->reserved[0] && !d->reserved[1]); ++destroys; return 0;
    }
    if (op==PS2GS_SURFACE_FILL) { last_fill=*(struct ps2gs_surface_fill*)argument; return 0; }
    if (op==PS2GS_SURFACE_COPY) { last_copy=*(struct ps2gs_surface_copy*)argument; return 0; }
    assert(op==PS2GS_SURFACE_UPLOAD);
    const struct ps2gs_surface_upload *u=argument;
    assert(u->abi==1 && !u->reserved && u->bytes<=3072 && u->bytes==u->width*u->height*expected_cpp);
    for (unsigned row=0;row<u->height;++row)
        assert(!memcmp((const char *)(uintptr_t)u->pixels+row*u->width*expected_cpp,
            expected_pixels+(u->y-expected_y+row)*expected_stride+(u->x-expected_x)*expected_cpp,
            u->width*expected_cpp));
    ++uploads; return 0;
}
static void storage_case(void)
{
    retry_open=1; retry_ioctl=1; assert(!ps2_surface_open(&gs,"/dev/ps2-gs") && sleeps==2);
    uint32_t a,b; assert(!ps2_surface_create(&gs,65,33,32,&a));
    assert(!ps2_surface_create(&gs,66,34,32,&b));
    const struct ps2_surface *s=ps2_surface_lookup(&gs,a);
    assert(s && s->buffer_width==128 && s->bytes==32768);
    assert(!ps2_surface_fill(&gs,a,0,0,65,33,0x80402010));
    assert(last_fill.handle==a && last_fill.width==65 && last_fill.rgba==0x80402010);
    assert(!ps2_surface_copy(&gs,a,b,0,0,2,1,64,32));
    assert(last_copy.source==a && last_copy.destination==b && last_copy.dx==2);
    assert(!ps2_surface_copy(&gs,b,0,0,0,8,9,64,32) && last_copy.destination==0);
    assert(!ps2_surface_destroy(&gs,a) && !ps2_surface_lookup(&gs,a));
    uint32_t c; assert(!ps2_surface_create(&gs,1,1,16,&c) && c!=a);
    assert(ps2_surface_copy(&gs,b,c,0,0,0,0,4,1)==-1 && errno==EINVAL);
    /* Local invalid rectangles are rejected without another syscall. */
    unsigned before=calls; assert(ps2_surface_fill(&gs,a,0,0,1,1,0)==-1 && calls==before);
    ps2_surface_close(&gs); assert(closes==1);
}
static void uploads_case(void)
{
    assert(!ps2_surface_open(&gs,"/dev/ps2-gs"));
    uint32_t handle; assert(!ps2_surface_create(&gs,2048,3,32,&handle));
    unsigned char *pixels=malloc(3*8200); assert(pixels);
    for (unsigned i=0;i<3*8200;++i) pixels[i]=i*37;
    expected_pixels=pixels; expected_stride=8200; expected_cpp=4;
    assert(!ps2_surface_upload(&gs,handle,0,0,2048,3,pixels,3*8200,8200) && uploads==9);
    free(pixels);
    assert(!ps2_surface_destroy(&gs,handle));
    assert(!ps2_surface_create(&gs,17,13,16,&handle));
    unsigned char odd[13*40]; memset(odd,0x5a,sizeof(odd));
    expected_pixels=odd; expected_stride=40; expected_cpp=2;
    assert(!ps2_surface_upload(&gs,handle,0,0,17,13,odd,sizeof(odd),40) && uploads==10);
    unsigned before=calls;
    assert(ps2_surface_upload(&gs,handle,0,0,17,13,odd,sizeof(odd),SIZE_MAX)==-1 && errno==EINVAL && calls==before);
    ps2_surface_close(&gs);
}
static void errors_case(void)
{
    assert(!ps2_surface_open(&gs,"/dev/ps2-gs"));
    corrupt_reply=1; uint32_t handle=77;
    assert(ps2_surface_create(&gs,1,1,32,&handle)==-1 && errno==EPROTO && !handle);
    unsigned before=calls; assert(ps2_surface_fill(&gs,0,0,0,1,1,0)==-1 && calls==before);
    ps2_surface_close(&gs); corrupt_reply=0;
    assert(!ps2_surface_open(&gs,"/dev/ps2-gs"));
    fail_ioctl=ESTALE; assert(ps2_surface_fill(&gs,0,0,0,1,1,0)==-1 && errno==ESTALE);
    fail_ioctl=0; before=calls;
    assert(ps2_surface_fill(&gs,0,0,0,1,1,0)==-1 && calls==before);
    ps2_surface_close(&gs);
    assert(!ps2_surface_open(&gs,"/dev/ps2-gs"));
    fail_ioctl=EIO; assert(ps2_surface_fill(&gs,0,0,0,1,1,0)==-1 && errno==EIO);
    fail_ioctl=0; before=calls;
    assert(ps2_surface_fill(&gs,0,0,0,1,1,0)==-1 && calls==before);
    ps2_surface_close(&gs);
}
static void exa_case(void)
{
    assert(!ps2_surface_open(&gs,"/dev/ps2-gs"));
    ScreenRec screen={.GetScreenPixmap=get_front};
    void *private_slot=NULL; screen.devPrivates=(PrivatePtr)&private_slot;
    PixmapRec front={.drawable={.pScreen=&screen,.depth=24,.bitsPerPixel=32}};
    front_pixmap=&front;
    struct ps2_surface_exa ps2; assert(ps2_surface_exa_prepare(&screen,&ps2,&gs));
    ExaDriverRec *e=&ps2.driver; int pitch;
    void *front_private=e->CreatePixmap2(&screen,0,0,24,0,32,&pitch); assert(front_private);
    front.devPrivates=(PrivatePtr)front_private;
    assert(e->ModifyPixmapHeader(&front,720,576,24,32,2880,NULL));
    assert(e->PixmapIsOffscreen(&front) && front.drawable.width==720 && front.devPrivate.ptr==NULL);
    void *data=e->CreatePixmap2(&screen,66,33,24,0,32,&pitch); assert(data && pitch==512);
    PixmapRec pixmap={.drawable={.pScreen=&screen,.width=66,.height=33,.depth=24,.bitsPerPixel=32},
                      .devPrivates=(PrivatePtr)data};
    assert(e->PixmapIsOffscreen(&pixmap) && e->ModifyPixmapHeader(&pixmap,66,33,24,32,512,NULL));
    assert(!e->ModifyPixmapHeader(&pixmap,67,33,24,32,512,NULL));
    assert(!e->ModifyPixmapHeader(&pixmap,66,33,24,32,512,(void *)1));
    assert(e->PrepareSolid(&pixmap,GXcopy,0xffffff,0x00402010));
    e->Solid(&pixmap,0,0,66,33); e->DoneSolid(&pixmap);
    uint32_t handle=last_fill.handle; assert(handle && last_fill.rgba==0x80402010);
    assert(e->PrepareCopy(&pixmap,&front,1,1,GXcopy,0xffffff));
    e->Copy(&front,0,0,8,9,64,33); e->DoneCopy(&front);
    assert(last_copy.source==handle && !last_copy.destination);
    unsigned char pixels[40]; for(unsigned i=0;i<40;++i) pixels[i]=i;
    expected_pixels=pixels; expected_stride=20; expected_cpp=4; expected_x=3; expected_y=4;
    assert(e->UploadToScreen(&pixmap,3,4,3,2,(char*)pixels,20) && uploads==1);
    assert(!e->PrepareSolid(&pixmap,GXxor,0xffffff,0));
    expect_fatal=1; if (!setjmp(fatal)) { e->PrepareAccess(&pixmap,0); abort(); }
    assert(!expect_fatal);
    e->DestroyPixmap(&screen,data); e->DestroyPixmap(&screen,front_private);
    assert(destroys==1); ps2_surface_close(&gs);
}
static void mesa_case(void)
{
    assert(!ps2_surface_open(&gs,"/dev/ps2-gs"));
    struct pipe_resource template={.target=PIPE_TEXTURE_2D,.format=PIPE_FORMAT_R8G8B8X8_UNORM,
        .width0=66,.height0=33,.depth0=1,.array_size=1,.bind=PIPE_BIND_RENDER_TARGET|PIPE_BIND_SAMPLER_VIEW};
    struct pipe_resource *a=ps2_resource_create(NULL,&gs,&template),*b=ps2_resource_create(NULL,&gs,&template);
    assert(a && b && a->reference.count==1 && creates==2);
    unsigned char pixels[40]; memset(pixels,0x80,sizeof(pixels));
    struct pipe_box box={.x=3,.y=4,.width=3,.height=2,.depth=1};
    expected_pixels=pixels; expected_stride=20; expected_cpp=4; expected_x=3; expected_y=4;
    assert(!ps2_resource_upload(a,0,&box,pixels,sizeof(pixels),20));
    assert(ps2_resource_upload(a,1,&box,pixels,sizeof(pixels),20)==-1 && errno==ENOTSUP);
    box=(struct pipe_box){.width=64,.height=33,.depth=1};
    assert(!ps2_resource_copy(b,2,0,a,&box));
    assert(last_copy.source==((struct ps2_resource*)a)->handle && last_copy.destination==((struct ps2_resource*)b)->handle);
    template.last_level=1; assert(!ps2_resource_create(NULL,&gs,&template) && errno==ENOTSUP);
    template.last_level=0; template.format=PIPE_FORMAT_B8G8R8A8_UNORM;
    assert(!ps2_resource_create(NULL,&gs,&template) && errno==ENOTSUP && creates==2);
    ps2_resource_destroy(a); ps2_resource_destroy(b); assert(destroys==2);
    ps2_surface_close(&gs);
}

static void allocation_recovery_case(void)
{
    assert(!ps2_surface_open(&gs,"/dev/ps2-gs"));uint32_t old,next;
    assert(!ps2_surface_create(&gs,13,7,32,&old));
    struct ps2_surface saved=*ps2_surface_lookup(&gs,old);unsigned before=calls,before_creates=creates;
    create_error=ENOSPC;next=99;
    assert(ps2_surface_create(&gs,1024,1024,32,&next)<0 && errno==ENOSPC && !next && !gs.error);
    assert(probes==1 && calls==before+2 && creates==before_creates && !memcmp(ps2_surface_lookup(&gs,old),&saved,sizeof(saved)));
    assert(!ps2_surface_fill(&gs,old,0,0,1,1,0x80402010) && !ps2_surface_destroy(&gs,old));
    assert(!ps2_surface_create(&gs,13,7,32,&next) && next!=old && !ps2_surface_destroy(&gs,next));ps2_surface_close(&gs);
    const int failure[]={ENOSPC,EIO,0};
    for(unsigned i=0;i<3;++i){
        assert(!ps2_surface_open(&gs,"/dev/ps2-gs"));create_error=ENOSPC;probe_error=failure[i];probe_success=i==2;
        before=calls;assert(ps2_surface_create(&gs,1,1,32,&next)<0 && gs.error==(i==2?EPROTO:failure[i]) && !next && calls==before+2);
        before=calls;assert(ps2_surface_create(&gs,1,1,32,&next)<0 && calls==before);
        assert(ps2_surface_fill(&gs,0,0,0,1,1,0)<0 && calls==before);ps2_surface_close(&gs);
    }
    probe_error=probe_success=0;
    for(unsigned i=0;i<2;++i){
        assert(!ps2_surface_open(&gs,"/dev/ps2-gs"));create_error=i?EIO:ENOMEM;unsigned before_probes=probes;
        assert(ps2_surface_create(&gs,1,1,32,&next)<0 && gs.error==(i?EIO:ENOMEM) && probes==before_probes);
        before=calls;assert(ps2_surface_create(&gs,1,1,32,&next)<0 && calls==before);ps2_surface_close(&gs);
    }
    assert(!ps2_surface_open(&gs,"/dev/ps2-gs"));uint32_t held[PS2GS_MAX_SURFACES];
    for(unsigned i=0;i<PS2GS_MAX_SURFACES;++i)assert(!ps2_surface_create(&gs,1,1,32,held+i));
    before=calls;unsigned before_probes=probes;
    assert(ps2_surface_create(&gs,1,1,32,&next)<0 && errno==ENOSPC && !gs.error && !next && calls==before && probes==before_probes);
    assert(!ps2_surface_destroy(&gs,held[PS2GS_MAX_SURFACES-1]) && !ps2_surface_create(&gs,1,1,32,&next));
    assert(next!=held[PS2GS_MAX_SURFACES-1] && !ps2_surface_fill(&gs,next,0,0,1,1,0x80402010));
    held[PS2GS_MAX_SURFACES-1]=next;
    gs.error=EIO;before=calls;assert(ps2_surface_create(&gs,1,1,32,&next)<0 && errno==EIO && calls==before && gs.error==EIO);
    ps2_surface_close(&gs);
    puts("PS2 creation ENOSPC recovery, full handle table, ambiguous/protocol/IO failure guards PASS");
}
static void exa_allocation_recovery_case(void)
{
    assert(!ps2_surface_open(&gs,"/dev/ps2-gs"));ScreenRec screen={.GetScreenPixmap=get_front};
    PixmapRec front={.drawable={.pScreen=&screen,.depth=24,.bitsPerPixel=32}};front_pixmap=&front;
    void *slot=NULL;screen.devPrivates=(PrivatePtr)&slot;struct ps2_surface_exa owner;
    assert(ps2_surface_exa_prepare(&screen,&owner,&gs));ExaDriverRec *e=&owner.driver;int pitch;
    create_error=ENOSPC;unsigned before_probes=probes;
    assert(!e->CreatePixmap2(&screen,1024,1024,24,0,32,&pitch) && !gs.error && probes==before_probes+1);
    void *data=e->CreatePixmap2(&screen,13,7,24,0,32,&pitch);assert(data);
    PixmapRec p={.drawable={.pScreen=&screen,.width=13,.height=7,.depth=24,.bitsPerPixel=32},.devPrivates=(PrivatePtr)data};
    assert(e->PrepareSolid(&p,GXcopy,0xffffff,0x402010));e->Solid(&p,0,0,13,7);e->DoneSolid(&p);
    assert(last_fill.handle && last_fill.rgba==0x80402010);e->DestroyPixmap(&screen,data);ps2_surface_close(&gs);
    puts("PS2 EXA allocation rejection leaves subsequent hardware drawing usable PASS");
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if (!strcmp(argv[1],"allocation-recovery")) allocation_recovery_case();
    else if (!strcmp(argv[1],"exa-allocation-recovery")) exa_allocation_recovery_case();
    else if (!strcmp(argv[1],"storage")) storage_case();
    else if (!strcmp(argv[1],"uploads")) uploads_case();
    else if (!strcmp(argv[1],"errors")) errors_case();
    else if (!strcmp(argv[1],"exa")) exa_case();
    else if (!strcmp(argv[1],"mesa")) mesa_case();
    else return 2;
    puts("PS2 Xorg/Mesa owned surface component test passed"); return 0;
}
