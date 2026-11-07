"""Build the small module-loading initramfs; SPDX-License-Identifier: GPL-2.0-only."""
from __future__ import annotations
import argparse
import gzip
from pathlib import Path
import stat
import sys

from common import ROOT, TARGET_FLAGS, linux_only, probe_linux_compiler, run


def newc(directory, *, console=False):
    """Deterministic newc, preserving permissions/symlinks and no host UID leak."""
    directory = Path(directory)
    result = bytearray()
    paths = sorted(directory.rglob("*"), key=lambda p: p.relative_to(directory).as_posix())
    entries = [(p.relative_to(directory).as_posix(), p) for p in paths]
    if console:
        # The kernel opens PID 1's stdio before /init can mount devtmpfs.
        # Encode this fixed device in CPIO without creating a host device node.
        dev = directory / "dev"
        if dev.is_symlink() or (dev.exists() and not dev.is_dir()):
            raise RuntimeError("Initramfs /dev must be a real directory")
        if (dev / "console").exists() or (dev / "console").is_symlink():
            raise RuntimeError("Initramfs console is an archive-only device; remove the host entry")
        if not dev.exists():
            entries.append(("dev", None))
        entries.append(("dev/console", None))
        entries.sort(key=lambda entry: entry[0])
    for ino, (name, path) in enumerate(entries + [("TRAILER!!!", None)], 1):
        major = minor = 0
        if path is None:
            mode, data = 0, b""
            if name == "dev":
                mode = stat.S_IFDIR | 0o755
            elif name == "dev/console":
                mode, major, minor = stat.S_IFCHR | 0o600, 5, 1
        else:
            mode = path.lstat().st_mode
            if stat.S_ISLNK(mode):
                data = path.readlink().as_posix().encode()
            elif stat.S_ISREG(mode):
                data = path.read_bytes()
            elif stat.S_ISDIR(mode):
                data = b""
            else:
                raise RuntimeError(f"Unsupported initramfs special file {path}")
        encoded = name.encode() + b"\0"
        fields = [ino, mode, 0, 0, 2 if stat.S_ISDIR(mode) else 1, 0, len(data), 0, 0, major, minor, len(encoded), 0]
        result.extend(b"070701" + b"".join(f"{x:08x}".encode() for x in fields))
        result.extend(encoded)
        result.extend(b"\0" * (-len(result) % 4))
        result.extend(data)
        result.extend(b"\0" * (-len(result) % 4))
    result.extend(b"\0" * (-len(result) % 512))
    return bytes(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["prepare", "pack"])
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--toolchain-prefix", default="mipsr5900el-unknown-linux-gnu-")
    parser.add_argument("--root-device", default="/dev/sda1")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--budget-kib", type=int, default=4096)
    args = parser.parse_args()
    if args.command == "prepare":
        linux_only()
        if not args.root_device.startswith("/dev/") or ".." in args.root_device or any(c.isspace() for c in args.root_device):
            raise RuntimeError("Root device must be an explicit /dev/ path without whitespace")
        probe_linux_compiler(args.toolchain_prefix, args.directory.parent / "probes")
        for name in ["dev", "proc", "sys", "newroot", "etc", "lib/firmware", "lib/modules"]:
            (args.directory / name).mkdir(parents=True, exist_ok=True)
        run([args.toolchain_prefix + "gcc", *TARGET_FLAGS, "-Os", "-static", "-s", "-Wall", "-Wextra", "-Werror",
             ROOT / "boot/early-root.c", "-o", args.directory / "init"])
        (args.directory / "init").chmod(0o755)
        (args.directory / "etc/eelinux-root").write_text(args.root_device + "\n")
        manifest = args.directory / "etc/eelinux-modules"
        if not manifest.exists():
            manifest.write_text("# Populated by scripts/kernel.py after modules_install\n")
    else:
        if not args.output:
            parser.error("pack requires --output")
        init = args.directory / "init"
        if not init.is_file() or not (init.stat().st_mode & 0o111):
            raise RuntimeError("Initramfs requires an executable /init")
        payload = newc(args.directory, console=True)
        if len(payload) > args.budget_kib * 1024:
            raise RuntimeError(f"Initramfs is {len(payload)} bytes; budget is {args.budget_kib * 1024}")
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(gzip.compress(payload, mtime=0))
        print(f"Initramfs: {len(payload)} bytes unpacked, {args.output.stat().st_size} compressed")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError) as exc:
        print(f"initramfs: {exc}", file=sys.stderr)
        sys.exit(1)
