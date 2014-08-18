/*
 * CSR sirfsoc LCD internal interface
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __SIRFSOC_LCDC__H
#define __SIRFSOC_LCDC__H

#include <linux/string.h>
#include <linux/delay.h>
#include <linux/io.h>

/*
 * LCD register definition
 */

#define S0_HSYNC_PERIOD		0x0000
#define S0_HSYNC_WIDTH		0x0004
#define S0_VSYNC_PERIOD		0x0008
#define S0_VSYNC_WIDTH		0x000c
#define S0_ACT_HSTART		0x0010
#define S0_ACT_VSTART		0x0014
#define S0_ACT_HEND		0x0018
#define S0_ACT_VEND		0x001c
#define S0_OSC_RATIO		0x0020
#define S0_TIM_CTRL		0x0024
#define S0_TIM_STATUS		0x0028
#define S0_HCOUNT		0x002c
#define S0_VCOUNT		0x0030
#define S0_BLANK		0x0034
#define S0_BACK_COLOR		0x0038
#define S0_DISP_MODE		0x003c
#define S0_LAYER_SEL		0x0040
#define S0_RGB_SEQ		0x0044
#define S0_RGB_YUV_COEF1	0x0048
#define S0_RGB_YUV_COEF2	0x004c
#define S0_RGB_YUV_COEF3	0x0050
#define S0_YUV_CTRL		0x0054
#define S0_TV_FIELD		0x0058
#define S0_INT_LINE		0x005c
#define S0_LAYER_STATUS		0x0060
#define S0_RGB_YUV_OFFSET	0x0070

#define BLS_CTRL1		0x0b00
#define BLS_CTRL2		0x0b04
#define BLS_STATUS		0x0b08
#define CRC_VALUE		0x0b0c
#define BLS_LEVEL_TB0		0x0b10
#define BLS_LEVEL_TB1		0x0b14
#define BLS_LEVEL_TB2		0x0b18
#define BLS_LEVEL_TB3		0x0b1c

#define DMA_STATUS		0x00f0
#define INT_MASK		0x00f4
#define INT_CTRL_STATUS		0x00f8
#define SCR_CTRL		0x00fc

#define L0_CTRL			0x0100
#define L0_HSTART		0x0104
#define L0_VSTART		0x0108
#define L0_HEND			0x010c
#define L0_VEND			0x0110
#define L0_BASE0		0x0114
#define L0_BASE1		0x0118
#define L0_XSIZE		0x011c
#define L0_YSIZE		0x0120
#define L0_SKIP			0x0124
#define L0_DMA_CTRL		0x0128
#define L0_ALPHA		0x012c
#define L0_CKEYB_SRC		0x0130
#define L0_CKEYS_SRC		0x0134
#define L0_FIFO_CHK		0x0138
#define L0_FIFO_STATUS		0x013c
#define L0_CKEYB_DST		0x0150
#define L0_CKEYS_DST		0x0154


#define L1_CTRL			0x0200
#define L1_HSTART		0x0204
#define L1_VSTART		0x0208
#define L1_HEND			0x020c
#define L1_VEND			0x0210
#define L1_BASE0		0x0214
#define L1_BASE1		0x0218
#define L1_XSIZE		0x021c
#define L1_YSIZE		0x0220
#define L1_SKIP			0x0224
#define L1_DMA_CTRL		0x0228
#define L1_ALPHA		0x022c
#define L1_CKEYB_SRC		0x0230
#define L1_CKEYS_SRC		0x0234
#define L1_FIFO_CHK		0x0238
#define L1_FIFO_STATUS		0x023c
#define L1_CKEYB_DST		0x0250
#define L1_CKEYS_DST		0x0254

#define L2_CTRL			0x0300
#define L2_HSTART		0x0304
#define L2_VSTART		0x0308
#define L2_HEND			0x030c
#define L2_VEND			0x0310
#define L2_BASE0		0x0314
#define L2_BASE1		0x0318
#define L2_XSIZE		0x031c
#define L2_YSIZE		0x0320
#define L2_SKIP			0x0324
#define L2_DMA_CTRL		0x0328
#define L2_ALPHA		0x032c
#define L2_CKEYB_SRC		0x0330
#define L2_CKEYS_SRC		0x0334
#define L2_FIFO_CHK		0x0338
#define L2_FIFO_STATUS		0x033c
#define L2_CKEYB_DST		0x0350
#define L2_CKEYS_DST		0x0354

