"""Actual new screen/context diagnostic and its restoration on failure/signal."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_ps2_texture_probe import ROOT,SOURCES,render_flags
CONTEXT_SOURCES=SOURCES+['mesa/ps2/ps2_pipe.c','mesa/ps2/ps2_copy_batch.c','mesa/ps2/ps2_blit.c']
def fixture(folder):
    text=(ROOT/'tests/graphics/ps2-texture-probe.c').read_text().replace('ps2_texture_probe_main','ps2_context_probe_main')
    text=text.replace('native texture probe lifecycle PASS','native context probe lifecycle PASS')
    # Only an optional debug label is replaced. All real surface/resource
    # references and destroy callbacks are linked from the actual driver.
    text+='\nvoid debug_describe_surface(char *buf,const struct pipe_surface *surface) { assert(surface); buf[0]=0; }\n'
    path=Path(folder)/'context-probe.c'; path.write_text(text); return path
@unittest.skipUnless(shutil.which('cc'),'native compiler required')
class ContextProbe(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory(); cls.binary=Path(cls.folder.name)/'probe'
        common=['cc','-std=gnu17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
            '-fno-pie','-no-pie','-DUSE_GCC_ATOMIC_BUILTINS',*render_flags()]
        obj=Path(cls.folder.name)/'main.o'
        subprocess.run([*common,'-Dmain=ps2_context_probe_main','-c',str(ROOT/'desktop/ps2-gs-context-test.c'),'-o',str(obj)],check=True)
        subprocess.run([*common,str(obj),*(str(ROOT/p) for p in CONTEXT_SOURCES),str(fixture(cls.folder.name)),
            *['-Wl,--wrap='+n for n in ('open','close','ioctl','nanosleep')],'-latomic','-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def run_case(self,c): subprocess.run([str(self.binary),c],check=True,timeout=15)
    def test_native_callback_scene_planar_grid_and_exact_reference_cleanup(self): self.run_case('success')
    def test_failed_sampling_has_no_later_upload_draw_copy_or_replay(self): self.run_case('failure')
    def test_sigint_closes_owned_file_before_console_restoration(self): self.run_case('interrupt')
if __name__=='__main__': unittest.main()
