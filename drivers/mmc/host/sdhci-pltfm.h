/*
 * Copyright 2010 MontaVista Software, LLC.
 *
 * Author: Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _DRIVERS_MMC_SDHCI_PLTFM_H
#define _DRIVERS_MMC_SDHCI_PLTFM_H

#include <linux/clk.h>
#include <linux/platform_device.h>
#include "sdhci.h"

struct sdhci_pltfm_data {
	const struct sdhci_ops *ops;
	unsigned int quirks;
	unsigned int quirks2;
};

struct sdhci_pltfm_host {
	struct clk *clk;
	void *priv; /* to handle quirks across io-accessor calls */

	/* migrate from sdhci_of_host */
	unsigned int clock;
	u16 xfer_mode_shadow;

	unsigned long private[0] ____cacheline_aligned;
};

/* CSR refine for trig */
struct sdhci_sirf_priv {
	struct clk *clk;
	struct clk *pclk;
	int has_pclk;
	int gpio_cd;
	int power_gpio;

	/* structure members towards CSR hacked SDIO ctl for TriG */
	int loopdma;
	dma_addr_t loopdma_buf[2];
	void *mem_buf[2];
	dma_addr_t dma_mem;
	unsigned int buffer_ready[2];
	unsigned int buffer_err[2];
	unsigned int buffer_crc_err;
	unsigned int buffer_dma_int;
	unsigned int lpdma_buf_sft;
};

#define LOOPDMA_BUFF0_RDY_FLAG  (1 << 10)
#define LOOPDMA_BUFF1_RDY_FLAG  (1 << 11)
#define LOOPDMA_BUFF0_ERR_FLAG  (1 << 26)
#define LOOPDMA_BUFF1_ERR_FLAG  (1 << 27)
#define SD_SYS_LOOPDMA_ADDR0    0x60
#define SD_SYS_LOOPDMA_ADDR1    0x64
#define LOOP_DMA_EN             (1 << 5)
#define LOOPDMA_INT_STATUS      0x30
#define LOOPDMA_INT_DMA_END     (1 << 3)
#define LOOPDMA_BUF_SIZE_SHIFT  9

#ifdef CONFIG_MMC_SDHCI_BIG_ENDIAN_32BIT_BYTE_SWAPPER
/*
 * These accessors are designed for big endian hosts doing I/O to
 * little endian controllers incorporating a 32-bit hardware byte swapper.
 */
static inline u32 sdhci_be32bs_readl(struct sdhci_host *host, int reg)
{
	return in_be32(host->ioaddr + reg);
}

static inline u16 sdhci_be32bs_readw(struct sdhci_host *host, int reg)
{
	return in_be16(host->ioaddr + (reg ^ 0x2));
}

static inline u8 sdhci_be32bs_readb(struct sdhci_host *host, int reg)
{
	return in_8(host->ioaddr + (reg ^ 0x3));
}

static inline void sdhci_be32bs_writel(struct sdhci_host *host,
				       u32 val, int reg)
{
	out_be32(host->ioaddr + reg, val);
}

static inline void sdhci_be32bs_writew(struct sdhci_host *host,
				       u16 val, int reg)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	int base = reg & ~0x3;
	int shift = (reg & 0x2) * 8;

	switch (reg) {
	case SDHCI_TRANSFER_MODE:
		/*
		 * Postpone this write, we must do it together with a
		 * command write that is down below.
		 */
		pltfm_host->xfer_mode_shadow = val;
		return;
	case SDHCI_COMMAND:
		sdhci_be32bs_writel(host,
				    val << 16 | pltfm_host->xfer_mode_shadow,
				    SDHCI_TRANSFER_MODE);
		return;
	}
	clrsetbits_be32(host->ioaddr + base, 0xffff << shift, val << shift);
}

static inline void sdhci_be32bs_writeb(struct sdhci_host *host, u8 val, int reg)
{
	int base = reg & ~0x3;
	int shift = (reg & 0x3) * 8;

	clrsetbits_be32(host->ioaddr + base , 0xff << shift, val << shift);
}
#endif /* CONFIG_MMC_SDHCI_BIG_ENDIAN_32BIT_BYTE_SWAPPER */

extern void sdhci_get_of_property(struct platform_device *pdev);

extern struct sdhci_host *sdhci_pltfm_init(struct platform_device *pdev,
					  const struct sdhci_pltfm_data *pdata,
					  size_t priv_size);
extern void sdhci_pltfm_free(struct platform_device *pdev);

extern int sdhci_pltfm_register(struct platform_device *pdev,
				const struct sdhci_pltfm_data *pdata,
				size_t priv_size);
extern int sdhci_pltfm_unregister(struct platform_device *pdev);

extern unsigned int sdhci_pltfm_clk_get_max_clock(struct sdhci_host *host);

static inline void *sdhci_pltfm_priv(struct sdhci_pltfm_host *host)
{
	return (void *)host->private;
}

#ifdef CONFIG_PM
extern int sdhci_pltfm_suspend(struct device *dev);
extern int sdhci_pltfm_resume(struct device *dev);
extern const struct dev_pm_ops sdhci_pltfm_pmops;
#define SDHCI_PLTFM_PMOPS (&sdhci_pltfm_pmops)
#else
#define SDHCI_PLTFM_PMOPS NULL
#endif

#endif /* _DRIVERS_MMC_SDHCI_PLTFM_H */
