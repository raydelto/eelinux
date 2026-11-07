/* SPDX-License-Identifier: MIT */
#include "ps2-gs-transfer.h"
#include "ps2-gs-logic.h"
#include "ps2-gs-readback.h"
#include "previous-render-kernel.c"
static unsigned char reply[8192];
static void packets(void)
{
    union package p[8];struct ps2gs_buffer b={.base=20*8192,.width=65,.height=33,.buffer_width=128,.bits_per_pixel=32};
    struct ps2gs_surface_download r={.abi=1,.x=1,.y=1,.width=64,.height=32,.bytes=8192};
    assert(!ps2gs_validate_download(&b,&r));assert(ps2gs_package_download(p,&b,&r)==6);
    assert(p[1].gif.packed.ad.data.bitbltbuf.sbp==20*32 && p[1].gif.packed.ad.data.bitbltbuf.sbw==2);
    assert(p[2].gif.packed.ad.data.trxpos.ssax==1 && p[2].gif.packed.ad.data.trxpos.ssay==1);
    assert(p[3].gif.packed.ad.data.trxreg.rrw==64 && p[3].gif.packed.ad.data.trxreg.rrh==32);
    assert(p[4].gif.packed.ad.addr==gs_addr_finish && p[5].gif.packed.ad.data.trxdir.xdir==gs_trxdir_local_to_host);
    b.bits_per_pixel=16;r.bytes=4096;assert(!ps2gs_validate_download(&b,&r));
    assert(ps2gs_package_download(p,&b,&r)==6 && p[1].gif.packed.ad.data.bitbltbuf.spsm==gs_psm_ct16);
}
static void validation(void)
{
    struct fb_info *info=setup();struct file f;vc.vc_mode=KD_GRAPHICS;assert(!open_file(info,&f));
    struct ps2gs_buffer *c=make_color(info,&f),*z=make_depth(info,&f,32);
    struct ps2gs_surface_download r={.abi=1,.handle=c->handle,.width=64,.height=32,.bytes=8192,.pixels=(uintptr_t)reply};
    assert(!call(&f,PS2GS_SURFACE_DOWNLOAD,&r) && rx_reads==512 && !rx_fdr && !rx_direction);
    for(unsigned i=0;i<8192;++i)assert(reply[i]==0x5a);
    unsigned before=rx_reads;
    for(unsigned i=0;i<10;++i){struct ps2gs_surface_download bad=r;
        switch(i){case 0:bad.reserved=1;break;case 1:bad.bytes=8193;break;case 2:bad.bytes=128;break;case 3:bad.width=63;break;
        case 4:bad.x=2;break;case 5:bad.y=2;break;case 6:bad.handle=z->handle;break;case 7:bad.pixels=UINT64_MAX;break;
        case 8:bad.pixels=UINT32_MAX-10;break;case 9:bad.width=UINT32_MAX;break;}
        assert(call(&f,PS2GS_SURFACE_DOWNLOAD,&bad)<0 && rx_reads==before);}
    rx_busy=1;assert(call(&f,PS2GS_SURFACE_DOWNLOAD,&r)==-EBUSY && rx_reads==before);rx_busy=0;
    assert(!call(&f,PS2GS_SURFACE_DOWNLOAD,&r));
    fail_copy_out=1;assert(call(&f,PS2GS_SURFACE_DOWNLOAD,&r)==-EFAULT);fail_copy_out=0;
    assert(!call(&f,PS2GS_SURFACE_DOWNLOAD,&r));
    assert(!ps2gs_device_release(NULL,&f));ps2gs_device_unregister(info);free_info(info);
}
static void failure(void)
{
    struct fb_info *info=setup();struct file f;vc.vc_mode=KD_GRAPHICS;assert(!open_file(info,&f));
    struct ps2gs_surface_download r={.abi=1,.width=64,.height=32,.bytes=8192,.pixels=(uintptr_t)reply};
    rx_stall=1;assert(call(&f,PS2GS_SURFACE_DOWNLOAD,&r)==-ETIMEDOUT && !rx_fdr && !rx_direction && !rx_reads);
    unsigned before=dma_calls;rx_stall=0;assert(call(&f,PS2GS_SURFACE_DOWNLOAD,&r)==-ETIMEDOUT && dma_calls==before);
    assert(!ps2gs_device_release(NULL,&f));ps2gs_device_unregister(info);free_info(info);
}
int main(int argc,char **argv)
{
    assert(argc==2);mutex_init(&console);
    if(!strcmp(argv[1],"packets"))packets();else if(!strcmp(argv[1],"validation"))validation();
    else if(!strcmp(argv[1],"failure"))failure();else return 2;
    pthread_mutex_destroy(&console.value);puts("PS2 native readback PASS");return 0;
}
