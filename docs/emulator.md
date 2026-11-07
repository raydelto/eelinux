# Testing eeLinux with emulators

The R5900 GNU userspace smoke suite passes all eight checks. The project-patched
PCSX2 2.6.3 builds for Linux and Windows boot a virtual USB root through SysVinit
and pass the target PAM tests: wrong password rejected, `eelinux` accepted for

`root`, and account/session checks successful. See

[authentication evidence](authentication-validation.json) and
[Windows build evidence](pcsx2-windows-validation.json). These checks do not yet
establish a physical PS2 login or an Xorg desktop. The current native SMAP
Windows test acquires DHCP, resolves `gnu.org`, receives replies from `8.8.8.8`,
and reports zero RX/TX errors. See [network evidence](network-runtime-validation.json).

## Current Windows launcher

The default Linux session profile uses OpenGL hardware rendering (`Renderer=12`) with
`UserHacks=true` and `UserHacks_Disable_Safe_Features=true`. The latter disables
PCSX2's game-specific clear shortcuts: native RGB555 masked drawing previously
produced striped presentation even when GS VRAM readback was correct. No
software renderer was selected for those earlier graphics checks. The separate native cursor restore now uses two
GS textured triangles to repaint its saved background; this removes the stale
black rectangle seen with local-to-local cursor restoration. The user confirms
both fixes in the `ps2-xorg-gpu2d-r9` session. Partial-invalidation disabling was
tested separately and did not fix the rectangle, so it is not enabled by the
launcher. See [graphics evidence](graphics-gpu2d-runtime-validation.json).

The current project runtime distinguishes progressive 576-line output from
480-line output using the GS vertical timing register. Its 576p display bounds
and dividers now retain all 576 lines rather than applying the 480p limits.
The user reports improved emulator output. The actual selector and tables pass
a host sanitizer test; a controlled guest run records the 720x576 GS probe
passing and SMAP link up. See
[progressive output and graphics evidence](graphics-development-validation.json).

It explicitly selects `720x576p@50`; the older ambiguous `720x576@50` could select 576i.
The user confirms stable terminal output from boot on the real SCPH-90001. This image
does not yet contain an enabled native Xorg/Mesa desktop.

Your existing command selects the checksum-verified project Windows runtime:

```powershell
py -3 scripts/emulator.py launch --directory C:/path/to/output/emulator/pcsx2-console --host windows --pcsx2 "C:\Program Files\PCSX2\pcsx2-qt.exe"
```

The selected executable is `build/emulator/pcsx2-patched-windows/runtime/pcsx2-qt.exe`.
Its DLLs, source patches and build provenance are checked before launch. A missing
project runtime now produces a launch error instead of silently using installed
PCSX2. The installed application remains untouched. `--stock-pcsx2` explicitly
selects the requested installed program; stock 2.6.3 stalls at the first Linux
userspace memory fault.

The October 14 comparison uses CPU GS emulation (`--gs-renderer software`,
`Renderer=13`) at the user's request. The launcher saves this setting and retains
it when subsequent `launch` or `refresh` commands omit the option. This is the
emulator's host renderer; the guest PS2 driver still submits GS operations and
does not gain a software raster fallback. CPU GS emulation can affect speed, but
the observed stock-runtime stall at `Run /init as init process` happens before
Xorg or Mesa starts. Both the original October 9 desktop image and the October
14 candidate reproduce that stall with the installed executable. Their original
images are preserved; boot tests use disposable copies. See
[the image comparison](../build/graphics-working-comparison-20261014/comparison.json).

The rebuilt Windows MMU runtime gets both images past `/init`. Its USB mass
storage correction reports the last sector address correctly, removing the
observed reads beyond the image and their reset/retry delays. The current
`pcsx2-console` disposable session uses the working filesystem's original
desktop, swap and bounded temporary filesystems, with the current GS driver.
TWM decorations, xclock and an xterm root prompt have been visually verified.
The earlier black screen was the pixel diagnostic session, which deliberately
ran without a window manager. Its CPU GS run passed colour-to-bitmap CopyPlane
and continued filtered-mask checks before it was stopped to show the desktop;
the full 18,326-check native suite remains incomplete. See
[runtime and desktop evidence](../build/graphics-working-comparison-20261014/runtime-comparison.json).

