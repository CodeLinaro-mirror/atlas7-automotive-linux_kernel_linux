/*
 * SIRF Inter-processor Communication Device Driver
 *
 * Copyright (c) 2014 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include <linux/mfd/core.h>
#include <linux/mfd/sirfsoc_ipc.h>

static const struct mfd_cell sirf_ipc_cells[] = {
	{
		.name = "atlas7-hwspinlock",
		.of_compatible = "sirf,hwspinlock",
	}, {
		.name = "s2ns0-rproc",
		.of_compatible = "sirf,s2ns0-rproc",
	}, {
		.name = "s2ns1-rproc",
		.of_compatible = "sirf,s2ns1-rproc",
	}, {
		.name = "s2m30-rproc",
		.of_compatible = "sirf,s2m30-rproc",
	}, {
		.name = "s2m31-rproc",
		.of_compatible = "sirf,s2m31-rproc",
	}, {
		.name = "s2kal0-rproc",
		.of_compatible = "sirf,s2kal0-rproc",
	}, {
		.name = "s2kal1-rproc",
		.of_compatible = "sirf,s2kal1-rproc",
	}, {
		.name = "ns2s0-rproc",
		.of_compatible = "sirf,ns2s0-rproc",
	}, {
		.name = "ns2s1-rproc",
		.of_compatible = "sirf,ns2s1-rproc",
	}, {
		.name = "ns2m30-rproc",
		.of_compatible = "sirf,ns2m30-rproc",
	}, {
		.name = "ns2m31-rproc",
		.of_compatible = "sirf,ns2m31-rproc",
	}, {
		.name = "ns2kal0-rproc",
		.of_compatible = "sirf,ns2kal0-rproc",
	}, {
		.name = "ns2kal1-rproc",
		.of_compatible = "sirf,ns2kal1-rproc",
	},
};


static int sirfsoc_ipc_probe(struct platform_device *pdev)
{
	struct sirf_ipc_device *ipc;
	int ret;

	if (!pdev->dev.of_node) {
		dev_err(&pdev->dev,
			"Doesn't have device tree node!\n");
		return -ENODEV;
	}

	ipc = devm_kzalloc(&pdev->dev, sizeof(*ipc), GFP_KERNEL);
	if (!ipc)
		return -ENOMEM;

	ipc->dev = &pdev->dev;
	ipc->pdev = pdev;
	ipc->cells = sirf_ipc_cells;
	ipc->num_cells = ARRAY_SIZE(sirf_ipc_cells);

	ret = mfd_add_devices(&pdev->dev, pdev->id, ipc->cells,
			ipc->num_cells, NULL, 0, NULL);
	if (ret) {
		dev_err(&pdev->dev, "fail to register client devices\n");
		return ret;
	}
	dev_info(&pdev->dev, "Added %d devices in to system!",
			ipc->num_cells);

	return 0;
}

static int sirfsoc_ipc_remove(struct platform_device *pdev)
{
	struct sirf_ipc_device *ipc = platform_get_drvdata(pdev);

	mfd_remove_devices(ipc->dev);

	return 0;
}

static const struct of_device_id sirfsoc_ipc_of_match[] = {
	{ .compatible = "sirf,atlas7-ipc", },
	{},
};
MODULE_DEVICE_TABLE(of, sirfsoc_ipc_of_match);

static struct platform_driver sirfsoc_ipc_driver = {
	.probe = sirfsoc_ipc_probe,
	.remove = sirfsoc_ipc_remove,
	.driver = {
		.name = "sirfsoc_ipc",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(sirfsoc_ipc_of_match),
	},
};

module_platform_driver(sirfsoc_ipc_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("SIRF Inter-processor Communication Device");
