/* SPDX-License-Identifier: GPL-2.0 */
static size_t ps2gs_package_rgba_transfer(union package *base,
		const struct ps2gs_buffer *color, const struct ps2gs_buffer *texture,
		const struct ps2gs_texture_triangle *triangle, u32 mask, u32 mode)
{
	size_t i, count = ps2gs_package_transfer(base, color, texture, triangle, mask, PS2GS_TRANSFER_REPLACE);
	bool frame = false;
	for (i = 1; i < count; ++i) {
		if (base[i].gif.packed.ad.addr == gs_addr_frame_2 && !frame) {
			base[i].gif.packed.ad.data.frame_1.fbmsk = ~mask;
			frame = true;
		}
		if (base[i].gif.packed.ad.addr == gs_addr_tex0_2)
			base[i].gif.doubleword[0] |= 1ULL << 34;
		if (mode == PS2GS_RGBA_MULTIPLY && base[i].gif.packed.ad.addr == gs_addr_alpha_2) {
			base[i].gif.packed.ad.data.alpha_1.c = 0; /* texture alpha */
			base[i].gif.packed.ad.data.alpha_1.d = 2; /* zero */
		}
		if (base[i].gif.packed.ad.addr == gs_addr_prim)
			base[i].gif.packed.ad.data.prim.abe = mode == PS2GS_RGBA_MULTIPLY;
	}
	return count;
}
