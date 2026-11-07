"""Owned render batches, depth formats and provider rejection before DMA."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_ps2_gs_surfaces import ROOT, prepare_surface_headers
import sys
sys.path.insert(0, str(ROOT / 'scripts'))
from kernel_gs_render_port import adapt_provider

def headers(folder):
    folder = Path(folder)
    prepare_surface_headers(folder)
    commands = (ROOT / 'kernel/new-drivers/ps2fb-gs.h').read_text()
    pure = commands[commands.index('static bool ps2gs_zero_vertex('):commands.index('static union package *ps2gs_environment(')]
    path = folder / 'ps2-gs-vertex-packet.h'
    vertex = path.read_text().split('static union package *ps2gs_environment(')[0]
    path.write_text(pure + vertex)
    (folder / 'ps2fb-gs-device.h').write_text(adapt_provider((ROOT / 'kernel/new-drivers/ps2fb-gs-device.h').read_text()))

@unittest.skipUnless(shutil.which('cc'), 'native C compiler required')
class OwnedRender(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory(); cls.binary = Path(cls.folder.name) / 'render'
        headers(cls.folder.name)
        subprocess.run(['cc', '-std=gnu17', '-Wall', '-Wextra', '-Werror',
            '-Wno-unused-parameter', '-Wno-unused-function', '-pthread', '-fsanitize=address,undefined',
            '-fno-pie', '-no-pie', '-I' + cls.folder.name, '-I' + str(ROOT / 'kernel/new-drivers'),
            str(ROOT / 'tests/graphics/ps2-gs-render.c'), '-o', str(cls.binary)], check=True)
    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def run_case(self, case): subprocess.run([str(self.binary), case], check=True, timeout=20)
    def test_depth_storage_roles_padding_zeroing_and_destroy(self): self.run_case('depth')
    def test_batch_snapshot_validation_and_wrong_resource_rejection(self): self.run_case('batch')
    def test_real_gs_target_z_format_mask_and_gequal_packets(self): self.run_case('packets')
    def test_failed_render_not_replayed_and_stale_epoch(self): self.run_case('failure')

if __name__ == '__main__': unittest.main()
