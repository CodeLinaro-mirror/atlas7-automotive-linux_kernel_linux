/*
 * atlas7-qspi.c - Quad SPI (qspi) NOR flash driver for CSRatlas7
 *
 * Copyright (c) 2014 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * JEDEC probe based on drivers/mtd/devices/m25p80.c
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <linux/mfd/syscon.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/spi-nor.h>
#include <linux/mtd/cfi.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>

#include "serial_flash_cmds.h"

#define DRIVER_NAME			"atlas7_qspi"

#define ATLAS7_SOUCRE_CLOCK		160000000

/* QSPI clock rate */
#define ATLAS7_QSPI_MAX_CLOCK_FREQ	96000000 /* 96 MHz */

#define ATLAS7_QSPI_24BIT_FLASH_SIZE	0x1000000

/*
 * QSPI_MEM_CTRL registers
 */
#define ATLAS7_QSPI_CTRL		0x0000
#define ATLAS7_QSPI_STAT		0x0004
#define ATLAS7_QSPI_ACCRR0		0x0008
#define ATLAS7_QSPI_ACCRR1		0x000C
#define ATLAS7_QSPI_ACCRR2		0x0010
#define ATLAS7_QSPI_DDPM		0x0014
#define ATLAS7_QSPI_RWDATA		0x0018
#define ATLAS7_QSPI_FFSTAT		0x001C
#define ATLAS7_QSPI_DEFMEM		0x0020
#define ATLAS7_QSPI_EXADDR		0x0024
#define ATLAS7_QSPI_MEMSPEC		0x0028
#define ATLAS7_QSPI_INTMSK		0x002C
#define ATLAS7_QSPI_INTREQ		0x0030
#define ATLAS7_QSPI_CICFG		0x0034
#define ATLAS7_QSPI_CIDR0		0x0038
#define ATLAS7_QSPI_CIDR1		0x003C
#define ATLAS7_QSPI_RDC			0x0040

/*
 * QSPI CORE registers
 */
#define ATLAS7_QSPI_DMA_SADDR		0x0800
#define ATLAS7_QSPI_DMA_FADDR		0x0804
#define ATLAS7_QSPI_DMA_LEN		0x0808
#define ATLAS7_QSPI_DMA_CST		0x080C
#define ATLAS7_QSPI_DEBUG		0x0810
#define ATLAS7_QSPI_XOTF_EN		0x0814
#define ATLAS7_QSPI_XOTF_BASE		0x0818
#define ATLAS7_QSPI_DELAY_LINE		0x081C
#define ATLAS7_QSPI_EXTMODE		0x0820


/* QSPI control register defines */
#define ATLAS7_QSPI_CLK_DELAY(x)	(((x) & 0xFF) << 0)
#define ATLAS7_QSPI_RX_FIFO_THD(x)	(((x) & 0xFF) << 8)
#define ATLAS7_QSPI_TX_FIFO_THD(x)	(((x) & 0xFF) << 16)
#define ATLAS7_QSPI_ENTER_DPM		BIT(24)
/* SPI MODE: 1: CPOL=1, CPHA=1; 0: CPOL=0, CPHA=0 */
#define ATLAS7_QSPI_SPI_MODE		BIT(25)
#define ATLAS7_QSPI_SOFT_RESET		BIT(26)
#define ATLAS7_QSPI_CLK_DIV_MASK	0xF
#define ATLAS7_QSPI_CLK_DIV(x)		(((x) &\
				ATLAS7_QSPI_CLK_DIV_MASK) << 28)

/* QSPI status register defines */
#define ATLAS7_QSPI_DATA_OUT_AV		BIT(0)
#define ATLAS7_QSPI_DATA_IN_RDY		BIT(1)
#define ATLAS7_QSPI_STATUS_DPM		BIT(2)
#define ATLAS7_QSPI_REQUEST_RDY		BIT(3)
#define ATLAS7_QSPI_FREAD_BUSY		BIT(4)
#define ATLAS7_QSPI_DEVICE_SR_OFFSET	24
#define ATLAS7_QSPI_DEVICE_SR_MASK	0xFF

/*
 * QSPI access request register1 defines
 * For erase operation
 */
#define ATLAS7_QSPI_SECTOR_ERASE	0
#define ATLAS7_QSPI_BLOCK_ERASE		1
#define ATLAS7_QSPI_CHIP_ERASE		2

/* QSPI access request register2 defines */
#define ATLAS7_QSPI_READ_REQUSET	0
#define ATLAS7_QSPI_WRITE_REQUSET	1
#define ATLAS7_QSPI_ERASE_REQUSET	2
#define ATLAS7_QSPI_FREAD_REQUSET	3
#define ATLAS7_QSPI_REQUEST_TYPE(x)	(((x) & 0xF) << 0)
#define ATLAS7_QSPI_BREAK_FREAD		BIT(4)

/* QSPI duration DPM register defines */
#define ATLAS7_QSPI_DURATION_ENTER_DPM(x)	(((x) & 0xFFFF) << 0)
#define ATLAS7_QSPI_DURATION_EXIT_DPM(x)	(((x) & 0xFFFF) << 16)

/* QSPI FIFO status register defines */
#define ATLAS7_QSPI_READ_FIFO_STATUS_MASK	0xFFFF
#define ATLAS7_QSPI_WRITE_FIFO_STATUS_MASK	0xFFFF0000

/* QSPI default memory register defines */
#define ATLAS7_QSPI_FAST_READ		0
#define ATLAS7_QSPI_READ2O		1
#define ATLAS7_QSPI_READ2IO		2
#define ATLAS7_QSPI_READ4O		3
#define ATLAS7_QSPI_READ4IO		4
#define ATLAS7_QSPI_READ_OPCODE_MASK	0x7
#define ATLAS7_QSPI_DEF_MEM_READ_OPCODE(x)	(((x) &\
				ATLAS7_QSPI_READ_OPCODE_MASK) << 0)
#define ATLAS7_QSPI_PP			0
#define ATLAS7_QSPI_PP2O		1
#define ATLAS7_QSPI_PP4O		2
#define ATLAS7_QSPI_PP4IO		3
#define ATLAS7_QSPI_WRITE_OPCODE_MASK	0x7
#define ATLAS7_QSPI_WRITE_OPCODE_OFFSET 3
#define ATLAS7_QSPI_DEF_MEM_WRITE_OPCODE(x)	(((x) &\
				ATLAS7_QSPI_WRITE_OPCODE_MASK) << 3)
#define ATLAS7_QSPI_DEF_MEM_ATTR_32	BIT(6)
#define ATLAS7_QSPI_DEF_MEM_ATTR_DPM	BIT(7)
#define ATLAS7_QSPI_DEF_MEM_CHIP_SELECT(x)	(((x) & 0x7) << 8)
#define ATLAS7_QSPI_DEF_MEM_AUTO_ID	BIT(11)

