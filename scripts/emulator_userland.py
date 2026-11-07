#!/usr/bin/env python3
"""Build pinned R5900 Linux-user QEMU and smoke-test genuine GNU executables.

No binfmt registration, chroot, PID 1, service scripts or PS2-device claims.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import time

try:
    import resource
except ImportError:  # Importable on Windows; builds/tests still require Linux.
    resource = None

from common import ROOT, download, extract, linux_only
from userland import CPU_FLAGS, sha256, toolchain_probe, use_native_linux_path, validate_elf
from rootfs import BuildError as RootfsError, check_staging, elf_info, guest_path

MANIFEST = ROOT / "emulator/qemu-sources.json"
CONFIGURE = ["--target-list=mipsel-linux-user", "--disable-system", "--disable-bsd-user",
             "--enable-linux-user", "--disable-tools", "--disable-docs", "--disable-guest-agent",
             "--disable-werror", "--disable-debug-info", "--disable-git-update", "--disable-modules",
             "--disable-plugins", "--disable-capstone", "--disable-fdt", "--disable-slirp"]

ARITHMETIC = r'''#include <stdint.h>
#include <limits.h>
#include <stdio.h>
_Static_assert(sizeof(void *) == 4, "o32 required");
_Static_assert(MB_LEN_MAX == 16, "Final GNU glibc headers required");
__attribute__((noinline)) static int32_t product(int32_t a, int32_t b) { return a * b; }
__attribute__((noinline)) static uint32_t uproduct(uint32_t a, uint32_t b) { return a * b; }
int main(void) {
    volatile int32_t a = -7, b = 37;
    volatile uint32_t u = UINT32_MAX, v = 2;
    uint32_t rd, hi, lo;
    if (product(a, b) != -259 || uproduct(u, v) != UINT32_MAX - 1) return 1;
    __asm__ volatile("multu %0,%3,%4\n\tmfhi %1\n\tmflo %2"
                     : "=&r"(rd), "=&r"(hi), "=&r"(lo)
                     : "r"(u), "r"(v) : "hi", "lo");
    if (rd != UINT32_MAX - 1 || lo != UINT32_MAX - 1 || hi != 1) return 2;
    __asm__ volatile("mult %0,%3,%4\n\tmfhi %1\n\tmflo %2"
                     : "=&r"(rd), "=&r"(hi), "=&r"(lo)
                     : "r"(a), "r"(b) : "hi", "lo");
    if (rd != (uint32_t)-259 || lo != (uint32_t)-259 || hi != UINT32_MAX) return 3;
    puts("R5900 MULT/MULTU GPR and HI/LO checks passed");
    return 0;
}
'''


def command(argv, *, cwd=None, env=None, timeout=None):
    result = subprocess.run(list(map(str, argv)), cwd=cwd, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}): {' '.join(map(str, argv))}\n{result.stderr[-5000:]}")
    return result.stdout


def source_files(folder):
    return {p.relative_to(folder).as_posix(): {"link": os.readlink(p)} if p.is_symlink()
            else {"sha256": sha256(p)} for p in sorted(folder.rglob("*")) if p.is_symlink() or p.is_file()}


def qemu_patches():
    # PCSX2 patches share this directory but target a different source tree.
    return sorted((ROOT / "emulator/patches").glob("qemu-*.patch"))


def cpu_models(emulator):
    # This pinned QEMU exits 1 after printing -cpu help. Require the actual
    # model table rather than interpreting that documented help exit as a fail.
    result = subprocess.run([str(emulator), "-cpu", "help"], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    models = set(re.findall(r"^MIPS '([^']+)'$", result.stdout, flags=re.MULTILINE))
    if result.returncode not in (0, 1) or "R5900" not in models:
        raise RuntimeError("Built emulator does not expose the R5900 CPU")
    return models


def prepare_sources(work, data):
    lock = work / "source-lock.json"
    source = work / "sources" / "qemu" / data["sources"][0]["archive_root"]
    identity = {"manifest_sha256": sha256(MANIFEST),
                "patches": {p.name: sha256(p) for p in qemu_patches()}}
    if lock.exists():
        saved = json.loads(lock.read_text())
        if saved["identity"] != identity or saved["files"] != source_files(source):
            raise RuntimeError("Emulator source inputs changed; use a fresh --work directory")
        return source
    for item in data["sources"]:
        archive = work / "downloads" / (item["name"] + "-" + item["commit"] + ".tar.gz")
        download(item["url"], archive, item["sha256"])
        destination = work / "sources" / item["name"]
        extract(archive, destination)
        unpacked = destination / item["archive_root"]
        if "destination" in item:
            target = source / item["destination"]
            if target.is_symlink() or not target.resolve().is_relative_to(source.resolve()):
                raise RuntimeError("Unsafe submodule destination")
            shutil.copytree(unpacked, target, dirs_exist_ok=True, symlinks=True)
    for patch in qemu_patches():
        command(["patch", "-p1", "--forward", "--batch", "--fuzz=0", "-i", patch], cwd=source)
    lock.write_text(json.dumps({"identity": identity, "files": source_files(source)}, indent=2) + "\n")
    return source


def build(args):
    linux_only()
    use_native_linux_path()
    if args.jobs < 1:
        raise RuntimeError("--jobs must be positive")
    for name in ("gcc", "make", "pkg-config", "python3", "patch"):
        if not shutil.which(name):
            raise RuntimeError(f"Missing host dependency: {name}; install gcc make pkg-config python3 libglib2.0-dev zlib1g-dev")
    command(["pkg-config", "--atleast-version=2.48", "gthread-2.0"])
    work = args.work.resolve()
    if any(c.isspace() for c in str(work)) or ":" in str(work):
        raise RuntimeError("QEMU source and build paths cannot contain whitespace or colons")
    work.mkdir(parents=True, exist_ok=True)
    data = json.loads(MANIFEST.read_text())
    source = prepare_sources(work, data)
    output = work / "build"
    output.mkdir(exist_ok=True)
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
    lock_data = {"manifest_sha256": sha256(MANIFEST), "configure": CONFIGURE,
                 "source_patch_sha256": {p.name: sha256(p) for p in qemu_patches()},
                 "source_lock_sha256": sha256(work / "source-lock.json"),
                 "host_gcc": command(["gcc", "--version"]).splitlines()[0],
                 "host_glib": command(["pkg-config", "--modversion", "gthread-2.0"]).strip()}
    lock = output / "eelinux-build-lock.json"
    if lock.exists() and json.loads(lock.read_text()) != lock_data:
        raise RuntimeError("QEMU build inputs changed; use a fresh --work directory")
    lock.write_text(json.dumps(lock_data, indent=2) + "\n")
    started = time.monotonic()
    with (work / "build.log").open("a") as log:
        for argv in ([str(source / "configure"), *CONFIGURE], ["make", f"-j{args.jobs}", "mipsel-linux-user/all"]):
            print("+ " + " ".join(argv), flush=True)
            result = subprocess.run(argv, cwd=output, env=env, stdout=log, stderr=subprocess.STDOUT)
            if result.returncode:
                raise RuntimeError(f"QEMU build failed; see {work / 'build.log'}")
    emulator = output / "mipsel-linux-user/qemu-mipsel"
    cpu_models(emulator)
    report = {**lock_data, "status": "built", "cpu": "R5900", "target": data["target"],
              "source_commit": data["sources"][0]["commit"], "version": command([emulator, "--version"]).splitlines()[0],
              "emulator_sha256": sha256(emulator), "emulator_bytes": emulator.stat().st_size,
              "last_build_invocation_seconds": round(time.monotonic() - started, 3), "jobs": args.jobs,
              "limitations": data["limitations"]}
    (work / "build-report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Built experimental R5900 Linux-user emulator: {emulator}")


def selected_emulator(work):
    report = json.loads((work / "build-report.json").read_text())
    emulator = work / "build/mipsel-linux-user/qemu-mipsel"
    if report["manifest_sha256"] != sha256(MANIFEST) or report["cpu"] != "R5900" or sha256(emulator) != report["emulator_sha256"]:
        raise RuntimeError("Missing or altered checksum-pinned R5900 emulator; run build first")
    return emulator, report


def guest_command(emulator, staging, executable, arguments):
    validate_elf(executable)
    return [str(emulator), "-cpu", "R5900", "-L", str(staging), str(executable), *arguments]


def child_limits():
    if resource is None:
        raise RuntimeError("Linux resource limits required")
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def runtime_library_hashes(staging):
    """Record the loader and staged ELF library inventory used by -L."""
    files = {}
    for prefix in ("/lib", "/usr/lib"):
        directory = guest_path(staging, prefix)
        if not directory.is_dir():
            continue
        for path in sorted(directory.rglob("*")):
            if ".so" not in path.name:
                continue
            guest_name = "/" + path.relative_to(staging).as_posix()
            resolved = guest_path(staging, guest_name)
            if resolved.is_file() and elf_info(resolved) is not None:
                files[guest_name] = {"resolved_guest_path": "/" + resolved.relative_to(staging).as_posix(),
                                     "sha256": sha256(resolved), "bytes": resolved.stat().st_size}
    return files


def test(args):
    linux_only()
    use_native_linux_path()
    if not 1 <= args.timeout <= 60:
        raise RuntimeError("--timeout must be 1..60 seconds per program")
    if args.staging is None:
        raise RuntimeError("test requires --staging")
    work = args.work.resolve()
    emulator, built = selected_emulator(work)
    staging = args.staging.resolve(strict=True)
    check_staging(staging, False)
    runtime_files = runtime_library_hashes(staging)
    info = toolchain_probe(args.toolchain_prefix, work / "compiler-probe", "base")
    source = work / "arithmetic.c"
    binary = work / "arithmetic-r5900.elf"
    source.write_text(ARITHMETIC)
    command([info["compiler"], *CPU_FLAGS, "-Os", "-s", source, "-o", binary])
    validate_elf(binary)
    import gzip
    payload = b"eeLinux R5900 GNU userland\n"
    results = []
    with tempfile.TemporaryDirectory(prefix="eelinux-user-smoke-", dir=work) as temporary:
        folder = Path(temporary)
        (folder / "data.txt").write_bytes(payload)
        (folder / "lines.txt").write_text("gamma\nalpha\nbeta\n")
        (folder / "data.gz").write_bytes(gzip.compress(payload, mtime=0))
        env = {"LC_ALL": "C", "LANG": "C", "HOME": str(folder), "TMPDIR": str(folder), "PATH": "/usr/bin:/bin"}
        tests = [
            ("r5900-multiply", binary, [], "R5900 MULT/MULTU GPR and HI/LO checks passed\n"),
            ("bash-builtins", guest_path(staging, "/bin/bash"), ["--noprofile", "--norc", "-c", "set -eu; test $(((-7)*37)) -eq -259; test $(((4294967295*2)&4294967295)) -eq 4294967294; printf 'Bash arithmetic and builtins passed\\n'"], "Bash arithmetic and builtins passed\n"),
            ("gnu-coreutils-printf", guest_path(staging, "/usr/bin/coreutils"), ["--coreutils-prog=printf", "%s %d\\n", "GNU", "42"], "GNU 42\n"),
            ("gnu-coreutils-sort", guest_path(staging, "/usr/bin/coreutils"), ["--coreutils-prog=sort", "lines.txt"], "alpha\nbeta\ngamma\n"),
            ("gnu-coreutils-sha256sum", guest_path(staging, "/usr/bin/coreutils"), ["--coreutils-prog=sha256sum", "data.txt"], hashlib.sha256(payload).hexdigest() + "  data.txt\n"),
            ("gnu-sed-regex", guest_path(staging, "/bin/sed"), ["-n", r"s/^\([a-z][a-z]*\)$/word=\1/p", "lines.txt"], "word=gamma\nword=alpha\nword=beta\n"),
            ("gnu-gawk-softfloat", guest_path(staging, "/usr/bin/gawk"), ["BEGIN { v=(-7)*37; if (v != -259) exit 1; printf \"%.0f\\n\", v }"], "-259\n"),
            ("gnu-gzip-decompress", guest_path(staging, "/bin/gzip"), ["-cd", "data.gz"], payload.decode()),
        ]
        for name, executable, arguments, expected in tests:
            argv = guest_command(emulator, staging, executable, arguments)
            started = time.monotonic()
            try:
                result = subprocess.run(argv, cwd=folder, env=env, text=True, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, timeout=args.timeout, preexec_fn=child_limits)
                record = {"name": name, "status": "passed" if result.returncode == 0 and result.stdout == expected else "failed",
                          "exit_code": result.returncode, "stdout": result.stdout, "stderr": result.stderr,
                          "expected_stdout": expected}
            except subprocess.TimeoutExpired:
                record = {"name": name, "status": "failed", "failure": "per-program timeout"}
            record.update({"seconds": round(time.monotonic() - started, 3), "executable_sha256": sha256(executable)})
            results.append(record)
            print(name + ": " + record["status"], flush=True)
    data = json.loads(MANIFEST.read_text())
    if runtime_files != runtime_library_hashes(staging):
        raise RuntimeError("Staged runtime libraries changed while smoke tests ran")
    report = {"schema": 1, "status": "passed" if all(r["status"] == "passed" for r in results) else "failed",
              "validation_date": time.strftime("%Y-%m-%d", time.gmtime()), "cpu": "R5900", "target_abi": "o32 little-endian soft float",
              "source_commit": built["source_commit"], "source_manifest_sha256": sha256(MANIFEST),
              "emulator_version": built["version"], "emulator_sha256": built["emulator_sha256"],
              "host_kernel": platform.release(), "cross_compiler": {"tuple": info["tuple"], "gcc_version": info["version"]},
              "per_program_timeout_seconds": args.timeout, "arithmetic_source_sha256": sha256(source),
              "staged_loader_and_library_elf_inventory": runtime_files,
              "staged_runtime_library_hashes_unchanged_during_tests": True,
              "tests": results, "filesystem_scope": "Selected target programs and temporary host data files; -L is a loader prefix, not a sandbox",
              "binfmt_registration": False, "chroot": False, "pid1_or_service_execution": False,
              "ps2_kernel_or_devices_tested": False, "limitations": data["limitations"]}
    destination = args.report or work / "test-report.json"
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report, indent=2) + "\n")
    print(f"R5900 userland smoke tests {report['status']}; report: {destination}")
    if report["status"] != "passed":
        raise RuntimeError("One or more experimental R5900 userland tests failed; see report")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("build", "test"))
    parser.add_argument("--work", type=Path, default=Path.home() / ".cache/eelinux/emulator-userland")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--staging", type=Path)
    parser.add_argument("--toolchain-prefix", default=os.environ.get("CROSS_COMPILE", str(Path.home() / ".local/eelinux/bin/mipsr5900el-unknown-linux-gnu-")))
    parser.add_argument("--timeout", type=int, default=15)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    try:
        {"build": build, "test": test}[args.command](args)
    except (RuntimeError, RootfsError, OSError, ValueError, subprocess.SubprocessError) as exc:
        print(f"emulator-userland: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
