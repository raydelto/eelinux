"""Keep user smoke tests pinned to their built R5900 CPU implementation."""
import importlib.util
import json
from pathlib import Path
from types import SimpleNamespace
import sys
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
SPEC = importlib.util.spec_from_file_location("eelinux_emulator_userland", ROOT / "scripts/emulator_userland.py")
EMU = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EMU)


class EmulatorUserlandTests(unittest.TestCase):
    def test_only_qemu_patches_are_applied_to_qemu_sources(self):
        patches=EMU.qemu_patches()
        self.assertTrue(any(p.name=='qemu-linux-user-cli-abi.patch' for p in patches))
        self.assertTrue(all(p.name.startswith('qemu-') for p in patches))
        self.assertFalse(any('pcsx2' in p.name or p.name.startswith('ps2-') for p in patches))

    def test_guest_execution_requires_explicit_r5900_and_loader_prefix(self):
        with patch.object(EMU, "validate_elf") as abi:
            emulator = Path("/pinned/qemu-mipsel")
            staging = Path("/guest")
            executable = staging / "bin/bash"
            argv = EMU.guest_command(emulator, staging, executable, ["--noprofile"])
            self.assertEqual(argv, [str(emulator), "-cpu", "R5900", "-L", str(staging), str(executable), "--noprofile"])
            abi.assert_called_once_with(executable)

    def test_replaced_emulator_is_rejected_before_guest_execution(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            binary = work / "build/mipsel-linux-user/qemu-mipsel"
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b"original pinned emulator fixture")
            report = {"manifest_sha256": EMU.sha256(EMU.MANIFEST), "cpu": "R5900", "emulator_sha256": EMU.sha256(binary)}
            (work / "build-report.json").write_text(json.dumps(report), encoding="utf-8")
            binary.write_bytes(b"different generic emulator fixture")
            with self.assertRaises(RuntimeError):
                EMU.selected_emulator(work)

    def test_help_status_one_requires_an_actual_r5900_model(self):
        with patch.object(EMU.subprocess, "run") as runner:
            runner.return_value = subprocess.CompletedProcess([], 1, "MIPS '24Kf'\nMIPS 'R5900'\n", "")
            self.assertIn("R5900", EMU.cpu_models(Path("/pinned/qemu-mipsel")))
            runner.return_value = subprocess.CompletedProcess([], 1, "MIPS '24Kf'\n", "")
            with self.assertRaises(RuntimeError):
                EMU.cpu_models(Path("/generic/qemu-mipsel"))

    def test_unbounded_program_timeout_is_rejected_before_execution(self):
        with patch.object(EMU, "linux_only"), patch.object(EMU, "use_native_linux_path"), \
                patch.object(EMU, "selected_emulator") as selected:
            with self.assertRaises(RuntimeError):
                EMU.test(SimpleNamespace(timeout=0))
            selected.assert_not_called()


if __name__ == "__main__":
    unittest.main()
