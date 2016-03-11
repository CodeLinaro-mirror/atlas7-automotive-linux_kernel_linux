/*
 * UIO driver for CSR sirfsoc LCD module
 *
 * Copyright (c) 2014, 2016, The Linux Foundation. All rights reserved.
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

#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/uio_driver.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_address.h>

#define SMEM_SIZE                       (6*SZ_1M)

struct sirf_lcd_info {
	int irq;
	struct clk *clk;
	unsigned long size;
	void *base;
	dma_addr_t smem_start;
	struct uio_info *uio_info;
};

static irqreturn_t sirf_lcd_irq_handler(int irq, struct uio_info *info)
{
	return IRQ_HANDLED;
}

static int sirf_lcd_probe(struct platform_device *pdev)
{
	int ret;
	struct sirf_lcd_info *lcd_info;
	struct uio_info *info;
	void __iomem *base;
	struct device_node *node;
	struct resource *res;

	lcd_info = devm_kzalloc(&pdev->dev, sizeof(*lcd_info), GFP_KERNEL);
	if (!lcd_info) {
		ret = -ENOMEM;
		goto err;
	}

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info) {
		ret = -ENOMEM;
		goto err;
	}

	lcd_info->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(lcd_info->clk)) {
		pr_err("%s: fail to get lcd clock!\n", __func__);
		ret = -EINVAL;
		goto err;
	}

	lcd_info->base = dma_alloc_coherent(&pdev->dev, SMEM_SIZE,
		&lcd_info->smem_start, GFP_KERNEL);
	if (!lcd_info->base) {
		pr_err("%s: fail to allocate lcd dma mem!\n", __func__);
		ret = -ENOMEM;
		goto err;
	}
	lcd_info->size = SMEM_SIZE;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		pr_err("%s: fail to find IO resource\n", __func__);
		ret = -ENOENT;
		goto err_free;
	}
	info->mem[0].addr = res->start;
	info->mem[0].size = res->end - res->start + 1;
	info->mem[0].memtype = UIO_MEM_PHYS;
	info->mem[1].addr = lcd_info->smem_start;
	info->mem[1].size = lcd_info->size;
	info->mem[1].memtype = UIO_MEM_PHYS;
	info->name = "sirf_lcd";
	info->version = "0.1";
	info->irq = lcd_info->irq;
	info->irq_flags = IRQF_TRIGGER_NONE;
	info->handler = sirf_lcd_irq_handler;
	if (uio_register_device(&pdev->dev, info)) {
		pr_err("%s: fail to register uio device!\n", __func__);
		ret = -ENODEV;
		goto err_free;
	}

	lcd_info->uio_info = info;
	platform_set_drvdata(pdev, lcd_info);

	clk_prepare_enable(lcd_info->clk);

	pr_info("%s: uio lcd is loaded!\n", __func__);

	return 0;

err_free:
	dma_free_coherent(&pdev->dev, lcd_info->size,
		lcd_info->base, lcd_info->smem_start);
err:
	return ret;
}

static int sirf_lcd_remove(struct platform_device *pdev)
{
	struct sirf_lcd_info *lcd_info = platform_get_drvdata(pdev);

	uio_unregister_device(lcd_info->uio_info);

	dma_free_coherent(&pdev->dev, lcd_info->size,
		lcd_info->base, lcd_info->smem_start);

	clk_disable(lcd_info->clk);
	clk_put(lcd_info->clk);

	return 0;
}

const struct of_device_id sirf_lcd_match_tbl[] = {
	{ .compatible = "sirf,atlas7-lcdc", },
	{ /* end */ }
};

static struct platform_driver uio_lcd_driver = {
	.driver = {
		.name = "sirfsoc_lcdc",
		.of_match_table = sirf_lcd_match_tbl,
	},
	.probe = sirf_lcd_probe,
	.remove = sirf_lcd_remove,
};

static int __init uio_lcd_init(void)
{
	return platform_driver_register(&uio_lcd_driver);
}

static void __exit uio_lcd_exit(void)
{
	platform_driver_unregister(&uio_lcd_driver);
}

module_init(uio_lcd_init);
module_exit(uio_lcd_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("UIO LCD module driver");
