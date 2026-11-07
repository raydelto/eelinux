/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_PS2_GS_LOGIC_H
#define _UAPI_PS2_GS_LOGIC_H
#include "ps2-gs-texture.h"

#define PS2GS_HW_SOLID_LOGIC (1U << 8)
/* Constant-source Boolean raster operations, numbered as X11 GXclear..GXset.
 * color/plane_mask are native RGB555 (CT16) or RGB888 (CT32) pixel bits.
 * Alpha is always preserved. The kernel uses at most two masked GS sprites:
 * constant bits are copied; inverted bits use (255 - destination RGB).
 * No readback, CPU destination pixels, user VRAM addresses or shaders.
 * Ownership, KD_GRAPHICS, epoch invalidation and sticky failure rules match
 * the surface interface. Each request is validated before any drawing.
 */
struct ps2gs_solid_logic {
	__u32 abi, handle, x, y, width, height;
	__u32 color, plane_mask, alu, reserved;
};
#define PS2GS_SOLID_LOGIC _IOW('G', 0x7b, struct ps2gs_solid_logic)
#endif
