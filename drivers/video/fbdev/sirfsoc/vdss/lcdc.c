/*
 * CSR sirfsoc vdss core file
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc
 * group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/delay.h>

#include <video/sirfsoc_vdss.h>
#include "vdss.h"
#include "lcdc.h"

#define LCDC_LDD_NUM 27
#define LCDC_MAX_NR_ISRS		8
#define LCDC_INT_MASK_ERRS	(LCDC_INT_L0_OFLOW | \
	LCDC_INT_L0_UFLOW | LCDC_INT_L1_OFLOW | \
	LCDC_INT_L1_UFLOW | LCDC_INT_L2_OFLOW | \
	LCDC_INT_L2_UFLOW | LCDC_INT_L3_OFLOW | \
	LCDC_INT_L3_UFLOW)

#define LCDC_PADMUX_NUM 32

struct sirfsoc_lcdc_isr_data {
	sirfsoc_lcdc_isr_t	isr;
	void			*arg;
	u32			mask;
};

struct sirfsoc_lcdc_irq {
	spinlock_t irq_lock;
	u32 irq_err_mask;
	struct sirfsoc_lcdc_isr_data registered_isr[LCDC_MAX_NR_ISRS];
	u32 err_irqs;
	struct work_struct err_work;
};

struct sirfsoc_lcdc_padinfo {
	u32 *padlist;
	u32 pad_set_num;
};

static struct sirfsoc_lcdc {
	struct platform_device *pdev;
	void __iomem    *base;
	enum vdss_lcdc id;

	int irq;
	irq_handler_t user_handler;
	void *user_data;

	bool	is_atlas7;

	struct clk	*clk;
	struct sirfsoc_lcdc_irq lcdc_irq;
	struct lcdc_prop property;

	/*
	 * For padmuxs whose mapping relationship
	 * are changed.
	 */
	struct sirfsoc_lcdc_padinfo pad_hdmi;
	struct sirfsoc_lcdc_padinfo pad_panel;
	struct sirfsoc_lcdc_padinfo *cur_pad;
} lcdc[NUM_LCDC];
static u32 num_lcdc;

static u32 lcdc_padlist_hdmi[LCDC_LDD_NUM] = {
	 8,  9, 10, 11, 12, 13, 14, 15,
	16, 17, 18, 19, 20, 21, 22, 23,
	 0,  1,  2,  3,  4,  5,  6,  7,
	24, 25, 26
};

static u32 lcdc_padlist_panel[][LCDC_LDD_NUM] = {
	{
		11, 12, 14,  6,  5,  8, 15, 10,
		13,  9,  1,  7,  3,  4,  0,  2,
		16, 17, 18, 19, 20, 21, 22, 23,
		24, 25, 26
	},
	/* padmux for entry board. */
	{
		11, 24,  7, 26, 15,  6, 13,  0,
		 3, 14,  9, 10,  2,  8,  1,  4,
		16, 17, 18, 19, 20, 21, 22, 23,
		 5, 12, 25
	},
};

static void __lcdc_wait_idle(u32 lcdc_index, int layer, bool with_vpp)
{
	int timeout;

	timeout = 0;
	while (lcdc_read_reg(lcdc_index, DMA_STATUS) & (1 << layer)) {
		timeout++;
		if (timeout > 1000)
			LCDC_DEBUG("wait DMA_STATUS timeout\n");
	}

	timeout = 0;
	while (lcdc_read_reg(lcdc_index, S0_LAYER_STATUS) & (1 << layer)) {
		timeout++;
		if (timeout > 1000)
			LCDC_DEBUG("wait S0_LAYER_STATUS timeout\n");
	}
}

static void __lcdc_disable_layer(u32 lcdc_index, enum vdss_layer layer,
				bool passthrough)
{
	u32 s0_layer_sel;
	u32 lx_dma_ctrl;

	s0_layer_sel = lcdc_read_reg(lcdc_index, S0_LAYER_SEL);
	if (s0_layer_sel & S0_LS_LAYER_SEL(1 << layer)) {
		s0_layer_sel &= ~S0_LS_LAYER_SEL(1 << layer);
		lcdc_write_reg(lcdc_index, S0_LAYER_SEL, s0_layer_sel);
		if (passthrough) {
			lx_dma_ctrl = lcdc_read_reg(lcdc_index,
				reg_offset(layer, L0_DMA_CTRL));
			lx_dma_ctrl &= ~LX_VPP_PASS_MODE;
			lcdc_write_reg(lcdc_index,
				reg_offset(layer, L0_DMA_CTRL),
				lx_dma_ctrl);
			__lcdc_confirm_layer_setting(lcdc_index, layer);
		}

		__lcdc_wait_idle(lcdc_index, layer, passthrough);
	}
}

static void __lcdc_enable_layer(u32 lcdc_index, int layer, bool passthrough)
{
	u32 s0_layer_sel;
	u32 lx_dma_ctrl;

	s0_layer_sel = lcdc_read_reg(lcdc_index, S0_LAYER_SEL);
	if (!(s0_layer_sel & S0_LS_LAYER_SEL(1 << layer))) {
		__lcdc_wait_idle(lcdc_index, layer, passthrough);
		__lcdc_reset_layer_fifo(lcdc_index, layer);

		lx_dma_ctrl = lcdc_read_reg(lcdc_index,
			reg_offset(layer, L0_DMA_CTRL));
		if (passthrough)
			lx_dma_ctrl |= LX_VPP_PASS_MODE;
		else
			lx_dma_ctrl &= ~LX_VPP_PASS_MODE;

		lcdc_write_reg(lcdc_index, reg_offset(layer, L0_DMA_CTRL),
			lx_dma_ctrl);
		__lcdc_confirm_layer_setting(lcdc_index, layer);

		s0_layer_sel |= S0_LS_LAYER_SEL(1 << layer);
		lcdc_write_reg(lcdc_index, S0_LAYER_SEL, s0_layer_sel);
	}
}

static u32 __lcdc_ckey_val(enum vdss_pixelformat fmt,
	bool duplicate, u32 value)
{
	if (fmt == VDSS_PIXELFORMAT_BGRX_8880 ||
		fmt == VDSS_PIXELFORMAT_8888) {
		return value;
	} else if (fmt == VDSS_PIXELFORMAT_565) {
		u32 ckval;

		ckval = LX_CKEY_R(((value >> 11) & 0x1F) << 3) |
			LX_CKEY_G(((value >> 5) & 0x3F) << 2) |
			LX_CKEY_B((value & 0x1F) << 3);

		if (duplicate) {
			ckval |= ((ckval & LX_CKEY_R_MASK) >> 5) |
				((ckval & LX_CKEY_G_MASK) >> 6) |
				((ckval & LX_CKEY_B_MASK) >> 5);
		}
		return ckval;
	} else if (fmt >= VDSS_PIXELFORMAT_UYVY)
		return value;

	LCDC_ERR("%s(%d): unknown format 0x%x\n",
			__func__, __LINE__, fmt);
	return 0;
}

static void lcdc_layer_check_size(
	struct vdss_rect *src_rect_orig,
	struct vdss_rect *dst_rect_orig,
	struct vdss_rect *src_rect,
	struct vdss_rect *dst_rect,
	int scn_width,
	int scn_height,
	bool need_vpp)
{

	int src_width_orig, src_height_orig;
	int dst_width_orig, dst_height_orig;
	int src_width, src_height;
	int dst_width, dst_height;

	src_width_orig = src_rect_orig->right - src_rect_orig->left;
	dst_width_orig = dst_rect_orig->right - dst_rect_orig->left;
	src_height_orig = src_rect_orig->bottom - src_rect_orig->top;
	dst_height_orig = dst_rect_orig->bottom - dst_rect_orig->top;

	if (dst_rect_orig->left < 0) {
		dst_rect->left = 0;
		src_rect->left = src_rect_orig->left +
			(src_width_orig / dst_width_orig *
			(-dst_rect_orig->left));
	} else if (dst_rect_orig->left > (scn_width - 1)) {
		dst_rect->left = scn_width - 1;
		dst_rect->right = scn_width;

		src_rect->left = src_rect_orig->left;
		src_rect->right = src_rect_orig->left + 1;
	} else {
		dst_rect->left = dst_rect_orig->left;
		src_rect->left = src_rect_orig->left;
	}

	if (dst_rect_orig->right < 0) {
		dst_rect->left = -1;
		dst_rect->right = 0;
		src_rect->left = src_rect_orig->right - 1;
		src_rect->right = src_rect_orig->right;
	} else if (dst_rect_orig->right > scn_width) {
		dst_rect->right = scn_width;
		src_rect->right = src_rect_orig->right -
			(src_width_orig / dst_width_orig *
			(dst_rect_orig->right - scn_width));
	} else {
		dst_rect->right = dst_rect_orig->right;
		src_rect->right = src_rect_orig->right;
	}


	if (dst_rect_orig->top < 0) {
		dst_rect->top = 0;
		src_rect->top = src_rect_orig->top +
			(src_height_orig / dst_height_orig *
			(-dst_rect_orig->top));
	} else if (dst_rect_orig->top > (scn_height - 1)) {
		dst_rect->top = scn_height - 1;
		dst_rect->bottom = scn_height;

		src_rect->top = src_rect_orig->top;
		src_rect->bottom = src_rect_orig->top + 1;
	} else {
		dst_rect->top = dst_rect_orig->top;
		src_rect->top = src_rect_orig->top;
	}

	if (dst_rect_orig->bottom < 0) {
		dst_rect->top = -1;
		dst_rect->bottom = 0;
		src_rect->top = src_rect_orig->bottom - 1;
		src_rect->bottom = src_rect_orig->bottom;
	} else if (dst_rect_orig->bottom > scn_height) {
		dst_rect->bottom = scn_height;
		src_rect->bottom = src_rect_orig->bottom -
			(src_height_orig / dst_height_orig *
			(dst_rect_orig->bottom - scn_height));
	} else {
		dst_rect->bottom = dst_rect_orig->bottom;
		src_rect->bottom = src_rect_orig->bottom;
	}

	/* workaround LCD don't support 1 line display */
	if ((dst_rect->bottom - dst_rect->top) == 1) {
		dst_rect->top = scn_height;
		dst_rect->bottom = scn_height + 2;

		src_rect->top = 0;
		src_rect->bottom = 2;
	}

	/* workaround RGB overlay stretch, we don't support it, so
	 * show it as the smaller rect.
	 */
	if (!need_vpp) {
		src_width = src_rect->right - src_rect->left;
		src_height = src_rect->bottom - src_rect->top;
		dst_width = dst_rect->right - dst_rect->left;
		dst_height = dst_rect->bottom - dst_rect->top;

		if (src_width > dst_width)
			src_rect->right = src_rect->left + dst_width;
		else if (src_width < dst_width)
			dst_rect->right = dst_rect->left + src_width;

		if (src_height > dst_height)
			src_rect->bottom = src_rect->top + dst_height;
		else if (src_height < dst_height)
			dst_rect->bottom = dst_rect->top + src_height;
	}
}

u32 lcdc_read_intstatus(u32 lcdc_index)
{
	return lcdc_read_reg(lcdc_index, INT_CTRL_STATUS);
}

void lcdc_clear_intstatus(u32 lcdc_index, u32 mask)
{
	lcdc_write_reg(lcdc_index, INT_CTRL_STATUS, mask);
}

u32 lcdc_read_intmask(u32 lcdc_index)
{
	return lcdc_read_reg(lcdc_index, INT_MASK);
}

void lcdc_write_intmask(u32 lcdc_index, u32 mask)
{
	u32 old_mask = lcdc_read_reg(lcdc_index, INT_MASK);

	/* clear the irqstatus for newly enabled irqs */
	lcdc_clear_intstatus(lcdc_index, (mask ^ old_mask) & mask);
	lcdc_write_reg(lcdc_index, INT_MASK, mask);
}

