/*
 * SDHCI support for SiRF primaII and marco SoCs
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/mmc/host.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/dma-mapping.h>
#include "sdhci-pltfm.h"

#define SDHCI_CLK_DELAY_SETTING	0x4C
#define SDHCI_SIRF_8BITBUS (0x1 << 3)

static unsigned int sdhci_sirf_get_max_clk(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_sirf_priv *priv = sdhci_pltfm_priv(pltfm_host);
	return clk_get_rate(priv->clk);
}

static unsigned int sdhci_sirf_get_power_config(struct sdhci_host *host,
	unsigned short power)
{
	return SDHCI_POWER_300;
}

static int sdhci_sirf_set_bus_width(struct sdhci_host *host, int width)
{
	u8 ctrl;

	ctrl = sdhci_readb(host, SDHCI_HOST_CONTROL);
	if ((width == MMC_BUS_WIDTH_8)
		&& (host->caps & MMC_CAP_8_BIT_DATA)) {
		ctrl &= ~SDHCI_CTRL_4BITBUS;
		/*
		 * CSR host 8 bit setting is bit3,
		 * while stardard host is bit 5
		 */
		ctrl |= SDHCI_SIRF_8BITBUS;
	} else {
		if (host->version >= SDHCI_SPEC_300)
			ctrl &= ~SDHCI_SIRF_8BITBUS;
		if (width == MMC_BUS_WIDTH_4)
			ctrl |= SDHCI_CTRL_4BITBUS;
		else
			ctrl &= ~SDHCI_CTRL_4BITBUS;
	}
	sdhci_writeb(host, ctrl, SDHCI_HOST_CONTROL);

	return 0;
}

static struct sdhci_ops sdhci_sirf_ops = {
	.get_max_clock	= sdhci_sirf_get_max_clk,
	.get_power_config  = sdhci_sirf_get_power_config,
	.platform_bus_width = sdhci_sirf_set_bus_width,
};

static struct sdhci_pltfm_data sdhci_sirf_pdata = {
	.ops = &sdhci_sirf_ops,
	.quirks = SDHCI_QUIRK_BROKEN_TIMEOUT_VAL |
		SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK |
		SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN |
		SDHCI_QUIRK_RESET_CMD_DATA_ON_IOS |
		SDHCI_QUIRK_DELAY_AFTER_POWER,
};

static int sdhci_sirf_probe(struct platform_device *pdev)
{
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_sirf_priv *priv;
	struct clk *clk, *pclk;
	struct device_node *np;
	int gpio_cd;
	int ret;

	np = pdev->dev.of_node;
	if (of_device_is_compatible(np, "sirf,atlas7-sdhc")) {
		clk = devm_clk_get(&pdev->dev, "core");
		if (IS_ERR(clk)) {
			dev_err(&pdev->dev, "unable to get core clock");
			return PTR_ERR(clk);
		}
		pclk = devm_clk_get(&pdev->dev, "iface");
		if (IS_ERR(pclk)) {
			dev_err(&pdev->dev, "unable to get interface clock");
			return PTR_ERR(pclk);
		}
	} else {
		clk = devm_clk_get(&pdev->dev, NULL);
		if (IS_ERR(clk)) {
			dev_err(&pdev->dev, "unable to get clock");
			return PTR_ERR(clk);
		}
	}

	if (np)
		gpio_cd = of_get_named_gpio(pdev->dev.of_node, "cd-gpios", 0);
	else
		gpio_cd = -EINVAL;

	host = sdhci_pltfm_init(pdev, &sdhci_sirf_pdata, sizeof(struct sdhci_sirf_priv));
	if (IS_ERR(host)) {
		ret = PTR_ERR(host);
		goto err_sdhci_pltfm_init;
	}

	pltfm_host = sdhci_priv(host);
	priv = sdhci_pltfm_priv(pltfm_host);
	pltfm_host->priv = priv;
	/* CSR refine for trig */
	priv->loopdma = of_property_read_bool(pdev->dev.of_node, "loop-dma");

	priv->clk = clk;
	if (of_device_is_compatible(np, "sirf,atlas7-sdhc"))
		priv->pclk = pclk;
	priv->gpio_cd = gpio_cd;

	sdhci_get_of_property(pdev);
	mmc_of_parse(host->mmc);

	ret = clk_prepare_enable(priv->clk);
	if (ret)
		goto err_clk_prepare;

	if (of_device_is_compatible(np, "sirf,atlas7-sdhc")) {
		ret = clk_prepare_enable(priv->pclk);
		if (ret)
			goto err_pclk_prepare;
	}

	ret = sdhci_add_host(host);
	if (ret)
		goto err_sdhci_add;

	/*
	 * We must request the IRQ after sdhci_add_host(), as the tasklet only
	 * gets setup in sdhci_add_host() and we oops.
	 */
	if (gpio_is_valid(priv->gpio_cd)) {
		ret = mmc_gpio_request_cd(host->mmc, priv->gpio_cd, 0);
		if (ret) {
			dev_err(&pdev->dev, "card detect irq request failed: %d\n",
				ret);
			goto err_request_cd;
		}
	}

	sdhci_writel(host, 0x60, SDHCI_CLK_DELAY_SETTING);

	host->quirks2 = SDHCI_QUIRK2_SG_LIST_COMBINED_DMA_BUFFER;
	host->combined_dma_buffer = dma_alloc_coherent(&pdev->dev,
		SZ_1M, &host->dma_buffer, GFP_KERNEL | GFP_DMA);
	if (!host->combined_dma_buffer)
		goto err_request_cd;

	/* CSR refine for trig */
	/* Loop DMA buffer allocation */
	if (priv->loopdma) {
		priv->mem_buf[0] = dma_alloc_coherent(&pdev->dev,
			512 * (1 << LOOPDMA_BUF_SIZE_SHIFT),
			&priv->loopdma_buf[0], GFP_KERNEL | GFP_DMA);
		priv->mem_buf[1] = dma_alloc_coherent(&pdev->dev,
			512 * (1 << LOOPDMA_BUF_SIZE_SHIFT),
			&priv->loopdma_buf[1], GFP_KERNEL | GFP_DMA);
	}

	return 0;

err_request_cd:
	sdhci_remove_host(host, 0);
err_sdhci_add:
	if (of_device_is_compatible(np, "sirf,atlas7-sdhc"))
		clk_disable_unprepare(priv->pclk);
err_pclk_prepare:
	clk_disable_unprepare(priv->clk);
err_clk_prepare:
	sdhci_pltfm_free(pdev);
err_sdhci_pltfm_init:
	return ret;
}

