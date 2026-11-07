"""Actual texture diagnostic success, hardware failure and signal cleanup."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_ps2_sampler import ROOT,render_flags
SOURCES=['graphics/ps2_gs_surface.c','graphics/ps2_gs_render.c','graphics/ps2_gs_texture.c',
    'mesa/ps2/ps2_resource.c','mesa/ps2/ps2_render_target.c','mesa/ps2/ps2_sampler.c']
@unittest.skipUnless(shutil.which('cc'),'native compiler required')
class Probe(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory(); cls.binary=Path(cls.folder.name)/'probe'
        common=['cc','-std=gnu17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
            '-fno-pie','-no-pie',*render_flags()]
        obj=Path(cls.folder.name)/'main.o'
        subprocess.run([*common,'-Dmain=ps2_texture_probe_main','-c',str(ROOT/'desktop/ps2-gs-texture-test.c'),'-o',str(obj)],check=True)
        subprocess.run([*common,str(obj),*(str(ROOT/p) for p in SOURCES),str(ROOT/'tests/graphics/ps2-texture-probe.c'),
            *['-Wl,--wrap='+n for n in ('open','close','ioctl','nanosleep')],'-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def run_case(self,c): subprocess.run([str(self.binary),c],check=True,timeout=15)
    def test_success_updated_texture_stq_and_real_reference_cleanup(self): self.run_case('success')
    def test_hardware_failure_no_later_sampling_copy_or_replay(self): self.run_case('failure')
    def test_sigint_resources_released_before_text_restoration(self): self.run_case('interrupt')
if __name__=='__main__': unittest.main()
