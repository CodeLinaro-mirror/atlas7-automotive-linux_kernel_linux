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
#include "sdhci-pltfm.h"

static unsigned int sdhci_sirf_get_max_clk(struct sdhci_host *host)
{
	return 150000000;
}

static void sdhci_sirf_set_clock(struct sdhci_host *host, unsigned int clock)
{
	/*
	 * Todo: fulfill the implement for Marco Board
	 * for FPGA, we use the default clock freq
	 */
	host->clock = clock;
}

static struct sdhci_ops sdhci_sirf_ops = {
	.get_max_clock	= sdhci_sirf_get_max_clk,
	.set_clock	= sdhci_sirf_set_clock,
};

static struct sdhci_pltfm_data sdhci_sirf_pdata = {
	.ops = &sdhci_sirf_ops,
	.quirks = SDHCI_QUIRK_BROKEN_DMA |
		SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK |
		SDHCI_QUIRK_INVERTED_WRITE_PROTECT |
		SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN |
		SDHCI_QUIRK_BROKEN_TIMEOUT_VAL |
		SDHCI_QUIRK_NONSTANDARD_CLOCK,
};

static int __devinit sdhci_sirf_probe(struct platform_device *pdev)
{
	return sdhci_pltfm_register(pdev, &sdhci_sirf_pdata);
}

static int __devexit sdhci_sirf_remove(struct platform_device *pdev)
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
	.remove		= __devexit_p(sdhci_sirf_remove),
};

module_platform_driver(sdhci_sirf_driver);

MODULE_DESCRIPTION("SDHCI driver for SiRFprimaII/SiRFmarco");
MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_LICENSE("GPL v2");
