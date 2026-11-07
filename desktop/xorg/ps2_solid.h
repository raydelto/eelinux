/* SPDX-License-Identifier: MIT */
#ifndef PS2_XORG_SOLID_H
#define PS2_XORG_SOLID_H
#include "ps2_gc.h"
struct ps2_solid_screen {
    struct ps2_gc_screen *gc;
    struct pipe_context *context;
    ScreenPtr screen;
    CloseScreenProcPtr CloseScreen;
    Bool installed;
};
/* Install after ps2_gc_screen_prepare, on the same submission thread.
 * Solid fills/points and zero-width solid lines in all octants use native GS
 * Boolean drawing. CPU geometry planning preserves Xorg zero-line bias;
 * solid line pixels use fgPixel independently of fillStyle. Thin on/off and
 * double-dash lines require FillSolid; normalized positive even dash lists
 * <=256 entries are snapshotted. Polyline/rectangle phase crosses joins;
 * independent segments/rectangles restart at dashOffset. Coloured geometry
 * runs are fully preflighted before fencing; no CPU colour pixel fallback.
 * At most 4096 inputs, 256 clip boxes and 16384 clipped coloured runs per request.
 * Cleanup before ps2_gc_screen_cleanup; CloseScreen enforces this ordering.
 * Other CloseScreen wrappers must unwrap first. State must outlive the screen.
 */
Bool ps2_solid_screen_prepare(ScreenPtr, struct ps2_solid_screen *, struct ps2_gc_screen *);
void ps2_solid_screen_cleanup(struct ps2_solid_screen *);
#endif
