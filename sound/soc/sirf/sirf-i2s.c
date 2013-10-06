/*
 * SiRF I2S driver
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */
#include <linux/module.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/pwm.h>
#include <linux/reset.h>

#include <sound/soc.h>
#include <sound/pcm_params.h>

#include "sirf-pcm.h"
#include "sirf-audio.h"

struct sirf_i2s {
	void __iomem		*base;
	struct clk		*clk;
	struct pwm_device	*mclk_pwm;
	u32			i2s_ctrl;
	spinlock_t		lock;
	int			master_mode;
};

static struct sirf_pcm_dma_data sirf_i2s_dai_dma_data[2] = {
	{
		.name = "Audio Playback",
	}, {
		.name = "Audio Capture",
	}
};

static int sirf_i2s_startup(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	pm_runtime_get_sync(dai->dev);
	snd_soc_dai_set_dma_data(dai, substream,
		&sirf_i2s_dai_dma_data[substream->stream]);
	return 0;
}

static void sirf_i2s_shutdown(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	pm_runtime_put(dai->dev);
}

static int sirf_i2s_trigger(struct snd_pcm_substream *substream,
		int cmd, struct snd_soc_dai *dai)
{
	struct sirf_i2s *si2s = snd_soc_dai_get_drvdata(dai);
	int playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		spin_lock(&si2s->lock);

		if (playback) {
			/* First start the FIFO, then enable the tx/rx */
			writel(AUDIO_FIFO_RESET,
				si2s->base + AUDIO_CTRL_EXT_TXFIFO1_OP);
			writel(AUDIO_FIFO_START,
				si2s->base + AUDIO_CTRL_EXT_TXFIFO1_OP);

			writel(readl(si2s->base+AUDIO_CTRL_I2S_TX_RX_EN)
				| I2S_TX_ENABLE | I2S_DOUT_OE |
				(si2s->master_mode == 1 ? I2S_MCLK_EN : 0),
				si2s->base + AUDIO_CTRL_I2S_TX_RX_EN);

		} else {
			/* First start the FIFO, then enable the tx/rx */
			writel(AUDIO_FIFO_RESET,
				si2s->base + AUDIO_CTRL_RXFIFO_OP);
			writel(AUDIO_FIFO_START,
				si2s->base + AUDIO_CTRL_RXFIFO_OP);

			writel(readl(si2s->base+AUDIO_CTRL_I2S_TX_RX_EN)
				| I2S_RX_ENABLE |
				(si2s->master_mode == 1 ? I2S_MCLK_EN : 0),
				si2s->base + AUDIO_CTRL_I2S_TX_RX_EN);
		}

		spin_unlock(&si2s->lock);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		spin_lock(&si2s->lock);

		if (playback) {
			writel(readl(si2s->base + AUDIO_CTRL_I2S_TX_RX_EN)
				& ~(I2S_TX_ENABLE | I2S_MCLK_EN),
				si2s->base + AUDIO_CTRL_I2S_TX_RX_EN);
			/* First disable the tx/rx, then stop the FIFO */
			writel(0, si2s->base + AUDIO_CTRL_EXT_TXFIFO1_OP);
		} else {
			writel(readl(si2s->base + AUDIO_CTRL_I2S_TX_RX_EN)
				& ~(I2S_RX_ENABLE | I2S_MCLK_EN),
				si2s->base+AUDIO_CTRL_I2S_TX_RX_EN);

			/* First disable the tx/rx, then stop the FIFO */
			writel(0, si2s->base + AUDIO_CTRL_RXFIFO_OP);
		}

		spin_unlock(&si2s->lock);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int sirf_i2s_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct sirf_i2s *si2s = snd_soc_dai_get_drvdata(dai);
	u32 i2s_ctrl = readl(si2s->base + AUDIO_CTRL_I2S_CTRL);
	u32 left_len, frame_len;
	int channels = params_channels(params);

	/*
	 * SiRFSoC I2S controller only support 2 and 6 channells output.
	 * I2S_SIX_CHANNELS bit clear: select 2 channels mode.
	 * I2S_SIX_CHANNELS bit set: select 6 channels mode.
	 */
	switch (channels) {
	case 2:
		i2s_ctrl &= ~I2S_SIX_CHANNELS;
		break;
	case 6:
		i2s_ctrl |= I2S_SIX_CHANNELS;
		break;
	default:
		dev_err(dai->dev, "%d channels unsupported\n", channels);
		return -EINVAL;
	}

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
	i2s_ctrl &= ~(I2S_L_CHAN_LEN_MASK | I2S_FRAME_LEN_MASK);
	/* Fill the actual len - 1 */
	i2s_ctrl |= ((frame_len - 1) << 9) | ((left_len - 1) << 4)
		| (0 << 15) | (3 << 24);
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
		si2s->master_mode = 0;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		si2s->master_mode = 1;
		return -EINVAL;
	default:
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		writel(readl(si2s->base + AUDIO_CTRL_MODE_SEL)
			| I2S_MODE,
			si2s->base + AUDIO_CTRL_MODE_SEL);
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

#ifdef CONFIG_PM_RUNTIME
static int sirf_i2s_runtime_suspend(struct device *dev)
{
	struct sirf_i2s *si2s = dev_get_drvdata(dev);
	clk_disable_unprepare(si2s->clk);

	if (si2s->master_mode)
		pwm_disable(si2s->mclk_pwm);

	return 0;
}

static int sirf_i2s_runtime_resume(struct device *dev)
{
	struct sirf_i2s *si2s = dev_get_drvdata(dev);
	if (si2s->master_mode)
		pwm_enable(si2s->mclk_pwm);
	clk_prepare_enable(si2s->clk);
	device_reset(dev);
	return 0;
}
#endif

#ifdef CONFIG_PM_SLEEP
static int sirf_i2s_suspend(struct device *dev)
{
	struct sirf_i2s *si2s = dev_get_drvdata(dev);

	if (!pm_runtime_status_suspended(dev)) {
		si2s->i2s_ctrl = readl(si2s->base+AUDIO_CTRL_I2S_CTRL);
		sirf_i2s_runtime_suspend(dev);
	}
	return 0;
}

static int sirf_i2s_resume(struct device *dev)
{
	struct sirf_i2s *si2s = dev_get_drvdata(dev);
	if (!pm_runtime_status_suspended(dev)) {
		sirf_i2s_runtime_resume(dev);
		writel(readl(si2s->base+AUDIO_CTRL_MODE_SEL)
				| I2S_MODE,
				si2s->base+AUDIO_CTRL_MODE_SEL);
		writel(si2s->i2s_ctrl, si2s->base+AUDIO_CTRL_I2S_CTRL);
		writel(0, si2s->base + AUDIO_CTRL_EXT_TXFIFO1_INT_MSK);
		writel(0, si2s->base + AUDIO_CTRL_RXFIFO_INT_MSK);
	}

	return 0;
}
#endif

static const struct snd_soc_component_driver sirf_i2s_component = {
	.name       = "sirf-i2s",
};

static int sirf_i2s_probe(struct platform_device *pdev)
{
	struct sirf_i2s *si2s;
	u32 rx_dma_ch, tx_dma_ch;
	int ret;
	struct resource mem_res;

	si2s = devm_kzalloc(&pdev->dev, sizeof(struct sirf_i2s),
			GFP_KERNEL);
	if (!si2s)
		return -ENOMEM;

	platform_set_drvdata(pdev, si2s);

	spin_lock_init(&si2s->lock);

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

	ret = of_address_to_resource(pdev->dev.of_node, 0, &mem_res);
	if (ret < 0) {
		dev_err(&pdev->dev, "Unable to get i2s memory resource.\n");
		return ret;
	}
	si2s->base = devm_ioremap(&pdev->dev, mem_res.start,
		resource_size(&mem_res));
	if (!si2s->base)
		return -ENOMEM;

	si2s->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(si2s->clk)) {
		dev_err(&pdev->dev, "Get clock failed.\n");
		ret = PTR_ERR(si2s->clk);
		goto err;
	}

	/* i2s bus uses an internal PWM to generate MCLK */
	si2s->mclk_pwm = devm_pwm_get(&pdev->dev, NULL);
	if (IS_ERR(si2s->mclk_pwm)) {
		dev_err(&pdev->dev, "unable to request PWM\n");
		ret = PTR_ERR(si2s->mclk_pwm);
		goto err;
	}

	ret = snd_soc_register_component(&pdev->dev, &sirf_i2s_component,
			&sirf_i2s_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "Register Audio SoC dai failed.\n");
		goto err;
	}

	pm_runtime_enable(&pdev->dev);
	return 0;

err:
	return ret;
}

static int sirf_i2s_remove(struct platform_device *pdev)
{
	snd_soc_unregister_component(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	return 0;
}

static const struct of_device_id sirf_i2s_of_match[] = {
	{ .compatible = "sirf,prima2-i2s", },
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
MODULE_AUTHOR("RongJun Ying <Rongjun.Ying@csr.com>");
MODULE_LICENSE("GPL v2");
