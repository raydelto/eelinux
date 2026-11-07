"""Reject unexpected Windows build source and unsafe dependency archive members."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import emulator_pcsx2_windows as windows


@unittest.skipUnless(shutil.which("git"), "git required")
class WindowsSourceGuard(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.source = self.root / "source"
        self.source.mkdir()
        (self.source / "pcsx2").mkdir()
        (self.source / "common").mkdir()
        (self.source / "pcsx2/example.cpp").write_bytes(b"const int value = 1;\n")
        (self.source / "common/windows.h").write_bytes(b"// windows\n")
        for command in (["init"], ["config", "core.autocrlf", "false"], ["config", "user.name", "Fixture"],
                        ["config", "user.email", "fixture@example.invalid"], ["add", "."], ["commit", "-m", "fixture"]):
            subprocess.run(["git", "-C", str(self.source), *command], check=True, capture_output=True)
        revision = windows.capture(["git", "-C", self.source, "rev-parse", "HEAD"]).strip()
        mmu = self.root / "mmu.patch"
        mmu.write_bytes(b"diff --git a/pcsx2/example.cpp b/pcsx2/example.cpp\n--- a/pcsx2/example.cpp\n+++ b/pcsx2/example.cpp\n@@ -1 +1 @@\n-const int value = 1;\n+const int value = 2;\n")
        portable = self.root / "windows.patch"
        portable.write_bytes(b"diff --git a/common/windows.h b/common/windows.h\n--- a/common/windows.h\n+++ b/common/windows.h\n@@ -1 +1,2 @@\n // windows\n+#include <bit>\n")
        for path in (mmu, portable):
            subprocess.run(["git", "-C", str(self.source), "-c", "core.autocrlf=true", "apply", str(path)],
                           check=True, capture_output=True)
        mmu_hash = windows.sha256(mmu)
        (self.root / "mmu.json").write_text(json.dumps({"source_revision": revision, "mmu_patch": "mmu.patch",
            "mmu_patch_sha256": mmu_hash, "modified_source_files": ["pcsx2/example.cpp"]}))
        self.data = {"source_revision": revision, "mmu_manifest": "mmu.json", "mmu_patch_sha256": mmu_hash,
            "windows_portability_patch": "windows.patch", "windows_portability_patch_sha256": windows.sha256(portable),
            "windows_portability_source_files": ["common/windows.h"]}
        self.root_patch = patch.object(windows, "ROOT", self.root)
        self.root_patch.start()

    def tearDown(self):
        self.root_patch.stop()
        self.temporary.cleanup()

    def test_exact_recorded_windows_patch_output_passes(self):
        hashes = windows.source_guard(self.source, self.data)
        self.assertEqual(set(hashes), {"pcsx2/example.cpp", "common/windows.h"})

    def test_same_file_unrelated_mutation_is_rejected(self):
        path = self.source / "pcsx2/example.cpp"
        path.write_bytes(path.read_bytes() + b"// unrelated mutation\r\n")
        with self.assertRaisesRegex(RuntimeError, "reviewed output"):
            windows.source_guard(self.source, self.data)

    def test_unrecorded_source_file_and_wrong_patch_digest_rejected(self):
        (self.source / "foreign.cpp").write_bytes(b"// extra\n")
        with self.assertRaisesRegex(RuntimeError, "changes beyond"):
            windows.source_guard(self.source, self.data)
        self.data["mmu_patch_sha256"] = "0" * 64
        with self.assertRaisesRegex(RuntimeError, "recorded inputs"):
            windows.source_guard(self.source, self.data)


class WindowsArchiveGuard(unittest.TestCase):
    def test_traversal_drive_and_foreign_root_rejected_before_extract(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / "deps.7z"
            archive.write_bytes(b"fixture")
            data = {"dependencies": {"sha256": windows.sha256(archive), "bytes": 7}}
            for member in (r"deps\..\outside", r"C:\outside", r"\\server\outside", r"foreign\file"):
                with self.subTest(member=member), patch.object(windows, "capture", return_value="----------\nPath = " + member):
                    with self.assertRaisesRegex(RuntimeError, "Unsafe"):
                        windows.prepare_dependencies(root / "source", archive, root / "7z", data)
            self.assertFalse((root / "source").exists())

    def test_archive_must_match_official_size_and_hash(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / "deps.7z"
            archive.write_bytes(b"fixture")
            data = {"dependencies": {"sha256": "0" * 64, "bytes": 7}}
            with self.assertRaisesRegex(RuntimeError, "size/hash"):
                windows.prepare_dependencies(root / "source", archive, root / "7z", data)


if __name__ == "__main__": unittest.main()
