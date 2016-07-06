/*
 * SiRF ATLAS7 IACC(internal audio codec cotroller) driver
 *
 * Copyright (c) 2015, 2016 The Linux Foundation. All rights reserved.
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
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/clk.h>
#include <linux/regmap.h>

#include "../atlas7-iacc.h"
#include "iacc.h"
#include "../../codecs/sirf-atlas7-codec.h"

#define IACC_TX_CHANNELS	4
#define IACC_RX_CHANNELS	2

struct atlas7_iacc {
	struct clk *clk;
	struct regmap *regmap;
	struct regmap *atlas7_codec_regmap;
	struct mutex tx_mutex;
	struct mutex rx_mutex;
	int tx_count;
	int rx_count;
};

static struct atlas7_iacc *atlas7_iacc;

static void atlas7_iacc_tx_enable(struct atlas7_iacc *atlas7_iacc,
	int channels)
{
	int i;

	mutex_lock(&atlas7_iacc->tx_mutex);
	if (atlas7_iacc->tx_count == 0) {
		if (channels == IACC_TX_CHANNELS)
			regmap_update_bits(atlas7_iacc->regmap,
				INTCODECCTL_MODE_CTRL,
				TX_SYNC_EN | TX_START_SYNC_EN,
				TX_SYNC_EN | TX_START_SYNC_EN);

		for (i = 0; i < channels; i++) {
			regmap_update_bits(atlas7_iacc->regmap,
				INTCODECCTL_TX_RX_EN,
				DAC_EN << i, DAC_EN << i);
			regmap_update_bits(atlas7_iacc->regmap,
				INTCODECCTL_TXFIFO0_OP + (i * 20),
				FIFO_RESET, FIFO_RESET);
			regmap_update_bits(atlas7_iacc->regmap,
				INTCODECCTL_TXFIFO0_OP + (i * 20),
				FIFO_RESET, ~FIFO_RESET);

			regmap_write(atlas7_iacc->regmap,
				INTCODECCTL_TXFIFO0_INT_MSK + (i * 20), 0);
			regmap_update_bits(atlas7_iacc->regmap,
				INTCODECCTL_TXFIFO0_OP + (i * 20),
				FIFO_START, FIFO_START);
		}
	}
	atlas7_iacc->tx_count++;
	mutex_unlock(&atlas7_iacc->tx_mutex);
}

static void atlas7_iacc_tx_disable(struct atlas7_iacc *atlas7_iacc)
{
	int i;

	mutex_lock(&atlas7_iacc->tx_mutex);
	atlas7_iacc->tx_count--;
	if (atlas7_iacc->tx_count == 0) {
		for (i = 0; i < IACC_TX_CHANNELS; i++) {
			regmap_write(atlas7_iacc->regmap,
				INTCODECCTL_TXFIFO0_OP + (i * 20), 0);
			regmap_update_bits(atlas7_iacc->regmap,
				INTCODECCTL_TX_RX_EN, DAC_EN << i, 0);
		}
	}
	mutex_unlock(&atlas7_iacc->tx_mutex);
}

static void atlas7_iacc_rx_enable(struct atlas7_iacc *atlas7_iacc,
	int channels)
{
	int i;
	u32 rx_dma_ctrl = 0;

	mutex_lock(&atlas7_iacc->rx_mutex);
	if (atlas7_iacc->rx_count == 0) {
		for (i = 0; i < channels; i++) {
			regmap_update_bits(atlas7_iacc->regmap,
				INTCODECCTL_TX_RX_EN, ADC_EN << i, ADC_EN << i);
			rx_dma_ctrl |= (1 << i);
		}
		regmap_update_bits(atlas7_iacc->regmap, INTCODECCTL_RXFIFO0_OP,
			RX_DMA_CTRL_MASK, rx_dma_ctrl << RX_DMA_CTRL_SHIFT);
	}
	atlas7_iacc->rx_count++;
	mutex_unlock(&atlas7_iacc->rx_mutex);
}

static void atlas7_iacc_rx_disable(struct atlas7_iacc *atlas7_iacc)
{
	int i;

	mutex_lock(&atlas7_iacc->rx_mutex);
	atlas7_iacc->rx_count--;
	if (atlas7_iacc->rx_count == 0) {
		for (i = 0; i < IACC_RX_CHANNELS; i++)
			regmap_update_bits(atlas7_iacc->regmap,
				INTCODECCTL_TX_RX_EN, ADC_EN << i, 0);
	}
	mutex_unlock(&atlas7_iacc->rx_mutex);
}

struct rate_reg_values_t {
	unsigned int rate;
	u32 value;
};

static struct rate_reg_values_t rate_dac_reg_values[] = {
	{32000, DAC_BASE_SMAPLE_RATE_32K0},
	{44100, DAC_BASE_SMAPLE_RATE_44K1},
	{48000, DAC_BASE_SMAPLE_RATE_48K0},
	{96000, DAC_BASE_SMAPLE_RATE_96K0},
	{192000, DAC_BASE_SMAPLE_RATE_192K0},
};

static struct rate_reg_values_t rate_adc_reg_values[] = {
	{8000, ADC_SAMPLE_RATE_08K},
	{11025, ADC_SAMPLE_RATE_11K},
	{16000, ADC_SAMPLE_RATE_16K},
	{22050, ADC_SAMPLE_RATE_22K},
	{32000, ADC_SAMPLE_RATE_32K},
	{44100, ADC_SAMPLE_RATE_44K},
	{48000, ADC_SAMPLE_RATE_48K},
	{96000, ADC_SAMPLE_RATE_96K},
};

static u32 rate_to_reg(int playback, int rate)
{
	int i;

	if (playback) {
		for (i = 0; i < ARRAY_SIZE(rate_dac_reg_values); i++) {
			if (rate_dac_reg_values[i].rate == rate)
				return KCODEC_DAC_SELECT_EXT
					| rate_dac_reg_values[i].value
					<< KCODEC_DAC_EXT_BASE_SAMP_RATE_SHIFT;
		}
	} else {
		for (i = 0; i < ARRAY_SIZE(rate_adc_reg_values); i++) {
			if (rate_adc_reg_values[i].rate == rate)
				return rate_adc_reg_values[i].value;
		}
	}
	return 0;
}

static u32 dac_sample_rate_regs[] = {
	KCODEC_DAC_A_SAMP_RATE,
	KCODEC_DAC_B_SAMP_RATE,
	KCODEC_DAC_C_SAMP_RATE,
	KCODEC_DAC_D_SAMP_RATE
};

static int atlas7_codec_setup(int pchannels, int rchannels,
	enum iacc_input_path path, u32 SampleRate)
{
	int i;

	/* DACx config */
	regmap_update_bits(atlas7_iacc->atlas7_codec_regmap, AUDIO_KCODEC_CTRL,
			KCODEC_DAC_EN, KCODEC_DAC_EN);
	regmap_update_bits(atlas7_iacc->atlas7_codec_regmap, KCODEC_CONFIG,
			1 << 12, 1 << 12);
	if (pchannels == 4) {
		regmap_update_bits(atlas7_iacc->atlas7_codec_regmap,
			KCODEC_CONFIG, 1 << 13, 1 << 13);
		regmap_update_bits(atlas7_iacc->atlas7_codec_regmap,
			KCODEC_CONFIG2, 1 << 12, 1 << 12);
		regmap_update_bits(atlas7_iacc->atlas7_codec_regmap,
			KCODEC_CONFIG2,	1 << 13, 1 << 13);
	}

	/* Sample rate */
	for (i = 0; i < pchannels; i++)
		regmap_write(atlas7_iacc->atlas7_codec_regmap,
			dac_sample_rate_regs[i], rate_to_reg(1, SampleRate));

	if (rchannels == 0)
		return 0;


	/* ADCx */
	regmap_update_bits(atlas7_iacc->atlas7_codec_regmap, AUDIO_KCODEC_CTRL,
		KCODEC_ADC_EN, KCODEC_ADC_EN);
	for (i = 0; i < rchannels; i++)
		regmap_update_bits(atlas7_iacc->atlas7_codec_regmap,
			KCODEC_CONFIG, 1 << (10 + i), 1 << (10 + i));

	regmap_write(atlas7_iacc->atlas7_codec_regmap, AUDIO_ANA_ADC_CTRL0,
		0x1850);

	/* Sample rate */
	for (i = 0; i < rchannels; i++)
		regmap_write(atlas7_iacc->atlas7_codec_regmap,
			KCODEC_ADC_A_SAMP_RATE + (i * 0x20),
			rate_to_reg(0, SampleRate));
	return 0;
}

