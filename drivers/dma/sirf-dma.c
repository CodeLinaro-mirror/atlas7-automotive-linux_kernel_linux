/*
 * DMA controller driver for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/module.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>

#include <linux/random.h>

/* Number of DMA Transfer descriptors allocated per channel */
#define SIRFSOC_DMA_DESCRIPTORS	16

/* Macro definitions */
#define SIRFSOC_DMA_CHANNELS	16

struct sirfsoc_dma_desc {
	struct dma_async_tx_descriptor	desc;
	int				error;
	struct list_head		node;
};

struct sirfsoc_dma_chan {
	struct dma_chan			chan;
	struct list_head		free;
	struct list_head		prepared;
	struct list_head		queued;
	struct list_head		active;
	struct list_head		completed;
	dma_cookie_t			completed_cookie;

	/* Lock for this structure */
	spinlock_t			lock;
};

struct sirfsoc_dma {
	struct dma_device		dma;
	struct tasklet_struct		tasklet;
	struct sirfsoc_dma_chan		channels[SIRFSOC_DMA_CHANNELS];
	struct sirfsoc_dma_regs __iomem	*regs;
	int				irq;
};

#define DRV_NAME	"sirf_dma"

/* Convert struct dma_chan to struct sirfsoc_dma_chan */
static inline struct sirfsoc_dma_chan *dma_chan_to_sirfsoc_dma_chan(struct dma_chan *c)
{
	return container_of(c, struct sirfsoc_dma_chan, chan);
}

/* Convert struct dma_chan to struct sirfsoc_dma */
static inline struct sirfsoc_dma *dma_chan_to_sirfsoc_dma(struct dma_chan *c)
{
	struct sirfsoc_dma_chan *mchan = dma_chan_to_sirfsoc_dma_chan(c);
	return container_of(mchan, struct sirfsoc_dma, channels[c->chan_id]);
}

/*
 * Execute all queued DMA descriptors.
 *
 * Following requirements must be met while calling sirfsoc_dma_execute():
 * a) mchan->lock is acquired,
 * b) mchan->active list is empty,
 * c) mchan->queued list contains at least one entry.
 */
static void sirfsoc_dma_execute(struct sirfsoc_dma_chan *mchan)
{
	struct sirfsoc_dma *sdma = dma_chan_to_sirfsoc_dma(&mchan->chan);
	struct sirfsoc_dma_desc *first = NULL;
	struct sirfsoc_dma_desc *prev = NULL;
	struct sirfsoc_dma_desc *mdesc;

	/* Move all queued descriptors to active list */
	list_splice_tail_init(&mchan->queued, &mchan->active);

	/* Chain descriptors into one transaction */
	list_for_each_entry(mdesc, &mchan->active, node) {
		if (!first)
			first = mdesc;

		if (!prev) {
			prev = mdesc;
			continue;
		}
	}
}

static void sirfsoc_dma_irq_process(struct sirfsoc_dma *sdma, u32 is, u32 es, int off)
{
	struct sirfsoc_dma_chan *mchan;
	struct sirfsoc_dma_desc *mdesc;
	u32 status;
	int ch;

	while ((ch = fls(status) - 1) >= 0) {
		status &= ~(1 << ch);
		mchan = &sdma->channels[ch + off];

		spin_lock(&mchan->lock);

		/* Check error status */
		if (es & (1 << ch))
			list_for_each_entry(mdesc, &mchan->active, node)
				mdesc->error = -EIO;

		/* Execute queued descriptors */
		list_splice_tail_init(&mchan->active, &mchan->completed);
		if (!list_empty(&mchan->queued))
			sirfsoc_dma_execute(mchan);

		spin_unlock(&mchan->lock);
	}
}

/* Interrupt handler */
static irqreturn_t sirfsoc_dma_irq(int irq, void *data)
{
	struct sirfsoc_dma *sdma = data;

	sirfsoc_dma_irq_process(sdma, readl(sdma->regs),
					readl(sdma->regs), 0);

	/* Schedule tasklet */
	tasklet_schedule(&sdma->tasklet);

	return IRQ_HANDLED;
}

