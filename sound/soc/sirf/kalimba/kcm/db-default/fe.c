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

static const struct kasdb_fe fe[] = {
	{
		.name = __S("Music"),
		.playback = 1,
		.internal = 0,
		.stream_name = __S(NULL),	/* "Music Playback" */
		.channels_min = 2,
		.channels_max = 2,
		.rates = KCM_RATES,
		.formats = KCM_FORMATS,
		.sink_codec = __S(CODEC_TYPE),
		.source_codec = __S(NULL),
	},
	{
		.name = __S("Navigation"),
		.playback = 1,
		.internal = 0,
		.stream_name = __S(NULL),	/* "Navigation Playback" */
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = KCM_FORMATS,
		.sink_codec = __S(CODEC_TYPE),
		.source_codec = __S(NULL),
	},
	{
		.name = __S("Alarm"),
		.playback = 1,
		.internal = 0,
		.stream_name = __S(NULL),	/* "Alaram Playback" */
		.channels_min = 1,
		.channels_max = 1,
		.rates = KCM_RATES,
		.formats = KCM_FORMATS,
		.sink_codec = __S(CODEC_TYPE),
		.source_codec = __S(NULL),
	},
	{
		.name = __S("A2DP"),
		.playback = 1,
		.internal = 1,
		.stream_name = __S(NULL),	/* "A2DP Playback" */
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = KCM_FORMATS,
		.sink_codec = __S(CODEC_TYPE),
		.source_codec = __S(NULL),
	},
	{
		.name = __S("Voicecall-bt-to-iacc"),
		.playback = 1,
		.internal = 1,
		.stream_name = __S(NULL),
		.channels_min = 1,
		.channels_max = 1,
		.rates = KCM_RATES,
		.formats = KCM_FORMATS,
		.sink_codec = __S(CODEC_TYPE),
		.source_codec = __S(NULL),
	},
	{
		/* Carplay Voicecall-playback */
		.name = __S("Voicecall-playback"),
		.playback = 1,
		.internal = 0,
		.flow_ctrl = 1,
		.stream_name = __S("Voicecall-playback"),
		.channels_min = 1,
		.channels_max = 1,
		.rates = KCM_RATES,
		.formats = KCM_FORMATS,
		.sink_codec = __S(CODEC_TYPE),
		.source_codec = __S(NULL),
	},
	{
		.name = __S("Iacc-loopback-playback"),
		.playback = 1,
		.internal = 1,
		.stream_name = __S("Iacc-loopback-playback"),
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = KCM_FORMATS,
		.sink_codec = __S(CODEC_TYPE),
		.source_codec = __S(NULL),
	},
	{
		/* Radio Playback */
		.name = __S("I2S-to-iacc-loopback"),
		.playback = 1,
		.internal = 1,
		.stream_name = __S(NULL),
		.channels_min = 2,
		.channels_max = 2,
		.rates = KCM_RATES,
		.formats = KCM_FORMATS,
		.sink_codec = __S(CODEC_TYPE),
		.source_codec = __S(NULL),
	},
	{
		.name = __S("AnalogCapture"),
		.playback = 0,
		.internal = 0,
		.stream_name = __S("Analog Capture"),
		.channels_min = SO_CODEC_CH_MIN,
		.channels_max = SO_CODEC_CH_MAX,
		.rates = KCM_RATES,
		.formats = KCM_FORMATS,
		.sink_codec = __S(NULL),
		.source_codec = __S(CODEC_TYPE),
	},
	{
		.name = __S("Voicecall-iacc-to-bt"),
		.playback = 0,
		.internal = 1,
		.stream_name = __S(NULL),
		.channels_min = 1,
		.channels_max = 1,
		.rates = KCM_RATES,
		.formats = KCM_FORMATS,
		.sink_codec = __S(NULL),
		.source_codec = __S(CODEC_TYPE),
	},
	{
		/* Carplay Voicecall-capture */
		.name = __S("Voicecall-capture"),
		.playback = 0,
		.internal = 0,
		.flow_ctrl = 1,
		.stream_name = __S("Voicecall-capture"),
		.channels_min = 1,
		.channels_max = 1,
		.rates = KCM_RATES,
		.formats = KCM_FORMATS,
		.sink_codec = __S(NULL),
		.source_codec = __S(CODEC_TYPE),
	},
	{
		.name = __S("Iacc-loopback-capture"),
		.playback = 0,
		.internal = 1,
		.stream_name = __S("Iacc-loopback-capture"),
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = KCM_FORMATS,
		.sink_codec = __S(NULL),
		.source_codec = __S(CODEC_TYPE),
	},
	{
		.name = __S("USP0"),
		.playback = 1,
		.internal = 1,
		.stream_name = __S(NULL),	/* "USP0 Playback" */
		.channels_min = 2,
		.channels_max = 2,
		.rates = KCM_RATES,
		.formats = KCM_FORMATS,
		.sink_codec = __S(CODEC_TYPE),
		.source_codec = __S(NULL),
	},
	{
		.name = __S("USP2"),
		.playback = 1,
		.internal = 1,
		.stream_name = __S(NULL),	/* "USP2 Playback" */
		.channels_min = 2,
		.channels_max = 2,
		.rates = KCM_RATES,
		.formats = KCM_FORMATS,
		.sink_codec = __S(CODEC_TYPE),
		.source_codec = __S(NULL),
	},
};
