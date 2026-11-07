/* SPDX-License-Identifier: MIT */
#include "ps2-gs-map-blit.h"
#ifndef max_t
#define max_t(type,a,b) ((type)(a)>(type)(b)?(type)(a):(type)(b))
#endif
#include "previous-map-kernel.c"
static void blit_packets(void)
{
    struct ps2gs_buffer color={.base=20*8192,.width=97,.height=73,.buffer_width=128,.bits_per_pixel=32};
    struct ps2gs_buffer texture=color,palette=color;texture.base=30*8192;palette.base=40*8192;
    for(unsigned sx=0;sx<64;++sx)for(unsigned sy=0;sy<32;++sy)for(unsigned variant=0;variant<3;++variant)
    for(unsigned channel=0;channel<4;++channel){
        unsigned w=variant==0?1:variant==1?(64-sx<7?64-sx:7):64-sx;
        unsigned h=variant==0?1:variant==1?(32-sy<5?32-sy:5):32-sy;
        unsigned total=DIV_ROUND_UP((sx&3)+w,4)*DIV_ROUND_UP((sy&1)+h,2),covered[97*73]={0};
        for(unsigned first=0;first<total;first+=48){
            union package p[258];memset(p,0x5a,sizeof(p));
            struct ps2gs_map_blit_operation op={.lookup={.texture=&texture,.palette=&palette,.width=w,.height=h,
                .mask=0x5731a5cf,.channel=channel,.first=first,.count=min_t(unsigned,total-first,48)},.sx=sx,.sy=sy,.dx=7,.dy=9};
            size_t n=ps2gs_package_map_blit(p,&color,&op);assert(n==19+op.lookup.count*4 && n<=256);
            assert(p[n].gif.doubleword[0]==UINT64_C(0x5a5a5a5a5a5a5a5a));
            assert(p[1].gif.packed.ad.data.frame_1.fbw==2 && p[n-2].gif.packed.ad.data.frame_1.fbw==2);
            assert(p[3].gif.packed.ad.data.scissor_1.scax0==7 && p[3].gif.packed.ad.data.scissor_1.scay0==9);
            assert(p[3].gif.packed.ad.data.scissor_1.scax1==7+w-1 && p[3].gif.packed.ad.data.scissor_1.scay1==9+h-1);
            assert(((p[7].gif.doubleword[0]>>34)&3)==3);
            for(unsigned i=0;i<op.lookup.count;++i){unsigned k=17+4*i;
                unsigned u=(p[k].gif.doubleword[0]&0x3fff)/16,v=((p[k].gif.doubleword[0]>>16)&0x3fff)/16;
                unsigned x=p[k+1].gif.packed.ad.data.xyz2.x/16,y=p[k+1].gif.packed.ad.data.xyz2.y/16;
                unsigned x2=p[k+3].gif.packed.ad.data.xyz2.x/16,y2=p[k+3].gif.packed.ad.data.xyz2.y/16;
                assert(x>=7 && y>=9 && x2<=7+w && y2<=9+h && x<x2 && y<y2);
                for(unsigned yy=y;yy<y2;++yy)for(unsigned xx=x;xx<x2;++xx){
                    ++covered[yy*97+xx];unsigned px=sx+xx-7,py=sy+yy-9,tx=u+xx-x,ty=v+yy-y;
                    assert(tx/16==px/8 && ty/16==py/8);
                    unsigned ct=channel|((px&1)<<2)|((py&1)<<3)|((px&2)<<3)|((px&4)<<3)|((py&2)<<5)|((py&4)<<5);
                    unsigned t8=((ty&2)>>1)|((tx&8)>>2)|((tx&1)<<2)|((ty&1)<<3)|((tx&2)<<3)|
                        ((((tx>>2)^(ty>>1)^(ty>>2))&1)<<5)|((ty&4)<<4)|((ty&8)<<4);
                    assert(ct==t8);
                }
            }
        }
        for(unsigned y=0;y<73;++y)for(unsigned x=0;x<97;++x)assert(covered[y*97+x]==(x>=7 && x<7+w && y>=9 && y<9+h));
    }
}
static void blit_validation(int fail)
{
    struct fb_info *info=setup();struct file file;vc.vc_mode=KD_GRAPHICS;assert(!open_file(info,&file));
    struct ps2gs_caps caps;assert(!call(&file,PS2GS_GET_CAPS,&caps) && (caps.features&PS2GS_HW_CHANNEL_MAP_BLIT));
    struct ps2gs_channel_map_blit r={.abi=1,.source=make_lut_surface(&file,83,61),
        .destination=make_lut_surface(&file,97,73),.palette=make_lut_surface(&file,16,16),
        .width=17,.height=11,.plane_mask=UINT32_MAX,.source_channel=3,.sx=65,.sy=33,.dx=13,.dy=7};
    unsigned before=dma_calls;
    if(fail){hardware_fail=1;assert(call(&file,PS2GS_CHANNEL_MAP_BLIT,&r)==-ETIMEDOUT);
        hardware_fail=0;assert(call(&file,PS2GS_CHANNEL_MAP_BLIT,&r)==-ETIMEDOUT && dma_calls==before);
    }else{
        for(unsigned i=0;i<15;++i){struct ps2gs_channel_map_blit bad=r;
            switch(i){case 0:bad.abi=2;break;case 1:bad.source_channel=4;break;case 2:bad.width=0;break;
            case 3:bad.width=65;break;case 4:bad.height=33;break;case 5:bad.sx=UINT32_MAX;break;
            case 6:bad.source=0;break;case 7:bad.source=bad.destination;break;case 8:bad.palette=bad.source;break;
            case 9:bad.source=999;break;case 10:bad.dx=UINT32_MAX;break;case 11:bad.sy=31;break;
            case 12:bad.sx=63;break;case 13:bad.dy=70;break;
            case 14:bad.palette=make_lut_surface(&file,17,16);before=dma_calls;break;}
            assert(call(&file,PS2GS_CHANNEL_MAP_BLIT,&bad)<0 && dma_calls==before);
        }
        assert(!call(&file,PS2GS_CHANNEL_MAP_BLIT,&r) && dma_calls==before+1);before=dma_calls;
        r.plane_mask=0;assert(!call(&file,PS2GS_CHANNEL_MAP_BLIT,&r) && dma_calls==before);
        r.source=make_lut_surface(&file,1,1);before=dma_calls;r.sx=r.sy=0;r.width=r.height=1;r.plane_mask=0xff000000;
        assert(!call(&file,PS2GS_CHANNEL_MAP_BLIT,&r) && dma_calls==before+1);
    }
    assert(!ps2gs_device_release(NULL,&file));ps2gs_device_unregister(info);free_info(info);
}
int main(int argc,char **argv)
{
    assert(argc==2);mutex_init(&console);
    if(!strcmp(argv[1],"packets"))blit_packets();else if(!strcmp(argv[1],"validation"))blit_validation(0);
    else if(!strcmp(argv[1],"failure"))blit_validation(1);else return 2;
    pthread_mutex_destroy(&console.value);puts("GS channel map blit PASS");return 0;
}
