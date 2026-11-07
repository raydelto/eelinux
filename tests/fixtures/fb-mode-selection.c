/* SPDX-License-Identifier: GPL-2.0-only
 * Unchanged fb_try_mode/fb_find_mode, Linux 7.2.8 modedb.c.
 * CVT and validation dependencies are provided by the host harness. */
/*
 *  linux/drivers/video/modedb.c -- Standard video mode database management
 *
 *	Copyright (C) 1999 Geert Uytterhoeven
 *
 *	2001 - Documented with DocBook
 *	- Brad Douglas <brad@neruo.com>
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

static int fb_try_mode(struct fb_var_screeninfo *var, struct fb_info *info,
		       const struct fb_videomode *mode, unsigned int bpp)
{
	int err = 0;

	DPRINTK("Trying mode %s %dx%d-%d@%d\n",
		mode->name ? mode->name : "noname",
		mode->xres, mode->yres, bpp, mode->refresh);
	var->xres = mode->xres;
	var->yres = mode->yres;
	var->xres_virtual = mode->xres;
	var->yres_virtual = mode->yres;
	var->xoffset = 0;
	var->yoffset = 0;
	var->bits_per_pixel = bpp;
	var->activate |= FB_ACTIVATE_TEST;
	var->pixclock = mode->pixclock;
	var->left_margin = mode->left_margin;
	var->right_margin = mode->right_margin;
	var->upper_margin = mode->upper_margin;
	var->lower_margin = mode->lower_margin;
	var->hsync_len = mode->hsync_len;
	var->vsync_len = mode->vsync_len;
	var->sync = mode->sync;
	var->vmode = mode->vmode;
	if (info->fbops->fb_check_var)
		err = info->fbops->fb_check_var(var, info);
	var->activate &= ~FB_ACTIVATE_TEST;
	return err;
}

/**
 * fb_find_mode - finds a valid video mode
 * @var: frame buffer user defined part of display
 * @info: frame buffer info structure
 * @mode_option: string video mode to find
 * @db: video mode database
 * @dbsize: size of @db
 * @default_mode: default video mode to fall back to
 * @default_bpp: default color depth in bits per pixel
 *
 * Finds a suitable video mode, starting with the specified mode
 * in @mode_option with fallback to @default_mode.  If
 * @default_mode fails, all modes in the video mode database will
 * be tried.
 *
 * Valid mode specifiers for @mode_option::
 *
 *     <xres>x<yres>[M][R][-<bpp>][@<refresh>][i][p][m]
 *
 * or ::
 *
 *     <name>[-<bpp>][@<refresh>]
 *
 * with <xres>, <yres>, <bpp> and <refresh> decimal numbers and
 * <name> a string.
 *
 * If 'M' is present after yres (and before refresh/bpp if present),
 * the function will compute the timings using VESA(tm) Coordinated
 * Video Timings (CVT).  If 'R' is present after 'M', will compute with
 * reduced blanking (for flatpanels).  If 'i' or 'p' are present, compute
 * interlaced or progressive mode.  If 'm' is present, add margins equal
 * to 1.8% of xres rounded down to 8 pixels, and 1.8% of yres. The char
 * 'i', 'p' and 'm' must be after 'M' and 'R'. Example::
 *
 *     1024x768MR-8@60m - Reduced blank with margins at 60Hz.
 *
 * NOTE: The passed struct @var is _not_ cleared!  This allows you
 * to supply values for e.g. the grayscale and accel_flags fields.
 *
 * Returns zero for failure, 1 if using specified @mode_option,
 * 2 if using specified @mode_option with an ignored refresh rate,
 * 3 if default mode is used, 4 if fall back to any valid mode.
 */

