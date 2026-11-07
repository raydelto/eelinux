/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "ps2-gs-surface.h"
#ifndef __MIPSEL__
#define __MIPSEL__ 1
#endif
#include "asm/gif.h"
typedef uint32_t u32;
typedef uint8_t u8;
#define ALIGN(n,a) (((n)+(a)-1)&~((a)-1))
#define DIV_ROUND_UP(n,d) (((n)+(d)-1)/(d))
#define GIF_PACKAGE_TAG(p) ((p)++)->gif.tag = (struct gif_tag)
#define GIF_PACKAGE_AD(p) ((p)++)->gif.packed.ad = (struct gif_packed_ad)
union package { union gif_data gif; };
#include "ps2fb-gs-arena.h"
#include "ps2-gs-image-packet.h"
#include "ps2-gs-vertex-packet.h"
#include "ps2fb-gs-surface-packet.h"

static void allocation(void)
{
    struct ps2gs_arena arena;
    struct ps2gs_buffer *buffer;
    assert(ps2gs_arena_init(&arena,8193,4*1024*1024)==-EINVAL);
    assert(!ps2gs_arena_init(&arena,10*8192,14*8192));
    assert(ps2gs_arena_create(&arena,UINT32_MAX,1,32,&buffer)==-EINVAL && !buffer);
    assert(ps2gs_arena_create(&arena,1,1,24,&buffer)==-EINVAL);
    assert(!ps2gs_arena_create(&arena,65,33,32,&buffer));
    assert(buffer->handle==1 && buffer->base==10*8192 && buffer->bytes==4*8192);
    assert(buffer->buffer_width==128 && buffer->width==65 && buffer->height==33);
    assert(ps2gs_arena_create(&arena,1,1,16,&buffer)==-ENOSPC);
    assert(!ps2gs_arena_destroy(&arena,1));
    assert(!ps2gs_arena_create(&arena,1,1,16,&buffer));
    assert(buffer->handle==2 && buffer->bytes==8192 && buffer->base==10*8192);
    assert(!ps2gs_arena_lookup(&arena,1) && !ps2gs_arena_lookup(&arena,0));
    assert(ps2gs_arena_destroy(&arena,1)==-ENOENT);
    assert(!ps2gs_buffer_rect(buffer,UINT32_MAX,0,1,1));
    assert(!ps2gs_buffer_rect(buffer,0,0,UINT32_MAX,1));
    assert(ps2gs_buffer_rect(buffer,0,0,1,1));
    assert(!ps2gs_arena_destroy(&arena,2));
    arena.next_handle=UINT32_MAX;
    assert(!ps2gs_arena_create(&arena,1,1,16,&buffer) && buffer->handle==UINT32_MAX);
    assert(ps2gs_arena_create(&arena,1,1,16,&buffer)==-EOVERFLOW);
}
static void fragmentation(void)
{
    struct ps2gs_arena arena; struct ps2gs_buffer *buffer;
    assert(!ps2gs_arena_init(&arena,0,64*8192));
    uint32_t handles[64];
    for (unsigned i=0;i<64;++i) {
        assert(!ps2gs_arena_create(&arena,1,1,32,&buffer));
        assert(buffer->base==i*8192); handles[i]=buffer->handle;
    }
    assert(ps2gs_arena_create(&arena,1,1,32,&buffer)==-ENOSPC);
    for (unsigned i=0;i<64;i+=2) assert(!ps2gs_arena_destroy(&arena,handles[i]));
    assert(ps2gs_arena_create(&arena,65,1,32,&buffer)==-ENOSPC);
    assert(!ps2gs_arena_destroy(&arena,handles[1]));
    assert(!ps2gs_arena_create(&arena,65,1,32,&buffer) && buffer->base==0);
    /* Random create/free stress verifies disjoint ranges and reservations. */
    uint32_t seed=42;
    for (unsigned step=0;step<2000;++step) {
        seed=seed*1664525U+1013904223U;
        unsigned slot=seed%64;
        if (arena.buffers[slot].handle) assert(!ps2gs_arena_destroy(&arena,arena.buffers[slot].handle));
        else ps2gs_arena_create(&arena,1+(seed%128),1+((seed>>8)%128),seed&1?16:32,&buffer);
        for (unsigned i=0;i<64;++i) for (unsigned j=i+1;j<64;++j) {
            const struct ps2gs_buffer *a=&arena.buffers[i],*b=&arena.buffers[j];
            if (a->handle && b->handle) assert(a->base+a->bytes<=b->base || b->base+b->bytes<=a->base);
        }
    }
}
static void packets(void)
{
    union package packet[256]; unsigned char pixels[8]={1,2,3,4,5,6,7,8};
    const struct ps2gs_buffer s={.base=10*8192,.width=66,.height=65,.buffer_width=128,.bits_per_pixel=32};
    const struct ps2gs_buffer d={.base=20*8192,.width=66,.height=65,.buffer_width=128,.bits_per_pixel=32};
    struct ps2gs_surface_fill fill={.abi=1,.x=2,.y=3,.width=64,.height=62,.rgba=0x80402010};
    size_t n=ps2gs_package_surface_fill(packet,&d,&fill);
    assert(n==13 && packet[1].gif.doubleword[1]==0x4d);
    assert(packet[1].gif.doubleword[0]==(UINT64_C(2)<<16 | 20));
    assert(packet[3].gif.doubleword[0]==(UINT64_C(65)<<16 | UINT64_C(64)<<48));
    assert(packet[4].gif.doubleword[0]==(UINT64_C(1)<<32 | UINT64_C(0xa)<<24));
    assert(packet[n-1].gif.doubleword[1]==0x61);
    struct ps2gs_surface_copy copy={.abi=1,.sx=2,.sy=3,.dx=0,.dy=0,.width=64,.height=62};
    assert(!ps2gs_validate_surface_copy(&s,&d,&copy));
    assert(ps2gs_package_surface_copy(packet,&s,&d,&copy)==6);
    assert(packet[1].gif.doubleword[0]==(UINT64_C(640)<<32 | UINT64_C(2)<<48 | UINT64_C(2)<<16 | 320));
    assert(packet[2].gif.doubleword[0]==(UINT64_C(2) | UINT64_C(3)<<16));
    copy.sx=0; copy.sy=0; copy.dx=2; copy.dy=3;
    ps2gs_package_surface_copy(packet,&s,&s,&copy);
    assert(packet[2].gif.doubleword[0]==(UINT64_C(2)<<32 | UINT64_C(3)<<48 | UINT64_C(3)<<59));
    copy.width=63; assert(ps2gs_validate_surface_copy(&s,&d,&copy)==-EOPNOTSUPP);
    copy.width=64; copy.sx=UINT32_MAX; assert(ps2gs_validate_surface_copy(&s,&d,&copy)==-EINVAL);
    struct ps2gs_upload upload={.x=1,.y=2,.image_width=2,.image_height=1,.bytes=8};
    ps2gs_package_upload_to(packet,&upload,pixels,d.base/256,d.buffer_width/64,gs_psm_ct32);
    assert(packet[1].gif.doubleword[0]==(UINT64_C(640)<<32 | UINT64_C(2)<<48));
    assert(!memcmp(&packet[6],pixels,8));
    /* Old front-buffer wrapper remains unchanged. */
    ps2gs_package_upload(packet,&upload,pixels,2,gs_psm_ct32);
    assert(packet[1].gif.doubleword[0]==UINT64_C(2)<<48);
    struct ps2gs_caps caps={.abi=1,.width=66,.height=65,.bits_per_pixel=32};
    upload.abi=1; upload.width=66; upload.height=65; upload.bits_per_pixel=32;
    assert(!ps2gs_validate_upload(&upload,&caps));
}
static void capacity(void)
{
    struct ps2gs_arena arena;struct ps2gs_buffer *buffer;uint32_t handles[PS2GS_MAX_SURFACES];
    /* Plenty of pages: exhaust handles independently of VRAM exhaustion. */
    assert(!ps2gs_arena_init(&arena,16*8192,512*8192));
    for(unsigned i=0;i<PS2GS_MAX_SURFACES;++i){
        assert(!ps2gs_arena_create(&arena,64,32,32,&buffer));
        assert(buffer->base==(16+i)*8192);handles[i]=buffer->handle;
    }
    assert(ps2gs_arena_create(&arena,64,32,32,&buffer)==-ENOSPC);
    assert(arena.used[15]==0 && arena.used[16+PS2GS_MAX_SURFACES]==0);
    assert(!ps2gs_arena_destroy(&arena,handles[64]));
    assert(!ps2gs_arena_create(&arena,64,32,32,&buffer));
    assert(buffer->base==(16+64)*8192 && buffer->handle!=handles[64]);
    for(unsigned i=0;i<PS2GS_MAX_SURFACES;++i)if(i!=64)assert(!ps2gs_arena_destroy(&arena,handles[i]));
    assert(!ps2gs_arena_destroy(&arena,buffer->handle));
    for(unsigned i=0;i<512;++i)assert(!arena.used[i]);
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if (!strcmp(argv[1],"allocation")) allocation();
    else if (!strcmp(argv[1],"fragmentation")) fragmentation();
    else if (!strcmp(argv[1],"packets")) packets();
    else if (!strcmp(argv[1],"capacity")) capacity();
    else return 2;
    puts("PS2 GS surface allocation/packet test passed"); return 0;
}
