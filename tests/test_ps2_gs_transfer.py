"""Actual provider validation and fixed-function GS texture transfer packets."""
from pathlib import Path
import shutil, subprocess, tempfile, unittest
from test_ps2_gs_logic import ROOT, headers as logic_headers
from kernel_gs_transfer_port import adapt_provider
CASES=('packets','validation','failure')

def headers(folder):
    logic_headers(folder)
    p=Path(folder)/'ps2fb-gs-device.h'
    p.write_text(adapt_provider(p.read_text()))

@unittest.skipUnless(shutil.which('cc'), 'native compiler required')
class TextureTransfer(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory(); headers(cls.folder.name)
        cls.binary=Path(cls.folder.name)/'transfer'
        subprocess.run(['cc','-std=gnu17','-Wall','-Wextra','-Werror','-Wno-unused-function',
            '-Wno-unused-parameter','-pthread','-fsanitize=address,undefined','-fno-pie','-no-pie',
            '-I'+cls.folder.name,'-I'+str(ROOT/'kernel/new-drivers'),'-I'+str(ROOT/'tests/graphics'),
            str(ROOT/'tests/graphics/ps2-gs-transfer.c'),'-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def test_actual_frame_masks_blend_wrap_modulation_and_state_reset(self): self.run_case('packets')
    def test_whole_batch_preflight_owned_handles_and_zero_mask(self): self.run_case('validation')
    def test_sticky_timeout_and_no_replay(self): self.run_case('failure')
    def run_case(self,case): subprocess.run([str(self.binary),case],check=True,timeout=30)
if __name__=='__main__': unittest.main()
