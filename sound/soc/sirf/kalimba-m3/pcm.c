/*
 * Copyright (c) [2016] The Linux Foundation. All rights reserved.
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

#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "audio-protocol.h"

struct kas_pcm_data {
	struct snd_pcm_substream *substream;
	u32 pos;
	snd_pcm_uframes_t last_appl_ptr;
};

#define KAS_PCM_COUNT	15
struct kas_pcm_data pcm_data[KAS_PCM_COUNT];

static const struct snd_pcm_hardware kas_pcm_hardware = {
	.info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_PAUSE |
		SNDRV_PCM_INFO_RESUME |
		SNDRV_PCM_INFO_BLOCK_TRANSFER,
	.formats = SNDRV_PCM_FMTBIT_S16_LE |
		SNDRV_PCM_FMTBIT_S24_LE |
		SNDRV_PCM_FMTBIT_S32_LE,
	.period_bytes_min	= 32,
	.period_bytes_max	= 256 * 1024,
	.periods_min		= 2,
	.periods_max		= 128,
	.buffer_bytes_max	= 512 * 1024, /* 512 kbytes */
};

#define KAS_RATES		(SNDRV_PCM_RATE_CONTINUOUS | \
				SNDRV_PCM_RATE_8000_192000)
#define KAS_FORMATS		(SNDRV_PCM_FMTBIT_S16_LE | \
				SNDRV_PCM_FMTBIT_S24_LE)

int kas_pcm_notify(u32 stream, u32 pos)
{
	pcm_data[stream].pos = pos;
	snd_pcm_period_elapsed(pcm_data[stream].substream);

	return 0;
}

static int kas_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;

	pcm_data[rtd->cpu_dai->id].substream = substream;

	snd_soc_set_runtime_hwparams(substream, &kas_pcm_hardware);
	return snd_pcm_hw_constraint_integer(substream->runtime,
		SNDRV_PCM_HW_PARAM_PERIODS);
}

static int kas_pcm_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	int ret;

	pcm_data[rtd->cpu_dai->id].pos = 0;
	pcm_data[rtd->cpu_dai->id].last_appl_ptr = 0;

	ret = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));
	if (ret < 0) {
		dev_err(rtd->dev, "allocate %d bytes for PCM failed: %d\n",
			params_buffer_bytes(params), ret);
		return ret;
	}

	return 0;
}

static int kas_pcm_hw_free(struct snd_pcm_substream *substream)
{
	snd_pcm_lib_free_pages(substream);
	return 0;
}

static int kas_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned int rate = runtime->rate;
	unsigned int channels = runtime->channels;
	u32 buff_addr = runtime->dma_addr;
	u32 buff_bytes = snd_pcm_lib_buffer_bytes(substream);
	u32 period_bytes = snd_pcm_lib_period_bytes(substream);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		kas_start_stream(rtd->cpu_dai->id, rate, channels, buff_addr,
			buff_bytes, period_bytes);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		kas_stop_stream(rtd->cpu_dai->id);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static snd_pcm_uframes_t kas_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;

	return bytes_to_frames(substream->runtime,
		pcm_data[rtd->cpu_dai->id].pos);
}

static int kas_pcm_ack(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	u32 pos;

	if (runtime->status->state != SNDRV_PCM_STATE_RUNNING)
		return 0;
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		return 0;

	if (runtime->control->appl_ptr -
		pcm_data[rtd->cpu_dai->id].last_appl_ptr >=
		runtime->period_size)
		pcm_data[rtd->cpu_dai->id].last_appl_ptr =
			runtime->control->appl_ptr;
	else
		return 0;

	pos = frames_to_bytes(runtime,
		pcm_data->last_appl_ptr % runtime->buffer_size) / 4;
	kas_send_data_produced(rtd->cpu_dai->id, pos);
	return 0;
}

static struct snd_pcm_ops kas_pcm_ops = {
	.open = kas_pcm_open,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = kas_pcm_hw_params,
	.hw_free = kas_pcm_hw_free,
	.trigger = kas_pcm_trigger,
	.pointer = kas_pcm_pointer,
	.ack = kas_pcm_ack,
};

static int kas_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_pcm *pcm = rtd->pcm;
	struct snd_card *card = rtd->card->snd_card;
	int ret = 0;

	ret = dma_coerce_mask_and_coherent(card->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream ||
			pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream) {
		ret = snd_pcm_lib_preallocate_pages_for_all(pcm,
				SNDRV_DMA_TYPE_DEV_IRAM,
				rtd->card->snd_card->dev,
				kas_pcm_hardware.buffer_bytes_max,
				kas_pcm_hardware.buffer_bytes_max);
		if (ret) {
			dev_err(rtd->dev, "dma buffer allocation failed %d\n",
					ret);
			return ret;
		}
	}

	return ret;
}

