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
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/slab.h>

#define SIRFSOC_AUDIO_CLKC_IACC_CLK_SEL	0x230
#define SIRFSOC_AUDIO_CLKC_IACC_CLK_STATUS	0x250

static void *sirfsoc_audioclk_vbase;
static struct clk_onecell_data audioclk_data;
static DEFINE_SPINLOCK(audio_gate_lock);

struct clk_audio {
	struct clk_hw hw;
	u16 regofs;
	u16 bit;
	spinlock_t *lock;
};
#define to_audioclk(_hw) container_of(_hw, struct clk_audio, hw)

/*audio clk controller*/
struct atlas7_audio_init_data {
	u32 index;
	const char *unit_name;
	const char *parent_name;
	unsigned long flags;
	u32 regofs;
	u8 bit;
	spinlock_t *lock;
};

static struct atlas7_audio_init_data audio_unit_list[] = {
	{0, "codec_iacc", "xin", 0,
		SIRFSOC_AUDIO_CLKC_IACC_CLK_SEL, 11, &audio_gate_lock},
};

static struct clk *audio_clks[ARRAY_SIZE(audio_unit_list)];

static inline unsigned long audio_clkc_readl(unsigned reg)
{
	return readl(sirfsoc_audioclk_vbase + reg);
}

static inline void audio_clkc_writel(u32 val, unsigned reg)
{
	writel(val, sirfsoc_audioclk_vbase + reg);
}

static int audio_unit_clk_is_enabled(struct clk_hw *hw)
{
	return !!(audio_clkc_readl(
			SIRFSOC_AUDIO_CLKC_IACC_CLK_STATUS) & BIT(0));
}

#define IACC_CLK_RESET_BITOFF	12
static int audio_unit_clk_enable(struct clk_hw *hw)
{
	struct clk_audio *clk = to_audioclk(hw);
	unsigned long flags = 0;

	spin_lock_irqsave(clk->lock, flags);

	/*power on*/
	audio_clkc_writel(audio_clkc_readl(
		clk->regofs) &
		~BIT(clk->bit),
		clk->regofs);
	udelay(10);
	/*reset*/
	audio_clkc_writel(audio_clkc_readl(
		clk->regofs) |
		BIT(IACC_CLK_RESET_BITOFF),
		clk->regofs);

	spin_unlock_irqrestore(clk->lock, flags);
	return 0;
}

static void audio_unit_clk_disable(struct clk_hw *hw)
{
	struct clk_audio *clk = to_audioclk(hw);
	unsigned long flags = 0;

	spin_lock_irqsave(clk->lock, flags);

	audio_clkc_writel(BIT(clk->bit), clk->regofs);

	spin_unlock_irqrestore(clk->lock, flags);
}

static struct clk_ops audio_unit_clk_ops = {
	.is_enabled = audio_unit_clk_is_enabled,
	.enable = audio_unit_clk_enable,
	.disable = audio_unit_clk_disable,
};


static struct clk * __init
atlas7_audio_unit_clk_register(struct device *dev, const char *name,
		 const char *parent_name, unsigned long flags,
		 u32 regofs, u8 bit, spinlock_t *lock)
{
	struct clk *clk;
	struct clk_audio *unit;
	struct clk_init_data init;

	unit = kzalloc(sizeof(*unit), GFP_KERNEL);
	if (!unit)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);
	init.ops = &audio_unit_clk_ops;
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

void __init sirfsoc_clk_audio_init(struct device_node *np)
{
	struct atlas7_audio_init_data *unit;
	int ret;
	int i;

	sirfsoc_audioclk_vbase = of_iomap(np, 0);
	if (!sirfsoc_audioclk_vbase)
		panic("unable to map audio clkc registers\n");

	for (i = 0; i < ARRAY_SIZE(audio_unit_list); i++) {
		unit = &audio_unit_list[i];
		audio_clks[i] = atlas7_audio_unit_clk_register(NULL,
				unit->unit_name, unit->parent_name,
				unit->flags, unit->regofs,
				unit->bit, unit->lock);
		BUG_ON(!audio_clks[i]);
	}

	audioclk_data.clks = audio_clks;
	audioclk_data.clk_num = ARRAY_SIZE(audio_unit_list);

	ret = of_clk_add_provider(np, of_clk_src_onecell_get, &audioclk_data);
	BUG_ON(ret);
}

CLK_OF_DECLARE(atlas7_clk, "sirf,atlas7-audioclk", sirfsoc_clk_audio_init);
