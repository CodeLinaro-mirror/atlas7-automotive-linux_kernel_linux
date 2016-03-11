/*
 * CSR bluetooth chip audio codec
 *
 * Copyright (c) 2013, 2016, The Linux Foundation. All rights reserved.
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

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>

static int csr_bt_codec_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int ret;

	if (cpu_dai->driver->ops->set_fmt) {
		ret = cpu_dai->driver->ops->set_fmt(cpu_dai,
				SND_SOC_DAIFMT_CBM_CFM);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static struct snd_soc_codec_driver soc_codec_device_csr_bt_codec = {
};

struct snd_soc_dai_ops csr_bt_codec_dai_ops = {
	.hw_params = csr_bt_codec_hw_params,
};

struct snd_soc_dai_driver csr_bt_codec_dai = {
	.name = "csr-bluetooth-codec",
	.playback = {
		.stream_name = "Audio Playback",
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
		.stream_name = "Audio Capture",
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
	.ops = &csr_bt_codec_dai_ops,
};
EXPORT_SYMBOL_GPL(csr_bt_codec_dai);

static int csr_bt_codec_probe(struct platform_device *pdev)
{
	return snd_soc_register_codec(&(pdev->dev),
			&soc_codec_device_csr_bt_codec,
			&csr_bt_codec_dai, 1);

}

static int csr_bt_codec_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&(pdev->dev));
	return 0;
}
static const struct of_device_id csr_bt_codec_of_match[] = {
	{ .compatible = "csr,bluetooth", },
	{}
};
MODULE_DEVICE_TABLE(of, csr_bt_codec_of_match);

static struct platform_driver csr_bt_codec_driver = {
	.driver = {
		.name = "csr-bluetooth-codec",
		.owner = THIS_MODULE,
		.of_match_table = csr_bt_codec_of_match,
	},
	.probe = csr_bt_codec_probe,
	.remove = csr_bt_codec_remove,
};

module_platform_driver(csr_bt_codec_driver);

MODULE_DESCRIPTION("CSR bluetooth codec driver");
MODULE_LICENSE("GPL v2");
