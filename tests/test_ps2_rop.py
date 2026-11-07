"""GS-only source Boolean copies compared with an independent bitwise oracle."""
from pathlib import Path
import shutil, subprocess, tempfile, unittest
from test_ps2_polygon_stipple import ROOT,SOURCES,headers,copy_flags,XORG
ROP_SOURCES=list(dict.fromkeys(SOURCES+['graphics/ps2_gs_transfer.c','mesa/ps2/ps2_rop.c','desktop/xorg/ps2_readback.c']))
CASES=('nearest-edge-rgbx','nearest-edge-rgb555','all-rgbx','all-rgb555','masks','overlap','bands','preflight','noop','failure','exhaustive','tiles-rgbx','tiles-rgb555','download-rgbx','download-rgb555','readback-records-rgbx','readback-records-rgb555','small-sources',
       'rgba-copy','rgba-overlap','rgba-alpha-pairs','rgba-solids','rgba-download')
@unittest.skipUnless(shutil.which('cc'),'native compiler required')
class ROP(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory();headers(cls.folder.name)
        cls.binary=Path(cls.folder.name)/'rop'
        common=['cc','-std=gnu17','-Wall','-Wextra','-Werror','-Wno-unused-function',
            '-fsanitize=address,undefined','-fno-pie','-no-pie','-DUSE_GCC_ATOMIC_BUILTINS',*copy_flags()]
        ref=Path(cls.folder.name)/'reference.o'
        subprocess.run([*common,'-DmiFillPolygon=miReferenceFillPolygon','-c',str(XORG/'mi/mipoly.c'),'-o',str(ref)],check=True)
        subprocess.run([*common,'-I'+cls.folder.name,'-I'+str(ROOT/'tests/graphics'),
            *(str(ROOT/p) for p in ROP_SOURCES),str(XORG/'mi/micopy.c'),str(ref),
            str(ROOT/'tests/graphics/ps2-rop.c'),*['-Wl,--wrap='+n for n in ('open','close','ioctl','nanosleep','calloc','malloc')],
            '-latomic','-lpixman-1','-lm','-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls):cls.folder.cleanup()
    def run_case(self,n):subprocess.run([str(self.binary),n],check=True,timeout=120)
for _case in CASES:setattr(ROP,'test_'+_case,lambda self,n=_case:self.run_case(n))
if __name__=='__main__':unittest.main()
