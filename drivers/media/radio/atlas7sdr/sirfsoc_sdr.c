/*
 * CSR SDR Accelerator driver
 *
 * Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
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

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/dma-direction.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <asm/cacheflush.h>

#include "sirfsoc_sdr.h"

#define DRV_NAME "sirf_sdr"
#define MAX_SG_COUNT 256

struct dma_info {
	dma_addr_t dma_addr;
	unsigned long *dma_virt_addr;
	int len;
	struct list_head list;
};

struct dma_chain_entry {
	int data_len : 25;
	int flag : 7;
	int address : 32;
};

struct sirf_sdr {
	unsigned long		sram_phy;
	resource_size_t		sram_size;
	struct clk		*clk;
	struct platform_device	*pdev;
	struct dma_info		rd_dma_info;
	struct dma_info		wt_dma_info;
	struct dma_chan		*tx_dma_chan;
	struct dma_chan		*rx_dma_chan;
	struct scatterlist	*rd_sg;
	struct scatterlist	*wt_sg;
	struct completion	data_ready;
	struct miscdevice	misc_sdr;
	void __iomem		*regbase;
};

static int sirf_sdr_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int sirf_sdr_release(struct inode *inode, struct file *filp)
{
	struct sirf_sdr *sdr = container_of(filp->private_data,
			struct sirf_sdr, misc_sdr);
	struct dma_info *pos, *n;

	list_for_each_entry_safe(pos, n, &sdr->rd_dma_info.list, list) {
		list_del(&pos->list);
		if (pos->dma_addr)
			dma_free_coherent(&sdr->pdev->dev, pos->len,
					pos->dma_virt_addr, pos->dma_addr);
	}

	list_for_each_entry_safe(pos, n, &sdr->wt_dma_info.list, list) {
		list_del(&pos->list);
		if (pos->dma_addr)
			dma_free_coherent(&sdr->pdev->dev, pos->len,
					pos->dma_virt_addr, pos->dma_addr);
	}
	return 0;
}

static int sirf_sdr_mmap(struct file *filp, struct vm_area_struct *vma)
{
	unsigned long off;
	struct dma_info *pos, *n;
	struct sirf_sdr *sdr = container_of(filp->private_data,
			struct sirf_sdr, misc_sdr);
	off = sdr->sram_phy >> PAGE_SHIFT;

	/* There are 3 types buffers to map:
	   1. SDRAM
	   2. input dma buffer
	   3. output dma buffer
	   The application get the info by IOCTL_ALLOC_INPUT_BUFFER and
	   IOCTL_ALLOC_OUTPUT_BUFFER, and set the offset by physical address
	   when mmap, the driver search the offset in saved address and map
	   it if found it.
	*/
	if (vma->vm_pgoff == off &&
			vma->vm_end - vma->vm_start == sdr->sram_size)
		goto do_map;

	list_for_each_entry_safe(pos, n, &sdr->rd_dma_info.list, list) {
		off = pos->dma_addr >> PAGE_SHIFT;
		if (off == vma->vm_pgoff)
			goto do_map;
	}
	list_for_each_entry_safe(pos, n, &sdr->wt_dma_info.list, list) {
		off = pos->dma_addr >> PAGE_SHIFT;
		if (off == vma->vm_pgoff)
			goto do_map;
	}

	return -EINVAL;

do_map:
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	return remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
				vma->vm_end - vma->vm_start,
				vma->vm_page_prot);
}

static void sdr_dma_rx_callback(void *data)
{
	struct sirf_sdr *sdr = (struct sirf_sdr *)data;

	complete(&sdr->data_ready);
}

static int sdr_prepare_rd_chain_sg(struct sirf_sdr *sdr,
		struct config_info *info)
{
	int count;
	int i;
	struct dma_chain_entry *pchain_entry = NULL;
	struct dma_info *pos, *n;

	count = info->rd_dma_entry_cnt;
	if (count > MAX_SG_COUNT)
		return -ENOMEM;

	sg_init_table(sdr->rd_sg, count);

