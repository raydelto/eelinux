# Building eeLinux

## Host prerequisites

To rebuild the release media on a fresh host, step by step, follow
[reproducible-build.md](reproducible-build.md).

For the current X.Org applications and test-free PAL DVD/2 GB USB packaging,
see [pal-desktop-media.md](pal-desktop-media.md). Use `make xorg-apps`,
`make native-xorg` on the full userland stage, then `make dvd-kernel-pal` and
`make pal-media` with the matching SDK, PAL `/dev/sda2` USB kernel and native
optical-root DVD kernel. DVD creation additionally requires `genisoimage`;
application builds require gettext and itstool.

Debian/Ubuntu Linux, Python 3.12+, Git, GNU make, a C/C++ host compiler and these
packages (run package installation on your host, not on the console):

```sh
sudo apt-get install build-essential git bison flex gawk texinfo bc \
  libgmp-dev libmpfr-dev libmpc-dev libssl-dev libncurses-dev \
  rsync cpio file patch pkgconf autoconf automake libtool wget unzip \
  fakeroot e2fsprogs kmod python3
```

Set `WORK` to a native Linux directory. The default is `$HOME/.cache/eelinux`.
Use `JOBS=2` with memory-constrained WSL installations; concurrent GCC generator
compilation can exhaust host RAM with large job counts. All target programs
use little-endian MIPS **o32**, R5900 instructions, soft float and the R5900
short-loop erratum workaround. Generic MIPS32r2 distributions are incompatible.

## Linux 7.2.8 source and port

Kernel choices live in [`configs/kernel.config`](../configs/kernel.config).
It contains the baseline and a marked optional-networking section in one file.
Modern builds use both sections; the legacy baseline reader retains its original
choices. USB/DVD builders apply only the networking section to their qualified
reference configuration, preserving its root device, initramfs, video and other
media-specific settings.

```sh
make kernel-port
make kernel-modernize
make kernel-audit
```

Source checksum is locked in `kernel/sources.json`. The transplant adds the
actual platform and driver source files and attempts the modified architecture
and core hunks. The report records rejected changes. Resolve every relevant
architecture and API defect, remove resolved rejects only after review, and
cross-compile the selected configuration before treating the tree as usable.
Never replace 7.2.8 core files wholesale with the 5.4 snapshots.

The active source tree is `$WORK/sources/linux-7.2.8`, outside the Windows source
checkout. From Windows, access it through the WSL filesystem, for example
`\\wsl.localhost\Debian\home\machaddr\.cache\eelinux\sources\linux-7.2.8`.
The repository's `kernel/vendor/frno7/snapshot` is an immutable reference delta,
not a complete kernel tree. Local fixes belong in `kernel/ports/7.2.8`.

## Two cross-compilers

