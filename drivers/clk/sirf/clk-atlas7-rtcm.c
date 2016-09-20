/*
 * Clock tree for CSR SiRFAtlas7
 *
 * Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
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

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/regmap.h>
#include <linux/mfd/sirfsoc_pwrc.h>
#include <linux/rtc/sirfsoc_rtciobrg.h>

#define SIRFSOC_RTCM_CLKC_PLL_CTRL		0x28
#define SIRFSOC_RTCM_CLKC_M3_CLK_SEL		0x8C
#define SIRFSOC_RTCM_CLKC_CAN0_CLK_SEL	0x90
#define SIRFSOC_RTCM_CLKC_QSPI0_CLK_SEL	0x94

static DEFINE_SPINLOCK(can0_gate_lock);
static DEFINE_SPINLOCK(qspi0_gate_lock);

struct clk_rtcm {
	struct clk_hw hw;
	u16 regofs;
	u16 bit;
	spinlock_t *lock;
};
#define to_rtcmclk(_hw) container_of(_hw, struct clk_rtcm, hw)

struct clk_rtcpll {
	struct clk_hw hw;
	unsigned short regofs;  /* register offset */
};

#define to_rtcpllclk(_hw) container_of(_hw, struct clk_rtcpll, hw)

struct atlas7_rtcmclk_init_data {
	u32 index;
	const char *unit_name;
	const char *parent_name;
	unsigned long flags;
	u32 regofs;
	u8 bit;
	spinlock_t *lock;
};

struct sirfsoc_rtcmclk_info {
	struct device *dev;
	struct regmap *regmap;
	struct sirfsoc_pwrc_register *pwrc_reg;
	u32 ver;
	u32 base;
};

static struct sirfsoc_rtcmclk_info *s_rtcmclk_info;

static inline unsigned long rtcm_clkc_readl(unsigned reg)
{
	u32 val;

	regmap_read(s_rtcmclk_info->regmap, s_rtcmclk_info->base + reg, &val);
	return val;
}

static inline void  rtcm_clkc_writel(u32 val, unsigned reg)
{
	regmap_write(s_rtcmclk_info->regmap, s_rtcmclk_info->base + reg, val);
}

static struct clk_onecell_data rtcmclk_data;

static struct atlas7_rtcmclk_init_data rtcm_unit_list[] = {
	{0, "can0", "rtcmpll_fast_fixdiv", 0,
		SIRFSOC_RTCM_CLKC_CAN0_CLK_SEL, 0, &can0_gate_lock},
	{1, "qspi0", "rtcmpll_fast_fixdiv", 0,
		SIRFSOC_RTCM_CLKC_QSPI0_CLK_SEL, 0, &qspi0_gate_lock},
};

/* AOPD (always on power domain) clk controller */

static struct clk *rtcm_clks[ARRAY_SIZE(rtcm_unit_list)];

static int rtcm_unit_clk_is_enabled(struct clk_hw *hw)
{
	struct clk_rtcm *clk = to_rtcmclk(hw);
	int ret;

	sirfsoc_iobg_lock();
	ret = !!(rtcm_clkc_readl(clk->regofs) & BIT(clk->bit));
	sirfsoc_iobg_unlock();

	return ret;
}

static int rtcm_unit_clk_enable(struct clk_hw *hw)
{
	struct clk_rtcm *clk = to_rtcmclk(hw);
	unsigned long flags = 0;

	spin_lock_irqsave(clk->lock, flags);
	sirfsoc_iobg_lock();
	rtcm_clkc_writel(BIT(clk->bit), clk->regofs);
	sirfsoc_iobg_unlock();
	spin_unlock_irqrestore(clk->lock, flags);

	return 0;
}

static void rtcm_unit_clk_disable(struct clk_hw *hw)
{
	struct clk_rtcm *clk = to_rtcmclk(hw);
	unsigned long flags = 0;

	spin_lock_irqsave(clk->lock, flags);
	sirfsoc_iobg_lock();
	rtcm_clkc_writel(rtcm_clkc_readl(clk->regofs) &
		~BIT(clk->bit),
		clk->regofs);
	sirfsoc_iobg_unlock();
	spin_unlock_irqrestore(clk->lock, flags);
}

static struct clk_ops rtcm_unit_clk_ops = {
	.is_enabled = rtcm_unit_clk_is_enabled,
	.enable = rtcm_unit_clk_enable,
	.disable = rtcm_unit_clk_disable,
};