#define L3_CTRL			0x0400
#define L3_HSTART		0x0404
#define L3_VSTART		0x0408
#define L3_HEND			0x040c
#define L3_VEND			0x0410
#define L3_BASE0		0x0414
#define L3_BASE1		0x0418
#define L3_XSIZE		0x041c
#define L3_YSIZE		0x0420
#define L3_SKIP			0x0424
#define L3_DMA_CTRL		0x0428
#define L3_ALPHA		0x042c
#define L3_CKEYB_SRC		0x0430
#define L3_CKEYS_SRC		0x0434
#define L3_FIFO_CHK		0x0438
#define L3_FIFO_STATUS		0x043c
#define L3_CKEYB_DST		0x0450
#define L3_CKEYS_DST		0x0454

#define S0_GAMMAFIFO_R		0x0800
#define S0_GAMMAFIFO_G		0x0900
#define S0_GAMMAFIFO_B		0x0a00

#define CUR0_CTRL		0x1000
#define CUR0_HSTART		0x1004
#define CUR0_VSTART		0x1008
#define CUR0_HEND		0x100c
#define CUR0_VEND		0x1010
#define CUR0_COLOR0		0x1014
#define CUR0_COLOR1		0x1018
#define CUR0_COLOR2		0x101c
#define CUR0_COLOR3		0x1020
#define CUR0_ALPHA		0x1024
#define CUR0_FIFO_RDPTR		0x1028
#define CUR0_CURRENT_XY		0x102C
#define CUR0_FIFODATA		0x1400

#define LCDC_LAYER_REG_SHIFT	8

#define LCDC_LAYER_REG_SPACE	(L1_CTRL - L0_CTRL)
#define LCDC_LAYER_REG_NUM	(L0_FIFO_STATUS - L0_CTRL)
#define LCDC_LAYER_NUM		0x4

#define LCDC_INT_MASK_ALL_OFF	0x0
#define LCDC_INT_MASK_ALL_ON	0xffffffff



#define S0_VW_VSYNC_WIDTH(x)	(((x) & 0xFFF) << 0)
#define S0_VSYC_WIDTH_UINT	BIT(12)

#define S0_OSC_DIV_RATIO_MASK	(0x3FF << 0)
#define S0_OSC_DIV_RATIO(x)	(((x) & 0x3FF) << 0)
#define S0_OSC_HALF_DUTY	BIT(12)
#define S0_OSC_PCLK_CTRL	BIT(16)

/* Timing Control */
#define S0_TIM_PCLK_IO		BIT(1)
#define S0_TIM_PCLK_POLAR	BIT(2)
#define S0_TIM_PCLK_EDGE	BIT(3)
#define S0_TIM_HSYNC_IO		BIT(4)
#define S0_TIM_HSYNC_POLAR	BIT(5)
#define S0_TIM_VSYNC_IO		BIT(6)
#define S0_TIM_VSYNC_POLAR	BIT(7)
#define S0_TIM_PCLK_MASK	BIT(8)
#define S0_TIM_HSYNC_MASK	BIT(9)
#define S0_TIM_SYNC_DLY(x)	(((x) & 0x7) << 10)

/* Timing Control Status */
#define S0_TIM_RGB_SEQ_STA(x)	(((x) & 0x3) << 0)
#define S0_TIM_VSYNC_STA	BIT(2)
#define S0_TIM_HSYNC_STA	BIT(3)
#define S0_TIM_PCLK_STA		BIT(4)

#define S0_BLANK_VALUE(x)	(((x) & 0xFFFFFF) << 0)
#define S0_BLANK_VALID		BIT(24)

#define S0_BACK_COLOR_B(x)	(((x) & 0xFF) << 0)
#define S0_BACK_COLOR_G(x)	(((x) & 0xFF) << 8)
#define S0_BACK_COLOR_R(x)	(((x) & 0xFF) << 16)

/* Display Mode and Format */
#define S0_FRAME_VALID		(1 << 0)
#define S0_OUT_FORMAT_MASK	(0x7 << 1)
#define S0_OUT_FORMAT(x)	(((x) & 0x7) << 1)
#define S0_TOP_LAYER_MASK	(0x3 << 4)
#define S0_TOP_LAYER(x)		(((x) & 0x3) << 4)
#define S0_GAMMA_COR_EN		BIT(7)

#define S0_LS_LAYER_SEL_MASK	(0xFF << 0)
#define S0_LS_LAYER_SEL(x)	(((x) & 0xFF) << 0)

#define S0_EVEN_RGB_MASK	(0x3F << 0)
#define S0_EVEN_RGB_SEQ(x)	(((x) & 0x3F) << 0)
#define S0_ODD_RGB_MASK		(0x3F << 6)
#define S0_ODD_RGB_SEQ(x)	(((x) & 0x3F) << 6)
#define S0_RGB_SEQ_RGB		0x186
#define S0_RGB_SEQ_BGR		0x924
#define S0_RGB_SEQ_BRG		0x861

