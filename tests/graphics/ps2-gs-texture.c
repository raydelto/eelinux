/* SPDX-License-Identifier: MIT */
#include "ps2-gs-texture.h"
#include "previous-render-kernel.c"
static struct ps2gs_texture_triangle triangles[2];
static struct ps2gs_texture_triangle good(void)
{
    struct ps2gs_texture_triangle t={.flags=PS2GS_DEPTH_TEST|PS2GS_TEXTURE_MODULATE};
    t.v[0]=(struct ps2gs_texture_vertex){.position={.x=16,.y=16,.z=30000,.rgba=0x80808080},.q=0x3f800000};
    t.v[1]=(struct ps2gs_texture_vertex){.position={.x=64*16,.y=16,.z=30000,.rgba=0x80808080},.s=0x3f000000,.q=0x3f000000};
    t.v[2]=(struct ps2gs_texture_vertex){.position={.x=32*16,.y=32*16,.z=30000,.rgba=0x80808080},.t=0x3f800000,.q=0x3f800000};
    return t;
}
static void texture_batch(void)
{
    struct fb_info *info=setup(); struct file file; vc.vc_mode=KD_GRAPHICS; assert(!open_file(info,&file));
    struct ps2gs_buffer *c=make_color(info,&file), *d=make_depth(info,&file,32), *t=make_color(info,&file);
    struct ps2gs_texture_submit r={.abi=1,.target=c->handle,.depth=d->handle,.texture=t->handle,
        .count=2,.width=65,.height=33,.bits_per_pixel=32,.triangles=(uintptr_t)triangles};
    triangles[0]=triangles[1]=good(); triangles[1].v[2].q=0x7f800000;
    unsigned before=dma_calls; assert(call(&file,PS2GS_TEXTURE_SUBMIT,&r)==-EINVAL && dma_calls==before);
    triangles[1]=good(); assert(!call(&file,PS2GS_TEXTURE_SUBMIT,&r) && dma_calls==before+2);
    before=dma_calls; r.texture=d->handle; assert(call(&file,PS2GS_TEXTURE_SUBMIT,&r)==-ENOENT && dma_calls==before);
    r.texture=c->handle; assert(call(&file,PS2GS_TEXTURE_SUBMIT,&r)==-EOPNOTSUPP && dma_calls==before);
    r.texture=t->handle; r.width=64; assert(call(&file,PS2GS_TEXTURE_SUBMIT,&r)==-ESTALE && dma_calls==before);
    assert(!ps2gs_device_release(NULL,&file)); ps2gs_device_unregister(info); free_info(info);
}
static void texture_packets(void)
{
    union package p[256]; struct ps2gs_buffer c={.base=20*8192,.width=65,.height=33,.buffer_width=128,.bits_per_pixel=32};
    struct ps2gs_buffer d={.base=40*8192,.width=65,.height=33,.buffer_width=128,.bits_per_pixel=16};
    struct ps2gs_buffer t={.base=30*8192,.width=65,.height=33,.buffer_width=128,.bits_per_pixel=16};
    struct ps2gs_texture_triangle tri=good(); assert(!ps2gs_validate_texture(&c,&d,32,&t,&tri));
    assert(ps2gs_package_texture(p,&c,&d,&t,&tri)==23);
    assert(p[6].gif.packed.ad.addr==gs_addr_texflush);
    assert(p[7].gif.packed.ad.addr==gs_addr_tex0_2 && p[7].gif.packed.ad.data.tex0.tbp0==30*32);
    assert(p[7].gif.packed.ad.data.tex0.tw==7 && p[7].gif.packed.ad.data.tex0.th==6 && p[7].gif.packed.ad.data.tex0.tbw==2);
    assert(p[7].gif.packed.ad.data.tex0.psm==gs_psm_ct16 && p[7].gif.packed.ad.data.tex0.tfx==gs_tfx_modulate);
    assert(p[9].gif.packed.ad.data.clamp_2.maxu==64 && p[9].gif.packed.ad.data.clamp_2.maxv==32);
    assert(p[9].gif.packed.ad.data.clamp_2.wms==gs_clamp_region_clamp);
    assert(p[12].gif.packed.ad.data.prim.tme && !p[12].gif.packed.ad.data.prim.fst);
    assert(p[16].gif.packed.ad.data.rgbaq.q==0x3f000000 && p[17].gif.packed.ad.addr==gs_addr_st);
    assert(p[17].gif.word[0]==0x3f000000 && p[17].gif.word[1]==0);
    assert(p[5].gif.packed.ad.data.test_1.ztst==gs_depth_gequal && p[4].gif.packed.ad.data.zbuf.psm==0xa);
}
static void texture_validation(void)
{
    struct ps2gs_buffer c={.width=65,.height=33,.buffer_width=128,.bits_per_pixel=32},t=c;
    struct ps2gs_texture_triangle tri=good(); tri.flags=0;
    assert(!ps2gs_validate_texture(&c,NULL,0,&t,&tri));
    tri.v[0].s=0x80000000; assert(ps2gs_validate_texture(&c,NULL,0,&t,&tri)==-EINVAL);
    tri=good(); tri.flags=0; tri.v[0].q=0x39000000; assert(ps2gs_validate_texture(&c,NULL,0,&t,&tri)==-EINVAL);
    tri=good(); tri.flags=0; tri.v[0].s=0x7fc00000; assert(ps2gs_validate_texture(&c,NULL,0,&t,&tri)==-EINVAL);
    tri=good(); tri.flags=0; t.width=1025; assert(ps2gs_validate_texture(&c,NULL,0,&t,&tri)==-EOPNOTSUPP);
    t=c; assert(ps2gs_validate_texture(&c,NULL,0,&c,&tri)==-EOPNOTSUPP);
    tri.flags=4; assert(ps2gs_validate_texture(&c,NULL,0,&t,&tri)==-EOPNOTSUPP);
}
static void texture_failure(void)
{
    struct fb_info *info=setup(); struct file file; vc.vc_mode=KD_GRAPHICS; assert(!open_file(info,&file));
    struct ps2gs_buffer *c=make_color(info,&file),*t=make_color(info,&file); triangles[0]=good(); triangles[0].flags=0;
    struct ps2gs_texture_submit r={.abi=1,.target=c->handle,.texture=t->handle,.count=1,
        .width=65,.height=33,.bits_per_pixel=32,.triangles=(uintptr_t)triangles};
    ++info->par->gs_epoch; assert(call(&file,PS2GS_TEXTURE_SUBMIT,&r)==-ESTALE); --info->par->gs_epoch;
    hardware_fail=1; unsigned before=dma_calls; assert(call(&file,PS2GS_TEXTURE_SUBMIT,&r)==-ETIMEDOUT);
    hardware_fail=0; assert(call(&file,PS2GS_TEXTURE_SUBMIT,&r)==-ETIMEDOUT && before==dma_calls);
    assert(!ps2gs_device_release(NULL,&file)); ps2gs_device_unregister(info); free_info(info);
}
int main(int argc,char **argv)
{
    assert(argc==2); mutex_init(&console);
    if (!strcmp(argv[1],"batch")) texture_batch(); else if (!strcmp(argv[1],"packets")) texture_packets();
    else if (!strcmp(argv[1],"validation")) texture_validation(); else if (!strcmp(argv[1],"failure")) texture_failure(); else assert(0);
    pthread_mutex_destroy(&console.value); puts("owned texture kernel test PASS"); return 0;
}
