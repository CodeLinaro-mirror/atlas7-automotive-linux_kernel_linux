/*
 * SiRF inner audio codec driver
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/rtc/sirfsoc_rtciobrg.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

#include "sirf-audio.h"

struct sirf_soc_inner_audio_reg_bits {
	u32 dig_mic_en_bits;
	u32 dig_mic_freq_bits;
	u32 adc14b_12_bits;
	u32 firdac_hsl_en_bits;
	u32 firdac_hsr_en_bits;
	u32 firdac_lout_en_bits;
	u32 por_bits;
	u32 codec_clk_en_bits;
};

struct sirf_soc_inner_audio {
	void __iomem            *base;
	struct clk              *clk;
	spinlock_t              lock;
	u32			sys_pwrc_reg_base;
	struct sirf_soc_inner_audio_reg_bits *reg_bits;
	u32			reg_ctrl0, reg_ctrl1;
	struct platform_device	*sirf_pcm_pdev;
};

static struct sirf_soc_inner_audio_reg_bits sirf_soc_inner_audio_reg_bits_prima2 = {
	20, 21, 22, 23, 24, 25, 26, 27,
};

static struct sirf_soc_inner_audio_reg_bits sirf_soc_inner_audio_reg_bits_atlas6 = {
	22, 23, 24, 25, 26, 27, 28, 29,
};
static const char * const input_mode_mux[] = {"Single-ended",
	"Differential"};

static const struct soc_enum sirf_inner_enum[] = {
	SOC_ENUM_SINGLE(AUDIO_IC_CODEC_CTRL1, 4, 2, input_mode_mux),
};

static const struct snd_kcontrol_new sirf_inner_input_mode_control =
	SOC_DAPM_ENUM("Route", sirf_inner_enum[0]);

static struct snd_kcontrol_new volume_controls_atlas6[] = {
	SOC_DOUBLE("Playback Volume", AUDIO_IC_CODEC_CTRL0, 21, 14,
			0x7F, 0),
	SOC_DOUBLE("Capture Volume", AUDIO_IC_CODEC_CTRL1, 16, 10,
			0x3F, 0),
};

static struct snd_kcontrol_new volume_controls_prima2[] = {
	SOC_DOUBLE("Speaker Volume", AUDIO_IC_CODEC_CTRL0, 21, 14,
			0x7F, 0),
	SOC_DOUBLE("Capture Volume", AUDIO_IC_CODEC_CTRL1, 15, 10,
			0x1F, 0),
};

static struct snd_kcontrol_new left_input_path_controls[] = {
	SOC_DAPM_SINGLE("Line left Switch", AUDIO_IC_CODEC_CTRL1, 6, 1, 0),
	SOC_DAPM_SINGLE("Mic left Switch", AUDIO_IC_CODEC_CTRL1, 3, 1, 0),
};

static struct snd_kcontrol_new right_input_path_controls[] = {
	SOC_DAPM_SINGLE("Line right Switch", AUDIO_IC_CODEC_CTRL1, 5, 1, 0),
	SOC_DAPM_SINGLE("Mic right Switch", AUDIO_IC_CODEC_CTRL1, 2, 1, 0),
};

static struct snd_kcontrol_new left_dac_to_hp_left_amp_switch_control =
	SOC_DAPM_SINGLE("Switch", AUDIO_IC_CODEC_CTRL0, 9, 1, 0);

static struct snd_kcontrol_new left_dac_to_hp_right_amp_switch_control =
	SOC_DAPM_SINGLE("Switch", AUDIO_IC_CODEC_CTRL0, 8, 1, 0);

static struct snd_kcontrol_new right_dac_to_hp_left_amp_switch_control =
	SOC_DAPM_SINGLE("Switch", AUDIO_IC_CODEC_CTRL0, 7, 1, 0);

static struct snd_kcontrol_new right_dac_to_hp_right_amp_switch_control =
	SOC_DAPM_SINGLE("Switch", AUDIO_IC_CODEC_CTRL0, 6, 1, 0);

static struct snd_kcontrol_new left_dac_to_speaker_lineout_switch_control =
	SOC_DAPM_SINGLE("Switch", AUDIO_IC_CODEC_CTRL0, 11, 1, 0);

static struct snd_kcontrol_new right_dac_to_speaker_lineout_switch_control =
	SOC_DAPM_SINGLE("Switch", AUDIO_IC_CODEC_CTRL0, 10, 1, 0);

static int adc_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct sirf_soc_inner_audio *sinner_audio = dev_get_drvdata(codec->dev);
	u32 val;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Enable capture power of codec*/
		val = sirfsoc_rtc_iobrg_readl(sinner_audio->sys_pwrc_reg_base +
			PWRC_PDN_CTRL_OFFSET);
		val |= (1 << AUDIO_POWER_EN_BIT);
		sirfsoc_rtc_iobrg_writel(val,
			sinner_audio->sys_pwrc_reg_base + PWRC_PDN_CTRL_OFFSET);
		break;
	case SND_SOC_DAPM_POST_PMU:
		/*After enable adc, Delay 200ms to avoid pop noise*/
		msleep(200);
	default:
		break;
	}

	return 0;
}

