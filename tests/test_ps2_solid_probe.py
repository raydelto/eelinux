"""Actual solid GC diagnostic: success, hardware failure and signal cleanup."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_ps2_solid import ROOT, SOLID_SOURCES, glyph_flags

@unittest.skipUnless(shutil.which('cc'),'native compiler required')
class SolidProbe(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory();cls.binary=Path(cls.folder.name)/'probe'
        common=['cc','-std=gnu17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                '-fno-pie','-no-pie','-DUSE_GCC_ATOMIC_BUILTINS',*glyph_flags()]
        obj=Path(cls.folder.name)/'main.o'
        subprocess.run([*common,'-Dmain=ps2_solid_probe_main','-c',str(ROOT/'desktop/ps2-gs-solid-test.c'),'-o',str(obj)],check=True)
        subprocess.run([*common,str(obj),*(str(ROOT/p) for p in SOLID_SOURCES),str(ROOT/'tests/graphics/ps2-solid-probe.c'),
            *['-Wl,--wrap='+n for n in ('open','close','ioctl','nanosleep')],
            '-latomic','-lpixman-1','-lm','-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls):cls.folder.cleanup()
    def run_case(self,case):subprocess.run([str(self.binary),case],check=True,timeout=20)
    def test_all_six_real_gc_callbacks_and_three_xor_outline_cycles(self):self.run_case('success')
    def test_hardware_failure_releases_gc_mesa_and_gs_before_text_restoration(self):self.run_case('failure')
    def test_sigint_restores_console_without_remaining_outline_commands(self):self.run_case('interrupt')
if __name__=='__main__':unittest.main()
