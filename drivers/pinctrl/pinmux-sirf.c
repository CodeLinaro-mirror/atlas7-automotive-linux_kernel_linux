/*
 * pinmux driver for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <mach/regs-gpio.h>

#include "pinmux-sirf.h"

#define DRIVER_NAME "pinmux-sirf"

#define SIRFSOC_NUM_PADS    622

#define SIRFSOC_RSC_PIN_MUX 0x4

/*
 * pad list for the pinmux subsystem
 * refer to CS-131858-DC-6A.xls
 */
const struct pinctrl_pin_desc __refdata sirfsoc_pads[] = {
	PINCTRL_PIN(242, "LDD[23], lcdrom_frdy"),
	PINCTRL_PIN(245, "L_PCLK"),
	PINCTRL_PIN(248, "L_LCK"),
	PINCTRL_PIN(249, "L_FCK"),
	PINCTRL_PIN(250, "L_DE"),
	PINCTRL_PIN(251, "LDD[0]"),
	PINCTRL_PIN(252, "LDD[1]"),
	PINCTRL_PIN(255, "LDD[2]"),
	PINCTRL_PIN(256, "LDD[3]"),
	PINCTRL_PIN(257, "LDD[4]"),
	PINCTRL_PIN(258, "LDD[5]"),
	PINCTRL_PIN(261, "LDD[6]"),
	PINCTRL_PIN(262, "LDD[7]"),
	PINCTRL_PIN(263, "LDD[8]"),
	PINCTRL_PIN(266, "LDD[9]"),
	PINCTRL_PIN(267, "LDD[10]"),
	PINCTRL_PIN(270, "LDD[11]"),
	PINCTRL_PIN(273, "LDD[12]"),
	PINCTRL_PIN(274, "LDD[13]"),
	PINCTRL_PIN(277, "LDD[14]"),
	PINCTRL_PIN(278, "LDD[15]"),
};

/**
 * @dev: a pointer back to containing device
 * @virtbase: the offset to the controller in virtual memory
 */
struct sirfsoc_pmx {
	struct device *dev;
	struct pinctrl_dev *pmx;
	void __iomem *virtbase;
};

struct sirfsoc_muxmask {
	unsigned long group;
	unsigned long mask;
};

struct sirfsoc_padmux {
	unsigned long muxmask_counts;
	struct sirfsoc_muxmask *muxmask;
	unsigned long funcmask;
	unsigned long funcval;
};

/**
 * struct sirfsoc_pinmux_func - describes a SIRFSOC pinmux function
 * @name: the name of this specific function
 * @pins: an array of discrete physical pins used in this mapping, taken
 *	from the global pin enumeration space
 * @num_pins: the number of pins in this mapping array, i.e. the number of
 *	elements in .pins so we can iterate over that array
 * @padmux: registers set for required pad mux
 */
struct sirfsoc_pinmux_func {
	const char *name;
	const unsigned int *pins;
	const unsigned num_pins;
	const struct sirfsoc_padmux *padmux;
};

static struct sirfsoc_muxmask lcd_16bits_sirfsoc_muxmask[] = {
	{
		.group = 3,
		.mask =
			~((1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) |
				(1 << 6) | (1 << 7)
				| (1 << 8) | (1 << 9) | (1 << 10) | (1 << 11) | (1 << 12) | (1 << 13)
				| (1 << 14) | (1 << 15)
				| (1 << 16) | (1 << 17) | (1 << 18)),
	}, {
		.group = 2,
		.mask = ~(1 << 31),
	},
};

static struct sirfsoc_padmux lcd_16bits_padmux = {
	.muxmask_counts = ARRAY_SIZE(lcd_16bits_sirfsoc_muxmask),
	.muxmask = lcd_16bits_sirfsoc_muxmask,
	.funcmask = (1 << 4),
	.funcval = (0 << 4),
};

static const unsigned lcd_16bits_pins[] = { 245, 248, 249, 250, 251, 252, 255,
	256, 257, 258, 261, 262, 263, 266, 267, 270, 273, 274, 277, 278 };

