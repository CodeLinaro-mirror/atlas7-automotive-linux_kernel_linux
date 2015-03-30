/*
 * Clock tree for CSR SiRFAtlas7
 *
 * Copyright (c) 2014 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef _CLK_ATLAS7_H_
#define _CLK_ATLAS7_H_

#define SIRFSOC_CLKC_MEMPLL_AB_FREQ          0x0000
#define SIRFSOC_CLKC_MEMPLL_AB_SSC           0x0004
#define SIRFSOC_CLKC_MEMPLL_AB_CTRL0         0x0008
#define SIRFSOC_CLKC_MEMPLL_AB_CTRL1         0x000c
#define SIRFSOC_CLKC_MEMPLL_AB_STATUS        0x0010
#define SIRFSOC_CLKC_MEMPLL_AB_SSRAM_ADDR    0x0014
#define SIRFSOC_CLKC_MEMPLL_AB_SSRAM_DATA    0x0018

#define SIRFSOC_CLKC_CPUPLL_AB_FREQ          0x001c
#define SIRFSOC_CLKC_CPUPLL_AB_SSC           0x0020
#define SIRFSOC_CLKC_CPUPLL_AB_CTRL0         0x0024
#define SIRFSOC_CLKC_CPUPLL_AB_CTRL1         0x0028
#define SIRFSOC_CLKC_CPUPLL_AB_STATUS        0x002c

#define SIRFSOC_CLKC_SYS0PLL_AB_FREQ         0x0030
#define SIRFSOC_CLKC_SYS0PLL_AB_SSC          0x0034
#define SIRFSOC_CLKC_SYS0PLL_AB_CTRL0        0x0038
#define SIRFSOC_CLKC_SYS0PLL_AB_CTRL1        0x003c
#define SIRFSOC_CLKC_SYS0PLL_AB_STATUS       0x0040

#define SIRFSOC_CLKC_SYS1PLL_AB_FREQ         0x0044
#define SIRFSOC_CLKC_SYS1PLL_AB_SSC          0x0048
#define SIRFSOC_CLKC_SYS1PLL_AB_CTRL0        0x004c
#define SIRFSOC_CLKC_SYS1PLL_AB_CTRL1        0x0050
#define SIRFSOC_CLKC_SYS1PLL_AB_STATUS       0x0054

#define SIRFSOC_CLKC_SYS2PLL_AB_FREQ         0x0058
#define SIRFSOC_CLKC_SYS2PLL_AB_SSC          0x005c
#define SIRFSOC_CLKC_SYS2PLL_AB_CTRL0        0x0060
#define SIRFSOC_CLKC_SYS2PLL_AB_CTRL1        0x0064
#define SIRFSOC_CLKC_SYS2PLL_AB_STATUS       0x0068

#define SIRFSOC_CLKC_SYS3PLL_AB_FREQ         0x006c
#define SIRFSOC_CLKC_SYS3PLL_AB_SSC          0x0070
#define SIRFSOC_CLKC_SYS3PLL_AB_CTRL0        0x0074
#define SIRFSOC_CLKC_SYS3PLL_AB_CTRL1        0x0078
#define SIRFSOC_CLKC_SYS3PLL_AB_STATUS       0x007c

#define SIRFSOC_ABPLL_CTRL0_SSEN     0x00001000
#define SIRFSOC_ABPLL_CTRL0_BYPASS   0x00000010
#define SIRFSOC_ABPLL_CTRL0_RESET    0x00000001

#define SIRFSOC_CLKC_AUDIO_DTO_INC           0x0088
#define SIRFSOC_CLKC_DISP0_DTO_INC           0x008c
#define SIRFSOC_CLKC_DISP1_DTO_INC           0x0090

#define SIRFSOC_CLKC_AUDIO_DTO_SRC           0x0094
#define SIRFSOC_CLKC_AUDIO_DTO_ENA           0x0098
#define SIRFSOC_CLKC_AUDIO_DTO_DROFF         0x009c

#define SIRFSOC_CLKC_DISP0_DTO_SRC           0x00a0
#define SIRFSOC_CLKC_DISP0_DTO_ENA           0x00a4
#define SIRFSOC_CLKC_DISP0_DTO_DROFF         0x00a8

#define SIRFSOC_CLKC_DISP1_DTO_SRC           0x00ac
#define SIRFSOC_CLKC_DISP1_DTO_ENA           0x00b0
#define SIRFSOC_CLKC_DISP1_DTO_DROFF         0x00b4

#define SIRFSOC_CLKC_I2S_CLK_SEL             0x00b8
#define SIRFSOC_CLKC_I2S_SEL_STAT            0x00bc

#define SIRFSOC_CLKC_USBPHY_CLKDIV_CFG       0x00c0
#define SIRFSOC_CLKC_USBPHY_CLKDIV_ENA       0x00c4
#define SIRFSOC_CLKC_USBPHY_CLK_SEL          0x00c8
#define SIRFSOC_CLKC_USBPHY_CLK_SEL_STAT     0x00cc

#define SIRFSOC_CLKC_BTSS_CLKDIV_CFG         0x00d0
#define SIRFSOC_CLKC_BTSS_CLKDIV_ENA         0x00d4
#define SIRFSOC_CLKC_BTSS_CLK_SEL            0x00d8
#define SIRFSOC_CLKC_BTSS_CLK_SEL_STAT       0x00dc

#define SIRFSOC_CLKC_RGMII_CLKDIV_CFG        0x00e0
#define SIRFSOC_CLKC_RGMII_CLKDIV_ENA        0x00e4
#define SIRFSOC_CLKC_RGMII_CLK_SEL           0x00e8
#define SIRFSOC_CLKC_RGMII_CLK_SEL_STAT      0x00ec

#define SIRFSOC_CLKC_CPU_CLKDIV_CFG          0x00f0
#define SIRFSOC_CLKC_CPU_CLKDIV_ENA          0x00f4
#define SIRFSOC_CLKC_CPU_CLK_SEL             0x00f8
#define SIRFSOC_CLKC_CPU_CLK_SEL_STAT        0x00fc

#define SIRFSOC_CLKC_SDPHY01_CLKDIV_CFG      0x0100
#define SIRFSOC_CLKC_SDPHY01_CLKDIV_ENA      0x0104
#define SIRFSOC_CLKC_SDPHY01_CLK_SEL         0x0108
#define SIRFSOC_CLKC_SDPHY01_CLK_SEL_STAT    0x010c

#define SIRFSOC_CLKC_SDPHY23_CLKDIV_CFG      0x0110
#define SIRFSOC_CLKC_SDPHY23_CLKDIV_ENA      0x0114
#define SIRFSOC_CLKC_SDPHY23_CLK_SEL         0x0118
#define SIRFSOC_CLKC_SDPHY23_CLK_SEL_STAT    0x011c

#define SIRFSOC_CLKC_SDPHY45_CLKDIV_CFG      0x0120
#define SIRFSOC_CLKC_SDPHY45_CLKDIV_ENA      0x0124
#define SIRFSOC_CLKC_SDPHY45_CLK_SEL         0x0128
#define SIRFSOC_CLKC_SDPHY45_CLK_SEL_STAT    0x012c

#define SIRFSOC_CLKC_SDPHY67_CLKDIV_CFG      0x0130
#define SIRFSOC_CLKC_SDPHY67_CLKDIV_ENA      0x0134
#define SIRFSOC_CLKC_SDPHY67_CLK_SEL         0x0138
#define SIRFSOC_CLKC_SDPHY67_CLK_SEL_STAT    0x013c

#define SIRFSOC_CLKC_CAN_CLKDIV_CFG          0x0140
#define SIRFSOC_CLKC_CAN_CLKDIV_ENA          0x0144
#define SIRFSOC_CLKC_CAN_CLK_SEL             0x0148
#define SIRFSOC_CLKC_CAN_CLK_SEL_STAT        0x014c

#define SIRFSOC_CLKC_DEINT_CLKDIV_CFG        0x0150
#define SIRFSOC_CLKC_DEINT_CLKDIV_ENA        0x0154
#define SIRFSOC_CLKC_DEINT_CLK_SEL           0x0158
#define SIRFSOC_CLKC_DEINT_CLK_SEL_STAT      0x015c

#define SIRFSOC_CLKC_NAND_CLKDIV_CFG         0x0160
#define SIRFSOC_CLKC_NAND_CLKDIV_ENA         0x0164
#define SIRFSOC_CLKC_NAND_CLK_SEL            0x0168
#define SIRFSOC_CLKC_NAND_CLK_SEL_STAT       0x016c

#define SIRFSOC_CLKC_DISP0_CLKDIV_CFG        0x0170
#define SIRFSOC_CLKC_DISP0_CLKDIV_ENA        0x0174
#define SIRFSOC_CLKC_DISP0_CLK_SEL           0x0178
#define SIRFSOC_CLKC_DISP0_CLK_SEL_STAT      0x017c

#define SIRFSOC_CLKC_DISP1_CLKDIV_CFG        0x0180
#define SIRFSOC_CLKC_DISP1_CLKDIV_ENA        0x0184
#define SIRFSOC_CLKC_DISP1_CLK_SEL           0x0188
#define SIRFSOC_CLKC_DISP1_CLK_SEL_STAT      0x018c

#define SIRFSOC_CLKC_GPU_CLKDIV_CFG          0x0190
#define SIRFSOC_CLKC_GPU_CLKDIV_ENA          0x0194
#define SIRFSOC_CLKC_GPU_CLK_SEL             0x0198
#define SIRFSOC_CLKC_GPU_CLK_SEL_STAT        0x019c

#define SIRFSOC_CLKC_GNSS_CLKDIV_CFG         0x01a0
#define SIRFSOC_CLKC_GNSS_CLKDIV_ENA         0x01a4
#define SIRFSOC_CLKC_GNSS_CLK_SEL            0x01a8
#define SIRFSOC_CLKC_GNSS_CLK_SEL_STAT       0x01ac

#define SIRFSOC_CLKC_SHARED_DIVIDER_CFG0     0x01b0
#define SIRFSOC_CLKC_SHARED_DIVIDER_CFG1     0x01b4
#define SIRFSOC_CLKC_SHARED_DIVIDER_ENA      0x01b8

#define SIRFSOC_CLKC_SYS_CLK_SEL             0x01bc
#define SIRFSOC_CLKC_SYS_CLK_SEL_STAT        0x01c0
#define SIRFSOC_CLKC_IO_CLK_SEL              0x01c4
#define SIRFSOC_CLKC_IO_CLK_SEL_STAT         0x01c8
#define SIRFSOC_CLKC_G2D_CLK_SEL             0x01cc
#define SIRFSOC_CLKC_G2D_CLK_SEL_STAT        0x01d0
#define SIRFSOC_CLKC_JPENC_CLK_SEL           0x01d4
#define SIRFSOC_CLKC_JPENC_CLK_SEL_STAT      0x01d8
#define SIRFSOC_CLKC_VDEC_CLK_SEL            0x01dc
#define SIRFSOC_CLKC_VDEC_CLK_SEL_STAT       0x01e0
#define SIRFSOC_CLKC_GMAC_CLK_SEL            0x01e4
#define SIRFSOC_CLKC_GMAC_CLK_SEL_STAT       0x01e8
#define SIRFSOC_CLKC_USB_CLK_SEL             0x01ec
#define SIRFSOC_CLKC_USB_CLK_SEL_STAT        0x01f0
#define SIRFSOC_CLKC_KAS_CLK_SEL             0x01f4
#define SIRFSOC_CLKC_KAS_CLK_SEL_STAT        0x01f8
#define SIRFSOC_CLKC_SEC_CLK_SEL             0x01fc
#define SIRFSOC_CLKC_SEC_CLK_SEL_STAT        0x0200
#define SIRFSOC_CLKC_SDR_CLK_SEL             0x0204
#define SIRFSOC_CLKC_SDR_CLK_SEL_STAT        0x0208
#define SIRFSOC_CLKC_VIP_CLK_SEL             0x020c
#define SIRFSOC_CLKC_VIP_CLK_SEL_STAT        0x0210
#define SIRFSOC_CLKC_NOCD_CLK_SEL            0x0214
#define SIRFSOC_CLKC_NOCD_CLK_SEL_STAT       0x0218
#define SIRFSOC_CLKC_NOCR_CLK_SEL            0x021c
#define SIRFSOC_CLKC_NOCR_CLK_SEL_STAT       0x0220
#define SIRFSOC_CLKC_TPIU_CLK_SEL            0x0224
#define SIRFSOC_CLKC_TPIU_CLK_SEL_STAT       0x0228

#define SIRFSOC_CLKC_ROOT_CLK_EN0_SET        0x022c
#define SIRFSOC_CLKC_ROOT_CLK_EN0_CLR        0x0230
#define SIRFSOC_CLKC_ROOT_CLK_EN0_STAT       0x0234
#define SIRFSOC_CLKC_ROOT_CLK_EN1_SET        0x0238
#define SIRFSOC_CLKC_ROOT_CLK_EN1_CLR        0x023c
#define SIRFSOC_CLKC_ROOT_CLK_EN1_STAT       0x0240

#define SIRFSOC_CLKC_LEAF_CLK_EN0_SET        0x0244
#define SIRFSOC_CLKC_LEAF_CLK_EN0_CLR        0x0248
#define SIRFSOC_CLKC_LEAF_CLK_EN0_STAT       0x024c

#define SIRFSOC_CLKC_RSTC_A7_SW_RST          0x0308

#define SIRFSOC_CLKC_LEAF_CLK_EN1_SET        0x04a0
#define SIRFSOC_CLKC_LEAF_CLK_EN2_SET        0x04b8
#define SIRFSOC_CLKC_LEAF_CLK_EN3_SET        0x04d0
#define SIRFSOC_CLKC_LEAF_CLK_EN4_SET        0x04e8
#define SIRFSOC_CLKC_LEAF_CLK_EN5_SET        0x0500
#define SIRFSOC_CLKC_LEAF_CLK_EN6_SET        0x0518
#define SIRFSOC_CLKC_LEAF_CLK_EN7_SET        0x0530
#define SIRFSOC_CLKC_LEAF_CLK_EN8_SET        0x0548

/*RTCM clock*/
#define SIRFSOC_RTCM_CLKC_PLL_CTRL		0x28
#define SIRFSOC_RTCM_CLKC_M3_CLK_SEL		0x8C
#define SIRFSOC_RTCM_CLKC_CAN0_CLK_SEL	0x90
#define SIRFSOC_RTCM_CLKC_QSPI0_CLK_SEL	0x94

