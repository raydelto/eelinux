/* SPDX-License-Identifier: GPL-2.0 */
static int ps2gs_texture_locked(struct ps2gs_session *session,
		struct fb_info *info, void __user *arg)
{
	struct ps2gs_texture_submit request;
	struct ps2gs_buffer front, *color, *depth = NULL, *texture;
	struct ps2gs_texture_triangle *triangles;
	u32 i, role = 0;
	int ret;
	if (copy_from_user(&request, arg, sizeof(request)))
		return -EFAULT;
	if (request.abi != PS2GS_ABI_VERSION || !request.count || request.count > PS2GS_MAX_COMMANDS)
		return -EINVAL;
	color = ps2gs_session_buffer(session, info, request.target, &front);
	texture = ps2gs_arena_lookup(&session->arena, request.texture);
	if (!color || !texture || ps2gs_depth_role(session, request.texture))
		return -ENOENT;
	if (color == texture)
		return -EOPNOTSUPP;
	if (request.width != color->width || request.height != color->height ||
	    request.bits_per_pixel != color->bits_per_pixel)
		return -ESTALE;
	if (request.depth) {
		depth = ps2gs_arena_lookup(&session->arena, request.depth);
		role = ps2gs_depth_role(session, request.depth);
		if (!depth || !role)
			return -ENOENT;
	}
	if (request.triangles >> 32)
		return -EFAULT;
	triangles = memdup_user(u64_to_user_ptr(request.triangles), request.count * sizeof(*triangles));
	if (IS_ERR(triangles))
		return PTR_ERR(triangles);
	for (i = 0; i < request.count; ++i) {
		ret = ps2gs_validate_texture(color, depth, role, texture, &triangles[i]);
		if (ret)
			goto out;
	}
	for (i = 0; i < request.count; ++i) {
		struct ps2gs_texture_operation operation = { .texture = texture, .triangle = &triangles[i] };
		ret = ps2gs_surface_execute(info, PS2GS_TEXTURE_SUBMIT, depth, color, &operation, NULL);
		if (ret) {
			session->failed = ret;
			goto out;
		}
	}
out:
	kfree(triangles);
	return ret;
}
