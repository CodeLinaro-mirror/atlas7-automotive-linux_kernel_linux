/*
 * SPI bus driver for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/bitops.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/spi/spi-sirf.h>

#define DRIVER_NAME "sirfsoc_spi"

#define SPI_CTRL		0x0000	/* SPI controller configuration register */
#define SPI_CMD			0x0004	/* SPI command register */
#define SPI_TX_RX_EN		0x0008	/* SPI interface transfer enable register */
#define SPI_INT_EN		0x000C	/* SPI interrupt enable register */
#define SPI_INT_STATUS		0x0010	/* SPI interrupt register */
#define SPI_TX_DMA_IO_CTRL	0x0100	/* SPI TXFIFO DMA/IO register */
#define SPI_TX_DMA_IO_LEN	0x0104	/* SPI transmit data length register */
#define SPI_TXFIFO_CTRL		0x0108	/* SPI TXFIFO control register */
#define SPI_TXFIFO_LEVEL_CHK	0x010C	/* SPI TXFIFO check level register */
#define SPI_TXFIFO_OP		0x0110	/* SPI TXFIFO operation register */
#define SPI_TXFIFO_STATUS	0x0114	/* SPI TXFIFO status register */
#define SPI_TXFIFO_DATA		0x0118	/* SPI TXFIFO bottom */
#define SPI_RX_DMA_IO_CTRL	0x0120	/* SPI RXFIFO DMA/IO register */
#define SPI_RX_DMA_IO_LEN	0x0124	/* SPI receive length register */
#define SPI_RXFIFO_CTRL		0x0128	/* SPI RXFIFO control register */
#define SPI_RXFIFO_LEVEL_CHK	0x012C	/* SPI RXFIFO check level register */
#define SPI_RXFIFO_OP		0x0130	/* SPI RXFIFO operation register */
#define SPI_RXFIFO_STATUS	0x0134	/* SPI RXFIFO status register */
#define SPI_RXFIFO_DATA		0x0138	/* SPI RXFIFO bottom */
#define SLV_RX_SAMPLE_MODE	0x0140	/* Rx sample mode when slave mode */
#define DUMMY_DELAY_CTRL	0x0144	/* Control reg when insert dummy delay */

/* SPI CTRL register defines */
#define SLV_MODE		BIT(16)
#define CMD_MODE		BIT(17)
#define CS_IO_OUT		BIT(18)
#define CS_IO_MODE		BIT(19)
#define CLK_IDLE_STAT		BIT(20)
#define CS_IDLE_STAT		BIT(21)
#define TRAN_MSB		BIT(22)
#define DRV_POS_EDGE		BIT(23)
#define CS_HOLD_TIME		BIT(24)
#define CLK_SAMPLE_MODE		BIT(25)
#define TRAN_DAT_FORMAT_8	(0 << 26)
#define TRAN_DAT_FORMAT_12	(1 << 26)
#define TRAN_DAT_FORMAT_16	(2 << 26)
#define TRAN_DAT_FORMAT_32	(3 << 26)
#define CMD_BYTE_NUM(x)		((x & 3) << 28)
#define ENA_AUTO_CLR		BIT(30)
#define MUL_DAT_MODE		BIT(31)

/* Interrupt Enable */
#define RX_DONE_INT_EN		BIT(0)
#define TX_DONE_INT_EN		BIT(1)
#define RX_OFLOW_INT_EN		BIT(2)
#define TX_UFLOW_INT_EN		BIT(3)
#define RX_IO_DMA_INT_EN	BIT(4)
#define TX_IO_DMA_INT_EN	BIT(5)
#define RXFIFO_FULL_INT_EN	BIT(6)
#define TXFIFO_EMPTY_INT_EN	BIT(7)
#define RXFIFO_THD_INT_EN	BIT(8)
#define TXFIFO_THD_INT_EN	BIT(9)
#define FRM_END_INT_EN		BIT(10)

#define INT_MASK_ALL		0x1FFF

/* Interrupt status */
#define RX_DONE			BIT(0)
#define TX_DONE			BIT(1)
#define RX_OFLOW		BIT(2)
#define TX_UFLOW		BIT(3)
#define DMA_IO_RX_DONE		BIT(4)
#define DMA_IO_TX_DONE		BIT(5)
#define RXFIFO_FULL		BIT(6)
#define TXFIFO_EMPTY		BIT(7)
#define RXFIFO_THD_REACH	BIT(8)
#define TXFIFO_THD_REACH	BIT(9)
#define FRM_END			BIT(10)

