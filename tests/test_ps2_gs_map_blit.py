"""Inspect native byte mapping across pages, unaligned rectangles and edges."""
from pathlib import Path
import shutil, subprocess, tempfile, unittest
from test_ps2_gs_map import ROOT,headers as map_headers
from kernel_gs_map_blit_port import adapt_provider
CASES=('packets','validation','failure')

def headers(folder):
    map_headers(folder)
    path=Path(folder)/'ps2fb-gs-device.h';path.write_text(adapt_provider(path.read_text()))
    source=(ROOT/'tests/graphics/ps2-gs-map.c').read_text().replace('int main(', 'int previous_map_main(')
    (Path(folder)/'previous-map-kernel.c').write_text(source)

@unittest.skipUnless(shutil.which('cc'),'native C compiler required')
class GSMapBlit(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory();headers(cls.folder.name);cls.binary=Path(cls.folder.name)/'map-blit'
        subprocess.run(['cc','-std=gnu17','-Wall','-Wextra','-Werror','-Wno-unused-function',
            '-Wno-unused-parameter','-pthread','-fsanitize=address,undefined','-fno-pie','-no-pie',
            '-I'+cls.folder.name,'-I'+str(ROOT/'kernel/new-drivers'),'-I'+str(ROOT/'tests/graphics'),
            str(ROOT/'tests/graphics/ps2-gs-map-blit.c'),'-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls):cls.folder.cleanup()
    def run_case(self,case):subprocess.run([str(self.binary),case],check=True,timeout=30)
    def test_packets(self):self.run_case('packets')
    def test_validation(self):self.run_case('validation')
    def test_failure(self):self.run_case('failure')

if __name__=='__main__':unittest.main()
