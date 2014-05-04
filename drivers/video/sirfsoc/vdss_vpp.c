/*
 * CSR sirfsoc VPP library
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/bug.h>
#include <linux/string.h>
#include <linux/delay.h>

#include "vpp_defs.h"


struct sirfsoc_vpp_config vpp_config = {
	.initialized = false,
};

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
	0x199, 0x0, 0x12A,	/* V, U, Y for R */
	0xD0, 0x64, 0x12A,	/* V, U, Y for G */
	0x0, 0x204, 0x12A,	/* V, U, Y for B */
};

static const u32 rgb_offsets[] = {
	0xdf20,
	0x8760,
	0x114a0,
};


static void __vpp_set_color_ctrl(void)
{
	u32 reg_bc = 0x0;
	u32 reg_hs = 0x0;
	struct vpp_color_ctrl *clrctrl = &vpp_config.clr_ctrl;

	reg_bc = VPP_COLOR_B_CTRL(clrctrl->bright) |
		VPP_COLOR_C_CTRL(clrctrl->contrast);
	reg_hs = VPP_COLOR_UC_CTRL(clrctrl->uc) |
		VPP_COLOR_VC_CTRL(clrctrl->vc);
	vpp_write_reg(VPP_COLOR_BC_CTRL, reg_bc);
	vpp_write_reg(VPP_COLOR_HS_CTRL, reg_hs);
}

static void __vpp_setup(void)
{
	u32 reg_thresh = 0x0;
	u32 offset, val;
	int i;

	reg_thresh = VPP_FIFO_FULL_THRESH(0x8);
	vpp_write_reg(VPP_FULL_THRESH, reg_thresh);

	offset = VPP_HSCA_COEF00;
	for (i = 0; i < ARRAY_SIZE(tap_filter_coeff); i++) {
		vpp_write_reg(offset, tap_filter_coeff[i]);
		offset += 4;
	}

	offset = VPP_RCOEF;
	for (i = 0; i < ARRAY_SIZE(rgb_yuv_coeff); i += 3) {
		val = rgb_yuv_coeff[i] |
			(rgb_yuv_coeff[i+1] << 10) |
			(rgb_yuv_coeff[i+2] << 20);
		vpp_write_reg(offset, val);
		offset += 4;
	}

	vpp_write_reg(VPP_OFFSET1, rgb_offsets[0]);
	vpp_write_reg(VPP_OFFSET2, rgb_offsets[1]);
	vpp_write_reg(VPP_OFFSET3, rgb_offsets[2]);

	__vpp_set_color_ctrl();
}

