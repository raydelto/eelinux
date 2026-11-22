"""Project entry points and imported-source integrity checks.
SPDX-License-Identifier: GPL-2.0-only
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]


def verify_imports():
    # Vendor formats preserve the upstream paths; read the source manifest schema
    # rather than trusting gitignore or build reports as an integrity check.
    total = 0
    required = [ROOT / "kernel/vendor/frno7/manifest.json", ROOT / "userland/sysvinit/manifest.json"]
    if any(not manifest.is_file() for manifest in required):
        raise RuntimeError("The PS2 source import and complete SysVinit source are both required")
    manifests = list((ROOT / "kernel/vendor").rglob("manifest.json")) + [required[1]]
    for manifest in manifests:
        data = json.loads(manifest.read_text())
        files = data.get("files", [])
        if isinstance(files, dict):
            files = [{"path": path, "sha256": digest} for path, digest in files.items()]
        for entry in files:
            if "sha256" not in entry:
                continue
            rel = entry.get("snapshot_path", entry.get("path"))
            choices = [manifest.parent / "snapshot" / rel, manifest.parent / rel]
            path = next((p for p in choices if p.is_file()), None)
            if path is None:
                raise RuntimeError(f"Missing imported source {manifest}: {rel}")
            if not path.resolve().is_relative_to(manifest.parent.resolve()):
                raise RuntimeError(f"Vendor path escapes manifest: {rel}")
            if hashlib.sha256(path.read_bytes()).hexdigest() != entry["sha256"]:
                raise RuntimeError(f"Imported source modified: {path}")
            total += 1
    if total < 400:
        raise RuntimeError(f"Unexpectedly small source import: {total} verified files")
    print(f"Verified SHA-256 of {total} imported source files")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["help", "verify-imports"])
    args = parser.parse_args()
    if args.command == "verify-imports":
        verify_imports()
    else:
        print("eeLinux — GNU/Linux for the PlayStation 2 Emotion Engine\n"
              "make check            Host tests and imported source integrity\n"
              "make plan             GNU/Xorg package build plan\n"
              "make kernel-port      Fetch 7.2.8 and apply the imported PS2 port; report rejects\n"
              "make kernel-modernize Apply implemented 7.2.8 driver API corrections to that import\n"
              "make kernel-audit     Check current 7.2.8 port; incomplete source returns nonzero\n"
              "make all              Build requested 7.2.8 system once kernel audit passes\n"
              "make toolchain        Bootstrap R5900 Linux GNU/glibc C/C++ cross compiler\n"
              "make ps2dev           Install checksum-pinned official PS2DEV SDK without root\n"
              "make firmware         Compile IOP modules with installed ps2dev tools\n"
              "make userland         Compile GNU userspace and Xorg/TWM (PROFILE=desktop)\n"
              "make locales          Install matching C.UTF-8/en_US.UTF-8 GNU libc data\n"
              "make xorg-apps-update Refresh official X.Org/xterm sources in userland/package/x11r7\n"
              "make xorg-apps        Build all selected current applications against XORG_SDK\n"
              "make native-xorg      Build the production GS-only PS2 DDX and desktop helpers\n"
              "make usb-image-2gb    Package a 2 GB desktop USB image without project diagnostics\n"
              "make dvd-kernel-pal   Build native optical-root kernel (VIDEO_MODE selects PAL/NTSC)\n"
              "make dvd-iso-slim-pal Create complete standalone Slim DVD ISO (VIDEO_MODE=480p: NTSC)\n"
              "make dvd-iso-fat-pal  Create firmware-specific Fat DVD ISOs; compatibility varies\n"
              "make pal-media        Package full GNU/X.Org USB and standalone PAL DVDs\n"
              "make ntsc-media       Same as pal-media VIDEO_MODE=480p (480p + EENTSC.ELF fallback)\n"
              "make userland PROFILE=full Build the current native development suite and strip safely\n"
              "make emulator-userland-build Build pinned experimental R5900 QEMU\n"
              "make emulator-smoke   Test selected built GNU programs under R5900 QEMU\n"
              "make emulator-prepare Prepare disposable PCSX2 boot/debug files\n"
              "make reference-system Explicit 5.4 reference build, not the requested 7.2.8 port\n"
              "\nUse Linux/WSL2. Read README.md, docs/build.md, docs/emulator.md and TODO.md.\n"
              "Native SMAP is implemented and built in; hardware packets and GS Xorg output need validation.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError) as exc:
        print(f"eelinux: {exc}", file=sys.stderr)
        sys.exit(1)
