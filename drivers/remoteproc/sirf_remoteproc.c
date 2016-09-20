/*
 * SIRF Remote processor machine-specific module
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/hwspinlock.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_device.h>
#include <linux/remoteproc.h>

#include "remoteproc_internal.h"

struct fifo_buffer {
	struct hwspinlock *lock;
	unsigned char *buffer;
	u32 w_pos;
	u32 r_pos;
	u32 size;
	u32 *count; /* pointer to shared memory */
};

static int fifo_write(struct fifo_buffer *fifo,
		const void *data, u32 len)
{
	int err;
	u32 overflow, count;
	ulong flags;

	err = hwspin_lock_timeout_irqsave(fifo->lock, 100, &flags);
	if (err) {
		pr_err("%s, Get hwspinlock failed!err= %d\n",
			__func__, err);
		WARN_ON(err);
		return -EBUSY;
	}

	if (len > fifo->size) {
		err = -EFBIG;
		goto err_exit;
	}

	count = *fifo->count;
	overflow = len > (fifo->size - count);
	if (overflow) {
		/* previous data hasn't been read, FIFO busy */
		err = -EBUSY;
		goto err_exit;
	}

	/* copy data to fifo buffer */
	memcpy(fifo->buffer + fifo->w_pos, data, len);
	/* update fifo position */
	fifo->w_pos = (fifo->w_pos + len) % fifo->size;
	*fifo->count = count + len;

	err = 0;

err_exit:
	hwspin_unlock_irqrestore(fifo->lock, &flags);

	return err;
}

static int fifo_read(struct fifo_buffer *fifo,
		void *data, u32 len)
{
	int err;
	u32 count;

	err = hwspin_lock_timeout(fifo->lock, 100);
	if (err) {
		pr_err("%s, Get hwspinlock failed!err= %d\n",
			__func__, err);
			WARN_ON(err);
		return err;
	}

	count = *fifo->count;
	if (!count) {
		err = -ENOSPC;
		goto err_exit;
	}

	if (len > count)
		len = count;

	/* copy data from fifo buffer */
	memcpy(data, fifo->buffer + fifo->r_pos, len);
	/* update fifo position */
	fifo->r_pos = (fifo->r_pos + len) % fifo->size;
	*fifo->count = count - len;

	err = 0;

err_exit:
	hwspin_unlock(fifo->lock);

	return err;
}

static int fifo_init(struct fifo_buffer *fifo, void *buffer,
			int size, int hwlock_id)
{

	fifo->lock = hwspin_lock_request_specific(hwlock_id);
	if (!fifo->lock) {
		pr_err("%s:Could not request specific hwspin lock!\n",
			__func__);
		return -ENODEV;
	}

	fifo->count = (u32 *)buffer;
	fifo->buffer = (unsigned char *)(buffer + sizeof(u32));
	fifo->w_pos = 0;
	fifo->r_pos = 0;
	fifo->size = size - sizeof(u32);
	*fifo->count = 0;

	return 0;
}

/* FIFO shared memory has been divide into 2 logical channels */
#define FIFO_LOGIC_CHN_0	0
#define FIFO_LOGIC_CHN_1	1

#define RPROC_DEF_FIFO_SIZE	0x1000

/**
 * struct sirf_rproc - SIRF remote processor instance state
 * @rproc: rproc handle
 * @rsc_dma: the dma address of the resource memory, include fifo.
 * @rsc_size: resource memory size.
 * @table_ptr: the virtual address of rproc resource table area.
 * @table_len: the length of rproc resource table.
 * @fifo_rx_lock: lock for fifo receive data.
 * @fifo_tx_lock: lock for fifo send data.
 * @tx_avail_wq: wait queue of send data when fifo is busy.
 * @fifo_avail: fifo status for send data.
 *		fifo can send data when fifo_avail is true.
 * @fifo_msg_rx: memory address for fifo arrived data.
 * @fifo_msg_tx: memory address for fifo send data.
 * @fifo_iomemmem: iomem address for fifo register.
 * @irq: the irq number of fifo allocated in backend OS.
 * @irq_gen_count: generate IRQ counter for statistic
 * @irq_get_count: arrive IRQ counter for statistic
 */
struct sirf_rproc {
	struct rproc *rproc;
	void *rsc_dma;
	size_t rsc_size;
	struct resource_table *table_ptr;
	u32 table_len;
	void __iomem *set_reg;
	void __iomem *clr_reg;
	struct fifo_buffer w_fifo;
	struct fifo_buffer r_fifo;
	int w_fifo_hwlock;
	int r_fifo_hwlock;
	int irq;
};