	list_for_each_entry_safe(pos, n, &sdr->rd_dma_info.list, list) {
		if (info->rd_dma_addr >= pos->dma_addr &&
				info->rd_dma_addr < pos->dma_addr + pos->len) {
			pchain_entry = (struct dma_chain_entry *)(
					pos->dma_virt_addr +
					(pos->dma_addr - info->rd_dma_addr));
		}
	}
	if (!pchain_entry)
		return -EINVAL;
	for (i = 0; i < count; i++) {
		sg_dma_address(&sdr->rd_sg[i]) = pchain_entry[i].address;
		sg_dma_len(&sdr->rd_sg[i]) = pchain_entry[i].data_len;
	}

	return 0;
}

static int sdr_prepare_wt_chain_sg(struct sirf_sdr *sdr,
		struct config_info *info)
{
	int count;
	int i;
	struct dma_chain_entry *pchain_entry = NULL;
	struct dma_info *pos, *n;

	count = info->wt_dma_entry_cnt;
	if (count > MAX_SG_COUNT)
		return -ENOMEM;

	sg_init_table(sdr->wt_sg, count);
	pchain_entry = NULL;
	list_for_each_entry_safe(pos, n, &sdr->wt_dma_info.list, list) {
		if (info->wt_dma_addr >= pos->dma_addr &&
				info->wt_dma_addr < pos->dma_addr + pos->len) {
			pchain_entry = (struct dma_chain_entry *)(
					pos->dma_virt_addr +
					(pos->dma_addr - info->wt_dma_addr));
		}
	}
	if (!pchain_entry)
		return -EINVAL;

	for (i = 0; i < count; i++) {
		sg_dma_address(&sdr->wt_sg[i]) = pchain_entry[i].address;
		sg_dma_len(&sdr->wt_sg[i]) = pchain_entry[i].data_len;
	}
	return 0;
}

static int sdr_dmaengine_start_dma(struct sirf_sdr *sdr,
		struct config_info *info)
{
	int ret;
	int count;
	struct dma_async_tx_descriptor *desc;

