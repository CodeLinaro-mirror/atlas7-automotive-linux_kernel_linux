/*
 * SiRF inner audio codec driver
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.*
 */

#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rtc/sirfsoc_rtciobrg.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>

#include "sirf-audio.h"
#include "sirf-pcm.h"

struct sirf_soc_inner_audio_reg_bits {
	u32 dig_mic_en_bits;
	u32 dig_mic_freq_bits;
	u32 adc14b_12_bits;
	u32 firdac_hsl_en_bits;
	u32 firdac_hsr_en_bits;
	u32 firdac_lout_en_bits;
	u32 por_bits;
	u32 codec_clk_en_bits;
	u32 hp_3db_boost_bits;
	u32 adc_left_gain_shift;
	u32 adc_right_gain_shift;
	u32 adc_gain_mask;
	u32 mic_max_gain;
};

struct sirf_soc_inner_audio {
	void __iomem            *base;
	unsigned int            irq;
	unsigned int            playing;
	struct clk              *clk;
	spinlock_t              lock;
	u32			sys_pwrc_reg_base;
	struct sirf_soc_inner_audio_reg_bits *reg_bits;
};

static struct sirf_soc_inner_audio_reg_bits sirf_soc_inner_audio_reg_bits_prima2 = {
	20, 21, 22, 23, 24, 25, 26, 27, 28, 15, 10, 0x1f, 0x19,
};

static struct sirf_soc_inner_audio_reg_bits sirf_soc_inner_audio_reg_bits_atlas6 = {
	22, 23, 24, 25, 26, 27, 28, 29, 30, 16, 10, 0x3f, 0x39,
};

static int sirf_inner_control(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol,
		int get, char *name)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct snd_soc_card *card = codec->card;
	int i;
	for (i = 0; i < card->num_controls; i++) {
		if (!strcmp(card->controls[i].name, name)) {
			if (card->controls[i].get && get)
				return card->controls[i].get(kcontrol, ucontrol);
			else if (card->controls[i].put && !get)
				return card->controls[i].put(kcontrol, ucontrol);
		}
	}
	return 0;
}

static int sirf_inner_snd_speaker_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	sirf_inner_control(kcontrol, ucontrol, 1, "Speaker Out");
	return 0;
}

static int sirf_inner_snd_speaker_set(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sirf_soc_inner_audio *sinner_audio = dev_get_drvdata(codec->dev);
	unsigned long flags;

	spin_lock_irqsave(&sinner_audio->lock, flags);
	sirf_inner_control(kcontrol, ucontrol, 0, "Speaker Out");

	if (ucontrol->value.integer.value[0]) {
		writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0)
				| IC_RDACEN | IC_SPSELR,
				sinner_audio->base + AUDIO_IC_CODEC_CTRL0);

		writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL1) |
				(1 << sinner_audio->reg_bits->firdac_lout_en_bits),
				sinner_audio->base + AUDIO_IC_CODEC_CTRL1);

		writel((readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0) |
					IC_SPEN), sinner_audio->base + AUDIO_IC_CODEC_CTRL0);
	} else {
		writel((readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0)
					& ~(IC_SPEN | IC_SPSELR)),
				sinner_audio->base + AUDIO_IC_CODEC_CTRL0);
		writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL1)
				& ~(1 << sinner_audio->reg_bits->firdac_lout_en_bits),
				sinner_audio->base + AUDIO_IC_CODEC_CTRL1);
	}

	spin_unlock_irqrestore(&sinner_audio->lock, flags);

	return 0;
}

static int sirf_inner_snd_headphone_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	sirf_inner_control(kcontrol, ucontrol, 1, "Headphone Out");
	return 0;
}

