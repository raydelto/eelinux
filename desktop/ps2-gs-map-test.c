/* SPDX-License-Identifier: MIT */
/* Client replies are read only for this diagnostic's independent oracle. */
#define main lut_main
#include "ps2-gs-lut-test.c"
#undef main
int ps2_native_map_check(void)
{
    if(lut_main())return 1;
    struct ps2_surface_client gs={.fd=-1};
    int tty=open("/dev/tty0",O_RDWR|O_CLOEXEC),graphics=0,status=1,mode;
    uint32_t source=0,dest=0,palette=0,input[2048],initial[2048],actual[2048];unsigned checks=0;
    if(tty<0 || ioctl(tty,KDGETMODE,&mode) || mode!=KD_TEXT || ioctl(tty,KDSETMODE,KD_GRAPHICS))goto out;
    graphics=1;
    if(ps2_surface_open(&gs,"/dev/ps2-gs") || !(gs.caps.features&PS2GS_HW_CHANNEL_MAP) ||
        ps2_surface_create(&gs,64,32,32,&source) || ps2_surface_create(&gs,64,32,32,&dest) ||
        ps2_surface_create(&gs,16,16,32,&palette))goto out;
    for(unsigned i=0;i<2048;++i){input[i]=(i&255)|((i*73&255)<<8)|((i*193&255)<<16)|((i*127&255)<<24);
        initial[i]=0xa58fc329U^(i*0x01030507U);}
    if(ps2_surface_upload(&gs,source,0,0,64,32,input,sizeof(input),256))goto out;
    const uint32_t masks[]={0xffffffffU,0xffffff,0xff000000,0xff,0xff00,0xff0000,0x80000000,0x5731a5cf,0};
    const unsigned sizes[][2]={{64,32},{63,31},{5,3},{1,1}};
    for(unsigned channel=0;channel<4;++channel)for(unsigned size=0;size<4;++size)
    for(unsigned m=0;m<sizeof(masks)/sizeof(*masks);++m){
        uint32_t table[256],mask=masks[m];unsigned w=sizes[size][0],h=sizes[size][1];
        for(unsigned i=0;i<256;++i)table[i]=i|((255-i)<<8)|((i*73&255)<<16)|((i*193&255)<<24);
        if(ps2_surface_upload(&gs,dest,0,0,64,32,initial,sizeof(initial),256) ||
            ps2_surface_map_palette(&gs,palette,table) ||
            ps2_surface_channel_map(&gs,source,dest,palette,w,h,mask,channel) ||
            ps2_surface_download(&gs,dest,0,0,64,32,actual,sizeof(actual)))goto out;
        for(unsigned i=0;i<2048;++i){uint32_t wanted=initial[i];
            if(i%64<w && i/64<h)wanted=(wanted&~mask)|(table[(input[i]>>(channel*8))&255]&mask);
            if(actual[i]!=wanted){fprintf(stderr,"ps2-map: mismatch channel=%u size=%ux%u mask=%x x=%u y=%u wanted=%08x got=%08x\n",
                channel,w,h,mask,i%64,i/64,wanted,actual[i]);goto out;}++checks;
        }
    }
    status=0;
out:
    if(status)perror("ps2-map: native test");
    ps2_surface_close(&gs);
    if(graphics && ioctl(tty,KDSETMODE,KD_TEXT))status=1;
    if(tty>=0)close(tty);
    printf("ps2-map: pixel_checks=%u exit=%d\n",checks,status);return status;
}
#ifndef PS2_MAP_DIAGNOSTIC_NO_MAIN
int main(void){return ps2_native_map_check();}
#endif