static void __vpp_set_base(void)
{
	struct vpp_parms *parms = &vpp_config.surf_stat;
	u32 reg_ybase = 0x0;
	u32 reg_ubase = 0x0;
	u32 reg_vbase = 0x0;
	u32 reg_dstbase = 0x0;
	unsigned int yoffset_pixel, uoffset_pixel, voffset_pixel;

	u32 reg_ybase_bot = 0x0;
	u32 reg_ubase_bot = 0x0;
	u32 reg_vbase_bot = 0x0;

	yoffset_pixel = parms->src_wstride_pixel * vpp_config.src_rect.top +
			vpp_config.src_rect.left;
	if (parms->src_fmt == VDSS_PIXELFORMAT_YV12 ||
		parms->src_fmt == VDSS_PIXELFORMAT_I420) {
		uoffset_pixel = (parms->src_wstride_pixel / 2) *
				(vpp_config.src_rect.top / 2) +
				vpp_config.src_rect.left / 2;
		voffset_pixel = uoffset_pixel;
	} else if (parms->src_fmt == VDSS_PIXELFORMAT_IMC1 ||
			parms->src_fmt == VDSS_PIXELFORMAT_IMC3 ||
			parms->src_fmt == VDSS_PIXELFORMAT_IMC2 ||
			parms->src_fmt == VDSS_PIXELFORMAT_IMC4) {
		uoffset_pixel = parms->src_wstride_pixel *
				(vpp_config.src_rect.top / 2) +
				vpp_config.src_rect.left / 2;
		voffset_pixel = uoffset_pixel;
	} else if (parms->src_fmt == VDSS_PIXELFORMAT_NV12 ||
			parms->src_fmt == VDSS_PIXELFORMAT_NV21) {
		uoffset_pixel = parms->src_wstride_pixel *
				(vpp_config.src_rect.top / 2) +
				vpp_config.src_rect.left;
		voffset_pixel = uoffset_pixel;
	} else {
		voffset_pixel = uoffset_pixel = 0;
	}

	switch (parms->src_fmt) {
	/* TODO: Need to clarify the format layout later */
	case VDSS_PIXELFORMAT_YV12:
		reg_ybase = parms->src_base + yoffset_pixel;
		reg_ybase &= VPP_BASE_ADDR_MASK;
		reg_vbase = parms->src_base + parms->src_wstride_pixel *
			parms->src_hstride_pixel + voffset_pixel;
		reg_vbase &= VPP_BASE_ADDR_MASK;
		reg_ubase = parms->src_base + parms->src_wstride_pixel *
			parms->src_hstride_pixel * 5 / 4 + uoffset_pixel;
		reg_ubase &= VPP_BASE_ADDR_MASK;
		break;
	case VDSS_PIXELFORMAT_I420:
		reg_ybase = parms->src_base + yoffset_pixel;
		reg_ybase &= VPP_BASE_ADDR_MASK;
		reg_ubase = parms->src_base + parms->src_wstride_pixel *
			parms->src_hstride_pixel + uoffset_pixel;
		reg_ubase &= VPP_BASE_ADDR_MASK;
		reg_vbase = parms->src_base + parms->src_wstride_pixel *
			parms->src_hstride_pixel * 5 / 4 + voffset_pixel;
		reg_vbase &= VPP_BASE_ADDR_MASK;
		break;
	case VDSS_PIXELFORMAT_IMC1:
	case VDSS_PIXELFORMAT_IMC3:
		reg_ybase = parms->src_base + yoffset_pixel;
		reg_ybase &= VPP_BASE_ADDR_MASK;
		reg_vbase = parms->src_base + parms->src_wstride_pixel *
			parms->src_hstride_pixel + voffset_pixel;
		reg_vbase &= VPP_BASE_ADDR_MASK;
		reg_ubase = parms->src_base + parms->src_wstride_pixel *
			parms->src_hstride_pixel * 3 / 2 + uoffset_pixel;
		reg_ubase &= VPP_BASE_ADDR_MASK;
		break;
	case VDSS_PIXELFORMAT_IMC2:
	case VDSS_PIXELFORMAT_IMC4:
		reg_ybase = parms->src_base + yoffset_pixel;
		reg_ybase &= VPP_BASE_ADDR_MASK;
		reg_ubase = parms->src_base + parms->src_wstride_pixel *
			parms->src_hstride_pixel + uoffset_pixel;
		reg_ubase &= VPP_BASE_ADDR_MASK;
		reg_vbase = parms->src_base + parms->src_wstride_pixel *
			parms->src_hstride_pixel + voffset_pixel +
			parms->src_wstride_pixel / 2;
		reg_vbase &= VPP_BASE_ADDR_MASK;
		break;
	case VDSS_PIXELFORMAT_NV12:
	case VDSS_PIXELFORMAT_NV21:
		/* NV12, NV21, hw requried UV base right shift 1*/
		reg_ybase = parms->src_base + yoffset_pixel;
		reg_ybase &= VPP_BASE_ADDR_MASK;
		reg_ubase = (parms->src_base + parms->src_wstride_pixel *
			((parms->src_hstride_pixel + 0x3f) & (~0x3f)) +
			uoffset_pixel) >> 1;
		reg_ubase &= VPP_BASE_ADDR_MASK;
		reg_vbase = reg_ubase;
		break;
	case VDSS_PIXELFORMAT_UYVY:
	case VDSS_PIXELFORMAT_UYNV:
	case VDSS_PIXELFORMAT_YUY2:
	case VDSS_PIXELFORMAT_YUYV:
	case VDSS_PIXELFORMAT_YUNV:
	case VDSS_PIXELFORMAT_YVYU:
	case VDSS_PIXELFORMAT_VYUY:
		reg_ybase = parms->src_base + (2 * yoffset_pixel);
		reg_ybase &= VPP_BASE_ADDR_MASK;
		reg_ubase = reg_vbase = reg_ybase;
		break;
	default:
		LCDC_ERR("%s(%d): unknown format 0x%x\n",
			__func__, __LINE__, parms->src_fmt);
		break;
	}

	if (vpp_config.interlace.in_interlaced) {
		if (vpp_config.interlace.field_offset) {
			reg_ybase_bot = reg_ybase +
				vpp_config.interlace.field_offset;
			reg_ybase_bot &= VPP_BASE_ADDR_BOT_MASK;
			reg_vbase_bot = reg_vbase +
				vpp_config.interlace.field_offset;
			reg_vbase_bot &= VPP_BASE_ADDR_BOT_MASK;
			reg_ubase_bot = reg_ubase +
				vpp_config.interlace.field_offset;
			reg_ubase_bot &= VPP_BASE_ADDR_BOT_MASK;
		} else {
			switch (parms->src_fmt) {
			case VDSS_PIXELFORMAT_YV12:
			case VDSS_PIXELFORMAT_I420:
			case VDSS_PIXELFORMAT_NV12:
			case VDSS_PIXELFORMAT_NV21:
				reg_ybase_bot = reg_ybase +
					parms->src_wstride_pixel;
				reg_ybase_bot &= VPP_BASE_ADDR_BOT_MASK;
				reg_vbase_bot = reg_vbase +
					parms->src_wstride_pixel / 2;
				reg_vbase_bot &= VPP_BASE_ADDR_BOT_MASK;
				reg_ubase_bot = reg_ubase +
					parms->src_wstride_pixel / 2;
				reg_ubase_bot &= VPP_BASE_ADDR_BOT_MASK;
				break;
			case VDSS_PIXELFORMAT_IMC4:
			case VDSS_PIXELFORMAT_IMC3:
			case VDSS_PIXELFORMAT_IMC2:
			case VDSS_PIXELFORMAT_IMC1:
				reg_ybase_bot = reg_ybase +
					parms->src_wstride_pixel;
				reg_ybase_bot &= VPP_BASE_ADDR_BOT_MASK;
				reg_vbase_bot = reg_vbase +
					parms->src_wstride_pixel;
				reg_vbase_bot &= VPP_BASE_ADDR_BOT_MASK;
				reg_ubase_bot = reg_ubase +
					parms->src_wstride_pixel;
				reg_ubase_bot &= VPP_BASE_ADDR_BOT_MASK;
				break;
			case VDSS_PIXELFORMAT_UYVY:
			case VDSS_PIXELFORMAT_UYNV:
			case VDSS_PIXELFORMAT_YUY2:
			case VDSS_PIXELFORMAT_YUYV:
			case VDSS_PIXELFORMAT_YUNV:
			case VDSS_PIXELFORMAT_YVYU:
			case VDSS_PIXELFORMAT_VYUY:
				reg_ybase_bot = reg_ybase +
					2 * parms->src_wstride_pixel;
				reg_ybase_bot &= VPP_BASE_ADDR_BOT_MASK;
				reg_ubase_bot = reg_vbase_bot = reg_ybase_bot;
				break;
			default:
				LCDC_ERR("%s(%d): unknown format 0x%x\n",
					__func__, __LINE__, parms->src_fmt);
				break;
			}
		}

		if (vpp_config.interlace.input_top_first) {
			vpp_write_reg(VPP_YBASE, reg_ybase);
			vpp_write_reg(VPP_UBASE, reg_ubase);
			vpp_write_reg(VPP_VBASE, reg_vbase);
			vpp_write_reg(VPP_YBASE_BOT, reg_ybase_bot);
			vpp_write_reg(VPP_UBASE_BOT, reg_ubase_bot);
			vpp_write_reg(VPP_VBASE_BOT, reg_vbase_bot);
		} else {
			vpp_write_reg(VPP_YBASE_BOT, reg_ybase);
			vpp_write_reg(VPP_UBASE_BOT, reg_ubase);
			vpp_write_reg(VPP_VBASE_BOT, reg_vbase);
			vpp_write_reg(VPP_YBASE, reg_ybase_bot);
			vpp_write_reg(VPP_UBASE, reg_ubase_bot);
			vpp_write_reg(VPP_VBASE, reg_vbase_bot);
		}
	} else {
		vpp_write_reg(VPP_YBASE, reg_ybase);
		vpp_write_reg(VPP_UBASE, reg_ubase);
		vpp_write_reg(VPP_VBASE, reg_vbase);
	}

	if (parms->dst_base) {
		unsigned int bpp;

		if (parms->dst_fmt == VDSS_PIXELFORMAT_666 ||
			parms->dst_fmt == VDSS_PIXELFORMAT_RGBX_8880 ||
			parms->dst_fmt == VDSS_PIXELFORMAT_BGRX_8880)
			bpp = 4;
		else
			bpp = 2;

		yoffset_pixel = parms->dst_wstride_pixel *
				vpp_config.dst_rect.top +
				vpp_config.dst_rect.left;
		reg_dstbase = (parms->dst_base + yoffset_pixel * bpp) & (~7);
		reg_dstbase &= VPP_BASE_ADDR_MASK;
		vpp_write_reg(VPP_DESBASE, reg_dstbase);

		if (vpp_config.interlace.out_mode == VPP_OUTPUT_INTERLACE)
			vpp_write_reg(VPP_DESBASE_BOT, VPP_BASE_ADDR_MASK &
				(reg_dstbase + parms->dst_wstride_pixel * bpp));
		else if (vpp_config.interlace.out_mode == VPP_OUTPUT_P_DOUBLE)
			vpp_write_reg(VPP_DESBASE_BOT, VPP_BASE_ADDR_MASK &
				(reg_dstbase + parms->dst_wstride_pixel *
				 parms->dst_hstride_pixel * bpp));
	}

	return;
}


static bool __vpp_set_size(void)
{
	u32 src_width = vpp_config.src_rect.right - vpp_config.src_rect.left;
	u32 dst_width = vpp_config.dst_rect.right - vpp_config.dst_rect.left;
	u32 src_height = vpp_config.src_rect.bottom - vpp_config.src_rect.top;
	u32 dst_height = vpp_config.dst_rect.bottom - vpp_config.dst_rect.top;
	u32 reg_width = 0x0;
	u32 reg_height = 0x0;

	vpp_config.valid = true;
	reg_width = VPP_SRC_WIDTH(src_width) | VPP_DES_WIDTH(dst_width);
	vpp_write_reg(VPP_WIDTH, reg_width);

	reg_height = VPP_SRC_HEIGHT(src_height) | VPP_DES_HEIGHT(dst_height);
	vpp_write_reg(VPP_HEIGHT, reg_height);

	__vpp_set_base();
	return true;
}

