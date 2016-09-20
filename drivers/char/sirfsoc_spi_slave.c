/*
 * SPI bus driver for CSR SiRFprimaII
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/dmaengine.h>
#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <linux/miscdevice.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/spi/spi.h>
#include <linux/uaccess.h>
#include <linux/spi/spidev.h>
#include <linux/delay.h>
#define DRIVER_NAME "sirfsoc_spi_slave"

#define SIRFSOC_SPI_CTRL		0x0000
#define SIRFSOC_SPI_CMD			0x0004
#define SIRFSOC_SPI_TX_RX_EN		0x0008
#define SIRFSOC_SPI_INT_EN		0x000C
#define SIRFSOC_SPI_INT_STATUS		0x0010
#define SIRFSOC_SPI_TX_DMA_IO_CTRL	0x0100
#define SIRFSOC_SPI_TX_DMA_IO_LEN	0x0104
#define SIRFSOC_SPI_TXFIFO_CTRL		0x0108
#define SIRFSOC_SPI_TXFIFO_LEVEL_CHK	0x010C
#define SIRFSOC_SPI_TXFIFO_OP		0x0110
#define SIRFSOC_SPI_TXFIFO_STATUS	0x0114
#define SIRFSOC_SPI_TXFIFO_DATA		0x0118
#define SIRFSOC_SPI_RX_DMA_IO_CTRL	0x0120
#define SIRFSOC_SPI_RX_DMA_IO_LEN	0x0124
#define SIRFSOC_SPI_RXFIFO_CTRL		0x0128
#define SIRFSOC_SPI_RXFIFO_LEVEL_CHK	0x012C
#define SIRFSOC_SPI_RXFIFO_OP		0x0130
#define SIRFSOC_SPI_RXFIFO_STATUS	0x0134
#define SIRFSOC_SPI_RXFIFO_DATA		0x0138
#define SIRFSOC_SPI_DUMMY_DELAY_CTL	0x0144

/* SPI CTRL register defines */
#define SIRFSOC_SPI_SLV_MODE		BIT(16)
#define SIRFSOC_SPI_CMD_MODE		BIT(17)
#define SIRFSOC_SPI_CS_IO_OUT		BIT(18)
#define SIRFSOC_SPI_CS_IO_MODE		BIT(19)
#define SIRFSOC_SPI_CLK_IDLE_STAT	BIT(20)
#define SIRFSOC_SPI_CS_IDLE_STAT	BIT(21)
#define SIRFSOC_SPI_TRAN_MSB		BIT(22)
#define SIRFSOC_SPI_DRV_POS_EDGE	BIT(23)
#define SIRFSOC_SPI_CS_HOLD_TIME	BIT(24)
#define SIRFSOC_SPI_CLK_SAMPLE_MODE	BIT(25)
#define SIRFSOC_SPI_TRAN_DAT_FORMAT_8	(0 << 26)
#define SIRFSOC_SPI_TRAN_DAT_FORMAT_12	(1 << 26)
#define SIRFSOC_SPI_TRAN_DAT_FORMAT_16	(2 << 26)
#define SIRFSOC_SPI_TRAN_DAT_FORMAT_32	(3 << 26)
#define SIRFSOC_SPI_CMD_BYTE_NUM(x)	((x & 3) << 28)
#define SIRFSOC_SPI_ENA_AUTO_CLR	BIT(30)
#define SIRFSOC_SPI_MUL_DAT_MODE	BIT(31)

/* Interrupt Enable */
#define SIRFSOC_SPI_RX_DONE_INT_EN	BIT(0)
#define SIRFSOC_SPI_TX_DONE_INT_EN	BIT(1)
#define SIRFSOC_SPI_RX_OFLOW_INT_EN	BIT(2)
#define SIRFSOC_SPI_TX_UFLOW_INT_EN	BIT(3)
#define SIRFSOC_SPI_RX_IO_DMA_INT_EN	BIT(4)
#define SIRFSOC_SPI_TX_IO_DMA_INT_EN	BIT(5)
#define SIRFSOC_SPI_RXFIFO_FULL_INT_EN	BIT(6)
#define SIRFSOC_SPI_TXFIFO_EMPTY_INT_EN	BIT(7)
#define SIRFSOC_SPI_RXFIFO_THD_INT_EN	BIT(8)
#define SIRFSOC_SPI_TXFIFO_THD_INT_EN	BIT(9)
#define SIRFSOC_SPI_FRM_END_INT_EN	BIT(10)

#define SIRFSOC_SPI_INT_MASK_ALL	0x1FFF