void lcdc_layer_enable(u32 lcdc_index, enum vdss_layer layer,
	bool enable, bool passthrough)
{

	if (enable)
		__lcdc_enable_layer(lcdc_index, layer, passthrough);
	else
		__lcdc_disable_layer(lcdc_index, layer, passthrough);

}

void lcdc_layer_confirm_setting(u32 lcdc_index, enum vdss_layer layer)
{
	u32 lx_ctrl;

	lx_ctrl = lcdc_read_reg(lcdc_index, reg_offset(layer, L0_CTRL));

	lx_ctrl |= LX_CTRL_CONFIRM;
	lcdc_write_reg(lcdc_index, reg_offset(layer, L0_CTRL), lx_ctrl);
}

static void lcdc_layer_set_fmt(u32 lcdc_index, enum vdss_layer layer,
	int fmt, bool passthrough)
{
	u32 lx_ctrl = 0x0000301e;

	lx_ctrl = lcdc_read_reg(lcdc_index, reg_offset(layer, L0_CTRL));

	lx_ctrl &= ~LX_CTRL_BPP_MASK;

	if (passthrough)
		lx_ctrl |= LX_CTRL_BPP(
			__lcdc_fmt_to_hwfmt(VPP_TO_LCD_PIXELFORMAT));
	else
		lx_ctrl |= LX_CTRL_BPP(__lcdc_fmt_to_hwfmt(fmt));

	lx_ctrl |= LX_CTRL_REPLICATE;

	lx_ctrl &= ~LX_CTRL_CONFIRM;

	lcdc_write_reg(lcdc_index, reg_offset(layer, L0_CTRL), lx_ctrl);
}

static void lcdc_layer_set_alpha(u32 lcdc_index, enum vdss_layer layer, int fmt,
	bool premulti, bool source, bool global, u8 alpha)
{
	u32 lx_ctrl;

	lx_ctrl = lcdc_read_reg(lcdc_index, reg_offset(layer, L0_CTRL));
	if (global)
		lx_ctrl |= LX_CTRL_GLOBAL_ALPHA;
	else
		lx_ctrl &= ~LX_CTRL_GLOBAL_ALPHA;

	if (fmt == VDSS_PIXELFORMAT_8888) {
		if (premulti)
			lx_ctrl |= LX_CTRL_PREMULTI_ALPHA;
		else
			lx_ctrl &= ~LX_CTRL_PREMULTI_ALPHA;

		if (source)
			lx_ctrl |= LX_CTRL_SOURCE_ALPHA;
		else
			lx_ctrl &= ~LX_CTRL_SOURCE_ALPHA;
	} else {
		/* Spec require following setting for non-ARGB format */
		lx_ctrl |= LX_CTRL_PREMULTI_ALPHA;
		lx_ctrl &= ~LX_CTRL_SOURCE_ALPHA;
	}
	lx_ctrl &= ~LX_CTRL_CONFIRM;
	lcdc_write_reg(lcdc_index, reg_offset(layer, L0_CTRL), lx_ctrl);

	if (global)
		lcdc_write_reg(lcdc_index, reg_offset(layer, L0_ALPHA),
			LX_ALPHA_VAL(0xff));

}

static void lcdc_layer_set_ckey(u32 lcdc_index, enum vdss_layer layer,
	bool ckey_on, u32 ckey, bool dst_ckey_on, u32 dst_ckey, int fmt)
{
	u32 lx_ctrl;

	lx_ctrl = lcdc_read_reg(lcdc_index, reg_offset(layer, L0_CTRL));

	if (ckey_on) {
		lx_ctrl |= LX_CTRL_SRC_CKEY_EN;
		lcdc_write_reg(lcdc_index, reg_offset(layer, L0_CKEYB_SRC),
			__lcdc_ckey_val(fmt, true, ckey));
		lcdc_write_reg(lcdc_index, reg_offset(layer, L0_CKEYS_SRC),
			__lcdc_ckey_val(fmt, true, ckey));
	} else
		lx_ctrl &= ~LX_CTRL_SRC_CKEY_EN;

	if (dst_ckey_on) {
		lx_ctrl |= LX_CTRL_DST_CKEY_EN;
		lcdc_write_reg(lcdc_index, reg_offset(layer, L0_CKEYB_DST),
			__lcdc_ckey_val(fmt, true, dst_ckey));
		lcdc_write_reg(lcdc_index, reg_offset(layer, L0_CKEYS_DST),
			__lcdc_ckey_val(fmt, true, dst_ckey));
	} else
		lx_ctrl &= ~LX_CTRL_DST_CKEY_EN;

	lx_ctrl &= ~LX_CTRL_CONFIRM;
	lcdc_write_reg(lcdc_index, reg_offset(layer, L0_CTRL), lx_ctrl);
}

static void lcdc_layer_set_base(u32 lcdc_index, enum vdss_layer layer,
	struct vdss_rect *src_rect,
	int surf_width, int surf_height,
	int fmt, u32 base)
{
	unsigned int bpp = hwfmt_to_bpp[__lcdc_fmt_to_hwfmt(fmt)];
	unsigned int offset =
		(src_rect->top * surf_width + src_rect->left) * bpp;

	lcdc_write_reg(lcdc_index, reg_offset(layer, L0_BASE0), base + offset);
}

static void lcdc_layer_set_dst(u32 lcdc_index, enum vdss_layer layer,
	struct vdss_rect *dst_rect)
{
	u32 s0_hstart;
	u32 s0_vstart;

	s0_hstart = lcdc_read_reg(lcdc_index, S0_ACT_HSTART);
	s0_vstart = lcdc_read_reg(lcdc_index, S0_ACT_VSTART);

	lcdc_write_reg(lcdc_index, reg_offset(layer, L0_HSTART),
		dst_rect->left + s0_hstart);
	lcdc_write_reg(lcdc_index, reg_offset(layer, L0_HEND),
		dst_rect->right + s0_hstart);

	lcdc_write_reg(lcdc_index, reg_offset(layer, L0_VSTART),
		dst_rect->top + s0_vstart);
	lcdc_write_reg(lcdc_index, reg_offset(layer, L0_VEND),
		dst_rect->bottom + s0_vstart);
}

static void lcdc_layer_set_dma(u32 lcdc_index, enum vdss_layer layer,
	struct vdss_rect *src_rect,
	int surf_width, int surf_height,
	int fmt, u32 base)
{
	u32 lx_dma_ctrl = 0x0;
	u32 lx_fifo_chk = 0x0;
	unsigned int bpp = hwfmt_to_bpp[__lcdc_fmt_to_hwfmt(fmt)];
	/*Set DMA register configuration*/
	unsigned int width = src_rect->right - src_rect->left + 1;
	unsigned int height = src_rect->bottom - src_rect->top + 1;

	bool tv_mode = false;
	unsigned int dma_unit = __lcdc_dma_unit(tv_mode);
	unsigned int offset =
		(src_rect->top * surf_width + src_rect->left) * bpp;

	unsigned int xsize = (((offset & 7) + width * bpp  + dma_unit - 1) /
		dma_unit) - 1;

	unsigned int ysize, skip;

	if (tv_mode) {
		ysize = height / 2 - 1;
		skip = (surf_width * bpp) * 2 - (xsize * dma_unit);
	} else {
		ysize = height - 1;	/* in line units */
		skip = surf_width * bpp - xsize * dma_unit;
	}

	/* Set overlay surface addr */
	lcdc_write_reg(lcdc_index, reg_offset(layer, L0_BASE0), base + offset);
	if (tv_mode)
		lcdc_write_reg(lcdc_index, reg_offset(layer, L0_BASE1),
			base + offset + surf_width * bpp);

	lcdc_write_reg(lcdc_index, reg_offset(layer, L0_XSIZE), xsize);
	lcdc_write_reg(lcdc_index, reg_offset(layer, L0_YSIZE), ysize);
	lcdc_write_reg(lcdc_index, reg_offset(layer, L0_SKIP),  skip);

	if (lcdc[lcdc_index].is_atlas7)
		lx_fifo_chk = LX_LO_CHK_A7(0x1F0) | LX_MI_CHK_A7(0x100)
						| LX_REQ_SEL_A7;
	else
		lx_fifo_chk = LX_LO_CHK(0xF0) | LX_MI_CHK(0x80) | LX_REQ_SEL;
	lcdc_write_reg(lcdc_index, reg_offset(layer, L0_FIFO_CHK), lx_fifo_chk);

	lx_dma_ctrl = lcdc_read_reg(lcdc_index, reg_offset(layer, L0_DMA_CTRL));
	lx_dma_ctrl &= ~LX_SUPPRESS_QW_NUM_MASK;
	lx_dma_ctrl &= ~LX_DMA_UNIT_MASK;
	lx_dma_ctrl |= LX_SUPPRESS_QW_NUM(((xsize + 1) * dma_unit -
		width * bpp - (offset & 7)) >> 3);
	lx_dma_ctrl |= LX_DMA_UNIT((dma_unit >> 3) - 1);
	lx_dma_ctrl |= LX_DMA_MODE;
	if (tv_mode)
		lx_dma_ctrl |= LX_DMA_CHAIN_MODE;
	lcdc_write_reg(lcdc_index, reg_offset(layer, L0_DMA_CTRL), lx_dma_ctrl);
}

void lcdc_layer_set_passthrough(u32 lcdc_index, enum vdss_layer layer,
	struct sirfsoc_vdss_layer_info *info)
{
	u32 src_skip, dst_skip;

	if ((info->fmt >= VDSS_PIXELFORMAT_UYVY) &&
		(info->fmt <= VDSS_PIXELFORMAT_VYUY))
		src_skip = (info->src_rect_on.left & 3);
	else
		src_skip = (info->src_rect_on.left & 15);

	if (src_skip && (info->src_rect_on.right - info->src_rect_on.left))
		dst_skip = src_skip *
			(info->dst_rect_on.right - info->dst_rect_on.left) /
			(info->src_rect_on.right - info->src_rect_on.left);
	else
		dst_skip = 0;

	info->src_rect_on.left -= src_skip;
	info->dst_rect_on.left -= dst_skip;

	lcdc_write_reg(lcdc_index, reg_offset(layer, L0_BASE0),
		VPP_TO_LCD_BPP * dst_skip);
	lcdc_write_reg(lcdc_index, reg_offset(layer, L0_BASE1),
		VPP_TO_LCD_BPP * dst_skip);
}

static void lcdc_layer_set_size(u32 lcdc_index, enum vdss_layer layer,
	struct sirfsoc_vdss_layer_info *info, int scn_width, int scn_height)
{
	lcdc_layer_check_size(&info->src_rect, &info->dst_rect,
		&info->src_rect_on, &info->dst_rect_on,
		scn_width, scn_height, info->passthrough);

	lcdc_layer_set_dst(lcdc_index, layer, &info->dst_rect_on);

	if (info->passthrough)
		lcdc_layer_set_passthrough(lcdc_index, layer, info);
	else
		lcdc_layer_set_dma(lcdc_index, layer, &info->src_rect_on,
			info->surf_width, info->surf_height,
			info->fmt, info->base);
}

void lcdc_layer_setup(u32 lcdc_index, enum vdss_layer layer,
	struct sirfsoc_vdss_layer_info *info,
	struct sirfsoc_video_timings *timings)
{
	lcdc_layer_set_fmt(lcdc_index, layer, info->fmt, info->passthrough);
	lcdc_layer_set_size(lcdc_index, layer, info, timings->xres,
		timings->yres);

	lcdc_layer_set_ckey(lcdc_index, layer, info->ckey_on, info->ckey,
		info->dst_ckey_on, info->dst_ckey, info->fmt);

	lcdc_layer_set_alpha(lcdc_index, layer, info->fmt, info->pre_mult_alpha,
		info->source_alpha, info->global_alpha,
		info->alpha);

	lcdc_layer_confirm_setting(lcdc_index, layer);
}

void lcdc_flip(u32 lcdc_index, enum vdss_layer layer,
	struct sirfsoc_vdss_layer_info *info)
{
	if (info->passthrough == false)
		lcdc_layer_set_base(lcdc_index, layer, &info->src_rect,
				info->surf_width, info->surf_height, info->fmt,
				info->base);
	lcdc_layer_confirm_setting(lcdc_index, layer);
}

