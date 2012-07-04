/*
 * Clock tree for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/spinlock.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/syscore_ops.h>
#include <asm/mach/map.h>
#include <mach/map.h>

#define SIRFSOC_CLKC_CLK_EN0    0x0000
#define SIRFSOC_CLKC_CLK_EN1    0x0004
#define SIRFSOC_CLKC_REF_CFG    0x0014
#define SIRFSOC_CLKC_CPU_CFG    0x0018
#define SIRFSOC_CLKC_MEM_CFG    0x001c
#define SIRFSOC_CLKC_SYS_CFG    0x0020
#define SIRFSOC_CLKC_IO_CFG     0x0024
#define SIRFSOC_CLKC_DSP_CFG    0x0028
#define SIRFSOC_CLKC_GFX_CFG    0x002c
#define SIRFSOC_CLKC_MM_CFG     0x0030
#define SIRFSOC_LKC_LCD_CFG     0x0034
#define SIRFSOC_CLKC_MMC_CFG    0x0038
#define SIRFSOC_CLKC_PLL1_CFG0  0x0040
#define SIRFSOC_CLKC_PLL2_CFG0  0x0044
#define SIRFSOC_CLKC_PLL3_CFG0  0x0048
#define SIRFSOC_CLKC_PLL1_CFG1  0x004c
#define SIRFSOC_CLKC_PLL2_CFG1  0x0050
#define SIRFSOC_CLKC_PLL3_CFG1  0x0054
#define SIRFSOC_CLKC_PLL1_CFG2  0x0058
#define SIRFSOC_CLKC_PLL2_CFG2  0x005c
#define SIRFSOC_CLKC_PLL3_CFG2  0x0060

#define SIRFSOC_CLOCK_VA_BASE		SIRFSOC_VA(0x005000)

#define KHZ     1000
#define MHZ     (KHZ * KHZ)

/*
 * SiRFprimaII clock controller
 * - 2 oscillators: osc-26MHz, rtc-32.768KHz
 * - 3 standard configurable plls: pll1, pll2 & pll3
 * - 2 exclusive plls: usb phy pll and sata phy pll
 * - 8 clock domains: cpu/cpudiv, mem/memdiv, sys/io, dsp, graphic, multimedia,
 *     display and sdphy.
 *     Each clock domain can select its own clock source from five clock sources,
 *     X_XIN, X_XINW, PLL1, PLL2 and PLL3. The domain clock is used as the source
 *     clock of the group clock.
 */

struct clk_pll {
	struct clk_hw hw;
	unsigned short regofs;  /* register offset */
};

#define to_pllclk(_hw) container_of(_hw, struct clk_pll, hw)

struct clk_dmn {
	struct clk_hw hw;
	unsigned short regofs;  /* register offset */
};

#define to_dmnclk(_hw) container_of(_hw, struct clk_dmn, hw)

struct clk_std {
	struct clk_hw hw;
	signed char enable_bit; /* enable bit: 0 ~ 63 */
};

#define to_stdclk(_hw) container_of(_hw, struct clk_std, hw)

static DEFINE_SPINLOCK(clocks_lock);

static inline unsigned long clkc_readl(unsigned reg)
{
	return readl(SIRFSOC_CLOCK_VA_BASE + reg);
}

static inline void clkc_writel(u32 val, unsigned reg)
{
	writel(val, SIRFSOC_CLOCK_VA_BASE + reg);
}

/*
 * std pll
 */

static unsigned long pll_clk_recalc_rate(struct clk_hw *hw,
	unsigned long parent_rate)
{
	unsigned long fin = parent_rate;
	struct clk_pll *clk = to_pllclk(hw);
	u32 regcfg2 = clk->regofs + SIRFSOC_CLKC_PLL1_CFG2 -
		SIRFSOC_CLKC_PLL1_CFG0;

	if (clkc_readl(regcfg2) & BIT(2)) {
		/* pll bypass mode */
		return fin;
	} else {
		/* fout = fin * nf / nr / od */
		u32 cfg0 = clkc_readl(clk->regofs);
		u32 nf = (cfg0 & (BIT(13) - 1)) + 1;
		u32 nr = ((cfg0 >> 13) & (BIT(6) - 1)) + 1;
		u32 od = ((cfg0 >> 19) & (BIT(4) - 1)) + 1;
		WARN_ON(fin % MHZ);
		return fin / MHZ * nf / nr / od * MHZ;
	}
}

