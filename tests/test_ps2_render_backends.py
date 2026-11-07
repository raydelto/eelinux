"""Real Mesa draw/resource and EXA ordering callbacks on the owned interface."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_ps2_surface_backends import ROOT, backend_flags, SOURCES, MESA

RENDER_SOURCES = SOURCES + ['graphics/ps2_gs_render.c', 'desktop/xorg/ps2_render_bridge.c',
    'mesa/ps2/ps2_render_target.c', 'mesa/ps2/ps2_owned_draw.c']

def render_flags():
    return backend_flags() + ['-isystem' + str(MESA / 'src/compiler/nir'),
        '-isystem' + str(MESA / 'src/compiler')]

@unittest.skipUnless(shutil.which('cc'), 'native C compiler required')
class RenderBackends(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory(); cls.binary = Path(cls.folder.name) / 'backends'
        subprocess.run(['cc', '-std=gnu17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
            '-fsanitize=address,undefined', '-fno-pie', '-no-pie', *render_flags(),
            *(str(ROOT / p) for p in RENDER_SOURCES), str(ROOT / 'tests/graphics/ps2-render-backends.c'),
            '-Wl,--wrap=open', '-Wl,--wrap=close', '-Wl,--wrap=ioctl', '-Wl,--wrap=nanosleep',
            '-o', str(cls.binary)], check=True)
    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def run_case(self, case): subprocess.run([str(self.binary), case], check=True, timeout=15)
    def test_depth_handles_target_switch_order_batch_bounds_and_no_replay(self): self.run_case('transport')
    def test_mesa_resources_framebuffer_references_depth_and_draw_stage(self): self.run_case('mesa-render')
    def test_xorg_flushes_shared_mesa_queue_before_2d_and_cleanup(self): self.run_case('xorg-order')

if __name__ == '__main__': unittest.main()