static bool __vpp_set_params(void)
{
	u32 reg_ctrl = 0x0;
	u32 reg_stride0 = 0x0, reg_stride1 = 0x0;
	u32 reg_thresh;

	struct vpp_parms *parms = &vpp_config.surf_stat;
	struct vpp_interlace_data *interlace = &vpp_config.interlace;

	switch (parms->src_fmt) {
	/* TODO: Need to clarify the format layout later */
	case VDSS_PIXELFORMAT_YV12:
	case VDSS_PIXELFORMAT_I420:
	/* NV12, NV21, hw requried UV stride right shift 1 */
	case VDSS_PIXELFORMAT_NV12:
	case VDSS_PIXELFORMAT_NV21:
		reg_ctrl |= VPP_CTRL_PIXEL_FORMAT;	/* YUV420 */
		reg_stride0 |= VPP_Y_STRIDE(parms->src_wstride_pixel);
		reg_stride0 |= VPP_U_STRIDE(parms->src_wstride_pixel / 2);
		reg_stride1 |= VPP_V_STRIDE(parms->src_wstride_pixel / 2);
		break;
	case VDSS_PIXELFORMAT_IMC4:
	case VDSS_PIXELFORMAT_IMC3:
	case VDSS_PIXELFORMAT_IMC2:
	case VDSS_PIXELFORMAT_IMC1:
		reg_ctrl |= VPP_CTRL_PIXEL_FORMAT;	/* YUV420 */
		reg_stride0 |= VPP_Y_STRIDE(parms->src_wstride_pixel);
		reg_stride0 |= VPP_U_STRIDE(parms->src_wstride_pixel);
		reg_stride1 |= VPP_V_STRIDE(parms->src_wstride_pixel);
		break;
	case VDSS_PIXELFORMAT_UYVY:
		reg_ctrl &= ~VPP_CTRL_PIXEL_FORMAT;	/* YUV422 */
		reg_ctrl &= ~VPP_CTRL_ENDIAN_MODE;	/* LITTLE MODE */
		reg_ctrl |= VPP_CTRL_YUV422_FORMAT(VPP_YUV422_FORMAT_VYUY);
		reg_stride0 |= VPP_Y_STRIDE(parms->src_wstride_pixel * 2);
		break;
	case VDSS_PIXELFORMAT_UYNV:
		reg_ctrl &= ~VPP_CTRL_PIXEL_FORMAT;	/* YUV422 */
		reg_ctrl &= ~VPP_CTRL_ENDIAN_MODE;	/* LITTLE MODE */
		reg_ctrl |= VPP_CTRL_YUV422_FORMAT(VPP_YUV422_FORMAT_UYVY);
		reg_stride0 |= VPP_Y_STRIDE(parms->src_wstride_pixel * 2);
		break;
	case VDSS_PIXELFORMAT_YUY2:
	case VDSS_PIXELFORMAT_YUYV:
	case VDSS_PIXELFORMAT_YUNV:
		reg_ctrl &= ~VPP_CTRL_PIXEL_FORMAT;	/* YUV422 */
		reg_ctrl &= ~VPP_CTRL_ENDIAN_MODE;	/* LITTLE MODE */
		reg_ctrl |= VPP_CTRL_YUV422_FORMAT(VPP_YUV422_FORMAT_YUYV);
		reg_stride0 |= VPP_Y_STRIDE(parms->src_wstride_pixel * 2);
		break;
	case VDSS_PIXELFORMAT_YVYU:
		reg_ctrl &= ~VPP_CTRL_PIXEL_FORMAT;	/* YUV422 */
		reg_ctrl &= ~VPP_CTRL_ENDIAN_MODE;	/* LITTLE MODE */
		reg_ctrl |= VPP_CTRL_YUV422_FORMAT(VPP_YUV422_FORMAT_YVYU);
		reg_stride0 |= VPP_Y_STRIDE(parms->src_wstride_pixel * 2);
		break;
	case VDSS_PIXELFORMAT_VYUY:
		reg_ctrl &= ~VPP_CTRL_PIXEL_FORMAT;	/* YUV422 */
		reg_ctrl &= ~VPP_CTRL_ENDIAN_MODE;	/* LITTLE MODE */
		reg_ctrl |= VPP_CTRL_YUV422_FORMAT(VPP_YUV422_FORMAT_VYUY);
		reg_stride0 |= VPP_Y_STRIDE(parms->src_wstride_pixel * 2);
		break;
	default:
		LCDC_ERR("%s(%d): unknown format 0x%x\n",
			__func__, __LINE__, parms->src_fmt);
		return false;
	}

	if (parms->src_fmt == VDSS_PIXELFORMAT_NV12 ||
		parms->src_fmt == VDSS_PIXELFORMAT_NV21) {
		reg_ctrl |= VPP_CTRL_UV_INTERLEAVE_EN;
		vpp_config.uv_interleave = true;
	} else {
		vpp_config.uv_interleave = false;
	}

	/* Attention, this is write only*/
	reg_thresh = vpp_read_reg(VPP_FULL_THRESH);
	if (parms->src_fmt == VDSS_PIXELFORMAT_NV12)
		reg_thresh |= VPP_UVUV_MODE;
	vpp_write_reg(VPP_FULL_THRESH, reg_thresh);

	if (interlace->in_interlaced) {
		if (interlace->field_offset == 0) {
			reg_stride0 = (reg_stride0 * 2) &
				(VPP_Y_STRIDE_MASK | VPP_U_STRIDE_MASK);
			reg_stride1 = (reg_stride1 * 2) & VPP_V_STRIDE_MASK;
		}

		if (interlace->out_mode == VPP_OUTPUT_INTERLACE) {
			reg_ctrl |= VPP_CTRL_SEQ_TYPE(VPP_SEQ_TYPE_IIIO);
			if (interlace->output_top_first)
				reg_ctrl |= VPP_CTRL_TOP_FIELD_FIRST;
			reg_ctrl |= VPP_CTRL_HW_DI_MODE(0);
		} else if (interlace->out_mode == VPP_OUTPUT_P_DOUBLE) {
			reg_ctrl |= VPP_CTRL_DOUBLE_FRATE;
			reg_ctrl |= VPP_CTRL_SEQ_TYPE(VPP_SEQ_TYPE_IIPO);
			if (interlace->di_top)
				reg_ctrl |= VPP_CTRL_DI_FIELD_BOT;
			if (interlace->output_top_first)
				reg_ctrl |= VPP_CTRL_TOP_FIELD_FIRST;
			reg_ctrl |= VPP_CTRL_HW_DI_MODE(interlace->di_mode);
		} else {
			reg_ctrl |= VPP_CTRL_SEQ_TYPE(VPP_SEQ_TYPE_IIPO);
			if (interlace->di_top)
				reg_ctrl |= VPP_CTRL_DI_FIELD_BOT;
			reg_ctrl |= VPP_CTRL_HW_DI_MODE(interlace->di_mode);
		}
	} else {
		if (interlace->out_mode == VPP_OUTPUT_INTERLACE) {
			reg_ctrl |= VPP_CTRL_SEQ_TYPE(VPP_SEQ_TYPE_PIIO);
			reg_ctrl |= VPP_CTRL_HW_DI_MODE(0);
			if (interlace->output_top_first)
				reg_ctrl |= VPP_CTRL_TOP_FIELD_FIRST;
		} else {
			reg_ctrl |= VPP_CTRL_SEQ_TYPE(VPP_SEQ_TYPE_PIPO);
			reg_ctrl |= VPP_CTRL_HW_DI_MODE(0);
		}
	}

	if (parms->dst_base == 0)
		reg_ctrl |= VPP_CTRL_DEST;	/* LCD */
	else
		reg_ctrl &= ~VPP_CTRL_DEST;	/* MEMORY */

	switch (parms->dst_fmt) {
	case VDSS_PIXELFORMAT_565:
		reg_ctrl |= VPP_CTRL_OUT_FORMAT(VPP_OUT_FORMAT_RGB565);
		reg_stride1 |= VPP_DES_STRIDE(parms->dst_wstride_pixel * 2);
		break;
	case VDSS_PIXELFORMAT_666:
		reg_ctrl |= VPP_CTRL_OUT_FORMAT(VPP_OUT_FORMAT_RGB666);
		reg_stride1 |= VPP_DES_STRIDE(parms->dst_wstride_pixel * 4);
		break;
	case VDSS_PIXELFORMAT_BGRX_8880:
	case VDSS_PIXELFORMAT_RGBX_8880:
		reg_ctrl |= VPP_CTRL_OUT_FORMAT(VPP_OUT_FORMAT_RGB888);
		reg_stride1 |= VPP_DES_STRIDE(parms->dst_wstride_pixel * 4);
		break;
	case VDSS_PIXELFORMAT_YUYV:
		reg_ctrl |= VPP_CTRL_OUT_FORMAT(VPP_OUT_FORMAT_YUV422);
		reg_ctrl |= VPP_CTRL_OUT_YUV422_FORMAT(VPP_YUV422_FORMAT_YUYV);
		reg_stride1 |= VPP_DES_STRIDE(parms->dst_wstride_pixel * 2);
		break;
	case VDSS_PIXELFORMAT_YVYU:
		reg_ctrl |= VPP_CTRL_OUT_FORMAT(VPP_OUT_FORMAT_YUV422);
		reg_ctrl |= VPP_CTRL_OUT_YUV422_FORMAT(VPP_YUV422_FORMAT_YVYU);
		reg_stride1 |= VPP_DES_STRIDE(parms->dst_wstride_pixel * 2);
		break;
	case VDSS_PIXELFORMAT_UYVY:
		reg_ctrl |= VPP_CTRL_OUT_FORMAT(VPP_OUT_FORMAT_YUV422);
		reg_ctrl |= VPP_CTRL_OUT_YUV422_FORMAT(VPP_YUV422_FORMAT_UYVY);
		reg_stride1 |= VPP_DES_STRIDE(parms->dst_wstride_pixel * 2);
		break;
	case VDSS_PIXELFORMAT_VYUY:
		reg_ctrl |= VPP_CTRL_OUT_FORMAT(VPP_OUT_FORMAT_YUV422);
		reg_ctrl |= VPP_CTRL_OUT_YUV422_FORMAT(VPP_YUV422_FORMAT_VYUY);
		reg_stride1 |= VPP_DES_STRIDE(parms->dst_wstride_pixel * 2);
		break;
	default:
		LCDC_ERR("%s(%d): unknown format 0x%x\n",
			__func__, __LINE__, parms->dst_fmt);
		return false;
	}
	vpp_write_reg(VPP_CTRL, reg_ctrl);
	vpp_write_reg(VPP_STRIDE0, reg_stride0);
	vpp_write_reg(VPP_STRIDE1, reg_stride1);

	if (parms->dst_fmt == VDSS_PIXELFORMAT_RGBX_8880) {
		u32 val;
		val = rgb_yuv_coeff[0] | (rgb_yuv_coeff[1] << 10) |
			(rgb_yuv_coeff[2] << 20);
		vpp_write_reg(VPP_BCOEF, val);
		val = rgb_yuv_coeff[6] | (rgb_yuv_coeff[7] << 10) |
			(rgb_yuv_coeff[8] << 20);
		vpp_write_reg(VPP_RCOEF, val);

		vpp_write_reg(VPP_OFFSET3, rgb_offsets[0]);
		vpp_write_reg(VPP_OFFSET1, rgb_offsets[2]);
	} else {
		u32 val;
		val = rgb_yuv_coeff[0] | (rgb_yuv_coeff[1] << 10) |
			(rgb_yuv_coeff[2] << 20);
		vpp_write_reg(VPP_RCOEF, val);
		val = rgb_yuv_coeff[6] | (rgb_yuv_coeff[7] << 10) |
			(rgb_yuv_coeff[8] << 20);
		vpp_write_reg(VPP_BCOEF, val);

		vpp_write_reg(VPP_OFFSET1, rgb_offsets[0]);
		vpp_write_reg(VPP_OFFSET3, rgb_offsets[2]);
	}

	return true;
}


