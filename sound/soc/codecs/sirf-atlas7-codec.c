/*
 * SiRF ATLAS7 internal codec driver
 *
 * Copyright (c) 2014 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

#include "sirf-atlas7-codec.h"

/* Temporary debugging support, will be removed */
#ifdef CONFIG_SND_SOC_SIRF_KALIMBA_DEBUG
#include "../sirf/kalimba/iacc.h"
#endif

struct sirf_atlas7_codec {
	struct clk *clk;
	struct regmap *regmap;
	/* The audio adc and dac use the 2.5V LDO and the 1.8V LDO */
	struct regulator *da_reg;
	struct regulator *ad_reg;
	unsigned int playback_volume;
	unsigned int capture_volume;
	unsigned int input_path;
};

enum input_path_enum {
	MIC0_IN,
	MIC1_IN,
	LINE0_IN,
	LINE1_IN,
	LINE2_IN,
	LINE3_IN
};

/* gain_dB = log10(reg_value / 32) *20 */
static const unsigned int volume_reg_values[] = {
	0,/* -32dB */
	3,/* -20dB */
	8,/* -12dB */
	16,/* -6dB */
	32,/* 0dB */
	45,/* 3dB */
	64,/* 6dB */
	90/* 9dB */
};

static int sirf_atlas7_codec_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	int channels = params_channels(params);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (channels != 1 && channels != 4) {
			dev_err(dai->dev, "Only support mono or 4 channels.");
			return -EINVAL;
		}
		switch (params_rate(params)) {
		case 32000:
		case 44100:
		case 48000:
		case 96000:
		case 192000:
			break;
		default:
			dev_err(dai->dev, "Playback rate %d no support\n",
				params_rate(params));
			return -EINVAL;
		}
	} else {
		switch (params_rate(params)) {
		case 8000:
		case 11025:
		case 16000:
		case 22050:
		case 32000:
		case 44100:
		case 48000:
		case 96000:
			break;
		default:
			dev_err(dai->dev, "Capture rate %d no support\n",
				params_rate(params));
			return -EINVAL;
		}

	}
	return 0;
}

struct rate_reg_values_t {
	unsigned int rate;
	u32 value;
};

struct rate_reg_values_t rate_dac_reg_values[] = {
	{32000, DAC_BASE_SMAPLE_RATE_32K0},
	{44100, DAC_BASE_SMAPLE_RATE_44K1},
	{48000, DAC_BASE_SMAPLE_RATE_48K0},
	{96000, DAC_BASE_SMAPLE_RATE_96K0},
	{192000, DAC_BASE_SMAPLE_RATE_192K0},
};

struct rate_reg_values_t rate_adc_reg_values[] = {
	{8000, ADC_SAMPLE_RATE_08K},
	{11025, ADC_SAMPLE_RATE_11K},
	{16000, ADC_SAMPLE_RATE_16K},
	{22050, ADC_SAMPLE_RATE_22K},
	{32000, ADC_SAMPLE_RATE_32K},
	{44100, ADC_SAMPLE_RATE_44K},
	{48000, ADC_SAMPLE_RATE_48K},
	{96000, ADC_SAMPLE_RATE_96K},
};

