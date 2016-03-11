/*
 * UIO driver for CSR sirfsoc VPP module
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

#define SIRFSOC_VPP_RESERVE_MEM

struct sirf_vpp_info {
	struct clk *clk;

	dma_addr_t smem_start;
	void *base;
	unsigned long size;

	struct uio_info *uio_info;
};

static int __sirf_uio_vpp_parse_memory(struct platform_device *pdev,
				struct sirf_vpp_info *vpp_info)
{
	struct device_node *m_node;
	struct resource res;
	int ret = 0;

	m_node = of_parse_phandle(pdev->dev.of_node, "memory-region", 0);
	if (!m_node)
		return -ENODEV;

	ret = of_address_to_resource(m_node, 0, &res);
	if (ret) {
		dev_err(&pdev->dev,
			"Convert address to resource failed! ret=%d\n",
			ret);
		return ret;
	}

	vpp_info->smem_start = res.start;
	vpp_info->size = res.end - res.start + 1;

	return ret;
}

static int __sirf_uio_vpp_parse_address(struct platform_device *pdev,
				struct uio_info *info)
{
	struct resource *res;
	int ret = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "cannot find IO resource\n");
		ret = -ENOENT;
		return ret;
	}

	info->mem[0].addr = res->start;
	info->mem[0].size = res->end - res->start + 1;

	return ret;
}

static int sirf_vpp_probe(struct platform_device *pdev)
{
	struct sirf_vpp_info *vpp_info;
	struct uio_info *info;
#ifndef SIRFSOC_VPP_RESERVE_MEM
	unsigned int smem_len;
#endif
	int ret;

	vpp_info = devm_kzalloc(&pdev->dev, sizeof(*vpp_info), GFP_KERNEL);
	if (!vpp_info) {
		ret = -ENOMEM;
		goto err;
	}

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info) {
		ret = -ENOMEM;
		goto err;
	}

	vpp_info->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(vpp_info->clk)) {
		ret = -EINVAL;
		goto err;
	}

#ifdef SIRFSOC_VPP_RESERVE_MEM
	ret = __sirf_uio_vpp_parse_memory(pdev, vpp_info);
	if (ret) {
		ret = -ENOMEM;
		goto err;
	}
#else
	/* allocate two 1920*1088 frame mem which is physical contiguous */
	smem_len = PAGE_ALIGN(1920 * 1088 * 2 * 2);
	vpp_info->base = dma_alloc_coherent(&pdev->dev, smem_len,
		&vpp_info->smem_start, GFP_KERNEL);
	if (!vpp_info->base) {
		ret = -ENOMEM;
		goto err;
	}
#endif
	__sirf_uio_vpp_parse_address(pdev, info);
	info->mem[0].memtype = UIO_MEM_PHYS;
	info->mem[1].addr = vpp_info->smem_start;
	info->mem[1].size = vpp_info->size;
	info->mem[1].memtype = UIO_MEM_PHYS;
	info->name = "sirf_vpp";
	info->version = "0.1";
	info->irq = UIO_IRQ_NONE;

	if (uio_register_device(&pdev->dev, info)) {
		ret = -ENODEV;
		goto err_free;
	}

	vpp_info->uio_info = info;
	platform_set_drvdata(pdev, vpp_info);

	ret = clk_prepare_enable(vpp_info->clk);

	return 0;

err_free:
#ifndef SIRFSOC_VPP_RESERVE_MEM
	dma_free_coherent(&pdev->dev, vpp_info->size,
		vpp_info->base, vpp_info->smem_start);
#endif
err:
	return ret;
}

static int sirf_vpp_remove(struct platform_device *pdev)
{
	struct sirf_vpp_info *vpp_info = platform_get_drvdata(pdev);

	uio_unregister_device(vpp_info->uio_info);

#ifndef SIRFSOC_VPP_RESERVE_MEM
	dma_free_coherent(&pdev->dev, vpp_info->size,
		vpp_info->base, vpp_info->smem_start);
#endif

	clk_disable(vpp_info->clk);
	clk_put(vpp_info->clk);

	return 0;
}

const struct of_device_id sirf_vpp_match_tbl[] = {
	{ .compatible = "sirf,atlas7-vpp", },
	{ /* end */ }
};

static struct platform_driver uio_vpp_driver = {
	.driver = {
		.name = "sirfsoc_vpp",
		.of_match_table = sirf_vpp_match_tbl,
	},
	.probe = sirf_vpp_probe,
	.remove = sirf_vpp_remove,
};

static int __init uio_vpp_init(void)
{
	return platform_driver_register(&uio_vpp_driver);
}

static void __exit uio_vpp_exit(void)
{
	platform_driver_unregister(&uio_vpp_driver);
}

module_init(uio_vpp_init);
module_exit(uio_vpp_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("UIO VPP module driver");
