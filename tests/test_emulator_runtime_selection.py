"""Reject mutated project runtime packages before creating an emulator process."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from emulator_runtime_selection import select_runtime, digest
from emulator_pcsx2_windows import inventory


class RuntimeSelection(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.folder = self.root / 'build/emulator/pcsx2-patched-windows/runtime'
        self.folder.mkdir(parents=True)
        self.program = self.folder / 'pcsx2-qt.exe'
        self.program.write_bytes(b'non-runnable runtime fixture')
        (self.folder / 'Qt6Core.dll').write_bytes(b'library fixture')
        for name in ('scripts/emulator_pcsx2_windows.py', 'emulator/patches/pcsx2-2.6.3-mmu.patch',
                     'emulator/patches/windows.patch'):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('fixture')
        lock = self.root / 'emulator/pcsx2-windows-build.json'
        lock.write_text(json.dumps({'source_revision': 'pinned revision',
                                   'mmu_patch_sha256': digest(self.root / 'emulator/patches/pcsx2-2.6.3-mmu.patch'),
                                   'windows_portability_patch': 'emulator/patches/windows.patch'}))
        record = {'source_revision': 'pinned revision', 'manifest_sha256': digest(lock),
                  'builder_sha256': digest(self.root / 'scripts/emulator_pcsx2_windows.py'),
                  'mmu_patch_sha256': digest(self.root / 'emulator/patches/pcsx2-2.6.3-mmu.patch'),
                  'windows_portability_patch_sha256': digest(self.root / 'emulator/patches/windows.patch'),
                  'sha256': digest(self.program), 'bytes': self.program.stat().st_size,
                  'runtime_files_sha256': inventory(self.folder)}
        (self.folder / 'eelinux-runtime.json').write_text(json.dumps(record))
        self.requested = self.root / 'installed/pcsx2-qt.exe'

    def tearDown(self): self.tmp.cleanup()

    def test_checked_package_selected_and_stock_override_preserved(self):
        selected, proof = select_runtime(self.requested, 'windows', root=self.root)
        self.assertEqual(selected, self.program)
        self.assertEqual(proof['requested_program'], str(self.requested))
        for host, stock in (('linux', False), ('windows', True)):
            self.assertEqual(select_runtime(self.requested, host, stock=stock, root=self.root),
                             (self.requested, None))

    def test_changed_dll_added_file_and_patch_cannot_be_launched(self):
        library = self.folder / 'Qt6Core.dll'
        library.write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'runtime changed'):
            select_runtime(self.requested, 'windows', root=self.root)
        library.write_bytes(b'library fixture')
        extra = self.folder / 'unexpected.dll'
        extra.write_bytes(b'extra')
        with self.assertRaises(ValueError): select_runtime(self.requested, 'windows', root=self.root)
        extra.unlink()
        (self.root / 'emulator/patches/pcsx2-2.6.3-mmu.patch').write_text('changed')
        with self.assertRaises(ValueError): select_runtime(self.requested, 'windows', root=self.root)

    def test_missing_project_runtime_does_not_silently_select_stock(self):
        (self.folder / 'eelinux-runtime.json').unlink()
        with self.assertRaisesRegex(ValueError, 'MMU runtime is missing'):
            select_runtime(self.requested, 'windows', root=self.root)
        self.assertEqual(select_runtime(self.requested, 'windows', stock=True, root=self.root),
                         (self.requested, None))


if __name__ == '__main__': unittest.main()
