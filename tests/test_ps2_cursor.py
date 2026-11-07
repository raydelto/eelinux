"""Native GS cursor save-under, front access ordering and Xorg pointer lifecycle."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from test_ps2_paint_window import headers,ROOT,copy_flags,XORG
from test_ps2_fill import FILL_SOURCES
CURSOR_SOURCES=FILL_SOURCES+['mesa/ps2/ps2_cursor.c','desktop/xorg/ps2_cursor_screen.c']
CASES=('shape','snapshot','guards','copy','queued','rgb555','preflight','budget','suspend','ownership','lifetime','failure','pointer','damage','close','installation','slave','empty','unrelated','repeated')
@unittest.skipUnless(shutil.which('cc'),'native compiler required')
class NativeCursor(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.folder=tempfile.TemporaryDirectory();headers(cls.folder.name);cls.binary=Path(cls.folder.name)/'cursor'
  subprocess.run(['cc','-std=gnu17','-Wall','-Wextra','-Werror','-Wno-unused-function',
   '-fsanitize=address,undefined','-fno-pie','-no-pie','-DUSE_GCC_ATOMIC_BUILTINS',*copy_flags(),
   '-I'+cls.folder.name,'-I'+str(ROOT/'tests/graphics'),*(str(ROOT/p) for p in CURSOR_SOURCES),
   str(XORG/'mi/micopy.c'),str(ROOT/'tests/graphics/ps2-cursor.c'),
   *['-Wl,--wrap='+n for n in ('open','close','ioctl','nanosleep','calloc')],
   '-latomic','-lpixman-1','-lm','-o',str(cls.binary)],check=True)
 @classmethod
 def tearDownClass(cls):cls.folder.cleanup()
 def run_case(self,n):subprocess.run([str(self.binary),n],check=True,timeout=60)
 def test_shape(self):self.run_case('shape')
 def test_snapshot(self):self.run_case('snapshot')
 def test_guards(self):self.run_case('guards')
 def test_copy(self):self.run_case('copy')
 def test_queued(self):self.run_case('queued')
 def test_rgb555(self):self.run_case('rgb555')
 def test_preflight(self):self.run_case('preflight')
 def test_budget(self):self.run_case('budget')
 def test_suspend(self):self.run_case('suspend')
 def test_ownership(self):self.run_case('ownership')
 def test_lifetime(self):self.run_case('lifetime')
 def test_failure(self):self.run_case('failure')
 def test_pointer(self):self.run_case('pointer')
 def test_damage(self):self.run_case('damage')
 def test_close(self):self.run_case('close')
 def test_installation(self):self.run_case('installation')
 def test_slave(self):self.run_case('slave')
 def test_empty(self):self.run_case('empty')
 def test_unrelated(self):self.run_case('unrelated')
 def test_repeated(self):self.run_case('repeated')
if __name__=='__main__':unittest.main()
