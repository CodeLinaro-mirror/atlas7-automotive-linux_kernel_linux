/*
 * CSR sirfsoc framebuffer driver
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
 */
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>

#include <video/sirfsoc_fb.h>

#include "sirfsocfb.h"

#define MODULE_NAME     "sirfsocfb"
#define SIRFSOCFB_NAME  MODULE_NAME

static u32 sirfsocfb_get_region_paddr(const struct sirfsocfb_info *sfbi)
{
	return sfbi->region->paddr;
}

static struct sirfsocfb_colormode sirfsocfb_colormodes[] = {
	{
		.fmt = VDSS_PIXELFORMAT_UYVY,
		.bits_per_pixel = 16,
		.nonstd = FORMAT_YUV_422,
	}, {
		.fmt = VDSS_PIXELFORMAT_565,
		.bits_per_pixel = 16,
		.red	= { .length = 5, .offset = 11, .msb_right = 0 },
		.green	= { .length = 6, .offset = 5, .msb_right = 0 },
		.blue	= { .length = 5, .offset = 0, .msb_right = 0 },
		.transp	= { .length = 0, .offset = 0, .msb_right = 0 },
	}, {
		.fmt = VDSS_PIXELFORMAT_8888,
		.bits_per_pixel = 32,
		.red	= { .length = 8, .offset = 16, .msb_right = 0 },
		.green	= { .length = 8, .offset = 8, .msb_right = 0 },
		.blue	= { .length = 8, .offset = 0, .msb_right = 0 },
		.transp	= { .length = 8, .offset = 24, .msb_right = 0 },
	},
};

static bool cmp_var_to_colormode(struct fb_var_screeninfo *var,
	struct sirfsocfb_colormode *color)
{
	bool cmp_component(struct fb_bitfield *f1, struct fb_bitfield *f2)
	{
		return f1->length == f2->length &&
			f1->offset == f2->offset &&
			f1->msb_right == f2->msb_right;
	}

	if (var->bits_per_pixel == 0 ||
			var->red.length == 0 ||
			var->blue.length == 0 ||
			var->green.length == 0)
		return 0;

	return var->bits_per_pixel == color->bits_per_pixel &&
		cmp_component(&var->red, &color->red) &&
		cmp_component(&var->green, &color->green) &&
		cmp_component(&var->blue, &color->blue) &&
		cmp_component(&var->transp, &color->transp);
}

static void assign_colormode_to_var(struct fb_var_screeninfo *var,
	struct sirfsocfb_colormode *color)
{
	var->bits_per_pixel = color->bits_per_pixel;
	var->nonstd = color->nonstd;
	var->red = color->red;
	var->green = color->green;
	var->blue = color->blue;
	var->transp = color->transp;
}

int vdss_fmt_to_fb_mode(enum vdss_pixelformat fmt,
	struct fb_var_screeninfo *var)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sirfsocfb_colormodes); ++i) {
		struct sirfsocfb_colormode *mode = &sirfsocfb_colormodes[i];

		if (fmt == mode->fmt) {
			assign_colormode_to_var(var, mode);
			return 0;
		}
	}
	return -ENOENT;
}

static int fb_mode_to_vdss_fmt(struct fb_var_screeninfo *var,
	enum vdss_pixelformat *fmt)
{
	enum vdss_pixelformat vdss_fmt;
	int i;

	/* first match with nonstd field */
	if (var->nonstd) {
		for (i = 0; i < ARRAY_SIZE(sirfsocfb_colormodes); ++i) {
			struct sirfsocfb_colormode *m =
				&sirfsocfb_colormodes[i];
			if (var->nonstd == m->nonstd) {
				assign_colormode_to_var(var, m);
				*fmt = m->fmt;
				return 0;
			}
		}

		return -EINVAL;
	}

	/* then try exact match of bpp and colors */
	for (i = 0; i < ARRAY_SIZE(sirfsocfb_colormodes); ++i) {
		struct sirfsocfb_colormode *m = &sirfsocfb_colormodes[i];

		if (cmp_var_to_colormode(var, m)) {
			assign_colormode_to_var(var, m);
			*fmt = m->fmt;
			return 0;
		}
	}
	/* match with bpp if user has not filled color fields
	 * properly */
	switch (var->bits_per_pixel) {
	case 16:
		vdss_fmt = VDSS_PIXELFORMAT_565;
		break;
	case 32:
		vdss_fmt = VDSS_PIXELFORMAT_8888;
		break;
	default:
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(sirfsocfb_colormodes); ++i) {
		struct sirfsocfb_colormode *m = &sirfsocfb_colormodes[i];

		if (vdss_fmt == m->fmt) {
			assign_colormode_to_var(var, m);
			*fmt = m->fmt;
			return 0;
		}
	}

	return -EINVAL;
}

static int check_fb_res_bounds(struct fb_var_screeninfo *var)
{
	int xres_min = SIRFSOCFB_PLANE_XRES_MIN;
	int xres_max = 2048;
	int yres_min = SIRFSOCFB_PLANE_YRES_MIN;
	int yres_max = 2048;

	/* XXX: some applications seem to set virtual res to 0. */
	if (var->xres_virtual == 0)
		var->xres_virtual = var->xres;

	if (var->yres_virtual == 0)
		var->yres_virtual = var->yres;

	if (var->xres_virtual < xres_min || var->yres_virtual < yres_min)
		return -EINVAL;

	if (var->xres < xres_min)
		var->xres = xres_min;
	if (var->yres < yres_min)
		var->yres = yres_min;
	if (var->xres > xres_max)
		var->xres = xres_max;
	if (var->yres > yres_max)
		var->yres = yres_max;

	if (var->xres > var->xres_virtual)
		var->xres = var->xres_virtual;
	if (var->yres > var->yres_virtual)
		var->yres = var->yres_virtual;

	return 0;
}
static void shrink_height(unsigned long max_frame_size,
		struct fb_var_screeninfo *var)
{
	DBG("can't fit FB into memory, reducing y\n");

