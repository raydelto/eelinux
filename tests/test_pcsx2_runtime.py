"""PCSX2 configuration must work with the installed CLI and stay isolated."""
from __future__ import annotations
import argparse
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import pcsx2_runtime as runtime
from emulator import configuration

HELP_263 = """PCSX2 v2.6.3
  -batch: Enables batch mode.
  -portable: Enables portable mode.
  -elf <file>: Overrides boot ELF.
  -logfile <path>: File log.
  -testconfig: Checks configuration and exits.
"""


class Pcsx2RuntimeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.tree = Path(self.temporary.name)
        self.install = self.tree / "installed"
        self.install.mkdir()
        self.executable = self.install / "pcsx2-qt.exe"
        self.executable.write_bytes(b"fixture never executed")
        (self.install / "needed.dll").write_bytes(b"runtime dependency")
        (self.install / "qt.conf").write_text("[Paths]\nPlugins=QtPlugins\n")
        (self.install / "resources").mkdir()
        (self.install / "resources/font.ttf").write_bytes(b"resource fixture")
        (self.install / "QtPlugins").mkdir()
        (self.install / "QtPlugins/platform.dll").write_bytes(b"plugin fixture")
        self.session = self.tree / "session"
        self.session.mkdir()
        self.preset = self.session / "preset.ini"
        self.preset.write_text(configuration(str(self.session / "copy.img")))
        self.personal = self.tree / "personal"
        (self.personal / "inis").mkdir(parents=True)
        (self.personal / "bios").mkdir()
        self.bios = self.personal / "bios/console.bin"
        self.bios.write_bytes(b"bios fixture not executable")
        self.bios.with_suffix(".NVM").write_bytes(b"private mutable NVM")
        self.settings = self.personal / "inis/PCSX2.ini"
        self.settings.write_text("[UI]\nSettingsVersion=1\n[Filenames]\nBIOS=console.bin\n"
                                 "[Folders]\nBios=bios\nMemoryCards=../precious-cards\n"
                                 "[Achievements]\nToken=fixture-do-not-import\n")

    def tearDown(self):
        self.temporary.cleanup()

    def prepare(self, help_text=HELP_263, host="windows", iop_recompiler=False,gs_renderer='opengl'):
        with patch.object(runtime.subprocess, "run", return_value=argparse.Namespace(
                returncode=1, stdout="", stderr=help_text)):
            return runtime.prepare_runtime(self.executable, self.session, self.preset, host,
                                           lambda path, _: str(path), settings=self.settings,
                                           iop_recompiler=iop_recompiler,gs_renderer=gs_renderer)

    def test_software_gs_comparison_is_isolated_and_resets_to_opengl(self):
        original=self.preset.read_bytes()
        for renderer,number in (('software',13),('opengl',12)):
            result=self.prepare(gs_renderer=renderer)
            self.assertEqual(runtime.read_ini(result['profile']).getint('EmuCore/GS','Renderer'),number)
            self.assertEqual(self.preset.read_bytes(),original)

    def test_iop_opt_in_overrides_legacy_cpu_settings_and_resets_on_next_launch(self):
        self.preset.write_text("[EmuCore/CPU/Recompiler]\nEnableEE=true\nEnableIOP=true\n"
                               "[EmuCore/Speedhacks]\nWaitLoop=false\nIntcStat=false\nEECycleRate=0\n")
        original = self.preset.read_bytes()
        for enabled in (False, True, False):
            with self.subTest(iop_recompiler=enabled):
                result = self.prepare(iop_recompiler=enabled)
                ini = runtime.read_ini(result["profile"])
                self.assertFalse(ini.getboolean("EmuCore/CPU/Recompiler", "EnableEE"))
                self.assertEqual(ini.getboolean("EmuCore/CPU/Recompiler", "EnableIOP"), enabled)
                self.assertFalse(ini.getboolean("EmuCore/Speedhacks", "WaitLoop"))
                self.assertFalse(ini.getboolean("EmuCore/Speedhacks", "IntcStat"))
                self.assertEqual(ini.getint("EmuCore/Speedhacks", "EECycleRate"), 0)
                self.assertEqual(self.preset.read_bytes(), original)

    def test_official_help_exit_one_is_accepted_without_inventing_options(self):
        result = self.prepare()
        self.assertEqual(result["arguments"], ["-portable"])
        self.assertNotIn("-gamecfg", result["capabilities"]["options"])
        self.assertNotIn("-datapath", result["capabilities"]["options"])
        self.assertEqual(result["capabilities"]["version"], "PCSX2 v2.6.3")

    def test_profile_runtime_and_bios_state_are_isolated_from_original_install(self):
        original = {path: path.read_bytes() for path in self.tree.rglob("*") if path.is_file()}
        result = self.prepare()
        self.assertNotEqual(result["program"], self.executable)
        self.assertEqual(result["program"].read_bytes(), self.executable.read_bytes())
        self.assertFalse((self.install / "portable.txt").exists())
        self.assertEqual((result["program"].parent / "portable.txt").read_text().strip(), "../pcsx2-profile/PCSX2")
        ini = runtime.read_ini(result["profile"])
        self.assertFalse(ini.getboolean("EmuCore/CPU/Recompiler", "EnableEE"))
        self.assertFalse(ini.getboolean("EmuCore/CPU/Recompiler", "EnableIOP"))
        self.assertFalse(ini.getboolean("EmuCore/CPU", "EnableBIOSHLE"))
        self.assertFalse(ini.getboolean("MemoryCards", "Slot1_Enable"))
        for port in (1, 2):
            for slot in (2, 3, 4):
                self.assertFalse(ini.getboolean("MemoryCards", f"Multitap{port}_Slot{slot}_Enable"))
        self.assertFalse(ini.has_section("Achievements"))
        self.assertEqual(ini.get("SPU2/Output", "Backend"), "Null")
        self.assertTrue(ini.getboolean("Logging", "EnableLogWindow"))
        self.assertTrue(ini.getboolean("Logging", "EnableEEConsole"))
        self.assertTrue(ini.getboolean("Logging", "EnableFileLogging"))
        self.assertTrue(ini.getboolean("Logging", "EnableIOPConsole"))
        self.assertFalse(ini.getboolean("Logging", "EnableVerbose"))
        self.assertFalse(ini.getboolean("EmuCore/TraceLog", "Enabled"))
        self.assertEqual(ini.getint("EmuCore/GS", "Renderer"), 12)
        self.assertTrue(ini.getboolean("EmuCore/GS", "UserHacks"))
        self.assertTrue(ini.getboolean("EmuCore/GS", "UserHacks_Disable_Safe_Features"))
        self.assertEqual(result["log_file"], self.session / "pcsx2-profile/PCSX2/logs/emulog.txt")
        self.assertEqual(ini.get("Folders", "MemoryCards"), "memcards")
        self.assertEqual(ini.getint("UI", "SettingsVersion"), 1)
        nvm = result["profile"].parent.parent / "bios/console.NVM"
        nvm.write_bytes(b"emulator changed local NVM")
        self.assertTrue(result["bios_configured"])
        for path, contents in original.items():
            self.assertEqual(path.read_bytes(), contents, path)

    def test_runtime_reuse_preserves_local_bios_selection_and_rejects_changed_executable(self):
        first = self.prepare()
        self.settings.unlink()
        second = self.prepare()
        self.assertEqual(first["program"], second["program"])
        self.assertTrue(second["bios_configured"])
        self.executable.write_bytes(b"changed installation")
        with self.assertRaisesRegex(ValueError, "runtime changed"):
            self.prepare()

    def test_newer_advertised_datapath_is_used_without_copying_runtime(self):
        result = self.prepare(HELP_263 + "  -datapath <path>: Override data root.\n")
        self.assertEqual(result["program"], self.executable)
        self.assertEqual(result["arguments"][0], "-datapath")
        self.assertFalse((self.session / "pcsx2-runtime").exists())

    def test_linux_uses_xdg_without_changing_parent_environment(self):
        before = os.environ.get("XDG_CONFIG_HOME")
        result = self.prepare(host="linux")
        self.assertEqual(result["arguments"], [])
        self.assertEqual(result["environment"]["XDG_CONFIG_HOME"], str(self.session / "pcsx2-profile"))
        self.assertEqual(os.environ.get("XDG_CONFIG_HOME"), before)
        (self.install / "portable.txt").write_text("")
        with self.assertRaisesRegex(ValueError, "portable Linux install"):
            self.prepare(host="linux")

    def test_failed_or_incomplete_help_never_creates_runtime_or_profile(self):
        for exit_code, help_text in ((1, ""), (3, HELP_263), (1, "not PCSX2 help")):
            with self.subTest(exit_code=exit_code, help=help_text):
                with patch.object(runtime.subprocess, "run", return_value=argparse.Namespace(
                        returncode=exit_code, stdout="", stderr=help_text)):
                    with self.assertRaises(ValueError):
                        runtime.prepare_runtime(self.executable, self.session, self.preset, "windows",
                                                lambda path, _: str(path), settings=self.settings)
                self.assertFalse((self.session / "pcsx2-profile").exists())
                self.assertFalse((self.session / "pcsx2-runtime").exists())

    def test_missing_bios_requires_session_setup_instead_of_importing_global_settings(self):
        self.bios.unlink()
        result = self.prepare()
        self.assertFalse(result["bios_configured"])
        ini = runtime.read_ini(result["profile"])
        self.assertTrue(ini.getboolean("UI", "SetupWizardIncomplete"))
        self.assertEqual(ini.get("Filenames", "BIOS"), "")

    def test_legacy_preset_migrates_to_null_audio_without_changing_the_original(self):
        self.preset.write_text("[EmuCore/CPU/Recompiler]\nEnableEE=false\nEnableIOP=false\n"
                               "[SPU2/Output]\nBackend=Cubeb\nOutputMuted=true\n"
                               "[Logging]\nEnableVerbose=true\n"
                               "[EmuCore/TraceLog]\nEnabled=true\n")
        original = self.preset.read_bytes()
        result = self.prepare()
        ini = runtime.read_ini(result["profile"])
        self.assertEqual(ini.get("SPU2/Output", "Backend"), "Null")
        self.assertEqual(self.preset.read_bytes(), original)
        self.assertTrue(ini.getboolean("Logging", "EnableLogWindow"))
        self.assertTrue(ini.getboolean("Logging", "EnableFileLogging"))
        self.assertFalse(ini.getboolean("Logging", "EnableVerbose"))
        self.assertFalse(ini.getboolean("EmuCore/TraceLog", "Enabled"))
        self.assertFalse(ini.getboolean("EmuCore/CPU", "EnableBIOSHLE"))

    def test_transient_windows_runtime_lock_is_retried(self):
        rename = Path.rename
        failures = []
        def transient_lock(path, destination):
            if path.name.startswith(".pcsx2-runtime-") and not failures:
                failures.append(path)
                error = PermissionError("Windows scanner still holds a copied DLL")
                error.winerror = 5
                raise error
            return rename(path, destination)
        with patch.object(Path, "rename", transient_lock), patch.object(runtime.time, "sleep") as sleep:
            result = self.prepare()
        self.assertEqual(len(failures), 1)
        sleep.assert_called_once_with(0.2)
        self.assertTrue(result["program"].is_file())

    def test_persistent_runtime_lock_fails_without_publishing_partial_runtime(self):
        error = PermissionError("Windows runtime remains locked")
        error.winerror = 5
        with patch.object(Path, "rename", side_effect=error), patch.object(runtime.time, "sleep") as sleep:
            with self.assertRaises(PermissionError):
                self.prepare()
        self.assertEqual(sleep.call_count, 4)
        self.assertFalse((self.session / "pcsx2-runtime").exists())
        self.assertFalse(list(self.session.glob(".pcsx2-runtime-*")))


if __name__ == "__main__":
    unittest.main()
