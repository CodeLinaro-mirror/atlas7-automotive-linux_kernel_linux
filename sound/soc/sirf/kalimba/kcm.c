/*
 * kailimba components PCM drive
 *
 * Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
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

#include <linux/dma-mapping.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "dsp.h"
#include "ipc.h"
#include "kcm.h"
#include "kerror.h"

#define BUFF_BYTES_EACH_CHANNEL		192
#define BUFF_BYTES_IACC_SCO_PLAYBACK	192
#define BUFF_BYTES_IACC_SCO_CAPTURE	192
#define BUFF_BYTES_USP_SCO_PLAYBACK	480
#define BUFF_BYTES_USP_SCO_CAPTURE	480

#define SET_PRIMARY_STREAM		0
#define CLEAR_PRIMARY_STREAM		1


static struct kcm_t *kcm;

static struct component components_global[512];
static int pipeline_link[TOTAL_SUPPORT_STREAMS][64];
static int pipeline_link_count[TOTAL_SUPPORT_STREAMS];
static u16 sw_endpoint_id[TOTAL_SUPPORT_STREAMS][8];
static int sw_channels[TOTAL_SUPPORT_STREAMS];
static u16 resample_op_id[TOTAL_SUPPORT_STREAMS];
static u16 sw_endpoint_connect_id[TOTAL_SUPPORT_STREAMS][8];
static int music_passthrough;
static int music_splitter;
static int music_resampler;
static int music_usr_peq;
static int music_dbe[2];
static int music_delay;
static int music_spk_peq[4];
static int mixer_1, mixer_2, mixer_3;
static int volume_ctrl;
static int aec_ref;

static int cvc_post_resampler;
static int cvc_rcv;
static int cvc_send;
static int cvc_rcv_24k;
static int cvc_send_24k;

static int iacc_source;
static int iacc_voicecall_source;
static int iacc_sink;

static int music_mixer3_to_passthrough_connection[2];
static int music_passthrough_to_resampler_connection[2];
static int music_resampler_to_splitter_connection[2];
static int music_splitter_to_usrpeq_connection[4];
static int music_usrpeq_to_dbe_connection[4];
static int music_dbe_to_delay_connection[4];
static int music_delay_to_spkpeq_connection[4];
static int music_spkpeq_to_mixer1_connection[4];
static int mixer1_to_mixer2_connection[4];
static int mixer2_to_volumectrl_connection[4];
static int volumectrl_to_aecref_connection[4];
static int aecref_to_iaccsink_connection[4];

static u16 aecref_to_cvcsend_ref_connect_id;

static int voicecall_splitter_1, voicecall_splitter_2;
static int cvcrcv_to_resampler_connection;
static int cvcrcv24k_to_resampler_connection;
static int resampler_to_voicecall_splitter1_connection;
static int voicecall_splitter1_to_voicecall_splitter2_connection[2];
static int voicecall_splitter2_to_mixer2_connection[4];
static int iaccsource_to_aecref_connection[2];
static int aecref_to_cvcsend_mic_connection[2];
static int aecref_to_cvcsend24k_mic_connection[2];
static u16 aec_ref_sample_rate_config[2] = {48000, 16000};
static u16 aec_ref_sample_rate_config_24k[2] = {48000, 24000};

static unsigned long active_stream;

static u16 mixer1_default_streams_volume[MIXER_SUPPORT_STREAMS];
static u16 mixer2_default_streams_volume[MIXER_SUPPORT_STREAMS];
static u16 mixer3_default_streams_volume[MIXER_SUPPORT_STREAMS];
static u16 mixer1_default_streams_channel_volume
	[1 + MIXER_SUPPORT_CHANNELS * 2] = {
	12, /* total channels */
	0, 0, 1, 0, 2, 0, 3, 0,  /* stream1 channels */
	4, 0, 5, 0, 6, 0, 7, 0,  /* stream2 channels */
	8, 0, 9, 0, 10, 0, 11, 0 /* stream3 channels */
};
static u16 mixer2_default_streams_channel_volume
	[1 + MIXER_SUPPORT_CHANNELS * 2] = {
	12, /* total channels */
	0, 0, 1, 0, 2, 0, 3, 0,  /* stream1 channels */
	4, 0, 5, 0, 6, 0, 7, 0,  /* stream2 channels */
	8, 0, 9, 0, 10, 0, 11, 0 /* stream3 channels */
};
static u16 volume_ctrl_default_volumes[4][4] = {
	{1, 0x10, 0, 0},
	{1, 0x11, 0, 0},
	{1, 0x12, 0, 0},
	{1, 0x13, 0, 0}
};
static u16 volume_ctrl_default_master_volume[4] = {
	1, 0x21, 0, 0
};
static u16 music_passthrough_default_volume;
static u16 music_peqs_defaule_control_mode[PEQ_NUM_MAX][4] = {
	{1, 1, 0, 2},
	{1, 1, 0, 2},
	{1, 1, 0, 2},
	{1, 1, 0, 2},
	{1, 1, 0, 2}
};
static u16 music_peqs_default_params[PEQ_NUM_MAX]
	[PEQ_MSG_PARAMS_ARRAY_LEN_16B] = {
	{1, 0, 44},
	{1, 0, 44},
	{1, 0, 44},
	{1, 0, 44},
	{1, 0, 44}
};
static u16 music_dbe_defaule_control_mode[4] = {
	1, 1, 0, 2
};
static u16 music_dbe_default_params[DBE_MSG_PARAMS_ARRAY_LEN_16B] = {
	1, 1, 7
};
static u16 music_delay_default_params[DELAY_MSG_PARAMS_ARRAY_LEN_16B] = {
	1, 0, 8
};

void set_default_music_delay_params(int offset, int val)
{
	put24bit((u8 *)(&music_delay_default_params[3]), offset, val);
}

void set_default_music_dbe_params(int offset, int val)
{
	put24bit((u8 *)(&music_dbe_default_params[3]), offset, val);
}

void set_default_music_dbe_control(u16 mode)
{
	music_dbe_defaule_control_mode[3] = mode;
}

void set_default_music_peq_params(int index, int offset, int val)
{
	put24bit((u8 *)(&music_peqs_default_params[index][3]), offset, val);
}

void set_default_music_peq_control(int index, u16 mode)
{
	music_peqs_defaule_control_mode[index][3] = mode;
}

void set_default_music_passthrough_volume(u16 volume)
{
	music_passthrough_default_volume = volume;
}

void set_default_volume_ctrl_volume(int channel, u32 volume)
{
	volume_ctrl_default_volumes[channel][2] = (u16)(volume >> 16);
	volume_ctrl_default_volumes[channel][3] = (u16)(volume & 0xffff);
}

void set_default_master_volume(u32 volume)
{
	volume_ctrl_default_master_volume[2] = (u16)(volume >> 16);
	volume_ctrl_default_master_volume[3] = (u16)(volume & 0xffff);
}

void set_default_mixer_stream_volume(int stream, u16 volume)
{
	int i = 0;

	if (stream < MIXER_SUPPORT_STREAMS) {
		mixer1_default_streams_volume[stream] = volume;
		for (i = 0; i < 4; i++)
			set_default_mixer_stream_channel_volume(stream,
							i, volume);
	} else {
		mixer2_default_streams_volume[stream - MIXER_SUPPORT_STREAMS] =
			volume;
		for (i = 0; i < 4; i++)
			set_default_mixer_stream_channel_volume(stream,
							i, volume);
	}
}

void set_default_mixer_stream_channel_volume(int stream, int channel,
	u16 volume)
{
	u16 index = 0;

	if (stream < MIXER_SUPPORT_STREAMS) {
		index = 2 + stream * 8 + channel * 2;
		mixer1_default_streams_channel_volume[index] = volume;
	} else {
		index = 2 + (stream - MIXER_SUPPORT_STREAMS) * 8 + channel * 2;
		mixer2_default_streams_channel_volume[index] = volume;
	}
}

static int init_music_mono_pipeline(int index)
{
	int i = index, k, j = 0;
	int music_mono_passthrough_to_resampler_connection;
	int music_splitter_1_to_2, music_splitter_2_to_4;
	int music_mono_resampler_to_splitter_1_to_2_connection;
	int music_mono_resampler_to_splitter_2_to_4_connection[2];
	int music_mono_splitter_to_usrpeq_connection[4];

	resample_op_id[MUSIC_MONO_STREAM] = music_resampler;

	music_mono_passthrough_to_resampler_connection = i;
	components_global[i].component_id = CONNECT_REQ;
	components_global[i].params[0] =
		(u32)(&components_global[music_passthrough].ret[0]);
	components_global[i].params[1] = 0x2000;
	components_global[i].params[2] =
		(u32)(&components_global[music_resampler].ret[0]);
	components_global[i].params[3] = 0xA000;
	i++;

	components_global[i].component_id = CREATE_OPERATOR_REQ;
	components_global[i].params[0] = CAPABILITY_ID_SPLITTER;
	music_splitter_1_to_2 = i;
	i++;

	components_global[i].component_id = CREATE_OPERATOR_REQ;
	components_global[i].params[0] = CAPABILITY_ID_SPLITTER;
	music_splitter_2_to_4 = i;
	i++;

	music_mono_resampler_to_splitter_1_to_2_connection = i;
	components_global[i].component_id = CONNECT_REQ;
	components_global[i].params[0] =
		(u32)(&components_global[music_resampler].ret[0]);
	components_global[i].params[1] = 0x2000;
	components_global[i].params[2] =
		(u32)(&components_global[music_splitter_1_to_2].ret[0]);
	components_global[i].params[3] = 0xA000;
	i++;

	for (k = 0; k < 2; k++) {
		music_mono_resampler_to_splitter_2_to_4_connection[k] = i;
		components_global[i].component_id = CONNECT_REQ;
		components_global[i].params[0] =
			(u32)(&components_global[music_splitter_1_to_2].ret[0]);
		components_global[i].params[1] = 0x2000 + k;
		components_global[i].params[2] =
			(u32)(&components_global[music_splitter_2_to_4].ret[0]);
		components_global[i].params[3] = 0xA000 + k;
		i++;
	}

	for (k = 0; k < 4; k++) {
		music_mono_splitter_to_usrpeq_connection[k] = i;
		components_global[i].component_id = CONNECT_REQ;
		components_global[i].params[0] =
			(u32)(&components_global[music_splitter_2_to_4].ret[0]);
		components_global[i].params[1] = 0x2000 + k;
		components_global[i].params[2] =
			(u32)(&components_global[music_usr_peq].ret[0]);
		components_global[i].params[3] = 0xA000 + k;
		i++;
	}

	/* Init pipeline link */
	pipeline_link[MUSIC_MONO_STREAM][j++] = music_passthrough;
	pipeline_link[MUSIC_MONO_STREAM][j++] = music_resampler;
	pipeline_link[MUSIC_MONO_STREAM][j++] =
		music_mono_passthrough_to_resampler_connection;
	pipeline_link[MUSIC_MONO_STREAM][j++] = music_splitter_1_to_2;
	pipeline_link[MUSIC_MONO_STREAM][j++] =
		music_mono_resampler_to_splitter_1_to_2_connection;
	pipeline_link[MUSIC_MONO_STREAM][j++] = music_splitter_2_to_4;
	for (k = 0; k < 2; k++)
		pipeline_link[MUSIC_MONO_STREAM][j++] =
			music_mono_resampler_to_splitter_2_to_4_connection[k];
	pipeline_link[MUSIC_MONO_STREAM][j++] = music_usr_peq;
	for (k = 0; k < 4; k++)
		pipeline_link[MUSIC_MONO_STREAM][j++] =
			music_mono_splitter_to_usrpeq_connection[k];
	for (k = 0; k < 2; k++)
		pipeline_link[MUSIC_MONO_STREAM][j++] = music_dbe[k];
	for (k = 0; k < 4; k++)
		pipeline_link[MUSIC_MONO_STREAM][j++] =
			music_usrpeq_to_dbe_connection[k];
	pipeline_link[MUSIC_MONO_STREAM][j++] = music_delay;
	for (k = 0; k < 4; k++)
		pipeline_link[MUSIC_MONO_STREAM][j++] =
			music_dbe_to_delay_connection[k];
	for (k = 0; k < 4; k++)
		pipeline_link[MUSIC_MONO_STREAM][j++] = music_spk_peq[k];
	for (k = 0; k < 4; k++)
		pipeline_link[MUSIC_MONO_STREAM][j++] =
			music_delay_to_spkpeq_connection[k];
	pipeline_link[MUSIC_MONO_STREAM][j++] = mixer_1;
	for (k = 0; k < 4; k++)
		pipeline_link[MUSIC_MONO_STREAM][j++] =
			music_spkpeq_to_mixer1_connection[k];
	pipeline_link[MUSIC_MONO_STREAM][j++] = mixer_2;
	for (k = 0; k < 4; k++)
		pipeline_link[MUSIC_MONO_STREAM][j++] =
			mixer1_to_mixer2_connection[k];
	pipeline_link[MUSIC_MONO_STREAM][j++] = volume_ctrl;
	for (k = 0; k < 4; k++)
		pipeline_link[MUSIC_MONO_STREAM][j++] =
			mixer2_to_volumectrl_connection[k];
	pipeline_link[MUSIC_MONO_STREAM][j++] = aec_ref;
	pipeline_link[MUSIC_MONO_STREAM][j++] = iacc_sink;
	for (k = 0; k < 4; k++)
		pipeline_link[MUSIC_MONO_STREAM][j++] =
			aecref_to_iaccsink_connection[k];
	for (k = 0; k < 4; k++)
		pipeline_link[MUSIC_MONO_STREAM][j++] =
			volumectrl_to_aecref_connection[k];
	pipeline_link_count[MUSIC_MONO_STREAM] = j;
	return i;
}