	var->yres_virtual = max_frame_size /
		(var->xres_virtual * var->bits_per_pixel >> 3);

	if (var->yres_virtual < SIRFSOCFB_PLANE_YRES_MIN)
		var->yres_virtual = SIRFSOCFB_PLANE_YRES_MIN;

	if (var->yres > var->yres_virtual)
		var->yres = var->yres_virtual;
}

static void shrink_width(unsigned long max_frame_size,
		struct fb_var_screeninfo *var)
{
	DBG("can't fit FB into memory, reducing x\n");

	var->xres_virtual = max_frame_size / var->yres_virtual /
		(var->bits_per_pixel >> 3);

	if (var->xres_virtual < SIRFSOCFB_PLANE_XRES_MIN)
		var->xres_virtual = SIRFSOCFB_PLANE_XRES_MIN;

	if (var->xres > var->xres_virtual)
		var->xres = var->xres_virtual;
}

static int check_fb_size(const struct sirfsocfb_info *sfbi,
	struct fb_var_screeninfo *var)
{
	unsigned long max_frame_size = sfbi->region->size;
	int bytespp = var->bits_per_pixel >> 3;
	unsigned long line_size = var->xres_virtual * bytespp;

	DBG("max frame size %lu, line size %lu\n", max_frame_size, line_size);

	if (line_size * var->yres_virtual > max_frame_size)
		shrink_height(max_frame_size, var);

	if (line_size * var->yres_virtual > max_frame_size) {
		shrink_width(max_frame_size, var);
		line_size = var->xres_virtual * bytespp;
	}

	if (line_size * var->yres_virtual > max_frame_size) {
		DBG("cannot fit FB to memory\n");
		return -EINVAL;
	}

	return 0;
}

void set_fb_fix(struct fb_info *fbi)
{
	struct fb_fix_screeninfo *fix = &fbi->fix;
	struct fb_var_screeninfo *var = &fbi->var;
	struct sirfsocfb_info *sfbi = FB2SFB(fbi);
	struct sirfsocfb_mem_region *rg = sfbi->region;

	DBG("set_fb_fix\n");

	/* used by open/write in fbmem.c */
	fbi->screen_base = (char __iomem *)rg->vaddr;

	fix->line_length =
		(var->xres_virtual * var->bits_per_pixel) >> 3;
	fix->smem_len = rg->size;

	fix->smem_start = rg->paddr;

	fix->type = FB_TYPE_PACKED_PIXELS;

	if (var->nonstd)
		fix->visual = FB_VISUAL_PSEUDOCOLOR;
	else {
		switch (var->bits_per_pixel) {
		case 32:
		case 24:
		case 16:
		case 12:
			fix->visual = FB_VISUAL_TRUECOLOR;
			/* 12bpp is stored in 16 bits */
			break;
		case 1:
		case 2:
		case 4:
		case 8:
			fix->visual = FB_VISUAL_PSEUDOCOLOR;
			break;
		}
	}

	fix->accel = FB_ACCEL_NONE;

	fix->xpanstep = 1;
	fix->ypanstep = 1;
}

/* check new var and possibly modify it to be ok */
int check_fb_var(struct fb_info *fbi, struct fb_var_screeninfo *var)
{
	struct sirfsocfb_info *sfbi = FB2SFB(fbi);
	struct sirfsoc_vdss_panel *panel = fb2display(fbi);
	enum vdss_pixelformat fmt = 0;
	int r;

	DBG("check_fb_var %d\n", sfbi->id);

	WARN_ON(!atomic_read(&sfbi->region->lock_count));

	r = fb_mode_to_vdss_fmt(var, &fmt);
	if (r) {
		DBG("cannot convert var to sirfsoc vdss mode\n");
		return r;
	}

	if (check_fb_res_bounds(var))
		return -EINVAL;

	/* When no memory is allocated ignore the size check */
	if (sfbi->region->size != 0 && check_fb_size(sfbi, var))
		return -EINVAL;

	if (var->xres + var->xoffset > var->xres_virtual)
		var->xoffset = var->xres_virtual - var->xres;
	if (var->yres + var->yoffset > var->yres_virtual)
		var->yoffset = var->yres_virtual - var->yres;

	DBG("xres = %d, yres = %d, vxres = %d, vyres = %d\n",
		var->xres, var->yres,
		var->xres_virtual, var->yres_virtual);

	if (panel && panel->driver->get_dimensions) {
		u32 w, h;

		panel->driver->get_dimensions(panel, &w, &h);
		var->width = DIV_ROUND_CLOSEST(w, 1000);
		var->height = DIV_ROUND_CLOSEST(h, 1000);
	} else {
		var->height = -1;
		var->width = -1;
	}

	var->grayscale          = 0;

	if (panel && panel->driver->get_timings) {
		struct sirfsoc_video_timings timings;

		panel->driver->get_timings(panel, &timings);

		/* pixclock in ps, the rest in pixclock */
		var->pixclock = timings.pixel_clock != 0 ?
			KHZ2PICOS(timings.pixel_clock / 1000) :
			0;
		var->left_margin = timings.hbp;
		var->right_margin = timings.hfp;
		var->upper_margin = timings.vbp;
		var->lower_margin = timings.vfp;
		var->hsync_len = timings.hsw;
		var->vsync_len = timings.vsw;
		var->sync |=
			timings.hsync_level == SIRFSOC_VDSS_SIG_ACTIVE_HIGH ?
			FB_SYNC_HOR_HIGH_ACT : 0;
		var->sync |=
			timings.vsync_level == SIRFSOC_VDSS_SIG_ACTIVE_HIGH ?
			FB_SYNC_VERT_HIGH_ACT : 0;
		var->vmode = timings.interlace ?
			FB_VMODE_INTERLACED : FB_VMODE_NONINTERLACED;
	} else {
		var->pixclock = 0;
		var->left_margin = 0;
		var->right_margin = 0;
		var->upper_margin = 0;
		var->lower_margin = 0;
		var->hsync_len = 0;
		var->vsync_len = 0;
		var->sync = 0;
		var->vmode = FB_VMODE_NONINTERLACED;
	}

	return 0;
}

