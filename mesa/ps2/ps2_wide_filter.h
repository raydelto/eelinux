/* SPDX-License-Identifier: MIT */
#ifndef PS2_NATIVE_WIDE_FILTER_H
#define PS2_NATIVE_WIDE_FILTER_H
#include "ps2_wide.h"
/* Wide bilinear sampling into four binary32 byte planes (three RGB lanes).
 * Taps are TL, TR, BL, BR; both UINT16_MAX coordinates mean transparent.
 * Fractions retain all 16 bits. Original UNORM widths are 1..8. The supplied
 * filter descriptor selects RGB, a channel, packed coverage or a constant.
 * Coordinates/constant weights are CPU geometry; fetched samples, rounded
 * products and left-associated sums stay on GS. Output pages are 64x32;
 * this writes width pixels in their first row and preserves other pixels.
 * Alpha-channel fetches may replace alpha from a validated native/decoded
 * map before filtering, using its original UNORM precision. Source/map/output
 * aliases into scratch pages are rejected before submitting work. */
int ps2_native_bilinear_float(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    uint32_t source,const uint32_t out[4],const unsigned bits[3],uint32_t palette,
    const struct ps2_filter_tap taps[4],unsigned fraction_x,unsigned fraction_y,
    unsigned width,int step);
#endif