struct lcdc_prop *lcdc_get_prop(u32 lcdc_index)
{
	return &lcdc[lcdc_index].property;
}

void lcdc_screen_set_timings(u32 lcdc_index, enum vdss_screen scn_id,
	const struct sirfsoc_video_timings *timings)
{
	u32 s0_tim_ctrl = 0x0;
	u32 s0_osc_ratio = 0x0;
	u32 s0_disp_mode = 0x0;
	u32 s0_hsync_period = 0x0;
	u32 s0_hsync_width = 0x0;
	u32 s0_vsync_period = 0x0;
	u32 s0_vsync_width = 0x0;
	u32 so_act_hstart = 0x0;
	u32 so_act_hend = 0x0;
	u32 so_act_vstart = 0x0;
	u32 so_act_vend = 0x0;
	u32 scr_ctrl = 0x0;
	u32 s0_int_line = 0x0;
	u32 s0_yuv_ctrl = 0x0;
	u32 s0_tv_field = 0x0;

	bool tv_mode = false;

	s0_osc_ratio |= S0_OSC_HALF_DUTY;

	if (tv_mode) {
		s0_osc_ratio |= S0_OSC_DIV_RATIO(0x8);
	} else {
		int div_ratio =
			lcdc_clk_get_rate(lcdc_index) / timings->pixel_clock
			- 1;

		if (div_ratio < 1)
			s0_osc_ratio |= S0_OSC_DIV_RATIO(0x1);
		else
			s0_osc_ratio |= S0_OSC_DIV_RATIO(div_ratio);
	}

	if (lcdc[lcdc_index].is_atlas7 && lvdsc_is_syn_mode())
		s0_osc_ratio |= S0_LVDS_STOP_PCKL;

	lcdc_write_reg(lcdc_index, S0_OSC_RATIO, s0_osc_ratio);

	s0_tim_ctrl |= S0_TIM_PCLK_IO;
	s0_tim_ctrl |= S0_TIM_HSYNC_IO;
	s0_tim_ctrl |= S0_TIM_VSYNC_IO;

	if (timings->pclk_edge == SIRFSOC_VDSS_SIG_RISING_EDGE)
		s0_tim_ctrl |= S0_TIM_PCLK_EDGE;
	if (timings->hsync_level == SIRFSOC_VDSS_SIG_ACTIVE_LOW)
		s0_tim_ctrl |= S0_TIM_HSYNC_POLAR;
	if (timings->vsync_level == SIRFSOC_VDSS_SIG_ACTIVE_LOW)
		s0_tim_ctrl |= S0_TIM_VSYNC_POLAR;

	s0_tim_ctrl |= S0_TIM_SYNC_DLY(0);

	lcdc_write_reg(lcdc_index, S0_TIM_CTRL, s0_tim_ctrl);

	lcdc_write_reg(lcdc_index, S0_RGB_SEQ, S0_RGB_SEQ_RGB);

	s0_hsync_period = timings->xres + timings->hsw +
		timings->hfp + timings->hbp - 1;
	lcdc_write_reg(lcdc_index, S0_HSYNC_PERIOD, s0_hsync_period);

	s0_hsync_width = timings->hsw - 1;
	/* atlas7: S0_HSYNC_WIDTH value at least 1 */
	if (lcdc[lcdc_index].is_atlas7)
		if (s0_hsync_width == 0)
			s0_hsync_width = 1;

	lcdc_write_reg(lcdc_index, S0_HSYNC_WIDTH, s0_hsync_width);

	s0_vsync_period = timings->yres + timings->vsw +
		timings->vfp + timings->vbp - 1;
	lcdc_write_reg(lcdc_index, S0_VSYNC_PERIOD, s0_vsync_period);

	s0_vsync_width |= S0_VW_VSYNC_WIDTH(timings->vsw - 1);
	/* vsync width is in number of lines */
	s0_vsync_width |= S0_VSYC_WIDTH_UINT;
	lcdc_write_reg(lcdc_index, S0_VSYNC_WIDTH, s0_vsync_width);

	/* atlas7: act hstart need move more 16 pclk position */
	if (lcdc[lcdc_index].is_atlas7)
		so_act_hstart = timings->hsw + timings->hbp - 16;
	else
		so_act_hstart = timings->hsw + timings->hbp - 12;
	lcdc_write_reg(lcdc_index, S0_ACT_HSTART, so_act_hstart);
	so_act_hend = so_act_hstart + timings->xres - 1;
	lcdc_write_reg(lcdc_index, S0_ACT_HEND, so_act_hend);

	so_act_vstart = timings->vsw + timings->vbp;
	lcdc_write_reg(lcdc_index, S0_ACT_VSTART, so_act_vstart);
	so_act_vend += so_act_vstart + timings->yres - 1;
	lcdc_write_reg(lcdc_index, S0_ACT_VEND, so_act_vend);

	s0_disp_mode = S0_TOP_LAYER(3) | S0_FRAME_VALID;
	lcdc_write_reg(lcdc_index, S0_DISP_MODE, s0_disp_mode);

	lcdc_write_reg(lcdc_index, BLS_CTRL1, (timings->xres << 20) |
		(timings->yres << 9) | 64);
	lcdc_write_reg(lcdc_index, BLS_CTRL2, (15 << 4) | (0));
	lcdc_write_reg(lcdc_index, BLS_LEVEL_TB0, 0 | (2 << 8) |
		(4 << 16) | (6 << 24));
	lcdc_write_reg(lcdc_index, BLS_LEVEL_TB1, 8 | (10 << 8) |
		(12 << 16) | (14 << 24));
	lcdc_write_reg(lcdc_index, BLS_LEVEL_TB2, 16 | (18 << 8) |
		(20 << 16) | (22 << 24));
	lcdc_write_reg(lcdc_index, BLS_LEVEL_TB3, 24 | (26 << 8) |
		(28 << 16) | (30 << 24));

	/* for other once setting */
	scr_ctrl |= SCREEN0_EN;
	lcdc_write_reg(lcdc_index, SCR_CTRL, scr_ctrl);

	s0_int_line |= S0_INT_LINE_VALID;
	lcdc_write_reg(lcdc_index, S0_INT_LINE, s0_int_line);

	lcdc_write_reg(lcdc_index, S0_RGB_YUV_COEF1, 0x00428119);
	lcdc_write_reg(lcdc_index, S0_RGB_YUV_COEF2, 0x00264A70);
	lcdc_write_reg(lcdc_index, S0_RGB_YUV_COEF3, 0x00705E12);
	lcdc_write_reg(lcdc_index, S0_RGB_YUV_OFFSET, 0x00108080);

	s0_yuv_ctrl |= S0_YUV_SEQ(1); /* YVYU sequence */
	s0_yuv_ctrl |= S0_EVEN_UV;
	s0_tv_field = S0_TV_HSTART(0x339) | S0_TV_VSTART(0x106);
	if (tv_mode) {
		s0_yuv_ctrl |= S0_RGB_YUV;
		s0_tv_field |= S0_TV_F_VALID;
	}
	lcdc_write_reg(lcdc_index, S0_YUV_CTRL, s0_yuv_ctrl);
	lcdc_write_reg(lcdc_index, S0_TV_FIELD, s0_tv_field);
}

void lcdc_screen_set_data_lines(u32 lcdc_index, enum vdss_screen scn_id,
	int data_lines)
{
	u32 s0_disp_mode = 0x0;

	s0_disp_mode = lcdc_read_reg(lcdc_index, S0_DISP_MODE);

	switch (data_lines) {
	case 16:
	case 18:
		s0_disp_mode |= S0_OUT_FORMAT(LCDC_OUT_18BIT_RBG666);
		break;
	case 24:
		s0_disp_mode |= S0_OUT_FORMAT(LCDC_OUT_24BIT_RBG888);
		break;
	default:
		BUG();
		return;
	}

	s0_disp_mode |= S0_FRAME_VALID;
	lcdc_write_reg(lcdc_index, S0_DISP_MODE, s0_disp_mode);
}

void lcdc_screen_set_error_diffusion(u32 lcdc_index, enum vdss_screen scn_id,
	int data_lines, bool error_diffusion)
{
	u32 ed_mode = 0x0;
	u32 ed_perform = 0x0;
	u32 ed_start_state = 0x0;
	u32 ed_lfsr_enable = 0x0;
	u32 ed_polynomial = 0x0;
	u32 ed_lfsr_steps = 0x0;
	u32 ed_left_align = 0x0;
	u32 bypass_ed = 0x0;

	if (!error_diffusion || data_lines == 24) {
		bypass_ed = ED_BY_PASS_EN;
		lcdc_write_reg(lcdc_index, BYPASS_ED, bypass_ed);
		return;
	}

	switch (data_lines) {
	case 16:
		ed_mode = (0x3 << 6) | (0x2 << 3) | (0x3 << 0);
		ed_lfsr_steps = 0x3;
		break;
	case 18:
		ed_mode = (0x2 << 6) | (0x2 << 3) | (0x2 << 0);
		ed_lfsr_steps = 0x2;
	default:
		BUG();
		return;
	}

	lcdc_write_reg(lcdc_index, ED_MODE, ed_mode);

	ed_perform = ED_HORIZONTAL;
	lcdc_write_reg(lcdc_index, ED_PERFORM, ed_perform);

	ed_start_state = START_WITH_PREVIOUS_FRAME;
	lcdc_write_reg(lcdc_index, ED_START_STATE, ed_start_state);

	ed_lfsr_enable = ED_LFSR_EN;
	lcdc_write_reg(lcdc_index, ED_LFSR_ENABLE, ed_lfsr_enable);

	ed_polynomial = DEFAULT_POLY_COEF;
	lcdc_write_reg(lcdc_index, ED_POLYNOMIAL, ed_polynomial);

	lcdc_write_reg(lcdc_index, ED_LFSR_STEPS, ed_lfsr_steps);

	ed_left_align = LEFT_ALIGN;
	lcdc_write_reg(lcdc_index, ED_LEFTALIGN, ed_left_align);

	lcdc_write_reg(lcdc_index, BYPASS_ED, bypass_ed);
}

void lcdc_screen_set_gamma(u32 lcdc_index, enum vdss_screen scn_id,
	const u8 *gamma)
{
	int i;
	u32 *pval;
	u32 s0_disp_mode = 0x0;

	s0_disp_mode = lcdc_read_reg(lcdc_index, S0_DISP_MODE);
	s0_disp_mode &= ~S0_GAMMA_COR_EN;
	s0_disp_mode |= S0_FRAME_VALID;
	lcdc_write_reg(lcdc_index, S0_DISP_MODE, s0_disp_mode);

	pval = (u32 *)gamma;
	for (i = 0; i < 256 * 3; i += 4)
		lcdc_write_reg(lcdc_index, S0_GAMMAFIFO_R + i, pval[i>>2]);
	s0_disp_mode |= S0_GAMMA_COR_EN;
	s0_disp_mode |= S0_FRAME_VALID;
	lcdc_write_reg(lcdc_index, S0_DISP_MODE, s0_disp_mode);
}

void lcdc_screen_setup(u32 lcdc_index, enum vdss_screen scn_id,
	const struct sirfsoc_vdss_screen_info *info)
{
	u32 s0_blank = 0x0;
	u32 s0_disp_mode = 0x0;

	/* Debug purpose: to check if we set right SCN_*_VAL */
	s0_blank = S0_BLANK_VALUE(0xff0000) | S0_BLANK_VALID;
	lcdc_write_reg(lcdc_index, S0_BLANK, s0_blank);
	lcdc_write_reg(lcdc_index, S0_BACK_COLOR, info->back_color);

	s0_disp_mode = lcdc_read_reg(lcdc_index, S0_DISP_MODE);
	s0_disp_mode &= ~S0_TOP_LAYER_MASK;
	s0_disp_mode |= S0_TOP_LAYER(info->top_layer);
	s0_disp_mode |= S0_FRAME_VALID;
	lcdc_write_reg(lcdc_index, S0_DISP_MODE, s0_disp_mode);
}

