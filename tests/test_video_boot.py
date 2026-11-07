"""Run the real Linux mode parser against the real PS2 table and boot profiles."""
from pathlib import Path
import json
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import video
import kernel


@unittest.skipUnless(shutil.which('cc'), 'requires native C compiler')
class BootScanSelection(unittest.TestCase):
    def test_profiles_select_exact_runtime_timings(self):
        # These stubs isolate selection from device I/O. The actual parser and
        # mode table are copied unchanged, including their interlaced-first order.
        harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef uint32_t u32;
#define FB_VMODE_NONINTERLACED 0
#define FB_VMODE_INTERLACED 1
#define FB_SYNC_HOR_HIGH_ACT 1
#define FB_SYNC_VERT_HIGH_ACT 2
#define FB_SYNC_BROADCAST 16
#define FB_MODE_IS_STANDARD 1
#define FB_MODE_IS_VESA 4
#define FB_ACTIVATE_TEST 2
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define __free(x)
#define DPRINTK(...) ((void)0)
#define simple_strtol strtol
#undef abs
/* Kernel abs() casts unsigned inputs to the same-width signed type. */
#define abs(x) ({ int _value=(int)(x); _value<0 ? -_value : _value; })
#define name_matches(v,s,l) ((v).name && !strncmp((s),(v).name,(l)) && strlen((v).name)==(l))
#define res_matches(v,x,y) ((v).xres==(x) && (v).yres==(y))
struct fb_videomode {
    const char *name; unsigned refresh,xres,yres,pixclock;
    unsigned left_margin,right_margin,upper_margin,lower_margin;
    unsigned hsync_len,vsync_len,sync,vmode,flag;
};
struct fb_var_screeninfo {
    unsigned xres,yres,xres_virtual,yres_virtual,xoffset,yoffset;
    unsigned bits_per_pixel,activate,pixclock;
    unsigned left_margin,right_margin,upper_margin,lower_margin;
    unsigned hsync_len,vsync_len,sync,vmode;
};
struct fb_info;
struct fb_ops { int (*fb_check_var)(struct fb_var_screeninfo *,struct fb_info *); };
struct fb_info {
    const struct fb_ops *fbops;
    struct { unsigned vfmin,vfmax,hfmin,hfmax,dclkmax; } monspecs;
};
static void fb_get_options(const char *device,char **options) {
    (void)device; *options=NULL;
}
static int fb_find_mode_cvt(struct fb_videomode *mode,int margins,int rb) {
    (void)mode;(void)margins;(void)rb; return -1;
}
#include "ps2fb-mode-table.c"
#define modedb standard_modes
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "fb-mode-selection.c"
#pragma GCC diagnostic pop
int main(int argc,char **argv) {
    assert(argc==7);
    const struct fb_ops ops={0};struct fb_info info={.fbops=&ops};
    struct fb_var_screeninfo selected={0};
    assert(fb_find_mode(&selected,&info,argv[1],standard_modes,
        ARRAY_SIZE(standard_modes),NULL,32)==1);
    unsigned width=atoi(argv[2]),height=atoi(argv[3]),hz=atoi(argv[4]);
    unsigned interlaced=atoi(argv[5]),broadcast=atoi(argv[6]);
    assert(selected.xres==width && selected.yres==height);
    assert(!!(selected.vmode&FB_VMODE_INTERLACED)==interlaced);
    assert(!!(selected.sync&FB_SYNC_BROADCAST)==broadcast);
    const struct fb_videomode *runtime=NULL;
    for(unsigned i=0;i<ARRAY_SIZE(standard_modes);i++) {
        const struct fb_videomode *m=standard_modes+i;
        if(m->xres==width && m->yres==height && m->refresh==hz &&
           !!(m->vmode&FB_VMODE_INTERLACED)==interlaced &&
           !!(m->sync&FB_SYNC_BROADCAST)==broadcast) { runtime=m; break; }
    }
    assert(runtime);
    assert(selected.pixclock==runtime->pixclock);
    assert(selected.left_margin==runtime->left_margin && selected.right_margin==runtime->right_margin);
    assert(selected.upper_margin==runtime->upper_margin && selected.lower_margin==runtime->lower_margin);
    assert(selected.hsync_len==runtime->hsync_len && selected.vsync_len==runtime->vsync_len);
    printf("%s -> %ux%u%s %uHz clock=%u\n",argv[1],width,height,interlaced?"i":"p",hz,selected.pixclock);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            source = folder / 'boot-modes.c'
            source.write_text(harness)
            exe = folder / 'boot-modes'
            subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-Wno-missing-field-initializers', '-fsanitize=address,undefined',
                            '-fno-pie', '-no-pie', '-I' + str(ROOT / 'tests/fixtures'),
                            str(source), '-o', str(exe)], check=True)
            for mode in video.MODES:
                with self.subTest(mode=mode['id']):
                    subprocess.run([str(exe), mode['spec'], str(mode['width']),
                                    str(mode['height']), str(mode['hz']),
                                    str(int(mode['interlaced'])),
                                    str(int(mode['family'] == 'component'))], check=True)
            # Demonstrate the reported failure using the former boot argument:
            # it chooses 576i rather than 576p even though the geometry is equal.
            subprocess.run([str(exe), '720x576@50', '720', '576', '50', '1', '1'], check=True)
            self.assertIn('video=ps2fb:720x576p@50', kernel.kernel_command_line('/dev/sda2'))
            config = (ROOT / 'configs/kernel.config').read_text()
            self.assertIn('video=ps2fb:720x576p@50', config)


if __name__ == '__main__':
    unittest.main()