/* RGB to YUV Conversion Control */
#define S0_YUV_SEQ(x)		(((x) & 0x3) << 6)
#define S0_RGB_YUV		BIT(8)
#define S0_EVEN_UV		BIT(9)
#define S0_EVEN_FIELD		BIT(12)

#define S0_TV_HSTART(x)		(((x) & 0xFFF) << 0)
#define S0_TV_VSTART(x)		(((x) & 0x7FF) << 12)
#define S0_TV_F_VALID		BIT(24)

#define S0_LINE_NUM(x)		(((x) & 0x7FF) << 0)
#define S0_INT_LINE_VALID	BIT(12)
#define S0_INT_TV_MODE		BIT(31)

#define S0_LAYER0_EN		BIT(0)
#define S0_LAYER1_EN		BIT(1)
#define S0_LAYER2_EN		BIT(2)
#define S0_LAYER3_EN		BIT(3)
#define S0_CURSOR_EN		BIT(6)

#define L0_DMA_STAT		BIT(0)
#define L1_DMA_STAT		BIT(1)
#define L2_DMA_STAT		BIT(2)
#define L3_DMA_STAT		BIT(3)

#define L0_DMA_MASK		BIT(0)
#define L1_DMA_MASK		BIT(1)
#define L2_DMA_MASK		BIT(2)
#define L3_DMA_MASK		BIT(3)
#define L0_OFLOW_MASK		BIT(6)
#define L1_OFLOW_MASK		BIT(7)
#define L2_OFLOW_MASK		BIT(8)
#define L3_OFLOW_MASK		BIT(9)
#define L0_UFLOW_MASK		BIT(12)
#define L1_UFLOW_MASK		BIT(13)
#define L2_UFLOW_MASK		BIT(14)
#define L3_UFLOW_MASK		BIT(15)
#define S0_LINE_INT_MASK	BIT(18)
#define L0_UNFINISH_MASK	BIT(28)
#define L1_UNFINISH_MASK	BIT(29)
#define L2_UNFINISH_MASK	BIT(30)
#define L3_UNFINISH_MASK	BIT(31)

#define L0_DMA_INT		BIT(0)
#define L1_DMA_INT		BIT(1)
#define L2_DMA_INT		BIT(2)
#define L3_DMA_INT		BIT(3)
#define L0_OFLOW_INT		BIT(6)
#define L1_OFLOW_INT		BIT(7)
#define L2_OFLOW_INT		BIT(8)
#define L3_OFLOW_INT		BIT(9)
#define L0_UFLOW_INT		BIT(12)
#define L1_UFLOW_INT		BIT(13)
#define L2_UFLOW_INT		BIT(14)
#define L3_UFLOW_INT		BIT(15)
#define S0_LINE_INT_INT		BIT(18)
#define L0_UNFINISH_INT		BIT(28)
#define L1_UNFINISH_INT		BIT(29)
#define L2_UNFINISH_INT		BIT(30)
#define L3_UNFINISH_INT		BIT(31)

#define SCREEN0_EN		BIT(0)
#define EN_DELAY_MODE		BIT(1)

/* Layer Control */
#define LX_CTRL_BPP_MASK	(0x7 << 0)
#define LX_CTRL_BPP(x)		(((x) & 0x7) << 0)
#define LX_CTRL_FIFO_RESET	BIT(5)
#define LX_CTRL_SRC_CKEY_EN	BIT(6)
#define LX_CTRL_FIFO_FKRDY	BIT(7)
#define LX_CTRL_CONFIRM		BIT(8)
#define LX_CTRL_GLOBAL_ALPHA	BIT(9)
#define LX_CTRL_REPLICATE	BIT(10)
#define LX_CTRL_DST_CKEY_EN	BIT(11)
#define LX_CTRL_PREMULTI_ALPHA	BIT(12)
#define LX_CTRL_SOURCE_ALPHA	BIT(13)

#define LX_DMA_MODE		BIT(1)
#define LX_DMA_CHAIN_MODE	BIT(2)
#define LX_DMA_UNIT_MASK	(0xF << 4)
#define LX_DMA_UNIT(x)		(((x) & 0xF) << 4)
#define LX_SUPPRESS_QW_NUM_MASK	(0xF << 8)
#define LX_SUPPRESS_QW_NUM(x)	(((x) & 0xF) << 8)
#define LX_DMA_HURRY		BIT(30)
#define LX_VPP_PASS_MODE	BIT(31)

#define LX_ALPHA_VAL_MASK	(0xFF << 0)
#define LX_ALPHA_VAL(x)		(((x) & 0xFF) << 0)

