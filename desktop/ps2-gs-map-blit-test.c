/* SPDX-License-Identifier: MIT */
/* Reply readback is used only for the independent native diagnostic oracle. */
#define PS2_MAP_DIAGNOSTIC_NO_MAIN
#include "ps2-gs-map-test.c"
#include "ps2_gs_logic.h"
#include "ps2_gs_transfer.h"
static uint32_t boolean_result(unsigned alu,uint32_t s,uint32_t d)
{
    switch(alu){case 0:return 0;case 1:return s&d;case 2:return s&~d;case 3:return s;
    case 4:return ~s&d;case 5:return d;case 6:return s^d;case 7:return s|d;
    case 8:return ~(s|d);case 9:return ~(s^d);case 10:return ~d;case 11:return s|~d;
    case 12:return ~s;case 13:return ~s|d;case 14:return ~(s&d);default:return UINT32_MAX;}
}
static int download_destination(struct ps2_surface_client *gs,uint32_t dest,uint32_t *actual)
{
    for(unsigned y=0;y<80;y+=16)
        if(ps2_surface_download(gs,dest,0,y,96,16,actual+y*96,96*16*4))return -1;
    return 0;
}
int main(void)
{
    if(ps2_native_map_check())return 1;
    struct ps2_surface_client gs={.fd=-1};
    int tty=open("/dev/tty0",O_RDWR|O_CLOEXEC),graphics=0,status=1,mode;
    uint32_t source=0,dest=0,palette=0,tiny=0,input[83*61],initial[96*80],actual[96*80];
    unsigned checks=0,solid_checks=0,alpha_checks=0,rgba_copy_checks=0,rgba_multiply_checks=0;
    if(tty<0 || ioctl(tty,KDGETMODE,&mode) || mode!=KD_TEXT || ioctl(tty,KDSETMODE,KD_GRAPHICS))goto out;
    graphics=1;
    if(ps2_surface_open(&gs,"/dev/ps2-gs") || !(gs.caps.features&PS2GS_HW_CHANNEL_MAP_BLIT) ||
        ps2_surface_create(&gs,83,61,32,&source) || ps2_surface_create(&gs,96,80,32,&dest) ||
        ps2_surface_create(&gs,16,16,32,&palette) || ps2_surface_create(&gs,1,1,32,&tiny) ||
        ps2_surface_set_depth(&gs,source,32) || ps2_surface_set_depth(&gs,dest,32))goto out;
    for(unsigned i=0;i<83*61;++i)input[i]=(i&255)|((i*73&255)<<8)|((i*193&255)<<16)|((i*127&255)<<24);
    for(unsigned i=0;i<96*80;++i)initial[i]=0xa58fc329U^(i*0x01030507U);
    uint32_t tiny_pixel=0x817f3b19;
    if(ps2_surface_upload(&gs,source,0,0,83,61,input,sizeof(input),83*4) ||
        ps2_surface_upload(&gs,tiny,0,0,1,1,&tiny_pixel,4,4))goto out;
    const uint32_t masks[]={UINT32_MAX,0xff000000,0x5731a5cf,0};
    const unsigned rectangles[][6]={{0,0,64,32,7,9},{1,1,7,5,13,33},{3,1,61,31,3,1},
        {63,31,1,1,95,79},{64,32,19,29,1,2},{65,33,17,11,77,61},{82,60,1,1,94,78},
        {0,32,64,29,17,43},{0,0,1,1,95,79}};
    for(unsigned channel=0;channel<4;++channel)for(unsigned size=0;size<9;++size)
    for(unsigned m=0;m<4;++m){
        uint32_t table[256],mask=masks[m];const unsigned *r=rectangles[size];
        for(unsigned i=0;i<256;++i)table[i]=i|((255-i)<<8)|((i*73&255)<<16)|((i*193&255)<<24);
        if(ps2_surface_upload(&gs,dest,0,0,96,80,initial,sizeof(initial),96*4) ||
            ps2_surface_map_palette(&gs,palette,table) ||
            ps2_surface_channel_map_blit(&gs,size==8?tiny:source,dest,palette,r[0],r[1],r[4],r[5],r[2],r[3],mask,channel) ||
            download_destination(&gs,dest,actual))goto out;
        for(unsigned y=0;y<80;++y)for(unsigned x=0;x<96;++x){unsigned i=y*96+x;uint32_t wanted=initial[i];
            if(x>=r[4] && y>=r[5] && x<r[4]+r[2] && y<r[5]+r[3]){
                uint32_t value=size==8?tiny_pixel:input[(r[1]+y-r[5])*83+r[0]+x-r[4]];
                wanted=(wanted&~mask)|(table[(value>>(channel*8))&255]&mask);
            }
            if(actual[i]!=wanted){fprintf(stderr,"ps2-map-blit: mismatch channel=%u rect=%u mask=%x x=%u y=%u wanted=%08x got=%08x\n",
                channel,size,mask,x,y,wanted,actual[i]);goto out;}++checks;
        }
    }
    unsigned char alpha[83*61];
    if(ps2_surface_alpha_read(&gs,source,0,0,83,61,alpha,sizeof(alpha)))goto out;
    for(unsigned i=0;i<sizeof(alpha);++i){if(alpha[i]!=(input[i]>>24)){fprintf(stderr,"ps2-alpha-read: mismatch index=%u\n",i);goto out;}++alpha_checks;}
    if(ps2_surface_upload(&gs,dest,0,0,96,80,initial,sizeof(initial),96*4) ||
        ps2_surface_solid_logic(&gs,dest,3,5,71,43,0x7f3b19,0xffffff,1) ||
        download_destination(&gs,dest,actual))goto out;
    printf("ps2-rgba-debug: RGB-only before=%08x after=%08x\n",initial[5*96+3],actual[5*96+3]);
    uint32_t debug_surface=0,debug_pixels[2048],debug_table[256];
    for(unsigned i=0;i<256;++i)debug_table[i]=(i&129U)<<24;
    if(ps2_surface_create(&gs,64,32,32,&debug_surface) ||
        ps2_surface_upload(&gs,dest,0,0,96,80,initial,sizeof(initial),96*4) ||
        ps2_surface_map_palette(&gs,palette,debug_table) ||
        ps2_surface_channel_map_blit(&gs,dest,debug_surface,palette,3,5,0,0,61,27,0xff000000,3) ||
        ps2_surface_download(&gs,debug_surface,0,0,64,32,debug_pixels,sizeof(debug_pixels)))goto out;
    printf("ps2-rgba-debug: alpha mapping before=%08x after=%08x wanted=%08x\n",initial[5*96+3],
        debug_pixels[0],debug_table[initial[5*96+3]>>24]);
    if(ps2_surface_destroy(&gs,debug_surface))goto out;
    uint32_t debug_rgba=0;
    if(ps2_surface_create(&gs,64,32,32,&debug_surface) || ps2_surface_create(&gs,64,32,32,&debug_rgba) ||
        ps2_surface_upload(&gs,dest,0,0,96,80,initial,sizeof(initial),96*4))goto out;
    for(unsigned i=0;i<256;++i)debug_table[i]=i&129U;
    if(ps2_surface_map_palette(&gs,palette,debug_table) ||
        ps2_surface_channel_map_blit(&gs,dest,debug_surface,palette,3,5,0,0,61,27,255,3) ||
        ps2_surface_download(&gs,debug_surface,0,0,64,32,debug_pixels,sizeof(debug_pixels)))goto out;
    printf("ps2-rgba-debug: alpha truth red=%08x %08x %08x %08x wanted=%02x %02x %02x %02x\n",
        debug_pixels[0],debug_pixels[1],debug_pixels[2],debug_pixels[3],
        (initial[5*96+3]>>24)&129U,(initial[5*96+4]>>24)&129U,(initial[5*96+5]>>24)&129U,(initial[5*96+6]>>24)&129U);
    for(unsigned i=0;i<256;++i)debug_table[i]=i | i<<24;
    if(ps2_surface_map_palette(&gs,palette,debug_table) ||
        ps2_surface_channel_map(&gs,debug_surface,debug_rgba,palette,61,27,UINT32_MAX,0) ||
        ps2_surface_download(&gs,debug_rgba,0,0,64,32,debug_pixels,sizeof(debug_pixels)))goto out;
    printf("ps2-rgba-debug: alpha truth RGBA=%08x %08x %08x %08x\n",
        debug_pixels[0],debug_pixels[1],debug_pixels[2],debug_pixels[3]);
    if(ps2_surface_rgba_rectangle(&gs,debug_rgba,dest,0,0,3,5,61,27,0xff000000,PS2GS_RGBA_REPLACE) ||
        download_destination(&gs,dest,actual))goto out;
    printf("ps2-rgba-debug: alpha truth copied=%08x %08x %08x %08x\n",
        actual[5*96+3],actual[5*96+4],actual[5*96+5],actual[5*96+6]);
    if(ps2_surface_destroy(&gs,debug_rgba) || ps2_surface_destroy(&gs,debug_surface))goto out;
    if(gs.caps.features&PS2GS_HW_RGBA_TRANSFER){
        for(unsigned size=0;size<9;++size)for(unsigned m=0;m<4;++m){const unsigned *r=rectangles[size];uint32_t mask=masks[m];
            if(ps2_surface_upload(&gs,dest,0,0,96,80,initial,sizeof(initial),96*4) ||
                ps2_surface_rgba_rectangle(&gs,size==8?tiny:source,dest,r[0],r[1],r[4],r[5],r[2],r[3],mask,PS2GS_RGBA_REPLACE) ||
                download_destination(&gs,dest,actual))goto out;
            for(unsigned y=0;y<80;++y)for(unsigned x=0;x<96;++x){unsigned i=y*96+x;uint32_t wanted=initial[i];
                if(x>=r[4] && y>=r[5] && x<r[4]+r[2] && y<r[5]+r[3]){
                    uint32_t value=size==8?tiny_pixel:input[(r[1]+y-r[5])*83+r[0]+x-r[4]];
                    wanted=(wanted&~mask)|(value&mask);}
                if(actual[i]!=wanted){fprintf(stderr,"ps2-rgba-copy: mismatch rect=%u mask=%x x=%u y=%u wanted=%08x got=%08x\n",
                    size,mask,x,y,wanted,actual[i]);goto out;}++rgba_copy_checks;
            }
        }
        uint32_t packed[2048];const uint32_t multiply_masks[]={0xffffff,0x31a5cf,0};
        for(unsigned page=0;page<32;++page){for(unsigned i=0;i<2048;++i){unsigned pair=page*2048+i,a=pair>>8,b=pair&255;
                packed[i]=a|((255-a)<<8)|((a*73&255)<<16)|(b<<24);}
            if(ps2_surface_upload(&gs,source,0,0,64,32,packed,sizeof(packed),64*4))goto out;
            for(unsigned m=0;m<3;++m){uint32_t mask=multiply_masks[m];
                if(ps2_surface_upload(&gs,dest,0,0,96,80,initial,sizeof(initial),96*4) ||
                    ps2_surface_rgba_rectangle(&gs,source,dest,0,0,3,5,64,32,mask,PS2GS_RGBA_MULTIPLY) ||
                    download_destination(&gs,dest,actual))goto out;
                for(unsigned y=0;y<80;++y)for(unsigned x=0;x<96;++x){unsigned i=y*96+x;uint32_t wanted=initial[i];
                    if(x>=3 && y>=5 && x<67 && y<37){uint32_t value=packed[(y-5)*64+x-3],color=0;
                        for(unsigned c=0;c<3;++c){unsigned p=((value>>(c*8))&255)*(value>>24)/128;
                            color|=(p>255?255:p)<<(c*8);}
                        wanted=(wanted&~mask)|(color&mask);}
                    if(actual[i]!=wanted){fprintf(stderr,"ps2-rgba-multiply: mismatch page=%u mask=%x x=%u y=%u wanted=%08x got=%08x\n",
                        page,mask,x,y,wanted,actual[i]);goto out;}++rgba_multiply_checks;
                }
            }
        }
    }
    for(unsigned alu=0;alu<16;++alu)for(unsigned m=0;m<4;++m){uint32_t mask=masks[m],color=0x817f3b19;
        if(ps2_surface_upload(&gs,dest,0,0,96,80,initial,sizeof(initial),96*4) ||
            ps2_surface_solid_logic(&gs,dest,3,5,71,43,color,mask,alu) ||
            download_destination(&gs,dest,actual))goto out;
        for(unsigned y=0;y<80;++y)for(unsigned x=0;x<96;++x){unsigned i=y*96+x;uint32_t wanted=initial[i];
            if(x>=3 && x<74 && y>=5 && y<48)wanted=(wanted&~mask)|(boolean_result(alu,color,wanted)&mask);
            if(actual[i]!=wanted){fprintf(stderr,"ps2-rgba-solid: mismatch alu=%u mask=%x x=%u y=%u wanted=%08x got=%08x\n",
                alu,mask,x,y,wanted,actual[i]);goto out;}++solid_checks;
        }
    }
    status=0;
out:
    if(status)perror("ps2-map-blit: native test");
    ps2_surface_close(&gs);
    if(graphics && ioctl(tty,KDSETMODE,KD_TEXT))status=1;
    if(tty>=0)close(tty);
    printf("ps2-map-blit: pixel_checks=%u exit=%d\n",checks,status);
    printf("ps2-rgba-solid: pixel_checks=%u exit=%d\n",solid_checks,status);
    printf("ps2-alpha-read: pixel_checks=%u exit=%d\n",alpha_checks,status);
    if(gs.caps.features&PS2GS_HW_RGBA_TRANSFER){
        printf("ps2-rgba-copy: pixel_checks=%u exit=%d\n",rgba_copy_checks,status);
        printf("ps2-rgba-multiply: pixel_checks=%u exit=%d\n",rgba_multiply_checks,status);
    }
    return status;
}
