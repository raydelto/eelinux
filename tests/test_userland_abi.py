"""Reject Linux libraries whose CPU/float ABI cannot run on the PS2."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("eelinux_userland", ROOT / "scripts/userland.py")
USERLAND = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(USERLAND)


def elf_with_abi(*, fp_abi=3, isa_level=3, extension=6, elf_flags=0x20921000):
    # ELF32 little endian, EM_MIPS, o32, PT_MIPS_ABIFLAGS. The ABI structure is
    # the public 24-byte MIPS ABI flags v0 format used by Linux and GNU Binutils.
    header = bytearray(52)
    header[:7] = b"\x7fELF\x01\x01\x01"
    struct.pack_into("<HHI", header, 16, 2, 8, 1)
    struct.pack_into("<I", header, 28, 52)
    struct.pack_into("<I", header, 36, elf_flags)
    struct.pack_into("<HHH", header, 40, 52, 32, 1)
    program = struct.pack("<8I", 0x70000003, 84, 0, 0, 24, 24, 4, 8)
    abi = struct.pack("<H6B4I", 0, isa_level, 0, 1, 0, 0, fp_abi, extension, 0, 0, 0)
    return bytes(header) + program + abi


class UserlandAbiTests(unittest.TestCase):
    def inspect(self, content):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "library.so"
            path.write_bytes(content)
            USERLAND.validate_elf(path)

    def test_r5900_o32_soft_float_is_accepted(self):
        self.inspect(elf_with_abi())

    def test_hard_float_and_fpxx_libraries_are_rejected(self):
        for fp_abi in (1, 2, 5, 6, 7):
            with self.subTest(fp_abi=fp_abi), self.assertRaises(USERLAND.BuildError):
                self.inspect(elf_with_abi(fp_abi=fp_abi))

    def test_generic_mips32_code_is_rejected(self):
        with self.assertRaises(USERLAND.BuildError):
            self.inspect(elf_with_abi(isa_level=32, extension=0, elf_flags=0x50001000))

    def test_incompatible_cpu_extension_is_rejected(self):
        with self.assertRaises(USERLAND.BuildError):
            self.inspect(elf_with_abi(extension=2))

    def test_missing_abi_attributes_are_rejected(self):
        header = bytearray(elf_with_abi()[:52])
        struct.pack_into("<H", header, 44, 0)
        with self.assertRaises(USERLAND.BuildError):
            self.inspect(bytes(header))


if __name__ == "__main__":
    unittest.main()
