/*
 * CSR sirfsoc framebuffer driver
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/workqueue.h>
#include <linux/init.h>
#include <linux/async.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_fdt.h>
#include <linux/pinctrl/consumer.h>
#include <linux/memblock.h>
#include <linux/reset.h>
#include <linux/uaccess.h>
#include <asm/cacheflush.h>
#include <video/sirfsoc_fb.h>

#include "vdss_lcdc.h"
#include "vdss_vpp.h"

#ifdef SUPPORT_BLE
#include "ble_defs.h"
#endif

#include "sirfsoc_clcdc.h"

#if 0
#undef FB_FUN_MSG
#define FB_FUN_MSG FB_DBG_MSG
#endif

#define SIRFSOC_ENABLE_ALPHA_BLENDING		1

#define SIRFSOC_OVERLAY_ENABLE_CACHE            0
#if SIRFSOC_OVERLAY_ENABLE_CACHE
#define SIRFSOC_OVERLAY_INDEX                   1
#endif

#define TIMEOUT_COUNT                           5

#ifndef MODULE
#ifdef MODULE_PARAM_PREFIX
#undef MODULE_PARAM_PREFIX
#endif
#define MODULE_PARAM_PREFIX
#endif

static uint bpp = 32;
module_param(bpp, uint, S_IRUGO);
MODULE_PARM_DESC(bpp, "LCD panel default bits per pixel.");

static uint toplayer = LCDC_PRIMARY;
module_param(toplayer, uint, S_IRUGO);
MODULE_PARM_DESC(toplayer, "LCD panel default top layer.");

#define REARVIEW_LAYER LCDC_OVERLAY_3

static u32 sirfsocfb_pseudo_palette[16];

static struct i2c_client *lcd_client;
static int vcc;
static int vdd;
static int vee;
static int vcc_gpio;
static int vdd_gpio;
static int vee_gpio;

/************** DEBUG DATA THROUGH SYSFS **************/
static ssize_t layer_uflow_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int ret;
	struct sirfsocfb *fb = dev_get_drvdata(dev);
	FB_FUN_MSG("layer_uflow_show\n");

	ret = sprintf(buf,
		"FIFO underflow errors:\nLayer0 = %d\nLayer1 = %d\nLayer2 = %d\nLayer3 = %d\n",
		fb->layer_info[0].fifo_underflow,
		fb->layer_info[1].fifo_underflow,
		fb->layer_info[2].fifo_underflow,
		fb->layer_info[3].fifo_underflow);
	return ret;
}

static DEVICE_ATTR(layer_fifo_underflow, S_IRUGO, layer_uflow_show, NULL);

static ssize_t layer_oflow_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int ret;
	struct sirfsocfb *fb = dev_get_drvdata(dev);
	FB_FUN_MSG("layer_oflow_show\n");

	ret = sprintf(buf,
		"FIFO overflow errors:\nLayer0 = %d\nLayer1 = %d\nLayer2 = %d\nLayer3 = %d\n",
		fb->layer_info[0].fifo_overflow,
		fb->layer_info[1].fifo_overflow,
		fb->layer_info[2].fifo_overflow,
		fb->layer_info[3].fifo_overflow);
	return ret;
}

static DEVICE_ATTR(layer_fifo_overflow, S_IRUGO, layer_oflow_show, NULL);

static ssize_t vsync_timestamp_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int ret;
	struct sirfsocfb *fb = dev_get_drvdata(dev);
	FB_FUN_MSG("vsync_timestamp_show\n");

	ret = scnprintf(buf, PAGE_SIZE, "%llu\n",
		ktime_to_ns(fb->vsync_timestamp));
	return ret;
}

static DEVICE_ATTR(vsync_timestamp, S_IRUGO, vsync_timestamp_show, NULL);

/**************** HELPER FUNCTIONS ****************/
static inline int sirfsocfb_get_layer(struct fb_info *info)
{
	return info->fix.id[10] - '0';
}

/* Gracefully shutdown a layer if it is already running */
static void layer_disable(struct sirfsocfb *fb, int layer)
{
	struct sirfsocfb_overlay *ovl = fb->layer_info[layer].ovl;

	if (fb->layer_info[layer].enabled != 0) {
		fb->lcdc_ops.hide_overlay(layer);

		if (ovl) {
			int i;
			for (i = 0; i < ovl->num_buffers; i++)
				ovl->pflips[i].wait = 0;
			ovl->num_dq =
			    (ovl->dq_idx >= ovl->q_idx) ?
			    (ovl->num_buffers - (ovl->dq_idx - ovl->q_idx))
			    : (ovl->q_idx - ovl->dq_idx);
			ovl->num_q = 0;
			ovl->flip_idx = ovl->q_idx;
		}

		/* Mark the layer as disabled */
		fb->layer_info[layer].enabled = 0;
	}
}

static void layer_enable(struct sirfsocfb *fb, int layer)
{
	if (fb->layer_info[layer].enabled == 0) {
		fb->lcdc_ops.show_overlay(layer);

		/* Mark the layer as enabled */
		fb->layer_info[layer].enabled = 1;
	}
}

static int set_bitfields(struct fb_var_screeninfo *var)
{
	switch (var->bits_per_pixel) {
	case 16:
		var->transp.offset = 0;
		var->transp.length = 0;
		var->red.offset = 11;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 6;
		var->blue.offset = 0;
		var->blue.length = 5;
		var->accel_flags = 0;
		break;
	case 32:
#if SIRFSOC_ENABLE_ALPHA_BLENDING
		/* by default transp is kept enable
		 * if application needs disable transp it can
		 * set via ioctl SET_VSCREENINFO*/
		var->transp.offset = 24;
		var->transp.length = 8;
#endif
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->accel_flags = 0;
		break;
	default:
		FB_ERR_MSG("BPP %d not supported\n", var->bits_per_pixel);
		return -EINVAL;
	}

	return 0;
}

static inline void wait_once(struct sirfsocfb *fb, int layer)
{
	FB_FUN_MSG("wait_once\n");

	fb->layer_info[layer].waiting_to_pan = WAIT_PAN;
	if (wait_for_completion_timeout
	    (&fb->layer_info[layer].done, msecs_to_jiffies(100)) == 0) {
		FB_ERR_MSG("Panning timed out\n");
	}
}

#define LARGER(A, B) \
	((((A) & 0xc0000000) == 0x00000000) && (((B) & 0xc0000000) == \
	0xc0000000) ? 1 : (A > B))

static void wait_vsync(struct sirfsocfb *fb, int layer, u32 v_count)
{
	/* wait until v_count < current vsync_count */
	struct sirfsocfb_overlay *ovl = fb->layer_info[layer].ovl;
	u32 current_count;
	FB_FUN_MSG("wait_vsync\n");

	if (!ovl)
		return;

	current_count = ovl->vsync_count;
	while (!LARGER(current_count, v_count)) {
		wait_once(fb, layer);
		current_count = ovl->vsync_count;
	}
}

static void insert_flip_item(struct sirfsocfb *fb, int layer, int bufidx)
{
	struct sirfsocfb_overlay *ovl = fb->layer_info[layer].ovl;
	struct sirfsocfb_flipitem *flip;
	u32 base;
	unsigned long flags;
	int q_idx;

	if (!ovl)
		return;

	base = (bufidx * ovl->hstride_byte) + fb->fb[layer].fix.smem_start;

	spin_lock_irqsave(&fb->lock, flags);
	FB_ASSERT(((ovl->dq_idx - ovl->flip_idx) ==
		   ovl->num_buffers - ovl->num_dq)
		  || ((ovl->dq_idx - ovl->flip_idx)
		      == -ovl->num_dq));

	q_idx = ovl->q_idx;
	ovl->q_idx++;
	if (ovl->q_idx == ovl->num_buffers)
		ovl->q_idx = 0;

	flip = &(ovl->pflips[q_idx]);
	flip->base = base;
	flip->bufidx = bufidx;

	ovl->num_q++;
	spin_unlock_irqrestore(&fb->lock, flags);
}

static void execute_flip_item(struct sirfsocfb *fb, int layer)
{
	struct sirfsocfb_overlay *ovl = fb->layer_info[layer].ovl;
	struct sirfsocfb_flipitem *flip;
	int flip_idx, prev_idx;
	unsigned long flags;

	if (!ovl)
		return;

	spin_lock_irqsave(&fb->lock, flags);
	if (ovl->num_q > 0) {
		FB_ASSERT(((ovl->q_idx - ovl->flip_idx) ==
			   ovl->num_q)
			  || (ovl->q_idx + ovl->num_buffers -
			      ovl->flip_idx) == ovl->num_q);
		ovl->num_q--;
		flip_idx = ovl->flip_idx;
		ovl->flip_idx++;
		if (ovl->flip_idx == ovl->num_buffers)
			ovl->flip_idx = 0;

		flip = &(ovl->pflips[flip_idx]);
		fb->lcdc_ops.flip_overlay(layer, flip->base, LCDC_FLIP_FRAME);
		flip->wait = fb->layer_info[layer].enabled;

		if (flip_idx == 0)
			prev_idx = ovl->num_buffers - 1;
		else
			prev_idx = flip_idx - 1;

		/* Base of last flip will be invalid after vsync_count */
		ovl->pflips[prev_idx].vsync_count = ovl->vsync_count;
		ovl->prev_count = ovl->vsync_count;

		ovl->num_dq++;
		spin_unlock_irqrestore(&fb->lock, flags);
	} else {
		spin_unlock_irqrestore(&fb->lock, flags);
	}
}

static void layer_frame_irq(struct work_struct *data)
{
	struct sirfsocfb *fb;
	int layer;
	struct sirfsocfb_overlay *ovl;

	fb = container_of(data, struct sirfsocfb, work);

	for (layer = 0; layer < SIRFSOCFB_MAX_LAYERS; layer++) {
		if (!fb->layer_info[layer].enabled)
			continue;

		ovl = fb->layer_info[layer].ovl;
		if (ovl) {
			ovl->vsync_count++;
			if (fb->layer_info[layer].waiting_to_pan) {
				fb->layer_info[layer].waiting_to_pan = DONT_PAN;
				complete(&fb->layer_info[layer].done);
			}

			if (ovl->num_q > 0)
				execute_flip_item(fb, layer);
		} else {
			if (fb->layer_info[layer].waiting_to_pan) {
				fb->layer_info[layer].waiting_to_pan = DONT_PAN;
				complete(&fb->layer_info[layer].done);
			}
		}
	}
}

static void send_vsync_timestamp(struct work_struct *data)
{
	struct sirfsocfb *fb;
	struct device *dev;
	fb = container_of(data, struct sirfsocfb, vsync_work);
	dev = &fb->dev->dev;

	sysfs_notify(&dev->kobj, NULL, "vsync_timestamp");
}