/*
 * VPP SOC function
 */
static void vpp_init(void *vpp_regs)
{
	LCDC_ENTRY("%s\n", __func__);
	if (!vpp_config.initialized) {
		memset(&vpp_config, 0, sizeof(vpp_config));
		vpp_config.initialized = true;

		if (vpp_regs == NULL) {
			if (vpp_config.vpp_regs == NULL)
				vpp_config.need_unmap = true;
		} else {
			vpp_config.need_unmap = false;
			vpp_config.vpp_regs = vpp_regs;
		}

		vpp_config.dma_interrupt_enabled = false;

		vpp_config.hscaling_ratio_last = 1.0;
		vpp_config.vscaling_ratio_last = 1.0;

		vpp_config.clr_ctrl.uc = 0x100;
		vpp_config.clr_ctrl.vc = 0;
		vpp_config.clr_ctrl.bright = 0;
		vpp_config.clr_ctrl.contrast = 0x80;

		vpp_config.hue = 0;
		vpp_config.saturation = 0x80;

		__vpp_setup();
	}
	vpp_config.ref_count++;
}

static void vpp_terminate(void)
{
	LCDC_ENTRY("%s\n", __func__);
	vpp_config.ref_count--;
	if (vpp_config.ref_count == 0) {
		vpp_config.initialized = false;
		if (vpp_config.need_unmap) {
			vpp_config.need_unmap = false;
			vpp_config.vpp_regs = NULL;
		}
	}
}

#define ALIGN_SIZE(size, align) ((size + align - 1) & ~(align - 1))

static bool vpp_alloc_overlay(struct lcdc_overlay *data)
{
	LCDC_ENTRY("%s\n", __func__);
	switch (data->fmt) {
	case VDSS_PIXELFORMAT_NV12:
	case VDSS_PIXELFORMAT_NV21:
		/* VXD require wstride&hstride to be 64 pixel aligned */
		data->wstride_byte = data->wstride_pixel =
			ALIGN_SIZE(data->width, 64);
		data->hstride_pixel = ALIGN_SIZE(data->height, 64);
		data->hstride_byte = ALIGN_SIZE(data->wstride_byte *
					data->hstride_pixel * 2, 4096);
		break;
	case VDSS_PIXELFORMAT_I420:
		/* MVED require wstride&hstride to be 16 pixel aligned */
		data->wstride_byte = data->wstride_pixel =
			ALIGN_SIZE(data->width, 16);
		data->hstride_pixel = ALIGN_SIZE(data->height, 16);
		data->hstride_byte = data->wstride_byte *
					data->hstride_pixel * 3 / 2;
		break;
	case VDSS_PIXELFORMAT_YV12:
		data->wstride_pixel = ALIGN_SIZE(data->width, 16);
		data->hstride_pixel = data->height;
		data->wstride_byte = data->wstride_pixel;
		data->hstride_byte = data->wstride_byte * data->height;
		break;
	case VDSS_PIXELFORMAT_IMC1:
	case VDSS_PIXELFORMAT_IMC3:
	case VDSS_PIXELFORMAT_VYUY:
		data->wstride_pixel = ALIGN_SIZE(data->width, 8);
		data->hstride_pixel = data->height;
		data->wstride_byte = data->wstride_pixel;
		data->hstride_byte = data->wstride_byte * data->height;
		break;
	case VDSS_PIXELFORMAT_UYVY:
	case VDSS_PIXELFORMAT_YUY2:
	case VDSS_PIXELFORMAT_YVYU:
	case VDSS_PIXELFORMAT_YUYV:
		data->wstride_byte = ALIGN_SIZE(data->width * 2, 8);
		data->hstride_byte = data->wstride_byte * data->height;
		data->wstride_pixel = data->wstride_byte / 2;
		data->hstride_pixel = data->height;
		break;
	default:
		LCDC_ERR("%s(%d): unknown format 0x%x\n",
			__func__, __LINE__, data->fmt);
		return false;
	}
	return true;
}

