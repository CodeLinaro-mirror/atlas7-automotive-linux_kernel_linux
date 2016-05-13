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
		.name = __S("IACC-Codec"),
	},
	{
		.name = __S("I2S-CS42888"),
	},
};

/* Sink, Source */
static const struct kasdb_hw hw[] = {
	{
		.name = __S("so_i2s"),
		.is_sink = 0,
		.is_slave = 0,
		.max_channels = 2,
		.def_channels = 2,
		.audio_format = 0,
		.pack_format = kasdb_pack_16,
		.def_rate = 48000,
		.bytes_per_ch = 192,
	},
	{
		.name = __S("si_usp3"),
		.is_sink = 1,
		.is_slave = 0,
		.instance_id = ENDPOINT_PHY_DEV_A7CA,
		.max_channels = 1,
		.def_channels = 1,
		.audio_format = 0,
		.pack_format = kasdb_pack_16,
		.def_rate = 16000,
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
		.def_rate = 16000,
		.bytes_per_ch = 192,
	},
	{
		/* FIXME: this should be combined with so_usp3 */
		.name = __S("so_usp3_a2dp"),
		.is_sink = 0,
		.is_slave = 0,
		.instance_id = ENDPOINT_PHY_DEV_A7CA,
		.max_channels = 4,
		.def_channels = 2,  /* stereo a2dp stream */
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
};

/* Front End */
static const struct kasdb_fe fe[] = {
	{
		.name = __S("Music"),
		.playback = 1,
		.internal = 0,
		.stream_name = __S(NULL),	/* "Music Playback" */
		.channels_min = 1,
		.channels_max = 4,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
		.sink_codec = __S("iacc"),
		.source_codec = __S(NULL),
	},
	{
		.name = __S("Navigation"),
		.playback = 1,
		.internal = 0,
		.stream_name = __S(NULL),	/* "Navigation Playback" */
		.channels_min = 4,
		.channels_max = 4,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
		.sink_codec = __S("iacc"),
		.source_codec = __S(NULL),
	},
	{
		.name = __S("Alarm"),
		.playback = 1,
		.internal = 0,
		.stream_name = __S(NULL),	/* "Alaram Playback" */
		.channels_min = 1,
		.channels_max = 1,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
		.sink_codec = __S("iacc"),
		.source_codec = __S(NULL),
	},
	{
		.name = __S("A2DP"),
		.playback = 1,
		.internal = 1,
		.stream_name = __S(NULL),	/* "A2DP Playback" */
		.channels_min = 1,
		.channels_max = 4,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
		.sink_codec = __S("iacc"),
		.source_codec = __S(NULL),
	},
	{
		.name = __S("Voicecall-bt-to-iacc"),
		.playback = 1,
		.internal = 1,
		.stream_name = __S("Voicecall-bt-to-iacc"),
		.channels_min = 1,
		.channels_max = 4,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
		.sink_codec = __S("iacc"),
		.source_codec = __S(NULL),
	},
	{
		/* Carplay Voicecall-playback */
		.name = __S("Voicecall-playback"),
		.playback = 1,
		.internal = 0,
		.stream_name = __S("Voicecall-playback"),
		.channels_min = 1,
		.channels_max = 1,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
		.sink_codec = __S("iacc"),
		.source_codec = __S(NULL),
	},
	{
		.name = __S("Iacc-loopback-playback"),
		.playback = 1,
		.internal = 1,
		.stream_name = __S("Iacc-loopback-playback"),
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
		.sink_codec = __S("iacc"),
		.source_codec = __S(NULL),
	},
	{
		/* Radio Playback */
		.name = __S("I2S-to-iacc-loopback"),
		.playback = 1,
		.internal = 0,
		.stream_name = __S("I2S-to-iacc-loopback"),
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
		.sink_codec = __S("iacc"),
		.source_codec = __S(NULL),
	},
	{
		.name = __S("AnalogCapture"),
		.playback = 0,
		.internal = 0,
		.stream_name = __S("Analog Capture"),
		.channels_min = 1,
		.channels_max = 1,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
		.sink_codec = __S(NULL),
		.source_codec = __S("iacc"),
	},
	{
		.name = __S("Voicecall-iacc-to-bt"),
		.playback = 0,
		.internal = 1,
		.stream_name = __S("Voicecall-iacc-to-bt"),
		.channels_min = 1,
		.channels_max = 1,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
		.sink_codec = __S(NULL),
		.source_codec = __S("iacc"),
	},
	{
		/* Carplay Voicecall-capture */
		.name = __S("Voicecall-capture"),
		.playback = 0,
		.internal = 0,
		.stream_name = __S("Voicecall-capture"),
		.channels_min = 1,
		.channels_max = 1,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
		.sink_codec = __S(NULL),
		.source_codec = __S("iacc"),
	},
	{
		.name = __S("Iacc-loopback-capture"),
		.playback = 0,
		.internal = 1,
		.stream_name = __S("Iacc-loopback-capture"),
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
		.sink_codec = __S(NULL),
		.source_codec = __S("iacc"),
	},
};
