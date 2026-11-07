/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_PS2_GS_SURFACE_H
#define _UAPI_PS2_GS_SURFACE_H
#include "ps2-gs.h"

/* /dev/ps2-gs is an exclusive, privileged drawing session. Handles belong to
 * the open file description (dup shares it); closing frees all its surfaces.
 * Mode/font/pan changes invalidate the session with ESTALE: close and reopen.
 * Handle zero names the current front buffer. No VRAM address is accepted.
 * Use KD_GRAPHICS on the active VT for drawing; restore KD_TEXT after close.
 * Reset front-buffer pan offsets through fbdev before opening the session.
 * Transfers/fills/copies are synchronous. Hardware failures poison the session;
 * earlier drawing must not be replayed. Old /dev/fb0 ABI-1 remains supported
 * when no session owns the GS. Text console rendering retains context 1.
 */
#define PS2GS_HW_SURFACES (1U << 5)
/* Wide Render arithmetic and simultaneous packed-format codecs need more
 * handles than the original desktop subset. VRAM remains bounded by the
 * arena's 512 8 KiB pages and its reserved front/font/depth ranges. */
#define PS2GS_MAX_SURFACES 128

struct ps2gs_surface_create {
	__u32 abi, width, height, bits_per_pixel;
	/* Output: opaque nonzero handle, padded width, reserved GS bytes. */
	__u32 handle, buffer_width, allocated_bytes, reserved;
};
struct ps2gs_surface_destroy { __u32 abi, handle, reserved[2]; };
struct ps2gs_surface_fill {
	__u32 abi, handle, x, y, width, height, rgba, reserved;
};
struct ps2gs_surface_copy {
	__u32 abi, source, destination, reserved;
	__u32 sx, sy, dx, dy, width, height, reserved2[2];
};
struct ps2gs_surface_upload {
	__u32 abi, handle, x, y, width, height, bytes, reserved;
	__aligned_u64 pixels;
};

#define PS2GS_SURFACE_CREATE  _IOWR('G', 0x73, struct ps2gs_surface_create)
#define PS2GS_SURFACE_DESTROY _IOW('G', 0x74, struct ps2gs_surface_destroy)
#define PS2GS_SURFACE_FILL    _IOW('G', 0x75, struct ps2gs_surface_fill)
#define PS2GS_SURFACE_COPY    _IOW('G', 0x76, struct ps2gs_surface_copy)
#define PS2GS_SURFACE_UPLOAD  _IOW('G', 0x77, struct ps2gs_surface_upload)
#endif
