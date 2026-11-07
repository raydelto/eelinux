"""GS sample counting and packed coverage compared with pixman."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from test_ps2_composite import ROOT,ROP_SOURCES,headers,copy_flags,XORG
CASES=('a1','a4','a8','a1-msb','traps-a1','traps-a4','traps-a8',
       'triangles-a1','triangles-a4','triangles-a8','random-a1','random-a4','random-a8',
       'rectangle-a1','rectangle-a4','rectangle-a8','guards','failure')
@unittest.skipUnless(shutil.which('cc'),'native compiler required')
class Raster(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.folder=tempfile.TemporaryDirectory();folder=Path(cls.folder.name);headers(cls.folder.name);cls.binary=folder/'raster'
  for name in ('rop','composite'):
   (folder/('previous-'+name+'.c')).write_text((ROOT/('tests/graphics/ps2-'+name+'.c')).read_text().replace('int main(', 'int previous_'+name+'_main(').replace('int __wrap_ioctl(', 'int '+name+'_ioctl('))
  common=['cc','-std=gnu17','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-fno-pie','-no-pie','-DUSE_GCC_ATOMIC_BUILTINS',*copy_flags()]
  ref=folder/'reference.o';subprocess.run([*common,'-DmiFillPolygon=miReferenceFillPolygon','-c',str(XORG/'mi/mipoly.c'),'-o',str(ref)],check=True)
  subprocess.run([*common,'-I'+cls.folder.name,'-I'+str(ROOT/'tests/graphics'),*(str(ROOT/p) for p in [*ROP_SOURCES,'mesa/ps2/ps2_render_raster.c']),str(XORG/'mi/micopy.c'),str(ref),str(ROOT/'tests/graphics/ps2-raster.c'),*['-Wl,--wrap='+n for n in ('open','close','ioctl','nanosleep','calloc','malloc')],'-latomic','-lpixman-1','-lm','-o',str(cls.binary)],check=True)
 @classmethod
 def tearDownClass(cls):cls.folder.cleanup()
 def run_case(self,name):subprocess.run([str(self.binary),name],check=True,timeout=180)
for case in CASES:setattr(Raster,'test_'+case.replace('-','_'),lambda self,n=case:self.run_case(n))
if __name__=='__main__':unittest.main()
