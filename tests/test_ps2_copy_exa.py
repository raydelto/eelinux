"""Real EXA copy callbacks with shared Mesa lifetime and independent pixels."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_ps2_resource_copy import ROOT, COPY_SOURCES, XORG, headers as resource_headers, copy_flags

EXA_COPY_SOURCES = COPY_SOURCES + ['desktop/xorg/ps2_copy_exa.c']
CASES = ('formats', 'overlap', 'preflight', 'unsupported', 'shared', 'lifetime', 'failure', 'installation')

def headers(folder):
    resource_headers(folder)
    p = Path(folder)/'previous-glyph.c'
    p.write_text(p.read_text().replace('Bool dixRegisterPrivateKey(', 'Bool original_register_private('))

@unittest.skipUnless(shutil.which('cc'), 'native compiler required')
class EXACopy(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory(); headers(cls.folder.name)
        cls.binary = Path(cls.folder.name)/'exa-copy'
        subprocess.run(['cc', '-std=gnu17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
            '-fsanitize=address,undefined', '-fno-pie', '-no-pie', '-DUSE_GCC_ATOMIC_BUILTINS',
            *copy_flags(), '-I'+cls.folder.name, '-I'+str(ROOT/'tests/graphics'),
            *(str(ROOT/p) for p in EXA_COPY_SOURCES), str(XORG/'mi/micopy.c'),
            str(ROOT/'tests/graphics/ps2-copy-exa.c'),
            *['-Wl,--wrap='+n for n in ('open', 'close', 'ioctl', 'nanosleep', 'calloc')],
            '-latomic', '-lpixman-1','-lm', '-o', str(cls.binary)], check=True)
    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def run_case(self, case): subprocess.run([str(self.binary), case], check=True, timeout=30)
    def test_front_offscreen_odd_width_rgbx_rgb555_and_tiny_source(self): self.run_case('formats')
    def test_self_overlap_and_multi_rectangle_direction_order_match_snapshot(self): self.run_case('overlap')
    def test_bounds_destination_and_stale_storage_checks_precede_mesa_flush(self): self.run_case('preflight')
    def test_unsupported_alu_masks_formats_and_foreign_owners_do_not_draw(self): self.run_case('unsupported')
    def test_noop_empty_and_identity_copies_preserve_pending_shared_context(self): self.run_case('shared')
    def test_closed_context_and_reverse_cleanup_release_file_once(self): self.run_case('lifetime')
    def test_capacity_and_device_failures_never_replay_drawing(self): self.run_case('failure')
    def test_registration_failure_and_callback_cleanup_order_are_transactional(self): self.run_case('installation')

if __name__ == '__main__': unittest.main()
