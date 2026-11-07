# PS2 video configuration patches

`ps2-progressive-boot.patch` targets this project's `configs/video-modes.json`
and `configs/kernel.config`. It explicitly adds the Linux framebuffer `p`
selector to progressive boot profiles, including 576p/50.

This fixes mode selection rather than changing the GS clock or scanout timing.
The unchanged Linux `fb_find_mode` parser selects the first refresh-matching
entry when scan type is unspecified; the PS2 table lists 576i before 576p.
The regression test executes that parser and table, reproduces the old
interlaced selection and checks the timing fields for every corrected profile.

The user confirms the new kernel is stable from boot on SCPH-90001 through
their PS2-to-HDMI setup. Older PAL and diagnostic artifacts are preserved.
