"""Typed RGBA transfer validation and actual GS packet state."""
from pathlib import Path
import shutil, subprocess, tempfile, unittest
from test_ps2_gs_map_blit import ROOT, headers as map_headers
from kernel_gs_rgba_transfer_port import adapt_provider
CASES=('packets','validation','failure')

def headers(folder):
    map_headers(folder)
    p=Path(folder)/'ps2fb-gs-device.h'
    p.write_text(adapt_provider(p.read_text()))

@unittest.skipUnless(shutil.which('cc'), 'native compiler required')
class RGBATransfer(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory();headers(cls.folder.name)
        cls.binary=Path(cls.folder.name)/'rgba-transfer'
        subprocess.run(['cc','-std=gnu17','-Wall','-Wextra','-Werror','-Wno-unused-function',
            '-Wno-unused-parameter','-pthread','-fsanitize=address,undefined','-fno-pie','-no-pie',
            '-I'+cls.folder.name,'-I'+str(ROOT/'kernel/new-drivers'),'-I'+str(ROOT/'tests/graphics'),
            str(ROOT/'tests/graphics/ps2-gs-rgba-transfer.c'),'-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls):cls.folder.cleanup()
    def run_case(self,name):subprocess.run([str(self.binary),name],check=True,timeout=30)
for _case in CASES:
    setattr(RGBATransfer,'test_'+_case,lambda self,n=_case:self.run_case(n))
if __name__=='__main__':unittest.main()
