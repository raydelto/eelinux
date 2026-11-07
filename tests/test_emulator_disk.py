"""Disposable emulator images must preserve the verified source filesystem."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from emulator_disk import DiskImageError, PARTITION_OFFSET, wrap_rootfs


def ext2_fixture() -> bytes:
    """A superblock/size fixture, not a mountable filesystem or boot artifact."""
    data = bytearray(8192)
    struct.pack_into("<I", data, 1024 + 4, 8)
    struct.pack_into("<I", data, 1024 + 20, 1)
    struct.pack_into("<H", data, 1024 + 56, 0xEF53)
    struct.pack_into("<I", data, 1024 + 76, 1)
    struct.pack_into("<III", data, 1024 + 92, 0, 2, 3)
    data[4096:4112] = b"source payload!!"
    return bytes(data)


class EmulatorDiskPackaging(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.tree = Path(self.temporary.name)
        self.source = self.tree / "rootfs.ext2"
        self.output = self.tree / "test.img"
        self.data = ext2_fixture()
        self.source.write_bytes(self.data)

    def tearDown(self):
        self.temporary.cleanup()

    def test_mbr_payload_hash_and_source_immutability(self):
        before = self.source.stat()
        report = wrap_rootfs(self.source, self.output)
        disk = self.output.read_bytes()
        self.assertEqual(disk[510:512], b"\x55\xaa")
        self.assertEqual(disk[440:444], b"eeLX")
        status, _, kind, _, start, sectors = struct.unpack_from("<B3sB3sII", disk, 446)
        self.assertEqual((status, kind, start, sectors), (0, 0x83, 2048, 16))
        self.assertEqual(disk[462:510], b"\0" * 48)
        self.assertEqual(disk[PARTITION_OFFSET:], self.data)
        self.assertEqual(len(disk), PARTITION_OFFSET + len(self.data))
        self.assertEqual(self.source.read_bytes(), self.data)
        self.assertEqual((self.source.stat().st_size, self.source.stat().st_mtime_ns), (before.st_size, before.st_mtime_ns))
        self.assertEqual(report["source"]["sha256"], hashlib.sha256(self.data).hexdigest())
        self.assertEqual(report["disk"]["sha256"], hashlib.sha256(disk).hexdigest())
        self.assertEqual(json.loads(Path(str(self.output) + ".json").read_text()), report)

    def test_disk_bytes_are_deterministic_and_existing_regular_copy_can_be_replaced(self):
        wrap_rootfs(self.source, self.output)
        first = self.output.read_bytes()
        self.output.write_bytes(b"emulator modified disposable copy")
        wrap_rootfs(self.source, self.output)
        other = self.tree / "nested/other.img"
        wrap_rootfs(self.source, other)
        self.assertEqual(self.output.read_bytes(), first)
        self.assertEqual(other.read_bytes(), first)
        self.assertFalse(list(self.tree.rglob("*.tmp")))

    def test_source_with_preserved_old_modification_time_remains_unchanged(self):
        # Copied release artifacts need not have their creation time as mtime.
        os.utime(self.source, ns=(1000000000000000000, 1000000000000000000))
        before = self.source.stat().st_mtime_ns
        wrap_rootfs(self.source, self.output)
        self.assertEqual(self.source.stat().st_mtime_ns, before)
        self.assertEqual(self.source.read_bytes(), self.data)

    def test_invalid_ext2_inputs_leave_existing_output_unchanged(self):
        wrong_magic = bytearray(self.data)
        struct.pack_into("<H", wrong_magic, 1080, 0)
        wrong_blocks = bytearray(self.data)
        struct.pack_into("<I", wrong_blocks, 1028, 9)
        ext4 = bytearray(self.data)
        struct.pack_into("<I", ext4, 1120, 0x42)
        journaled = bytearray(self.data)
        struct.pack_into("<I", journaled, 1116, 4)
        huge_block = bytearray(self.data)
        struct.pack_into("<I", huge_block, 1048, 0xFFFFFFFF)
        for name, contents in (("magic", wrong_magic), ("block count", wrong_blocks), ("unaligned", self.data + b"!"), ("truncated", self.data[:1536]), ("ext4", ext4), ("journal", journaled), ("block size", huge_block)):
            with self.subTest(name=name):
                self.source.write_bytes(contents)
                self.output.write_bytes(b"keep this old copy")
                with self.assertRaises(DiskImageError):
                    wrap_rootfs(self.source, self.output)
                self.assertEqual(self.output.read_bytes(), b"keep this old copy")
                self.assertFalse(Path(str(self.output) + ".json").exists())

    def test_source_output_manifest_aliases_and_hard_links_are_rejected(self):
        for output, manifest in ((self.source, None), (self.output, self.source), (self.output, self.output)):
            with self.subTest(output=output, manifest=manifest), self.assertRaises(DiskImageError):
                wrap_rootfs(self.source, output, manifest)
        linked = self.tree / "hard-link.img"
        try:
            os.link(self.source, linked)
        except (OSError, NotImplementedError):
            self.skipTest("hard links unavailable")
        with self.assertRaises(DiskImageError):
            wrap_rootfs(self.source, linked)
        self.assertEqual(self.source.read_bytes(), self.data)

    def test_ext4_features_and_journal_are_verified(self):
        data = bytearray(self.data)
        struct.pack_into('<III', data, 1024 + 0x5C, 0x24, 0x42, 0x443)
        struct.pack_into('<H', data, 1024 + 0x58, 256)
        struct.pack_into('<I', data, 1024 + 0xE0, 8)
        self.source.write_bytes(data)
        self.assertEqual(wrap_rootfs(self.source, self.output)['filesystem']['type'], 'ext4')
        for offset, value in ((0x60, 0xC2), (0x64, 0x643), (0xE0, 0), (0xE4, 1)):
            with self.subTest(offset=offset):
                changed = bytearray(data)
                struct.pack_into('<I', changed, 1024 + offset, value)
                self.source.write_bytes(changed)
                with self.assertRaisesRegex(DiskImageError, 'ext4 profile'):
                    wrap_rootfs(self.source, self.output)

    def test_directories_devices_and_device_namespaces_are_rejected(self):
        for source, output in ((self.tree, self.output), (self.source, self.tree), (Path(os.devnull), self.output), (self.source, Path(os.devnull)), (self.source, Path(r"\\.\PhysicalDrive0"))):
            with self.subTest(source=source, output=output), self.assertRaises((DiskImageError, OSError)):
                wrap_rootfs(source, output)

    def test_symlinks_in_inputs_outputs_and_ancestors_are_rejected(self):
        linked = self.tree / "link"
        try:
            linked.symlink_to(self.source)
        except (OSError, NotImplementedError):
            self.skipTest("symlinks unavailable")
        for source, output, manifest in ((linked, self.output, None), (self.source, linked, None), (self.source, self.output, linked)):
            with self.subTest(source=source, output=output), self.assertRaises(DiskImageError):
                wrap_rootfs(source, output, manifest)
        directory_link = self.tree / "linked-directory"
        directory_link.symlink_to(self.tree, target_is_directory=True)
        with self.assertRaises(DiskImageError):
            wrap_rootfs(self.source, directory_link / "test.img")
        self.assertEqual(self.source.read_bytes(), self.data)


if __name__ == "__main__":
    unittest.main()
