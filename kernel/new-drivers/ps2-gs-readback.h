/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_PS2_GS_READBACK_H
#define _UAPI_PS2_GS_READBACK_H
#include "ps2-gs-surface.h"
#define PS2GS_HW_READBACK (1U << 10)
#define PS2GS_MAX_READBACK_BYTES 8192U
/* Read-only local-to-host transfer, tightly packed native CT16/CT32 pixels.
 * Width has local-transfer alignment (4/2 pixels); total bytes are a multiple
 * of 128. Arbitrary client rectangles are staged/masked on GS by userspace.
 * No CPU rendering or writable VRAM mapping. Handle ownership and VT/epoch
 * rules are identical to drawing. A hardware timeout poisons the session. */
struct ps2gs_surface_download {
	__u32 abi, handle, x, y, width, height, bytes, reserved;
	__aligned_u64 pixels;
};
#define PS2GS_SURFACE_DOWNLOAD _IOW('G', 0x7d, struct ps2gs_surface_download)
#endif
