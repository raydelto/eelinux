"""Actual offscreen probe lifecycle including interruption and GS failure."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]

@unittest.skipUnless(shutil.which('cc'),'native compiler required')
class SurfaceProbe(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory(); cls.binary=Path(cls.folder.name)/'probe'
        flags=['cc','-std=gnu17','-Wall','-Wextra','-Werror',
            '-fsanitize=address,undefined','-fno-pie','-no-pie',
            '-I'+str(ROOT/'graphics'),'-I'+str(ROOT/'kernel/new-drivers')]
        obj=Path(cls.folder.name)/'probe.o'
        subprocess.run([*flags,'-Dmain=ps2_surface_probe_main','-c',
                        str(ROOT/'desktop/ps2-gs-surface-test.c'),'-o',str(obj)],check=True)
        subprocess.run([*flags,str(obj),str(ROOT/'graphics/ps2_gs_surface.c'),
            str(ROOT/'tests/graphics/ps2-surface-probe.c'),
            '-Wl,--wrap=open','-Wl,--wrap=close','-Wl,--wrap=ioctl','-Wl,--wrap=nanosleep',
            '-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def run_case(self,case): subprocess.run([str(self.binary),case],check=True,timeout=15)
    def test_success_release_before_console_restore(self): self.run_case('success')
    def test_failed_upload_stops_copies_and_releases_resources(self): self.run_case('failure')
    def test_sigint_releases_owner_and_restores_text(self): self.run_case('interrupt')

if __name__=='__main__': unittest.main()
