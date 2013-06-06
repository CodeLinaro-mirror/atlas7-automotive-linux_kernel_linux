/*
 * SiRF I2S driver
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/pwm.h>
#include <linux/delay.h>
#include <linux/reset.h>

#include <sound/soc.h>
#include <sound/pcm_params.h>

#include "sirf-pcm.h"
#include "sirf-audio.h"

struct sirf_i2s {
	void __iomem        *base;
	struct clk          *clk;
	struct pwm_device   *mclk_pwm;
	u32                 i2s_ctrl;
};

static struct sirf_pcm_dma_data sirf_i2s_dai_dma_data[2] = {
	{
		.name = "Audio Playback",
	},{
		.name = "Audio Capture",
	}
};

static int sirf_i2s_startup(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{

	struct sirf_i2s *si2s = snd_soc_dai_get_drvdata(dai);
	pwm_enable(si2s->mclk_pwm);
	return 0;
}

static void sirf_i2s_shutdown(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct sirf_i2s *si2s = snd_soc_dai_get_drvdata(dai);
	pwm_disable(si2s->mclk_pwm);
}

static int sirf_i2s_trigger(struct snd_pcm_substream *substream,
		int cmd, struct snd_soc_dai *dai)
{
	struct sirf_i2s *si2s = snd_soc_dai_get_drvdata(dai);
	int playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);
	unsigned long irqs;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		local_irq_save(irqs);

		if (playback) {
			/* First start the FIFO, then enable the tx/rx */
			writel(AUDIO_FIFO_START,
				si2s->base+AUDIO_CTRL_EXT_TXFIFO1_OP);
			mdelay(1);

			writel(readl(si2s->base+AUDIO_CTRL_I2S_TX_RX_EN)
				| I2S_TX_ENABLE | I2S_DOUT_OE | I2S_MCLK_EN,
				si2s->base+AUDIO_CTRL_I2S_TX_RX_EN);

		} else {
			/* First start the FIFO, then enable the tx/rx */
			writel(AUDIO_FIFO_START,
				si2s->base+AUDIO_CTRL_RXFIFO_OP);
			mdelay(1);

			writel(readl(si2s->base+AUDIO_CTRL_I2S_TX_RX_EN)
				| I2S_RX_ENABLE | I2S_MCLK_EN,
				si2s->base+AUDIO_CTRL_I2S_TX_RX_EN);
		}

		local_irq_restore(irqs);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		local_irq_save(irqs);

		if (playback) {
			writel(readl(si2s->base+AUDIO_CTRL_I2S_TX_RX_EN)
				& ~(I2S_TX_ENABLE | I2S_MCLK_EN),
				si2s->base + AUDIO_CTRL_I2S_TX_RX_EN);
			/* First disable the tx/rx, then stop the FIFO */
			writel(0, si2s->base+AUDIO_CTRL_EXT_TXFIFO1_OP);
		} else {
			writel(readl(si2s->base+AUDIO_CTRL_I2S_TX_RX_EN)
				& ~(I2S_RX_ENABLE | I2S_MCLK_EN),
				si2s->base+AUDIO_CTRL_I2S_TX_RX_EN);

			/* First disable the tx/rx, then stop the FIFO */
			writel(0, si2s->base+AUDIO_CTRL_RXFIFO_OP);
		}

		local_irq_restore(irqs);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int sirf_i2s_prepare(struct snd_pcm_substream *substream,
			struct snd_soc_dai *dai)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct sirf_i2s *si2s = snd_soc_dai_get_drvdata(dai);
	u32 ctrl = readl(si2s->base+AUDIO_CTRL_I2S_CTRL);

	/* NOTE: It must not be a case of 2 channel input and 6 channel output.
	 * Both the directions must be configured for same number of channels.
	 * Anyways, in case of TSC2100 codec, which supports only 2 channels,
	 * it will not be the case of different channel numbers in
	 * different directions. */
	if (runtime->channels == 2)
		ctrl &= ~I2S_SIX_CHANNELS;	/* 2 channels */
	else
		ctrl |= I2S_SIX_CHANNELS;	/* 6 channels */

	writel(ctrl, si2s->base+AUDIO_CTRL_I2S_CTRL);

	return 0;
}