static void sirfsocfb_calc_addr(const struct sirfsocfb_info *sfbi,
	const struct fb_var_screeninfo *var,
	const struct fb_fix_screeninfo *fix,
	int rotation, u32 *paddr)
{
	u32 data_start_p;
	int offset;

	data_start_p = sirfsocfb_get_region_paddr(sfbi);

	offset = var->yoffset * fix->line_length +
		var->xoffset * (var->bits_per_pixel >> 3);

	data_start_p += offset;

	if (offset)
		DBG("offset %d, %d = %d\n",
		    var->xoffset, var->yoffset, offset);

	DBG("paddr %x\n", data_start_p);

	*paddr = data_start_p;
}

/* setup overlay according to the fb */
int sirfsocfb_setup_layer(struct fb_info *fbi, struct sirfsoc_vdss_layer *l,
	int posx, int posy, int outw, int outh, bool pre_mult_alpha)
{
	int r = 0;
	struct sirfsocfb_info *sfbi = FB2SFB(fbi);
	struct fb_var_screeninfo *var = &fbi->var;
	struct fb_fix_screeninfo *fix = &fbi->fix;
	enum vdss_pixelformat fmt = 0;
	u32 data_start_p = 0;
	struct sirfsoc_vdss_layer_info info;
	int xres, yres;
	int surf_width, surf_height;
	int i;

	WARN_ON(!atomic_read(&sfbi->region->lock_count));

	for (i = 0; i < sfbi->num_layers; i++) {
		if (l != sfbi->layers[i])
			continue;
		break;
	}

	DBG("setup_overlay %d, posx %d, posy %d, outw %d, outh %d\n", sfbi->id,
		posx, posy, outw, outh);


	xres = var->xres;
	yres = var->yres;

	if (sfbi->region->size)
		sirfsocfb_calc_addr(sfbi, var, fix, 0, &data_start_p);

	r = fb_mode_to_vdss_fmt(var, &fmt);
	if (r) {
		DBG("fb_mode_to_dss_mode failed");
		goto err;
	}

	surf_width = fix->line_length / (var->bits_per_pixel >> 3);
	surf_height = var->yres;

	l->get_info(l, &info);

	info.base = data_start_p;
	info.surf_width = surf_width;
	info.surf_height = surf_height;
	info.src_rect.left = 0;
	info.src_rect.right = info.src_rect.left + xres - 1;
	info.src_rect.top = 0;
	info.src_rect.bottom = info.src_rect.top + yres - 1;
	info.fmt = fmt;

	info.dst_rect.left = posx;
	info.dst_rect.right = info.dst_rect.left + outw - 1;
	info.dst_rect.top = posy;
	info.dst_rect.bottom = info.dst_rect.top + outh - 1;

	info.pre_mult_alpha = pre_mult_alpha;

	r = l->set_info(l, &info);
	if (r) {
		DBG("layer set_info failed\n");
		goto err;
	}

	return 0;

err:
	DBG("setup_layer failed\n");
	return r;
}

/* apply var to the overlay */
int sirfsocfb_apply_changes(struct fb_info *fbi, int init)
{
	int r = 0;
	struct sirfsocfb_info *sfbi = FB2SFB(fbi);
	struct fb_var_screeninfo *var = &fbi->var;
	struct sirfsoc_vdss_layer *l;
	int posx, posy;
	int outw, outh;
	bool pre_mult_alpha;
	int i;

	WARN_ON(!atomic_read(&sfbi->region->lock_count));

	for (i = 0; i < sfbi->num_layers; i++) {
		l = sfbi->layers[i];

		DBG("apply_changes, fb %d, ovl %d\n", sfbi->id, l->id);

		if (sfbi->region->size == 0) {
			/* the fb is not available. disable the overlay */
			sirfsocfb_layer_enable(l, 0);
			if (!init && l->screen)
				l->screen->apply(l->screen);
			continue;
		}

		if (init) {
			posx = 0;
			posy = 0;
			outw = var->xres;
			outh = var->yres;
			pre_mult_alpha = true;
		} else {
			struct sirfsoc_vdss_layer_info info;

			l->get_info(l, &info);
			posx = info.dst_rect.left;
			posy = info.dst_rect.top;
			outw = info.dst_rect.right - info.dst_rect.left + 1;
			outh = info.dst_rect.bottom - info.dst_rect.top + 1;
			pre_mult_alpha = info.pre_mult_alpha;
		}

		r = sirfsocfb_setup_layer(fbi, l, posx, posy,
			outw, outh, pre_mult_alpha);
		if (r)
			goto err;

		if (!init && l->screen)
			l->screen->apply(l->screen);
	}
	return 0;
err:
	DBG("apply_changes failed\n");
	return r;
}

static int sirfsocfb_open(struct fb_info *info, int user)
{
	return 0;
}

static int sirfsocfb_close(struct fb_info *info, int user)
{
	return 0;
}

static int sirfsocfb_check_var(struct fb_var_screeninfo *var,
	struct fb_info *fbi)
{
	struct sirfsocfb_info *sfbi = FB2SFB(fbi);
	int r;

