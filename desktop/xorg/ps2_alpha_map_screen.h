/* SPDX-License-Identifier: MIT */
#ifndef PS2_ALPHA_MAP_SCREEN_H
#define PS2_ALPHA_MAP_SCREEN_H
#include "ps2_blit_exa.h"
struct ps2_alpha_map_screen {
    ScreenPtr screen;
    struct ps2_blit_exa *blit;
    CompositeProcPtr composite;
    GlyphsProcPtr glyphs;
    CloseScreenProcPtr close;
    Bool installed;
};
/* Install after EXA and Damage. Preserve Render's alpha-map clipping, then
 * dispatch to the native EXA compositor without its software-only gate. */
Bool ps2_alpha_map_screen_prepare(ScreenPtr,struct ps2_alpha_map_screen *,struct ps2_blit_exa *);
#endif
