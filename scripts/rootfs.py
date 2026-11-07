#!/usr/bin/env python3
"""Assemble a checked MIPS GNU staging tree into an unprivileged ext4 image.

Run on Linux; fakeroot supplies root ownership and essential /dev entries. All
filesystem formatting happens in a newly created regular file in a temporary
directory. Neither root privileges nor a device name are accepted.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import struct
import subprocess
import sys
import tarfile
import tempfile

from userland import BuildError as AbiError, reject_busybox, validate_elf
from iop_firmware import FirmwareError, validate_firmware

REPO = Path(__file__).resolve().parent.parent
UUID = "e30eebbb-9122-536d-b9b4-0950c22e8c24"
DEFAULT_EPOCH = 946684800

# Keep real GNU utilities on the disk root, with dhcpcd for DHCP.
TOOL_COVERAGE = json.loads((REPO / "configs/gnu-tools.json").read_text(encoding="utf-8"))
GNU_COMMANDS = sorted({name for names in TOOL_COVERAGE["gnu"].values() for name in names.split()})
SYSTEM_COMMANDS = sorted({"less", "ps2-video", *(name for names in TOOL_COVERAGE["other_required_tools"].values()
                                  for name in names.split())})
DESKTOP_COMMANDS = [*"Xorg startx xinit twm xauth xterm xrdb xset ps2-desktop".split(),
                    *(name for names in TOOL_COVERAGE.get("desktop_required_tools", {}).values()
                      for name in names.split())]
CANONICAL = {
    **{name: f"/bin/{name}" for name in (
        "bash mount umount hostname mkdir chmod chown sleep sync login mountpoint"
    ).split()},
    **{name: f"/usr/bin/{name}" for name in (
        "mktemp mv rm grep sleep startx twm xterm xrdb"
    ).split()},
    **{name: f"/sbin/{name}" for name in (
        "init telinit runlevel sulogin killall5 halt reboot shutdown swapon swapoff agetty ip"
    ).split()},
    "dhcpcd": "/sbin/dhcpcd",
    "loadkeys": "/usr/bin/loadkeys",
    "Xorg": "/usr/bin/Xorg",
}
DEVICE_NODES = {
    "console": (5, 1, 0o600), "null": (1, 3, 0o666),
    "zero": (1, 5, 0o666), "tty": (5, 0, 0o666),
    "tty1": (4, 1, 0o600), "random": (1, 8, 0o666),
    "urandom": (1, 9, 0o666),
}


class BuildError(Exception):
    pass


def tool_catalog(desktop: bool = True, development: bool = False) -> str:
    """Export the audited command contract for the lightweight Bash browser."""
    groups = {**TOOL_COVERAGE["gnu"], **TOOL_COVERAGE["other_required_tools"]}
    if desktop:
        groups.update(TOOL_COVERAGE.get("desktop_required_tools", {}))
    if development:
        groups.update(TOOL_COVERAGE.get("development_tools", {}))
    return "# Upstream suite\tRequired commands\n" + "".join(
        suite + "\t" + " ".join(commands.split()) + "\n"
        for suite, commands in sorted(groups.items()))


def guest_path(root: Path, name: str) -> Path:
    """Resolve symlinks *inside* a guest root without following host absolute links."""
    pending = list(PurePosixPath("/" + name.lstrip("/")).parts[1:])
    components: list[str] = []
    links = 0
    while pending:
        part = pending.pop(0)
        if part in ("", "."):
            continue
        if part == "..":
            if not components:
                raise BuildError(f"guest path escapes its root: {name}")
            components.pop()
            continue
        candidate = root.joinpath(*components, part)
        if candidate.is_symlink():
            links += 1
            if links > 40:
                raise BuildError(f"symlink loop: {name}")
            target = os.readlink(candidate)
            if target.startswith("/"):
                components = []
            pending = list(PurePosixPath(target).parts) + pending
            if pending and pending[0] == "/":
                pending.pop(0)
        else:
            components.append(part)
    return root.joinpath(*components)


def install_file(root: Path, guest: str, source: Path | None = None,
                 content: str | None = None, mode: int = 0o644) -> Path:
    parent = guest_path(root, str(PurePosixPath(guest).parent))
    parent.mkdir(parents=True, exist_ok=True)
    target = parent / PurePosixPath(guest).name
    if target.is_symlink():
        target.unlink()
    if target.exists() and not target.is_file():
        raise BuildError(f"overlay destination is not a regular file: {guest}")
    if source is not None:
        shutil.copyfile(source, target)
    else:
        target.write_text(content or "", encoding="utf-8", newline="\n")
    target.chmod(mode)
    return target


def install_symlink(root: Path, guest: str, link: str) -> None:
    parent = guest_path(root, str(PurePosixPath(guest).parent))
    parent.mkdir(parents=True, exist_ok=True)
    target = parent / PurePosixPath(guest).name
    if target.is_symlink() or target.is_file():
        target.unlink()
    elif target.exists():
        raise BuildError(f"cannot replace directory with symlink: {guest}")
    target.symlink_to(link)


def find_command(root: Path, command: str) -> str | None:
    for prefix in ("/bin", "/sbin", "/usr/bin", "/usr/sbin", "/usr/lib/xorg"):
        name = f"{prefix}/{command}"
        path = guest_path(root, name)
        if path.is_file() and path.stat().st_mode & 0o111:
            return name
    return None


def elf_info(path: Path) -> dict | None:
    """Read MIPS ELF32 dynamic dependencies without executing target programs."""
    with path.open("rb") as stream:
        data = stream.read()
    if not data.startswith(b"\x7fELF"):
        return None
    if len(data) < 52 or data[4:6] != b"\x01\x01":
        raise BuildError(f"expected little-endian ELF32: {path.name}")
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    if header[2] != 8:
        raise BuildError(f"expected MIPS target ELF, machine={header[2]}: {path.name}")
    # Directly supplied staging trees need the same CPU/float checks as built
    # packages; ELF32 and o32 alone also admit incompatible MIPS32/hard-float.
    try:
        validate_elf(path)
    except AbiError as exc:
        raise BuildError(str(exc)) from exc
    offset, entry_size, count = header[5], header[9], header[10]
    if count and entry_size < 32:
        raise BuildError(f"invalid ELF program table: {path.name}")
    programs = []
    for index in range(count):
        at = offset + index * entry_size
        if at + 32 > len(data):
            raise BuildError(f"truncated ELF: {path.name}")
        program = struct.unpack_from("<IIIIIIII", data, at)
        if program[1] + program[4] > len(data):
            raise BuildError(f"truncated ELF segment: {path.name}")
        programs.append(program)
    interpreter = None
    needed_offsets = []
    search_offsets = {}
    string_address = None
    string_size = 0
    for program in programs:
        kind, file_offset, _, _, file_size, *_ = program
        if kind == 3:
            interpreter = data[file_offset:file_offset + file_size].rstrip(b"\0").decode("ascii")
        elif kind == 2:
            for at in range(file_offset, file_offset + file_size, 8):
                if at + 8 > len(data):
                    raise BuildError(f"truncated ELF dynamic table: {path.name}")
                tag, value = struct.unpack_from("<II", data, at)
                if tag == 0:
                    break
                if tag == 1:
                    needed_offsets.append(value)
                elif tag in (15, 29):  # DT_RPATH / DT_RUNPATH
                    search_offsets[tag] = value
                elif tag == 5:
                    string_address = value
                elif tag == 10:
                    string_size = value
    needed = []
    search_paths = []
    if needed_offsets or search_offsets:
        string_offset = None
        for program in programs:
            if program[0] == 1 and string_address is not None:
                if program[2] <= string_address < program[2] + program[4]:
                    string_offset = program[1] + string_address - program[2]
                    break
        if string_offset is None or string_offset + string_size > len(data):
            raise BuildError(f"invalid ELF string table: {path.name}")
        strings = data[string_offset:string_offset + string_size]
        for at in needed_offsets:
            end = strings.find(b"\0", at)
            if end < 0:
                raise BuildError(f"invalid ELF dependency: {path.name}")
            needed.append(strings[at:end].decode("ascii"))
        if search_offsets:
            at = search_offsets.get(29, search_offsets.get(15))
            end = strings.find(b"\0", at)
            if end < 0:
                raise BuildError(f"invalid ELF library search path: {path.name}")
            search_paths = strings[at:end].decode("ascii").split(":")
    result = {"interpreter": interpreter, "needed": needed}
    if search_paths:
        result["search_paths"] = search_paths
    return result


def has_runtime_library(root: Path, guest_file: str, library: str,
                        info: dict, system_libraries: set[str]) -> bool:
    if library in system_libraries:
        return True
    # gconv and other self-contained modules use an explicit $ORIGIN RPATH.
    # Resolve it inside the guest; an unrelated copy elsewhere is insufficient.
    origin = str(PurePosixPath("/" + guest_file.lstrip("/")).parent)
    for directory in info.get("search_paths", []):
        directory = directory.replace("${ORIGIN}", origin).replace("$ORIGIN", origin)
        if directory.startswith("/") and "$" not in directory:
            if guest_path(root, directory + "/" + library).is_file():
                return True
    return False


def check_staging(root: Path, desktop: bool = True) -> dict[str, str]:
    iop_files = set()
    iop_directory = guest_path(root, "/lib/firmware/ps2")
    if iop_directory.is_dir() and (list(iop_directory.glob("*.irx"))
                                    or (iop_directory / "manifest.json").exists()):
        try:
            iop_files = validate_firmware(iop_directory)
        except FirmwareError as exc:
            raise BuildError(str(exc)) from exc
    try:
        reject_busybox(root)
    except AbiError as exc:
        raise BuildError(str(exc)) from exc
    commands = GNU_COMMANDS + SYSTEM_COMMANDS + (DESKTOP_COMMANDS if desktop else [])
    located = {command: find_command(root, command) for command in commands}
    missing = [command for command, path in located.items() if path is None]
    if missing:
        raise BuildError("incomplete staged userland; missing commands: " + ", ".join(missing))
    if elf_info(guest_path(root, located["bash"])) is None:
        raise BuildError("bash must be a real target ELF executable")
    libc = guest_path(root, "/lib/libc.so.6")
    if not libc.is_file():
        libc = guest_path(root, "/usr/lib/libc.so.6")
    if not libc.is_file() or elf_info(libc) is None:
        raise BuildError("GNU glibc libc.so.6 is required in /lib or /usr/lib")
    libraries: set[str] = set()
    for prefix in ("/lib", "/usr/lib"):
        folder = guest_path(root, prefix)
        if folder.is_dir():
            for item in folder.iterdir():
                if guest_path(root, prefix + "/" + item.name).is_file():
                    libraries.add(item.name)
    dependency_errors = []
    for directory, dirs, files in os.walk(root, followlinks=False):
        dirs.sort()
        for filename in dirs:
            candidate = Path(directory) / filename
            if candidate.is_symlink():
                guest_path(root, "/" + candidate.relative_to(root).as_posix())
        for filename in sorted(files):
            candidate = Path(directory) / filename
            if candidate.is_symlink():
                # Validate every symlink in guest coordinates before packaging.
                guest_path(root, "/" + candidate.relative_to(root).as_posix())
                continue
            if not candidate.is_file():
                raise BuildError(f"staging contains a special file: {candidate.relative_to(root)}")
            if candidate in iop_files:
                continue  # Already checked as hashed R3000 IOP firmware, not GNU userland.
            info = elf_info(candidate)
            if info is None:
                continue
            label = candidate.relative_to(root).as_posix()
            interpreter = info["interpreter"]
            if interpreter and (not interpreter.startswith("/") or not guest_path(root, interpreter).is_file()):
                dependency_errors.append(f"{label}: missing interpreter {interpreter}")
            for library in info["needed"]:
                if not has_runtime_library(root, label, library, info, libraries):
                    dependency_errors.append(f"{label}: missing runtime library {library}")
    if dependency_errors:
        raise BuildError("incomplete runtime library closure:\n" + "\n".join(dependency_errors[:30]))
    if desktop:
        native = guest_path(root, "/usr/lib/xorg/modules/drivers/ps2_drv.so").is_file()
        # Keep older diagnostic staging usable; production media separately
        # requires native GS and removes the legacy framebuffer module.
        driver = "/usr/lib/xorg/modules/drivers/" + ("ps2_drv.so" if native else "fbdev_drv.so")
        for required in (
            "/usr/lib/eelinux/ps2-xorg-prepare",
            driver,
            "/usr/lib/xorg/modules/input/evdev_drv.so",
            "/usr/lib/xorg/modules/input/mouse_drv.so",
            "/usr/share/X11/xkb/rules/base",
            "/usr/share/fonts/X11/misc/fonts.dir",
        ):
            if not guest_path(root, required).is_file():
                raise BuildError(f"desktop staging missing {required}")
    return {command: path for command, path in located.items() if path is not None}


def validate_login_policy(root: Path) -> None:
    """Check PAM's runtime-loaded policy and modules after applying the overlay."""
    login = find_command(root, "login")
    info = elf_info(guest_path(root, login)) if login else None
    if not info or "libpam.so.0" not in info["needed"]:
        return
    done, active = set(), set()

    def policy(service: str) -> None:
        if not re.fullmatch(r"[a-zA-Z0-9_-]+", service):
            raise BuildError(f"unsafe PAM include: {service}")
        if service in active:
            raise BuildError(f"PAM include cycle: {service}")
        if service in done:
            return
        config = guest_path(root, "/etc/pam.d/" + service)
        if not config.is_file():
            raise BuildError(f"missing PAM policy: {service}")
        active.add(service)
        for raw in config.read_text().splitlines():
            line = raw.partition("#")[0].strip()
            if not line:
                continue
            match = re.fullmatch(r"(?:auth|account|password|session)\s+(\[[^\]]+\]|\S+)\s+(\S+)(?:\s+.*)?", line)
            if not match:
                raise BuildError(f"malformed PAM policy in {service}")
            control, module = match.groups()
            if control in ("include", "substack"):
                policy(module)
            elif module.startswith("/"):
                if not guest_path(root, module).is_file():
                    raise BuildError(f"missing PAM module: {module}")
            elif not any(guest_path(root, prefix + module).is_file()
                         for prefix in ("/lib/security/", "/usr/lib/security/")):
                raise BuildError(f"missing PAM module: {module}")
        active.remove(service)
        done.add(service)

    for service in ("login", "passwd"):
        policy(service)


