"""Boot preparation must preserve source artifacts and never imply a boot pass."""
from __future__ import annotations

import argparse
import contextlib
import io
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import emulator
from test_emulator_disk import ext2_fixture


def boot_elf(address=0x800000, *, paddr=None, entry=None, memory=256, filesz=16):
    ident = b"\x7fELF\x01\x01\x01" + b"\0" * 9
    header = struct.pack("<16sHHIIIIIHHHHHH", ident, 2, 8, 1,
                         address if entry is None else entry, 52, 0,
                         0x20921101, 52, 32, 1, 40, 0, 0)
    program = struct.pack("<8I", 1, 84, address, address if paddr is None else paddr,
                          filesz, memory, 7, 4)
    return header + program + b"\0" * filesz


class EmulatorSession(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.tree = Path(self.temporary.name)
        self.kernel = self.tree / "input.elf"
        self.kernel.write_bytes(boot_elf())
        self.rootfs = self.tree / "rootfs.ext2"
        self.rootfs.write_bytes(ext2_fixture())
        self.args = argparse.Namespace(kernel=self.kernel, rootfs=self.rootfs,
                                       directory=self.tree / "session", audit_kernel=True,
                                       host="windows" if os.name == "nt" else "linux", pcsx2=None)

    def tearDown(self):
        self.temporary.cleanup()

    def prepare(self):
        with contextlib.redirect_stdout(io.StringIO()):
            return emulator.prepare(self.args)

    def runtime(self):
        return {"program": self.args.pcsx2, "arguments": ["-portable"],
                "profile": self.args.directory / "pcsx2-eeLinux.ini", "environment": {},
                "log_file": self.args.directory / "pcsx2-profile/PCSX2/logs/emulog.txt",
                "bios_configured": True, "mode": "portable runtime copy",
                "capabilities": {"version": "PCSX2 v2.6.3", "options": ["-portable", "-elf"]}}

    def test_real_physical_loader_and_kseg_layouts_are_accepted(self):
        for address in (0x00800000, 0x80010000, 0xA0010000):
            with self.subTest(address=address):
                self.kernel.write_bytes(boot_elf(address, paddr=address & 0x1FFFFFFF))
                emulator.validate_kernel(self.kernel)

    def test_invalid_ram_mapping_and_unbacked_entry_are_rejected(self):
        for description, data in (
            ("MMIO", boot_elf(0x1F000000)),
            ("over 32 MiB", boot_elf(0x01FFFFF0, memory=256)),
            ("physical disagreement", boot_elf(paddr=0x900000)),
            ("BSS entry", boot_elf(entry=0x800040)),
            ("unaligned entry", boot_elf(entry=0x800001)),
            ("no load memory", boot_elf(memory=0)),
            ("filesz exceeds memory", boot_elf(memory=8)),
            ("truncated file", boot_elf()[:-1]),
            ("truncated headers", boot_elf()[:64]),
        ):
            with self.subTest(description=description), self.assertRaises(ValueError):
                self.kernel.write_bytes(data)
                emulator.validate_kernel(self.kernel)

    def test_wrong_isa_dynamic_loads_and_malformed_sections_are_rejected(self):
        bad_isa = bytearray(boot_elf())
        struct.pack_into("<I", bad_isa, 36, 0x70921001)  # MIPS32R2
        dynamic = bytearray(boot_elf())
        struct.pack_into("<I", dynamic, 52, 2)
        sections = bytearray(boot_elf())
        struct.pack_into("<I", sections, 32, 90)
        struct.pack_into("<H", sections, 48, 2)
        overlapping = bytearray(boot_elf())
        struct.pack_into("<H", overlapping, 44, 2)
        struct.pack_into("<I", overlapping, 56, 116)
        overlapping[84:84] = struct.pack("<8I", 1, 116, 0xA0800000, 0x800000, 16, 256, 7, 4)
        for data in (bad_isa, dynamic, sections, overlapping):
            with self.subTest(data=data[:52]), self.assertRaises(ValueError):
                self.kernel.write_bytes(data)
                emulator.validate_kernel(self.kernel)

    def test_session_preserves_sources_records_unperformed_boot_and_refuses_overwrite(self):
        before = (self.kernel.read_bytes(), self.rootfs.read_bytes())
        report = self.prepare()
        self.assertEqual((self.kernel.read_bytes(), self.rootfs.read_bytes()), before)
        self.assertEqual(report["required_guest_root"], "/dev/sda1")
        self.assertEqual(set(report["milestones"].values()), {"not performed"})
        self.assertFalse(report["stock_pcsx2_linux_mmu_qualified"])
        sentinel = self.args.directory / "keep.txt"
        sentinel.write_text("keep this experiment")
        with self.assertRaisesRegex(ValueError, "new empty"):
            self.prepare()
        self.assertEqual(sentinel.read_text(), "keep this experiment")

    def test_optical_session_has_disc_argument_and_no_usb_storage(self):
        disc = self.tree / 'source.iso'
        data = bytearray(20 * 2048)
        data[16 * 2048:16 * 2048 + 7] = b'\x01CD001\x01'
        disc.write_bytes(data)
        self.args.dvd_image = disc
        self.args.rootfs = None
        report = self.prepare()
        self.assertEqual(disc.read_bytes(), data)
        self.assertEqual(report['required_guest_root'], '/dev/ps2dvd')
        self.assertFalse(report['USB_storage_attached'])
        self.assertTrue(report['optical_root'])
        self.assertEqual(report['disposable_disk'], 'disc.iso')
        config = (self.args.directory / report['preset']).read_text()
        self.assertNotIn('Type = Msd', config)
        self.assertIn('Type = hidkbd', config)
        self.args.pcsx2 = self.tree / 'pcsx2'
        self.args.pcsx2.write_bytes(b'dummy never executed')
        with patch.object(emulator, 'prepare_runtime', return_value=self.runtime()), \
                patch.object(emulator, 'run_process', return_value=argparse.Namespace(returncode=0)) as run, \
                contextlib.redirect_stdout(io.StringIO()):
            emulator.launch(self.args)
        self.assertIn(str((self.args.directory / 'disc.iso').absolute()), run.call_args.args[0])

    def test_launch_rejects_changed_disk_or_kernel_before_starting_process(self):
        self.prepare()
        executable = self.tree / "pcsx2"
        executable.write_bytes(b"dummy never executed")
        self.args.pcsx2 = executable
        disk = self.args.directory / "rootfs-usb.img"
        disk.write_bytes(b"changed emulator disk")
        with patch.object(emulator, "run_process") as run:
            with self.assertRaisesRegex(ValueError, "disk changed"):
                emulator.launch(self.args)
            run.assert_not_called()

    def test_process_exit_does_not_become_a_linux_boot_result(self):
        self.prepare()
        executable = self.tree / "pcsx2"
        executable.write_bytes(b"dummy never executed")
        self.args.pcsx2 = executable
        with patch.object(emulator, "prepare_runtime", return_value=self.runtime()), \
                patch.object(emulator, "run_process", return_value=argparse.Namespace(returncode=0)) as run:
            with contextlib.redirect_stdout(io.StringIO()):
                emulator.launch(self.args)
            self.assertIn("-elf", run.call_args.args[0])
            self.assertNotIn("--elf", run.call_args.args[0])
            self.assertNotIn("-gamecfg", run.call_args.args[0])
            self.assertIn("-portable", run.call_args.args[0])
            self.assertNotIn("-logfile", run.call_args.args[0])
        report = json.loads((self.args.directory / "process-result.json").read_text())
        self.assertEqual(report["pcsx2_exit_code"], 0)
        self.assertIsNone(report["linux_boot_pass"])
        self.assertEqual(report["log_file"], "pcsx2-profile/PCSX2/logs/emulog.txt")
        self.assertEqual(report["log_mode"], "normal")

    def test_debug_log_is_opt_in_and_does_not_persist_into_normal_launch(self):
        self.prepare()
        self.args.pcsx2 = self.tree / "pcsx2"
        self.args.pcsx2.write_bytes(b"dummy never executed")
        for debug_log in (True, False):
            with self.subTest(debug_log=debug_log):
                self.args.debug_log = debug_log
                with patch.object(emulator, "prepare_runtime", return_value=self.runtime()), \
                        patch.object(emulator, "run_process", return_value=argparse.Namespace(returncode=0)) as run:
                    with contextlib.redirect_stdout(io.StringIO()):
                        emulator.launch(self.args)
                command = run.call_args.args[0]
                saved = json.loads((self.args.directory / "session.json").read_text())
                result = json.loads((self.args.directory / "process-result.json").read_text())
                expected = "pcsx2-boot.log" if debug_log else "pcsx2-profile/PCSX2/logs/emulog.txt"
                self.assertEqual(saved["log_file"], expected)
                self.assertEqual(result["log_file"], expected)
                self.assertEqual(result["log_mode"], "debug" if debug_log else "normal")
                self.assertEqual("-logfile" in command, debug_log)
                if debug_log:
                    self.assertEqual(command[command.index("-logfile") + 1], str(self.args.directory / expected))
                self.assertEqual(saved["pcsx2_arguments"], command[1:])

    def test_failed_emulator_exit_is_reported_as_launcher_failure(self):
        self.prepare()
        executable = self.tree / "pcsx2"
        executable.write_bytes(b"dummy never executed")
        self.args.pcsx2 = executable
        with patch.object(emulator, "prepare_runtime", return_value=self.runtime()), \
                patch.object(emulator, "run_process", return_value=argparse.Namespace(returncode=1)):
            with contextlib.redirect_stdout(io.StringIO()), self.assertRaisesRegex(RuntimeError, "exited with code 1"):
                emulator.launch(self.args)
        report = json.loads((self.args.directory / "process-result.json").read_text())
        self.assertEqual(report["pcsx2_exit_code"], 1)
        self.assertIsNone(report["linux_boot_pass"])

    def test_iop_recompiler_is_explicit_and_reported_without_boot_qualification(self):
        prepared = self.prepare()
        self.assertEqual(prepared["iop_cpu_mode"], "interpreter")
        self.assertIn("completeness unverified", prepared["kernel_kind"])
        self.assertNotIn("without", prepared["kernel_kind"])
        self.args.pcsx2 = self.tree / "pcsx2"
        self.args.pcsx2.write_bytes(b"dummy never executed")
        for enabled in (True, False):
            with self.subTest(iop_recompiler=enabled):
                self.args.iop_recompiler = enabled
                with patch.object(emulator, "prepare_runtime", return_value=self.runtime()) as prepare_runtime, \
                        patch.object(emulator, "run_process", return_value=argparse.Namespace(returncode=0)):
                    with contextlib.redirect_stdout(io.StringIO()):
                        emulator.launch(self.args)
                self.assertEqual(prepare_runtime.call_args.kwargs["iop_recompiler"], enabled)
                saved = json.loads((self.args.directory / "session.json").read_text())
                result = json.loads((self.args.directory / "process-result.json").read_text())
                self.assertEqual(saved["iop_cpu_mode"], "recompiler" if enabled else "interpreter")
                self.assertEqual(result["iop_cpu_mode"], saved["iop_cpu_mode"])
                self.assertEqual(result["ee_cpu_mode"], "interpreter")
                self.assertIsNone(result["linux_boot_pass"])

    def test_renderer_choice_persists_when_launch_option_is_omitted(self):
        self.prepare()
        self.args.pcsx2 = self.tree / "pcsx2"
        self.args.pcsx2.write_bytes(b"dummy never executed")
        for choice, expected in (("software", "software"), (None, "software"),
                                 ("opengl", "opengl"), (None, "opengl")):
            with self.subTest(choice=choice, expected=expected):
                self.args.gs_renderer = choice
                with patch.object(emulator, "prepare_runtime", return_value=self.runtime()) as prepare_runtime, \
                        patch.object(emulator, "run_process", return_value=argparse.Namespace(returncode=0)), \
                        contextlib.redirect_stdout(io.StringIO()):
                    emulator.launch(self.args)
                self.assertEqual(prepare_runtime.call_args.kwargs["gs_renderer"], expected)
                saved = json.loads((self.args.directory / "session.json").read_text())
                result = json.loads((self.args.directory / "process-result.json").read_text())
                self.assertEqual(saved["gs_renderer"], expected)
                self.assertEqual(result["gs_renderer"], expected)

    def test_invalid_saved_renderer_is_rejected_before_process_launch(self):
        report = self.prepare()
        report["gs_renderer"] = "invalid"
        (self.args.directory / "session.json").write_text(json.dumps(report))
        with patch.object(emulator, "run_process") as run, \
                self.assertRaisesRegex(ValueError, "invalid session GS renderer"):
            emulator.launch(self.args)
        run.assert_not_called()

    def test_interactive_timeout_persists_without_changing_boot_qualification(self):
        self.prepare()
        self.args.pcsx2 = self.tree / "pcsx2"
        self.args.pcsx2.write_bytes(b"dummy never executed")
        for choice, expected in ((0, None), (None, None), (30, 30), (None, 30)):
            with self.subTest(choice=choice, expected=expected):
                self.args.timeout = choice
                with patch.object(emulator, "prepare_runtime", return_value=self.runtime()), \
                        patch.object(emulator, "run_process", return_value=argparse.Namespace(returncode=0)) as run, \
                        contextlib.redirect_stdout(io.StringIO()):
                    emulator.launch(self.args)
                self.assertEqual(run.call_args.kwargs["timeout"], expected)
                result = json.loads((self.args.directory / "process-result.json").read_text())
                self.assertEqual(result["timeout_seconds"], expected or 0)
                self.assertIsNone(result["linux_boot_pass"])

    def test_config_check_migrates_legacy_session_without_booting(self):
        report = self.prepare()
        report["pcsx2_arguments"] = ["-gamecfg", "old.ini"]
        (self.args.directory / "session.json").write_text(json.dumps(report))
        self.args.pcsx2 = self.tree / "pcsx2"
        self.args.pcsx2.write_bytes(b"dummy never executed")
        self.args.check_config = True
        with patch.object(emulator, "prepare_runtime", return_value=self.runtime()), \
                patch.object(emulator, "run_process", return_value=argparse.Namespace(returncode=0)) as run:
            with contextlib.redirect_stdout(io.StringIO()):
                emulator.launch(self.args)
        command = run.call_args.args[0]
        self.assertIn("-testconfig", command)
        self.assertIn("-logfile", command)
        self.assertNotIn("-elf", command)
        self.assertNotIn("-gamecfg", command)
        self.assertEqual(run.call_args.kwargs["timeout"], 15)
        saved = json.loads((self.args.directory / "session.json").read_text())
        self.assertNotIn("-gamecfg", saved["pcsx2_arguments"])
        self.assertFalse((self.args.directory / "process-result.json").exists())
        self.assertIsNone(json.loads((self.args.directory / "config-check.json").read_text())["linux_boot_pass"])
        self.assertEqual(saved["log_file"], "pcsx2-config-check.log")

    def test_locked_disk_reports_existing_session_instead_of_starting_another(self):
        self.prepare()
        real_digest = emulator.digest
        def locked_digest(path):
            if path.name == "rootfs-usb.img":
                raise PermissionError("file locked")
            return real_digest(path)
        with patch.object(emulator, "digest", side_effect=locked_digest), \
                patch.object(emulator, "prepare_runtime") as prepare_runtime:
            with self.assertRaisesRegex(ValueError, "close the existing PCSX2"):
                emulator.launch(self.args)
            prepare_runtime.assert_not_called()

    def test_refresh_publishes_new_kernel_at_existing_path_and_preserves_old_session(self):
        self.args.gs_renderer = "software"
        old = self.prepare()
        self.args.gs_renderer = None
        original_disk = (self.args.directory / "rootfs-usb.img").read_bytes()
        profile = self.args.directory / "pcsx2-profile/PCSX2"
        (profile / "bios").mkdir(parents=True)
        (profile / "bios/console.bin").write_bytes(b"local BIOS fixture")
        (profile / "inis").mkdir()
        (profile / "inis/PCSX2.ini").write_text("[Filenames]\nBIOS = console.bin\n")
        (self.args.directory / "boot-observation.txt").write_text("old experiment")
        self.kernel.write_bytes(boot_elf(filesz=32))
        with contextlib.redirect_stdout(io.StringIO()):
            report = emulator.refresh(self.args)
        backup = Path(report["previous_session_backup"])
        self.assertEqual(emulator.digest(backup / "kernel-audit.elf"), old["kernel_sha256"])
        self.assertEqual((backup / "rootfs-usb.img").read_bytes(), original_disk)
        self.assertEqual((backup / "boot-observation.txt").read_text(), "old experiment")
        self.assertEqual((profile / "bios/console.bin").read_bytes(), b"local BIOS fixture")
        self.assertEqual(report["kernel_sha256"], emulator.digest(self.kernel))
        self.assertEqual(report["gs_renderer"], "software")
        self.assertEqual(report["pcsx2_arguments"][-1], str(self.args.directory / "kernel-audit.elf"))
        preset = (self.args.directory / report["preset"]).read_text()
        self.assertIn(str(self.args.directory / "rootfs-usb.img"), preset)
        self.assertNotIn("-refresh-", preset)
        self.assertEqual(set(report["milestones"].values()), {"not performed"})

    def test_failed_refresh_and_active_session_leave_previous_files_untouched(self):
        self.prepare()
        before = {name: (self.args.directory / name).read_bytes()
                  for name in ("session.json", "kernel-audit.elf", "rootfs-usb.img")}
        self.kernel.write_bytes(b"invalid ELF")
        with contextlib.redirect_stdout(io.StringIO()), self.assertRaisesRegex(ValueError, "ELF32"):
            emulator.refresh(self.args)
        self.assertEqual({name: (self.args.directory / name).read_bytes() for name in before}, before)
        self.kernel.write_bytes(boot_elf())
        with emulator.session_guard(self.args.directory):
            with self.assertRaisesRegex(ValueError, "already launching or refreshing"):
                emulator.refresh(self.args)
        self.assertEqual({name: (self.args.directory / name).read_bytes() for name in before}, before)

    def test_rebuilt_source_kernel_requires_refresh_before_launch(self):
        self.prepare()
        self.kernel.write_bytes(boot_elf(filesz=32))
        with patch.object(emulator, "prepare_runtime") as runtime:
            with self.assertRaisesRegex(ValueError, "source kernel was rebuilt"):
                emulator.launch(self.args)
            runtime.assert_not_called()


if __name__ == "__main__":
    unittest.main()