static int hp_amp_left_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct sirf_soc_inner_audio *sinner_audio = dev_get_drvdata(codec->dev);
	u32 val;

	val = snd_soc_read(codec, AUDIO_IC_CODEC_CTRL1);
	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		val |= (1 << sinner_audio->reg_bits->firdac_hsl_en_bits);
		break;
	case SND_SOC_DAPM_POST_PMD:
		val &= ~(1 << sinner_audio->reg_bits->firdac_hsl_en_bits);
	default:
		break;
	}
	snd_soc_write(codec, AUDIO_IC_CODEC_CTRL1, val);
	return 0;
}

static int hp_amp_right_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct sirf_soc_inner_audio *sinner_audio = dev_get_drvdata(codec->dev);
	u32 val;

	val = snd_soc_read(codec, AUDIO_IC_CODEC_CTRL1);
	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		val |= (1 << sinner_audio->reg_bits->firdac_hsr_en_bits);
		break;
	case SND_SOC_DAPM_POST_PMD:
		val &= ~(1 << sinner_audio->reg_bits->firdac_hsr_en_bits);
	default:
		break;
	}
	snd_soc_write(codec, AUDIO_IC_CODEC_CTRL1, val);
	return 0;
}

static int speaker_output_enable_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct sirf_soc_inner_audio *sinner_audio = dev_get_drvdata(codec->dev);
	u32 val;

	val = snd_soc_read(codec, AUDIO_IC_CODEC_CTRL1);
	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		val |= (1 << sinner_audio->reg_bits->firdac_lout_en_bits);
		break;
	case SND_SOC_DAPM_POST_PMD:
		val &= ~(1 << sinner_audio->reg_bits->firdac_lout_en_bits);
	default:
		break;
	}
	snd_soc_write(codec, AUDIO_IC_CODEC_CTRL1, val);
	return 0;
}