	DBG("check_var(%d)\n", FB2SFB(fbi)->id);

	sirfsocfb_get_mem_region(sfbi->region);

	r = check_fb_var(fbi, var);

	sirfsocfb_put_mem_region(sfbi->region);

	return r;
}

static int sirfsocfb_set_par(struct fb_info *fbi)
{
	struct sirfsocfb_info *sfbi = FB2SFB(fbi);
	int r;

	DBG("set_par(%d)\n", FB2SFB(fbi)->id);

	sirfsocfb_get_mem_region(sfbi->region);

	set_fb_fix(fbi);

	r = sirfsocfb_apply_changes(fbi, 0);

	sirfsocfb_put_mem_region(sfbi->region);

	return r;
}

static int sirfsocfb_pan_display(struct fb_var_screeninfo *var,
	struct fb_info *fbi)
{
	struct sirfsocfb_info *sfbi = FB2SFB(fbi);
	struct fb_var_screeninfo new_var;
	struct sirfsoc_vdss_panel *panel = fb2display(fbi);
	struct sirfsoc_vdss_screen *scn;
	int r;

	DBG("pan_display(%d)\n", FB2SFB(fbi)->id);

	if (var->xoffset == fbi->var.xoffset &&
	    var->yoffset == fbi->var.yoffset)
		return 0;

	new_var = fbi->var;
	new_var.xoffset = var->xoffset;
	new_var.yoffset = var->yoffset;

	fbi->var = new_var;

	sirfsocfb_get_mem_region(sfbi->region);

	r = sirfsocfb_apply_changes(fbi, 0);

	sirfsocfb_put_mem_region(sfbi->region);

	scn = sirfsoc_vdss_find_screen_from_panel(panel);
	if (var->activate & FB_ACTIVATE_VBL)
		scn->wait_for_vsync(scn);

	return r;
}

static int sirfsocfb_blank(int blank, struct fb_info *fbi)
{
	struct sirfsocfb_info *sfbi = FB2SFB(fbi);
	struct sirfsocfb_device *fbdev = sfbi->fbdev;
	struct sirfsoc_vdss_panel *panel = fb2display(fbi);
	int r = 0;

	if (!panel)
		return -EINVAL;

	sirfsocfb_lock(fbdev);

	switch (blank) {
	case FB_BLANK_UNBLANK:
		if (panel->state == SIRFSOC_VDSS_PANEL_ENABLED)
			goto exit;

		r = panel->driver->enable(panel);

		break;

	case FB_BLANK_NORMAL:
	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_HSYNC_SUSPEND:
	case FB_BLANK_POWERDOWN:
		if (panel->state != SIRFSOC_VDSS_PANEL_ENABLED)
			goto exit;

		panel->driver->disable(panel);

		break;

	default:
		r = -EINVAL;
	}

exit:
	sirfsocfb_unlock(fbdev);

	return r;
}

static int sirfsocfb_set_gamma(struct fb_info *info, u8 *gamma)
{
	struct sirfsocfb_info *sfbi = FB2SFB(info);
	struct sirfsoc_vdss_layer *l = sfbi->layers[0];
	int r;

	r = l->screen->set_gamma(l->screen, gamma);

	l->screen->apply(l->screen);

	return r;
}

static int sirfsocfb_get_gamma(struct fb_info *info, u8 *gamma)
{
	struct sirfsocfb_info *sfbi = FB2SFB(info);
	struct sirfsoc_vdss_layer *l = sfbi->layers[0];
	int r = 0;

	r = l->screen->get_gamma(l->screen, gamma);

	return r;
}

static int sirfsocfb_set_toplayer(struct fb_info *info, u8 toplayer)
{
	struct sirfsocfb_info *sfbi = FB2SFB(info);
	struct sirfsoc_vdss_layer *l = sfbi->layers[0];
	struct sirfsoc_vdss_screen_info sinfo;
	int r = 0;

	if (toplayer < SIRFSOC_VDSS_LAYER0 || toplayer > SIRFSOC_VDSS_LAYER3)
		return -EINVAL;

	l->screen->get_info(l->screen, &sinfo);

	if (sinfo.top_layer != toplayer) {
		sinfo.top_layer = toplayer;
		r = l->screen->set_info(l->screen, &sinfo);
		if (r)
			return r;
		r = l->screen->apply(l->screen);
	}

	return r;
}

static int sirfsocfb_get_toplayer(struct fb_info *info, u8 *toplayer)
{
	struct sirfsocfb_info *sfbi = FB2SFB(info);
	struct sirfsoc_vdss_layer *l = sfbi->layers[0];
	struct sirfsoc_vdss_screen_info sinfo;

	l->screen->get_info(l->screen, &sinfo);

	*toplayer = sinfo.top_layer;

	return 0;
}

static int sirfsocfb_ioctl(struct fb_info *info, unsigned int cmd,
	unsigned long arg)
{
	union {
		u8 *gamma;
		u8 toplayer;
	} data;
	int r = 0;

	switch (cmd) {
	case SIRFSOCFB_SET_GAMMA:
		data.gamma = memdup_user((void __user *)arg,
			256 * 3 * sizeof(u8));
		if (IS_ERR(data.gamma))
			return PTR_ERR(data.gamma);
		r = sirfsocfb_set_gamma(info, data.gamma);
		kfree(data.gamma);
		break;
	case SIRFSOCFB_GET_GAMMA:
		data.gamma = kmalloc(256 * 3 * sizeof(u8), GFP_KERNEL);
		if (!data.gamma)
			return -ENOMEM;
		r = sirfsocfb_get_gamma(info, data.gamma);
		if (copy_to_user((void __user *)arg, data.gamma,
				256 * 3 * sizeof(u8))) {
			kfree(data.gamma);
			return -EFAULT;
		}
		kfree(data.gamma);
		break;
	case SIRFSOCFB_SET_TOPLAYER:
		if (copy_from_user(&data.toplayer, (void __user *)arg,
				   sizeof(data.toplayer)))
			r = -EFAULT;
		else
			r = sirfsocfb_set_toplayer(info, data.toplayer);
		break;
	case SIRFSOCFB_GET_TOPLAYER:
		r = sirfsocfb_get_toplayer(info, &data.toplayer);
		if (r < 0)
			break;
		if (copy_to_user((void __user *)arg, &data.toplayer,
				   sizeof(data.toplayer)))
			r = -EFAULT;
		break;
	default:
		return -EINVAL;
	}

	return r;
}

