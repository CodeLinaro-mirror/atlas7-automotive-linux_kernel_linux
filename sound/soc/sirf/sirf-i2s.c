/*
 * Copyright (c) 2013-2016, The Linux Foundation. All rights reserved.
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

#include <linux/module.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/dmaengine_pcm.h>

#include "sirf-i2s.h"

struct sirf_i2s {
	struct regmap *regmap;
	struct clk *clk;
	u32 i2s_ctrl;
	u32 i2s_ctrl_tx_rx_en;
	u32 tdm_ctrl, tdm_bclk_ratio, tdm_tx_mask, tdm_rx_mask;
	bool master;
	bool clkout;
	int clk_id;
	int sysclk;
	struct snd_dmaengine_dai_dma_data playback_dma_data;
	struct snd_dmaengine_dai_dma_data capture_dma_data;

	/* Atlas7 specific */
	bool is_atlas7;
	struct clk *clk_audioif;
	struct clk *clk_mux, *clk_dto;
};

static int sirf_i2s_dai_probe(struct snd_soc_dai *dai)
{
	struct sirf_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	snd_soc_dai_init_dma_data(dai, &i2s->playback_dma_data,
			&i2s->capture_dma_data);
	return 0;
}

static void sirf_i2s_tx_enable(struct sirf_i2s *i2s)
{
	/* Reset TDM playback logic */
	regmap_update_bits(i2s->regmap, AUDIO_CTRL_I2S_TDM_CTRL,
			I2S_TDM_TX_RESET, I2S_TDM_TX_RESET);
	regmap_update_bits(i2s->regmap, AUDIO_CTRL_I2S_TDM_CTRL,
			I2S_TDM_TX_RESET, 0);

	/* First start the FIFO, then enable the tx/rx */
	regmap_update_bits(i2s->regmap, AUDIO_CTRL_I2S_TXFIFO_OP,
		AUDIO_FIFO_RESET, AUDIO_FIFO_RESET);
	regmap_update_bits(i2s->regmap, AUDIO_CTRL_I2S_TXFIFO_OP,
		AUDIO_FIFO_RESET, 0);
	regmap_update_bits(i2s->regmap, AUDIO_CTRL_I2S_TXFIFO_OP,
		AUDIO_FIFO_START, AUDIO_FIFO_START);
	regmap_update_bits(i2s->regmap, AUDIO_CTRL_I2S_TX_RX_EN,
		I2S_TX_ENABLE | I2S_DOUT_OE,
		I2S_TX_ENABLE | I2S_DOUT_OE);
}

static void sirf_i2s_tx_disable(struct sirf_i2s *i2s)
{
	regmap_update_bits(i2s->regmap, AUDIO_CTRL_I2S_TX_RX_EN,
		I2S_TX_ENABLE, ~I2S_TX_ENABLE);
	/* First disable the tx/rx, then stop the FIFO */
	regmap_write(i2s->regmap, AUDIO_CTRL_I2S_TXFIFO_OP, 0);
}

static void sirf_i2s_rx_enable(struct sirf_i2s *i2s)
{
	/* Reset TDM record logic */
	regmap_update_bits(i2s->regmap, AUDIO_CTRL_I2S_TDM_CTRL,
			I2S_TDM_RX_RESET, I2S_TDM_RX_RESET);
	regmap_update_bits(i2s->regmap, AUDIO_CTRL_I2S_TDM_CTRL,
			I2S_TDM_RX_RESET, 0);

	/* First start the FIFO, then enable the tx/rx */
	regmap_update_bits(i2s->regmap, AUDIO_CTRL_I2S_RXFIFO_OP,
		AUDIO_FIFO_RESET, AUDIO_FIFO_RESET);
	regmap_update_bits(i2s->regmap, AUDIO_CTRL_I2S_RXFIFO_OP,
		AUDIO_FIFO_RESET, 0);
	regmap_update_bits(i2s->regmap, AUDIO_CTRL_I2S_RXFIFO_OP,
		AUDIO_FIFO_START, AUDIO_FIFO_START);
	regmap_update_bits(i2s->regmap, AUDIO_CTRL_I2S_TX_RX_EN,
		I2S_RX_ENABLE, I2S_RX_ENABLE);
}