static void lcdc_output_configure_pins(u32 lcdc_index,
	bool hdmi, int data_lines)
{
	/* atlas7: can't use default value any more */
	if (!lcdc[lcdc_index].is_atlas7)
		return;

	if (hdmi)
		lcdc[lcdc_index].cur_pad = &lcdc[lcdc_index].pad_hdmi;
	else if (data_lines == 16)
		lcdc[lcdc_index].cur_pad = &lcdc[lcdc_index].pad_panel;
	else
		lcdc[lcdc_index].cur_pad = NULL;

	if (lcdc[lcdc_index].cur_pad) {
		u32 *plist = NULL;
		u32 pad_num = 0;
		u32 i;

		plist = lcdc[lcdc_index].cur_pad->padlist;
		pad_num = lcdc[lcdc_index].cur_pad->pad_set_num;
		for (i = 0; i < pad_num; i++)
			lcdc_write_reg(lcdc_index, PADMUX_LDD_0 + i * 4,
				       1 << plist[i]);
	}
}

static void lcdc_dump_regs(struct seq_file *s, u32 lcdc_index)
{
#define LCDC_DUMP(fmt, ...) seq_printf(s, fmt, ##__VA_ARGS__)

	LCDC_DUMP("LCD registers:\n");
	LCDC_DUMP("S0_HSYNC_PERIOD=0x%08x\n",
		lcdc_read_reg(lcdc_index, S0_HSYNC_PERIOD));
	LCDC_DUMP("S0_HSYNC_WIDTH=0x%08x\n",
		lcdc_read_reg(lcdc_index, S0_HSYNC_WIDTH));
	LCDC_DUMP("S0_VSYNC_PERIOD=0x%08x\n",
		lcdc_read_reg(lcdc_index, S0_VSYNC_PERIOD));
	LCDC_DUMP("S0_VSYNC_WIDTH=0x%08x\n",
		lcdc_read_reg(lcdc_index, S0_VSYNC_WIDTH));
	LCDC_DUMP("S0_ACT_HSTART=0x%08x\n",
		lcdc_read_reg(lcdc_index, S0_ACT_HSTART));
	LCDC_DUMP("S0_ACT_VSTART=0x%08x\n",
		lcdc_read_reg(lcdc_index, S0_ACT_VSTART));
	LCDC_DUMP("S0_ACT_HEND=0x%08x\n",
		lcdc_read_reg(lcdc_index, S0_ACT_HEND));
	LCDC_DUMP("S0_ACT_VEND=0x%08x\n",
		lcdc_read_reg(lcdc_index, S0_ACT_VEND));
	LCDC_DUMP("S0_OSC_RATIO=0x%08x\n",
		lcdc_read_reg(lcdc_index, S0_OSC_RATIO));
	LCDC_DUMP("S0_TIM_CTRL=0x%08x\n",
		lcdc_read_reg(lcdc_index, S0_TIM_CTRL));
	LCDC_DUMP("S0_TIM_STATUS=0x%08x\n",
		lcdc_read_reg(lcdc_index, S0_TIM_STATUS));
	LCDC_DUMP("S0_HCOUNT=0x%08x\n",
		lcdc_read_reg(lcdc_index, S0_HCOUNT));
	LCDC_DUMP("S0_VCOUNT=0x%08x\n",
		lcdc_read_reg(lcdc_index, S0_VCOUNT));
	LCDC_DUMP("S0_BLANK=0x%08x\n",
		lcdc_read_reg(lcdc_index, S0_BLANK));
	LCDC_DUMP("S0_BACK_COLOR=0x%08x\n",
		lcdc_read_reg(lcdc_index, S0_BACK_COLOR));
	LCDC_DUMP("S0_DISP_MODE=0x%08x\n",
		lcdc_read_reg(lcdc_index, S0_DISP_MODE));
	LCDC_DUMP("S0_LAYER_SEL=0x%08x\n",
		lcdc_read_reg(lcdc_index, S0_LAYER_SEL));
	LCDC_DUMP("S0_RGB_SEQ=0x%08x\n",
		lcdc_read_reg(lcdc_index, S0_RGB_SEQ));
	LCDC_DUMP("S0_RGB_YUV_COEF1=0x%08x\n",
		lcdc_read_reg(lcdc_index, S0_RGB_YUV_COEF1));
	LCDC_DUMP("S0_RGB_YUV_COEF2=0x%08x\n",
		lcdc_read_reg(lcdc_index, S0_RGB_YUV_COEF2));
	LCDC_DUMP("S0_RGB_YUV_COEF3=0x%08x\n",
		lcdc_read_reg(lcdc_index, S0_RGB_YUV_COEF3));
	LCDC_DUMP("S0_YUV_CTRL=0x%08x\n",
		lcdc_read_reg(lcdc_index, S0_YUV_CTRL));
	LCDC_DUMP("S0_TV_FIELD=0x%08x\n",
		lcdc_read_reg(lcdc_index, S0_TV_FIELD));
	LCDC_DUMP("S0_INT_LINE=0x%08x\n",
		lcdc_read_reg(lcdc_index, S0_INT_LINE));
	LCDC_DUMP("S0_LAYER_STATUS=0x%08x\n",
		lcdc_read_reg(lcdc_index, S0_LAYER_STATUS));
	LCDC_DUMP("DMA_STATUS=0x%08x\n",
		lcdc_read_reg(lcdc_index, DMA_STATUS));
	LCDC_DUMP("SCR_CTRL=0X%08X\n",
		lcdc_read_reg(lcdc_index, SCR_CTRL));
	LCDC_DUMP("INT_MASK=0X%08X\n",
		lcdc_read_reg(lcdc_index, INT_MASK));
	LCDC_DUMP("INT_CTRL_STATUS=0X%08X\n",
		lcdc_read_reg(lcdc_index, INT_CTRL_STATUS));
	LCDC_DUMP("WB_CTRL=0X%08X\n",
		lcdc_read_reg(lcdc_index, WB_CTRL));
	LCDC_DUMP("S0_RGB_YUV_OFFSET=0X%08X\n",
		lcdc_read_reg(lcdc_index, S0_RGB_YUV_OFFSET));
	LCDC_DUMP("S0_LAYER_SEL_SET=0X%08X\n",
		lcdc_read_reg(lcdc_index, S0_LAYER_SEL_SET));
	LCDC_DUMP("S0_LAYER_SEL_CLR=0X%08X\n",
		lcdc_read_reg(lcdc_index, S0_LAYER_SEL_CLR));
	LCDC_DUMP("S0_FRONT_INT_LINE=0X%08X\n",
		lcdc_read_reg(lcdc_index, S0_FRONT_INT_LINE));
	LCDC_DUMP("FRONT_INT_MASK=0X%08X\n",
		lcdc_read_reg(lcdc_index, FRONT_INT_MASK));
	LCDC_DUMP("FRONT_INT_CTRL_STATUS=0X%08X\n",
		lcdc_read_reg(lcdc_index, FRONT_INT_CTRL_STATUS));
	LCDC_DUMP("FRONT_INT_MASK_SET=0X%08X\n",
		lcdc_read_reg(lcdc_index, FRONT_INT_MASK_SET));
	LCDC_DUMP("FRONT_INT_MASK_CLR=0X%08X\n",
		lcdc_read_reg(lcdc_index, FRONT_INT_MASK_CLR));
	LCDC_DUMP("INT_MASK_SET=0X%08X\n",
		lcdc_read_reg(lcdc_index, INT_MASK_SET));
	LCDC_DUMP("INT_MASK_CLR=0X%08X\n",
		lcdc_read_reg(lcdc_index, INT_MASK_CLR));

	LCDC_DUMP("ED_MODE=0X%08X\n",
		lcdc_read_reg(lcdc_index, ED_MODE));
	LCDC_DUMP("ED_PERFORM=0X%08X\n",
		lcdc_read_reg(lcdc_index, ED_PERFORM));
	LCDC_DUMP("ED_START_STATE=0X%08X\n",
		lcdc_read_reg(lcdc_index, ED_START_STATE));
	LCDC_DUMP("INVERSEDATA=0X%08X\n",
		lcdc_read_reg(lcdc_index, INVERSEDATA));
	LCDC_DUMP("ED_LFSR_ENABLE=0X%08X\n",
		lcdc_read_reg(lcdc_index, ED_LFSR_ENABLE));
	LCDC_DUMP("ED_POLYNOMIAL=0X%08X\n",
		lcdc_read_reg(lcdc_index, ED_POLYNOMIAL));
	LCDC_DUMP("ED_LFSR_STEPS=0X%08X\n",
		lcdc_read_reg(lcdc_index, ED_LFSR_STEPS));
	LCDC_DUMP("ED_LEFTALIGN=0X%08X\n",
		lcdc_read_reg(lcdc_index, ED_LEFTALIGN));
	LCDC_DUMP("BYPASS_ED=0X%08X\n",
		lcdc_read_reg(lcdc_index, BYPASS_ED));

	LCDC_DUMP("PADMUX_LDD_0=0X%08X\n",
		lcdc_read_reg(lcdc_index, PADMUX_LDD_0));
	LCDC_DUMP("PADMUX_LDD_1=0X%08X\n",
		lcdc_read_reg(lcdc_index, PADMUX_LDD_1));
	LCDC_DUMP("PADMUX_LDD_2=0X%08X\n",
		lcdc_read_reg(lcdc_index, PADMUX_LDD_2));
	LCDC_DUMP("PADMUX_LDD_3=0X%08X\n",
		lcdc_read_reg(lcdc_index, PADMUX_LDD_3));
	LCDC_DUMP("PADMUX_LDD_4=0X%08X\n",
		lcdc_read_reg(lcdc_index, PADMUX_LDD_4));
	LCDC_DUMP("PADMUX_LDD_5=0X%08X\n",
		lcdc_read_reg(lcdc_index, PADMUX_LDD_5));
	LCDC_DUMP("PADMUX_LDD_6=0X%08X\n",
		lcdc_read_reg(lcdc_index, PADMUX_LDD_6));
	LCDC_DUMP("PADMUX_LDD_7=0X%08X\n",
		lcdc_read_reg(lcdc_index, PADMUX_LDD_7));
	LCDC_DUMP("PADMUX_LDD_8=0X%08X\n",
		lcdc_read_reg(lcdc_index, PADMUX_LDD_8));
	LCDC_DUMP("PADMUX_LDD_9=0X%08X\n",
		lcdc_read_reg(lcdc_index, PADMUX_LDD_9));
	LCDC_DUMP("PADMUX_LDD_10=0X%08X\n",
		lcdc_read_reg(lcdc_index, PADMUX_LDD_10));
	LCDC_DUMP("PADMUX_LDD_11=0X%08X\n",
		lcdc_read_reg(lcdc_index, PADMUX_LDD_11));
	LCDC_DUMP("PADMUX_LDD_12=0X%08X\n",
		lcdc_read_reg(lcdc_index, PADMUX_LDD_12));
	LCDC_DUMP("PADMUX_LDD_13=0X%08X\n",
		lcdc_read_reg(lcdc_index, PADMUX_LDD_13));
	LCDC_DUMP("PADMUX_LDD_14=0X%08X\n",
		lcdc_read_reg(lcdc_index, PADMUX_LDD_14));
	LCDC_DUMP("PADMUX_LDD_15=0X%08X\n",
		lcdc_read_reg(lcdc_index, PADMUX_LDD_15));
	LCDC_DUMP("PADMUX_LDD_16=0X%08X\n",
		lcdc_read_reg(lcdc_index, PADMUX_LDD_16));
	LCDC_DUMP("PADMUX_LDD_17=0X%08X\n",
		lcdc_read_reg(lcdc_index, PADMUX_LDD_17));
	LCDC_DUMP("PADMUX_LDD_18=0X%08X\n",
		lcdc_read_reg(lcdc_index, PADMUX_LDD_18));
	LCDC_DUMP("PADMUX_LDD_19=0X%08X\n",
		lcdc_read_reg(lcdc_index, PADMUX_LDD_19));
	LCDC_DUMP("PADMUX_LDD_20=0X%08X\n",
		lcdc_read_reg(lcdc_index, PADMUX_LDD_20));
	LCDC_DUMP("PADMUX_LDD_21=0X%08X\n",
		lcdc_read_reg(lcdc_index, PADMUX_LDD_21));
	LCDC_DUMP("PADMUX_LDD_22=0X%08X\n",
		lcdc_read_reg(lcdc_index, PADMUX_LDD_22));
	LCDC_DUMP("PADMUX_LDD_23=0X%08X\n",
		lcdc_read_reg(lcdc_index, PADMUX_LDD_23));
	LCDC_DUMP("PADMUX_L_DE=0X%08X\n",
		lcdc_read_reg(lcdc_index, PADMUX_L_DE));
	LCDC_DUMP("PADMUX_L_LCK=0X%08X\n",
		lcdc_read_reg(lcdc_index, PADMUX_L_LCK));
	LCDC_DUMP("PADMUX_L_FCK=0X%08X\n",
		lcdc_read_reg(lcdc_index, PADMUX_L_FCK));
	LCDC_DUMP("PADMUX_L_PCLK=0X%08X\n",
		lcdc_read_reg(lcdc_index, PADMUX_L_PCLK));
	LCDC_DUMP("PADMUX_OUT_MUX=0X%08X\n",
		lcdc_read_reg(lcdc_index, PADMUX_OUT_MUX));
	LCDC_DUMP("PADMUX_DELAY_CFG=0X%08X\n",
		lcdc_read_reg(lcdc_index, PADMUX_DELAY_CFG));

	LCDC_DUMP("STRS_CONTROL=0X%08X\n",
		lcdc_read_reg(lcdc_index, STRS_CONTROL));
	LCDC_DUMP("STRS0_VAL=0X%08X\n",
		lcdc_read_reg(lcdc_index, STRS0_VAL));
	LCDC_DUMP("STRS1_VAL=0X%08X\n",
		lcdc_read_reg(lcdc_index, STRS1_VAL));
	LCDC_DUMP("STRS2_VAL=0X%08X\n",
		lcdc_read_reg(lcdc_index, STRS2_VAL));
	LCDC_DUMP("STRS3_VAL=0X%08X\n",
		lcdc_read_reg(lcdc_index, STRS3_VAL));

	LCDC_DUMP("DMAN_ADDR=0X%08X\n",
		lcdc_read_reg(lcdc_index, DMAN_ADDR));
	LCDC_DUMP("DMAN_XLEN=0X%08X\n",
		lcdc_read_reg(lcdc_index, DMAN_XLEN));
	LCDC_DUMP("DMAN_YLEN=0X%08X\n",
		lcdc_read_reg(lcdc_index, DMAN_YLEN));
	LCDC_DUMP("DMAN_CTRL=0X%08X\n",
		lcdc_read_reg(lcdc_index, DMAN_CTRL));
	LCDC_DUMP("DMAN_WIDTH=0X%08X\n",
		lcdc_read_reg(lcdc_index, DMAN_WIDTH));
	LCDC_DUMP("DMAN_VALID=0X%08X\n",
		lcdc_read_reg(lcdc_index, DMAN_VALID));
	LCDC_DUMP("DMAN_INT=0X%08X\n",
		lcdc_read_reg(lcdc_index, DMAN_INT));
	LCDC_DUMP("DMAN_INT_EN=0X%08X\n",
		lcdc_read_reg(lcdc_index, DMAN_INT_EN));
	LCDC_DUMP("DMAN_LOOP_CTRL=0X%08X\n",
		lcdc_read_reg(lcdc_index, DMAN_LOOP_CTRL));
	LCDC_DUMP("DMAN_INT_CNT=0X%08X\n",
		lcdc_read_reg(lcdc_index, DMAN_INT_CNT));
	LCDC_DUMP("DMAN_TIMEOUT_CNT=0X%08X\n",
		lcdc_read_reg(lcdc_index, DMAN_TIMEOUT_CNT));
	LCDC_DUMP("DMAN_PAU_TIME_CNT=0X%08X\n",
		lcdc_read_reg(lcdc_index, DMAN_PAU_TIME_CNT));
	LCDC_DUMP("DMAN_CUR_TABLE_ADDR=0X%08X\n",
		lcdc_read_reg(lcdc_index, DMAN_CUR_TABLE_ADDR));
	LCDC_DUMP("DMAN_CUR_DATA_ADDR=0X%08X\n",
		lcdc_read_reg(lcdc_index, DMAN_CUR_DATA_ADDR));
	LCDC_DUMP("DMAN_MUL=0X%08X\n",
		lcdc_read_reg(lcdc_index, DMAN_MUL));
	LCDC_DUMP("DMAN_STATE0=0X%08X\n",
		lcdc_read_reg(lcdc_index, DMAN_STATE0));
	LCDC_DUMP("DMAN_STATE1=0X%08X\n",
		lcdc_read_reg(lcdc_index, DMAN_STATE1));
	LCDC_DUMP("WB_DMA_PENDING_START_ADDR=0X%08X\n",
		lcdc_read_reg(lcdc_index, WB_DMA_PENDING_START_ADDR));

	/* Lay0 register */
	LCDC_DUMP("L0_CTRL=0x%08x\n",
		lcdc_read_reg(lcdc_index, L0_CTRL));
	LCDC_DUMP("L0_HSTART=0x%08x\n",
		lcdc_read_reg(lcdc_index, L0_HSTART));
	LCDC_DUMP("L0_VSTART=0X%08X\n",
		lcdc_read_reg(lcdc_index, L0_VSTART));
	LCDC_DUMP("L0_HEND=0X%08X\n",
		lcdc_read_reg(lcdc_index, L0_HEND));
	LCDC_DUMP("L0_VEND=0x%08x\n",
		lcdc_read_reg(lcdc_index, L0_VEND));
	LCDC_DUMP("L0_BASE0=0x%08x\n",
		lcdc_read_reg(lcdc_index, L0_BASE0));
	LCDC_DUMP("L0_BASE1=0X%08X\n",
		lcdc_read_reg(lcdc_index, L0_BASE1));
	LCDC_DUMP("L0_XSIZE=0X%08X\n",
		lcdc_read_reg(lcdc_index, L0_XSIZE));
	LCDC_DUMP("L0_YSIZE=0x%08x\n",
		lcdc_read_reg(lcdc_index, L0_YSIZE));
	LCDC_DUMP("L0_SKIP=0x%08x\n",
		lcdc_read_reg(lcdc_index, L0_SKIP));
	LCDC_DUMP("L0_DMA_CTRL=0X%08X\n",
		lcdc_read_reg(lcdc_index, L0_DMA_CTRL));
	LCDC_DUMP("L0_ALPHA=0X%08X\n",
		lcdc_read_reg(lcdc_index, L0_ALPHA));
	LCDC_DUMP("L0_CKEYB_SRC=0x%08x\n",
		lcdc_read_reg(lcdc_index, L0_CKEYB_SRC));
	LCDC_DUMP("L0_CKEYS_SRC=0X%08X\n",
		lcdc_read_reg(lcdc_index, L0_CKEYS_SRC));
	LCDC_DUMP("L0_CKEYB_DST=0x%08x\n",
		lcdc_read_reg(lcdc_index, L0_CKEYB_DST));
	LCDC_DUMP("L0_CKEYS_DST=0X%08X\n",
		lcdc_read_reg(lcdc_index, L0_CKEYS_DST));
	LCDC_DUMP("L0_FIFO_CHK=0X%08X\n",
		lcdc_read_reg(lcdc_index, L0_FIFO_CHK));
	LCDC_DUMP("L0_FIFO_STATUS=0x%08x\n",
		lcdc_read_reg(lcdc_index, L0_FIFO_STATUS));
	LCDC_DUMP("L0_DMA_ACCCNT=0X%08X\n",
		lcdc_read_reg(lcdc_index, L0_DMA_ACCCNT));
	LCDC_DUMP("L0_YUV2RGB_RCOEF=0X%08X\n",
		lcdc_read_reg(lcdc_index, L0_YUV2RGB_RCOEF));
	LCDC_DUMP("L0_YUV2RGB_GCOEF=0X%08X\n",
		lcdc_read_reg(lcdc_index, L0_YUV2RGB_GCOEF));
	LCDC_DUMP("L0_YUV2RGB_BCOEF=0X%08X\n",
		lcdc_read_reg(lcdc_index, L0_YUV2RGB_BCOEF));
	LCDC_DUMP("L0_YUV2RGB_OFFSET1=0X%08X\n",
		lcdc_read_reg(lcdc_index, L0_YUV2RGB_OFFSET1));
	LCDC_DUMP("L0_YUV2RGB_OFFSET2=0X%08X\n",
		lcdc_read_reg(lcdc_index, L0_YUV2RGB_OFFSET2));
	LCDC_DUMP("L0_YUV2RGB_OFFSET3=0X%08X\n",
		lcdc_read_reg(lcdc_index, L0_YUV2RGB_OFFSET3));

	/* Layer1 Register */
	LCDC_DUMP("L1_CTRL=0x%08x\n",
		lcdc_read_reg(lcdc_index, L1_CTRL));
	LCDC_DUMP("L1_HSTART=0x%08x\n",
		lcdc_read_reg(lcdc_index, L1_HSTART));
	LCDC_DUMP("L1_VSTART=0X%08X\n",
		lcdc_read_reg(lcdc_index, L1_VSTART));
	LCDC_DUMP("L1_HEND=0X%08X\n",
		lcdc_read_reg(lcdc_index, L1_HEND));
	LCDC_DUMP("L1_VEND=0x%08x\n",
		lcdc_read_reg(lcdc_index, L1_VEND));
	LCDC_DUMP("L1_BASE0=0x%08x\n",
		lcdc_read_reg(lcdc_index, L1_BASE0));
	LCDC_DUMP("L1_BASE1=0X%08X\n",
		lcdc_read_reg(lcdc_index, L1_BASE1));
	LCDC_DUMP("L1_XSIZE=0X%08X\n",
		lcdc_read_reg(lcdc_index, L1_XSIZE));
	LCDC_DUMP("L1_YSIZE=0x%08x\n",
		lcdc_read_reg(lcdc_index, L1_YSIZE));
	LCDC_DUMP("L1_SKIP=0x%08x\n",
		lcdc_read_reg(lcdc_index, L1_SKIP));
	LCDC_DUMP("L1_DMA_CTRL=0X%08X\n",
		lcdc_read_reg(lcdc_index, L1_DMA_CTRL));
	LCDC_DUMP("L1_ALPHA=0X%08X\n",
		lcdc_read_reg(lcdc_index, L1_ALPHA));
	LCDC_DUMP("L1_CKEYB_SRC=0x%08x\n",
		lcdc_read_reg(lcdc_index, L1_CKEYB_SRC));
	LCDC_DUMP("L1_CKEYS_SRC=0X%08X\n",
		lcdc_read_reg(lcdc_index, L1_CKEYS_SRC));
	LCDC_DUMP("L1_CKEYB_DST=0x%08x\n",
		lcdc_read_reg(lcdc_index, L1_CKEYB_DST));
	LCDC_DUMP("L1_CKEYS_DST=0X%08X\n",
		lcdc_read_reg(lcdc_index, L1_CKEYS_DST));
	LCDC_DUMP("L1_FIFO_CHK=0X%08X\n",
		lcdc_read_reg(lcdc_index, L1_FIFO_CHK));
	LCDC_DUMP("L1_FIFO_STATUS=0x%08x\n",
		lcdc_read_reg(lcdc_index, L1_FIFO_STATUS));
	LCDC_DUMP("L1_DMA_ACCCNT=0X%08X\n",
		lcdc_read_reg(lcdc_index, L1_DMA_ACCCNT));
	LCDC_DUMP("L1_YUV2RGB_RCOEF=0X%08X\n",
		lcdc_read_reg(lcdc_index, L1_YUV2RGB_RCOEF));
	LCDC_DUMP("L1_YUV2RGB_GCOEF=0X%08X\n",
		lcdc_read_reg(lcdc_index, L1_YUV2RGB_GCOEF));
	LCDC_DUMP("L1_YUV2RGB_BCOEF=0X%08X\n",
		lcdc_read_reg(lcdc_index, L1_YUV2RGB_BCOEF));
	LCDC_DUMP("L1_YUV2RGB_OFFSET1=0X%08X\n",
		lcdc_read_reg(lcdc_index, L1_YUV2RGB_OFFSET1));
	LCDC_DUMP("L1_YUV2RGB_OFFSET2=0X%08X\n",
		lcdc_read_reg(lcdc_index, L1_YUV2RGB_OFFSET2));
	LCDC_DUMP("L1_YUV2RGB_OFFSET3=0X%08X\n",
		lcdc_read_reg(lcdc_index, L1_YUV2RGB_OFFSET3));

	/* Lay2 Register */
	LCDC_DUMP("L2_CTRL=0x%08x\n",
		lcdc_read_reg(lcdc_index, L2_CTRL));
	LCDC_DUMP("L2_HSTART=0x%08x\n",
		lcdc_read_reg(lcdc_index, L2_HSTART));
	LCDC_DUMP("L2_VSTART=0X%08X\n",
		lcdc_read_reg(lcdc_index, L2_VSTART));
	LCDC_DUMP("L2_HEND=0X%08X\n",
		lcdc_read_reg(lcdc_index, L2_HEND));
	LCDC_DUMP("L2_VEND=0x%08x\n",
		lcdc_read_reg(lcdc_index, L2_VEND));
	LCDC_DUMP("L2_BASE0=0x%08x\n",
		lcdc_read_reg(lcdc_index, L2_BASE0));
	LCDC_DUMP("L2_BASE1=0X%08X\n",
		lcdc_read_reg(lcdc_index, L2_BASE1));
	LCDC_DUMP("L2_XSIZE=0X%08X\n",
		lcdc_read_reg(lcdc_index, L2_XSIZE));
	LCDC_DUMP("L2_YSIZE=0x%08x\n",
		lcdc_read_reg(lcdc_index, L2_YSIZE));
	LCDC_DUMP("L2_SKIP=0x%08x\n",
		lcdc_read_reg(lcdc_index, L2_SKIP));
	LCDC_DUMP("L2_DMA_CTRL=0X%08X\n",
		lcdc_read_reg(lcdc_index, L2_DMA_CTRL));
	LCDC_DUMP("L2_ALPHA=0X%08X\n",
		lcdc_read_reg(lcdc_index, L2_ALPHA));
	LCDC_DUMP("L2_CKEYB_SRC=0x%08x\n",
		lcdc_read_reg(lcdc_index, L2_CKEYB_SRC));
	LCDC_DUMP("L2_CKEYS_SRC=0X%08X\n",
		lcdc_read_reg(lcdc_index, L2_CKEYS_SRC));
	LCDC_DUMP("L2_CKEYB_DST=0x%08x\n",
		lcdc_read_reg(lcdc_index, L2_CKEYB_DST));
	LCDC_DUMP("L2_CKEYS_DST=0X%08X\n",
		lcdc_read_reg(lcdc_index, L2_CKEYS_DST));
	LCDC_DUMP("L2_FIFO_CHK=0X%08X\n",
		lcdc_read_reg(lcdc_index, L2_FIFO_CHK));
	LCDC_DUMP("L2_FIFO_STATUS=0x%08x\n",
		lcdc_read_reg(lcdc_index, L2_FIFO_STATUS));
	LCDC_DUMP("L2_DMA_ACCCNT=0X%08X\n",
		lcdc_read_reg(lcdc_index, L2_DMA_ACCCNT));
	LCDC_DUMP("L2_YUV2RGB_RCOEF=0X%08X\n",
		lcdc_read_reg(lcdc_index, L2_YUV2RGB_RCOEF));
	LCDC_DUMP("L2_YUV2RGB_GCOEF=0X%08X\n",
		lcdc_read_reg(lcdc_index, L2_YUV2RGB_GCOEF));
	LCDC_DUMP("L2_YUV2RGB_BCOEF=0X%08X\n",
		lcdc_read_reg(lcdc_index, L2_YUV2RGB_BCOEF));
	LCDC_DUMP("L2_YUV2RGB_OFFSET1=0X%08X\n",
		lcdc_read_reg(lcdc_index, L2_YUV2RGB_OFFSET1));
	LCDC_DUMP("L2_YUV2RGB_OFFSET2=0X%08X\n",
		lcdc_read_reg(lcdc_index, L2_YUV2RGB_OFFSET2));
	LCDC_DUMP("L2_YUV2RGB_OFFSET3=0X%08X\n",
		lcdc_read_reg(lcdc_index, L2_YUV2RGB_OFFSET3));

	/* Lay3 Register */
	LCDC_DUMP("L3_CTRL=0x%08x\n",
		lcdc_read_reg(lcdc_index, L3_CTRL));
	LCDC_DUMP("L3_HSTART=0x%08x\n",
		lcdc_read_reg(lcdc_index, L3_HSTART));
	LCDC_DUMP("L3_VSTART=0X%08X\n",
		lcdc_read_reg(lcdc_index, L3_VSTART));
	LCDC_DUMP("L3_HEND=0X%08X\n",
		lcdc_read_reg(lcdc_index, L3_HEND));
	LCDC_DUMP("L3_VEND=0x%08x\n",
		lcdc_read_reg(lcdc_index, L3_VEND));
	LCDC_DUMP("L3_BASE0=0x%08x\n",
		lcdc_read_reg(lcdc_index, L3_BASE0));
	LCDC_DUMP("L3_BASE1=0X%08X\n",
		lcdc_read_reg(lcdc_index, L3_BASE1));
	LCDC_DUMP("L3_XSIZE=0X%08X\n",
		lcdc_read_reg(lcdc_index, L3_XSIZE));
	LCDC_DUMP("L3_YSIZE=0x%08x\n",
		lcdc_read_reg(lcdc_index, L3_YSIZE));
	LCDC_DUMP("L3_SKIP=0x%08x\n",
		lcdc_read_reg(lcdc_index, L3_SKIP));
	LCDC_DUMP("L3_DMA_CTRL=0X%08X\n",
		lcdc_read_reg(lcdc_index, L3_DMA_CTRL));
	LCDC_DUMP("L3_ALPHA=0X%08X\n",
		lcdc_read_reg(lcdc_index, L3_ALPHA));
	LCDC_DUMP("L3_CKEYB_SRC=0x%08x\n",
		lcdc_read_reg(lcdc_index, L3_CKEYB_SRC));
	LCDC_DUMP("L3_CKEYS_SRC=0X%08X\n",
		lcdc_read_reg(lcdc_index, L3_CKEYS_SRC));
	LCDC_DUMP("L3_CKEYB_DST=0x%08x\n",
		lcdc_read_reg(lcdc_index, L3_CKEYB_DST));
	LCDC_DUMP("L3_CKEYS_DST=0X%08X\n",
		lcdc_read_reg(lcdc_index, L3_CKEYS_DST));
	LCDC_DUMP("L3_FIFO_CHK=0X%08X\n",
		lcdc_read_reg(lcdc_index, L3_FIFO_CHK));
	LCDC_DUMP("L3_FIFO_STATUS=0x%08x\n",
		lcdc_read_reg(lcdc_index, L3_FIFO_STATUS));
	LCDC_DUMP("L3_DMA_ACCCNT=0X%08X\n",
		lcdc_read_reg(lcdc_index, L3_DMA_ACCCNT));
	LCDC_DUMP("L3_YUV2RGB_RCOEF=0X%08X\n",
		lcdc_read_reg(lcdc_index, L3_YUV2RGB_RCOEF));
	LCDC_DUMP("L3_YUV2RGB_GCOEF=0X%08X\n",
		lcdc_read_reg(lcdc_index, L3_YUV2RGB_GCOEF));
	LCDC_DUMP("L3_YUV2RGB_BCOEF=0X%08X\n",
		lcdc_read_reg(lcdc_index, L3_YUV2RGB_BCOEF));
	LCDC_DUMP("L3_YUV2RGB_OFFSET1=0X%08X\n",
		lcdc_read_reg(lcdc_index, L3_YUV2RGB_OFFSET1));
	LCDC_DUMP("L3_YUV2RGB_OFFSET2=0X%08X\n",
		lcdc_read_reg(lcdc_index, L3_YUV2RGB_OFFSET2));
	LCDC_DUMP("L3_YUV2RGB_OFFSET3=0X%08X\n",
		lcdc_read_reg(lcdc_index, L3_YUV2RGB_OFFSET3));
}