static const struct snd_soc_dapm_widget sirf_inner_dapm_widgets[] = {
	SND_SOC_DAPM_DAC("DAC left", NULL, AUDIO_IC_CODEC_CTRL0, 1, 0),
	SND_SOC_DAPM_DAC("DAC right", NULL, AUDIO_IC_CODEC_CTRL0, 0, 0),
	SND_SOC_DAPM_SWITCH("Left dac to hp left amp", SND_SOC_NOPM, 0, 0,
			&left_dac_to_hp_left_amp_switch_control),
	SND_SOC_DAPM_SWITCH("Left dac to hp right amp", SND_SOC_NOPM, 0, 0,
			&left_dac_to_hp_right_amp_switch_control),
	SND_SOC_DAPM_SWITCH("Right dac to hp left amp", SND_SOC_NOPM, 0, 0,
			&right_dac_to_hp_left_amp_switch_control),
	SND_SOC_DAPM_SWITCH("Right dac to hp right amp", SND_SOC_NOPM, 0, 0,
			&right_dac_to_hp_right_amp_switch_control),
	SND_SOC_DAPM_OUT_DRV_E("HP amp left driver", AUDIO_IC_CODEC_CTRL0, 3, 0,
			NULL, 0, hp_amp_left_event,
			SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_OUT_DRV_E("HP amp right driver", AUDIO_IC_CODEC_CTRL0, 2, 0,
			NULL, 0, hp_amp_right_event,
			SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SWITCH("Left dac to speaker lineout", SND_SOC_NOPM, 0, 0,
			&left_dac_to_speaker_lineout_switch_control),
	SND_SOC_DAPM_SWITCH("Right dac to speaker lineout", SND_SOC_NOPM, 0, 0,
			&right_dac_to_speaker_lineout_switch_control),
	SND_SOC_DAPM_OUT_DRV_E("Speaker output driver", AUDIO_IC_CODEC_CTRL0, 4, 0,
			NULL, 0, speaker_output_enable_event,
			SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_OUTPUT("HPOUTL"),
	SND_SOC_DAPM_OUTPUT("HPOUTR"),
	SND_SOC_DAPM_OUTPUT("SPKOUT"),

	SND_SOC_DAPM_ADC_E("ADC left", NULL, AUDIO_IC_CODEC_CTRL1, 8, 0,
			adc_event,
			SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_ADC_E("ADC right", NULL, AUDIO_IC_CODEC_CTRL1, 7, 0,
			adc_event, SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_MIXER("Left PGA mixer", AUDIO_IC_CODEC_CTRL1, 1, 0,
		&left_input_path_controls[0],
		ARRAY_SIZE(left_input_path_controls)),
	SND_SOC_DAPM_MIXER("Right PGA mixer", AUDIO_IC_CODEC_CTRL1, 0, 0,
		&right_input_path_controls[0],
		ARRAY_SIZE(right_input_path_controls)),

	SND_SOC_DAPM_MUX("Mic input mode mux", SND_SOC_NOPM, 0, 0,
			&sirf_inner_input_mode_control),
	SND_SOC_DAPM_MICBIAS("Mic Bias", AUDIO_IC_CODEC_PWR, 3, 0),
	SND_SOC_DAPM_INPUT("MICIN1"),
	SND_SOC_DAPM_INPUT("MICIN2"),
	SND_SOC_DAPM_INPUT("LINEIN1"),
	SND_SOC_DAPM_INPUT("LINEIN2"),

	SND_SOC_DAPM_SUPPLY("HSL Phase Opposite", AUDIO_IC_CODEC_CTRL0,
			30, 0, NULL, 0),
};

static const struct snd_soc_dapm_route sirf_inner_audio_map[] = {
	{"SPKOUT", NULL, "Speaker output driver"},
	{"Speaker output driver", NULL, "Left dac to speaker lineout"},
	{"Speaker output driver", NULL, "Right dac to speaker lineout"},
	{"Left dac to speaker lineout", "Switch", "DAC left"},
	{"Right dac to speaker lineout", "Switch", "DAC right"},
	{"HPOUTL", NULL, "HP amp left driver"},
	{"HPOUTR", NULL, "HP amp right driver"},
	{"HP amp left driver", NULL, "Right dac to hp left amp"},
	{"HP amp right driver", NULL , "Right dac to hp right amp"},
	{"HP amp left driver", NULL, "Left dac to hp left amp"},
	{"HP amp right driver", NULL , "Right dac to hp right amp"},
	{"Right dac to hp left amp", "Switch", "DAC left"},
	{"Right dac to hp right amp", "Switch", "DAC right"},
	{"Left dac to hp left amp", "Switch", "DAC left"},
	{"Left dac to hp right amp", "Switch", "DAC right"},
	{"DAC left", NULL, "Playback"},
	{"DAC right", NULL, "Playback"},
	{"DAC left", NULL, "HSL Phase Opposite"},
	{"DAC right", NULL, "HSL Phase Opposite"},

	{"Capture", NULL, "ADC left"},
	{"Capture", NULL, "ADC right"},
	{"ADC left", NULL, "Left PGA mixer"},
	{"ADC right", NULL, "Right PGA mixer"},
	{"Left PGA mixer", "Line left Switch", "LINEIN2"},
	{"Right PGA mixer", "Line right Switch", "LINEIN1"},
	{"Left PGA mixer", "Mic left Switch", "MICIN2"},
	{"Right PGA mixer", "Mic right Switch", "Mic input mode mux"},
	{"Mic input mode mux", "Single-ended", "MICIN1"},
	{"Mic input mode mux", "Differential", "MICIN1"},
};

static int sirf_inner_codec_startup(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	pm_runtime_get_sync(codec->dev);
	return 0;
}

static void sirf_inner_codec_shutdown(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	pm_runtime_put(codec->dev);
}

static int sirf_inner_codec_trigger(struct snd_pcm_substream *substream,
		int cmd,
		struct snd_soc_dai *dai)
{
	struct sirf_soc_inner_audio *sinner_audio = snd_soc_dai_get_drvdata(dai);
	int playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	struct snd_soc_codec *codec = dai->codec;
	u32 val;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		spin_lock(&sinner_audio->lock);
		if (playback) {
			/*Disconnect HP amp connect to avoid pop noise*/
			val = snd_soc_read(codec, AUDIO_IC_CODEC_CTRL0);
			val &= ~(IC_HSLEN | IC_HSREN);
			snd_soc_write(codec, AUDIO_IC_CODEC_CTRL0, val);

			snd_soc_write(codec, AUDIO_CTRL_IC_TXFIFO_OP, 0x0);

			val = snd_soc_read(codec, AUDIO_CTRL_IC_CODEC_TX_CTRL);
			val &= ~IC_TX_ENABLE;
			snd_soc_write(codec, AUDIO_CTRL_IC_CODEC_TX_CTRL, val);
		} else {
			val = snd_soc_read(codec, AUDIO_CTRL_IC_CODEC_RX_CTRL);
			val &= ~IC_RX_ENABLE;
			snd_soc_write(codec, AUDIO_CTRL_IC_CODEC_RX_CTRL, val);
		}
		spin_unlock(&sinner_audio->lock);
		break;
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		spin_lock(&sinner_audio->lock);
		if (playback) {
			snd_soc_write(codec, AUDIO_CTRL_IC_TXFIFO_OP, AUDIO_FIFO_RESET);
			snd_soc_write(codec, AUDIO_CTRL_IC_TXFIFO_INT_MSK, 0);
			snd_soc_write(codec, AUDIO_CTRL_IC_TXFIFO_OP, 0x0);
			snd_soc_write(codec, AUDIO_CTRL_IC_TXFIFO_OP,
				AUDIO_FIFO_START);
			snd_soc_write(codec, AUDIO_CTRL_IC_CODEC_TX_CTRL,
				IC_TX_ENABLE);
			val = snd_soc_read(codec, AUDIO_IC_CODEC_CTRL0);
			val |= (IC_HSLEN | IC_HSREN);
			snd_soc_write(codec, AUDIO_IC_CODEC_CTRL0, val);
		} else {
			snd_soc_write(codec, AUDIO_CTRL_IC_RXFIFO_OP, AUDIO_FIFO_RESET);
			/* unmask rx fifo interrupt */
			snd_soc_write(codec, AUDIO_CTRL_IC_RXFIFO_INT_MSK, 0);

			snd_soc_write(codec, AUDIO_CTRL_IC_RXFIFO_OP, 0x0);
			/* First start the FIFO, then enable the tx/rx */
			snd_soc_write(codec, AUDIO_CTRL_IC_RXFIFO_OP,
					AUDIO_FIFO_START);
			/* mono capture from dacr*/
			if (substream->runtime->channels == 1)
				snd_soc_write(codec,
					AUDIO_CTRL_IC_CODEC_RX_CTRL, 0x1);
			else
				snd_soc_write(codec,
					AUDIO_CTRL_IC_CODEC_RX_CTRL,
					IC_RX_ENABLE);
		}
		spin_unlock(&sinner_audio->lock);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

struct snd_soc_dai_ops sirf_inner_codec_dai_ops = {
	.startup = sirf_inner_codec_startup,
	.shutdown = sirf_inner_codec_shutdown,
	.trigger = sirf_inner_codec_trigger,
};

struct snd_soc_dai_driver sirf_inner_codec_dai = {
	.name = "sirf-soc-inner",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.stream_name = "Capture",
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
	pm_runtime_enable(codec->dev);
	if (of_device_is_compatible(codec->dev->of_node, "sirf,prima2-audio"))
		return snd_soc_add_codec_controls(codec,
			volume_controls_prima2,
			ARRAY_SIZE(volume_controls_prima2));
	if (of_device_is_compatible(codec->dev->of_node, "sirf,atlas6-audio"))
		return snd_soc_add_codec_controls(codec,
			volume_controls_atlas6,
			ARRAY_SIZE(volume_controls_atlas6));

	return -EINVAL;
}

static int sirf_inner_codec_remove(struct snd_soc_codec *codec)
{
	pm_runtime_disable(codec->dev);
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
	.dapm_widgets = sirf_inner_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sirf_inner_dapm_widgets),
	.dapm_routes = sirf_inner_audio_map,
	.num_dapm_routes = ARRAY_SIZE(sirf_inner_audio_map),
	.idle_bias_off = true,
};

static struct snd_dmaengine_dai_dma_data dma_data[2];

static int sirf_soc_inner_dai_probe(struct snd_soc_dai *dai)
{
	dai->playback_dma_data = &dma_data[0];
	dai->capture_dma_data = &dma_data[1];
	return 0;
}

static struct snd_soc_dai_driver sirf_soc_inner_dai = {
	.probe = sirf_soc_inner_dai_probe,
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
	u32 val;

	match = of_match_node(sirf_soc_inner_of_match, pdev->dev.of_node);

	sinner_audio = devm_kzalloc(&pdev->dev,
		sizeof(struct sirf_soc_inner_audio), GFP_KERNEL);
	if (!sinner_audio)
		return -ENOMEM;

	sinner_audio->sirf_pcm_pdev = platform_device_register_simple("sirf-pcm-audio",
			1, NULL, 0);
	if (IS_ERR(sinner_audio->sirf_pcm_pdev))
		return PTR_ERR(sinner_audio->sirf_pcm_pdev);

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

	dma_data[0].filter_data = (void *)tx_dma_ch;
	dma_data[1].filter_data = (void *)rx_dma_ch;

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

	ret = devm_snd_soc_register_component(&pdev->dev, &sirf_soc_inner_component,
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
	/*
	 * Always open charge pump, if not, when the charge pump closed the
	 * adc will not stable
	 */
	val = readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0);
	val |= IC_CPFREQ;
	writel(val, sinner_audio->base + AUDIO_IC_CODEC_CTRL0);

	if (of_device_is_compatible(pdev->dev.of_node, "sirf,atlas6-audio")) {
		val |= IC_CPEN;
		writel(val, sinner_audio->base + AUDIO_IC_CODEC_CTRL0);
	}
	spin_lock_init(&sinner_audio->lock);
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

	return 0;
}

#ifdef CONFIG_PM_RUNTIME
static int sirf_inner_runtime_suspend(struct device *dev)
{
	struct sirf_soc_inner_audio *sinner_audio = dev_get_drvdata(dev);
	u32 val;
	val = readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL1);
	val &= ~(1 << sinner_audio->reg_bits->codec_clk_en_bits);
	writel(val, sinner_audio->base + AUDIO_IC_CODEC_CTRL1);
	return 0;
}

static int sirf_inner_runtime_resume(struct device *dev)
{
	struct sirf_soc_inner_audio *sinner_audio = dev_get_drvdata(dev);
	u32 val;
	val = readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL1);
	val |= (1 << sinner_audio->reg_bits->codec_clk_en_bits);
	val &= ~(1 << sinner_audio->reg_bits->por_bits);
	writel(val, sinner_audio->base + AUDIO_IC_CODEC_CTRL1);

	msleep(20);
	val |= (1 << sinner_audio->reg_bits->por_bits);
	writel(val, sinner_audio->base + AUDIO_IC_CODEC_CTRL1);

	return 0;
}
#endif

#ifdef CONFIG_PM_SLEEP
static int sirf_soc_inner_suspend(struct device *dev)
{
	struct sirf_soc_inner_audio *sinner_audio = dev_get_drvdata(dev);

	sinner_audio->reg_ctrl0 = readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL0);
	sinner_audio->reg_ctrl1 = readl(sinner_audio->base + AUDIO_IC_CODEC_CTRL1);
	sirf_inner_runtime_suspend(dev);
	clk_disable_unprepare(sinner_audio->clk);

	return 0;
}

static int sirf_soc_inner_resume(struct device *dev)
{
	struct sirf_soc_inner_audio *sinner_audio = dev_get_drvdata(dev);

	clk_prepare_enable(sinner_audio->clk);

	writel(sinner_audio->reg_ctrl0,
		sinner_audio->base + AUDIO_IC_CODEC_CTRL0);
	writel(sinner_audio->reg_ctrl1, sinner_audio->base + AUDIO_IC_CODEC_CTRL1);

	if (!pm_runtime_status_suspended(dev))
		sirf_inner_runtime_resume(dev);

	return 0;
}
#endif

static const struct dev_pm_ops sirf_inner_pm_ops = {
	SET_RUNTIME_PM_OPS(sirf_inner_runtime_suspend, sirf_inner_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(sirf_soc_inner_suspend, sirf_soc_inner_resume)
};

static struct platform_driver sirf_soc_inner_driver = {
	.driver = {
		.name = "sirf-soc-inner",
		.owner = THIS_MODULE,
		.of_match_table = sirf_soc_inner_of_match,
		.pm = &sirf_inner_pm_ops,
	},
	.probe = sirf_soc_inner_probe,
	.remove = sirf_soc_inner_remove,
};

module_platform_driver(sirf_soc_inner_driver);

MODULE_DESCRIPTION("SiRF SoC inner bus and codec driver");
MODULE_AUTHOR("RongJun Ying <Rongjun.Ying@csr.com>");
MODULE_LICENSE("GPL v2");