/* TX RX enable */
#define SPI_RX_EN		BIT(0)
#define SPI_TX_EN		BIT(1)
#define SPI_CMD_TX_EN		BIT(2)

#define IO_MODE_SEL		BIT(0)
#define RX_DMA_FLUSH		BIT(2)

/* FIFO OPs */
#define FIFO_RESET		BIT(0)
#define FIFO_START		BIT(1)

/* FIFO CTRL */
#define FIFO_WIDTH_BYTE		(0 << 0)
#define FIFO_WIDTH_WORD		(1 << 0)
#define FIFO_WIDTH_DWORD	(2 << 0)

/* FIFO Status */
#define	FIFO_LEVEL_MASK		0xFF
#define FIFO_FULL		BIT(8)
#define FIFO_EMPTY		BIT(9)

/* 256 bytes rx/tx FIFO */
#define FIFO_SIZE		256
#define DATA_FRAME_LEN_MAX	(64 * 1024)

#define FIFO_SC(x)		((x) & 0x3F)
#define FIFO_LC(x)		(((x) & 0x3F) << 10)
#define FIFO_HC(x)		(((x) & 0x3F) << 20)
#define FIFO_THD(x)		(((x) & 0xFF) << 2)

struct sirfsoc_spi {
	struct spi_bitbang bitbang;
	struct completion done;

	u32 irq;
	void __iomem *base;
	u32 ctrl_freq;  /* SPI controller clock speed */
	struct clk *clk;
	int bus_num;
	struct pinmux *pmx;

	/* rx & tx bufs from the spi_transfer */
	const void *tx;
	void *rx;

	/* place received word into rx buffer */
	void (*enq_rx_word) (u32 rx_data, struct sirfsoc_spi *);
	/* get word from tx buffer for sending */
	 u32(*pop_tx_word) (struct sirfsoc_spi *);

	/* number of words left to be tranmitted/received */
	unsigned int left_tx_cnt;
	unsigned int left_rx_cnt;

	/* tasklet to push tx msg into FIFO */
	struct tasklet_struct tasklet_tx;
};

static void spi_sirfsoc_rx_buf_u8(u32 data, struct sirfsoc_spi *sspi)
{
	u8 *rx = sspi->rx;
	*rx++ = (u8) data;
	sspi->rx = rx;
}

static u32 spi_sirfsoc_tx_buf_u8(struct sirfsoc_spi *sspi)
{
	u32 data;
	const u8 *tx = sspi->tx;
	data = *tx++;
	sspi->tx = tx;
	return data;
}

static void spi_sirfsoc_rx_buf_u16(u32 data, struct sirfsoc_spi *sspi)
{
	u16 *rx = sspi->rx;
	*rx++ = (u16) data;
	sspi->rx = rx;
}

static u32 spi_sirfsoc_tx_buf_u16(struct sirfsoc_spi *sspi)
{
	u32 data;
	const u16 *tx = sspi->tx;
	data = *tx++;
	sspi->tx = tx;
	return data;
}

static void spi_sirfsoc_rx_buf_u32(u32 data, struct sirfsoc_spi *sspi)
{
	u32 *rx = sspi->rx;
	*rx++ = (u32) data;
	sspi->rx = rx;
}

static u32 spi_sirfsoc_tx_buf_u32(struct sirfsoc_spi *sspi)
{
	u32 data;
	const u32 *tx = sspi->tx;
	data = *tx++;
	sspi->tx = tx;
	return data;
}

static void spi_sirfsoc_tasklet_tx(unsigned long arg)
{
	struct sirfsoc_spi *sspi = (struct sirfsoc_spi *)arg;
	u32 word = 0;

	/* Fill Tx FIFO while there are left words to be transmitted */
	while (!((readl(sspi->base + SPI_TXFIFO_STATUS) & FIFO_FULL))
	       && sspi->left_tx_cnt) {
		if (sspi->tx)
			word = sspi->pop_tx_word(sspi);
		writel(word, sspi->base + SPI_TXFIFO_DATA);
		sspi->left_tx_cnt--;
	}
}

