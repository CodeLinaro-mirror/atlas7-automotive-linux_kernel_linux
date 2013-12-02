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
	void __iomem *base;
	struct clk *clk;
	u32 mode1_reg;
	u32 mode2_reg;
	struct platform_device *sirf_pcm_pdev;
};

static struct snd_dmaengine_dai_dma_data dma_data[2];

static void sirf_usp_tx_fifo_op(struct sirf_usp *susp, int cmd)
{
	switch (cmd) {
	case FIFO_RESET:
		writel(USP_TX_FIFO_RESET, susp->base + USP_TX_FIFO_OP);
		writel(0, susp->base + USP_TX_FIFO_OP);
		break;
	case FIFO_START:
		writel(USP_TX_FIFO_START, susp->base + USP_TX_FIFO_OP);
		break;
	case FIFO_STOP:
		writel(0, susp->base + USP_TX_FIFO_OP);
		break;
	}
}

static void sirf_usp_rx_fifo_op(struct sirf_usp *susp, int cmd)
{
	switch (cmd) {
	case FIFO_RESET:
		writel(USP_RX_FIFO_RESET, susp->base + USP_RX_FIFO_OP);
		writel(0, susp->base + USP_RX_FIFO_OP);
		break;
	case FIFO_START:
		writel(USP_RX_FIFO_START, susp->base + USP_RX_FIFO_OP);
		break;
	case FIFO_STOP:
		writel(0, susp->base + USP_RX_FIFO_OP);
		break;
	}
}

static inline void sirf_usp_tx_enable(struct sirf_usp *susp)
{
	writel(readl(susp->base + USP_TX_RX_ENABLE) | USP_TX_ENA,
			susp->base + USP_TX_RX_ENABLE);
}

static inline void sirf_usp_tx_disable(struct sirf_usp *susp)
{
	writel(readl(susp->base + USP_TX_RX_ENABLE) & ~USP_TX_ENA,
			susp->base + USP_TX_RX_ENABLE);
}

static inline void sirf_usp_rx_enable(struct sirf_usp *susp)
{
	writel(readl(susp->base + USP_TX_RX_ENABLE) | USP_RX_ENA,
			susp->base + USP_TX_RX_ENABLE);
}

static inline void sirf_usp_rx_disable(struct sirf_usp *susp)
{
	writel(readl(susp->base + USP_TX_RX_ENABLE) & ~USP_RX_ENA,
			susp->base + USP_TX_RX_ENABLE);
}

static int sirf_usp_pcm_dai_probe(struct snd_soc_dai *dai)
{
	dai->playback_dma_data = &dma_data[0];
	dai->capture_dma_data = &dma_data[1];
	return 0;
}

static int sirf_usp_pcm_set_dai_fmt(struct snd_soc_dai *dai,
		unsigned int fmt)
{
	struct sirf_usp *susp = snd_soc_dai_get_drvdata(dai);
	u32 val = readl(susp->base + USP_MODE2);
	u32 val1 = readl(susp->base + USP_MODE1);

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		val1 &= ~USP_CLOCK_MODE_SLAVE;
		val &= ~USP_TFS_CLK_SLAVE_MODE;
		val &= ~USP_RFS_CLK_SLAVE_MODE;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		val1 |= USP_CLOCK_MODE_SLAVE;
		val |= USP_TFS_CLK_SLAVE_MODE;
		val |= USP_RFS_CLK_SLAVE_MODE;
		break;
	default:
		return -EINVAL;
	}
	writel(val1, susp->base + USP_MODE1);
	writel(val, susp->base + USP_MODE2);

	return 0;
}

static int sirf_usp_pcm_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	int playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	struct sirf_usp *susp = snd_soc_dai_get_drvdata(dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (playback) {
			sirf_usp_tx_fifo_op(susp, FIFO_RESET);
			sirf_usp_tx_fifo_op(susp, FIFO_START);
			sirf_usp_tx_enable(susp);
		} else {
			sirf_usp_rx_fifo_op(susp, FIFO_RESET);
			sirf_usp_rx_fifo_op(susp, FIFO_START);
			sirf_usp_rx_enable(susp);
		}
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (playback) {
			sirf_usp_tx_disable(susp);
			sirf_usp_tx_fifo_op(susp, FIFO_STOP);
		} else {
			sirf_usp_rx_disable(susp);
			sirf_usp_rx_fifo_op(susp, FIFO_STOP);
		}
		break;
	}

	return 0;
}

