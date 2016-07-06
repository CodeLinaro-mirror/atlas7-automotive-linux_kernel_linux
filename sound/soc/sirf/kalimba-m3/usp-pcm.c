/*
 * CSRAtlas7 USP in I2S/DSP mode
 *
 * Copyright (c) [2014-2016] The Linux Foundation. All rights reserved.
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

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <sound/soc.h>

#include "../sirf-usp.h"
#include "usp-pcm.h"

/* Extra clocks required by Atlas7 USP3 */
static const char *const a7_exclks[] = {
	"a7ca_btss", "a7ca_io",
};

struct sirf_usp {
	struct regmap *regmap;
	struct clk *clk;

	struct clk *exclks[ARRAY_SIZE(a7_exclks)];
	bool is_atlas7_bt_usp;

	u32 mode1_reg;
	u32 mode2_reg;
	int daifmt_format;
	u32 fifo_size;
};

static struct sirf_usp *usp[USP_PORTS];

static void sirf_usp_tx_enable(struct sirf_usp *usp)
{
	regmap_update_bits(usp->regmap, USP_TX_RX_ENABLE,
		USP_TX_ENA, USP_TX_ENA);
}

static void sirf_usp_tx_disable(struct sirf_usp *usp)
{
	regmap_update_bits(usp->regmap, USP_TX_RX_ENABLE,
		USP_TX_ENA, ~USP_TX_ENA);
}

static void sirf_usp_rx_enable(struct sirf_usp *usp)
{
	regmap_update_bits(usp->regmap, USP_TX_RX_ENABLE,
		USP_RX_ENA, USP_RX_ENA);
}

static void sirf_usp_rx_disable(struct sirf_usp *usp)
{
	regmap_update_bits(usp->regmap, USP_TX_RX_ENABLE,
		USP_RX_ENA, ~USP_RX_ENA);
}

void sirf_usp_pcm_start(int port, int playback)
{
	if (playback)
		sirf_usp_tx_enable(usp[port]);
	else
		sirf_usp_rx_enable(usp[port]);
}

void sirf_usp_pcm_stop(int port, int playback)
{
	if (playback)
		sirf_usp_tx_disable(usp[port]);
	else
		sirf_usp_rx_disable(usp[port]);
}

void sirf_usp_pcm_params(int port, int playback, int channels, int rate)
{
	u32 data_len = 16;
	u32 frame_len, shifter_len;

	shifter_len = data_len;

	/* SYNC mode I2S or DSP_A*/
	if (usp[port]->daifmt_format & SND_SOC_DAIFMT_I2S) {
		regmap_update_bits(usp[port]->regmap, USP_RX_FRAME_CTRL,
				USP_I2S_SYNC_CHG, USP_I2S_SYNC_CHG);
		frame_len = data_len;
	} else {
		regmap_update_bits(usp[port]->regmap, USP_RX_FRAME_CTRL,
				USP_I2S_SYNC_CHG, 0);
		frame_len = data_len * channels;
	}
	data_len = frame_len;

	if (playback)
		regmap_update_bits(usp[port]->regmap, USP_TX_FRAME_CTRL,
			USP_TXC_DATA_LEN_MASK | USP_TXC_FRAME_LEN_MASK
			| USP_TXC_SHIFTER_LEN_MASK | USP_TXC_SLAVE_CLK_SAMPLE,
			((data_len - 1) << USP_TXC_DATA_LEN_OFFSET)
			| ((frame_len - 1) << USP_TXC_FRAME_LEN_OFFSET)
			| ((shifter_len - 1) << USP_TXC_SHIFTER_LEN_OFFSET)
			| USP_TXC_SLAVE_CLK_SAMPLE);
	else {
		regmap_update_bits(usp[port]->regmap, USP_RX_FRAME_CTRL,
			USP_RXC_DATA_LEN_MASK | USP_RXC_FRAME_LEN_MASK
			| USP_RXC_SHIFTER_LEN_MASK | USP_SINGLE_SYNC_MODE,
			((data_len - 1) << USP_RXC_DATA_LEN_OFFSET)
			| ((frame_len - 1) << USP_RXC_FRAME_LEN_OFFSET)
			| ((shifter_len - 1) << USP_RXC_SHIFTER_LEN_OFFSET)
			| USP_SINGLE_SYNC_MODE);
		/*
		 * In single sync mode, TFS is used both as TX and RX, and is
		 * driven by peer. So it should be set to slave mode.
		 */
		regmap_update_bits(usp[port]->regmap, USP_TX_FRAME_CTRL,
			USP_TXC_SLAVE_CLK_SAMPLE, USP_TXC_SLAVE_CLK_SAMPLE);
	}
}

