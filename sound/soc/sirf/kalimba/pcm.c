/*
 * kailimba audio system PCM drive
 *
 * Copyright (c) 2015 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "iacc.h"
#include "ipc.h"
#include "kcm.h"

struct kas_pcm_data {
	struct snd_pcm_substream *substream;
	u16 kalimba_notify_ep_id;
	struct endpoint_handle *sw_ep_handle;
	struct endpoint_handle *hw_ep_handle;
	u32 sw_ep_handle_phy_addr;
	u32 hw_ep_handle_phy_addr;
	void *hw_ep_buff;
	u32 hw_ep_buff_phy_addr;
	u32 pos;
	snd_pcm_uframes_t last_appl_ptr;
	void *action_id;
	struct components_chain *components_chain;
	struct component *data_produced_ack_component;
};

struct kas_priv_data {
	struct ipc_data *ipc_data;
	struct kas_pcm_data pcm[2];
};

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

static int kas_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct kas_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct kas_pcm_data *pcm_data = &pdata->pcm[substream->stream];

	pcm_data->substream = substream;
	pcm_data->components_chain =
		get_components_chain(rtd->dai_link->stream_name);
	snd_soc_set_runtime_hwparams(substream, &kas_pcm_hardware);
	return snd_pcm_hw_constraint_integer(substream->runtime,
		SNDRV_PCM_HW_PARAM_PERIODS);
}

static void kas_data_notify(u32 message, void *priv_data, u32 *message_data)
{
	struct kas_pcm_data *pcm_data = (struct kas_pcm_data *)priv_data;

	if (message_data[0] == pcm_data->kalimba_notify_ep_id) {
		pcm_data->pos = (message_data[1] << 16 | message_data[2]) * 4;
		snd_pcm_period_elapsed(pcm_data->substream);
	}
}

static int kas_pcm_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct kas_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct kas_pcm_data *pcm_data = &pdata->pcm[substream->stream];
	int playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	struct snd_dma_buffer *dmab;
	int ret;

	pdata->ipc_data = ipc_get_data();
	pcm_data->pos = 0;
	pcm_data->last_appl_ptr = 0;

	pcm_data->data_produced_ack_component =
		get_data_produced_ack_component(pcm_data->components_chain);

	ret = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));
	if (ret < 0) {
		dev_err(rtd->dev, "allocate %d bytes for PCM failed: %d\n",
			params_buffer_bytes(params), ret);
		return ret;
	}

	dmab = snd_pcm_get_dma_buf(substream);

	pcm_data->hw_ep_handle->buff_length = 256 *
		params_channels(params) / 4;
	pcm_data->sw_ep_handle->buff_addr = dmab->addr;
	pcm_data->sw_ep_handle->buff_length =
		params_buffer_bytes(params) / 4;

	set_external_param(pcm_data->components_chain,
			"hw_ep_channles", params_channels(params));
	set_external_param(pcm_data->components_chain,
			"hw_ep_handle_addr", pcm_data->hw_ep_handle_phy_addr);
	set_external_param(pcm_data->components_chain,
			"hw_ep_conf_audio_sample_rate", params_rate(params));
	set_external_param(pcm_data->components_chain,
			"hw_ep_conf_audio_data_format", 0);
	set_external_param(pcm_data->components_chain,
			"hw_ep_conf_dram_packing_format", 2);
	set_external_param(pcm_data->components_chain,
			"hw_ep_conf_interleaving_mode", 1);
	set_external_param(pcm_data->components_chain,
			"hw_ep_conf_clock_master", 1);
	set_external_param(pcm_data->components_chain,
			"sw_ep_channles", params_channels(params));
	set_external_param(pcm_data->components_chain,
			"sw_ep_handle_addr", pcm_data->sw_ep_handle_phy_addr);
	set_external_param(pcm_data->components_chain,
			"sw_ep_conf_audio_sample_rate", params_rate(params));
	set_external_param(pcm_data->components_chain,
			"sw_ep_conf_audio_data_format", 0);
	set_external_param(pcm_data->components_chain,
			"sw_ep_conf_dram_packing_format", 2);
	set_external_param(pcm_data->components_chain,
			"sw_ep_conf_interleaving_mode", 1);
	set_external_param(pcm_data->components_chain,
			"sw_ep_conf_clock_master", 1);
	set_external_param(pcm_data->components_chain,
			"sw_ep_period_size", params_period_bytes(params) / 4);

	ret = execute_components_chain(pcm_data->components_chain,
			EXEC_PHASE_HW_PARAMS);
	if (ret < 0)
		goto failed;

	pcm_data->kalimba_notify_ep_id =
		get_notify_ep_id(pcm_data->components_chain);
	if (playback)
		pcm_data->action_id = request_ipc(
			pdata->ipc_data, DATA_CONSUMED, kas_data_notify,
			pcm_data);
	else
		pcm_data->action_id = request_ipc(
			pdata->ipc_data, DATA_PRODUCED, kas_data_notify,
			pcm_data);
	return 0;
failed:
	snd_pcm_lib_free_pages(substream);
	return ret;
}

static int kas_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct kas_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct kas_pcm_data *pcm_data = &pdata->pcm[substream->stream];

	execute_components_chain(pcm_data->components_chain,
			EXEC_PHASE_HW_FREE);
	free_ipc(pdata->ipc_data, pcm_data->action_id);
	snd_pcm_lib_free_pages(substream);
	return 0;
}

static int kas_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct kas_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct kas_pcm_data *pcm_data = &pdata->pcm[substream->stream];
	int playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	int ret;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		iacc_start(playback, substream->runtime->channels,
				pcm_data->hw_ep_buff_phy_addr, 256);
		ret = execute_components_chain(pcm_data->components_chain,
			EXEC_PHASE_TRIGGER_START);
		if (ret < 0)
			return ret;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		execute_components_chain(pcm_data->components_chain,
			EXEC_PHASE_TRIGGER_STOP);
		iacc_stop(playback);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static snd_pcm_uframes_t kas_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct kas_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct kas_pcm_data *pcm_data = &pdata->pcm[substream->stream];

	return bytes_to_frames(substream->runtime, pcm_data->pos);
}

static int kas_pcm_ack(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct kas_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct kas_pcm_data *pcm_data = &pdata->pcm[substream->stream];

	if (runtime->status->state != SNDRV_PCM_STATE_RUNNING)
		return 0;

	if (pcm_data->data_produced_ack_component == NULL)
		return 0;

	if (runtime->control->appl_ptr - pcm_data->last_appl_ptr >=
		runtime->period_size)
		pcm_data->last_appl_ptr = runtime->control->appl_ptr;
	else
		return 0;

	pcm_data->sw_ep_handle->write_pointer =	frames_to_bytes(runtime,
		pcm_data->last_appl_ptr % runtime->buffer_size) / 4;
	execute_component(pcm_data->data_produced_ack_component);

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
	struct snd_soc_platform *platform = rtd->platform;
	struct device *dev = platform->dev;
	struct kas_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct kas_pcm_data *pcm_data;
	struct snd_pcm_substream *substream;
	int ret = 0;
	int stream;

	ret = dma_coerce_mask_and_coherent(card->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;
	/* Enable PCM operations are in non-atomic context */
	pcm->nonatomic = true;

	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream ||
			pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream) {
		ret = snd_pcm_lib_preallocate_pages_for_all(pcm,
				SNDRV_DMA_TYPE_DEV_IRAM,
				dev,
				kas_pcm_hardware.buffer_bytes_max,
				kas_pcm_hardware.buffer_bytes_max);
		if (ret) {
			dev_err(rtd->dev, "dma buffer allocation failed %d\n",
					ret);
			return ret;
		}
	}

	for (stream = 0; stream < 2; stream++) {
		substream = pcm->streams[stream].substream;
		if (!substream)
			continue;
		pcm_data = &pdata->pcm[substream->stream];
		pcm_data->sw_ep_handle = dma_alloc_coherent(rtd->platform->dev,
				sizeof(struct endpoint_handle),
				&pcm_data->sw_ep_handle_phy_addr, GFP_KERNEL);
		pcm_data->hw_ep_handle = dma_alloc_coherent(rtd->platform->dev,
				sizeof(struct endpoint_handle),
				&pcm_data->hw_ep_handle_phy_addr, GFP_KERNEL);
		pcm_data->hw_ep_buff = dma_alloc_coherent(rtd->platform->dev,
				1024, &pcm_data->hw_ep_buff_phy_addr,
				GFP_KERNEL);
		memset(pcm_data->hw_ep_buff, 0, 1024);
		pcm_data->hw_ep_handle->buff_addr =
			pcm_data->hw_ep_buff_phy_addr;
	}

	return ret;
}