static struct clk *atlas7_rtcm_unit_clk_register
		(struct device *dev, const char *name,
		 const char *parent_name, unsigned long flags,
		 u32 regofs, u8 bit, spinlock_t *lock)
{
	struct clk *clk;
	struct clk_rtcm *unit;
	struct clk_init_data init;

	unit = kzalloc(sizeof(*unit), GFP_KERNEL);
	if (!unit)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);
	init.ops = &rtcm_unit_clk_ops;
	init.flags = flags;

	unit->hw.init = &init;
	unit->regofs = regofs;
	unit->bit = bit;
	unit->lock = lock;

	clk = clk_register(dev, &unit->hw);
	if (IS_ERR(clk))
		kfree(unit);

	return clk;
}

static unsigned long pll_rtcmclk_recalc_rate(struct clk_hw *hw,
	unsigned long parent_rate)
{
	struct clk_rtcpll *clk = to_rtcpllclk(hw);
	u32 pllctl;
	u32 fbdiv;
	u64 rate;

	sirfsoc_iobg_lock();
	pllctl = rtcm_clkc_readl(clk->regofs);
	sirfsoc_iobg_unlock();

	fbdiv = (pllctl>>1) & 0x3fff;
	rate = parent_rate * fbdiv;

	return rate;
}


static struct clk_ops rtcm_pll_ops = {
	.recalc_rate = pll_rtcmclk_recalc_rate,
};

static const char *const rtcm_clk_parents[] = {
	"xinw",
};

static struct clk_init_data clk_rtcmpll_init = {
	.name = "rtcmpll_vco",
	.ops = &rtcm_pll_ops,
	.parent_names = rtcm_clk_parents,
	.num_parents = 1,
};

static struct clk_rtcpll clk_rtcmpll = {
	.regofs = SIRFSOC_RTCM_CLKC_PLL_CTRL,
	.hw = {
		.init = &clk_rtcmpll_init,
	},
};

const struct regmap_config rtcmclk_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.fast_io = true,
};

static const struct of_device_id rtcmclk_ids[] = {
	{ .compatible = "sirf,atlas7-rtcmclk"},
	{}
};

static int sirfsoc_rtcmclk_probe(struct platform_device *pdev)
{
	struct sirfsoc_pwrc_info *pwrcinfo = dev_get_drvdata(pdev->dev.parent);
	struct atlas7_rtcmclk_init_data *unit;
	struct sirfsoc_rtcmclk_info *info;
	struct device_node *np;
	struct clk *clk;
	int ret;
	int i;

	info = kzalloc(sizeof(struct sirfsoc_rtcmclk_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	info->pwrc_reg = pwrcinfo->pwrc_reg;
	info->regmap = pwrcinfo->regmap;
	info->base = pwrcinfo->base;
	info->ver = pwrcinfo->ver;

	if (!info->regmap) {
		kfree(info);
		dev_err(&pdev->dev, "no regmap!\n");
		return -EINVAL;
	}

	s_rtcmclk_info = info;

	clk = clk_register(NULL, &clk_rtcmpll.hw);
	BUG_ON(!clk);

	clk = clk_register_fixed_factor(NULL, "rtcmpll_fast_fixdiv",
					"rtcmpll_vco",
					CLK_SET_RATE_PARENT, 1, 1);
	BUG_ON(!clk);

	for (i = 0; i < ARRAY_SIZE(rtcm_unit_list); i++) {
		unit = &rtcm_unit_list[i];
		rtcm_clks[i] = atlas7_rtcm_unit_clk_register(NULL,
				unit->unit_name, unit->parent_name,
				unit->flags, unit->regofs,
				unit->bit, unit->lock);
		BUG_ON(!rtcm_clks[i]);
	}

	rtcmclk_data.clks = rtcm_clks;
	rtcmclk_data.clk_num = ARRAY_SIZE(rtcm_unit_list);

	np = of_find_matching_node(NULL, rtcmclk_ids);
	if (!np)
		panic("unable to find compatible sirf node in dtb\n");

	ret = of_clk_add_provider(np, of_clk_src_onecell_get, &rtcmclk_data);
	BUG_ON(ret);
	return 0;
}

static struct platform_driver sirfsoc_rtcmclk_driver = {
	.probe	= sirfsoc_rtcmclk_probe,
	.driver	= {
		.name	= "rtcmclk",
		.of_match_table = rtcmclk_ids,
	},
};
module_platform_driver(sirfsoc_rtcmclk_driver);
