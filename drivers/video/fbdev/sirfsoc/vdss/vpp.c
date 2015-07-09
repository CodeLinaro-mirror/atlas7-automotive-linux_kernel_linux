
#define VDSS_SUBSYS_NAME "VPP"

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

static const u32 rgb_offsets[] = {
	0xdf20,
	0x8760,
	0x114a0,
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

static bool __vpp_setup_src(struct vpp_adapter *adapter,
			struct vdss_surface *surf,
			struct vdss_vpp_interlace *interlace)
{
	u32 reg_ctrl = 0;
	u32 reg_stride0 = 0, reg_stride1 = 0;
	u32 reg_thresh;
	u32 ctrl_mask = VPP_CTRL_YUV420_FORMAT |
			VPP_CTRL_ENDIAN_MODE |
			VPP_CTRL_YUV422_FORMAT_MASK |
			VPP_CTRL_UV_INTERLEAVE_EN |
			VPP_CTRL_HW_DI_MODE_MASK |
			VPP_CTRL_SEQ_TYPE_MASK |
			VPP_CTRL_TOP_FIELD_FIRST |
			VPP_CTRL_DI_FIELD_BOT |
			VPP_CTRL_DOUBLE_FRATE |
			VPP_CTRL_UVUV_MODE;

	switch (surf->fmt) {
	case VDSS_PIXELFORMAT_YV12:
	case VDSS_PIXELFORMAT_I420:
		/* NV12, NV21, hw required UV stride right shift 1*/
	case VDSS_PIXELFORMAT_NV12:
	case VDSS_PIXELFORMAT_NV21:
		reg_ctrl |= VPP_CTRL_YUV420_FORMAT;
		reg_stride0 |= VPP_Y_STRIDE(surf->width);
		reg_stride0 |= VPP_U_STRIDE(surf->width / 2);
		reg_stride1 |= VPP_V_STRIDE(surf->width / 2);
		break;
	case VDSS_PIXELFORMAT_IMC4:
	case VDSS_PIXELFORMAT_IMC3:
	case VDSS_PIXELFORMAT_IMC2:
	case VDSS_PIXELFORMAT_IMC1:
		reg_ctrl |= VPP_CTRL_YUV420_FORMAT;
		reg_stride0 |= VPP_Y_STRIDE(surf->width);
		reg_stride0 |= VPP_U_STRIDE(surf->width);
		reg_stride1 |= VPP_V_STRIDE(surf->width);
		break;
	case VDSS_PIXELFORMAT_UYVY:
		reg_ctrl &= ~VPP_CTRL_YUV420_FORMAT;
		reg_ctrl &= ~VPP_CTRL_ENDIAN_MODE;
		reg_ctrl |= VPP_CTRL_YUV422_FORMAT(VPP_YUV422_FORMAT_YVYU);
		reg_stride0 |= VPP_Y_STRIDE(surf->width * 2);
		break;
	case VDSS_PIXELFORMAT_UYNV:
		reg_ctrl &= ~VPP_CTRL_YUV420_FORMAT;
		reg_ctrl &= ~VPP_CTRL_ENDIAN_MODE;
		reg_ctrl |= VPP_CTRL_YUV422_FORMAT(VPP_YUV422_FORMAT_UYVY);
		reg_stride0 |= VPP_Y_STRIDE(surf->width * 2);
		break;
	case VDSS_PIXELFORMAT_YUY2:
	case VDSS_PIXELFORMAT_YUYV:
	case VDSS_PIXELFORMAT_YUNV:
		reg_ctrl &= ~VPP_CTRL_YUV420_FORMAT;
		reg_ctrl &= ~VPP_CTRL_ENDIAN_MODE;
		reg_ctrl |= VPP_CTRL_YUV422_FORMAT(VPP_YUV422_FORMAT_UYVY);
		reg_stride0 |= VPP_Y_STRIDE(surf->width * 2);
		break;
	case VDSS_PIXELFORMAT_YVYU:
		reg_ctrl &= ~VPP_CTRL_YUV420_FORMAT;
		reg_ctrl &= ~VPP_CTRL_ENDIAN_MODE;
		reg_ctrl |= VPP_CTRL_YUV422_FORMAT(VPP_YUV422_FORMAT_VYUY);
		reg_stride0 |= VPP_Y_STRIDE(surf->width * 2);
		break;
	case VDSS_PIXELFORMAT_VYUY:
		reg_ctrl &= ~VPP_CTRL_YUV420_FORMAT;
		reg_ctrl &= ~VPP_CTRL_ENDIAN_MODE;
		reg_ctrl |= VPP_CTRL_YUV422_FORMAT(VPP_YUV422_FORMAT_YVYU);
		reg_stride0 |= VPP_Y_STRIDE(surf->width * 2);
		break;
	default:
		vpp_err("%s(%d): unkonwn src format 0x%x\n",
			__func__, __LINE__, surf->fmt);
		return false;
	}

	if (surf->fmt == VDSS_PIXELFORMAT_NV12 ||
	    surf->fmt == VDSS_PIXELFORMAT_NV21)
		reg_ctrl |= VPP_CTRL_UV_INTERLEAVE_EN;

	if (surf->fmt == VDSS_PIXELFORMAT_NV12) {
		if (adapter->is_atlas7)
			reg_ctrl |= VPP_CTRL_UVUV_MODE;
		else {
			reg_thresh = vpp_read_reg(adapter, VPP_FULL_THRESH);
			reg_thresh |= VPP_UVUV_MODE;
			vpp_write_reg(adapter, VPP_FULL_THRESH, reg_thresh);
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

	return true;
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

static bool __vpp_setup_dst(struct vpp_adapter *adapter,
			struct vdss_surface *surf)
{
	u32 reg_ctrl = 0;
	u32 reg_stride1 = 0;
	enum vdss_pixelformat fmt;
	u32 ctrl_mask = VPP_CTRL_OUT_FORMAT_MASK |
			VPP_CTRL_DEST |
			VPP_CTRL_OUT_YUV422_FORMAT_MASK;

	/* passthrough mode is enabled*/
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
		reg_ctrl |= VPP_CTRL_OUT_YUV422_FORMAT(VPP_YUV422_FORMAT_YUYV);
		break;
	case VDSS_PIXELFORMAT_YVYU:
		reg_ctrl |= VPP_CTRL_OUT_FORMAT(VPP_OUT_FORMAT_YUV422);
		reg_ctrl |= VPP_CTRL_OUT_YUV422_FORMAT(VPP_YUV422_FORMAT_YVYU);
		break;
	case VDSS_PIXELFORMAT_UYVY:
		reg_ctrl |= VPP_CTRL_OUT_FORMAT(VPP_OUT_FORMAT_YUV422);
		reg_ctrl |= VPP_CTRL_OUT_YUV422_FORMAT(VPP_YUV422_FORMAT_UYVY);
		break;
	case VDSS_PIXELFORMAT_VYUY:
		reg_ctrl |= VPP_CTRL_OUT_FORMAT(VPP_OUT_FORMAT_YUV422);
		reg_ctrl |= VPP_CTRL_OUT_YUV422_FORMAT(VPP_YUV422_FORMAT_VYUY);
		break;
	default:
		vpp_err("%s(%d): unknown dst format 0x%x\n",
			__func__, __LINE__, fmt);
		return false;
	}

	if (fmt == VDSS_PIXELFORMAT_RGBX_8880) {
		vpp_write_reg(adapter, VPP_BCOEF, rgb_yuv_coeff[0] |
				(rgb_yuv_coeff[1] << 10) |
				(rgb_yuv_coeff[2] << 20));
		vpp_write_reg(adapter, VPP_RCOEF, rgb_yuv_coeff[6] |
				(rgb_yuv_coeff[7] << 10) |
				(rgb_yuv_coeff[8] << 20));

		vpp_write_reg(adapter, VPP_OFFSET3, rgb_offsets[0]);
		vpp_write_reg(adapter, VPP_OFFSET1, rgb_offsets[2]);
	} else {
		vpp_write_reg(adapter, VPP_BCOEF, rgb_yuv_coeff[6] |
				(rgb_yuv_coeff[7] << 10) |
				(rgb_yuv_coeff[8] << 20));
		vpp_write_reg(adapter, VPP_RCOEF, rgb_yuv_coeff[0] |
				(rgb_yuv_coeff[1] << 10) |
				(rgb_yuv_coeff[2] << 20));

		vpp_write_reg(adapter, VPP_OFFSET1, rgb_offsets[0]);
		vpp_write_reg(adapter, VPP_OFFSET3, rgb_offsets[2]);
	}

	if (surf) {
		switch (fmt) {
		case VDSS_PIXELFORMAT_565:
			reg_stride1 |= VPP_DEST_STRIDE(surf->width * 2);
			break;
		case VDSS_PIXELFORMAT_666:
			reg_stride1 |= VPP_DEST_STRIDE(surf->width * 4);
			break;
		case VDSS_PIXELFORMAT_BGRX_8880:
		case VDSS_PIXELFORMAT_RGBX_8880:
			reg_stride1 |= VPP_DEST_STRIDE(surf->width * 4);
			break;
		case VDSS_PIXELFORMAT_YUYV:
			reg_stride1 |= VPP_DEST_STRIDE(surf->width * 2);
			break;
		case VDSS_PIXELFORMAT_YVYU:
			reg_stride1 |= VPP_DEST_STRIDE(surf->width * 2);
			break;
		case VDSS_PIXELFORMAT_UYVY:
			reg_stride1 |= VPP_DEST_STRIDE(surf->width * 2);
			break;
		case VDSS_PIXELFORMAT_VYUY:
			reg_stride1 |= VPP_DEST_STRIDE(surf->width * 2);
			break;
		default:
			vpp_err("%s(%d): unknown dst format 0x%x\n",
				__func__, __LINE__, fmt);
			return false;
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
	return true;
}

static void __vpp_blt_start(struct vpp_adapter *adapter)
{
	vpp_write_reg_with_mask(adapter, VPP_CTRL,
			VPP_CTRL_START, ~VPP_CTRL_START);
}

static bool __vpp_set_srcbase(struct vpp_adapter *adapter,
				struct vdss_surface *surf,
				u32 size,
				struct vdss_rect *rect,
				struct vdss_vpp_interlace *interlace)
{
	u32 ybase = 0, ubase = 0, vbase = 0;
	u32 ybase_bot = 0, ubase_bot = 0, vbase_bot = 0;
	u32 yoffset, uoffset, voffset;
	u32 i = 0;

	yoffset = surf->width * rect->top + rect->left;
	if (surf->fmt == VDSS_PIXELFORMAT_YV12 ||
		surf->fmt == VDSS_PIXELFORMAT_I420) {
		uoffset = (surf->width / 2) *
			(rect->top / 2) + rect->left / 2;
		voffset = uoffset;
	} else if (surf->fmt == VDSS_PIXELFORMAT_IMC1 ||
		surf->fmt == VDSS_PIXELFORMAT_IMC3 ||
		surf->fmt == VDSS_PIXELFORMAT_IMC2 ||
		surf->fmt == VDSS_PIXELFORMAT_IMC4) {
		uoffset = surf->width * (rect->top / 2) +
			rect->left / 2;
		voffset = uoffset;
	} else if (surf->fmt == VDSS_PIXELFORMAT_NV12 ||
		surf->fmt == VDSS_PIXELFORMAT_NV21) {
		uoffset = surf->width * (rect->top / 2) +
			rect->left;
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
		return false;
	}

	if (interlace->interlaced) {
		if (interlace->field_offset) {
			ybase_bot = ybase + interlace->field_offset;
			ubase_bot = ubase + interlace->field_offset;
			vbase_bot = vbase + interlace->field_offset;
		} else {
			switch (surf->fmt) {
			case VDSS_PIXELFORMAT_YV12:
			case VDSS_PIXELFORMAT_I420:
			case VDSS_PIXELFORMAT_NV12:
			case VDSS_PIXELFORMAT_NV21:
				ybase_bot = ybase + surf->width;
				ubase_bot = ubase + surf->width / 2;
				vbase_bot = vbase + surf->width / 2;
				break;
			case VDSS_PIXELFORMAT_IMC4:
			case VDSS_PIXELFORMAT_IMC3:
			case VDSS_PIXELFORMAT_IMC2:
			case VDSS_PIXELFORMAT_IMC1:
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
				return false;
			}
		}

		if (interlace->input_top_first) {
			vpp_write_reg(adapter, VPP_YBASE, ybase);
			vpp_write_reg(adapter, VPP_UBASE, ubase);
			vpp_write_reg(adapter, VPP_VBASE, vbase);
			vpp_write_reg(adapter, VPP_YBASE_BOT, ybase_bot);
			vpp_write_reg(adapter, VPP_UBASE_BOT, ubase_bot);
			vpp_write_reg(adapter, VPP_VBASE_BOT, vbase_bot);
		} else {
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
			ybase = surf[i].base + (2 * yoffset);
			if (interlace->interlaced && interlace->field_offset) {
				ybase_bot = ybase + interlace->field_offset;
				if (interlace->input_top_first) {
					vpp_write_reg(adapter,
							y_top_addr_regs[i],
							ybase);
					vpp_write_reg(adapter,
							y_bot_addr_regs[i],
							ybase_bot);
				} else {
					vpp_write_reg(adapter,
							y_top_addr_regs[i],
							ybase_bot);
					vpp_write_reg(adapter,
							y_bot_addr_regs[i],
							ybase);
				}
			} else {
				vpp_write_reg(adapter,
						y_top_addr_regs[i],
						ybase);
			}
		}
	} else {
		for (i = 1; i < 3; i++) {
			vpp_write_reg(adapter, y_top_addr_regs[i], 0);
			vpp_write_reg(adapter, y_bot_addr_regs[i], 0);
		}
	}

	return true;
}

static void  __vpp_set_dstbase(struct vpp_adapter *adapter,
			struct vdss_surface *surf,
			struct vdss_rect *rect,
			struct vdss_vpp_interlace *interlace)
{
	u32 dstbase;
	u32 bpp;
	u32 yoffset;

	if (surf && surf->base) {
		if (surf->fmt == VDSS_PIXELFORMAT_666 ||
		    surf->fmt == VDSS_PIXELFORMAT_RGBX_8880 ||
		    surf->fmt == VDSS_PIXELFORMAT_BGRX_8880)
			bpp = 4;
		else
			bpp = 2;

		yoffset = surf->width * rect->top + rect->left;
		dstbase = (surf->base + yoffset * bpp) & (~7);

		if (interlace->interlaced) {
			if (interlace->out_mode == VDSS_INTERLACE)
				vpp_write_reg(adapter, VPP_DESTBASE_BOT,
						dstbase +
						surf->width * bpp);
			else if (interlace->out_mode == VDSS_P_DOUBLE)
				vpp_write_reg(adapter, VPP_DESTBASE_BOT,
						dstbase +
						surf->width *
						surf->height * bpp);
		}
	} else {
		vpp_write_reg(adapter, VPP_DESTBASE_BOT, 0);
	}
}

static int __vpp_blt(struct vpp_adapter *adapter,
		struct vdss_vpp_blt_params *params)
{
	if (adapter == NULL || params == NULL)
		return -EINVAL;

	/* src setting */
	__vpp_setup_src(adapter, &params->src_surf, &params->interlace);
	__vpp_set_srcbase(adapter, &params->src_surf, 1,
			&params->src_rect, &params->interlace);
	__vpp_set_src_rect(adapter, &params->src_rect);
	__vpp_ibv_disable(adapter);

	/* dst setting */
	__vpp_setup_dst(adapter, &params->dst_surf);
	__vpp_set_dstbase(adapter, &params->dst_surf,
			&params->dst_rect, &params->interlace);
	__vpp_set_dst_rect(adapter, &params->dst_rect);

	/* vpp blt start */
	__vpp_blt_start(adapter);
	return 0;
}

static int __vpp_passthrough(struct vpp_adapter *adapter,
		struct vdss_vpp_passthrough_params *params)
{
	if (adapter == NULL || params == NULL)
		return -EINVAL;

	if (params->flip) {
		__vpp_set_srcbase(adapter, &params->src_surf, 1,
			&params->src_rect, &params->interlace);
	} else {
		/* src setting */
		__vpp_setup_src(adapter, &params->src_surf, &params->interlace);
		__vpp_set_srcbase(adapter, &params->src_surf, 1,
				&params->src_rect, &params->interlace);
		__vpp_set_src_rect(adapter, &params->src_rect);
		__vpp_ibv_disable(adapter);

		/* dst setting */
		__vpp_setup_dst(adapter, NULL);
		__vpp_set_dstbase(adapter, NULL,
				&params->dst_rect, &params->interlace);
		__vpp_set_dst_rect(adapter, &params->dst_rect);
	}

	return 0;
}

static int __vpp_ibv(struct vpp_adapter *adapter,
		struct vdss_vpp_ibv_params *params)
{
	if (adapter == NULL || params == NULL)
		return -EINVAL;

	/* src setting */
	__vpp_setup_src(adapter, &params->src_surf[0], &params->interlace);
	__vpp_set_srcbase(adapter, &params->src_surf[0], params->src_size,
			&params->src_rect, &params->interlace);
	__vpp_set_src_rect(adapter, &params->src_rect);
	__vpp_ibv_enable(adapter, params->src_id, params->src_size);

	/* dst setting */
	__vpp_setup_dst(adapter, NULL);
	__vpp_set_dstbase(adapter, NULL,
			&params->dst_rect, &params->interlace);
	__vpp_set_dst_rect(adapter, &params->dst_rect);

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

	/* High priority work is doing, notify the client */
	if (new_dev != in_dev && in_dev && in_dev->func)
		in_dev->func(in_dev->arg, in_dev->vpp_id, type);

	if (new_dev != adapter->cur_dev) {
		pdev = adapter->cur_dev;
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
	case VDSS_PIXELFORMAT_UYVI:
	case VDSS_PIXELFORMAT_NV12:
	case VDSS_PIXELFORMAT_NV21:
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

static int sirfsoc_vpp_probe(struct platform_device *pdev)
{
	struct device_node *dn = pdev->dev.of_node;
	struct resource *res;
	struct vpp_adapter *adapter;
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

	adapter->id = index;
	sprintf(adapter->name, "vpp%d", index);
	if (index == 0)
		vdss_debugfs_create_file("vpp0_regs", vpp0_dump_regs);
	else if (index == 1)
		vdss_debugfs_create_file("vpp1_regs", vpp1_dump_regs);

	INIT_LIST_HEAD(&adapter->devices);
	adapter->cur_dev = NULL;
	vpp_init(adapter);

	platform_set_drvdata(pdev, adapter);

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
