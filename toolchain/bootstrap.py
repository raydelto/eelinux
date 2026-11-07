"""Bootstrap a genuine GNU/Linux R5900 compiler. SPDX-License-Identifier: GPL-2.0-only."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from common import ROOT, download, extract, linux_only, probe_linux_compiler, run

LOCK = json.loads((ROOT / "toolchain/source-lock.json").read_text())
TARGET_RUNTIME_FLAGS = "-O2 -g -march=r5900 -mabi=32 -msoft-float -mfix-r5900 -mno-llsc"


def target_environment(host_env):
    """Apply EE CPU flags to GCC target runtimes without changing host CFLAGS."""
    result = host_env.copy()
    result["CFLAGS_FOR_TARGET"] = TARGET_RUNTIME_FLAGS
    result["CXXFLAGS_FOR_TARGET"] = TARGET_RUNTIME_FLAGS
    return result


def glibc_cpu_flags(version):
    """glibc 2.44 builtins require lock-free atomics via Linux LL/SC traps."""
    flags = ["-march=r5900", "-mabi=32", "-msoft-float", "-mfix-r5900"]
    # R5900 has no LL/SC hardware. The PS2 Linux reserved-instruction handler
    # provides these operations. New glibc requires GCC's lock-free builtins;
    # older MIPS glibc used explicit LL/SC assembly even with -mno-llsc.
    flags.append("-mllsc" if tuple(map(int, version.split("."))) >= (2, 44) else "-mno-llsc")
    return flags


def install_kernel_headers(kernel, output, sysroot, env):
    """Export MIPS syscall headers even when the source tree was built in place."""
    output.mkdir(parents=True, exist_ok=True)
    # Make's source-tree VPATH can otherwise treat generated unistd headers as
    # already built while headers_install searches only the separate O= tree.
    run(["make", "-B", "-C", kernel, f"O={output}", "ARCH=mips",
         "archheaders", "headers_install", f"INSTALL_HDR_PATH={sysroot}/usr"], env=env)
    required = ["unistd.h"] + [f"unistd{suffix}_{abi}.h"
                                for suffix in ("", "_nr")
                                for abi in ("o32", "n32", "n64")]
    missing = [name for name in required
               if not (sysroot / "usr/include/asm" / name).is_file()]
    if missing:
        raise RuntimeError("Incomplete MIPS syscall header export: " + ", ".join(missing))


def apply_source_patches(source, package):
    """Apply checksum-pinned downstream changes only to exact release files."""
    for patch in package.get("patches", []):
        path = ROOT / patch["path"]
        if hashlib.sha256(path.read_bytes()).hexdigest() != patch["sha256"]:
            raise RuntimeError("Toolchain patch checksum mismatch: " + str(path))
        hashes = {name: hashlib.sha256((source / name).read_bytes()).hexdigest()
                  for name in patch["files"]}
        if all(hashes[name] == data["after_sha256"] for name, data in patch["files"].items()):
            continue
        if not all(hashes[name] == data["before_sha256"] for name, data in patch["files"].items()):
            raise RuntimeError("Toolchain patch requires pristine release files: " + str(path))
        command = ["patch", "--batch", "--forward", "--fuzz=0", "-p1", "-i", path]
        run([*command, "--dry-run"], cwd=source)
        run(command, cwd=source)
        for name, data in patch["files"].items():
            if hashlib.sha256((source / name).read_bytes()).hexdigest() != data["after_sha256"]:
                raise RuntimeError("Unexpected toolchain patched file: " + name)


def build(args):
    linux_only()
    lock = json.loads(args.source_lock.read_text())
    work, prefix = args.work.resolve(), args.prefix.resolve()
    if " " in str(prefix) or str(work).startswith("/mnt/"):
        raise RuntimeError("Use a native Linux build directory and an installation prefix without spaces")
    for command in ["gcc", "g++", "make", "bison", "flex", "gawk", "makeinfo", "patch"]:
        if not shutil.which(command):
            raise RuntimeError(f"Missing host dependency: {command}; see docs/build.md")
    kernel = args.kernel_source.resolve()
    if not (kernel / "arch/mips/Makefile").is_file():
        raise RuntimeError("--kernel-source must be a complete PS2 Linux tree, not the vendored delta")
    # Headers define the syscall ABI. The current PS2 baseline uses 5.4; the newer
    # target must pass its own port gate before its headers can replace these.
    target = lock["target"]
    sysroot = prefix / target / "sysroot"
    sysroot.mkdir(parents=True, exist_ok=True)
    env = target_environment(os.environ)
    env["PATH"] = str(prefix / "bin") + os.pathsep + env["PATH"]
    env["LC_ALL"] = "C"
    env["SOURCE_DATE_EPOCH"] = "946684800"
    def logged(command, cwd, stage_env, label):
        logs = work / "logs"
        logs.mkdir(parents=True, exist_ok=True)
        log = logs / (label + ".log")
        print(f"Building {label}; log: {log}", flush=True)
        with log.open("a") as output:
            result = subprocess.run(list(map(str, command)), cwd=cwd, env=stage_env,
                                    stdout=output, stderr=subprocess.STDOUT)
        if result.returncode:
            tail = log.read_text(errors="replace").splitlines()[-35:]
            raise RuntimeError(f"{label} failed ({result.returncode}):\n" + "\n".join(tail))
    sources = {}
    for package in lock["sources"]:
        archive = download(package["url"], work / "downloads" / package["url"].rsplit("/", 1)[1],
                           package["digest"], package["algorithm"])
        unpack = work / "sources" / package["name"]
        source = unpack / (package["name"] + "-" + package["version"])
        if not source.exists():
            extract(archive, unpack)
        apply_source_patches(source, package)
        sources[package["name"]] = source
    headers_output = work / "kernel-headers"
    install_kernel_headers(kernel, headers_output, sysroot, env)
    header_hash = hashlib.sha256()
    for folder in ("asm", "asm-generic", "linux"):
        for header in sorted((sysroot / "usr/include" / folder).rglob("*")):
            if header.is_file():
                header_hash.update(header.relative_to(sysroot).as_posix().encode() + b"\0")
                header_hash.update(header.read_bytes())
    expected = {"source_lock": lock, "prefix": str(prefix),
                "headers_sha256": header_hash.hexdigest(),
                "recipe_sha256": hashlib.sha256(Path(__file__).read_bytes() + (ROOT / "scripts/common.py").read_bytes()).hexdigest()}

    def stage(name, configure, targets):
        directory = work / name
        directory.mkdir(parents=True, exist_ok=True)
        done = directory / ".eelinux-installed"
        if done.exists():
            if json.loads(done.read_text()) != expected:
                raise RuntimeError(f"{name} was built with another source lock/prefix; use a new work directory")
            print(f"Already completed: {name}")
            return
        run(configure, cwd=directory, env=env)
        for make_targets in targets:
            logged(["make", f"-j{args.jobs}", *make_targets], directory, env, name)
        done.write_text(json.dumps(expected, indent=2))

    common = [f"--target={target}", f"--prefix={prefix}", f"--with-sysroot={sysroot}",
              "--with-arch=r5900", "--with-tune=r5900", "--with-abi=32", "--with-float=soft",
              "--disable-multilib", "--disable-nls", "--disable-werror"]
    stage("binutils", [sources["binutils"] / "configure", f"--target={target}", f"--prefix={prefix}",
          f"--with-sysroot={sysroot}", "--disable-nls", "--disable-werror"], [["all"], ["install"]])
    stage("gcc-bootstrap", [sources["gcc"] / "configure", *common,
          "--enable-languages=c", "--without-headers", "--with-newlib", "--disable-shared",
          "--disable-threads", "--disable-libatomic", "--disable-libgomp", "--disable-libquadmath",
          "--disable-libsanitizer", "--disable-libssp", "--disable-libvtv"],
          [["all-gcc", "all-target-libgcc"], ["install-gcc", "install-target-libgcc"]])
    if args.stage == "bootstrap":
        print("Bootstrap compiler installed; no GNU libc yet. Continue with --stage complete.")
        return
    libc_env = env.copy()
    libc_version = next(package["version"] for package in lock["sources"] if package["name"] == "glibc")
    libc_env["CC"] = str(prefix / "bin" / (target + "-gcc")) + " " + " ".join(glibc_cpu_flags(libc_version))
    libc_env["CXX"] = str(prefix / "bin" / (target + "-g++"))
    libc_dir = work / "glibc"
    libc_dir.mkdir(parents=True, exist_ok=True)
    libc_marker = libc_dir / ".eelinux-installed"
    if libc_marker.exists() and json.loads(libc_marker.read_text()) != expected:
        raise RuntimeError("glibc cache was built with another source lock/prefix/headers/recipe; use a new work directory")
    if not libc_marker.exists():
        host = run(["gcc", "-dumpmachine"], capture=True).stdout.strip()
        run([sources["glibc"] / "configure", "--prefix=/usr", f"--host={target}", f"--build={host}",
             f"--with-headers={sysroot}/usr/include", "--enable-kernel=5.4.0", "--disable-werror"],
             cwd=libc_dir, env=libc_env)
        logged(["make", f"-j{args.jobs}"], libc_dir, libc_env, "glibc")
        logged(["make", f"install_root={sysroot}", "install"], libc_dir, libc_env, "glibc")
        libc_marker.write_text(json.dumps(expected, indent=2))
    stage("gcc-final", [sources["gcc"] / "configure", *common, "--enable-languages=c,c++",
          "--enable-shared", "--enable-threads=posix", "--disable-libsanitizer", "--disable-libquadmath"],
          [["all"], ["install"]])
    probe_linux_compiler(str(prefix / "bin" / (target + "-")), work / "probes")
    print(f"Linux compiler installed: {prefix}/bin/{target}-gcc")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["plan", "build"])
    parser.add_argument("--work", type=Path, default=Path.home() / ".cache/eelinux-linux-latest/toolchain")
    parser.add_argument("--prefix", type=Path, default=Path.home() / ".local/eelinux-linux-latest")
    parser.add_argument("--source-lock", type=Path, default=ROOT / "toolchain/source-lock.json")
    parser.add_argument("--kernel-source", type=Path)
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 1, 2))
    parser.add_argument("--stage", choices=["bootstrap", "complete"], default="complete")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    if args.command == "plan":
        print(json.dumps(json.loads(args.source_lock.read_text()), indent=2))
        print("Build order: Linux headers -> binutils -> C/libgcc bootstrap -> glibc -> GCC C/C++")
    else:
        if not args.kernel_source:
            parser.error("build requires --kernel-source")
        build(args)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError) as exc:
        print(f"toolchain: {exc}", file=sys.stderr)
        sys.exit(1)
