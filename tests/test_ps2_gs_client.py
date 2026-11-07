"""Execute the real GS transport with wrapped syscalls; no graphics device is opened."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which('cc'), 'native C compiler required')
class GSClient(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory()
        cls.binary = Path(cls.folder.name) / 'client'
        subprocess.run(['cc', '-std=gnu17', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
            '-I'+str(ROOT/'graphics'), '-I'+str(ROOT/'kernel/new-drivers'),
            str(ROOT/'graphics/ps2_gs_client.c'), str(ROOT/'tests/graphics/ps2-gs-client.c'),
            '-Wl,--wrap=open,--wrap=close,--wrap=ioctl,--wrap=nanosleep',
            '-o', str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.folder.cleanup()

    def run_case(self, case):
        subprocess.run([str(self.binary), case], check=True, timeout=10)

    def test_batch_limit_mode_guard_and_close_discards(self): self.run_case('batching')
    def test_contention_retry_but_never_replay_hardware_failure(self): self.run_case('errors')
    def test_overflow_copy_alignment_and_depth_limits(self): self.run_case('bounds')
    def test_reject_incompatible_capabilities(self): self.run_case('capabilities')
    def test_native_rgb555_and_rgba(self): self.run_case('colors')
    def test_upload_wide_rows_padding_and_odd_16bit_pixels(self): self.run_case('image')
    def test_upload_bounds_overflow_and_no_replay(self): self.run_case('image-errors')


if __name__ == '__main__':
    unittest.main()
