/* SPDX-License-Identifier: MIT */
/* Pixman and downloads are independent diagnostic oracles, never a fallback. */
#define _POSIX_C_SOURCE 200809L
#include "ps2_wide.h"
#include <pixman.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/kd.h>
int main(void)
{
    struct ps2_surface_client gs={.fd=-1};struct ps2_divide_workspace w={0};struct ps2_filter_workspace f={0};
    uint32_t h[6]={0},p=0,source[2048],mask[2048],destination[2048],planes[6][2048],rgb[2048],alpha[2048];
    int tty=open("/dev/tty0",O_RDWR|O_CLOEXEC),mode,graphics=0,status=1;unsigned checks=0,state=0x512495ac;
    if(tty<0 || ioctl(tty,KDGETMODE,&mode) || mode!=KD_TEXT || ioctl(tty,KDSETMODE,KD_GRAPHICS))goto done;
    graphics=1;
    if(ps2_surface_open(&gs,"/dev/ps2-gs") || ps2_divide_reserve(&w,&gs) || ps2_filter_reserve(&f,&gs,3))goto done;
    for(unsigned i=0;i<6;++i)if(ps2_surface_create(&gs,64,32,32,h+i) || ps2_surface_set_depth(&gs,h[i],32))goto done;
    if(ps2_surface_create(&gs,16,16,32,&p))goto done;
    for(unsigned batch=0;batch<4;++batch){
        unsigned component=batch>=2;
        for(unsigned i=0;i<2048;++i){
            state=state*1664525U+1013904223U;source[i]=state;
            state=state*1664525U+1013904223U;mask[i]=batch==0?UINT32_MAX:state;
            state=state*1664525U+1013904223U;destination[i]=state;
            if(i%17==0)source[i]&=0xffffff;
            if(i%19==0)destination[i]|=0xff000000;
            if(i%23==0)mask[i]&=0xffffff;
            if(!(batch&1)){unsigned a=source[i]>>24,da=destination[i]>>24;
                for(unsigned c=0;c<3;++c){unsigned s=(source[i]>>(c*8))&255,d=(destination[i]>>(c*8))&255;
                    source[i]=(source[i]&~(255U<<(c*8)))|((s*a+127)/255)<<(c*8);
                    destination[i]=(destination[i]&~(255U<<(c*8)))|((d*da+127)/255)<<(c*8);}
            }
            planes[0][i]=0xa5000000|(source[i]&0xffffff);planes[1][i]=0xa5000000|(source[i]>>24)*0x010101;
            planes[2][i]=0xa5000000|(component?(mask[i]&0xffffff):(mask[i]>>24)*0x010101);
            planes[3][i]=0xa5000000|(mask[i]>>24)*0x010101;
            planes[4][i]=0xa5000000|(destination[i]&0xffffff);planes[5][i]=0xa5000000|(destination[i]>>24)*0x010101;
        }
        pixman_image_t *si=pixman_image_create_bits(PIXMAN_a8b8g8r8,64,32,source,256),
            *mi=pixman_image_create_bits(PIXMAN_a8b8g8r8,64,32,mask,256),
            *di=pixman_image_create_bits(PIXMAN_a8b8g8r8,64,32,destination,256);
        if(!si || !mi || !di)goto done;
        pixman_image_set_component_alpha(mi,component);
        pixman_image_composite32(PIXMAN_OP_SATURATE,si,mi,di,0,0,0,0,0,0,64,32);
        pixman_image_unref(si);pixman_image_unref(mi);pixman_image_unref(di);
        for(unsigned i=0;i<6;++i)if(ps2_surface_upload(&gs,h[i],0,0,64,32,planes[i],sizeof(planes[i]),256))goto done;
        if(ps2_native_saturate_rgba8(&w,&f,h[0],h[1],h[2],h[3],h[4],h[5],p,64,32) ||
            ps2_surface_download(&gs,h[4],0,0,64,32,rgb,sizeof(rgb)) ||
            ps2_surface_download(&gs,h[5],0,0,64,32,alpha,sizeof(alpha)))goto done;
        for(unsigned i=0;i<2048;++i){uint32_t wanted_rgb=0xa5000000|(destination[i]&0xffffff),
            wanted_alpha=0xa5000000|(destination[i]>>24)*0x010101;
            if(rgb[i]!=wanted_rgb || alpha[i]!=wanted_alpha){fprintf(stderr,
                "ps2-saturate-rgba8: batch=%u pixel=%u expected=%08x rgb=%08x alpha=%08x\n",
                batch,i,destination[i],rgb[i],alpha[i]);goto done;}checks+=4;
        }
        printf("ps2-saturate-rgba8: batch=%u/4 checks=%u\n",batch+1,checks);fflush(stdout);
    }
    status=0;
done:
    if(status)perror("ps2-saturate-rgba8");
    if(w.gs && ps2_divide_release(&w))status=1;
    if(f.gs && ps2_filter_release(&f))status=1;
    ps2_surface_close(&gs);
    if(graphics && ioctl(tty,KDSETMODE,KD_TEXT))status=1;
    if(tty>=0)close(tty);
    printf("ps2-saturate-rgba8: pixel_checks=%u exit=%d\n",checks,status);return status;
}
