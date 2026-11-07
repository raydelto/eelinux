/* SPDX-License-Identifier: MIT */
#ifndef PS2_NATIVE_MULTIPLY_H
#define PS2_NATIVE_MULTIPLY_H
#include "ps2_filter.h"
/* Exact rounded component-wise RGB product / 255. All input values and
 * arithmetic stay in owned GS VRAM. Operands and output are origin pages;
 * palette storage cannot alias an output. */
int ps2_native_multiply(struct ps2_filter_workspace *,uint32_t a,uint32_t b,
    uint32_t destination,uint32_t palette,unsigned width,unsigned height);
int ps2_native_product(struct ps2_filter_workspace *,uint32_t a,uint32_t b,
    uint32_t low,uint32_t high,uint32_t palette,unsigned width,unsigned height);
/* Rounded Exclusion RGB: retain a*(255-b)+b*(255-a) before dividing by
 * 255. Three distinct external 64x32 scratch pages must not alias operands,
 * destination or palette. Destination may alias either input. All pixels,
 * carries and the single final rounding stay on GS; destination alpha is preserved. */
int ps2_native_exclusion(struct ps2_filter_workspace *,uint32_t a,uint32_t b,
    uint32_t destination,const uint32_t work[3],uint32_t palette,unsigned width,unsigned height);
enum ps2_native_order_blend { PS2_NATIVE_DARKEN,PS2_NATIVE_LIGHTEN,PS2_NATIVE_DIFFERENCE };
/* Exact byte Darken/Lighten/Difference, including non-premultiplied inputs.
 * Four distinct external 64x32 CT32 work pages retain unrounded products.
 * The 17-bit sum, unsigned comparison, subtraction, clamp and normalization
 * execute on GS. Inputs remain read-only unless aliased by destination;
 * destination alpha and pixels outside the rectangle are preserved. */
int ps2_native_order_blend(struct ps2_filter_workspace *,uint32_t source,uint32_t destination_colour,
    uint32_t source_alpha,uint32_t destination_alpha,uint32_t destination,const uint32_t work[4],
    uint32_t palette,unsigned width,unsigned height,unsigned mode);
enum ps2_native_piecewise_blend { PS2_NATIVE_OVERLAY,PS2_NATIVE_HARDLIGHT };
/* Byte Overlay/HardLight with the pinned XRender integer pipeline's single
 * final rounding. Six distinct external 64x32 CT32 pages retain a signed
 * intermediate modulo 2^24, carries and constants; comparisons, branch
 * selection and products execute on GS. Output may alias any input. */
int ps2_native_piecewise_blend(struct ps2_filter_workspace *,uint32_t source,uint32_t destination_colour,
    uint32_t source_alpha,uint32_t destination_alpha,uint32_t destination,const uint32_t work[6],
    uint32_t palette,unsigned width,unsigned height,unsigned mode);
/* Coverage Saturate retains the unrounded source/mask product until the
 * final XRender 256-bin quantization. No sampled values reach the CPU. */
int ps2_native_saturate_alpha(struct ps2_filter_workspace *,uint32_t source,
    uint32_t mask,uint32_t destination,uint32_t low,uint32_t high,uint32_t extra,
    uint32_t factor,uint32_t palette,unsigned width);
#endif
