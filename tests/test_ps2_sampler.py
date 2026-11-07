"""Real Mesa sampler/reference/stage callbacks and shared Xorg ordering."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_ps2_render_backends import ROOT, render_flags, RENDER_SOURCES
SOURCES=RENDER_SOURCES+['graphics/ps2_gs_texture.c','mesa/ps2/ps2_sampler.c','mesa/ps2/ps2_textured_draw.c',
    'desktop/xorg/ps2_texture_exa.c']
def headers(folder):
    text=(ROOT/'tests/graphics/ps2-render-backends.c').read_text()
    text=text.replace('int __wrap_ioctl(', 'int render_ioctl(').replace('int main(', 'int previous_render_backends_main(')
    (Path(folder)/'previous-render-backends.c').write_text(text)
@unittest.skipUnless(shutil.which('cc'), 'native compiler required')
class Sampler(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory(); cls.binary=Path(cls.folder.name)/'sampler'; headers(cls.folder.name)
        subprocess.run(['cc','-std=gnu17','-Wall','-Wextra','-Werror','-Wno-unused-function',
            '-fsanitize=address,undefined','-fno-pie','-no-pie',*render_flags(),'-I'+cls.folder.name,
            '-I'+str(ROOT/'tests/graphics'),*(str(ROOT/p) for p in SOURCES),str(ROOT/'tests/graphics/ps2-sampler.c'),
            *['-Wl,--wrap='+n for n in ('open','close','ioctl','nanosleep')],'-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def run_case(self,c): subprocess.run([str(self.binary),c],check=True,timeout=15)
    def test_real_sampler_views_references_state_and_textured_stage(self): self.run_case('mesa')
    def test_flush_upload_order_stq_retry_and_hardware_failure_no_replay(self): self.run_case('transport')
    def test_existing_xorg_order_with_sampler_capability(self): self.run_case('xorg')
    def test_real_exa_textured_composite_bounds_format_and_mesa_binding_restoration(self): self.run_case('composite')
if __name__=='__main__': unittest.main()
