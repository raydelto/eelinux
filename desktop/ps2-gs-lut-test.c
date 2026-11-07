/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "ps2_gs_lut.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>
int main(void)
{
    struct ps2_surface_client gs={.fd=-1};
    int tty=open("/dev/tty0",O_RDWR|O_CLOEXEC),graphics=0,status=1,mode;
    uint32_t source=0,dest=0,palette=0,initial[2048],actual[2048],input[2048];unsigned checks=0;
    if(tty<0 || ioctl(tty,KDGETMODE,&mode) || mode!=KD_TEXT || ioctl(tty,KDSETMODE,KD_GRAPHICS))goto out;
    graphics=1;
    if(ps2_surface_open(&gs,"/dev/ps2-gs") || ps2_surface_create(&gs,64,32,32,&source) ||
        ps2_surface_create(&gs,64,32,32,&dest) || ps2_surface_create(&gs,16,16,32,&palette))goto out;
    printf("ps2-lut: native GS features=%x\n",gs.caps.features);fflush(stdout);
    for(unsigned i=0;i<2048;++i){input[i]=(i&255)|((i*73&255)<<8)|((i*193&255)<<16)|0x73000000U;
        initial[i]=0xa58fc329U;}
    if(ps2_surface_upload(&gs,source,0,0,64,32,input,sizeof(input),256))goto out;
    unsigned weights[]={0,1,2,17,63,127,128,129,191,254,255};
    for(unsigned wi=0;wi<sizeof(weights)/sizeof(*weights);++wi){
        unsigned char table[256];for(unsigned i=0;i<256;++i)table[i]=(i*weights[wi]+127)/255;
        for(unsigned c=0;c<4;++c){uint32_t mask=c==3?0xffffff:255U<<(c*8);
            if(ps2_surface_upload(&gs,dest,0,0,64,32,initial,sizeof(initial),256) ||
                ps2_surface_lut_palette(&gs,palette,table) || ps2_surface_channel_lut(&gs,source,dest,palette,64,32,mask) ||
                ps2_surface_download(&gs,dest,0,0,64,32,actual,sizeof(actual)))goto out;
            for(unsigned i=0;i<2048;++i){uint32_t wanted=initial[i];
                for(unsigned ch=0;ch<3;++ch)if(mask&(255U<<(ch*8)))
                    wanted=(wanted&~(255U<<(ch*8)))|((uint32_t)table[(input[i]>>(ch*8))&255]<<(ch*8));
                if(actual[i]!=wanted){fprintf(stderr,"ps2-lut: mismatch weight=%u channels=%x x=%u y=%u wanted=%08x got=%08x\n",
                    weights[wi],mask,i%64,i/64,wanted,actual[i]);goto out;}++checks;
            }
        }
        printf("ps2-lut: weight=%u passed\n",weights[wi]);fflush(stdout);
    }
    status=0;
out:
    if(status)perror("ps2-lut: native test");
    ps2_surface_close(&gs);
    if(graphics && ioctl(tty,KDSETMODE,KD_TEXT))status=1;
    if(tty>=0)close(tty);
    printf("ps2-lut: pixel_checks=%u exit=%d\n",checks,status);return status;
}
