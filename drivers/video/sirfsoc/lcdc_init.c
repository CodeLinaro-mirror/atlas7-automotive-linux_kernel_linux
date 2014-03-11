/*
 * CSR sirfsoc LCD internal library
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
 */

#include "lcdc_defs.h"


void __iomem *lcdc_regs;


void __lcdc_get_screen_size(unsigned int *width,
				unsigned int *height,
				struct lcdc_panel_info *panel)
{
	switch (panel->out_fmt) {
	case LCDC_OUT_8_BIT_RBGRBG:
		*width = (panel->hend - panel->hstart) / 3 + 1;
		break;
	case LCDC_OUT_8_BIT_YUV422:
		*width = (panel->hend - panel->hstart) / 2 + 1;
		break;
	default:
		*width = panel->hend - panel->hstart + 1;
		break;
	}
	*height = panel->vend - panel->vstart + 1;
}


static void __lcdc_set_panel(struct lcdc_panel_info *panel)
{
	u32 s0_tim_ctrl = 0x0;
	u32 s0_osc_ratio = 0x0;
	u32 s0_disp_mode = 0x0;
	u32 s0_vsync_width = 0x0;
	bool tv_mode = __lcdc_is_tvmode(panel);
	unsigned int  pix_clk = pixel_clock(panel->ref_rate,
		panel->hsync_period, panel->vsync_period);

	s0_osc_ratio |= S0_OSC_HALF_DUTY;

	if (tv_mode) {
		s0_osc_ratio |= S0_OSC_DIV_RATIO(0x8);
	} else {
		int div_ratio = (panel->sys_clk / pix_clk) - 1;

		if (div_ratio < 2)
			s0_osc_ratio |= S0_OSC_DIV_RATIO(0x2);
		else
			s0_osc_ratio |= S0_OSC_DIV_RATIO(div_ratio);
	}
	lcdc_write_reg(S0_OSC_RATIO, s0_osc_ratio);

	if (panel->iomaster) {
		s0_tim_ctrl |= S0_TIM_PCLK_IO;
		s0_tim_ctrl |= S0_TIM_HSYNC_IO;
		s0_tim_ctrl |= S0_TIM_VSYNC_IO;
	} else {
		s0_tim_ctrl &= ~S0_TIM_PCLK_IO;
		s0_tim_ctrl &= ~S0_TIM_HSYNC_IO;
		s0_tim_ctrl &= ~S0_TIM_VSYNC_IO;
	}
	if (panel->pclk_polar)
		s0_tim_ctrl |= S0_TIM_PCLK_POLAR;
	if (panel->pclk_edge)
		s0_tim_ctrl |= S0_TIM_PCLK_EDGE;
	if (panel->hsync_polar)
		s0_tim_ctrl |= S0_TIM_HSYNC_POLAR;
	if (panel->vsync_polar)
		s0_tim_ctrl |= S0_TIM_VSYNC_POLAR;
	s0_tim_ctrl |= S0_TIM_SYNC_DLY(panel->hsync_delay);
	lcdc_write_reg(S0_TIM_CTRL, s0_tim_ctrl);

	lcdc_write_reg(S0_RGB_SEQ, panel->rgb_sequence);
	lcdc_write_reg(S0_HSYNC_PERIOD, panel->hsync_period);
	lcdc_write_reg(S0_HSYNC_WIDTH, panel->hsync_width);
	lcdc_write_reg(S0_VSYNC_PERIOD, panel->vsync_period);

	s0_vsync_width |= S0_VW_VSYNC_WIDTH(panel->vsync_width);
	s0_vsync_width |= S0_VSYC_WIDTH_UINT;
	lcdc_write_reg(S0_VSYNC_WIDTH, s0_vsync_width);

	lcdc_write_reg(S0_ACT_HSTART, panel->hstart);
	lcdc_write_reg(S0_ACT_VSTART, panel->vstart);
	lcdc_write_reg(S0_ACT_HEND, panel->hend);
	lcdc_write_reg(S0_ACT_VEND, panel->vend);

	s0_disp_mode = S0_TOP_LAYER(panel->layer) |
			S0_OUT_FORMAT(panel->out_fmt) |
			S0_FRAME_VALID;
	lcdc_write_reg(S0_DISP_MODE, s0_disp_mode);

	/* backlight scaling setting */
	lcdc_write_reg(BLS_CTRL1,
			((panel->hend - panel->hstart + 1) << 20) |
			((panel->vend - panel->vstart + 1) << 9) |
			64);
	lcdc_write_reg(BLS_CTRL2, (15 << 4) | (0));
	lcdc_write_reg(BLS_LEVEL_TB0, 0 | (2 << 8) | (4 << 16) | (6 << 24));
	lcdc_write_reg(BLS_LEVEL_TB1, 8 | (10 << 8) | (12 << 16) | (14 << 24));
	lcdc_write_reg(BLS_LEVEL_TB2, 16 | (18 << 8) | (20 << 16) | (22 << 24));
	lcdc_write_reg(BLS_LEVEL_TB3, 24 | (26 << 8) | (28 << 16) | (30 << 24));
}

