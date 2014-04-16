/*
 * Clock tree for CSR SiRFAtlas7
 *
 * Copyright (c) 2014 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/of_address.h>
#include <linux/syscore_ops.h>

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

#define SIRFSOC_CLKC_AUDIO_DTO_INC	     0x0088
#define SIRFSOC_CLKC_DISP0_DTO_INC	     0x008c
#define SIRFSOC_CLKC_DISP1_DTO_INC	     0x0090

#define SIRFSOC_CLKC_AUDIO_DTO_SRC           0x0094
#define SIRFSOC_CLKC_AUDIO_DTO_ENA	     0x0098
#define SIRFSOC_CLKC_AUDIO_DTO_DROFF	     0x009c

#define SIRFSOC_CLKC_DISP0_DTO_SRC           0x00a0
#define SIRFSOC_CLKC_DISP0_DTO_ENA	     0x00a4
#define SIRFSOC_CLKC_DISP0_DTO_DROFF	     0x00a8

#define SIRFSOC_CLKC_DISP1_DTO_SRC           0x00ac
#define SIRFSOC_CLKC_DISP1_DTO_ENA	     0x00b0
#define SIRFSOC_CLKC_DISP1_DTO_DROFF	     0x00b4

static void *sirfsoc_clk_vbase, *sirfsoc_clk_vbase;

static const struct clk_div_table pll_div_table[] = {
	{ .val = 0, .div = 1 },
	{ .val = 1, .div = 2 },
	{ .val = 2, .div = 4 },
	{ .val = 3, .div = 8 },
	{ .val = 4, .div = 16 },
	{ .val = 5, .div = 32 },
};

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


static DEFINE_SPINLOCK(cpupll_ctrl1_lock);
static DEFINE_SPINLOCK(mempll_ctrl1_lock);
static DEFINE_SPINLOCK(sys0pll_ctrl1_lock);
static DEFINE_SPINLOCK(sys1pll_ctrl1_lock);
static DEFINE_SPINLOCK(sys2pll_ctrl1_lock);
static DEFINE_SPINLOCK(sys3pll_ctrl1_lock);

static inline unsigned long clkc_readl(unsigned reg)
{
	return readl(sirfsoc_clk_vbase + reg);
}

static inline void clkc_writel(u32 val, unsigned reg)
{
	writel(val, sirfsoc_clk_vbase + reg);
}

/*
  ABPLL
  interger_n mode: Fvco = Fin * 2 * NF / NR
  Spread Spectrum mode: Fvco = Fin * SSN / NR
  SSN = 2^24 / (ssdiv | (ssmod << ssdepth))
 */
static unsigned long pll_clk_recalc_rate(struct clk_hw *hw,
	unsigned long parent_rate)
{
	unsigned long fin = parent_rate;
	struct clk_pll *clk = to_pllclk(hw);
	u32 regctrl0 = clkc_readl(clk->regofs + SIRFSOC_CLKC_MEMPLL_AB_CTRL0 -
			SIRFSOC_CLKC_MEMPLL_AB_FREQ);
	u32 regfreq = clkc_readl(clk->regofs);
	u32 regssc = clkc_readl(clk->regofs + SIRFSOC_CLKC_MEMPLL_AB_SSC -
			SIRFSOC_CLKC_MEMPLL_AB_FREQ);
	u32 nr = (regfreq  >> 16 & (BIT(3) - 1)) + 1;
	u32 nf = (regfreq & (BIT(9) - 1)) + 1;
	u32 ssdiv = regssc >> 8 & (BIT(12) - 1);
	u32 ssdepth = regssc >> 20 & (BIT(2) - 1);
	u32 ssmod = regssc & (BIT(8) - 1);
	u32 ssn;

	if (regctrl0 & SIRFSOC_ABPLL_CTRL0_BYPASS)
		return fin;

	if (regctrl0 & SIRFSOC_ABPLL_CTRL0_SSEN) {
		ssn = (1 << 24) / (ssdiv | (ssmod << ssdepth));
		return fin * ssn / nr;
	} else
		return 2 * fin * nf / nr;
}

