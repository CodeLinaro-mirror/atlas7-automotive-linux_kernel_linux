/*
 * CSR sirfsoc LCD internal interface
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __SIRFSOC_LCDC_DEFS_H
#define __SIRFSOC_LCDC_DEFS_H

#include <linux/string.h>
#include <linux/delay.h>
#include <linux/io.h>

#include "vdss_lcdc.h"
#include "vdss_vpp.h"
#include "lcdc_regs.h"


struct lcdc_cursor_state {
	int width;
	int height;
	int xhot;
	int yhot;
	int xpos;
	int ypos;
	int rotate;
	u32 fifo[256 * 2];	/*first 256 for normal, second 256 for rotate*/
	bool show;
};

struct lcdc_layer_state {
	bool show;
	bool in_use;
	bool need_vpp;

	struct vdss_rect src_rect;	/* IN: source rectangle */
	struct vdss_rect dst_rect;	/* IN: destination rectangle */

	struct vdss_rect src_rect_on;	/* HW state: src rect on screen */
	struct vdss_rect dst_rect_on;	/* HW state: dst rect on screen */

	u32 surf_width;
	u32 surf_height;
	enum vdss_pixelformat fmt;

	bool ckey_on;			/* IN: if color key enable */
	u32 ckey_high;			/* IN: high color key */
	u32 ckey_low;			/* IN: low color key */
	u32 base;			/* IN: physical base address */
	bool global_alpha;		/* IN: if const alpha */
	u8 alpha;			/* IN: alpha value */
	bool dst_ckey_on;
	u32 dst_ckey_high;		/* IN: high color key */
	u32 dst_ckey_low;		/* IN: low color key */
	bool source_alpha;
	bool premulti_alpha;
	bool replicate;

	enum lcdc_flip_mode flip_mode;

	int brightness;
	int contrast;
	int hue;
	int saturation;
};

struct lcdc_config {
	/* Immutable setting from Bootup */

	u32 fb_size;		/* Size of RAM based Video Memory (should be a
				   multiple of screen pitch) */
	enum lcdc_layer top_layer;
	u32 int_state;		/* Interrupt state */
	struct lcdc_layer_state layer_state[LAYER_NUM];
	struct lcdc_cursor_state cursor_state;
	void *vpp_handle;
	bool gamma_enable;
	u8 gamma[256 * 3];
};

extern void __iomem *lcdc_regs;


static inline unsigned int pixel_clock(unsigned int disp_freq,
					unsigned int hpsync,
					unsigned int vpsync)
{
	unsigned int val;

	val = disp_freq * (hpsync + 1) * (vpsync + 1);
	return val;
}

static inline unsigned int refresh_rate(unsigned int pixel_clock,
					unsigned int hpsync,
					unsigned int vpsync)
{
	unsigned int val;

	val = pixel_clock / (hpsync + 1) / (vpsync + 1);
	return val;
}

static inline unsigned int byte_stride(unsigned int screen_width,
					unsigned int bpp)
{
	unsigned int val;

	val = ((bpp * (screen_width + 63)) >> 6) << 3;
	return val;
}

static inline unsigned int reg_offset(int layer, unsigned int reg_offset)
{
	return reg_offset + (layer << LCDC_LAYER_REG_SHIFT);
}

/*
 * Register operation
 */
static inline unsigned int lcdc_read_reg(unsigned int offset)
{
	return readl(lcdc_regs + offset);
}

static inline void lcdc_write_reg(unsigned int offset, unsigned int value)
{
	writel(value, lcdc_regs + offset);
}

#define LCDC_MAX_OVERLAY_WIDTH	2046
#define LCDC_MAX_OVERLAY_HEIGHT	2046
#define LCDC_DISPLAY_FREQUENCY	60

#ifdef VPP_TO_LCDC_8880
#define VPP_TO_LCDC_CTRL_BPP	LO_CTRL_BPP_RGB888
#define VPP_TO_LCDC_PIXELFORMAT	VDSS_PIXELFORMAT_BGRX_8880
#define VPP_TO_LCDC_BPP		4
#else
#define VPP_TO_LCDC_CTRL_BPP	LO_CTRL_BPP_RGB565
#define VPP_TO_LCDC_PIXELFORMAT	VDSS_PIXELFORMAT_565
#define VPP_TO_LCDC_BPP		2
#endif


static inline bool __lcdc_need_vpp(enum vdss_pixelformat format)
{
	return (format >= VDSS_PIXELFORMAT_UYVY);
}

static inline bool __lcdc_is_tvmode(struct lcdc_panel_info *panel)
{
	return (panel->out_fmt == LCDC_OUT_8_BIT_YUV422);
}

static inline unsigned int __lcdc_dma_unit(bool tvmode)
{
	if (tvmode)
		return 32;

	return 128;
}