static int sirfsocfb_setcolreg(unsigned regno,
	unsigned red, unsigned green,
	unsigned blue, unsigned transp,
	struct fb_info *info)
{

	return 0;
}

static int sirfsocfb_setcmap(struct fb_cmap *cmap, struct fb_info *info)
{
	return 0;
}

static void mmap_user_open(struct vm_area_struct *vma)
{
	struct sirfsocfb_mem_region *rg = vma->vm_private_data;

	sirfsocfb_get_mem_region(rg);
	atomic_inc(&rg->map_count);
	sirfsocfb_put_mem_region(rg);
}

static void mmap_user_close(struct vm_area_struct *vma)
{
	struct sirfsocfb_mem_region *rg = vma->vm_private_data;

	sirfsocfb_get_mem_region(rg);
	atomic_dec(&rg->map_count);
	sirfsocfb_put_mem_region(rg);
}

static struct vm_operations_struct mmap_user_ops = {
	.open = mmap_user_open,
	.close = mmap_user_close,
};

static int sirfsocfb_mmap(struct fb_info *fbi, struct vm_area_struct *vma)
{
	struct sirfsocfb_info *sfbi = FB2SFB(fbi);
	struct fb_fix_screeninfo *fix = &fbi->fix;
	struct sirfsocfb_mem_region *rg;
	unsigned long start;
	u32 len;
	int r;

	rg = sirfsocfb_get_mem_region(sfbi->region);

	start = sirfsocfb_get_region_paddr(sfbi);
	len = fix->smem_len;

	DBG("user mmap region start %lx, len %d, off %lx\n", start, len,
		vma->vm_pgoff << PAGE_SHIFT);

	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	vma->vm_ops = &mmap_user_ops;
	vma->vm_private_data = rg;

	r = vm_iomap_memory(vma, start, len);
	if (r)
		goto error;

	/* vm_ops.open won't be called for mmap itself. */
	atomic_inc(&rg->map_count);

	sirfsocfb_put_mem_region(rg);

	return 0;

error:
	sirfsocfb_put_mem_region(sfbi->region);

	return r;
}

struct fb_ops sirfsocfb_ops = {.owner = THIS_MODULE,
	.fb_open = sirfsocfb_open, .fb_release = sirfsocfb_close,
	.fb_check_var = sirfsocfb_check_var, .fb_set_par = sirfsocfb_set_par,
	.fb_blank = sirfsocfb_blank, .fb_pan_display = sirfsocfb_pan_display,
	.fb_ioctl = sirfsocfb_ioctl, .fb_setcolreg = sirfsocfb_setcolreg,
	.fb_setcmap = sirfsocfb_setcmap,
	.fb_fillrect = cfb_fillrect, .fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit, .fb_mmap = sirfsocfb_mmap,
};

static int sirfsocfb_get_recommended_bpp(struct sirfsocfb_device *fbdev,
	struct sirfsoc_vdss_panel *panel)
{
	BUG_ON(panel->driver->get_recommended_bpp == NULL);
	return panel->driver->get_recommended_bpp(panel);
}

static void sirfsocfb_free_fbmem(struct fb_info *fbi)
{
	struct sirfsocfb_info *sfbi = FB2SFB(fbi);
	struct sirfsocfb_device *fbdev = sfbi->fbdev;
	struct sirfsocfb_mem_region *rg;

	rg = sfbi->region;

	if (rg->token == NULL)
		return;

	WARN_ON(atomic_read(&rg->map_count));


	dma_free_attrs(fbdev->dev, rg->size, rg->token, rg->dma_handle,
			&rg->attrs);

	rg->token = NULL;
	rg->vaddr = NULL;
	rg->paddr = 0;
	rg->alloc = 0;
	rg->size = 0;
}

static void clear_fb_info(struct fb_info *fbi)
{
	memset(&fbi->var, 0, sizeof(fbi->var));
	memset(&fbi->fix, 0, sizeof(fbi->fix));
	strlcpy(fbi->fix.id, MODULE_NAME, sizeof(fbi->fix.id));
}

static int sirfsocfb_free_all_fbmem(struct sirfsocfb_device *fbdev)
{
	int i;

	DBG("free all fbmem\n");

	for (i = 0; i < fbdev->num_fbs; i++) {
		struct fb_info *fbi = fbdev->fbs[i];

		sirfsocfb_free_fbmem(fbi);
		clear_fb_info(fbi);
	}

	return 0;
}