The profile uses the saved GS renderer, null host audio, 32 MiB EE RAM and the EE interpreter.

The recompiler warnings are expected: the current Linux MMU changes require
the interpreter. Enabling that recompiler does not provide a validated Linux
speed improvement. New Windows sessions default to a 300-second timeout.
`--timeout 0` enables an unlimited session; the launcher retains the selected
limit on later launches and refreshes. The current desktop was launched with
an unlimited session and is left running.

To stop a session that does not respond to its window controls, use another
PowerShell terminal:

```powershell
py -3 scripts/emulator.py stop --directory C:/path/to/output/emulator/pcsx2-console
```

The stop command checks the recorded process identity and stops only that
session's emulator. A repeated launch is rejected while its verified process
is still running. The session keeps the exit/timeout result and boot log.

`pcsx2-console` now contains the kernel and disk from

This image uses FAT32 plus ext4,

GNU tools without BusyBox, dhcpcd 10.5.2, UK keys, and built-in native SMAP.

Login is `root` with password `eelinux`. The user confirmed authenticated shell,
correct keys and successful `ping gnu.org` in the emulator.

USB/storage/input boot drivers are built in. OHCI prepares its interrupt
relay before enabling DMA. R5900 CP0 accesses, SIF receive-page ownership and
aligned IRX loading are corrected. The user confirms physical USB/ext4 root
mounting with the relay-v7 root, followed by a fault in the GNU dynamic loader.
Relay-v8 restores the missing original clean-page writeback path, fixes folio
byte offsets and restores CP0 guards in the newer interrupt prologue.
Physical GNU init/login is now user-confirmed on the SCPH-90001.
The current candidate retains those fixes and uses the generic `ps2-smap`
IOP packet service. The exact ELF and matching USB image pass PCSX2 DHCP,
external ping and DNS. The user now confirms physical dynamic IPv4,
external ICMP and successful `ping gnu.org` on the SCPH-90001, with no
messages returned by `dmesg -l err`; see
[physical Ethernet evidence](hardware-network-validation.json). Other models,
sustained traffic remain to be qualified. See
[current validation](ps2-smap-validation.json).

Relay-v7 adds `ps2-video`, padded GS memory checks and RGB output for VGA.

Missing EDID keeps PAL. Actual emulator mode previews and restoration are
recorded in [video evidence](video-validation.json); see
[resolution commands and connection requirements](video-modes.md).

The private guest checks also verify the real vectors, 32 separate GNU dynamic
executions, 13 UK symbols, DHCP/rebind, DNS and external ping. Emulator results
do not establish physical display or network compatibility.

The Windows runtime preserves physical punctuation scan codes, preventing
PCSX2's US shifted-text mapping from corrupting the guest UK layout. Successful
SMAP descriptors now contain completion status rather than retained command
bits. Routine DEV9 register/FIFO traces use DEBUG; normal logs retain guest
boot output, link changes, warnings and errors. Add `--debug-log` to the launch
command when detailed register traces are needed. Linux Qt host input remains
unchanged; the UK kernel console map is common to both hosts and real hardware.

The Windows runtime handles the valid SMAP RX FIFO read pointer, ignores the
unsupported FORCERENEW capability (DHCP option 145), and returns a correctly
encoded DNS search list for option 119. These changes remove the reported
false warnings while retaining malformed-packet and unknown-register errors.

dhcpcd includes the MIPS multiword CPU-info fix and a default eeLinux hook, so
normal startup and a manual `dhcpcd` invocation use the same route/DNS setup.

## Earlier test procedure and preserved artifacts

The paths and ext2 experiments below document the earlier bring-up work. Use
the current runtime and status above when interpreting those saved results.

## 1. Run the GNU userspace smoke test