static void sirf_usp_i2s_init(struct sirf_usp *usp)
{
	/* FIFO level check threshold in dwords */
	const int fifo_l = 16 / 4;
	const int fifo_m = (usp->fifo_size / 2) / 4;
	const int fifo_h = (usp->fifo_size - 16) / 4;

	/* Configure RISC mode */
	regmap_update_bits(usp->regmap, USP_RISC_DSP_MODE,
		USP_RISC_DSP_SEL, ~USP_RISC_DSP_SEL);

	/*
	 * Configure DMA IO Length register
	 * Set no limit, USP can receive data continuously until it is diabled
	 */
	regmap_write(usp->regmap, USP_TX_DMA_IO_LEN, 0);
	regmap_write(usp->regmap, USP_RX_DMA_IO_LEN, 0);

	/* Configure Mode2 register */
	regmap_write(usp->regmap, USP_MODE2, (1 << USP_RXD_DELAY_LEN_OFFSET) |
		(0 << USP_TXD_DELAY_LEN_OFFSET) |
		USP_TFS_CLK_SLAVE_MODE | USP_RFS_CLK_SLAVE_MODE);

	/* Configure Mode1 register */
	regmap_write(usp->regmap, USP_MODE1,
		USP_SYNC_MODE | USP_EN | USP_TXD_ACT_EDGE_FALLING |
		USP_RFS_ACT_LEVEL_LOGIC1 | USP_TFS_ACT_LEVEL_LOGIC1 |
		USP_TX_UFLOW_REPEAT_ZERO | USP_CLOCK_MODE_SLAVE);

	/* Configure RX DMA IO Control register */
	regmap_write(usp->regmap, USP_RX_DMA_IO_CTRL, 0);

	/* Configure RX FIFO Control register */
	regmap_write(usp->regmap, USP_RX_FIFO_CTRL,
		((usp->fifo_size / 2) << USP_RX_FIFO_THD_OFFSET) |
		(USP_TX_RX_FIFO_WIDTH_DWORD << USP_RX_FIFO_WIDTH_OFFSET));

	/* Configure RX FIFO Level Check register */
	regmap_write(usp->regmap, USP_RX_FIFO_LEVEL_CHK,
		RX_FIFO_SC(fifo_l) | RX_FIFO_LC(fifo_m) | RX_FIFO_HC(fifo_h));

	/* Configure TX DMA IO Control register*/
	regmap_write(usp->regmap, USP_TX_DMA_IO_CTRL, 0);

	/* Configure TX FIFO Control register */
	regmap_write(usp->regmap, USP_TX_FIFO_CTRL,
		((usp->fifo_size / 2) << USP_TX_FIFO_THD_OFFSET) |
		(USP_TX_RX_FIFO_WIDTH_DWORD << USP_TX_FIFO_WIDTH_OFFSET));

	/* Congiure TX FIFO Level Check register */
	regmap_write(usp->regmap, USP_TX_FIFO_LEVEL_CHK,
		TX_FIFO_SC(fifo_h) | TX_FIFO_LC(fifo_m) | TX_FIFO_HC(fifo_l));

}

static const struct regmap_config sirf_usp_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = USP_RX_FIFO_DATA,
	.cache_type = REGCACHE_NONE,
};

#ifdef CONFIG_PM_SLEEP
static int sirf_usp_pcm_suspend(struct device *dev)
{
	int i;
	struct sirf_usp *usp = dev_get_drvdata(dev);

	if (usp->is_atlas7_bt_usp)
		for (i = 0; i < ARRAY_SIZE(a7_exclks); i++)
			clk_disable_unprepare(usp->exclks[i]);
	clk_disable_unprepare(usp->clk);

	return 0;
}