static bool vpp_lock(bool continues)
{
	LCDC_ENTRY("%s\n", __func__);
	if (vpp_config.continue_lock) {
		return false;
	} else {
		vpp_config.continue_lock = continues;
		return true;
	}
}
static void vpp_unlock(void)
{
	LCDC_ENTRY("%s\n", __func__);
	vpp_config.continue_lock = false;
}


/* Set parameters which will be set only once */
static bool vpp_set_params(struct vpp_parms *parms)
{
	LCDC_ENTRY("%s\n", __func__);
	vpp_config.surf_stat = *parms;
	__vpp_set_params();
	return true;
}

static void vpp_set_base(unsigned long base)
{
	LCDC_ENTRY("%s\n", __func__);
	vpp_config.surf_stat.src_base = base;
	__vpp_set_base();
}

static bool vpp_set_size(struct vdss_rect *src_rect,
			struct vdss_rect *dst_rect)
{
	LCDC_ENTRY("%s\n", __func__);
	vpp_config.src_rect = *src_rect;
	vpp_config.dst_rect = *dst_rect;
	return __vpp_set_size();
}

static void vpp_start(bool continues)
{
	u32 reg_ctrl;

	LCDC_ENTRY("%s\n", __func__);
	reg_ctrl = vpp_read_reg(VPP_CTRL);
	if (vpp_config.uv_interleave)
		reg_ctrl |= VPP_CTRL_UV_INTERLEAVE_EN;
	reg_ctrl |= VPP_CTRL_START;
	vpp_write_reg(VPP_CTRL, reg_ctrl);
}

static void vpp_stop(void)
{
	u32 reg_ctrl;
	LCDC_ENTRY("%s\n", __func__);
	reg_ctrl = vpp_read_reg(VPP_CTRL);
	vpp_write_reg(VPP_CTRL, reg_ctrl);
}

static bool vpp_is_busy(void)
{
	u32 reg_ctrl;
	LCDC_ENTRY("%s\n", __func__);
	reg_ctrl = vpp_read_reg(VPP_CTRL);
	return ((reg_ctrl & VPP_CTRL_BUSY_STATUS) != 0);
}

static void vpp_clear_dma_interrupt(void)
{
	u32 reg_int_stat = 0x0;
	reg_int_stat = VPP_INT_SINGLE_STATUS;
	vpp_write_reg(VPP_INT_STATUS, reg_int_stat);
}

static void vpp_enable_dma_interrupt(void)
{
	u32 reg_int_mask;
	vpp_clear_dma_interrupt();
	vpp_config.dma_interrupt_enabled = true;
	reg_int_mask = vpp_read_reg(VPP_INT_MASK);
	reg_int_mask |= VPP_INT_SINGLE_ENABLE;
	vpp_write_reg(VPP_INT_MASK, reg_int_mask);
}

static void vpp_disable_dma_interrupt(void)
{
	u32 reg_int_mask;
	vpp_config.dma_interrupt_enabled = false;
	reg_int_mask = vpp_read_reg(VPP_INT_MASK);
	reg_int_mask &= ~VPP_INT_SINGLE_ENABLE;
	vpp_write_reg(VPP_INT_MASK, reg_int_mask);
}

static bool vpp_dma_irq_detected(void)
{
	u32 reg_int_stat;
	reg_int_stat = vpp_read_reg(VPP_INT_STATUS);
	return ((reg_int_stat & VPP_INT_SINGLE_STATUS) != 0);
}

static void vpp_sleep(void)
{
	int count = 0;
	LCDC_ENTRY("%s\n", __func__);
	while (vpp_is_busy()) {
		msleep(20);
		count++;
		if (count > 20) {
			LCDC_ERR("%s(%d): VPP can't stop\n",
				__func__, __LINE__);
			break;
		}
	}
}

static void vpp_wakeup(void)
{
	LCDC_ENTRY("%s\n", __func__);
	__vpp_setup();

	/* Coefs are reset to default table */
	vpp_config.hscaling_ratio_last = 1.0;
	vpp_config.vscaling_ratio_last = 1.0;

#if 0
	/*
	 * Restore registers base on software state, but it seems LCD or blt
	 * function will update these parameters.
	 */
	__vpp_set_params();
	__vpp_set_size();
#endif
	__vpp_set_color_ctrl();
	if (vpp_config.dma_interrupt_enabled)
		vpp_enable_dma_interrupt();
}


static void vpp_update_bright(int brightness)
{
	u32 reg_bc_ctrl = 0x0;
	struct vpp_color_ctrl *clr_ctrl = &vpp_config.clr_ctrl;
	LCDC_ENTRY("%s\n", __func__);

	clr_ctrl->bright = (short)brightness;

	reg_bc_ctrl = VPP_COLOR_B_CTRL(clr_ctrl->bright) |
			VPP_COLOR_C_CTRL(clr_ctrl->contrast);
	vpp_write_reg(VPP_COLOR_BC_CTRL, reg_bc_ctrl);
}

static void vpp_update_contrast(int contrast)
{
	u32 reg_bc_ctrl = 0x0;
	struct vpp_color_ctrl *clr_ctrl = &vpp_config.clr_ctrl;
	LCDC_ENTRY("%s\n", __func__);

	clr_ctrl->contrast = (short)contrast;

	reg_bc_ctrl = VPP_COLOR_B_CTRL(clr_ctrl->bright) |
			VPP_COLOR_C_CTRL(clr_ctrl->contrast);
	vpp_write_reg(VPP_COLOR_BC_CTRL, reg_bc_ctrl);
}
static void vpp_set_color_ctrl(struct vpp_color_ctrl *data)
{
	LCDC_ENTRY("%s\n", __func__);
	vpp_config.clr_ctrl = *data;
	__vpp_set_color_ctrl();
}

static void vpp_set_interlace(bool input_mode, int out_mode,
	bool output_top_first, bool top_field_reserved, int di_mode,
	bool input_top_first, u32 field_offset)
{
	LCDC_ENTRY("%s\n", __func__);
	vpp_config.interlace.in_interlaced = input_mode;
	vpp_config.interlace.out_mode = out_mode;
	vpp_config.interlace.output_top_first = output_top_first;
	vpp_config.interlace.input_top_first = input_top_first;
	vpp_config.interlace.di_top = top_field_reserved;
	vpp_config.interlace.di_mode = di_mode;
	vpp_config.interlace.field_offset = field_offset;
}