static struct clk_ops ab_pll_ops = {
	.recalc_rate = pll_clk_recalc_rate,
};

static const char *pll_clk_parents[] = {
	"xin",
};

static struct clk_init_data clk_cpupll_init = {
	.name = "cpupll_vco",
	.ops = &ab_pll_ops,
	.parent_names = pll_clk_parents,
	.num_parents = ARRAY_SIZE(pll_clk_parents),
};

static struct clk_pll clk_cpupll = {
	.regofs = SIRFSOC_CLKC_CPUPLL_AB_FREQ,
	.hw = {
		.init = &clk_cpupll_init,
	},
};

static struct clk_init_data clk_mempll_init = {
	.name = "mempll_vco",
	.ops = &ab_pll_ops,
	.parent_names = pll_clk_parents,
	.num_parents = ARRAY_SIZE(pll_clk_parents),
};

static struct clk_pll clk_mempll = {
	.regofs = SIRFSOC_CLKC_MEMPLL_AB_FREQ,
	.hw = {
		.init = &clk_mempll_init,
	},
};

static struct clk_init_data clk_sys0pll_init = {
	.name = "sys0pll_vco",
	.ops = &ab_pll_ops,
	.parent_names = pll_clk_parents,
	.num_parents = ARRAY_SIZE(pll_clk_parents),
};

static struct clk_pll clk_sys0pll = {
	.regofs = SIRFSOC_CLKC_SYS0PLL_AB_FREQ,
	.hw = {
		.init = &clk_sys0pll_init,
	},
};

static struct clk_init_data clk_sys1pll_init = {
	.name = "sys1pll_vco",
	.ops = &ab_pll_ops,
	.parent_names = pll_clk_parents,
	.num_parents = ARRAY_SIZE(pll_clk_parents),
};

static struct clk_pll clk_sys1pll = {
	.regofs = SIRFSOC_CLKC_SYS1PLL_AB_FREQ,
	.hw = {
		.init = &clk_sys1pll_init,
	},
};

static struct clk_init_data clk_sys2pll_init = {
	.name = "sys2pll_vco",
	.ops = &ab_pll_ops,
	.parent_names = pll_clk_parents,
	.num_parents = ARRAY_SIZE(pll_clk_parents),
};

static struct clk_pll clk_sys2pll = {
	.regofs = SIRFSOC_CLKC_SYS2PLL_AB_FREQ,
	.hw = {
		.init = &clk_sys2pll_init,
	},
};

static struct clk_init_data clk_sys3pll_init = {
	.name = "sys3pll_vco",
	.ops = &ab_pll_ops,
	.parent_names = pll_clk_parents,
	.num_parents = ARRAY_SIZE(pll_clk_parents),
};

static struct clk_pll clk_sys3pll = {
	.regofs = SIRFSOC_CLKC_SYS3PLL_AB_FREQ,
	.hw = {
		.init = &clk_sys3pll_init,
	},
};

/*
   DTO in clkc, default enable double resolution mode
   double resolution mode:fout = fin * finc / 2^29
   normal mode:fout = fin * finc / 2^28
 */
static int dto_clk_is_enabled(struct clk_hw *hw)
{
	struct clk_dto *clk = to_dtoclk(hw);
	int reg;

	reg = clk->src_offset + SIRFSOC_CLKC_AUDIO_DTO_ENA - SIRFSOC_CLKC_AUDIO_DTO_SRC;

	return !!(clkc_readl(reg) & BIT(0));
}

static int dto_clk_enable(struct clk_hw *hw)
{
	u32 val, reg;
	struct clk_dto *clk = to_dtoclk(hw);

	reg = clk->src_offset + SIRFSOC_CLKC_AUDIO_DTO_ENA - SIRFSOC_CLKC_AUDIO_DTO_SRC;

	val = clkc_readl(reg) | BIT(0);
	clkc_writel(val, reg);
	return 0;
}

