"""Explicit 3D MVP/clipping adapter and shared Xorg ordering, without GL shaders."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from test_ps2_pipe import ROOT,PIPE_SOURCES,headers as pipe_headers
from test_ps2_paint_window import copy_flags,XORG
SOURCES=PIPE_SOURCES+['mesa/ps2/ps2_transform.c','desktop/xorg/ps2_copy_exa.c',
    'mesa/ps2/ps2_logic.c','mesa/ps2/ps2_solid_batch.c']
CASES=('transform','matrix','perspective','clip','near','eye','viewport','colours','noop','preflight','allocation','budget','snapshot','target','xorg','failure','rgb555','lifetime','random')
def headers(folder):
 pipe_headers(folder)
 (Path(folder)/'previous-pipe.c').write_text((ROOT/'tests/graphics/ps2-pipe.c').read_text().replace('int main(', 'int previous_pipe_main(').replace('int __wrap_ioctl(', 'int pipe_ioctl('))
@unittest.skipUnless(shutil.which('cc'),'native compiler required')
class Transform(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.folder=tempfile.TemporaryDirectory();headers(cls.folder.name);cls.binary=Path(cls.folder.name)/'transform'
  subprocess.run(['cc','-std=gnu17','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-fno-pie','-no-pie','-DUSE_GCC_ATOMIC_BUILTINS',*copy_flags(),'-I'+cls.folder.name,'-I'+str(ROOT/'tests/graphics'),*(str(ROOT/p) for p in SOURCES),str(ROOT/'tests/graphics/ps2-transform.c'),*['-Wl,--wrap='+n for n in ('open','close','ioctl','nanosleep','malloc')],'-latomic','-lpixman-1','-lm','-o',str(cls.binary)],check=True)
 @classmethod
 def tearDownClass(cls):cls.folder.cleanup()
 def run_case(self,n):subprocess.run([str(self.binary),n],check=True,timeout=30)
for _case in CASES:setattr(Transform,'test_'+_case,lambda self,n=_case:self.run_case(n))
if __name__=='__main__':unittest.main()