/* QSPI extended addressing mode register defines */
#define ATLAS7_QSPI_EXT_AM_OPCODE(x)	(((x) & 0xFF) << 0)
#define ATLAS7_QSPI_EXT_AM_BYTE0(x)	(((x) & 0xFF) << 8)
#define ATLAS7_QSPI_EXT_AM_BYTE1(x)	(((x) & 0xFF) << 16)
#define ATLAS7_QSPI_EXT_AM_MODE(x)	(((x) & 0x3) << 24)
#define ATLAS7_QSPI_EXT_AM_CHECK_WIP	BIT(26)
#define ATLAS7_QSPI_EXT_AM_WREN		BIT(27)

/* QSPI interrupt mask register defines */
#define ATLAS7_QSPI_IMR_DATA_OUT_AV	BIT(0)
#define ATLAS7_QSPI_IMR_DATA_IN_RDY	BIT(1)
#define ATLAS7_QSPI_IMR_REQUEST_RDY	BIT(2)
#define ATLAS7_QSPI_IMR_MASK		0x7

/* QSPI interrupt request register defines */
#define ATLAS7_QSPI_IRR_DATA_OUT_AV	BIT(0)
#define ATLAS7_QSPI_IRR_DATA_IN_RDY	BIT(1)
#define ATLAS7_QSPI_IRR_REQUEST_RDY	BIT(2)

/* QSPI custom instruction setup register defines */
#define ATLAS7_QSPI_CI_OPCODE(x)	(((x) & 0xFF) << 0)
#define ATLAS7_QSPI_CI_LENGTH(x)	(((x + 1) & 0xF) << 8)
#define ATLAS7_QSPI_CI_SPI_WPN		BIT(12)
#define ATLAS7_QSPI_CI_SPI_HOLDN	BIT(13)
#define ATLAS7_QSPI_CI_CHECK_WIP	BIT(14)
#define ATLAS7_QSPI_CI_WREN		BIT(15)

/* QSPI read dummy cycles register defines */
#define ATLAS7_QSPI_RX_DELAY_MAX	0x7
#define ATLAS7_QSPI_RDC_READ2IO(x)	(((x) & 0xF) << 0)
#define ATLAS7_QSPI_RDC_READ4IO(x)	(((x) & 0xF) << 4)
#define ATLAS7_QSPI_RX_DELAY(x)		(((x) & 0x7) << 8)

/* QSPI dma control/status register defines */
#define ATLAS7_QSPI_DMA_READ_OP		(0x0 << 0)
#define ATLAS7_QSPI_DMA_WRITE_OP	(0x1 << 0)
#define ATLAS7_QSPI_DMA_START		BIT(4)
#define ATLAS7_QSPI_DMA_STAT		BIT(8)
#define ATLAS7_QSPI_DMA_SRESET		BIT(12)
#define ATLAS7_QSPI_DMA_INT_EN		BIT(16)
#define ATLAS7_QSPI_DMA_INT_STAT	BIT(20)
#define ATLAS7_QSPI_DMA_INT_CLR		BIT(24)
#define ATLAS7_QSPI_DMA_CE		BIT(28)

/* QSPI XOTF enable register defines */
#define ATLAS7_QSPI_XOTF_DEACTIVATED	(0x0 << 0)
#define ATLAS7_QSPI_XOTF_ACTIVATED	(0x1 << 0)

/* QSPI delay line register defines */
#define ATLAS7_QSPI_DELAY_LING_DATA0(clock_delay, use, negedge)\
		((((clock_delay) & 0x1F) | (((use) & 0x1) << 6) |\
		(((negedge) | 0x1)) << 7) << 0)
#define ATLAS7_QSPI_DELAY_LING_DATA1(clock_delay, use, negedge)\
		((((clock_delay) & 0x1F) | (((use) & 0x1) << 6) |\
		(((negedge) | 0x1)) << 7) << 8)
#define ATLAS7_QSPI_DELAY_LING_DATA2(clock_delay, use, negedge)\
		((((clock_delay) & 0x1F) | (((use) & 0x1) << 6) |\
		(((negedge) | 0x1)) << 7) << 16)
#define ATLAS7_QSPI_DELAY_LING_DATA3(clock_delay, use, negedge)\
		((((clock_delay) & 0x1F) | (((use) & 0x1) << 6) |\
		(((negedge) | 0x1)) << 7) << 24)

#define ATLAS7_QSPI_FIFO_SIZE		128
#define ATLAS7_QSPI_FIFO_THREAD		(ATLAS7_QSPI_FIFO_SIZE/4/2)

/*
* Define max times to check status register before we give up.
* M25P16 specs 40s max chip erase
*/
#define ATLAS7_QSPI_MAX_TIMEOUT		40000
#define ATLAS7_JEDEC_MFR(_jedec_id)	((_jedec_id) >> 16)

struct atlas7_qspi_nor {
	struct device		*dev;
	void __iomem		*base;
	struct mtd_info		mtd;
	struct nor_flash_info	*info;

	struct completion	tx_av;
	struct completion	rx_rdy;
	struct completion	req_rdy;

	struct mutex		lock;
	struct clk		*clk;
	u32			speed_hz;

	int	(*read)(struct atlas7_qspi_nor *a7nor,
				u32 *buf, u32 size, u32 offset);
	int	(*write)(struct atlas7_qspi_nor *a7nor,
				u32 *buf, u32 size, u32 offset);

	u32			read_flag;
	u32			write_flag;
};


/* SPI Flash Device Table */
struct nor_flash_info {
	char            *name;
	/*
	 * JEDEC id zero means "no ID" (most older chips); otherwise it has
	 * a high byte of zero plus three data bytes: the manufacturer id,
	 * then a two byte device id.
	 */
	u32             jedec_id;
	u16             ext_id;
	/*
	 * The size listed here is what works with SPINOR_OP_SE, which isn't
	 * necessarily called a "sector" by the vendor.
	 */
	u32		page_size;
	unsigned        sector_size;
	u16             n_sectors;
	u32             flags;
	/*
	 * Note, where FAST_READ is supported, freq_max specifies the
	 * FAST_READ frequency, not the READ frequency.
	 */
	u32             max_freq;
	/*these there values is used for clock configure*/
	u8		tshsl;
	u8		twhsl;
	u8		tshwl;
	/*
	 * dummy_2b is for read2io dummy cycles
	 * dummy_4b is for read4io dummy cycles
	 */
	u8		dummy_2b;
	u8		dummy_4b;
	/*callback function for enable dual, quad and enter 32bit address*/
	int		(*dual_enable)(struct atlas7_qspi_nor *a7nor);
	int		(*quad_enable)(struct atlas7_qspi_nor *a7nor);
	int		(*enter_32addr)(struct atlas7_qspi_nor *a7nor);
};

