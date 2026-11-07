/* SPDX-License-Identifier: GPL-2.0 */
static int ps2gs_solid_logic_locked(struct ps2gs_session *session,
		struct fb_info *info, void __user *arg)
{
	struct ps2gs_solid_logic request;
	struct ps2gs_buffer front, *buffer;
	int ret;
	if (copy_from_user(&request, arg, sizeof(request))) return -EFAULT;
	buffer = ps2gs_session_buffer(session, info, request.handle, &front);
	if (!buffer) return -ENOENT;
	ret = ps2gs_validate_solid_logic(buffer, &request);
	if (ret) return ret;
	ret = ps2gs_surface_execute(info, PS2GS_SOLID_LOGIC, NULL, buffer, &request, NULL);
	if (ret) session->failed = ret;
	return ret;
}