static void sirf_i2s_rx_disable(struct sirf_i2s *i2s)
{
	regmap_update_bits(i2s->regmap, AUDIO_CTRL_I2S_TX_RX_EN,
		I2S_RX_ENABLE, ~I2S_RX_ENABLE);
	/* First disable the tx/rx, then stop the FIFO */
	regmap_write(i2s->regmap, AUDIO_CTRL_I2S_RXFIFO_OP, 0);
}

static int sirf_i2s_trigger(struct snd_pcm_substream *substream,
		int cmd, struct snd_soc_dai *dai)
{
	struct sirf_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	int playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (playback)
			sirf_i2s_tx_enable(i2s);
		else
			sirf_i2s_rx_enable(i2s);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (playback)
			sirf_i2s_tx_disable(i2s);
		else
			sirf_i2s_rx_disable(i2s);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int sirf_i2s_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct sirf_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	u32 i2s_ctrl = 0;
	u32 i2s_tx_rx_ctrl = 0, i2s_tx_rx_mask = 0;
	u32 tdm_ctrl = 0, tdm_mask = BIT(0);
	u32 left_len, frame_len;
	int channels = params_channels(params);
	int playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);
	u32 bitclk;
	u32 bclk_div;
	u32 div;
	u32 bclk_ratio = i2s->tdm_bclk_ratio;

	switch (channels) {
	case 2:
		i2s_ctrl &= ~I2S_SIX_CHANNELS;
		break;
	case 4:
	case 6:
	case 8:
		/* TDM */
		regmap_read(i2s->regmap, AUDIO_CTRL_I2S_TDM_CTRL, &tdm_ctrl);
		if (playback) {
			tdm_mask = I2S_TDM_MASK_TX;
			tdm_ctrl &= ~tdm_mask;
			tdm_ctrl |= I2S_TDM_DAC_CH(channels);
			tdm_ctrl |= i2s->tdm_tx_mask;
		} else {
			tdm_mask = I2S_TDM_MASK_RX;
			tdm_ctrl &= ~tdm_mask;
			tdm_ctrl |= I2S_TDM_ADC_CH(channels);
			tdm_ctrl |= i2s->tdm_rx_mask;
		}
		tdm_ctrl |= I2S_TDM_ENA;	/* Start TDM */
		tdm_ctrl |= i2s->tdm_ctrl;	/* Frame sync and polarity */
		break;
	default:
		dev_err(dai->dev, "%d channels unsupported\n", channels);
		return -EINVAL;
	}

	regmap_update_bits(i2s->regmap, AUDIO_CTRL_I2S_TDM_CTRL,
			tdm_mask, tdm_ctrl);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		left_len = 8;
		frame_len = 16;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		left_len = 16;
		frame_len = 32;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		left_len = 24;
		frame_len = 64;
		break;
	default:
		dev_err(dai->dev, "Format unsupported\n");
		return -EINVAL;
	}

	if (bclk_ratio == 0)
		bclk_ratio = frame_len;

	/* Atlas7 supports 24bit resolution */
	if (i2s->is_atlas7) {
		if (playback) {
			i2s_tx_rx_mask |= I2S_TX_24BIT_ATLAS7;
			i2s_tx_rx_ctrl |=
				(left_len == 24 ? I2S_TX_24BIT_ATLAS7 : 0);
		} else {
			i2s_tx_rx_mask |= I2S_RX_24BIT_ATLAS7;
			i2s_tx_rx_ctrl |=
				(left_len == 24 ? I2S_RX_24BIT_ATLAS7 : 0);
		}
	}

	/* Fill the actual len - 1 */
	i2s_ctrl |= ((frame_len - 1) << I2S_FRAME_LEN_SHIFT)
		| ((left_len - 1) << I2S_L_CHAN_LEN_SHIFT);

	if (i2s->master) {
		i2s_ctrl &= ~I2S_SLAVE_MODE;
		bitclk = params_rate(params) * bclk_ratio;
		div = i2s->sysclk / bitclk;
		/* MCLK divide-by-2 from source clk */
		div /= 2;
		bclk_div = div / 2 - 1;
		i2s_ctrl |= (bclk_div << I2S_BITCLK_DIV_SHIFT);
		/*
		 * MCLK coefficient must set to 0, means
		 * divide-by-two from reference clock.
		 */
		i2s_ctrl &= ~I2S_MCLK_DIV_MASK;
	} else {
		i2s_ctrl |= I2S_SLAVE_MODE;
	}

	i2s_tx_rx_mask |= I2S_REF_CLK_SEL_EXT;
	i2s_tx_rx_ctrl |=
		(i2s->clk_id == SIRF_I2S_EXT_CLK ? I2S_REF_CLK_SEL_EXT : 0);

	i2s_tx_rx_mask |= I2S_MCLK_EN;
	i2s_tx_rx_ctrl |= (i2s->clkout ? I2S_MCLK_EN : 0);

	regmap_write(i2s->regmap, AUDIO_CTRL_I2S_CTRL, i2s_ctrl);
	regmap_update_bits(i2s->regmap, AUDIO_CTRL_I2S_TX_RX_EN,
			i2s_tx_rx_mask, i2s_tx_rx_ctrl);
	if (!i2s->is_atlas7)
		regmap_update_bits(i2s->regmap, AUDIO_CTRL_MODE_SEL,
				I2S_MODE, I2S_MODE);

	return 0;
}