static int
atlas7_qspi_nor_macronix_quad_enable(struct atlas7_qspi_nor *a7nor);

static int
atlas7_qspi_enter_32bit_addr(struct atlas7_qspi_nor *a7nor);

static struct nor_flash_info flash_types[] = {
	/* default */
	{ "default", 0, 0, 256, 4 * 1024, 4096,
		0, 108, 20, 20, 100, 8, 8,
		NULL, NULL, NULL},
	/* Micron n25xxx */
#define N25Q_FLAG (FLASH_FLAG_READ_WRITE	|	\
		   FLASH_FLAG_READ_FAST		|	\
		   FLASH_FLAG_READ_1_1_2	|	\
		   FLASH_FLAG_READ_1_2_2	|	\
		   FLASH_FLAG_READ_1_1_4	|	\
		   FLASH_FLAG_READ_1_4_4	|	\
		   FLASH_FLAG_WRITE_1_1_2	|	\
		   FLASH_FLAG_WRITE_1_2_2	|	\
		   FLASH_FLAG_WRITE_1_1_4)
	{ "n25q256a", 0x20ba19, 0, 256, 4 * 1024, 4096 * 2,
		N25Q_FLAG | FLASH_FLAG_32BIT_ADDR,
		108, 20, 20, 100, 8, 10,
		NULL, NULL, atlas7_qspi_enter_32bit_addr},

	/*Micronix mx25xx */
#define MX25_FLAG (FLASH_FLAG_READ_WRITE	|	\
		   FLASH_FLAG_READ_FAST		|	\
		   FLASH_FLAG_READ_1_1_2	|	\
		   FLASH_FLAG_READ_1_2_2	|	\
		   FLASH_FLAG_READ_1_1_4	|	\
		   FLASH_FLAG_READ_1_4_4	|	\
		   FLASH_FLAG_WRITE_1_4_4)
#define ATLAS7_QSPI_MACRONIX_QUAD_EN_BIT	(0x1<<6)

	{ "mx25l25635f", 0xc22019, 0, 256, 4 * 1024, 4096 * 2,
		MX25_FLAG | FLASH_FLAG_32BIT_ADDR,
		133, 30, 20, 100, 4, 6,
		NULL, atlas7_qspi_nor_macronix_quad_enable,
		atlas7_qspi_enter_32bit_addr},

	/* Sentinel */
	{},
};


static irqreturn_t atlas7_qspi_irq(int irq, void *_sr)
{
	struct atlas7_qspi_nor *a7nor = _sr;
	u32 int_stat;

	int_stat = readl(a7nor->base + ATLAS7_QSPI_INTREQ);
	/*disable interrupt*/
	writel(readl(a7nor->base + ATLAS7_QSPI_INTMSK) & (~int_stat),
		a7nor->base + ATLAS7_QSPI_INTMSK);
	/*clear interrupt status*/
	writel(int_stat, a7nor->base + ATLAS7_QSPI_INTREQ);

	if (int_stat & ATLAS7_QSPI_IRR_DATA_OUT_AV)
		complete(&a7nor->rx_rdy);

	if (int_stat & ATLAS7_QSPI_IRR_DATA_IN_RDY)
		complete(&a7nor->tx_av);

	if (int_stat & ATLAS7_QSPI_IRR_REQUEST_RDY)
		complete(&a7nor->req_rdy);

	return IRQ_HANDLED;
}

static void
atlas7_qspi_set_work_mode(struct atlas7_qspi_nor *a7nor)
{
	u8 rd_op, wrt_op;
	u32 regval = 0;

	rd_op = ATLAS7_QSPI_FAST_READ;
	if (a7nor->read_flag & FLASH_FLAG_READ_1_1_2)
		rd_op = ATLAS7_QSPI_READ2O;
	if (a7nor->read_flag & FLASH_FLAG_READ_1_2_2)
		rd_op = ATLAS7_QSPI_READ2IO;
	if (a7nor->read_flag & FLASH_FLAG_READ_1_1_4)
		rd_op = ATLAS7_QSPI_READ4O;
	if (a7nor->read_flag & FLASH_FLAG_READ_1_4_4)
		rd_op = ATLAS7_QSPI_READ4IO;

	wrt_op = ATLAS7_QSPI_PP;
	if (a7nor->write_flag & FLASH_FLAG_WRITE_1_1_2)
		wrt_op = ATLAS7_QSPI_PP2O;
	if (a7nor->write_flag & FLASH_FLAG_WRITE_1_1_4)
		wrt_op = ATLAS7_QSPI_PP4O;
	if (a7nor->write_flag & FLASH_FLAG_WRITE_1_4_4)
		wrt_op = ATLAS7_QSPI_PP4IO;

	regval |= ATLAS7_QSPI_DEF_MEM_READ_OPCODE(rd_op);
	regval |= ATLAS7_QSPI_DEF_MEM_WRITE_OPCODE(wrt_op);

	/* enable 4-byte addressing if the device exceeds 16MiB */
	if (a7nor->mtd.size > ATLAS7_QSPI_24BIT_FLASH_SIZE)
		regval |= ATLAS7_QSPI_DEF_MEM_ATTR_32;
	writel(regval, a7nor->base + ATLAS7_QSPI_DEFMEM);
	ndelay(10);
}

static void
atlas7_qspi_set_dummy(struct atlas7_qspi_nor *a7nor)
{
	u32 regval = 0;
	int rx_delay = 0;

	if (a7nor->info->flags & FLASH_FLAG_READ_1_2_2)
		regval = ATLAS7_QSPI_RDC_READ2IO(a7nor->info->dummy_2b);
	if (a7nor->info->flags & FLASH_FLAG_READ_1_4_4)
		regval |= ATLAS7_QSPI_RDC_READ4IO(a7nor->info->dummy_4b);

	rx_delay = (clk_get_rate(a7nor->clk) / (2 * a7nor->speed_hz)) - 1;
	rx_delay = clamp(rx_delay, 1, ATLAS7_QSPI_RX_DELAY_MAX);
	regval |= ATLAS7_QSPI_RX_DELAY(rx_delay);

	writel(regval, a7nor->base + ATLAS7_QSPI_RDC);
}

