"""Actual Mesa resource/raster probe, signal handler and close-before-text paths."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_ps2_render_backends import ROOT, render_flags
PROBE_SOURCES=['graphics/ps2_gs_surface.c','graphics/ps2_gs_render.c',
    'mesa/ps2/ps2_resource.c','mesa/ps2/ps2_render_target.c']
@unittest.skipUnless(shutil.which('cc'), 'native compiler required')
class RenderProbe(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory(); cls.binary=Path(cls.folder.name)/'probe'
        common=['cc','-std=gnu17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-pie','-no-pie',*render_flags()]
        obj=Path(cls.folder.name)/'main.o'
        subprocess.run([*common,'-Dmain=ps2_render_probe_main','-c',str(ROOT/'desktop/ps2-gs-render-test.c'),'-o',str(obj)],check=True)
        subprocess.run([*common,str(obj),*(str(ROOT/p) for p in PROBE_SOURCES),str(ROOT/'tests/graphics/ps2-render-probe.c'),
            *['-Wl,--wrap='+n for n in ('open','close','ioctl','nanosleep')],'-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def run_case(self,case): subprocess.run([str(self.binary),case],check=True,timeout=15)
    def test_success_real_mesa_resources_and_text_restoration(self): self.run_case('success')
    def test_render_error_has_no_later_draw_copy_or_replay(self): self.run_case('failure')
    def test_sigint_releases_owned_resources_before_console_restoration(self): self.run_case('interrupt')
if __name__=='__main__': unittest.main()