static int set_par(struct fb_info *info)
{
	struct sirfsocfb *fb = (struct sirfsocfb *)info->par;
	int layer = sirfsocfb_get_layer(info);
	struct lcdc_parms set_parms, get_parms;
	int ret = 0;
	unsigned int byte_offset =
		(info->var.yoffset * info->var.xres_virtual +
		 info->var.xoffset) * (info->var.bits_per_pixel >> 3);

	FB_FUN_MSG("set_par\n");

	memset(&set_parms, 0, sizeof(set_parms));
	memset(&get_parms, 0, sizeof(get_parms));

	set_parms.layer = layer;
	set_parms.base = info->fix.smem_start + byte_offset;

	if (info->var.bits_per_pixel == 16)
		set_parms.fmt = VDSS_PIXELFORMAT_565;
	else if (info->var.transp.length)
		set_parms.fmt = VDSS_PIXELFORMAT_8888;
	else
		set_parms.fmt = VDSS_PIXELFORMAT_BGRX_8880;

	if (set_parms.fmt == VDSS_PIXELFORMAT_8888)
		set_parms.src_alpha_enabled = 1;
	else
		set_parms.src_alpha_enabled = 0;

	set_parms.surf_width = info->var.xres_virtual;
	set_parms.surf_height = info->var.yres_virtual;
	set_parms.src_rect.left = 0;
	set_parms.src_rect.right = info->var.xres;
	set_parms.src_rect.top = 0;
	set_parms.src_rect.bottom = info->var.yres;

	get_parms.layer = layer;
	fb->lcdc_ops.get_parameters(&get_parms);

	if ((get_parms.base == set_parms.base) &&
	    (get_parms.fmt == set_parms.fmt) &&
	    (get_parms.surf_width == set_parms.surf_width) &&
	    (get_parms.surf_height == set_parms.surf_height) &&
	    ((get_parms.src_rect.right - get_parms.src_rect.left) ==
	     (set_parms.src_rect.right - set_parms.src_rect.left)) &&
	    ((get_parms.src_rect.bottom - get_parms.src_rect.top) ==
	     (set_parms.src_rect.bottom - set_parms.src_rect.top))) {
		fb->lcdc_ops.flip_overlay(layer, set_parms.base,
						LCDC_FLIP_FRAME);
	} else {
		get_parms.base = set_parms.base;
		get_parms.fmt = set_parms.fmt;
		get_parms.surf_width = set_parms.surf_width;
		get_parms.surf_height = set_parms.surf_height;
		get_parms.src_rect = set_parms.src_rect;
		get_parms.src_alpha_enabled = set_parms.src_alpha_enabled;
		get_parms.pre_alpha_enabled = 1;

		/* no change for dst_rect.left & dst_rect.top */
		get_parms.dst_rect.right =
		    get_parms.dst_rect.left + info->var.xres;
		get_parms.dst_rect.bottom =
		    get_parms.dst_rect.top + info->var.yres;

		set_bitfields(&info->var);

		if (!fb->lcdc_ops.set_parameters(&get_parms))
			ret = -EINVAL;
	}

	info->fix.line_length = info->var.xres_virtual *
		SIRFSOCFB_BYTES(info->var.bits_per_pixel);

	return ret;
}

/**************** IOCTL OPERATIONS ****************/

static void sirfsocfb_layer_disable(struct sirfsocfb *fb, int layer)
{
	FB_FUN_MSG("sirfsocfb_layer_disable\n");
	mutex_lock(&fb->layer_info[layer].layer_lock);
	layer_disable(fb, layer);
	mutex_unlock(&fb->layer_info[layer].layer_lock);
}

static void sirfsocfb_layer_enable(struct sirfsocfb *fb, int layer)
{
	FB_FUN_MSG("sirfsocfb_layer_enable\n");
	mutex_lock(&fb->layer_info[layer].layer_lock);
	layer_enable(fb, layer);
	mutex_unlock(&fb->layer_info[layer].layer_lock);
}

static void sirfsocfb_set_toplayer(struct sirfsocfb *fb, int layer)
{
	FB_FUN_MSG("sirfsocfb_set_toplayer layer:%d\n", layer);

	if (layer >= SIRFSOCFB_MAX_LAYERS) {
		FB_ERR_MSG("Bad layer %d\n", layer);
		return;
	}
	fb->lcdc_ops.set_toplayer(layer);
}

static void sirfsocfb_set_alpha(struct sirfsocfb *fb, int layer,
				unsigned long alpha_val)
{
	unsigned long flags;
	FB_FUN_MSG("sirfsocfb_set_alpha\n");

	spin_lock_irqsave(&fb->lock, flags);
	fb->lcdc_ops.set_global_alpha(layer, alpha_val);
	spin_unlock_irqrestore(&fb->lock, flags);

	fb->layer_info[layer].alpha = alpha_val;
}

static void sirfsocfb_set_ckey(struct sirfsocfb *fb, int layer)
{
	unsigned long flags;
	struct sirfsocfb_colorkeys *pckey = &(fb->layer_info[layer].ckey);
	FB_FUN_MSG("sirfsocfb_set_ckey\n");

	spin_lock_irqsave(&fb->lock, flags);
	fb->lcdc_ops.set_src_ckey(layer,
				   pckey->enable,
				   pckey->color_key_big,
				   pckey->color_key_small);
	spin_unlock_irqrestore(&fb->lock, flags);
}

static int sirfsocfb_layer_setsize(struct sirfsocfb *fb, int layer,
				   struct sirfsocfb_screen *scr)
{
	int ret = 0;
	struct sirfsocfb_overlay *ovl = fb->layer_info[layer].ovl;
	int invalid;

	FB_FUN_MSG("+sirfsocfb_layer_setsize x:%d y:%d w:%d h:%d\n",
		   scr->xstart, scr->ystart, scr->xsize, scr->ysize);

	mutex_lock(&fb->ovl_lock);
	if ((scr->xstart < 0) || (scr->xstart >= fb->panel->mode.xres)
	    || (scr->xsize <= 0) || (scr->xsize > fb->panel->mode.xres)
	    || (scr->ystart < 0) || (scr->ystart >= fb->panel->mode.yres)
	    || (scr->ysize <= 0) || (scr->ysize > fb->panel->mode.yres)) {
		invalid = 1;
	} else {
		invalid = 0;
	}

	fb->layer_info[layer].valid_pos = invalid ? 0 : 1;

	if (!ovl && invalid) {
		FB_ERR_MSG("Set invalid position\n");
		FB_FUN_MSG("-sirfsocfb_layer_setsize");
		mutex_unlock(&fb->ovl_lock);
		return -EINVAL;
	} else {
		struct vdss_rect dst_rect;
		/* backup overlay position for only surface flinger knows where
		 * the overlay surface located at */
		fb->ovl_pos.x = scr->xstart;
		fb->ovl_pos.y = scr->ystart;
		fb->ovl_pos.w = scr->xsize;
		fb->ovl_pos.h = scr->ysize;

		dst_rect.left = scr->xstart;
		dst_rect.right = scr->xstart + scr->xsize;
		dst_rect.top = scr->ystart;
		dst_rect.bottom = scr->ystart + scr->ysize;

		mutex_lock(&fb->layer_info[layer].layer_lock);

		fb->lcdc_ops.set_overlay_pos(layer, NULL, &dst_rect);
		if (ovl)
			if (!invalid && !fb->layer_info[layer].enabled &&
				fb->layer_info[layer].queued)
				layer_enable(fb, layer);

		mutex_unlock(&fb->layer_info[layer].layer_lock);
	}

	FB_FUN_MSG("-sirfsocfb_layer_setsize\n");

	mutex_unlock(&fb->ovl_lock);
	return ret;
}

static void sirfsocfb_layer_getsize(struct sirfsocfb *fb, int layer,
				    struct sirfsocfb_screen *scr)
{

	FB_FUN_MSG("sirfsocfb_layer_getsize\n");

	mutex_lock(&fb->ovl_lock);

	scr->xstart = fb->ovl_pos.x;
	scr->ystart = fb->ovl_pos.y;
	scr->xsize = fb->ovl_pos.w;
	scr->ysize = fb->ovl_pos.h;
	mutex_unlock(&fb->ovl_lock);
}

static int sirfsocfb_set_dmasize(struct sirfsocfb *fb, int layer,
				 struct sirfsocfb_screen *scr)
{
	struct vdss_rect src_rect;

	FB_FUN_MSG("+sirfsocfb_layer_setdma x:%d y:%d w:%d h:%d\n", scr->xstart,
		   scr->ystart, scr->xsize, scr->ysize);

	mutex_lock(&fb->layer_info[layer].layer_lock);
	src_rect.left = scr->xstart;
	src_rect.right = scr->xstart + scr->xsize;
	src_rect.top = scr->ystart;
	src_rect.bottom = scr->ystart + scr->ysize;

	fb->lcdc_ops.set_overlay_pos(layer, &src_rect, NULL);

	mutex_unlock(&fb->layer_info[layer].layer_lock);
	FB_FUN_MSG("-sirfsocfb_layer_setdma\n");
	return 0;
}

static void sirfsocfb_get_dmasize(struct sirfsocfb *fb, int layer,
				  struct sirfsocfb_screen *scr)
{
	struct lcdc_parms parms;
	FB_FUN_MSG("sirfsocfb_layer_getdma\n");

	parms.layer = layer;

	fb->lcdc_ops.get_parameters(&parms);
	scr->xsize = parms.src_rect.right - parms.src_rect.left;
	scr->ysize = parms.src_rect.bottom - parms.src_rect.top;
	scr->xstart = parms.src_rect.left;
	scr->ystart = parms.src_rect.top;
}

static int sirfsocfb_create_overlay(struct sirfsocfb *fb, int layer,
				    struct sirfsocfb_createlayer *create)
{
	int bits_per_pixel = 0, i;
	struct sirfsocfb_overlay *ovl;
	struct lcdc_overlay data;

	data.layer = layer;

	switch (create->format) {
	case FORMAT_RGB_565:
		bits_per_pixel = 16;
		data.fmt = VDSS_PIXELFORMAT_565;
		break;
	case FORMAT_BGRA_8888:
	case FORMAT_BGRX_8888:
		bits_per_pixel = 32;
		data.fmt = VDSS_PIXELFORMAT_8888;
		break;
	case FORMAT_YCbCr_420_P:
		bits_per_pixel = 12;
		data.fmt = VDSS_PIXELFORMAT_I420;
		break;
	case FORMAT_YCbYCr_422_I:
		bits_per_pixel = 16;
		data.fmt = VDSS_PIXELFORMAT_YUYV;
		break;
	case FORMAT_CrYCbY_422_I:
		bits_per_pixel = 16;
		data.fmt = VDSS_PIXELFORMAT_VYUY;
		break;
	case FORMAT_YCbCr_420_SP:
		bits_per_pixel = 12;
		data.fmt = VDSS_PIXELFORMAT_NV12;
		break;
	case FORMAT_YCrCb_420_SP:
		bits_per_pixel = 12;
		data.fmt = VDSS_PIXELFORMAT_NV21;
		break;
	case FORMAT_YCbCr_422_SP:
	case FORMAT_RGBA_8888:
	case FORMAT_RGBX_8888:
		FB_ERR_MSG("Unsupported format!\n");
		return -EINVAL;
	}

	data.width = create->width;
	data.height = create->height;

	fb->lcdc_ops.alloc_overlay(&data);
	create->wstride_byte = data.wstride_byte;
	create->hstride_byte = data.hstride_byte;
	create->wstride_pixel = data.wstride_pixel;
	create->hstride_pixel = data.hstride_pixel;

	create->num_buffers = fb->fb[layer].fix.smem_len / create->hstride_byte;

	ovl = kzalloc(sizeof(struct sirfsocfb_overlay) +
			   (sizeof(struct sirfsocfb_flipitem) *
			    create->num_buffers), GFP_KERNEL);

	ovl->pflips = (struct sirfsocfb_flipitem *)
	    (((unsigned char *)ovl) + sizeof(struct sirfsocfb_overlay));

	ovl->num_dq = ovl->num_buffers = create->num_buffers;
	ovl->hstride_byte = create->hstride_byte;

	for (i = 0; i < create->num_buffers; i++)
		ovl->pflips[i].bufidx = i;

	fb->layer_info[layer].ovl = ovl;
	fb->layer_info[layer].queued = 0;
	fb->layer_info[layer].valid_pos = 0;

	return 0;
}

