/* SPDX-License-Identifier: MIT */
#include "ps2-gs-logic.h"
#include "previous-render-kernel.c"

/* Independent X11 oracle, not the production truth-table decomposition. */
static uint32_t oracle(unsigned alu, uint32_t s, uint32_t d)
{
    switch (alu) {
    case 0: return 0; case 1: return s & d; case 2: return s & ~d; case 3: return s;
    case 4: return ~s & d; case 5: return d; case 6: return s ^ d; case 7: return s | d;
    case 8: return ~(s | d); case 9: return ~(s ^ d); case 10: return ~d; case 11: return s | ~d;
    case 12: return ~s; case 13: return ~s | d; case 14: return ~(s & d); case 15: return ~0U;
    default: abort();
    }
}
static uint32_t expand16(uint32_t p)
{ return ((p & 31) << 3) | (((p >> 5) & 31) << 11) | (((p >> 10) & 31) << 19) | ((p & 0x8000) << 16); }
static uint32_t pack16(uint32_t p)
{ return ((p >> 3) & 31) | ((p >> 6) & 0x3e0) | ((p >> 9) & 0x7c00) | ((p >> 16) & 0x8000); }
/* Decode raw packet addresses/words with an independent model of only the
 * documented masked sprite math. This is an oracle, not GS pixel readback. */
