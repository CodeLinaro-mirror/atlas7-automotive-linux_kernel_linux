/*
 * CSR sirfsoc vdss vpp driver
 *
 * Copyright (c) 2014-2016, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define VDSS_SUBSYS_NAME "VPP"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/suspend.h>

#include <video/sirfsoc_vdss.h>
#include "vdss.h"
#include "vpp.h"

#define NUM_VPP 2

struct vpp_info {
	struct vdss_vpp_op_params params;
	bool is_dirty;
};

struct vpp_device {
	enum vdss_vpp vpp_id;
	enum vdss_vpp_op_type op;
	struct vpp_info info;
	sirfsoc_vpp_notify_t func;
	void *arg;
	struct list_head head;
};

#define VPP_MAX_DEVICES	8

static const s8 sin[91] = {0, 2, 3, 5, 7, 9, 10, 12,
	14, 16, 17, 19, 21, 22, 24, 26, 28, 29, 31,
	33, 34, 38, 37, 39, 41, 42, 44, 45, 47, 48,
	50, 52, 53, 54, 56, 57, 59, 60, 62, 63, 64,
	66, 67, 68, 69, 71, 72, 73, 74, 75, 77, 78,
	79, 80, 81, 82, 83, 84, 85, 86, 87, 87, 88,
	89, 90, 91, 91, 92, 93, 93, 94, 95, 95, 96,
	96, 97, 97, 97, 98, 98, 98, 99, 99, 99, 99,
	100, 100, 100, 100, 100, 100,};

static const s8 cos[91] = {100, 100, 100, 100, 100,
	100, 99, 99, 99, 99, 98, 98, 98, 97, 97, 97,
	96, 96, 95, 95, 94, 93, 93, 92, 91, 91, 90,
	89, 88, 87, 87, 86, 85, 84, 83, 82, 81, 80,
	79, 78, 77, 75, 74, 73, 72, 71, 69, 68, 67,
	66, 64, 63, 62, 60, 59, 57, 56, 54, 53, 51,
	50, 48, 47, 45, 44, 42, 41, 39, 37, 36, 34,
	33, 31, 29, 28, 26, 24, 23, 21, 19, 17, 16,
	14, 12, 10, 9, 7, 5, 3, 2, 0,};

struct vpp_irq {
	spinlock_t irq_lock;
	struct completion comp;
};

struct vpp_adapter {
	/* static fields */
	unsigned char name[8];
	enum vdss_vpp id;

	struct platform_device *pdev;
	void __iomem    *base;

	int irq;
	struct clk *clk;
	bool is_atlas7;

	struct list_head devices;
	struct vpp_device *cur_dev;

	struct vpp_irq vpp_irq;
};

static struct vpp_adapter vpp[NUM_VPP];

static const u32 tap_filter_coeff[] = {
	0x00000000,
	0x00001000,
	0x00000000,
	0x7f3f002f,
	0x00e50fe3,
	0x00017fc6,
	0x7ea40053,
	0x01ee0f8f,
	0x00077f83,
	0x7e2f006c,
	0x03150f05,
	0x00117f39,
	0x7ddf007b,
	0x04560e48,
	0x001e7eea,
	0x7db10080,
	0x05aa0d5d,
	0x002e7e9a,
	0x7da2007c,
	0x07090c49,
	0x00407e4e,
	0x7db00072,
	0x086b0b15,
	0x00527e0b,
	0x7dd40064,
	0x09c809c8,
	0x00647dd4,
	0x10000000,
	0x00000000,
	0x0fdb7f72,
	0x7ffc00b7,
	0x0f717f0b,
	0x7fef0195,
	0x0ec77eca,
	0x7fd80298,
	0x0de57ea9,
	0x7fb803ba,
	0x0cd67ea4,
	0x7f9004f6,
	0x0ba47eb5,
	0x7f620645,
	0x0a597ed5,
	0x7f3107a1,
	0x09007f00,
	0x7f000900,
};

static const u32 rgb_yuv_coeff[] = {
	0x0199, 0x0000, 0x12A,/* V, U, Y for R*/
	0x00D0, 0x0064, 0x12A,/* V, U, Y for G*/
	0x0000, 0x0204, 0x12A,/* V, U, Y for B*/
};

static const u32 rgb_yuv_coeff_nj12[] = {
	0x0198, 0x0000, 0x100,/* V, U, Y for R*/
	0x00D0, 0x0064, 0x100,/* V, U, Y for G*/
	0x0000, 0x0204, 0x100,/* V, U, Y for B*/
};

static const u32 rgb_offsets[] = {
	0xdf20,
	0x8760,
	0x114a0,
};

static const u32 rgb_offsets_nj12[] = {
	0xcc00,
	0x9a00,
	0x10200,
};

static const u32 y_top_addr_regs[] = {
	VPP_YBASE,
	VPP_YBASE1,
	VPP_YBASE2,
};

static const u32 y_bot_addr_regs[] = {
	VPP_YBASE_BOT,
	VPP_YBASE1_ADDR_BOT,
	VPP_YBASE2_ADDR_BOT,
};

/* protects vpp_data */
static DEFINE_SPINLOCK(data_lock);

static unsigned int vpp_read_reg(struct vpp_adapter *adapter,
				unsigned int offset)
{
	return readl(adapter->base + offset);
}

static void vpp_write_reg(struct vpp_adapter *adapter,
			unsigned int offset,
			unsigned int value)
{
	writel(value, adapter->base + offset);
}

static void vpp_write_reg_with_mask(
			struct vpp_adapter *adapter,
			unsigned int offset,
			unsigned int value,
			unsigned int mask)
{
	u32 tmp;

	tmp = vpp_read_reg(adapter, offset);
	tmp &= mask;
	tmp |= (value & ~mask);
	vpp_write_reg(adapter, offset, tmp);
}

static s32 vpp_cal_vc(u32 hue, u32 saturation)
{
	s32 vc;
	s32 tmp;

	if (hue > 90 && hue <= 180)
		tmp = sin[180 - hue];
	else if (hue > 180 && hue <= 270)
		tmp = -sin[hue - 180];
	else if (hue > 270 && hue <= 360)
		tmp = -sin[360 - hue];
	else
		tmp = sin[hue];

	/*
	 * According to vpp spec, vc is calculated by
	 * below formula:
	 * vc = cos(2*PI*(hue/360.0)) * saturation * 2
	 */
	vc = tmp * saturation * 2 / 100;

	return vc;
}

static s32 vpp_cal_uc(u32 hue, u32 saturation)
{
	s32 uc;
	s32 tmp;

	if (hue > 90 && hue <= 180)
		tmp = -cos[hue - 90];
	else if (hue > 180 && hue <= 270)
		tmp = -cos[270 - hue];
	else if (hue > 270 && hue <= 360)
		tmp = cos[360 - hue];
	else
		tmp = cos[hue];

	/*
	 * According to vpp spec, uc is calculated by
	 * below formula:
	 * uc = sin(2*PI*(hue/360.0)) * saturation * 2
	 */
	uc = tmp * saturation * 2 / 100;

	return uc;
}

static bool vpp_blt_check_size(struct vdss_surface *src_surf,
	struct vdss_rect *src_rect,
	struct vdss_surface *dst_surf,
	struct vdss_rect *dst_rect)
{
	int src_rect_width, src_rect_height;
	int dst_rect_width, dst_rect_height;
	int pixel_aligned = 0;
	int src_skip = 0, dst_skip = 0;

	src_rect_width = src_rect->right - src_rect->left + 1;
	src_rect_height = src_rect->bottom - src_rect->top + 1;
	dst_rect_width = dst_rect->right - dst_rect->left + 1;
	dst_rect_height = dst_rect->bottom - dst_rect->top + 1;

	/*
	 * If the src rect is out the range of src surface
	 * or the dst rect is out the range of the screen,
	 * they are invalid inputs, return false
	 * */
	if (src_rect->right < 0 || src_rect->bottom < 0 ||
		dst_rect->right < 0 || dst_rect->bottom < 0) {
		VDSSWARN("source or destination rect is out of range\n");
		return false;
	}

	if (src_rect->left >= src_surf->width ||
		src_rect->top >= src_surf->height ||
		dst_rect->left >= dst_surf->width ||
		dst_rect->top >= dst_surf->height) {
		VDSSWARN("source or destination rect is out of range\n");
		return false;
	}

	/* check and update the source rect */
	if (src_rect->left < 0) {
		dst_rect->left = dst_rect->left +
			(dst_rect_width * (-src_rect->left) /
			src_rect_width);
		src_rect->left = 0;
	}

	if (src_rect->top < 0) {
		dst_rect->top = dst_rect->top +
			(dst_rect_height * (-src_rect->top) /
			src_rect_height);
		src_rect->top = 0;
	}

	if (src_rect->right >= src_surf->width) {
		dst_rect->right = dst_rect->right -
			(dst_rect_width *
			(src_rect->right - src_surf->width + 1) /
			src_rect_width);
		src_rect->right = src_surf->width - 1;
	}

	if (src_rect->bottom >= src_surf->height) {
		dst_rect->bottom = dst_rect->bottom -
			(dst_rect_height *
			(src_rect->bottom - src_surf->height + 1) /
			src_rect_height);
		src_rect->bottom = src_surf->height - 1;
	}

	/* check and update the destination rect */
	if (dst_rect->left < 0) {
		src_rect->left = src_rect->left +
			(src_rect_width * (-dst_rect->left) /
			dst_rect_width);
		dst_rect->left = 0;
	}

	if (dst_rect->top < 0) {
		src_rect->top = src_rect->top +
			(src_rect_height * (-dst_rect->top) /
			dst_rect_height);
		dst_rect->top = 0;
	}

	if (dst_rect->right >= dst_surf->width) {
		src_rect->right = src_rect->right -
			(src_rect_width *
			(dst_rect->right - dst_surf->width + 1) /
			dst_rect_width);
		dst_rect->right = dst_surf->width - 1;
	}

	if (dst_rect->bottom >= dst_surf->height) {
		src_rect->bottom = src_rect->bottom -
			(src_rect_height *
			(dst_rect->bottom - dst_surf->height + 1) /
			dst_rect_height);
		dst_rect->bottom = dst_surf->height - 1;
	}

	switch (src_surf->fmt) {
	case VDSS_PIXELFORMAT_NV12:
	case VDSS_PIXELFORMAT_NV21:
	case VDSS_PIXELFORMAT_NJ12:
		pixel_aligned = 8;
		break;
	case VDSS_PIXELFORMAT_I420:
	case VDSS_PIXELFORMAT_Q420:
	case VDSS_PIXELFORMAT_YV12:
		pixel_aligned = 16;
		break;
	case VDSS_PIXELFORMAT_IMC1:
	case VDSS_PIXELFORMAT_IMC2:
	case VDSS_PIXELFORMAT_IMC3:
	case VDSS_PIXELFORMAT_IMC4:
		pixel_aligned = 16;
		break;
	case VDSS_PIXELFORMAT_UYVY:
	case VDSS_PIXELFORMAT_UYNV:
	case VDSS_PIXELFORMAT_YUY2:
	case VDSS_PIXELFORMAT_YUYV:
	case VDSS_PIXELFORMAT_YUNV:
	case VDSS_PIXELFORMAT_YVYU:
	case VDSS_PIXELFORMAT_VYUY:
	case VDSS_PIXELFORMAT_565:
		pixel_aligned = 4;
		break;
	case VDSS_PIXELFORMAT_8888:
	case VDSS_PIXELFORMAT_BGRX_8880:
	case VDSS_PIXELFORMAT_RGBX_8880:
		pixel_aligned = 2;
		break;
	default:
		pixel_aligned = 16;
		break;
	}

