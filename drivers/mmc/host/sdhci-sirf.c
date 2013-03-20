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
#include "sdhci-pltfm.h"

struct sdhci_sirf_priv {
	struct clk *clk;
	u32 clock;
	int gpio_cd;
};

static irqreturn_t sdhci_sirf_carddetect_irq(int irq, void *data)
{
	struct sdhci_host *host = data;

	tasklet_schedule(&host->card_tasklet);
	return IRQ_HANDLED;
}

static unsigned int sdhci_sirf_get_max_clk(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_sirf_priv *priv = pltfm_host->priv;
	return priv->clock;
}

#ifdef CONFIG_PM
static void sdhci_sirf_suspend(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_sirf_priv *priv = pltfm_host->priv;
	clk_disable_unprepare(priv->clk);
}

static void sdhci_sirf_resume(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_sirf_priv *priv = pltfm_host->priv;
	clk_prepare_enable(priv->clk);
}
#endif

static struct sdhci_ops sdhci_sirf_ops = {
	.get_max_clock	= sdhci_sirf_get_max_clk,
#ifdef CONFIG_PM
	.platform_suspend = sdhci_sirf_suspend,
	.platform_resume = sdhci_sirf_resume,
#endif
};

static struct sdhci_pltfm_data sdhci_sirf_pdata = {
	.ops = &sdhci_sirf_ops,
	.quirks = SDHCI_QUIRK_BROKEN_TIMEOUT_VAL |
		SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK |
		SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN |
		SDHCI_QUIRK_INVERTED_WRITE_PROTECT |
		SDHCI_QUIRK_DELAY_AFTER_POWER |
		SDHCI_QUIRK_BROKEN_DMA,
};

static int sdhci_sirf_probe(struct platform_device *pdev)
{
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_sirf_priv *priv;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(struct sdhci_sirf_priv),
		GFP_KERNEL);
	if (!priv) {
		dev_err(&pdev->dev, "unable to allocate private data");
		return -ENOMEM;
	}

	priv->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(&pdev->dev, "unable to get clock");
		return PTR_ERR(priv->clk);
	}

	if (pdev->dev.of_node) {
		priv->gpio_cd = of_get_named_gpio(pdev->dev.of_node,
			"cd-gpios", 0);
	} else {
		priv->gpio_cd = -EINVAL;
	}

	if (gpio_is_valid(priv->gpio_cd)) {
		ret = gpio_request(priv->gpio_cd, "sdhci-cd");
		if (ret) {
			dev_err(&pdev->dev, "card detect gpio request failed: %d\n",
				ret);
			return ret;
		}
		gpio_direction_input(priv->gpio_cd);
	}

	host = sdhci_pltfm_init(pdev, &sdhci_sirf_pdata);
	if (IS_ERR(host)) {
		ret = PTR_ERR(host);
		goto err_sdhci_pltfm_init;
	}

	pltfm_host = sdhci_priv(host);
	pltfm_host->priv = priv;

	sdhci_get_of_property(pdev);

	clk_prepare_enable(priv->clk);
	priv->clock = clk_get_rate(priv->clk);

	ret = sdhci_add_host(host);
	if (ret)
		goto err_sdhci_add;

	/*
	 * We must request the IRQ after sdhci_add_host(), as the tasklet only
	 * gets setup in sdhci_add_host() and we oops.
	 */
	if (gpio_is_valid(priv->gpio_cd)) {
		ret = request_irq(gpio_to_irq(priv->gpio_cd),
			sdhci_sirf_carddetect_irq,
			IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
			mmc_hostname(host->mmc), host);
		if (ret) {
			dev_err(&pdev->dev, "card detect irq request failed: %d\n",
				ret);
			goto err_request_irq;
		}
	}

	return 0;

err_request_irq:
	sdhci_remove_host(host, 0);
err_sdhci_add:
	clk_disable_unprepare(priv->clk);
	sdhci_pltfm_free(pdev);
err_sdhci_pltfm_init:
	if (gpio_is_valid(priv->gpio_cd))
		gpio_free(priv->gpio_cd);
	return ret;

}

static int sdhci_sirf_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_sirf_priv *priv = pltfm_host->priv;

	sdhci_pltfm_unregister(pdev);

	if (gpio_is_valid(priv->gpio_cd)) {
		free_irq(gpio_to_irq(priv->gpio_cd), host);
		gpio_free(priv->gpio_cd);
	}

	clk_disable_unprepare(priv->clk);
	return 0;
}

static const struct of_device_id sdhci_sirf_of_match[] = {
	{ .compatible = "sirf,prima2-sdhc" },
	{ .compatible = "sirf,marco-sdhc" },
	{ }
};
MODULE_DEVICE_TABLE(of, sdhci_sirf_of_match);

static struct platform_driver sdhci_sirf_driver = {
	.driver		= {
		.name	= "sdhci-sirf",
		.owner	= THIS_MODULE,
		.of_match_table = sdhci_sirf_of_match,
		.pm	= SDHCI_PLTFM_PMOPS,
	},
	.probe		= sdhci_sirf_probe,
	.remove		= sdhci_sirf_remove,
};

module_platform_driver(sdhci_sirf_driver);

MODULE_DESCRIPTION("SDHCI driver for SiRFprimaII/SiRFmarco");
MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_LICENSE("GPL v2");
