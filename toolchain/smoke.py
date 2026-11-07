"""Compile and run direct R5900 GNU libc tests under pinned Linux-user QEMU.

The emulator services syscalls with the host kernel; this does not qualify the
PS2 kernel's TLS traps, futex implementation, MMU, devices or real hardware.
SPDX-License-Identifier: GPL-2.0-only
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from common import ROOT, linux_only, probe_linux_compiler
from emulator_userland import child_limits, guest_command, runtime_library_hashes, selected_emulator
from userland import CPU_FLAGS, sha256, validate_elf


def test(args):
    linux_only()
    args.work.mkdir(parents=True, exist_ok=True)
    prefix = str(args.prefix.resolve() / "bin/mipsr5900el-unknown-linux-gnu-")
    triple = probe_linux_compiler(prefix, args.work / "compiler-probe")
    emulator, emulator_report = selected_emulator(args.emulator_work.resolve())
    sysroot = Path(subprocess.check_output([prefix + "gcc", "-print-sysroot"], text=True).strip()).resolve()
    source = ROOT / "toolchain/smoke.c"
    lock = json.loads(args.source_lock.read_text())
    expected_glibc = next(p["version"] for p in lock["sources"] if p["name"] == "glibc")
    expected = f"glibc {expected_glibc}: R5900 multiply, soft float, TLS, pthread and futex passed\n"
    runtime_before = runtime_library_hashes(sysroot)
    target_libraries = args.prefix.resolve() / triple / "lib"
    gcc_runtime_before = runtime_library_hashes(args.prefix.resolve() / triple)
    records = []
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "LANG": "C", "LC_ALL": "C"}
    cases = [("dynamic", "gcc", source, expected, False),
             ("static", "gcc", source, expected, True),
             ("cxx-dynamic", "g++", ROOT / "toolchain/smoke.cc", "R5900 GNU C++ runtime passed\n", False),
             ("cxx-static", "g++", ROOT / "toolchain/smoke.cc", "R5900 GNU C++ runtime passed\n", True)]
    for mode, compiler, test_source, expected_output, static in cases:
        binary = args.work / ("gnu-runtime-" + mode)
        command = [prefix + compiler, *CPU_FLAGS, "-Os", "-pthread", str(test_source), "-o", str(binary)]
        if static:
            command.insert(-2, "-static")
        if compiler == "g++":
            command.append("-latomic")
        subprocess.run(command, check=True, capture_output=True, text=True)
        validate_elf(binary)
        run_command = guest_command(emulator, sysroot, binary, [])
        case_env = env.copy()
        if mode == "cxx-dynamic":
            # GCC installs its target runtimes beside, rather than inside, the
            # glibc sysroot. QEMU's -L is a loader prefix, not a filesystem jail.
            case_env["LD_LIBRARY_PATH"] = str(target_libraries)
        try:
            result = subprocess.run(run_command, env=case_env, capture_output=True, text=True,
                                    timeout=args.timeout, preexec_fn=child_limits)
            record = {"mode": mode, "exit_code": result.returncode, "stdout": result.stdout,
                      "stderr": result.stderr, "passed": result.returncode == 0 and result.stdout == expected_output}
        except subprocess.TimeoutExpired:
            record = {"mode": mode, "passed": False, "failure": "bounded runtime timeout"}
        record.update({"binary_sha256": sha256(binary), "bytes": binary.stat().st_size,
                       "compile_command": command, "test_source_sha256": sha256(test_source)})
        records.append(record)
    unchanged = (runtime_library_hashes(sysroot) == runtime_before
                 and runtime_library_hashes(args.prefix.resolve() / triple) == gcc_runtime_before)
    passed = unchanged and all(r["passed"] for r in records)
    report = {"status": "passed" if passed else "failed", "cpu": "R5900", "tuple": triple,
              "gcc_version": subprocess.check_output([prefix + "gcc", "-dumpfullversion"], text=True).strip(),
              "source_lock_sha256": sha256(args.source_lock), "test_source_sha256": sha256(source),
              "emulator_sha256": emulator_report["emulator_sha256"],
              "emulator_source_commit": emulator_report["source_commit"], "tests": records,
              "runtime_elf_inventory": runtime_before, "runtime_hashes_unchanged": unchanged,
              "gcc_runtime_elf_inventory": gcc_runtime_before,
              "ps2_kernel_or_hardware_tested": False,
              "scope": "Direct target ELF programs; Linux-user syscall and atomic behavior uses the host kernel"}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n")
    print(f"GNU R5900 runtime smoke {report['status']}: {args.report}")
    if not passed:
        raise RuntimeError("GNU runtime smoke failed; see report")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, default=Path.home() / ".local/eelinux-linux-latest")
    parser.add_argument("--work", type=Path, default=Path.home() / ".cache/eelinux-linux-latest/toolchain/smoke")
    parser.add_argument("--emulator-work", type=Path, required=True)
    parser.add_argument("--source-lock", type=Path, default=ROOT / "toolchain/source-lock.json")
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=15)
    args = parser.parse_args()
    if not 1 <= args.timeout <= 60:
        parser.error("--timeout must be 1..60 seconds")
    try:
        test(args)
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as exc:
        print(f"toolchain-smoke: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
