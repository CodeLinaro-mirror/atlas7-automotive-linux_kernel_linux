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

static const struct kasdb_codec codec[] = {
	{
		.name = __S("dummy"),
		.chip_name = __S("snd-soc-dummy"),
		.dai_name = __S("snd-soc-dummy-dai"),
		.enable = 1,
		.rate = 48000,
		.playback = 1,
		.capture = 1,
		.codec_widget_num = 2,
		.codec_widget = {
			/* IACC Backend DAIs  */
			SND_SOC_DAPM_AIF_IN("Codec IN", NULL,
				0, SND_SOC_NOPM, 0, 0),
			SND_SOC_DAPM_AIF_OUT("Codec OUT", NULL,
				0, SND_SOC_NOPM, 0, 0)},
		.card_widget_num = 3,
		.card_widget = {
			SND_SOC_DAPM_HP("Headphones", NULL),
			SND_SOC_DAPM_LINE("LINEIN", NULL),
			SND_SOC_DAPM_MIC("MICIN", NULL)},
		.route_num = 2,
		.route = {
			{"Playback", NULL, "Codec OUT"},
			{"Codec IN", NULL, "Capture"},},
	},
};
