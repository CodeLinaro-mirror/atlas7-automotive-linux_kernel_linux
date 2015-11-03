/*
 * SDHCI support for SiRF primaII and marco SoCs
 *
 * Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
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

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/mmc/host.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/dma-mapping.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include "sdhci-pltfm.h"

#define SDHCI_CLK_DELAY_SETTING	0x4C
#define SDHCI_SIRF_8BITBUS BIT(3)
#define SDHCI_CONTROL_DDR_EN (0x1 << 4)
#define SDHCI_SIRF_LDO_CNTL 0x6c
#define SIRF_TUNING_COUNT 16384

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
	.set_bypass = regulator_set_bypass_regmap,
	.get_bypass = regulator_get_bypass_regmap,
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
	.enable_mask = (0x1 << 3),
	.vsel_reg = SDHCI_SIRF_LDO_CNTL,
	.vsel_mask = 0x7,
	.bypass_reg = SDHCI_SIRF_LDO_CNTL,
	.bypass_mask = (0x1 << 4),
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

	if (priv->has_pclk)
		return ((clk_get_rate(priv->pclk) / 1000000) * 1000000);
	else
		return ((clk_get_rate(priv->clk) / 1000000) * 1000000);
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


	if (host->mmc &&
		((host->mmc->ios.timing == MMC_TIMING_UHS_DDR50)
		 || (host->mmc->ios.timing == MMC_TIMING_MMC_DDR52)))
		ctrl |= SDHCI_CONTROL_DDR_EN;
	else
		ctrl &= ~SDHCI_CONTROL_DDR_EN;

	sdhci_writeb(host, ctrl, SDHCI_HOST_CONTROL);
}

static int sirf_signal_voltage_switch(struct sdhci_host *host,
	unsigned char voltage)
{
	struct mmc_host *mmc = host->mmc;
	int ret;

	if (IS_ERR(mmc->supply.vqmmc))
		return 0;

	switch (voltage) {
	case MMC_SIGNAL_VOLTAGE_330:
		ret = regulator_allow_bypass(mmc->supply.vqmmc, true);
		if (ret) {
			pr_warn("%s: Switching to 3.3V voltage failed\n",
				mmc_hostname(mmc));
			return -EIO;
		}
		/* waiting voltage switch complete */
		usleep_range(5000, 5500);

		return 0;
	case MMC_SIGNAL_VOLTAGE_180:
		ret = regulator_allow_bypass(mmc->supply.vqmmc, false);
		if (ret) {
			pr_warn("%s: Switching to 1.8V voltage failed\n",
				mmc_hostname(mmc));
			return -EIO;
		}
		/* waiting voltage switch complete */
		usleep_range(5000, 5500);

		return 0;
	default:
		/* No signal voltage switch required */
		return 0;
	}
}

static u32 sdhci_sirf_readl_le(struct sdhci_host *host, int reg)
{
	u32 val = readl(host->ioaddr + reg);

	if (unlikely(reg == SDHCI_CAPABILITIES_1)) {
		/* fake CAP_1 register
		* SDR50_TUNING need to be faked
		*/
		if (host->mmc->caps & MMC_CAP_UHS_SDR50)
			val |= SDHCI_USE_SDR50_TUNING;
		else
			/*for slots which does not set SDR50
			*in dts, disable the SDR50 support
			*/
			val &= ~(SDHCI_SUPPORT_SDR50
				| SDHCI_USE_SDR50_TUNING);

	}


	return val;
}


static int sdhci_sirf_execute_tuning(struct sdhci_host *host, u32 opcode)
{
	int tuning_seq_cnt = 3;
	int phase;
	u8 tuned_phase_cnt = 0;
	int rc = 0, longest_range = 0;
	int start = -1, end = 0, tuning_value = -1, range = 0;
	u16 clock_setting;
	struct mmc_host *mmc = host->mmc;

	clock_setting = sdhci_readw(host, SDHCI_CLK_DELAY_SETTING);
	clock_setting &= ~0x3fff;

retry:
	phase = 0;
	tuned_phase_cnt = 0;
	do {
		sdhci_writel(host,
			clock_setting | phase,
			SDHCI_CLK_DELAY_SETTING);

		if (!mmc_send_tuning(mmc)) {
			/* Tuning is successful at this tuning point */
			tuned_phase_cnt++;
			dev_dbg(mmc_dev(mmc), "%s: Found good phase = %d\n",
				 mmc_hostname(mmc), phase);
			if (start == -1)
				start = phase;
			end = phase;
			range++;
			if (phase == (SIRF_TUNING_COUNT - 1)
				&& range > longest_range)
				tuning_value = (start + end) / 2;
		} else {
			dev_dbg(mmc_dev(mmc), "%s: Found bad phase = %d\n",
				 mmc_hostname(mmc), phase);
			if (range > longest_range) {
				tuning_value = (start + end) / 2;
				longest_range = range;
			}
			start = -1;
			end = range = 0;
		}
	} while (++phase < SIRF_TUNING_COUNT);

	if (tuned_phase_cnt && tuning_value > 0) {
		/*
		 * Finally set the selected phase in delay
		 * line hw block.
		 */
		phase = tuning_value;
		sdhci_writel(host,
			clock_setting | phase,
			SDHCI_CLK_DELAY_SETTING);

		dev_dbg(mmc_dev(mmc), "%s: Setting the tuning phase to %d\n",
			 mmc_hostname(mmc), phase);
	} else {
		if (--tuning_seq_cnt)
			goto retry;
		/* Tuning failed */
		dev_dbg(mmc_dev(mmc), "%s: No tuning point found\n",
		       mmc_hostname(mmc));
		rc = -EIO;
	}

	return rc;
}