static void dto_clk_disable(struct clk_hw *hw)
{
	u32 val, reg;
	struct clk_dto *clk = to_dtoclk(hw);

	reg = clk->src_offset + SIRFSOC_CLKC_AUDIO_DTO_ENA - SIRFSOC_CLKC_AUDIO_DTO_SRC;

	val = clkc_readl(reg) & ~BIT(0);
	clkc_writel(val, reg);
}

static unsigned long dto_clk_recalc_rate(struct clk_hw *hw,
	unsigned long parent_rate)
{
	u64 rate = parent_rate;
	struct clk_dto *clk = to_dtoclk(hw);
	u32 finc = clkc_readl(clk->inc_offset);
	u32 droff = clkc_readl(clk->src_offset + SIRFSOC_CLKC_AUDIO_DTO_DROFF - SIRFSOC_CLKC_AUDIO_DTO_SRC);

	rate *= finc;
	if (droff & BIT(0))
		/* Double resolution off */
		do_div(rate, 1 << 28);
	else
		do_div(rate, 1 << 29);

	return rate;
}

static long dto_clk_round_rate(struct clk_hw *hw, unsigned long rate,
	unsigned long *parent_rate)
{
	u64 dividend = rate * (1 << 29);

	do_div(dividend, *parent_rate);
	dividend *= *parent_rate;
	do_div(dividend, 1 << 29);

	return dividend;
}

static int dto_clk_set_rate(struct clk_hw *hw, unsigned long rate,
	unsigned long parent_rate)
{
	u64 dividend = rate * 1 << 29;
	struct clk_dto *clk = to_dtoclk(hw);

	do_div(dividend, parent_rate);
	clkc_writel(0, clk->src_offset + SIRFSOC_CLKC_AUDIO_DTO_DROFF - SIRFSOC_CLKC_AUDIO_DTO_SRC);
	clkc_writel(dividend, clk->inc_offset);

	return 0;
}

static u8 dto_clk_get_parent(struct clk_hw *hw)
{
	struct clk_dto *clk = to_dtoclk(hw);

	return clkc_readl(clk->src_offset);
}

/*
   dto need CLK_SET_PARENT_GATE
*/
static int dto_clk_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_dto *clk = to_dtoclk(hw);

	clkc_writel(index, clk->src_offset);
	return 0;
}

static struct clk_ops dto_ops = {
	.is_enabled = dto_clk_is_enabled,
	.enable = dto_clk_enable,
	.disable = dto_clk_disable,
	.recalc_rate = dto_clk_recalc_rate,
	.round_rate = dto_clk_round_rate,
	.set_rate = dto_clk_set_rate,
	.get_parent = dto_clk_get_parent,
	.set_parent = dto_clk_set_parent,
};

/* dto parent clock as syspllvco/clk1 */
static const char *audiodto_clk_parents[] = {
	"sys0pll_clk1",
	"sys1pll_clk1",
	"sys3pll_clk1",
};

static struct clk_init_data clk_audiodto_init = {
	.name = "audio_dto",
	.ops = &dto_ops,
	.parent_names = audiodto_clk_parents,
	.num_parents = ARRAY_SIZE(audiodto_clk_parents),
};

static struct clk_dto clk_audio_dto = {
	.inc_offset = SIRFSOC_CLKC_AUDIO_DTO_INC,
	.src_offset = SIRFSOC_CLKC_AUDIO_DTO_SRC,
	.hw = {
		.init = &clk_audiodto_init,
	},
};

static const char *disp0dto_clk_parents[] = {
	"sys0pll_clk1",
	"sys1pll_clk1",
	"sys3pll_clk1",
};

