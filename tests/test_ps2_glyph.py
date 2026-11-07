"""Native bitmap plans and actual core Xorg GC glyph callbacks/lifetime."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_ps2_pipe import ROOT,PIPE_SOURCES,headers as pipe_headers,render_flags
from test_ps2_exa import SYSROOT
GLYPH_SOURCES=PIPE_SOURCES+['graphics/ps2_gs_bitmap.c','desktop/xorg/ps2_glyph.c']
CASES=('bitmap','invalid','poly','image','redirected','lifetime','unsupported','failure')

def glyph_flags():
    return render_flags()+['-idirafter'+str(SYSROOT/'usr/include')]

def headers(folder):
    pipe_headers(folder)
    folder=Path(folder)
    text=(folder/'previous-render-backends.c').read_text()
    text=text.replace('Bool dixRegisterPrivateKey(', 'Bool pipe_register_private(')
    (folder/'previous-render-backends.c').write_text(text)
    text=(ROOT/'tests/graphics/ps2-pipe.c').read_text()
    text=text.replace('int __wrap_ioctl(', 'int pipe_ioctl(').replace('int main(', 'int previous_pipe_main(')
    for name in ('lifetime_case','failure_case','unsupported_case'):
        text=text.replace(name,'previous_pipe_'+name)
    (folder/'previous-pipe.c').write_text(text)

@unittest.skipUnless(shutil.which('cc'),'native compiler required')
class Glyphs(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory(); headers(cls.folder.name)
        cls.binary=Path(cls.folder.name)/'glyph'
        subprocess.run(['cc','-std=gnu17','-Wall','-Wextra','-Werror','-Wno-unused-function',
            '-fsanitize=address,undefined','-fno-pie','-no-pie','-DUSE_GCC_ATOMIC_BUILTINS',
            *glyph_flags(),'-I'+cls.folder.name,'-I'+str(ROOT/'tests/graphics'),
            *(str(ROOT/p) for p in GLYPH_SOURCES),str(ROOT/'tests/graphics/ps2-glyph.c'),
            *['-Wl,--wrap='+n for n in ('open','close','ioctl','nanosleep')],
            '-latomic','-lpixman-1','-lm','-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def run_case(self,c): subprocess.run([str(self.binary),c],check=True,timeout=15)
    def test_both_bit_orders_odd_width_padding_and_independent_clipped_pixels(self): self.run_case('bitmap')
    def test_whole_plan_preflight_and_empty_clip_do_not_submit(self): self.run_case('invalid')
    def test_real_poly_glyph_callback_metrics_translation_and_multi_box_clip(self): self.run_case('poly')
    def test_image_text_background_negative_advance_and_forced_copy_state(self): self.run_case('image')
    def test_owned_offscreen_storage_origins_and_mesa_depth_binding_restored(self): self.run_case('redirected')
    def test_gc_lease_defers_context_close_until_callback_detachment(self): self.run_case('lifetime')
    def test_unsupported_masks_raster_states_and_fonts_never_draw(self): self.run_case('unsupported')
    def test_hardware_failure_stops_work_and_never_replays(self): self.run_case('failure')
if __name__=='__main__': unittest.main()
