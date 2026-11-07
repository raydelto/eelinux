/* SPDX-License-Identifier: MIT */
#ifndef PS2_PAINT_WINDOW_H
#define PS2_PAINT_WINDOW_H
#include "ps2_surface_exa.h"
#include "ps2_context_link.h"

#define PS2_PAINT_BATCH_BOXES 256U
struct ps2_paint_window {
    ScreenPtr screen;
    struct ps2_surface_exa *exa;
    struct pipe_context *context;
    PaintWindowProcPtr PaintWindow;
    CloseScreenProcPtr CloseScreen;
    Bool installed;
};
/* Zero-initialize screen-owned state. Install after EXA, before later screen
 * wrappers; remove those wrappers before cleanup. The retained context lease
 * outlives both callbacks. Regions arrive in screen coordinates. Backgrounds
 * are clipped to the child's clipList even when inherited from an ancestor;
 * borders use the backing pixmap bounds. Solid pixels and distinct owned
 * native tile pixmaps are supported; None and inhibited backgrounds are no-ops.
 * Tiles repeat from the resolved background ancestor origin, including borders.
 * Damage must be initialized before drawing. No scratch GC or CPU rasterizer.
 */
Bool ps2_paint_window_prepare(ScreenPtr, struct ps2_paint_window *,
    struct ps2_surface_exa *, struct pipe_context *);
void ps2_paint_window_cleanup(struct ps2_paint_window *);
#endif
