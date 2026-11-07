"""Exercise framebuffer transitions and keyboard detection without devices."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(sys.platform == 'linux' and shutil.which('cc'), 'requires Linux C headers/compiler')
class XorgPrepare(unittest.TestCase):
    def test_preserve_timings_skip_native_reset_and_verify_driver_format(self):
        with tempfile.TemporaryDirectory(prefix='eelinux-xorg-prepare-') as folder:
            work = Path(folder)
            program = work / 'prepare'
            subprocess.run(['cc', '-std=gnu17', '-Wall', '-Wextra', '-Werror',
                            str(ROOT / 'tests/windows/xorg-prepare.c'), '-o', str(program)], check=True)
            config = work / 'source.conf'
            config.write_text('    Option "Device" "/dev/input/event0"\n'
                              '    Option "XkbLayout" "us"\n'
                              '    Option "XkbModel" "pc104"\n')
            for scenario in range(4):
                with self.subTest(scenario=scenario):
                    output = work / ('generated-' + str(scenario))
                    subprocess.run([str(program), str(scenario), str(config), str(output)], check=True)
                    if scenario == 2:
                        self.assertFalse(output.exists())
                    else:
                        generated = output.read_text()
                        self.assertIn('"/dev/input/event3"', generated)
                        self.assertIn('"XkbLayout" "gb"', generated)
                        self.assertIn('"XkbModel" "pc105"', generated)


if __name__ == '__main__':
    unittest.main()