The [ps2dev SDK](https://github.com/ps2dev/ps2dev) builds homebrew and IOP firmware.
Its bare-metal/newlib toolchain cannot link a GNU/Linux userland. Install that
SDK according to its instructions, keep the `PS2DEV` path free of spaces, and
put its IOP tools on `PATH`. The firmware target requires `mipsel-none-elf-gcc`,
`ld`, and `objcopy`; a successful build is required before accepting IRX files.
Set `IOP_CROSS_COMPILE` to an explicit compatible prefix when needed.

```sh
make toolchain                 # Separate Linux/glibc R5900 C/C++ compiler
make ps2dev                    # Install the checksum-pinned official x86_64 SDK
make firmware                  # Actual ps2dev IOP module build
```

GNU compiler sources and checksums are locked in `toolchain/source-lock.json`. The
current Linux compiler is GCC 16.2.0 with binutils 2.47 and glibc 2.44, installed under
`$HOME/.local/eelinux-linux-latest`; its work directory is
`$HOME/.cache/eelinux-linux-latest/toolchain`. All four stages built and installed.
Dynamic/static C and C++ smoke tests passed, including multiplication, software floating
point, TLS, threads and atomic flags in experimental R5900 Linux-user emulation.
[Toolchain evidence](../toolchain/latest-validation.json) records the archive, recipe,
compiler and runtime hashes. The prior GCC 14.4.0/binutils 2.44/glibc 2.41 prefix and
artifacts are preserved.

Each build stage has a checked completion marker and logs. The bootstrap's
headers use the established PS2 Linux 5.4.221 port, so the userspace ABI can be
tested independently. The actual generated MIPS o32 syscall headers are checked.
After the modern port works, rebuild the compiler in a new work directory with
7.2.8 headers; keep the glibc minimum supported kernel consistent with the
intended boot kernel. The SDK root pin documents provenance; its optional
upstream dependency scripts contain moving revisions, so a pinned/versioned
installed SDK is preferable for repeatable firmware builds. Applications and
compiler runtimes explicitly use `-mno-llsc`; glibc 2.44 uses `-mllsc` to retain
its required atomic operations. Those instructions require the PS2 kernel's
reserved-instruction emulation. Linux-user smoke tests translate syscalls to
the host kernel and cannot qualify that kernel path on physical hardware.

The current firmware target builds all six real IRX modules with the checked
DEV9/IRQ overlays. They implement the bounded DEV9 lease/control exchange and
shared SMAP IRQ rearm protocol, retaining the existing ATA export ABI. See
[IOP build evidence](iop-dev9-validation.json) and
[native Ethernet implementation](ethernet.md).

## Userland and images

The complete native development suite uses the existing toolchain/settings:

```sh
make plan PROFILE=full JOBS=2
make userland PROFILE=full JOBS=2
```

This stages `$WORK/rootfs-full`, with logs and safe-strip evidence under
`$WORK/userland-latest`. See [the full build workflow](userland.md) for package
recipes, source locks and stage preservation. `make image` creates a root
filesystem; the FAT32+ext4 USB disk packager is a separate step.

The desktop/full build installs the native PS2 GS driver and applies the Xorg
image-censoring and TWM window-list patches. Keyboard presets, `loadkeys`, desktop
settings and the native GNU library cache are included by the same build scripts.
TWM's list entries follow window titles and update and re-sort when those titles
change. `make xorg-apps` applies the package's numbered patches automatically;
use a fresh `WORK` when changing a previously built package's patch set.
The desktop package selection enables MIT-SCREEN-SAVER for `beforelight`.
UTF-8 locale generation uses the matching target `localedef` through the pinned
R5900 QEMU, so build it with `make emulator-userland-build` first. Set
`EMULATOR_WORK=/path/to/existing/emulator-userland` to reuse a verified build.
Both GNU libc locale data and libX11's locale definitions are installed;
the login profile keeps `C` as its default while preserving an explicitly set
`LANG`. It no longer forces `LC_ALL=C`, so uxterm can select its UTF-8 locale.
`make locales` regenerates those files in an existing staging tree, using
`LIBC_SYSROOT` and, for desktop/full, `XORG_SDK`. These changes do not require
manually patching a finished image.

```sh
make userland PROFILE=base JOBS=1 WORK="$HOME/.cache/eelinux-system-latest"
make kernel PROFILE=base WORK="$HOME/.cache/eelinux-system-latest" # Source audit must pass
make image PROFILE=base WORK="$HOME/.cache/eelinux-system-latest"
```

The Buildroot release, its R5900 architecture patch, package selections and
additional native-GCC recipe are in `configs/packages.json` and `userland/`.
Use the same fresh `WORK` for source/port/modernizer and firmware targets before
the commands above. An older userland cache is rejected when its compiler or
recipes differ; a new workspace preserves those prior results.
The complete pinned SysVinit source is in `userland/sysvinit`; the local source
override compiles its real PID 1 and administration tools. Boot, shutdown,
single-user access and ordered services are configured in the rootfs overlay.
`boot/early-root.c` only performs the initial disk-root handoff to `/sbin/init`.
Buildroot verifies package source hashes. Unknown/discarded Kconfig selections,
wrong CPU/ABI, missing GNU tools, and missing ELF runtime dependencies stop
the build. `full` includes native GCC and development tools; it requires external
GCC 16.2.0 so compiler headers and runtime libraries match. Source and license
material can be collected with the Buildroot `legal-info` target.

The separate latest-GNU base has built all 38 concrete target packages and 32
host dependencies. Its 310 regular Linux ELF files and 78 current console
command providers pass the R5900/o32/soft-float and loader/library audits;
eight direct GNU program smoke tests pass in R5900 Linux-user emulation.
Bash and ed select their supported pre-C23 dialect with GCC 15 or newer, while
Nano's two pointer truth tests are made explicit with a checksum-guarded patch.
The ncurses ABI is preserved. Exact build/ELF/smoke records are in
[userland validation](../userland/validation.json). The `desktop` and `full`
profiles resolve through Kconfig but have not been compiled.

The current kernel selection builds native SMAP, PHY support and the ordered
SIF/IOP bridge into the kernel. USB Ethernet and all other Ethernet vendor
drivers are disabled, including runtime requests to load them. USB storage
and keyboard/mouse drivers remain. The integrated GCC 16.2.0 built-in SMAP
compile proof has 24 modules; the new firmware-equipped ext4/root2 audit removes
the unused AX88796B PHY module and targets 23 remaining modules. Use the final
kernel manifest for the installed module list. No physical SMAP packets have
been demonstrated. The modern architecture source gate remains closed with
64 recorded rejected files; the ordinary kernel target cannot bypass it.

Output is `$WORK/images`: kernel ELF with a build manifest, `rootfs.tar`,
`rootfs.ext4`, and filesystem hash/size metadata. The default ext4 uses 4 KiB
blocks, a 4 MiB internal journal and explicit supported features. Filesystem
creation and PAM-sensitive permissions are prepared on native Linux storage
even if the destination is on the Windows drive. Each new image must pass an
offline `e2fsck` before publication. `--filesystem ext2` is retained only for
reproducing historical audits; older ext2 artifacts remain preserved.

This output is a filesystem image, **not a partitioned USB installer**. No
physical disk is automatically formatted, repartitioned or written. An existing
ext4 partition matching `ROOT_DEVICE=/dev/sda1` is required for the default
initramfs path. The separately prepared FAT32-plus-ext4 hardware image uses
partition 2: build its kernel with `ROOT_DEVICE=/dev/sda2`, and follow the
[hardware test instructions](hardware-test.md).
If multiple USB disks are connected, `/dev/sda1` is ambiguous; persistent UUID
selection remains a TODO.

Default accounts are locked. To make console login available, supply an existing
SHA-512 crypt password hash in a private file outside the source tree:

```sh
make image PROFILE=base ROOT_PASSWORD_HASH_FILE=/absolute/private/root-hash
```

The hash file is read only during image assembly and is not copied to build
reports. For desktop operation, create an ordinary user using the installed
`useradd`/`passwd` tools and grant the appropriate device access after driver
validation. No graphical login manager or remote login service starts by default.

## Explicit reference kernel

```sh
make reference-system PROFILE=base
```

This selects the pinned existing 5.4.221 PS2 port explicitly. It is useful for
toolchain/firmware/rootfs bring-up; it is not a modern
kernel implementation. The standalone compressed ELF follows frno7's PS2 boot
path and can be launched by an existing homebrew ELF launcher after it passes
the image checks. Do not distribute or flash an audit kernel with an empty
`/init`; compilation-only fixtures are not boot images.
