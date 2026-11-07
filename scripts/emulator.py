#!/usr/bin/env python3
"""Prepare an experimental PCSX2 session with a disposable USB disk or DVD.

SPDX-License-Identifier: GPL-2.0-only
Successful preparation or emulator process exit is not a Linux boot result.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import uuid

from emulator_disk import regular_path, wrap_rootfs
from emulator_hardware_disk import copy_hardware_disk
from emulator_process import process_identity, run_process, stop_process
from pcsx2_runtime import prepare_runtime
from emulator_runtime_selection import select_runtime

ROOT = Path(__file__).resolve().parents[1]
PCSX2_REFERENCE = "81526d4dc7cc70e4ae75abb35a789417456c6d43"


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def validate_kernel(path: Path) -> None:
    path = regular_path(path, must_exist=True)
    data = path.read_bytes()
    if len(data) < 52 or data[:7] != b"\x7fELF\x01\x01\x01":
        raise ValueError("kernel must be little-endian ELF32")
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    if (header[1:4] != (2, 8, 1) or header[8:10] != (52, 32) or not header[10]
            or header[5] < 52 or header[5] + 32 * header[10] > len(data)):
        raise ValueError("kernel must be a standalone MIPS executable")
    if (header[7] & 0xF0000000 != 0x20000000 or header[7] & 0x00FF0000 != 0x00920000
            or header[7] & 0xF000 != 0x1000 or header[7] & 0x0E000020):
        raise ValueError("kernel must target R5900 MIPS3/o32 without unsupported instruction modes")
    shoff, shsize, shnum, shstrings = header[6], header[11], header[12], header[13]
    if shnum:
        if shsize != 40 or shoff < 52 or shoff + shsize * shnum > len(data) or shstrings >= shnum:
            raise ValueError("kernel ELF has a malformed section table")
        for index in range(shnum):
            section = struct.unpack_from("<10I", data, shoff + shsize * index)
            if section[1] != 8 and section[4] + section[5] > len(data):  # NOBITS occupies no file bytes
                raise ValueError("kernel ELF has a truncated section")
    elif shoff or shstrings:
        raise ValueError("kernel ELF extended section numbering is unsupported")

    def physical(address: int) -> int:
        # The compressed vmlinuz loader uses physical 8 MiB. Uncompressed
        # kernels can use unmapped KSEG0/KSEG1 aliases of the same EE RAM.
        if 0x80000000 <= address < 0xC0000000:
            return address & 0x1FFFFFFF
        if address < 32 * 1024 * 1024:
            return address
        raise ValueError("kernel address is outside EE RAM and its KSEG aliases")

    entry = physical(header[4])
    if entry % 4:
        raise ValueError("kernel entry must be aligned to a R5900 instruction")
    mapped = False
    loads = []
    for index in range(header[10]):
        offset = header[5] + index * header[9]
        if offset + 32 > len(data):
            raise ValueError("truncated kernel ELF program table")
        kind, start, virtual, paddr, size, memory, flags, _ = struct.unpack_from("<8I", data, offset)
        if start + size > len(data) or kind in (2, 3):
            raise ValueError("kernel ELF contains a truncated segment or userspace interpreter")
        if kind != 1:
            continue
        address = physical(virtual)
        if not memory or size > memory or physical(paddr) != address:
            raise ValueError("kernel load segment has invalid sizes or physical/virtual mapping")
        if address + memory > 32 * 1024 * 1024:
            raise ValueError("kernel ELF exceeds 32 MiB main RAM")
        if any(address < end and first < address + memory for first, end in loads):
            raise ValueError("kernel ELF has overlapping physical load segments")
        loads.append((address, address + memory))
        if flags & 1 and address <= entry < address + size:
            mapped = True
    if not mapped:
        raise ValueError("kernel ELF entry is not backed by executable file bytes")


def host_path(path: Path, host: str) -> str:
    absolute = path.resolve()
    if host == "windows" and os.name != "nt":
        if not shutil.which("wslpath"):
            raise ValueError("--host windows requires Windows Python or WSL's wslpath")
        result = subprocess.run(["wslpath", "-w", str(absolute)], check=True,
                                text=True, capture_output=True).stdout.rstrip("\r\n")
    elif host == "linux" and os.name == "nt":
        raise ValueError("prepare Linux sessions using Linux/WSL Python")
    else:
        result = str(absolute)
    if any(character in result for character in ("\r", "\n", "\0")):
        raise ValueError("emulator paths cannot contain INI control characters")
    return result


def configuration(disk: str, *, optical: bool = False) -> str:
    if any(character in disk for character in ("\r", "\n", "\0")):
        raise ValueError("invalid emulator disk path")
    return f"""; eeLinux experimental boot/debug preset, not a Linux compatibility claim.
