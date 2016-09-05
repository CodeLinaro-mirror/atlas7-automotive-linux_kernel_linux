/*
 * DMA controller driver for CSR SiRFprimaII
 *
 * Copyright (c) 2011-2016, The Linux Foundation. All rights reserved.
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

#include <linux/module.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/clk.h>
#include <linux/of_dma.h>
#include <linux/sirfsoc_dma.h>
#include <asm/cacheflush.h>

#include "dmaengine.h"

#define SIRFSOC_DMA_VER_A7V1                    1
#define SIRFSOC_DMA_VER_A7V2                    2
#define SIRFSOC_DMA_VER_A6                      4

#define SIRFSOC_DMA_DESCRIPTORS                 16
#define SIRFSOC_DMA_CHANNELS                    16
#define SIRFSOC_DMA_TABLE_NUM                   256

#define SIRFSOC_DMA_CH_ADDR                     0x00
#define SIRFSOC_DMA_CH_XLEN                     0x04
#define SIRFSOC_DMA_CH_YLEN                     0x08
#define SIRFSOC_DMA_CH_CTRL                     0x0C

#define SIRFSOC_DMA_WIDTH_0                     0x100
#define SIRFSOC_DMA_CH_VALID                    0x140
#define SIRFSOC_DMA_CH_INT                      0x144
#define SIRFSOC_DMA_INT_EN                      0x148
#define SIRFSOC_DMA_INT_EN_CLR                  0x14C
#define SIRFSOC_DMA_CH_LOOP_CTRL                0x150
#define SIRFSOC_DMA_CH_LOOP_CTRL_CLR            0x154
#define SIRFSOC_DMA_WIDTH_ATLAS7                0x10
#define SIRFSOC_DMA_VALID_ATLAS7                0x14
#define SIRFSOC_DMA_INT_ATLAS7                  0x18
#define SIRFSOC_DMA_INT_EN_ATLAS7               0x1c
#define SIRFSOC_DMA_LOOP_CTRL_ATLAS7            0x20
#define SIRFSOC_DMA_CUR_DATA_ADDR               0x34
#define SIRFSOC_DMA_MUL_ATLAS7                  0x38
#define SIRFSOC_DMA_CH_LOOP_CTRL_ATLAS7         0x158
#define SIRFSOC_DMA_CH_LOOP_CTRL_CLR_ATLAS7     0x15C
#define SIRFSOC_DMA_INT_OWNER_REG0		0x164
#define SIRFSOC_DMA_INT_OWNER_REG1		0x168
#define SIRFSOC_OWNER_MASK			0x7
#define SIRFSOC_DMA_IOBG_SCMD_EN		0x800
#define SIRFSOC_DMA_EARLY_RESP_SET		0x818
#define SIRFSOC_DMA_EARLY_RESP_CLR		0x81C

#define SIRFSOC_DMA_MODE_CTRL_BIT               4
#define SIRFSOC_DMA_DIR_CTRL_BIT                5
#define SIRFSOC_DMA_MODE_CTRL_BIT_ATLAS7        2
#define SIRFSOC_DMA_CHAIN_CTRL_BIT_ATLAS7       3
#define SIRFSOC_DMA_DIR_CTRL_BIT_ATLAS7         4
#define SIRFSOC_DMA_TAB_NUM_ATLAS7              7
#define SIRFSOC_DMA_CHAIN_INT_BIT_ATLAS7        5
#define SIRFSOC_DMA_CHAIN_FLAG_SHIFT_ATLAS7     25
#define SIRFSOC_DMA_CHAIN_ADDR_SHIFT            32

#define SIRFSOC_DMA_INT_FINI_INT_ATLAS7         BIT(0)
#define SIRFSOC_DMA_INT_CNT_INT_ATLAS7          BIT(1)
#define SIRFSOC_DMA_INT_PAU_INT_ATLAS7          BIT(2)
#define SIRFSOC_DMA_INT_LOOP_INT_ATLAS7         BIT(3)
#define SIRFSOC_DMA_INT_INV_INT_ATLAS7          BIT(4)
#define SIRFSOC_DMA_INT_END_INT_ATLAS7          BIT(5)
#define SIRFSOC_DMA_INT_ALL_ATLAS7              0x3F

/* xlen and dma_width register is in 4 bytes boundary */
#define SIRFSOC_DMA_WORD_LEN			4
#define SIRFSOC_DMA_XLEN_MAX_V1         0x800
#define SIRFSOC_DMA_XLEN_MAX_V2         0x1000

struct sirfsoc_dma_desc {
	struct dma_async_tx_descriptor	desc;
	struct list_head		node;

	/* SiRFprimaII 2D-DMA parameters */

	int             xlen;           /* DMA xlen */
	int             ylen;           /* DMA ylen */
	int             width;          /* DMA width */
	int             dir;
	bool            cyclic;         /* is loop DMA? */
	bool            chain;          /* is chain DMA? */
	u32             addr;		/* DMA buffer address */
	u64 chain_table[SIRFSOC_DMA_TABLE_NUM]; /* chain tbl */
};

struct sirfsoc_dma_chan {
	struct dma_chan			chan;
	struct list_head		free;
	struct list_head		prepared;
	struct list_head		queued;
	struct list_head		active;
	struct list_head		completed;
	unsigned long			happened_cyclic;
	unsigned long			completed_cyclic;

	/* Lock for this structure */
	spinlock_t			lock;

	int				mode;
};

struct sirfsoc_dma_regs {
	u32				ctrl[SIRFSOC_DMA_CHANNELS];
	u32				interrupt_en;
	u32				owener_reg0;
	u32				owener_reg1;
};

struct sirfsoc_dma {
	struct dma_device		dma;
	dma_cap_mask_t			cap;
	struct tasklet_struct		tasklet;
	struct sirfsoc_dma_chan		channels[SIRFSOC_DMA_CHANNELS];
	void __iomem			*base;
	int				irq;
	struct clk			*clk;
	int				type;
	void (*exec_desc)(struct sirfsoc_dma_desc *sdesc,
		int cid, int burst_mode, void __iomem *base);
	struct sirfsoc_dma_regs		regs_save;
	u32				owned;
};

struct sirfsoc_dmadata {
	void (*exec)(struct sirfsoc_dma_desc *sdesc,
		int cid, int burst_mode, void __iomem *base);
	int type;
};

enum sirfsoc_dma_chain_flag {
	SIRFSOC_DMA_CHAIN_NORMAL = 0x01,
	SIRFSOC_DMA_CHAIN_PAUSE = 0x02,
	SIRFSOC_DMA_CHAIN_LOOP = 0x03,
	SIRFSOC_DMA_CHAIN_END = 0x04
};

#define SIRFSOC_DMA_OWNER_A7	0x0
#define SIRFSOC_DMA_OWNER_KAS	0x03
static u32 sirfsoc_owner_regs[] = {
	SIRFSOC_DMA_INT_OWNER_REG0,
	SIRFSOC_DMA_INT_OWNER_REG1
};

#define DRV_NAME	"sirfsoc_dma"

static int sirfsoc_dma_runtime_suspend(struct device *dev);

