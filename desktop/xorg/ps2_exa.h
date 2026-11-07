/* SPDX-License-Identifier: MIT */
#ifndef PS2_EXA_H
#define PS2_EXA_H
#include "xorg-server.h"
#include "scrnintstr.h"
#include "exa.h"
#include "ps2_gs_client.h"

/* Raster callbacks for the native DDX. Offscreen ownership, image uploads and
 * the screen/VT lifecycle must be implemented before enabling a desktop. */
struct ps2_exa {
    struct ps2_gs_client *gs;
    ExaDriverRec driver;
    uint32_t color;
};
Bool ps2_exa_prepare(ScreenPtr screen, struct ps2_exa *, struct ps2_gs_client *);
#endif
