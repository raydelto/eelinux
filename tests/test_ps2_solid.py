"""Real Xorg solid GC routing, independent pixel references and Mesa ordering."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_ps2_gc import ROOT, GC_SOURCES, headers as gc_headers
from test_ps2_glyph import glyph_flags
SOLID_SOURCES = GC_SOURCES + ['mesa/ps2/ps2_logic.c',
                            'mesa/ps2/ps2_solid_batch.c', 'desktop/xorg/ps2_solid.c']
CASES = ('fills', 'lines', 'preflight', 'owned', 'rewrap', 'lifetime', 'failure', 'random', 'installation', 'batch', 'fast-batch')

def headers(folder):
    gc_headers(folder)
    folder = Path(folder)
    text = (folder / 'previous-glyph.c').read_text().replace('int __wrap_ioctl(', 'int glyph_ioctl(')
    (folder / 'previous-glyph.c').write_text(text)
    text = (ROOT / 'tests/graphics/ps2-gc.c').read_text().replace('int main(', 'int previous_gc_main(')
    (folder / 'previous-gc.c').write_text(text)

@unittest.skipUnless(shutil.which('cc'), 'native compiler required')
class SolidGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory(); headers(cls.folder.name)
        cls.binary = Path(cls.folder.name) / 'solid'
        subprocess.run(['cc', '-std=gnu17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
            '-fsanitize=address,undefined', '-fno-pie', '-no-pie', '-DUSE_GCC_ATOMIC_BUILTINS',
            *glyph_flags(), '-I'+cls.folder.name, '-I'+str(ROOT/'tests/graphics'),
            *(str(ROOT/p) for p in SOLID_SOURCES), str(ROOT/'tests/graphics/ps2-solid.c'),
            *['-Wl,--wrap='+n for n in ('open','close','ioctl','nanosleep','calloc','realloc')],
            '-latomic', '-lpixman-1','-lm', '-o', str(cls.binary)], check=True)
    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def run_case(self, case): subprocess.run([str(self.binary), case], check=True, timeout=25)
    def test_solid_fills_spans_points_all_alus_masks_and_order(self): self.run_case('fills')
    def test_thin_axis_lines_caps_join_corners_xor_erase_and_degenerate_rectangles(self): self.run_case('lines')
    def test_complete_request_preflight_and_allocation_failure_precede_any_submission(self): self.run_case('preflight')
    def test_owned_rgb555_storage_multi_box_clips_and_mesa_targets_unchanged(self): self.run_case('owned')
    def test_gc_changes_lower_table_replacements_and_hook_detachment(self): self.run_case('rewrap')
    def test_destroyed_context_rejects_drawing_and_close_restores_every_callback_first(self): self.run_case('lifetime')
    def test_hardware_failure_is_sticky_and_no_drawing_replayed(self): self.run_case('failure')
    def test_randomized_protocol_pixels_match_independent_reference(self): self.run_case('random')
    def test_feature_absence_and_incomplete_registration_leave_callbacks_untouched(self): self.run_case('installation')
    def test_explicit_mesa_batch_validates_every_box_before_shared_queue_fence(self): self.run_case('batch')
    def test_batched_all_alus_masks_order_alpha_and_transport_reduction(self): self.run_case('fast-batch')
if __name__ == '__main__': unittest.main()
