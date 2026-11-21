# NTSC media (experimental)

The 480p kernel selects `video=ps2fb:720x480p@60`. The interlaced
recovery kernel selects `576x384i@60` and must be launched as `EENTSC.ELF`.
PAL remains the default (`VIDEO_MODE=576p`). The Xorg PS2 driver obtains its
screen dimensions from the framebuffer; it does not use an Xorg modeline.

Build the USB kernels with `make usb-kernel VIDEO_MODE=480p` and
`make usb-kernel VIDEO_MODE=ntsc`. Build DVD kernels against the DVD source
copy using `make dvd-kernel-pal VIDEO_MODE=480p` and again with
`VIDEO_MODE=ntsc`; use separate `MEDIA_DVD_WORK` and `MEDIA_DVD_KERNEL_DIR`
directories for each. For packaging with `VIDEO_MODE=480p`, provide the 480p
kernel triplets using the existing `MEDIA_KERNEL*` and `MEDIA_DVD_KERNEL_DIR`
variables. The fallback triplets default to `$(OUTPUT)/usb-kernel-ntsc` and
`$(OUTPUT)/dvd-kernel-ntsc`. Use a fresh media output directory and the full
userland staging tree as described in `reproducible-build.md`.

These are experimental, unqualified homebrew images. FreeDVDBoot support is
firmware-specific; in particular the included Fat 3.04 variant explicitly
excludes Japanese DVD players. Confirm the actual console and DVD player
version against the compatibility manifest before burning. The console's
video cable and display must support 480p; otherwise try the interlaced ELF.

On the NTSC-J console, record the cable, display and boot method; launch the
480p ELF and check boot text, display stability and console edges. Run `startx`
and check screen dimensions, pointer and basic drawing. Repeat using the
interlaced ELF and compare. Test PAL on a PAL setup separately. Neither
hardware nor emulator boot is implied by a successful build.