static void sirfsocfb_dq_buffer(struct sirfsocfb *fb, int layer, int *bufidx)
{
	struct sirfsocfb_overlay *ovl = fb->layer_info[layer].ovl;
	int dq_idx;
	unsigned long flags;
	int wait_count = 0;

	if (!ovl) {
		*bufidx = -1;
		return;
	}

	/* Avoid to dequeue front buffer */
	while (ovl->num_dq <= 1) {
		FB_DBG_MSG("dq_buffer: waiting...\n");
		wait_count++;
		if (wait_count >= 5) {
			FB_ERR_MSG("dq_buffer: wait_count>=5 Exit...\n");
			*bufidx = -1;
			return;
		}
		msleep(20);
	}

	spin_lock_irqsave(&fb->lock, flags);
	FB_ASSERT(ovl->num_dq > 1);
	ovl->num_dq--;
	dq_idx = ovl->dq_idx;
	ovl->dq_idx++;
	if (ovl->dq_idx == ovl->num_buffers)
		ovl->dq_idx = 0;

	*bufidx = ovl->pflips[dq_idx].bufidx;
	spin_unlock_irqrestore(&fb->lock, flags);

	if (ovl->pflips[dq_idx].wait)
		wait_vsync(fb, layer, ovl->pflips[dq_idx].vsync_count);

	return;
}

static void sirfsocfb_destroy_overlay(struct sirfsocfb *fb, int layer)
{
	struct sirfsocfb_overlay *ovl = fb->layer_info[layer].ovl;
	struct layer_info *info = &fb->layer_info[layer];

	mutex_lock(&info->layer_lock);
	layer_disable(fb, layer);
	mutex_unlock(&info->layer_lock);
	fb->lcdc_ops.free_overlay(layer);
	kfree(ovl);
	fb->layer_info[layer].ovl = NULL;
}

static void sirfsocfb_q_buffer(struct sirfsocfb *fb, int layer, int bufidx)
{

	struct sirfsocfb_overlay *ovl = fb->layer_info[layer].ovl;
	struct layer_info *info = &fb->layer_info[layer];

	u32 vsync_count;

	if (!ovl)
		return;

	mutex_lock(&info->layer_lock);
	insert_flip_item(fb, layer, bufidx);
	vsync_count = ovl->vsync_count;

	if (fb->layer_info[layer].enabled) {
		if ((ovl->num_q == 1)
		    && LARGER(vsync_count, ovl->prev_count)) {
			execute_flip_item(fb, layer);
		}
	} else {
		execute_flip_item(fb, layer);
		fb->layer_info[layer].queued = 1;
		if (fb->layer_info[layer].valid_pos)
			layer_enable(fb, layer);
	}
	mutex_unlock(&info->layer_lock);
}

static void sirfsocfb_flush_cache(struct sirfsocfb *fb, int layer,
				  struct sirfsocfb_flush_cache_addr
				  *flush_cache_addr)
{
	struct platform_device *pdev = fb->dev;
	unsigned long addr_start, addr_end;

	/* calculate physical address */
	addr_start = flush_cache_addr->phy_addr_start;
	addr_end = addr_start + flush_cache_addr->phy_addr_size;

	switch (flush_cache_addr->flush_cache_op) {
	case FLUSH_CACHE_OP_INVALID:
		/* invalid L1 and L2 cache */
		dma_sync_single_for_cpu(&pdev->dev, addr_start,
			flush_cache_addr->phy_addr_size, DMA_FROM_DEVICE);
		break;
	case FLUSH_CACHE_OP_CLEAN:
		/* clean L1 and L2 cache */
		dma_sync_single_for_device(&pdev->dev, addr_start,
			flush_cache_addr->phy_addr_size, DMA_TO_DEVICE);
		break;
	case FLUSH_CACHE_OP_FLUSH:
		/* flush L1 and L2 cache */
		dma_sync_single_for_device(&pdev->dev, addr_start,
			flush_cache_addr->phy_addr_size, DMA_TO_DEVICE);
		dma_sync_single_for_cpu(&pdev->dev, addr_start,
			flush_cache_addr->phy_addr_size, DMA_FROM_DEVICE);
		break;
	default:
		break;
	}

}

static void sirfsocfb_set_layers(struct sirfsocfb *fb,
				struct sirfsocfb_layers_parms *param)
{
	struct lcdc_parms set_parms;
	struct layer_info *info = &fb->layer_info[0];
	int i, index = 0;

	mutex_lock(&info->layer_lock);

	for (i = 0; i < SIRFSOCFB_MAX_LAYERS; i++) {
		if (!(param->layer_mask & (1 << i))) {
			if (param->phys_addr[i] != 0) {
				fb->lcdc_ops.flip_overlay(i,
				param->phys_addr[i], LCDC_FLIP_FRAME);
			}
			continue;
		}
		memset(&set_parms, 0, sizeof(set_parms));
		set_parms.layer = i;
		if (param->layer_info[index].enable) {
			if (param->phys_addr[i])
				set_parms.base = param->phys_addr[i];
			if (param->layer_info[index].format == FORMAT_RGB_565) {
				set_parms.fmt = VDSS_PIXELFORMAT_565;
			} else if (param->layer_info[index].format ==
				FORMAT_BGRA_8888) {
				set_parms.fmt = VDSS_PIXELFORMAT_8888;
			} else if (param->layer_info[index].format ==
				FORMAT_BGRX_8888) {
				set_parms.fmt = VDSS_PIXELFORMAT_BGRX_8880;
			} else if (param->layer_info[index].format ==
				FORMAT_YCbCr_420_P) {
				set_parms.fmt = VDSS_PIXELFORMAT_I420;
			} else {
				FB_ERR_MSG("Unsupported format!\n");
				mutex_unlock(&info->layer_lock);
				return;
			}
			set_parms.surf_width =
				param->layer_info[index].width;
			set_parms.surf_height =
				param->layer_info[index].height;
			set_parms.src_rect.left =
				param->layer_info[index].src_rect.left;
			set_parms.src_rect.top =
				param->layer_info[index].src_rect.top;
			set_parms.src_rect.right =
				param->layer_info[index].src_rect.right;
			set_parms.src_rect.bottom =
				param->layer_info[index].src_rect.bottom;
			set_parms.dst_rect.left =
				param->layer_info[index].dst_rect.left;
			set_parms.dst_rect.top =
				param->layer_info[index].dst_rect.top;
			set_parms.dst_rect.right =
				param->layer_info[index].dst_rect.right;
			set_parms.dst_rect.bottom =
				param->layer_info[index].dst_rect.bottom;

			set_parms.pre_alpha_enabled = 1;
			if (set_parms.fmt == VDSS_PIXELFORMAT_8888)
				set_parms.src_alpha_enabled = 1;

			if (!fb->lcdc_ops.set_parameters(&set_parms)) {
				FB_ERR_MSG("Set parameters failed!\n");
				mutex_unlock(&info->layer_lock);
				return;
			}

			fb->lcdc_ops.show_overlay(i);
		} else {
			fb->lcdc_ops.hide_overlay(i);
		}
		index++;
	}

	if (param->wait) {
		if (fb->layer_info[0].enabled)
			wait_once(fb, 0);
	}
	mutex_unlock(&info->layer_lock);
}

int sirfsocfb_enable_feature_layer(struct sirfsocfb *fb, int layer,
	enum sirfsocfb_feature_layer feature)
{
	if (feature == REARVIEW_FEATURE_LAYER) {
		fb->record_toplayer = fb->lcdc_ops.get_toplayer();
		layer_enable(fb, layer);
		sirfsocfb_set_toplayer(fb, layer);
		fb->layer_info[layer].feature = REARVIEW_FEATURE_LAYER;
	} else {
		FB_ERR_MSG("feature %d is not among the supported\n", feature);
		return -EINVAL;
	}

	return 0;
}

int sirfsocfb_disable_feature_layer(struct sirfsocfb *fb, int layer)
{
	if (fb->layer_info[layer].feature == REARVIEW_FEATURE_LAYER) {
		sirfsocfb_set_toplayer(fb, fb->record_toplayer);
		layer_disable(fb, layer);
		fb->layer_info[layer].feature = NORMAL_LAYER;
	} else {
		FB_ERR_MSG("layer %d has feature %d which is not supported\n",
			layer, fb->layer_info[layer].feature);
		return -EINVAL;
	}

	return 0;
}

static int sirfsocfb_set_gamma_table(struct sirfsocfb *fb,
	int layer, unsigned short *lut)
{
	fb->lcdc_ops.set_gamma_ramp(lut);

	return 0;
}

static int sirfsocfb_get_gamma_table(struct sirfsocfb *fb,
	int layer, unsigned short *lut)
{
	fb->lcdc_ops.get_gamma_ramp(lut);

	return 0;
}

#define ALIGN_SIZE(size, align) ((size + align - 1) & ~(align - 1))

static int __get_lcd_fmt(int fmt, int *is_yuv)
{
	int lcd_fmt = VDSS_PIXELFORMAT_UNKNOWN;

	*is_yuv = 0;
	switch (fmt) {
	case FORMAT_RGB_565:
		lcd_fmt = VDSS_PIXELFORMAT_565;
		break;
	case FORMAT_BGRA_8888:
	case FORMAT_BGRX_8888:
		/* for vpp blt purpose */
		lcd_fmt = VDSS_PIXELFORMAT_BGRX_8880;
		break;
	case FORMAT_RGBA_8888:
	case FORMAT_RGBX_8888:
		lcd_fmt = VDSS_PIXELFORMAT_RGBX_8880;
		break;
	case FORMAT_YCbCr_420_P:
		lcd_fmt = VDSS_PIXELFORMAT_I420;
		*is_yuv = 1;
		break;
	case FORMAT_YCbYCr_422_I:
		lcd_fmt = VDSS_PIXELFORMAT_YUYV;
		*is_yuv = 1;
		break;
	case FORMAT_CrYCbY_422_I:
		lcd_fmt = VDSS_PIXELFORMAT_VYUY;
		*is_yuv = 1;
		break;
	case FORMAT_YCbCr_420_SP:
		lcd_fmt = VDSS_PIXELFORMAT_NV12;
		*is_yuv = 1;
		break;
	case FORMAT_YCrCb_420_SP:
		lcd_fmt = VDSS_PIXELFORMAT_NV21;
		*is_yuv = 1;
		break;
	case FORMAT_YCbCr_422_SP:
		FB_ERR_MSG("Unsupported format!\n");
		break;
	}

	return lcd_fmt;
}

static int __calculate_surf_layout(int lcd_fmt, int width, int height,
	int *wstride_pixel, int *hstride_pixel)
{

	int wstride_byte, hstride_byte;

	switch (lcd_fmt) {
	case VDSS_PIXELFORMAT_565:
	case VDSS_PIXELFORMAT_556:
	case VDSS_PIXELFORMAT_655:
		wstride_byte = ((2 * width + 7) / 8) * 8;
		hstride_byte = wstride_byte * height;
		*wstride_pixel = wstride_byte / 2;
		*hstride_pixel = hstride_byte / wstride_byte;
		break;
	case VDSS_PIXELFORMAT_RGBX_8880:
	case VDSS_PIXELFORMAT_8888:
	case VDSS_PIXELFORMAT_BGRX_8880:
		wstride_byte = ((4 * width + 7) / 8) * 8;
		hstride_byte = wstride_byte * height;
		*wstride_pixel = wstride_byte / 4;
		*hstride_pixel = hstride_byte / wstride_byte;
		break;
	case VDSS_PIXELFORMAT_NV12:
	case VDSS_PIXELFORMAT_NV21:
		*wstride_pixel = ALIGN_SIZE(width, 64);
		*hstride_pixel = ALIGN_SIZE(height, 64);
		break;
	case VDSS_PIXELFORMAT_I420:
		*wstride_pixel = ALIGN_SIZE(width, 16);
		*hstride_pixel = ALIGN_SIZE(height, 16);
		break;
	case VDSS_PIXELFORMAT_YV12:
	case VDSS_PIXELFORMAT_YUYV:
	case VDSS_PIXELFORMAT_VYUY:
		wstride_byte = ALIGN_SIZE(width * 2, 8);
		hstride_byte = wstride_byte * height;
		*wstride_pixel = wstride_byte / 2;
		*hstride_pixel = height;
		break;
	default:
		FB_ERR_MSG("Unsupported format!\n");
		return -1;
	}

	return 0;
}

