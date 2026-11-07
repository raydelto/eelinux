"""Native filled polygons checked against pinned Xorg MI scan conversion."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from test_ps2_planar import ROOT,PLANAR_SOURCES,headers as planar_headers,copy_flags,XORG
POLYGON_SOURCES=PLANAR_SOURCES+['mesa/ps2/ps2_polygon.c','desktop/xorg/ps2_polygon_gc.c','mesa/ps2/ps2_polygon_stipple.c','mesa/ps2/ps2_polygon_tile.c']
CASES=('convex','concave','rules','relative','clipping','redirected','rgb555','boolean','snapshot','noop','preflight','allocation','budget','failure','rewrap','cursor','random','extreme')
def headers(folder):
 planar_headers(folder)
 (Path(folder)/'previous-planar.c').write_text((ROOT/'tests/graphics/ps2-planar.c').read_text().replace('int main(', 'int previous_planar_main(').replace('int __wrap_ioctl(', 'int planar_ioctl('))
@unittest.skipUnless(shutil.which('cc'),'native compiler required')
class NativePolygons(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.folder=tempfile.TemporaryDirectory();headers(cls.folder.name);cls.binary=Path(cls.folder.name)/'polygon'
  common=['cc','-std=gnu17','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-fno-pie','-no-pie','-DUSE_GCC_ATOMIC_BUILTINS',*copy_flags()]
  reference=Path(cls.folder.name)/'reference.o'
  subprocess.run([*common,'-DmiFillPolygon=miReferenceFillPolygon','-c',str(XORG/'mi/mipoly.c'),'-o',str(reference)],check=True)
  subprocess.run([*common,'-I'+cls.folder.name,'-I'+str(ROOT/'tests/graphics'),*(str(ROOT/p) for p in POLYGON_SOURCES),str(XORG/'mi/micopy.c'),str(reference),str(ROOT/'tests/graphics/ps2-polygon.c'),*['-Wl,--wrap='+n for n in ('open','close','ioctl','nanosleep','calloc','malloc')],'-latomic','-lpixman-1','-lm','-o',str(cls.binary)],check=True)
 @classmethod
 def tearDownClass(cls):cls.folder.cleanup()
 def run_case(self,n):subprocess.run([str(self.binary),n],check=True,timeout=60)
for _case in CASES:setattr(NativePolygons,'test_'+_case,lambda self,n=_case:self.run_case(n))
if __name__=='__main__':unittest.main()
