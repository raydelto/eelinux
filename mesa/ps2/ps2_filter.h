/* SPDX-License-Identifier: MIT */
#ifndef PS2_NATIVE_FILTER_H
#define PS2_NATIVE_FILTER_H
#include "ps2_rop.h"
#include "ps2_alpha_map.h"
/* Four exact seven-bit bilinear weights. Coordinates and weights are CPU
 * geometry; all sampled colour and multi-byte arithmetic stay in GS VRAM. */
struct ps2_filter_tap { uint16_t x, y, weight; };
enum ps2_filter_source_kind {
    PS2_FILTER_RGB, PS2_FILTER_CHANNEL, PS2_FILTER_A8, PS2_FILTER_A1,
    PS2_FILTER_CONSTANT
};
struct ps2_filter_workspace {
    struct ps2_surface_client *gs;
    uint32_t tap, term, sum[4], saturated, carry;
    unsigned bytes;
    unsigned source_rotate;
    unsigned source_kind, source_channel, source_width, source_height;
    unsigned source_constant, source_msb_first;
    unsigned source_alpha_bits;
    struct ps2_picture_alpha_map alpha_map;
};
int ps2_filter_reserve(struct ps2_filter_workspace *, struct ps2_surface_client *, unsigned bytes);
int ps2_filter_release(struct ps2_filter_workspace *);
/* Decode a texel or run into RGB byte planes without a CPU readback.
 * A8/A1 surfaces retain their original packed bytes in CT32 storage. */
int ps2_filter_fetch(struct ps2_filter_workspace *, uint32_t source,
    uint32_t destination, uint32_t palette, unsigned x, unsigned y,
    unsigned dx, unsigned width, int step);
/* Native CT32 output, origin rectangle <=64x32. Each input already contains
 * one tap per output pixel; UINT16_MAX coordinates represent transparency.
 * The output is floor(sum(expanded-channel * weight) / 16384), once. */
int ps2_filter_bilinear(struct ps2_filter_workspace *, uint32_t source,
    uint32_t destination, uint32_t palette, const struct ps2_filter_tap [4],
    unsigned width, int step);
/* Convolution follows the pinned pixman's signed 16.16, wrapping 32-bit
 * accumulation, half-unit rounding and signed saturation. */
int ps2_filter_convolution_begin(struct ps2_filter_workspace *);
int ps2_filter_convolution_tap(struct ps2_filter_workspace *, uint32_t source,
    uint32_t palette, unsigned x, unsigned y, int32_t coefficient);
int ps2_filter_convolution_end(struct ps2_filter_workspace *, uint32_t destination, uint32_t palette);
#endif
