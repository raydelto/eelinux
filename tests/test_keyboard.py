"""UK layout symbols and fail-closed include/payload source boundaries."""
from pathlib import Path
import json
import os
import struct
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import keyboard
from keyboard import map_closure, validate_binary_map


class KeyboardUK(unittest.TestCase):
    def binary(self):
        plain, shift = [0] * 128, [0] * 128
        plain[43], plain[86] = ord("#"), ord("\\")
        shift[3], shift[4], shift[40], shift[86] = ord('"'), 0xA3, ord("@"), ord("|")
        plain[51], plain[52], plain[53] = ord(","), ord("."), ord("/")
        shift[43], shift[51], shift[52], shift[53] = ord("~"), ord("<"), ord(">"), ord("?")
        return b"bkeymap" + bytes([1, 1] + [0] * 254) + struct.pack("<256H", *plain, *shift)

    def test_actual_binary_format_checks_uk_punctuation_and_iso_key(self):
        result = validate_binary_map(self.binary())
        self.assertEqual(result["uk_punctuation"]["modifier1_keycode4"], "0xa3")
        self.assertEqual(result["uk_punctuation"]["modifier0_keycode53"], "0x2f")
        wrong = bytearray(self.binary())
        struct.pack_into("<H", wrong, 263 + 256 + 3 * 2, ord("@"))
        for data in (wrong, self.binary()[:-1], self.binary() + b"extra", b"invalid"):
            with self.subTest(data=data[:7]), self.assertRaises(RuntimeError):
                validate_binary_map(data)

    def test_incorrect_slash_is_rejected(self):
        wrong = bytearray(self.binary())
        struct.pack_into("<H", wrong, 263 + 53 * 2, ord("-"))
        with self.assertRaises(RuntimeError):
            validate_binary_map(wrong)

    def test_ANSI_key_above_enter_maps_to_backslash_and_pipe(self):
        plain, shifted = [0] * 128, [0] * 128
        plain[43], shifted[43] = 0x5c, 0x7c
        shifted[3], shifted[4] = 0x40, 0x23
        plain[40], shifted[40] = 0x27, 0x22
        plain[53], shifted[53] = 0x2f, 0x3f
        data = b'bkeymap' + bytes([1, 1] + [0] * 254) + struct.pack('<256H', *plain, *shifted)
        result = validate_binary_map(data, 'us')
        self.assertEqual(result['us_punctuation']['modifier0_keycode43'], '0x5c')
        self.assertEqual(result['us_punctuation']['modifier1_keycode43'], '0x7c')
        with self.assertRaises(RuntimeError): validate_binary_map(self.binary(), 'us')

    def test_recursive_latin1_includes_keep_exact_upstream_bytes(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary)
            maps = source / "data/keymaps/i386"
            (maps / "qwerty").mkdir(parents=True); (maps / "include").mkdir()
            (maps / "qwerty/uk.map").write_text('include "qwerty-layout"\n')
            content = b'# Rosenkr\xe4nzer\ninclude "compose"\n'
            (maps / "include/qwerty-layout.inc").write_bytes(content)
            (maps / "include/compose.inc").write_text("keycode 99 = Compose\n")
            closure = map_closure(source)
            self.assertEqual(set(closure), {"uk.map", "qwerty-layout.inc", "compose.inc"})
            self.assertEqual(closure["qwerty-layout.inc"].read_bytes(), content)

    def test_missing_ambiguous_recursive_and_parent_includes_rejected(self):
        for scenario in ("missing", "ambiguous", "recursive", "parent"):
            with self.subTest(scenario=scenario), tempfile.TemporaryDirectory() as temporary:
                source = Path(temporary); maps = source / "data/keymaps/i386"
                (maps / "qwerty").mkdir(parents=True); (maps / "include").mkdir()
                (maps / "qwerty/uk.map").write_text('include "layout"\n')
                if scenario != "missing":
                    (maps / "include/layout.inc").write_text('include "uk.map"\n' if scenario == "recursive" else "# layout\n")
                if scenario == "ambiguous":
                    (maps / "qwerty/layout.inc").write_text("# duplicate\n")
                if scenario == "parent":
                    (maps / "qwerty/uk.map").write_text('include "../outside"\n')
                with self.assertRaises(RuntimeError): map_closure(source)

    def install_fixture(self, folder):
        payload, staging, outside = folder / "payload", folder / "rootfs", folder / "outside"
        payload.mkdir(); (staging / "usr/bin").mkdir(parents=True); outside.mkdir()
        lock = keyboard.read_lock()
        lock["files"] = dict(lock["files"])
        for source_name in lock["files"]:
            relative = ("usr/share/licenses/kbd/COPYING" if source_name == "COPYING" else
                        "usr/share/keymaps/" + Path(source_name).name)
            path = payload / relative; path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(("fixture " + source_name).encode())
            lock["files"][source_name] = keyboard.sha(path)
        for layout in lock.get('layouts', {}):
            (payload / ('usr/share/keymaps/eelinux-' + layout + '.map')).write_text('# generated map fixture\n')
        binary = payload / "usr/bin/loadkeys"; binary.parent.mkdir(parents=True)
        binary.write_bytes(b"ELF fixture: path tests mock the separately tested ABI validator")
        existing = staging / "usr/bin/loadkeys"; existing.write_bytes(binary.read_bytes())
        existing.chmod(0o640); os.utime(existing, ns=(1234567890000000000, 1234567890000000000))
        report = {"source_lock_sha256": keyboard.sha(keyboard.LOCK),
                  "recipe_sha256": keyboard.sha(Path(keyboard.__file__)),
                  "files": keyboard.inventory(payload)}
        (payload / keyboard.MANIFEST).write_text(json.dumps(report))
        return payload, staging, outside, lock, existing

    def assert_metadata_preflight_blocks_all_copies(self, kind):
        with tempfile.TemporaryDirectory() as temporary:
            payload, staging, outside, lock, existing = self.install_fixture(Path(temporary))
            parent = staging / "usr/share/eelinux"
            parent.parent.mkdir(parents=True)
            if kind == "parent_symlink": parent.symlink_to(outside, target_is_directory=True)
            elif kind == "parent_file": parent.write_text("existing unrelated file\n")
            else:
                parent.mkdir()
                metadata = parent / "keyboard-uk.json"
                if kind == "metadata_symlink": metadata.symlink_to(outside / "metadata.json")
                else: metadata.mkdir()
            (outside / "metadata.json").write_text("outside sentinel\n")
            before = (existing.read_bytes(), existing.stat().st_mode, existing.stat().st_mtime_ns)
            with (mock.patch.object(keyboard, "read_lock", return_value=lock),
                  mock.patch.object(keyboard, "linux_only"),
                  mock.patch.object(keyboard, "validate_elf"),
                  mock.patch.object(keyboard.shutil, "copyfile") as copy):
                with self.assertRaises(RuntimeError): keyboard.install(payload, staging)
                copy.assert_not_called()
            self.assertEqual((existing.read_bytes(), existing.stat().st_mode, existing.stat().st_mtime_ns), before)
            self.assertEqual((outside / "metadata.json").read_text(), "outside sentinel\n")

    @unittest.skipUnless(os.name == "posix", "requires native symlink semantics")
    def test_metadata_leaf_and_parent_symlinks_rejected_before_any_copy(self):
        for kind in ("metadata_symlink", "parent_symlink"):
            with self.subTest(kind=kind): self.assert_metadata_preflight_blocks_all_copies(kind)

    def test_metadata_directory_and_file_parent_rejected_before_any_copy(self):
        for kind in ("metadata_directory", "parent_file"):
            with self.subTest(kind=kind): self.assert_metadata_preflight_blocks_all_copies(kind)


if __name__ == "__main__":
    unittest.main()