static int init_music_4channels_pipeline(int index)
{
	int i = index, k, j = 0;
	int music_4channels_passthrough_to_resampler_connection[4];
	int music_4channels_resampler_to_usrpeq_connection[4];

	resample_op_id[MUSIC_4CHANNELS_STREAM] = music_resampler;
	for (k = 0; k < 4; k++) {
		music_4channels_passthrough_to_resampler_connection[k] = i;
		components_global[i].component_id = CONNECT_REQ;
		components_global[i].params[0] =
			(u32)(&components_global[
				music_passthrough].ret[0]);
		components_global[i].params[1] = 0x2000 + k;
		components_global[i].params[2] =
			(u32)(&components_global[
				music_resampler].ret[0]);
		components_global[i].params[3] = 0xA000 + k;
		i++;
	}

	for (k = 0; k < 4; k++) {
		music_4channels_resampler_to_usrpeq_connection[k] = i;
		components_global[i].component_id = CONNECT_REQ;
		components_global[i].params[0] =
			(u32)(&components_global[
			music_resampler].ret[0]);
		components_global[i].params[1] = 0x2000 + k;
		components_global[i].params[2] =
			(u32)(&components_global[music_usr_peq].ret[0]);
		components_global[i].params[3] = 0xA000 + k;
		i++;
	}

	/* Init pipeline link */
	pipeline_link[MUSIC_4CHANNELS_STREAM][j++] = music_passthrough;
	pipeline_link[MUSIC_4CHANNELS_STREAM][j++] = music_resampler;
	for (k = 0; k < 4; k++)
		pipeline_link[MUSIC_4CHANNELS_STREAM][j++] =
			music_4channels_passthrough_to_resampler_connection[k];
	pipeline_link[MUSIC_4CHANNELS_STREAM][j++] = music_usr_peq;
	for (k = 0; k < 4; k++)
		pipeline_link[MUSIC_4CHANNELS_STREAM][j++] =
			music_4channels_resampler_to_usrpeq_connection[k];
	for (k = 0; k < 2; k++)
		pipeline_link[MUSIC_4CHANNELS_STREAM][j++] = music_dbe[k];
	for (k = 0; k < 4; k++)
		pipeline_link[MUSIC_4CHANNELS_STREAM][j++] =
			music_usrpeq_to_dbe_connection[k];
	pipeline_link[MUSIC_4CHANNELS_STREAM][j++] = music_delay;
	for (k = 0; k < 4; k++)
		pipeline_link[MUSIC_4CHANNELS_STREAM][j++] =
			music_dbe_to_delay_connection[k];
	for (k = 0; k < 4; k++)
		pipeline_link[MUSIC_4CHANNELS_STREAM][j++] = music_spk_peq[k];
	for (k = 0; k < 4; k++)
		pipeline_link[MUSIC_4CHANNELS_STREAM][j++] =
			music_delay_to_spkpeq_connection[k];
	pipeline_link[MUSIC_4CHANNELS_STREAM][j++] = mixer_1;
	for (k = 0; k < 4; k++)
		pipeline_link[MUSIC_4CHANNELS_STREAM][j++] =
			music_spkpeq_to_mixer1_connection[k];
	pipeline_link[MUSIC_4CHANNELS_STREAM][j++] = mixer_2;
	for (k = 0; k < 4; k++)
		pipeline_link[MUSIC_4CHANNELS_STREAM][j++] =
			mixer1_to_mixer2_connection[k];
	pipeline_link[MUSIC_4CHANNELS_STREAM][j++] = volume_ctrl;
	for (k = 0; k < 4; k++)
		pipeline_link[MUSIC_4CHANNELS_STREAM][j++] =
			mixer2_to_volumectrl_connection[k];
	pipeline_link[MUSIC_4CHANNELS_STREAM][j++] = aec_ref;
	pipeline_link[MUSIC_4CHANNELS_STREAM][j++] = iacc_sink;
	for (k = 0; k < 4; k++)
		pipeline_link[MUSIC_4CHANNELS_STREAM][j++] =
			aecref_to_iaccsink_connection[k];
	for (k = 0; k < 4; k++)
		pipeline_link[MUSIC_4CHANNELS_STREAM][j++] =
			volumectrl_to_aecref_connection[k];
	pipeline_link_count[MUSIC_4CHANNELS_STREAM] = j;
	return i;
}
static int init_music_stereo_pipeline(void)
{
	int i = 0, k, j = 0;
	static u16 aec_ref_ucid = 4;
	static u16 mixer_oper_conf_channels[MIXER_SUPPORT_STREAMS] = {
		0x4, 0x4, 0x4};
	static u16 mixer3_oper_conf_channels[MIXER_SUPPORT_STREAMS] = {
		0x2, 0x2, 0x2};
	/*
	 * Mixer need set the sample rate for avoid noise.
	 * The value is "sample rate / 25".
	 */
	static u16 mixer_sample_rate = 48000 / 25;

	mixer_3 = i;
	components_global[i].component_id = CREATE_OPERATOR_REQ;
	components_global[i].params[0] = CAPABILITY_ID_MIXER;
	components_global[i].params[1] = 3; /* Config items */
	components_global[i].params[2] = OPERATOR_MSG_SET_CHANNELS;
	components_global[i].params[3] = MIXER_SUPPORT_STREAMS;
	components_global[i].params[4] = (u32)(mixer3_oper_conf_channels);
	components_global[i].params[5] = OPMSG_COMMON_SET_SAMPLE_RATE;
	components_global[i].params[6] = 1;
	components_global[i].params[7] = (u32)(&mixer_sample_rate);
	components_global[i].params[8] = OPERATOR_MSG_SET_GAINS;
	components_global[i].params[9] = MIXER_SUPPORT_STREAMS;
	components_global[i].params[10] = (u32)(mixer3_default_streams_volume);
	i++;

	/* Music stream pipeline */
	components_global[i].component_id = CREATE_OPERATOR_REQ;
	components_global[i].params[0] = CAPABILITY_ID_BASIC_PASSTHROUGH;
	components_global[i].params[1] = 1; /* Config items */
	components_global[i].params[2] = OPERATOR_MSG_SET_PASSTHROUGH_GAIN;
	components_global[i].params[3] = 1;
	components_global[i].params[4] =
		(u32)(&music_passthrough_default_volume);
	music_passthrough = i;
	i++;

	for (k = 0; k < 2; k++) {
		music_mixer3_to_passthrough_connection[k] = i;
		components_global[i].component_id = CONNECT_REQ;
		components_global[i].params[0] =
			(u32)(&components_global[mixer_3].ret[0]);
		components_global[i].params[1] = 0x2000 + k;
		components_global[i].params[2] =
			(u32)(&components_global[music_passthrough].ret[0]);
		components_global[i].params[3] = 0xA000 + k;
		i++;

	}

	components_global[i].component_id = CREATE_OPERATOR_REQ;
	components_global[i].params[0] = CAPABILITY_ID_RESAMPLER;
	resample_op_id[MUSIC_STEREO_STREAM] = i;
	music_resampler = i;
	i++;

	for (k = 0; k < 2; k++) {
		music_passthrough_to_resampler_connection[k] = i;
		components_global[i].component_id = CONNECT_REQ;
		components_global[i].params[0] =
			(u32)(&components_global[music_passthrough].ret[0]);
		components_global[i].params[1] = 0x2000 + k;
		components_global[i].params[2] =
			(u32)(&components_global[music_resampler].ret[0]);
		components_global[i].params[3] = 0xA000 + k;
		i++;
	}

	components_global[i].component_id = CREATE_OPERATOR_REQ;
	components_global[i].params[0] = CAPABILITY_ID_SPLITTER;
	music_splitter = i;
	i++;

	for (k = 0; k < 2; k++) {
		music_resampler_to_splitter_connection[k] = i;
		components_global[i].component_id = CONNECT_REQ;
		components_global[i].params[0] =
			(u32)(&components_global[music_resampler].ret[0]);
		components_global[i].params[1] = 0x2000 + k;
		components_global[i].params[2] =
			(u32)(&components_global[music_splitter].ret[0]);
		components_global[i].params[3] = 0xA000 + k;
		i++;
	}

	/* Create user PEQ */
	components_global[i].component_id = CREATE_OPERATOR_REQ;
	components_global[i].params[0] = CAPABILITY_ID_PEQ;
	components_global[i].params[1] = 3; /* Config items */
	components_global[i].params[2] = OPMSG_COMMON_SET_SAMPLE_RATE;
	components_global[i].params[3] = 1;
	components_global[i].params[4] = (u32)(&mixer_sample_rate);
	components_global[i].params[5] = OPMSG_COMMON_SET_PARAMS;
	components_global[i].params[6] = PEQ_MSG_PARAMS_ARRAY_LEN_16B;
	components_global[i].params[7] = (u32)(music_peqs_default_params[0]);
	components_global[i].params[8] = OPMSG_COMMON_SET_CONTROL;
	components_global[i].params[9] = 4;
	components_global[i].params[10] =
		(u32)(music_peqs_defaule_control_mode[0]);
	music_usr_peq = i;
	i++;

	music_splitter_to_usrpeq_connection[0] = i;
	components_global[i].component_id = CONNECT_REQ;
	components_global[i].params[0] =
		(u32)(&components_global[music_splitter].ret[0]);
	components_global[i].params[1] = 0x2000;
	components_global[i].params[2] =
		(u32)(&components_global[music_usr_peq].ret[0]);
	components_global[i].params[3] = 0xA000;
	i++;

	music_splitter_to_usrpeq_connection[1] = i;
	components_global[i].component_id = CONNECT_REQ;
	components_global[i].params[0] =
		(u32)(&components_global[music_splitter].ret[0]);
	components_global[i].params[1] = 0x2001;
	components_global[i].params[2] =
		(u32)(&components_global[music_usr_peq].ret[0]);
	components_global[i].params[3] = 0xA002;
	i++;

	music_splitter_to_usrpeq_connection[2] = i;
	components_global[i].component_id = CONNECT_REQ;
	components_global[i].params[0] =
		(u32)(&components_global[music_splitter].ret[0]);
	components_global[i].params[1] = 0x2002;
	components_global[i].params[2] =
		(u32)(&components_global[music_usr_peq].ret[0]);
	components_global[i].params[3] = 0xA001;
	i++;

	music_splitter_to_usrpeq_connection[3] = i;
	components_global[i].component_id = CONNECT_REQ;
	components_global[i].params[0] =
		(u32)(&components_global[music_splitter].ret[0]);
	components_global[i].params[1] = 0x2003;
	components_global[i].params[2] =
		(u32)(&components_global[music_usr_peq].ret[0]);
	components_global[i].params[3] = 0xA003;
	i++;

	/* Create DBE */
	for (k = 0; k < 2; k++) {
		music_dbe[k] = i;
		components_global[i].component_id = CREATE_OPERATOR_REQ;
		components_global[i].params[0] =
			CAPABILITY_ID_DBE_FULLBAND_IN_OUT;
		components_global[i].params[1] = 3; /* Config items */
		components_global[i].params[2] = OPMSG_COMMON_SET_SAMPLE_RATE;
		components_global[i].params[3] = 1;
		components_global[i].params[4] = (u32)(&mixer_sample_rate);
		components_global[i].params[5] = OPMSG_COMMON_SET_PARAMS;
		components_global[i].params[6] = DBE_MSG_PARAMS_ARRAY_LEN_16B;
		components_global[i].params[7] =
			(u32)(music_dbe_default_params);
		components_global[i].params[8] = OPMSG_COMMON_SET_CONTROL;
		components_global[i].params[9] = 4;
		components_global[i].params[10] =
			(u32)(music_dbe_defaule_control_mode);
		i++;
	}

	/* Connect User PEQ with DBE */
	for (k = 0; k < 4; k++) {
		music_usrpeq_to_dbe_connection[k] = i;
		components_global[i].component_id = CONNECT_REQ;
		components_global[i].params[0] =
			(u32)(&components_global[music_usr_peq].ret[0]);
		components_global[i].params[1] = 0x2000 + k;
		if (k < 2) {
			components_global[i].params[2] =
				(u32)(&components_global[music_dbe[0]].ret[0]);
			components_global[i].params[3] = 0xA000 + k;
		} else {
			components_global[i].params[2] =
				(u32)(&components_global[music_dbe[1]].ret[0]);
			components_global[i].params[3] = 0xA000 + k - 2;
		}
		i++;
	}

	/* Create delay */
	components_global[i].component_id = CREATE_OPERATOR_REQ;
	components_global[i].params[0] = CAPABILITY_ID_DELAY;
	components_global[i].params[1] = 1; /* Config items */
	components_global[i].params[2] = OPMSG_COMMON_SET_PARAMS;
	components_global[i].params[3] = DELAY_MSG_PARAMS_ARRAY_LEN_16B;
	components_global[i].params[4] = (u32)(music_delay_default_params);
	music_delay = i;
	i++;

	/* Connect DBE with delay */
	for (k = 0; k < 4; k++) {
		music_dbe_to_delay_connection[k] = i;
		components_global[i].component_id = CONNECT_REQ;
		if (k < 2) {
			components_global[i].params[0] =
				(u32)(&components_global[music_dbe[0]].ret[0]);
			components_global[i].params[1] = 0x2000 + k;
		} else {
			components_global[i].params[0] =
				(u32)(&components_global[music_dbe[1]].ret[0]);
			components_global[i].params[1] = 0x2000 + k - 2;
		}
		components_global[i].params[2] =
			(u32)(&components_global[music_delay].ret[0]);
		components_global[i].params[3] = 0xA000 + k;
		i++;
	}

	/* Create spk PEQs */
	for (k = 0; k < 4; k++) {
		music_spk_peq[k] = i;
		components_global[i].component_id = CREATE_OPERATOR_REQ;
		components_global[i].params[0] = CAPABILITY_ID_PEQ;
		components_global[i].params[1] = 3; /* Config items */
		components_global[i].params[2] = OPMSG_COMMON_SET_SAMPLE_RATE;
		components_global[i].params[3] = 1;
		components_global[i].params[4] = (u32)(&mixer_sample_rate);
		components_global[i].params[5] = OPMSG_COMMON_SET_PARAMS;
		components_global[i].params[6] = PEQ_MSG_PARAMS_ARRAY_LEN_16B;
		components_global[i].params[7] =
			(u32)(music_peqs_default_params[k + 1]);
		components_global[i].params[8] = OPMSG_COMMON_SET_CONTROL;
		components_global[i].params[9] = 4;
		components_global[i].params[10] =
			(u32)(music_peqs_defaule_control_mode[k + 1]);
		i++;
	}

	/* Connect delay with spk PEQs */
	for (k = 0; k < 4; k++) {
		music_delay_to_spkpeq_connection[k] = i;
		components_global[i].component_id = CONNECT_REQ;
		components_global[i].params[0] =
			(u32)(&components_global[music_delay].ret[0]);
		components_global[i].params[1] = 0x2000 + k;
		components_global[i].params[2] =
			(u32)(&components_global[music_spk_peq[k]].ret[0]);
		components_global[i].params[3] = 0xA000;
		i++;
	}

	mixer_1 = i;
	components_global[i].component_id = CREATE_OPERATOR_REQ;
	components_global[i].params[0] = CAPABILITY_ID_MIXER;
	components_global[i].params[1] = 4; /* Config items */
	components_global[i].params[2] = OPERATOR_MSG_SET_CHANNELS;
	components_global[i].params[3] = MIXER_SUPPORT_STREAMS;
	components_global[i].params[4] = (u32)(mixer_oper_conf_channels);
	components_global[i].params[5] = OPMSG_COMMON_SET_SAMPLE_RATE;
	components_global[i].params[6] = 1;
	components_global[i].params[7] = (u32)(&mixer_sample_rate);
	components_global[i].params[8] = OPERATOR_MSG_SET_GAINS;
	components_global[i].params[9] = MIXER_SUPPORT_STREAMS;
	components_global[i].params[10] = (u32)(mixer1_default_streams_volume);
	components_global[i].params[11] = OPERATOR_MSG_SET_CHANNEL_GAINS;
	components_global[i].params[12] = 1 + MIXER_SUPPORT_CHANNELS * 2;
	components_global[i].params[13] =
		(u32)(mixer1_default_streams_channel_volume);
	i++;

	mixer_2 = i;
	components_global[i].component_id = CREATE_OPERATOR_REQ;
	components_global[i].params[0] = CAPABILITY_ID_MIXER;
	components_global[i].params[1] = 4; /* Config items */
	components_global[i].params[2] = OPERATOR_MSG_SET_CHANNELS;
	components_global[i].params[3] = MIXER_SUPPORT_STREAMS;
	components_global[i].params[4] = (u32)(mixer_oper_conf_channels);
	components_global[i].params[5] = OPMSG_COMMON_SET_SAMPLE_RATE;
	components_global[i].params[6] = 1;
	components_global[i].params[7] = (u32)(&mixer_sample_rate);
	components_global[i].params[8] = OPERATOR_MSG_SET_GAINS;
	components_global[i].params[9] = MIXER_SUPPORT_STREAMS;
	components_global[i].params[10] = (u32)(mixer2_default_streams_volume);
	components_global[i].params[11] = OPERATOR_MSG_SET_CHANNEL_GAINS;
	components_global[i].params[12] = 1 + MIXER_SUPPORT_CHANNELS * 2;
	components_global[i].params[13] =
		(u32)(mixer2_default_streams_channel_volume);
	i++;

	/* Mixer first connect to mixer second */
	for (k = 0; k < 4; k++) {
		mixer1_to_mixer2_connection[k] = i;
		components_global[i].component_id = CONNECT_REQ;
		components_global[i].params[0] =
			(u32)(&components_global[mixer_1].ret[0]);
		components_global[i].params[1] = 0x2000 + k;
		components_global[i].params[2] =
			(u32)(&components_global[mixer_2].ret[0]);
		components_global[i].params[3] = 0xA000 + k;
		i++;
	}

	/* Connect spk PEQs with Mixer stream 1 */
	for (k = 0; k < 4; k++) {
		music_spkpeq_to_mixer1_connection[k] = i;
		components_global[i].component_id = CONNECT_REQ;
		components_global[i].params[0] =
			(u32)(&components_global[music_spk_peq[k]].ret[0]);
		components_global[i].params[1] = 0x2000;
		components_global[i].params[2] =
			(u32)(&components_global[mixer_1].ret[0]);
		components_global[i].params[3] = 0xA000 + k;
		i++;
	}

	volume_ctrl = i;
	components_global[i].component_id = CREATE_OPERATOR_REQ;
	components_global[i].params[0] = CAPABILITY_ID_VOLUME_CONTROL;
	components_global[i].params[1] = 5; /* Config items */
	components_global[i].params[2] = OPERATOR_MSG_VOLUME_CTRL_SET_CONTROL;
	components_global[i].params[3] = 4;
	components_global[i].params[4] = (u32)(volume_ctrl_default_volumes[0]);
	components_global[i].params[5] = OPERATOR_MSG_VOLUME_CTRL_SET_CONTROL;
	components_global[i].params[6] = 4;
	components_global[i].params[7] = (u32)(volume_ctrl_default_volumes[1]);
	components_global[i].params[8] = OPERATOR_MSG_VOLUME_CTRL_SET_CONTROL;
	components_global[i].params[9] = 4;
	components_global[i].params[10] = (u32)(volume_ctrl_default_volumes[2]);
	components_global[i].params[11] = OPERATOR_MSG_VOLUME_CTRL_SET_CONTROL;
	components_global[i].params[12] = 4;
	components_global[i].params[13] = (u32)(volume_ctrl_default_volumes[3]);
	components_global[i].params[14] = OPERATOR_MSG_VOLUME_CTRL_SET_CONTROL;
	components_global[i].params[15] = 4;
	components_global[i].params[16] =
		(u32)(volume_ctrl_default_master_volume);
	i++;

	/* Connect Mixer with Volume control */
	for (k = 0; k < 4; k++) {
		mixer2_to_volumectrl_connection[k] = i;
		components_global[i].component_id = CONNECT_REQ;
		components_global[i].params[0] =
			(u32)(&components_global[mixer_2].ret[0]);
		components_global[i].params[1] = 0x2000 + k;
		components_global[i].params[2] =
			(u32)(&components_global[volume_ctrl].ret[0]);
		components_global[i].params[3] = 0xA000 + k * 2;
		i++;
	}

	/* AEC-Ref */
	components_global[i].component_id = CREATE_OPERATOR_REQ;
	if (enable_2mic_cvc)
		components_global[i].params[0] = CAPABILITY_ID_AEC_REF_2MIC;
	else
		components_global[i].params[0] = CAPABILITY_ID_AEC_REF_1MIC;
	components_global[i].params[1] = 2;
	components_global[i].params[2] = AEC_REF_SET_SAMPLE_RATES;
	components_global[i].params[3] = ARRAY_SIZE(aec_ref_sample_rate_config);
	components_global[i].params[4] = (u32)(aec_ref_sample_rate_config);
	components_global[i].params[5] = OPERATOR_MSG_SET_UCID;
	components_global[i].params[6] = 1;
	components_global[i].params[7] = (u32)(&aec_ref_ucid);
	aec_ref = i;
	i++;

	/* Connect Volume control to AEC-REF */
	volumectrl_to_aecref_connection[0] = i;
	components_global[i].component_id = CONNECT_REQ;
	components_global[i].params[0] =
		(u32)(&components_global[volume_ctrl].ret[0]);
	components_global[i].params[1] = 0x2000;
	components_global[i].params[2] =
		(u32)(&components_global[aec_ref].ret[0]);
	components_global[i].params[3] = 0xA000;
	i++;

	volumectrl_to_aecref_connection[1] = i;
	components_global[i].component_id = CONNECT_REQ;
	components_global[i].params[0] =
		(u32)(&components_global[volume_ctrl].ret[0]);
	components_global[i].params[1] = 0x2001;
	components_global[i].params[2] =
		(u32)(&components_global[aec_ref].ret[0]);
	components_global[i].params[3] = 0xA001;
	i++;

	volumectrl_to_aecref_connection[2] = i;
	components_global[i].component_id = CONNECT_REQ;
	components_global[i].params[0] =
		(u32)(&components_global[volume_ctrl].ret[0]);
	components_global[i].params[1] = 0x2002;
	components_global[i].params[2] =
		(u32)(&components_global[aec_ref].ret[0]);
	components_global[i].params[3] = 0xA006;
	i++;

	volumectrl_to_aecref_connection[3] = i;
	components_global[i].component_id = CONNECT_REQ;
	components_global[i].params[0] =
		(u32)(&components_global[volume_ctrl].ret[0]);
	components_global[i].params[1] = 0x2003;
	components_global[i].params[2] =
		(u32)(&components_global[aec_ref].ret[0]);
	components_global[i].params[3] = 0xA007;
	i++;

	iacc_sink = i;
	/* Get Sink */
	components_global[i].component_id = GET_SINK_REQ;
	components_global[i].params[0] = ENDPOINT_TYPE_IACC;
	components_global[i].params[1] = ENDPOINT_PHY_DEV_IACC;
	components_global[i].params[2] = (u32)(&kcm->playback_iacc_ep);
	components_global[i].params[3] = 5; /* Config items */
	components_global[i].params[4] = ENDPOINT_CONF_AUDIO_SAMPLE_RATE;
	components_global[i].params[5] =
			(u32)(&kcm->playback_iacc_ep.sample_rate);
	components_global[i].params[6] = ENDPOINT_CONF_AUDIO_DATA_FORMAT;
	components_global[i].params[7] =
			(u32)(&kcm->playback_iacc_ep.audio_data_format);
	components_global[i].params[8] = ENDPOINT_CONF_DRAM_PACKING_FORMAT;
	components_global[i].params[9] =
			(u32)(&kcm->playback_iacc_ep.packing_format);
	components_global[i].params[10] = ENDPOINT_CONF_INTERLEAVING_MODE;
	components_global[i].params[11] =
			(u32)(&kcm->playback_iacc_ep.interleaving_format);
	components_global[i].params[12] = ENDPOINT_CONF_CLOCK_MASTER;
	components_global[i].params[13] =
			(u32)(&kcm->playback_iacc_ep.clock_master);
	i++;

	/* Connect AEC REF with hw_sink */
	aecref_to_iaccsink_connection[0] = i;
	components_global[i].component_id = CONNECT_REQ;
	components_global[i].params[0] =
		(u32)(&components_global[aec_ref].ret[0]);
	components_global[i].params[1] = 0x2001;
	components_global[i].params[2] =
		(u32)(&components_global[iacc_sink].ret[0]);
	components_global[i].params[3] = 0;
	i++;

	aecref_to_iaccsink_connection[1] = i;
	components_global[i].component_id = CONNECT_REQ;
	components_global[i].params[0] =
		(u32)(&components_global[aec_ref].ret[0]);
	components_global[i].params[1] = 0x2002;
	components_global[i].params[2] =
		(u32)(&components_global[iacc_sink].ret[1]);
	components_global[i].params[3] = 0;
	i++;

	aecref_to_iaccsink_connection[2] = i;
	components_global[i].component_id = CONNECT_REQ;
	components_global[i].params[0] =
		(u32)(&components_global[aec_ref].ret[0]);
	components_global[i].params[1] = 0x2007;
	components_global[i].params[2] =
		(u32)(&components_global[iacc_sink].ret[2]);
	components_global[i].params[3] = 0;
	i++;

	aecref_to_iaccsink_connection[3] = i;
	components_global[i].component_id = CONNECT_REQ;
	components_global[i].params[0] =
		(u32)(&components_global[aec_ref].ret[0]);
	components_global[i].params[1] = 0x2008;
	components_global[i].params[2] =
		(u32)(&components_global[iacc_sink].ret[3]);
	components_global[i].params[3] = 0;
	i++;

	/* Init pipeline link */
	pipeline_link[MUSIC_STEREO_STREAM][j++] = mixer_3;
	pipeline_link[MUSIC_STEREO_STREAM][j++] = music_passthrough;
	for (k = 0; k < 2; k++)
		pipeline_link[MUSIC_STEREO_STREAM][j++] =
			music_mixer3_to_passthrough_connection[k];
	pipeline_link[MUSIC_STEREO_STREAM][j++] = music_resampler;
	for (k = 0; k < 2; k++)
		pipeline_link[MUSIC_STEREO_STREAM][j++] =
			music_passthrough_to_resampler_connection[k];
	pipeline_link[MUSIC_STEREO_STREAM][j++] = music_splitter;
	for (k = 0; k < 2; k++)
		pipeline_link[MUSIC_STEREO_STREAM][j++] =
			music_resampler_to_splitter_connection[k];
	pipeline_link[MUSIC_STEREO_STREAM][j++] = music_usr_peq;
	for (k = 0; k < 4; k++)
		pipeline_link[MUSIC_STEREO_STREAM][j++] =
			music_splitter_to_usrpeq_connection[k];
	for (k = 0; k < 2; k++)
		pipeline_link[MUSIC_STEREO_STREAM][j++] = music_dbe[k];
	for (k = 0; k < 4; k++)
		pipeline_link[MUSIC_STEREO_STREAM][j++] =
			music_usrpeq_to_dbe_connection[k];
	pipeline_link[MUSIC_STEREO_STREAM][j++] = music_delay;
	for (k = 0; k < 4; k++)
		pipeline_link[MUSIC_STEREO_STREAM][j++] =
			music_dbe_to_delay_connection[k];
	for (k = 0; k < 4; k++)
		pipeline_link[MUSIC_STEREO_STREAM][j++] = music_spk_peq[k];
	for (k = 0; k < 4; k++)
		pipeline_link[MUSIC_STEREO_STREAM][j++] =
			music_delay_to_spkpeq_connection[k];
	pipeline_link[MUSIC_STEREO_STREAM][j++] = mixer_1;
	for (k = 0; k < 4; k++)
		pipeline_link[MUSIC_STEREO_STREAM][j++] =
			music_spkpeq_to_mixer1_connection[k];
	pipeline_link[MUSIC_STEREO_STREAM][j++] = mixer_2;
	for (k = 0; k < 4; k++)
		pipeline_link[MUSIC_STEREO_STREAM][j++] =
			mixer1_to_mixer2_connection[k];
	pipeline_link[MUSIC_STEREO_STREAM][j++] = volume_ctrl;
	for (k = 0; k < 4; k++)
		pipeline_link[MUSIC_STEREO_STREAM][j++] =
			mixer2_to_volumectrl_connection[k];
	pipeline_link[MUSIC_STEREO_STREAM][j++] = aec_ref;
	pipeline_link[MUSIC_STEREO_STREAM][j++] = iacc_sink;
	for (k = 0; k < 4; k++)
		pipeline_link[MUSIC_STEREO_STREAM][j++] =
			aecref_to_iaccsink_connection[k];
	for (k = 0; k < 4; k++)
		pipeline_link[MUSIC_STEREO_STREAM][j++] =
			volumectrl_to_aecref_connection[k];
	pipeline_link_count[MUSIC_STEREO_STREAM] = j;
	return i;
}

