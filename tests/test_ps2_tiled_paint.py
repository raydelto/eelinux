"""Native repeated tile painting, bounded GS expansion and Xorg alignment."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_ps2_paint_window import ROOT, PAINT_SOURCES, copy_flags, XORG, headers as paint_headers

CASES=('background','border','redirected','tiny','large','preflight','capacity','noop',
       'seedfailure','expandfailure','failure','lifetime','random','complex')
def headers(folder):
    paint_headers(folder)
    text=(ROOT/'tests/graphics/ps2-paint-window.c').read_text()
    text=text.replace('int main(', 'int previous_paint_main(').replace('int __wrap_ioctl(', 'int paint_ioctl(')
    (Path(folder)/'previous-paint.c').write_text(text)

@unittest.skipUnless(shutil.which('cc'), 'native compiler required')
class TiledPaint(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory(); headers(cls.folder.name)
        cls.binary=Path(cls.folder.name)/'tiled-paint'
        subprocess.run(['cc','-std=gnu17','-Wall','-Wextra','-Werror','-Wno-unused-function',
            '-fsanitize=address,undefined','-fno-pie','-no-pie','-DUSE_GCC_ATOMIC_BUILTINS',
            *copy_flags(),'-I'+cls.folder.name,'-I'+str(ROOT/'tests/graphics'),
            *(str(ROOT/p) for p in PAINT_SOURCES),str(XORG/'mi/micopy.c'),
            str(ROOT/'tests/graphics/ps2-tiled-paint.c'),
            *['-Wl,--wrap='+n for n in ('open','close','ioctl','nanosleep','calloc')],
            '-latomic','-lpixman-1','-lm','-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def run_case(self,name): subprocess.run([str(self.binary),name],check=True,timeout=30)
    def test_npot_tiled_child_clipping_and_source_preservation(self): self.run_case('background')
    def test_inherited_background_and_distinct_border_image_align_to_ancestor(self): self.run_case('border')
    def test_redirected_rgbx_rgb555_tile_and_damage_origins(self): self.run_case('redirected')
    def test_one_and_two_pixel_patterns_expand_natively_with_bounded_work(self): self.run_case('tiny')
    def test_direct_texture_batches_for_large_and_unaligned_npot_tiles(self): self.run_case('large')
    def test_all_geometry_ownership_format_feature_checks_precede_flush(self): self.run_case('preflight')
    def test_pixel_piece_and_box_limits_and_scratch_reservation(self): self.run_case('capacity')
    def test_empty_clipped_inherited_none_and_inhibited_queues(self): self.run_case('noop')
    def test_seed_texture_failure_leaves_destination_untouched(self): self.run_case('seedfailure')
    def test_scratch_doubling_failure_leaves_destination_untouched(self): self.run_case('expandfailure')
    def test_expansion_and_later_texture_packet_failure_never_replays(self): self.run_case('failure')
    def test_closed_context_rejection_and_reverse_screen_cleanup(self): self.run_case('lifetime')
    def test_random_npot_periods_and_signed_origins_match_independent_modulo_pixels(self): self.run_case('random')
    def test_complex_tiled_regions_cross_GS_batches_without_changing_pixels(self): self.run_case('complex')
if __name__=='__main__': unittest.main()
