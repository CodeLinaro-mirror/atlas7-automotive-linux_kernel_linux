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

#include <sound/pcm.h>
#include <sound/soc.h>
#include "../../dsp.h"

static const char *fe_init_cpu_dai(struct kasobj *obj,
		const char **dai_name)
{
	/* struct snd_soc_dai_driver dai = {
	 *	.name = "Music Pin",
	 *	.playback = {
	 *		.stream_name = "Music Playback",
	 *		.channels_min = 1,
	 *		.channels_max = 2,
	 *		.rates = KAS_RATES,
	 *		.formats = KAS_FORMATS,
	 *	},
	 *	.capture = {
	 *		......
	 *	},
	 * };
	 */
	char buf[64];
	const struct kasdb_fe *db = kasobj_to_fe(obj)->db;
	struct snd_soc_dai_driver *dai = kcm_alloc_dai();
	struct snd_soc_pcm_stream *pcm = &dai->playback;

	if (!db->playback)
		pcm = &dai->capture;

	snprintf(buf, sizeof(buf), "%s Pin", db->name.s);
	dai->name = kstrdup(buf, GFP_KERNEL);
	*dai_name = dai->name;

	if (db->stream_name.s) {
		pcm->stream_name = db->stream_name.s;
	} else {
		/* Default stream name */
		snprintf(buf, sizeof(buf), "%s %s", db->name.s,
			db->playback ? "Playback" : "Capture");
		pcm->stream_name = kstrdup(buf, GFP_KERNEL);
	}

	pcm->channels_min = db->channels_min;
	pcm->channels_max = db->channels_max;
	pcm->rates = db->rates;
	pcm->formats = db->formats;

	return pcm->stream_name;
}

static int fe_init_route(struct kasobj *obj, const char *stream_name)
{
	/* struct snd_soc_dapm_route route =
	 *	{ "Codec Out", NULL, "Music Playback" };
	 *	{ "Analog Capture", NULL, "Codec In" };
	 */
	const struct kasdb_fe *db = kasobj_to_fe(obj)->db;
	struct snd_soc_dapm_route *route;
	struct kasobj_codec *codec;
	const struct kasdb_codec *cdb;
	int idx;

	/* Check avalible codec */
	if (db->sink_codec.s) {
		codec = (struct kasobj_codec *)kasobj_find_obj(db->sink_codec.s,
			kasobj_type_cd);
		if (codec) {
			route = kcm_alloc_route();
			cdb = codec->db;
			for (idx = 0; idx < cdb->codec_widget_num; idx++)
				if (cdb->codec_widget[idx].id ==
					snd_soc_dapm_aif_out) {
					route->sink =
						cdb->codec_widget[idx].name;
					route->source = stream_name;
					return 0;
				}
		}
	} else if (db->source_codec.s) {
		codec = (struct kasobj_codec *)kasobj_find_obj(
			db->source_codec.s, kasobj_type_cd);
		if (codec) {
			route = kcm_alloc_route();
			cdb = codec->db;
			for (idx = 0; idx < cdb->codec_widget_num; idx++)
				if (cdb->codec_widget[idx].id ==
					snd_soc_dapm_aif_in) {
					route->source =
						cdb->codec_widget[idx].name;
					route->sink = stream_name;
					return 0;
				}
		}
	}

	pr_err("KASFE(%s): Invalid codec!\n", db->name.s);
	return -EINVAL;
}

static void fe_init_dai_link(struct kasobj *obj, const char *dai_name,
		const char *stream_name)
{
	/* struct snd_soc_dai_link dai_link = {
	 *	.name = "Music",
	 *	.stream_name = "Music Playback",
	 *	.cpu_dai_name = "Music Pin",
	 *	.platform_name = "kas-pcm-audio",
	 *	.dynamic = 1,
	 *	.codec_name = "snd-soc-dummy",
	 *	.codec_dai_name = "snd-soc-dummy-dai",
	 *	.trigger = {SND_SOC_DPCM_TRIGGER_POST,
	 *		SND_SOC_DPCM_TRIGGER_POST},
	 *	.dpcm_playback = 1,
	 * };
	 */
	const struct kasdb_fe *db = kasobj_to_fe(obj)->db;
	struct snd_soc_dai_link *dai_link = kcm_alloc_dai_link();

	dai_link->name = db->name.s;
	dai_link->stream_name = stream_name;
	dai_link->cpu_dai_name = dai_name;
	dai_link->platform_name = "kas-pcm-audio";
	dai_link->dynamic = 1;
	dai_link->codec_name = "snd-soc-dummy";
	dai_link->codec_dai_name = "snd-soc-dummy-dai";
	dai_link->trigger[0] = SND_SOC_DPCM_TRIGGER_POST;
	dai_link->trigger[1] = SND_SOC_DPCM_TRIGGER_POST;
	if (db->playback)
		dai_link->dpcm_playback = 1;
	else
		dai_link->dpcm_capture = 1;
}

