"""Exercise the actual GS validator and packed GIF register structures."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which('cc'), 'native C compiler required')
class GSCommands(unittest.TestCase):
    def test_bounds_unsupported_operations_and_context_two_encoding(self):
        source = (ROOT/'kernel/new-drivers/ps2fb-gs.h').read_text()
        functions = source[source.index('static bool ps2gs_zero_vertex'):
                           source.index('static int ps2gs_execute')]
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            (folder/'asm').mkdir()
            (folder/'asm/bitfield.h').write_text('#define __BITFIELD_FIELD(field, more) more field;\n')
            for name in ('gs.h', 'gif.h'):
                shutil.copyfile(ROOT/'kernel/vendor/frno7/snapshot/arch/mips/include/uapi/asm'/name,
                                folder/name)
            harness = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <linux/types.h>
#include "ps2-gs.h"
#include "gif.h"
typedef uint32_t u32;
struct fb_info { struct { u32 xres, yres, xres_virtual, bits_per_pixel; } var; };
union package { union gif_data gif; unsigned char bytes[16]; };
#define GIF_PACKAGE_AD(p) ((p)++)->gif.packed.ad = (struct gif_packed_ad)
static u32 var_to_fbw(const void *var) { (void)var; return 10; }
static u32 var_to_psm(const void *var, const struct fb_info *info)
{ (void)var; return info->var.bits_per_pixel == 16 ? 2 : 0; }
''' + functions + r'''
int main(void) {
    struct ps2gs_caps caps = { .width=640, .height=480, .bits_per_pixel=16,
        .features=PS2GS_HW_FILL|PS2GS_HW_COPY|PS2GS_HW_TRIANGLE|PS2GS_HW_DEPTH16 };
    struct ps2gs_command c = { .op=PS2GS_FILL, .width=640, .height=480,
        .v={{.rgba=0x80112233}} };
    assert(sizeof(c)==64 && sizeof(struct ps2gs_caps)==80);
    assert(sizeof(struct ps2gs_submit)==32 && sizeof(union package)==16);
    assert(ps2gs_validate(&c,&caps)==0);
    c.width=641;assert(ps2gs_validate(&c,&caps)==-EINVAL);c.width=640;
    c.v[0].x=UINT32_MAX;assert(ps2gs_validate(&c,&caps)==-EINVAL);c.v[0].x=0;
    c.v[0].y=1;assert(ps2gs_validate(&c,&caps)==-EINVAL);c.v[0].y=0;
    c.width=0;assert(ps2gs_validate(&c,&caps)==-EINVAL);c.width=640;
    c.flags=0x80000000;assert(ps2gs_validate(&c,&caps)==-EOPNOTSUPP);c.flags=0;
    c.v[1].rgba=1;assert(ps2gs_validate(&c,&caps)==-EINVAL);c.v[1].rgba=0;
    c.op=PS2GS_COPY;c.v[0].rgba=0;assert(ps2gs_validate(&c,&caps)==0);
    c.width=639;assert(ps2gs_validate(&c,&caps)==-EOPNOTSUPP);
    caps.bits_per_pixel=32;c.width=638;assert(ps2gs_validate(&c,&caps)==0);
    c.v[1].x=640*16;assert(ps2gs_validate(&c,&caps)==-EINVAL);
    c=(struct ps2gs_command){.op=PS2GS_TRIANGLE,.flags=PS2GS_DEPTH_TEST,
        .v={{1,2,3,0x80112233},{640*16,0,65535,0},{0,480*16,0,0}}};
    assert(ps2gs_validate(&c,&caps)==0);
    c.v[0].z=65536;assert(ps2gs_validate(&c,&caps)==-EINVAL);c.v[0].z=3;
    c.v[0].x=640*16+1;assert(ps2gs_validate(&c,&caps)==-EINVAL);c.v[0].x=1;
    caps.features &= ~PS2GS_HW_DEPTH16;assert(ps2gs_validate(&c,&caps)==-ENOSPC);
    c.flags=0;assert(ps2gs_validate(&c,&caps)==0);
    c=(struct ps2gs_command){.op=PS2GS_CLEAR_DEPTH};
    assert(ps2gs_validate(&c,&caps)==-ENOSPC);
    caps.features |= PS2GS_HW_DEPTH16;assert(ps2gs_validate(&c,&caps)==0);
    c.v[2].x=1;assert(ps2gs_validate(&c,&caps)==-EINVAL);
    c=(struct ps2gs_command){.op=99};assert(ps2gs_validate(&c,&caps)==-EOPNOTSUPP);
    union package packets[16]={0};
    struct fb_info info={.var={640,480,640,16}};
    c=(struct ps2gs_command){.op=PS2GS_TRIANGLE,.flags=PS2GS_DEPTH_TEST};
    assert(ps2gs_environment(packets,&info,&c,0x300000)-packets==6);
    assert(packets[0].gif.packed.ad.addr==gs_addr_frame_2);
    assert(packets[0].gif.packed.ad.data.frame_1.psm==2);
    assert(packets[2].gif.packed.ad.data.scissor_1.scax1==639);
    assert(packets[2].gif.packed.ad.data.scissor_1.scay1==479);
    assert(packets[3].gif.packed.ad.data.zbuf.zbp==0x300000/8192);
    assert(packets[3].gif.packed.ad.data.zbuf.psm==2);
    assert(packets[3].gif.packed.ad.data.zbuf.zmsk==gs_zbuf_on);
    assert(packets[4].gif.packed.ad.data.test_1.zte==1);
    assert(packets[4].gif.packed.ad.data.test_1.ztst==gs_depth_greater);
    assert(packets[5].gif.packed.ad.addr==gs_addr_prmodecont);
    assert(packets[5].gif.packed.ad.data.prmodecont.ac==1);
    c.op=PS2GS_CLEAR_DEPTH;c.flags=0;ps2gs_environment(packets,&info,&c,0x300000);
    assert(packets[0].gif.packed.ad.data.frame_1.fbmsk==0xffffffff);
    assert(packets[4].gif.packed.ad.data.test_1.ztst==gs_depth_pass);
    c.op=PS2GS_FILL;ps2gs_environment(packets,&info,&c,0x300000);
    assert(packets[3].gif.packed.ad.data.zbuf.zmsk==gs_zbuf_off);
    info.var.bits_per_pixel=32;
    c.op=PS2GS_TRIANGLE;c.flags=PS2GS_DEPTH_TEST;
    ps2gs_environment(packets,&info,&c,0x300000);
    assert(packets[0].gif.packed.ad.data.frame_1.psm==0);
    assert(packets[3].gif.packed.ad.data.zbuf.psm==0xa);
    c.op=PS2GS_CLEAR_DEPTH;c.flags=0;
    ps2gs_environment(packets,&info,&c,0x300000);
    assert(packets[3].gif.packed.ad.data.zbuf.psm==0xa);
    assert(packets[0].gif.packed.ad.data.frame_1.fbmsk==0xffffffff);
    assert(ps2gs_vertex(packets,(struct ps2gs_vertex){16,32,65535,0x80443322})-packets==2);
    assert(packets[0].gif.packed.ad.addr==gs_addr_rgbaq);
    assert(packets[0].gif.packed.ad.data.rgbaq.r==0x22);
    assert(packets[0].gif.packed.ad.data.rgbaq.g==0x33);
    assert(packets[0].gif.packed.ad.data.rgbaq.b==0x44);
    assert(packets[0].gif.packed.ad.data.rgbaq.a==0x80);
    assert(packets[1].gif.packed.ad.addr==gs_addr_xyz2);
    assert(packets[1].gif.packed.ad.data.xyz2.z==65535);
    puts("GS bounds and context-2 packets passed");
}
'''
            (folder/'test.c').write_text(harness)
            subprocess.run(['cc','-std=gnu17','-Wall','-Wextra','-Werror',
                '-fsanitize=address,undefined','-fno-pie','-no-pie','-I'+str(folder),
                '-I'+str(ROOT/'kernel/new-drivers'),str(folder/'test.c'),
                '-o',str(folder/'test')],check=True)
            subprocess.run([str(folder/'test')],check=True)


if __name__ == '__main__':
    unittest.main()