/* Convert struct dma_chan to struct sirfsoc_dma_chan */
static inline
struct sirfsoc_dma_chan *dma_chan_to_sirfsoc_dma_chan(struct dma_chan *c)
{
	return container_of(c, struct sirfsoc_dma_chan, chan);
}

/* Convert struct dma_chan to struct sirfsoc_dma */
static inline struct sirfsoc_dma *dma_chan_to_sirfsoc_dma(struct dma_chan *c)
{
	struct sirfsoc_dma_chan *schan = dma_chan_to_sirfsoc_dma_chan(c);
	return container_of(schan, struct sirfsoc_dma, channels[c->chan_id]);
}

static void sirfsoc_dma_execute_hw_a7v2(struct sirfsoc_dma_desc *sdesc,
		int cid, int burst_mode, void __iomem *base)
{
	if (sdesc->chain) {
		/* DMA v2 HW chain mode */
		writel_relaxed((sdesc->dir << SIRFSOC_DMA_DIR_CTRL_BIT_ATLAS7) |
			       (sdesc->chain <<
				SIRFSOC_DMA_CHAIN_CTRL_BIT_ATLAS7) |
			       (0x8 << SIRFSOC_DMA_TAB_NUM_ATLAS7) | 0x3,
			       base + SIRFSOC_DMA_CH_CTRL);
	} else {
		/* DMA v2 legacy mode */
		writel_relaxed(sdesc->xlen, base + SIRFSOC_DMA_CH_XLEN);
		writel_relaxed(sdesc->ylen, base + SIRFSOC_DMA_CH_YLEN);
		writel_relaxed(sdesc->width, base + SIRFSOC_DMA_WIDTH_ATLAS7);
		writel_relaxed((sdesc->width*((sdesc->ylen+1)>>1)),
				base + SIRFSOC_DMA_MUL_ATLAS7);
		writel_relaxed((sdesc->dir << SIRFSOC_DMA_DIR_CTRL_BIT_ATLAS7) |
			       (sdesc->chain <<
				SIRFSOC_DMA_CHAIN_CTRL_BIT_ATLAS7) |
			       0x3, base + SIRFSOC_DMA_CH_CTRL);
	}
	writel_relaxed(sdesc->chain ? SIRFSOC_DMA_INT_END_INT_ATLAS7 :
		       (SIRFSOC_DMA_INT_FINI_INT_ATLAS7 |
			SIRFSOC_DMA_INT_LOOP_INT_ATLAS7),
		       base + SIRFSOC_DMA_INT_EN_ATLAS7);
	writel(sdesc->addr, base + SIRFSOC_DMA_CH_ADDR);
	if (sdesc->cyclic)
		writel(0x10001, base + SIRFSOC_DMA_LOOP_CTRL_ATLAS7);
}

static void sirfsoc_dma_execute_hw_a7v1(struct sirfsoc_dma_desc *sdesc,
		int cid, int burst_mode, void __iomem *base)
{
	writel_relaxed(1, base + SIRFSOC_DMA_IOBG_SCMD_EN);
	writel_relaxed((1 << cid), base + SIRFSOC_DMA_EARLY_RESP_SET);
	writel_relaxed(sdesc->width, base + SIRFSOC_DMA_WIDTH_0 + cid * 4);
	writel_relaxed(cid | (burst_mode << SIRFSOC_DMA_MODE_CTRL_BIT) |
		       (sdesc->dir << SIRFSOC_DMA_DIR_CTRL_BIT),
		       base + cid * 0x10 + SIRFSOC_DMA_CH_CTRL);
	writel_relaxed(sdesc->xlen, base + cid * 0x10 + SIRFSOC_DMA_CH_XLEN);
	writel_relaxed(sdesc->ylen, base + cid * 0x10 + SIRFSOC_DMA_CH_YLEN);
	writel_relaxed(readl_relaxed(base + SIRFSOC_DMA_INT_EN) |
		       (1 << cid), base + SIRFSOC_DMA_INT_EN);
	writel(sdesc->addr >> 2, base + cid * 0x10 + SIRFSOC_DMA_CH_ADDR);
	if (sdesc->cyclic) {
		writel((1 << cid) | 1 << (cid + 16) |
		       readl_relaxed(base + SIRFSOC_DMA_CH_LOOP_CTRL_ATLAS7),
		       base + SIRFSOC_DMA_CH_LOOP_CTRL_ATLAS7);
	}

}

static void sirfsoc_dma_execute_hw_a6(struct sirfsoc_dma_desc *sdesc,
		int cid, int burst_mode, void __iomem *base)
{
	writel_relaxed(sdesc->width, base + SIRFSOC_DMA_WIDTH_0 + cid * 4);
	writel_relaxed(cid | (burst_mode << SIRFSOC_DMA_MODE_CTRL_BIT) |
		       (sdesc->dir << SIRFSOC_DMA_DIR_CTRL_BIT),
		       base + cid * 0x10 + SIRFSOC_DMA_CH_CTRL);
	writel_relaxed(sdesc->xlen, base + cid * 0x10 + SIRFSOC_DMA_CH_XLEN);
	writel_relaxed(sdesc->ylen, base + cid * 0x10 + SIRFSOC_DMA_CH_YLEN);
	writel_relaxed(readl_relaxed(base + SIRFSOC_DMA_INT_EN) |
		       (1 << cid), base + SIRFSOC_DMA_INT_EN);
	writel(sdesc->addr >> 2, base + cid * 0x10 + SIRFSOC_DMA_CH_ADDR);
	if (sdesc->cyclic) {
		writel((1 << cid) | 1 << (cid + 16) |
		       readl_relaxed(base + SIRFSOC_DMA_CH_LOOP_CTRL),
		       base + SIRFSOC_DMA_CH_LOOP_CTRL);
	}

}

/* Execute all queued DMA descriptors */
static void sirfsoc_dma_execute(struct sirfsoc_dma_chan *schan)
{
	struct sirfsoc_dma *sdma = dma_chan_to_sirfsoc_dma(&schan->chan);
	int cid = schan->chan.chan_id;
	struct sirfsoc_dma_desc *sdesc = NULL;
	void __iomem *base;

	/*
	 * lock has been held by functions calling this, so we don't hold
	 * lock again
	 */
	base = sdma->base;
	sdesc = list_first_entry(&schan->queued, struct sirfsoc_dma_desc,
				 node);
	/* Move the first queued descriptor to active list */
	list_move_tail(&sdesc->node, &schan->active);

	if (sdma->type == SIRFSOC_DMA_VER_A7V2)
		cid = 0;

	/* Start the DMA transfer */
	sdma->exec_desc(sdesc, cid, schan->mode, base);

	if (sdesc->cyclic)
		schan->happened_cyclic = schan->completed_cyclic = 0;
}

