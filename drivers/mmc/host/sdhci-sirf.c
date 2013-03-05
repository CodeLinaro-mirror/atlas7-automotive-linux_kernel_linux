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
#include "sdhci-pltfm.h"

/* Fixme: make it the platform data of sdhci_host */

static u32 sdhci_sirf_clk = 26000000; /* For FPGA */

static unsigned int sdhci_sirf_get_max_clk(struct sdhci_host *host)
{
	return sdhci_sirf_clk;
}

static struct sdhci_ops sdhci_sirf_ops = {
	.get_max_clock	= sdhci_sirf_get_max_clk,
};

static struct sdhci_pltfm_data sdhci_sirf_pdata = {
	.ops = &sdhci_sirf_ops,
	.quirks = SDHCI_QUIRK_BROKEN_TIMEOUT_VAL |
		SDHCI_QUIRK_BROKEN_TIMEOUT_VAL |
		SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK |
		SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN |
		SDHCI_QUIRK_INVERTED_WRITE_PROTECT |
		SDHCI_QUIRK_DELAY_AFTER_POWER |
		SDHCI_QUIRK_BROKEN_DMA,
};

static int sdhci_sirf_probe(struct platform_device *pdev)
{
	/* 
	 * we use 26MB for all marco for the moment, and get mmc clk
	 * from DT for primaII
	 */
	if (of_device_is_compatible(pdev->dev.of_node, "sirf,prima2-sdhc")) {
		struct clk *clk = clk_get(&pdev->dev, NULL);
		clk_prepare_enable(clk);
		sdhci_sirf_clk = clk_get_rate(clk);
		clk_put(clk);
	}

	return sdhci_pltfm_register(pdev, &sdhci_sirf_pdata);
}

static int sdhci_sirf_remove(struct platform_device *pdev)
{
	return sdhci_pltfm_unregister(pdev);
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
