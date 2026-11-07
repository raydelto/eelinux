"""Actual native logic diagnostic failure/SIGINT cleanup and text restoration."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_ps2_logic import ROOT, LOGIC_SOURCES, render_flags


@unittest.skipUnless(shutil.which('cc'), 'native compiler required')
class LogicProbe(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory(); folder=Path(cls.folder.name)
        common=['cc','-std=gnu17','-Wall','-Wextra','-Werror','-DUSE_GCC_ATOMIC_BUILTINS',
            '-fsanitize=address,undefined','-fno-pie','-no-pie',*render_flags()]
        main=folder/'main.o'
        subprocess.run([*common,'-Dmain=ps2_logic_probe_main','-c',str(ROOT/'desktop/ps2-gs-logic-test.c'),'-o',str(main)],check=True)
        cls.binary=folder/'probe'
        subprocess.run([*common,str(main),*(str(ROOT/p) for p in LOGIC_SOURCES),str(ROOT/'tests/graphics/ps2-logic-probe.c'),
            *['-Wl,--wrap='+n for n in ('open','close','ioctl','nanosleep')],'-latomic','-lpixman-1','-lm','-o',str(cls.binary)],check=True)

    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def run_case(self,c): subprocess.run([str(self.binary),c],check=True,timeout=20)
    def test_success_releases_context_and_file_before_text_mode(self): self.run_case('success')
    def test_hardware_failure_never_replays_and_restores_console(self): self.run_case('failure')
    def test_sigint_releases_ownership_and_restores_console(self): self.run_case('interrupt')


if __name__=='__main__': unittest.main()
