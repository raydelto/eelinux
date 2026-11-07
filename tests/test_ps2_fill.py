"""Actual native GC tile fills, independent pixels and lifecycle ordering."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_ps2_paint_window import ROOT, PAINT_SOURCES, copy_flags, XORG
from test_ps2_tiled_paint import headers as tile_headers

FILL_SOURCES = PAINT_SOURCES + ['desktop/xorg/ps2_fill.c']
CASES = ('rectangles','spans','redirected','pixel','solid','noop','preflight','capacity','complex',
         'seedfailure','expandfailure','failure','rewrap','lifetime','installation','random')
def headers(folder):
    tile_headers(folder)
    text=(ROOT/'tests/graphics/ps2-tiled-paint.c').read_text()
    text=text.replace('int main(', 'int previous_tiled_main(').replace('int __wrap_ioctl(', 'int tile_ioctl(')
    (Path(folder)/'previous-tiled.c').write_text(text)

@unittest.skipUnless(shutil.which('cc'), 'native compiler required')
class NativeFill(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory(); headers(cls.folder.name)
        cls.binary=Path(cls.folder.name)/'fill'
        subprocess.run(['cc','-std=gnu17','-Wall','-Wextra','-Werror','-Wno-unused-function',
            '-fsanitize=address,undefined','-fno-pie','-no-pie','-DUSE_GCC_ATOMIC_BUILTINS',
            *copy_flags(),'-I'+cls.folder.name,'-I'+str(ROOT/'tests/graphics'),
            *(str(ROOT/p) for p in FILL_SOURCES),str(XORG/'mi/micopy.c'),
            str(ROOT/'tests/graphics/ps2-fill.c'),
            *['-Wl,--wrap='+n for n in ('open','close','ioctl','nanosleep','calloc')],
            '-latomic','-lpixman-1','-lm','-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def run_case(self,name): subprocess.run([str(self.binary),name],check=True,timeout=120 if name=='complex' else 30)
    def test_tiled_rectangle_clipping_pattern_origins_and_source_preservation(self): self.run_case('rectangles')
    def test_pretranslated_spans_and_miTranslate_independent_pattern_origin(self): self.run_case('spans')
    def test_redirected_windows_and_owned_rgbx_rgb555_pixmaps(self): self.run_case('redirected')
    def test_tileIsPixel_uses_tile_color_for_all_alus_and_partial_masks(self): self.run_case('pixel')
    def test_solid_fills_and_other_solid_ops_keep_native_dispatch(self): self.run_case('solid')
    def test_empty_clipped_gxnoop_and_zero_masks_preserve_pending_queue(self): self.run_case('noop')
    def test_invalid_later_input_and_tile_metadata_precede_flush(self): self.run_case('preflight')
    def test_box_pixel_batching_and_recoverable_capacity_preserve_pending_work(self): self.run_case('capacity')
    def test_complex_clips_and_overlapping_batches_preserve_all_alus_masks_and_preflight(self): self.run_case('complex')
    def test_failed_texture_seed_leaves_destination_unchanged(self): self.run_case('seedfailure')
    def test_failed_scratch_expansion_leaves_destination_unchanged(self): self.run_case('expandfailure')
    def test_later_packet_failure_never_replays_or_reports_success_damage(self): self.run_case('failure')
    def test_revalidation_native_copy_and_fill_override_cleanup_order(self): self.run_case('rewrap')
    def test_closed_context_rejection_and_reverse_close_lease_cleanup(self): self.run_case('lifetime')
    def test_transactional_registration_and_incomplete_fill_table_rejection(self): self.run_case('installation')
    def test_random_npot_tiles_rectangles_and_spans_match_modulo_reference(self): self.run_case('random')
if __name__=='__main__': unittest.main()
