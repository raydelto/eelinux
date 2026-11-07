"""Test real GS page allocation and packet code, not an invented GPU model."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
SOURCE=Path(os.environ.get('PS2_SURFACE_KERNEL_SOURCE',
    str(Path.home()/'.cache/eelinux/sources/linux-7.2.8-ps2-gs-surfaces')))

def prepare_surface_headers(folder):
    folder=Path(folder); (folder/'asm').mkdir(parents=True,exist_ok=True)
    for name in ('gif.h','gs.h','bitfield.h'):
        shutil.copyfile(SOURCE/'arch/mips/include/uapi/asm'/name,folder/'asm'/name)
    image=(SOURCE/'drivers/video/fbdev/ps2fb-gs-image.h').read_text()
    pure,sep,_=image.partition('static int ps2gs_upload_execute(')
    assert sep
    (folder/'ps2-gs-image-packet.h').write_text(pure)
    commands=(ROOT/'kernel/new-drivers/ps2fb-gs.h').read_text()
    start=commands.index('static union package *ps2gs_vertex(')
    end=commands.index('static int ps2gs_execute(',start)
    (folder/'ps2-gs-vertex-packet.h').write_text(commands[start:end])

@unittest.skipUnless(shutil.which('cc') and (SOURCE/'arch/mips/include/uapi/asm/gif.h').is_file(),
                    'native compiler and built surface candidate required')
class GSSurfaces(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory(); cls.binary=Path(cls.folder.name)/'surfaces'
        prepare_surface_headers(cls.folder.name)
        subprocess.run(['cc','-std=gnu17','-Wall','-Wextra','-Werror',
            '-fsanitize=address,undefined','-fno-pie','-no-pie',
            '-I'+cls.folder.name,'-I'+str(ROOT/'kernel/new-drivers'),
            str(ROOT/'tests/graphics/ps2-gs-surfaces.c'),'-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def run_case(self,case): subprocess.run([str(self.binary),case],check=True,timeout=15)
    def test_reservations_limits_odd_sizes_and_nonreused_handles(self): self.run_case('allocation')
    def test_fragmentation_table_limit_and_disjoint_random_allocations(self): self.run_case('fragmentation')
    def test_exact_surface_base_pitch_context_copy_order_and_upload_encodings(self): self.run_case('packets')
    def test_handle_capacity_independent_of_vram_and_nonreused_recycled_slots(self): self.run_case('capacity')

if __name__=='__main__': unittest.main()