Use Linux or Debian WSL2, the built `base` GNU staging tree, and the project
Linux/glibc compiler. Install the host build dependencies listed in
[build instructions](build.md), plus `libglib2.0-dev`, `pkg-config` and

`zlib1g-dev`. Keep the emulator source/build on the native Linux filesystem.

```sh
make emulator-userland-build JOBS=2
make emulator-smoke
```

These commands use `~/.cache/eelinux/emulator-userland`,

`~/.cache/eelinux/rootfs-base`, and `~/.local/eelinux` by default. If those

GNU artifacts have not been built, first follow [userland.md](userland.md)
with `PROFILE=base`. This target deliberately requires existing GNU binaries
and never silently substitutes host commands.

In the current workspace, the completed builds use the `eelinux-linux`
directories. To repeat the already successful test from PowerShell:

```powershell
wsl --distribution Debian --cd "$PWD" --exec make emulator-smoke WORK=/home/machaddr/.cache/eelinux-linux TOOLCHAIN=/home/machaddr/.local/eelinux-linux
```

The ordinary Windows wrapper also exposes `emulator-userland-build` and

`emulator-smoke` for the default paths:

```powershell
.\build.ps1 emulator-userland-build -Jobs 2
.\build.ps1 emulator-smoke
```

The script downloads checksum-locked sources from [qemu-sources.json](../emulator/qemu-sources.json),
builds only `mipsel-linux-user`, verifies the compiled emulator identity/hash,
and runs with **`-cpu R5900`**. The pinned [frno7 fork](https://github.com/frno7/qemu/tree/5f48e76da75409ac7aa23716c8a626d8f40539ef)
is experimental QEMU 5.0.1, not current upstream QEMU. Generic MIPS CPU
substitution would give incorrect semantics for R5900 three-operand multiply;
the smoke suite checks both the destination GPR and HI/LO. See the pinned
[instruction decoder](https://github.com/frno7/qemu/blob/5f48e76da75409ac7aa23716c8a626d8f40539ef/target/mips/translate.c).

The eight checks cover a cross-compiled arithmetic probe, Bash builtins,
GNU Coreutils printf/sort/SHA256, sed regular expressions, gawk software
floating point and gzip decompression. Each program has a 15-second timeout,
disabled core dumps, and an exact expected result. The report includes source
and executable hashes, the staged loader/library inventory and compiler version.

The saved actual run is
[userland-validation.json](../emulator/userland-validation.json); subsequent
Make runs write `EMULATOR_WORK/test-report.json`.

This is Linux-user emulation: [QEMU translates system calls to the host kernel](https://www.qemu.org/docs/master/user/main.html).

The loader prefix is not a filesystem sandbox. The harness uses selected
ELFs and temporary data, without binfmt registration, chroot, guest PID 1,
services or child guest `exec`. It does not execute eeLinux's kernel, its
LL/SC exception handler or any PS2 device. The fork also lacks its packed
MMI/LQ/SQ instructions. These results establish useful GNU program behavior,
not complete PS2 CPU or operating system compatibility.

## 2. Prepare a PCSX2 boot/debug session

Use an installed PCSX2 and configure your console's BIOS through its normal
setup. The project does not distribute BIOS files; see the official
[BIOS setup guide](https://pcsx2.net/docs/setup/bios/).

PCSX2 2.6.3 is installed in this workspace. Its isolated configuration check
passed against the actual executable; a complete kernel/userspace boot has
not been established.

The launcher reads the installed executable's `-help` rather than assuming
newer CLI flags. The hardware/MMU review uses source revision

`81526d4dc7cc70e4ae75abb35a789417456c6d43`; the 2.6.3 compatibility path uses

the [release's actual CLI](https://github.com/PCSX2/pcsx2/blob/v2.6.3/pcsx2-qt/QtHost.cpp).

The current workspace has a corrected compressed **early-root audit** ELF and a
built 64 MiB base filesystem. Prepare a fresh session from PowerShell:

```powershell
py -3 scripts/emulator.py prepare --kernel C:/path/to/output/verification/kernel-root-pal-7.2.8.elf --rootfs C:/path/to/output/images/base/rootfs.ext2 --directory C:/path/to/output/emulator/pcsx2-run-new --host windows --audit-kernel
```

Use a new directory for each experiment. A prepared example already exists at

The script produces:

- A copied standalone MIPS/R5900 kernel ELF with validated executable entry,
  load/section bounds, physical/KSEG mapping and the 32 MiB RAM limit.

- `rootfs-usb.img`: a disposable MBR disk with Linux partition 1 at sector
  2048. It preserves every byte of the source ext2 image, whose hash is recorded.

- `pcsx2-eeLinux.ini`: 32 MiB EE RAM, EE/IOP interpreters, disabled speedhacks,
  null host audio, a visible guest log window, USB mass storage on port 1 and HID keyboard on port 2.

- `session.json`: input hashes, CLI arguments, and six unperformed boot milestones.

Historical console artifact sizes, hashes and preserved source payload are in
[pcsx2-console-validation.json](../emulator/pcsx2-console-validation.json).

The existing Windows `pcsx2-console` directory has been refreshed from the
old placeholder-initramfs ELF to the current PAL/root1 audit. For later rebuilds,
close PCSX2 and update this same directory with:

```powershell
py -3 scripts/emulator.py refresh --kernel C:/path/to/output/verification/kernel-root-pal-7.2.8.elf --rootfs C:/path/to/output/images/base/rootfs.ext2 --directory C:/path/to/output/emulator/pcsx2-console --host windows --audit-kernel
py -3 scripts/emulator.py launch --directory C:/path/to/output/emulator/pcsx2-console --host windows --pcsx2 "C:\Program Files\PCSX2\pcsx2-qt.exe"
```

`refresh` builds the replacement first, retains the entire previous session

in a named sibling backup, and carries forward the local BIOS selection/files.

The launch command uses the copied ELF in that directory. Its printed path
and SHA-256 show exactly which kernel was selected; it refuses a stale copy
if the recorded source ELF has changed. Refreshing selects the new kernel;
it does not add the project's MMU changes to a stock Windows executable.

The disk wrapper creates regular files only. It does not mount or format a
host disk. PCSX2 [opens its mass-storage image for writing](https://github.com/PCSX2/pcsx2/blob/81526d4dc7cc70e4ae75abb35a789417456c6d43/pcsx2/USB/usb-msd/usb-msd.cpp),
so always attach the disposable copy. The original `rootfs.ext2` remains a
filesystem image, not a whole-device installer image. `/dev/sda1` is the
guest root for this generated partition layout.

This is an emulated USB disk: **no physical USB stick is required**.

For future complete boot artifacts on Linux:

```sh
make emulator-prepare EMULATOR_KERNEL=/path/to/eelinux-7.2.8-ps2.elf EMULATOR_ROOTFS=/path/to/rootfs.ext2 EMULATOR_DIRECTORY=/path/to/output/emulator/pcsx2-run-01
```

To prepare a Windows preset from WSL, add `EMULATOR_HOST=windows`; the script
uses `wslpath` for all paths passed to PCSX2. Do not pass Windows-native paths
to a Linux PCSX2 executable or Linux paths to a Windows executable.

## 3. Launch and record where boot stops

Close any existing PCSX2 error dialog before starting this experiment.

For the fresh session prepared above:

```powershell
py -3 scripts/emulator.py launch --directory C:/path/to/output/emulator/pcsx2-run-new --host windows --pcsx2 "C:\Program Files\PCSX2\pcsx2-qt.exe"
```

On Linux, use the same command with `python3`, `--host linux`, and the path to
the installed `pcsx2-qt` executable. The launcher validates the kernel,
configuration and initial disk hashes before starting. After closing PCSX2,

`process-result.json` records its exit code. An emulator write changes the

disk hash; prepare a fresh session before rerunning.

To check the configuration without booting, add `--check-config` to the launch
command. It exits after PCSX2 loads the session profile and records

`config-check.json` and `pcsx2-config-check.log`, preserving the boot log.

This passed with exit code 0 on the installed 2.6.3 build.

Optional `--timeout 30` bounds an experimental boot attempt to 30 seconds.

**Compatibility fix:** PCSX2 2.6.3 supports neither `-gamecfg` nor `-datapath`.

The launcher automatically copies its executable, DLLs, Qt plugins and resources
to `SESSION/pcsx2-runtime`, enables `-portable`, and points `portable.txt` at

`SESSION/pcsx2-profile/PCSX2`. The generated preset is installed there as

`inis/PCSX2.ini`, with a valid settings version and memory cards disabled.

This preserves your installed PCSX2 files, personal settings and existing cards.

Newer executables use `-datapath` only when their help advertises it; a normal
Linux installation can instead use a session-local `XDG_CONFIG_HOME`.

Older prepared sessions are adapted automatically; no disk recopy is required
solely to fix the unsupported argument.

The launcher imports only your configured BIOS selection/files into the
isolated profile. If that selection is missing or points to a nonexistent file,
use the session's setup wizard or supply `--bios "C:\path\to\your\console.bin"`.

Use `--pcsx2-config "C:\path\to\inis\PCSX2.ini"` if your personal settings are
outside the usual Documents location. Local BIOS/NVM copies stay under ignored

The launcher uses supported `-elf` and `-batch` switches and checks
configuration-path capabilities before starting. `-nogui` does not provide a headless machine;
it suppresses the main window while rendering uses a separate display window.

Normal boots use filtered file logging at `pcsx2-profile/PCSX2/logs/emulog.txt`.

Add `--debug-log` only for a bounded diagnostic run: `-logfile` forces PCSX2's
debug log level and repeated TLB faults can produce nearly a gigabyte in two
minutes. Configuration checks still use their separate `-logfile`.

The debug profile uses `[SPU2/Output] Backend = Null`. This bypasses host audio
device initialization, including `cubeb_get_min_latency()` failures when no
default audio device is available. Muting Cubeb would still initialize it.

The launcher applies Null audio to old prepared sessions automatically, without
changing their checked kernel/disk/preset files. Close an existing session and
run the same launch command again. Recompiler/speedhack notices are expected
because this preset deliberately uses the interpreters; they are performance
notices, not startup failures.

**Blank-screen fix:** the old audit image had `CONFIG_FB_PS2=m` without a way
to load that module. It also omitted the R5900 decompressor CPU Status
initialization. A separate trace showed Status `0x10020c01`: the EE's EIE bit
was clear while Timer0 was running with a pending enabled interrupt. Linux
waited indefinitely in delay calibration, before probing its display driver.

The adapter now restores the [PS2 startup contract](https://github.com/frno7/linux/blob/77174dd4c8958a91aa5b76fb064a60eb68119e70/arch/mips/boot/compressed/head.S),
and the seed builds GS/ps2fb into the kernel. The generated kernel command
line retains both serial and screen consoles; modern builds also enable the

`/dev/fb0` interface. The final image contains no temporary tracing or

calibration shortcuts.

An actual installed 2.6.3 test of the older `kernel-console-7.2.8.elf` completed delay
calibration and displayed readable **72×28 Linux text at 576×460 PAL** in the
Vulkan window. That image ends at `Waiting for root device /dev/sda1...`. The
visible console and serial log were both checked; the evidence is in
[pcsx2-console-validation.json](../emulator/pcsx2-console-validation.json).

The earlier `pcsx2-validated` session preserves the old failing ELF: changing
PCSX2 settings alone cannot fix it. Prepare a new session
with the newer early-root ELF above.

Record each milestone in a separate result file with log excerpts/screenshots
and the emulator version/settings:

| Milestone | Required evidence |

| --- | --- |

| ELF loaded | PCSX2 reports this ELF and its actual entry point |

| Kernel started | eeLinux/Linux version and CPU/RAM detection from guest output |

| Early root started | Real `boot/early-root.c` helper, modules and IOP firmware loading |

| Disk root mounted | `/dev/sda1` ext2 mount and root handoff |

| GNU SysVinit started | Upstream SysVinit 3.13 executes the runlevel scripts |

| Authenticated shell | Provisioned account logs in and GNU commands run |

**The old console audit ELF had an empty initramfs.** Its `Waiting for root
device /dev/sda1` message could not progress because the USB drivers were modules
without a loader. `kernel-root-pal-7.2.8.elf` includes a static real `/init`,
an archive-only `/dev/console` device, dependency-ordered modules and firmware.

Its actual embedded archive is 1,454,592 bytes unpacked, below the 4 MiB limit.

The IOP IRQ driver now performs its sleeping firmware/RPC calls in process-context
resource callbacks, before the IRQ core disables interrupts.

On stock 2.6.3 this image reaches `Freeing unused kernel image` and `Run /init
as init process`, then repeatedly installs an invalid TLB entry for `0x004a6000`
with ASID 1. Source review identifies invalid-page exceptions incorrectly routed
to the refill vector, preventing Linux's demand-page handler from running.

This happens before the helper can print its module-loading messages.

The project also builds a pinned experimental native Linux PCSX2 2.6.3 with
[reviewed interpreter changes](../emulator/patches/README.md). It distinguishes
matching-invalid pages from refill misses, repairs TLB probe/Random behavior,
and raises user/supervisor address errors. The kernel restores the PS2 port's
RDHWR compatibility handler: glibc's thread-pointer instruction is decoded by
R5900 as an SQ store into a forbidden segment, then emulated by the kernel's
address-exception handler. Ordinary SQ alignment remains unchanged. See the
[original MIPS discussion](https://www.spinics.net/lists/mips/msg70031.html).

The actual paired test is recorded in
[root validation](../emulator/pcsx2-root-validation.json). It advances beyond
the former `0xffffe830` SIGSEGV but stops with `The futex facility returned an
unexpected error code.` and init SIGTRAP (`exitcode=0x00000005`). This is still
an incomplete boot. The preserved native executable and TLS kernel can be
tested from Debian WSL2 using a fresh Linux session:

```sh
python3 scripts/emulator.py prepare --kernel /path/to/output/verification/kernel-root-tls-7.2.8.elf --rootfs /path/to/output/images/base/rootfs.ext2 --directory /path/to/output/emulator/pcsx2-tls-repeat --host linux --audit-kernel
QT_QPA_PLATFORM=xcb python3 scripts/emulator.py launch --directory /path/to/output/emulator/pcsx2-tls-repeat --host linux --pcsx2 /home/machaddr/.cache/eelinux/pcsx2-native-2.6.3//path/to/output/bin/pcsx2-qt --bios /mnt/c/Users/sedzc/Documents/PCSX2/bios/SCPH-70004_BIOS_V12_PAL_200.BIN --timeout 120
```

These workspace paths refer to the built native emulator and your existing
local BIOS. For a rebuild, `scripts/emulator_pcsx2.py` checks the pinned source,
exact patch, Qt/dependency versions and output executable; prerequisites are in
[pcsx2-linux-build.json](../emulator/pcsx2-linux-build.json).

Keep qualified source checkouts and binaries preserved. Create a separate private
native Linux checkout at the pinned revision with the reviewed MMU patch applied,
then prepare the additional patches before building:

```sh
PRIVATE_CHECKOUT=/home/you/pcsx2-private
python3 scripts/emulator_pcsx2.py prepare-patches --source "$PRIVATE_CHECKOUT"
python3 scripts/emulator_pcsx2.py build --source "$PRIVATE_CHECKOUT" \
  --output /home/you/pcsx2-private-build \
  --qt /home/you/Qt/6.10.1/gcc_64 \
  --dependencies /home/you/pcsx2-dependencies --jobs 2
```

`prepare-patches` explicitly applies the locked USB `READ CAPACITY(10)` correction
to that private checkout: the response must contain the last sector address,
rather than the number of sectors. The read-only `build` command rejects a missing
correction or unrelated edits and verifies all 15 MMU files plus the USB file
before and after compilation. Reports record the separate patch and source
hashes; [builder checks](../build/emulator-usb-capacity/future-builder-validation.json)
also verify that the original MMU restrictions remain intact.

ASID, dirty-page/COW
and BIOS syscall handling still require further work and system tests.

Architecture rejects and decompressor cache/erratum review remain open. The built base disk also
has locked root login and no installed
Linux modules. Before a future shell test, build a complete kernel and an
image with an explicitly provisioned password hash using [runtime.md](runtime.md).

Read the `log_file` path in `session.json` and capture the screen/debugger state when progress stops.

Guest EE logging can expose [SIO output](https://github.com/PCSX2/pcsx2/blob/81526d4dc7cc70e4ae75abb35a789417456c6d43/pcsx2/HwWrite.cpp).

The corrected kernel command line includes

`console=ttyS0,115200 console=tty0`, so messages continue in both the SIO log

and the GS console. The launcher enables the
[PCSX2 log window](https://github.com/PCSX2/pcsx2/blob/v2.6.3/pcsx2-qt/LogWindow.cpp)
for old and new sessions. To follow the file from another PowerShell terminal:

```powershell
Get-Content C:/path/to/output/emulator/pcsx2-run-04/pcsx2-profile/PCSX2/logs/emulog.txt -Tail 40 -Wait
```

The log window is a read-only serial view, not a guest terminal.

`-gameargs` does not configure Linux's

command line in this port. Preserve the failing PC/EPC, CP0 Cause/BadVAddr,
last guest message and expected next milestone when debugging exceptions.

## Limits of PCSX2 testing

The EE interpreter is necessary to exercise guest TLB-miss exceptions in the
reviewed [vtlb implementation](https://github.com/PCSX2/pcsx2/blob/81526d4dc7cc70e4ae75abb35a789417456c6d43/pcsx2/vtlb.cpp).

It is not sufficient for full Linux correctness: source review finds incomplete
ASID/dirty-page/privilege handling and simplified refill behavior in
[COP0.cpp](https://github.com/PCSX2/pcsx2/blob/81526d4dc7cc70e4ae75abb35a789417456c6d43/pcsx2/COP0.cpp),
[Memory.cpp](https://github.com/PCSX2/pcsx2/blob/81526d4dc7cc70e4ae75abb35a789417456c6d43/pcsx2/Memory.cpp)
and [R5900.cpp](https://github.com/PCSX2/pcsx2/blob/81526d4dc7cc70e4ae75abb35a789417456c6d43/pcsx2/R5900.cpp).

We infer that stock PCSX2 is not qualified for eeLinux's paging, copy-on-write
and process-isolation acceptance. A boot banner or zero emulator exit status
does not establish those properties.

PCSX2 implements USB mass storage and HID input in its
[device registry](https://github.com/PCSX2/pcsx2/blob/81526d4dc7cc70e4ae75abb35a789417456c6d43/pcsx2/USB/deviceproxy.cpp).

It does not register the USB Ethernet adapters selected in eeLinux.

Its [DEV9 implementation](https://github.com/PCSX2/pcsx2/blob/81526d4dc7cc70e4ae75abb35a789417456c6d43/pcsx2/DEV9/DEV9.cpp)
can emulate native SMAP/ATA hardware, but the eeLinux native SMAP driver is
still missing. DEV9 networking is disabled in the preset until that driver
exists and receives a separate test plan.

The [Pcsx2-lnx MMU research](https://github.com/Florin9doi/Pcsx2-lnx/blob/main/tools/pcsx2/README.md)
is a useful future emulator-development reference for historical Linux 2.4.17.

Its patches and shell results do not establish compatibility with frno7's
platform port or Linux 7.2.8. Keep emulator patches versioned, reviewed and
distinct from real-console acceptance in [TODO.md](../TODO.md).
