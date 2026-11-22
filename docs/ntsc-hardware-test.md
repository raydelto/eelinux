# NTSC hardware test checklist (Japanese DTL-H)

This is the test procedure for the first NTSC media set, on a Japanese NTSC
dev/TEST PlayStation 2 (DTL-H). It covers writing the media, booting and the
checks to run. See [NTSC media](ntsc-media.md) for how the images are built and
[boot-chain regions](boot-chain-regions.md) for the region audit.

Nothing here has run on real hardware yet. Every expected result below is
what the build is designed to do, not an observation. Record what actually
happens, including failures.

## 1. The media set

The 2026-11-22 set was built from `main` @ `4dd5d64` (PR 1, PR 2 and PR 3
merged). It has six images:

| File | Boot path | Kernel(s) |
|---|---|---|
| `eelinux-ntsc-desktop-2gb-usb.img` | USB stick, launched from uLaunchELF | `EELINUX.ELF` 480p, `EENTSC.ELF` 576x384i |
| `eelinux-ntsc-slim.iso` | FreeDVDBoot, DVD Player 3.10/3.11 | same pair, DVD build |
| `eelinux-ntsc-fat-2.10-2.12.iso` | FreeDVDBoot, DVD Player 2.10/2.12 | same pair, DVD build |
| `eelinux-ntsc-fat-3.04.iso` | FreeDVDBoot, DVD Player 3.04. **Not for NTSC-J.** | same pair, DVD build |
| `eelinux-ntsc-test-unit.iso` | DTL-H only: `SYSTEM.CNF` `BOOT2 = cdrom0:\EELINUX.ELF;1` | DVD build, starts 480p |
| `eelinux-ntsc-test-unit-interlaced.iso` | DTL-H only: `BOOT2 = cdrom0:\EENTSC.ELF;1` | DVD build, starts interlaced |

`SHA256SUMS` sits next to the images. Check it before writing anything:
`sha256sum -c SHA256SUMS` on Linux, or `Get-FileHash -Algorithm SHA256 <file>`
in PowerShell.

Video modes:

- `EELINUX.ELF` is `720x480p@60`. It needs component (YPbPr), D-terminal D2 or
  higher, or a PS2-to-HDMI adapter.
- `EENTSC.ELF` is `576x384i@60` interlaced. Use it on composite, S-Video or
  RGB, and as the fallback when 480p shows nothing.
- `/etc/default/ps2-video` is `MODE=boot`: the system keeps whichever mode the
  kernel started in.

Login: `root`, password `eelinux`. A password change is forced at first login.

## 2. Record before testing

Write these down first. They decide which image to try and they go into
PS2L-6.

1. Console model from the label (`DTL-H…`) and serial.
2. DVD Player version. In the system browser, press Triangle → Version. Write
   down every line (ROM, DVD Player, and so on).
3. Display chain: cable (composite, S-Video, component, D-terminal, RGB,
   PS2HDMI), any scaler, the display model and its input.
4. Launchers available on the unit: FreeMcBoot/OpenTuna memory card,
   uLaunchELF on a disc, nothing.
5. The USB stick (make, size) and any USB keyboard/mouse/hub (make, model).
6. Network adapter, if any (Fat expansion-bay network adapter, model).

## 3. Pick the boot path

Try in this order and stop at the first one that reaches a login prompt:

1. **USB via uLaunchELF** (`mass:/EELINUX.ELF`). This does not depend on the
   DVD Player version, so it is the safest first boot. It needs a way to start
   uLaunchELF on the DTL-H.
2. **FreeDVDBoot ISO** matching the DVD Player version from step 2.2:
   - 3.10/3.11 → `eelinux-ntsc-slim.iso`. The console language must be English.
   - 2.10/2.12 (Fat) → `eelinux-ntsc-fat-2.10-2.12.iso`.
   - 3.04 → do not use on NTSC-J; FreeDVDBoot excludes it.
   - any other version → no FreeDVDBoot image fits; use USB or the TEST-unit disc.
