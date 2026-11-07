"""Real GS cursor compositing, save-under and clipping against pixman."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from test_ps2_composite import ROOT,ROP_SOURCES,headers,copy_flags,XORG
CASES=('rgbx','rgb555','snapshot-rgbx','snapshot-rgb555','damage-rgbx','damage-rgb555',
       'suspend-rgbx','suspend-rgb555','replace-rgbx','replace-rgb555','guards','failure')
@unittest.skipUnless(shutil.which('cc'),'native compiler required')
class ARGBCursor(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.folder=tempfile.TemporaryDirectory();folder=Path(cls.folder.name);headers(cls.folder.name);cls.binary=folder/'cursor-argb'
  (folder/'previous-rop.c').write_text((ROOT/'tests/graphics/ps2-rop.c').read_text().replace('int main(', 'int previous_rop_main(').replace('int __wrap_ioctl(', 'int rop_ioctl('))
  (folder/'previous-composite.c').write_text((ROOT/'tests/graphics/ps2-composite.c').read_text().replace('int main(', 'int previous_composite_main(').replace('int __wrap_ioctl(', 'int composite_ioctl('))
  common=['cc','-std=gnu17','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-fno-pie','-no-pie','-DUSE_GCC_ATOMIC_BUILTINS',*copy_flags()]
  ref=folder/'reference.o';subprocess.run([*common,'-DmiFillPolygon=miReferenceFillPolygon','-c',str(XORG/'mi/mipoly.c'),'-o',str(ref)],check=True)
  subprocess.run([*common,'-I'+cls.folder.name,'-I'+str(ROOT/'tests/graphics'),*(str(ROOT/p) for p in ROP_SOURCES),str(XORG/'mi/micopy.c'),str(ref),str(ROOT/'tests/graphics/ps2-cursor-argb.c'),*['-Wl,--wrap='+n for n in ('open','close','ioctl','nanosleep','calloc','malloc')],'-latomic','-lpixman-1','-lm','-o',str(cls.binary)],check=True)
 @classmethod
 def tearDownClass(cls):cls.folder.cleanup()
 def run_case(self,name):subprocess.run([str(self.binary),name],check=True,timeout=120)
for case in CASES:setattr(ARGBCursor,'test_'+case.replace('-','_'),lambda self,n=case:self.run_case(n))
if __name__=='__main__':unittest.main()
