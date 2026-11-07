/* SPDX-License-Identifier: MIT */
#ifndef PS2_UNORM_LAYOUT_H
#define PS2_UNORM_LAYOUT_H
/* Original component precision, before expansion into CT32 GS canvases.
 * Zero means the default eight-bit representation. Colour-less pictures
 * also use eight-bit zero RGB lanes; their alpha retains its actual width. */
struct ps2_unorm_layout {
    unsigned rgb_bits[3],alpha_bits;
};
#endif
