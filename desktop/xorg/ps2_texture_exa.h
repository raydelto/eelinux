/* SPDX-License-Identifier: MIT */
#ifndef PS2_TEXTURE_EXA_H
#define PS2_TEXTURE_EXA_H
#include "ps2_render_bridge.h"
#include "ps2_gs_texture.h"
#include "picturestr.h"
struct ps2_texture_exa {
    struct ps2_surface_exa *exa;
    struct ps2_render_client *render;
    uint32_t source, target;
};
/* Attach after the existing EXA/render bridge and before exaDriverInit.
 * PictOpSrc, native RGB layouts, no mask/repeat/transform, nearest filter.
 * This is a drawing component; the complete DDX/glyph path remains separate. */
Bool ps2_texture_exa_prepare(ScreenPtr, struct ps2_texture_exa *,
    struct ps2_surface_exa *, struct ps2_render_client *);
#endif