static u32 rate_reg_value(struct snd_pcm_substream *substream)
{
	int i;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		for (i = 0; i < ARRAY_SIZE(rate_dac_reg_values); i++) {
			if (rate_dac_reg_values[i].rate ==
				substream->runtime->rate)
				return KCODEC_DAC_SELECT_EXT
					| rate_dac_reg_values[i].value
					<< KCODEC_DAC_EXT_BASE_SAMP_RATE_SHIFT;
		}
	} else {
		for (i = 0; i < ARRAY_SIZE(rate_adc_reg_values); i++) {
			if (rate_adc_reg_values[i].rate ==
				substream->runtime->rate)
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

static u32 dac_gain_regs[] = {
	KCODEC_DAC_A_GAIN,
	KCODEC_DAC_B_GAIN,
	KCODEC_DAC_C_GAIN,
	KCODEC_DAC_D_GAIN
};

static u32 adc_gain_regs[] = {
	KCODEC_ADC_A_GAIN,
	KCODEC_ADC_B_GAIN
};

static const int input_path_val[];

static int sirf_atlas7_codec_trigger(struct snd_pcm_substream *substream,
		int cmd, struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct sirf_atlas7_codec *atlas7_codec = dev_get_drvdata(codec->dev);
	int channels = substream->runtime->channels;
	int i;
	u32 volume_level;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			volume_level = atlas7_codec->playback_volume;
			for (i = 0; i < channels; i++) {
				snd_soc_update_bits(codec, dac_gain_regs[i],
					AUDIO_GAIN_MASK,
					volume_reg_values[volume_level]);
				snd_soc_write(codec, dac_sample_rate_regs[i],
					rate_reg_value(substream));
			}
		} else {
			volume_level = atlas7_codec->capture_volume;
			if (atlas7_codec->input_path == MIC0_IN)
				snd_soc_update_bits(codec, AUDIO_ANA_ADC_CTRL2,
					AUDIO_ANA_ADC_MICAMP_GAIN_SEL_MASK,
					AUDIO_ANA_ADC_MICAMP_GAIN);
			else if (atlas7_codec->input_path == MIC1_IN)
				snd_soc_update_bits(codec, AUDIO_ANA_ADC_CTRL3,
					AUDIO_ANA_ADC_MICAMP_GAIN_SEL_MASK,
					AUDIO_ANA_ADC_MICAMP_GAIN);
			snd_soc_update_bits(codec, AUDIO_ANA_ADC_CTRL0, 0xFFFF,
				input_path_val[atlas7_codec->input_path]);

			for (i = 0; i < channels; i++) {
				snd_soc_update_bits(codec, adc_gain_regs[i],
					AUDIO_GAIN_MASK,
					volume_reg_values[volume_level]);
				snd_soc_write(codec, KCODEC_ADC_A_SAMP_RATE
					+ (i * 0x20),
					rate_reg_value(substream));
			}
		}
		break;
	}
	return 0;
}

struct snd_soc_dai_ops sirf_atlas7_codec_dai_ops = {
	.hw_params = sirf_atlas7_codec_hw_params,
	.trigger = sirf_atlas7_codec_trigger,
};

#define ATLAS7_CODEC_DAC_RATES	(SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 \
				| SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 \
				| SNDRV_PCM_RATE_192000)

#define ATLAS7_CODEC_ADC_RATES	(SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 \
				| SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 \
				| SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 \
				| SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000)

#define ATLAS7_CODEC_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE \
				| SNDRV_PCM_FMTBIT_S24_LE)

struct snd_soc_dai_driver sirf_atlas7_codec_dai = {
	.name = "atlas7-codec-hifi",
	.playback = {
		.stream_name = "AIF Playback",
		.channels_min = 1,
		.channels_max = 4,
		.rates = ATLAS7_CODEC_DAC_RATES,
		.formats = ATLAS7_CODEC_FORMATS,
	},
	.capture = {
		.stream_name = "AIF Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = ATLAS7_CODEC_ADC_RATES,
		.formats = ATLAS7_CODEC_FORMATS,
	},
	.ops = &sirf_atlas7_codec_dai_ops,
};

static int vbg_trim_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(w->codec, ANA_PMUCTL2,
			PMUCTRL2_VBG_TRIM, PMUCTRL2_VBG_TRIM_0XF);

		/* Workaround for some registers update fail. */
		snd_soc_update_bits(w->codec,  0x58,
			(0x3 << 5), (2 << 5));
		snd_soc_update_bits(w->codec, 0x50,
			1, 1);
		snd_soc_update_bits(w->codec, 0x50,
			1, 0);
		snd_soc_update_bits(w->codec,  0x58,
			(0x3 << 5), (0 << 5));

		break;
	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(w->codec, ANA_PMUCTL2,
			PMUCTRL2_VBG_TRIM, 0);
		break;
	default:
		break;
	}
	return 0;
}

static int loutbias_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	if (event == SND_SOC_DAPM_POST_PMU) {
		snd_soc_update_bits(w->codec, AUDIO_ANA_REF_CTRL0,
			AUDIO_ANA_REF_AUDBIAS_VAG_RX_EN,
			AUDIO_ANA_REF_AUDBIAS_VAG_RX_EN);
		snd_soc_update_bits(w->codec, AUDIO_REF_CTRL2,
			AUDIO_REF_BIAS_BG_VTH_TRIM_MASK, 4);
		snd_soc_update_bits(w->codec, AUDIO_ANA_REF_CTRL0,
			AUDIO_ANA_REF_AUDBIAS_IREF_TRIM_MASK,
			(7 << AUDIO_ANA_REF_AUDBIAS_IREF_TRIM_SHIFT));
		snd_soc_update_bits(w->codec, AUDIO_REF_CTRL,
			AUDIO_REF_BOOST_EN_DACBUFF_IREF_MASK, 3);
	}
	return 0;
}

