/*
 * CSR SiRFSoC power control module MFD interface
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

#ifndef _SIRFSOC_PWRC_H_
#define _SIRFSOC_PWRC_H_
#include <linux/interrupt.h>
#include <linux/regmap.h>

#define PWRC_PDN_CTRL_OFFSET	0
#define AUDIO_POWER_EN_BIT	14

struct sirfsoc_pwrc_register {
	/* hardware pwrc specific */
	u32 pwrc_pdn_ctrl_set;
	u32 pwrc_pdn_ctrl_clr;
	u32 pwrc_pon_status;
	u32 pwrc_trigger_en_set;
	u32 pwrc_trigger_en_clr;
	u32 pwrc_int_mask_set;
	u32 pwrc_int_mask_clr;
	u32 pwrc_int_status;
	u32 pwrc_pin_status;
	u32 pwrc_rtc_pll_ctrl;
	u32 pwrc_gpio3_debug;
	u32 pwrc_rtc_noc_pwrctl_set;
	u32 pwrc_rtc_noc_pwrctl_clr;
	u32 pwrc_rtc_can_ctrl;
	u32 pwrc_rtc_can_status;
	u32 pwrc_fsm_m3_ctrl;
	u32 pwrc_fsm_state;
	u32 pwrc_rtcldo_reg;
	u32 pwrc_gnss_ctrl;
	u32 pwrc_gnss_status;
	u32 pwrc_xtal_reg;
	u32 pwrc_xtal_ldo_mux_sel;
	u32 pwrc_rtc_sw_rstc_set;
	u32 pwrc_rtc_sw_rstc_clr;
	u32 pwrc_power_sw_ctrl_set;
	u32 pwrc_power_sw_ctrl_clr;
	u32 pwrc_rtc_dcog;
	u32 pwrc_m3_memories;
	u32 pwrc_can0_memory;
	u32 pwrc_rtc_gnss_memory;
	u32 pwrc_m3_clk_en;
	u32 pwrc_can0_clk_en;
	u32 pwrc_spi0_clk_en;
	u32 pwrc_rtc_sec_clk_en;
	u32 pwrc_rtc_noc_clk_en;

	/*only for prima2*/
	u32 pwrc_scratch_pad1;
	u32 pwrc_scratch_pad2;
	u32 pwrc_scratch_pad3;
	u32 pwrc_scratch_pad4;
	u32 pwrc_scratch_pad5;
	u32 pwrc_scratch_pad6;
	u32 pwrc_scratch_pad7;
	u32 pwrc_scratch_pad8;
	u32 pwrc_scratch_pad9;
	u32 pwrc_scratch_pad10;
	u32 pwrc_scratch_pad11;
	u32 pwrc_scratch_pad12;
	u32 pwrc_gpio3_clk;
	u32 pwrc_gpio_ds;

};

enum pwrc_version {
	PWRC_MARCO_VER,
	PWRC_PRIMA2_VER,
	PWRC_ATLAS7_VER,
};

struct sirfsoc_pwrc_info {
	struct device *dev;
	struct regmap *regmap;
	struct sirfsoc_pwrc_register *pwrc_reg;
	struct regmap_irq_chip *regmap_irq_chip;
	struct regmap_irq_chip_data *irq_data;
	u32 ver;
	u32 base;
	u32 size;
	int irq;
};

enum {
	PWRC_IRQ_ONKEY = 0,
	PWRC_IRQ_EXT_ONKEY,
	PWRC_IRQ_LOWBAT,
	PWRC_IRQ_MULT_BUTTON1,
	PWRC_IRQ_MULT_BUTTON2,
	PWRC_IRQ_GNSS_PON_REQ,
	PWRC_IRQ_GNSS_POFF_REQ,
	PWRC_IRQ_GNSS_PON_ACK,
	PWRC_IRQ_GNSS_POFF_ACK,

	PWRC_MAX_IRQ,
};

extern struct sirfsoc_pwrc_register sirfsoc_a7da_pwrc;
extern struct sirfsoc_pwrc_register sirfsoc_prima2_pwrc;
#endif