static struct clk_init_data clk_disp0dto_init = {
	.name = "disp0_dto",
	.ops = &dto_ops,
	.parent_names = disp0dto_clk_parents,
	.num_parents = ARRAY_SIZE(disp0dto_clk_parents),
};

static struct clk_dto clk_disp0_dto = {
	.inc_offset = SIRFSOC_CLKC_DISP0_DTO_INC,
	.src_offset = SIRFSOC_CLKC_DISP0_DTO_SRC,
	.hw = {
		.init = &clk_disp0dto_init,
	},
};

static const char *disp1dto_clk_parents[] = {
	"sys0pll_clk1",
	"sys1pll_clk1",
	"sys3pll_clk1",
};

static struct clk_init_data clk_disp1dto_init = {
	.name = "disp1_dto",
	.ops = &dto_ops,
	.parent_names = disp1dto_clk_parents,
	.num_parents = ARRAY_SIZE(disp1dto_clk_parents),
};

static struct clk_dto clk_disp1_dto = {
	.inc_offset = SIRFSOC_CLKC_DISP1_DTO_INC,
	.src_offset = SIRFSOC_CLKC_DISP1_DTO_SRC,
	.hw = {
		.init = &clk_disp1dto_init,
	},
};

/* pwm clock as syspllvco/clk2 */
static const char *pwm1_clk_parents[] = {
	"sys0pll_clk2",
	"sys1pll_clk2",
	"sys3pll_clk2",
};

static const char *pwm2_clk_parents[] = {
	"sys0pll_clk2",
	"sys1pll_clk2",
	"sys3pll_clk2",
};

static const char *pwm3_clk_parents[] = {
	"sys0pll_clk2",
	"sys1pll_clk2",
	"sys3pll_clk2",
};

static const char *usbphy_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a1"
	"sys1pll_a1",
	"sys2pll_a1",
	"sys3pll_a1",
};

static const char *bt_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a2"
	"sys1pll_a2",
	"sys2pll_a2",
	"sys3pll_a2",
};

static const char *rgmii_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a3"
	"sys1pll_a3",
	"sys2pll_a3",
	"sys3pll_a3",
};

static const char *cpu_clk_parents[] = {
	"xin",
	"xinw",
	"cpupll_clk1"
	"sys0pll_a4",
	"sys1pll_a4",
};

static const char *sdphy01_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a5"
	"sys1pll_a5",
	"sys2pll_a5",
	"sys3pll_a5",
};

static const char *sdphy23_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a6"
	"sys1pll_a6",
	"sys2pll_a6",
	"sys3pll_a6",
};

static const char *sdphy45_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a7"
	"sys1pll_a7",
	"sys2pll_a7",
	"sys3pll_a7",
};

static const char *sdphy67_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a8"
	"sys1pll_a8",
	"sys2pll_a8",
	"sys3pll_a8",
};

static const char *can_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a9"
	"sys1pll_a9",
	"sys2pll_a9",
	"sys3pll_a9",
};

static const char *deint_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a10"
	"sys1pll_a10",
	"sys2pll_a10",
	"sys3pll_a10",
};

static const char *nand_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a11"
	"sys1pll_a11",
	"sys2pll_a11",
	"sys3pll_a11",
};

static const char *disp0vpp0_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a12"
	"sys1pll_a12",
	"sys2pll_a12",
	"sys3pll_a12",
	"disp0_dto",
};

static const char *disp1vpp1_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a13"
	"sys1pll_a13",
	"sys2pll_a13",
	"sys3pll_a13",
	"disp1_dto",
};

static const char *gpu_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a14"
	"sys1pll_a14",
	"sys2pll_a14",
	"sys3pll_a14",
};

static const char *gnss_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a15"
	"sys1pll_a15",
	"sys2pll_a15",
	"sys3pll_a15",
};

static const char *spar1_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a16"
	"sys1pll_a16",
	"sys2pll_16",
	"sys3pll_a16",
};

static const char *sys0a20_clk_parents[] = {
	"sys0pll_fixdiv",
};

