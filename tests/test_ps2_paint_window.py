"""Native Xorg window backgrounds/borders and shared Mesa solid ordering."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_ps2_copy_window import ROOT, WINDOW_SOURCES, copy_flags, XORG, headers as window_headers

PAINT_SOURCES = list(dict.fromkeys(WINDOW_SOURCES + ['mesa/ps2/ps2_tile.c', 'desktop/xorg/ps2_paint_window.c']))
CASES = ('background', 'border', 'inherited', 'redirected', 'noop', 'preflight',
         'ownership', 'lifetime', 'failure', 'installation', 'random', 'complex')

def headers(folder):
    window_headers(folder)
    folder = Path(folder)
    p = folder/'previous-copy.c'
    p.write_text(p.read_text().replace('int __wrap_ioctl(', 'int copy_ioctl('))
    p = folder/'previous-glyph.c'
    p.write_text(p.read_text().replace('void DamageRegionAppend(', 'void previous_damage_append(')
        .replace('void DamageRegionProcessPending(', 'void previous_damage_process('))

@unittest.skipUnless(shutil.which('cc'), 'native compiler required')
class PaintWindow(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory(); headers(cls.folder.name)
        cls.binary = Path(cls.folder.name)/'paint-window'
        subprocess.run(['cc', '-std=gnu17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
            '-fsanitize=address,undefined', '-fno-pie', '-no-pie', '-DUSE_GCC_ATOMIC_BUILTINS',
            *copy_flags(), '-I'+cls.folder.name, '-I'+str(ROOT/'tests/graphics'),
            *(str(ROOT/p) for p in PAINT_SOURCES), str(XORG/'mi/micopy.c'),
            str(ROOT/'tests/graphics/ps2-paint-window.c'),
            *['-Wl,--wrap='+n for n in ('open','close','ioctl','nanosleep','calloc')],
            '-latomic', '-lpixman-1','-lm', '-o', str(cls.binary)], check=True)
    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def run_case(self, name): subprocess.run([str(self.binary), name], check=True, timeout=30)
    def test_child_background_clipping_matches_independent_pixels(self): self.run_case('background')
    def test_border_uses_pixmap_bounds_and_local_damage_region(self): self.run_case('border')
    def test_nested_parent_relative_none_and_inhibition(self): self.run_case('inherited')
    def test_redirected_rgbx_rgb555_and_preserved_mesa_bindings(self): self.run_case('redirected')
    def test_empty_and_inhibited_paint_preserves_pending_queue(self): self.run_case('noop')
    def test_region_capacity_allocation_and_unsupported_state_preflight(self): self.run_case('preflight')
    def test_missing_foreign_stale_and_incompatible_storage(self): self.run_case('ownership')
    def test_close_unwinds_paint_copy_and_gc_before_context_release(self): self.run_case('lifetime')
    def test_partial_device_failure_never_replays_or_reports_success(self): self.run_case('failure')
    def test_installation_cleanup_order_and_reinstallation(self): self.run_case('installation')
    def test_random_region_pixels_and_damage_match_independent_reference(self): self.run_case('random')
    def test_complex_shaped_regions_cross_GS_batch_boundaries(self): self.run_case('complex')
if __name__ == '__main__': unittest.main()
