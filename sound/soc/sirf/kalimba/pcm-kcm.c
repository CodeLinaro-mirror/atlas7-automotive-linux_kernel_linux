/*
 * SiRF Kalimba pcm audio driver
 *
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

#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "dsp.h"
#include "iacc.h"
#include "usp-pcm.h"
#include "kcm/kcm.h"
#include "kcm/kasobj.h"

static int _cpu_dai_cnt;

struct kas_pcm_data {
	struct snd_pcm_substream *substream;
	u16 kalimba_notify_ep_id;
	struct endpoint_handle *sw_ep_handle;
	u32 sw_ep_handle_phy_addr;
	u32 pos;
	snd_pcm_uframes_t last_appl_ptr;
	void *action_id;
	bool kas_started;
	const struct kasobj_fe *fe;
	struct kcm_chain *chain;
	struct mutex pcm_free_lock;
};

struct kas_priv_data {
	struct kas_pcm_data (*pcm)[2];	/* Clockwise rule */
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

static void kas_pcm_data_produced(u16 ep_id)
{
	kcm_lock();
	kalimba_data_produced(ep_id);
	kcm_unlock();
}

static void kas_pcm_set_clock_master(const u16 *ep_id, int ep_cnt,
		int clock_master)
{
	int i;
	u16 resp[64];

	kcm_lock();
	for (i = 0; i < ep_cnt; i++) {
		kalimba_config_endpoint(ep_id[i], ENDPOINT_CONF_CLOCK_MASTER,
				clock_master, resp);
	}
	kcm_unlock();
}

static int kas_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct kas_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct kas_pcm_data *pcm_data =
		&pdata->pcm[rtd->cpu_dai->id][substream->stream];

	BUG_ON(!pcm_data->fe);

	pcm_data->substream = substream;
	snd_soc_set_runtime_hwparams(substream, &kas_pcm_hardware);
	return snd_pcm_hw_constraint_integer(substream->runtime,
		SNDRV_PCM_HW_PARAM_PERIODS);
}

static int kas_data_notify(u16 message, void *priv_data, u16 *message_data)
{
	int ret;
	struct kas_pcm_data *pcm_data = (struct kas_pcm_data *)priv_data;

	/* Prevent stream from being freed by kas_pcm_hw_free() */
	mutex_lock(&pcm_data->pcm_free_lock);

	if (pcm_data->kas_started &&
			message_data[0] == pcm_data->kalimba_notify_ep_id) {
		pcm_data->pos = (message_data[1] << 16 | message_data[2]) * 4;
		snd_pcm_period_elapsed(pcm_data->substream);
		ret = ACTION_HANDLED;
	} else {
		ret = ACTION_NONE;
	}

	mutex_unlock(&pcm_data->pcm_free_lock);
	return ret;
}

static int kas_pcm_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct kas_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct kas_pcm_data *pcm_data =
		&pdata->pcm[rtd->cpu_dai->id][substream->stream];
	int playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	int is_internal = pcm_data->fe->db->internal;
	struct kasobj_param kasparam;
	int ret;

	/* Find chain, check collision */
	pcm_data->chain = kcm_prepare_chain(pcm_data->fe,
			playback, params_channels(params));
	if (IS_ERR(pcm_data->chain))
		return PTR_ERR(pcm_data->chain);

	pcm_data->pos = 0;
	pcm_data->last_appl_ptr = 0;

	ret = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));
	if (ret < 0) {
		dev_err(rtd->dev, "allocate %d bytes for PCM failed: %d\n",
			params_buffer_bytes(params), ret);
		goto err1;
	}

	if (!is_internal) {
		struct snd_dma_buffer *dmab = snd_pcm_get_dma_buf(substream);

		pcm_data->sw_ep_handle->buff_addr = dmab->addr;
		pcm_data->sw_ep_handle->buff_length =
			params_buffer_bytes(params) / 4;
	}

	kasparam.rate = params_rate(params);
	kasparam.channels = params_channels(params);
	kasparam.period_size = params_period_bytes(params) / 4;
	kasparam.format = params_format(params);
	kasparam.ep_handle_pa = pcm_data->sw_ep_handle_phy_addr;

	/* Call get() of all links and objects in this chain */
	ret = kcm_get_chain(pcm_data->chain, &kasparam);
	if (ret)
		goto err2;

	if (!is_internal) {
		if (pcm_data->fe->ep_id[0] == KCM_INVALID_EP_ID) {
			dev_err(rtd->dev, "WTF!\n");
			goto err2;
		}

		pcm_data->kalimba_notify_ep_id = pcm_data->fe->ep_id[0];

		if (playback)
			pcm_data->action_id = register_kalimba_msg_action(
				DATA_CONSUMED, kas_data_notify, pcm_data);
		else
			pcm_data->action_id = register_kalimba_msg_action(
				DATA_PRODUCED, kas_data_notify, pcm_data);
	}

	pcm_data->kas_started = true;
	return 0;

err2:
	snd_pcm_lib_free_pages(substream);
err1:
	kcm_unprepare_chain(pcm_data->chain);
	pcm_data->chain = NULL;
	return ret;
}

static int kas_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct kas_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct kas_pcm_data *pcm_data =
		&pdata->pcm[rtd->cpu_dai->id][substream->stream];

	if (!pcm_data->kas_started)
		return 0;

	if (!(pcm_data->fe->db->internal))
		unregister_kalimba_msg_action(pcm_data->action_id);

	/* Wait if kas_data_notify() is running */
	mutex_lock(&pcm_data->pcm_free_lock);

	pcm_data->kas_started = false;

	kcm_put_chain(pcm_data->chain);

	snd_pcm_lib_free_pages(substream);

	mutex_unlock(&pcm_data->pcm_free_lock);
	return 0;
}