static void kas_pcm_free(struct snd_pcm *pcm)
{
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static struct snd_soc_platform_driver kas_soc_platform = {
	.ops = &kas_pcm_ops,
	.pcm_new = kas_pcm_new,
	.pcm_free = kas_pcm_free,
};

static struct snd_soc_dai_driver kas_dais[] = {
	{
		.name = "Music Pin",
		.playback = {
			.stream_name = "Music Playback",
			.channels_min = 1,
			.channels_max = 4,
			.rates = KAS_RATES,
			.formats = KAS_FORMATS,
		},
	},
	{
		.name = "Navigation Pin",
		.playback = {
			.stream_name = "Navigation Playback",
			.channels_min = 1,
			.channels_max = 4,
			.rates = KAS_RATES,
			.formats = KAS_FORMATS,
		},
	},
	{
		.name = "Alarm Pin",
		.playback = {
			.stream_name = "Alarm Playback",
			.channels_min = 1,
			.channels_max = 4,
			.rates = KAS_RATES,
			.formats = KAS_FORMATS,
		},
	},
	{
		.name = "A2DP Pin",
		.playback = {
			.stream_name = "A2DP Playback",
			.channels_min = 1,
			.channels_max = 4,
			.rates = KAS_RATES,
			.formats = KAS_FORMATS,
		},
	},
	{
		.name = "Voicecall-bt-to-iacc Pin",
		.playback = {
			.stream_name = "Voicecall-bt-to-iacc",
			.channels_min = 1,
			.channels_max = 4,
			.rates = KAS_RATES,
			.formats = KAS_FORMATS,
		},
	},
	{
		.name = "Voicecall-playback Pin",
		.playback = {
			.stream_name = "Voicecall-playback",
			.channels_min = 1,
			.channels_max = 1,
			.rates = KAS_RATES,
			.formats = KAS_FORMATS,
		},
	},
	{
		.name = "Iacc-loopback-playback Pin",
		.playback = {
			.stream_name = "Iacc-loopback-playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = KAS_RATES,
			.formats = KAS_FORMATS,
		},
	},
	{
		.name = "I2S-to-iacc-loopback Pin",
		.playback = {
			.stream_name = "I2S-to-iacc-loopback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = KAS_RATES,
			.formats = KAS_FORMATS,
		},
	},
	{
		.name = "Capture Pin",
		.capture = {
			.stream_name = "Analog Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = KAS_RATES,
			.formats = KAS_FORMATS,
		},
	},
	{
		.name = "Voicecall-iacc-to-bt Pin",
		.capture = {
			.stream_name = "Voicecall-iacc-to-bt",
			.channels_min = 1,
			.channels_max = 1,
			.rates = KAS_RATES,
			.formats = KAS_FORMATS,
		},
	},
	{
		.name = "Voicecall-capture Pin",
		.capture = {
			.stream_name = "Voicecall-capture",
			.channels_min = 1,
			.channels_max = 1,
			.rates = KAS_RATES,
			.formats = KAS_FORMATS,
		},
	},
	{
		.name = "Iacc-loopback-capture Pin",
		.capture = {
			.stream_name = "Iacc-loopback-capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = KAS_RATES,
			.formats = KAS_FORMATS,
		},
	},
	{
		.name = "USP0 Pin",
		.playback = {
			.stream_name = "USP0 Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = KAS_RATES,
			.formats = KAS_FORMATS,
		},
	},
	{
		.name = "USP1 Pin",
		.playback = {
			.stream_name = "USP1 Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = KAS_RATES,
			.formats = KAS_FORMATS,
		},
	},
	{
		.name = "USP2 Pin",
		.playback = {
			.stream_name = "USP2 Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = KAS_RATES,
			.formats = KAS_FORMATS,
		},
	}

};

static const struct snd_soc_dapm_widget widgets[] = {
	/* Backend DAIs  */
	SND_SOC_DAPM_AIF_IN("Codec IN", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("Codec OUT", NULL, 0, SND_SOC_NOPM, 0, 0),
	/* Global Playback Mixer */
	SND_SOC_DAPM_MIXER("Playback VMixer", SND_SOC_NOPM, 0, 0, NULL, 0),
};

static const struct snd_soc_dapm_route graph[] = {
	/* Playback Mixer */
	{"Playback VMixer", NULL, "Music Playback"},
	{"Playback VMixer", NULL, "Navigation Playback"},
	{"Playback VMixer", NULL, "Alarm Playback"},
	{"Playback VMixer", NULL, "A2DP Playback"},
	{"Playback VMixer", NULL, "Voicecall-bt-to-iacc"},
	{"Playback VMixer", NULL, "Voicecall-playback"},
	{"Playback VMixer", NULL, "Iacc-loopback-playback"},
	{"Playback VMixer", NULL, "I2S-to-iacc-loopback"},
	{"Codec OUT", NULL, "Playback VMixer"},
	{"Analog Capture", NULL, "Codec IN"},
	{"Voicecall-iacc-to-bt", NULL, "Codec IN"},
	{"Voicecall-capture", NULL, "Codec IN"},
	{"Iacc-loopback-capture", NULL, "Codec IN"},
	{"Playback VMixer", NULL, "USP0 Playback"},
	{"Playback VMixer", NULL, "USP1 Playback"},
	{"Playback VMixer", NULL, "USP2 Playback"},

};

static const struct snd_soc_component_driver kas_dai_component = {
	.name = "kas-dai",
	.dapm_widgets = widgets,
	.num_dapm_widgets = ARRAY_SIZE(widgets),
	.dapm_routes = graph,
	.num_dapm_routes = ARRAY_SIZE(graph),
};

static int kas_pcm_dev_probe(struct platform_device *pdev)
{
	int ret;

	audio_protocol_init();
	ret = devm_snd_soc_register_platform(&pdev->dev, &kas_soc_platform);
	if (ret < 0)
		return ret;

	ret = devm_snd_soc_register_component(&pdev->dev, &kas_dai_component,
			kas_dais, ARRAY_SIZE(kas_dais));
	return ret;
}

static const struct of_device_id kas_pcm_of_match[] = {
	{ .compatible = "csr,kas-pcm", },
	{}
};
MODULE_DEVICE_TABLE(of, kas_pcm_of_match);

static struct platform_driver kas_pcm_driver = {
	.driver = {
		.name = "kas-pcm-audio",
		.owner = THIS_MODULE,
		.of_match_table = kas_pcm_of_match,
	},
	.probe = kas_pcm_dev_probe,
};
module_platform_driver(kas_pcm_driver);

MODULE_DESCRIPTION("SiRF Kalimba pcm audio driver");