static long pll_clk_round_rate(struct clk_hw *hw, unsigned long rate,
			unsigned long *parent_rate)
{
	unsigned long fin, nf, nr, od;

	/*
	 * fout = fin * nf / (nr * od);
	 * set od = 1, nr = fin/MHz, so fout = nf * MHz
	 */
	rate = rate - rate % MHZ;

	nf = rate / MHZ;
	if (nf > BIT(13))
		nf = BIT(13);
	if (nf < 1)
		nf = 1;

	fin = *parent_rate;

	nr = fin / MHZ;
	if (nr > BIT(6))
		nr = BIT(6);
	od = 1;

	return fin * nf / (nr * od);
}

static int pll_clk_set_rate(struct clk_hw *hw, unsigned long rate,
			unsigned long parent_rate)
{
	struct clk_pll *clk = to_pllclk(hw);
	unsigned long fin, nf, nr, od, reg;

	/*
	 * fout = fin * nf / (nr * od);
	 * set od = 1, nr = fin/MHz, so fout = nf * MHz
	 */

	nf = rate / MHZ;
	if (unlikely((rate % MHZ) || nf > BIT(13) || nf < 1))
		return -EINVAL;

	fin = parent_rate;
	BUG_ON(fin < MHZ);

	nr = fin / MHZ;
	BUG_ON((fin % MHZ) || nr > BIT(6));

	od = 1;

	reg = (nf - 1) | ((nr - 1) << 13) | ((od - 1) << 19);
	clkc_writel(reg, clk->regofs);

	reg = clk->regofs + SIRFSOC_CLKC_PLL1_CFG1 - SIRFSOC_CLKC_PLL1_CFG0;
	clkc_writel((nf >> 1) - 1, reg);

	reg = clk->regofs + SIRFSOC_CLKC_PLL1_CFG2 - SIRFSOC_CLKC_PLL1_CFG0;
	while (!(clkc_readl(reg) & BIT(6)))
		cpu_relax();

	return 0;
}

static struct clk_ops std_pll_ops = {
	.recalc_rate = pll_clk_recalc_rate,
	.round_rate = pll_clk_round_rate,
	.set_rate = pll_clk_set_rate,
};

static const char * const pll_clk_parents[] = {
	"osc",
};

static struct clk_init_data clk_pll1_init = {
	.name = "pll1",
	.ops = &std_pll_ops,
	.parent_names = pll_clk_parents,
	.num_parents = ARRAY_SIZE(pll_clk_parents),
	.flags = CLK_SET_RATE_GATE,
};

static struct clk_init_data clk_pll2_init = {
	.name = "pll2",
	.ops = &std_pll_ops,
	.parent_names = pll_clk_parents,
	.num_parents = ARRAY_SIZE(pll_clk_parents),
	.flags = CLK_SET_RATE_GATE,
};

static struct clk_init_data clk_pll3_init = {
	.name = "pll3",
	.ops = &std_pll_ops,
	.parent_names = pll_clk_parents,
	.num_parents = ARRAY_SIZE(pll_clk_parents),
	.flags = CLK_SET_RATE_GATE,
};

static struct clk_pll clk_pll1 = {
	.regofs = SIRFSOC_CLKC_PLL1_CFG0,
	.hw = {
		.init = &clk_pll1_init,
	},
};

static struct clk_pll clk_pll2 = {
	.regofs = SIRFSOC_CLKC_PLL2_CFG0,
	.hw = {
		.init = &clk_pll2_init,
	},
};

static struct clk_pll clk_pll3 = {
	.regofs = SIRFSOC_CLKC_PLL3_CFG0,
	.hw = {
		.init = &clk_pll3_init,
	},
};

/*
 * clock domains - cpu, mem, sys/io
 */