static int sirf_inner_snd_headphone_set(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sirf_soc_inner_audio *sinner_audio = dev_get_drvdata(codec->dev);
	unsigned long flags;

	spin_lock_irqsave(&sinner_audio->lock, flags);
	sirf_inner_control(kcontrol, ucontrol, 0, "Headphone Out");
	if (ucontrol->value.integer.value[0])
		writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0)
				| IC_HSLEN | IC_HSREN | IC_HPRSELR
				| IC_HPLSELL,
				sinner_audio->base + AUDIO_IC_CODEC_CTRL0);
	else
		writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0)
				& ~(IC_HSLEN | IC_HSREN | IC_HPRSELR
					| IC_HPLSELL),
				sinner_audio->base + AUDIO_IC_CODEC_CTRL0);
	spin_unlock_irqrestore(&sinner_audio->lock, flags);
	return 0;
}

static struct snd_kcontrol_new snd_sirf_inner_volume_controls_atlas6[] = {
	SOC_DOUBLE("Speaker Volume", AUDIO_IC_CODEC_CTRL0, 21, 14,
			0x7F, 0),
	SOC_DOUBLE("Capture Volume", AUDIO_IC_CODEC_CTRL1, 16, 10,
			0x3F, 0),
	SOC_DOUBLE("Capture Switch", AUDIO_IC_CODEC_CTRL1, 0, 1,
			1, 0),
	SOC_SINGLE_BOOL_EXT("Speaker Switch", 0, sirf_inner_snd_speaker_get,
			sirf_inner_snd_speaker_set),
	SOC_SINGLE_BOOL_EXT("Headphone Switch", 0, sirf_inner_snd_headphone_get,
			sirf_inner_snd_headphone_set),
};

static struct snd_kcontrol_new snd_sirf_inner_volume_controls_prima2[] = {
	SOC_DOUBLE("Speaker Volume", AUDIO_IC_CODEC_CTRL0, 21, 14,
			0x7F, 0),
	SOC_DOUBLE("Capture Volume", AUDIO_IC_CODEC_CTRL1, 15, 10,
			0x1F, 0),
	SOC_DOUBLE("Capture Switch", AUDIO_IC_CODEC_CTRL1, 0, 1,
			1, 0),
	SOC_SINGLE_BOOL_EXT("Speaker Switch", 0, sirf_inner_snd_speaker_get,
			sirf_inner_snd_speaker_set),
	SOC_SINGLE_BOOL_EXT("Headphone Switch", 0, sirf_inner_snd_headphone_get,
			sirf_inner_snd_headphone_set),
};

