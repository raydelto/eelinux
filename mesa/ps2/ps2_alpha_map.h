/* SPDX-License-Identifier: MIT */
#ifndef PS2_PICTURE_ALPHA_MAP_H
#define PS2_PICTURE_ALPHA_MAP_H
#include <stdint.h>
/* A native or GS-decoded map. Its transform/repeat/own alpha map do not
 * participate in pixel fetches: XRender replaces alpha at the source texel,
 * then filters the complete picture. CPU work is coordinates only. */
struct ps2_picture_alpha_map {
    uint32_t surface;
    unsigned enabled, width, height, has_alpha, alpha_only, swap, rotate;
    unsigned alpha_bits; /* Original alpha precision; zero means A8. */
    unsigned rgb_bits[3]; /* Original colour precision; zero uses native width. */
    int32_t x, y;
};
#endif