static int linbias_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	if (event == SND_SOC_DAPM_POST_PMU) {
		/*
		 * If only enable TX_EN bit, the quality of
		 * audio data is not good.
		 */
		snd_soc_update_bits(w->codec, AUDIO_ANA_REF_CTRL0,
			AUDIO_ANA_REF_AUDBIAS_VAG_TX_EN
			| AUDIO_ANA_REF_AUDBIAS_VAG_RX_EN,
			AUDIO_ANA_REF_AUDBIAS_VAG_TX_EN
			| AUDIO_ANA_REF_AUDBIAS_VAG_RX_EN);
		snd_soc_update_bits(w->codec, AUDIO_CTRL_SPARE_0,
			TXADC_IREF_EN, TXADC_IREF_EN);
		snd_soc_update_bits(w->codec, AUDIO_REF_CTRL2,
			AUDIO_REF_BIAS_BG_VTH_TRIM_MASK
			| AUDIO_REF_BIAS_BG_PTAT_TRIM_MASK,
			4 | (0xC << AUDIO_REF_BIAS_BG_PTAT_TRIM_SHIFT));
		snd_soc_update_bits(w->codec, AUDIO_ANA_REF_CTRL0,
			AUDIO_ANA_REF_AUDBIAS_IREF_TRIM_MASK,
			(7 << AUDIO_ANA_REF_AUDBIAS_IREF_TRIM_SHIFT));
	}
	return 0;
}

static int dac_en_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_update_bits(w->codec, AUDIO_KCODEC_CTRL,
			KCODEC_DAC_EN, KCODEC_DAC_EN);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_update_bits(w->codec, AUDIO_KCODEC_CTRL,
			KCODEC_DAC_EN, 0);
		break;
	}
	return 0;
}

static int adc_en_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_update_bits(w->codec, AUDIO_KCODEC_CTRL,
			KCODEC_ADC_EN, KCODEC_ADC_EN);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_update_bits(w->codec, AUDIO_KCODEC_CTRL,
			KCODEC_ADC_EN, 0);
		break;
	}
	return 0;
}

static int dither_en_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	if (event == SND_SOC_DAPM_POST_PMU)
		snd_soc_update_bits(w->codec,  w->reg,
			KCODEC_CONFIG_DEM_DITHER_CFG_MASK, 0);

	return 0;
}

static int adc_reset_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	if (event == SND_SOC_DAPM_PRE_PMU) {
		snd_soc_update_bits(w->codec,  w->reg,
			AUDIO_ANA_CTRL_ADC_EN, 0);
		snd_soc_update_bits(w->codec, AUDIO_REGS_CLK_CTRL,
			AUDIO_ANA_CAL_CLK_EN, AUDIO_ANA_CAL_CLK_EN);
	}
	return 0;
}

static int sirf_atlas7_codec_get_playback_volume(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct sirf_atlas7_codec *atlas7_codec = dev_get_drvdata(codec->dev);

	ucontrol->value.integer.value[0] = atlas7_codec->playback_volume;
	return 0;
}

static int sirf_atlas7_codec_put_playback_volume(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct sirf_atlas7_codec *atlas7_codec = dev_get_drvdata(codec->dev);

	atlas7_codec->playback_volume = ucontrol->value.integer.value[0];
	snd_soc_update_bits(codec, KCODEC_DAC_A_GAIN, AUDIO_GAIN_MASK,
		volume_reg_values[atlas7_codec->playback_volume]);
	snd_soc_update_bits(codec, KCODEC_DAC_B_GAIN, AUDIO_GAIN_MASK,
		volume_reg_values[atlas7_codec->playback_volume]);
	snd_soc_update_bits(codec, KCODEC_DAC_C_GAIN, AUDIO_GAIN_MASK,
		volume_reg_values[atlas7_codec->playback_volume]);
	snd_soc_update_bits(codec, KCODEC_DAC_D_GAIN, AUDIO_GAIN_MASK,
		volume_reg_values[atlas7_codec->playback_volume]);
	return 0;
}

static int sirf_atlas7_codec_get_capture_volume(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct sirf_atlas7_codec *atlas7_codec = dev_get_drvdata(codec->dev);

	ucontrol->value.integer.value[0] = atlas7_codec->capture_volume;
	return 0;
}

