/* SPDX-License-Identifier: MIT */
/* CPU arithmetic and readback are independent diagnostic oracles only. */
#define _POSIX_C_SOURCE 200809L
#include "ps2_wide.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/kd.h>
int main(void)
{
    struct ps2_surface_client gs={.fd=-1};struct ps2_divide_workspace w={0};struct ps2_filter_workspace f={0};
    uint32_t a[3]={0},b[3]={0},out[6]={0},p=0,values[6][2048],expected[6][2048],actual[2048];
    const uint32_t corners[]={0,1,2,127,128,255,256,257,65535,65536,65537,0x7fffff,0x800000,0xfffffe,0xffffff};
    int tty=open("/dev/tty0",O_RDWR|O_CLOEXEC),mode,graphics=0,status=1;unsigned checks=0,state=0x539729ab;
    if(tty<0 || ioctl(tty,KDGETMODE,&mode) || mode!=KD_TEXT || ioctl(tty,KDSETMODE,KD_GRAPHICS))goto done;
    graphics=1;
    if(ps2_surface_open(&gs,"/dev/ps2-gs") || ps2_divide_reserve(&w,&gs) || ps2_filter_reserve(&f,&gs,3))goto done;
    for(unsigned i=0;i<3;++i)if(ps2_surface_create(&gs,64,32,32,a+i) || ps2_surface_create(&gs,64,32,32,b+i))goto done;
    for(unsigned i=0;i<6;++i)if(ps2_surface_create(&gs,64,32,32,out+i) || ps2_surface_set_depth(&gs,out[i],32))goto done;
    if(ps2_surface_create(&gs,16,16,32,&p))goto done;
    for(unsigned batch=0;batch<4;++batch){
        for(unsigned i=0;i<2048;++i){
            for(unsigned byte=0;byte<6;++byte){values[byte][i]=0;expected[byte][i]=0xa5000000;}
            for(unsigned channel=0;channel<3;++channel){
                state=state*1664525U+1013904223U;uint32_t av=state&0xffffff;
                state=state*1664525U+1013904223U;uint32_t bv=state&0xffffff;
                if(batch<2){av=corners[(i+batch*3+channel)%15];bv=corners[(i/15+batch*7+channel)%15];}
                uint64_t product=(uint64_t)av*bv;
                for(unsigned byte=0;byte<3;++byte){
                    values[byte][i]|=((av>>(byte*8))&255)<<(channel*8);
                    values[byte+3][i]|=((bv>>(byte*8))&255)<<(channel*8);
                }
                for(unsigned byte=0;byte<6;++byte)expected[byte][i]|=((product>>(byte*8))&255)<<(channel*8);
            }
            actual[i]=0xa5000000;
        }
        for(unsigned i=0;i<6;++i)if(ps2_surface_upload(&gs,i<3?a[i]:b[i-3],0,0,64,32,values[i],sizeof(values[i]),256) ||
            ps2_surface_upload(&gs,out[i],0,0,64,32,actual,sizeof(actual),256))goto done;
        if(ps2_native_product24(&w,&f,a,b,out,p,64,32))goto done;
        for(unsigned byte=0;byte<6;++byte){
            if(ps2_surface_download(&gs,out[byte],0,0,64,32,actual,sizeof(actual)))goto done;
            for(unsigned i=0;i<2048;++i){
                if(actual[i]!=expected[byte][i]){fprintf(stderr,"ps2-wide-product: batch=%u byte=%u pixel=%u expected=%08x actual=%08x\n",
                    batch,byte,i,expected[byte][i],actual[i]);goto done;}
                checks+=3;
            }
        }
        printf("ps2-wide-product: batch=%u/4 byte_checks=%u\n",batch+1,checks);fflush(stdout);
    }
    status=0;
done:
    if(status)perror("ps2-wide-product");
    if(w.gs && ps2_divide_release(&w))status=1;
    if(f.gs && ps2_filter_release(&f))status=1;
    ps2_surface_close(&gs);
    if(graphics && ioctl(tty,KDSETMODE,KD_TEXT))status=1;
    if(tty>=0)close(tty);
    printf("ps2-wide-product: pixel_checks=%u exit=%d\n",checks,status);return status;
}
