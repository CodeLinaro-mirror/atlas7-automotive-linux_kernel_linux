/*
 * Watchdog driver for CSR SiRFprimaII and SiRFatlasVI
 *
 * Copyright (c) 2013 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/io.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/watchdog.h>
#include <linux/of_platform.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>

struct sirfsoc_timer_hw {
	/* hardware timer specific*/
	u32 match;
	u32 irq_status;
	u32 watchdog_en;
	u32 cnt64_lo;
	u32 cnt64_hi;
	u32 cnt64_latch_lo;
	u32 cnt64_latch_hi;

	/* only atlas7 has*/
	u32 match_num;
	u32 cnt_ctrl;
	u32 cnt;
	u32 cnt64_load_lo;
	u32 cnt64_load_hi;
	u32 cnt64_ctrl;

	/* only prima2 has*/
	u32 irq_enable;
	u32 div;
	u32 latch_eable;

	int (*wdt_enable)(struct watchdog_device *);
	int (*wdt_disable)(struct watchdog_device *);
	void (*wdt_latch)(struct watchdog_device *);

};

#define SIRFSOC_CNT64_CTRL_LOAD_BIT		BIT(1)
#define SIRFSOC_CNT64_CTRL_LATCH_BIT		BIT(0)

#define SIRFSOC_TIMER_WDT_INDEX		5
#define SIRFSOC_WDT_MIN_TIMEOUT		30		/* 30 secs */
#define SIRFSOC_WDT_MAX_TIMEOUT		(10 * 60)	/* 10 mins */
#define SIRFSOC_WDT_DEFAULT_TIMEOUT	30		/* 30 secs */

static unsigned int timeout = SIRFSOC_WDT_DEFAULT_TIMEOUT;
static bool nowayout = WATCHDOG_NOWAYOUT;

module_param(timeout, uint, 0);
module_param(nowayout, bool, 0);

MODULE_PARM_DESC(timeout, "Default watchdog timeout (in seconds)");
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
			__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

struct sirfsoc_wdog {
	struct device *dev;
	struct sirfsoc_timer_hw *hw;
	void __iomem *base;
	unsigned long tick_rate;
};

static unsigned int sirfsoc_wdt_gettimeleft(struct watchdog_device *wdd)
{
	struct sirfsoc_wdog *wdt = watchdog_get_drvdata(wdd);
	struct sirfsoc_timer_hw *hw = wdt->hw;
	u32 counter, match;
	int time_left;

	counter = readl(wdt->base + hw->cnt64_lo);
	match = readl(wdt->base + hw->match +
			4 * SIRFSOC_TIMER_WDT_INDEX);

	time_left = match - counter;

	return  time_left / wdt->tick_rate;
}

static void atlas7_wdt_latch(struct watchdog_device *wdd)
{
	struct sirfsoc_wdog *wdt = watchdog_get_drvdata(wdd);
	struct sirfsoc_timer_hw *hw = wdt->hw;

	/* Enable the latch before reading the LATCH_LO register */
	writel((readl(wdt->base +
			hw->cnt64_ctrl) |
			SIRFSOC_CNT64_CTRL_LATCH_BIT) &
			~SIRFSOC_CNT64_CTRL_LOAD_BIT,
		wdt->base + hw->cnt64_ctrl);
}

static void prima2_wdt_latch(struct watchdog_device *wdd)
{
	struct sirfsoc_wdog *wdt = watchdog_get_drvdata(wdd);
	struct sirfsoc_timer_hw *hw = wdt->hw;

	/* Enable the latch before reading the LATCH_LO register */
	writel(SIRFSOC_CNT64_CTRL_LATCH_BIT,
		wdt->base + hw->latch_eable);
}

static int prima2_wdt_enable(struct watchdog_device *wdd)
{
	struct sirfsoc_wdog *wdt = watchdog_get_drvdata(wdd);
	struct sirfsoc_timer_hw *hw = wdt->hw;
	/*
	 * NOTE: If interrupt is not enabled
	 * then WD-Reset doesn't get generated at all.
	 */
	writel(readl(wdt->base + hw->irq_enable) |
				(1 << SIRFSOC_TIMER_WDT_INDEX),
			wdt->base + hw->irq_enable);

	writel(1, wdt->base + hw->watchdog_en);
	return 0;
}

