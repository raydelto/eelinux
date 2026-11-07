#!/usr/bin/env python3
"""Pinned PS2 kernel/IOP build and an honest, reproducible Linux 7.2.8 transplant."""
from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import urllib.request
from iop_firmware import validate_firmware

ROOT = Path(__file__).resolve().parents[1]
KERNEL_CONFIG = ROOT / "configs/kernel.config"
NETWORKING_BEGIN = "# BEGIN EELINUX OPTIONAL NETWORKING"
NETWORKING_END = "# END EELINUX OPTIONAL NETWORKING"
LOCK = json.loads((ROOT / "kernel/sources.json").read_text())
VENDOR = ROOT / "kernel/vendor/frno7"
REQUIRED = {
    "SONY_PS2": "y", "CPU_R5900": "y", "CPU_LITTLE_ENDIAN": "y", "32BIT": "y",
    "MODULES": "y", "BLK_DEV_INITRD": "y", "DEVTMPFS": "y", "FW_LOADER": "y",
    "FUTEX": "y", "COMPAT_32BIT_TIME": "y", "FILE_LOCKING": "y",
    "TTY": "y", "VT": "y", "VT_CONSOLE": "y", "UNIX98_PTYS": "y",
    "FRAMEBUFFER_CONSOLE": "y", "SERIAL_PS2_UART": "y", "SERIAL_PS2_UART_CONSOLE": "y",
    "EXT4_FS": "y", "BINFMT_ELF": "y", "PS2_GS": "y", "FB_PS2": "y",
    "USB": "y", "USB_OHCI_HCD": "y", "USB_OHCI_HCD_PS2": "y",
    "USB_STORAGE": "y", "BLK_DEV_SD": "y", "HID": "y",
    "USB_HID": "y", "HID_GENERIC": "y", "INPUT_EVDEV": "y",
    "INET": "y", "PACKET": "y", "ETHERNET": "y", "PS2_SMAP": "y",
    "PS2_IOP_BRIDGE": "y", "PATA_PS2": "m", "RTC_DRV_PS2": "y",
}
DRIVER_PATHS = [
    "arch/mips/ps2/memory.c", "arch/mips/ps2/Platform",
    "drivers/ps2/sif.c", "drivers/ps2/iop-irq.c", "drivers/ps2/iop-module-request.c",
    "drivers/usb/host/ohci-ps2.c", "drivers/ata/pata_ps2.c",
    "drivers/video/fbdev/ps2fb.c", "drivers/rtc/rtc-ps2.c",
    "drivers/tty/serial/ps2_uart.c", "drivers/ps2/gamepad.c",
    "drivers/gpu/drm/gs/gs_drm.c",
]
# GS and ps2fb are built in so a console does not depend on loading the root disk.
EARLY = ["sif", "iop-memory", "iop-module", "iop-irq", "ohci-ps2",
         "usb-storage", "sd_mod", "hid-generic", "usbhid", "evdev"]


def run(cmd, *, cwd=None, capture=False, check=True):
    result = subprocess.run([str(x) for x in cmd], cwd=cwd, check=check,
                            stdout=subprocess.PIPE if capture else None,
                            stderr=subprocess.STDOUT if capture else None)
    return result.stdout.decode(errors="replace") if capture else result.returncode


def linux_required():
    if sys.platform != "linux":
        raise RuntimeError("Kernel/IOP builds and full Linux source checkout require Linux. "
                           "Use Debian/Ubuntu or WSL and keep build/source files on its native filesystem.")


