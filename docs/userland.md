# GNU userland and the TWM desktop

`scripts/userland.py` cross-builds real GNU applications through the official,
SHA256-pinned Buildroot 2025.02.18 LTS release. Its external tree adds an explicit
R5900 architecture selection, a scoped external GCC16 capability extension and native GNU development packages that Buildroot
normally builds only for the workstation. `plan` is an offline recipe inventory;
`configure` resolves dependency selections; `build` compiles and stages packages.
Neither a resolved configuration nor a successful cross-build proves a PS2 boot.

| Profile | Included functionality | Deployment |
| --- | --- | --- |
| `base` | GNU shell, file/text tools and editors; SysVinit, util-linux, shadow, procps-ng, kmod, ext/FAT filesystem tools, iproute2, iputils, dhcpcd and syslog; SSH/SFTP clients, HTTPS curl, rsync, jq, strace, lsof, tcpdump, ethtool, file, tree, mtools and ZIP utilities | Disk-backed command-line system |
| `desktop` | Everything in `base`; Xorg, evdev, PS2-themed TWM, xterm, xinit, xauth, keyboard data and bitmap/cursor fonts | Native PS2 DDX integration passes the extended 32 MiB emulator session; physical qualification remains separate |
| `full` | Everything in `desktop`; native GCC/G++ 16.2.0, Binutils 2.47, glibc 2.44 SDK, GDB 18.1, Make, m4, Bison, Flex, Autoconf, Automake, Libtool, Perl 5.44.0, pkgconf, nano, Vim, Screen, Wget, Lynx, Python and certificates; development headers and static libraries | Disk-backed system and swap; compilation/runtime acceptance is recorded separately |

## Build the complete development userland

From the project directory in Linux/WSL, with the existing Linux toolchain:

```sh
make plan PROFILE=full JOBS=2
make userland PROFILE=full JOBS=2
```

`WORK`, `TOOLCHAIN`, `CROSS_COMPILE` and `JOBS` retain their existing Makefile
meaning. For a separate build, use `WORK="$HOME/.cache/eelinux-personal"`.
The result is `$WORK/rootfs-full`; component logs, source/configuration locks,
the full report and `strip-report.json` are under `$WORK/userland-latest`.
Use a new staging directory when rebuilding so completed stages are preserved.
Completed packages resume only when their locked inputs agree.

The full entry point is `scripts/userland_full.py`. It builds the selected base
and desktop, isolated native compiler and library components, current tools,
and the production PS2 DDX/Mesa GS components. It then merges the matching SDK,
removes diagnostic clients/services, strips the private stage and audits the
MIPS ABI, dynamic libraries and required commands. It does not install a Mesa
software renderer or advertise completed shader/GLX support.

Every package has an entry under `userland/package`; the current tool builder
reads its `recipe.json` directly. Package archives are downloaded by their
locked upstream URL and SHA-256 into the selected build's `WORK/downloads`
cache. The optional, ignored `userland/sources` directory can supply existing
verified archives read-only, but is not required in a source-only checkout.
`userland/package-index.json` identifies recipes,
versions and build state. The baseline desktop/library pins in
`configs/packages.json` remain distinct from the current native tool pins in
`configs/native-tools-latest.json`. GNU signatures are checked where recorded in
the source inventory; GDB uses
the publisher's SHA512 manifest, while other source archives have recorded
HTTPS origin and hashes. Versions are deliberately locked rather than fetched
from a moving "latest" URL during a build.

`make check` does not download upstream packages. Integration checks that
extract real upstream sources report an explicit skip when their optional
archive is absent. To run them with a prepared cache, set
`EELINUX_TEST_SOURCES=/absolute/path/to/archives`; the independent recipe,
source, protocol and arithmetic checks continue to run without that cache.

Safe stripping uses the matching target Binutils. Executables/shared libraries
lose unneeded symbols while retaining dynamic exports, ELF ABI/entry points and
loader dependencies. SDK archives and relocatable objects lose debug data only;
their public/undefined symbols are checked and archive indexes rebuilt. Hardlink
aliases, permissions and timestamps are retained. IOP firmware and kernel
modules are preserved byte-for-byte. Headers, static libraries, licenses and
source archives remain available for native development. Original source/build
trees are not stripped.

Native GCC defaults to R5900/o32/soft float, enables the short-loop workaround
and disables LL/SC instructions through `userland/configs/native-gcc.specs`.
The options are `-march=r5900 -mabi=32 -msoft-float -mfix-r5900 -mno-llsc`,
with little-endian output. The installed specs also supply the C++ unwind
libraries and exception headers needed by the native Canadian compiler build.
glibc 2.44 targets R5900 and uses the existing Linux LL/SC trap-emulation policy.
IOP firmware targets its separate R3000 CPU and is preserved by its hash manifest.
The shell uses `/var/tmp` for temporary compiler files so `/tmp` RAM use does not
consume the console's 32 MiB. The personal image adds disk-backed swap; large
builds remain limited by the console's CPU and memory. Wget includes OpenSSL
HTTPS; its optional IRI/libidn and libpsl integrations are disabled in this
package recipe.

Python's SSL, SQLite, libffi 3.8.0 callbacks, readline and compression modules, plus
Lynx and the GNU build tools, pass the same check. The certificate finalizer installs
144 locked CA certificates, a combined bundle and OpenSSL hash links. This check
exercises stripped binaries; PS2 kernel, 32 MiB RAM, swap and physical boot
qualification are recorded separately. This confirms native compilation works within
that memory configuration; large translation units remain slow on the console and
interpreter.

The clean personal image boots to a normal console login. Neither native SDK checks nor
graphics tests run at boot; their services, clients and old logs are excluded. Start the
desktop manually with `startx`. Use the existing Tuna/wLaunchELF setup to launch
`mass:/EELINUX.ELF`; log in as `root` with password `eelinux`, then change it with
`passwd`.