void __lcdc_config_screen(struct lcdc_panel_info *panel)
{
	u32 scr_ctrl = 0x0;
	u32 s0_int_line = 0x0;
	u32 s0_yuv_ctrl = 0x0;
	u32 s0_tv_field = 0x0;
	u32 s0_blank = 0x0;

	bool tv_mode = __lcdc_is_tvmode(panel);

	/* Config screen */
	lcdc_write_reg(INT_MASK, 0);
	lcdc_write_reg(INT_CTRL_STATUS, 0xffff);

	/* Panel related */
	__lcdc_set_panel(panel);

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

static void __lcdc_config_layer0(unsigned int prim_base,
			enum vdss_pixelformat fmt,
			struct lcdc_panel_info *panel)
{
	u32 s0_layer_sel = 0x0;
	u32 lx_alpha = 0x0;
	u32 lx_ctrl = 0x0;
	u32 lx_dma_ctrl = 0x0;
	u32 lx_fifo_chk = 0x0;
	unsigned int w, h;
	int bpp;

	unsigned int xsize, ysize, skip, suppress;
	unsigned int dma_unit, disp_stride;

	bool tv_mode = __lcdc_is_tvmode(panel);
	int layer = panel->layer;

	__lcdc_get_screen_size(&w, &h, panel);

	/* Config Layer 0 */
	lcdc_write_reg(reg_offset(layer, L0_HSTART), panel->hstart);
	lcdc_write_reg(reg_offset(layer, L0_HEND), panel->hend);
	lcdc_write_reg(reg_offset(layer, L0_VSTART), panel->vstart);
	lcdc_write_reg(reg_offset(layer, L0_VEND), panel->vend);

	lx_alpha = LX_ALPHA_VAL(0xff);
	lcdc_write_reg(reg_offset(layer, L0_ALPHA), lx_alpha);

	if (fmt == VDSS_PIXELFORMAT_8888 || fmt == VDSS_PIXELFORMAT_BGRX_8880)
		bpp = 32;
	else
		bpp = 16;

	dma_unit = __lcdc_dma_unit(tv_mode);
	disp_stride = byte_stride(w, bpp);

	xsize = ((disp_stride + dma_unit - 1) / dma_unit) - 1;
	suppress = ((xsize + 1) * dma_unit - disp_stride) >> 3;

	lx_dma_ctrl = LX_SUPPRESS_QW_NUM(suppress) |
			LX_DMA_UNIT((dma_unit >> 3) - 1) |
			LX_DMA_MODE;

	if (tv_mode) {
		ysize = h / 2 - 1;
		skip = disp_stride * 2 - xsize * dma_unit;

		lx_dma_ctrl |= LX_DMA_CHAIN_MODE;

		lcdc_write_reg(reg_offset(layer, L0_BASE1),
				prim_base + disp_stride);
	} else {
		ysize = h - 1;
		skip = disp_stride - (xsize * dma_unit);
	}

	lcdc_write_reg(reg_offset(layer, L0_XSIZE), xsize);
	lcdc_write_reg(reg_offset(layer, L0_SKIP), skip);
	lcdc_write_reg(reg_offset(layer, L0_YSIZE), ysize);
	lcdc_write_reg(reg_offset(layer, L0_DMA_CTRL), lx_dma_ctrl);

	lx_fifo_chk = LX_LO_CHK(0xF0) | LX_MI_CHK(0x80) | LX_REQ_SEL;
	lcdc_write_reg(reg_offset(layer, L0_FIFO_CHK), lx_fifo_chk);

	lcdc_write_reg(reg_offset(layer, L0_BASE0), prim_base);


	lx_ctrl |= LX_CTRL_REPLICATE;
	lx_ctrl |= LX_CTRL_FIFO_RESET;
	/* enable source alpha default for LO_CTRL_BPP_ARGB8888 */
	lx_ctrl |= LX_CTRL_BPP(__lcdc_fmt_to_hwfmt(fmt));
	if ((lx_ctrl & LX_CTRL_BPP_MASK) == LO_CTRL_BPP_ARGB8888) {
		lx_ctrl &= ~LX_CTRL_PREMULTI_ALPHA;
		lx_ctrl |= LX_CTRL_SOURCE_ALPHA;
	} else {
		if (LCDC_DEFAULT_PREMULTI_ALPHA)
			lx_ctrl |= LX_CTRL_PREMULTI_ALPHA;
		else
			lx_ctrl &= ~LX_CTRL_PREMULTI_ALPHA;

		lx_ctrl &= ~LX_CTRL_SOURCE_ALPHA;
	}

	lx_ctrl |= LX_CTRL_CONFIRM;
	lcdc_write_reg(reg_offset(layer, L0_CTRL), lx_ctrl);
	lx_ctrl |= LX_CTRL_FIFO_RESET;
	lcdc_write_reg(reg_offset(layer, L0_CTRL), lx_ctrl);

	s0_layer_sel = S0_LS_LAYER_SEL(1 << layer);
	lcdc_write_reg(S0_LAYER_SEL, s0_layer_sel);
}

void __lcdc_power_up(unsigned int prim_base,
			enum vdss_pixelformat fmt,
			struct lcdc_panel_info *panel)
{
	__lcdc_config_screen(panel);
	__lcdc_config_layer0(prim_base, fmt, panel);
}

void __lcdc_boot_up(void *regs, unsigned long prim_base,
		unsigned int bpp, struct lcdc_panel_info *panel)
{
	enum vdss_pixelformat fmt;

	lcdc_regs = (unsigned char *)regs;
	fmt = (bpp == 16) ? VDSS_PIXELFORMAT_565 : VDSS_PIXELFORMAT_8888;

	panel->pre_power_up();
	__lcdc_power_up(prim_base, fmt, panel);
	panel->post_power_up();
}

