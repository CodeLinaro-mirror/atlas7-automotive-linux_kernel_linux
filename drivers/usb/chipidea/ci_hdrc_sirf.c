/*
 * USB Controller Driver for CSR SiRF SoC
 *
 * Copyright (c) 2014-2016, The Linux Foundation. All rights reserved.
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

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <linux/usb/chipidea.h>

#include "ci.h"

struct ci_hdrc_sirf_data {
	struct platform_device	*ci_pdev;
	struct clk		*clk;
};

static struct ci_hdrc_platform_data ci_hdrc_sirf_platdata[] = {
	[0] = {
		.name			= "ci_hdrc_sirf.0",
		.flags			= CI_HDRC_DISABLE_STREAMING,
		.capoffset		= DEF_CAPOFFSET,
	},
	[1] = {
		.name			= "ci_hdrc_sirf.1",
		.flags			= CI_HDRC_DISABLE_STREAMING |
					  CI_HDRC_DUAL_ROLE_NOT_OTG,
		.capoffset		= DEF_CAPOFFSET,
	},
};

static int ci_hdrc_sirf_probe(struct platform_device *pdev)
{
	struct ci_hdrc_sirf_data *data;
	int ret;
	int id;

	if (of_property_read_u32(pdev->dev.of_node, "cell-index", &id)) {
		dev_err(&pdev->dev, "Fail to get USB index\n");
		return -ENODEV;
	}

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		dev_err(&pdev->dev, "Failed to allocate ci_hdrc_sirf_data!\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, data);

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

	ret = dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(&pdev->dev, "Failed to set dma mask\n");
		goto err;
	}

	ci_hdrc_sirf_platdata[id].usb_phy =
		devm_usb_get_phy_by_phandle(&pdev->dev, "sirf,usbphy", 0);
	if (IS_ERR(ci_hdrc_sirf_platdata[id].usb_phy)) {
		dev_err(&pdev->dev, "Failed to get transceiver\n");
		ret = -ENODEV;
		goto err;
	}

	data->ci_pdev = ci_hdrc_add_device(&pdev->dev,
				pdev->resource, pdev->num_resources,
				&ci_hdrc_sirf_platdata[id]);
	if (IS_ERR(data->ci_pdev)) {
		dev_err(&pdev->dev, "ci_hdrc_add_device failed!\n");
		return PTR_ERR(data->ci_pdev);
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
	ci_hdrc_remove_device(data->ci_pdev);

	clk_disable_unprepare(data->clk);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int ci_hdrc_sirf_pm_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ci_hdrc_sirf_data *data = platform_get_drvdata(pdev);

	clk_disable_unprepare(data->clk);

	return 0;
}

static int ci_hdrc_sirf_pm_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ci_hdrc_sirf_data *data = platform_get_drvdata(pdev);

	return clk_prepare_enable(data->clk);
}

static SIMPLE_DEV_PM_OPS(ci_hdrc_sirf_pm_ops, ci_hdrc_sirf_pm_suspend,
		ci_hdrc_sirf_pm_resume);
#endif

static const struct of_device_id ci_hdrc_sirf_dt_ids[] = {
	{ .compatible = "sirf,atlas7-usb", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ci_hdrc_sirf_dt_ids);

static struct platform_driver ci_hdrc_sirf_driver = {
	.probe = ci_hdrc_sirf_probe,
	.remove = ci_hdrc_sirf_remove,
	.driver = {
		.name = "sirf-usb",
		.owner = THIS_MODULE,
		.of_match_table = ci_hdrc_sirf_dt_ids,
#ifdef CONFIG_PM_SLEEP
		.pm = &ci_hdrc_sirf_pm_ops,
#endif
	 },
};
module_platform_driver(ci_hdrc_sirf_driver);

MODULE_DESCRIPTION("CI HDRC SiRF USB Binding");
MODULE_LICENSE("GPL v2");