static const char * const dmn_clk_parents[] = {
	"rtc",
	"osc",
	"pll1",
	"pll2",
	"pll3",
};

static u8 dmn_clk_get_parent(struct clk_hw *hw)
{
	struct clk_dmn *clk = to_dmnclk(hw);
	u32 cfg = clkc_readl(clk->regofs);

	/* parent of io domain can only be pll3 */
	if (strcmp(hw->init->name, "io") == 0)
		return 4;

	WARN_ON((cfg & (BIT(3) - 1)) > 4);

	return cfg & (BIT(3) - 1);
}

static int dmn_clk_set_parent(struct clk_hw *hw, u8 parent)
{
	struct clk_dmn *clk = to_dmnclk(hw);
	u32 cfg = clkc_readl(clk->regofs);

	/* parent of io domain can only be pll3 */
	if (strcmp(hw->init->name, "io") == 0)
		return -EINVAL;

	cfg &= ~(BIT(3) - 1);
	clkc_writel(cfg | parent, clk->regofs);
	/* BIT(3) - switching status: 1 - busy, 0 - done */
	while (clkc_readl(clk->regofs) & BIT(3))
		cpu_relax();

	return 0;
}

static unsigned long dmn_clk_recalc_rate(struct clk_hw *hw,
	unsigned long parent_rate)

{
	unsigned long fin = parent_rate;
	struct clk_dmn *clk = to_dmnclk(hw);

	u32 cfg = clkc_readl(clk->regofs);

	if (cfg & BIT(24)) {
		/* fcd bypass mode */
		return fin;
	} else {
		/*
		 * wait count: bit[19:16], hold count: bit[23:20]
		 */
		u32 wait = (cfg >> 16) & (BIT(4) - 1);
		u32 hold = (cfg >> 20) & (BIT(4) - 1);

		return fin / (wait + hold + 2);
	}
}

static long dmn_clk_round_rate(struct clk_hw *hw, unsigned long rate,
	unsigned long *parent_rate)
{
	unsigned long fin;
	unsigned ratio, wait, hold;
	unsigned bits = (strcmp(hw->init->name, "mem") == 0) ? 3 : 4;

	fin = *parent_rate;
	ratio = fin / rate;

	if (ratio < 2)
		ratio = 2;
	if (ratio > BIT(bits + 1))
		ratio = BIT(bits + 1);

	wait = (ratio >> 1) - 1;
	hold = ratio - wait - 2;

	return fin / (wait + hold + 2);
}

static int dmn_clk_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk_dmn *clk = to_dmnclk(hw);
	unsigned long fin;
	unsigned ratio, wait, hold, reg;
	unsigned bits = (strcmp(hw->init->name, "mem") == 0) ? 3 : 4;

	fin = parent_rate;
	ratio = fin / rate;

	if (unlikely(ratio < 2 || ratio > BIT(bits + 1)))
		return -EINVAL;

	WARN_ON(fin % rate);

	wait = (ratio >> 1) - 1;
	hold = ratio - wait - 2;

	reg = clkc_readl(clk->regofs);
	reg &= ~(((BIT(bits) - 1) << 16) | ((BIT(bits) - 1) << 20));
	reg |= (wait << 16) | (hold << 20) | BIT(25);
	clkc_writel(reg, clk->regofs);

	/* waiting FCD been effective */
	while (clkc_readl(clk->regofs) & BIT(25))
		cpu_relax();

	return 0;
}

static struct clk_ops msi_ops = {
	.set_rate = dmn_clk_set_rate,
	.round_rate = dmn_clk_round_rate,
	.recalc_rate = dmn_clk_recalc_rate,
	.set_parent = dmn_clk_set_parent,
	.get_parent = dmn_clk_get_parent,
};

static struct clk_init_data clk_mem_init = {
	.name = "mem",
	.ops = &msi_ops,
	.parent_names = dmn_clk_parents,
	.num_parents = ARRAY_SIZE(dmn_clk_parents),
	.flags = CLK_SET_RATE_GATE,
};

static struct clk_dmn clk_mem = {
	.regofs = SIRFSOC_CLKC_MEM_CFG,
	.hw = {
		.init = &clk_mem_init,
	},
};

