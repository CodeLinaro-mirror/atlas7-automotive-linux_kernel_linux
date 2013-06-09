/*
 * USB Controller Driver for CSR SiRF SoC
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 * Rong Wang<Rong.Wang@csr.com>
 *
 * Licensed under GPLv2 or later.
 */
#include <linux/module.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/reset.h>

#include <linux/usb/chipidea.h>
#include "ci.h"

#define RSC_USB_UART_SHARE	0x0
#define USB1_MODE_SEL		BIT(2)
#define pdev_to_phy(pdev)	((struct usb_phy *)platform_get_drvdata(pdev))

static int sirfsoc_vbus_gpio;

struct ci13xxx_sirf_data {
	struct platform_device	*ci_pdev;
	struct clk		*clk;
};

static inline int ci13xxx_sirf_drive_vbus(int value)
{
	return gpio_direction_output(sirfsoc_vbus_gpio, value ? 0 : 1);
}

static void ci13xxx_sirf_notify_event(struct ci13xxx *ci, unsigned event)
{
	switch (event) {
	case CI13XXX_CONTROLLER_RESET_EVENT:
		ci13xxx_sirf_drive_vbus(1);
		break;
	case CI13XXX_CONTROLLER_STOPPED_EVENT:
		ci13xxx_sirf_drive_vbus(0);
		break;
	default:
		dev_info(ci->dev, "Unknown Event\n");
		break;
	}
}

static struct ci13xxx_platform_data ci13xxx_sirf_platdata = {
	.name			= "ci13xxx_sirf",
	.flags			= CI13XXX_DISABLE_STREAMING,
	.capoffset		= DEF_CAPOFFSET,
	.notify_event		= ci13xxx_sirf_notify_event,
};

static struct of_device_id rsc_ids[] = {
	{ .compatible = "sirf,prima2-rsc", },
	{ /* sentinel */ }
};

static int ci13xxx_sirf_probe(struct platform_device *pdev)
{
	struct platform_device *plat_ci, *phy_pdev;
	struct device_node *rsc_np, *phy_np;
	struct ci13xxx_sirf_data *data;
	struct usb_phy *phy;
	void __iomem *rsc_vbase;
	int ret;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		dev_err(&pdev->dev, "Failed to allocate ci13xxx_sirf_data!\n");
		return -ENOMEM;
	}

	/* 1. set usb controller clock */
	data->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(data->clk)) {
		dev_err(&pdev->dev,
			"Failed to get clock, err=%ld\n", PTR_ERR(data->clk));
		return PTR_ERR(data->clk);
	}
	ret = clk_prepare_enable(data->clk);
	if (ret) {
		dev_err(&pdev->dev,
			"Failed to prepare or enable clock, err=%d\n", ret);
		return ret;
	}

	/* 2. software reset */
	ret = device_reset(&pdev->dev);
	if (ret)
		dev_info(&pdev->dev,
			"Failed to reset device, err=%d\n", ret);

	/* 3. vbus configuration */
	sirfsoc_vbus_gpio = of_get_named_gpio(pdev->dev.of_node,
							"vbus-gpios", 0);
	if (sirfsoc_vbus_gpio < 0) {
		dev_err(&pdev->dev, "Can't get vbus gpio from DT\n");
		ret = -ENODEV;
		goto err;
	}
	ret = gpio_request(sirfsoc_vbus_gpio, "ci13xxx_sirf");
	if (ret) {
		dev_err(&pdev->dev, "Failed to get gpio control\n");
		goto err;
	}

	/* 4. rsc control */
	rsc_np = of_find_matching_node(NULL, rsc_ids);
	if (!rsc_np) {
		dev_err(&pdev->dev, "Failed to get rsc device node\n");
		ret = -ENODEV;
		goto err;
	}
	rsc_vbase = of_iomap(rsc_np, 0);
	if (!rsc_vbase) {
		dev_err(&pdev->dev, "Failed to iomap rsc memory\n");
		ret = -ENOMEM;
		goto err;
	}
	writel(readl(rsc_vbase + RSC_USB_UART_SHARE) | USB1_MODE_SEL,
					rsc_vbase + RSC_USB_UART_SHARE);

	/* 5. set device dma mask */
	if (!pdev->dev.dma_mask) {
		pdev->dev.dma_mask = devm_kzalloc(&pdev->dev,
				      sizeof(*pdev->dev.dma_mask), GFP_KERNEL);
		if (!pdev->dev.dma_mask) {
			dev_err(&pdev->dev, "Failed to alloc dma_mask!\n");
			ret = -ENOMEM;
			goto err;
		}
		*pdev->dev.dma_mask = DMA_BIT_MASK(32);
		dma_set_coherent_mask(&pdev->dev, *pdev->dev.dma_mask);
	}

	/* 6. get phy for controller */
	phy_np = of_parse_phandle(pdev->dev.of_node, "sirf,ci13xxx-usbphy", 0);
	if (!phy_np) {
		dev_err(&pdev->dev, "Failed to get phy device node\n");
		ret = -ENODEV;
		goto err;
	}
	phy_pdev = of_find_device_by_node(phy_np);
	if (!phy_pdev) {
		dev_err(&pdev->dev, "Failed to get phy platform device\n");
		ret = -ENODEV;
		goto err;
	}
	phy = pdev_to_phy(phy_pdev);
	if (!phy || !try_module_get(phy_pdev->dev.driver->owner)) {
		dev_err(&pdev->dev, "Failed to get phy control\n");
		ret = -ENODEV;
		goto err;
	}
	usb_phy_init(phy);
	ci13xxx_sirf_platdata.phy = phy;

	/* 7. register to ci13xxx core */
	plat_ci = ci13xxx_add_device(&pdev->dev,
				pdev->resource, pdev->num_resources,
				&ci13xxx_sirf_platdata);
	if (IS_ERR(plat_ci)) {
		dev_err(&pdev->dev, "ci13xxx_add_device failed!\n");
		return PTR_ERR(plat_ci);
	}

	platform_set_drvdata(pdev, plat_ci);

	pm_runtime_no_callbacks(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	dev_info(&pdev->dev, "Ready\n");
	return 0;

err:
	clk_disable_unprepare(data->clk);
	return ret;
}

static int ci13xxx_sirf_remove(struct platform_device *pdev)
{
	struct ci13xxx_sirf_data *data = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);
	ci13xxx_remove_device(data->ci_pdev);

	clk_disable_unprepare(data->clk);

	return 0;
}

static const struct of_device_id ci13xxx_sirf_dt_ids[] = {
	{ .compatible = "sirf,ci13xxx-usbcontroller", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ci13xxx_sirf_dt_ids);

static struct platform_driver ci13xxx_sirf_driver = {
	.probe = ci13xxx_sirf_probe,
	.remove = ci13xxx_sirf_remove,
	.driver = {
		.name = "sirf-usbcontroller",
		.owner = THIS_MODULE,
		.of_match_table = ci13xxx_sirf_dt_ids,
	 },
};
module_platform_driver(ci13xxx_sirf_driver);

MODULE_ALIAS("platform:sirf-ci13xxx-usbcontroller");
MODULE_AUTHOR("Rong Wang <Rong.Wang@csr.com>");
MODULE_DESCRIPTION("CI13XXX SiRF USB Binding");
MODULE_LICENSE("GPL v2");