/* Create and register CPU DAI, DAPM graph, Card DAI to kcm driver */
static int fe_init(struct kasobj *obj)
{
	int i;
	const char *dai_name, *stream_name;
	struct kasobj_fe *fe = kasobj_to_fe(obj);

	stream_name = fe_init_cpu_dai(obj, &dai_name);
	fe_init_route(obj, stream_name);
	fe_init_dai_link(obj, dai_name, stream_name);

	for (i = 0; i < fe->db->channels_max; i++)
		fe->ep_id[i] = KCM_INVALID_EP_ID;
	fe->ep_cnt = 0;

	return 0;
}

/* Must be consistent with Kalimba definition */
static void fe_parse_format(int format, int channels,
		int *audio_format, int *pack_format)
{
	*audio_format = 0;	/* XXX: Where's the definition? */

	switch (format) {
	case SNDRV_PCM_FORMAT_S16_LE:
		*pack_format = kasdb_pack_16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		*pack_format = kasdb_pack_24r;	/* XXX: Maybe 24l? */
		break;
	default:
		pr_err("KASFE: unknown format!\n");
		*pack_format = kasdb_pack_16;
		break;
	}
}

static int fe_get(struct kasobj *obj, const struct kasobj_param *param)
{
	int i, audio_format, pack_format;
	struct kasobj_fe *fe = kasobj_to_fe(obj);

	if (obj->life_cnt++) {
		kcm_debug("FE '%s' refcnt++: %d\n", obj->name, obj->life_cnt);
		return 0;
	}

	fe_parse_format(param->format, param->channels,
			&audio_format, &pack_format);
	fe->ep_cnt = param->channels;

	if (fe->db->playback)
		kalimba_get_source(ENDPOINT_TYPE_FILE, 0, fe->ep_cnt,
				param->ep_handle_pa, fe->ep_id, __kcm_resp);
	else
		kalimba_get_sink(ENDPOINT_TYPE_FILE, 0, fe->ep_cnt,
				param->ep_handle_pa, fe->ep_id, __kcm_resp);
	for (i = 0; i < fe->ep_cnt; i++) {
		kalimba_config_endpoint(fe->ep_id[i],
				ENDPOINT_CONF_AUDIO_SAMPLE_RATE,
				param->rate, __kcm_resp);
		kalimba_config_endpoint(fe->ep_id[i],
				ENDPOINT_CONF_AUDIO_DATA_FORMAT,
				audio_format, __kcm_resp);
		kalimba_config_endpoint(fe->ep_id[i],
				ENDPOINT_CONF_DRAM_PACKING_FORMAT,
				pack_format, __kcm_resp);
		kalimba_config_endpoint(fe->ep_id[i],
				ENDPOINT_CONF_INTERLEAVING_MODE,
				1, __kcm_resp);		/* Needs configure? */
		kalimba_config_endpoint(fe->ep_id[i],
				ENDPOINT_CONF_PERIOD_SIZE,
				param->period_size, __kcm_resp);
		kalimba_config_endpoint(fe->ep_id[i],
				ENDPOINT_CONF_FLOW_CONTROL,
				fe->db->flow_ctrl, __kcm_resp);
		if (!fe->db->playback)
			kalimba_config_endpoint(fe->ep_id[i],
					ENDPOINT_CONF_CLOCK_MASTER,
					1, __kcm_resp);	/* Needs configure? */
	}

	kcm_debug("FE '%s' created\n", obj->name);
	return 0;
}

static int fe_put(struct kasobj *obj)
{
	int i;
	struct kasobj_fe *fe = kasobj_to_fe(obj);

	BUG_ON(!obj->life_cnt);
	if (--obj->life_cnt) {
		kcm_debug("FE '%s' refcnt--: %d\n", obj->name, obj->life_cnt);
		return 0;
	}

	if (fe->ep_id[0] != KCM_INVALID_EP_ID) {
		if (fe->db->playback)
			kalimba_close_source(fe->ep_cnt, fe->ep_id, __kcm_resp);
		else
			kalimba_close_sink(fe->ep_cnt, fe->ep_id, __kcm_resp);
	}
	for (i = 0; i < fe->db->channels_max; i++)
		fe->ep_id[i] = KCM_INVALID_EP_ID;
	fe->ep_cnt = 0;

	kcm_debug("FE '%s' destroyed\n", obj->name);
	return 0;
}

static u16 fe_get_ep(struct kasobj *obj, unsigned pin, int is_sink)
{
	struct kasobj_fe *fe = kasobj_to_fe(obj);

	BUG_ON(pin >= fe->ep_cnt);
	return fe->ep_id[pin];
}

static struct kasobj_ops fe_ops = {
	.init = fe_init,
	.get = fe_get,
	.put = fe_put,
	.get_ep = fe_get_ep,
};