static int sirf_inner_codec_startup(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct sirf_soc_inner_audio *sinner_audio = snd_soc_dai_get_drvdata(dai);
	u32 adc_gain_mask = sinner_audio->reg_bits->adc_gain_mask;
	u32 adc_left_gain_shift = sinner_audio->reg_bits->adc_left_gain_shift;
	u32 adc_right_gain_shift = sinner_audio->reg_bits->adc_right_gain_shift;
	u32 mic_max_gain = sinner_audio->reg_bits->mic_max_gain;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		writel((readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL1)
					| (1 << sinner_audio->reg_bits->codec_clk_en_bits) |
					(1 << sinner_audio->reg_bits->por_bits)),
				sinner_audio->base + AUDIO_IC_CODEC_CTRL1);

		writel((readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0) | IC_HSINVEN)
				& ~IC_MONOR,
				sinner_audio->base + AUDIO_IC_CODEC_CTRL0);

		msleep(50);

		writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0) | IC_RDACEN |
				IC_LDACEN | IC_HPRSELR | IC_HPLSELL,
				sinner_audio->base + AUDIO_IC_CODEC_CTRL0);

		writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL1) |
					(1 << sinner_audio->reg_bits->firdac_hsl_en_bits) |
					(1 << sinner_audio->reg_bits->firdac_hsr_en_bits),
				sinner_audio->base + AUDIO_IC_CODEC_CTRL1);

		usleep_range(300, 1000);

		writel((readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0)
					| IC_HSREN | IC_HSLEN),
				sinner_audio->base + AUDIO_IC_CODEC_CTRL0);

		/* avoid break noise when sound loud, set RX gain -1dB */
		writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0) &
				~((IC_RXPGAR_MASK << IC_RXPGAR_SHIFT) |
					(IC_RXPGAL_MASK << IC_RXPGAL_SHIFT)),
				sinner_audio->base + AUDIO_IC_CODEC_CTRL0);

		writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0) |
				((IC_RXPGAR << IC_RXPGAR_SHIFT) |
				 (IC_RXPGAL << IC_RXPGAL_SHIFT)),
				sinner_audio->base + AUDIO_IC_CODEC_CTRL0);
	} else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		sirfsoc_rtc_iobrg_writel(sirfsoc_rtc_iobrg_readl(
			(sinner_audio->sys_pwrc_reg_base +
			PWRC_PDN_CTRL_OFFSET))
			| (1 << AUDIO_POWER_EN_BIT),
			sinner_audio->sys_pwrc_reg_base +
			PWRC_PDN_CTRL_OFFSET);

		writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL1) |
			(1 << sinner_audio->reg_bits->codec_clk_en_bits) |
			(1 << sinner_audio->reg_bits->por_bits),
			sinner_audio->base + AUDIO_IC_CODEC_CTRL1);
		msleep(50);

		writel(readl(sinner_audio->base + AUDIO_IC_CODEC_PWR) |
			MICBIASEN, sinner_audio->base + AUDIO_IC_CODEC_PWR);
		usleep_range(300, 1000);

		writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL1)
			| IC_MICINREN | IC_MICINLEN,
			sinner_audio->base + AUDIO_IC_CODEC_CTRL1);
		usleep_range(100, 200);
		writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL1) | IC_RADCEN |
			IC_LADCEN, sinner_audio->base + AUDIO_IC_CODEC_CTRL1);
		usleep_range(100, 200);
		writel((readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL1) | IC_MICIN1SEL
			| IC_MICDIFSEL) & (~IC_MICIN2SEL),
			sinner_audio->base + AUDIO_IC_CODEC_CTRL1);

		writel((readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL1) &
			~(adc_gain_mask << adc_left_gain_shift) &
			~(adc_gain_mask << adc_right_gain_shift)) |
			(mic_max_gain << adc_left_gain_shift) |
			(mic_max_gain << adc_right_gain_shift),
			sinner_audio->base + AUDIO_IC_CODEC_CTRL1);
	}
	return 0;
}

static void sirf_inner_codec_shutdown(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
}


static int sirf_inner_codec_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
	return 0;
}

static int sirf_inner_codec_trigger(struct snd_pcm_substream *substream,
		int cmd,
		struct snd_soc_dai *dai)
{
	struct sirf_soc_inner_audio *sinner_audio = snd_soc_dai_get_drvdata(dai);
	int playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	unsigned long irqs;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		local_irq_save(irqs);
		if (playback) {
			spin_lock(&sinner_audio->lock);
			writel(readl(sinner_audio->base + AUDIO_CTRL_IC_CODEC_TX_CTRL)
				& ~IC_TX_ENABLE,
				sinner_audio->base + AUDIO_CTRL_IC_CODEC_TX_CTRL);
			sinner_audio->playing = false;
			writel((readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0)
				& ~(IC_SPEN | IC_SPSELR | IC_HSLEN)),
				sinner_audio->base + AUDIO_IC_CODEC_CTRL0);
			writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL1)
				& ~(1 << sinner_audio->reg_bits->firdac_lout_en_bits),
				sinner_audio->base + AUDIO_IC_CODEC_CTRL1);
			writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0)
				& ~(IC_HSLEN | IC_HSREN | IC_HPRSELR | IC_HPLSELL),
				sinner_audio->base + AUDIO_IC_CODEC_CTRL0);
			spin_unlock(&sinner_audio->lock);
		} else {
			writel(readl(sinner_audio->base + AUDIO_CTRL_IC_CODEC_RX_CTRL)
				& ~IC_RX_ENABLE,
				sinner_audio->base + AUDIO_CTRL_IC_CODEC_RX_CTRL);
		}
		local_irq_restore(irqs);
		break;
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		local_irq_save(irqs);
		if (playback) {
			spin_lock(&sinner_audio->lock);
			writel(0, sinner_audio->base + AUDIO_CTRL_IC_TXFIFO_INT_MSK);
			writel(AUDIO_FIFO_START,
				sinner_audio->base + AUDIO_CTRL_IC_TXFIFO_OP);
			writel(IC_TX_ENABLE,
				sinner_audio->base + AUDIO_CTRL_IC_CODEC_TX_CTRL);
			sinner_audio->playing = true;
			writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0)
				| IC_RDACEN | IC_SPSELR,
				sinner_audio->base + AUDIO_IC_CODEC_CTRL0);

			writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL1)
				| (1 << sinner_audio->reg_bits->firdac_lout_en_bits),
				sinner_audio->base + AUDIO_IC_CODEC_CTRL1);

			writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0)
				| IC_SPEN, sinner_audio->base + AUDIO_IC_CODEC_CTRL0);
			writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0)
				| IC_HSLEN | IC_HSREN
				| IC_HPRSELR | IC_HPLSELL,
				sinner_audio->base
				+ AUDIO_IC_CODEC_CTRL0);
			spin_unlock(&sinner_audio->lock);
		} else {
			/* unmask rx fifo interrupt */
			writel(0, sinner_audio->base
				+ AUDIO_CTRL_IC_RXFIFO_INT_MSK);

				/* First start the FIFO, then enable the tx/rx */
			writel(AUDIO_FIFO_START, sinner_audio->base
				+ AUDIO_CTRL_IC_RXFIFO_OP);
			/* mono capture from dacr*/
			if (substream->runtime->channels == 1)
				writel(0x01, sinner_audio->base
					+ AUDIO_CTRL_IC_CODEC_RX_CTRL);
			else
				writel(IC_RX_ENABLE, sinner_audio->base
					+ AUDIO_CTRL_IC_CODEC_RX_CTRL);
		}
		local_irq_restore(irqs);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

