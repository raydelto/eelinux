/* SPDX-License-Identifier: MIT */
#ifndef PS2_GALLIUM_DRAW_H
#define PS2_GALLIUM_DRAW_H
#include "draw/draw_pipe.h"
#include "ps2_gs_client.h"

/* Final primitive stage: Mesa performs vertex transformation and clipping;
 * the GS performs rasterization. No software fragment renderer is installed.
 * The eventual pipe_context must reject unsupported fragment/depth state
 * before drawing. This component alone is not an OpenGL driver.
 */
struct draw_stage *ps2_draw_stage_create(struct draw_context *, struct ps2_gs_client *);
int ps2_draw_stage_outputs(struct draw_stage *, unsigned output_count,
                           unsigned position_slot, unsigned color_slot, int depth_test);
#endif