static const char *sys1a17_clk_parents[] = {
	"sys1pll_fixdiv",
};

static const char *sys1a18_clk_parents[] = {
	"sys1pll_fixdiv",
};

static const char *sys1a19_clk_parents[] = {
	"sys1pll_fixdiv",
};

static const char *sys1a20_clk_parents[] = {
	"sys1pll_fixdiv",
};

static const char *sys2a20_clk_parents[] = {
	"sys2pll_fixdiv",
};

static const char *sys_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a20",
	"sys1pll_a17",
	"sys1pll_a18",
	"sys1pll_a19",
	"sys1pll_a20",
	"sys2pll_a20",
};

static const char *io_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a20",
	"sys1pll_a17",
	"sys1pll_a18",
	"sys1pll_a19",
	"sys1pll_a20",
	"sys2pll_a20",
};

static const char *g2d_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a20",
	"sys1pll_a17",
	"sys1pll_a18",
	"sys1pll_a19",
	"sys1pll_a20",
	"sys2pll_a20",
};

static const char *jpegc_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a20",
	"sys1pll_a17",
	"sys1pll_a18",
	"sys1pll_a19",
	"sys1pll_a20",
	"sys2pll_a20",
};

static const char *vdec_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a20",
	"sys1pll_a17",
	"sys1pll_a18",
	"sys1pll_a19",
	"sys1pll_a20",
	"sys2pll_a20",
};

static const char *gmac_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a20",
	"sys1pll_a17",
	"sys1pll_a18",
	"sys1pll_a19",
	"sys1pll_a20",
	"sys2pll_a20",
};

static const char *usb_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a20",
	"sys1pll_a17",
	"sys1pll_a18",
	"sys1pll_a19",
	"sys1pll_a20",
	"sys2pll_a20",
};

static const char *kas_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a20",
	"sys1pll_a17",
	"sys1pll_a18",
	"sys1pll_a19",
	"sys1pll_a20",
	"sys2pll_a20",
};

static const char *sec_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a20",
	"sys1pll_a17",
	"sys1pll_a18",
	"sys1pll_a19",
	"sys1pll_a20",
	"sys2pll_a20",
};

static const char *sdr_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a20",
	"sys1pll_a17",
	"sys1pll_a18",
	"sys1pll_a19",
	"sys1pll_a20",
	"sys2pll_a20",
};

static const char *vip_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a20",
	"sys1pll_a17",
	"sys1pll_a18",
	"sys1pll_a19",
	"sys1pll_a20",
	"sys2pll_a20",
};

static const char *nocd_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a20",
	"sys1pll_a17",
	"sys1pll_a18",
	"sys1pll_a19",
	"sys1pll_a20",
	"sys2pll_a20",
};

static const char *nocr_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a20",
	"sys1pll_a17",
	"sys1pll_a18",
	"sys1pll_a19",
	"sys1pll_a20",
	"sys2pll_a20",
};

static const char *tpiu_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a20",
	"sys1pll_a17",
	"sys1pll_a18",
	"sys1pll_a19",
	"sys1pll_a20",
	"sys2pll_a20",
};