void atlas7_codec_release(void)
{
	int i;

	for (i = 0; i < 2; i++)
		regmap_update_bits(atlas7_iacc->atlas7_codec_regmap,
			KCODEC_CONFIG, 1 << (10 + i), 0);

	regmap_update_bits(atlas7_iacc->atlas7_codec_regmap, AUDIO_KCODEC_CTRL,
		KCODEC_ADC_EN, 0);

	regmap_update_bits(atlas7_iacc->atlas7_codec_regmap, KCODEC_CONFIG,
			1 << 12, 0);
	regmap_update_bits(atlas7_iacc->atlas7_codec_regmap, KCODEC_CONFIG,
			1 << 13, 0);
	regmap_update_bits(atlas7_iacc->atlas7_codec_regmap, KCODEC_CONFIG2,
			1 << 12, 0);
	regmap_update_bits(atlas7_iacc->atlas7_codec_regmap, KCODEC_CONFIG2,
			1 << 13, 0);
	regmap_update_bits(atlas7_iacc->atlas7_codec_regmap, AUDIO_KCODEC_CTRL,
			KCODEC_DAC_EN, 0);
}

int iacc_setup(int pchannels, int rchannels,
	enum iacc_input_path path, u32 SampleRate, u32 format)
{
	int ret;

	ret = atlas7_codec_setup(pchannels, rchannels, path, SampleRate);
	if (ret < 0)
		return ret;

