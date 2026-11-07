"""Isolated zipapp installation checks; no hosting access or release payloads."""

from argparse import Namespace
from hashlib import sha256
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import installer_main
import make_installer


@unittest.skipUnless(os.name == "posix" and shutil.which("git"), "Linux Git fixtures required")
class InstallerArchiveTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="eelinux-zipapp-test-")
        self.root = Path(self.temporary.name)
        self.runtime = self.root / "runtime"
        self.runtime.mkdir()
        lock = json.loads((make_installer.HERE / "source-lock.json").read_text())
        header = bytearray(64)
        header[:6] = b"\x7fELF\x02\x01"
        header[18:20] = (62).to_bytes(2, "little")
        (self.runtime / "cgit").write_bytes(header)
        report = {
            "cgit_commit": lock["commit"], "git_commit": lock["git_submodule_commit"],
            "binary_sha256": sha256(header).hexdigest(), "build_mode": "static",
            "elf_has_dynamic_interpreter": False,
        }
        (self.runtime / "eelinux-build.json").write_text(json.dumps(report))
        for name in ("install.py", "source-lock.json"):
            shutil.copyfile(make_installer.HERE / name, self.runtime / name)
        for name in ("cgit.css", "cgit.js", "cgit.png", "favicon.ico"):
            (self.runtime / name).write_bytes(b"fixture")
        hashes = {p.name: sha256(p.read_bytes()).hexdigest() for p in self.runtime.iterdir()}
        (self.runtime / "runtime-bundle.json").write_text(json.dumps({
            "schema": 1, "cgit_commit": lock["commit"],
            "git_commit": lock["git_submodule_commit"], "files": hashes,
        }))
        work = self.root / "work"
        self.command("git", "init", "-b", "main", str(work))
        (work / "README").write_text("fixture source\n")
        self.command("git", "-C", str(work), "add", "README")
        self.command("git", "-C", str(work), "-c", "user.name=Fixture",
                     "-c", "user.email=fixture@example.org", "commit", "-m", "fixture")
        self.bare = self.root / "source.git"
        self.command("git", "clone", "--bare", str(work), str(self.bare))
        self.archive = self.root / "setup.pyz"
        make_installer.create(self.runtime, self.bare, self.archive)
        self.docroot = self.root / "public"
        self.docroot.mkdir()
        self.args = Namespace(
            document_root=str(self.docroot), private_root=str(self.root / "private"),
            repository=str(self.root / "repos/eelinux.git"), work_root=str(self.root / "setup"),
            update=False, check_only=False,
        )

    def tearDown(self):
        self.temporary.cleanup()

    def command(self, *command):
        return subprocess.check_output(command, stderr=subprocess.STDOUT, text=True).strip()

    def rewrite_archive(self, change):
        altered = self.root / "altered.pyz"
        with zipfile.ZipFile(self.archive) as source, zipfile.ZipFile(altered, "w") as target:
            for item in source.infolist():
                target.writestr(item, change(item.filename, source.read(item.filename)))
        return altered

    def test_install_exact_commit_and_safe_rerun(self):
        installer_main.run_installer(self.archive, self.args)
        expected = make_installer.git(self.bare, "rev-parse", "HEAD")
        self.assertEqual(installer_main.git(self.args.repository, "rev-parse", "HEAD"), expected)
        self.assertTrue((self.docroot / "git/index.cgi").is_file())
        installer_main.run_installer(self.archive, self.args)
        wrapper = self.docroot / "git/index.cgi"
        wrapper.write_text("local custom wrapper\n")
        with self.assertRaisesRegex(ValueError, "left untouched"):
            installer_main.run_installer(self.archive, self.args)
        self.assertEqual(wrapper.read_text(), "local custom wrapper\n")

    def test_corrupt_payload_stops_before_extraction(self):
        altered = self.rewrite_archive(
            lambda name, data: b"X" + data[1:] if name == "runtime/cgit" else data
        )
        with self.assertRaisesRegex(ValueError, "hash mismatch"):
            installer_main.run_installer(altered, self.args)
        self.assertFalse((self.root / "setup").exists())
        self.assertFalse((self.docroot / "git").exists())

    def test_extra_path_is_rejected(self):
        with zipfile.ZipFile(self.archive, "a") as archive:
            archive.writestr("../escape", b"no")
        with self.assertRaisesRegex(ValueError, "Unexpected"):
            installer_main.run_installer(self.archive, self.args)
        self.assertFalse((self.root / "escape").exists())

    def test_existing_empty_repository_directory_is_preserved(self):
        target = Path(self.args.repository)
        target.mkdir(parents=True)
        with self.assertRaises(subprocess.CalledProcessError):
            installer_main.run_installer(self.archive, self.args)
        self.assertEqual(list(target.iterdir()), [])
        self.assertFalse((self.docroot / "git").exists())

    def test_unsupported_architecture_stops_without_writes(self):
        with patch.object(installer_main.platform, "machine", return_value="aarch64"):
            with self.assertRaisesRegex(ValueError, "x86_64"):
                installer_main.run_installer(self.archive, self.args)
        self.assertFalse((self.root / "setup").exists())

    def test_check_only_leaves_repository_and_public_files_absent(self):
        self.args.check_only = True
        installer_main.run_installer(self.archive, self.args)
        self.assertFalse(Path(self.args.repository).exists())
        self.assertFalse((self.docroot / "git").exists())


if __name__ == "__main__":
    unittest.main()
