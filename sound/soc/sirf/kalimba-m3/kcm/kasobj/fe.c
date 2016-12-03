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
		codec = kasobj_to_codec(
			kasobj_find_obj(db->sink_codec.s, kasobj_type_cd));
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
		codec = kasobj_to_codec(
			kasobj_find_obj(db->source_codec.s, kasobj_type_cd));
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

	kcm_debug("KASFE(%s): Invalid codec!\n", db->name.s);
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
	const char *dai_name, *stream_name;

	kcm_debug("FE '%s': go init ...\n", obj->name);
	stream_name = fe_init_cpu_dai(obj, &dai_name);
	fe_init_route(obj, stream_name);
	fe_init_dai_link(obj, dai_name, stream_name);

	return 0;
}

static struct kasobj_ops fe_ops = {
	.init = fe_init,
};
