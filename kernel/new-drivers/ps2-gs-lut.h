/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_PS2_GS_LUT_H
#define _UAPI_PS2_GS_LUT_H
#include "ps2-gs-surface.h"
#define PS2GS_HW_CHANNEL_LUT (1U << 12)
/* Apply an owned 256-entry CT32 CSM1 palette independently to RGB bytes.
 * Source and destination are separate 64x32 CT32 scratch surfaces; palette
 * is a separate 16x16 CT32 surface. No address or register is user supplied.
 * Coordinates cover the origin rectangle, at most one GS page. Alpha and
 * unselected RGB bits are preserved. The palette is uploaded using the
 * existing owned image interface. This is a GS lookup, never CPU rendering. */
struct ps2gs_channel_lut {
	__u32 abi, source, destination, palette;
	__u32 width, height, plane_mask, reserved;
};
#define PS2GS_CHANNEL_LUT _IOW('G', 0x7f, struct ps2gs_channel_lut)
#endif