static void vpp_print_register(void)
{
	LCDC_DUMP("VPP registers:\n");
	LCDC_DUMP("VPP_CTRL=0x%08x\n",		vpp_read_reg(VPP_CTRL));
	LCDC_DUMP("VPP_YBASE=0x%08x\n",		vpp_read_reg(VPP_YBASE));
	LCDC_DUMP("VPP_UBASE=0x%08x\n",		vpp_read_reg(VPP_UBASE));
	LCDC_DUMP("VPP_VBASE=0x%08x\n",		vpp_read_reg(VPP_VBASE));
	LCDC_DUMP("VPP_DESBASE=0x%08x\n",	vpp_read_reg(VPP_DESBASE));
	LCDC_DUMP("VPP_WIDTH =0x%08x\n",	vpp_read_reg(VPP_WIDTH));
	LCDC_DUMP("VPP_HEIGHT=0x%08x\n",	vpp_read_reg(VPP_HEIGHT));
	LCDC_DUMP("VPP_STRIDE0=0x%08x\n",	vpp_read_reg(VPP_STRIDE0));
	LCDC_DUMP("VPP_STRIDE1=0x%08x\n",	vpp_read_reg(VPP_STRIDE1));
	LCDC_DUMP("VPP_HSCA_COEF00=0x%08x\n",	vpp_read_reg(VPP_HSCA_COEF00));
	LCDC_DUMP("VPP_HSCA_COEF01=0x%08x\n",	vpp_read_reg(VPP_HSCA_COEF01));
	LCDC_DUMP("VPP_HSCA_COEF02=0x%08x\n",	vpp_read_reg(VPP_HSCA_COEF02));
	LCDC_DUMP("VPP_HSCA_COEF10=0x%08x\n",	vpp_read_reg(VPP_HSCA_COEF10));
	LCDC_DUMP("VPP_HSCA_COEF11=0x%08x\n",	vpp_read_reg(VPP_HSCA_COEF11));
	LCDC_DUMP("VPP_HSCA_COEF12=0x%08x\n",	vpp_read_reg(VPP_HSCA_COEF12));
	LCDC_DUMP("VPP_HSCA_COEF20=0x%08x\n",	vpp_read_reg(VPP_HSCA_COEF20));
	LCDC_DUMP("VPP_HSCA_COEF21=0x%08x\n",	vpp_read_reg(VPP_HSCA_COEF21));
	LCDC_DUMP("VPP_HSCA_COEF22=0x%08x\n",	vpp_read_reg(VPP_HSCA_COEF22));
	LCDC_DUMP("VPP_HSCA_COEF30=0x%08x\n",	vpp_read_reg(VPP_HSCA_COEF30));
	LCDC_DUMP("VPP_HSCA_COEF31=0x%08x\n",	vpp_read_reg(VPP_HSCA_COEF31));
	LCDC_DUMP("VPP_HSCA_COEF32=0x%08x\n",	vpp_read_reg(VPP_HSCA_COEF32));
	LCDC_DUMP("VPP_HSCA_COEF40=0x%08x\n",	vpp_read_reg(VPP_HSCA_COEF40));
	LCDC_DUMP("VPP_HSCA_COEF41=0x%08x\n",	vpp_read_reg(VPP_HSCA_COEF41));
	LCDC_DUMP("VPP_HSCA_COEF42=0x%08x\n",	vpp_read_reg(VPP_HSCA_COEF42));
	LCDC_DUMP("VPP_HSCA_COEF50=0x%08x\n",	vpp_read_reg(VPP_HSCA_COEF50));
	LCDC_DUMP("VPP_HSCA_COEF51=0x%08x\n",	vpp_read_reg(VPP_HSCA_COEF51));
	LCDC_DUMP("VPP_HSCA_COEF52=0x%08x\n",	vpp_read_reg(VPP_HSCA_COEF52));
	LCDC_DUMP("VPP_HSCA_COEF60=0x%08x\n",	vpp_read_reg(VPP_HSCA_COEF60));
	LCDC_DUMP("VPP_HSCA_COEF61=0x%08x\n",	vpp_read_reg(VPP_HSCA_COEF61));
	LCDC_DUMP("VPP_HSCA_COEF62=0x%08x\n",	vpp_read_reg(VPP_HSCA_COEF62));
	LCDC_DUMP("VPP_HSCA_COEF70=0x%08x\n",	vpp_read_reg(VPP_HSCA_COEF70));
	LCDC_DUMP("VPP_HSCA_COEF71=0x%08x\n",	vpp_read_reg(VPP_HSCA_COEF71));
	LCDC_DUMP("VPP_HSCA_COEF72=0x%08x\n",	vpp_read_reg(VPP_HSCA_COEF72));
	LCDC_DUMP("VPP_HSCA_COEF80=0x%08x\n",	vpp_read_reg(VPP_HSCA_COEF80));
	LCDC_DUMP("VPP_HSCA_COEF81=0x%08x\n",	vpp_read_reg(VPP_HSCA_COEF81));
	LCDC_DUMP("VPP_HSCA_COEF82=0x%08x\n",	vpp_read_reg(VPP_HSCA_COEF82));
	LCDC_DUMP("VPP_VSCA_COEF00=0x%08x\n",	vpp_read_reg(VPP_VSCA_COEF00));
	LCDC_DUMP("VPP_VSCA_COEF01=0x%08x\n",	vpp_read_reg(VPP_VSCA_COEF01));
	LCDC_DUMP("VPP_VSCA_COEF10=0x%08x\n",	vpp_read_reg(VPP_VSCA_COEF10));
	LCDC_DUMP("VPP_VSCA_COEF11=0x%08x\n",	vpp_read_reg(VPP_VSCA_COEF11));
	LCDC_DUMP("VPP_VSCA_COEF20=0x%08x\n",	vpp_read_reg(VPP_VSCA_COEF20));
	LCDC_DUMP("VPP_VSCA_COEF21=0x%08x\n",	vpp_read_reg(VPP_VSCA_COEF21));
	LCDC_DUMP("VPP_VSCA_COEF30=0x%08x\n",	vpp_read_reg(VPP_VSCA_COEF30));
	LCDC_DUMP("VPP_VSCA_COEF31=0x%08x\n",	vpp_read_reg(VPP_VSCA_COEF31));
	LCDC_DUMP("VPP_VSCA_COEF40=0x%08x\n",	vpp_read_reg(VPP_VSCA_COEF40));
	LCDC_DUMP("VPP_VSCA_COEF41=0x%08x\n",	vpp_read_reg(VPP_VSCA_COEF41));
	LCDC_DUMP("VPP_VSCA_COEF50=0x%08x\n",	vpp_read_reg(VPP_VSCA_COEF50));
	LCDC_DUMP("VPP_VSCA_COEF51=0x%08x\n",	vpp_read_reg(VPP_VSCA_COEF51));
	LCDC_DUMP("VPP_VSCA_COEF60=0x%08x\n",	vpp_read_reg(VPP_VSCA_COEF60));
	LCDC_DUMP("VPP_VSCA_COEF61=0x%08x\n",	vpp_read_reg(VPP_VSCA_COEF61));
	LCDC_DUMP("VPP_VSCA_COEF70=0x%08x\n",	vpp_read_reg(VPP_VSCA_COEF70));
	LCDC_DUMP("VPP_VSCA_COEF71=0x%08x\n",	vpp_read_reg(VPP_VSCA_COEF71));
	LCDC_DUMP("VPP_VSCA_COEF80=0x%08x\n",	vpp_read_reg(VPP_VSCA_COEF80));
	LCDC_DUMP("VPP_VSCA_COEF81=0x%08x\n",	vpp_read_reg(VPP_VSCA_COEF81));
	LCDC_DUMP("VPP_RCOEF=0x%08x\n",		vpp_read_reg(VPP_RCOEF));
	LCDC_DUMP("VPP_GCOEF=0x%08x\n",		vpp_read_reg(VPP_GCOEF));
	LCDC_DUMP("VPP_BCOEF=0x%08x\n",		vpp_read_reg(VPP_BCOEF));
	LCDC_DUMP("VPP_OFFSET1=0x%08x\n",	vpp_read_reg(VPP_OFFSET1));
	LCDC_DUMP("VPP_OFFSET2=0x%08x\n",	vpp_read_reg(VPP_OFFSET2));
	LCDC_DUMP("VPP_OFFSET3=0x%08x\n",	vpp_read_reg(VPP_OFFSET3));
	LCDC_DUMP("VPP_INT_MASK=0x%08x\n",	vpp_read_reg(VPP_INT_MASK));
	LCDC_DUMP("VPP_INT_STATUS=0x%08x\n",	vpp_read_reg(VPP_INT_STATUS));
	LCDC_DUMP("VPP_ACC=0x%08x\n",		vpp_read_reg(VPP_ACC));
	LCDC_DUMP("VPP_FULL_THRESH=0x%08x\n",	vpp_read_reg(VPP_FULL_THRESH));
	LCDC_DUMP("VPP_COLOR_HS_CTRL=0x%08x\n",
		vpp_read_reg(VPP_COLOR_HS_CTRL));
	LCDC_DUMP("VPP_COLOR_BC_CTRL=0x%08x\n",
		vpp_read_reg(VPP_COLOR_BC_CTRL));
	LCDC_DUMP("VPP_YBASE_BOT=0x%08x\n",	vpp_read_reg(VPP_YBASE_BOT));
	LCDC_DUMP("VPP_UBASE_BOT=0x%08x\n",	vpp_read_reg(VPP_UBASE_BOT));
	LCDC_DUMP("VPP_VBASE_BOT=0x%08x\n",	vpp_read_reg(VPP_VBASE_BOT));
	LCDC_DUMP("VPP_DESBASE_BOT=0x%08x\n",	vpp_read_reg(VPP_DESBASE_BOT));
}


