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
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/reset.h>

#include <linux/usb/chipidea.h>
#include "ci.h"

#define PORTSC_PHCD		BIT(23)

struct ci_hdrc_sirf_data {
	struct platform_device	*plat_ci;
	struct clk		*clk;
};

static inline int
ci_hdrc_sirf_drive_vbus(struct ci_hdrc *ci, int value)
{
	struct platform_device *pdev = container_of(ci->dev->parent,
						struct platform_device, dev);
	struct ci_hdrc_sirf_data *data = platform_get_drvdata(pdev);

	if (data->vbus)
		return gpio_direction_output(data->vbus, value ? 0 : 1);

	return 0;
}

static void ci_hdrc_sirf_notify_event(struct ci_hdrc *ci, unsigned event)
{
	switch (event) {
	case CI_HDRC_CONTROLLER_RESET_EVENT:
		ci_hdrc_sirf_drive_vbus(ci, 1);
		break;
	case CI_HDRC_CONTROLLER_STOPPED_EVENT:
		ci_hdrc_sirf_drive_vbus(ci, 0);
		break;
	default:
		dev_info(ci->dev, "Unknown Event\n");
		break;
	}
}

static struct ci_hdrc_platform_data ci_hdrc_sirf_platdata = {
	.name			= "ci_hdrc_sirf",
	.flags			= CI_HDRC_DISABLE_STREAMING,
	.capoffset		= DEF_CAPOFFSET,
	.notify_event		= ci_hdrc_sirf_notify_event,
};

static struct of_device_id rsc_ids[] = {
	{ .compatible = "sirf,prima2-rsc", },
	{ /* sentinel */ }
};

static int ci_hdrc_sirf_probe(struct platform_device *pdev)
{
	struct platform_device *plat_ci, *phy_pdev;
	struct device_node *rsc_np, *phy_np;
	struct ci_hdrc_sirf_data *data;
	struct usb_phy *phy;
	void __iomem *rsc_vbase;
	int ret;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		dev_err(&pdev->dev, "Failed to allocate ci_hdrc_sirf_data!\n");
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

	/* 3. set device dma mask */
	ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(&pdev->dev, "Failed to set coherent dma mask\n");
		goto err;
	}
	pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;

	/* 4. get phy for controller */
	ci_hdrc_sirf_platdata.phy = devm_usb_get_phy_by_phandle(&pdev->dev,
								"usbphy", 0);
	if (IS_ERR(ci_hdrc_sirf_platdata.phy)) {
		dev_err(&pdev->dev, "Failed to get transceiver\n");
		ret = PTR_ERR(ci_hdrc_sirf_platdata.phy);
		goto err;
	}
	ret = usb_phy_init(ci_hdrc_sirf_platdata.phy);
	if (ret) {
		dev_err(&pdev->dev, "Failed to init transceiver\n");
		goto err;
	}

	/* 5. register to ci_hdrc core */
	data->ci_hdrc_pdev = ci_hdrc_add_device(&pdev->dev,
				pdev->resource, pdev->num_resources,
				&ci_hdrc_sirf_platdata);
	if (IS_ERR(data->ci_hdrc_pdev)) {
		dev_err(&pdev->dev, "ci_hdrc_add_device failed!\n");
		return PTR_ERR(data->ci_hdrc_pdev);
	}

	pm_runtime_no_callbacks(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	return 0;

err:
	clk_disable_unprepare(data->clk);
	return ret;
}

static int ci_hdrc_sirf_remove(struct platform_device *pdev)
{
	struct ci_hdrc_sirf_data *data = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);
	ci_hdrc_remove_device(data->plat_ci);
	clk_disable_unprepare(data->clk);

	return 0;
}

#ifdef CONFIG_PM
static int ci_hdrc_sirf_suspend(struct device *dev)
{
	struct ci_hdrc_sirf_data *data =
		platform_get_drvdata(to_platform_device(dev));
	struct ci_hdrc *ci = platform_get_drvdata(data->ci_hdrc_pdev);
	struct ci_hdrc_platform_data *platdata = ci->platdata;

	hw_write(ci, OP_PORTSC, PORTSC_PHCD, 1);

	if (platdata->phy)
		usb_phy_set_suspend(platdata->phy, 1);

	clk_disable_unprepare(data->clk);

	return 0;
}

static int ci_hdrc_sirf_resume(struct device *dev)
{
	struct ci_hdrc_sirf_data *data =
		platform_get_drvdata(to_platform_device(dev));
	struct ci_hdrc *ci = platform_get_drvdata(data->ci_hdrc_pdev);
	struct ci_hdrc_platform_data *platdata = ci->platdata;
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

static const struct dev_pm_ops ci_hdrc_sirf_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ci_hdrc_sirf_suspend, ci_hdrc_sirf_resume)
};
#endif

static const struct of_device_id ci_hdrc_sirf_dt_ids[] = {
	{ .compatible = "chipidea,ci13611a-prima2", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ci_hdrc_sirf_dt_ids);

static struct platform_driver ci_hdrc_sirf_driver = {
	.probe = ci_hdrc_sirf_probe,
	.remove = ci_hdrc_sirf_remove,
	.driver = {
		.name = "sirf-usbcontroller",
		.owner = THIS_MODULE,
		.of_match_table = ci_hdrc_sirf_dt_ids,
#ifdef CONFIG_PM
		.pm = &ci_hdrc_sirf_pm_ops,
#endif
	 },
};
module_platform_driver(ci_hdrc_sirf_driver);

MODULE_AUTHOR("Rong Wang <Rong.Wang@csr.com>");
MODULE_DESCRIPTION("CI_HDRC SiRF USB Binding");
MODULE_LICENSE("GPL v2");
