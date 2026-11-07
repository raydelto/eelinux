"""Actual opaque-stipple GC callbacks and native Mesa pattern construction."""
from pathlib import Path
import shutil, subprocess, tempfile, unittest
from test_ps2_fill import ROOT, FILL_SOURCES, headers as fill_headers
from test_ps2_copy_window import copy_flags, XORG
CASES=('rectangles','spans','redirected','colors','large','tiny','noop','preflight',
       'allocation','seedfailure','expandfailure','laterfailure','bitorder','random')
def headers(folder):
    fill_headers(folder)
    text=(ROOT/'tests/graphics/ps2-fill.c').read_text()
    text=text.replace('int main(', 'int previous_fill_main(').replace('int __wrap_ioctl(', 'int fill_ioctl(')
    (Path(folder)/'previous-fill.c').write_text(text)
@unittest.skipUnless(shutil.which('cc'), 'native compiler required')
class OpaqueStipple(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory();headers(cls.folder.name)
        cls.binary=Path(cls.folder.name)/'stipple'
        subprocess.run(['cc','-std=gnu17','-Wall','-Wextra','-Werror','-Wno-unused-function',
            '-fsanitize=address,undefined','-fno-pie','-no-pie','-DUSE_GCC_ATOMIC_BUILTINS',
            *copy_flags(),'-I'+cls.folder.name,'-I'+str(ROOT/'tests/graphics'),
            *(str(ROOT/p) for p in FILL_SOURCES),str(XORG/'mi/micopy.c'),
            str(ROOT/'tests/graphics/ps2-stipple.c'),
            *['-Wl,--wrap='+n for n in ('open','close','ioctl','nanosleep','calloc','malloc')],
            '-latomic','-lpixman-1','-lm','-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls):cls.folder.cleanup()
    def run_case(self,name):subprocess.run([str(self.binary),name],check=True,timeout=30)
    def test_rectangles_clip_pattern_origin_and_source_preservation(self):self.run_case('rectangles')
    def test_pretranslated_spans_and_miTranslate_independent_origin(self):self.run_case('spans')
    def test_redirected_windows_and_rgb555_owned_pixmaps(self):self.run_case('redirected')
    def test_foreground_background_all_zero_one_and_equal_colors(self):self.run_case('colors')
    def test_maximum_256_by_256_pattern_and_partial_edge_periods(self):self.run_case('large')
    def test_full_screen_one_two_pixel_patterns_use_bounded_native_expansion(self):self.run_case('tiny')
    def test_empty_clipped_gxnoop_zero_mask_leave_pending_queue(self):self.run_case('noop')
    def test_invalid_source_state_and_later_boxes_before_output(self):self.run_case('preflight')
    def test_snapshot_heap_and_gs_reservation_fail_before_queue_flush(self):self.run_case('allocation')
    def test_failed_pattern_seed_leaves_destination_unchanged(self):self.run_case('seedfailure')
    def test_failed_expansion_leaves_destination_unchanged(self):self.run_case('expandfailure')
    def test_later_output_failure_never_replays_or_notifies_damage(self):self.run_case('laterfailure')
    def test_both_bit_orders_and_source_snapshot_before_fence(self):self.run_case('bitorder')
    def test_random_patterns_and_live_gc_revalidation(self):self.run_case('random')
if __name__=='__main__':unittest.main()
