/* SPDX-License-Identifier: MIT */
#ifndef PS2_XORG_GEOMETRY_H
#define PS2_XORG_GEOMETRY_H
#include "ps2_gc.h"
struct ps2_geometry_screen {
    ScreenPtr screen;
    struct ps2_gc_screen *gc;
    CloseScreenProcPtr CloseScreen;
    Bool installed;
};
/* Pinned Xorg MI computes source geometry/1bpp coverage, while the existing
 * native FillSpans/PolyFillRect/PolyPoint callbacks draw all colour pixels.
 * Install after native fill/polygon/image owners, before pointer setup.
 * Requests are bounded to 1024 primitives, 2048 geometry extent/line width,
 * and 4194304 estimated primitive scanlines; no CPU colour destination map. */
Bool ps2_geometry_screen_prepare(ScreenPtr,struct ps2_geometry_screen *,struct ps2_gc_screen *);
void ps2_geometry_screen_cleanup(struct ps2_geometry_screen *);
#endif