static irqreturn_t spi_sirfsoc_irq(int irq, void *dev_id)
{
	struct sirfsoc_spi *sspi = dev_id;
	u32 spi_stat = readl(sspi->base + SPI_INT_STATUS);
	u32 word = 0;

	writel(spi_stat, sspi->base + SPI_INT_STATUS);

	/* Error Conditions */
	if (spi_stat & RX_OFLOW || spi_stat & TX_UFLOW) {
		complete(&sspi->done);
		writel(0x0, sspi->base + SPI_INT_EN);
	}

	if (spi_stat & FRM_END) {
		while (!((readl(sspi->base + SPI_RXFIFO_STATUS)
					& FIFO_EMPTY)) && sspi->left_rx_cnt) {
			word = readl(sspi->base + SPI_RXFIFO_DATA);
			if (sspi->rx)
				sspi->enq_rx_word(word, sspi);

			sspi->left_rx_cnt--;
		}

		/* Received all words */
		if ((sspi->left_rx_cnt == 0) && (sspi->left_tx_cnt == 0)) {
			complete(&sspi->done);
			writel(0x0, sspi->base + SPI_INT_EN);
		}
	}

	if (spi_stat & RXFIFO_THD_REACH || spi_stat & TXFIFO_THD_REACH ||
		spi_stat & RXFIFO_FULL || spi_stat & TXFIFO_EMPTY)
		tasklet_schedule(&sspi->tasklet_tx);

	return IRQ_HANDLED;
}

static int spi_sirfsoc_transfer(struct spi_device *spi, struct spi_transfer *t)
{
	struct sirfsoc_spi *sspi;
	u32 word = 0;
	int timeout = t->len * 10;
	sspi = spi_master_get_devdata(spi->master);

	sspi->tx = t->tx_buf;
	sspi->rx = t->rx_buf;
	sspi->left_tx_cnt = sspi->left_rx_cnt = t->len;
	INIT_COMPLETION(sspi->done);

	writel(INT_MASK_ALL, sspi->base + SPI_INT_STATUS);

	if (t->len == 1) {
		writel(readl(sspi->base + SPI_CTRL) | ENA_AUTO_CLR,
		       sspi->base + SPI_CTRL);
		writel(0, sspi->base + SPI_TX_DMA_IO_LEN);
		writel(0, sspi->base + SPI_RX_DMA_IO_LEN);
	} else if ((t->len > 1) && (t->len < DATA_FRAME_LEN_MAX)) {
		writel(readl(sspi->base + SPI_CTRL) | MUL_DAT_MODE |
		       ENA_AUTO_CLR, sspi->base + SPI_CTRL);
		writel(t->len - 1, sspi->base + SPI_TX_DMA_IO_LEN);
		writel(t->len - 1, sspi->base + SPI_RX_DMA_IO_LEN);
	} else {
		writel(readl(sspi->base + SPI_CTRL),
			sspi->base + SPI_CTRL);
		writel(0, sspi->base + SPI_TX_DMA_IO_LEN);
		writel(0, sspi->base + SPI_RX_DMA_IO_LEN);
	}

	writel(FIFO_RESET, sspi->base + SPI_RXFIFO_OP);
	writel(FIFO_RESET, sspi->base + SPI_TXFIFO_OP);
	writel(FIFO_START, sspi->base + SPI_RXFIFO_OP);
	writel(FIFO_START, sspi->base + SPI_TXFIFO_OP);

	/* fill up the Tx FIFO */
	while (!(readl(sspi->base + SPI_TXFIFO_STATUS) & FIFO_FULL) &&
		(sspi->left_tx_cnt > 0)) {
		if (sspi->tx)
			word = sspi->pop_tx_word(sspi);
		writel(word, sspi->base + SPI_TXFIFO_DATA);
		sspi->left_tx_cnt--;
	}
	writel(RX_OFLOW_INT_EN | TX_UFLOW_INT_EN | RXFIFO_THD_INT_EN |
		TXFIFO_THD_INT_EN | FRM_END_INT_EN | RXFIFO_FULL_INT_EN |
		TXFIFO_EMPTY_INT_EN, sspi->base + SPI_INT_EN);
	writel(SPI_RX_EN | SPI_TX_EN, sspi->base + SPI_TX_RX_EN);

	if (wait_for_completion_timeout(&sspi->done, timeout) == 0)
		dev_err(&spi->dev, "transfer timeout\n");

	/* TX, RX FIFO stop */
	writel(0, sspi->base + SPI_RXFIFO_OP);
	writel(0, sspi->base + SPI_TXFIFO_OP);
	writel(0, sspi->base + SPI_TX_RX_EN);
	writel(0, sspi->base + SPI_INT_EN);

	return t->len - sspi->left_rx_cnt;
}