static int sdhci_sirf_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_sirf_priv *priv = sdhci_pltfm_priv(pltfm_host);

	sdhci_pltfm_unregister(pdev);

	if (gpio_is_valid(priv->gpio_cd))
		mmc_gpio_free_cd(host->mmc);

	clk_disable_unprepare(priv->clk);
	if (of_device_is_compatible(pdev->dev.of_node, "sirf,atlas7-sdhc"))
		clk_disable_unprepare(priv->pclk);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int sdhci_sirf_suspend(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_sirf_priv *priv = sdhci_pltfm_priv(pltfm_host);
	int ret;

	ret = sdhci_suspend_host(host);
	if (ret)
		return ret;

	clk_disable(priv->clk);
	if (of_device_is_compatible(dev->of_node, "sirf,atlas7-sdhc"))
		clk_disable(priv->pclk);

	return 0;
}

static int sdhci_sirf_resume(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_sirf_priv *priv = sdhci_pltfm_priv(pltfm_host);
	int ret;

	ret = clk_enable(priv->clk);
	if (ret) {
		dev_dbg(dev, "Resume: Error enabling clock\n");
		return ret;
	}

	if (of_device_is_compatible(dev->of_node, "sirf,atlas7-sdhc")) {
		ret = clk_enable(priv->pclk);
		if (ret) {
			dev_dbg(dev, "Resume: Error enable interface clock\n");
			clk_disable(priv->clk);
			return ret;
		}
	}

	ret = sdhci_resume_host(host);

	/* restore sirf hacked regs after resume, since lose in suspend */
	sdhci_writel(host, 0x60, SDHCI_CLK_DELAY_SETTING);
	sdhci_writeb(host, 0xE, SDHCI_TIMEOUT_CONTROL);

	return ret;
}

static SIMPLE_DEV_PM_OPS(sdhci_sirf_pm_ops,
	sdhci_sirf_suspend, sdhci_sirf_resume);
#endif

static const struct of_device_id sdhci_sirf_of_match[] = {
	{ .compatible = "sirf,prima2-sdhc" },
	{ .compatible = "sirf,atlas7-sdhc" },
	{ }
};
MODULE_DEVICE_TABLE(of, sdhci_sirf_of_match);

static struct platform_driver sdhci_sirf_driver = {
	.driver		= {
		.name	= "sdhci-sirf",
		.owner	= THIS_MODULE,
		.of_match_table = sdhci_sirf_of_match,
#ifdef CONFIG_PM_SLEEP
		.pm	= &sdhci_sirf_pm_ops,
#endif
	},
	.probe		= sdhci_sirf_probe,
	.remove		= sdhci_sirf_remove,
};

module_platform_driver(sdhci_sirf_driver);

MODULE_DESCRIPTION("SDHCI driver for SiRFprimaII/SiRFmarco");
MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_LICENSE("GPL v2");
