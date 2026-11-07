/* SPDX-License-Identifier: MIT */
/* Test the actual kernel's pure upload validator/packet builder with its real
 * GS/GIF UAPI layouts. No framebuffer, DMA or GS hardware is emulated here. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <linux/types.h>
#include "ps2-gs.h"
#ifndef __MIPSEL__
#define __MIPSEL__ 1
#endif
#include "asm/gif.h"
typedef uint32_t u32;
typedef uint8_t u8;
union package { union gif_data gif; };
#define DIV_ROUND_UP(n,d) (((n)+(d)-1)/(d))
#define GIF_PACKAGE_TAG(p) ((p)++)->gif.tag = (struct gif_tag)
#define GIF_PACKAGE_AD(p) ((p)++)->gif.packed.ad = (struct gif_packed_ad)
#include "ps2-gs-image-packet.h"

static const struct ps2gs_caps caps={.abi=1,.width=2048,.height=576,.bits_per_pixel=32};
static struct ps2gs_upload valid(void)
{
    return (struct ps2gs_upload){.abi=1,.width=2048,.height=576,.bits_per_pixel=32,
        .x=4,.y=5,.image_width=3,.image_height=2,.bytes=24,.pixels=0x400000};
}
static void validation(void)
{
    struct ps2gs_upload u=valid(); assert(!ps2gs_validate_upload(&u,&caps));
    u.abi=2; assert(ps2gs_validate_upload(&u,&caps)==-EINVAL);
    u=valid(); u.width=720; assert(ps2gs_validate_upload(&u,&caps)==-ESTALE);
    u=valid(); u.bits_per_pixel=16; assert(ps2gs_validate_upload(&u,&caps)==-ESTALE);
    u=valid(); u.x=2047; assert(ps2gs_validate_upload(&u,&caps)==-EINVAL);
    u=valid(); u.y=575; assert(ps2gs_validate_upload(&u,&caps)==-EINVAL);
    u=valid(); u.image_width=UINT32_MAX; assert(ps2gs_validate_upload(&u,&caps)==-EINVAL);
    u=valid(); u.image_height=0; assert(ps2gs_validate_upload(&u,&caps)==-EINVAL);
    u=valid(); u.bytes=23; assert(ps2gs_validate_upload(&u,&caps)==-EINVAL);
    u=valid(); u.bytes=3073; assert(ps2gs_validate_upload(&u,&caps)==-EINVAL);
    u=valid(); u.reserved=1; assert(ps2gs_validate_upload(&u,&caps)==-EINVAL);
    u=valid(); u.pixels=UINT64_C(1)<<32; assert(ps2gs_validate_upload(&u,&caps)==-EFAULT);
    u=valid(); u.image_width=768; u.image_height=1; u.bytes=3072;
    assert(!ps2gs_validate_upload(&u,&caps));
    u.image_height=2; assert(ps2gs_validate_upload(&u,&caps)==-EINVAL);
    struct ps2gs_caps c16=caps; c16.bits_per_pixel=16;
    u=valid(); u.bits_per_pixel=16; u.image_height=1; u.bytes=6;
    assert(!ps2gs_validate_upload(&u,&c16));
}
static void packet(void)
{
    _Static_assert(sizeof(union package)==16,"GIF quadword layout");
    _Static_assert(sizeof(struct ps2gs_upload)==48,"upload ABI");
    union package buffer[200]; unsigned char source[3072];
    for (unsigned i=0;i<sizeof(source);++i) source[i]=i&255;
    memset(buffer,0xa5,sizeof(buffer));
    struct ps2gs_upload u=valid();
    assert(ps2gs_package_upload(buffer,&u,source,32,gs_psm_ct32)==10);
    /* Numeric register encodings detect errors independently of the field names. */
    assert(buffer[0].gif.doubleword[0]==(UINT64_C(1)<<60 | 4));
    assert(buffer[0].gif.doubleword[1]==0xe);
    assert(buffer[1].gif.doubleword[0]==UINT64_C(32)<<48);
    assert(buffer[1].gif.doubleword[1]==0x50);
    assert(buffer[2].gif.doubleword[0]==(UINT64_C(4)<<32 | UINT64_C(5)<<48));
    assert(buffer[2].gif.doubleword[1]==0x51);
    assert(buffer[3].gif.doubleword[0]==(UINT64_C(2)<<32 | 3));
    assert(buffer[3].gif.doubleword[1]==0x52);
    assert(buffer[4].gif.doubleword[0]==0 && buffer[4].gif.doubleword[1]==0x53);
    assert(buffer[5].gif.doubleword[0]==(UINT64_C(2)<<58 | 2));
    assert(!memcmp(&buffer[6],source,24));
    for (unsigned i=24;i<32;++i) assert(((unsigned char *)&buffer[6])[i]==0);
    assert(buffer[8].gif.doubleword[0]==(UINT64_C(1)<<60 | 0x8001));
    assert(buffer[9].gif.doubleword[0]==0 && buffer[9].gif.doubleword[1]==0x61);
    assert(buffer[10].gif.doubleword[0]==UINT64_C(0xa5a5a5a5a5a5a5a5));
    u.bits_per_pixel=16; u.image_height=1; u.bytes=6;
    assert(ps2gs_package_upload(buffer,&u,source,12,gs_psm_ct16)==9);
    assert(buffer[1].gif.doubleword[0]==(UINT64_C(2)<<56 | UINT64_C(12)<<48));
    assert(!memcmp(&buffer[6],source,6));
    for (unsigned i=6;i<16;++i) assert(((unsigned char *)&buffer[6])[i]==0);
    u.bits_per_pixel=32; u.image_width=768; u.bytes=3072;
    assert(ps2gs_package_upload(buffer,&u,source,32,gs_psm_ct32)==200);
    assert(!memcmp(&buffer[6],source,3072));
    assert(buffer[199].gif.doubleword[1]==0x61);
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if (!strcmp(argv[1],"validation")) validation();
    else if (!strcmp(argv[1],"packet")) packet();
    else return 2;
    puts("PS2 kernel GS image test passed"); return 0;
}