	if (info->rd_dma_chain) {
		ret = sdr_prepare_rd_chain_sg(sdr, info);
		if (ret)
			return ret;
		count = info->rd_dma_entry_cnt;
		desc = dmaengine_prep_slave_sg(sdr->tx_dma_chan, sdr->rd_sg,
				count,
				DMA_MEM_TO_DEV,
				DMA_PREP_INTERRUPT | DMA_CTRL_ACK);

	} else {
		desc = dmaengine_prep_slave_single(sdr->tx_dma_chan,
				info->rd_dma_addr,
				info->rd_dma_size,
				DMA_MEM_TO_DEV,
				DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	}
	if (IS_ERR(desc)) {
		ret = PTR_ERR(desc);
		goto tx_fail;
	}
	dmaengine_submit(desc);

	if (info->wt_dma_chain) {
		ret = sdr_prepare_wt_chain_sg(sdr, info);

		if (ret)
			return ret;
		count = info->wt_dma_entry_cnt;
		desc = dmaengine_prep_slave_sg(sdr->rx_dma_chan, sdr->wt_sg,
				count,
				DMA_DEV_TO_MEM,
				DMA_PREP_INTERRUPT | DMA_CTRL_ACK);

	} else {
		desc = dmaengine_prep_slave_single(sdr->rx_dma_chan,
				info->wt_dma_addr,
				info->wt_dma_size,
				DMA_DEV_TO_MEM,
				DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	}

	if (IS_ERR(desc)) {
		dev_err(&sdr->pdev->dev, "prepare rx chan error\n");
		ret = PTR_ERR(desc);
		goto rx_fail;
	}

	desc->callback = sdr_dma_rx_callback;
	desc->callback_param = sdr;
	dmaengine_submit(desc);

	dma_async_issue_pending(sdr->tx_dma_chan);
	dma_async_issue_pending(sdr->rx_dma_chan);
	return 0;
tx_fail:
rx_fail:
	dev_err(&sdr->pdev->dev, "prepare han error\n");
	return ret;
}

static long sdr_ioctl_decoder(struct sirf_sdr *sdr,
		struct config_info *info)
{
	int ret;
	int wait;
	struct config_info cfg_info;
	struct platform_device *pdev;

	pdev = sdr->pdev;

	if (copy_from_user(&cfg_info,
				(void __user *)info,
				sizeof(struct config_info))) {
		dev_err(&pdev->dev, "sdr: copy buf_info fail\n");
		ret = -EINVAL;
		goto out;
	}

	ret = sdr_dmaengine_start_dma(sdr, &cfg_info);

	if (ret)
		goto err;

	wait = wait_for_completion_interruptible_timeout(&sdr->data_ready,
			msecs_to_jiffies(100));
	reinit_completion(&sdr->data_ready);

	if (!wait)
		ret = -ETIMEDOUT;
	else if (wait < 0)
		ret = -EINTR;

	if (ret)
		goto err;

	return 0;
err:
	dmaengine_terminate_all(sdr->tx_dma_chan);
	dmaengine_terminate_all(sdr->rx_dma_chan);
out:
	return ret;
}

static long sdr_alloc_in_dma_buf(struct sirf_sdr *sdr,
		unsigned long in_buf_info)
{
	struct platform_device *pdev;
	struct sdr_buf_info buf_info;
	struct dma_info *pnode;
	long ret;

	pdev = sdr->pdev;

	if (copy_from_user(&buf_info, (void __user *)in_buf_info,
				sizeof(struct sdr_buf_info))) {
		dev_err(&pdev->dev, "sdr: copy buf_info fail\n");
		return -EINVAL;
	}
	pnode = kzalloc(sizeof(*pnode), GFP_KERNEL);
	if (!pnode)
		return -ENOMEM;

	pnode->dma_virt_addr = dma_alloc_coherent(&pdev->dev,
			buf_info.size,
			&pnode->dma_addr,
			GFP_KERNEL);
	if (!pnode->dma_virt_addr) {
		ret = -ENOMEM;
		goto alloc_dma_fail;
	}
	pnode->len = buf_info.size;
	list_add_tail(&pnode->list, &sdr->rd_dma_info.list);

	return pnode->dma_addr;
alloc_dma_fail:
	kfree(pnode);
	return ret;
}

static long sdr_alloc_out_dma_buf(struct sirf_sdr *sdr,
		unsigned long out_buf_info)
{
	struct platform_device *pdev;
	struct sdr_buf_info buf_info;
	struct dma_info *pnode;
	long ret;

	pdev = sdr->pdev;

	if (copy_from_user(&buf_info,
				(void __user *)out_buf_info,
				sizeof(struct sdr_buf_info))) {
		dev_err(&pdev->dev, "sdr: copy buf_info fail\n");
		return -EINVAL;
	}
	pnode = kzalloc(sizeof(*pnode), GFP_KERNEL);
	if (!pnode)
		return -ENOMEM;
	pnode->dma_virt_addr = dma_alloc_coherent(&pdev->dev,
			buf_info.size,
			&pnode->dma_addr,
			GFP_KERNEL);
	if (!pnode->dma_virt_addr) {
		ret = -ENOMEM;
		goto alloc_dma_fail;
	}
	pnode->len = buf_info.size;
	list_add_tail(&pnode->list, &sdr->wt_dma_info.list);
	return pnode->dma_addr;
alloc_dma_fail:
	kfree(pnode);
	return ret;
}

static long sdr_free_in_dma_buf(struct sirf_sdr *sdr, unsigned int dma_addr)
{
	struct platform_device *pdev;
	struct dma_info *pos, *n;
	long ret = -EINVAL;

	pdev = sdr->pdev;

	list_for_each_entry_safe(pos, n, &sdr->rd_dma_info.list, list) {
		if (pos->dma_addr == dma_addr) {
			dma_free_coherent(&pdev->dev,
					pos->len,
					pos->dma_virt_addr,
					pos->dma_addr);
			list_del(&pos->list);
			ret = 0;
			break;
		}
	}
	return ret;

}

static long sdr_free_out_dma_buf(struct sirf_sdr *sdr, unsigned int dma_addr)
{
	struct platform_device *pdev;
	struct dma_info *pos, *n;
	long ret = -EINVAL;

	pdev = sdr->pdev;

	list_for_each_entry_safe(pos, n, &sdr->wt_dma_info.list, list) {
		if (pos->dma_addr == dma_addr) {
			dma_free_coherent(&pdev->dev,
					pos->len,
					pos->dma_virt_addr,
					pos->dma_addr);
			list_del(&pos->list);
			ret = 0;
			break;
		}
	}
	return ret;

}

static long sdr_reset(struct sirf_sdr *sdr)
{
	int ret = 0;

	ret = device_reset(&sdr->pdev->dev);
	if (ret)
		dev_crit(&(sdr->pdev->dev), "Error device reset\n");

	writel(1, sdr->regbase + SDR_VSS_DEBUG_RESET);

	return 0;
}

static long
sirf_sdr_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int ret;
	struct sirf_sdr *sdr =
		container_of(filp->private_data, struct sirf_sdr, misc_sdr);

	switch (cmd) {
	case IOCTL_ALLOC_INPUT_BUFFER:
		ret = sdr_alloc_in_dma_buf(sdr, arg);
		break;
	case IOCTL_ALLOC_OUTPUT_BUFFER:
		ret = sdr_alloc_out_dma_buf(sdr, arg);
		break;
	case IOCTL_DECODER:
		ret = sdr_ioctl_decoder(sdr, (struct config_info *)arg);
		break;
	case IOCTL_FREE_INPUT_BUFFER:
		ret = sdr_free_in_dma_buf(sdr, arg);
		break;
	case IOCTL_FREE_OUTPUT_BUFFER:
		ret = sdr_free_out_dma_buf(sdr, arg);
		break;
	case IOCTL_SDR_RESET:
		ret = sdr_reset(sdr);
		break;
	default:
		return -EINVAL;
	}
	return ret;
}

static const struct file_operations sirfsdr_fops = {
	.unlocked_ioctl = sirf_sdr_ioctl,
	.open = sirf_sdr_open,
	.release = sirf_sdr_release,
	.mmap = sirf_sdr_mmap,
};

#ifdef CONFIG_PM_SLEEP
static int sirfsoc_sdr_pm_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sirf_sdr *sdr = platform_get_drvdata(pdev);

