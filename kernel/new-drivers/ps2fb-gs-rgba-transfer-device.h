/* SPDX-License-Identifier: GPL-2.0 */
static int ps2gs_rgba_transfer_locked(struct ps2gs_session *session,
		struct fb_info *info, void __user *arg)
{
	struct ps2gs_texture_transfer request;
	struct ps2gs_texture_submit *r = &request.draw;
	struct ps2gs_buffer *color, *texture;
	struct ps2gs_texture_triangle *triangles;
	u32 i;
	int ret;
	if (copy_from_user(&request, arg, sizeof(request)))
		return -EFAULT;
	if (r->abi != PS2GS_ABI_VERSION || !r->count ||
	    r->count > PS2GS_MAX_COMMANDS || r->depth || request.mode > PS2GS_RGBA_MULTIPLY ||
	    (request.mode == PS2GS_RGBA_MULTIPLY && (request.plane_mask & 0xff000000)))
		return -EINVAL;
	color = ps2gs_arena_lookup(&session->arena, r->target);
	texture = ps2gs_arena_lookup(&session->arena, r->texture);
	if (!color || !texture || ps2gs_depth_role(session, r->target) ||
	    ps2gs_depth_role(session, r->texture))
		return -ENOENT;
	if (color->bits_per_pixel != 32 || texture->bits_per_pixel != 32)
		return -EOPNOTSUPP;
	if (r->width != color->width || r->height != color->height || r->bits_per_pixel != 32)
		return -ESTALE;
	if (r->triangles >> 32)
		return -EFAULT;
	triangles = memdup_user(u64_to_user_ptr(r->triangles), r->count * sizeof(*triangles));
	if (IS_ERR(triangles))
		return PTR_ERR(triangles);
	for (i = 0; i < r->count; ++i) {
		if (triangles[i].flags) {
			ret = -EINVAL;
			goto out;
		}
		ret = ps2gs_validate_texture(color, NULL, 0, texture, &triangles[i]);
		if (ret)
			goto out;
	}
	ret = 0;
	for (i = 0; request.plane_mask && i < r->count; ++i) {
		struct ps2gs_transfer_operation operation = {
			.texture = texture, .triangle = &triangles[i],
			.mask = request.plane_mask, .mode = request.mode };
		ret = ps2gs_surface_execute(info, PS2GS_TEXTURE_RGBA_TRANSFER, NULL, color, &operation, NULL);
		if (ret) {
			session->failed = ret;
			break;
		}
	}
out:
	kfree(triangles);
	return ret;
}
