"""Independent boot archive and source extraction regression tests."""
from __future__ import annotations

import hashlib
import importlib.util
import io
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "scripts"))
import common
import initramfs


class ConsoleArchive(unittest.TestCase):
    def test_console_metadata_exists_without_a_host_device(self):
        with tempfile.TemporaryDirectory() as temporary:
            tree = Path(temporary)
            payload = initramfs.newc(tree, console=True)
            offset, entries = 0, {}
            while True:
                header = payload[offset:offset + 110]
                self.assertEqual(header[:6], b"070701")
                fields = [int(header[i:i + 8], 16) for i in range(6, 110, 8)]
                start = offset + 110
                name = payload[start:start + fields[11] - 1].decode()
                entries[name] = fields
                offset = (start + fields[11] + 3) & ~3
                offset = (offset + fields[6] + 3) & ~3
                if name == "TRAILER!!!":
                    break
            console = entries["dev/console"]
            self.assertTrue(stat.S_ISCHR(console[1]))
            self.assertEqual(console[1] & 0o777, 0o600)
            self.assertEqual((console[2], console[3], console[9], console[10]), (0, 0, 5, 1))
            self.assertFalse((tree / "dev").exists())
            self.assertEqual(payload, initramfs.newc(tree, console=True))

    def test_console_cannot_be_replaced_with_a_regular_file(self):
        with tempfile.TemporaryDirectory() as temporary:
            tree = Path(temporary)
            (tree / "dev").mkdir()
            (tree / "dev/console").write_bytes(b"wrong stdio")
            with self.assertRaisesRegex(RuntimeError, "archive-only device"):
                initramfs.newc(tree, console=True)


class BuildInputSafety(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.folder = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def archive(self, name, members):
        path = self.folder / name
        with tarfile.open(path, "w") as archive:
            for member, data in members:
                archive.addfile(member, io.BytesIO(data) if data is not None else None)
        return path

    def test_source_archive_traversal_and_devices_are_rejected(self):
        for name, kind in (("../outside", tarfile.REGTYPE), ("/absolute", tarfile.REGTYPE), ("dev/console", tarfile.CHRTYPE)):
            with self.subTest(name=name):
                member = tarfile.TarInfo(name)
                member.type = kind
                member.size = 0
                archive = self.archive("malformed.tar", [(member, b"")])
                with self.assertRaises((RuntimeError, tarfile.TarError)):
                    common.extract(archive, self.folder / "unpack")
                self.assertFalse((self.folder / "outside").exists())

    @unittest.skipIf(os.name == "nt", "requires POSIX symlink extraction")
    def test_chained_symlinks_cannot_escape_extraction_root(self):
        alias = tarfile.TarInfo("alias")
        alias.type, alias.linkname = tarfile.SYMTYPE, "."
        escape = tarfile.TarInfo("alias/inside")
        escape.type, escape.linkname = tarfile.SYMTYPE, ".."
        payload = tarfile.TarInfo("inside/outside")
        payload.size = 8
        archive = self.archive("chain.tar", [(alias, None), (escape, None), (payload, b"escaped!")])
        with self.assertRaises((RuntimeError, tarfile.TarError)):
            common.extract(archive, self.folder / "unpack")
        self.assertFalse((self.folder / "outside").exists())

    def test_source_cache_rejects_corruption_without_overwriting_it(self):
        cache = self.folder / "cached.tar"
        cache.write_bytes(b"corrupt cache")
        with self.assertRaisesRegex(RuntimeError, "Checksum mismatch"):
            common.download("https://example.invalid/source", cache, hashlib.sha256(b"real source").hexdigest())
        self.assertEqual(cache.read_bytes(), b"corrupt cache")

    def test_bare_metal_compiler_is_rejected_before_linking(self):
        with patch.object(common, "run", return_value=subprocess.CompletedProcess([], 0, "mips64r5900el-ps2-elf\n", "")) as run:
            with self.assertRaisesRegex(RuntimeError, "not a Linux compiler"):
                common.probe_linux_compiler("ee-", self.folder / "probe")
            self.assertEqual(run.call_count, 1)


@unittest.skipUnless(sys.platform == "linux" and shutil.which("cpio"), "requires Linux cpio")
class InitramfsArchive(unittest.TestCase):
    def test_kernel_newc_archive_roundtrips_without_host_uid_or_timestamp(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            tree, extracted = folder / "tree", folder / "extracted"
            (tree / "etc").mkdir(parents=True)
            extracted.mkdir()
            (tree / "init").write_text("#!/bin/sh\nexec /sbin/init\n")
            (tree / "init").chmod(0o755)
            (tree / "etc/eelinux-root").write_text("/dev/sda1\n")
            (tree / "root-device").symlink_to("etc/eelinux-root")
            first = initramfs.newc(tree)
            os.utime(tree / "init", (1700000000, 1700000000))
            self.assertEqual(first, initramfs.newc(tree))
            result = subprocess.run(["cpio", "-id", "--no-absolute-filenames", "--quiet"],
                                    input=first, cwd=extracted, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            self.assertEqual((extracted / "init").read_text(), (tree / "init").read_text())
            self.assertEqual((extracted / "init").stat().st_mode & 0o777, 0o755)
            self.assertEqual(os.readlink(extracted / "root-device"), "etc/eelinux-root")
            # Inspect the first newc header as an independent metadata contract.
            header = first[:110]
            self.assertEqual(header[:6], b"070701")
            self.assertEqual((int(header[22:30], 16), int(header[30:38], 16), int(header[46:54], 16)), (0, 0, 0))
            self.assertEqual(len(first) % 512, 0)


@unittest.skipUnless(sys.platform == "linux" and shutil.which("cc"), "requires Linux C compiler")
class BootSafety(unittest.TestCase):
    def test_initramfs_program_refuses_to_reclaim_a_normal_host_process_root(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / "init"
            compile_result = subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                                             str(REPO / "boot/early-root.c"), "-o", str(binary)], capture_output=True, text=True)
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=2)
            self.assertEqual(result.returncode, 1)
            self.assertIn("must run as PID 1", result.stderr)


if __name__ == "__main__":
    unittest.main()
