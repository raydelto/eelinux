/* SPDX-License-Identifier: MIT */
#ifndef PS2_XORG_IMAGE_GC_H
#define PS2_XORG_IMAGE_GC_H
#include "ps2_gc.h"
struct ps2_image_screen {
    ScreenPtr screen;
    struct ps2_gc_screen *gc;
    struct pipe_context *context;
    CloseScreenProcPtr CloseScreen;
    Bool installed;
};
/* Native ZPixmap/XYBitmap/XYPixmap PutImage and already-translated SetSpans,
 * RGBX/RGB555. XY planes arrive in server bit order with descending significance.
 * Install after native GC and before outer cursor/screen wrappers. Source
 * buffers are server-validated client input; their padded length is inferred
 * from request dimensions/widths. XY left pad follows server scanline padding;
 * depth-1 GC editing
 * keeps its existing source-only lower path. State outlives registration. */
Bool ps2_image_screen_prepare(ScreenPtr,struct ps2_image_screen *,struct ps2_gc_screen *);
void ps2_image_screen_cleanup(struct ps2_image_screen *);
#endif