	src_rect_width = src_rect->right - src_rect->left + 1;
	src_rect_height = src_rect->bottom - src_rect->top + 1;
	dst_rect_width = dst_rect->right - dst_rect->left + 1;
	dst_rect_height = dst_rect->bottom - dst_rect->top + 1;

	/*
	 * Because downscaling in horizontal/vertical direction should
	 * be no less than 1/8 and upscaling in horizontal/vertical
	 * direction should be no greater than 8, driver update ratio of
	 * scaling for this hardware(VPP) limitatin.
	 * */
	if (src_rect_width > (dst_rect_width << 3)) {
		src_rect_width = (dst_rect_width << 3);
		src_rect->right = src_rect->left +
			src_rect_width - 1;
	}

	if (src_rect_height > (dst_rect_height << 3)) {
		src_rect_height = (dst_rect_height << 3);
		src_rect->bottom = src_rect->top +
			src_rect_height - 1;
	}

	if (dst_rect_width > (src_rect_width << 3)) {
		dst_rect_width = (src_rect_width << 3);
		dst_rect->right = dst_rect->left +
			dst_rect_width - 1;
	}

	if (dst_rect_height > (src_rect_height << 3)) {
		dst_rect_height = (src_rect_height << 3);
		dst_rect->bottom = dst_rect->top +
			dst_rect_height - 1;
	}

	/*
	 * the start address of source need 8 bytes aligned,
	 * so driver should update the src rect for request
	 * */
	src_skip = src_rect->left & (pixel_aligned - 1);
	if (src_skip) {
		dst_skip = src_skip * dst_rect_width /
			src_rect_width;
	}

	src_rect->left -= src_skip;
	dst_rect->left -= dst_skip;

	if (dst_rect->left < 0)
		dst_rect->left = 0;

	return true;
}

bool vpp_passthrough_check_size(struct vdss_surface *src_surf,
	struct vdss_rect *src_rect,
	int *psrc_skip,
	struct vdss_rect *dst_rect,
	int *pdst_skip)
{
	int src_rect_width, src_rect_height;
	int dst_rect_width, dst_rect_height;
	int pixel_aligned = 0;
	int src_skip = 0;
	int dst_skip = 0;

	/*
	 * DMA address must be 8 bytes aligned, so
	 * we must shift the src address
	 * */
	switch (src_surf->fmt) {
	case VDSS_PIXELFORMAT_NV12:
	case VDSS_PIXELFORMAT_NV21:
	case VDSS_PIXELFORMAT_NJ12:
		pixel_aligned = 8;
		break;
	case VDSS_PIXELFORMAT_I420:
	case VDSS_PIXELFORMAT_Q420:
	case VDSS_PIXELFORMAT_YV12:
		pixel_aligned = 16;
		break;
	case VDSS_PIXELFORMAT_IMC1:
	case VDSS_PIXELFORMAT_IMC2:
	case VDSS_PIXELFORMAT_IMC3:
	case VDSS_PIXELFORMAT_IMC4:
		pixel_aligned = 16;
		break;
	case VDSS_PIXELFORMAT_UYVY:
	case VDSS_PIXELFORMAT_UYNV:
	case VDSS_PIXELFORMAT_YUY2:
	case VDSS_PIXELFORMAT_YUYV:
	case VDSS_PIXELFORMAT_YUNV:
	case VDSS_PIXELFORMAT_YVYU:
	case VDSS_PIXELFORMAT_VYUY:
	case VDSS_PIXELFORMAT_565:
		pixel_aligned = 4;
		break;
	case VDSS_PIXELFORMAT_8888:
	case VDSS_PIXELFORMAT_BGRX_8880:
	case VDSS_PIXELFORMAT_RGBX_8880:
		pixel_aligned = 2;
		break;
	default:
		pixel_aligned = 16;
		break;
	}

	src_rect_width = src_rect->right - src_rect->left + 1;
	src_rect_height = src_rect->bottom - src_rect->top + 1;
	dst_rect_width = dst_rect->right - dst_rect->left + 1;
	dst_rect_height = dst_rect->bottom - dst_rect->top + 1;

	/*
	 * Because downscaling in horizontal/vertical direction should
	 * be no less than 1/8 and upscaling in horizontal/vertical
	 * direction should be no greater than 8, driver update ratio of
	 * scaling for this hardware(VPP) limitatin.
	 * */
	if (src_rect_width > (dst_rect_width << 3)) {
		src_rect_width = (dst_rect_width << 3);
		src_rect->right = src_rect->left +
			src_rect_width - 1;
	}

	if (src_rect_height > (dst_rect_height << 3)) {
		src_rect_height = (dst_rect_height << 3);
		src_rect->bottom = src_rect->top +
			src_rect_height - 1;
	}

	if (dst_rect_width > (src_rect_width << 3)) {
		dst_rect_width = (src_rect_width << 3);
		dst_rect->right = dst_rect->left +
			dst_rect_width - 1;
	}

	if (dst_rect_height > (src_rect_height << 3)) {
		dst_rect_height = (src_rect_height << 3);
		dst_rect->bottom = dst_rect->top +
			dst_rect_height - 1;
	}

	/*
	 * In the passthrough mode, the start address of source need
	 * 8 bytes aligned, so driver should update the src rect for
	 * request. dst_skip size will be used by LCDC to skip data
	 * from VPP which pixels will not be shown on the screen
	 * */
	src_skip = src_rect->left & (pixel_aligned - 1);
	if (src_skip) {
		dst_skip = src_skip * dst_rect_width /
			src_rect_width;
	}

	src_rect->left -= src_skip;
	dst_rect->left -= dst_skip;

	if (dst_rect->left < 0) {
		dst_rect->left = 0;
		dst_skip = 0;
	}

	*psrc_skip = src_skip;
	*pdst_skip = dst_skip;

	return true;
}

static void __vpp_setup(struct vpp_adapter *adapter)
{
	u32 offset, val;
	int i;

	vpp_write_reg(adapter, VPP_FULL_THRESH, VPP_FIFO_FULL_THRESH(0x8));

	offset = VPP_HSCA_COEF00;
	for (i = 0; i < ARRAY_SIZE(tap_filter_coeff); i++) {
		vpp_write_reg(adapter, offset, tap_filter_coeff[i]);
		offset += 4;
	}

	offset = VPP_RCOEF;
	for (i = 0; i < ARRAY_SIZE(rgb_yuv_coeff); i += 3) {
		val = rgb_yuv_coeff[i] |
		      (rgb_yuv_coeff[i+1] << 10) |
		      (rgb_yuv_coeff[i+2] << 20);
		vpp_write_reg(adapter, offset, val);
		offset += 4;
	}

	vpp_write_reg(adapter, VPP_OFFSET1, rgb_offsets[0]);
	vpp_write_reg(adapter, VPP_OFFSET2, rgb_offsets[1]);
	vpp_write_reg(adapter, VPP_OFFSET3, rgb_offsets[2]);

	val = VPP_COLOR_B_CTRL(0x0) | VPP_COLOR_C_CTRL(0x80);
	vpp_write_reg(adapter, VPP_COLOR_BC_CTRL, val);

	val = VPP_COLOR_UC_CTRL(0x100) | VPP_COLOR_VC_CTRL(0x0);
	vpp_write_reg(adapter, VPP_COLOR_HS_CTRL, val);
}

static void __vpp_set_src_rect(struct vpp_adapter *adapter,
		struct vdss_rect *src_rect)
{
	u32 src_width = src_rect->right - src_rect->left + 1;
	u32 src_height = src_rect->bottom - src_rect->top + 1;


	vpp_write_reg_with_mask(adapter, VPP_WIDTH,
			VPP_SRC_WIDTH(src_width), ~VPP_SRC_WIDTH_MASK);
	vpp_write_reg_with_mask(adapter, VPP_HEIGHT,
			VPP_SRC_HEIGHT(src_height), ~VPP_SRC_HEIGHT_MASK);
}

static void __vpp_set_dst_rect(struct vpp_adapter *adapter,
		struct vdss_rect *dst_rect)
{
	u32 dst_width = dst_rect->right - dst_rect->left + 1;
	u32 dst_height = dst_rect->bottom - dst_rect->top + 1;


	vpp_write_reg_with_mask(adapter, VPP_WIDTH,
			VPP_DES_WIDTH(dst_width), ~VPP_DES_WIDTH_MASK);
	vpp_write_reg_with_mask(adapter, VPP_HEIGHT,
			VPP_DES_HEIGHT(dst_height), ~VPP_DES_HEIGHT_MASK);
}