3. **TEST-unit disc** (`eelinux-ntsc-test-unit.iso`, then `-interlaced`). This
   is the experimental path when there is no launcher. It depends on the
   DTL-H booting an unlicensed burned DVD-R, which is unverified.

## 4. Write the media

**USB.** The image is exactly 2,000,000,000 bytes, so use a stick of 2 GB or
more. Writing it erases the whole stick.

- Windows: Rufus (DD image mode), balenaEtcher or Win32 Disk Imager. Select the
  whole device.
- Linux: `sudo dd if=eelinux-ntsc-desktop-2gb-usb.img of=/dev/sdX bs=4M
  conv=fsync status=progress`, where `/dev/sdX` is the whole stick, not a
  partition.

Partition 1 is FAT32 (`EELINUX.ELF`, `EENTSC.ELF`, `README.TXT`). Partition 2
is the ext4 root (`/dev/sda2`). On first boot the root silently grows to fill
the stick.

**DVD.** Burn the ISO as a disc image (ImgBurn "Write image file to disc", or
`growisofs -dvd-compat -Z /dev/sr0=<iso>`) to DVD-R, at a low speed (4x or
lower), and finalise the disc. A CD cannot hold these images.

## 5. Checklist

For each item, record pass, fail or not tested, and what you saw. Do it first
with `EELINUX.ELF` (480p) if the display chain supports 480p, then repeat
items 5.1–5.5 with `EENTSC.ELF`.

### 5.1 Boot to console

- [ ] uLaunchELF (or the OSD for the TEST-unit disc) starts the ELF without an
      immediate return to the browser.
- [ ] The kernel text appears. Note how long until the first text.
- [ ] Boot reaches the `login:` prompt. Note the total time.
- [ ] Login as `root`/`eelinux` works, and the forced password change works.
- [ ] `cat /sys/rom/rom0/version/*` (or the `rom0: Version` line in `dmesg`).
      Expected on a Japanese DTL-H: `Japan` and `DEX`. Record the exact text.
- [ ] `uname -a`; `cat /proc/cmdline`. The command line should contain
      `video=ps2fb:720x480p@60` for `EELINUX.ELF` and
      `video=ps2fb:576x384i@60` for `EENTSC.ELF`.

### 5.2 Video mode and timing

- [ ] The display shows a stable image: no rolling, no black screen, no
      repeated resync. Note the mode the display or adapter reports
      (for example `480p`, `720x480 60 Hz`).
- [ ] Colours look right (no green/purple tint, which would suggest a
      component/RGB mismatch).
- [ ] `ps2-video status` and `cat /etc/default/ps2-video` (expected
      `MODE=boot`).
- [ ] Interlaced (`EENTSC.ELF`) only: the mode is still interlaced after login,
      not switched to 480p. Note whether text flickers.
- [ ] Optional, only if stable: `ps2-video list`, then `ps2-video test 480p 15`
      from the interlaced kernel. The mode reverts after 15 seconds.

### 5.3 Overscan

- [ ] All four edges of the console are visible: the first column of the login
      prompt, the top line, the bottom line (run `seq 1 60` and check the last
      line), and the right edge (run a long `ls -l /usr/bin`).
- [ ] Note how many characters/lines are cut on each edge, if any, for each
      display chain tried (CRT, component TV, PS2HDMI).

### 5.4 Input and USB storage