/* Interrupt handler */
static irqreturn_t sirfsoc_dma_irq(int irq, void *data)
{
	struct sirfsoc_dma *sdma = data;
	struct sirfsoc_dma_chan *schan;
	struct sirfsoc_dma_desc *sdesc = NULL;
	u32 is;
	bool chain;
	int ch;
	void __iomem *reg;

	switch (sdma->type) {
	case SIRFSOC_DMA_VER_A6:
	case SIRFSOC_DMA_VER_A7V1:
		is = readl(sdma->base + SIRFSOC_DMA_CH_INT) & sdma->owned;
		reg = sdma->base + SIRFSOC_DMA_CH_INT;
		while ((ch = fls(is) - 1) >= 0) {
			is &= ~(1 << ch);
			writel_relaxed(1 << ch, reg);
			schan = &sdma->channels[ch];
			spin_lock(&schan->lock);
			sdesc = list_first_entry_or_null(&schan->active,
						 struct sirfsoc_dma_desc, node);
			if (!sdesc)
				goto empty_list1;
			if (!sdesc->cyclic) {
				/* Execute queued descriptors */
				list_splice_tail_init(&schan->active,
						      &schan->completed);
				dma_cookie_complete(&sdesc->desc);
				if (!list_empty(&schan->queued))
					sirfsoc_dma_execute(schan);
			} else
				schan->happened_cyclic++;
empty_list1:
			spin_unlock(&schan->lock);
		}
		break;

	case SIRFSOC_DMA_VER_A7V2:
		is = readl(sdma->base + SIRFSOC_DMA_INT_ATLAS7);

		reg = sdma->base + SIRFSOC_DMA_INT_ATLAS7;
		writel_relaxed(SIRFSOC_DMA_INT_ALL_ATLAS7, reg);
		schan = &sdma->channels[0];
		spin_lock(&schan->lock);
		sdesc = list_first_entry_or_null(&schan->active,
					 struct sirfsoc_dma_desc, node);
		if (!sdesc)
			goto empty_list2;
		if (!sdesc->cyclic) {
			chain = sdesc->chain;
			if ((chain && (is & SIRFSOC_DMA_INT_END_INT_ATLAS7)) ||
				(!chain &&
				(is & SIRFSOC_DMA_INT_FINI_INT_ATLAS7))) {
				/* Execute queued descriptors */
				list_splice_tail_init(&schan->active,
						      &schan->completed);
				dma_cookie_complete(&sdesc->desc);
				if (!list_empty(&schan->queued))
					sirfsoc_dma_execute(schan);
			}
		} else if (sdesc->cyclic && (is &
					SIRFSOC_DMA_INT_LOOP_INT_ATLAS7))
			schan->happened_cyclic++;
empty_list2:
		spin_unlock(&schan->lock);
		break;

	default:
		break;
	}

	/* Schedule tasklet */
	tasklet_schedule(&sdma->tasklet);

	return IRQ_HANDLED;
}

/* process completed descriptors */
static void sirfsoc_dma_process_completed(struct sirfsoc_dma *sdma)
{
	struct sirfsoc_dma_chan *schan;
	struct sirfsoc_dma_desc *sdesc;
	struct dma_async_tx_descriptor *desc;
	unsigned long flags;
	unsigned long happened_cyclic;
	LIST_HEAD(list);
	int i;

	for (i = 0; i < sdma->dma.chancnt; i++) {
		schan = &sdma->channels[i];

		/* Get all completed descriptors */
		spin_lock_irqsave(&schan->lock, flags);
		if (!list_empty(&schan->completed)) {
			list_splice_tail_init(&schan->completed, &list);
			spin_unlock_irqrestore(&schan->lock, flags);

			/* Execute callbacks and run dependencies */
			list_for_each_entry(sdesc, &list, node) {
				desc = &sdesc->desc;

				if (desc->callback)
					desc->callback(desc->callback_param);

				dma_run_dependencies(desc);
			}

			/* Free descriptors */
			spin_lock_irqsave(&schan->lock, flags);
			list_splice_tail_init(&list, &schan->free);
			spin_unlock_irqrestore(&schan->lock, flags);
		} else {
			if (list_empty(&schan->active)) {
				spin_unlock_irqrestore(&schan->lock, flags);
				continue;
			}

			/* for cyclic channel, desc is always in active list */
			sdesc = list_first_entry(&schan->active,
				struct sirfsoc_dma_desc, node);

			/* cyclic DMA */
			happened_cyclic = schan->happened_cyclic;
			spin_unlock_irqrestore(&schan->lock, flags);

			desc = &sdesc->desc;
			while (happened_cyclic != schan->completed_cyclic) {
				if (desc->callback)
					desc->callback(desc->callback_param);
				schan->completed_cyclic++;
			}
		}
	}
}

/* DMA Tasklet */
static void sirfsoc_dma_tasklet(unsigned long data)
{
	struct sirfsoc_dma *sdma = (void *)data;

	sirfsoc_dma_process_completed(sdma);
}

/* Submit descriptor to hardware */
static dma_cookie_t sirfsoc_dma_tx_submit(struct dma_async_tx_descriptor *txd)
{
	struct sirfsoc_dma_chan *schan =
		dma_chan_to_sirfsoc_dma_chan(txd->chan);
	struct sirfsoc_dma_desc *sdesc;
	unsigned long flags;
	dma_cookie_t cookie;

	sdesc = container_of(txd, struct sirfsoc_dma_desc, desc);

	spin_lock_irqsave(&schan->lock, flags);

	/* Move descriptor to queue */
	list_move_tail(&sdesc->node, &schan->queued);

	cookie = dma_cookie_assign(txd);

	spin_unlock_irqrestore(&schan->lock, flags);

	return cookie;
}

static int sirfsoc_dma_slave_config(struct sirfsoc_dma_chan *schan,
	struct dma_slave_config *config)
{
	unsigned long flags;

	spin_lock_irqsave(&schan->lock, flags);
	schan->mode = ((config->src_maxburst > 1 ||
				config->dst_maxburst > 1) ? 1 : 0);
	spin_unlock_irqrestore(&schan->lock, flags);

	return 0;
}

static int sirfsoc_dma_terminate_all(struct sirfsoc_dma_chan *schan)
{
	struct sirfsoc_dma *sdma = dma_chan_to_sirfsoc_dma(&schan->chan);
	int cid = schan->chan.chan_id;
	unsigned long flags;

	spin_lock_irqsave(&schan->lock, flags);

	switch (sdma->type) {
	case SIRFSOC_DMA_VER_A7V1:
		writel_relaxed(1 << cid, sdma->base + SIRFSOC_DMA_INT_EN_CLR);
		writel_relaxed(1 << cid, sdma->base + SIRFSOC_DMA_CH_INT);
		writel_relaxed((1 << cid) | 1 << (cid + 16),
			       sdma->base +
			       SIRFSOC_DMA_CH_LOOP_CTRL_CLR_ATLAS7);
		writel_relaxed(1 << cid, sdma->base + SIRFSOC_DMA_CH_VALID);
		break;
	case SIRFSOC_DMA_VER_A7V2:
		writel_relaxed(0, sdma->base + SIRFSOC_DMA_INT_EN_ATLAS7);
		writel_relaxed(SIRFSOC_DMA_INT_ALL_ATLAS7,
			       sdma->base + SIRFSOC_DMA_INT_ATLAS7);
		writel_relaxed(0, sdma->base + SIRFSOC_DMA_LOOP_CTRL_ATLAS7);
		writel_relaxed(0, sdma->base + SIRFSOC_DMA_VALID_ATLAS7);
		break;
	case SIRFSOC_DMA_VER_A6:
		writel_relaxed(readl_relaxed(sdma->base + SIRFSOC_DMA_INT_EN) &
			       ~(1 << cid), sdma->base + SIRFSOC_DMA_INT_EN);
		writel_relaxed(readl_relaxed(sdma->base +
					     SIRFSOC_DMA_CH_LOOP_CTRL) &
			       ~((1 << cid) | 1 << (cid + 16)),
			       sdma->base + SIRFSOC_DMA_CH_LOOP_CTRL);
		writel_relaxed(1 << cid, sdma->base + SIRFSOC_DMA_CH_VALID);
		break;

	default:
		break;
	}

	list_splice_tail_init(&schan->active, &schan->free);
	list_splice_tail_init(&schan->queued, &schan->free);

	spin_unlock_irqrestore(&schan->lock, flags);

	return 0;
}

