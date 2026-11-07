/* SPDX-License-Identifier: MIT */
#include "ps2-gs-rgba-transfer.h"
#include "ps2-gs-map-blit.h"
#include "ps2-gs-map.h"
#include "ps2-gs-lut.h"
#include "ps2-gs-logic.h"
#include "ps2-gs-batch.h"
#include "ps2-gs-readback.h"
#define min_t(type,a,b) ((type)(a)<(type)(b)?(type)(a):(type)(b))
#define max_t(type,a,b) ((type)(a)>(type)(b)?(type)(a):(type)(b))
#include "previous-render-kernel.c"
static struct ps2gs_texture_triangle triangles[2];
static void packets(void)
{
    struct ps2gs_buffer d={.base=20*8192,.width=65,.height=33,.buffer_width=128,.bits_per_pixel=32};
    struct ps2gs_buffer s={.base=30*8192,.width=64,.height=32,.buffer_width=64,.bits_per_pixel=32};
    const uint32_t masks[]={UINT32_MAX,0xff000000,0x5731a5cf,0x80000000,0x010101,0};
    for(unsigned mode=0;mode<=PS2GS_RGBA_MULTIPLY;++mode)for(unsigned m=0;m<sizeof(masks)/sizeof(*masks);++m){
        union package p[32];memset(p,0x5a,sizeof(p));uint32_t mask=mode?masks[m]&0xffffff:masks[m];
        size_t n=ps2gs_package_rgba_transfer(p,&d,&s,triangles,mask,mode);assert(n==29);
        assert(p[0].gif.tag.nloop==n-1 && p[n].gif.doubleword[0]==UINT64_C(0x5a5a5a5a5a5a5a5a));
        assert(p[1].gif.packed.ad.data.frame_1.fbmsk==~mask);
        assert(p[7].gif.doubleword[1]==gs_addr_tex0_2 && (p[7].gif.doubleword[0]&(1ULL<<34)));
        assert(((p[7].gif.doubleword[0]>>35)&3)==1); /* RGBA DECAL */
        assert(p[12].gif.packed.ad.addr==gs_addr_alpha_2);
        assert(p[12].gif.packed.ad.data.alpha_1.c==(mode?0:2));
        assert(p[12].gif.packed.ad.data.alpha_1.d==(mode?2:1));
        assert(p[16].gif.packed.ad.data.prim.abe==!!mode);
        assert(p[n-3].gif.packed.ad.addr==gs_addr_frame_2 && !p[n-3].gif.packed.ad.data.frame_1.fbmsk);
        assert(p[n-2].gif.doubleword[1]==gs_addr_colclamp && p[n-2].gif.doubleword[0]==1);
        assert(p[n-1].gif.doubleword[1]==gs_addr_finish);
    }
}
static void validation(void)
{
    struct fb_info *info=setup();struct file f;vc.vc_mode=KD_GRAPHICS;assert(!open_file(info,&f));
    struct ps2gs_caps caps;assert(!call(&f,PS2GS_GET_CAPS,&caps) && (caps.features&PS2GS_HW_RGBA_TRANSFER));
    struct ps2gs_buffer *d=make_color(info,&f),*s=make_color(info,&f),*z=make_depth(info,&f,32);
    struct ps2gs_texture_transfer r={.draw={.abi=1,.target=d->handle,.texture=s->handle,.count=2,
        .width=65,.height=33,.bits_per_pixel=32,.triangles=(uintptr_t)triangles},.plane_mask=UINT32_MAX};
    for(unsigned i=0;i<2;++i)for(unsigned j=0;j<3;++j)triangles[i].v[j].q=0x3f800000;
    unsigned before=dma_calls;
    triangles[1].v[2].q=0x7fc00000;assert(call(&f,PS2GS_TEXTURE_RGBA_TRANSFER,&r)==-EINVAL && dma_calls==before);
    triangles[1].v[2].q=0x3f800000;assert(!call(&f,PS2GS_TEXTURE_RGBA_TRANSFER,&r) && dma_calls==before+2);before=dma_calls;
    for(unsigned i=0;i<12;++i){struct ps2gs_texture_transfer bad=r;
        switch(i){case 0:bad.mode=2;break;case 1:bad.mode=1;break;case 2:bad.draw.depth=z->handle;break;
        case 3:bad.draw.texture=z->handle;break;case 4:bad.draw.target=z->handle;break;case 5:bad.draw.texture=d->handle;break;
        case 6:bad.draw.count=65;break;case 7:bad.draw.width=64;break;case 8:bad.draw.target=0;break;
        case 9:bad.draw.texture=0;break;case 10:bad.draw.abi=2;break;case 11:bad.draw.bits_per_pixel=16;break;}
        assert(call(&f,PS2GS_TEXTURE_RGBA_TRANSFER,&bad)<0 && dma_calls==before);}
    triangles[1].flags=PS2GS_TEXTURE_MODULATE;
    assert(call(&f,PS2GS_TEXTURE_RGBA_TRANSFER,&r)==-EINVAL && dma_calls==before);triangles[1].flags=0;
    r.plane_mask=0xffffff;r.mode=PS2GS_RGBA_MULTIPLY;
    assert(!call(&f,PS2GS_TEXTURE_RGBA_TRANSFER,&r) && dma_calls==before+2);before=dma_calls;
    r.plane_mask=0;assert(!call(&f,PS2GS_TEXTURE_RGBA_TRANSFER,&r) && dma_calls==before);
    assert(!ps2gs_device_release(NULL,&f));ps2gs_device_unregister(info);free_info(info);
}
static void failure(void)
{
    struct fb_info *info=setup();struct file f;vc.vc_mode=KD_GRAPHICS;assert(!open_file(info,&f));
    struct ps2gs_buffer *d=make_color(info,&f),*s=make_color(info,&f);
    struct ps2gs_texture_transfer r={.draw={.abi=1,.target=d->handle,.texture=s->handle,.count=1,
        .width=65,.height=33,.bits_per_pixel=32,.triangles=(uintptr_t)triangles},.plane_mask=UINT32_MAX};
    unsigned before=dma_calls;hardware_fail=1;assert(call(&f,PS2GS_TEXTURE_RGBA_TRANSFER,&r)==-ETIMEDOUT);
    hardware_fail=0;assert(call(&f,PS2GS_TEXTURE_RGBA_TRANSFER,&r)==-ETIMEDOUT && dma_calls==before);
    assert(!ps2gs_device_release(NULL,&f));ps2gs_device_unregister(info);free_info(info);
}
int main(int argc,char **argv)
{
    assert(argc==2);mutex_init(&console);
    for(unsigned i=0;i<2;++i)for(unsigned j=0;j<3;++j)triangles[i].v[j].q=0x3f800000;
    if(!strcmp(argv[1],"packets"))packets();else if(!strcmp(argv[1],"validation"))validation();
    else if(!strcmp(argv[1],"failure"))failure();else return 2;
    pthread_mutex_destroy(&console.value);puts("PS2 GS RGBA transfer PASS");return 0;
}