static struct clk_init_data clk_sys_init = {
	.name = "sys",
	.ops = &msi_ops,
	.parent_names = dmn_clk_parents,
	.num_parents = ARRAY_SIZE(dmn_clk_parents),
	.flags = CLK_SET_RATE_GATE,
};

static struct clk_dmn clk_sys = {
	.regofs = SIRFSOC_CLKC_SYS_CFG,
	.hw = {
		.init = &clk_sys_init,
	},
};

static struct clk_init_data clk_io_init = {
	.name = "io",
	.ops = &msi_ops,
	.parent_names = dmn_clk_parents,
	.num_parents = ARRAY_SIZE(dmn_clk_parents),
	.flags = CLK_SET_RATE_GATE,
};

static struct clk_dmn clk_io = {
	.regofs = SIRFSOC_CLKC_IO_CFG,
	.hw = {
		.init = &clk_io_init,
	},
};

static struct clk_ops cpu_ops = {
	.set_parent = dmn_clk_set_parent,
	.get_parent = dmn_clk_get_parent,
};

static struct clk_init_data clk_cpu_init = {
	.name = "cpu",
	.ops = &cpu_ops,
	.parent_names = dmn_clk_parents,
	.num_parents = ARRAY_SIZE(dmn_clk_parents),
	.flags = CLK_SET_RATE_PARENT,
};

static struct clk_dmn clk_cpu = {
	.regofs = SIRFSOC_CLKC_CPU_CFG,
	.hw = {
		.init = &clk_cpu_init,
	},
};

/*
 * peripheral controllers in io domain
 */

static int std_clk_enable(struct clk_hw *hw)
{
	u32 val, reg;
	int bit;
	struct clk_std *clk = to_stdclk(hw);

	BUG_ON(clk->enable_bit < 0 || clk->enable_bit > 63);

	bit = clk->enable_bit % 32;
	reg = clk->enable_bit / 32;
	reg = SIRFSOC_CLKC_CLK_EN0 + reg * sizeof(reg);

	val = clkc_readl(reg) | BIT(bit);
	clkc_writel(val, reg);
	return 0;
}

static void std_clk_disable(struct clk_hw *hw)
{
	u32 val, reg;
	int bit;
	struct clk_std *clk = to_stdclk(hw);

	BUG_ON(clk->enable_bit < 0 || clk->enable_bit > 63);

	bit = clk->enable_bit % 32;
	reg = clk->enable_bit / 32;
	reg = SIRFSOC_CLKC_CLK_EN0 + reg * sizeof(reg);

	val = clkc_readl(reg) & ~BIT(bit);
	clkc_writel(val, reg);
}

static const char * const std_clk_parents[] = {
	"io",
};

static struct clk_ops ios_ops = {
	.enable = std_clk_enable,
	.disable = std_clk_disable,
};

static struct clk_init_data clk_spi0_init = {
	.name = "spi0",
	.ops = &ios_ops,
	.parent_names = std_clk_parents,
	.num_parents = ARRAY_SIZE(std_clk_parents),
};

static struct clk_std clk_spi0 = {
	.enable_bit = 43,
	.hw = {
		.init = &clk_spi0_init,
	},
};

static struct clk_init_data clk_spi1_init = {
	.name = "spi1",
	.ops = &ios_ops,
	.parent_names = std_clk_parents,
	.num_parents = ARRAY_SIZE(std_clk_parents),
};

static struct clk_std clk_spi1 = {
	.enable_bit = 44,
	.hw = {
		.init = &clk_spi1_init,
	},
};

static struct clk_init_data clk_i2c0_init = {
	.name = "i2c0",
	.ops = &ios_ops,
	.parent_names = std_clk_parents,
	.num_parents = ARRAY_SIZE(std_clk_parents),
};

static struct clk_std clk_i2c0 = {
	.enable_bit = 46,
	.hw = {
		.init = &clk_i2c0_init,
	},
};

