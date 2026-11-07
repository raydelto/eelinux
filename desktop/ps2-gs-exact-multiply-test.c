/* SPDX-License-Identifier: MIT */
/* Colour readback is used only for this independent diagnostic. */
#define _POSIX_C_SOURCE 200809L
#include <ps2_multiply.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/kd.h>
int main(void)
{
    struct ps2_surface_client gs={.fd=-1};struct ps2_filter_workspace f={0};
    uint32_t a=0,b=0,d=0,p=0,av[2048],bv[2048],initial[2048],actual[2048];
    int tty=open("/dev/tty0",O_RDWR|O_CLOEXEC),mode,graphics=0,status=1;unsigned checks=0;
    if(tty<0 || ioctl(tty,KDGETMODE,&mode) || mode!=KD_TEXT || ioctl(tty,KDSETMODE,KD_GRAPHICS))goto out;
    graphics=1;
    if(ps2_surface_open(&gs,"/dev/ps2-gs") || ps2_filter_reserve(&f,&gs,3) ||
        ps2_surface_create(&gs,64,32,32,&a) || ps2_surface_create(&gs,64,32,32,&b) ||
        ps2_surface_create(&gs,64,32,32,&d) || ps2_surface_create(&gs,16,16,32,&p) ||
        ps2_surface_set_depth(&gs,d,32))goto out;
    for(unsigned page=0;page<32;++page){
        for(unsigned i=0;i<2048;++i){unsigned pair=page*2048+i,x=pair>>8,y=pair&255;
            av[i]=x|((255-x)<<8)|((x*73&255)<<16)|((y*193&255)<<24);
            bv[i]=y|((255-y)<<8)|((y*127&255)<<16)|((x*17&255)<<24);
            initial[i]=0xa58fc329U^(i*0x01030507U);
        }
        if(ps2_surface_upload(&gs,a,0,0,64,32,av,sizeof(av),256) ||
            ps2_surface_upload(&gs,b,0,0,64,32,bv,sizeof(bv),256) ||
            ps2_surface_upload(&gs,d,0,0,64,32,initial,sizeof(initial),256) ||
            ps2_native_multiply(&f,a,b,d,p,64,32) ||
            ps2_surface_download(&gs,d,0,0,64,32,actual,sizeof(actual)))goto out;
        for(unsigned i=0;i<2048;++i){uint32_t wanted=initial[i]&0xff000000;
            for(unsigned c=0;c<3;++c){unsigned x=av[i]>>(c*8)&255,y=bv[i]>>(c*8)&255;
                wanted|=((x*y+127)/255)<<(c*8);++checks;}
            if(actual[i]!=wanted){fprintf(stderr,"ps2-exact-multiply: pair=%u expected=%08x actual=%08x\n",page*2048+i,wanted,actual[i]);goto out;}
        }
        printf("ps2-exact-multiply: page=%u/32 comparisons=%u\n",page+1,checks);fflush(stdout);
    }
    status=0;
out:
    if(status)perror("ps2-exact-multiply");
    if(f.gs && ps2_filter_release(&f))status=1;
    ps2_surface_close(&gs);
    if(graphics && ioctl(tty,KDSETMODE,KD_TEXT))status=1;
    if(tty>=0)close(tty);
    printf("ps2-exact-multiply: pixel_checks=%u exit=%d\n",checks,status);return status;
}
