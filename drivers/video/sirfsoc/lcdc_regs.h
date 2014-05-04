/*
 * CSR sirfsoc LCD register def file
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __SIRFSOC_LCDC_REGS_H
#define __SIRFSOC_LCDC_REGS_H

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

enum s0_layer_sel {
	PRIMARY = 0,
	OVERLAY_1 = 1,
	OVERLAY_2 = 2,
	OVERLAY_3 = 3,
	LAYER_NUM = 4,
	CURSOR = 6,	/* Bit 6 of S0_LAYER_STATUS indicate cursor */
};

enum cur0_ctrl_mode {
	CURSOR_MODE_32x32x2_2_T = 0,
	CURSOR_MODE_32x32x2_4 = 1,
	CURSOR_MODE_32x32x2_3_T = 2,
	CURSOR_MODE_64x64x2_2_T = 4,
	CURSOR_MODE_64x64x2_4 = 5,
	CURSOR_MODE_64x64x2_3_T = 6,
};

enum l0_ctrl_bpp {
	LO_CTRL_BPP_RGB666 = 0,
	LO_CTRL_BPP_RGB565 = 1,
	LO_CTRL_BPP_RGB556 = 2,
	LO_CTRL_BPP_RGB655 = 3,
	LO_CTRL_BPP_RGB888 = 4,
	LO_CTRL_BPP_TRGB888 = 5,
	LO_CTRL_BPP_ARGB8888 = 6,
	LO_CTRL_BPP_UNKNOWN = 7,
};

enum s0_disp_mode_out_format {
	FORMAT_8_BIT_RBGRBG = 0,
	FORMAT_8_BIT_YUV422 = 1,
	FORMAT_16BIT_YUV422 = 2,
	FORMAT_18BIT_RBG666 = 3,
	FORMAT_24BIT_RBG888 = 4,
};


#define S0_VW_VSYNC_WIDTH(x)	(((x) & 0xFFF) << 0)
#define S0_VSYC_WIDTH_UINT	(1 << 12)

#define S0_OSC_DIV_RATIO_MASK	(0x3FF << 0)
#define S0_OSC_DIV_RATIO(x)	(((x) & 0x3FF) << 0)
#define S0_OSC_HALF_DUTY	(1 << 12)
#define S0_OSC_PCLK_CTRL	(1 << 16)

/* Timing Control */
#define S0_TIM_PCLK_IO		(1 << 1)
#define S0_TIM_PCLK_POLAR	(1 << 2)
#define S0_TIM_PCLK_EDGE	(1 << 3)
#define S0_TIM_HSYNC_IO		(1 << 4)
#define S0_TIM_HSYNC_POLAR	(1 << 5)
#define S0_TIM_VSYNC_IO		(1 << 6)
#define S0_TIM_VSYNC_POLAR	(1 << 7)
#define S0_TIM_PCLK_MASK	(1 << 8)
#define S0_TIM_HSYNC_MASK	(1 << 9)
#define S0_TIM_SYNC_DLY(x)	(((x) & 0x7) << 10)

/* Timing Control Status */
#define S0_TIM_RGB_SEQ_STA(x)	(((x) & 0x3) << 0)
#define S0_TIM_VSYNC_STA	(1 << 2)
#define S0_TIM_HSYNC_STA	(1 << 3)
#define S0_TIM_PCLK_STA		(1 << 4)

#define S0_BLANK_VALUE(x)	(((x) & 0xFFFFFF) << 0)
#define S0_BLANK_VALID		(1 << 24)

#define S0_BACK_COLOR_B(x)	(((x) & 0xFF) << 0)
#define S0_BACK_COLOR_G(x)	(((x) & 0xFF) << 8)
#define S0_BACK_COLOR_R(x)	(((x) & 0xFF) << 16)

/* Display Mode and Format */
#define S0_FRAME_VALID		(1 << 0)
#define S0_OUT_FORMAT_MASK	(0x7 << 1)
#define S0_OUT_FORMAT(x)	(((x) & 0x7) << 1)
#define S0_TOP_LAYER_MASK	(0x3 << 4)
#define S0_TOP_LAYER(x)		(((x) & 0x3) << 4)
#define S0_GAMMA_COR_EN		(1 << 7)

#define S0_LS_LAYER_SEL_MASK	(0xFF << 0)
#define S0_LS_LAYER_SEL(x)	(((x) & 0xFF) << 0)

#define S0_EVEN_RGB_MASK	(0x3F << 0)
#define S0_EVEN_RGB_SEQ(x)	(((x) & 0x3F) << 0)
#define S0_ODD_RGB_MASK		(0x3F << 6)
#define S0_ODD_RGB_SEQ(x)	(((x) & 0x3F) << 6)


/* RGB to YUV Conversion Control */
#define S0_YUV_SEQ(x)		(((x) & 0x3) << 6)
#define S0_RGB_YUV		(1 << 8)
#define S0_EVEN_UV		(1 << 9)
#define S0_EVEN_FIELD		(1 << 12)

#define S0_TV_HSTART(x)		(((x) & 0xFFF) << 0)
#define S0_TV_VSTART(x)		(((x) & 0x7FF) << 12)
#define S0_TV_F_VALID		(1 << 24)

#define S0_LINE_NUM(x)		(((x) & 0x7FF) << 0)
#define S0_INT_LINE_VALID	(1 << 12)
#define S0_INT_TV_MODE		(1 << 31)

