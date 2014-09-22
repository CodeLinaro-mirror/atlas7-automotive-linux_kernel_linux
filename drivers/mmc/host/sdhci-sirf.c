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
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include "sdhci-pltfm.h"

#define SDHCI_CLK_DELAY_SETTING	0x4C
#define SDHCI_SIRF_8BITBUS BIT(3)
#define SDHCI_SIRF_LDO_CNTL 0x6c

static const unsigned int sirf_vqmmc_voltages[] = {
	1650000,
	1700000,
	1750000,
	1800000,
	1850000,
	1900000,
	1950000,
};

static struct regulator_ops sirf_vqmmc_ops = {
	.list_voltage = regulator_list_voltage_table,
	.enable      = regulator_enable_regmap,
	.disable     = regulator_disable_regmap,
	.is_enabled  = regulator_is_enabled_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
};

static int sirf_vqmmc_reg_read(void *context, unsigned int reg,
	unsigned int *val)
{
	struct sdhci_host *host = (struct sdhci_host *)context;

	*val = sdhci_readb(host, reg);

	return 0;
}

static int sirf_vqmmc_reg_write(void *context, unsigned int reg,
	unsigned int val)
{
	struct sdhci_host *host = (struct sdhci_host *)context;

	sdhci_writeb(host, val, reg);

	return 0;
}

static struct regmap_config sirf_vqmmc_regmap_config = {
	.reg_read = sirf_vqmmc_reg_read,
	.reg_write = sirf_vqmmc_reg_write,
	.reg_bits = 8,
	.val_bits = 8,
};

static struct regulator_desc vqmmc_regulator = {
	.name = "VQMMC",
	.id   = 0,
	.ops  = &sirf_vqmmc_ops,
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
	.n_voltages = ARRAY_SIZE(sirf_vqmmc_voltages),
	.volt_table = sirf_vqmmc_voltages,
	.enable_reg = SDHCI_SIRF_LDO_CNTL,
	.enable_is_inverted = 1,
	.enable_mask = (0x1 << 4),
	.vsel_reg = SDHCI_SIRF_LDO_CNTL,
	.vsel_mask = 0x7,
};

static int sirf_vqmmc_regulator_init(struct platform_device *pdev,
	struct sdhci_host *host, struct device_node *np)
{
	struct regulator_config config = { };
	struct regulator_dev *vqmmc;

	/* Register VQMMC regulator */
	config.dev = &pdev->dev;
	config.regmap = devm_regmap_init(&pdev->dev, NULL, host,
		&sirf_vqmmc_regmap_config);
	config.of_node = np;
	config.init_data = of_get_regulator_init_data(&pdev->dev, np);

	vqmmc = devm_regulator_register(&pdev->dev,
					&vqmmc_regulator, &config);
	if (IS_ERR(vqmmc)) {
		dev_err(&pdev->dev,
			"error initializing sirf VQMMC regulator\n");
		return PTR_ERR(vqmmc);
	}

	dev_info(&pdev->dev, "initialized sirf VQMMC regulator\n");
	return 0;
}

static unsigned int sdhci_sirf_get_max_clk(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_sirf_priv *priv = sdhci_pltfm_priv(pltfm_host);
#ifdef CONFIG_A7DA_FPGA
		return 20000000;
#else
		return clk_get_rate(priv->clk);
#endif
}

static unsigned int sdhci_sirf_get_power_config(struct sdhci_host *host,
	unsigned short power)
{
	return SDHCI_POWER_300;
}

