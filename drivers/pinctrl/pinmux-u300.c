/*
 * Driver for the U300 pin controller
 *
 * Based on the original U300 padmux functions
 * Copyright (C) 2009-2011 ST-Ericsson AB
 * Author: Martin Persson <martin.persson@stericsson.com>
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * The DB3350 design and control registers are oriented around pads rather than
 * pins, so we enumerate the pads we can mux rather than actual pins. The pads
 * are connected to different pins in different packaging types, so it would
 * be confusing.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include "pinmux-u300.h"

#define DRIVER_NAME "pinmux-u300"

/*
 * The DB3350 has 467 pads, I have enumerated the pads clockwise around the
 * edges of the silicon, finger by finger. LTCORNER upper left is pad 0.
 * Data taken from the PadRing chart, arranged like this:
 *
 *   0 ..... 104
 * 466        105
 *   .        .
 *   .        .
 * 358        224
 *  357 .... 225
 */
#define U300_NUM_PADS 467

/* Pad names for the pinmux subsystem */
const struct pinctrl_pin_desc __refdata u300_pads[] = {
	PINCTRL_PIN(0, "P PAD VDD 28"),
	PINCTRL_PIN(1, "P PAD GND 28"),
	PINCTRL_PIN(2, "PO SIM RST N"),
	PINCTRL_PIN(3, "VSSIO 25"),
	PINCTRL_PIN(4, "VSSA ADDA ESDSUB"),
	PINCTRL_PIN(5, "PWR VSSCOMMON"),
	PINCTRL_PIN(6, "PI ADC I1 POS"),
	PINCTRL_PIN(7, "PI ADC I1 NEG"),
	PINCTRL_PIN(8, "PWR VSSAD0"),
	PINCTRL_PIN(9, "PWR VCCAD0"),
	PINCTRL_PIN(10, "PI ADC Q1 NEG"),
	PINCTRL_PIN(11, "PI ADC Q1 POS"),
	PINCTRL_PIN(12, "PWR VDDAD"),
	PINCTRL_PIN(13, "PWR GNDAD"),
	PINCTRL_PIN(14, "PI ADC I2 POS"),
	PINCTRL_PIN(15, "PI ADC I2 NEG"),
	PINCTRL_PIN(16, "PWR VSSAD1"),
	PINCTRL_PIN(17, "PWR VCCAD1"),
	PINCTRL_PIN(18, "PI ADC Q2 NEG"),
	PINCTRL_PIN(19, "PI ADC Q2 POS"),
	PINCTRL_PIN(20, "VSSA ADDA ESDSUB"),
	PINCTRL_PIN(21, "PWR VCCGPAD"),
	PINCTRL_PIN(22, "PI TX POW"),
	PINCTRL_PIN(23, "PWR VSSGPAD"),
	PINCTRL_PIN(24, "PO DAC I POS"),
	PINCTRL_PIN(25, "PO DAC I NEG"),
	PINCTRL_PIN(26, "PO DAC Q POS"),
	PINCTRL_PIN(27, "PO DAC Q NEG"),
	PINCTRL_PIN(28, "PWR VSSDA"),
	PINCTRL_PIN(29, "PWR VCCDA"),
	PINCTRL_PIN(30, "VSSA ADDA ESDSUB"),
	PINCTRL_PIN(31, "P PAD VDDIO 11"),
	PINCTRL_PIN(32, "PI PLL 26 FILTVDD"),
	PINCTRL_PIN(33, "PI PLL 26 VCONT"),
	PINCTRL_PIN(34, "PWR AGNDPLL2V5 32 13"),
	PINCTRL_PIN(35, "PWR AVDDPLL2V5 32 13"),
	PINCTRL_PIN(36, "VDDA PLL ESD"),
	PINCTRL_PIN(37, "VSSA PLL ESD"),
	PINCTRL_PIN(38, "VSS PLL"),
	PINCTRL_PIN(39, "VDDC PLL"),
	PINCTRL_PIN(40, "PWR AGNDPLL2V5 26 60"),
	PINCTRL_PIN(41, "PWR AVDDPLL2V5 26 60"),
	PINCTRL_PIN(42, "PWR AVDDPLL2V5 26 208"),
	PINCTRL_PIN(43, "PWR AGNDPLL2V5 26 208"),
	PINCTRL_PIN(44, "PWR AVDDPLL2V5 13 208"),
	PINCTRL_PIN(45, "PWR AGNDPLL2V5 13 208"),
	PINCTRL_PIN(46, "P PAD VSSIO 11"),
	PINCTRL_PIN(47, "P PAD VSSIO 12"),
	PINCTRL_PIN(48, "PI POW RST N"),
	PINCTRL_PIN(49, "VDDC IO"),
	PINCTRL_PIN(50, "P PAD VDDIO 16"),
	PINCTRL_PIN(134, "UART0 RTS"),
	PINCTRL_PIN(135, "UART0 CTS"),
	PINCTRL_PIN(136, "UART0 TX"),
	PINCTRL_PIN(137, "UART0 RX"),
	PINCTRL_PIN(166, "MMC DATA DIR LS"),
	PINCTRL_PIN(167, "MMC DATA 3"),
	PINCTRL_PIN(168, "MMC DATA 2"),
	PINCTRL_PIN(169, "MMC DATA 1"),
	PINCTRL_PIN(170, "MMC DATA 0"),
	PINCTRL_PIN(171, "MMC CMD DIR LS"),
	PINCTRL_PIN(176, "MMC CMD"),
	PINCTRL_PIN(177, "MMC CLK"),
	PINCTRL_PIN(420, "SPI CLK"),
	PINCTRL_PIN(421, "SPI DO"),
	PINCTRL_PIN(422, "SPI DI"),
	PINCTRL_PIN(423, "SPI CS0"),
	PINCTRL_PIN(424, "SPI CS1"),
	PINCTRL_PIN(425, "SPI CS2"),
};