static struct sirfsoc_muxmask lcdrom_muxmask[] = {
	{
		.group = 3,
		.mask =
			~((1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) |
				(1 << 6) | (1 << 7)
				| (1 << 8) | (1 << 9) | (1 << 10) | (1 << 11) | (1 << 12) | (1 << 13)
				| (1 << 14) | (1 << 15)
				| (1 << 16) | (1 << 17) | (1 << 18)),
	}, {
		.group = 2,
		.mask = ~(1 << 31),
	}, {
		.group = 0,
		.mask = ~((1 << 23)),
	},
};

static struct sirfsoc_padmux lcdrom_padmux = {
	.muxmask_counts = ARRAY_SIZE(lcdrom_muxmask),
	.muxmask = lcdrom_muxmask,
	.funcmask = (1 << 4),
	.funcval = (1 << 4),
};

static const unsigned lcdrom_pins[] = { 242, 245, 248, 249, 250, 251, 252, 255,
	256, 257, 258, 261, 262, 263, 266, 267, 270, 273, 274, 277, 278 };

static const struct sirfsoc_pinmux_func sirfsoc_pinmux_funcs[] = {
	{
		.name = "lcd_16bits_pins",
		.pins = lcd_16bits_pins,
		.num_pins = ARRAY_SIZE(lcd_16bits_pins),
		.padmux = &lcd_16bits_padmux,
	}, {
		.name = "lcdrom_pins",
		.pins = lcdrom_pins,
		.num_pins = ARRAY_SIZE(lcdrom_pins),
		.padmux = &lcdrom_padmux,
	},
};

static void sirfsoc_pinmux_endisable(struct sirfsoc_pmx *upmx, unsigned selector,
	bool enable)
{
	int i;
	const struct sirfsoc_padmux *mux = sirfsoc_pinmux_funcs[selector].padmux;
	const struct sirfsoc_muxmask *mask = mux->muxmask;

	for (i = 0; i < mux->muxmask_counts; i++) {
		u32 muxval;
		muxval = readl(upmx->virtbase + SIRFSOC_GPIO_PAD_EN(mask[i].group));
		if (enable)
			muxval = muxval & mask[i].mask;
		else
			muxval = muxval | ~mask[i].mask;
		writel(muxval, upmx->virtbase + SIRFSOC_GPIO_PAD_EN(mask[i].group));
	}

	if (mux->funcmask && enable) {
		u32 func_en_val;
		func_en_val =
			readl(upmx->virtbase + SIRFSOC_RSC_PIN_MUX);
		func_en_val =
			(func_en_val & (~(mux->funcmask))) | (mux->
				funcval);
		writel(func_en_val, upmx->virtbase + SIRFSOC_RSC_PIN_MUX);
	}
}

static int sirfsoc_pinmux_enable(struct pinctrl_dev *pmxdev, unsigned selector)
{
	struct sirfsoc_pmx *upmx;

	if (selector >= ARRAY_SIZE(sirfsoc_pinmux_funcs))
		return -EINVAL;
	upmx = pctldev_get_drvdata(pmxdev);
	sirfsoc_pinmux_endisable(upmx, selector, true);

	return 0;
}

static void sirfsoc_pinmux_disable(struct pinctrl_dev *pmxdev, unsigned selector)
{
	struct sirfsoc_pmx *upmx;

	if (selector >= ARRAY_SIZE(sirfsoc_pinmux_funcs))
		return;
	upmx = pctldev_get_drvdata(pmxdev);
	sirfsoc_pinmux_endisable(upmx, selector, false);
}

static int sirfsoc_pinmux_list(struct pinctrl_dev *pmxdev, unsigned selector)
{
	if (selector >= ARRAY_SIZE(sirfsoc_pinmux_funcs))
		return -EINVAL;
	return 0;
}

static const char *sirfsoc_pinmux_get_fname(struct pinctrl_dev *pmxdev,
	unsigned selector)
{
	if (selector >= ARRAY_SIZE(sirfsoc_pinmux_funcs))
		return NULL;
	return sirfsoc_pinmux_funcs[selector].name;
}