/* Interrupt handler for IRQs from remote processor */
static irqreturn_t sirf_rproc_ipc_isr(int irq, void *data)
{
	struct rproc *rproc = (struct rproc *)data;
	struct sirf_rproc *srproc = (struct sirf_rproc *)rproc->priv;
	u32 notifyid;
	int err;

	/* clear interrupt */
	readl(srproc->clr_reg);

	do {
		err = fifo_read(&srproc->r_fifo, &notifyid, sizeof(notifyid));
		if (err)
			break;
		rproc_vq_interrupt(rproc, notifyid);
	} while (1);

	return IRQ_HANDLED;
}

static void sirf_rproc_kick(struct rproc *rproc, int notify_id)
{
	struct sirf_rproc *srproc = rproc->priv;
	int ret;

	ret = fifo_write(&srproc->w_fifo, &notify_id, sizeof(notify_id));
	if (ret) {
		dev_err(&rproc->dev,
			"%s could not completed, err=%d\n",
			__func__, ret);
		WARN_ON(1);
	}

	/*
	 * Trigger interrupt to ask remote side to get new added data
	 * or handle the data already in the FIFO as fast as possible.
	 */
	smp_mb();

	writel(0x01, srproc->set_reg);
}

static const struct of_device_id sirf_rproc_dt_ids[] = {
	{ .compatible = "sirf,atlas7-rproc", },
	{},
};

static struct rproc_ops sirf_rproc_ops = {
	.kick = sirf_rproc_kick,
};

static struct resource_table *
srproc_fw_find_rsc_table(struct rproc *rproc,
				const struct firmware *fw,
				int *tablesz)
{
	struct sirf_rproc *srproc = (struct sirf_rproc *)rproc->priv;

	*tablesz = srproc->table_len;
	return srproc->table_ptr;
}

struct rproc_fw_ops sirf_rproc_fw_ops = {
	.find_rsc_table = srproc_fw_find_rsc_table,
};

static int sirf_rproc_parse_memory(struct platform_device *pdev,
				struct sirf_rproc *srproc)
{
	struct device_node *m_node;
	struct resource res;
	void *rsc_addr;
	size_t rsc_size;
	int ret;

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

	rsc_addr = (void *)__phys_to_virt(res.start);
	rsc_size = res.end - res.start + 1;

	/* create a coherent mapping */
	srproc->rsc_dma = dma_common_contiguous_remap(virt_to_page(rsc_addr),
				rsc_size, VM_IO,
				pgprot_dmacoherent(PAGE_KERNEL),
				NULL);
	if (!srproc->rsc_dma)
		return -ENOMEM;

	srproc->rsc_size = rsc_size;

	return 0;
}

static int sirf_rproc_parse_args(struct platform_device *pdev,
				struct sirf_rproc *srproc)
{
	void *tx_buffer, *rx_buffer;
	struct resource *res;
	int ret;

	/* retrieve trigger interrupt io base */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	srproc->set_reg = devm_ioremap_resource(&pdev->dev, res);
	if (!srproc->set_reg) {
		dev_err(&pdev->dev,
			"Unable to map rproc trigger interrupt registers!\n");
		return -ENOMEM;
	}

	/* retrieve clear interrupt io base */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	srproc->clr_reg = devm_ioremap_resource(&pdev->dev, res);
	if (!srproc->clr_reg) {
		dev_err(&pdev->dev,
			"Unable to map rproc clear interrupt registers!\n");
		return -ENOMEM;
	}

	ret = of_irq_get(pdev->dev.of_node, 0);
	if (ret == -EPROBE_DEFER) {
		dev_err(&pdev->dev,
			"Unable to find IRQ number. ret=%d\n", ret);
		return ret;
	}
	srproc->irq = ret;

	/* Request hwlocks for rproc */
	srproc->w_fifo_hwlock = of_hwspin_lock_get_id(pdev->dev.of_node, 0);
	if (srproc->w_fifo_hwlock < 0) {
		ret = srproc->w_fifo_hwlock;
		dev_err(&pdev->dev,
			"Unable to get hwlock for write fifo. ret=%d\n", ret);
		goto failed;
	}

	srproc->r_fifo_hwlock = of_hwspin_lock_get_id(pdev->dev.of_node, 1);
	if (srproc->r_fifo_hwlock < 0) {
		ret = srproc->r_fifo_hwlock;
		dev_err(&pdev->dev,
			"Unable to get hwlock for read fifo. ret=%d\n", ret);
		goto failed;
	}