The full GNU command-line suite is genuine GNU software. Coreutils uses its own
multi-call implementation and symlinks to reduce storage. dhcpcd 10.5.2 supplies
DHCP, address/route configuration and lease renewal. BusyBox is disabled; the
build and image assembler reject its executable and applet links. The network
service backgrounds dhcpcd immediately, and its Bash hook updates DNS atomically.
[`configs/gnu-tools.json`](../configs/gnu-tools.json) now declares the expanded
command contract for base, desktop and development builds, including 105 GNU
Coreutils commands. The expanded catalog contains more than 570 commands with
native development tools. Xorg's applications and other
upstream aliases add commands beyond this minimum catalog.
The earlier image's 199-command audit remains historical
evidence; adding selections requires rebuilding and auditing the new stage.
All 105 Coreutils names resolve to GNU's `coreutils` executable in that earlier
image, including `groups`. The former
BusyBox build supplied only `udhcpc`; dhcpcd replaces its lease lifecycle and
domain-search handling. Linux system utilities and the explicitly requested BSD
dhcpcd client retain their own licenses.

### Administration and command discovery

The expanded build selects real upstream tools for storage inspection, loop
devices, partition tables, filesystem checks, scheduling, namespaces, processes,
network diagnosis, encrypted transfers, archives and JSON. These include
`losetup`, `partx`, `wipefs`, `zramctl`, `findmnt`, `lsfd`, `logger`, `chrt`,
`ionice`, `taskset`, `unshare`, `debugfs`, `dumpe2fs`, `vmstat`, `pmap`, `watch`,
`ssh`, `scp`, `sftp`, `curl`, `rsync`, `jq`, `strace`, `lsof`, `netcat`,
`tcpdump`, `ethtool`, `mkfs.fat`, `fsck.fat`, `mcopy`, `zip`, `unzip`, `file`
and `tree`. The desktop also includes `lsusb`. SSH clients and key utilities
and a manually started SSH server are included. The expanded tools do not
add background services to the desktop startup path.

The base build retains the matching glibc character-conversion modules, including
the DOS code pages used by mtools. They load when requested and leave the default
`C` locale unchanged. The builder also imports native `ldconfig` before generating
the library cache used by the desktop and other dynamically linked commands.

Run `ps2-tools` to browse installed commands by upstream suite, or select one:

```sh
ps2-tools util-linux
ps2-tools openssh
ps2-tools --list
ps2-tools --check
```

The desktop's **Applications > Tools > Command and administration tools** entry
opens the same browser in a terminal.

The catalog records the build's expected commands. The browser checks executable
availability on the running system, and `--check` returns failure if an expected
tool is missing. Use each command's own help or manual for its options. Tools
that use optional kernel facilities still depend on the running kernel and
connected hardware; their presence alone does not qualify those facilities.

