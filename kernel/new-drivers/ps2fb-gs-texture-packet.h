/* SPDX-License-Identifier: GPL-2.0 */
static int ps2gs_validate_texture(const struct ps2gs_buffer *color,
		const struct ps2gs_buffer *depth, u32 depth_role,
		const struct ps2gs_buffer *texture,
		const struct ps2gs_texture_triangle *triangle)
{
	struct ps2gs_command command = { .op = PS2GS_TRIANGLE };
	u32 i;
	if (!texture || texture == color || !texture->width || !texture->height ||
	    texture->width > 1024 || texture->height > 1024 ||
	    (texture->bits_per_pixel != 16 && texture->bits_per_pixel != 32))
		return -EOPNOTSUPP;
	if (triangle->reserved[0] || triangle->reserved[1] || triangle->reserved[2])
		return -EINVAL;
	if (triangle->flags & ~(PS2GS_DEPTH_TEST | PS2GS_TEXTURE_MODULATE))
		return -EOPNOTSUPP;
	command.flags = triangle->flags & PS2GS_DEPTH_TEST;
	for (i = 0; i < 3; ++i) {
		const struct ps2gs_texture_vertex *v = &triangle->v[i];
		/* Positive finite floats are ordered by their unsigned bit patterns.
		 * No FP code runs in the kernel. Bound Q and S/T before GS conversion. */
		if (v->q < 0x39800000 || v->q > 0x45800000 ||
		    v->s > v->q || v->t > v->q)
			return -EINVAL;
		command.v[i] = v->position;
	}
	return ps2gs_validate_render(color, depth, depth_role, &command);
}
static u32 ps2gs_texture_log2(u32 size)
{
	u32 power = 0;
	while ((1U << power) < size)
		++power;
	return power;
}
static union package *ps2gs_texture_raw(union package *p, u32 addr, u64 value)
{
	*p = (union package) { };
	p->gif.packed.ad.addr = addr;
	memcpy(&p->gif.packed.ad.data, &value, sizeof(value));
	return p + 1;
}
static size_t ps2gs_package_texture(union package *base,
		const struct ps2gs_buffer *color, const struct ps2gs_buffer *depth,
		const struct ps2gs_buffer *texture,
		const struct ps2gs_texture_triangle *triangle)
{
	union package *p = base + 1;
	u32 i;
	bool tested = triangle->flags & PS2GS_DEPTH_TEST;
	GIF_PACKAGE_AD(p) { .addr = gs_addr_frame_2,
		.data.frame_1 = { .fbp = color->base / 8192, .fbw = color->buffer_width / 64,
			.psm = ps2gs_buffer_psm(color) } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_xyoffset_2, .data.xyoffset_1 = { } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_scissor_2,
		.data.scissor_1 = { .scax1 = color->width - 1, .scay1 = color->height - 1 } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_zbuf_2,
		.data.zbuf = { .zbp = depth ? depth->base / 8192 : 0,
			.psm = color->bits_per_pixel == 16 ? 2 : 0xa,
			.zmsk = tested ? gs_zbuf_on : gs_zbuf_off } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_test_2,
		.data.test_1 = { .zte = gs_depth_test_on,
			.ztst = tested ? gs_depth_gequal : gs_depth_pass } };
	/* FINISH in the preceding operation completes uploads/copies/draws.
	 * Explicitly invalidate before sampling, including render-to-texture. */
	GIF_PACKAGE_AD(p) { .addr = gs_addr_texflush };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_tex0_2,
		.data.tex0 = { .tbp0 = texture->base / 256, .tbw = texture->buffer_width / 64,
			.psm = ps2gs_buffer_psm(texture), .tw = ps2gs_texture_log2(texture->width),
			.th = ps2gs_texture_log2(texture->height), .tcc = gs_tcc_rgb,
			.tfx = triangle->flags & PS2GS_TEXTURE_MODULATE ? gs_tfx_modulate : gs_tfx_decal } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_tex1_2,
		.data.tex1 = { .lcm = gs_lcm_fixed, .mmin = gs_lod_nearest, .mmag = gs_lod_nearest } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_clamp_2,
		.data.clamp_2 = { .wms = gs_clamp_region_clamp, .wmt = gs_clamp_region_clamp,
			.maxu = texture->width - 1, .maxv = texture->height - 1 } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_prmodecont, .data.prmodecont = { .ac = 1 } };
	p = ps2gs_texture_raw(p, gs_addr_colclamp, 1);
	GIF_PACKAGE_AD(p) { .addr = gs_addr_prim,
		.data.prim = { .prim = gs_triangle, .ctxt = gs_context_2,
			.iip = gs_gouraud_shading, .tme = gs_texturing_on, .fst = gs_texturing_stq } };
	for (i = 0; i < 3; ++i) {
		const struct ps2gs_texture_vertex *v = &triangle->v[i];
		GIF_PACKAGE_AD(p) { .addr = gs_addr_rgbaq,
			.data.rgbaq = { .r = v->position.rgba & 255, .g = (v->position.rgba >> 8) & 255,
				.b = (v->position.rgba >> 16) & 255, .a = v->position.rgba >> 24, .q = v->q } };
		p = ps2gs_texture_raw(p, gs_addr_st, (u64)v->s | (u64)v->t << 32);
		GIF_PACKAGE_AD(p) { .addr = gs_addr_xyz2,
			.data.xyz2 = { .x = v->position.x, .y = v->position.y, .z = v->position.z } };
	}
	GIF_PACKAGE_AD(p) { .addr = gs_addr_finish };
	base->gif.tag = (struct gif_tag) { .flg = gif_packed_mode, .reg0 = gif_reg_ad,
		.nreg = 1, .nloop = p - base - 1, .eop = 1 };
	return p - base;
}
struct ps2gs_texture_operation {
	const struct ps2gs_buffer *texture;
	const struct ps2gs_texture_triangle *triangle;
};
