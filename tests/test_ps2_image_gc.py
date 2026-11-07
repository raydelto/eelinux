"""Native client images/spans and GS upload/Boolean ordering."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from test_ps2_cursor import ROOT,CURSOR_SOURCES,headers as cursor_headers,copy_flags,XORG
IMAGE_SOURCES=list(dict.fromkeys(CURSOR_SOURCES+['graphics/ps2_gs_transfer.c','mesa/ps2/ps2_rop.c','mesa/ps2/ps2_plane.c','mesa/ps2/ps2_image.c','mesa/ps2/ps2_planar.c','desktop/xorg/ps2_readback.c','desktop/xorg/ps2_image_gc.c']))
CASES=('put','spans','redirected','rgb555','boolean','snapshot','noop','preflight','allocation','budget','failure','rewrap','lifetime','installation','cursor','random')
def headers(folder):
 cursor_headers(folder)
 p=Path(folder)/'previous-cursor.c'
 p.write_text((ROOT/'tests/graphics/ps2-cursor.c').read_text().replace('int main(', 'int previous_cursor_main(').replace('int __wrap_ioctl(', 'int cursor_ioctl('))
@unittest.skipUnless(shutil.which('cc'),'native compiler required')
class NativeImages(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.folder=tempfile.TemporaryDirectory();headers(cls.folder.name);cls.binary=Path(cls.folder.name)/'image'
  subprocess.run(['cc','-std=gnu17','-Wall','-Wextra','-Werror','-Wno-unused-function',
   '-fsanitize=address,undefined','-fno-pie','-no-pie','-DUSE_GCC_ATOMIC_BUILTINS',*copy_flags(),
   '-I'+cls.folder.name,'-I'+str(ROOT/'tests/graphics'),*(str(ROOT/p) for p in IMAGE_SOURCES),
   str(XORG/'mi/micopy.c'),str(ROOT/'tests/graphics/ps2-image-gc.c'),
   *['-Wl,--wrap='+n for n in ('open','close','ioctl','nanosleep','calloc','malloc')],
   '-latomic','-lpixman-1','-lm','-o',str(cls.binary)],check=True)
 @classmethod
 def tearDownClass(cls):cls.folder.cleanup()
 def run_case(self,n):subprocess.run([str(self.binary),n],check=True,timeout=60)
 def test_put(self):self.run_case('put')
 def test_spans(self):self.run_case('spans')
 def test_redirected(self):self.run_case('redirected')
 def test_rgb555(self):self.run_case('rgb555')
 def test_boolean(self):self.run_case('boolean')
 def test_snapshot(self):self.run_case('snapshot')
 def test_noop(self):self.run_case('noop')
 def test_preflight(self):self.run_case('preflight')
 def test_allocation(self):self.run_case('allocation')
 def test_budget(self):self.run_case('budget')
 def test_failure(self):self.run_case('failure')
 def test_rewrap(self):self.run_case('rewrap')
 def test_lifetime(self):self.run_case('lifetime')
 def test_installation(self):self.run_case('installation')
 def test_cursor(self):self.run_case('cursor')
 def test_random(self):self.run_case('random')
if __name__=='__main__':unittest.main()
