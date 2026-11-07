"""Actual owned texture validation, packets and device paths under sanitizers."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_ps2_gs_render import ROOT, headers as render_headers
import sys
sys.path.insert(0, str(ROOT/'scripts'))
from kernel_gs_texture_port import adapt_provider

def headers(folder):
    folder = Path(folder); render_headers(folder)
    p = folder/'ps2fb-gs-device.h'; p.write_text(adapt_provider(p.read_text()))
    text = (ROOT/'tests/graphics/ps2-gs-render.c').read_text()
    (folder/'previous-render-kernel.c').write_text(text.replace('int main(', 'int previous_render_kernel_main('))

@unittest.skipUnless(shutil.which('cc'), 'native compiler required')
class TextureKernel(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory(); cls.binary=Path(cls.folder.name)/'texture'
        headers(cls.folder.name)
        subprocess.run(['cc','-std=gnu17','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter',
            '-pthread','-fsanitize=address,undefined','-fno-pie','-no-pie','-I'+cls.folder.name,
            '-I'+str(ROOT/'kernel/new-drivers'),'-I'+str(ROOT/'tests/graphics'),
            str(ROOT/'tests/graphics/ps2-gs-texture.c'),'-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def run_case(self,c): subprocess.run([str(self.binary),c],check=True,timeout=20)
    def test_texture_roles_whole_batch_rejection_and_snapshot(self): self.run_case('batch')
    def test_actual_texflush_tex0_stq_clamp_and_depth_packets(self): self.run_case('packets')
    def test_nonfinite_q_bounds_alias_and_unsupported_states(self): self.run_case('validation')
    def test_hardware_error_and_stale_session_no_replay(self): self.run_case('failure')
if __name__=='__main__': unittest.main()
