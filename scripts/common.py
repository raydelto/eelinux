"""Small, dependency-free build helpers. SPDX-License-Identifier: GPL-2.0-only"""
from __future__ import annotations

import hashlib
import os
import re
from pathlib import Path
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
TARGET_FLAGS = ["-march=r5900", "-mabi=32", "-msoft-float", "-mfix-r5900"]


def run(args, *, cwd=None, env=None, capture=False):
    print("+ " + " ".join(map(str, args)), flush=True)
    return subprocess.run(list(map(str, args)), cwd=cwd, env=env,
                          text=True, check=True, capture_output=capture)


def download(url, dest, digest, algorithm="sha256"):
    """Cache only byte-for-byte verified archives; interrupted transfers are never reused."""
    dest = Path(dest)
    dest.parent.mkdir(parents=True, exist_ok=True)
    def verify(path):
        h = hashlib.new(algorithm)
        with path.open("rb") as f:
            for chunk in iter(lambda: f.read(1024 * 1024), b""):
                h.update(chunk)
        return h.hexdigest() == digest
    if dest.exists():
        if not verify(dest):
            raise RuntimeError(f"Checksum mismatch in cached {dest}; remove this file to retry")
        return dest
    part = dest.with_name(dest.name + ".part")
    try:
        print(f"Downloading {url}", flush=True)
        with urllib.request.urlopen(url, timeout=90) as src, part.open("wb") as dst:
            while chunk := src.read(1024 * 1024):
                dst.write(chunk)
        if not verify(part):
            raise RuntimeError(f"Checksum mismatch for {url}")
        part.replace(dest)
    finally:
        part.unlink(missing_ok=True)
    return dest


def extract(archive, dest):
    """Extract into a new directory, rejecting traversal and links outside that directory."""
    dest = Path(dest).resolve()
    if dest.exists():
        raise RuntimeError(f"Refusing to extract over existing directory: {dest}")
    with tarfile.open(archive) as tar:
        for member in tar.getmembers():
            path = dest / member.name
            if member.name.startswith("/") or not path.resolve().is_relative_to(dest):
                raise RuntimeError(f"Unsafe archive path: {member.name}")
            if member.isdev() or member.isfifo():
                raise RuntimeError(f"Unexpected special file: {member.name}")
            if member.issym() or member.islnk():
                link = (path.parent if member.issym() else dest) / member.linkname
                if not link.resolve().is_relative_to(dest):
                    raise RuntimeError(f"Unsafe archive link: {member.name}")
        dest.mkdir(parents=True)
        if hasattr(tarfile, "data_filter"):
            # Revalidates after earlier archive members created links. Validating
            # the whole archive only once cannot detect chained symlink escapes.
            tar.extractall(dest, filter="data")
        else:
            # Older Python: refuse traversal through a previously created link.
            for member in tar.getmembers():
                path = dest / member.name
                if not path.resolve().is_relative_to(dest):
                    raise RuntimeError(f"Unsafe archive path after link creation: {member.name}")
                if any(p.is_symlink() for p in [path, *path.parents] if p != dest and p.is_relative_to(dest)):
                    raise RuntimeError(f"Archive member traverses a symlink: {member.name}")
                if member.issym() or member.islnk():
                    link = (path.parent if member.issym() else dest) / member.linkname
                    if not link.resolve().is_relative_to(dest):
                        raise RuntimeError(f"Unsafe archive link after extraction: {member.name}")
                tar.extract(member, dest)


def linux_only():
    if os.name != "posix" or not Path("/proc/sys/kernel").exists():
        raise RuntimeError("Build on Linux or WSL2; use a Linux filesystem, not /mnt/c, for build output")


def probe_linux_compiler(prefix, work):
    """Reject the SDK/newlib ABI and require an actual linkable glibc o32 sysroot."""
    work = Path(work)
    work.mkdir(parents=True, exist_ok=True)
    triple = run([prefix + "gcc", "-dumpmachine"], capture=True).stdout.strip()
    if "linux" not in triple:
        raise RuntimeError(f"{triple} is not a Linux compiler. ps2dev SDK uses a separate bare-metal ABI.")
    src = work / "eelinux-toolchain-probe.c"
    src.write_text('#include <features.h>\n#include <stdio.h>\n#ifndef __GLIBC__\n#error GNU libc required\n#endif\n_Static_assert(sizeof(void*) == 4, "o32 required");\nint main(void) { return puts("eeLinux Linux")==EOF; }\n')
    binary = work / "eelinux-toolchain-probe"
    run([prefix + "gcc", *TARGET_FLAGS, src, "-o", binary])
    data = binary.read_bytes()
    if data[:7] != b"\x7fELF\x01\x01\x01" or int.from_bytes(data[18:20], "little") != 8:
        raise RuntimeError("Compiler did not generate little-endian ELF32 MIPS")
    abi = run([prefix + "readelf", "-A", binary], capture=True).stdout
    if "Soft float" not in abi:
        raise RuntimeError("Linux compiler/sysroot is not consistently soft-float")
    return triple


def same_patch_content(recorded: bytes, generated: bytes) -> bool:
    """True when two unified diffs differ at most in hunk line offsets.

    Regenerated port patches shift when earlier steps change line counts in the
    same file; the reviewed hunk content itself must still match exactly.
    """
    header = re.compile(rb"(?m)^@@ -\d+(?:,\d+)? \+\d+(?:,\d+)? @@")
    return header.sub(b"@@", recorded) == header.sub(b"@@", generated)


def write_reviewed_patch(patch: Path, blob: bytes) -> None:
    """Record a newly generated patch; an existing reviewed patch stays as committed."""
    patch.parent.mkdir(parents=True, exist_ok=True)
    if not patch.exists():
        patch.write_bytes(blob)
