"""Actual Gallium screen/context callbacks, references and Xorg lifetime lease."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_ps2_sampler import ROOT,SOURCES,render_flags,headers as sampler_headers
PIPE_SOURCES=SOURCES+['graphics/ps2_gs_lut.c','graphics/ps2_gs_transfer.c','graphics/ps2_gs_logic.c',
    'mesa/ps2/ps2_pipe.c','mesa/ps2/ps2_copy_batch.c','mesa/ps2/ps2_blit.c',
    'mesa/ps2/ps2_rop.c','mesa/ps2/ps2_filter.c','mesa/ps2/ps2_multiply.c','mesa/ps2/ps2_divide.c','mesa/ps2/ps2_wide.c','mesa/ps2/ps2_wide_filter.c','mesa/ps2/ps2_gradient.c','mesa/ps2/ps2_packed.c',
    'mesa/ps2/ps2_coverage.c','mesa/ps2/ps2_format.c','mesa/ps2/ps2_tile.c','mesa/ps2/ps2_composite.c','desktop/xorg/ps2_context_bridge.c']
def headers(folder):
    sampler_headers(folder)
    text=(ROOT/'tests/graphics/ps2-sampler.c').read_text()
    text=text.replace('int __wrap_ioctl(', 'int sampler_ioctl(').replace('int main(', 'int previous_sampler_main(')
    (Path(folder)/'previous-sampler.c').write_text(text)
@unittest.skipUnless(shutil.which('cc'),'native compiler required')
class Pipe(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory(); headers(cls.folder.name)
        cls.binary=Path(cls.folder.name)/'pipe'
        subprocess.run(['cc','-std=gnu17','-Wall','-Wextra','-Werror','-Wno-unused-function',
            '-fsanitize=address,undefined','-fno-pie','-no-pie','-DUSE_GCC_ATOMIC_BUILTINS',
            *render_flags(),'-I'+cls.folder.name,'-I'+str(ROOT/'tests/graphics'),
            *(str(ROOT/p) for p in PIPE_SOURCES),str(ROOT/'tests/graphics/ps2-pipe.c'),
            *['-Wl,--wrap='+n for n in ('open','close','ioctl','nanosleep')],'-latomic','-lpixman-1','-lm','-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def run_case(self,case): subprocess.run([str(self.binary),case],check=True,timeout=15)
    def test_real_screen_surface_framebuffer_and_native_clear(self): self.run_case('framebuffer')
    def test_sampler_ownership_native_upload_and_target_restore(self): self.run_case('sampler')
    def test_screen_context_destruction_deferred_by_real_resource_references(self): self.run_case('lifetime')
    def test_xorg_callbacks_hold_context_until_full_table_restoration(self): self.run_case('xorg')
    def test_failed_hardware_work_not_replayed_during_callbacks_or_cleanup(self): self.run_case('failure')
    def test_unsupported_shader_draw_and_cross_context_targets_never_render(self): self.run_case('unsupported')
if __name__=='__main__': unittest.main()