/**/
#define SIRFSOC_AUDIO_CLKC_IACC_CLK_SEL	0x230
#define SIRFSOC_AUDIO_CLKC_IACC_CLK_STATUS	0x250

#define SIRFSOC_DIVIDOR_TYPE_TABLE	0x1
struct clk_pll {
	struct clk_hw hw;
	unsigned short regofs;  /* register offset */
};
#define to_pllclk(_hw) container_of(_hw, struct clk_pll, hw)

struct clk_dto {
	struct clk_hw hw;
	unsigned short inc_offset;  /* dto increment offset */
	unsigned short src_offset;  /* dto src offset */
};
#define to_dtoclk(_hw) container_of(_hw, struct clk_dto, hw)

struct clk_unit {
	struct clk_hw hw;
	unsigned short regofs;
	unsigned short bit;
	spinlock_t *lock;
};
#define to_unitclk(_hw) container_of(_hw, struct clk_unit, hw)

struct atlas7_div_init_data {
	const char *div_name;
	const char *parent_name;
	const char *gate_name;
	unsigned long flags;
	u8 divider_flags;
	u8 gate_flags;
	u32 div_offset;
	u8 shift;
	u8 width;
	u32 gate_offset;
	u8 gate_bit;
	spinlock_t *lock;
};

struct atlas7_mux_init_data {
	const char *mux_name;
	const char **parent_names;
	u8 parent_num;
	unsigned long flags;
	u8 mux_flags;
	u32 mux_offset;
	u8 shift;
	u8 width;
};

struct atlas7_unit_init_data {
	u32 index;
	const char *unit_name;
	const char *parent_name;
	unsigned long flags;
	u32 regofs;
	u8 bit;
	spinlock_t *lock;
};

struct atlas7_reset_desc {
	const char *name;
	u32 clk_ofs;
	u8  clk_bit;
	u32 rst_ofs;
	u8  rst_bit;
	spinlock_t *lock;
};

#endif
