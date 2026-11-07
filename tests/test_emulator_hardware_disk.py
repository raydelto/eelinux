"""Verify hardware-image copies using structural fixtures, never physical drives."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import emulator_hardware_disk as hardware
from emulator_disk import DiskImageError, hash_stream


def digest(path):
    with path.open('rb') as stream:
        return hash_stream(stream)


class HardwareDiskCopy(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.tree = Path(self.temporary.name)
        self.rootfs = self.tree / 'rootfs.ext2'
        self.kernel = self.tree / 'kernel.elf'
        self.image = self.tree / 'hardware.img'
        self.evidence = Path(str(self.image) + '.json')
        self.output = self.tree / 'disposable.img'
        # These are structural fixtures, not mountable filesystems or target ELF.
        self.kernel.write_bytes(b'reviewed kernel fixture' * 37)
        with self.rootfs.open('wb') as stream:
            stream.truncate(hardware.PARTITION_BYTES)
            block = bytearray(1024)
            struct.pack_into('<I', block, 4, hardware.PARTITION_BYTES // 1024)
            struct.pack_into('<I', block, 20, 1)
            struct.pack_into('<H', block, 56, 0xEF53)
            struct.pack_into('<I', block, 76, 1)
            struct.pack_into('<III', block, 92, 0, 2, 3)
            stream.seek(1024)
            stream.write(block)
        boot = bytearray(512)
        struct.pack_into('<H', boot, 11, 512)
        boot[13] = 1
        struct.pack_into('<H', boot, 14, 32)
        boot[16] = 2
        struct.pack_into('<III', boot, 28, hardware.FAT_START, hardware.PARTITION_SECTORS, 1024)
        struct.pack_into('<I', boot, 44, 2)
        boot[82:90] = b'FAT32   '
        boot[510:512] = b'\x55\xaa'
        prefix = bytearray(hardware.FAT_START * 512)
        prefix[440:444] = b'eeLX'
        for index, kind, start in ((0, 0x0c, hardware.FAT_START), (1, 0x83, hardware.ROOT_START)):
            struct.pack_into('<B3sB3sII', prefix, 446 + index * 16, 0, b'\0' * 3, kind,
                             b'\0' * 3, start, hardware.PARTITION_SECTORS)
        prefix[510:512] = b'\x55\xaa'
        kernel_clusters = (self.kernel.stat().st_size + 511) // 512
        fat = bytearray(1024 * 512)
        struct.pack_into('<III', fat, 0, 0x0ffffff8, 0x0fffffff, 0x0fffffff)
        for cluster in range(3, 3 + kernel_clusters):
            struct.pack_into('<I', fat, cluster * 4,
                             cluster + 1 if cluster < 2 + kernel_clusters else 0x0fffffff)
        directory = bytearray(512)
        directory[:11] = b'EELINUX ELF'
        directory[11] = 0x20
        struct.pack_into('<H', directory, 26, 3)
        struct.pack_into('<I', directory, 28, self.kernel.stat().st_size)
        self.fat_base = hardware.FAT_START * 512
        self.fat_offset = self.fat_base + 32 * 512
        self.data_offset = self.fat_base + (32 + 2048) * 512
        with self.image.open('wb') as stream:
            stream.truncate(hardware.DISK_BYTES)
            stream.seek(0); stream.write(prefix)
            stream.seek(self.fat_base); stream.write(boot)
            stream.seek(self.fat_offset); stream.write(fat); stream.write(fat)
            stream.seek(self.data_offset); stream.write(directory); stream.write(self.kernel.read_bytes())
            stream.seek(hardware.ROOT_START * 512)
            with self.rootfs.open('rb') as root:
                while chunk := root.read(1024 * 1024):
                    stream.write(chunk)
        self.report = {
            'schema': 1,
            'disk': {'bytes': hardware.DISK_BYTES, 'sha256': digest(self.image)},
            'source_rootfs': {'bytes': hardware.PARTITION_BYTES, 'sha256': digest(self.rootfs)},
            'kernel': {'bytes': self.kernel.stat().st_size, 'sha256': digest(self.kernel),
                       'root_device': '/dev/sda2', 'video': hardware.PAL_VIDEO,
                       'fat_path': 'EELINUX.ELF',
                       'command_line': 'root=/dev/sda2 rootfstype=ext2 video=ps2fb:576x460i@50'},
            'partitions': [
                {'index': 1, 'type': '0x0c', 'start_lba': hardware.FAT_START,
                 'sectors': hardware.PARTITION_SECTORS, 'offset_bytes': self.fat_base},
                {'index': 2, 'type': '0x83', 'start_lba': hardware.ROOT_START,
                 'sectors': hardware.PARTITION_SECTORS, 'offset_bytes': hardware.ROOT_START * 512}]
        }
        self.write_manifest()

    def tearDown(self):
        self.temporary.cleanup()

    def write_manifest(self):
        self.evidence.write_text(json.dumps(self.report))

    def copy(self):
        return hardware.copy_hardware_disk(self.image, self.rootfs, self.kernel, self.output)

    def mutate_disk(self, offset, value):
        with self.image.open('r+b') as stream:
            stream.seek(offset); stream.write(value)
        # An internally consistent manifest must not conceal malformed contents.
        self.report['disk']['sha256'] = digest(self.image)
        self.write_manifest()

    def assert_unpublished(self):
        self.assertFalse(self.output.exists())
        self.assertFalse(Path(str(self.output) + '.json').exists())
        self.assertFalse(list(self.tree.glob('*.tmp')))

    def test_exact_copy_readback_root2_report_and_source_immutability(self):
        before = {path: (digest(path), path.stat().st_mtime_ns)
                  for path in (self.image, self.rootfs, self.kernel, self.evidence)}
        result = self.copy()
        self.assertEqual(digest(self.output), before[self.image][0])
        self.assertEqual(result['disk']['bytes'], hardware.DISK_BYTES)
        self.assertEqual(result['partition']['index'], 2)
        self.assertEqual(result['required_guest_root'], '/dev/sda2')
        self.assertEqual(result['source']['sha256'], before[self.rootfs][0])
        self.assertEqual(json.loads(Path(str(self.output) + '.json').read_text()), result)
        for path, expected in before.items():
            self.assertEqual((digest(path), path.stat().st_mtime_ns), expected)

    def test_manifest_hash_root_video_and_size_mismatches_are_rejected(self):
        mutations = [('disk', 'sha256', '0' * 64), ('disk', 'bytes', 1),
                     ('source_rootfs', 'sha256', '0' * 64), ('kernel', 'sha256', '0' * 64),
                     ('kernel', 'root_device', '/dev/sda1'), ('kernel', 'video', 'NTSC'),
                     ('kernel', 'command_line', 'root=/dev/sda1 video=ps2fb:576x460i@50')]
        for section, key, invalid in mutations:
            with self.subTest(section=section, key=key):
                old = self.report[section][key]
                self.report[section][key] = invalid; self.write_manifest()
                with self.assertRaises(DiskImageError): self.copy()
                self.assert_unpublished()
                self.report[section][key] = old
        self.write_manifest()

    def test_checked_progressive_mode_keeps_exact_manifest_command_line(self):
        self.report['kernel']['video'] = '576P 720x576p@50'
        self.report['kernel']['command_line'] = 'root=/dev/sda2 rootfstype=ext2 video=ps2fb:720x576p@50'
        self.write_manifest()
        result = self.copy()
        self.assertEqual(result['video'], '576P 720x576p@50')
        self.assertEqual(digest(self.output), digest(self.image))

    def test_actual_overlapping_mbr_is_rejected_despite_refreshed_hash(self):
        self.mutate_disk(462 + 8, struct.pack('<I', hardware.FAT_START))
        with self.assertRaisesRegex(DiskImageError, 'actual MBR'): self.copy()
        self.assert_unpublished()

    def test_actual_fat_geometry_and_kernel_readback_are_verified(self):
        self.mutate_disk(self.fat_base + 13, b'\x02')
        with self.assertRaisesRegex(DiskImageError, 'FAT32 BPB'): self.copy()
        self.assert_unpublished()
        self.mutate_disk(self.fat_base + 13, b'\x01')
        self.mutate_disk(self.data_offset + 512, b'wrong kernel')
        with self.assertRaisesRegex(DiskImageError, 'EELINUX.ELF payload'): self.copy()
        self.assert_unpublished()

    def test_actual_ext2_payload_is_verified(self):
        self.mutate_disk(hardware.ROOT_START * 512 + 4096, b'changed root')
        with self.assertRaisesRegex(DiskImageError, 'ext2 partition payload'): self.copy()
        self.assert_unpublished()

    def test_existing_output_manifest_and_aliases_are_preserved(self):
        self.output.write_bytes(b'keep existing')
        with self.assertRaises(DiskImageError): self.copy()
        self.assertEqual(self.output.read_bytes(), b'keep existing')
        self.output.unlink()
        manifest = Path(str(self.output) + '.json'); manifest.write_text('keep existing')
        with self.assertRaises(DiskImageError): self.copy()
        self.assertEqual(manifest.read_text(), 'keep existing'); manifest.unlink()
        with self.assertRaises(DiskImageError):
            hardware.copy_hardware_disk(self.image, self.rootfs, self.kernel, self.image)
        alias = self.tree / 'kernel-hardlink'
        try: os.link(self.kernel, alias)
        except (OSError, NotImplementedError): self.skipTest('hard links unavailable')
        with self.assertRaises(DiskImageError): self.copy()
        self.assert_unpublished()

    def test_symlink_and_devices_are_rejected(self):
        with self.assertRaises((OSError, DiskImageError)):
            hardware.copy_hardware_disk(Path(os.devnull), self.rootfs, self.kernel, self.output)
        alias = self.tree / 'linked-image.img'
        try: alias.symlink_to(self.image)
        except (OSError, NotImplementedError): self.skipTest('symlinks unavailable')
        with self.assertRaises(DiskImageError):
            hardware.copy_hardware_disk(alias, self.rootfs, self.kernel, self.output)
        self.assert_unpublished()

    def test_mid_copy_input_change_prevents_publication(self):
        original = hardware.hash_stream
        calls = 0
        def mutate_on_recheck(stream):
            nonlocal calls
            calls += 1
            if calls == 5:  # Final image recheck after all four initial hashes.
                with self.image.open('r+b') as target:
                    target.seek(100); target.write(b'changed')
            return original(stream)
        with patch.object(hardware, 'hash_stream', mutate_on_recheck):
            with self.assertRaisesRegex(DiskImageError, 'changed'): self.copy()
        self.assert_unpublished()


if __name__ == '__main__':
    unittest.main()
