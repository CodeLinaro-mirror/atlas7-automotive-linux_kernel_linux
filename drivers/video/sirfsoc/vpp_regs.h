/*
 * CSR sirfsoc VPP register definition file
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __SIRFSOC_VPP_REGS_H
#define __SIRFSOC_VPP_REGS_H

#define VPP_CTRL		0x0000
#define VPP_YBASE		0x0004
#define VPP_UBASE		0x0008
#define VPP_VBASE		0x000C
#define VPP_DESBASE		0x0010
#define VPP_WIDTH		0x0014
#define VPP_HEIGHT		0x0018
#define VPP_STRIDE0		0x001c
#define VPP_STRIDE1		0x0020
#define VPP_HSCA_COEF00		0x0024
#define VPP_HSCA_COEF01		0x0028
#define VPP_HSCA_COEF02		0x002c
#define VPP_HSCA_COEF10		0x0030
#define VPP_HSCA_COEF11		0x0034
#define VPP_HSCA_COEF12		0x0038
#define VPP_HSCA_COEF20		0x003c
#define VPP_HSCA_COEF21		0x0040
#define VPP_HSCA_COEF22		0x0044
#define VPP_HSCA_COEF30		0x0048
#define VPP_HSCA_COEF31		0x004c
#define VPP_HSCA_COEF32		0x0050
#define VPP_HSCA_COEF40		0x0054
#define VPP_HSCA_COEF41		0x0058
#define VPP_HSCA_COEF42		0x005c
#define VPP_HSCA_COEF50		0x0060
#define VPP_HSCA_COEF51		0x0064
#define VPP_HSCA_COEF52		0x0068
#define VPP_HSCA_COEF60		0x006c
#define VPP_HSCA_COEF61		0x0070
#define VPP_HSCA_COEF62		0x0074
#define VPP_HSCA_COEF70		0x0078
#define VPP_HSCA_COEF71		0x007c
#define VPP_HSCA_COEF72		0x0080
#define VPP_HSCA_COEF80		0x0084
#define VPP_HSCA_COEF81		0x0088
#define VPP_HSCA_COEF82		0x008c
#define VPP_VSCA_COEF00		0x0090
#define VPP_VSCA_COEF01		0x0094
#define VPP_VSCA_COEF10		0x0098
#define VPP_VSCA_COEF11		0x009c
#define VPP_VSCA_COEF20		0x00a0
#define VPP_VSCA_COEF21		0x00a4
#define VPP_VSCA_COEF30		0x00a8
#define VPP_VSCA_COEF31		0x00ac
#define VPP_VSCA_COEF40		0x00b0
#define VPP_VSCA_COEF41		0x00b4
#define VPP_VSCA_COEF50		0x00b8
#define VPP_VSCA_COEF51		0x00bc
#define VPP_VSCA_COEF60		0x00c0
#define VPP_VSCA_COEF61		0x00c4
#define VPP_VSCA_COEF70		0x00c8
#define VPP_VSCA_COEF71		0x00cc
#define VPP_VSCA_COEF80		0x00d0
#define VPP_VSCA_COEF81		0x00d4
#define VPP_RCOEF		0x00d8
#define VPP_GCOEF		0x00dc
#define VPP_BCOEF		0x00e0
#define VPP_OFFSET1		0x00e4
#define VPP_OFFSET2		0x00e8
#define VPP_OFFSET3		0x00ec
#define VPP_INT_MASK		0x00f0
#define VPP_INT_STATUS		0x00f4
#define VPP_ACC			0x00f8
#define VPP_FULL_THRESH		0x00fc

#define VPP_COLOR_HS_CTRL	0x100
#define	VPP_COLOR_BC_CTRL	0x104
#define	VPP_YBASE_BOT		0x108
#define	VPP_UBASE_BOT		0x10c
#define	VPP_VBASE_BOT		0x110
#define	VPP_DESBASE_BOT		0x114

#define VPP_HSCA_REG_SPACE	((VPP_HSCA_COEF82 - VPP_HSCA_COEF00) / 4 + 1)
#define VPP_VSCA_REG_SPACE	((VPP_VSCA_COEF81 - VPP_VSCA_COEF00) / 4 + 1)

#define VPP_LAYER_REG_NUM	(VPP_DESBASE_ADDR_BOT + 4)


enum vpp_yuv422_format {
	VPP_YUV422_FORMAT_YUYV = 0,
	VPP_YUV422_FORMAT_YVYU = 1,
	VPP_YUV422_FORMAT_UYVY = 2,
	VPP_YUV422_FORMAT_VYUY = 3,
};

enum vpp_out_format {
	VPP_OUT_FORMAT_RGB565 = 0,
	VPP_OUT_FORMAT_RGB666 = 1,
	VPP_OUT_FORMAT_RGB888 = 2,
	VPP_OUT_FORMAT_YUV422 = 3,
};

enum vpp_pixelformat {
	VPP_PIXEL_FORMAT_YUV422 = 0,
	VPP_PIXEL_FORMAT_YUV420 = 1,
};

enum vpp_endian_mode {
	VPP_ENDIAN_MODE_LITTLE = 0,
	VPP_ENDIAN_MODE_BIG = 1,
};

enum vpp_dest {
	VPP_DEST_MEMORY = 0,
	VPP_DEST_LCD = 1,
};

enum vpp_seq_type {
	VPP_SEQ_TYPE_PIPO = 0,
	VPP_SEQ_TYPE_PIIO = 1,
	VPP_SEQ_TYPE_IIPO = 2,
	VPP_SEQ_TYPE_IIIO = 3,
};

enum vpp_hw_di_mode {
	VPP_HW_DI_MODE_RESERVED = 0,
	VPP_HW_DI_MODE_WEAVE = 1,
	VPP_HW_DI_MODE_3MEDIAN = 2,
	/* Vertical Median Ranking Interpolation */
	VPP_HW_DI_MODE_VMRI = 3,
};


