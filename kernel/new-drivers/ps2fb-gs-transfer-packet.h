/* SPDX-License-Identifier: GPL-2.0 */
static size_t ps2gs_package_transfer(union package *base,
		const struct ps2gs_buffer *color, const struct ps2gs_buffer *texture,
		const struct ps2gs_texture_triangle *triangle, u32 mask, u32 mode)
{
	size_t count = ps2gs_package_texture(base, color, NULL, texture, triangle);
	union package *p;
	size_t i;
	/* Reuse the pinned packet's validated geometry, texture state and fence.
	 * Insert blend state immediately before PRIM, never after its vertices. */
	for (i = 1; i < count; ++i) {
		if (base[i].gif.packed.ad.addr == gs_addr_frame_2)
			base[i].gif.packed.ad.data.frame_1.fbmsk =
				~ps2gs_logic_rgb(mask, color->bits_per_pixel);
		if (base[i].gif.packed.ad.addr == gs_addr_colclamp)
			base[i].gif.doubleword[0] = mode != PS2GS_TRANSFER_ADD;
		if (base[i].gif.packed.ad.addr == gs_addr_prim)
			break;
	}
	memmove(base + i + 4, base + i, (count - i) * sizeof(*base));
	p = base + i;
	GIF_PACKAGE_AD(p) { .addr = gs_addr_alpha_2,
		.data.alpha_1 = {
			.a = mode == PS2GS_TRANSFER_SUBTRACT ? gs_alpha_a_0 : gs_alpha_a_cs,
			.b = mode == PS2GS_TRANSFER_SUBTRACT ? gs_alpha_b_cs : gs_alpha_b_0,
			.c = gs_alpha_c_fix, .d = gs_alpha_d_cd, .fix = 128 } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_dthe };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_pabe };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_fba_2 };
	p->gif.packed.ad.data.prim.abe = mode != PS2GS_TRANSFER_REPLACE;
	count += 4;
	/* Reset mask and clamp before FINISH. Following native drawing must not
	 * inherit this operation's bit-plane state. */
	p = base + count - 1;
	GIF_PACKAGE_AD(p) { .addr = gs_addr_frame_2,
		.data.frame_1 = { .fbp = color->base / 8192,
			.fbw = color->buffer_width / 64, .psm = ps2gs_buffer_psm(color) } };
	p = ps2gs_texture_raw(p, gs_addr_colclamp, 1);
	GIF_PACKAGE_AD(p) { .addr = gs_addr_finish };
	count = p - base;
	base->gif.tag.nloop = count - 1;
	return count;
}
struct ps2gs_transfer_operation {
	const struct ps2gs_buffer *texture;
	const struct ps2gs_texture_triangle *triangle;
	u32 mask, mode;
};
