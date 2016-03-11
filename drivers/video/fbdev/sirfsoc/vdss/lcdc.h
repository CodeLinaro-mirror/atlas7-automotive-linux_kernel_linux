/*
 * CSR sirfsoc LCD internal interface
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

#define WB_CTRL			0x0064
#define S0_RGB_YUV_OFFSET	0x0070
#define S0_LAYER_SEL_SET	0x0074
#define S0_LAYER_SEL_CLR	0x0078
#define S0_FRONT_INT_LINE	0x007C
#define FRONT_INT_MASK		0x00D8
#define FRONT_INT_CTRL_STATUS	0x00DC
#define FRONT_INT_MASK_SET	0x00E0
#define FRONT_INT_MASK_CLR	0x00E4
#define INT_MASK_SET		0x00E8
#define INT_MASK_CLR		0x00EC

#define DMA_STATUS		0x00F0
#define INT_MASK		0x00F4
#define INT_CTRL_STATUS		0x00F8
#define SCR_CTRL		0x00FC

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

#define L0_DMA_ACCCNT		0x0158
#define L0_YUV2RGB_RCOEF	0x0160
#define L0_YUV2RGB_GCOEF	0x0164
#define L0_YUV2RGB_BCOEF	0x0168
#define L0_YUV2RGB_OFFSET1	0x016C
#define L0_YUV2RGB_OFFSET2	0x0170
#define L0_YUV2RGB_OFFSET3	0x0174

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

#define L1_DMA_ACCCNT		0x0258
#define L1_YUV2RGB_RCOEF	0x0260
#define L1_YUV2RGB_GCOEF	0x0264
#define L1_YUV2RGB_BCOEF	0x0268
#define L1_YUV2RGB_OFFSET1	0x026C
#define L1_YUV2RGB_OFFSET2	0x0270
#define L1_YUV2RGB_OFFSET3	0x0274

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

#define L2_DMA_ACCCNT		0x0358
#define L2_YUV2RGB_RCOEF	0x0360
#define L2_YUV2RGB_GCOEF	0x0364
#define L2_YUV2RGB_BCOEF	0x0368
#define L2_YUV2RGB_OFFSET1	0x036C
#define L2_YUV2RGB_OFFSET2	0x0370
#define L2_YUV2RGB_OFFSET3	0x0374

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

#define L3_DMA_ACCCNT		0x0458
#define L3_YUV2RGB_RCOEF	0x0460
#define L3_YUV2RGB_GCOEF	0x0464
#define L3_YUV2RGB_BCOEF	0x0468
#define L3_YUV2RGB_OFFSET1	0x046C
#define L3_YUV2RGB_OFFSET2	0x0470
#define L3_YUV2RGB_OFFSET3	0x0474

#define S0_GAMMAFIFO_R		0x0800
#define S0_GAMMAFIFO_G		0x0900
#define S0_GAMMAFIFO_B		0x0a00

#define BLS_CTRL1		0x0b00
#define BLS_CTRL2		0x0b04
#define BLS_STATUS		0x0b08
#define CRC_VALUE		0x0b0c
#define BLS_LEVEL_TB0		0x0b10
#define BLS_LEVEL_TB1		0x0b14
#define BLS_LEVEL_TB2		0x0b18
#define BLS_LEVEL_TB3		0x0b1c

#define ED_MODE			0x0c00
#define ED_PERFORM		0x0c04
#define ED_START_STATE		0x0c08
#define INVERSEDATA		0x0c0C
#define ED_LFSR_ENABLE		0x0c10
#define ED_POLYNOMIAL		0x0c14
#define ED_LFSR_STEPS		0x0c18
#define ED_LEFTALIGN		0x0c1C
#define BYPASS_ED		0x0c20

#define PADMUX_LDD_0		0x0D00
#define PADMUX_LDD_1		0x0D04
#define PADMUX_LDD_2		0x0D08
#define PADMUX_LDD_3		0x0D0C
#define PADMUX_LDD_4		0x0D10
#define PADMUX_LDD_5		0x0D14
#define PADMUX_LDD_6		0x0D18
#define PADMUX_LDD_7		0x0D1C
#define PADMUX_LDD_8		0x0D20
#define PADMUX_LDD_9		0x0D24
#define PADMUX_LDD_10		0x0D28
#define PADMUX_LDD_11		0x0D2C
#define PADMUX_LDD_12		0x0D30
#define PADMUX_LDD_13		0x0D34
#define PADMUX_LDD_14		0x0D38
#define PADMUX_LDD_15		0x0D3C
#define PADMUX_LDD_16		0x0D40
#define PADMUX_LDD_17		0x0D44
#define PADMUX_LDD_18		0x0D48
#define PADMUX_LDD_19		0x0D4C
#define PADMUX_LDD_20		0x0D50
#define PADMUX_LDD_21		0x0D54
#define PADMUX_LDD_22		0x0D58
#define PADMUX_LDD_23		0x0D5C
#define PADMUX_L_DE		0x0D60
#define PADMUX_L_LCK		0x0D64
#define PADMUX_L_FCK		0x0D68
#define PADMUX_L_PCLK		0x0D6C
#define PADMUX_OUT_MUX		0x0D70
#define PADMUX_DELAY_CFG	0x0D74

#define STRS_CONTROL		0x0d80
#define STRS0_VAL		0x0d84
#define STRS1_VAL		0x0d88
#define STRS2_VAL		0x0d8C
#define STRS3_VAL		0x0d90

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

#define S0_GAMMAFIFO_READ_R	0x1800
#define S0_GAMMAFIFO_READ_G	0x1900
#define S0_GAMMAFIFO_READ_B	0x1a00

#define DMAN_ADDR		0x4000
#define DMAN_XLEN		0x4004
#define DMAN_YLEN		0x4008
#define DMAN_CTRL		0x400C
#define DMAN_WIDTH		0x4010
#define DMAN_VALID		0x4014
#define DMAN_INT		0x4018
#define DMAN_INT_EN		0x401C
#define DMAN_LOOP_CTRL		0x4020
#define DMAN_INT_CNT		0x4024
#define DMAN_TIMEOUT_CNT	0x4028
#define DMAN_PAU_TIME_CNT	0x402C
#define DMAN_CUR_TABLE_ADDR	0x4030
#define DMAN_CUR_DATA_ADDR	0x4034
#define DMAN_MUL		0x4038
#define DMAN_STATE0		0x403C
#define DMAN_STATE1		0x4040

#define WB_DMA_PENDING_START_ADDR	0x5000

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
#define S0_LVDS_STOP_PCKL	BIT(17)
#define S0_LVDS_PCLK_NON_PAUSE	BIT(18)

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
#define S0_TV_EXT_FIELD		BIT(28)

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
#define S0_WB_OVERFLOW_MASK	BIT(19)
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
#define S0_WB_OVERFLOW_INT	BIT(19)
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
#define LX_CTRL_ENDIANMODE	BIT(16)
#define LX_CTRL_YUV422_FORMAT_MASK	(0x3 << 17)
#define LX_CTRL_YUV422_FORMAT(x)	(((x) & 0x3) << 17)
#define LX_CTRL_LATCH_IMMEDIATE	BIT(31)

#define LX_HSTART(x)		(((x) & 0xFFF) << 0)
#define LX_VSTART(x)		(((x) & 0x7FF) << 0)
#define LX_HEND(x)		(((x) & 0xFFF) << 0)
#define LX_VEND(x)		(((x) & 0x7FF) << 0)
#define LX_XSIZE(x)		(((x) & 0x1FFF) << 0)
#define LX_YSIZE(x)		(((x) & 0x1FFF) << 0)
#define LX_SKIP(x)		(((x) & 0x1FFF) << 0)

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
#define LX_LO_CHK_A7(x)		(((x) & 0x1FF) << 0)
#define LX_LO_CHK_MSB_A7(x)	(((x) & 0x1) << 9)
#define LX_MI_CHK_A7(x)		(((x) & 0x1FF) << 16)
#define LX_MI_CHK_MSB_A7(x)	(((x) & 0x1) << 25)
#define LX_DB_SZ_EN_A7		BIT(30)
#define LX_REQ_SEL_A7		BIT(31)

#define LCDC_ERR(fmt, ...)	pr_err(fmt, ## __VA_ARGS__)
#define LCDC_DEBUG(fmt, ...)	pr_debug(fmt, ## __VA_ARGS__)
#define LCDC_ENTRY(fmt, ...)

/* Screen error-diffusion configuration */
#define DEFAULT_POLY_COEF 0x6801
#define ED_LFSR_EN 0x1
#define ED_BY_PASS_EN 0xf

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

