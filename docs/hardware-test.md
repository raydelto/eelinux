# Test eeLinux from one USB stick with Tuna

## Current hardware findings

The user confirms relay-v8 boots to an authenticated root shell on the real
SCPH-90001. The photographs show GNU commands working. The earlier loader
panic is resolved in this retest; the bundled cache, folio and interrupt
corrections cannot be individually attributed from that observation.
Kernel/image hashes were not measured on the console.

Native Ethernet now works on the user's SCPH-90001 with the current IOP
backend. The photograph shows `eth0` up with carrier, dynamic address
`192.168.2.112/24`, and four replies from `8.8.8.8` with zero packet loss.
Average RTT was 29.618 ms. The console's kernel hash was not measured;
this is a user-confirmed retest following delivery of the current candidate.
The user also reports successful `ping gnu.org`, confirming hostname
resolution, and no messages returned by `dmesg -l err`.
See [physical Ethernet evidence](hardware-network-validation.json).

Earlier photographs identified TX FIFO reset timeout (`-145`) on the legacy
EE register path. The current driver moves SMAP hardware access to the IOP,
while Linux retains network and PHY policy. Other console models and
sustained traffic remain to be qualified.
The physical UK keyboard also has incorrect backslash/pipe input.

The v8 emulator passed USB/ext4, GNU dynamic init, UK map ioctl checks,
DHCP/DNS/ping and nine mode previews. Those results do not establish physical
network or keyboard acceptance. See [the physical observations](hardware-network-v8.json)
and [the userland investigation](hardware-userland-validation.json).

## Test the current kernel

Safely eject, launch Tuna/LaunchELF, then open `mass:/EELINUX.ELF`. The kernel banner
must say `ps2: native SMAP IOP transport`. The existing root can stay in place; its disk
build ID will continue to identify its earlier filesystem version. After login, run `ip
a`. If `eth0` is still absent, run `dmesg -l err`: the new kernel identifies DEV9
acquisition, FIFO/MAC reset or MDIO/PHY discovery failures. This command requires no
pipe or backslash.

The current `ps2-smap` driver defaults to IOP-owned MAC/FIFO/MDIO operations
over bounded SIF RPC. It avoids the legacy EE window used by the earlier
backend and preserves the working USB/cache/CP0/VM corrections. Host tests
and actual PCSX2 networking pass. Basic Ethernet is now user-confirmed on
the SCPH-90001 with `ip a` and an external ping. The user also confirms DNS
with `ping gnu.org`; repeat with `ping -c 4 gnu.org` when testing another setup.
The same path is intended for Fat consoles with an
official Ethernet adaptor; those and other Slim revisions still need testing.
See [current evidence](ps2-smap-validation.json) and
[generic patch exports](../kernel/patches/README.md).

Writing the whole image erases the target stick. Its first partition is FAT32 for
LaunchELF, and the second is ext4 GNU root on `/dev/sda2`. The new disk ID is `ps2-smap
PAL ext4 GNU glibc-2.44 smap`.

The full image also adds `eelinux-network diagnose`, which prints the
interface list and relevant kernel messages without requiring shell punctuation.
That helper is available after a full rootfs update; an ELF-only update can
use `dmesg -l err`. The signed UK map is unchanged, and the reported physical
backslash/pipe issue still requires keycode or character examples.

The image retains PAL, GNU tools without BusyBox, UK console keys, dhcpcd,
and native SMAP. USB core, OHCI/PS2 OHCI, SCSI disk, USB storage, HID and IOP
register support are built into the kernel. `/init` does not load these
Linux modules. The separate IOP still requires its embedded IRX firmware.
GNU dynamic SysVinit and glibc 2.44 are retained.

The development login is **root**, password **eelinux**. Authenticated root login is now user-confirmed on the SCPH-90001.

See [earlier boot investigation](hardware-boot-history.md) for preserved
checkpoint, IRQ relay, firmware, DMA and earlier crash observations.
The USB procedure below is historical and uses the older ext2 image.

## Earlier USB image procedure

The paths, ext2 filesystem and locked account described below belong to the
preserved earlier test image. The requested PAL video mode remains the default.

Use your existing Tuna setup on the SCPH-90001 to open wLaunchELF, then launch the
kernel from the same USB stick that holds the Linux root filesystem. This image forces
**PAL 50 Hz**, as requested; your TV/video adapter must accept that signal.

| File | Use |

| --- | --- |

| `eelinux-usb-pal.img` | Write this whole disk image to the new USB stick with Etcher. |

| `eelinux-usb-pal.img.json` | Partition layout, verified kernel/rootfs hashes and packaging checks. |

| `EENTSC.ELF` | Optional 60 Hz diagnostic ELF to copy onto an already flashed stick. |

