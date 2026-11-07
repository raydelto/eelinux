/* SPDX-License-Identifier: MIT */
/* CPU binary32 arithmetic and readback are diagnostic oracles only. */
#define _POSIX_C_SOURCE 200809L
#include "ps2_wide.h"
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/kd.h>
#ifndef PS2_FLOAT_DIVIDE
#define PS2_FLOAT_DIVIDE 0
#endif
#ifndef PS2_FLOAT_OPERATION
#define PS2_FLOAT_OPERATION PS2_FLOAT_DIVIDE
#endif
#if PS2_FLOAT_OPERATION==1
#define LABEL "ps2-float-divide"
#elif PS2_FLOAT_OPERATION==2
#define LABEL "ps2-float-add"
#elif PS2_FLOAT_OPERATION==3
#define LABEL "ps2-float-subtract"
#else
#define LABEL "ps2-float-multiply"
#endif
int main(void)
{
    struct ps2_surface_client gs={.fd=-1};struct ps2_divide_workspace w={0};struct ps2_filter_workspace f={0};
    uint32_t a[4]={0},b[4]={0},out[4]={0},p=0,values[8][2048],expected[4][2048],actual[2048];
    int tty=open("/dev/tty0",O_RDWR|O_CLOEXEC),mode,graphics=0,status=1;unsigned checks=0,state=0x59142ce7;
    if(tty<0 || ioctl(tty,KDGETMODE,&mode) || mode!=KD_TEXT || ioctl(tty,KDSETMODE,KD_GRAPHICS))goto done;
    graphics=1;
    if(ps2_surface_open(&gs,"/dev/ps2-gs") || ps2_divide_reserve(&w,&gs) || ps2_filter_reserve(&f,&gs,3))goto done;
    for(unsigned i=0;i<4;++i)if(ps2_surface_create(&gs,64,32,32,a+i) || ps2_surface_create(&gs,64,32,32,b+i) ||
        ps2_surface_create(&gs,64,32,32,out+i) || ps2_surface_set_depth(&gs,out[i],32))goto done;
    if(ps2_surface_create(&gs,16,16,32,&p))goto done;
    for(unsigned batch=0;batch<4;++batch){
        for(unsigned i=0;i<2048;++i){
            for(unsigned byte=0;byte<8;++byte)values[byte][i]=0;
            for(unsigned byte=0;byte<4;++byte)expected[byte][i]=0xa5000000;
            for(unsigned c=0;c<3;++c){float av,bv;
                if(batch<2){float reciprocal=1.0f/255;unsigned pair=(batch?65535:0)^(i*7+c*131);
                    av=((pair>>8)&255)*reciprocal;
                    bv=(PS2_FLOAT_OPERATION==1?(pair%255+1):(pair&255))*reciprocal;
                }else{
                    unsigned base=PS2_FLOAT_OPERATION?90:96,range=PS2_FLOAT_OPERATION?61:31;
                    state=state*1664525U+1013904223U;uint32_t bits_=((base+state%range)<<23)|(state&0x7fffff);memcpy(&av,&bits_,4);
                    state=state*1664525U+1013904223U;bits_=((base+state%range)<<23)|(state&0x7fffff);memcpy(&bv,&bits_,4);
                    if(i%17==0)av=0;
                    if(i%19==0)bv=1;
                    if(PS2_FLOAT_OPERATION && i%23==0)av=bv;
                    if(PS2_FLOAT_OPERATION>=2 && i%31==0)bv=0;
                    if(PS2_FLOAT_OPERATION>=2 && i%37==0){memcpy(&bits_,&av,4);if(bits_)++bits_;memcpy(&bv,&bits_,4);}
                    if(PS2_FLOAT_OPERATION>=2 && i%41==0)bv=av*0.5f;
                }
                if(PS2_FLOAT_OPERATION==3 && av<bv){float swap=av;av=bv;bv=swap;}
                float product=PS2_FLOAT_OPERATION==1?av/bv:PS2_FLOAT_OPERATION==2?av+bv:
                    PS2_FLOAT_OPERATION==3?av-bv:av*bv;
                uint32_t aa,bb,value;memcpy(&aa,&av,4);memcpy(&bb,&bv,4);memcpy(&value,&product,4);
                for(unsigned byte=0;byte<4;++byte){values[byte][i]|=((aa>>(byte*8))&255)<<(c*8);
                    values[byte+4][i]|=((bb>>(byte*8))&255)<<(c*8);expected[byte][i]|=((value>>(byte*8))&255)<<(c*8);}
            }
            actual[i]=0xa5000000;
        }
        for(unsigned i=0;i<8;++i)if(ps2_surface_upload(&gs,i<4?a[i]:b[i-4],0,0,64,32,values[i],sizeof(values[i]),256))goto done;
        for(unsigned i=0;i<4;++i)if(ps2_surface_upload(&gs,out[i],0,0,64,32,actual,sizeof(actual),256))goto done;
        if(PS2_FLOAT_OPERATION==1?ps2_native_float_divide(&w,&f,a,b,out,p,64,32):
            PS2_FLOAT_OPERATION>=2?ps2_native_float_add(&w,&f,a,b,out,p,64,32,PS2_FLOAT_OPERATION==3):
            ps2_native_float_multiply(&w,&f,a,b,out,p,64,32))goto done;
        for(unsigned byte=0;byte<4;++byte){
            if(ps2_surface_download(&gs,out[byte],0,0,64,32,actual,sizeof(actual)))goto done;
            for(unsigned i=0;i<2048;++i){if(actual[i]!=expected[byte][i]){
                fprintf(stderr,LABEL ": batch=%u byte=%u pixel=%u expected=%08x actual=%08x\n",
                    batch,byte,i,expected[byte][i],actual[i]);goto done;}checks+=3;}
        }
        printf(LABEL ": batch=%u/4 byte_checks=%u\n",batch+1,checks);fflush(stdout);
    }
    status=0;
done:
    if(status)perror(LABEL);
    if(w.gs && ps2_divide_release(&w))status=1;
    if(f.gs && ps2_filter_release(&f))status=1;
    ps2_surface_close(&gs);
    if(graphics && ioctl(tty,KDSETMODE,KD_TEXT))status=1;
    if(tty>=0)close(tty);
    printf(LABEL ": pixel_checks=%u exit=%d\n",checks,status);return status;
}
