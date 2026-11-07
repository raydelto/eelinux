/* SPDX-License-Identifier: MIT */
/* CPU colour arithmetic and readback are confined to this diagnostic oracle. */
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
    uint32_t handles[11]={0},palette=0,input[4][2048],initial[2048],actual[2048];
    unsigned checks=0,overflows=0;const unsigned edges[]={0,1,127,128,254,255};
    int tty=open("/dev/tty0",O_RDWR|O_CLOEXEC),mode,graphics=0,status=1;
    if(tty<0 || ioctl(tty,KDGETMODE,&mode) || mode!=KD_TEXT || ioctl(tty,KDSETMODE,KD_GRAPHICS))goto out;
    graphics=1;
    if(ps2_surface_open(&gs,"/dev/ps2-gs") || ps2_filter_reserve(&f,&gs,4))goto out;
    for(unsigned i=0;i<11;++i)if(ps2_surface_create(&gs,64,32,32,handles+i) || ps2_surface_set_depth(&gs,handles[i],32))goto out;
    if(ps2_surface_create(&gs,16,16,32,&palette))goto out;
    const uint32_t work[]={handles[5],handles[6],handles[7],handles[8],handles[9],handles[10]};
    for(unsigned op=0;op<2;++op)for(unsigned alias=0;alias<3;++alias){
        for(unsigned page=0;page<(alias?1U:32U);++page){
            for(unsigned i=0;i<2048;++i){unsigned pair=(alias?alias*12289:page*2048)+i,s=pair>>8&255,d=pair&255;
                unsigned group=(i+page+alias)%5;
                for(unsigned j=0;j<4;++j)input[j][i]=(0xa1U+j)<<24;
                for(unsigned c=0;c<3;++c){unsigned sc=c==1?255-s:s,dc=c==2?255-d:d;
                    int boundary_s=2*sc+(int)((pair+c)%3)-1,boundary_d=2*dc+(int)((pair/3+c)%3)-1;
                    unsigned sa=group==0?255:group==1?(s*73+d*37+c*19)&255:
                        group==2?(sc>((s*17+d*7)&255)?sc:(s*17+d*7)&255):group==3?edges[(pair+c)%6]:
                        boundary_s<0?0:boundary_s>255?255:(unsigned)boundary_s;
                    unsigned da=group==0?255:group==1?(d*29+s*17+c*43)&255:
                        group==2?(dc>((s*11+d*13)&255)?dc:(s*11+d*13)&255):group==3?edges[(pair/6+c)%6]:
                        boundary_d<0?0:boundary_d>255?255:(unsigned)boundary_d;
                    input[0][i]|=sc<<(8*c);input[1][i]|=dc<<(8*c);
                    input[2][i]|=sa<<(8*c);input[3][i]|=da<<(8*c);
                }
                initial[i]=0xc38fc329U^(i*0x00030507U);
            }
            for(unsigned j=0;j<4;++j)
                if(ps2_surface_upload(&gs,handles[j],0,0,64,32,input[j],sizeof(input[j]),256))goto out;
            uint32_t output=alias?handles[alias-1]:handles[4];
            if((!alias && ps2_surface_upload(&gs,output,0,0,64,32,initial,sizeof(initial),256)) ||
                ps2_native_piecewise_blend(&f,handles[0],handles[1],handles[2],handles[3],output,work,palette,64,32,op) ||
                ps2_surface_download(&gs,output,0,0,64,32,actual,sizeof(actual)))goto out;
            for(unsigned i=0;i<2048;++i){uint32_t wanted=(alias?input[alias-1][i]:initial[i])&0xff000000;
                for(unsigned c=0;c<3;++c){unsigned s=input[0][i]>>(8*c)&255,d=input[1][i]>>(8*c)&255,
                        sa=input[2][i]>>(8*c)&255,da=input[3][i]>>(8*c)&255;
                    int blend=(op==0?2*d<da:2*s<sa)?2*s*d:(int)(sa*da)-2*((int)da-d)*((int)sa-s);
                    unsigned n=s*(255-da)+d*(255-sa)+blend,v=n>65025?255:(n+127)/255;
                    wanted|=v<<(8*c);++checks;overflows+=n>65535;
                }
                if(actual[i]!=wanted){fprintf(stderr,"ps2-piecewise: op=%u alias=%u page=%u pixel=%u expected=%08x actual=%08x\n",
                    op,alias,page,i,wanted,actual[i]);goto out;}
            }
            printf("ps2-piecewise: op=%u alias=%u page=%u comparisons=%u overflow_vectors=%u\n",op,alias,page+1,checks,overflows);fflush(stdout);
        }
    }
    if(checks!=417792 || !overflows)goto out;
    status=0;
out:
    if(status)perror("ps2-piecewise");
    if(f.gs && ps2_filter_release(&f))status=1;
    ps2_surface_close(&gs);
    if(graphics && ioctl(tty,KDSETMODE,KD_TEXT))status=1;
    if(tty>=0)close(tty);
    printf("ps2-piecewise: pixel_checks=%u exit=%d\n",checks,status);return status;
}