static void lcdc0_dump_regs(struct seq_file *s)
{
	lcdc_dump_regs(s, SIRFSOC_VDSS_LCDC0);
}

static void lcdc1_dump_regs(struct seq_file *s)
{
	lcdc_dump_regs(s, SIRFSOC_VDSS_LCDC1);
}

#define VDSS_SUBSYS_NAME "LCDC"
#define NUM_LVDS_OUTPUT 2

static struct sirfsoc_output_rgb {
	struct platform_device *pdev;
	struct mutex lock;
	struct sirfsoc_video_timings timings;
	int data_lines;

	struct sirfsoc_vdss_output output;
} rgb;

static struct sirfsoc_output_lvds {
	struct platform_device *pdev;
	struct mutex lock;
	struct sirfsoc_video_timings timings;
	int data_lines;

	enum vdss_lvdsc_fmt fmt;
	struct sirfsoc_vdss_output output;
} lvds[NUM_LVDS_OUTPUT];


unsigned int lcdc_read_reg(u32 index, unsigned int offset)
{
	return readl(lcdc[index].base + offset);
}

void lcdc_write_reg(u32 index, unsigned int offset, unsigned int value)
{
	writel(value, lcdc[index].base + offset);
}

static int lvds_connect(struct sirfsoc_vdss_output *out,
	struct sirfsoc_vdss_panel *dst)
{
	struct sirfsoc_vdss_screen *scn;
	int ret;

