/*
 * CSR SiRFprima2 VIP hardware registers
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __SIRFSOC_VIP_REGS_H
#define __SIRFSOC_VIP_REGS_H

#define CAM_COUNT               0x0000
#define CAM_INT_COUNT           0x0004
#define CAM_START               0x0008
#define CAM_END                 0x000C
#define CAM_CTRL                0x0010
#define CAM_PIXEL_SHIFT         0x0014
#define CAM_YUV_COEFR           0x0018
#define CAM_YUV_COEFG           0x001C
#define CAM_YUV_COEFB           0x0020
#define CAM_YUV_OFFSET          0x0024
#define CAM_INT_EN              0x0028
#define CAM_INT_CTRL            0x002C
#define CAM_VSYNC_CTRL          0x0030
#define CAM_HSYNC_CTRL          0x0034
#define CAM_PXCLK_CTRL          0x0038
#define CAM_VSYNC_HSYNC         0x003C
#define CAM_TIMING_CTRL         0x0040
#define CAM_DMA_CTRL            0x0044
#define CAM_DMA_LEN             0x0048
#define CAM_FIFO_CTRL_REG       0x004C
#define CAM_FIFO_LEVEL_CHECK    0x0050
#define CAM_FIFO_OP_REG         0x0054
#define CAM_FIFO_STATUS_REG     0x0058
#define CAM_RD_FIFO_DATA        0x005C
#define CAM_TS_CTRL             0x0060

#define CAM_COUNT_XC_MASK	(0xFFFF << 0)
#define CAM_COUNT_YC_MASK	(0xFFFF << 16)

#define CAM_INT_COUNT_XI_MASK	(0xFFFF << 0)
#define CAM_INT_COUNT_XI(x)	(((x) & 0xFFFF) << 0)
#define CAM_INT_COUNT_YI_MASK	(0xFFFF << 16)
#define CAM_INT_COUNT_YI(x)	(((x) & 0xFFFF) << 16)

#define CAM_START_XS_MASK	(0xFFFF << 0)
#define CAM_START_XS(x)		(((x) & 0xFFFF) << 0)
#define CAM_START_YS_MASK	(0xFFFF << 16)
#define CAM_START_YS(x)		(((x) & 0xFFFF) << 16)

#define CAM_END_XE_MASK		(0xFFFF << 0)
#define CAM_END_XE(x)		(((x) & 0xFFFF) << 0)
#define CAM_END_YE_MASK		(0xFFFF << 16)
#define CAM_END_YE(x)		(((x) & 0xFFFF) << 16)

#define CAM_CTRL_PXCLK_CTRL		(1 << 0)
#define CAM_CTRL_HSYNC_CTRL		(1 << 1)
#define CAM_CTRL_VSYNC_CTRL		(1 << 2)
#define CAM_CTRL_PIXCLK_INV		(1 << 3)
#define CAM_CTRL_HSYNC_INV		(1 << 4)
#define CAM_CTRL_VSYNC_INV		(1 << 5)
#define CAM_CTRL_SINGLE			(1 << 6)
#define CAM_CTRL_IO_TRIGGER		(1 << 7)
#define CAM_CTRL_YUV_YCRCB		(1 << 8)
#define CAM_CTRL_YUV_FORMAT_MASK	(0x7 << 9)
#define CAM_CTRL_YUV_FORMAT(x)		(((x) & 0x7) << 9)
#define CAM_CTRL_OUT_FORMAT_MASK	(0x3 << 12)
#define CAM_CTRL_OUT_FORMAT(x)		(((x) & 0x3) << 12)
#define CAM_CTRL_YUVRGB			(1 << 14)
#define CAM_CTRL_X_SCA_MASK		(0x3 << 16)
#define CAM_CTRL_X_SCA(x)		(((x) & 0x3) << 16)
#define CAM_CTRL_Y_SCA_MASK		(0x3 << 18)
#define CAM_CTRL_Y_SCA(x)		(((x) & 0x3) << 18)
#define CAM_CTRL_HOR_MIRROR		(1 << 20)
#define CAM_CTRL_CAP_FROM_ODD		(1 << 21)
#define CAM_CTRL_CAP_FROM_EVEN		(1 << 22)
#define CAM_CTRL_PAD_MUX_ON_UPLI	(1 << 23)
#define CAM_CTRL_CCIR656_EN		(1 << 24)
#define CAM_CTRL_FID			(1 << 25)
#define CAM_CTRL_INIT			(1 << 31)

#define CAM_PS_PIXEL_SHIFT_MASK	(0x7 << 0)
#define CAM_PS_PIXEL_SHIFT(x)	(((x) & 0x7) << 0)

#define CAM_YUV_COEF1_C1_MASK	(0x3FF << 0)
#define CAM_YUV_COEF1_C1(x)	(((x) & 0x3FF) << 0)
#define CAM_YUV_COEF1_C2_MASK	(0x3FF << 0)
#define CAM_YUV_COEF1_C2(x)	(((x) & 0x3FF) << 10)
#define CAM_YUV_COEF1_C3_MASK	(0x3FF << 0)
#define CAM_YUV_COEF1_C3(x)	(((x) & 0x3FF) << 20)

#define CAM_YUV_COEF2_C4_MASK	(0x3FF << 0)
#define CAM_YUV_COEF2_C4(x)	(((x) & 0x3FF) << 0)
#define CAM_YUV_COEF2_C5_MASK	(0x3FF << 0)
#define CAM_YUV_COEF2_C5(x)	(((x) & 0x3FF) << 10)
#define CAM_YUV_COEF2_C6_MASK	(0x3FF << 0)
#define CAM_YUV_COEF2_C6(x)	(((x) & 0x3FF) << 20)

#define CAM_YUV_COEF3_C7_MASK	(0x3FF << 0)
#define CAM_YUV_COEF3_C7(x)	(((x) & 0x3FF) << 0)
#define CAM_YUV_COEF3_C8_MASK	(0x3FF << 0)
#define CAM_YUV_COEF3_C8(x)	(((x) & 0x3FF) << 10)
#define CAM_YUV_COEF3_C9_MASK	(0x3FF << 0)
#define CAM_YUV_COEF3_C9(x)	(((x) & 0x3FF) << 20)

#define CAM_YUV_OFFSET_OFF1_MASK	(0x3FF << 0)
#define CAM_YUV_OFFSET_OFF1(x)		(((x) & 0x3FF) << 0)
#define CAM_YUV_OFFSET_OFF2_MASK	(0x3FF << 10)
#define CAM_YUV_OFFSET_OFF2(x)		(((x) & 0x3FF) << 10)
#define CAM_YUV_OFFSET_OFF3_MASK	(0x3FF << 20)
#define CAM_YUV_OFFSET_OFF3(x)		(((x) & 0x3FF) << 20)

#define CAM_INT_EN_SENSOR_INT	(1 << 0)
#define CAM_INT_EN_FIFO_OFLOW	(1 << 1)
#define CAM_INT_EN_FIFO_UFLOW	(1 << 2)
#define CAM_INT_EN_TS_OVER	(1 << 3)

#define CAM_INT_CTRL_SENSOR_INT	(1 << 0)
#define CAM_INT_CTRL_FIFO_OFLOW	(1 << 1)
#define CAM_INT_CTRL_FIFO_UFLOW	(1 << 2)
#define CAM_INT_CTRL_TS_OVER	(1 << 3)

#define CAM_VSYNC_CTRL_ACT_NUM_MASK	(0xFFFF << 0)
#define CAM_VSYNC_CTRL_ACT_NUM(x)	(((x) & 0xFFFF) << 0)
#define CAM_VSYNC_CTRL_BLANK_NUM_MASK	(0xFFFF << 16)
#define CAM_VSYNC_CTRL_BLANK_NUM(x)	(((x) & 0xFFFF) << 16)

#define CAM_HSYNC_CTRL_ACT_NUM_MASK	(0xFFFF << 0)
#define CAM_HSYNC_CTRL_ACT_NUM(x)	(((x) & 0xFFFF) << 0)
#define CAM_HSYNC_CTRL_BLANK_NUM_MASK	(0xFFFF << 16)
#define CAM_HSYNC_CTRL_BLANK_NUM(x)	(((x) & 0xFFFF) << 16)

#define CAM_PIXCLK_CTRL_NUM_MASK	(0xFFFF << 0)
#define CAM_PIXCLK_CTRL_NUM(x)		(((x) & 0xFFFF) << 0)

#define CAM_VH_VSYNC_HSYNC_MASK		(0xFFFF << 0)
#define CAM_VH_VSYNC_HSYNC(x)		(((x) & 0xFFFF) << 0)
#define CAM_VH_VSYNC_WIDTH_MASK		(0xFFFF << 16)
#define CAM_VH_VSYNC_WIDTH(x)		(((x) & 0xFFFF) << 16)

#define CAM_TIMING_CTRL_PCLK_POLAR	(1 << 0)
#define CAM_TIMING_CTRL_HSYNC_POLAR	(1 << 1)
#define CAM_TIMING_CTRL_VSYNC_POLAR	(1 << 2)
#define CAM_TIMING_CTRL_HSYNC_MASK	(1 << 3)

#define CAM_DMA_CTRL_DMA_IO		(1 << 0)
#define CAM_DMA_CTRL_DMA_FLUSH		(1 << 2)
#define CAM_DMA_CTRL_ENDIAN_MODE_MASK	(0x3 << 4)
#define CAM_DMA_CTRL_ENDIAN_MODE(x)	(((x) & 0x3) << 4)

#define CAM_FIFO_CTRL_FIFO_WIDTH_MASK	(0x3 << 0)
#define CAM_FIFO_CTRL_FIFO_WIDTH(x)	(((x) & 0x3) << 0)

#define CAM_FIFO_LEVEL_CHK_FIFO_SC_MASK	(0x7F << 0)
#define CAM_FIFO_LEVEL_CHK_FIFO_SC(x)	(((x) & 0x7F) << 0)
#define CAM_FIFO_LEVEL_CHK_FIFO_LC_MASK	(0x7F << 10)
#define CAM_FIFO_LEVEL_CHK_FIFO_LC(x)	(((x) & 0x7F) << 10)
#define CAM_FIFO_LEVEL_CHK_FIFO_HC_MASK	(0x7F << 20)
#define CAM_FIFO_LEVEL_CHK_FIFO_HC(x)	(((x) & 0x7F) << 20)

#define CAM_FIFO_OP_FIFO_START	(1 << 0)
#define CAM_FIFO_OP_FIFO_RESET	(1 << 1)

#define CAM_FIFO_STATUS_FIFO_LEVEL_MASK	(0x1FF << 0)
#define CAM_FIFO_STATUS_FIFO_LEVEL(x)	(((x) & 0x1FF) << 0)
#define CAM_FIFO_STATUS_FIFO_FULL	(1 << 9)
#define CAM_FIFO_STATUS_FIFO_EMPTY	(1 << 10)

#define CAM_TS_CTRL_VIP_TS	(1 << 4)
#define CAM_TS_CTRL_NEG_SAMPLE	(1 << 5)
#define CAM_TS_CTRL_SINGLE	(1 << 6)
#define CAM_TS_CTRL_ENDIAN	(1 << 7)
#define CAM_TS_CTRL_INIT	(1 << 31)

#endif