static int kas_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct kas_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct kas_pcm_data *pcm_data =
		&pdata->pcm[rtd->cpu_dai->id][substream->stream];
	int playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;

	/* TODO: start/stop of operators and audio controllers are not paired.
	 * Maybe caused by blocking IPC mechanism. This weird behaviour is to
	 * introduce continuous pain, such as xrun bugs found before.
	 */
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		/* Weird code: set endpoints' clock_master attribute */
		if (playback && !pcm_data->fe->db->internal)
			kas_pcm_set_clock_master(pcm_data->fe->ep_id,
					pcm_data->fe->ep_cnt,
					!!atomic_read(&substream->mmap_count));

		kcm_start_chain(pcm_data->chain);

		if (playback && !pcm_data->fe->db->internal)
			kas_pcm_data_produced(pcm_data->kalimba_notify_ep_id);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		kcm_stop_chain(pcm_data->chain);
		pcm_data->pos = 0;
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
	struct kas_pcm_data *pcm_data =
		&pdata->pcm[rtd->cpu_dai->id][substream->stream];

	return bytes_to_frames(substream->runtime, pcm_data->pos);
}

static int kas_pcm_ack(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct kas_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct kas_pcm_data *pcm_data =
		&pdata->pcm[rtd->cpu_dai->id][substream->stream];

	if (runtime->status->state != SNDRV_PCM_STATE_RUNNING)
		return 0;
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE ||
			pcm_data->fe->db->internal)
		return 0;

	if (runtime->control->appl_ptr - pcm_data->last_appl_ptr >=
		runtime->period_size)
		pcm_data->last_appl_ptr = runtime->control->appl_ptr;
	else
		return 0;

	pcm_data->sw_ep_handle->write_pointer =	frames_to_bytes(runtime,
		pcm_data->last_appl_ptr % runtime->buffer_size) / 4;

	kas_pcm_data_produced(pcm_data->kalimba_notify_ep_id);
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
	/* Enable PCM operations in non-atomic context */
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
		pcm_data = &pdata->pcm[rtd->cpu_dai->id][substream->stream];
		pcm_data->sw_ep_handle = dma_alloc_coherent(rtd->platform->dev,
				sizeof(struct endpoint_handle),
				&pcm_data->sw_ep_handle_phy_addr, GFP_KERNEL);
		pcm_data->fe = kcm_find_fe(rtd->cpu_dai->driver->name,
				substream->stream == SNDRV_PCM_STREAM_PLAYBACK);
		BUG_ON(!pcm_data->fe);
		mutex_init(&pcm_data->pcm_free_lock);
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
		pcm_data = &pdata->pcm[rtd->cpu_dai->id][substream->stream];
		dma_free_coherent(rtd->platform->dev,
				sizeof(struct endpoint_handle),
				pcm_data->sw_ep_handle,
				pcm_data->sw_ep_handle_phy_addr);
	}
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static int kas_pcm_probe(struct snd_soc_platform *platform)
{
	struct kas_pcm_data (*pcm_data)[2];
	struct kas_priv_data *priv_data;
	struct snd_kcontrol_new *ctrl;

	pcm_data = devm_kzalloc(platform->dev, sizeof(*pcm_data) * _cpu_dai_cnt,
			GFP_KERNEL);
	if (pcm_data == NULL)
		return -ENOMEM;
	priv_data = devm_kzalloc(platform->dev, sizeof(*priv_data), GFP_KERNEL);
	priv_data->pcm = pcm_data;
	snd_soc_platform_set_drvdata(platform, priv_data);

	/* Register all control interfaces */
	ctrl = kcm_ctrl_first();
	while (ctrl) {
		snd_soc_add_platform_controls(platform, ctrl, 1);
		ctrl = kcm_ctrl_next();
	}

	return 0;
}

static struct snd_soc_platform_driver kas_soc_platform = {
	.probe = kas_pcm_probe,
	.ops = &kas_pcm_ops,
	.pcm_new = kas_pcm_new,
	.pcm_free = kas_pcm_free,
};


static struct snd_soc_component_driver kas_dai_component = {
	.name = "kas-dai",
};

static int kas_pcm_dev_probe(struct platform_device *pdev)
{
	int ret, route_cnt, widget_cnt;
	struct snd_soc_dai_driver *cpu_dai;
	const struct snd_soc_dapm_widget *widget;
	const struct snd_soc_dapm_route *route;

	ret = kcm_drv_status();
	if (ret)
		return ret;

	kcm_set_dev(&pdev->dev);

	/* Get CPU DAI and DAPM route table */
	cpu_dai = kcm_get_dai(&_cpu_dai_cnt);
	widget = kcm_get_codec_widget(&widget_cnt);
	route = kcm_get_route(&route_cnt);
	kas_dai_component.dapm_widgets = widget;
	kas_dai_component.num_dapm_widgets = widget_cnt;
	kas_dai_component.dapm_routes = route,
	kas_dai_component.num_dapm_routes = route_cnt;

	ret = devm_snd_soc_register_platform(&pdev->dev, &kas_soc_platform);
	if (ret < 0)
		return ret;

	return devm_snd_soc_register_component(&pdev->dev, &kas_dai_component,
			cpu_dai, _cpu_dai_cnt);
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
