"""Owned Xorg/Mesa storage callbacks using real upstream headers, wrapped ioctls."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_ps2_exa import SYSROOT
from test_ps2_gallium import MESA,GENERATED

ROOT=Path(__file__).resolve().parents[1]

def backend_flags():
    # These integration fixtures consume generated upstream headers, which are
    # intentionally absent from a clean source checkout. Do not turn a missing
    # prerequisite into dozens of compiler failures, or mask a real build error.
    required = (SYSROOT/'usr/include/xorg/exa.h',
                MESA/'src/gallium/include/pipe/p_state.h',
                GENERATED/'nir_opcodes.h', GENERATED/'builtin_types.h')
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise unittest.SkipTest('Optional Xorg/Mesa integration prerequisites missing: '
                                + ', '.join(missing)
                                + '; prepare the upstream SDK/generated headers first')
    return ['-DHAVE_ENDIAN_H','-DHAVE_STRUCT_TIMESPEC','-DHAVE_PTHREAD',
        '-I'+str(SYSROOT/'usr/include/xorg'),'-I'+str(SYSROOT/'usr/include/pixman-1'),
        *['-isystem'+str(MESA/path) for path in ('src','include','src/gallium/include','src/gallium/auxiliary')],
        '-isystem'+str(GENERATED),'-I'+str(ROOT/'graphics'),'-I'+str(ROOT/'kernel/new-drivers'),
        '-I'+str(ROOT/'desktop/xorg'),'-I'+str(ROOT/'mesa/ps2')]

SOURCES=['graphics/ps2_gs_client.c','graphics/ps2_gs_surface.c',
         'desktop/xorg/ps2_surface_exa.c','mesa/ps2/ps2_resource.c']
FIXTURE_SOURCES=list(dict.fromkeys(SOURCES+['graphics/'+name+'.c' for name in
    ('ps2_gs_render','ps2_gs_texture','ps2_gs_bitmap','ps2_gs_logic','ps2_gs_transfer','ps2_gs_lut')]))
@unittest.skipUnless(shutil.which('cc') and (SYSROOT/'usr/include/xorg/exa.h').is_file()
                    and (MESA/'src/gallium/include/pipe/p_state.h').is_file(),
                    'native compiler and real Xorg/Mesa headers required')
class SurfaceBackends(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory(); cls.binary=Path(cls.folder.name)/'backends'
        subprocess.run(['cc','-std=gnu17','-Wall','-Wextra','-Werror',
            '-fsanitize=address,undefined','-fno-pie','-no-pie',*backend_flags(),
            *(str(ROOT/p) for p in FIXTURE_SOURCES),str(ROOT/'tests/graphics/ps2-surface-backends.c'),
            '-Wl,--wrap=open','-Wl,--wrap=close','-Wl,--wrap=ioctl','-Wl,--wrap=nanosleep',
            '-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def run_case(self,case): subprocess.run([str(self.binary),case],check=True,timeout=15)
    def test_capacity_recovery_preserves_io_and_protocol_failure_guards(self): self.run_case('allocation-recovery')
    def test_exa_rejects_exhaustion_and_continues_native_drawing(self): self.run_case('exa-allocation-recovery')
    def test_surface_metadata_bounds_handles_and_retry(self): self.run_case('storage')
    def test_native_wide_odd_padded_uploads_and_overflow(self): self.run_case('uploads')
    def test_corrupt_response_stale_session_failure_and_no_replay(self): self.run_case('errors')
    def test_exa_offscreen_allocation_drawing_copy_upload_headers_and_no_cpu(self): self.run_case('exa')
    def test_real_gallium_resources_formats_levels_upload_copy_and_cleanup(self): self.run_case('mesa')

if __name__=='__main__': unittest.main()
