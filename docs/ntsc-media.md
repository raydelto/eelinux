# NTSC media (experimental)

The 480p kernel selects `video=ps2fb:720x480p@60`. The interlaced
recovery kernel selects `576x384i@60` and must be launched as `EENTSC.ELF`.
NTSC media keeps the selected kernel mode during startup (`MODE=boot` in
`/etc/default/ps2-video`), so the fallback is not switched back to 480p. Do not
change it to `MODE=480p` on NTSC media: that would force `EENTSC.ELF` back to
480p as well. PAL remains the default (`VIDEO_MODE=576p`). The Xorg PS2 driver
obtains its screen dimensions from the framebuffer; it does not use an Xorg
modeline.

## Building

1. USB kernels: `make usb-kernel VIDEO_MODE=480p` and
   `make usb-kernel VIDEO_MODE=ntsc`. They land in `$(OUTPUT)/usb-kernel-480p`
   and `$(OUTPUT)/usb-kernel-ntsc`.
2. DVD kernels, against the DVD source copy: `make dvd-kernel-pal VIDEO_MODE=480p`
   and `make dvd-kernel-pal VIDEO_MODE=ntsc`. The work and output directories
   default to `dvd-kernel-480p` and `dvd-kernel-ntsc`.
3. Media: `make ntsc-media` (the same as `make pal-media VIDEO_MODE=480p`). With
   `VIDEO_MODE=480p`, the defaults are:
   - primary USB kernel: `$(OUTPUT)/usb-kernel-480p`;
   - fallback kernels: `$(OUTPUT)/usb-kernel-ntsc` and `$(OUTPUT)/dvd-kernel-ntsc`
     (`MEDIA_FALLBACK_KERNEL_DIR`, `MEDIA_DVD_FALLBACK_KERNEL_DIR`);
   - work directory: `$(WORK)/ntsc-media`;
   - output: `build/images/ntsc-desktop`.

   PAL keeps `pal-media`/`pal-desktop`, so the two never share a report.
   Override any `MEDIA_*` variable to use other paths. Use the full userland
   staging tree as described in `reproducible-build.md`.

All four kernels must share the same `kernel_release` and module payload. The
packager refuses a fallback whose modules differ.

## Which image to try on an NTSC console

These are experimental, unqualified homebrew images. **USB via uLaunchELF is the
safer first boot path**: it doesn't depend on the DVD Player version.

FreeDVDBoot only works with specific DVD Player firmware. Before burning,
check the version on the browser's Version screen (press Triangle in the system
browser). On a dev/TEST unit (DTL-H), assume nothing: the DVD Player version is
unknown until checked.

| Console | DVD Player | Image |
|---|---|---|
| Slim, NTSC-J or NTSC-U | 3.10 / 3.11 | `eelinux-ntsc-slim.iso`. The console language must be set to English. |
| Fat, NTSC-J | 2.12J | `eelinux-ntsc-fat-2.10-2.12.iso`. 2.12 is documented upstream for U/J/G. |
| Fat, NTSC-U | 2.10 / 2.12 | `eelinux-ntsc-fat-2.10-2.12.iso`. Only some 2.10 models work. |
| Fat | 3.04 | `eelinux-ntsc-fat-3.04.iso`. **Never on NTSC-J**: upstream explicitly excludes J. 3.04U is untested. |
| Anything else | — | Use the USB image. |

`dvd-compatibility.json`, next to the ISOs, carries the same per-region notes.

## Video cable

`EELINUX.ELF` runs at 480p. It needs a component (YPbPr) cable, a D-terminal
cable (D2 or higher), or a PS2-to-HDMI adapter. **Composite, S-Video and RGB/SCART
users must boot `EENTSC.ELF`.**

## Hardware checklist (NTSC-J)

1. Record:
   - the exact console model (for example DTL-H…/SCPH-…);
   - the DVD Player version from the Version screen;
   - the cable/adapter and display;
   - the boot method (USB or DVD, and which ISO).
2. Launch the 480p `EELINUX.ELF`. Check:
   - the boot text is readable;
   - the display is stable;
   - the console edges aren't cut off.
3. Log in and run `startx`. Check:
   - the screen dimensions;
   - the pointer;
   - basic drawing.
4. Repeat steps 2 and 3 with `EENTSC.ELF` and compare. `cat /etc/default/ps2-video`
   should show `MODE=boot`, and the interlaced mode must survive startup.
5. Test PAL separately, on a PAL setup.

A successful build doesn't mean the image boots on hardware or in an emulator.