struct snd_soc_dai_ops sirf_inner_codec_dai_ops = {
	.startup = sirf_inner_codec_startup,
	.hw_params = sirf_inner_codec_hw_params,
	.shutdown = sirf_inner_codec_shutdown,
	.trigger = sirf_inner_codec_trigger,
};

struct snd_soc_dai_driver sirf_inner_codec_dai = {
	.name = "sirf-soc-inner",
	.playback = {
		.stream_name = "Audio Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.stream_name = "Audio Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &sirf_inner_codec_dai_ops,
};
EXPORT_SYMBOL_GPL(sirf_inner_codec_dai);

static int sirf_inner_codec_probe(struct snd_soc_codec *codec)
{
	if (of_device_is_compatible(codec->dev->of_node, "sirf,prima2-audio"))
		return snd_soc_add_codec_controls(codec,
			snd_sirf_inner_volume_controls_prima2,
			ARRAY_SIZE(snd_sirf_inner_volume_controls_prima2));
	if (of_device_is_compatible(codec->dev->of_node, "sirf,atlas6-audio"))
		return snd_soc_add_codec_controls(codec,
			snd_sirf_inner_volume_controls_atlas6,
			ARRAY_SIZE(snd_sirf_inner_volume_controls_atlas6));

	return -EINVAL;
}

static int sirf_inner_codec_remove(struct snd_soc_codec *codec)
{
	return 0;
}

static unsigned int sirf_inner_codec_reg_read(struct snd_soc_codec *codec,
		unsigned int reg)
{
	struct sirf_soc_inner_audio *sinner_audio = dev_get_drvdata(codec->dev);
	return readl(sinner_audio->base + reg);
}

static int sirf_inner_codec_reg_write(struct snd_soc_codec *codec,
	unsigned int reg, unsigned int val)
{
	struct sirf_soc_inner_audio *sinner_audio = dev_get_drvdata(codec->dev);
	writel(val, sinner_audio->base + reg);
	return 0;
}


static struct snd_soc_codec_driver soc_codec_device_sirf_inner_codec = {
	.probe = sirf_inner_codec_probe,
	.remove = sirf_inner_codec_remove,
	.read = sirf_inner_codec_reg_read,
	.write = sirf_inner_codec_reg_write,
};

static struct sirf_pcm_dma_data sirf_soc_inner_dai_dma_data[2] = {
	{
		.name = "Audio Playback",
	}, {
		.name = "Audio Capture",
	},
};

static int sirf_soc_inner_dai_startup(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	snd_soc_dai_set_dma_data(dai, substream,
			&sirf_soc_inner_dai_dma_data[substream->stream]);
	return 0;
}

static int sirf_soc_inner_dai_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
	int playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	struct sirf_soc_inner_audio *sinner_audio =
			snd_soc_dai_get_drvdata(dai);
	if (playback) {
		writel(AUDIO_FIFO_RESET,
				sinner_audio->base + AUDIO_CTRL_IC_TXFIFO_OP);

		writel(0x00,
				sinner_audio->base + AUDIO_CTRL_IC_TXFIFO_OP);
	} else {
		writel(AUDIO_FIFO_RESET,
				sinner_audio->base + AUDIO_CTRL_IC_RXFIFO_OP);

		writel(0x00,
				sinner_audio->base + AUDIO_CTRL_IC_RXFIFO_OP);
	}
	return 0;
}