/**
 * @dev: a pointer back to containing device
 * @virtbase: the offset to the controller in virtual memory
 */
struct u300_pmx {
	struct device *dev;
	struct pinmux_dev *pmx;
	u32 phybase;
	u32 physize;
	void __iomem *virtbase;
};

/**
 * u300_pmx_registers - the array of registers read/written for each pinmux
 * shunt setting
 */
const u32 u300_pmx_registers[] = {
	U300_SYSCON_PMC1LR,
	U300_SYSCON_PMC1HR,
	U300_SYSCON_PMC2R,
	U300_SYSCON_PMC3R,
	U300_SYSCON_PMC4R,
};

/**
 * struct pmx_onmask - mask bits to enable/disable padmux
 * @mask: mask bits to disable
 * @val: mask bits to enable
 *
 * onmask lazy dog:
 * onmask = {
 *   {"PMC1LR" mask, "PMC1LR" value},
 *   {"PMC1HR" mask, "PMC1HR" value},
 *   {"PMC2R"  mask, "PMC2R"  value},
 *   {"PMC3R"  mask, "PMC3R"  value},
 *   {"PMC4R"  mask, "PMC4R"  value}
 * }
 */
struct u300_pmx_mask {
	u16 mask;
	u16 bits;
};

/**
 * struct u300_pmx_func - describes a U300 pinmux function
 * @name: the name of this specific function
 * @pins: an array of discrete physical pins used in this mapping, taken
 *	from the global pin enumeration space
 * @num_pins: the number of pins in this mapping array, i.e. the number of
 *	elements in .pins so we can iterate over that array
 * @onmask: bits to set to enable this muxing
 */
struct u300_pmx_func {
	const char *name;
	const unsigned int *pins;
	const unsigned num_pins;
	const struct u300_pmx_mask *mask;
};

static const unsigned uart0_pins[] = { 134, 135, 136, 137 };
static const unsigned mmc0_pins[] = { 166, 167, 168, 169, 170, 171, 176, 177 };
static const unsigned spi0_pins[] = { 420, 421, 422, 423, 424, 425 };

