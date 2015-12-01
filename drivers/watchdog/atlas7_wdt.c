/*
 * Watchdog driver for CSR Atlas7
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

#include <linux/module.h>
#include <linux/watchdog.h>
#include <linux/platform_device.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/clk.h>

#define ATLAS7_TIMER_WDT_INDEX		5
#define ATLAS7_WDT_MIN_TIMEOUT		10		/* 20 secs */
#define ATLAS7_WDT_MAX_TIMEOUT		28	/* 28 secs for 150Mhz */
#define ATLAS7_WDT_DEFAULT_TIMEOUT	20		/* 20 secs */

#define ATLAS7_WDT_CNT_CTRL	0
#define ATLAS7_WDT_CNT_MATCH	0x18
#define ATLAS7_WDT_CNT	0x48
#define ATLAS7_WDT_EN	0x64

static unsigned int timeout = ATLAS7_WDT_DEFAULT_TIMEOUT;
static bool nowayout = WATCHDOG_NOWAYOUT;

module_param(timeout, uint, 0);
module_param(nowayout, bool, 0);

MODULE_PARM_DESC(timeout, "Default watchdog timeout (in seconds)");
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
			__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

struct atlas7_wdog {
	struct device *dev;
	void __iomem *base;
	unsigned long tick_rate;
	struct clk *clk;
};

static unsigned int atlas7_wdt_gettimeleft(struct watchdog_device *wdd)
{
	struct atlas7_wdog *wdt = watchdog_get_drvdata(wdd);
	u32 counter, match;
	unsigned int time_left;

	counter = readl(wdt->base + ATLAS7_WDT_CNT +
		4 * ATLAS7_TIMER_WDT_INDEX);
	match = readl(wdt->base + ATLAS7_WDT_CNT_MATCH +
			4 * ATLAS7_TIMER_WDT_INDEX);
	time_left = match - counter;

	return  time_left / wdt->tick_rate;
}

static int atlas7_wdt_ping(struct watchdog_device *wdd)
{
	struct atlas7_wdog *wdt = watchdog_get_drvdata(wdd);
	u32 timeout_ticks;

	timeout_ticks = wdd->timeout * wdt->tick_rate;

	writel(readl(wdt->base + ATLAS7_WDT_CNT +
			4 * ATLAS7_TIMER_WDT_INDEX) +
			timeout_ticks,
			wdt->base + ATLAS7_WDT_CNT_MATCH +
				4 * ATLAS7_TIMER_WDT_INDEX);

	return 0;
}

static int atlas7_wdt_enable(struct watchdog_device *wdd)
{
	struct atlas7_wdog *wdt = watchdog_get_drvdata(wdd);

	atlas7_wdt_ping(wdd);
	writel(readl(wdt->base + ATLAS7_WDT_CNT_CTRL +
			4 * ATLAS7_TIMER_WDT_INDEX) | 0x3,
			wdt->base + ATLAS7_WDT_CNT_CTRL +
			4 * ATLAS7_TIMER_WDT_INDEX);
	writel(1, wdt->base + ATLAS7_WDT_EN);

	return 0;
}

static int atlas7_wdt_disable(struct watchdog_device *wdd)
{
	struct atlas7_wdog *wdt = watchdog_get_drvdata(wdd);

	writel(0, wdt->base + ATLAS7_WDT_EN);
	writel(readl(wdt->base + ATLAS7_WDT_CNT_CTRL +
			4 * ATLAS7_TIMER_WDT_INDEX) &  ~0x3,
			wdt->base + ATLAS7_WDT_CNT_CTRL +
			4 * ATLAS7_TIMER_WDT_INDEX);

	return 0;
}

static int atlas7_wdt_settimeout(struct watchdog_device *wdd, unsigned int to)
{
	wdd->timeout = to;
	atlas7_wdt_ping(wdd);

	return 0;
}

#define OPTIONS (WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE)

static const struct watchdog_info atlas7_wdt_ident = {
	.options          =     OPTIONS,
	.firmware_version =	0,
	.identity         =	"atlas7 Watchdog",
};