static int sirf_i2s_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct sirf_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		i2s->master = false;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		i2s->master = true;
		break;
	default:
		return -EINVAL;
	}

	i2s->tdm_ctrl = 0;

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		i2s->tdm_ctrl |= I2S_TDM_FRAME_SYNC_I2S;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		i2s->tdm_ctrl |= I2S_TDM_FRAME_SYNC_DSP0;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		i2s->tdm_ctrl |= I2S_TDM_FRAME_SYNC_DSP1;
		break;
	default:
		return -EINVAL;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		i2s->tdm_ctrl |= I2S_TDM_FRAME_POLARITY_HIGH;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		i2s->tdm_ctrl |= I2S_TDM_FRAME_POLARITY_LOW;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int sirf_i2s_set_sysclk(struct snd_soc_dai *dai, int clk_id,
	unsigned int freq, int dir)
{
	struct sirf_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	i2s->clkout = (dir == SND_SOC_CLOCK_OUT);

	switch (clk_id) {
	case SIRF_I2S_EXT_CLK:
		i2s->clkout = false;
		break;
	case SIRF_I2S_PWM_CLK:
		if (i2s->is_atlas7)
			return -EINVAL;	/* PWM deprecated in Atlas7 */
		break;
	case SIRF_I2S_DTO_CLK:
		if (!i2s->is_atlas7)	/* DTO only available in Atlas7 */
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	if (clk_id == SIRF_I2S_DTO_CLK) {
		if (clk_get_parent(i2s->clk_mux) != i2s->clk_dto)
			clk_set_parent(i2s->clk_mux, i2s->clk_dto);
		clk_set_rate(i2s->clk_dto, freq);
	}

	i2s->clk_id = clk_id;
	i2s->sysclk = freq;
	return 0;
}

static int sirf_i2s_set_bclk_ratio(struct snd_soc_dai *dai, unsigned int ratio)
{
	struct sirf_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	i2s->tdm_bclk_ratio = ratio;
	return 0;
}

static int sirf_i2s_set_tdm_slot(struct snd_soc_dai *dai, unsigned tx_mask,
		unsigned int rx_mask, int slots, int slot_width)
{
	struct sirf_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	i2s->tdm_tx_mask = tx_mask;
	i2s->tdm_rx_mask = rx_mask;
	return 0;
}

struct snd_soc_dai_ops sirfsoc_i2s_dai_ops = {
	.trigger	= sirf_i2s_trigger,
	.hw_params	= sirf_i2s_hw_params,
	.set_fmt	= sirf_i2s_set_dai_fmt,
	.set_sysclk	= sirf_i2s_set_sysclk,
	.set_bclk_ratio	= sirf_i2s_set_bclk_ratio,
	.set_tdm_slot	= sirf_i2s_set_tdm_slot,
};

static struct snd_soc_dai_driver sirf_i2s_dai = {
	.probe = sirf_i2s_dai_probe,
	.name = "sirf-i2s",
	.id = 0,
	.playback = {
		.stream_name = "SiRF I2S Playback",
		.channels_min = 2,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
	},
	.capture = {
		.stream_name = "SiRF I2S Capture",
		.channels_min = 2,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
	},
	.ops = &sirfsoc_i2s_dai_ops,
};

#ifdef CONFIG_PM_RUNTIME
static int sirf_i2s_runtime_suspend(struct device *dev)
{
	struct sirf_i2s *i2s = dev_get_drvdata(dev);
	clk_disable_unprepare(i2s->clk);
	if (i2s->is_atlas7) {
		clk_disable_unprepare(i2s->clk_audioif);
		clk_disable_unprepare(i2s->clk_mux);
		clk_disable_unprepare(i2s->clk_dto);
	}

	return 0;
}

static int sirf_i2s_runtime_resume(struct device *dev)
{
	int ret;
	struct sirf_i2s *i2s = dev_get_drvdata(dev);

	ret = clk_prepare_enable(i2s->clk);
	if (ret == 0 && i2s->is_atlas7) {
		ret = clk_prepare_enable(i2s->clk_audioif);
		if (ret)
			return ret;
		ret = clk_prepare_enable(i2s->clk_dto);
		if (ret)
			return ret;
		ret = clk_prepare_enable(i2s->clk_mux);
	}

	return ret;
}
#endif

#ifdef CONFIG_PM_SLEEP
static int sirf_i2s_suspend(struct device *dev)
{
	struct sirf_i2s *i2s = dev_get_drvdata(dev);

	if (!pm_runtime_status_suspended(dev)) {
		regmap_read(i2s->regmap, AUDIO_CTRL_I2S_CTRL, &i2s->i2s_ctrl);
		regmap_read(i2s->regmap, AUDIO_CTRL_I2S_TX_RX_EN,
			&i2s->i2s_ctrl_tx_rx_en);
		sirf_i2s_runtime_suspend(dev);
	}
	return 0;
}

static int sirf_i2s_resume(struct device *dev)
{
	struct sirf_i2s *i2s = dev_get_drvdata(dev);
	int ret;
	if (!pm_runtime_status_suspended(dev)) {
		ret = sirf_i2s_runtime_resume(dev);
		if (ret)
			return ret;
		if (!i2s->is_atlas7)
			regmap_update_bits(i2s->regmap, AUDIO_CTRL_MODE_SEL,
					I2S_MODE, I2S_MODE);
		regmap_write(i2s->regmap, AUDIO_CTRL_I2S_CTRL, i2s->i2s_ctrl);
		/* Restore MCLK enable and reference clock select bits. */
		i2s->i2s_ctrl_tx_rx_en &= (I2S_MCLK_EN | I2S_REF_CLK_SEL_EXT);
		regmap_write(i2s->regmap, AUDIO_CTRL_I2S_TX_RX_EN,
			i2s->i2s_ctrl_tx_rx_en);

		regmap_write(i2s->regmap, AUDIO_CTRL_I2S_TXFIFO_INT_MSK, 0);
		regmap_write(i2s->regmap, AUDIO_CTRL_I2S_RXFIFO_INT_MSK, 0);
	}

	return 0;
}
#endif

static const struct snd_soc_component_driver sirf_i2s_component = {
	.name       = "sirf-i2s",
};

static const struct regmap_config sirf_i2s_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = AUDIO_CTRL_I2S_RXFIFO_INT_MSK,
	.cache_type = REGCACHE_NONE,
};