static const struct u300_pmx_mask uart0_mask[] = {
	{0, 0},
	{
		U300_SYSCON_PMC1HR_APP_UART0_1_MASK |
		U300_SYSCON_PMC1HR_APP_UART0_2_MASK,
		U300_SYSCON_PMC1HR_APP_UART0_1_UART0 |
		U300_SYSCON_PMC1HR_APP_UART0_2_UART0
	},
	{0, 0},
	{0, 0},
	{0, 0},
};

static const struct u300_pmx_mask mmc0_mask[] = {
	{ U300_SYSCON_PMC1LR_MMCSD_MASK, U300_SYSCON_PMC1LR_MMCSD_MMCSD},
	{0, 0},
	{0, 0},
	{0, 0},
	{ U300_SYSCON_PMC4R_APP_MISC_12_MASK,
	  U300_SYSCON_PMC4R_APP_MISC_12_APP_GPIO }
};

static const struct u300_pmx_mask spi0_mask[] = {
	{0, 0},
	{
		U300_SYSCON_PMC1HR_APP_SPI_2_MASK |
		U300_SYSCON_PMC1HR_APP_SPI_CS_1_MASK |
		U300_SYSCON_PMC1HR_APP_SPI_CS_2_MASK,
		U300_SYSCON_PMC1HR_APP_SPI_2_SPI |
		U300_SYSCON_PMC1HR_APP_SPI_CS_1_SPI |
		U300_SYSCON_PMC1HR_APP_SPI_CS_2_SPI
	},
	{0, 0},
	{0, 0},
	{0, 0}
};

static const struct u300_pmx_func u300_pmx_funcs[] = {
	{
		.name = "uart0",
		.pins = uart0_pins,
		.num_pins = ARRAY_SIZE(uart0_pins),
		.mask = uart0_mask,
	},
	{
		.name = "mmc0",
		.pins = mmc0_pins,
		.num_pins = ARRAY_SIZE(mmc0_pins),
		.mask = mmc0_mask,
	},
	{
		.name = "spi0",
		.pins = spi0_pins,
		.num_pins = ARRAY_SIZE(spi0_pins),
		.mask = spi0_mask,
	},
};

static void u300_pmx_endisable(struct u300_pmx *upmx, unsigned selector,
			       bool enable)
{
	u16 regval, val, mask;
	int i;

	for (i = 0; i < ARRAY_SIZE(u300_pmx_registers); i++) {
		if (enable)
			val = u300_pmx_funcs[selector].mask->bits;
		else
			val = 0;

		mask = u300_pmx_funcs[selector].mask->mask;
		if (mask != 0) {
			regval = readw(upmx->virtbase + u300_pmx_registers[i]);
			regval &= ~mask;
			regval |= val;
			writew(regval, upmx->virtbase + u300_pmx_registers[i]);
		}
	}
}

static int u300_pmx_enable(struct pinmux_dev *pmxdev, unsigned selector)
{
	struct u300_pmx *upmx;

	if (selector >= ARRAY_SIZE(u300_pmx_funcs))
		return -EINVAL;
	upmx = pmxdev_get_drvdata(pmxdev);
	u300_pmx_endisable(upmx, selector, true);

	return 0;
}

static void u300_pmx_disable(struct pinmux_dev *pmxdev, unsigned selector)
{
	struct u300_pmx *upmx;

	if (selector >= ARRAY_SIZE(u300_pmx_funcs))
		return;
	upmx = pmxdev_get_drvdata(pmxdev);
	u300_pmx_endisable(upmx, selector, false);
}

static int u300_pmx_list(struct pinmux_dev *pmxdev, unsigned selector)
{
	if (selector >= ARRAY_SIZE(u300_pmx_funcs))
		return -EINVAL;
	return 0;
}

static const char *u300_pmx_get_fname(struct pinmux_dev *pmxdev,
				      unsigned selector)
{
	if (selector >= ARRAY_SIZE(u300_pmx_funcs))
		return NULL;
	return u300_pmx_funcs[selector].name;
}

