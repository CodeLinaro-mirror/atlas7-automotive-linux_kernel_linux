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

static int codec_fixup(struct snd_soc_pcm_runtime *rtd,
			struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_CHANNELS);

	/* The kalimba DSP will covert the FE rate to 48k */
	rate->min = rate->max = 48000;

	/*cvc 2mic should enable Iacc ADC1/2 both work*/
	if (kcm_enable_2mic_cvc)
		channels->min = channels->max = 2;
	return 0;
}

static const struct codec_name_ops{
	char *name;
	struct snd_soc_ops *hw_ops;
	int (*fixup)(struct snd_soc_pcm_runtime *rtd,
			struct snd_pcm_hw_params *params);
} _codec_ops[] = {
	{"dummy", NULL, codec_fixup},
};

static const struct codec_name_ops *codec_find_ops(const char *name)
{
	int cnt;

	for (cnt = 0; cnt < ARRAY_SIZE(_codec_ops); cnt++) {
		if (kcm_strcasestr(name, _codec_ops[cnt].name))
			return &_codec_ops[cnt];
	}

	return NULL;
}

static int codec_init_dai_link(struct kasobj *obj)
{
	/*
	 * struct snd_soc_dai_link kas_be_dais[] = {
	 *	.name = "IACC-Codec",
	 *	.be_id = 0,
	 *	.cpu_dai_name = "snd-soc-dummy-dai",
	 *	.platform_name = "snd-soc-dummy",
	 *	.no_pcm = 1,
	 *	.codec_name = "10e30000.atlas7_codec",
	 *	.codec_dai_name = "atlas7-codec-hifi",
	 *	.be_hw_params_fixup = kas_iacc_fixup,
	 *	.ignore_suspend = 1,
	 *	.ignore_pmdown_time = 1,
	 *	.dpcm_playback = 1,
	 *	.dpcm_capture = 1,
	 * },
	 */
	const struct kasdb_codec *db = kasobj_to_codec(obj)->db;
	struct snd_soc_dai_link *dai_link = kcm_alloc_dai_link();
	const struct codec_name_ops *codec_ops;
	static char be_id;

	codec_ops = codec_find_ops(db->name.s);
	if (!codec_ops) {
		pr_err("KASCODEC(%s): Unsupported codec ops!\n",
			db->name.s);
		return -EINVAL;
	}

	dai_link->name = db->name.s;
	dai_link->be_id = be_id++;
	dai_link->cpu_dai_name = "snd-soc-dummy-dai";
	dai_link->platform_name = "snd-soc-dummy";
	dai_link->no_pcm = 1;
	dai_link->codec_name = db->chip_name.s;
	dai_link->codec_dai_name = db->dai_name.s;
	dai_link->ignore_suspend = 1;
	dai_link->ignore_pmdown_time = 1;
	dai_link->dpcm_playback = db->playback;
	dai_link->dpcm_capture = db->capture;
	if (codec_ops->fixup)
		dai_link->be_hw_params_fixup = codec_ops->fixup;
	if (codec_ops->hw_ops)
		dai_link->ops = codec_ops->hw_ops;

	return 0;
}

static int codec_init(struct kasobj *obj)
{
	struct kasobj_codec *codec = kasobj_to_codec(obj);
	const struct kasdb_codec *db = codec->db;

	if (!db->enable)
		return 0;

	codec->rate = db->rate;
	kcm_put_codec_widget(db->codec_widget, db->codec_widget_num);
	kcm_put_card_widget(db->card_widget, db->card_widget_num);
	kcm_put_card_route(db->route, db->route_num);

	return codec_init_dai_link(obj);
}

static struct kasobj_ops codec_ops = {
	.init = codec_init,
};
