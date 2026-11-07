"""Actual GS batch provider: packet bounds, operation order and DMA counts."""
from pathlib import Path
import shutil, subprocess, tempfile, unittest
from test_ps2_gs_readback import ROOT, headers as readback_headers
import kernel_gs_batch_port as batch

def headers(folder):
    readback_headers(folder); folder=Path(folder)
    p=folder/'ps2fb-gs-device.h'; p.write_text(batch.adapt_provider(p.read_text()))
    for name, transform in [('ps2fb-gs-render-device.h', batch.adapt_render),
                            ('ps2fb-gs-texture-device.h', batch.adapt_texture),
                            ('ps2fb-gs-transfer-device.h', batch.adapt_transfer)]:
        (folder/name).write_text(transform((ROOT/'kernel/new-drivers'/name).read_text()))
    # Existing lifecycle scaffold's DMA bound predates texture transfer batches.
    source=(ROOT/'tests/graphics/ps2-gs-device.c').read_text().replace('n<=200','n<=256')
    (folder/'ps2-gs-device.c').write_text(source)
    old=(ROOT/'tests/graphics/ps2-gs-logic.c').read_text().replace('int main(', 'int previous_logic_main(')
    (folder/'previous-logic-kernel.c').write_text(old)

@unittest.skipUnless(shutil.which('cc'), 'native C compiler required')
class GSBatch(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory(); headers(cls.folder.name)
        cls.binary=Path(cls.folder.name)/'batch'
        subprocess.run(['cc','-std=gnu17','-Wall','-Wextra','-Werror','-Wno-unused-function',
            '-Wno-unused-parameter','-pthread','-fsanitize=address,undefined','-fno-pie','-no-pie',
            '-I'+cls.folder.name,'-I'+str(ROOT/'kernel/new-drivers'),'-I'+str(ROOT/'tests/graphics'),
            str(ROOT/'tests/graphics/ps2-gs-batch.c'),'-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def run_case(self, case): subprocess.run([str(self.binary),case],check=True,timeout=30)
    def test_joined_packets_retain_masks_boolean_math_order_and_single_finish(self): self.run_case('packets')
    def test_64_rectangles_use_8_dmas_and_every_rectangle_is_preflighted(self): self.run_case('solid')
    def test_render_and_texture_batches_use_bounded_grouped_dmas(self): self.run_case('render')
    def test_timeout_is_sticky_and_invalidated_session_never_draws(self): self.run_case('failure')
if __name__=='__main__': unittest.main()
