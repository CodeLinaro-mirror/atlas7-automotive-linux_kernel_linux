/*
 * (C) Copyright (C) 2007 SiRF Technology Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
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
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_i2c.h>
#include <linux/of_gpio.h>
#include <linux/of_address.h>
#include <linux/pinctrl/consumer.h>
#include <linux/memblock.h>
#include <linux/sirfsoc_rst.h>
#include <asm/cacheflush.h>
#include <asm/uaccess.h>
#include <video/sirfsoc_fb.h>

#include "CspCmnLcd.h"
#include "CspCmnVpp.h"

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

static uint toplayer = LCD_PRIMARY;
module_param(toplayer, uint, S_IRUGO);
MODULE_PARM_DESC(toplayer, "LCD panel default top layer.");

#define REARVIEW_LAYER LCD_OVERLAY_3

static u32 sirfsocfb_pseudo_palette[16];

static struct i2c_client *lcd_client;
static phys_addr_t  sirf_fb_phy_base;
static phys_addr_t  sirf_fb_phy_size;
static int vcc;
static int vdd;
static int vee;


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

/**************** HELPER FUNCTIONS ****************/
static inline int sirfsocfb_get_layer(struct fb_info *info)
{
	return info->fix.id[10] - '0';
}

/* Gracefully shutdown a layer if it is already running */
static void layer_disable(void *vfb, int layer)
{
	struct sirfsocfb *fb = (struct sirfsocfb *)vfb;
	struct sirfsocfb_overlay *pOverlay = fb->layer_info[layer].pOvl;

	if (fb->layer_info[layer].enabled != 0) {
		fb->lcd_func.pfnHideOverlay(layer);

		if (pOverlay) {
			int i;
			for (i = 0; i < pOverlay->num_buffers; i++)
				pOverlay->pflips[i].wait = 0;
			pOverlay->num_dq =
			    (pOverlay->dq_idx >= pOverlay->q_idx) ?
			    (pOverlay->num_buffers - (pOverlay->dq_idx - pOverlay->q_idx))
			    : (pOverlay->q_idx - pOverlay->dq_idx);
			pOverlay->num_q = 0;
			pOverlay->flip_idx = pOverlay->q_idx;
		}

		/* Mark the layer as disabled */
		fb->layer_info[layer].enabled = 0;
	}
}