static int sirf_usp_pcm_resume(struct device *dev)
{
	int ret;
	int i;
	struct sirf_usp *usp = dev_get_drvdata(dev);

	ret = clk_prepare_enable(usp->clk);
	if (ret) {
		dev_err(dev, "clk_enable failed: %d\n", ret);
		return ret;
	}

	if (usp->is_atlas7_bt_usp) {
		for (i = 0; i < ARRAY_SIZE(a7_exclks); i++) {
			ret = clk_prepare_enable(usp->exclks[i]);
			if (ret) {
				dev_err(dev, "%s exclk enable failed: %d\n",
						a7_exclks[i], ret);
				return ret;
			}
		}
	}
	sirf_usp_i2s_init(usp);
	return 0;
}
#endif

static int sirf_usp_pcm_probe(struct platform_device *pdev)
{
	int ret;
	void __iomem *base;
	struct resource *mem_res;
	int i;
	int port;
	const char *fs_mode;
	bool is_atlas7_bt_usp = false;

	if (of_property_read_u32(pdev->dev.of_node, "cell-index", &port)) {
		dev_err(&pdev->dev, "Fail to get USP index\n");
		return -ENODEV;
	}

	usp[port] = devm_kzalloc(&pdev->dev, sizeof(struct sirf_usp),
			GFP_KERNEL);
	if (!(usp[port]))
		return -ENOMEM;

	platform_set_drvdata(pdev, usp[port]);

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap(&pdev->dev, mem_res->start,
		resource_size(mem_res));
	if (base == NULL)
		return -ENOMEM;
	usp[port]->regmap = devm_regmap_init_mmio(&pdev->dev, base,
					    &sirf_usp_regmap_config);
	if (IS_ERR(usp[port]->regmap))
		return PTR_ERR(usp[port]->regmap);

	usp[port]->fifo_size = -1;
	if (of_property_read_u32(pdev->dev.of_node, "fifosize",
				&usp[port]->fifo_size))
		usp[port]->fifo_size = USP_FIFO_SIZE;

	usp[port]->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(usp[port]->clk)) {
		dev_err(&pdev->dev, "Get clock failed.\n");
		return PTR_ERR(usp[port]->clk);
	}

	if (of_device_is_compatible(pdev->dev.of_node, "sirf,atlas7-bt-usp")) {
		for (i = 0; i < ARRAY_SIZE(a7_exclks); i++) {
			usp[port]->exclks[i] = devm_clk_get(&pdev->dev,
				a7_exclks[i]);
			if (IS_ERR(usp[port]->exclks[i])) {
				dev_err(&pdev->dev, "Get clock failed.\n");
				return PTR_ERR(usp[port]->exclks[i]);
			}
		}
		is_atlas7_bt_usp = true;
	}

	ret = of_property_read_string(pdev->dev.of_node, "frame-sync-mode",
			&fs_mode);
	if (ret == 0) {
		if (!strcmp("i2s", fs_mode))
			usp[port]->daifmt_format |= SND_SOC_DAIFMT_I2S;
	}


	usp[port]->is_atlas7_bt_usp = is_atlas7_bt_usp;
	platform_set_drvdata(pdev, usp[port]);
	ret = sirf_usp_pcm_resume(&pdev->dev);
	if (ret)
		return ret;
	return 0;
}

static const struct of_device_id sirf_usp_pcm_of_match[] = {
	{ .compatible = "sirf,atlas7-bt-usp", },
	{ .compatible = "sirf,prima2-usp-pcm", },
	{}
};
MODULE_DEVICE_TABLE(of, sirf_usp_pcm_of_match);

static const struct dev_pm_ops sirf_usp_pcm_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sirf_usp_pcm_suspend, sirf_usp_pcm_resume)
};

static struct platform_driver sirf_usp_pcm_driver = {
	.driver = {
		.name = "sirf-usp-pcm",
		.of_match_table = sirf_usp_pcm_of_match,
		.pm = &sirf_usp_pcm_pm_ops,
	},
	.probe = sirf_usp_pcm_probe,
};

module_platform_driver(sirf_usp_pcm_driver);
MODULE_DESCRIPTION("CSRAtlas7 SoC USP PCM bus driver by kalimnba used");
MODULE_LICENSE("GPL v2");
