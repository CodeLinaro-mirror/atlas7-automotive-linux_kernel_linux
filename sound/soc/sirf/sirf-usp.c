/*
 * SiRF USP audio transfer interface like I2S
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */
#include <linux/module.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

#include "sirf-usp.h"

#define FIFO_RESET  0
#define FIFO_START	1
#define FIFO_STOP	2

#define AUDIO_WORD_SIZE 16

struct sirf_usp {
	struct regmap *regmap;
	struct clk *clk;
	u32 mode1_reg;
	u32 mode2_reg;
	int master;
	struct snd_dmaengine_dai_dma_data playback_dma_data;
	struct snd_dmaengine_dai_dma_data capture_dma_data;
};

static void sirf_usp_tx_enable(struct sirf_usp *usp)
{
	regmap_update_bits(usp->regmap, USP_TX_FIFO_OP,
		USP_TX_FIFO_RESET, USP_TX_FIFO_RESET);
	regmap_write(usp->regmap, USP_TX_FIFO_OP, 0);

	regmap_update_bits(usp->regmap, USP_TX_FIFO_OP,
		USP_TX_FIFO_START, USP_TX_FIFO_START);

	regmap_update_bits(usp->regmap, USP_TX_RX_ENABLE,
		USP_TX_ENA, USP_TX_ENA);
}

static void sirf_usp_tx_disable(struct sirf_usp *usp)
{
	regmap_update_bits(usp->regmap, USP_TX_RX_ENABLE,
		USP_TX_ENA, ~USP_TX_ENA);
	/* FIFO stop */
	regmap_write(usp->regmap, USP_TX_FIFO_OP, 0);
}

static void sirf_usp_rx_enable(struct sirf_usp *usp)
{
	regmap_update_bits(usp->regmap, USP_RX_FIFO_OP,
		USP_RX_FIFO_RESET, USP_RX_FIFO_RESET);
	regmap_write(usp->regmap, USP_RX_FIFO_OP, 0);

	regmap_update_bits(usp->regmap, USP_RX_FIFO_OP,
		USP_RX_FIFO_START, USP_RX_FIFO_START);

	regmap_update_bits(usp->regmap, USP_TX_RX_ENABLE,
		USP_RX_ENA, USP_RX_ENA);
}

static void sirf_usp_rx_disable(struct sirf_usp *usp)
{
	regmap_update_bits(usp->regmap, USP_TX_RX_ENABLE,
		USP_RX_ENA, ~USP_RX_ENA);
	/* FIFO stop */
	regmap_write(usp->regmap, USP_RX_FIFO_OP, 0);
}

static int sirf_usp_pcm_dai_probe(struct snd_soc_dai *dai)
{
	struct sirf_usp *usp = snd_soc_dai_get_drvdata(dai);
	snd_soc_dai_init_dma_data(dai, &usp->playback_dma_data,
			&usp->capture_dma_data);
	return 0;
}

static int sirf_usp_pcm_set_dai_fmt(struct snd_soc_dai *dai,
		unsigned int fmt)
{
	struct sirf_usp *usp = snd_soc_dai_get_drvdata(dai);

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		usp->master = 1;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		usp->master = 0;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int sirf_usp_pcm_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct sirf_usp *usp = snd_soc_dai_get_drvdata(dai);
	u32 mode1;
	u32 mode2;
	u32 rate = params_rate(params);
	u32 clk_rate, clk_div, clk_div_hi, clk_div_lo;

	regmap_read(usp->regmap, USP_MODE1, &mode1);
	regmap_read(usp->regmap, USP_MODE2, &mode2);
	if (usp->master) {
		mode1 &= ~USP_CLOCK_MODE_SLAVE;
		mode2 &= ~USP_TFS_CLK_SLAVE_MODE;
		mode2 &= ~USP_RFS_CLK_SLAVE_MODE;
	} else {
		mode1 |= USP_CLOCK_MODE_SLAVE;
		mode2 |= USP_TFS_CLK_SLAVE_MODE;
		mode2 |= USP_RFS_CLK_SLAVE_MODE;
	}
	regmap_write(usp->regmap, USP_MODE1, mode1);
	regmap_write(usp->regmap, USP_MODE2, mode2);

	clk_rate = clk_get_rate(usp->clk);
	if (clk_rate < rate * 2) {
		dev_err(dai->dev, "Can't get rate(%d) by need.\n", rate);
		return -EINVAL;
	}

	clk_div = (clk_rate / (2 * rate)) - 1;
	clk_div_hi = (clk_div & 0xC00) >> 10;
	clk_div_lo = (clk_div & 0x3FF);

	regmap_update_bits(usp->regmap, USP_MODE2, USP_CLK_DIVISOR_MASK,
		clk_div_lo << USP_CLK_DIVISOR_OFFSET);

	regmap_update_bits(usp->regmap, USP_TX_FRAME_CTRL,
		USP_TXC_CLK_DIVISOR_MASK,
		clk_div_hi << USP_TXC_CLK_DIVISOR_OFFSET);

	return 0;
}