static int __vpp_setup_src(struct vpp_adapter *adapter,
			struct vdss_surface *surf,
			bool inline_mode)
{
	u32 reg_ctrl = 0;
	u32 reg_stride0 = 0, reg_stride1 = 0;
	u32 reg_thresh;
	const u32 *vpp_coef = NULL, *vpp_offset = NULL;
	u32 ctrl_mask = VPP_CTRL_YUV420_FORMAT |
			VPP_CTRL_ENDIAN_MODE |
			VPP_CTRL_YUV422_FORMAT_MASK |
			VPP_CTRL_UV_INTERLEAVE_EN |
			VPP_CTRL_INLINE_EN |
			VPP_CTRL_INLINE_3LINE |
			VPP_CTRL_UVUV_MODE;

	switch (surf->fmt) {
	case VDSS_PIXELFORMAT_YV12:
	case VDSS_PIXELFORMAT_I420:
		/* NV12, NV21, hw required UV stride right shift 1*/
	case VDSS_PIXELFORMAT_NV12:
	case VDSS_PIXELFORMAT_NV21:
	case VDSS_PIXELFORMAT_NJ12:
		reg_ctrl |= VPP_CTRL_YUV420_FORMAT;
		reg_stride0 |= VPP_Y_STRIDE(surf->width);
		reg_stride0 |= VPP_U_STRIDE(surf->width / 2);
		reg_stride1 |= VPP_V_STRIDE(surf->width / 2);
		break;
	case VDSS_PIXELFORMAT_IMC4:
	case VDSS_PIXELFORMAT_IMC3:
	case VDSS_PIXELFORMAT_IMC2:
	case VDSS_PIXELFORMAT_IMC1:
	case VDSS_PIXELFORMAT_Q420:
		reg_ctrl |= VPP_CTRL_YUV420_FORMAT;
		reg_stride0 |= VPP_Y_STRIDE(surf->width);
		reg_stride0 |= VPP_U_STRIDE(surf->width);
		reg_stride1 |= VPP_V_STRIDE(surf->width);
		break;
	case VDSS_PIXELFORMAT_UYVY:
		reg_ctrl &= ~VPP_CTRL_YUV420_FORMAT;
		reg_ctrl |= VPP_CTRL_YUV422_FORMAT(VPP_YUV422_FORMAT_YVYU);
		reg_stride0 |= VPP_Y_STRIDE(surf->width * 2);
		break;
	case VDSS_PIXELFORMAT_UYNV:
		reg_ctrl &= ~VPP_CTRL_YUV420_FORMAT;
		reg_ctrl |= VPP_CTRL_YUV422_FORMAT(VPP_YUV422_FORMAT_YVYU);
		reg_stride0 |= VPP_Y_STRIDE(surf->width * 2);
		break;
	case VDSS_PIXELFORMAT_YUY2:
	case VDSS_PIXELFORMAT_YUYV:
	case VDSS_PIXELFORMAT_YUNV:
		reg_ctrl &= ~VPP_CTRL_YUV420_FORMAT;
		reg_ctrl |= VPP_CTRL_YUV422_FORMAT(VPP_YUV422_FORMAT_VYUY);
		reg_stride0 |= VPP_Y_STRIDE(surf->width * 2);
		break;
	case VDSS_PIXELFORMAT_YVYU:
		reg_ctrl &= ~VPP_CTRL_YUV420_FORMAT;
		reg_ctrl |= VPP_CTRL_YUV422_FORMAT(VPP_YUV422_FORMAT_UYVY);
		reg_stride0 |= VPP_Y_STRIDE(surf->width * 2);
		break;
	case VDSS_PIXELFORMAT_VYUY:
		reg_ctrl &= ~VPP_CTRL_YUV420_FORMAT;
		reg_ctrl |= VPP_CTRL_YUV422_FORMAT(VPP_YUV422_FORMAT_YUYV);
		reg_stride0 |= VPP_Y_STRIDE(surf->width * 2);
		break;
	default:
		vpp_err("%s(%d): unkonwn src format 0x%x\n",
			__func__, __LINE__, surf->fmt);
		return -EINVAL;
	}

	if (surf->fmt == VDSS_PIXELFORMAT_NV12 ||
	    surf->fmt == VDSS_PIXELFORMAT_NV21 ||
	    surf->fmt == VDSS_PIXELFORMAT_NJ12)
		reg_ctrl |= VPP_CTRL_UV_INTERLEAVE_EN;

	if (surf->fmt == VDSS_PIXELFORMAT_NV12 ||
		surf->fmt == VDSS_PIXELFORMAT_NJ12) {
		if (adapter->is_atlas7)
			reg_ctrl |= VPP_CTRL_UVUV_MODE;
		else {
			reg_thresh = vpp_read_reg(adapter, VPP_FULL_THRESH);
			reg_thresh |= VPP_UVUV_MODE;
			vpp_write_reg(adapter, VPP_FULL_THRESH, reg_thresh);
		}
	}

	if (surf->fmt == VDSS_PIXELFORMAT_NJ12) {
		vpp_coef = &rgb_yuv_coeff_nj12[0];
		vpp_offset = &rgb_offsets_nj12[0];
	} else {
		vpp_coef = &rgb_yuv_coeff[0];
		vpp_offset = &rgb_offsets[0];
	}

	{
		int i;
		u32 offset, val;

		offset = VPP_RCOEF;
		for (i = 0; i < ARRAY_SIZE(rgb_yuv_coeff); i += 3) {
			val = vpp_coef[i] |
				(vpp_coef[i+1] << 10) |
				(vpp_coef[i+2] << 20);
			vpp_write_reg(adapter, offset, val);
			offset += 4;
		}

		vpp_write_reg(adapter, VPP_OFFSET1, vpp_offset[0]);
		vpp_write_reg(adapter, VPP_OFFSET2, vpp_offset[1]);
		vpp_write_reg(adapter, VPP_OFFSET3, vpp_offset[2]);
	}

	if (inline_mode)
		reg_ctrl |= (VPP_CTRL_INLINE_EN |
			VPP_CTRL_INLINE_3LINE | VPP_CTRL_ENDIAN_MODE);

	switch (surf->field) {
	case VDSS_FIELD_INTERLACED:
	case VDSS_FIELD_INTERLACED_TB:
	case VDSS_FIELD_INTERLACED_BT:
		reg_stride0 = (reg_stride0 * 2) &
			(VPP_Y_STRIDE_MASK | VPP_U_STRIDE_MASK);
		reg_stride1 = (reg_stride1 * 2) & VPP_V_STRIDE_MASK;
		break;
	default:
		break;
	}

	vpp_write_reg_with_mask(adapter,
				VPP_CTRL,
				reg_ctrl,
				~ctrl_mask);
	vpp_write_reg_with_mask(adapter,
				VPP_STRIDE0,
				reg_stride0,
				~(VPP_Y_STRIDE_MASK | VPP_U_STRIDE_MASK));
	vpp_write_reg_with_mask(adapter,
				VPP_STRIDE1,
				reg_stride1,
				~VPP_V_STRIDE_MASK);

	return 0;
}

static void __vpp_ibv_enable(struct vpp_adapter *adapter,
			enum vdss_vip_ext src,
			u32 bufsize)
{
	u32 reg_ctrl = 0;

	reg_ctrl |= (src << 28);
	reg_ctrl |= ((bufsize - 1) << 23);
	reg_ctrl |= VPP_CTRL_HW_BUF_SWITCH;

	vpp_write_reg_with_mask(adapter, VPP_CTRL,
			reg_ctrl, ~VPP_CTRL_IBV_MASK);
}

static void __vpp_ibv_disable(struct vpp_adapter *adapter)
{
	vpp_write_reg_with_mask(adapter, VPP_CTRL,
			0, ~VPP_CTRL_IBV_MASK);
}

static int __vpp_setup_dst(struct vpp_adapter *adapter,
			struct vdss_surface *surf)
{
	u32 reg_ctrl = 0;
	u32 reg_stride1 = 0;
	enum vdss_pixelformat fmt;
	u32 ctrl_mask = VPP_CTRL_OUT_FORMAT_MASK |
			VPP_CTRL_DEST |
			VPP_CTRL_TOP_FIELD_FIRST |
			VPP_CTRL_OUT_YUV422_FORMAT_MASK;

	/* passthrough mode is enabled */
	if (surf == NULL) {
		reg_ctrl |= (VPP_DEST_LCD << 7);
		fmt = VPP_TO_LCD_PIXELFORMAT;
	} else {
		reg_ctrl |= (VPP_DEST_MEMORY << 7);
		fmt = surf->fmt;
	}

	switch (fmt) {
	case VDSS_PIXELFORMAT_565:
		reg_ctrl |= VPP_CTRL_OUT_FORMAT(VPP_OUT_FORMAT_RGB565);
		break;
	case VDSS_PIXELFORMAT_666:
		reg_ctrl |= VPP_CTRL_OUT_FORMAT(VPP_OUT_FORMAT_RGB666);
		break;
	case VDSS_PIXELFORMAT_BGRX_8880:
	case VDSS_PIXELFORMAT_RGBX_8880:
		reg_ctrl |= VPP_CTRL_OUT_FORMAT(VPP_OUT_FORMAT_RGB888);
		break;
	case VDSS_PIXELFORMAT_YUYV:
		reg_ctrl |= VPP_CTRL_OUT_FORMAT(VPP_OUT_FORMAT_YUV422);
		reg_ctrl |= VPP_CTRL_OUT_YUV422_FORMAT(VPP_YUV422_FORMAT_VYUY);
		break;
	case VDSS_PIXELFORMAT_YVYU:
		reg_ctrl |= VPP_CTRL_OUT_FORMAT(VPP_OUT_FORMAT_YUV422);
		reg_ctrl |= VPP_CTRL_OUT_YUV422_FORMAT(VPP_YUV422_FORMAT_UYVY);
		break;
	case VDSS_PIXELFORMAT_UYVY:
		reg_ctrl |= VPP_CTRL_OUT_FORMAT(VPP_OUT_FORMAT_YUV422);
		reg_ctrl |= VPP_CTRL_OUT_YUV422_FORMAT(VPP_YUV422_FORMAT_YVYU);
		break;
	case VDSS_PIXELFORMAT_VYUY:
		reg_ctrl |= VPP_CTRL_OUT_FORMAT(VPP_OUT_FORMAT_YUV422);
		reg_ctrl |= VPP_CTRL_OUT_YUV422_FORMAT(VPP_YUV422_FORMAT_YUYV);
		break;
	default:
		vpp_err("%s(%d): unknown dst format 0x%x\n",
			__func__, __LINE__, fmt);
		return -EINVAL;
	}

	if (fmt == VDSS_PIXELFORMAT_RGBX_8880) {
		u32 val;

		val =  vpp_read_reg(adapter, VPP_RCOEF);
		vpp_write_reg(adapter, VPP_RCOEF,
			vpp_read_reg(adapter, VPP_BCOEF));
		vpp_write_reg(adapter, VPP_BCOEF, val);

		val = vpp_read_reg(adapter, VPP_OFFSET1);
		vpp_write_reg(adapter, VPP_OFFSET1,
			vpp_read_reg(adapter, VPP_OFFSET3));
		vpp_write_reg(adapter, VPP_OFFSET3, val);
	}

	if (surf) {
		switch (fmt) {
		case VDSS_PIXELFORMAT_565:
			reg_stride1 = VPP_DEST_STRIDE(surf->width * 2);
			break;
		case VDSS_PIXELFORMAT_666:
			reg_stride1 = VPP_DEST_STRIDE(surf->width * 4);
			break;
		case VDSS_PIXELFORMAT_BGRX_8880:
		case VDSS_PIXELFORMAT_RGBX_8880:
			reg_stride1 = VPP_DEST_STRIDE(surf->width * 4);
			break;
		case VDSS_PIXELFORMAT_YUYV:
			reg_stride1 = VPP_DEST_STRIDE(surf->width * 2);
			break;
		case VDSS_PIXELFORMAT_YVYU:
			reg_stride1 = VPP_DEST_STRIDE(surf->width * 2);
			break;
		case VDSS_PIXELFORMAT_UYVY:
			reg_stride1 = VPP_DEST_STRIDE(surf->width * 2);
			break;
		case VDSS_PIXELFORMAT_VYUY:
			reg_stride1 = VPP_DEST_STRIDE(surf->width * 2);
			break;
		default:
			vpp_err("%s(%d): unknown dst format 0x%x\n",
				__func__, __LINE__, fmt);
			return -EINVAL;
		}

		switch (surf->field) {
		case VDSS_FIELD_SEQ_TB:
			reg_ctrl |= VPP_CTRL_TOP_FIELD_FIRST;
			break;
		case VDSS_FIELD_INTERLACED:
		case VDSS_FIELD_INTERLACED_TB:
			reg_ctrl |= VPP_CTRL_TOP_FIELD_FIRST;
			reg_stride1 = VPP_DEST_STRIDE(surf->width * 4);
			break;
		case VDSS_FIELD_INTERLACED_BT:
			reg_stride1 = VPP_DEST_STRIDE(surf->width * 4);
			break;
		default:
			break;
		}
	}