	if (format == 0x00000001) {
		regmap_update_bits(atlas7_iacc->regmap,
			INTCODECCTL_MODE_CTRL, TX_24BIT, 0);
		regmap_update_bits(atlas7_iacc->regmap,
			INTCODECCTL_MODE_CTRL, RX_24BIT, 0);
	} else if (format == 0x00000002) {
		regmap_update_bits(atlas7_iacc->regmap,
			INTCODECCTL_MODE_CTRL, TX_24BIT, 1);
		regmap_update_bits(atlas7_iacc->regmap,
			INTCODECCTL_MODE_CTRL, RX_24BIT, 1);
	}
	return 0;
}

/* TODO: Remove this function after Kalimba takes over the job */
void iacc_start(int playback, int channels)
{
	if (playback)
		atlas7_iacc_tx_enable(atlas7_iacc, channels);
	else
		atlas7_iacc_rx_enable(atlas7_iacc, channels);
}

/* TODO: Remove this function after Kalimba takes over the job */
void iacc_stop(int playback)
{
	if (playback)
		atlas7_iacc_tx_disable(atlas7_iacc);
	else
		atlas7_iacc_rx_disable(atlas7_iacc);
}

static const struct regmap_config atlas7_iacc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = INTCODECCTL_RXFIFO2_INT_MSK,
	.cache_type = REGCACHE_NONE,
};

static const struct regmap_config atlas7_codec_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = KCODEC_WARP_UPDATE,
	.cache_type = REGCACHE_NONE,
};

#ifdef CONFIG_PM_SLEEP
static int sirf_iacc_suspend(struct device *dev)
{
	struct atlas7_iacc *atlas7_iacc = dev_get_drvdata(dev);

	clk_disable_unprepare(atlas7_iacc->clk);
	return 0;
}

static int sirf_iacc_resume(struct device *dev)
{
	int ret;
	struct atlas7_iacc *atlas7_iacc = dev_get_drvdata(dev);

	ret = clk_prepare_enable(atlas7_iacc->clk);
	if (ret) {
		dev_err(dev, "clk_enable failed: %d\n", ret);
		return ret;
	}
	return 0;
}
#endif

#define IACC_AUDIO_CODEC_REGS_BASE			0x10E30000
#define IACC_AUDIO_CODEC_REGS_SIZE			0x400

static int atlas7_iacc_probe(struct platform_device *pdev)
{
	int ret;
	struct clk *clk;
	struct resource *mem_res;
	void __iomem *base;

	atlas7_iacc = devm_kzalloc(&pdev->dev,
			sizeof(struct atlas7_iacc), GFP_KERNEL);
	if (!atlas7_iacc)
		return -ENOMEM;

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, mem_res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	atlas7_iacc->regmap = devm_regmap_init_mmio(&pdev->dev, base,
					    &atlas7_iacc_regmap_config);

	if (IS_ERR(atlas7_iacc->regmap))
		return PTR_ERR(atlas7_iacc->regmap);

	base = devm_ioremap_nocache(&pdev->dev, IACC_AUDIO_CODEC_REGS_BASE,
		IACC_AUDIO_CODEC_REGS_SIZE);
	if (IS_ERR(base))
		return PTR_ERR(base);

	atlas7_iacc->atlas7_codec_regmap = devm_regmap_init_mmio(&pdev->dev,
		base, &atlas7_codec_regmap_config);
	if (IS_ERR(atlas7_iacc->atlas7_codec_regmap))
		return PTR_ERR(atlas7_iacc->atlas7_codec_regmap);

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "Get clock failed.\n");
		ret = PTR_ERR(clk);
		return ret;
	}

	atlas7_iacc->clk = clk;

	mutex_init(&atlas7_iacc->tx_mutex);
	mutex_init(&atlas7_iacc->rx_mutex);
	platform_set_drvdata(pdev, atlas7_iacc);

	return sirf_iacc_resume(&pdev->dev);
}

static int atlas7_iacc_remove(struct platform_device *pdev)
{
	return sirf_iacc_suspend(&pdev->dev);
}

static const struct dev_pm_ops sirf_iacc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sirf_iacc_suspend, sirf_iacc_resume)
};

static const struct of_device_id atlas7_iacc_of_match[] = {
	{ .compatible = "sirf,atlas7-iacc", },
	{}
};
MODULE_DEVICE_TABLE(of, atlas7_iacc_of_match);

static struct platform_driver atlas7_iacc_driver = {
	.driver = {
		.name = "sirf-atlas7-iacc",
		.of_match_table = atlas7_iacc_of_match,
		.pm = &sirf_iacc_pm_ops,
	},
	.probe = atlas7_iacc_probe,
	.remove = atlas7_iacc_remove,
};

module_platform_driver(atlas7_iacc_driver);

MODULE_DESCRIPTION("SiRF ATLAS7 IACC(internal audio codec cotroller) driver");
MODULE_LICENSE("GPL v2");
