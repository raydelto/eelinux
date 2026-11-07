# eeLinux disk runtime

The disk image contains GNU Bash and GNU command-line utilities, glibc, SysVinit,
Linux system utilities and an optional Xorg/TWM desktop. dhcpcd supplies DHCP;
BusyBox is disabled. The image assembler rejects a missing command, a BusyBox payload,
a foreign architecture, an incompatible MIPS ABI or a missing ELF interpreter
or shared-library dependency. This verifies installation completeness, not
successful execution on the R5900.

The six PS2 IOP firmware modules under `/lib/firmware/ps2` use the coprocessor's
IRX format. Their installed build manifest, hashes, MIPS1 header, entry point
and segment bounds are checked separately. This does not relax the GNU
userspace ABI checks or establish that the modules run on the IOP.

The current PS2 kernel port still needs hardware work. In particular, the
imported PS2 framebuffer cannot provide the mapped pixel buffer required by
Xorg's fbdev driver, and the GS DRM buffer/update routines remain incomplete.
The installed desktop configuration is therefore an integration target, not a
working or hardware-validated desktop. `startx-ps2` runs the compiled `fbcheck`
helper before launching X and explains a missing mapping rather than attempting
to run the server against that interface. Passing that check is only an
interface check; it does not validate GS drawing, mode changes or input.

## Building the image

Use the main build workflow to produce a target installation tree. Every target
program must use the Linux MIPS little-endian o32 toolchain, not the bare-metal
PS2 ELF ABI. The assembler can also accept an already prepared installation:

```sh
python3 scripts/rootfs.py --staging /path/to/output/staging --check-only
python3 scripts/rootfs.py --staging /path/to/output/staging --output-dir /path/to/output/images --size-mib 512
```

Run image creation on Linux or WSL with Python 3, `fakeroot`, `mke2fs`, and
`e2fsck` installed. `--check-only` is portable. The builder uses newly created
regular files, never a host disk or partition, and needs no real root access.
It produces `rootfs.ext4`, `rootfs.tar` and `SHA256SUMS` by default. The image has no
reserved root blocks, uses 1 KiB blocks and contains initial console device
nodes. An offline `e2fsck` must succeed before outputs are published. A fixed
filesystem UUID, timestamps and directory order make repeated builds from the
same staging tree reproducible with the same host tools.

The default filesystem is 512 MiB on storage, not in RAM. Adjust `--size-mib`
to fit the staged files and available storage. `--no-desktop` explicitly omits
the desktop dependency checks and launcher. It does not remove packages already
installed in the staging tree; build the console userland profile to save disk
space. Hostname and reproducible timestamp are configurable using `--hostname`
and `--epoch`.

Desktop staging must contain the installed fbdev, evdev and mouse Xorg modules,
X keyboard data, indexed fixed fonts, Xorg, xinit/startx, xauth, xrdb, xterm and
TWM. Compile `rootfs/usr/lib/eelinux/fbcheck.c` with the target Linux compiler and
install it as `/usr/lib/eelinux/fbcheck` before building the image. The C source
is not installed in the image. A regular user's legacy fbdev session also needs
Xorg's installed setuid wrapper and console access; `/etc/X11/Xwrapper.config`
restricts that wrapper to console users. The assembler preserves staged
executable modes; it does not manufacture a setuid Xorg installation.

## Login provisioning

Root and nobody are locked by default. There is no automatic login, root shell
on boot, SSH server or network login service. An unprovisioned image will reach
the login prompt but cannot authenticate. For a usable local root login,
generate a SHA-512 crypt hash in a private local file:

```sh
umask 077
openssl passwd -6 > /path/to/output/root-password.hash
python3 scripts/rootfs.py --staging /path/to/output/staging --output-dir /path/to/output/images \
  --root-password-hash-file /path/to/output/root-password.hash
```

`openssl` prompts for the password; the image builder accepts the hash, not a
plaintext password. Do not commit the hash file. After first login, use the
installed `useradd`/`passwd` tools to create a regular account with `/bin/bash`,
a home directory and any needed video group membership. `/etc/skel` provides
the modest TWM and xterm preferences. A failed essential mount enters
`sulogin`, which requires the configured root password, then reboots.