static void spi_sirfsoc_chipselect(struct spi_device *spi, int value)
{
	struct sirfsoc_spi *sspi = spi_master_get_devdata(spi->master);
	struct sirfsoc_spi_ctrldata *ctl_data = spi->controller_data;
	u32 regval = readl(sspi->base + SPI_CTRL);

	switch (value) {
	case BITBANG_CS_ACTIVE:
		if (ctl_data->cs_type == CS_HW_CTRL) {
			/*
			 * In hardware control mode, CS output is controlled
			 * by the CS hardware logic
			 */
			regval &= ~CS_IO_OUT;
			if (ctl_data->cs_hold_clk == CS_HOLD_2)
				regval |= CS_HOLD_TIME;
		} else if (ctl_data->cs_type == CS_RISC_IO) {
			/*
			 * In I/O mode, CS outputs the value of the CS_IO_OUT bit
			 */
			regval |= CS_IO_OUT;
			if (spi->mode & SPI_CS_HIGH)
				regval |= CS_IO_OUT;
		} else if (ctl_data->cs_type == CS_GPIO)
			ctl_data->chip_select();
		break;
	case BITBANG_CS_INACTIVE:
		if (ctl_data->cs_type == CS_RISC_IO) {
			if (spi->mode & SPI_CS_HIGH)
				regval &= ~CS_IO_OUT;
			else
				regval |= CS_IO_OUT;
		} else if (ctl_data->cs_type == CS_GPIO)
			ctl_data->chip_deselect();
		break;
	}
	writel(regval, sspi->base + SPI_CTRL);
}

static int
spi_sirfsoc_setup_transfer(struct spi_device *spi, struct spi_transfer *t)
{
	struct sirfsoc_spi *sspi;
	u8 bits_per_word = 0;
	int hz = 0;
	u32 regval;
	u32 txfifo_ctrl, rxfifo_ctrl;
	u32 fifo_size = FIFO_SIZE / 4;

	sspi = spi_master_get_devdata(spi->master);

	bits_per_word = t && t->bits_per_word ? t->bits_per_word :
		spi->bits_per_word;
	hz = t && t->speed_hz ? t->speed_hz : spi->max_speed_hz;

	/* Enable IO mode for RX, TX */
	writel(IO_MODE_SEL, sspi->base + SPI_TX_DMA_IO_CTRL);
	writel(IO_MODE_SEL, sspi->base + SPI_RX_DMA_IO_CTRL);
	regval = (sspi->ctrl_freq / (2 * hz)) - 1;

	if (regval > 0xFFFF || regval < 0) {
		dev_err(&spi->dev, "Speed %d not supported\n", hz);
		return -EINVAL;
	}

	switch (bits_per_word) {
	case 8:
		regval |= TRAN_DAT_FORMAT_8;
		sspi->enq_rx_word = spi_sirfsoc_rx_buf_u8;
		sspi->pop_tx_word = spi_sirfsoc_tx_buf_u8;
		txfifo_ctrl = FIFO_THD(FIFO_SIZE / 2) | FIFO_WIDTH_BYTE;
		rxfifo_ctrl = FIFO_THD(FIFO_SIZE / 2) | FIFO_WIDTH_BYTE;
		break;
	case 12:
	case 16:
		regval |= (bits_per_word ==  12) ? TRAN_DAT_FORMAT_12 :
			TRAN_DAT_FORMAT_16;
		sspi->enq_rx_word = spi_sirfsoc_rx_buf_u16;
		sspi->pop_tx_word = spi_sirfsoc_tx_buf_u16;
		txfifo_ctrl = FIFO_THD(FIFO_SIZE / 2) | FIFO_WIDTH_WORD;
		rxfifo_ctrl = FIFO_THD(FIFO_SIZE / 2) | FIFO_WIDTH_WORD;
		break;
	case 32:
		regval |= TRAN_DAT_FORMAT_32;
		sspi->enq_rx_word = spi_sirfsoc_rx_buf_u32;
		sspi->pop_tx_word = spi_sirfsoc_tx_buf_u32;
		txfifo_ctrl = FIFO_THD(FIFO_SIZE / 2) | FIFO_WIDTH_DWORD;
		rxfifo_ctrl = FIFO_THD(FIFO_SIZE / 2) | FIFO_WIDTH_DWORD;
		break;
	default:
		dev_err(&spi->dev, "Bits per word %d not supported\n",
		       bits_per_word);
		return -EINVAL;
	}

	if (!(spi->mode & SPI_CS_HIGH))
		regval |= CS_IDLE_STAT;
	if (!(spi->mode & SPI_LSB_FIRST))
		regval |= TRAN_MSB;
	if (spi->mode & SPI_CPOL)
		regval |= CLK_IDLE_STAT;

	/*
	 * Data should be driven at least 1/2 cycle before the fetch edge to make
	 * sure that data gets stable at the fetch edge.
	 */
	if (((spi->mode & SPI_CPOL) && (spi->mode & SPI_CPHA)) ||
	    (!(spi->mode & SPI_CPOL) && !(spi->mode & SPI_CPHA)))
		regval &= ~DRV_POS_EDGE;
	else
		regval |= DRV_POS_EDGE;

	writel(FIFO_SC(fifo_size - 2) | FIFO_LC(fifo_size / 2) | FIFO_HC(2),
	       sspi->base + SPI_TXFIFO_LEVEL_CHK);
	writel(FIFO_SC(2) | FIFO_LC(fifo_size / 2) | FIFO_HC(fifo_size - 2),
	       sspi->base + SPI_RXFIFO_LEVEL_CHK);
	writel(txfifo_ctrl, sspi->base + SPI_TXFIFO_CTRL);
	writel(rxfifo_ctrl, sspi->base + SPI_RXFIFO_CTRL);

	writel(regval, sspi->base + SPI_CTRL);
	return 0;
}

