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
		.name = __S("iacc"),
		.chip_name = __S("10e30000.atlas7_codec"),
		.dai_name = __S("atlas7-codec-hifi"),
		.enable = 1,
		.rate = 48000,
		.playback = 1,
		.capture = 1,
		.codec_widget_num = 2,
		.codec_widget = {
			/* IACC Backend DAIs  */
			SND_SOC_DAPM_AIF_IN("IACC Codec IN", NULL,
				0, SND_SOC_NOPM, 0, 0),
			SND_SOC_DAPM_AIF_OUT("IACC Codec OUT", NULL,
				0, SND_SOC_NOPM, 0, 0)},
		.card_widget_num = 3,
		.card_widget = {
			SND_SOC_DAPM_HP("Headphones", NULL),
			SND_SOC_DAPM_LINE("LINEIN", NULL),
			SND_SOC_DAPM_MIC("MICIN", NULL)},
		.route_num = 8,
		.route = {
			{"Headphones", NULL, "LOUT0"},
			{"Headphones", NULL, "LOUT1"},
			{"Headphones", NULL, "LOUT2"},
			{"Headphones", NULL, "LOUT3"},
			{"AIF Playback", NULL, "IACC Codec OUT"},
			{"LIN0", NULL, "LINEIN"},
			{"MICIN0", NULL, "MICIN"},
			{"IACC Codec IN", NULL, "AIF Capture"},},
	},
	{
		.name = __S("i2s"),
	},
};

/* Sink, Source */
static const struct kasdb_hw hw[] = {
	{
		.name = __S("so_i2s"),
		.is_sink = 0,
		.is_slave = 0,
		.max_channels = 2,
		.def_channels = 0,
		.audio_format = 0,
		.pack_format = kasdb_pack_16,
		.def_rate = 0,
		.bytes_per_ch = 192,
	},
	{
		.name = __S("si_usp3"),
		.is_sink = 1,
		.is_slave = 0,
		.instance_id = ENDPOINT_PHY_DEV_A7CA,
		.max_channels = 4,
		.def_channels = 0,
		.audio_format = 0,
		.pack_format = kasdb_pack_16,
		.def_rate = 0,
		.bytes_per_ch = 192,
	},
	{
		.name = __S("so_usp3"),
		.is_sink = 0,
		.is_slave = 0,
		.instance_id = ENDPOINT_PHY_DEV_A7CA,
		.max_channels = 4,
		.def_channels = 0,	/* Stream dependent */
		.audio_format = 0,
		.pack_format = kasdb_pack_16,
		.def_rate = 0,
		.bytes_per_ch = 192,
	},
	{
		.name = __S("si_usp2"),
		.is_sink = 1,
		.is_slave = 0,
		.instance_id = ENDPOINT_PHY_DEV_A7CA,
		.max_channels = 4,
		.def_channels = 0,
		.audio_format = 0,
		.pack_format = kasdb_pack_16,
		.def_rate = 0,
		.bytes_per_ch = 192,
	},
	{
		.name = __S("so_usp2"),
		.is_sink = 0,
		.is_slave = 0,
		.instance_id = ENDPOINT_PHY_DEV_PCM2,
		.max_channels = 4,
		.def_channels = 0,	/* Stream dependent */
		.audio_format = 0,
		.pack_format = kasdb_pack_16,
		.def_rate = 0,
		.bytes_per_ch = 192,
	},
	{
		.name = __S("si_usp1"),
		.is_sink = 1,
		.is_slave = 0,
		.instance_id = ENDPOINT_PHY_DEV_A7CA,
		.max_channels = 4,
		.def_channels = 0,
		.audio_format = 0,
		.pack_format = kasdb_pack_16,
		.def_rate = 0,
		.bytes_per_ch = 192,
	},
	{
		.name = __S("so_usp1"),
		.is_sink = 0,
		.is_slave = 0,
		.instance_id = ENDPOINT_PHY_DEV_PCM1,
		.max_channels = 4,
		.def_channels = 0,	/* Stream dependent */
		.audio_format = 0,
		.pack_format = kasdb_pack_16,
		.def_rate = 0,
		.bytes_per_ch = 192,
	},
	{
		.name = __S("si_usp0"),
		.is_sink = 1,
		.is_slave = 0,
		.instance_id = ENDPOINT_PHY_DEV_A7CA,
		.max_channels = 4,
		.def_channels = 0,
		.audio_format = 0,
		.pack_format = kasdb_pack_16,
		.def_rate = 0,
		.bytes_per_ch = 192,
	},
	{
		.name = __S("so_usp0"),
		.is_sink = 0,
		.is_slave = 0,
		.instance_id = ENDPOINT_PHY_DEV_PCM0,
		.max_channels = 4,
		.def_channels = 0,	/* Stream dependent */
		.audio_format = 0,
		.pack_format = kasdb_pack_16,
		.def_rate = 0,
		.bytes_per_ch = 192,
	},
	{
		.name = __S("si_iacc"),
		.is_sink = 1,
		.is_slave = 0,
		.max_channels = 4,
		.def_channels = 4,
		.audio_format = 0,
		.pack_format = kasdb_pack_16,
		.def_rate = 48000,
		.bytes_per_ch = 192,
	},
	{
		.name = __S("so_iacc"),
		.is_sink = 0,
		.is_slave = 0,
		.max_channels = 2,
		.def_channels = 0,		/* Stream dependent */
		.audio_format = 0,
		.pack_format = kasdb_pack_16,
		.def_rate = 48000,
		.bytes_per_ch = 192,
	},
	{
		.name = __S("so_iacc_2mic"),
		.is_sink = 0,
		.is_slave = 0,
		.max_channels = 2,
		.def_channels = 2,		/* For two mic cvc */
		.audio_format = 0,
		.pack_format = kasdb_pack_16,
		.def_rate = 48000,
		.bytes_per_ch = 192,
	},
};
