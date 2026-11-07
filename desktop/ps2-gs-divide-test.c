/* SPDX-License-Identifier: MIT */
/* CPU integer arithmetic and readback are an independent diagnostic oracle. */
#define _POSIX_C_SOURCE 200809L
#include "ps2_divide.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/kd.h>
int main(void)
{
    struct ps2_surface_client gs={.fd=-1};struct ps2_divide_workspace w={0};
    uint32_t n[4]={0},d[3]={0},q=0,p=0,values[7][2048],expected[2048],actual[2048];
    const uint32_t divisors[]={0,1,2,127,128,255,256,257,65535,65536,65537,0x7fffff,0x800000,0xfffffe,0xffffff};
    int tty=open("/dev/tty0",O_RDWR|O_CLOEXEC),mode,graphics=0,status=1;unsigned checks=0,state=0x32b175a9;
    if(tty<0 || ioctl(tty,KDGETMODE,&mode) || mode!=KD_TEXT || ioctl(tty,KDSETMODE,KD_GRAPHICS))goto out;
    graphics=1;
    if(ps2_surface_open(&gs,"/dev/ps2-gs") || ps2_divide_reserve(&w,&gs))goto out;
    for(unsigned i=0;i<4;++i)if(ps2_surface_create(&gs,64,32,32,n+i))goto out;
    for(unsigned i=0;i<3;++i)if(ps2_surface_create(&gs,64,32,32,d+i))goto out;
    if(ps2_surface_create(&gs,64,32,32,&q) || ps2_surface_create(&gs,16,16,32,&p) ||
        ps2_surface_set_depth(&gs,q,32))goto out;
    for(unsigned batch=0;batch<4;++batch){
        for(unsigned i=0;i<2048;++i){
            expected[i]=0xa5000000;
            for(unsigned plane=0;plane<7;++plane)values[plane][i]=0;
            for(unsigned channel=0;channel<3;++channel){
                state=state*1664525U+1013904223U;
                uint32_t divisor=divisors[(i+channel)%15],numerator=state;
                if(batch<3){uint64_t boundary=(uint64_t)divisor*(batch*127+channel)+(i%3);
                    numerator=boundary>UINT32_MAX?UINT32_MAX:(uint32_t)boundary;}
                uint32_t result=divisor?numerator/divisor:255;if(result>255)result=255;
                expected[i]|=result<<(channel*8);
                for(unsigned byte=0;byte<4;++byte)values[byte][i]|=((numerator>>(byte*8))&255)<<(channel*8);
                for(unsigned byte=0;byte<3;++byte)values[byte+4][i]|=((divisor>>(byte*8))&255)<<(channel*8);
            }
            actual[i]=0xa5000000;
        }
        for(unsigned plane=0;plane<7;++plane)if(ps2_surface_upload(&gs,plane<4?n[plane]:d[plane-4],
            0,0,64,32,values[plane],sizeof(values[plane]),256))goto out;
        if(ps2_surface_upload(&gs,q,0,0,64,32,actual,sizeof(actual),256) ||
            ps2_native_divide_u32_u24(&w,n,d,q,p,64,32) ||
            ps2_surface_download(&gs,q,0,0,64,32,actual,sizeof(actual)))goto out;
        for(unsigned i=0;i<2048;++i){
            if(actual[i]!=expected[i]){fprintf(stderr,"ps2-divide: batch=%u pixel=%u expected=%08x actual=%08x\n",
                batch,i,expected[i],actual[i]);goto out;}
            checks+=3;
        }
        printf("ps2-divide: batch=%u/4 comparisons=%u\n",batch+1,checks);fflush(stdout);
    }
    status=0;
out:
    if(status)perror("ps2-divide");
    if(w.gs && ps2_divide_release(&w))status=1;
    ps2_surface_close(&gs);
    if(graphics && ioctl(tty,KDSETMODE,KD_TEXT))status=1;
    if(tty>=0)close(tty);
    printf("ps2-divide: pixel_checks=%u exit=%d\n",checks,status);return status;
}