- [ ] The USB keyboard types at the console. The default layout is US/ANSI;
      `ps2-keyboard --list` shows the presets and `ps2-keyboard --set <ID>`
      changes it. There is **no Japanese (JIS/106) preset** in this build, so
      on a Japanese keyboard expect symbol keys to be wrong. Letters, digits
      and Enter should work. Note which keys are wrong (especially `\`, `|`,
      `_`, `@`, `:` and `¥`) for a later JP keymap.
- [ ] USB mouse: the pointer moves in Xorg (5.6).
- [ ] USB storage (DVD boot only, with a second stick):
      `dmesg | tail`, then `mount /dev/sda1 /mnt && ls /mnt && umount /mnt`.
- [ ] Hot-plug of a keyboard after boot is detected (`dmesg | tail`).

### 5.5 Network (only with a network adapter)

- [ ] `ip a` shows `eth0`. If not: `dmesg -l err` and `eelinux-network diagnose`.
- [ ] `eth0` gets a DHCP address, then `ping -c 4 8.8.8.8` and
      `ping -c 4 gnu.org` succeed.
- [ ] Network on the DTL-H is untested. A failure here does not block the
      video work; record it.

### 5.6 Xorg 2D

- [ ] `startx` starts TWM with the analogue xclock.
- [ ] `xdpyinfo | grep dimensions` reports `720x480` for `EELINUX.ELF`, or the
      interlaced size for `EENTSC.ELF`.
- [ ] The pointer moves and clicks. F1 opens the Applications menu; Alt+Enter
      opens a terminal; windows move, resize, minimise and close.
- [ ] The whole desktop is on screen (window title bars at the top, the
      window list at the bottom right).
- [ ] Exit Xorg and confirm the console comes back in the same mode.

### 5.7 Stability

- [ ] Leave the system at the console for 10 minutes or more, then type: it
      still responds.
- [ ] Leave Xorg running for 10 minutes or more with xclock: the clock
      advances, and the display stays stable.
- [ ] Optional load: `find / -xdev -type f | wc -l` or a small native build.
- [ ] Clean shutdown with `poweroff`. For USB, boot a second time to check that
      the root filesystem comes back without errors.

## 6. What to capture on failure

- A photo of the screen at the point of failure, including any kernel panic
  text. Photograph several times if it scrolls.
- What the display or adapter reports (mode/resolution, "no signal",
  "unsupported").
- Whether the console's own browser still showed correctly on the same
  display chain (this separates a cable problem from a mode problem).
- If a shell is reachable, save the logs to the stick's FAT partition so they
  can be read on Windows:

  ```sh
  mkdir -p /mnt/fat && mount /dev/sda1 /mnt/fat
  dmesg > /mnt/fat/dmesg.txt
  cat /proc/cmdline /sys/rom/rom0/version/* > /mnt/fat/boot-info.txt 2>&1
  ps2-video status >> /mnt/fat/boot-info.txt 2>&1
  cp /var/log/Xorg.0.log /mnt/fat/ 2>/dev/null
  umount /mnt/fat && sync
  ```

  On the USB boot, `/dev/sda1` is the boot stick. On a DVD boot, use a second,
  FAT-formatted stick.
- The file name and SHA-256 of the image that was used.

## 7. Reporting

Post the results on PS2L-6: the record from section 2, each checklist item
with pass/fail/not tested, and the photos and logs. A failure on one path
(for example, the TEST-unit disc) does not invalidate the others.

## Rebuilding this media set

Kernels come from the PS2L-3 builds (`usb-kernel` and `dvd-kernel-pal` with
`VIDEO_MODE=480p` and `VIDEO_MODE=ntsc`; see [NTSC media](ntsc-media.md)).
Put them in one directory as `usb-kernel-480p`, `usb-kernel-ntsc`,
`dvd-kernel-480p` and `dvd-kernel-ntsc`, then run, under the packaging
runner from [reproducible-build.md](reproducible-build.md):

```sh
make ntsc-media OUTPUT=<kernels> STAGING=<rootfs-full> MEDIA_OUTPUT=<out>
make dvd-iso-test-unit VIDEO_MODE=480p OUTPUT=<kernels> STAGING=<rootfs-full> MEDIA_OUTPUT=<out>
```

Each step takes about a minute. The second one adds the two TEST-unit ISOs
to the same `SHA256SUMS` and `dvd-compatibility.json`.