	vpp_write_reg_with_mask(adapter,
				VPP_CTRL,
				reg_ctrl,
				~ctrl_mask);

	vpp_write_reg_with_mask(adapter,
				VPP_STRIDE1,
				reg_stride1,
				~VPP_DEST_STRIDE_MASK);

	return 0;
}

static void __vpp_blt_start(struct vpp_adapter *adapter)
{
	vpp_write_reg_with_mask(adapter, VPP_CTRL,
			VPP_CTRL_START, ~VPP_CTRL_START);
}

static int __vpp_set_srcbase(struct vpp_adapter *adapter,
				struct vdss_surface *surf,
				u32 size,
				bool inline_mode,
				struct vdss_rect *rect)
{
	u32 ybase = 0, ubase = 0, vbase = 0;
	u32 ybase_bot = 0, ubase_bot = 0, vbase_bot = 0;
	u32 yoffset, uoffset, voffset;
	u32 i = 0;
	u32 field_offset = 0;
	struct vdss_rect src_rect = *rect;
	enum vdss_field field = surf->field;

	if (inline_mode) {
		/* Inline address is fixed */
		vpp_write_reg(adapter, VPP_INLINE_ADDR,
			INLINE_NOCFIFO_ADDR);
		return 0;
	}

	vpp_write_reg(adapter, VPP_INLINE_ADDR, 0);

	switch (field) {
	case VDSS_FIELD_SEQ_TB:
	case VDSS_FIELD_SEQ_BT:
		/*
		 * The input rect is range in the frame surface
		 * which is double as field size
		 * */
		src_rect.top = src_rect.top >> 1;
		src_rect.bottom = src_rect.bottom >> 1;
		if ((surf->fmt > VDSS_PIXELFORMAT_32BPPGENERIC &&
			surf->fmt < VDSS_PIXELFORMAT_IMC2) ||
			surf->fmt  == VDSS_PIXELFORMAT_Q420)
			field_offset = surf->width * surf->height;
		else
			field_offset = surf->width *
				(surf->height / 2) * 3 / 2;
		break;
	case VDSS_FIELD_INTERLACED:
	case VDSS_FIELD_INTERLACED_TB:
	case VDSS_FIELD_INTERLACED_BT:
		field_offset = 0;
		break;
	default:
		field_offset = 0;
		break;
	}

	yoffset = surf->width * src_rect.top + src_rect.left;

	if (surf->fmt == VDSS_PIXELFORMAT_YV12 ||
		surf->fmt == VDSS_PIXELFORMAT_I420) {
		uoffset = (surf->width / 2) *
			(src_rect.top / 2) + src_rect.left / 2;
		voffset = uoffset;
	} else if (surf->fmt == VDSS_PIXELFORMAT_IMC1 ||
		surf->fmt == VDSS_PIXELFORMAT_IMC3 ||
		surf->fmt == VDSS_PIXELFORMAT_IMC2 ||
		surf->fmt == VDSS_PIXELFORMAT_IMC4 ||
		surf->fmt == VDSS_PIXELFORMAT_Q420) {
		uoffset = surf->width * (src_rect.top / 2) +
			src_rect.left / 2;
		voffset = uoffset;
	} else if (surf->fmt == VDSS_PIXELFORMAT_NV12 ||
		surf->fmt == VDSS_PIXELFORMAT_NV21 ||
		surf->fmt == VDSS_PIXELFORMAT_NJ12) {
		uoffset = surf->width * (src_rect.top / 2) +
			src_rect.left;
		voffset = uoffset;
	} else
		voffset = uoffset = 0;

	switch (surf->fmt) {
	case VDSS_PIXELFORMAT_YV12:
		ybase = surf->base + yoffset;
		ubase = surf->base + surf->width *
			surf->height * 5 / 4 + uoffset;
		vbase = surf->base + surf->width *
			surf->height + voffset;
		break;
	case VDSS_PIXELFORMAT_I420:
		ybase = surf->base + yoffset;
		ubase = surf->base + surf->width *
			surf->height + uoffset;
		vbase = surf->base + surf->width *
			surf->height * 5 / 4 + voffset;
		break;
	case VDSS_PIXELFORMAT_Q420:
		ybase = surf->base + yoffset;
		ubase = surf->base +  surf->width  *
			surf->height + uoffset;
		vbase = surf->base + surf->width  *
			surf->height * 3 / 2 + voffset;
		break;
	case VDSS_PIXELFORMAT_IMC1:
	case VDSS_PIXELFORMAT_IMC3:
		ybase = surf->base + yoffset;
		ubase = surf->base + surf->width *
			surf->height * 3 / 2 + uoffset;
		vbase = surf->base + surf->width *
			surf->height + voffset;
		break;
	case VDSS_PIXELFORMAT_IMC2:
	case VDSS_PIXELFORMAT_IMC4:
		ybase = surf->base + yoffset;
		ubase = surf->base + surf->width *
			surf->height + uoffset;
		vbase = surf->base + surf->width *
			surf->height + voffset +
			surf->width / 2;
		break;
	case VDSS_PIXELFORMAT_NV12:
	case VDSS_PIXELFORMAT_NV21:
	case VDSS_PIXELFORMAT_NJ12:
		ybase = surf->base + yoffset;
		/*
		 * According to spec, if the input format is semi-planar YUV420,
		 * this value should be divided by 2 as it should be.
		 */
		ubase = (surf->base + surf->width *
			surf->height + uoffset) >> 1;
		vbase = ubase;
		break;
	case VDSS_PIXELFORMAT_UYVY:
	case VDSS_PIXELFORMAT_UYNV:
	case VDSS_PIXELFORMAT_YUY2:
	case VDSS_PIXELFORMAT_YUYV:
	case VDSS_PIXELFORMAT_YUNV:
	case VDSS_PIXELFORMAT_YVYU:
	case VDSS_PIXELFORMAT_VYUY:
		ybase = surf->base + (2 * yoffset);
		ubase = vbase = ybase;
		break;
	default:
		vpp_err("%s(%d): unknown src format 0x%x\n",
			__func__, __LINE__, surf->fmt);
		return -EINVAL;
	}

	if (field != VDSS_FIELD_NONE) {
		if (field_offset) {
			ybase_bot = ybase + field_offset;
			ubase_bot = ubase + field_offset;
			vbase_bot = vbase + field_offset;
		} else {
			switch (surf->fmt) {
			case VDSS_PIXELFORMAT_YV12:
			case VDSS_PIXELFORMAT_I420:
			case VDSS_PIXELFORMAT_NV12:
			case VDSS_PIXELFORMAT_NV21:
			case VDSS_PIXELFORMAT_NJ12:
				ybase_bot = ybase + surf->width;
				ubase_bot = ubase + surf->width / 2;
				vbase_bot = vbase + surf->width / 2;
				break;
			case VDSS_PIXELFORMAT_IMC4:
			case VDSS_PIXELFORMAT_IMC3:
			case VDSS_PIXELFORMAT_IMC2:
			case VDSS_PIXELFORMAT_IMC1:
			case VDSS_PIXELFORMAT_Q420:
				ybase_bot = ybase + surf->width;
				ubase_bot = ubase + surf->width;
				vbase_bot = vbase + surf->width;
				break;
			case VDSS_PIXELFORMAT_UYVY:
			case VDSS_PIXELFORMAT_UYNV:
			case VDSS_PIXELFORMAT_YUY2:
			case VDSS_PIXELFORMAT_YUYV:
			case VDSS_PIXELFORMAT_YUNV:
			case VDSS_PIXELFORMAT_YVYU:
			case VDSS_PIXELFORMAT_VYUY:
				ybase_bot = ybase + 2 * surf->width;
				ubase_bot = vbase_bot = ybase_bot;
				break;
			default:
				vpp_err("%s(%d): unknown src format 0x%x\n",
					__func__, __LINE__, surf->fmt);
				return -EINVAL;
			}
		}

		if (field == VDSS_FIELD_SEQ_TB ||
		    field == VDSS_FIELD_INTERLACED_TB ||
		    field == VDSS_FIELD_INTERLACED) {
			vpp_write_reg(adapter, VPP_YBASE, ybase);
			vpp_write_reg(adapter, VPP_UBASE, ubase);
			vpp_write_reg(adapter, VPP_VBASE, vbase);
			vpp_write_reg(adapter, VPP_YBASE_BOT, ybase_bot);
			vpp_write_reg(adapter, VPP_UBASE_BOT, ubase_bot);
			vpp_write_reg(adapter, VPP_VBASE_BOT, vbase_bot);
		} else if (field == VDSS_FIELD_SEQ_BT ||
		    field == VDSS_FIELD_INTERLACED_BT) {
			vpp_write_reg(adapter, VPP_YBASE_BOT, ybase);
			vpp_write_reg(adapter, VPP_UBASE_BOT, ubase);
			vpp_write_reg(adapter, VPP_VBASE_BOT, vbase);
			vpp_write_reg(adapter, VPP_YBASE, ybase_bot);
			vpp_write_reg(adapter, VPP_UBASE, ubase_bot);
			vpp_write_reg(adapter, VPP_VBASE, vbase_bot);
		}

	} else {
		vpp_write_reg(adapter, VPP_YBASE, ybase);
		vpp_write_reg(adapter, VPP_UBASE, ubase);
		vpp_write_reg(adapter, VPP_VBASE, vbase);
	}

	if (size > 1) {
		for (i = 1; i < size; i++) {
			/*
			 * Only rearview works in this path and only
			 * support YVU422 format
			 * */
			if (surf[i].fmt <= VDSS_PIXELFORMAT_32BPPGENERIC ||
			    surf[i].fmt >= VDSS_PIXELFORMAT_IMC2) {
				vpp_err("%s(%d): src format 0x%x unsupported\n",
					__func__, __LINE__, surf->fmt);
				return -EINVAL;
			}

			ybase = surf[i].base + (2 * yoffset);
			ybase_bot = ybase + field_offset;
			if (surf[i].field == VDSS_FIELD_SEQ_TB) {
				vpp_write_reg(adapter,
						y_top_addr_regs[i],
						ybase);
				vpp_write_reg(adapter,
						y_bot_addr_regs[i],
						ybase_bot);
			} else if (surf[i].field == VDSS_FIELD_SEQ_BT) {
				vpp_write_reg(adapter,
						y_top_addr_regs[i],
						ybase_bot);
				vpp_write_reg(adapter,
						y_bot_addr_regs[i],
						ybase);
			} else
				vpp_write_reg(adapter,
						y_top_addr_regs[i],
						ybase);
		}
	} else {
		for (i = 1; i < 3; i++) {
			vpp_write_reg(adapter, y_top_addr_regs[i], 0);
			vpp_write_reg(adapter, y_bot_addr_regs[i], 0);
		}
	}

