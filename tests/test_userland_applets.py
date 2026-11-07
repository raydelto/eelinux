"""Reject BusyBox executables and hidden applet links in a GNU staging tree."""
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from userland import BuildError, reject_busybox

class GNUOnlyRuntime(unittest.TestCase):
    def test_real_gnu_files_are_allowed(self):
        with tempfile.TemporaryDirectory() as temporary:
            stage = Path(temporary)
            (stage / 'bash').write_bytes(b'GNU fixture')
            reject_busybox(stage)

    def test_busybox_binary_or_applet_is_rejected(self):
        for name in ('busybox', 'udhcpc'):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary:
                stage = Path(temporary)
                (stage / name).write_bytes(b'forbidden fixture')
                with self.assertRaises(BuildError): reject_busybox(stage)

    @unittest.skipIf(sys.platform == 'win32', 'requires symlinks')
    def test_hidden_busybox_link_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            stage = Path(temporary)
            (stage / 'ls').symlink_to('/bin/busybox')
            with self.assertRaises(BuildError): reject_busybox(stage)

if __name__ == '__main__':
    unittest.main()