static int sirfsoc_pinmux_get_pins(struct pinctrl_dev *pmxdev, unsigned selector,
	unsigned ** const pins, unsigned * const num_pins)
{
	if (selector >= ARRAY_SIZE(sirfsoc_pinmux_funcs))
		return -EINVAL;
	*pins = (unsigned *) sirfsoc_pinmux_funcs[selector].pins;
	*num_pins = sirfsoc_pinmux_funcs[selector].num_pins;
	return 0;
}

static void sirfsoc_dbg_show(struct pinctrl_dev *pmxdev, struct seq_file *s,
	unsigned offset)
{
	seq_printf(s, " " DRIVER_NAME);
}

static struct pinmux_ops sirfsoc_pinmux_ops = {
	.list_functions = sirfsoc_pinmux_list,
	.get_function_name = sirfsoc_pinmux_get_fname,
	.get_function_pins = sirfsoc_pinmux_get_pins,
	.enable = sirfsoc_pinmux_enable,
	.disable = sirfsoc_pinmux_disable,
	.dbg_show = sirfsoc_dbg_show,
};

static struct pinctrl_desc sirfsoc_pinmux_desc = {
	.name = DRIVER_NAME,
	.pins = sirfsoc_pads,
	.npins = ARRAY_SIZE(sirfsoc_pads),
	.maxpin = SIRFSOC_NUM_PADS - 1,
	.pmxops = &sirfsoc_pinmux_ops,
	.owner = THIS_MODULE,
};

static int __devinit sirfsoc_pinmux_probe(struct platform_device *pdev)
{
	int ret;
	struct sirfsoc_pmx *upmx;
	struct device_node *np = pdev->dev.of_node;

	/* Create state holders etc for this driver */
	upmx = kzalloc(sizeof(struct sirfsoc_pmx), GFP_KERNEL);
	if (!upmx)
		return -ENOMEM;

	upmx->dev = &pdev->dev;

	platform_set_drvdata(pdev, upmx);

	upmx->virtbase = of_iomap(np, 0);
	if (!upmx->virtbase) {
		ret = -ENOMEM;
		goto out_no_remap;
	}

	/* Now register the pin controller and all pins it handles */
	upmx->pmx = pinctrl_register(&sirfsoc_pinmux_desc, &pdev->dev, upmx);
	if (IS_ERR(upmx->pmx)) {
		dev_err(&pdev->dev, "could not register SIRFSOC pinmux driver\n");
		ret = PTR_ERR(upmx->pmx);
		goto out_no_pmx;
	}

	dev_info(&pdev->dev, "initialized SIRFSOC pinmux driver\n");

	return 0;

out_no_pmx:
	iounmap(upmx->virtbase);
out_no_remap:
	platform_set_drvdata(pdev, NULL);
	kfree(upmx);
	return ret;
}

static int __devexit sirfsoc_pinmux_remove(struct platform_device *pdev)
{
	struct sirfsoc_pmx *upmx = platform_get_drvdata(pdev);

	pinctrl_unregister(upmx->pmx);
	iounmap(upmx->virtbase);
	platform_set_drvdata(pdev, NULL);
	kfree(upmx);

	return 0;
}

static const struct of_device_id pinmux_ids[]  = {
	{ .compatible = "sirf,prima2-pinmux" },
	{}
};

static struct platform_driver sirfsoc_pinmux_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = pinmux_ids,
	},
	.remove = __devexit_p(sirfsoc_pinmux_remove),
};

static int __init sirfsoc_pinmux_init(void)
{
	return platform_driver_probe(&sirfsoc_pinmux_driver, sirfsoc_pinmux_probe);
}
arch_initcall(sirfsoc_pinmux_init);

static void __exit sirfsoc_pinmux_exit(void)
{
	platform_driver_unregister(&sirfsoc_pinmux_driver);
}
module_exit(sirfsoc_pinmux_exit);

MODULE_AUTHOR("Rongjun Ying <rongjun.ying@csr.com>, "
	"Barry Song <baohua.song@csr.com>");
MODULE_DESCRIPTION("SIRFSOC pin control driver");
MODULE_LICENSE("GPL");