static int sirfsoc_dma_pause_chan(struct sirfsoc_dma_chan *schan)
{
	struct sirfsoc_dma *sdma = dma_chan_to_sirfsoc_dma(&schan->chan);
	int cid = schan->chan.chan_id;
	unsigned long flags;

	spin_lock_irqsave(&schan->lock, flags);

	switch (sdma->type) {
	case SIRFSOC_DMA_VER_A7V1:
		writel_relaxed((1 << cid) | 1 << (cid + 16),
			       sdma->base +
			       SIRFSOC_DMA_CH_LOOP_CTRL_CLR_ATLAS7);
		break;
	case SIRFSOC_DMA_VER_A7V2:
		writel_relaxed(0, sdma->base + SIRFSOC_DMA_LOOP_CTRL_ATLAS7);
		break;
	case SIRFSOC_DMA_VER_A6:
		writel_relaxed(readl_relaxed(sdma->base +
					     SIRFSOC_DMA_CH_LOOP_CTRL) &
			       ~((1 << cid) | 1 << (cid + 16)),
			       sdma->base + SIRFSOC_DMA_CH_LOOP_CTRL);
		break;

	default:
		break;
	}

	spin_unlock_irqrestore(&schan->lock, flags);

	return 0;
}

static int sirfsoc_dma_resume_chan(struct sirfsoc_dma_chan *schan)
{
	struct sirfsoc_dma *sdma = dma_chan_to_sirfsoc_dma(&schan->chan);
	int cid = schan->chan.chan_id;
	unsigned long flags;

	spin_lock_irqsave(&schan->lock, flags);
	switch (sdma->type) {
	case SIRFSOC_DMA_VER_A7V1:
		writel_relaxed((1 << cid) | 1 << (cid + 16),
			       sdma->base + SIRFSOC_DMA_CH_LOOP_CTRL_ATLAS7);
		break;
	case SIRFSOC_DMA_VER_A7V2:
		writel_relaxed(0x10001,
			       sdma->base + SIRFSOC_DMA_LOOP_CTRL_ATLAS7);
		break;
	case SIRFSOC_DMA_VER_A6:
		writel_relaxed(readl_relaxed(sdma->base +
					     SIRFSOC_DMA_CH_LOOP_CTRL) |
			       ((1 << cid) | 1 << (cid + 16)),
			       sdma->base + SIRFSOC_DMA_CH_LOOP_CTRL);
		break;

	default:
		break;
	}

	spin_unlock_irqrestore(&schan->lock, flags);

	return 0;
}

static int sirfsoc_dma_control(struct dma_chan *chan, enum dma_ctrl_cmd cmd,
	unsigned long arg)
{
	struct dma_slave_config *config;
	struct sirfsoc_dma_chan *schan = dma_chan_to_sirfsoc_dma_chan(chan);

	switch (cmd) {
	case DMA_PAUSE:
		return sirfsoc_dma_pause_chan(schan);
	case DMA_RESUME:
		return sirfsoc_dma_resume_chan(schan);
	case DMA_TERMINATE_ALL:
		return sirfsoc_dma_terminate_all(schan);
	case DMA_SLAVE_CONFIG:
		config = (struct dma_slave_config *)arg;
		return sirfsoc_dma_slave_config(schan, config);

	default:
		break;
	}

	return -ENOSYS;
}

static int sirfsoc_dma_chan_owner_get(struct dma_chan *chan)
{
	int reg_idx, offset;
	u32 val;
	struct sirfsoc_dma *sdma = dma_chan_to_sirfsoc_dma(chan);
	struct sirfsoc_dma_chan *schan = dma_chan_to_sirfsoc_dma_chan(chan);
	/*
	 * DMA ownership registers are separated into two groups,
	 * group 0 for ch0-9, group 1 for the rest channels
	 */
	reg_idx = schan->chan.chan_id / 10;
	offset = schan->chan.chan_id % 10;
	offset *= 3;
	val = readl_relaxed(sdma->base + sirfsoc_owner_regs[reg_idx]);
	val &= ~(SIRFSOC_OWNER_MASK << offset);
	val |= SIRFSOC_DMA_OWNER_A7 << offset;
	writel_relaxed(val, sdma->base + sirfsoc_owner_regs[reg_idx]);
	sdma->owned |= (0x1 << schan->chan.chan_id);

	return 0;
}

static int sirfsoc_dma_chan_owner_put(struct dma_chan *chan)
{
	int reg_idx, offset;
	u32 val;
	struct sirfsoc_dma *sdma = dma_chan_to_sirfsoc_dma(chan);
	struct sirfsoc_dma_chan *schan = dma_chan_to_sirfsoc_dma_chan(chan);

	reg_idx = schan->chan.chan_id / 10;
	offset = schan->chan.chan_id % 10;
	offset *= 3;
	val = readl_relaxed(sdma->base + sirfsoc_owner_regs[reg_idx]);
	val &= ~(SIRFSOC_OWNER_MASK << offset);
	val |= SIRFSOC_DMA_OWNER_KAS << offset;
	writel_relaxed(val, sdma->base + sirfsoc_owner_regs[reg_idx]);
	sdma->owned &= ~(0x1 << schan->chan.chan_id);

	return 0;
}

/* Alloc channel resources */
static int sirfsoc_dma_alloc_chan_resources(struct dma_chan *chan)
{
	struct sirfsoc_dma *sdma = dma_chan_to_sirfsoc_dma(chan);
	struct sirfsoc_dma_chan *schan = dma_chan_to_sirfsoc_dma_chan(chan);
	struct sirfsoc_dma_desc *sdesc;
	unsigned long flags;
	LIST_HEAD(descs);
	int i;

	pm_runtime_get_sync(sdma->dma.dev);

	/* Alloc descriptors for this channel */
	for (i = 0; i < SIRFSOC_DMA_DESCRIPTORS; i++) {
		sdesc = kzalloc(sizeof(*sdesc), GFP_KERNEL);
		if (!sdesc) {
			dev_notice(sdma->dma.dev, "Memory allocation error. "
				"Allocated only %u descriptors\n", i);
			break;
		}

		dma_async_tx_descriptor_init(&sdesc->desc, chan);
		sdesc->desc.flags = DMA_CTRL_ACK;
		sdesc->desc.tx_submit = sirfsoc_dma_tx_submit;

		list_add_tail(&sdesc->node, &descs);
	}

	/* Return error only if no descriptors were allocated */
	if (i == 0)
		return -ENOMEM;

	spin_lock_irqsave(&schan->lock, flags);

	if (sdma->type == SIRFSOC_DMA_VER_A7V1)
		sirfsoc_dma_chan_owner_get(chan);
	list_splice_tail_init(&descs, &schan->free);
	spin_unlock_irqrestore(&schan->lock, flags);

	return i;
}

