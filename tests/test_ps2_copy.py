"""Native CopyArea with actual upstream miDoCopy and independent pixel models."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_ps2_solid import SOLID_SOURCES, headers as solid_headers, glyph_flags, ROOT
XORG=Path('/home/machaddr/.cache/eelinux/desktop-xorg/output-desktop/build/xserver_xorg-server-21.1.24')
COPY_SOURCES=SOLID_SOURCES+['desktop/xorg/ps2_copy.c']
CASES=('widths-edge','widths','overlap','preflight','clipping','exposures','rewrap','lifetime','failure','random')

def headers(folder):
    solid_headers(folder)

def copy_flags():
    # mi.h includes xorg-server.h. Load the same feature settings before any
    # record headers in every translation unit to keep WindowRec's ABI equal.
    return glyph_flags()+['-include','xorg-server.h']

@unittest.skipUnless(shutil.which('cc'),'native compiler required')
class CopyArea(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory();headers(cls.folder.name)
        cls.binary=Path(cls.folder.name)/'copy'
        subprocess.run(['cc','-std=gnu17','-Wall','-Wextra','-Werror','-Wno-unused-function',
            '-fsanitize=address,undefined','-fno-pie','-no-pie','-DUSE_GCC_ATOMIC_BUILTINS',
            *copy_flags(),'-I'+cls.folder.name,'-I'+str(ROOT/'tests/graphics'),
            *(str(ROOT/p) for p in COPY_SOURCES),str(XORG/'mi/micopy.c'),
            str(ROOT/'tests/graphics/ps2-copy.c'),
            *['-Wl,--wrap='+n for n in ('open','close','ioctl','nanosleep','calloc')],
            '-latomic','-lpixman-1','-lm','-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls):cls.folder.cleanup()
    def run_case(self,name):subprocess.run([str(self.binary),name],check=True,timeout=30)
    def test_aligned_odd_edge_and_tiny_rgb555_copies_preserve_mesa_bindings(self):self.run_case('widths')
    def test_banded_and_tiled_overlap_all_directions_matches_snapshot_copy(self):self.run_case('overlap')
    def test_entire_region_and_capacity_failures_precede_shared_queue_flush(self):self.run_case('preflight')
    def test_upstream_source_and_destination_clipping_with_window_and_pixmap_origins(self):self.run_case('clipping')
    def test_upstream_source_validation_inferiors_and_exposure_return_values(self):self.run_case('exposures')
    def test_gc_table_replacement_retains_native_copy_and_other_routes(self):self.run_case('rewrap')
    def test_leases_and_close_restore_copy_before_lower_gc_and_file_teardown(self):self.run_case('lifetime')
    def test_sticky_copy_failure_never_replays_and_scratch_reclaimed_on_close(self):self.run_case('failure')
    def test_randomized_region_copies_match_independent_snapshot_reference(self):self.run_case('random')
if __name__=='__main__':unittest.main()