int fb_find_mode(struct fb_var_screeninfo *var,
		 struct fb_info *info, const char *mode_option,
		 const struct fb_videomode *db, unsigned int dbsize,
		 const struct fb_videomode *default_mode,
		 unsigned int default_bpp)
{
	char *mode_option_buf __free(kfree) = NULL;
	int i;

	/* Set up defaults */
	if (!db) {
		db = modedb;
		dbsize = ARRAY_SIZE(modedb);
	}

	if (!default_mode)
		default_mode = &db[0];

	if (!default_bpp)
		default_bpp = 8;

	/* Did the user specify a video mode? */
	if (!mode_option) {
		fb_get_options(NULL, &mode_option_buf);
		mode_option = mode_option_buf;
	}
	if (mode_option) {
		const char *name = mode_option;
		unsigned int namelen = strlen(name);
		int res_specified = 0, bpp_specified = 0, refresh_specified = 0;
		unsigned int xres = 0, yres = 0, bpp = default_bpp, refresh = 0;
		int yres_specified = 0, cvt = 0, rb = 0;
		int interlace_specified = 0, interlace = 0;
		int margins = 0;
		u32 best, diff, tdiff;

		for (i = namelen-1; i >= 0; i--) {
			switch (name[i]) {
			case '@':
				namelen = i;
				if (!refresh_specified && !bpp_specified &&
				    !yres_specified) {
					refresh = simple_strtol(&name[i+1], NULL,
								10);
					refresh_specified = 1;
					if (cvt || rb)
						cvt = 0;
				} else
					goto done;
				break;
			case '-':
				namelen = i;
				if (!bpp_specified && !yres_specified) {
					bpp = simple_strtol(&name[i+1], NULL,
							    10);
					bpp_specified = 1;
					if (cvt || rb)
						cvt = 0;
				} else
					goto done;
				break;
			case 'x':
				if (!yres_specified) {
					yres = simple_strtol(&name[i+1], NULL,
							     10);
					yres_specified = 1;
				} else
					goto done;
				break;
			case '0' ... '9':
				break;
			case 'M':
				if (!yres_specified)
					cvt = 1;
				break;
			case 'R':
				if (!cvt)
					rb = 1;
				break;
			case 'm':
				if (!cvt)
					margins = 1;
				break;
			case 'p':
				if (!cvt) {
					interlace = 0;
					interlace_specified = 1;
				}
				break;
			case 'i':
				if (!cvt) {
					interlace = 1;
					interlace_specified = 1;
				}
				break;
			default:
				goto done;
			}
		}
		if (i < 0 && yres_specified) {
			xres = simple_strtol(name, NULL, 10);
			res_specified = 1;
		}
done:
		if (cvt) {
			struct fb_videomode cvt_mode;
			int ret;

			DPRINTK("CVT mode %dx%d@%dHz%s%s%s\n", xres, yres,
				(refresh) ? refresh : 60,
				(rb) ? " reduced blanking" : "",
				(margins) ? " with margins" : "",
				(interlace) ? " interlaced" : "");

			memset(&cvt_mode, 0, sizeof(cvt_mode));
			cvt_mode.xres = xres;
			cvt_mode.yres = yres;
			cvt_mode.refresh = (refresh) ? refresh : 60;

			if (interlace)
				cvt_mode.vmode |= FB_VMODE_INTERLACED;
			else
				cvt_mode.vmode &= ~FB_VMODE_INTERLACED;

			ret = fb_find_mode_cvt(&cvt_mode, margins, rb);

			if (!ret && !fb_try_mode(var, info, &cvt_mode, bpp)) {
				DPRINTK("modedb CVT: CVT mode ok\n");
				return 1;
			}

			DPRINTK("CVT mode invalid, getting mode from database\n");
		}

		DPRINTK("Trying specified video mode%s %ix%i\n",
			refresh_specified ? "" : " (ignoring refresh rate)",
			xres, yres);

		if (!refresh_specified) {
			/*
			 * If the caller has provided a custom mode database and
			 * a valid monspecs structure, we look for the mode with
			 * the highest refresh rate.  Otherwise we play it safe
			 * it and try to find a mode with a refresh rate closest
			 * to the standard 60 Hz.
			 */
			if (db != modedb &&
			    info->monspecs.vfmin && info->monspecs.vfmax &&
			    info->monspecs.hfmin && info->monspecs.hfmax &&
			    info->monspecs.dclkmax) {
				refresh = 1000;
			} else {
				refresh = 60;
			}
		}

		diff = -1;
		best = -1;
		for (i = 0; i < dbsize; i++) {
			if ((name_matches(db[i], name, namelen) ||
			     (res_specified && res_matches(db[i], xres, yres))) &&
			    !fb_try_mode(var, info, &db[i], bpp)) {
				const int db_interlace = (db[i].vmode &
					FB_VMODE_INTERLACED ? 1 : 0);
				int score = abs(db[i].refresh - refresh);

				if (interlace_specified)
					score += abs(db_interlace - interlace);

				if (!interlace_specified ||
				    db_interlace == interlace)
					if (refresh_specified &&
					    db[i].refresh == refresh)
						return 1;

				if (score < diff) {
					diff = score;
					best = i;
				}
			}
		}
		if (best != -1) {
			fb_try_mode(var, info, &db[best], bpp);
			return (refresh_specified) ? 2 : 1;
		}

		diff = 2 * (xres + yres);
		best = -1;
		DPRINTK("Trying best-fit modes\n");
		for (i = 0; i < dbsize; i++) {
			DPRINTK("Trying %ix%i\n", db[i].xres, db[i].yres);
			if (!fb_try_mode(var, info, &db[i], bpp)) {
				tdiff = abs(db[i].xres - xres) +
					abs(db[i].yres - yres);

				/*
				 * Penalize modes with resolutions smaller
				 * than requested.
				 */
				if (xres > db[i].xres || yres > db[i].yres)
					tdiff += xres + yres;

				if (diff > tdiff) {
					diff = tdiff;
					best = i;
				}
			}
		}
		if (best != -1) {
			fb_try_mode(var, info, &db[best], bpp);
			return 5;
		}
	}

	DPRINTK("Trying default video mode\n");
	if (!fb_try_mode(var, info, default_mode, default_bpp))
		return 3;

	DPRINTK("Trying all modes\n");
	for (i = 0; i < dbsize; i++)
		if (!fb_try_mode(var, info, &db[i], default_bpp))
			return 4;

	DPRINTK("No valid mode found\n");
	return 0;
}
