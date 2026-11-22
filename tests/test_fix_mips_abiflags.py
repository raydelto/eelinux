"""Repair of PT_MIPS_ABIFLAGS segments left empty by patchelf."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("fix_mips_abiflags", ROOT / "userland/scripts/fix-mips-abiflags.py")
fix = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(fix)

ABIFLAGS_OFFSET = 52 + 2 * 32
SHOFF = ABIFLAGS_OFFSET + 24
VADDR = 0x10000


def shared_object(abiflags_size=0, machine=8, section=True):
    """Two program headers (PT_LOAD over the file, PT_MIPS_ABIFLAGS) plus .MIPS.abiflags."""
    shnum = 2 if section else 0
    size = SHOFF + shnum * 40
    header = struct.pack("<16sHHIIIIIHHHHHH", b"\x7fELF\x01\x01\x01" + bytes(9), 3, machine, 1,
                         0, 52, SHOFF if section else 0, 0, 52, 32, 2, 40, shnum, 0)
    load = struct.pack("<8I", fix.PT_LOAD, 0, VADDR, VADDR, size, size, 5, 0x1000)
    # patchelf's damage: the segment keeps a stale offset and has zero size.
    flags = struct.pack("<8I", fix.PT_MIPS_ABIFLAGS, 0x40, 0x40, 0x40,
                        abiflags_size, abiflags_size, 4, 8)
    body = bytes(range(24))
    sections = b""
    if section:
        sections = bytes(40) + struct.pack("<10I", 0, fix.SHT_MIPS_ABIFLAGS, 2, VADDR + ABIFLAGS_OFFSET,
                                           ABIFLAGS_OFFSET, 24, 0, 0, 8, 24)
    return header + load + flags + body + sections


class RepairTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "EUC-JP.so"

    def write(self, data):
        self.path.write_bytes(data)
        return self.path

    def test_empty_segment_points_at_relocated_section(self):
        self.assertTrue(fix.repair(self.write(shared_object())))
        data = self.path.read_bytes()
        segment = struct.unpack_from("<8I", data, 52 + 32)
        self.assertEqual(segment[:6], (fix.PT_MIPS_ABIFLAGS, ABIFLAGS_OFFSET, VADDR + ABIFLAGS_OFFSET,
                                       VADDR + ABIFLAGS_OFFSET, 24, 24))
        self.assertEqual(segment[6:], (4, 8))
        self.assertFalse(fix.repair(self.path))  # idempotent

    def test_intact_object_is_untouched(self):
        original = shared_object(abiflags_size=24)
        self.assertFalse(fix.repair(self.write(original)))
        self.assertEqual(self.path.read_bytes(), original)

    def test_short_and_foreign_files_are_ignored(self):
        for data in (b"", b"\x7fELF\x01", b"not an elf" * 10, shared_object(machine=62)):
            self.assertFalse(fix.repair(self.write(data)))

    def test_missing_section_fails(self):
        with self.assertRaisesRegex(RuntimeError, "no .MIPS.abiflags section"):
            fix.repair(self.write(shared_object(section=False)))

    def test_truncated_header_tables_fail(self):
        with self.assertRaisesRegex(RuntimeError, "section header table is out of bounds"):
            fix.repair(self.write(shared_object()[:-10]))
        with self.assertRaisesRegex(RuntimeError, "program header table is out of bounds"):
            fix.repair(self.write(shared_object()[:60]))


if __name__ == "__main__":
    unittest.main()