#define S0_LAYER0_EN		(1 << 0)
#define S0_LAYER1_EN		(1 << 1)
#define S0_LAYER2_EN		(1 << 2)
#define S0_LAYER3_EN		(1 << 3)
#define S0_CURSOR_EN		(1 << 6)

#define L0_DMA_STAT		(1 << 0)
#define L1_DMA_STAT		(1 << 1)
#define L2_DMA_STAT		(1 << 2)
#define L3_DMA_STAT		(1 << 3)

#define L0_DMA_MASK		(1 << 0)
#define L1_DMA_MASK		(1 << 1)
#define L2_DMA_MASK		(1 << 2)
#define L3_DMA_MASK		(1 << 3)
#define L0_OFLOW_MASK		(1 << 6)
#define L1_OFLOW_MASK		(1 << 7)
#define L2_OFLOW_MASK		(1 << 8)
#define L3_OFLOW_MASK		(1 << 9)
#define L0_UFLOW_MASK		(1 << 12)
#define L1_UFLOW_MASK		(1 << 13)
#define L2_UFLOW_MASK		(1 << 14)
#define L3_UFLOW_MASK		(1 << 15)
#define S0_LINE_INT_MASK	(1 << 18)
#define L0_UNFINISH_MASK	(1 << 28)
#define L1_UNFINISH_MASK	(1 << 29)
#define L2_UNFINISH_MASK	(1 << 30)
#define L3_UNFINISH_MASK	(1 << 31)

#define L0_DMA_INT		(1 << 0)
#define L1_DMA_INT		(1 << 1)
#define L2_DMA_INT		(1 << 2)
#define L3_DMA_INT		(1 << 3)
#define L0_OFLOW_INT		(1 << 6)
#define L1_OFLOW_INT		(1 << 7)
#define L2_OFLOW_INT		(1 << 8)
#define L3_OFLOW_INT		(1 << 9)
#define L0_UFLOW_INT		(1 << 12)
#define L1_UFLOW_INT		(1 << 13)
#define L2_UFLOW_INT		(1 << 14)
#define L3_UFLOW_INT		(1 << 15)
#define S0_LINE_INT_INT		(1 << 18)
#define L0_UNFINISH_INT		(1 << 28)
#define L1_UNFINISH_INT		(1 << 29)
#define L2_UNFINISH_INT		(1 << 30)
#define L3_UNFINISH_INT		(1 << 31)

#define SCREEN0_EN		(1 << 0)
#define EN_DELAY_MODE		(1 << 1)

/* Layer Control */
#define LX_CTRL_BPP_MASK	(0x7 << 0)
#define LX_CTRL_BPP(x)		(((x) & 0x7) << 0)
#define LX_CTRL_FIFO_RESET	(1 << 5)
#define LX_CTRL_SRC_CKEY_EN	(1 << 6)
#define LX_CTRL_FIFO_FKRDY	(1 << 7)
#define LX_CTRL_CONFIRM		(1 << 8)
#define LX_CTRL_GLOBAL_ALPHA	(1 << 9)
#define LX_CTRL_REPLICATE	(1 << 10)
#define LX_CTRL_DST_CKEY_EN	(1 << 11)
#define LX_CTRL_PREMULTI_ALPHA	(1 << 12)
#define LX_CTRL_SOURCE_ALPHA	(1 << 13)

#define LX_HSTART(x)		(((x) & 0xFFF) << 0)
#define LX_VSTART(x)		(((x) & 0x7FF) << 0)
#define LX_HEND(x)		(((x) & 0xFFF) << 0)
#define LX_VEND(x)		(((x) & 0x7FF) << 0)

#define LX_XSIZE(x)		(((x) & 0x1FFF) << 0)
#define LX_YSIZE(x)		(((x) & 0x1FFF) << 0)

#define LX_SKIP(x)		(((x) & 0x1FFF) << 0)

#define LX_DMA_MODE		(1 << 1)
#define LX_DMA_CHAIN_MODE	(1 << 2)
#define LX_DMA_UNIT_MASK	(0xF << 4)
#define LX_DMA_UNIT(x)		(((x) & 0xF) << 4)
#define LX_SUPPRESS_QW_NUM_MASK	(0xF << 8)
#define LX_SUPPRESS_QW_NUM(x)	(((x) & 0xF) << 8)
#define LX_DMA_HURRY		(1 << 30)
#define LX_VPP_PASS_MODE	(1 << 31)

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
#define LX_REQ_SEL		(1 << 24)

#define LX_FIFO_LEN(x)		(((x) & 0xFF) << 0)

/* Cursor */
#define CUR0_CTRL_MODE(x)	(((x) & 0x7) << 0)
#define CUR0_DWORD_BLE		(1 << 4)
#define CUR0_BYTE_BLE		(1 << 5)
#define CUR0_SRAM_ADDRST		(1 << 8)
#define CUR0_SETTING_VALID	(1 << 16)

#define CUR0_COLOR_B(x)		(((x) & 0xFF) << 0)
#define CUR0_COLOR_G(x)		(((x) & 0xFF) << 8)
#define CUR0_COLOR_R(x)		(((x) & 0xFF) << 16)

#define CUR0_ALPHA_VAL(x)	(((x) & 0xFF) << 0)

#define CUR0_CUR_X(x)		(((x) & 0x7F) << 0)
#define CUR0_CUR_Y(x)		(((x) & 0x7F) << 16)


#endif