static int
atlas7_qspi_custom_out(struct atlas7_qspi_nor *a7nor,
			u32 command, u8 *data_buf, u32 size)
{
	u8 *buf = data_buf;
	u32 data = 0;
	int len = size;
	int idx = 0;

	writel(ATLAS7_QSPI_IRR_REQUEST_RDY, a7nor->base + ATLAS7_QSPI_INTMSK);
	if (!wait_for_completion_timeout(&a7nor->req_rdy,
		ATLAS7_QSPI_MAX_TIMEOUT)) {
		dev_err(a7nor->dev, "wait for request ready timeout\n");
		return -ETIMEDOUT;
	}

	if (len > 0) {
		idx = 0;
		data = 0;
		while ((len > 0) && (idx < 4)) {
			data |= buf[idx] << (idx * 8);
			idx++;
			len--;
		}
		writel(data, a7nor->base + ATLAS7_QSPI_CIDR0);
	}
	if (len > 0) {
		idx = 0;
		data = 0;
		buf += sizeof(u32);
		while ((len > 0) && (idx < 4)) {
			data |= buf[idx] << (idx * 8);
			idx++;
			len--;
		}
		writel(data, a7nor->base + ATLAS7_QSPI_CIDR1);
	}
	writel(ATLAS7_QSPI_CI_OPCODE(command) |
		ATLAS7_QSPI_CI_LENGTH(size) |
		ATLAS7_QSPI_CI_SPI_HOLDN |
		ATLAS7_QSPI_CI_CHECK_WIP,
		a7nor->base + ATLAS7_QSPI_CICFG);

	writel(ATLAS7_QSPI_IRR_REQUEST_RDY, a7nor->base + ATLAS7_QSPI_INTMSK);
	if (!wait_for_completion_timeout(&a7nor->req_rdy,
		ATLAS7_QSPI_MAX_TIMEOUT)) {
		dev_err(a7nor->dev, "wait for request ready timeout\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int
atlas7_qspi_custom_in(struct atlas7_qspi_nor *a7nor,
			u32 command, u8 *data_buf, u32 size)
{
	u8 *buf = data_buf;
	u32 data = 0;
	u32 len = size;
	int idx = 0;
	u8 *pbyte = (u8 *)&data;

	writel(ATLAS7_QSPI_IRR_REQUEST_RDY, a7nor->base + ATLAS7_QSPI_INTMSK);
	if (!wait_for_completion_timeout(&a7nor->req_rdy,
		ATLAS7_QSPI_MAX_TIMEOUT)) {
		dev_err(a7nor->dev, "wait for request ready timeout\n");
		return -ETIMEDOUT;
	}

	writel(ATLAS7_QSPI_CI_OPCODE(command) |
		ATLAS7_QSPI_CI_LENGTH(size) |
		ATLAS7_QSPI_CI_SPI_HOLDN |
		ATLAS7_QSPI_CI_CHECK_WIP,
		a7nor->base + ATLAS7_QSPI_CICFG);

	writel(ATLAS7_QSPI_IRR_REQUEST_RDY, a7nor->base + ATLAS7_QSPI_INTMSK);
	if (!wait_for_completion_timeout(&a7nor->req_rdy,
		ATLAS7_QSPI_MAX_TIMEOUT)) {
		dev_err(a7nor->dev, "wait for request ready timeout\n");
		return -ETIMEDOUT;
	}

	if (len > 0) {
		data = readl(a7nor->base + ATLAS7_QSPI_CIDR0);
		idx = 0;
		while ((len > 0) && (idx < 4)) {
			buf[idx] = pbyte[idx];
			idx++;
			len--;
		}
	}
	if (len > 0) {
		buf += sizeof(u32);
		idx = 0;
		data = readl(a7nor->base + ATLAS7_QSPI_CIDR1);
		while ((len > 0) && (idx < 4)) {
			buf[idx] = pbyte[idx];
			idx++;
			len--;
		}
	}
	return 0;
}

static void
atlas7_qspi_host_out_data(struct atlas7_qspi_nor *a7nor, u32 data)
{
	writel(data, a7nor->base + ATLAS7_QSPI_RWDATA);
}

static u32
atlas7_qspi_host_in_data(struct atlas7_qspi_nor *a7nor)
{
	return readl(a7nor->base + ATLAS7_QSPI_RWDATA);
}

static int
atlas7_qspi_io_data_out(struct atlas7_qspi_nor *a7nor,
				u32 *buf, u32 size,
				u32 offset)
{
	int i = 0;
	u32 left_word = size / sizeof(u32);

	writel(ATLAS7_QSPI_IRR_REQUEST_RDY, a7nor->base + ATLAS7_QSPI_INTMSK);
	if (!wait_for_completion_timeout(&a7nor->req_rdy,
		ATLAS7_QSPI_MAX_TIMEOUT)) {
		dev_err(a7nor->dev, "wait for request ready timeout\n");
		return -ETIMEDOUT;
	}

	writel(offset, a7nor->base + ATLAS7_QSPI_ACCRR0);
	writel(size, a7nor->base + ATLAS7_QSPI_ACCRR1);
	writel(ATLAS7_QSPI_REQUEST_TYPE(ATLAS7_QSPI_WRITE_REQUSET),
		a7nor->base + ATLAS7_QSPI_ACCRR2);
	do {
		while (readl(a7nor->base + ATLAS7_QSPI_STAT)
			& ATLAS7_QSPI_DATA_IN_RDY) {
			atlas7_qspi_host_out_data(a7nor, buf[i++]);
			left_word--;
			if (left_word == 0)
				break;
		}
			writel(ATLAS7_QSPI_DATA_IN_RDY,
				a7nor->base + ATLAS7_QSPI_INTMSK);
			if (!wait_for_completion_timeout(&a7nor->tx_av,
				ATLAS7_QSPI_MAX_TIMEOUT)) {
				dev_err(a7nor->dev, "transmit out timeout\n");
				return (i - 1) * sizeof(u32);
			}
	} while (left_word != 0);
	return size;
}

static int
atlas7_qspi_io_data_in(struct atlas7_qspi_nor *a7nor,
				u32 *buf, u32 size,
				u32 offset)
{
	u32 left_word = size / sizeof(u32);
	int len;
	u32 i = 0;

	writel(ATLAS7_QSPI_IRR_REQUEST_RDY, a7nor->base + ATLAS7_QSPI_INTMSK);
	if (!wait_for_completion_timeout(&a7nor->req_rdy,
		ATLAS7_QSPI_MAX_TIMEOUT)) {
		dev_err(a7nor->dev, "wait for request ready timeout\n");
		return -ETIMEDOUT;
	}

	writel(offset, a7nor->base + ATLAS7_QSPI_ACCRR0);
	writel(size, a7nor->base + ATLAS7_QSPI_ACCRR1);
	writel(ATLAS7_QSPI_REQUEST_TYPE(ATLAS7_QSPI_READ_REQUSET),
		a7nor->base + ATLAS7_QSPI_ACCRR2);

	do {
		len = readl(a7nor->base + ATLAS7_QSPI_FFSTAT) &
				ATLAS7_QSPI_READ_FIFO_STATUS_MASK;
		while (len > 0) {
			buf[i++] = atlas7_qspi_host_in_data(a7nor);
			len--;
			left_word--;
			if (left_word == 0)
				break;
		}

		if (left_word < ATLAS7_QSPI_FIFO_THREAD) {
			writel(ATLAS7_QSPI_IRR_REQUEST_RDY,
				a7nor->base + ATLAS7_QSPI_INTMSK);
			if (!wait_for_completion_timeout(&a7nor->req_rdy,
				ATLAS7_QSPI_MAX_TIMEOUT)) {
				dev_err(a7nor->dev, "transmit in timeout\n");
				return (i - 1) * sizeof(u32);
			}
		} else {
			writel(ATLAS7_QSPI_DATA_OUT_AV,
				a7nor->base + ATLAS7_QSPI_INTMSK);
			if (!wait_for_completion_timeout(&a7nor->rx_rdy,
				ATLAS7_QSPI_MAX_TIMEOUT)) {
				dev_err(a7nor->dev, "transmit in timeout\n");
				return (i - 1) * sizeof(u32);
			}
		}
	} while (left_word != 0);

	return size;
}

#ifdef CONFIG_MTD_ATLAS7_QSPI_DMA
static int
atlas7_qspi_dma_data_out(struct atlas7_qspi_nor *a7nor,
				u32 *buf, u32 size,
				u32 offset)
{
	dma_addr_t addr;

	writel(ATLAS7_QSPI_IRR_REQUEST_RDY, a7nor->base + ATLAS7_QSPI_INTMSK);
	if (!wait_for_completion_timeout(&a7nor->req_rdy,
		ATLAS7_QSPI_MAX_TIMEOUT)) {
		dev_err(a7nor->dev, "wait for request ready timeout\n");
		return -ETIMEDOUT;
	}

	addr = dma_map_single(a7nor->dev, (void *)buf, size, DMA_TO_DEVICE);
	writel(addr, a7nor->base + ATLAS7_QSPI_DMA_SADDR);
	writel(offset, a7nor->base + ATLAS7_QSPI_DMA_FADDR);
	writel(size, a7nor->base + ATLAS7_QSPI_DMA_LEN);

	writel(ATLAS7_QSPI_DMA_WRITE_OP |
		ATLAS7_QSPI_DMA_START,
		a7nor->base + ATLAS7_QSPI_DMA_CST);

	writel(ATLAS7_QSPI_IRR_REQUEST_RDY, a7nor->base + ATLAS7_QSPI_INTMSK);
	if (!wait_for_completion_timeout(&a7nor->req_rdy,
		ATLAS7_QSPI_MAX_TIMEOUT)) {
		dev_err(a7nor->dev, "wait for request ready timeout\n");
		return -ETIMEDOUT;
	}

	return size;
}

static int
atlas7_qspi_dma_data_in(struct atlas7_qspi_nor *a7nor,
				u32 *buf, u32 size,
				u32 offset)
{
	dma_addr_t addr;

	writel(ATLAS7_QSPI_IRR_REQUEST_RDY, a7nor->base + ATLAS7_QSPI_INTMSK);
	if (!wait_for_completion_timeout(&a7nor->req_rdy,
		ATLAS7_QSPI_MAX_TIMEOUT)) {
		dev_err(a7nor->dev, "wait for request ready timeout\n");
		return -ETIMEDOUT;
	}

	addr = dma_map_single(a7nor->dev, (void *)buf, size, DMA_FROM_DEVICE);
	writel(addr, a7nor->base + ATLAS7_QSPI_DMA_SADDR);
	writel(offset, a7nor->base + ATLAS7_QSPI_DMA_FADDR);
	writel(size, a7nor->base + ATLAS7_QSPI_DMA_LEN);

	writel(ATLAS7_QSPI_DMA_READ_OP |
		ATLAS7_QSPI_DMA_START,
		a7nor->base + ATLAS7_QSPI_DMA_CST);

	writel(ATLAS7_QSPI_IRR_REQUEST_RDY, a7nor->base + ATLAS7_QSPI_INTMSK);
	if (!wait_for_completion_timeout(&a7nor->req_rdy,
		ATLAS7_QSPI_MAX_TIMEOUT)) {
		dev_err(a7nor->dev, "wait for request ready timeout\n");
		return -ETIMEDOUT;
	}
	return size;
}
#endif

static int
atlas7_qspi_nor_macronix_quad_enable(struct atlas7_qspi_nor *a7nor)
{
	int ret;
	u8 val = 0;

	mutex_lock(&a7nor->lock);
	ret = atlas7_qspi_custom_in(a7nor, SPINOR_OP_RDSR, &val, 1);
	if (ret < 0)
		goto out;

	ret = atlas7_qspi_custom_out(a7nor, SPINOR_OP_WREN, NULL, 0);
	if (ret < 0)
		goto out;

	val |= ATLAS7_QSPI_MACRONIX_QUAD_EN_BIT;
	ret = atlas7_qspi_custom_out(a7nor, SPINOR_OP_WRSR, &val, 1);
	if (ret < 0)
		goto out;

	val = 0;
	ret = atlas7_qspi_custom_in(a7nor, SPINOR_OP_RDSR, &val, 1);
	if (ret < 0)
		goto out;
	if (!(val > 0 && (val & ATLAS7_QSPI_MACRONIX_QUAD_EN_BIT))) {
		dev_err(a7nor->dev, "Macronix Quad bit not set\n");
		ret = -EINVAL;
	}
out:
	mutex_unlock(&a7nor->lock);
	return ret;
}

static int
atlas7_qspi_enter_32bit_addr(struct atlas7_qspi_nor *a7nor)
{
	int ret;
	u8 cmd;

	mutex_lock(&a7nor->lock);
	cmd = SPINOR_OP_WREN;
	ret = atlas7_qspi_custom_out(a7nor, cmd, NULL, 0);
	if (ret < 0)
		goto out;

	cmd = SPINOR_OP_EN4B;
	ret = atlas7_qspi_custom_out(a7nor, cmd, NULL, 0);
	if (ret < 0)
		goto out;

	cmd = SPINOR_OP_WRDI;
	ret = atlas7_qspi_custom_out(a7nor, cmd, NULL, 0);
out:
	mutex_unlock(&a7nor->lock);
	return ret;
}

static int
atlas7_qspi_nor_erase_chip(struct atlas7_qspi_nor *a7nor)
{
	writel(ATLAS7_QSPI_IRR_REQUEST_RDY, a7nor->base + ATLAS7_QSPI_INTMSK);
	if (!wait_for_completion_timeout(&a7nor->req_rdy,
		ATLAS7_QSPI_MAX_TIMEOUT)) {
		dev_err(a7nor->dev, "wait for request ready timeout\n");
		return -ETIMEDOUT;
	}

	writel(0, a7nor->base + ATLAS7_QSPI_ACCRR0);
	writel(ATLAS7_QSPI_CHIP_ERASE, a7nor->base + ATLAS7_QSPI_ACCRR1);
	writel(ATLAS7_QSPI_REQUEST_TYPE(ATLAS7_QSPI_ERASE_REQUSET),
		a7nor->base + ATLAS7_QSPI_ACCRR2);

	writel(ATLAS7_QSPI_IRR_REQUEST_RDY, a7nor->base + ATLAS7_QSPI_INTMSK);
	if (!wait_for_completion_timeout(&a7nor->req_rdy,
		ATLAS7_QSPI_MAX_TIMEOUT)) {
		dev_err(a7nor->dev, "wait for request ready timeout\n");
		return -ETIMEDOUT;
	}
	return 0;
}

/* the block erase function is not used now*/
#if 0
static int
atlas7_qspi_nor_erase_block(struct atlas7_qspi_nor *a7nor, u32 offset)
{
	writel(ATLAS7_QSPI_IRR_REQUEST_RDY, a7nor->base + ATLAS7_QSPI_INTMSK);
	if (!wait_for_completion_timeout(&a7nor->req_rdy,
		ATLAS7_QSPI_MAX_TIMEOUT)) {
		dev_err(a7nor->dev, "wait for request ready timeout\n");
		return -ETIMEDOUT;
	}

	writel(offset, a7nor->base + ATLAS7_QSPI_ACCRR0);
	writel(ATLAS7_QSPI_BLOCK_ERASE, a7nor->base + ATLAS7_QSPI_ACCRR1);
	writel(ATLAS7_QSPI_REQUEST_TYPE(ATLAS7_QSPI_ERASE_REQUSET),
		a7nor->base + ATLAS7_QSPI_ACCRR2);

	writel(ATLAS7_QSPI_IRR_REQUEST_RDY, a7nor->base + ATLAS7_QSPI_INTMSK);
	if (!wait_for_completion_timeout(&a7nor->req_rdy,
		ATLAS7_QSPI_MAX_TIMEOUT)) {
		dev_err(a7nor->dev, "wait for request ready timeout\n");
		return -ETIMEDOUT;
	}
	return 0;
}
#endif

static int
atlas7_qspi_nor_erase_sector(struct atlas7_qspi_nor *a7nor, u32 offset)
{
	writel(ATLAS7_QSPI_IRR_REQUEST_RDY, a7nor->base + ATLAS7_QSPI_INTMSK);
	if (!wait_for_completion_timeout(&a7nor->req_rdy,
		ATLAS7_QSPI_MAX_TIMEOUT)) {
		dev_err(a7nor->dev, "wait for request ready timeout\n");
		return -ETIMEDOUT;
	}

	writel(offset, a7nor->base + ATLAS7_QSPI_ACCRR0);
	writel(ATLAS7_QSPI_SECTOR_ERASE, a7nor->base + ATLAS7_QSPI_ACCRR1);
	writel(ATLAS7_QSPI_REQUEST_TYPE(ATLAS7_QSPI_ERASE_REQUSET),
		a7nor->base + ATLAS7_QSPI_ACCRR2);

	writel(ATLAS7_QSPI_IRR_REQUEST_RDY, a7nor->base + ATLAS7_QSPI_INTMSK);
	if (!wait_for_completion_timeout(&a7nor->req_rdy,
		ATLAS7_QSPI_MAX_TIMEOUT)) {
		dev_err(a7nor->dev, "wait for request ready timeout\n");
		return -ETIMEDOUT;
	}
	return 0;
}

/*
 * Read an address range from the flash chip. The address range
 * may be any size provided it is within the physical boundaries.
 */
static int
atlas7_qspi_nor_mtd_read(struct mtd_info *mtd, loff_t from, size_t len,
			  size_t *retlen, u_char *buf)
{
	struct atlas7_qspi_nor *a7nor = dev_get_drvdata(mtd->dev.parent);
	u32 bytes;

	dev_dbg(a7nor->dev, "%s from 0x%08x, len %zd\n",
		__func__, (u32)from, len);

	mutex_lock(&a7nor->lock);

	bytes = a7nor->read(a7nor, (u32 *)buf, (u32)len, from);

	*retlen = bytes;
	mutex_unlock(&a7nor->lock);

	return 0;
}

/*
 * Write an address range to the flash chip.  Data must be written in
 * FLASH_PAGESIZE chunks.  The address range may be any size provided
 * it is within the physical boundaries.
 */
static int
atlas7_qspi_nor_mtd_write(struct mtd_info *mtd, loff_t to, size_t len,
			   size_t *retlen, const u_char *buf)
{
	struct atlas7_qspi_nor *a7nor = dev_get_drvdata(mtd->dev.parent);
	u32 bytes;

	dev_dbg(a7nor->dev, "%s to 0x%08x, len %zd\n",
					__func__, (u32)to, len);

	mutex_lock(&a7nor->lock);

	bytes = a7nor->write(a7nor, (u32 *)buf,	(u32)len, to);

	*retlen = bytes;
	mutex_unlock(&a7nor->lock);

	return 0;
}

/*
 * Erase an address range on the flash chip. The address range may extend
 * one or more erase sectors.  Return an error is there is a problem erasing.
 */
static int
atlas7_qspi_nor_mtd_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct atlas7_qspi_nor *a7nor = dev_get_drvdata(mtd->dev.parent);
	int addr, len;
	int ret;

	dev_dbg(a7nor->dev, "%s at 0x%llx, len %lld\n", __func__,
		(long long)instr->addr, (long long)instr->len);

	addr = instr->addr;
	len = instr->len;

	mutex_lock(&a7nor->lock);

	/* Whole-chip erase? */
	if (len == mtd->size) {
		ret = atlas7_qspi_nor_erase_chip(a7nor);
		if (ret)
			goto out;
	} else {
		while (len > 0) {
			ret = atlas7_qspi_nor_erase_sector(a7nor, addr);
			if (ret)
				goto out;
			addr += mtd->erasesize;
			len -= mtd->erasesize;
		}
	}

	mutex_unlock(&a7nor->lock);

	instr->state = MTD_ERASE_DONE;
	mtd_erase_callback(instr);

	return 0;
out:
	instr->state = MTD_ERASE_FAILED;
	mutex_unlock(&a7nor->lock);

	return ret;
}

static int
atlas7_qspi_setup_controller(struct atlas7_qspi_nor *a7nor)
{
	u32 source_clk;
	u32 clk_div, regval;
	u32 clk_delay;

	source_clk = clk_get_rate(a7nor->clk);
	clk_div = (source_clk + a7nor->speed_hz) / (2 * a7nor->speed_hz) - 1;
	if (clk_div > ATLAS7_QSPI_CLK_DIV_MASK || regval < 0)
		return -EINVAL;
	regval = ATLAS7_QSPI_CLK_DIV(clk_div);

	regval |= ATLAS7_QSPI_SPI_MODE;
	/* clock delay */
	clk_delay = max3(a7nor->info->tshsl, a7nor->info->tshwl,
				a7nor->info->twhsl);
	clk_delay = max(clk_delay, 9 * (1 * 1000000000 / a7nor->speed_hz));
	clk_delay = clk_delay / (1000000000 / source_clk);
	if (clk_delay > 0xff)
		clk_delay = 0xff;
	regval |= ATLAS7_QSPI_CLK_DELAY(clk_delay);

	/* fifo threshold */
	regval |= ATLAS7_QSPI_RX_FIFO_THD(ATLAS7_QSPI_FIFO_THREAD) |
		ATLAS7_QSPI_TX_FIFO_THD(ATLAS7_QSPI_FIFO_THREAD);

	writel(regval, a7nor->base + ATLAS7_QSPI_CTRL);

	atlas7_qspi_set_work_mode(a7nor);

	atlas7_qspi_set_dummy(a7nor);

	return 0;
}

static int
atlas7_qspi_nor_configure_flash(struct atlas7_qspi_nor *a7nor)
{
	struct nor_flash_info *info = a7nor->info;
	int ret;

	if (!(info->flags & FLASH_FLAG_READ_FAST))
		return -EINVAL;

	a7nor->read_flag = FLASH_FLAG_READ_FAST;

	if (info->flags & FLASH_FLAG_READ_1_1_2)
		a7nor->read_flag = FLASH_FLAG_READ_1_1_2;
	if (info->flags & FLASH_FLAG_READ_1_2_2)
		a7nor->read_flag = FLASH_FLAG_READ_1_2_2;
	if (info->flags & FLASH_FLAG_READ_1_1_4)
		a7nor->read_flag = FLASH_FLAG_READ_1_1_4;
	if (info->flags & FLASH_FLAG_READ_1_4_4)
		a7nor->read_flag = FLASH_FLAG_READ_1_4_4;

	a7nor->write_flag = SPINOR_OP_WRITE;
	if (info->flags & FLASH_FLAG_WRITE_1_1_2)
		a7nor->write_flag = FLASH_FLAG_WRITE_1_1_2;
	if (info->flags & FLASH_FLAG_WRITE_1_1_4)
		a7nor->write_flag = FLASH_FLAG_WRITE_1_1_4;
	if (info->flags & FLASH_FLAG_WRITE_1_4_4)
		a7nor->write_flag = FLASH_FLAG_WRITE_1_4_4;

	if (((a7nor->read_flag & FLASH_FLAG_DUAL) ||
		(a7nor->write_flag & FLASH_FLAG_DUAL)) &&
		info->dual_enable) {
		ret = info->dual_enable(a7nor);
		if (ret < 0) {
			dev_err(a7nor->dev,
				"set dual mode fail, use fast mode.\n");
			a7nor->read_flag = FLASH_FLAG_READ_FAST;
			a7nor->write_flag = SPINOR_OP_WRITE;
		}
	}

	if (((a7nor->read_flag & FLASH_FLAG_QUAD) ||
		(a7nor->write_flag & FLASH_FLAG_QUAD)) &&
		info->quad_enable) {
		ret = info->quad_enable(a7nor);
		if (ret < 0) {
			dev_err(a7nor->dev,
				"set quad mode fail, use fast mode.\n");
			a7nor->read_flag = FLASH_FLAG_READ_FAST;
			a7nor->write_flag = SPINOR_OP_WRITE;
		}
	}

	if ((a7nor->mtd.size > ATLAS7_QSPI_24BIT_FLASH_SIZE) &&
		info->enter_32addr) {
		/* enable 4-byte addressing if the device exceeds 16MiB*/
		ret = info->enter_32addr(a7nor);
		if (ret < 0) {
			dev_err(a7nor->dev, "enter 32 bit address fail\n");
			return ret;
		}
	}

	/*
	 * flash device have update mode, need reconfigure controller
	 */
	mutex_lock(&a7nor->lock);

	atlas7_qspi_setup_controller(a7nor);

	mutex_unlock(&a7nor->lock);

	return 0;
}

static int
atlas7_qspi_nor_read_jedec(struct atlas7_qspi_nor *a7nor,
				u8 *jedec, u32 size)
{
	u8 cmd = SPINOR_OP_RDID;
	int ret = 0;

	mutex_lock(&a7nor->lock);

	ret = atlas7_qspi_custom_out(a7nor, cmd, NULL, 0);
	if (ret < 0)
		goto  out;
	ret = atlas7_qspi_custom_in(a7nor, cmd, jedec, size);
out:
	mutex_unlock(&a7nor->lock);
	return ret;
}

static struct nor_flash_info *
atlas7_qspi_nor_jedec_probe(struct atlas7_qspi_nor *a7nor)
{
	struct nor_flash_info	*info;
	u16                     ext_jedec;
	u32			jedec;
	u8			id[5];
	int tmp;

	tmp = atlas7_qspi_nor_read_jedec(a7nor, id, 5);
	if (tmp < 0) {
		dev_err(a7nor->dev, "read jedec fail.\n");
		return NULL;
	}

	jedec     = id[0] << 16 | id[1] << 8 | id[2];
	/*
	 * JEDEC also defines an optional "extended device information"
	 * string for after vendor-specific data, after the three bytes
	 * we use here. Supporting some chips might require using it.
	 */
	ext_jedec = id[3] << 8  | id[4];

	dev_dbg(a7nor->dev, "JEDEC =  0x%08x [%02x %02x %02x %02x %02x]\n",
		jedec, id[0], id[1], id[2], id[3], id[4]);

	for (info = flash_types; info->name; info++) {
		if (info->jedec_id == jedec) {
			if (info->ext_id && info->ext_id != ext_jedec)
				continue;
			return info;
		}
	}
	dev_err(a7nor->dev, "Unrecognized JEDEC id %06x\n", jedec);

	return NULL;
}

static int atlas7_qspi_is_xip(struct atlas7_qspi_nor *a7nor)
{
	return readl(a7nor->base + ATLAS7_QSPI_XOTF_EN) &
			ATLAS7_QSPI_XOTF_ACTIVATED;
}

static int atlas7_qspi_nor_hw_init(struct atlas7_qspi_nor *a7nor)
{
	mutex_lock(&a7nor->lock);

	/*
	 * before use the controller, need to close the
	 * automatic indentification function
	 */
	writel(0, a7nor->base + ATLAS7_QSPI_DEFMEM);

	atlas7_qspi_setup_controller(a7nor);

	mutex_unlock(&a7nor->lock);

	return 0;
}

static int atlas7_qspi_nor_config_dt(struct atlas7_qspi_nor *a7nor)
{
	int ret;

	ret = of_property_read_u32(a7nor->dev->of_node, "clock-rate",
				&a7nor->speed_hz);
	if (ret) {
		dev_err(a7nor->dev, "cannot find the clock rate.\n");
		return -ENODEV;
	}

	if (a7nor->speed_hz > ATLAS7_QSPI_MAX_CLOCK_FREQ)
		a7nor->speed_hz = ATLAS7_QSPI_MAX_CLOCK_FREQ;

	return 0;
}

static int atlas7_qspi_nor_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct mtd_part_parser_data ppdata;
	struct nor_flash_info *info;
	struct resource *res;
	struct atlas7_qspi_nor *a7nor;
	int irq, ret;

	if (!np) {
		dev_err(&pdev->dev, "No DT found\n");
		return -EINVAL;
	}
	ppdata.of_node = np;

	a7nor = devm_kzalloc(&pdev->dev, sizeof(*a7nor), GFP_KERNEL);
	if (!a7nor)
		return -ENOMEM;

	a7nor->dev = &pdev->dev;

	platform_set_drvdata(pdev, a7nor);

	ret = atlas7_qspi_nor_config_dt(a7nor);
	if (ret < 0) {
		dev_err(&pdev->dev, "con't get config data form DT\n");
		goto err;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = -ENODEV;
		dev_err(&pdev->dev, "invalid atlast7 qspi irq\n");
		goto err;
	}

	ret = devm_request_irq(&pdev->dev, irq, atlas7_qspi_irq, 0,
				DRIVER_NAME, a7nor);
	if (ret) {
		dev_err(&pdev->dev, "Sirf IRQ allocation failed\n");
		goto err;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	a7nor->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(a7nor->base)) {
		dev_err(&pdev->dev,
			"Failed to reserve memory region %pR\n", res);
		ret = PTR_ERR(a7nor->base);
		goto err;
	}

	/*
	* if the QSPI is on XIP mode, M3 is run on it,
	* a7 should not use qspi.
	*/
	if (atlas7_qspi_is_xip(a7nor)) {
		dev_err(&pdev->dev,
			"The QSPI is on XIP mode.\n");
		return -EBUSY;
	}

	init_completion(&a7nor->tx_av);
	init_completion(&a7nor->rx_rdy);
	init_completion(&a7nor->req_rdy);
	mutex_init(&a7nor->lock);


	a7nor->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(a7nor->clk)) {
		dev_err(&pdev->dev, "cannot get clock\n");
		ret = -EPROBE_DEFER;
		goto err;
	}
	clk_prepare_enable(a7nor->clk);

	/* set flash info to a default value, hardware init need them */
	a7nor->info = flash_types;
	ret = atlas7_qspi_nor_hw_init(a7nor);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialise atlast7 qspi Controller\n");
		goto err_clk;
	}

	/* Detect SPI FLASH device */
	info = atlas7_qspi_nor_jedec_probe(a7nor);
	if (!info) {
		dev_err(&pdev->dev, "probe jedec fail\n");
		ret = -ENODEV;
		goto err_clk;
	}
	a7nor->info = info;

	/* Use device size to determine address width */
	if (info->sector_size * info->n_sectors > ATLAS7_QSPI_24BIT_FLASH_SIZE)
		info->flags |= FLASH_FLAG_32BIT_ADDR;

	a7nor->mtd.name		= info->name;
	a7nor->mtd.dev.parent	= &pdev->dev;
	a7nor->mtd.type		= MTD_NORFLASH;
	a7nor->mtd.writesize	= 4;
	a7nor->mtd.writebufsize	= info->page_size;
	a7nor->mtd.flags	= MTD_CAP_NORFLASH;
	a7nor->mtd.size		= info->sector_size * info->n_sectors;
	a7nor->mtd.erasesize	= info->sector_size;

	a7nor->mtd._read	= atlas7_qspi_nor_mtd_read;
	a7nor->mtd._write	= atlas7_qspi_nor_mtd_write;
	a7nor->mtd._erase	= atlas7_qspi_nor_mtd_erase;

	a7nor->read = atlas7_qspi_io_data_in;
	a7nor->write = atlas7_qspi_io_data_out;