## Boot and the 32 MiB memory budget

The initramfs `boot/early-root.c` locates and mounts storage, loads its configured
storage modules, reclaims its RAM files and execs the real disk `/sbin/init`.
That PID 1 is the vendored SysVinit implementation in `userland/sysvinit`, built
as a normal target package. `/etc/init.d/rcS` accepts already mounted
`/proc`, `/sys` and `/dev` from that handoff, remounts root writable and creates
bounded temporary filesystems. Kernel devtmpfs and PTYs are required. There is
one text console getty and one DHCP client; X starts only after an explicit
`startx-ps2` command. There is no display manager or persistent logging daemon.

`/etc/inittab` defines the real SysV runlevel actions. `rcS` prepares the core
mounts and volatile login records, then `/etc/init.d/rc S` runs the ordered
early device/swap services. `/etc/init.d/rc LEVEL` executes stop links before
start links from `/etc/rc.d/rcLEVEL.d`, in numeric name order. Shared services
remain running when moving between equivalent multiuser levels. A service
failure is reported while later optional services still run.

| Runlevel | Behavior |
| --- | --- |
| S | Essential mounts, device setup and existing configured swap |
| 0 | Stop networking/devices, terminate applications, disable swap, unmount and power off |
| 1 | Stop networking and enter authenticated `sulogin` maintenance |
| 2, 3 | Text multiuser mode with Ethernet and one console login |
| 4, 5 | The same modest text services; X remains manual |
| 6 | Ordered shutdown followed by reboot |

Use `telinit 1` for maintenance and `telinit 2` or `telinit 3` from its shell
to return to multiuser mode. `runlevel` reports the previous/current level.
`shutdown -h now` and `shutdown -r now` request the normal SysV shutdown paths.
The source `rootfs/etc/rc.d/runlevels.json` defines the image's actual relative
service symlinks; the assembler creates them without needing symlink privileges
on the Windows checkout. Modify the target links or that file when adding
services. The shell service runner does not replace SysVinit's PID 1.

| Writable RAM filesystem | Limit | Purpose |
| --- | --- | --- |
| `/run` | 2 MiB | PID files, resolver, runtime state and logs |
| `/tmp` | 2 MiB | Temporary application files |
| `/dev/shm` | 1 MiB | POSIX shared memory |

These are maximum allocations, not memory reservations. Logs under `/var/log`
are volatile through `/run/log`; `/var/run` and `/var/lock` also point into
`/run`. GNU tools and libraries remain on storage and are paged normally.
Optional eudev is started only when its `S10udev` script exists in the staged
desktop profile. Xorg, fonts, xterm and TWM still need measured PS2 resident
memory and swap tests. A 32 MiB desktop is not yet demonstrated.

To enable already initialized swap, edit `/etc/default/swap` and set
`SWAP_DEVICE=/dev/your-partition` or `SWAP_DEVICE=/swapfile`. Boot calls `swapon`
on that existing object. It never creates, partitions or formats swap. File
swap must be a regular file, not a symlink. Changes to storage layout and swap
creation are separate manual operations.