def assemble_tree(staging: Path, target: Path, hostname: str, desktop: bool,
                  root_password_hash: str | None = None, video_mode: str = "576p",
                  swap_file_mib: int = 0) -> None:
    if target.exists() or target.is_symlink():
        raise BuildError("assembly tree must be new")
    # GNU package aliases can be hardlinks. Keep them shared in the image.
    subprocess.run(["cp", "-a", "--reflink=auto", str(staging), str(target)], check=True)
    # This warning belongs to Buildroot's unassembled target directory. Our
    # image assembler supplies ownership, devices and runtime configuration.
    marker = target / "THIS_IS_NOT_YOUR_ROOT_FILESYSTEM"
    if marker.is_file() or marker.is_symlink():
        marker.unlink()
    # Network devices are initialized by the built-in SMAP driver. Remove the
    # obsolete optional-module list when rebuilding from older staging trees.
    obsolete = guest_path(target, "/etc/eelinux-modules")
    if obsolete.is_file() or obsolete.is_symlink():
        obsolete.unlink()
    obsolete = guest_path(target, "/usr/lib/eelinux/udhcpc-hook")
    if obsolete.is_file() or obsolete.is_symlink():
        obsolete.unlink()
    # Validate source before the overlay supplies its own scripts or configuration.
    commands = check_staging(target, desktop)
    for directory in (
        "proc sys dev dev/pts dev/shm run tmp root var var/log var/lib var/tmp home "
        "mnt media etc etc/inittab.d usr/lib/eelinux usr/sbin var/lib/dhcpcd var/empty var/lib/unbound"
    ).split():
        guest_path(target, "/" + directory).mkdir(parents=True, exist_ok=True)
    guest_path(target, "/var/empty").chmod(0o755)
    for source in sorted((REPO / "rootfs").rglob("*")):
        if not source.is_file() or source.suffix == ".c":
            continue
        guest = "/" + source.relative_to(REPO / "rootfs").as_posix()
        if not desktop and (guest.startswith("/etc/X11/") or guest == "/usr/sbin/startx-ps2"):
            continue
        executable = guest.startswith(("/usr/bin/", "/usr/sbin/", "/etc/init.d/")) or guest.endswith(("/dhcpcd-hook", "/xinitrc", "/xserverrc"))
        mode = 0o755 if executable else 0o644
        if guest in ("/etc/shadow", "/etc/gshadow"):
            mode = 0o600
        install_file(target, guest, source, mode=mode)
    for command, destination in list(CANONICAL.items()) + [("sleep", "/bin/sleep"), ("chmod", "/usr/bin/chmod")]:
        if command not in commands:
            continue
        source = commands[command]
        if guest_path(target, destination) != guest_path(target, source):
            install_symlink(target, destination, os.path.relpath(source, str(PurePosixPath(destination).parent)))
    install_symlink(target, "/bin/sh", "bash")
    if desktop:
        install_symlink(target, "/usr/bin/X", "Xorg")
        install_symlink(target, "/etc/X11/xinitrc", "xinit/xinitrc")
    runlevels = json.loads((REPO / "rootfs/etc/rc.d/runlevels.json").read_text(encoding="utf-8"))
    for level, links in runlevels.items():
        if level not in ("S", "0", "1", "2", "3", "4", "5", "6"):
            raise BuildError("invalid runlevel definition")
        guest_path(target, f"/etc/rc.d/rc{level}.d").mkdir(parents=True, exist_ok=True)
        for name, service in links.items():
            if not re.fullmatch(r"[SK][0-9]{2}[a-zA-Z0-9_-]+", name) or not re.fullmatch(r"[a-zA-Z0-9_-]+", service):
                raise BuildError("invalid service link definition")
            if not guest_path(target, f"/etc/init.d/{service}").is_file():
                raise BuildError(f"runlevel service does not exist: {service}")
            install_symlink(target, f"/etc/rc.d/rc{level}.d/{name}", f"../../init.d/{service}")
    install_file(target, "/etc/hostname", content=hostname + "\n")
    from video import BOOT_MODES, MODES
    if video_mode not in BOOT_MODES:
        raise BuildError("unknown startup video mode")
    rgb = any(mode['id'] == video_mode and mode['family'] == 'rgb-sog' for mode in MODES)
    install_file(target, "/etc/default/ps2-video",
                 content=f"# Preview on the connected display before changing this preference.\nMODE={video_mode}\nRGB_SOG={int(rgb)}\n")
    install_file(target, "/etc/hosts", content=f"127.0.0.1 localhost {hostname}\n::1 localhost\n")
    if root_password_hash is not None:
        # A zero last-change date forces a password change at the first login.
        # Use the reproducible project epoch for explicitly provisioned accounts.
        day = DEFAULT_EPOCH // 86400
        shadow = guest_path(target, "/etc/shadow").read_text(encoding="utf-8")
        shadow = re.sub(r"^root:[^\n]*$", f"root:{root_password_hash}:{day}:0:99999:7:::", shadow, flags=re.M)
        install_file(target, "/etc/shadow", content=shadow, mode=0o600)
    install_symlink(target, "/etc/resolv.conf", "/run/resolv.conf")
    for destination, link in (("/var/run", "/run"), ("/var/lock", "/run/lock"), ("/var/log", "/run/log")):
        old = guest_path(target, str(PurePosixPath(destination).parent)) / PurePosixPath(destination).name
        if old.is_dir() and not old.is_symlink():
            shutil.rmtree(old)
        install_symlink(target, destination, link)
    guest_path(target, "/tmp").chmod(0o1777)
    guest_path(target, "/var/tmp").chmod(0o1777)
    if swap_file_mib:
        if not 16 <= swap_file_mib <= 1024:
            raise BuildError("swap file must be 16..1024 MiB")
        swap = guest_path(target, "/swapfile")
        if swap.exists() or swap.is_symlink():
            raise BuildError("staging already contains a swapfile; use a clean staging tree")
        # Nonzero pages keep mke2fs's directory population from creating sparse
        # holes. A sparse swapfile would be rejected by swapon on the console.
        with swap.open("xb") as stream:
            for _ in range(swap_file_mib):
                stream.write(b"\x5a" * (1024 * 1024))
        swap.chmod(0o600)
        subprocess.run(["mkswap", "--pagesize", "4096", str(swap)], check=True,
                       stdout=subprocess.DEVNULL)
        install_file(target, "/etc/default/swap", content="SWAP_DEVICE=/swapfile\n")
    guest_path(target, "/root").chmod(0o700)
    for source in (REPO / "rootfs/etc/skel").iterdir():
        if desktop:
            install_file(target, "/root/" + source.name, source)
    manifest = {
        "target": "mipsel-linux-gnu-o32", "hostname": hostname,
        "desktop": desktop, "commands": commands, "video_mode": video_mode,
        "warning": "Hardware support requires the external PS2 kernel/IOP port; an image alone cannot provide it.",
    }
    install_file(target, "/usr/share/eelinux/rootfs-manifest.json", content=json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    install_file(target, "/usr/share/eelinux/gnu-tools.json", source=REPO / "configs/gnu-tools.json")
    install_file(target, "/usr/share/eelinux/tools.tsv",
                 content=tool_catalog(desktop, development=find_command(target, "gcc") is not None))
    validate_login_policy(target)
    if not guest_path(target, "/usr/share/keymaps/uk.map").is_file():
        raise BuildError("missing English UK console map; build/install scripts/keyboard.py first")


def normalize_tree(root: Path, epoch: int) -> int:
    count = 0
    for directory, dirs, files in os.walk(root, followlinks=False):
        for name in sorted(dirs + files):
            item = Path(directory) / name
            os.chown(item, 0, 0, follow_symlinks=False)
            os.utime(item, (epoch, epoch), follow_symlinks=False)
            count += 1
    os.chown(root, 0, 0)
    os.utime(root, (epoch, epoch))
    return count


def create_tar(root: Path, archive: Path, epoch: int) -> None:
    def metadata(info: tarfile.TarInfo) -> tarfile.TarInfo:
        info.uid = info.gid = 0
        info.uname = info.gname = "root"
        info.mtime = epoch
        info.pax_headers = {}
        return info
    with archive.open("xb") as output:
        with tarfile.open(fileobj=output, mode="w", format=tarfile.GNU_FORMAT) as tar:
            tar.add(root, arcname=".", recursive=False, filter=metadata)
            for directory, dirs, files in os.walk(root, followlinks=False):
                dirs.sort()
                for name in sorted(dirs + files):
                    item = Path(directory) / name
                    tar.add(item, arcname="./" + item.relative_to(root).as_posix(), recursive=False, filter=metadata)


def image_worker(folder: Path) -> None:
    config = json.loads((folder / "config.json").read_text(encoding="utf-8"))
    tree = folder / "tree"
    for name, (major, minor, mode) in DEVICE_NODES.items():
        target = guest_path(tree, "/dev") / name
        if target.is_symlink() or target.is_file():
            target.unlink()
        os.mknod(target, stat.S_IFCHR | mode, os.makedev(major, minor))
    count = normalize_tree(tree, config["epoch"])
    create_tar(tree, folder / "rootfs.tar", config["epoch"])
    filesystem = config.get("filesystem", "ext4")
    image = folder / f"rootfs.{filesystem}"
    with image.open("xb") as output:
        output.truncate(config["size_mib"] * 1024 * 1024)
    environment = os.environ.copy()
    environment.update({"SOURCE_DATE_EPOCH": str(config["epoch"]), "E2FSPROGS_FAKE_TIME": str(config["epoch"]), "LC_ALL": "C"})
    # A 4 MiB internal journal and 4 KiB blocks fit the PS2 memory budget.
    # Explicit features avoid changes caused by the host's mke2fs.conf.
    format_args = (["-b", "4096", "-I", "256", "-J", "size=4", "-O",
                    "none,has_journal,extent,filetype,sparse_super,large_file,dir_index,extra_isize,metadata_csum"]
                   if filesystem == "ext4" else
                   ["-b", "1024", "-I", "128", "-O", "none,filetype,sparse_super,large_file"])
    resize_options=""
    if config.get('usb_auto_expand',False):
        if filesystem!='ext4':raise BuildError('USB online expansion requires ext4')
        format_args[-1]+=",resize_inode"
        # Reserve group descriptors for the largest filesystem addressable by
        # this 512-byte-sector DOS partition layout, without large boot RAM use.
        resize_options=",resize=536870911"
    subprocess.run([
        "mke2fs", "-q", "-t", filesystem, "-F", *format_args, "-m", "0",
        "-N", str(max(16384, count * 2)), "-L", "EELINUX_ROOT", "-U", UUID,
        "-E", f"root_owner=0:0,hash_seed={UUID},lazy_itable_init=0,lazy_journal_init=0{resize_options}",
        "-d", str(tree), str(image),
    ], env=environment, check=True)
    # A freshly populated image must pass an offline check before it is delivered.
    subprocess.run(["e2fsck", "-fn", str(image)], env=environment, check=True, stdout=subprocess.DEVNULL)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--staging", type=Path, help="target GNU userland installation directory")
    parser.add_argument("--output-dir", type=Path, help="directory for rootfs.tar/rootfs.ext4")
    parser.add_argument("--filesystem", choices=("ext4", "ext2"), default="ext4",
                        help="ext4 by default; ext2 is only for reproducing historical audits")
    parser.add_argument("--size-mib", type=int, default=512)
    parser.add_argument("--swap-file-mib", type=int, default=0,
                        help="optional fully allocated disk-backed swapfile (16..1024 MiB)")
    parser.add_argument('--usb-auto-expand',action='store_true',
                        help='reserve ext4 online-growth metadata for the USB first-boot service')
    parser.add_argument("--hostname", default="ps2")
    from video import BOOT_MODES
    parser.add_argument("--video-mode", choices=tuple(BOOT_MODES), default="576p",
                        help="startup preference; 576p/50 by default, pal for interlaced recovery")
    parser.add_argument("--root-password-hash-file", type=Path, help="optional file containing one SHA-512 crypt hash; default root account is locked")
    parser.add_argument("--epoch", type=int, default=DEFAULT_EPOCH)
    parser.add_argument("--no-desktop", action="store_true", help="explicitly build a console-only image")
    parser.add_argument("--check-only", action="store_true", help="validate staging without creating an image")
    parser.add_argument("--_worker", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    try:
        if args._worker is not None:
            image_worker(args._worker)
            return 0
        if args.staging is None:
            parser.error("--staging is required")
        staging = args.staging.resolve(strict=True)
        if not staging.is_dir():
            raise BuildError("--staging must be a directory")
        if args.usb_auto_expand and (args.filesystem!='ext4' or
                not (staging/'expand-usb-rootfs.sh').is_file() or
                not (staging/'etc/eelinux/usb-rootfs-layout').is_file() or
                (staging/'etc/eelinux/usb-rootfs-layout').read_text()!='eeLinux USB root expansion v1\n'):
            raise BuildError('USB auto expansion requires its USB-only script and layout marker')
        if not re.fullmatch(r"[a-zA-Z0-9][a-zA-Z0-9.-]{0,62}", args.hostname):
            raise BuildError("invalid hostname")
        if not 32 <= args.size_mib <= 4096 or not 0 <= args.epoch <= 0x7FFFFFFF:
            raise BuildError("size must be 32..4096 MiB and epoch must fit a signed 32-bit timestamp")
        if args.swap_file_mib and (not 16 <= args.swap_file_mib <= 1024 or args.swap_file_mib >= args.size_mib):
            raise BuildError("swap must be 16..1024 MiB and smaller than the root filesystem")
        root_password_hash = None
        if args.root_password_hash_file is not None:
            root_password_hash = args.root_password_hash_file.read_text(encoding="ascii").strip()
            if not re.fullmatch(r"\$6\$(?:rounds=[0-9]+\$)?[./A-Za-z0-9]{1,16}\$[./A-Za-z0-9]{86}", root_password_hash):
                raise BuildError("root password file must contain one SHA-512 crypt hash, without a username")
        check_staging(staging, not args.no_desktop)
        if args.check_only:
            print("staging passed command, MIPS o32 ELF and dynamic-library checks")
            return 0
        if args.output_dir is None:
            parser.error("--output-dir is required when building")
        if sys.platform != "linux":
            raise BuildError("image creation requires Linux (use WSL); --check-only is portable")
        for command in ("fakeroot", "mke2fs", "e2fsck"):
            if shutil.which(command) is None:
                raise BuildError(f"missing host tool: {command}")
        if args.output_dir.is_symlink():
            raise BuildError("output directory cannot be a symlink")
        output = args.output_dir.resolve()
        if output == staging or staging in output.parents or output in staging.parents:
            raise BuildError("staging and output must be separate directory trees")
        output.mkdir(parents=True, exist_ok=True)
        image_name = f"rootfs.{args.filesystem}"
        for name in ("rootfs.tar", image_name, "SHA256SUMS"):
            target = output / name
            if target.is_symlink() or (target.exists() and not target.is_file()):
                raise BuildError(f"refusing non-regular output path: {target}")
        # DrvFS without metadata reports chmod'ed files as 0777. Assemble on
        # native Linux storage so PAM's securetty and shadow permissions survive
        # formatting, even when the final image is published into a Windows repo.
        # A full SDK, tar archive and filesystem exceed a RAM-backed /tmp.
        # Use native disk storage for assembly rather than consuming host RAM.
        temporary_root = "/var/tmp" if sys.platform.startswith("linux") else None
        with tempfile.TemporaryDirectory(prefix="eelinux-rootfs-", dir=temporary_root) as temporary:
            folder = Path(temporary)
            assemble_tree(staging, folder / "tree", args.hostname, not args.no_desktop,
                          root_password_hash, args.video_mode, args.swap_file_mib)
            (folder / "config.json").write_text(json.dumps({"epoch": args.epoch, "size_mib": args.size_mib,
                                                          "filesystem": args.filesystem,
                                                          "usb_auto_expand":args.usb_auto_expand}), encoding="utf-8")
            subprocess.run(["fakeroot", "--", sys.executable, str(Path(__file__).resolve()), "--_worker", str(folder)], check=True)
            checksums = []
            for name in ("rootfs.tar", image_name):
                path = folder / name
                with path.open("rb") as stream:
                    digest = hashlib.file_digest(stream, "sha256").hexdigest()
                checksums.append(f"{digest}  {name}")
            (folder / "SHA256SUMS").write_text("\n".join(checksums) + "\n", encoding="ascii")
            for name in ("rootfs.tar", image_name, "SHA256SUMS"):
                # Cross-filesystem rename is not possible for /tmp -> /mnt/c.
                # Copy to an output-local temporary file before atomic replace.
                with tempfile.NamedTemporaryFile(dir=output, prefix="." + name + ".",
                                                 suffix=".tmp", delete=False) as local:
                    local_name = Path(local.name)
                    try:
                        with (folder / name).open("rb") as source:
                            shutil.copyfileobj(source, local)
                        local.flush()
                        os.fsync(local.fileno())
                    except BaseException:
                        local_name.unlink(missing_ok=True)
                        raise
                try:
                    os.replace(local_name, output / name)
                finally:
                    local_name.unlink(missing_ok=True)
        account = "root password hash was provisioned" if root_password_hash else "root login is locked until provisioned offline"
        print(f"Created {output / image_name} and rootfs.tar; {account}.")
        return 0
    except (BuildError, OSError, subprocess.CalledProcessError, ValueError) as exc:
        print(f"rootfs: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
