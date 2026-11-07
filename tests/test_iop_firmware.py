"""IOP IRX packaging must not weaken Linux userspace ABI checks."""
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
from iop_firmware import FirmwareError, MODULES, validate_firmware, validate_irx
import rootfs
import userland
from test_runtime import small_elf


def irx_fixture() -> bytes:
    # Structural fixture only; never load or execute it as firmware.
    header = struct.pack("<16sHHIIIIIHHHHHH", b"\x7fELF\x01\x01\x01" + b"\0" * 9,
                         0xFF80, 8, 1, 0, 52, 160, 1, 52, 32, 2, 40, 3, 0)
    programs = struct.pack("<8I", 0x70000080, 132, 0, 0, 26, 0, 4, 4)
    programs += struct.pack("<8I", 1, 128, 0, 0, 4, 4, 5, 16)
    sections = b"\0" * 40
    sections += struct.pack("<10I", 0, 1, 6, 0, 128, 4, 0, 0, 4, 0)
    sections += struct.pack("<10I", 0, 0x70000080, 0, 0, 132, 26, 0, 0, 4, 0)
    return (header + programs).ljust(160, b"\0") + sections


class IopFirmwarePackaging(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.tree = Path(self.temporary.name)
        self.folder = self.tree / "lib/firmware/ps2"
        self.folder.mkdir(parents=True)
        self.hashes = {}
        for name in MODULES:
            filename = name + ".irx"
            data = irx_fixture()
            (self.folder / filename).write_bytes(data)
            self.hashes[filename] = hashlib.sha256(data).hexdigest()
        self.manifest = self.folder / "manifest.json"
        self.manifest.write_text(json.dumps({"firmware_sha256": self.hashes}))

    def tearDown(self):
        self.temporary.cleanup()

    def populate_linux_runtime(self):
        # Keep discovery and ELF validation real: the exception is for the six
        # hash-checked IRX files, never for ordinary Linux commands or libraries.
        for name in set(rootfs.GNU_COMMANDS + rootfs.SYSTEM_COMMANDS):
            path = self.tree / "bin" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(small_elf())
            path.chmod(0o755)
        for relative in (
            "usr/bin/coreutils", "usr/bin/find", "usr/bin/diff", "usr/bin/gawk",
            "sbin/init", "sbin/telinit", "sbin/runlevel", "sbin/ip", "sbin/dhcpcd",
            "usr/bin/nano", "usr/bin/hostname", "usr/bin/getent",
            "usr/lib/eelinux/fbcheck", "usr/sbin/ps2-video", "lib/libc.so.6",
        ):
            path = self.tree / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(small_elf())
            path.chmod(0o755)
        for name in rootfs.TOOL_COVERAGE["gnu"]["coreutils"].split():
            path = self.tree / "bin" / name
            path.unlink()
            path.symlink_to("../usr/bin/coreutils")

    @unittest.skipIf(os.name == "nt", "requires POSIX command modes and symlinks")
    def test_userland_verifier_accepts_manifest_checked_iop(self):
        self.populate_linux_runtime()
        userland.verify_target(self.tree, "base")

    @unittest.skipIf(os.name == "nt", "requires POSIX command modes and symlinks")
    def test_userland_verifier_rejects_irx_outside_firmware_directory(self):
        self.populate_linux_runtime()
        (self.tree / "bin/foreign.irx").write_bytes(irx_fixture())
        with self.assertRaisesRegex(userland.BuildError, "incompatible MIPS ELF"):
            userland.verify_target(self.tree, "base")

    @unittest.skipIf(os.name == "nt", "requires POSIX command modes and symlinks")
    def test_userland_verifier_requires_valid_firmware_manifest(self):
        self.populate_linux_runtime()
        self.manifest.unlink()
        with self.assertRaisesRegex(userland.BuildError, "manifest is required"):
            userland.verify_target(self.tree, "base")
        self.manifest.write_text(json.dumps({"firmware_sha256": self.hashes}))
        (self.folder / "ata.irx").write_bytes(irx_fixture() + b"tampered")
        with self.assertRaisesRegex(userland.BuildError, "hash mismatch"):
            userland.verify_target(self.tree, "base")

    @unittest.skipIf(os.name == "nt", "staging command checks require POSIX modes")
    def test_iop_firmware_packages_without_relaxing_userspace_abi(self):
        (self.tree / "bin").mkdir()
        for name in rootfs.GNU_COMMANDS + rootfs.SYSTEM_COMMANDS:
            path = self.tree / "bin" / name
            path.write_bytes(small_elf())
            path.chmod(0o755)
        (self.tree / "lib/libc.so.6").write_bytes(small_elf())
        rootfs.check_staging(self.tree, desktop=False)
        with self.assertRaises(rootfs.BuildError):
            rootfs.elf_info(self.folder / "dev9.irx")
        # The same IRX outside the explicit firmware location must still fail.
        (self.tree / "bin/foreign").write_bytes(irx_fixture())
        with self.assertRaises(rootfs.BuildError):
            rootfs.check_staging(self.tree, desktop=False)

    def test_missing_manifest_or_tampering_rejects_firmware(self):
        self.manifest.unlink()
        with self.assertRaises(FirmwareError):
            validate_firmware(self.folder)
        self.manifest.write_text(json.dumps({"firmware_sha256": self.hashes}))
        (self.folder / "dev9.irx").write_bytes(irx_fixture() + b"tampered")
        with self.assertRaisesRegex(FirmwareError, "hash mismatch"):
            validate_firmware(self.folder)

    def test_wrong_cpu_or_malformed_irx_cannot_be_approved_by_its_hash(self):
        path = self.folder / "dev9.irx"
        for offset, fmt, value in ((16, "<H", 2), (36, "<I", 0x70000001),
                                   (32, "<I", 0xFFFFFFF0), (104, "<I", 0x400000)):
            data = bytearray(irx_fixture())
            struct.pack_into(fmt, data, offset, value)
            path.write_bytes(data)
            self.hashes[path.name] = hashlib.sha256(data).hexdigest()
            self.manifest.write_text(json.dumps({"firmware_sha256": self.hashes}))
            with self.subTest(offset=offset), self.assertRaises(FirmwareError):
                validate_firmware(self.folder)


if __name__ == "__main__":
    unittest.main()
