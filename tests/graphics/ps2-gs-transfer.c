/* SPDX-License-Identifier: MIT */
#include "ps2-gs-transfer.h"
#include "ps2-gs-logic.h"
#include "previous-render-kernel.c"
static struct ps2gs_texture_triangle triangles[2];
static void packet(void)
{
    struct ps2gs_buffer c={.base=20*8192,.width=65,.height=33,.buffer_width=128,.bits_per_pixel=32};
    struct ps2gs_buffer t={.base=30*8192,.width=64,.height=32,.buffer_width=64,.bits_per_pixel=32};
    struct ps2gs_texture_triangle tri={0};
    for(unsigned i=0;i<3;++i)tri.v[i].q=0x3f800000;
    for(unsigned mode=0;mode<4;++mode)for(unsigned bpp=16;bpp<=32;bpp+=16){
        union package p[32];memset(p,0x5a,sizeof(p));c.bits_per_pixel=bpp;
        uint32_t mask=bpp==16?0x514a:0x82a1f0;
        size_t n=ps2gs_package_transfer(p,&c,&t,&tri,mask,mode);assert(n==29);
        assert(p[0].gif.tag.nloop==n-1 && p[n].gif.doubleword[0]==UINT64_C(0x5a5a5a5a5a5a5a5a));
        assert(p[1].gif.packed.ad.data.frame_1.fbmsk==~ps2gs_logic_rgb(mask,bpp));
        assert(p[11].gif.doubleword[0]==(mode!=PS2GS_TRANSFER_ADD));
        assert(p[12].gif.packed.ad.addr==gs_addr_alpha_2);
        assert(p[12].gif.packed.ad.data.alpha_1.fix==128 && p[12].gif.packed.ad.data.alpha_1.d==gs_alpha_d_cd);
        assert(p[12].gif.packed.ad.data.alpha_1.a==(mode==2?gs_alpha_a_0:gs_alpha_a_cs));
        assert(p[12].gif.packed.ad.data.alpha_1.b==(mode==2?gs_alpha_b_cs:gs_alpha_b_0));
        assert(p[16].gif.packed.ad.data.prim.abe==(mode!=0));
        assert(p[n-3].gif.packed.ad.addr==gs_addr_frame_2 && !p[n-3].gif.packed.ad.data.frame_1.fbmsk);
        assert(p[n-2].gif.doubleword[1]==gs_addr_colclamp && p[n-2].gif.doubleword[0]==1);
        assert(p[n-1].gif.doubleword[1]==gs_addr_finish);
    }
}
static void validation(void)
{
    struct fb_info *info=setup();struct file f;vc.vc_mode=KD_GRAPHICS;assert(!open_file(info,&f));
    struct ps2gs_caps caps;assert(!call(&f,PS2GS_GET_CAPS,&caps) && (caps.features&PS2GS_HW_TEXTURE_TRANSFER));
    struct ps2gs_buffer *c=make_color(info,&f),*t=make_color(info,&f),*z=make_depth(info,&f,32);
    struct ps2gs_texture_transfer r={.draw={.abi=1,.target=c->handle,.texture=t->handle,.count=2,
        .width=65,.height=33,.bits_per_pixel=32,.triangles=(uintptr_t)triangles},.plane_mask=0x82a1f0,.mode=1};
    for(unsigned i=0;i<2;++i)for(unsigned j=0;j<3;++j)triangles[i].v[j].q=0x3f800000;
    unsigned before=dma_calls;
    triangles[1].v[2].q=0x7fc00000;assert(call(&f,PS2GS_TEXTURE_TRANSFER,&r)==-EINVAL && dma_calls==before);
    triangles[1].v[2].q=0x3f800000;assert(!call(&f,PS2GS_TEXTURE_TRANSFER,&r) && dma_calls==before+2);before=dma_calls;
    for(unsigned i=0;i<8;++i){struct ps2gs_texture_transfer bad=r;
        switch(i){case 0:bad.mode=4;break;case 1:bad.plane_mask=0x1000000;break;case 2:bad.draw.depth=z->handle;break;
        case 3:bad.draw.texture=z->handle;break;case 4:bad.draw.target=z->handle;break;case 5:bad.draw.texture=c->handle;break;
        case 6:bad.draw.count=65;break;case 7:bad.draw.width=64;break;}
        assert(call(&f,PS2GS_TEXTURE_TRANSFER,&bad)<0 && dma_calls==before);}
    r.plane_mask=0;assert(!call(&f,PS2GS_TEXTURE_TRANSFER,&r) && dma_calls==before);
    assert(!ps2gs_device_release(NULL,&f));ps2gs_device_unregister(info);free_info(info);
}
static void failure(void)
{
    struct fb_info *info=setup();struct file f;vc.vc_mode=KD_GRAPHICS;assert(!open_file(info,&f));
    struct ps2gs_buffer *c=make_color(info,&f),*t=make_color(info,&f);
    struct ps2gs_texture_transfer r={.draw={.abi=1,.target=c->handle,.texture=t->handle,.count=1,
        .width=65,.height=33,.bits_per_pixel=32,.triangles=(uintptr_t)triangles},.plane_mask=0xffffff};
    for(unsigned i=0;i<3;++i)triangles[0].v[i].q=0x3f800000;
    unsigned before=dma_calls;hardware_fail=1;assert(call(&f,PS2GS_TEXTURE_TRANSFER,&r)==-ETIMEDOUT);
    hardware_fail=0;assert(call(&f,PS2GS_TEXTURE_TRANSFER,&r)==-ETIMEDOUT && dma_calls==before);
    assert(!ps2gs_device_release(NULL,&f));ps2gs_device_unregister(info);free_info(info);
}
int main(int argc,char **argv)
{
    assert(argc==2);mutex_init(&console);
    if(!strcmp(argv[1],"packets"))packet();
    else if(!strcmp(argv[1],"validation"))validation();
    else if(!strcmp(argv[1],"failure"))failure();
    else return 2;
    pthread_mutex_destroy(&console.value);puts("PS2 GS transfer PASS");return 0;
}