	scn = sirfsoc_vdss_get_screen(out->lcdc_id, out->screen_id);
	if (!scn) {
		pr_err("screen error\n");
		return -ENODEV;
	}

	ret = vdss_screen_set_output(scn, out);
	if (ret) {
		pr_err("set output error\n");
		return ret;
	}

	ret = sirfsoc_vdss_output_set_panel(out, dst);
	if (ret) {
		VDSSERR("failed to connect output to device: %s\n", dst->name);
		vdss_screen_unset_output(scn);
		return ret;
	}

	lvdsc_select_src(out->lcdc_id);

	lcdc_output_configure_pins(out->lcdc_id, false,
		dst->phy.lvds.data_lines);

	return 0;
}

static void lvds_disconnect(struct sirfsoc_vdss_output *out,
	struct sirfsoc_vdss_panel *dst)
{
	WARN_ON(dst != out->dst);

	if (dst != out->dst)
		return;

	sirfsoc_vdss_output_unset_panel(out);

	if (out->screen)
		vdss_screen_unset_output(out->screen);
}

static int lvds_enable(struct sirfsoc_vdss_output *out)
{
	struct sirfsoc_output_lvds *plvds = container_of(out,
		struct sirfsoc_output_lvds, output);
	struct sirfsoc_video_timings *t = &plvds->timings;

	mutex_lock(&plvds->lock);

	vdss_screen_set_timings(out->screen, t);
	vdss_screen_set_data_lines(out->screen, plvds->data_lines);
	vdss_screen_update_regs_extra(out->screen);
	lvdsc_setup(plvds->fmt);
	vdss_screen_enable(out->screen);

	mutex_unlock(&plvds->lock);

	return 0;
}