/* Copy the data in source buffer to the external buffer */
static void vpp_get_src_info(unsigned int *width,
	unsigned int *height, int *format)
{
	u32 reg_ctrl;
	u32 reg_width;
	u32 reg_height;

	reg_ctrl = vpp_read_reg(VPP_CTRL);
	reg_width = vpp_read_reg(VPP_WIDTH);
	reg_height = vpp_read_reg(VPP_HEIGHT);

	*width = reg_width & VPP_SRC_WIDTH_MASK;
	*height = reg_height & VPP_SRC_HEIGHT_MASK;

	if (reg_ctrl & VPP_CTRL_PIXEL_FORMAT) {
		*format = VPP_INFORMAT_YUV420;
	} else {
		switch ((reg_ctrl & VPP_CTRL_YUV422_FORMAT_MASK) >> 2) {
		case 0: /* YUYV */
			if (reg_ctrl & VPP_CTRL_ENDIAN_MODE) /* big endian */
				*format = VPP_INFORMAT_Y0UY1V;
			else
				*format = VPP_INFORMAT_Y1UY0V;
			break;
		case 1: /* YVYU */
			if (reg_ctrl & VPP_CTRL_ENDIAN_MODE) /* big endian */
				*format = VPP_INFORMAT_Y0VY1U;
			else
				*format = VPP_INFORMAT_Y1VY0U;
			break;
		case 2: /* UYVY */
			if (reg_ctrl & VPP_CTRL_ENDIAN_MODE) /* big endian */
				*format = VPP_INFORMAT_UY0VY1;
			else
				*format = VPP_INFORMAT_UY1VY0;
			break;
		case 3: /* VYUY */
			if (reg_ctrl & VPP_CTRL_ENDIAN_MODE) /* big endian */
				*format = VPP_INFORMAT_VY0UY1;
			else
				*format = VPP_INFORMAT_VY1UY0;
			break;
		}
	}
}

static bool vpp_get_src_buf(unsigned char *src)
{
	unsigned char *addr;
	int j;

	u32 reg_ctrl;
	u32 reg_stride0;
	u32 reg_stride1;
	u32 reg_width;
	u32 reg_height;

	reg_ctrl = vpp_read_reg(VPP_CTRL);
	reg_stride0 = vpp_read_reg(VPP_STRIDE0);
	reg_stride1 = vpp_read_reg(VPP_STRIDE1);
	reg_width = vpp_read_reg(VPP_WIDTH);
	reg_height = vpp_read_reg(VPP_HEIGHT);

	if (reg_ctrl & VPP_CTRL_DEST)
		return false;

	if (reg_ctrl & VPP_CTRL_PIXEL_FORMAT) {
		u32 reg_ybase;
		u32 reg_ubase;
		u32 reg_vbase;
		reg_ybase = vpp_read_reg(VPP_YBASE);
		reg_ubase = vpp_read_reg(VPP_UBASE);
		reg_vbase = vpp_read_reg(VPP_VBASE);

		addr = (unsigned char *)reg_ybase;
		for (j = 0; j < (reg_height & VPP_SRC_HEIGHT_MASK); j++) {
			memcpy(src, addr, reg_width & VPP_SRC_WIDTH_MASK);
			src += reg_width & VPP_SRC_WIDTH_MASK;
			addr += reg_stride0 & VPP_Y_STRIDE_MASK;
		}

		addr = (unsigned char *)reg_ubase;
		for (j = 0; j < (reg_height & VPP_SRC_HEIGHT_MASK) / 2; j++) {
			memcpy(src, addr, (reg_width & VPP_SRC_WIDTH_MASK) / 2);
			src += (reg_width & VPP_SRC_WIDTH_MASK) / 2;
			addr += (reg_stride0 & VPP_U_STRIDE_MASK) >> 16;
		}

		addr = (unsigned char *)reg_vbase;
		for (j = 0; j < (reg_height & VPP_SRC_HEIGHT_MASK) / 2; j++) {
			memcpy(src, addr, (reg_width & VPP_SRC_WIDTH_MASK) / 2);
			src += (reg_width & VPP_SRC_WIDTH_MASK) / 2;
			addr += reg_stride1 & VPP_V_STRIDE_MASK;
		}
	} else {
		u32 reg_ybase;
		reg_ybase = vpp_read_reg(VPP_YBASE);

		addr = (unsigned char *)reg_ybase;
		for (j = 0; j < (reg_height & VPP_SRC_HEIGHT_MASK); j++) {
			memcpy(src, addr, (reg_width & VPP_SRC_WIDTH_MASK) * 2);
			src += (reg_width & VPP_SRC_WIDTH_MASK) * 2;
			addr += reg_stride0 & VPP_Y_STRIDE_MASK;
		}
	}

	return true;
}

static void vpp_get_dst_info(unsigned int *width,
	unsigned int *height, int *format)
{
	u32 reg_ctrl;
	u32 reg_width;
	u32 reg_height;

	reg_ctrl = vpp_read_reg(VPP_CTRL);
	reg_width = vpp_read_reg(VPP_WIDTH);
	reg_height = vpp_read_reg(VPP_HEIGHT);

	*width  = (reg_width & VPP_DES_WIDTH_MASK) >> 16;
	*height = (reg_height & VPP_DES_WIDTH_MASK) >> 16;

	if ((reg_ctrl & VPP_CTRL_OUT_FORMAT_MASK) >> 8 == 0)
		*format = VPP_OUTFORMAT_RGB565;
	else if ((reg_ctrl & VPP_CTRL_OUT_FORMAT_MASK) >> 8 == 1)
		*format = VPP_OUTFORMAT_RGB666;
	else if ((reg_ctrl & VPP_CTRL_OUT_FORMAT_MASK) >> 8 == 2)
		*format = VPP_OUTFORMAT_RGB888;
	else if ((reg_ctrl & VPP_CTRL_OUT_FORMAT_MASK) >> 8 == 3) {
		switch ((reg_ctrl & VPP_CTRL_OUT_YUV422_FORMAT_MASK) >> 4) {
		case 0: /* YUYV */
			if (reg_ctrl & VPP_CTRL_OUT_ENDIAN_MODE)
				*format = VPP_OUTFORMAT_Y0UY1V;
			else
				*format = VPP_OUTFORMAT_Y1UY0V;
			break;
		case 1: /* YVYU */
			if (reg_ctrl & VPP_CTRL_OUT_ENDIAN_MODE)
				*format = VPP_OUTFORMAT_Y0VY1U;
			else
				*format = VPP_OUTFORMAT_Y1VY0U;
			break;
		case 2: /* UYVY */
			if (reg_ctrl & VPP_CTRL_OUT_ENDIAN_MODE)
				*format = VPP_OUTFORMAT_UY0VY1;
			else
				*format = VPP_OUTFORMAT_UY1VY0;
			break;
		case 3: /* VYUY */
			if (reg_ctrl & VPP_CTRL_OUT_ENDIAN_MODE)
				*format = VPP_OUTFORMAT_VY0UY1;
			else
				*format = VPP_OUTFORMAT_VY1UY0;
			break;
		}
	}
}