/* process completed descriptors */
static void sirfsoc_dma_process_completed(struct sirfsoc_dma *sdma)
{
	dma_cookie_t last_cookie = 0;
	struct sirfsoc_dma_chan *mchan;
	struct sirfsoc_dma_desc *mdesc;
	struct dma_async_tx_descriptor *desc;
	unsigned long flags;
	LIST_HEAD(list);
	int i;

	for (i = 0; i < sdma->dma.chancnt; i++) {
		mchan = &sdma->channels[i];

		/* Get all completed descriptors */
		spin_lock_irqsave(&mchan->lock, flags);
		if (!list_empty(&mchan->completed))
			list_splice_tail_init(&mchan->completed, &list);
		spin_unlock_irqrestore(&mchan->lock, flags);

		if (list_empty(&list))
			continue;

		/* Execute callbacks and run dependencies */
		list_for_each_entry(mdesc, &list, node) {
			desc = &mdesc->desc;

			if (desc->callback)
				desc->callback(desc->callback_param);

			last_cookie = desc->cookie;
			dma_run_dependencies(desc);
		}

		/* Free descriptors */
		spin_lock_irqsave(&mchan->lock, flags);
		list_splice_tail_init(&list, &mchan->free);
		mchan->completed_cookie = last_cookie;
		spin_unlock_irqrestore(&mchan->lock, flags);
	}
}

/* DMA Tasklet */
static void sirfsoc_dma_tasklet(unsigned long data)
{
	struct sirfsoc_dma *sdma = (void *)data;
	unsigned long flags;

	sirfsoc_dma_process_completed(sdma);
}

/* Submit descriptor to hardware */
static dma_cookie_t sirfsoc_dma_tx_submit(struct dma_async_tx_descriptor *txd)
{
	struct sirfsoc_dma_chan *mchan = dma_chan_to_sirfsoc_dma_chan(txd->chan);
	struct sirfsoc_dma_desc *mdesc;
	unsigned long flags;
	dma_cookie_t cookie;

	mdesc = container_of(txd, struct sirfsoc_dma_desc, desc);

	spin_lock_irqsave(&mchan->lock, flags);

	/* Move descriptor to queue */
	list_move_tail(&mdesc->node, &mchan->queued);

	/* If channel is idle, execute all queued descriptors */
	if (list_empty(&mchan->active))
		sirfsoc_dma_execute(mchan);

	/* Update cookie */
	cookie = mchan->chan.cookie + 1;
	if (cookie <= 0)
		cookie = 1;

	mchan->chan.cookie = cookie;
	mdesc->desc.cookie = cookie;

	spin_unlock_irqrestore(&mchan->lock, flags);

	return cookie;
}

/* Alloc channel resources */
static int sirfsoc_dma_alloc_chan_resources(struct dma_chan *chan)
{
	struct sirfsoc_dma *sdma = dma_chan_to_sirfsoc_dma(chan);
	struct sirfsoc_dma_chan *mchan = dma_chan_to_sirfsoc_dma_chan(chan);
	struct sirfsoc_dma_desc *mdesc;
	unsigned long flags;
	LIST_HEAD(descs);
	int i;

	/* Alloc descriptors for this channel */
	for (i = 0; i < SIRFSOC_DMA_DESCRIPTORS; i++) {
		mdesc = kzalloc(sizeof(struct sirfsoc_dma_desc), GFP_KERNEL);
		if (!mdesc) {
			dev_notice(sdma->dma.dev, "Memory allocation error. "
					"Allocated only %u descriptors\n", i);
			break;
		}

		dma_async_tx_descriptor_init(&mdesc->desc, chan);
		mdesc->desc.flags = DMA_CTRL_ACK;
		mdesc->desc.tx_submit = sirfsoc_dma_tx_submit;

		list_add_tail(&mdesc->node, &descs);
	}

	/* Return error only if no descriptors were allocated */
	if (i == 0)
		return -ENOMEM;

	spin_lock_irqsave(&mchan->lock, flags);

	list_splice_tail_init(&descs, &mchan->free);
	spin_unlock_irqrestore(&mchan->lock, flags);

	return 0;
}

