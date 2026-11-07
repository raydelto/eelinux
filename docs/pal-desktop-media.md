# PAL desktop DVD and USB images

These FreeDVDBoot ISOs require a **DVD**, with **DVD-R recommended**. A CD cannot
boot this DVD-player exploit. Burn the ISO using the writer's disc-image mode,
finalise the disc, and set the PS2 system language to English. See the
[upstream burning instructions](https://github.com/CTurt/FreeDVDBoot#easy-setup-for-all-ps2-slim-consoles--bravia-tv).

The production desktop uses native GS pixel processing, TWM, core bitmap fonts, and
current X.Org applications from the existing `userland/package/x11r7` recipes. Sources
and SHA-256/SHA-512 hashes are locked in
[`userland/xorg-apps-sources.json`](../userland/xorg-apps-sources.json). The October 18,
2026 resolution includes 70 application/data releases, including xterm 412, xclock
1.2.1, twm 1.0.13, xcalc, xeyes, bitmap, xedit, xman, xbiff, oclock, xclipboard,
xfontsel, xmessage and the applicable command-line tools. libXmu 1.3.1, libXScrnSaver
1.2.5, libXv 1.0.13, libpng 1.6.59 and groff 1.24.2 supply runtime dependencies. The lock records excluded
obsolete and unrelated hardware-specific modules; extra graphics drivers, software
rendering and GLX are excluded. Version resolution uses the [official X.Org release
directory](https://xorg.freedesktop.org/archive/individual/app/) and [xterm's upstream
directory](https://invisible-island.net/archives/xterm/current/). The resulting programs
provide 68 XDG menu entries and 74 inventoried programs. Added tools include xgc,
xrandr, xvinfo, bdftopcf, fonttosfnt, xcursorgen, sessreg, x11perf and rendercheck.
Benchmarks and conversion utilities are available from the terminal. The
[r41 application record](desktop-applications-r41-validation.json) documents the
emulator smoke checks; the new r41 USB image is separate from earlier DVD artifacts. Application defaults are installed under
`/usr/share/X11/app-defaults`; the full calculator layout was visually confirmed after
correcting the cross-build resource directory. Its square root, pi and division buttons
use the available bitmap font. The [final desktop
check](desktop-applications-validation.json) records xclock, xterm, xcalc and xeyes on
the USB image, with 192 cursor moves and no changed pixels. The native Slim DVD booted
its ISO9660 root, GNU services, Xorg and clock; the user closed the emulator before the
remaining DVD desktop checks. The other packages are built and dependency-checked, but
have not all been exercised interactively.

The same **2,000,000,000-byte** MBR USB image works with either DVD family. It has
a 64 MiB FAT boot partition and an 1842 MiB ext4 root, including 128 MiB swap.
New USB images silently expand partition 2 and its mounted ext4 filesystem to
fill the USB drive on first boot. The source is
[`scripts/expand-usb-rootfs.sh`](../scripts/expand-usb-rootfs.sh); its installed
`/expand-usb-rootfs.sh` copy, boot hook and marker delete themselves only after
successful verification. The original FAT partition, root start sector, UUID
and files remain in place. A failed attempt keeps the script for the next boot.
The USB filesystem reserves online-growth metadata when it is formatted.
Automatic growth supports the image's 512-byte-sector DOS layout up to 2 TiB;
other layouts are left untouched.
DVD images contain neither this script nor its startup hook.
This is the total image size, rather than 2 GB of free space. Project tests,
diagnostic clients, qualification services and unused framebuffer drivers are
removed during packaging. Compiler headers and libraries are retained. Standard X.Org tools
such as xev and xdpyinfo remain user applications.
The supplied images use username `root` and password `eelinux`. Without an
explicit `ROOT_PASSWORD_HASH_FILE`, media packaging uses this requested default.
A custom hash overrides it. No plaintext password or hash is written to build reports.

The current DVD kernel clears inherited PHY loopback/isolate state before normal
PHY initialization and negotiation. The DVD loader can leave loopback enabled;
the physical report showed BMCR `0x7100`, and `mii-tool -r eth0` immediately
restored 100 Mb/s full-duplex and automatic DHCP. Healthy USB PHY state is left
untouched. The rebuilt DVD kernel retains the exact previous configuration.

The filesystem tool also opens mounted metadata read-only before issuing the
online resize ioctl through the filesystem mount. This permits first-boot growth
while retaining the kernel's protection against raw writes to mounted devices.

## Boot on a console

1. For USB boot, write the complete `eelinux-pal-desktop-2gb-usb.img` to a USB disk of at least
   2,000,000,000 bytes. This overwrites that disk; the project packaging command
   itself writes only a regular image file.
2. Burn the matching DVD ISO to a finalised DVD-R and set the console language
   to English. DVD boot does not require a USB disk.
3. The DVD starts uLaunchELF through FreeDVDBoot. Choose `cdfs:/EELINUX.ELF`.
   For USB boot through an existing loader, choose `mass:/EELINUX.ELF`.
4. Linux mounts its full system from the selected medium and displays the login console. Log in using
   the root password provisioned when the image was built, then run `startx`.
5. TWM starts the themed terminal and classic analogue xclock. Press F1 for the menu,
   select Applications, or use Alt+Enter for another terminal. Exiting TWM
   returns to the console. Applications are not launched automatically as tests.

The menu uses installed XDG desktop entries, including user overrides. The DVD
reuses a prepared system menu when application directories and locale are
unchanged, avoiding individual disc reads for each entry. User entries, changed
application directories, locale and XDG paths use the live parser.
Accessories, Applets and Tools include every installed menu entry;
long lists have continuation menus. Command-line applications run inside xterm.
The clock has a white face and black hands, uses core GS drawing, and updates
once a minute. Its TWM title bar and frame let you move, resize or minimize it.
The supplied literal X resources load with `xrdb -nocpp`, avoiding an unnecessary
GCC preprocessor load before the desktop appears. Personal `.Xresources` files
also load directly unless they contain preprocessor directives such as
`#include` or `#define`.

## DVD compatibility

These are homebrew FreeDVDBoot discs, not officially authenticated Sony discs.
No Tuna, modchip or installed memory-card exploit is required on compatible
firmware. Compatibility follows [FreeDVDBoot's upstream documentation](https://github.com/CTurt/FreeDVDBoot#phat-consoles):

| Image | Console / DVD Player requirement |
| --- | --- |
| `eelinux-pal-slim.iso` | All Slim models according to upstream; English language |
| `eelinux-pal-fat-2.10-2.12.iso` | Confirmed PAL SCPH-30004 R, 2.10E, ROMGEN 0160EC20011004; SCPH-39004 is unsupported. 2.12 is documented for U/J/G, with no confirmed E variant |
| `eelinux-pal-fat-3.04.iso` | Experimental PAL 3.04E candidate; upstream only tested M in an emulator and hypothesizes other regions except J; English language |

There is no universally supported Fat PAL disc. Check the console's DVD Player
version on its Version screen. Fat ISOs preserve the upstream exploit and UDF
sectors because their payload has a fixed sector offset. Only ISO9660 sector 16
is replaced to reference the appended complete filesystem. Slim retains unchanged VIDEO_TS files and
adds the Linux ELF in an ISO9660/UDF hybrid. The adjacent
`dvd-compatibility.json` records exact hashes and these limits.

The user confirmed USB desktop startup on real hardware with r27 and reported
pointer corruption. The r28 cursor change still needs confirmation on that
console. Physical FreeDVDBoot startup remains unqualified. The native Xorg emulator checks
boot the ELF directly and do not validate the DVD exploit or a console's laser.

## Build targets

Use a native Linux build directory, the project's R5900 GNU compiler, and an
already built desktop stage/SDK. Install `genisoimage`, libarchive, gettext and itstool in
addition to the host prerequisites in [build.md](build.md).

```sh
make xorg-apps-update                     # Explicitly resolve latest releases
make userland PROFILE=full
make xorg-apps STAGING=/path/to/full-userland XORG_SDK=/path/to/sdk
make native-xorg STAGING=/path/to/full-userland XORG_SDK=/path/to/sdk XORG_HOST=/path/to/host
make dvd-kernel-pal KERNEL_SOURCE=/path/to/native-GS-linux \
  MEDIA_KERNEL_BUILD=/path/to/native-GS-build \
  MEDIA_DVD_KERNEL_DIR=/path/to/DVD-kernel
make pal-media STAGING=/path/to/full-userland \
  MEDIA_KERNEL=/path/to/PAL/EELINUX.ELF \
  MEDIA_KERNEL_CONFIG=/path/to/PAL/kernel.config \
  MEDIA_KERNEL_REPORT=/path/to/PAL/kernel-report.json \
  MEDIA_DVD_KERNEL_DIR=/path/to/DVD-kernel \
  MEDIA_OUTPUT=/path/to/new-output-directory \
  ROOT_PASSWORD_HASH_FILE=/path/to/password.sha512
```

The kernel must use `root=/dev/sda2`, ext4, the PAL `720x576p@50` mode, and the
native GS patch series through `ps2-gs-batch.patch`. Kernel generation uses
`ROOT_DEVICE=/dev/sda2 VIDEO_MODE=576p`. `MEDIA_OUTPUT` must be fresh: existing
image files are preserved, and packaging fails if they would be overwritten.
The password hash remains external to the repository.

Separate targets are `usb-image-2gb`, `dvd-iso-slim-pal`, `dvd-iso-fat-pal`
(both firmware variants), and `dvd-isos-pal`. `pal-media` creates all four
artifacts. `build.ps1` accepts the same target names. These targets neither
run nor ship qualification tests. Each DVD stores the complete GNU/Xorg files
directly under `/eelinux/root`, with Rock Ridge preserving case, permissions
and symlinks. There is no embedded USB image, ext4 loop root, disk swapfile or
2 GB padding. DVD targets build directly from staging and need no USB artifact.
ISO9660, the native PS2 CDVD block driver and overlay are built into the optical
kernel. An 8 MiB RAM overlay holds changes until shutdown. DVD boot uses LZ4
zram with 16 MiB of logical swap and a 4 MiB limit on compressed page storage.
Packaging reads back every root file byte, permission, owner and symlink target.
Optical reads use eight-sector (16 KiB) transfers and a bounded cache in the EE
and IOP, reducing repeated drive commands and SIF calls. Heavy native builds
still need external writable storage and swap on this 32 MiB machine.

The full profile includes GCC/G++, binutils, make, m4, bison, flex, autoconf,
automake, libtool, pkgconf, GDB, Python, Perl, GNU shell and core tools, editors,
compression, disk utilities and networking tools. The installed program inventory
is `/usr/share/eelinux/installed-programs.json`. Packaging rejects a reduced
stage missing the native development tools or SDK.

From PowerShell, pass Linux paths through `MakeVariables`, for example:

```powershell
.\build.ps1 -Target pal-media -MakeVariables @(
  'STAGING=/path/to/desktop',
  'MEDIA_KERNEL=/path/to/PAL/EELINUX.ELF',
  'MEDIA_KERNEL_CONFIG=/path/to/PAL/kernel.config',
  'MEDIA_KERNEL_REPORT=/path/to/PAL/kernel-report.json',
  'MEDIA_OUTPUT=/path/to/new-output-directory',
  'ROOT_PASSWORD_HASH_FILE=/path/to/password.sha512'
)
```

## Drawing performance

Solid spans are submitted in batches of up to 64 metadata rectangles. The
kernel snapshots and validates the entire request before drawing, then joins
groups of eight existing GS packets into one bounded DMA transaction with a
single FINISH wait. Render and texture batches use the same mechanism. Native
RGB555/RGB888, all sixteen Boolean operations, plane masks, overlap ordering,
depth and alpha preservation keep their original semantics. Older kernels
retain the individually submitted path when the new capability is absent.

Cursor saves/restores use exact GS local-to-local VRAM transfers. Restoration
does not use STQ texture sampling, avoiding changes to saved texels during
repeated pointer movement. Only damage intersecting the cursor
region hides it; readback refreshes it before the server sends a large reply.
CPU geometry planning and read-only X11 reply serialization remain; CPU pixel
rendering is not used. Colour-source CopyPlane, general XRender alpha/masks/
transforms, and OpenGL/GLX remain unsupported, so full 2D coverage is unfinished.

Matched PCSX2 repaint samples (four repeats per shape) measured rectangles at
10.226 to 2.116 ms, arcs at 15.027 to 4.782 ms, and polygons at 24.347 to
12.963 ms. These are emulator samples, not physical hardware timing or an
overall desktop speed guarantee. [The performance report](desktop-performance-validation.json)
records both runs. [The native pixel qualification](graphics-gpu2d-batch-validation.json)
passes all 141 comparisons plus cursor, resizing, scrolling and shell typing.
