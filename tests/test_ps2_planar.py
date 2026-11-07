"""Native Xorg XY images and shared Mesa bitplane snapshots/Boolean runs."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from test_ps2_image_gc import ROOT,IMAGE_SOURCES,headers as image_headers,copy_flags,XORG
PLANAR_SOURCES=IMAGE_SOURCES
CASES=('bitmap','planes','padding','clipping','rgb555','boolean','bitorder','snapshot',
       'noop','preflight','allocation','budget','failure','rewrap','cursor','random')
def headers(folder):
 image_headers(folder)
 (Path(folder)/'previous-image.c').write_text((ROOT/'tests/graphics/ps2-image-gc.c').read_text()
  .replace('int main(', 'int previous_image_main(').replace('int __wrap_ioctl(', 'int image_ioctl('))
@unittest.skipUnless(shutil.which('cc'),'native compiler required')
class NativePlanar(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.folder=tempfile.TemporaryDirectory();headers(cls.folder.name);cls.binary=Path(cls.folder.name)/'planar'
  subprocess.run(['cc','-std=gnu17','-Wall','-Wextra','-Werror','-Wno-unused-function',
   '-fsanitize=address,undefined','-fno-pie','-no-pie','-DUSE_GCC_ATOMIC_BUILTINS',*copy_flags(),
   '-I'+cls.folder.name,'-I'+str(ROOT/'tests/graphics'),*(str(ROOT/p) for p in PLANAR_SOURCES),
   str(XORG/'mi/micopy.c'),str(ROOT/'tests/graphics/ps2-planar.c'),
   *['-Wl,--wrap='+n for n in ('open','close','ioctl','nanosleep','calloc','malloc')],
   '-latomic','-lpixman-1','-lm','-o',str(cls.binary)],check=True)
 @classmethod
 def tearDownClass(cls):cls.folder.cleanup()
 def run_case(self,n):subprocess.run([str(self.binary),n],check=True,timeout=60)
for _case in CASES:
 setattr(NativePlanar,'test_'+_case,lambda self,n=_case:self.run_case(n))
if __name__=='__main__':unittest.main()