static struct clk_init_data clk_i2c1_init = {
	.name = "i2c1",
	.ops = &ios_ops,
	.parent_names = std_clk_parents,
	.num_parents = ARRAY_SIZE(std_clk_parents),
};

static struct clk_std clk_i2c1 = {
	.enable_bit = 47,
	.hw = {
		.init = &clk_i2c1_init,
	},
};

void __init sirfsoc_clk_init(void)
{
	struct clk *clk;

	/* These are always available (RTC and 26MHz OSC)*/
	clk = clk_register_fixed_rate(NULL, "rtc", NULL,
				      CLK_IS_ROOT, 32768);
	BUG_ON(!clk);
	clk = clk_register_fixed_rate(NULL, "osc", NULL,
				      CLK_IS_ROOT, 26000000);
	BUG_ON(!clk);

	clk = clk_register(NULL, &clk_pll1.hw);
	BUG_ON(!clk);
	clk = clk_register(NULL, &clk_pll2.hw);
	BUG_ON(!clk);
	clk = clk_register(NULL, &clk_pll3.hw);
	BUG_ON(!clk);
	clk = clk_register(NULL, &clk_mem.hw);
	BUG_ON(!clk);
	clk = clk_register(NULL, &clk_sys.hw);
	BUG_ON(!clk);
	clk = clk_register(NULL, &clk_io.hw);
	BUG_ON(!clk);
	clk_register_clkdev(clk, NULL, "io");
	clk = clk_register(NULL, &clk_cpu.hw);
	BUG_ON(!clk);
	clk_register_clkdev(clk, NULL, "cpu");

	clk = clk_register(NULL, &clk_i2c0.hw);
	BUG_ON(!clk);
	clk_register_clkdev(clk, NULL, "i2c0");
	clk = clk_register(NULL, &clk_i2c1.hw);
	BUG_ON(!clk);
	clk_register_clkdev(clk, NULL, "i2c1");
	clk = clk_register(NULL, &clk_spi0.hw);
	BUG_ON(!clk);
	clk_register_clkdev(clk, NULL, "spi0");
	clk = clk_register(NULL, &clk_spi1.hw);
	BUG_ON(!clk);
	clk_register_clkdev(clk, NULL, "spi1");

	/* enable all clocks for testing */
	clkc_writel(0xFFFFFFFF, SIRFSOC_CLKC_CLK_EN0);
	clkc_writel(0xFFFFFFFF, SIRFSOC_CLKC_CLK_EN1);
}

static struct of_device_id clkc_ids[] = {
	{ .compatible = "sirf,prima2-clkc" },
	{},
};

void __init sirfsoc_clk_map(void)
{
	struct device_node *np;
	struct resource res;
	struct map_desc sirfsoc_clkc_iodesc = {
		.virtual = SIRFSOC_CLOCK_VA_BASE,
		.type    = MT_DEVICE,
	};

	np = of_find_matching_node(NULL, clkc_ids);
	if (!np)
		panic("unable to find compatible clkc node in dtb\n");

	if (of_address_to_resource(np, 0, &res))
		panic("unable to find clkc range in dtb");
	of_node_put(np);

	sirfsoc_clkc_iodesc.pfn = __phys_to_pfn(res.start);
	sirfsoc_clkc_iodesc.length = 1 + res.end - res.start;

	iotable_init(&sirfsoc_clkc_iodesc, 1);
}

/*
 * clk disable/enable should be doned by every device
 * here we just give a workaround to enable all clk
 */
static int sirfsoc_clk_suspend(void)
{
	return 0;
}

static void sirfsoc_clk_resume(void)
{
	clkc_writel(0xFFFFFFFF, SIRFSOC_CLKC_CLK_EN0);
	clkc_writel(0xFFFFFFFF, SIRFSOC_CLKC_CLK_EN1);
}

static struct syscore_ops sirfsoc_clk_syscore_ops = {
	.suspend	= sirfsoc_clk_suspend,
	.resume		= sirfsoc_clk_resume,
};

static int __init sirfsoc_clk_pm_init(void)
{
	register_syscore_ops(&sirfsoc_clk_syscore_ops);
	return 0;
}
device_initcall(sirfsoc_clk_pm_init);