static void sdhci_sirf_card_event(struct sdhci_host *host)
{
	u32 clock_setting, present;

	/*
	 * The clock delay value is tuned and set in case SDR50 compatible
	 * card is inserted.While the value is not fitful for HS card.So we need
	 * to reset the clock delay in case the card is removed or else later
	 * inseted HS card may encounter initialization issue
	 */
	present = sdhci_readl(host, SDHCI_PRESENT_STATE) & SDHCI_CARD_PRESENT;
	if (!present) {
		clock_setting = sdhci_readl(host, SDHCI_CLK_DELAY_SETTING);
		clock_setting &= ~0x3FFFF;
		sdhci_writel(host, clock_setting, SDHCI_CLK_DELAY_SETTING);
	}

}

static struct sdhci_ops sdhci_sirf_ops = {
	.read_l = sdhci_sirf_readl_le,
	.platform_execute_tuning = sdhci_sirf_execute_tuning,
	.set_clock = sdhci_set_clock,
	.get_max_clock	= sdhci_sirf_get_max_clk,
	.set_bus_width = sdhci_sirf_set_bus_width,
	.reset = sdhci_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
	.signal_voltage_switch = sirf_signal_voltage_switch,
	.card_event = sdhci_sirf_card_event,
};

static struct sdhci_pltfm_data sdhci_sirf_pdata = {
	.ops = &sdhci_sirf_ops,
	.quirks = SDHCI_QUIRK_BROKEN_TIMEOUT_VAL |
		SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK |
		SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN |
		SDHCI_QUIRK_RESET_CMD_DATA_ON_IOS,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN |
		SDHCI_QUIRK2_NO_DMA_RESELECT,
};

static int sdhci_sirf_probe(struct platform_device *pdev)
{
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_sirf_priv *priv;
	struct clk *clk, *pclk;
	struct device_node *np, *child;
	int gpio_cd;
	int ret;

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
		priv->clk = clk;
		priv->pclk = pclk;
	} else {
		clk = devm_clk_get(&pdev->dev, NULL);
		if (IS_ERR(clk)) {
			dev_err(&pdev->dev, "unable to get clock");
			return PTR_ERR(clk);
		}
		priv->clk = clk;
	}

	ret = clk_prepare_enable(priv->clk);
	if (ret)
		goto err_clk_prepare;

	if (priv->has_pclk) {
		ret = clk_prepare_enable(priv->pclk);
		if (ret)
			goto err_pclk_prepare;
	}

	child = of_get_child_by_name(np, "vqmmc");
	if (child) {
		ret = sirf_vqmmc_regulator_init(pdev, host, child);
		if (ret)
			return ret;
	}
	if (np) {
		gpio_cd = of_get_named_gpio(pdev->dev.of_node, "cd-gpios", 0);
		priv->power_gpio = of_get_named_gpio(pdev->dev.of_node,
			"power-gpios", 0);
	} else {
		gpio_cd = -EINVAL;
		priv->power_gpio = -EINVAL;
	}

	priv->gpio_cd = gpio_cd;

	sdhci_get_of_property(pdev);
	mmc_of_parse(host->mmc);

	host->quirks2 |= SDHCI_QUIRK2_SG_LIST_COMBINED_DMA_BUFFER;
	host->mmc->caps2 |= MMC_CAP2_NO_PRESCAN_POWERUP;

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
		mmc_gpiod_request_cd_irq(host->mmc);
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
			2048 * (1 << LOOPDMA_BUF_SIZE_SHIFT),
			&priv->loopdma_buf[0], GFP_KERNEL | GFP_DMA);
		priv->mem_buf[1] = priv->mem_buf[0] + 1024 * (1 <<
				LOOPDMA_BUF_SIZE_SHIFT);
		priv->loopdma_buf[1] = priv->loopdma_buf[0] + 1024 * (1 <<
				LOOPDMA_BUF_SIZE_SHIFT);

		priv->lpdma_buf_sft = LOOPDMA_BUF_SIZE_SHIFT;
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
	struct device_node *np = dev->of_node;
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
	if (!of_device_is_compatible(np, "sirf,atlas7-sdhc"))
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