	clk_disable_unprepare(sdr->clk);

	return 0;
}

static int sirfsoc_sdr_pm_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sirf_sdr *sdr = platform_get_drvdata(pdev);

	return clk_prepare_enable(sdr->clk);
}

#endif

static SIMPLE_DEV_PM_OPS(sirfsoc_sdr_pm_ops, sirfsoc_sdr_pm_suspend,
		sirfsoc_sdr_pm_resume);

static int sdr_sirf_probe(struct platform_device *pdev)
{
	int ret;
	struct sirf_sdr *sdr;
	struct device *dev = &pdev->dev;
	struct device_node *dp;
	struct resource res;
	struct resource *res_io;
	struct dma_slave_config tx_slv_cfg = {
		.dst_maxburst = 2,
	};
	struct dma_slave_config rx_slv_cfg = {
		.src_maxburst = 2,
	};

	sdr = devm_kzalloc(&pdev->dev, sizeof(*sdr), GFP_KERNEL);
	if (!sdr)
		return -ENOMEM;

	sdr->pdev = pdev;
	platform_set_drvdata(pdev, sdr);
	init_completion(&sdr->data_ready);

	sdr->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(sdr->clk)) {
		dev_crit(dev, "failed to get a clock.\n");
		return PTR_ERR(sdr->clk);
	}

	ret = clk_prepare_enable(sdr->clk);
	if (ret) {
		dev_crit(dev, "Error enable clock\n");
		return ret;
	}

	ret = device_reset(&pdev->dev);
	if (ret)
		dev_crit(dev, "Error device_reset\n");

	dp = of_find_node_by_name(NULL, "sdrsram");
	if (!dp)
		return -EINVAL;
	ret = of_address_to_resource(dp, 0, &res);
	if (ret)
		return ret;
	sdr->sram_phy = res.start;
	sdr->sram_size = resource_size(&res);
	res_io = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	sdr->regbase = devm_ioremap_resource(&pdev->dev, res_io);
	if (!sdr->regbase) {
		ret = -ENOMEM;
		goto tx_dma_fail;
	}

	sdr_reset(sdr);
	sdr->tx_dma_chan = dma_request_slave_channel(&pdev->dev, "tx");
	if (!sdr->tx_dma_chan) {
		dev_err(&pdev->dev, "sdr: request write dma failed\n");
		ret = -ENODEV;
		goto tx_dma_fail;
	}
	sdr->rx_dma_chan = dma_request_slave_channel(&pdev->dev, "rx");
	if (!sdr->rx_dma_chan) {
		dev_err(&pdev->dev, "sdr: request read dma failed\n");
		ret = -ENODEV;
		goto rx_dma_fail;
	}
	dmaengine_slave_config(sdr->tx_dma_chan, &tx_slv_cfg);
	dmaengine_slave_config(sdr->rx_dma_chan, &rx_slv_cfg);

	INIT_LIST_HEAD(&sdr->rd_dma_info.list);
	INIT_LIST_HEAD(&sdr->wt_dma_info.list);
	sdr->rd_sg = devm_kcalloc(&pdev->dev,
			MAX_SG_COUNT,
			sizeof(struct scatterlist),
			GFP_KERNEL);
	if (!sdr->rd_sg) {
		ret = -ENOMEM;
		goto fail_do_release;
	}

	sdr->wt_sg = devm_kcalloc(&pdev->dev,
			MAX_SG_COUNT,
			sizeof(struct scatterlist),
			GFP_KERNEL);
	if (!sdr->wt_sg) {
		ret = -ENOMEM;
		goto fail_do_release;
	}
	sdr->misc_sdr.name = DRV_NAME;
	sdr->misc_sdr.fops = &sirfsdr_fops;
	sdr->misc_sdr.minor = MISC_DYNAMIC_MINOR;
	ret = misc_register(&sdr->misc_sdr);
	if (unlikely(ret)) {
		dev_err(&pdev->dev, "misc register fail\n");
		goto fail_do_release;
	}
	return 0;