static void layer_enable(void *vfb, int layer)
{
	struct sirfsocfb *fb = (struct sirfsocfb *)vfb;

	if (fb->layer_info[layer].enabled == 0) {
		fb->lcd_func.pfnShowOverlay(layer);

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
	struct sirfsocfb_overlay *pOverlay = fb->layer_info[layer].pOvl;
	u32 current_count;
	FB_FUN_MSG("wait_vsync\n");

	if (!pOverlay)
		return;

	current_count = pOverlay->vsync_count;
	while (!LARGER(current_count, v_count)) {
		wait_once(fb, layer);
		current_count = pOverlay->vsync_count;
	}
}

static void insert_flip_item(struct sirfsocfb *fb, int layer, int bufidx)
{
	struct sirfsocfb_overlay *pOverlay = fb->layer_info[layer].pOvl;
	struct sirfsocfb_flipitem *flip;
	u32 base;
	unsigned long flags;
	int q_idx;

	if (!pOverlay)
		return;

	base = (bufidx * pOverlay->hstride_byte) + fb->fb[layer].fix.smem_start;

	spin_lock_irqsave(&fb->lock, flags);
	FB_ASSERT(((pOverlay->dq_idx - pOverlay->flip_idx) ==
		   pOverlay->num_buffers - pOverlay->num_dq)
		  || ((pOverlay->dq_idx - pOverlay->flip_idx)
		      == -pOverlay->num_dq));

	q_idx = pOverlay->q_idx;
	pOverlay->q_idx++;
	if (pOverlay->q_idx == pOverlay->num_buffers)
		pOverlay->q_idx = 0;

	flip = &(pOverlay->pflips[q_idx]);
	flip->base = base;
	flip->bufidx = bufidx;

	pOverlay->num_q++;
	spin_unlock_irqrestore(&fb->lock, flags);
}

static void execute_flip_item(struct sirfsocfb *fb, int layer)
{
	struct sirfsocfb_overlay *pOverlay = fb->layer_info[layer].pOvl;
	struct sirfsocfb_flipitem *flip;
	int flip_idx, prev_idx;
	unsigned long flags;

	if (!pOverlay)
		return;

	spin_lock_irqsave(&fb->lock, flags);
	if (pOverlay->num_q > 0) {
		FB_ASSERT(((pOverlay->q_idx - pOverlay->flip_idx) ==
			   pOverlay->num_q)
			  || (pOverlay->q_idx + pOverlay->num_buffers -
			      pOverlay->flip_idx) == pOverlay->num_q);
		pOverlay->num_q--;
		flip_idx = pOverlay->flip_idx;
		pOverlay->flip_idx++;
		if (pOverlay->flip_idx == pOverlay->num_buffers)
			pOverlay->flip_idx = 0;

		flip = &(pOverlay->pflips[flip_idx]);
		fb->lcd_func.pfnFlipOverlay(layer, flip->base, LCD_FLIP_FRAME);
		flip->wait = fb->layer_info[layer].enabled;

		if (flip_idx == 0)
			prev_idx = pOverlay->num_buffers - 1;
		else
			prev_idx = flip_idx - 1;

		/* Base of last flip will be invalid after vsync_count */
		pOverlay->pflips[prev_idx].vsync_count = pOverlay->vsync_count;
		pOverlay->prev_count = pOverlay->vsync_count;

		pOverlay->num_dq++;
		spin_unlock_irqrestore(&fb->lock, flags);
	} else {
		spin_unlock_irqrestore(&fb->lock, flags);
	}
}

static void layer_frame_irq(struct work_struct *data)
{
	struct sirfsocfb *fb;
	int layer;
	struct sirfsocfb_overlay *pOverlay;

	fb = container_of(data, struct sirfsocfb, work);

	for (layer = 0; layer < SIRFSOCFB_MAX_LAYERS; layer++) {
		if (!fb->layer_info[layer].enabled)
			continue;

		pOverlay = fb->layer_info[layer].pOvl;
		if (pOverlay) {
			pOverlay->vsync_count++;
			if (fb->layer_info[layer].waiting_to_pan) {
				fb->layer_info[layer].waiting_to_pan = DONT_PAN;
				complete(&fb->layer_info[layer].done);
			}

			if (pOverlay->num_q > 0)
				execute_flip_item(fb, layer);
		} else {
			if (fb->layer_info[layer].waiting_to_pan) {
				fb->layer_info[layer].waiting_to_pan = DONT_PAN;
				complete(&fb->layer_info[layer].done);
			}
		}
	}
}

static int set_par(struct fb_info *info)
{
	struct sirfsocfb *fb = (struct sirfsocfb *)info->par;
	int layer = sirfsocfb_get_layer(info);
	LCD_SETPARAMS_DATA sSetData, sGetData;
	int ret = 0;
	unsigned int byte_offset =
		(info->var.yoffset * info->var.xres_virtual + info->var.xoffset) *
		(info->var.bits_per_pixel >> 3);

	FB_FUN_MSG("set_par\n");

	memset(&sSetData, 0, sizeof(sSetData));
	memset(&sGetData, 0, sizeof(sGetData));

	sSetData.eLayer = layer;
	sSetData.ui32Base = info->fix.smem_start + byte_offset;

	if (info->var.bits_per_pixel == 16)
		sSetData.eLcdFormat = LCD_PIXELFORMAT_565;
	else if (info->var.transp.length)
		sSetData.eLcdFormat = LCD_PIXELFORMAT_8888;
	else
		sSetData.eLcdFormat = LCD_PIXELFORMAT_BGRX_8880;

	if (sSetData.eLcdFormat == LCD_PIXELFORMAT_8888)
		sSetData.bSourceAlpha = 1;
	else
		sSetData.bSourceAlpha = 0;

	sSetData.i32SurfWidth = info->var.xres_virtual;
	sSetData.i32SurfHeight = info->var.yres_virtual;
	sSetData.sRectSrc.left = 0;
	sSetData.sRectSrc.right = info->var.xres;
	sSetData.sRectSrc.top = 0;
	sSetData.sRectSrc.bottom = info->var.yres;

	sGetData.eLayer = layer;
	fb->lcd_func.pfnGetParameters(&sGetData);

	if ((sGetData.ui32Base == sSetData.ui32Base) &&
	    (sGetData.eLcdFormat == sSetData.eLcdFormat) &&
	    (sGetData.i32SurfWidth == sSetData.i32SurfWidth) &&
	    (sGetData.i32SurfHeight == sSetData.i32SurfHeight) &&
	    ((sGetData.sRectSrc.right - sGetData.sRectSrc.left) ==
	     (sSetData.sRectSrc.right - sSetData.sRectSrc.left)) &&
	    ((sGetData.sRectSrc.bottom - sGetData.sRectSrc.top) ==
	     (sSetData.sRectSrc.bottom - sSetData.sRectSrc.top))) {
		fb->lcd_func.pfnFlipOverlay(layer, sSetData.ui32Base, LCD_FLIP_FRAME);
	} else {
		sGetData.ui32Base = sSetData.ui32Base;
		sGetData.eLcdFormat = sSetData.eLcdFormat;
		sGetData.i32SurfWidth = sSetData.i32SurfWidth;
		sGetData.i32SurfHeight = sSetData.i32SurfHeight;
		sGetData.sRectSrc = sSetData.sRectSrc;
		sGetData.bSourceAlpha = sSetData.bSourceAlpha;
		sGetData.bPremultiAlpha = 1;

		/* no change for sRectDst.left & sRectDst.top */
		sGetData.sRectDst.right =
		    sGetData.sRectDst.left + info->var.xres;
		sGetData.sRectDst.bottom =
		    sGetData.sRectDst.top + info->var.yres;

		set_bitfields(&info->var);

		if (!fb->lcd_func.pfnSetParameters(&sGetData))
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
	fb->lcd_func.pfnSetTopLayer(layer);
}

static void sirfsocfb_set_alpha(struct sirfsocfb *fb, int layer,
				unsigned long alpha_val)
{
	unsigned long flags;
	FB_FUN_MSG("sirfsocfb_set_alpha\n");

	spin_lock_irqsave(&fb->lock, flags);
	fb->lcd_func.pfnSetGlobalAlpha(layer, alpha_val);
	spin_unlock_irqrestore(&fb->lock, flags);

	fb->layer_info[layer].alpha = alpha_val;
}

static void sirfsocfb_set_ckey(struct sirfsocfb *fb, int layer)
{
	unsigned long flags;
	struct sirfsocfb_colorkeys *pckey = &(fb->layer_info[layer].ckey);
	FB_FUN_MSG("sirfsocfb_set_ckey\n");

	spin_lock_irqsave(&fb->lock, flags);
	fb->lcd_func.pfnSetSrcCKey(layer,
				   pckey->enable,
				   pckey->color_key_big,
				   pckey->color_key_small);
	spin_unlock_irqrestore(&fb->lock, flags);
}

static int sirfsocfb_layer_setsize(struct sirfsocfb *fb, int layer,
				   struct sirfsocfb_screen *scr)
{
	int ret = 0;
	struct sirfsocfb_overlay *pOverlay = fb->layer_info[layer].pOvl;
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

	if (!pOverlay && invalid) {
		FB_ERR_MSG("Set invalid position\n");
		FB_FUN_MSG("-sirfsocfb_layer_setsize");
		mutex_unlock(&fb->ovl_lock);
		return -EINVAL;
	} else {
		RECT sRectDst;
		/* backup overlay position for only surface flinger knows where the overlay surface located at*/
		fb->ovl_pos.x = scr->xstart;
		fb->ovl_pos.y = scr->ystart;
		fb->ovl_pos.w = scr->xsize;
		fb->ovl_pos.h = scr->ysize;

		sRectDst.left = scr->xstart;
		sRectDst.right = scr->xstart + scr->xsize;
		sRectDst.top = scr->ystart;
		sRectDst.bottom = scr->ystart + scr->ysize;

		mutex_lock(&fb->layer_info[layer].layer_lock);

		fb->lcd_func.pfnSetOverlayPos(layer, NULL, &sRectDst);
		if (pOverlay)
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
	RECT sRectSrc;

	FB_FUN_MSG("+sirfsocfb_layer_setdma x:%d y:%d w:%d h:%d\n", scr->xstart,
		   scr->ystart, scr->xsize, scr->ysize);

	mutex_lock(&fb->layer_info[layer].layer_lock);
	sRectSrc.left = scr->xstart;
	sRectSrc.right = scr->xstart + scr->xsize;
	sRectSrc.top = scr->ystart;
	sRectSrc.bottom = scr->ystart + scr->ysize;

	fb->lcd_func.pfnSetOverlayPos(layer, &sRectSrc, NULL);

	mutex_unlock(&fb->layer_info[layer].layer_lock);
	FB_FUN_MSG("-sirfsocfb_layer_setdma\n");
	return 0;
}

static void sirfsocfb_get_dmasize(struct sirfsocfb *fb, int layer,
				  struct sirfsocfb_screen *scr)
{
	LCD_SETPARAMS_DATA sData;
	FB_FUN_MSG("sirfsocfb_layer_getdma\n");

	sData.eLayer = layer;

	fb->lcd_func.pfnGetParameters(&sData);
	scr->xsize = sData.sRectSrc.right - sData.sRectSrc.left;
	scr->ysize = sData.sRectSrc.bottom - sData.sRectSrc.top;
	scr->xstart = sData.sRectSrc.left;
	scr->ystart = sData.sRectSrc.top;
}

static int sirfsocfb_create_overlay(struct sirfsocfb *fb, int layer,
				    struct sirfsocfb_createlayer *create)
{
	int bits_per_pixel = 0, i;
	struct sirfsocfb_overlay *pOverlay;
	LCD_ALLOCOVERLAY_DATA sData;

	sData.eLayer = layer;

	switch (create->format) {
	case FORMAT_RGB_565:
		bits_per_pixel = 16;
		sData.eLcdFormat = LCD_PIXELFORMAT_565;
		break;
	case FORMAT_BGRA_8888:
	case FORMAT_BGRX_8888:
		bits_per_pixel = 32;
		sData.eLcdFormat = LCD_PIXELFORMAT_8888;
		break;
	case FORMAT_YCbCr_420_P:
		bits_per_pixel = 12;
		sData.eLcdFormat = LCD_PIXELFORMAT_I420;
		break;
	case FORMAT_YCbYCr_422_I:
		bits_per_pixel = 16;
		sData.eLcdFormat = LCD_PIXELFORMAT_YUYV;
		break;
	case FORMAT_CrYCbY_422_I:
		bits_per_pixel = 16;
		sData.eLcdFormat = LCD_PIXELFORMAT_VYUY;
		break;
	case FORMAT_YCbCr_420_SP:
		bits_per_pixel = 12;
		sData.eLcdFormat = LCD_PIXELFORMAT_NV12;
		break;
	case FORMAT_YCrCb_420_SP:
		bits_per_pixel = 12;
		sData.eLcdFormat = LCD_PIXELFORMAT_NV21;
		break;
	case FORMAT_YCbCr_422_SP:
	case FORMAT_RGBA_8888:
	case FORMAT_RGBX_8888:
		FB_ERR_MSG("Unsupported format!\n");
		return -EINVAL;
	}

	sData.i32Width = create->width;
	sData.i32Height = create->height;

	fb->lcd_func.pfnAllocOverlay(&sData);
	create->wstride_byte = sData.i32WStrideByte;
	create->hstride_byte = sData.i32HStrideByte;
	create->wstride_pixel = sData.i32WStridePixel;
	create->hstride_pixel = sData.i32HStridePixel;

	create->num_buffers = fb->fb[layer].fix.smem_len / create->hstride_byte;

	pOverlay = kzalloc(sizeof(struct sirfsocfb_overlay) +
			   (sizeof(struct sirfsocfb_flipitem) *
			    create->num_buffers), GFP_KERNEL);

	pOverlay->pflips = (struct sirfsocfb_flipitem *)
	    (((unsigned char *)pOverlay) + sizeof(struct sirfsocfb_overlay));

	pOverlay->num_dq = pOverlay->num_buffers = create->num_buffers;
	pOverlay->hstride_byte = create->hstride_byte;

	for (i = 0; i < create->num_buffers; i++)
		pOverlay->pflips[i].bufidx = i;

	fb->layer_info[layer].pOvl = pOverlay;
	fb->layer_info[layer].queued = 0;
	fb->layer_info[layer].valid_pos = 0;

	return 0;
}

static void sirfsocfb_dq_buffer(struct sirfsocfb *fb, int layer, int *bufidx)
{
	struct sirfsocfb_overlay *pOverlay = fb->layer_info[layer].pOvl;
	int dq_idx;
	unsigned long flags;
	int wait_count = 0;

	if (!pOverlay) {
		*bufidx = -1;
		return;
	}

	/* Avoid to dequeue front buffer */
	while (pOverlay->num_dq <= 1) {
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
	FB_ASSERT(pOverlay->num_dq > 1);
	pOverlay->num_dq--;
	dq_idx = pOverlay->dq_idx;
	pOverlay->dq_idx++;
	if (pOverlay->dq_idx == pOverlay->num_buffers)
		pOverlay->dq_idx = 0;

	*bufidx = pOverlay->pflips[dq_idx].bufidx;
	spin_unlock_irqrestore(&fb->lock, flags);

	if (pOverlay->pflips[dq_idx].wait)
		wait_vsync(fb, layer, pOverlay->pflips[dq_idx].vsync_count);

	return;
}

static void sirfsocfb_destroy_overlay(struct sirfsocfb *fb, int layer)
{
	struct sirfsocfb_overlay *pOverlay = fb->layer_info[layer].pOvl;
	struct layer_info *info = &fb->layer_info[layer];

	mutex_lock(&info->layer_lock);
	layer_disable(fb, layer);
	mutex_unlock(&info->layer_lock);
	fb->lcd_func.pfnFreeOverlay(layer);
	kfree(pOverlay);
	fb->layer_info[layer].pOvl = NULL;
}

static void sirfsocfb_q_buffer(struct sirfsocfb *fb, int layer, int bufidx)
{

	struct sirfsocfb_overlay *pOverlay = fb->layer_info[layer].pOvl;
	struct layer_info *info = &fb->layer_info[layer];

	u32 vsync_count;

	if (!pOverlay)
		return;

	mutex_lock(&info->layer_lock);
	insert_flip_item(fb, layer, bufidx);
	vsync_count = pOverlay->vsync_count;

	if (fb->layer_info[layer].enabled) {
		if ((pOverlay->num_q == 1)
		    && LARGER(vsync_count, pOverlay->prev_count)) {
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
	void *flush_start, *flush_end;
	unsigned long addr_start, addr_end;

	/* calculate virtual address */
	flush_start = flush_cache_addr->phy_addr_start
	    - fb->fb[layer].fix.smem_start + fb->fb[layer].screen_base;
	flush_end = flush_start + flush_cache_addr->phy_addr_size;

	/* calculate physical address */
	addr_start = flush_cache_addr->phy_addr_start;
	addr_end = addr_start + flush_cache_addr->phy_addr_size;

	switch (flush_cache_addr->flush_cache_op) {
	case FLUSH_CACHE_OP_INVALID:
		/* invalid L1 cache */
		dmac_flush_range(flush_start, flush_end);
		/* invalid L2 cache */
		outer_inv_range(addr_start, addr_end);
		break;
	case FLUSH_CACHE_OP_CLEAN:
		/* clean L1 cache */
		dmac_flush_range(flush_start, flush_end);
		/* clean L2 cache */
		outer_clean_range(addr_start, addr_end);
		break;
	case FLUSH_CACHE_OP_FLUSH:
		/* flush L1 cache */
		dmac_flush_range(flush_start, flush_end);
		/* flush L2 cache */
		outer_flush_range(addr_start, addr_end);
		break;
	default:
		break;
	}

}

static void sirfsocfb_set_layers(struct sirfsocfb *fb, struct sirfsocfb_layers_parms *param)
{
	LCD_SETPARAMS_DATA sSetData;
	struct layer_info *info = &fb->layer_info[0];
	int i, dirty_index = 0;

	mutex_lock(&info->layer_lock);

	for (i = 0; i < SIRFSOCFB_MAX_LAYERS; i++) {
		if (param->layer_mask & (1<<i))	{
			memset(&sSetData, 0, sizeof(sSetData));
			sSetData.eLayer = i;
			if (param->layer_info[dirty_index].enable) {
				if (param->phys_addr[i])
					sSetData.ui32Base = param->phys_addr[i];
				if (param->layer_info[dirty_index].format == FORMAT_RGB_565) {
					sSetData.eLcdFormat = LCD_PIXELFORMAT_565;
				} else if (param->layer_info[dirty_index].format == FORMAT_BGRA_8888) {
					sSetData.eLcdFormat = LCD_PIXELFORMAT_8888;
				} else if (param->layer_info[dirty_index].format == FORMAT_BGRX_8888) {
					sSetData.eLcdFormat = LCD_PIXELFORMAT_BGRX_8880;
				} else if (param->layer_info[dirty_index].format == FORMAT_YCbCr_420_P) {
					sSetData.eLcdFormat = LCD_PIXELFORMAT_I420;
				} else {
					FB_ERR_MSG("Unsupported format!\n");
					mutex_unlock(&info->layer_lock);
					return;
				}
				sSetData.i32SurfWidth = param->layer_info[dirty_index].width;
				sSetData.i32SurfHeight = param->layer_info[dirty_index].height;
				sSetData.sRectSrc.left = param->layer_info[dirty_index].src_rect.left;
				sSetData.sRectSrc.top = param->layer_info[dirty_index].src_rect.top;
				sSetData.sRectSrc.right = param->layer_info[dirty_index].src_rect.right;
				sSetData.sRectSrc.bottom = param->layer_info[dirty_index].src_rect.bottom;
				sSetData.sRectDst.left = param->layer_info[dirty_index].dst_rect.left;
				sSetData.sRectDst.top = param->layer_info[dirty_index].dst_rect.top;
				sSetData.sRectDst.right = param->layer_info[dirty_index].dst_rect.right;
				sSetData.sRectDst.bottom = param->layer_info[dirty_index].dst_rect.bottom;
				/* Now we disable overlay source alpha and global alpha default though its format is RGBA.
				 * Add it in future if this feature is required.
				 */
				sSetData.bPremultiAlpha = 1;

				if (!fb->lcd_func.pfnSetParameters(&sSetData)) {
					FB_ERR_MSG("Set parameters failed!\n");
					mutex_unlock(&info->layer_lock);
					return;
				}

				fb->lcd_func.pfnShowOverlay(i);
			} else {
				fb->lcd_func.pfnHideOverlay(i);
			}
			dirty_index++;
		} else {
			if (param->phys_addr[i] != 0)
				fb->lcd_func.pfnFlipOverlay(i, param->phys_addr[i], LCD_FLIP_FRAME);
		}
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
		sirfsocfb_set_toplayer(fb, toplayer);
		layer_disable(fb, layer);
		fb->layer_info[layer].feature = NORMAL_LAYER;
	} else {
		FB_ERR_MSG("layer %d has feature %d which is not among the supported\n",
			layer, fb->layer_info[layer].feature);
		return -EINVAL;
	}

	return 0;
}

#define ALIGN_SIZE(size, align) ((size + align - 1) & ~(align - 1))

static int __get_lcd_fmt(int fmt, int *is_yuv)
{
	LCD_PIXELFORMAT lcd_fmt = LCD_PIXELFORMAT_UNKNOWN;

	*is_yuv = 0;
	switch (fmt) {
	case FORMAT_RGB_565:
		lcd_fmt = LCD_PIXELFORMAT_565;
		break;
	case FORMAT_BGRA_8888:
	case FORMAT_BGRX_8888:
		/* for vpp blt purpose */
		lcd_fmt = LCD_PIXELFORMAT_BGRX_8880;
		break;
	case FORMAT_RGBA_8888:
	case FORMAT_RGBX_8888:
		lcd_fmt = LCD_PIXELFORMAT_RGBX_8880;
		break;
	case FORMAT_YCbCr_420_P:
		lcd_fmt = LCD_PIXELFORMAT_I420;
		*is_yuv = 1;
		break;
	case FORMAT_YCbYCr_422_I:
		lcd_fmt = LCD_PIXELFORMAT_YUYV;
		*is_yuv = 1;
		break;
	case FORMAT_CrYCbY_422_I:
		lcd_fmt = LCD_PIXELFORMAT_VYUY;
		*is_yuv = 1;
		break;
	case FORMAT_YCbCr_420_SP:
		lcd_fmt = LCD_PIXELFORMAT_NV12;
		*is_yuv = 1;
		break;
	case FORMAT_YCrCb_420_SP:
		lcd_fmt = LCD_PIXELFORMAT_NV21;
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
	case LCD_PIXELFORMAT_565:
	case LCD_PIXELFORMAT_556:
	case LCD_PIXELFORMAT_655:
		wstride_byte = ((2 * width + 7) / 8) * 8;
		hstride_byte = wstride_byte * height;
		*wstride_pixel = wstride_byte / 2;
		*hstride_pixel = hstride_byte / wstride_byte;
		break;
	case LCD_PIXELFORMAT_RGBX_8880:
	case LCD_PIXELFORMAT_8888:
	case LCD_PIXELFORMAT_BGRX_8880:
		wstride_byte = ((4 * width + 7) / 8) * 8;
		hstride_byte = wstride_byte * height;
		*wstride_pixel = wstride_byte / 4;
		*hstride_pixel = hstride_byte / wstride_byte;
		break;
	case LCD_PIXELFORMAT_NV12:
	case LCD_PIXELFORMAT_NV21:
		*wstride_pixel = ALIGN_SIZE(width, 64);
		*hstride_pixel = ALIGN_SIZE(height, 64);
		break;
	case LCD_PIXELFORMAT_I420:
		*wstride_pixel = ALIGN_SIZE(width, 16);
		*hstride_pixel = ALIGN_SIZE(height, 16);
		break;
	case LCD_PIXELFORMAT_YV12:
	case LCD_PIXELFORMAT_YUYV:
	case LCD_PIXELFORMAT_VYUY:
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
	VPP_SETPARAMS_DATA  vpp_parms;
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

	vpp_parms.eSrcFormat = __get_lcd_fmt(parms->src.fmt, &is_yuv);
	if (vpp_parms.eSrcFormat > 0) {
		if (!is_yuv) {
			FB_ERR_MSG("Src formt is not yuv!\n");
			return -EINVAL;
		}
	} else {
		FB_ERR_MSG("Unsupported src format!\n");
		return -EINVAL;
	}

	vpp_parms.eDstFormat = __get_lcd_fmt(parms->dst.fmt, &is_yuv);
	if (vpp_parms.eDstFormat > 0) {
		if (is_yuv) {
			FB_ERR_MSG("Dst formt is not rgb!\n");
			return -EINVAL;
		}
	} else {
		FB_ERR_MSG("Unsupported dst format!\n");
		return -EINVAL;
	}

	ret = __calculate_surf_layout(vpp_parms.eSrcFormat, parms->src.width,
		parms->src.height, &vpp_parms.uiSrcWStride_pixel,
		&vpp_parms.uiSrcHStride_pixel);

	if (ret != 0) {
		FB_ERR_MSG("Cannot calculate src surf layout!\n");
		return -EINVAL;
	}

	ret = __calculate_surf_layout(vpp_parms.eDstFormat, parms->dst.width,
		parms->dst.height, &vpp_parms.uiDstWStride_pixel,
		&vpp_parms.uiDstHStride_pixel);

	if (ret != 0) {
		FB_ERR_MSG("Cannot calculate dst surf layout!\n");
		return -EINVAL;
	}

	vpp_parms.ui32SrcBase = parms->src.base;
	vpp_parms.ui32DstBase = parms->dst.base;

	if (parms->flag & BLT_BOT_FIELD_FIRST)
		input_top_first = 0;

	di_mode = parms->flag & BLT_DI_MODE_MASK;

	if (parms->flag & BLT_FIELDS_MIX)
		field_offset = 0;
	else
		field_offset = parms->src.width * parms->src.height;

	switch (di_mode) {
	case BLT_DI_NONE:
		fb->vpp_func->pfnSetInterlace(FALSE, VPP_OUTPUT_P_SINGLE, TRUE, TRUE,
			VPP_DI_WEAVE, TRUE, 0);
		break;
	case BLT_DI_WEAVE:
		fb->vpp_func->pfnSetInterlace(TRUE, VPP_OUTPUT_P_SINGLE, TRUE, TRUE,
			VPP_DI_WEAVE, input_top_first, field_offset);
		break;
	case BLT_DI_3MEDIAN:
		fb->vpp_func->pfnSetInterlace(TRUE, VPP_OUTPUT_P_SINGLE, TRUE, TRUE,
			VPP_DI_3MEDIAN, input_top_first, field_offset);
		break;
	case BLT_DI_VMRI:
		fb->vpp_func->pfnSetInterlace(TRUE, VPP_OUTPUT_P_SINGLE, TRUE, TRUE,
			VPP_DI_VMRI, input_top_first, field_offset);
		break;
	case BLT_DI_INTRA_FIELD_SPATIAL:
	default:
		FB_ERR_MSG("Unsupported deinterlace mode!\n");
		return -EINVAL;

	}

	fb->vpp_func->pfnSetParames(&vpp_parms);
	fb->vpp_func->pfnSetSize((RECT *)&parms->src.rect, (RECT *)&parms->dst.rect);
	fb->vpp_func->pfnStart(FALSE);

	if (!(parms->flag & BLT_NOT_WAIT_COMPLETE))
		while (fb->vpp_func->pfnIsBusy()) {
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

	fb->lcd_func.pfnFlipOverlay(layer, info->fix.smem_start + byte_offset, LCD_FLIP_FRAME);

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
		struct sirfsocfb_flush_cache_addr flush_cache_addr;
		struct sirfsocfb_layers_parms layers;
		int feature_layer;
	} data;
	int ret = 0;

	switch (cmd) {
	case SIRFSOCFB_SET_TOPLAYER:
		sirfsocfb_set_toplayer(fb, layer);
		break;
	case SIRFSOCFB_GET_TOPLAYER:
		{
			int layer = fb->lcd_func.pfnGetTopLayer();
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
	case SIRFSOCFB_DUMP_REGISTER:
		fb->lcd_func.pfnPrintRegister();
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

	if (layer != LCD_PRIMARY)
		return 0;

	if (info->fix.visual == FB_VISUAL_TRUECOLOR)
		((u32 *)(info->pseudo_palette))[regno] =
			(red << info->var.red.offset)	  |
			(green << info->var.green.offset) |
			(blue << info->var.blue.offset);

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

	intr_status = fb->lcd_func.pfnIsInterrupted(LCD_INTERRUPT_ALL);
	fb->lcd_func.pfnClearInterrupt(LCD_INTERRUPT_ALL);

	/* handle oflow interrupts */
	for (index = 0; index < SIRFSOCFB_MAX_LAYERS; index++) {
		if (intr_status & (1 << (LCD_INTERRUPT_L0_OFLOW + index))) {
			/* overflow count  */
			fb->layer_info[index].fifo_overflow++;
			ret = IRQ_HANDLED;
		}
	}

	/* handle uflow interrupts */
	for (index = 0; index < SIRFSOCFB_MAX_LAYERS; index++) {
		if (intr_status & (1 << (LCD_INTERRUPT_L0_UFLOW + index))) {
			/* overflow count  */
			fb->layer_info[index].fifo_underflow++;
			ret = IRQ_HANDLED;
		}
	}

	/* handle vsync interrupt */
	if (intr_status & (1 << LCD_INTERRUPT_VSYNC)) {
		queue_work(fb->flip_wq, &fb->work);
		ret = IRQ_HANDLED;
	}

	return ret;
}

/**************** INIT/DEINIT ROUTINES ****************/
static int sirfsocfb_irq_init(struct sirfsocfb *fb)
{
	FB_FUN_MSG("sirfsocfb_irq_init\n");

	fb->lcd_func.pfnClearInterrupt(LCD_INTERRUPT_ALL);
	fb->lcd_func.pfnDisableInterrupt(LCD_INTERRUPT_ALL);

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

	return 0;
}

static void sirfsocfb_irq_deinit(struct sirfsocfb *fb)
{
	FB_FUN_MSG("sirfsocfb_irq_deinit\n");

	fb->lcd_func.pfnDisableInterrupt(LCD_INTERRUPT_ALL);
	fb->lcd_func.pfnClearInterrupt(LCD_INTERRUPT_ALL);
	free_irq(fb->irq, fb);
	destroy_workqueue(fb->flip_wq);
}

static int sirfsocfb_register(struct sirfsocfb *fb)
{
	int ret, layer;

	FB_FUN_MSG("sirfsocfb_register\n");

	for (layer = 0; layer < SIRFSOCFB_MAX_LAYERS; layer++) {
		struct fb_var_screeninfo var = { 0 };

		if (!fb->layer_info[layer].valid)
			continue;

		fb->layer_info[layer].registered = 0;

		fb->layer_info[layer].pOvl = NULL;

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
		fb->fb[layer].fix.accel = FB_ACCEL_NONE;

		var.xres = fb->panel->mode.xres;
		var.yres = fb->panel->mode.yres;
		var.xres_virtual = fb->panel->mode.xres;
		var.yres_virtual = fb->panel->mode.yres * 2;
		var.bits_per_pixel = fb->panel->bpp;

		fb->fb[layer].fix.line_length =
		    var.xres_virtual * SIRFSOCFB_BYTES(var.bits_per_pixel);

		var.grayscale = fb->panel->grayscale;
		var.pixclock = fb->panel->mode.pixclock;
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

		if ((layer == LCD_PRIMARY)) {
			fb->fb[layer].pseudo_palette = &sirfsocfb_pseudo_palette;

			/* allocate a colormap for layer0 */
			if (fb_alloc_cmap(&fb->fb[layer].cmap, 16, 0))
				return -ENOMEM;

			/* enable bootsplash layer if not enabled by the bootloader */
			if (!fb->init_enabled) {
				var.activate = FB_ACTIVATE_NOW;
				ret = fb_set_var(&fb->fb[layer], &var);
				if (ret)
					return ret;
			} else {
				/* already enabled by the bootloader so just set the
				 * activate field */
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
			FB_ERR_MSG
			    ("Cannot register framebuffer device for layer %d\n",
			     layer);
			return ret;
		}

		/* Enable layer overflow and underflow interrupts.  */
		fb->lcd_func.pfnEnableInterrupt(LCD_INTERRUPT_L0_OFLOW + layer);
		fb->lcd_func.pfnEnableInterrupt(LCD_INTERRUPT_L0_UFLOW + layer);

		fb->layer_info[layer].registered = 1;
		fb->layer_info[layer].feature = NORMAL_LAYER;
		FB_NOT_MSG("layer%d registered!\n", layer);
	}

	mutex_init(&fb->ovl_lock);
	fb->lcd_func.pfnEnableInterrupt(LCD_INTERRUPT_VSYNC);

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
		fb->lcd_func.pfnDisableInterrupt(LCD_INTERRUPT_ALL);

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

static int remap_frame_buffers(struct platform_device *dev,
			       struct sirfsocfb *fb)
{
	int i, ret = 0;
	int layer_mem_offset = 0;

	FB_FUN_MSG("remap_frame_buffers\n");

	for (i = 0; i < SIRFSOCFB_MAX_LAYERS; i++) {
		if (!fb->layer_info[i].valid)
			continue;

		fb->fb[i].fix.smem_start = sirf_fb_phy_base+layer_mem_offset;
		fb->fb[i].fix.smem_len = fb->panel->bpp/8 *
			fb->panel->mode.xres * fb->panel->mode.yres *4;
		layer_mem_offset += fb->fb[i].fix.smem_len;
		fb->fb[i].screen_base = ioremap_wc(fb->fb[i].fix.smem_start,
			fb->fb[i].fix.smem_len);

		if (fb->fb[i].screen_base == NULL) {
			FB_ERR_MSG("L%d IO remap failed!\n", i);
			ret = -ENOMEM;
			break;
		}

		if (i == LCD_PRIMARY) {
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
	int index = 0;

        if (lcd_client) {
		if (vcc != 0) {
			index = vcc&0x0000FFFF;
			i2c_smbus_write_byte_data(lcd_client, index, 0x1);
		}
		msleep(50);
		index = vdd&0x0000FFFF;
		i2c_smbus_write_byte_data(lcd_client, index, 0x1);
	}
}

static void std_post_enable(void)
{
	int index = 0;

	if (lcd_client) {
		msleep(200);
		index = vee&0x0000FFFF;
		i2c_smbus_write_byte_data(lcd_client, index, 0x1);
	}
}

static void std_pre_disable(void)
{
	if (lcd_client)
		i2c_smbus_write_byte_data(lcd_client, vee>>16, 0x1);

}

static void std_post_disable(void)
{
	if (lcd_client) {
		i2c_smbus_write_byte_data(lcd_client, vdd>>16, 0x1);
		if (vcc != 0) {
			i2c_smbus_write_byte_data(lcd_client, vcc>>16, 0x1);
		}
	}
}
static void reset(void)
{
}

static void param_prepare(struct sirfsocfb *fb, LCD_PANEL_INFO * pPanel)
{
	memset(pPanel, 0, sizeof(*pPanel));

	pPanel->ui32HsyncPeriod =
	    fb->panel->mode.xres + fb->panel->mode.hsync_len +
	    fb->panel->mode.left_margin + fb->panel->mode.right_margin - 1;
	pPanel->ui32HsyncWidth = fb->panel->mode.hsync_len - 1;
	pPanel->ui32VsyncPeriod =
	    fb->panel->mode.yres + fb->panel->mode.vsync_len +
	    fb->panel->mode.upper_margin + fb->panel->mode.lower_margin - 1;
	pPanel->ui32VsyncWidth = fb->panel->mode.vsync_len - 1;
	pPanel->ui32HStart =
	    fb->panel->mode.hsync_len + fb->panel->mode.left_margin - 12;
	pPanel->ui32VStart =
	    fb->panel->mode.vsync_len + fb->panel->mode.upper_margin;
	pPanel->ui32HEnd = pPanel->ui32HStart + fb->panel->mode.xres - 1;
	pPanel->ui32VEnd = pPanel->ui32VStart + fb->panel->mode.yres - 1;

	pPanel->eOutFormat = LCD_OUT_24BIT_RBG888;
	pPanel->ui32RGBSequence = RGB_SEQ_RGB;
	if (fb->panel->timing & PANEL_PCLK_POLAR)
		pPanel->bPClkPolar = 1;
	if (fb->panel->timing & PANEL_PCLK_EDGE)
		pPanel->bPClkEdge = 1;
	if (fb->panel->timing & PANEL_HSYNC_POLAR)
		pPanel->bHSyncPolar = 1;
	if (fb->panel->timing & PANEL_VSYNC_POLAR)
		pPanel->bVSyncPolar = 1;

	pPanel->bIOMaster = TRUE;

	pPanel->ui32SysClock = clk_get_rate(fb->clk);
	pPanel->ui32FreshRate = fb->panel->mode.pixclock /
	    (pPanel->ui32HsyncPeriod + 1) / (pPanel->ui32VsyncPeriod + 1);
	pPanel->pfnPrePowerUp = fb->panel->enable_pre;
	pPanel->pfnPostPowerUp = fb->panel->enable_post;
	pPanel->pfnPrePowerDown = fb->panel->disable_pre;
	pPanel->pfnPostPowerDown = fb->panel->disable_post;
	pPanel->pfnReset = reset;

	pPanel->eMaxLayer = SIRFSOCFB_MAX_LAYERS - 1;
	pPanel->eLayer = LCD_PRIMARY;

}

void  __init sirfsoc_fb_reserve_memblock(void)
{
	sirf_fb_phy_size = 30 * SZ_1M;
	sirf_fb_phy_base = memblock_alloc(sirf_fb_phy_size, PAGE_SIZE);
	memblock_remove(sirf_fb_phy_base, sirf_fb_phy_size);
}
EXPORT_SYMBOL(sirfsoc_fb_reserve_memblock);

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
	int bl_gpio, vcc_gpio;
	LCD_PANEL_INFO panel_info;

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
		goto err_free_fb;
	}
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		FB_ERR_MSG("Fail to get lcd regs resource!\n");
		ret = -EINVAL;
		goto err_free_panel;
	}

	fb->base = devm_request_and_ioremap(&pdev->dev, res);
	if (fb->base == NULL) {
		FB_ERR_MSG("Fail to remap lcd regs!\n");
		ret = -ENOMEM;
		goto err_free_panel;
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

	sirfsoc_reset_device(&pdev->dev);

	if (of_property_read_u32(pdev->dev.of_node, "layer-ctrl",
				&layer_ctrl)) {
		dev_err(&pdev->dev, "get valid layer failed,only enable primary\n");
		layer_ctrl = 0x00000001;
	}

	get_layer_ctrl_info(fb, layer_ctrl);

	/* later bl should be managed by pwm */
	bl_gpio = of_get_named_gpio(pdev->dev.of_node, "bl-gpios", 0);
	if (gpio_is_valid(bl_gpio)) {
		ret = devm_gpio_request(&pdev->dev, bl_gpio, "sirfsoc_backlight");
		if (ret) {
			dev_err(&pdev->dev, "request backlight gpio failed\n");
			ret = -ENODEV;
			goto err_unmap;
		}
		gpio_direction_output(bl_gpio, 1);
	}

	if (of_device_is_compatible(pdev->dev.of_node, "sirf,prima2")) {
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
	}

	if (of_property_read_u32(pdev->dev.of_node, "power-vdd", &vdd)) {
		dev_err(&pdev->dev, "get lcd vdd failed\n");
		ret = -ENODEV;
		goto err_unmap;
	}

	if (of_property_read_u32(pdev->dev.of_node, "power-vee", &vee)) {
		 dev_err(&pdev->dev, "get lcd vee failed\n");
		 ret = -ENODEV;
		goto err_unmap;
	}

	if (of_property_read_u32(pdev->dev.of_node, "power-vcc", &vcc)) {
		vcc = 0;
	}

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

	LCD_GetFuncTable(&fb->lcd_func);

	/* Check if LCD preinited by uboot */
	fb->init_enabled =
	    fb->lcd_func.pfnInitialize(fb->base, fb->vpp_base, 0,
		    bpp, &panel_info);
	fb->vpp_func = fb->lcd_func.pfnGetVppTable();

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
		goto err_remove_dev_file;


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

#if !defined(CONFIG_FRAMEBUFFER_CONSOLE) && defined(CONFIG_LOGO)
	fb_prepare_logo(&fb->fb[LCD_PRIMARY], 0);
	fb_show_logo(&fb->fb[LCD_PRIMARY], 0);
#endif
	layer_enable(fb, LCD_PRIMARY);

	FB_FUN_MSG("-sirfsocfb_probe\n");
	return;
err_remove_dev_file:
	device_remove_file(&pdev->dev,
		&dev_attr_layer_fifo_underflow);
err_unregister:
	sirfsocfb_unregister(fb);
	iounmap(fb->fb[LCD_PRIMARY].screen_base);
err_deinit_irq:
	sirfsocfb_irq_deinit(fb);
err_rel_clk:
	clk_disable(fb->clk);
	clk_put(fb->clk);
	clk_disable(fb->vpp_clk);
	clk_put(fb->vpp_clk);
err_unmap:
	devm_iounmap(&pdev->dev, fb->base);
err_free_panel:
	devm_kfree(&pdev->dev, fb->panel);
err_free_fb:
	devm_kfree(&pdev->dev, fb);
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
	for (i = 0; i < SIRFSOCFB_MAX_LAYERS; i++)
		kfree(fb->layer_info[i].pOvl);

	fb->lcd_func.pfnTerminate();
	fb->init_enabled = 0;
	device_remove_file(&pdev->dev, &dev_attr_layer_fifo_overflow);
	device_remove_file(&pdev->dev, &dev_attr_layer_fifo_underflow);
	sirfsocfb_irq_deinit(fb);

	clk_disable(fb->clk);
	clk_put(fb->clk);
	iounmap(fb->base);

	devm_kfree(&pdev->dev, fb->panel);
	devm_kfree(&pdev->dev, fb);

	FB_NOT_MSG("framebuffer driver unregistered!\n");
	return 0;
}

int sirfsocfb_km_blt_yuv2rgb(struct fb_info *info, struct sirfsocfb_bltparms *parms)
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

	disable_irq(fb->irq);

	fb->lcd_func.pfnSleep();
	clk_disable(fb->clk);
	clk_disable(fb->vpp_clk);

	return 0;
}

static int sirfsocfb_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sirfsocfb *fb = platform_get_drvdata(pdev);
	FB_FUN_MSG("sirfsocfb_resume\n");

	clk_enable(fb->clk);
	clk_enable(fb->vpp_clk);

	/* Check if LCD preinited by uboot */
	fb->init_enabled = fb->lcd_func.pfnWakeup();
	enable_irq(fb->irq);
	FB_NOT_MSG("LCD resumed\n");

	return 0;
}

static int sirfsocfb_freeze(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sirfsocfb *fb = platform_get_drvdata(pdev);
	FB_FUN_MSG("sirfsocfb_freeze\n");

	sirfsocfb_irq_deinit(fb);

	return 0;
}

static int sirfsocfb_restore(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sirfsocfb *fb = platform_get_drvdata(pdev);
	FB_NOT_MSG("LCD restore\n");

	sirfsocfb_irq_init(fb);

	fb->lcd_func.pfnEnableInterrupt(LCD_INTERRUPT_VSYNC);

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

module_platform_driver(sirfsocfb_driver);

MODULE_DESCRIPTION("SiRF SoC Frame buffer driver");
MODULE_AUTHOR("Ramya Segar");
MODULE_LICENSE("GPL");
