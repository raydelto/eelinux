"""Compile the actual bounded GS lookup provider and inspect native packets."""
from pathlib import Path
import shutil, subprocess, tempfile, unittest
from test_ps2_gs_batch import ROOT, headers as batch_headers
from kernel_gs_lut_port import adapt_provider
CASES = ('packets', 'validation', 'failure')

def headers(folder):
    batch_headers(folder)
    path = Path(folder) / 'ps2fb-gs-device.h'
    path.write_text(adapt_provider(path.read_text()))
    source = (ROOT / 'tests/graphics/ps2-gs-batch.c').read_text().replace('int main(', 'int previous_batch_main(')
    (Path(folder) / 'previous-batch-kernel.c').write_text(source)

@unittest.skipUnless(shutil.which('cc'), 'native C compiler required')
class GSLUT(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory(); headers(cls.folder.name)
        cls.binary = Path(cls.folder.name) / 'lut'
        subprocess.run(['cc', '-std=gnu17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
            '-Wno-unused-parameter', '-pthread', '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
            '-I' + cls.folder.name, '-I' + str(ROOT / 'kernel/new-drivers'), '-I' + str(ROOT / 'tests/graphics'),
            str(ROOT / 'tests/graphics/ps2-gs-lut.c'), '-o', str(cls.binary)], check=True)
    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def test_packets(self): self.run_case('packets')
    def test_validation(self): self.run_case('validation')
    def test_failure(self): self.run_case('failure')
    def run_case(self, case): subprocess.run([str(self.binary), case], check=True, timeout=30)

if __name__ == '__main__': unittest.main()