Shadow 4.20 removed the deprecated `expiry` program. Use `chage -l ACCOUNT`
to inspect account and password expiration. The current command catalogue follows
the [upstream removal](https://github.com/shadow-maint/shadow/pull/1481);
the image does not retain an obsolete set-user-ID `expiry` binary.

### Current tool releases and compatibility

The current CLI and networking release overlay is pinned in
[`latest-packages.json`](../configs/latest-packages.json), with primary release
URLs, archive SHA256 checksums, reviewed recipe changes and exact hashes for
retired upstream patches. `scripts/latest_packages.py` applies these changes
before Buildroot evaluates package recipes. Sources and licenses are checked
before publishing a prepared backend; changed inputs require a new work directory.

The selections include Nmap, Ncat, Nping, iperf/iperf3, MTR, traceroute, tcpdump,
conntrack, nftables, iptables, ipset, WireGuard, OpenVPN, wireless configuration,
CIFS/NFS clients, DNS diagnostics and Unbound, FTP/TFTP clients, socket tools,
traffic monitors, tmux, htop, ncdu, Midnight Commander, SQLite, ACL/attribute tools,
Expect, terminal dialogs, archive formats and Bash completion. Network servers
start only when explicitly invoked; these tools add no desktop startup services.
See [network administration](network-tools.md) for manual SSH host-key setup
and an Unbound configuration with writable runtime state.

OpenSSL 4 is the primary new API; a separate current OpenSSL 3 provider retains
the existing desktop's versioned ABI and uses private provider/config directories.
Tcl 9.1 is primary, with the maintained Tcl 8.6 ABI for stable Expect. Ncdu uses
the maintained C 1.x release because the 2.x implementation requires Zig, whose
R5900 target is unavailable. The build does not claim unsupported atomic CPU
operations to enable otherwise incompatible packages.

Both OpenSSL SDKs pass dynamic linking and ordinary static crypto/TLS checks;
shared applications also load their matching legacy providers successfully.
Fully static executables loading a module with dynamic TLS currently crash
under the R5900 Linux-user emulator. The same failure reproduces without
OpenSSL and in a host-native control; physical PS2 behavior is unqualified.
See the [SDK checks and isolated TLS diagnostics](../build/network-work/dual-openssl-sdk-runtime.json).
The shipped clients use shared crypto, so this separate static-TLS limitation
does not block media composition.

The native tool inventory was rechecked on 2026-11-06. Updated sources include
Python 3.15.2, Vim 9.2.1302, nano 9.3, GNU patch 2.8.1 and pkgconf 3.0.10.
The official stable patch series is also pinned and applied for Bash 5.3.20
and Readline 8.3 patch level 6.
Their new build/runtime qualification is separate from the historical r43
images. The qualified compiler, graphics stack and matching SDK are preserved
when composing the new media.

The dhcpcd release archive and license are locked in
[`dhcpcd-sources.json`](../userland/dhcpcd-sources.json). Its Buildroot recipe and
standalone `scripts/dhcpcd.py` cross-build have both compiled successfully with
the project GCC16/glibc toolchain. The client payload occupies about 341 KiB.

## SysVinit source and PID 1

The default keyboard is **English UK**. The `S15keyboard` service loads the
upstream UK console keymap into `/dev/tty1` after device setup and before login.
Xorg's `Keyboard0` uses `XkbLayout "gb"`. This gives the UK positions for
`"`, `@`, `#`, `~`, `\` and `£`; it also applies to the USB keyboard in PCSX2.
The `loadkeys` process exits after installing the map and leaves no resident
keyboard daemon. After changing a console map, reload it with
`/etc/init.d/keyboard restart`.
Thirteen key-table checks now include `/`, `?`, comma/less-than and period/greater-than.
The project Windows PCSX2 runtime also fixes host punctuation translation by
using physical scan codes. The user confirmed correct keys at the text console.

The small keyboard payload is built separately from pinned kbd 2.10.0 and
installed into the GNU staging tree by `scripts/keyboard.py`. It includes only
the loader, the UK map's required includes and the upstream license; a complete
font/keymap collection is not added to the 32 MiB console image.

New `scripts/userland.py build` runs include this step automatically. For a
previously completed GNU stage, keep its original build evidence and install
the supplemental payload into a fresh copy:

```sh
python3 scripts/keyboard.py build --work ~/.cache/eelinux/keyboard-build --staging ~/.cache/eelinux/keyboard-payload --toolchain-prefix ~/.local/eelinux-linux-latest/bin/mipsr5900el-unknown-linux-gnu- --jobs 1
python3 scripts/keyboard.py install --payload ~/.cache/eelinux/keyboard-payload --staging /path/to/fresh-gnu-stage
```

The builder verifies the upstream developer signature and source hashes,
audits the target ABI and map includes, and retains a seven-file inventory.
It needs host `gpgv`, Bison, Flex, Perl, a C compiler and Make. Use fresh work
and payload directories when the recipe changes.

The complete upstream SysVinit 3.13 implementation is stored at
`userland/sysvinit/`, including its C sources, manuals, documentation, contributed
tools and original license files. Its `manifest.json` records the official
release URL, archive SHA256 and the SHA256 of all 104 imported files. The original
files are unmodified. `scripts/userland.py` audits that manifest before resolving
or building the userland.

Buildroot compiles this local tree through its normal SysVinit recipe using
`userland/configs/local-sources.mk`. Buildroot retains its internal `custom`
version for local-source build paths; the audited upstream version `3.13` is
passed explicitly to SysVinit's Makefile. It installs the real `/sbin/init` PID 1,
halt/shutdown controls, `/sbin/telinit`, `/sbin/runlevel`, fstab-decode, bootlogd,
logsave and its accounting/log diagnostics. Single-user authentication uses the
selected util-linux `sulogin` executable. The runtime overlay supplies `/etc/inittab`,
runlevel transitions and service scripts. The small `boot/early-root.c` program
only mounts the disk root from the initramfs and executes this SysVinit; normal
service supervision and child reaping are handled by the upstream implementation.

The following completed-build evidence uses the preserved GCC14.4.0/glibc2.41
toolchain and remains recorded in `userland/validation.json`. It does not certify
the new GCC16.2.0/glibc2.44 build.

The entire SysVinit suite has been compiled successfully in a separate temporary
directory on the Linux workstation. The upstream `init` was also cross-linked
against GNU glibc 2.41 with GCC 14.4.0: its stripped ELF is 40,904 bytes, and
`readelf` plus the project audit confirm R5900, little-endian o32 and soft float.
Its PID 1 executable was not run on the workstation. The complete GNU C/C++
toolchain is installed and passes the production C/C++ link, glibc `limits.h`
and PS2 ELF checks. The console userland cross-build now completes. PS2 boot
and runlevel transitions require hardware acceptance checks.
The recorded compiler, source-integrity and ELF results are in
`userland/validation.json`; that report distinguishes completed builds from
configuration checks and unperformed hardware tests.

All 14 upstream programs also clean-built with the finished compiler in an
isolated staging tree, without bootstrap-header overrides. The stripped suite
totals 190,640 bytes. ABI and loader/library checks passed; the original 104
files remained unchanged. [Detailed SysVinit evidence](sysvinit-validation.json)
records these results. No target executable was run on a PS2.

The production `base` profile installs a 69,696-byte stripped SysVinit `init`,
compiled with Buildroot's size and hardening flags. Its ELF and complete staged
loader/library dependency checks pass. The isolated 40,904-byte result above uses
the separate source-validation build flags.

## Toolchain and Linux host

Use the project Linux toolchain bootstrap or supply an equivalent external GNU
toolchain. The standard ps2dev `ee-gcc` toolchain targets bare-metal/newlib and
cannot link a GNU glibc Linux program. It remains useful for the PS2 loader and
IOP firmware; Linux applications require the Linux compiler.

The userland build checks that the compiler:

- Targets `mipsel-*-linux-*` or `mipsr5900el-*-linux-*` and has a GNU glibc sysroot.
- Accepts `-march=r5900 -mabi=32 -msoft-float -mno-llsc -mfix-r5900`.
- Links C and C++ programs and produces little-endian o32 ELF files with an
  instruction-set level compatible with the R5900. Imported executables and
  shared libraries also have their MIPS ABI attributes checked for soft float
  and compatible CPU extensions; generic MIPS32 and hard-float binaries fail.
- Supplies Linux headers >=4.14, which the selected shadow package needs.

The current project bootstrap pins GCC16.2.0, GNU Binutils2.47 and glibc2.44.
`full` requires GCC16.2.0 exactly and
the compiler's canonical target tuple so its installed C++ headers and compiler
runtime match the new native compiler. Other profiles accept GCC versions
supported by the pinned Buildroot release or its explicit external GCC16 extension.
The extension adds GCC16 to the external compiler choice and its version
capability chain. It does not replace the backend's host compiler or stock
package recipes. Native GCC and Binutils have separate, source- and
license-hashed external recipes. The native Binutils package cannot coexist
with Buildroot's older target Binutils binaries.
`-mfix-r5900` enables the R5900
short-loop workaround. The CPU has no hardware `ll`/`sc`; avoiding those
instructions does not eliminate the need to validate the Linux/glibc port's
thread and atomic-operation support on hardware.
The preserved GCC14.4.0 GNU/Linux compiler defaults enable the R5900 workaround in both
GCC and GAS. Its runtime-library builds use GNU/Linux's default `ll`/`sc` atomics,
which require the PS2 kernel's instruction emulation; application recipes pass
`-mno-llsc` explicitly.
The R5900 Buildroot patch disables the generic MIPS `__sync` capability claims;
the current diagnostic Xorg build consequently does not select libXshmfence or
DRI3. Future Mesa integration needs verified target atomics and its own driver.

Build on Linux or WSL2 with Python >=3.12, using a native Linux filesystem for the large working
directory. The repository may live under `/mnt/c`.
The CLI uses native Linux PATH entries, removing WSL's appended Windows paths
that Buildroot rejects or cannot use for its host tools.

Debian/Ubuntu dependencies:

```sh
sudo apt-get install build-essential bash bc binutils bison flex gawk git \
    gzip bzip2 xz-utils tar cpio unzip rsync file patch perl python3 wget \
    libncurses-dev libssl-dev
```

Use paths without whitespace. Do not put a full GNU root filesystem into the
32 MiB boot initramfs. The boot initramfs should mount a disk root and hand off
to SysVinit, allowing the kernel to page executables from storage.

## Build commands

The examples assume the latest bootstrap installation is at `$HOME/.local/eelinux-linux-latest`.
Adjust the prefix to the bootstrap's printed installation path. The CLI defaults
to `$HOME/.local/eelinux/bin/mipsr5900el-unknown-linux-gnu-`, `$HOME/.cache/eelinux/userland` for work,
`$HOME/.cache/eelinux/rootfs` for staging and two build jobs. `CROSS_COMPILE`
overrides the compiler default.
`--jobs` bounds both GNU Make and Buildroot's helper bootstrap concurrency.

```sh
python3 scripts/userland.py plan --profile desktop

python3 scripts/userland.py build --profile desktop \
    --toolchain-prefix "$HOME/.local/eelinux-linux-latest/bin/mipsr5900el-unknown-linux-gnu-" \
    --work "$HOME/.cache/eelinux/userland-gcc16" \
    --sysroot "$HOME/.cache/eelinux/rootfs-gcc16" --jobs 2
```

`--sysroot` names the **output root filesystem**, not the compiler sysroot. The
compiler's GNU glibc sysroot is obtained with `gcc -print-sysroot`. The script
stages files after every required package and ELF audit passes. It does not
install the project's runtime overlay; the root project build applies that
overlay afterward.

To inspect resolved package choices while the cross toolchain is bootstrapping:

```sh
python3 scripts/userland.py configure --profile desktop \
    --toolchain-prefix "$HOME/.local/eelinux-linux-latest/bin/mipsr5900el-unknown-linux-gnu-" \
    --work "$HOME/.cache/eelinux/userland-gcc16" \
    --assume-toolchain-version 16.2.0 --assume-kernel-headers 7.2
```

These explicit assumptions apply only to Kconfig resolution. `build` still probes
the installed compiler, libc, C++ runtime and actual header version before it
starts compilation. It rejects incompatible CPUs instead of silently selecting
generic MIPS32 or MIPS32r2.
Optional OpenMP, stack protector, Fortran, D and native RPC capabilities are also
determined with real link probes. Their external-toolchain declarations must
match the supplied compiler even when no application uses those interfaces.
`configure` assumes the project's OpenMP/SSP/SSP-strong capabilities; override
`--assume-toolchain-features` for a different bootstrap. These assumptions are
replaced by detected facts for `build`.

Build outputs under `--work` include:

- `output-PROFILE/target`: the Buildroot target staging tree, without final
  fakeroot ownership. Use it as build input rather than a chroot.
- `output-PROFILE/images/rootfs.tar`: the deployable tar filesystem, preserving
  ownership, setuid permissions and device metadata. The `full` profile adds its
  SDK headers, linker scripts and compiler support files to this archive.
- `output-PROFILE/packages-resolved.json`: the complete host/target dependency,
  version, source, patch and license inventory.
- `output-PROFILE/eelinux-build-report.json`: compiler identity, source/config
  locks, ELF audit outcome and the explicit hardware-validation status.

Input locks prevent reusing a completed build directory with different package,
toolchain or external-tree settings. Choose a fresh working directory after such
changes. Source archives are checked against their locked hashes. The official
Buildroot source archive locks the package recipes, source hashes and patches;
the native GNU extension packages also contain their own `.hash` files.
Use a fresh working directory for GCC16 builds. Earlier Buildroot caches and
GCC14.4.0 validation reports are preserved historical results; they do not
establish that the updated packages have built or booted. Compiler-version and
SDK checks still run against the installed toolchain before a real build starts.
The fresh GCC16 `base`, `desktop` and `full` configurations resolve successfully,
including the GCC16 capability chain and the separate native Binutils package.
The source archive and license hashes for both native recipes were checked
against the downloaded official releases. [GCC16 configuration evidence](userland-gcc16-validation.json)
preserves the earlier assumed-header configuration checks separately from the
completed latest compiler/base build. The actual latest compiler supplies
5.4.221 headers; its base build and eight smoke tests now pass. The latest
desktop packages also cross-build; the full profile and native compiler packages
remain unbuilt. Native accelerated Xorg/Mesa integration is separate work.

## Xorg, fonts and input

The desktop packages use modular Xorg 21.1.24 and `xf86-input-evdev`. The build
also includes `xf86-video-fbdev` for framebuffer diagnostics; hardware-only
runtime selects the unfinished `ps2` DDX and refuses to substitute fbdev.
Buildroot resolves the complete library chain, including X11,
Xau/Xdmcp, Xext, Xi, Xrender, Xrandr, Xft, Xfont2, Xcursor, Xinerama, Xfixes,
Xdamage, Xres, Xxf86vm, ICE, SM, Xt, Xmu and Xaw; xcb, X protocol headers, pixman,
freetype, fontconfig, font encodings, zlib, libpciaccess, libdrm and the small SHA1
implementation. Xinit, xauth, xkbcomp, X keyboard data, bitmap fonts and cursor
fonts are selected explicitly or by these package dependencies. Compositing
managers, a display manager and desktop environments are omitted. Mesa source
is pinned separately; a PS2 hardware driver and GLX are not yet implemented.

`desktop` and `full` select eudev because the evdev driver requires libudev. Its
large hardware database and persistent rule generator are disabled. The root
runtime must retain/start the small udev service or otherwise populate the event
devices used in its Xorg configuration. `base` uses kernel devtmpfs alone.
The project runtime provides the display/input configuration and the TWM session.
The userland builder also cross-compiles its `fbcheck` framebuffer-interface
helper and includes the stripped R5900 executable in the output filesystem.
Xorg's stock auto-start service is removed; X starts after login rather than
occupying memory before a shell is available.

Xorg still needs a native GS drawing driver, console switching and verified input. A
separate kernel candidate adds demand-paged framebuffer uploads and bounded GS commands.
Upload mapping is not an accelerated renderer. Classic `startx` detects UK evdev input
and refuses startup without `ps2_drv.so`. On 2026-10-04 the user confirms that the ten
existing native graphics runtime diagnostics work on real PS2 hardware, covering
primitives, uploads, owned surfaces/depth, texture/context panels, glyph/GC text and
solid drawing. [Physical diagnostic evidence](graphics-hardware-validation.json) records
this acceptance. These component diagnostics do not establish a full Xorg/TWM or
existing OpenGL application session. A new eleventh `ps2-gs-copy-test` uses native Xorg
CopyArea and shared Mesa region copies; twelve new host sanitizer and thirty R5900
checks pass, while its physical scene remains pending. The new copy image retains the
previously qualified diagnostic binaries and stable 576p kernel. The actual Gallium
resource copy callback now shares that GS adapter and passes seven new checks plus 39
shared Xorg/context regressions on both host and R5900. Its generic incremental patch
and isolated archives are recorded in [callback
evidence](graphics-resource-copy-validation.json). New callback runtime pixels,
shader/state execution, GLX and the full TWM session remain open; the previous
diagnostic image is preserved. EXA's three pixmap copy callbacks now share the Mesa
native batch as well; eight new host/R5900 cases and sixteen shared copy regressions
pass in each environment. Install this screen-owned adapter after the
context/texture/logic bridges, before EXA initialization, and restore it before the
outer context bridge. See [EXA copy evidence](graphics-exa-copy-validation.json). Its
runtime pixel and full DDX window-lifecycle tests remain open. Gallium `blit` now
supports native nearest scaling, source flips and destination scissoring for matching
RGBX/RGB555 resources. Opaque EXA `PictOpSrc` copies use the same adapter; Xorg
transforms/scaling are still pending. Eleven new host/R5900 cases and thirty regressions
pass in each environment. Install the EXA composite adapter after the context/texture
bridges, before EXA initialization, and restore its callbacks before the outer context
bridge. See [blit evidence](graphics-blit-validation.json). PCSX2 logs probe exit zero,
and supplied screenshots match its RGBX scene panels. Four new diagnostic lifecycle
cases pass in each environment. See [runtime
evidence](graphics-blit-runtime-validation.json). Physical/RGB555 runtime qualification,
the full DDX, Mesa shader/vertex execution, GLX and desktop RAM measurements remain
open. See [hardware graphics and its evidence](graphics.md).

## Memory and verification

The profiles use dynamic linking, size optimization, stripped binaries, no
generated locale archive and a small terminal/font set. C.UTF-8 remains available
when provided by GNU glibc. RAM use depends on active processes, framebuffer mode,
kernel memory and page cache; installed filesystem bytes are not resident RAM.
No unmeasured RAM or image-size claim is used as an acceptance result.

Measure booted free memory and each process's RSS/PSS before increasing services.
For the desktop, test one xterm and TWM first, then record memory pressure and
swap activity. Native GCC belongs on disk and can exceed the console's available
memory; use the cross compiler for routine development and enable swap for native
compiler experiments.

The preserved GCC14.4.0 baseline's three profile configurations have been resolved through Buildroot's actual
Kconfig on WSL2. The complete `base` profile has also built and installed all 38
concrete target packages and 33 host dependencies. Its 310 target ELF files pass
the R5900/o32/soft-float audit, and the production filesystem checker finds all
79 required command providers and their loaders/runtime libraries. Its GNU libc
tools and compiled `fbcheck` are included. The generated BusyBox applet list
contains exactly `udhcpc`; both compiled applets and installed links are audited.

The base Buildroot tar is 43,243,520 bytes, with 41,676,076 bytes in regular target
files counted once per hard link. These are storage measurements; runtime RAM
has not been measured. The project assembles its SysV service/configuration
overlay separately. `userland/validation.json` records this build and its SHA256.

This integration build retained its original input lock while applying recorded
corrections: bounding Buildroot helpers as well as Make to one job, retaining
SysVinit's `custom` internal local-source paths while compiling version 3.13,
and updating the pinned BusyBox 1.37 seed to explicitly disable its newly enabled
getfattr and shell applets. Only the affected packages/filesystem were rebuilt.
Reverting each recipe correction reproduced the previous external-tree hash.
Normal CLI builds continue to reject changed-input workspace reuse.

Eight smoke tests also passed with the checksum-pinned experimental
[R5900 Linux-user QEMU fork](https://github.com/frno7/qemu/tree/5f48e76da75409ac7aa23716c8a626d8f40539ef):
signed/unsigned multiplication and HI/LO guards, Bash arithmetic/builtins,
Coreutils printf/sort/SHA256, sed regular expressions, gawk soft-float arithmetic
and gzip decompression. The report pins the tested executables and staged
loader/library files at `emulator/userland-validation.json`. QEMU translates
syscalls to the workstation kernel and directly handles some instructions that
require kernel trap emulation on a PS2; those kernel paths require separate tests.
The fork's vector MMI and PS2 device emulation are incomplete.

The separate latest-GNU base also builds all 38 concrete target packages and 32
host dependencies with GCC 16.2.0, GNU binutils 2.47 and glibc 2.44, using the
verified 5.4.221 Linux UAPI headers. Its 310 regular target ELF files pass the
R5900/o32/soft-float and dynamic-library checks, and all 78 current console
command providers are present. The eight GNU Linux-user smoke tests pass with
this staging and its own pinned loader/library inventory. The raw Buildroot tar
is 43,171,840 bytes; the staged regular files, including six verified IOP IRX
files copied separately, total 42,465,295 bytes counted once per hard link.
These remain storage measurements, not runtime RAM measurements.

GCC's newer C default exposed three package compatibility issues. Bash 5.2.37
and GNU ed 1.20.2 use `-std=gnu17` only when the compiler is GCC 15 or newer.
Nano 8.2 has two pointer truth tests made explicit with `!= NULL`; exact patch
and before/after source hashes are checked, preserving ncurses' generated public
ABI. The reviewed incremental resumes retain the original input lock and verify
all previously completed package stamp identities. Normal CLI builds still
require a fresh workspace when inputs change. Three focused GNU Make tests
verify cross-environment/dialect behavior and rejection of unrecognized Nano
source. `userland/validation.json` preserves the older baseline and records the
new packages, every regular target ELF hash, recipe corrections and smoke tests.

The latest staging includes Linux-PAM 1.6.1, libxcrypt 4.4.38 and Shadow 4.16.0.
Its library closure passes; authentication must also be tested with the project
PAM policy, NSS files and private password in the booted kernel. Linux-user
emulation's loader prefix does not redirect general file accesses to guest
`/etc`. Filesystem overlay and the requested ext4 image are assembled separately.
The preserved GNU14 system's actual-kernel PAM test now rejects a wrong password
with code 7 and accepts the correct password, account and session with code 0,
after preserving permissions during native-Linux image assembly. Repeating
that result with the latest staging and an interactive shell remains necessary.

The current kernel uses built-in native SMAP and its ordered SIF/IOP bridge;
other Ethernet drivers and their modprobe requests are disabled. Updated DEV9
and IRQ-overlay firmware builds are separate from the six historical firmware
files copied into the base staging proof. The kernel/media assembly selects the
matching current firmware manifest. Native Ethernet packets and DNS are
user-confirmed on the SCPH-90001; an Xorg/TWM session remains unverified.

The latest desktop packages compiled with GCC16/glibc2.44 and passed target
ELF/library checks. Scoped GNU17 flags address libXt/TWM's C23 incompatibility,
and the bounded Xtrans hostname patch preserves compiler diagnostics. Desktop
helpers were cross-linked with the staging sysroot. The full profile remains
unbuilt. Native Xorg/Mesa drivers, a physical desktop session and measured RAM
remain separate acceptance steps.
The root `TODO.md` records those hardware and porting milestones.

Sources: [Buildroot releases](https://buildroot.org/download.html),
[Buildroot user manual](https://buildroot.org/downloads/manual/manual.html),
[official GNU GCC archives](https://gcc.gnu.org/pub/gcc/releases/),
[official SysVinit source](https://github.com/slicer69/sysvinit).

The native screen `CopyWindow` component uses the existing Mesa GS copy batch for window
moves, with full region clipping, overlap ordering, redirected pixmap origins and
callback/context ownership. It adds eleven sanitizer/R5900 checks to 41 shared
regressions per environment. Install after EXA initialization and before Damage setup,
then add native GC owners; unwrap later screen owners before cleanup. Its separate
runtime diagnostic now passes the PCSX2 command checks, with user-confirmed scene panels
and console return; physical pixels and full X server/TWM integration remain pending. It
is not installed as a working DDX or GLX driver. See [CopyWindow
evidence](graphics-copy-window-validation.json).

The stable 576p kernel is unchanged. Four new lifecycle checks pass on host/R5900, and
PCSX2 completes all four scene command checks with exit zero and text restoration. Run
`ps2-gs-window-test` after login to replay the overlapping, clipped and redirected
window moves. The user confirms all four scene panels and console return. Physical
pixels and RGB555 runtime use remain open. See [text-only runtime
evidence](graphics-copy-window-runtime-validation.json).

`xorg-ps2-paint-window.patch` follows `xorg-ps2-copy-window.patch` and adds native solid
background/border painting through the shared Mesa GS batch. Nested inherited
backgrounds retain child clipping; redirected backing pixmaps use native RGBX/RGB555
storage and exact Damage coordinates. Install after EXA, before later screen wrappers,
and initialize Damage before drawing. Eleven new and 27 related checks pass on
host/R5900. This source component does not enable the DDX; the later tiled component
extends painting, while new runtime pixels and Xorg/TWM/GLX integration remain open. See
[window-painting evidence](graphics-paint-window-validation.json).

Apply `mesa-ps2-tile.patch` after the Mesa blit patch and `xorg-ps2-tiled-paint.patch`
after the preserved Xorg solid-paint patch. The current `PaintWindow` accepts distinct
owned native tiles, inherited alignment and redirected RGBX/RGB555. Small patterns
expand on GS into a block of at most 64x32 logical pixels before output, without CPU
destination drawing. This explicit repeat adapter leaves Gallium sampler/shader
capabilities unchanged. Physical runtime pixels, actual X server/Damage use and full
DDX/Mesa/GLX integration remain pending. See [tiled-painting
evidence](graphics-tiled-paint-validation.json).

Only `ps2-gs-paint-test` runs at boot; all thirteen preceding diagnostic binaries remain
available. Its four scenes exercise solid/tiled backgrounds and borders, inherited tile
alignment and an owned backing pixmap presented beside a front-buffer reference. Five
lifecycle checks pass per environment. Run `ps2-gs-paint-test` after login to replay.
Supplied PCSX2 panels match scenes 2–4; the user confirms the solid scene and console
return. Hardware qualification and actual Xorg/Damage execution remain open. Packaging
writes a regular image file and does not flash a physical device. PCSX2 completes all
four scene command checks with exit zero and text-mode restoration; [text-only runtime
evidence](graphics-tiled-paint-runtime-validation.json) records the exact binary/image,
qualitative panel review and user confirmation of all four scenes and console return
without adding images.

Apply `xorg-ps2-tiled-gc.patch` after `xorg-ps2-tiled-paint.patch`, with the existing
Mesa tile and Boolean batch dependencies. It adds `ps2_fill.c/h` and a native GC fill
override for FillSpans/PolyFillRect, preserving solid, copy and glyph registrations
through revalidation. Owned RGBX/RGB555 tiles, GC pattern origins, composite clipping
and redirected backing offsets use the shared Mesa GS path; pixel tiles use native
Boolean drawing. Source-pixmap tile writes require GXcopy/full RGB masks, with no CPU
destination fallback. Fifteen new and 59 regression cases pass per environment, and the
generic Xorg series applies and compiles in both COMPOSITE modes.

Only this probe runs at boot; fourteen earlier binaries remain available. Its four
scenes compare rectangle/span checker fills, shifted clipping, a yellow/cyan
non-power-of-two pattern and front/backing output with a pixel-tile bar. Five host/R5900
lifecycle checks pass per environment. Replay with `ps2-gs-fill-test`. New physical
pixels, actual X server/Damage execution, full DDX/Mesa/GLX and TWM RAM measurements
remain pending. PCSX2 completes the four new command/fence scenes with exit zero and
text-mode restoration; [text-only fill runtime
evidence](graphics-tiled-gc-runtime-validation.json) identifies the image and saved run.
Four supplied PCSX2 panels match the expected scenes, and the user confirms all four and
console return. Verification is recorded as text only, with no repository images added.

Apply `mesa-ps2-stipple.patch` after `mesa-ps2-tile.patch` and
`xorg-ps2-stippled-gc.patch` after `xorg-ps2-tiled-gc.patch`. Opaque stippled
rectangle/span callbacks construct foreground/background colour patterns on GS from
existing server monochrome source bitmaps, then repeat through the native Mesa tile
adapter. GXcopy/full RGB masks, GC origins, clipping and redirected RGBX/RGB555 are
covered; no CPU colour-pixel expansion or destination drawing is used. Fourteen new and
74 regression checks pass per environment. Both generic series apply and compile in both
COMPOSITE modes.

Five host/R5900 lifecycle cases check four modeled scenes and cleanup. Replay with
`ps2-gs-stipple-test`. New physical pixels, depth-1 source creation/ drawing,
transparent stipples, actual X server/Damage and full DDX/Mesa/GLX/TWM RAM qualification
remain separate from this component milestone. PCSX2 completes the new four
command/fence scenes with exit zero and text-mode restoration; see [text-only runtime
evidence](graphics-stippled-gc-runtime-validation.json). All four supplied PCSX2 panels
match the expected scenes; the user confirms all four and console return. The
qualification is recorded as text only, without adding or linking images.

Apply `xorg-ps2-monochrome.patch` after `xorg-ps2-stippled-gc.patch`. It adds
source-only depth-1 EXA storage/access/destruction, stable native source views
while EXA hides pointers, and lower bitmap GC dispatch. It uses the existing
Mesa opaque-stipple adapter; colour destinations stay on GS. Ten new plus 88
regression checks pass per environment, and the generic patch compiles in both
COMPOSITE modes. Actual full server bitmap upload/drawing remains pending.
See [monochrome component evidence](graphics-monochrome-validation.json).

The new probe owns its monochrome sources through the driver callbacks and passes five
host/R5900 lifecycle checks. Run `ps2-gs-mono-test` after login to replay. PCSX2
command/fence and visual review are separate from physical qualification.

PCSX2 completes all four owned-source command/fence scenes with exit zero and
text-mode restoration. The four supplied panels match, and the user confirms
all four and console return; see [text-only runtime evidence](graphics-monochrome-runtime-validation.json).
Physical qualification and actual Xorg/fb client execution remain pending.

Apply `mesa-ps2-transparent-stipple.patch` after `mesa-ps2-stipple.patch` and
`xorg-ps2-transparent-stipple.patch` after `xorg-ps2-monochrome.patch` for native
transparent GC rectangle/span fills. One-bit runs use GS fills/Boolean
operations, preserving the destination at zero bits. All 16 ALUs and partial
RGB masks pass component checks; explicit run budgets bound work without CPU
colour fallback. Sixteen new plus 98 regression checks pass per environment.
See [transparent component evidence](graphics-transparent-validation.json).

Six host/R5900 lifecycle checks pass. Replay with `ps2-gs-transparent-test` after
console login. PCSX2 completion, visual review and physical qualification remain
distinct. Full Xorg/fb client execution, DDX/Mesa/GLX/TWM and desktop RAM measurements
remain pending.

PCSX2 completes all four transparent-stipple command/fence scenes with exit zero
and text-mode restoration at 70.664 guest seconds. See
[text-only runtime evidence](graphics-transparent-runtime-validation.json). All four supplied panels match the expected scenes, and the user confirms all
four and console return. This qualitative verification stays text-only;
physical/RGB555 pixels and full X server/GLX/TWM use remain unqualified.

Apply `mesa-ps2-opaque-stipple-logic.patch` after
`mesa-ps2-transparent-stipple.patch` and `xorg-ps2-opaque-stipple-logic.patch`
after `xorg-ps2-transparent-stipple.patch` to support native opaque stipple ALUs
and partial RGB masks. Each source run applies a foreground/background ALU
once, retaining overlap order and optimized full-mask copy. Sixteen new plus
114 regressions pass per environment; see
[opaque Boolean component evidence](graphics-opaque-stipple-logic-validation.json).

Six host/R5900 probe lifecycle checks pass. Replay `ps2-gs-opaque-test` after login.
PCSX2 visual and physical/RGB555 qualification remain separate; actual Xorg client/fb,
full DDX/Mesa/GLX/TWM and desktop RAM remain pending.

PCSX2 completes all four opaque Boolean command/fence scenes with exit zero
and text-mode restoration at 109.3742 guest seconds. See
[text-only runtime evidence](graphics-opaque-stipple-logic-runtime-validation.json). All four panel scenes match the user-provided images; text-console return awaits user confirmation;
physical/RGB555 pixels and full X server/GLX/TWM use remain unqualified.

Apply `mesa-ps2-thin-lines.patch` and `xorg-ps2-thin-lines.patch` after their
opaque-stipple-logic patches to extend zero-width solid segments/polylines to
all octants. Shared Mesa geometry preserves Xorg screen bias, clipped phase,
caps and XOR joins before native GS Boolean run submission. Twelve new plus
130 regressions pass per environment; see
[thin-line component evidence](graphics-thin-lines-validation.json).

Six host/R5900 lifecycle checks pass; replay `ps2-gs-line-test` from the console. It
remains a native diagnostic with minimal server bookkeeping. Full Xorg/GLX/TWM and
desktop RAM are unfinished.

PCSX2 completes all four thin-line command/fence scenes with exit zero and
text-mode restoration at 70.0678 guest seconds. See
[text-only runtime evidence](graphics-thin-lines-runtime-validation.json).
The exact binary/image/session and completion log are preserved. All four
supplied line panels match, and the user explicitly confirms console return.
No images are copied or linked in the documentation. Physical/RGB555 pixels,
actual server/Damage, performance and full desktop use remain separate.

Apply `mesa-ps2-thin-dashes.patch` and `xorg-ps2-thin-dashes.patch` after their
thin-lines patches to add zero-width on/off and double-dashed segments,
polylines and rectangles with FillSolid. Shared geometry preserves dash phase
through clipping and joins, then native GS coloured Boolean runs apply output.
Sixteen new plus 142 regressions pass per host/R5900 environment; see
[dashed-line evidence](graphics-thin-dashes-validation.json).

Six lifecycle checks pass per environment, with 39 additional host checks of earlier
probes. PCSX2 completes all four scenes with exit zero and text-mode restoration at
69.5018 guest seconds; replay `ps2-gs-dash-test` from the console. See [text-only
runtime evidence](graphics-thin-dashes-runtime-validation.json). All four supplied scene
panels match, and the user explicitly confirms console return. This remains a native
diagnostic with minimal server bookkeeping; full Xorg/GLX/TWM and desktop RAM are
unfinished.

Apply `mesa-ps2-native-cursor.patch` and `xorg-ps2-native-cursor.patch` after
the dashed-line patches to add GS save-under monochrome pointer rendering
and Xorg sprite/Damage/block/cleanup integration. Shared native front accesses
hide the cursor before reads or writes; no CPU colour framebuffer is added.
Sixteen new plus 158 regressions pass per host/R5900 environment; see
[cursor evidence](graphics-native-cursor-validation.json).

Six probe checks pass per environment and nine preceding probes pass 45 host sanitizer
checks. Replay `ps2-gs-cursor-test` from the console. Native sprite callbacks run with
minimal MI pointer/Damage scaffolding; real input/VT/Xorg, full GLX/TWM/OpenGL and
measured desktop RAM remain unfinished.

PCSX2 completes all four cursor scenes with exit zero and text-mode restoration
at 168.4982 guest seconds; see [text-only runtime evidence](graphics-native-cursor-runtime-validation.json).
All four supplied panels match qualitatively: the small white/black pixelated
arrow is intended, movement leaves no visible trail, the front copy excludes
cursor pixels and the final panels are clean. Explicit user console-return
confirmation is pending. Positions are scripted; USB mouse input and actual
X server/Damage/VT execution are not qualified by this diagnostic.

Apply `mesa-ps2-client-images.patch` and `xorg-ps2-client-images.patch` after
the native cursor patches to connect colour GC ZPixmap PutImage/SetSpans to
shared Mesa GS uploads and Boolean colour runs. Source snapshots precede the
queue fence, clipping preserves source offsets and span overlap order, and
front writes retain cursor removal. Sixteen new plus 174 regressions pass per
host/R5900 environment; see [client-image evidence](graphics-client-images-validation.json).

Six probe cases pass per environment and ten prior probes pass 51 host checks. Replay
`ps2-gs-image-test` from the console. Actual X server/Damage/input/VT, full
GLX/TWM/OpenGL, physical/RGB555 and total desktop RAM remain unfinished.

PCSX2 completes all four client-image command/fence scenes with exit zero and
text-mode restoration at 163.0047 guest seconds in a 32 MiB guest. Exact
binary/image/session hashes and the completion-log prefix are preserved in
[text-only runtime evidence](graphics-client-images-runtime-validation.json).
All four supplied panels match qualitatively: shaded source rows, shifted
colours around the red clip gap, white/blue XOR checkers with a green bar, and
clean matching front/backing bars with a white bar. The user confirms all four
scenes and normal text-console return.
This probe invokes native image callbacks with minimal MI/Damage bookkeeping;
actual X server/input/VT, physical/RGB555 pixels, GLX/TWM and total desktop RAM
remain separate qualifications.

Apply `mesa-ps2-planar-images.patch` and `xorg-ps2-planar-images.patch` after
the client-image patches to add native XYBitmap/XYPixmap PutImage. Foreground/
background bitmap colours and descending colour planes flow through Mesa GS
colour fills for full RGB-mask GXcopy and Boolean runs for other ALUs/masks,
with server bit order, row padding, every legal left pad and
existing clipping/cursor ordering. Sixteen new plus 190 regressions pass per
host/R5900; see [planar-image evidence](graphics-planar-images-validation.json).

Six lifecycle checks pass per environment, and eleven earlier probes pass 57 host
sanitizer cases. Replay `ps2-gs-planar-test` from the console. Actual
server/Damage/input/VT, physical/RGB555, full GLX/TWM/OpenGL and measured desktop RAM
remain separate requirements.

The [initial visual run](graphics-planar-images-initial-visual-failure.json)
failed its final backing panel. The corrected full-copy path completes PCSX2
with exit zero at 176.5307 guest seconds in 32 MiB; the user confirms all four
scenes and normal console return. See
[corrected runtime evidence](graphics-planar-images-runtime-validation.json).

Apply `mesa-ps2-filled-polygons.patch` and `xorg-ps2-filled-polygons.patch`
after planar-image patches to add native solid FillPolygon. Both fill rules
and coordinate modes use bounded Mesa geometry, GS full-copy fills or Boolean
runs, and existing front/backing/cursor ordering. Eighteen new plus 206
regressions pass per host/R5900; see
[polygon evidence](graphics-filled-polygons-validation.json).

Six lifecycle checks pass per environment. Replay the command from the console. Polygon
tile/stipple fills, actual server/Damage/input/VT, physical/RGB555, full GLX/TWM/OpenGL
and measured desktop RAM remain separate.

The polygon diagnostic exits zero at 229.5788 guest seconds in 32 MiB PCSX2.
All four supplied scenes pass qualitative review and the user confirms normal
text-console return; see
[runtime evidence](graphics-filled-polygons-runtime-validation.json).
All 63 earlier host probe checks also pass against the updated components;
see [legacy probe evidence](graphics-filled-polygons-legacy-probe-validation.json).

Apply `mesa-ps2-stippled-polygons.patch` and `xorg-ps2-stippled-polygons.patch` after
filled polygons for transparent/opaque monochrome polygon fills. All 242 component
checks and six diagnostic checks pass per host/R5900, plus 69 earlier host probe checks;
see [component evidence](graphics-stippled-polygons-validation.json) and [legacy
evidence](graphics-stippled-polygons-legacy-probe-validation.json). The new probe exits
zero at 174.9583 guest seconds in 32 MiB PCSX2; all four supplied scenes pass
qualitative review and the user confirms normal text-console return. See [runtime
evidence](graphics-stippled-polygons-runtime-validation.json). The next native graphics
diagnostic will test transformed triangles and depth occlusion; OpenGL applications
still require vertex/shader, draw_vbo and GLX integration.

### Transformed 3D diagnostic

Apply `mesa-ps2-transformed-triangles.patch` after `mesa-ps2-stippled-polygons.patch`.
The Xorg patch series remains unchanged. It runs only `ps2-gs-transform-test` at boot;
the same command replays it after login. Four scenes exercise scalar MVP/clipping, GS
Gouraud triangles, Z16 occlusion and Xorg EXA Copy presentation. This explicit geometry
API keeps shader caps at zero and leaves `draw_vbo`, GLX and a real Xorg desktop
unfinished.

The new probe exits zero at 209.9709 guest seconds in 32 MiB PCSX2. All four
still scenes pass qualitative review, with user-confirmed text-console return;
see [runtime evidence](graphics-transformed-triangles-runtime-validation.json).
Continuous animation is not part of this fixed-angle diagnostic.
