/* SPDX-License-Identifier: MIT */
#ifndef PS2_POLYGON_GC_H
#define PS2_POLYGON_GC_H
#include "ps2_gc.h"
struct ps2_polygon_screen {
    ScreenPtr screen;
    struct ps2_gc_screen *gc;
    struct pipe_context *context;
    CloseScreenProcPtr CloseScreen;
    Bool installed;
};
/* Solid and monochrome stippled FillPolygon, both fill rules/coordinate modes.
 * Stipples <=256x256 use drawable origin + patOrg, independent of miTranslate.
 * Transparent zero bits preserve destination; opaque zero bits use bgPixel. Prepare before
 * outer cursor hooks; cleanup before the native GC/context owner. Geometry is
 * snapshotted and bounded before fencing; no CPU destination access. */
Bool ps2_polygon_screen_prepare(ScreenPtr,struct ps2_polygon_screen *,struct ps2_gc_screen *);
void ps2_polygon_screen_cleanup(struct ps2_polygon_screen *);
#endif