static int sirf_usp_pcm_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	int playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	struct sirf_usp *usp = snd_soc_dai_get_drvdata(dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (playback)
			sirf_usp_tx_enable(usp);
		else
			sirf_usp_rx_enable(usp);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (playback)
			sirf_usp_tx_disable(usp);
		else
			sirf_usp_rx_disable(usp);
		break;
	}

	return 0;
}

static const struct snd_soc_dai_ops sirf_usp_pcm_dai_ops = {
	.trigger = sirf_usp_pcm_trigger,
	.set_fmt = sirf_usp_pcm_set_dai_fmt,
	.hw_params = sirf_usp_pcm_hw_params,
};

static struct snd_soc_dai_driver sirf_usp_pcm_dai = {
	.probe = sirf_usp_pcm_dai_probe,
	.name		= "sirf-usp-pcm",
	.id			= 0,
	.playback = {
		.stream_name = "SiRF USP PCM Playback",
		.channels_min = 1,
		.channels_max = 1,
		.rates = SNDRV_PCM_RATE_48000
			| SNDRV_PCM_RATE_44100
			| SNDRV_PCM_RATE_32000
			| SNDRV_PCM_RATE_22050
			| SNDRV_PCM_RATE_16000
			| SNDRV_PCM_RATE_11025
			| SNDRV_PCM_RATE_8000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.stream_name = "SiRF USP PCM Capture",
		.channels_min = 1,
		.channels_max = 1,
		.rates = SNDRV_PCM_RATE_48000
			| SNDRV_PCM_RATE_44100
			| SNDRV_PCM_RATE_32000
			| SNDRV_PCM_RATE_22050
			| SNDRV_PCM_RATE_16000
			| SNDRV_PCM_RATE_11025
			| SNDRV_PCM_RATE_8000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &sirf_usp_pcm_dai_ops,
};

static void sirf_usp_controller_init(struct sirf_usp *usp)
{
	u32 val;

	/* Configure RISC mode */
	regmap_update_bits(usp->regmap, USP_RISC_DSP_MODE,
		USP_RISC_DSP_SEL, ~USP_RISC_DSP_SEL);

	/* Clear all interrupts status */
	regmap_read(usp->regmap, USP_INT_STATUS, &val);
	regmap_write(usp->regmap, USP_INT_STATUS, val);

	/* Configure DMA IO Length register */
	regmap_write(usp->regmap, USP_TX_DMA_IO_LEN, 0);
	regmap_write(usp->regmap, USP_RX_DMA_IO_LEN, 0);

	/* Configure RX Frame Control */
	val = (AUDIO_WORD_SIZE * 2 - 1) << USP_RXC_DATA_LEN_OFFSET;
	val |= (AUDIO_WORD_SIZE * 2 - 1) << USP_RXC_FRAME_LEN_OFFSET;
	val |= (AUDIO_WORD_SIZE * 2 - 1) << USP_RXC_SHIFTER_LEN_OFFSET;
	val |= USP_SINGLE_SYNC_MODE;
	regmap_write(usp->regmap, USP_RX_FRAME_CTRL, val);

	/* Configure TX Frame Control */
	val = (AUDIO_WORD_SIZE * 2 - 1) << USP_TXC_DATA_LEN_OFFSET;
	val |= 0 << USP_TXC_SYNC_LEN_OFFSET;
	val |= (AUDIO_WORD_SIZE * 2 - 1) << USP_TXC_FRAME_LEN_OFFSET;
	val |= (AUDIO_WORD_SIZE * 2 - 1) << USP_TXC_SHIFTER_LEN_OFFSET;
	val |= USP_TXC_SLAVE_CLK_SAMPLE;
	regmap_write(usp->regmap, USP_TX_FRAME_CTRL, val);

	/* Configure Mode2 register */
	val = (1 << USP_RXD_DELAY_LEN_OFFSET) | (0 << USP_TXD_DELAY_LEN_OFFSET);
	val &= ~USP_ENA_CTRL_MODE;
	val &= ~USP_FRAME_CTRL_MODE;
	val &= ~USP_TFS_SOURCE_MODE;
	regmap_write(usp->regmap, USP_MODE2, val);

	/* Configure Mode1 register */
	val = USP_SYNC_MODE;
	val |= USP_ENDIAN_CTRL_LSBF;
	val |= USP_EN;
	val |= USP_RXD_ACT_EDGE_FALLING;
	val &= ~USP_TXD_ACT_EDGE_FALLING;
	val |= USP_RFS_ACT_LEVEL_LOGIC1;
	val |= USP_TFS_ACT_LEVEL_LOGIC1;
	val |= USP_SCLK_IDLE_MODE_TOGGLE;
	val |= USP_SCLK_IDLE_LEVEL_LOGIC1;
	val &= ~USP_SCLK_PIN_MODE_IO;
	val &= ~USP_RFS_PIN_MODE_IO;
	val &= ~USP_TFS_PIN_MODE_IO;
	val &= ~USP_RXD_PIN_MODE_IO;
	val &= ~USP_TXD_PIN_MODE_IO;
	val |= USP_TX_UFLOW_REPEAT_ZERO;
	regmap_write(usp->regmap, USP_MODE1, val);

	/* Configure RX DMA IO Control register */
	regmap_write(usp->regmap, USP_RX_DMA_IO_CTRL, 0);

	/* Congiure RX FIFO Control register */
	regmap_write(usp->regmap, USP_RX_FIFO_CTRL,
		(USP_RX_FIFO_THRESHOLD << USP_RX_FIFO_THD_OFFSET) |
		(USP_TX_RX_FIFO_WIDTH_DWORD << USP_RX_FIFO_WIDTH_OFFSET));

	/* Congiure RX FIFO Level Check register */
	regmap_write(usp->regmap, USP_RX_FIFO_LEVEL_CHK,
		RX_FIFO_SC(0x04) | RX_FIFO_LC(0x0E) | RX_FIFO_HC(0x1B));

	/* Configure TX DMA IO Control register*/
	regmap_write(usp->regmap, USP_TX_DMA_IO_CTRL, 0);

	/* Configure TX FIFO Control register */
	regmap_write(usp->regmap, USP_TX_FIFO_CTRL,
		(USP_TX_FIFO_THRESHOLD << USP_TX_FIFO_THD_OFFSET) |
		(USP_TX_RX_FIFO_WIDTH_DWORD << USP_TX_FIFO_WIDTH_OFFSET));

	/* Congiure TX FIFO Level Check register */
	regmap_write(usp->regmap, USP_TX_FIFO_LEVEL_CHK,
		TX_FIFO_SC(0x1B) | TX_FIFO_LC(0x0E) | TX_FIFO_HC(0x04));

	/* Configure RX FIFO */
	regmap_update_bits(usp->regmap, USP_RX_FIFO_OP,
		USP_RX_FIFO_RESET, USP_RX_FIFO_RESET);
	regmap_write(usp->regmap, USP_RX_FIFO_OP, 0);

	/* Configure TX FIFO */
	regmap_update_bits(usp->regmap, USP_TX_FIFO_OP,
		USP_TX_FIFO_RESET, USP_TX_FIFO_RESET);
	regmap_write(usp->regmap, USP_TX_FIFO_OP, 0);
}

static void sirf_usp_controller_uninit(struct sirf_usp *usp)
{
	/* Disable RX/TX */
	regmap_write(usp->regmap, USP_INT_ENABLE, 0);
	regmap_write(usp->regmap, USP_TX_RX_ENABLE, 0);
}

#ifdef CONFIG_PM_RUNTIME
static int sirf_usp_pcm_runtime_suspend(struct device *dev)
{
	struct sirf_usp *usp = dev_get_drvdata(dev);
	sirf_usp_controller_uninit(usp);
	clk_disable_unprepare(usp->clk);
	return 0;
}

static int sirf_usp_pcm_runtime_resume(struct device *dev)
{
	struct sirf_usp *usp = dev_get_drvdata(dev);
	int ret;
	ret = clk_prepare_enable(usp->clk);
	if (ret)
		return ret;
	sirf_usp_controller_init(usp);
	return 0;
}
#endif

#ifdef CONFIG_PM_SLEEP
static int sirf_usp_pcm_suspend(struct device *dev)
{
	struct sirf_usp *usp = dev_get_drvdata(dev);

	if (!pm_runtime_status_suspended(dev)) {
		regmap_read(usp->regmap, USP_MODE1, &usp->mode1_reg);
		regmap_read(usp->regmap, USP_MODE2, &usp->mode2_reg);
		sirf_usp_pcm_runtime_suspend(dev);
	}
	return 0;
}

static int sirf_usp_pcm_resume(struct device *dev)
{
	struct sirf_usp *usp = dev_get_drvdata(dev);
	int ret;

	if (!pm_runtime_status_suspended(dev)) {
		ret = sirf_usp_pcm_runtime_resume(dev);
		if (ret)
			return ret;
		regmap_write(usp->regmap, USP_MODE1, usp->mode1_reg);
		regmap_write(usp->regmap, USP_MODE2, usp->mode2_reg);
	}
	return 0;
}
#endif

static const struct snd_soc_component_driver sirf_usp_component = {
	.name		= "sirf-usp",
};

static const struct regmap_config sirf_usp_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = USP_RX_FIFO_DATA,
	.cache_type = REGCACHE_NONE,
};

