/* SPDX-License-Identifier: MIT */
#ifndef PS2_PIPE_COMPOSITE_H
#define PS2_PIPE_COMPOSITE_H
#include "ps2_copy_batch.h"
#include "ps2_alpha_map.h"
#include "ps2_unorm.h"
#include "ps2_gradient.h"
#include "ps2_packed.h"
#include <stddef.h>
enum ps2_picture_repeat { PS2_REPEAT_NONE, PS2_REPEAT_NORMAL, PS2_REPEAT_PAD, PS2_REPEAT_REFLECT };
struct ps2_format_alias;
enum ps2_picture_operation { PS2_COMPOSITE_SRC, PS2_COMPOSITE_OVER, PS2_COMPOSITE_CLEAR,
    PS2_COMPOSITE_DST, PS2_COMPOSITE_IN_REVERSE, PS2_COMPOSITE_OUT_REVERSE, PS2_COMPOSITE_ADD,
    PS2_COMPOSITE_OVER_REVERSE,PS2_COMPOSITE_IN,PS2_COMPOSITE_OUT,PS2_COMPOSITE_ATOP,
    PS2_COMPOSITE_ATOP_REVERSE,PS2_COMPOSITE_XOR,PS2_COMPOSITE_SATURATE,
    PS2_COMPOSITE_SCREEN,PS2_COMPOSITE_EXCLUSION,PS2_COMPOSITE_MULTIPLY,
    PS2_COMPOSITE_DARKEN,PS2_COMPOSITE_LIGHTEN,PS2_COMPOSITE_DIFFERENCE,PS2_COMPOSITE_OVERLAY,PS2_COMPOSITE_HARDLIGHT,
    PS2_COMPOSITE_DISJOINT_CLEAR,PS2_COMPOSITE_DISJOINT_SRC,PS2_COMPOSITE_DISJOINT_DST,
    PS2_COMPOSITE_CONJOINT_CLEAR,PS2_COMPOSITE_CONJOINT_SRC,PS2_COMPOSITE_CONJOINT_DST,
    PS2_COMPOSITE_DISJOINT_OVER_REVERSE,
    PS2_COMPOSITE_DISJOINT_OVER,PS2_COMPOSITE_DISJOINT_IN,PS2_COMPOSITE_DISJOINT_IN_REVERSE,
    PS2_COMPOSITE_DISJOINT_OUT,PS2_COMPOSITE_DISJOINT_OUT_REVERSE,PS2_COMPOSITE_DISJOINT_ATOP,
    PS2_COMPOSITE_DISJOINT_ATOP_REVERSE,PS2_COMPOSITE_DISJOINT_XOR,
    PS2_COMPOSITE_CONJOINT_OVER,PS2_COMPOSITE_CONJOINT_OVER_REVERSE,
    PS2_COMPOSITE_CONJOINT_IN,PS2_COMPOSITE_CONJOINT_IN_REVERSE,
    PS2_COMPOSITE_CONJOINT_OUT,PS2_COMPOSITE_CONJOINT_OUT_REVERSE,
    PS2_COMPOSITE_CONJOINT_ATOP,PS2_COMPOSITE_CONJOINT_ATOP_REVERSE,PS2_COMPOSITE_CONJOINT_XOR,
    PS2_COMPOSITE_COLOR_DODGE,PS2_COMPOSITE_COLOR_BURN,PS2_COMPOSITE_SOFTLIGHT,
    PS2_COMPOSITE_HSL_HUE,PS2_COMPOSITE_HSL_SATURATION,PS2_COMPOSITE_HSL_COLOR,PS2_COMPOSITE_HSL_LUMINOSITY };
struct ps2_picture_sample {
    int32_t matrix[3][3]; /* 16.16 source transform, evaluated at pixel centres. */
    unsigned transformed, repeat;
    unsigned has_alpha, alpha; /* Optional constant premultiplied source alpha. */
    unsigned bilinear; /* Exact seven-bit, four-tap XRender sampling. */
    unsigned convolution;
    const int32_t *filter_params; /* width, height, signed 16.16 coefficients. */
    unsigned filter_nparams;
    unsigned source_swap, destination_swap; /* Red/blue order in RGBX/RGB555. */
    unsigned source_argb,destination_argb; /* Varying high-byte alpha. */
    unsigned source_rotate,destination_rotate; /* BGRA/BGRX low-byte alpha/padding. */
    unsigned alpha_only_destination; /* GS-decoded A1/A8 target, including Saturate. */
    struct ps2_unorm_layout source_unorm,destination_unorm;
    /* Uniform Render source colour, RGBA16 before the narrow cached value.
     * Saturate uploads constant float planes; no image pixels reach CPU. */
    struct ps2_gradient gradient;
    unsigned source_packed10,destination_packed10; /* Raw CT32 ten-bit formats. */
    unsigned source_solid;
    uint16_t solid_rgba[4];
    struct ps2_picture_alpha_map source_alpha_map, destination_alpha_map;
    struct ps2_format_alias *destination_alias; /* Different packed views of one raw pixmap. */
};
struct ps2_picture_bitmap {
    const unsigned char *bits;
    size_t bytes, stride;
    unsigned width, height, msb_first;
    int32_t x, y;
    struct ps2_picture_sample sample;
    unsigned bits_per_pixel; /* 0/1: A1, 8: A8 coverage. */
    unsigned alpha_bits; /* 0: storage depth; 4: low-nibble coverage in byte storage. */
    unsigned has_surface, component_alpha;
    uint32_t surface; /* Native colour mask; sample carries its channel order. */
};
/* Basic Porter-Duff, Screen and Exclusion operations, constant source alpha,
 * nearest/bilinear affine/projective colour and A1/A8 coverage.
 * CPU computes geometry and uploads original client bytes. Sampling,
 * coverage filtering, alpha arithmetic and pixel writes execute on GS.
 * Missing Src samples clear; missing Over samples preserve destination RGB.
 * Alias/front sources are snapshotted in VRAM before drawing. */
int ps2_pipe_composite_rect(struct pipe_context *, uint32_t source, uint32_t destination,
    const struct ps2_pipe_copy_box *, const struct ps2_picture_sample *,
    const struct ps2_picture_bitmap *, unsigned over);
#endif
