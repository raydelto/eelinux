/* SPDX-License-Identifier: MIT */
#include "previous-composite.c"
#include <ps2_multiply.h>
int main(void)
{
    rgba_enabled=1;setup_rop(32);
    struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    struct ps2_filter_workspace f={0};uint32_t a=0,b=0,d=0,p=0;
    assert(!ps2_filter_reserve(&f,gs,3));
    assert(!ps2_surface_create(gs,64,32,32,&a) && !ps2_surface_create(gs,64,32,32,&b) &&
        !ps2_surface_create(gs,64,32,32,&d) && !ps2_surface_create(gs,16,16,32,&p));
    unsigned comparisons=0;
    for(unsigned page=0;page<32;++page){
        for(unsigned i=0;i<2048;++i){unsigned pair=page*2048+i,x=pair>>8,y=pair&255;
            model(a)->data[i]=x|((255-x)<<8)|((x*73&255)<<16)|((y*193&255)<<24);
            model(b)->data[i]=y|((255-y)<<8)|((y*127&255)<<16)|((x*17&255)<<24);
            model(d)->data[i]=0xa58fc329U^(i*0x01030507U);
        }
        assert(!ps2_native_multiply(&f,a,b,d,p,64,32));
        for(unsigned i=0;i<2048;++i){uint32_t wanted=(0xa58fc329U^(i*0x01030507U))&0xff000000;
            for(unsigned c=0;c<3;++c){unsigned x=(model(a)->data[i]>>(c*8))&255,y=(model(b)->data[i]>>(c*8))&255;
                wanted|=((x*y+127)/255)<<(c*8);++comparisons;}
            if(model(d)->data[i]!=wanted){fprintf(stderr,"native multiply pair=%u wanted=%08x actual=%08x\n",
                page*2048+i,wanted,model(d)->data[i]);abort();}
        }
    }
    uint32_t low=0,high=0,extra=0,factor=0;
    assert(!ps2_surface_create(gs,64,32,32,&low) && !ps2_surface_create(gs,64,32,32,&high) &&
        !ps2_surface_create(gs,64,32,32,&extra) && !ps2_surface_create(gs,64,32,32,&factor));
    const unsigned destinations[]={0,1,127,128,254,255};unsigned saturation_checks=0;
    for(unsigned k=0;k<6;++k)for(unsigned page=0;page<1024;++page){
        for(unsigned i=0;i<64;++i){unsigned pair=page*64+i;
            model(a)->data[i]=(pair>>8)*0x010101U;
            model(b)->data[i]=(pair&255)*0x010101U;
            model(d)->data[i]=destinations[k]*0x010101U;}
        assert(!ps2_native_saturate_alpha(&f,a,b,d,low,high,extra,factor,p,64));
        for(unsigned i=0;i<64;++i){unsigned pair=page*64+i;
            unsigned expected=(((pair>>8)*(pair&255)+destinations[k]*255)*256)/65025;
            if(expected>255)expected=255;
            if((model(d)->data[i]&0xffffff)!=expected*0x010101U){
                fprintf(stderr,"GS Saturate mismatch source=%u mask=%u destination=%u expected=%u actual=%08x\n",
                    pair>>8,pair&255,destinations[k],expected,model(d)->data[i]);abort();}
            ++saturation_checks;
        }
    }
    assert(!ps2_surface_destroy(gs,factor) && !ps2_surface_destroy(gs,extra) &&
        !ps2_surface_destroy(gs,high) && !ps2_surface_destroy(gs,low));
    assert(!ps2_filter_release(&f));
    assert(!ps2_surface_destroy(gs,p) && !ps2_surface_destroy(gs,d) && !ps2_surface_destroy(gs,b) && !ps2_surface_destroy(gs,a));
    finish_rop();printf("GS exact multiplication PASS comparisons=%u\nGS unfiltered Saturate PASS triples=%u\n",
        comparisons,saturation_checks);return 0;
}
