/*
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

#ifndef _KAS_KCM_H
#define _KAS_KCM_H

#define EXEC_PHASE_HW_PARAMS		1
#define EXEC_PHASE_TRIGGER_START	2
#define EXEC_PHASE_TRIGGER_STOP		3
#define EXEC_PHASE_HW_FREE		4
#define EXEC_PHASE_HW_FREE_1		5
#define EXEC_PHASE_ACK			6

#define CONNECT_SINK			1
#define CONNECT_SOURCE			2

#define PIPELINE_READY	0
#define PIPELINE_BUSY	1

#define MUSIC_STREAM		0
#define NAVIGATION_STREAM		1
#define ALARM_STREAM			2
#define A2DP_STREAM			3
#define VOICECALL_BT_TO_IACC_STREAM	4
#define VOICECALL_PLAYBACK_STREAM	5
#define IACC_LOOPBACK_PLAYBACK_STREAM	6
#define I2S_TO_IACC_LOOPBACK_STREAM	7
#define ANALOG_CAPTURE_STREAM		8
#define VOICECALL_IACC_TO_BT_STREAM	9
#define VOICECALL_CAPTURE_STREAM	10
#define IACC_LOOPBACK_CAPTURE_STREAM	11
#define USP0_TO_IACC_LOOPBACK_STREAM	12
#define USP1_TO_IACC_LOOPBACK_STREAM	13
#define USP2_TO_IACC_LOOPBACK_STREAM	14

/* Virtual streams which are used mono and 4 channels music playback */
#define MUSIC_MONO_STREAM		16
#define MUSIC_STEREO_STREAM		17
#define MUSIC_4CHANNELS_STREAM		18
#define CAPTURE_MONO_STREAM		19
#define CAPTURE_STEREO_STREAM		20

#define TOTAL_SUPPORT_STREAMS		32


#include "ipc.h"
#include "usp-pcm.h"

struct hw_ep_handle_buff_t {
	struct endpoint_handle *handle;
	u32 handle_phy_addr;
	void *buff;
	u32 sample_rate;
	u16 channels;
	u32 audio_data_format;
	u32 packing_format;
	u32 interleaving_format;
	u32 clock_master;
	int buff_bytes;
};

struct kcm_t {
	struct hw_ep_handle_buff_t playback_iacc_ep;
	struct hw_ep_handle_buff_t capture_iacc_mono_ep;
	struct hw_ep_handle_buff_t capture_iacc_sco_ep;
	struct hw_ep_handle_buff_t playback_usp_sco_ep;
	struct hw_ep_handle_buff_t capture_usp_sco_ep;
	struct hw_ep_handle_buff_t capture_iacc_stereo_ep;
	struct hw_ep_handle_buff_t capture_i2s_stereo_ep;
	struct hw_ep_handle_buff_t capture_usp_stereo_ep[USP_PORTS];
	unsigned long running_pipeline;
};

struct component {
	u32 component_id;
	u32 params[32];
	u16 ret[16];
	int primary_stream;
	int create_refcnt;
	int running_refcnt;
	int id_count;
};

static inline u32 get24bit(u8 *buf, u16 pos)
{
#ifdef __LITTLE_ENDIAN
	if (pos % 2) {
		return buf[3 * pos + 1] + (buf[3 * pos + 2] << 8)
			+ (buf[3 * pos - 1] << 16);
	} else {
		return buf[3 * pos + 3] + (buf[3 * pos] << 8)
			+ (buf[3 * pos + 1] << 16);
	}
#else
	return buf[3 * pos] + (buf[3 * pos + 1] << 8)
			+ (buf[3 * pos - 1] << 16);
#endif
}

static inline void put24bit(u8 *buf, u16 pos, u32 data)
{
#ifdef __LITTLE_ENDIAN
	if (pos % 2) {
		buf[3 * pos + 1] = data;
		buf[3 * pos + 2] = data >> 8;
		buf[3 * pos - 1] = data >> 16;
	} else {
		buf[3 * pos + 3] = data;
		buf[3 * pos] = data >> 8;
		buf[3 * pos + 1] = data >> 16;
	}
#else
	buf[3 * pos] = data;
	buf[3 * pos + 1] = data >> 8;
	buf[3 * pos + 2] = data >> 16;
#endif
}

extern bool enable_2mic_cvc;
extern bool disable_uwb_cvc;

void set_default_music_delay_params(int offset, int val);
void set_default_music_dbe_params(int offset, int val);
void set_default_music_dbe_control(u16 mode);
void set_default_music_peq_params(int index, int offset, int val);
void set_default_music_peq_control(int index, u16 mode);
void set_default_music_passthrough_volume(u16 volume);
void set_default_volume_ctrl_volume(int channel, u32 volume);
void set_default_master_volume(u32 volume);
void set_default_mixer_stream_volume(int stream, u16 volume);
void set_default_mixer_stream_channel_volume(int stream, int channel,
	u16 volume);
u16 get_volume_control_op_id(void);
u16 get_mixer_op_id(int which);
u16 get_music_passthrough_op_id(void);
u16 get_peq_op_id(u16 index);
u16 get_dbe_op_id(u16 index);
u16 get_delay_op_id(void);
struct kcm_t *kcm_init(int bt_usp_port, struct device *dev, int i2s_master);
void kcm_deinit(struct device *dev);
int open_stream(int stream);
void close_stream(int stream);
u16 prepare_stream(int stream, int channels, u32 handle_addr, int sample_rate,
	int clock_master, int period_size);
int start_stream(int stream, int clock_master);
void stop_stream(int stream);
void destroy_stream(int stream);
int data_produced(u16 endpoint_id);
#endif