/* Free channel resources */
static void sirfsoc_dma_free_chan_resources(struct dma_chan *chan)
{
	struct sirfsoc_dma_chan *schan = dma_chan_to_sirfsoc_dma_chan(chan);
	struct sirfsoc_dma *sdma = dma_chan_to_sirfsoc_dma(chan);
	struct sirfsoc_dma_desc *sdesc, *tmp;
	unsigned long flags;
	LIST_HEAD(descs);

	spin_lock_irqsave(&schan->lock, flags);

	/* Channel must be idle */
	BUG_ON(!list_empty(&schan->prepared));
	BUG_ON(!list_empty(&schan->queued));
	BUG_ON(!list_empty(&schan->active));
	BUG_ON(!list_empty(&schan->completed));

	/* Move data */
	list_splice_tail_init(&schan->free, &descs);

	if (sdma->type == SIRFSOC_DMA_VER_A7V1)
		sirfsoc_dma_chan_owner_put(chan);
	spin_unlock_irqrestore(&schan->lock, flags);

	/* Free descriptors */
	list_for_each_entry_safe(sdesc, tmp, &descs, node)
		kfree(sdesc);

	pm_runtime_put(sdma->dma.dev);
}

/* Send pending descriptor to hardware */
static void sirfsoc_dma_issue_pending(struct dma_chan *chan)
{
	struct sirfsoc_dma_chan *schan = dma_chan_to_sirfsoc_dma_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&schan->lock, flags);

	if (list_empty(&schan->active) && !list_empty(&schan->queued))
		sirfsoc_dma_execute(schan);

	spin_unlock_irqrestore(&schan->lock, flags);
}

/* Check request completion status */
static enum dma_status
sirfsoc_dma_tx_status(struct dma_chan *chan, dma_cookie_t cookie,
	struct dma_tx_state *txstate)
{
	struct sirfsoc_dma *sdma = dma_chan_to_sirfsoc_dma(chan);
	struct sirfsoc_dma_chan *schan = dma_chan_to_sirfsoc_dma_chan(chan);
	unsigned long flags;
	enum dma_status ret;
	struct sirfsoc_dma_desc *sdesc;
	int cid = schan->chan.chan_id;
	unsigned long dma_pos;
	unsigned long dma_request_bytes;
	unsigned long residue;

	spin_lock_irqsave(&schan->lock, flags);

	if (list_empty(&schan->active)) {
		ret = dma_cookie_status(chan, cookie, txstate);
		dma_set_residue(txstate, 0);
		spin_unlock_irqrestore(&schan->lock, flags);
		return ret;
	}
	sdesc = list_first_entry(&schan->active, struct sirfsoc_dma_desc, node);
	if (sdesc->cyclic)
		dma_request_bytes = (sdesc->xlen + 1) * (sdesc->ylen + 1) *
			(sdesc->width * SIRFSOC_DMA_WORD_LEN);
	else
		dma_request_bytes = sdesc->xlen * SIRFSOC_DMA_WORD_LEN;

	ret = dma_cookie_status(chan, cookie, txstate);

	if (sdma->type == SIRFSOC_DMA_VER_A7V2)
		cid = 0;

	if (sdma->type == SIRFSOC_DMA_VER_A7V2) {
		dma_pos = readl_relaxed(sdma->base + SIRFSOC_DMA_CUR_DATA_ADDR);
	} else {
		dma_pos = readl_relaxed(
			sdma->base + cid * 0x10 + SIRFSOC_DMA_CH_ADDR) << 2;
	}

	residue = dma_request_bytes - (dma_pos - sdesc->addr);
	dma_set_residue(txstate, residue);

	spin_unlock_irqrestore(&schan->lock, flags);

	return ret;
}

static struct dma_async_tx_descriptor *sirfsoc_dma_prep_interleaved(
	struct dma_chan *chan, struct dma_interleaved_template *xt,
	unsigned long flags)
{
	struct sirfsoc_dma *sdma = dma_chan_to_sirfsoc_dma(chan);
	struct sirfsoc_dma_chan *schan = dma_chan_to_sirfsoc_dma_chan(chan);
	struct sirfsoc_dma_desc *sdesc = NULL;
	unsigned long iflags;
	int ret;

	if ((xt->dir != DMA_MEM_TO_DEV) && (xt->dir != DMA_DEV_TO_MEM)) {
		ret = -EINVAL;
		goto err_dir;
	}

	/* Get free descriptor */
	spin_lock_irqsave(&schan->lock, iflags);
	if (!list_empty(&schan->free)) {
		sdesc = list_first_entry(&schan->free, struct sirfsoc_dma_desc,
			node);
		list_del(&sdesc->node);
	}
	spin_unlock_irqrestore(&schan->lock, iflags);

	if (!sdesc) {
		/* try to free completed descriptors */
		sirfsoc_dma_process_completed(sdma);
		ret = 0;
		goto no_desc;
	}

	/* Place descriptor in prepared list */
	spin_lock_irqsave(&schan->lock, iflags);

	/*
	 * Number of chunks in a frame can only be 1 for prima2
	 * and ylen (number of frame - 1) must be at least 0
	 */
	if ((xt->frame_size == 1) && (xt->numf > 0)) {
		sdesc->cyclic = 0;
		sdesc->xlen = xt->sgl[0].size / SIRFSOC_DMA_WORD_LEN;
		sdesc->width = (xt->sgl[0].size + xt->sgl[0].icg) /
				SIRFSOC_DMA_WORD_LEN;
		sdesc->ylen = xt->numf - 1;
		if (xt->dir == DMA_MEM_TO_DEV) {
			sdesc->addr = xt->src_start;
			sdesc->dir = 1;
		} else {
			sdesc->addr = xt->dst_start;
			sdesc->dir = 0;
		}

		list_add_tail(&sdesc->node, &schan->prepared);
	} else {
		pr_err("sirfsoc DMA Invalid xfer\n");
		ret = -EINVAL;
		goto err_xfer;
	}
	spin_unlock_irqrestore(&schan->lock, iflags);

	return &sdesc->desc;
err_xfer:
	spin_unlock_irqrestore(&schan->lock, iflags);
no_desc:
err_dir:
	return ERR_PTR(ret);
}