static const struct snd_soc_dai_ops sirf_soc_inner_dai_ops = {
	.startup        = sirf_soc_inner_dai_startup,
	.hw_params      = sirf_soc_inner_dai_hw_params,
};

static struct snd_soc_dai_driver sirf_soc_inner_dai = {
	.name		= "sirf-soc-inner",
	.id			= 0,
	.playback = {
		.stream_name = "inner Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.stream_name = "inner Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &sirf_soc_inner_dai_ops,
};

static const struct snd_soc_component_driver sirf_soc_inner_component = {
	.name		= "sirf-soc-inner",
};

static const struct of_device_id sirf_soc_inner_of_match[] = {
	{ .compatible = "sirf,prima2-audio", .data = &sirf_soc_inner_audio_reg_bits_prima2 },
	{ .compatible = "sirf,atlas6-audio", .data = &sirf_soc_inner_audio_reg_bits_atlas6 },
	{}
};
MODULE_DEVICE_TABLE(of, sirf_soc_inner_of_match);

static int sirf_soc_inner_probe(struct platform_device *pdev)
{
	int ret;
	u32 rx_dma_ch, tx_dma_ch;
	struct sirf_soc_inner_audio *sinner_audio;
	struct resource *mem_res;
	struct device_node *dn = NULL;
	const struct of_device_id *match;

	match = of_match_node(sirf_soc_inner_of_match, pdev->dev.of_node);

	sinner_audio = devm_kzalloc(&pdev->dev,
		sizeof(struct sirf_soc_inner_audio), GFP_KERNEL);
	if (!sinner_audio)
		return -ENOMEM;

	platform_set_drvdata(pdev, sinner_audio);

	ret = of_property_read_u32(pdev->dev.of_node,
			"sirf,inner-audio-dma-rx-channel", &rx_dma_ch);
	if (ret < 0) {
		dev_err(&pdev->dev, "Unable to audio capture dma channel\n");
		return ret;
	}
	ret = of_property_read_u32(pdev->dev.of_node,
			"sirf,inner-audio-dma-tx-channel", &tx_dma_ch);
	if (ret < 0) {
		dev_err(&pdev->dev, "Unable to audio playback dma channel\n");
		return ret;
	}

	dn = of_find_compatible_node(dn, NULL, "sirf,prima2-pwrc");
	if (!dn) {
		dev_err(&pdev->dev, "Failed to get sirf,prima2-pwrc  node!\n");
		return -ENODEV;
	}

	ret = of_property_read_u32(dn, "reg", &sinner_audio->sys_pwrc_reg_base);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed tp get pwrc register base address\n");
		return -EINVAL;
	}

	sirf_soc_inner_dai_dma_data[0].dma_req = tx_dma_ch;
	sirf_soc_inner_dai_dma_data[1].dma_req = rx_dma_ch;

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	sinner_audio->base = devm_ioremap_resource(&pdev->dev, mem_res);
	if (sinner_audio->base == NULL)
		return -ENOMEM;