/* Free channel resources */
static void sirfsoc_dma_free_chan_resources(struct dma_chan *chan)
{
	struct sirfsoc_dma *sdma = dma_chan_to_sirfsoc_dma(chan);
	struct sirfsoc_dma_chan *mchan = dma_chan_to_sirfsoc_dma_chan(chan);
	struct sirfsoc_dma_desc *mdesc, *tmp;
	unsigned long flags;
	LIST_HEAD(descs);

	spin_lock_irqsave(&mchan->lock, flags);

	/* Channel must be idle */
	BUG_ON(!list_empty(&mchan->prepared));
	BUG_ON(!list_empty(&mchan->queued));
	BUG_ON(!list_empty(&mchan->active));
	BUG_ON(!list_empty(&mchan->completed));

	/* Move data */
	list_splice_tail_init(&mchan->free, &descs);

	spin_unlock_irqrestore(&mchan->lock, flags);

	/* Free descriptors */
	list_for_each_entry_safe(mdesc, tmp, &descs, node)
		kfree(mdesc);
}

/* Send all pending descriptor to hardware */
static void sirfsoc_dma_issue_pending(struct dma_chan *chan)
{
	/*
	 * We are posting descriptors to the hardware as soon as
	 * they are ready, so this function does nothing.
	 */
}

/* Check request completion status */
static enum dma_status
sirfsoc_dma_tx_status(struct dma_chan *chan, dma_cookie_t cookie,
	       struct dma_tx_state *txstate)
{
	struct sirfsoc_dma_chan *mchan = dma_chan_to_sirfsoc_dma_chan(chan);
	unsigned long flags;
	dma_cookie_t last_used;
	dma_cookie_t last_complete;

	spin_lock_irqsave(&mchan->lock, flags);
	last_used = mchan->chan.cookie;
	last_complete = mchan->completed_cookie;
	spin_unlock_irqrestore(&mchan->lock, flags);

	dma_set_tx_state(txstate, last_complete, last_used, 0);
	return dma_async_is_complete(cookie, last_complete, last_used);
}

/* Prepare descriptor for memory to memory copy */
static struct dma_async_tx_descriptor *
sirfsoc_dma_prep_memcpy(struct dma_chan *chan, dma_addr_t dst, dma_addr_t src,
					size_t len, unsigned long flags)
{
	struct sirfsoc_dma *sdma = dma_chan_to_sirfsoc_dma(chan);
	struct sirfsoc_dma_chan *mchan = dma_chan_to_sirfsoc_dma_chan(chan);
	struct sirfsoc_dma_desc *mdesc = NULL;
	unsigned long iflags;

	/* Get free descriptor */
	spin_lock_irqsave(&mchan->lock, iflags);
	if (!list_empty(&mchan->free)) {
		mdesc = list_first_entry(&mchan->free, struct sirfsoc_dma_desc,
									node);
		list_del(&mdesc->node);
	}
	spin_unlock_irqrestore(&mchan->lock, iflags);

	if (!mdesc) {
		/* try to free completed descriptors */
		sirfsoc_dma_process_completed(sdma);
		return NULL;
	}

	/* Place descriptor in prepared list */
	spin_lock_irqsave(&mchan->lock, iflags);
	list_add_tail(&mdesc->node, &mchan->prepared);
	spin_unlock_irqrestore(&mchan->lock, iflags);

	return &mdesc->desc;
}