static int sirf_atlas7_codec_put_capture_volume(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct sirf_atlas7_codec *atlas7_codec = dev_get_drvdata(codec->dev);

	atlas7_codec->capture_volume = ucontrol->value.integer.value[0];
	snd_soc_update_bits(codec, KCODEC_ADC_A_GAIN, AUDIO_GAIN_MASK,
		volume_reg_values[atlas7_codec->capture_volume]);
	snd_soc_update_bits(codec, KCODEC_ADC_B_GAIN, AUDIO_GAIN_MASK,
		volume_reg_values[atlas7_codec->capture_volume]);
	return 0;
}

static int sirf_atlas7_codec_dapm_get_input_path_enum(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_dapm_kcontrol_codec(kcontrol);
	struct sirf_atlas7_codec *atlas7_codec = dev_get_drvdata(codec->dev);

	ucontrol->value.enumerated.item[0] = atlas7_codec->input_path;
	return 0;
}

static int sirf_atlas7_codec_dapm_put_input_path_enum(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_dapm_kcontrol_codec(kcontrol);
	struct sirf_atlas7_codec *atlas7_codec = dev_get_drvdata(codec->dev);

	atlas7_codec->input_path = ucontrol->value.enumerated.item[0];
	if (atlas7_codec->input_path == 0)
		snd_soc_update_bits(codec, AUDIO_ANA_ADC_CTRL2,
			AUDIO_ANA_ADC_MICAMP_GAIN_SEL_MASK,
			AUDIO_ANA_ADC_MICAMP_GAIN);
	else if (atlas7_codec->input_path == 1)
		snd_soc_update_bits(codec, AUDIO_ANA_ADC_CTRL3,
			AUDIO_ANA_ADC_MICAMP_GAIN_SEL_MASK,
			AUDIO_ANA_ADC_MICAMP_GAIN);
	else {
		snd_soc_update_bits(codec, AUDIO_ANA_ADC_CTRL2,
			AUDIO_ANA_ADC_MICAMP_GAIN_SEL_MASK, 0);
		snd_soc_update_bits(codec, AUDIO_ANA_ADC_CTRL3,
			AUDIO_ANA_ADC_MICAMP_GAIN_SEL_MASK, 0);
	}

	return snd_soc_dapm_put_enum_double(kcontrol, ucontrol);
}

static const char * const output_mode_text[] = {"Differential",
		"Single-ended"};

static const int output_mode_val[] = {0, 0xf};

static const struct soc_enum output_mode_enum =
	SOC_VALUE_ENUM_SINGLE(AUDIO_DAC_CTRL, 6, 0xf, 2, output_mode_text,
		output_mode_val);

static const struct snd_kcontrol_new sirf_atlas7_codec_output_mode_control =
	SOC_DAPM_ENUM("Output mode", output_mode_enum);

static const char * const input_path_text[] = {"MIC0", "MIC1", "LINE0",
		"LINE1", "LINE2", "LINE3"};
static const int input_path_val[] = {0x1080, 0x0041, 0x1850, 0x1448,
		0x1244, 0x1142};
static const struct soc_enum input_path_enum =
	SOC_VALUE_ENUM_SINGLE(AUDIO_ANA_ADC_CTRL0, 0, 0xFFFF, 6,
		input_path_text, input_path_val);
static const struct snd_kcontrol_new sirf_atlas7_codec_input_path_control =
	SOC_DAPM_ENUM_EXT("Input path", input_path_enum,
		sirf_atlas7_codec_dapm_get_input_path_enum,
		sirf_atlas7_codec_dapm_put_input_path_enum);

/* {-32, -20, -12, -6, 0, +3, +6, +9} dB */
static const DECLARE_TLV_DB_RANGE(sirf_atlas7_volume_tlv,
	0, 0, TLV_DB_SCALE_ITEM(-3200, 0, 0),
	1, 1, TLV_DB_SCALE_ITEM(-2000, 0, 0),
	2, 2, TLV_DB_SCALE_ITEM(-1200, 0, 0),
	3, 3, TLV_DB_SCALE_ITEM(-600, 0, 0),
	4, 4, TLV_DB_SCALE_ITEM(0, 0, 0),
	5, 5, TLV_DB_SCALE_ITEM(300, 0, 0),
	6, 6, TLV_DB_SCALE_ITEM(600, 0, 0),
	7, 7, TLV_DB_SCALE_ITEM(900, 0, 0)
);