static int sirfsocfb_blt_yuv2rgb(struct sirfsocfb *fb, int layer,
				    struct sirfsocfb_bltparms *parms)
{
	struct vpp_parms vpp_parms;
	int is_yuv = 0;
	int ret;
	int wait_count = 0;
	int di_mode = 0;
	int input_top_first = 1;
	int field_offset = 0;

	/* rearview function ought to be the overriding priority */
	if (fb->layer_info[REARVIEW_LAYER].enabled && layer != REARVIEW_LAYER)
		return 0;

	memset((void *)&vpp_parms, 0, sizeof(vpp_parms));

	vpp_parms.src_fmt = __get_lcd_fmt(parms->src.fmt, &is_yuv);
	if (vpp_parms.src_fmt > 0) {
		if (!is_yuv) {
			FB_ERR_MSG("Src formt is not yuv!\n");
			return -EINVAL;
		}
	} else {
		FB_ERR_MSG("Unsupported src format!\n");
		return -EINVAL;
	}

	vpp_parms.dst_fmt = __get_lcd_fmt(parms->dst.fmt, &is_yuv);
	if (vpp_parms.dst_fmt > 0) {
		if (is_yuv) {
			FB_ERR_MSG("Dst formt is not rgb!\n");
			return -EINVAL;
		}
	} else {
		FB_ERR_MSG("Unsupported dst format!\n");
		return -EINVAL;
	}

	ret = __calculate_surf_layout(vpp_parms.src_fmt, parms->src.width,
		parms->src.height, &vpp_parms.src_wstride_pixel,
		&vpp_parms.src_hstride_pixel);

	if (ret != 0) {
		FB_ERR_MSG("Cannot calculate src surf layout!\n");
		return -EINVAL;
	}

	ret = __calculate_surf_layout(vpp_parms.dst_fmt, parms->dst.width,
		parms->dst.height, &vpp_parms.dst_wstride_pixel,
		&vpp_parms.dst_hstride_pixel);

	if (ret != 0) {
		FB_ERR_MSG("Cannot calculate dst surf layout!\n");
		return -EINVAL;
	}

	vpp_parms.src_base = parms->src.base;
	vpp_parms.dst_base = parms->dst.base;

	if (parms->flag & BLT_BOT_FIELD_FIRST)
		input_top_first = 0;

	di_mode = parms->flag & BLT_DI_MODE_MASK;

	if (parms->flag & BLT_FIELDS_MIX)
		field_offset = 0;
	else
		field_offset = parms->src.width * parms->src.height;

	switch (di_mode) {
	case BLT_DI_NONE:
		fb->vpp_ops->set_interlace(false, VPP_OUTPUT_P_SINGLE,
					true, true, VPP_DI_WEAVE,
					true, 0);
		break;
	case BLT_DI_WEAVE:
		fb->vpp_ops->set_interlace(true, VPP_OUTPUT_P_SINGLE,
					true, true, VPP_DI_WEAVE,
					input_top_first, field_offset);
		break;
	case BLT_DI_3MEDIAN:
		if (parms->flag & BLT_DOUBLE_FRATE)
			fb->vpp_ops->set_interlace(true, VPP_OUTPUT_P_DOUBLE,
				!input_top_first, input_top_first,
				VPP_DI_3MEDIAN, input_top_first, field_offset);
		else
			fb->vpp_ops->set_interlace(true, VPP_OUTPUT_P_SINGLE,
				true, true,
				VPP_DI_3MEDIAN, input_top_first, field_offset);
		break;
	case BLT_DI_VMRI:
		if (parms->flag & BLT_DOUBLE_FRATE)
			fb->vpp_ops->set_interlace(true, VPP_OUTPUT_P_DOUBLE,
				!input_top_first, input_top_first,
				VPP_DI_VMRI, input_top_first, field_offset);
		else
			fb->vpp_ops->set_interlace(true, VPP_OUTPUT_P_SINGLE,
				true, true,
				VPP_DI_VMRI, input_top_first, field_offset);
		break;
	case BLT_DI_INTRA_FIELD_SPATIAL:
	default:
		FB_ERR_MSG("Unsupported deinterlace mode!\n");
		return -EINVAL;

	}

	fb->vpp_ops->set_params(&vpp_parms);
	fb->vpp_ops->set_size((struct vdss_rect *)&parms->src.rect,
				(struct vdss_rect *)&parms->dst.rect);
	fb->vpp_ops->start(false);

	if (!(parms->flag & BLT_NOT_WAIT_COMPLETE))
		while (fb->vpp_ops->is_busy()) {
			FB_DBG_MSG("vpp blt: waiting...\n");
			wait_count++;
			if (wait_count >= 5) {
				FB_ERR_MSG("vpp blt: wait_count>=5 Exit...\n");
				return -EINVAL;
			}
			usleep_range(4000, 6000);
		}

	return 0;
}

#ifdef SUPPORT_BLE

#define MEM_INFO_ARRAY_SIZE 5
static struct ble_meminfo mem_src_info[MEM_INFO_ARRAY_SIZE];
static struct ble_meminfo mem_dst_info[MEM_INFO_ARRAY_SIZE];
static int cur_mem_info = -1;

static int sirfsocfb_blt_ble(struct sirfsocfb *fb, int layer,
	struct sirfsocfb_bltparms_ble *parms)
{
	struct ble_bltinfo blt_info;

	cur_mem_info++;

	if (cur_mem_info >= MEM_INFO_ARRAY_SIZE)
		cur_mem_info = 0;

	memset(&blt_info, 0, sizeof(blt_info));
	blt_info.rop3 = parms->rop3;
	blt_info.fill_color = parms->fill_color;
	blt_info.colorkey = parms->color_key;
	blt_info.global_alpha = parms->global_alpha;
	blt_info.blendfunc = parms->blend_func;
	blt_info.num_cliprect = parms->num_rects;
	blt_info.ble_cliprect = (struct ble_rect *)parms->rects;
	blt_info.blt_flags = (parms->flags & ~BLE_BLT_WAIT_COMPLETE);

	blt_info.dmeminfo = &mem_dst_info[cur_mem_info];
	blt_info.dmeminfo->offset = parms->dst_offset;
	blt_info.dst_stride = parms->dst_stride;
	blt_info.dstx = parms->dstx;
	blt_info.dsty = parms->dsty;
	blt_info.dst_sizex = parms->dst_sizex;
	blt_info.dst_sizey = parms->dst_sizey;
	blt_info.dst_format = parms->dst_fmt;
	blt_info.dst_surfwidth = parms->dst_width;
	blt_info.dst_surfheight = parms->dst_height;

	blt_info.pat_exist = false;
	blt_info.src_exist = false;

	if (parms->src_offset > 0) {
		blt_info.smeminfo = &mem_src_info[cur_mem_info];
		blt_info.smeminfo->offset = parms->src_offset;
		blt_info.src_stride = parms->src_stride;
		blt_info.srcx = parms->srcx;
		blt_info.srcy = parms->srcy;
		blt_info.src_sizex = parms->src_sizex;
		blt_info.src_sizey = parms->src_sizey;
		blt_info.src_format = parms->src_fmt;
		blt_info.src_surfwidth = parms->src_width;
		blt_info.src_surfheight = parms->src_height;
		blt_info.src_exist = true;
	}

	if (parms->flags & BLE_BLT_WAIT_COMPLETE)
		blt_info.need_synclast = true;

	fb->ble_func.bitblt(fb->ble_context, &blt_info);

	return 0;
}

static int sirfsocfb_blt_ble_complete(struct sirfsocfb *fb, int layer,
	int wait)
{
	if (cur_mem_info == -1)
		return 0;
	else
		return fb->ble_func.query_status(fb->ble_context,
			&mem_dst_info[cur_mem_info], wait);
}

#endif

/**************** FRAMEBUFFER OPERATIONS ****************/
static int sirfsocfb_check_var(struct fb_var_screeninfo *var,
			       struct fb_info *info)
{
	FB_FUN_MSG("+sirfsocfb_check_var\n");

	if ((var->bits_per_pixel != 32) && (var->bits_per_pixel != 16)) {
		FB_ERR_MSG("Unsupported bits per pixel\n");
		return -EINVAL;
	}
	if (var->bits_per_pixel == 32) {
		if (var->transp.length && var->transp.offset != 24) {
			FB_ERR_MSG("Unsupported transperancy parameters\n");
			return -EINVAL;
		}
	}

	if (var->xres <= 0)
		return -EINVAL;
	if (var->yres <= 0)
		return -EINVAL;

	if ((var->bits_per_pixel != 16) && (var->bits_per_pixel != 32))
		return -EINVAL;

	return 0;
}

static int sirfsocfb_set_par(struct fb_info *info)
{
	struct sirfsocfb *fb = (struct sirfsocfb *)info->par;
	int layer = sirfsocfb_get_layer(info);
	int ret;

	FB_FUN_MSG("+sirfsocfb_set_par\n");
	mutex_lock(&fb->layer_info[layer].layer_lock);
	ret = set_par(info);
	mutex_unlock(&fb->layer_info[layer].layer_lock);
	FB_FUN_MSG("-sirfsocfb_set_par\n");
	return ret;
}

static int sirfsocfb_blank(int blank_mode, struct fb_info *info)
{
	struct sirfsocfb *fb = (struct sirfsocfb *)info->par;

	int layer = sirfsocfb_get_layer(info);
	FB_FUN_MSG("sirfsocfb_blank\n");

	switch (blank_mode) {
	case FB_BLANK_UNBLANK:
		sirfsocfb_layer_enable(fb, layer);
		break;
	case FB_BLANK_NORMAL:
	case FB_BLANK_HSYNC_SUSPEND:
	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_POWERDOWN:
		sirfsocfb_layer_disable(fb, layer);
		break;
	}
	return 0;
}

static int sirfsocfb_pan_display(struct fb_var_screeninfo *var,
				 struct fb_info *info)
{
	int ret = 0;
	struct sirfsocfb *fb = (struct sirfsocfb *)info->par;
	int layer = sirfsocfb_get_layer(info);
	unsigned int byte_offset =
		(var->yoffset * var->xres_virtual + var->xoffset) *
		(var->bits_per_pixel >> 3);

	FB_FUN_MSG("+sirfsocfb_pan_display\n");

	mutex_lock(&fb->layer_info[layer].layer_lock);

	fb->lcdc_ops.flip_overlay(layer, info->fix.smem_start + byte_offset,
							LCDC_FLIP_FRAME);

	if ((var->activate & FB_ACTIVATE_VBL) && fb->layer_info[layer].enabled)
		wait_once(fb, layer);

	mutex_unlock(&fb->layer_info[layer].layer_lock);
	FB_FUN_MSG("-sirfsocfb_pan_display\n");
	return ret;
}

static int sirfsocfb_open(struct fb_info *info, int user)
{
	return 0;
}

static int sirfsocfb_close(struct fb_info *info, int user)
{
	return 0;
}