static int __devinit sirfsoc_dma_probe(struct platform_device *op)
{
	struct device_node *dn = op->dev.of_node;
	struct device *dev = &op->dev;
	struct dma_device *dma;
	struct sirfsoc_dma *sdma;
	struct sirfsoc_dma_chan *mchan;
	struct resource res;
	ulong regs_start, regs_size;
	int retval, i;

	sdma = devm_kzalloc(dev, sizeof(struct sirfsoc_dma), GFP_KERNEL);
	if (!sdma) {
		dev_err(dev, "Memory exhausted!\n");
		return -ENOMEM;
	}

	sdma->irq = irq_of_parse_and_map(dn, 0);
	if (sdma->irq == NO_IRQ) {
		dev_err(dev, "Error mapping IRQ!\n");
		return -EINVAL;
	}

	retval = of_address_to_resource(dn, 0, &res);
	if (retval) {
		dev_err(dev, "Error parsing memory region!\n");
		return retval;
	}

	regs_start = res.start;
	regs_size = resource_size(&res);

	if (!devm_request_mem_region(dev, regs_start, regs_size, DRV_NAME)) {
		dev_err(dev, "Error requesting memory region!\n");
		return -EBUSY;
	}

	sdma->regs = devm_ioremap(dev, regs_start, regs_size);
	if (!sdma->regs) {
		dev_err(dev, "Error mapping memory region!\n");
		return -ENOMEM;
	}

	retval = devm_request_irq(dev, sdma->irq, &sirfsoc_dma_irq, 0, DRV_NAME,
									sdma);
	if (retval) {
		dev_err(dev, "Error requesting IRQ!\n");
		return -EINVAL;
	}

	dma = &sdma->dma;
	dma->dev = dev;
	dma->chancnt = SIRFSOC_DMA_CHANNELS;

	dma->device_alloc_chan_resources = sirfsoc_dma_alloc_chan_resources;
	dma->device_free_chan_resources = sirfsoc_dma_free_chan_resources;
	dma->device_issue_pending = sirfsoc_dma_issue_pending;
	dma->device_tx_status = sirfsoc_dma_tx_status;
	dma->device_prep_dma_memcpy = sirfsoc_dma_prep_memcpy;

	INIT_LIST_HEAD(&dma->channels);
	dma_cap_set(DMA_MEMCPY, dma->cap_mask);

	for (i = 0; i < dma->chancnt; i++) {
		mchan = &sdma->channels[i];

		mchan->chan.device = dma;
		mchan->chan.chan_id = i;
		mchan->chan.cookie = 1;
		mchan->completed_cookie = mchan->chan.cookie;

		INIT_LIST_HEAD(&mchan->free);
		INIT_LIST_HEAD(&mchan->prepared);
		INIT_LIST_HEAD(&mchan->queued);
		INIT_LIST_HEAD(&mchan->active);
		INIT_LIST_HEAD(&mchan->completed);

		spin_lock_init(&mchan->lock);
		list_add_tail(&mchan->chan.device_node, &dma->channels);
	}

	tasklet_init(&sdma->tasklet, sirfsoc_dma_tasklet, (unsigned long)sdma);

	/* Register DMA engine */
	dev_set_drvdata(dev, sdma);
	retval = dma_async_device_register(dma);
	if (retval) {
		devm_free_irq(dev, sdma->irq, sdma);
		irq_dispose_mapping(sdma->irq);
	}

	return retval;
}

static int __devexit sirfsoc_dma_remove(struct platform_device *op)
{
	struct device *dev = &op->dev;
	struct sirfsoc_dma *sdma = dev_get_drvdata(dev);

	dma_async_device_unregister(&sdma->dma);
	devm_free_irq(dev, sdma->irq, sdma);
	irq_dispose_mapping(sdma->irq);

	return 0;
}

static struct of_device_id sirfsoc_dma_match[] = {
	{ .compatible = "sirf,prima2-dma", },
	{},
};

static struct platform_driver sirfsoc_dma_driver = {
	.probe		= sirfsoc_dma_probe,
	.remove		= __devexit_p(sirfsoc_dma_remove),
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table	= sirfsoc_dma_match,
	},
};

static int __init sirfsoc_dma_init(void)
{
	return platform_driver_register(&sirfsoc_dma_driver);
}
module_init(sirfsoc_dma_init);

static void __exit sirfsoc_dma_exit(void)
{
	platform_driver_unregister(&sirfsoc_dma_driver);
}
module_exit(sirfsoc_dma_exit);

MODULE_AUTHOR("Rongjun Ying <rongjun.ying@csr.com>, "
	"Barry Song <baohua.song@csr.com>");
MODULE_DESCRIPTION("SIRFSOC pin control driver");
MODULE_LICENSE("GPL");