| `EECACHE.ELF` | Optional PAL 50 Hz probe with an explicit decompressor cache handoff. |

This is an experimental hardware test. The ELF derives from the time32 audit
configuration with a real static early `/init`, USB/storage modules and six IOP firmware
modules. It includes the corrected OHCI interrupt wrapper and selects `/dev/sda2`
as root. Linux text, early userspace and USB/SCSI disk discovery have run in the
patched PCSX2 interpreter. A later emulator probe has also displayed GNU SysVinit
and a login prompt; services and hardware boot still require further work.

This earlier image did not establish physical eeLinux boot. The production kernel gate
remains closed with 64 unresolved port hunks. This disk contains the existing
GNU **base** root filesystem; it is not an Xorg/TWM desktop image.

The upstream PS2 port lists SCPH-90001 among tested models. That is evidence for
the upstream port, not acceptance of this eeLinux 7.2.8 audit kernel.

[Upstream hardware compatibility](https://github.com/frno7/linux/wiki/PlayStation-2-Linux-hardware-model-compatibility)

## 1. Write the new USB stick on Windows

**Flashing erases the selected USB stick, including its existing partitions.**
Keep the existing Tuna memory card unchanged.

1. Install [balenaEtcher](https://etcher.balena.io/) for Windows.

2. Insert the new USB stick. It must hold at least 135,266,304 bytes (129 MiB).

3. Open Etcher and choose **Flash from file**.

4. Choose **Select target**, and identify the new USB stick by its model and
   capacity. Check that it is the intended device.

5. Choose **Flash**, approve Windows elevation, and wait for writing and
   validation to finish.

6. After writing, Windows can browse the first FAT32 partition. It contains
   `EELINUX.ELF` and `README.TXT`. If Windows asks to format another partition,
   **cancel**: that is the Linux ext2 root filesystem. Eject the stick after
   Etcher finishes.

Etcher writes disk-image bytes directly and verifies the result; no preliminary
formatting is required. [Etcher documentation](https://etcher-docs.balena.io/) Write
`eelinux-usb-pal.img` as the image. Copying the `.img` file onto a FAT32 stick does not
create this layout.

The prepared disk has an MBR and two partitions:

| Partition | Filesystem | Start sector | Size | Use |

| --- | --- | --- | --- | --- |

| 1 | FAT32, type `0x0c` | 2048 | 64 MiB | wLaunchELF loads `EELINUX.ELF`. |

| 2 | ext2, type `0x83` | 133120 | 64 MiB | Linux mounts `/dev/sda2`. |

Unused capacity on a larger stick can remain unallocated for this first test.

Flash the new

`eelinux-usb-pal.img` for the one-stick procedure below.

## 2. Launch the kernel through Tuna/wLaunchELF

1. Connect the flashed stick as the **only USB storage device**. A USB keyboard
   can use the other USB port.

2. Start the PS2 through your existing Tuna setup, then open wLaunchELF/uLaunchELF.

3. Open its file browser, enter `mass:/`, and launch `EELINUX.ELF`.

No kernel transfer to the memory card and no Tuna reinstallation is needed.

Keep Tuna's current files, including its `BOOT.ELF`, intact.

wLaunchELF is the upstream PS2 Linux ELF loader. The compressed kernel must keep
its `.elf` extension so the launcher recognizes it; there is no separate kernel
installer or PC boot sector to run.

[Upstream boot instructions](https://github.com/frno7/linux/wiki/Installing-and-booting-PlayStation-2-Linux)

## 3. Observe the hardware test

The boot path is:

```text
Tuna -> wLaunchELF -> mass:/EELINUX.ELF -> Linux -> /dev/sda2
```

The US SCPH-90001 normally selects NTSC automatically. This specific ELF overrides
that default with `video=ps2fb:576x460i@50`, so its console uses PAL 50 Hz.

Initial video can take several seconds; the upstream loader guide advises
allowing approximately 10–20 seconds before expecting the first display.

[Upstream boot timing](https://github.com/frno7/linux/wiki/Installing-and-booting-PlayStation-2-Linux)
Record the last visible lines and whether the boot reaches each stage:

- Linux banner and a readable GS text console.

- Early `/init` module-loading messages and the USB OHCI controller.

- USB mass storage, a SCSI disk, and both `sda1` and `sda2` partitions.

- `eeLinux: mounted disk root /dev/sda2` and GNU SysVinit startup.

- A console login prompt, if the handoff succeeds.

The original prepared base image keeps the root account locked. It has no automatic root
shell or default password. Reaching a login prompt would prove progress, but
that image cannot authenticate a login until a password hash is provisioned
through the image builder. See [login provisioning](runtime.md#login-provisioning).

If the display stays blank, record the TV/cable and whether the ELF returns to
wLaunchELF. If boot stops at a `/dev/sda2` wait, record the preceding USB/SCSI
messages. Emulator testing has progressed through disk discovery but is still
investigating platform and service behavior; an emulator result does not establish how
this physical console will behave.

### Separate 60 Hz test for a blank HDMI display

The PAL image remains the requested default. A separate `EENTSC.ELF` uses

`video=ps2fb:576x384i@60` to test the video path on the US console. It keeps the

same `/dev/sda2` root filesystem, early `/init`, modules and IOP firmware.

This is a diagnostic choice; it does not establish that the blank display is
caused by PAL or that hardware boot succeeds.

1. Insert the already flashed stick into Windows and open its visible FAT32
   partition. Cancel any request to format the Linux partition.

2. Keep the original PAL file.

3. Eject the stick, connect it to the PS2, and launch `mass:/EENTSC.ELF` through
   Tuna/wLaunchELF. Allow at least 20 seconds for the initial display.

4. Record whether a Linux banner appears, the last visible line, and whether
   the TV reports a lost signal or continues showing black.

No reflash is needed for this diagnostic. Its SHA-256 is

`aa7c043834527a4476d883a2369da742a8ffad668e7d365c13548203780034f5`;

The current audits do not draw a pre-decompression GS boot marker, so an
uninterrupted black screen alone cannot identify the failing boot stage.

### Separate PAL cache-handoff probe

`EECACHE.ELF` keeps PAL 50 Hz and the same `/dev/sda2` Linux kernel, early archive,

modules and firmware. Its compressed entry explicitly writes back the R5900
data cache, then invalidates the instruction cache before jumping to Linux.

The cache routines are byte-identical to the pinned frno7 source. Compilation,
instruction ordering and RAM layout were checked; this is not a confirmed fix
for the physical console's blank screen.

Keep the earlier ELFs for comparison. No reflash is needed for this separate
probe. Its SHA-256 is

`f407a6048fb5c67baa80785cdd92672cf2f25f741ebe41d7de9aaf8eb6a431f4`;

Keep this first root stick devoted to testing. Use GNU shutdown only after a
successful login has been provisioned; otherwise the writable root filesystem
may need an offline filesystem check after an interrupted boot.

## Recreate the regular image file

The packager runs in Linux/WSL with `dosfstools` and `mtools`. It requires a new
output path, checks the actual root2/PAL kernel against its report/config, and
verifies FAT32 using `fsck.fat -n` and kernel file readback. It does not open or
flash any physical drive. From the project directory in WSL:

```sh
python3 scripts/hardware_usb.py \
  --rootfs /path/to/output/images/base/rootfs.ext2 \
  --kernel /path/to/output/verification/kernel-root-pal-sda2-7.2.8.elf \
  --kernel-config /path/to/output/verification/kernel-root-pal-sda2-7.2.8.config \
  --kernel-report /path/to/output/verification/kernel-root-pal-sda2-7.2.8.json \
  --output /path/to/output/images/hardware-pal/eelinux-usb-pal-new.img
```

The current packaged kernel SHA-256 is

`fe3132c7f0b696f4427a01d2548ee9e278a7b50066427bd8c4830d494fced69e`;

the prepared `eelinux-usb-pal.img` SHA-256 is

`0b2850348b30b5a3e6fcd1ab239548933560a7d3c4d9264534a419d1b22d879d`.

The kernel's build evidence is in

Repackaged FAT file timestamps

can change the whole-image hash; verify each new image against its own manifest.

No physical-drive write or
real-hardware acceptance is implied by these hashes.

The packager can include the checked diagnostics by adding complete triplets:

```sh
  --ntsc-kernel /path/to/output/verification/kernel-root-ntsc-sda2-7.2.8.elf \
  --ntsc-config /path/to/output/verification/kernel-root-ntsc-sda2-7.2.8.config \
  --ntsc-report /path/to/output/verification/kernel-root-ntsc-sda2-7.2.8.json \
  --cache-kernel /path/to/output/verification/kernel-root-cache-sda2-7.2.8.elf \
  --cache-config /path/to/output/verification/kernel-root-cache-sda2-7.2.8.config \
  --cache-report /path/to/output/verification/kernel-root-cache-sda2-7.2.8.json
```

Every ELF is checked against its own config/report and read back from FAT32.

If the main `--kernel` triplet selects the PAL cache probe, it becomes

`EELINUX.ELF`; include only the NTSC diagnostic to avoid duplicating the cache

file. Use a distinct output path for each update and preserve earlier images.

The packager does not provision credentials or alter the supplied GNU rootfs.