static int sirfsocfb_ioctl(struct fb_info *info, unsigned int cmd,
			   unsigned long arg)
{
	struct sirfsocfb *fb = (struct sirfsocfb *)info->par;
	int layer = sirfsocfb_get_layer(info);
	union {
		struct sirfsocfb_screen scr;
		unsigned long alpha_val;
		u32 count;
		int bufidx;
		struct sirfsocfb_createlayer create;
		struct sirfsocfb_bltparms    blt;
		struct sirfsocfb_bltparms_ble blt_ble;
		struct sirfsocfb_flush_cache_addr flush_cache_addr;
		struct sirfsocfb_layers_parms layers;
		int feature_layer;
		u16 *gamma_table;
		int wait;
	} data;
	int ret = 0;

	switch (cmd) {
	case SIRFSOCFB_SET_TOPLAYER:
		sirfsocfb_set_toplayer(fb, layer);
		break;
	case SIRFSOCFB_GET_TOPLAYER:
		{
			int layer = fb->lcdc_ops.get_toplayer();
			if (copy_to_user((void __user *)arg, &layer,
					 sizeof(int)))
				return -EFAULT;
			break;
		}
	case SIRFSOCFB_SET_ALPHA:
		if (copy_from_user(&data.alpha_val, (void __user *)arg,
				   sizeof(unsigned long)))
			return -EFAULT;
		if (data.alpha_val > 0xFF)
			return -EINVAL;
		sirfsocfb_set_alpha(fb, layer, data.alpha_val);
		break;
	case SIRFSOCFB_GET_ALPHA:
		if (copy_to_user
		    ((void __user *)arg, &(fb->layer_info[layer].alpha),
		     sizeof(unsigned long)))
			return -EFAULT;
		break;
	case SIRFSOCFB_SET_SCRSIZE:
		if (copy_from_user(&data.scr, (void __user *)arg,
				   sizeof(struct sirfsocfb_screen)))
			return -EFAULT;
		ret = sirfsocfb_layer_setsize(fb, layer, &data.scr);
		if (ret)
			return ret;
		break;
	case SIRFSOCFB_GET_SCRSIZE:
		sirfsocfb_layer_getsize(fb, layer, &data.scr);
		if (copy_to_user((void __user *)arg, &data.scr,
				 sizeof(struct sirfsocfb_screen)))
			return -EFAULT;
		break;
	case SIRFSOCFB_GET_COLORKEYS:
		if (copy_to_user
		    ((void __user *)arg, &(fb->layer_info[layer].ckey),
		     sizeof(struct sirfsocfb_colorkeys)))
			return -EFAULT;
		break;
	case SIRFSOCFB_SET_COLORKEYS:
		if (copy_from_user
		    (&(fb->layer_info[layer].ckey), (void __user *)arg,
		     sizeof(struct sirfsocfb_colorkeys)))
			return -EFAULT;
		sirfsocfb_set_ckey(fb, layer);
		break;
	case SIRFSOCFB_CREATE_LAYER:
		if (copy_from_user(&data.create, (void __user *)arg,
				   sizeof(struct sirfsocfb_createlayer)))
			return -EFAULT;
		if (sirfsocfb_create_overlay(fb, layer, &data.create))
			return -EFAULT;
		if (copy_to_user
		    ((void __user *)arg, &data.create,
		     sizeof(struct sirfsocfb_createlayer)))
			return -EFAULT;
		break;
	case SIRFSOCFB_DESTROY_LAYER:
		sirfsocfb_destroy_overlay(fb, layer);
		break;
	case SIRFSOCFB_Q_BUFFER:
		if (copy_from_user(&data.bufidx, (void __user *)arg,
				   sizeof(int)))
			return -EFAULT;
		sirfsocfb_q_buffer(fb, layer, data.bufidx);
		break;
	case SIRFSOCFB_DQ_BUFFER:
		sirfsocfb_dq_buffer(fb, layer, &data.bufidx);
		if (copy_to_user((void __user *)arg, &data.bufidx, sizeof(int)))
			return -EFAULT;
		break;
	case SIRFSOCFB_BLT_YUV2RGB:
		if (copy_from_user(&data.blt, (void __user *)arg,
				   sizeof(struct sirfsocfb_bltparms)))
			return -EFAULT;
		if (sirfsocfb_blt_yuv2rgb(fb, layer, &data.blt))
			return -EFAULT;
		break;
#ifdef SUPPORT_BLE
	case SIRFSOCFB_BLT_BLE:
		if (copy_from_user(&data.blt_ble, (void __user *)arg,
				   sizeof(struct sirfsocfb_bltparms_ble)))
			return -EFAULT;
		if (sirfsocfb_blt_ble(fb, layer, &data.blt_ble))
			return -EFAULT;
		break;
	case SIRFSOCFB_BLT_BLE_COMPLETE:
		if (copy_from_user(&data.wait, (void __user *)arg,
				   sizeof(int)))
			return -EFAULT;
		return sirfsocfb_blt_ble_complete(fb, layer, data.wait);
		break;
#endif
	case SIRFSOCFB_ENABLE_LAYER:
		sirfsocfb_layer_enable(fb, layer);
		break;
	case SIRFSOCFB_DISABLE_LAYER:
		sirfsocfb_layer_disable(fb, layer);
		break;
	case SIRFSOCFB_SET_DMASIZE:
		if (copy_from_user(&data.scr, (void __user *)arg,
				   sizeof(struct sirfsocfb_screen)))
			return -EFAULT;
		ret = sirfsocfb_set_dmasize(fb, layer, &data.scr);
		if (ret)
			return ret;
		break;
	case SIRFSOCFB_GET_DMASIZE:
		sirfsocfb_get_dmasize(fb, layer, &data.scr);
		if (copy_to_user((void __user *)arg, &data.scr,
				 sizeof(struct sirfsocfb_screen)))
			return -EFAULT;
		break;
	case SIRFSOCFB_FLUSH_CACHE:
		if (copy_from_user(&data.flush_cache_addr, (void __user *)arg,
				   sizeof(struct sirfsocfb_flush_cache_addr)))
			return -EFAULT;
		sirfsocfb_flush_cache(fb, layer, &data.flush_cache_addr);
		break;
	case SIRFSOCFB_SET_LAYERS:
		if (copy_from_user(&data.layers, (void __user *)arg,
			((struct sirfsocfb_layers_parms *)arg)->size))
			return -EFAULT;
		sirfsocfb_set_layers(fb, &data.layers);
		break;
	case SIRFSOCFB_ENABLE_FEATURE_LAYER:
		if (copy_from_user(&data.feature_layer, (void __user *)arg,
				   sizeof(int)))
			return -EFAULT;
		sirfsocfb_enable_feature_layer(fb, layer, data.feature_layer);
		break;
	case SIRFSOCFB_DISABLE_FEATURE_LAYER:
		sirfsocfb_disable_feature_layer(fb, layer);
		break;
	case SIRFSOCFB_SET_GAMMA_TABLE:
		data.gamma_table = memdup_user((void __user *)arg,
			256 * 3 * sizeof(u16));
		if (IS_ERR(data.gamma_table))
			return PTR_ERR(data.gamma_table);
		sirfsocfb_set_gamma_table(fb, layer, data.gamma_table);
		kfree(data.gamma_table);
		break;
	case SIRFSOCFB_GET_GAMMA_TABLE:
		data.gamma_table = kmalloc(256 * 3 * sizeof(u16), GFP_KERNEL);
		if (!data.gamma_table)
			return -ENOMEM;
		sirfsocfb_get_gamma_table(fb, layer, data.gamma_table);
		if (copy_to_user((void __user *)arg, data.gamma_table,
				256 * 3 * sizeof(u16))) {
			kfree(data.gamma_table);
			return -EFAULT;
		}
		kfree(data.gamma_table);
		break;
	case SIRFSOCFB_DUMP_REGISTER:
		fb->lcdc_ops.print_register();
		break;
	default:
		return -EINVAL;
		break;
	}

	return 0;
}

static int sirfsocfb_setcolreg(unsigned regno, unsigned red, unsigned green,
			       unsigned blue, unsigned transp,
			       struct fb_info *info)
{
	int layer = sirfsocfb_get_layer(info);

	FB_FUN_MSG("sirfsocfb_setcolreg\n");

	if (layer != LCDC_PRIMARY)
		return 0;

	if (info->fix.visual == FB_VISUAL_TRUECOLOR) {
		if (regno >= 16)
			return -EINVAL;

		((u32 *)(info->pseudo_palette))[regno] =
			(red << info->var.red.offset)	  |
			(green << info->var.green.offset) |
			(blue << info->var.blue.offset);
	}

	return 0;
}

static int sirfsocfb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
#if SIRFSOC_OVERLAY_ENABLE_CACHE
	int layer = sirfsocfb_get_layer(info);
#endif
	unsigned long off;
	unsigned long start;
	u32 len;

	if (vma->vm_pgoff > (~0UL >> PAGE_SHIFT))
		return -EINVAL;
	off = vma->vm_pgoff << PAGE_SHIFT;

	/* frame buffer memory */
	start = info->fix.smem_start;
	len = PAGE_ALIGN((start & ~PAGE_MASK) + info->fix.smem_len);
	if (off >= len) {
		/* memory mapped io */
		off -= len;
		if (info->var.accel_flags)
			return -EINVAL;
		start = info->fix.mmio_start;
		len = PAGE_ALIGN((start & ~PAGE_MASK) + info->fix.mmio_len);
	}

	start &= PAGE_MASK;
	if ((vma->vm_end - vma->vm_start + off) > len)
		return -EINVAL;
	off += start;
	vma->vm_pgoff = off >> PAGE_SHIFT;

#if SIRFSOC_OVERLAY_ENABLE_CACHE
	if (layer != SIRFSOC_OVERLAY_INDEX)
#endif
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	if (io_remap_pfn_range
	    (vma, vma->vm_start, off >> PAGE_SHIFT, vma->vm_end - vma->vm_start,
	     vma->vm_page_prot))
		return -EAGAIN;
	return 0;
}

struct fb_ops sirfsocfb_ops = {.owner = THIS_MODULE,
	.fb_open = sirfsocfb_open, .fb_release = sirfsocfb_close,
	.fb_check_var = sirfsocfb_check_var, .fb_set_par = sirfsocfb_set_par,
	.fb_blank = sirfsocfb_blank, .fb_pan_display = sirfsocfb_pan_display,
	.fb_ioctl = sirfsocfb_ioctl, .fb_setcolreg = sirfsocfb_setcolreg,
	.fb_fillrect = cfb_fillrect, .fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit, .fb_mmap = sirfsocfb_mmap,
};

static irqreturn_t sirfsocfb_irq_handler(int irq, void *data)
{
	struct sirfsocfb *fb = (struct sirfsocfb *)data;
	int index;
	int ret = IRQ_NONE;
	int intr_status;

	FB_FUN_MSG("sirfsocfb_irq_handler\n");

	intr_status = fb->lcdc_ops.irq_detected(LCDC_INTERRUPT_ALL);
	fb->lcdc_ops.clear_interrupt(LCDC_INTERRUPT_ALL);

	/* handle oflow interrupts */
	for (index = 0; index < SIRFSOCFB_MAX_LAYERS; index++) {
		if (intr_status & (1 << (LCDC_INTERRUPT_L0_OFLOW + index))) {
			/* overflow count  */
			fb->layer_info[index].fifo_overflow++;
			ret = IRQ_HANDLED;
		}
	}

	/* handle uflow interrupts */
	for (index = 0; index < SIRFSOCFB_MAX_LAYERS; index++) {
		if (intr_status & (1 << (LCDC_INTERRUPT_L0_UFLOW + index))) {
			/* overflow count  */
			fb->layer_info[index].fifo_underflow++;
			ret = IRQ_HANDLED;
		}
	}

	/* handle vsync interrupt */
	if (intr_status & (1 << LCDC_INTERRUPT_VSYNC)) {
		queue_work(fb->flip_wq, &fb->work);
		fb->vsync_timestamp = ktime_get();
		schedule_work(&fb->vsync_work);
		ret = IRQ_HANDLED;
	}

	return ret;
}

