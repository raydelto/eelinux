"""Actual native screen window diagnostic and console/resource restoration."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_ps2_pipe import ROOT, PIPE_SOURCES
from test_ps2_copy_window import copy_flags

PROBE_SOURCES = PIPE_SOURCES + ['desktop/xorg/ps2_copy_window.c']
CASES = ('success', 'move-failure', 'present-failure', 'interrupt')

@unittest.skipUnless(shutil.which('cc'), 'native compiler required')
class WindowProbe(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory(); cls.binary = Path(cls.folder.name)/'probe'
        flags = ['cc', '-std=gnu17', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=address,undefined', '-fno-pie', '-no-pie', '-DUSE_GCC_ATOMIC_BUILTINS', *copy_flags()]
        obj = Path(cls.folder.name)/'main.o'
        subprocess.run([*flags, '-Dmain=ps2_window_probe_main', '-c', str(ROOT/'desktop/ps2-gs-window-test.c'), '-o', str(obj)], check=True)
        subprocess.run([*flags, str(obj), *(str(ROOT/p) for p in PROBE_SOURCES),
            str(ROOT/'tests/graphics/ps2-window-probe.c'),
            *['-Wl,--wrap='+n for n in ('open', 'close', 'ioctl', 'nanosleep')],
            '-latomic', '-lpixman-1','-lm', '-o', str(cls.binary)], check=True)
    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def run_case(self, name): subprocess.run([str(self.binary), name], check=True, timeout=20)
    def test_four_actual_window_scenes_release_resources_then_console(self): self.run_case('success')
    def test_failed_native_window_move_restores_hook_without_replay(self): self.run_case('move-failure')
    def test_failed_presentation_reclaims_session_and_restores_console(self): self.run_case('present-failure')
    def test_sigint_unwinds_screen_owner_before_session_close(self): self.run_case('interrupt')
if __name__ == '__main__': unittest.main()
