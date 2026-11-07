#!/usr/bin/env python3
"""Cross-build eeLinux GNU userland and Xorg with a pinned Buildroot backend.

This builds real packages. It deliberately refuses bare-metal ps2dev compilers,
and never substitutes a generic MIPS32 CPU for the R5900.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

import latest_packages

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "configs/packages.json"
EXTERNAL = ROOT / "userland"
CPU_FLAGS = ["-march=r5900", "-mabi=32", "-msoft-float", "-mno-llsc", "-mfix-r5900"]
NATIVE_GCC_VERSION = "16.2.0"
SUPPORTED_GCC_MAJORS = {*range(4, 15), 16}


class BuildError(RuntimeError):
    pass


def validate_gcc_version(version: str, profile: str, *, configure_only: bool = False) -> int:
    if not re.fullmatch(r"\d+\.\d+(?:\.\d+)?", version):
        raise BuildError(f"Invalid GCC version: {version!r}")
    major = int(version.split(".")[0])
    if major not in SUPPORTED_GCC_MAJORS or configure_only and major < 5:
        raise BuildError(f"GCC {version} is outside the pinned backend's supported choices "
                         "(GCC 5..14 and the reviewed external GCC 16 extension)")
    if profile == "full" and version != NATIVE_GCC_VERSION:
        raise BuildError(f"The full/native-GCC profile requires external GCC {NATIVE_GCC_VERSION} exactly "
                         "so target compiler headers and runtime libraries match")
    return major


def run(argv: list[str], *, cwd: Path | None = None, capture: bool = False,
        input: str | None = None, env: dict[str, str] | None = None) -> str:
    result = subprocess.run(argv, cwd=cwd, input=input, text=True,
                            stdout=subprocess.PIPE if capture else None,
                            stderr=subprocess.PIPE if capture else None,
                            env=env, check=False)
    if result.returncode:
        detail = result.stderr.strip() if capture else "see build output above"
        raise BuildError(f"Command failed ({result.returncode}): {' '.join(argv)}\n{detail}")
    return result.stdout if capture else ""


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def kstring(value: str | Path) -> str:
    value = str(value)
    if any(ch in value for ch in ("\n", "\r", "\x00", "$")):
        raise BuildError("Kconfig paths/values must not contain newlines, NUL, or '$'")
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def manifest() -> dict:
    return json.loads(MANIFEST.read_text(encoding="utf-8"))


def profile_symbols(data: dict, profile: str, field: str) -> list[str]:
    groups = ["base"]
    if profile in ("desktop", "full"):
        groups.append("desktop")
    if profile == "full":
        groups.append("development")
    return sorted({symbol for group in groups for symbol in data["profiles"][group].get(field, [])})


def selected_symbols(data: dict, profile: str) -> list[str]:
    return profile_symbols(data, profile, "symbols")


def disabled_symbols(data: dict, profile: str) -> list[str]:
    return profile_symbols(data, profile, "disabled_symbols")


def plan(args: argparse.Namespace) -> None:
    data = manifest()
    selected = selected_symbols(data, args.profile)
    print(json.dumps({
        "profile": args.profile,
        "backend": data["buildroot"],
        "target": {"architecture": "MIPS little endian R5900", "abi": "o32",
                   "float": "soft", "libc": "GNU glibc", "cflags": CPU_FLAGS},
        "selected_symbols": selected,
        "packages": {key: value for key, value in data["packages"].items()
                     if value.get("symbol") in selected},
        "output": str(args.sysroot),
        "build_prerequisites": ["Linux host (WSL2 is supported)",
                                "R5900 Linux/glibc cross toolchain with C and C++",
                                "Buildroot host dependencies; see docs/userland.md"],
        "validation": "Recipe plan only; this command does not assert a PS2 hardware boot",
    }, indent=2))


def validate_elf(path: Path) -> None:
    with path.open("rb") as stream:
        header = stream.read(52)
    if len(header) != 52 or header[:4] != b"\x7fELF" or header[4:6] != b"\x01\x01":
        raise BuildError(f"{path}: expected a 32-bit little-endian ELF")
    machine = struct.unpack_from("<H", header, 18)[0]
    flags = struct.unpack_from("<I", header, 36)[0]
    # EF_MIPS_ABI_O32 is 0x1000. Reject n32, n64, MIPS32 and MIPS64
    # instruction-set binaries even if a compiler accepts -march=r5900.
    if machine != 8 or flags & 0xF000 != 0x1000 or flags & 0xF0000000 > 0x20000000:
        raise BuildError(f"{path}: incompatible MIPS ELF machine/ABI/ISA (flags {flags:#x})")
    # Modern Binutils preserves MIPS ABI flags even in stripped binaries. Check
    # the actual object ABI too: accepting -msoft-float does not prove that an
    # imported libc or C++ runtime was built with the same floating-point ABI.
    abi = None
    with path.open("rb") as stream:
        phoff = struct.unpack_from("<I", header, 28)[0]
        phsize, phnum = struct.unpack_from("<HH", header, 42)
        shoff = struct.unpack_from("<I", header, 32)[0]
        shsize, shnum = struct.unpack_from("<HH", header, 46)
        for offset, size, count, type_value, data_offset_index in (
                (phoff, phsize, phnum, 0x70000003, 4),  # PT_MIPS_ABIFLAGS
                (shoff, shsize, shnum, 0x7000002A, 16)):  # SHT_MIPS_ABIFLAGS
            if size < (32 if data_offset_index == 4 else 40):
                continue
            for index in range(count):
                stream.seek(offset + index * size)
                entry = stream.read(size)
                if len(entry) != size:
                    raise BuildError(f"{path}: truncated MIPS ELF table")
                entry_type = struct.unpack_from("<I", entry, 0 if data_offset_index == 4 else 4)[0]
                if entry_type == type_value:
                    stream.seek(struct.unpack_from("<I", entry, data_offset_index)[0])
                    abi = stream.read(24)
                    break
            if abi is not None:
                break
    if abi is None or len(abi) != 24:
        raise BuildError(f"{path}: missing MIPS ABI attributes; soft-float compatibility cannot be verified")
    version, isa_level, isa_rev, _, _, _, fp_abi, isa_ext, ases, _, _ = struct.unpack("<H6B4I", abi)
    # 0 = ABI-independent assembly, 3 = soft float; extension 6 = R5900.
    if version != 0 or isa_level not in (1, 2, 3) or isa_rev != 0 or fp_abi not in (0, 3) or isa_ext not in (0, 6) or ases:
        raise BuildError(f"{path}: incompatible MIPS ABI attributes "
                         f"(ISA {isa_level}/{isa_rev}, FP {fp_abi}, extension {isa_ext}, ASE {ases:#x})")


def toolchain_probe(prefix: str, work: Path, profile: str) -> dict:
    compiler = shutil.which(prefix + "gcc")
    if compiler is None:
        raise BuildError(f"Compiler not found: {prefix}gcc")
    compiler_path = Path(compiler).absolute()
    if compiler_path.parent.name != "bin":
        raise BuildError("External toolchain must be installed under TOOLCHAIN/bin/")
    tuple_name = run([str(compiler_path), "-dumpmachine"], capture=True).strip()
    if not re.match(r"mips(?:el|r5900el)(?:-|$)", tuple_name) or "linux" not in tuple_name:
        raise BuildError(f"{tuple_name!r} is not a little-endian MIPS Linux compiler. "
                         "ps2dev's ee-gcc/newlib toolchain cannot build GNU Linux userland.")
    version = run([str(compiler_path), "-dumpfullversion", "-dumpversion"], capture=True).strip()
    major = validate_gcc_version(version, profile)
    if profile == "full" and compiler_path.name[:-4] != tuple_name:
        raise BuildError("The full profile requires the canonical compiler target tuple prefix")
    sysroot = Path(run([str(compiler_path), "-print-sysroot"], capture=True).strip())
    if not sysroot.is_absolute() or not (sysroot / "usr/include/features.h").is_file():
        raise BuildError("Compiler must provide an absolute GNU glibc sysroot with development headers")
    work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="toolchain-probe-", dir=work) as temp:
        probe = Path(temp)
        source = probe / "probe.c"
        source.write_text("#include <features.h>\n#include <linux/version.h>\n"
                          "#include <stdio.h>\n#include <pthread.h>\n#include <limits.h>\n"
                          "#ifndef __GLIBC__\n#error GNU glibc required\n#endif\n"
                          "_Static_assert(MB_LEN_MAX == 16, \"Final GCC/glibc limits.h required\");\n"
                          "int main(void) { puts(\"R5900 Linux GNU userland\"); return 0; }\n",
                          encoding="utf-8")
        binary = probe / "probe"
        run([str(compiler_path), *CPU_FLAGS, str(source), "-pthread", "-o", str(binary)], capture=True)
        validate_elf(binary)
        macros = run([str(compiler_path), *CPU_FLAGS, "-dM", "-E", str(source)], capture=True)
        if not re.search(r"^#define __mips_soft_float\b", macros, re.MULTILINE):
            raise BuildError("Compiler did not select the soft-float ABI")
        if not re.search(r"^#define _MIPS_SIM (?:_ABIO32|1)$", macros, re.MULTILINE):
            raise BuildError("Compiler did not select the Linux o32 ABI")
        header_match = re.search(r"^#define LINUX_VERSION_CODE (\d+)", macros, re.MULTILINE)
        if not header_match:
            raise BuildError("Missing LINUX_VERSION_CODE in cross sysroot")
        header_code = int(header_match.group(1))
        headers = (header_code >> 16, (header_code >> 8) & 255)
        if headers < (4, 14):
            raise BuildError("This userland requires Linux headers >=4.14 for shadow/login")
        cxx = shutil.which(str(compiler_path)[:-3] + "g++")
        if cxx is None:
            raise BuildError("C++ cross compiler/libstdc++ is required by the desktop and full profiles")
        cxx_source = probe / "probe.cc"
        cxx_source.write_text("#include <string>\nint main() { return std::string(\"ps2\").empty(); }\n",
                              encoding="utf-8")
        run([cxx, *CPU_FLAGS, str(cxx_source), "-o", str(probe / "probe-cxx")], capture=True)
        validate_elf(probe / "probe-cxx")
        def optional_link(driver: str, suffix: str, body: str, options: list[str]) -> bool:
            executable = shutil.which(str(compiler_path)[:-3] + driver)
            if executable is None:
                return False
            feature_source = probe / ("feature-" + driver + suffix)
            feature_binary = probe / ("feature-" + driver)
            feature_source.write_text(body, encoding="utf-8")
            result = subprocess.run([executable, *CPU_FLAGS, *options, str(feature_source),
                                     "-o", str(feature_binary)], capture_output=True, text=True)
            if result.returncode:
                return False
            validate_elf(feature_binary)
            return True
        features = {
            "openmp": optional_link("gcc", ".c", "#include <omp.h>\nint main(void) { return omp_get_thread_num(); }\n", ["-fopenmp"]),
            "ssp": optional_link("gcc", ".c", "int main(void) { volatile char a[128]; a[0] = 0; return a[0]; }\n", ["-fstack-protector"]),
            "ssp_strong": optional_link("gcc", ".c", "int main(void) { volatile char a[128]; a[0] = 0; return a[0]; }\n", ["-fstack-protector-strong"]),
            "fortran": optional_link("gfortran", ".f90", "program hello\nprint *, 'PS2'\nend program hello\n", []),
            "dlang": optional_link("gdc", ".d", "import std.stdio; void main() { writeln(\"PS2\"); }\n", []),
            "rpc": optional_link("gcc", ".c", "#include <rpc/rpc.h>\nint main(void) { svc_run(); return 0; }\n", []),
        }
        if (sysroot / "usr/include/rpc/rpc.h").is_file() and not features["rpc"]:
            raise BuildError("RPC headers exist but native RPC cannot link; this sysroot cannot satisfy Buildroot's RPC check")
    return {"compiler": str(compiler_path), "prefix": compiler_path.name[:-4],
            "path": str(compiler_path.parent.parent), "tuple": tuple_name,
            "version": version, "major": major, "headers": list(headers), "sysroot": str(sysroot),
            "features": features}


def download_buildroot(data: dict, work: Path) -> Path:
    release = data["buildroot"]
    dl = work / "downloads"
    dl.mkdir(parents=True, exist_ok=True)
    archive = dl / f"buildroot-{release['version']}.tar.xz"
    if not archive.is_file():
        partial = archive.with_suffix(archive.suffix + ".part")
        try:
            with urllib.request.urlopen(release["url"], timeout=60) as response, partial.open("wb") as stream:
                shutil.copyfileobj(response, stream)
            if sha256(partial) != release["sha256"]:
                raise BuildError("Downloaded Buildroot archive failed SHA256 verification")
            partial.replace(archive)
        finally:
            partial.unlink(missing_ok=True)
    if sha256(archive) != release["sha256"]:
        raise BuildError(f"Buildroot checksum mismatch: {archive}")
    source = work / f"buildroot-{release['version']}"
    marker = source / ".eelinux-source-lock"
    patches = [EXTERNAL / "patches/buildroot-r5900.patch",
               EXTERNAL / "patches/buildroot-external-gcc16.patch"]
    latest_data = latest_packages.manifest()
    latest_identity = latest_packages.identity(latest_data)
    lock = ":".join([release["sha256"], *(sha256(patch) for patch in patches),
                     hashlib.sha256(json.dumps(latest_identity, sort_keys=True).encode()).hexdigest()])
    if marker.exists():
        if marker.read_text(encoding="utf-8").strip() != lock:
            raise BuildError(f"{source}: source/patch lock changed. Use a new --work directory")
        latest_packages.verify(source, latest_data)
        latest_packages.cache_sources(latest_data, work)
        return source
    if source.exists():
        raise BuildError(f"{source}: existing source lacks verification marker; use a new --work directory")
    # Buildroot's source contains intentional absolute skeleton symlinks. Permit
    # those links, but reject any archive member traversing a symlink (including
    # a link declared later in the archive). All other members use data_filter.
    # This preserves skeleton content without allowing chained link escapes.
    with tempfile.TemporaryDirectory(prefix=".buildroot-prepare-", dir=work) as temporary, \
            tarfile.open(archive, "r:xz") as tar:
        expected = source.name
        entries = tar.getmembers()
        symlinks = {PurePosixPath(entry.name) for entry in entries if entry.issym()}
        for entry in entries:
            name = PurePosixPath(entry.name)
            parts = name.parts
            if not parts or parts[0] != expected or ".." in parts or entry.name.startswith("/"):
                raise BuildError("Unsafe path in pinned Buildroot archive")
            if any(parent in symlinks for parent in name.parents):
                raise BuildError("Buildroot archive member traverses a symbolic link")
            if entry.islnk():
                target = PurePosixPath(entry.linkname)
                if target.is_absolute() or ".." in target.parts or not target.parts or target.parts[0] != expected:
                    raise BuildError("Unsafe hardlink in pinned Buildroot archive")
        def source_filter(entry: tarfile.TarInfo, destination: str) -> tarfile.TarInfo | None:
            if entry.issym():
                return entry.replace(mode=None, uid=None, gid=None, uname=None, gname=None)
            return tarfile.data_filter(entry, destination)
        if not hasattr(tarfile, "data_filter"):
            raise BuildError("Python >=3.12 is required for safe Buildroot extraction")
        tar.extractall(temporary, members=entries, filter=source_filter)
        prepared = Path(temporary) / expected
        for patch in patches:
            run(["patch", "--batch", "--forward", "--fuzz=0", "-p1", "-i", str(patch)], cwd=prepared)
        latest_packages.apply(prepared, work, latest_data)
        (prepared / marker.name).write_text(lock + "\n", encoding="utf-8")
        prepared.replace(source)
    return source


def config_lines(data: dict, info: dict, profile: str) -> list[str]:
    # Buildroot derives target -march from the explicit R5900 architecture patch.
    # Declaring mips32 and merely appending CFLAGS would leave incompatible flags
    # in its toolchain wrapper and feature detection.
    major = info["major"]
    gcc_choice = str(major) if major >= 5 else "4_9"
    headers = tuple(info["headers"])
    header_choice = min(headers, (6, 12))
    lines = [
        "BR2_mipsel=y", "BR2_mips_r5900=y", "BR2_MIPS_SOFT_FLOAT=y",
        "BR2_TOOLCHAIN_EXTERNAL=y", "BR2_TOOLCHAIN_EXTERNAL_CUSTOM=y",
        "BR2_TOOLCHAIN_EXTERNAL_PREINSTALLED=y",
        f"BR2_TOOLCHAIN_EXTERNAL_PATH={kstring(info['path'])}",
        f"BR2_TOOLCHAIN_EXTERNAL_CUSTOM_PREFIX={kstring(info['prefix'])}",
        f"BR2_TOOLCHAIN_EXTERNAL_GCC_{gcc_choice}=y",
        f"BR2_TOOLCHAIN_EXTERNAL_HEADERS_{header_choice[0]}_{header_choice[1]}=y",
        "BR2_TOOLCHAIN_EXTERNAL_CUSTOM_GLIBC=y", "BR2_TOOLCHAIN_EXTERNAL_CXX=y",
        'BR2_TARGET_OPTIMIZATION="-Os -pipe -mno-llsc -mfix-r5900"',
        "BR2_OPTIMIZE_S=y", "BR2_SHARED_LIBS=y", "BR2_REPRODUCIBLE=y",
        "BR2_STRIP_strip=y", "BR2_INIT_SYSV=y", "BR2_SYSTEM_BIN_SH_BASH=y",
        "# BR2_ROOTFS_MERGED_USR is not set",
        'BR2_TARGET_GENERIC_HOSTNAME="eelinux"', 'BR2_TARGET_GENERIC_ISSUE="eeLinux GNU/Linux for PlayStation 2"',
        "# BR2_TARGET_ENABLE_ROOT_LOGIN is not set", "# BR2_TARGET_GENERIC_GETTY is not set",
        "BR2_ENABLE_LOCALE_PURGE=y",
        'BR2_ENABLE_LOCALE_WHITELIST="C"', 'BR2_GENERATE_LOCALE=""',
        # FAT tools and user-selected code pages require glibc's on-demand
        # character converters even while the interactive locale stays C.
        "BR2_TOOLCHAIN_GLIBC_GCONV_LIBS_COPY=y",
        'BR2_TOOLCHAIN_GLIBC_GCONV_LIBS_LIST=""',
        "# BR2_PACKAGE_BUSYBOX is not set",
        "# BR2_PACKAGE_COREUTILS_INDIVIDUAL_BINARIES is not set",
        'BR2_ROOTFS_POST_BUILD_SCRIPT="$(BR2_EXTERNAL_EELINUX_PATH)/scripts/post-build.sh"',
        'BR2_PACKAGE_OVERRIDE_FILE="$(BR2_EXTERNAL_EELINUX_PATH)/configs/local-sources.mk"',
        'BR2_ROOTFS_DEVICE_TABLE="system/device_table.txt $(BR2_EXTERNAL_EELINUX_PATH)/configs/device-table.txt"',
        "BR2_TARGET_ROOTFS_TAR=y", "BR2_TARGET_ROOTFS_TAR_NONE=y",
    ]
    for feature, symbol in {
        "openmp": "BR2_TOOLCHAIN_EXTERNAL_OPENMP", "ssp": "BR2_TOOLCHAIN_EXTERNAL_HAS_SSP",
        "ssp_strong": "BR2_TOOLCHAIN_EXTERNAL_HAS_SSP_STRONG", "fortran": "BR2_TOOLCHAIN_EXTERNAL_FORTRAN",
        "dlang": "BR2_TOOLCHAIN_EXTERNAL_DLANG", "rpc": "BR2_TOOLCHAIN_EXTERNAL_INET_RPC",
    }.items():
        lines.append(symbol + "=y" if info["features"][feature] else "# " + symbol + " is not set")
    lines.extend(symbol + "=y" for symbol in selected_symbols(data, profile))
    conflicting = set(selected_symbols(data, profile)) & set(disabled_symbols(data, profile))
    if conflicting:
        raise BuildError("Profile enables and disables the same options: " + ", ".join(sorted(conflicting)))
    lines.extend("# " + symbol + " is not set" for symbol in disabled_symbols(data, profile))
    lines += ["# BR2_PACKAGE_UTIL_LINUX_LOGIN is not set",  # shadow supplies login
              "# BR2_PACKAGE_NANO_TINY is not set"]
    if profile in ("desktop", "full"):
        lines += ["BR2_ROOTFS_DEVICE_CREATION_DYNAMIC_EUDEV=y",
                  "# BR2_PACKAGE_EUDEV_ENABLE_HWDB is not set",
                  "# BR2_PACKAGE_EUDEV_RULES_GEN is not set",
                  "BR2_PACKAGE_XSERVER_XORG_SERVER_MODULAR=y",
                  "# BR2_PACKAGE_XSERVER_XORG_SERVER_XVFB is not set",
                  "# BR2_PACKAGE_XSERVER_XORG_SERVER_XEPHYR is not set",
                  "# BR2_PACKAGE_MESA3D is not set", "# BR2_PACKAGE_DBUS is not set",
                  "# BR2_PACKAGE_XAPP_XDM is not set"]
    else:
        lines += ["BR2_ROOTFS_DEVICE_CREATION_DYNAMIC_DEVTMPFS=y"]
    if profile == "full":
        lines += ["# BR2_PACKAGE_BINUTILS_TARGET is not set", "BR2_GDB_VERSION_16=y"]
    return lines


def parse_config(path: Path) -> dict[str, str]:
    return dict(line.split("=", 1) for line in path.read_text(encoding="utf-8").splitlines()
                if line.startswith("BR2_") and "=" in line)


def check_resolved_config(path: Path, data: dict, profile: str) -> None:
    config = parse_config(path)
    required = selected_symbols(data, profile) + ["BR2_mips_r5900", "BR2_MIPS_SOFT_FLOAT"]
    missing = [symbol for symbol in required if config.get(symbol) != "y"]
    if missing:
        raise BuildError("Kconfig dropped required packages/options: " + ", ".join(missing))
    unexpected = [symbol for symbol in disabled_symbols(data, profile) if config.get(symbol) == "y"]
    if unexpected:
        raise BuildError("Kconfig enabled disabled profile options: " + ", ".join(unexpected))
    if config.get("BR2_PACKAGE_BUSYBOX") == "y":
        raise BuildError("BusyBox must be disabled; GNU tools and dhcpcd supply the runtime")
    for symbol in ("BR2_PACKAGE_BIND_SERVER", "BR2_PACKAGE_NTP_NTPD", "BR2_PACKAGE_NFS_UTILS_RPC_NFSD"):
        if config.get(symbol) == "y":
            raise BuildError("The standard profiles install networking tools without automatic server components: " + symbol)
    if config.get("BR2_GCC_TARGET_ARCH") != '"r5900"' or config.get("BR2_GCC_TARGET_ABI") != '"32"':
        raise BuildError("Buildroot resolved an incompatible target CPU/ABI")
    if profile == "full" and config.get("BR2_TOOLCHAIN_EXTERNAL_GCC_16") != "y":
        raise BuildError("Full profile must resolve the matching external GCC 16 choice")
    if profile == "full" and config.get("BR2_PACKAGE_BINUTILS_TARGET") == "y":
        raise BuildError("Full profile cannot install two competing native Binutils versions")


def reject_busybox(target: Path) -> None:
    for path in target.rglob("*"):
        if path.name.lower() in ("busybox", "udhcpc") or (
                path.is_symlink() and "busybox" in os.readlink(path).lower()):
            raise BuildError(f"BusyBox payload is not allowed in the GNU runtime: {path}")


def verify_target(target: Path, profile: str) -> None:
    # IOP drivers execute on the separate R3000, and carry a hashed firmware
    # manifest. Validate that manifest before excluding them from R5900 checks.
    from iop_firmware import FirmwareError, validate_firmware
    iop_directory=target/'lib/firmware/ps2'
    iop_files=set()
    if iop_directory.is_dir() and (list(iop_directory.glob('*.irx')) or (iop_directory/'manifest.json').exists()):
        try:
            iop_files=validate_firmware(iop_directory)
        except FirmwareError as exc:
            raise BuildError(str(exc)) from exc
    required = ["bin/bash", "usr/bin/coreutils", "usr/bin/find", "usr/bin/diff",
                "bin/grep", "bin/sed", "usr/bin/gawk", "bin/tar", "bin/gzip",
                "sbin/init", "sbin/telinit", "sbin/runlevel", "sbin/ip", "sbin/dhcpcd", "bin/mount", "bin/login",
                "usr/bin/nano", "bin/ps", "usr/bin/hostname", "usr/bin/getent", "usr/lib/eelinux/fbcheck",
                "usr/sbin/ps2-video"]
    if profile in ("desktop", "full"):
        driver = ("ps2_drv.so" if (target / "usr/lib/xorg/modules/drivers/ps2_drv.so").is_file()
                  else "fbdev_drv.so")
        required += ["usr/bin/Xorg", "usr/bin/twm", "usr/bin/xterm", "usr/bin/xinit",
                     "usr/lib/xorg/modules/drivers/" + driver,
                     "usr/lib/xorg/modules/input/evdev_drv.so",
                     "usr/lib/xorg/modules/input/mouse_drv.so", "usr/bin/xrdb"]
    if profile == "full":
        required += ["usr/bin/gcc", "usr/bin/g++", "usr/bin/make", "usr/bin/as", "usr/bin/ld"]
    missing = [name for name in required if not (target / name).exists()]
    if missing:
        raise BuildError("Built target is missing required binaries: " + ", ".join(missing))
    # Auditing every ELF also catches unexpectedly imported n32/n64 libraries.
    for directory, _, names in os.walk(target):
        for name in names:
            path = Path(directory) / name
            if path.is_symlink() or not path.is_file() or path in iop_files:
                continue
            with path.open("rb") as stream:
                if stream.read(4) == b"\x7fELF":
                    validate_elf(path)
    reject_busybox(target)

    # The installed command contract covers the full GNU suite rather than a
    # small boot-only subset. This profile explicitly selects GNU's multicall
    # Coreutils, including groups when Shadow installs an overlapping command.
    from rootfs import GNU_COMMANDS, SYSTEM_COMMANDS, TOOL_COVERAGE, find_command, guest_path
    commands = GNU_COMMANDS + SYSTEM_COMMANDS
    if profile in ("desktop", "full"):
        commands += [name for names in TOOL_COVERAGE.get("desktop_required_tools", {}).values()
                     for name in names.split()]
    located = {name: find_command(target, name) for name in commands}
    missing = [name for name, path in located.items() if path is None]
    if missing:
        raise BuildError("Missing required command coverage: " + ", ".join(missing))
    for name in TOOL_COVERAGE["gnu"]["coreutils"].split():
        if guest_path(target, located[name]) != target / "usr/bin/coreutils":
            raise BuildError(f"{name} must use the selected GNU Coreutils implementation")


def export_development_files(target: Path, source: Path, info: dict) -> None:
    # Buildroot intentionally omits development headers/static libraries. A native
    # GCC must have these, so full explicitly restores its matching toolchain SDK.
    # Precompiled headers are optional compiler caches and can dwarf the actual
    # header tree. Keep the complete textual SDK without copying workstation PCH.
    def copy_sdk_tree(origin: Path, destination: Path) -> None:
        destination.mkdir(parents=True, exist_ok=True)
        # SDK exports can overlap read-only headers and existing symlinks.
        # rsync replaces them without following guest absolute links.
        run(["rsync", "-a", "--exclude=*.gch", "--exclude=*.pch",
             str(origin) + "/", str(destination) + "/"])

    def copy_sdk_file(origin: Path, destination: Path) -> None:
        run(["cp", "-af", "--remove-destination", str(origin), str(destination)])

    extra_headers = source / "include"
    # Some standalone providers (notably Skarnet) deliberately install under
    # /include. Expose those matching headers through the native compiler's
    # conventional include path as well as the standard /usr/include SDK.
    if extra_headers.is_dir() and extra_headers.resolve() != (source / "usr/include").resolve():
        copy_sdk_tree(extra_headers, target / "usr/include")
    copy_sdk_tree(source / "usr/include", target / "usr/include")
    # GCC commonly installs C++ headers outside its glibc sysroot. Check the
    # compiler installation as well as headers imported by Buildroot.
    candidates = [Path(info["path"]) / "include/c++" / info["version"],
                  Path(info["path"]) / info["tuple"] / "include/c++" / info["version"]]
    if not (target / "usr/include/c++" / info["version"]).is_dir():
        found = next((candidate for candidate in candidates if (candidate / "vector").is_file()), None)
        if found is None:
            raise BuildError("Full profile cannot find matching GCC C++ development headers")
        copy_sdk_tree(found, target / "usr/include/c++" / info["version"])
    for subdir in ("usr/lib", "lib"):
        origin = source / subdir
        destination = target / subdir
        destination.mkdir(parents=True, exist_ok=True)
        for path in origin.glob("*"):
            if path.is_file() and (path.suffix == ".a" or path.name.startswith("crt") and path.suffix == ".o"
                                   or path.suffix == ".so"):
                dest = destination / path.name
                if dest.is_symlink():
                    dest.unlink()
                copy_sdk_file(path, dest)
    gcc = info["compiler"]
    runtime = Path(run([gcc, *CPU_FLAGS, "-print-libgcc-file-name"], capture=True).strip()).parent
    native = target / "usr/lib/gcc" / info["tuple"] / info["version"]
    native.mkdir(parents=True, exist_ok=True)
    for path in runtime.iterdir():
        if path.is_dir() and path.name in ("include", "include-fixed"):
            copy_sdk_tree(path, native / path.name)
        elif path.is_file() and (path.suffix == ".a" or path.name.startswith("crt") and path.suffix == ".o"):
            copy_sdk_file(path, native / path.name)
    for name in ("libstdc++.a", "libsupc++.a"):
        location = Path(run([gcc, *CPU_FLAGS, f"-print-file-name={name}"], capture=True).strip())
        if not location.is_file():
            raise BuildError(f"Full profile requires matching compiler runtime {name}")
        copy_sdk_file(location, native / name)
    # Keep the shared C++ runtime in the first GCC library search directory;
    # otherwise the SDK's static archive there shadows /usr/lib/libstdc++.so.
    shared_cpp = target / "usr/lib/libstdc++.so"
    if shared_cpp.exists() or shared_cpp.is_symlink():
        link = native / "libstdc++.so"
        link.unlink(missing_ok=True)
        link.symlink_to("../../../libstdc++.so")


def append_project_archive(archive: Path, target: Path, epoch: int, *, development: bool,
                           extra_files: list[str]) -> None:
    # Buildroot's tar already carries fakeroot ownership, setuid bits and device
    # nodes. Add SDK files without replacing that permission/device metadata.
    names: set[str] = {"usr/lib/eelinux/fbcheck", "usr/sbin/ps2-video", "usr/bin/ps2-keyboard", *extra_files}
    for path in target.rglob("*") if development else []:
        if path.is_symlink():
            if path.suffix == ".so" and path.parent.relative_to(target).as_posix() in ("usr/lib", "lib"):
                names.add(path.relative_to(target).as_posix())
            continue
        if path.is_dir():
            continue
        relative = path.relative_to(target).as_posix()
        if relative.startswith("usr/include/") or path.suffix == ".a" or (
                path.name.startswith("crt") and path.suffix == ".o") or (
                path.suffix == ".so" and not path.read_bytes().startswith(b"\x7fELF")):
            names.add(relative)
    # Include SDK links too: C++ include trees and linker scripts can reference
    # files through symbolic links, which should remain links in the artifact.
    for path in (target / "usr/include").rglob("*") if development else []:
        if path.is_symlink():
            names.add(path.relative_to(target).as_posix())
    with tarfile.open(archive, "a") as tar:
        for relative in sorted(names):
            path = target / relative
            member = tar.gettarinfo(str(path), arcname=relative)
            member.uid = member.gid = 0
            member.uname = member.gname = "root"
            member.mtime = epoch
            if member.isfile():
                with path.open("rb") as stream:
                    tar.addfile(member, stream)
            else:
                tar.addfile(member)


def compile_runtime_helpers(target: Path, info: dict) -> None:
    source = ROOT / "rootfs/usr/lib/eelinux/fbcheck.c"
    destination = target / "usr/lib/eelinux/fbcheck"
    destination.parent.mkdir(parents=True, exist_ok=True)
    run([info["compiler"], *CPU_FLAGS, "-Os", "-s", str(source), "-o", str(destination)], capture=True)
    destination.chmod(0o755)
    validate_elf(destination)
    destination = target / 'usr/bin/ps2-keyboard'
    destination.parent.mkdir(parents=True, exist_ok=True)
    run([info['compiler'], *CPU_FLAGS, '-Os', '-std=gnu17', '-Wall', '-Wextra',
         str(ROOT/'desktop/ps2-keyboard.c'), str(ROOT/'desktop/ps2_keyboard_config.c'),
         '-o', str(destination)], capture=True)
    destination.chmod(0o755)
    validate_elf(destination)
    import video
    video.build(target, info["compiler"].removesuffix("gcc"))


def compile_desktop_helpers(target: Path, info: dict, staging: Path) -> None:
    """Compile small session helpers against the matching staged X11 ABI."""
    for name, destination, libraries in (
        ('ps2-xorg-prepare', 'usr/lib/eelinux/ps2-xorg-prepare', []),
        ('ps2-desktop', 'usr/bin/ps2-desktop', ['-lX11']),
        ('ps2-xdg-menu', 'usr/bin/ps2-xdg-menu', []),
    ):
        path = target/destination
        path.parent.mkdir(parents=True, exist_ok=True)
        run([info['compiler'], *CPU_FLAGS, '-Os', '-std=gnu17', '-Wall', '-Wextra',
             '--sysroot='+str(staging),
             '-I'+str(staging/'usr/include'), '-L'+str(staging/'usr/lib'),
             '-Wl,-rpath-link,'+str(staging/'usr/lib'), str(ROOT/('desktop/'+name+'.c')),
             *([str(ROOT/'desktop/ps2_keyboard_config.c')] if name=='ps2-xorg-prepare' else []),
             *libraries, '-o', str(path)])
        validate_elf(path)
        run([info['compiler'].removesuffix('gcc')+'strip', '--strip-unneeded', str(path)])


def compile_keyboard(target: Path, info: dict, work: Path, jobs: int) -> list[str]:
    """Build signed loadkeys and selectable layouts once per locked work tree."""
    import keyboard
    payload = work / "keyboard-payload"
    if not payload.exists():
        run([sys.executable, str(ROOT / "scripts/keyboard.py"), "build",
             "--work", str(work / "keyboard-build"),
             "--toolchain-prefix", info["compiler"].removesuffix("gcc"),
             "--staging", str(payload), "--jobs", str(jobs)])
    try:
        keyboard.install(payload, target)
    except RuntimeError as exc:
        raise BuildError(str(exc)) from exc
    return sorted(keyboard.declared_paths() | {"usr/share/eelinux/keyboard-uk.json"})


def import_libc_tools(target: Path, info: dict, profile: str) -> list[str]:
    # The external-toolchain package imports glibc libraries; it intentionally
    # omits libc's small command-line tools. getent is needed for account/NSS
    # lookup by the target runtime. Preserve their real GNU implementations.
    source = Path(info["sysroot"])
    names = ["getent", "ldd", "locale"]
    if profile == "full":
        names += ["iconv", "localedef"]
    installed = []
    for name in names:
        origin = source / "usr/bin" / name
        if not origin.is_file():
            if name == "getent":
                raise BuildError("GNU glibc sysroot is missing usr/bin/getent required by the runtime")
            continue
        destination = target / "usr/bin" / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(origin, destination)
        if name != "ldd":
            run([info["compiler"][:-3] + "strip", str(destination)], capture=True)
            validate_elf(destination)
        installed.append(destination.relative_to(target).as_posix())
    origin=source/'sbin/ldconfig'
    if not origin.is_file():
        raise BuildError('GNU glibc sysroot is missing sbin/ldconfig')
    destination=target/'sbin/ldconfig';destination.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(origin,destination)
    run([info['compiler'][:-3]+'strip','--strip-unneeded',str(destination)],capture=True)
    validate_elf(destination);installed.append('sbin/ldconfig')
    return installed


def build_utf8_locales(target: Path, info: dict, work: Path, x11_sysroot: Path | None = None) -> list[str]:
    """Install real matching GNU libc locales required by uxterm and Xlib."""
    import locales
    try:
        files=locales.build(target, Path(info['sysroot']), work/'utf8-locales',
                            locales.selected_emulator())
        if x11_sysroot is not None:
            files+=locales.install_x11(target,x11_sysroot,work/'utf8-locales')
        return files
    except (RuntimeError, subprocess.CalledProcessError) as exc:
        raise BuildError('Native UTF-8 locale generation failed: '+str(exc)) from exc


def build_loader_cache(target: Path, work: Path) -> list[str]:
    """Use native glibc metadata with pinned R5900 emulation; preserve SONAME links."""
    emulator_work=Path(os.environ.get('EELINUX_QEMU_WORK',
        str(Path.home()/'.cache/eelinux-linux/emulator-userland')))
    if not (emulator_work/'build-report.json').is_file():
        print('Native library cache deferred; run /sbin/ldconfig on the console, or set EELINUX_QEMU_WORK.')
        return []
    from emulator_userland import selected_emulator
    emulator,metadata=selected_emulator(emulator_work)
    config=target/'etc/ld.so.conf'
    if not config.exists():
        config.parent.mkdir(parents=True,exist_ok=True);config.write_text('/lib\n/usr/lib\n')
    run([str(emulator),'-cpu','R5900',str(target/'sbin/ldconfig'),'-X','-r',str(target)],capture=True)
    cache=target/'etc/ld.so.cache'
    if not cache.is_file() or not cache.stat().st_size:
        raise BuildError('Native ldconfig did not generate the library cache')
    (work/'loader-cache.json').write_text(json.dumps({'result':'generated',
        'cache_sha256':sha256(cache),'cache_bytes':cache.stat().st_size,
        'emulator_sha256':metadata['emulator_sha256'],'SONAME_links_preserved':True},indent=2)+'\n')
    return ['etc/ld.so.conf','etc/ld.so.cache']


def prepare_config(data: dict, info: dict, profile: str, work: Path) -> tuple[list[str], Path, dict]:
    verify_sysvinit_source()
    source = download_buildroot(data, work)
    output = work / f"output-{profile}"
    output.mkdir(parents=True, exist_ok=True)
    config = work / f"eelinux-{profile}.defconfig"
    content = "\n".join(config_lines(data, info, profile)) + "\n"
    lock_data = {"profile": profile, "toolchain": info,
                 "latest_packages": latest_packages.identity(),
                 "config_sha256": hashlib.sha256(content.encode()).hexdigest(),
                 "builder_sha256": sha256(Path(__file__).resolve()),
                 "keyboard_builder_sha256": sha256(ROOT / "scripts/keyboard.py"),
                 "keyboard_sources_sha256": sha256(EXTERNAL / "kbd-sources.json"),
                 "manifest_sha256": sha256(MANIFEST),
                 "external_tree_sha256": external_digest(),
                 "runtime_fbcheck_sha256": sha256(ROOT / "rootfs/usr/lib/eelinux/fbcheck.c"),
                 "runtime_video_sha256": sha256(ROOT / "video/ps2-video.c"),
                 "runtime_edid_sha256": sha256(ROOT / "video/edid.h"),
                 "video_modes_sha256": sha256(ROOT / "configs/video-modes.json")}
    lock = output / "eelinux-build-lock.json"
    if lock.exists() and json.loads(lock.read_text()) != lock_data:
        raise BuildError(f"Build inputs changed for {output}. Use a fresh --work directory")
    config.write_text(content, encoding="utf-8")
    # Windows/Git exports may omit this shell file's executable bit.
    (EXTERNAL / "scripts/post-build.sh").chmod(0o755)
    make = ["make", "-C", str(source), f"O={output}", f"BR2_EXTERNAL={EXTERNAL}",
            f"BR2_DL_DIR={work / 'downloads'}"]
    run([*make, f"BR2_DEFCONFIG={config}", "defconfig"])
    check_resolved_config(output / ".config", data, profile)
    return make, output, lock_data


def external_digest() -> str:
    digest = hashlib.sha256()
    for path in sorted(EXTERNAL.rglob("*")):
        # Archives have separate checksum locks. Generated inventory status must
        # not invalidate a configured package build when another package finishes.
        if path.is_relative_to(EXTERNAL / "sources") or path.name == "package-index.json":
            continue
        if path.is_file() and path != EXTERNAL / "validation.json":
            digest.update(path.relative_to(EXTERNAL).as_posix().encode() + b"\0")
            digest.update(path.read_bytes())
    return digest.hexdigest()


def verify_sysvinit_source() -> None:
    directory = EXTERNAL / "sysvinit"
    lock = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
    if lock.get("version") != "3.13" or len(lock.get("files", {})) < 100:
        raise BuildError("Incomplete or unexpected vendored SysVinit source tree")
    for relative, digest in lock["files"].items():
        path = directory / relative
        if not path.resolve().is_relative_to(directory.resolve()) or path.is_symlink():
            raise BuildError(f"Unsafe path in SysVinit source lock: {relative}")
        if not path.is_file() or sha256(path) != digest:
            raise BuildError(f"SysVinit imported source checksum mismatch: {relative}")


def use_native_linux_path() -> None:
    """Keep WSL's appended Windows paths out of Buildroot's host tool search."""
    entries = []
    for entry in os.environ.get("PATH", "").split(os.pathsep):
        if (entry and not any(character.isspace() for character in entry)
                and not re.match(r"^/mnt/[a-z]/", entry) and Path(entry).is_absolute()
                and Path(entry).is_dir() and entry not in entries):
            entries.append(entry)
    for entry in ("/usr/local/sbin", "/usr/local/bin", "/usr/sbin", "/usr/bin", "/sbin", "/bin"):
        if entry not in entries and Path(entry).is_dir():
            entries.append(entry)
    os.environ["PATH"] = os.pathsep.join(entries)


def configure(args: argparse.Namespace) -> None:
    """Resolve Kconfig before a toolchain has completed its bootstrap.

    Explicit version/header assumptions are configuration inputs only. This
    command does not compile packages, stage a rootfs, or certify the toolchain.
    """
    if sys.platform != "linux":
        raise BuildError("Buildroot configuration requires Linux/WSL2")
    use_native_linux_path()
    if not args.assume_toolchain_version or not args.assume_kernel_headers:
        raise BuildError("configure requires --assume-toolchain-version and --assume-kernel-headers")
    prefix = Path(args.toolchain_prefix).absolute()
    if prefix.parent.name != "bin":
        raise BuildError("--toolchain-prefix must be an absolute TOOLCHAIN/bin/TUPLE- path")
    tuple_name = prefix.name.removesuffix("-")
    if not re.match(r"mips(?:el|r5900el)(?:-|$)", tuple_name) or "linux" not in tuple_name:
        raise BuildError("configure requires a little-endian MIPS Linux target tuple")
    version = args.assume_toolchain_version
    major = validate_gcc_version(version, args.profile, configure_only=True)
    headers = tuple(int(part) for part in args.assume_kernel_headers.split("."))
    if len(headers) != 2 or headers < (4, 14):
        raise BuildError("--assume-kernel-headers must be MAJOR.MINOR >=4.14")
    info = {"compiler": str(prefix) + "gcc", "prefix": tuple_name,
            "path": str(prefix.parent.parent), "tuple": tuple_name,
            "version": version, "major": major,
            "headers": list(headers), "sysroot": "not-probed-configure-only"}
    assumptions = set(filter(None, args.assume_toolchain_features.split(",")))
    allowed = {"openmp", "ssp", "ssp-strong", "fortran", "dlang", "rpc"}
    if assumptions - allowed or "ssp-strong" in assumptions and "ssp" not in assumptions:
        raise BuildError("Invalid --assume-toolchain-features; SSP strong requires SSP")
    info["features"] = {feature.replace("-", "_"): feature in assumptions for feature in allowed}
    _, output, _ = prepare_config(manifest(), info, args.profile, args.work.resolve())
    print(f"Resolved Buildroot config: {output / '.config'}\n"
          "Configuration only. Toolchain validation, package compilation and hardware boot are pending.")


def build(args: argparse.Namespace) -> None:
    if sys.platform != "linux":
        raise BuildError("Real cross-builds require a Linux host. Use WSL2/Linux; plan works on Windows")
    use_native_linux_path()
    if args.jobs < 1:
        raise BuildError("--jobs must be positive")
    for executable in ("make", "patch", "tar", "rsync", "gcc", "g++", "wget", "perl", "bc"):
        if not shutil.which(executable):
            raise BuildError(f"Missing Linux host dependency: {executable}; see docs/userland.md")
    work = args.work.resolve()
    sysroot = args.sysroot.resolve()
    if any(ch.isspace() for ch in str(work) + str(ROOT) + args.toolchain_prefix):
        raise BuildError("Buildroot source, work, and toolchain paths must not contain whitespace")
    if sysroot == ROOT or sysroot in ROOT.parents:
        raise BuildError("--sysroot cannot be the repository root or one of its ancestors")
    data = manifest()
    info = toolchain_probe(args.toolchain_prefix, work, args.profile)
    make, output, lock_data = prepare_config(data, info, args.profile, work)
    lock = output / "eelinux-build-lock.json"
    lock.write_text(json.dumps(lock_data, indent=2) + "\n", encoding="utf-8")
    env = dict(os.environ)
    env.setdefault("SOURCE_DATE_EPOCH", str(data["source_date_epoch"]))
    # Buildroot's helper bootstraps use BR2_JLEVEL independently of make's
    # jobserver. Bound both so --jobs also limits CMake/Meson helper processes.
    run([*make, f"BR2_JLEVEL={args.jobs}", f"-j{args.jobs}"], env=env)
    target = output / "target"
    reject_busybox(target)
    compile_runtime_helpers(target, info)
    application_files=[]
    native_driver=None
    if args.profile in ('desktop', 'full'):
        import native_xorg
        native_driver=native_xorg.build(work/'xorg-native',
            info['compiler'].removesuffix('gcc'),(output/'staging').resolve(),
            output/'host',args.jobs,target)
        import xorg_apps
        xorg_apps.build(work/'xorg-applications', (output/'staging').resolve(),
                        info['compiler'].removesuffix('gcc'), target, args.jobs)
        app_report=json.loads((work/'xorg-applications/applications-build.json').read_text())
        application_files=list({p for r in app_report['applications'] for p in r['installed_files']})
        application_files+=['usr/bin/ps2-xdg-menu','usr/bin/ps2-desktop','usr/lib/eelinux/ps2-xorg-prepare',
            'usr/share/eelinux/xorg-applications.json','usr/lib/xorg/modules/drivers/ps2_drv.so']
        application_files += [p.relative_to(target).as_posix() for p in (target/'usr/share/applications').glob('ps2-*.desktop')]
    keyboard_files = compile_keyboard(target, info, work, args.jobs)
    libc_tools = import_libc_tools(target, info, args.profile)
    libc_tools += build_utf8_locales(target, info, work,
        (output/'staging').resolve() if args.profile in ('desktop','full') else None)
    if args.profile == "full":
        export_development_files(target, output / "staging", info)
    libc_tools += build_loader_cache(target, work)
    append_project_archive(output / "images/rootfs.tar", target, data["source_date_epoch"],
                           development=args.profile == "full", extra_files=libc_tools + keyboard_files + application_files)
    verify_target(target, args.profile)
    sysroot.mkdir(parents=True, exist_ok=True)
    run(["rsync", "-a", str(target) + "/", str(sysroot) + "/"])
    report = {**lock_data, "target_elf_audit": "passed", "output_sysroot": str(sysroot),
              "native_driver": native_driver,
              "hardware_boot_test": "not performed", "rootfs_tar": str(output / "images/rootfs.tar")}
    (output / "eelinux-build-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    # Keep the full resolved dependency/version inventory beside the artifact.
    show_info = run([*make, "--no-print-directory", "show-info"], capture=True, env=env)
    (output / "packages-resolved.json").write_text(show_info, encoding="utf-8")
    print(f"GNU userland staged at {sysroot}\nReport: {output / 'eelinux-build-report.json'}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("plan", "configure", "build"))
    parser.add_argument("--sysroot", type=Path, default=Path.home() / ".cache/eelinux/rootfs")
    parser.add_argument("--toolchain-prefix", default=os.environ.get(
        "CROSS_COMPILE", str(Path.home() / ".local/eelinux/bin/mipsr5900el-unknown-linux-gnu-")))
    parser.add_argument("--work", type=Path, default=Path.home() / ".cache/eelinux/userland")
    parser.add_argument("--jobs", type=int, default=min(2, os.cpu_count() or 1))
    parser.add_argument("--profile", choices=("base", "desktop", "full"), default="desktop")
    parser.add_argument("--assume-toolchain-version", help="configure only: GCC version being bootstrapped")
    parser.add_argument("--assume-kernel-headers", help="configure only: kernel header MAJOR.MINOR")
    parser.add_argument("--assume-toolchain-features", default="openmp,ssp,ssp-strong",
                        help="configure only: comma-separated openmp,ssp,ssp-strong,fortran,dlang,rpc; default matches project bootstrap")
    args = parser.parse_args(argv)
    try:
        {"plan": plan, "configure": configure, "build": build}[args.command](args)
    except (BuildError, latest_packages.LatestPackageError, OSError, ValueError) as exc:
        print(f"userland: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