static int init_navigation_pipeline(int index)
{
	int i = index;
	int k, j = 0;
	int passthrough_to_mixer1_connection[4];
	int passthrough;

	/* Navigation stream pipeline */
	components_global[i].component_id = CREATE_OPERATOR_REQ;
	components_global[i].params[0] = CAPABILITY_ID_BASIC_PASSTHROUGH;
	passthrough = i;
	i++;

	/* Connect resample with Mixer stream 1 */
	for (k = 0; k < 4; k++) {
		passthrough_to_mixer1_connection[k] = i;
		components_global[i].component_id = CONNECT_REQ;
		components_global[i].params[0] =
			(u32)(&components_global[passthrough].ret[0]);
		components_global[i].params[1] = 0x2000 + k;
		components_global[i].params[2] =
			(u32)(&components_global[mixer_1].ret[0]);
		components_global[i].params[3] = 0xA004 + k;
		i++;
	}

	pipeline_link[NAVIGATION_STREAM][j++] = passthrough;
	pipeline_link[NAVIGATION_STREAM][j++] = mixer_1;
	for (k = 0; k < 4; k++)
		pipeline_link[NAVIGATION_STREAM][j++] =
			passthrough_to_mixer1_connection[k];
	pipeline_link[NAVIGATION_STREAM][j++] = mixer_2;
	for (k = 0; k < 4; k++)
		pipeline_link[NAVIGATION_STREAM][j++] =
			mixer1_to_mixer2_connection[k];
	pipeline_link[NAVIGATION_STREAM][j++] = volume_ctrl;
	for (k = 0; k < 4; k++)
		pipeline_link[NAVIGATION_STREAM][j++] =
			mixer2_to_volumectrl_connection[k];
	pipeline_link[NAVIGATION_STREAM][j++] = aec_ref;
	pipeline_link[NAVIGATION_STREAM][j++] = iacc_sink;
	for (k = 0; k < 4; k++)
		pipeline_link[NAVIGATION_STREAM][j++] =
			aecref_to_iaccsink_connection[k];
	for (k = 0; k < 4; k++)
		pipeline_link[NAVIGATION_STREAM][j++] =
			volumectrl_to_aecref_connection[k];
	pipeline_link_count[NAVIGATION_STREAM] = j;

	return i;
}

static int init_alarm_pipeline(int index)
{
	int i = index;
	int k, j = 0;
	int splitter_1, splitter_2;
	int resampler_to_splitter1_connection;
	int splitter1_to_splitter2_connection[2];
	int splitter2_to_mixer1_connection[4];

	/* Alarm stream pipeline */
	components_global[i].component_id = CREATE_OPERATOR_REQ;
	components_global[i].params[0] = CAPABILITY_ID_RESAMPLER;
	resample_op_id[ALARM_STREAM] = i;
	i++;

	components_global[i].component_id = CREATE_OPERATOR_REQ;
	components_global[i].params[0] = CAPABILITY_ID_SPLITTER;
	splitter_1 = i;
	i++;

	resampler_to_splitter1_connection = i;
	components_global[i].component_id = CONNECT_REQ;
	components_global[i].params[0] =
		(u32)(&components_global[resample_op_id[
		ALARM_STREAM]].ret[0]);
	components_global[i].params[1] = 0x2000;
	components_global[i].params[2] =
		(u32)(&components_global[splitter_1].ret[0]);
	components_global[i].params[3] = 0xA000;
	i++;

	components_global[i].component_id = CREATE_OPERATOR_REQ;
	components_global[i].params[0] = CAPABILITY_ID_SPLITTER;
	splitter_2 = i;
	i++;

	for (k = 0; k < 2; k++) {
		splitter1_to_splitter2_connection[k] = i;
		components_global[i].component_id = CONNECT_REQ;
		components_global[i].params[0] =
			(u32)(&components_global[splitter_1].ret[0]);
		components_global[i].params[1] = 0x2000 + k;
		components_global[i].params[2] =
			(u32)(&components_global[splitter_2].ret[0]);
		components_global[i].params[3] = 0xA000 + k;
		i++;
	}

	for (k = 0; k < 4; k++) {
		splitter2_to_mixer1_connection[k] = i;
		components_global[i].component_id = CONNECT_REQ;
		components_global[i].params[0] =
			(u32)(&components_global[splitter_2].ret[0]);
		components_global[i].params[1] = 0x2000 + k;
		components_global[i].params[2] =
			(u32)(&components_global[mixer_1].ret[0]);
		components_global[i].params[3] = 0xA008 + k;
		i++;
	}

	pipeline_link[ALARM_STREAM][j++] =
		resample_op_id[ALARM_STREAM];
	pipeline_link[ALARM_STREAM][j++] = splitter_1;
	pipeline_link[ALARM_STREAM][j++] = resampler_to_splitter1_connection;
	pipeline_link[ALARM_STREAM][j++] = splitter_2;
	for (k = 0; k < 2; k++)
		pipeline_link[ALARM_STREAM][j++] =
			splitter1_to_splitter2_connection[k];
	pipeline_link[ALARM_STREAM][j++] = mixer_1;
	for (k = 0; k < 4; k++)
		pipeline_link[ALARM_STREAM][j++] =
			splitter2_to_mixer1_connection[k];
	pipeline_link[ALARM_STREAM][j++] = mixer_2;
	for (k = 0; k < 4; k++)
		pipeline_link[ALARM_STREAM][j++] =
			mixer1_to_mixer2_connection[k];
	pipeline_link[ALARM_STREAM][j++] = volume_ctrl;
	for (k = 0; k < 4; k++)
		pipeline_link[ALARM_STREAM][j++] =
			mixer2_to_volumectrl_connection[k];
	pipeline_link[ALARM_STREAM][j++] = aec_ref;
	pipeline_link[ALARM_STREAM][j++] = iacc_sink;
	for (k = 0; k < 4; k++)
		pipeline_link[ALARM_STREAM][j++] =
			aecref_to_iaccsink_connection[k];
	for (k = 0; k < 4; k++)
		pipeline_link[ALARM_STREAM][j++] =
			volumectrl_to_aecref_connection[k];
	pipeline_link_count[ALARM_STREAM] = j;

	return i;
}

static int init_capture_mono_pipeline(int index)
{
	int i = index;
	int j = 0;
	int source_to_resampler_connection[2];

	/* Analog Capture pipeline */
	components_global[i].component_id = CREATE_OPERATOR_REQ;
	components_global[i].params[0] = CAPABILITY_ID_RESAMPLER;
	resample_op_id[CAPTURE_MONO_STREAM] = i;
	i++;

	/* Get Source */
	components_global[i].component_id = GET_SOURCE_REQ;
	components_global[i].params[0] = ENDPOINT_TYPE_IACC;
	components_global[i].params[1] = ENDPOINT_PHY_DEV_IACC;
	components_global[i].params[2] = (u32)(&kcm->capture_iacc_mono_ep);
	components_global[i].params[3] = 5; /* Config items */
	components_global[i].params[4] = ENDPOINT_CONF_AUDIO_SAMPLE_RATE;
	components_global[i].params[5] =
			(u32)(&kcm->capture_iacc_mono_ep.sample_rate);
	components_global[i].params[6] = ENDPOINT_CONF_AUDIO_DATA_FORMAT;
	components_global[i].params[7] =
			(u32)(&kcm->capture_iacc_mono_ep.audio_data_format);
	components_global[i].params[8] = ENDPOINT_CONF_DRAM_PACKING_FORMAT;
	components_global[i].params[9] =
			(u32)(&kcm->capture_iacc_mono_ep.packing_format);
	components_global[i].params[10] = ENDPOINT_CONF_INTERLEAVING_MODE;
	components_global[i].params[11] =
			(u32)(&kcm->capture_iacc_mono_ep.interleaving_format);
	components_global[i].params[12] = ENDPOINT_CONF_CLOCK_MASTER;
	components_global[i].params[13] =
			(u32)(&kcm->capture_iacc_mono_ep.clock_master);
	iacc_source = i;
	i++;

	source_to_resampler_connection[0] = i;
	components_global[i].component_id = CONNECT_REQ;
	components_global[i].params[0] =
		(u32)(&components_global[iacc_source].ret[0]);
	components_global[i].params[1] = 0;
	components_global[i].params[2] =
		(u32)(&components_global[resample_op_id[
				CAPTURE_MONO_STREAM]].ret[0]);
	components_global[i].params[3] = 0xA000;
	i++;

	pipeline_link[CAPTURE_MONO_STREAM][j++] = resample_op_id[
		CAPTURE_MONO_STREAM];
	pipeline_link[CAPTURE_MONO_STREAM][j++] = iacc_source;
	pipeline_link[CAPTURE_MONO_STREAM][j++] =
		source_to_resampler_connection[0];

	pipeline_link_count[CAPTURE_MONO_STREAM] = j;

	return i;
}

