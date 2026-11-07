/* SPDX-License-Identifier: MIT */
#ifndef PS2_LOGIC_EXA_H
#define PS2_LOGIC_EXA_H
#include "ps2_context_bridge.h"
#include "ps2_gs_logic.h"
struct ps2_logic_exa {
    struct ps2_surface_exa *exa;
    struct pipe_context *context;
    Bool (*PrepareSolid)(PixmapPtr, int, Pixel, Pixel);
    void (*Solid)(PixmapPtr, int, int, int, int);
    uint32_t color, plane_mask;
    unsigned alu;
    Bool attached;
};
/* Install after the context/texture EXA bridge, before exaDriverInit.
 * All constant-source solid ALUs and native RGB plane masks. Restore these
 * callbacks before cleaning the outer context bridge or releasing its lease.
 * Single submission thread. Lines, tiled fills and alpha remain DDX work. */
Bool ps2_logic_exa_prepare(ScreenPtr, struct ps2_logic_exa *, struct ps2_surface_exa *, struct pipe_context *);
void ps2_logic_exa_cleanup(ScreenPtr, struct ps2_logic_exa *);
#endif