	return 0;
}

static int __vpp_set_dstbase(struct vpp_adapter *adapter,
			struct vdss_surface *surf,
			struct vdss_rect *rect,
			struct vdss_vpp_interlace *interlace)
{
	u32 dstbase = 0;
	u32 dstbase_bot = 0;
	u32 bpp = 0;
	u32 yoffset = 0;
	struct vdss_rect dst_rect = *rect;

	/*
	 * No Blt mode, set dst base as zero
	 * */
	if (surf == NULL) {
		vpp_write_reg(adapter, VPP_DESBASE, 0);
		vpp_write_reg(adapter, VPP_DESTBASE_BOT, 0);
		return 0;
	}

	switch (surf->fmt) {
	case VDSS_PIXELFORMAT_565:
		bpp = 2;
		break;
	case VDSS_PIXELFORMAT_666:
	case VDSS_PIXELFORMAT_BGRX_8880:
	case VDSS_PIXELFORMAT_RGBX_8880:
		bpp = 4;
		break;
	case VDSS_PIXELFORMAT_YUYV:
	case VDSS_PIXELFORMAT_YVYU:
	case VDSS_PIXELFORMAT_UYVY:
	case VDSS_PIXELFORMAT_VYUY:
		bpp = 2;
		break;
	default:
		vpp_err("%s(%d): unknown dst format 0x%x\n",
			__func__, __LINE__, surf->fmt);
		return -EINVAL;
	}

	switch (surf->field) {
	case VDSS_FIELD_SEQ_TB:
		dst_rect.top = dst_rect.top >> 1;
		dst_rect.bottom = dst_rect.bottom >> 1;
		yoffset = surf->width * dst_rect.top + dst_rect.left;
		dstbase = (surf->base + yoffset * bpp) & (~7);
		dstbase_bot = dstbase + surf->width * surf->height * bpp / 2;
		break;
	case VDSS_FIELD_SEQ_BT:
		dst_rect.top = dst_rect.top >> 1;
		dst_rect.bottom = dst_rect.bottom >> 1;
		yoffset = surf->width * dst_rect.top + dst_rect.left;
		dstbase_bot = (surf->base + yoffset * bpp) & (~7);
		dstbase = dstbase_bot + surf->width * surf->height * bpp / 2;
		break;
	case VDSS_FIELD_INTERLACED:
	case VDSS_FIELD_INTERLACED_TB:
		yoffset = surf->width * dst_rect.top + dst_rect.left;
		dstbase = (surf->base + yoffset * bpp) & (~7);
		dstbase_bot = dstbase + bpp * surf->width;
		break;
	case VDSS_FIELD_INTERLACED_BT:
		yoffset = surf->width * dst_rect.top + dst_rect.left;
		dstbase_bot = (surf->base + yoffset * bpp) & (~7);
		dstbase = dstbase_bot + bpp * surf->width;
		break;
	case VDSS_FIELD_NONE:
		yoffset = surf->width * dst_rect.top + dst_rect.left;
		dstbase = (surf->base + yoffset * bpp) & (~7);
		break;
	case VDSS_FRAME_TOP:
		yoffset = surf->width * dst_rect.top + dst_rect.left;
		dstbase = (surf->base + yoffset * bpp) & (~7);
		if (surf[1].field != VDSS_FRAME_BOTTOM) {
			vpp_err("%s(%d): unsupported dst field 0x%x\n",
				__func__, __LINE__, surf[1].field);
			return -EINVAL;
		}
		dstbase_bot = (surf[1].base + yoffset * bpp) & (~7);
		break;
	case VDSS_FRAME_BOTTOM:
		yoffset = surf->width * dst_rect.top + dst_rect.left;
		dstbase_bot = (surf->base + yoffset * bpp) & (~7);
		if (surf[1].field != VDSS_FRAME_TOP) {
			vpp_err("%s(%d): unsupported dst field 0x%x\n",
				__func__, __LINE__, surf[1].field);
			return -EINVAL;
		}
		dstbase = (surf[1].base + yoffset * bpp) & (~7);
		break;
	default:
		vpp_err("%s(%d): unsupported dst field 0x%x\n",
			__func__, __LINE__, surf->field);
		return -EINVAL;
	}

	vpp_write_reg(adapter, VPP_DESBASE, dstbase);
	vpp_write_reg(adapter, VPP_DESTBASE_BOT, dstbase_bot);

	return 0;
}

static int __vpp_set_color_ctrl(struct vpp_adapter *adapter,
		struct vdss_vpp_colorctrl *color_ctrl)
{
	s32 bc_data;
	s32 uv_data;
	s32 uc, vc;

	bc_data = VPP_COLOR_B_CTRL(color_ctrl->brightness) |
		VPP_COLOR_C_CTRL(color_ctrl->contrast);

	uc = vpp_cal_uc(color_ctrl->hue, color_ctrl->saturation);
	vc = vpp_cal_vc(color_ctrl->hue, color_ctrl->saturation);

	uv_data = VPP_COLOR_UC_CTRL(uc) | VPP_COLOR_VC_CTRL(vc);

	vpp_write_reg(adapter, VPP_COLOR_HS_CTRL, uv_data);
	vpp_write_reg(adapter, VPP_COLOR_BC_CTRL, bc_data);

	return 0;
}

static void __vpp_enable_interrupt(struct vpp_adapter *adapter,
	u32 mask)
{
	u32 val = vpp_read_reg(adapter, VPP_INT_MASK);

	if ((mask & val) == mask)
		return;

	val |= mask;
	vpp_write_reg(adapter, VPP_INT_MASK, val);
}

static void __vpp_disable_interrupt(struct vpp_adapter *adapter,
	u32 mask)
{
	u32 val = vpp_read_reg(adapter, VPP_INT_MASK);

	if (((~val) & mask) == mask)
		return;

	val &= ~mask;
	vpp_write_reg(adapter, VPP_INT_MASK, val);
}

static void __vpp_clear_interrupt(struct vpp_adapter *adapter,
	u32 mask)
{
	vpp_write_reg(adapter, VPP_INT_STATUS, mask);
}

static int __vpp_wait_for_idle(struct vpp_adapter *adapter)
{
	unsigned long timeout = msecs_to_jiffies(100);
	struct vpp_irq *vpp_irq = &adapter->vpp_irq;
	struct completion *completion = &vpp_irq->comp;
	unsigned long flags;
	long result;

	spin_lock_irqsave(&vpp_irq->irq_lock, flags);

	reinit_completion(completion);

	spin_unlock_irqrestore(&vpp_irq->irq_lock, flags);

	result = wait_for_completion_interruptible_timeout(completion,
		timeout);

	if (result == 0)
		return -ETIMEDOUT;

	if (result < 0)
		return result;

	return 0;
}

static irqreturn_t vpp_irq_handler(int irq, void *dev_id)
{
	struct vpp_adapter *adapter = (struct vpp_adapter *)dev_id;
	u32 int_status, int_mask;
	struct vpp_irq *vpp_irq = &adapter->vpp_irq;

	spin_lock(&vpp_irq->irq_lock);

	int_status = vpp_read_reg(adapter, VPP_INT_STATUS);
	int_mask = vpp_read_reg(adapter, VPP_INT_MASK);

	__vpp_clear_interrupt(adapter, int_status);

	if (int_status & VPP_INT_SINGLE_STATUS)
		complete(&vpp_irq->comp);

	spin_unlock(&vpp_irq->irq_lock);

	return IRQ_HANDLED;
}

static int __vpp_request_irq(struct vpp_adapter *adapter)
{
	int r;

	r = devm_request_irq(&adapter->pdev->dev, adapter->irq, vpp_irq_handler,
		IRQF_SHARED, "SIRFSOC VPP", adapter);

	return r;
}

static int vpp_init_irq(struct vpp_adapter *adapter)
{
	int r;
	struct vpp_irq *vpp_irq = &adapter->vpp_irq;

	spin_lock_init(&vpp_irq->irq_lock);
	init_completion(&vpp_irq->comp);

	r = __vpp_request_irq(adapter);
	if (r) {
		VDSSERR("__vpp_request_irq failed, ret = %x\n", r);
		return r;
	}

	return 0;
}

static enum vpp_seq_type __vpp_seq_type(struct vdss_surface *src_surf,
	struct vdss_surface *dst_surf)
{
	bool src_i = false;
	bool dst_i = false;

	if (src_surf->field != VDSS_FIELD_NONE)
		src_i = true;

	if (dst_surf) {
		switch (dst_surf->field) {
		case VDSS_FIELD_TOP:
		case VDSS_FIELD_BOTTOM:
		case VDSS_FIELD_INTERLACED:
		case VDSS_FIELD_SEQ_TB:
		case VDSS_FIELD_SEQ_BT:
		case VDSS_FIELD_INTERLACED_TB:
		case VDSS_FIELD_INTERLACED_BT:
			dst_i = true;
			break;
		default:
			break;
		}
	}

	if (src_i && dst_i)
		return VPP_SEQ_TYPE_IIIO;
	else if (src_i && !dst_i)
		return VPP_SEQ_TYPE_IIPO;
	else if (!src_i && dst_i) {
		/*
		* When frame in - field out, VPP take this case the same
		* as field(VDSS_FIELD_INTERLACED_TB) in - field out
		*/
		src_surf->field =
			VDSS_FIELD_INTERLACED_TB;
		return VPP_SEQ_TYPE_IIIO;
	}
	else
		return VPP_SEQ_TYPE_PIPO;
}

static int __vpp_set_ctrl(struct vpp_adapter *adapter,
	enum vpp_seq_type seq,
	struct vdss_surface *src_surf,
	struct vdss_surface *dst_surf,
	struct vdss_vpp_interlace *interlace)
{
	u32 reg_ctrl = 0;
	u32 ctrl_mask = VPP_CTRL_HW_DI_MODE_MASK |
			VPP_CTRL_SEQ_TYPE_MASK |
			VPP_CTRL_TOP_FIELD_FIRST |
			VPP_CTRL_DI_FIELD_BOT |
			VPP_CTRL_DOUBLE_FRATE;

	if (adapter == NULL)
		return -EINVAL;

	reg_ctrl |= VPP_CTRL_SEQ_TYPE(seq);
	if (interlace && interlace->di_mode)
		reg_ctrl |= VPP_CTRL_HW_DI_MODE(interlace->di_mode);
	else
		reg_ctrl |= VPP_CTRL_HW_DI_MODE(0);

	if (dst_surf && (
	    dst_surf->field == VDSS_FRAME_TOP ||
	    dst_surf->field == VDSS_FRAME_BOTTOM)) {
		if (seq == VPP_SEQ_TYPE_IIPO)
			reg_ctrl |= VPP_CTRL_DOUBLE_FRATE;
		else {
			VDSSERR("Up-sampling scaling, only support IIPO\n");
			return -EINVAL;
		}
	}