/* Interrupt status */
#define SIRFSOC_SPI_RX_DONE		BIT(0)
#define SIRFSOC_SPI_TX_DONE		BIT(1)
#define SIRFSOC_SPI_RX_OFLOW		BIT(2)
#define SIRFSOC_SPI_TX_UFLOW		BIT(3)
#define SIRFSOC_SPI_RX_IO_DMA		BIT(4)
#define SIRFSOC_SPI_TX_IO_DMA		BIT(5)
#define SIRFSOC_SPI_RX_FIFO_FULL	BIT(6)
#define SIRFSOC_SPI_TXFIFO_EMPTY	BIT(7)
#define SIRFSOC_SPI_RXFIFO_THD_REACH	BIT(8)
#define SIRFSOC_SPI_TXFIFO_THD_REACH	BIT(9)
#define SIRFSOC_SPI_FRM_END		BIT(10)

/* TX RX enable */
#define SIRFSOC_SPI_RX_EN		BIT(0)
#define SIRFSOC_SPI_TX_EN		BIT(1)
#define SIRFSOC_SPI_CMD_TX_EN		BIT(2)

#define SIRFSOC_SPI_IO_MODE_SEL		BIT(0)
#define SIRFSOC_SPI_RX_DMA_FLUSH	BIT(2)

/* FIFO OPs */
#define SIRFSOC_SPI_FIFO_RESET		BIT(0)
#define SIRFSOC_SPI_FIFO_START		BIT(1)

/* FIFO CTRL */
#define SIRFSOC_SPI_FIFO_WIDTH_BYTE	(0 << 0)
#define SIRFSOC_SPI_FIFO_WIDTH_WORD	(1 << 0)
#define SIRFSOC_SPI_FIFO_WIDTH_DWORD	(2 << 0)

/* FIFO Status */
#define	SIRFSOC_SPI_FIFO_LEVEL_MASK	0xFF
#define SIRFSOC_SPI_FIFO_FULL		BIT(8)
#define SIRFSOC_SPI_FIFO_EMPTY		BIT(9)

/* 256 bytes rx/tx FIFO */
#define SIRFSOC_SPI_FIFO_SIZE		256
#define SIRFSOC_SPI_DAT_FRM_LEN_MAX	(64 * 1024)

#define SIRFSOC_SPI_FIFO_SC(x)		((x) & 0x3F)
#define SIRFSOC_SPI_FIFO_LC(x)		(((x) & 0x3F) << 10)
#define SIRFSOC_SPI_FIFO_HC(x)		(((x) & 0x3F) << 20)
#define SIRFSOC_SPI_FIFO_THD(x)		(((x) & 0xFF) << 2)

/*
 * only if the rx/tx buffer and transfer size are 4-bytes aligned, we use dma
 * due to the limitation of dma controller
 */

#define ALIGNED(x) (!((u32)x & 0x3))
#define IS_DMA_VALID(x) (x && ALIGNED(x->tx_buf) && ALIGNED(x->rx_buf) && \
	ALIGNED(x->len) && (x->len < 2 * PAGE_SIZE))

#define SIRFSOC_MAX_CMD_BYTES	4

#define SPI_MODE_MASK		(SPI_CPHA | SPI_CPOL | SPI_CS_HIGH \
				| SPI_LSB_FIRST | SPI_LOOP)
/* #define SPI_DEBUG*/
#ifdef SPI_DEBUG
#define spi_dbg(format, args...)	pr_info(format, ## args)
#else
#define spi_dbg(format, args...) \
	do {} while (0)
#endif

struct sirfsoc_spi_slave {
	struct completion rx_done;
	struct completion tx_done;
	void __iomem *base;
	u32 ctrl_freq;
	struct clk *clk;
	void *tx;
	void *rx;
	void (*rx_word)(struct sirfsoc_spi_slave *);
	void (*tx_word)(struct sirfsoc_spi_slave *);
	unsigned int left_tx_word;
	unsigned int left_rx_word;
	struct dma_chan *rx_chan;
	struct dma_chan *tx_chan;
	dma_addr_t src_start;
	dma_addr_t dst_start;
	int word_width; /* in bytes */
	u8 mode;
	u8 bits_per_word;
	u32 max_speed_hz;
	struct miscdevice misc;
};

static struct sirfsoc_spi_slave *get_spi_slave(struct miscdevice *misc)
{
	return container_of(misc, struct sirfsoc_spi_slave, misc);
}

static void spi_sirfsoc_rx_word_u8(struct sirfsoc_spi_slave *spi_slave)
{
	u32 data;
	u8 *rx = spi_slave->rx;

	data = readl(spi_slave->base + SIRFSOC_SPI_RXFIFO_DATA);
	if (rx) {
		*rx++ = (u8) data;
		spi_slave->rx = rx;
	}
	spi_slave->left_rx_word--;
}

static void spi_sirfsoc_tx_word_u8(struct sirfsoc_spi_slave *spi_slave)
{
	u32 data = 0;
	u8 *tx = spi_slave->tx;

	if (tx) {
		data = *tx++;
		spi_slave->tx = tx;
	}
	writel(data, spi_slave->base + SIRFSOC_SPI_TXFIFO_DATA);
	spi_slave->left_tx_word--;
}

