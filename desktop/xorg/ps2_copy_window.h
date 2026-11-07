/* SPDX-License-Identifier: MIT */
#ifndef PS2_COPY_WINDOW_H
#define PS2_COPY_WINDOW_H
#include "ps2_surface_exa.h"
#include "ps2_context_link.h"

struct ps2_copy_window {
    ScreenPtr screen;
    struct ps2_surface_exa *exa;
    struct ps2_surface_client *gs;
    struct pipe_context *context;
    CopyWindowProcPtr CopyWindow;
    CloseScreenProcPtr CloseScreen;
    Bool installed;
};
/* Zero-initialize screen-owned state. Install after exaDriverInit, before
 * DamageSetup wraps CopyWindow. Damage owns notification around this hook;
 * the saved lower raster callback is restored, never used for drawing.
 * Later CopyWindow/CloseScreen wrappers must unwrap before cleanup, including
 * GC owners installed after Damage setup. One submission thread; the screen
 * retains a context lease until callbacks are restored. Not a complete DDX. */
Bool ps2_copy_window_prepare(ScreenPtr, struct ps2_copy_window *,
    struct ps2_surface_exa *, struct pipe_context *);
void ps2_copy_window_cleanup(struct ps2_copy_window *);
#endif