	if (interlace &&
	   (interlace->di_mode == VDSS_VPP_3MEDIAN ||
	    interlace->di_mode == VDSS_VPP_DI_VMRI) &&
	    interlace->di_top)
		reg_ctrl |= VPP_CTRL_DI_FIELD_BOT;

	vpp_write_reg_with_mask(adapter,
				VPP_CTRL,
				reg_ctrl,
				~ctrl_mask);

	return 0;
}

static int __vpp_blt(struct vpp_adapter *adapter,
		struct vdss_vpp_blt_params *params)
{
	enum vpp_seq_type seq;

	if (adapter == NULL || params == NULL)
		return -EINVAL;

	if (!vpp_blt_check_size(
	    &params->src_surf, &params->src_rect,
	    &params->dst_surf[0], &params->dst_rect))
		return -EINVAL;

	/* using interrupt to check frame complete*/
	__vpp_enable_interrupt(adapter, VPP_INT_SINGLE_STATUS);
	__vpp_clear_interrupt(adapter, VPP_INT_SINGLE_STATUS);

	seq = __vpp_seq_type(&params->src_surf, &params->dst_surf[0]);
	/* src setting */
	__vpp_setup_src(adapter, &params->src_surf, false);
	__vpp_set_srcbase(adapter, &params->src_surf, 1, false,
			&params->src_rect);
	__vpp_set_src_rect(adapter, &params->src_rect);
	__vpp_ibv_disable(adapter);

	/* dst setting */
	__vpp_setup_dst(adapter, &params->dst_surf[0]);
	__vpp_set_dstbase(adapter, &params->dst_surf[0],
			&params->dst_rect, &params->interlace);
	__vpp_set_dst_rect(adapter, &params->dst_rect);

	__vpp_set_ctrl(adapter, seq, &params->src_surf,
		&params->dst_surf[0], &params->interlace);

	/* color ctrl setting */
	__vpp_set_color_ctrl(adapter, &params->color_ctrl);

	/* vpp blt start */
	__vpp_blt_start(adapter);

	return 0;
}

static int __vpp_inline(struct vpp_adapter *adapter,
		struct vdss_vpp_inline_params *params)
{
	enum vpp_seq_type seq;

	if (adapter == NULL || params == NULL)
		return -EINVAL;

	seq = __vpp_seq_type(&params->src_surf, NULL);
	__vpp_disable_interrupt(adapter, VPP_INT_SINGLE_STATUS);
	/* src setting */
	__vpp_setup_src(adapter, &params->src_surf, true);
	__vpp_set_srcbase(adapter, &params->src_surf, 1, true,
			&params->src_rect);
	__vpp_set_src_rect(adapter, &params->src_rect);
	__vpp_ibv_disable(adapter);

	/* dst setting */
	__vpp_setup_dst(adapter, NULL);
	__vpp_set_dstbase(adapter, NULL,
			&params->dst_rect, NULL);
	__vpp_set_dst_rect(adapter, &params->dst_rect);

	__vpp_set_ctrl(adapter, seq, &params->src_surf,
		NULL, NULL);

	/* color ctrl setting */
	__vpp_set_color_ctrl(adapter, &params->color_ctrl);

	return 0;
}


static int __vpp_passthrough(struct vpp_adapter *adapter,
		struct vdss_vpp_passthrough_params *params)
{
	enum vpp_seq_type seq;

	if (adapter == NULL || params == NULL)
		return -EINVAL;

	seq = __vpp_seq_type(&params->src_surf, NULL);
	__vpp_disable_interrupt(adapter, VPP_INT_SINGLE_STATUS);

	if (params->flip) {
		__vpp_set_srcbase(adapter, &params->src_surf, 1, false,
			&params->src_rect);
	} else {
		/* src setting */
		__vpp_setup_src(adapter, &params->src_surf,
				false);
		__vpp_set_srcbase(adapter, &params->src_surf, 1, false,
				&params->src_rect);
		__vpp_set_src_rect(adapter, &params->src_rect);
		__vpp_ibv_disable(adapter);

		/* dst setting */
		__vpp_setup_dst(adapter, NULL);
		__vpp_set_dstbase(adapter, NULL,
				&params->dst_rect, &params->interlace);
		__vpp_set_dst_rect(adapter, &params->dst_rect);

		__vpp_set_ctrl(adapter, seq, &params->src_surf,
			NULL, &params->interlace);

		/* color ctrl setting */
		__vpp_set_color_ctrl(adapter, &params->color_ctrl);
	}

	return 0;
}

static int __vpp_ibv(struct vpp_adapter *adapter,
		struct vdss_vpp_ibv_params *params)
{
	enum vpp_seq_type seq;

	if (adapter == NULL || params == NULL)
		return -EINVAL;

	seq = __vpp_seq_type((struct vdss_surface *)&params->src_surf, NULL);
	/* color ctrl setting */
	__vpp_set_color_ctrl(adapter, &params->color_ctrl);

	if (params->color_update_only)
		return 0;

	__vpp_disable_interrupt(adapter, VPP_INT_SINGLE_STATUS);
	/* src setting */
	__vpp_setup_src(adapter, &params->src_surf[0],
			false);
	__vpp_set_srcbase(adapter, &params->src_surf[0],
			params->src_size, false,
			&params->src_rect);
	__vpp_set_src_rect(adapter, &params->src_rect);
	__vpp_ibv_enable(adapter, params->src_id, params->src_size);

	/* dst setting */
	__vpp_setup_dst(adapter, NULL);
	__vpp_set_dstbase(adapter, NULL,
			&params->dst_rect, &params->interlace);
	__vpp_set_dst_rect(adapter, &params->dst_rect);

	__vpp_set_ctrl(adapter, seq, (struct vdss_surface *)&params->src_surf,
		NULL, (struct vdss_vpp_interlace *)&params->interlace);

	return 0;
}

static int vpp_init(struct vpp_adapter *adapter)
{
	__vpp_setup(adapter);

	return 0;
}

static void vpp_dump_regs(struct seq_file *s, struct vpp_adapter *adapter)
{
#define VPP_DUMP(fmt, ...) seq_printf(s, fmt, ##__VA_ARGS__)

	VPP_DUMP("VPP Regs:\n");
	VPP_DUMP("CTRL=0x%08x\n", vpp_read_reg(adapter, VPP_CTRL));
	VPP_DUMP("YBASE=0x%08x\n", vpp_read_reg(adapter, VPP_YBASE));
	VPP_DUMP("UBASE=0x%08x\n", vpp_read_reg(adapter, VPP_UBASE));
	VPP_DUMP("VBASE=0x%08x\n", vpp_read_reg(adapter, VPP_VBASE));
	VPP_DUMP("DESBASE=0x%08x\n", vpp_read_reg(adapter, VPP_DESBASE));
	VPP_DUMP("WIDTH =0x%08x\n", vpp_read_reg(adapter, VPP_WIDTH));
	VPP_DUMP("HEIGHT=0x%08x\n", vpp_read_reg(adapter, VPP_HEIGHT));
	VPP_DUMP("STRIDE0=0x%08x\n", vpp_read_reg(adapter, VPP_STRIDE0));
	VPP_DUMP("STRIDE1=0x%08x\n", vpp_read_reg(adapter, VPP_STRIDE1));
	VPP_DUMP("HSCA_COEF00=0x%08x\n",
		vpp_read_reg(adapter, VPP_HSCA_COEF00));
	VPP_DUMP("HSCA_COEF01=0x%08x\n",
		vpp_read_reg(adapter, VPP_HSCA_COEF01));
	VPP_DUMP("HSCA_COEF02=0x%08x\n",
		vpp_read_reg(adapter, VPP_HSCA_COEF02));
	VPP_DUMP("HSCA_COEF10=0x%08x\n",
		vpp_read_reg(adapter, VPP_HSCA_COEF10));
	VPP_DUMP("HSCA_COEF11=0x%08x\n",
		vpp_read_reg(adapter, VPP_HSCA_COEF11));
	VPP_DUMP("HSCA_COEF12=0x%08x\n",
		vpp_read_reg(adapter, VPP_HSCA_COEF12));
	VPP_DUMP("HSCA_COEF20=0x%08x\n",
		vpp_read_reg(adapter, VPP_HSCA_COEF20));
	VPP_DUMP("HSCA_COEF21=0x%08x\n",
		vpp_read_reg(adapter, VPP_HSCA_COEF21));
	VPP_DUMP("HSCA_COEF22=0x%08x\n",
		vpp_read_reg(adapter, VPP_HSCA_COEF22));
	VPP_DUMP("HSCA_COEF30=0x%08x\n",
		vpp_read_reg(adapter, VPP_HSCA_COEF30));
	VPP_DUMP("HSCA_COEF31=0x%08x\n",
		vpp_read_reg(adapter, VPP_HSCA_COEF31));
	VPP_DUMP("HSCA_COEF32=0x%08x\n",
		vpp_read_reg(adapter, VPP_HSCA_COEF32));
	VPP_DUMP("HSCA_COEF40=0x%08x\n",
		vpp_read_reg(adapter, VPP_HSCA_COEF40));
	VPP_DUMP("HSCA_COEF41=0x%08x\n",
		vpp_read_reg(adapter, VPP_HSCA_COEF41));
	VPP_DUMP("HSCA_COEF42=0x%08x\n",
		vpp_read_reg(adapter, VPP_HSCA_COEF42));
	VPP_DUMP("HSCA_COEF50=0x%08x\n",
		vpp_read_reg(adapter, VPP_HSCA_COEF50));
	VPP_DUMP("HSCA_COEF51=0x%08x\n",
		vpp_read_reg(adapter, VPP_HSCA_COEF51));
	VPP_DUMP("HSCA_COEF52=0x%08x\n",
		vpp_read_reg(adapter, VPP_HSCA_COEF52));
	VPP_DUMP("HSCA_COEF60=0x%08x\n",
		vpp_read_reg(adapter, VPP_HSCA_COEF60));
	VPP_DUMP("HSCA_COEF61=0x%08x\n",
		vpp_read_reg(adapter, VPP_HSCA_COEF61));
	VPP_DUMP("HSCA_COEF62=0x%08x\n",
		vpp_read_reg(adapter, VPP_HSCA_COEF62));
	VPP_DUMP("HSCA_COEF70=0x%08x\n",
		vpp_read_reg(adapter, VPP_HSCA_COEF70));
	VPP_DUMP("HSCA_COEF71=0x%08x\n",
		vpp_read_reg(adapter, VPP_HSCA_COEF71));
	VPP_DUMP("HSCA_COEF72=0x%08x\n",
		vpp_read_reg(adapter, VPP_HSCA_COEF72));
	VPP_DUMP("HSCA_COEF80=0x%08x\n",
		vpp_read_reg(adapter, VPP_HSCA_COEF80));
	VPP_DUMP("HSCA_COEF81=0x%08x\n",
		vpp_read_reg(adapter, VPP_HSCA_COEF81));
	VPP_DUMP("HSCA_COEF82=0x%08x\n",
		vpp_read_reg(adapter, VPP_HSCA_COEF82));
	VPP_DUMP("VSCA_COEF00=0x%08x\n",
		vpp_read_reg(adapter, VPP_VSCA_COEF00));
	VPP_DUMP("VSCA_COEF01=0x%08x\n",
		vpp_read_reg(adapter, VPP_VSCA_COEF01));
	VPP_DUMP("VSCA_COEF10=0x%08x\n",
		vpp_read_reg(adapter, VPP_VSCA_COEF10));
	VPP_DUMP("VSCA_COEF11=0x%08x\n",
		vpp_read_reg(adapter, VPP_VSCA_COEF11));
	VPP_DUMP("VSCA_COEF20=0x%08x\n",
		vpp_read_reg(adapter, VPP_VSCA_COEF20));
	VPP_DUMP("VSCA_COEF21=0x%08x\n",
		vpp_read_reg(adapter, VPP_VSCA_COEF21));
	VPP_DUMP("VSCA_COEF30=0x%08x\n",
		vpp_read_reg(adapter, VPP_VSCA_COEF30));
	VPP_DUMP("VSCA_COEF31=0x%08x\n",
		vpp_read_reg(adapter, VPP_VSCA_COEF31));
	VPP_DUMP("VSCA_COEF40=0x%08x\n",
		vpp_read_reg(adapter, VPP_VSCA_COEF40));
	VPP_DUMP("VSCA_COEF41=0x%08x\n",
		vpp_read_reg(adapter, VPP_VSCA_COEF41));
	VPP_DUMP("VSCA_COEF50=0x%08x\n",
		vpp_read_reg(adapter, VPP_VSCA_COEF50));
	VPP_DUMP("VSCA_COEF51=0x%08x\n",
		vpp_read_reg(adapter, VPP_VSCA_COEF51));
	VPP_DUMP("VSCA_COEF60=0x%08x\n",
		vpp_read_reg(adapter, VPP_VSCA_COEF60));
	VPP_DUMP("VSCA_COEF61=0x%08x\n",
		vpp_read_reg(adapter, VPP_VSCA_COEF61));
	VPP_DUMP("VSCA_COEF70=0x%08x\n",
		vpp_read_reg(adapter, VPP_VSCA_COEF70));
	VPP_DUMP("VSCA_COEF71=0x%08x\n",
		vpp_read_reg(adapter, VPP_VSCA_COEF71));
	VPP_DUMP("VSCA_COEF80=0x%08x\n",
		vpp_read_reg(adapter, VPP_VSCA_COEF80));
	VPP_DUMP("VSCA_COEF81=0x%08x\n",
		vpp_read_reg(adapter, VPP_VSCA_COEF81));
	VPP_DUMP("RCOEF=0x%08x\n", vpp_read_reg(adapter, VPP_RCOEF));
	VPP_DUMP("GCOEF=0x%08x\n", vpp_read_reg(adapter, VPP_GCOEF));
	VPP_DUMP("BCOEF=0x%08x\n", vpp_read_reg(adapter, VPP_BCOEF));
	VPP_DUMP("OFFSET1=0x%08x\n", vpp_read_reg(adapter, VPP_OFFSET1));
	VPP_DUMP("OFFSET2=0x%08x\n", vpp_read_reg(adapter, VPP_OFFSET2));
	VPP_DUMP("OFFSET3=0x%08x\n", vpp_read_reg(adapter, VPP_OFFSET3));
	VPP_DUMP("INT_MASK=0x%08x\n", vpp_read_reg(adapter, VPP_INT_MASK));
	VPP_DUMP("INT_STATUS=0x%08x\n", vpp_read_reg(adapter, VPP_INT_STATUS));
	VPP_DUMP("ACC=0x%08x\n", vpp_read_reg(adapter, VPP_ACC));
	VPP_DUMP("FULL_THRESH=0x%08x\n",
		vpp_read_reg(adapter, VPP_FULL_THRESH));
	VPP_DUMP("COLOR_HS_CTRL=0x%08x\n",
		vpp_read_reg(adapter, VPP_COLOR_HS_CTRL));
	VPP_DUMP("COLOR_BC_CTRL=0x%08x\n",
		vpp_read_reg(adapter, VPP_COLOR_BC_CTRL));
	VPP_DUMP("YBASE_BOT=0x%08x\n", vpp_read_reg(adapter, VPP_YBASE_BOT));
	VPP_DUMP("UBASE_BOT=0x%08x\n", vpp_read_reg(adapter, VPP_UBASE_BOT));
	VPP_DUMP("VBASE_BOT=0x%08x\n", vpp_read_reg(adapter, VPP_VBASE_BOT));
	VPP_DUMP("DESBASE_BOT=0x%08x\n",
		vpp_read_reg(adapter, VPP_DESTBASE_BOT));
	VPP_DUMP("INLINE_ADDR=0x%08x\n",
		vpp_read_reg(adapter, VPP_INLINE_ADDR));
}