; Confirm these keys against the installed PCSX2 version; see docs/emulator.md.
[EmuCore/CPU]
ExtraMemory = false
EnableBIOSHLE = false
[EmuCore/CPU/Recompiler]
EnableEE = false
EnableIOP = false
[EmuCore/Speedhacks]
EECycleRate = 0
EECycleSkip = 0
WaitLoop = false
IntcStat = false
vuFlagHack = false
vu1Instant = false
vuThread = false
fastCDVD = false
[Logging]
EnableLogWindow = true
AttachLogWindowToMainWindow = false
EnableFileLogging = true
EnableVerbose = false
EnableEEConsole = true
EnableIOPConsole = true
[EmuCore/TraceLog]
Enabled = false
[SPU2/Output]
Backend = Null
[DEV9/Eth]
EthEnable = true
EthApi = Sockets
EthDevice = Auto
[DEV9/Hdd]
HddEnable = false
[USB1]
Type = {'' if optical else 'Msd'}
Msd_subtype = 0
Msd_ImagePathMsd = {disk}
[USB2]
Type = hidkbd
"""


def prepare(args: argparse.Namespace) -> dict:
    gs_renderer = getattr(args, "gs_renderer", None) or "opengl"
    if gs_renderer not in ("opengl", "software"):
        raise ValueError("invalid session GS renderer")
    kernel = args.kernel.absolute()
    optical = getattr(args, 'dvd_image', None) is not None
    rootfs = (args.dvd_image if optical else args.rootfs).absolute()
    validate_kernel(kernel)
    directory = args.directory.absolute()
    regular_path(directory / ".session-path-check", must_exist=False)
    if directory.is_symlink() or directory.exists() and any(directory.iterdir()):
        raise ValueError("choose a new empty session directory; existing emulator disks are preserved")
    directory.mkdir(parents=True, exist_ok=True)
    published = getattr(args, "_published_directory", directory)
    usb_image = getattr(args, "usb_image", None)
    disk_name = 'disc.iso' if optical else 'rootfs-usb.img'
    if optical:
        regular_path(rootfs, must_exist=True)
        with rootfs.open('rb') as stream:
            stream.seek(16 * 2048)
            if stream.read(7) != b'\x01CD001\x01':
                raise ValueError('DVD image must contain an ISO9660 primary volume descriptor')
        shutil.copyfile(rootfs, directory / disk_name)
        disk_report = {'disk': {'sha256': digest(directory / disk_name)},
                       'source': {'sha256': digest(rootfs)}, 'required_guest_root': '/dev/ps2dvd'}
    else:
        disk_report = (copy_hardware_disk(usb_image, rootfs, kernel, directory / disk_name)
                       if usb_image is not None else wrap_rootfs(rootfs, directory / disk_name))
    disk_report["disk"]["path"] = host_path(published / disk_name, args.host)
    disk_report["source"]["path"] = host_path(rootfs, args.host)
    (directory / (disk_name + '.json')).write_text(json.dumps(disk_report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    copied_kernel = directory / ("kernel-audit.elf" if args.audit_kernel else "kernel.elf")
    shutil.copyfile(kernel, copied_kernel)
    config = directory / "pcsx2-eeLinux.ini"
    config.write_text(configuration(host_path(published / disk_name, args.host), optical=optical),
                      encoding="utf-8", newline="\n")
    command_args = ["-batch", "-elf", host_path(published / copied_kernel.name, args.host)]
    if optical:
        command_args.append(host_path(published / disk_name, args.host))
    milestones = ["elf_loaded", "kernel_started", "early_root_started",
                  "disk_root_mounted", "gnu_sysvinit_started", "authenticated_shell"]
    report = {
        "schema": 1, "purpose": "experimental PCSX2 boot/debug session",
        "host": args.host, "pcsx2_source_reference": PCSX2_REFERENCE,
        "kernel_sha256": digest(copied_kernel), "kernel_file": copied_kernel.name,
        "kernel_source_path": host_path(kernel, args.host),
        "kernel_kind": "incomplete port/audit ELF; included contents and completeness unverified" if args.audit_kernel else "provided standalone ELF; completeness unverified",
        "rootfs_source_sha256": disk_report["source"]["sha256"],
        "rootfs_source_path": host_path(rootfs, args.host),
        "prepared_disk_sha256": disk_report["disk"]["sha256"],
        "disposable_disk": disk_name, "optical_root": optical, "USB_storage_attached": not optical, "preset": config.name,
        "preset_sha256": digest(config), "pcsx2_arguments": command_args,
        "log_file": "pcsx2-profile/PCSX2/logs/emulog.txt", "log_mode": "normal",
        "ee_cpu_mode": "interpreter", "iop_cpu_mode": "interpreter",
        "gs_renderer": gs_renderer,
        "configuration_method": "selected from installed PCSX2 -help during launch",
        "required_guest_root": disk_report.get("required_guest_root", "/dev/sda1"), "guest_main_ram_mib": 32,
        "milestones": {name: "not performed" for name in milestones},
        "stock_pcsx2_linux_mmu_qualified": False,
        "audit_scope": "incomplete port/audit boot observation; included contents and completeness unverified" if args.audit_kernel else "manual stage-by-stage boot observation",
        "bios": "configure your console BIOS in PCSX2 before launch",
        "native_ethernet_and_gs_acceptance": "not established by this session",
    }
    (directory / "session.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Prepared {directory}\nBoot arguments: {json.dumps(command_args)}\n"
          "The launcher applies the preset through an isolated PCSX2 profile.\n"
          "Preparation passed; emulator boot has not been tested. See docs/emulator.md.")
    return report


@contextmanager
def session_guard(directory: Path):
    """Exclude launch/refresh races, including while renaming the directory."""
    lock = regular_path(directory.parent / ("." + directory.name + ".running.lock"), must_exist=False)
    try:
        descriptor = os.open(lock, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    except FileExistsError:
        regular_path(lock, must_exist=True)
        contents = lock.read_text(encoding="utf-8")
        prior = json.loads(contents)
        if prior.get("platform", sys.platform) != sys.platform:
            raise ValueError("session lock belongs to another host; close its launcher first") from None
        pid = prior.get("pid")
        if not isinstance(pid, int) or pid <= 0:
            raise ValueError("invalid session lock") from None
        current = process_identity(pid)
        active = current is not None and (not prior.get("identity") or current == prior["identity"])
        running = directory / "running-process.json"
        if running.is_file():
            child = json.loads(running.read_text())
            if child.get("active") and child.get("identity"):
                active = active or process_identity(child["pid"]) == child["identity"]
        if active:
            raise ValueError(f"session is already launching or refreshing; use stop first ({lock})") from None
        if lock.read_text(encoding="utf-8") != contents:
            raise ValueError("session lock changed during validation") from None
        lock.unlink()
        descriptor = os.open(lock, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump({"pid": os.getpid(), "session": str(directory), "platform": sys.platform,
                       "identity": process_identity(os.getpid())}, stream)
        yield
    finally:
        lock.unlink()


def refresh(args: argparse.Namespace) -> dict:
    """Prepare first, then retain the old session and publish at the same path."""
    directory = args.directory.absolute()
    regular_path(directory / ".session-path-check", must_exist=False)
    if not directory.is_dir():
        raise ValueError("refresh requires an existing session; use prepare for a new directory")
    report_path = regular_path(directory / "session.json", must_exist=True)
    old = json.loads(report_path.read_text(encoding="utf-8"))
    if old.get("host") != args.host:
        raise ValueError("refresh host must match the existing session")
    inputs = [args.kernel, getattr(args, 'dvd_image', None) or args.rootfs]
    if getattr(args, 'usb_image', None):
        inputs.append(args.usb_image)
    for input_path in inputs:
        source = regular_path(input_path, must_exist=True)
        if source.is_relative_to(directory.resolve()):
            raise ValueError("refresh inputs must be outside the session being replaced")
    with session_guard(directory):
        temporary = Path(tempfile.mkdtemp(prefix="." + directory.name + "-refresh-", dir=directory.parent))
        backup = directory.with_name(directory.name + "-backup-" +
                                     datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ-") + uuid.uuid4().hex[:8])
        regular_path(backup / ".session-path-check", must_exist=False)
        try:
            staged = argparse.Namespace(**vars(args))
            staged.directory = temporary
            staged._published_directory = directory
            staged.gs_renderer = getattr(args, "gs_renderer", None) or old.get("gs_renderer", "opengl")
            report = prepare(staged)
            if "timeout_seconds" in old:
                report["timeout_seconds"] = old["timeout_seconds"]
            # Preserve the local BIOS selection and files. Runtime and logs stay
            # in the complete backup; launch recreates its portable runtime.
            for name in ("bios", "inis"):
                source = directory / "pcsx2-profile/PCSX2" / name
                if source.is_dir():
                    regular_path(source / ".path-check", must_exist=False)
                    for path in source.rglob("*"):
                        regular_path(path if path.is_file() else path / ".path-check", must_exist=path.is_file())
                    shutil.copytree(source, temporary / "pcsx2-profile/PCSX2" / name)
            report["previous_session_backup"] = str(backup)
            (temporary / "session.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
            try:
                directory.rename(backup)
            except PermissionError as exc:
                raise ValueError("close the existing PCSX2 session before refreshing its files") from exc
            try:
                temporary.rename(directory)
            except OSError:
                backup.rename(directory)
                raise
            print(f"Refreshed {directory}\nPrevious session preserved at {backup}\n"
                  f"Kernel SHA256: {report['kernel_sha256']}")
            return report
        finally:
            if temporary.exists():
                if temporary.resolve().parent != directory.resolve().parent or not temporary.name.startswith("." + directory.name + "-refresh-"):
                    raise ValueError("invalid refresh temporary directory")
                shutil.rmtree(temporary)


def launch(args: argparse.Namespace) -> None:
    directory = args.directory.resolve(strict=True)
    with session_guard(directory):
        _launch(args)


def _launch(args: argparse.Namespace) -> None:
    directory = args.directory.resolve(strict=True)
    regular_path(directory / ".session-path-check", must_exist=False)
    report = json.loads((directory / "session.json").read_text(encoding="utf-8"))
    gs_renderer = getattr(args, "gs_renderer", None) or report.get("gs_renderer", "opengl")
    if gs_renderer not in ("opengl", "software"):
        raise ValueError("invalid session GS renderer")
    kernel = directory / report["kernel_file"]
    config = directory / report["preset"]
    if kernel.parent != directory or config.parent != directory:
        raise ValueError("invalid session paths")
    validate_kernel(kernel)
    regular_path(config, must_exist=True)
    disk = regular_path(directory / report.get('disposable_disk', 'rootfs-usb.img'), must_exist=True)
    if disk.parent != directory:
        raise ValueError('invalid session disk path')
    source = Path(report["rootfs_source_path"])
    if source.exists() and os.path.samefile(source, disk):
        raise ValueError("disposable emulator disk must not alias the source root filesystem")
    if digest(kernel) != report["kernel_sha256"] or digest(config) != report["preset_sha256"]:
        raise ValueError("session kernel/config changed; prepare a new session")
    kernel_source = Path(report.get("kernel_source_path", ""))
    if report.get("kernel_source_path") and kernel_source.is_file() and digest(kernel_source) != report["kernel_sha256"]:
        raise ValueError("source kernel was rebuilt; run refresh with --kernel and --rootfs before launching this session")
    try:
        disk_digest = digest(disk)
    except PermissionError as exc:
        raise ValueError("emulator disk is in use; close the existing PCSX2 session before launching again") from exc
    if disk_digest != report["prepared_disk_sha256"]:
        raise ValueError("disposable disk changed since preparation; prepare a new session")
    if args.pcsx2 is None or not args.pcsx2.is_file():
        raise ValueError("launch requires --pcsx2 pointing to the installed executable")
    if args.host != report["host"]:
        raise ValueError("launch host must match the prepared session")
    timeout = getattr(args, "timeout", None)
    if timeout is None:
        timeout = report.get("timeout_seconds", 300 if args.host == "windows" else 0)
    if not isinstance(timeout, int) or isinstance(timeout, bool) or not 0 <= timeout <= 3600:
        raise ValueError("--timeout must be 0..3600 seconds (0 disables the limit)")
    selected, selection = select_runtime(args.pcsx2.resolve(), args.host,
                                         stock=getattr(args, 'stock_pcsx2', False))
    if selection:
        print(f"Using the project Windows MMU runtime: {selected}", flush=True)
    runtime = prepare_runtime(selected, directory, config, args.host, host_path,
                              settings=getattr(args, "pcsx2_config", None),
                              bios=getattr(args, "bios", None),
                              iop_recompiler=getattr(args, "iop_recompiler", False),
                              gs_renderer=gs_renderer)
    check_config = getattr(args, "check_config", False)
    # Build CLI arguments from checked local paths and advertised capabilities.
    command = [str(runtime["program"]), *runtime["arguments"]]
    if check_config:
        command += ["-testconfig"]
    else:
        command += ["-batch", "-elf", host_path(kernel, args.host)]
        if report.get('optical_root'):
            command.append(host_path(disk, args.host))
    debug_log = getattr(args, "debug_log", False)
    if check_config or debug_log:
        log_file = "pcsx2-config-check.log" if check_config else "pcsx2-boot.log"
        log_path = directory / log_file
        command += ["-logfile", host_path(log_path, args.host)]
        log_mode = "configuration check" if check_config else "debug"
    else:
        # PCSX2 2.6.3 SetFileLogPath() forces LOGLEVEL_DEBUG. Ordinary profile
        # logging filters DevCon TLB writes while retaining guest serial output.
        # https://github.com/PCSX2/pcsx2/blob/v2.6.3/pcsx2/VMManager.cpp
        log_path = runtime["log_file"]
        log_file = log_path.relative_to(directory).as_posix()
        log_mode = "normal"
    metadata = {"pcsx2_version": runtime["capabilities"]["version"],
                "runtime_selection": selection,
                "last_invocation": "configuration check" if check_config else "kernel boot attempt",
                "configuration_method": runtime["mode"],
                "configuration_file": str(runtime["profile"]),
                "configuration_sha256_before_launch": digest(runtime["profile"]),
                "bios_configured": runtime["bios_configured"],
                "audio_backend": "Null", "log_file": log_file, "log_mode": log_mode,
                "ee_cpu_mode": "interpreter",
                "gs_renderer": gs_renderer,
                "timeout_seconds": timeout,
                "iop_cpu_mode": "recompiler" if getattr(args, "iop_recompiler", False) else "interpreter",
                "cli_capabilities": runtime["capabilities"], "pcsx2_arguments": command[1:]}
    report.update(metadata)
    (directory / "session.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"{runtime['capabilities']['version']}: {runtime['mode']}\n"
          + ("Checking isolated PCSX2 configuration."
             if check_config else "Launching experimental PCSX2 session; record boot stages separately."), flush=True)
    if not check_config:
        print(f"Kernel: {kernel}\nSHA256: {report['kernel_sha256']}\n"
              f"Guest root: {report['required_guest_root']}", flush=True)
        print(f"Guest serial output: PCSX2 log window and {directory / log_file}", flush=True)
    if not runtime["bios_configured"] and not check_config:
        print("Select your BIOS in the session's PCSX2 setup wizard, or supply --bios.", flush=True)
    timed_out = False
    try:
        result = run_process(command, cwd=directory, env=runtime["environment"],
                             timeout=15 if check_config else timeout or None)
        exit_code = result.returncode
    except subprocess.TimeoutExpired:
        timed_out = True
        exit_code = None
    result_file = "config-check.json" if check_config else "process-result.json"
    (directory / result_file).write_text(json.dumps({
        **metadata, "pcsx2_exit_code": exit_code, "timed_out": timed_out,
        "configuration_check_only": check_config, "linux_boot_pass": None,
        "note": "exit code/configuration check is not a Linux boot result",
    }, indent=2) + "\n", encoding="utf-8")
    if timed_out:
        raise RuntimeError(f"PCSX2 reached the time limit; see {result_file} and {log_file}")
    if result.returncode:
        raise RuntimeError(f"PCSX2 exited with code {result.returncode}; see {result_file} and {log_file}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("prepare", "refresh", "launch", "stop"))
    parser.add_argument("--directory", type=Path, default=ROOT / "build/emulator/pcsx2")
    parser.add_argument("--host", choices=("windows", "linux"), default="windows" if os.name == "nt" else "linux")
    parser.add_argument("--kernel", type=Path)
    parser.add_argument("--rootfs", type=Path)
    parser.add_argument("--usb-image", type=Path, help="copy a checked project FAT32+ext4 image instead of wrapping rootfs as partition 1")
    parser.add_argument('--dvd-image', type=Path, help='boot optical media with no USB storage attached')
    parser.add_argument("--audit-kernel", action="store_true", help="identify incomplete audit ELF; no userspace boot claim")
    parser.add_argument("--pcsx2", type=Path)
    parser.add_argument("--pcsx2-config", type=Path, help="existing PCSX2.ini used only to locate your BIOS")
    parser.add_argument("--bios", type=Path, help="your existing BIOS file to copy into the isolated session")
    parser.add_argument("--check-config", action="store_true", help="validate PCSX2 configuration and exit without booting")
    parser.add_argument("--debug-log", action="store_true", help="launch with detailed debug logging to pcsx2-boot.log")
    parser.add_argument("--iop-recompiler", action="store_true", help="experiment with the IOP recompiler; EE remains interpreted")
    parser.add_argument('--gs-renderer',choices=('opengl','software'),
                        help='isolated host GS renderer; reuse the session choice when omitted; guest driver is unchanged')
    parser.add_argument("--timeout", type=int,
                        help="seconds; reuse the session limit when omitted; new Windows sessions default to 300; 0 disables the limit")
    parser.add_argument("--stock-pcsx2", action="store_true",
                        help="explicitly use the supplied stock executable for comparison")
    args = parser.parse_args()
    if args.dvd_image and (args.rootfs or args.usb_image):
        parser.error('--dvd-image cannot be combined with --rootfs or --usb-image')
    if args.command in ("prepare", "refresh") and (args.kernel is None or not (args.rootfs or args.dvd_image)):
        parser.error(f"{args.command} requires --kernel and either --rootfs or --dvd-image")
    try:
        if args.command == "stop":
            stopped = stop_process(args.directory.resolve(strict=True))
            print("Stopped the verified session process." if stopped else "This session has no active emulator process.")
        else:
            {"prepare": prepare, "refresh": refresh, "launch": launch}[args.command](args)
        return 0
    except (ValueError, OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"emulator: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
