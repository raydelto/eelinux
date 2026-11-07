"""Exercise real EXA callback signatures with the project's Xorg dev headers.

The screen/private registry scaffold does not emulate Xorg's rendering or GPU.
Unsupported access must terminate rather than invoke a CPU renderer.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
SYSROOT=Path(os.environ.get('PS2_XORG_SYSROOT',
    str(Path.home()/'.cache/eelinux/desktop-xorg/output-desktop/staging')))


@unittest.skipUnless(shutil.which('cc') and (SYSROOT/'usr/include/xorg/exa.h').is_file(),
                    'native compiler and actual Xorg server dev headers required')
class EXACallbacks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory()
        cls.binary=Path(cls.folder.name)/'exa'
        subprocess.run(['cc','-std=gnu17','-Wall','-Wextra','-Werror',
            '-fsanitize=address,undefined','-fno-pie','-no-pie',
            '-I'+str(SYSROOT/'usr/include/xorg'), '-I'+str(SYSROOT/'usr/include/pixman-1'),
            '-I'+str(ROOT/'graphics'), '-I'+str(ROOT/'kernel/new-drivers'),
            '-I'+str(ROOT/'desktop/xorg'), str(ROOT/'graphics/ps2_gs_client.c'),
            str(ROOT/'desktop/xorg/ps2_exa.c'), str(ROOT/'tests/graphics/ps2-exa.c'),
            '-Wl,--wrap=ioctl','-o',str(cls.binary)],check=True)

    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()

    def run_case(self, case): subprocess.run([str(self.binary),case],check=True,timeout=10)
    def test_native_solids_full_mask_and_display_format(self): self.run_case('solid')
    def test_front_copy_and_offscreen_rejection(self): self.run_case('copy')
    def test_forbid_cpu_access_and_never_replay_failed_copy(self): self.run_case('failures')
    def test_upload_stride_order_and_no_timeout_replay(self): self.run_case('upload')


if __name__=='__main__': unittest.main()