static void vpp0_dump_regs(struct seq_file *s)
{
	vpp_dump_regs(s, &vpp[0]);
}

static void vpp1_dump_regs(struct seq_file *s)
{
	vpp_dump_regs(s, &vpp[1]);
}

static void __vpp_reset(struct vpp_adapter *adapter)
{
	u32 i;

	vpp_write_reg(adapter, VPP_CTRL, 0);
	vpp_write_reg(adapter, VPP_STRIDE0, 0);
	vpp_write_reg(adapter, VPP_STRIDE1, 0);

	vpp_write_reg(adapter, VPP_OFFSET1, 0);
	vpp_write_reg(adapter, VPP_OFFSET3, 0);

	vpp_write_reg(adapter, VPP_WIDTH, 0);
	vpp_write_reg(adapter, VPP_HEIGHT, 0);

	vpp_write_reg(adapter, VPP_YBASE, 0);
	vpp_write_reg(adapter, VPP_UBASE, 0);
	vpp_write_reg(adapter, VPP_VBASE, 0);

	for (i = 1; i < 3; i++) {
		vpp_write_reg(adapter, y_top_addr_regs[i], 0);
		vpp_write_reg(adapter, y_bot_addr_regs[i], 0);
	}
	vpp_write_reg(adapter, VPP_DESTBASE_BOT, 0);
}

static int __vpp_schedule(struct vpp_adapter *adapter,
			struct vpp_device *in_dev)
{
	enum vdss_vpp_op_type type = VPP_OP_IDEL;
	struct vpp_device *pdev = NULL;
	struct vpp_device *new_dev = NULL;
	int ret = 0;
	unsigned long flags;
	bool changed = false;

	spin_lock_irqsave(&data_lock, flags);

	if (list_empty(&adapter->devices)) {
		__vpp_reset(adapter);
		goto pro_end;
	}

	list_for_each_entry(pdev, &adapter->devices, head) {
		if (pdev->op > type) {
			type = pdev->op;
			new_dev = pdev;
		}
	}

	if (new_dev == NULL)
		goto pro_end;

	/* High priority work is doing, notify the client */
	if (new_dev != in_dev && in_dev && in_dev->func)
		in_dev->func(in_dev->arg, in_dev->vpp_id, type);

	if (new_dev != adapter->cur_dev) {
		pdev = adapter->cur_dev;

		/* Preempt successfully, notify the new client */
		if (in_dev == new_dev && new_dev->func && pdev)
			new_dev->func(new_dev->arg, new_dev->vpp_id, pdev->op);

		/* High priority work will do, notify the current client */
		if (pdev != in_dev && pdev && pdev->func)
			pdev->func(pdev->arg, pdev->vpp_id, type);

		/*
		 * when switch back to passthrough, should program
		 * VPP again, skip flip in the next frame
		 * */
		if (new_dev->op == VPP_OP_PASS_THROUGH) {
			new_dev->info.params.op.passthrough.flip = false;
			new_dev->info.is_dirty = true;
		}

		adapter->cur_dev = new_dev;
		changed = true;
	}

	pdev = adapter->cur_dev;
	if (pdev->info.is_dirty) {
		switch (pdev->op) {
		case VPP_OP_BITBLT:
			__vpp_blt(adapter, &pdev->info.params.op.blt);
			break;
		case VPP_OP_INLINE:
			__vpp_inline(adapter,
				&pdev->info.params.op.inline_mode);
			break;
		case VPP_OP_PASS_THROUGH:
			__vpp_passthrough(adapter,
				&pdev->info.params.op.passthrough);
			break;
		case VPP_OP_IBV:
			__vpp_ibv(adapter, &pdev->info.params.op.ibv);
			break;
		default:
			ret = -1;
			goto pro_end;
		}
		pdev->info.is_dirty = false;
	}

	if (changed) {
		if (in_dev != new_dev && new_dev->func != NULL)
			new_dev->func(new_dev->arg, new_dev->vpp_id, type);
	}

pro_end:
	spin_unlock_irqrestore(&data_lock, flags);
	return ret;
}

static bool __vpp_is_busy(struct vpp_adapter *adapter,
			enum vdss_vpp_op_type type)
{
	struct vpp_device *pdev;

	if (list_empty(&adapter->devices))
		return false;

	list_for_each_entry(pdev, &adapter->devices, head) {
		if (pdev->op >= type)
			return true;
	}

	return false;
}

static int vpp_blt(struct vpp_device *pdev,
		struct vdss_vpp_blt_params *params)
{
	struct vpp_info info;
	struct vpp_adapter *adapter = NULL;
	u32 i;
	unsigned long flags;
	int ret = 0;

	if (params == NULL)
		return -EINVAL;

	memset(&info, 0, sizeof(struct vpp_info));

	spin_lock_irqsave(&data_lock, flags);

	if (pdev == NULL) {
		for (i = 0; i < NUM_VPP; i++) {
			if (!__vpp_is_busy(&vpp[i], VPP_OP_BITBLT)) {
				adapter = &vpp[i];
				break;
			}
		}
	} else if (!__vpp_is_busy(&vpp[pdev->vpp_id], VPP_OP_BITBLT))
			adapter = &vpp[pdev->vpp_id];

	if (adapter != NULL)
		__vpp_blt(adapter, params);
	else
		ret = -EBUSY;

	spin_unlock_irqrestore(&data_lock, flags);

	if (!ret)
		ret = __vpp_wait_for_idle(adapter);

	return ret;
}

static int vpp_inline(struct vpp_device *pdev,
		struct vdss_vpp_inline_params *params)
{
	int ret = 0;
	unsigned long flags;

	if (pdev == NULL || params == NULL)
		return -EINVAL;

	spin_lock_irqsave(&data_lock, flags);

	pdev->op = VPP_OP_INLINE;
	pdev->info.params.op.inline_mode = *params;
	pdev->info.is_dirty = true;

	spin_unlock_irqrestore(&data_lock, flags);

	__vpp_schedule(&vpp[pdev->vpp_id], pdev);

	return ret;
}

static int vpp_passthrough(struct vpp_device *pdev,
			struct vdss_vpp_passthrough_params *params)
{
	int ret = 0;
	unsigned long flags;