static const struct snd_kcontrol_new sirf_atlas7_volume_mixer_controls[] = {
	SOC_SINGLE_EXT_TLV("Playback Volume", NULL, 0, 0x7, 0,
		sirf_atlas7_codec_get_playback_volume,
		sirf_atlas7_codec_put_playback_volume, sirf_atlas7_volume_tlv),
	SOC_SINGLE_EXT_TLV("Capture Volume", NULL, 0, 0x7, 0,
		sirf_atlas7_codec_get_capture_volume,
		sirf_atlas7_codec_put_capture_volume, sirf_atlas7_volume_tlv),
};

static const struct snd_soc_dapm_widget sirf_atlas7_codec_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY("VBG TRIM", SND_SOC_NOPM, 0, 0, vbg_trim_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY_S("IREF EN", 1, AUDIO_ANA_REF_CTRL0, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("LOUTBIAS", 2, SND_SOC_NOPM, 0, 0,
		loutbias_event, SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_SUPPLY_S("LINBIAS", 2, SND_SOC_NOPM, 0, 0,
		linbias_event, SND_SOC_DAPM_POST_PMU),

	SND_SOC_DAPM_MUX("Output mode", SND_SOC_NOPM, 0, 0,
			&sirf_atlas7_codec_output_mode_control),
	SND_SOC_DAPM_MUX("Input path", SND_SOC_NOPM, 0, 0,
			&sirf_atlas7_codec_input_path_control),

	SND_SOC_DAPM_SUPPLY_S("DACACLK", 3, AUDIO_REGS_CLK_CTRL, 6, 0,
		NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DACBCLK", 3, AUDIO_REGS_CLK_CTRL, 7, 0,
		NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DACCCLK", 3, AUDIO_REGS_CLK_CTRL, 8, 0,
		NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DACDCLK", 3, AUDIO_REGS_CLK_CTRL, 9, 0,
		NULL, 0),

	SND_SOC_DAPM_SUPPLY_S("ADCACLK", 3, AUDIO_REGS_CLK_CTRL, 4, 0,
		NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("ADCBCLK", 3, AUDIO_REGS_CLK_CTRL, 5, 0,
		NULL, 0),

	SND_SOC_DAPM_DAC_E("DACA", NULL, KCODEC_CONFIG, 12, 0,
		dac_en_event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_DAC_E("DACB", NULL, KCODEC_CONFIG, 13, 0,
		dac_en_event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_DAC_E("DACC", NULL, KCODEC_CONFIG2, 12, 0,
		dac_en_event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_DAC_E("DACD", NULL, KCODEC_CONFIG2, 13, 0,
		dac_en_event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_AIF_IN("AIFRX", "AIF Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIFTX", "AIF Capture", 0, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_PGA("VGEN EN", AUDIO_ANA_DAC_CTRL0, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DAC A PGA EN", KCODEC_DAC_A_GAIN, 15, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DAC B PGA EN", KCODEC_DAC_B_GAIN, 15, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DAC C PGA EN", KCODEC_DAC_C_GAIN, 15, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DAC D PGA EN", KCODEC_DAC_D_GAIN, 15, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("LOUT0 PGA", 1, AUDIO_ANA_DAC_CTRL0, 1, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("LOUT1 PGA", 1, AUDIO_ANA_DAC_CTRL0, 2, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("LOUT2 PGA", 1, AUDIO_ANA_DAC_CTRL0, 3, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("LOUT3 PGA", 1, AUDIO_ANA_DAC_CTRL0, 4, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("LOUT0 BUF PGA", 1, AUDIO_ANA_DAC_CTRL0, 7, 0,
				NULL, 0),
	SND_SOC_DAPM_PGA_S("LOUT1 BUF PGA", 1, AUDIO_ANA_DAC_CTRL0, 8, 0,
				NULL, 0),
	SND_SOC_DAPM_PGA_S("LOUT2 BUF PGA", 1, AUDIO_ANA_DAC_CTRL0, 9, 0,
				NULL, 0),
	SND_SOC_DAPM_PGA_S("LOUT3 BUF PGA", 1, AUDIO_ANA_DAC_CTRL0, 10, 0,
				NULL, 0),

	SND_SOC_DAPM_PGA_S("DACA RESET", 2, AUDIO_DAC_CTRL, 0, 1, NULL, 0),
	SND_SOC_DAPM_PGA_S("DACB RESET", 2, AUDIO_DAC_CTRL, 1, 1, NULL, 0),
	SND_SOC_DAPM_PGA_S("DACC RESET", 2, AUDIO_DAC_CTRL, 2, 1, NULL, 0),
	SND_SOC_DAPM_PGA_S("DACD RESET", 2, AUDIO_DAC_CTRL, 3, 1, NULL, 0),

	SND_SOC_DAPM_OUT_DRV_E("Dither EN CH01", KCODEC_CONFIG_EXTENSION2,
		5, 1, NULL, 0, dither_en_event, SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_OUT_DRV_E("Dither EN CH23", KCODEC_CONFIG2_EXTENSION2,
		5, 1, NULL, 0, dither_en_event, SND_SOC_DAPM_POST_PMU),

	SND_SOC_DAPM_OUTPUT("LOUT0"),
	SND_SOC_DAPM_OUTPUT("LOUT1"),
	SND_SOC_DAPM_OUTPUT("LOUT2"),
	SND_SOC_DAPM_OUTPUT("LOUT3"),

	SND_SOC_DAPM_ADC_E("ADCA", NULL, KCODEC_CONFIG, 10, 0,
		adc_en_event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("ADCB", NULL, KCODEC_CONFIG, 11, 0,
		adc_en_event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_PGA("ADC A PGA EN", KCODEC_ADC_A_GAIN, 15, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ADC B PGA EN", KCODEC_ADC_B_GAIN, 15, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ADCA ANA EN", AUDIO_ANA_ADC_CTRL2, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ADCB ANA EN", AUDIO_ANA_ADC_CTRL3, 0, 0, NULL, 0),

	SND_SOC_DAPM_PGA_S("ADCA ANA Dither EN", 1, AUDIO_ANA_ADC_CTRL2, 1, 0,
		NULL, 0),
	SND_SOC_DAPM_PGA_S("ADCB ANA Dither EN", 1, AUDIO_ANA_ADC_CTRL3, 1, 0,
		NULL, 0),

	SND_SOC_DAPM_PGA_S("ADCA ANA DWA EN", 2, AUDIO_ANA_ADC_CTRL2, 2, 0,
		NULL, 0),
	SND_SOC_DAPM_PGA_S("ADCB ANA DWA EN", 2, AUDIO_ANA_ADC_CTRL3, 2, 0,
		NULL, 0),

	SND_SOC_DAPM_OUT_DRV_E("ADC RESET", AUDIO_ANA_CAL_CTRL0, 0, 0, NULL, 0,
		adc_reset_event, SND_SOC_DAPM_PRE_PMU),

	SND_SOC_DAPM_INPUT("LIN0"),
	SND_SOC_DAPM_INPUT("LIN1"),
	SND_SOC_DAPM_INPUT("LIN2"),
	SND_SOC_DAPM_INPUT("LIN3"),
	SND_SOC_DAPM_INPUT("MICIN0"),
	SND_SOC_DAPM_INPUT("MICIN1"),
};

static const struct snd_soc_dapm_route sirf_atlas7_codec_map[] = {
	{"DACA", NULL, "DACACLK"},
	{"DACB", NULL, "DACBCLK"},
	{"DACC", NULL, "DACCCLK"},
	{"DACD", NULL, "DACDCLK"},

	{"DACACLK", NULL, "LOUTBIAS"},
	{"DACBCLK", NULL, "LOUTBIAS"},
	{"DACCCLK", NULL, "LOUTBIAS"},
	{"DACDCLK", NULL, "LOUTBIAS"},

	{"LOUTBIAS", NULL, "IREF EN"},
	{"IREF EN", NULL, "VBG TRIM"},

	{"DACA", NULL, "AIFRX"},
	{"DACB", NULL, "AIFRX"},
	{"DACC", NULL, "AIFRX"},
	{"DACD", NULL, "AIFRX"},

	{"VGEN EN", NULL, "DACA"},
	{"VGEN EN", NULL, "DACB"},
	{"VGEN EN", NULL, "DACC"},
	{"VGEN EN", NULL, "DACD"},

	{"Output mode", "Single-ended", "VGEN EN"},
	{"Output mode", "Differential", "VGEN EN"},

	{"LOUT0 PGA", NULL, "Output mode"},
	{"LOUT1 PGA", NULL, "Output mode"},
	{"LOUT2 PGA", NULL, "Output mode"},
	{"LOUT3 PGA", NULL, "Output mode"},

	{"DAC A PGA EN", NULL, "LOUT0 PGA"},
	{"DAC B PGA EN", NULL, "LOUT1 PGA"},
	{"DAC C PGA EN", NULL, "LOUT2 PGA"},
	{"DAC D PGA EN", NULL, "LOUT3 PGA"},

	{"LOUT0 BUF PGA", NULL, "DAC A PGA EN"},
	{"LOUT1 BUF PGA", NULL, "DAC B PGA EN"},
	{"LOUT2 BUF PGA", NULL, "DAC C PGA EN"},
	{"LOUT3 BUF PGA", NULL, "DAC D PGA EN"},

	{"DACA RESET", NULL, "LOUT0 BUF PGA"},
	{"DACB RESET", NULL, "LOUT1 BUF PGA"},
	{"DACC RESET", NULL, "LOUT2 BUF PGA"},
	{"DACD RESET", NULL, "LOUT3 BUF PGA"},

	{"Dither EN CH01", NULL, "DACA RESET"},
	{"Dither EN CH01", NULL, "DACB RESET"},
	{"Dither EN CH23", NULL, "DACC RESET"},
	{"Dither EN CH23", NULL, "DACD RESET"},

	{"LOUT0", NULL, "Dither EN CH01"},
	{"LOUT1", NULL, "Dither EN CH01"},
	{"LOUT2", NULL, "Dither EN CH23"},
	{"LOUT3", NULL, "Dither EN CH23"},

	{"IREF EN", NULL, "VBG TRIM"},
	{"LINBIAS", NULL, "IREF EN"},
	{"ADCACLK", NULL, "LINBIAS"},
	{"ADCBCLK", NULL, "LINBIAS"},
	{"ADCA", NULL, "ADCACLK"},
	{"ADCB", NULL, "ADCBCLK"},
	{"AIFTX", NULL, "ADCA"},
	{"AIFTX", NULL, "ADCB"},
	{"ADCA", NULL, "ADC A PGA EN"},
	{"ADCB", NULL, "ADC B PGA EN"},
	{"ADC A PGA EN", NULL, "ADCA ANA EN"},
	{"ADC B PGA EN", NULL, "ADCB ANA EN"},
	{"ADCA ANA EN", NULL, "ADCA ANA DWA EN"},
	{"ADCB ANA EN", NULL, "ADCB ANA DWA EN"},
	{"ADCA ANA DWA EN", NULL, "ADCA ANA Dither EN"},
	{"ADCB ANA DWA EN", NULL, "ADCB ANA Dither EN"},
	{"ADCA ANA Dither EN", NULL, "ADC RESET"},
	{"ADCB ANA Dither EN", NULL, "ADC RESET"},

	{"ADC RESET", NULL, "Input path"},
	{"Input path", "MIC0", "MICIN0"},
	{"Input path", "MIC1", "MICIN1"},
	{"Input path", "LINE0", "LIN0"},
	{"Input path", "LINE1", "LIN1"},
	{"Input path", "LINE2", "LIN2"},
	{"Input path", "LINE3", "LIN3"},
};

static struct snd_soc_codec_driver soc_codec_device_sirf_atlas7_codec = {
	.dapm_widgets = sirf_atlas7_codec_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sirf_atlas7_codec_dapm_widgets),
	.dapm_routes = sirf_atlas7_codec_map,
	.num_dapm_routes = ARRAY_SIZE(sirf_atlas7_codec_map),
	.controls = sirf_atlas7_volume_mixer_controls,
	.num_controls = ARRAY_SIZE(sirf_atlas7_volume_mixer_controls),
	.idle_bias_off = true,
};

static int sirf_atlas7_codec_runtime_suspend(struct device *dev)
{
	struct sirf_atlas7_codec *atlas7_codec = dev_get_drvdata(dev);

	clk_disable_unprepare(atlas7_codec->clk);
	regulator_disable(atlas7_codec->da_reg);
	regulator_disable(atlas7_codec->ad_reg);
	return 0;
}

static int sirf_atlas7_codec_runtime_resume(struct device *dev)
{
	struct sirf_atlas7_codec *atlas7_codec = dev_get_drvdata(dev);
	int ret;

	ret = regulator_enable(atlas7_codec->da_reg);
	if (ret) {
		dev_err(dev, "Enable LDO failed: %d\n", ret);
		return ret;
	}

	ret = regulator_enable(atlas7_codec->ad_reg);
	if (ret) {
		dev_err(dev, "Enable LDO failed: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(atlas7_codec->clk);
	if (ret) {
		dev_err(dev, "clk_enable failed: %d\n", ret);
		return ret;
	}
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int sirf_atlas7_codec_suspend(struct device *dev)
{
	if (!pm_runtime_status_suspended(dev))
		sirf_atlas7_codec_runtime_suspend(dev);

	return 0;
}

static int sirf_atlas7_codec_resume(struct device *dev)
{
	int ret;

	if (!pm_runtime_status_suspended(dev)) {
		ret = sirf_atlas7_codec_runtime_resume(dev);
		if (ret)
			return ret;
	}
	return 0;
}
#endif

static const struct regmap_config sirf_atlas7_codec_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = KCODEC_WARP_UPDATE,
	.cache_type = REGCACHE_NONE,
};

static int sirf_atlas7_codec_driver_probe(struct platform_device *pdev)
{
	int ret;
	void __iomem *base;
	struct resource *mem_res;
	void __iomem *clk;
	struct sirf_atlas7_codec *atlas7_codec;

	atlas7_codec = devm_kzalloc(&pdev->dev,
		sizeof(struct sirf_atlas7_codec), GFP_KERNEL);
	if (!atlas7_codec)
		return -ENOMEM;
	platform_set_drvdata(pdev, atlas7_codec);

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, mem_res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	atlas7_codec->regmap = devm_regmap_init_mmio(&pdev->dev, base,
					    &sirf_atlas7_codec_regmap_config);
	if (IS_ERR(atlas7_codec->regmap))
		return PTR_ERR(atlas7_codec->regmap);

	atlas7_codec->da_reg = devm_regulator_get(&pdev->dev, "ldo0");
	if (IS_ERR(atlas7_codec->da_reg)) {
		ret = PTR_ERR(atlas7_codec->da_reg);
		dev_err(&pdev->dev, "Failed to obtain ldo: %d\n", ret);
		return ret;
	}

	atlas7_codec->ad_reg = devm_regulator_get(&pdev->dev, "ldo1");
	if (IS_ERR(atlas7_codec->ad_reg)) {
		ret = PTR_ERR(atlas7_codec->ad_reg);
		dev_err(&pdev->dev, "Failed to obtain ldo: %d\n", ret);
		return ret;
	}

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "Get clock failed.\n");
		ret = PTR_ERR(clk);
		return ret;
	}

	atlas7_codec->clk = clk;
	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = sirf_atlas7_codec_runtime_resume(&pdev->dev);
		if (ret)
			return ret;
	}

	ret = snd_soc_register_codec(&(pdev->dev),
			&soc_codec_device_sirf_atlas7_codec,
			&sirf_atlas7_codec_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "Register Audio Codec dai failed.\n");
		return ret;
	}
#ifdef CONFIG_SND_SOC_SIRF_KALIMBA_DEBUG
	debug_setup_codec_regmap(atlas7_codec->regmap);
#endif
	return 0;
}

static int sirf_atlas7_codec_driver_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&(pdev->dev));
	if (!pm_runtime_enabled(&pdev->dev))
		sirf_atlas7_codec_runtime_suspend(&pdev->dev);
	else
		pm_runtime_disable(&pdev->dev);

	return 0;
}

static const struct dev_pm_ops sirf_atlas7_codec_pm_ops = {
	SET_RUNTIME_PM_OPS(sirf_atlas7_codec_runtime_suspend,
		sirf_atlas7_codec_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(sirf_atlas7_codec_suspend,
		sirf_atlas7_codec_resume)
};

static const struct of_device_id sirf_atlas7_codec_of_match[] = {
	{ .compatible = "sirf,atlas7-codec" },
	{}
};
MODULE_DEVICE_TABLE(of, sirf_atlas7_codec_of_match);


static struct platform_driver sirf_atlas7_codec_driver = {
	.driver = {
		.name = "sirf-atlas7-codec",
		.owner = THIS_MODULE,
		.of_match_table = sirf_atlas7_codec_of_match,
		.pm = &sirf_atlas7_codec_pm_ops,
	},
	.probe = sirf_atlas7_codec_driver_probe,
	.remove = sirf_atlas7_codec_driver_remove,
};

module_platform_driver(sirf_atlas7_codec_driver);

MODULE_DESCRIPTION("SiRF atlas7 internal codec driver");
MODULE_AUTHOR("RongJun Ying <Rongjun.Ying@csr.com>");
MODULE_LICENSE("GPL v2");
