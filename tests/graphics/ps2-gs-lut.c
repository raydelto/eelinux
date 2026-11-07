/* SPDX-License-Identifier: MIT */
#include "ps2-gs-lut.h"
#include "previous-batch-kernel.c"
static void lut_packets(void)
{
    struct ps2gs_buffer color = {.base=20*8192,.width=64,.height=32,.buffer_width=64,.bits_per_pixel=32};
    struct ps2gs_buffer texture=color,palette=color;texture.base=30*8192;palette.base=40*8192;
    for(unsigned w=1;w<=64;++w)for(unsigned h=1;h<=32;++h)for(unsigned channel=0;channel<3;++channel){
        unsigned total=DIV_ROUND_UP(w,4)*DIV_ROUND_UP(h,2),covered[64*32]={0};
        for(unsigned first=0;first<total;first+=48){
            union package p[258];memset(p,0x5a,sizeof(p));
            struct ps2gs_lut_operation op={.texture=&texture,.palette=&palette,.width=w,.height=h,
                .mask=255U<<(8*channel),.channel=channel,.first=first,.count=min_t(unsigned,total-first,48)};
            size_t n=ps2gs_package_lut(p,&color,&op);assert(n==19+op.count*4 && n<=256);
            assert(p[n].gif.doubleword[0]==UINT64_C(0x5a5a5a5a5a5a5a5a));
            assert(p[0].gif.tag.nloop==n-1 && p[n-1].gif.doubleword[1]==gs_addr_finish);
            assert(p[1].gif.packed.ad.data.frame_1.fbmsk==~op.mask);
            uint64_t tex=p[7].gif.doubleword[0];
            assert((tex&0x3fff)==texture.base/256 && ((tex>>20)&63)==0x13 && ((tex>>37)&0x3fff)==palette.base/256);
            for(unsigned i=0;i<op.count;++i){
                unsigned k=17+4*i;
                unsigned u=(p[k].gif.doubleword[0]&0x3fff)/16,v=((p[k].gif.doubleword[0]>>16)&0x3fff)/16;
                unsigned x=p[k+1].gif.packed.ad.data.xyz2.x/16,y=p[k+1].gif.packed.ad.data.xyz2.y/16;
                unsigned x2=p[k+3].gif.packed.ad.data.xyz2.x/16,y2=p[k+3].gif.packed.ad.data.xyz2.y/16;
                assert(x2<=w && y2<=h && u+x2-x<=128 && v+y2-y<=64);
                for(unsigned yy=y;yy<y2;++yy)for(unsigned xx=x;xx<x2;++xx){
                    ++covered[yy*64+xx];
                    /* Compare byte-address bits in each format, independently
                     * of the packet's inverse coordinate expression. */
                    unsigned tx=u+xx-x,ty=v+yy-y;
                    assert(tx/16==xx/8 && ty/16==yy/8);
                    unsigned ct=channel|((xx&1)<<2)|((yy&1)<<3)|((xx&2)<<3)|((xx&4)<<3)|((yy&2)<<5)|((yy&4)<<5);
                    unsigned t8=((ty&2)>>1)|((tx&8)>>2)|((tx&1)<<2)|((ty&1)<<3)|((tx&2)<<3)|
                        ((((tx>>2)^(ty>>1)^(ty>>2))&1)<<5)|((ty&4)<<4)|((ty&8)<<4);
                    assert(ct==t8);
                }
            }
            assert(!p[n-2].gif.packed.ad.data.frame_1.fbmsk);
        }
        for(unsigned y=0;y<32;++y)for(unsigned x=0;x<64;++x)assert(covered[y*64+x]==(x<w && y<h));
    }
}
static uint32_t make_lut_surface(struct file *f,unsigned width,unsigned height)
{
    struct ps2gs_surface_create r={.abi=1,.width=width,.height=height,.bits_per_pixel=32};
    assert(!call(f,PS2GS_SURFACE_CREATE,&r));return r.handle;
}
static void lut_validation(int fail)
{
    struct fb_info *info=setup();struct file file;vc.vc_mode=KD_GRAPHICS;assert(!open_file(info,&file));
    struct ps2gs_caps caps;assert(!call(&file,PS2GS_GET_CAPS,&caps) && (caps.features&PS2GS_HW_CHANNEL_LUT));
    struct ps2gs_channel_lut r={.abi=1,.source=make_lut_surface(&file,64,32),
        .destination=make_lut_surface(&file,64,32),.palette=make_lut_surface(&file,16,16),
        .width=64,.height=32,.plane_mask=0xffffff};
    unsigned before=dma_calls;
    if(fail){hardware_fail=1;assert(call(&file,PS2GS_CHANNEL_LUT,&r)==-ETIMEDOUT);
        hardware_fail=0;assert(call(&file,PS2GS_CHANNEL_LUT,&r)==-ETIMEDOUT && dma_calls==before);
    }else{
        for(unsigned i=0;i<12;++i){struct ps2gs_channel_lut bad=r;
            switch(i){case 0:bad.abi=2;break;case 1:bad.reserved=1;break;case 2:bad.width=0;break;
            case 3:bad.width=65;break;case 4:bad.height=33;break;case 5:bad.plane_mask=0x1000000;break;
            case 6:bad.source=0;break;case 7:bad.source=bad.destination;break;case 8:bad.palette=bad.source;break;
            case 9:bad.source=999;break;case 10:bad.palette=r.destination;break;case 11:bad.palette=make_lut_surface(&file,17,16);before=dma_calls;break;}
            assert(call(&file,PS2GS_CHANNEL_LUT,&bad)<0 && dma_calls==before);}
        assert(!call(&file,PS2GS_CHANNEL_LUT,&r) && dma_calls==before+18);before=dma_calls;
        r.plane_mask=0;assert(!call(&file,PS2GS_CHANNEL_LUT,&r) && dma_calls==before);
        r.width=1;r.height=1;r.plane_mask=0x00ff00;assert(!call(&file,PS2GS_CHANNEL_LUT,&r) && dma_calls==before+1);
    }
    assert(!ps2gs_device_release(NULL,&file));ps2gs_device_unregister(info);free_info(info);
}
int main(int argc,char **argv)
{
    assert(argc==2);mutex_init(&console);
    if(!strcmp(argv[1],"packets"))lut_packets();else if(!strcmp(argv[1],"validation"))lut_validation(0);
    else if(!strcmp(argv[1],"failure"))lut_validation(1);else return 2;
    pthread_mutex_destroy(&console.value);puts("GS channel lookup PASS");return 0;
}
