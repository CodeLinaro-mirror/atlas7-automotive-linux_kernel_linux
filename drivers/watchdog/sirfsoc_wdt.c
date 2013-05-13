/*
 * I2C bus driver for CSR SiRFprimaII
 *
 * Copyright (c) 2013 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/platform_device.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/uaccess.h>

#define SIRFSOC_TIMER_COUNTER_LO	0x0000
#define SIRFSOC_TIMER_MATCH_0		0x0008
#define SIRFSOC_TIMER_INT_EN		0x0024
#define SIRFSOC_TIMER_WATCHDOG_EN	0x0028
#define SIRFSOC_TIMER_LATCH		0x0030
#define SIRFSOC_TIMER_LATCHED_LO	0x0034

#define SIRFSOC_TIMER_WDT_INDEX		5

#define SIRFSOC_WDT_MIN_TIMEOUT		30		/* 30 secs */
#define SIRFSOC_WDT_MAX_TIMEOUT		(10 * 60)	/* 10 mins */
#define SIRFSOC_WDT_DEFAULT_TIMEOUT	30		/* 30 secs */


static unsigned int default_timeout = SIRFSOC_WDT_DEFAULT_TIMEOUT;
module_param(default_timeout, uint, 0);
MODULE_PARM_DESC(default_timeout, "Default watchdog timeout (in seconds)");

static unsigned int sirfsoc_wdt_gettimeleft(struct watchdog_device *wdd)
{
	u32 counter, match;
	int time_left;

	counter = readl(watchdog_get_drvdata(wdd) + SIRFSOC_TIMER_COUNTER_LO);
	match = readl(watchdog_get_drvdata(wdd) +
		SIRFSOC_TIMER_MATCH_0 + (SIRFSOC_TIMER_WDT_INDEX << 2));

	if (match >= counter) {
		time_left = match-counter;
	} else {
		/* rollover */
		time_left = (0xffffffffUL - counter) + match;
	}

	return time_left / CLOCK_TICK_RATE;
}

static int sirfsoc_wdt_updatetimeout(struct watchdog_device *wdd)
{
	u32 counter, timeout_ticks;

	timeout_ticks = wdd->timeout * CLOCK_TICK_RATE;

	/* Enable the latch before reading the LATCH_LO register */
	writel(1, watchdog_get_drvdata(wdd) + SIRFSOC_TIMER_LATCH);

	/* Set the TO value */
	counter = readl(watchdog_get_drvdata(wdd) + SIRFSOC_TIMER_LATCHED_LO);

	if ((0xffffffffUL - counter) >= timeout_ticks) {
		counter += timeout_ticks;
	} else {
		/* Rollover */
		counter = timeout_ticks - (0xffffffffUL - counter);
	}
	writel(counter, watchdog_get_drvdata(wdd) +
		SIRFSOC_TIMER_MATCH_0 + (SIRFSOC_TIMER_WDT_INDEX << 2));

	return 0;
}

static int sirfsoc_wdt_enable(struct watchdog_device *wdd)
{
	sirfsoc_wdt_updatetimeout(wdd);

	/*
	 * NOTE: If interrupt is not enabled
	 * then WD-Reset doesn't get generated at all.
	 */
	writel(readl(watchdog_get_drvdata(wdd) + SIRFSOC_TIMER_INT_EN)
		| (1 << SIRFSOC_TIMER_WDT_INDEX),
		watchdog_get_drvdata(wdd) + SIRFSOC_TIMER_INT_EN);
	writel(1, watchdog_get_drvdata(wdd) + SIRFSOC_TIMER_WATCHDOG_EN);

	return 0;
}

static int sirfsoc_wdt_disable(struct watchdog_device *wdd)
{
	writel(0, watchdog_get_drvdata(wdd) + SIRFSOC_TIMER_WATCHDOG_EN);
	writel(readl(watchdog_get_drvdata(wdd) + SIRFSOC_TIMER_INT_EN)
		& (~(1 << SIRFSOC_TIMER_WDT_INDEX)),
		watchdog_get_drvdata(wdd) + SIRFSOC_TIMER_INT_EN);

	return 0;
}