/* VPP_CTRL register definitions */
#define VPP_CTRL_PIXEL_FORMAT		(1 << 0)
#define VPP_CTRL_ENDIAN_MODE		(1 << 1)
#define VPP_CTRL_YUV422_FORMAT_MASK	(0x3 << 2)
#define VPP_CTRL_YUV422_FORMAT(x)	(((x) & 0x3) << 2)
#define VPP_CTRL_OUT_YUV422_FORMAT_MASK	(0x3 << 4)
#define VPP_CTRL_OUT_YUV422_FORMAT(x)	(((x) & 0x3) << 4)
#define VPP_CTRL_DEST			(1 << 7)
#define VPP_CTRL_OUT_FORMAT_MASK	(0x3 << 8)
#define VPP_CTRL_OUT_FORMAT(x)		(((x) & 0x3) << 8)
#define VPP_CTRL_OUT_ENDIAN_MODE	(1 << 10)
#define VPP_CTRL_CLK_OFF_ENABLE		(1 << 11)
#define VPP_CTRL_UV_INTERLEAVE_EN	(1 << 14)
#define VPP_CTRL_HW_DI_MODE_MASK	(0x3 << 15)
#define VPP_CTRL_HW_DI_MODE(x)		(((x) & 0x3) << 15)
#define VPP_CTRL_SEQ_TYPE_MASK		(0x3 << 17)
#define VPP_CTRL_SEQ_TYPE(x)		(((x) & 0x3) << 17)
#define VPP_CTRL_TOP_FIELD_FIRST	(1 << 19)
#define VPP_CTRL_DI_FIELD_BOT		(1 << 20)
#define VPP_CTRL_DOUBLE_FRATE		(1 << 21)
#define VPP_CTRL_START			(1 << 29)
#define VPP_CTRL_SCA_OVER		(1 << 30)
#define VPP_CTRL_BUSY_STATUS		(1 << 31)