	if (pdev == NULL || params == NULL)
		return -EINVAL;

	spin_lock_irqsave(&data_lock, flags);

	pdev->op = VPP_OP_PASS_THROUGH;
	if (params->flip) {
		pdev->info.params.op.passthrough.src_surf.base =
				params->src_surf.base;
		pdev->info.params.op.passthrough.flip = true;
	} else {
		pdev->info.params.op.passthrough = *params;
	}
	pdev->info.is_dirty = true;

	spin_unlock_irqrestore(&data_lock, flags);

	__vpp_schedule(&vpp[pdev->vpp_id], pdev);

	return ret;
}

static int vpp_ibv(struct vpp_device *pdev,
		struct vdss_vpp_ibv_params *params)
{
	unsigned long flags;

	if (pdev == NULL || params == NULL)
		return -EINVAL;

	spin_lock_irqsave(&data_lock, flags);

	pdev->op = VPP_OP_IBV;
	memset(&pdev->info, 0, sizeof(struct vpp_info));
	pdev->info.is_dirty = true;
	pdev->info.params.op.ibv = *params;

	spin_unlock_irqrestore(&data_lock, flags);

	__vpp_schedule(&vpp[pdev->vpp_id], pdev);

	return 0;
}

bool sirfsoc_vpp_is_passthrough_support(enum vdss_pixelformat fmt)
{
	switch (fmt) {
	case VDSS_PIXELFORMAT_YUYV:
	case VDSS_PIXELFORMAT_UYVY:
	case VDSS_PIXELFORMAT_YUY2:
	case VDSS_PIXELFORMAT_YUNV:
	case VDSS_PIXELFORMAT_YVYU:
	case VDSS_PIXELFORMAT_UYNV:
	case VDSS_PIXELFORMAT_VYUY:
	case VDSS_PIXELFORMAT_IMC1:
	case VDSS_PIXELFORMAT_IMC3:
	case VDSS_PIXELFORMAT_YV12:
	case VDSS_PIXELFORMAT_I420:
	case VDSS_PIXELFORMAT_Q420:
	case VDSS_PIXELFORMAT_UYVI:
	case VDSS_PIXELFORMAT_NV12:
	case VDSS_PIXELFORMAT_NV21:
	case VDSS_PIXELFORMAT_NJ12:
		return true;
	default:
		return false;
	}
}
EXPORT_SYMBOL(sirfsoc_vpp_is_passthrough_support);

void *sirfsoc_vpp_create_device(enum vdss_vpp id,
				struct vdss_vpp_create_device_params *params)
{
	struct vpp_adapter *adapter = &vpp[id];
	unsigned long flags;
	struct vpp_device *pdev = NULL;

	pdev = kzalloc(sizeof(struct vpp_device), GFP_KERNEL);
	if (pdev != NULL) {
		pdev->vpp_id = id;
		pdev->func = params->func;
		pdev->arg = params->arg;
		pdev->op = VPP_OP_IDEL;
		memset(&pdev->info, 0, sizeof(pdev->info));

		spin_lock_irqsave(&data_lock, flags);
		list_add_tail(&pdev->head, &adapter->devices);
		spin_unlock_irqrestore(&data_lock, flags);
	}

	return pdev;
}
EXPORT_SYMBOL(sirfsoc_vpp_create_device);

int sirfsoc_vpp_destroy_device(void *handle)
{
	unsigned long flags;
	struct vpp_device *pdev = (struct vpp_device *)handle;

	if (pdev == NULL)
		return -EINVAL;

	spin_lock_irqsave(&data_lock, flags);
	if (vpp[pdev->vpp_id].cur_dev == pdev)
		vpp[pdev->vpp_id].cur_dev = NULL;
	list_del_init(&pdev->head);
	spin_unlock_irqrestore(&data_lock, flags);

	__vpp_schedule(&vpp[pdev->vpp_id], NULL);

	kfree(pdev);
	return 0;
}
EXPORT_SYMBOL(sirfsoc_vpp_destroy_device);

int sirfsoc_vpp_present(void *handle, struct vdss_vpp_op_params *params)
{
	int ret = 0;

	if (params == NULL)
		return -EINVAL;

	switch (params->type) {
	case VPP_OP_BITBLT:
		ret = vpp_blt(handle, &params->op.blt);
		break;
	case VPP_OP_INLINE:
		ret = vpp_inline(handle, &params->op.inline_mode);
		break;
	case VPP_OP_PASS_THROUGH:
		ret = vpp_passthrough(handle, &params->op.passthrough);
		break;
	case VPP_OP_IBV:
		ret = vpp_ibv(handle, &params->op.ibv);
		break;
	default:
		vpp_err("%s: wrong operation\n", __func__);
		return -EINVAL;
	}

	return ret;
}
EXPORT_SYMBOL(sirfsoc_vpp_present);

#ifdef CONFIG_PM_SLEEP
static int sirfsoc_vpp_suspend(struct device *dev)
{
	struct vpp_adapter *adapter;

	adapter = dev_get_drvdata(dev);
	clk_disable_unprepare(adapter->clk);
	return 0;
}

static int sirfsoc_vpp_pm_resume(struct device *dev)
{
	struct vpp_adapter *adapter;
	int ret = 0;

	adapter = dev_get_drvdata(dev);
	ret = clk_prepare_enable(adapter->clk);
	if (!ret)
		ret = vpp_init(adapter);
	return ret;
}
#endif

static const struct dev_pm_ops sirfsoc_vpp_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(sirfsoc_vpp_suspend,
				     sirfsoc_vpp_pm_resume)
};

static ssize_t vpp_status_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int ctrl;
	unsigned long flags;
	struct vpp_adapter *ddev = dev_get_drvdata(dev);

	spin_lock_irqsave(&data_lock, flags);
	ctrl = vpp_read_reg(ddev, VPP_CTRL);
	spin_unlock_irqrestore(&data_lock, flags);

	if (ctrl & VPP_CTRL_INLINE_EN)
		return snprintf(buf, PAGE_SIZE,
				"%s\n", "INLINE MODE");
	else if (ctrl & VPP_CTRL_DEST)
		return snprintf(buf, PAGE_SIZE,
				"%s\n", "PASSTHROUGH MODE");
	else if (ctrl & VPP_CTRL_BUSY_STATUS)
		return snprintf(buf, PAGE_SIZE,
				"%s\n", "BLT MODE");
	else
		return snprintf(buf, PAGE_SIZE,
				"%s\n", "IDLE");
}

static DEVICE_ATTR(vpp_status, S_IRUGO,
	vpp_status_show, NULL);

static const struct attribute *vpp_sysfs_attrs[] = {
	&dev_attr_vpp_status.attr,
	NULL
};

static int vpp_init_sysfs(struct vpp_adapter *adapter)
{
	int ret = 0;
	struct platform_device *pdev = vdss_get_core_pdev();

	if (adapter == NULL || pdev == NULL)
		return -EINVAL;

	ret = sysfs_create_files(&adapter->pdev->dev.kobj,
			vpp_sysfs_attrs);
	if (ret) {
		VDSSERR("failed to create sysfs files!\n");
		return ret;
	}

	ret = sysfs_create_link(&pdev->dev.kobj,
			&adapter->pdev->dev.kobj, adapter->name);
	if (ret) {
		sysfs_remove_files(&adapter->pdev->dev.kobj,
			vpp_sysfs_attrs);
		VDSSERR("failed to create sysfs display link\n");
		return ret;
	}

	return ret;
}

static int vpp_uninit_sysfs(struct vpp_adapter *adapter)
{
	struct platform_device *pdev = vdss_get_core_pdev();

	if (adapter == NULL || pdev == NULL)
		return -EINVAL;

	sysfs_remove_link(&pdev->dev.kobj, adapter->name);
	sysfs_remove_files(&adapter->pdev->dev.kobj,
				vpp_sysfs_attrs);

	return 0;
}

static int sirfsoc_vpp_probe(struct platform_device *pdev)
{
	struct device_node *dn = pdev->dev.of_node;
	struct resource *res;
	struct vpp_adapter *adapter;
	u32 index;
	int ret = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		VDSSERR("can't get IORESOURCE_MEM\n");
		return -EINVAL;
	}

	if (of_property_read_u32(dn, "cell-index", &index)) {
		dev_err(&pdev->dev, "Fail to get vpp index\n");
		return -ENODEV;
	}

	if (index > NUM_VPP - 1) {
		dev_err(&pdev->dev, "vpp index error\n");
		return -ENODEV;
	}

	adapter = &vpp[index];
	adapter->pdev = pdev;
	adapter->base = devm_ioremap(&pdev->dev, res->start,
					resource_size(res));

	if (!adapter->base) {
		VDSSERR("can't ioremap\n");
		return -ENOMEM;
	}

	if (of_device_is_compatible(pdev->dev.of_node, "sirf,atlas7-vpp"))
		adapter->is_atlas7 = true;

	adapter->irq = platform_get_irq(pdev, 0);
	if (adapter->irq < 0) {
		VDSSERR("platform_get_irq failed\n");
		return -ENODEV;
	}

	adapter->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(adapter->clk)) {
		VDSSERR("Failed to get vpp clock!\n");
		return -ENODEV;
	}

	clk_prepare_enable(adapter->clk);

	if (device_reset(&pdev->dev)) {
		VDSSERR("Failed to reset vpp %d\n", index);
		return  -EINVAL;
	}

	adapter->id = index;
	sprintf(adapter->name, "vpp%d", index);
	if (index == 0)
		vdss_debugfs_create_file("vpp0_regs", vpp0_dump_regs);
	else if (index == 1)
		vdss_debugfs_create_file("vpp1_regs", vpp1_dump_regs);

	INIT_LIST_HEAD(&adapter->devices);
	adapter->cur_dev = NULL;

	vpp_init_irq(adapter);
	vpp_init(adapter);

	platform_set_drvdata(pdev, adapter);

	ret = vpp_init_sysfs(adapter);

	return ret;
}

static int __exit sirfsoc_vpp_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct vpp_adapter *adapter = dev_get_drvdata(dev);

	vpp_uninit_sysfs(adapter);

	return 0;
}

static const struct of_device_id vpp_of_match[] = {
	{ .compatible = "sirf,prima2-vpp", },
	{ .compatible = "sirf,atlas7-vpp", },
	{},
};

static struct platform_driver sirfsoc_vpp_driver = {
	.remove         = sirfsoc_vpp_remove,
	.driver         = {
		.name   = "sirfsoc_vpp",
		.pm	= &sirfsoc_vpp_pm_ops,
		.owner  = THIS_MODULE,
		.of_match_table = vpp_of_match,
	},
};

int __init vpp_init_platform_driver(void)
{
	return platform_driver_probe(&sirfsoc_vpp_driver,
		sirfsoc_vpp_probe);
}

void vpp_uninit_platform_driver(void)
{
	platform_driver_unregister(&sirfsoc_vpp_driver);
}
