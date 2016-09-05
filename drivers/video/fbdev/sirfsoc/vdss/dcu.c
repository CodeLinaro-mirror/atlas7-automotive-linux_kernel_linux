/*
 * linux/drivers/video/fbdev/sirfsoc/vdss/dcu.c
 *
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
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

#define VDSS_SUBSYS_NAME "DCU"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/suspend.h>

#include <video/sirfsoc_vdss.h>
#include "vdss.h"
#include "dcu.h"

#define DCU_IPP_LEVEL	1
#define DCU_MOT_WIDTH_MAX	1920
#define DCU_MOT_HEIGHT_MAX	544
#define DCU_EE_LUMA_AUTO_FILTER_INDEX	4
#define DCU_EE_CHROMA_AUTO_FILTER_INDEX	0

/* DCU EE sharpness level range is 0 ~ 100 */
#define DCU_EE_SHARPNESS_LEVEL	50

struct dcu_device {
	/* static fields */
	unsigned char name[8];

	struct platform_device *pdev;
	void __iomem *core_base;
	void __iomem *nocfifo_base;

	int irq;
	bool inline_en;
	struct clk *clk;
	struct dcu_param_set dcu_params;
	struct vdss_dcu_op_params vdss_params;
};

static struct dcu_device dcu;

static struct dcu_ipp_setting dcu_ipp_setting[2] = {
	{0,	0,	0,	0}, /* off */
	{2,	2,	2,	2}, /* light */
};

static ssize_t dcu_inline_enable_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct dcu_device *ddev = dev_get_drvdata(dev);

	bool e = ddev->inline_en;

	return snprintf(buf, PAGE_SIZE, "%d\n", e);
}

static ssize_t dcu_inline_enable_store(struct device *dev,
	struct device_attribute *attr,
	const char *buf, size_t size)
{
	int r;
	bool e;
	struct dcu_device *ddev = dev_get_drvdata(dev);

	r = strtobool(buf, &e);
	if (r)
		return r;

	if (e)
		ddev->inline_en = true;
	else
		ddev->inline_en = false;

	return size;
}

static ssize_t dcu_status_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct dcu_device *ddev = dev_get_drvdata(dev);
	int val = dcu_read_reg(ddev->core_base, DCU_LLU_DONE_SEL);

	if (val & DCU_START)
		if (val & INLINE_DONE)
			return snprintf(buf, PAGE_SIZE,
				"%s\n", "INLINE MODE");
		else
			return snprintf(buf, PAGE_SIZE,
				"%s\n", "BLT MODE");
	else
		return snprintf(buf, PAGE_SIZE, "%s\n", "IDLE");
}

static DEVICE_ATTR(dcu_inline_enable, S_IRUGO|S_IWUSR,
	dcu_inline_enable_show, dcu_inline_enable_store);

static DEVICE_ATTR(dcu_status, S_IRUGO,
	dcu_status_show, NULL);

static const struct attribute *dcu_sysfs_attrs[] = {
	&dev_attr_dcu_inline_enable.attr,
	&dev_attr_dcu_status.attr,
	NULL
};

static int dcu_init_sysfs(struct dcu_device *ddev)
{
	int ret = 0;
	struct platform_device *pdev = vdss_get_core_pdev();

	if (ddev == NULL || pdev == NULL)
		return -EINVAL;

	ret = sysfs_create_files(&ddev->pdev->dev.kobj,
			dcu_sysfs_attrs);
	if (ret) {
		VDSSERR("failed to create sysfs files!\n");
		return ret;
	}

	ret = sysfs_create_link(&pdev->dev.kobj,
			&ddev->pdev->dev.kobj, "dcu");
	if (ret) {
		sysfs_remove_files(&ddev->pdev->dev.kobj,
			dcu_sysfs_attrs);
		VDSSERR("failed to create sysfs display link\n");
		return ret;
	}

	return ret;
}

static int dcu_uninit_sysfs(struct dcu_device *ddev)
{
	struct platform_device *pdev = vdss_get_core_pdev();

	if (ddev == NULL || pdev == NULL)
		return -EINVAL;

	sysfs_remove_link(&pdev->dev.kobj, "dcu");
	sysfs_remove_files(&ddev->pdev->dev.kobj,
				dcu_sysfs_attrs);

	return 0;
}

