/*
 * CSR sirfsoc lvds internal interface
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

#ifndef _SIRFSOC_LVDSC_H_
#define _SIRFSOC_LVDSC_H_

#define LVDS_CTRL			0x0000
#define LVDS_LANE_FIFO_CTRL		0x0004
#define LVDS_LANE_FIFO_STATUS           0x0008
#define LVDS_INT_STATUS                 0x0010
#define LVDS_INT_MASK                   0x000c
#define LVDS_TX_LANE0_SRC0              0x0014
#define LVDS_TX_LANE0_SRC1              0x0018
#define LVDS_TX_LANE1_SRC0              0x001c
#define LVDS_TX_LANE1_SRC1              0x0020
#define LVDS_TX_LANE2_SRC0              0x0024
#define LVDS_TX_LANE2_SRC1              0x0028
#define LVDS_TX_LANE3_SRC0              0x002c
#define LVDS_TX_LANE3_SRC1              0x0030
#define LVDS_TX_LANE4_SRC0              0x0034
#define LVDS_TX_LANE4_SRC1              0x0038
#define LVDS_TEST_CTRL                  0x003c
#define LVDS_TEST_HOR                   0x0040
#define LVDS_TEST_VER                   0x0044
#define LVDS_PHY_CFG0                   0x0048
#define LVDS_PHY_CFG1                   0x004c
#define LVDS_PHY_CFG2                   0x0050
#define LVDS_PHY_CFG3                   0x0054
#define LVDS_PHY_CFG4                   0x0058
#define LVDS_PHY_CFG5			0x005c
#define LVDS_PHY_RAM_ACCESS             0x0060
#define LVDS_VERSION			0x0064

#define RSC_PIN_MUX_SET			0x0000
#define RSC_PIN_MUX_CLR			0x0004
/* LVDS_PHY_CONFIG2 */
#define PLL_MODE(x)			((x & 0x1) << 0)
#define IPLLLOGIC_A_SEL(x)		((x & 0x3) << 1)
#define IPLLLOGIC_B_SEL(x)		((x & 0x3) << 3)
#define LOCKMON_EN(x)			((x & 0x1) << 5)
#define LPF_C1_SEL(x)			((x & 0x3) << 6)
#define LPF_C2_SEL(x)			((x & 0x3) << 8)
#define LPF_R_SEL(x)			((x & 0x7) << 10)
#define LPF_R3_SEL(x)			((x & 0x3) << 13)
#define CP_I_SET(x)			((x & 0xF) << 15)
#define LPF_INTV(x)			((x & 0x3) << 19)
#define M(x)				((x & 0x1FF) << 21)
#define PLLCLKD2_ENABLE(x)		((x & 0x1) << 30)
#define PLL_LOCK(x)			((x & 0x1) << 31)

/* LVDS_PHY_CONFIG3 */
#define N(x)				((x & 0x7FFFFFF) << 0)
#define FRACTIONAL(x)			((x & 0x1) << 27)

#define LVDSC_LCDCSRC_SEL		BIT(17)

#endif
