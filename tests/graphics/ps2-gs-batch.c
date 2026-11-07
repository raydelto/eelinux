/* SPDX-License-Identifier: MIT */
#include "ps2-gs-batch.h"
#include "ps2-gs-transfer.h"
#include "ps2-gs-readback.h"
#define min_t(t,a,b) ((t)(a)<(t)(b)?(t)(a):(t)(b))
#include "previous-logic-kernel.c"
static struct ps2gs_solid_logic rectangles[64];
static struct ps2gs_command draws[64];
static struct ps2gs_texture_triangle textures[64];
static void joined(void)
{
    struct ps2gs_buffer color={.base=0x180000,.width=65,.height=33,.buffer_width=128,.bits_per_pixel=32};
    union package joined[258],single[32];
    for(unsigned bpp=16;bpp<=32;bpp+=16)for(unsigned alu=0;alu<16;++alu){
        color.bits_per_pixel=bpp;uint32_t bits=bpp==16?0x7fff:0xffffff;
        for(unsigned i=0;i<8;++i)rectangles[i]=(struct ps2gs_solid_logic){.abi=1,.x=i,.y=2,
            .width=3,.height=4,.color=(0x782135*i)&bits,.plane_mask=(0xab529f+i)&bits,.alu=alu};
        memset(joined,0x5a,sizeof(joined));
        struct ps2gs_draw_batch batch={.operation=PS2GS_SOLID_LOGIC,.count=8,.requests=rectangles};
        size_t n=ps2gs_package_batch(joined,&color,NULL,&batch),offset=1;
        assert(n<=256 && joined[0].gif.tag.nloop==n-1 && joined[n-1].gif.doubleword[1]==gs_addr_finish);
        uint32_t pixel=bpp==16?0xf3a6:0xaba35e79;
        for(unsigned i=0;i<8;++i){
            size_t m=ps2gs_package_solid_logic(single,&color,rectangles+i);
            assert(!memcmp(joined+offset,single+1,(m-2)*sizeof(*single)));
            pixel=packet_pixel(single,m,pixel,bpp);
            offset+=m-2;
        }
        uint32_t expected=bpp==16?0xf3a6:0xaba35e79;
        for(unsigned i=0;i<8;++i)expected=(expected&~rectangles[i].plane_mask)|
            (oracle(alu,rectangles[i].color,expected)&rectangles[i].plane_mask);
        assert(pixel==expected && offset==n-1);
        assert(joined[n].gif.doubleword[0]==UINT64_C(0x5a5a5a5a5a5a5a5a));
        for(unsigned i=1;i<n-1;++i)assert(joined[i].gif.doubleword[1]!=gs_addr_finish);
    }
}
static void solids(void)
{
    struct fb_info *info=setup();struct file file;vc.vc_mode=KD_GRAPHICS;assert(!open_file(info,&file));
    struct ps2gs_caps caps;assert(!call(&file,PS2GS_GET_CAPS,&caps) && (caps.features&PS2GS_HW_SOLID_BATCH));
    struct ps2gs_solid_batch request={.abi=1,.count=64,.width=128,.height=96,.bits_per_pixel=32,
        .plane_mask=0xf13a77,.alu=6,.rectangles=(uintptr_t)rectangles};
    for(unsigned i=0;i<64;++i)rectangles[i]=(struct ps2gs_solid_logic){.abi=1,.x=i,.y=i,
        .width=1,.height=1,.color=i*375,.plane_mask=request.plane_mask,.alu=request.alu};
    unsigned before=dma_calls;assert(!call(&file,PS2GS_SOLID_BATCH,&request) && dma_calls==before+8);
    assert(info->par->gs_stats.fills==64 && info->par->gs_stats.completed_batches==8);
    before=dma_calls;
    for(unsigned i=0;i<10;++i){
        struct ps2gs_solid_logic saved=rectangles[63];
        switch(i){case 0:rectangles[63].x=UINT32_MAX;break;case 1:rectangles[63].width=0;break;
        case 2:rectangles[63].color=0x1000000;break;case 3:rectangles[63].alu=3;break;
        case 4:rectangles[63].plane_mask=0xffffff;break;case 5:rectangles[63].handle=1;break;
        case 6:rectangles[63].reserved=1;break;case 7:rectangles[63].abi=2;break;
        case 8:rectangles[63].height=UINT32_MAX;break;case 9:rectangles[63].y=96;break;}
        assert(call(&file,PS2GS_SOLID_BATCH,&request)==-EINVAL && dma_calls==before);rectangles[63]=saved;
    }
    request.count=65;assert(call(&file,PS2GS_SOLID_BATCH,&request)==-EINVAL && dma_calls==before);
    request.count=64;request.width=127;assert(call(&file,PS2GS_SOLID_BATCH,&request)==-ESTALE);
    assert(!ps2gs_device_release(NULL,&file));ps2gs_device_unregister(info);free_info(info);
}
static void rendering(void)
{
    struct fb_info *info=setup();struct file file;vc.vc_mode=KD_GRAPHICS;assert(!open_file(info,&file));
    struct ps2gs_buffer *color=make_color(info,&file),*texture=make_color(info,&file);
    for(unsigned i=0;i<64;++i)draws[i]=(struct ps2gs_command){.op=PS2GS_FILL,.width=1,.height=1,
        .v={{.x=i*16,.rgba=0x80332211}}};
    struct ps2gs_render_submit render={.abi=1,.target=color->handle,.count=64,.width=65,.height=33,
        .bits_per_pixel=32,.commands=(uintptr_t)draws};
    unsigned before=dma_calls;assert(!call(&file,PS2GS_RENDER_SUBMIT,&render) && dma_calls==before+8);
    for(unsigned i=0;i<64;++i){textures[i]=(struct ps2gs_texture_triangle){0};
        for(unsigned j=0;j<3;++j)textures[i].v[j].q=0x3f800000;}
    struct ps2gs_texture_transfer transfer={.draw={.abi=1,.target=color->handle,.texture=texture->handle,
        .count=64,.width=65,.height=33,.bits_per_pixel=32,.triangles=(uintptr_t)textures},.plane_mask=0xffffff};
    before=dma_calls;assert(!call(&file,PS2GS_TEXTURE_TRANSFER,&transfer) && dma_calls==before+8);
    before=dma_calls;assert(!call(&file,PS2GS_TEXTURE_SUBMIT,&transfer.draw) && dma_calls==before+8);
    assert(!ps2gs_device_release(NULL,&file));ps2gs_device_unregister(info);free_info(info);
}
static void batch_failure(void)
{
    struct fb_info *info=setup();struct file file;vc.vc_mode=KD_GRAPHICS;assert(!open_file(info,&file));
    rectangles[0]=(struct ps2gs_solid_logic){.abi=1,.width=1,.height=1,.plane_mask=0xffffff,.alu=6};
    struct ps2gs_solid_batch request={.abi=1,.count=1,.width=128,.height=96,.bits_per_pixel=32,
        .plane_mask=0xffffff,.alu=6,.rectangles=(uintptr_t)rectangles};
    ++info->par->gs_epoch;assert(call(&file,PS2GS_SOLID_BATCH,&request)==-ESTALE && !dma_calls);
    --info->par->gs_epoch;hardware_fail=1;assert(call(&file,PS2GS_SOLID_BATCH,&request)==-ETIMEDOUT && !dma_calls);
    hardware_fail=0;assert(call(&file,PS2GS_SOLID_BATCH,&request)==-ETIMEDOUT && !dma_calls);
    assert(!ps2gs_device_release(NULL,&file));ps2gs_device_unregister(info);free_info(info);
}
int main(int argc,char **argv)
{
    assert(argc==2);mutex_init(&console);
    if(!strcmp(argv[1],"packets"))joined();else if(!strcmp(argv[1],"solid"))solids();
    else if(!strcmp(argv[1],"render"))rendering();else if(!strcmp(argv[1],"failure"))batch_failure();else return 2;
    pthread_mutex_destroy(&console.value);puts("GS ordered batch PASS");return 0;
}
