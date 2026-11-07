"""Execute EDID selection, GS allocation checks and timed mode rollback."""
from pathlib import Path
import json
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import video
from kernel_video_port import adapt_framebuffer


def edid(vics=(), established=False):
    data=bytearray(256 if vics else 128)
    data[:8]=bytes((0,255,255,255,255,255,255,0))
    data[18:20]=bytes((1,4));data[38:54]=bytes((1,1))*8
    if established:data[35]=33;data[36]=8
    if vics:
        data[126]=1;data[128:132]=bytes((2,3,5+len(vics),0))
        data[132]=64+len(vics);data[133:133+len(vics)]=bytes(vics)
    for offset in range(0,len(data),128):data[offset+127]=(-sum(data[offset:offset+127]))&255
    return data


class VideoProfiles(unittest.TestCase):
    def test_experimental_modes_are_not_persistent_boot_choices(self):
        self.assertIn('1080i',video.BOOT_MODES)
        self.assertIn('800x600',video.BOOT_MODES)
        self.assertNotIn('1080p',video.BOOT_MODES)
        self.assertNotIn('1080p60',video.BOOT_MODES)


@unittest.skipUnless(shutil.which('cc'),'requires native C compiler')
class VideoRuntime(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory();cls.work=Path(cls.tmp.name)
        (cls.work/'modes.inc').write_text(video.header())
        cls.exe=cls.work/'ps2-video'
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',
            '-fsanitize=address,undefined','-fno-pie','-no-pie','-I'+str(cls.work),
            str(ROOT/'video/ps2-video.c'),'-o',str(cls.exe)],check=True)

    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()

    def setUp(self):
        self.folder=tempfile.TemporaryDirectory();self.fb=Path(self.folder.name)
        (self.fb/'modes').write_text(''.join(m['sysfs']+'\n' for m in video.MODES))
        (self.fb/'mode').write_text('U:576x460i-50\n');(self.fb/'bits_per_pixel').write_text('32\n')

    def tearDown(self):self.folder.cleanup()

    def run_video(self,*args):
        return subprocess.run([str(self.exe),'--sysfs',str(self.fb),*args],capture_output=True,text=True)

    def test_no_edid_and_corrupt_edid_preserve_pal(self):
        self.assertEqual(self.run_video('auto').returncode,0)
        bad=edid((20,));bad[10]^=1;(self.fb/'edid').write_bytes(bad)
        self.assertEqual(self.run_video('auto').returncode,0)
        self.assertNotEqual(self.run_video('--edid',str(self.fb/'edid'),'auto').returncode,0)
        self.assertEqual((self.fb/'mode').read_text(),'U:576x460i-50\n')

    def test_cable_intersection_1080_depth_and_experimental_exclusion(self):
        (self.fb/'edid').write_bytes(edid((19,20,31),True))
        result=self.run_video('auto');self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual((self.fb/'mode').read_text(),'S:1920x1080i-50\n')
        self.assertEqual((self.fb/'bits_per_pixel').read_text(),'16\n')
        result=self.run_video('--rgb-sog','auto');self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual((self.fb/'mode').read_text(),'V:1024x768p-60\n')
        self.assertNotEqual(self.run_video('set','800x600').returncode,0)
        self.assertNotEqual(self.run_video('--experimental','set','1080p').returncode,0)
        (self.fb/'modes').write_text('S:1280x720p-50\n')
        self.assertEqual(self.run_video('auto').returncode,0)
        self.assertEqual((self.fb/'mode').read_text(),'S:1280x720p-50\n')

    def test_preview_restores_on_timeout_and_term(self):
        result=self.run_video('test','720p','1');self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual((self.fb/'mode').read_text(),'U:576x460i-50\n')
        process=subprocess.Popen([str(self.exe),'--sysfs',str(self.fb),'test','1080i','60'],
                                 stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        try:
            for _ in range(100):
                if (self.fb/'mode').read_text()=='S:1920x1080i-50\n':break
                time.sleep(.01)
            else:self.fail('preview did not select requested mode')
            process.send_signal(signal.SIGTERM);out,err=process.communicate(timeout=3)
            self.assertEqual(process.returncode,1,out+err)
            self.assertEqual((self.fb/'mode').read_text(),'U:576x460i-50\n')
            self.assertEqual((self.fb/'bits_per_pixel').read_text(),'32\n')
        finally:
            if process.poll() is None:process.kill();process.wait()

    def test_edid_bounds_detailed_timing_and_malformed_extension(self):
        harness=r'''
#define main video_cli_main
#include "ps2-video.c"
#undef main
#include <assert.h>
static void checksum(uint8_t *p) { p[127]=0; unsigned sum=0;for(unsigned i=0;i<127;i++)sum+=p[i];p[127]=-sum; }
int main(void) {
    uint8_t data[1024]={0};uint8_t magic[]={0,255,255,255,255,255,255,0};
    memcpy(data,magic,8);data[18]=1;data[19]=4;memset(data+38,1,16);
    uint8_t *p=data+54;unsigned clock=7425,w=1920,h=540,hb=720,vb=22;
    p[0]=clock;p[1]=clock>>8;p[2]=w;p[3]=hb;p[4]=((w>>8)<<4)|(hb>>8);
    p[5]=h;p[6]=vb;p[7]=((h>>8)<<4)|(vb>>8);p[17]=128;checksum(data);
    assert(edid_supports(data,128,1920,1080,50,1));
    assert(!edid_supports(data,128,1920,1080,50,0));
    data[126]=1;data[128]=2;data[129]=3;data[130]=5;data[132]=95;checksum(data);checksum(data+128);
    /* Matching base DTD must not bypass a malformed extension. */
    assert(!edid_supports(data,256,1920,1080,50,1));
    for(size_t length=0;length<1024;length++) assert(!edid_valid(data,length));
    assert(budget_bpp(1920,1080)==16 && budget_bpp(1280,1024)==16);
    assert(budget_bpp(1280,720)==32 && budget_bpp(2048,2048)==0);
    return 0;
}
'''
        source=self.fb/'edid-test.c';source.write_text(harness);exe=self.fb/'edid-test'
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
            '-fno-pie','-no-pie','-I'+str(self.work),'-I'+str(ROOT/'video'),str(source),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)

    def test_kernel_prospective_geometry_and_font_budget(self):
        original=(ROOT/'tests/fixtures/ps2fb-mode-check.c').read_text()
        patched=adapt_framebuffer(original)
        with self.assertRaises(RuntimeError):adapt_framebuffer(patched)
        harness=r'''
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>
typedef uint32_t u32;
#define DIV_ROUND_UP(a,b) (((a)+(b)-1)/(b))
#define GS_FB_PAGE_WIDTH 64
#define GS_BLOCK_SIZE 256
#define GS_MEMORY_SIZE 4194304
#define GS_BLOCK_COUNT 16384
#define PALETTE_BLOCK_COUNT 1
#define spin_lock_irqsave(lock,flags) ((void)(lock),(flags)=0)
#define spin_unlock_irqrestore(lock,flags) ((void)(lock),(void)(flags))
static u32 gs_psm_ct16_block_count(u32 w,u32 h) { return w*4*DIV_ROUND_UP(h,8); }
static u32 gs_psm_ct32_block_count(u32 w,u32 h) { return w*8*DIV_ROUND_UP(h,8); }
struct fb_bitfield { unsigned offset,length; };
struct fb_var_screeninfo { u32 xres,yres,xres_virtual,yres_virtual,xoffset,yoffset,bits_per_pixel,rotate;
    struct fb_bitfield red,green,blue,transp; };
struct ps2fb_par { int lock;struct {struct {struct { u32 cols,rows; } block;}tile;}cb; };
struct fb_info { void *par,*tileops,*modelist;struct { u32 smem_len; }fix; };
struct fb_videomode { int dummy; };
static int filled_var_videomode(void *p) {(void)p;return 1;}
static const struct fb_videomode *fb_find_best_mode(void *p,void *q) {(void)p;(void)q;return NULL;}
static void fb_videomode_to_var(void *p,const void *q) {(void)p;(void)q;}
static bool bits_per_pixel_fits(u32 w,u32 h,int b,size_t max);
static int default_bits_per_pixel(u32 w,u32 h,size_t max);
static struct { struct {int gcont;}smode1;}sp;
static int bc;
#define gs_gcont_ycrcb 1
#define gs_gcont_rgbyc 0
''' + patched + r'''
static bool bits_per_pixel_fits(u32 w,u32 h,int b,size_t max) {return display_buffer_size(w,h,b)<=max;}
static int default_bits_per_pixel(u32 w,u32 h,size_t max) {return bits_per_pixel_fits(w,h,32,max)?32:16;}
int main(void) {
    struct ps2fb_par par={0};par.cb.tile.block.cols=4;par.cb.tile.block.rows=1;
    struct fb_info info={.par=&par,.tileops=&par,.fix={.smem_len=GS_MEMORY_SIZE}};
    struct fb_var_screeninfo var={.xres=1920,.yres=1080,.bits_per_pixel=32};
    assert(ps2fb_cb_check_var(&var,&info)==0 && var.bits_per_pixel==16);
    assert(display_buffer_size(800,600,32)==832*600*4);
    var=(struct fb_var_screeninfo){.xres=2048,.yres=1024,.bits_per_pixel=16};
    assert(ps2fb_cb_check_var(&var,&info)==-ENOMEM); /* pixels alone fit; font does not */
    var=(struct fb_var_screeninfo){.xres=800,.yres=600,.bits_per_pixel=32};
    assert(ps2fb_cb_check_var(&var,&info)==0); /* prospective mode fits independently of old allocation */
    var.xres_virtual=UINT32_MAX;assert(ps2fb_cb_check_var(&var,&info)==-EINVAL);
    bc=0;output();assert(sp.smode1.gcont==0);bc=1;output();assert(sp.smode1.gcont==1);
    return 0;
}
'''
        source=self.fb/'gs-test.c';source.write_text(harness);exe=self.fb/'gs-test'
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                        '-fno-pie','-no-pie',str(source),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)


if __name__=='__main__':unittest.main()
