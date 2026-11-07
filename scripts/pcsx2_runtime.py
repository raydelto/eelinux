"""Select supported PCSX2 CLI options and create an isolated session profile.

SPDX-License-Identifier: GPL-2.0-only
PCSX2 2.6.3 has -portable, but neither -gamecfg nor -datapath.
"""
from __future__ import annotations

import configparser
import ctypes
import io
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time

from emulator_disk import regular_path


def read_ini(path: Path) -> configparser.ConfigParser:
    parser = configparser.ConfigParser(interpolation=None, strict=False)
    parser.optionxform = str
    if path.is_file():
        regular_path(path, must_exist=True)
        try:
            parser.read_string(path.read_text(encoding="utf-8-sig"))
        except configparser.Error as exc:
            raise ValueError(f"invalid PCSX2 settings file: {path}") from exc
    return parser


def default_settings(host: str) -> Path | None:
    if host == "windows":
        if os.name != "nt":
            return None  # WSL callers can provide --pcsx2-config explicitly.
        # Use the real Documents folder, including Windows folder redirection.
        buffer = ctypes.create_unicode_buffer(32768)
        if ctypes.windll.shell32.SHGetFolderPathW(None, 5, None, 0, buffer) != 0:
            raise ValueError("cannot locate Windows Documents; use --pcsx2-config")
        return Path(buffer.value) / "PCSX2/inis/PCSX2.ini"
    config_home = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config"))
    return config_home / "PCSX2/inis/PCSX2.ini"


def probe(executable: Path) -> dict:
    regular_path(executable, must_exist=True)
    # -help exits 1 in the official 2.6.3 build, before configuration/UI startup.
    result = subprocess.run([str(executable), "-help"], text=True,
                            encoding="utf-8", errors="replace", capture_output=True, timeout=15)
    output = result.stdout + result.stderr
    options = set(re.findall(r"^\s+(-[a-z][a-z0-9]*)\b", output, re.MULTILINE))
    required = {"-batch", "-elf", "-logfile", "-testconfig"}
    if result.returncode not in (0, 1) or "PCSX2" not in output or not required <= options:
        raise ValueError("PCSX2 help did not confirm the required CLI options; no boot was started")
    version = next((line.strip() for line in output.splitlines() if line.startswith("PCSX2 ")), "PCSX2")
    return {"version": version, "options": sorted(options), "help_exit_code": result.returncode}


def portable_runtime(executable: Path, session: Path) -> Path:
    """Copy Windows runtime dependencies; never modify the installed executable."""
    destination = session / "pcsx2-runtime"
    import hashlib

    def digest(path):
        with path.open("rb") as stream:
            return hashlib.file_digest(stream, "sha256").hexdigest()

    source_hash = digest(executable)
    identity = destination / "source-executable.sha256"
    copied = destination / executable.name
    if destination.exists():
        regular_path(identity, must_exist=True)
        regular_path(copied, must_exist=True)
        if identity.read_text().strip() != source_hash or digest(copied) != source_hash:
            raise ValueError("PCSX2 runtime changed; prepare a new session directory")
        if (destination / "portable.txt").read_text().strip() != "../pcsx2-profile/PCSX2":
            raise ValueError("session portable profile path changed; prepare a new session")
        return copied
    temporary = Path(tempfile.mkdtemp(prefix=".pcsx2-runtime-", dir=session))
    try:
        selected = [executable, *executable.parent.glob("*.dll")]
        if (executable.parent / "qt.conf").is_file():
            selected.append(executable.parent / "qt.conf")
        for path in selected:
            regular_path(path, must_exist=True)
            shutil.copyfile(path, temporary / path.name)
        for name in ("resources", "QtPlugins", "translations", "D3D12"):
            source = executable.parent / name
            if source.is_dir():
                # No links/junctions to writable global profiles or other files.
                regular_path(source / ".path-check", must_exist=False)
                for path in source.rglob("*"):
                    regular_path(path if path.is_file() else path / ".path-check", must_exist=path.is_file())
                shutil.copytree(source, temporary / name)
        if not (temporary / "resources").is_dir():
            raise ValueError("PCSX2 resources must be next to the installed executable")
        if digest(temporary / executable.name) != source_hash:
            raise ValueError("PCSX2 executable changed during copying")
        (temporary / "portable.txt").write_text("../pcsx2-profile/PCSX2\n", encoding="utf-8")
        (temporary / "source-executable.sha256").write_text(source_hash + "\n", encoding="ascii")
        # Newly copied EXEs/DLLs may be briefly held by the Windows file scanner.
        # Never replace an existing runtime, and propagate persistent failures.
        for attempt in range(5):
            try:
                temporary.rename(destination)
                break
            except PermissionError as exc:
                if getattr(exc, "winerror", None) not in (5, 32, 33) or attempt == 4:
                    raise
                time.sleep(0.2 * (attempt + 1))
        return copied
    finally:
        # Only our checked temporary directory under this session is removed.
        if temporary.exists():
            if temporary.resolve().parent != session.resolve() or not temporary.name.startswith(".pcsx2-runtime-"):
                raise ValueError("invalid runtime temporary directory")
            shutil.rmtree(temporary)


def bios_from_profile(settings: Path | None) -> Path | None:
    if settings is None or not settings.is_file():
        return None
    ini = read_ini(settings)
    filename = ini.get("Filenames", "BIOS", fallback="")
    if not filename:
        return None
    folder = Path(ini.get("Folders", "Bios", fallback="bios"))
    if not folder.is_absolute():
        folder = settings.parent.parent / folder
    return folder / filename