static int prima2_wdt_disable(struct watchdog_device *wdd)
{
	struct sirfsoc_wdog *wdt = watchdog_get_drvdata(wdd);
	struct sirfsoc_timer_hw *hw = wdt->hw;

	writel(0, wdt->base + hw->watchdog_en);

	writel(readl(wdt->base + hw->irq_enable) &
		~(1 << SIRFSOC_TIMER_WDT_INDEX),
	wdt->base + hw->irq_enable);

	return 0;
}

static int atlas7_wdt_enable(struct watchdog_device *wdd)
{
	struct sirfsoc_wdog *wdt = watchdog_get_drvdata(wdd);
	struct sirfsoc_timer_hw *hw = wdt->hw;


	/*
	 * NOTE: If interrupt is not enabled
	 * then WD-Reset doesn't get generated at all.
	 */
	writel(readl(wdt->base + hw->cnt_ctrl +
			4 * SIRFSOC_TIMER_WDT_INDEX) | 0x3,
		wdt->base + hw->cnt_ctrl +
			4 * SIRFSOC_TIMER_WDT_INDEX);

	writel(1, wdt->base + hw->watchdog_en);
	return 0;
}

static int atlas7_wdt_disable(struct watchdog_device *wdd)
{
	struct sirfsoc_wdog *wdt = watchdog_get_drvdata(wdd);
	struct sirfsoc_timer_hw *hw = wdt->hw;

	writel(0, wdt->base + hw->watchdog_en);

	writel(readl(wdt->base +
			hw->cnt_ctrl +
			4 * SIRFSOC_TIMER_WDT_INDEX) & ~0x3,
		wdt->base + hw->cnt_ctrl +
			4 * SIRFSOC_TIMER_WDT_INDEX);

	return 0;
}

static int sirfsoc_wdt_updatetimeout(struct watchdog_device *wdd)
{
	struct sirfsoc_wdog *wdt = watchdog_get_drvdata(wdd);
	struct sirfsoc_timer_hw *hw = wdt->hw;
	u32 timeout_ticks;

	timeout_ticks = wdd->timeout * wdt->tick_rate;

	/* Enable the latch before reading the LATCH_LO register */
	hw->wdt_latch(wdd);

	writel(readl(wdt->base + hw->cnt64_latch_lo) +
				timeout_ticks,
			wdt->base + hw->match +
				4 * SIRFSOC_TIMER_WDT_INDEX);

	return 0;
}

static int sirfsoc_wdt_enable(struct watchdog_device *wdd)
{
	struct sirfsoc_wdog *wdt = watchdog_get_drvdata(wdd);
	struct sirfsoc_timer_hw *hw = wdt->hw;

	sirfsoc_wdt_updatetimeout(wdd);
	hw->wdt_enable(wdd);
	return 0;
}

static int sirfsoc_wdt_disable(struct watchdog_device *wdd)
{
	struct sirfsoc_wdog *wdt = watchdog_get_drvdata(wdd);
	struct sirfsoc_timer_hw *hw = wdt->hw;

	hw->wdt_disable(wdd);
	return 0;
}

static int sirfsoc_wdt_settimeout(struct watchdog_device *wdd, unsigned int to)
{
	wdd->timeout = to;
	sirfsoc_wdt_updatetimeout(wdd);

	return 0;
}

#define OPTIONS (WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE)

static const struct watchdog_info sirfsoc_wdt_ident = {
	.options          =     OPTIONS,
	.firmware_version =	0,
	.identity         =	"SiRFSOC Watchdog",
};

static struct watchdog_ops sirfsoc_wdt_ops = {
	.owner = THIS_MODULE,
	.start = sirfsoc_wdt_enable,
	.stop = sirfsoc_wdt_disable,
	.get_timeleft = sirfsoc_wdt_gettimeleft,
	.ping = sirfsoc_wdt_updatetimeout,
	.set_timeout = sirfsoc_wdt_settimeout,
};

static struct watchdog_device sirfsoc_wdd = {
	.info = &sirfsoc_wdt_ident,
	.ops = &sirfsoc_wdt_ops,
	.timeout = SIRFSOC_WDT_DEFAULT_TIMEOUT,
	.min_timeout = SIRFSOC_WDT_MIN_TIMEOUT,
	.max_timeout = SIRFSOC_WDT_MAX_TIMEOUT,
};


