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
		.name = __S("chain_music_1"),
		.trg_fe_name = __S("Music"),
		.trg_channels = 1,
		.links = __S("lk_music_pass_1;lk_pass_src_1;lk_src_split1x2;"
				"lk_split1x2_split;lk_split_upeq;lk_upeq_bass_1;"
				"lk_upeq_bass_2;lk_bass_1_delay;lk_bass_2_delay;"
				"lk_delay_s1peq;lk_delay_s2peq;lk_delay_s3peq;"
				"lk_delay_s4peq;lk_s1peq_mixer;lk_s2peq_mixer;"
				"lk_s3peq_mixer;lk_s4peq_mixer;lk_mixer_mixer2;"
				"lk_mixer2_volctrl;lk_aecref_1mic_iacc;"
				"lk_volctrl_aecref_1mic"),
		.mutexs = __S("chain_lin_to_lout_1;chain_lin_to_lout_2;"
				"chain_i2s_to_iacc_2;chain_a2dp_2ch;"
				"chain_usp0_2ch;chain_usp1_2ch;chain_usp2_2ch"),
	},
	{
		.name = __S("chain_music_2"),
		.trg_fe_name = __S("Music"),
		.trg_channels = 2,
		.links = __S("lk_music_pass_2;lk_pass_src_2;lk_src_split;"
				"lk_split_upeq;lk_upeq_bass_1;lk_upeq_bass_2;"
				"lk_bass_1_delay;lk_bass_2_delay;lk_delay_s1peq;"
				"lk_delay_s2peq;lk_delay_s3peq;lk_delay_s4peq;"
				"lk_s1peq_mixer;lk_s2peq_mixer;lk_s3peq_mixer;"
				"lk_s4peq_mixer;lk_mixer_mixer2;lk_mixer2_volctrl;"
				"lk_aecref_1mic_iacc;lk_volctrl_aecref_1mic"),
		.mutexs = __S("chain_lin_to_lout_1;chain_lin_to_lout_2;"
				"chain_i2s_to_iacc_2;chain_a2dp_2ch;"
				"chain_usp0_2ch;chain_usp1_2ch;chain_usp2_2ch"),
	},
	{
		.name = __S("chain_music_4"),
		.trg_fe_name = __S("Music"),
		.trg_channels = 4,
		.links = __S("lk_music_pass_4;lk_pass_src_4;lk_src_upeq;"
				"lk_upeq_bass_1;lk_upeq_bass_2;lk_bass_1_delay;"
				"lk_bass_2_delay;lk_delay_s1peq;lk_delay_s2peq;"
				"lk_delay_s3peq;lk_delay_s4peq;lk_s1peq_mixer;"
				"lk_s2peq_mixer;lk_s3peq_mixer;lk_s4peq_mixer;"
				"lk_mixer_mixer2;lk_mixer2_volctrl;"
				"lk_aecref_1mic_iacc;lk_volctrl_aecref_1mic"),
		.mutexs = __S("chain_lin_to_lout_1;chain_lin_to_lout_2;"
				"chain_i2s_to_iacc_2;chain_a2dp_2ch;"
				"chain_usp0_2ch;chain_usp1_2ch;chain_usp2_2ch"),
	},

	/* Navigation */
	{
		.name = __S("chain_navi"),
		.trg_fe_name = __S("Navigation"),
		.trg_channels = 4,
		.links = __S(
				"lk_navi_mixer;lk_mixer_mixer2;lk_mixer2_volctrl;"
				"lk_aecref_1mic_iacc;lk_volctrl_aecref_1mic"),
		.mutexs = __S(NULL),
	},

	/* Alarm */
	{
		.name = __S("chain_alarm"),
		.trg_fe_name = __S("Alarm"),
		.trg_channels = 1,
		.links = __S("lk_alarm_src;lk_alarm_split_1x2;lk_alarm_split;"
				"lk_alarm_mixer;lk_mixer_mixer2;lk_mixer2_volctrl;"
				"lk_aecref_1mic_iacc;lk_volctrl_aecref_1mic"),
		.mutexs = __S(NULL),
	},

	/* CVC Voice call */
	{
		.name = __S("chain_cvc_send_1mic"),
		.trg_fe_name = __S("Voicecall-iacc-to-bt"),
		.trg_channels = 1,
		.links = __S(NULL),
		.mutexs = __S("chain_lin_to_lout_1;chain_lin_to_lout_2;"
				"chain_cap;chain_a2dp_2ch;"
				"chain_voicecall_capture;chain_usp0_2ch;"
				"chain_usp1_2ch;chain_usp2_2ch"),
	},
	{
		.name = __S("chain_cvc_recv"),
		.trg_fe_name = __S("Voicecall-bt-to-iacc"),
		.trg_channels = 1,
		.links = __S("lk_usp3_cvc_recv;lk_cvc_recv_src;"
				"lk_src_split1x2_cvc;lk_split1x2_split2x4_cvc;"
				"lk_split2x4_mixer2_cvc;lk_mixer2_volctrl;"
				"lk_aecref_1mic_iacc;lk_iacc_aecref_1mic;"
				"lk_volctrl_aecref_1mic;lk_aecref_1mic_cvc_send;"
				"lk_cvc_send_usp3;lk_aecref_1mic_cvc_send_ref"),
		.mutexs = __S("chain_a2dp_2ch;chain_voicecall_playback;"
				"chain_usp0_2ch;chain_usp1_2ch;chain_usp2_2ch"),
	},

	/* Microphone */
	{
		.name = __S("chain_cap"),
		.trg_fe_name = __S("AnalogCapture"),
		.trg_channels = 1,
		.links = __S("lk_iacc_pass;lk_pass_cap"),
		.mutexs = __S("chain_lin_to_lout_1;chain_lin_to_lout_2;"
				"chain_cvc_send_1mic;chain_voicecall_capture"),
	},

	/* IACC Line-In to Line-Out */
	{
		.name = __S("chain_lin_to_lout_1"),
		.trg_fe_name = __S("Iacc-loopback-playback"),
		.trg_channels = 1,
		.links = __S("lk_lin_pass_1;lk_pass_src_1;lk_src_split1x2;"
				"lk_split1x2_split;lk_split_upeq;lk_upeq_bass_1;"
				"lk_upeq_bass_2;lk_bass_1_delay;lk_bass_2_delay;"
				"lk_delay_s1peq;lk_delay_s2peq;lk_delay_s3peq;"
				"lk_delay_s4peq;lk_s1peq_mixer;lk_s2peq_mixer;"
				"lk_s3peq_mixer;lk_s4peq_mixer;lk_mixer_mixer2;"
				"lk_mixer2_volctrl;lk_aecref_1mic_iacc;"
				"lk_volctrl_aecref_1mic"),
		.mutexs = __S("chain_music_1;chain_music_2;chain_music_4;"
				"chain_a2dp_2ch;chain_cap;chain_cvc_send_1mic;"
				"chain_voicecall_capture;chain_i2s_to_iacc_2;"
				"chain_usp0_2ch;chain_usp1_2ch;chain_usp2_2ch"),
	},
	{
		.name = __S("chain_lin_to_lout_2"),
		.trg_fe_name = __S("Iacc-loopback-playback"),
		.trg_channels = 2,
		.links = __S("lk_lin_pass_2;lk_pass_src_2;lk_src_split;"
				"lk_split_upeq;lk_upeq_bass_1;lk_upeq_bass_2;"
				"lk_bass_1_delay;lk_bass_2_delay;lk_delay_s1peq;"
				"lk_delay_s2peq;lk_delay_s3peq;lk_delay_s4peq;"
				"lk_s1peq_mixer;lk_s2peq_mixer;lk_s3peq_mixer;"
				"lk_s4peq_mixer;lk_mixer_mixer2;lk_mixer2_volctrl;"
				"lk_aecref_1mic_iacc;lk_volctrl_aecref_1mic"),
		.mutexs = __S("chain_music_1;chain_music_2;chain_music_4;"
				"chain_a2dp_2ch;chain_cap;chain_cvc_send_1mic;"
				"chain_voicecall_capture;chain_i2s_to_iacc_2;"
				"chain_usp0_2ch;chain_usp1_2ch;chain_usp2_2ch"),
	},
	{
		/* Only to trigger codec working */
		.name = __S("chain_lin_to_lout_dummy"),
		.trg_fe_name = __S("Iacc-loopback-capture"),
		.trg_channels = 0,	/* Any channels */
		.links = __S(NULL),	/* No links */
		.mutexs = __S("chain_music_1;chain_music_2;chain_music_4;"
				"chain_cap;chain_cvc_send_1mic;"
				"chain_voicecall_capture"),
	},

	/* A2DP */
	{
		.name = __S("chain_a2dp_2ch"),
		.trg_fe_name = __S("A2DP"),
		.trg_channels = 4, /* FIXME: it should be 2 */
		.links = __S("lk_usp3_pass_2;lk_pass_src_2;lk_src_split;"
				"lk_split_upeq;lk_upeq_bass_1;lk_upeq_bass_2;"
				"lk_bass_1_delay;lk_bass_2_delay;lk_delay_s1peq;"
				"lk_delay_s2peq;lk_delay_s3peq;lk_delay_s4peq;"
				"lk_s1peq_mixer;lk_s2peq_mixer;lk_s3peq_mixer;"
				"lk_s4peq_mixer;lk_mixer_mixer2;lk_mixer2_volctrl;"
				"lk_aecref_1mic_iacc;lk_volctrl_aecref_1mic"),
		.mutexs = __S("chain_music_1;chain_music_2;chain_music_4;"
				"chain_cvc_send_1mic;chain_cvc_recv;chain_lin_to_lout_1;"
				"chain_lin_to_lout_2;chain_i2s_to_iacc_2;"
				"chain_usp0_2ch;chain_usp1_2ch;chain_usp2_2ch"),
	},

	/* USP0 Playback */
	{
		.name = __S("chain_usp0_2ch"),
		.trg_fe_name = __S("USP0"),
		.trg_channels = 2,
		.links = __S("lk_usp0_pass_2;lk_pass_src_2;lk_src_split;"
				"lk_split_upeq;lk_upeq_bass_1;lk_upeq_bass_2;"
				"lk_bass_1_delay;lk_bass_2_delay;lk_delay_s1peq;"
				"lk_delay_s2peq;lk_delay_s3peq;lk_delay_s4peq;"
				"lk_s1peq_mixer;lk_s2peq_mixer;lk_s3peq_mixer;"
				"lk_s4peq_mixer;lk_mixer_mixer2;lk_mixer2_volctrl;"
				"lk_aecref_1mic_iacc;lk_volctrl_aecref_1mic"),
		.mutexs = __S("chain_music_1;chain_music_2;chain_music_4;"
				"chain_cvc_send_1mic;chain_cvc_recv;chain_lin_to_lout_1;"
				"chain_lin_to_lout_2;chain_i2s_to_iacc_2;"
				"chain_usp1_2ch;chain_usp2_2ch;chain_a2dp_2ch"),
	},

	/* USP1 Playback */
	{
		.name = __S("chain_usp1_2ch"),
		.trg_fe_name = __S("USP1"),
		.trg_channels = 2,
		.links = __S("lk_usp1_pass_2;lk_pass_src_2;lk_src_split;"
				"lk_split_upeq;lk_upeq_bass_1;lk_upeq_bass_2;"
				"lk_bass_1_delay;lk_bass_2_delay;lk_delay_s1peq;"
				"lk_delay_s2peq;lk_delay_s3peq;lk_delay_s4peq;"
				"lk_s1peq_mixer;lk_s2peq_mixer;lk_s3peq_mixer;"
				"lk_s4peq_mixer;lk_mixer_mixer2;lk_mixer2_volctrl;"
				"lk_aecref_1mic_iacc;lk_volctrl_aecref_1mic"),
		.mutexs = __S("chain_music_1;chain_music_2;chain_music_4;"
				"chain_cvc_send_1mic;chain_cvc_recv;chain_lin_to_lout_1;"
				"chain_lin_to_lout_2;chain_i2s_to_iacc_2;"
				"chain_usp0_2ch;chain_usp2_2ch;chain_a2dp_2ch"),
	},

	/* USP2 Playback */
	{
		.name = __S("chain_usp2_2ch"),
		.trg_fe_name = __S("USP2"),
		.trg_channels = 2,
		.links = __S("lk_usp2_pass_2;lk_pass_src_2;lk_src_split;"
				"lk_split_upeq;lk_upeq_bass_1;lk_upeq_bass_2;"
				"lk_bass_1_delay;lk_bass_2_delay;lk_delay_s1peq;"
				"lk_delay_s2peq;lk_delay_s3peq;lk_delay_s4peq;"
				"lk_s1peq_mixer;lk_s2peq_mixer;lk_s3peq_mixer;"
				"lk_s4peq_mixer;lk_mixer_mixer2;lk_mixer2_volctrl;"
				"lk_aecref_1mic_iacc;lk_volctrl_aecref_1mic"),
		.mutexs = __S("chain_music_1;chain_music_2;chain_music_4;"
				"chain_cvc_send_1mic;chain_cvc_recv;chain_lin_to_lout_1;"
				"chain_lin_to_lout_2;chain_i2s_to_iacc_2;"
				"chain_usp0_2ch;chain_usp1_2ch;chain_a2dp_2ch"),
	},

	/* Carplay */
	{
		.name = __S("chain_voicecall_capture"),
		.trg_fe_name = __S("Voicecall-capture"),
		.trg_channels = 1,
		.links = __S("lk_iacc_aecref_1mic;lk_aecref_1mic_cvc_send;"
				"lk_aecref_1mic_cvc_send_ref;"
				"lk_cvc_send_vocall_cap"),
		.mutexs = __S(
				"chain_lin_to_lout_1;chain_lin_to_lout_2;chain_cap;"
				"chain_cvc_send_1mic;chain_lin_to_lout_dummy"),
	},
	{
		.name = __S("chain_voicecall_playback"),
		.trg_fe_name = __S("Voicecall-playback"),
		.trg_channels = 1,
		.links = __S("lk_vocall_play_cvc_recv;lk_cvc_recv_src;"
				"lk_src_split1x2_cvc;lk_split1x2_split2x4_cvc;"
				"lk_split2x4_mixer2_cvc;lk_mixer2_volctrl;"
				"lk_aecref_1mic_iacc;lk_volctrl_aecref_1mic"),
		.mutexs = __S("chain_cvc_recv"),
	},

	/* I2S to IACC loop */
	{
		.name = __S("chain_i2s_to_iacc_2"),
		.trg_fe_name = __S("I2S-to-iacc-loopback"),
		.trg_channels = 2,
		.links = __S("lk_i2s_pass_2;lk_pass_src_2;lk_src_split;"
				"lk_split_upeq;lk_upeq_bass_1;lk_upeq_bass_2;"
				"lk_bass_1_delay;lk_bass_2_delay;lk_delay_s1peq;"
				"lk_delay_s2peq;lk_delay_s3peq;lk_delay_s4peq;"
				"lk_s1peq_mixer;lk_s2peq_mixer;lk_s3peq_mixer;"
				"lk_s4peq_mixer;lk_mixer_mixer2;lk_mixer2_volctrl;"
				"lk_aecref_1mic_iacc;lk_volctrl_aecref_1mic"),
		.mutexs = __S("chain_music_1;chain_music_2;chain_music_4;"
				"chain_a2dp_2ch;chain_lin_to_lout_1;"
				"chain_lin_to_lout_2;chain_usp0_2ch;"
				"chain_usp1_2ch;chain_usp2_2ch"),
	},
};