static int sirf_i2s_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct sirf_i2s *si2s = snd_soc_dai_get_drvdata(dai);
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	u32 i2s_ctrl = readl(si2s->base + AUDIO_CTRL_I2S_CTRL);
	u32 left_len, frame_len;

	snd_soc_dai_set_dma_data(rtd->cpu_dai, substream,
		&sirf_i2s_dai_dma_data);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		left_len = 8;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		left_len = 16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		left_len = 24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		left_len = 32;
		break;
	default:
		dev_err(dai->dev, "Format unsupported\n");
		return -EINVAL;
	}

	frame_len = left_len * 2;
	i2s_ctrl &= (~(I2S_L_CHAN_LEN_MASK | I2S_FRAME_LEN_MASK));
	/* Fill the actual len - 1 */
	i2s_ctrl |= ((frame_len - 1)<<9) | ((left_len - 1)<<4)
		| (0<<15) | (3<<24);
	writel(i2s_ctrl, si2s->base + AUDIO_CTRL_I2S_CTRL);
	return 0;
}

static int sirf_i2s_set_dai_fmt(struct snd_soc_dai *dai,
		unsigned int fmt)
{
	struct sirf_i2s *si2s = snd_soc_dai_get_drvdata(dai);
	u32 ctrl;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		ctrl = readl(si2s->base + AUDIO_CTRL_I2S_CTRL);
		ctrl |= I2S_SLAVE_MODE;
		writel(ctrl, si2s->base + AUDIO_CTRL_I2S_CTRL);
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		return -EINVAL;
	default:
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;
	default:
		dev_err(dai->dev, "Only I2S format supported\n");
		return -EINVAL;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	default:
		dev_err(dai->dev, " Only normal bit clock, normal frame clock supported\n");
		return -EINVAL;
	}

	return 0;
}

struct snd_soc_dai_ops sirfsoc_i2s_dai_ops = {
	.startup	= sirf_i2s_startup,
	.shutdown	= sirf_i2s_shutdown,
	.trigger	= sirf_i2s_trigger,
	.prepare	= sirf_i2s_prepare,
	.hw_params	= sirf_i2s_hw_params,
	.set_fmt	= sirf_i2s_set_dai_fmt,
};

static struct snd_soc_dai_driver sirf_i2s_dai = {
	.name		= "sirf-i2s",
	.id			= 0,
	.playback = {
		.stream_name = "SiRF I2S Playback",
		.channels_min = 2,
		.channels_max = 6,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = SNDRV_PCM_FMTBIT_S8 |
			SNDRV_PCM_FMTBIT_S16_LE |
			SNDRV_PCM_FMTBIT_S24_LE |
			SNDRV_PCM_FMTBIT_S32_LE,
	},
	.capture = {
		.stream_name = "SiRF I2S Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = SNDRV_PCM_FMTBIT_S8 |
			SNDRV_PCM_FMTBIT_S16_LE |
			SNDRV_PCM_FMTBIT_S24_LE |
			SNDRV_PCM_FMTBIT_S32_LE,
	},
	.ops = &sirfsoc_i2s_dai_ops,
};

#ifdef CONFIG_PM
static int sirf_i2s_suspend(struct platform_device *pdev,
		pm_message_t state)
{
	struct sirf_i2s *si2s = platform_get_drvdata(pdev);

	pwm_enable(si2s->mclk_pwm);

	si2s->i2s_ctrl = readl(si2s->base+AUDIO_CTRL_I2S_CTRL);

	clk_disable_unprepare(si2s->clk);

	return 0;
}

static int sirf_i2s_resume(struct platform_device *pdev)
{
	struct sirf_i2s *si2s = platform_get_drvdata(pdev);
	clk_prepare_enable(si2s->clk);

	device_reset(&pdev->dev);
	writel(readl(si2s->base+AUDIO_CTRL_MODE_SEL)
			| I2S_MODE,
			si2s->base+AUDIO_CTRL_MODE_SEL);
	writel(si2s->i2s_ctrl, si2s->base+AUDIO_CTRL_I2S_CTRL);
	writel(0, si2s->base + AUDIO_CTRL_EXT_TXFIFO1_INT_MSK);
	writel(0, si2s->base + AUDIO_CTRL_RXFIFO_INT_MSK);

	pwm_disable(si2s->mclk_pwm);

	return 0;
}
#else
#define sirf_usp_pcm_suspend NULL
#define sirf_usp_pcm_resume NULL
#endif