bool dcu_inline_check_size(struct vdss_surface *src_surf,
	struct vdss_rect *src_rect,
	int *psrc_skip,
	struct vdss_rect *dst_rect,
	int *pdst_skip)
{
	int src_rect_width, src_rect_height;
	int dst_rect_width, dst_rect_height;
	int pixel_aligned = 0;

	/*
	 * DMA address must be 8 bytes aligned, so
	 * we must shift the src address
	 * */
	switch (src_surf->fmt) {
	case VDSS_PIXELFORMAT_NV12:
	case VDSS_PIXELFORMAT_NV21:
		pixel_aligned = 8;
		break;
	case VDSS_PIXELFORMAT_I420:
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
	 * scaling for this hardware limitatin.
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
	 * Inline mode, driver use DCU do vertical scaling, so
	 * the dst height must match the request of VPP source:
	 *    1. vpp need read three lines at the beginning;
	 *    2. TODO: workaround solution
	 *       If the dst height of DCU is less than 32,
	 *       fifo underflow randomly happens in DCU+VPP scaling
	 *       overnight test.
	 *       According to the test result, 32 is a safe size of
	 *       dst height
	 *    3. height of VPP source surface must be integer
	 *       multiples of 2.
	 * */
	if (dst_rect_height < 32) {
		VDSSWARN("The height of dst rect is less than 4!\n");
		return false;
	}

	if (dst_rect_height & 0x01) {
		if (src_rect_height > ((dst_rect_height - 1) << 3))
			src_rect_height = ((dst_rect_height - 1) << 3);
		else
			src_rect_height = src_rect_height -
				src_rect_height/dst_rect_height;

		dst_rect_height = dst_rect_height - 1;

		dst_rect->bottom = dst_rect->top + dst_rect_height - 1;
		src_rect->bottom = src_rect->top + src_rect_height - 1;
	}

	src_rect_width = src_rect->right - src_rect->left + 1;
	dst_rect_width = dst_rect->right - dst_rect->left + 1;

	/*
	 * Inline mode, driver use VPP do horizontal scaling, so
	 * the src width must match the request of VPP source:
	 *    1. width of VPP source surface must be integer
	 *       multiples of 2.
	 * */
	if (src_rect_width < 2) {
		VDSSWARN("The width of src rect is less than 2!\n");
		return false;
	}

	if (src_rect_width & 0x01) {
		if (dst_rect_width > ((src_rect_width - 1) << 3))
			dst_rect_width = ((src_rect_width - 1) << 3);
		else
			dst_rect_width = dst_rect_width -
				dst_rect_width/src_rect_width;

		src_rect_width = src_rect_width - 1;

		src_rect->right = src_rect->left + src_rect_width - 1;
		dst_rect->right = dst_rect->left + dst_rect_width - 1;
	}

	/*
	 * At present, DCU driver doesn't support source clip
	 * */
	if (src_rect_width < src_surf->width ||
	    src_rect->top != 0) {
		VDSSWARN("Source clip isn't supported\n");
		return false;
	}

	return true;
}

unsigned int dcu_read_reg(void __iomem *iomem,
		unsigned int offset)
{
	return readl(iomem + offset);
}

void dcu_write_reg(void __iomem *iomem,
		unsigned int offset,
		unsigned int value)
{
	writel(value, iomem + offset);
}

static void __dcu_reset(struct dcu_param_set *dcu_param)
{
	void __iomem *base = dcu_param->core_iomem;

	/* disable DCU interrupts */
	dcu_write_reg(base, DCU_INTR_EN, 0x0);
	/* stop DCU processing */
	dcu_write_reg(base, DCU_CTRL, 0x0);
	dcu_write_reg(base, DCU_BLANK, VBLANK_SIZE | HBLANK_SIZE);
	dcu_write_reg(base, DCU_DITHER, RAND2_EN_B | RAND3_EN_B|
					RAND2_EN_A | RAND3_EN_A);
	dcu_write_reg(base, DCU_NRDNT_HORIZONTAL_EDGE_DETECT, EDGE_DETECT_EN);

	dcu_nrdnt_reset(dcu_param);
	dcu_vs_reset(dcu_param);
	dcu_ipp_reset(dcu_param);
}

static int __dcu_init(struct dcu_param_set *dcu_param)
{
	__dcu_reset(dcu_param);
	dcu_nrdnt_init(dcu_param);
	dcu_vs_init(dcu_param);
	dcu_ipp_init(dcu_param);
	dcu_ee_init(dcu_param);

	return 0;
}

static void __dcu_setup(struct dcu_param_set *dcu_param)
{
	/*setup DCU NRDNT parameter*/
	dcu_nrdnt_set_lowangle_interpmode(dcu_param, 14, 0, 1, 1);
	dcu_nrdnt_set_lowangle_interpv90mode(dcu_param, 5, 0, 0, 1);
	dcu_nrdnt_set_mode(dcu_param, 0, 2, 2, 0);
	dcu_nrdnt_set_frame_th(dcu_param, 4, 6, 8);
	dcu_nrdnt_set_field_th(dcu_param, 1, 2, 4, 1, 0xe6);
	dcu_nrdnt_set_frame_mode(dcu_param, false, 0, 0, 0);
	dcu_nrdnt_set_frame_mad(dcu_param, 0, 40, 20, 30, 30, 2, 2);
	dcu_nrdnt_set_frame_field(dcu_param, 32, 64, 8);
	dcu_nrdnt_set_frame_recursive(dcu_param, true, 0, 16, 1, 2);
	dcu_nrdnt_set_k_history(dcu_param, 1, 1, 2, 6);

	/*setup DCU IPP parameter*/
	dcu_ipp_set_mode(dcu_param,
		dcu_ipp_setting[DCU_IPP_LEVEL].dblk_v_mode,
		dcu_ipp_setting[DCU_IPP_LEVEL].dblk_vc_mode,
		dcu_ipp_setting[DCU_IPP_LEVEL].dblk_h_mode,
		dcu_ipp_setting[DCU_IPP_LEVEL].dblk_hc_mode);

	dcu_ipp_set_luma(dcu_param, true, 0, 0xc8, 0x20, 0x80);
	dcu_ipp_set_chroma(dcu_param, true, 0, 0xa0, 0x8, 0x40, 0x3);
	dcu_ipp_set_det_mode(dcu_param, true, 0, 0, 0);
	dcu_ipp_set_det_thresh(dcu_param, true,
			0x10, 0x190, 0x80, 0x5, 0xc8, 0x10);
	dcu_ipp_set_luma(dcu_param, false, 0, 0xc8, 0x20, 0x80);
	dcu_ipp_set_chroma(dcu_param, false, 0, 0xa0, 0x8, 0x40, 0x3);
	dcu_ipp_set_det_mode(dcu_param, false, 0, 0, 0);
	dcu_ipp_set_det_thresh(dcu_param, false, 0x10, 0x190, 0x80,
			0x5, 0xc8, 0x10);

	/*set up DCU EE parameter*/
	dcu_ee_set_sharpness(dcu_param, DCU_EE_SHARPNESS_LEVEL);
	dcu_ee_set_luma_filter(dcu_param, DCU_EE_LUMA_AUTO_FILTER_INDEX);
	dcu_ee_set_chroma_filter(dcu_param, DCU_EE_CHROMA_AUTO_FILTER_INDEX);

	dcu_nrdnt_set_deint_mode(dcu_param, 0x7, 0x1, 0x1);

	dcu_param->cmif_disable = NXT_WR_DIS | BOT_PROG_WR_DIS |
				TOP_PROG_WR_DIS | PREV_RD_DIS;
	dcu_param->linear_config = SWAP_C_8 | SWAP_Y_8;
	dcu_param->tempbuf_base = 0x0;
}

static void __dcu_set_src_rect(struct dcu_param_set *dcu_param,
				struct vdss_rect *src_rect,
				struct vdss_rect *dst_rect)
{
	u32 src_width = src_rect->right - src_rect->left + 1;
	u32 src_height = src_rect->bottom - src_rect->top + 1;
	u32 dst_width = dst_rect->right - dst_rect->left + 1;
	u32 dst_height = dst_rect->bottom - dst_rect->top + 1;
	struct dcu_vs_params *vs_param = &dcu_param->vs_params;

	/*DCU clip and output width/height parameter setting*/
	vs_param->clip_width = src_width;
	vs_param->clip_height = src_height;
	vs_param->output_width = dst_width;
	vs_param->output_height = dst_height;

	if (src_width > 720 && src_height > 576)
		dcu_nrdnt_set_field_automode(dcu_param, 0x1, 0xf000);
	else
		dcu_nrdnt_set_field_automode(dcu_param, 0x1, 0x8000);

	dcu_ipp_setup(dcu_param);
	dcu_ee_set_scale_factor(dcu_param, src_height, dst_height);
	dcu_vs_calc_params(dcu_param);
}

static void __dcu_update_reg(struct dcu_param_set *dcu_param)
{
	u32 reg_val;
	void __iomem *base = dcu_param->core_iomem;
	void __iomem *nocfifo_base = dcu_param->nocfifo_iomem;
	struct dcu_vs_params *vs_param = &dcu_param->vs_params;
	struct dcu_ee_params *ee_param = &dcu_param->ee_params;
	struct dcu_ipp_params *ipp_param = &dcu_param->ipp_params;
	struct dcu_nrdnt_params *nrdnt_param = &dcu_param->nrdnt_params;

	if (dcu_param->is_inline)
		dcu_write_reg(base, DCU_LLU_DONE_SEL,
				SEL1_DISABLE | INLINE_DONE);

	dcu_write_reg(base, DCU_LBUF, LBUF_START);
	dcu_write_reg(base, DCU_FUT_FBUF_RD, FIELD_PIX_SIZE | FUT_BASE);
	dcu_write_reg(base, DCU_TOP_FBUF_RD, FIELD_PIX_SIZE | TOP_BASE);
	dcu_write_reg(base, DCU_BOT_FBUF_RD, FIELD_PIX_SIZE | BOT_BASE);
	dcu_write_reg(base, DCU_MOT_FBUF_RD, MOT_PIX_SIZE | RD_BASE);
	dcu_write_reg(base, DCU_MOT_FBUF_WR, MOT_PIX_SIZE | WR_BASE);
	dcu_write_reg(base, DCU_CMIF_DISABLE, dcu_param->cmif_disable);
	dcu_write_reg(base, DCU_DITHER, RAND2_EN_B | RAND3_EN_B|
					RAND2_EN_A | RAND3_EN_A);
	dcu_write_reg(base, DCU_NUM_STRIP, 0x0);
	dcu_write_reg(base, DCU_STRIP_SIZE1, vs_param->left_strip_size);
	reg_val = 0x0;
	reg_val |= (vs_param->clip_height >> 1);
	reg_val |= (vs_param->output_height << 16);
	dcu_write_reg(base, DCU_VSIZE, reg_val);

	dcu_write_reg(base, DCU_NRDNT_INPUT_FORMAT, dcu_param->input_format);
	dcu_write_reg(base, DCU_MIF2AXI_VPP_CTRL, dcu_param->vpp_ctrl);
	dcu_write_reg(base, DCU_NRDNT_DEINT_MODE, nrdnt_param->deint_mode);
	dcu_write_reg(base, DCU_NRDNT_LAI_CTRL, nrdnt_param->interp_ctrl);
	dcu_write_reg(base, DCU_NRDNT_LAI_V90_CTRL,
		nrdnt_param->interp_v90_ctrl);
	dcu_write_reg(base, DCU_NRDNT_LAI_CEIL, LAI_VAR_CEIL);
	dcu_write_reg(base, DCU_NRDNT_LAI_VAR_FLOOR, LAI_VAR_FLOOR);
	dcu_write_reg(base, DCU_NRDNT_LAI_VAR_FUNC, LAI_VAR_FUNC);
	dcu_write_reg(base, DCU_NRDNT_LAI_NOISE_FLOOR, LAI_NOISE_FLOOR);
	dcu_write_reg(base, DCU_NRDNT_LAI_VIOLATE, LAI_VIOLATE);
	dcu_write_reg(base, DCU_NRDNT_HORIZONTAL_EDGE_DETECT, EDGE_DETECT_EN);
	dcu_write_reg(base, DCU_NRDNT_NOISE_LEVEL_SOURCE, NOISE_LEVEL_SRC);
	dcu_write_reg(base, DCU_NRDNT_IMPLUSE_WINDOW_SEL, IMPLUSE_WIN_SEL);

	dcu_write_reg(base, DCU_NRDNT_MOTION_CTRL,
			nrdnt_param->luma_mdet_mode);
	dcu_write_reg(base, DCU_NRDNT_MOTION_DETECTION,
		nrdnt_param->chroma_mdet_mode);
	dcu_write_reg(base, DCU_NRDNT_FRAME_MOTION_THA,
		nrdnt_param->frame_mdet_tha);
	dcu_write_reg(base, DCU_NRDNT_FRAME_MOTION_THB,
		nrdnt_param->frame_mdet_thb);
	dcu_write_reg(base, DCU_NRDNT_FRAME_MOTION_THC,
		nrdnt_param->frame_mdet_thc);
	dcu_write_reg(base, DCU_NRDNT_FIELD_MOTION_THA,
		nrdnt_param->field_mdet_tha);
	dcu_write_reg(base, DCU_NRDNT_FIELD_MOTION_THB,
		nrdnt_param->field_mdet_thb);
	dcu_write_reg(base, DCU_NRDNT_FIELD_MOTION_THC,
		nrdnt_param->field_mdet_thc);
	dcu_write_reg(base, DCU_NRDNT_FIELD_MOTION_GAIN,
		nrdnt_param->field_mdet_gain);
	dcu_write_reg(base, DCU_NRDNT_FIELD_MOTION_AUTO_DIS_MSB,
		nrdnt_param->field_mdet_autodismsb);
	dcu_write_reg(base, DCU_NRDNT_FIELD_MOTION_AUTO_DIS_LSB,
		nrdnt_param->field_mdet_autodislsb);
	dcu_write_reg(base, DCU_NRDNT_FRAME_MDETC,
		nrdnt_param->frame_mdet_mode);
	dcu_write_reg(base, DCU_NRDNT_FRAME_MDETC_MAD_BLEND_TH,
		nrdnt_param->frame_mdet_blendth);
	dcu_write_reg(base, DCU_NRDNT_FRAME_MDETC_MAD_BLEND_RES,
		nrdnt_param->frame_mdet_blendres);
	dcu_write_reg(base, DCU_NRDNT_FRAME_MDETC_MAD_GAIN,
		nrdnt_param->frame_mdet_gain);
	dcu_write_reg(base, DCU_NRDNT_FRAME_MDETC_MAD_GAIN_CORE,
		nrdnt_param->frame_mdet_gaincore);
	dcu_write_reg(base, DCU_NRDNT_FRAME_MDETC_MAD_GAIN_CORE2,
		nrdnt_param->frame_mdet_gaincore2);
	dcu_write_reg(base, DCU_NRDNT_FRAME_MDETC_MAD_GAIN_CORE_TH,
		nrdnt_param->frame_mdet_gaincoreth);
	dcu_write_reg(base, DCU_NRDNT_FRAME_MDETC_MAD_GAIN_CORE2_TH,
		nrdnt_param->frame_mdet_gaincore2th);
	dcu_write_reg(base, DCU_NRDNT_FRAME_MDETC_FIELD_GAIN,
		nrdnt_param->frame_mdet_fieldgain);
	dcu_write_reg(base, DCU_NRDNT_FRAME_MDETC_FIELD_CORE_GAIN,
		nrdnt_param->frame_mdet_fieldcoregain);
	dcu_write_reg(base, DCU_NRDNT_FRAME_MDETC_FIELD_CORE_TH,
		nrdnt_param->frame_mdet_fieldcoreth);
	dcu_write_reg(base, DCU_NRDNT_FRAME_MDETC_RECURSIVE,
		nrdnt_param->frame_mdet_recursive);
	dcu_write_reg(base, DCU_NRDNT_FRAME_MDETC_RECURSIVE_GAIN,
		nrdnt_param->frame_mdet_recursivegain);
	dcu_write_reg(base, DCU_NRDNT_NOISE_FILTER_ENABLE, NOISE_FILTER_EN);
	dcu_write_reg(base, DCU_NRDNT_K_FRAME_MODE,
		nrdnt_param->k_history_det);

	/* DCU scaler parameter setting */
	dcu_write_reg(base, DCU_VS_CFG, VS_EN);
	reg_val = 0x0;
	reg_val |= (vs_param->num_tap << 4);
	reg_val |= (vs_param->num_phase << 16);
	dcu_write_reg(base, DCU_VS_LUM_V_INT_CTRL, reg_val);
	reg_val = 0x0;
	reg_val |= ((u16)vs_param->start_line << 0);
	reg_val |= (vs_param->frac_offset << 26);
	dcu_write_reg(base, DCU_VS_LUM_V_INT_START, reg_val);
	reg_val = 0x0;
	reg_val |= (vs_param->scale_factor << 0);
	dcu_write_reg(base, DCU_VS_LUM_V_SCALE_PAR, reg_val);
	reg_val = 0x0;
	reg_val |= ((u16)vs_param->under_shoot << 0);
	reg_val |= (vs_param->over_shoot << 16);
	dcu_write_reg(base, DCU_VS_LUM_V_DDA_PAR, reg_val);
	reg_val = 0x0;
	reg_val |= ((u16)vs_param->init_phase << 0);
	dcu_write_reg(base, DCU_VS_LUM_V_DDA_START, reg_val);
	dcu_vs_load_coefset(dcu_param);

	dcu_write_reg(base, DCU_MPP_VBLKDET_CNTL0, ipp_param->vblk_det_ctrl);
	dcu_write_reg(base, DCU_MPP_VBLKDET_CNTL1, ipp_param->vblk_det_th13);
	dcu_write_reg(base, DCU_MPP_VBLKDET_CNTL2, ipp_param->vblk_det_th46);
	dcu_write_reg(base, DCU_MPP_HBLKDET_CNTL0, ipp_param->hblk_det_ctrl);
	dcu_write_reg(base, DCU_MPP_HBLKDET_CNTL1, ipp_param->hblk_det_th13);
	dcu_write_reg(base, DCU_MPP_HBLKDET_CNTL2, ipp_param->hblk_det_th46);
	dcu_write_reg(base, DCU_MPP_VDBK_CNTL, ipp_param->vblk_luma_ctrl);
	dcu_write_reg(base, DCU_MPP_VDBK_C_CNTL, ipp_param->vblk_chroma_ctrl);
	dcu_write_reg(base, DCU_MPP_VDBK_C_CNTL2,
		ipp_param->vblk_chroma_ctrl2);
	dcu_write_reg(base, DCU_MPP_HDBK_CNTL, ipp_param->hblk_luma_ctrl);
	dcu_write_reg(base, DCU_MPP_HDBK_C_CNTL, ipp_param->hblk_chroma_ctrl);
	dcu_write_reg(base, DCU_MPP_HDBK_C_CNTL2, ipp_param->hblk_chroma_ctrl2);
	dcu_write_reg(base, DCU_MPP_DBK_FILT_CNTL, ipp_param->blk_filt_ctrl);
	dcu_write_reg(base, DCU_MPP_DCT_BLK_CNTL, ipp_param->dct_blk_ctrl);

	dcu_write_reg(base, DCU_MPP_EE_MODE_Y, ee_param->ee_mode_y);
	dcu_write_reg(base, DCU_MPP_PEAK_COEF_Y, ee_param->coef_th_y);
	dcu_write_reg(base, DCU_MPP_PEAK_GAIN_SET1, ee_param->peak_gains_14);
	dcu_write_reg(base, DCU_MPP_PEAK_GAIN_SET2, ee_param->peak_gains_58);
	dcu_write_reg(base, DCU_MPP_EE_MODE_C, ee_param->ee_mode_c);
	dcu_write_reg(base, DCU_MPP_PEAK_COEF_C, ee_param->coef_th_c);

	if (dcu_param->is_inline) {
		dcu_write_reg(base, DCU_SOFT_RESET,
				INLINE_FIFO01_RESET | INLINE_FIFO2_RESET);
		dcu_write_reg(base, DCU_SOFT_RESET, 0x0);
		dcu_write_reg(base, DCU_INLINE_ADDR, INLINE_NOCFIFO_ADDR);
		dcu_write_reg(nocfifo_base, NOCFIFO_MODE, NOCFIFO_DISABLE);
		dcu_write_reg(nocfifo_base, NOCFIFO_MODE, NOCFIFO_EN);
	} else {
		dcu_write_reg(base, DCU_LINEAR_CFG, dcu_param->linear_config);
		dcu_write_reg(base, DCU_LINEAR_PITCH_Y,
			dcu_param->out_buf.ystride);
		dcu_write_reg(base, DCU_LINEAR_START_ADDR_Y,
			dcu_param->out_buf.yaddr_phy);
	}
}

static bool __dcu_setup_src(struct dcu_param_set *dcu_param,
			enum vdss_pixelformat fmt)
{
	u32	vpp_ctrl = 0x0;
	u32	input_format = 0x0;

	vpp_ctrl |= (OUT_LITTLE_ENDIAN |
			NOCFIFO_DIRECT_AXIW);
	input_format |= FBUF_MASK;

	switch (fmt) {
	case VDSS_PIXELFORMAT_YV12:
	case VDSS_PIXELFORMAT_I420:
		input_format |= INPUT_FORMAT_YUV420;
		vpp_ctrl |= YUV420_FORMAT;
		break;
	case VDSS_PIXELFORMAT_NV12:
		input_format |= INPUT_FORMAT_YUV420;
		vpp_ctrl |= (YUV420_FORMAT |
				UV_INTERLEAVE_EN |
				UVUV_MODE);
		break;
	case VDSS_PIXELFORMAT_NV21:
		input_format |= INPUT_FORMAT_YUV420;
		vpp_ctrl |= (YUV420_FORMAT |
				UV_INTERLEAVE_EN |
				VUVU_MODE);
		break;
	case VDSS_PIXELFORMAT_UYVY:
		input_format |= INPUT_FORMAT_YUV422;
		vpp_ctrl |= (YUV422_FORMAT |
				YUV422_FORMAT_YVYU);
		break;
	case VDSS_PIXELFORMAT_UYNV:
	case VDSS_PIXELFORMAT_YUY2:
	case VDSS_PIXELFORMAT_YUYV:
	case VDSS_PIXELFORMAT_YUNV:
		input_format |= INPUT_FORMAT_YUV422;
		vpp_ctrl |= (YUV422_FORMAT |
				YUV422_FORMAT_VYUY);
		break;
	case VDSS_PIXELFORMAT_YVYU:
		input_format |= INPUT_FORMAT_YUV422;
		vpp_ctrl |= (YUV422_FORMAT |
				YUV422_FORMAT_UYVY);
		break;
	case VDSS_PIXELFORMAT_VYUY:
		input_format |= INPUT_FORMAT_YUV422;
		vpp_ctrl |= (YUV422_FORMAT |
				YUV422_FORMAT_YUYV);
		break;
	default:
		dcu_err("%s(%d): unkonwn src format 0x%x\n",
			__func__, __LINE__, fmt);
		return false;
	}

	dcu_param->input_format = input_format;
	dcu_param->vpp_ctrl = vpp_ctrl;

	return true;
}

static bool __dcu_setup_dst(struct dcu_param_set *dcu_param,
			struct vdss_surface *surf)
{
	u32	pixel_w;
	bool	bret = true;
	struct dcu_vs_params *vs_param = &dcu_param->vs_params;
	struct dcu_field_buf *out_buf = &dcu_param->out_buf;

	switch (surf->fmt) {
	case VDSS_PIXELFORMAT_UYVY:
	case VDSS_PIXELFORMAT_UYNV:
	case VDSS_PIXELFORMAT_YUY2:
	case VDSS_PIXELFORMAT_YUYV:
	case VDSS_PIXELFORMAT_YUNV:
	case VDSS_PIXELFORMAT_YVYU:
	case VDSS_PIXELFORMAT_VYUY:
		pixel_w = vs_param->output_width;
		out_buf->yaddr_phy = surf->base;
		out_buf->uaddr_phy = 0;
		out_buf->vaddr_phy = 0;
		out_buf->ystride = (surf->width << 1);
		out_buf->ustride = 0;
		out_buf->vstride = 0;
		break;
	default:
		dcu_err("%s(%d): unkonwn src format 0x%x\n",
			__func__, __LINE__, surf->fmt);
		bret = false;
		break;
	}

	return bret;
}

static int __dcu_alloc_mot_buf(struct dcu_param_set *dcu_param)
{
	u32 size;
	u32 stride;

	if (NULL == dcu_param) {
		dcu_err("%s: invalid input parameter!\n", __func__);
		return dcu_param_invalid;
	}

	stride = (DCU_MOT_WIDTH_MAX * 6 / 8 + 4) / 8 * 8;
	size = stride * DCU_MOT_HEIGHT_MAX;
	size = (PAGE_ALIGN(size) << 1);

	if (NULL == dcu_param->mot_buf.mot_base) {
		dcu_param->mot_buf.mot_base =
			(void __iomem __force *)dma_alloc_coherent(
				dcu_param->dev, size,
				&dcu_param->mot_buf.mot_mem_addr, GFP_KERNEL);

		if (NULL == dcu_param->mot_buf.mot_base) {
			dcu_err("%s: failed to allocate motion buffer\n",
				__func__);
			return -ENOMEM;
		}
		dcu_param->mot_buf.mot_size = size;
	}

	return dcu_success;
}

static int __dcu_free_mot_buf(struct dcu_param_set *dcu_param)
{
	if (NULL == dcu_param) {
		dcu_err("%s: invalid input parameter!\n", __func__);
		return dcu_param_invalid;
	}

	if (dcu_param->mot_buf.mot_base) {
		dma_free_coherent(dcu_param->dev,
			dcu_param->mot_buf.mot_size,
			(void __force *)dcu_param->mot_buf.mot_base,
			dcu_param->mot_buf.mot_mem_addr);
		dcu_param->mot_buf.mot_base = 0;
		dcu_param->mot_buf.mot_mem_addr = 0;
		dcu_param->mot_buf.mot_size = 0;
	}

	return dcu_success;
}

static int __dcu_motbuf_init(struct dcu_param_set *dcu_param,
	struct vdss_rect *src_rect)
{
	u32 size;
	u32 width;
	u32 height;
	u32 stride;

	if (dcu_param->is_motbuf_init) {
		dcu_err("%s: motion buffer is not initialized!\n", __func__);
		return 0;
	}

	width = src_rect->right - src_rect->left + 1;
	height = src_rect->bottom - src_rect->top + 1;
	stride = (width * 6 / 8 + 4) / 8 * 8;
	size = stride * height;
	size = (PAGE_ALIGN(size) << 1);

	dcu_param->mot_buf.mot_buf[0].yaddr_phy =
		dcu_param->mot_buf.mot_mem_addr;
	dcu_param->mot_buf.mot_buf[0].uaddr_phy = 0;
	dcu_param->mot_buf.mot_buf[0].vaddr_phy = 0;
	dcu_param->mot_buf.mot_buf[0].ystride = stride;
	dcu_param->mot_buf.mot_buf[0].ustride = 0;
	dcu_param->mot_buf.mot_buf[0].vstride = 0;

	dcu_param->mot_buf.mot_buf[1].yaddr_phy =
		dcu_param->mot_buf.mot_mem_addr + (size >> 1);
	dcu_param->mot_buf.mot_buf[1].uaddr_phy = 0;
	dcu_param->mot_buf.mot_buf[1].vaddr_phy = 0;
	dcu_param->mot_buf.mot_buf[1].ystride = stride;
	dcu_param->mot_buf.mot_buf[1].ustride = 0;
	dcu_param->mot_buf.mot_buf[1].vstride = 0;

	dcu_param->mot_buf.buf_read = &dcu_param->mot_buf.mot_buf[0];
	dcu_param->mot_buf.buf_write = &dcu_param->mot_buf.mot_buf[1];

	dcu_param->is_motbuf_init = true;

	return 0;
}

static void __dcu_motbuf_destroy(struct dcu_param_set *dcu_param)
{
	dcu_param->mot_buf.mot_buf[0].yaddr_phy = 0;
	dcu_param->mot_buf.mot_buf[0].uaddr_phy = 0;
	dcu_param->mot_buf.mot_buf[0].vaddr_phy = 0;
	dcu_param->mot_buf.mot_buf[0].ystride = 0;
	dcu_param->mot_buf.mot_buf[0].ustride = 0;
	dcu_param->mot_buf.mot_buf[0].vstride = 0;

	dcu_param->mot_buf.mot_buf[1].yaddr_phy = 0;
	dcu_param->mot_buf.mot_buf[1].uaddr_phy = 0;
	dcu_param->mot_buf.mot_buf[1].vaddr_phy = 0;
	dcu_param->mot_buf.mot_buf[1].ystride = 0;
	dcu_param->mot_buf.mot_buf[1].ustride = 0;
	dcu_param->mot_buf.mot_buf[1].vstride = 0;

	dcu_param->mot_buf.buf_read = NULL;
	dcu_param->mot_buf.buf_write = NULL;

	dcu_param->is_motbuf_init = false;
}

static void __dcu_motbuf_swap(struct dcu_param_set *dcu_param)
{
	struct dcu_field_buf *temp_buf;

	if (!dcu_param->dcu_start) {
		dcu_err("%s: invalid field updating...\n", __func__);
		return;
	}

	if (dcu_param->is_motbuf_init) {
		temp_buf = dcu_param->mot_buf.buf_read;
		dcu_param->mot_buf.buf_read = dcu_param->mot_buf.buf_write;
		dcu_param->mot_buf.buf_write = temp_buf;
	}
}

static void __dcu_motbuf_clean(struct dcu_param_set *dcu_param)
{
	if (dcu_param->is_first)
		memset(dcu_param->mot_buf.mot_base,
			0, dcu_param->mot_buf.mot_size);
}

static void __dcu_update_shadow_reg(struct dcu_param_set *dcu_param)
{
	void __iomem *base = dcu_param->core_iomem;
	struct dcu_field_buf *top_buf = &dcu_param->top_buf;
	struct dcu_field_buf *bot_buf = &dcu_param->bot_buf;
	struct dcu_field_buf *fut_buf = &dcu_param->fut_buf;
	struct dcu_field_buf *mot_buf_r = dcu_param->mot_buf.buf_read;
	struct dcu_field_buf *mot_buf_w = dcu_param->mot_buf.buf_write;

	if (!dcu_param->dcu_start) {
		dcu_err("%s: invalid field updating...\n", __func__);
		return;
	}

	/* set field buffer address */
	dcu_write_reg(base, DCU_MIF2AXI_FUT_FBUF_Y_BASE, fut_buf->yaddr_phy);
	dcu_write_reg(base, DCU_MIF2AXI_FUT_FBUF_U_BASE, fut_buf->uaddr_phy);
	dcu_write_reg(base, DCU_MIF2AXI_FUT_FBUF_V_BASE, fut_buf->vaddr_phy);

	dcu_write_reg(base, DCU_MIF2AXI_TOP_FBUF_Y_BASE, top_buf->yaddr_phy);
	dcu_write_reg(base, DCU_MIF2AXI_TOP_FBUF_U_BASE, top_buf->uaddr_phy);
	dcu_write_reg(base, DCU_MIF2AXI_TOP_FBUF_V_BASE, top_buf->vaddr_phy);

	dcu_write_reg(base, DCU_MIF2AXI_BOT_FBUF_Y_BASE, bot_buf->yaddr_phy);
	dcu_write_reg(base, DCU_MIF2AXI_BOT_FBUF_U_BASE, bot_buf->uaddr_phy);
	dcu_write_reg(base, DCU_MIF2AXI_BOT_FBUF_V_BASE, bot_buf->vaddr_phy);

	/* set field buffer stride */
	dcu_write_reg(base, DCU_MIF2AXI_FUT_FBUF_Y_STRIDE,
			fut_buf->ystride / 8);
	dcu_write_reg(base, DCU_MIF2AXI_FUT_FBUF_U_STRIDE,
			fut_buf->ustride / 8);
	dcu_write_reg(base, DCU_MIF2AXI_FUT_FBUF_V_STRIDE,
			fut_buf->vstride / 8);

	dcu_write_reg(base, DCU_MIF2AXI_TOP_FBUF_Y_STRIDE,
			top_buf->ystride / 8);
	dcu_write_reg(base, DCU_MIF2AXI_TOP_FBUF_U_STRIDE,
			top_buf->ustride / 8);
	dcu_write_reg(base, DCU_MIF2AXI_TOP_FBUF_V_STRIDE,
			top_buf->vstride / 8);

	dcu_write_reg(base, DCU_MIF2AXI_BOT_FBUF_Y_STRIDE,
			bot_buf->ystride / 8);
	dcu_write_reg(base, DCU_MIF2AXI_BOT_FBUF_U_STRIDE,
			bot_buf->ustride / 8);
	dcu_write_reg(base, DCU_MIF2AXI_BOT_FBUF_V_STRIDE,
			bot_buf->vstride / 8);

	dcu_write_reg(base, DCU_MIF2AXI_MOT_FBUF_RD_BASE,
		mot_buf_r->yaddr_phy);
	dcu_write_reg(base, DCU_MIF2AXI_MOT_FBUF_WR_BASE,
		mot_buf_w->yaddr_phy);
	dcu_write_reg(base, DCU_MIF2AXI_MOT_FBUF_RD_STRIDE,
		mot_buf_r->ystride / 8);
	dcu_write_reg(base, DCU_MIF2AXI_MOT_FBUF_WR_STRIDE,
		mot_buf_w->ystride / 8);
}

static int __dcu_set_srcbase(struct dcu_param_set *dcu_param,
		struct vdss_surface *surf,
		struct vdss_rect *rect)
{
	u32 field_offset;
	u32 ystride, ustride, vstride;
	u32 yoffset, uoffset, voffset;
	struct dcu_field_buf *top_buf;
	struct dcu_field_buf *bot_buf;
	struct dcu_field_buf *fut_buf;

	if (NULL == dcu_param || NULL == surf || NULL == rect) {
		dcu_err("%s: invalid input parameter!\n", __func__);
		return dcu_param_invalid;
	}

	dcu_param->dcu_start = false;

	if (0 == surf[1].base)
		return dcu_more_data;

	switch (surf[0].field) {
	case VDSS_FIELD_SEQ_TB:
	case VDSS_FIELD_SEQ_BT:
		if (surf[0].fmt > VDSS_PIXELFORMAT_32BPPGENERIC &&
			surf[0].fmt < VDSS_PIXELFORMAT_IMC2)
			field_offset = surf[0].width * surf[0].height;
		else
			field_offset = surf[0].width *
				(surf[0].height / 2) * 3 / 2;
		break;
	case VDSS_FIELD_INTERLACED_TB:
	case VDSS_FIELD_INTERLACED_BT:
		field_offset = 0;
		break;
	default:
		field_offset = 0;
		dcu_err("%s: invalid input parameter - field!\n", __func__);
		break;
	}

	top_buf = &dcu_param->top_buf;
	bot_buf = &dcu_param->bot_buf;
	fut_buf = &dcu_param->fut_buf;

	if (0 == field_offset) {
		switch (surf[0].fmt) {
		case VDSS_PIXELFORMAT_YV12:
		case VDSS_PIXELFORMAT_I420:
			ystride = (surf[0].width << 1);
			ustride = surf[0].width;
			   vstride = surf[0].width;
			yoffset = (ystride >> 1);
			uoffset = (ustride >> 1);
			voffset = (vstride >> 1);
			break;
		case VDSS_PIXELFORMAT_NV12:
		case VDSS_PIXELFORMAT_NV21:
			ystride = (surf[0].width << 1);
			ustride = (surf[0].width << 1);
			vstride = 0;
			yoffset = (ystride >> 1);
			uoffset = (ustride >> 1);
			voffset = 0;
			break;
		case VDSS_PIXELFORMAT_UYVY:
		case VDSS_PIXELFORMAT_UYNV:
		case VDSS_PIXELFORMAT_YUY2:
		case VDSS_PIXELFORMAT_YUYV:
		case VDSS_PIXELFORMAT_YUNV:
		case VDSS_PIXELFORMAT_YVYU:
		case VDSS_PIXELFORMAT_VYUY:
			ystride = (surf[0].width << 2);
			ustride = 0;
			vstride = 0;
			yoffset = (ystride >> 1);
			uoffset = 0;
			voffset = 0;
			break;
		default:
			ystride = 0;
			ustride = 0;
			vstride = 0;
			yoffset = 0;
			uoffset = 0;
			voffset = 0;
			dcu_err("%s(%d): unknown src format 0x%x\n",
				__func__, __LINE__, surf[0].fmt);
			return dcu_param_invalid;
		}
	} else {
		switch (surf[0].fmt) {
		case VDSS_PIXELFORMAT_YV12:
		case VDSS_PIXELFORMAT_I420:
			ystride = surf[0].width;
			ustride = (surf[0].width >> 1);
			vstride = (surf[0].width >> 1);
			yoffset = field_offset;
			uoffset = field_offset;
			voffset = field_offset;
			break;
		case VDSS_PIXELFORMAT_NV12:
		case VDSS_PIXELFORMAT_NV21:
			ystride = surf[0].width;
			ustride = surf[0].width;
			vstride = 0;
			yoffset = field_offset;
			uoffset = field_offset;
			voffset = 0;
			break;
		case VDSS_PIXELFORMAT_UYVY:
		case VDSS_PIXELFORMAT_UYNV:
		case VDSS_PIXELFORMAT_YUY2:
		case VDSS_PIXELFORMAT_YUYV:
		case VDSS_PIXELFORMAT_YUNV:
		case VDSS_PIXELFORMAT_YVYU:
		case VDSS_PIXELFORMAT_VYUY:
			ystride = (surf[0].width << 1);
			ustride = 0;
			vstride = 0;
			yoffset = field_offset;
			uoffset = 0;
			voffset = 0;
			break;
		default:
			ystride = 0;
			ustride = 0;
			vstride = 0;
			yoffset = 0;
			uoffset = 0;
			voffset = 0;
			dcu_err("%s(%d): unknown src format 0x%x\n",
				__func__, __LINE__, surf[0].fmt);
			return dcu_param_invalid;
		}
	}

	top_buf->ystride = ystride;
	top_buf->ustride = ustride;
	top_buf->vstride = vstride;
	bot_buf->ystride = ystride;
	bot_buf->ustride = ustride;
	bot_buf->vstride = vstride;
	fut_buf->ystride = ystride;
	fut_buf->ustride = ustride;
	fut_buf->vstride = vstride;

	if (dcu_param->tempbuf_base != surf[0].base) {
		dcu_param->tempbuf_base = surf[0].base;
		switch (surf[0].fmt) {
		case VDSS_PIXELFORMAT_YV12:
			top_buf->yaddr_phy = surf[0].base;
			top_buf->uaddr_phy = surf[0].base +
				surf[0].width * surf[0].height * 5 / 4;
			top_buf->vaddr_phy = surf[0].base +
				surf[0].width * surf[0].height;
			bot_buf->yaddr_phy = top_buf->yaddr_phy + yoffset;
			bot_buf->uaddr_phy = top_buf->uaddr_phy + uoffset;
			bot_buf->vaddr_phy = top_buf->vaddr_phy + voffset;
			fut_buf->yaddr_phy = surf[1].base;
			fut_buf->uaddr_phy = surf[1].base +
				surf[1].width * surf[1].height * 5 / 4;
			fut_buf->vaddr_phy = surf[1].base +
				surf[1].width * surf[1].height;
			dcu_param->dcu_start = true;
			break;
		case VDSS_PIXELFORMAT_I420:
			top_buf->yaddr_phy = surf[0].base;
			top_buf->uaddr_phy = surf[0].base +
				surf[0].width * surf[0].height;
			top_buf->vaddr_phy = surf[0].base +
				surf[0].width * surf[0].height * 5 / 4;
			bot_buf->yaddr_phy = top_buf->yaddr_phy + yoffset;
			bot_buf->uaddr_phy = top_buf->uaddr_phy + uoffset;
			bot_buf->vaddr_phy = top_buf->vaddr_phy + voffset;
			fut_buf->yaddr_phy = surf[1].base;
			fut_buf->uaddr_phy = surf[1].base +
				surf[1].width * surf[1].height;
			fut_buf->vaddr_phy = surf[1].base +
				surf[1].width * surf[1].height * 5 / 4;
			dcu_param->dcu_start = true;
			break;
		case VDSS_PIXELFORMAT_NV12:
		case VDSS_PIXELFORMAT_NV21:
			top_buf->yaddr_phy = surf[0].base;
			top_buf->uaddr_phy = surf[0].base +
				surf[0].width * surf[0].height;
			top_buf->vaddr_phy = 0;
			bot_buf->yaddr_phy = top_buf->yaddr_phy + yoffset;
			bot_buf->uaddr_phy = top_buf->uaddr_phy + uoffset;
			bot_buf->vaddr_phy = 0;
			fut_buf->yaddr_phy = surf[1].base;
			fut_buf->uaddr_phy = surf[1].base +
				surf[1].width * surf[1].height;
			fut_buf->vaddr_phy = 0;
			dcu_param->dcu_start = true;
			break;
		case VDSS_PIXELFORMAT_UYVY:
		case VDSS_PIXELFORMAT_UYNV:
		case VDSS_PIXELFORMAT_YUY2:
		case VDSS_PIXELFORMAT_YUYV:
		case VDSS_PIXELFORMAT_YUNV:
		case VDSS_PIXELFORMAT_YVYU:
		case VDSS_PIXELFORMAT_VYUY:
			top_buf->yaddr_phy = surf[0].base;
			top_buf->uaddr_phy = 0;
			top_buf->vaddr_phy = 0;
			bot_buf->yaddr_phy = top_buf->yaddr_phy + yoffset;
			bot_buf->uaddr_phy = 0;
			bot_buf->vaddr_phy = 0;
			fut_buf->yaddr_phy = surf[1].base;
			fut_buf->uaddr_phy = 0;
			fut_buf->vaddr_phy = 0;
			dcu_param->dcu_start = true;
			break;
		default:
			dcu_err("%s(%d): unknown src format 0x%x\n",
				__func__, __LINE__, surf[0].fmt);
			return dcu_param_invalid;
		}
	} else {
		dcu_param->tempbuf_base = surf[0].base;
		switch (surf[0].fmt) {
		case VDSS_PIXELFORMAT_YV12:
			top_buf->yaddr_phy = surf[0].base + yoffset;
			top_buf->uaddr_phy = surf[0].base +
			surf[0].width * surf[0].height * 5 / 4 + uoffset;
			top_buf->vaddr_phy = surf[0].base +
			surf[0].width * surf[0].height + voffset;
			bot_buf->yaddr_phy = surf[1].base;
			bot_buf->uaddr_phy = surf[1].base +
				surf[1].width * surf[1].height * 5 / 4;
			bot_buf->vaddr_phy = surf[1].base +
				surf[1].width * surf[1].height;
			fut_buf->yaddr_phy = bot_buf->yaddr_phy + yoffset;
			fut_buf->uaddr_phy = bot_buf->uaddr_phy + uoffset;
			fut_buf->vaddr_phy = bot_buf->vaddr_phy + voffset;
			dcu_param->dcu_start = true;
			break;
		case VDSS_PIXELFORMAT_I420:
			top_buf->yaddr_phy = surf[0].base + yoffset;
			top_buf->uaddr_phy = surf[0].base +
			surf[0].width * surf[0].height + uoffset;
			top_buf->vaddr_phy = surf[0].base +
			surf[0].width * surf[0].height * 5 / 4 + voffset;
			bot_buf->yaddr_phy = surf[1].base;
			bot_buf->uaddr_phy = surf[1].base +
			surf[1].width * surf[1].height;
			bot_buf->vaddr_phy = surf[1].base +
			surf[1].width * surf[1].height * 5 / 4;
			fut_buf->yaddr_phy = bot_buf->yaddr_phy + yoffset;
			fut_buf->uaddr_phy = bot_buf->uaddr_phy + uoffset;
			fut_buf->vaddr_phy = bot_buf->vaddr_phy + voffset;
			dcu_param->dcu_start = true;
			break;
		case VDSS_PIXELFORMAT_NV12:
		case VDSS_PIXELFORMAT_NV21:
			top_buf->yaddr_phy = surf[0].base + yoffset;
			top_buf->uaddr_phy = surf[0].base +
				surf[0].width * surf[0].height + uoffset;
			top_buf->vaddr_phy = 0;
			bot_buf->yaddr_phy = surf[1].base;
			bot_buf->uaddr_phy = surf[1].base  +
				surf[1].width * surf[1].height;
			bot_buf->vaddr_phy = 0;
			fut_buf->yaddr_phy = bot_buf->yaddr_phy + yoffset;
			fut_buf->uaddr_phy = bot_buf->uaddr_phy + uoffset;
			fut_buf->vaddr_phy = 0;
			dcu_param->dcu_start = true;
			break;
		case VDSS_PIXELFORMAT_UYVY:
		case VDSS_PIXELFORMAT_UYNV:
		case VDSS_PIXELFORMAT_YUY2:
		case VDSS_PIXELFORMAT_YUYV:
		case VDSS_PIXELFORMAT_YUNV:
		case VDSS_PIXELFORMAT_YVYU:
		case VDSS_PIXELFORMAT_VYUY:
			top_buf->yaddr_phy = surf[0].base + yoffset;
			top_buf->uaddr_phy = 0;
			top_buf->vaddr_phy = 0;
			bot_buf->yaddr_phy = surf[1].base;
			bot_buf->uaddr_phy = 0;
			bot_buf->vaddr_phy = 0;
			fut_buf->yaddr_phy = bot_buf->yaddr_phy + yoffset;
			fut_buf->uaddr_phy = 0;
			fut_buf->vaddr_phy = 0;
			dcu_param->dcu_start = true;
			break;
		default:
			dcu_err("%s(%d): unknown src format 0x%x\n",
				__func__, __LINE__, surf[0].fmt);
			return dcu_param_invalid;
		}
	}

	return 0;
}

static bool __dcu_is_busy(void)
{
	u32     reg_val;
	bool    bret = false;
	void    __iomem *core_base = dcu.dcu_params.core_iomem;

	reg_val = dcu_read_reg(core_base, DCU_CTRL);
	if (reg_val & 0x1)
		bret = true;
	else
		bret = false;

	return bret;
}

static bool __dcu_is_ready(void)
{
	bool    bret = false;
	void    __iomem *nocfifo_base = dcu.dcu_params.nocfifo_iomem;

	if (dcu.dcu_params.is_inline) {
		if (dcu_read_reg(nocfifo_base,
			NOCFIFO_GAP_COUNT) < NOCFIFO_GAP_MAX)
			bret = false;
		else
			bret = true;
	} else
		bret = true;

	return bret;
}

static void __dcu_start(struct dcu_param_set *dcu_param)
{
	u32 time_out = 0;
	u32 reg_val = 0;
	void __iomem *base = dcu_param->core_iomem;

	if (!dcu_param->dcu_start) {
		dcu_err("%s: invalid field field updating...\n", __func__);
		return;
	}

	if (dcu_param->is_inline) {
		if (dcu_param->is_first)
			reg_val = 0x1;
		else
			reg_val = 0x0;

		reg_val |= EVEN_FIELD;

		if (dcu_param->vpp_ctrl & 0x1)
			/* YUV420 */
			reg_val |= DCU_YUV420_FORMAT;
		else
			/* YUV422 */
			reg_val |= DCU_YUV422_FORMAT;

		/* enable dcu inline mode */
		reg_val |= INLINE_EN;
		dcu_write_reg(base, DCU_CTRL, reg_val);

		/* wait nocfifo is full */
		if (dcu_param->is_first) {
			dcu_param->is_first = false;
			while (!__dcu_is_ready()) {
				if (time_out > 10000) {
					dcu_err("%s: waiting timeout\n",
						__func__);
					break;
				}
				cpu_relax();
				time_out++;
			}
		}
	} else {
		reg_val = 0x1;

		reg_val |= EVEN_FIELD;

		if (dcu_param->vpp_ctrl & 0x1)
			/* YUV420 */
			reg_val |= DCU_YUV420_FORMAT;
		else
			/* YUV422 */
			reg_val |= DCU_YUV422_FORMAT;

		dcu_write_reg(base, DCU_CTRL, reg_val);
	}
}

static void __dcu_stop(struct dcu_param_set *dcu_param)
{
	u32 time_out = 0;
	void __iomem *base = dcu_param->core_iomem;

	if (dcu_param->is_inline)
		dcu_write_reg(base, DCU_LLU_DONE_SEL,
			SEL1_DISABLE | INLINE_DONE);
	else
		while (__dcu_is_busy()) {
			if (time_out > 10000) {
				dcu_err("%s: waiting dcu stop timeout\n",
				__func__);
				break;
			}
			cpu_relax();
			time_out++;
		}
}

static int __dcu_blt(struct dcu_param_set *dcu_param,
		struct vdss_dcu_op_params *vdss_param)
{
	int ret = 0;
	u32 time_out = 0;
	struct vdss_dcu_op_params *vdss_params;

	if (NULL == dcu_param || NULL == vdss_param) {
		dcu_err("%s: invalid input parameter!\n", __func__);
		return -EINVAL;
	}

	dcu_param->is_inline = false;
	vdss_params = &dcu.vdss_params;
	*vdss_params = *vdss_param;

	if (dcu_param->is_first)
		__dcu_motbuf_init(dcu_param,
			&vdss_params->op.blt.src_rect);

	ret = __dcu_set_srcbase(dcu_param,
			&vdss_params->op.blt.src_surf[0],
			&vdss_params->op.blt.src_rect);

	if (ret) {
		dcu_err("%s: invalid input parameter!\n", __func__);
		return -EINVAL;
	}

	__dcu_motbuf_swap(dcu_param);
	__dcu_setup(dcu_param);
	__dcu_set_src_rect(dcu_param,
		&vdss_params->op.blt.src_rect,
		&vdss_params->op.blt.dst_rect);
	__dcu_setup_src(dcu_param,
		vdss_params->op.blt.src_surf[0].fmt);
	__dcu_setup_dst(dcu_param,
		&vdss_params->op.blt.dst_surf);
	__dcu_update_reg(dcu_param);

	__dcu_update_shadow_reg(dcu_param);
	__dcu_start(dcu_param);

	while (__dcu_is_busy()) {
		if (time_out > 1000)
			dcu_err("%s: waiting dcu idle timeout\n", __func__);
		cpu_relax();
		time_out++;
	}

	return 0;
}

static int __dcu_inline(struct dcu_param_set *dcu_param,
		struct vdss_dcu_op_params *vdss_param)
{
	int ret = 0;
	struct vdss_dcu_op_params *vdss_params;

	if (NULL == vdss_param || NULL == dcu_param) {
		dcu_err("%s: invalid input parameter!\n", __func__);
		return -EINVAL;
	}

	dcu_param->is_inline = true;
	vdss_params = &dcu.vdss_params;

	if (vdss_param->op.inline_mode.flip) {
		vdss_params->op.inline_mode.src_surf[0].base =
			vdss_param->op.inline_mode.src_surf[0].base;
		vdss_params->op.inline_mode.src_surf[1].base =
			vdss_param->op.inline_mode.src_surf[1].base;

		ret = __dcu_set_srcbase(dcu_param,
				&vdss_params->op.inline_mode.src_surf[0],
				&vdss_params->op.inline_mode.src_rect);

		if (ret) {
			dcu_err("%s: invalid input parameter!\n", __func__);
			return -EINVAL;
		}

		__dcu_motbuf_swap(dcu_param);
		__dcu_update_shadow_reg(dcu_param);
		__dcu_start(dcu_param);
	} else {
		*vdss_params = *vdss_param;

		ret = __dcu_set_srcbase(dcu_param,
				&vdss_params->op.inline_mode.src_surf[0],
				&vdss_params->op.inline_mode.src_rect);

		if (ret) {
			dcu_err("%s: invalid input parameter!\n", __func__);
			return -EINVAL;
		}

		__dcu_motbuf_swap(dcu_param);
		__dcu_motbuf_init(dcu_param,
			&vdss_params->op.inline_mode.src_rect);
		__dcu_setup(dcu_param);
		__dcu_set_src_rect(dcu_param,
			&vdss_params->op.inline_mode.src_rect,
			&vdss_params->op.inline_mode.dst_rect);
		__dcu_setup_src(dcu_param,
			vdss_params->op.inline_mode.src_surf[0].fmt);
		__dcu_update_reg(dcu_param);

		__dcu_update_shadow_reg(dcu_param);
		__dcu_start(dcu_param);
		__dcu_update_shadow_reg(dcu_param);
	}

	return 0;
}

static void __dcu_dump_regs(struct seq_file *s, struct dcu_param_set *dcu_param)
{
	void __iomem *core_base = dcu_param->core_iomem;
	void __iomem *nocfifo_base = dcu_param->nocfifo_iomem;

#define DCU_DUMP(fmt, ...) seq_printf(s, fmt, ##__VA_ARGS__)

	DCU_DUMP("DCU Regs:\n");
	DCU_DUMP("DCU_CTRL=0x%08x\n",
		dcu_read_reg(core_base, DCU_CTRL));
	DCU_DUMP("DCU_FBUF_X=0x%08x\n",
		dcu_read_reg(core_base, DCU_FBUF_X));
	DCU_DUMP("DCU_FUT_FBUF_RD=0x%08x\n",
		dcu_read_reg(core_base, DCU_FUT_FBUF_RD));
	DCU_DUMP("DCU_TOP_FBUF_RD=0x%08x\n",
		dcu_read_reg(core_base, DCU_TOP_FBUF_RD));
	DCU_DUMP("DCU_BOT_FBUF_RD=0x%08x\n",
		dcu_read_reg(core_base, DCU_BOT_FBUF_RD));
	DCU_DUMP("DCU_MOT_FBUF_RD=0x%08x\n",
		dcu_read_reg(core_base, DCU_MOT_FBUF_RD));
	DCU_DUMP("DCU_TOP_FBUF_WR=0x%08x\n",
		dcu_read_reg(core_base, DCU_TOP_FBUF_WR));
	DCU_DUMP("DCU_BOT_FBUF_WR=0x%08x\n",
		dcu_read_reg(core_base, DCU_BOT_FBUF_WR));
	DCU_DUMP("DCU_MOT_FBUF_WR=0x%08x\n",
		dcu_read_reg(core_base, DCU_MOT_FBUF_WR));
	DCU_DUMP("DCU_PROG_TOP=0x%08x\n",
		dcu_read_reg(core_base, DCU_PROG_TOP));
	DCU_DUMP("DCU_PROG_BOT=0x%08x\n",
		dcu_read_reg(core_base, DCU_PROG_BOT));
	DCU_DUMP("DCU_FILL_COLOR=0x%08x\n",
		dcu_read_reg(core_base, DCU_FILL_COLOR));
	DCU_DUMP("DCU_FILL_AREA0=0x%08x\n",
		dcu_read_reg(core_base, DCU_FILL_AREA0));
	DCU_DUMP("DCU_FILL_AREA1=0x%08x\n",
		dcu_read_reg(core_base, DCU_FILL_AREA1));
	DCU_DUMP("DCU_FILL_AREA2=0x%08x\n",
		dcu_read_reg(core_base, DCU_FILL_AREA2));
	DCU_DUMP("DCU_INTR_EN=0x%08x\n",
		dcu_read_reg(core_base, DCU_INTR_EN));
	DCU_DUMP("DCU_INTR_STATUS=0x%08x\n",
		dcu_read_reg(core_base, DCU_INTR_STATUS));
	DCU_DUMP("DCU_INTR_STATUS_ALIAS=0x%08x\n",
		dcu_read_reg(core_base, DCU_INTR_STATUS_ALIAS));
	DCU_DUMP("DCU_INTR=0x%08x\n",
		dcu_read_reg(core_base, DCU_INTR));
	DCU_DUMP("DCU_LBUF=0x%08x\n",
		dcu_read_reg(core_base, DCU_LBUF));
	DCU_DUMP("DCU_NUM_STRIP=0x%08x\n",
		dcu_read_reg(core_base, DCU_NUM_STRIP));
	DCU_DUMP("DCU_STRIP_SIZE1=0x%08x\n",
		dcu_read_reg(core_base, DCU_STRIP_SIZE1));
	DCU_DUMP("DCU_STRIP_SIZE2=0x%08x\n",
		dcu_read_reg(core_base, DCU_STRIP_SIZE2));
	DCU_DUMP("DCU_STRIP_OVRLP=0x%08x\n",
		dcu_read_reg(core_base, DCU_STRIP_OVRLP));
	DCU_DUMP("DCU_VSIZE=0x%08x\n",
		dcu_read_reg(core_base, DCU_VSIZE));
	DCU_DUMP("DCU_BLANK=0x%08x\n",
		dcu_read_reg(core_base, DCU_BLANK));
	DCU_DUMP("DCU_DITHER=0x%08x\n",
		dcu_read_reg(core_base, DCU_DITHER));
	DCU_DUMP("DCU_CMIF_DISABLE=0x%08x\n",
		dcu_read_reg(core_base, DCU_CMIF_DISABLE));
	DCU_DUMP("DCU_LLU_DONE_SEL=0x%08x\n",
		dcu_read_reg(core_base, DCU_LLU_DONE_SEL));
	DCU_DUMP("DCU_FUT_BOT_FBUF_RD=0x%08x\n",
		dcu_read_reg(core_base, DCU_FUT_BOT_FBUF_RD));
	DCU_DUMP("DCU_PREV_FBUF_RD=0x%08x\n",
		dcu_read_reg(core_base, DCU_PREV_FBUF_RD));
	DCU_DUMP("DCU_SOFT_RESET=0x%08x\n",
		dcu_read_reg(core_base, DCU_SOFT_RESET));
	DCU_DUMP("DCU_NRDNT_SOFT_RESET=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_SOFT_RESET));
	DCU_DUMP("DCU_NRDNT_REV_ID=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_REV_ID));
	DCU_DUMP("DCU_NRDNT_INPUT_FORMAT=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_INPUT_FORMAT));
	DCU_DUMP("DCU_NRDNT_FIELD_POLARITY=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FIELD_POLARITY));
	DCU_DUMP("DCU_NRDNT_DBG_MODE=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_DBG_MODE));
	DCU_DUMP("DCU_NRDNT_DEINT_MODE=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_DEINT_MODE));
	DCU_DUMP("DCU_NRDNT_FILM_CTRL=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FILM_CTRL));
	DCU_DUMP("DCU_NRDNT_MOTION_CTRL=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_MOTION_CTRL));
	DCU_DUMP("DCU_NRDNT_LAI_CTRL=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_LAI_CTRL));
	DCU_DUMP("DCU_NRDNT_LAI_CEIL=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_LAI_CEIL));
	DCU_DUMP("DCU_NRDNT_LAI_VAR_FLOOR=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_LAI_VAR_FLOOR));
	DCU_DUMP("DCU_NRDNT_LAI_VAR_FUNC=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_LAI_VAR_FUNC));
	DCU_DUMP("DCU_NRDNT_LAI_NOISE_FLOOR=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_LAI_NOISE_FLOOR));
	DCU_DUMP("DCU_NRDNT_FRAME_MOTION_THA=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FRAME_MOTION_THA));
	DCU_DUMP("DCU_NRDNT_FRAME_MOTION_THB=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FRAME_MOTION_THB));
	DCU_DUMP("DCU_NRDNT_FRAME_MOTION_THC=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FRAME_MOTION_THC));
	DCU_DUMP("DCU_NRDNT_LAI_V90_CTRL=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_LAI_V90_CTRL));
	DCU_DUMP("DCU_NRDNT_FIELD_MOTION_THA=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FIELD_MOTION_THA));
	DCU_DUMP("DCU_NRDNT_FIELD_MOTION_THB=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FIELD_MOTION_THB));
	DCU_DUMP("DCU_NRDNT_FIELD_MOTION_THC=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FIELD_MOTION_THC));
	DCU_DUMP("DCU_NRDNT_FIELD_MOTION_GAIN=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FIELD_MOTION_GAIN));
	DCU_DUMP("DCU_NRDNT_FIELD_MOTION_AUTO_DIS_MSB=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FIELD_MOTION_AUTO_DIS_MSB));
	DCU_DUMP("DCU_NRDNT_FIELD_MOTION_AUTO_DIS_LSB=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FIELD_MOTION_AUTO_DIS_LSB));
	DCU_DUMP("DCU_NRDNT_LAI_VIOLATE=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_LAI_VIOLATE));
	DCU_DUMP("DCU_NRDNT_DEFEATHER_FALSE_COL_CTRL=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_DEFEATHER_FALSE_COL_CTRL));
	DCU_DUMP("DCU_NRDNT_FALSE_COL_CTRL=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FALSE_COL_CTRL));
	DCU_DUMP("DCU_NRDNT_FILM_ACCU_CTRL=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FILM_ACCU_CTRL));
	DCU_DUMP("DCU_NRDNT_FILM_PAT_MATCH_CTRL=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FILM_PAT_MATCH_CTRL));
	DCU_DUMP("DCU_NRDNT_FILM_FRAME_DIFF_MSB=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FILM_FRAME_DIFF_MSB));
	DCU_DUMP("DCU_NRDNT_FILM_FRAME_DIFF_LSB=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FILM_FRAME_DIFF_LSB));
	DCU_DUMP("DCU_NRDNT_FILM_FIELD_STATIC_THA_MSB=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FILM_FIELD_STATIC_THA_MSB));
	DCU_DUMP("DCU_NRDNT_FILM_FIELD_STATIC_THA_LSB=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FILM_FIELD_STATIC_THA_LSB));
	DCU_DUMP("DCU_NRDNT_FILM_FIELD_MOTION_THA_MSB=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FILM_FIELD_MOTION_THA_MSB));
	DCU_DUMP("DCU_NRDNT_FILM_FIELD_MOTION_THA_LSB=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FILM_FIELD_MOTION_THA_LSB));
	DCU_DUMP("DCU_NRDNT_FILM_FRAME_THRESHA_MSB=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FILM_FRAME_THRESHA_MSB));
	DCU_DUMP("DCU_NRDNT_FILM_FRAME_THRESHA_LSB=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FILM_FRAME_THRESHA_LSB));
	DCU_DUMP("DCU_NRDNT_FILM_FIELD_STATIC_THB_MSB=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FILM_FIELD_STATIC_THB_MSB));
	DCU_DUMP("DCU_NRDNT_FILM_FIELD_STATIC_THB_LSB=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FILM_FIELD_STATIC_THB_LSB));
	DCU_DUMP("DCU_NRDNT_FILM_FIELD_MOTION_THB_MSB=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FILM_FIELD_MOTION_THB_MSB));
	DCU_DUMP("DCU_NRDNT_FILM_FIELD_MOTION_THB_LSB=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FILM_FIELD_MOTION_THB_LSB));
	DCU_DUMP("DCU_NDDNT_FILM_FRAME_THB_MSB=0x%08x\n",
		dcu_read_reg(core_base, DCU_NDDNT_FILM_FRAME_THB_MSB));
	DCU_DUMP("DCU_NRDNT_FILM_FRAME_THB_LSB=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FILM_FRAME_THB_LSB));
	DCU_DUMP("DCU_NRDNT_FILM_STATUS1=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FILM_STATUS1));
	DCU_DUMP("DCU_NRDNT_FILM_STATUS2=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FILM_STATUS2));
	DCU_DUMP("DCU_NRDNT_FILM_STATUS3=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FILM_STATUS3));
	DCU_DUMP("DCU_NRDNT_NEXT_FIELD_MOTION_STATUS_MSB=0x%08x\n",
		dcu_read_reg(core_base,
			DCU_NRDNT_NEXT_FIELD_MOTION_STATUS_MSB));
	DCU_DUMP("DCU_NRDNT_NEXT_FIELD_MOTION_STATUS_MID=0x%08x\n",
		dcu_read_reg(core_base,
			DCU_NRDNT_NEXT_FIELD_MOTION_STATUS_MID));
	DCU_DUMP("DCU_NRDNT_NEXT_FIELD_MOTION_STATUS_LSB=0x%08x\n",
		dcu_read_reg(core_base,
			DCU_NRDNT_NEXT_FIELD_MOTION_STATUS_LSB));
	DCU_DUMP("DCU_NRDNT_FRAME_MOTION_STATUS_MSB=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FRAME_MOTION_STATUS_MSB));
	DCU_DUMP("DCU_NRDNT_FRAME_MOTION_STATUS_MID=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FRAME_MOTION_STATUS_MID));
	DCU_DUMP("DCU_NRDNT_FRAME_MOTION_STATUS_LSB=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FRAME_MOTION_STATUS_LSB));
	DCU_DUMP("DCU_NRDNT_K_FRAME_MODE=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_K_FRAME_MODE));
	DCU_DUMP("DCU_NRDNT_HORIZONTAL_EDGE_DETECT=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_HORIZONTAL_EDGE_DETECT));
	DCU_DUMP("DCU_NRDNT_HORIZONTAL_EDGE_DETECT_ADJUST=0x%08x\n",
		dcu_read_reg(core_base,
			DCU_NRDNT_HORIZONTAL_EDGE_DETECT_ADJUST));
	DCU_DUMP("DCU_NRDNT_IMPLUSE_FILTER_MDETECT_TH=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_IMPLUSE_FILTER_MDETECT_TH));
	DCU_DUMP("DCU_NRDNT_SPATIAL_FILTER_MDETECT_TH=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_SPATIAL_FILTER_MDETECT_TH));
	DCU_DUMP("DCU_NRDNT_NOISE_FILTER_FLUSH_COUNT=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_NOISE_FILTER_FLUSH_COUNT));
	DCU_DUMP("DCU_NRDNT_NOISE_FILTER_FLUSH_ENABLE=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_NOISE_FILTER_FLUSH_ENABLE));
	DCU_DUMP("DCU_NRDNT_NOISE_FILTER_ENABLE=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_NOISE_FILTER_ENABLE));
	DCU_DUMP("DCU_NRDNT_CONSTANT_NOISE=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_CONSTANT_NOISE));
	DCU_DUMP("DCU_NRDNT_EXTERNAL_NOISE=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_EXTERNAL_NOISE));
	DCU_DUMP("DCU_NRDNT_NOISE_LEVEL_SOURCE=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_NOISE_LEVEL_SOURCE));
	DCU_DUMP("DCU_NRDNT_SPATIAL_TH_WEIGHTS=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_SPATIAL_TH_WEIGHTS));
	DCU_DUMP("DCU_NRDNT_TEMPORAL_TH_WEIGHTS=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_TEMPORAL_TH_WEIGHTS));
	DCU_DUMP("DCU_NRDNT_LUMA_SPATIAL_TH_MAX=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_LUMA_SPATIAL_TH_MAX));
	DCU_DUMP("DCU_NRDNT_CHROMA_SPATIAL_TH_MAX=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_CHROMA_SPATIAL_TH_MAX));
	DCU_DUMP("DCU_NRDNT_LUMA_TEMPORAL_TH_MAX=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_LUMA_TEMPORAL_TH_MAX));
	DCU_DUMP("DCU_NRDNT_CHROMA_TEMPORAL_TH_MAX=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_CHROMA_TEMPORAL_TH_MAX));
	DCU_DUMP("DCU_NRDNT_LUMA_SPATIAL_TH_MIN=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_LUMA_SPATIAL_TH_MIN));
	DCU_DUMP("DCU_NRDNT_CHROMA_SPATIAL_TH_MIN=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_CHROMA_SPATIAL_TH_MIN));
	DCU_DUMP("DCU_NRDNT_LUMA_TEMPORAL_TH_MIN=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_LUMA_TEMPORAL_TH_MIN));
	DCU_DUMP("DCU_NRDNT_CHROMA_TEMPORAL_TH_MIN=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_CHROMA_TEMPORAL_TH_MIN));
	DCU_DUMP("DCU_NRDNT_HORIZONTAL_EDGE_THA=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_HORIZONTAL_EDGE_THA));
	DCU_DUMP("DCU_NRDNT_HORIZONTAL_EDGE_THB=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_HORIZONTAL_EDGE_THB));
	DCU_DUMP("DCU_NRDNT_HORIZONTAL_EDGE_THC=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_HORIZONTAL_EDGE_THC));
	DCU_DUMP("DCU_NRDNT_STATUS_NOISE=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_STATUS_NOISE));
	DCU_DUMP("DCU_NRDNT_IMPLUSE_WINDOW_SEL=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_IMPLUSE_WINDOW_SEL));
	DCU_DUMP("DCU_NRDNT_AFM_FRAME_MDETECT_THA=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_AFM_FRAME_MDETECT_THA));
	DCU_DUMP("DCU_NRDNT_AFM_FRAME_MDETECT_THB=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_AFM_FRAME_MDETECT_THB));
	DCU_DUMP("DCU_NRDNT_AFM_FRAME_MDETECT_THC=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_AFM_FRAME_MDETECT_THC));
	DCU_DUMP("DCU_NRDNT_AFM_FIELD_MDETECT_THA=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_AFM_FIELD_MDETECT_THA));
	DCU_DUMP("DCU_NRDNT_AFM_FIELD_MDETECT_THB=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_AFM_FIELD_MDETECT_THB));
	DCU_DUMP("DCU_NRDNT_AFM_FIELD_MDETECT_THC=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_AFM_FIELD_MDETECT_THC));
	DCU_DUMP("DCU_NRDNT_AFM_FIELD_MDETECT_GAIN=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_AFM_FIELD_MDETECT_GAIN));
	DCU_DUMP("DCU_NRDNT_MOTION_DETECTION=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_MOTION_DETECTION));
	DCU_DUMP("DCU_NRDNT_SCENE_CHANGE_THA1=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_SCENE_CHANGE_THA1));
	DCU_DUMP("DCU_NRDNT_SCENE_CHANGE_THA2=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_SCENE_CHANGE_THA2));
	DCU_DUMP("DCU_NRDNT_SCENE_CHANGE_THA3=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_SCENE_CHANGE_THA3));
	DCU_DUMP("DCU_NRDNT_SCENE_CHANGE_THB1=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_SCENE_CHANGE_THB1));
	DCU_DUMP("DCU_NRDNT_SCENE_CHANGE_THB2=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_SCENE_CHANGE_THB2));
	DCU_DUMP("DCU_NRDNT_SCENE_CHANGE_THB3=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_SCENE_CHANGE_THB3));
	DCU_DUMP("DCU_NRDNT_SCENE_CHANGE_FLUSH_NUM=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_SCENE_CHANGE_FLUSH_NUM));
	DCU_DUMP("DCU_NRDNT_TEMPORAL_NOISE_ESTIMATION1=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_TEMPORAL_NOISE_ESTIMATION1));
	DCU_DUMP("DCU_NRDNT_TEMPORAL_NOISE_ESTIMATION2=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_TEMPORAL_NOISE_ESTIMATION2));
	DCU_DUMP("DCU_NRDNT_TEMPORAL_NOISE_ESTIMATION3=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_TEMPORAL_NOISE_ESTIMATION3));
	DCU_DUMP("DCU_NRDNT_TEMPORAL_NOISE_ESTIMATION4=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_TEMPORAL_NOISE_ESTIMATION4));
	DCU_DUMP("DCU_NRDNT_TEMPORAL_NOISE_ESTIMATION5=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_TEMPORAL_NOISE_ESTIMATION5));
	DCU_DUMP("DCU_NRDNT_TEMPORAL_NOISE_ESTIMATION6=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_TEMPORAL_NOISE_ESTIMATION6));
	DCU_DUMP("DCU_NRDNT_TEMPORAL_NOISE_ESTIMATION7=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_TEMPORAL_NOISE_ESTIMATION7));
	DCU_DUMP("DCU_NRDNT_SPATIAL_NOISE_ESTIMATION1=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_SPATIAL_NOISE_ESTIMATION1));
	DCU_DUMP("DCU_NRDNT_SPATIAL_NOISE_ESTIMATION2=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_SPATIAL_NOISE_ESTIMATION2));
	DCU_DUMP("DCU_NRDNT_SPATIAL_NOISE_ESTIMATION3=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_SPATIAL_NOISE_ESTIMATION3));
	DCU_DUMP("DCU_NRDNT_SPATIAL_NOISE_ESTIMATION4=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_SPATIAL_NOISE_ESTIMATION4));
	DCU_DUMP("DCU_NRDNT_SPATIAL_NOISE_ESTIMATION5=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_SPATIAL_NOISE_ESTIMATION5));
	DCU_DUMP("DCU_NRDNT_SPATIAL_NOISE_ESTIMATION6=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_SPATIAL_NOISE_ESTIMATION6));
	DCU_DUMP("DCU_NRDNT_SPATIAL_NOISE_ESTIMATION7=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_SPATIAL_NOISE_ESTIMATION7));
	DCU_DUMP("DCU_NRDNT_FRAME_MDETC=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FRAME_MDETC));
	DCU_DUMP("DCU_NRDNT_FRAME_MDETC_MAD_BLEND_TH=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FRAME_MDETC_MAD_BLEND_TH));
	DCU_DUMP("DCU_NRDNT_FRAME_MDETC_MAD_BLEND_RES=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FRAME_MDETC_MAD_BLEND_RES));
	DCU_DUMP("DCU_NRDNT_FRAME_MDETC_MAD_GAIN=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FRAME_MDETC_MAD_GAIN));
	DCU_DUMP("DCU_NRDNT_FRAME_MDETC_MAD_GAIN_CORE=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FRAME_MDETC_MAD_GAIN_CORE));
	DCU_DUMP("DCU_NRDNT_FRAME_MDETC_MAD_GAIN_CORE2=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FRAME_MDETC_MAD_GAIN_CORE2));
	DCU_DUMP("DCU_NRDNT_FRAME_MDETC_MAD_GAIN_CORE_TH=0x%08x\n",
		dcu_read_reg(core_base,
			DCU_NRDNT_FRAME_MDETC_MAD_GAIN_CORE_TH));
	DCU_DUMP("DCU_NRDNT_FRAME_MDETC_MAD_GAIN_CORE2_TH=0x%08x\n",
		dcu_read_reg(core_base,
			DCU_NRDNT_FRAME_MDETC_MAD_GAIN_CORE2_TH));
	DCU_DUMP("DCU_NRDNT_FRAME_MDETC_FIELD_GAIN=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FRAME_MDETC_FIELD_GAIN));
	DCU_DUMP("DCU_NRDNT_FRAME_MDETC_FIELD_CORE_GAIN=0x%08x\n",
		dcu_read_reg(core_base,
			DCU_NRDNT_FRAME_MDETC_FIELD_CORE_GAIN));
	DCU_DUMP("DCU_NRDNT_FRAME_MDETC_FIELD_CORE_TH=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FRAME_MDETC_FIELD_CORE_TH));
	DCU_DUMP("DCU_NRDNT_FRAME_MDETC_RECURSIVE=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FRAME_MDETC_RECURSIVE));
	DCU_DUMP("DCU_NRDNT_FRAME_MDETC_RECURSIVE_GAIN=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRDNT_FRAME_MDETC_RECURSIVE_GAIN));
	DCU_DUMP("DCU_BAD_EDIT_FRAME_THA=0x%08x\n",
		dcu_read_reg(core_base, DCU_BAD_EDIT_FRAME_THA));
	DCU_DUMP("DCU_BAD_EDIT_FRAME_THB=0x%08x\n",
		dcu_read_reg(core_base, DCU_BAD_EDIT_FRAME_THB));
	DCU_DUMP("DCU_BAD_EDIT_FRAME_THC=0x%08x\n",
		dcu_read_reg(core_base, DCU_BAD_EDIT_FRAME_THC));
	DCU_DUMP("DCU_BAD_EDIT_FIELD_THA=0x%08x\n",
		dcu_read_reg(core_base, DCU_BAD_EDIT_FIELD_THA));
	DCU_DUMP("DCU_BAD_EDIT_FIELD_THB=0x%08x\n",
		dcu_read_reg(core_base, DCU_BAD_EDIT_FIELD_THB));
	DCU_DUMP("DCU_BAD_EDIT_FIELD_THC=0x%08x\n",
		dcu_read_reg(core_base, DCU_BAD_EDIT_FIELD_THC));
	DCU_DUMP("DCU_BAD_EDIT_DETC_ENABLE=0x%08x\n",
		dcu_read_reg(core_base, DCU_BAD_EDIT_DETC_ENABLE));
	DCU_DUMP("DCU_BAD_EDIT_DETC_AFTER_MODE=0x%08x\n",
		dcu_read_reg(core_base, DCU_BAD_EDIT_DETC_AFTER_MODE));
	DCU_DUMP("DCU_BAD_EDIT_DETC_STATUS=0x%08x\n",
		dcu_read_reg(core_base, DCU_BAD_EDIT_DETC_STATUS));
	DCU_DUMP("DCU_DISTRESS_THA_ADDR=0x%08x\n",
		dcu_read_reg(core_base, DCU_DISTRESS_THA_ADDR));
	DCU_DUMP("DCU_DISTRESS_THB_ADDR=0x%08x\n",
		dcu_read_reg(core_base, DCU_DISTRESS_THB_ADDR));
	DCU_DUMP("DCU_CYCLES_PER_LINE_ADDRA=0x%08x\n",
		dcu_read_reg(core_base, DCU_CYCLES_PER_LINE_ADDRA));
	DCU_DUMP("DCU_CYCLES_PER_LINE_ADDRB=0x%08x\n",
		dcu_read_reg(core_base, DCU_CYCLES_PER_LINE_ADDRB));
	DCU_DUMP("DCU_DISTRESS_SEL=0x%08x\n",
		dcu_read_reg(core_base, DCU_DISTRESS_SEL));
	DCU_DUMP("DCU_NRDNT_PREV_FIELD_MOTION_STATUS_MSB=0x%08x\n",
		dcu_read_reg(core_base,
			DCU_NRDNT_PREV_FIELD_MOTION_STATUS_MSB));
	DCU_DUMP("DCU_NRDNT_PREV_FIELD_MOTION_STATUS_MID=0x%08x\n",
		dcu_read_reg(core_base,
			DCU_NRDNT_PREV_FIELD_MOTION_STATUS_MID));
	DCU_DUMP("DCU_NRDNT_PREV_FIELD_MOTION_STATUS_LSB=0x%08x\n",
		dcu_read_reg(core_base,
			DCU_NRDNT_PREV_FIELD_MOTION_STATUS_LSB));
	DCU_DUMP("DCU_MPP_SOFT_RESET=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_SOFT_RESET));
	DCU_DUMP("DCU_MPP_REVISION_ID=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_REVISION_ID));
	DCU_DUMP("DCU_MPP_DCT_BLK_CNTL=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_DCT_BLK_CNTL));
	DCU_DUMP("DCU_MPP_DCT_CWIDTH16=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_DCT_CWIDTH16));
	DCU_DUMP("DCU_MPP_VBLKDET_CNTL0=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_VBLKDET_CNTL0));
	DCU_DUMP("DCU_MPP_VBLKDET_CNTL1=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_VBLKDET_CNTL1));
	DCU_DUMP("DCU_MPP_VBLKDET_CNTL2=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_VBLKDET_CNTL2));
	DCU_DUMP("DCU_MPP_HBLKDET_CNTL0=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_HBLKDET_CNTL0));
	DCU_DUMP("DCU_MPP_HBLKDET_CNTL1=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_HBLKDET_CNTL1));
	DCU_DUMP("DCU_MPP_HBLKDET_CNTL2=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_HBLKDET_CNTL2));
	DCU_DUMP("DCU_MPP_VDBK_CNTL=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_VDBK_CNTL));
	DCU_DUMP("DCU_MPP_VDBK_C_CNTL=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_VDBK_C_CNTL));
	DCU_DUMP("DCU_MPP_VDBK_C_CNTL2=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_VDBK_C_CNTL2));
	DCU_DUMP("DCU_MPP_HDBK_CNTL=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_HDBK_CNTL));
	DCU_DUMP("DCU_MPP_HDBK_C_CNTL=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_HDBK_C_CNTL));
	DCU_DUMP("DCU_MPP_HDBK_C_CNTL2=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_HDBK_C_CNTL2));
	DCU_DUMP("DCU_MPP_DRG_EDGE_TH=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_DRG_EDGE_TH));
	DCU_DUMP("DCU_MPP_DRG_TH=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_DRG_TH));
	DCU_DUMP("DCU_MPP_DRG_FILT_CNTL=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_DRG_FILT_CNTL));
	DCU_DUMP("DCU_MPP_DRG_FILT_SEL=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_DRG_FILT_SEL));
	DCU_DUMP("DCU_MPP_DBK_FILT_CNTL=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_DBK_FILT_CNTL));
	DCU_DUMP("DCU_MPP_DBG_MODE=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_DBG_MODE));
	DCU_DUMP("DCU_MPP_STATUS_READY_TOGGLE=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_STATUS_READY_TOGGLE));
	DCU_DUMP("DCU_MPP_STATUS_VBLK_SUM0=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_STATUS_VBLK_SUM0));
	DCU_DUMP("DCU_MPP_STATUS_VBLK_SUM1=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_STATUS_VBLK_SUM1));
	DCU_DUMP("DCU_MPP_STATUS_VBLK_SUM2=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_STATUS_VBLK_SUM2));
	DCU_DUMP("DCU_MPP_STATUS_VBLK_SUM3=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_STATUS_VBLK_SUM3));
	DCU_DUMP("DCU_MPP_STATUS_HBLK_SUM0=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_STATUS_HBLK_SUM0));
	DCU_DUMP("DCU_MPP_STATUS_HBLK_SUM1=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_STATUS_HBLK_SUM1));
	DCU_DUMP("DCU_MPP_STATUS_HBLK_SUM2=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_STATUS_HBLK_SUM2));
	DCU_DUMP("DCU_MPP_STATUS_HBLK_SUM3=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_STATUS_HBLK_SUM3));
	DCU_DUMP("DCU_MPP_EE_MODE_Y=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_EE_MODE_Y));
	DCU_DUMP("DCU_MPP_PEAK_COEF_Y=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_PEAK_COEF_Y));
	DCU_DUMP("DCU_MPP_PEAK_GAIN_SET1=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_PEAK_GAIN_SET1));
	DCU_DUMP("DCU_MPP_PEAK_GAIN_SET2=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_PEAK_GAIN_SET2));
	DCU_DUMP("DCU_MPP_EE_MODE_C=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_EE_MODE_C));
	DCU_DUMP("DCU_MPP_PEAK_COEF_C=0x%08x\n",
		dcu_read_reg(core_base, DCU_MPP_PEAK_COEF_C));
	DCU_DUMP("DCU_VS_CFG=0x%08x\n",
		dcu_read_reg(core_base, DCU_VS_CFG));
	DCU_DUMP("DCU_VS_WRITEBACK_CFG=0x%08x\n",
		dcu_read_reg(core_base, DCU_VS_WRITEBACK_CFG));
	DCU_DUMP("DCU_VS_LUM_WRITEBACK_SIZE=0x%08x\n",
		dcu_read_reg(core_base, DCU_VS_LUM_WRITEBACK_SIZE));
	DCU_DUMP("DCU_VS_LUM_RD_SIZE=0x%08x\n",
		dcu_read_reg(core_base, DCU_VS_LUM_RD_SIZE));
	DCU_DUMP("DCU_VS_LUM_V_INT_CTRL=0x%08x\n",
		dcu_read_reg(core_base, DCU_VS_LUM_V_INT_CTRL));
	DCU_DUMP("DCU_VS_LUM_V_INT_START=0x%08x\n",
		dcu_read_reg(core_base, DCU_VS_LUM_V_INT_START));
	DCU_DUMP("DCU_VS_LUM_V_SCALE_PAR=0x%08x\n",
		dcu_read_reg(core_base, DCU_VS_LUM_V_SCALE_PAR));
	DCU_DUMP("DCU_VS_LUM_V_DDA_PAR=0x%08x\n",
		dcu_read_reg(core_base, DCU_VS_LUM_V_DDA_PAR));
	DCU_DUMP("DCU_VS_LUM_V_DDA_START=0x%08x\n",
		dcu_read_reg(core_base, DCU_VS_LUM_V_DDA_START));
	DCU_DUMP("DCU_CONTEXT_CONFIG=0x%08x\n",
		dcu_read_reg(core_base, DCU_CONTEXT_CONFIG));
	DCU_DUMP("DCU_NRD_COUNTER_STATUS=0x%08x\n",
		dcu_read_reg(core_base, DCU_NRD_COUNTER_STATUS));
	DCU_DUMP("DCU_INLINE_CROP_H=0x%08x\n",
		dcu_read_reg(core_base, DCU_INLINE_CROP_H));
	DCU_DUMP("DCU_INLINE_CROP_V=0x%08x\n",
		dcu_read_reg(core_base, DCU_INLINE_CROP_V));
	DCU_DUMP("DCU_INLINE_HSIZE_PATCH=0x%08x\n",
		dcu_read_reg(core_base, DCU_INLINE_HSIZE_PATCH));
	DCU_DUMP("DCU_INLINE_FIFO_FULL_TH=0x%08x\n",
		dcu_read_reg(core_base, DCU_INLINE_FIFO_FULL_TH));
	DCU_DUMP("DCU_INLINE_FIFO_VALID_TH=0x%08x\n",
		dcu_read_reg(core_base, DCU_INLINE_FIFO_VALID_TH));
	DCU_DUMP("DCU_INLINE_FIFO_INVALID_TH=0x%08x\n",
		dcu_read_reg(core_base, DCU_INLINE_FIFO_INVALID_TH));
	DCU_DUMP("DCU_INLINE_FIFO1_STATUS=0x%08x\n",
		dcu_read_reg(core_base, DCU_INLINE_FIFO1_STATUS));
	DCU_DUMP("DCU_INLINE_FIFO2_STATUS=0x%08x\n",
		dcu_read_reg(core_base, DCU_INLINE_FIFO2_STATUS));
	DCU_DUMP("DCU_INLINE_FIFO2_STATISTICS=0x%08x\n",
		dcu_read_reg(core_base, DCU_INLINE_FIFO2_STATISTICS));
	DCU_DUMP("DCU_INLINE_FIFO2_STATISTICS_CTRL=0x%08x\n",
		dcu_read_reg(core_base, DCU_INLINE_FIFO2_STATISTICS_CTRL));
	DCU_DUMP("DCU_INLINE_FLD=0x%08x\n",
		dcu_read_reg(core_base, DCU_INLINE_FLD));
	DCU_DUMP("DCU_LINEAR_CFG=0x%08x\n",
		dcu_read_reg(core_base, DCU_LINEAR_CFG));
	DCU_DUMP("DCU_LINEAR_START_ADDR_Y=0x%08x\n",
		dcu_read_reg(core_base, DCU_LINEAR_START_ADDR_Y));
	DCU_DUMP("DCU_LINEAR_START_ADDR_C=0x%08x\n",
		dcu_read_reg(core_base, DCU_LINEAR_START_ADDR_C));
	DCU_DUMP("DCU_LINEAR_PITCH_Y=0x%08x\n",
		dcu_read_reg(core_base, DCU_LINEAR_PITCH_Y));
	DCU_DUMP("DCU_LINEAR_PITCH_C=0x%08x\n",
		dcu_read_reg(core_base, DCU_LINEAR_PITCH_C));
	DCU_DUMP("DCU_MIF2AXI_VPP_CTRL=0x%08x\n",
		dcu_read_reg(core_base, DCU_MIF2AXI_VPP_CTRL));
	DCU_DUMP("DCU_MIF2AXI_FUT_FBUF_Y_BASE=0x%08x\n",
		dcu_read_reg(core_base, DCU_MIF2AXI_FUT_FBUF_Y_BASE));
	DCU_DUMP("DCU_MIF2AXI_FUT_FBUF_U_BASE=0x%08x\n",
		dcu_read_reg(core_base, DCU_MIF2AXI_FUT_FBUF_U_BASE));
	DCU_DUMP("DCU_MIF2AXI_FUT_FBUF_V_BASE=0x%08x\n",
		dcu_read_reg(core_base, DCU_MIF2AXI_FUT_FBUF_V_BASE));
	DCU_DUMP("DCU_MIF2AXI_FUT_FBUF_Y_STRIDE=0x%08x\n",
		dcu_read_reg(core_base, DCU_MIF2AXI_FUT_FBUF_Y_STRIDE));
	DCU_DUMP("DCU_MIF2AXI_FUT_FBUF_U_STRIDE=0x%08x\n",
		dcu_read_reg(core_base, DCU_MIF2AXI_FUT_FBUF_U_STRIDE));
	DCU_DUMP("DCU_MIF2AXI_FUT_FBUF_V_STRIDE=0x%08x\n",
		dcu_read_reg(core_base, DCU_MIF2AXI_FUT_FBUF_V_STRIDE));
	DCU_DUMP("DCU_MIF2AXI_TOP_FBUF_Y_BASE=0x%08x\n",
		dcu_read_reg(core_base, DCU_MIF2AXI_TOP_FBUF_Y_BASE));
	DCU_DUMP("DCU_MIF2AXI_TOP_FBUF_U_BASE=0x%08x\n",
		dcu_read_reg(core_base, DCU_MIF2AXI_TOP_FBUF_U_BASE));
	DCU_DUMP("DCU_MIF2AXI_TOP_FBUF_V_BASE=0x%08x\n",
		dcu_read_reg(core_base, DCU_MIF2AXI_TOP_FBUF_V_BASE));
	DCU_DUMP("DCU_MIF2AXI_TOP_FBUF_Y_STRIDE=0x%08x\n",
		dcu_read_reg(core_base, DCU_MIF2AXI_TOP_FBUF_Y_STRIDE));
	DCU_DUMP("DCU_MIF2AXI_TOP_FBUF_U_STRIDE=0x%08x\n",
		dcu_read_reg(core_base, DCU_MIF2AXI_TOP_FBUF_U_STRIDE));
	DCU_DUMP("DCU_MIF2AXI_TOP_FBUF_V_STRIDE=0x%08x\n",
		dcu_read_reg(core_base, DCU_MIF2AXI_TOP_FBUF_V_STRIDE));
	DCU_DUMP("DCU_MIF2AXI_BOT_FBUF_Y_BASE=0x%08x\n",
		dcu_read_reg(core_base, DCU_MIF2AXI_BOT_FBUF_Y_BASE));
	DCU_DUMP("DCU_MIF2AXI_BOT_FBUF_U_BASE=0x%08x\n",
		dcu_read_reg(core_base, DCU_MIF2AXI_BOT_FBUF_U_BASE));
	DCU_DUMP("DCU_MIF2AXI_BOT_FBUF_V_BASE=0x%08x\n",
		dcu_read_reg(core_base, DCU_MIF2AXI_BOT_FBUF_V_BASE));
	DCU_DUMP("DCU_MIF2AXI_BOT_FBUF_Y_STRIDE=0x%08x\n",
		dcu_read_reg(core_base, DCU_MIF2AXI_BOT_FBUF_Y_STRIDE));
	DCU_DUMP("DCU_MIF2AXI_BOT_FBUF_U_STRIDE=0x%08x\n",
		dcu_read_reg(core_base, DCU_MIF2AXI_BOT_FBUF_U_STRIDE));
	DCU_DUMP("DCU_MIF2AXI_BOT_FBUF_V_STRIDE=0x%08x\n",
		dcu_read_reg(core_base, DCU_MIF2AXI_BOT_FBUF_V_STRIDE));
	DCU_DUMP("DCU_MIF2AXI_MOT_FBUF_RD_BASE=0x%08x\n",
		dcu_read_reg(core_base, DCU_MIF2AXI_MOT_FBUF_RD_BASE));
	DCU_DUMP("DCU_MIF2AXI_MOT_FBUF_RD_STRIDE=0x%08x\n",
		dcu_read_reg(core_base, DCU_MIF2AXI_MOT_FBUF_RD_STRIDE));
	DCU_DUMP("DCU_MIF2AXI_MOT_FBUF_WR_BASE=0x%08x\n",
		dcu_read_reg(core_base, DCU_MIF2AXI_MOT_FBUF_WR_BASE));
	DCU_DUMP("DCU_MIF2AXI_MOT_FBUF_WR_STRIDE=0x%08x\n",
		dcu_read_reg(core_base, DCU_MIF2AXI_MOT_FBUF_WR_STRIDE));
	DCU_DUMP("DCU_INLINE_ADDR=0x%08x\n",
		dcu_read_reg(core_base, DCU_INLINE_ADDR));
	DCU_DUMP("NOCFIFO_MODE=0x%08x\n",
		dcu_read_reg(nocfifo_base, NOCFIFO_MODE));
	DCU_DUMP("NOCFIFO_INT_ENA=0x%08x\n",
		dcu_read_reg(nocfifo_base, NOCFIFO_INT_ENA));
	DCU_DUMP("NOCFIFO_INT_STS=0x%08x\n",
		dcu_read_reg(nocfifo_base, NOCFIFO_INT_STS));
	DCU_DUMP("NOCFIFO_MAX_FULLNESS=0x%08x\n",
		dcu_read_reg(nocfifo_base, NOCFIFO_MAX_FULLNESS));
	DCU_DUMP("NOCFIFO_READ_OFFSET=0x%08x\n",
		dcu_read_reg(nocfifo_base, NOCFIFO_READ_OFFSET));
	DCU_DUMP("NOCFIFO_WRITE_OFFSET=0x%08x\n",
		dcu_read_reg(nocfifo_base, NOCFIFO_WRITE_OFFSET));
	DCU_DUMP("NOCFIFO_PROBE_ADDR=0x%08x\n",
		dcu_read_reg(nocfifo_base, NOCFIFO_PROBE_ADDR));
	DCU_DUMP("NOCFIFO_PROBE_DATA_MSW=0x%08x\n",
		dcu_read_reg(nocfifo_base, NOCFIFO_PROBE_DATA_MSW));
	DCU_DUMP("NOCFIFO_PROBE_DATA_LSW=0x%08x\n",
		dcu_read_reg(nocfifo_base, NOCFIFO_PROBE_DATA_LSW));
	DCU_DUMP("NOCFIFO_GAP_COUNT=0x%08x\n",
		dcu_read_reg(nocfifo_base, NOCFIFO_GAP_COUNT));
}

static void dcu_dump_regs(struct seq_file *s)
{
	__dcu_dump_regs(s, &dcu.dcu_params);
}

void dcu_enable(void)
{
	void __iomem *base;
	struct dcu_param_set *dcu_param;

	dcu_param = &dcu.dcu_params;
	base = dcu_param->core_iomem;

	dcu_write_reg(base, DCU_LLU_DONE_SEL,
		DCU_START | SHADOW_EN | SEL1_DISABLE | INLINE_DONE);
}

void dcu_disable(void)
{
	struct dcu_param_set *dcu_param;

	dcu_param = &dcu.dcu_params;
	__dcu_stop(dcu_param);
}

int sirfsoc_dcu_reset(void)
{
	struct dcu_param_set *dcu_param;

	dcu_param = &dcu.dcu_params;
	dcu_param->is_first = true;
	dcu_param->is_inline = false;
	dcu_param->dcu_start = false;
	dcu_param->dcu_ctrl = 0x0;
	dcu_param->input_format = 0x0;
	dcu_param->vpp_ctrl = 0x0;

	__dcu_motbuf_destroy(dcu_param);
	__dcu_reset(dcu_param);
	__dcu_motbuf_clean(dcu_param);
	__dcu_init(dcu_param);

	return 0;
}
EXPORT_SYMBOL(sirfsoc_dcu_reset);

bool sirfsoc_dcu_is_inline_support(enum vdss_pixelformat fmt,
				enum vdss_field field)
{
	switch (fmt) {
	case VDSS_PIXELFORMAT_YUYV:
	case VDSS_PIXELFORMAT_UYVY:
	case VDSS_PIXELFORMAT_YUY2:
	case VDSS_PIXELFORMAT_YUNV:
	case VDSS_PIXELFORMAT_YVYU:
	case VDSS_PIXELFORMAT_UYNV:
	case VDSS_PIXELFORMAT_VYUY:
	case VDSS_PIXELFORMAT_YV12:
	case VDSS_PIXELFORMAT_I420:
	case VDSS_PIXELFORMAT_UYVI:
	case VDSS_PIXELFORMAT_NV12:
	case VDSS_PIXELFORMAT_NV21:
		break;
	default:
		return false;
	}

	switch (field) {
	case VDSS_FIELD_SEQ_TB:
	case VDSS_FIELD_SEQ_BT:
	case VDSS_FIELD_INTERLACED_TB:
	case VDSS_FIELD_INTERLACED_BT:
	case VDSS_FIELD_INTERLACED:
		break;
	default:
		return false;
	}

	return dcu.inline_en;
}
EXPORT_SYMBOL(sirfsoc_dcu_is_inline_support);

int sirfsoc_dcu_present(struct vdss_dcu_op_params *vdss_param)
{
	int ret = 0;
	struct dcu_param_set *dcu_param;

	if (NULL == vdss_param)
		return -EINVAL;

	dcu_param = &dcu.dcu_params;

	if (DCU_OP_INLINE == vdss_param->type)
		ret = __dcu_inline(dcu_param, vdss_param);
	else if (DCU_OP_BITBLT == vdss_param->type)
		ret = __dcu_blt(dcu_param, vdss_param);

	if (ret) {
		dcu_err("%s: wrong operation\n", __func__);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(sirfsoc_dcu_present);

#ifdef CONFIG_PM_SLEEP
static int sirfsoc_dcu_suspend(struct device *dev)
{
	struct dcu_device *dcu_dev;

	dcu_dev = dev_get_drvdata(dev);
	clk_disable_unprepare(dcu_dev->clk);
	return 0;
}

static int sirfsoc_dcu_pm_resume(struct device *dev)
{
	struct dcu_device *dcu_dev;
	int ret = 0;

	dcu_dev = dev_get_drvdata(dev);
	ret = clk_prepare_enable(dcu_dev->clk);
	if (!ret)
		ret = __dcu_init(&dcu_dev->dcu_params);

	return ret;
}
#endif

static const struct dev_pm_ops sirfsoc_dcu_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(sirfsoc_dcu_suspend,
	sirfsoc_dcu_pm_resume)
};

static int sirfsoc_dcu_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct dcu_device *dcu_dev;
	int ret = 0;

	memset(&dcu, 0x0, sizeof(dcu));
	dcu.inline_en = false;
	dcu_dev = &dcu;
	dcu_dev->pdev = pdev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		VDSSERR("can't get DCU core IORESOURCE_MEM\n");
		return -EINVAL;
	}
	dcu_dev->core_base = devm_ioremap(&pdev->dev, res->start,
		resource_size(res));

	if (!dcu_dev->core_base) {
		VDSSERR("can't dcu core ioremap\n");
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res) {
		VDSSERR("can't get NOCFIFO IORESOURCE_MEM\n");
		return -EINVAL;
	}
	dcu_dev->nocfifo_base = devm_ioremap(&pdev->dev, res->start,
			resource_size(res));

	if (!dcu_dev->nocfifo_base) {
		VDSSERR("can't nocfifo ioremap\n");
		return -ENOMEM;
	}

	dcu_dev->irq = platform_get_irq(pdev, 0);
	if (dcu_dev->irq < 0) {
		VDSSERR("platform_get_irq failed\n");
		return -EINVAL;
	}

	dcu_dev->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(dcu_dev->clk)) {
		VDSSERR("Failed to get dcu clock!\n");
		return -ENODEV;
	}

	clk_prepare_enable(dcu_dev->clk);

	dcu_dev->dcu_params.dev = &pdev->dev;
	dcu_dev->dcu_params.core_iomem = dcu_dev->core_base;
	dcu_dev->dcu_params.nocfifo_iomem = dcu_dev->nocfifo_base;

	__dcu_alloc_mot_buf(&dcu_dev->dcu_params);

	platform_set_drvdata(pdev, dcu_dev);

	ret = dcu_init_sysfs(dcu_dev);
	if (ret)
		return ret;

	vdss_debugfs_create_file("dcu_regs", dcu_dump_regs);

	return 0;
}

static int sirfsoc_dcu_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dcu_device *dcu_dev;

	dcu_dev = dev_get_drvdata(dev);
	__dcu_free_mot_buf(&dcu_dev->dcu_params);
	dcu_uninit_sysfs(dcu_dev);

	return 0;
}

static const struct of_device_id dcu_of_match[] = {
	{ .compatible = "sirf,atlas7-dcu", },
	{},
};

static struct platform_driver sirfsoc_dcu_driver = {
	.driver = {
		.name = "sirfsoc_dcu",
		.pm = &sirfsoc_dcu_pm_ops,
		.owner = THIS_MODULE,
		.of_match_table = dcu_of_match,
	},
	.probe = sirfsoc_dcu_probe,
	.remove = sirfsoc_dcu_remove,
};

int __init dcu_init_platform_driver(void)
{
	return platform_driver_register(&sirfsoc_dcu_driver);
}

void dcu_uninit_platform_driver(void)
{
	platform_driver_unregister(&sirfsoc_dcu_driver);
}
