/* SPDX-License-Identifier: GPL-2.0 */
static int ps2gs_map_locked(struct ps2gs_session *session,
		struct fb_info *info, void __user *arg)
{
	struct ps2gs_channel_map r;
	struct ps2gs_buffer *source, *destination, *palette;
	u32 first, total;
	int ret;
	if (copy_from_user(&r, arg, sizeof(r)))
		return -EFAULT;
	if (r.abi != PS2GS_ABI_VERSION || r.source_channel > 3 || !r.width || !r.height ||
	    r.width > 64 || r.height > 32 || !r.source || !r.destination || !r.palette ||
	    r.source == r.destination || r.source == r.palette || r.destination == r.palette)
		return -EINVAL;
	source = ps2gs_arena_lookup(&session->arena, r.source);
	destination = ps2gs_arena_lookup(&session->arena, r.destination);
	palette = ps2gs_arena_lookup(&session->arena, r.palette);
	if (!source || !destination || !palette)
		return -ENOENT;
	if (ps2gs_depth_role(session, r.source) || ps2gs_depth_role(session, r.destination) ||
	    ps2gs_depth_role(session, r.palette) || source->bits_per_pixel != 32 ||
	    destination->bits_per_pixel != 32 || palette->bits_per_pixel != 32 ||
	    source->width != 64 || source->height != 32 || destination->width != 64 ||
	    destination->height != 32 || palette->width != 16 || palette->height != 16 ||
	    source->buffer_width != 64 || destination->buffer_width != 64 || palette->buffer_width != 64)
		return -EOPNOTSUPP;
	if (!r.plane_mask)
		return 0;
	total = DIV_ROUND_UP(r.width, 4) * DIV_ROUND_UP(r.height, 2);
	for (first = 0; first < total; first += 48) {
		struct ps2gs_lut_operation op = { .texture = source, .palette = palette,
			.width = r.width, .height = r.height, .mask = r.plane_mask,
			.channel = r.source_channel, .first = first,
			.count = min_t(u32, total - first, 48) };
		ret = ps2gs_surface_execute(info, PS2GS_CHANNEL_MAP, NULL, destination, &op, NULL);
		if (ret) {
			session->failed = ret;
			return ret;
		}
	}
	return 0;
}
