"""Native zero-width dash geometry/colour plans checked against pinned fbSegment."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from test_ps2_lines import ROOT,SOLID_SOURCES,copy_flags,XORG,headers as line_headers
CASES=('onoff','double','offsets','clipping','joins','rectangles','caps','owned','fillstyle','noop','preflight','budget','allocation','snapshot','failure','direct')
def headers(folder):
    line_headers(folder)
    text=(ROOT/'tests/graphics/ps2-lines.c').read_text().replace('int main(', 'int previous_lines_main(')
    text=text.replace('int __wrap_ioctl(', 'int lines_ioctl(')
    (Path(folder)/'previous-lines.c').write_text(text)
@unittest.skipUnless(shutil.which('cc'),'native compiler required')
class ThinDash(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory();headers(cls.folder.name);cls.binary=Path(cls.folder.name)/'dash'
        reference=Path(cls.folder.name)/'fbseg.o'
        subprocess.run(['cc','-std=gnu17','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
            '-fsanitize=address,undefined','-fno-pie','-DUSE_GCC_ATOMIC_BUILTINS',*copy_flags(),
            '-c',str(XORG/'fb/fbseg.c'),'-o',str(reference)],check=True)
        subprocess.run(['cc','-std=gnu17','-Wall','-Wextra','-Werror','-Wno-unused-function',
            '-fsanitize=address,undefined','-fno-pie','-no-pie','-DUSE_GCC_ATOMIC_BUILTINS',
            *copy_flags(),'-I'+cls.folder.name,'-I'+str(ROOT/'tests/graphics'),
            *(str(ROOT/p) for p in SOLID_SOURCES),str(XORG/'mi/mizerline.c'),str(XORG/'mi/mizerclip.c'),
            str(reference),str(ROOT/'tests/graphics/ps2-dash.c'),
            *['-Wl,--wrap='+n for n in ('open','close','ioctl','nanosleep','calloc','realloc')],
            '-latomic','-lpixman-1','-lm','-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls):cls.folder.cleanup()
    def run_case(self,n):subprocess.run([str(self.binary),n],check=True,timeout=60)
    def test_onoff_dashes_all_octants_preserve_gaps(self):self.run_case('onoff')
    def test_double_dash_all_ALUs_masks_and_overlapping_segments(self):self.run_case('double')
    def test_offsets_different_lengths_and_normalized_odd_client_lists(self):self.run_case('offsets')
    def test_multibox_clip_does_not_restart_dash_phase(self):self.run_case('clipping')
    def test_previous_mode_join_phase_and_closed_XOR_erasure(self):self.run_case('joins')
    def test_each_rectangle_restarts_but_corners_continue_phase(self):self.run_case('rectangles')
    def test_caps_degenerates_and_endpoint_dash_colour(self):self.run_case('caps')
    def test_redirected_RGB555_native_foreground_and_background(self):self.run_case('owned')
    def test_patterned_dash_fill_rejects_before_any_native_submission(self):self.run_case('fillstyle')
    def test_noop_empty_clip_and_odd_only_gaps_preserve_queue_Damage(self):self.run_case('noop')
    def test_invalid_later_coloured_run_dash_state_and_unsupported_features_preflight(self):self.run_case('preflight')
    def test_coloured_run_budget_rejects_before_original_fence(self):self.run_case('budget')
    def test_geometry_allocation_failure_preserves_pending_work(self):self.run_case('allocation')
    def test_dash_source_is_snapshotted_before_original_fence(self):self.run_case('snapshot')
    def test_later_background_failure_is_sticky_without_replay_Damage(self):self.run_case('failure')
    def test_direct_Mesa_odd_lists_extreme_offsets_endpoints_and_callback_failure(self):self.run_case('direct')
if __name__=='__main__':unittest.main()
