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

static const struct kasdb_link link[] = {
	/* Music, 2ch */
	{
		/* Music -> Resampler, 2ch */
		.name = __S("lk_music_src"),
		.source_name = __S("Music"),
		.sink_name = __S("op_src_music"),
		.source_pins = { 1, 2 },
		.sink_pins = { 1, 2 },
		.channels = 2,
	},
	{
		/* Resampler -> Source sync, 2ch */
		.name = __S("lk_src_srcsync"),
		.source_name = __S("op_src_music"),
		.sink_name = __S("op_srcsync"),
		.source_pins = { 1, 2 },
		.sink_pins = { 1, 2 },
		.channels = 2,
	},
	{
		/* Source sync -> Passthrough, 2ch */
		.name = __S("lk_srcsync_pass"),
		.source_name = __S("op_srcsync"),
		.sink_name = __S("op_pass_music"),
		.source_pins = { 1, 2 },
		.sink_pins = { 1, 2 },
		.channels = 2,
	},
	{
		/* Passthrough -> Bass, 2ch */
		.name = __S("lk_pass_bass"),
		.source_name = __S("op_pass_music"),
		.sink_name = __S("op_bass_music"),
		.source_pins = { 1, 2 },
		.sink_pins = { 1, 2 },
		.channels = 2,
	},
	{
		/* Bass -> Splitter, 2ch */
		.name = __S("lk_bass_split"),
		.source_name = __S("op_bass_music"),
		.sink_name = __S("op_split_music"),
		.source_pins = { 1, 2 },
		.sink_pins = { 1, 2 },
		.channels = 2,
	},
	{
		/* Splitter -> User PEQ 4ch (1-4) */
		.name = __S("lk_split_upeq"),
		.source_name = __S("op_split_music"),
		.sink_name = __S("op_upeq_music"),
		.source_pins = { 1, 2, 3, 4},
		.sink_pins = { 1, 3, 2, 4},
		.channels = 4,
	},
	{
		/* User PEQ -> Delay, 4ch */
		.name = __S("lk_upeq_delay"),
		.source_name = __S("op_upeq_music"),
		.sink_name = __S("op_delay_music"),
		.source_pins = {1, 2, 3, 4 },
		.sink_pins = {1, 2, 3, 4},
		.channels = 4,
	},
	{
		/* Delay -> Spk1 PEQ, 1ch (1) */
		.name = __S("lk_delay_s1peq"),
		.source_name = __S("op_delay_music"),
		.sink_name = __S("op_spk1_peq_music"),
		.source_pins = { 1},
		.sink_pins = { 1},
		.channels = 1,
	},
	{
		/* Delay -> Spk2 PEQ, 1ch (2) */
		.name = __S("lk_delay_s2peq"),
		.source_name = __S("op_delay_music"),
		.sink_name = __S("op_spk2_peq_music"),
		.source_pins = { 2},
		.sink_pins = { 1},
		.channels = 1,
	},
	{
		/* Delay -> Spk3 PEQ, 1ch (3) */
		.name = __S("lk_delay_s3peq"),
		.source_name = __S("op_delay_music"),
		.sink_name = __S("op_spk3_peq_music"),
		.source_pins = { 3},
		.sink_pins = { 1},
		.channels = 1,
	},
	{
		/* Delay -> Spk4 PEQ, 1ch (4) */
		.name = __S("lk_delay_s4peq"),
		.source_name = __S("op_delay_music"),
		.sink_name = __S("op_spk4_peq_music"),
		.source_pins = { 4},
		.sink_pins = { 1},
		.channels = 1,
	},
	{
		/* Spk1 PEQ -> Mixer, 1ch (1) */
		.name = __S("lk_s1peq_mixer"),
		.source_name = __S("op_spk1_peq_music"),
		.sink_name = __S("op_mixer"),
		.source_pins = { 1},
		.sink_pins = { 7},
		.channels = 1,
	},
	{
		/* Spk2 PEQ -> Mixer, 1ch (2) */
		.name = __S("lk_s2peq_mixer"),
		.source_name = __S("op_spk2_peq_music"),
		.sink_name = __S("op_mixer"),
		.source_pins = { 1},
		.sink_pins = { 8},
		.channels = 1,
	},
	{
		/* Spk3 PEQ -> Mixer, 1ch (3) */
		.name = __S("lk_s3peq_mixer"),
		.source_name = __S("op_spk3_peq_music"),
		.sink_name = __S("op_mixer"),
		.source_pins = { 1},
		.sink_pins = { 9},
		.channels = 1,
	},
	{
		/* Spk4 PEQ -> Mixer, 1ch (4) */
		.name = __S("lk_s4peq_mixer"),
		.source_name = __S("op_spk4_peq_music"),
		.sink_name = __S("op_mixer"),
		.source_pins = { 1},
		.sink_pins = { 10},
		.channels = 1,
	},

	/* Navigation, 4ch */
	{
		/* Navigation -> Source-sync, 2ch */
		.name = __S("lk_navi_srcsync"),
		.source_name = __S("Navigation"),
		.sink_name = __S("op_srcsync"),
		.source_pins = { 1, 2 },
		.sink_pins = { 9, 10 },
		.channels = 2,
	},
	{
		/* Source-sync -> Mixer1, 2ch */
		.name = __S("lk_srcsync_mixer"),
		.source_name = __S("op_srcsync"),
		.sink_name = __S("op_mixer"),
		.source_pins = { 3, 4 },
		.sink_pins = { 1, 2 },
		.channels = 2,
	},

	/* Alarm, 1ch */
	{
		/* Alarm -> Resampler, 1ch */
		.name = __S("lk_alarm_src"),
		.source_name = __S("Alarm"),
		.sink_name = __S("op_src_alarm"),
		.source_pins = { 1 },
		.sink_pins = { 1 },
		.channels = 1,
	},
	{
		/* Resampler -> Source-sync, 1ch */
		.name = __S("lk_src_srcsync_alarm"),
		.source_name = __S("op_src_alarm"),
		.sink_name = __S("op_srcsync"),
		.source_pins = { 1 },
		.sink_pins = { 11 },
		.channels = 1,
	},
	{
		/* Source-sync -> Splitter_1x2, 1ch */
		.name = __S("lk_srcsync_split_1x2"),
		.source_name = __S("op_srcsync"),
		.sink_name = __S("op_split_alarm_1x2"),
		.source_pins = { 5 },
		.sink_pins = { 1 },
		.channels = 1,
	},
	{
		/* Splitter_1x2 -> Splitter_2x4, 2ch */
		.name = __S("lk_alarm_split"),
		.source_name = __S("op_split_alarm_1x2"),
		.sink_name = __S("op_split_alarm"),
		.source_pins = { 1, 2 },
		.sink_pins = { 1, 2 },
		.channels = 2,
	},
	{
		/* Splitter -> Mixer1, 4ch (9-12) */
		.name = __S("lk_alarm_mixer"),
		.source_name = __S("op_split_alarm"),
		.sink_name = __S("op_mixer"),
		.source_pins = { 1, 2, 3, 4 },
		.sink_pins = { 3, 4, 5, 6 },
		.channels = 4,
	},

	/* Mixer -> IACC, 4ch */
	{
		/* Mixer1 -> Mixer2, 4ch */
		.name = __S("lk_mixer_mixer2"),
		.source_name = __S("op_mixer"),
		.sink_name = __S("op_mixer2"),
		.source_pins = { 1, 2, 3, 4 },
		.sink_pins = { 1, 2, 3, 4 },
		.channels = 4,
	},
	{
		/* Mixer2 -> Volume Control, 4ch */
		.name = __S("lk_mixer2_volctrl"),
		.source_name = __S("op_mixer2"),
		.sink_name = __S("op_volume_control"),
		.source_pins = { 1, 2, 3, 4 },
		.sink_pins = { 1, 3, 5, 7 },
		.channels = 4,
	},
	{
		/* Volume Control -> AEC-Ref, 4ch */
		.name = __S("lk_volctrl_aecref"),
		.source_name = __S("op_volume_control"),
		.sink_name = __S("op_aecref"),
		.source_pins = { 1, 2, 3, 4 },
		.sink_pins = { 1, 2, 7, 8 },
		.channels = 4,
	},
	{
		/* AEC-Ref -> IACC, 4ch */
		.name = __S("lk_aecref_codec"),
		.source_name = __S("op_aecref"),
		.sink_name = __S(SI_CODEC),
		.source_pins = { 2, 3, 8, 9 },
		.sink_pins = { 1, 2, 3, 4 },
		.channels = 4,
	},
	/* CVC send & recv */
	{
		/* IACC -> AEC-Ref 2 Mic */
		.name = __S("lk_codec_aecref_2mic"),
		.source_name = __S(SO_CODEC_2MIC),
		.sink_name = __S("op_aecref"),
		.source_pins = { 1, 2 },
		.sink_pins = { 3, 4 },
		.channels = 2,
	},
	{
		/* AEC-Ref 2 Mic -> CVC send */
		.name = __S("lk_aecref_cvc_send_2mic"),
		.source_name = __S("op_aecref"),
		.sink_name = __S("op_send_cvc"),
		.source_pins = { 4, 5 },
		.sink_pins = { 2, 3 },
		.channels = 2,
	},
	{
		/* IACC -> AEC-Ref 1 Mic */
		.name = __S("lk_codec_aecref_1mic"),
		.source_name = __S(SO_CODEC),
		.sink_name = __S("op_aecref"),
		.source_pins = { 1 },
		.sink_pins = { 3 },
		.channels = 1,
	},
	{
		/* AEC-Ref 1 Mic -> CVC send */
		.name = __S("lk_aecref_cvc_send_1mic"),
		.source_name = __S("op_aecref"),
		.sink_name = __S("op_send_cvc"),
		.source_pins = { 4 },
		.sink_pins = { 2 },
		.channels = 1,
	},
	{
		/* AEC-Ref  -> CVC send ref */
		.name = __S("lk_aecref_cvc_send_ref"),
		.source_name = __S("op_aecref"),
		.sink_name = __S("op_send_cvc"),
		.source_pins = { 1 },
		.sink_pins = { 1 },
		.channels = 1,
	},
	{
		/* CVC send -> USP3  */
		.name = __S("lk_cvc_send_usp3"),
		.source_name = __S("op_send_cvc"),
		.sink_name = __S("si_usp3"),
		.source_pins = { 1 },
		.sink_pins = { 1 },
		.channels = 1,
	},
	{
		/* USP3 -> CVC recv */
		.name = __S("lk_usp3_cvc_recv"),
		.source_name = __S("so_usp3"),
		.sink_name = __S("op_recv_cvc"),
		.source_pins = { 1 },
		.sink_pins = { 1 },
		.channels = 1,
	},
	{
		/* CVC recv -> SRC */
		.name = __S("lk_cvc_recv_src"),
		.source_name = __S("op_recv_cvc"),
		.sink_name = __S("op_src_cvc"),
		.source_pins = { 1 },
		.sink_pins = { 1 },
		.channels = 1,
	},
	{
		/* CVC: SRC -> Source-sync, 1 ch */
		.name = __S("lk_src_srcsync_cvc"),
		.source_name = __S("op_src_cvc"),
		.sink_name = __S("op_srcsync"),
		.source_pins = { 1 },
		.sink_pins = { 12 },
		.channels = 1,
	},
	{
		/* CVC: Source-sync -> spliter1x2, 1 ch */
		.name = __S("lk_srcsync_split1x2_cvc"),
		.source_name = __S("op_srcsync"),
		.sink_name = __S("op_split1x2_cvc"),
		.source_pins = { 6 },
		.sink_pins = { 1 },
		.channels = 1,
	},
	{
		/* CVC: spliter1x2 -> mixer2, 2 ch */
		.name = __S("lk_split1x2_mixer2_cvc"),
		.source_name = __S("op_split1x2_cvc"),
		.sink_name = __S("op_mixer2"),
		.source_pins = { 1, 2 },
		.sink_pins = { 5, 6 },
		.channels = 2,
	},
	{
		/* CVC send -> USP1  */
		.name = __S("lk_cvc_send_usp1"),
		.source_name = __S("op_send_cvc"),
		.sink_name = __S("si_usp1"),
		.source_pins = { 1 },
		.sink_pins = { 1 },
		.channels = 1,
	},
	{
		/* USP1 -> CVC recv */
		.name = __S("lk_usp1_cvc_recv"),
		.source_name = __S("so_usp1"),
		.sink_name = __S("op_recv_cvc"),
		.source_pins = { 1 },
		.sink_pins = { 1 },
		.channels = 1,
	},

	/* Microphone: Analog Capture, Mono */
	{
		/* IACC -> Resampler, 1ch */
		.name = __S("lk_codec_src_1ch"),
		.source_name = __S(SO_CODEC),
		.sink_name = __S("op_src_cap"),
		.source_pins = { 1 },
		.sink_pins = { 1 },
		.channels = 1,
	},
	{
		/* Resampler -> Passthrough, 1ch */
		.name = __S("lk_src_pass_1ch"),
		.source_name = __S("op_src_cap"),
		.sink_name = __S("op_pass_cap"),
		.source_pins = { 1 },
		.sink_pins = { 1 },
		.channels = 1,
	},
	{
		/* Passthrough -> Analog Capture, 1ch */
		.name = __S("lk_pass_cap_1ch"),
		.source_name = __S("op_pass_cap"),
		.sink_name = __S("AnalogCapture"),
		.source_pins = { 1 },
		.sink_pins = { 1 },
		.channels = 1,
	},
	/* Microphone: Analog Capture, Stereo */
	{
		/* IACC -> Resampler, 2ch */
		.name = __S("lk_codec_src_2ch"),
		.source_name = __S(SO_CODEC),
		.sink_name = __S("op_src_cap"),
		.source_pins = { 1, 2 },
		.sink_pins = { 1, 2 },
		.channels = 2,
	},
	{
		/* Resampler -> Passthrough, 2ch */
		.name = __S("lk_src_pass_2ch"),
		.source_name = __S("op_src_cap"),
		.sink_name = __S("op_pass_cap"),
		.source_pins = { 1, 2 },
		.sink_pins = { 1, 2 },
		.channels = 2,
	},
	{
		/* Passthrough -> Analog Capture, 2ch */
		.name = __S("lk_pass_cap_2ch"),
		.source_name = __S("op_pass_cap"),
		.sink_name = __S("AnalogCapture"),
		.source_pins = { 1, 2 },
		.sink_pins = { 1, 2 },
		.channels = 2,
	},

	/* Line-In */
	{
		/* CODEC -> Resampler, 2ch */
		.name = __S("lk_codec_src"),
		.source_name = __S(SO_CODEC),
		.sink_name = __S("op_src_cap"),
		.source_pins = { 1, 2 },
		.sink_pins = { 1, 2 },
		.channels = 2,
	},
	{
		/* Resampler -> Source sync, 2ch */
		.name = __S("lk_src_srcsync_lin"),
		.source_name = __S("op_src_cap"),
		.sink_name = __S("op_srcsync"),
		.source_pins = { 1, 2 },
		.sink_pins = { 3, 4 },
		.channels = 2,
	},

	/* USP */
	{
		/* usp3 -> Source sync, 2ch */
		.name = __S("lk_usp3_srcsync"),
		.source_name = __S("so_usp3"),
		.sink_name = __S("op_srcsync"),
		.source_pins = { 1, 2 },
		.sink_pins = { 5, 6 },
		.channels = 2,
	},
	{
		/* usp2 -> Resampler, 2ch */
		.name = __S("lk_usp2_src"),
		.source_name = __S("so_usp2"),
		.sink_name = __S("op_src_radio"),
		.source_pins = { 1, 2 },
		.sink_pins = { 1, 2 },
		.channels = 2,
	},
	{
		/* usp0 -> Resampler, 2ch */
		.name = __S("lk_usp0_src"),
		.source_name = __S("so_usp0"),
		.sink_name = __S("op_src_radio"),
		.source_pins = { 1, 2 },
		.sink_pins = { 1, 2 },
		.channels = 2,
	},
	{
		/* Radio resampler -> , 2ch */
		.name = __S("lk_src_srcsync_radio"),
		.source_name = __S("op_src_radio"),
		.sink_name = __S("op_srcsync"),
		.source_pins = { 1, 2 },
		.sink_pins = { 7, 8 },
		.channels = 2,
	},

	/* Carplay */
	{
		/* CVC send -> Voicecall capture, 1ch */
		.name = __S("lk_cvc_send_vocall_cap"),
		.source_name = __S("op_send_cvc"),
		.sink_name = __S("Voicecall-capture"),
		.source_pins = { 1 },
		.sink_pins = { 1 },
		.channels = 1,
	},
	{
		/* Voicecall playback -> CVC recv */
		.name = __S("lk_vocall_play_cvc_recv"),
		.source_name = __S("Voicecall-playback"),
		.sink_name = __S("op_recv_cvc"),
		.source_pins = { 1 },
		.sink_pins = { 1 },
		.channels = 1,
	},

	/* I2S */
	{
		/* I2S -> Resampler, 2ch */
		.name = __S("lk_i2s_src"),
		.source_name = __S("so_i2s_radio"),
		.sink_name = __S("op_src_radio"),
		.source_pins = { 1, 2 },
		.sink_pins = { 1, 2 },
		.channels = 2,
	},
};
