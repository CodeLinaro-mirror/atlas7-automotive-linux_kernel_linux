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

#define PORTSC_PHCD		BIT(23)

struct ci13xxx_sirf_data {
	struct platform_device	*ci13xxx_pdev;
	struct clk		*clk;
	int			gpio_vbus;
};

static inline void
ci13xxx_sirf_drive_vbus(struct ci13xxx *ci, int value)
{
	struct ci13xxx_sirf_data *data =
		platform_get_drvdata(to_platform_device(ci->dev->parent));

	if (gpio_is_valid(data->gpio_vbus))
		gpio_direction_output(data->gpio_vbus, value ? 0 : 1);
}

static void ci13xxx_sirf_notify_event(struct ci13xxx *ci, unsigned event)
{
	switch (event) {
	case CI13XXX_CONTROLLER_RESET_EVENT:
		ci13xxx_sirf_drive_vbus(ci, 1);
		break;
	case CI13XXX_CONTROLLER_STOPPED_EVENT:
		ci13xxx_sirf_drive_vbus(ci, 0);
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

static int ci13xxx_sirf_probe(struct platform_device *pdev)
{
	struct ci13xxx_sirf_data *data;
	int ret;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		dev_err(&pdev->dev, "Failed to allocate ci13xxx_sirf_data!\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, data);

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
	data->gpio_vbus = of_get_named_gpio(pdev->dev.of_node,
							"vbus-gpios", 0);
	if (gpio_is_valid(data->gpio_vbus))
		ret = gpio_request(data->gpio_vbus, "ci13xxx_sirf");
		if (ret)
			dev_info(&pdev->dev, "Failed to get gpio control\n");
	}

	/* 4. set device dma mask */
	ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(&pdev->dev, "Failed to set coherent dma mask\n");
		goto err;
	}
	pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;

	/* 5. get phy for controller */
	ci13xxx_sirf_platdata.phy = devm_usb_get_phy_by_phandle(&pdev->dev,
								"usbphy", 0);
	if (IS_ERR(ci13xxx_sirf_platdata.phy)) {
		dev_err(&pdev->dev, "Failed to get transceiver\n");
		ret = PTR_ERR(ci13xxx_sirf_platdata.phy);
		goto err;
	}
	ret = usb_phy_init(ci13xxx_sirf_platdata.phy);
	if (ret) {
		dev_err(&pdev->dev, "Failed to init transceiver\n");
		goto err;
	}

	/* 7. register to ci13xxx core */
	data->ci13xxx_pdev = ci13xxx_add_device(&pdev->dev,
				pdev->resource, pdev->num_resources,
				&ci13xxx_sirf_platdata);
	if (IS_ERR(data->ci13xxx_pdev)) {
		dev_err(&pdev->dev, "ci13xxx_add_device failed!\n");
		return PTR_ERR(data->ci13xxx_pdev);
	}

	pm_runtime_no_callbacks(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	return 0;

err:
	clk_disable_unprepare(data->clk);
	return ret;
}

static int ci13xxx_sirf_remove(struct platform_device *pdev)
{
	struct ci13xxx_sirf_data *data = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);
	ci13xxx_remove_device(data->ci13xxx_pdev);

	clk_disable_unprepare(data->clk);

	return 0;
}

#ifdef CONFIG_PM
static int ci13xxx_sirf_suspend(struct device *dev)
{
	struct ci13xxx_sirf_data *data =
		platform_get_drvdata(to_platform_device(dev));
	struct ci13xxx *ci = platform_get_drvdata(data->ci13xxx_pdev);
	struct ci13xxx_platform_data *platdata = ci->platdata;

	hw_write(ci, OP_PORTSC, PORTSC_PHCD, 1);

	if (platdata->phy)
		usb_phy_set_suspend(platdata->phy, 1);

	clk_disable_unprepare(data->clk);

	return 0;
}

static int ci13xxx_sirf_resume(struct device *dev)
{
	struct ci13xxx_sirf_data *data =
		platform_get_drvdata(to_platform_device(dev));
	struct ci13xxx *ci = platform_get_drvdata(data->ci13xxx_pdev);
	struct ci13xxx_platform_data *platdata = ci->platdata;
	int ret;

	ret = clk_prepare_enable(data->clk);
	if (ret) {
		dev_err(dev,
			"Failed to prepare or enable clock, err=%d\n", ret);
		return ret;
	}

	if (hw_read(ci, OP_PORTSC, PORTSC_PHCD)) {
		hw_write(ci, OP_PORTSC, PORTSC_PHCD, 0);
		mdelay(10);
	}

	if (platdata->phy)
		usb_phy_set_suspend(platdata->phy, 0);

	return ret;
}

static const struct dev_pm_ops ci13xxx_sirf_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ci13xxx_sirf_suspend, ci13xxx_sirf_resume)
};
#endif

static const struct of_device_id ci13xxx_sirf_dt_ids[] = {
	{ .compatible = "chipidea,ci13611a-prima2", },
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
#ifdef CONFIG_PM
		.pm = &ci13xxx_sirf_pm_ops,
#endif
	 },
};
module_platform_driver(ci13xxx_sirf_driver);

MODULE_ALIAS("platform:sirf-ci13xxx-usbcontroller");
MODULE_AUTHOR("Rong Wang <Rong.Wang@csr.com>");
MODULE_DESCRIPTION("CI13XXX SiRF USB Binding");
MODULE_LICENSE("GPL v2");