static long sirfsoc_wdt_ioctl(struct watchdog_device *wdd,
	unsigned int cmd, unsigned long arg)
{
	int val = 0;
	const struct watchdog_info ident = *wdd->info;
	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user((struct watchdog_info __user *)arg, &ident,
				sizeof(ident));
	case WDIOC_SETTIMEOUT:
		if (get_user(val, (int __user *)arg))
			return -EFAULT;
		if (val < SIRFSOC_WDT_MIN_TIMEOUT)
			val = SIRFSOC_WDT_MIN_TIMEOUT;
		else if (val > SIRFSOC_WDT_MAX_TIMEOUT)
			val = SIRFSOC_WDT_MAX_TIMEOUT;
		wdd->timeout = val;
		sirfsoc_wdt_updatetimeout(wdd);
		/* Fall to WDIOC_GETTIMEOUT*/
	case WDIOC_GETTIMEOUT:
		/* timeout == 0 means that we don't know the timeout */
		if (wdd->timeout == 0)
			return -EOPNOTSUPP;
		return put_user(wdd->timeout, (int __user *)arg);
	default:
		return -ENOIOCTLCMD;
	}
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
	.ioctl = sirfsoc_wdt_ioctl,
	.get_timeleft = sirfsoc_wdt_gettimeleft,
	.ping = sirfsoc_wdt_updatetimeout,
};

static struct watchdog_device sirfsoc_wdd = {
	.info = &sirfsoc_wdt_ident,
	.ops = &sirfsoc_wdt_ops,
	.timeout = SIRFSOC_WDT_DEFAULT_TIMEOUT,
	.min_timeout = SIRFSOC_WDT_MIN_TIMEOUT,
	.max_timeout = SIRFSOC_WDT_MAX_TIMEOUT,
};

static int sirfsoc_wdt_probe(struct platform_device *pdev)
{
	struct resource *res;
	int ret;
	void __iomem *base;

	/* reserve static register mappings */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "sirfsoc wdt: could not get mem resources\n");
		ret = -ENOMEM;
		goto out;
	}

	base = devm_ioremap_resource(&pdev->dev, res);
	if (!base) {
		dev_err(&pdev->dev, "sirfsoc wdt: could not remap the mem\n");
		ret = -EADDRNOTAVAIL;
		goto out;
	}
	watchdog_set_drvdata(&sirfsoc_wdd, base);

	sirfsoc_wdd.timeout = default_timeout;

	ret = watchdog_register_device(&sirfsoc_wdd);
	if (!!ret)
		goto out;

	platform_set_drvdata(pdev, &sirfsoc_wdd);

	return 0;

out:
	return ret;
}

static int sirfsoc_wdt_remove(struct platform_device *pdev)
{
	struct watchdog_device *wdd = platform_get_drvdata(pdev);

	sirfsoc_wdt_disable(wdd);

	return 0;
}

static void sirfsoc_wdt_shutdown(struct platform_device *pdev)
{
	sirfsoc_wdt_remove(pdev);
}

#ifdef	CONFIG_PM

static int sirfsoc_wdt_suspend(struct device *dev)
{
	return 0;
}

static int sirfsoc_wdt_resume(struct device *dev)
{
	struct watchdog_device *wdd = dev_get_drvdata(dev);

	/*
	 * NOTE: Since timer controller registers settings are saved
	 * and restored back by the pm.c, so we need not update WD
	 * settings except refreshing timeout.
	 */
	sirfsoc_wdt_updatetimeout(wdd);

	return 0;
}

#else
#define	sirfsoc_wdt_suspend		NULL
#define	sirfsoc_wdt_resume		NULL
#endif

static const struct dev_pm_ops sirfsoc_wdt_pm_ops = {
	.suspend = sirfsoc_wdt_suspend,
	.resume = sirfsoc_wdt_resume,
};

static const struct of_device_id sirfsoc_wdt_of_match[] = {
	{ .compatible = "sirf,prima2-wdt"},
	{},
};
MODULE_DEVICE_TABLE(of, sirfsoc_wdt_of_match);

static struct platform_driver sirfsoc_wdt_driver = {
	.driver = {
		.name = "sirfsoc-wdt",
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm = &sirfsoc_wdt_pm_ops,
#endif
		.of_match_table	= of_match_ptr(sirfsoc_wdt_of_match),
	},
	.probe = sirfsoc_wdt_probe,
	.remove = sirfsoc_wdt_remove,
	.shutdown = sirfsoc_wdt_shutdown,
};
module_platform_driver(sirfsoc_wdt_driver);

MODULE_DESCRIPTION("SiRF SoC watchdog driver");
MODULE_AUTHOR("Xianglong Du <Xianglong.Du@csr.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
MODULE_ALIAS("platform:sirfsoc-wdt");
