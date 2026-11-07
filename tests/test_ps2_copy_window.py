"""Native screen CopyWindow, real pixman clipping and independent GS pixels."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_ps2_blit import ROOT, BLIT_SOURCES, headers, copy_flags as base_copy_flags, XORG

def copy_flags():
    return base_copy_flags() + ['-DCOMPOSITE']

WINDOW_SOURCES = BLIT_SOURCES + ['desktop/xorg/ps2_copy_window.c']
CASES = ('moves', 'clipping', 'redirected', 'noop', 'preflight', 'capacity',
         'ownership', 'wrappers', 'lifetime', 'failure', 'installation')

@unittest.skipUnless(shutil.which('cc'), 'native compiler required')
class CopyWindow(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory(); headers(cls.folder.name)
        cls.binary = Path(cls.folder.name)/'copy-window'
        subprocess.run(['cc', '-std=gnu17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
            '-fsanitize=address,undefined', '-fno-pie', '-no-pie', '-DUSE_GCC_ATOMIC_BUILTINS',
            *copy_flags(), '-I'+cls.folder.name, '-I'+str(ROOT/'tests/graphics'),
            *(str(ROOT/p) for p in WINDOW_SOURCES), str(XORG/'mi/micopy.c'),
            str(ROOT/'tests/graphics/ps2-copy-window.c'),
            *['-Wl,--wrap='+n for n in ('open','close','ioctl','nanosleep','calloc')],
            '-latomic', '-lpixman-1','-lm', '-o', str(cls.binary)], check=True)
    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def run_case(self, name): subprocess.run([str(self.binary), name], check=True, timeout=30)
    def test_front_window_moves_all_eight_overlap_directions(self): self.run_case('moves')
    def test_multiband_source_border_clip_and_region_translation(self): self.run_case('clipping')
    def test_redirected_pixmap_origins_odd_width_rgbx_rgb555(self): self.run_case('redirected')
    def test_empty_disjoint_and_identity_preserve_pending_queue(self): self.run_case('noop')
    def test_all_geometry_and_translation_checks_precede_queue_flush(self): self.run_case('preflight')
    def test_region_limit_and_scratch_capacity_leave_request_unmodified(self): self.run_case('capacity')
    def test_missing_foreign_stale_and_incompatible_storage_do_not_draw(self): self.run_case('ownership')
    def test_damage_unwrap_rewrap_and_reverse_close_restore_native_hook(self): self.run_case('wrappers')
    def test_closed_context_and_screen_lease_release_file_once(self): self.run_case('lifetime')
    def test_sticky_device_failure_never_replays_request(self): self.run_case('failure')
    def test_installation_and_callback_cleanup_failures_are_transactional(self): self.run_case('installation')
if __name__ == '__main__': unittest.main()
