"""Native thin lines checked against the pinned Xorg miZeroLine/miZeroClipLine."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from test_ps2_solid import ROOT,SOLID_SOURCES,headers as solid_headers
from test_ps2_copy_window import copy_flags,XORG
CASES=('octants','ties','clipping','caps','joins','fillstyle','owned','random','extreme','budget','allocation','failure')
def headers(folder):
    solid_headers(folder)
    text=(ROOT/'tests/graphics/ps2-solid.c').read_text().replace('int main(', 'int previous_solid_main(')
    text=text.replace('int __wrap_ioctl(', 'int solid_ioctl(')
    (Path(folder)/'previous-solid.c').write_text(text)
@unittest.skipUnless(shutil.which('cc'),'native compiler required')
class ThinLines(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory();headers(cls.folder.name)
        cls.binary=Path(cls.folder.name)/'lines'
        subprocess.run(['cc','-std=gnu17','-Wall','-Wextra','-Werror','-Wno-unused-function',
            '-fsanitize=address,undefined','-fno-pie','-no-pie','-DUSE_GCC_ATOMIC_BUILTINS',
            *copy_flags(),'-I'+cls.folder.name,'-I'+str(ROOT/'tests/graphics'),
            *(str(ROOT/p) for p in SOLID_SOURCES),str(XORG/'mi/mizerline.c'),str(XORG/'mi/mizerclip.c'),
            str(ROOT/'tests/graphics/ps2-lines.c'),
            *['-Wl,--wrap='+n for n in ('open','close','ioctl','nanosleep','calloc','realloc')],
            '-latomic','-lpixman-1','-lm','-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls):cls.folder.cleanup()
    def run_case(self,n):subprocess.run([str(self.binary),n],check=True,timeout=60)
    def test_all_eight_octants_and_reversed_endpoints(self):self.run_case('octants')
    def test_all_256_screen_biases_exact_tie_pixels(self):self.run_case('ties')
    def test_drawable_and_multibox_clip_preserve_original_line_phase(self):self.run_case('clipping')
    def test_cap_styles_zero_length_and_clipped_endpoints(self):self.run_case('caps')
    def test_XOR_open_closed_previous_mode_joins_and_erase(self):self.run_case('joins')
    def test_zero_width_line_foreground_independent_of_fillStyle(self):self.run_case('fillstyle')
    def test_RGB555_redirected_storage_all_ALUs_and_partial_masks(self):self.run_case('owned')
    def test_random_requests_match_actual_upstream_raster_and_independent_ALU(self):self.run_case('random')
    def test_full_int32_Mesa_endpoints_and_large_CoordModePrevious(self):self.run_case('extreme')
    def test_geometry_budget_failure_precedes_original_queue_fence(self):self.run_case('budget')
    def test_realloc_failure_leaves_queue_and_Damage_untouched(self):self.run_case('allocation')
    def test_later_native_failure_is_sticky_without_replay_or_Damage(self):self.run_case('failure')
if __name__=='__main__':unittest.main()
