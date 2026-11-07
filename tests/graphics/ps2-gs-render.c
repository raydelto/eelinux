/* SPDX-License-Identifier: MIT */
#include "ps2-gs-render.h"
#define main previous_device_main
#include "ps2-gs-device.c"
#undef main

static struct ps2gs_command commands[3]; /* no-PIE host address fits the o32 ABI */
static struct ps2gs_buffer *make_color(struct fb_info *info, struct file *file)
{
    struct ps2gs_surface_create c={.abi=1,.width=65,.height=33,.bits_per_pixel=32};
    assert(!call(file,PS2GS_SURFACE_CREATE,&c));
    return ps2gs_arena_lookup(&((struct ps2gs_session*)file->private_data)->arena,c.handle);
}
static struct ps2gs_buffer *make_depth(struct fb_info *info,struct file *file,unsigned bpp)
{
    struct ps2gs_depth_create d={.abi=1,.width=65,.height=33,.color_bits_per_pixel=bpp};
    assert(!call(file,PS2GS_DEPTH_CREATE,&d));
    assert(d.buffer_width==128 && d.allocated_bytes==16384);
    return ps2gs_arena_lookup(&((struct ps2gs_session*)file->private_data)->arena,d.handle);
}
static void depth_case(void)
{
    struct fb_info *info=setup(); struct file file; vc.vc_mode=KD_GRAPHICS;
    assert(!open_file(info,&file)); struct ps2gs_caps caps;
    assert(!call(&file,PS2GS_GET_CAPS,&caps) && caps.features==127);
    struct ps2gs_buffer *d=make_depth(info,&file,32);
    unsigned slot=d-((struct ps2gs_session*)file.private_data)->arena.buffers;
    assert(ps2gs_depth_role(file.private_data,d->handle)==32);
    union package *p=info->par->package.buffer;
    assert(p[1].gif.packed.ad.data.frame_1.fbmsk==0xffffffff);
    assert(p[4].gif.packed.ad.data.zbuf.zbp==d->base/8192 && p[4].gif.packed.ad.data.zbuf.psm==0xa);
    assert(p[11].gif.packed.ad.data.xyz2.x==128*16 && p[11].gif.packed.ad.data.xyz2.y==64*16);
    struct ps2gs_surface_fill f={.abi=1,.handle=d->handle,.width=1,.height=1};
    unsigned before=dma_calls; assert(call(&file,PS2GS_SURFACE_FILL,&f)==-ENOENT && dma_calls==before);
    struct ps2gs_surface_destroy destroy={.abi=1,.handle=d->handle};
    assert(!call(&file,PS2GS_SURFACE_DESTROY,&destroy));
    assert(!((struct ps2gs_session*)file.private_data)->depth_color_bpp[slot]);
    struct ps2gs_depth_create bad={.abi=1,.width=1,.height=1,.color_bits_per_pixel=24};
    assert(call(&file,PS2GS_DEPTH_CREATE,&bad)==-EINVAL && dma_calls==before);
    assert(!ps2gs_device_release(NULL,&file)); ps2gs_device_unregister(info); free_info(info);
}
static void batch_case(void)
{
    struct fb_info *info=setup(); struct file file; vc.vc_mode=KD_GRAPHICS; assert(!open_file(info,&file));
    struct ps2gs_buffer *c=make_color(info,&file),*d=make_depth(info,&file,32);
    struct ps2gs_render_submit r={.abi=1,.target=c->handle,.depth=d->handle,.count=2,
        .width=65,.height=33,.bits_per_pixel=32,.commands=(uintptr_t)commands};
    commands[0]=(struct ps2gs_command){.op=PS2GS_CLEAR_DEPTH};
    commands[1]=(struct ps2gs_command){.op=PS2GS_TRIANGLE,.flags=PS2GS_DEPTH_TEST,
        .v={{.x=16,.y=16,.z=40000,.rgba=0x800000ff},{.x=64*16,.y=16,.z=40000},
            {.x=32*16,.y=32*16,.z=40000}}};
    unsigned before=dma_calls; commands[1].v[2].z=65536;
    assert(call(&file,PS2GS_RENDER_SUBMIT,&r)==-EINVAL && dma_calls==before);
    commands[1].v[2].z=40000; assert(!call(&file,PS2GS_RENDER_SUBMIT,&r) && dma_calls==before+2);
    before=dma_calls; r.target=d->handle;
    assert(call(&file,PS2GS_RENDER_SUBMIT,&r)==-ENOENT && dma_calls==before);
    r.target=c->handle; r.depth=c->handle;
    assert(call(&file,PS2GS_RENDER_SUBMIT,&r)==-ENOENT && dma_calls==before);
    r.depth=0; assert(call(&file,PS2GS_RENDER_SUBMIT,&r)==-ENOSPC && dma_calls==before);
    struct ps2gs_buffer *wrong=make_depth(info,&file,16); r.depth=wrong->handle; before=dma_calls;
    assert(call(&file,PS2GS_RENDER_SUBMIT,&r)==-EINVAL && dma_calls==before);
    r.depth=d->handle; r.width=66; assert(call(&file,PS2GS_RENDER_SUBMIT,&r)==-ESTALE && dma_calls==before);
    assert(!ps2gs_device_release(NULL,&file)); ps2gs_device_unregister(info); free_info(info);
}
static void packets_case(void)
{
    union package p[256]; struct ps2gs_buffer c={.base=20*8192,.width=65,.height=33,.buffer_width=128,.bits_per_pixel=32};
    struct ps2gs_buffer d={.base=40*8192,.width=65,.height=33,.buffer_width=128,.bits_per_pixel=16};
    struct ps2gs_command tri={.op=PS2GS_TRIANGLE,.flags=PS2GS_DEPTH_TEST,
        .v={{.x=16,.y=16,.z=5,.rgba=0x800000ff},{.x=64*16,.y=16,.z=5},
            {.x=32*16,.y=32*16,.z=5}}};
    assert(!ps2gs_validate_render(&c,&d,32,&tri));
    assert(ps2gs_package_render(p,&c,&d,&tri)==15);
    assert(p[1].gif.packed.ad.data.frame_1.fbp==20 && p[1].gif.packed.ad.data.frame_1.fbw==2);
    assert(p[4].gif.packed.ad.data.zbuf.zbp==40 && p[4].gif.packed.ad.data.zbuf.psm==0xa && !p[4].gif.packed.ad.data.zbuf.zmsk);
    assert(p[5].gif.packed.ad.data.test_1.ztst==gs_depth_gequal && p[7].gif.packed.ad.data.prim.ctxt==gs_context_2);
    struct ps2gs_command clear={.op=PS2GS_CLEAR_DEPTH}; c.bits_per_pixel=16;
    assert(!ps2gs_validate_render(&c,&d,16,&clear));
    assert(ps2gs_package_render(p,&c,&d,&clear)==13 && p[1].gif.packed.ad.data.frame_1.fbmsk==0xffffffff);
    assert(p[4].gif.packed.ad.data.zbuf.psm==2 && p[5].gif.packed.ad.data.test_1.ztst==gs_depth_pass);
}
static void failure_case(void)
{
    struct fb_info *info=setup(); struct file file; vc.vc_mode=KD_GRAPHICS; assert(!open_file(info,&file));
    struct ps2gs_buffer *c=make_color(info,&file);
    commands[0]=(struct ps2gs_command){.op=PS2GS_FILL,.width=1,.height=1};
    struct ps2gs_render_submit r={.abi=1,.target=c->handle,.count=1,.width=65,.height=33,.bits_per_pixel=32,.commands=(uintptr_t)commands};
    ++info->par->gs_epoch; assert(call(&file,PS2GS_RENDER_SUBMIT,&r)==-ESTALE); --info->par->gs_epoch;
    hardware_fail=1; unsigned before=dma_calls;
    assert(call(&file,PS2GS_RENDER_SUBMIT,&r)==-ETIMEDOUT && dma_calls==before);
    hardware_fail=0; assert(call(&file,PS2GS_RENDER_SUBMIT,&r)==-ETIMEDOUT && dma_calls==before);
    assert(!ps2gs_device_release(NULL,&file)); ps2gs_device_unregister(info); free_info(info);
}
int main(int argc,char **argv)
{
    assert(argc==2); mutex_init(&console);
    if (!strcmp(argv[1],"depth")) depth_case(); else if (!strcmp(argv[1],"batch")) batch_case();
    else if (!strcmp(argv[1],"packets")) packets_case(); else if (!strcmp(argv[1],"failure")) failure_case(); else assert(0);
    pthread_mutex_destroy(&console.value); puts("owned render kernel test PASS"); return 0;
}