#define VPP_BASE_ADDR_MASK		(0x3FFFFFFF << 0)
#define VPP_BASE_ADDR(x)		(((x) & 0x3FFFFFFF) << 0)
#define VPP_BASE_ADDR_BOT_MASK		(0x3FFFFFFF << 0)
#define VPP_BASE_ADDR_BOT(x)		(((x) & 0x3FFFFFFF) << 0)

#define VPP_SRC_WIDTH_MASK		(0x7FF << 0)
#define VPP_SRC_WIDTH(x)		(((x) & 0x7FF) << 0)
#define VPP_DES_WIDTH_MASK		(0x7FF << 16)
#define VPP_DES_WIDTH(x)		(((x) & 0x7FF) << 16)
#define VPP_SRC_HEIGHT_MASK		(0x7FF << 0)
#define VPP_SRC_HEIGHT(x)		(((x) & 0x7FF) << 0)
#define VPP_DES_HEIGHT_MASK		(0x7FF << 16)
#define VPP_DES_HEIGHT(x)		(((x) & 0x7FF) << 16)

#define VPP_Y_STRIDE_MASK		(0x1FFF << 0)
#define VPP_Y_STRIDE(x)			(((x) & 0x1FFF) << 0)
#define VPP_U_STRIDE_MASK		(0xFFF << 16)
#define VPP_U_STRIDE(x)			(((x) & 0xFFF) << 16)
#define VPP_V_STRIDE_MASK		(0xFFF << 0)
#define VPP_V_STRIDE(x)			(((x) & 0xFFF) << 0)
#define VPP_DES_STRIDE_MASK		(0x1FFF << 16)
#define VPP_DES_STRIDE(x)		(((x) & 0x1FFF) << 16)

#define VPP_HC_HSCA_COEF00_MASK		(0x7FFF << 0)
#define VPP_HC_HSCA_COEF00(x)		(((x) & 0x7FFF) << 0)
#define VPP_HC_HSCA_COEF01_MASK		(0x7FFF << 16)
#define VPP_HC_HSCA_COEF01(x)		(((x) & 0x7FFF) << 16)
#define VPP_VC_VSCA_COEF00_MASK		(0x7FFF << 0)
#define VPP_VC_VSCA_COEF00(x)		(((x) & 0x7FFF) << 0)
#define VPP_VC_VSCA_COEF01_MASK		(0x7FFF << 16)
#define VPP_VC_VSCA_COEF01(x)		(((x) & 0x7FFF) << 16)

#define VPP_COEF_C3(x)			(((x) & 0x3FF) << 0)
#define VPP_COEF_C2(x)			(((x) & 0x3FF) << 10)
#define VPP_COEF_C1(x)			(((x) & 0x3FF) << 20)

#define VPP_OFFSET(x)			(((x) & 0xFFFFFF) << 0)

#define VPP_INT_SINGLE_ENABLE		(1 << 0)
#define VPP_INT_CON_ENABLE		(1 << 1)
#define VPP_INT_AB_ENABLE		(1 << 2)
#define VPP_INT_SINGLE_STATUS		(1 << 0)
#define VPP_INT_CON_STATUS		(1 << 1)
#define VPP_INT_AB_STATUS		(1 << 2)

#define VPP_FIFO_FULL_THRESH(x)		(((x) & 0xF) << 0)
#define VPP_UVUV_MODE			(1 << 4)

#define VPP_COLOR_UC_CTRL(x)		(((x) & 0x1FFF) << 0)
#define VPP_COLOR_VC_CTRL(x)		(((x) & 0x1FFF) << 16)
#define VPP_COLOR_B_CTRL(x)		(((x) & 0x1FF) << 0)
#define VPP_COLOR_C_CTRL(x)		(((x) & 0x1FF) << 16)


#endif
