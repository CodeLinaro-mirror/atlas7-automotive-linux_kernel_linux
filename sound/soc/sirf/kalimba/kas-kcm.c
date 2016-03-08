/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
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
#include "dsp.h"
#include "kcm/kcm.h"

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
	{"AIF Playback", NULL, "IACC Codec OUT"},
	{"LIN0", NULL, "LINEIN"},
	{"MICIN0", NULL, "MICIN"},
	{"IACC Codec IN", NULL, "AIF Capture"},
};

static int kas_iacc_fixup(struct snd_soc_pcm_runtime *rtd,
			struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_RATE);

	/* The kalimba DSP will covert the FE rate to 48k, stereo */
	rate->min = rate->max = 48000;

	return 0;
}

/* Only BE DAI links are listed. FE DAI links are created on demand. */
static const struct snd_soc_dai_link kas_be_dais[] = {
	{
		.name = "IACC-Codec",
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
	.dapm_widgets = kas_audio_widgets,
	.num_dapm_widgets = ARRAY_SIZE(kas_audio_widgets),
	.dapm_routes = kas_audio_map,
	.num_dapm_routes = ARRAY_SIZE(kas_audio_map),
	.fully_routed = true,
};

static int kas_audio_probe(struct platform_device *pdev)
{
	int ret, num_links, free_links;
	struct snd_soc_dai_link *dai_link =
		kcm_get_dai_link(&num_links, &free_links);

	ret = kcm_drv_status();
	if (ret)
		return ret;

	/* Paste BE DAI links to table. Latest kernel supports adding DAI links
	 * dynamically by snd_soc_add_dai_link, this feature is not merged yet.
	 */
	BUG_ON(free_links < ARRAY_SIZE(kas_be_dais));
	memcpy(dai_link + num_links, kas_be_dais, ARRAY_SIZE(kas_be_dais) *
			sizeof(struct snd_soc_dai_link));

	kas_audio_card.dai_link = dai_link;
	kas_audio_card.num_links = num_links + ARRAY_SIZE(kas_be_dais);
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
