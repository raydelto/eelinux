"""Actual opaque-stipple GC callbacks and native Mesa pattern construction."""
from pathlib import Path
import shutil, subprocess, tempfile, unittest
from test_ps2_mono import ROOT, FILL_SOURCES, headers as mono_headers
from test_ps2_copy_window import copy_flags, XORG
CASES=('rectangles','spans','redirected','alu','owned','noop','empty','large','preflight','budget',
       'allocation','snapshot','firstfailure','laterfailure','random','bitorders')
def headers(folder):
    mono_headers(folder)
    p=Path(folder)/'previous-stipple.c'
    p.write_text(p.read_text().replace('int __wrap_ioctl(', 'int stippler_ioctl('))
    text=(ROOT/'tests/graphics/ps2-mono.c').read_text().replace('int main(', 'int previous_mono_main(')
    (Path(folder)/'previous-mono.c').write_text(text)
@unittest.skipUnless(shutil.which('cc'), 'native compiler required')
class TransparentStipple(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory();headers(cls.folder.name)
        cls.binary=Path(cls.folder.name)/'mono'
        subprocess.run(['cc','-std=gnu17','-Wall','-Wextra','-Werror','-Wno-unused-function',
            '-fsanitize=address,undefined','-fno-pie','-no-pie','-DUSE_GCC_ATOMIC_BUILTINS',
            *copy_flags(),'-I'+cls.folder.name,'-I'+str(ROOT/'tests/graphics'),
            *(str(ROOT/p) for p in FILL_SOURCES),str(XORG/'mi/micopy.c'),
            str(ROOT/'tests/graphics/ps2-transparent.c'),
            *['-Wl,--wrap='+n for n in ('open','close','ioctl','nanosleep','calloc','malloc')],
            '-latomic','-lpixman-1','-lm','-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls):cls.folder.cleanup()
    def run_case(self,name):subprocess.run([str(self.binary),name],check=True,timeout=60)
    def test_clipped_rectangles_preserve_zero_bits_and_source(self):self.run_case('rectangles')
    def test_pretranslated_spans_and_miTranslate(self):self.run_case('spans')
    def test_RGB555_backing_pixmap_and_drawable_origins(self):self.run_case('redirected')
    def test_all_ALUs_partial_masks_and_overlapping_requests(self):self.run_case('alu')
    def test_owned_source_pointer_remains_hidden(self):self.run_case('owned')
    def test_GXnoop_zero_masks_empty_clip_preserve_pending_queue(self):self.run_case('noop')
    def test_all_zero_pattern_preserves_pending_queue_and_no_Damage(self):self.run_case('empty')
    def test_full_screen_tiny_checker_and_identical_row_coalescing(self):self.run_case('large')
    def test_invalid_source_state_and_later_geometry_before_fence(self):self.run_case('preflight')
    def test_pixel_run_and_boolean_work_limits_before_fence(self):self.run_case('budget')
    def test_snapshot_allocation_failure_before_fence(self):self.run_case('allocation')
    def test_source_snapshot_survives_mutation_at_original_fence(self):self.run_case('snapshot')
    def test_first_output_failure_never_replays_or_reports_Damage(self):self.run_case('firstfailure')
    def test_later_packet_failure_retains_prior_effects_without_replay(self):self.run_case('laterfailure')
    def test_random_periods_origins_and_native_ALUs(self):self.run_case('random')
    def test_direct_Mesa_source_bit_orders_and_extreme_origins(self):self.run_case('bitorders')
if __name__=='__main__':unittest.main()