	sinner_audio->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(sinner_audio->clk)) {
		dev_err(&pdev->dev, "Get clock failed.\n");
		return PTR_ERR(sinner_audio->clk);
	}
	clk_prepare_enable(sinner_audio->clk);

	sinner_audio->irq = platform_get_irq(pdev, 0);
	if (sinner_audio->irq < 0) {
		dev_err(&pdev->dev, "Get irq failed.\n");
		ret = -ENXIO;
		goto err_clk_put;
	}
	ret = snd_soc_register_component(&pdev->dev, &sirf_soc_inner_component,
		&sirf_soc_inner_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "Register Audio SoC dai failed.\n");
		goto err_clk_put;
	}

	ret = snd_soc_register_codec(&(pdev->dev),
			&soc_codec_device_sirf_inner_codec,
			&sirf_inner_codec_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "Register Audio Codec dai failed.\n");
		goto err_com_unreg;
	}

	sinner_audio->reg_bits = (struct sirf_soc_inner_audio_reg_bits *)match->data;

	spin_lock_init(&sinner_audio->lock);
	writel((readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL1)
				| (1 << sinner_audio->reg_bits->codec_clk_en_bits)),
			sinner_audio->base + AUDIO_IC_CODEC_CTRL1);
	writel((readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL1)
			| (1 << sinner_audio->reg_bits->adc14b_12_bits)),
			sinner_audio->base + AUDIO_IC_CODEC_CTRL1);
	writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0) | IC_CPFREQ,
			sinner_audio->base + AUDIO_IC_CODEC_CTRL0);
	writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0) | IC_CPEN,
			sinner_audio->base + AUDIO_IC_CODEC_CTRL0);
	return 0;

err_com_unreg:
	snd_soc_unregister_component(&pdev->dev);
err_clk_put:
	clk_disable_unprepare(sinner_audio->clk);
	return ret;
}

static int sirf_soc_inner_remove(struct platform_device *pdev)
{
	struct sirf_soc_inner_audio *sinner_audio = platform_get_drvdata(pdev);

	clk_disable_unprepare(sinner_audio->clk);
	snd_soc_unregister_codec(&(pdev->dev));
	snd_soc_unregister_component(&pdev->dev);

	return 0;
}

#ifdef CONFIG_PM
static int sirf_soc_inner_suspend(struct platform_device *pdev,
		pm_message_t msg)
{
	struct sirf_soc_inner_audio *sinner_audio;
	sinner_audio = platform_get_drvdata(pdev);
	clk_disable_unprepare(sinner_audio->clk);
	return 0;
}

static int sirf_soc_inner_resume(struct platform_device *pdev)
{
	struct sirf_soc_inner_audio *sinner_audio;
	sinner_audio = platform_get_drvdata(pdev);

	clk_prepare_enable(sinner_audio->clk);

	writel((readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL1)
		| (1 << sinner_audio->reg_bits->codec_clk_en_bits)),
		sinner_audio->base + AUDIO_IC_CODEC_CTRL1);
	writel((readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL1)
		| (1 << sinner_audio->reg_bits->adc14b_12_bits)),
		sinner_audio->base + AUDIO_IC_CODEC_CTRL1);
	writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0) | IC_CPFREQ,
		sinner_audio->base + AUDIO_IC_CODEC_CTRL0);
	writel(readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0) | IC_CPEN,
		sinner_audio->base + AUDIO_IC_CODEC_CTRL0);
	return 0;
}
#else
#define sirf_soc_inner_suspend NULL
#define sirf_soc_inner_resume NULL
#endif

static struct platform_driver sirf_soc_inner_driver = {
	.driver = {
		.name = "sirf-soc-inner",
		.owner = THIS_MODULE,
		.of_match_table = sirf_soc_inner_of_match,
	},
	.probe = sirf_soc_inner_probe,
	.remove = sirf_soc_inner_remove,
	.suspend = sirf_soc_inner_suspend,
	.resume = sirf_soc_inner_resume,
};

module_platform_driver(sirf_soc_inner_driver);

MODULE_DESCRIPTION("SiRF SoC inner bus and codec driver");
MODULE_AUTHOR("RongJun Ying <Rongjun.Ying@csr.com>");
MODULE_LICENSE("GPL v2");