/**************** INIT/DEINIT ROUTINES ****************/
static int sirfsocfb_irq_init(struct sirfsocfb *fb)
{
	FB_FUN_MSG("sirfsocfb_irq_init\n");

	fb->lcdc_ops.clear_interrupt(LCDC_INTERRUPT_ALL);
	fb->lcdc_ops.disable_interrupt(LCDC_INTERRUPT_ALL);

	if (request_irq(fb->irq, sirfsocfb_irq_handler, IRQF_SHARED,
			"SIRFSOC-FB", fb)) {
		FB_ERR_MSG("irq_init: request_irq failed\n");
		return 1;
	}

	fb->flip_wq = create_singlethread_workqueue("sirfsocfb_workqueue");
	if (fb->flip_wq == NULL) {
		FB_ERR_MSG
		    ("irq_init: create_singlethreaded_workqueue failed\n");
		return 1;
	}
	INIT_WORK(&fb->work, layer_frame_irq);
	INIT_WORK(&fb->vsync_work, send_vsync_timestamp);

	return 0;
}

static void sirfsocfb_irq_deinit(struct sirfsocfb *fb)
{
	FB_FUN_MSG("sirfsocfb_irq_deinit\n");

	fb->lcdc_ops.disable_interrupt(LCDC_INTERRUPT_ALL);
	fb->lcdc_ops.clear_interrupt(LCDC_INTERRUPT_ALL);
	free_irq(fb->irq, fb);
	destroy_workqueue(fb->flip_wq);
}

static int sirfsocfb_register(struct sirfsocfb *fb)
{
	int ret, layer;
	unsigned int sysclk, div;
	u64 pixclk = 1000000000000ULL;

	FB_FUN_MSG("sirfsocfb_register\n");

	for (layer = 0; layer < SIRFSOCFB_MAX_LAYERS; layer++) {
		struct fb_var_screeninfo var = { 0 };

		if (!fb->layer_info[layer].valid)
			continue;

		fb->layer_info[layer].registered = 0;

		fb->layer_info[layer].ovl = NULL;

		fb->fb[layer].par = (void *)fb;
		fb->fb[layer].fbops = &sirfsocfb_ops;
		fb->fb[layer].flags = FBINFO_FLAG_DEFAULT;

		sprintf(fb->fb[layer].fix.id, "SIRFSOC-FB%d", layer);
		mutex_init(&fb->layer_info[layer].layer_lock);
		init_completion(&fb->layer_info[layer].done);
		fb->fb[layer].fix.type = FB_TYPE_PACKED_PIXELS;
		fb->fb[layer].fix.visual = FB_VISUAL_TRUECOLOR;

		fb->fb[layer].fix.type_aux = 0;
		fb->fb[layer].fix.xpanstep = 0;
		/* layer supports panning? */
		fb->fb[layer].fix.ypanstep = 1;
		fb->fb[layer].fix.ywrapstep = 0;
#ifdef SUPPORT_BLE
		fb->fb[layer].fix.accel = FB_ACCEL_BLE;
#else
		fb->fb[layer].fix.accel = FB_ACCEL_NONE;
#endif

		var.xres = fb->panel->mode.xres;
		var.yres = fb->panel->mode.yres;
		var.xres_virtual = fb->panel->mode.xres;
		var.yres_virtual = fb->panel->mode.yres * 2;
		var.bits_per_pixel = fb->panel->bpp;

		fb->fb[layer].fix.line_length =
		    var.xres_virtual * SIRFSOCFB_BYTES(var.bits_per_pixel);

		var.grayscale = fb->panel->grayscale;
		sysclk = clk_get_rate(fb->clk);
		div = sysclk / (sysclk / fb->panel->mode.pixclock);
		do_div(pixclk, div);
		var.pixclock = pixclk;
		var.left_margin = fb->panel->mode.left_margin;
		var.right_margin = fb->panel->mode.right_margin;
		var.upper_margin = fb->panel->mode.upper_margin;
		var.lower_margin = fb->panel->mode.lower_margin;
		var.hsync_len = fb->panel->mode.hsync_len;
		var.vsync_len = fb->panel->mode.vsync_len;
		var.sync = fb->panel->mode.sync;
		var.vmode = fb->panel->mode.vmode;
		var.activate = FB_ACTIVATE_NXTOPEN;
		var.nonstd = 0;
		var.accel_flags = 0;

		if (layer == REARVIEW_LAYER) {
			var.bits_per_pixel = 16;
			fb->fb[layer].fix.line_length = var.xres_virtual
			    * SIRFSOCFB_BYTES(var.bits_per_pixel);
		}

		fb->layer_info[layer].waiting_to_pan = DONT_PAN;

		ret = set_bitfields(&var);
		if (ret)
			return ret;

#if !SIRFSOC_ENABLE_ALPHA_BLENDING
		/* by default transp is disabled
		 * if needed by application, it has
		 * to set using PUT_VSCREENINFO ioctl */
		var.transp.offset = 0;
		var.transp.length = 0;
#endif

		/* store the var in fb_info stucture */
		fb->fb[layer].var = var;

		if ((layer == LCDC_PRIMARY)) {
			fb->fb[layer].pseudo_palette =
				&sirfsocfb_pseudo_palette;

			/* allocate a colormap for layer0 */
			if (fb_alloc_cmap(&fb->fb[layer].cmap, 16, 0))
				return -ENOMEM;

			/* enable bootsplash layer if not enabled by the
			 * bootloader */
			if (!fb->init_enabled) {
				var.activate = FB_ACTIVATE_NOW;
				ret = fb_set_var(&fb->fb[layer], &var);
				if (ret)
					return ret;
			} else {
				/* already enabled by the bootloader so just
				 * set the activate field */
				fb->layer_info[layer].enabled = 1;
				fb->fb[layer].var.activate = FB_ACTIVATE_NOW;
			}
		}

		if (layer == REARVIEW_LAYER) {
			var.activate = FB_ACTIVATE_NOW;
			ret = fb_set_var(&fb->fb[layer], &var);
			if (ret)
				return ret;
		}

		ret = register_framebuffer(&fb->fb[layer]);
		if (ret) {
			FB_ERR_MSG("Can't register fbdev for layer %d\n",
				layer);
			return ret;
		}

		/* Enable layer overflow and underflow interrupts.  */
		fb->lcdc_ops.enable_interrupt(LCDC_INTERRUPT_L0_OFLOW + layer);
		fb->lcdc_ops.enable_interrupt(LCDC_INTERRUPT_L0_UFLOW + layer);

		fb->layer_info[layer].registered = 1;
		fb->layer_info[layer].feature = NORMAL_LAYER;
		FB_NOT_MSG("layer%d registered!\n", layer);
	}

	mutex_init(&fb->ovl_lock);
	fb->lcdc_ops.enable_interrupt(LCDC_INTERRUPT_VSYNC);

	return 0;
}

static void sirfsocfb_unregister(struct sirfsocfb *fb)
{
	int layer;
	FB_FUN_MSG("sirfsocfb_unregister\n");

	for (layer = 0; layer < SIRFSOCFB_MAX_LAYERS; layer++) {
		if (!fb->layer_info[layer].valid)
			continue;
		/* disable all interrupts */
		fb->lcdc_ops.disable_interrupt(LCDC_INTERRUPT_ALL);

		layer_disable(fb, layer);

		if (fb->layer_info[layer].registered)
			unregister_framebuffer(&fb->fb[layer]);
	}
}

static void get_layer_ctrl_info(struct sirfsocfb *fb, int layer_ctrl)
{
	int i = 0;

	for (i = 0; i < SIRFSOCFB_MAX_LAYERS; i++)
		fb->layer_info[i].valid = ((layer_ctrl>>i)&0x1);
}

static int remap_frame_buffers(struct platform_device *pdev,
			       struct sirfsocfb *fb)
{
	int i, ret = 0;
	unsigned int size;
	unsigned int layer_reserve_size[4];
	dma_addr_t map_dma;

	FB_FUN_MSG("remap_frame_buffers\n");

	/* allocate memory per defined, otherwise according to actual needs. */
	of_property_read_u32_array(pdev->dev.of_node, "sirf,rsvmem_size",
			layer_reserve_size, ARRAY_SIZE(layer_reserve_size));

	for (i = 0; i < SIRFSOCFB_MAX_LAYERS; i++) {
		if (!fb->layer_info[i].valid)
			continue;

		if (layer_reserve_size[i] != 0)
			fb->fb[i].fix.smem_len = layer_reserve_size[i];
		else
			fb->fb[i].fix.smem_len = (fb->panel->bpp / 8) *
				fb->panel->mode.xres * fb->panel->mode.yres * 2;

		size = PAGE_ALIGN(fb->fb[i].fix.smem_len);
		fb->fb[i].screen_base =
			dma_alloc_writecombine(&pdev->dev,
				size, &map_dma, GFP_KERNEL);
		fb->fb[i].fix.smem_start = map_dma;

		if (fb->fb[i].screen_base == NULL) {
			FB_ERR_MSG("L%d IO remap failed!\n", i);
			ret = -ENOMEM;
			break;
		}
		fb->dma_buf[i].size = size;
		fb->dma_buf[i].base = fb->fb[i].screen_base;
		fb->dma_buf[i].dma_addr = fb->fb[i].fix.smem_start;

		if (i == LCDC_PRIMARY) {
			fb->layer_info[i].enabled = (fb->init_enabled) ? 1 : 0;
			if (!fb->init_enabled)
				memset(fb->fb[i].screen_base, 0x0,
				       fb->fb[i].fix.smem_len);
		} else {
			fb->layer_info[i].enabled = 0;
			memset(fb->fb[i].screen_base, 0x0,
			       fb->fb[i].fix.smem_len);
		}
	}

	return ret;
}

static void std_pre_enable(void)
{
	if (vcc != 0)
		i2c_smbus_write_byte_data(lcd_client, vcc & 0xFFFF, 0x1);
	else if (gpio_is_valid(vcc_gpio))
		gpio_set_value_cansleep(vcc_gpio, 1);

	msleep(50);
	if (vdd != 0)
		i2c_smbus_write_byte_data(lcd_client, vdd & 0xFFFF, 0x1);
	else if (gpio_is_valid(vdd_gpio))
		gpio_set_value_cansleep(vdd_gpio, 1);
}

static void std_post_enable(void)
{
	msleep(200);
	if (vee != 0)
		i2c_smbus_write_byte_data(lcd_client, vee & 0xFFFF, 0x1);
	else if (gpio_is_valid(vee_gpio))
		gpio_set_value_cansleep(vee_gpio, 1);
}

static void std_pre_disable(void)
{
	if (vee != 0)
		i2c_smbus_write_byte_data(lcd_client, vee >> 16, 0x1);
	else if (gpio_is_valid(vee_gpio))
		gpio_set_value_cansleep(vee_gpio, 0);

}

static void std_post_disable(void)
{
	if (vdd != 0)
		i2c_smbus_write_byte_data(lcd_client, vdd >> 16, 0x1);
	else if (gpio_is_valid(vdd_gpio))
		gpio_set_value_cansleep(vdd_gpio, 0);

	if (vcc != 0)
		i2c_smbus_write_byte_data(lcd_client, vcc >> 16, 0x1);
	else if (gpio_is_valid(vcc_gpio))
		gpio_set_value_cansleep(vcc_gpio, 0);
}