static int sirf_usp_pcm_divider(struct snd_soc_dai *dai, int div_id, int rate)
{
	struct sirf_usp *susp = snd_soc_dai_get_drvdata(dai);
	u32 clk_rate, clk_div, clk_div_hi, clk_div_lo;

	if (div_id != SIRF_USP_DIV_MCLK)
		return -EINVAL;

	clk_rate = clk_get_rate(susp->clk);
	if (clk_rate < rate * 2) {
		dev_err(dai->dev, "Can't get rate(%d) by need.\n", rate);
		return -EINVAL;
	}

	clk_div = (clk_rate / (2 * rate)) - 1;
	clk_div_hi = (clk_div & 0xC00) >> 10;
	clk_div_lo = (clk_div & 0x3FF);

	writel((clk_div_lo << 21) | readl(susp->base + USP_MODE2),
		susp->base + USP_MODE2);
	writel((clk_div_hi << 30) | readl(susp->base + USP_TX_FRAME_CTRL),
		susp->base + USP_TX_FRAME_CTRL);

	return 0;
}

static const struct snd_soc_dai_ops sirf_usp_pcm_dai_ops = {
	.trigger = sirf_usp_pcm_trigger,
	.set_fmt = sirf_usp_pcm_set_dai_fmt,
	.set_clkdiv = sirf_usp_pcm_divider,
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

static void sirf_usp_controller_init(struct sirf_usp *susp)
{
	u32 val;

	/* Configure RISC mode */
	writel(readl(susp->base + USP_RISC_DSP_MODE) & ~USP_RISC_DSP_SEL,
		susp->base + USP_RISC_DSP_MODE);

	/* Disable all interrupts status */
	writel(readl(susp->base + USP_INT_STATUS), susp->base + USP_INT_STATUS);

	/* Configure DMA IO Length register */
	writel(0, susp->base + USP_TX_DMA_IO_LEN);
	writel(0, susp->base + USP_RX_DMA_IO_LEN);

	/* Configure RX Frame Control */
	val = (AUDIO_WORD_SIZE * 2 - 1) << USP_RXC_DATA_LEN_OFFSET;
	val |= (AUDIO_WORD_SIZE * 2 - 1) << USP_RXC_FRAME_LEN_OFFSET;
	val |= (AUDIO_WORD_SIZE * 2 - 1) << USP_RXC_SHIFTER_LEN_OFFSET;
	val |= USP_SINGLE_SYNC_MODE;
	writel(val, susp->base + USP_RX_FRAME_CTRL);

	/* Configure TX Frame Control */
	val = (AUDIO_WORD_SIZE * 2 - 1) << USP_TXC_DATA_LEN_OFFSET;
	val |= 0 << USP_TXC_SYNC_LEN_OFFSET;
	val |= (AUDIO_WORD_SIZE * 2 - 1) << USP_TXC_FRAME_LEN_OFFSET;
	val |= (AUDIO_WORD_SIZE * 2 - 1) << USP_TXC_SHIFTER_LEN_OFFSET;
	val |= USP_TXC_SLAVE_CLK_SAMPLE;
	writel(val, susp->base + USP_TX_FRAME_CTRL);

	/* Configure Mode2 register */
	val = (1 << USP_RXD_DELAY_LEN_OFFSET) | (0 << USP_TXD_DELAY_LEN_OFFSET);
	val &= ~USP_ENA_CTRL_MODE;
	val &= ~USP_FRAME_CTRL_MODE;
	val &= ~USP_TFS_SOURCE_MODE;
	writel(val, susp->base + USP_MODE2);

	/* Configure Mode1 register */
	val = 0;
	val |= USP_SYNC_MODE;
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
	writel(val, susp->base + USP_MODE1);

	/* Configure RX DMA IO Control register */
	writel(0x0, susp->base + USP_RX_DMA_IO_CTRL);

	/* Congiure RX FIFO Control register */
	writel((USP_RX_FIFO_THRESHOLD << USP_RX_FIFO_THD_OFFSET) |
		(USP_TX_RX_FIFO_WIDTH_DWORD << USP_RX_FIFO_WIDTH_OFFSET),
		susp->base + USP_RX_FIFO_CTRL);

	/* Congiure RX FIFO Level Check register */
	writel(RX_FIFO_SC(0x04) | RX_FIFO_LC(0x0E) | RX_FIFO_HC(0x1B),
		susp->base + USP_RX_FIFO_LEVEL_CHK);

	/* Configure TX DMA IO Control register*/
	writel(0x0, susp->base + USP_TX_DMA_IO_CTRL);

	/* Configure TX FIFO Control register */
	writel((USP_TX_FIFO_THRESHOLD << USP_TX_FIFO_THD_OFFSET) |
		(USP_TX_RX_FIFO_WIDTH_DWORD << USP_TX_FIFO_WIDTH_OFFSET),
		susp->base + USP_TX_FIFO_CTRL);

	/* Congiure TX FIFO Level Check register */
	writel(TX_FIFO_SC(0x1B) | TX_FIFO_LC(0x0E) | TX_FIFO_HC(0x04),
		susp->base + USP_TX_FIFO_LEVEL_CHK);

	/* Configure RX FIFO */
	writel(USP_RX_FIFO_RESET, susp->base + USP_RX_FIFO_OP);
	writel(0, susp->base + USP_RX_FIFO_OP);

	/* Configure TX FIFO */
	writel(USP_TX_FIFO_RESET, susp->base + USP_TX_FIFO_OP);
	writel(0, susp->base + USP_TX_FIFO_OP);
}

static void sirf_usp_controller_uninit(struct sirf_usp *susp)
{
	/* Disable RX/TX */
	writel(0, susp->base + USP_INT_ENABLE);
	writel(0, susp->base + USP_TX_RX_ENABLE);
}

#ifdef CONFIG_PM_RUNTIME
static int sirf_usp_pcm_runtime_suspend(struct device *dev)
{
	struct sirf_usp *susp = dev_get_drvdata(dev);
	sirf_usp_controller_uninit(susp);
	clk_disable_unprepare(susp->clk);
	return 0;
}

static int sirf_usp_pcm_runtime_resume(struct device *dev)
{
	struct sirf_usp *susp = dev_get_drvdata(dev);
	clk_prepare_enable(susp->clk);
	sirf_usp_controller_init(susp);
	return 0;
}
#endif

#ifdef CONFIG_PM_SLEEP
static int sirf_usp_pcm_suspend(struct device *dev)
{
	struct sirf_usp *susp = dev_get_drvdata(dev);

	if (!pm_runtime_status_suspended(dev)) {
		susp->mode1_reg = readl(susp->base + USP_MODE1);
		susp->mode2_reg = readl(susp->base + USP_MODE2);
		sirf_usp_pcm_runtime_suspend(dev);
	}
	return 0;
}

static int sirf_usp_pcm_resume(struct device *dev)
{
	struct sirf_usp *susp = dev_get_drvdata(dev);

	if (!pm_runtime_status_suspended(dev)) {
		sirf_usp_pcm_runtime_resume(dev);
		writel(susp->mode1_reg, susp->base + USP_MODE1);
		writel(susp->mode2_reg, susp->base + USP_MODE2);
	}
	return 0;
}
#endif

static const struct snd_soc_component_driver sirf_usp_component = {
	.name		= "sirf-usp",
};

static int sirf_usp_pcm_probe(struct platform_device *pdev)
{
	struct sirf_usp *susp;
	u32 rx_dma_ch, tx_dma_ch;
	int ret;
	struct resource *mem_res;

	susp = devm_kzalloc(&pdev->dev, sizeof(struct sirf_usp),
			GFP_KERNEL);
	if (!susp)
		return -ENOMEM;

	susp->sirf_pcm_pdev = platform_device_register_simple("sirf-pcm-audio",
			2, NULL, 0);
	if (IS_ERR(susp->sirf_pcm_pdev))
		return PTR_ERR(susp->sirf_pcm_pdev);

	platform_set_drvdata(pdev, susp);

	ret = of_property_read_u32(pdev->dev.of_node,
			"sirf,usp-dma-rx-channel", &rx_dma_ch);
	if (ret < 0) {
		dev_err(&pdev->dev, "Unable to USP0 rx dma channel\n");
		return ret;
	}
	ret = of_property_read_u32(pdev->dev.of_node,
			"sirf,usp-dma-tx-channel", &tx_dma_ch);
	if (ret < 0) {
		dev_err(&pdev->dev, "Unable to USP0 tx dma channel\n");
		return ret;
	}

	dma_data[0].filter_data = (void *)tx_dma_ch;
	dma_data[1].filter_data = (void *)rx_dma_ch;

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	susp->base = devm_ioremap_resource(&pdev->dev, mem_res);
	if (susp->base == NULL)
		return -ENOMEM;

	susp->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(susp->clk)) {
		dev_err(&pdev->dev, "Get clock failed.\n");
		return PTR_ERR(susp->clk);
	}
	clk_prepare_enable(susp->clk);

	ret = devm_snd_soc_register_component(&pdev->dev, &sirf_usp_component,
		&sirf_usp_pcm_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "Register Audio SoC dai failed.\n");
		goto err;
	}
	pm_runtime_enable(&pdev->dev);
	return 0;

err:
	return ret;
}

static int sirf_usp_pcm_remove(struct platform_device *pdev)
{
	struct sirf_usp *susp = platform_get_drvdata(pdev);
	pm_runtime_disable(&pdev->dev);
	platform_device_unregister(susp->sirf_pcm_pdev);

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