static int sirf_i2s_probe(struct platform_device *pdev)
{
	int ret;
	struct sirf_i2s *i2s;
	void __iomem *base;
	struct resource *mem_res;

	i2s = devm_kzalloc(&pdev->dev, sizeof(struct sirf_i2s),
			GFP_KERNEL);
	if (!i2s)
		return -ENOMEM;

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem_res) {
		dev_err(&pdev->dev, "no mem resource?\n");
		return -ENODEV;
	}

	base = devm_ioremap(&pdev->dev, mem_res->start,
		resource_size(mem_res));
	if (base == NULL)
		return -ENOMEM;

	i2s->regmap = devm_regmap_init_mmio(&pdev->dev, base,
					    &sirf_i2s_regmap_config);
	if (IS_ERR(i2s->regmap))
		return PTR_ERR(i2s->regmap);

	i2s->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(i2s->clk)) {
		dev_err(&pdev->dev, "Get clock failed.\n");
		ret = PTR_ERR(i2s->clk);
		return ret;
	}

	if (of_device_is_compatible(pdev->dev.of_node, "sirf,atlas7-i2s")) {
		i2s->clk_audioif = devm_clk_get(&pdev->dev, "audioif");
		if (IS_ERR(i2s->clk_audioif)) {
			dev_err(&pdev->dev, "Get audio clock failed.\n");
			return PTR_ERR(i2s->clk_audioif);
		}

		i2s->clk_mux = devm_clk_get(&pdev->dev, "i2s_mux");
		if (IS_ERR(i2s->clk_mux)) {
			dev_err(&pdev->dev, "Get I2S clock mux failed.\n");
			return PTR_ERR(i2s->clk_mux);
		}

		i2s->clk_dto = devm_clk_get(&pdev->dev, "audio_dto");
		if (IS_ERR(i2s->clk_dto)) {
			dev_err(&pdev->dev, "Get audio DTO clock failed.\n");
			return PTR_ERR(i2s->clk_dto);
		}

		i2s->is_atlas7 = true;
	} else {
		i2s->is_atlas7 = false;
	}

	pm_runtime_enable(&pdev->dev);

	ret = devm_snd_soc_register_component(&pdev->dev, &sirf_i2s_component,
			&sirf_i2s_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "Register Audio SoC dai failed.\n");
		return ret;
	}

	platform_set_drvdata(pdev, i2s);
	return devm_snd_dmaengine_pcm_register(&pdev->dev, NULL, 0);
}

static int sirf_i2s_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static const struct of_device_id sirf_i2s_of_match[] = {
	{ .compatible = "sirf,prima2-i2s", },
	{ .compatible = "sirf,atlas7-i2s", },
	{}
};
MODULE_DEVICE_TABLE(of, sirf_i2s_of_match);

static const struct dev_pm_ops sirf_i2s_pm_ops = {
	SET_RUNTIME_PM_OPS(sirf_i2s_runtime_suspend, sirf_i2s_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(sirf_i2s_suspend, sirf_i2s_resume)
};

static struct platform_driver sirf_i2s_driver = {
	.driver = {
		.name = "sirf-i2s",
		.owner = THIS_MODULE,
		.of_match_table = sirf_i2s_of_match,
		.pm = &sirf_i2s_pm_ops,
	},
	.probe = sirf_i2s_probe,
	.remove = sirf_i2s_remove,
};

module_platform_driver(sirf_i2s_driver);

MODULE_DESCRIPTION("SiRF SoC I2S driver");
MODULE_LICENSE("GPL v2");
