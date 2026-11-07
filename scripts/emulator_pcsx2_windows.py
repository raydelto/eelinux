#!/usr/bin/env python3
"""Build and package the pinned experimental PCSX2 port in a local Windows tree.

Never replaces an installed application, changes BIOS configuration or runs a VM.
SPDX-License-Identifier: GPL-2.0-only
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PureWindowsPath
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "emulator/pcsx2-windows-build.json"


def sha256(path: Path) -> str:
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def capture(command, **kwargs):
    return subprocess.check_output(list(map(str, command)), text=True, stderr=subprocess.STDOUT, **kwargs)


def source_guard(source: Path, data: dict) -> dict[str, str]:
    mmu = json.loads((ROOT / data["mmu_manifest"]).read_text())
    revision = capture(["git", "-C", source, "rev-parse", "HEAD"]).strip()
    patch = ROOT / mmu["mmu_patch"]
    portability_patch = ROOT / data["windows_portability_patch"]
    if (revision != data["source_revision"] or revision != mmu["source_revision"]
            or sha256(patch) != data["mmu_patch_sha256"]
            or sha256(patch) != mmu["mmu_patch_sha256"]
            or sha256(portability_patch) != data["windows_portability_patch_sha256"]):
        raise RuntimeError("PCSX2 source or MMU patch differs from the recorded inputs")
    modified = {line[3:] for line in capture(
        ["git", "-C", source, "status", "--porcelain", "--untracked-files=all"]).splitlines()}
    expected_files = list(mmu["modified_source_files"]) + list(data["windows_portability_source_files"])
    if modified != set(expected_files):
        raise RuntimeError("Windows source has changes beyond the exact reviewed MMU patch")
    with tempfile.TemporaryDirectory(prefix="eelinux-pcsx2-proof-") as temporary:
        pristine = Path(temporary)
        for name in expected_files:
            result = subprocess.run(["git", "-C", str(source), "show", f"{revision}:{name}"],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if result.returncode:
                if name == "pcsx2/R5900MMU.h": continue
                raise RuntimeError(f"Missing original PCSX2 source: {name}")
            path = pristine / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(result.stdout)
        # Explicitly reproduce Windows checkout bytes independently of global Git config.
        git_apply = ["git", "-c", "core.autocrlf=true", "-c", "core.eol=crlf", "apply"]
        subprocess.run([*git_apply, str(patch)], cwd=pristine, check=True)
        subprocess.run([*git_apply, str(portability_patch)], cwd=pristine, check=True)
        for name in expected_files:
            if sha256(source / name) != sha256(pristine / name):
                raise RuntimeError(f"Windows MMU source differs from reviewed output: {name}")
    return {name: sha256(source / name) for name in expected_files}


def inventory(directory: Path) -> dict[str, str]:
    result = {}
    for path in sorted(directory.rglob("*")):
        if path.is_symlink():
            raise RuntimeError(f"Dependency symlink is unsupported: {path}")
        if path.is_file() and path.name not in (".eelinux-windows-deps.json", "eelinux-runtime.json"):
            result[path.relative_to(directory).as_posix()] = sha256(path)
    return result


def prepare_dependencies(source: Path, archive: Path, seven_zip: Path, data: dict) -> dict:
    specification = data["dependencies"]
    if (archive.is_symlink() or not archive.is_file()
            or archive.stat().st_size != specification["bytes"]
            or sha256(archive) != specification["sha256"]):
        raise RuntimeError("Official Windows dependency archive size/hash mismatch")
    # Inspect all paths before extraction, including Windows drive and parent paths.
    listing = capture([seven_zip, "l", "-slt", archive])
    entries = listing.split("----------", 1)
    if len(entries) != 2: raise RuntimeError("Unsupported dependency archive listing")
    for line in entries[1].splitlines():
        if line.startswith("Path = "):
            path = PureWindowsPath(line[7:])
            if path.is_absolute() or path.drive or ".." in path.parts or not path.parts or path.parts[0] != "deps":
                raise RuntimeError(f"Unsafe dependency archive member: {path}")
        elif line.startswith(("Symbolic Link = ", "Hard Link = ")):
            raise RuntimeError("Linked dependency archive members are unsupported")
    dependencies = source / "deps"
    # Extract independently even when reusing a directory, then compare every byte.
    source.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="eelinux-pcsx2-deps-", dir=source.parent) as temporary:
        temporary = Path(temporary).resolve()
        if not temporary.is_relative_to(source.parent.resolve()):
            raise RuntimeError("Dependency scratch directory escaped its work parent")
        subprocess.run([str(seven_zip), "x", "-y", f"-o{temporary}", str(archive)],
                       stdout=subprocess.DEVNULL, check=True)
        expected = inventory(temporary / "deps")
        if dependencies.exists():
            if inventory(dependencies) != expected:
                raise RuntimeError("Extracted Windows dependencies differ from the official archive")
        else:
            # Both resolved paths are inside the explicitly named work parent.
            if not dependencies.resolve().is_relative_to(source.parent.resolve()):
                raise RuntimeError("Dependency destination escaped its work parent")
            shutil.move(str(temporary / "deps"), dependencies)
    report = {"archive_sha256": specification["sha256"], "files_sha256": expected}
    (dependencies / ".eelinux-windows-deps.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def check_dependencies(dependencies: Path, data: dict) -> dict:
    report = json.loads((dependencies / ".eelinux-windows-deps.json").read_text())
    if (report.get("archive_sha256") != data["dependencies"]["sha256"]
            or report.get("files_sha256") != inventory(dependencies)):
        raise RuntimeError("Windows dependency bytes differ from the verified official archive")
    return report


def windows_environment(visual_studio: Path) -> dict[str, str]:
    vcvars = visual_studio / "VC/Auxiliary/Build/vcvars64.bat"
    if not vcvars.is_file() or any(character in str(vcvars) for character in '"%\r\n'):
        raise RuntimeError("Visual Studio vcvars64.bat was not found at a safe path")
    # A fixed batch file avoids cmd quoting through subprocess.list2cmdline.
    with tempfile.TemporaryDirectory(prefix="eelinux-vc-env-") as temporary:
        batch = Path(temporary) / "environment.cmd"
        batch.write_text(f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b 1\nset\n', encoding="ascii")
        command = f'cmd.exe /d /s /c ""{batch}""'
        output = subprocess.check_output(command, text=True, stderr=subprocess.STDOUT)
    environment = dict(os.environ)
    for line in output.splitlines():
        if "=" in line and not line.startswith("="):
            name, value = line.split("=", 1)
            environment[name] = value
    return environment


def build(args) -> dict:
    if os.name != "nt": raise RuntimeError("Use native Windows Python and Visual Studio")
    data = json.loads(MANIFEST.read_text())
    source, output, runtime = map(lambda value: Path(value).resolve(), (args.source, args.output, args.runtime))
    if (output == source or output.is_relative_to(source) or runtime.is_relative_to(source)
            or runtime == output or runtime.is_relative_to(output) or runtime.exists()):
        raise RuntimeError("Use a separate build directory and a new separate local runtime directory")
    if args.jobs not in (1, 2): raise RuntimeError("Use one or two Windows build jobs")
    before = source_guard(source, data)
    dependencies = source / "deps"
    check_dependencies(dependencies, data)
    environment = windows_environment(Path(args.visual_studio).resolve())
    clang = Path(args.visual_studio) / "VC/Tools/Llvm/x64/bin/clang-cl.exe"
    compiler = capture([clang, "--version"], env=environment).splitlines()[0]
    if "20.1.8" not in compiler: raise RuntimeError("Use the recorded clang-cl20.1.8 compiler")
    qt_version = capture([dependencies / "bin/qmake.exe", "-query", "QT_VERSION"], env=environment).strip()
    if qt_version != data["dependencies"]["qt_version"]: raise RuntimeError("Unexpected Windows Qt version")
    output.mkdir(parents=True, exist_ok=True)
    configure = ["cmake", "-S", str(source), "-B", str(output), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                 f"-DCMAKE_C_COMPILER={clang}", f"-DCMAKE_CXX_COMPILER={clang}",
                 f"-DCMAKE_PREFIX_PATH={dependencies}", *data["cmake_options"]]
    log_path = output / "eelinux-build.log"
    with log_path.open("a", buffering=1) as log:
        for command in (configure, ["cmake", "--build", str(output), "--parallel", str(args.jobs)]):
            print("Running:", command, flush=True)
            log.write(repr(command) + "\n")
            subprocess.run(command, env=environment, stdout=log, stderr=subprocess.STDOUT, check=True)
    if source_guard(source, data) != before: raise RuntimeError("MMU source changed during Windows compilation")
    check_dependencies(dependencies, data)
    executable = output / "pcsx2-qt/pcsx2-qt.exe"
    if not executable.is_file(): raise RuntimeError("Windows PCSX2 target executable is missing")
    runtime.mkdir(parents=True)
    shutil.copyfile(executable, runtime / "pcsx2-qt.exe")
    shutil.copytree(source / "bin/resources", runtime / "resources")
    # Copy official runtime libraries, including dependencies loaded dynamically.
    for path in (dependencies / "bin").glob("*.dll"):
        shutil.copyfile(path, runtime / path.name)
    if (dependencies / "bin/D3D12").is_dir():
        shutil.copytree(dependencies / "bin/D3D12", runtime / "D3D12")
    subprocess.run([str(dependencies / "bin/windeployqt.exe"), str(runtime / "pcsx2-qt.exe"),
                    "--release", "--dir", str(runtime), "--plugindir", str(runtime / "QtPlugins"),
                    "--compiler-runtime", "--no-translations",
                    "--no-system-d3d-compiler", "--no-system-dxc-compiler"], env=environment, check=True)
    # windeployqt selects qwindows for the desktop; include the verified plugin
    # used by the bounded CLI-only check as well, without opening a window.
    platforms = runtime / "QtPlugins/platforms"
    platforms.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(dependencies / "plugins/platforms/qoffscreen.dll", platforms / "qoffscreen.dll")
    (runtime / "qt.conf").write_text("[Paths]\nPlugins = ./QtPlugins\n")
    help_environment = dict(environment)
    help_environment["QT_QPA_PLATFORM"] = "offscreen"
    help_environment["PATH"] = str(runtime) + os.pathsep + str(Path(os.environ["SystemRoot"]) / "System32")
    help_result = subprocess.run([str(runtime / "pcsx2-qt.exe"), "-help"], env=help_environment,
                                 capture_output=True, text=True, timeout=20, creationflags=subprocess.CREATE_NO_WINDOW)
    help_text = help_result.stdout + help_result.stderr
    if help_result.returncode not in (0, 1) or "-elf" not in help_text or "-testconfig" not in help_text:
        raise RuntimeError(f"Patched Windows PCSX2 help check failed: {help_result.returncode}: {help_text}")
    (output / "eelinux-help.txt").write_text(help_text)
    report = {"schema": 1, "source_revision": data["source_revision"], "mmu_patch_sha256": data["mmu_patch_sha256"],
              "windows_portability_patch_sha256": data["windows_portability_patch_sha256"],
              "manifest_sha256": sha256(MANIFEST), "builder_sha256": sha256(Path(__file__)),
              "mmu_source_sha256": before, "dependencies_archive_sha256": data["dependencies"]["sha256"],
              "dependencies_inventory_sha256": sha256(dependencies / ".eelinux-windows-deps.json"),
              "qt_version": qt_version, "compiler": compiler, "cmake_arguments": configure,
              "executable": str(runtime / "pcsx2-qt.exe"), "sha256": sha256(runtime / "pcsx2-qt.exe"),
              "bytes": executable.stat().st_size, "runtime_files_sha256": inventory(runtime),
              "build_log_sha256": sha256(log_path), "help_exit_code": help_result.returncode,
              "help_sha256": hashlib.sha256(help_text.encode()).hexdigest(),
              "boot_tested": False, "full_linux_mmu_qualified": False}
    (runtime / "eelinux-runtime.json").write_text(json.dumps(report, indent=2) + "\n")
    (output / "eelinux-build.json").write_text(json.dumps(report, indent=2) + "\n")
    print("Patched Windows runtime:", runtime / "pcsx2-qt.exe", flush=True)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    deps = sub.add_parser("dependencies")
    deps.add_argument("--source", type=Path, required=True)
    deps.add_argument("--archive", type=Path, required=True)
    deps.add_argument("--seven-zip", type=Path, default=Path("C:/Program Files/7-Zip/7z.exe"))
    compile_parser = sub.add_parser("build")
    for name in ("source", "output", "runtime"):
        compile_parser.add_argument("--" + name, type=Path, required=True)
    compile_parser.add_argument("--visual-studio", type=Path, default=Path("C:/Program Files/Microsoft Visual Studio"))
    compile_parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    try:
        if args.command == "dependencies":
            if os.name != "nt": raise RuntimeError("Use native Windows Python")
            prepare_dependencies(args.source.resolve(), args.archive.resolve(), args.seven_zip.resolve(), json.loads(MANIFEST.read_text()))
            print("Official Windows dependency bytes verified")
        else: build(args)
    except (RuntimeError, OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"emulator-pcsx2-windows: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