static void sdhci_sirf_set_bus_width(struct sdhci_host *host, int width)
{
	u8 ctrl;

	ctrl = sdhci_readb(host, SDHCI_HOST_CONTROL);
	ctrl &= ~(SDHCI_CTRL_4BITBUS | SDHCI_SIRF_8BITBUS);

	/*
	 * CSR atlas7 and prima2 SD host version is not 3.0
	 * 8bit-width enable bit of CSR SD hosts is 3,
	 * while stardard hosts use bit 5
	 */
	if (width == MMC_BUS_WIDTH_8)
		ctrl |= SDHCI_SIRF_8BITBUS;
	else if (width == MMC_BUS_WIDTH_4)
		ctrl |= SDHCI_CTRL_4BITBUS;

	sdhci_writeb(host, ctrl, SDHCI_HOST_CONTROL);
}

static struct sdhci_ops sdhci_sirf_ops = {
	.set_clock = sdhci_set_clock,
	.get_max_clock	= sdhci_sirf_get_max_clk,
	.get_power_config  = sdhci_sirf_get_power_config,
	.set_bus_width = sdhci_sirf_set_bus_width,
	.reset = sdhci_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
};

static struct sdhci_pltfm_data sdhci_sirf_pdata = {
	.ops = &sdhci_sirf_ops,
	.quirks = SDHCI_QUIRK_BROKEN_TIMEOUT_VAL |
		SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK |
		SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN |
		SDHCI_QUIRK_RESET_CMD_DATA_ON_IOS |
		SDHCI_QUIRK_DELAY_AFTER_POWER,
};
#ifdef CONFIG_A7DA_FPGA
static void __iomem *sirf_sd_fun_clr_3;
static void __iomem *sirf_sd_fun_set_3;
static void __iomem *sirf_sd_fun_clr_16;
static void __iomem *sirf_sd_fun_set_16;
static void __iomem *sirf_sd_fun_clr_1;
static void __iomem *sirf_sd_fun_set_1;
static void __iomem *sirf_sd_pull_clr_2;
static void __iomem *sirf_sd_pull_set_2;
static void __iomem *sirf_sd_pull_clr_11;
static void __iomem *sirf_sd_pull_set_11;
static void __iomem *sirf_sd_pull_clr_1;
static void __iomem *sirf_sd_pull_set_1;

static void sirf_sdio_pin_ioremap(void)
{
	  sirf_sd_fun_clr_3 = (void __iomem *)ioremap(0x10e4009c, 0x4);
	  sirf_sd_fun_set_3 = (void __iomem *)ioremap(0x10e40098, 0x4);
	  sirf_sd_fun_clr_16 = (void __iomem *)ioremap(0x10e40104, 0x4);
	  sirf_sd_fun_set_16 = (void __iomem *)ioremap(0x10e40100, 0x4);
	  sirf_sd_fun_clr_1 = (void __iomem *)ioremap(0x10e4008c, 0x4);
	  sirf_sd_fun_set_1 = (void __iomem *)ioremap(0x10e40088, 0x4);
	  sirf_sd_pull_clr_2 = (void __iomem *)ioremap(0x10e40194, 0x4);
	  sirf_sd_pull_set_2 = (void __iomem *)ioremap(0x10e40190, 0x4);
	  sirf_sd_pull_clr_11 = (void __iomem *)ioremap(0x10e40254, 0x4);
	  sirf_sd_pull_set_11 = (void __iomem *)ioremap(0x10e40250, 0x4);
	  sirf_sd_pull_clr_1 = (void __iomem *)ioremap(0x10e4018c, 0x4);
	  sirf_sd_pull_set_1 = (void __iomem *)ioremap(0x10e40188, 0x4);
}

static void sd2_config_pads(void)
{
	sirf_sdio_pin_ioremap();
	writel(0x00111111, sirf_sd_fun_clr_3);
	writel(0x00111111, sirf_sd_fun_set_3);
	writel(0x00220000, sirf_sd_fun_clr_16);
	writel(0x00220000, sirf_sd_fun_set_16);
	writel(0x00200000, sirf_sd_fun_clr_1);
	writel(0x00200000, sirf_sd_fun_set_1);
	writel(0x0, sirf_sd_pull_clr_2);
	writel(0x0, sirf_sd_pull_set_2);
	writel(0x00005055, sirf_sd_pull_clr_11);
	writel(0x00005055, sirf_sd_pull_set_11);
	writel(0x00015155, sirf_sd_pull_clr_1);
	writel(0x00015155, sirf_sd_pull_set_1);
}
#endif