	/* Parse share memory information */
	ret = sirf_rproc_parse_memory(pdev, srproc);
	if (ret) {
		dev_err(&pdev->dev,
			"Unable to setup ipc share memory info. ret=%d\n",
			ret);
		goto failed;
	}
	srproc->table_ptr = srproc->rsc_dma;

	/* check resource table size */
	if (RPROC_DEF_FIFO_SIZE * 2 >= srproc->rsc_size) {
		dev_err(&pdev->dev,
			"There is no memory left for resource table!\n");
		ret = -EINVAL;
		goto free_rsc;
	}

	if (srproc->table_ptr->ver != 1) {
		dev_err(&pdev->dev,
			"unsupported fw ver: %d\n",
			srproc->table_ptr->ver);
		ret = -EINVAL;
		goto free_rsc;
	}

	srproc->table_len = srproc->rsc_size - RPROC_DEF_FIFO_SIZE * 2;
	tx_buffer = srproc->rsc_dma + srproc->table_len +
		RPROC_DEF_FIFO_SIZE * FIFO_LOGIC_CHN_0;
	rx_buffer = srproc->rsc_dma + srproc->table_len +
		RPROC_DEF_FIFO_SIZE * FIFO_LOGIC_CHN_1;

	ret = fifo_init(&srproc->w_fifo, tx_buffer,
			RPROC_DEF_FIFO_SIZE, srproc->w_fifo_hwlock);
	if (ret)
		goto free_rsc;

	ret = fifo_init(&srproc->r_fifo, rx_buffer,
			RPROC_DEF_FIFO_SIZE, srproc->r_fifo_hwlock);
	if (ret)
		goto free_rsc;

	return 0;

free_rsc:
	dma_common_free_remap(srproc->rsc_dma,
			srproc->rsc_size, VM_IO);
	srproc->table_ptr = NULL;
	srproc->rsc_dma = NULL;

failed:
	return ret;
}

static int sirf_rproc_remove(struct platform_device *pdev)
{
	struct rproc *rproc = platform_get_drvdata(pdev);
	struct sirf_rproc *srproc = rproc->priv;

	dma_common_free_remap(srproc->rsc_dma,
				srproc->rsc_size, VM_IO);

	rproc->table_ptr = 0;

	rproc_del(rproc);
	rproc_put(rproc);

	return 0;
}

static int sirf_rproc_probe(struct platform_device *pdev)
{
	struct sirf_rproc *srproc;
	struct rproc *rproc;
	const char *fw;
	int ret;

	ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(&pdev->dev, "dma_set_coherent_mask: %d\n", ret);
		return ret;
	}

	ret = of_property_read_string(pdev->dev.of_node, "firmware", &fw);
	if (ret)
		fw = NULL; /* Set to NULL, rproc core will use default name */

	rproc = rproc_alloc(&pdev->dev, dev_name(&pdev->dev), &sirf_rproc_ops,
				fw, sizeof(*srproc));
	if (!rproc)
		return -ENOMEM;

	srproc = rproc->priv;
	srproc->rproc = rproc;
	/* Setup sirf rproc firmware ops */
	rproc->fw_ops = &sirf_rproc_fw_ops;
	/* This rproc is always on */
	rproc->state = RPROC_ALWAYS_ON;

	ret = sirf_rproc_parse_args(pdev, srproc);
	if (ret)
		goto free_rproc;

	ret = devm_request_threaded_irq(&rproc->dev, srproc->irq,
				NULL, sirf_rproc_ipc_isr,
				IRQF_ONESHOT,
				dev_name(&pdev->dev), rproc);
	if (ret) {
		dev_err(&rproc->dev,
			"request_threaded_irq %d error: %d\n",
			srproc->irq, ret);
		goto free_rproc;
	}

	ret = rproc_add(rproc);
	if (ret) {
		dev_err(&rproc->dev, "rproc_add failed: %d\n", ret);
		goto free_rproc;
	}

	platform_set_drvdata(pdev, rproc);

	return 0;

free_rproc:
	rproc_put(rproc);

	return ret;
}

static struct platform_driver sirf_rproc_driver = {
	.probe = sirf_rproc_probe,
	.remove = sirf_rproc_remove,
	.driver = {
		.name = "sirfsoc_remoteproc",
		.of_match_table = of_match_ptr(sirf_rproc_dt_ids),
	},
};
module_platform_driver(sirf_rproc_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("SIRF Remote Processor driver");