static void lvds_disable(struct sirfsoc_vdss_output *out)
{
	struct sirfsoc_output_lvds *plvds = container_of(out,
		struct sirfsoc_output_lvds, output);

	mutex_lock(&plvds->lock);

	vdss_screen_disable(out->screen);

	mutex_unlock(&plvds->lock);
}

static void lvds_set_timings(struct sirfsoc_vdss_output *out,
	struct sirfsoc_video_timings *timings)
{
	struct sirfsoc_output_lvds *plvds = container_of(out,
		struct sirfsoc_output_lvds, output);
	mutex_lock(&plvds->lock);

	plvds->timings = *timings;

	mutex_unlock(&plvds->lock);
}

static void lvds_set_data_lines(struct sirfsoc_vdss_output *out,
	int data_lines)
{
	struct sirfsoc_output_lvds *plvds = container_of(out,
		struct sirfsoc_output_lvds, output);
	mutex_lock(&plvds->lock);

	plvds->data_lines = data_lines;

	mutex_unlock(&plvds->lock);
}

static void lvds_set_fmt(struct sirfsoc_vdss_output *out,
	enum vdss_lvdsc_fmt fmt)
{
	struct sirfsoc_output_lvds *plvds = container_of(out,
		struct sirfsoc_output_lvds, output);
	mutex_lock(&plvds->lock);

	plvds->fmt = fmt;

	mutex_unlock(&plvds->lock);
}

static const struct sirfsoc_vdss_lvds_ops lvds_ops = {
	.connect = lvds_connect,
	.disconnect = lvds_disconnect,

	.enable = lvds_enable,
	.disable = lvds_disable,

	.set_timings = lvds_set_timings,

	.set_data_lines = lvds_set_data_lines,
	.set_fmt = lvds_set_fmt,
};

static int lvds_init_output(struct platform_device *pdev)
{
	struct sirfsoc_vdss_output *out;
	struct sirfsoc_lcdc *plcdc = dev_get_drvdata(&pdev->dev);

	if (SIRFSOC_VDSS_LCDC0 == plcdc->id) {
		mutex_init(&lvds[0].lock);
		out = &lvds[0].output;
		out->id = SIRFSOC_VDSS_OUTPUT_LVDS1;
		out->name = "lvds.0";
		out->lcdc_id = SIRFSOC_VDSS_LCDC0;
	} else {
		mutex_init(&lvds[1].lock);
		out = &lvds[1].output;
		out->id = SIRFSOC_VDSS_OUTPUT_LVDS2;
		out->name = "lvds.1";
		out->lcdc_id = SIRFSOC_VDSS_LCDC1;
	}

	out->dev = &pdev->dev;
	out->screen_id = SIRFSOC_VDSS_SCREEN0;
	out->supported_panel = SIRFSOC_PANEL_LVDS;
	out->ops.lvds = &lvds_ops;
	out->owner = THIS_MODULE;
	sirfsoc_vdss_register_output(out);

	return 0;
}

static void lvds_deinit_output(struct platform_device *pdev)
{
	struct sirfsoc_lcdc *plcdc = dev_get_drvdata(&pdev->dev);
	struct sirfsoc_vdss_output *out;

	if (SIRFSOC_VDSS_LCDC0 == plcdc->id)
		out = &lvds[0].output;
	else
		out = &lvds[1].output;

	sirfsoc_vdss_unregister_output(out);
}

unsigned long lcdc_clk_get_rate(u32 index)
{
	return clk_get_rate(lcdc[index].clk);
}

static int rgb_connect(struct sirfsoc_vdss_output *out,
	struct sirfsoc_vdss_panel *dst)
{
	struct sirfsoc_vdss_screen *scn;
	int r;

	scn = sirfsoc_vdss_get_screen(out->lcdc_id, out->screen_id);

	if (!scn) {
		pr_err("rgb connect, no scn\n");
		return -ENODEV;
	}

	r = vdss_screen_set_output(scn, out);
	if (r) {
		pr_err("rgb connect, set scn failed\n");
		return r;
	}

	r = sirfsoc_vdss_output_set_panel(out, dst);
	if (r) {
		VDSSERR("failed to connect output to new device: %s\n",
			dst->name);
		vdss_screen_unset_output(scn);
		return r;
	}

	lcdc_output_configure_pins(out->lcdc_id,
		dst->type == SIRFSOC_PANEL_HDMI, dst->phy.rgb.data_lines);

	return 0;
}

static void rgb_disconnect(struct sirfsoc_vdss_output *out,
	struct sirfsoc_vdss_panel *dst)
{
	WARN_ON(dst != out->dst);

	if (dst != out->dst)
		return;

	sirfsoc_vdss_output_unset_panel(out);

	if (out->screen)
		vdss_screen_unset_output(out->screen);
}

static int rgb_enable(struct sirfsoc_vdss_output *out)
{
	struct sirfsoc_output_rgb *prgb = container_of(out,
		struct sirfsoc_output_rgb, output);
	struct sirfsoc_video_timings *t = &prgb->timings;

	mutex_lock(&prgb->lock);

	vdss_screen_set_timings(out->screen, t);
	vdss_screen_set_data_lines(out->screen, prgb->data_lines);
	vdss_screen_enable(out->screen);

	mutex_unlock(&prgb->lock);

	return 0;
}

static void rgb_disable(struct sirfsoc_vdss_output *out)
{
	struct sirfsoc_output_rgb *prgb = container_of(out,
		struct sirfsoc_output_rgb, output);
	mutex_lock(&prgb->lock);

	vdss_screen_disable(out->screen);

	mutex_unlock(&prgb->lock);
}

static void rgb_set_timings(struct sirfsoc_vdss_output *out,
	struct sirfsoc_video_timings *timings)
{
	struct sirfsoc_output_rgb *prgb = container_of(out,
		struct sirfsoc_output_rgb, output);
	mutex_lock(&prgb->lock);

	prgb->timings = *timings;

	mutex_unlock(&prgb->lock);
}


static void rgb_set_data_lines(struct sirfsoc_vdss_output *out,
	int data_lines)
{
	struct sirfsoc_output_rgb *prgb = container_of(out,
		struct sirfsoc_output_rgb, output);
	mutex_lock(&prgb->lock);

	prgb->data_lines = data_lines;

	mutex_unlock(&prgb->lock);
}

static const struct sirfsoc_vdss_rgb_ops rgb_ops = {
	.connect = rgb_connect,
	.disconnect = rgb_disconnect,

	.enable = rgb_enable,
	.disable = rgb_disable,

	.set_timings = rgb_set_timings,

	.set_data_lines = rgb_set_data_lines,
};

static int rgb_init_output(struct platform_device *pdev)
{
	struct sirfsoc_vdss_output *out = &rgb.output;
	struct sirfsoc_lcdc *plcdc = dev_get_drvdata(&pdev->dev);

	/* only lcd0 support rgb output */
	if (plcdc->id != SIRFSOC_VDSS_LCDC0)
		return 0;

	mutex_init(&rgb.lock);

	out->dev = &pdev->dev;
	out->id = SIRFSOC_VDSS_OUTPUT_RGB;
	out->name = "rgb.0";

	out->lcdc_id = plcdc->id;
	out->screen_id = SIRFSOC_VDSS_SCREEN0;
	out->supported_panel = SIRFSOC_PANEL_RGB | SIRFSOC_PANEL_HDMI;
	out->ops.rgb = &rgb_ops;
	out->owner = THIS_MODULE;
	sirfsoc_vdss_register_output(out);


	return 0;
}

static void rgb_deinit_output(struct platform_device *pdev)
{
	struct sirfsoc_vdss_output *out = &rgb.output;
	struct sirfsoc_lcdc *plcdc = dev_get_drvdata(&pdev->dev);

	/* only lcd0 support rgb output */
	if (plcdc->id != SIRFSOC_VDSS_LCDC0)
		return;
	sirfsoc_vdss_unregister_output(out);
}

static irqreturn_t lcdc_irq_handler(int irq, void *dev_id)
{
	struct sirfsoc_lcdc *plcdc = (struct sirfsoc_lcdc *)dev_id;

	return plcdc->user_handler(irq, plcdc->user_data);
}

static int lcdc_request_irq(irq_handler_t handler, void *dev_id)
{
	int r;
	struct sirfsoc_lcdc *plcdc = (struct sirfsoc_lcdc *)dev_id;

	if (plcdc->user_handler)
		return -EBUSY;

	plcdc->user_handler = handler;
	plcdc->user_data = dev_id;

	/* ensure the lcdc_irq_handler sees the values above */
	smp_wmb();

	r = devm_request_irq(&plcdc->pdev->dev, plcdc->irq, lcdc_irq_handler,
			     IRQF_SHARED, "SIRFSOC LCDC", plcdc);
	if (r) {
		plcdc->user_handler = NULL;
		plcdc->user_data = NULL;
	}

	return r;
}

static void lcdc_free_irq(void *dev_id)
{
	struct sirfsoc_lcdc *plcdc = (struct sirfsoc_lcdc *)dev_id;

	devm_free_irq(&plcdc->pdev->dev, plcdc->irq, plcdc);

	plcdc->user_handler = NULL;
	plcdc->user_data = NULL;
}

/* lcdc.irq_lock has to be locked by the caller */
static void _sirfsoc_lcdc_set_irqs(u32 lcdc_index)
{
	u32 mask;
	int i;
	struct sirfsoc_lcdc_irq *lcdc_irq = &lcdc[lcdc_index].lcdc_irq;
	struct sirfsoc_lcdc_isr_data *isr_data;

	mask = lcdc_irq->irq_err_mask;

	for (i = 0; i < LCDC_MAX_NR_ISRS; i++) {
		isr_data = &lcdc_irq->registered_isr[i];

		if (isr_data->isr == NULL)
			continue;

		mask |= isr_data->mask;
	}

	lcdc_write_intmask(lcdc_index, mask);
}

int sirfsoc_lcdc_register_isr(u32 lcdc_index, sirfsoc_lcdc_isr_t isr,
	void *arg, u32 mask)
{
	int i;
	int ret;
	unsigned long flags;
	struct sirfsoc_lcdc_irq *lcdc_irq = &lcdc[lcdc_index].lcdc_irq;
	struct sirfsoc_lcdc_isr_data *isr_data;

	if (isr == NULL)
		return -EINVAL;

	spin_lock_irqsave(&lcdc_irq->irq_lock, flags);

	/* check for duplicate entry */
	for (i = 0; i < LCDC_MAX_NR_ISRS; i++) {
		isr_data = &lcdc_irq->registered_isr[i];
		if (isr_data->isr == isr && isr_data->arg == arg &&
				isr_data->mask == mask) {
			ret = -EINVAL;
			goto err;
		}
	}

	isr_data = NULL;
	ret = -EBUSY;

	for (i = 0; i < LCDC_MAX_NR_ISRS; i++) {
		isr_data = &lcdc_irq->registered_isr[i];

		if (isr_data->isr != NULL)
			continue;

		isr_data->isr = isr;
		isr_data->arg = arg;
		isr_data->mask = mask;
		ret = 0;

		break;
	}

	if (ret)
		goto err;

	_sirfsoc_lcdc_set_irqs(lcdc_index);

	spin_unlock_irqrestore(&lcdc_irq->irq_lock, flags);

	return 0;
err:
	spin_unlock_irqrestore(&lcdc_irq->irq_lock, flags);

	return ret;
}
EXPORT_SYMBOL(sirfsoc_lcdc_register_isr);

int sirfsoc_vdss_get_num_lcdc(void)
{
	return num_lcdc;
}
EXPORT_SYMBOL(sirfsoc_vdss_get_num_lcdc);

