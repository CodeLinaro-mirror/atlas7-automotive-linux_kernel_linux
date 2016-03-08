/*
 * Copyright (c) 2014-2016, The Linux Foundation. All rights reserved.
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

#define PLAYBACK_FIX_CHANNELS		4

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
	MIC_MONO_IN,
	MIC_STEREO_IN,
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

static u32 rate_reg_value(int stream, int rate)
{
	int i;

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
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

/*
 * This codec can support some sample rate, if the DSP wants fix sample rate,
 * Use be_hw_params_fixup interface to modify the rate_min and rate_max values.
 */
static int sirf_atlas7_codec_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct sirf_atlas7_codec *atlas7_codec = dev_get_drvdata(codec->dev);
	int i;
	u32 volume_level;
	int stream = substream->stream;
	int rate = params_rate(params);

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		volume_level = atlas7_codec->playback_volume;
		/*
		 * The codec uses the fixed 4 channels.
		 * Because this codec is used by the backend.
		 * The kalimba DSP should upmix or downmix
		 * any channels to fixed 4 channels.
		 */
		for (i = 0; i < PLAYBACK_FIX_CHANNELS; i++) {
			snd_soc_update_bits(codec, dac_gain_regs[i],
					AUDIO_GAIN_MASK,
					volume_reg_values[volume_level]);
			snd_soc_write(codec, dac_sample_rate_regs[i],
					rate_reg_value(stream, rate));
		}
	} else {
		volume_level = atlas7_codec->capture_volume;
		if (atlas7_codec->input_path == MIC_MONO_IN
				|| atlas7_codec->input_path == MIC_STEREO_IN)
			snd_soc_update_bits(codec, AUDIO_ANA_ADC_CTRL2,
					AUDIO_ANA_ADC_MICAMP_GAIN_SEL_MASK,
					AUDIO_ANA_ADC_MICAMP_GAIN);
		if (atlas7_codec->input_path == MIC_STEREO_IN)
			snd_soc_update_bits(codec, AUDIO_ANA_ADC_CTRL3,
					AUDIO_ANA_ADC_MICAMP_GAIN_SEL_MASK,
					AUDIO_ANA_ADC_MICAMP_GAIN);
		snd_soc_update_bits(codec, AUDIO_ANA_ADC_CTRL0, 0xFFFF,
				input_path_val[atlas7_codec->input_path]);

		for (i = 0; i < params_channels(params); i++) {
			snd_soc_update_bits(codec, adc_gain_regs[i],
					AUDIO_GAIN_MASK,
					volume_reg_values[volume_level]);
			snd_soc_write(codec, KCODEC_ADC_A_SAMP_RATE
					+ (i * 0x20),
					rate_reg_value(stream, rate));
		}
	}
	return 0;
}

