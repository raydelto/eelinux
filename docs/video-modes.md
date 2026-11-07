# Console resolutions

The small `ps2-video` utility and built-in framebuffer driver use GNU/glibc,
without BusyBox or Python on the console. The project default is now **720×576
progressive at 50 Hz (`576p`)**. The user confirms `ps2-video test 576p 15`
eliminates terminal flicker on the real SCPH-90001 with the PS2-to-HDMI adapter.
The previously used PAL interlaced mode flickers on that setup.

Kernel configuration, Make, the kernel CLI and root filesystem startup use
the same default. `/etc/default/ps2-video` supplies `MODE=576p` and
`RGB_SOG=0`; SysVinit applies it before login. Set `MODE=pal` for interlaced
compatibility or `MODE=auto` for EDID selection. New builds can use
`VIDEO_MODE=pal` or `--video-mode pal` for compatibility mode.

Its boot argument explicitly uses `720x576p@50`. Omitting `p`, as the previous
`720x576@50` build did, allows Linux's mode parser to choose the earlier 576i entry with
the same dimensions and refresh. This reproduced the startup flicker; the user confirms
the explicit progressive candidate is stable from boot without a runtime reset.
`tests/test_video_boot.py` runs the unchanged Linux parser and PS2 mode table to compare
every boot profile with its intended runtime timing.

The configuration correction is exported in
`video/patches/ps2-progressive-boot.patch` (targets this project's configuration
files, not the Linux framebuffer timing implementation). The previous working PAL console
image is preserved. The user reports blank output for 1080i and 1080p on the
current display chain; those modes are not selected for this setup.

## Automatic selection

When explicitly selected, `ps2-video auto` reads valid EDID from `/sys/class/graphics/fb0/edid` or
`/sys/class/graphics/fb0/device/edid`, if a display bridge supplies it. It intersects
advertised monitor timings with the kernel's modes, the selected output family,
and the GS memory budget. It excludes experimental 1080p. Missing, truncated,
bad-checksum or malformed EDID preserves the current boot mode; this is the
normal result on the PS2 analog connector. The current driver has no DDC/EDID
provider. The tool cannot identify an arbitrary TV or the capabilities of a
PS2-to-HDMI converter without that information.

For an independently obtained monitor EDID file, explicit selection is:

```sh
ps2-video --edid /root/monitor.edid auto
```

That file must describe the connected display, and the converter must support
the selected signal. VGA/RGB selection also requires `--rgb-sog`.
The kernel build's `--video-mode auto` has a different purpose: it leaves the
initial video timing to the PS2 ROM's region; it does not detect a monitor.

## Preview before keeping a mode

After logging in as `root` with password `eelinux`:

```sh
ps2-video list
ps2-video status
ps2-video test 720p 15
ps2-video set 720p
```

`test` restores the previous resolution and depth after 15 seconds, even if the
display cannot show the new signal. SIGINT, SIGTERM and SIGHUP also restore it.
The allowed preview interval is 1–60 seconds. `set` keeps a mode for this boot;
it does not change the startup preference. `ps2-video set pal` returns to
interlaced PAL for the current session.

| Profile | Resolution / refresh | Output connection | Depth chosen |
| --- | --- | --- | --- |
| `pal` | 576×460i / 50 Hz, with borders | Composite/component/compatible adapter | 32 bpp |
| `ntsc` | 576×384i / 60 Hz, with borders | Composite/component/compatible adapter | 32 bpp |
| `640x480` | 640×480p / 60 Hz | RGB sync-on-green | 32 bpp |
| `800x600` | 800×600p / 60 Hz | RGB sync-on-green | 32 bpp |
| `1024x768` | 1024×768p / 60 Hz | RGB sync-on-green | 32 bpp |
| `1280x1024` | 1280×1024p / 60 Hz | RGB sync-on-green | 16 bpp |
| `480p`, `576p` | 720×480p / 60 Hz; 720×576p / 50 Hz | Component/compatible converter | 32 bpp |
| `720p`, `720p60` | 1280×720p / 50 or 60 Hz | Component/compatible converter | 32 bpp |
| `1080i`, `1080i60` | 1920×1080i / 50 or 60 fields/s | Component/compatible converter | 16 bpp |
| `1080p`, `1080p60` | 1920×1080p / 50 or 60 Hz | Experimental timing; preview only | 16 bpp |

For a real RGB sync-on-green connection:

```sh
ps2-video --rgb-sog test 800x600 15
```

The VGA modes switch the GS colour output to RGB. A component PS2-to-HDMI
adapter does not become an RGB adapter when this flag is used. Composite
connections are limited to standard TV timings. There is no promise that every
converter/display accepts 720p or 1080i. Preview-only experimental 1080p is:

```sh
ps2-video --experimental test 1080p 15
```

## Memory and verification

The GS has 4 MiB of separate local memory. The kernel now checks padded GS
block allocation for the proposed geometry and reserves palette/font space,
instead of checking the previous active screen. Virtual geometry is bounded
before arithmetic. The utility reserves 32 KiB for a 512-glyph 8×16 font plus
one palette block; 1920×1080 therefore uses 16 bpp. No second full-screen buffer
or main-RAM shadow framebuffer is added.

The automated host tests execute the allocation checks, EDID base/CTA parsing,
invalid input handling, cable filtering, timed restoration and signal
restoration with AddressSanitizer/UndefinedBehaviorSanitizer. Actual guest mode
switch evidence is recorded in [video-validation.json](video-validation.json).
The user confirms physical 576p acceptance and no terminal flicker on the
tested setup. Other displays and modes remain unqualified. A separate GS
candidate adds framebuffer upload mapping and native commands; Xorg/TWM
packages cross-build, but accelerated Xorg and Mesa drivers are unfinished.
See [graphics status](graphics.md) and [display evidence](hardware-display-validation.json).

Timing and memory references are the pinned
[PS2 framebuffer source](https://github.com/frno7/linux/blob/77174dd4c8958a91aa5b76fb064a60eb68119e70/drivers/video/fbdev/ps2fb.c),
[GS output registers](https://github.com/frno7/linux/blob/77174dd4c8958a91aa5b76fb064a60eb68119e70/arch/mips/include/asm/mach-ps2/gs-registers.h),
and Linux's [framebuffer sysfs interface](https://github.com/torvalds/linux/blob/master/drivers/video/fbdev/core/fbsysfs.c).