int sirfsoc_lcdc_unregister_isr(u32 lcdc_index, sirfsoc_lcdc_isr_t isr,
	void *arg, u32 mask)
{
	int i;
	unsigned long flags;
	int ret = -EINVAL;
	struct sirfsoc_lcdc_irq *lcdc_irq = &lcdc[lcdc_index].lcdc_irq;
	struct sirfsoc_lcdc_isr_data *isr_data;

	spin_lock_irqsave(&lcdc_irq->irq_lock, flags);

	for (i = 0; i < LCDC_MAX_NR_ISRS; i++) {
		isr_data = &lcdc_irq->registered_isr[i];
		if (isr_data->isr != isr || isr_data->arg != arg ||
			isr_data->mask != mask)
			continue;

		/* found the correct isr */

		isr_data->isr = NULL;
		isr_data->arg = NULL;
		isr_data->mask = 0;

		ret = 0;
		break;
	}

	if (ret == 0)
		_sirfsoc_lcdc_set_irqs(lcdc_index);

	spin_unlock_irqrestore(&lcdc_irq->irq_lock, flags);

	return ret;
}
EXPORT_SYMBOL(sirfsoc_lcdc_unregister_isr);

static irqreturn_t sirfsoc_lcdc_irq_handler(int irq, void *dev_id)
{
	int i;
	u32 int_status, int_mask;
	u32 handledirqs = 0;
	u32 unhandled_errors;
	struct sirfsoc_lcdc_isr_data *isr_data;
	struct sirfsoc_lcdc_isr_data registered_isr[LCDC_MAX_NR_ISRS];
	struct sirfsoc_lcdc *plcdc = (struct sirfsoc_lcdc *)dev_id;
	struct sirfsoc_lcdc_irq *lcdc_irq = &plcdc->lcdc_irq;

	spin_lock(&lcdc_irq->irq_lock);

	int_status = lcdc_read_intstatus(plcdc->id);
	int_mask = lcdc_read_intmask(plcdc->id);

	/* IRQ is not for us */
	if (!(int_status & int_mask)) {
		spin_unlock(&lcdc_irq->irq_lock);
		return IRQ_NONE;
	}

	/* Ack the interrupt. Do it here before clocks are possibly turned
	 * off */
	lcdc_clear_intstatus(plcdc->id, int_status);
	/* flush posted write */
	lcdc_read_intstatus(plcdc->id);

	/* make a copy and unlock, so that isrs can unregister
	 * themselves */
	memcpy(registered_isr, lcdc_irq->registered_isr,
		sizeof(registered_isr));

	spin_unlock(&lcdc_irq->irq_lock);

	for (i = 0; i < LCDC_MAX_NR_ISRS; i++) {
		isr_data = &registered_isr[i];

		if (!isr_data->isr)
			continue;

		if (isr_data->mask & int_status) {
			isr_data->isr(isr_data->arg, int_status);
			handledirqs |= isr_data->mask;
		}
	}

	spin_lock(&lcdc_irq->irq_lock);

	unhandled_errors = int_status & ~handledirqs & lcdc_irq->irq_err_mask;

	if (unhandled_errors) {
		lcdc_irq->err_irqs |= unhandled_errors;

		lcdc_irq->irq_err_mask &= ~unhandled_errors;
		_sirfsoc_lcdc_set_irqs(plcdc->id);

		schedule_work(&lcdc_irq->err_work);
	}

	spin_unlock(&lcdc_irq->irq_lock);

	return IRQ_HANDLED;
}

static void lcdc_err_worker(struct work_struct *work)
{
	int i;
	u32 errors;
	unsigned long flags;
	struct sirfsoc_lcdc_irq *lcdc_irq = container_of(work,
		struct sirfsoc_lcdc_irq, err_work);
	struct sirfsoc_lcdc *plcdc = container_of(lcdc_irq,
		struct sirfsoc_lcdc, lcdc_irq);

	static const unsigned fifo_abnormal_bits[] = {
		LCDC_INT_L0_OFLOW | LCDC_INT_L0_UFLOW,
		LCDC_INT_L1_OFLOW | LCDC_INT_L1_UFLOW,
		LCDC_INT_L2_OFLOW | LCDC_INT_L2_UFLOW,
		LCDC_INT_L3_OFLOW | LCDC_INT_L3_UFLOW,
	};

	spin_lock_irqsave(&lcdc_irq->irq_lock, flags);
	errors = lcdc_irq->err_irqs;
	lcdc_irq->err_irqs = 0;
	spin_unlock_irqrestore(&lcdc_irq->irq_lock, flags);

	for (i = 0; i < sirfsoc_vdss_get_num_layers(plcdc->id); ++i) {
		struct sirfsoc_vdss_layer *l;
		unsigned bit;

		l = sirfsoc_vdss_get_layer(plcdc->id, i);
		bit = fifo_abnormal_bits[i];

		if (bit & errors) {
			VDSSERR("FIFO exception on %s,disable the layer\n",
				l->name);
			/*
			* Fix me. Temporarily, do not disable layer
			* when underflow and overflow happen.
			* Need to fix later.
			*/
#if 0
			l->disable(l);
			msleep(50);
#endif
		}
	}


	spin_lock_irqsave(&lcdc_irq->irq_lock, flags);
	lcdc_irq->irq_err_mask |= errors;
	_sirfsoc_lcdc_set_irqs(plcdc->id);
	spin_unlock_irqrestore(&lcdc_irq->irq_lock, flags);
}

static int lcdc_init_irq(u32 lcdc_index)
{
	int r;
	struct sirfsoc_lcdc_irq *lcdc_irq = &lcdc[lcdc_index].lcdc_irq;

	spin_lock_init(&lcdc_irq->irq_lock);

	memset(lcdc_irq->registered_isr, 0,
		sizeof(lcdc_irq->registered_isr));

	lcdc_irq->irq_err_mask = LCDC_INT_MASK_ERRS;

	lcdc_clear_intstatus(lcdc_index, lcdc_read_intstatus(lcdc_index));

	INIT_WORK(&lcdc_irq->err_work, lcdc_err_worker);

	_sirfsoc_lcdc_set_irqs(lcdc_index);

	r = lcdc_request_irq(sirfsoc_lcdc_irq_handler, &lcdc[lcdc_index]);
	if (r) {
		VDSSERR("lcdc_request_irq failed, ret = %x\n", r);
		return r;
	}

	return 0;
}

static void lcdc_deinit_irq(u32 lcdc_index)
{
	lcdc_free_irq(&lcdc[lcdc_index].lcdc_irq);
}

static int __init sirfsoc_lcdc_probe(struct platform_device *pdev)
{
	struct device_node *dn = pdev->dev.of_node;
	struct resource *res;
	struct pinctrl *p;
	u32 id;
	u32 ed = 1;
	u32 plist_idx = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		VDSSERR("can't get IORESOURCE_MEM\n");
		return -EINVAL;
	}

	if (of_property_read_u32(dn, "cell-index", &id)) {
		dev_err(&pdev->dev, "Fail to get LCDC index\n");
		return -ENODEV;
	}

	if (id > SIRFSOC_VDSS_LCDC1) {
		dev_err(&pdev->dev, "LCDC index error\n");
		return -ENODEV;
	}

	lcdc[id].id = id;
	lcdc[id].pdev = pdev;

	lcdc[id].base = devm_ioremap(&pdev->dev, res->start,
		resource_size(res));
	if (!lcdc[id].base) {
		VDSSERR("can't ioremap\n");
		return -ENOMEM;
	}

	if (of_device_is_compatible(dn, "sirf,atlas7-lcdc"))
		lcdc[id].is_atlas7 = true;
	else
		lcdc[id].is_atlas7 = false;

	of_property_read_u32(dn, "ldd-panel", &plist_idx);
	lcdc[id].pad_panel.padlist = lcdc_padlist_panel[plist_idx];
	lcdc[id].pad_panel.pad_set_num = LCDC_LDD_NUM;

	lcdc[id].pad_hdmi.padlist = lcdc_padlist_hdmi;
	lcdc[id].pad_hdmi.pad_set_num = LCDC_LDD_NUM;

	lcdc[id].irq = platform_get_irq(pdev, 0);
	if (lcdc[id].irq < 0) {
		VDSSERR("platform_get_irq failed\n");
		return -ENODEV;
	}

	lcdc[id].clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(lcdc[id].clk)) {
		VDSSERR("Failed to get lcdc clock!\n");
		return -ENODEV;
	}

	of_property_read_u32(dn, "error-diffusion", &ed);
	lcdc[id].property.error_diffusion = !!ed;

	if (SIRFSOC_VDSS_LCDC0 == id) {
		p = devm_pinctrl_get_select_default(&pdev->dev);
		if (IS_ERR(p)) {
			VDSSERR("Fail to select lcdc pinmux\n");
			return  -EINVAL;
		}
	}

	clk_prepare_enable(lcdc[id].clk);

	vdss_init_layers(id);
	vdss_init_screens(id);

	vdss_init_layers_sysfs(id);

	lcdc_init_irq(id);
	dev_set_drvdata(&pdev->dev, &lcdc[id]);

	if (SIRFSOC_VDSS_LCDC0 == id)
		rgb_init_output(pdev);
	lvds_init_output(pdev);

	if (SIRFSOC_VDSS_LCDC0 == id)
		vdss_debugfs_create_file("lcdc0_regs", lcdc0_dump_regs);
	else if (SIRFSOC_VDSS_LCDC1 == id)
		vdss_debugfs_create_file("lcdc1_regs", lcdc1_dump_regs);

	num_lcdc++;
	return 0;

}

static int __exit sirfsoc_lcdc_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sirfsoc_lcdc *plcdc = dev_get_drvdata(dev);

	if (SIRFSOC_VDSS_LCDC0 == plcdc->id) {
		rgb_deinit_output(pdev);
		lvds_deinit_output(pdev);
	} else
		lvds_deinit_output(pdev);

	vdss_uninit_layers_sysfs(plcdc->id);

	vdss_uninit_screens(plcdc->id);
	vdss_uninit_layers(plcdc->id);

	lcdc_deinit_irq(plcdc->id);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int sirfsoc_lcdc_resume_early(struct device *dev)
{
	struct sirfsoc_lcdc *lcdc = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(lcdc->clk);
	if (unlikely(ret))
		goto exit;

	_sirfsoc_lcdc_set_irqs(lcdc->id);

	if (lcdc->cur_pad) {
		u32 *plist = NULL;
		u32 pad_num = 0;
		u32 i;

		plist = lcdc->cur_pad->padlist;
		pad_num = lcdc->cur_pad->pad_set_num;
		for (i = 0; i < pad_num; i++)
			lcdc_write_reg(lcdc->id, PADMUX_LDD_0 + i * 4,
				       1 << plist[i]);
	}

exit:
	return ret;
}

static int sirfsoc_lcdc_suspend(struct device *dev)
{
	struct sirfsoc_lcdc *lcdc = dev_get_drvdata(dev);
	struct sirfsoc_lcdc_irq *lcdc_irq = &lcdc->lcdc_irq;

	lcdc_write_intmask(lcdc->id, lcdc_irq->irq_err_mask);
	clk_disable_unprepare(lcdc->clk);
	return 0;
}

static const struct dev_pm_ops sirfsoc_lcdc_pm_ops = {
	.resume_early	= sirfsoc_lcdc_resume_early,
	.suspend	= sirfsoc_lcdc_suspend,
};

#define SIRFVDSS_LCDC_PM_OPS (&sirfsoc_lcdc_pm_ops)

#else

#define SIRFVDSS_LCDC_PM_OPS NULL

#endif /* CONFIG_PM_SLEEP */

static const struct of_device_id lcdc_of_match[] = {
	{.compatible = "sirf,lcdc",},
	{.compatible = "sirf,atlas7-lcdc",},
	{},
};

static struct platform_driver sirfsoc_lcdc_driver = {
	.remove         = sirfsoc_lcdc_remove,
	.driver         = {
		.name   = "sirfsoc_lcdc",
		.owner  = THIS_MODULE,
		.of_match_table = lcdc_of_match,
		.pm	= SIRFVDSS_LCDC_PM_OPS,
	},
};

int __init lcdc_init_platform_driver(void)
{
	return platform_driver_probe(&sirfsoc_lcdc_driver,
		sirfsoc_lcdc_probe);
}

void lcdc_uninit_platform_driver(void)
{
	platform_driver_unregister(&sirfsoc_lcdc_driver);
}