static int init_capture_stereo_pipeline(int index)
{
	int i = index;
	int k, j = 0;
	int source_to_resampler_connection[2];

	/* Analog Capture pipeline */
	components_global[i].component_id = CREATE_OPERATOR_REQ;
	components_global[i].params[0] = CAPABILITY_ID_RESAMPLER;
	resample_op_id[CAPTURE_STEREO_STREAM] = i;
	i++;

	/* Get Source */
	components_global[i].component_id = GET_SOURCE_REQ;
	components_global[i].params[0] = ENDPOINT_TYPE_IACC;
	components_global[i].params[1] = ENDPOINT_PHY_DEV_IACC;
	components_global[i].params[2] = (u32)(&kcm->capture_iacc_stereo_ep);
	components_global[i].params[3] = 5; /* Config items */
	components_global[i].params[4] = ENDPOINT_CONF_AUDIO_SAMPLE_RATE;
	components_global[i].params[5] =
			(u32)(&kcm->capture_iacc_stereo_ep.sample_rate);
	components_global[i].params[6] = ENDPOINT_CONF_AUDIO_DATA_FORMAT;
	components_global[i].params[7] =
			(u32)(&kcm->capture_iacc_stereo_ep.audio_data_format);
	components_global[i].params[8] = ENDPOINT_CONF_DRAM_PACKING_FORMAT;
	components_global[i].params[9] =
			(u32)(&kcm->capture_iacc_stereo_ep.packing_format);
	components_global[i].params[10] = ENDPOINT_CONF_INTERLEAVING_MODE;
	components_global[i].params[11] =
			(u32)(&kcm->capture_iacc_stereo_ep.interleaving_format);
	components_global[i].params[12] = ENDPOINT_CONF_CLOCK_MASTER;
	components_global[i].params[13] =
			(u32)(&kcm->capture_iacc_stereo_ep.clock_master);
	iacc_source = i;
	i++;

	for (k = 0; k < 2; k++) {
		source_to_resampler_connection[k] = i;
		components_global[i].component_id = CONNECT_REQ;
		components_global[i].params[0] =
			(u32)(&components_global[iacc_source].ret[k]);
		components_global[i].params[1] = 0;
		components_global[i].params[2] =
			(u32)(&components_global[resample_op_id[
				CAPTURE_STEREO_STREAM]].ret[0]);
		components_global[i].params[3] = 0xA000 + k;
		i++;
	}

	pipeline_link[CAPTURE_STEREO_STREAM][j++] = resample_op_id[
		CAPTURE_STEREO_STREAM];
	pipeline_link[CAPTURE_STEREO_STREAM][j++] = iacc_source;
	for (k = 0; k < 2; k++)
		pipeline_link[CAPTURE_STEREO_STREAM][j++] =
			source_to_resampler_connection[k];

	pipeline_link_count[CAPTURE_STEREO_STREAM] = j;

	return i;
}
static int init_voicecall_bt_to_iacc_pipeline(int bt_usp_port, int index)
{
	int i = index;
	int k, j = 0;
	int usp3_source;
	int usp3_sink;
	int usp3source_to_cvcrcv_connection;
	int cvcsend_to_usp3sink_connection;
	static u16 cvc_ucid = 4;
	u32 device_instance_id;

	switch (bt_usp_port) {
	case 0:
		device_instance_id = ENDPOINT_PHY_DEV_PCM0;
		break;
	case 1:
		device_instance_id = ENDPOINT_PHY_DEV_PCM1;
		break;
	case 2:
		device_instance_id = ENDPOINT_PHY_DEV_PCM2;
		break;
	case 3:
		device_instance_id = ENDPOINT_PHY_DEV_A7CA;
		break;
	default:
		WARN_ON(1);
		pr_err("Only support usp 0,1,2,3 for bluetooth\n");
		return -EINVAL;
	}

	/* Voicecall bt to IACC pipeline */
	/* Get Source */
	components_global[i].component_id = GET_SOURCE_REQ;
	components_global[i].params[0] = ENDPOINT_TYPE_USP;
	components_global[i].params[1] = device_instance_id;
	components_global[i].params[2] = (u32)(&kcm->capture_usp_sco_ep);
	components_global[i].params[3] = 5; /* Config items */
	components_global[i].params[4] = ENDPOINT_CONF_AUDIO_SAMPLE_RATE;
	components_global[i].params[5] =
			(u32)(&kcm->capture_usp_sco_ep.sample_rate);
	components_global[i].params[6] = ENDPOINT_CONF_AUDIO_DATA_FORMAT;
	components_global[i].params[7] =
			(u32)(&kcm->capture_usp_sco_ep.audio_data_format);
	components_global[i].params[8] = ENDPOINT_CONF_DRAM_PACKING_FORMAT;
	components_global[i].params[9] =
			(u32)(&kcm->capture_usp_sco_ep.packing_format);
	components_global[i].params[10] = ENDPOINT_CONF_INTERLEAVING_MODE;
	components_global[i].params[11] =
			(u32)(&kcm->capture_usp_sco_ep.interleaving_format);
	components_global[i].params[12] = ENDPOINT_CONF_CLOCK_MASTER;
	components_global[i].params[13] =
			(u32)(&kcm->capture_usp_sco_ep.clock_master);
	usp3_source = i;
	i++;

	components_global[i].component_id = CREATE_OPERATOR_REQ;
	components_global[i].params[0] = CAPABILITY_ID_CVC_RCV_WB;
	components_global[i].params[1] = 1;
	components_global[i].params[2] = OPERATOR_MSG_SET_UCID;
	components_global[i].params[3] = 1;
	components_global[i].params[4] = (u32)(&cvc_ucid);
	cvc_rcv = i;
	i++;

	usp3source_to_cvcrcv_connection = i;
	components_global[i].component_id = CONNECT_REQ;
	components_global[i].params[0] =
		(u32)(&components_global[usp3_source].ret[0]);
	components_global[i].params[1] = 0;
	components_global[i].params[2] =
		(u32)(&components_global[cvc_rcv].ret[0]);
	components_global[i].params[3] = 0xA000;
	i++;

	components_global[i].component_id = CREATE_OPERATOR_REQ;
	components_global[i].params[0] = CAPABILITY_ID_RESAMPLER;
	resample_op_id[VOICECALL_BT_TO_IACC_STREAM] = i;
	cvc_post_resampler = i;
	i++;

	cvcrcv_to_resampler_connection = i;
	components_global[i].component_id = CONNECT_REQ;
	components_global[i].params[0] =
		(u32)(&components_global[cvc_rcv].ret[0]);
	components_global[i].params[1] = 0x2000;
	components_global[i].params[2] =
		(u32)(&components_global[resample_op_id[
			VOICECALL_BT_TO_IACC_STREAM]].ret[0]);
	components_global[i].params[3] = 0xA000;
	i++;

	components_global[i].component_id = CREATE_OPERATOR_REQ;
	components_global[i].params[0] = CAPABILITY_ID_SPLITTER;
	voicecall_splitter_1 = i;
	i++;

	resampler_to_voicecall_splitter1_connection = i;
	components_global[i].component_id = CONNECT_REQ;
	components_global[i].params[0] =
		(u32)(&components_global[resample_op_id[
			VOICECALL_BT_TO_IACC_STREAM]].ret[0]);
	components_global[i].params[1] = 0x2000;
	components_global[i].params[2] =
		(u32)(&components_global[voicecall_splitter_1].ret[0]);
	components_global[i].params[3] = 0xA000;
	i++;

	components_global[i].component_id = CREATE_OPERATOR_REQ;
	components_global[i].params[0] = CAPABILITY_ID_SPLITTER;
	voicecall_splitter_2 = i;
	i++;

	for (k = 0; k < 2; k++) {
		voicecall_splitter1_to_voicecall_splitter2_connection[k] = i;
		components_global[i].component_id = CONNECT_REQ;
		components_global[i].params[0] =
			(u32)(&components_global[voicecall_splitter_1].ret[0]);
		components_global[i].params[1] = 0x2000 + k;
		components_global[i].params[2] =
			(u32)(&components_global[voicecall_splitter_2].ret[0]);
		components_global[i].params[3] = 0xA000 + k;
		i++;
	}

	for (k = 0; k < 4; k++) {
		voicecall_splitter2_to_mixer2_connection[k] = i;
		components_global[i].component_id = CONNECT_REQ;
		components_global[i].params[0] =
			(u32)(&components_global[voicecall_splitter_2].ret[0]);
		components_global[i].params[1] = 0x2000 + k;
		components_global[i].params[2] =
			(u32)(&components_global[mixer_2].ret[0]);
		components_global[i].params[3] = 0xA004 + k;
		i++;
	}

	/* Voicecall IACC to BT */
	/* Get Source */
	components_global[i].component_id = GET_SOURCE_REQ;
	components_global[i].params[0] = ENDPOINT_TYPE_IACC;
	components_global[i].params[1] = ENDPOINT_PHY_DEV_IACC;
	components_global[i].params[2] = (u32)(&kcm->capture_iacc_sco_ep);
	components_global[i].params[3] = 5; /* Config items */
	components_global[i].params[4] = ENDPOINT_CONF_AUDIO_SAMPLE_RATE;
	components_global[i].params[5] =
		(u32)(&kcm->capture_iacc_sco_ep.sample_rate);
	components_global[i].params[6] = ENDPOINT_CONF_AUDIO_DATA_FORMAT;
	components_global[i].params[7] =
		(u32)(&kcm->capture_iacc_sco_ep.audio_data_format);
	components_global[i].params[8] = ENDPOINT_CONF_DRAM_PACKING_FORMAT;
	components_global[i].params[9] =
		(u32)(&kcm->capture_iacc_sco_ep.packing_format);
	components_global[i].params[10] = ENDPOINT_CONF_INTERLEAVING_MODE;
	components_global[i].params[11] =
		(u32)(&kcm->capture_iacc_sco_ep.interleaving_format);
	components_global[i].params[12] = ENDPOINT_CONF_CLOCK_MASTER;
	components_global[i].params[13] =
		(u32)(&kcm->capture_iacc_sco_ep.clock_master);
	iacc_voicecall_source = i;
	i++;

	components_global[i].component_id = CREATE_OPERATOR_REQ;
	if (enable_2mic_cvc)
		components_global[i].params[0] =
			CAPABILITY_ID_CVCHF2MIC_SEND_WB;
	else
		components_global[i].params[0] =
			CAPABILITY_ID_CVCHF1MIC_SEND_WB;
	components_global[i].params[1] = 1;
	components_global[i].params[2] = OPERATOR_MSG_SET_UCID;
	components_global[i].params[3] = 1;
	components_global[i].params[4] = (u32)(&cvc_ucid);
	cvc_send = i;
	i++;

	components_global[i].component_id = GET_SINK_REQ;
	components_global[i].params[0] = ENDPOINT_TYPE_USP;
	components_global[i].params[1] = device_instance_id;
	components_global[i].params[2] = (u32)(&kcm->playback_usp_sco_ep);
	components_global[i].params[3] = 5; /* Config items */
	components_global[i].params[4] = ENDPOINT_CONF_AUDIO_SAMPLE_RATE;
	components_global[i].params[5] =
		(u32)(&kcm->playback_usp_sco_ep.sample_rate);
	components_global[i].params[6] = ENDPOINT_CONF_AUDIO_DATA_FORMAT;
	components_global[i].params[7] =
		(u32)(&kcm->playback_usp_sco_ep.audio_data_format);
	components_global[i].params[8] = ENDPOINT_CONF_DRAM_PACKING_FORMAT;
	components_global[i].params[9] =
		(u32)(&kcm->playback_usp_sco_ep.packing_format);
	components_global[i].params[10] = ENDPOINT_CONF_INTERLEAVING_MODE;
	components_global[i].params[11] =
		(u32)(&kcm->playback_usp_sco_ep.interleaving_format);
	components_global[i].params[12] = ENDPOINT_CONF_CLOCK_MASTER;
	components_global[i].params[13] =
		(u32)(&kcm->playback_usp_sco_ep.clock_master);
	usp3_sink = i;
	i++;

	/* MIC 1 to AEC REF */
	iaccsource_to_aecref_connection[0] = i;
	components_global[i].component_id = CONNECT_REQ;
	components_global[i].params[0] =
		(u32)(&components_global[iacc_voicecall_source].ret[0]);
	components_global[i].params[1] = 0;
	components_global[i].params[2] =
		(u32)(&components_global[aec_ref].ret[0]);
	components_global[i].params[3] = 0xA002;
	i++;

	if (enable_2mic_cvc) {
		/* MIC 2 to AEC REF */
		iaccsource_to_aecref_connection[1] = i;
		components_global[i].component_id = CONNECT_REQ;
		components_global[i].params[0] =
			(u32)(&components_global[iacc_voicecall_source].ret[1]);
		components_global[i].params[1] = 0;
		components_global[i].params[2] =
			(u32)(&components_global[aec_ref].ret[0]);
		components_global[i].params[3] = 0xA003;
		i++;
	}

	/* AEC REF to cvc send (MIC1) */
	aecref_to_cvcsend_mic_connection[0] = i;
	components_global[i].component_id = CONNECT_REQ;
	components_global[i].params[0] =
		(u32)(&components_global[aec_ref].ret[0]);
	components_global[i].params[1] = 0x2003;
	components_global[i].params[2] =
		(u32)(&components_global[cvc_send].ret[0]);
	components_global[i].params[3] = 0xA001;
	i++;

	if (enable_2mic_cvc) {
		/* AEC REF to cvc send (MIC2) */
		aecref_to_cvcsend_mic_connection[1] = i;
		components_global[i].component_id = CONNECT_REQ;
		components_global[i].params[0] =
			(u32)(&components_global[aec_ref].ret[0]);
		components_global[i].params[1] = 0x2004;
		components_global[i].params[2] =
			(u32)(&components_global[cvc_send].ret[0]);
		components_global[i].params[3] = 0xA002;
		i++;
	}

	cvcsend_to_usp3sink_connection = i;
	components_global[i].component_id = CONNECT_REQ;
	components_global[i].params[0] =
		(u32)(&components_global[cvc_send].ret[0]);
	components_global[i].params[1] = 0x2000;
	components_global[i].params[2] =
		(u32)(&components_global[usp3_sink].ret[0]);
	components_global[i].params[3] = 0;
	i++;

	pipeline_link[VOICECALL_BT_TO_IACC_STREAM][j++] = usp3_source;
	pipeline_link[VOICECALL_BT_TO_IACC_STREAM][j++] = cvc_rcv;
	pipeline_link[VOICECALL_BT_TO_IACC_STREAM][j++] =
		usp3source_to_cvcrcv_connection;
	pipeline_link[VOICECALL_BT_TO_IACC_STREAM][j++] =
		resample_op_id[VOICECALL_BT_TO_IACC_STREAM];
	pipeline_link[VOICECALL_BT_TO_IACC_STREAM][j++] =
		cvcrcv_to_resampler_connection;
	pipeline_link[VOICECALL_BT_TO_IACC_STREAM][j++] = voicecall_splitter_1;
	pipeline_link[VOICECALL_BT_TO_IACC_STREAM][j++] =
		resampler_to_voicecall_splitter1_connection;
	pipeline_link[VOICECALL_BT_TO_IACC_STREAM][j++] = voicecall_splitter_2;
	for (k = 0; k < 2; k++)
		pipeline_link[VOICECALL_BT_TO_IACC_STREAM][j++] =
		voicecall_splitter1_to_voicecall_splitter2_connection[k];
	pipeline_link[VOICECALL_BT_TO_IACC_STREAM][j++] = mixer_2;
	for (k = 0; k < 4; k++)
		pipeline_link[VOICECALL_BT_TO_IACC_STREAM][j++] =
			voicecall_splitter2_to_mixer2_connection[k];
	pipeline_link[VOICECALL_BT_TO_IACC_STREAM][j++] = volume_ctrl;
	for (k = 0; k < 4; k++)
		pipeline_link[VOICECALL_BT_TO_IACC_STREAM][j++] =
			mixer2_to_volumectrl_connection[k];
	pipeline_link[VOICECALL_BT_TO_IACC_STREAM][j++] = aec_ref;
	pipeline_link[VOICECALL_BT_TO_IACC_STREAM][j++] = iacc_sink;
	for (k = 0; k < 4; k++)
		pipeline_link[VOICECALL_BT_TO_IACC_STREAM][j++] =
			aecref_to_iaccsink_connection[k];

	pipeline_link[VOICECALL_BT_TO_IACC_STREAM][j++] = iacc_voicecall_source;
	pipeline_link[VOICECALL_BT_TO_IACC_STREAM][j++] =
		iaccsource_to_aecref_connection[0];
	if (enable_2mic_cvc)
		pipeline_link[VOICECALL_BT_TO_IACC_STREAM][j++] =
			iaccsource_to_aecref_connection[1];
	for (k = 0; k < 4; k++)
		pipeline_link[VOICECALL_BT_TO_IACC_STREAM][j++] =
			volumectrl_to_aecref_connection[k];
	pipeline_link[VOICECALL_BT_TO_IACC_STREAM][j++] = cvc_send;
	pipeline_link[VOICECALL_BT_TO_IACC_STREAM][j++] =
		aecref_to_cvcsend_mic_connection[0];
	if (enable_2mic_cvc)
		pipeline_link[VOICECALL_BT_TO_IACC_STREAM][j++] =
			aecref_to_cvcsend_mic_connection[1];
	pipeline_link[VOICECALL_BT_TO_IACC_STREAM][j++] = usp3_sink;
	pipeline_link[VOICECALL_BT_TO_IACC_STREAM][j++] =
		cvcsend_to_usp3sink_connection;

	pipeline_link_count[VOICECALL_BT_TO_IACC_STREAM] = j;

	return i;
}

