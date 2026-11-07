"""Colour CopyPlane, independent snapshot oracle, actual miDoCopy and GS paths."""
from pathlib import Path
import shutil, subprocess, tempfile, unittest
from test_ps2_rop import ROOT, ROP_SOURCES, headers, copy_flags, XORG
CASES = ('rgbx', 'rgb555', 'cross-depth', 'overlap', 'bands', 'clipping',
         'window', 'planes', 'random', 'streaming', 'preflight', 'noop',
         'allocation', 'failure', 'snapshot', 'mono-rgbx', 'mono-rgb555','rgba-overlap')

@unittest.skipUnless(shutil.which('cc'), 'native compiler required')
class ColourCopyPlane(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory(); headers(cls.folder.name)
        cls.binary = Path(cls.folder.name) / 'plane'
        (Path(cls.folder.name) / 'previous-rop.c').write_text(
            (ROOT / 'tests/graphics/ps2-rop.c').read_text()
            .replace('int main(', 'int previous_rop_main(')
            .replace('int __wrap_ioctl(', 'int rop_ioctl('))
        common = ['cc', '-std=gnu17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                  '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                  '-DUSE_GCC_ATOMIC_BUILTINS', *copy_flags()]
        ref = Path(cls.folder.name) / 'reference.o'
        subprocess.run([*common, '-DmiFillPolygon=miReferenceFillPolygon', '-c',
                        str(XORG / 'mi/mipoly.c'), '-o', str(ref)], check=True)
        subprocess.run([*common, '-I' + cls.folder.name, '-I' + str(ROOT / 'tests/graphics'),
                        *(str(ROOT / p) for p in ROP_SOURCES), str(XORG / 'mi/micopy.c'), str(ref),
                        str(ROOT / 'tests/graphics/ps2-plane.c'),
                        *['-Wl,--wrap=' + n for n in ('open', 'close', 'ioctl', 'nanosleep', 'calloc', 'malloc')],
                        '-latomic', '-lpixman-1','-lm', '-o', str(cls.binary)], check=True)
    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def run_case(self, name): subprocess.run([str(self.binary), name], check=True, timeout=120)
for _case in CASES:
    setattr(ColourCopyPlane, 'test_' + _case.replace('-', '_'), lambda self, n=_case: self.run_case(n))
if __name__ == '__main__': unittest.main()