struct snd_soc_dai_ops sirf_atlas7_codec_dai_ops = {
	.hw_params = sirf_atlas7_codec_hw_params,
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
	if (atlas7_codec->input_path == MIC_MONO_IN ||
		atlas7_codec->input_path == MIC_STEREO_IN)
		snd_soc_update_bits(codec, AUDIO_ANA_ADC_CTRL2,
			AUDIO_ANA_ADC_MICAMP_GAIN_SEL_MASK,
			AUDIO_ANA_ADC_MICAMP_GAIN);
	if (atlas7_codec->input_path == MIC_STEREO_IN)
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
	SOC_VALUE_ENUM_SINGLE(AUDIO_DAC_CTRL, 4, 0xf, 2, output_mode_text,
		output_mode_val);

static const struct snd_kcontrol_new sirf_atlas7_codec_output_mode_control =
	SOC_DAPM_ENUM("Output mode", output_mode_enum);

static const char * const input_path_text[] = {"MIC0", "2MIC", "LINE0",
		"LINE1", "LINE2", "LINE3"};
static const int input_path_val[] = {0x1080, 0x10C1, 0x1850,
		0x1448, 0x1244, 0x1142};

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
	/*output widgets*/
	SND_SOC_DAPM_MUX("Output mode", SND_SOC_NOPM, 0, 0,
			&sirf_atlas7_codec_output_mode_control),
	SND_SOC_DAPM_DAC_E("DACA", NULL, KCODEC_CONFIG, 12, 0,
		dac_en_event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_DAC_E("DACB", NULL, KCODEC_CONFIG, 13, 0,
		dac_en_event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_DAC_E("DACC", NULL, KCODEC_CONFIG2, 12, 0,
		dac_en_event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_DAC_E("DACD", NULL, KCODEC_CONFIG2, 13, 0,
		dac_en_event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_AIF_IN("AIFRX", "AIF Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_OUTPUT("LOUT0"),
	SND_SOC_DAPM_OUTPUT("LOUT1"),
	SND_SOC_DAPM_OUTPUT("LOUT2"),
	SND_SOC_DAPM_OUTPUT("LOUT3"),

	/*
	 * input widgets, enable LINE_IN here,
	 *adc codec has been powered in power_on_adc()
	 */
	SND_SOC_DAPM_MUX("Input path", SND_SOC_NOPM, 0, 0,
			&sirf_atlas7_codec_input_path_control),
	SND_SOC_DAPM_AIF_OUT("AIFTX", "AIF Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_ADC_E("ADCA", NULL, KCODEC_CONFIG, 10, 0,
		adc_en_event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("ADCB", NULL, KCODEC_CONFIG, 11, 0,
		adc_en_event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_INPUT("LIN0"),
	SND_SOC_DAPM_INPUT("LIN1"),
	SND_SOC_DAPM_INPUT("LIN2"),
	SND_SOC_DAPM_INPUT("LIN3"),
	SND_SOC_DAPM_INPUT("MICIN0"),
	SND_SOC_DAPM_INPUT("MICIN1"),
};

static const struct snd_soc_dapm_route sirf_atlas7_codec_map[] = {
	/*output map*/
	{"DACA", NULL, "AIFRX"},
	{"DACB", NULL, "AIFRX"},
	{"DACC", NULL, "AIFRX"},
	{"DACD", NULL, "AIFRX"},

	{"Output mode", "Single-ended", "DACA"},
	{"Output mode", "Differential", "DACA"},
	{"Output mode", "Single-ended", "DACB"},
	{"Output mode", "Differential", "DACB"},
	{"Output mode", "Single-ended", "DACC"},
	{"Output mode", "Differential", "DACC"},
	{"Output mode", "Single-ended", "DACD"},
	{"Output mode", "Differential", "DACD"},

	{"LOUT0", NULL, "Output mode"},
	{"LOUT1", NULL, "Output mode"},
	{"LOUT2", NULL, "Output mode"},
	{"LOUT3", NULL, "Output mode"},

	/*input map*/
	{"AIFTX", NULL, "ADCA"},
	{"AIFTX", NULL, "ADCB"},

	{"ADCA", NULL, "Input path"},
	{"ADCB", NULL, "Input path"},

	{"Input path", "MIC0", "MICIN0"},
	{"Input path", "2MIC", "MICIN0"},
	{"Input path", "2MIC", "MICIN1"},
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

static int sirf_atlas7_codec_pre_init_adc(struct regmap *regmap)
{
	regmap_update_bits(regmap, ANA_PMUCTL2,
			PMUCTRL2_VBG_TRIM, PMUCTRL2_VBG_TRIM_0XF);

	/* Workaround for some registers update fail. */
	regmap_update_bits(regmap, AUDIO_CTRL_SPARE_0,
		ANA_MACRO_CLK_TEST_SEL_MASK,
		(2 << ANA_MACRO_CLK_TEST_SEL_SHIFT));
	regmap_update_bits(regmap, AUDIO_DIGMIC_CTRL,
		ANA_DIG_MIC_CLK_TEST_EN_MASK,
		(1 << ANA_DIG_MIC_CLK_TEST_EN_SHIFT));
	regmap_update_bits(regmap, AUDIO_DIGMIC_CTRL,
		ANA_DIG_MIC_CLK_TEST_EN_MASK, 0);
	regmap_update_bits(regmap, AUDIO_CTRL_SPARE_0,
		ANA_MACRO_CLK_TEST_SEL_MASK, 0);

	regmap_update_bits(regmap, AUDIO_ANA_REF_CTRL0,
		AUDIO_ANA_REF_AUDBIAS_IREF_EN |
		AUDIO_ANA_REF_AUDBIAS_VAG_RX_EN |
		AUDIO_ANA_REF_AUDBIAS_VAG_TX_EN |
		AUDIO_ANA_REF_VGA_IREF_TX_EN |
		AUDIO_ANA_REF_AUDBIAS_IREF_TRIM |
		AUDIO_ANA_REF_AUDBIAS_VAG_TSADC_EN,
		AUDIO_ANA_REF_AUDBIAS_IREF_EN |
		AUDIO_ANA_REF_AUDBIAS_VAG_RX_EN |
		AUDIO_ANA_REF_AUDBIAS_VAG_TX_EN |
		AUDIO_ANA_REF_VGA_IREF_TX_EN |
		AUDIO_ANA_REF_AUDBIAS_IREF_TRIM |
		AUDIO_ANA_REF_AUDBIAS_VAG_TSADC_EN);

	regmap_update_bits(regmap, AUDIO_REGS_CLK_CTRL,
		DAC_CLK_EN | ADC_CLK_EN,
		DAC_CLK_EN | ADC_CLK_EN);
	regmap_update_bits(regmap, AUDIO_ANA_DAC_CTRL0,
		AUDIO_ANA_DAC_VGEN_EN_MASK | AUDIO_ANA_DAC_LOUT_EN_MASK |
		AUDIO_ANA_DAC_BUF_LOUT_EN_MASK,
		(1 << AUDIO_ANA_DAC_VGEN_EN_SHIFT) |
		(0xf << AUDIO_ANA_DAC_LOUT_EN_SHIFT) |
		(0xf << AUDIO_ANA_DAC_BUF_LOUT_EN_SHIFT));

	regmap_update_bits(regmap, KCODEC_DAC_A_GAIN,
		KCODEC_DAC_GAIN_SELECT_FINE, KCODEC_DAC_GAIN_SELECT_FINE);
	regmap_update_bits(regmap, KCODEC_DAC_B_GAIN,
		KCODEC_DAC_GAIN_SELECT_FINE, KCODEC_DAC_GAIN_SELECT_FINE);
	regmap_update_bits(regmap, KCODEC_DAC_C_GAIN,
		KCODEC_DAC_GAIN_SELECT_FINE, KCODEC_DAC_GAIN_SELECT_FINE);
	regmap_update_bits(regmap, KCODEC_DAC_D_GAIN,
		KCODEC_DAC_GAIN_SELECT_FINE, KCODEC_DAC_GAIN_SELECT_FINE);

	regmap_update_bits(regmap, KCODEC_ADC_A_GAIN,
		KCODEC_ADC_GAIN_SELECT_FINE, KCODEC_ADC_GAIN_SELECT_FINE);
	regmap_update_bits(regmap, KCODEC_ADC_B_GAIN,
		KCODEC_ADC_GAIN_SELECT_FINE, KCODEC_ADC_GAIN_SELECT_FINE);

	regmap_update_bits(regmap, KCODEC_CONFIG,
		KCODEC_CONFIG_DSM_DITHER_EN_MASK, 0);
	regmap_update_bits(regmap, KCODEC_CONFIG_EXTENSION2,
		KCODEC_CONFIG_DEM_DITHER_CFG_MASK,
		(0x1f << KCODEC_CONFIG_DEM_DITHER_CFG_SHIFT));
	regmap_update_bits(regmap, KCODEC_CONFIG2,
		KCODEC_CONFIG_DSM_DITHER_EN_MASK, 0);
	regmap_update_bits(regmap, KCODEC_CONFIG2_EXTENSION2,
		KCODEC_CONFIG_DEM_DITHER_CFG_MASK,
		(0x1f << KCODEC_CONFIG_DEM_DITHER_CFG_SHIFT));

	/* DAC reset */
	regmap_update_bits(regmap, AUDIO_DAC_CTRL,
		DAC_RESET_MASK, 0);

	/* ADC CH1 */
	regmap_update_bits(regmap, AUDIO_ANA_ADC_CTRL2,
		AUDIO_ANA_ADC_CHANNEL_EN | AUDIO_ANA_ADC_CHANNEL_DITHER_EN |
		AUDIO_ANA_ADC_CHANNEL_DWA_EN,
		AUDIO_ANA_ADC_CHANNEL_EN | AUDIO_ANA_ADC_CHANNEL_DITHER_EN |
		AUDIO_ANA_ADC_CHANNEL_DWA_EN);
	/* ADC CH2 */
	regmap_update_bits(regmap, AUDIO_ANA_ADC_CTRL3,
		AUDIO_ANA_ADC_CHANNEL_EN | AUDIO_ANA_ADC_CHANNEL_DITHER_EN |
		AUDIO_ANA_ADC_CHANNEL_DWA_EN,
		AUDIO_ANA_ADC_CHANNEL_EN | AUDIO_ANA_ADC_CHANNEL_DITHER_EN |
		AUDIO_ANA_ADC_CHANNEL_DWA_EN);

	regmap_update_bits(regmap, AUDIO_ANA_ADC_CTRL2,
		AUDIO_ANA_ADC_CHANNEL_GAIN_SEL_MASK,
		(0xA << AUDIO_ANA_ADC_CHANNEL_GAIN_SEL_SHIFT));
	regmap_update_bits(regmap, AUDIO_ANA_ADC_CTRL3,
		AUDIO_ANA_ADC_CHANNEL_GAIN_SEL_MASK,
		(0xA << AUDIO_ANA_ADC_CHANNEL_GAIN_SEL_SHIFT));
	regmap_update_bits(regmap, AUDIO_ANA_ADC_CTRL3,
		AUDIO_ANA_ADC_CHANNEL_CLK_SEL_DELAY_MASK,
		(0x8 << AUDIO_ANA_ADC_CHANNEL_CLK_SEL_DELAY_SHIFT));
	regmap_update_bits(regmap, AUDIO_ANA_ADC_CTRL2,
		AUDIO_ANA_ADC_CHANNEL_CLK_SEL_DELAY_MASK,
		(0x8 << AUDIO_ANA_ADC_CHANNEL_CLK_SEL_DELAY_SHIFT));

	/* ADC reset */
	regmap_update_bits(regmap, AUDIO_ANA_CAL_CTRL0,
		AUDIO_ANA_CAL_ADC_EN, 0);
	regmap_update_bits(regmap, AUDIO_REGS_CLK_CTRL,
		AUDIO_ANA_CAL_CLK_EN, AUDIO_ANA_CAL_CLK_EN);
	regmap_update_bits(regmap, AUDIO_ANA_CAL_CTRL0,
		AUDIO_ANA_CAL_ADC_EN, AUDIO_ANA_CAL_ADC_EN);

	regmap_update_bits(regmap, AUDIO_ANA_ADC_CTRL1,
		AUDIO_ANA_ADC_CH12_CCAL_SEL_MASK,
		(6 << AUDIO_ANA_ADC_CH12_CCAL_SEL_SHIFT));
	return 0;
}

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
	/*
	 * don't call runtime enable but to call runtime_resume here,
	 * so register can be set
	 */
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = sirf_atlas7_codec_runtime_resume(&pdev->dev);
		if (ret)
			return ret;
	}

	/*
	 * to get rid of pop noise at start of record,
	 * below api need to be called when chip first poered on
	 */
	sirf_atlas7_codec_pre_init_adc(atlas7_codec->regmap);
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
MODULE_LICENSE("GPL v2");