def sha256(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def write_json(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def fetch_git(entry, dest):
    dest = Path(dest).resolve()
    if not (dest / ".git").exists():
        if dest.exists() and any(dest.iterdir()):
            raise RuntimeError(f"Refusing to replace nonempty source directory: {dest}")
        dest.mkdir(parents=True, exist_ok=True)
        run(["git", "init", dest])
        run(["git", "-C", dest, "remote", "add", "origin", entry["repository"]])
    elif run(["git", "-C", dest, "status", "--porcelain"], capture=True).strip():
        raise RuntimeError(f"Refusing to reset modified source checkout: {dest}")
    run(["git", "-C", dest, "fetch", "--depth", "1", "origin", entry["revision"]])
    run(["git", "-C", dest, "checkout", "--detach", entry["revision"]])
    head = run(["git", "-C", dest, "rev-parse", "HEAD"], capture=True).strip()
    if head != entry["revision"]:
        raise RuntimeError("Fetched source revision differs from source lock")
    return dest


def safe_extract(archive, destination):
    destination = Path(destination).resolve()
    with tarfile.open(archive, "r:xz") as tar:
        for member in tar.getmembers():
            target = (destination / member.name).resolve()
            if not target.is_relative_to(destination):
                raise RuntimeError(f"Archive path escapes destination: {member.name}")
            if member.issym() or member.islnk():
                anchor = target.parent if member.issym() else destination
                if not (anchor / member.linkname).resolve().is_relative_to(destination):
                    raise RuntimeError(f"Archive link escapes destination: {member.name}")
        tar.extractall(destination, filter="data")


def fetch(args):
    linux_required()
    entry = LOCK[args.profile]
    if entry["kind"] == "git":
        source = fetch_git(entry, args.source)
    else:
        source = Path(args.source).resolve()
        if source.exists():
            raise RuntimeError(f"Source destination already exists: {source}; audit it or use a new path")
        cache = Path(args.cache).resolve()
        cache.mkdir(parents=True, exist_ok=True)
        archive = cache / f"linux-{entry['version']}.tar.xz"
        if not archive.exists():
            temporary = archive.with_suffix(archive.suffix + ".part")
            urllib.request.urlretrieve(entry["url"], temporary)
            if sha256(temporary) != entry["sha256"]:
                raise RuntimeError("Kernel archive SHA-256 mismatch; source not extracted")
            temporary.replace(archive)
        if sha256(archive) != entry["sha256"]:
            raise RuntimeError("Cached kernel archive SHA-256 mismatch")
        unpack = source.parent / f".eelinux-unpack-{os.getpid()}"
        unpack.mkdir(parents=True)
        safe_extract(archive, unpack)
        (unpack / f"linux-{entry['version']}").rename(source)
        unpack.rmdir()
    # Marker is ignored by git status, without altering tracked upstream files.
    if (source / ".git/info/exclude").exists():
        with (source / ".git/info/exclude").open("a") as f:
            f.write("\n.eelinux-source.json\n")
    write_json(source / ".eelinux-source.json", {"profile": args.profile, "source": entry})
    print(source)


def source_text(source, path):
    source = Path(source)
    if (source / path).exists():
        return (source / path).read_text(errors="replace")
    if (source / ".git").exists():
        return run(["git", "-C", source, "show", f"HEAD:{path}"], capture=True)
    raise FileNotFoundError(path)


def version(source):
    content = source_text(source, "Makefile")
    parts = [re.search(rf"^{key}\s*=\s*(\d+)", content, re.M).group(1)
             for key in ("VERSION", "PATCHLEVEL", "SUBLEVEL")]
    return ".".join(parts)


def inspect_source(source, profile):
    missing = []
    for path in DRIVER_PATHS:
        try:
            source_text(source, path)
        except (FileNotFoundError, subprocess.CalledProcessError):
            missing.append(path)
    kconfig = source_text(source, "arch/mips/Kconfig")
    for symbol in ("SONY_PS2", "CPU_R5900"):
        if not re.search(rf"^config {symbol}$", kconfig, re.M):
            missing.append(f"Kconfig:{symbol}")
    rejects = [str(p.relative_to(source)) for p in Path(source).rglob("*.rej")]
    actual = version(source)
    report = {"profile": profile, "version": actual, "expected_version": LOCK[profile]["version"],
              "missing": missing, "rejects": sorted(rejects),
              "source_gate_passed": not missing and not rejects and actual == LOCK[profile]["version"],
              "hardware_validated": False,
              "native_smap_implemented": (Path(source) / "drivers/net/ethernet/ps2/ps2-smap.c").is_file(),
              "xorg_gs_backend_complete": False}
    return report


def audit(args):
    report = inspect_source(Path(args.source), args.profile)
    if args.report:
        write_json(args.report, report)
    print(json.dumps(report, indent=2))
    return 0 if report["source_gate_passed"] else 2


def verify_vendor():
    manifest = json.loads((VENDOR / "manifest.json").read_text())
    for item in manifest["files"]:
        path = VENDOR / "snapshot" / item["path"]
        if sha256(path) != item["sha256"]:
            raise RuntimeError(f"Vendored upstream source checksum mismatch: {item['path']}")
    return manifest


def port(args):
    """Import every added PS2 file; preserve and report incompatible core hunks."""
    linux_required()
    source = Path(args.source).resolve()
    if version(source) != "7.2.8":
        raise RuntimeError("PS2 transplant destination must be the real Linux 7.2.8 tree")
    if (source / ".eelinux-port.json").exists():
        raise RuntimeError("Port already attempted; preserve rejected hunks and use a fresh source for reruns")
    manifest = verify_vendor()
    imported = []
    # Copy added source in full. Existing modern files are never replaced by old files.
    for item in manifest["files"]:
        if item["change"] != "A":
            continue
        if item["path"].startswith((".github/", "README")):
            continue
        target = source / item["path"]
        if target.exists():
            raise RuntimeError(f"Upstream addition collides with modern file: {item['path']}")
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(VENDOR / "snapshot" / item["path"], target)
        imported.append(item["path"])
    result = subprocess.run(["git", "apply", "--reject", "--whitespace=nowarn",
                             str(VENDOR / "ps2-compat.patch")], cwd=source,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    log = result.stdout.decode(errors="replace")
    apply_correctness(source)
    report = {"target_version": "7.2.8", "upstream_ps2_revision": manifest["revision"],
              "imported": imported, "compatibility_patch_sha256": sha256(VENDOR / "ps2-compat.patch"),
              "apply_exit_code": result.returncode, "apply_log": log,
              "rejects": sorted(str(p.relative_to(source)) for p in source.rglob("*.rej")),
              "hardware_validated": False}
    write_json(source / ".eelinux-port.json", report)
    if args.report:
        write_json(args.report, report)
    print(f"Imported {len(imported)} complete added files; {len(report['rejects'])} files have rejected compatibility hunks")
    print("Resolve recorded .rej hunks against the modern APIs, then run audit/configure. Hardware acceptance remains required.")
    return 0 if result.returncode == 0 else 2


def config_values_text(config):
    result = {}
    for line in config.splitlines():
        if line.startswith("CONFIG_") and "=" in line:
            key, value = line.split("=", 1)
            result[key[7:]] = value
        elif line.startswith("# CONFIG_") and line.endswith(" is not set"):
            result[line[9:-11]] = "n"
    return result


def config_values(path):
    return config_values_text(Path(path).read_text())


def kernel_config_sections():
    """Read one source while keeping qualified-kernel overlays narrowly scoped."""
    lines = KERNEL_CONFIG.read_text(encoding="utf-8").splitlines(keepends=True)
    markers = [line.rstrip("\r\n") for line in lines]
    if markers.count(NETWORKING_BEGIN) != 1 or markers.count(NETWORKING_END) != 1:
        raise RuntimeError("Kernel configuration requires one marked optional-networking section")
    begin, end = markers.index(NETWORKING_BEGIN), markers.index(NETWORKING_END)
    if begin >= end:
        raise RuntimeError("Kernel optional-networking section markers are out of order")
    seen = set()
    for line in lines:
        match = re.match(r"^(?:CONFIG_([A-Z0-9_]+)=|# CONFIG_([A-Z0-9_]+) is not set$)",
                         line.rstrip("\r\n"))
        if match:
            key = match[1] or match[2]
            if key in seen:
                raise RuntimeError("Duplicate kernel configuration option: CONFIG_" + key)
            seen.add(key)
    baseline = "".join(lines[:begin] + lines[end + 1:])
    networking = "".join(lines[begin + 1:end])
    if not config_values_text(networking):
        raise RuntimeError("Kernel optional-networking section contains no options")
    return baseline, networking


def base_kernel_config():
    """Return the legacy baseline; modern builds add the marked tool choices."""
    return kernel_config_sections()[0]


def networking_config_text():
    return kernel_config_sections()[1]


def networking_config_values():
    return config_values_text(networking_config_text())


def networking_config_sha256():
    # Keep the prior networking-fragment provenance field meaningful without
    # reapplying unrelated seed choices to a qualified USB/DVD configuration.
    return hashlib.sha256(networking_config_text().encode("utf-8")).hexdigest()


def with_networking_config(config):
    """Merge the modern optional-tool profile without leaving stale disables."""
    fragment = networking_config_text()
    requested = config_values_text(fragment)
    retained = []
    for line in config.splitlines():
        match = re.match(r"^(?:CONFIG_([A-Z0-9_]+)=|# CONFIG_([A-Z0-9_]+) is not set$)", line)
        if match and (match[1] or match[2]) in requested:
            continue
        retained.append(line)
    return "\n".join(retained).rstrip() + "\n\n" + fragment.rstrip() + "\n"


def verify_networking_config(path):
    """Fail when Kconfig silently discards a requested tool backend or module."""
    values = config_values(path)
    errors = [f"CONFIG_{key}: required {value}, got {values.get(key, 'absent')}"
              for key, value in networking_config_values().items()
              if values.get(key, "n") != value]
    if errors:
        raise RuntimeError("Kconfig discarded optional networking/tool support:\n" + "\n".join(errors))


def native_gs_files(source):
    """Inventory the native framebuffer/GS ABI and implementation together."""
    source = Path(source).resolve()
    paths = sorted((source / 'drivers/video/fbdev').glob('ps2*'))
    paths += sorted((source / 'include/linux').glob('ps2-gs*.h'))
    paths += sorted((source / 'include/uapi/linux').glob('ps2-gs*.h'))
    files = {p.relative_to(source).as_posix(): sha256(p)
             for p in paths if p.is_file() and not p.is_symlink() and p.suffix.lower() in ('.c', '.h', '.s')}
    if 'drivers/video/fbdev/ps2fb-gs-device.h' not in files:
        raise RuntimeError('The native PS2 GS source is missing')
    return files


def verify_gs_reference(source, reference_output):
    """Preserve a known native GS implementation when changing media configs."""
    reference_output = Path(reference_output).resolve()
    reference_source = (reference_output / 'source').resolve()
    files = native_gs_files(source)
    if files != native_gs_files(reference_source):
        raise RuntimeError('Native GS sources differ from the selected reference kernel')
    return {'output': str(reference_output), 'source': str(reference_source),
            'kernel_sha256': sha256(reference_output / 'vmlinuz'),
            'native_GS_source_comparison_passed': True, 'source_files': files}


def install_module_payload(make, output, modules, log):
    """Install a clean, matching release without embedding build-host links."""
    modules = Path(modules).absolute()
    if modules.is_symlink():
        raise RuntimeError('Module payload directory must not be a symlink')
    modules.mkdir(parents=True, exist_ok=True)
    release = (Path(output) / 'include/config/kernel.release').read_text().strip()
    if not release or release in ('.', '..') or '/' in release or '\\' in release:
        raise RuntimeError('Unsafe kernel module release')
    destination = modules / 'lib/modules' / release
    if not destination.parent.resolve().is_relative_to(modules.resolve()):
        raise RuntimeError('Module installation escapes its payload directory')
    if destination.is_symlink():
        raise RuntimeError('Kernel release directory must not be a symlink')
    if destination.exists():
        if not destination.resolve().is_relative_to(modules.resolve()):
            raise RuntimeError('Unsafe previous module payload')
        shutil.rmtree(destination)
    subprocess.run([*make, 'INSTALL_MOD_PATH=' + str(modules), 'INSTALL_MOD_STRIP=1', 'modules_install'],
                   stdout=log, stderr=subprocess.STDOUT, check=True)
    for name in ('build', 'source'):
        link = destination / name
        if link.is_symlink():
            link.unlink()
    subprocess.run(['depmod', '-b', str(modules), release], check=True)
    files = {p.relative_to(modules).as_posix(): sha256(p)
             for p in sorted(modules.rglob('*')) if p.is_file() and not p.is_symlink()}
    return release, files


def verify_config(path):
    values = config_values(path)
    errors = [f"CONFIG_{key}: required {value}, got {values.get(key, 'absent')}"
              for key, value in REQUIRED.items() if values.get(key) != value]
    if values.get("DRM_GS", "n") != "n":
        errors.append("CONFIG_DRM_GS must be disabled: imported GS DRM has incomplete buffer/upload paths")
    for key, value in values.items():
        if (key.startswith(("USB_NET_", "NET_VENDOR_")) or key in ("USB_USBNET", "USB_RTL8152")) and value != "n":
            errors.append(f"CONFIG_{key} must be disabled: native SMAP is the only Ethernet driver")
    if errors:
        raise RuntimeError("Kconfig discarded required PS2 features:\n" + "\n".join(errors))


def make_command(args, *targets):
    return ["make", "-C", Path(args.source).resolve(), f"O={Path(args.output).resolve()}",
            "ARCH=mips", f"CROSS_COMPILE={args.cross_prefix}", "W=1", "KCFLAGS=-Werror", *targets]


def pack_early_root(directory, output):
    from initramfs import newc
    payload = newc(directory, console=True)
    if len(payload) > 4 * 1024 * 1024:
        raise RuntimeError(f"Unpacked early initramfs archive exceeds 4 MiB budget ({len(payload)} bytes)")
    path = Path(output) / "early-root.cpio"
    path.write_bytes(payload)
    return path


def kernel_command_line(root_device, video_mode="576p"):
    if (not root_device.startswith("/dev/") or ".." in root_device or
            any(c.isspace() or c in '\\"' for c in root_device)):
        raise RuntimeError("Root device must be an explicit /dev/ path without whitespace or quotes")
    from video import BOOT_MODES
    modes = {name: (' video=ps2fb:' + spec if spec else '') for name, spec in BOOT_MODES.items()}
    if video_mode not in modes:
        raise RuntimeError("Unknown video mode; use a profile from configs/video-modes.json")
    return (f"console=ttyS0,115200 console=tty0 root={root_device} "
            f"rootfstype=ext4 rw rootwait{modes[video_mode]}")


def configure(args):
    linux_required()
    report = inspect_source(Path(args.source), args.profile)
    if not report["source_gate_passed"]:
        raise RuntimeError("Source has incomplete PS2 integration or rejected port hunks. "
                           "Run audit; resolve source defects before configuring a boot image.")
    for tool in ("gcc", "ld", "objcopy", "strip"):
        if not shutil.which(args.cross_prefix + tool):
            raise RuntimeError(f"Missing R5900 kernel compiler tool: {args.cross_prefix + tool}")
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    initramfs = Path(args.initramfs).resolve()
    if not (initramfs / "init").is_file():
        raise RuntimeError("--initramfs must contain a static Linux R5900 /init executable")
    seed = base_kernel_config()
    if args.profile == "7.2.8":
        seed = seed.replace("CONFIG_BASE_SMALL=1", "CONFIG_BASE_SMALL=y")
        # Modern fbdev separates the /dev/fb0 interface from the console core.
        seed += "\nCONFIG_HID_SUPPORT=y\nCONFIG_FB_DEVICE=y\n"
        seed = with_networking_config(seed)
    archive = pack_early_root(initramfs, output)
    seed += f'\nCONFIG_INITRAMFS_SOURCE="{archive}"\n'
    seed = re.sub(r'^CONFIG_CMDLINE=.*$',
                  f'CONFIG_CMDLINE="{kernel_command_line(args.root_device, args.video_mode)}"',
                  seed, flags=re.M)
    (output / "eelinux-seed.config").write_text(seed)
    run(make_command(args, f"KCONFIG_ALLCONFIG={output / 'eelinux-seed.config'}", "allnoconfig"))
    run(make_command(args, "olddefconfig"))
    verify_config(output / ".config")
    if args.profile == "7.2.8":
        verify_networking_config(output / ".config")


def module_manifest(modules_dir, roots):
    modules_dir = Path(modules_dir)
    deps = {}
    for line in (modules_dir / "modules.dep").read_text().splitlines():
        path, dependencies = line.split(":", 1)
        for name in [path, *dependencies.split()]:
            if Path(name).is_absolute() or ".." in Path(name).parts or "\\" in name:
                raise RuntimeError(f"Unsafe module dependency path: {name}")
        deps[path] = dependencies.split()
    names = {Path(path).stem.replace("-", "_"): path for path in deps}
    builtins_file = modules_dir / "modules.builtin"
    builtins = ({Path(path).stem.replace("-", "_") for path in builtins_file.read_text().splitlines()}
                if builtins_file.is_file() else set())
    ordered, active, done = [], set(), set()

    def visit(path):
        if path in done:
            return
        if path in active:
            raise RuntimeError(f"Kernel module dependency cycle: {path}")
        if path not in deps or not (modules_dir / path).is_file():
            raise RuntimeError(f"Missing early boot module: {path}")
        active.add(path)
        for dependency in deps[path]:
            visit(dependency)
        active.remove(path)
        done.add(path)
        ordered.append(path)

    for name in roots:
        key = name.replace("-", "_")
        if key in builtins:
            continue
        if key not in names:
            raise RuntimeError(f"Early boot module was not built: {name}")
        visit(names[key])
    return ordered


def elf32_mips(path):
    data = Path(path).read_bytes()
    if len(data) < 52 or data[:7] != b"\x7fELF\x01\x01\x01":
        raise RuntimeError(f"Expected ELF32 little-endian executable: {path}")
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    if header[2] != 8:
        raise RuntimeError(f"Expected MIPS machine in {path}")
    return data, header


def validate_static_init(path):
    data, header = elf32_mips(path)
    if header[1] != 2:
        raise RuntimeError("Early /init must be a static ELF executable (ET_EXEC)")
    phoff, phentsize, phnum = header[5], header[9], header[10]
    if not phnum or phentsize < 32 or phoff + phentsize * phnum > len(data):
        raise RuntimeError("Early /init has no valid program header table")
    executable_entry = False
    for index in range(phnum):
        segment = struct.unpack_from("<IIIIIIII", data, phoff + index * phentsize)
        kind, offset, address, _, filesz, memsz, flags, _ = segment
        if kind == 3:
            raise RuntimeError("Early /init has PT_INTERP; it must be static and self-contained")
        if kind == 1:
            if filesz > memsz or offset + filesz > len(data):
                raise RuntimeError("Early /init has an invalid load segment")
            if flags & 1 and address <= header[4] < address + filesz:
                executable_entry = True
    if not executable_entry:
        raise RuntimeError("Early /init entry is outside its executable load segments")


def apply_correctness(source):
    patch = ROOT / "kernel/patches/driver-correctness.patch"
    check = subprocess.run(["git", "apply", "--check", str(patch)], cwd=source,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if check.returncode == 0:
        run(["git", "apply", str(patch)], cwd=source)
    else:
        # An exact already-applied patch is allowed; unrelated changes fail closed.
        reverse = subprocess.run(["git", "apply", "--reverse", "--check", str(patch)], cwd=source,
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if reverse.returncode:
            raise RuntimeError("Driver correctness patch neither applies nor is present exactly; inspect source edits")


def initramfs_archive_bytes(output, limit=4 * 1024 * 1024):
    """Count the actual unpacked newc archive, including names and metadata."""
    directory = Path(output) / "usr"
    # Linux 7.2 embeds initramfs_inc_data. Older out-of-tree builds can leave
    # a stale initramfs_data.cpio alongside it; that file is not the payload.
    path = next((directory / name for name in
                 ("initramfs_inc_data", "initramfs_data.cpio.gz", "initramfs_data.cpio")
                 if (directory / name).is_file()), None)
    if path is None:
        raise RuntimeError("Expected kernel initramfs archive is absent")
    with path.open("rb") as stream:
        magic = stream.read(6)
    if magic.startswith(b"\x1f\x8b"):
        with gzip.open(path, "rb") as stream:
            size = len(stream.read(limit + 1))
    elif magic in (b"070701", b"070702"):
        size = path.stat().st_size
    else:
        raise RuntimeError("Expected gzip or uncompressed newc kernel initramfs archive")
    if size > limit:
        raise RuntimeError(f"Unpacked early initramfs archive exceeds 4 MiB budget ({size} bytes or more)")
    return size


def boot_layout(kernel_path, compressed_path):
    """Verify the fixed PS2 decompressor/output regions using actual ELF loads."""
    def physical(address):
        # R5900 unmapped KSEG0/KSEG1 aliases share the low 29 physical bits.
        return address & 0x1FFFFFFF if 0x80000000 <= address < 0xC0000000 else address

    def ranges(path):
        data, header = elf32_mips(path)
        phoff, phsize, phnum = header[5], header[9], header[10]
        if (header[1] != 2 or header[3] != 1 or header[8] != 52 or not phnum or phsize < 32
                or phoff + phsize * phnum > len(data)):
            raise RuntimeError(f"Invalid boot ELF executable/program headers: {path}")
        loaded = []
        entry_mapped = False
        for index in range(phnum):
            kind, offset, address, paddr, filesz, memsz, flags, _ = struct.unpack_from(
                "<8I", data, phoff + index * phsize)
            if kind == 3:
                raise RuntimeError(f"Boot ELF must not require a dynamic interpreter: {path}")
            if kind != 1:
                continue
            start = physical(paddr)
            end = start + memsz
            if (not memsz or filesz > memsz or offset + filesz > len(data)
                    or physical(address) != start or end > 32 * 1024 * 1024):
                raise RuntimeError(f"Boot ELF has invalid load segment or exceeds PS2 32 MiB RAM: {path}")
            if flags & 1 and start <= physical(header[4]) < start + filesz:
                entry_mapped = True
            loaded.append((start, end))
        if not loaded or not entry_mapped:
            raise RuntimeError(f"Boot ELF entry is outside its executable load segments: {path}")
        return loaded

    kernel_ranges = ranges(kernel_path)
    compressed_ranges = ranges(compressed_path)
    if min(start for start, _ in kernel_ranges) != 0x10000:
        raise RuntimeError("PS2 kernel load address must be the reviewed physical 64 KiB")
    if min(start for start, _ in compressed_ranges) != 0x800000:
        raise RuntimeError("PS2 compressed ELF load address must be the reviewed physical 8 MiB")
    for start, end in kernel_ranges:
        for cstart, cend in compressed_ranges:
            if start < cend and cstart < end:
                raise RuntimeError("Decompressed kernel overlaps compressed ELF/heap/stack; shrink early initramfs")
    return {"ram_bytes": 32 * 1024 * 1024,
            "kernel_physical_ranges": kernel_ranges,
            "compressed_physical_ranges": compressed_ranges,
            "decompressor_overlap_check": "passed"}


def build(args):
    linux_required()
    if not inspect_source(Path(args.source), args.profile)["source_gate_passed"]:
        raise RuntimeError("Source integration audit failed; resolve rejected port hunks before building a boot image")
    validate_static_init(Path(args.initramfs).resolve() / "init")
    firmware = Path(args.firmware).resolve()
    validate_firmware(firmware)
    from iop_dev9 import validate_overlay_firmware
    validate_overlay_firmware(firmware)
    apply_correctness(Path(args.source).resolve())
    configure(args)
    if not shutil.which("depmod"):
        raise RuntimeError("Host depmod is required for a verified early module dependency order")
    run(make_command(args, f"-j{args.jobs}", "vmlinux", "modules"))
    modules_root = Path(args.modules_root).resolve()
    modules_root.mkdir(parents=True, exist_ok=True)
    run(make_command(args, f"INSTALL_MOD_PATH={modules_root}", "INSTALL_MOD_STRIP=1", "modules_install"))
    release_output = run(make_command(args, "--no-print-directory", "-s", "kernelrelease"), capture=True)
    release = release_output.strip().splitlines()[-1]
    run(["depmod", "-b", modules_root, release])
    modules_dir = modules_root / "lib/modules" / release
    roots = EARLY + (["iop-dev9", "pata_ps2"] if args.boot_storage == "ata" else [])
    early = module_manifest(modules_dir, roots)
    initramfs = Path(args.initramfs).resolve()
    manifest = []
    for path in early:
        relative = Path("lib/modules") / release / path
        target = initramfs / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(modules_dir / path, target)
        manifest.append("/" + relative.as_posix())
    (initramfs / "etc").mkdir(exist_ok=True)
    (initramfs / "etc/eelinux-modules").write_text("\n".join(manifest) + "\n")
    for destination in (initramfs / "lib/firmware/ps2", modules_root / "lib/firmware/ps2"):
        destination.mkdir(parents=True, exist_ok=True)
        for name in LOCK["iopmod"]["modules"]:
            shutil.copyfile(firmware / f"{name}.irx", destination / f"{name}.irx")
        shutil.copyfile(firmware / "manifest.json", destination / "manifest.json")
        validate_firmware(destination)
    init_bytes = sum(p.stat().st_size for p in initramfs.rglob("*") if p.is_file() and not p.is_symlink())
    if init_bytes > 4 * 1024 * 1024:
        raise RuntimeError(f"Early initramfs exceeds 4 MiB budget ({init_bytes} bytes); keep GNU userland on disk")
    # Rebuild after installed early modules/firmware have changed the embedded initramfs.
    pack_early_root(initramfs, Path(args.output).resolve())
    run(make_command(args, f"-j{args.jobs}", "vmlinuz"))
    output = Path(args.output).resolve()
    archive_bytes = initramfs_archive_bytes(output)
    image = output / "vmlinuz"
    data, _ = elf32_mips(image)
    if len(data) > 4 * 1024 * 1024:
        raise RuntimeError(f"Compressed boot ELF exceeds conservative 4 MiB budget: {len(data)} bytes")
    layout = boot_layout(output / "vmlinux", image)
    artifact = Path(args.artifact).resolve()
    artifact.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(image, artifact)
    write_json(artifact.with_suffix(".json"), {"profile": args.profile, "kernel_release": release,
               "source_revision": LOCK[args.profile].get("revision"), "image_sha256": sha256(artifact),
               "image_bytes": len(data), "initramfs_file_bytes": init_bytes, "early_modules": manifest,
               "initramfs_archive_bytes": archive_bytes,
               "boot_layout": layout,
               "hardware_validated": False, "native_smap_implemented": True,
               "native_smap_builtin": True, "root_filesystem": "ext4"})
    print(f"Boot ELF: {artifact}; real-hardware validation pending")


def firmware(args):
    """Compile the current DEV9/SMAP firmware from the preserved GPL import."""
    linux_required()
    from iop_dev9 import build as build_dev9_firmware
    build_dev9_firmware(Path(args.source), Path(args.destination), args.cross_prefix, args.jobs)


def legacy_firmware(args):
    """Historical firmware recipe, retained only for reproducing old audits."""
    linux_required()
    source = fetch_git(LOCK["iopmod"], args.source)
    for tool in ("gcc", "ld", "objcopy"):
        if not shutil.which(args.cross_prefix + tool):
            raise RuntimeError(f"Missing ps2dev IOP tool: {args.cross_prefix + tool}")
    # Override the target-only flags: a bare-metal ps2dev compiler deliberately
    # has no glibc headers, and the host IRX linker must retain its host headers.
    target_flags = ("-O0 -march=r3000 -EL -msoft-float -fomit-frame-pointer "
                    "-fno-pic -mno-abicalls -fno-common -ffreestanding -static "
                    "-fno-strict-aliasing -nostdlib -mlong-calls -mno-gpopt "
                    "-mno-shared -G0 -ffunction-sections -fdata-sections "
                    f"-Wall -Iinclude -I{ROOT / 'kernel/iop-compat'}")
    formats = run([args.cross_prefix + "objdump", "-i"], capture=True)
    linker = source / "script/iop.ld"
    if "elf32-tradlittlemips" not in formats:
        if "elf32-littlemips" not in formats:
            raise RuntimeError("ps2dev binutils has no supported little-endian MIPS ELF32 output format")
        linker = source / ".eelinux-iop.ld"
        template = (source / "script/iop.ld").read_text()
        linker.write_text(template.replace("elf32-tradlittlemips", "elf32-littlemips"))
        with (source / ".git/info/exclude").open("a") as f:
            f.write("\n.eelinux-iop.ld\n")
    run(["make", "-C", source, f"-j{args.jobs}", f"CROSS_COMPILE={args.cross_prefix}",
         f"IOP_CFLAGS={target_flags}", f"MODULE_LD={linker}"])
    dest = Path(args.destination).resolve()
    dest.mkdir(parents=True, exist_ok=True)
    hashes = {}
    for name in LOCK["iopmod"]["modules"]:
        image = source / "module" / f"{name}.irx"
        elf32_mips(image)
        shutil.copyfile(image, dest / image.name)
        hashes[image.name] = sha256(dest / image.name)
    write_json(dest / "manifest.json", {"source": LOCK["iopmod"], "compiler": args.cross_prefix,
                                         "firmware_sha256": hashes})
    validate_firmware(dest)


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="command", required=True)
    for name in ("fetch", "audit", "configure", "build", "port"):
        a = sub.add_parser(name)
        a.add_argument("--source", required=True)
        if name != "port":
            a.add_argument("--profile", choices=("baseline", "7.2.8"), default="7.2.8")
        if name in ("audit", "port"):
            a.add_argument("--report")
        if name == "fetch":
            a.add_argument("--cache", default=str(ROOT / "build/downloads"))
        if name in ("configure", "build"):
            a.add_argument("--output", required=True)
            a.add_argument("--initramfs", required=True)
            a.add_argument("--cross-prefix", default=os.environ.get("CROSS_COMPILE", "mipsr5900el-unknown-linux-gnu-"))
            a.add_argument("--root-device", default="/dev/sda1")
            from video import BOOT_MODES
            a.add_argument("--video-mode", choices=tuple(BOOT_MODES), default="576p",
                           help="GS timing; default progressive 576p/50 Hz, pal selects interlaced recovery")
        if name == "build":
            a.add_argument("--firmware", required=True, help="directory containing six compiled IOP .irx objects")
            a.add_argument("--modules-root", required=True, help="disk root filesystem staging directory")
            a.add_argument("--boot-storage", choices=("usb", "ata"), default="usb")
            a.add_argument("--jobs", type=int, default=min(2, os.cpu_count() or 1))
            a.add_argument("--artifact", default=str(ROOT / "dist/eelinux-ps2.elf"))
    a = sub.add_parser("firmware")
    a.add_argument("--source", required=True)
    a.add_argument("--destination", required=True)
    a.add_argument("--cross-prefix", default=os.environ.get("IOP_CROSS_COMPILE", "mipsel-none-elf-"))
    a.add_argument("--jobs", type=int, default=min(2, os.cpu_count() or 1))
    return p


def main():
    args = parser().parse_args()
    try:
        return globals()[args.command](args) or 0
    except (RuntimeError, OSError, subprocess.CalledProcessError, ValueError) as exc:
        print(f"kernel: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