static int spi_sirfsoc_setup(struct spi_device *spi)
{
	struct spi_bitbang *bitbang;
	struct sirfsoc_spi *sspi;

	if (!spi->max_speed_hz)
		return -EINVAL;

	sspi = spi_master_get_devdata(spi->master);
	bitbang = &sspi->bitbang;

	if (!spi->bits_per_word)
		spi->bits_per_word = 8;

	return spi_sirfsoc_setup_transfer(spi, NULL);
}

static int __devinit spi_sirfsoc_probe(struct platform_device *dev)
{
	struct sirfsoc_spi *sspi;
	struct spi_master *master;
	struct resource *mem_res;
	int ret;

	master = spi_alloc_master(&dev->dev, sizeof(*sspi));
	if (master == NULL) {
		dev_err(&dev->dev, "Unable to allocate SPI master\n");
		return -ENOMEM;
	}
	platform_set_drvdata(dev, master);
	sspi = spi_master_get_devdata(master);

	mem_res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (mem_res == NULL) {
		dev_err(&dev->dev, "Unable to get IO resource\n");
		ret = -ENOMEM;
		goto free_master;
	}

	sspi->base = devm_request_and_ioremap(&dev->dev, mem_res);
	if (sspi->base == NULL) {
		dev_err(&dev->dev, "IO remap failed!\n");
		ret = -ENOMEM;
		goto free_master;
	}

	if (of_property_read_u32(dev->dev.of_node, "cell-index", &dev->id)) {
		dev_err(&dev->dev, "Fail to get index\n");
		ret = -ENODEV;
		goto free_master;
	}

	sspi->irq = platform_get_irq(dev, 0);
	if (sspi->irq < 0) {
		ret = -ENODEV;
		goto free_master;
	}
	ret = devm_request_irq(&dev->dev, sspi->irq, spi_sirfsoc_irq, 0, DRIVER_NAME, sspi);
	if (ret)
		goto free_master;

	sspi->bitbang.master = spi_master_get(master);
	sspi->bitbang.chipselect = spi_sirfsoc_chipselect;
	sspi->bitbang.setup_transfer = spi_sirfsoc_setup_transfer;
	sspi->bitbang.txrx_bufs = spi_sirfsoc_transfer;
	sspi->bitbang.master->setup = spi_sirfsoc_setup;
	sspi->bitbang.master->num_chipselect = 0xFFFF;
	master->bus_num = dev->id;
	sspi->bitbang.master->dev.of_node = dev->dev.of_node;

	sspi->pmx = pinmux_get(&dev->dev, NULL);
	ret = IS_ERR(sspi->pmx);
	if (ret)
		goto free_master;

	pinmux_enable(sspi->pmx);

	sspi->clk = clk_get(&dev->dev, NULL);
	if (IS_ERR(sspi->clk)) {
		ret = -EINVAL;
		goto free_pmx;
	}
	clk_enable(sspi->clk);
	sspi->ctrl_freq = clk_get_rate(sspi->clk);

	init_completion(&sspi->done);

	tasklet_init(&sspi->tasklet_tx, spi_sirfsoc_tasklet_tx,
		     (unsigned long)sspi);

	writel(FIFO_RESET, sspi->base + SPI_RXFIFO_OP);
	writel(FIFO_RESET, sspi->base + SPI_TXFIFO_OP);
	writel(FIFO_START, sspi->base + SPI_RXFIFO_OP);
	writel(FIFO_START, sspi->base + SPI_TXFIFO_OP);
	writel(0, sspi->base + DUMMY_DELAY_CTRL);	/* We are not using dummy delay between command and data */

	ret = spi_bitbang_start(&sspi->bitbang);
	if (ret != 0)
		goto free_clk;

	dev_info(&dev->dev, "registerred, bus number = %d\n", master->bus_num);

	return 0;

free_clk:
	clk_disable(sspi->clk);
	clk_put(sspi->clk);
free_pmx:
	pinmux_disable(sspi->pmx);
	pinmux_put(sspi->pmx);
free_master:
	spi_master_put(master);

	return ret;
}

