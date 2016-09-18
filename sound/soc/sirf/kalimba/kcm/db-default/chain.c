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

static const struct kasdb_chain chain[] = {
	/* Music */
	{
		.name = __S("chain_music_2"),
		.trg_fe_name = __S("Music"),
		.trg_channels = 2,
		.links = __S("lk_music_src;lk_src_srcsync;"
			"lk_srcsync_pass;lk_pass_bass;lk_bass_split;"
			"lk_split_upeq;lk_upeq_delay;lk_delay_s1peq;"
			"lk_delay_s2peq;lk_delay_s3peq;lk_delay_s4peq;"
			"lk_s1peq_mixer;lk_s2peq_mixer;lk_s3peq_mixer;"
			"lk_s4peq_mixer;lk_mixer_mixer2;lk_mixer2_volctrl;"
			"lk_aecref_codec;lk_volctrl_aecref"),
		.mutexs = __S(NULL),
	},

	/* Navigation */
	{
		.name = __S("chain_navi"),
		.trg_fe_name = __S("Navigation"),
		.trg_channels = 2,
		.links = __S("lk_navi_srcsync;lk_srcsync_mixer;lk_mixer_mixer2;"
			"lk_mixer2_volctrl;lk_aecref_codec;lk_volctrl_aecref"),
		.mutexs = __S(NULL),
	},

	/* Alarm */
	{
		.name = __S("chain_alarm"),
		.trg_fe_name = __S("Alarm"),
		.trg_channels = 1,
		.links = __S("lk_alarm_src;lk_src_srcsync_alarm;"
			"lk_srcsync_split_1x2;lk_alarm_split;"
			"lk_alarm_mixer;lk_mixer_mixer2;lk_mixer2_volctrl;"
			"lk_aecref_codec;lk_volctrl_aecref"),
		.mutexs = __S(NULL),
	},

	/* CVC Voice call */
	{
		.name = __S("chain_cvc_send_1mic"),
		.trg_fe_name = __S("Voicecall-iacc-to-bt"),
		.trg_channels = 1,
		.cvc_mic = single,
		.links = __S("lk_usp3_cvc_recv;lk_cvc_recv_src;"
			"lk_src_srcsync_cvc;lk_srcsync_split1x2_cvc;"
			"lk_split1x2_mixer2_cvc;lk_mixer2_volctrl;"
			"lk_aecref_codec;lk_codec_aecref_1mic;"
			"lk_volctrl_aecref;lk_aecref_cvc_send_1mic;"
			"lk_cvc_send_usp3;lk_aecref_cvc_send_ref"),
		.mutexs = __S("chain_lin_to_lout_2;chain_cap_mono;"
			"chain_cap_stereo;chain_a2dp_2ch;"
			"chain_voicecall_capture_1mic;"
			"chain_voicecall_capture_2mic"),
	},
	{
		.name = __S("chain_cvc_send_2mic"),
		.trg_fe_name = __S("Voicecall-iacc-to-bt"),
		.trg_channels = 1,
		.cvc_mic = doub,
		.links = __S("lk_usp3_cvc_recv;lk_cvc_recv_src;"
			"lk_src_srcsync_cvc;lk_srcsync_split1x2_cvc;"
			"lk_split1x2_mixer2_cvc;lk_mixer2_volctrl;"
			"lk_aecref_codec;lk_codec_aecref_2mic;"
			"lk_volctrl_aecref;lk_aecref_cvc_send_2mic;"
			"lk_cvc_send_usp3;lk_aecref_cvc_send_ref"),
		.mutexs = __S("chain_lin_to_lout_2;chain_cap_mono;"
			"chain_cap_stereo;chain_a2dp_2ch;"
			"chain_voicecall_capture_1mic;"
			"chain_voicecall_capture_2mic"),
	},
	{
		.name = __S("chain_cvc_recv"),
		.trg_fe_name = __S("Voicecall-bt-to-iacc"),
		.trg_channels = 1,
		.links = __S(NULL),
		.mutexs = __S("chain_a2dp_2ch;chain_voicecall_playback"),
	},

	/* Microphone */
	{
		.name = __S("chain_cap_mono"),
		.trg_fe_name = __S("AnalogCapture"),
		.trg_channels = 1,
		.links = __S("lk_codec_src_1ch;lk_src_pass_1ch;"
			"lk_pass_cap_1ch"),
		.mutexs = __S("chain_lin_to_lout_2;chain_cvc_send_1mic;"
			"chain_cvc_send_2mic;chain_voicecall_capture_1mic;"
			"chain_voicecall_capture_2mic"),
	},
	{
		.name = __S("chain_cap_stereo"),
		.trg_fe_name = __S("AnalogCapture"),
		.trg_channels = 2,
		.links = __S("lk_codec_src_2ch;lk_src_pass_2ch;"
			"lk_pass_cap_2ch"),
		.mutexs = __S("chain_lin_to_lout_2;chain_cvc_send_1mic;"
			"chain_cvc_send_1mic;chain_voicecall_capture_1mic;"
			"chain_voicecall_capture_2mic"),
	},

	/* IACC Line-In to Line-Out */
	{
		.name = __S("chain_lin_to_lout_2"),
		.trg_fe_name = __S("Iacc-loopback-playback"),
		.trg_channels = 2,
		.links = __S("lk_codec_src;lk_src_srcsync_lin;"
			"lk_srcsync_pass;lk_pass_bass;lk_bass_split;"
			"lk_split_upeq;lk_upeq_delay;lk_delay_s1peq;"
			"lk_delay_s2peq;lk_delay_s3peq;lk_delay_s4peq;"
			"lk_s1peq_mixer;lk_s2peq_mixer;lk_s3peq_mixer;"
			"lk_s4peq_mixer;lk_mixer_mixer2;lk_mixer2_volctrl;"
			"lk_aecref_codec;lk_volctrl_aecref"),
		.mutexs = __S("chain_cap_mono;chain_cap_stereo;"
			"chain_cvc_send_1mic;chain_cvc_send_2mic;"
			"chain_voicecall_capture_1mic;"
			"chain_voicecall_capture_2mic;chain_i2s_to_iacc_2;"
			"chain_usp0_2ch;chain_usp2_2ch"),
	},
	{
		/* Only to trigger codec working */
		.name = __S("chain_lin_to_lout_dummy"),
		.trg_fe_name = __S("Iacc-loopback-capture"),
		.trg_channels = 0,	/* Any channels */
		.links = __S(NULL),	/* No links */
		.mutexs = __S("chain_cap_mono;chain_cap_stereo;"
			"chain_cvc_send_1mic;chain_cvc_send_2mic;"
			"chain_voicecall_capture_1mic;"
			"chain_voicecall_capture_2mic"),
	},

	/* A2DP */
	{
		.name = __S("chain_a2dp_2ch"),
		.trg_fe_name = __S("A2DP"),
		.trg_channels = 2,
		.links = __S("lk_usp3_srcsync;"
			"lk_srcsync_pass;lk_pass_bass;lk_bass_split;"
			"lk_split_upeq;lk_upeq_delay;lk_delay_s1peq;"
			"lk_delay_s2peq;lk_delay_s3peq;lk_delay_s4peq;"
			"lk_s1peq_mixer;lk_s2peq_mixer;lk_s3peq_mixer;"
			"lk_s4peq_mixer;lk_mixer_mixer2;lk_mixer2_volctrl;"
			"lk_aecref_codec;lk_volctrl_aecref"),
		.mutexs = __S("chain_cvc_send_1mic;chain_cvc_send_1mic;"
			"chain_cvc_recv"),
	},

	/* USP0 Playback */
	{
		.name = __S("chain_usp0_2ch"),
		.trg_fe_name = __S("USP0"),
		.trg_channels = 2,
		.links = __S("lk_usp0_src;lk_src_srcsync_radio;"
			"lk_srcsync_pass;lk_pass_bass;lk_bass_split;"
			"lk_split_upeq;lk_upeq_delay;lk_delay_s1peq;"
			"lk_delay_s2peq;lk_delay_s3peq;lk_delay_s4peq;"
			"lk_s1peq_mixer;lk_s2peq_mixer;lk_s3peq_mixer;"
			"lk_s4peq_mixer;lk_mixer_mixer2;lk_mixer2_volctrl;"
			"lk_aecref_codec;lk_volctrl_aecref"),
		.mutexs = __S("chain_lin_to_lout_2;chain_i2s_to_iacc_2;"
			"chain_usp2_2ch"),
	},

	/* USP2 Playback */
	{
		.name = __S("chain_usp2_2ch"),
		.trg_fe_name = __S("USP2"),
		.trg_channels = 2,
		.links = __S("lk_usp2_src;lk_src_srcsync_radio;"
			"lk_srcsync_pass;lk_pass_bass;lk_bass_split;"
			"lk_split_upeq;lk_upeq_delay;lk_delay_s1peq;"
			"lk_delay_s2peq;lk_delay_s3peq;lk_delay_s4peq;"
			"lk_s1peq_mixer;lk_s2peq_mixer;lk_s3peq_mixer;"
			"lk_s4peq_mixer;lk_mixer_mixer2;lk_mixer2_volctrl;"
			"lk_aecref_codec;lk_volctrl_aecref"),
		.mutexs = __S("chain_lin_to_lout_2;chain_i2s_to_iacc_2;"
			"chain_usp0_2ch"),
	},

	/* Carplay */
	{
		.name = __S("chain_voicecall_capture_1mic"),
		.trg_fe_name = __S("Voicecall-capture"),
		.trg_channels = 1,
		.cvc_mic = single,
		.links = __S("lk_codec_aecref_1mic;lk_aecref_cvc_send_1mic;"
			"lk_aecref_cvc_send_ref;lk_cvc_send_vocall_cap"),
		.mutexs = __S("chain_lin_to_lout_2;chain_cap_mono;"
			"chain_cap_stereo;chain_cvc_send_1mic;"
			"chain_cvc_send_2mic;chain_lin_to_lout_dummy"),
	},
	{
		.name = __S("chain_voicecall_capture_2mic"),
		.trg_fe_name = __S("Voicecall-capture"),
		.trg_channels = 1,
		.cvc_mic = doub,
		.links = __S("lk_codec_aecref_2mic;lk_aecref_cvc_send_2mic;"
			"lk_aecref_cvc_send_ref;lk_cvc_send_vocall_cap"),
		.mutexs = __S("chain_lin_to_lout_2;chain_cap_mono;"
			"chain_cap_stereo;chain_cvc_send_1mic;"
			"chain_cvc_send_2mic;chain_lin_to_lout_dummy"),
	},
	{
		.name = __S("chain_voicecall_playback"),
		.trg_fe_name = __S("Voicecall-playback"),
		.trg_channels = 1,
		.links = __S("lk_vocall_play_cvc_recv;"
			"lk_cvc_recv_src;lk_src_srcsync_cvc;"
			"lk_srcsync_split1x2_cvc;lk_split1x2_mixer2_cvc;"
			"lk_mixer2_volctrl;lk_aecref_codec;lk_volctrl_aecref"),
		.mutexs = __S("chain_cvc_recv"),
	},

	/* I2S to IACC loop */
	{
		.name = __S("chain_i2s_to_iacc_2"),
		.trg_fe_name = __S("I2S-to-iacc-loopback"),
		.trg_channels = 2,
		.links = __S("lk_i2s_src;lk_src_srcsync_radio;"
			"lk_srcsync_pass;lk_pass_bass;lk_bass_split;"
			"lk_split_upeq;lk_upeq_delay;lk_delay_s1peq;"
			"lk_delay_s2peq;lk_delay_s3peq;lk_delay_s4peq;"
			"lk_s1peq_mixer;lk_s2peq_mixer;lk_s3peq_mixer;"
			"lk_s4peq_mixer;lk_mixer_mixer2;lk_mixer2_volctrl;"
			"lk_aecref_codec;lk_volctrl_aecref"),
		.mutexs = __S("chain_lin_to_lout_2;chain_usp0_2ch;"
			"chain_usp2_2ch"),
	},
};