static inline void __lcdc_reset_layer_fifo(int layer)
{
	u32 lx_ctrl;

	lx_ctrl = lcdc_read_reg(reg_offset(layer, L0_CTRL));

	lx_ctrl |= LX_CTRL_FIFO_RESET;
	lcdc_write_reg(reg_offset(layer, L0_CTRL), lx_ctrl);

	lx_ctrl &= ~LX_CTRL_FIFO_RESET;
	lcdc_write_reg(reg_offset(layer, L0_CTRL), lx_ctrl);
}

static inline void __lcdc_clear_layer_confirm_setting(int layer)
{
	u32 lx_ctrl;

	lx_ctrl = lcdc_read_reg(reg_offset(layer, L0_CTRL));

	if (lx_ctrl & LX_CTRL_CONFIRM) {
		lx_ctrl &= ~LX_CTRL_CONFIRM;
		lcdc_write_reg(reg_offset(layer, L0_CTRL), lx_ctrl);
	}
}

static inline void __lcdc_confirm_layer_setting(int layer)
{
	u32 lx_ctrl;

	lx_ctrl = lcdc_read_reg(reg_offset(layer, L0_CTRL));

	lx_ctrl |= LX_CTRL_CONFIRM;
	lcdc_write_reg(reg_offset(layer, L0_CTRL), lx_ctrl);
}

static inline void __lcdc_confirm_cursor_setting(void)
{
	u32 cur0_ctrl;

	cur0_ctrl = lcdc_read_reg(CUR0_CTRL);

	cur0_ctrl |= CUR0_SETTING_VALID;
	lcdc_write_reg(CUR0_CTRL, cur0_ctrl);
}


static inline int __lcdc_fmt_to_hwfmt(enum vdss_pixelformat fmt)
{
	switch (fmt) {
	case VDSS_PIXELFORMAT_565:
		return LO_CTRL_BPP_RGB565;
	case VDSS_PIXELFORMAT_556:
		return LO_CTRL_BPP_RGB556;
	case VDSS_PIXELFORMAT_655:
		return LO_CTRL_BPP_RGB655;
	case VDSS_PIXELFORMAT_666:
		return LO_CTRL_BPP_RGB666;
	case VDSS_PIXELFORMAT_BGRX_8880:
		return LO_CTRL_BPP_RGB888;
	case VDSS_PIXELFORMAT_8888:
		return LO_CTRL_BPP_ARGB8888;
	default:
		LCDC_ERR("%s(%d): unknown format 0x%x\n",
			__func__, __LINE__, fmt);
		break;
	}
	return LO_CTRL_BPP_UNKNOWN;
}

static inline int __lcdc_hwfmt_to_fmt(enum l0_ctrl_bpp hwfmt)
{
	switch (hwfmt) {
	case LO_CTRL_BPP_RGB565:
		return VDSS_PIXELFORMAT_565;
	case LO_CTRL_BPP_RGB556:
		return VDSS_PIXELFORMAT_556;
	case LO_CTRL_BPP_RGB655:
		return VDSS_PIXELFORMAT_655;
	case LO_CTRL_BPP_RGB666:
		return VDSS_PIXELFORMAT_666;
	case LO_CTRL_BPP_RGB888:
		return VDSS_PIXELFORMAT_BGRX_8880;
	case LO_CTRL_BPP_ARGB8888:
		return VDSS_PIXELFORMAT_8888;
	default:
		LCDC_ERR("%s(%d): unknown format 0x%x\n",
			__func__, __LINE__, hwfmt);
		break;
	}
	return VDSS_PIXELFORMAT_UNKNOWN;
}

static inline int __lcdc_fmt_to_bpp(enum vdss_pixelformat fmt)
{
	switch (fmt) {
	case VDSS_PIXELFORMAT_565:
	case VDSS_PIXELFORMAT_556:
	case VDSS_PIXELFORMAT_655:
		return 2;
	case VDSS_PIXELFORMAT_666:
	case VDSS_PIXELFORMAT_BGRX_8880:
	case VDSS_PIXELFORMAT_8888:
		return 4;
	default:
		LCDC_ERR("%s(%d): unknown format 0x%x\n",
			__func__, __LINE__, fmt);
		break;
	}
	return 2;
}


void __lcdc_get_screen_size(u32 *width, u32 *height,
			struct lcdc_panel_info *panel);
void __lcdc_config_screen(struct lcdc_panel_info *panel);
void __lcdc_power_up(u32 prim_base, enum vdss_pixelformat fmt,
				struct lcdc_panel_info *panel);
void __lcdc_boot_up(void *regs, unsigned long prim_base,
		unsigned int bpp, struct lcdc_panel_info *panel);


/* Default Replicate conversion setting */
#define LCDC_DEFAULT_REPLICATE		1
#define LCDC_DEFAULT_PREMULTI_ALPHA	1
#define LCDC_DEFAULT_REFRESH_RATE	60


#endif
