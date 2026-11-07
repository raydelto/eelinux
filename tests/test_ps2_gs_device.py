"""Provider lifetime with syscall/locking scaffold; not kernel lockdep or GPU proof."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_ps2_gs_surfaces import ROOT,SOURCE,prepare_surface_headers

@unittest.skipUnless(shutil.which('cc') and (SOURCE/'drivers/video/fbdev/ps2fb-gs-device.h').is_file(),
                    'native compiler and candidate kernel required')
class GSDevice(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory(); cls.binary=Path(cls.folder.name)/'device'
        prepare_surface_headers(cls.folder.name)
        subprocess.run(['cc','-std=gnu17','-Wall','-Wextra','-Werror',
            '-Wno-unused-parameter','-Wno-unused-function','-pthread',
            '-fsanitize=address,undefined','-fno-pie','-no-pie',
            '-I'+cls.folder.name,'-I'+str(ROOT/'kernel/new-drivers'),
            str(ROOT/'tests/graphics/ps2-gs-device.c'),'-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def run_case(self,case): subprocess.run([str(self.binary),case],check=True,timeout=20)
    def test_exclusive_owner_maps_graphics_mode_rollback_and_stale_epoch(self): self.run_case('ownership')
    def test_hardware_timeout_poison_and_no_replay(self): self.run_case('failures')
    def test_disconnect_with_concurrent_ioctl_and_live_file(self): self.run_case('disconnect')

if __name__=='__main__': unittest.main()
