"""Actual native opaque stipple diagnostic, independent pixels and restoration."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_ps2_fill import ROOT, FILL_SOURCES
from test_ps2_copy_window import copy_flags

PROBE_SOURCES = [p for p in FILL_SOURCES if p not in (
    'desktop/xorg/ps2_copy.c', 'desktop/xorg/ps2_copy_exa.c',
    'desktop/xorg/ps2_blit_exa.c', 'desktop/xorg/ps2_copy_window.c',
    'desktop/xorg/ps2_paint_window.c')]
CASES = ('success', 'seed-failure', 'expand-failure', 'present-failure', 'interrupt')

@unittest.skipUnless(shutil.which('cc'), 'native compiler required')
class StippleProbe(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory(); cls.binary = Path(cls.folder.name)/'probe'
        flags = ['cc', '-std=gnu17', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=address,undefined', '-fno-pie', '-no-pie', '-DUSE_GCC_ATOMIC_BUILTINS', *copy_flags()]
        obj = Path(cls.folder.name)/'main.o'
        subprocess.run([*flags, '-Dmain=ps2_stipple_probe_main', '-c', str(ROOT/'desktop/ps2-gs-stipple-test.c'), '-o', str(obj)], check=True)
        subprocess.run([*flags, str(obj), *(str(ROOT/p) for p in PROBE_SOURCES),
            str(ROOT/'tests/graphics/ps2-stipple-probe.c'),
            *['-Wl,--wrap='+n for n in ('open', 'close', 'ioctl', 'nanosleep')],
            '-latomic', '-lpixman-1','-lm', '-o', str(cls.binary)], check=True)
    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def run_case(self, name): subprocess.run([str(self.binary), name], check=True, timeout=30)
    def test_four_scene_pixels_and_console_resource_restoration(self): self.run_case('success')
    def test_failed_tile_seed_restores_hook_without_replay(self): self.run_case('seed-failure')
    def test_failed_tile_expansion_reclaims_session_without_replay(self): self.run_case('expand-failure')
    def test_failed_presentation_reclaims_session_and_restores_console(self): self.run_case('present-failure')
    def test_sigint_unwinds_screen_owner_before_session_close(self): self.run_case('interrupt')
if __name__ == '__main__': unittest.main()