static int sirf_usp_pcm_probe(struct platform_device *pdev)
{
	int ret;
	struct sirf_usp *usp;
	void __iomem *base;
	struct resource *mem_res;

	usp = devm_kzalloc(&pdev->dev, sizeof(struct sirf_usp),
			GFP_KERNEL);
	if (!usp)
		return -ENOMEM;

	platform_set_drvdata(pdev, usp);


	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap(&pdev->dev, mem_res->start,
		resource_size(mem_res));
	if (base == NULL)
		return -ENOMEM;
	usp->regmap = devm_regmap_init_mmio(&pdev->dev, base,
					    &sirf_usp_regmap_config);
	if (IS_ERR(usp->regmap))
		return PTR_ERR(usp->regmap);

	usp->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(usp->clk)) {
		dev_err(&pdev->dev, "Get clock failed.\n");
		return PTR_ERR(usp->clk);
	}

	pm_runtime_enable(&pdev->dev);

	ret = devm_snd_soc_register_component(&pdev->dev, &sirf_usp_component,
		&sirf_usp_pcm_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "Register Audio SoC dai failed.\n");
		return ret;
	}
	return devm_snd_dmaengine_pcm_register(&pdev->dev, NULL, 0);
}

static int sirf_usp_pcm_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	return 0;
}

static const struct of_device_id sirf_usp_pcm_of_match[] = {
	{ .compatible = "sirf,prima2-usp-pcm", },
	{}
};
MODULE_DEVICE_TABLE(of, sirf_usp_pcm_of_match);

static const struct dev_pm_ops sirf_usp_pcm_pm_ops = {
	SET_RUNTIME_PM_OPS(sirf_usp_pcm_runtime_suspend, sirf_usp_pcm_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(sirf_usp_pcm_suspend, sirf_usp_pcm_resume)
};

static struct platform_driver sirf_usp_pcm_driver = {
	.driver = {
		.name = "sirf-usp-pcm",
		.owner = THIS_MODULE,
		.of_match_table = sirf_usp_pcm_of_match,
		.pm = &sirf_usp_pcm_pm_ops,
	},
	.probe = sirf_usp_pcm_probe,
	.remove = sirf_usp_pcm_remove,
};

module_platform_driver(sirf_usp_pcm_driver);

MODULE_DESCRIPTION("SiRF SoC USP PCM bus driver");
MODULE_AUTHOR("RongJun Ying <Rongjun.Ying@csr.com>");
MODULE_LICENSE("GPL v2");
