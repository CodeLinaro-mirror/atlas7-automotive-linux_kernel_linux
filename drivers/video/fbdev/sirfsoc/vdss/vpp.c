
#define VDSS_SUBSYS_NAME "VPP"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/of.h>

#include <video/sirfsoc_vdss.h>
#include "vdss.h"
#include "vpp.h"

#define NUM_VPP 2

static void vpp_print_regs(u32 index);

static struct {
	struct platform_device *pdev;
	void __iomem    *base;

	int irq;
	struct clk	*clk;
	bool is_atlas7;
} vpp[NUM_VPP];

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

static const u32 rgb_offsets[] = {
	0xdf20,
	0x8760,
	0x114a0,
};

static unsigned int vpp_read_reg(u32 index, unsigned int offset)
{
	return readl(vpp[index].base + offset);
}

static void vpp_write_reg(u32 index, unsigned int offset, unsigned int value)
{
	writel(value, vpp[index].base + offset);
}

static void __vpp_setup(u32 index)
{
	u32 offset, val;
	int i;

	vpp_write_reg(index, VPP_FULL_THRESH, VPP_FIFO_FULL_THRESH(0x8));

	offset = VPP_HSCA_COEF00;
	for (i = 0; i < ARRAY_SIZE(tap_filter_coeff); i++) {
		vpp_write_reg(index, offset, tap_filter_coeff[i]);
		offset += 4;
	}

	offset = VPP_RCOEF;
	for (i = 0; i < ARRAY_SIZE(rgb_yuv_coeff); i += 3) {
		val = rgb_yuv_coeff[i] |
			(rgb_yuv_coeff[i+1] << 10) |
			 (rgb_yuv_coeff[i+2] << 20);
		vpp_write_reg(index, offset, val);
		offset += 4;
	}

	vpp_write_reg(index, VPP_OFFSET1, rgb_offsets[0]);
	vpp_write_reg(index, VPP_OFFSET2, rgb_offsets[1]);
	vpp_write_reg(index, VPP_OFFSET3, rgb_offsets[2]);

	val = VPP_COLOR_B_CTRL(0x0) | VPP_COLOR_C_CTRL(0x80);
	vpp_write_reg(index, VPP_COLOR_BC_CTRL, val);

	val = VPP_COLOR_UC_CTRL(0x100) | VPP_COLOR_VC_CTRL(0x0);
	vpp_write_reg(index, VPP_COLOR_HS_CTRL, val);
}

static void __vpp_set_rect(struct vdss_vpp_params *params)
{
	struct vdss_rect *src_rect = &params->src_rect;
	struct vdss_rect *dst_rect = &params->dst_rect;
	u32 index = params->index;
	u32 src_width = src_rect->right - src_rect->left + 1;
	u32 src_height = src_rect->bottom - src_rect->top + 1;
	u32 dst_width = dst_rect->right - dst_rect->left + 1;
	u32 dst_height = dst_rect->bottom - dst_rect->top + 1;

	vpp_write_reg(index, VPP_WIDTH, VPP_SRC_WIDTH(src_width) |
		VPP_DES_WIDTH(dst_width));
	vpp_write_reg(index, VPP_HEIGHT, VPP_SRC_HEIGHT(src_height) |
		VPP_DES_HEIGHT(dst_height));

}