`/dev` holds kernel-managed device metadata. Its underlying devtmpfs instance
is created by the kernel and does not inherit a hard size limit from an
initial userspace mount option in the 5.4 baseline; measure its metadata use
with the actual device set. The runtime's explicit temporary-data limits apply
to `/run`, `/tmp` and `/dev/shm`.
See the [Linux 5.4 devtmpfs implementation](https://raw.githubusercontent.com/torvalds/linux/v5.4/drivers/base/devtmpfs.c).

Shutdown terminates services, disables swap, syncs and unmounts filesystems
before calling the SysVinit halt/reboot command. There is no automatic disk-root
filesystem repair in `rcS`; perform an offline `e2fsck` after an unclean shutdown
or when storage problems are reported.

## Ethernet

`/usr/sbin/eelinux-network` implements `start`, `stop`, `restart` and `status`.
It parses `/etc/network.conf` as data and validates device names, IPv4 addresses,
prefix lengths, routes and DNS addresses before configuration. It does not
source or evaluate the file. Use one `KEY=value` per line, without quotes,
indentation, inline comments or shell expansions. The default is:

```ini
ENABLED=yes
DEVICE=eth0
MODE=dhcp
ADDRESS=
GATEWAY=
DNS=
```

dhcpcd starts in the background immediately, keeps retrying, and renews leases.
It owns address and route configuration; the Bash hook handles DNS on
`BOUND`, `RENEW`, `REBIND`, `REBOOT`, `STATIC` and `INFORM`, and clears DNS on
lease expiry, release or loss of carrier. It validates each advertised DNS
address and advertised search domain before atomically replacing `/run/resolv.conf`. `/etc/resolv.conf`
points to that runtime file. Logs are in `/run/log/dhcpcd-eth0.log`.
Stopping the service clears routes, addresses and DNS, and checks the actual
executable behind the client's PID before signalling it.
The kernel must enable `CONFIG_FILE_LOCKING=y`: dhcpcd's PID locking uses the
real flock/fcntl implementation. The locked `dhcpcd` account supplies privilege
separation. IPv6 and seccomp are disabled to match this kernel configuration.

For a fixed address, configure your actual LAN values:

```ini
ENABLED=yes
DEVICE=eth0
MODE=static
ADDRESS=192.168.1.20/24
GATEWAY=192.168.1.1
DNS=192.168.1.1 1.1.1.1
```

Run `eelinux-network restart` after changes. `ENABLED=no` prevents boot from
configuring Ethernet. The service manages one IPv4 interface; it does not
provide IPv6 address management, Wi-Fi, VLANs, forwarding or firewall rules.
The kernel may support additional features, but they need separate configuration.

The current kernel builds native SMAP into the image, with its matching DEV9/IRQ
firmware. Other Ethernet drivers are disabled, and boot does not call modprobe.
The patched Windows emulator passes DHCP, explicit rebind, DNS and external
ICMP tests; duplicate service start retains the existing client. Actual Slim,
Fat Network Adaptor and PCMCIA PHY/link testing, long lease renewal, cable
reinsertion and sustained disk/network traffic remain physical acceptance tasks.

## Desktop use

Attach a USB keyboard and mouse and identify the keyboard event node using
`/proc/bus/input/devices`. Set its actual `/dev/input/eventN` path in
`/etc/X11/xorg.conf`; the default `/dev/input/event0` is not an enumeration
guarantee. The mouse configuration uses `/dev/input/mice`. Configure the kernel
for the native RGB555 framebuffer layout (15 color bits in 16-bit storage).
Xorg uses `DefaultDepth 15`; the helper rejects RGB565 or an incompatible channel
layout and reports the actual channel offsets/lengths. No 640x480 mode is forced:
use the kernel's actual supported PAL/NTSC mode and timings. This file does not
invent GS timings.

After the framebuffer port supports mapping, run `startx-ps2` from the local
console. The launcher runs Xorg on `:0` with TCP listening disabled and standard
`startx`/`xauth` cookie authentication. TWM and one xterm start; no background
clock, desktop service or compositor is used. Alt with mouse button 1 moves a
window; Alt with button 3 resizes it. The root-window menu can open a terminal
or exit TWM. xterm saves only 100 lines. Logs live under `/run/log` and vanish
on reboot. Keep the session small until hardware memory measurements exist.

## Verification

```sh
python3 -m unittest discover -s tests -p test_runtime.py -v
```

The Linux tests exercise static routes/DNS, DHCP retries and renewal, invalid
leases, non-evaluated configuration, safe process termination, reproducible
archive/image assembly, setuid permissions, SysV runlevel order/transitions and
console device metadata. Image tests use explicitly
non-runnable tiny MIPS ELF fixtures to test the assembler. They are not evidence
of a PS2 kernel boot or working target applications. Windows skips tests that
need Linux permissions, Bash or ext2 tools. The framebuffer helper can be
compiled on the host for warnings and failure-path checks; that also does not
validate PS2 video hardware.
