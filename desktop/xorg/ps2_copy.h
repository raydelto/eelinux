/* SPDX-License-Identifier: MIT */
#ifndef PS2_XORG_COPY_H
#define PS2_XORG_COPY_H
#include "ps2_gc.h"
struct ps2_copy_screen {
    struct ps2_gc_screen *gc;
    struct pipe_context *context;
    ScreenPtr screen;
    CloseScreenProcPtr CloseScreen;
    Bool installed;
};
/* Install after GC/solid preparation. Uses upstream miDoCopy for source and
 * destination clipping, SourceValidate and GraphicsExpose/NoExpose handling.
 * Native GXcopy/full RGB masks and GXnoop; no CPU pixel fallback. At most
 * 256 clipped boxes and 1,048,576 copied pixels per request. Cleanup before
 * solid/GC teardown; other CloseScreen wrappers must unwrap first. */
Bool ps2_copy_screen_prepare(ScreenPtr, struct ps2_copy_screen *, struct ps2_gc_screen *);
void ps2_copy_screen_cleanup(struct ps2_copy_screen *);
#endif