struct sirfsoc_timer_hw sirfsoc_timer_atlas7 = {
	.cnt_ctrl = 0x0,
	.match = 0x18,
	.cnt = 0x48,
	.irq_status = 0x60,
	.watchdog_en = 0x64,
	.cnt64_ctrl = 0x68,
	.cnt64_lo = 0x6c,
	.cnt64_hi = 0x70,
	.cnt64_load_lo = 0x74,
	.cnt64_load_hi = 0x78,
	.cnt64_latch_lo = 0x7c,
	.cnt64_latch_hi = 0x80,
	.wdt_enable = atlas7_wdt_enable,
	.wdt_disable = atlas7_wdt_disable,
	.wdt_latch = atlas7_wdt_latch,
};

struct sirfsoc_timer_hw sirfsoc_timer_prima2 = {
	.match = 0x8,
	.irq_status = 0x20,
	.watchdog_en = 0x28,
	.cnt64_lo = 0x0,
	.cnt64_hi = 0x4,
	.cnt64_latch_lo = 0x34,
	.cnt64_latch_hi = 0x38,
	.irq_enable = 0x24,
	.div = 0x2c,
	.latch_eable = 0x30,
	.wdt_enable = prima2_wdt_enable,
	.wdt_disable = prima2_wdt_disable,
	.wdt_latch = prima2_wdt_latch,
};

static const struct of_device_id sirfsoc_wdt_of_match[] = {
	{ .compatible = "sirf,atlas7-tick", .data = &sirfsoc_timer_atlas7},
	{ .compatible = "sirf,prima2-tick", .data = &sirfsoc_timer_prima2},
	{}
};

static int sirfsoc_wdt_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct sirfsoc_wdog *wdt;
	const struct of_device_id *match;
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

	match = of_match_node(sirfsoc_wdt_of_match, np);
	if (!match)
		return -ENODEV;
	wdt->hw = (struct sirfsoc_timer_hw *)match->data;

	/*assign clks div and callbacks*/
	if (of_device_is_compatible(np, "sirf,atlas7-tick")) {
		clk = of_clk_get(np, 0);
		if (IS_ERR(clk)) {
			pr_debug("wdt clk get failed\n");
			goto err;
		}

		ret = clk_prepare_enable(clk);
		if (ret) {
			pr_debug("wdt clk enable failed\n");
			goto err;
		}
		wdt->tick_rate = clk_get_rate(clk);
	} else if (of_device_is_compatible(np, "sirf,prima2-tick"))
		wdt->tick_rate = 1000000;

	watchdog_init_timeout(&sirfsoc_wdd, timeout, &pdev->dev);
	watchdog_set_nowayout(&sirfsoc_wdd, nowayout);
	ret = watchdog_register_device(&sirfsoc_wdd);
	if (ret)
		goto err;

	watchdog_set_drvdata(&sirfsoc_wdd, wdt);
	platform_set_drvdata(pdev, &sirfsoc_wdd);

	return 0;
err:
	return ret;
}

static void sirfsoc_wdt_shutdown(struct platform_device *pdev)
{
	struct watchdog_device *wdd = platform_get_drvdata(pdev);

	sirfsoc_wdt_disable(wdd);
}

static int sirfsoc_wdt_remove(struct platform_device *pdev)
{
	sirfsoc_wdt_shutdown(pdev);
	return 0;
}

#ifdef	CONFIG_PM_SLEEP
static int sirfsoc_wdt_suspend(struct device *dev)
{
	return 0;
}

static int sirfsoc_wdt_resume(struct device *dev)
{
	struct watchdog_device *wdd = dev_get_drvdata(dev);

	/*
	 * NOTE: Since timer controller registers settings are saved
	 * and restored back by the timer-prima2.c, so we need not
	 * update WD settings except refreshing timeout.
	 */
	sirfsoc_wdt_updatetimeout(wdd);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(sirfsoc_wdt_pm_ops,
		sirfsoc_wdt_suspend, sirfsoc_wdt_resume);

MODULE_DEVICE_TABLE(of, sirfsoc_wdt_of_match);

static struct platform_driver sirfsoc_wdt_driver = {
	.driver = {
		.name = "sirfsoc-wdt",
		.owner = THIS_MODULE,
		.pm = &sirfsoc_wdt_pm_ops,
		.of_match_table	= sirfsoc_wdt_of_match,
	},
	.probe = sirfsoc_wdt_probe,
	.remove = sirfsoc_wdt_remove,
	.shutdown = sirfsoc_wdt_shutdown,
};
module_platform_driver(sirfsoc_wdt_driver);

MODULE_DESCRIPTION("SiRF SoC watchdog driver");
MODULE_AUTHOR("Xianglong Du <Xianglong.Du@csr.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:sirfsoc-wdt");