static void spi_sirfsoc_rx_word_u16(struct sirfsoc_spi_slave *spi_slave)
{
	u32 data;
	u16 *rx = spi_slave->rx;

	data = readl(spi_slave->base + SIRFSOC_SPI_RXFIFO_DATA);
	if (rx) {
		*rx++ = (u16) data;
		spi_slave->rx = rx;
	}
	spi_slave->left_rx_word--;
}

static void spi_sirfsoc_tx_word_u16(struct sirfsoc_spi_slave *spi_slave)
{
	u32 data = 0;
	u16 *tx = spi_slave->tx;

	if (tx) {
		data = *tx++;
		spi_slave->tx = tx;
	}
	writel(data, spi_slave->base + SIRFSOC_SPI_TXFIFO_DATA);
	spi_slave->left_tx_word--;
}

static void spi_sirfsoc_rx_word_u32(struct sirfsoc_spi_slave *spi_slave)
{
	u32 data;
	u32 *rx = spi_slave->rx;

	data = readl(spi_slave->base + SIRFSOC_SPI_RXFIFO_DATA);
	if (rx) {
		*rx++ = (u32) data;
		spi_slave->rx = rx;
	}
	spi_slave->left_rx_word--;
}

static void spi_sirfsoc_tx_word_u32(struct sirfsoc_spi_slave *spi_slave)
{
	u32 data = 0;
	u32 *tx = spi_slave->tx;

	if (tx) {
		data = *tx++;
		spi_slave->tx = tx;
	}
	writel(data, spi_slave->base + SIRFSOC_SPI_TXFIFO_DATA);
	spi_slave->left_tx_word--;
}

static irqreturn_t spi_sirfsoc_irq(int irq, void *dev_id)
{
	struct sirfsoc_spi_slave *spi_slave =
			(struct sirfsoc_spi_slave *)dev_id;
	u32 spi_rx_ctrl, spi_stat;

	spi_stat = readl(spi_slave->base + SIRFSOC_SPI_INT_STATUS);
	spi_stat &= readl(spi_slave->base + SIRFSOC_SPI_INT_EN);
	spi_dbg("%s SLAVE: interrupt state:%x\n", __func__, spi_stat);
	/* Error Conditions */
	if (spi_stat & SIRFSOC_SPI_RX_OFLOW ||
		spi_stat & SIRFSOC_SPI_TX_UFLOW) {
		spi_slave->left_tx_word = spi_slave->left_rx_word = 0;
		goto irq_exit;
	}
	if (spi_stat & SIRFSOC_SPI_TXFIFO_EMPTY)
		goto irq_exit;

	if (spi_stat & SIRFSOC_SPI_FRM_END) {
		/* flush rxfifo data while transfer no 4 aligned bytes in DMA
		 * mode.
		 */
		spi_rx_ctrl = readl(spi_slave->base +
					SIRFSOC_SPI_RX_DMA_IO_CTRL);
		if (!spi_rx_ctrl && ((spi_slave->left_tx_word *
					spi_slave->word_width) % 4))
			writel(spi_rx_ctrl | SIRFSOC_SPI_RX_DMA_FLUSH,
				spi_slave->base + SIRFSOC_SPI_RX_DMA_IO_CTRL);
		goto irq_exit;
	}
irq_exit:
	writel(0x0, spi_slave->base + SIRFSOC_SPI_INT_EN);
	writel(SIRFSOC_SPI_INT_MASK_ALL,
				spi_slave->base + SIRFSOC_SPI_INT_STATUS);
	complete(&spi_slave->tx_done);
	complete(&spi_slave->rx_done);
	return IRQ_HANDLED;
}

