/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_PS2_GS_RGBA_TRANSFER_H
#define _UAPI_PS2_GS_RGBA_TRANSFER_H
#include "ps2-gs-transfer.h"
#define PS2GS_HW_RGBA_TRANSFER (1U << 15)
#define PS2GS_RGBA_REPLACE 0U
#define PS2GS_RGBA_MULTIPLY 1U
/* Owned CT32 only, with the existing bounded texture geometry and ownership
 * checks. REPLACE copies all four bytes under an arbitrary mask. MULTIPLY
 * writes RGB = floor(texture.RGB * texture.A / 128), clamped to 255, with
 * RGB-only masks. No arbitrary registers, depth writes or framebuffer access. */
#define PS2GS_TEXTURE_RGBA_TRANSFER _IOW('G', 0x82, struct ps2gs_texture_transfer)
#endif