static struct dma_async_tx_descriptor *
sirfsoc_dma_prep_slave_sg(struct dma_chan *chan, struct scatterlist *sgl,
	unsigned int sg_len, enum dma_transfer_direction direction,
	unsigned long flags, void *context)
{
	struct sirfsoc_dma *sdma = dma_chan_to_sirfsoc_dma(chan);
	struct sirfsoc_dma_chan *schan = dma_chan_to_sirfsoc_dma_chan(chan);
	struct sirfsoc_dma_desc *first_sdesc;
	struct sirfsoc_dma_desc *sdesc;
	struct list_head *l;
	unsigned long iflags;
	struct scatterlist *sg;
	int desc_cnt = 0, i = 0;
	int ret = 0;
	int flag = 0;
	dma_addr_t addr;
	unsigned int len;
	int desc_req_cnt;
	unsigned int xlen_max;

	spin_lock_irqsave(&schan->lock, iflags);
	list_for_each(l, &schan->free)
		desc_cnt++;
	desc_req_cnt = (sdma->type == SIRFSOC_DMA_VER_A7V2) ? 1 : sg_len;

	if (desc_cnt < desc_req_cnt) {
		spin_unlock_irqrestore(&schan->lock, iflags);
		pr_err("sirfsoc DMA channel busy\n");
		ret = -EBUSY;
		goto err;
	}

	xlen_max = (sdma->type == SIRFSOC_DMA_VER_A7V1) ?
			SIRFSOC_DMA_XLEN_MAX_V1 : SIRFSOC_DMA_XLEN_MAX_V2;
	first_sdesc = list_first_entry(&schan->free, struct sirfsoc_dma_desc,
			node);

	switch (sdma->type) {
	case SIRFSOC_DMA_VER_A6:
	case SIRFSOC_DMA_VER_A7V1:
		/*
		* the hardware doesn't support sg, here we use software list
		* to simulate sg, so make sure we have enough desc nodes
		*/
		for_each_sg(sgl, sg, sg_len, i) {
			u32 i;

			addr = sg_dma_address(sg);
			len = sg_dma_len(sg);
			sdesc = list_first_entry(&schan->free,
						struct sirfsoc_dma_desc, node);
			list_del(&sdesc->node);
			sdesc->addr = addr;
			sdesc->dir = (direction == DMA_MEM_TO_DEV ? 1 : 0);
			sdesc->cyclic = 0;
			sdesc->chain = 0;

			/* The xlen, ylen and width has the size limit in
			 * hardware(12 bits), so if the len is smaller than
			 * 4*2048,driver can set the ylen to 0, the dma works
			 * in 1D mode, if the len is equal or larger than 4*2048
			 * xlen and ylen must be set to proper value and dma
			 * works in 2D mode.
			 * */
			if (len / 4 < xlen_max) {
				sdesc->xlen = len / 4;
				sdesc->ylen = 0;
			} else {
				for (i = xlen_max - 1; i > 0; i--)
					if (!(len%(4 * i)))
						break;
				if (!i) {
					ret = -EINVAL;
					spin_unlock_irqrestore(
						&schan->lock, iflags);
					goto err;
				}
				sdesc->xlen = i;
				sdesc->ylen = len/(4 * i) - 1;
			}
			sdesc->width = sdesc->xlen;
			list_add_tail(&sdesc->node, &schan->prepared);
		}
		break;

	case SIRFSOC_DMA_VER_A7V2:
		if (!(sg_len > 0 && sg_len < SIRFSOC_DMA_TABLE_NUM)) {
			ret = -EINVAL;
			goto err;
		}

		list_del(&first_sdesc->node);
		for_each_sg(sgl, sg, sg_len, i) {
			addr = sg_dma_address(sg);
			len = sg_dma_len(sg);
			flag = SIRFSOC_DMA_CHAIN_NORMAL;
			first_sdesc->chain_table[i] = (
			(u64) addr << SIRFSOC_DMA_CHAIN_ADDR_SHIFT) |
			(flag << SIRFSOC_DMA_CHAIN_FLAG_SHIFT_ATLAS7) |
			len >> 2;
			}

		first_sdesc->chain_table[i] =
		    (SIRFSOC_DMA_CHAIN_END <<
		     SIRFSOC_DMA_CHAIN_FLAG_SHIFT_ATLAS7);
		__cpuc_flush_dcache_area(first_sdesc->chain_table,
			SIRFSOC_DMA_TABLE_NUM *
			sizeof(first_sdesc->chain_table));
		first_sdesc->addr = virt_to_phys(first_sdesc->chain_table);
		first_sdesc->dir = (direction == DMA_MEM_TO_DEV ? 1 : 0);
		first_sdesc->cyclic = 0;
		first_sdesc->chain = 1;
		list_add_tail(&first_sdesc->node, &schan->prepared);
		break;

	default:
		ret = -EINVAL;
		goto err;
	}

	spin_unlock_irqrestore(&schan->lock, iflags);
	return &first_sdesc->desc;
err:
	return ERR_PTR(ret);
}

static struct dma_async_tx_descriptor *
sirfsoc_dma_prep_cyclic(struct dma_chan *chan, dma_addr_t addr,
	size_t buf_len, size_t period_len,
	enum dma_transfer_direction direction, unsigned long flags)
{
	struct sirfsoc_dma_chan *schan = dma_chan_to_sirfsoc_dma_chan(chan);
	struct sirfsoc_dma_desc *sdesc = NULL;
	int width = 0, i;
	unsigned long iflags;

	/*
	 * we only support cycle transfer with 2 period
	 * If the X-length is set to 0, it would be the loop mode.
	 * The DMA address keeps increasing until reaching the end of a loop
	 * area whose size is defined by (DMA_WIDTH x (Y_LENGTH + 1)). Then
	 * the DMA address goes back to the beginning of this area.
	 * In loop mode, the DMA data region is divided into two parts, BUFA
	 * and BUFB. DMA controller generates interrupts twice in each loop:
	 * when the DMA address reaches the end of BUFA or the end of the
	 * BUFB
	 */
	if (buf_len !=  2 * period_len)
		return ERR_PTR(-EINVAL);

	/* Get free descriptor */
	spin_lock_irqsave(&schan->lock, iflags);
	if (!list_empty(&schan->free)) {
		sdesc = list_first_entry(&schan->free, struct sirfsoc_dma_desc,
			node);
		list_del(&sdesc->node);
	}
	spin_unlock_irqrestore(&schan->lock, iflags);

	if (!sdesc)
		return NULL;

	/* Place descriptor in prepared list */
	spin_lock_irqsave(&schan->lock, iflags);
	sdesc->addr = addr;
	sdesc->cyclic = 1;
	sdesc->xlen = 0;
	/*
	 * In loop mode, DMA width is in D-word unit and is restricted to an
	 * integer multiple of 4
	 */
	for (i = 1024; i >= 16; i /= 2) {
		if (!(period_len % i)) {
			width = i / 4;
			break;
		}
	}
	if (width == 0)
		return ERR_PTR(-EINVAL);

	sdesc->ylen = buf_len / (width * SIRFSOC_DMA_WORD_LEN) - 1;
	sdesc->width = width;
	if (direction == DMA_MEM_TO_DEV)
		sdesc->dir = 1;
	else
		sdesc->dir = 0;
	list_add_tail(&sdesc->node, &schan->prepared);
	spin_unlock_irqrestore(&schan->lock, iflags);

	return &sdesc->desc;
}

/*
 * The DMA controller consists of 16 independent DMA channels.
 * Each channel is allocated to a different function
 */
bool sirfsoc_dma_filter_id(struct dma_chan *chan, void *chan_id)
{
	unsigned int ch_nr = (unsigned int) chan_id;

	if (ch_nr == chan->chan_id +
		chan->device->dev_id * SIRFSOC_DMA_CHANNELS)
		return true;

	return false;
}
EXPORT_SYMBOL(sirfsoc_dma_filter_id);