#define LX_CKEY_B_MASK		(0xFF << 0)
#define LX_CKEY_B(x)		(((x) & 0xFF) << 0)
#define LX_CKEY_G_MASK		(0xFF << 8)
#define LX_CKEY_G(x)		(((x) & 0xFF) << 8)
#define LX_CKEY_R_MASK		(0xFF << 16)
#define LX_CKEY_R(x)		(((x) & 0xFF) << 16)

/* Screen FIFO Control */
#define LX_LO_CHK(x)		(((x) & 0xFF) << 0)
#define LX_MI_CHK(x)		(((x) & 0xFF) << 8)
#define LX_REQ_SEL		BIT(24)

#define LCDC_ERR(fmt, ...)	pr_err(fmt, ## __VA_ARGS__)
#define LCDC_DEBUG(fmt, ...)	pr_debug(fmt, ## __VA_ARGS__)
#define LCDC_ENTRY(fmt, ...)
#define LCDC_DUMP(fmt, ...)	pr_info(fmt, ## __VA_ARGS__)

enum s0_layer_sel {
	PRIMARY = 0,
	OVERLAY_1,
	OVERLAY_2,
	OVERLAY_3,
	LAYER_NUM,
	CURSOR = 6,	/* Bit 6 of S0_LAYER_STATUS indicate cursor */
};

enum cur0_ctrl_mode {
	CURSOR_MODE_32x32x2_2_T,
	CURSOR_MODE_32x32x2_4,
	CURSOR_MODE_32x32x2_3_T,
	CURSOR_MODE_64x64x2_2_T,
	CURSOR_MODE_64x64x2_4,
	CURSOR_MODE_64x64x2_3_T,
};

enum l0_ctrl_bpp {
	LO_CTRL_BPP_RGB666,
	LO_CTRL_BPP_RGB565,
	LO_CTRL_BPP_RGB556,
	LO_CTRL_BPP_RGB655,
	LO_CTRL_BPP_RGB888,
	LO_CTRL_BPP_TRGB888,
	LO_CTRL_BPP_ARGB8888,
	LO_CTRL_BPP_UNKNOWN,
};

enum s0_disp_mode_out_format {
	FORMAT_8_BIT_RBGRBG,
	FORMAT_8_BIT_YUV422,
	FORMAT_16BIT_YUV422,
	FORMAT_18BIT_RBG666,
	FORMAT_24BIT_RBG888,
};

enum lcdc_out_format {
	LCDC_OUT_8_BIT_RBGRBG,
	LCDC_OUT_8_BIT_YUV422,
	LCDC_OUT_16BIT_YUV422,
	LCDC_OUT_18BIT_RBG666,
	LCDC_OUT_24BIT_RBG888,
};

enum lcdc_interrupt_type {
	LCDC_INTERRUPT_L0_DMA = 0,
	LCDC_INTERRUPT_L1_DMA,
	LCDC_INTERRUPT_L2_DMA,
	LCDC_INTERRUPT_L3_DMA,
	LCDC_INTERRUPT_L0_OFLOW = 6,
	LCDC_INTERRUPT_L1_OFLOW,
	LCDC_INTERRUPT_L2_OFLOW,
	LCDC_INTERRUPT_L3_OFLOW,
	LCDC_INTERRUPT_L0_UFLOW = 12,
	LCDC_INTERRUPT_L1_UFLOW,
	LCDC_INTERRUPT_L2_UFLOW,
	LCDC_INTERRUPT_L3_UFLOW,
	LCDC_INTERRUPT_VSYNC = 18,
	LCDC_INTERRUPT_ALL = 0xFFFFFFFF,
};

static unsigned int hwfmt_to_bpp[] = {
	4,	/* LO_CTRL_BPP_RGB666 */
	2,	/* LO_CTRL_BPP_RGB565 */
	2,	/* LO_CTRL_BPP_RGB556 */
	2,	/* LO_CTRL_BPP_RGB655 */
	4,	/* LO_CTRL_BPP_RGB888 */
	4,	/* LO_CTRL_BPP_TRGB888 */
	4,	/* LO_CTRL_BPP_ARGB8888 */
	2,	/* LO_CTRL_BPP_UNKNOWN */
};

unsigned int lcdc_read_reg(unsigned int offset);
void lcdc_write_reg(unsigned int offset, unsigned int value);
unsigned long lcdc_clk_get_rate(void);

static inline unsigned int reg_offset(int layer, unsigned int reg_offset)
{
	return reg_offset + (layer << LCDC_LAYER_REG_SHIFT);
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

static inline void __lcdc_confirm_layer_setting(int layer)
{
	u32 lx_ctrl;

	lx_ctrl = lcdc_read_reg(reg_offset(layer, L0_CTRL));

	lx_ctrl |= LX_CTRL_CONFIRM;
	lcdc_write_reg(reg_offset(layer, L0_CTRL), lx_ctrl);
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
#endif
