"""New core-X paths with pinned MI clipping/scan conversion and pixel references."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from test_ps2_polygon_stipple import ROOT,SOURCES,headers,copy_flags,XORG
CASES=('tile','rules','relative','rgb555','noop','invalid','validation','plane','plane-owned','plane-rgb555')
@unittest.skipUnless(shutil.which('cc'),'native compiler required')
class Core2D(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.folder=tempfile.TemporaryDirectory();headers(cls.folder.name);cls.binary=Path(cls.folder.name)/'core2d'
  common=['cc','-std=gnu17','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-fno-pie','-no-pie','-DUSE_GCC_ATOMIC_BUILTINS',*copy_flags()]
  ref=Path(cls.folder.name)/'reference.o'
  subprocess.run([*common,'-DmiFillPolygon=miReferenceFillPolygon','-c',str(XORG/'mi/mipoly.c'),'-o',str(ref)],check=True)
  subprocess.run([*common,'-I'+cls.folder.name,'-I'+str(ROOT/'tests/graphics'),*(str(ROOT/p) for p in SOURCES),str(XORG/'mi/micopy.c'),str(ref),str(ROOT/'tests/graphics/ps2-core2d.c'),*['-Wl,--wrap='+n for n in ('open','close','ioctl','nanosleep','calloc','malloc')],'-latomic','-lpixman-1','-lm','-o',str(cls.binary)],check=True)
 @classmethod
 def tearDownClass(cls):cls.folder.cleanup()
 def run_case(self,n):subprocess.run([str(self.binary),n],check=True,timeout=30)
for _case in CASES:setattr(Core2D,'test_'+_case,lambda self,n=_case:self.run_case(n))
if __name__=='__main__':unittest.main()
