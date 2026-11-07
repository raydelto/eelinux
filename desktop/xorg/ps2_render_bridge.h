/* SPDX-License-Identifier: MIT */
#ifndef PS2_EXA_RENDER_BRIDGE_H
#define PS2_EXA_RENDER_BRIDGE_H
#include "ps2_surface_exa.h"
#include "ps2_gs_render.h"
struct ps2_exa_render_bridge {
    struct ps2_surface_exa *exa;
    struct ps2_render_client *render;
    ExaDriverRec original;
};
/* Install after preparing EXA and before exaDriverInit. The eventual DDX
 * shares its session with in-process Mesa; flush queued 3D before 2D/storage
 * operations. This is not DRI or a complete screen/VT/glyph implementation. */
Bool ps2_exa_render_bridge_prepare(ScreenPtr, struct ps2_exa_render_bridge *,
                                   struct ps2_surface_exa *, struct ps2_render_client *);
#endif