static const struct snd_soc_component_driver sirf_i2s_component = {
	.name       = "sirf-i2s",
};

static int sirf_i2s_probe(struct platform_device *pdev)
{
	struct sirf_i2s *si2s;
	u32 rx_dma_ch, tx_dma_ch;
	int ret;
	struct resource *mem_res;

	si2s = devm_kzalloc(&pdev->dev, sizeof(struct sirf_i2s),
			GFP_KERNEL);
	if (si2s == NULL)
		return -ENOMEM;
	platform_set_drvdata(pdev, si2s);
	ret = of_property_read_u32(pdev->dev.of_node,
			"sirf,i2s-dma-rx-channel", &rx_dma_ch);
	if (ret < 0) {
		dev_err(&pdev->dev, "Unable to USP0 rx dma channel\n");
		return ret;
	}
	ret = of_property_read_u32(pdev->dev.of_node,
			"sirf,i2s-dma-tx-channel", &tx_dma_ch);
	if (ret < 0) {
		dev_err(&pdev->dev, "Unable to USP0 tx dma channel\n");
		return ret;
	}

	sirf_i2s_dai_dma_data[0].dma_req = tx_dma_ch;
	sirf_i2s_dai_dma_data[1].dma_req = rx_dma_ch;

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	si2s->base = devm_ioremap(mem_res->start, mem_res->end - mem_res->start + 1);
	if (!si2s->base)
		return -ENOMEM;

	si2s->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(si2s->clk)) {
		dev_err(&pdev->dev, "Get clock failed.\n");
		ret = PTR_ERR(si2s->clk);
		goto err;
	}
	clk_prepare_enable(si2s->clk);

	device_reset(&pdev->dev);

	/* i2s bus uses PWM to generate MCLK */
	si2s->mclk_pwm = devm_pwm_get(&pdev->dev, NULL);
	if (IS_ERR(si2s->mclk_pwm)) {
		dev_err(&pdev->dev, "unable to request PWM\n");
		ret = PTR_ERR(si2s->mclk_pwm);
		goto err_clk_put;
	}

	writel(readl(si2s->base+AUDIO_CTRL_MODE_SEL)
			| I2S_MODE,
			si2s->base+AUDIO_CTRL_MODE_SEL);

	ret = snd_soc_register_component(&pdev->dev, &sirf_i2s_component,
			&sirf_i2s_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "Register Audio SoC dai failed.\n");
		goto err_clk_put;
	}

	return 0;

err_clk_put:
	clk_disable_unprepare(si2s->clk);
err:
	return ret;
}

static int sirf_i2s_remove(struct platform_device *pdev)
{
	struct sirf_i2s *si2s = platform_get_drvdata(pdev);

	pwm_disable(si2s->mclk_pwm);
	snd_soc_unregister_component(&pdev->dev);
	clk_disable_unprepare(si2s->clk);

	return 0;
}

static const struct of_device_id sirf_i2s_of_match[] = {
	{ .compatible = "sirf,prima2-i2s", },
	{}
};
MODULE_DEVICE_TABLE(of, sirf_i2s_of_match);

static struct platform_driver sirf_i2s_driver = {
	.driver = {
		.name = "sirf-i2s",
		.owner = THIS_MODULE,
		.of_match_table = sirf_i2s_of_match,
	},
	.probe = sirf_i2s_probe,
	.remove = sirf_i2s_remove,
	.suspend = sirf_i2s_suspend,
	.resume = sirf_i2s_resume,
};

module_platform_driver(sirf_i2s_driver);

MODULE_DESCRIPTION("SiRF SoC I2S driver");
MODULE_AUTHOR("RongJun Ying <Rongjun.Ying@csr.com>");
MODULE_LICENSE("GPL v2");