static int sirfsocfb_alloc_fbmem(struct fb_info *fbi, unsigned long size)
{
	struct sirfsocfb_info *sfbi = FB2SFB(fbi);
	struct sirfsocfb_device *fbdev = sfbi->fbdev;
	struct sirfsocfb_mem_region *rg;
	void *token;
	DEFINE_DMA_ATTRS(attrs);
	dma_addr_t dma_handle;

	rg = sfbi->region;

	rg->paddr = 0;
	rg->vaddr = NULL;
	rg->size = 0;
	rg->type = 0;
	rg->alloc = false;
	rg->map = false;

	size = PAGE_ALIGN(size);

	dma_set_attr(DMA_ATTR_WRITE_COMBINE, &attrs);

	DBG("allocating %lu bytes for fb %d\n", size, sfbi->id);

	token = dma_alloc_attrs(fbdev->dev, size, &dma_handle,
		GFP_KERNEL, &attrs);

	if (token == NULL) {
		dev_err(fbdev->dev, "failed to allocate framebuffer\n");
		return -ENOMEM;
	}

	DBG("allocated VRAM paddr %lx, vaddr %p\n",
		(unsigned long)dma_handle, token);

	rg->attrs = attrs;
	rg->token = token;
	rg->dma_handle = dma_handle;

	rg->paddr = (unsigned long)dma_handle;
	rg->vaddr = (void __iomem *)token;
	rg->size = size;
	rg->alloc = 1;

	return 0;
}

/* allocate fbmem using display resolution as reference */
static int sirfsocfb_alloc_fbmem_display(struct fb_info *fbi,
	unsigned long size)
{
	struct sirfsocfb_info *sfbi = FB2SFB(fbi);
	struct sirfsocfb_device *fbdev = sfbi->fbdev;

	if (!size) {
		u16 w, h;
		int bytespp;
		struct sirfsoc_vdss_panel *panel;

		panel = fb2display(fbi);
		if (!panel)
			return 0;
		switch (sirfsocfb_get_recommended_bpp(fbdev, panel)) {
		case 16:
			bytespp = 2;
			break;
		case 24:
			bytespp = 4;
			break;
		default:
			bytespp = 4;
			break;
		}

		panel->driver->get_resolution(panel, &w, &h);
		size = w * h * bytespp * 2;
	}

	if (!size)
		return 0;

	return sirfsocfb_alloc_fbmem(fbi, size);
}

static int sirfsocfb_allocate_all_fbs(struct sirfsocfb_device *fbdev)
{
	int i, r;

	for (i = 0; i < fbdev->num_fbs; i++) {
		r = sirfsocfb_alloc_fbmem_display(fbdev->fbs[i], 0);
		if (r)
			return r;
	}

	for (i = 0; i < fbdev->num_fbs; i++) {
		struct sirfsocfb_info *sfbi = FB2SFB(fbdev->fbs[i]);
		struct sirfsocfb_mem_region *rg;

		rg = sfbi->region;

		DBG("region%d phys %08x virt %p size=%lu\n",
			i,
			rg->paddr,
			rg->vaddr,
			rg->size);
	}

	return 0;
}

static void sirfsocfb_clear_fb(struct fb_info *fbi)
{
	memset(fbi->screen_base, 0xFF,
		fbi->fix.smem_len);
}

/* initialize fb_info, var, fix to something sane based on the display */
static int sirfsocfb_fb_init(struct sirfsocfb_device *fbdev,
	struct fb_info *fbi)
{
	struct fb_var_screeninfo *var = &fbi->var;
	struct sirfsoc_vdss_panel *panel = fb2display(fbi);
	struct sirfsocfb_info *sfbi = FB2SFB(fbi);
	int ret = 0;

	fbi->fbops = &sirfsocfb_ops;
	fbi->flags = FBINFO_FLAG_DEFAULT;
	fbi->pseudo_palette = fbdev->pseudo_palette;

	if (sfbi->region->size == 0) {
		clear_fb_info(fbi);
		return 0;
	}

	var->nonstd = 0;
	var->bits_per_pixel = 0;

	var->rotate = 0;

	if (panel) {
		u16 w, h;

		panel->driver->get_resolution(panel, &w, &h);

		var->xres = w;
		var->yres = h;

		var->xres_virtual = var->xres;
		var->yres_virtual = var->yres * 2;

		if (!var->bits_per_pixel) {
			switch (sirfsocfb_get_recommended_bpp(fbdev, panel)) {
			case 16:
				var->bits_per_pixel = 16;
				break;
			case 24:
				var->bits_per_pixel = 32;
				break;
			default:
				dev_err(fbdev->dev, "illegal display bpp\n");
				return -EINVAL;
			}
		}
	} else {
		/* if there's no display, let's just guess some basic values */
		var->xres = 320;
		var->yres = 240;
		var->xres_virtual = var->xres;
		var->yres_virtual = var->yres;
		if (!var->bits_per_pixel)
			var->bits_per_pixel = 16;
	}

	ret = check_fb_var(fbi, var);
	if (ret)
		goto err;

	set_fb_fix(fbi);

	ret = fb_alloc_cmap(&fbi->cmap, 256, 0);
	if (ret)
		dev_err(fbdev->dev, "unable to allocate color map memory\n");

err:
	return ret;
}

static void fbinfo_cleanup(struct sirfsocfb_device *fbdev, struct fb_info *fbi)
{
	fb_dealloc_cmap(&fbi->cmap);
}


static void sirfsocfb_free_resources(struct sirfsocfb_device *fbdev)
{
	int i;

	DBG("free_resources\n");

	if (fbdev == NULL)
		return;

	for (i = 0; i < fbdev->num_fbs; i++)
		unregister_framebuffer(fbdev->fbs[i]);

	/* free the reserved fbmem */
	sirfsocfb_free_all_fbmem(fbdev);

	for (i = 0; i < fbdev->num_fbs; i++) {
		fbinfo_cleanup(fbdev, fbdev->fbs[i]);
		framebuffer_release(fbdev->fbs[i]);
	}

	for (i = 0; i < fbdev->num_displays; i++) {
		struct sirfsoc_vdss_panel *panel = fbdev->displays[i].panel;

		if (panel == NULL)
			break;

		if (panel->state != SIRFSOC_VDSS_PANEL_DISABLED)
			panel->driver->disable(panel);
		panel->driver->disconnect(panel);

		sirfsoc_vdss_put_panel(panel);
	}

	dev_set_drvdata(fbdev->dev, NULL);
}