static bool __vpp_set_params(struct vdss_vpp_params *params)
{
	u32 reg_ctrl = 0;
	u32 reg_stride0 = 0, reg_stride1 = 0;
	struct vdss_vpp_interlace *interlace = &params->interlace;
	u32 reg_thresh;
	u32 index = params->index;

	switch (params->src_fmt) {
	case VDSS_PIXELFORMAT_YV12:
	case VDSS_PIXELFORMAT_I420:
	/* NV12, NV21, hw required UV stride right shift 1*/
	case VDSS_PIXELFORMAT_NV12:
	case VDSS_PIXELFORMAT_NV21:
		reg_ctrl |= VPP_CTRL_YUV420_FORMAT;
		reg_stride0 |= VPP_Y_STRIDE(params->src_hor_stride);
		reg_stride0 |= VPP_U_STRIDE(params->src_hor_stride / 2);
		reg_stride1 |= VPP_V_STRIDE(params->src_hor_stride / 2);
		break;
	case VDSS_PIXELFORMAT_IMC4:
	case VDSS_PIXELFORMAT_IMC3:
	case VDSS_PIXELFORMAT_IMC2:
	case VDSS_PIXELFORMAT_IMC1:
		reg_ctrl |= VPP_CTRL_YUV420_FORMAT;
		reg_stride0 |= VPP_Y_STRIDE(params->src_hor_stride);
		reg_stride0 |= VPP_U_STRIDE(params->src_hor_stride);
		reg_stride1 |= VPP_V_STRIDE(params->src_hor_stride);
		break;
	case VDSS_PIXELFORMAT_UYVY:
		reg_ctrl &= ~VPP_CTRL_YUV420_FORMAT;
		reg_ctrl &= ~VPP_CTRL_ENDIAN_MODE;
		reg_ctrl |= VPP_CTRL_YUV422_FORMAT(VPP_YUV422_FORMAT_YVYU);
		reg_stride0 |= VPP_Y_STRIDE(params->src_hor_stride * 2);
		break;
	case VDSS_PIXELFORMAT_UYNV:
		reg_ctrl &= ~VPP_CTRL_YUV420_FORMAT;
		reg_ctrl &= ~VPP_CTRL_ENDIAN_MODE;
		reg_ctrl |= VPP_CTRL_YUV422_FORMAT(VPP_YUV422_FORMAT_UYVY);
		reg_stride0 |= VPP_Y_STRIDE(params->src_hor_stride * 2);
		break;
	case VDSS_PIXELFORMAT_YUY2:
	case VDSS_PIXELFORMAT_YUYV:
	case VDSS_PIXELFORMAT_YUNV:
		reg_ctrl &= ~VPP_CTRL_YUV420_FORMAT;
		reg_ctrl &= ~VPP_CTRL_ENDIAN_MODE;
		reg_ctrl |= VPP_CTRL_YUV422_FORMAT(VPP_YUV422_FORMAT_UYVY);
		reg_stride0 |= VPP_Y_STRIDE(params->src_hor_stride * 2);
		break;
	case VDSS_PIXELFORMAT_YVYU:
		reg_ctrl &= ~VPP_CTRL_YUV420_FORMAT;
		reg_ctrl &= ~VPP_CTRL_ENDIAN_MODE;
		reg_ctrl |= VPP_CTRL_YUV422_FORMAT(VPP_YUV422_FORMAT_VYUY);
		reg_stride0 |= VPP_Y_STRIDE(params->src_hor_stride * 2);
		break;
	case VDSS_PIXELFORMAT_VYUY:
		reg_ctrl &= ~VPP_CTRL_YUV420_FORMAT;
		reg_ctrl &= ~VPP_CTRL_ENDIAN_MODE;
		reg_ctrl |= VPP_CTRL_YUV422_FORMAT(VPP_YUV422_FORMAT_YVYU);
		reg_stride0 |= VPP_Y_STRIDE(params->src_hor_stride * 2);
		break;
	default:
		vpp_err("%s(%d): unkonwn src format 0x%x\n",
			__func__, __LINE__, params->src_fmt);
		return false;
	}

	if (params->src_fmt == VDSS_PIXELFORMAT_NV12 ||
		params->src_fmt == VDSS_PIXELFORMAT_NV21)
		reg_ctrl |= VPP_CTRL_UV_INTERLEAVE_EN;


	if (params->src_fmt == VDSS_PIXELFORMAT_NV12) {
		if (vpp[index].is_atlas7)
			reg_ctrl |= (1 << 13);
		else {
			reg_thresh = vpp_read_reg(index, VPP_FULL_THRESH);
			reg_thresh |= VPP_UVUV_MODE;
			vpp_write_reg(index, VPP_FULL_THRESH, reg_thresh);
		}
	}

	if (interlace->interlaced) {
		if (interlace->field_offset == 0) {
			reg_stride0 = (reg_stride0 * 2) &
				(VPP_Y_STRIDE_MASK | VPP_U_STRIDE_MASK);
			reg_stride1 = (reg_stride1 * 2) & VPP_V_STRIDE_MASK;
		}

		if (interlace->out_mode == VDSS_INTERLACE) {
			reg_ctrl |= VPP_CTRL_SEQ_TYPE(VPP_SEQ_TYPE_IIIO);

			if (interlace->output_top_first)
				reg_ctrl |= VPP_CTRL_TOP_FIELD_FIRST;

			reg_ctrl |= VPP_CTRL_HW_DI_MODE(0);
		} else if (interlace->out_mode == VDSS_P_DOUBLE) {
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
		if (interlace->out_mode == VDSS_INTERLACE) {
			reg_ctrl |= VPP_CTRL_SEQ_TYPE(VPP_SEQ_TYPE_PIIO);
			reg_ctrl |= VPP_CTRL_HW_DI_MODE(0);

			if (interlace->output_top_first)
				reg_ctrl |= VPP_CTRL_TOP_FIELD_FIRST;

		} else {
			reg_ctrl |= VPP_CTRL_SEQ_TYPE(VPP_SEQ_TYPE_PIPO);
			reg_ctrl |= VPP_CTRL_HW_DI_MODE(0);
		}
	}

	reg_ctrl &= ~VPP_CTRL_DEST;
	if (params->dst_base == 0)
		reg_ctrl |= (VPP_DEST_LCD << 7);
	else
		reg_ctrl |= (VPP_DEST_MEMORY << 7);

	switch (params->dst_fmt) {
	case VDSS_PIXELFORMAT_565:
		reg_ctrl |= VPP_CTRL_OUT_FORMAT(VPP_OUT_FORMAT_RGB565);
		reg_stride1 |= VPP_DEST_STRIDE(params->dst_hor_stride * 2);
		break;
	case VDSS_PIXELFORMAT_666:
		reg_ctrl |= VPP_CTRL_OUT_FORMAT(VPP_OUT_FORMAT_RGB666);
		reg_stride1 |= VPP_DEST_STRIDE(params->dst_hor_stride * 4);
		break;
	case VDSS_PIXELFORMAT_BGRX_8880:
	case VDSS_PIXELFORMAT_RGBX_8880:
		reg_ctrl |= VPP_CTRL_OUT_FORMAT(VPP_OUT_FORMAT_RGB888);
		reg_stride1 |= VPP_DEST_STRIDE(params->dst_hor_stride * 4);
		break;
	case VDSS_PIXELFORMAT_YUYV:
		reg_ctrl |= VPP_CTRL_OUT_FORMAT(VPP_OUT_FORMAT_YUV422);
		reg_ctrl |= VPP_CTRL_OUT_YUV422_FORMAT(VPP_YUV422_FORMAT_YUYV);
		reg_stride1 |= VPP_DEST_STRIDE(params->dst_hor_stride * 2);
		break;
	case VDSS_PIXELFORMAT_YVYU:
		reg_ctrl |= VPP_CTRL_OUT_FORMAT(VPP_OUT_FORMAT_YUV422);
		reg_ctrl |= VPP_CTRL_OUT_YUV422_FORMAT(VPP_YUV422_FORMAT_YVYU);
		reg_stride1 |= VPP_DEST_STRIDE(params->dst_hor_stride * 2);
		break;
	case VDSS_PIXELFORMAT_UYVY:
		reg_ctrl |= VPP_CTRL_OUT_FORMAT(VPP_OUT_FORMAT_YUV422);
		reg_ctrl |= VPP_CTRL_OUT_YUV422_FORMAT(VPP_YUV422_FORMAT_UYVY);
		reg_stride1 |= VPP_DEST_STRIDE(params->dst_hor_stride * 2);
		break;
	case VDSS_PIXELFORMAT_VYUY:
		reg_ctrl |= VPP_CTRL_OUT_FORMAT(VPP_OUT_FORMAT_YUV422);
		reg_ctrl |= VPP_CTRL_OUT_YUV422_FORMAT(VPP_YUV422_FORMAT_VYUY);
		reg_stride1 |= VPP_DEST_STRIDE(params->dst_hor_stride * 2);
		break;
	default:
		vpp_err("%s(%d): unknown dst format 0x%x\n",
			__func__, __LINE__, params->dst_fmt);
		return false;
	}

	vpp_write_reg(index, VPP_CTRL, reg_ctrl);
	vpp_write_reg(index, VPP_STRIDE0, reg_stride0);
	vpp_write_reg(index, VPP_STRIDE1, reg_stride1);

	if (params->dst_fmt == VDSS_PIXELFORMAT_RGBX_8880) {
		vpp_write_reg(index, VPP_BCOEF, rgb_yuv_coeff[0] |
			(rgb_yuv_coeff[1] << 10) |
			(rgb_yuv_coeff[2] << 20));
		vpp_write_reg(index, VPP_RCOEF, rgb_yuv_coeff[6] |
			(rgb_yuv_coeff[7] << 10) |
			(rgb_yuv_coeff[8] << 20));

		vpp_write_reg(index, VPP_OFFSET3, rgb_offsets[0]);
		vpp_write_reg(index, VPP_OFFSET1, rgb_offsets[2]);
	} else {
		vpp_write_reg(index, VPP_BCOEF, rgb_yuv_coeff[6] |
			(rgb_yuv_coeff[7] << 10) |
			(rgb_yuv_coeff[8] << 20));
		vpp_write_reg(index, VPP_RCOEF, rgb_yuv_coeff[0] |
			(rgb_yuv_coeff[1] << 10) |
			(rgb_yuv_coeff[2] << 20));

		vpp_write_reg(index, VPP_OFFSET1, rgb_offsets[0]);
		vpp_write_reg(index, VPP_OFFSET3, rgb_offsets[2]);
	}

	return true;
}

static bool __vpp_start(u32 index)
{
	u32 reg_ctrl = vpp_read_reg(index, VPP_CTRL);

	reg_ctrl |= VPP_CTRL_START;
	vpp_write_reg(index, VPP_CTRL, reg_ctrl);

	return true;
}

static bool __vpp_set_srcbase(struct vdss_vpp_params *params)
{
	u32 ybase = 0, ubase = 0, vbase = 0;
	u32 ybase_bot = 0, ubase_bot = 0, vbase_bot = 0;
	u32 yoffset, uoffset, voffset;
	struct vdss_vpp_interlace *interlace = &params->interlace;
	u32 index = params->index;

	yoffset = params->src_hor_stride * params->src_rect.top +
		params->src_rect.left;
	if (params->src_fmt == VDSS_PIXELFORMAT_YV12 ||
		params->src_fmt == VDSS_PIXELFORMAT_I420) {
		uoffset = (params->src_hor_stride / 2) *
			(params->src_rect.top / 2) + params->src_rect.left / 2;
		voffset = uoffset;
	} else if (params->src_fmt == VDSS_PIXELFORMAT_IMC1 ||
		params->src_fmt == VDSS_PIXELFORMAT_IMC3 ||
		params->src_fmt == VDSS_PIXELFORMAT_IMC2 ||
		params->src_fmt == VDSS_PIXELFORMAT_IMC4) {
		uoffset = params->src_hor_stride * (params->src_rect.top / 2) +
			params->src_rect.left / 2;
		voffset = uoffset;
	} else if (params->src_fmt == VDSS_PIXELFORMAT_NV12 ||
		params->src_fmt == VDSS_PIXELFORMAT_NV21) {
		uoffset = params->src_hor_stride * (params->src_rect.top / 2) +
			params->src_rect.left;
		voffset = uoffset;
	} else
		voffset = uoffset = 0;

	switch (params->src_fmt) {
	case VDSS_PIXELFORMAT_YV12:
		ybase = params->src_base + yoffset;
		ubase = params->src_base + params->src_hor_stride *
			params->src_ver_stride * 5 / 4 + uoffset;
		vbase = params->src_base + params->src_hor_stride *
			params->src_ver_stride + voffset;
		break;
	case VDSS_PIXELFORMAT_I420:
		ybase = params->src_base + yoffset;
		ubase = params->src_base + params->src_hor_stride *
			params->src_ver_stride + uoffset;
		vbase = params->src_base + params->src_hor_stride *
			params->src_ver_stride * 5 / 4 + voffset;
		break;
	case VDSS_PIXELFORMAT_IMC1:
	case VDSS_PIXELFORMAT_IMC3:
		ybase = params->src_base + yoffset;
		ubase = params->src_base + params->src_hor_stride *
			params->src_ver_stride * 3 / 2 + uoffset;
		vbase = params->src_base + params->src_hor_stride *
			params->src_ver_stride + voffset;
		break;
	case VDSS_PIXELFORMAT_IMC2:
	case VDSS_PIXELFORMAT_IMC4:
		ybase = params->src_base + yoffset;
		ubase = params->src_base + params->src_hor_stride *
			params->src_ver_stride + uoffset;
		vbase = params->src_base + params->src_hor_stride *
			params->src_ver_stride + voffset +
			params->src_hor_stride / 2;
		break;
	case VDSS_PIXELFORMAT_NV12:
	case VDSS_PIXELFORMAT_NV21:
		ybase = params->src_base + yoffset;
		/*
		 * According to spec, if the input format is semi-planar YUV420,
		 * this value should be divided by 2 as it should be.
		 */
		ubase = (params->src_base + params->src_hor_stride *
			params->src_ver_stride + uoffset) >> 1;
		vbase = ubase;
		break;
	case VDSS_PIXELFORMAT_UYVY:
	case VDSS_PIXELFORMAT_UYNV:
	case VDSS_PIXELFORMAT_YUY2:
	case VDSS_PIXELFORMAT_YUYV:
	case VDSS_PIXELFORMAT_YUNV:
	case VDSS_PIXELFORMAT_YVYU:
	case VDSS_PIXELFORMAT_VYUY:
		ybase = params->src_base + (2 * yoffset);
		ubase = vbase = ybase;
		break;
	default:
		vpp_err("%s(%d): unknown src format 0x%x\n",
			__func__, __LINE__, params->src_fmt);
		return false;
	}

	if (interlace->interlaced) {
		if (interlace->field_offset) {
			ybase_bot = ybase + interlace->field_offset;
			ubase_bot = ubase + interlace->field_offset;
			vbase_bot = vbase + interlace->field_offset;
		} else {
			switch (params->src_fmt) {
			case VDSS_PIXELFORMAT_YV12:
			case VDSS_PIXELFORMAT_I420:
			case VDSS_PIXELFORMAT_NV12:
			case VDSS_PIXELFORMAT_NV21:
				ybase_bot = ybase + params->src_hor_stride;
				ubase_bot = ubase + params->src_hor_stride / 2;
				vbase_bot = vbase + params->src_hor_stride / 2;
				break;
			case VDSS_PIXELFORMAT_IMC4:
			case VDSS_PIXELFORMAT_IMC3:
			case VDSS_PIXELFORMAT_IMC2:
			case VDSS_PIXELFORMAT_IMC1:
				ybase_bot = ybase + params->src_hor_stride;
				ubase_bot = ubase + params->src_hor_stride;
				vbase_bot = vbase + params->src_hor_stride;
				break;
			case VDSS_PIXELFORMAT_UYVY:
			case VDSS_PIXELFORMAT_UYNV:
			case VDSS_PIXELFORMAT_YUY2:
			case VDSS_PIXELFORMAT_YUYV:
			case VDSS_PIXELFORMAT_YUNV:
			case VDSS_PIXELFORMAT_YVYU:
			case VDSS_PIXELFORMAT_VYUY:
				ybase_bot = ybase + 2 * params->src_hor_stride;
				ubase_bot = vbase_bot = ybase_bot;
				break;
			default:
				vpp_err("%s(%d): unknown src format 0x%x\n",
					__func__, __LINE__, params->src_fmt);
				return false;
			}
		}

		if (interlace->input_top_first) {
			vpp_write_reg(index, VPP_YBASE, ybase);
			vpp_write_reg(index, VPP_UBASE, ubase);
			vpp_write_reg(index, VPP_VBASE, vbase);
			vpp_write_reg(index, VPP_YBASE_BOT, ybase_bot);
			vpp_write_reg(index, VPP_UBASE_BOT, ubase_bot);
			vpp_write_reg(index, VPP_VBASE_BOT, vbase_bot);
		} else {
			vpp_write_reg(index, VPP_YBASE_BOT, ybase);
			vpp_write_reg(index, VPP_UBASE_BOT, ubase);
			vpp_write_reg(index, VPP_VBASE_BOT, vbase);
			vpp_write_reg(index, VPP_YBASE, ybase_bot);
			vpp_write_reg(index, VPP_UBASE, ubase_bot);
			vpp_write_reg(index, VPP_VBASE, vbase_bot);
		}
	} else {
		vpp_write_reg(index, VPP_YBASE, ybase);
		vpp_write_reg(index, VPP_UBASE, ubase);
		vpp_write_reg(index, VPP_VBASE, vbase);
	}

	return true;
}

static void  __vpp_set_dstbase(struct vdss_vpp_params *params)
{
	u32 dstbase;
	u32 bpp;
	u32 yoffset;
	u32 index = params->index;
	struct vdss_vpp_interlace *interlace = &params->interlace;

	if (params->dst_base) {
		if (params->dst_fmt == VDSS_PIXELFORMAT_666 ||
			params->dst_fmt == VDSS_PIXELFORMAT_RGBX_8880 ||
			params->dst_fmt == VDSS_PIXELFORMAT_BGRX_8880)
			bpp = 4;
		else
			bpp = 2;

		yoffset = params->dst_hor_stride * params->dst_rect.top +
			params->dst_rect.left;
		dstbase = (params->dst_base + yoffset * bpp) & (~7);

		if (interlace->interlaced) {
			if (interlace->out_mode == VDSS_INTERLACE)
				vpp_write_reg(index, VPP_DESTBASE_BOT,
					dstbase +
					params->dst_hor_stride * bpp);
			else if (interlace->out_mode == VDSS_P_DOUBLE)
				vpp_write_reg(index, VPP_DESTBASE_BOT,
					dstbase +
					params->dst_hor_stride *
					params->dst_ver_stride * bpp);
		}
	}
}

static int __vpp_blt(struct vdss_vpp_params *params)
{
	__vpp_set_params(params);
	__vpp_set_rect(params);
	__vpp_set_srcbase(params);
	__vpp_set_dstbase(params);
	__vpp_start(params->index);

	return 0;
}

int vpp_blt(struct vdss_blt_params *blt_params)
{
	int ret = 0;

	if ((blt_params->flags & VDSS_VPP_MASK) == 0x0) {
		vpp_err("invalid blt flags\n");
		return -EINVAL;
	}

	if (blt_params->flags & VDSS_VPP_UPDATE_SRCBASE)
		ret = __vpp_set_srcbase(&blt_params->params);

	if (blt_params->flags & VDSS_VPP_BLT)
		ret = __vpp_blt(&blt_params->params);

	return ret;
}

void vpp_passthrough_setup(struct vdss_vpp_params *params)
{
	__vpp_set_params(params);
	__vpp_set_rect(params);
	__vpp_set_srcbase(params);
	__vpp_set_dstbase(params);
	vpp_print_regs(params->index);
}

static int vpp_init(u32 index)
{
	__vpp_setup(index);

	return 0;
}

static void vpp_print_regs(u32 index)
{
	vpp_dump("VPP Regs:\n");
	vpp_dump("CTRL=0x%08x\n", vpp_read_reg(index, VPP_CTRL));
	vpp_dump("YBASE=0x%08x\n", vpp_read_reg(index, VPP_YBASE));
	vpp_dump("UBASE=0x%08x\n", vpp_read_reg(index, VPP_UBASE));
	vpp_dump("VBASE=0x%08x\n", vpp_read_reg(index, VPP_VBASE));
	vpp_dump("DESBASE=0x%08x\n", vpp_read_reg(index, VPP_DESBASE));
	vpp_dump("WIDTH =0x%08x\n", vpp_read_reg(index, VPP_WIDTH));
	vpp_dump("HEIGHT=0x%08x\n", vpp_read_reg(index, VPP_HEIGHT));
	vpp_dump("STRIDE0=0x%08x\n", vpp_read_reg(index, VPP_STRIDE0));
	vpp_dump("STRIDE1=0x%08x\n", vpp_read_reg(index, VPP_STRIDE1));
	vpp_dump("HSCA_COEF00=0x%08x\n", vpp_read_reg(index, VPP_HSCA_COEF00));
	vpp_dump("HSCA_COEF01=0x%08x\n", vpp_read_reg(index, VPP_HSCA_COEF01));
	vpp_dump("HSCA_COEF02=0x%08x\n", vpp_read_reg(index, VPP_HSCA_COEF02));
	vpp_dump("HSCA_COEF10=0x%08x\n", vpp_read_reg(index, VPP_HSCA_COEF10));
	vpp_dump("HSCA_COEF11=0x%08x\n", vpp_read_reg(index, VPP_HSCA_COEF11));
	vpp_dump("HSCA_COEF12=0x%08x\n", vpp_read_reg(index, VPP_HSCA_COEF12));
	vpp_dump("HSCA_COEF20=0x%08x\n", vpp_read_reg(index, VPP_HSCA_COEF20));
	vpp_dump("HSCA_COEF21=0x%08x\n", vpp_read_reg(index, VPP_HSCA_COEF21));
	vpp_dump("HSCA_COEF22=0x%08x\n", vpp_read_reg(index, VPP_HSCA_COEF22));
	vpp_dump("HSCA_COEF30=0x%08x\n", vpp_read_reg(index, VPP_HSCA_COEF30));
	vpp_dump("HSCA_COEF31=0x%08x\n", vpp_read_reg(index, VPP_HSCA_COEF31));
	vpp_dump("HSCA_COEF32=0x%08x\n", vpp_read_reg(index, VPP_HSCA_COEF32));
	vpp_dump("HSCA_COEF40=0x%08x\n", vpp_read_reg(index, VPP_HSCA_COEF40));
	vpp_dump("HSCA_COEF41=0x%08x\n", vpp_read_reg(index, VPP_HSCA_COEF41));
	vpp_dump("HSCA_COEF42=0x%08x\n", vpp_read_reg(index, VPP_HSCA_COEF42));
	vpp_dump("HSCA_COEF50=0x%08x\n", vpp_read_reg(index, VPP_HSCA_COEF50));
	vpp_dump("HSCA_COEF51=0x%08x\n", vpp_read_reg(index, VPP_HSCA_COEF51));
	vpp_dump("HSCA_COEF52=0x%08x\n", vpp_read_reg(index, VPP_HSCA_COEF52));
	vpp_dump("HSCA_COEF60=0x%08x\n", vpp_read_reg(index, VPP_HSCA_COEF60));
	vpp_dump("HSCA_COEF61=0x%08x\n", vpp_read_reg(index, VPP_HSCA_COEF61));
	vpp_dump("HSCA_COEF62=0x%08x\n", vpp_read_reg(index, VPP_HSCA_COEF62));
	vpp_dump("HSCA_COEF70=0x%08x\n", vpp_read_reg(index, VPP_HSCA_COEF70));
	vpp_dump("HSCA_COEF71=0x%08x\n", vpp_read_reg(index, VPP_HSCA_COEF71));
	vpp_dump("HSCA_COEF72=0x%08x\n", vpp_read_reg(index, VPP_HSCA_COEF72));
	vpp_dump("HSCA_COEF80=0x%08x\n", vpp_read_reg(index, VPP_HSCA_COEF80));
	vpp_dump("HSCA_COEF81=0x%08x\n", vpp_read_reg(index, VPP_HSCA_COEF81));
	vpp_dump("HSCA_COEF82=0x%08x\n", vpp_read_reg(index, VPP_HSCA_COEF82));
	vpp_dump("VSCA_COEF00=0x%08x\n", vpp_read_reg(index, VPP_VSCA_COEF00));
	vpp_dump("VSCA_COEF01=0x%08x\n", vpp_read_reg(index, VPP_VSCA_COEF01));
	vpp_dump("VSCA_COEF10=0x%08x\n", vpp_read_reg(index, VPP_VSCA_COEF10));
	vpp_dump("VSCA_COEF11=0x%08x\n", vpp_read_reg(index, VPP_VSCA_COEF11));
	vpp_dump("VSCA_COEF20=0x%08x\n", vpp_read_reg(index, VPP_VSCA_COEF20));
	vpp_dump("VSCA_COEF21=0x%08x\n", vpp_read_reg(index, VPP_VSCA_COEF21));
	vpp_dump("VSCA_COEF30=0x%08x\n", vpp_read_reg(index, VPP_VSCA_COEF30));
	vpp_dump("VSCA_COEF31=0x%08x\n", vpp_read_reg(index, VPP_VSCA_COEF31));
	vpp_dump("VSCA_COEF40=0x%08x\n", vpp_read_reg(index, VPP_VSCA_COEF40));
	vpp_dump("VSCA_COEF41=0x%08x\n", vpp_read_reg(index, VPP_VSCA_COEF41));
	vpp_dump("VSCA_COEF50=0x%08x\n", vpp_read_reg(index, VPP_VSCA_COEF50));
	vpp_dump("VSCA_COEF51=0x%08x\n", vpp_read_reg(index, VPP_VSCA_COEF51));
	vpp_dump("VSCA_COEF60=0x%08x\n", vpp_read_reg(index, VPP_VSCA_COEF60));
	vpp_dump("VSCA_COEF61=0x%08x\n", vpp_read_reg(index, VPP_VSCA_COEF61));
	vpp_dump("VSCA_COEF70=0x%08x\n", vpp_read_reg(index, VPP_VSCA_COEF70));
	vpp_dump("VSCA_COEF71=0x%08x\n", vpp_read_reg(index, VPP_VSCA_COEF71));
	vpp_dump("VSCA_COEF80=0x%08x\n", vpp_read_reg(index, VPP_VSCA_COEF80));
	vpp_dump("VSCA_COEF81=0x%08x\n", vpp_read_reg(index, VPP_VSCA_COEF81));
	vpp_dump("RCOEF=0x%08x\n", vpp_read_reg(index, VPP_RCOEF));
	vpp_dump("GCOEF=0x%08x\n", vpp_read_reg(index, VPP_GCOEF));
	vpp_dump("BCOEF=0x%08x\n", vpp_read_reg(index, VPP_BCOEF));
	vpp_dump("OFFSET1=0x%08x\n", vpp_read_reg(index, VPP_OFFSET1));
	vpp_dump("OFFSET2=0x%08x\n", vpp_read_reg(index, VPP_OFFSET2));
	vpp_dump("OFFSET3=0x%08x\n", vpp_read_reg(index, VPP_OFFSET3));
	vpp_dump("INT_MASK=0x%08x\n", vpp_read_reg(index, VPP_INT_MASK));
	vpp_dump("INT_STATUS=0x%08x\n",	vpp_read_reg(index, VPP_INT_STATUS));
	vpp_dump("ACC=0x%08x\n", vpp_read_reg(index, VPP_ACC));
	vpp_dump("FULL_THRESH=0x%08x\n", vpp_read_reg(index, VPP_FULL_THRESH));
	vpp_dump("COLOR_HS_CTRL=0x%08x\n",
		vpp_read_reg(index, VPP_COLOR_HS_CTRL));
	vpp_dump("COLOR_BC_CTRL=0x%08x\n",
		vpp_read_reg(index, VPP_COLOR_BC_CTRL));
	vpp_dump("YBASE_BOT=0x%08x\n", vpp_read_reg(index, VPP_YBASE_BOT));
	vpp_dump("UBASE_BOT=0x%08x\n", vpp_read_reg(index, VPP_UBASE_BOT));
	vpp_dump("VBASE_BOT=0x%08x\n", vpp_read_reg(index, VPP_VBASE_BOT));
	vpp_dump("DESBASE_BOT=0x%08x\n", vpp_read_reg(index, VPP_DESTBASE_BOT));
}


static int sirfsoc_vpp_probe(struct platform_device *pdev)
{
	struct device_node *dn = pdev->dev.of_node;
	struct resource *res;
	u32 index;

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

	vpp[index].pdev = pdev;
	vpp[index].base = devm_ioremap(&pdev->dev, res->start,
		resource_size(res));

	if (!vpp[index].base) {
		VDSSERR("can't ioremap\n");
		return -ENOMEM;
	}

	if (of_device_is_compatible(pdev->dev.of_node, "sirf,atlas7-vpp"))
		vpp[index].is_atlas7 = true;

	vpp[index].irq = platform_get_irq(pdev, 0);
	if (vpp[index].irq < 0) {
		VDSSERR("platform_get_irq failed\n");
		return -ENODEV;
	}

	vpp[index].clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(vpp[index].clk)) {
		VDSSERR("Failed to get vpp clock!\n");
		return -ENODEV;
	}

	clk_prepare_enable(vpp[index].clk);

	vpp_init(index);

	return 0;

}

static const struct of_device_id vpp_of_match[] = {
	{ .compatible = "sirf,prima2-vpp", },
	{ .compatible = "sirf,atlas7-vpp", },
	{},
};

static struct platform_driver sirfsoc_vpp_driver = {
	.driver         = {
		.name   = "sirfsoc_vpp",
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
