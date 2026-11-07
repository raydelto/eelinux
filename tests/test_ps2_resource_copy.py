"""Actual Gallium copy callback, GS pixel positions and shared Xorg lifetime."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_ps2_copy import ROOT, COPY_SOURCES, XORG, headers as copy_headers, copy_flags

CASES = ('formats', 'overlap', 'preflight', 'ownership', 'capacity', 'failure', 'shared')

def headers(folder):
    copy_headers(folder)
    text = (ROOT/'tests/graphics/ps2-copy.c').read_text()
    text = text.replace('int main(', 'int previous_copy_main(')
    (Path(folder)/'previous-copy.c').write_text(text)

@unittest.skipUnless(shutil.which('cc'), 'native compiler required')
class ResourceCopy(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory(); headers(cls.folder.name)
        cls.binary = Path(cls.folder.name)/'resource-copy'
        subprocess.run(['cc', '-std=gnu17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
            '-fsanitize=address,undefined', '-fno-pie', '-no-pie', '-DUSE_GCC_ATOMIC_BUILTINS',
            *copy_flags(), '-I'+cls.folder.name, '-I'+str(ROOT/'tests/graphics'),
            *(str(ROOT/p) for p in COPY_SOURCES), str(XORG/'mi/micopy.c'),
            str(ROOT/'tests/graphics/ps2-resource-copy.c'),
            *['-Wl,--wrap='+n for n in ('open', 'close', 'ioctl', 'nanosleep', 'calloc')],
            '-latomic', '-lpixman-1','-lm', '-o', str(cls.binary)], check=True)
    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def run_case(self, case): subprocess.run([str(self.binary), case], check=True, timeout=30)
    def test_rgbx_rgb555_odd_width_edges_and_tiny_surfaces(self): self.run_case('formats')
    def test_self_copy_order_matches_independent_source_snapshot(self): self.run_case('overlap')
    def test_all_box_and_unsigned_destination_validation_precedes_flush(self): self.run_case('preflight')
    def test_foreign_screen_owner_depth_and_format_rejected_before_submission(self): self.run_case('ownership')
    def test_scratch_capacity_failure_leaves_pending_queue_intact(self): self.run_case('capacity')
    def test_device_failure_sticks_without_callback_or_teardown_replay(self): self.run_case('failure')
    def test_sampler_depth_references_and_xorg_copy_share_one_live_context(self): self.run_case('shared')

if __name__ == '__main__': unittest.main()