static int init_usp_pipeline(int usp_port, int index)
{
	int i = index;
	int k, j = 0;
	int source_to_mixer3_connection[2];
	int usp_source;
	u32 device_instance_id;
	u32 stream_id;
	struct hw_ep_handle_buff_t *usp_hw_ep;

	switch (usp_port) {
	case 0:
		device_instance_id = ENDPOINT_PHY_DEV_PCM0;
		stream_id = USP0_TO_IACC_LOOPBACK_STREAM;
		break;
	case 1:
		device_instance_id = ENDPOINT_PHY_DEV_PCM1;
		stream_id = USP1_TO_IACC_LOOPBACK_STREAM;
		break;
	case 2:
		device_instance_id = ENDPOINT_PHY_DEV_PCM2;
		stream_id = USP2_TO_IACC_LOOPBACK_STREAM;
		break;
	case 3:
		device_instance_id = ENDPOINT_PHY_DEV_A7CA;
		stream_id = A2DP_STREAM;
		break;
	default:
		WARN_ON(1);
		pr_err("Only support usp 0,1,2,3 for bluetooth\n");
		return -EINVAL;
	}
	usp_hw_ep = &kcm->capture_usp_stereo_ep[usp_port];
	components_global[i].component_id = GET_SOURCE_REQ;
	components_global[i].params[0] = ENDPOINT_TYPE_USP;
	components_global[i].params[1] = device_instance_id;
	components_global[i].params[2] = (u32)(usp_hw_ep);
	components_global[i].params[3] = 5; /* Config items */
	components_global[i].params[4] = ENDPOINT_CONF_AUDIO_SAMPLE_RATE;
	components_global[i].params[5] = (u32)(&usp_hw_ep->sample_rate);
	components_global[i].params[6] = ENDPOINT_CONF_AUDIO_DATA_FORMAT;
	components_global[i].params[7] = (u32)(&usp_hw_ep->audio_data_format);
	components_global[i].params[8] = ENDPOINT_CONF_DRAM_PACKING_FORMAT;
	components_global[i].params[9] = (u32)(&usp_hw_ep->packing_format);
	components_global[i].params[10] = ENDPOINT_CONF_INTERLEAVING_MODE;
	components_global[i].params[11] =
		(u32)(&usp_hw_ep->interleaving_format);
	components_global[i].params[12] = ENDPOINT_CONF_CLOCK_MASTER;
	components_global[i].params[13] = (u32)(&usp_hw_ep->clock_master);
	usp_source = i;
	i++;
	resample_op_id[stream_id] = music_resampler;

	for (k = 0; k < 2; k++) {
		source_to_mixer3_connection[k] = i;
		components_global[i].component_id = CONNECT_REQ;
		components_global[i].params[0] =
			(u32)(&components_global[usp_source].ret[k]);
		components_global[i].params[1] = 0;
		components_global[i].params[2] =
			(u32)(&components_global[mixer_3].ret[0]);
		if (stream_id == A2DP_STREAM)
			components_global[i].params[3] = 0xA002 + k;
		else
			components_global[i].params[3] = 0xA004 + k;
		i++;
	}

	pipeline_link[stream_id][j++] = usp_source;
	pipeline_link[stream_id][j++] = mixer_3;
	for (k = 0; k < 2; k++)
		pipeline_link[stream_id][j++] =
			source_to_mixer3_connection[k];
	pipeline_link[stream_id][j++] = music_passthrough;
	for (k = 0; k < 2; k++)
		pipeline_link[stream_id][j++] =
			music_mixer3_to_passthrough_connection[k];
	pipeline_link[stream_id][j++] = music_resampler;
	for (k = 0; k < 2; k++)
		pipeline_link[stream_id][j++] =
			music_passthrough_to_resampler_connection[k];
	pipeline_link[stream_id][j++] = music_splitter;
	for (k = 0; k < 2; k++)
		pipeline_link[stream_id][j++] =
			music_resampler_to_splitter_connection[k];
	pipeline_link[stream_id][j++] = music_usr_peq;
	for (k = 0; k < 4; k++)
		pipeline_link[stream_id][j++] =
			music_splitter_to_usrpeq_connection[k];
	for (k = 0; k < 2; k++)
		pipeline_link[stream_id][j++] = music_dbe[k];
	for (k = 0; k < 4; k++)
		pipeline_link[stream_id][j++] =
			music_usrpeq_to_dbe_connection[k];
	pipeline_link[stream_id][j++] = music_delay;
	for (k = 0; k < 4; k++)
		pipeline_link[stream_id][j++] =
			music_dbe_to_delay_connection[k];
	for (k = 0; k < 4; k++)
		pipeline_link[stream_id][j++] = music_spk_peq[k];
	for (k = 0; k < 4; k++)
		pipeline_link[stream_id][j++] =
			music_delay_to_spkpeq_connection[k];
	pipeline_link[stream_id][j++] = mixer_1;
	for (k = 0; k < 4; k++)
		pipeline_link[stream_id][j++] =
			music_spkpeq_to_mixer1_connection[k];
	pipeline_link[stream_id][j++] = mixer_2;
	for (k = 0; k < 4; k++)
		pipeline_link[stream_id][j++] =
			mixer1_to_mixer2_connection[k];
	pipeline_link[stream_id][j++] = volume_ctrl;
	for (k = 0; k < 4; k++)
		pipeline_link[stream_id][j++] =
			mixer2_to_volumectrl_connection[k];
	pipeline_link[stream_id][j++] = aec_ref;
	pipeline_link[stream_id][j++] = iacc_sink;
	for (k = 0; k < 4; k++)
		pipeline_link[stream_id][j++] =
			aecref_to_iaccsink_connection[k];
	for (k = 0; k < 4; k++)
		pipeline_link[stream_id][j++] =
			volumectrl_to_aecref_connection[k];
	pipeline_link_count[stream_id] = j;
	return i;
}

static int init_voicecall_playback_pipeline(int index)
{
	int i = index;
	int k, j = 0;
	int resampler_to_cvcrcv_connection;
	int resampler_to_cvcrcv24k_connection;

	components_global[i].component_id = CREATE_OPERATOR_REQ;
	components_global[i].params[0] = CAPABILITY_ID_RESAMPLER;
	resample_op_id[VOICECALL_PLAYBACK_STREAM] = i;
	i++;

	if (disable_uwb_cvc) {
		resampler_to_cvcrcv_connection = i;
		components_global[i].component_id = CONNECT_REQ;
		components_global[i].params[0] =
			(u32)(&components_global[resample_op_id[
				VOICECALL_PLAYBACK_STREAM]].ret[0]);
		components_global[i].params[1] = 0x2000;
		components_global[i].params[2] =
			(u32)(&components_global[cvc_rcv].ret[0]);
		components_global[i].params[3] = 0xA000;
		i++;
	} else {
		resampler_to_cvcrcv24k_connection = i;
		components_global[i].component_id = CONNECT_REQ;
		components_global[i].params[0] =
			(u32)(&components_global[resample_op_id[
				VOICECALL_PLAYBACK_STREAM]].ret[0]);
		components_global[i].params[1] = 0x2000;
		components_global[i].params[2] =
			(u32)(&components_global[cvc_rcv_24k].ret[0]);
		components_global[i].params[3] = 0xA000;
		i++;
	}

	pipeline_link[VOICECALL_PLAYBACK_STREAM][j++] =
		resample_op_id[VOICECALL_PLAYBACK_STREAM];
	if (disable_uwb_cvc) {
		pipeline_link[VOICECALL_PLAYBACK_STREAM][j++] = cvc_rcv;
		pipeline_link[VOICECALL_PLAYBACK_STREAM][j++] =
			resampler_to_cvcrcv_connection;
		pipeline_link[VOICECALL_PLAYBACK_STREAM][j++] =
			resample_op_id[VOICECALL_BT_TO_IACC_STREAM];
		pipeline_link[VOICECALL_PLAYBACK_STREAM][j++] =
			cvcrcv_to_resampler_connection;
	} else {
		pipeline_link[VOICECALL_PLAYBACK_STREAM][j++] = cvc_rcv_24k;
		pipeline_link[VOICECALL_PLAYBACK_STREAM][j++] =
			resampler_to_cvcrcv24k_connection;
		pipeline_link[VOICECALL_PLAYBACK_STREAM][j++] =
			resample_op_id[VOICECALL_BT_TO_IACC_STREAM];
		pipeline_link[VOICECALL_PLAYBACK_STREAM][j++] =
			cvcrcv24k_to_resampler_connection;
	}
	pipeline_link[VOICECALL_PLAYBACK_STREAM][j++] = voicecall_splitter_1;
	pipeline_link[VOICECALL_PLAYBACK_STREAM][j++] =
		resampler_to_voicecall_splitter1_connection;
	pipeline_link[VOICECALL_PLAYBACK_STREAM][j++] = voicecall_splitter_2;
	for (k = 0; k < 2; k++)
		pipeline_link[VOICECALL_PLAYBACK_STREAM][j++] =
		voicecall_splitter1_to_voicecall_splitter2_connection[k];
	pipeline_link[VOICECALL_PLAYBACK_STREAM][j++] = mixer_2;
	for (k = 0; k < 4; k++)
		pipeline_link[VOICECALL_PLAYBACK_STREAM][j++] =
			voicecall_splitter2_to_mixer2_connection[k];
	pipeline_link[VOICECALL_PLAYBACK_STREAM][j++] = volume_ctrl;
	for (k = 0; k < 4; k++)
		pipeline_link[VOICECALL_PLAYBACK_STREAM][j++] =
			mixer2_to_volumectrl_connection[k];
	pipeline_link[VOICECALL_PLAYBACK_STREAM][j++] = aec_ref;
	for (k = 0; k < 4; k++)
		pipeline_link[VOICECALL_PLAYBACK_STREAM][j++] =
			volumectrl_to_aecref_connection[k];
	if (disable_uwb_cvc)
		pipeline_link[VOICECALL_PLAYBACK_STREAM][j++] = cvc_send;
	else
		pipeline_link[VOICECALL_PLAYBACK_STREAM][j++] = cvc_send_24k;
	pipeline_link[VOICECALL_PLAYBACK_STREAM][j++] =
		resample_op_id[VOICECALL_CAPTURE_STREAM];

	pipeline_link_count[VOICECALL_PLAYBACK_STREAM] = j;
	return i;
}

static int init_voicecall_capture_pipeline(int index)
{
	int i = index;
	int j = 0, k;
	int cvcsend_to_resampler_to_connection;
	int cvcsend24k_to_resampler_to_connection;
	static u16 cvc_ucid = 4;

	components_global[i].component_id = CREATE_OPERATOR_REQ;
	components_global[i].params[0] = CAPABILITY_ID_RESAMPLER;
	resample_op_id[VOICECALL_CAPTURE_STREAM] = i;
	i++;

	if (!disable_uwb_cvc) {
		/* cvc_send_24k */
		components_global[i].component_id = CREATE_OPERATOR_REQ;
		if (enable_2mic_cvc)
			components_global[i].params[0] =
				CAPABILITY_ID_CVCHF2MIC_SEND_UWB;
		else
			components_global[i].params[0] =
				CAPABILITY_ID_CVCHF1MIC_SEND_UWB;
		components_global[i].params[1] = 1;
		components_global[i].params[2] = OPERATOR_MSG_SET_UCID;
		components_global[i].params[3] = 1;
		components_global[i].params[4] = (u32)(&cvc_ucid);
		cvc_send_24k = i;
		i++;

		/* cvc_rcv_24k */
		components_global[i].component_id = CREATE_OPERATOR_REQ;
		components_global[i].params[0] = CAPABILITY_ID_CVC_RCV_UWB;
		components_global[i].params[1] = 1;
		components_global[i].params[2] = OPERATOR_MSG_SET_UCID;
		components_global[i].params[3] = 1;
		components_global[i].params[4] = (u32)(&cvc_ucid);
		cvc_rcv_24k = i;
		i++;

		/* cvc_rcv_24k to resampler */
		cvcrcv24k_to_resampler_connection = i;
		components_global[i].component_id = CONNECT_REQ;
		components_global[i].params[0] =
			(u32)(&components_global[cvc_rcv_24k].ret[0]);
		components_global[i].params[1] = 0x2000;
		components_global[i].params[2] =
			(u32)(&components_global[resample_op_id[
				VOICECALL_BT_TO_IACC_STREAM]].ret[0]);
		components_global[i].params[3] = 0xA000;
		i++;

		/* AEC REF to cvc send (MIC1) */
		aecref_to_cvcsend24k_mic_connection[0] = i;
		components_global[i].component_id = CONNECT_REQ;
		components_global[i].params[0] =
			(u32)(&components_global[aec_ref].ret[0]);
		components_global[i].params[1] = 0x2003;
		components_global[i].params[2] =
			(u32)(&components_global[cvc_send_24k].ret[0]);
		components_global[i].params[3] = 0xA001;
		i++;

		if (enable_2mic_cvc) {
			/* AEC REF to cvc send (MIC2) */
			aecref_to_cvcsend24k_mic_connection[1] = i;
			components_global[i].component_id = CONNECT_REQ;
			components_global[i].params[0] =
				(u32)(&components_global[aec_ref].ret[0]);
			components_global[i].params[1] = 0x2004;
			components_global[i].params[2] =
				(u32)(&components_global[cvc_send_24k].ret[0]);
			components_global[i].params[3] = 0xA002;
			i++;
		}
	}

	if (disable_uwb_cvc) {
		cvcsend_to_resampler_to_connection = i;
		components_global[i].component_id = CONNECT_REQ;
		components_global[i].params[0] =
			(u32)(&components_global[cvc_send].ret[0]);
		components_global[i].params[1] = 0x2000;
		components_global[i].params[2] =
			(u32)(&components_global[resample_op_id[
				VOICECALL_CAPTURE_STREAM]].ret[0]);
		components_global[i].params[3] = 0xA000;
		i++;
	} else {
		cvcsend24k_to_resampler_to_connection = i;
		components_global[i].component_id = CONNECT_REQ;
		components_global[i].params[0] =
			(u32)(&components_global[cvc_send_24k].ret[0]);
		components_global[i].params[1] = 0x2000;
		components_global[i].params[2] =
			(u32)(&components_global[resample_op_id[
				VOICECALL_CAPTURE_STREAM]].ret[0]);
		components_global[i].params[3] = 0xA000;
		i++;
	}

	pipeline_link[VOICECALL_CAPTURE_STREAM][j++] =
		resample_op_id[VOICECALL_CAPTURE_STREAM];
	if (disable_uwb_cvc) {
		pipeline_link[VOICECALL_CAPTURE_STREAM][j++] = cvc_send;
		pipeline_link[VOICECALL_CAPTURE_STREAM][j++] =
			cvcsend_to_resampler_to_connection;
	} else {
		pipeline_link[VOICECALL_CAPTURE_STREAM][j++] = cvc_send_24k;
		pipeline_link[VOICECALL_CAPTURE_STREAM][j++] =
			cvcsend24k_to_resampler_to_connection;
	}
	pipeline_link[VOICECALL_CAPTURE_STREAM][j++] = iacc_sink;
	pipeline_link[VOICECALL_CAPTURE_STREAM][j++] = aec_ref;
	for (k = 0; k < 4; k++)
		pipeline_link[VOICECALL_CAPTURE_STREAM][j++] =
			aecref_to_iaccsink_connection[k];
	pipeline_link[VOICECALL_CAPTURE_STREAM][j++] = iacc_voicecall_source;
	pipeline_link[VOICECALL_CAPTURE_STREAM][j++] =
		iaccsource_to_aecref_connection[0];
	if (disable_uwb_cvc) {
		pipeline_link[VOICECALL_CAPTURE_STREAM][j++] =
			aecref_to_cvcsend_mic_connection[0];
		if (enable_2mic_cvc) {
			pipeline_link[VOICECALL_CAPTURE_STREAM][j++] =
				iaccsource_to_aecref_connection[1];
			pipeline_link[VOICECALL_CAPTURE_STREAM][j++] =
				aecref_to_cvcsend_mic_connection[1];
		}
	} else {
		pipeline_link[VOICECALL_CAPTURE_STREAM][j++] =
			aecref_to_cvcsend24k_mic_connection[0];
		if (enable_2mic_cvc) {
			pipeline_link[VOICECALL_CAPTURE_STREAM][j++] =
				iaccsource_to_aecref_connection[1];
			pipeline_link[VOICECALL_CAPTURE_STREAM][j++] =
				aecref_to_cvcsend24k_mic_connection[1];
		}
	}

	pipeline_link_count[VOICECALL_CAPTURE_STREAM] = j;
	return i;
}

