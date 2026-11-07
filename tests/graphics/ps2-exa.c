/* SPDX-License-Identifier: MIT */
#include "ps2_exa.h"
#include "os.h"
#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static jmp_buf failed;
static int expect_fatal, io_error;
static unsigned submissions;
static unsigned uploads;
static PixmapPtr front_pixmap;
static struct ps2gs_command captured;
void FatalError(const char *format, ...)
{
    assert(expect_fatal && format);
    expect_fatal=0; longjmp(failed,1);
}
Bool dixRegisterPrivateKey(DevPrivateKey key, DevPrivateType type, unsigned size)
{
    assert(type==PRIVATE_SCREEN && !size);
    *key=(DevPrivateKeyRec){.initialized=TRUE,.type=type}; return TRUE;
}
static PixmapPtr get_screen_pixmap(ScreenPtr screen)
{ assert(front_pixmap->drawable.pScreen==screen); return front_pixmap; }
int __wrap_ioctl(int fd, unsigned long op, ...)
{
    assert(fd==999);
    va_list args; va_start(args,op);
    void *argument=va_arg(args,void *); va_end(args);
    if (op==PS2GS_UPLOAD) {
        struct ps2gs_upload *u=argument;
        assert(u->x==11 && u->y==12 && u->image_width==3 && u->image_height==2);
        assert(u->bytes==12);
        const unsigned char expected[12]={1,2,3,4,5,6,9,10,11,12,13,14};
        assert(!memcmp((void *)(uintptr_t)u->pixels,expected,12));
        ++uploads;
        if (io_error) { errno=io_error; return -1; }
        return 0;
    }
    assert(op==PS2GS_SUBMIT);
    struct ps2gs_submit *s=argument;
    assert(s->count==1);
    captured=*(struct ps2gs_command *)(uintptr_t)s->commands;
    ++submissions;
    if (io_error) { errno=io_error; return -1; }
    return 0;
}
int main(int argc, char **argv)
{
    assert(argc==2);
    static struct ps2_gs_client gs={.fd=999,
        .caps={.abi=1,.features=15,.width=720,.height=576,.bits_per_pixel=16,.max_commands=64}};
    ScreenRec screen={.GetScreenPixmap=get_screen_pixmap};
    void *private_slot=NULL; screen.devPrivates=(PrivatePtr)&private_slot;
    PixmapRec pixmap={.drawable={.pScreen=&screen,.width=720,.height=576,.depth=15,.bitsPerPixel=16}};
    front_pixmap=&pixmap;
    struct ps2_exa ps2;
    assert(ps2_exa_prepare(&screen,&ps2,&gs));
    ExaDriverRec *e=&ps2.driver;
    if (!strcmp(argv[1],"solid")) {
        assert(e->PrepareSolid(&pixmap,GXcopy,0x7fff,0x1f));
        e->Solid(&pixmap,10,20,30,40);
        assert(gs.count==1 && gs.commands[0].v[0].rgba==0x800000ff);
        e->DoneSolid(&pixmap);
        assert(submissions==1 && captured.width==20 && captured.height==20);
        assert(captured.v[0].x==160 && captured.v[0].y==320);
        e->WaitMarker(&screen,123); assert(submissions==1);
        assert(!e->PrepareSolid(&pixmap,GXxor,0x7fff,0));
        assert(!e->PrepareSolid(&pixmap,GXcopy,0x7ffe,0));
        pixmap.drawable.depth=16;
        assert(!e->PrepareSolid(&pixmap,GXcopy,0xffff,0));
        pixmap.drawable.depth=15;
        PixmapRec other=pixmap;
        assert(!e->PrepareSolid(&other,GXcopy,0x7fff,0));
    } else if (!strcmp(argv[1],"copy")) {
        assert(e->PrepareCopy(&pixmap,&pixmap,-1,-1,GXcopy,0x7fff));
        e->Copy(&pixmap,0,1,2,3,4,5); e->DoneCopy(&pixmap);
        assert(submissions==1 && captured.op==PS2GS_COPY && captured.width==4);
        assert(captured.v[0].y==16 && captured.v[1].x==32);
        assert(!e->PrepareCopy(&pixmap,&pixmap,1,1,GXxor,0x7fff));
        PixmapRec other=pixmap;
        assert(!e->PrepareCopy(&other,&pixmap,1,1,GXcopy,0x7fff));
    } else if (!strcmp(argv[1],"failures")) {
        expect_fatal=1;
        if (!setjmp(failed)) { e->PrepareAccess(&pixmap,0); abort(); }
        assert(!expect_fatal && !submissions);
        assert(e->PrepareSolid(&pixmap,GXcopy,0x7fff,0));
        expect_fatal=1;
        if (!setjmp(failed)) { e->Solid(&pixmap,-1,0,3,3); abort(); }
        assert(!submissions);
        assert(e->PrepareCopy(&pixmap,&pixmap,1,1,GXcopy,0x7fff));
        expect_fatal=1;
        if (!setjmp(failed)) { e->Copy(&pixmap,0,0,1,1,3,2); abort(); }
        assert(gs.error==ENOTSUP && !submissions && !gs.count);
        gs.error=0; e->Solid(&pixmap,0,0,4,4); io_error=EIO;
        expect_fatal=1;
        if (!setjmp(failed)) { e->DoneSolid(&pixmap); abort(); }
        assert(gs.error==EIO && submissions==1 && !gs.count);
        expect_fatal=1;
        if (!setjmp(failed)) { e->WaitMarker(&screen,0); abort(); }
        assert(submissions==1);
    } else if (!strcmp(argv[1],"upload")) {
        gs.caps.features |= PS2GS_HW_UPLOAD;
        char source[14]={1,2,3,4,5,6,99,99,9,10,11,12,13,14};
        assert(e->PrepareSolid(&pixmap,GXcopy,0x7fff,0)); e->Solid(&pixmap,0,0,4,4);
        assert(e->UploadToScreen(&pixmap,11,12,3,2,source,8));
        assert(uploads==1 && submissions==1 && !gs.count);
        PixmapRec other=pixmap;
        assert(!e->UploadToScreen(&other,11,12,3,2,source,8));
        expect_fatal=1;
        if (!setjmp(failed)) { e->UploadToScreen(&pixmap,11,12,3,2,source,-8); abort(); }
        assert(uploads==1);
        io_error=ETIMEDOUT; expect_fatal=1;
        if (!setjmp(failed)) { e->UploadToScreen(&pixmap,11,12,3,2,source,8); abort(); }
        assert(uploads==2 && gs.error==ETIMEDOUT);
        expect_fatal=1;
        if (!setjmp(failed)) { e->UploadToScreen(&pixmap,11,12,3,2,source,8); abort(); }
        assert(uploads==2);
    } else return 2;
    puts("PS2 EXA callback test passed"); return 0;
}