static void kas_pcm_free(struct snd_pcm *pcm)
{
	struct snd_pcm_substream *substream;
	struct snd_soc_pcm_runtime *rtd;
	struct kas_priv_data *pdata;
	struct kas_pcm_data *pcm_data;
	int stream;

	for (stream = 0; stream < 2; stream++) {
		substream = pcm->streams[stream].substream;
		if (!substream)
			continue;
		rtd = substream->private_data;
		pdata = snd_soc_platform_get_drvdata(rtd->platform);
		pcm_data = &pdata->pcm[substream->stream];
		dma_free_coherent(rtd->platform->dev,
				sizeof(struct endpoint_handle),
				pcm_data->sw_ep_handle,
				pcm_data->sw_ep_handle_phy_addr);
		dma_free_coherent(rtd->platform->dev,
				sizeof(struct endpoint_handle),
				pcm_data->hw_ep_handle,
				pcm_data->hw_ep_handle_phy_addr);
		dma_free_coherent(rtd->platform->dev, 1024,
				pcm_data->hw_ep_buff,
				pcm_data->hw_ep_buff_phy_addr);
	}
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static int kas_pcm_probe(struct snd_soc_platform *platform)
{
	struct kas_priv_data *priv_data;

	priv_data = devm_kzalloc(platform->dev, sizeof(*priv_data), GFP_KERNEL);
	if (priv_data == NULL)
		return -ENOMEM;

	snd_soc_platform_set_drvdata(platform, priv_data);
	kcm_init();
	return 0;
}

static struct snd_soc_platform_driver kas_soc_platform = {
	.probe = kas_pcm_probe,
	.ops = &kas_pcm_ops,
	.pcm_new = kas_pcm_new,
	.pcm_free = kas_pcm_free,
};

#define KAS_RATES		SNDRV_PCM_RATE_8000_192000
#define KAS_FORMATS		(SNDRV_PCM_FMTBIT_S16_LE | \
				SNDRV_PCM_FMTBIT_S24_LE)

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
		.name = "Capture Pin",
		.capture = {
			.stream_name = "Analog Capture",
			.channels_min = 1,
			.channels_max = 1,
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
	{"Codec OUT", NULL, "Playback VMixer"},
	{"Analog Capture", NULL, "Codec IN"},
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
MODULE_AUTHOR("RongJun Ying <Rongjun.Ying@csr.com>");