static void reset(void)
{
}

static void param_prepare(struct sirfsocfb *fb, struct lcdc_panel_info *panel)
{
	memset(panel, 0, sizeof(*panel));

	panel->hsync_period =
	    fb->panel->mode.xres + fb->panel->mode.hsync_len +
	    fb->panel->mode.left_margin + fb->panel->mode.right_margin - 1;
	panel->hsync_width = fb->panel->mode.hsync_len - 1;
	panel->vsync_period =
	    fb->panel->mode.yres + fb->panel->mode.vsync_len +
	    fb->panel->mode.upper_margin + fb->panel->mode.lower_margin - 1;
	panel->vsync_width = fb->panel->mode.vsync_len - 1;
	panel->hstart =
	    fb->panel->mode.hsync_len + fb->panel->mode.left_margin - 12;
	panel->vstart =
	    fb->panel->mode.vsync_len + fb->panel->mode.upper_margin;
	panel->hend = panel->hstart + fb->panel->mode.xres - 1;
	panel->vend = panel->vstart + fb->panel->mode.yres - 1;

	panel->out_fmt = LCDC_OUT_24BIT_RBG888;
	panel->rgb_sequence = RGB_SEQ_RGB;
	if (fb->panel->timing & PANEL_PCLK_POLAR)
		panel->pclk_polar = 1;
	if (fb->panel->timing & PANEL_PCLK_EDGE)
		panel->pclk_edge = 1;
	if (fb->panel->timing & PANEL_HSYNC_POLAR)
		panel->hsync_polar = 1;
	if (fb->panel->timing & PANEL_VSYNC_POLAR)
		panel->vsync_polar = 1;

	panel->iomaster = true;

	panel->sys_clk = clk_get_rate(fb->clk);
	panel->ref_rate = fb->panel->mode.pixclock /
	    (panel->hsync_period + 1) / (panel->vsync_period + 1);
	panel->pre_power_up = fb->panel->enable_pre;
	panel->post_power_up = fb->panel->enable_post;
	panel->pre_power_down = fb->panel->disable_pre;
	panel->post_power_down = fb->panel->disable_post;
	panel->reset = reset;

	panel->maxlayer = SIRFSOCFB_MAX_LAYERS - 1;
	panel->layer = LCDC_PRIMARY;

}

#ifdef SUPPORT_BLE
static irqreturn_t sirfsocfb_ble_irq_handler(int irq, void *data)
{
	struct sirfsocfb *fb = (struct sirfsocfb *)data;

	fb->ble_func.interrupt_routine(fb->ble_context);

	return IRQ_HANDLED;
}

static int sirfsocfb_setup_ble(struct sirfsocfb *fb)
{
	struct platform_device *pdev = fb->dev;
	struct ble_init_meminfo mem_info;
	int ret;
	struct device_node *np;
	const struct of_device_id sirfsoc_ble_tbl[] = {
		{ .compatible = "sirf,atlas6-ble"},
		{/* end */},
	};

	np = of_find_matching_node(NULL, sirfsoc_ble_tbl);
	if (!np) {
		dev_err(&pdev->dev, "Fail to get ble device node!\n");
		ret = -ENODEV;
		goto err;
	}
	fb->ble_base = of_iomap(np, 0);
	if (!fb->ble_base) {
		dev_err(&pdev->dev, "Fail to map ble regs\n");
		ret = -ENOMEM;
		goto err_put;
	}

	fb->ble_clk = of_clk_get_by_name(np, NULL);
	if (IS_ERR(fb->ble_clk)) {
		dev_err(&pdev->dev, "Fail to get ble clock!\n");
		ret = -EINVAL;
		goto err_unmap;
	}

	fb->ble_mem_base = dma_alloc_coherent(&pdev->dev,
		SZ_1M, &fb->ble_mem_offset, GFP_KERNEL);
	if (!fb->ble_mem_base) {
		FB_ERR_MSG("Fail to allocate ble dma mem!\n");
		ret = -ENOMEM;
		goto err_unmap;
	}
	fb->ble_mem_size = SZ_1M;

	fb->ble_irq = irq_of_parse_and_map(np, 0);
	if (!fb->ble_irq) {
		FB_ERR_MSG("Fail to get ble irq!\n");
		ret = -EINVAL;
		goto err_free;
	}

	vdss_ble_install_ops(&fb->ble_func);
	clk_prepare_enable(fb->ble_clk);

	mem_info.regbase = fb->ble_base;
	mem_info.membase = (unsigned int)fb->ble_mem_base;
	mem_info.memoffset = (unsigned int)fb->ble_mem_offset;
	mem_info.memsize = fb->ble_mem_size;

	fb->ble_func.initialize(&fb->ble_context, &mem_info);

	if (request_irq(fb->ble_irq, sirfsocfb_ble_irq_handler, 0,
		"SIRFSOC-BLE", fb)) {
		FB_ERR_MSG("Fail to request ble irq\n");
		ret = -EINVAL;
		goto err_disable_clk;
	}

	of_node_put(np);

	return 0;

err_disable_clk:
	clk_disable(fb->ble_clk);
err_free:
	dma_free_coherent(&pdev->dev, SZ_1M,
		fb->ble_mem_base, fb->ble_mem_offset);
err_unmap:
	iounmap(fb->ble_base);
err_put:
	of_node_put(np);
err:
	return ret;
}
#endif

static void sirfsocfb_probe_async(void *async_data, async_cookie_t cookie)
{
	struct platform_device *pdev = async_data;
	struct device_node *np;
	const struct of_device_id sirfsoc_vpp_tbl[] = {
		{ .compatible = "sirf,prima2-vpp"},
		{ .compatible = "sirf,macro-vpp" },
		{/* end */},
	};
	struct sirfsocfb *fb;
	struct sirfsocfb_panel *panel;
	struct resource *res;
	struct pinctrl *p;
	int i, ret = 0;
	int    layer_ctrl;
	struct lcdc_panel_info panel_info;

	FB_FUN_MSG("+sirfsocfb_probe\n");

	/* we sync to make sure multi-fb is registerred in order */
	async_synchronize_cookie(cookie);

	fb = devm_kzalloc(&pdev->dev, sizeof(*fb), GFP_KERNEL);
	if (!fb) {
		FB_ERR_MSG("Fail to allocate fb private data!\n");
		ret = -ENOMEM;
		goto err;
	}

	panel = devm_kzalloc(&pdev->dev, sizeof(*panel), GFP_KERNEL);
	if (!panel) {
		FB_ERR_MSG("Fail to allocate lcd panel!\n");
		ret = -ENOMEM;
		goto err;
	}
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		FB_ERR_MSG("Fail to get lcd regs resource!\n");
		ret = -EINVAL;
		goto err;
	}

	fb->base = devm_request_and_ioremap(&pdev->dev, res);
	if (fb->base == NULL) {
		FB_ERR_MSG("Fail to remap lcd regs!\n");
		ret = -ENOMEM;
		goto err;
	}

	fb->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(fb->clk)) {
		FB_ERR_MSG("Fail to get lcd clock!\n");
		ret = -EINVAL;
		goto err_unmap;
	}

	fb->irq = platform_get_irq(pdev, 0);
	if (!fb->irq) {
		FB_ERR_MSG("Fail to get lcd irq!\n");
		ret = -EINVAL;
		goto err_unmap;
	}

	/* init panel */
	np = of_parse_phandle(pdev->dev.of_node, "default-panel", 0);
	if (!np) {
		FB_ERR_MSG("Fail to get panel description\n");
		ret = -EINVAL;
		goto err_unmap;
	}
	ret = 0;

	ret |= of_property_read_u32(np, "hactive", &panel->mode.xres);
	ret |= of_property_read_u32(np, "vactive", &panel->mode.yres);
	ret |= of_property_read_u32(np, "left-margin",
		&panel->mode.left_margin);
	ret |= of_property_read_u32(np, "right-margin",
		&panel->mode.right_margin);
	ret |= of_property_read_u32(np, "hsync-len",
		&panel->mode.hsync_len);
	ret |= of_property_read_u32(np, "upper-margin",
		&panel->mode.upper_margin);
	ret |= of_property_read_u32(np, "lower-margin",
		&panel->mode.lower_margin);
	ret |= of_property_read_u32(np, "vsync-len",
		&panel->mode.vsync_len);
	ret |= of_property_read_u32(np, "pixclock",
		&panel->mode.pixclock);
	ret |= of_property_read_u32(np, "timing", &panel->timing);
	if (ret) {
		FB_ERR_MSG("Fail to get panel parms\n");
		ret = -EINVAL;
		goto err_unmap;
	}
	panel->enable_pre   = std_pre_enable;
	panel->enable_post  = std_post_enable;
	panel->disable_pre  = std_pre_disable;
	panel->disable_post = std_post_disable;

	panel->bpp = bpp;
	fb->panel = panel;
	param_prepare(fb, &panel_info);

	np = of_find_compatible_node(NULL, NULL, "sirf,lcd");
	if (!np) {
		FB_ERR_MSG("Fail to find lcd i2c node\n");
		ret = -EINVAL;
		goto err_unmap;
	}

	lcd_client = of_find_i2c_device_by_node(np);
	if (!lcd_client) {
		FB_ERR_MSG("Fail to get lcd i2c client\n");
		ret = -EINVAL;
		goto err_unmap;
	}

	p = devm_pinctrl_get_select_default(&pdev->dev);
	if (IS_ERR(p)) {
		FB_ERR_MSG("Fail to select lcd pinmux\n");
		ret = -EINVAL;
		goto err_unmap;
	}

	spin_lock_init(&fb->lock);

	fb->dev = pdev;
	platform_set_drvdata(pdev, fb);

	clk_prepare_enable(fb->clk);

	device_reset(&pdev->dev);

	if (of_property_read_u32(pdev->dev.of_node, "layer-ctrl",
				&layer_ctrl)) {
		dev_err(&pdev->dev, "get valid layer failed,only enable primary\n");
		layer_ctrl = 0x00000001;
	}

	get_layer_ctrl_info(fb, layer_ctrl);

	vcc_gpio = of_get_named_gpio(pdev->dev.of_node, "vcc-gpios", 0);
	if (gpio_is_valid(vcc_gpio)) {
		ret = devm_gpio_request(&pdev->dev, vcc_gpio, "sirfsoc_vcc");
		if (ret) {
			dev_err(&pdev->dev, "request VCC gpio failed\n");
			ret = -ENODEV;
			goto err_unmap;
		}
		gpio_direction_output(vcc_gpio, 1);
	}

	vdd_gpio = of_get_named_gpio(pdev->dev.of_node, "vdd-gpios", 0);
	if (gpio_is_valid(vdd_gpio)) {
		ret = devm_gpio_request(&pdev->dev, vdd_gpio, "sirfsoc_vdd");
		if (ret) {
			dev_err(&pdev->dev, "request VDD gpio failed\n");
			ret = -ENODEV;
			goto err_unmap;
		}
		gpio_direction_output(vdd_gpio, 1);
	}

	vee_gpio = of_get_named_gpio(pdev->dev.of_node, "vee-gpios", 0);
	if (gpio_is_valid(vee_gpio)) {
		ret = devm_gpio_request(&pdev->dev, vee_gpio, "sirfsoc_vee");
		if (ret) {
			dev_err(&pdev->dev, "request VEE gpio failed\n");
			ret = -ENODEV;
			goto err_unmap;
		}
		gpio_direction_output(vee_gpio, 1);
	}

	of_property_read_u32(pdev->dev.of_node, "power-vdd", &vdd);
	of_property_read_u32(pdev->dev.of_node, "power-vcc", &vcc);
	of_property_read_u32(pdev->dev.of_node, "power-vee", &vee);

	/* Init vpp staff here */
	np = of_find_matching_node(NULL, sirfsoc_vpp_tbl);
	if (!np) {
		dev_err(&pdev->dev, "get vpp device node failed");
		ret = -ENODEV;
		goto err_unmap;
	}
	fb->vpp_base = of_iomap(np, 0);
	fb->vpp_clk = of_clk_get_by_name(np, NULL);
	if (IS_ERR(fb->vpp_clk)) {
		FB_ERR_MSG("Unable to get vpp clock!\n");
		ret = -EINVAL;
		goto err_unmap;
	}
	clk_prepare_enable(fb->vpp_clk);

	vdss_install_lcdc_ops(&fb->lcdc_ops);

	/* Check if LCD preinited by uboot */
	fb->init_enabled =
	    fb->lcdc_ops.init(fb->base, fb->vpp_base, 0,
		    bpp, &panel_info);
	fb->vpp_ops = fb->lcdc_ops.load_vpp_ops();

	FB_INF_MSG("lcd external init status %d\n", fb->init_enabled);

	ret = sirfsocfb_irq_init(fb);
	if (ret)
		goto err_rel_clk;

	/* remap the frame buffers */
	ret = remap_frame_buffers(pdev, fb);
	if (ret)
		goto err_deinit_irq;

	/* Register the framebuffer device */
	ret = sirfsocfb_register(fb);
	if (ret)
		goto err_unregister;

	ret = device_create_file(&pdev->dev, &dev_attr_layer_fifo_underflow);
	if (ret)
		goto err_unregister;

	ret = device_create_file(&pdev->dev, &dev_attr_layer_fifo_overflow);
	if (ret)
		goto err_remove_fifo_underflow_file;

	ret = device_create_file(&pdev->dev, &dev_attr_vsync_timestamp);
	if (ret)
		goto err_remove_fifo_overflow_file;

