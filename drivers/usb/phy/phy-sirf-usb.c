/*
 * USB PHY Driver for CSR SiRF SoC
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 * Rong Wang<Rong.Wang@csr.com>
 *
 * Licensed under GPLv2 or later.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/usb/otg.h>
#include <linux/usb/phy.h>
#include <linux/stmp_device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/workqueue.h>

struct sirf_phy {
	struct usb_phy		phy;
	struct clk		*clk;
};

#define DRIVER_NAME	"sirf-usbphy"
#define to_sirf_phy(p)	container_of((p), struct sirf_phy, phy)
#define USBPHY_POR	BIT(27)

static inline void sirf_phy_por(void __iomem *base)
{
	writel(readl(base) | USBPHY_POR, base);
	udelay(15);
	writel(readl(base) & ~USBPHY_POR, base);
}

static int sirf_phy_init(struct usb_phy *phy)
{
	struct sirf_phy *sirf_phy = to_sirf_phy(phy);

	clk_prepare_enable(sirf_phy->clk);
	sirf_phy_por(phy->io_priv);

	return 0;
}

static void sirf_phy_shutdown(struct usb_phy *phy)
{
	struct sirf_phy *sirf_phy = to_sirf_phy(phy);

	clk_disable_unprepare(sirf_phy->clk);
}

static int
sirf_phy_set_peripheral(struct usb_otg *otg, struct usb_gadget *gadget)
{
	if (!otg)
		return -ENODEV;

	if (!gadget) {
		otg->gadget = NULL;
		return -ENODEV;
	}

	otg->gadget = gadget;
	otg->phy->state = OTG_STATE_B_IDLE;
	return 0;
}

static int sirf_phy_set_host(struct usb_otg *otg, struct usb_bus *host)
{
	if (!otg)
		return -ENODEV;

	if (!host) {
		otg->host = NULL;
		return -ENODEV;
	}

	otg->host = host;
	return 0;
}

static int sirf_phy_probe(struct platform_device *pdev)
{
	struct resource *res;
	void __iomem *base;
	struct clk *clk;
	struct sirf_phy *sirf_phy;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev,
			"Can't get the clock, err=%ld", PTR_ERR(clk));
		return PTR_ERR(clk);
	}

	sirf_phy = devm_kzalloc(&pdev->dev, sizeof(*sirf_phy), GFP_KERNEL);
	if (!sirf_phy)
		return -ENOMEM;

	sirf_phy->phy.otg = devm_kzalloc(&pdev->dev,
				sizeof(*sirf_phy->phy.otg), GFP_KERNEL);
	if (!sirf_phy->phy.otg)
		return -ENOMEM;

	sirf_phy->phy.io_priv		= base;
	sirf_phy->phy.dev		= &pdev->dev;
	sirf_phy->phy.label		= DRIVER_NAME;
	sirf_phy->phy.init		= sirf_phy_init;
	sirf_phy->phy.shutdown		= sirf_phy_shutdown;

	sirf_phy->phy.otg->phy			= &sirf_phy->phy;
	sirf_phy->phy.otg->set_host		= sirf_phy_set_host;
	sirf_phy->phy.otg->set_peripheral	= sirf_phy_set_peripheral;

	ATOMIC_INIT_NOTIFIER_HEAD(&sirf_phy->phy.notifier);

	sirf_phy->clk = clk;
	platform_set_drvdata(pdev, sirf_phy);

	ret = usb_add_phy_dev(&sirf_phy->phy);
	if (ret)
		return ret;
	dev_info(&pdev->dev, "Ready\n");
	return 0;
}

static int sirf_phy_remove(struct platform_device *pdev)
{
	struct sirf_phy *sirf_phy = platform_get_drvdata(pdev);

	usb_remove_phy(&sirf_phy->phy);
	return 0;
}

static const struct of_device_id sirf_phy_dt_ids[] = {
	{ .compatible = "sirf,atlas7-usbphy", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sirf_phy_dt_ids);

static struct platform_driver sirf_phy_driver = {
	.probe = sirf_phy_probe,
	.remove = sirf_phy_remove,
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = sirf_phy_dt_ids,
	 },
};

module_platform_driver(sirf_phy_driver);

MODULE_AUTHOR("Rong Wang <Rong.Wang@csr.com>");
MODULE_DESCRIPTION("SiRF ChipIdea USB PHY driver");
MODULE_LICENSE("GPL v2");
