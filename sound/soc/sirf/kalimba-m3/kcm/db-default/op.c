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

static const struct kasdb_op op[] = {
	{
		/* Music passthrough */
		.name = __S("op_pass_music"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(KCM_CTRLS_BASICPASS("Music")),
		.cap_id = CAPABILITY_ID_BASIC_PASSTHROUGH,
		.rate = 0,
		.param.dummy = 0,
	},
	{
		/* Music resampler */
		.name = __S("op_src_music"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(NULL),
		.cap_id = CAPABILITY_ID_RESAMPLER,
		.rate = 48000,
		.param.resampler_custom_output = 0,
	},
	{
		/* Music splitter: 2 -> 4 */
		.name = __S("op_split_music"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(NULL),
		.cap_id = CAPABILITY_ID_SPLITTER,
		.rate = 0,
		.param.dummy = 0,
	},
	{
		/* Music user PEQ */
		.name = __S("op_upeq_music"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(KCM_CTRLS_PEQ("User")),
		.cap_id = CAPABILITY_ID_PEQ,
		.rate = 48000,
		.param.dummy = 0,
	},
	{
		/* Music Spk1 PEQ */
		.name = __S("op_spk1_peq_music"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(KCM_CTRLS_PEQ("Spk1")),
		.cap_id = CAPABILITY_ID_PEQ,
		.rate = 48000,
		.param.dummy = 0,
	},
	{
		/* Music Spk2 PEQ */
		.name = __S("op_spk2_peq_music"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(KCM_CTRLS_PEQ("Spk2")),
		.cap_id = CAPABILITY_ID_PEQ,
		.rate = 48000,
		.param.dummy = 0,
	},
	{
		/* Music Spk3 PEQ */
		.name = __S("op_spk3_peq_music"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(KCM_CTRLS_PEQ("Spk3")),
		.cap_id = CAPABILITY_ID_PEQ,
		.rate = 48000,
		.param.dummy = 0,
	},
	{
		/* Music Spk4 PEQ */
		.name = __S("op_spk4_peq_music"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(KCM_CTRLS_PEQ("Spk4")),
		.cap_id = CAPABILITY_ID_PEQ,
		.rate = 48000,
		.param.dummy = 0,
	},
	{
		/* Music bass+ */
		.name = __S("op_bass_music"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(KCM_CTRLS_BASS("Music")),
		.cap_id = CAPABILITY_ID_DBE_FULLBAND_IN_OUT,
		.rate = 48000,
		.param.bass_pair_idx = 0,
	},
	{
		/* Music delay */
		.name = __S("op_delay_music"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(KCM_CTRLS_DELAY("Music")),
		.cap_id = CAPABILITY_ID_DELAY,
		.rate = 0,
		.param.delay_channels = 4,
	},
	{
		/* Capture passthrough */
		.name = __S("op_pass_cap"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(KCM_CTRLS_BASICPASS("Capture")),
		.cap_id = CAPABILITY_ID_BASIC_PASSTHROUGH,
		.rate = 0,
		.param.dummy = 0,
	},
	{
		/* Capture Resampler */
		.name = __S("op_src_cap"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(NULL),
		.cap_id = CAPABILITY_ID_RESAMPLER,
		.rate = 48000,
		.param.resampler_custom_output = 1,
	},
	{
		/* Mixer1: music, navigation, alarm */
		.name = __S("op_mixer"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(KCM_CTRLS_MIXER(
			"Navigation", "Alarm", "Music")),
		.cap_id = CAPABILITY_ID_MIXER,
		.rate = 48000,
		.param.mixer_streams = 0x244,
	},
	{
		/* Mixer2: mixer1, voice */
		.name = __S("op_mixer2"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(KCM_CTRLS_MIXER(
			"Multimedia", "Voicecall", "NOCTRL")),
		.cap_id = CAPABILITY_ID_MIXER,
		.rate = 48000,
		.param.mixer_streams = 0x424,
	},
	{
		/* Volume control */
		.name = __S("op_volume_control"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(KCM_CTRLS_VOLCTRL("Main")),
		.cap_id = CAPABILITY_ID_VOLUME_CONTROL,
		.rate = 48000,
		.param.dummy = 0,
	},
	{
		/* AEC-ref */
		.name = __S("op_aecref"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(KCM_CTRLS_AECREF("Voicecall")),
		.cap_id = CAPABILITY_ID_AEC_REF_DUMMY,
		.rate = 48000,
		.param.dummy = 0,
	},
	{
		/* CVC send 1 Mic*/
		.name = __S("op_send_cvc"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(KCM_CTRLS_CVCSEND("Voicecall")),
		.cap_id = CAPABILITY_ID_CVCHF_SEND_DUMMY,
		.rate = 0,
		.param.dummy = 0,
	},
	{
		/* CVC recv */
		.name = __S("op_recv_cvc"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(KCM_CTRLS_CVCRECV("Voicecall")),
		.cap_id = CAPABILITY_ID_CVC_RCV_DUMMY,
		.rate = 0,
		.param.dummy = 0,
	},
	{
		/* CVC Resampler */
		.name = __S("op_src_cvc"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(NULL),
		.cap_id = CAPABILITY_ID_RESAMPLER,
		.rate = 48000,
		.param.resampler_custom_output = 0,
	},
	{
		/* CVC splitter: 1 -> 2 */
		.name = __S("op_split1x2_cvc"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(NULL),
		.cap_id = CAPABILITY_ID_SPLITTER,
		.rate = 0,
		.param.dummy = 0,
	},
	{
		/* Alaram resampler */
		.name = __S("op_src_alarm"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(NULL),
		.cap_id = CAPABILITY_ID_RESAMPLER,
		.rate = 48000,
		.param.resampler_custom_output = 0,
	},
	{
		/* Alarm splitter: 1 -> 2 */
		.name = __S("op_split_alarm_1x2"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(NULL),
		.cap_id = CAPABILITY_ID_SPLITTER,
		.rate = 0,
		.param.dummy = 0,
	},
	{
		/* Alarm splitter: 2 -> 4 */
		.name = __S("op_split_alarm"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(NULL),
		.cap_id = CAPABILITY_ID_SPLITTER,
		.rate = 0,
		.param.dummy = 0,
	},
	{
		/* Source sync: 1-Music, 2-Linein, 3-A2DP, 4-I2Sin, 5-Navigation
		 *		6-Alarm, 7-cVc recv
		 */
		.name = __S("op_srcsync"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(KCM_CTRLS_SOURCESYNC("Multimedia")),
		.cap_id = CAPABILITY_ID_SOURCE_SYNC,
		.rate = 48000,
		.param.srcsync_cfg.stream_ch = { 2, 2, 2, 2, 2, 1, 1 , 0, },
		.param.srcsync_cfg.input_map = { 1, 2, 1, 2, 1, 2, 1, 2,
						 3, 4, 5, 6, 0 },
	},
	{
		/* Radio resampler */
		.name = __S("op_src_radio"),
		.ctrl_base = __S(NULL),
		.ctrl_names = __S(NULL),
		.cap_id = CAPABILITY_ID_RESAMPLER,
		.rate = 48000,
		.param.resampler_custom_output = 0,
	},
};