static int sirfsocfb_create_framebuffers(struct sirfsocfb_device *fbdev)
{
	int ret, i;

	fbdev->num_fbs = 0;

	DBG("create %d framebuffers\n",	fbdev->num_displays);

	/* allocate fb_infos */
	for (i = 0; i < fbdev->num_displays; i++) {
		struct fb_info *fbi;
		struct sirfsocfb_info *sfbi;

		fbi = framebuffer_alloc(sizeof(*sfbi), fbdev->dev);

		if (fbi == NULL) {
			dev_err(fbdev->dev,
				"unable to allocate memory for plane info\n");
			return -ENOMEM;
		}

		clear_fb_info(fbi);

		fbdev->fbs[i] = fbi;

		sfbi = FB2SFB(fbi);
		sfbi->fbdev = fbdev;
		sfbi->id = i;

		sfbi->region = &fbdev->regions[i];
		sfbi->region->id = i;
		init_rwsem(&sfbi->region->lock);

		fbdev->num_fbs++;
	}

	DBG("fb_infos allocated\n");

	/* assign overlays for the fbs */
	for (i = 0; i < fbdev->num_fbs; i++) {
		struct sirfsocfb_info *sfbi = FB2SFB(fbdev->fbs[i]);

		sfbi->layers[0] = fbdev->displays[i].layers[0];
		sfbi->num_layers = 1;
	}

	/* allocate fb memories */
	ret = sirfsocfb_allocate_all_fbs(fbdev);
	if (ret) {
		dev_err(fbdev->dev, "failed to allocate fbmem\n");
		return ret;
	}

	DBG("fbmems allocated\n");

	/* setup fb_infos */
	for (i = 0; i < fbdev->num_fbs; i++) {
		struct fb_info *fbi = fbdev->fbs[i];
		struct sirfsocfb_info *sfbi = FB2SFB(fbi);

		sirfsocfb_get_mem_region(sfbi->region);
		ret = sirfsocfb_fb_init(fbdev, fbi);
		sirfsocfb_put_mem_region(sfbi->region);

		if (ret) {
			dev_err(fbdev->dev, "failed to setup fb_info\n");
			return ret;
		}
	}

	for (i = 0; i < fbdev->num_fbs; i++) {
		struct fb_info *fbi = fbdev->fbs[i];
		struct sirfsocfb_info *sfbi = FB2SFB(fbi);

		if (sfbi->region->size == 0)
			continue;

		sirfsocfb_clear_fb(fbi);
	}

	DBG("fb_infos initialized\n");

	for (i = 0; i < fbdev->num_fbs; i++) {
		ret = register_framebuffer(fbdev->fbs[i]);
		if (ret != 0) {
			dev_err(fbdev->dev,
				"registering framebuffer %d failed\n", i);
			return ret;
		}

		dev_info(fbdev->dev, "/dev/fb%i created\n", i);
	}

	DBG("framebuffers registered\n");

	for (i = 0; i < fbdev->num_fbs; i++) {
		struct fb_info *fbi = fbdev->fbs[i];
		struct sirfsocfb_info *sfbi = FB2SFB(fbi);

		sirfsocfb_get_mem_region(sfbi->region);
		ret = sirfsocfb_apply_changes(fbi, 1);
		sirfsocfb_put_mem_region(sfbi->region);

		if (ret) {
			dev_err(fbdev->dev, "failed to change mode\n");
			return ret;
		}
	}

	/* Enable fb layers */
	for (i = 0; i < fbdev->num_fbs; i++) {
		struct fb_info *fbi = fbdev->fbs[i];
		struct sirfsocfb_info *sfbi = FB2SFB(fbi);

		if (sfbi->num_layers > 0) {
			struct sirfsoc_vdss_layer *l = sfbi->layers[0];

			l->screen->apply(l->screen);

			ret = sirfsocfb_layer_enable(l, 1);

			if (ret) {
				dev_err(fbdev->dev,
					"failed to enable layer\n");
				return ret;
			}
		}
	}

	DBG("create_framebuffers done\n");

	return 0;
}

static int sirfsocfb_init_panel(struct sirfsocfb_device *fbdev,
	struct sirfsoc_vdss_panel *panel)
{
	struct sirfsoc_vdss_driver *pdrv = panel->driver;
	int ret;

	ret = pdrv->enable(panel);
	if (ret) {
		dev_warn(fbdev->dev, "Failed to enable display '%s'\n",
				panel->name);
		return ret;
	}

	return 0;
}

static int sirfsocfb_init_connections(struct sirfsocfb_device *fbdev,
	struct sirfsoc_vdss_panel *panel)
{
	int i, ret;
	struct sirfsoc_vdss_screen *scn;
	struct sirfsocfb_display_data *d;

	ret = panel->driver->connect(panel);
	if (ret) {
		dev_err(fbdev->dev, "failed to connect default display\n");
		return ret;
	}

	scn = sirfsoc_vdss_find_screen_from_panel(panel);

	if (!scn) {
		dev_err(fbdev->dev, "no screen for the default panel\n");
		return -EINVAL;
	}

	d = get_display_data(fbdev, panel);
	for (i = 0; i < d->num_layers; i++) {
		struct sirfsoc_vdss_layer *layer = d->layers[i];

		if (layer->screen)
			layer->unset_screen(layer);

		ret = layer->set_screen(layer, scn);
		if (ret)
			dev_warn(fbdev->dev,
				"failed to connect layer %s to screen %s\n",
				layer->name, scn->name);
	}

	return 0;
}