static int init_iacc_loopback_playback_pipeline(int index)
{
	int i = index;
	int j = 0, k;
	int iacc_stereo_source;
	int source_to_mixer3_connection[2];

	/* Get Source */
	components_global[i].component_id = GET_SOURCE_REQ;
	components_global[i].params[0] = ENDPOINT_TYPE_IACC;
	components_global[i].params[1] = ENDPOINT_PHY_DEV_IACC;
	components_global[i].params[2] = (u32)(&kcm->capture_iacc_stereo_ep);
	components_global[i].params[3] = 5; /* Config items */
	components_global[i].params[4] = ENDPOINT_CONF_AUDIO_SAMPLE_RATE;
	components_global[i].params[5] =
			(u32)(&kcm->capture_iacc_stereo_ep.sample_rate);
	components_global[i].params[6] = ENDPOINT_CONF_AUDIO_DATA_FORMAT;
	components_global[i].params[7] =
			(u32)(&kcm->capture_iacc_stereo_ep.audio_data_format);
	components_global[i].params[8] = ENDPOINT_CONF_DRAM_PACKING_FORMAT;
	components_global[i].params[9] =
			(u32)(&kcm->capture_iacc_stereo_ep.packing_format);
	components_global[i].params[10] = ENDPOINT_CONF_INTERLEAVING_MODE;
	components_global[i].params[11] =
			(u32)(&kcm->capture_iacc_stereo_ep.interleaving_format);
	components_global[i].params[12] = ENDPOINT_CONF_CLOCK_MASTER;
	components_global[i].params[13] =
			(u32)(&kcm->capture_iacc_stereo_ep.clock_master);
	iacc_stereo_source = i;
	i++;

	for (k = 0; k < kcm->capture_iacc_stereo_ep.channels; k++) {
		source_to_mixer3_connection[k] = i;
		components_global[i].component_id = CONNECT_REQ;
		components_global[i].params[0] =
			(u32)(&components_global[iacc_stereo_source].ret[k]);
		components_global[i].params[1] = 0;
		components_global[i].params[2] =
			(u32)(&components_global[mixer_3].ret[0]);
		components_global[i].params[3] = 0xA004 + k;
		i++;
	}

	resample_op_id[IACC_LOOPBACK_PLAYBACK_STREAM] = music_resampler;
	/* Init pipeline link */
	pipeline_link[IACC_LOOPBACK_PLAYBACK_STREAM][j++] = iacc_stereo_source;
	pipeline_link[IACC_LOOPBACK_PLAYBACK_STREAM][j++] = mixer_3;
	for (k = 0; k < kcm->capture_iacc_stereo_ep.channels; k++)
		pipeline_link[IACC_LOOPBACK_PLAYBACK_STREAM][j++] =
			source_to_mixer3_connection[k];
	pipeline_link[IACC_LOOPBACK_PLAYBACK_STREAM][j++] = music_passthrough;
	for (k = 0; k < kcm->capture_iacc_stereo_ep.channels; k++)
		pipeline_link[IACC_LOOPBACK_PLAYBACK_STREAM][j++] =
			music_mixer3_to_passthrough_connection[k];
	pipeline_link[IACC_LOOPBACK_PLAYBACK_STREAM][j++] = music_resampler;
	for (k = 0; k < 2; k++)
		pipeline_link[IACC_LOOPBACK_PLAYBACK_STREAM][j++] =
			music_passthrough_to_resampler_connection[k];
	pipeline_link[IACC_LOOPBACK_PLAYBACK_STREAM][j++] = music_splitter;
	for (k = 0; k < 2; k++)
		pipeline_link[IACC_LOOPBACK_PLAYBACK_STREAM][j++] =
			music_resampler_to_splitter_connection[k];
	pipeline_link[IACC_LOOPBACK_PLAYBACK_STREAM][j++] = music_usr_peq;
	for (k = 0; k < 4; k++)
		pipeline_link[IACC_LOOPBACK_PLAYBACK_STREAM][j++] =
			music_splitter_to_usrpeq_connection[k];
	for (k = 0; k < 2; k++)
		pipeline_link[IACC_LOOPBACK_PLAYBACK_STREAM][j++] =
			music_dbe[k];
	for (k = 0; k < 4; k++)
		pipeline_link[IACC_LOOPBACK_PLAYBACK_STREAM][j++] =
			music_usrpeq_to_dbe_connection[k];
	pipeline_link[IACC_LOOPBACK_PLAYBACK_STREAM][j++] = music_delay;
	for (k = 0; k < 4; k++)
		pipeline_link[IACC_LOOPBACK_PLAYBACK_STREAM][j++] =
			music_dbe_to_delay_connection[k];
	for (k = 0; k < 4; k++)
		pipeline_link[IACC_LOOPBACK_PLAYBACK_STREAM][j++] =
			music_spk_peq[k];
	for (k = 0; k < 4; k++)
		pipeline_link[IACC_LOOPBACK_PLAYBACK_STREAM][j++] =
			music_delay_to_spkpeq_connection[k];
	pipeline_link[IACC_LOOPBACK_PLAYBACK_STREAM][j++] = mixer_1;
	for (k = 0; k < 4; k++)
		pipeline_link[IACC_LOOPBACK_PLAYBACK_STREAM][j++] =
			music_spkpeq_to_mixer1_connection[k];
	pipeline_link[IACC_LOOPBACK_PLAYBACK_STREAM][j++] = mixer_2;
	for (k = 0; k < 4; k++)
		pipeline_link[IACC_LOOPBACK_PLAYBACK_STREAM][j++] =
			mixer1_to_mixer2_connection[k];
	pipeline_link[IACC_LOOPBACK_PLAYBACK_STREAM][j++] = volume_ctrl;
	for (k = 0; k < 4; k++)
		pipeline_link[IACC_LOOPBACK_PLAYBACK_STREAM][j++] =
			mixer2_to_volumectrl_connection[k];
	pipeline_link[IACC_LOOPBACK_PLAYBACK_STREAM][j++] = aec_ref;
	pipeline_link[IACC_LOOPBACK_PLAYBACK_STREAM][j++] = iacc_sink;
	for (k = 0; k < 4; k++)
		pipeline_link[IACC_LOOPBACK_PLAYBACK_STREAM][j++] =
			aecref_to_iaccsink_connection[k];
	for (k = 0; k < 4; k++)
		pipeline_link[IACC_LOOPBACK_PLAYBACK_STREAM][j++] =
			volumectrl_to_aecref_connection[k];
	pipeline_link_count[IACC_LOOPBACK_PLAYBACK_STREAM] = j;

	return i;
}

static int init_i2s_to_iacc_loopback_pipeline(int index)
{
	int i = index;
	int j = 0, k;
	int i2s_stereo_source;
	int source_to_mixer3_connection[2];

	/* Get Source */
	components_global[i].component_id = GET_SOURCE_REQ;
	components_global[i].params[0] = ENDPOINT_TYPE_I2S;
	components_global[i].params[1] = ENDPOINT_PHY_DEV_I2S1;
	components_global[i].params[2] = (u32)(&kcm->capture_i2s_stereo_ep);
	components_global[i].params[3] = 5; /* Config items */
	components_global[i].params[4] = ENDPOINT_CONF_AUDIO_SAMPLE_RATE;
	components_global[i].params[5] =
			(u32)(&kcm->capture_i2s_stereo_ep.sample_rate);
	components_global[i].params[6] = ENDPOINT_CONF_AUDIO_DATA_FORMAT;
	components_global[i].params[7] =
			(u32)(&kcm->capture_i2s_stereo_ep.audio_data_format);
	components_global[i].params[8] = ENDPOINT_CONF_DRAM_PACKING_FORMAT;
	components_global[i].params[9] =
			(u32)(&kcm->capture_i2s_stereo_ep.packing_format);
	components_global[i].params[10] = ENDPOINT_CONF_INTERLEAVING_MODE;
	components_global[i].params[11] =
			(u32)(&kcm->capture_i2s_stereo_ep.interleaving_format);
	components_global[i].params[12] = ENDPOINT_CONF_CLOCK_MASTER;
	components_global[i].params[13] =
			(u32)(&kcm->capture_i2s_stereo_ep.clock_master);
	i2s_stereo_source = i;
	i++;

	for (k = 0; k < kcm->capture_i2s_stereo_ep.channels; k++) {
		source_to_mixer3_connection[k] = i;
		components_global[i].component_id = CONNECT_REQ;
		components_global[i].params[0] =
			(u32)(&components_global[i2s_stereo_source].ret[k]);
		components_global[i].params[1] = 0;
		components_global[i].params[2] =
			(u32)(&components_global[mixer_3].ret[0]);
		components_global[i].params[3] = 0xA004 + k;
		i++;
	}

	resample_op_id[I2S_TO_IACC_LOOPBACK_STREAM] = music_resampler;
	/* Init pipeline link */
	pipeline_link[I2S_TO_IACC_LOOPBACK_STREAM][j++] = i2s_stereo_source;
	pipeline_link[I2S_TO_IACC_LOOPBACK_STREAM][j++] = mixer_3;
	for (k = 0; k < kcm->capture_iacc_stereo_ep.channels; k++)
		pipeline_link[I2S_TO_IACC_LOOPBACK_STREAM][j++] =
			source_to_mixer3_connection[k];
	pipeline_link[I2S_TO_IACC_LOOPBACK_STREAM][j++] =
		music_passthrough;
	for (k = 0; k < kcm->capture_iacc_stereo_ep.channels; k++)
		pipeline_link[I2S_TO_IACC_LOOPBACK_STREAM][j++] =
			music_mixer3_to_passthrough_connection[k];
	pipeline_link[I2S_TO_IACC_LOOPBACK_STREAM][j++] =
		music_resampler;
	for (k = 0; k < 2; k++)
		pipeline_link[I2S_TO_IACC_LOOPBACK_STREAM][j++] =
			music_passthrough_to_resampler_connection[k];
	pipeline_link[I2S_TO_IACC_LOOPBACK_STREAM][j++] =
		music_splitter;
	for (k = 0; k < 2; k++)
		pipeline_link[I2S_TO_IACC_LOOPBACK_STREAM][j++] =
			music_resampler_to_splitter_connection[k];
	pipeline_link[I2S_TO_IACC_LOOPBACK_STREAM][j++] =
		music_usr_peq;
	for (k = 0; k < 4; k++)
		pipeline_link[I2S_TO_IACC_LOOPBACK_STREAM][j++] =
			music_splitter_to_usrpeq_connection[k];
	for (k = 0; k < 2; k++)
		pipeline_link[I2S_TO_IACC_LOOPBACK_STREAM][j++] =
			music_dbe[k];
	for (k = 0; k < 4; k++)
		pipeline_link[I2S_TO_IACC_LOOPBACK_STREAM][j++] =
			music_usrpeq_to_dbe_connection[k];
	pipeline_link[I2S_TO_IACC_LOOPBACK_STREAM][j++] = music_delay;
	for (k = 0; k < 4; k++)
		pipeline_link[I2S_TO_IACC_LOOPBACK_STREAM][j++] =
			music_dbe_to_delay_connection[k];
	for (k = 0; k < 4; k++)
		pipeline_link[I2S_TO_IACC_LOOPBACK_STREAM][j++] =
			music_spk_peq[k];
	for (k = 0; k < 4; k++)
		pipeline_link[I2S_TO_IACC_LOOPBACK_STREAM][j++] =
			music_delay_to_spkpeq_connection[k];
	pipeline_link[I2S_TO_IACC_LOOPBACK_STREAM][j++] = mixer_1;
	for (k = 0; k < 4; k++)
		pipeline_link[I2S_TO_IACC_LOOPBACK_STREAM][j++] =
			music_spkpeq_to_mixer1_connection[k];
	pipeline_link[I2S_TO_IACC_LOOPBACK_STREAM][j++] = mixer_2;
	for (k = 0; k < 4; k++)
		pipeline_link[I2S_TO_IACC_LOOPBACK_STREAM][j++] =
			mixer1_to_mixer2_connection[k];
	pipeline_link[I2S_TO_IACC_LOOPBACK_STREAM][j++] = volume_ctrl;
	for (k = 0; k < 4; k++)
		pipeline_link[I2S_TO_IACC_LOOPBACK_STREAM][j++] =
			mixer2_to_volumectrl_connection[k];
	pipeline_link[I2S_TO_IACC_LOOPBACK_STREAM][j++] = aec_ref;
	pipeline_link[I2S_TO_IACC_LOOPBACK_STREAM][j++] = iacc_sink;
	for (k = 0; k < 4; k++)
		pipeline_link[I2S_TO_IACC_LOOPBACK_STREAM][j++] =
			aecref_to_iaccsink_connection[k];
	for (k = 0; k < 4; k++)
		pipeline_link[I2S_TO_IACC_LOOPBACK_STREAM][j++] =
			volumectrl_to_aecref_connection[k];
	pipeline_link_count[I2S_TO_IACC_LOOPBACK_STREAM] = j;

	return i;
}

static void init_pipeline(int bt_usp_port)
{
	int index;
	int i;

	index = init_music_stereo_pipeline();
	index = init_music_mono_pipeline(index);
	index = init_music_4channels_pipeline(index);
	index = init_navigation_pipeline(index);
	index = init_alarm_pipeline(index);
	index = init_capture_mono_pipeline(index);
	index = init_capture_stereo_pipeline(index);
	index = init_voicecall_bt_to_iacc_pipeline(bt_usp_port, index);
	for (i = 0; i < USP_PORTS; i++)
		index = init_usp_pipeline(i, index);
	index = init_voicecall_capture_pipeline(index);
	index = init_voicecall_playback_pipeline(index);
	index = init_iacc_loopback_playback_pipeline(index);
	init_i2s_to_iacc_loopback_pipeline(index);
}

static u16 get_rasample_conversion_conf(int input_rate, int output_rate)
{
	int sample_rate[] = {8000, 11025, 12000, 16000, 22050, 24000, 32000,
		44100, 48000};
	u16 conf = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(sample_rate); i++) {
		if (sample_rate[i] == input_rate) {
			conf = i << 4;
			break;
		}
	}
	for (i = 0; i < ARRAY_SIZE(sample_rate); i++) {
		if (sample_rate[i] == output_rate) {
			conf |= i;
			break;
		}
	}
	return conf;
}

struct conflict_pipeline {
	int stream_1;
	int stream_2;
};

/*
 * Some streams' pipeline can't work concorrency, So before prepare stream's
 * pipeline, should check the pipeline's conflict.
 */
static struct conflict_pipeline conflict_pipeline_array[] = {
	{I2S_TO_IACC_LOOPBACK_STREAM, USP0_TO_IACC_LOOPBACK_STREAM},
	{I2S_TO_IACC_LOOPBACK_STREAM, USP1_TO_IACC_LOOPBACK_STREAM},
	{I2S_TO_IACC_LOOPBACK_STREAM, USP2_TO_IACC_LOOPBACK_STREAM},
	{IACC_LOOPBACK_PLAYBACK_STREAM, USP0_TO_IACC_LOOPBACK_STREAM},
	{IACC_LOOPBACK_PLAYBACK_STREAM, USP1_TO_IACC_LOOPBACK_STREAM},
	{IACC_LOOPBACK_PLAYBACK_STREAM, USP2_TO_IACC_LOOPBACK_STREAM},
	{I2S_TO_IACC_LOOPBACK_STREAM, IACC_LOOPBACK_PLAYBACK_STREAM},
	{ANALOG_CAPTURE_STREAM, VOICECALL_BT_TO_IACC_STREAM},
	{ANALOG_CAPTURE_STREAM, VOICECALL_IACC_TO_BT_STREAM},
	{ANALOG_CAPTURE_STREAM, VOICECALL_CAPTURE_STREAM},
	{ANALOG_CAPTURE_STREAM, IACC_LOOPBACK_PLAYBACK_STREAM},
	{ANALOG_CAPTURE_STREAM, IACC_LOOPBACK_CAPTURE_STREAM},
	{VOICECALL_PLAYBACK_STREAM, VOICECALL_BT_TO_IACC_STREAM},
	{VOICECALL_PLAYBACK_STREAM, VOICECALL_IACC_TO_BT_STREAM},
	{VOICECALL_CAPTURE_STREAM, VOICECALL_BT_TO_IACC_STREAM},
	{VOICECALL_CAPTURE_STREAM, VOICECALL_IACC_TO_BT_STREAM},
	{VOICECALL_CAPTURE_STREAM, IACC_LOOPBACK_CAPTURE_STREAM},
	{VOICECALL_IACC_TO_BT_STREAM, IACC_LOOPBACK_CAPTURE_STREAM}
};

static int check_pipeline_conflict(int stream)
{
	int i;
	int ret = PIPELINE_READY;

	for (i = 0; i < ARRAY_SIZE(conflict_pipeline_array); i++) {
		if ((test_bit(conflict_pipeline_array[i].stream_1,
			&kcm->running_pipeline)) &&
			(stream == conflict_pipeline_array[i].stream_2)) {
			ret = PIPELINE_BUSY;
			goto out;
		}
		if ((test_bit(conflict_pipeline_array[i].stream_2,
			&kcm->running_pipeline)) &&
			(stream == conflict_pipeline_array[i].stream_1)) {
			ret = PIPELINE_BUSY;
			goto out;
		}
	}
out:
	return ret;
}

int open_stream(int stream)
{
	int ret;

	kalimba_msg_send_lock();
	ret = check_pipeline_conflict(stream);
	if (ret == PIPELINE_READY)
		set_bit(stream, &kcm->running_pipeline);
	kalimba_msg_send_unlock();
	return ret;
}

void close_stream(int stream)
{
	kalimba_msg_send_lock();
	clear_bit(stream, &kcm->running_pipeline);
	kalimba_msg_send_unlock();
}

static int execute_component(struct component *component);

