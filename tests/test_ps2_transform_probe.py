"""Native transformed 3D diagnostic pixels, depth and console cleanup."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from test_ps2_transform import ROOT,SOURCES as PROBE_SOURCES,copy_flags
CASES=('success','first-failure','later-failure','bar-failure','present-failure','interrupt')
@unittest.skipUnless(shutil.which('cc'),'native compiler required')
class TransformProbe(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.folder=tempfile.TemporaryDirectory();cls.binary=Path(cls.folder.name)/'probe'
  flags=['cc','-std=gnu17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-pie','-no-pie','-DUSE_GCC_ATOMIC_BUILTINS',*copy_flags()]
  obj=Path(cls.folder.name)/'main.o'
  subprocess.run([*flags,'-DNDEBUG','-Dmain=ps2_transform_probe_main','-c',str(ROOT/'desktop/ps2-gs-transform-test.c'),'-o',str(obj)],check=True)
  subprocess.run([*flags,str(obj),*(str(ROOT/p) for p in PROBE_SOURCES),str(ROOT/'tests/graphics/ps2-transform-probe.c'),
    *['-Wl,--wrap='+n for n in ('open','close','ioctl','nanosleep')],'-latomic','-lpixman-1','-lm','-o',str(cls.binary)],check=True)
 @classmethod
 def tearDownClass(cls):cls.folder.cleanup()
 def run_case(self,n):subprocess.run([str(self.binary),n],check=True,timeout=30)
for _case in CASES:setattr(TransformProbe,'test_'+_case.replace('-','_'),lambda self,n=_case:self.run_case(n))
if __name__=='__main__':unittest.main()
