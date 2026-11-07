"""Actual GC/text diagnostic with the existing GPU/console cleanup harness."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_ps2_gc import ROOT, GC_SOURCES, glyph_flags


def probe_fixture(folder):
    text=(ROOT/'tests/graphics/ps2-glyph-probe.c').read_text()
    text=text.replace('ps2_glyph_probe_main','ps2_gc_probe_main').replace('ps2-gs-glyph-test','ps2-gs-gc-test')
    text=text.replace('native glyph probe lifecycle PASS','native GC probe lifecycle PASS')
    path=Path(folder)/'gc-probe.c'
    path.write_text(text)
    return path


@unittest.skipUnless(shutil.which('cc'),'native compiler required')
class GCProbe(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory()
        cls.binary=Path(cls.folder.name)/'probe'
        common=['cc','-std=gnu17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                '-fno-pie','-no-pie','-DUSE_GCC_ATOMIC_BUILTINS',*glyph_flags()]
        obj=Path(cls.folder.name)/'main.o'
        subprocess.run([*common,'-Dmain=ps2_gc_probe_main','-c',str(ROOT/'desktop/ps2-gs-gc-test.c'),'-o',str(obj)],check=True)
        subprocess.run([*common,str(obj),*(str(ROOT/p) for p in GC_SOURCES),str(probe_fixture(cls.folder.name)),
            *['-Wl,--wrap='+n for n in ('open','close','ioctl','nanosleep')],
            '-latomic','-lpixman-1','-lm','-o',str(cls.binary)],check=True)

    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()

    def run_case(self,case): subprocess.run([str(self.binary),case],check=True,timeout=15)
    def test_real_gc_text_request_path_and_early_private_registration(self): self.run_case('success')
    def test_hardware_failure_destroys_gc_and_restores_console_without_replay(self): self.run_case('failure')
    def test_sigint_closes_gc_screen_and_gs_before_restoring_text(self): self.run_case('interrupt')


if __name__=='__main__': unittest.main()