static int sdhci_sirf_probe(struct platform_device *pdev)
{
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_sirf_priv *priv;
	struct clk *clk, *pclk;
	struct device_node *np, *child;
	int gpio_cd;
	int ret;
#ifdef CONFIG_A7DA_FPGA
		sd2_config_pads();
#endif

	np = pdev->dev.of_node;

	host = sdhci_pltfm_init(pdev, &sdhci_sirf_pdata,
		sizeof(struct sdhci_sirf_priv));
	if (IS_ERR(host)) {
		ret = PTR_ERR(host);
		goto err_sdhci_pltfm_init;
	}

	pltfm_host = sdhci_priv(host);
	priv = sdhci_pltfm_priv(pltfm_host);
	pltfm_host->priv = priv;
	/* CSR refine for trig */
	priv->loopdma = of_property_read_bool(pdev->dev.of_node, "loop-dma");
#ifndef CONFIG_A7DA_FPGA

	if (of_device_is_compatible(np, "sirf,atlas7-sdhc"))
		priv->has_pclk = true;
	else
		priv->has_pclk = false;
	if (priv->has_pclk) {
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

	child = of_get_child_by_name(np, "vqmmc");
	if (child) {
		ret = sirf_vqmmc_regulator_init(pdev, host, child);
		if (ret)
			return ret;
	}
#endif
	if (np) {
		gpio_cd = of_get_named_gpio(pdev->dev.of_node, "cd-gpios", 0);
		priv->power_gpio = of_get_named_gpio(pdev->dev.of_node,
			"power-gpios", 0);
	} else {
		gpio_cd = -EINVAL;
		priv->power_gpio = -EINVAL;
	}

	priv->clk = clk;
	if (priv->has_pclk)
		priv->pclk = pclk;
	priv->gpio_cd = gpio_cd;

	sdhci_get_of_property(pdev);
	mmc_of_parse(host->mmc);
#ifndef CONFIG_A7DA_FPGA
	ret = clk_prepare_enable(priv->clk);
	if (ret)
		goto err_clk_prepare;

	if (priv->has_pclk) {
		ret = clk_prepare_enable(priv->pclk);
		if (ret)
			goto err_pclk_prepare;
	}
#endif
	host->quirks2 = SDHCI_QUIRK2_SG_LIST_COMBINED_DMA_BUFFER;

	ret = sdhci_add_host(host);
	if (ret)
		goto err_sdhci_add;

	if (gpio_is_valid(priv->power_gpio)) {
		ret = gpio_request(priv->power_gpio, "sirf_sdhci_power");
		if (ret) {
			dev_err(mmc_dev(host->mmc),
				"failed to allocate power gpio\n");
			goto err_request_power_pin;
		}
		gpio_direction_output(priv->power_gpio, 1);
	}

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

	if (of_device_is_compatible(np, "sirf,prima2-sdhc"))
		sdhci_writel(host, 0x60, SDHCI_CLK_DELAY_SETTING);

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
	if (gpio_is_valid(priv->power_gpio))
		gpio_free(priv->power_gpio);
err_request_power_pin:
	sdhci_remove_host(host, 0);
err_sdhci_add:
	if (priv->has_pclk)
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

	if (gpio_is_valid(priv->power_gpio))
		gpio_free(priv->power_gpio);

	if (gpio_is_valid(priv->gpio_cd))
		mmc_gpio_free_cd(host->mmc);

	clk_disable_unprepare(priv->clk);
	if (priv->has_pclk)
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
	if (priv->has_pclk)
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

	if (priv->has_pclk) {
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