static uint32_t packet_pixel(union package *p, size_t count, uint32_t d, unsigned bpp)
{
    uint32_t native = d, mask = 0, src = 0;
    unsigned abe = 0, kicks = 0, passes = 0;
    uint64_t alpha = 0;
    for (unsigned i = 1; i < count; ++i) {
        uint64_t word = p[i].gif.doubleword[0];
        unsigned addr = p[i].gif.doubleword[1];
        if (addr == 0x4d) mask = word >> 32;
        if (addr == 0x43) alpha = word;
        if (addr == 0x00) { assert((word & 7) == 6); abe = (word >> 6) & 1; kicks = 0; }
        if (addr == 0x01) src = word;
        if (addr != 0x05 || ++kicks != 2) continue;
        uint32_t dest = bpp == 16 ? expand16(native) : native, rgb = src & 0xffffff;
        if (abe) {
            assert((alpha & 255) == 0xa4 && ((alpha >> 32) & 255) == 128);
            rgb = 0;
            for (unsigned shift = 0; shift < 24; shift += 8)
                rgb |= (((src >> shift) & 255) - ((dest >> shift) & 255)) << shift;
        }
        /* Alpha bypasses RGB blending, but is always masked by FRAME. */
        uint32_t output = ((rgb | (src & 0xff000000)) & ~mask) | (dest & mask);
        native = bpp == 16 ? pack16(output) : output;
        ++passes;
    }
    assert(passes <= 2);
    return native;
}
static void pixels(void)
{
    union package p[32];
    struct ps2gs_buffer b = {.width=13,.height=9,.buffer_width=64,.bits_per_pixel=32};
    struct ps2gs_solid_logic r = {.abi=1,.x=1,.y=2,.width=3,.height=4,.plane_mask=0xff};
    for (r.alu=0; r.alu<16; ++r.alu) for (r.color=0; r.color<256; ++r.color) {
        size_t count = ps2gs_package_solid_logic(p,&b,&r);
        for (uint32_t d=0; d<256; ++d) {
            uint32_t pixel=0xa5943200 | d;
            assert(packet_pixel(p,count,pixel,32) ==
                ((oracle(r.alu,r.color,pixel) & r.plane_mask) | (pixel & ~r.plane_mask)));
        }
    }
    uint32_t random=0x12ab8901;
    for (unsigned bpp=16; bpp<=32; bpp+=16) {
        b.bits_per_pixel=bpp; uint32_t bits=bpp==16 ? 0x7fff : 0xffffff;
        for (unsigned i=0; i<4096; ++i) {
            random=random*1664525U+1013904223U; r.color=random & bits;
            random=random*1664525U+1013904223U; r.plane_mask=random & bits;
            random=random*1664525U+1013904223U; uint32_t d=bpp==16 ? (random & 0xffff) : random;
            for (r.alu=0; r.alu<16; ++r.alu) {
                size_t count=ps2gs_package_solid_logic(p,&b,&r);
                assert(packet_pixel(p,count,d,bpp)==
                    ((oracle(r.alu,r.color,d)&r.plane_mask)|(d&~r.plane_mask)));
            }
        }
    }
}
static void packets(void)
{
    union package p[34]; memset(p,0x5a,sizeof(p));
    struct ps2gs_buffer b={.base=0x180000,.width=65,.height=33,.buffer_width=128,.bits_per_pixel=16};
    struct ps2gs_solid_logic r={.abi=1,.x=1,.y=2,.width=63,.height=31,
        .color=0x2d65,.plane_mask=0x7fdf,.alu=2};
    assert(!ps2gs_validate_solid_logic(&b,&r));
    size_t n=ps2gs_package_solid_logic(p,&b,&r); assert(n==25);
    assert(p[0].gif.tag.nloop==24 && p[0].gif.tag.eop);
    assert(p[2].gif.doubleword[1]==0x41 && p[2].gif.packed.ad.data.scissor_1.scax1==64);
    assert(p[3].gif.packed.ad.data.zbuf.zmsk==1);
    assert(p[6].gif.doubleword[0]==(UINT64_C(128)<<32 | 0xa4));
    assert(p[7].gif.doubleword[0]==0 && p[8].gif.doubleword[0]==0 && p[9].gif.doubleword[0]==0);
    assert(p[10].gif.doubleword[0]==1 && p[10].gif.doubleword[1]==0x46);
    for(unsigned i=1;i<n;++i) if(p[i].gif.doubleword[1]==0x4d) {
        assert((p[i].gif.doubleword[0]&0xffffffff)==(UINT64_C(2)<<24 | UINT64_C(2)<<16 | 192));
        if(i!=n-2) assert(p[i].gif.packed.ad.data.frame_1.fbmsk & 0x80000000);
    }
    assert(p[n-2].gif.packed.ad.data.frame_1.fbmsk==0 && p[n-1].gif.doubleword[1]==0x61);
    assert(p[n].gif.doubleword[0]==UINT64_C(0x5a5a5a5a5a5a5a5a));
    assert(p[14].gif.packed.ad.data.xyz2.x==16 && p[16].gif.packed.ad.data.xyz2.x==64*16);
    r.alu=5; assert(ps2gs_package_solid_logic(p,&b,&r)==13); /* unchanged destination */
}
static void validation(void)
{
    struct fb_info *info=setup(); struct file f; vc.vc_mode=KD_GRAPHICS; assert(!open_file(info,&f));
    struct ps2gs_caps caps; assert(!call(&f,PS2GS_GET_CAPS,&caps) && caps.features==511);
    struct ps2gs_buffer *c=make_color(info,&f),*z=make_depth(info,&f,32);
    struct ps2gs_solid_logic r={.abi=1,.handle=c->handle,.width=65,.height=33,.color=0x1342ab,.plane_mask=0xffffff,.alu=6};
    unsigned before=dma_calls;
    assert(!call(&f,PS2GS_SOLID_LOGIC,&r) && dma_calls==before+1); before=dma_calls;
    for(unsigned i=0;i<8;++i) {
        struct ps2gs_solid_logic bad=r;
        switch(i) {
        case 0: bad.abi=2; break; case 1: bad.reserved=1; break; case 2: bad.alu=16; break;
        case 3: bad.x=UINT32_MAX; break; case 4: bad.width=UINT32_MAX; break;
        case 5: bad.width=0; break; case 6: bad.color=0x80000000; break; case 7: bad.plane_mask=0x1000000; break;
        }
        assert(call(&f,PS2GS_SOLID_LOGIC,&bad)==-EINVAL && dma_calls==before);
    }
    r.handle=z->handle; assert(call(&f,PS2GS_SOLID_LOGIC,&r)==-ENOENT);
    r.handle=99999; assert(call(&f,PS2GS_SOLID_LOGIC,&r)==-ENOENT);
    r.handle=0; vc.vc_mode=0; assert(call(&f,PS2GS_SOLID_LOGIC,&r)==-EBUSY && dma_calls==before);
    assert(!ps2gs_device_release(NULL,&f)); ps2gs_device_unregister(info); free_info(info);
}
static void failure(void)
{
    struct fb_info *info=setup(); struct file f; vc.vc_mode=KD_GRAPHICS; assert(!open_file(info,&f));
    struct ps2gs_solid_logic r={.abi=1,.width=1,.height=1,.plane_mask=0xffffff,.alu=10};
    ++info->par->gs_epoch; assert(call(&f,PS2GS_SOLID_LOGIC,&r)==-ESTALE && !dma_calls);
    --info->par->gs_epoch; hardware_fail=1;
    assert(call(&f,PS2GS_SOLID_LOGIC,&r)==-ETIMEDOUT && !dma_calls);
    hardware_fail=0; assert(call(&f,PS2GS_SOLID_LOGIC,&r)==-ETIMEDOUT && !dma_calls);
    assert(!ps2gs_device_release(NULL,&f)); assert(!atomic_read(&info->par->gs_owned));
    ps2gs_device_unregister(info); free_info(info);
}
int main(int argc,char **argv)
{
    assert(argc==2); mutex_init(&console);
    if(!strcmp(argv[1],"pixels")) pixels(); else if(!strcmp(argv[1],"packets")) packets();
    else if(!strcmp(argv[1],"validation")) validation(); else if(!strcmp(argv[1],"failure")) failure(); else return 2;
    pthread_mutex_destroy(&console.value);
    puts("PS2 GS solid logic kernel test PASS"); return 0;
}
