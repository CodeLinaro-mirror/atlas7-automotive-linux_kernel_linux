/*
 * RTC I/O Bridge interfaces for CSR SiRFprimaII/atlas7
 *
 * Copyright (c) 2011, 2013-2016, The Linux Foundation. All rights reserved.
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/regmap.h>
#include <linux/hwspinlock.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/rtc/sirfsoc_rtciobrg.h>

#define SIRFSOC_CPUIOBRG_CTRL           0x00
#define SIRFSOC_CPUIOBRG_WRBE           0x04
#define SIRFSOC_CPUIOBRG_ADDR           0x08
#define SIRFSOC_CPUIOBRG_DATA           0x0c

/*
 * suspend asm codes will access this address to make system deepsleep
 * after DRAM becomes self-refresh
 */
void __iomem *sirfsoc_rtciobrg_base;
struct hwspinlock *rtciobrg_hwlock;

/*
 * symbols without lock are only used by suspend asm codes
 * and these symbols are not exported too
 */
void sirfsoc_rtc_iobrg_wait_sync(void)
{
	while (readl_relaxed(sirfsoc_rtciobrg_base + SIRFSOC_CPUIOBRG_CTRL))
		cpu_relax();
}

void sirfsoc_rtc_iobrg_besyncing(void)
{
	unsigned long flags;
	int err;

	err = hwspin_lock_timeout_irqsave(rtciobrg_hwlock, 100, &flags);
	if (err) {
		pr_err("%s, IOBridge get hwspinlock failed!err= %d\n",
			__func__, err);
		BUG_ON(err);
	}

	sirfsoc_rtc_iobrg_wait_sync();

	hwspin_unlock_irqrestore(rtciobrg_hwlock, &flags);
}
EXPORT_SYMBOL_GPL(sirfsoc_rtc_iobrg_besyncing);

u32 __sirfsoc_rtc_iobrg_readl(u32 addr)
{
	sirfsoc_rtc_iobrg_wait_sync();

	writel_relaxed(0x00, sirfsoc_rtciobrg_base + SIRFSOC_CPUIOBRG_WRBE);
	writel_relaxed(addr, sirfsoc_rtciobrg_base + SIRFSOC_CPUIOBRG_ADDR);
	writel_relaxed(0x01, sirfsoc_rtciobrg_base + SIRFSOC_CPUIOBRG_CTRL);

	sirfsoc_rtc_iobrg_wait_sync();

	return readl_relaxed(sirfsoc_rtciobrg_base + SIRFSOC_CPUIOBRG_DATA);
}


u32 sirfsoc_rtc_iobrg_readl(u32 addr)
{
#ifdef CONFIG_NOC_LOCK_RTCM
	return restricted_reg_read(0x18840000 + addr);
#else
	unsigned long flags, val;
	int err;

	err = hwspin_lock_timeout_irqsave(rtciobrg_hwlock, 100, &flags);
	if (err) {
		pr_err("%s, IOBridge get hwspinlock failed!err= %d\n",
			__func__, err);
		BUG_ON(err);
	}

	val = __sirfsoc_rtc_iobrg_readl(addr);

	hwspin_unlock_irqrestore(rtciobrg_hwlock, &flags);

	return val;
#endif
}
EXPORT_SYMBOL_GPL(sirfsoc_rtc_iobrg_readl);

void sirfsoc_rtc_iobrg_pre_writel(u32 val, u32 addr)
{
	sirfsoc_rtc_iobrg_wait_sync();

	writel_relaxed(0xf1, sirfsoc_rtciobrg_base + SIRFSOC_CPUIOBRG_WRBE);
	writel_relaxed(addr, sirfsoc_rtciobrg_base + SIRFSOC_CPUIOBRG_ADDR);

	writel_relaxed(val, sirfsoc_rtciobrg_base + SIRFSOC_CPUIOBRG_DATA);
}

void sirfsoc_rtc_iobrg_writel(u32 val, u32 addr)
{
#ifdef CONFIG_NOC_LOCK_RTCM
	restricted_reg_write(0x18840000 + addr, val);
#else
	unsigned long flags;
	int err;

	err = hwspin_lock_timeout_irqsave(rtciobrg_hwlock, 100, &flags);
	if (err) {
		pr_err("%s, IOBridge get hwspinlock failed!err= %d\n",
			__func__, err);
		BUG_ON(err);
	}

	sirfsoc_rtc_iobrg_pre_writel(val, addr);

	writel_relaxed(0x01, sirfsoc_rtciobrg_base + SIRFSOC_CPUIOBRG_CTRL);

	sirfsoc_rtc_iobrg_wait_sync();

	hwspin_unlock_irqrestore(rtciobrg_hwlock, &flags);

#endif
}
EXPORT_SYMBOL_GPL(sirfsoc_rtc_iobrg_writel);


static int regmap_iobg_regwrite(void *context, unsigned int reg,
				   unsigned int val)
{
	sirfsoc_rtc_iobrg_writel(val, reg);
	return 0;
}

static int regmap_iobg_regread(void *context, unsigned int reg,
				  unsigned int *val)
{
	*val = (u32)sirfsoc_rtc_iobrg_readl(reg);
	return 0;
}

static struct regmap_bus regmap_iobg = {
	.reg_write = regmap_iobg_regwrite,
	.reg_read = regmap_iobg_regread,
};

/**
 * devm_regmap_init_iobg(): Initialise managed register map
 *
 * @iobg: Device that will be interacted with
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer
 * to a struct regmap.  The regmap will be automatically freed by the
 * device management code.
 */
struct regmap *devm_regmap_init_iobg(struct device *dev,
				    const struct regmap_config *config)
{
	const struct regmap_bus *bus = &regmap_iobg;

	return devm_regmap_init(dev, bus, dev, config);
}
EXPORT_SYMBOL_GPL(devm_regmap_init_iobg);

static const struct of_device_id rtciobrg_ids[] = {
	{ .compatible = "sirf,prima2-rtciobg" },
	{}
};

static int sirfsoc_rtciobrg_probe(struct platform_device *op)
{
	struct device_node *np = op->dev.of_node;
#ifndef CONFIG_NOC_LOCK_RTCM
	int hwlock_id;
#endif
	sirfsoc_rtciobrg_base = of_iomap(np, 0);
	if (!sirfsoc_rtciobrg_base)
		panic("unable to map rtc iobrg registers\n");
#ifndef CONFIG_NOC_LOCK_RTCM
	/* Request hwlock for rtc io-bridge */
	hwlock_id = of_hwspin_lock_get_id(np, 0);
	if (hwlock_id < 0)
		panic("unable to acquire hwlock for rtc iobrg\n");


	rtciobrg_hwlock = hwspin_lock_request_specific(hwlock_id);
	if (!rtciobrg_hwlock)
		panic("request specific hwlock for rtc iobrg failed!\n");
#endif
	return 0;
}

static struct platform_driver sirfsoc_rtciobrg_driver = {
	.probe		= sirfsoc_rtciobrg_probe,
	.driver = {
		.name = "sirfsoc-rtciobrg",
		.owner = THIS_MODULE,
		.of_match_table	= rtciobrg_ids,
	},
};

static int __init sirfsoc_rtciobrg_init(void)
{
	return platform_driver_register(&sirfsoc_rtciobrg_driver);
}
subsys_initcall(sirfsoc_rtciobrg_init);

MODULE_DESCRIPTION("CSR SiRFprimaII rtc io bridge");
MODULE_LICENSE("GPL v2");
