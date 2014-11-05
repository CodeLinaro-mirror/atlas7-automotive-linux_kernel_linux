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
#include <linux/pinctrl/consumer.h>
#include <linux/delay.h>

#include <video/sirfsoc_vdss.h>
#include "vdss.h"
#include "lcdc.h"

static void __lcdc_wait_idle(int layer, bool with_vpp)
{
	int timeout;

	timeout = 0;
	while (lcdc_read_reg(DMA_STATUS) & (1 << layer)) {
		msleep(20);
		timeout++;
		if (timeout > 1000)
			LCDC_DEBUG("wait DMA_STATUS timeout\n");
	}

	timeout = 0;
	while (lcdc_read_reg(S0_LAYER_STATUS) & (1 << layer)) {
		msleep(20);
		timeout++;
		if (timeout > 1000)
			LCDC_DEBUG("wait S0_LAYER_STATUS timeout\n");
	}
}

static void __lcdc_disable_layer(enum vdss_layer layer,
	bool wait, bool passthrough)
{
	u32 s0_layer_sel;
	u32 lx_dma_ctrl;

	s0_layer_sel = lcdc_read_reg(S0_LAYER_SEL);
	if (s0_layer_sel & S0_LS_LAYER_SEL(1 << layer)) {
		s0_layer_sel &= ~S0_LS_LAYER_SEL(1 << layer);
		lcdc_write_reg(S0_LAYER_SEL, s0_layer_sel);
		if (passthrough) {
			lx_dma_ctrl = lcdc_read_reg(reg_offset(layer,
					L0_DMA_CTRL));
			lx_dma_ctrl &= ~LX_VPP_PASS_MODE;
			lcdc_write_reg(reg_offset(layer, L0_DMA_CTRL),
					lx_dma_ctrl);
			__lcdc_confirm_layer_setting(layer);
		}

		if (wait)
			__lcdc_wait_idle(layer, passthrough);
	}
}