static void spi_sirfsoc_dma_transfer(struct sirfsoc_spi_slave *spi_slave)
{
	struct dma_async_tx_descriptor *rx_desc, *tx_desc;
	int transfer_len, timeout = spi_slave->left_tx_word * 20;
	void *rx_ptr = spi_slave->rx;
	void *tx_ptr = spi_slave->tx;

	writel(SIRFSOC_SPI_FIFO_RESET,
			spi_slave->base + SIRFSOC_SPI_RXFIFO_OP);
	writel(SIRFSOC_SPI_FIFO_RESET,
			spi_slave->base + SIRFSOC_SPI_TXFIFO_OP);
	writel(SIRFSOC_SPI_FIFO_START,
			spi_slave->base + SIRFSOC_SPI_RXFIFO_OP);
	writel(SIRFSOC_SPI_FIFO_START,
			spi_slave->base + SIRFSOC_SPI_TXFIFO_OP);
	writel(0, spi_slave->base + SIRFSOC_SPI_INT_EN);
	writel(SIRFSOC_SPI_INT_MASK_ALL,
			spi_slave->base + SIRFSOC_SPI_INT_STATUS);
	transfer_len = spi_slave->left_tx_word * spi_slave->word_width;
	if (transfer_len % 4)
		transfer_len = transfer_len + (4 - (transfer_len % 4));
	if (spi_slave->left_tx_word < SIRFSOC_SPI_DAT_FRM_LEN_MAX) {
		writel(readl(spi_slave->base + SIRFSOC_SPI_CTRL) |
			SIRFSOC_SPI_ENA_AUTO_CLR | SIRFSOC_SPI_MUL_DAT_MODE,
			spi_slave->base + SIRFSOC_SPI_CTRL);
		writel(transfer_len / spi_slave->word_width - 1,
			spi_slave->base + SIRFSOC_SPI_TX_DMA_IO_LEN);
		writel(spi_slave->left_tx_word - 1,
			spi_slave->base + SIRFSOC_SPI_RX_DMA_IO_LEN);
	} else {
		writel(readl(spi_slave->base + SIRFSOC_SPI_CTRL),
			spi_slave->base + SIRFSOC_SPI_CTRL);
		writel(0, spi_slave->base + SIRFSOC_SPI_TX_DMA_IO_LEN);
		writel(0, spi_slave->base + SIRFSOC_SPI_RX_DMA_IO_LEN);
	}
	spi_dbg("%s real_bytes:%d wrong_transfer_len:%d\n", __func__,
		spi_slave->left_tx_word * spi_slave->word_width, transfer_len);
	spi_slave->dst_start = dma_map_single(spi_slave->misc.this_device,
				rx_ptr, transfer_len, DMA_FROM_DEVICE);
	rx_desc = dmaengine_prep_slave_single(spi_slave->rx_chan,
			spi_slave->dst_start, transfer_len, DMA_DEV_TO_MEM,
			DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	rx_desc->callback = NULL;
	spi_slave->src_start = dma_map_single(spi_slave->misc.this_device,
				(void *)tx_ptr, transfer_len, DMA_TO_DEVICE);
	tx_desc = dmaengine_prep_slave_single(spi_slave->tx_chan,
			spi_slave->src_start, transfer_len, DMA_MEM_TO_DEV,
			DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	tx_desc->callback = NULL;
	dmaengine_submit(tx_desc);
	dmaengine_submit(rx_desc);
	dma_async_issue_pending(spi_slave->tx_chan);
	dma_async_issue_pending(spi_slave->rx_chan);
	writel(SIRFSOC_SPI_FRM_END_INT_EN,
			spi_slave->base + SIRFSOC_SPI_INT_EN);
	writel(SIRFSOC_SPI_RX_EN | SIRFSOC_SPI_TX_EN,
			spi_slave->base + SIRFSOC_SPI_TX_RX_EN);
	if (wait_for_completion_timeout(&spi_slave->rx_done, timeout) == 0) {
		pr_err("dma mode: transfer timeout\n");
		dmaengine_terminate_all(spi_slave->rx_chan);
	} else
		spi_slave->left_rx_word = 0;
	/*
	 * we only wait tx-done event if transferring by DMA. for PIO,
	 * we get rx data by writing tx data, so if rx is done, tx has
	 * done earlier
	 */
	if (wait_for_completion_timeout(&spi_slave->tx_done, timeout) == 0) {
		pr_err("dma mode: transfer timeout\n");
		dmaengine_terminate_all(spi_slave->tx_chan);
	}
	dma_unmap_single(spi_slave->misc.this_device, spi_slave->src_start,
			transfer_len, DMA_TO_DEVICE);
	dma_unmap_single(spi_slave->misc.this_device, spi_slave->dst_start,
			transfer_len, DMA_FROM_DEVICE);
	/* TX, RX FIFO stop */
	writel(0, spi_slave->base + SIRFSOC_SPI_RXFIFO_OP);
	writel(0, spi_slave->base + SIRFSOC_SPI_TXFIFO_OP);
	writel(0, spi_slave->base + SIRFSOC_SPI_TX_RX_EN);
}

static void sirfsoc_spi_slave_pio_transfer(struct sirfsoc_spi_slave *spi_slave)
{
	void *ptx;
	void *prx;
	unsigned int data_units;

	ptx = spi_slave->tx;
	prx = spi_slave->rx;
	do {
		writel(0, spi_slave->base + SIRFSOC_SPI_TX_RX_EN);
		writel(SIRFSOC_SPI_FIFO_RESET,
			spi_slave->base + SIRFSOC_SPI_RXFIFO_OP);
		writel(SIRFSOC_SPI_FIFO_RESET,
			spi_slave->base + SIRFSOC_SPI_TXFIFO_OP);
		writel(SIRFSOC_SPI_FIFO_START,
			spi_slave->base + SIRFSOC_SPI_RXFIFO_OP);
		writel(SIRFSOC_SPI_FIFO_START,
			spi_slave->base + SIRFSOC_SPI_TXFIFO_OP);
		writel(readl(spi_slave->base + SIRFSOC_SPI_CTRL) |
			SIRFSOC_SPI_MUL_DAT_MODE | SIRFSOC_SPI_ENA_AUTO_CLR,
			spi_slave->base + SIRFSOC_SPI_CTRL);
		data_units = 256 / spi_slave->word_width;
		writel(min(spi_slave->left_tx_word, data_units) - 1,
				spi_slave->base + SIRFSOC_SPI_TX_DMA_IO_LEN);
		writel(min(spi_slave->left_rx_word, data_units) - 1,
				spi_slave->base + SIRFSOC_SPI_RX_DMA_IO_LEN);
		writel(0, spi_slave->base + SIRFSOC_SPI_INT_EN);
		writel(SIRFSOC_SPI_INT_MASK_ALL,
			spi_slave->base + SIRFSOC_SPI_INT_STATUS);
		while (!((readl(spi_slave->base + SIRFSOC_SPI_TXFIFO_STATUS)
			& SIRFSOC_SPI_FIFO_FULL)) && spi_slave->left_tx_word)
			spi_slave->tx_word(spi_slave);
		writel(SIRFSOC_SPI_TXFIFO_EMPTY |
			SIRFSOC_SPI_TX_UFLOW_INT_EN |
			SIRFSOC_SPI_RX_OFLOW_INT_EN |
			SIRFSOC_SPI_FRM_END_INT_EN,
			spi_slave->base + SIRFSOC_SPI_INT_EN);
		writel(SIRFSOC_SPI_RX_EN | SIRFSOC_SPI_TX_EN,
			spi_slave->base + SIRFSOC_SPI_TX_RX_EN);
		wait_for_completion(&spi_slave->tx_done);
		wait_for_completion(&spi_slave->rx_done);
		while (!(readl(spi_slave->base + SIRFSOC_SPI_INT_STATUS)
			& 0x01))
			;
		while (!((readl(spi_slave->base + SIRFSOC_SPI_RXFIFO_STATUS)
			& SIRFSOC_SPI_FIFO_EMPTY)) && spi_slave->left_rx_word)
			spi_slave->rx_word(spi_slave);
		writel(0, spi_slave->base + SIRFSOC_SPI_RXFIFO_OP);
		writel(0, spi_slave->base + SIRFSOC_SPI_TXFIFO_OP);
	} while (spi_slave->left_tx_word != 0 || spi_slave->left_rx_word != 0);
	spi_slave->tx = ptx;
	spi_slave->rx = prx;
}

static int sirfsoc_spi_slave_setup(struct sirfsoc_spi_slave *spi_slave)
{
	u8 bits_per_word = 0;
	int hz = 0;
	u32 regval;
	u32 txfifo_ctrl, rxfifo_ctrl;

	if (!spi_slave->bits_per_word)
		spi_slave->bits_per_word = 8;
	if (!spi_slave->max_speed_hz)
		spi_slave->max_speed_hz = 1000000;
	bits_per_word = spi_slave->bits_per_word;
	hz = spi_slave->max_speed_hz;
	regval = (spi_slave->ctrl_freq / (2 * hz)) - 1;
	if (regval > 0xFFFF) {
		pr_err("Speed %d not supported\n", hz);
		return -EINVAL;
	}
	switch (bits_per_word) {
	case 8:
		regval |= SIRFSOC_SPI_TRAN_DAT_FORMAT_8;
		spi_slave->rx_word = spi_sirfsoc_rx_word_u8;
		spi_slave->tx_word = spi_sirfsoc_tx_word_u8;
		break;
	case 12:
	case 16:
		regval |= (bits_per_word ==  12) ?
			SIRFSOC_SPI_TRAN_DAT_FORMAT_12 :
			SIRFSOC_SPI_TRAN_DAT_FORMAT_16;
		spi_slave->rx_word = spi_sirfsoc_rx_word_u16;
		spi_slave->tx_word = spi_sirfsoc_tx_word_u16;
		break;
	case 32:
		regval |= SIRFSOC_SPI_TRAN_DAT_FORMAT_32;
		spi_slave->rx_word = spi_sirfsoc_rx_word_u32;
		spi_slave->tx_word = spi_sirfsoc_tx_word_u32;
		break;
	default:
		pr_err("Bits per word %d not supported\n", bits_per_word);
		return -EINVAL;
	}
	spi_slave->word_width = DIV_ROUND_UP(bits_per_word, 8);
	txfifo_ctrl = SIRFSOC_SPI_FIFO_THD(SIRFSOC_SPI_FIFO_SIZE / 2) |
					   (spi_slave->word_width >> 1);
	rxfifo_ctrl = SIRFSOC_SPI_FIFO_THD(SIRFSOC_SPI_FIFO_SIZE / 2) |
					   (spi_slave->word_width >> 1);
	/* cs low active*/
	if (!(spi_slave->mode & SPI_CS_HIGH))
		regval |= SIRFSOC_SPI_CS_IDLE_STAT;
	else /* cs high active */
		regval &= ~SIRFSOC_SPI_CS_IDLE_STAT;
	/* msb mode */
	if (!(spi_slave->mode & SPI_LSB_FIRST))
		regval |= SIRFSOC_SPI_TRAN_MSB;
	else /* lsb mode */
		regval &= ~SIRFSOC_SPI_TRAN_MSB;
	/* clock idle: high */
	if (spi_slave->mode & SPI_CPOL)
		regval |= SIRFSOC_SPI_CLK_IDLE_STAT;
	else /* clock idle: low */
		regval &= ~SIRFSOC_SPI_CLK_IDLE_STAT;
	if (spi_slave->mode & SPI_LOOP) {
		pr_err("mode: SPI_LOOP not supported\n");
		return -EINVAL;
	}
	/*
	 * Data should be driven at least 1/2 cycle before the fetch edge
	 * to make sure that data gets stable at the fetch edge.
	 */
	if (((spi_slave->mode & SPI_CPOL) && (spi_slave->mode & SPI_CPHA)) ||
	    (!(spi_slave->mode & SPI_CPOL) && !(spi_slave->mode & SPI_CPHA)))
		regval &= ~SIRFSOC_SPI_DRV_POS_EDGE;
	else
		regval |= SIRFSOC_SPI_DRV_POS_EDGE;
	writel(regval | SIRFSOC_SPI_SLV_MODE,
		spi_slave->base + SIRFSOC_SPI_CTRL);
	writel(SIRFSOC_SPI_FIFO_SC(0x30) |
		SIRFSOC_SPI_FIFO_LC(0x20) |
		SIRFSOC_SPI_FIFO_HC(0x10),
		spi_slave->base + SIRFSOC_SPI_TXFIFO_LEVEL_CHK);
	writel(SIRFSOC_SPI_FIFO_SC(0x1) |
		SIRFSOC_SPI_FIFO_LC(0x2) |
		SIRFSOC_SPI_FIFO_HC(0x3),
		spi_slave->base + SIRFSOC_SPI_RXFIFO_LEVEL_CHK);
	writel(txfifo_ctrl, spi_slave->base + SIRFSOC_SPI_TXFIFO_CTRL);
	writel(rxfifo_ctrl, spi_slave->base + SIRFSOC_SPI_RXFIFO_CTRL);

	return 0;
}

static ssize_t spi_slave_read(struct file *flip,
		char __user *data, size_t len, loff_t *pos)
{
	struct sirfsoc_spi_slave *spi_slave;
	int missing, i, ret;
	u8 *ptr;

	if (len > 2 * PAGE_SIZE) {
		pr_info("Please use data len less than %ld\n", 2 * PAGE_SIZE);
		ret = -EINVAL;
		goto exit_read;
	}
	spi_slave = get_spi_slave(flip->private_data);
	spi_slave->left_rx_word = spi_slave->left_tx_word =
				len / spi_slave->word_width;
	reinit_completion(&spi_slave->rx_done);
	reinit_completion(&spi_slave->tx_done);
	/*
	 * used in PIO mode while transfer less than 256 bytes;
	 * more than 256 bytes use DMA mode.
	 */
	if (len < 256) {
		writel(SIRFSOC_SPI_IO_MODE_SEL,
			spi_slave->base + SIRFSOC_SPI_TX_DMA_IO_CTRL);
		writel(SIRFSOC_SPI_IO_MODE_SEL,
			spi_slave->base + SIRFSOC_SPI_RX_DMA_IO_CTRL);
		sirfsoc_spi_slave_pio_transfer(spi_slave);
	} else {
		writel(0x0,
			spi_slave->base + SIRFSOC_SPI_TX_DMA_IO_CTRL);
		writel(0x0,
			spi_slave->base + SIRFSOC_SPI_RX_DMA_IO_CTRL);
		spi_sirfsoc_dma_transfer(spi_slave);
	}
	missing = copy_to_user(data, spi_slave->rx, len);
	if (missing == len) {
		ret = -EFAULT;
		goto exit_read;
	}
	ret = len - missing;
	ptr = spi_slave->rx;
	for (i = 0; i < len; i++)
		spi_dbg("%X ", *ptr++);
exit_read:
	return ret;
}

static ssize_t spi_slave_write(struct file *flip,
		const char __user *data, size_t len, loff_t *pos)
{
	struct sirfsoc_spi_slave *spi_slave;
	int missing, i, ret;
	u8 *ptr;

	spi_slave = get_spi_slave(flip->private_data);
	if (len > 2 * PAGE_SIZE) {
		pr_info("Please use data len less than %ld\n", 2 * PAGE_SIZE);
		ret = -EINVAL;
		goto exit_write;
	}
	memset(spi_slave->tx, 0, len);
	memset(spi_slave->rx, 0, len);
	missing = copy_from_user(spi_slave->tx, data, len);
	if (missing) {
		ret = -EFAULT;
		goto exit_write;
	}
	ret = len - missing;
	ptr = spi_slave->tx;
	for (i = 0; i < len; i++)
		spi_dbg("%X ", *ptr++);
	spi_slave->left_tx_word = spi_slave->left_rx_word =
				len / spi_slave->word_width;
	reinit_completion(&spi_slave->rx_done);
	reinit_completion(&spi_slave->tx_done);
	/* PIO mode transfer less than 256 bytes */
	if (len <= 256) {
		writel(SIRFSOC_SPI_IO_MODE_SEL,
			spi_slave->base + SIRFSOC_SPI_TX_DMA_IO_CTRL);
		writel(SIRFSOC_SPI_IO_MODE_SEL,
			spi_slave->base + SIRFSOC_SPI_RX_DMA_IO_CTRL);
		sirfsoc_spi_slave_pio_transfer(spi_slave);
	} else {
		writel(0x0, spi_slave->base + SIRFSOC_SPI_TX_DMA_IO_CTRL);
		writel(0x0, spi_slave->base + SIRFSOC_SPI_RX_DMA_IO_CTRL);
		spi_sirfsoc_dma_transfer(spi_slave);
	}
exit_write:
	return ret;
}

static int spi_slave_open(struct inode *inode, struct file *flip)
{
	struct sirfsoc_spi_slave *spi_slave;

	spi_slave = get_spi_slave(flip->private_data);
	memset((char *)spi_slave->tx, 0, 2 * PAGE_SIZE);
	memset(spi_slave->rx, 0, 2 * PAGE_SIZE);
	/* while open it, set it in slave mode */
	writel(readl(spi_slave->base + SIRFSOC_SPI_CTRL) |
			SIRFSOC_SPI_SLV_MODE,
			spi_slave->base + SIRFSOC_SPI_CTRL);

	return 0;
}

static int spi_slave_release(struct inode *inode, struct file *flip)
{
	struct sirfsoc_spi_slave *spi_slave;

	spi_slave = get_spi_slave(flip->private_data);
	writel(0x0, spi_slave->base + SIRFSOC_SPI_INT_EN);
	writel(SIRFSOC_SPI_INT_MASK_ALL,
			spi_slave->base + SIRFSOC_SPI_INT_STATUS);

	return 0;
}

static long spi_slave_ioctl(struct file *flip,
		unsigned int cmd, unsigned long arg)
{
	int			err = 0;
	int			retval = 0;
	u32			tmp;
	struct sirfsoc_spi_slave *spi_slave;

	/* Check type and command number */
	if (_IOC_TYPE(cmd) != SPI_IOC_MAGIC)
		return -ENOTTY;

	/* Check access direction once here; don't repeat below.
	 * IOC_DIR is from the user perspective, while access_ok is
	 * from the kernel perspective; so they look reversed.
	 */
	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok(VERIFY_WRITE,
				(void __user *)arg, _IOC_SIZE(cmd));
	if (err == 0 && _IOC_DIR(cmd) & _IOC_WRITE)
		err = !access_ok(VERIFY_READ,
				(void __user *)arg, _IOC_SIZE(cmd));
	if (err)
		return -EFAULT;

	spi_slave = get_spi_slave(flip->private_data);
	switch (cmd) {
	/* read requests */
	case SPI_IOC_RD_MODE:
		retval = __put_user(spi_slave->mode & SPI_MODE_MASK,
					(__u8 __user *)arg);
		break;
	case SPI_IOC_RD_BITS_PER_WORD:
		retval = __put_user(spi_slave->bits_per_word,
					(__u8 __user *)arg);
		break;
	case SPI_IOC_RD_MAX_SPEED_HZ:
		retval = __put_user(spi_slave->max_speed_hz,
					(__u32 __user *)arg);
		break;

	/* write requests */
	case SPI_IOC_WR_MODE:
		retval = __get_user(tmp, (u8 __user *)arg);
		if (retval == 0) {
			u8	save = spi_slave->mode;

			if (tmp & ~SPI_MODE_MASK) {
				retval = -EINVAL;
				break;
			}

			tmp |= spi_slave->mode & ~SPI_MODE_MASK;
			spi_slave->mode = (u8)tmp;
			retval = sirfsoc_spi_slave_setup(spi_slave);
			if (retval < 0)
				spi_slave->mode = save;
			else
				spi_dbg("spi mode %02x\n", tmp);
		}
		break;
	case SPI_IOC_WR_BITS_PER_WORD:
		retval = __get_user(tmp, (__u8 __user *)arg);
		if (retval == 0) {
			u8	save = spi_slave->bits_per_word;

			spi_slave->bits_per_word = tmp;
			retval = sirfsoc_spi_slave_setup(spi_slave);
			if (retval < 0)
				spi_slave->bits_per_word = save;
			else
				spi_dbg("%d bits per word\n", tmp);
		}
		break;
	case SPI_IOC_WR_MAX_SPEED_HZ:
		retval = __get_user(tmp, (__u32 __user *)arg);
		if (retval == 0) {
			u32	save = spi_slave->max_speed_hz;

			spi_slave->max_speed_hz = tmp;
			retval = sirfsoc_spi_slave_setup(spi_slave);
			if (retval < 0)
				spi_slave->max_speed_hz = save;
			else
				spi_dbg("%d Hz (max)\n", tmp);
		}
		break;

	default:
		break;
	}

	return retval;
}

static const struct file_operations spi_slave_fops = {
	.owner		= THIS_MODULE,
	.read		= spi_slave_read,
	.write		= spi_slave_write,
	.open		= spi_slave_open,
	.release	= spi_slave_release,
	.unlocked_ioctl	= spi_slave_ioctl,
};

static struct miscdevice spi_slave_dev = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "sirf-spi-slave",
	.fops		= &spi_slave_fops,
};

static int spi_sirfsoc_probe(struct platform_device *pdev)
{
	struct sirfsoc_spi_slave *spi_slave;
	struct resource *mem_res;
	int irq, ret;

	spi_slave = devm_kzalloc(&pdev->dev, sizeof(*spi_slave), GFP_KERNEL);
	if (!spi_slave) {
		ret = -ENOMEM;
		goto err;
	}
	platform_set_drvdata(pdev, spi_slave);
	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	spi_slave->base = devm_ioremap_resource(&pdev->dev, mem_res);
	if (IS_ERR(spi_slave->base)) {
		ret = PTR_ERR(spi_slave->base);
		goto err;
	}
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = -ENXIO;
		goto err;
	}
	ret = devm_request_irq(&pdev->dev, irq, spi_sirfsoc_irq, 0,
				DRIVER_NAME, spi_slave);
	if (ret)
		goto err;
	spi_slave->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(spi_slave->clk)) {
		ret = PTR_ERR(spi_slave->clk);
		goto err;
	}
	clk_prepare_enable(spi_slave->clk);
	spi_slave->ctrl_freq = clk_get_rate(spi_slave->clk);

	spi_slave->rx_chan = dma_request_slave_channel(&pdev->dev, "rx");
	if (!spi_slave->rx_chan) {
		dev_err(&pdev->dev, "can not allocate rx dma channel\n");
		ret = -ENODEV;
		goto err;
	}
	spi_slave->tx_chan = dma_request_slave_channel(&pdev->dev, "tx");
	if (!spi_slave->tx_chan) {
		dev_err(&pdev->dev, "can not allocate tx dma channel\n");
		ret = -ENODEV;
		goto free_rx_dma;
	}
	init_completion(&spi_slave->rx_done);
	init_completion(&spi_slave->tx_done);
	spi_slave->tx = devm_kzalloc(&pdev->dev, 2 * PAGE_SIZE, GFP_KERNEL);
	if (!spi_slave->tx) {
		ret = -ENOMEM;
		goto free_tx_dma;
	}
	spi_slave->rx = devm_kzalloc(&pdev->dev, 2 * PAGE_SIZE, GFP_KERNEL);
	if (!spi_slave->rx) {
		ret = -ENOMEM;
		goto free_tx_dma;
	}
	spi_slave->misc = spi_slave_dev;
	if (misc_register(&spi_slave->misc)) {
		ret = -ENODEV;
		goto free_tx_dma;
	}
	dev_info(&pdev->dev, "spi slave registerred\n");

	return 0;
free_tx_dma:
	dma_release_channel(spi_slave->tx_chan);
free_rx_dma:
	dma_release_channel(spi_slave->rx_chan);
err:
	return ret;
}

static int  spi_sirfsoc_remove(struct platform_device *pdev)
{
	struct sirfsoc_spi_slave *spi_slave;

	spi_slave = platform_get_drvdata(pdev);
	clk_disable_unprepare(spi_slave->clk);
	dma_release_channel(spi_slave->rx_chan);
	dma_release_channel(spi_slave->tx_chan);
	misc_deregister(&spi_slave->misc);

	return 0;
}

static const struct of_device_id spi_sirfsoc_of_match[] = {
	{ .compatible = "sirf,prima2-spi-slave", },
	{}
};
MODULE_DEVICE_TABLE(of, spi_sirfsoc_of_match);

static struct platform_driver spi_sirfsoc_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = spi_sirfsoc_of_match,
	},
	.probe = spi_sirfsoc_probe,
	.remove = spi_sirfsoc_remove,
};
module_platform_driver(spi_sirfsoc_driver);
MODULE_DESCRIPTION("SiRF SoC SPI slave driver");
MODULE_LICENSE("GPL v2");