u16 prepare_stream(int stream, int channels, u32 handle_addr, int sample_rate,
	int clock_master, int period_size)
{
	u16 resp[64];
	int i;
	int ret;
	int usp_port;
	struct component *stream_first_component =
		&components_global[pipeline_link[stream][0]];
	u16 resample_cfg;

	if ((stream == VOICECALL_PLAYBACK_STREAM
	    || stream == VOICECALL_CAPTURE_STREAM) && !disable_uwb_cvc)
		/* Set aec_ref resample configration */
		components_global[aec_ref].params[4] =
			(u32)(aec_ref_sample_rate_config_24k);
	else
		components_global[aec_ref].params[4] =
			(u32)(aec_ref_sample_rate_config);

	kalimba_msg_send_lock();
	for (i = 0;  i < pipeline_link_count[stream]; i++) {
		ret = execute_component(&components_global[
				pipeline_link[stream][i]]);
		if (ret < 0)
			goto out;
	}
	if (stream == VOICECALL_BT_TO_IACC_STREAM) {
		resample_cfg = get_rasample_conversion_conf(
				kcm->capture_usp_sco_ep.sample_rate, 48000);
		/* Set resample configration */
		ret = kalimba_operator_message(
			components_global[resample_op_id[stream]].ret[0],
				RESAMPLER_SET_CONVERSION_RATE, 1,
				&resample_cfg, NULL, NULL, resp);
		if (ret < 0)
			goto out;
	} else if (stream == CAPTURE_MONO_STREAM
		|| stream == CAPTURE_STEREO_STREAM
		|| stream == VOICECALL_CAPTURE_STREAM) {
		sw_channels[stream] = channels;
		if (stream == CAPTURE_MONO_STREAM)
			resample_cfg = get_rasample_conversion_conf(
					kcm->capture_iacc_mono_ep.sample_rate,
					sample_rate);
		else if (stream == CAPTURE_STEREO_STREAM)
			resample_cfg = get_rasample_conversion_conf(
					kcm->capture_iacc_stereo_ep.sample_rate,
					sample_rate);
		else if (stream == VOICECALL_CAPTURE_STREAM) {
			if (disable_uwb_cvc)
				resample_cfg = get_rasample_conversion_conf(
						16000, sample_rate);
			else
				resample_cfg = get_rasample_conversion_conf(
						24000, sample_rate);
		}

		ret = kalimba_get_sink(ENDPOINT_TYPE_FILE, 0, (u16)channels,
				handle_addr, sw_endpoint_id[stream], resp);
		if (ret < 0)
			goto out;
		for (i = 0; i < channels; i++) {
			ret = kalimba_config_endpoint(sw_endpoint_id[stream][i],
				ENDPOINT_CONF_AUDIO_SAMPLE_RATE,
				sample_rate, resp);
			if (ret < 0)
				goto out;
			ret = kalimba_config_endpoint(sw_endpoint_id[stream][i],
				ENDPOINT_CONF_AUDIO_DATA_FORMAT, 0, resp);
			if (ret < 0)
				goto out;
			ret = kalimba_config_endpoint(sw_endpoint_id[stream][i],
				ENDPOINT_CONF_DRAM_PACKING_FORMAT, 2, resp);
			if (ret < 0)
				goto out;
			ret = kalimba_config_endpoint(sw_endpoint_id[stream][i],
				ENDPOINT_CONF_INTERLEAVING_MODE, 1, resp);
			if (ret < 0)
				goto out;
			ret = kalimba_config_endpoint(sw_endpoint_id[stream][i],
				ENDPOINT_CONF_CLOCK_MASTER, clock_master, resp);
			if (ret < 0)
				goto out;
			ret = kalimba_config_endpoint(sw_endpoint_id[stream][i],
				ENDPOINT_CONF_PERIOD_SIZE, period_size, resp);
			if (ret < 0)
				goto out;
		}

		/* Set resample configration */
		ret = kalimba_operator_message(components_global[
			resample_op_id[stream]].ret[0],
			RESAMPLER_SET_CONVERSION_RATE, 1,
			&resample_cfg, NULL, NULL, resp);
		if (ret < 0)
			goto out;

		/* Connect resampler with sink*/
		for (i = 0; i < channels; i++)
			ret = kalimba_connect_endpoints(components_global[
				resample_op_id[stream]]
				.ret[0] + 0x2000 + i,
				sw_endpoint_id[stream][i],
				&sw_endpoint_connect_id[stream][i],
				resp);
			if (ret < 0)
				goto out;
	} else if (stream == MUSIC_STEREO_STREAM || stream == NAVIGATION_STREAM
		|| stream == MUSIC_MONO_STREAM
		|| stream == MUSIC_4CHANNELS_STREAM
		|| stream == ALARM_STREAM
		|| stream == VOICECALL_PLAYBACK_STREAM) {
		sw_channels[stream] = channels;
		if (stream == VOICECALL_PLAYBACK_STREAM) {
			if (disable_uwb_cvc)
				resample_cfg = get_rasample_conversion_conf(
					16000, 48000);
			else
				resample_cfg = get_rasample_conversion_conf(
					24000, 48000);
			/* Set resample configration */
			ret = kalimba_operator_message(
			components_global[cvc_post_resampler].ret[0],
				RESAMPLER_SET_CONVERSION_RATE, 1,
				&resample_cfg, NULL, NULL, resp);
			if (ret < 0)
				goto out;
			if (disable_uwb_cvc)
				resample_cfg = get_rasample_conversion_conf(
					sample_rate, 16000);
			else
				resample_cfg = get_rasample_conversion_conf(
					sample_rate, 24000);
		} else if (stream != NAVIGATION_STREAM)
			resample_cfg = get_rasample_conversion_conf(sample_rate,
				kcm->playback_iacc_ep.sample_rate);

		kalimba_get_source(ENDPOINT_TYPE_FILE, 0, (u16)channels,
				handle_addr, sw_endpoint_id[stream], resp);
		for (i = 0; i < channels; i++) {
			ret = kalimba_config_endpoint(sw_endpoint_id[stream][i],
				ENDPOINT_CONF_AUDIO_SAMPLE_RATE,
				sample_rate, resp);
			if (ret < 0)
				goto out;
			ret = kalimba_config_endpoint(sw_endpoint_id[stream][i],
				ENDPOINT_CONF_AUDIO_DATA_FORMAT, 0, resp);
			if (ret < 0)
				goto out;
			ret = kalimba_config_endpoint(sw_endpoint_id[stream][i],
				ENDPOINT_CONF_DRAM_PACKING_FORMAT, 2, resp);
			if (ret < 0)
				goto out;
			ret = kalimba_config_endpoint(sw_endpoint_id[stream][i],
				ENDPOINT_CONF_INTERLEAVING_MODE, 1, resp);
			if (ret < 0)
				goto out;
			ret = kalimba_config_endpoint(sw_endpoint_id[stream][i],
				ENDPOINT_CONF_PERIOD_SIZE, period_size, resp);
			if (ret < 0)
				goto out;
		}
		/* Set resample configration */
		if (stream != NAVIGATION_STREAM) {
			if (components_global[resample_op_id[stream]].
				running_refcnt == 0) {
				ret = kalimba_operator_message(
					components_global[
					resample_op_id[stream]].ret[0],
					RESAMPLER_SET_CONVERSION_RATE, 1,
					&resample_cfg, NULL, NULL, resp);
				if (ret < 0)
					goto out;
			}
		}

		/* Connect source with first operator */
		for (i = 0; i < channels; i++)
			ret = kalimba_connect_endpoints(
				sw_endpoint_id[stream][i],
				stream_first_component->ret[0] + 0xA000 + i,
				&sw_endpoint_connect_id[stream][i], resp);
			if (ret < 0)
				goto out;
	} else if (stream == A2DP_STREAM
		|| stream == USP0_TO_IACC_LOOPBACK_STREAM
		|| stream == USP1_TO_IACC_LOOPBACK_STREAM
		|| stream == USP2_TO_IACC_LOOPBACK_STREAM) {
		switch (stream) {
		case USP0_TO_IACC_LOOPBACK_STREAM:
			usp_port = 0;
			break;
		case USP1_TO_IACC_LOOPBACK_STREAM:
			usp_port = 1;
			break;
		case USP2_TO_IACC_LOOPBACK_STREAM:
			usp_port = 2;
			break;
		case A2DP_STREAM:
			usp_port = 3;
			break;
		default:
			break;
		}
		resample_cfg = get_rasample_conversion_conf(
			kcm->capture_usp_stereo_ep[usp_port].sample_rate,
			kcm->playback_iacc_ep.sample_rate);
		/* Set resample configration */
		if (components_global[resample_op_id[stream]].
				running_refcnt == 0) {
			ret = kalimba_operator_message(components_global[
				resample_op_id[stream]].ret[0],
				RESAMPLER_SET_CONVERSION_RATE, 1,
				&resample_cfg, NULL, NULL, resp);
			if (ret < 0)
				goto out;
		}
	} else if (stream == IACC_LOOPBACK_PLAYBACK_STREAM
		|| stream == I2S_TO_IACC_LOOPBACK_STREAM) {
		resample_cfg = get_rasample_conversion_conf(
			kcm->capture_iacc_stereo_ep.sample_rate,
			kcm->playback_iacc_ep.sample_rate);
		/* Set resample configration */
		if (components_global[resample_op_id[stream]].
				running_refcnt == 0) {
			ret = kalimba_operator_message(components_global[
				resample_op_id[stream]].ret[0],
				RESAMPLER_SET_CONVERSION_RATE, 1,
				&resample_cfg, NULL, NULL, resp);
			if (ret < 0)
				goto out;
		}
	}
	ret = sw_endpoint_id[stream][0];
out:
	kalimba_msg_send_unlock();
	return ret;
}