static struct watchdog_ops atlas7_wdt_ops = {
	.owner = THIS_MODULE,
	.start = atlas7_wdt_enable,
	.stop = atlas7_wdt_disable,
	.get_timeleft = atlas7_wdt_gettimeleft,
	.ping = atlas7_wdt_ping,
	.set_timeout = atlas7_wdt_settimeout,
};

static struct watchdog_device atlas7_wdd = {
	.info = &atlas7_wdt_ident,
	.ops = &atlas7_wdt_ops,
	.timeout = ATLAS7_WDT_DEFAULT_TIMEOUT,
	.min_timeout = ATLAS7_WDT_MIN_TIMEOUT,
	.max_timeout = ATLAS7_WDT_MAX_TIMEOUT,
};

static const struct of_device_id atlas7_wdt_ids[] = {
	{ .compatible = "sirf,atlas7-tick"},
	{}
};

static int atlas7_wdt_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct atlas7_wdog *wdt;
	struct resource *res;
	struct clk *clk;
	int ret;

	wdt = devm_kzalloc(&pdev->dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	wdt->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(wdt->base))
		return PTR_ERR(wdt->base);

	clk = of_clk_get(np, 0);
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		goto err;
	}
	ret = clk_prepare_enable(clk);
	if (ret) {
		pr_err("wdt clk enable failed\n");
		goto err;
	}
	writel(0, wdt->base + ATLAS7_WDT_CNT_CTRL +
			4 * ATLAS7_TIMER_WDT_INDEX);
	wdt->tick_rate = clk_get_rate(clk);
	wdt->clk = clk;

	watchdog_init_timeout(&atlas7_wdd, timeout, &pdev->dev);
	watchdog_set_nowayout(&atlas7_wdd, nowayout);
	ret = watchdog_register_device(&atlas7_wdd);
	if (ret)
		goto err1;

	watchdog_set_drvdata(&atlas7_wdd, wdt);
	platform_set_drvdata(pdev, &atlas7_wdd);

	return 0;

err1:
	clk_disable_unprepare(wdt->clk);
err:
	clk_put(wdt->clk);
	return ret;
}

static void atlas7_wdt_shutdown(struct platform_device *pdev)
{
	struct watchdog_device *wdd = platform_get_drvdata(pdev);
	struct atlas7_wdog *wdt = watchdog_get_drvdata(wdd);

	atlas7_wdt_disable(wdd);
	clk_disable_unprepare(wdt->clk);
}

static int atlas7_wdt_remove(struct platform_device *pdev)
{
	struct watchdog_device *wdd = platform_get_drvdata(pdev);
	struct atlas7_wdog *wdt = watchdog_get_drvdata(wdd);

	atlas7_wdt_shutdown(pdev);
	clk_put(wdt->clk);
	return 0;
}

#ifdef	CONFIG_PM_SLEEP
static int atlas7_wdt_suspend(struct device *dev)
{
	return 0;
}

static int atlas7_wdt_resume(struct device *dev)
{
	struct watchdog_device *wdd = dev_get_drvdata(dev);

	/*
	 * NOTE: Since timer controller registers settings are saved
	 * and restored back by the timer-atlas7.c, so we need not
	 * update WD settings except refreshing timeout.
	 */
	atlas7_wdt_ping(wdd);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(atlas7_wdt_pm_ops,
		atlas7_wdt_suspend, atlas7_wdt_resume);

MODULE_DEVICE_TABLE(of, atlas7_wdt_ids);

static struct platform_driver atlas7_wdt_driver = {
	.driver = {
		.name = "atlas7-wdt",
		.pm = &atlas7_wdt_pm_ops,
		.of_match_table	= atlas7_wdt_ids,
	},
	.probe = atlas7_wdt_probe,
	.remove = atlas7_wdt_remove,
	.shutdown = atlas7_wdt_shutdown,
};
module_platform_driver(atlas7_wdt_driver);

MODULE_DESCRIPTION("CSRatlas7 watchdog driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:atlas7-wdt");