/* Copy the data in dest buffer to the external buffer */
static bool vpp_get_dst_buf(unsigned char *dst)
{
	unsigned char *addr;
	int j;

	u32 reg_ctrl;
	u32 reg_dstbase;
	u32 reg_stride1;
	u32 reg_width;
	u32 reg_height;

	reg_ctrl = vpp_read_reg(VPP_CTRL);
	reg_stride1 = vpp_read_reg(VPP_STRIDE1);
	reg_width = vpp_read_reg(VPP_WIDTH);
	reg_height = vpp_read_reg(VPP_HEIGHT);

	if (reg_ctrl & VPP_CTRL_DEST) /* pass through mode */
		return false;

	reg_dstbase = vpp_read_reg(VPP_DESBASE);

	addr = (unsigned char *)reg_dstbase;
	for (j = 0; j < (reg_height & VPP_DES_HEIGHT_MASK) >> 16; j++) {
		memcpy(dst, addr, (reg_width & VPP_DES_WIDTH_MASK) >> 16);
		dst += (reg_width & VPP_DES_WIDTH_MASK) >> 16;
		addr += (reg_stride1 & VPP_DES_STRIDE_MASK) >> 16;
	}

	return true;
}


static void vpp_update_coeff2(u32 *filter_coef)
{
	int i;
	u32 offset;
	u32 reg_hsca_coef = 0x0;
	u32 reg_vsca_coef = 0x0;

	LCDC_ENTRY("%s\n", __func__);
	offset = VPP_HSCA_COEF00;

	for (i = 0; i < 54; i += 2) {
		reg_hsca_coef = VPP_HC_HSCA_COEF00(filter_coef[i]) |
				VPP_HC_HSCA_COEF01(filter_coef[i+1]);
		vpp_write_reg(offset, reg_hsca_coef);
		offset += 4;
	}

	offset = VPP_VSCA_COEF00;

	for (i = 54; i < 90; i += 2) {
		reg_vsca_coef = VPP_VC_VSCA_COEF00(filter_coef[i]) |
				VPP_VC_VSCA_COEF01(filter_coef[i+1]);
		vpp_write_reg(offset, reg_vsca_coef);
		offset += 4;
	}
}

static void vpp_update_yuv2rgb(struct vpp_coeff *rcoef,
				struct vpp_coeff *gcoef,
				struct vpp_coeff *bcoef)
{
	u32 reg_rcoef = 0x0;
	u32 reg_gcoef = 0x0;
	u32 reg_bcoef = 0x0;

	LCDC_ENTRY("%s\n", __func__);
	reg_rcoef = VPP_COEF_C1(rcoef->ycoeff) |
		VPP_COEF_C2(rcoef->ucoeff) |
		VPP_COEF_C3(rcoef->vcoeff);
	vpp_write_reg(VPP_RCOEF, reg_rcoef);

	reg_gcoef = VPP_COEF_C1(gcoef->ycoeff) |
		VPP_COEF_C2(gcoef->ucoeff) |
		VPP_COEF_C3(gcoef->vcoeff);
	vpp_write_reg(VPP_GCOEF, reg_gcoef);

	reg_bcoef = VPP_COEF_C1(bcoef->ycoeff) |
		VPP_COEF_C2(bcoef->ucoeff) |
		VPP_COEF_C3(bcoef->vcoeff);
	vpp_write_reg(VPP_BCOEF, reg_bcoef);

	vpp_write_reg(VPP_OFFSET1, rcoef->offset);
	vpp_write_reg(VPP_OFFSET2, gcoef->offset);
	vpp_write_reg(VPP_OFFSET3, bcoef->offset);
}

static void vpp_update_format_endian(bool input_big_endian,
					bool output_big_endian)
{
	u32 reg_ctrl;

	LCDC_ENTRY("%s\n", __func__);
	reg_ctrl = vpp_read_reg(VPP_CTRL);
	if (input_big_endian)
		reg_ctrl |= VPP_CTRL_ENDIAN_MODE;
	else
		reg_ctrl &= ~VPP_CTRL_ENDIAN_MODE;

	if (output_big_endian)
		reg_ctrl |= VPP_CTRL_OUT_ENDIAN_MODE;
	else
		reg_ctrl &= ~VPP_CTRL_OUT_ENDIAN_MODE;

	vpp_write_reg(VPP_CTRL, reg_ctrl);
}

static void vpp_set_usr_mode(bool usr)
{
	LCDC_ENTRY("%s\n", __func__);
	vpp_config.usr_mode = usr;
}

static void vpp_reset(void)
{
	LCDC_ENTRY("%s\n", __func__);
}

void vdss_install_vpp_ops(struct vdss_vpp_ops *vpp_ops)
{
	LCDC_ENTRY("%s\n", __func__);
	memset(vpp_ops, 0, sizeof(*vpp_ops));

	vpp_ops->init = vpp_init;
	vpp_ops->terminate = vpp_terminate;
	vpp_ops->lock = vpp_lock;
	vpp_ops->unlock = vpp_unlock;
	vpp_ops->alloc_overlay = vpp_alloc_overlay;

	/* Set parameters which will be set only once */
	vpp_ops->set_params = vpp_set_params;
	vpp_ops->set_base = vpp_set_base;
	vpp_ops->set_size = vpp_set_size;
	vpp_ops->start = vpp_start;
	vpp_ops->stop = vpp_stop;
	vpp_ops->is_busy = vpp_is_busy;
	vpp_ops->clear_dma_interrupt = vpp_clear_dma_interrupt;
	vpp_ops->enable_dma_interrupt = vpp_enable_dma_interrupt;
	vpp_ops->disable_dma_interrupt = vpp_disable_dma_interrupt;
	vpp_ops->dma_irq_detected = vpp_dma_irq_detected;
	vpp_ops->update_format_endian = vpp_update_format_endian;

	vpp_ops->sleep = vpp_sleep;
	vpp_ops->wakeup = vpp_wakeup;
	vpp_ops->set_color_ctrl = vpp_set_color_ctrl;
	vpp_ops->set_interlace = vpp_set_interlace;
	vpp_ops->update_bright = vpp_update_bright;
	vpp_ops->update_contrast = vpp_update_contrast;
	vpp_ops->update_coeff2  = vpp_update_coeff2;
	vpp_ops->update_yuv2rgb = vpp_update_yuv2rgb;
	vpp_ops->print_register = vpp_print_register;

	/* Internal function, debug only */
	vpp_ops->get_src_info = vpp_get_src_info;
	vpp_ops->get_src_buf = vpp_get_src_buf;
	vpp_ops->get_dst_info = vpp_get_dst_info;
	vpp_ops->get_dst_buf = vpp_get_dst_buf;
	vpp_ops->set_usr_mode = vpp_set_usr_mode;
	vpp_ops->reset = vpp_reset;
}