static int change_primary_stream(int action, int stream)
{
	u16 mixer1_primary_stream = 0;
	u16 mixer2_primary_stream = 0;
	u16 mixer3_primary_stream = 0;
	int mixer3_stream_count = 0;
	u16 mixer3_stream_gain = 0;
	u16 resp[64];
	int i;
	int ret;

	/*
	 * The VOICECALL_PLAYBACK_STREAM and VOICECALL_BT_TO_IACC_STREAM
	 * share same port of mixer operator.
	 */
	if (stream == VOICECALL_PLAYBACK_STREAM)
		stream = VOICECALL_BT_TO_IACC_STREAM;
	/*
	 * The IACC_LOOPBACK_PLAYBACK_STREAM,
	 * I2S_TO_IACC_LOOPBACK_STREAM and MUSIC_STEREO_STREAM
	 * share same port of mixer operator.
	 * And the IACC_LOOPBACK_CAPTURE_STREAM don't need change the
	 * primary stream, so bypass it.
	 */
	if (stream == MUSIC_MONO_STREAM
		|| stream == MUSIC_4CHANNELS_STREAM
		|| stream == MUSIC_STEREO_STREAM)
		stream = MUSIC_STREAM;
	if (stream == IACC_LOOPBACK_CAPTURE_STREAM)
		return 0;

	if (action == SET_PRIMARY_STREAM)
		set_bit(stream, &active_stream);
	else if (action == CLEAR_PRIMARY_STREAM &&
		test_bit(stream, &active_stream))
		clear_bit(stream, &active_stream);
	else
		return 0;

	if (test_bit(MUSIC_STREAM, &active_stream)) {
		mixer1_primary_stream = 1;
		mixer2_primary_stream = 1;
		mixer3_primary_stream = 1;
		mixer3_stream_count++;
	} else if (test_bit(NAVIGATION_STREAM, &active_stream)) {
		mixer1_primary_stream = 2;
		mixer2_primary_stream = 1;
	} else if (test_bit(ALARM_STREAM, &active_stream)) {
		mixer1_primary_stream = 3;
		mixer2_primary_stream = 1;
	} else if (test_bit(A2DP_STREAM, &active_stream)) {
		mixer1_primary_stream = 1;
		mixer2_primary_stream = 1;
		mixer3_primary_stream = 2;
	} else if (test_bit(USP0_TO_IACC_LOOPBACK_STREAM, &active_stream)) {
		mixer1_primary_stream = 1;
		mixer2_primary_stream = 1;
		mixer3_primary_stream = 3;
	} else if (test_bit(USP1_TO_IACC_LOOPBACK_STREAM, &active_stream)) {
		mixer1_primary_stream = 1;
		mixer2_primary_stream = 1;
		mixer3_primary_stream = 3;
	} else if (test_bit(USP2_TO_IACC_LOOPBACK_STREAM, &active_stream)) {
		mixer1_primary_stream = 1;
		mixer2_primary_stream = 1;
		mixer3_primary_stream = 3;
	} else if (test_bit(IACC_LOOPBACK_PLAYBACK_STREAM, &active_stream)) {
		mixer1_primary_stream = 1;
		mixer2_primary_stream = 1;
		mixer3_primary_stream = 3;
	} else if (test_bit(I2S_TO_IACC_LOOPBACK_STREAM, &active_stream)) {
		mixer1_primary_stream = 1;
		mixer2_primary_stream = 1;
		mixer3_primary_stream = 3;
	}
	if (test_bit(VOICECALL_BT_TO_IACC_STREAM, &active_stream))
		mixer2_primary_stream = 2;

	/*
	 * For the mixer 3, the music, a2dp, i2s and usp should be mix together.
	 * If a stream input from the audio hardware endpoint, which must be
	 * set to primary stream, it is the limitation of the mixer operator.
	 */
	if (test_bit(A2DP_STREAM, &active_stream)) {
		mixer1_primary_stream = 1;
		mixer2_primary_stream = 1;
		mixer3_primary_stream = 2;
		mixer3_stream_count++;
	}

	if (test_bit(IACC_LOOPBACK_PLAYBACK_STREAM, &active_stream) ||
		test_bit(I2S_TO_IACC_LOOPBACK_STREAM, &active_stream) ||
		test_bit(USP0_TO_IACC_LOOPBACK_STREAM, &active_stream) ||
		test_bit(USP1_TO_IACC_LOOPBACK_STREAM, &active_stream) ||
		test_bit(USP2_TO_IACC_LOOPBACK_STREAM, &active_stream))	{
		mixer1_primary_stream = 1;
		mixer2_primary_stream = 1;
		mixer3_primary_stream = 3;
		mixer3_stream_count++;
	}

	/*
	 * Base the stream counter of mixer 3, change the gain of each stream,
	 * This change avoids the saturation of the mixer output.
	 */
	if (mixer3_stream_count == 2)
		mixer3_stream_gain = 0xFE98; /* 1/2(-6dB) for each stream */
	else if (mixer3_stream_count == 3)
		mixer3_stream_gain = 0xFDC5; /* 1/3(-9.5dB) for each stream */

	for (i = 0; i < MIXER_SUPPORT_STREAMS; i++)
		mixer3_default_streams_volume[i] = mixer3_stream_gain;

	/* Some streams do not have mixer 3 operator, so bypass set */
	if (mixer3_stream_count && get_mixer_op_id(3))
		kalimba_operator_message(get_mixer_op_id(3),
			OPERATOR_MSG_SET_GAINS,
			MIXER_SUPPORT_STREAMS, mixer3_default_streams_volume,
			NULL, NULL, NULL);
	if (mixer1_primary_stream != 0) {
		if (components_global[mixer_1].primary_stream !=
				mixer1_primary_stream) {
			components_global[mixer_1].primary_stream =
				mixer1_primary_stream;
			ret = kalimba_operator_message(get_mixer_op_id(1),
				OPERATOR_MSG_SET_PRIMARY_STREAM,
				1, &mixer1_primary_stream, NULL, NULL, resp);
			if (ret < 0)
				return ret;
		}
	}
	if (mixer2_primary_stream != 0) {
		if (components_global[mixer_2].primary_stream !=
			mixer2_primary_stream) {
			components_global[mixer_2].primary_stream =
				mixer2_primary_stream;
			ret = kalimba_operator_message(get_mixer_op_id(2),
				OPERATOR_MSG_SET_PRIMARY_STREAM,
				1, &mixer2_primary_stream, NULL, NULL, resp);
			if (ret < 0)
				return ret;
		}
	}
	/* Some streams do not have mixer 3 operator, so bypass set */
	if (mixer3_primary_stream != 0 && get_mixer_op_id(3)) {
		if (components_global[mixer_3].primary_stream !=
			mixer3_primary_stream) {
			components_global[mixer_3].primary_stream =
				mixer3_primary_stream;
			ret = kalimba_operator_message(get_mixer_op_id(3),
				OPERATOR_MSG_SET_PRIMARY_STREAM,
				1, &mixer3_primary_stream, NULL, NULL, resp);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

int start_stream(int stream, int clock_master)
{
	int i;
	u16 resp[64];
	struct component *component;
	u16 should_start_ops[64];
	int should_start_ops_count = 0;
	int ret = 0;

	/*
	 * The VOICECALL_CAPTURE_STREAM does not start any operators, config
	 * and connect, these actions should  be done when start
	 * VOICECALL_PLAYBACK_STREAM
	 */
	if (stream == VOICECALL_CAPTURE_STREAM)
		return 0;
	kalimba_msg_send_lock();
	change_primary_stream(SET_PRIMARY_STREAM, stream);
	if (stream == MUSIC_STEREO_STREAM || stream == NAVIGATION_STREAM
		|| stream == MUSIC_MONO_STREAM
		|| stream == MUSIC_4CHANNELS_STREAM
		|| stream == ALARM_STREAM
		|| stream == VOICECALL_PLAYBACK_STREAM) {
		for (i = 0; i < sw_channels[stream]; i++)
			ret = kalimba_config_endpoint(sw_endpoint_id[stream][i],
				ENDPOINT_CONF_CLOCK_MASTER, clock_master, resp);
			if (ret < 0)
				goto out;
	}

	for (i = 0;  i < pipeline_link_count[stream]; i++) {
		component = &components_global[
			pipeline_link[stream][i]];

		if (component->component_id == CREATE_OPERATOR_REQ) {
			if (component->running_refcnt == 0)
				should_start_ops[should_start_ops_count++] =
					component->ret[0];
			component->running_refcnt++;
		}
	}

	if (should_start_ops_count > 0)
		ret = kalimba_start_operator(should_start_ops,
				should_start_ops_count,
				resp);
		if (ret < 0)
			goto out;

	if (stream == VOICECALL_BT_TO_IACC_STREAM)
		ret = kalimba_connect_endpoints(
			components_global[aec_ref].ret[0] + 0x2000,
			components_global[cvc_send].ret[0] + 0xA000,
			&aecref_to_cvcsend_ref_connect_id, resp);
	else if (stream == VOICECALL_PLAYBACK_STREAM) {
		if (disable_uwb_cvc)
			ret = kalimba_connect_endpoints(
				components_global[aec_ref].ret[0] + 0x2000,
				components_global[cvc_send].ret[0] + 0xA000,
				&aecref_to_cvcsend_ref_connect_id, resp);
		else
			ret = kalimba_connect_endpoints(
				components_global[aec_ref].ret[0] + 0x2000,
				components_global[cvc_send_24k].ret[0] + 0xA000,
				&aecref_to_cvcsend_ref_connect_id, resp);
	}
	if (ret < 0)
		goto out;

out:
	kalimba_msg_send_unlock();
	return ret;
}

void stop_stream(int stream)
{
	int i;
	u16 resp[64];
	struct component *component;
	u16 should_stop_ops[64];
	int should_stop_ops_count = 0;

	/*
	 * The VOICECALL_CAPTURE_STREAM does not stop any operators,
	 * and disconnect, these actions should  be done when stop
	 * VOICECALL_PLAYBACK_STREAM
	 */
	if (stream == VOICECALL_CAPTURE_STREAM)
		return;
	kalimba_msg_send_lock();
	change_primary_stream(CLEAR_PRIMARY_STREAM, stream);
	if ((stream == VOICECALL_BT_TO_IACC_STREAM
		|| stream == VOICECALL_IACC_TO_BT_STREAM
		|| stream == VOICECALL_PLAYBACK_STREAM)
			&& aecref_to_cvcsend_ref_connect_id) {
		kalimba_disconnect_endpoints(1,
			&aecref_to_cvcsend_ref_connect_id, resp);
		aecref_to_cvcsend_ref_connect_id = 0;
	}
	for (i = 0;  i < pipeline_link_count[stream]; i++) {
		component = &components_global[pipeline_link[stream][i]];

		if (stream == VOICECALL_IACC_TO_BT_STREAM &&
			component->params[0] == (enable_2mic_cvc ?
			CAPABILITY_ID_AEC_REF_2MIC :
			CAPABILITY_ID_AEC_REF_1MIC))
			continue;
		if (component->component_id == CREATE_OPERATOR_REQ) {
			if (component->running_refcnt == 1) {
				should_stop_ops[should_stop_ops_count++] =
					component->ret[0];
			}
			if (component->running_refcnt > 0)
				component->running_refcnt--;
		}
	}
	if (should_stop_ops_count > 0 && !kaschk_crash())
		kalimba_stop_operator(should_stop_ops, should_stop_ops_count,
			resp);
	kalimba_msg_send_unlock();
}

void destroy_stream(int stream)
{
	int i;
	u16 resp[64];
	struct component *component;

	kalimba_msg_send_lock();

	/* Disconnect source */
	for (i = 0; i < sw_channels[stream]; i++)
		if (!kaschk_crash())
			kalimba_disconnect_endpoints(1,
				&sw_endpoint_connect_id[stream][i], resp);
	/* Disconnect */
	for (i = 0;  i < pipeline_link_count[stream]; i++) {
		component = &components_global[pipeline_link[stream][i]];

		if (component->component_id == CONNECT_REQ) {
			if (component->create_refcnt == 1)
				if (!kaschk_crash())
					kalimba_disconnect_endpoints(1,
						&component->ret[0], resp);
			if (component->create_refcnt > 0)
				component->create_refcnt--;
		}
	}

	/* Destroy operators*/
	for (i = 0;  i < pipeline_link_count[stream]; i++) {
		component = &components_global[pipeline_link[stream][i]];

		if (component->component_id == CREATE_OPERATOR_REQ) {
			if (component->create_refcnt == 1) {
				if (!kaschk_crash())
					kalimba_destroy_operator(
						&component->ret[0],
						1, resp);
				/*
				 * Clear operator id, if this operator
				 * is destoried.
				 */
				component->ret[0] = 0;
			}
			if (component->create_refcnt > 0)
				component->create_refcnt--;
		}
	}

	/* Close Sinks */
	for (i = 0;  i < pipeline_link_count[stream]; i++) {
		component = &components_global[pipeline_link[stream][i]];

		if (component->component_id == GET_SINK_REQ) {
			if (component->create_refcnt == 1)
				if (!kaschk_crash())
					kalimba_close_sink(
						component->id_count,
						component->ret, resp);
			if (component->create_refcnt > 0)
				component->create_refcnt--;
		} else if (component->component_id == GET_SOURCE_REQ) {
			if (component->create_refcnt == 1)
				if (!kaschk_crash())
					kalimba_close_source(
						component->id_count,
						component->ret,
						resp);
			if (component->create_refcnt > 0)
				component->create_refcnt--;
		}
	}

	/* Close source or sink*/
	if (stream == CAPTURE_MONO_STREAM
		|| stream == VOICECALL_CAPTURE_STREAM
		|| stream == CAPTURE_STEREO_STREAM) {
		if (!kaschk_crash())
				kalimba_close_sink(sw_channels[stream],
					sw_endpoint_id[stream], resp);
	} else if (stream == MUSIC_STEREO_STREAM || stream == NAVIGATION_STREAM
		|| stream == MUSIC_MONO_STREAM
		|| stream == MUSIC_4CHANNELS_STREAM
		|| stream == VOICECALL_PLAYBACK_STREAM
		|| stream == ALARM_STREAM)
		if (!kaschk_crash())
			kalimba_close_source(sw_channels[stream],
				sw_endpoint_id[stream], resp);
	kalimba_msg_send_unlock();
}

int data_produced(u16 endpoint_id)
{
	int ret;

	kalimba_msg_send_lock();
	ret = kalimba_data_produced(endpoint_id);
	kalimba_msg_send_unlock();

	return ret;
}

u16 get_volume_control_op_id(void)
{
	return components_global[volume_ctrl].ret[0];
}

u16 get_mixer_op_id(int which)
{
	if (which == 1)
		return components_global[mixer_1].ret[0];
	else if (which == 2)
		return components_global[mixer_2].ret[0];
	else if (which == 3)
		return components_global[mixer_3].ret[0];
	return 0;
}

u16 get_music_passthrough_op_id(void)
{
	return components_global[music_passthrough].ret[0];
}

u16 get_peq_op_id(u16 index)
{
	if (index == 0)
		return components_global[music_usr_peq].ret[0];
	else
		return components_global[music_spk_peq[index - 1]].ret[0];
}

u16 get_dbe_op_id(u16 index)
{
	return components_global[music_dbe[index]].ret[0];
}

u16 get_delay_op_id(void)
{
	return components_global[music_delay].ret[0];
}

static int config_operator(u16 operator_id, u16 operator_type, u32 *config)
{
	u32 config_items = config[0];
	u32 i;
	u16 resp[64];
	int ret;

	for (i = 0; i < config_items; i++) {
		ret = kalimba_operator_message(operator_id,
			(u16)config[i * 3 + 1],
			(u16)config[i * 3 + 2],
			(u16 *)(config[i * 3 + 3]),
			NULL, NULL, resp);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int config_endpoint(u16 *endpoint_id, int endpoint_count, u32 *config)
{
	u32 config_items = config[0];
	int i, c, ret;
	u16 resp[64];

	for (c = 0; c < endpoint_count; c++) {
		for (i = 0; i < config_items; i++)
			ret = kalimba_config_endpoint(endpoint_id[c],
				config[i * 2 + 1], *((u16 *)config[i * 2 + 2]),
				resp);
			if (ret < 0)
				return ret;
	}

	return 0;
}

static int execute_component(struct component *component)
{
	int ret = 0;
	u16 resp[64];

	switch (component->component_id) {
	case CREATE_OPERATOR_REQ:
		if  (component->create_refcnt == 0) {
			ret = kalimba_create_operator(
					(u16)(component->params[0]),
					component->ret, resp);
			if (ret < 0)
				return ret;
			ret = config_operator((u16)(component->ret[0]),
				(u16)(component->params[0]),
				&component->params[1]);
			if (ret < 0)
				return ret;
			if (component->params[0] == CAPABILITY_ID_MIXER)
				component->primary_stream = 0;
		}
		component->create_refcnt++;
		break;
	case OPERATOR_MESSAGE_REQ:
		if ((u16)(component->params[1]) !=
				OPERATOR_MSG_SET_PRIMARY_STREAM) {
			ret = kalimba_operator_message(
				*((u16 *)(component->params[0])),
				(u16)(component->params[1]),
				(u16)(component->params[2]),
				(u16 *)(component->params[3]),
				NULL, NULL, resp);
			if (ret < 0)
				return ret;
			break;
		}
		change_primary_stream((u32)(component->params[4]),
			*(u16 *)(component->params[3]) - 1);
		break;
	case GET_SINK_REQ:
		if  (component->create_refcnt == 0) {
			struct hw_ep_handle_buff_t *hw_ep_handle =
				(struct hw_ep_handle_buff_t *)
					(component->params[2]);

			memset(hw_ep_handle->buff, 0, hw_ep_handle->buff_bytes);
			ret = kalimba_get_sink((u16)(component->params[0]),
				(u16)(component->params[1]),
				hw_ep_handle->channels,
				hw_ep_handle->handle_phy_addr,
				component->ret, resp);
			ret = config_endpoint(component->ret,
				hw_ep_handle->channels,
				&component->params[3]);
			component->id_count = hw_ep_handle->channels;
		}
		component->create_refcnt++;
		break;
	case GET_SOURCE_REQ:
		if  (component->create_refcnt == 0) {
			struct hw_ep_handle_buff_t *hw_ep_handle =
				(struct hw_ep_handle_buff_t *)
					(component->params[2]);

			memset(hw_ep_handle->buff, 0, hw_ep_handle->buff_bytes);
			ret = kalimba_get_source((u16)(component->params[0]),
					(u16)(component->params[1]),
					hw_ep_handle->channels,
					hw_ep_handle->handle_phy_addr,
					component->ret, resp);
			ret = config_endpoint(component->ret,
				hw_ep_handle->channels,
				&component->params[3]);
			component->id_count = hw_ep_handle->channels;
		}
		component->create_refcnt++;
		break;
	case ENDPOINT_CONFIGURE_REQ:
		ret = kalimba_config_endpoint(*((u16 *)(component->params[0])),
			(u16)(component->params[1]),
			*((u32 *)(component->params[2])), resp);
		break;
	case CONNECT_REQ:
		if  (component->create_refcnt == 0)
			ret = kalimba_connect_endpoints(
				*((u16 *)(component->params[0]))
				+ (u16)(component->params[1]),
				*((u16 *)(component->params[2])) +
				(u16)(component->params[3]),
				component->ret, resp);
			if (ret < 0)
				return ret;
		component->create_refcnt++;
		break;
	case START_OPERATOR_REQ:
		ret = kalimba_start_operator((u16 *)(component->params[0]),
			(u16)(component->params[1]), resp);
		break;
	case DATA_PRODUCED:
		kalimba_data_produced(*((u16 *)(component->params[0])));
		ret = 0;
		break;
	case STOP_OPERATOR_REQ:
		ret = kalimba_stop_operator((u16 *)(component->params[0]),
			(u16)(component->params[1]), resp);
		break;
	case DISCONNECT_REQ:
		ret = kalimba_disconnect_endpoints((u16)(component->params[0]),
			(u16 *)(component->params[1]), resp);
		break;
	case CLOSE_SINK_REQ:
		ret = kalimba_close_sink((u16)(component->params[0]),
			(u16 *)(component->params[1]), resp);
		break;
	case CLOSE_SOURCE_REQ:
		ret = kalimba_close_source((u16)(component->params[0]),
			(u16 *)(component->params[1]), resp);
		break;
	case DESTROY_OPERATOR_REQ:
		ret = kalimba_destroy_operator((u16 *)(component->params[0]),
			(u16)(component->params[1]), resp);
		/* Clear operator id, if this operator is destoried */
		*((u16 *)(component->params[0])) = 0;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret != 0)
		pr_err("ipc command executed failed: command id: 0x%04x\n",
			component->component_id);

	return ret;
}

static int alloc_hw_ep_handle_and_buff(struct device *dev,
	struct hw_ep_handle_buff_t *hw_ep_handle_buff,
	int buff_bytes_each_channel, int channels, int rate)
{
	hw_ep_handle_buff->audio_data_format = 0;
	hw_ep_handle_buff->packing_format = 2;
	hw_ep_handle_buff->interleaving_format = 1;
	hw_ep_handle_buff->clock_master = 1;
	hw_ep_handle_buff->sample_rate = rate;
	hw_ep_handle_buff->channels = channels;

	hw_ep_handle_buff->buff_bytes = buff_bytes_each_channel * channels;

	hw_ep_handle_buff->handle = dma_alloc_coherent(dev,
		sizeof(struct endpoint_handle),
		&hw_ep_handle_buff->handle_phy_addr, GFP_KERNEL);
	if (hw_ep_handle_buff->handle == NULL) {
		pr_err("Can't allocate playback hw endpoint handle buffer.\n");
		return -ENOMEM;
	}

	hw_ep_handle_buff->buff = dma_alloc_coherent(dev,
		hw_ep_handle_buff->buff_bytes,
		&hw_ep_handle_buff->handle->buff_addr, GFP_KERNEL);
	if (hw_ep_handle_buff->buff == NULL) {
		pr_err("Can't allocate playback hw endpoint buffer.\n");
		dma_free_coherent(dev, sizeof(struct endpoint_handle),
			hw_ep_handle_buff->handle,
			hw_ep_handle_buff->handle_phy_addr);
		return -ENOMEM;
	}
	hw_ep_handle_buff->handle->buff_length =
		hw_ep_handle_buff->buff_bytes / sizeof(u32);
	return 0;
}

static void free_hw_ep_handle_and_buff(struct device *dev,
	struct hw_ep_handle_buff_t *hw_ep_handle_buff)
{
	dma_free_coherent(dev, hw_ep_handle_buff->buff_bytes,
		hw_ep_handle_buff->buff, hw_ep_handle_buff->handle->buff_addr);
	dma_free_coherent(dev, sizeof(struct endpoint_handle),
		hw_ep_handle_buff->handle, hw_ep_handle_buff->handle_phy_addr);
}

struct kcm_t *kcm_init(int bt_usp_port, struct device *dev, int i2s_master)
{
	int ret;
	int i;
	int alloced_usp_ep = 0;

	kcm = devm_kzalloc(dev, sizeof(struct kcm_t), GFP_KERNEL);
	if (kcm == NULL)
		return ERR_PTR(-ENOMEM);

	ret = alloc_hw_ep_handle_and_buff(dev, &kcm->playback_iacc_ep,
		BUFF_BYTES_EACH_CHANNEL, 4, 48000);
	if (ret) {
		pr_err("Allocate IACC playback endpoint buffer failed.\n");
		return ERR_PTR(ret);
	}

	ret = alloc_hw_ep_handle_and_buff(dev, &kcm->capture_iacc_mono_ep,
		BUFF_BYTES_EACH_CHANNEL, 1, 48000);
	if (ret) {
		pr_err("Allocate IACC capture mono endpoint buffer failed.\n");
		goto error_alloc_capture_iacc_mono_ep_failed;
	}

	ret = alloc_hw_ep_handle_and_buff(dev, &kcm->playback_usp_sco_ep,
		BUFF_BYTES_USP_SCO_PLAYBACK, 1, 16000);
	if (ret) {
		pr_err("Allocate USP-SCO playback endpoint buffer failed.\n");
		goto error_alloc_playback_usp_sco_ep_failed;
	}
	ret = alloc_hw_ep_handle_and_buff(dev, &kcm->capture_usp_sco_ep,
		BUFF_BYTES_USP_SCO_CAPTURE, 1, 16000);
	if (ret) {
		pr_err("Allocate USP-SCO capture endpoint buffer failed.\n");
		goto error_alloc_capture_usp_sco_ep_failed;
	}
	ret = alloc_hw_ep_handle_and_buff(dev, &kcm->capture_iacc_sco_ep,
		BUFF_BYTES_IACC_SCO_CAPTURE, enable_2mic_cvc ? 2 : 1,
		48000);
	if (ret) {
		pr_err("Allocate IACC-SCO capture endpoint buffer failed.\n");
		goto error_alloc_capture_iacc_sco_ep_failed;
	}
	ret  = alloc_hw_ep_handle_and_buff(dev,
		&kcm->capture_iacc_stereo_ep, BUFF_BYTES_EACH_CHANNEL,
		2, 48000);
	if (ret) {
		pr_err("Allocate IACC(2ch) capture endpoint buffer failed.\n");
		goto error_alloc_capture_iacc_stereo_ep_failed;
	}
	ret  = alloc_hw_ep_handle_and_buff(dev,
		&kcm->capture_i2s_stereo_ep, BUFF_BYTES_EACH_CHANNEL,
		2, 48000);
	if (ret) {
		pr_err("Allocate I2S(2ch) capture endpoint buffer failed.\n");
		goto error_alloc_capture_i2s_stereo_ep_failed;
	}
	/*
	 * If i2s is master mode, that means the device is salve mode.
	 * The endpointer clock master configuration is set the external
	 * device clock mode. So if The i2s host is master mode, the endpoint
	 * clock mode must be set slave mode.
	 */
	kcm->capture_i2s_stereo_ep.clock_master = i2s_master ? 0 : 1;
	for (i = 0; i < USP_PORTS; i++) {
		ret  = alloc_hw_ep_handle_and_buff(dev,
			&kcm->capture_usp_stereo_ep[i], BUFF_BYTES_EACH_CHANNEL,
			2, 48000);
		if (ret) {
			pr_err("Allocate usp capture ep buffer failed.\n");
			goto error_alloc_capture_usp_stereo_ep_failed;
		}
		alloced_usp_ep++;
	}
	for (i = 0; i < PEQ_NUM_MAX; i++)
		memcpy(&music_peqs_default_params[i][3],
			peq_params_array_def, sizeof(peq_params_array_def));
	memcpy(music_dbe_default_params,
		dbe_params_array_def, sizeof(dbe_params_array_def));
	init_pipeline(bt_usp_port);

	return kcm;
error_alloc_capture_usp_stereo_ep_failed:
	for (i = 0; i < alloced_usp_ep; i++)
		free_hw_ep_handle_and_buff(dev, &kcm->capture_usp_stereo_ep[i]);
	free_hw_ep_handle_and_buff(dev, &kcm->capture_i2s_stereo_ep);
error_alloc_capture_i2s_stereo_ep_failed:
	free_hw_ep_handle_and_buff(dev, &kcm->capture_iacc_stereo_ep);
error_alloc_capture_iacc_stereo_ep_failed:
	free_hw_ep_handle_and_buff(dev, &kcm->capture_iacc_sco_ep);
error_alloc_capture_iacc_sco_ep_failed:
	free_hw_ep_handle_and_buff(dev, &kcm->capture_usp_sco_ep);
error_alloc_capture_usp_sco_ep_failed:
	free_hw_ep_handle_and_buff(dev, &kcm->playback_usp_sco_ep);
error_alloc_playback_usp_sco_ep_failed:
	free_hw_ep_handle_and_buff(dev, &kcm->capture_iacc_mono_ep);
error_alloc_capture_iacc_mono_ep_failed:
	free_hw_ep_handle_and_buff(dev, &kcm->playback_iacc_ep);
	return ERR_PTR(ret);
}

void kcm_deinit(struct device *dev)
{
	int i;

	free_hw_ep_handle_and_buff(dev, &kcm->capture_i2s_stereo_ep);
	free_hw_ep_handle_and_buff(dev, &kcm->capture_iacc_stereo_ep);
	free_hw_ep_handle_and_buff(dev, &kcm->capture_iacc_mono_ep);
	free_hw_ep_handle_and_buff(dev, &kcm->capture_iacc_sco_ep);
	free_hw_ep_handle_and_buff(dev, &kcm->capture_usp_sco_ep);
	free_hw_ep_handle_and_buff(dev, &kcm->playback_usp_sco_ep);
	free_hw_ep_handle_and_buff(dev, &kcm->playback_iacc_ep);
	for (i = 0; i < USP_PORTS; i++)
		free_hw_ep_handle_and_buff(dev, &kcm->capture_usp_stereo_ep[i]);
}