#define SIRFSOC_DMA_BUSWIDTHS \
	(BIT(DMA_SLAVE_BUSWIDTH_UNDEFINED) | \
	BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) | \
	BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) | \
	BIT(DMA_SLAVE_BUSWIDTH_4_BYTES) | \
	BIT(DMA_SLAVE_BUSWIDTH_8_BYTES))

static int sirfsoc_dma_device_slave_caps(struct dma_chan *dchan,
	struct dma_slave_caps *caps)
{
	caps->src_addr_widths = SIRFSOC_DMA_BUSWIDTHS;
	caps->dstn_addr_widths = SIRFSOC_DMA_BUSWIDTHS;
	caps->directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	caps->cmd_pause = true;
	caps->cmd_terminate = true;

	return 0;
}

static struct dma_chan *of_dma_sirfsoc_xlate(struct of_phandle_args *dma_spec,
	struct of_dma *ofdma)
{
	struct sirfsoc_dma *sdma = ofdma->of_dma_data;
	unsigned int request = dma_spec->args[0];

	if (request >= SIRFSOC_DMA_CHANNELS)
		return NULL;

	return dma_get_slave_channel(&sdma->channels[request].chan);
}

static int sirfsoc_dma_probe(struct platform_device *op)
{
	struct device_node *dn = op->dev.of_node;
	struct device *dev = &op->dev;
	struct dma_device *dma;
	struct sirfsoc_dma *sdma;
	struct sirfsoc_dma_chan *schan;
	struct sirfsoc_dmadata *data;
	struct resource res;
	ulong regs_start, regs_size;
	u32 id;
	int ret, i;
	int dma_channels = 0;
	const struct of_device_id *match;

	sdma = devm_kzalloc(dev, sizeof(*sdma), GFP_KERNEL);
	if (!sdma) {
		dev_err(dev, "Memory exhausted!\n");
		return -ENOMEM;
	}
	match = of_match_device(op->dev.driver->of_match_table, &op->dev);
	if (WARN_ON(!match))
		return -ENODEV;
	data = (struct sirfsoc_dmadata *)(match->data);
	sdma->exec_desc = data->exec;
	sdma->type = data->type;

	if (of_property_read_u32(dn, "cell-index", &id)) {
		dev_err(dev, "Fail to get DMAC index\n");
		return -ENODEV;
	}

	sdma->irq = irq_of_parse_and_map(dn, 0);
	if (sdma->irq == NO_IRQ) {
		dev_err(dev, "Error mapping IRQ!\n");
		return -EINVAL;
	}

	sdma->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(sdma->clk)) {
		dev_err(dev, "failed to get a clock.\n");
		return PTR_ERR(sdma->clk);
	}

	ret = of_address_to_resource(dn, 0, &res);
	if (ret) {
		dev_err(dev, "Error parsing memory region!\n");
		goto irq_dispose;
	}

	regs_start = res.start;
	regs_size = resource_size(&res);

	sdma->base = devm_ioremap(dev, regs_start, regs_size);
	if (!sdma->base) {
		dev_err(dev, "Error mapping memory region!\n");
		ret = -ENOMEM;
		goto irq_dispose;
	}

	ret = request_irq(sdma->irq, &sirfsoc_dma_irq, 0, DRV_NAME, sdma);
	if (ret) {
		dev_err(dev, "Error requesting IRQ!\n");
		ret = -EINVAL;
		goto irq_dispose;
	}

	dma = &sdma->dma;
	dma->dev = dev;

	of_property_read_u32(dn, "#dma-channels", &dma_channels);
	dma->chancnt = dma_channels;
	if (!dma->chancnt)
		dma->chancnt = SIRFSOC_DMA_CHANNELS;

	dma->device_alloc_chan_resources = sirfsoc_dma_alloc_chan_resources;
	dma->device_free_chan_resources = sirfsoc_dma_free_chan_resources;
	dma->device_issue_pending = sirfsoc_dma_issue_pending;
	dma->device_control = sirfsoc_dma_control;
	dma->device_tx_status = sirfsoc_dma_tx_status;
	dma->device_prep_interleaved_dma = sirfsoc_dma_prep_interleaved;
	dma->device_prep_slave_sg = sirfsoc_dma_prep_slave_sg;
	dma->device_prep_dma_cyclic = sirfsoc_dma_prep_cyclic;
	dma->device_slave_caps = sirfsoc_dma_device_slave_caps;

	INIT_LIST_HEAD(&dma->channels);
	dma_cap_set(DMA_SLAVE, dma->cap_mask);
	dma_cap_set(DMA_CYCLIC, dma->cap_mask);
	dma_cap_set(DMA_INTERLEAVE, dma->cap_mask);
	dma_cap_set(DMA_PRIVATE, dma->cap_mask);

	for (i = 0; i < dma->chancnt; i++) {
		schan = &sdma->channels[i];

		schan->chan.device = dma;
		dma_cookie_init(&schan->chan);

		INIT_LIST_HEAD(&schan->free);
		INIT_LIST_HEAD(&schan->prepared);
		INIT_LIST_HEAD(&schan->queued);
		INIT_LIST_HEAD(&schan->active);
		INIT_LIST_HEAD(&schan->completed);

		spin_lock_init(&schan->lock);
		list_add_tail(&schan->chan.device_node, &dma->channels);
	}

	tasklet_init(&sdma->tasklet, sirfsoc_dma_tasklet, (unsigned long)sdma);

	/* Register DMA engine */
	dev_set_drvdata(dev, sdma);

	ret = dma_async_device_register(dma);
	if (ret)
		goto free_irq;

	/* Device-tree DMA controller registration */
	ret = of_dma_controller_register(dn, of_dma_sirfsoc_xlate, sdma);
	if (ret) {
		sdma->cap = dma->cap_mask;
		dev_err(dev, "failed to register DMA controller\n");
		goto unreg_dma_dev;
	}

	pm_runtime_enable(&op->dev);
	dev_info(dev, "initialized SIRFSOC DMAC driver\n");

	return 0;

unreg_dma_dev:
	dma_async_device_unregister(dma);
free_irq:
	free_irq(sdma->irq, sdma);
irq_dispose:
	irq_dispose_mapping(sdma->irq);
	return ret;
}

static int sirfsoc_dma_remove(struct platform_device *op)
{
	struct device *dev = &op->dev;
	struct sirfsoc_dma *sdma = dev_get_drvdata(dev);

	of_dma_controller_free(op->dev.of_node);
	dma_async_device_unregister(&sdma->dma);
	free_irq(sdma->irq, sdma);
	irq_dispose_mapping(sdma->irq);
	pm_runtime_disable(&op->dev);
	if (!pm_runtime_status_suspended(&op->dev))
		sirfsoc_dma_runtime_suspend(&op->dev);

	return 0;
}

static int sirfsoc_dma_runtime_suspend(struct device *dev)
{
	struct sirfsoc_dma *sdma = dev_get_drvdata(dev);

	clk_disable_unprepare(sdma->clk);
	return 0;
}

static int sirfsoc_dma_runtime_resume(struct device *dev)
{
	struct sirfsoc_dma *sdma = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(sdma->clk);
	if (ret < 0) {
		dev_err(dev, "clk_enable failed: %d\n", ret);
		return ret;
	}
	return 0;
}