static int  __devexit spi_sirfsoc_remove(struct platform_device *dev)
{
	struct spi_master *master;
	struct sirfsoc_spi *sspi;

	master = platform_get_drvdata(dev);
	sspi = spi_master_get_devdata(master);

	spi_bitbang_stop(&sspi->bitbang);
	clk_disable(sspi->clk);
	clk_put(sspi->clk);
	pinmux_disable(sspi->pmx);
	pinmux_put(sspi->pmx);
	spi_master_put(master);
	return 0;
}

#ifdef CONFIG_PM
static int spi_sirfsoc_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct spi_master *master = platform_get_drvdata(pdev);
	struct sirfsoc_spi *sspi = spi_master_get_devdata(master);

	clk_disable(sspi->clk);
	return 0;
}

static int spi_sirfsoc_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct spi_master *master = platform_get_drvdata(pdev);
	struct sirfsoc_spi *sspi = spi_master_get_devdata(master);

	clk_enable(sspi->clk);
	writel(FIFO_RESET, sspi->base + SPI_RXFIFO_OP);
	writel(FIFO_RESET, sspi->base + SPI_TXFIFO_OP);
	writel(FIFO_START, sspi->base + SPI_RXFIFO_OP);
	writel(FIFO_START, sspi->base + SPI_TXFIFO_OP);

	return 0;
}

static const struct dev_pm_ops spi_sirfsoc_pm_ops = {
	.suspend = spi_sirfsoc_suspend,
	.resume = spi_sirfsoc_resume,
};
#endif

static const struct of_device_id spi_sirfsoc_of_match[] = {
	{ .compatible = "sirf,prima2-spi", },
	{}
};
MODULE_DEVICE_TABLE(of, sirfsoc_spi_of_match);

static struct platform_driver spi_sirfsoc_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm     = &spi_sirfsoc_pm_ops,
#endif
		.of_match_table = spi_sirfsoc_of_match,
	},
	.probe = spi_sirfsoc_probe,
	.remove = __devexit_p(spi_sirfsoc_remove),
};

static int __init spi_sirfsoc_init(void)
{
	return platform_driver_register(&spi_sirfsoc_driver);
}
arch_initcall(spi_sirfsoc_init);

static void __exit spi_sirfsoc_exit(void)
{
	platform_driver_unregister(&spi_sirfsoc_driver);
}
module_exit(spi_sirfsoc_exit);

MODULE_DESCRIPTION("SiRF SoC SPI master driver");
MODULE_AUTHOR("Zhiwu Song <Zhiwu.Song@csr.com>, "
		"Barry Song <Baohua.Song@csr.com>");
MODULE_LICENSE("GPL v2");