static int sirfsocfb_init_displays(struct sirfsocfb_device *fbdev)
{
	struct sirfsoc_vdss_panel *panel;
	struct sirfsocfb_display_data *d;
	int i;

	fbdev->num_displays = 0;
	panel = NULL;

	panel = sirfsoc_vdss_get_primary_device();

	if (panel == NULL) {
		dev_err(fbdev->dev, "no primary display available\n");
		goto err;
	}

	sirfsoc_vdss_get_panel(panel);
	if (panel->driver == NULL) {
		dev_warn(fbdev->dev, "no driver for primary display: %s\n",
			panel->name);
		sirfsoc_vdss_put_panel(panel);
		goto err;
	}

	d = &fbdev->displays[fbdev->num_displays++];
	d->panel = panel;
	d->lcdc_index = SIRFSOC_VDSS_LCDC0;

	d->num_layers = sirfsoc_vdss_get_num_layers(d->lcdc_index);
	for (i = 0; i < d->num_layers; i++)
		d->layers[i] = sirfsoc_vdss_get_layer(d->lcdc_index, i);

	d->num_screens = sirfsoc_vdss_get_num_screens(d->lcdc_index);
	for (i = 0; i < d->num_screens; i++)
		d->screens[i] = sirfsoc_vdss_get_screen(d->lcdc_index, i);

	if (sirfsoc_vdss_get_num_lcdc() > 1) {
		panel = sirfsoc_vdss_get_secondary_device();
		if (panel == NULL)
			goto out;
		sirfsoc_vdss_get_panel(panel);
		if (panel->driver == NULL) {
			dev_warn(fbdev->dev, "no driver for secondary display: %s\n",
				panel->name);
			sirfsoc_vdss_put_panel(panel);
			goto out;
		}
		d = &fbdev->displays[fbdev->num_displays++];
		d->lcdc_index = SIRFSOC_VDSS_LCDC1;
		d->num_layers = sirfsoc_vdss_get_num_layers(d->lcdc_index);
		for (i = 0; i < d->num_layers; i++)
			d->layers[i] = sirfsoc_vdss_get_layer(d->lcdc_index, i);
		d->num_screens = sirfsoc_vdss_get_num_screens(d->lcdc_index);
		for (i = 0; i < d->num_screens; i++)
			d->screens[i] =
				sirfsoc_vdss_get_screen(d->lcdc_index, i);

		d->panel = panel;
	}

out:
	return 0;
err:
	return -EINVAL;

}

static int sirfsocfb_probe(struct platform_device *pdev)
{
	struct sirfsocfb_device *fbdev;
	int ret = 0;
	int i;
	struct sirfsoc_vdss_panel *panel;

	ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		goto err;

	if (!pdev->dev.dma_mask)
		pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;
	else
		dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));

	if (sirfsoc_vdss_is_initialized() == false)
		return -EPROBE_DEFER;

	fbdev = devm_kzalloc(&pdev->dev, sizeof(*fbdev),
		GFP_KERNEL);
	if (fbdev == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	mutex_init(&fbdev->mtx);
	fbdev->dev = &pdev->dev;
	platform_set_drvdata(pdev, fbdev);

	ret = sirfsocfb_init_displays(fbdev);
	if (ret) {
		dev_err(fbdev->dev, "failed to init display data\n");
		goto cleanup;
	}

	for (i = 0; i < fbdev->num_displays; i++) {
		panel = fbdev->displays[i].panel;
		ret = sirfsocfb_init_connections(fbdev, panel);
		if (ret) {
			dev_err(fbdev->dev,
				"failed to init connections for %s\n",
				panel->name);
			goto cleanup;
		}
	}

	ret = sirfsocfb_create_framebuffers(fbdev);
	if (ret)
		goto cleanup;

	for (i = 0; i < fbdev->num_displays; i++) {
		panel = fbdev->displays[i].panel;
		ret = sirfsocfb_init_panel(fbdev, panel);
		if (ret) {
			dev_err(fbdev->dev,
				"failed to initialize default panel\n");
			goto cleanup;
		}
	}

	return 0;

cleanup:
	sirfsocfb_free_resources(fbdev);
err:
	dev_err(&pdev->dev, "failed to setup sirfsocfb\n");
	return ret;
}

static int sirfsocfb_remove(struct platform_device *pdev)
{
	struct sirfsocfb_device *fbdev = platform_get_drvdata(pdev);

	sirfsocfb_free_resources(fbdev);

	return 0;
}

static struct platform_driver sirfsocfb_driver = {
	.driver = {
		.name = SIRFSOCFB_NAME,
		.owner = THIS_MODULE,
	},
	.probe = sirfsocfb_probe,
	.remove = sirfsocfb_remove,
};

static struct platform_device *sirfsocfb_dev;

static __init int sirfsocfb_init(void)
{
	int ret = 0;

	ret = platform_driver_register(&sirfsocfb_driver);

	if (!ret) {
		sirfsocfb_dev = platform_device_alloc(SIRFSOCFB_NAME, 0);

		if (sirfsocfb_dev) {
			ret = platform_device_add(sirfsocfb_dev);
			if (ret)
				goto init_err1;
		} else {
			ret = -ENOMEM;
			goto init_err2;
		}

	}

	return ret;

init_err1:
	platform_device_put(sirfsocfb_dev);
init_err2:
	platform_driver_unregister(&sirfsocfb_driver);
	return ret;
}

static __exit void sirfsocfb_exit(void)
{
	platform_device_unregister(sirfsocfb_dev);
	platform_driver_unregister(&sirfsocfb_driver);
}

subsys_initcall(sirfsocfb_init);
module_exit(sirfsocfb_exit);

MODULE_DESCRIPTION("SiRF Soc fbdev driver");
MODULE_AUTHOR("Jiansong Chen<Jiansong.Chen@csr.com>");
MODULE_LICENSE("GPL");
