# Reproducing the PAL release media from a clean checkout

This guide rebuilds the r44-style PAL media (2 GB USB image and the three
FreeDVDBoot DVD ISOs) from a clean clone, on a host that has never built
eeLinux before. It records the exact order, the host setup, and the steps that
earlier documentation left implicit. [build.md](build.md) and
[pal-desktop-media.md](pal-desktop-media.md) remain the reference for what each
component is; this page is the runbook.

It was written while reproducing the build on Ubuntu 26.04 (x86_64, 12 cores,
14 GiB RAM, no root access) on 2026-11-21. The resulting images have been checked by
the repository's own packaging verifiers, but not booted on hardware. See
[Verification and known differences](#verification-and-known-differences).

## 1. Host

Debian/Ubuntu x86_64 with Python 3.12+ (3.14 works). Install the packages from
[build.md](build.md#host-prerequisites), plus:

```sh
sudo apt-get install genisoimage libarchive-tools gettext itstool \
  libglib2.0-dev zlib1g-dev ninja-build meson lz4 mtools dosfstools bubblewrap
```

`e2fsprogs` must be **1.47 or newer** (`mke2fs -V`); with 1.46 the ext4 image
test fails. Check that the
`mke2fs` first on `PATH` is the system one (`/usr/sbin/mke2fs`). Other SDKs,
such as Android platform-tools, ship an older `mke2fs` that can shadow it.

### Without root

If you cannot install packages, extract them into a private prefix and overlay
it on `/usr` with bubblewrap. Kernel, toolchain and userland builds then see a
normal Debian layout, which matters because bison, texinfo and libtool use
hard-coded `/usr/share` paths:

```sh
H=$HOME/.local/eelinux-hostdeps
mkdir -p $H/debs && cd $H/debs
apt-get download bison flex gawk texinfo texinfo-lib libtool libtool-bin \
  libintl-perl libtext-unidecode-perl libxml-libxml-perl tex-common \
  libgmp-dev libmpfr-dev libmpc-dev libfl-dev libfl2 genisoimage \
  libarchive-tools itstool python3-libxml2 lz4 gperf help2man \
  libglib2.0-dev libglib2.0-dev-bin libgio-2.0-dev libffi-dev libmount-dev \
  libblkid-dev libpcre2-dev libselinux-dev libsepol-dev libsysprof-capture-4-dev zlib1g-dev \
  mtools dosfstools
cd $H && mkdir root && for d in debs/*.deb; do dpkg -x "$d" root; done
cat > $H/run.sh <<'EOF'
#!/bin/sh
H="$(dirname "$(readlink -f "$0")")"
exec bwrap --dev-bind / / --overlay-src /usr --overlay-src "$H/root/usr" --ro-overlay /usr \
  env PATH="/usr/sbin:/usr/bin:/sbin:/bin" HOME="$HOME" "$@"
EOF
cat > $H/pack.sh <<'EOF'
#!/bin/sh
H="$(dirname "$(readlink -f "$0")")"
exec env PATH="/usr/sbin:/usr/bin:/sbin:/bin:$H/root/usr/bin" "$@"
EOF
chmod +x $H/run.sh $H/pack.sh
```

Prefix compile steps with `$H/run.sh` and packaging steps (anything that runs
`fakeroot`: `image`, `usb-image-2gb`, `dvd-iso*`, `pal-media`) with
`$H/pack.sh`. Inside the bubblewrap user namespace, `fakeroot` cannot fake
`chown` to root (it fails with `EINVAL`), so packaging must run outside it.
`genisoimage` and `bsdtar` work from the private prefix without the overlay.

## 2. Build order

All commands run from the repository root with the default `WORK`
(`~/.cache/eelinux`). `JOBS` values are what was used on a 12-core host;
use `JOBS=2` on memory-constrained WSL. Times are wall-clock on that host.

| Step | Command | Time |
| --- | --- | --- |
| Tests | `python3 -m unittest discover -s tests` | 1 min |
| R5900 glibc toolchain | `make toolchain JOBS=10` | ~15 min |
| ps2dev SDK | `make ps2dev PS2DEV_LOCK=toolchain/ps2dev-binary-lock-v2.0.0.json` | 1 min |
| IOP firmware | `make firmware JOBS=2` (the IOP build accepts 1 or 2 jobs) | 1 min |
| R5900 user emulator | `make emulator-userland-build JOBS=3` | 2 min |
| Kernel port | `make kernel-port` (exits 2 by design: 64 recorded rejects) | 1 min |
| Kernel API/platform | `make kernel-modernize` | 1 min |
| Native-GS tree | `make kernel-gs-tree` | 1 min |
| Native-GS reference kernel | `make kernel-gs-reference ROOT_DEVICE=/dev/sda2 VIDEO_MODE=576p JOBS=6` | ~3 min |
| USB kernel | `make usb-kernel VIDEO_MODE=576p JOBS=6` | ~1 min |
| DVD kernel | see below | ~3 min |
| Full userland, Xorg, apps, locales | `make userland PROFILE=full JOBS=6` | ~1 h 45 min |
| Media | see below | 1 min |

### Kernels

The 7.2.8 source gate stays closed (`kernel-audit` fails with the recorded
rejects), so `make kernel` and `make all` refuse to build. The released media
kernels were built by hand from a native-GS tree outside that gate. The three
`kernel-gs-*`/`usb-kernel` targets reproduce that path:

- `kernel-gs-tree` copies the modernized tree to
  `$WORK/sources/linux-7.2.8-ps2-gs` and applies
  `ps2-kernel-warning-cleanup.patch` (required for `W=1 -Werror`), then the 14
  `ps2-gs-*.patch` files in the documented order: framebuffer through batch,
  then lut, map, map-blit, rgba-transfer, surface-capacity.
- `kernel-gs-reference` runs `kernel.py build --allow-open-gate` on that tree
  to produce the reference build directory (`$WORK/kernel-7.2.8-ps2-gs`) that
  the media kernels derive from. The flag is opt-in, prints a warning, and the
  report records `"source_gate_passed": false`.
- `usb-kernel` runs `scripts/kernel_usb.py`, producing `EELINUX.ELF`,
  `kernel.config` and `kernel-report.json` under
  `$WORK/images/usb-kernel-$(VIDEO_MODE)`. Those are the `MEDIA_KERNEL*`
  inputs for packaging. (The Makefile's default `MEDIA_KERNEL_REPORT` points at
  `kernel.py build` output, which lacks the fields the USB packager checks.)

`kernel_dvd.py` patches its `--source` in place (it adds the CDVD driver), so
give it its own copy of the native-GS tree:

```sh
cp -a ~/.cache/eelinux/sources/linux-7.2.8-ps2-gs ~/.cache/eelinux/sources/linux-7.2.8-ps2-gs-dvd
make dvd-kernel-pal KERNEL_SOURCE=$HOME/.cache/eelinux/sources/linux-7.2.8-ps2-gs-dvd \
  MEDIA_KERNEL_BUILD=$HOME/.cache/eelinux/kernel-7.2.8-ps2-gs JOBS=6
```

The GS kernel unit tests look for `~/.cache/eelinux/sources/linux-7.2.8-ps2-gs-surfaces`.
Point `PS2_SURFACE_KERNEL_SOURCE` (or a symlink with that name) at the native-GS tree;
with it, all 397 tests pass.

### Media

Packaging runs outside the bubblewrap overlay (see [Without root](#without-root)).
It needs `mtools` and `dosfstools` as well as `genisoimage`. Without a
`ROOT_PASSWORD_HASH_FILE`, the images use the documented `root` / `eelinux`
login. `MEDIA_OUTPUT` must be a new directory.

```sh
K=$HOME/.cache/eelinux/images/usb-kernel-576p
make pal-media STAGING=$HOME/.cache/eelinux/rootfs-full \
  MEDIA_KERNEL=$K/EELINUX.ELF MEDIA_KERNEL_CONFIG=$K/kernel.config \
  MEDIA_KERNEL_REPORT=$K/kernel-report.json \
  MEDIA_DVD_KERNEL_DIR=$HOME/.cache/eelinux/images/dvd-kernel \
  MEDIA_OUTPUT=$HOME/eelinux-media/pal
```

This writes `eelinux-pal-desktop-2gb-usb.img`, `eelinux-pal-slim.iso`,
`eelinux-pal-fat-2.10-2.12.iso`, `eelinux-pal-fat-3.04.iso`, `SHA256SUMS`,
`BOOT-README.txt` and the JSON records that the packager verifies.

The NTSC variant is tracked separately. As a data point, `make usb-kernel
VIDEO_MODE=480p` already builds a USB kernel with
`video=ps2fb:720x480p@60`. The DVD kernel and the media names are still
hard-coded to PAL.

## 3. Fixes needed to reproduce from a clean checkout

These are part of this change. Each was found while following the existing docs
on a fresh host:

- **ps2dev SDK pin is gone upstream.** `toolchain/ps2dev-binary-lock.json` pins
  the moving `releases/download/latest` asset; upstream replaced it on
  2026-11-20 and the pinned asset ID now returns 404, so `make ps2dev` fails its
  checksum. `toolchain/ps2dev-binary-lock-v2.0.0.json` pins the immutable
  `v2.0.0` release, selected with `PS2DEV_LOCK=`. The download cache name now
  includes the hash, so the two locks do not collide.
- **`driver-correctness.patch` did not apply** to the vendored
  `ohci-ps2.c`. Two hunks only converted double-encoded UTF-8 (`JÃ¼rgen`) in a
  local tree back to `Jürgen`. The vendored snapshot is already correct UTF-8,
  so those hunks are dropped; the functional `ioremap` hunk is unchanged.
- **Port-patch guards compared line numbers.** Eleven `kernel_*_port.py`
  installers regenerate their patch and refused to continue unless it was
  byte-identical to the committed one, including `@@` line offsets. Those
  shift when earlier steps change the same file (e.g. `init/main.c`), so a clean
  `make kernel-modernize` stopped at the DMA/fault step. They now compare hunk
  content and ignore only the offsets (`common.same_patch_content`), and they
  keep the committed patch instead of rewriting it.
- **`ps2-smap.patch` was stale.** `kernel-modernize` regenerates it from
  `kernel/new-drivers/ps2-smap`, which already contains the r44 PHY-loopback
  handoff (`smap_phy_prepare`). The committed patch now matches.
- **`kernel.py build` re-checked the correctness patch** after the GS patches
  had edited the same drivers, so neither direction applied. `port` now
  records `driver_correctness_patch_sha256` in its `.eelinux-port.json`
  marker, and `build` skips the re-check only when that digest matches the
  current patch. A tree ported before this change has no digest and fails
  closed; re-run `make kernel-port` (and the later kernel steps) on a fresh
  source.
- **`--allow-open-gate` waives rejected hunks only.** `kernel-gs-reference`
  passes it because the media kernels were built with `.rej` files still in
  the tree. Missing PS2 drivers or Kconfig symbols and a kernel version
  mismatch still fail the build. The boot ELF report lists the waived reject
  files in `open_gate_waived_rejects`.

The userland failures below only appear in a single clean pass. An incremental
workspace hides them because packages built before a version bump stay in the
target tree (an old `expiry` binary, a `tar` built against the previous libacl,
and so on). Each fix is a checksum-guarded `userland/external.mk` hook, the
same pattern as the existing package fixes:

- **tar 1.35 vs libacl 2.4.0.** libacl 2.4 exports `acl_get_file_at`,
  `acl_set_file_at` and `acl_delete_def_file_at` with types that conflict with
  tar's private static wrappers. The patch renames tar's wrappers
  (`tar-1.35-acl-2.4-wrapper-names.patch`).
- **host-tcl 9.1.0 picked the wrong `crypt.h`.** `HOST_DIR/include` comes
  first on the include path and holds host-libxcrypt's `<crypt.h>`, so
  `tclZipfs.c` missed minizip's `crypt.h`. The patch includes it by its bundled
  path (`tcl-9.1.0-minizip-crypt-header.patch`).
- **wget 1.25.0 vs OpenSSL 4.1.0.** OpenSSL 4 removed `SSLv3_client_method`
  without defining `OPENSSL_NO_SSL3_METHOD`. With the patch,
  `--secure-protocol=SSLv3` fails cleanly, as it does on SSLv3-less OpenSSL 3
  builds (`wget-1.25.0-openssl4-sslv3.patch`).
- **shadow 4.20.3 no longer installs `expiry`**, but Buildroot's permission
  table still lists it, so `makedevs` stops while generating `rootfs.tar`.
  `userland/package/shadow/latest-buildroot-recipe.txt` (a `recipe_override`
  in `configs/latest-packages.json`) drops that line.
- **patchelf broke 14 glibc CJK gconv modules.** Buildroot's RPATH sanitizer
  (host-patchelf 0.13) moved `.MIPS.abiflags` into a new segment but left
  `PT_MIPS_ABIFLAGS` empty and pointing at overwritten bytes. The project's own
  ELF ABI audit caught it (`EUC-JP-MS.so: incompatible MIPS ABI attributes`).
  This is a runtime problem too: glibc's MIPS loader reads ABI flags through
  that segment, which would break `iconv` for EUC-JP, Shift_JISX0213,
  ISO-2022-JP, EUC-KR, EUC-CN and others. `userland/scripts/fix-mips-abiflags.py`
  runs from `post-build.sh` and points the segment back at the relocated
  section.

The native development tools (`scripts/native_development.py`) build a second
copy of some of the same packages:

- `userland/package/tar/recipe.json` and `userland/package/wget/recipe.json`
  apply the tar and wget patches above.
- Build-host helper programs now use `-std=gnu17` (`BUILD_CFLAGS`,
  `CFLAGS_FOR_BUILD`), like the target code. GCC 15 and newer default to C23,
  where bash 5.3's `mkbuiltins` fails (`'bool' cannot be defined via
  'typedef'`). Debian's GCC 14 defaults to gnu17, which is why this did not
  show up on the original build host.
- less 710 removed `lesskey` and moved `lessecho` to `/usr/libexec`, so the
  coverage list in `configs/gnu-tools.json` now names only `less`.

## 4. Verification and known differences

Run on 2026-11-21 from fork commit `6f8435a` plus this change:

- `make check` equivalents: 397 unit tests OK (88 skipped), with
  `PS2_SURFACE_KERNEL_SOURCE` pointing at the native-GS tree; 31 kernel
  integration tests OK; `verify-imports` checked 437 imported sources.
- Kernels: native-GS reference, USB (`root=/dev/sda2`,
  `video=ps2fb:720x576p@50`) and DVD (`root=/dev/ps2dvd`) build with
  `W=1 KCFLAGS=-Werror`. Kernel reports record `source_gate_passed: false`
  (64 recorded rejects, unchanged).
- Userland, in a fresh `WORK`: all Buildroot desktop packages, the native compiler, libraries and
  development tools, the native Xorg driver and the 70 X.Org application
  releases built and passed the target ELF/ABI audit and command coverage
  check. The fresh run stopped twice on problems listed in section 3
  (less coverage, then native tools). After each fix it was resumed with
  `userland_full.py --reuse-desktop-report` (plus the compiler/libraries
  reports for the tools stage) instead of rebuilding the finished stages. A
  single uninterrupted pass with this change has not been timed.
- Media: the packager's own checks passed: FAT32 BPB and cluster count,
  read-only `fsck.fat`, byte-exact kernel/README readback, byte-exact root
  payload, Rock Ridge root manifest and per-file readback on the DVDs.
- CJK `iconv` (EUC-JP, Shift_JISX0213, ISO-2022-JP, EUC-KR) round-trips under
  the R5900 Linux-user emulator. With the pre-fix `PT_MIPS_ABIFLAGS` restored
  on `EUC-JP.so`, glibc rejects the module (`conversion to 'EUC-JP' is not
  supported`). With the fix it converts correctly.

Known differences from the published r44 images:

- **Not byte-identical.** The toolchain differs from the recorded hashes in
  `toolchain/latest-validation.json` (build paths and host compilers are not
  normalized), and media embed build-specific kernels and staging trees. Only
  the file formats, layout, sizes and the packager's checks can be compared.
- **IOP firmware**: `ata`, `gamepad`, `irq` and `printk` `.irx` match the hashes
  in `docs/iop-dev9-validation.json` exactly. `dev9.irx` (28,300 bytes against
  the recorded 16,108) and `irqrelay.irx` differ, and this is expected. The
  validation record (2026-10-02) is for the older DEV9 module `0x0101`, while
  the current recipe (`scripts/iop_dev9.py`) builds `0x0102` with
  `module/smap-service.h` and an updated `irqrelay.c`. The ps2dev SDK change
  is not the cause: both builds use `mipsel-none-elf-gcc` 15.2.0, and the four
  unchanged modules are byte-identical. There is no recorded hardware
  validation of the `0x0102` modules from this build yet.
- **Native GS sources**: the generated GS headers match the r43 record where
  it lists the same files (for example `ps2-gs-surface.h` `30c937b6…`). The
  repository holds no r44 kernel hashes to compare against.
- **Userland versions** follow the pinned `configs/latest-packages.json`
  (OpenSSL 4.1.0, shadow 4.20.3, less 710, and so on). less 710 removed the
  `lesskey` program and installs `lessecho` in `/usr/libexec`, so
  `configs/gnu-tools.json` now lists only `less`.
- **Hardware**: none of these images has been booted on a console or in PCSX2
  yet. Use the [real-hardware test procedure](hardware-test.md) (USB stick
  through Tuna/wLaunchELF, then the observation checklist in its section 3).
