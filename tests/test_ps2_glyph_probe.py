"""Actual native Xorg glyph diagnostic success, error and signal cleanup."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_ps2_glyph import ROOT,GLYPH_SOURCES,glyph_flags
@unittest.skipUnless(shutil.which('cc'),'native compiler required')
class GlyphProbe(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory(); cls.binary=Path(cls.folder.name)/'probe'
        common=['cc','-std=gnu17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
            '-fno-pie','-no-pie','-DUSE_GCC_ATOMIC_BUILTINS',*glyph_flags()]
        obj=Path(cls.folder.name)/'main.o'
        subprocess.run([*common,'-Dmain=ps2_glyph_probe_main','-c',str(ROOT/'desktop/ps2-gs-glyph-test.c'),'-o',str(obj)],check=True)
        subprocess.run([*common,str(obj),*(str(ROOT/p) for p in GLYPH_SOURCES),
            str(ROOT/'tests/graphics/ps2-glyph-probe.c'),
            *['-Wl,--wrap='+n for n in ('open','close','ioctl','nanosleep')],
            '-latomic','-lpixman-1','-lm','-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def run_case(self,c): subprocess.run([str(self.binary),c],check=True,timeout=15)
    def test_actual_glyph_diagnostic_and_context_gc_cleanup(self): self.run_case('success')
    def test_failed_hardware_has_no_replay_and_restores_console(self): self.run_case('failure')
    def test_sigint_releases_gc_context_gs_before_console(self): self.run_case('interrupt')
if __name__=='__main__': unittest.main()
