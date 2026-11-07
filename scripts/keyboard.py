#!/usr/bin/env python3
"""Build signed upstream loadkeys and selectable console keyboard maps.

Only the declared executable, keyboard maps and license are installed.
This optional package does not change the locked GNU distribution profiles.
SPDX-License-Identifier: GPL-2.0-only
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import lzma
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

from common import ROOT, download, extract, linux_only
from userland import CPU_FLAGS, toolchain_probe, validate_elf

LOCK = ROOT / "userland/kbd-sources.json"
MANIFEST = ".eelinux-keyboard.json"
CONFIGURE = ["--prefix=/usr", "--disable-nls", "--disable-shared", "--enable-static",
             "--disable-vlock", "--disable-tests", "--disable-xkb", "--without-zlib",
             "--without-bzip2", "--without-lzma", "--without-zstd", "--disable-compress"]
FILE_FLAGS = "-D_FILE_OFFSET_BITS=64"
EXPECTED_KEYS = {(1, 3): 0x22, (1, 4): 0xA3, (1, 40): 0x40,
                 (0, 43): 0x23, (1, 43): 0x7E, (0, 86): 0x5C, (1, 86): 0x7C,
                 (0, 53): 0x2F, (1, 53): 0x3F,
                 (0, 51): 0x2C, (1, 51): 0x3C, (0, 52): 0x2E, (1, 52): 0x3E}
EXPECTED_US_KEYS = {(0, 43): 0x5C, (1, 43): 0x7C, (1, 3): 0x40,
                    (1, 4): 0x23, (0, 40): 0x27, (1, 40): 0x22,
                    (0, 53): 0x2F, (1, 53): 0x3F}


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_lock() -> dict:
    return json.loads(LOCK.read_text(encoding="utf-8"))


def declared_paths(lock=None) -> set[str]:
    lock = read_lock() if lock is None else lock
    layouts = lock.get('layouts', {})
    if any(not re.fullmatch(r'[a-z][a-z0-9-]{0,30}', name) for name in layouts):
        raise RuntimeError('Invalid keyboard layout identifier')
    return {"usr/bin/loadkeys", "usr/share/licenses/kbd/COPYING", *(
        "usr/share/keymaps/" + Path(name).name for name in lock["files"] if name != "COPYING"),
        *('usr/share/keymaps/eelinux-' + name + '.map' for name in layouts)}


def inventory(folder: Path, *, source_links: bool = False) -> dict:
    files = {}
    for path in sorted(folder.rglob("*")):
        if path.is_symlink():
            if source_links and path.resolve().is_relative_to(folder.resolve()):
                files[path.relative_to(folder).as_posix()] = {"symlink": os.readlink(path)}
                continue
            raise RuntimeError(f"Keyboard source/payload contains an unexpected symbolic link: {path}")
        if path.is_file():
            files[path.relative_to(folder).as_posix()] = {"sha256": sha(path), "bytes": path.stat().st_size}
    return files


def map_closure(source: Path) -> dict[str, Path]:
    """Resolve every include uniquely; preserve upstream Latin-1 bytes."""
    keymaps = source / "data/keymaps"
    result = {}
    active = set()

    def visit(path):
        if path.name in active:
            raise RuntimeError("Recursive UK keymap include")
        if path.name in result:
            if result[path.name] != path:
                raise RuntimeError("Ambiguous flat UK keymap include")
            return
        if path.is_symlink() or not path.is_file():
            raise RuntimeError(f"Missing regular keymap: {path}")
        active.add(path.name)
        result[path.name] = path
        for name in re.findall(r'^\s*include\s+"([^"]+)"', path.read_bytes().decode("latin1"), re.M):
            if Path(name).name != name or name in (".", ".."):
                raise RuntimeError("Unexpected UK keymap include path")
            candidates = sorted(set(keymaps.rglob(name)) | set(keymaps.rglob(name + ".inc")))
            if len(candidates) != 1:
                raise RuntimeError(f"Missing or ambiguous UK keymap include: {name}")
            visit(candidates[0])
        active.remove(path.name)

    visit(keymaps / "i386/qwerty/uk.map")
    return result


def validate_binary_map(data: bytes, layout: str | None = 'uk') -> dict:
    """Check actual loadkeys output, including UK punctuation and ISO key86."""
    if len(data) < 263 or data[:7] != b"bkeymap":
        raise RuntimeError("Invalid loadkeys binary map")
    present = data[7:263]
    if any(flag not in (0, 1) for flag in present) or len(data) != 263 + 256 * sum(present):
        raise RuntimeError("Invalid binary keymap table dimensions")
    tables = {}
    offset = 263
    for index, exists in enumerate(present):
        if exists:
            tables[index] = struct.unpack_from("<128H", data, offset)
            offset += 256
    observed = {}
    expected_keys = EXPECTED_KEYS if layout in ('uk', 'gb') else EXPECTED_US_KEYS if layout == 'us' else {}
    for (modifier, keycode), expected in expected_keys.items():
        actual = tables.get(modifier, (0,) * 128)[keycode]
        if modifier not in tables or actual not in (expected, expected ^ 0xf000):
            raise RuntimeError(f"{layout} keymap mismatch: modifier={modifier} keycode={keycode}")
        observed[f"modifier{modifier}_keycode{keycode}"] = hex(expected)
    return {"map_tables": sorted(tables), "binary_bytes": len(data),
            'uk_punctuation' if layout in ('uk', 'gb') else 'us_punctuation' if layout == 'us' else 'punctuation': observed}


def execute(argv, *, cwd=None, log=None, env=None, timeout=None) -> bytes:
    if log is not None:
        log.write(("+ " + " ".join(map(str, argv)) + "\n").encode()); log.flush()
    result = subprocess.run(list(map(str, argv)), cwd=cwd, env=env, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, timeout=timeout)
    if log is not None:
        log.write(result.stdout); log.write(result.stderr); log.flush()
    if result.returncode:
        raise RuntimeError(f"Keyboard command failed ({result.returncode}): {argv}\n{result.stderr.decode(errors='replace')[-3000:]}")
    return result.stdout


def verify_source(work: Path, lock: dict) -> tuple[Path, dict]:
    archive = work / "downloads" / lock["archive"]["name"]
    signature = work / "downloads" / lock["signature"]["name"]
    download(lock["archive"]["url"], archive, lock["archive"]["sha256"])
    download(lock["signature"]["url"], signature, lock["signature"]["sha256"])
    key = ROOT / lock["signature"]["public_key"]
    if sha(key) != lock["signature"]["public_key_sha256"]:
        raise RuntimeError("Pinned kbd maintainer public key changed")
    armor = key.read_text().split("\n\n", 1)[1].split("-----END PGP PUBLIC KEY BLOCK-----", 1)[0]
    packets = base64.b64decode("".join(line for line in armor.splitlines() if not line.startswith("=")), validate=True)
    with tempfile.TemporaryDirectory(prefix="signature-", dir=work) as temporary:
        folder = Path(temporary)
        (folder / "keyring.gpg").write_bytes(packets)
        (folder / "release.tar").write_bytes(lzma.decompress(archive.read_bytes()))
        status = execute(["gpgv", "--homedir", folder, "--keyring", folder / "keyring.gpg",
                          "--status-fd", "1", signature, folder / "release.tar"], timeout=15).decode()
    if "[GNUPG:] VALIDSIG " + lock["signature"]["fingerprint"] + " " not in status:
        raise RuntimeError("kbd developer signature has an unexpected fingerprint")
    source = work / "sources" / lock["archive"]["root"]
    record = work / "source-inventory.json"
    if source.exists():
        if not record.exists() or inventory(source, source_links=True) != json.loads(record.read_text()):
            raise RuntimeError("kbd source changed or has no inventory; use a fresh --work directory")
    else:
        extract(archive, work / "sources")
        record.write_text(json.dumps(inventory(source, source_links=True), indent=2) + "\n")
    for name, digest in lock["files"].items():
        if sha(source / name) != digest:
            raise RuntimeError(f"Pinned UK keymap/license changed: {name}")
    selected = map_closure(source)
    expected = {Path(name).name for name in lock["files"] if name != "COPYING"}
    if set(selected) != expected:
        raise RuntimeError("UK include closure changed; review source lock")
    for name, entry in lock.get('layouts', {}).items():
        path = source / entry['source']
        if not path.resolve().is_relative_to((source / 'data/keymaps').resolve()) or sha(path) != entry['sha256']:
            raise RuntimeError('Pinned keyboard layout changed: ' + name)
    return source, {"developer_tar_signature": "verified", "signing_fingerprint": lock["signature"]["fingerprint"],
                    "public_key_sha256": sha(key), "source_inventory_sha256": sha(record)}


def build(args) -> dict:
    linux_only()
    if args.jobs < 1:
        raise RuntimeError("--jobs must be positive")
    for name in ("cc", "make", "gpgv", "bison", "flex", "perl"):
        if not shutil.which(name):
            raise RuntimeError(f"Missing keyboard build dependency: {name}")
    work = args.work.resolve(); stage = args.staging.resolve()
    if work.is_relative_to(ROOT / "userland") or stage.is_relative_to(work) or stage == work:
        raise RuntimeError("Keep keyboard build work and separate payload staging outside preserved sources")
    if any(c.isspace() for c in str(work)):
        raise RuntimeError("Use a native Linux work path without whitespace")
    if stage.exists():
        raise RuntimeError("Keyboard build requires fresh --staging; use install for an existing GNU rootfs")
    work.mkdir(parents=True, exist_ok=True)
    lock = read_lock(); source, verification = verify_source(work, lock)
    compiler = toolchain_probe(args.toolchain_prefix, work / "compiler-probe", "base")
    prefix = compiler["compiler"].removesuffix("gcc")
    identity = {"recipe_sha256": sha(Path(__file__)), "source_lock_sha256": sha(LOCK),
                "compiler": compiler, "configure": CONFIGURE, "CPU_FLAGS": CPU_FLAGS}
    recipe_record = work / "build-identity.json"
    if recipe_record.exists() and json.loads(recipe_record.read_text()) != identity:
        raise RuntimeError("Keyboard build recipe changed; use a fresh --work")
    recipe_record.write_text(json.dumps(identity, indent=2) + "\n")
    env = dict(os.environ, LC_ALL="C", PKG_CONFIG_LIBDIR="/nonexistent", PKG_CONFIG_PATH="")
    programs = {}
    with (work / "build.log").open("ab") as log:
        for kind in ("host", "target"):
            output = work / ("build-" + kind); output.mkdir(exist_ok=True)
            options = [*CONFIGURE, "CPPFLAGS=" + FILE_FLAGS, "CFLAGS=-O2"] if kind == "host" else [
                *CONFIGURE, "--host=" + compiler["tuple"], "CC=" + compiler["compiler"],
                "CPPFLAGS=" + FILE_FLAGS,
                "AR=" + prefix + "ar", "RANLIB=" + prefix + "ranlib", "STRIP=" + prefix + "strip",
                "CFLAGS=-Os " + " ".join(CPU_FLAGS), "LDFLAGS=-Wl,--gc-sections"]
            execute([source / "configure", *options], cwd=output, log=log, env=env)
            for directory in ("src/libcommon", "src/libkbdfile", "src/libkeymap"):
                execute(["make", "-C", output / directory, f"-j{args.jobs}"], log=log, env=env)
            execute(["make", "-C", output / "src", f"-j{args.jobs}", "loadkeys"], log=log, env=env)
            programs[kind] = output / "src/loadkeys"
    if inventory(source, source_links=True) != json.loads((work / "source-inventory.json").read_text()):
        raise RuntimeError("Out-of-tree kbd build changed preserved source")
    stage.mkdir(parents=True)
    selected = map_closure(source)
    for path in selected.values():
        destination = stage / "usr/share/keymaps" / path.name
        destination.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(path, destination)
    license_path = stage / "usr/share/licenses/kbd/COPYING"
    license_path.parent.mkdir(parents=True); shutil.copyfile(source / "COPYING", license_path)
    executable = stage / "usr/bin/loadkeys"; executable.parent.mkdir(parents=True)
    shutil.copyfile(programs["target"], executable); executable.chmod(0o755)
    execute([prefix + "strip", "--strip-unneeded", executable])
    validate_elf(executable)
    map_path = stage / "usr/share/keymaps/uk.map"
    clean_env = {"LC_ALL": "C", "LANG": "C", "PATH": "/usr/bin:/bin", "HOME": str(work)}
    binary = execute([programs["host"], "-b", map_path], cwd=work, env=clean_env, timeout=10)
    parsed = validate_binary_map(binary)
    text = execute([programs["host"], "-t8", map_path], cwd=work, env=clean_env, timeout=10)
    (work / "uk.bmap").write_bytes(binary); (work / "uk.expanded.map").write_bytes(text)
    emulation = {"status": "not requested", "PS2_kernel_console_ioctl_or_hardware_tested": False}
    if args.emulator_work is not None:
        if args.runtime_staging is None:
            raise RuntimeError("--emulator-work requires --runtime-staging for genuine GNU loader/libraries")
        from emulator_userland import selected_emulator, runtime_library_hashes
        emulator, emulator_report = selected_emulator(args.emulator_work.resolve())
        runtime = args.runtime_staging.resolve(strict=True)
        libraries = runtime_library_hashes(runtime)
        target_output = execute([emulator, "-cpu", "R5900", "-L", runtime, executable, "-b", map_path],
                                cwd=work, env=clean_env, timeout=15)
        target_text = execute([emulator, "-cpu", "R5900", "-L", runtime, executable, "-t8", map_path],
                              cwd=work, env=clean_env, timeout=15)
        if target_output != binary or target_text != text or runtime_library_hashes(runtime) != libraries:
            raise RuntimeError("Target/host UK maps differ or staged runtime libraries changed")
        emulation = {"status": "passed", "binary_and_text_map_byte_exact_host_vs_R5900": True,
                     "emulator_sha256": emulator_report["emulator_sha256"], "source_commit": emulator_report["source_commit"],
                     "loader_and_library_hashes": libraries,
                     "PS2_kernel_console_ioctl_or_hardware_tested": False,
                     "scope": "Direct target ELF parses host data only; Linux-user does not test keyboard input or console ioctls"}
    additional = {}
    include_dirs = [source / 'data/keymaps/include', source / 'data/keymaps/i386/include',
                    source / 'data/keymaps/i386/qwerty', source / 'data/keymaps/i386/qwertz',
                    source / 'data/keymaps/i386/azerty', source / 'data/keymaps/i386/dvorak']
    layout_env = dict(clean_env, LOADKEYS_INCLUDE_PATH=':'.join(map(str, include_dirs)))
    for name, entry in lock.get('layouts', {}).items():
        original = source / entry['source']
        expanded = execute([programs['host'], '-u', '-t8', original], cwd=work, env=layout_env, timeout=15)
        if re.search(rb'^\s*include\s', expanded, re.M):
            raise RuntimeError('Expanded keyboard map still has includes: ' + name)
        destination = stage / ('usr/share/keymaps/eelinux-' + name + '.map')
        destination.write_bytes(expanded)
        output = execute([programs['host'], '-u', '-b', destination], cwd=work, env=clean_env, timeout=15)
        validation = validate_binary_map(output, name)
        target_equal = False
        if args.emulator_work is not None:
            target_expanded = execute([emulator, '-cpu', 'R5900', '-L', runtime, executable, '-u', '-t8', original],
                                      cwd=work, env=layout_env, timeout=20)
            target_binary = execute([emulator, '-cpu', 'R5900', '-L', runtime, executable, '-u', '-b', destination],
                                    cwd=work, env=clean_env, timeout=20)
            if target_expanded != expanded or target_binary != output:
                raise RuntimeError('Target/host keyboard maps differ: ' + name)
            target_equal = True
        additional[name] = {'source': entry, 'expanded_map_sha256': sha(destination),
                            'binary_map_sha256': hashlib.sha256(output).hexdigest(),
                            'validation': validation, 'host_R5900_byte_exact': target_equal}
    files = inventory(stage)
    if set(files) != declared_paths(lock):
        raise RuntimeError("Unexpected keyboard payload inventory")
    report = {"schema": 1, "package": "kbd", "version": lock["version"], "layout": "selectable console layouts; US/ANSI default",
              "source_lock_sha256": sha(LOCK), "recipe_sha256": sha(Path(__file__)), "verification": verification,
              "compiler": {k: compiler[k] for k in ("tuple", "version", "major", "headers", "features")},
              "CPU_FLAGS": CPU_FLAGS, "CPPFLAGS": FILE_FLAGS,
              "files": files, "total_regular_file_bytes": sum(v["bytes"] for v in files.values()),
              "map_validation": parsed, "binary_map_sha256": hashlib.sha256(binary).hexdigest(),
              "expanded_map_sha256": hashlib.sha256(text).hexdigest(), "linux_user": emulation,
              "unicode_mode": "Selected generated maps are parsed/applied in Unicode mode; legacy UK input retains its original validation",
              "additional_layouts": additional, "default_layout": "us",
              "base_distribution_lock_modified": False, "build_identity_sha256": sha(recipe_record)}
    (stage / MANIFEST).write_text(json.dumps(report, indent=2) + "\n")
    if args.report is not None:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n")
    print(f"Built selectable keyboard payload: {stage} ({report['total_regular_file_bytes']} bytes)")
    return report


def install(payload: Path, staging: Path) -> dict:
    linux_only()
    payload = payload.resolve(strict=True); staging = staging.resolve(strict=True)
    if payload == staging or payload.is_relative_to(staging) or staging.is_relative_to(payload):
        raise RuntimeError("Keyboard payload and GNU staging must be separate")
    if not (staging / "usr").is_dir():
        raise RuntimeError("Install requires an existing GNU rootfs staging directory")
    report = json.loads((payload / MANIFEST).read_text())
    files = inventory(payload); files.pop(MANIFEST)
    if (report.get("source_lock_sha256") != sha(LOCK) or report.get("recipe_sha256") != sha(Path(__file__)) or
            report.get("files") != files or set(files) != declared_paths()):
        raise RuntimeError("Keyboard payload provenance/inventory mismatch")
    for source_name, digest in read_lock()["files"].items():
        relative = "usr/share/licenses/kbd/COPYING" if source_name == "COPYING" else "usr/share/keymaps/" + Path(source_name).name
        if files[relative]["sha256"] != digest:
            raise RuntimeError("Keyboard payload differs from signed upstream map/license")
    validate_elf(payload / "usr/bin/loadkeys")
    # Preflight every destination before copying. Existing differing files need
    # a fresh rootfs stage; symlinked parents must not escape GNU staging.
    metadata_name = "usr/share/eelinux/keyboard-uk.json"
    for name in [*files, metadata_name]:
        destination = staging / name
        if not destination.resolve().is_relative_to(staging):
            raise RuntimeError("Keyboard installation destination escapes staging")
        for parent in destination.parents:
            if parent == staging: break
            if parent.is_symlink():
                raise RuntimeError("Keyboard destination parent is unexpectedly symlinked")
            if parent.exists() and not parent.is_dir():
                raise RuntimeError("Keyboard destination parent is not a directory")
        if destination.exists() and not destination.is_file():
            raise RuntimeError("Keyboard installation destination is not a regular file")
        if destination.is_symlink() or (name in files and destination.exists() and sha(destination) != files[name]["sha256"]):
            raise RuntimeError(f"Refusing to replace a different keyboard output: {name}")
    for name in sorted(files):
        destination = staging / name; destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(payload / name, destination)
        destination.chmod(0o755 if name == "usr/bin/loadkeys" else 0o644)
    metadata = staging / "usr/share/eelinux/keyboard-uk.json"
    metadata.parent.mkdir(parents=True, exist_ok=True); metadata.write_text(json.dumps(report, indent=2) + "\n")
    print(f"Installed verified English UK keyboard payload into {staging}")
    return {**report, "installed_files": sorted([*files, metadata_name])}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    p = commands.add_parser("build")
    p.add_argument("--work", type=Path, default=Path.home() / ".cache/eelinux/keyboard-uk-build")
    p.add_argument("--toolchain-prefix", default=str(Path.home() / ".local/eelinux-linux-latest/bin/mipsr5900el-unknown-linux-gnu-"))
    p.add_argument("--staging", type=Path, required=True)
    p.add_argument("--jobs", type=int, default=1)
    p.add_argument("--emulator-work", type=Path)
    p.add_argument("--runtime-staging", type=Path)
    p.add_argument("--report", type=Path)
    p = commands.add_parser("install")
    p.add_argument("--payload", type=Path, required=True)
    p.add_argument("--staging", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "build": build(args)
    else: install(args.payload, args.staging)


if __name__ == "__main__":
    main()
