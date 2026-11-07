/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_PS2_GS_TRANSFER_H
#define _UAPI_PS2_GS_TRANSFER_H
#include "ps2-gs-texture.h"
#define PS2GS_HW_TEXTURE_TRANSFER (1U << 9)
#define PS2GS_TRANSFER_REPLACE 0U
#define PS2GS_TRANSFER_ADD 1U
#define PS2GS_TRANSFER_SUBTRACT 2U
#define PS2GS_TRANSFER_ADD_CLAMP 3U /* Requires the channel-LUT kernel extension. */
/* Typed fixed-function texture operation. ADD wraps each eight-bit channel;
 * SUBTRACT clamps. FRAME masks preserve every other RGB bit and spare/alpha
 * bit. No arbitrary register programming, shaders or CPU destination access.
 * Existing texture validation and file ownership rules apply unchanged. */
struct ps2gs_texture_transfer {
	struct ps2gs_texture_submit draw;
	__u32 plane_mask, mode;
};
#define PS2GS_TEXTURE_TRANSFER _IOW('G', 0x7c, struct ps2gs_texture_transfer)
#endif