static void __lcdc_enable_layer(int layer, bool passthrough)
{
	u32 s0_layer_sel;
	u32 lx_dma_ctrl;

	s0_layer_sel = lcdc_read_reg(S0_LAYER_SEL);
	if (!(s0_layer_sel & S0_LS_LAYER_SEL(1 << layer))) {
		__lcdc_wait_idle(layer, passthrough);
		__lcdc_reset_layer_fifo(layer);

		lx_dma_ctrl = lcdc_read_reg(reg_offset(layer, L0_DMA_CTRL));
		if (passthrough)
			lx_dma_ctrl |= LX_VPP_PASS_MODE;
		else
			lx_dma_ctrl &= ~LX_VPP_PASS_MODE;

		lcdc_write_reg(reg_offset(layer, L0_DMA_CTRL), lx_dma_ctrl);
		__lcdc_confirm_layer_setting(layer);

		s0_layer_sel |= S0_LS_LAYER_SEL(1 << layer);
		lcdc_write_reg(S0_LAYER_SEL, s0_layer_sel);
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

u32 lcdc_read_intstatus(void)
{
	return lcdc_read_reg(INT_CTRL_STATUS);
}

void lcdc_clear_intstatus(u32 mask)
{
	lcdc_write_reg(INT_CTRL_STATUS, mask);
}

u32 lcdc_read_intmask(void)
{
	return lcdc_read_reg(INT_MASK);
}

void lcdc_write_intmask(u32 mask)
{
	u32 old_mask = lcdc_read_reg(INT_MASK);

	/* clear the irqstatus for newly enabled irqs */
	lcdc_clear_intstatus((mask ^ old_mask) & mask);
	lcdc_write_reg(INT_MASK, mask);
}

void lcdc_layer_enable(enum vdss_layer layer, bool enable, bool passthrough)
{

	if (enable)
		__lcdc_enable_layer(layer, passthrough);
	else
		__lcdc_disable_layer(layer, false, passthrough);

}

bool lcdc_is_vpp_passthrough(enum vdss_pixelformat fmt)
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

void lcdc_layer_confirm_setting(enum vdss_layer layer)
{
	u32 lx_ctrl;

	lx_ctrl = lcdc_read_reg(reg_offset(layer, L0_CTRL));

	lx_ctrl |= LX_CTRL_CONFIRM;
	lcdc_write_reg(reg_offset(layer, L0_CTRL), lx_ctrl);
}

static void lcdc_layer_set_fmt(enum vdss_layer layer, int fmt, bool passthrough)
{
	u32 lx_ctrl = 0x0000301e;

	lx_ctrl = lcdc_read_reg(reg_offset(layer, L0_CTRL));

	lx_ctrl &= ~LX_CTRL_BPP_MASK;

	if (passthrough)
		lx_ctrl |= LX_CTRL_BPP(
			__lcdc_fmt_to_hwfmt(VPP_TO_LCD_PIXELFORMAT));
	else
		lx_ctrl |= LX_CTRL_BPP(__lcdc_fmt_to_hwfmt(fmt));

	lx_ctrl |= LX_CTRL_REPLICATE;

	lx_ctrl &= ~LX_CTRL_CONFIRM;

	lcdc_write_reg(reg_offset(layer, L0_CTRL), lx_ctrl);
}

static void lcdc_layer_set_alpha(enum vdss_layer layer, int fmt,
	bool premulti, bool source, bool global, u8 alpha)
{
	u32 lx_ctrl;

	lx_ctrl = lcdc_read_reg(reg_offset(layer, L0_CTRL));
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
	lcdc_write_reg(reg_offset(layer, L0_CTRL), lx_ctrl);

	if (global)
		lcdc_write_reg(reg_offset(layer, L0_ALPHA),
			LX_ALPHA_VAL(0xff));

}

static void lcdc_layer_set_ckey(enum vdss_layer layer, bool ckey_on,
	u32 ckey, bool dst_ckey_on, u32 dst_ckey, int fmt)
{
	u32 lx_ctrl;

	lx_ctrl = lcdc_read_reg(reg_offset(layer, L0_CTRL));

	if (ckey_on) {
		lx_ctrl |= LX_CTRL_SRC_CKEY_EN;
		lcdc_write_reg(reg_offset(layer, L0_CKEYB_SRC),
			__lcdc_ckey_val(fmt, true, ckey));
		lcdc_write_reg(reg_offset(layer, L0_CKEYS_SRC),
			__lcdc_ckey_val(fmt, true, ckey));
	} else
		lx_ctrl &= ~LX_CTRL_SRC_CKEY_EN;

	if (dst_ckey_on) {
		lx_ctrl |= LX_CTRL_DST_CKEY_EN;
		lcdc_write_reg(reg_offset(layer, L0_CKEYB_DST),
			__lcdc_ckey_val(fmt, true, dst_ckey));
		lcdc_write_reg(reg_offset(layer, L0_CKEYS_DST),
			__lcdc_ckey_val(fmt, true, dst_ckey));
	} else
		lx_ctrl &= ~LX_CTRL_DST_CKEY_EN;

	lx_ctrl &= ~LX_CTRL_CONFIRM;
	lcdc_write_reg(reg_offset(layer, L0_CTRL), lx_ctrl);
}

static void lcdc_layer_set_base(enum vdss_layer layer,
	struct vdss_rect *src_rect,
	int surf_width, int surf_height,
	int fmt, u32 base)
{
	unsigned int bpp = hwfmt_to_bpp[__lcdc_fmt_to_hwfmt(fmt)];
	unsigned int offset =
		(src_rect->top * surf_width + src_rect->left) * bpp;

	lcdc_write_reg(reg_offset(layer, L0_BASE0), base + offset);
}

static void lcdc_layer_set_dst(enum vdss_layer layer,
	struct vdss_rect *dst_rect)
{
	u32 s0_hstart;
	u32 s0_vstart;

	s0_hstart = lcdc_read_reg(S0_ACT_HSTART);
	s0_vstart = lcdc_read_reg(S0_ACT_VSTART);

	lcdc_write_reg(reg_offset(layer, L0_HSTART),
		dst_rect->left + s0_hstart);
	lcdc_write_reg(reg_offset(layer, L0_HEND),
		dst_rect->right + s0_hstart);

	lcdc_write_reg(reg_offset(layer, L0_VSTART),
		dst_rect->top + s0_vstart);
	lcdc_write_reg(reg_offset(layer, L0_VEND),
		dst_rect->bottom + s0_vstart);
}

static void lcdc_layer_set_dma(enum vdss_layer layer,
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
	lcdc_write_reg(reg_offset(layer, L0_BASE0), base + offset);
	if (tv_mode)
		lcdc_write_reg(reg_offset(layer, L0_BASE1),
			base + offset + surf_width * bpp);

	lcdc_write_reg(reg_offset(layer, L0_XSIZE), xsize);
	lcdc_write_reg(reg_offset(layer, L0_YSIZE), ysize);
	lcdc_write_reg(reg_offset(layer, L0_SKIP),  skip);

	lx_fifo_chk = LX_LO_CHK(0xF0) | LX_MI_CHK(0x80) | LX_REQ_SEL;
	lcdc_write_reg(reg_offset(layer, L0_FIFO_CHK), lx_fifo_chk);

	lx_dma_ctrl = lcdc_read_reg(reg_offset(layer, L0_DMA_CTRL));
	lx_dma_ctrl &= ~LX_SUPPRESS_QW_NUM_MASK;
	lx_dma_ctrl &= ~LX_DMA_UNIT_MASK;
	lx_dma_ctrl |= LX_SUPPRESS_QW_NUM(((xsize + 1) * dma_unit -
		width * bpp - (offset & 7)) >> 3);
	lx_dma_ctrl |= LX_DMA_UNIT((dma_unit >> 3) - 1);
	lx_dma_ctrl |= LX_DMA_MODE;
	if (tv_mode)
		lx_dma_ctrl |= LX_DMA_CHAIN_MODE;
	lcdc_write_reg(reg_offset(layer, L0_DMA_CTRL), lx_dma_ctrl);
}

void lcdc_layer_set_passthrough(enum vdss_layer layer,
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

	lcdc_write_reg(reg_offset(layer, L0_BASE0), VPP_TO_LCD_BPP * dst_skip);
	lcdc_write_reg(reg_offset(layer, L0_BASE1), VPP_TO_LCD_BPP * dst_skip);
}

static void lcdc_layer_set_size(enum vdss_layer layer,
	struct sirfsoc_vdss_layer_info *info, int scn_width, int scn_height)
{
	lcdc_layer_check_size(&info->src_rect, &info->dst_rect,
		&info->src_rect_on, &info->dst_rect_on,
		scn_width, scn_height, info->passthrough);

	lcdc_layer_set_dst(layer, &info->dst_rect_on);

	if (info->passthrough)
		lcdc_layer_set_passthrough(layer, info);
	else
		lcdc_layer_set_dma(layer, &info->src_rect_on,
			info->surf_width, info->surf_height,
			info->fmt, info->base);
}

void lcdc_layer_setup(enum vdss_layer layer,
	struct sirfsoc_vdss_layer_info *info,
	struct sirfsoc_video_timings *timings)
{
	info->passthrough = lcdc_is_vpp_passthrough(info->fmt);

	lcdc_layer_set_fmt(layer, info->fmt, info->passthrough);
	lcdc_layer_set_size(layer, info, timings->xres, timings->yres);

	lcdc_layer_set_ckey(layer, info->ckey_on, info->ckey,
		info->dst_ckey_on, info->dst_ckey, info->fmt);

	lcdc_layer_set_alpha(layer, info->fmt, info->pre_mult_alpha,
		info->source_alpha, info->global_alpha,
		info->alpha);

	if (info->passthrough) {
		struct vdss_vpp_params params;

		memset(&params, 0, sizeof(params));

		params.src_base = info->base;
		params.src_fmt = info->fmt;
		params.src_hor_stride = info->surf_width;
		params.src_ver_stride = info->surf_height;
		params.src_rect = info->src_rect_on;
		params.dst_rect = info->dst_rect_on;
		params.dst_base = 0;
		params.dst_fmt = VPP_TO_LCD_PIXELFORMAT;

		vpp_passthrough_setup(&params);
	}

	lcdc_layer_confirm_setting(layer);
}

bool lcdc_flip(enum vdss_layer layer, struct sirfsoc_vdss_layer_info *info)
{
	if (info->passthrough) {
		struct vdss_blt_params params;

		memset(&params, 0, sizeof(params));

		params.params.src_base = info->base;
		params.flags |= VDSS_VPP_UPDATE_SRCBASE;

		params.params.src_fmt = info->fmt;
		params.params.src_hor_stride = info->surf_width;
		params.params.src_ver_stride = info->surf_height;
		params.params.src_rect = info->src_rect_on;
		params.params.dst_rect = info->dst_rect_on;
		params.params.dst_base = 0;
		params.params.dst_fmt = VPP_TO_LCD_PIXELFORMAT;

		vpp_blt(&params);
		lcdc_layer_confirm_setting(layer);
	} else
		lcdc_layer_set_base(layer, &info->src_rect, info->surf_width,
			info->surf_height, info->fmt, info->base);

	return true;
}

void lcdc_screen_set_timings(enum vdss_screen scn_id,
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

	bool tv_mode = false;

	s0_osc_ratio |= S0_OSC_HALF_DUTY;

	if (tv_mode) {
		s0_osc_ratio |= S0_OSC_DIV_RATIO(0x8);
	} else {
		int div_ratio =
			lcdc_clk_get_rate() / timings->pixel_clock  - 1;

		if (div_ratio < 1)
			s0_osc_ratio |= S0_OSC_DIV_RATIO(0x1);
		else
			s0_osc_ratio |= S0_OSC_DIV_RATIO(div_ratio);
	}

	lcdc_write_reg(S0_OSC_RATIO, s0_osc_ratio);

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
	lcdc_write_reg(S0_TIM_CTRL, s0_tim_ctrl);

	lcdc_write_reg(S0_RGB_SEQ, S0_RGB_SEQ_RGB);

	s0_hsync_period = timings->xres + timings->hsw +
		timings->hfp + timings->hbp - 1;
	lcdc_write_reg(S0_HSYNC_PERIOD, s0_hsync_period);

	s0_hsync_width = timings->hsw - 1;
	lcdc_write_reg(S0_HSYNC_WIDTH, s0_hsync_width);

	s0_vsync_period = timings->yres + timings->vsw +
		timings->vfp + timings->vbp - 1;
	lcdc_write_reg(S0_VSYNC_PERIOD, s0_vsync_period);

	s0_vsync_width |= S0_VW_VSYNC_WIDTH(timings->vsw - 1);
	/* vsync width is in number of lines */
	s0_vsync_width |= S0_VSYC_WIDTH_UINT;
	lcdc_write_reg(S0_VSYNC_WIDTH, s0_vsync_width);

	so_act_hstart = timings->hsw + timings->hbp - 12;
	lcdc_write_reg(S0_ACT_HSTART, so_act_hstart);
	so_act_hend = so_act_hstart + timings->xres - 1;
	lcdc_write_reg(S0_ACT_HEND, so_act_hend);

	so_act_vstart = timings->vsw + timings->vbp;
	lcdc_write_reg(S0_ACT_VSTART, so_act_vstart);
	so_act_vend += so_act_vstart + timings->yres - 1;
	lcdc_write_reg(S0_ACT_VEND, so_act_vend);

	s0_disp_mode = S0_TOP_LAYER(3) |
		S0_OUT_FORMAT(LCDC_OUT_24BIT_RBG888) |
		S0_FRAME_VALID;
	lcdc_write_reg(S0_DISP_MODE, s0_disp_mode);

	/* backlight scaling setting */
	lcdc_write_reg(BLS_CTRL1, (timings->xres << 20) |
		(timings->yres << 9) | 64);
	lcdc_write_reg(BLS_CTRL2, (15 << 4) | (0));
	lcdc_write_reg(BLS_LEVEL_TB0, 0 | (2 << 8) | (4 << 16) | (6 << 24));
	lcdc_write_reg(BLS_LEVEL_TB1, 8 | (10 << 8) | (12 << 16) | (14 << 24));
	lcdc_write_reg(BLS_LEVEL_TB2, 16 | (18 << 8) | (20 << 16) | (22 << 24));
	lcdc_write_reg(BLS_LEVEL_TB3, 24 | (26 << 8) | (28 << 16) | (30 << 24));
}

void lcdc_screen_setup(enum vdss_screen scn_id,
	const struct sirfsoc_vdss_screen_info *info)
{
	u32 scr_ctrl = 0x0;
	u32 s0_int_line = 0x0;
	u32 s0_yuv_ctrl = 0x0;
	u32 s0_tv_field = 0x0;
	u32 s0_blank = 0x0;

	bool tv_mode = false;

	/* Debug purpose: to check if we set right SCN_*_VAL */
	s0_blank = S0_BLANK_VALUE(0xff0000) | S0_BLANK_VALID;
	lcdc_write_reg(S0_BLANK, s0_blank);
	lcdc_write_reg(S0_BACK_COLOR, 0);

	scr_ctrl |= SCREEN0_EN;
	lcdc_write_reg(SCR_CTRL, scr_ctrl);

	s0_int_line |= S0_INT_LINE_VALID;
	lcdc_write_reg(S0_INT_LINE, s0_int_line);

	lcdc_write_reg(S0_RGB_YUV_COEF1, 0x00428119);
	lcdc_write_reg(S0_RGB_YUV_COEF2, 0x00264A70);
	lcdc_write_reg(S0_RGB_YUV_COEF3, 0x00705E12);
	lcdc_write_reg(S0_RGB_YUV_OFFSET, 0x00108080);

	s0_yuv_ctrl |= S0_YUV_SEQ(1); /* YVYU sequence */
	s0_yuv_ctrl |= S0_EVEN_UV;
	s0_tv_field = S0_TV_HSTART(0x339) | S0_TV_VSTART(0x106);
	if (tv_mode) {
		s0_yuv_ctrl |= S0_RGB_YUV;
		s0_tv_field |= S0_TV_F_VALID;
	}
	lcdc_write_reg(S0_YUV_CTRL, s0_yuv_ctrl);
	lcdc_write_reg(S0_TV_FIELD, s0_tv_field);
}

void lcdc_print_regs(void)
{
	LCDC_DUMP("LCD registers:\n");
	LCDC_DUMP("S0_HSYNC_PERIOD=0x%08x\n", lcdc_read_reg(S0_HSYNC_PERIOD));
	LCDC_DUMP("S0_HSYNC_WIDTH=0x%08x\n", lcdc_read_reg(S0_HSYNC_WIDTH));
	LCDC_DUMP("S0_VSYNC_PERIOD=0x%08x\n", lcdc_read_reg(S0_VSYNC_PERIOD));
	LCDC_DUMP("S0_VSYNC_WIDTH=0x%08x\n", lcdc_read_reg(S0_VSYNC_WIDTH));
	LCDC_DUMP("S0_ACT_HSTART=0x%08x\n", lcdc_read_reg(S0_ACT_HSTART));
	LCDC_DUMP("S0_ACT_VSTART=0x%08x\n", lcdc_read_reg(S0_ACT_VSTART));
	LCDC_DUMP("S0_ACT_HEND=0x%08x\n", lcdc_read_reg(S0_ACT_HEND));
	LCDC_DUMP("S0_ACT_VEND=0x%08x\n", lcdc_read_reg(S0_ACT_VEND));
	LCDC_DUMP("S0_OSC_RATIO=0x%08x\n", lcdc_read_reg(S0_OSC_RATIO));
	LCDC_DUMP("S0_TIM_CTRL=0x%08x\n", lcdc_read_reg(S0_TIM_CTRL));
	LCDC_DUMP("S0_TIM_STATUS=0x%08x\n", lcdc_read_reg(S0_TIM_STATUS));
	LCDC_DUMP("S0_HCOUNT=0x%08x\n", lcdc_read_reg(S0_HCOUNT));
	LCDC_DUMP("S0_VCOUNT=0x%08x\n", lcdc_read_reg(S0_VCOUNT));
	LCDC_DUMP("S0_BLANK=0x%08x\n", lcdc_read_reg(S0_BLANK));
	LCDC_DUMP("S0_BACK_COLOR=0x%08x\n", lcdc_read_reg(S0_BACK_COLOR));
	LCDC_DUMP("S0_DISP_MODE=0x%08x\n", lcdc_read_reg(S0_DISP_MODE));
	LCDC_DUMP("S0_LAYER_SEL=0x%08x\n", lcdc_read_reg(S0_LAYER_SEL));
	LCDC_DUMP("S0_RGB_SEQ=0x%08x\n", lcdc_read_reg(S0_RGB_SEQ));
	LCDC_DUMP("S0_RGB_YUV_COEF1=0x%08x\n", lcdc_read_reg(S0_RGB_YUV_COEF1));
	LCDC_DUMP("S0_RGB_YUV_COEF2=0x%08x\n", lcdc_read_reg(S0_RGB_YUV_COEF2));
	LCDC_DUMP("S0_RGB_YUV_COEF3=0x%08x\n", lcdc_read_reg(S0_RGB_YUV_COEF3));
	LCDC_DUMP("S0_YUV_CTRL=0x%08x\n", lcdc_read_reg(S0_YUV_CTRL));
	LCDC_DUMP("S0_TV_FIELD=0x%08x\n", lcdc_read_reg(S0_TV_FIELD));
	LCDC_DUMP("S0_INT_LINE=0x%08x\n", lcdc_read_reg(S0_INT_LINE));
	LCDC_DUMP("S0_LAYER_STATUS=0x%08x\n", lcdc_read_reg(S0_LAYER_STATUS));
	LCDC_DUMP("DMA_STATUS=0x%08x\n", lcdc_read_reg(DMA_STATUS));
	LCDC_DUMP("SCR_CTRL=0X%08X\n", lcdc_read_reg(SCR_CTRL));
	LCDC_DUMP("INT_MASK=0X%08X\n", lcdc_read_reg(INT_MASK));
	LCDC_DUMP("INT_CTRL_STATUS=0X%08X\n", lcdc_read_reg(INT_CTRL_STATUS));

	/* Lay0 register */
	LCDC_DUMP("L0_CTRL=0x%08x\n", lcdc_read_reg(L0_CTRL));
	LCDC_DUMP("L0_HSTART=0x%08x\n", lcdc_read_reg(L0_HSTART));
	LCDC_DUMP("L0_VSTART=0X%08X\n", lcdc_read_reg(L0_VSTART));
	LCDC_DUMP("L0_HEND=0X%08X\n", lcdc_read_reg(L0_HEND));
	LCDC_DUMP("L0_VEND=0x%08x\n", lcdc_read_reg(L0_VEND));
	LCDC_DUMP("L0_BASE0=0x%08x\n", lcdc_read_reg(L0_BASE0));
	LCDC_DUMP("L0_BASE1=0X%08X\n", lcdc_read_reg(L0_BASE1));
	LCDC_DUMP("L0_XSIZE=0X%08X\n", lcdc_read_reg(L0_XSIZE));
	LCDC_DUMP("L0_YSIZE=0x%08x\n", lcdc_read_reg(L0_YSIZE));
	LCDC_DUMP("L0_SKIP=0x%08x\n", lcdc_read_reg(L0_SKIP));
	LCDC_DUMP("L0_DMA_CTRL=0X%08X\n", lcdc_read_reg(L0_DMA_CTRL));
	LCDC_DUMP("L0_ALPHA=0X%08X\n", lcdc_read_reg(L0_ALPHA));
	LCDC_DUMP("L0_CKEYB_SRC=0x%08x\n", lcdc_read_reg(L0_CKEYB_SRC));
	LCDC_DUMP("L0_CKEYS_SRC=0X%08X\n", lcdc_read_reg(L0_CKEYS_SRC));
	LCDC_DUMP("L0_CKEYB_DST=0x%08x\n", lcdc_read_reg(L0_CKEYB_DST));
	LCDC_DUMP("L0_CKEYS_DST=0X%08X\n", lcdc_read_reg(L0_CKEYS_DST));
	LCDC_DUMP("L0_FIFO_CHK=0X%08X\n", lcdc_read_reg(L0_FIFO_CHK));
	LCDC_DUMP("L0_FIFO_STATUS=0x%08x\n", lcdc_read_reg(L0_FIFO_STATUS));

	/* Lay1 Register */
	LCDC_DUMP("L1_CTRL=0x%08x\n", lcdc_read_reg(L1_CTRL));
	LCDC_DUMP("L1_HSTART=0x%08x\n", lcdc_read_reg(L1_HSTART));
	LCDC_DUMP("L1_VSTART=0X%08X\n", lcdc_read_reg(L1_VSTART));
	LCDC_DUMP("L1_HEND=0X%08X\n", lcdc_read_reg(L1_HEND));
	LCDC_DUMP("L1_VEND=0x%08x\n", lcdc_read_reg(L1_VEND));
	LCDC_DUMP("L1_BASE0=0x%08x\n", lcdc_read_reg(L1_BASE0));
	LCDC_DUMP("L1_BASE1=0X%08X\n", lcdc_read_reg(L1_BASE1));
	LCDC_DUMP("L1_XSIZE=0X%08X\n", lcdc_read_reg(L1_XSIZE));
	LCDC_DUMP("L1_YSIZE=0x%08x\n", lcdc_read_reg(L1_YSIZE));
	LCDC_DUMP("L1_SKIP=0x%08x\n", lcdc_read_reg(L1_SKIP));
	LCDC_DUMP("L1_DMA_CTRL=0X%08X\n", lcdc_read_reg(L1_DMA_CTRL));
	LCDC_DUMP("L1_ALPHA=0X%08X\n", lcdc_read_reg(L1_ALPHA));
	LCDC_DUMP("L1_CKEYB_SRC=0x%08x\n", lcdc_read_reg(L1_CKEYB_SRC));
	LCDC_DUMP("L1_CKEYS_SRC=0X%08X\n", lcdc_read_reg(L1_CKEYS_SRC));
	LCDC_DUMP("L1_CKEYB_DST=0x%08x\n", lcdc_read_reg(L1_CKEYB_DST));
	LCDC_DUMP("L1_CKEYS_DST=0X%08X\n", lcdc_read_reg(L1_CKEYS_DST));
	LCDC_DUMP("L1_FIFO_CHK=0X%08X\n", lcdc_read_reg(L1_FIFO_CHK));
	LCDC_DUMP("L1_FIFO_STATUS=0x%08x\n", lcdc_read_reg(L1_FIFO_STATUS));

	/* Lay2 Register */
	LCDC_DUMP("L2_CTRL=0x%08x\n", lcdc_read_reg(L2_CTRL));
	LCDC_DUMP("L2_HSTART=0x%08x\n", lcdc_read_reg(L2_HSTART));
	LCDC_DUMP("L2_VSTART=0X%08X\n", lcdc_read_reg(L2_VSTART));
	LCDC_DUMP("L2_HEND=0X%08X\n", lcdc_read_reg(L2_HEND));
	LCDC_DUMP("L2_VEND=0x%08x\n", lcdc_read_reg(L2_VEND));
	LCDC_DUMP("L2_BASE0=0x%08x\n", lcdc_read_reg(L2_BASE0));
	LCDC_DUMP("L2_BASE1=0X%08X\n", lcdc_read_reg(L2_BASE1));
	LCDC_DUMP("L2_XSIZE=0X%08X\n", lcdc_read_reg(L2_XSIZE));
	LCDC_DUMP("L2_YSIZE=0x%08x\n", lcdc_read_reg(L2_YSIZE));
	LCDC_DUMP("L2_SKIP=0x%08x\n", lcdc_read_reg(L2_SKIP));
	LCDC_DUMP("L2_DMA_CTRL=0X%08X\n", lcdc_read_reg(L2_DMA_CTRL));
	LCDC_DUMP("L2_ALPHA=0X%08X\n", lcdc_read_reg(L2_ALPHA));
	LCDC_DUMP("L2_CKEYB_SRC=0x%08x\n", lcdc_read_reg(L2_CKEYB_SRC));
	LCDC_DUMP("L2_CKEYS_SRC=0X%08X\n", lcdc_read_reg(L2_CKEYS_SRC));
	LCDC_DUMP("L2_CKEYB_DST=0x%08x\n", lcdc_read_reg(L2_CKEYB_DST));
	LCDC_DUMP("L2_CKEYS_DST=0X%08X\n", lcdc_read_reg(L2_CKEYS_DST));
	LCDC_DUMP("L2_FIFO_CHK=0X%08X\n", lcdc_read_reg(L2_FIFO_CHK));
	LCDC_DUMP("L2_FIFO_STATUS=0x%08x\n", lcdc_read_reg(L2_FIFO_STATUS));

	/* Lay3 Register */
	LCDC_DUMP("L3_CTRL=0x%08x\n", lcdc_read_reg(L3_CTRL));
	LCDC_DUMP("L3_HSTART=0x%08x\n", lcdc_read_reg(L3_HSTART));
	LCDC_DUMP("L3_VSTART=0X%08X\n", lcdc_read_reg(L3_VSTART));
	LCDC_DUMP("L3_HEND=0X%08X\n", lcdc_read_reg(L3_HEND));
	LCDC_DUMP("L3_VEND=0x%08x\n", lcdc_read_reg(L3_VEND));
	LCDC_DUMP("L3_BASE0=0x%08x\n", lcdc_read_reg(L3_BASE0));
	LCDC_DUMP("L3_BASE1=0X%08X\n", lcdc_read_reg(L3_BASE1));
	LCDC_DUMP("L3_XSIZE=0X%08X\n", lcdc_read_reg(L3_XSIZE));
	LCDC_DUMP("L3_YSIZE=0x%08x\n", lcdc_read_reg(L3_YSIZE));
	LCDC_DUMP("L3_SKIP=0x%08x\n", lcdc_read_reg(L3_SKIP));
	LCDC_DUMP("L3_DMA_CTRL=0X%08X\n", lcdc_read_reg(L3_DMA_CTRL));
	LCDC_DUMP("L3_ALPHA=0X%08X\n", lcdc_read_reg(L3_ALPHA));
	LCDC_DUMP("L3_CKEYB_SRC=0x%08x\n", lcdc_read_reg(L3_CKEYB_SRC));
	LCDC_DUMP("L3_CKEYS_SRC=0X%08X\n", lcdc_read_reg(L3_CKEYS_SRC));
	LCDC_DUMP("L3_CKEYB_DST=0x%08x\n", lcdc_read_reg(L3_CKEYB_DST));
	LCDC_DUMP("L3_CKEYS_DST=0X%08X\n", lcdc_read_reg(L3_CKEYS_DST));
	LCDC_DUMP("L3_FIFO_CHK=0X%08X\n", lcdc_read_reg(L3_FIFO_CHK));
	LCDC_DUMP("L3_FIFO_STATUS=0x%08x\n", lcdc_read_reg(L3_FIFO_STATUS));

}

#define VDSS_SUBSYS_NAME "LCDC"

static struct sirfsoc_lcdc {
	struct platform_device *pdev;
	void __iomem    *base;

	int irq;
	irq_handler_t user_handler;
	void *user_data;

	struct clk	*clk;
} lcdc;

#define LCDC_MAX_NR_ISRS		8
#define LCDC_INT_MASK_ERRS	(LCDC_INT_L0_OFLOW | \
	LCDC_INT_L0_UFLOW | LCDC_INT_L1_OFLOW | \
	LCDC_INT_L1_UFLOW | LCDC_INT_L2_OFLOW | \
	LCDC_INT_L2_UFLOW | LCDC_INT_L3_OFLOW | \
	LCDC_INT_L3_UFLOW)

struct sirfsoc_lcdc_isr_data {
	sirfsoc_lcdc_isr_t	isr;
	void			*arg;
	u32			mask;
};

static struct {
	spinlock_t irq_lock;
	u32 irq_err_mask;
	struct sirfsoc_lcdc_isr_data registered_isr[LCDC_MAX_NR_ISRS];
	u32 err_irqs;
	struct work_struct err_work;
} lcdc_irq;

static struct {
	struct platform_device *pdev;
	struct mutex lock;
	struct sirfsoc_video_timings timings;
	int data_lines;

	struct sirfsoc_vdss_output output;
} rgb;


unsigned int lcdc_read_reg(unsigned int offset)
{
	return readl(lcdc.base + offset);
}

void lcdc_write_reg(unsigned int offset, unsigned int value)
{
	writel(value, lcdc.base + offset);
}

unsigned long lcdc_clk_get_rate(void)
{
	return clk_get_rate(lcdc.clk);
}

static int rgb_connect(struct sirfsoc_vdss_output *out,
	struct sirfsoc_vdss_panel *dst)
{
	struct sirfsoc_vdss_screen *scn;
	int r;

	scn = sirfsoc_vdss_get_screen(out->screen_id);
	if (!scn)
		return -ENODEV;

	r = vdss_screen_set_output(scn, out);
	if (r)
		return r;

	r = sirfsoc_vdss_output_set_panel(out, dst);
	if (r) {
		VDSSERR("failed to connect output to new device: %s\n",
			dst->name);
		vdss_screen_unset_output(scn);
		return r;
	}

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
	struct sirfsoc_video_timings *t = &rgb.timings;

	mutex_lock(&rgb.lock);

	vdss_screen_set_timings(out->screen, t);
	vdss_screen_set_data_lines(out->screen, rgb.data_lines);
	vdss_screen_enable(out->screen);

	mutex_unlock(&rgb.lock);

	return 0;
}

static void rgb_disable(struct sirfsoc_vdss_output *out)
{
	mutex_lock(&rgb.lock);

	vdss_screen_disable(out->screen);

	mutex_unlock(&rgb.lock);
}

static void rgb_set_timings(struct sirfsoc_vdss_output *out,
	struct sirfsoc_video_timings *timings)
{
	mutex_lock(&rgb.lock);

	rgb.timings = *timings;

	mutex_unlock(&rgb.lock);
}


static void rgb_set_data_lines(struct sirfsoc_vdss_output *out,
	int data_lines)
{
	mutex_lock(&rgb.lock);

	rgb.data_lines = data_lines;

	mutex_unlock(&rgb.lock);
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

	mutex_init(&rgb.lock);

	out->dev = &pdev->dev;
	out->id = SIRFSOC_VDSS_OUTPUT_RGB;
	out->name = "rgb.0";
	out->screen_id = SIRFSOC_VDSS_SCREEN0;
	out->type = SIRFSOC_PANEL_RGB;
	out->ops.rgb = &rgb_ops;
	out->owner = THIS_MODULE;
	sirfsoc_vdss_register_output(out);

	return 0;
}

static void rgb_deinit_output(void)
{
	struct sirfsoc_vdss_output *out = &rgb.output;

	sirfsoc_vdss_unregister_output(out);
}

static int (*vdss_output_init_funcs[])(struct platform_device *) __initdata = {
	rgb_init_output,
};

static void (*vdss_output_deinit_funcs[])(void) __exitdata = {
	rgb_deinit_output,
};

static bool vdss_output_inited[ARRAY_SIZE(vdss_output_init_funcs)];

static irqreturn_t lcdc_irq_handler(int irq, void *arg)
{
	return lcdc.user_handler(irq, lcdc.user_data);
}

static int lcdc_request_irq(irq_handler_t handler, void *dev_id)
{
	int r;

	if (lcdc.user_handler != NULL)
		return -EBUSY;

	lcdc.user_handler = handler;
	lcdc.user_data = dev_id;

	/* ensure the lcdc_irq_handler sees the values above */
	smp_wmb();

	r = devm_request_irq(&lcdc.pdev->dev, lcdc.irq, lcdc_irq_handler,
			     IRQF_SHARED, "SIRFSOC LCDC", &lcdc);
	if (r) {
		lcdc.user_handler = NULL;
		lcdc.user_data = NULL;
	}

	return r;
}

static void lcdc_free_irq(void *dev_id)
{
	devm_free_irq(&lcdc.pdev->dev, lcdc.irq, &lcdc);

	lcdc.user_handler = NULL;
	lcdc.user_data = NULL;
}

/* lcdc.irq_lock has to be locked by the caller */
static void _sirfsoc_lcdc_set_irqs(void)
{
	u32 mask;
	int i;
	struct sirfsoc_lcdc_isr_data *isr_data;

	mask = lcdc_irq.irq_err_mask;

	for (i = 0; i < LCDC_MAX_NR_ISRS; i++) {
		isr_data = &lcdc_irq.registered_isr[i];

		if (isr_data->isr == NULL)
			continue;

		mask |= isr_data->mask;
	}

	lcdc_write_intmask(mask);
}

int sirfsoc_lcdc_register_isr(sirfsoc_lcdc_isr_t isr, void *arg, u32 mask)
{
	int i;
	int ret;
	unsigned long flags;
	struct sirfsoc_lcdc_isr_data *isr_data;

	if (isr == NULL)
		return -EINVAL;

	spin_lock_irqsave(&lcdc_irq.irq_lock, flags);

	/* check for duplicate entry */
	for (i = 0; i < LCDC_MAX_NR_ISRS; i++) {
		isr_data = &lcdc_irq.registered_isr[i];
		if (isr_data->isr == isr && isr_data->arg == arg &&
				isr_data->mask == mask) {
			ret = -EINVAL;
			goto err;
		}
	}

	isr_data = NULL;
	ret = -EBUSY;

	for (i = 0; i < LCDC_MAX_NR_ISRS; i++) {
		isr_data = &lcdc_irq.registered_isr[i];

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

	_sirfsoc_lcdc_set_irqs();

	spin_unlock_irqrestore(&lcdc_irq.irq_lock, flags);

	return 0;
err:
	spin_unlock_irqrestore(&lcdc_irq.irq_lock, flags);

	return ret;
}
EXPORT_SYMBOL(sirfsoc_lcdc_register_isr);

int sirfsoc_lcdc_unregister_isr(sirfsoc_lcdc_isr_t isr, void *arg, u32 mask)
{
	int i;
	unsigned long flags;
	int ret = -EINVAL;
	struct sirfsoc_lcdc_isr_data *isr_data;

	spin_lock_irqsave(&lcdc_irq.irq_lock, flags);

	for (i = 0; i < LCDC_MAX_NR_ISRS; i++) {
		isr_data = &lcdc_irq.registered_isr[i];
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
		_sirfsoc_lcdc_set_irqs();

	spin_unlock_irqrestore(&lcdc_irq.irq_lock, flags);

	return ret;
}
EXPORT_SYMBOL(sirfsoc_lcdc_unregister_isr);

static irqreturn_t sirfsoc_lcdc_irq_handler(int irq, void *arg)
{
	int i;
	u32 int_status, int_mask;
	u32 handledirqs = 0;
	u32 unhandled_errors;
	struct sirfsoc_lcdc_isr_data *isr_data;
	struct sirfsoc_lcdc_isr_data registered_isr[LCDC_MAX_NR_ISRS];

	spin_lock(&lcdc_irq.irq_lock);

	int_status = lcdc_read_intstatus();
	int_mask = lcdc_read_intmask();

	/* IRQ is not for us */
	if (!(int_status & int_mask)) {
		spin_unlock(&lcdc_irq.irq_lock);
		return IRQ_NONE;
	}

	/* Ack the interrupt. Do it here before clocks are possibly turned
	 * off */
	lcdc_clear_intstatus(int_status);
	/* flush posted write */
	lcdc_read_intstatus();

	/* make a copy and unlock, so that isrs can unregister
	 * themselves */
	memcpy(registered_isr, lcdc_irq.registered_isr,
		sizeof(registered_isr));

	spin_unlock(&lcdc_irq.irq_lock);

	for (i = 0; i < LCDC_MAX_NR_ISRS; i++) {
		isr_data = &registered_isr[i];

		if (!isr_data->isr)
			continue;

		if (isr_data->mask & int_status) {
			isr_data->isr(isr_data->arg, int_status);
			handledirqs |= isr_data->mask;
		}
	}

	spin_lock(&lcdc_irq.irq_lock);

	unhandled_errors = int_status & ~handledirqs & lcdc_irq.irq_err_mask;

	if (unhandled_errors) {
		lcdc_irq.err_irqs |= unhandled_errors;

		lcdc_irq.irq_err_mask &= ~unhandled_errors;
		_sirfsoc_lcdc_set_irqs();

		schedule_work(&lcdc_irq.err_work);
	}

	spin_unlock(&lcdc_irq.irq_lock);

	return IRQ_HANDLED;
}

static void lcdc_err_worker(struct work_struct *work)
{
	int i;
	u32 errors;
	unsigned long flags;
	static const unsigned fifo_abnormal_bits[] = {
		LCDC_INT_L0_OFLOW | LCDC_INT_L0_UFLOW,
		LCDC_INT_L1_OFLOW | LCDC_INT_L1_UFLOW,
		LCDC_INT_L2_OFLOW | LCDC_INT_L2_UFLOW,
		LCDC_INT_L3_OFLOW | LCDC_INT_L3_UFLOW,
	};

	spin_lock_irqsave(&lcdc_irq.irq_lock, flags);
	errors = lcdc_irq.err_irqs;
	lcdc_irq.err_irqs = 0;
	spin_unlock_irqrestore(&lcdc_irq.irq_lock, flags);

	for (i = 0; i < sirfsoc_vdss_get_num_layers(); ++i) {
		struct sirfsoc_vdss_layer *l;
		unsigned bit;

		l = sirfsoc_vdss_get_layer(i);
		bit = fifo_abnormal_bits[i];

		if (bit & errors) {
			VDSSERR("FIFO exception on %s,disable the layer\n",
				l->name);
			l->disable(l);
			msleep(50);
		}
	}


	spin_lock_irqsave(&lcdc_irq.irq_lock, flags);
	lcdc_irq.irq_err_mask |= errors;
	_sirfsoc_lcdc_set_irqs();
	spin_unlock_irqrestore(&lcdc_irq.irq_lock, flags);
}

static int lcdc_init_irq(void)
{
	int r;

	spin_lock_init(&lcdc_irq.irq_lock);

	memset(lcdc_irq.registered_isr, 0,
		sizeof(lcdc_irq.registered_isr));

	lcdc_irq.irq_err_mask = LCDC_INT_MASK_ERRS;

	lcdc_clear_intstatus(lcdc_read_intstatus());

	INIT_WORK(&lcdc_irq.err_work, lcdc_err_worker);

	_sirfsoc_lcdc_set_irqs();

	r = lcdc_request_irq(sirfsoc_lcdc_irq_handler, &lcdc_irq);
	if (r) {
		VDSSERR("lcdc_request_irq failed\n");
		return r;
	}

	return 0;
}

static void lcdc_deinit_irq(void)
{
	lcdc_free_irq(&lcdc_irq);
}

static int __init sirfsoc_lcdc_probe(struct platform_device *pdev)
{
	int r = 0;
	struct resource *res;
	int i;
	struct pinctrl *p;

	lcdc.pdev = pdev;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		VDSSERR("can't get IORESOURCE_MEM\n");
		return -EINVAL;
	}

	lcdc.base = devm_ioremap(&pdev->dev, res->start,
		resource_size(res));
	if (!lcdc.base) {
		VDSSERR("can't ioremap\n");
		return -ENOMEM;
	}

	lcdc.irq = platform_get_irq(pdev, 0);
	if (lcdc.irq < 0) {
		VDSSERR("platform_get_irq failed\n");
		return -ENODEV;
	}

	lcdc.clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(lcdc.clk)) {
		VDSSERR("Failed to get lcdc clock!\n");
		return -ENODEV;
	}

	p = devm_pinctrl_get_select_default(&pdev->dev);
	if (IS_ERR(p)) {
		VDSSERR("Fail to select lcdc pinmux\n");
		return  -EINVAL;
	}

	clk_prepare_enable(lcdc.clk);

	vdss_init_layers();
	vdss_init_screens();

	for (i = 0; i < ARRAY_SIZE(vdss_output_init_funcs); ++i) {
		r = vdss_output_init_funcs[i](pdev);
		if (r == 0)
			vdss_output_inited[i] = true;
	}

	lcdc_init_irq();

	return 0;

}

static int __exit sirfsoc_lcdc_remove(struct platform_device *pdev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(vdss_output_deinit_funcs); ++i) {
		if (vdss_output_inited[i])
			vdss_output_deinit_funcs[i]();
	}

	vdss_uninit_screens();
	vdss_uninit_layers();

	lcdc_deinit_irq();

	return 0;
}

static const struct of_device_id lcdc_of_match[] = {
	{.compatible = "sirf,lcdc",},
	{},
};

static struct platform_driver sirfsoc_lcdc_driver = {
	.remove         = sirfsoc_lcdc_remove,
	.driver         = {
		.name   = "sirfsoc_lcdc",
		.owner  = THIS_MODULE,
		.of_match_table = lcdc_of_match,
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