#ifdef SUPPORT_BLE
	sirfsocfb_setup_ble(fb);
#endif

	/* default alpha 0xFF */
	for (i = 0; i < SIRFSOCFB_MAX_LAYERS; i++)
		sirfsocfb_set_alpha(fb, i, 0xFF);

	FB_INF_MSG("set layer%d as top layer\n", toplayer);
	sirfsocfb_set_toplayer(fb, toplayer);

#if !SIRFSOC_ENABLE_ALPHA_BLENDING
	/* Enable toplayer colorkey */
	fb->layer_info[toplayer].ckey.enable = 1;
	fb->layer_info[toplayer].ckey.color_key_big = (bpp == 32) ?
		0x100010 : 0x1002;
	fb->layer_info[toplayer].ckey.color_key_small = (bpp == 32) ?
		0x100010 : 0x1002;
	sirfsocfb_set_ckey(fb, toplayer);
#endif

#ifndef MODULE
#if !defined(CONFIG_FRAMEBUFFER_CONSOLE) && defined(CONFIG_LOGO)
	fb_prepare_logo(&fb->fb[LCDC_PRIMARY], 0);
	fb_show_logo(&fb->fb[LCDC_PRIMARY], 0);
#endif
#endif
	layer_enable(fb, LCDC_PRIMARY);

	FB_FUN_MSG("-sirfsocfb_probe\n");
	return;
err_remove_fifo_overflow_file:
	device_remove_file(&pdev->dev,
		&dev_attr_layer_fifo_overflow);
err_remove_fifo_underflow_file:
	device_remove_file(&pdev->dev,
		&dev_attr_layer_fifo_underflow);
err_unregister:
	sirfsocfb_unregister(fb);
err_deinit_irq:
	for (i = 0; i < SIRFSOCFB_MAX_LAYERS; i++) {
		if (fb->dma_buf[i].base)
			dma_free_writecombine(&pdev->dev, fb->dma_buf[i].size,
				fb->dma_buf[i].base,
				fb->dma_buf[i].dma_addr);
	}
	sirfsocfb_irq_deinit(fb);
err_rel_clk:
	clk_disable(fb->clk);
	clk_put(fb->clk);
	clk_disable(fb->vpp_clk);
	clk_put(fb->vpp_clk);
err_unmap:
	devm_iounmap(&pdev->dev, fb->base);
err:
	async_synchronize_cookie(cookie);
	return;
}

static int sirfsocfb_probe(struct platform_device *pdev)
{
	async_schedule(sirfsocfb_probe_async, pdev);
	return 0;
}

static int sirfsocfb_remove(struct platform_device *pdev)
{
	int i;
	struct sirfsocfb *fb = platform_get_drvdata(pdev);

	FB_FUN_MSG("sirfsocfb_remove\n");

	sirfsocfb_unregister(fb);
	for (i = 0; i < SIRFSOCFB_MAX_LAYERS; i++) {
		kfree(fb->layer_info[i].ovl);
		if (fb->dma_buf[i].base)
			dma_free_writecombine(&pdev->dev, fb->dma_buf[i].size,
				fb->dma_buf[i].base,
				fb->dma_buf[i].dma_addr);
	}

	fb->lcdc_ops.terminate();
	fb->init_enabled = 0;
	device_remove_file(&pdev->dev, &dev_attr_layer_fifo_overflow);
	device_remove_file(&pdev->dev, &dev_attr_layer_fifo_underflow);
	device_remove_file(&pdev->dev, &dev_attr_vsync_timestamp);
	sirfsocfb_irq_deinit(fb);

	clk_disable(fb->clk);
	clk_put(fb->clk);

	FB_NOT_MSG("framebuffer driver unregistered!\n");
	return 0;
}

int sirfsocfb_km_blt_yuv2rgb(struct fb_info *info,
	struct sirfsocfb_bltparms *parms)
{
	struct sirfsocfb *fb = (struct sirfsocfb *)info->par;
	int layer = sirfsocfb_get_layer(info);

	return sirfsocfb_blt_yuv2rgb(fb, layer, parms);
}
EXPORT_SYMBOL(sirfsocfb_km_blt_yuv2rgb);

int sirfsocfb_km_enable_feature_layer(struct fb_info *info,
	enum sirfsocfb_feature_layer feature)
{
	struct sirfsocfb *fb = (struct sirfsocfb *)info->par;
	int layer = sirfsocfb_get_layer(info);

	return sirfsocfb_enable_feature_layer(fb, layer, feature);
}
EXPORT_SYMBOL(sirfsocfb_km_enable_feature_layer);

int sirfsocfb_km_disable_feature_layer(struct fb_info *info)
{
	struct sirfsocfb *fb = (struct sirfsocfb *)info->par;
	int layer = sirfsocfb_get_layer(info);

	return sirfsocfb_disable_feature_layer(fb, layer);
}
EXPORT_SYMBOL(sirfsocfb_km_disable_feature_layer);

#ifdef CONFIG_PM
static int sirfsocfb_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sirfsocfb *fb = platform_get_drvdata(pdev);
	FB_FUN_MSG("sirfsocfb_suspend\n");

#ifdef SUPPORT_BLE
	disable_irq(fb->ble_irq);
	fb->ble_func.sleep();
	clk_disable(fb->ble_clk);
#endif
	disable_irq(fb->irq);

	fb->lcdc_ops.sleep();
	clk_disable(fb->clk);
	clk_disable(fb->vpp_clk);

	return 0;
}

static int sirfsocfb_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sirfsocfb *fb = platform_get_drvdata(pdev);
	FB_FUN_MSG("sirfsocfb_resume\n");

#ifdef SUPPORT_BLE
	clk_enable(fb->ble_clk);
	fb->ble_func.wakeup();
	enable_irq(fb->ble_irq);
#endif
	clk_enable(fb->clk);
	clk_enable(fb->vpp_clk);

	/* Check if LCD preinited by uboot */
	fb->init_enabled = fb->lcdc_ops.wakeup();
	enable_irq(fb->irq);
	FB_NOT_MSG("LCD resumed\n");

	return 0;
}

static int sirfsocfb_freeze(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sirfsocfb *fb = platform_get_drvdata(pdev);
	FB_FUN_MSG("sirfsocfb_freeze\n");

#ifdef SUPPORT_BLE
	disable_irq(fb->ble_irq);
	fb->ble_func.sleep();
	clk_disable(fb->ble_clk);
#endif
	disable_irq(fb->irq);

	/* For Android hibernation, lcd clock can not be disabled here.
	 * Or else, shutdown wallpaper can not be seen after this function.
	 * Shut down lcd panel will only show backlight if lcd clock is
	 * enabled, so don't do it here.
	 * Separate Android from Linux because of Linux don't have shutdown
	 * wallpaper now.
	 * Make it general if Linux adds shutdown wallpaper in future */
#ifndef CONFIG_ANDROID
	fb->lcdc_ops.sleep();
	clk_disable(fb->clk);
#endif
	clk_disable(fb->vpp_clk);

	return 0;
}

static int sirfsocfb_restore(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sirfsocfb *fb = platform_get_drvdata(pdev);
	FB_NOT_MSG("LCD restore\n");

#ifdef CONFIG_ANDROID
	/* Clear fb0 to avoid wallpaper garbage after hibernation back */
	if (fb->layer_info[LCDC_PRIMARY].enabled)
		memset(fb->fb[LCDC_PRIMARY].screen_base, 0x0,
			fb->fb[LCDC_PRIMARY].fix.smem_len);
#endif

#ifdef SUPPORT_BLE
	clk_enable(fb->ble_clk);
	fb->ble_func.wakeup();
	enable_irq(fb->ble_irq);
#endif
	/* For Android hibernation, there is no need to enable lcd clock here
	 * because it is always enabled in uboot. This is only used for Android
	 * shutdown wallpaper feature.
	 * Make it general if Linux adds shutdown wallpaper in future */
#ifndef CONFIG_ANDROID
	clk_enable(fb->clk);
#endif
	clk_enable(fb->vpp_clk);

	/* Check if LCD preinited by uboot */
	fb->init_enabled = fb->lcdc_ops.wakeup();
	enable_irq(fb->irq);

	return 0;
}

#else
#define sirfsocfb_suspend   NULL
#define sirfsocfb_resume    NULL
#define sirfsocfb_freeze    NULL
#define sirfsocfb_restore   NULL
#endif

static const struct dev_pm_ops sirfsocfb_pm_ops = {
	.freeze = sirfsocfb_freeze,
	.restore = sirfsocfb_restore,
	.suspend = sirfsocfb_suspend,
	.resume = sirfsocfb_resume,
};

static struct of_device_id sirfsocfb_match_tbl[] = {
	{ .compatible = "sirf,prima2-lcd", },
	{ /* end */ }
};

static struct platform_driver sirfsocfb_driver = {
	.driver = {
		.name = "sirfsocfb",
		.pm = &sirfsocfb_pm_ops,
		.of_match_table = sirfsocfb_match_tbl,
	},
	.probe = sirfsocfb_probe,
	.remove = sirfsocfb_remove,
};

static __init int sirfsocfb_init(void)
{
	return platform_driver_register(&sirfsocfb_driver);
}

static void __exit sirfsocfb_exit(void)
{
	platform_driver_unregister(&sirfsocfb_driver);
}

subsys_initcall(sirfsocfb_init);
module_exit(sirfsocfb_exit);

MODULE_DESCRIPTION("SiRF SoC Frame buffer driver");
MODULE_AUTHOR("Ramya Segar");
MODULE_LICENSE("GPL");
