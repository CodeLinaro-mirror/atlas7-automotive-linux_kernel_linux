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

struct sdhci_sirf_priv {
	struct clk *clk;
	int gpio_cd;
};

static unsigned int sdhci_sirf_get_max_clk(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_sirf_priv *priv = sdhci_pltfm_priv(pltfm_host);
	return clk_get_rate(priv->clk);
}

static unsigned int sdhci_sirf_get_power_config(struct sdhci_host *host, unsigned short power)
{
       return SDHCI_POWER_300;
}

static struct sdhci_ops sdhci_sirf_ops = {
	.get_max_clock	= sdhci_sirf_get_max_clk,
	.get_power_config  = sdhci_sirf_get_power_config,
};

static struct sdhci_pltfm_data sdhci_sirf_pdata = {
	.ops = &sdhci_sirf_ops,
	.quirks = SDHCI_QUIRK_BROKEN_TIMEOUT_VAL |
		SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK |
		SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN |
		SDHCI_QUIRK_RESET_CMD_DATA_ON_IOS |
		SDHCI_QUIRK_DELAY_AFTER_POWER,
};

/*
 * The following functions are needed for DMA bouncing because SiRFprimaII SD
 * controller can address up to 256MByte
 */
static int sdhci_sirf_needs_bounce(struct device *dev, dma_addr_t dma_addr,
	size_t size)
{
	return (dma_addr + size) >= SZ_256M;
}

static int sdhci_sirf_probe(struct platform_device *pdev)
{
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_sirf_priv *priv;
	struct clk *clk;
	int gpio_cd;
	int ret;

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "unable to get clock");
		return PTR_ERR(clk);
	}

	if (pdev->dev.of_node)
		gpio_cd = of_get_named_gpio(pdev->dev.of_node, "cd-gpios", 0);
	else
		gpio_cd = -EINVAL;

	host = sdhci_pltfm_init(pdev, &sdhci_sirf_pdata, sizeof(struct sdhci_sirf_priv));
	if (IS_ERR(host))
		return PTR_ERR(host);

	pltfm_host = sdhci_priv(host);
	priv = sdhci_pltfm_priv(pltfm_host);
	priv->clk = clk;
	priv->gpio_cd = gpio_cd;

	sdhci_get_of_property(pdev);
	mmc_of_parse(host->mmc);

	ret = clk_prepare_enable(priv->clk);
	if (ret)
		goto err_clk_prepare;

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

	if (of_machine_is_compatible("sirf,prima2")) {
		ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(28));
		if (!ret) {
			pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;
			dmabounce_register_dev(&pdev->dev, 1024, 2048,
				sdhci_sirf_needs_bounce);
		} else {
			dev_err(&pdev->dev, "dma coherent mask: %d failed, ret: %d\n",
				DMA_BIT_MASK(28), ret);
			goto err_request_cd;
		}
	}

	return 0;

err_request_cd:
	sdhci_remove_host(host, 0);
err_sdhci_add:
	clk_disable_unprepare(priv->clk);
err_clk_prepare:
	sdhci_pltfm_free(pdev);
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

	ret = sdhci_resume_host(host);

	/* restore sirf hacked regs after resume, since lose in suspend */
	sdhci_writel(host, 0x60, SDHCI_CLK_DELAY_SETTING);
	sdhci_writeb(host, 0xE, SDHCI_TIMEOUT_CONTROL);

	return ret;
}

static SIMPLE_DEV_PM_OPS(sdhci_sirf_pm_ops, sdhci_sirf_suspend, sdhci_sirf_resume);
#endif

static const struct of_device_id sdhci_sirf_of_match[] = {
	{ .compatible = "sirf,prima2-sdhc" },
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
