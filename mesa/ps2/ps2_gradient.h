/* SPDX-License-Identifier: MIT */
#ifndef PS2_GRADIENT_H
#define PS2_GRADIENT_H
#include "ps2_wide.h"
struct ps2_gradient_stop { int32_t x; uint16_t rgba[4]; };
struct ps2_gradient {
    unsigned kind,nstops,exclude_opaque; /* 0: image; 1/2/3: linear/radial/conical. */
    const struct ps2_gradient_stop *stops;
    int32_t points[4]; /* Signed 16.16 endpoints or circle centres. */
    int32_t radii[2],angle; /* Radial radii or conical angle in 16.16 degrees. */
};
int ps2_gradient_valid(const struct ps2_gradient *);
int ps2_gradient_opaque(const struct ps2_gradient *);
int ps2_gradient_extent_valid(const int32_t matrix[3][3],unsigned transformed,
    int x,int y,unsigned width,unsigned height);
/* Coordinate planning only. No stop colours or image pixels are evaluated. */
int ps2_gradient_plan_row(const struct ps2_gradient *,const int32_t matrix[3][3],
    unsigned transformed,unsigned repeat,int x,int y,unsigned width,unsigned height,unsigned row,int64_t *positions);
/* One procedural sample at GS origin. Float RGB/alpha and byte RGB/alpha
 * are alternative outputs. All interpolation and premultiplication use GS.
 * CPU coefficients depend only on the uniform stop interval and repeat period. */
int ps2_gradient_fetch(struct ps2_divide_workspace *,struct ps2_filter_workspace *,
    const struct ps2_gradient *,unsigned repeat,int64_t position,
    const uint32_t rgb[4],const uint32_t alpha[4],uint32_t rgb_byte,uint32_t alpha_byte,
    uint32_t palette,unsigned wide);
#endif