fail_do_release:
	dma_release_channel(sdr->rx_dma_chan);
rx_dma_fail:
	dma_release_channel(sdr->tx_dma_chan);
tx_dma_fail:
	clk_disable_unprepare(sdr->clk);
	return ret;
}

static int sdr_sirf_remove(struct platform_device *pdev)
{
	struct sirf_sdr *sdr;
	struct dma_info *pos, *n;

	sdr = platform_get_drvdata(pdev);
	dma_release_channel(sdr->tx_dma_chan);
	dma_release_channel(sdr->rx_dma_chan);

	list_for_each_entry_safe(pos, n, &sdr->rd_dma_info.list, list) {
		list_del(&pos->list);
		if (pos->dma_addr)
			dma_free_coherent(&sdr->pdev->dev, pos->len,
					pos->dma_virt_addr, pos->dma_addr);
	}

	list_for_each_entry_safe(pos, n, &sdr->wt_dma_info.list, list) {
		list_del(&pos->list);
		if (pos->dma_addr)
			dma_free_coherent(&sdr->pdev->dev, pos->len,
					pos->dma_virt_addr, pos->dma_addr);
	}
	misc_deregister(&sdr->misc_sdr);
	return 0;
}

static const struct of_device_id sirf_sdr_of_match[] = {
	{ .compatible = "sirf,atlas7-sdr",},
	{}
};
MODULE_DEVICE_TABLE(of, sirf_sdr_of_match);

static struct platform_driver sdr_sirf_driver = {
	.driver		= {
		.name	= "sdr",
		.owner	= THIS_MODULE,
		.of_match_table = sirf_sdr_of_match,
		.pm = &sirfsoc_sdr_pm_ops,
	},
	.probe		= sdr_sirf_probe,
	.remove		= sdr_sirf_remove,
};

module_platform_driver(sdr_sirf_driver);

MODULE_DESCRIPTION("SDR driver for SiRFAtlas7DA");
MODULE_LICENSE("GPL v2");