static int sirfsoc_dma_pm_suspend_noirq(struct device *dev)
{
	struct sirfsoc_dma *sdma = dev_get_drvdata(dev);
	struct sirfsoc_dma_regs *save = &sdma->regs_save;
	struct sirfsoc_dma_desc *sdesc;
	struct sirfsoc_dma_chan *schan;
	int ch;
	int ret;
	int count;
	u32 int_offset;

	/*
	 * if we were runtime-suspended before, resume to enable clock
	 * before accessing register
	 */
	if (pm_runtime_status_suspended(dev)) {
		ret = sirfsoc_dma_runtime_resume(dev);
		if (ret < 0)
			return ret;
	}

	if (sdma->type == SIRFSOC_DMA_VER_A7V2) {
		count = 1;
		int_offset = SIRFSOC_DMA_INT_EN_ATLAS7;
	} else {
		count = SIRFSOC_DMA_CHANNELS;
		int_offset = SIRFSOC_DMA_INT_EN;
	}

	/*
	 * DMA controller will lose all registers while suspending
	 * so we need to save registers for active channels
	 */
	for (ch = 0; ch < count; ch++) {
		schan = &sdma->channels[ch];
		if (list_empty(&schan->active))
			continue;
		sdesc = list_first_entry(&schan->active,
			struct sirfsoc_dma_desc,
			node);
		save->ctrl[ch] = readl_relaxed(sdma->base +
			ch * 0x10 + SIRFSOC_DMA_CH_CTRL);
	}
	save->interrupt_en = readl_relaxed(sdma->base + int_offset);

	if (sdma->type == SIRFSOC_DMA_VER_A7V1) {
		save->owener_reg0 = readl_relaxed(sdma->base +
						  SIRFSOC_DMA_INT_OWNER_REG0);
		save->owener_reg1 = readl_relaxed(sdma->base +
						  SIRFSOC_DMA_INT_OWNER_REG1);
	}
	/* Disable clock */
	sirfsoc_dma_runtime_suspend(dev);

	return 0;
}

static int sirfsoc_dma_pm_resume_noirq(struct device *dev)
{
	struct sirfsoc_dma *sdma = dev_get_drvdata(dev);
	struct sirfsoc_dma_regs *save = &sdma->regs_save;
	struct sirfsoc_dma_desc *sdesc;
	struct sirfsoc_dma_chan *schan;
	int ch;
	int ret;
	int count;
	u32 int_offset;
	u32 width_offset;

	/* Enable clock before accessing register */
	ret = sirfsoc_dma_runtime_resume(dev);
	if (ret < 0)
		return ret;

	if (sdma->type == SIRFSOC_DMA_VER_A7V2) {
		count = 1;
		int_offset = SIRFSOC_DMA_INT_EN_ATLAS7;
		width_offset = SIRFSOC_DMA_WIDTH_ATLAS7;
	} else {
		count = SIRFSOC_DMA_CHANNELS;
		int_offset = SIRFSOC_DMA_INT_EN;
		width_offset = SIRFSOC_DMA_WIDTH_0;
	}

	if (sdma->type == SIRFSOC_DMA_VER_A7V1) {
		writel_relaxed(save->owener_reg0, sdma->base +
			       SIRFSOC_DMA_INT_OWNER_REG0);
		writel_relaxed(save->owener_reg1, sdma->base +
			       SIRFSOC_DMA_INT_OWNER_REG1);
	}
	writel_relaxed(save->interrupt_en, sdma->base + int_offset);
	for (ch = 0; ch < count; ch++) {
		schan = &sdma->channels[ch];
		if (list_empty(&schan->active))
			continue;
		sdesc = list_first_entry(&schan->active,
			struct sirfsoc_dma_desc,
			node);
		writel_relaxed(sdesc->width,
			sdma->base + width_offset + ch * 4);
		writel_relaxed(sdesc->xlen,
			sdma->base + ch * 0x10 + SIRFSOC_DMA_CH_XLEN);
		writel_relaxed(sdesc->ylen,
			sdma->base + ch * 0x10 + SIRFSOC_DMA_CH_YLEN);
		writel_relaxed(save->ctrl[ch],
			sdma->base + ch * 0x10 + SIRFSOC_DMA_CH_CTRL);
		if (sdma->type == SIRFSOC_DMA_VER_A7V2) {
			writel_relaxed(sdesc->addr,
				sdma->base + SIRFSOC_DMA_CH_ADDR);
		} else {
			writel_relaxed(sdesc->addr >> 2,
				sdma->base + ch * 0x10 + SIRFSOC_DMA_CH_ADDR);

		}
	}

	/* if we were runtime-suspended before, suspend again */
	if (pm_runtime_status_suspended(dev))
		sirfsoc_dma_runtime_suspend(dev);

	return 0;
}

static const struct dev_pm_ops sirfsoc_dma_pm_ops = {
	SET_RUNTIME_PM_OPS(sirfsoc_dma_runtime_suspend,
		sirfsoc_dma_runtime_resume, NULL)
	.suspend_noirq = sirfsoc_dma_pm_suspend_noirq,
	.resume_noirq = sirfsoc_dma_pm_resume_noirq,
	.freeze_noirq = sirfsoc_dma_pm_suspend_noirq,
	.thaw_noirq = sirfsoc_dma_pm_resume_noirq,
	.poweroff_noirq = sirfsoc_dma_pm_suspend_noirq,
	.restore_noirq = sirfsoc_dma_pm_resume_noirq,
};

struct sirfsoc_dmadata sirfsoc_dmadata_a6 = {
	.exec = sirfsoc_dma_execute_hw_a6,
	.type = SIRFSOC_DMA_VER_A6,
};

struct sirfsoc_dmadata sirfsoc_dmadata_a7v1 = {
	.exec = sirfsoc_dma_execute_hw_a7v1,
	.type = SIRFSOC_DMA_VER_A7V1,
};

struct sirfsoc_dmadata sirfsoc_dmadata_a7v2 = {
	.exec = sirfsoc_dma_execute_hw_a7v2,
	.type = SIRFSOC_DMA_VER_A7V2,
};

static const struct of_device_id sirfsoc_dma_match[] = {
	{ .compatible = "sirf,prima2-dmac", .data = &sirfsoc_dmadata_a6,},
	{ .compatible = "sirf,atlas7-dmac", .data = &sirfsoc_dmadata_a7v1,},
	{ .compatible = "sirf,atlas7-dmac-v2", .data = &sirfsoc_dmadata_a7v2,},
	{},
};

static struct platform_driver sirfsoc_dma_driver = {
	.probe		= sirfsoc_dma_probe,
	.remove		= sirfsoc_dma_remove,
	.driver = {
		.name = DRV_NAME,
		.pm = &sirfsoc_dma_pm_ops,
		.of_match_table	= sirfsoc_dma_match,
	},
};

static __init int sirfsoc_dma_init(void)
{
	return platform_driver_register(&sirfsoc_dma_driver);
}

static void __exit sirfsoc_dma_exit(void)
{
	platform_driver_unregister(&sirfsoc_dma_driver);
}

subsys_initcall(sirfsoc_dma_init);
module_exit(sirfsoc_dma_exit);

MODULE_DESCRIPTION("SIRFSOC DMA control driver");
MODULE_LICENSE("GPL v2");