static int u300_pmx_get_pins(struct pinmux_dev *pmxdev, unsigned selector,
			     unsigned ** const pins, unsigned * const num_pins)
{
	if (selector >= ARRAY_SIZE(u300_pmx_funcs))
		return -EINVAL;
	*pins = (unsigned *) u300_pmx_funcs[selector].pins;
	*num_pins = u300_pmx_funcs[selector].num_pins;
	return 0;
}

static void u300_dbg_show(struct pinmux_dev *pmxdev, struct seq_file *s,
		   unsigned offset)
{
	seq_printf(s, " " DRIVER_NAME);
}

static struct pinmux_ops u300_pmx_ops = {
	.list_functions = u300_pmx_list,
	.get_function_name = u300_pmx_get_fname,
	.get_function_pins = u300_pmx_get_pins,
	.enable = u300_pmx_enable,
	.disable = u300_pmx_disable,
	.dbg_show = u300_dbg_show,
};

static struct pinmux_desc u300_pmx_desc = {
	.name = DRIVER_NAME,
	.pins = u300_pads,
	.npins = ARRAY_SIZE(u300_pads),
	.maxpin = U300_NUM_PADS-1,
	.pmxops = &u300_pmx_ops,
	.owner = THIS_MODULE,
};

static int __init u300_pmx_probe(struct platform_device *pdev)
{
	int ret;
	struct u300_pmx *upmx;
	struct resource *res;

	/* Create state holders etc for this driver */
	upmx = kzalloc(sizeof(struct u300_pmx), GFP_KERNEL);
	if (!upmx)
		return -ENOMEM;

	upmx->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -ENOENT;
		goto out_no_resource;
	}
	upmx->phybase = res->start;
	upmx->physize = resource_size(res);

	if (request_mem_region(upmx->phybase, upmx->physize,
			       DRIVER_NAME) == NULL) {
		ret = -EBUSY;
		goto out_no_memregion;
	}

	upmx->virtbase = ioremap(upmx->phybase, upmx->physize);
	if (!upmx->virtbase) {
		ret = -ENOMEM;
		goto out_no_remap;
	}

	/* Now register the pin controller and all pins it handles */
	upmx->pctl = pinctrl_register(&u300_pmx_desc, &pdev->dev, upmx);
	if (IS_ERR(upmx->pctl)) {
		dev_err(&pdev->dev, "could not register U300 pinmux driver\n");
		ret = PTR_ERR(upmx->pmx);
		goto out_no_pmx;
	}
	platform_set_drvdata(pdev, upmx);

	dev_info(&pdev->dev, "initialized U300 pinmux driver\n");

	return 0;

out_no_pmx:
	iounmap(upmx->virtbase);
out_no_remap:
	platform_set_drvdata(pdev, NULL);
out_no_memregion:
	release_mem_region(upmx->phybase, upmx->physize);
out_no_resource:
	kfree(upmx);
	return ret;
}

static int __exit u300_pmx_remove(struct platform_device *pdev)
{
	struct u300_pmx *upmx = platform_get_drvdata(pdev);

	if (upmx) {
		pinmux_unregister(upmx->pmx);
		iounmap(upmx->virtbase);
		release_mem_region(upmx->phybase, upmx->physize);
		platform_set_drvdata(pdev, NULL);
		kfree(upmx);
	}

	return 0;
}

static struct platform_driver u300_pmx_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
	},
	.remove = __exit_p(u300_pmx_remove),
};

static int __init u300_pmx_init(void)
{
	return platform_driver_probe(&u300_pmx_driver, u300_pmx_probe);
}
arch_initcall(u300_pmx_init);

static void __exit u300_pmx_exit(void)
{
	platform_driver_unregister(&u300_pmx_driver);
}
module_exit(u300_pmx_exit);

MODULE_AUTHOR("Linus Walleij <linus.walleij@linaro.org>");
MODULE_DESCRIPTION("U300 pin control driver");
MODULE_LICENSE("GPL v2");