#ifdef CONFIG_MTD_ATLAS7_QSPI_DMA
	a7nor->read = atlas7_qspi_dma_data_in;
	a7nor->write = atlas7_qspi_dma_data_out;
#endif
	ret = atlas7_qspi_nor_configure_flash(a7nor);

	dev_info(&pdev->dev,
		"Found serial flash device: %s\n"
		"size = %llx (%lldMiB) erasesize = 0x%08x (%uKiB)\n",
		info->name,
		(long long)a7nor->mtd.size,
		(long long)(a7nor->mtd.size >> 20),
		a7nor->mtd.erasesize, (a7nor->mtd.erasesize >> 10));

	ret = mtd_device_parse_register(&a7nor->mtd, NULL, &ppdata, NULL, 0);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register mtd device\n");
		goto err_clk;
	}
	return 0;
err_clk:
	clk_disable_unprepare(a7nor->clk);
	clk_put(a7nor->clk);
err:
	return ret;
}

static int atlas7_qspi_nor_remove(struct platform_device *pdev)
{
	struct atlas7_qspi_nor *a7nor = platform_get_drvdata(pdev);

	return mtd_device_unregister(&a7nor->mtd);
}

static const struct of_device_id atlas7_qspi_nor_match[] = {
	{ .compatible = "sirf,atlas7-qspi-nor", },
	{},
};
MODULE_DEVICE_TABLE(of, atlas7_qspi_nor_match);

static struct platform_driver atlas7_qspi_nor_driver = {
	.probe		= atlas7_qspi_nor_probe,
	.remove		= atlas7_qspi_nor_remove,
	.driver		= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = atlas7_qspi_nor_match,
	},
};
module_platform_driver(atlas7_qspi_nor_driver);

MODULE_DESCRIPTION("SiRF SoC QSPI NOR FLASH driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Shunli Ai<Shunli.Ai@csr.com>");
