/*
 * CSR Altas7 kailimba audio
 *
 * Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
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
#include <linux/platform_device.h>
#include <sound/soc.h>
#include "kcm.h"
#include "dsp.h"

static const struct snd_soc_dapm_widget kas_audio_widgets[] = {
	SND_SOC_DAPM_HP("Headphones", NULL),
	SND_SOC_DAPM_LINE("LINEIN", NULL),
	SND_SOC_DAPM_MIC("MICIN", NULL),
};

static const struct snd_soc_dapm_route kas_audio_map[] = {
	{"Headphones", NULL, "LOUT0"},
	{"Headphones", NULL, "LOUT1"},
	{"Headphones", NULL, "LOUT2"},
	{"Headphones", NULL, "LOUT3"},
	{"AIF Playback", NULL, "Codec OUT"},
	{"LIN0", NULL, "LINEIN"},
	{"MICIN0", NULL, "MICIN"},
	{"Codec IN", NULL, "AIF Capture"},
};

static int kas_iacc_fixup(struct snd_soc_pcm_runtime *rtd,
			struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
						SNDRV_PCM_HW_PARAM_CHANNELS);

	/* The kalimba DSP will covert the FE rate to 48k, stereo */
	rate->min = rate->max = 48000;
	if (enable_2mic_cvc)
		channels->min = channels->max = 2;

	return 0;
}

static struct snd_soc_dai_link kas_audio_dais[] = {
	/* Front End DAI links */
	{
		.name = "Music",
		.stream_name = "Music Playback",
		.cpu_dai_name = "Music Pin",
		.platform_name = "kas-pcm-audio",
		.dynamic = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
	},
	{
		.name = "Navigation",
		.stream_name = "Navigation Playback",
		.cpu_dai_name = "Navigation Pin",
		.platform_name = "kas-pcm-audio",
		.dynamic = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
	},
	{
		.name = "Alarm",
		.stream_name = "Alarm Playback",
		.cpu_dai_name = "Alarm Pin",
		.platform_name = "kas-pcm-audio",
		.dynamic = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
	},
	{
		.name = "A2DP",
		.stream_name = "A2DP Playback",
		.cpu_dai_name = "A2DP Pin",
		.platform_name = "kas-pcm-audio",
		.dynamic = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
	},
	{
		.name = "Voicecall-bt-to-iacc",
		.stream_name = "Voicecall-bt-to-iacc",
		.cpu_dai_name = "Voicecall-bt-to-iacc Pin",
		.platform_name = "kas-pcm-audio",
		.dynamic = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
	},
	{
		.name = "Voicecall-playback",
		.stream_name = "Voicecall-playback",
		.cpu_dai_name = "Voicecall-playback Pin",
		.platform_name = "kas-pcm-audio",
		.dynamic = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
	},
	{
		.name = "Iacc-loopback-playback",
		.stream_name = "Iacc-loopback-playback",
		.cpu_dai_name = "Iacc-loopback-playback Pin",
		.platform_name = "kas-pcm-audio",
		.dynamic = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
	},
	{
		.name = "I2S-to-iacc-loopback",
		.stream_name = "I2S-to-iacc-loopback",
		.cpu_dai_name = "I2S-to-iacc-loopback Pin",
		.platform_name = "kas-pcm-audio",
		.dynamic = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
	},
	{
		.name = "Analog Capture",
		.stream_name = "Analog Capture",
		.cpu_dai_name = "Capture Pin",
		.platform_name = "kas-pcm-audio",
		.dynamic = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_capture = 1,
	},
	{
		.name = "Voicecall-iacc-to-bt",
		.stream_name = "Voicecall-iacc-to-bt",
		.cpu_dai_name = "Voicecall-iacc-to-bt Pin",
		.platform_name = "kas-pcm-audio",
		.dynamic = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_capture = 1,
	},
	{
		.name = "Voicecall-capture",
		.stream_name = "Voicecall-capture",
		.cpu_dai_name = "Voicecall-capture Pin",
		.platform_name = "kas-pcm-audio",
		.dynamic = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_capture = 1,
	},
	{
		.name = "Iacc-loopback-capture",
		.stream_name = "Iacc-loopback-capture",
		.cpu_dai_name = "Iacc-loopback-capture Pin",
		.platform_name = "kas-pcm-audio",
		.dynamic = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_capture = 1,
	},
	{
		.name = "USP0",
		.stream_name = "USP0 Playback",
		.cpu_dai_name = "USP0 Pin",
		.platform_name = "kas-pcm-audio",
		.dynamic = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
	},
	{
		.name = "USP1",
		.stream_name = "USP1 Playback",
		.cpu_dai_name = "USP1 Pin",
		.platform_name = "kas-pcm-audio",
		.dynamic = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
	},
	{
		.name = "USP2",
		.stream_name = "USP2 Playback",
		.cpu_dai_name = "USP2 Pin",
		.platform_name = "kas-pcm-audio",
		.dynamic = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
	},

	/* Back End DAI links */
	{
		/* IACC - Codec */
		.name = "IACC Codec",
		.be_id = 0,
		.cpu_dai_name = "snd-soc-dummy-dai",
		.platform_name = "snd-soc-dummy",
		.no_pcm = 1,
		.codec_name = "10e30000.atlas7_codec",
		.codec_dai_name = "atlas7-codec-hifi",
		.be_hw_params_fixup = kas_iacc_fixup,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
};

static struct snd_soc_card kas_audio_card = {
	.name = "kas-audio-card",
	.owner = THIS_MODULE,
	.dai_link = kas_audio_dais,
	.num_links = ARRAY_SIZE(kas_audio_dais),
	.dapm_widgets = kas_audio_widgets,
	.num_dapm_widgets = ARRAY_SIZE(kas_audio_widgets),
	.dapm_routes = kas_audio_map,
	.num_dapm_routes = ARRAY_SIZE(kas_audio_map),
	.fully_routed = true,
};

static int kas_audio_probe(struct platform_device *pdev)
{
	kas_audio_card.dev = &pdev->dev;
	return devm_snd_soc_register_card(&pdev->dev, &kas_audio_card);
}

static const struct of_device_id kas_audio_match[] = {
	{.compatible = "csr,kas-audio", },
	{},
};
static struct platform_driver kas_audio_driver = {
	.probe = kas_audio_probe,
	.driver = {
		.name = "kas-audio",
		.of_match_table = kas_audio_match,
	},
};
module_platform_driver(kas_audio_driver);

/* Module information */
MODULE_DESCRIPTION("Kalimba audio driver for SiRF A7DA");
MODULE_LICENSE("GPL v2");