void __init atlas7_clk_init(struct device_node *np)
{
	struct clk *clk;


	sirfsoc_clk_vbase = of_iomap(np, 0);
	if (!sirfsoc_clk_vbase)
		panic("unable to map clkc registers\n");

	of_node_put(np);
	/* These are always available (32k osc xinw and 26MHz OSC xin) */
	clk_register_fixed_rate(NULL, "xinw", NULL,
		CLK_IS_ROOT, 32768);
	clk_register_fixed_rate(NULL, "xin", NULL,
		CLK_IS_ROOT, 26000000);

	clk = clk_register(NULL, &clk_cpupll.hw);
	BUG_ON(!clk);
	clk = clk_register(NULL, &clk_mempll.hw);
	BUG_ON(!clk);
	clk = clk_register(NULL, &clk_sys0pll.hw);
	BUG_ON(!clk);
	clk = clk_register(NULL, &clk_sys1pll.hw);
	BUG_ON(!clk);
	clk = clk_register(NULL, &clk_sys2pll.hw);
	BUG_ON(!clk);
	clk = clk_register(NULL, &clk_sys3pll.hw);
	BUG_ON(!clk);

	clk = clk_register_divider_table(NULL, "cpupll_div1", "cpupll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_CPUPLL_AB_CTRL1, 0, 3, 0,
			 pll_div_table, &cpupll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_divider_table(NULL, "cpupll_div2", "cpupll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_CPUPLL_AB_CTRL1, 4, 3, 0,
			 pll_div_table, &cpupll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_divider_table(NULL, "cpupll_div3", "cpupll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_CPUPLL_AB_CTRL1, 8, 3, 0,
			 pll_div_table, &cpupll_ctrl1_lock);
	BUG_ON(!clk);

	clk = clk_register_divider_table(NULL, "mempll_div1", "mempll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_MEMPLL_AB_CTRL1, 0, 3, 0,
			 pll_div_table, &mempll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_divider_table(NULL, "mempll_div2", "mempll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_MEMPLL_AB_CTRL1, 4, 3, 0,
			 pll_div_table, &mempll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_divider_table(NULL, "mempll_div3", "mempll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_MEMPLL_AB_CTRL1, 8, 3, 0,
			 pll_div_table, &mempll_ctrl1_lock);
	BUG_ON(!clk);

	clk = clk_register_divider_table(NULL, "sys0pll_div1", "sys0pll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS0PLL_AB_CTRL1, 0, 3, 0,
			 pll_div_table, &sys0pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_divider_table(NULL, "sys0pll_div2", "sys0pll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS0PLL_AB_CTRL1, 4, 3, 0,
			 pll_div_table, &sys0pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_divider_table(NULL, "sys0pll_div3", "sys0pll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS0PLL_AB_CTRL1, 8, 3, 0,
			 pll_div_table, &sys0pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_fixed_factor(NULL, "sys0pll_fixdiv", "sys0pll_vco",
					CLK_SET_RATE_PARENT, 1, 2);

	clk = clk_register_divider_table(NULL, "sys1pll_div1", "sys1pll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS1PLL_AB_CTRL1, 0, 3, 0,
			 pll_div_table, &sys1pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_divider_table(NULL, "sys1pll_div2", "sys1pll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS1PLL_AB_CTRL1, 4, 3, 0,
			 pll_div_table, &sys1pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_divider_table(NULL, "sys1pll_div3", "sys1pll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS1PLL_AB_CTRL1, 8, 3, 0,
			 pll_div_table, &sys1pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_fixed_factor(NULL, "sys1pll_fixdiv", "sys1pll_vco",
					CLK_SET_RATE_PARENT, 1, 2);

	clk = clk_register_divider_table(NULL, "sys2pll_div1", "sys2pll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS2PLL_AB_CTRL1, 0, 3, 0,
			 pll_div_table, &sys2pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_divider_table(NULL, "sys2pll_div2", "sys2pll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS2PLL_AB_CTRL1, 4, 3, 0,
			 pll_div_table, &sys2pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_divider_table(NULL, "sys2pll_div3", "sys2pll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS2PLL_AB_CTRL1, 8, 3, 0,
			 pll_div_table, &sys2pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_fixed_factor(NULL, "sys2pll_fixdiv", "sys2pll_vco",
					CLK_SET_RATE_PARENT, 1, 2);

	clk = clk_register_divider_table(NULL, "sys3pll_div1", "sys3pll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS3PLL_AB_CTRL1, 0, 3, 0,
			 pll_div_table, &sys3pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_divider_table(NULL, "sys3pll_div2", "sys3pll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS3PLL_AB_CTRL1, 4, 3, 0,
			 pll_div_table, &sys3pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_divider_table(NULL, "sys3pll_div3", "sys3pll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS3PLL_AB_CTRL1, 8, 3, 0,
			 pll_div_table, &sys3pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_fixed_factor(NULL, "sys3pll_fixdiv", "sys3pll_vco",
					CLK_SET_RATE_PARENT, 1, 2);

	BUG_ON(!clk);
	clk = clk_register_gate(NULL, "cpupll_clk1", "cpupll_div1",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_CPUPLL_AB_CTRL1,
				12, 0, &cpupll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_gate(NULL, "cpupll_clk2", "cpupll_div2",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_CPUPLL_AB_CTRL1,
				13, 0, &cpupll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_gate(NULL, "cpupll_clk3", "cpupll_div3",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_CPUPLL_AB_CTRL1,
				14, 0, &cpupll_ctrl1_lock);
	BUG_ON(!clk);


	clk = clk_register_gate(NULL, "mempll_clk1", "mempll_div1",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_MEMPLL_AB_CTRL1,
				12, 0, &mempll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_gate(NULL, "mempll_clk2", "mempll_div2",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_MEMPLL_AB_CTRL1,
				13, 0, &mempll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_gate(NULL, "mempll_clk3", "mempll_div3",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_MEMPLL_AB_CTRL1,
				14, 0, &mempll_ctrl1_lock);
	BUG_ON(!clk);

	clk = clk_register_gate(NULL, "sys0pll_clk1", "sys0pll_div1",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS0PLL_AB_CTRL1,
				12, 0, &sys0pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_gate(NULL, "sys0pll_clk2", "sys0pll_div2",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS0PLL_AB_CTRL1,
				13, 0, &sys0pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_gate(NULL, "sys0pll_clk3", "sys0pll_div3",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS0PLL_AB_CTRL1,
				14, 0, &sys0pll_ctrl1_lock);
	BUG_ON(!clk);

	clk = clk_register_gate(NULL, "sys1pll_clk1", "sys1pll_div1",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS1PLL_AB_CTRL1,
				12, 0, &sys1pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_gate(NULL, "sys1pll_clk2", "sys1pll_div2",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS1PLL_AB_CTRL1,
				13, 0, &sys1pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_gate(NULL, "sys1pll_clk3", "sys1pll_div3",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS1PLL_AB_CTRL1,
				14, 0, &sys1pll_ctrl1_lock);
	BUG_ON(!clk);

	clk = clk_register_gate(NULL, "sys2pll_clk1", "sys2pll_div1",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS2PLL_AB_CTRL1,
				12, 0, &sys2pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_gate(NULL, "sys2pll_clk2", "sys2pll_div2",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS2PLL_AB_CTRL1,
				13, 0, &sys2pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_gate(NULL, "sys2pll_clk3", "sys2pll_div3",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS2PLL_AB_CTRL1,
				14, 0, &sys2pll_ctrl1_lock);
	BUG_ON(!clk);

	clk = clk_register_gate(NULL, "sys3pll_clk1", "sys3pll_div1",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS3PLL_AB_CTRL1,
				12, 0, &sys3pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_gate(NULL, "sys3pll_clk2", "sys3pll_div2",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS3PLL_AB_CTRL1,
				13, 0, &sys3pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_gate(NULL, "sys3pll_clk3", "sys3pll_div3",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS3PLL_AB_CTRL1,
				14, 0, &sys0pll_ctrl1_lock);
	BUG_ON(!clk);

	clk = clk_register(NULL, &clk_audio_dto.hw);
	BUG_ON(!clk);

	clk = clk_register(NULL, &clk_disp0_dto.hw);
	BUG_ON(!clk);

	clk = clk_register(NULL, &clk_disp1_dto.hw);
	BUG_ON(!clk);

}
CLK_OF_DECLARE(atlas7_clk, "sirf,atlas7-clkc", atlas7_clk_init);
