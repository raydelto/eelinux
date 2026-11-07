"""Run GS raster/draw callbacks using real headers from the pinned Mesa source.

PS2_MESA_SOURCE can select the verified source tree. Generate upstream headers
with build/verification/build-ps2-raster-backends.py before running this test.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MESA = Path(os.environ.get('PS2_MESA_SOURCE',
    str(Path.home()/'.cache/eelinux/mesa-xlib/mesa-24.0.9/mesa-24.0.9')))
GENERATED = Path(os.environ.get('PS2_MESA_GENERATED',
    str(ROOT/'build/desktop/ps2-raster-backends/mesa-generated')))


@unittest.skipUnless(shutil.which('cc') and (MESA/'src/gallium/include/pipe/p_state.h').is_file()
                    and (GENERATED/'nir_opcodes.h').is_file()
                    and (GENERATED/'builtin_types.h').is_file(),
                    'native compiler, pinned Mesa source and upstream generated headers required')
class GalliumRaster(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory()
        cls.binary=Path(cls.folder.name)/'raster'
        flags=['-isystem'+str(MESA/path) for path in (
            'src','include','src/gallium/include','src/gallium/auxiliary','src/compiler/nir')]
        subprocess.run(['cc','-std=gnu17','-Wall','-Wextra','-Werror',
            '-DHAVE_ENDIAN_H','-DHAVE_STRUCT_TIMESPEC','-DHAVE_PTHREAD',
            '-fsanitize=address,undefined','-fno-pie','-no-pie', *flags,
            '-isystem'+str(GENERATED), '-I'+str(ROOT/'graphics'),
            '-I'+str(ROOT/'kernel/new-drivers'), '-I'+str(ROOT/'mesa/ps2'),
            str(ROOT/'graphics/ps2_gs_client.c'), str(ROOT/'mesa/ps2/ps2_raster.c'),
            str(ROOT/'mesa/ps2/ps2_draw.c'), str(ROOT/'tests/graphics/ps2-gallium.c'),
            '-Wl,--wrap=ioctl','-o',str(cls.binary)],check=True)

    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()

    def run_case(self, case): subprocess.run([str(self.binary),case],check=True,timeout=10)
    def test_scissor_exclusive_bounds_color_and_failed_session(self): self.run_case('clear')
    def test_fixed_coordinates_depth_order_colors_and_invalid_input(self): self.run_case('triangle')
    def test_draw_stage_attributes_fences_and_failures(self): self.run_case('stage')
    def test_native_format_upload_box_and_no_replay(self): self.run_case('upload')


if __name__=='__main__': unittest.main()
