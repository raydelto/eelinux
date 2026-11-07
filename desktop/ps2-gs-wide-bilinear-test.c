/* SPDX-License-Identifier: MIT */
/* Independent diagnostic: pixman/CPU comparisons never render guest pixels. */
#define _POSIX_C_SOURCE 200809L
#include "ps2_wide_filter.h"
#include <pixman.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/kd.h>
int main(void)
{
    struct ps2_surface_client gs={.fd=-1};struct ps2_divide_workspace w={0};struct ps2_filter_workspace f={0};
    uint32_t source=0,out[4]={0},p=0,upload[2048],actual[2048],initial[2048];unsigned char raw[8192];
    const pixman_format_code_t formats[]={PIXMAN_a8b8g8r8,PIXMAN_b2g3r3,PIXMAN_a4b4g4r4};
    const unsigned sizes[][4]={{8,8,8,8},{3,3,2,0},{4,4,4,4}};
    const unsigned fractions[][2]={{0,0},{1,1},{257,255},{19661,32767},{32768,32768},{62177,731},{65535,65535}};
    const struct ps2_filter_tap original[]={{2,3,0},{3,3,0},{2,4,0},{3,4,0}};
    int tty=open("/dev/tty0",O_RDWR|O_CLOEXEC),mode,graphics=0,status=1;unsigned checks=0;
    if(tty<0 || ioctl(tty,KDGETMODE,&mode) || mode!=KD_TEXT || ioctl(tty,KDSETMODE,KD_GRAPHICS))goto done;
    graphics=1;
    if(ps2_surface_open(&gs,"/dev/ps2-gs") || ps2_divide_reserve(&w,&gs) || ps2_filter_reserve(&f,&gs,3) ||
        ps2_surface_create(&gs,64,32,32,&source) || ps2_surface_set_depth(&gs,source,32))goto done;
    for(unsigned i=0;i<4;++i)if(ps2_surface_create(&gs,64,32,32,out+i) || ps2_surface_set_depth(&gs,out[i],32))goto done;
    if(ps2_surface_create(&gs,16,16,32,&p))goto done;
    for(unsigned i=0;i<2048;++i)initial[i]=0xa5332211;
    for(unsigned layout=0;layout<3;++layout){unsigned cpp=layout==0?4:layout==1?1:2;
        memset(raw,0,sizeof(raw));
        for(unsigned i=0;i<2048;++i){uint32_t packed=0,expanded=0;unsigned shift=0;
            for(unsigned c=0;c<4;++c){unsigned n=sizes[layout][c];if(!n){expanded|=255U<<(c*8);continue;}
                unsigned value=(i*(c*37+73)+c*29)&((1U<<n)-1),v=value,filled=n;
                packed|=value<<shift;shift+=n;
                while(filled<8){v=(v<<n)|value;filled+=n;}expanded|=(v>>(filled-8))<<(c*8);
            }
            memcpy(raw+i*cpp,&packed,cpp);upload[i]=expanded;
        }
        if(ps2_surface_upload(&gs,source,0,0,64,32,upload,sizeof(upload),256))goto done;
        for(unsigned alpha=0;alpha<2;++alpha)for(unsigned fraction=0;fraction<7;++fraction){
            unsigned fx=fractions[fraction][0],fy=fractions[fraction][1],clipped=fraction==6;
            struct ps2_filter_tap taps[4];memcpy(taps,original,sizeof(taps));
            if(clipped)taps[2].x=taps[2].y=taps[3].x=taps[3].y=UINT16_MAX;
            float expected[32*4]={0};
            pixman_image_t *si=pixman_image_create_bits(formats[layout],64,clipped?4:32,(uint32_t*)raw,64*cpp),
                *di=pixman_image_create_bits(PIXMAN_rgba_float,32,1,(uint32_t*)expected,32*16);
            if(!si || !di){if(si)pixman_image_unref(si);if(di)pixman_image_unref(di);goto done;}
            pixman_transform_t transform={{{65536,0,fx},{0,65536,fy},{0,0,65536}}};
            if(!pixman_image_set_transform(si,&transform) || !pixman_image_set_filter(si,PIXMAN_FILTER_BILINEAR,NULL,0)){
                pixman_image_unref(si);pixman_image_unref(di);goto done;
            }
            pixman_image_composite32(PIXMAN_OP_SRC,si,NULL,di,2,3,0,0,0,0,32,1);
            pixman_image_unref(si);pixman_image_unref(di);
            unsigned bits_[3];for(unsigned c=0;c<3;++c)bits_[c]=alpha?(sizes[layout][3]?sizes[layout][3]:8):sizes[layout][c];
            f.source_kind=alpha?(sizes[layout][3]?PS2_FILTER_CHANNEL:PS2_FILTER_CONSTANT):PS2_FILTER_RGB;
            f.source_channel=3;f.source_constant=255;f.source_width=64;f.source_height=32;
            for(unsigned byte=0;byte<4;++byte)
                if(ps2_surface_upload(&gs,out[byte],0,0,64,32,initial,sizeof(initial),256))goto done;
            if(ps2_native_bilinear_float(&w,&f,source,out,bits_,p,taps,fx,fy,32,1))goto done;
            for(unsigned byte=0;byte<4;++byte){
                if(ps2_surface_download(&gs,out[byte],0,0,64,32,actual,sizeof(actual)))goto done;
                for(unsigned i=0;i<2048;++i){uint32_t wanted=initial[i];
                    if(i<32){wanted=0xa5000000;
                        for(unsigned c=0;c<3;++c){uint32_t value;memcpy(&value,expected+i*4+(alpha?3:c),4);
                            wanted|=((value>>(byte*8))&255)<<(c*8);}}
                    if(actual[i]!=wanted){fprintf(stderr,"ps2-wide-bilinear: layout=%u alpha=%u fraction=%u byte=%u pixel=%u wanted=%08x got=%08x\n",
                        layout,alpha,fraction,byte,i,wanted,actual[i]);goto done;}
                    if(i<32)checks+=3;
                }
            }
            printf("ps2-wide-bilinear: layout=%u alpha=%u fraction=%u checks=%u\n",layout,alpha,fraction,checks);fflush(stdout);
        }
    }
    status=0;
done:
    if(status)perror("ps2-wide-bilinear");
    if(w.gs && ps2_divide_release(&w))status=1;
    if(f.gs && ps2_filter_release(&f))status=1;
    ps2_surface_close(&gs);
    if(graphics && ioctl(tty,KDSETMODE,KD_TEXT))status=1;
    if(tty>=0)close(tty);
    printf("ps2-wide-bilinear: pixel_checks=%u exit=%d\n",checks,status);return status;
}
