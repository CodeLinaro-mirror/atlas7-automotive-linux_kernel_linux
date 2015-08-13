/*
 * Clock tree for CSR SiRFAtlas7
 *
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
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

#ifndef _CLK_ATLAS7_H_
#define _CLK_ATLAS7_H_

struct clk_pll {
	struct clk_hw hw;
	u16 regofs;  /* register offset */
};
#define to_pllclk(_hw) container_of(_hw, struct clk_pll, hw)

enum clk_unit_type {
	CLK_UNIT_NOC_OTHER,
	CLK_UNIT_NOC_CLOCK,
	CLK_UNIT_NOC_SOCKET,
};

struct clk_unit {
	struct clk_hw hw;
	u16 regofs;
	u16 bit;
	u32 type;
	u8 idle_bit;
	spinlock_t *lock;
};
#define to_unitclk(_hw) container_of(_hw, struct clk_unit, hw)

struct atlas7_unit_init_data {
	u32 index;
	const char *unit_name;
	const char *parent_name;
	unsigned long flags;
	u32 regofs;
	u8 bit;
	u32 type;
	u8 idle_bit;
	spinlock_t *lock;
};

#endif