def session_profile(preset: Path, data: Path, *, settings: Path | None,
                    bios: Path | None, iop_recompiler: bool = False, gs_renderer: str = 'opengl') -> tuple[Path, bool]:
    """Import only BIOS files/selection; personal settings and secrets stay outside."""
    profile = data / "inis/PCSX2.ini"
    if gs_renderer not in ('opengl','software'):raise ValueError('unsupported diagnostic GS renderer')
    regular_path(profile, must_exist=False)
    selected = bios or bios_from_profile(profile) or bios_from_profile(settings)
    selected_name = ""
    if selected is not None and selected.is_file():
        selected = regular_path(selected, must_exist=True)
        target = data / "bios" / selected.name
        regular_path(target, must_exist=False)
        target.parent.mkdir(parents=True, exist_ok=True)
        if target != selected:
            shutil.copyfile(selected, target)
            # NVM/MEC and ROM extensions belong to this BIOS stem, never to a
            # shared folder during the emulation experiment.
            for suffix in (".NVM", ".MEC", ".ROM1", ".ROM2", ".EROM"):
                sidecar = selected.with_suffix(suffix)
                if sidecar.is_file():
                    regular_path(sidecar, must_exist=True)
                    regular_path(target.with_suffix(suffix), must_exist=False)
                    shutil.copyfile(sidecar, target.with_suffix(suffix))
        selected_name = selected.name
    elif bios is not None:
        raise ValueError("--bios must name your existing BIOS file")
    ini = read_ini(preset)
    values = {
        "UI": {"SettingsVersion": "1", "SetupWizardIncomplete": "false" if selected_name else "true",
               "StartFullscreen": "false", "PauseOnFocusLoss": "false"},
        "Filenames": {"BIOS": selected_name},
        "Folders": {"Bios": "bios", "MemoryCards": "memcards", "Logs": "logs"},
        # Applied for legacy sessions too, whose immutable preset lacks audio
        # settings. Null bypasses Cubeb/device probing rather than just muting.
        "SPU2/Output": {"Backend": "Null"},
        # A dummy tty0 or a graphics probe failure must not hide the serial
        # diagnostics. Apply to older immutable presets on every launch too.
        "Logging": {"EnableLogWindow": "true", "AttachLogWindowToMainWindow": "false",
                    "EnableFileLogging": "true", "EnableVerbose": "false", "EnableEEConsole": "true",
                    "EnableIOPConsole": "true"},
        "EmuCore/TraceLog": {"Enabled": "false"},
        # The project emulator supports raw kernel SYSCALL exceptions. BIOS HLE
        # interprets v1 as a Sony call and can corrupt Linux syscall arguments.
        "EmuCore/CPU": {"EnableBIOSHLE": "false"},
        # The project Windows build provides OpenGL; a carried-over Vulkan
        # renderer selection must not prevent its graphics window from starting.
        # The native Linux driver uses masked RGB555 clears and frequent GS
        # local copies/readbacks. PCSX2's game-specific clear heuristics can
        # present stale striped targets despite correct VRAM readback. Keep
        # OpenGL hardware rendering, but execute those draws normally.
        "EmuCore/GS": {"Renderer": "13" if gs_renderer=='software' else "12", "UserHacks": "true",
                       "UserHacks_Disable_Safe_Features": "true"},
        # Linux requires the EE interpreter. IOP recompilation is an explicit
        # experiment and must not leak from a previous launch or old preset.
        "EmuCore/CPU/Recompiler": {"EnableEE": "false",
                                   "EnableIOP": "true" if iop_recompiler else "false"},
        "MemoryCards": {"Slot1_Enable": "false", "Slot2_Enable": "false",
                        **{f"Multitap{port}_Slot{slot}_Enable": "false"
                           for port in (1, 2) for slot in (2, 3, 4)}},
    }
    for section, entries in values.items():
        if not ini.has_section(section):
            ini.add_section(section)
        for key, value in entries.items():
            ini.set(section, key, value)
    profile.parent.mkdir(parents=True, exist_ok=True)
    buffer = io.StringIO()
    ini.write(buffer)
    profile.write_text(buffer.getvalue(), encoding="utf-8", newline="\n")
    return profile, bool(selected_name)


def prepare_runtime(executable: Path, session: Path, preset: Path, host: str,
                    path_for_host, *, settings: Path | None = None,
                    bios: Path | None = None, iop_recompiler: bool = False, gs_renderer: str = 'opengl') -> dict:
    capabilities = probe(executable)
    options = set(capabilities["options"])
    data = session / "pcsx2-profile/PCSX2"
    regular_path(data / ".path-check", must_exist=False)
    environment = dict(os.environ)
    program = executable
    if "-datapath" in options:
        mode = "datapath"
        arguments = ["-datapath", path_for_host(data, host)]
    elif host == "windows" and "-portable" in options:
        mode = "portable runtime copy"
        program = portable_runtime(executable, session)
        arguments = ["-portable"]
    elif host == "linux":
        if any((executable.parent / name).exists() for name in ("portable.txt", "portable.ini")):
            raise ValueError("portable Linux install cannot use XDG isolation; use a normal install or a build with -datapath")
        mode = "XDG config directory"
        environment["XDG_CONFIG_HOME"] = str(data.parent)
        arguments = []
    else:
        raise ValueError("PCSX2 offers no supported isolated configuration path")
    profile, configured = session_profile(preset, data,
                                         settings=settings or default_settings(host), bios=bios,
                                         iop_recompiler=iop_recompiler,gs_renderer=gs_renderer)
    return {"program": program, "arguments": arguments, "environment": environment,
            "profile": profile, "log_file": data / "logs/emulog.txt", "bios_configured": configured,
            "mode": mode, "capabilities": capabilities}
