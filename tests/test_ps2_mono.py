"""Actual opaque-stipple GC callbacks and native Mesa pattern construction."""
from pathlib import Path
import shutil, subprocess, tempfile, unittest
from test_ps2_stipple import ROOT, FILL_SOURCES, headers as stipple_headers
from test_ps2_copy_window import copy_flags, XORG
CASES=('borrowed-lifetime','borrowed-guards','allocation','budget','count','headers','access','pixels','source-edit','gc','failure','foreign','alpha-storage')
def headers(folder):
    stipple_headers(folder)
    text=(ROOT/'tests/graphics/ps2-stipple.c').read_text().replace('int main(', 'int previous_stipple_main(')
    (Path(folder)/'previous-stipple.c').write_text(text)
    p=Path(folder)/'previous-gc.c'
    text=p.read_text().replace('void *__wrap_calloc(size_t count, size_t size)',
        'static unsigned mono_calloc_countdown;\nvoid *__wrap_calloc(size_t count, size_t size)')
    text=text.replace('if (allocation_failure)',
        'if ((mono_calloc_countdown && --mono_calloc_countdown==0) || allocation_failure)')
    p.write_text(text)
@unittest.skipUnless(shutil.which('cc'), 'native compiler required')
class MonochromePixmap(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory();headers(cls.folder.name)
        cls.binary=Path(cls.folder.name)/'mono'
        subprocess.run(['cc','-std=gnu17','-Wall','-Wextra','-Werror','-Wno-unused-function',
            '-fsanitize=address,undefined','-fno-pie','-no-pie','-DUSE_GCC_ATOMIC_BUILTINS',
            *copy_flags(),'-I'+cls.folder.name,'-I'+str(ROOT/'tests/graphics'),
            *(str(ROOT/p) for p in FILL_SOURCES),str(XORG/'mi/micopy.c'),
            str(ROOT/'tests/graphics/ps2-mono.c'),
            *['-Wl,--wrap='+n for n in ('open','close','ioctl','nanosleep','calloc','malloc')],
            '-latomic','-lpixman-1','-lm','-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls):cls.folder.cleanup()
    def run_case(self,name):subprocess.run([str(self.binary),name],check=True,timeout=30)
    def test_readonly_scratch_header_reuse_across_glyph_depths(self):self.run_case('borrowed-lifetime')
    def test_readonly_scratch_bounds_and_cpu_destination_rejection(self):self.run_case('borrowed-guards')
    def test_zeroed_padded_source_storage_and_no_GS_allocation(self):self.run_case('allocation')
    def test_one_MiB_source_budget_and_release_reuse(self):self.run_case('budget')
    def test_bounded_pixmap_count_and_non_head_unlink(self):self.run_case('count')
    def test_immutable_headers_and_invalid_owned_views(self):self.run_case('headers')
    def test_monochrome_access_hides_pointer_and_color_access_is_fatal(self):self.run_case('access')
    def test_hidden_owned_source_drives_native_RGBX_and_RGB555_fills(self):self.run_case('pixels')
    def test_live_source_edits_change_native_pattern_without_color_upload(self):self.run_case('source-edit')
    def test_depth_one_lower_GC_callbacks_rewrap_and_destroy(self):self.run_case('gc')
    def test_allocation_failure_is_transactional_and_closed_GS_teardown_frees_bits(self):self.run_case('failure')
    def test_foreign_and_corrupt_bitmap_headers_reject_before_fence(self):self.run_case('foreign')
if __name__=='__main__':unittest.main()