enum lcdc_ed_perform {
	ED_CUT_LSB_ONLY,
	ED_FLOYD_STEINBERG,
	ED_HORIZONTAL,
	ED_BYPASS,
};

enum lcdc_ed_start_state {
	START_WITH_ZERO,
	START_WITH_PREVIOUS_FRAME,
};

enum lcdc_ed_left_align {
	RIGHT_ALIGN,
	LEFT_ALIGN,
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

unsigned int lcdc_read_reg(u32 lcdc_index, unsigned int offset);
void lcdc_write_reg(u32 lcdc_index, unsigned int offset, unsigned int value);
unsigned long lcdc_clk_get_rate(u32 lcdc_index);

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

static inline void __lcdc_reset_layer_fifo(u32 lcdc_index, int layer)
{
	u32 lx_ctrl;

	lx_ctrl = lcdc_read_reg(lcdc_index, reg_offset(layer, L0_CTRL));

	lx_ctrl |= LX_CTRL_FIFO_RESET;
	lcdc_write_reg(lcdc_index, reg_offset(layer, L0_CTRL), lx_ctrl);

	lx_ctrl &= ~LX_CTRL_FIFO_RESET;
	lcdc_write_reg(lcdc_index, reg_offset(layer, L0_CTRL), lx_ctrl);
}

static inline void __lcdc_confirm_layer_setting(u32 lcdc_index, int layer)
{
	u32 lx_ctrl;

	lx_ctrl = lcdc_read_reg(lcdc_index, reg_offset(layer, L0_CTRL));

	lx_ctrl |= LX_CTRL_CONFIRM;
	lcdc_write_reg(lcdc_index, reg_offset(layer, L0_CTRL), lx_ctrl);
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
