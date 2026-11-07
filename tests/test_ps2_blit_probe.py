"""Actual blit/composite diagnostic scene sequence and console restoration."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_ps2_pipe import ROOT, PIPE_SOURCES
from test_ps2_copy import copy_flags

PROBE_SOURCES = PIPE_SOURCES + ['desktop/xorg/ps2_blit_exa.c']
CASES = ('success', 'blit-failure', 'composite-failure', 'interrupt')

@unittest.skipUnless(shutil.which('cc'), 'native compiler required')
class BlitProbe(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory(); cls.binary = Path(cls.folder.name)/'probe'
        flags = ['cc', '-std=gnu17', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=address,undefined', '-fno-pie', '-no-pie', '-DUSE_GCC_ATOMIC_BUILTINS', *copy_flags()]
        obj = Path(cls.folder.name)/'main.o'
        subprocess.run([*flags, '-Dmain=ps2_blit_probe_main', '-c', str(ROOT/'desktop/ps2-gs-blit-test.c'), '-o', str(obj)], check=True)
        subprocess.run([*flags, str(obj), *(str(ROOT/p) for p in PROBE_SOURCES),
            str(ROOT/'tests/graphics/ps2-blit-probe.c'),
            *['-Wl,--wrap='+n for n in ('open', 'close', 'ioctl', 'nanosleep')],
            '-latomic', '-lpixman-1','-lm', '-o', str(cls.binary)], check=True)
    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def run_case(self, name): subprocess.run([str(self.binary), name], check=True, timeout=20)
    def test_actual_blit_and_composite_four_scenes_then_console(self): self.run_case('success')
    def test_failed_scaled_blit_stops_without_replay_and_restores_console(self): self.run_case('blit-failure')
    def test_failed_exa_composite_restores_callbacks_and_console(self): self.run_case('composite-failure')
    def test_sigint_releases_owned_resources_before_console_restoration(self): self.run_case('interrupt')

if __name__ == '__main__': unittest.main()
